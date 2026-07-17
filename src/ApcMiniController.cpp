#include "ApcMiniController.h"

#include <algorithm>
#include <cmath>

#include "FaderPickup.h"
#include "FaderTakeover.h"
#include "ofMain.h"

ApcMiniController::ApcMiniController() {
  padCurrentColors.fill(kColorOff);
  padLedRetryUntilMs.fill(0);
  for (auto& fs : faderStates) {
    fs.lastMidiValue = -1.0f;
  }
}

ApcMiniController::~ApcMiniController() {
  exit();
}

void ApcMiniController::queuePadLedUpdate(int padNote) {
  if (padNote < 0 || padNote >= kPadCount) return;
  std::lock_guard<std::mutex> lock(ledQueueMutex);
  pendingPadLedUpdates.push_back(padNote);
}

void ApcMiniController::flushQueuedLedUpdates() {
  std::vector<int> padNotes;

  {
    std::lock_guard<std::mutex> lock(ledQueueMutex);
    padNotes.swap(pendingPadLedUpdates);
  }

  if (padNotes.empty()) return;

  std::sort(padNotes.begin(), padNotes.end());
  padNotes.erase(std::unique(padNotes.begin(), padNotes.end()), padNotes.end());

  for (int note : padNotes) {
    updatePadLed(note);
  }
}

bool ApcMiniController::tryConnect() {
  if (connected) return true;

  midiIn.listInPorts();
  midiOut.listOutPorts();

  int inPortControl = -1;
  int inPortNotes = -1;
  int outPortNotes = -1;
  int outPortControl = -1;

  // Find input ports (Control + Notes)
  for (int i = 0; i < midiIn.getNumInPorts(); i++) {
    std::string name = midiIn.getInPortName(i);
    if (inPortControl < 0 && name.find(kInputPortPattern) != std::string::npos) {
      inPortControl = i;
      ofLogNotice("ApcMiniController") << "Found Control input port: " << name;
    }
    if (inPortNotes < 0 && name.find(kNotesPortPattern) != std::string::npos) {
      inPortNotes = i;
      ofLogNotice("ApcMiniController") << "Found Notes input port: " << name;
    }
  }

  // Find output ports
  for (int i = 0; i < midiOut.getNumOutPorts(); i++) {
    std::string name = midiOut.getOutPortName(i);
    if (name.find(kNotesPortPattern) != std::string::npos) {
      outPortNotes = i;
      ofLogNotice("ApcMiniController") << "Found Notes output port: " << name;
    }
    if (name.find(kControlPortPattern) != std::string::npos) {
      outPortControl = i;
      ofLogNotice("ApcMiniController") << "Found Control output port: " << name;
    }
  }

  if ((inPortControl < 0 && inPortNotes < 0) || outPortNotes < 0) {
    ofLogNotice("ApcMiniController") << "APC Mini MK2 not found";
    return false;
  }

  // Prefer the Control port for input (buttons + faders).
  // If it's not present, fall back to Notes.
  int primaryInPort = (inPortControl >= 0) ? inPortControl : inPortNotes;
  if (!midiIn.openPort(primaryInPort)) {
    ofLogWarning("ApcMiniController") << "Failed to open input port";
    return false;
  }
  midiIn.addListener(this);


  if (!midiOut.openPort(outPortNotes)) {
    ofLogWarning("ApcMiniController") << "Failed to open Notes output port";
    midiIn.closePort();
    return false;
  }

  // Also open Control output port if available (buttons + some devices accept LEDs here too)
  if (outPortControl >= 0) {
    if (midiOutControl.openPort(outPortControl)) {
      ofLogNotice("ApcMiniController") << "Opened Control output port";
    } else {
      ofLogWarning("ApcMiniController") << "Failed to open Control output port";
    }
  }

  connected = true;
  ofLogNotice("ApcMiniController") << "Connected to APC Mini MK2";

  // Clear all LEDs to known state on connection
  clearAllLeds();

  return true;
}

void ApcMiniController::disconnect() {
  if (!connected) return;

  // Best-effort: clear LEDs before closing ports.
  clearAllLeds();

  midiIn.removeListener(this);
  midiIn.closePort();
  midiOut.closePort();
  midiOutControl.closePort();
  connected = false;

  ofLogNotice("ApcMiniController") << "Disconnected from APC Mini MK2";
}

void ApcMiniController::update() {
  // Process queued MIDI events on the main thread first — all synth/navigator/
  // set work and hold-state management happens here, never on the MIDI thread.
  // Runs even without a synth so mid-switch events land promptly as the same
  // no-ops they always were (each handler null-checks synthPtr).
  drainMidiEvents();

  if (!synthPtr) return;

  uint64_t nowMs = ofGetElapsedTimeMillis();

  // A loaded set turns the grid into page-aware set cells + a meta row; with no
  // set the pads keep their buttonGrid behaviour unchanged.
  const bool setMode = synthPtr->getSetController().hasSet();

  // Apply any LED updates requested by the drained pad handlers early.
  // This improves perceived responsiveness for hold/amber feedback.
  flushQueuedLedUpdates();

  // While a pad is being held, resend amber at a gentle fixed rate.
  // This makes hold feedback resilient to occasional dropped SysEx.
  if (currentHold.active && currentHold.padNote >= 0 && currentHold.padNote < kPadCount) {
    if (lastHoldAmberSendMs == 0 || nowMs - lastHoldAmberSendMs >= 120) {
      setPadRgbBatch({{currentHold.padNote, kColorAmber}});
      padCurrentColors[currentHold.padNote] = kColorAmber;
      padLedRetryUntilMs[currentHold.padNote] = nowMs + 500;
      lastHoldAmberSendMs = nowMs;
    }
  } else {
    lastHoldAmberSendMs = 0;
  }

  // === Set mode: apply meta-row intents, follow page changes, track memory
  // readiness. Navigator/hibernation LED tracking below is skipped — set cells
  // show their set colour, not a current-config highlight. ===
  if (setMode) {
    auto& set = synthPtr->getSetController();

    // Meta-row page switch (recorded by the pad-event drain). setCurrentPage
    // fires pageChanged only on an actual change, raising pendingSetFullRepaint.
    int requestedPage = pendingSetPageRequest.exchange(-1);
    if (requestedPage >= 0) {
      set.setCurrentPage(requestedPage);
    }

    // Meta-row HOME: load the set's safe config. The grid layout is unchanged by
    // a config load (cell colours come from the set file), so no full repaint.
    if (pendingSetHomeRequest.exchange(false)) {
      synthPtr->loadSetCellConfig(set.homeConfig());
    }

    // Defensive page-change poll: a backstop alongside the pageChanged event —
    // polling currentPage() here guarantees the LED refresh even if the event
    // subscription is ever disturbed across synth reloads.
    const int page = set.currentPage();
    if (page != lastKnownSetPage) {
      lastKnownSetPage = page;
      pendingSetFullRepaint = true;
    }

    // The one permitted full-grid rewrite, as a single batch pass.
    if (pendingSetFullRepaint.exchange(false)) {
      updateAllPadLeds();
      lastKnownMemoryReady = isMemoryReady();  // resync so the block below is a no-op
    }

    // Memory readiness crossing: repaint only the memoryDependent cells, and only
    // on the transition (delta writes, never per-frame).
    const bool ready = isMemoryReady();
    if (ready != lastKnownMemoryReady) {
      lastKnownMemoryReady = ready;
      for (const auto& cell : set.cellsForCurrentPage()) {
        if (cell.memoryDependent) {
          updatePadLed(xyToPadNote(cell.x, cell.y));
        }
      }
    }
  }

  // Track external config changes (e.g. keyboard / other controller)
  if (!setMode) {
    auto& nav = synthPtr->getPerformanceNavigator();
    int currentIdx = nav.getCurrentIndex();
    bool needsRecompute = (currentIdx != lastKnownConfigIndex);
    if (!needsRecompute && currentIdx >= 0) {
      if (currentConfigPadNote < 0 || currentConfigPadNote >= kPadCount ||
          padConfigMap[currentConfigPadNote].configIndex != currentIdx) {
        needsRecompute = true;
      }
    }


    if (needsRecompute) {
      int previousPad = currentConfigPadNote;
      currentConfigPadNote = -1;
      for (int i = 0; i < kPadCount; i++) {
        if (padConfigMap[i].configIndex == currentIdx) {
          currentConfigPadNote = i;
          break;
        }
      }

      if (previousPad >= 0 && previousPad < kPadCount) {
        updatePadLed(previousPad);
      }
      if (currentConfigPadNote >= 0 && currentConfigPadNote < kPadCount) {
        updatePadLed(currentConfigPadNote);
      }

      lastKnownConfigIndex = currentIdx;

      // Debounced full repaint after config changes.
      // This recovers from occasional dropped SysEx chunks during big updates.
      pendingFullPadRepaintAtMs = nowMs + 120;
    }
  }

  // Track hibernation state changes so current config brightness updates
  if (!setMode) {
    int hibState = static_cast<int>(synthPtr->getHibernationState());
    if (hibState != lastKnownHibState) {
      if (currentConfigPadNote >= 0 && currentConfigPadNote < kPadCount) {
        updatePadLed(currentConfigPadNote);
      }
      lastKnownHibState = hibState;
    }

    // Startup robustness: if we're not hibernated, keep the current-config pad in sync.
    if (hibState != static_cast<int>(ofxMarkSynth::HibernationController::State::HIBERNATED)) {
      auto& nav = synthPtr->getPerformanceNavigator();
      int currentIdx = nav.getCurrentIndex();

      if (currentIdx >= 0 && (currentConfigPadNote < 0 || currentConfigPadNote >= kPadCount ||
                              padConfigMap[currentConfigPadNote].configIndex != currentIdx)) {
        currentConfigPadNote = -1;
        for (int i = 0; i < kPadCount; i++) {
          if (padConfigMap[i].configIndex == currentIdx) {
            currentConfigPadNote = i;
            break;
          }
        }
      }

      if (currentConfigPadNote >= 0 && currentConfigPadNote < kPadCount) {
        updatePadLed(currentConfigPadNote);
      }
    }
  }

  // Check for hold timeout
  if (currentHold.active) {
    uint64_t elapsed = nowMs - currentHold.startTimeMs;
    // Publish hold-to-commit progress (0..1) so the GUI can show how close the
    // hold is to committing. Set mode has no navigator preview.
    if (!setMode) {
      float p = static_cast<float>(elapsed) / static_cast<float>(kHoldThresholdMs);
      synthPtr->getPerformanceNavigator().setPreviewProgress(p > 1.0f ? 1.0f : p);
    }
    if (elapsed >= kHoldThresholdMs) {
      // Trigger config switch
      int heldPad = currentHold.padNote;

      if (setMode) {
        // A held set cell (y=0..6) commits to loading its config. Meta-row pads
        // never start a hold, so heldPad always resolves to a cell here.
        int x, y;
        padNoteToXY(heldPad, x, y);
        const auto* cell = synthPtr->getSetController().cellAt(x, y);

        currentHold.active = false;
        currentHold.padNote = -1;

        if (cell != nullptr) {
          synthPtr->loadSetCellConfig(cell->config);
          ofLogNotice("ApcMiniController") << "Set cell config load: " << cell->config;
        }
        // Set-cell colours don't change on load; just restore this pad from amber.
        updatePadLed(heldPad);
      } else {
        int configIndex = padConfigMap[heldPad].configIndex;

        // Clear hold state first
        currentHold.active = false;
        currentHold.padNote = -1;

        if (configIndex >= 0) {
          // Trigger the config switch.
          auto& nav = synthPtr->getPerformanceNavigator();
          nav.clearPreviewConfig(); // commit reached — drop the held-cell preview
          nav.jumpTo(configIndex);
          ofLogNotice("ApcMiniController") << "Config jump triggered to index " << configIndex;

          // Defer a full repaint slightly to avoid overwhelming SysEx during transitions.
          pendingFullPadRepaintAtMs = nowMs + 180;
        } else {
          // Refresh LEDs after ending hold (if still relevant)
          updatePadLed(heldPad);
        }
      }
    }
  }

  if (pendingFullPadRepaintAtMs != 0 && nowMs >= pendingFullPadRepaintAtMs) {
    pendingFullPadRepaintAtMs = 0;
    updateAllPadLeds();
  }

  // Keep hold feedback + recent changes reliable.
  if (currentHold.active && currentHold.padNote >= 0 && currentHold.padNote < kPadCount) {
    padLedRetryUntilMs[currentHold.padNote] = nowMs + 500;
  }
  processPadLedRetries();
}

void ApcMiniController::exit() {
  // Avoid repeated/slow LED clear attempts on quit.
  // A single best-effort clear happens in disconnect().
  disconnect();
  synthPtr.reset();
}

void ApcMiniController::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synth) {
  this->synthPtr = synth;

  // Try to connect if not already
  if (!connected) {
    tryConnect();
  }

  if (!connected) {
    ofLogNotice("ApcMiniController") << "Synth loaded but APC Mini not connected";
    return;
  }

  // Always start from a known dark state before painting the pad layout.
  // This avoids any stale LEDs surviving across performance/config loads.
  clearAllLeds();
  for (auto& c : padCurrentColors) {
    c = kColorOff;
  }
  currentHold.active = false;
  currentHold.padNote = -1;

  auto& nav = synthPtr->getPerformanceNavigator();

  // Build pad-to-config mapping from the navigator's config list
  buildPadConfigMap();

  // Find which pad corresponds to the current config
  currentConfigPadNote = -1;
  int currentIdx = nav.getCurrentIndex();
  lastKnownConfigIndex = currentIdx;
  lastKnownHibState = static_cast<int>(synthPtr->getHibernationState());
  for (int i = 0; i < kPadCount; i++) {
    if (padConfigMap[i].configIndex == currentIdx) {
      currentConfigPadNote = i;
      break;
    }
  }

  // Reset fader pickup states so they re-acquire against the new config's
  // parameter values.
  resetFaderPickupStates();

  // Set-mode wiring: subscribe to the multi-listener pageChanged event (RAII
  // token — re-registering on each synth load replaces our own listener only,
  // never another consumer's). The update() poll of currentPage() remains as a
  // defensive backstop. The lambda only flips an atomic, so it is safe from any
  // thread and outlives this synth (the controller is longer-lived than the set).
  {
    auto& set = synthPtr->getSetController();
    pageChangedListener = set.pageChanged.newListener([this]() { pendingSetFullRepaint = true; });
    pendingSetPageRequest = -1;
    pendingSetHomeRequest = false;
    pendingSetFullRepaint = false;
    lastKnownSetPage = set.hasSet() ? set.currentPage() : -1;
    lastKnownMemoryReady = set.hasSet() ? isMemoryReady() : false;
  }

  // Update all LEDs
  updateAllPadLeds();
  dimInactiveControls();
}

void ApcMiniController::onSynthWillUnload() {
  // Ensure the device is dark when no synth is active
  if (connected) {
    clearAllLeds();
  }

  // Reset fader pickup so they re-acquire on the next config.
  resetFaderPickupStates();

  // Clear cached LED state
  for (auto& c : padCurrentColors) {
    c = kColorOff;
  }

  // Clear pending set-mode intents. The pageChanged listener token persists:
  // this fires on every config switch (the SetController persists across them),
  // and the callback only flips an app-lifetime atomic, so it can never dangle.
  // onSynthDidLoad re-registers it and re-baselines the tracking below.
  pendingSetPageRequest = -1;
  pendingSetHomeRequest = false;
  pendingSetFullRepaint = false;
  lastKnownSetPage = -1;
  lastKnownMemoryReady = false;

  synthPtr.reset();
  currentConfigPadNote = -1;
  lastKnownConfigIndex = -1;
  lastKnownHibState = -1;
}

void ApcMiniController::newMidiMessage(ofxMidiMessage& message) {
  if (!connected) return;

  // Queue events for processing on the main thread. This keeps the MIDI
  // listener thread away from the synth's parameter tree, the navigator, the
  // set controller and the pad config map — all owned (and rebuilt across
  // config switches) by the main thread.
  switch (message.status) {
    case MIDI_NOTE_ON:
    case MIDI_NOTE_OFF:
    case MIDI_CONTROL_CHANGE:
      break;
    default:
      return;  // Statuses we never handle don't occupy ring slots.
  }

  int writeIndex = midiEventWriteIndex.load();
  int nextIndex = (writeIndex + 1) % kMidiEventBufferSize;
  if (nextIndex == midiEventReadIndex.load()) {
    // Ring full (a long main-thread hitch, e.g. mid config load). Drop the
    // incoming event but raise the overflow flag so the drain fails safe — a
    // lost pad release must never leave a hold armed to commit.
    midiEventOverflow.store(true);
    return;
  }

  const bool isCC = (message.status == MIDI_CONTROL_CHANGE);
  midiEventBuffer[writeIndex] = {
    message.status,
    isCC ? message.control : message.pitch,
    isCC ? message.value : message.velocity,
  };
  midiEventWriteIndex.store(nextIndex);
}

void ApcMiniController::drainMidiEvents() {
  // Overflow fail-safe: the producer had to drop events, possibly including a
  // pad release. Cancel any active hold and clear the held-cell preview so the
  // worst case is a hold the performer must re-press — never an unintended
  // hold-to-commit config switch from a lost note-off.
  if (midiEventOverflow.exchange(false)) {
    ofLogWarning("ApcMiniController") << "MIDI event ring overflowed; cancelling any active hold";
    if (currentHold.active) {
      int heldPad = currentHold.padNote;
      currentHold.active = false;
      currentHold.padNote = -1;
      if (heldPad >= 0 && heldPad < kPadCount) {
        // Force a send even if our cached color is stale.
        padCurrentColors[heldPad] = kColorOff;
        queuePadLedUpdate(heldPad);
      }
    }
    if (synthPtr && !synthPtr->getSetController().hasSet()) {
      synthPtr->getPerformanceNavigator().clearPreviewConfig();
    }
  }

  int writeIndex = midiEventWriteIndex.load();
  int readIndex = midiEventReadIndex.load();
  while (readIndex != writeIndex) {
    const auto& event = midiEventBuffer[readIndex];

    switch (event.status) {
      case MIDI_NOTE_ON:
        if (event.data2 > 0) {
          handleNoteOn(event.data1, event.data2);
        } else {
          handleNoteOff(event.data1);
        }
        break;
      case MIDI_NOTE_OFF:
        handleNoteOff(event.data1);
        break;
      case MIDI_CONTROL_CHANGE:
        handleCC(event.data1, event.data2);
        break;
      default:
        break;
    }

    // Publish the new read position only after the slot is consumed — once it
    // advances, the producer is free to reuse that slot.
    readIndex = (readIndex + 1) % kMidiEventBufferSize;
    midiEventReadIndex.store(readIndex);
  }
}

void ApcMiniController::handleNoteOn(int note, int velocity) {
  // Config pads (rows 0-7, notes 0-63) — the only interactive input now.
  if (note >= kConfigPadNoteFirst && note <= kConfigPadNoteLast) {
    onPadPressed(note);
    return;
  }

  // Physical Track buttons (100-107), side buttons, and Shift are all
  // unused — ignore.
}

void ApcMiniController::handleNoteOff(int note) {
  // Config pads (rows 0-7, notes 0-63) - need release for hold-to-confirm
  if (note >= kConfigPadNoteFirst && note <= kConfigPadNoteLast) {
    onPadReleased(note);
    return;
  }

  // All other notes are unused.
}

void ApcMiniController::handleCC(int cc, int value) {
  // Faders 1-3 (CC 48-50) drive the three top-of-sidebar synth controls.
  // Faders 4-9 (CC 51-56) are unbound and silently dropped.
  if (cc >= kFaderCCFirst && cc <= kFaderCCLast) {
    int faderIndex = cc - kFaderCCFirst;
    if (faderIndex < kBoundFaderCount) {
      handleFaderCC(faderIndex, value);
    }
    return;
  }
}

void ApcMiniController::handleFaderCC(int faderIndex, int value) {
  if (!synthPtr) return;
  if (faderIndex < 0 || faderIndex >= kBoundFaderCount) return;

  const char* paramName = kFaderBindings[faderIndex];
  if (paramName == nullptr) return;

  auto paramWrapper = synthPtr->findParameterByNamePrefix(paramName);
  if (paramWrapper == std::nullopt) {
    // Param not present in the current config — silent no-op.
    return;
  }

  ofParameter<float>& param = paramWrapper->get().cast<float>();
  applyPickup(param, value, faderStates[faderIndex].lastMidiValue);
}

void ApcMiniController::resetFaderPickupStates() {
  for (auto& fs : faderStates) {
    fs.lastMidiValue = -1.0f;
  }
}

// === Pad Grid ===

void ApcMiniController::buildPadConfigMap() {
  // Clear existing map
  for (auto& info : padConfigMap) {
    info.configIndex = -1;
    info.color = kColorOff;
    info.isAssigned = false;
  }

  if (!synthPtr) return;

  auto& nav = synthPtr->getPerformanceNavigator();
  const auto& configs = nav.getConfigs();

  // Populate from the navigator's resolved 8x8 grid (y=0 is top row, y=7 bottom).
  for (int y = 0; y < ofxMarkSynth::PerformanceNavigator::GRID_HEIGHT; ++y) {
    for (int x = 0; x < ofxMarkSynth::PerformanceNavigator::GRID_WIDTH; ++x) {
      const int configIdx = nav.getGridConfigIndex(x, y);
      if (configIdx < 0 || configIdx >= static_cast<int>(configs.size())) continue;

      const int padNote = xyToPadNote(x, y);
      if (padNote < kConfigPadNoteFirst || padNote > kConfigPadNoteLast) continue;

      const auto c = nav.getConfigGridColor(configIdx);
      const RgbColor color { c.r, c.g, c.b };

      padConfigMap[padNote].configIndex = configIdx;
      padConfigMap[padNote].configPath = configs[configIdx];
      padConfigMap[padNote].color = color;
      padConfigMap[padNote].isAssigned = true;
    }
  }

  ofLogNotice("ApcMiniController") << "Built pad config map for " << configs.size() << " configs";
}

void ApcMiniController::updateAllPadLeds() {
  if (!connected) return;

  uint64_t nowMs = ofGetElapsedTimeMillis();

  // Paint all config pads every time (64 pads).
  // This avoids getting "stuck" when the device drops a SysEx but our cached
  // padCurrentColors thinks it succeeded.
  std::vector<std::pair<int, RgbColor>> updates;
  updates.reserve(kConfigPadCount);

  for (int padNote = kConfigPadNoteFirst; padNote <= kConfigPadNoteLast; padNote++) {
    RgbColor color = getPadDisplayColor(padNote);
    updates.push_back({padNote, color});
    padCurrentColors[padNote] = color;

    // Retry window for dropped chunks (covers a full scan of all pads).
    padLedRetryUntilMs[padNote] = nowMs + 1500;
  }

  setPadRgbBatch(updates);
}


void ApcMiniController::updatePadLed(int padNote) {
  if (!connected || padNote < 0 || padNote >= kPadCount) return;

  RgbColor color = getPadDisplayColor(padNote);
  if (color != padCurrentColors[padNote]) {
    setPadRgb(padNote, color);
    padCurrentColors[padNote] = color;

    // Some SysEx updates get dropped; retry for a moment.
    padLedRetryUntilMs[padNote] = ofGetElapsedTimeMillis() + 350;
  }
}

void ApcMiniController::processPadLedRetries() {
  if (!connected) return;

  uint64_t nowMs = ofGetElapsedTimeMillis();

  // Trickle retries to avoid overwhelming the device/driver.
  if (nowMs - lastPadLedRetrySendMs < 40) return;

  static constexpr size_t kMaxRetryPadsPerTick = 12;

  std::vector<std::pair<int, RgbColor>> updates;
  updates.reserve(kMaxRetryPadsPerTick);

  auto maybeAdd = [&](int padNote) {
    if (padNote < 0 || padNote >= kPadCount) return;
    if (padLedRetryUntilMs[padNote] <= nowMs) return;
    for (const auto& [n, _] : updates) {
      if (n == padNote) return;
    }
    updates.push_back({padNote, padCurrentColors[padNote]});
  };

  // Priority: held pad and current-config pad.
  if (currentHold.active) {
    maybeAdd(currentHold.padNote);
  }
  maybeAdd(currentConfigPadNote);

  // Fair scan across all pads.
  for (int i = 0; i < kPadCount && updates.size() < kMaxRetryPadsPerTick; i++) {
    int padNote = (retryScanStart + i) % kPadCount;
    maybeAdd(padNote);
  }

  retryScanStart = (retryScanStart + 1) % kPadCount;

  if (!updates.empty()) {
    lastPadLedRetrySendMs = nowMs;
    setPadRgbBatch(updates);
  }
}


ApcMiniController::RgbColor ApcMiniController::scaleRgb(const RgbColor& c, float factor) {
  auto clamp255 = [](float v) -> uint8_t {
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return static_cast<uint8_t>(v);
  };
  return {
    clamp255(static_cast<float>(c.r) * factor),
    clamp255(static_cast<float>(c.g) * factor),
    clamp255(static_cast<float>(c.b) * factor),
  };
}

bool ApcMiniController::isMemoryReady() const {
  return synthPtr && MemoryReadyPolicy::isReady(*synthPtr);
}

ApcMiniController::RgbColor ApcMiniController::getSetPadDisplayColor(int padNote) const {
  // Same orientation as the buttonGrid: xyToPadNote maps y=0 to the top physical
  // row, so set y=7 lands on the bottom row — the meta row.
  int x, y;
  padNoteToXY(padNote, x, y);

  const auto& set = synthPtr->getSetController();

  if (y == kMetaRowY) {
    // Meta row: pages (x=0..3) amber — current bright, other valid pages dim;
    // HOME (x=7) amber-bright. Everything else dark. The APC can't ring, so
    // brightness alone carries the cue.
    if (x >= kMetaPageXFirst && x <= kMetaPageXLast) {
      if (x >= set.pageCount()) return kColorOff;      // page not present
      if (x == set.currentPage()) return kColorAmber;   // current page: bright
      return scaleRgb(kColorAmber, kConfigDimFactor);   // other valid page: dim
    }
    if (x == kMetaHomeX) return kColorAmber;            // HOME: bright
    return kColorOff;
  }

  // Cell rows (y=0..6). Unassigned pads stay dark.
  const auto* cell = set.cellAt(x, y);
  if (cell == nullptr) return kColorOff;

  const RgbColor base { cell->color.r, cell->color.g, cell->color.b };

  // memoryDependent cells render dimmed until the MemoryBank has collected
  // enough textures. Home cells are already the brightest of their world (the
  // builder guarantees it), so at rest they just show their colour.
  if (cell->memoryDependent && !isMemoryReady()) {
    return scaleRgb(base, kMemoryDimFactor);
  }
  return base;
}

ApcMiniController::RgbColor ApcMiniController::getPadDisplayColor(int padNote) const {
  // Currently holding this pad — amber (both modes; holds only start on
  // assigned/actionable pads).
  if (currentHold.active && currentHold.padNote == padNote) {
    return kColorAmber;
  }

  // Set mode: page-aware set cells + meta row replace the buttonGrid display.
  if (synthPtr && synthPtr->getSetController().hasSet()) {
    return getSetPadDisplayColor(padNote);
  }

  const auto& info = padConfigMap[padNote];

  // No config assigned
  if (!info.isAssigned) {
    return kColorOff;
  }

  // Determine whether this pad's config is the current one.
  // Prefer the navigator index (robust against path normalization / symlinks).
  bool isCurrentConfig = false;
  if (synthPtr && info.isAssigned) {
    const auto& nav = synthPtr->getPerformanceNavigator();
    int currentIdx = nav.getCurrentIndex();
    isCurrentConfig = (currentIdx >= 0 && info.configIndex == currentIdx);

    // Fallback (extra safety): direct path match.
    if (!isCurrentConfig && !info.configPath.empty()) {
      const auto& currentPath = synthPtr->getConfigSubsystem().getCurrentConfigPath();
      isCurrentConfig = (!currentPath.empty() && currentPath == info.configPath);
    }
  }

  // Current config should always be full strength (regardless of hibernation).
  if (isCurrentConfig) {
    return info.color;
  }

  // Non-current configs show a dimmed version of their color.
  return scaleRgb(info.color, kConfigDimFactor);
}

void ApcMiniController::onPadPressed(int padNote) {
  // Set mode overrides the buttonGrid resolution entirely.
  if (synthPtr && synthPtr->getSetController().hasSet()) {
    onSetPadPressed(padNote);
    return;
  }

  const auto& info = padConfigMap[padNote];
  if (!info.isAssigned) return;

  // Start hold timer
  currentHold.active = true;
  currentHold.padNote = padNote;
  currentHold.startTimeMs = ofGetElapsedTimeMillis();

  // Publish a transient preview of the held cell so the GUI can show what's
  // behind it during the hold-to-commit window. Cleared on release/commit.
  if (synthPtr && info.configIndex >= 0) {
    auto& nav = synthPtr->getPerformanceNavigator();
    nav.setPreviewProgress(0.0f);
    nav.setPreviewConfig(info.configIndex);
  }

  // Force a send even if our cached color is stale.
  padCurrentColors[padNote] = kColorOff;

  // LED updates are applied on the main thread in update().
  queuePadLedUpdate(padNote);

  // If the first SysEx is dropped, keep retrying while held.
  padLedRetryUntilMs[padNote] = ofGetElapsedTimeMillis() + 500;
}

void ApcMiniController::onSetPadPressed(int padNote) {
  // Only records intent / arms a hold. Config loads and page switches are
  // applied at a fixed point in update(), after this drain.
  int x, y;
  padNoteToXY(padNote, x, y);
  auto& set = synthPtr->getSetController();

  if (y == kMetaRowY) {
    // Meta row = instant taps (no hold, no press-time LED change).
    if (x >= kMetaPageXFirst && x <= kMetaPageXLast) {
      if (x < set.pageCount()) {
        pendingSetPageRequest = x;  // 0-based page; only pages that exist are active
      }
    } else if (x == kMetaHomeX) {
      pendingSetHomeRequest = true;
    }
    return;
  }

  // Cell rows (y=0..6): unassigned pads do nothing.
  if (set.cellAt(x, y) == nullptr) return;

  // Hold-to-confirm the config load — the same live-performance guard the
  // buttonGrid path uses (an accidental brush must not swap the whole config).
  currentHold.active = true;
  currentHold.padNote = padNote;
  currentHold.startTimeMs = ofGetElapsedTimeMillis();

  // Force an amber send even if our cached colour is stale.
  padCurrentColors[padNote] = kColorOff;
  queuePadLedUpdate(padNote);
  padLedRetryUntilMs[padNote] = ofGetElapsedTimeMillis() + 500;
}

void ApcMiniController::onPadReleased(int padNote) {
  // Clear the held-cell preview on any pad release (navigator only — set mode
  // has no navigator preview).
  if (synthPtr && !synthPtr->getSetController().hasSet()) {
    synthPtr->getPerformanceNavigator().clearPreviewConfig();
  }
  if (currentHold.active && currentHold.padNote == padNote) {
    // Released before threshold - cancel hold.
    currentHold.active = false;
    currentHold.padNote = -1;

    // Force a send even if our cached color is stale.
    padCurrentColors[padNote] = kColorOff;

    // LED updates are applied on the main thread in update().
    queuePadLedUpdate(padNote);
  }
}

// === LED Control ===

void ApcMiniController::clearAllLeds() {
  if (!connected) return;

  // Clear all 64 pads (all config pads, including the bottom row)
  std::vector<std::pair<int, RgbColor>> padUpdates;
  for (int i = 0; i < kPadCount; i++) {
    padUpdates.push_back({i, kColorOff});
    padCurrentColors[i] = kColorOff;
    padLedRetryUntilMs[i] = 0;
  }
  setPadRgbBatch(padUpdates);

  // Turn off physical bottom buttons (RED-only LEDs, notes 100-107)
  if (midiOutControl.isOpen()) {
    for (int i = 0; i < kBottomButtonCount; i++) {
      midiOutControl.sendNoteOn(1, kBottomButtonNoteFirst + i, 0);
    }
  }

  // Clear side buttons (GREEN-only LEDs, notes 112-119)
  for (int i = 0; i < kSideButtonCount; i++) {
    setSideButtonLed(i, kColorOff);
  }

  // Clear shift button
  if (midiOutControl.isOpen()) {
    midiOutControl.sendNoteOn(1, kShiftButtonNote, 0);
  } else if (midiOut.isOpen()) {
    midiOut.sendNoteOn(1, kShiftButtonNote, 0);
  }

  ofLogNotice("ApcMiniController") << "Cleared all LEDs";
}



void ApcMiniController::setPadRgb(int padNote, const RgbColor& color) {
  if (!connected || padNote < 0 || padNote >= kPadCount) return;

  auto [rMsb, rLsb] = toMsbLsb(color.r);
  auto [gMsb, gLsb] = toMsbLsb(color.g);
  auto [bMsb, bLsb] = toMsbLsb(color.b);

  std::vector<uint8_t> data = {
    static_cast<uint8_t>(padNote),  // from
    static_cast<uint8_t>(padNote),  // to (same pad)
    rMsb, rLsb,
    gMsb, gLsb,
    bMsb, bLsb
  };

  sendSysex(kSysexRgbMessageType, data);
}

void ApcMiniController::setPadRgbBatch(const std::vector<std::pair<int, RgbColor>>& pads) {
  if (!connected || pads.empty()) return;

  // Reliability: some APC Mini MK2 firmware/OS combos appear to drop or partially
  // apply very large SysEx messages. Send in small chunks.
  static constexpr size_t kMaxPadsPerMessage = 4;

  std::vector<uint8_t> data;
  data.reserve(kMaxPadsPerMessage * 8);

  for (size_t i = 0; i < pads.size(); ) {
    data.clear();

    size_t padsAdded = 0;
    for (; i < pads.size() && padsAdded < kMaxPadsPerMessage; ++i) {
      const auto& [padNote, color] = pads[i];
      if (padNote < 0 || padNote >= kPadCount) continue;

      auto [rMsb, rLsb] = toMsbLsb(color.r);
      auto [gMsb, gLsb] = toMsbLsb(color.g);
      auto [bMsb, bLsb] = toMsbLsb(color.b);

      data.push_back(static_cast<uint8_t>(padNote));  // from
      data.push_back(static_cast<uint8_t>(padNote));  // to
      data.push_back(rMsb);
      data.push_back(rLsb);
      data.push_back(gMsb);
      data.push_back(gLsb);
      data.push_back(bMsb);
      data.push_back(bLsb);
      padsAdded++;
    }

    if (!data.empty()) {
      sendSysex(kSysexRgbMessageType, data);

      // Give the device/driver a moment to consume SysEx bursts.
      // Without this, large multi-chunk updates (like the config grid repaint)
      // can be partially dropped, leaving stale/off LEDs.
      if (i < pads.size()) {
        ofSleepMillis(2);
      }
    }
  }
}

void ApcMiniController::setSideButtonLed(int buttonIndex, const RgbColor& color) {
  if (!connected || buttonIndex < 0 || buttonIndex >= kSideButtonCount) return;

  int note = kSideButtonNoteFirst + buttonIndex;
  
  // Side buttons use indexed colors via Note On velocity
  // For inactive buttons, just turn them off (velocity 0)
  int velocity = 0;
  if (color == kColorOff || color == kColorDimGray) {
    velocity = 0;  // Off
  } else {
    velocity = 1;  // Dim white if somehow needed
  }
  
  if (midiOutControl.isOpen()) {
    midiOutControl.sendNoteOn(1, note, velocity);
  } else {
    midiOut.sendNoteOn(1, note, velocity);
  }
}

void ApcMiniController::dimInactiveControls() {
  if (!connected) return;

  // Dim all side buttons (use indexed color via note velocity)
  for (int i = 0; i < kSideButtonCount; i++) {
    setSideButtonLed(i, kColorDimGray);
  }

  // Dim shift button (use indexed color via note velocity)
  if (midiOutControl.isOpen()) {
    midiOutControl.sendNoteOn(1, kShiftButtonNote, 0);
  } else {
    midiOut.sendNoteOn(1, kShiftButtonNote, 0);
  }
}

// === Sysex Helpers ===

void ApcMiniController::sendSysex(uint8_t messageType, const std::vector<uint8_t>& data) {
  if (!connected) return;

  size_t len = data.size();
  uint8_t lenMsb = static_cast<uint8_t>(len / 128);
  uint8_t lenLsb = static_cast<uint8_t>(len % 128);

  std::vector<unsigned char> message;
  message.reserve(7 + len + 1);

  message.push_back(0xF0);  // Sysex start
  message.push_back(kManufacturerId);
  message.push_back(kDeviceId);
  message.push_back(kModelId);
  message.push_back(messageType);
  message.push_back(lenMsb);
  message.push_back(lenLsb);

  for (uint8_t b : data) {
    message.push_back(b);
  }

  message.push_back(0xF7);  // Sysex end

  // Send RGB SysEx via the Control output port (Notes fallback).
  // Empirically, some setups only apply RGB SysEx on the Control port.
  if (midiOutControl.isOpen()) {
    midiOutControl.sendMidiBytes(message);
  } else if (midiOut.isOpen()) {
    midiOut.sendMidiBytes(message);
  }
}

std::pair<uint8_t, uint8_t> ApcMiniController::toMsbLsb(uint8_t value) {
  return {static_cast<uint8_t>(value / 128), static_cast<uint8_t>(value % 128)};
}

ApcMiniController::RgbColor ApcMiniController::parseHexColor(const std::string& hex) {
  RgbColor color = kColorDefaultConfig;

  std::string h = hex;
  if (!h.empty() && h[0] == '#') {
    h = h.substr(1);
  }

  if (h.length() == 6) {
    try {
      unsigned int rgb = std::stoul(h, nullptr, 16);
      color.r = (rgb >> 16) & 0xFF;
      color.g = (rgb >> 8) & 0xFF;
      color.b = rgb & 0xFF;
    } catch (...) {
      ofLogWarning("ApcMiniController") << "Failed to parse hex color: " << hex;
    }
  }

  return color;
}

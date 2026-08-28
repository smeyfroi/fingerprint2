#include "ApcMiniController.h"

#include <algorithm>
#include <cmath>

#include "FaderPickup.h"
#include "FaderTakeover.h"
#include "ofMain.h"

ApcMiniController::ApcMiniController() {
  padCurrentColors.fill(kColorOff);
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

  // A loaded set turns the grid into page-aware set cells (all 8 rows) with
  // paging on the track buttons; with no set the pads keep their buttonGrid
  // behaviour unchanged.
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
      lastHoldAmberSendMs = nowMs;
    }
  } else {
    lastHoldAmberSendMs = 0;
  }

  // === Set mode: apply pager intents, follow page changes, track memory
  // readiness and the active cell (bright white). The buttonGrid navigator/
  // hibernation tracking below is skipped. ===
  if (setMode) {
    auto& set = synthPtr->getSetController();

    // Pager buttons (recorded by the MIDI drain): ▲ requested an absolute page,
    // ◄/► accumulated a delta. setCurrentPage clamps internally and fires
    // pageChanged only on an actual change, raising pendingSetFullRepaint.
    int requestedPage = pendingSetPageRequest.exchange(-1);
    if (requestedPage >= 0) {
      set.setCurrentPage(requestedPage);
    }
    int pageDelta = pendingSetPageDelta.exchange(0);
    if (pageDelta != 0) {
      set.setCurrentPage(set.currentPage() + pageDelta);
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

    // Memory readiness crossing: repaint only the memoryDependent CONFIG cells,
    // and only on the transition (delta writes, never per-frame). Snapshot
    // cells never memory-dim, so they are skipped by kind (engine-side their
    // memoryDependent is always false, but don't rely on it).
    const bool ready = isMemoryReady();
    if (ready != lastKnownMemoryReady) {
      lastKnownMemoryReady = ready;
      for (const auto& cell : set.cellsForCurrentPage()) {
        if (cell.kind == ofxMarkSynth::SetController::CellKind::Config && cell.memoryDependent) {
          updatePadLed(xyToPadNote(cell.x, cell.y));
        }
      }
    }

    // Active-cell highlight: repaint the old and new active pads when the
    // loaded config changes (however it was switched — pad, GUI, keyboard).
    // Config cells only: a snapshot cell never wears the active white (its
    // config stem, if any, is forward state, not a load target).
    const std::string stem = currentConfigStem();
    if (stem != lastKnownSetConfigStem) {
      for (const auto& cell : set.cellsForCurrentPage()) {
        if (cell.kind != ofxMarkSynth::SetController::CellKind::Config) continue;
        if (cell.config == stem || cell.config == lastKnownSetConfigStem) {
          updatePadLed(xyToPadNote(cell.x, cell.y));
        }
      }
      lastKnownSetConfigStem = stem;
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
    // Publish hold-to-commit progress (0..1) so the GUI can show how close the hold is to
    // committing. Both modes now publish a preview (set mode gained one 2026-08-01), so the
    // progress ring is no longer gated on buttonGrid mode.
    {
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
          // Drop the held-cell preview at the commit, exactly as the buttonGrid path does —
          // otherwise it would linger over the config it just finished loading.
          synthPtr->getPerformanceNavigator().clearPreviewConfig();
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

  servicePadLeds();
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
  snapshotFeedbackPadNote = -1;

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
    pendingSetPageDelta = 0;
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
  pendingSetPageDelta = 0;
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
    // Same fail-safe for the snapshot press feedback: a dropped release must
    // not leave the pad amber forever (the heal sweep would sustain it).
    if (snapshotFeedbackPadNote >= 0 && snapshotFeedbackPadNote < kPadCount) {
      int feedbackPad = snapshotFeedbackPadNote;
      snapshotFeedbackPadNote = -1;
      padCurrentColors[feedbackPad] = kColorOff;
      queuePadLedUpdate(feedbackPad);
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
  // Config pads (rows 0-7, notes 0-63).
  if (note >= kConfigPadNoteFirst && note <= kConfigPadNoteLast) {
    onPadPressed(note);
    return;
  }

  // Set pager on the printed track buttons (▲=104 ◄=106 ►=107): instant taps,
  // applied at the fixed point in update(). Set mode only — with no set these
  // notes stay ignored, like every other track/side/shift button.
  if (synthPtr && synthPtr->getSetController().hasSet()) {
    if (note == kPagerUpNote)    { pendingSetPageRequest = 0; return; }
    if (note == kPagerLeftNote)  { pendingSetPageDelta.fetch_add(-1); return; }
    if (note == kPagerRightNote) { pendingSetPageDelta.fetch_add(1); return; }
  }

  // Remaining track buttons, side buttons, and Shift are unused — ignore.
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
  // Arm the paced repaint: servicePadLeds() drains the cursor a budgeted few
  // pads per frame (fresh colours at send time), so a full-grid rewrite never
  // blocks the main thread and never bursts — the old path blasted 16 SysEx
  // with blocking 2 ms sleeps and then re-blasted the grid for 1.5 s of retry
  // windows, which is exactly the sustained traffic the device choked on.
  if (!connected) return;
  padRepaintCursor = 0;
}


void ApcMiniController::updatePadLed(int padNote) {
  if (!connected || padNote < 0 || padNote >= kPadCount) return;

  RgbColor color = getPadDisplayColor(padNote);
  if (color != padCurrentColors[padNote]) {
    setPadRgb(padNote, color);
    padCurrentColors[padNote] = color;
    // No retry window needed: the heal sweep in servicePadLeds() resends every
    // pad within ~0.6 s regardless, which covers the occasional dropped SysEx.
  }
}

void ApcMiniController::servicePadLeds() {
  if (!connected) return;

  std::vector<std::pair<int, RgbColor>> updates;

  // 1) Paced full repaint: drain the cursor within a per-frame budget, colours
  //    computed fresh at send time. A 64-pad rewrite completes in ~6 frames
  //    (~100 ms) with at most 3 SysEx per frame.
  int budget = kPadRepaintBudgetPerFrame;
  while (padRepaintCursor < kPadCount && budget-- > 0) {
    const int note = padRepaintCursor++;
    const RgbColor c = getPadDisplayColor(note);
    updates.push_back({note, c});
    padCurrentColors[note] = c;
  }

  // 2) Permanent heal sweep (only once no repaint is in flight): resend a
  //    couple of pads per frame round-robin, UNCONDITIONALLY. The device is
  //    write-only — the cache can never prove a message landed — so the slow
  //    sweep is the only thing that guarantees every drop heals (<= ~0.6 s at
  //    60 fps), at a constant, gentle 2 messages/frame the old hardware can
  //    actually keep up with.
  if (padRepaintCursor >= kPadCount) {
    for (int i = 0; i < kPadHealPerFrame; ++i) {
      const int note = padHealCursor;
      padHealCursor = (padHealCursor + 1) % kPadCount;
      const RgbColor c = getPadDisplayColor(note);
      updates.push_back({note, c});
      padCurrentColors[note] = c;
    }
  }

  if (!updates.empty()) {
    setPadRgbBatch(updates);
  }
}

std::string ApcMiniController::currentConfigStem() const {
  if (!synthPtr) return {};
  const auto& path = synthPtr->getConfigSubsystem().getCurrentConfigPath();
  if (path.empty()) return {};
  return ofFilePath::getBaseName(path);
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
  // row. All 8 rows are set cells (the y=7 meta row was released 2026-07-31 —
  // paging lives on the track buttons now). Unassigned pads stay dark.
  int x, y;
  padNoteToXY(padNote, x, y);

  const auto& set = synthPtr->getSetController();

  const auto* cell = set.cellAt(x, y);
  if (cell == nullptr) return kColorOff;

  // Active-white and memory-dim are CONFIG-cell states. A snapshot cell is
  // never "loaded" (its `config`, if any, is forward state, not a load
  // target) and never waits on the MemoryBank — it rests at the standard
  // authored-colour × rest-dim tier like any other assigned cell.
  const bool isConfigCell = (cell->kind == ofxMarkSynth::SetController::CellKind::Config);

  // The ACTIVE cell — the currently-loaded config — renders bright white, a
  // colour no at-rest pad wears (owner 2026-07-31), overriding both its hue
  // and the memory dim.
  const std::string stem = currentConfigStem();
  if (isConfigCell && !stem.empty() && cell->config == stem) {
    return kColorBrightWhite;
  }

  const RgbColor base { cell->color.r, cell->color.g, cell->color.b };

  // memoryDependent cells render dimmed until the MemoryBank has collected
  // enough textures. Home cells are already the brightest of their world (the
  // builder guarantees it), so at rest they just show their colour.
  if (isConfigCell && cell->memoryDependent && !isMemoryReady()) {
    return scaleRgb(base, kMemoryDimFactor);
  }
  // At-rest cells sit at ~55% so the active cell's full white actually pops
  // against the (bright, semantic) grid palette.
  return scaleRgb(base, kSetCellRestDimFactor);
}

ApcMiniController::RgbColor ApcMiniController::getPadDisplayColor(int padNote) const {
  // Currently holding this pad — amber (both modes; holds only start on
  // assigned/actionable pads).
  if (currentHold.active && currentHold.padNote == padNote) {
    return kColorAmber;
  }

  // A pressed snapshot cell (instant commit, no hold armed) wears the same
  // amber until release; the heal sweep sustains it like the hold amber.
  if (padNote == snapshotFeedbackPadNote) {
    return kColorAmber;
  }

  // Set mode: page-aware set cells replace the buttonGrid display.
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

  // LED updates are applied on the main thread in update(); the heal sweep
  // covers any dropped send while held (plus the 120 ms amber refresh).
  queuePadLedUpdate(padNote);
}

void ApcMiniController::onSetPadPressed(int padNote) {
  // Config cells only arm a hold here — the load is applied at a fixed point in
  // update(), after this drain. Snapshot cells commit inside this handler (see
  // below). All 8 rows are set cells (paging is the track buttons); unassigned
  // pads do nothing.
  int x, y;
  padNoteToXY(padNote, x, y);
  auto& set = synthPtr->getSetController();

  const auto* cell = set.cellAt(x, y);
  if (cell == nullptr) return;

  // Snapshot cells commit INSTANTLY on press (owner 2026-08-28): recalling a
  // mod-snapshot slot is param-only and reversible, so it gets the Launch
  // Control XL's tap semantics rather than the config pads' 400 ms guard. No
  // hold is armed and no GUI preview is published (there is no config behind
  // the pad to preview). Feedback is the same amber the hold path wears —
  // getPadDisplayColor sustains it via snapshotFeedbackPadNote until the
  // release repaint restores the rest colour. No blink/pulse (surface policy).
  if (cell->kind == ofxMarkSynth::SetController::CellKind::Snapshot) {
    synthPtr->applySetCellAction(*cell);
    ofLogNotice("ApcMiniController") << "Set cell snapshot recall: slot " << cell->snapshotSlot;
    snapshotFeedbackPadNote = padNote;
    // Force the amber send even if our cached colour is stale.
    padCurrentColors[padNote] = kColorOff;
    queuePadLedUpdate(padNote);
    return;
  }

  // Hold-to-confirm the config load — the same live-performance guard the
  // buttonGrid path uses (an accidental brush must not swap the whole config).
  currentHold.active = true;
  currentHold.padNote = padNote;
  currentHold.startTimeMs = ofGetElapsedTimeMillis();

  // ★ PUBLISH THE HELD-CELL PREVIEW (owner 2026-08-01: check the momentary touch still previews).
  // It did not — set mode shipped without this. The buttonGrid path publishes a preview from
  // padConfigMap's configIndex, but a set cell only knows its config STEM, so the index had to be
  // resolved. That is what findConfigIndex is for. Without it the GUI overlay had nothing to show
  // for a held set pad, which is the whole point of a momentary touch: see it before committing.
  {
    auto& nav = synthPtr->getPerformanceNavigator();
    const int previewIdx = nav.findConfigIndex(cell->config);
    if (previewIdx >= 0) {
      nav.setPreviewProgress(0.0f);
      nav.setPreviewConfig(previewIdx);
    }
  }

  // Force an amber send even if our cached colour is stale.
  padCurrentColors[padNote] = kColorOff;
  queuePadLedUpdate(padNote);
}

void ApcMiniController::onPadReleased(int padNote) {
  // Snapshot pads never arm a hold and never publish a preview — the release
  // just restores the rest colour from the one-shot amber press feedback, and
  // must not clear a preview some other (config) pad may be holding.
  if (padNote == snapshotFeedbackPadNote) {
    snapshotFeedbackPadNote = -1;
    // Force a send even if our cached color is stale.
    padCurrentColors[padNote] = kColorOff;
    queuePadLedUpdate(padNote);
    return;
  }

  // Clear the held-cell preview on any pad release. Both modes now publish one
  // (set mode gained it 2026-08-01), so this is unconditional.
  if (synthPtr) {
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
  }
  padRepaintCursor = kPadCount;
  padHealCursor = 0;
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

      // Pace only OVERSIZED batches (the connect-time clear): routine traffic
      // is budgeted by servicePadLeds and never needs a blocking sleep.
      if (pads.size() > 16 && i < pads.size()) {
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

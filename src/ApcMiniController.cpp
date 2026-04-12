#include "ApcMiniController.h"

#include <algorithm>
#include <cmath>

#include "ofMain.h"

ApcMiniController::ApcMiniController() {
  padCurrentColors.fill(kColorOff);
  padLedRetryUntilMs.fill(0);
  for (auto& fs : faderStates) {
    fs.lastMidiValue = -1.0f;
    fs.targetParamValue = 0.0f;
    fs.pickedUp = false;
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

void ApcMiniController::queueLayerLedRefresh() {
  std::lock_guard<std::mutex> lock(ledQueueMutex);
  pendingLayerLedRefresh = true;
}

void ApcMiniController::flushQueuedLedUpdates() {
  std::vector<int> padNotes;
  bool refreshLayer = false;

  {
    std::lock_guard<std::mutex> lock(ledQueueMutex);
    padNotes.swap(pendingPadLedUpdates);
    refreshLayer = pendingLayerLedRefresh;
    pendingLayerLedRefresh = false;
  }

  if (!padNotes.empty()) {
    std::sort(padNotes.begin(), padNotes.end());
    padNotes.erase(std::unique(padNotes.begin(), padNotes.end()), padNotes.end());

    for (int note : padNotes) {
      updatePadLed(note);
    }
  }

  if (refreshLayer) {
    updateAllLayerButtonLeds();
  }
}

void ApcMiniController::queueLayerFaderOverlay(int layerIndex, bool pickedUp) {
  std::lock_guard<std::mutex> lock(layerOverlayMutex);
  hasPendingLayerOverlay = true;
  pendingLayerOverlayIndex = layerIndex;
  pendingLayerOverlayPickedUp = pickedUp;
}

void ApcMiniController::flushLayerFaderOverlay() {
  if (!layerFaderOverlayCallback) return;

  int layerIndex = -1;
  bool pickedUp = false;
  bool hasPending = false;

  {
    std::lock_guard<std::mutex> lock(layerOverlayMutex);
    hasPending = hasPendingLayerOverlay;
    if (hasPending) {
      layerIndex = pendingLayerOverlayIndex;
      pickedUp = pendingLayerOverlayPickedUp;
      hasPendingLayerOverlay = false;
    }
  }

  if (hasPending && layerIndex >= 0) {
    layerFaderOverlayCallback(layerIndex, pickedUp);
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
  restorePersistentLeds();

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
  if (!synthPtr) return;

  uint64_t nowMs = ofGetElapsedTimeMillis();

  // Apply any LED updates requested by MIDI callbacks early.
  // This improves perceived responsiveness for hold/amber feedback.
  flushQueuedLedUpdates();
  flushLayerFaderOverlay();

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

  // Track external config changes (e.g. keyboard / other controller)
  {
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
  {
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
    if (elapsed >= kHoldThresholdMs) {
      // Trigger config switch
      int heldPad = currentHold.padNote;
      int configIndex = padConfigMap[heldPad].configIndex;

      // Clear hold state first
      currentHold.active = false;
      currentHold.padNote = -1;

      if (configIndex >= 0) {
        // Trigger the config switch.
        auto& nav = synthPtr->getPerformanceNavigator();
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

  if (pendingFullPadRepaintAtMs != 0 && nowMs >= pendingFullPadRepaintAtMs) {
    pendingFullPadRepaintAtMs = 0;
    updateAllPadLeds();
  }

  // Layer LEDs: refresh periodically (layer params can come online after load)
  if (nowMs - lastLayerLedUpdateMs >= 200) {
    updateAllLayerButtonLeds();
    restorePersistentLeds();
    lastLayerLedUpdateMs = nowMs;
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

  // Bind faders to layer alphas
  bindFadersToLayerAlphas();
  bindAgencyFader();

  // Update all LEDs
  updateAllPadLeds();
  updateAllLayerButtonLeds();
  dimInactiveControls();
  restorePersistentLeds();
}

void ApcMiniController::onSynthWillUnload() {
  // Ensure the device is dark when no synth is active
  if (connected) {
    clearAllLeds();
  }

  // Reset fader pickup states so they need to re-acquire on next load
  resetFaderPickupStates();

  // Clear cached LED state
  for (auto& c : padCurrentColors) {
    c = kColorOff;
  }

  synthPtr.reset();
  currentConfigPadNote = -1;
  lastKnownConfigIndex = -1;
  lastKnownHibState = -1;
  lastLayerLedUpdateMs = 0;
}

void ApcMiniController::newMidiMessage(ofxMidiMessage& message) {
  if (!connected) return;

  switch (message.status) {
    case MIDI_NOTE_ON:
      if (message.velocity > 0) {
        handleNoteOn(message.pitch, message.velocity);
      } else {
        handleNoteOff(message.pitch);
      }
      break;
    case MIDI_NOTE_OFF:
      handleNoteOff(message.pitch);
      break;
    case MIDI_CONTROL_CHANGE:
      handleCC(message.control, message.value);
      break;
    default:
      break;
  }
}

void ApcMiniController::handleNoteOn(int note, int velocity) {
  // Layer toggle pads (bottom row of grid, notes 0-7)
  if (note >= kLayerPadNoteFirst && note <= kLayerPadNoteLast) {
    int layerIndex = note - kLayerPadNoteFirst;
    onLayerButtonPressed(layerIndex);
    return;
  }

  // Config pads (rows 1-7, notes 8-63)
  if (note >= kConfigPadNoteFirst && note <= kConfigPadNoteLast) {
    onPadPressed(note);
    return;
  }

  // Arrow buttons (physical bottom row) → same behavior as keyboard arrow keys
  if (note == kArrowLeftButtonNote) {
    if (synthPtr) {
      synthPtr->keyPressed(OF_KEY_LEFT);
    }
    return;
  }

  if (note == kArrowRightButtonNote) {
    if (synthPtr) {
      synthPtr->keyPressed(OF_KEY_RIGHT);
    }
    return;
  }

  // Physical Track Buttons (notes 100-107): ignored
  // (They still send input, but we use the RGB pad row for layer controls.)

  // Shift and side buttons are inactive - ignore
}

void ApcMiniController::handleNoteOff(int note) {
  // Config pads (rows 1-7, notes 8-63) - need release for hold-to-confirm
  if (note >= kConfigPadNoteFirst && note <= kConfigPadNoteLast) {
    onPadReleased(note);
    return;
  }

  // Arrow buttons (physical bottom row)
  if (note == kArrowLeftButtonNote) {
    if (synthPtr) {
      synthPtr->keyReleased(OF_KEY_LEFT);
    }
    return;
  }

  if (note == kArrowRightButtonNote) {
    if (synthPtr) {
      synthPtr->keyReleased(OF_KEY_RIGHT);
    }
    return;
  }

  // Layer pads (notes 0-7) don't need release handling - toggle on press
  // Other note offs ignored
}

void ApcMiniController::handleCC(int cc, int value) {
  // Faders
  // CC 48-55: layer alphas
  // CC 56: Synth Agency
  float normalized = value / 127.0f;

  if (cc >= kFaderCCFirst && cc < kFaderCCFirst + kLayerFaderCount) {
    int faderIndex = cc - kFaderCCFirst;
    onFaderMoved(faderIndex, normalized);
    return;
  }

  if (cc == kAgencyFaderCC) {
    onAgencyFaderMoved(normalized);
    return;
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

  // Populate from the navigator's resolved 8x7 grid (y=0 is top row).
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

  // Paint all config pads every time (56 pads).
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


ApcMiniController::RgbColor ApcMiniController::getPadDisplayColor(int padNote) const {
  const auto& info = padConfigMap[padNote];

  auto scale = [](RgbColor c, float factor) -> RgbColor {
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
  };

  // No config assigned
  if (!info.isAssigned) {
    return kColorOff;
  }

  // Currently holding this pad - show amber
  if (currentHold.active && currentHold.padNote == padNote) {
    return kColorAmber;
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
  return scale(info.color, kConfigDimFactor);
}

void ApcMiniController::onPadPressed(int padNote) {
  const auto& info = padConfigMap[padNote];
  if (!info.isAssigned) return;

  // Start hold timer
  currentHold.active = true;
  currentHold.padNote = padNote;
  currentHold.startTimeMs = ofGetElapsedTimeMillis();

  // Force a send even if our cached color is stale.
  padCurrentColors[padNote] = kColorOff;

  // LED updates are applied on the main thread in update().
  queuePadLedUpdate(padNote);

  // If the first SysEx is dropped, keep retrying while held.
  padLedRetryUntilMs[padNote] = ofGetElapsedTimeMillis() + 500;
}

void ApcMiniController::onPadReleased(int padNote) {
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

// === Layer Buttons ===

void ApcMiniController::onLayerButtonPressed(int buttonIndex) {
  if (!synthPtr || buttonIndex < 0 || buttonIndex >= kLayerPadCount) return;

  // Use pause slots as the authoritative "layer exists" signal
  const auto& pauseParamPtrs = synthPtr->getRenderSubsystem().getLayerPauseParamPtrs();
  if (buttonIndex >= static_cast<int>(pauseParamPtrs.size()) || pauseParamPtrs[buttonIndex] == nullptr) return;

  // Toggle layer pause
  synthPtr->getRenderSubsystem().toggleLayerPause(buttonIndex);

  // LED updates are applied on the main thread in update().
  queueLayerLedRefresh();
}

void ApcMiniController::updateLayerButtonLed(int buttonIndex) {
  // Keep per-button update for completeness, but prefer row updates
  if (!connected || buttonIndex < 0 || buttonIndex >= kLayerPadCount) return;

  bool layerExists = false;
  bool isPaused = false;

  if (synthPtr) {
    const auto& pauseParamPtrs = synthPtr->getRenderSubsystem().getLayerPauseParamPtrs();
    layerExists = buttonIndex < static_cast<int>(pauseParamPtrs.size()) && pauseParamPtrs[buttonIndex] != nullptr;
    if (layerExists) {
      isPaused = pauseParamPtrs[buttonIndex]->get();
    }
  }

  RgbColor color = kColorOff;
  if (layerExists) {
    color = isPaused ? kColorDimLayer : kColorBrightLayer;
  }

  setBottomButtonLed(buttonIndex, color);
}

void ApcMiniController::updateAllLayerButtonLeds() {
  if (!connected) return;

  std::vector<std::pair<int, RgbColor>> updates;
  updates.reserve(kLayerPadCount);

  const auto* pauseParamPtrsPtr = synthPtr ? &synthPtr->getRenderSubsystem().getLayerPauseParamPtrs() : nullptr;

  for (int i = 0; i < kLayerPadCount; i++) {
    bool layerExists = false;
    bool isPaused = false;

    if (pauseParamPtrsPtr) {
      const auto& pauseParamPtrs = *pauseParamPtrsPtr;
      layerExists = i < static_cast<int>(pauseParamPtrs.size()) && pauseParamPtrs[i] != nullptr;
      if (layerExists) {
        isPaused = pauseParamPtrs[i]->get();
      }
    }

    RgbColor color = kColorOff;
    if (layerExists) {
      color = isPaused ? kColorDimLayer : kColorBrightLayer;
    }

    int padNote = kLayerPadNoteFirst + i;
    updates.push_back({padNote, color});
    padCurrentColors[padNote] = color;
  }

  setPadRgbBatch(updates);
}

// === Faders ===

void ApcMiniController::onFaderMoved(int faderIndex, float normalizedValue) {
  if (!synthPtr || faderIndex >= kLayerFaderCount) return;

  ofParameterGroup& alphas = synthPtr->getRenderSubsystem().getLayerAlphaParameters();
  if (faderIndex >= static_cast<int>(alphas.size())) return;

  auto& fs = faderStates[faderIndex];
  ofParameter<float>& param = alphas.getFloat(faderIndex);

  if (!fs.pickedUp) {
    // Check if fader has crossed the parameter value (pickup)
    float paramValue = param.get();
    float paramNormalized = (paramValue - param.getMin()) / (param.getMax() - param.getMin());

    if (std::abs(normalizedValue - paramNormalized) <= kPickupThreshold) {
      fs.pickedUp = true;
      ofLogVerbose("ApcMiniController") << "Fader " << faderIndex << " picked up";
    } else {
      // Not picked up yet - don't change parameter
      fs.lastMidiValue = normalizedValue;
      queueLayerFaderOverlay(faderIndex, false);
      return;
    }
  }

  // Apply value
  float min = param.getMin();
  float max = param.getMax();
  float newValue = min + normalizedValue * (max - min);
  param.set(newValue);
  fs.lastMidiValue = normalizedValue;
  queueLayerFaderOverlay(faderIndex, true);
}

void ApcMiniController::onAgencyFaderMoved(float normalizedValue) {
  if (!synthPtr) return;

  auto paramWrapper = synthPtr->findParameterByNamePrefix("agency");
  if (paramWrapper == std::nullopt) return;

  ofParameter<float>& param = paramWrapper->get().cast<float>();
  auto& fs = agencyFaderState;

  if (!fs.pickedUp) {
    float paramValue = param.get();
    float paramNormalized = (paramValue - param.getMin()) / (param.getMax() - param.getMin());

    if (std::abs(normalizedValue - paramNormalized) <= kPickupThreshold) {
      fs.pickedUp = true;
      ofLogVerbose("ApcMiniController") << "Agency fader picked up";
    } else {
      fs.lastMidiValue = normalizedValue;
      return;
    }
  }

  float min = param.getMin();
  float max = param.getMax();
  float newValue = min + normalizedValue * (max - min);
  param.set(newValue);
  fs.lastMidiValue = normalizedValue;
}

void ApcMiniController::bindAgencyFader() {
  if (!synthPtr) return;

  auto paramWrapper = synthPtr->findParameterByNamePrefix("agency");
  if (paramWrapper == std::nullopt) return;

  ofParameter<float>& param = paramWrapper->get().cast<float>();
  float paramValue = param.get();
  float paramNormalized = (paramValue - param.getMin()) / (param.getMax() - param.getMin());

  agencyFaderState.targetParamValue = paramNormalized;
  agencyFaderState.pickedUp = false;
  agencyFaderState.lastMidiValue = -1.0f;
}

void ApcMiniController::bindFadersToLayerAlphas() {
  resetFaderPickupStates();

  if (!synthPtr) return;

  ofParameterGroup& alphas = synthPtr->getRenderSubsystem().getLayerAlphaParameters();
  size_t count = std::min<size_t>(kLayerFaderCount, alphas.size());

  for (size_t i = 0; i < count; i++) {
    ofParameter<float>& param = alphas.getFloat(i);
    float paramValue = param.get();
    float paramNormalized = (paramValue - param.getMin()) / (param.getMax() - param.getMin());
    faderStates[i].targetParamValue = paramNormalized;
    faderStates[i].pickedUp = false;
    faderStates[i].lastMidiValue = -1.0f;
  }

  ofLogNotice("ApcMiniController") << "Bound " << count << " faders to layer alphas";
}

void ApcMiniController::resetFaderPickupStates() {
  for (auto& fs : faderStates) {
    fs.pickedUp = false;
    fs.lastMidiValue = -1.0f;
  }
  agencyFaderState.pickedUp = false;
  agencyFaderState.lastMidiValue = -1.0f;
}

// === LED Control ===

void ApcMiniController::clearAllLeds() {
  if (!connected) return;

  // Clear all 64 pads (includes layer pads on bottom row)
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

void ApcMiniController::setBottomButtonLed(int buttonIndex, const RgbColor& color) {
  // Layer toggle buttons are on the bottom row of the RGB pad grid (notes 0-7)
  // NOT the physical Track Buttons (notes 100-107) which are RED-only LEDs
  if (!connected || buttonIndex < 0 || buttonIndex >= kLayerPadCount) return;

  int padNote = kLayerPadNoteFirst + buttonIndex;  // notes 0-7
  setPadRgb(padNote, color);
  padCurrentColors[padNote] = color;
}

void ApcMiniController::setPhysicalBottomButtonLed(int note, int velocity) {
  if (!connected) return;

  velocity = ofClamp(velocity, 0, 127);

  if (midiOutControl.isOpen()) {
    midiOutControl.sendNoteOn(1, note, velocity);
  } else if (midiOut.isOpen()) {
    midiOut.sendNoteOn(1, note, velocity);
  }
}

void ApcMiniController::restorePersistentLeds() {
  if (!connected) return;

  setPhysicalBottomButtonLed(kArrowLeftButtonNote, kSingleLedOn);
  setPhysicalBottomButtonLed(kArrowRightButtonNote, kSingleLedOn);
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

  restorePersistentLeds();
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

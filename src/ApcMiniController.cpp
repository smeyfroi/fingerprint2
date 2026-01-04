#include "ApcMiniController.h"

#include <cmath>
#include <fstream>

#include "ofMain.h"
#include "ofJson.h"

ApcMiniController::ApcMiniController() {
  padCurrentColors.fill(kColorOff);
  for (auto& fs : faderStates) {
    fs.lastMidiValue = -1.0f;
    fs.targetParamValue = 0.0f;
    fs.pickedUp = false;
  }
}

ApcMiniController::~ApcMiniController() {
  exit();
}

bool ApcMiniController::tryConnect() {
  if (connected) return true;

  midiIn.listInPorts();
  midiOut.listOutPorts();

  int inPort = -1;
  int outPortNotes = -1;
  int outPortControl = -1;

  // Find input port (Control port for buttons/faders)
  for (int i = 0; i < midiIn.getNumInPorts(); i++) {
    std::string name = midiIn.getInPortName(i);
    if (name.find(kInputPortPattern) != std::string::npos) {
      inPort = i;
      ofLogNotice("ApcMiniController") << "Found input port: " << name;
      break;
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

  if (inPort < 0 || outPortNotes < 0) {
    ofLogNotice("ApcMiniController") << "APC Mini MK2 not found";
    return false;
  }

  if (!midiIn.openPort(inPort)) {
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

  midiIn.removeListener(this);
  midiIn.closePort();
  midiOut.closePort();
  midiOutControl.closePort();
  connected = false;

  ofLogNotice("ApcMiniController") << "Disconnected from APC Mini MK2";
}

void ApcMiniController::update() {
  // Check for hold timeout
  if (currentHold.active && synthPtr) {
    uint64_t elapsed = ofGetElapsedTimeMillis() - currentHold.startTimeMs;
    if (elapsed >= kHoldThresholdMs) {
      // Trigger config switch
      int heldPad = currentHold.padNote;
      int configIndex = padConfigMap[heldPad].configIndex;
      
      // Clear hold state first
      currentHold.active = false;
      currentHold.padNote = -1;
      
      if (configIndex >= 0) {
        // Update current config pad before triggering load
        int previousPad = currentConfigPadNote;
        currentConfigPadNote = heldPad;
        
        // Update LEDs for old and new current config pads
        if (previousPad >= 0 && previousPad < kPadCount) {
          updatePadLed(previousPad);
        }
        updatePadLed(heldPad);
        
        // Now trigger the config switch
        auto& nav = synthPtr->getPerformanceNavigator();
        nav.jumpTo(configIndex);
        ofLogNotice("ApcMiniController") << "Config jump triggered to index " << configIndex;
      } else {
        // Just update the held pad LED (restore from amber)
        updatePadLed(heldPad);
      }
    }
  }
}

void ApcMiniController::exit() {
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

  // Build pad-to-config mapping from the navigator's config list
  buildPadConfigMap();

  // Find which pad corresponds to the current config
  auto& nav = synthPtr->getPerformanceNavigator();
  currentConfigPadNote = -1;
  int currentIdx = nav.getCurrentIndex();
  for (int i = 0; i < kPadCount; i++) {
    if (padConfigMap[i].configIndex == currentIdx) {
      currentConfigPadNote = i;
      break;
    }
  }

  // Bind faders to layer alphas
  bindFadersToLayerAlphas();

  // Update all LEDs
  updateAllPadLeds();
  updateAllLayerButtonLeds();
  dimInactiveControls();
}

void ApcMiniController::onSynthWillUnload() {
  // Reset fader pickup states so they need to re-acquire on next load
  resetFaderPickupStates();
  synthPtr.reset();
  currentConfigPadNote = -1;
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

  // Physical bottom buttons (notes 100-107) - ignore, LEDs are red-only
  // Shift and side buttons are inactive - ignore
}

void ApcMiniController::handleNoteOff(int note) {
  // Config pads (rows 1-7, notes 8-63) - need release for hold-to-confirm
  if (note >= kConfigPadNoteFirst && note <= kConfigPadNoteLast) {
    onPadReleased(note);
    return;
  }

  // Layer pads (notes 0-7) don't need release handling - toggle on press
  // Other note offs ignored
}

void ApcMiniController::handleCC(int cc, int value) {
  // Faders (CC 48-55 for layers, 56 unused)
  if (cc >= kFaderCCFirst && cc < kFaderCCFirst + kLayerFaderCount) {
    int faderIndex = cc - kFaderCCFirst;
    float normalized = value / 127.0f;
    onFaderMoved(faderIndex, normalized);
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

  // Track which pads are taken (for conflict resolution)
  std::array<bool, kPadCount> padTaken;
  padTaken.fill(false);

  // Track last assigned color for cloning
  RgbColor lastColor = kColorDefaultConfig;

  // First pass: assign configs with explicit buttonGrid
  for (int configIdx = 0; configIdx < static_cast<int>(configs.size()); configIdx++) {
    const std::string& configPath = configs[configIdx];

    // Parse the config JSON to get buttonGrid
    std::ifstream file(configPath);
    if (!file.is_open()) continue;

    ofJson json;
    try {
      file >> json;
    } catch (...) {
      ofLogWarning("ApcMiniController") << "Failed to parse config: " << configPath;
      continue;
    }

    if (!json.contains("buttonGrid")) continue;

    auto& bg = json["buttonGrid"];
    if (!bg.contains("x") || !bg.contains("y")) continue;

    int x = bg["x"].get<int>();
    int y = bg["y"].get<int>();

    if (x < 0 || x >= kGridWidth || y < 0 || y >= kGridHeight) {
      ofLogWarning("ApcMiniController") << "Invalid buttonGrid coords in " << configPath;
      continue;
    }

    int padNote = xyToPadNote(x, y);
    
    // Bottom row (y=7, notes 0-7) is reserved for layer toggle buttons
    if (padNote < kConfigPadNoteFirst) {
      ofLogWarning("ApcMiniController") << "Config " << configPath
                                         << " uses reserved layer row (y=7), will be auto-assigned";
      continue;
    }

    // Check for conflict
    if (padTaken[padNote]) {
      ofLogError("ApcMiniController") << "Config " << configPath
                                       << " conflicts with existing pad at (" << x << "," << y << ")";
      // Will be auto-assigned later
      continue;
    }

    // Parse color
    RgbColor color = kColorDefaultConfig;
    if (bg.contains("color")) {
      color = parseHexColor(bg["color"].get<std::string>());
    }

    padConfigMap[padNote].configIndex = configIdx;
    padConfigMap[padNote].color = color;
    padConfigMap[padNote].isAssigned = true;
    padTaken[padNote] = true;
    lastColor = color;
  }

  // Second pass: auto-assign configs without buttonGrid or with conflicts
  // Start at note 8 (row 1) since notes 0-7 (row 0) are reserved for layer toggle
  int nextFreePad = kConfigPadNoteFirst;
  auto findNextFreePad = [&]() {
    while (nextFreePad <= kConfigPadNoteLast && padTaken[nextFreePad]) {
      nextFreePad++;
    }
    return nextFreePad <= kConfigPadNoteLast ? nextFreePad : -1;
  };

  for (int configIdx = 0; configIdx < static_cast<int>(configs.size()); configIdx++) {
    // Check if this config is already assigned
    bool alreadyAssigned = false;
    for (int i = 0; i < kPadCount; i++) {
      if (padConfigMap[i].configIndex == configIdx) {
        alreadyAssigned = true;
        break;
      }
    }
    if (alreadyAssigned) continue;

    int freePad = findNextFreePad();
    if (freePad < 0) {
      ofLogWarning("ApcMiniController") << "No free pad for config index " << configIdx;
      break;
    }

    padConfigMap[freePad].configIndex = configIdx;
    padConfigMap[freePad].color = lastColor;  // Clone previous color
    padConfigMap[freePad].isAssigned = true;
    padTaken[freePad] = true;
  }

  ofLogNotice("ApcMiniController") << "Built pad config map for " << configs.size() << " configs";
}

void ApcMiniController::updateAllPadLeds() {
  if (!connected) return;

  // Only update config pads. Notes 0-7 are reserved for layer toggle LEDs.
  std::vector<std::pair<int, RgbColor>> updates;
  for (int i = kConfigPadNoteFirst; i <= kConfigPadNoteLast; i++) {
    RgbColor color = getPadDisplayColor(i);
    if (color != padCurrentColors[i]) {
      updates.push_back({i, color});
      padCurrentColors[i] = color;
    }
  }

  if (!updates.empty()) {
    setPadRgbBatch(updates);
  }
}

void ApcMiniController::updatePadLed(int padNote) {
  if (!connected || padNote < 0 || padNote >= kPadCount) return;

  RgbColor color = getPadDisplayColor(padNote);
  if (color != padCurrentColors[padNote]) {
    setPadRgb(padNote, color);
    padCurrentColors[padNote] = color;
  }
}

ApcMiniController::RgbColor ApcMiniController::getPadDisplayColor(int padNote) const {
  const auto& info = padConfigMap[padNote];

  // No config assigned
  if (!info.isAssigned) {
    return kColorOff;
  }

  // Currently holding this pad - show white
  if (currentHold.active && currentHold.padNote == padNote) {
    return kColorBrightWhite;
  }

  // This is the current config
  if (padNote == currentConfigPadNote && synthPtr) {
    // Check synth state for different whites/grays
    auto hibState = synthPtr->getHibernationState();
    if (hibState == ofxMarkSynth::HibernationController::State::HIBERNATED) {
      return kColorDimGray;  // Hibernated = dim gray
    } else if (hibState == ofxMarkSynth::HibernationController::State::FADING_OUT ||
               hibState == ofxMarkSynth::HibernationController::State::FADING_IN) {
      return kColorMediumGray;  // Fading = medium gray
    } else {
      return kColorBrightWhite;  // Active = bright white
    }
  }

  // Regular config pad - show its color
  return info.color;
}

void ApcMiniController::onPadPressed(int padNote) {
  const auto& info = padConfigMap[padNote];
  if (!info.isAssigned) return;

  // Start hold timer
  currentHold.active = true;
  currentHold.padNote = padNote;
  currentHold.startTimeMs = ofGetElapsedTimeMillis();

  // Show amber while holding
  updatePadLed(padNote);
}

void ApcMiniController::onPadReleased(int padNote) {
  if (currentHold.active && currentHold.padNote == padNote) {
    // Released before threshold - cancel hold
    currentHold.active = false;
    currentHold.padNote = -1;
  }
  // Always update the pad LED on release to restore correct color
  // (handles both cancelled holds and completed holds where user releases after activation)
  updatePadLed(padNote);
}

// === Layer Buttons ===

void ApcMiniController::onLayerButtonPressed(int buttonIndex) {
  if (!synthPtr || buttonIndex >= kLayerPadCount) return;

  // Check if layer exists
  size_t layerCount = synthPtr->getLayerCount();
  if (buttonIndex >= static_cast<int>(layerCount)) return;

  // Toggle layer pause
  synthPtr->toggleLayerPauseSlot(buttonIndex);
  updateLayerButtonLed(buttonIndex);
}

void ApcMiniController::updateLayerButtonLed(int buttonIndex) {
  if (!connected || buttonIndex >= kLayerPadCount) return;
  if (!synthPtr) {
    setBottomButtonLed(buttonIndex, kColorOff);
    return;
  }

  // Get pause param pointers to check layer existence and state
  const auto& pauseParamPtrs = synthPtr->getLayerPauseParamPtrs();
  if (buttonIndex >= static_cast<int>(pauseParamPtrs.size())) {
    setBottomButtonLed(buttonIndex, kColorOff);
    return;
  }

  // Get actual pause state from synth
  bool isPaused = pauseParamPtrs[buttonIndex]->get();
  RgbColor color = isPaused ? kColorDimGreen : kColorBrightGreen;
  setBottomButtonLed(buttonIndex, color);
}

void ApcMiniController::updateAllLayerButtonLeds() {
  if (!connected) return;

  for (int i = 0; i < kLayerPadCount; i++) {
    updateLayerButtonLed(i);
  }
}

// === Faders ===

void ApcMiniController::onFaderMoved(int faderIndex, float normalizedValue) {
  if (!synthPtr || faderIndex >= kLayerFaderCount) return;

  ofParameterGroup& alphas = synthPtr->getLayerAlphaParameters();
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
      return;
    }
  }

  // Apply value
  float min = param.getMin();
  float max = param.getMax();
  float newValue = min + normalizedValue * (max - min);
  param.set(newValue);
  fs.lastMidiValue = normalizedValue;
}

void ApcMiniController::bindFadersToLayerAlphas() {
  resetFaderPickupStates();

  if (!synthPtr) return;

  ofParameterGroup& alphas = synthPtr->getLayerAlphaParameters();
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
}

// === LED Control ===

void ApcMiniController::clearAllLeds() {
  if (!connected) return;

  // Clear all 64 pads (includes layer pads on bottom row)
  std::vector<std::pair<int, RgbColor>> padUpdates;
  for (int i = 0; i < kPadCount; i++) {
    padUpdates.push_back({i, kColorOff});
    padCurrentColors[i] = kColorOff;
  }
  setPadRgbBatch(padUpdates);

  // Turn off physical bottom buttons (RED-only LEDs, notes 100-107)
  // These are not used for our layer controls but we clear them for cleanliness
  if (midiOutControl.isOpen()) {
    for (int i = 0; i < kBottomButtonCount; i++) {
      midiOutControl.sendNoteOn(1, kBottomButtonNoteFirst + i, 0);
    }
  }

  // Clear side buttons (GREEN-only LEDs, notes 112-119)
  for (int i = 0; i < kSideButtonCount; i++) {
    setSideButtonLed(i, kColorOff);
  }

  // Clear shift button (use note-on with velocity 0)
  if (midiOutControl.isOpen()) {
    midiOutControl.sendNoteOn(1, kShiftButtonNote, 0);
  } else {
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

  // Build batch sysex data - each pad needs 8 bytes
  std::vector<uint8_t> data;
  data.reserve(pads.size() * 8);

  for (const auto& [padNote, color] : pads) {
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
  }

  if (!data.empty()) {
    sendSysex(kSysexRgbMessageType, data);
  }
}

void ApcMiniController::setBottomButtonLed(int buttonIndex, const RgbColor& color) {
  // Layer toggle buttons are now on the bottom row of the RGB pad grid (notes 0-7)
  // NOT the physical bottom buttons (notes 100-107) which are RED-only LEDs
  if (!connected || buttonIndex < 0 || buttonIndex >= kLayerPadCount) return;

  int padNote = kLayerPadNoteFirst + buttonIndex;  // notes 0-7
  setPadRgb(padNote, color);
  padCurrentColors[padNote] = color;
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

  // Some firmware/OS combinations appear to accept RGB SysEx on either port.
  // Broadcast to both outputs to keep LEDs reliable.
  if (midiOut.isOpen()) {
    midiOut.sendMidiBytes(message);
  }
  if (midiOutControl.isOpen()) {
    midiOutControl.sendMidiBytes(message);
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

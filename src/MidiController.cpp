#include "MidiController.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include "ofMain.h"

namespace {
constexpr int kFaderKnobOffset = 24;
}

MidiController::MidiController() {
  shiftModeParameter.addListener(this, &MidiController::onShiftModeChanged);
}

void MidiController::update() {
  // Process queued button events on the main thread
  int writeIndex = buttonEventWriteIndex.load();
  while (buttonEventReadIndex != writeIndex) {
    const auto& event = buttonEventBuffer[buttonEventReadIndex];
    handleButtonCC(event.channel, event.cc, event.value);
    buttonEventReadIndex = (buttonEventReadIndex + 1) % kButtonEventBufferSize;
  }

  // Poll recording, saving states, and timers - update display when any change
  if (synthPtr) {
    bool currentRecordingState = synthPtr->isRecording();
    bool currentSavingState = synthPtr->getActiveSaveCount() > 0;
    
    // Get current config timer values (total seconds)
    int currentConfigTimeSeconds = synthPtr->getConfigRunningMinutes() * 60 + synthPtr->getConfigRunningSeconds();
    
    if (currentRecordingState != lastRecordingState || 
        currentSavingState != lastSavingState ||
        currentConfigTimeSeconds != lastDisplayedConfigTimeSeconds) {
      lastRecordingState = currentRecordingState;
      lastSavingState = currentSavingState;
      lastDisplayedConfigTimeSeconds = currentConfigTimeSeconds;
      updateStationaryDisplay();
    }
  }
}

void MidiController::newMidiMessage(ofxMidiMessage& message) {
  // Queue button CC events for processing on the main thread.
  // This avoids threading issues with OpenGL calls (e.g., video recording).
  if (message.status == MIDI_CONTROL_CHANGE) {
    int writeIndex = buttonEventWriteIndex.load();
    buttonEventBuffer[writeIndex] = {message.channel, message.control, message.value};
    int nextIndex = (writeIndex + 1) % kButtonEventBufferSize;
    buttonEventWriteIndex.store(nextIndex);
  }
}

void MidiController::handleButtonCC(int channel, int cc, int value) {
  bool pressed = value > 64;

  // === Shift button (CC 63 on channel 7) - latching toggle ===
  if (cc == kShiftButtonCC && channel == kShiftButtonChannel) {
    if (pressed) {
      shiftModeParameter = !shiftModeParameter;
      // LED update and display update happen in onShiftModeChanged
    }
    return;
  }

  // === Top row function buttons (CC 37-44) ===
  if (cc >= kFunctionButtonCCFirst && cc <= kFunctionButtonCCLast) {
    int index = cc - kFunctionButtonCCFirst;
    if (pressed) {
      setButtonLedByCC(cc, kButtonPressedColor);
      if (synthPtr) {
        if (!shiftModeParameter) {
          // Snapshot mode: load snapshot
          synthPtr->loadModSnapshotSlot(index);
          showTempDisplay("Snapshot", std::to_string(index + 1));
        } else {
          // Layer mode: toggle layer pause
          synthPtr->toggleLayerPauseSlot(index);
          showTempDisplay("Layer " + std::to_string(index + 1), "Toggle Pause");
        }
      }
    } else {
      // Restore to mode color
      LedColor modeColor = shiftModeParameter ? kLayerModeColor : kAgencyModeColor;
      setButtonLedByCC(cc, modeColor);
    }
    return;
  }

  // === Transport buttons (all on channel 1, except Shift handled above) ===

  // Play button (CC 116)
  if (cc == kPlayButtonCC) {
    if (pressed) {
      if (!shiftModeParameter) {
        // Pause/Play
        sendKeyPress(' ');
        showTempDisplay("Transport", "Pause/Play");
      } else {
        // Hibernate
        sendKeyPress('H');
        showTempDisplay("Transport", "Hibernate");
      }
    } else {
      if (!shiftModeParameter) {
        sendKeyRelease(' ');
      } else {
        sendKeyRelease('H');
      }
    }
    return;
  }

  // Record button (CC 118) - Save Image in both modes
  if (cc == kRecordTransportCC) {
    if (pressed) {
      sendKeyPress('S');
      showTempDisplay("Action", "Save Image");
    } else {
      sendKeyRelease('S');
    }
    return;
  }

  // Track Left button (CC 103) - Previous Config (same in both modes)
  if (cc == kTrackLeftCC) {
    if (pressed) {
      sendKeyPress(OF_KEY_LEFT);
      showTempDisplay("Config", "Previous");
    } else {
      sendKeyRelease(OF_KEY_LEFT);
    }
    return;
  }

  // Track Right button (CC 102) - Next Config (same in both modes)
  if (cc == kTrackRightCC) {
    if (pressed) {
      sendKeyPress(OF_KEY_RIGHT);
      showTempDisplay("Config", "Next");
    } else {
      sendKeyRelease(OF_KEY_RIGHT);
    }
    return;
  }

  // Bottom row buttons (CC 45-52) - now unused, ignore
}

void MidiController::setButtonLedByCC(int cc, const LedColor& color) {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  // Convert CC to button number (1-16)
  // Top row: CC 37-44 → buttons 1-8
  // Bottom row: CC 45-52 → buttons 9-16 (unused but kept for potential future use)
  int buttonNum = 0;
  if (cc >= kFunctionButtonCCFirst && cc <= kFunctionButtonCCLast) {
    buttonNum = cc - kFunctionButtonCCFirst + 1;  // 1-8
  } else if (cc >= kBottomRowButtonCCFirst && cc <= kBottomRowButtonCCLast) {
    buttonNum = cc - kBottomRowButtonCCFirst + 9;  // 9-16
  }

  if (buttonNum >= 1 && buttonNum <= 16) {
    leds->setButtonLED(buttonNum, color);
  }
}

MidiController::LedColor MidiController::getButtonRestoreColor(int cc) const {
  auto it = buttonRestoreColors.find(cc);
  if (it != buttonRestoreColors.end()) {
    return it->second;
  }
  return kOffColor;
}

void MidiController::sendKeyPress(int key) {
  if (!synthPtr) return;
  synthPtr->keyPressed(key);
  heldKeys.insert(key);
}

void MidiController::sendKeyRelease(int key) {
  if (!synthPtr) return;
  if (heldKeys.count(key)) {
    synthPtr->keyReleased(key);
    heldKeys.erase(key);
  }
}

void MidiController::onShiftModeChanged(bool& value) {
  ofLogNotice("MidiController") << "Shift mode changed to "
                               << (value ? "Layer mode (green)" : "Snapshot mode (red)");
  applyFaderBank();
  updateModeLeds();
  showTempDisplay("Shift", value ? "On" : "Off");
}

void MidiController::updateModeLeds() {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  LedColor modeColor = shiftModeParameter ? kLayerModeColor : kAgencyModeColor;

  // Top row buttons (CC 37-44 → buttons 1-8)
  for (int i = 1; i <= 8; ++i) {
    leds->setButtonLED(i, modeColor);
  }
}

void MidiController::setupInitialLeds() {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  // === Top row (CC 37-44 → buttons 1-8) - Snapshot mode color (red) ===
  for (int i = 1; i <= 8; ++i) {
    leds->setButtonLED(i, kAgencyModeColor);
  }

  // === Bottom row buttons (9-16) - all off (unused, transport handled by hardware) ===
  for (int i = 9; i <= 16; ++i) {
    leds->setButtonLED(i, kOffColor);
  }

  // === Encoder LEDs (1-based numbering, encoders 1-24) ===
  // Row 1 (encoders 1-8): indices 0-7 in addon terminology
  leds->setEncoderLED(1, kAgencyEncoderColor);    // Encoder 0 - agency (red)
  leds->setEncoderLED(2, kOffColor);              // Encoder 1 - unused
  leds->setEncoderLED(3, kBlueEncoderColor);      // Encoder 2 - blue
  leds->setEncoderLED(4, kCyanEncoderColor);      // Encoder 3 - cyan
  leds->setEncoderLED(5, kPurpleEncoderColor);    // Encoder 4 - purple
  leds->setEncoderLED(6, kMagentaEncoderColor);   // Encoder 5 - magenta
  leds->setEncoderLED(7, kOffColor);              // Encoder 6 - unused
  leds->setEncoderLED(8, kOffColor);              // Encoder 7 - unused

  // Row 2 (encoders 9-16): indices 8-15 in addon terminology
  leds->setEncoderLED(9, kOffColor);              // Encoder 8 - unused
  leds->setEncoderLED(10, kOffColor);             // Encoder 9 - unused
  leds->setEncoderLED(11, kBlueEncoderColor);     // Encoder 10 - blue
  leds->setEncoderLED(12, kCyanEncoderColor);     // Encoder 11 - cyan
  leds->setEncoderLED(13, kPurpleEncoderColor);   // Encoder 12 - purple
  leds->setEncoderLED(14, kMagentaEncoderColor);  // Encoder 13 - magenta
  leds->setEncoderLED(15, kPurpleEncoderColor);   // Encoder 14 - purple
  leds->setEncoderLED(16, kMagentaEncoderColor);  // Encoder 15 - magenta

  // Row 3 (encoders 17-24): all unused
  for (int i = 17; i <= 24; ++i) {
    leds->setEncoderLED(i, kOffColor);
  }
}

void MidiController::setLayerAlphasFullyOn() {
  if (!synthPtr) return;

  ofParameterGroup& layerAlphaParameters = synthPtr->getLayerAlphaParameters();
  size_t count = std::min<size_t>(8, layerAlphaParameters.size());
  for (size_t i = 0; i < count; ++i) {
    ofParameter<float>& layerParameter = layerAlphaParameters.getFloat(i);
    layerParameter = layerParameter.getMax();
  }
}

void MidiController::applyFaderBank() {
  if (!lc || !synthPtr) return;

  lc->clearFaders();

  if (shiftModeParameter) {
    ofParameterGroup& layerAlphaParameters = synthPtr->getLayerAlphaParameters();
    size_t count = std::min<size_t>(8, layerAlphaParameters.size());
    for (size_t i = 0; i < count; ++i) {
      ofParameter<float>& layerParameter = layerAlphaParameters.getFloat(i);
      int knobIndex = kFaderKnobOffset + (int)i;
      lc->knobPickup(knobIndex, layerParameter);
      ofLogNotice("MidiController") << "Binding MIDI fader " << i << " (knob index " << knobIndex
                                    << ") to Layer alpha parameter (pickup): " << layerParameter.getName();
    }
    return;
  }

  ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
  if (intentParameters.size() == 0) return;

  // Intent group is ordered: activations first, master strength last.
  size_t masterIndex = intentParameters.size() - 1;
  size_t activationCount = std::min<size_t>(7, masterIndex);

  for (size_t i = 0; i < activationCount; ++i) {
    ofParameter<float>& intentParameter = intentParameters.getFloat(i);
    int knobIndex = kFaderKnobOffset + (int)i;
    lc->knobPickup(knobIndex, intentParameter);
    ofLogNotice("MidiController") << "Binding MIDI fader " << i << " (knob index " << knobIndex
                                  << ") to Intent parameter (pickup): " << intentParameter.getName();
  }

  ofParameter<float>& masterStrengthParameter = intentParameters.getFloat(masterIndex);
  lc->knobPickup(kFaderKnobOffset + 7, masterStrengthParameter);
  ofLogNotice("MidiController") << "Binding MIDI fader 7 (knob index " << (kFaderKnobOffset + 7)
                                << ") to master intent (pickup): " << masterStrengthParameter.getName();
}

void MidiController::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr) {
  this->synthPtr = synthPtr;

  if (!lc) {
    lc = std::make_unique<ofxLaunchControlXL>();
  } else {
    // Defensive: ensure we don't accumulate event listeners across config reloads.
    lc->shutdown();
  }

  ofLogNotice() << "Setting up Launch Control XL MIDI controller";

  lc->listDevices();

  // Use DAW mode for LED control capability.
  // This uses the DAW MIDI port for both input and output.
  if (!lc->setupDawMode()) {
    ofLogWarning("MidiController") << "DAW mode setup failed, falling back to Custom mode (no LED control)";
    if (!lc->setup()) return;
  }

  // Register ourselves as an additional MIDI listener to receive button CCs.
  // The addon handles knobs internally, but we handle buttons ourselves.
  lc->addMidiListener(this);

  // Explicitly keep the 3rd row of rotaries (17-24) unbound.
  for (int i = 0; i < 8; ++i) {
    lc->clearKnob(16 + i);
  }

  // === Knob bindings (using addon with pickup/soft-takeover) ===

  // Global agency knob (encoder 0)
  lc->knobPickup(0, synthPtr->findParameterByNamePrefix("Synth Agency")->get().cast<float>());

  // Bind knobs to audio analysis parameters if they exist for this Synth
  auto bindKnob = [&](const std::string& name, int knobId) {
    auto paramWrapper = synthPtr->findParameterByNamePrefix(name);
    if (paramWrapper != std::nullopt) {
      lc->knobPickup(knobId, paramWrapper->get().cast<float>());
    }
  };

  bindKnob("MinPitch", 2);
  bindKnob("MaxPitch", 3);
  bindKnob("MinRms", 10);
  bindKnob("MaxRms", 11);
  bindKnob("MinComplexSpectralDifference", 4);
  bindKnob("MaxComplexSpectralDifference", 5);
  bindKnob("MinSpectralCrest", 12);
  bindKnob("MaxSpectralCrest", 13);
  bindKnob("MinZeroCrossingRate", 14);
  bindKnob("MaxZeroCrossingRate", 15);

  // NOTE: We do NOT use addon's toggleButton() for any buttons.
  // All button handling is done manually in handleButtonCC().

  // Apply the active fader bank (intent vs layer alpha).
  applyFaderBank();

  // Set initial LED colors for all controls.
  setupInitialLeds();

  // Setup OLED display (shares MIDI output with LED controller)
  if (auto* leds = lc->getLeds()) {
    if (auto* midiOut = leds->getMidiOut()) {
      display = std::make_unique<ofxLaunchControlXL3Display>();
      display->setup(midiOut);
      disableControlAutoDisplays();
      updateStationaryDisplay();
    }
  }
}

void MidiController::onSynthWillUnload() {
  // During config switching, Synth-owned parameters are destroyed/recreated.
  // We must drop all bindings immediately so we don't dereference stale params.
  //
  // Keeping the controller object alive (vs deleting it) avoids teardown races
  // with any in-flight MIDI callbacks.
  if (lc) {
    lc->removeMidiListener(this);
    lc->shutdown();
  }
  synthPtr.reset();
  heldKeys.clear();
}

void MidiController::exit() {
  if (lc) {
    lc->removeMidiListener(this);
    lc->shutdown();
    lc.reset();
  }
  display.reset();
  heldKeys.clear();
}

void MidiController::updateStationaryDisplay() {
  if (!display || !synthPtr) return;

  // Line 1 (Title): config filename
  std::string configName;
  const std::string& configPath = synthPtr->getCurrentConfigPath();
  if (!configPath.empty()) {
    std::filesystem::path p(configPath);
    configName = p.filename().string();
  }

  // Line 2 (Name): countdown (time remaining) if duration configured, else config time
  auto& nav = synthPtr->getPerformanceNavigator();
  char timerBuf[32];
  
  if (nav.hasConfigDuration()) {
    int countdownMin = nav.getCountdownMinutes();
    int countdownSec = nav.getCountdownSeconds();
    const char* sign = nav.isCountdownNegative() ? "-" : "";
    std::snprintf(timerBuf, sizeof(timerBuf), "%s%02d:%02d", 
                  sign, countdownMin, countdownSec);
  } else {
    // No duration: show config time
    int configMinutes = synthPtr->getConfigRunningMinutes();
    int configSeconds = synthPtr->getConfigRunningSeconds();
    std::snprintf(timerBuf, sizeof(timerBuf), "%02d:%02d", 
                  configMinutes, configSeconds);
  }
  std::string timerStr = timerBuf;

  // Line 3 (Value): status indicators
  std::string statusLine;
  if (synthPtr->isRecording()) {
    statusLine = "REC";
  }
  int activeSaveCount = synthPtr->getActiveSaveCount();
  if (activeSaveCount > 0) {
    if (!statusLine.empty()) statusLine += " ";
    statusLine += std::to_string(activeSaveCount) + " SAV";
  }

  display->setStationary3Line(configName, timerStr, statusLine);
}

void MidiController::showTempDisplay(const std::string& name, const std::string& value) {
  if (!display) return;
  display->showTemporary(name, value);
}

void MidiController::disableControlAutoDisplays() {
  if (!display) return;

  // Disable auto-temp-display for all faders (targets 5-12)
  for (uint8_t i = 5; i <= 12; ++i) {
    display->cancelControlDisplay(i);
  }

  // Disable auto-temp-display for all encoders (targets 13-36)
  for (uint8_t i = 13; i <= 36; ++i) {
    display->cancelControlDisplay(i);
  }

  // Disable auto-temp-display for all buttons (targets 37-52)
  for (uint8_t i = 37; i <= 52; ++i) {
    display->cancelControlDisplay(i);
  }
}

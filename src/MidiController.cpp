#include "MidiController.h"

#include <algorithm>
#include <optional>
#include <string>

#include "ofMain.h"

namespace {
constexpr int kFaderKnobOffset = 24;
}

MidiController::MidiController() {
  agencyLayerToggleParameter.addListener(this, &MidiController::onAgencyLayerToggleChanged);
}

void MidiController::update() {
  // Process queued button events on the main thread
  int writeIndex = buttonEventWriteIndex.load();
  while (buttonEventReadIndex != writeIndex) {
    const auto& event = buttonEventBuffer[buttonEventReadIndex];
    handleButtonCC(event.cc, event.value);
    buttonEventReadIndex = (buttonEventReadIndex + 1) % kButtonEventBufferSize;
  }
}

void MidiController::newMidiMessage(ofxMidiMessage& message) {
  // Queue button CC events for processing on the main thread.
  // This avoids threading issues with OpenGL calls (e.g., video recording).
  if (message.status == MIDI_CONTROL_CHANGE) {
    int writeIndex = buttonEventWriteIndex.load();
    buttonEventBuffer[writeIndex] = {message.control, message.value};
    int nextIndex = (writeIndex + 1) % kButtonEventBufferSize;
    buttonEventWriteIndex.store(nextIndex);
  }
}

void MidiController::handleButtonCC(int cc, int value) {
  bool pressed = value > 64;

  // Agency/Layer toggle (CC 47) - latching behavior
  if (cc == kToggleButtonCC) {
    if (pressed) {
      agencyLayerToggleParameter = !agencyLayerToggleParameter;
      // LED update happens in onAgencyLayerToggleChanged
    }
    return;
  }

  // Top row function buttons (CC 37-44)
  if (cc >= kFunctionButtonCCFirst && cc <= kFunctionButtonCCLast) {
    int index = cc - kFunctionButtonCCFirst;
    if (pressed) {
      setButtonLedByCC(cc, kButtonPressedColor);
      if (synthPtr) {
        if (!agencyLayerToggleParameter) {
          synthPtr->loadModSnapshotSlot(index);
        } else {
          synthPtr->toggleLayerPauseSlot(index);
        }
      }
    } else {
      // Restore to mode color
      LedColor modeColor = agencyLayerToggleParameter ? kLayerModeColor : kAgencyModeColor;
      setButtonLedByCC(cc, modeColor);
    }
    return;
  }

  // Bottom row keypress buttons
  int key = 0;
  switch (cc) {
    case kPauseButtonCC:      key = ' '; break;
    case kHibernateButtonCC:  key = 'H'; break;
    case kPrevConfigButtonCC: key = OF_KEY_LEFT; break;
    case kNextConfigButtonCC: key = OF_KEY_RIGHT; break;
    case kRecordButtonCC:     key = 'R'; break;
    case kSaveButtonCC:       key = 'S'; break;
    default: return; // Unknown CC, ignore
  }

  if (pressed) {
    setButtonLedByCC(cc, kButtonPressedColor);
    sendKeyPress(key);
  } else {
    setButtonLedByCC(cc, getButtonRestoreColor(cc));
    sendKeyRelease(key);
  }
}

void MidiController::setButtonLedByCC(int cc, const LedColor& color) {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  // Convert CC to button number (1-16)
  // Top row: CC 37-44 → buttons 1-8
  // Bottom row: CC 45-52 → buttons 9-16
  int buttonNum = 0;
  if (cc >= kFunctionButtonCCFirst && cc <= kFunctionButtonCCLast) {
    buttonNum = cc - kFunctionButtonCCFirst + 1;  // 1-8
  } else if (cc >= kPauseButtonCC && cc <= kSaveButtonCC) {
    buttonNum = cc - kPauseButtonCC + 9;  // 9-16
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

void MidiController::onAgencyLayerToggleChanged(bool& value) {
  ofLogNotice("MidiController") << "Agency/Layer toggle changed to "
                               << (value ? "Layer mode (green)" : "Agency mode (red)");
  applyFaderBank();
  updateModeLeds();
}

void MidiController::updateModeLeds() {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  LedColor modeColor = agencyLayerToggleParameter ? kLayerModeColor : kAgencyModeColor;

  // Toggle button (CC 47 → button 11)
  leds->setButtonLED(11, modeColor);

  // Top row buttons (CC 37-44 → buttons 1-8)
  for (int i = 1; i <= 8; ++i) {
    leds->setButtonLED(i, modeColor);
  }
}

void MidiController::setupInitialLeds() {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  // === Toggle button (CC 47 → button 11) - starts in Agency mode (red) ===
  leds->setButtonLED(11, kAgencyModeColor);

  // === Top row (CC 37-44 → buttons 1-8) - Agency mode color (red) ===
  for (int i = 1; i <= 8; ++i) {
    leds->setButtonLED(i, kAgencyModeColor);
  }

  // === Bottom row keypress buttons ===
  leds->setButtonLED(9, kPauseButtonColor);       // CC 45 - space
  leds->setButtonLED(10, kHibernateButtonColor);  // CC 46 - H
  // Button 11 (CC 47) is toggle, already set above
  leds->setButtonLED(12, kPrevConfigButtonColor); // CC 48 - left arrow
  leds->setButtonLED(13, kNextConfigButtonColor); // CC 49 - right arrow
  leds->setButtonLED(14, kOffColor);              // CC 50 - unassigned
  leds->setButtonLED(15, kRecordButtonColor);     // CC 51 - R
  leds->setButtonLED(16, kSaveButtonColor);       // CC 52 - S

  // Store restore colors for keypress buttons
  buttonRestoreColors[kPauseButtonCC] = kPauseButtonColor;
  buttonRestoreColors[kHibernateButtonCC] = kHibernateButtonColor;
  buttonRestoreColors[kPrevConfigButtonCC] = kPrevConfigButtonColor;
  buttonRestoreColors[kNextConfigButtonCC] = kNextConfigButtonColor;
  buttonRestoreColors[kRecordButtonCC] = kRecordButtonColor;
  buttonRestoreColors[kSaveButtonCC] = kSaveButtonColor;

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

  if (agencyLayerToggleParameter) {
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

  // On each config load, start layer alpha fully on.
  setLayerAlphasFullyOn();

  // Apply the active fader bank (intent vs layer alpha).
  applyFaderBank();

  // Set initial LED colors for all controls.
  setupInitialLeds();
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
  heldKeys.clear();
}

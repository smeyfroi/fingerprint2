#include "MidiController.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>

#include "ofMain.h"

namespace {
constexpr int kFaderKnobOffset = 24;
constexpr float kIntentEpsilon = 0.0001f;
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

  // Dismiss temporary display after timeout
  if (tempDisplayDismissTimeMs > 0 && ofGetElapsedTimeMillis() >= tempDisplayDismissTimeMs) {
    tempDisplayDismissTimeMs = 0;
    if (display) {
      display->clearTemporary();
    }
  }

  // Poll recording, saving states, and timers - update display when any change
  if (synthPtr) {
    const auto& runtime = synthPtr->getRuntimeSubsystem();
    bool currentRecordingState = runtime.isRecording();
    bool currentSavingState = runtime.getActiveSaveCount() > 0;

    // Get current config timer values (total seconds)
    int currentConfigTimeSeconds = runtime.getConfigRunningMinutes() * 60 + runtime.getConfigRunningSeconds();

    if (currentRecordingState != lastRecordingState ||
        currentSavingState != lastSavingState ||
        currentConfigTimeSeconds != lastDisplayedConfigTimeSeconds) {
      lastRecordingState = currentRecordingState;
      lastSavingState = currentSavingState;
      lastDisplayedConfigTimeSeconds = currentConfigTimeSeconds;
      updateStationaryDisplay();
    }
  }

  updateIntentIndicatorLeds();
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

  struct EncoderBinding {
    const char* paramName;
    bool nudgeEnabled;
    float baseStep;
  };

  auto getEncoderBinding = [&](int knobIndex) -> std::optional<EncoderBinding> {
    switch (knobIndex) {
      case 0: return EncoderBinding{"agency", false, 0.0f};
      case 8: return EncoderBinding{"AudioResp", false, 0.0f};
      case 16: return EncoderBinding{"VideoResp", false, 0.0f};

      case 2: return EncoderBinding{"MinPitch", true, 2.0f};
      case 3: return EncoderBinding{"MaxPitch", true, 2.0f};

      case 10: return EncoderBinding{"MinRms", true, 0.001f};
      case 11: return EncoderBinding{"MaxRms", true, 0.001f};

      case 4: return EncoderBinding{"MinSpectralCentroid", true, 0.5f};
      case 5: return EncoderBinding{"MaxSpectralCentroid", true, 0.5f};

      case 12: return EncoderBinding{"MinSpectralCrest", true, 2.0f};
      case 13: return EncoderBinding{"MaxSpectralCrest", true, 2.0f};

      case 20: return EncoderBinding{"MinZeroCrossingRate", true, 0.5f};
      case 21: return EncoderBinding{"MaxZeroCrossingRate", true, 0.5f};

      default: return std::nullopt;
    }
  };

  // === Shift button (CC 63 on channel 7) - latching toggle ===
  if (cc == kShiftButtonCC && channel == kShiftButtonChannel) {
    if (pressed) {
      shiftModeParameter = !shiftModeParameter;
      // LED update and display update happen in onShiftModeChanged
    }
    return;
  }

  // === Top row function buttons (CC 37-44) ===
  // Used as intent indicator LEDs only (no button actions).
  if (cc >= kFunctionButtonCCFirst && cc <= kFunctionButtonCCLast) {
    return;
  }

  // === Bottom row buttons (CC 45-52) ===
  // Always active Mod Snapshot load (independent of shift mode).
  if (cc >= kBottomRowButtonCCFirst && cc <= kBottomRowButtonCCLast) {
    int index = cc - kBottomRowButtonCCFirst;
    if (pressed) {
      setButtonLedByCC(cc, kButtonPressedColor);
      if (synthPtr) {
        synthPtr->loadModSnapshotSlot(index);
        showTempDisplay("Snapshot", std::to_string(index + 1));
      }
    } else {
      setButtonLedByCC(cc, kAgencyModeColor);
    }
    return;
  }

  // === Fader movement (CC 5-12) ===
  // Faders are bound via the addon (pickup/soft-takeover); we only drive OLED overlays.
  if (cc >= 5 && cc <= 12) {
    if (synthPtr && display) {
      const uint64_t nowMs = ofGetElapsedTimeMillis();
      const int faderIndex = cc - 5; // 0-7
      auto& state = faderOverlayStates[static_cast<size_t>(faderIndex)];

      if (shiftModeParameter) {
        // Shift ON: faders map to layer alphas.
        ofParameterGroup& layerAlphaParameters = synthPtr->getRenderSubsystem().getLayerAlphaParameters();
        if (faderIndex >= 0 && faderIndex < static_cast<int>(layerAlphaParameters.size())) {
          ofParameter<float>& param = layerAlphaParameters.getFloat(static_cast<size_t>(faderIndex));
          const float paramValue = param.get();

          const bool ccChanged = (state.lastCcValue >= 0) && (state.lastCcValue != value);
          const bool paramChanged = (std::abs(paramValue - state.lastParamValue) > 1e-4f);
          const bool pickupLikely = ccChanged && !paramChanged;

          maybeShowFaderOverlay(faderIndex, param.getName(), paramValue, pickupLikely, nowMs);

          state.lastCcValue = value;
          state.lastParamValue = paramValue;
        }
      } else {
        // Shift OFF: faders map to intent activations + master.
        ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
        if (intentParameters.size() > 0) {
          const size_t masterIndex = intentParameters.size() - 1;
          const size_t activationCount = std::min<size_t>(7, masterIndex);
          const bool isMaster = (faderIndex == 7);

          if (isMaster || static_cast<size_t>(faderIndex) < activationCount) {
            ofParameter<float>& param = isMaster ? intentParameters.getFloat(masterIndex)
                                                 : intentParameters.getFloat(static_cast<size_t>(faderIndex));
            const float paramValue = param.get();

            const bool ccChanged = (state.lastCcValue >= 0) && (state.lastCcValue != value);
            const bool paramChanged = (std::abs(paramValue - state.lastParamValue) > 1e-4f);
            const bool pickupLikely = ccChanged && !paramChanged;

            maybeShowFaderOverlay(faderIndex, param.getName(), paramValue, pickupLikely, nowMs);

            state.lastCcValue = value;
            state.lastParamValue = paramValue;
          }
        }
      }
    }
    return;
  }

  // === Encoder movement (rotaries) ===
  // In DAW mode, the 24 encoders send CC 13-36.
  // We use this to show a short-lived OLED overlay while the rotary is moving.
  //
  // NOTE: Agency (encoder 0) is handled by the addon via pickup/soft-takeover.
  // Audio analysis encoders are handled here as relative/nudge controls with
  // clutch + acceleration.
  if (cc >= kEncoderCcFirst && cc <= kEncoderCcLast) {
    if (synthPtr && display) {
      const uint64_t nowMs = ofGetElapsedTimeMillis();
      int knobIndex = cc - kEncoderCcFirst;

      auto bindingOpt = getEncoderBinding(knobIndex);
      if (!bindingOpt) return;

      auto paramOpt = synthPtr->findParameterByNamePrefix(bindingOpt->paramName);
      if (!paramOpt) return;

      auto showKnobOverlay = [&](const std::string& name, const std::string& value, uint64_t durationMs = kKnobTempDisplayDurationMs) {
        display->showTemporary(name, value);
        tempDisplayDismissTimeMs = nowMs + durationMs;
      };
      auto formatKnobValue = [](float value) {
        return ofToString(value, 2);
      };

      if (!bindingOpt->nudgeEnabled) {
        showKnobOverlay(bindingOpt->paramName, formatKnobValue(paramOpt->get().cast<float>().get()));
        return;
      }

      // Nudge tuning uses incoming CC deltas, not absolute mapping.
      auto& state = encoderNudgeStates[(size_t)knobIndex];
      const int ccValue = value;

      auto determineMode = [&](EncoderClutchMode current) {
        switch (current) {
          case EncoderClutchMode::Active:
            if (ccValue <= kLowClutchEnterCc) return EncoderClutchMode::ClutchLow;
            if (ccValue >= kHighClutchEnterCc) return EncoderClutchMode::ClutchHigh;
            return EncoderClutchMode::Active;
          case EncoderClutchMode::ClutchLow:
            if (ccValue >= kLowClutchExitCc) return EncoderClutchMode::Active;
            return EncoderClutchMode::ClutchLow;
          case EncoderClutchMode::ClutchHigh:
            if (ccValue <= kHighClutchExitCc) return EncoderClutchMode::Active;
            return EncoderClutchMode::ClutchHigh;
        }
        return EncoderClutchMode::Active;
      };

      auto& param = paramOpt->get().cast<float>();

      // First touch: arm without changing anything.
      if (state.lastCcValue < 0) {
        state.lastCcValue = ccValue;
        state.lastEventTimeMs = nowMs;
        state.smoothedSpeedCcs = 0.0f;
        state.clutchMode = determineMode(EncoderClutchMode::Active);
        showKnobOverlay(bindingOpt->paramName, std::string("ARM ") + formatKnobValue(param.get()));
        return;
      }

      // Update clutch mode with hysteresis.
      const EncoderClutchMode prevMode = state.clutchMode;
      const EncoderClutchMode nextMode = determineMode(prevMode);
      if (nextMode != prevMode) {
        state.clutchMode = nextMode;
        state.lastCcValue = ccValue;
        state.lastEventTimeMs = nowMs;
        const char* modeStr = (nextMode == EncoderClutchMode::ClutchLow) ? "CLUTCH LO" :
                              (nextMode == EncoderClutchMode::ClutchHigh) ? "CLUTCH HI" :
                              "ACTIVE";

        std::string nameLine = std::string(bindingOpt->paramName) + " [" + modeStr + "]";
        std::string valueLine;
        if (nextMode == EncoderClutchMode::ClutchLow) {
          valueLine = "EXIT>=" + std::to_string(kLowClutchExitCc);
        } else if (nextMode == EncoderClutchMode::ClutchHigh) {
          valueLine = "EXIT<=" + std::to_string(kHighClutchExitCc);
        } else {
          valueLine = "VAL " + formatKnobValue(param.get());
        }

        showKnobOverlay(nameLine, valueLine, kKnobStatusDisplayDurationMs);
        return;
      }

      if (state.clutchMode != EncoderClutchMode::Active) {
        state.lastCcValue = ccValue;
        state.lastEventTimeMs = nowMs;
        const char* modeStr = (state.clutchMode == EncoderClutchMode::ClutchLow) ? "CLUTCH LO" : "CLUTCH HI";
        const std::string nameLine = std::string(bindingOpt->paramName) + " [" + modeStr + "]";
        const std::string valueLine = (state.clutchMode == EncoderClutchMode::ClutchLow)
                                        ? ("EXIT>=" + std::to_string(kLowClutchExitCc))
                                        : ("EXIT<=" + std::to_string(kHighClutchExitCc));
        showKnobOverlay(nameLine, valueLine, kKnobStatusDisplayDurationMs);
        return;
      }

      uint64_t dtMs = nowMs - state.lastEventTimeMs;
      if (dtMs > kNudgeRearmGapMs) {
        state.lastCcValue = ccValue;
        state.lastEventTimeMs = nowMs;
        state.smoothedSpeedCcs = 0.0f;
        showKnobOverlay(bindingOpt->paramName, std::string("ARM ") + formatKnobValue(param.get()));
        return;
      }
      dtMs = std::max<uint64_t>(dtMs, 1);

      const int prevCcValue = state.lastCcValue;
      int deltaCc = ccValue - prevCcValue;
      deltaCc = std::clamp(deltaCc, -kNudgeMaxDeltaCc, kNudgeMaxDeltaCc);
      state.lastCcValue = ccValue;
      state.lastEventTimeMs = nowMs;

      if (deltaCc == 0) {
        showKnobOverlay(bindingOpt->paramName, formatKnobValue(param.get()));
        return;
      }

      const float speedCcs = std::abs((float)deltaCc) * 1000.0f / (float)dtMs;
      state.smoothedSpeedCcs = (1.0f - kNudgeSpeedSmoothing) * state.smoothedSpeedCcs + kNudgeSpeedSmoothing * speedCcs;

      const float mult = ofClamp(1.0f + state.smoothedSpeedCcs * kNudgeAccelGain, 1.0f, kNudgeMaxMult);
      const float increment = (float)deltaCc * bindingOpt->baseStep * mult;

      const float current = param.get();
      const float next = std::clamp(current + increment, param.getMin(), param.getMax());
      param.set(next);

      std::string nameLine = bindingOpt->paramName;
      if (next <= param.getMin() + 1e-6f && deltaCc < 0) {
        nameLine += " [MIN]";
      } else if (next >= param.getMax() - 1e-6f && deltaCc > 0) {
        nameLine += " [MAX]";
      }

      const uint64_t durationMs = (nameLine.find("[") != std::string::npos) ? kKnobStatusDisplayDurationMs : kKnobTempDisplayDurationMs;
      showKnobOverlay(nameLine, formatKnobValue(next) + "  x" + ofToString(mult, 1), durationMs);
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

  // Unhandled CCs ignored.
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

  // Bottom row buttons (9-16): always Mod Snapshot buttons
  for (int i = 9; i <= 16; ++i) {
    leds->setButtonLED(i, kAgencyModeColor);
  }

  // Top row buttons (1-8): intent indicators (updated per-frame)
  updateIntentIndicatorLeds();
}

void MidiController::updateIntentIndicatorLeds() {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  std::array<LedColor, 8> desiredColors {
    kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor
  };

  if (synthPtr) {
    ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
    if (intentParameters.size() > 0) {
      size_t masterIndex = intentParameters.size() - 1;
      size_t activationCount = std::min<size_t>(7, masterIndex);

      float masterStrength = intentParameters.getFloat(masterIndex).get();

      for (size_t i = 0; i < activationCount; ++i) {
        float value = intentParameters.getFloat(i).get();
        if (masterStrength <= kIntentEpsilon) {
          desiredColors[i] = kDimIntentColor;
        } else {
          desiredColors[i] = (value > kIntentEpsilon) ? kBrightIntentColor : kDimIntentColor;
        }
      }

      desiredColors[7] = (masterStrength > kIntentEpsilon) ? kBrightIntentColor : kDimIntentColor;
    }
  }

  for (size_t i = 0; i < desiredColors.size(); ++i) {
    const LedColor& desired = desiredColors[i];
    LedColor& current = lastIntentIndicatorColors[i];
    if (desired.r != current.r || desired.g != current.g || desired.b != current.b) {
      leds->setButtonLED((int)i + 1, desired);
      current = desired;
    }
  }
}

void MidiController::setupInitialLeds() {
  auto* leds = lc ? lc->getLeds() : nullptr;
  if (!leds) return;

  // === Top row (buttons 1-8): intent indicator LEDs (set per-frame) ===
  for (int i = 1; i <= 8; ++i) {
    leds->setButtonLED(i, kOffColor);
  }
  lastIntentIndicatorColors.fill(kOffColor);

  // === Bottom row buttons (9-16): Mod Snapshot 1-8 (always active) ===
  for (int i = 9; i <= 16; ++i) {
    leds->setButtonLED(i, kAgencyModeColor);
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
  leds->setEncoderLED(9, kAgencyEncoderColor);    // Encoder 8 - audio response
  leds->setEncoderLED(10, kOffColor);             // Encoder 9 - unused
  leds->setEncoderLED(11, kBlueEncoderColor);     // Encoder 10 - blue
  leds->setEncoderLED(12, kCyanEncoderColor);     // Encoder 11 - cyan
  leds->setEncoderLED(13, kPurpleEncoderColor);   // Encoder 12 - purple
  leds->setEncoderLED(14, kMagentaEncoderColor);  // Encoder 13 - magenta
  leds->setEncoderLED(15, kOffColor);             // Encoder 14 - unused
  leds->setEncoderLED(16, kOffColor);             // Encoder 15 - unused

  // Row 3 (encoders 17-24): mostly unused
  for (int i = 17; i <= 24; ++i) {
    leds->setEncoderLED(i, kOffColor);
  }
  leds->setEncoderLED(17, kAgencyEncoderColor);   // Encoder 16 - video response
  leds->setEncoderLED(21, kPurpleEncoderColor);  // Encoder 20 - purple
  leds->setEncoderLED(22, kMagentaEncoderColor); // Encoder 21 - magenta
}

void MidiController::setLayerAlphasFullyOn() {
  if (!synthPtr) return;

  ofParameterGroup& layerAlphaParameters = synthPtr->getRenderSubsystem().getLayerAlphaParameters();
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
    ofParameterGroup& layerAlphaParameters = synthPtr->getRenderSubsystem().getLayerAlphaParameters();
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

  // Register ourselves as an additional MIDI listener to receive button/CC events.
  // The addon handles faders and any explicitly-bound knobs; we handle buttons and
  // custom encoder behavior ourselves.
  lc->addMidiListener(this);

  // Ensure no stale rotary bindings survive across config reloads.
  for (int i = 0; i < 24; ++i) {
    lc->clearKnob(i);
  }
  for (auto& state : encoderNudgeStates) {
    state = {};
  }
  for (auto& state : faderOverlayStates) {
    state = {};
  }

  // === Knob bindings ===

  // Agency-family encoders use pickup/soft-takeover with a slightly wider
  // tolerance so the XL3 rings pick up reliably at visual midpoint.
  if (auto agencyParamOpt = synthPtr->findParameterByNamePrefix("agency")) {
    lc->knobPickup(0, agencyParamOpt->get().cast<float>(), kAgencyEncoderPickupToleranceCc);
  } else {
    ofLogWarning("MidiController") << "Agency parameter not found; leaving encoder 0 unbound";
  }
  if (auto audioRespParamOpt = synthPtr->findParameterByNamePrefix("AudioResp")) {
    lc->knobPickup(8, audioRespParamOpt->get().cast<float>(), kAgencyEncoderPickupToleranceCc);
  } else {
    ofLogWarning("MidiController") << "AudioResp parameter not found; leaving encoder 8 unbound";
  }
  if (auto videoRespParamOpt = synthPtr->findParameterByNamePrefix("VideoResp")) {
    lc->knobPickup(16, videoRespParamOpt->get().cast<float>(), kAgencyEncoderPickupToleranceCc);
  } else {
    ofLogWarning("MidiController") << "VideoResp parameter not found; leaving encoder 16 unbound";
  }

  // Audio analysis encoders are handled as relative/nudge controls in handleButtonCC().

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
  const std::string& configPath = synthPtr->getConfigSubsystem().getCurrentConfigPath();
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
    const auto& runtime = synthPtr->getRuntimeSubsystem();
    int configMinutes = runtime.getConfigRunningMinutes();
    int configSeconds = runtime.getConfigRunningSeconds();
    std::snprintf(timerBuf, sizeof(timerBuf), "%02d:%02d", 
                  configMinutes, configSeconds);
  }
  std::string timerStr = timerBuf;

  // Line 3 (Value): status indicators
  std::string statusLine;
  const auto& runtime = synthPtr->getRuntimeSubsystem();
  if (runtime.isRecording()) {
    statusLine = "REC";
  }
  int activeSaveCount = runtime.getActiveSaveCount();
  if (activeSaveCount > 0) {
    if (!statusLine.empty()) statusLine += " ";
    statusLine += std::to_string(activeSaveCount) + " SAV";
  }

  display->setStationary3Line(configName, timerStr, statusLine);
}

void MidiController::showTempDisplay(const std::string& name, const std::string& value) {
  if (!display) return;
  display->showTemporary(name, value);
  tempDisplayDismissTimeMs = ofGetElapsedTimeMillis() + kTempDisplayDurationMs;
}

void MidiController::maybeShowFaderOverlay(int faderIndex, const std::string& name, float paramValue, bool pickupLikely, uint64_t nowMs) {
  if (!display || faderIndex < 0 || faderIndex >= static_cast<int>(faderOverlayStates.size())) return;

  auto& state = faderOverlayStates[static_cast<size_t>(faderIndex)];
  const std::string valueLine = ofToString(paramValue, 3);

  std::string nameLine = name;
  if (pickupLikely) {
    nameLine += " [PICKUP]";
  }

  const bool contentChanged = (nameLine != state.lastName) || (valueLine != state.lastValue);
  const bool intervalOk = (nowMs - state.lastSendTimeMs) >= kFaderTempDisplayMinIntervalMs;

  if (intervalOk && contentChanged) {
    showTempDisplay(nameLine, valueLine);
    state.lastSendTimeMs = nowMs;
    state.lastName = nameLine;
    state.lastValue = valueLine;
  }
}

void MidiController::showLayerAlphaOverlay(int layerIndex, bool pickedUp) {
  if (!synthPtr || !display) return;
  if (layerIndex < 0) return;

  ofParameterGroup& layerAlphaParameters = synthPtr->getRenderSubsystem().getLayerAlphaParameters();
  if (layerIndex >= static_cast<int>(layerAlphaParameters.size())) return;

  ofParameter<float>& param = layerAlphaParameters.getFloat(static_cast<size_t>(layerIndex));
  const float paramValue = param.get();
  const uint64_t nowMs = ofGetElapsedTimeMillis();
  maybeShowFaderOverlay(layerIndex, param.getName(), paramValue, !pickedUp, nowMs);

  auto& state = faderOverlayStates[static_cast<size_t>(layerIndex)];
  state.lastParamValue = paramValue;
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

#include "MidiController.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>

#include "FaderPickup.h"
#include "MidiPortScan.h"
#include "ofMain.h"

namespace {
constexpr float kIntentEpsilon = 0.0001f;
}

MidiController::MidiController() = default;

MidiController::~MidiController() {
  disconnect();
}

bool MidiController::tryConnect() {
  if (connected) return true;

  midiIn.listInPorts();

  const int inPort = findMidiInPortMatching(midiIn, kDawPortPattern,
                                            {kDeviceNameShort, kDeviceNameLong});
  if (inPort < 0) {
    ofLogNotice("MidiController") << "Launch Control XL 3 DAW input port not found";
    return false;
  }
  ofLogNotice("MidiController") << "Found DAW input port: " << midiIn.getInPortName(inPort);

  if (!midiIn.openPort(inPort)) {
    ofLogWarning("MidiController") << "Failed to open DAW input port";
    return false;
  }
  midiIn.addListener(this);

  // The LED controller finds and owns the matching DAW *output* port (scanned
  // against the out-port list, which is a different list from the in-ports
  // above) and puts the device into DAW mode. The OLED display shares it.
  leds = std::make_unique<ofxLaunchControlXL3Leds>();
  if (!leds->setup(true)) {
    ofLogWarning("MidiController") << "DAW output unavailable; no LED or OLED feedback";
    leds.reset();
  } else if (auto* midiOut = leds->getMidiOut()) {
    display = std::make_unique<ofxLaunchControlXL3Display>();
    display->setup(midiOut);
    disableControlAutoDisplays();
  }

  connected = true;
  ofLogNotice("MidiController") << "Connected to Launch Control XL 3";
  return true;
}

void MidiController::disconnect() {
  if (!connected) return;

  midiIn.removeListener(this);
  midiIn.closePort();

  // Order matters: the display borrows the LED controller's ofxMidiOut, so it
  // must go first. ~ofxLaunchControlXL3Leds leaves DAW mode and closes the port.
  display.reset();
  leds.reset();

  connected = false;
  ofLogNotice("MidiController") << "Disconnected from Launch Control XL 3";
}

void MidiController::update() {
  // Process queued button events on the main thread
  buttonEventRing.drain([this](const ButtonEvent& event) {
    handleButtonCC(event.channel, event.cc, event.value);
  });

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
    buttonEventRing.push({message.channel, message.control, message.value});
  }
}

void MidiController::handleFaderCC(int faderIndex, int value) {
  if (!synthPtr) return;
  if (faderIndex < 0 || faderIndex >= kFaderCount) return;

  // Poles in group order on faders 0-6, master IntentStrength on fader 7 (the
  // group's last index) — master returned to the Novation when Ordered was
  // dropped. Looked up fresh on every message rather than bound once: the Synth
  // destroys and rebuilds its parameter tree across config loads, so a stored
  // reference would dangle.
  ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
  if (intentParameters.size() == 0) return;
  const size_t masterIndex = intentParameters.size() - 1;
  if (static_cast<size_t>(faderIndex) > masterIndex) return;

  ofParameter<float>& param = intentParameters.getFloat(static_cast<size_t>(faderIndex));

  // A fader with no prior sample only baselines: the takeover records where the
  // hardware sits and leaves the parameter alone. That is the one message where
  // moving the fader does nothing, so it is the one worth labelling on the OLED.
  float& lastMidiValue = faderStates[static_cast<size_t>(faderIndex)].lastMidiValue;
  const bool baselining = (lastMidiValue < 0.0f);

  applyPickup(param, value, lastMidiValue);

  maybeShowFaderOverlay(faderIndex, param.getName(), param.get(), baselining,
                        ofGetElapsedTimeMillis());
}

void MidiController::resetFaderPickupStates() {
  for (auto& state : faderStates) {
    state.lastMidiValue = -1.0f;
  }
  for (auto& state : faderOverlayStates) {
    state = {};
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
      // Encoders 0, 8, 16 (agency / AudioResp / VideoResp) moved to APC Mini
      // faders 1-3 — see docs/APC-Mini-Controller.md.

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

  // === Top row function buttons (CC 37-44) ===
  // Used as intent indicator LEDs only (no button actions).
  if (cc >= kFunctionButtonCCFirst && cc <= kFunctionButtonCCLast) {
    return;
  }

  // ⛑ THE BOTTOM ROW (CC 45-52) IS RETIRED, 2026-09-03. It recalled mod-snapshot slots 0-7
  // by position, and it was the ONLY surface that ever addressed a slot by its number — which
  // is why ModSnapshotManager carried an 8-slot cap that had nothing to do with snapshots.
  // Snapshots are driven from the performance grid now, as groups, so these CCs fall through
  // to the unhandled tail below and light nothing.

  // === Fader movement (CC 5-12) ===
  if (cc >= kFaderCcFirst && cc <= kFaderCcLast) {
    handleFaderCC(cc - kFaderCcFirst, value);
    return;
  }

  // === Encoder movement (rotaries) ===
  // In DAW mode, the 24 encoders send CC 13-36.
  // We use this to show a short-lived OLED overlay while the rotary is moving.
  //
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

  // Transport buttons (Play/Pause, Hibernate, Save Image, Prev/Next config)
  // are intentionally NOT handled here — they live on the KORG NanoKontrol2.
  // The Novation is faders→Intent, encoders→audio nudge, and top-row intent LEDs;
  // the bottom row is retired (see above).

  // Unhandled CCs ignored.
}

void MidiController::setButtonLedByCC(int cc, const LedColor& color) {
  if (!leds) return;

  // Convert CC to button number. Only the top row is driven now — the bottom row
  // (CC 45-52 → buttons 9-16) was snapshot recall and is retired.
  int buttonNum = 0;
  if (cc >= kFunctionButtonCCFirst && cc <= kFunctionButtonCCLast) {
    buttonNum = cc - kFunctionButtonCCFirst + 1;  // 1-8
  }

  if (buttonNum >= 1 && buttonNum <= 16) {
    leds->setButtonLED(buttonNum, color);
  }
}

void MidiController::updateIntentIndicatorLeds() {
  if (!leds) return;

  std::array<LedColor, 8> desiredColors {
    kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor
  };

  if (synthPtr) {
    ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
    if (intentParameters.size() > 0) {
      size_t masterIndex = intentParameters.size() - 1;  // 7 poles at 0-6, master at 7

      float masterStrength = intentParameters.getFloat(masterIndex).get();

      // The 7 pole LEDs (buttons 1-7) mirror the poles in their axis-pair hue
      // (same hues as the GUI Intents panel); brightness gated by master strength
      // (dim = poles armed but strength at zero). Button 8 = the master itself.
      for (size_t i = 0; i < masterIndex; ++i) {
        float value = intentParameters.getFloat(i).get();
        const LedColor& bright = kAxisBrightIntentColors[(i / 2) % 4];
        const LedColor& dim = kAxisDimIntentColors[(i / 2) % 4];
        if (masterStrength <= kIntentEpsilon) {
          desiredColors[i] = dim;
        } else {
          desiredColors[i] = (value > kIntentEpsilon) ? bright : dim;
        }
      }
      // Master strength on the 8th LED (amber, gated by its own level).
      desiredColors[masterIndex] = (masterStrength > kIntentEpsilon)
          ? LedColor{127, 90, 0} : LedColor{8, 6, 0};
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
  if (!leds) return;

  // === Top row (buttons 1-8): intent indicator LEDs (set per-frame) ===
  for (int i = 1; i <= 8; ++i) {
    leds->setButtonLED(i, kOffColor);
  }
  lastIntentIndicatorColors.fill(kOffColor);

  // === Bottom row buttons (9-16): dark ===
  // These were Mod Snapshot recall until 2026-09-03. Nothing writes them now, so they are
  // cleared once at startup and left alone.
  for (int i = 9; i <= 16; ++i) {
    leds->setButtonLED(i, kOffColor);
  }

  // === Encoder LEDs (1-based numbering, encoders 1-24) ===
  // Row 1 (encoders 1-8): indices 0-7 in addon terminology
  leds->setEncoderLED(1, kOffColor);              // Encoder 0 - unused (moved to APC fader 1)
  leds->setEncoderLED(2, kOffColor);              // Encoder 1 - unused
  leds->setEncoderLED(3, kBlueEncoderColor);      // Encoder 2 - blue
  leds->setEncoderLED(4, kCyanEncoderColor);      // Encoder 3 - cyan
  leds->setEncoderLED(5, kPurpleEncoderColor);    // Encoder 4 - purple
  leds->setEncoderLED(6, kMagentaEncoderColor);   // Encoder 5 - magenta
  leds->setEncoderLED(7, kOffColor);              // Encoder 6 - unused
  leds->setEncoderLED(8, kOffColor);              // Encoder 7 - unused

  // Row 2 (encoders 9-16): indices 8-15 in addon terminology
  leds->setEncoderLED(9, kOffColor);              // Encoder 8 - unused (moved to APC fader 2)
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
  // Encoder 16 (video response) moved to APC fader 3 — leave dark.
  leds->setEncoderLED(21, kPurpleEncoderColor);  // Encoder 20 - purple
  leds->setEncoderLED(22, kMagentaEncoderColor); // Encoder 21 - magenta
}

void MidiController::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr) {
  this->synthPtr = synthPtr;

  // Connect once and stay connected. The ports and the device's DAW mode have
  // nothing to do with which config is loaded, and re-opening them per load
  // cost a 100 ms settle inside the LED controller's setup.
  if (!connected) {
    tryConnect();
  }

  // Faders re-acquire against the new config's parameters; encoder nudge state
  // (speed, clutch) is likewise meaningless across a config change.
  resetFaderPickupStates();
  for (auto& state : encoderNudgeStates) {
    state = {};
  }

  if (!connected) {
    ofLogNotice("MidiController") << "Synth loaded but Launch Control XL 3 not connected";
    return;
  }

  // Nothing is bound here: faders resolve the Intent group and encoders the
  // audio-analysis parameters per message, so a config swap needs no rebinding.
  setupInitialLeds();
  updateStationaryDisplay();
}

void MidiController::onSynthWillUnload() {
  // The Synth destroys and recreates its parameters across a config switch.
  // Nothing here holds a parameter reference — handleFaderCC and handleButtonCC
  // resolve them per message and guard on synthPtr — so dropping the Synth
  // reference IS the teardown. The device connection stays open.
  resetFaderPickupStates();

  if (display) {
    display->clearTemporary();
  }
  tempDisplayDismissTimeMs = 0;

  synthPtr.reset();
}

void MidiController::exit() {
  disconnect();
  // Release our strong reference to the Synth (Stage 14). The other controllers
  // (Apc/NanoKontrol/Osc) already reset theirs in exit(); this one was missing it,
  // which was harmless while the Synth<->Gui cycle meant ~Synth never ran anyway.
  // Now that Stage 14 breaks that cycle, a lingering strong ref here would keep the
  // Synth alive past ofApp::exit()'s synthPtr.reset() and defer ~Synth (and the
  // audited teardown) — so drop it.
  synthPtr.reset();
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

void MidiController::maybeShowFaderOverlay(int faderIndex, const std::string& name, float paramValue, bool baselining, uint64_t nowMs) {
  if (!display || faderIndex < 0 || faderIndex >= static_cast<int>(faderOverlayStates.size())) return;

  auto& state = faderOverlayStates[static_cast<size_t>(faderIndex)];
  const std::string valueLine = ofToString(paramValue, 3);

  std::string nameLine = name;
  if (baselining) {
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

#include "MidiController.h"

#include <algorithm>
#include <optional>
#include <string>

#include "ofMain.h"

MidiController::MidiController() {
  intentLayerToggleParameter.addListener(this, &MidiController::onIntentLayerToggleChanged);

  for (int i = 0; i < 8; ++i) {
    functionButtonPressedParameters[i].set("Function Button " + std::to_string(i + 1), false);
    functionButtonListenerTokens[i] = functionButtonPressedParameters[i].newListener([this, i](bool& value) {
      if (!value) return;
      if (!synthPtr) return;

      if (!intentLayerToggleParameter) {
        synthPtr->loadModSnapshotSlot(i);
      } else {
        synthPtr->toggleLayerPauseSlot(i);
      }
    });
  }
}

void MidiController::onIntentLayerToggleChanged(bool& value) {
  ofLogNotice() << "MIDI Intent/Layer toggle changed to "
                << (value ? "Layer mode (layer alpha + layer pause)" : "Intent mode (intent strength + snapshot load)");
  applyFaderBank();
}

void MidiController::applyFaderBank() {
  if (!lc || !synthPtr) return;

  lc->clearFaders();

  if (intentLayerToggleParameter) {
    ofParameterGroup& layerAlphaParameters = synthPtr->getLayerAlphaParameters();
    size_t count = std::min<size_t>(8, layerAlphaParameters.size());
    for (size_t i = 0; i < count; ++i) {
      ofParameter<float>& layerParameter = layerAlphaParameters.getFloat(i);
      lc->fader((int)i, layerParameter);
      ofLogNotice("MidiController") << "Binding MIDI fader " << i << " to Layer alpha parameter: " << layerParameter.getName();
    };
  } else {
    ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
    if (intentParameters.size() == 0) return;

    // Intent group is ordered: activations first, master strength last.
    size_t masterIndex = intentParameters.size() - 1;
    size_t activationCount = std::min<size_t>(7, masterIndex);

    for (size_t i = 0; i < activationCount; ++i) {
      ofParameter<float>& intentParameter = intentParameters.getFloat(i);
      lc->fader((int)i, intentParameter);
      ofLogNotice("MidiController") << "Binding MIDI fader " << i << " to Intent parameter: " << intentParameter.getName();
    };

    ofParameter<float>& masterStrengthParameter = intentParameters.getFloat(masterIndex);
    lc->fader(7, masterStrengthParameter);
    ofLogNotice("MidiController") << "Binding MIDI fader 7 to master intent: " << masterStrengthParameter.getName();
  }
}

void MidiController::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr) {
  this->synthPtr = synthPtr;

  lc = std::make_unique<ofxLaunchControlXL>();
  ofLogNotice() << "Setting up Launch Control XL MIDI controller";

  lc->listDevices();
  //lc->setup(1); // setup with a defined id
  if (!lc->setup()) return; // setup with automatic id finding

  // Global agency knob
  lc->knob(0, synthPtr->findParameterByNamePrefix("Synth Agency")->get().cast<float>());

  // TODO:
  // Make a toggle-button for whether the faders control Intent or Layer alphas
  // Same for "Load Snapshot" function keys versus Layer active toggle keys

  // Intent/Layer toggle button
  lc->toggleButton(47, intentLayerToggleParameter);

  // Function buttons row (CC 37-44)
  for (int i = 0; i < 8; ++i) {
    lc->toggleButton(37 + i, functionButtonPressedParameters[i]);
  }

  // Bind faders to whichever bank is active.
  applyFaderBank();

  // Bind knobs to audio analysis parameters if the param exist for this Synth
  auto bindKnob = [&](const std::string& name, int knobId) {
    auto paramWrapper = synthPtr->findParameterByNamePrefix(name);
    if (paramWrapper != std::nullopt) lc->knob(knobId, paramWrapper->get().cast<float>());
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
}

void MidiController::onSynthWillUnload() {
  lc.reset();
  synthPtr.reset();
}

void MidiController::exit() {
  if (lc) {
    lc->close();
    lc.reset();
  }
}

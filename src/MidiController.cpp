#include "MidiController.h"

#include <algorithm>
#include <optional>
#include <string>

#include "ofMain.h"

namespace {
constexpr int kFaderKnobOffset = 24;
}

MidiController::MidiController() {
  snapshotLayerToggleParameter.addListener(this, &MidiController::onSnapshotLayerToggleChanged);

  for (int i = 0; i < 8; ++i) {
    functionButtonPressedParameters[i].set("Function Button " + std::to_string(i + 1), false);
    functionButtonListenerTokens[i] = functionButtonPressedParameters[i].newListener([this, i](bool& value) {
      if (!value) return;
      if (!synthPtr) return;

      if (!snapshotLayerToggleParameter) {
        synthPtr->loadModSnapshotSlot(i);
      } else {
        synthPtr->toggleLayerPauseSlot(i);
      }
    });
  }
}

void MidiController::onSnapshotLayerToggleChanged(bool& value) {
  ofLogNotice("MidiController") << "MIDI Snapshot/Layer toggle changed to "
                               << (value ? "Layer mode (faders: layer alpha, buttons: layer pause)"
                                         : "Snapshot mode (faders: intent, buttons: load mod snapshot)");
  applyFaderBank();
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

  if (snapshotLayerToggleParameter) {
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
  //lc->setup(1); // setup with a defined id
  if (!lc->setup()) return; // setup with automatic id finding

  // Explicitly keep the 3rd row of rotaries (17-24) unbound.
  for (int i = 0; i < 8; ++i) {
    lc->clearKnob(16 + i);
  }

  // Global agency knob
  lc->knob(0, synthPtr->findParameterByNamePrefix("Synth Agency")->get().cast<float>());

  // Snapshot/Layer toggle button
  lc->toggleButton(47, snapshotLayerToggleParameter);

  // Function buttons row (CC 37-44)
  for (int i = 0; i < 8; ++i) {
    lc->toggleButton(37 + i, functionButtonPressedParameters[i]);
  }

  // On each config load, start layer alpha fully on.
  setLayerAlphasFullyOn();

  // Apply the active fader bank (intent vs layer alpha).
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
  // During config switching, Synth-owned parameters are destroyed/recreated.
  // We must drop all bindings immediately so we don't dereference stale params.
  //
  // Keeping the controller object alive (vs deleting it) avoids teardown races
  // with any in-flight MIDI callbacks.
  if (lc) {
    lc->shutdown();
  }
  synthPtr.reset();
}

void MidiController::exit() {
  if (lc) {
    lc->shutdown();
    lc.reset();
  }
}

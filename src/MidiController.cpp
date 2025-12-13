#include "MidiController.h"

#include <optional>
#include <string>

#include "ofMain.h"

void MidiController::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr) {
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

  // Bind intent strengths to faders
  //  ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
  //  for (size_t i = 0; i < intentParameters.size(); ++i) {
  //    ofParameter<float>& intentParameter = intentParameters.getFloat(i);
  //    lc->fader(i, intentParameter);
  //    ofLogNotice("ofApp") << "Binding MIDI fader " << i << " to Intent parameter: " << intentParameter.getName();
  //  };

  // Bind layer alphas to faders
  ofParameterGroup& layerAlphaParameters = synthPtr->getLayerAlphaParameters();
  for (size_t i = 0; i < layerAlphaParameters.size(); ++i) {
    ofParameter<float>& layerParameter = layerAlphaParameters.getFloat(i);
    lc->fader(i, layerParameter);
    ofLogNotice("ofApp") << "Binding MIDI fader " << i << " to Intent parameter: " << layerParameter.getName();
  };

  // Bind knobs to audio analysis parameters if the param exist for this Synth
  auto bindKnob = [&](const std::string& name, int knobId) {
    auto paramWrapper = synthPtr->findParameterByNamePrefix(name);
    if (paramWrapper != std::nullopt) lc->knob(knobId, paramWrapper->get().cast<float>());
  };

  bindKnob("MinPitch", 2);
  bindKnob("MaxPitch", 3);
  bindKnob("MinRms", 12);
  bindKnob("MaxRms", 13);
  bindKnob("MinComplexSpectralDifference", 4);
  bindKnob("MaxComplexSpectralDifference", 5);
  bindKnob("MinSpectralCrest", 14);
  bindKnob("MaxSpectralCrest", 15);
  bindKnob("MinZeroCrossingRate", 22);
  bindKnob("MaxZeroCrossingRate", 23);
}

void MidiController::onSynthWillUnload() {
  lc.reset();
}

void MidiController::exit() {
  if (lc) {
    lc->close();
    lc.reset();
  }
}

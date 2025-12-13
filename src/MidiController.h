#pragma once

#include <array>
#include <memory>

#include "ofxLaunchControllers.h"
#include "ofxMarkSynth.h"

class MidiController {
public:
  MidiController();
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();
  void exit();
  void onIntentLayerToggleChanged(bool& value);

private:
  void applyFaderBank();

  std::unique_ptr<ofxLaunchControlXL> lc;
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;
  ofParameter<bool> intentLayerToggleParameter { "Intent/Layer Toggle", false };

  std::array<ofParameter<bool>, 8> functionButtonPressedParameters;
  std::array<std::unique_ptr<of::priv::AbstractEventToken>, 8> functionButtonListenerTokens;
};

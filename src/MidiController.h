#pragma once

#include <memory>

#include "ofxLaunchControllers.h"
#include "ofxMarkSynth.h"

class MidiController {
public:
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();
  void exit();

private:
  std::unique_ptr<ofxLaunchControlXL> lc;
};

#pragma once

#include <array>
#include <memory>

#include "ofxLaunchControllers.h"
#include "ofxMarkSynth.h"
#include "ofxMidi.h"

class MidiController : public ofxMidiListener {
 public:
  MidiController();
  void update();
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();
  void exit();
  void onSnapshotLayerToggleChanged(bool& value);

  void newMidiMessage(ofxMidiMessage& message) override;

 private:
  void applyFaderBank();
  void setLayerAlphasFullyOn();

  std::unique_ptr<ofxLaunchControlXL> lc;
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;
  ofParameter<bool> snapshotLayerToggleParameter { "Snapshot/Layer Toggle", false };

  std::array<ofParameter<bool>, 8> functionButtonPressedParameters;
  std::array<std::unique_ptr<of::priv::AbstractEventToken>, 8> functionButtonListenerTokens;
};

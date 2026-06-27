#pragma once

#include <memory>

#include "ApcMiniController.h"
#include "MidiController.h"
#include "NanoKontrol2Controller.h"
#include "OscController.h"
#include "ofxMarkSynth.h"

class ofApp : public ofBaseApp {

public:
  void setup() override;
  void setGuiWindowPtr(std::shared_ptr<ofAppBaseWindow> windowPtr) { guiWindowPtr = windowPtr; }
  void setForceChooseConfig(bool v) { forceChooseConfig = v; }
  void attachGuiWindowListeners();
  void detachGuiWindowListeners();
  void onSynthWillUnload(ofxMarkSynth::Synth::ConfigUnloadEvent& e);
  void onSynthDidLoad(ofxMarkSynth::Synth::ConfigLoadedEvent& e);
  void update() override;
  void draw() override;
  void exit() override;
  void drawGui(ofEventArgs& args);

  void keyPressedEvent(ofKeyEventArgs& e);
  void keyReleasedEvent(ofKeyEventArgs& e);
  void keyPressed(int key) override;
  void keyReleased(int key) override;
  void mouseMoved(int x, int y ) override;
  void mouseDragged(int x, int y, int button) override;
  void mousePressed(int x, int y, int button) override;
  void mouseReleased(int x, int y, int button) override;
  void mouseScrolled(int x, int y, float scrollX, float scrollY) override;
  void mouseEntered(int x, int y) override;
  void mouseExited(int x, int y) override;
  void windowResized(int w, int h) override;
  void dragEvent(ofDragInfo dragInfo) override;
  void gotMessage(ofMessage msg) override;
  
private:
  std::shared_ptr<ofAppBaseWindow> guiWindowPtr;
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;
  bool guiWindowListenersAttached { false };
  bool isShuttingDown { false };
  bool forceChooseConfig { false };
  MidiController midiController;
  ApcMiniController apcMiniController;
  NanoKontrol2Controller nanoKontrolController;
  OscController oscController;
};

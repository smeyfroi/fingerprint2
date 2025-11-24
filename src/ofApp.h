#pragma once

#include "ofMain.h"
#include "ofxMarkSynth.h"
#include "ofxLaunchControllers.h"


// ***********************************************
// ***********************************************
const std::filesystem::path FONT_PATH { "/System/Library/Fonts/Helvetica.ttc" };
const std::filesystem::path ROOT_SOURCE_MATERIAL_PATH { "/Users/steve/Documents/music-source-material" };
const std::filesystem::path SOURCE_AUDIO_PATH { ROOT_SOURCE_MATERIAL_PATH/"belfast/20250208-violin-separate-scale-vibrato-harmonics.wav" };
constexpr bool RECORD_AUDIO = false;
const std::string MIC_DEVICE_NAME = "Apple Inc.: MacBook Pro Microphone";
constexpr float FRAME_RATE = 30.0;
constexpr bool START_PAUSED = false; // false for dev
constexpr glm::vec2 SYNTH_COMPOSITE_SIZE = { 2400, 2400 }; // drawing layers are scaled down to this size to fit into the window height

//const std::filesystem::path SOURCE_VIDEO_PATH { ROOT_SOURCE_MATERIAL_PATH/"belfast/trombone-trimmed.mov" };
//const bool SOURCE_VIDEO_MUTE = false;
//const std::filesystem::path SOURCE_MATERIAL_PATH { "belfast/20250208-violin-separate-scale-vibrato-harmonics.wav" };
//const std::filesystem::path SOURCE_MATERIAL_PATH { "percussion/Alex Petcu Bell Plates.wav" };
//const std::filesystem::path SOURCE_MATERIAL_PATH { "percussion/Alex Petcu Sound Bath.wav" };
//const std::filesystem::path SOURCE_MATERIAL_PATH { "belfast/20250208-trombone-melody.wav" };
//const std::filesystem::path SOURCE_MATERIAL_PATH { "cork/audio-2025-06-16-11-16-14-782.wav" };
//const std::filesystem::path SOURCE_MATERIAL_PATH { "cork/audio-2025-06-16-11-25-03-931.wav" };
//const std::filesystem::path SOURCE_MATERIAL_PATH { "misc/nightsong.wav" };
//const std::filesystem::path SOURCE_MATERIAL_PATH { "misc/treganna.wav" };
//constexpr int VIDEO_DEVICE_ID = 0;
//constexpr bool RECORD_VIDEO = false;
//const std::string MIC_DEVICE_NAME = "Audient: iD4";
//const std::string MIC_DEVICE_NAME = "Apple Inc.: Steve\325s iPhone Microphone";
//const std::string MIC_DEVICE_NAME = "Apple Inc.: MacBook Pro Microphone";
//const std::filesystem::path RECORDING_PATH { "/Users/steve/Documents/MarkSynth/audio-recordings" };
//const std::filesystem::path IMGUI_CONFIG_PATH { "/Users/steve/Documents/MarkSynth/imgui.ini" }; // *** would need to do something about this if the app is going to be packaged
//constexpr int SOUND_OUT_DEVICE_ID = 1;
// ***********************************************
// ***********************************************

class ofApp : public ofBaseApp{
  
public:
  void setup() override;
  void setGuiWindowPtr(std::shared_ptr<ofAppBaseWindow> windowPtr) { guiWindowPtr = windowPtr; }
  void update() override;
  void draw() override;
  void exit() override;
  void drawGui(ofEventArgs& args);

  void keyPressedEvent(ofKeyEventArgs& e) { keyPressed(e.key); } // adapter for ofAddListener
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
  ofxLaunchControlXL lc;
};

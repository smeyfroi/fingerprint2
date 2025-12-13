#pragma once

#include <memory>

#include "MidiController.h"
#include "ofxMarkSynth.h"

// === RECORDED MATERIAL FOR DEVELOPMENT
const std::string SYNTH_CONFIG = "00-movement1-source";
const std::string AUDIO_FILE = "cork/audio-2025-06-14-16-38-50-531.wav";
const std::string VIDEO_FILE = "belfast/trombone-trimmed.mov";

// === ffmpeg installed via `brew install ffmpeg`
const std::filesystem::path FFMPEG_BINARY_PATH { "/opt/homebrew/bin/ffmpeg" };

// === ROOT PATHS ===
const std::filesystem::path ROOT_SOURCE_MATERIAL_PATH { "/Users/steve/Documents/music-source-material" };
const std::filesystem::path ROOT_PERFORMANCE_PATH { "/Users/steve/Documents/MarkSynth-performances/Practice" };

// === DERIVED PATHS ===
const std::filesystem::path PERFORMANCE_CONFIG_ROOT_PATH { ROOT_PERFORMANCE_PATH/"config" }; // must exist
const std::filesystem::path PERFORMANCE_ARTEFACT_ROOT_PATH { ROOT_PERFORMANCE_PATH/"artefact" }; // subdirectories created by Synth

// === SYNTH CONFIGURATION ===
constexpr glm::vec2 COMPOSITE_SIZE { 7200, 7200 }; // Todo: complete this by rippling it through all the examples
constexpr float COMPOSITE_PANEL_GAP_PX = 28.0;
// Config not in ResourceManager
constexpr bool START_PAUSED = false;
constexpr float FRAME_RATE = 30.0;
constexpr glm::vec2 VIDEO_RECORDER_SIZE { 1280, 720 };

// === AUDIO INPUT CONFIGURATION ===
// Option A: Audio file playback
#define AUDIO_FILE_PLAYBACK
const std::filesystem::path SOURCE_AUDIO_PATH { ROOT_SOURCE_MATERIAL_PATH/AUDIO_FILE };
//const std::filesystem::path SOURCE_AUDIO_PATH { ROOT_SOURCE_MATERIAL_PATH/"belfast/20250208-violin-separate-scale-vibrato-harmonics.wav" };
  //const std::filesystem::path SOURCE_MATERIAL_PATH { "percussion/Alex Petcu Bell Plates.wav" };
  //const std::filesystem::path SOURCE_MATERIAL_PATH { "percussion/Alex Petcu Sound Bath.wav" };
  //const std::filesystem::path SOURCE_MATERIAL_PATH { "belfast/20250208-trombone-melody.wav" };
  //const std::filesystem::path SOURCE_MATERIAL_PATH { "cork/audio-2025-06-16-11-25-03-931.wav" };
  //const std::filesystem::path SOURCE_MATERIAL_PATH { "misc/nightsong.wav" };
  //const std::filesystem::path SOURCE_MATERIAL_PATH { "misc/treganna.wav" };
const std::string AUDIO_OUT_DEVICE_NAME = "Apple Inc.: MacBook Pro Speakers";
constexpr int AUDIO_BUFFER_SIZE = 512; // Todo: make sure these are for the source. How does it get set for the out device?
constexpr int AUDIO_CHANNELS = 1;
constexpr int AUDIO_SAMPLE_RATE = 44100; //48000;
// Option B: Microphone input
#undef MICROPHONE_INPUT
const std::string MIC_DEVICE_NAME = "Apple Inc.: MacBook Pro Microphone";
//const std::string MIC_DEVICE_NAME = "Audient: iD4";
//const std::string MIC_DEVICE_NAME = "Apple Inc.: Steve\325s iPhone Microphone";
constexpr bool RECORD_AUDIO = false;
const std::filesystem::path AUDIO_RECORDING_PATH { PERFORMANCE_ARTEFACT_ROOT_PATH/"audio-recordings" }; // created

// === VIDEO/CAMERA INPUT CONFIGURATION ===
#define VIDEO_FILE_PLAYBACK
// Option A: Video file playback
const std::filesystem::path SOURCE_VIDEO_PATH { ROOT_SOURCE_MATERIAL_PATH/VIDEO_FILE };
constexpr bool SOURCE_VIDEO_MUTE = true;
// Option B: Camera input
#undef VIDEO_CAMERA_INPUT
constexpr int CAMERA_DEVICE_ID = 0;
const glm::vec2 VIDEO_SIZE { 640, 480 };
constexpr bool SAVE_VIDEO_RECORDING = false;
const std::filesystem::path VIDEO_RECORDING_PATH { PERFORMANCE_ARTEFACT_ROOT_PATH/"video-recordings" }; // created

// === TEXT/FONT RESOURCES ===
const std::filesystem::path FONT_PATH { "PlayfairDisplay-Regular.ttf" }; // FIXME: should be in Synth config json?
const std::string TEXT_SOURCES_PATH = PERFORMANCE_CONFIG_ROOT_PATH/"text"; // must exist



class ofApp : public ofBaseApp{
  
public:
  void setup() override;
  void setGuiWindowPtr(std::shared_ptr<ofAppBaseWindow> windowPtr) { guiWindowPtr = windowPtr; }
  void onSynthWillUnload(ofxMarkSynth::Synth::ConfigUnloadEvent& e);
  void onSynthDidLoad(ofxMarkSynth::Synth::ConfigLoadedEvent& e);
  void update() override;
  void draw() override;
  void exit() override;
  void drawGui(ofEventArgs& args);
  
  void keyPressedEvent(ofKeyEventArgs& e) { keyPressed(e.key); } // adapter for ofAddListener
  void keyReleasedEvent(ofKeyEventArgs& e) { keyReleased(e.key); } // adapter for ofAddListener
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
  MidiController midiController;
};

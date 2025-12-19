#include "ofApp.h"
#include "ofxTimeMeasurements.h"
#include "Mod.hpp"

using namespace ofxMarkSynth;



void ofApp::setup(){
  ofDisableArbTex();
  glEnable(GL_PROGRAM_POINT_SIZE);
  ofSetBackgroundColor(0);
  ofSetFrameRate(FRAME_RATE);
//  ofSetLogLevel(OF_LOG_VERBOSE);
  TIME_SAMPLE_SET_FRAMERATE(FRAME_RATE);
  TIME_SAMPLE_SET_DRAW_LOCATION(TIME_MEASUREMENTS_BOTTOM_LEFT);
  TIME_SAMPLE_DISABLE(); // *********************************
  
  ResourceManager resources;
  resources.add("performanceConfigRootPath", PERFORMANCE_CONFIG_ROOT_PATH);
  resources.add("performanceArtefactRootPath", PERFORMANCE_ARTEFACT_ROOT_PATH);
  
//  resources.add("compositeSize", COMPOSITE_SIZE);
  resources.add("compositePanelGapPx", COMPOSITE_PANEL_GAP_PX);
  resources.add("recorderCompositeSize", VIDEO_RECORDER_SIZE);
  resources.add("ffmpegBinaryPath", FFMPEG_BINARY_PATH);
  // --- Audio Input Resources ---
  // For file playback mode:
#ifdef AUDIO_FILE_PLAYBACK
  resources.add("sourceAudioPath", SOURCE_AUDIO_PATH);
  resources.add("audioOutDeviceName", AUDIO_OUT_DEVICE_NAME);
  resources.add("audioBufferSize", AUDIO_BUFFER_SIZE);
  resources.add("audioChannels", AUDIO_CHANNELS);
  resources.add("audioSampleRate", AUDIO_SAMPLE_RATE);
  resources.add("sourceAudioStartPosition", SOURCE_AUDIO_START_POSITION);
#endif
  // For microphone mode:
#ifdef MICROPHONE_INPUT
  resources.add("micDeviceName", MIC_DEVICE_NAME);
  resources.add("recordAudio", RECORD_AUDIO);
  resources.add("audioRecordingPath", AUDIO_RECORDING_PATH);
#endif
  // --- Video/Camera Input Resources ---
  // For video file playback mode:
#ifdef VIDEO_FILE_PLAYBACK
  resources.add("sourceVideoPath", SOURCE_VIDEO_PATH);
  resources.add("sourceVideoMute", SOURCE_VIDEO_MUTE);
  resources.add("sourceVideoStartPosition", SOURCE_VIDEO_START_POSITION);
#endif
  // For camera mode:
#ifdef VIDEO_CAMERA_INPUT
  resources.add("cameraDeviceId", CAMERA_DEVICE_ID);
  resources.add("videoSize", VIDEO_SIZE);
  resources.add("saveRecording", SAVE_VIDEO_RECORDING);
  resources.add("videoRecordingPath", VIDEO_RECORDING_PATH);
#endif
  // --- Text/Font Resources ---
  FontCache fontCache(ofToDataPath(FONT_PATH.string()));
  fontCache.preloadAll();
  resources.add("fontCache", fontCache);
  resources.add("textSourcesPath", TEXT_SOURCES_PATH);
  // --- Start at config ---
  resources.add("startupPerformanceConfigName", SYNTH_CONFIG);

  synthPtr = ofxMarkSynth::Synth::create("fingerprint2", ofxMarkSynth::ModConfig {
  }, START_PAUSED, COMPOSITE_SIZE, resources);
  synthPtr->loadFirstPerformanceConfig();
  ofAddListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload); // before configureGui
  ofAddListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad); // before configureGui
  synthPtr->configureGui(guiWindowPtr);
}

void ofApp::onSynthWillUnload(ofxMarkSynth::Synth::ConfigUnloadEvent& e) {
  midiController.onSynthWillUnload();
}

void ofApp::onSynthDidLoad(ofxMarkSynth::Synth::ConfigLoadedEvent& e) {
  midiController.onSynthDidLoad(synthPtr);
}

//--------------------------------------------------------------
void ofApp::update(){
  synthPtr->update();
  midiController.update();
}

//--------------------------------------------------------------
void ofApp::draw(){
  synthPtr->draw();
}

void ofApp::drawGui(ofEventArgs& args){
  synthPtr->drawGui();
}

//--------------------------------------------------------------
void ofApp::exit(){
  if (synthPtr) {
    ofRemoveListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload);
    ofRemoveListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad);
    synthPtr->shutdown();
  }

  midiController.exit();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
  synthPtr->keyPressed(key);
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
  synthPtr->keyReleased(key);
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){
  
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){
  
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){
  
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){
  
}

//--------------------------------------------------------------
void ofApp::mouseScrolled(int x, int y, float scrollX, float scrollY){
  
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){
  
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){
  
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
  
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
  
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
  
}

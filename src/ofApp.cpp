#include "ofApp.h"
#include "ofxTimeMeasurements.h"
#include "Mod.hpp"

using namespace ofxMarkSynth;



void ofApp::setup(){
  ofDisableArbTex();
  glEnable(GL_PROGRAM_POINT_SIZE);
  ofSetBackgroundColor(0);
  ofSetFrameRate(FRAME_RATE);
  TIME_SAMPLE_SET_FRAMERATE(FRAME_RATE);
  TIME_SAMPLE_SET_DRAW_LOCATION(TIME_MEASUREMENTS_BOTTOM_LEFT);
  TIME_SAMPLE_DISABLE(); // *********************************

  ResourceManager resources;
  resources.add("performanceConfigRootPath", PERFORMANCE_CONFIG_ROOT_PATH);
  resources.add("performanceArtefactRootPath", PERFORMANCE_ARTEFACT_ROOT_PATH);
  
//  resources.add("compositeSize", COMPOSITE_SIZE);
  resources.add("compositePanelGapPx", COMPOSITE_PANEL_GAP_PX);
  // --- Audio Input Resources ---
  // For file playback mode:
#ifdef AUDIO_FILE_PLAYBACK
  resources.add("sourceAudioPath", SOURCE_AUDIO_PATH);
  resources.add("audioOutDeviceName", AUDIO_OUT_DEVICE_NAME);
  resources.add("audioBufferSize", AUDIO_BUFFER_SIZE);
  resources.add("audioChannels", AUDIO_CHANNELS);
  resources.add("audioSampleRate", AUDIO_SAMPLE_RATE);
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
#endif
  // For camera mode:
#ifdef VIDEO_CAMERA_INPUT
  resources.add("cameraDeviceId", CAMERA_DEVICE_ID);
  resources.add("videoSize", VIDEO_SIZE);
  resources.add("saveRecording", SAVE_VIDEO_RECORDING);
  resources.add("videoRecordingPath", VIDEO_RECORDING_PATH);
#endif
  // --- Text/Font Resources ---
  resources.add("fontPath", FONT_PATH);
  resources.add("textSourcesPath", TEXT_SOURCES_PATH);

  synthPtr = std::make_shared<ofxMarkSynth::Synth>("fingerprint2", ofxMarkSynth::ModConfig {
  }, START_PAUSED, COMPOSITE_SIZE, resources);
  synthPtr->loadFirstPerformanceConfig();
  ofAddListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload); // before configureGui
  ofAddListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad); // before configureGui
  synthPtr->configureGui(guiWindowPtr);
}

void ofApp::onSynthWillUnload(ofxMarkSynth::Synth::ConfigUnloadEvent& e) {
  if (lc) {
    lc->close();
    lc.reset();
  }
}

void ofApp::onSynthDidLoad(ofxMarkSynth::Synth::ConfigLoadedEvent& e) {
  setupMidiController();
}

// TODO: refactor the MIDI controller setup into a separate class when we know more about it
void ofApp::setupMidiController() {
  if (lc) {
    lc->close();
    lc.reset();
    ofLogNotice() << "Re-setting up Launch Control XL MIDI controller after config load";
  }
  lc = std::make_unique<ofxLaunchControlXL>();
  ofLogNotice() << "Setting up Launch Control XL MIDI controller";

  lc->listDevices();
  //lc->setup(1); // sewtup with a defined id
  if (!lc->setup()) return; // setup with automatic id finding
    
  // Global agency knob
  lc->knob(0, synthPtr->findParameterByNamePrefix("Synth Agency")->get().cast<float>());

  // Bind intent strengths to faders
  ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
  for (size_t i = 0; i < intentParameters.size(); ++i) {
    ofParameter<float>& layerParameter = intentParameters.getFloat(i);
    lc->fader(i, layerParameter);
    ofLogNotice("ofApp") << "Binding MIDI fader " << i << " to Intent parameter: " << layerParameter.getName();
  };

  // Bind knobs to audio analysis parameters
  lc->knob(4, synthPtr->findParameterByNamePrefix("MinPitch")->get().cast<float>());
  lc->knob(5, synthPtr->findParameterByNamePrefix("MaxPitch")->get().cast<float>());
  lc->knob(12, synthPtr->findParameterByNamePrefix("MinRms")->get().cast<float>());
  lc->knob(13, synthPtr->findParameterByNamePrefix("MaxRms")->get().cast<float>());
  lc->knob(6, synthPtr->findParameterByNamePrefix("MinComplexSpectralDifference")->get().cast<float>());
  lc->knob(7, synthPtr->findParameterByNamePrefix("MaxComplexSpectralDifference")->get().cast<float>());
  lc->knob(14, synthPtr->findParameterByNamePrefix("MinSpectralCrest")->get().cast<float>());
  lc->knob(15, synthPtr->findParameterByNamePrefix("MaxSpectralCrest")->get().cast<float>());
  lc->knob(22, synthPtr->findParameterByNamePrefix("MinZeroCrossingRate")->get().cast<float>());
  lc->knob(23, synthPtr->findParameterByNamePrefix("MaxZeroCrossingRate")->get().cast<float>());
}

//--------------------------------------------------------------
void ofApp::update(){
  synthPtr->update();
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

  if (lc) {
    lc->close();
    lc.reset();
  }
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

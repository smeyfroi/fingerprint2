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
  
  ofxMarkSynth::ResourceManager resources;
  resources.add("sourceAudioPath", SOURCE_AUDIO_PATH);
  resources.add("micDeviceName", MIC_DEVICE_NAME);
  resources.add("recordAudio", RECORD_AUDIO);
  resources.add("fontPath", FONT_PATH);

  synthPtr = std::make_shared<ofxMarkSynth::Synth>("Audio Clusters", ofxMarkSynth::ModConfig {
  }, START_PAUSED, SYNTH_COMPOSITE_SIZE, resources);

  synthPtr->loadFromConfig(ofToDataPath("1.json"));
  synthPtr->configureGui(guiWindowPtr);
  
  // >>> TODO: refactor the MIDI controller setup into a separate class when we know more about it
  lc.listDevices();
  //lc.setup(1);
  if (lc.setup()) { // setup with automatic id finding
    
    // Global agency knob
    lc.knob(0, synthPtr->findParameterByNamePrefix("Synth Agency")->get().cast<float>());

    // Bind intent strengths to faders
    ofParameterGroup& intentParameters = synthPtr->getIntentParameterGroup();
    for (size_t i = 0; i < intentParameters.size(); ++i) {
      ofParameter<float>& layerParameter = intentParameters.getFloat(i);
      lc.fader(i, layerParameter);
    };

    // Bind knobs to audio analysis parameters
    lc.knob(4, synthPtr->findParameterByNamePrefix("MinPitch")->get().cast<float>());
    lc.knob(5, synthPtr->findParameterByNamePrefix("MaxPitch")->get().cast<float>());
    lc.knob(12, synthPtr->findParameterByNamePrefix("MinRms")->get().cast<float>());
    lc.knob(13, synthPtr->findParameterByNamePrefix("MaxRms")->get().cast<float>());
    lc.knob(6, synthPtr->findParameterByNamePrefix("MinComplexSpectralDifference")->get().cast<float>());
    lc.knob(7, synthPtr->findParameterByNamePrefix("MaxComplexSpectralDifference")->get().cast<float>());
    lc.knob(14, synthPtr->findParameterByNamePrefix("MinSpectralCrest")->get().cast<float>());
    lc.knob(15, synthPtr->findParameterByNamePrefix("MaxSpectralCrest")->get().cast<float>());
    lc.knob(22, synthPtr->findParameterByNamePrefix("MinZeroCrossingRate")->get().cast<float>());
    lc.knob(23, synthPtr->findParameterByNamePrefix("MaxZeroCrossingRate")->get().cast<float>());
  }
  // <<<
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
  synthPtr->shutdown();
  lc.close(); // close midi ports
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
  if (synthPtr->keyPressed(key)) return;
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
  
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

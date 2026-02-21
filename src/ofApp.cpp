#include "ofApp.h"

#include "ofxTimeMeasurements.h"

using namespace ofxMarkSynth;

void ofApp::setup(){
  ofDisableArbTex();
  glEnable(GL_PROGRAM_POINT_SIZE);
  ofSetBackgroundColor(0);
  TIME_SAMPLE_SET_DRAW_LOCATION(TIME_MEASUREMENTS_BOTTOM_LEFT);

  ResourceManager resources = loadSessionResourceManagerOrExit({
    .appNamespace = "fingerprint2",
    .dialogTitle = "Choose fingerprint2 session config (JSON)",
  });

  synthPtr = ofxMarkSynth::Synth::create("fingerprint2", ofxMarkSynth::ModConfig {
  }, resources);
  synthPtr->loadFirstPerformanceConfig();
  ofAddListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload); // before configureGui
  ofAddListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad); // before configureGui
  synthPtr->configureGui(guiWindowPtr);
}

void ofApp::onSynthWillUnload(ofxMarkSynth::Synth::ConfigUnloadEvent& e) {
  midiController.onSynthWillUnload();
  apcMiniController.onSynthWillUnload();
}

void ofApp::onSynthDidLoad(ofxMarkSynth::Synth::ConfigLoadedEvent& e) {
  midiController.onSynthDidLoad(synthPtr);
  apcMiniController.onSynthDidLoad(synthPtr);
}

//--------------------------------------------------------------
void ofApp::update(){
  synthPtr->update();
  midiController.update();
  apcMiniController.update();
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
  // Clear controller LEDs before tearing down the synth.
  midiController.exit();
  apcMiniController.exit();

  if (synthPtr) {
    ofRemoveListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload);
    ofRemoveListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad);
    synthPtr->shutdown();
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
  if (synthPtr) {
    synthPtr->windowResized(w, h);
  }
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
  
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
  
}

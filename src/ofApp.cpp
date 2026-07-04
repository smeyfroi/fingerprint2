#include "ofApp.h"

#include "ofxTimeMeasurements.h"

// Stage 17: the session loader moved out of the ofxMarkSynth umbrella into
// config/session/. Include it directly (previously pulled in via ofxMarkSynth.h).
#include "config/session/SessionResourceLoader.hpp"

using namespace ofxMarkSynth;

void ofApp::attachGuiWindowListeners() {
  if (!guiWindowPtr || guiWindowListenersAttached) {
    return;
  }

  ofAddListener(guiWindowPtr->events().draw, this, &ofApp::drawGui);
  ofAddListener(guiWindowPtr->events().keyPressed, this, &ofApp::keyPressedEvent);
  ofAddListener(guiWindowPtr->events().keyReleased, this, &ofApp::keyReleasedEvent);
  guiWindowListenersAttached = true;
}

void ofApp::detachGuiWindowListeners() {
  if (!guiWindowPtr || !guiWindowListenersAttached) {
    return;
  }

  ofRemoveListener(guiWindowPtr->events().draw, this, &ofApp::drawGui);
  ofRemoveListener(guiWindowPtr->events().keyPressed, this, &ofApp::keyPressedEvent);
  ofRemoveListener(guiWindowPtr->events().keyReleased, this, &ofApp::keyReleasedEvent);
  guiWindowListenersAttached = false;
}

void ofApp::keyPressedEvent(ofKeyEventArgs& e) {
  if (isShuttingDown || !synthPtr) {
    return;
  }

  keyPressed(e.key);
}

void ofApp::keyReleasedEvent(ofKeyEventArgs& e) {
  if (isShuttingDown || !synthPtr) {
    return;
  }

  keyReleased(e.key);
}

void ofApp::setup(){
  ofDisableArbTex();
  glEnable(GL_PROGRAM_POINT_SIZE);
  ofSetBackgroundColor(0);
  TIME_SAMPLE_SET_DRAW_LOCATION(TIME_MEASUREMENTS_BOTTOM_LEFT);

  ResourceManager resources = loadSessionResourceManagerOrExit({
    .appNamespace = "fingerprint2",
    .dialogTitle = "Choose fingerprint2 session config (JSON)",
    .forceChoose = forceChooseConfig,
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
  nanoKontrolController.onSynthWillUnload();
  oscController.onSynthWillUnload();
}

void ofApp::onSynthDidLoad(ofxMarkSynth::Synth::ConfigLoadedEvent& e) {
  midiController.onSynthDidLoad(synthPtr);
  apcMiniController.onSynthDidLoad(synthPtr);
  nanoKontrolController.onSynthDidLoad(synthPtr);
  oscController.onSynthDidLoad(synthPtr);
}

//--------------------------------------------------------------
void ofApp::update(){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->update();
  midiController.update();
  apcMiniController.update();
  nanoKontrolController.update();
  oscController.update();
}

//--------------------------------------------------------------
void ofApp::draw(){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->draw();
}

void ofApp::drawGui(ofEventArgs& args){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->drawGui();
}

//--------------------------------------------------------------
void ofApp::exit(){
  if (isShuttingDown) {
    return;
  }

  isShuttingDown = true;
  detachGuiWindowListeners();

  // Clear controller LEDs before tearing down the synth.
  midiController.exit();
  apcMiniController.exit();
  nanoKontrolController.exit();
  oscController.exit();

  if (synthPtr) {
    ofRemoveListener(synthPtr->configWillUnloadEvent, this, &ofApp::onSynthWillUnload);
    ofRemoveListener(synthPtr->configDidLoadEvent, this, &ofApp::onSynthDidLoad);
    synthPtr->shutdown();
    synthPtr.reset();
  }
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->keyPressed(key);
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
  if (!synthPtr || isShuttingDown) {
    return;
  }

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
  if (!synthPtr || isShuttingDown) {
    return;
  }

  synthPtr->windowResized(w, h);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){
  
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){
  
}

#include "ofxLaunchControlXL.h"

ofxLaunchControlXL::ofxLaunchControlXL(){
  
  name = "Launch Control XL";
  
  buttons.resize(16);
  
  buttons[0].controlNum = 24;
  buttons[1].controlNum = 25;
  buttons[2].controlNum = 26;
  buttons[3].controlNum = 27;
  
  buttons[4].controlNum = 28;
  buttons[5].controlNum = 29;
  buttons[6].controlNum = 30;
  buttons[7].controlNum = 31;
  
  buttons[8].controlNum = 45;
  buttons[9].controlNum = 46;
  buttons[10].controlNum = 47;
  buttons[11].controlNum = 48;
  
  buttons[12].controlNum = 49;
  buttons[13].controlNum = 50;
  buttons[14].controlNum = 51;
  buttons[15].controlNum = 52;
  
  
  knobsCC.resize(32);
  
  knobsCC[0]  = 13;
  knobsCC[1]  = 14;
  knobsCC[2]  = 15;
  knobsCC[3]  = 16;
  knobsCC[4]  = 17;
  knobsCC[5]  = 18;
  knobsCC[6]  = 19;
  knobsCC[7]  = 20;
  
  knobsCC[8]  = 21;
  knobsCC[9]  = 22;
  knobsCC[10] = 23;
  knobsCC[11] = 24;
  knobsCC[12] = 25;
  knobsCC[13] = 26;
  knobsCC[14] = 27;
  knobsCC[15] = 28;
  
  knobsCC[16] = 29;
  knobsCC[17] = 30;
  knobsCC[18] = 31;
  knobsCC[19] = 32;
  knobsCC[20] = 33;
  knobsCC[21] = 34;
  knobsCC[22] = 35;
  knobsCC[23] = 36;
  
  // faders
  knobsCC[24] = 5;
  knobsCC[25] = 6;
  knobsCC[26] = 7;
  knobsCC[27] = 8;
  knobsCC[28] = 9;
  knobsCC[29] = 10;
  knobsCC[30] = 11;
  knobsCC[31] = 12;
}

bool ofxLaunchControlXL::setup( int id, int channel ){
  const auto & ports = midiIn.getInPortList();
  string target = "LCXL3";
  for( size_t i=0; i<ports.size(); ++i){
    string cut = ports[i].substr(0, target.length());
    // Skip DAW ports - we want the main port for Custom Mode
    string portLower = ofToLower(ports[i]);
    if( target.compare(cut)==0 && portLower.find("daw") == string::npos ){
      id = i;
      break;
    }
  }
  
  if(id>=0){
    ofxControllerBase::setup( id, channel );
    dawModeActive = false;
    return true;
  }
  
  ofLogError()<<"ofxLaunchControls: automatic setup error, Launch Control XL not found";
  return false;
}

bool ofxLaunchControlXL::setupDawMode( int channel ){
  const auto & ports = midiIn.getInPortList();
  
  // Find the DAW input port
  int dawPortIndex = -1;
  for( size_t i=0; i<ports.size(); ++i){
    string portLower = ofToLower(ports[i]);
    bool isLCXL3 = (portLower.find("lcxl3") != string::npos) ||
                   (portLower.find("launch control xl") != string::npos);
    bool isDaw = portLower.find("daw") != string::npos;
    
    if( isLCXL3 && isDaw ){
      dawPortIndex = i;
      ofLogNotice("ofxLaunchControlXL") << "Found DAW input port: " << ports[i] << " at index " << i;
      break;
    }
  }
  
  if(dawPortIndex < 0){
    ofLogError("ofxLaunchControlXL") << "DAW input port not found";
    return false;
  }
  
  // Setup base controller with DAW port
  ofxControllerBase::setup( dawPortIndex, channel );
  
  // Setup LED controller (which also opens DAW output port and enables DAW mode)
  leds = std::make_unique<ofxLaunchControlXL3Leds>();
  if (!leds->setup(true)) {
    ofLogWarning("ofxLaunchControlXL") << "LED controller setup failed, but MIDI input may still work";
  }
  
  dawModeActive = true;
  ofLogNotice("ofxLaunchControlXL") << "DAW mode setup complete";
  return true;
}

void ofxLaunchControlXL::fader( int index, ofParameter<float> & param, float min, float max ){
  if(index>=0 && index<8 ){
    knob( index+24, param, min, max );
  }else{
    ofLogError()<<"ofxLaunchControls: wrong index in fader() function, binding ignored";
  }
}

void ofxLaunchControlXL::fader( int index, ofParameter<int> & param, int min, int max ){
  if(index>=0 && index<8 ){
    knob( index+24, param, min, max );
  }else{
    ofLogError()<<"ofxLaunchControls: wrong index in fader() function, binding ignored";
  }
}

void ofxLaunchControlXL::fader( int index, ofParameter<float> & param ){
  if(index>=0 && index<8 ){
    knob( index+24, param );
  }else{
    ofLogError()<<"ofxLaunchControls: wrong index in fader() function, binding ignored";
  }
}

void ofxLaunchControlXL::fader( int index, ofParameter<int> & param ){
  if(index>=0 && index<8 ){
    knob( index+24, param);
  }else{
    ofLogError()<<"ofxLaunchControls: wrong index in fader() function, binding ignored";
  }
}

void ofxLaunchControlXL::clearFaders(){
  for(int i = 0; i < 8; ++i){
    clearKnob(i + 24);
  }
}



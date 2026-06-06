#pragma once

#include <memory>

#include "ofxControllerBase.h"
#include "ofxLaunchControlXL3Leds.h"

class ofxLaunchControlXL : public ofxControllerBase {
public:
  ofxLaunchControlXL();
  
  /**
   * Setup the controller using Custom Mode (main MIDI port).
   * LED control is NOT available in this mode.
   */
  bool setup( int id=-1, int channel=9 );
  
  /**
   * Setup the controller using DAW Mode (DAW MIDI port).
   * This enables RGB LED control but uses DAW port for all MIDI I/O.
   * The CC/note mappings are the same as Custom Mode.
   */
  bool setupDawMode( int channel=1 );
  
  void fader( int index, ofParameter<float> & param, float min, float max );
  void fader( int index, ofParameter<int> & param, int min, int max );
  void fader( int index, ofParameter<float> & param );
  void fader( int index, ofParameter<int> & param );

  void clearFaders();

  /**
   * Get the LED controller (only available if setupDawMode() was used).
   */
  ofxLaunchControlXL3Leds* getLeds() { return leds.get(); }

  /**
   * Add an external MIDI listener to receive raw MIDI messages.
   * This allows external code to handle button presses, etc.
   */
  void addMidiListener(ofxMidiListener* listener) { midiIn.addListener(listener); }

  /**
   * Remove an external MIDI listener.
   */
  void removeMidiListener(ofxMidiListener* listener) { midiIn.removeListener(listener); }

private:
  std::unique_ptr<ofxLaunchControlXL3Leds> leds;
  bool dawModeActive = false;
};

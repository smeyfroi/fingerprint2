#pragma once

#include <string>

#include "ofxMidi.h"

// First-match substring scan of the available MIDI ports. Returns the port
// index whose name contains `pattern`, or -1 if none match.
inline int findMidiInPort(ofxMidiIn& in, const std::string& pattern) {
  for (int i = 0; i < in.getNumInPorts(); ++i)
    if (in.getInPortName(i).find(pattern) != std::string::npos) return i;
  return -1;
}

inline int findMidiOutPort(ofxMidiOut& out, const std::string& pattern) {
  for (int i = 0; i < out.getNumOutPorts(); ++i)
    if (out.getOutPortName(i).find(pattern) != std::string::npos) return i;
  return -1;
}

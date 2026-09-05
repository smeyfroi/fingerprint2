#pragma once

#include <cctype>
#include <initializer_list>
#include <string>
#include <vector>

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

namespace midiPortScanDetail {

inline std::string toLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

inline int findMatching(const std::vector<std::string>& names,
                        const std::string& required,
                        std::initializer_list<const char*> anyOf) {
  const std::string requiredLower = toLower(required);
  for (size_t i = 0; i < names.size(); ++i) {
    const std::string name = toLower(names[i]);
    if (name.find(requiredLower) == std::string::npos) continue;
    if (anyOf.size() == 0) return static_cast<int>(i);
    for (const char* alternative : anyOf) {
      if (name.find(toLower(alternative)) != std::string::npos) return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace midiPortScanDetail

// Case-insensitive first-match scan for devices that expose several ports under
// one family name — the LC XL3's MIDI/DAW pair, the APC Mini's Control/Notes
// pair — where one token isn't enough to pick the right one. Matches the first
// port whose name contains `required` AND at least one entry of `anyOf`; pass
// an empty `anyOf` to match on `required` alone. Returns -1 if none match.
inline int findMidiInPortMatching(ofxMidiIn& in, const std::string& required,
                                  std::initializer_list<const char*> anyOf = {}) {
  return midiPortScanDetail::findMatching(in.getInPortList(), required, anyOf);
}

inline int findMidiOutPortMatching(ofxMidiOut& out, const std::string& required,
                                   std::initializer_list<const char*> anyOf = {}) {
  return midiPortScanDetail::findMatching(out.getOutPortList(), required, anyOf);
}

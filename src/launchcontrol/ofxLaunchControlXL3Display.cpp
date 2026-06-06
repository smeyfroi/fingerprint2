#include "ofxLaunchControlXL3Display.h"

namespace {
// SysEx message constants for Launch Control XL 3
constexpr unsigned char kSysExStart = 0xF0;
constexpr unsigned char kSysExEnd = 0xF7;
constexpr unsigned char kNovationId0 = 0x00;
constexpr unsigned char kNovationId1 = 0x20;
constexpr unsigned char kNovationId2 = 0x29;
constexpr unsigned char kDeviceFamily = 0x02;
constexpr unsigned char kLaunchControlXL3Id = 0x15;
constexpr unsigned char kConfigureDisplayCmd = 0x04;
constexpr unsigned char kSetTextCmd = 0x06;
}  // namespace

void ofxLaunchControlXL3Display::setup(ofxMidiOut* midiOutPtr) {
  midiOut = midiOutPtr;
}

void ofxLaunchControlXL3Display::sendSysEx(const std::vector<unsigned char>& data) {
  if (!midiOut) return;

  std::vector<unsigned char> sysex;
  sysex.reserve(7 + data.size());
  sysex.push_back(kSysExStart);
  sysex.push_back(kNovationId0);
  sysex.push_back(kNovationId1);
  sysex.push_back(kNovationId2);
  sysex.push_back(kDeviceFamily);
  sysex.push_back(kLaunchControlXL3Id);
  sysex.insert(sysex.end(), data.begin(), data.end());
  sysex.push_back(kSysExEnd);

  midiOut->sendMidiBytes(sysex);
}

void ofxLaunchControlXL3Display::configure(uint8_t target, uint8_t config) {
  sendSysEx({kConfigureDisplayCmd, target, config});
}

void ofxLaunchControlXL3Display::setText(uint8_t target, uint8_t field, const std::string& text) {
  std::vector<unsigned char> data;
  data.push_back(kSetTextCmd);
  data.push_back(target);
  data.push_back(field);
  for (char c : text) {
    // Only send valid ASCII characters (0x20-0x7E)
    if (c >= 0x20 && c <= 0x7E) {
      data.push_back(static_cast<unsigned char>(c));
    }
  }
  sendSysEx(data);
}

void ofxLaunchControlXL3Display::setStationary(const std::string& line1, const std::string& line2) {
  // Configure 2-line arrangement
  configure(kTargetStationary, kArrangement2Line);
  // Set field 0 (Name/line1)
  setText(kTargetStationary, 0, line1);
  // Set field 1 (Value/line2)
  setText(kTargetStationary, 1, line2);
  // Trigger display
  configure(kTargetStationary, kArrangementTrigger);
}

void ofxLaunchControlXL3Display::setStationary3Line(const std::string& title, const std::string& name, const std::string& value) {
  // Configure 3-line arrangement (Title + Name + Value)
  configure(kTargetStationary, kArrangement3Line);
  // Set field 0 (Title)
  setText(kTargetStationary, 0, title);
  // Set field 1 (Name)
  setText(kTargetStationary, 1, name);
  // Set field 2 (Value)
  setText(kTargetStationary, 2, value);
  // Trigger display
  configure(kTargetStationary, kArrangementTrigger);
}

void ofxLaunchControlXL3Display::showTemporary(const std::string& name, const std::string& value) {
  // Configure 2-line arrangement
  configure(kTargetTemporary, kArrangement2Line);
  // Set field 0 (Name)
  setText(kTargetTemporary, 0, name);
  // Set field 1 (Value)
  setText(kTargetTemporary, 1, value);
  // Trigger display
  configure(kTargetTemporary, kArrangementTrigger);
}

void ofxLaunchControlXL3Display::clearStationary() {
  configure(kTargetStationary, kArrangementCancel);
}

void ofxLaunchControlXL3Display::clearTemporary() {
  configure(kTargetTemporary, kArrangementCancel);
}

void ofxLaunchControlXL3Display::cancelControlDisplay(uint8_t target) {
  // Configure with default arrangement but WITHOUT auto-display flags.
  // Using arrangement 4 (Name + Numeric Value) with bits 5-6 cleared
  // prevents the device from showing temporary overlays when controls are moved.
  configure(target, kArrangementNumeric);
}

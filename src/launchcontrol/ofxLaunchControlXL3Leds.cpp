#include "ofxLaunchControlXL3Leds.h"

#include "ofMain.h"

namespace {
// SysEx message constants for Launch Control XL 3
constexpr unsigned char kSysExStart = 0xF0;
constexpr unsigned char kSysExEnd = 0xF7;
constexpr unsigned char kNovationId0 = 0x00;
constexpr unsigned char kNovationId1 = 0x20;
constexpr unsigned char kNovationId2 = 0x29;
constexpr unsigned char kDeviceFamily = 0x02;
constexpr unsigned char kLaunchControlXL3Id = 0x15;
constexpr unsigned char kLedCommand = 0x01;
constexpr unsigned char kRgbSubcommand = 0x53;

// DAW mode enable/disable
constexpr int kDawModeChannel = 16;
constexpr int kDawModeNote = 12;
constexpr int kDawModeEnableVelocity = 127;
constexpr int kDawModeDisableVelocity = 0;

// Port name matching
const std::string kDawPortNameLower = "daw";
const std::string kLaunchControlXL3NameLower = "launch control xl";
const std::string kLCXL3ShortName = "lcxl3";
}  // namespace

ofxLaunchControlXL3Leds::ofxLaunchControlXL3Leds()
    : connected(false), dawModeEnabled(false) {
}

ofxLaunchControlXL3Leds::~ofxLaunchControlXL3Leds() {
  shutdown();
}

bool ofxLaunchControlXL3Leds::setup(bool enableDaw) {
  if (connected) {
    return true;
  }

  midiOut.listOutPorts();
  std::vector<std::string> ports = midiOut.getOutPortList();

  // Find the DAW port (e.g., "Launch Control XL 3 DAW Out" or contains "LCXL3" and "DAW")
  int dawPortIndex = -1;
  for (size_t i = 0; i < ports.size(); ++i) {
    std::string portLower = ofToLower(ports[i]);
    bool isLaunchControlXL3 = (portLower.find(kLaunchControlXL3NameLower) != std::string::npos) ||
                               (portLower.find(kLCXL3ShortName) != std::string::npos);
    bool isDawPort = portLower.find(kDawPortNameLower) != std::string::npos;

    if (isLaunchControlXL3 && isDawPort) {
      dawPortIndex = static_cast<int>(i);
      ofLogNotice("ofxLaunchControlXL3Leds") << "Found DAW port: " << ports[i] << " at index " << i;
      break;
    }
  }

  if (dawPortIndex < 0) {
    ofLogWarning("ofxLaunchControlXL3Leds") << "DAW port not found. LED control will be unavailable.";
    return false;
  }

  if (!midiOut.openPort(dawPortIndex)) {
    ofLogError("ofxLaunchControlXL3Leds") << "Failed to open DAW port at index " << dawPortIndex;
    return false;
  }

  // Brief delay after opening port before sending commands
  ofSleepMillis(100);

  connected = true;
  
  // Only enable DAW mode if requested. DAW mode overrides Custom Mode,
  // so if you want to keep Custom Mode active for knob/fader mappings,
  // set enableDaw to false. Note: LED commands may not work without DAW mode
  // on some firmware versions.
  if (enableDaw) {
    enableDawMode();
  }

  ofLogNotice("ofxLaunchControlXL3Leds") << "LED controller initialized successfully"
                                         << (enableDaw ? " (DAW mode)" : " (Custom mode preserved)");
  return true;
}

void ofxLaunchControlXL3Leds::shutdown() {
  if (!connected) {
    return;
  }

  disableDawMode();
  midiOut.closePort();
  connected = false;
  dawModeEnabled = false;

  ofLogNotice("ofxLaunchControlXL3Leds") << "LED controller shutdown";
}

void ofxLaunchControlXL3Leds::enableDawMode() {
  if (!connected || dawModeEnabled) {
    return;
  }

  midiOut.sendNoteOn(kDawModeChannel, kDawModeNote, kDawModeEnableVelocity);
  dawModeEnabled = true;
  ofLogNotice("ofxLaunchControlXL3Leds") << "DAW mode enabled";
}

void ofxLaunchControlXL3Leds::disableDawMode() {
  if (!connected || !dawModeEnabled) {
    return;
  }

  midiOut.sendNoteOn(kDawModeChannel, kDawModeNote, kDawModeDisableVelocity);
  dawModeEnabled = false;
  ofLogNotice("ofxLaunchControlXL3Leds") << "DAW mode disabled";
}

void ofxLaunchControlXL3Leds::setLED(int controlIndex, int r, int g, int b) {
  if (!connected) {
    return;
  }

  // Clamp to valid MIDI data range
  r = ofClamp(r, 0, 127);
  g = ofClamp(g, 0, 127);
  b = ofClamp(b, 0, 127);

  // Validate control index (5-52)
  if (controlIndex < 5 || controlIndex > 52) {
    ofLogWarning("ofxLaunchControlXL3Leds") << "Invalid control index: " << controlIndex
                                            << " (valid range: 5-52)";
    return;
  }

  std::vector<unsigned char> sysex;
  sysex.reserve(13);
  sysex.push_back(kSysExStart);
  sysex.push_back(kNovationId0);
  sysex.push_back(kNovationId1);
  sysex.push_back(kNovationId2);
  sysex.push_back(kDeviceFamily);
  sysex.push_back(kLaunchControlXL3Id);
  sysex.push_back(kLedCommand);
  sysex.push_back(kRgbSubcommand);
  sysex.push_back(static_cast<unsigned char>(controlIndex));
  sysex.push_back(static_cast<unsigned char>(r));
  sysex.push_back(static_cast<unsigned char>(g));
  sysex.push_back(static_cast<unsigned char>(b));
  sysex.push_back(kSysExEnd);

  midiOut.sendMidiBytes(sysex);
}

void ofxLaunchControlXL3Leds::setLED(int controlIndex, const Color& color) {
  setLED(controlIndex, color.r, color.g, color.b);
}

void ofxLaunchControlXL3Leds::setButtonLED(int buttonNum, int r, int g, int b) {
  if (buttonNum < 1 || buttonNum > 16) {
    ofLogWarning("ofxLaunchControlXL3Leds") << "Invalid button number: " << buttonNum
                                            << " (valid range: 1-16)";
    return;
  }
  // Button 1 = index 37, button 9 = index 45
  int controlIndex = 36 + buttonNum;
  setLED(controlIndex, r, g, b);
}

void ofxLaunchControlXL3Leds::setButtonLED(int buttonNum, const Color& color) {
  setButtonLED(buttonNum, color.r, color.g, color.b);
}

void ofxLaunchControlXL3Leds::setEncoderLED(int encoderNum, int r, int g, int b) {
  if (encoderNum < 1 || encoderNum > 24) {
    ofLogWarning("ofxLaunchControlXL3Leds") << "Invalid encoder number: " << encoderNum
                                            << " (valid range: 1-24)";
    return;
  }
  // Encoder 1 = index 13
  int controlIndex = 12 + encoderNum;
  setLED(controlIndex, r, g, b);
}

void ofxLaunchControlXL3Leds::setEncoderLED(int encoderNum, const Color& color) {
  setEncoderLED(encoderNum, color.r, color.g, color.b);
}

void ofxLaunchControlXL3Leds::setFaderLED(int faderNum, int r, int g, int b) {
  if (faderNum < 1 || faderNum > 8) {
    ofLogWarning("ofxLaunchControlXL3Leds") << "Invalid fader number: " << faderNum
                                            << " (valid range: 1-8)";
    return;
  }
  // Fader 1 = index 5
  int controlIndex = 4 + faderNum;
  setLED(controlIndex, r, g, b);
}

void ofxLaunchControlXL3Leds::setFaderLED(int faderNum, const Color& color) {
  setFaderLED(faderNum, color.r, color.g, color.b);
}

void ofxLaunchControlXL3Leds::setTopButtonRowLEDs(int r, int g, int b) {
  for (int i = 1; i <= 8; ++i) {
    setButtonLED(i, r, g, b);
  }
}

void ofxLaunchControlXL3Leds::setTopButtonRowLEDs(const Color& color) {
  setTopButtonRowLEDs(color.r, color.g, color.b);
}

void ofxLaunchControlXL3Leds::setBottomButtonRowLEDs(int r, int g, int b) {
  for (int i = 9; i <= 16; ++i) {
    setButtonLED(i, r, g, b);
  }
}

void ofxLaunchControlXL3Leds::setBottomButtonRowLEDs(const Color& color) {
  setBottomButtonRowLEDs(color.r, color.g, color.b);
}

void ofxLaunchControlXL3Leds::clearAllLEDs() {
  // Clear all controls (indices 5-52)
  for (int i = 5; i <= 52; ++i) {
    setLED(i, 0, 0, 0);
  }
}

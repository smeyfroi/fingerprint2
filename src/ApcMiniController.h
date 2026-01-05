#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ofxMidi.h"
#include "ofxMarkSynth.h"

// Forward declaration for Synth's config info
namespace ofxMarkSynth {
  class Synth;
}

/// Controller for Akai APC Mini MK2
///
/// Features:
/// - 8x7 RGB pad grid (rows 1-7) for config selection (hold-to-confirm)
/// - Bottom row of pad grid (row 0, notes 0-7) for layer pause toggle
/// - 8 faders for layer alpha control (with 5% pickup)
///
/// Note: Physical bottom buttons (notes 100-107) only support red LEDs,
/// so we use the bottom row of the RGB pad grid for layer control instead.
///
/// Works alongside the existing MidiController (Launch Control XL).
class ApcMiniController : public ofxMidiListener {
public:
  // === Device Identification ===
  // APC Mini MK2 has two ports: "Control" and "Notes"
  // We read input from the Control port.
  // For LED output, we open BOTH ports to be robust:
  // - Pads (0-63): RGB via SysEx (we broadcast to both outs)
  // - Buttons (100-119, 122): single-color LEDs via Note On (Control out)
  static constexpr const char* kInputPortPattern = "APC mini mk2 Control";
  static constexpr const char* kControlPortPattern = "APC mini mk2 Control";
  static constexpr const char* kNotesPortPattern = "APC mini mk2 Notes";

  // === Sysex Identifiers ===
  static constexpr uint8_t kManufacturerId = 0x47;  // Akai
  static constexpr uint8_t kDeviceId = 0x7F;
  static constexpr uint8_t kModelId = 0x4F;
  static constexpr uint8_t kSysexRgbMessageType = 0x24;

  // === Pad Grid (Notes 0-63) ===
  static constexpr int kPadNoteFirst = 0;
  static constexpr int kPadNoteLast = 63;
  static constexpr int kPadCount = 64;
  static constexpr int kGridWidth = 8;
  static constexpr int kGridHeight = 8;

  // === Side Buttons (Notes 112-119) - Inactive ===
  static constexpr int kSideButtonNoteFirst = 112;
  static constexpr int kSideButtonNoteLast = 119;
  static constexpr int kSideButtonCount = 8;

  // === Track Buttons (Notes 100-107) - RED LED ONLY ===
  // These physical buttons only support red LEDs (off/on/blink).
  // We still use them as alternate layer-toggle inputs, and as a
  // redundant status indicator (solid = active, blink = paused).
  static constexpr int kBottomButtonNoteFirst = 100;
  static constexpr int kBottomButtonNoteLast = 107;
  static constexpr int kBottomButtonCount = 8;

  // Bottom button note aliases
  static constexpr int kArrowLeftButtonNote = 106;   // Prev config (OF_KEY_LEFT)
  static constexpr int kArrowRightButtonNote = 107;  // Next config (OF_KEY_RIGHT)

  // Single-color LED velocity values
  static constexpr int kSingleLedOff = 0;
  static constexpr int kSingleLedOn = 1;
  static constexpr int kSingleLedBlink = 2;

  static constexpr int kShiftButtonNote = 122;

  // === Layer Toggle Pads (Bottom row of grid, notes 0-7) ===
  static constexpr int kLayerPadNoteFirst = 0;
  static constexpr int kLayerPadNoteLast = 7;
  static constexpr int kLayerPadCount = 8;
  
  // === Config Grid (Rows 1-7, notes 8-63) ===
  static constexpr int kConfigPadNoteFirst = 8;
  static constexpr int kConfigPadNoteLast = 63;
  static constexpr int kConfigPadCount = 56;  // 7 rows * 8 columns

  // === Faders (CC 48-56) ===
  static constexpr int kFaderCCFirst = 48;
  static constexpr int kFaderCCLast = 56;
  static constexpr int kFaderCount = 9;
  static constexpr int kLayerFaderCount = 8;  // Only first 8 used for layers

  // === Timing ===
  static constexpr uint64_t kHoldThresholdMs = 400;
  static constexpr float kPickupThreshold = 0.05f;  // 5% for soft-takeover

  // === LED Colors (RGB) ===
  struct RgbColor {
    uint8_t r, g, b;
    
    bool operator==(const RgbColor& other) const {
      return r == other.r && g == other.g && b == other.b;
    }
    bool operator!=(const RgbColor& other) const {
      return !(*this == other);
    }
  };

  static constexpr RgbColor kColorOff = {0, 0, 0};
  static constexpr RgbColor kColorDimGray = {30, 30, 30};
  static constexpr RgbColor kColorMediumGray = {80, 80, 80};
  static constexpr RgbColor kColorBrightWhite = {255, 255, 255};
  static constexpr RgbColor kColorAmber = {255, 140, 0};

  // Layer LED colors (use white to avoid confusion with config colors)
  static constexpr RgbColor kColorBrightLayer = {160, 160, 160};
  static constexpr RgbColor kColorDimLayer = {14, 14, 14};

  static constexpr RgbColor kColorDefaultConfig = {128, 128, 128};

  // === Config LED Brightness ===
  static constexpr float kConfigDimFactor = 0.09f;

  // === Config Grid Entry ===
  struct ConfigPadInfo {
    int configIndex = -1;             // Index into PerformanceNavigator::getConfigs()
    std::string configPath;           // Full path to the config JSON
    RgbColor color = kColorOff;       // Color from config JSON or default
    bool isAssigned = false;
  };

  ApcMiniController();
  ~ApcMiniController();

  // Lifecycle
  void update();
  void exit();

  // Synth connection
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();

  // ofxMidiListener
  void newMidiMessage(ofxMidiMessage& message) override;

  // State queries
  bool isConnected() const { return connected; }

private:
  // === MIDI I/O ===
  ofxMidiIn midiIn;
  ofxMidiOut midiOut;        // Notes port - for pad RGB
  ofxMidiOut midiOutControl; // Control port - for buttons (test)
  bool connected = false;

  // === Synth Reference ===
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;

  // === Pad Grid State ===
  std::array<ConfigPadInfo, kPadCount> padConfigMap;
  std::array<RgbColor, kPadCount> padCurrentColors;
  int currentConfigPadNote = -1;  // Which pad has the currently loaded config
  int lastKnownConfigIndex = -1;  // Tracks navigator changes (including non-APC switches)
  int lastKnownHibState = -1;     // Tracks hibernation state changes
  uint64_t lastLayerLedUpdateMs = 0;

  // === Hold State ===
  struct HoldState {
    bool active = false;
    int padNote = -1;
    uint64_t startTimeMs = 0;
  };
  HoldState currentHold;

  // === Fader Pickup State ===
  struct FaderState {
    float lastMidiValue = -1.0f;  // Last value from MIDI (normalized 0-1)
    float targetParamValue = 0.0f; // Current parameter value
    bool pickedUp = false;
  };
  std::array<FaderState, kLayerFaderCount> faderStates;

  // === Connection ===
  bool tryConnect();
  void disconnect();

  // === Message Handling ===
  void handleNoteOn(int note, int velocity);
  void handleNoteOff(int note);
  void handleCC(int cc, int value);

  // === Pad Grid ===
  void buildPadConfigMap();
  void updateAllPadLeds();
  void updatePadLed(int padNote);
  void onPadPressed(int padNote);
  void onPadReleased(int padNote);
  RgbColor getPadDisplayColor(int padNote) const;

  // Coordinate conversion (y=0 is top row, but MIDI note 0 is bottom-left)
  // So we invert Y: y=0 maps to row 7 (notes 56-63), y=7 maps to row 0 (notes 0-7)
  static int xyToPadNote(int x, int y) { return x + (kGridHeight - 1 - y) * kGridWidth; }
  static void padNoteToXY(int note, int& x, int& y) {
    x = note % kGridWidth;
    y = kGridHeight - 1 - (note / kGridWidth);
  }

  // === Layer Buttons ===
  void onLayerButtonPressed(int buttonIndex);
  void updateLayerButtonLed(int buttonIndex);
  void updateAllLayerButtonLeds();

  // === Faders ===
  void onFaderMoved(int faderIndex, float normalizedValue);
  void bindFadersToLayerAlphas();
  void resetFaderPickupStates();

  // === LED Control ===
  void clearAllLeds();
  void restorePersistentLeds();
  void setPadRgb(int padNote, const RgbColor& color);
  void setPadRgbBatch(const std::vector<std::pair<int, RgbColor>>& pads);
  void setBottomButtonLed(int buttonIndex, const RgbColor& color);
  void setPhysicalBottomButtonLed(int note, int velocity);
  void setSideButtonLed(int buttonIndex, const RgbColor& color);
  void dimInactiveControls();

  // === Sysex Helpers ===
  void sendSysex(uint8_t messageType, const std::vector<uint8_t>& data);
  static std::pair<uint8_t, uint8_t> toMsbLsb(uint8_t value);
  static RgbColor parseHexColor(const std::string& hex);
};

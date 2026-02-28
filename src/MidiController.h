#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ofxLaunchControllers.h"
#include "ofxMarkSynth.h"
#include "ofxMidi.h"

class MidiController : public ofxMidiListener {
 public:
  // Type alias for LED colors
  using LedColor = ofxLaunchControlXL3Leds::Color;

  // === LED Color Constants ===
  // Agency/Layer toggle and top row button colors
  static constexpr LedColor kAgencyModeColor = {127, 0, 0};     // Bright red
  static constexpr LedColor kLayerModeColor = {0, 127, 0};      // Bright green
  // Softer alternatives:
  // static constexpr LedColor kAgencyModeColor = {64, 0, 0};
  // static constexpr LedColor kLayerModeColor = {0, 64, 0};

  // Feedback and utility colors
  static constexpr LedColor kButtonPressedColor = {127, 127, 127}; // White
  static constexpr LedColor kOffColor = {0, 0, 0};

  // Intent indicator colors (top row)
  static constexpr LedColor kBrightIntentColor = {127, 127, 0};  // Bright yellow
  static constexpr LedColor kDimIntentColor = {8, 8, 0};        // Dim yellow

  // Encoder colors
  static constexpr LedColor kAgencyEncoderColor = {64, 0, 0};    // Red (encoder 0)
  static constexpr LedColor kBlueEncoderColor = {0, 0, 32};      // Blue (encoders 2, 10)
  static constexpr LedColor kCyanEncoderColor = {0, 32, 32};    // Cyan (encoders 3, 11)
  static constexpr LedColor kPurpleEncoderColor = {32, 0, 64};   // Purple (encoders 4, 12, 14)
  static constexpr LedColor kMagentaEncoderColor = {64, 0, 64}; // Magenta (encoders 5, 13, 15)

  // === Button CC Constants ===
  // Top row function buttons (CC 37-44 on channel 1) - used as intent indicator LEDs
  static constexpr int kFunctionButtonCCFirst = 37;
  static constexpr int kFunctionButtonCCLast = 44;

  // Bottom row buttons (CC 45-52 on channel 1) - Mod Snapshot buttons 1-8
  static constexpr int kBottomRowButtonCCFirst = 45;
  static constexpr int kBottomRowButtonCCLast = 52;

  // Hardware transport buttons
  static constexpr int kShiftButtonCC = 63;          // Shift (channel 7)
  static constexpr int kShiftButtonChannel = 7;      // Shift is on channel 7
  static constexpr int kPlayButtonCC = 116;          // Play (channel 1)
  static constexpr int kRecordTransportCC = 118;     // Record (channel 1)
  static constexpr int kTrackLeftCC = 103;           // Track Left (channel 1)
  static constexpr int kTrackRightCC = 102;          // Track Right (channel 1)

  // Temporary display duration (milliseconds)
  static constexpr uint64_t kTempDisplayDurationMs = 1000;
  static constexpr uint64_t kKnobTempDisplayDurationMs = 1200;
  static constexpr uint64_t kKnobStatusDisplayDurationMs = 3000;

  // Fader overlays can generate a lot of SysEx; rate-limit updates.
  static constexpr uint64_t kFaderTempDisplayMinIntervalMs = 50;

  // LaunchControl XL encoders (24 knobs) send CC 13-36 in DAW mode.
  static constexpr int kEncoderCcFirst = 13;
  static constexpr int kEncoderCcLast = 36;

  // Relative/nudge tuning clutch with hysteresis (larger band).
  static constexpr int kLowClutchEnterCc = 4;
  static constexpr int kLowClutchExitCc = 11;
  static constexpr int kHighClutchEnterCc = 123;
  static constexpr int kHighClutchExitCc = 116;

  static constexpr float kNudgeMaxMult = 12.0f;
  static constexpr float kNudgeSpeedSmoothing = 0.2f;
  static constexpr float kNudgeAccelGain = 0.04f; // multiplier per (CC/sec)
  static constexpr uint64_t kNudgeRearmGapMs = 500;
  static constexpr int kNudgeMaxDeltaCc = 20;

  MidiController();
  void update();
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();
  void exit();
  void onShiftModeChanged(bool& value);

  // External controllers (e.g. APC Mini) can request a layer overlay.
  // `pickedUp=false` shows "[PICKUP]" until engaged.
  void showLayerAlphaOverlay(int layerIndex, bool pickedUp);

  void newMidiMessage(ofxMidiMessage& message) override;

 private:
  void applyFaderBank();
  void setLayerAlphasFullyOn();
  void setupInitialLeds();
  void updateModeLeds();
  void updateIntentIndicatorLeds();
  void handleButtonCC(int channel, int cc, int value);

  void setButtonLedByCC(int cc, const LedColor& color);
  LedColor getButtonRestoreColor(int cc) const;
  void sendKeyPress(int key);
  void sendKeyRelease(int key);
  void updateStationaryDisplay();
  void showTempDisplay(const std::string& name, const std::string& value);
  void maybeShowFaderOverlay(int faderIndex, const std::string& name, float paramValue, bool pickupLikely, uint64_t nowMs);
  void disableControlAutoDisplays();

  std::unique_ptr<ofxLaunchControlXL> lc;
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;
  ofParameter<bool> shiftModeParameter { "Shift Mode", false };  // false = Snapshot mode, true = Layer mode
  std::unique_ptr<ofxLaunchControlXL3Display> display;
  bool lastRecordingState = false;  // For polling recording state changes
  bool lastSavingState = false;     // For polling save-in-progress state changes
  int lastDisplayedConfigTimeSeconds = -1;  // For polling config timer changes
  uint64_t tempDisplayDismissTimeMs = 0;  // When to dismiss temp display (0 = none pending)

  // Top row (buttons 1-8) intent indicator LED state cache
  std::array<LedColor, 8> lastIntentIndicatorColors {
    kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor
  };

  // LED state tracking for restore after momentary press feedback
  std::unordered_map<int, LedColor> buttonRestoreColors;

  // Track held keys for keyReleased calls (needed for arrow keys)
  std::unordered_set<int> heldKeys;

  enum class EncoderClutchMode {
    Active,
    ClutchLow,
    ClutchHigh,
  };

  struct EncoderNudgeState {
    int lastCcValue = -1;
    uint64_t lastEventTimeMs = 0;
    float smoothedSpeedCcs = 0.0f;
    EncoderClutchMode clutchMode = EncoderClutchMode::Active;
  };

  std::array<EncoderNudgeState, 24> encoderNudgeStates;

  struct FaderOverlayState {
    int lastCcValue = -1;
    float lastParamValue = -1.0f;
    uint64_t lastSendTimeMs = 0;
    std::string lastName;
    std::string lastValue;
  };

  std::array<FaderOverlayState, 8> faderOverlayStates;

  // Thread-safe ring buffer for button/CC events (MIDI thread → main thread)
  struct ButtonEvent {
    int channel;
    int cc;
    int value;
  };
  static constexpr size_t kButtonEventBufferSize = 64;
  std::array<ButtonEvent, kButtonEventBufferSize> buttonEventBuffer;
  std::atomic<int> buttonEventWriteIndex{0};
  int buttonEventReadIndex{0};
};

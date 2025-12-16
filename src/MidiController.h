#pragma once

#include <array>
#include <atomic>
#include <memory>
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

  // Bottom row keypress button colors
  static constexpr LedColor kPauseButtonColor = {64, 32, 0};      // Dim orange (CC 45, space)
  static constexpr LedColor kHibernateButtonColor = {127, 64, 0}; // Bright orange (CC 46, H)
  static constexpr LedColor kPrevConfigButtonColor = {64, 64, 0}; // Dim yellow (CC 48, left)
  static constexpr LedColor kNextConfigButtonColor = {127, 127, 0}; // Bright yellow (CC 49, right)
  static constexpr LedColor kRecordButtonColor = {64, 0, 64};     // Dim magenta (CC 51, R)
  static constexpr LedColor kSaveButtonColor = {127, 0, 127};     // Bright magenta (CC 52, S)

  // Feedback and utility colors
  static constexpr LedColor kButtonPressedColor = {127, 127, 127}; // White
  static constexpr LedColor kOffColor = {0, 0, 0};

  // Encoder colors
  static constexpr LedColor kAgencyEncoderColor = {127, 0, 0};    // Red (encoder 0)
  static constexpr LedColor kBlueEncoderColor = {0, 0, 127};      // Blue (encoders 2, 10)
  static constexpr LedColor kCyanEncoderColor = {0, 127, 127};    // Cyan (encoders 3, 11)
  static constexpr LedColor kPurpleEncoderColor = {64, 0, 127};   // Purple (encoders 4, 12, 14)
  static constexpr LedColor kMagentaEncoderColor = {127, 0, 127}; // Magenta (encoders 5, 13, 15)

  // === Button CC Constants ===
  // Top row function buttons
  static constexpr int kFunctionButtonCCFirst = 37;
  static constexpr int kFunctionButtonCCLast = 44;

  // Bottom row buttons
  static constexpr int kPauseButtonCC = 45;       // Space - pause/unpause
  static constexpr int kHibernateButtonCC = 46;   // H - hibernate
  static constexpr int kToggleButtonCC = 47;      // Agency/Layer toggle
  static constexpr int kPrevConfigButtonCC = 48;  // Left arrow - prev config
  static constexpr int kNextConfigButtonCC = 49;  // Right arrow - next config
  // CC 50 is unassigned
  static constexpr int kRecordButtonCC = 51;      // R - start/stop recording
  static constexpr int kSaveButtonCC = 52;        // S - save snapshot

  MidiController();
  void update();
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();
  void exit();
  void onAgencyLayerToggleChanged(bool& value);

  void newMidiMessage(ofxMidiMessage& message) override;

 private:
  void applyFaderBank();
  void setLayerAlphasFullyOn();
  void setupInitialLeds();
  void updateModeLeds();
  void handleButtonCC(int cc, int value);
  void setButtonLedByCC(int cc, const LedColor& color);
  LedColor getButtonRestoreColor(int cc) const;
  void sendKeyPress(int key);
  void sendKeyRelease(int key);

  std::unique_ptr<ofxLaunchControlXL> lc;
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;
  ofParameter<bool> agencyLayerToggleParameter { "Agency/Layer Toggle", false };

  // LED state tracking for restore after momentary press feedback
  std::unordered_map<int, LedColor> buttonRestoreColors;

  // Track held keys for keyReleased calls (needed for arrow keys)
  std::unordered_set<int> heldKeys;

  // Thread-safe ring buffer for button CC events (MIDI thread → main thread)
  struct ButtonEvent {
    int cc;
    int value;
  };
  static constexpr size_t kButtonEventBufferSize = 64;
  std::array<ButtonEvent, kButtonEventBufferSize> buttonEventBuffer;
  std::atomic<int> buttonEventWriteIndex{0};
  int buttonEventReadIndex{0};
};

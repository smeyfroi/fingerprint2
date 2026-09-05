#pragma once

#include <array>
#include <memory>
#include <string>

#include "MidiEventRing.h"
#include "ofxLaunchControlXL3Display.h"
#include "ofxLaunchControlXL3Leds.h"
#include "ofxMarkSynth.h"
#include "ofxMidi.h"

namespace ofxMarkSynth {
  class Synth;
}

/// Controller for the Novation Launch Control XL 3.
///
/// Faders → Intent poles + master strength (value-scaling takeover), encoders →
/// audio-analysis nudge, top-row LEDs → intent indicators. The bottom row is
/// dark and unassigned since snapshot recall retired. Transport lives on the
/// nanoKONTROL2 and layer control on the APC Mini, so nothing here handles them.
///
/// Requires DAW mode: the XL3 exposes a separate "DAW" port pair, and only that
/// pair carries the RGB-LED and OLED SysEx. Input is opened here; the matching
/// output is owned by ofxLaunchControlXL3Leds and shared with the OLED display.
///
/// Works alongside ApcMiniController and NanoKontrol2Controller.
class MidiController : public ofxMidiListener {
 public:
  // Type alias for LED colors
  using LedColor = ofxLaunchControlXL3Leds::Color;

  // === Device Identification ===
  // The XL3 enumerates a main MIDI port and a DAW port; we want the DAW one.
  // The family name is spelled differently across firmware/OS revisions, so
  // match "daw" plus either spelling (case-insensitive, see MidiPortScan.h).
  static constexpr const char* kDawPortPattern = "daw";
  static constexpr const char* kDeviceNameShort = "lcxl3";
  static constexpr const char* kDeviceNameLong = "launch control xl";

  // === LED Color Constants ===
  // Feedback and utility colors
  static constexpr LedColor kButtonPressedColor = {127, 127, 127}; // White (momentary press)
  static constexpr LedColor kOffColor = {0, 0, 0};

  // Intent indicator colors (top row): the 8 poles are 4 bipolar pairs and each
  // pair shares a hue — the SAME axis hues as the GUI's Intents panel (presence
  // coral, motion cyan, order violet, memory green), so hardware and screen read
  // as one vocabulary. Indexed by pole/2; dim = armed but master strength at 0.
  static constexpr std::array<LedColor, 4> kAxisBrightIntentColors {{
    {127, 60, 45},   // presence: coral
    {45, 100, 127},  // motion:   cyan
    {95, 65, 127},   // order:    violet
    {55, 110, 70},   // memory:   green
  }};
  static constexpr std::array<LedColor, 4> kAxisDimIntentColors {{
    {10, 5, 4},
    {4, 8, 10},
    {8, 5, 10},
    {4, 9, 6},
  }};

  // Encoder colors
  static constexpr LedColor kBlueEncoderColor = {0, 0, 32};      // Blue (encoders 2, 10)
  static constexpr LedColor kCyanEncoderColor = {0, 32, 32};    // Cyan (encoders 3, 11)
  static constexpr LedColor kPurpleEncoderColor = {32, 0, 64};   // Purple (encoders 4, 12, 14)
  static constexpr LedColor kMagentaEncoderColor = {64, 0, 64}; // Magenta (encoders 5, 13, 15)

  // === Button CC Constants ===
  // Top row function buttons (CC 37-44 on channel 1) - used as intent indicator LEDs
  static constexpr int kFunctionButtonCCFirst = 37;
  static constexpr int kFunctionButtonCCLast = 44;

  // ⛑ CC 45-52 (bottom row, buttons 9-16) WAS Mod Snapshot recall 1-8, retired 2026-09-03.
  // It was the only surface that addressed a snapshot slot by its position, and the engine's
  // 8-slot cap existed to match it. Snapshots are driven from the performance grid now.
  // The CCs are unassigned and free for a future job.

  // Transport/Shift buttons intentionally NOT handled by the Novation any more.
  // Play/Pause, Hibernate, Save Image and Prev/Next config all live on the
  // KORG NanoKontrol2 (and layer control on the APC Mini). The Novation is now
  // faders→Intent, encoders→audio nudge and top-row intent LEDs only.

  // Temporary display duration (milliseconds)
  static constexpr uint64_t kTempDisplayDurationMs = 1000;
  static constexpr uint64_t kKnobTempDisplayDurationMs = 1200;
  static constexpr uint64_t kKnobStatusDisplayDurationMs = 3000;

  // Fader overlays can generate a lot of SysEx; rate-limit updates.
  static constexpr uint64_t kFaderTempDisplayMinIntervalMs = 50;

  // === Faders (CC 5-12 on channel 1 in DAW mode) ===
  // Faders always bind to the Intent system: the poles in group order on
  // faders 1-7, master IntentStrength on fader 8 (the group's last index).
  // The old shift-mode layer-alpha bank was removed — layer alpha lives on the
  // APC Mini. Each fader picks its parameter up with the same value-scaling
  // takeover as the sibling surfaces (see FaderPickup.h).
  static constexpr int kFaderCcFirst = 5;
  static constexpr int kFaderCcLast = 12;
  static constexpr int kFaderCount = 8;

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
  ~MidiController();

  // Lifecycle
  void update();
  void exit();

  // Synth connection
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();

  // ofxMidiListener
  void newMidiMessage(ofxMidiMessage& message) override;

  bool isConnected() const { return connected; }

 private:
  // === Connection ===
  bool tryConnect();
  void disconnect();

  void handleFaderCC(int faderIndex, int value);
  void resetFaderPickupStates();
  void setupInitialLeds();
  void updateIntentIndicatorLeds();
  void handleButtonCC(int channel, int cc, int value);

  void setButtonLedByCC(int cc, const LedColor& color);
  void updateStationaryDisplay();
  void showTempDisplay(const std::string& name, const std::string& value);
  void maybeShowFaderOverlay(int faderIndex, const std::string& name, float paramValue, bool baselining, uint64_t nowMs);
  void disableControlAutoDisplays();

  // === MIDI I/O ===
  // Input port opened here; the DAW output port is owned by the LED controller
  // and shared with the OLED display (display->setup(leds->getMidiOut())).
  ofxMidiIn midiIn;
  bool connected = false;
  std::unique_ptr<ofxLaunchControlXL3Leds> leds;
  std::unique_ptr<ofxLaunchControlXL3Display> display;

  // === Synth Reference ===
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;

  bool lastRecordingState = false;  // For polling recording state changes
  bool lastSavingState = false;     // For polling save-in-progress state changes
  int lastDisplayedConfigTimeSeconds = -1;  // For polling config timer changes
  uint64_t tempDisplayDismissTimeMs = 0;  // When to dismiss temp display (0 = none pending)

  // Top row (buttons 1-8) intent indicator LED state cache
  std::array<LedColor, 8> lastIntentIndicatorColors {
    kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor, kOffColor
  };

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

  // === Fader takeover state (one per fader) ===
  // Holds the previous normalized MIDI value for value-scaling. -1 means
  // "no prior sample" — next message just baselines, no param movement.
  struct FaderState {
    float lastMidiValue = -1.0f;
  };
  std::array<FaderState, kFaderCount> faderStates;

  // OLED overlay de-duplication, separate from the takeover state above: the
  // display is the expensive side (SysEx per update), so only re-send when the
  // rendered content actually changes and the rate limit has elapsed.
  struct FaderOverlayState {
    uint64_t lastSendTimeMs = 0;
    std::string lastName;
    std::string lastValue;
  };

  std::array<FaderOverlayState, kFaderCount> faderOverlayStates;

  // Thread-safe ring buffer for button/CC events (MIDI thread → main thread).
  // Drop-on-full SPSC ring shared with the other MIDI surfaces (MS-069).
  struct ButtonEvent {
    int channel;
    int cc;
    int value;
  };
  MidiEventRing<ButtonEvent, 64> buttonEventRing;
};

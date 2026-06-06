#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>

#include "ofxMarkSynth.h"
#include "ofxMidi.h"

namespace ofxMarkSynth {
  class Synth;
}

/// Controller for Korg nanoKONTROL2.
///
/// Mirrors a subset of the APC Mini MK2 / LC XL3 functions onto a small,
/// portable controller. The device must be flashed to "External LED Mode"
/// (via Korg Kontrol Editor, one-time prep) for host-driven LED feedback to
/// work; sliders and buttons still work as inputs regardless.
///
/// Default-mode (Scene 1, factory) MIDI map, all on channel 1:
///   Sliders 1-8  : CC 0..7        (input only — layer alpha, with pickup)
///   S buttons    : CC 32..39      (unused — kept dark)
///   M buttons    : CC 48..55      (toggle layer pause; LED reflects pause)
///   R buttons    : CC 64..71      (LED only — lit when layer N exists)
///   Rewind       : CC 43          (prev config; LED always-on + flash)
///   FFwd         : CC 44          (next config; LED always-on + flash)
///   Stop         : CC 42          (hibernate; LED while HIBERNATED/FADING_OUT)
///   Play         : CC 41          (wake; LED while ACTIVE/FADING_IN)
///   Record       : CC 45          (saveImage; LED while save in progress)
///
/// LED output: send Control Change on channel 1 at the same CC number as the
/// input, value 127 (on) or 0 (off). Sends are de-duplicated against a cache.
///
/// Works alongside MidiController (LC XL3) and ApcMiniController.
class NanoKontrol2Controller : public ofxMidiListener {
public:
  // === Device Identification ===
  static constexpr const char* kPortPattern = "nanoKONTROL2";

  // === MIDI channel ===
  // ofxMidi uses 1-based channel numbers when sending.
  static constexpr int kMidiChannel = 1;

  // === Sliders (CC 0..7) ===
  static constexpr int kSliderCCFirst = 0;
  static constexpr int kSliderCCLast = 7;
  static constexpr int kSliderCount = 8;

  // === S buttons (CC 32..39) ===
  static constexpr int kSButtonCCFirst = 32;
  static constexpr int kSButtonCCLast = 39;
  static constexpr int kSButtonCount = 8;

  // === M buttons (CC 48..55) ===
  static constexpr int kMButtonCCFirst = 48;
  static constexpr int kMButtonCCLast = 55;
  static constexpr int kMButtonCount = 8;

  // === R buttons (CC 64..71) — factory-default Record-arm row ===
  // LED-only "layer N exists" indicator (moved here from the S buttons so the
  // light sits on the bottom button of each channel strip, next to the fader).
  static constexpr int kRButtonCCFirst = 64;
  static constexpr int kRButtonCCLast = 71;
  static constexpr int kRButtonCount = 8;

  // === Transport buttons ===
  static constexpr int kPlayButtonCC = 41;
  static constexpr int kStopButtonCC = 42;
  static constexpr int kRewindButtonCC = 43;
  static constexpr int kFFwdButtonCC = 44;
  static constexpr int kRecordButtonCC = 45;

  // === Timing ===
  static constexpr uint64_t kFlashDurationMs = 120;    // Rewind/FFwd press flash

  NanoKontrol2Controller();
  ~NanoKontrol2Controller();

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
  // === MIDI I/O ===
  ofxMidiIn midiIn;
  ofxMidiOut midiOut;
  bool connected = false;

  // === Synth Reference ===
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;

  // === Fader takeover state (one per slider) ===
  // Holds the previous normalized MIDI value for value-scaling. -1 means
  // "no prior sample" — next message just baselines, no param movement.
  struct FaderState {
    float lastMidiValue = -1.0f;
  };
  std::array<FaderState, kSliderCount> faderStates;

  // === LED cache: only send when desired state changes ===
  std::unordered_map<int, int> lastSentLedValue;  // cc → last sent value (0 or 127)

  // === Brief-flash expiry timestamps (Rewind/FFwd) ===
  uint64_t rewindFlashUntilMs = 0;
  uint64_t ffwdFlashUntilMs = 0;

  // === Thread-safe ring buffer (MIDI listener thread → main thread) ===
  struct CCEvent {
    int cc;
    int value;
  };
  static constexpr size_t kCCEventBufferSize = 64;
  std::array<CCEvent, kCCEventBufferSize> ccEventBuffer;
  std::atomic<int> ccEventWriteIndex { 0 };
  int ccEventReadIndex { 0 };

  // === Connection ===
  bool tryConnect();
  void disconnect();

  // === Main-thread event handling ===
  void drainCCEvents();
  void handleSliderCC(int cc, int value);
  void handleButtonCC(int cc, int value);

  // === LED management ===
  void pollAndUpdateLeds();
  void setLed(int cc, bool lit);
  void clearAllManagedLeds();

  // === Fader binding ===
  void resetFaderPickupStates();
};

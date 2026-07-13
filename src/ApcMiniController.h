#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
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
/// - 8x8 RGB pad grid (rows 0-7) for config selection (hold-to-confirm).
/// - Faders 1-3 drive the three top-of-sidebar synth-level parameters
///   (agency / AudioResp / VideoResp) with soft-takeover pickup.
///
/// Faders 4-9 are unused — layer alpha control, layer mute toggles, and
/// prev/next config moved to the nanoKONTROL2
/// (see docs/NanoKontrol2-Controller.md).
///
/// Works alongside MidiController (Launch Control XL) and
/// NanoKontrol2Controller.
class ApcMiniController : public ofxMidiListener {
public:
  // === Device Identification ===
  // APC Mini MK2 has two ports: "Control" and "Notes".
  // We prefer the Control port for input (buttons + faders).
  // For output, we open Notes and (if available) Control:
  // - Pad RGB: SysEx (sent via Control, Notes fallback)
  // - Buttons (100-119, 122): single-color LEDs via Note On (Control preferred)
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
  // Currently unused (we just clear them on connect so they stay dark).
  static constexpr int kBottomButtonNoteFirst = 100;
  static constexpr int kBottomButtonNoteLast = 107;
  static constexpr int kBottomButtonCount = 8;

  static constexpr int kShiftButtonNote = 122;

  // === Config Grid (Rows 0-7, notes 0-63) ===
  // All 8 physical rows are config-selection pads. Config grid row y=7 maps
  // to the bottom physical row (notes 0-7) via xyToPadNote. Layer alpha + mute
  // live on the nanoKONTROL2 (see docs/NanoKontrol2-Controller.md).
  static constexpr int kConfigPadNoteFirst = 0;
  static constexpr int kConfigPadNoteLast = 63;
  static constexpr int kConfigPadCount = 64;  // 8 rows * 8 columns

  // === Faders (CC 48..56) ===
  // Faders 1-3 (CC 48-50) drive synth-level parameters via soft-takeover
  // pickup. The remaining faders (51-56) are unbound — their CCs are
  // received and discarded.
  static constexpr int kFaderCCFirst = 48;
  static constexpr int kFaderCCLast = 56;
  static constexpr int kFaderCount = 9;
  static constexpr int kBoundFaderCount = 3;  // Faders 1-3 (CC 48-50)

  // Per-fader parameter binding. Each bound fader drives the named
  // ofParameter<float> resolved via Synth::findParameterByNamePrefix.
  // Only the first kBoundFaderCount entries are used; remaining faders
  // are silently dropped.
  static constexpr std::array<const char*, kBoundFaderCount> kFaderBindings = {
    "LiveAgency",  // Fader 1 (CC 48) — renamed 2026-07-11 (operator doctrine)
    "AudioResp",   // Fader 2 (CC 49) — "AudioGain" in the sidebar
    "VideoResp",   // Fader 3 (CC 50) — "MotionGain" in the sidebar
    // Fader 4 (CC 51) freed 2026-07-12: master IntentStrength returned to the
    // Novation's 8th fader when Ordered was dropped (7 poles on faders 1-7).
  };

  // === Timing ===
  static constexpr uint64_t kHoldThresholdMs = 400;

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

  static constexpr RgbColor kColorDefaultConfig = {128, 128, 128};

  // === Config LED Brightness ===
  static constexpr float kConfigDimFactor = 0.20f;

  // === Set-mode LED policy (SetController-driven pages) ===
  // A memoryDependent cell is READY once the MemoryBank has collected this many
  // textures; until then it renders as its own hue dimmed to kMemoryDimFactor.
  // No pulsing/animation on this surface (AKAI LED timing is fragile) — the dim
  // hue IS the "not ready" cue.
  static constexpr int kMemoryReadyThreshold = 3;
  static constexpr float kMemoryDimFactor = 0.25f;
  // Meta row (hardware row mapping to y=7): pads x=0..3 = pages, pad x=7 = HOME.
  static constexpr int kMetaRowY = 7;
  static constexpr int kMetaPageXFirst = 0;
  static constexpr int kMetaPageXLast = 3;
  static constexpr int kMetaHomeX = 7;

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
  // The APC Mini MK2 exposes separate MIDI ports for "Control" and "Notes".
  // We prefer Control for input, and fall back to Notes if needed.
  ofxMidiIn midiIn;
  ofxMidiOut midiOut;        // Notes output port
  ofxMidiOut midiOutControl; // Control output port
  bool connected = false;

  // === Synth Reference ===
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;

  // === Pad Grid State ===
  std::array<ConfigPadInfo, kPadCount> padConfigMap;
  std::array<RgbColor, kPadCount> padCurrentColors;
  std::array<uint64_t, kPadCount> padLedRetryUntilMs;
  uint64_t lastPadLedRetrySendMs = 0;
  int retryScanStart = 0;
  uint64_t pendingFullPadRepaintAtMs = 0;
  int currentConfigPadNote = -1;  // Which pad has the currently loaded config
  int lastKnownConfigIndex = -1;  // Tracks navigator changes (including non-APC switches)
  int lastKnownHibState = -1;     // Tracks hibernation state changes

  // === Set-mode state (active only while synth.getSetController().hasSet()) ===
  // Pad presses arrive on the MIDI thread; meta-row actions (page switch / HOME)
  // are recorded here and applied on the main thread in update() — the same
  // thread-handoff discipline the hold-to-commit config load already uses.
  std::atomic<int> pendingSetPageRequest { -1 };   // 0-based page to switch to, or -1
  std::atomic<bool> pendingSetHomeRequest { false };
  // A page change is the one permitted full-grid rewrite. onPageChanged (or the
  // defensive poll) raises this; update() consumes it for a single repaint pass.
  std::atomic<bool> pendingSetFullRepaint { false };
  int lastKnownSetPage = -1;         // Polled fallback if the callback slot is taken
  bool lastKnownMemoryReady = false; // Transition-gates the memory-dim repaint

  // === Hold State ===
  struct HoldState {
    bool active = false;
    int padNote = -1;
    uint64_t startTimeMs = 0;
  };
  HoldState currentHold;
  uint64_t lastHoldAmberSendMs = 0;

  // === LED Update Queue (MIDI thread → main thread) ===
  std::mutex ledQueueMutex;
  std::vector<int> pendingPadLedUpdates;

  void queuePadLedUpdate(int padNote);
  void flushQueuedLedUpdates();

  // === Fader takeover state (one per bound fader) ===
  // Holds the previous normalized MIDI value for value-scaling. -1 means
  // "no prior sample" — next message just baselines, no param movement.
  struct FaderState {
    float lastMidiValue = -1.0f;
  };
  std::array<FaderState, kBoundFaderCount> faderStates;

  void handleFaderCC(int faderIndex, int value);
  void resetFaderPickupStates();

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
  void processPadLedRetries();
  void onPadPressed(int padNote);
  void onPadReleased(int padNote);
  RgbColor getPadDisplayColor(int padNote) const;

  // === Set-mode pad grid (page-aware; falls back to buttonGrid when no set) ===
  void onSetPadPressed(int padNote);
  RgbColor getSetPadDisplayColor(int padNote) const;
  bool isMemoryReady() const;
  static RgbColor scaleRgb(const RgbColor& c, float factor);

  // Coordinate conversion (y=0 is top row, but MIDI note 0 is bottom-left)
  // So we invert Y: y=0 maps to row 7 (notes 56-63), y=7 maps to row 0 (notes 0-7)
  static int xyToPadNote(int x, int y) { return x + (kGridHeight - 1 - y) * kGridWidth; }
  static void padNoteToXY(int note, int& x, int& y) {
    x = note % kGridWidth;
    y = kGridHeight - 1 - (note / kGridWidth);
  }

  // === LED Control ===
  void clearAllLeds();
  void setPadRgb(int padNote, const RgbColor& color);
  void setPadRgbBatch(const std::vector<std::pair<int, RgbColor>>& pads);
  void setSideButtonLed(int buttonIndex, const RgbColor& color);
  void dimInactiveControls();

  // === Sysex Helpers ===
  void sendSysex(uint8_t messageType, const std::vector<uint8_t>& data);
  static std::pair<uint8_t, uint8_t> toMsbLsb(uint8_t value);
  static RgbColor parseHexColor(const std::string& hex);
};

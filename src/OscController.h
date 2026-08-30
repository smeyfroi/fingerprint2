#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ofParameter.h"
#include "ofEvent.h"
#include "ofxOsc.h"
#include "gui/panels/IntentSurfaceInfo.h"

#include "MemoryReadyPolicy.h"
// For kAudibleAlphaEpsilon: the iPad's "audible" boundary IS the Korg's.
#include "NanoKontrol2Controller.h"

namespace ofxMarkSynth {
  class Synth;
  class AgencyControllerMod;
}

/// Receives OSC from a TouchOSC iPad surface and drives MarkSynth parameters,
/// and echoes current state back so the surface stays in sync (e.g. when a new
/// config loads, or another controller / the GUI moves a parameter).
///
/// Surface <-> parameter map. All values are normalised 0..1 on the wire and
/// scaled to each parameter's [min,max] here, exactly like the MIDI controllers:
///   /layer/<i>/alpha    fader   -> layer i composite alpha
///   /layer/<i>/pause    toggle  -> layer i pause
///   /layer/<i>/name     (out)   -> layer i name (relabels the surface strip)
///   /layer/<i>/state    (out)   -> int 0..7, the strip's R/M/S lamps packed as
///                                   kStripExists|kStripParked|kStripAudible —
///                                   the same three facts the nanoKONTROL2 lights
///                                   (see the constants below). Delta-tracked and
///                                   pushed the instant it changes, unlike alpha
///                                   and pause; safe to push promptly because no
///                                   interactive widget binds it, so it can never
///                                   fight the performer's finger.
///   /master/alpha       fader   -> master composite alpha
///   /intent/<i>         fader   -> intent pole i (0..6: Dense Sparse Still Agitated
///                                   Persistent Ephemeral Chaotic)
///   /intent/strength    fader   -> master intent strength
///   /synth/agency       fader   -> "LiveAgency"
///   /synth/audiogain    fader   -> "AudioResp"
///   /synth/motiongain   fader   -> "VideoResp"
///   /agency/level       (out)   -> overall agency level (read-only meter)
///   /agency/<i>/budget  (out)   -> agency controller i budget (read-only meter)
///   /agency/<i>/armed   (out)   -> agency controller i armed (budget >= threshold)
///   /agency/<i>/name    (out)   -> agency controller i name
///   /agency/<i>/force   button  -> force-trigger agency controller i
///
/// Set-pages grid surface (tab 2; only live while the Synth has a set loaded):
///   /grid/press  i i    button  -> press the cell at (x, y=0..7) if assigned
///   /grid/page   i      button  -> switch to 1-based page i (clamped)
///   /grid/home   trig   button  -> load the set's designated home config
///   /grid/cells  (out)          -> ONE msg, 64 int32 (row-major y=0..7, x=0..7):
///                                   0xRRGGBB per assigned cell — full brightness
///                                   for the ACTIVE pad (last-landed press) while
///                                   its pose is intact, x0.55 rest tier otherwise
///                                   (x0.25 for memory-waiting config cells until
///                                   the bank fills) — 0 for unassigned / no set
///                                   (clears the surface)
///   /grid/page   (out)          -> 1-based current page (highlights the page btn)
///
/// The receiver is polled on the main thread from update(). ofxOscReceiver does
/// its own socket threading and getNextMessage() is synchronous, so unlike the
/// MIDI controllers no lock-free ring buffer is needed. The iPad's address is
/// learned from the first inbound packet, so there is no hard-coded client IP.
///
/// Works alongside MidiController (LC XL3), ApcMiniController and
/// NanoKontrol2Controller.
class OscController {
public:
  static constexpr int kReceivePort = 8000;  // iPad  -> here
  static constexpr int kSendPort    = 9000;  // here  -> iPad
  static constexpr int kSurfaceLayers = 7;   // layer strips on the surface (hardware-bounded)
  static constexpr int kAgencySlots   = 4;   // agency-controller slots on the surface
  static constexpr uint64_t kIndicatorIntervalMs = 200;   // 5 Hz read-only indicator stream
  static constexpr uint64_t kFullSyncIntervalMs  = 2000;  // re-push all control state every ~2 s
  static constexpr uint64_t kIdleGuardMs         = 1500;  // ...but only while the surface is quiet

  // === Per-strip R/M/S state (/layer/<i>/state) ===
  // The nanoKONTROL2's per-strip cue grammar, packed into one int so the iPad can
  // read all three lamps from a single message: R lit = the strip EXISTS, M lit =
  // the chain is PARKED, S lit = the chain is AUDIBLE. The values a surface
  // actually sees: 0 = no strip here, 1 = armed and waiting (running but silent —
  // the state the crossover gesture is aiming at), 5 = playing, 3 = parked,
  // 7 = parked with its picture still held on screen.
  //
  // Alpha and pause already travel separately, but only on the three
  // sendCurrentState() occasions — up to ~2 s stale, and suppressed entirely
  // while the performer is touching the surface — and the iPad would have to
  // derive the three states from two interactive widgets' values to read them.
  // This address carries the resolved answer instead, so the surface renders it
  // directly.
  static constexpr int kStripExists  = 1 << 0;   // R
  static constexpr int kStripParked  = 1 << 1;   // M
  static constexpr int kStripAudible = 1 << 2;   // S
  // Deliberately the Korg's own threshold, not a copy: the two surfaces must
  // agree on where audible begins, or the same strip would read differently on
  // the iPad and on the hardware.
  static constexpr float kAudibleAlphaEpsilon = NanoKontrol2Controller::kAudibleAlphaEpsilon;

  // Set-pages grid (tab 2). All 8 rows are cell rows (y=7 released 2026-07-31 —
  // paging moved to hardware/GUI pager buttons; this surface already had its own
  // explicit page/home buttons, which stay).
  static constexpr int kGridCols = 8;
  static constexpr int kGridRows = 8;
  static constexpr int kGridCellCount = kGridCols * kGridRows;  // 64
  // A memoryDependent cell is READY once the MemoryBank has collected this many
  // textures; until then its colour is dimmed (mirrors ApcMiniController's policy
  // so the iPad and the pads agree). No pulsing — the dim IS the "not ready" cue.
  static constexpr int kMemoryReadyThreshold = MemoryReadyPolicy::kReadyThreshold;
  static constexpr float kMemoryDimFactor = MemoryReadyPolicy::kDimFactor;
  // At-rest cells dim to the same tier as the APC pads (mirrors
  // ApcMiniController::kSetCellRestDimFactor, 2026-08-30) so the ACTIVE pad —
  // the last-landed press — reads as LIT at its full authored colour. This
  // surface receives colour only (no border/white layer; it never had a
  // loaded-config indicator), so full-vs-dim IS the whole played-pad affordance.
  static constexpr float kSetCellRestDimFactor = 0.55f;
  // Snapshot/scene cells scoped to a NOT-loaded family (Cell::config carries
  // the stem; the engine refuses the press) dim to the same foreign tier as
  // the APC pads (mirrors ApcMiniController::kForeignDimFactor, 2026-08-30) —
  // colour is this surface's only channel, so the dim IS the "enter that
  // family first" affordance. Config cells are exempt: they are the doors.
  static constexpr float kForeignDimFactor = 0.30f;

  OscController();
  ~OscController();

  // Lifecycle (fanned out from ofApp, mirroring the MIDI controllers).
  void update();
  void exit();
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();

  bool isListening() const { return listening; }

private:
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;
  // Measured intent surface for the active config — feeds the /intent/impacts
  // broadcast so the iPad can mirror the GUI's impact colouring.
  ofxMarkSynth::IntentSurfaceInfo surfaceInfo;

  ofxOscReceiver receiver;
  ofxOscSender   sender;
  bool listening   = false;
  bool senderReady = false;
  std::string remoteHost;   // learned from the first inbound packet

  // /intent/0../7 in on-screen order; resolved by name against the intent group.
  static const std::array<std::string, 7> kIntentNames;

  bool startReceiver();
  void pollIncoming();
  void handleMessage(const ofxOscMessage& m);

  // Target resolution (nullptr if absent in the current config).
  ofParameter<float>* layerAlphaParam(int i);
  ofParameter<float>* intentParam(int i);
  ofParameter<float>* intentStrengthParam();
  ofParameter<float>* synthParam(const std::string& namePrefix);

  void setNormalized(ofParameter<float>& p, float norm);
  void setLayerPause(int i, bool paused);

  // Outbound state echo (keeps the surface in sync).
  void ensureSender(const std::string& host);
  void sendCurrentState();
  // Slow "tempo sync": periodically re-push the full control state so GUI/MIDI
  // edits reach the surface, gated so it never fights an in-flight iPad drag.
  void maybePeriodicSync();
  void sendFloat(const std::string& addr, float value);
  void sendString(const std::string& addr, const std::string& value);
  void sendInt(const std::string& addr, int value);
  static float normOf(ofParameter<float>& p);

  // Per-strip R/M/S state tracker. Like maybeActiveCellResync, this polls each
  // update() and pushes only what changed — the ~2 s periodic re-push is far too
  // slow for a lamp, and pause/alpha changes made on the Korg or in the GUI reach
  // the iPad no other way while the performer is working the surface. `force`
  // re-sends every strip regardless (used by sendCurrentState, so a fresh config
  // or a newly-connected surface starts from a known state). -1 is the "nothing
  // sent yet" sentinel, which no real state can equal.
  void maybeStripStateResync(bool force = false);
  std::array<int, kSurfaceLayers> lastStripState_;   // filled with -1 by the ctor

  // Set-pages grid surface. sendGridState() pushes the two /grid outbound
  // messages (cells + current page); it is folded into sendCurrentState so the
  // grid stays in sync on every full-state push, re-fired on page change via
  // the RAII listener below, and re-fired promptly when the active pad moves
  // (maybeActiveCellResync). isMemoryReady() gates the memory-dependent dimming.
  void sendGridState();
  bool isMemoryReady() const;
  // Active-pad tracker: SetController has no activeCellChanged event, so
  // update() polls Synth::getActiveSetCell() / isActiveSetCellPoseIntact() and
  // re-pushes the grid the moment the last-landed cell moves or its scene pose
  // breaks — the ~2 s periodic resync would leave the played pad dim too long
  // for a live surface. page = -1 means nothing was active.
  void maybeActiveCellResync();
  int lastActiveCellPage_ = -1;
  int lastActiveCellX_ = -1;
  int lastActiveCellY_ = -1;
  bool lastActiveCellIntact_ = true;
  // RAII subscription to SetController::pageChanged (a MULTI-listener ofEvent):
  // re-registered on each config load, which replaces only our own slot. The
  // lambda re-sends the grid state; it checks synthPtr/senderReady so a fire
  // during an unloaded window is a safe no-op.
  ofEventListener pageChangedListener_;

  // Agency indicators (read-only meters streamed at 5 Hz) + Force triggers.
  std::vector<std::string> agencyModNames_;  // slot order (sorted), cached per config
  uint64_t lastStreamMs_   = 0;
  uint64_t lastFullSyncMs_ = 0;  // last periodic full-state re-push
  uint64_t lastControlInMs_ = 0; // last non-/sync inbound; gates the re-push
  void cacheAgencyMods();
  void streamIndicators();
  std::shared_ptr<ofxMarkSynth::AgencyControllerMod> agencyMod(int slot);
};

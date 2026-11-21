#include "OscController.h"

#include "subsystem/SynthSubsystems.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "ofMain.h"
#include "ofxMarkSynth.h"
#include "processMods/AgencyControllerMod.hpp"
#include "audio/IAudioAnalysisSource.hpp"
#include "audio/Looper.hpp"
#include "fingerprint/FingerprintPlayer.hpp"
#include "gui/surfaces/calibrate/SynthLive.hpp"

const std::array<std::string, 7> OscController::kIntentNames = {
  // The 7 poles (2026-07-12: Ordered dropped, Chaotic solo last), fader order. /intent/0../6.
  "Dense", "Sparse", "Still", "Agitated", "Persistent", "Ephemeral", "Chaotic"
};

OscController::OscController() {
  // -1 is the "nothing sent yet" sentinel: 0 is a real strip state ("no strip
  // here"), so a zero-initialised tracker would swallow the first push.
  lastStripState_.fill(-1);
}

OscController::~OscController() {
  exit();
}

void OscController::update() {
  if (!synthPtr) return;
  // Everything here runs on the main thread, so a slow frame here is a slow
  // frame on screen. Timed per phase so a stall can be pinned on inbound
  // handling (parameter listeners) or on the outbound pushes.
  using Clock = std::chrono::steady_clock;
  const auto msSince = [](Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
  };
  for (const auto& line : sender.takeLogLines()) {
    ofLogWarning("OscController") << line;
  }
  sendCount_ = 0;
  sendMs_ = 0.0;
  const auto t0 = Clock::now();
  const int inbound = pollIncoming();
  const double inMs = msSince(t0);
  const auto t1 = Clock::now();
  streamIndicators();
  maybeActiveCellResync();
  maybeStripStateResync();
  const double streamMs = msSince(t1);
  const auto t2 = Clock::now();
  const bool fullSync = maybePeriodicSync();
  const double syncMs = msSince(t2);
  const double totalMs = msSince(t0);
  if (totalMs > kSlowUpdateWarnMs) {
    ofLogWarning("OscController") << "Slow update " << totalMs << " ms: inbound "
        << inMs << " ms (" << inbound << " msgs), indicators " << streamMs
        << " ms, full sync " << syncMs << " ms" << (fullSync ? " (ran)" : "")
        << "; of which " << sendCount_ << " sends took " << sendMs_ << " ms to queue";
  }
}

void OscController::exit() {
  // ofxOscReceiver tears itself down on destruction. The send thread is joined
  // here so nothing is still sending while the app shuts down.
  listening = false;
  senderReady = false;
  sender.stop();
  synthPtr.reset();
}

void OscController::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synth) {
  if (!enabled) {
    // --no-osc: no socket is bound and no synth is held, so every other entry is a no-op.
    if (!disabledLogged) ofLogNotice("OscController") << "OSC is off for this launch (--no-osc)";
    disabledLogged = true;
    return;
  }
  synthPtr = synth;
  cacheAgencyMods();
  cacheInputs();
  // Re-subscribe the RAII page-change listener to THIS synth's SetController.
  // The Synth persists across config switches, so the multi-listener event
  // outlives a reload; re-assigning here replaces only our own slot. Any
  // consumer's page switch (APC meta row, GUI, this surface) re-pushes the grid
  // so the iPad follows. The lambda guards senderReady/synthPtr, so a fire
  // during an unloaded window is a safe no-op.
  pageChangedListener_ = synthPtr->getSetController().pageChanged.newListener(
      [this]() { if (senderReady) sendGridState(); });
  if (!listening) startReceiver();
  // Push the new config's values so an already-connected surface snaps to them.
  // (Master alpha is a Synth-lifetime member that PERSISTS across config loads —
  // nothing resets it — so the push keeps the surface honest about it too.)
  if (senderReady) sendCurrentState();
}

void OscController::onSynthWillUnload() {
  // Keep the socket bound across reloads; just drop the synth reference so we
  // never touch parameters mid-swap (mirrors the MIDI controllers).
  agencyModNames_.clear();
  synthPtr.reset();
}

bool OscController::startReceiver() {
  if (listening) return true;
  if (receiver.setup(receivePort_)) {
    listening = true;
    ofLogNotice("OscController") << "Listening for OSC on UDP " << receivePort_;
  } else {
    ofLogWarning("OscController") << "Could not bind OSC receive port "
                                  << receivePort_ << " (already in use?)";
  }
  return listening;
}

void OscController::ensureSender(const std::string& host) {
  // The send thread resolves and connects; anything that goes wrong there
  // comes back through takeLogLines() in update().
  sender.setTarget(host, sendPort_);
  senderReady = true;
  ofLogNotice("OscController") << "Echoing OSC state to " << host << ":" << sendPort_;
}

int OscController::pollIncoming() {
  int handled = 0;
  while (receiver.hasWaitingMessages()) {
    ofxOscMessage m;
    if (!receiver.getNextMessage(m)) break;

    // Learn the surface's address from any inbound traffic, so we can echo
    // state back without a hard-coded client IP. On FIRST contact with a new
    // host (which includes after an app restart, when remoteHost is cleared)
    // push current state once so the surface snaps to it.
    const std::string host = m.getRemoteHost();
    const bool newHost = (!host.empty() && host != remoteHost);
    if (newHost) {
      remoteHost = host;
      ensureSender(host);
      if (senderReady) sendCurrentState();
    }

    // /sync is the surface's keepalive/discovery heartbeat (sent on connect and
    // then every ~2s). Its only job is to arm the sender above; once the host
    // is known it is a no-op here, so it never re-pushes state and never fights
    // live edits. Real pushes happen on first contact (above) and on config
    // load (onSynthDidLoad -> sendCurrentState).
    if (m.getAddress() == "/sync") continue;

    // Real control traffic: remember when the surface was last touched so the
    // periodic sync can hold off while the performer is actively editing.
    lastControlInMs_ = ofGetElapsedTimeMillis();
    handleMessage(m);
    ++handled;
  }
  return handled;
}

bool OscController::maybePeriodicSync() {
  if (!synthPtr || !senderReady) return false;
  const uint64_t now = ofGetElapsedTimeMillis();
  if (now - lastFullSyncMs_ < kFullSyncIntervalMs) return false;
  // Hold off if the surface sent control traffic recently: re-pushing mid-drag
  // would echo a slightly-stale value back and fight the performer's finger.
  // The /sync heartbeat is excluded (it never stamps lastControlInMs_), so an
  // idle-but-connected surface still gets re-synced. This idle gate doubles as
  // echo-suppression, which is why no per-parameter guard is needed.
  if (now - lastControlInMs_ < kIdleGuardMs) return false;
  lastFullSyncMs_ = now;
  sendCurrentState();
  return true;
}

namespace {
  // Match "<prefix><digits><suffix>" and extract the integer index.
  bool matchIndexed(const std::string& addr, const std::string& prefix,
                    const std::string& suffix, int& outIdx) {
    if (addr.size() <= prefix.size() + suffix.size()) return false;
    if (addr.compare(0, prefix.size(), prefix) != 0) return false;
    if (addr.compare(addr.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
    const std::string mid =
        addr.substr(prefix.size(), addr.size() - prefix.size() - suffix.size());
    if (mid.empty()) return false;
    // Length cap before stoi: a hostile/buggy datagram like /layer/9999999999999/alpha
    // would otherwise throw std::out_of_range, uncaught, and kill the app mid-show.
    // No real strip index needs more than 3 digits.
    if (mid.size() > 3) return false;
    for (char c : mid) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    outIdx = std::stoi(mid);
    return true;
  }

  // The surface's labels are drawn by TouchOSC with no fallback font, and every
  // other string this app hands a UI is plain ASCII (the GUI font rule), so
  // fold here too: drop anything outside printable ASCII.
  std::string asciiOnly(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
      if (c >= 0x20 && c < 0x7f) out.push_back(static_cast<char>(c));
    }
    return out;
  }

  // Fit a name into at most two lines of `width` characters, breaking after a
  // space or a hyphen (group names are hyphenated: voice-2-fluid-group), and
  // mark anything that still does not fit with a trailing '.'. Labels cannot
  // measure text, so this is the only place a long name gets shaped.
  std::string wrapTwoLines(const std::string& raw, std::size_t width) {
    const std::string name = asciiOnly(raw);
    if (name.size() <= width) return name;
    std::size_t cut = std::string::npos;
    for (std::size_t i = 0; i < name.size() && i < width; ++i) {
      if (name[i] == ' ' || name[i] == '-') cut = i;
    }
    std::string first, rest;
    if (cut == std::string::npos) {
      first = name.substr(0, width);
      rest = name.substr(width);
    } else {
      first = name.substr(0, name[cut] == '-' ? cut + 1 : cut);
      rest = name.substr(cut + 1);
    }
    if (rest.size() > width) rest = rest.substr(0, width - 1) + ".";
    return first + "\n" + rest;
  }

  // Controller names are CamelCase with "Agency" somewhere in them (Agency1,
  // RoomAgencyOmni, PedalGlassAgencyOmni); the panel heading already says
  // AGENCY. Drop the word wherever it sits and space out what is left:
  // RoomAgencyOmni -> "Room Omni", TrioAgency1 -> "Trio 1". A bare number
  // keeps the word ("Agency 1"), since "1" alone says nothing.
  std::string agencyShortName(const std::string& name) {
    std::string s = name;
    for (const char* word : { "Agency", "agency" }) {
      for (auto at = s.find(word); at != std::string::npos; at = s.find(word)) {
        s.replace(at, 6, " ");
      }
    }
    std::string spaced;
    for (std::size_t i = 0; i < s.size(); ++i) {
      const char c = s[i];
      const char prev = i > 0 ? s[i - 1] : ' ';
      const bool boundary = (std::isupper(static_cast<unsigned char>(c)) &&
                             std::islower(static_cast<unsigned char>(prev))) ||
                            (std::isdigit(static_cast<unsigned char>(c)) &&
                             std::islower(static_cast<unsigned char>(prev)));
      if (boundary) spaced.push_back(' ');
      spaced.push_back(c == '-' || c == '_' ? ' ' : c);
    }
    std::string out;
    for (char c : spaced) {
      if (c == ' ' && (out.empty() || out.back() == ' ')) continue;
      out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) return name;
    const bool bareNumber = std::all_of(out.begin(), out.end(),
        [](unsigned char c) { return std::isdigit(c) || c == ' '; });
    return bareNumber ? "Agency " + out : out;
  }

  // A strip's name under the GROUPS heading: "-group" says nothing there and
  // costs the line its last word (voice-2-fluid-group).
  std::string stripName(const std::string& name) {
    constexpr std::string_view kSuffix = "-group";
    if (name.size() > kSuffix.size() && name.ends_with(kSuffix)) {
      return name.substr(0, name.size() - kSuffix.size());
    }
    return name;
  }

  // 0 NW, 1 NE, 2 SW, 3 SE: the 4x4 quarters of the 8x8 grid.
  int quadrantOf(int x, int y) {
    return (y < 4 ? 0 : 2) + (x < 4 ? 0 : 1);
  }

  // What a pad says on the surface: a scene's name, else the config cell's
  // headline, else its world, else its config; snapshots by slot (1-based,
  // as the GUI numbers them).
  std::string padName(const ofxMarkSynth::SetController::Cell& cell) {
    using Kind = ofxMarkSynth::SetController::CellKind;
    if (cell.kind == Kind::Scene) return cell.sceneName.empty() ? "Scene" : cell.sceneName;
    if (cell.kind == Kind::Snapshot) return "Snap " + ofToString(cell.snapshotSlot + 1);
    if (!cell.label.empty()) return cell.label;
    if (!cell.world.empty()) return cell.world;
    return cell.config;
  }
}  // namespace

void OscController::handleMessage(const ofxOscMessage& m) {
  if (!synthPtr) return;
  const std::string& addr = m.getAddress();

  // === Set-pages grid surface (tab 2) ===
  // Handled before the generic single-float path below because /grid/press and
  // /grid/release carry two ints and /grid/home may carry none. All of them are no-ops unless
  // a set is loaded — when hasSet() is false the pads/GUI/iPad keep today's
  // buttonGrid behaviour untouched.
  if (addr == "/grid/press" || addr == "/grid/release") {
    if (!synthPtr->getSetController().hasSet() || m.getNumArgs() < 2) return;
    const int x = m.getArgAsInt32(0);
    const int y = m.getArgAsInt32(1);
    // Hold to change pad, as on the APC (2026-10-18, owner: "let's make that hold to change pad
    // update everywhere including the OSC surface"): the pad sends /grid/press when the finger
    // lands and /grid/release when it lifts. The Synth applies the cell once held for
    // kSetCellHoldMs; a tap only shows its preview in the GUI. A layout without the release
    // message applies every tap after the hold time.
    if (addr == "/grid/press") {
      synthPtr->pressSetCell(x, y);
    } else {
      synthPtr->releaseSetCell(x, y);
    }
    return;
  }
  if (addr == "/grid/page") {
    if (!synthPtr->getSetController().hasSet() || m.getNumArgs() < 1) return;
    // Surface speaks 1-based pages; SetController is 0-based and clamps.
    synthPtr->getSetController().setCurrentPage(m.getArgAsInt32(0) - 1);
    return;
  }
  if (addr == "/grid/home") {
    if (!synthPtr->getSetController().hasSet()) return;
    // Trigger button (RISE-only on the surface): if it carries a value at all,
    // act on the press edge only.
    if (m.getNumArgs() >= 1 && m.getArgAsFloat(0) <= 0.5f) return;
    synthPtr->loadSetCellConfig(synthPtr->getSetController().homeConfig());
    return;
  }

  // Live Calibration's steps pressed from a surface, by name: "/calibrate/press room" starts the
  // room capture, "music" starts the music capture and stops it, "gains" and "save" follow
  // (Synth::pressCalibrationStep lists them). The GUI shows Live Calibration.
  if (addr == "/calibrate/press") {
    if (m.getNumArgs() >= 1 && m.getArgType(0) == OFXOSC_TYPE_STRING) {
      const std::string step = m.getArgAsString(0);
      if (!synthPtr->pressCalibrationStep(step)) {
        ofLogWarning("OscController") << "/calibrate/press: no step called '" << step << "'";
      }
    }
    return;
  }

  // A launch's grids (LaunchComposer.hpp): "/grid/previous" and "/grid/next" step through them,
  // "/grid/select <name>" plays one. A button's release (a value of 0) does nothing; none moves
  // while a take records.
  if (addr == "/grid/previous" || addr == "/grid/next" || addr == "/grid/select") {
    if (addr == "/grid/select") {
      if (m.getNumArgs() >= 1 && m.getArgType(0) == OFXOSC_TYPE_STRING) {
        const std::string grid = m.getArgAsString(0);
        if (!synthPtr->requestLaunchGrid(grid)) ofLogWarning("OscController") << "/grid/select: cannot play '" << grid << "' now";
      }
      return;
    }
    if (m.getNumArgs() >= 1 && m.getArgAsFloat(0) <= 0.5f) return;
    synthPtr->stepLaunchGrid(addr == "/grid/next" ? 1 : -1);
    return;
  }

  // The fingerprint desk: its buttons carry a constant int or nothing, its faders a float.
  if (addr.compare(0, 13, "/fingerprint/") == 0) {
    handleFingerprint(m);
    return;
  }

  if (m.getNumArgs() < 1) return;
  const float v = m.getArgAsFloat(0);  // surface sends all values as float 0..1
  int idx = 0;

  if (matchIndexed(addr, "/layer/", "/alpha", idx)) {
    if (auto* p = layerAlphaParam(idx)) setNormalized(*p, v);
  } else if (matchIndexed(addr, "/layer/", "/pause", idx)) {
    setLayerPause(idx, v > 0.5f);
  } else if (addr == "/master/alpha") {
    setNormalized(synthPtr->getRenderSubsystem().getMasterAlphaParameter(), v);
  } else if (addr == "/intent/strength") {
    if (auto* p = intentStrengthParam()) setNormalized(*p, v);
  } else if (matchIndexed(addr, "/intent/", "", idx)) {
    if (auto* p = intentParam(idx)) setNormalized(*p, v);
  } else if (addr == "/synth/agency") {
    // Param renamed LiveAgency (operator doctrine 2026-07-11); the OSC address stays
    // /synth/agency so the TouchOSC layout keeps working.
    if (auto* p = synthParam("LiveAgency")) setNormalized(*p, v);
  } else if (addr == "/synth/audiogain") {
    if (auto* p = synthParam("AudioResp")) setNormalized(*p, v);
  } else if (addr == "/synth/motiongain") {
    if (auto* p = synthParam("VideoResp")) setNormalized(*p, v);
  } else if (matchIndexed(addr, "/agency/", "/force", idx)) {
    if (v > 0.5f) {  // momentary press
      if (auto mod = agencyMod(idx)) mod->requestForceTrigger();
    }
  } else if (matchIndexed(addr, "/input/", "/gain", idx)) {
    setInputTrim(idx, (std::clamp(v, 0.0f, 1.0f) * 2.0f - 1.0f) * kInputTrimRangeDb);
  } else if (addr == "/loop/record") {
    if (v > 0.5f) {  // momentary press: LOOP / STOP LOOP / NEW LOOP
      if (auto* looper = synthPtr->getLooper()) looper->requestRecord();
    }
  } else if (addr == "/loop/stop") {
    if (v > 0.5f) {  // momentary press: STOP the loop, or PLAY the held one again
      if (auto* looper = synthPtr->getLooper()) looper->requestToggle();
    }
  } else if (addr == "/loop/level") {
    if (auto* looper = synthPtr->getLooper()) looper->setMonitorLevel(v);
  } else if (addr == "/take/mark") {
    if (v > 0.5f) synthPtr->markTake();  // momentary press: a marker chapter in the take
  } else if (matchIndexed(addr, "/input/", "/reset", idx)) {
    if (v > 0.5f && idx >= 0 && idx < static_cast<int>(inputIds_.size())) {
      setInputTrim(idx, inputBaselineDb_[inputIds_[idx]]);
      sendInputState();  // snap the fader back; the finger is off it
    }
  }
}

// The desk the PRINTS tab plays (ofxMarkSynth gui/panels/FingerprintDeskView.hpp is the same
// desk in the GUI): every move lands on the settings the engine holds for both.
void OscController::handleFingerprint(const ofxOscMessage& m) {
  auto* player = synthPtr->getFingerprintPlayer();
  if (!player) return;
  const std::string& addr = m.getAddress();
  const float v = m.getNumArgs() >= 1 ? m.getArgAsFloat(0) : 1.0f;   // a bare press counts as down
  auto& placement = player->deskPlacement();
  const float least = ofxMarkSynth::fingerprint::MIN_SCALE;
  int idx = 0;

  if (matchIndexed(addr, "/fingerprint/", "/fire", idx)) {
    if (v <= 0.5f) return;   // momentary press
    const auto prints = player->list();
    if (idx < 0 || idx >= static_cast<int>(prints.size())) return;
    synthPtr->fireFingerprintFromDesk(prints[static_cast<std::size_t>(idx)].name);
  } else if (addr == "/fingerprint/stop") {
    if (v > 0.5f) player->stopAll();
  } else if (addr == "/fingerprint/size") {
    placement.scale = least + std::clamp(v, 0.0f, 1.0f) * (1.0f - least);
  } else if (addr == "/fingerprint/opacity") {
    placement.opacity = std::clamp(v, 0.0f, 1.0f);
  } else if (addr == "/fingerprint/carrier") {
    if (m.getNumArgs() < 1) return;
    const int k = m.getArgAsInt32(0);
    const auto carriers = synthPtr->getFingerprintCarriers();
    if (k <= 0) {
      synthPtr->setFingerprintDeskCarrier({});
    } else if (k <= static_cast<int>(carriers.size())) {
      synthPtr->setFingerprintDeskCarrier(carriers[static_cast<std::size_t>(k - 1)].name);
    }
  } else if (addr == "/fingerprint/place") {
    if (m.getNumArgs() >= 2) {
      placement.x = std::clamp(m.getArgAsFloat(0), 0.0f, 1.0f);
      placement.y = std::clamp(m.getArgAsFloat(1), 0.0f, 1.0f);
    } else if (m.getNumArgs() == 1) {
      const int cell = m.getArgAsInt32(0);
      if (cell < 0 || cell >= kPlaceGrid * kPlaceGrid) {
        placement.x = placement.y = -1.0f;
      } else {
        placement.x = (static_cast<float>(cell % kPlaceGrid) + 0.5f) / kPlaceGrid;
        placement.y = (static_cast<float>(cell / kPlaceGrid) + 0.5f) / kPlaceGrid;
      }
    }
  } else if (addr == "/fingerprint/asdrawn") {
    if (v > 0.5f) placement.x = placement.y = -1.0f;
  }
}

ofParameter<float>* OscController::layerAlphaParam(int i) {
  // The strips ride GROUPS when the config authors a chains manifest (the strip
  // names pushed by sendCurrentState relabel the surface automatically, so the
  // same TouchOSC layout serves both worlds); manifest-less configs keep the
  // per-layer binding.
  auto& render = synthPtr->getRenderSubsystem();
  auto& alphas = render.hasChainManifest() ? render.getChainAlphaParameters()
                                           : render.getLayerAlphaParameters();
  if (i < 0 || i >= static_cast<int>(alphas.size())) return nullptr;
  return &alphas.getFloat(i);
}

ofParameter<float>* OscController::intentParam(int i) {
  if (i < 0 || i >= static_cast<int>(kIntentNames.size())) return nullptr;
  auto& g = synthPtr->getIntentParameterGroup();
  if (!g.contains(kIntentNames[i])) return nullptr;
  return &g.getFloat(kIntentNames[i]);
}

ofParameter<float>* OscController::intentStrengthParam() {
  auto& g = synthPtr->getIntentParameterGroup();
  if (!g.contains("IntentStrength")) return nullptr;
  return &g.getFloat("IntentStrength");
}

ofParameter<float>* OscController::synthParam(const std::string& namePrefix) {
  auto paramWrapper = synthPtr->findParameterByNamePrefix(namePrefix);
  if (paramWrapper == std::nullopt) return nullptr;
  return &paramWrapper->get().cast<float>();
}

void OscController::setNormalized(ofParameter<float>& p, float norm) {
  norm = std::clamp(norm, 0.0f, 1.0f);
  p.set(p.getMin() + norm * (p.getMax() - p.getMin()));
}

void OscController::setLayerPause(int i, bool paused) {
  auto& render = synthPtr->getRenderSubsystem();
  const bool groups = render.hasChainManifest();
  const auto& pausePtrs = groups ? render.getChainPauseParamPtrs()
                                 : render.getLayerPauseParamPtrs();
  if (i < 0 || i >= static_cast<int>(pausePtrs.size()) || !pausePtrs[i]) return;
  // toggle*Pause() flips; only flip when the desired state differs so an
  // absolute toggle value from the surface lands deterministically.
  if (pausePtrs[i]->get() != paused) {
    if (groups) render.toggleChainPause(i);
    else render.toggleLayerPause(i);
  }
}

float OscController::normOf(ofParameter<float>& p) {
  const float min = p.getMin();
  const float max = p.getMax();
  if (max == min) return 0.0f;
  return std::clamp((p.get() - min) / (max - min), 0.0f, 1.0f);
}

void OscController::send(const ofxOscMessage& m) {
  const auto t = std::chrono::steady_clock::now();
  sender.post(m);
  sendMs_ += std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - t).count();
  ++sendCount_;
}

void OscController::sendFloat(const std::string& addr, float value) {
  ofxOscMessage m;
  m.setAddress(addr);
  m.addFloatArg(value);
  send(m);
}

void OscController::sendString(const std::string& addr, const std::string& value) {
  ofxOscMessage m;
  m.setAddress(addr);
  m.addStringArg(value);
  send(m);
}

void OscController::sendInt(const std::string& addr, int value) {
  ofxOscMessage m;
  m.setAddress(addr);
  m.addInt32Arg(value);
  send(m);
}

void OscController::maybeStripStateResync(bool force) {
  if (!synthPtr || !senderReady) return;

  // Same chain-vs-layer binding as everything else on this surface, and the same
  // three facts NanoKontrol2Controller::pollAndUpdateLeds lights: the strip
  // EXISTS, it is PARKED, it is AUDIBLE. Reading them from the same parameters,
  // through the same epsilon, is what stops the iPad and the Korg disagreeing
  // about a strip.
  auto& render = synthPtr->getRenderSubsystem();
  const bool groups = render.hasChainManifest();
  auto& alphas = groups ? render.getChainAlphaParameters()
                        : render.getLayerAlphaParameters();
  const auto& pausePtrs = groups ? render.getChainPauseParamPtrs()
                                 : render.getLayerPauseParamPtrs();
  const int nStrips = static_cast<int>(alphas.size());

  for (int i = 0; i < kSurfaceLayers; ++i) {
    int state = 0;
    if (i < nStrips) {
      // Existence is the same test /layer/<i>/active already answers, so the two
      // addresses can never contradict each other on the same surface.
      state |= kStripExists;
      if (i < static_cast<int>(pausePtrs.size()) && pausePtrs[i] && pausePtrs[i]->get()) {
        state |= kStripParked;
      }
      if (alphas.getFloat(i).get() > kAudibleAlphaEpsilon) {
        state |= kStripAudible;
      }
    }
    // Only what moved. A lamp that never changes costs one comparison a frame.
    if (!force && state == lastStripState_[i]) continue;
    lastStripState_[i] = state;
    sendInt("/layer/" + ofToString(i) + "/state", state);
  }
}

void OscController::sendCurrentState() {
  if (!synthPtr || !senderReady) return;

  auto& render = synthPtr->getRenderSubsystem();
  // Groups when the config authors a chains manifest — the /name pushes relabel
  // the surface strips to the chain names (room / voice1 / ...).
  const bool groups = render.hasChainManifest();
  auto& alphas = groups ? render.getChainAlphaParameters()
                        : render.getLayerAlphaParameters();
  const auto& pausePtrs = groups ? render.getChainPauseParamPtrs()
                                 : render.getLayerPauseParamPtrs();

  // Send active + values for every strip the surface has (kSurfaceLayers), so a
  // config with fewer layers marks the surplus strips inactive — the surface
  // hides them — instead of leaving stale faders/labels behind. The heading is
  // GROUPS whatever the strips ride (owner 2026-10-26: "the iPad label should be
  // GROUPS"); a config without a manifest shows its layers as the groups.
  sendString("/mix/heading", "GROUPS");
  const int nLayers = static_cast<int>(alphas.size());
  for (int i = 0; i < kSurfaceLayers; ++i) {
    const bool active = (i < nLayers);
    sendFloat("/layer/" + ofToString(i) + "/active", active ? 1.0f : 0.0f);
    if (active) {
      ofParameter<float>& a = alphas.getFloat(i);
      sendFloat("/layer/" + ofToString(i) + "/alpha", normOf(a));
      sendString("/layer/" + ofToString(i) + "/name",
                 wrapTwoLines(stripName(a.getName()), kStripNameLineChars));
    }
  }
  for (int i = 0; i < static_cast<int>(pausePtrs.size()) && i < kSurfaceLayers; ++i) {
    if (pausePtrs[i]) {
      sendFloat("/layer/" + ofToString(i) + "/pause", pausePtrs[i]->get() ? 1.0f : 0.0f);
    }
  }

  // The R/M/S lamps for every strip, forced: a fresh config or a just-connected
  // surface must start from a known state rather than waiting for one to change.
  maybeStripStateResync(true);

  sendFloat("/master/alpha", normOf(render.getMasterAlphaParameter()));

  auto& g = synthPtr->getIntentParameterGroup();
  for (int i = 0; i < static_cast<int>(kIntentNames.size()); ++i) {
    if (g.contains(kIntentNames[i])) {
      sendFloat("/intent/" + ofToString(i), normOf(g.getFloat(kIntentNames[i])));
    }
  }
  if (g.contains("IntentStrength")) {
    sendFloat("/intent/strength", normOf(g.getFloat("IntentStrength")));
  }

  // Measured intent surface for the ACTIVE config: one message, 8 bucket ints
  // (-1 unmeasured, 0 below-noise, 1/2/3 moderate/solid/strong) in fader order.
  // The TouchOSC layout's root script recolours the pole faders from this so
  // the iPad mirrors the GUI's at-a-glance impact colouring.
  surfaceInfo.refreshIfChanged(synthPtr->getConfigSubsystem().currentConfigPath);
  ofxOscMessage impacts;
  impacts.setAddress("/intent/impacts");
  for (const auto& name : kIntentNames) {
    impacts.addInt32Arg(surfaceInfo.bucket(name));
  }
  send(impacts);

  if (auto* p = synthParam("LiveAgency")) sendFloat("/synth/agency", normOf(*p));
  if (auto* p = synthParam("AudioResp")) sendFloat("/synth/audiogain", normOf(*p));
  if (auto* p = synthParam("VideoResp")) sendFloat("/synth/motiongain", normOf(*p));

  // Agency controller slots: name + active (the live budget/armed values are
  // streamed separately at 5 Hz by streamIndicators()).
  for (int i = 0; i < kAgencySlots; ++i) {
    const bool active = (i < static_cast<int>(agencyModNames_.size()));
    sendFloat("/agency/" + ofToString(i) + "/active", active ? 1.0f : 0.0f);
    if (active) sendString("/agency/" + ofToString(i) + "/name",
                           agencyShortName(agencyModNames_[i]));
  }

  sendInputState();
  sendLoopState();
  sendFingerprintState();

  // SET tab: pads, quadrants, pages (or a clear when no set).
  sendGridState();
}

bool OscController::isMemoryReady() const {
  return synthPtr && MemoryReadyPolicy::isReady(*synthPtr);
}

void OscController::sendGridState() {
  if (!synthPtr || !senderReady) return;

  const auto& set = synthPtr->getSetController();
  ofxOscMessage cells, state, labels;
  cells.setAddress("/grid/cells");
  state.setAddress("/grid/state");
  labels.setAddress("/grid/labels");

  ofxOscMessage quadrants;
  quadrants.setAddress("/grid/quadrants");
  ofxOscMessage now;
  now.setAddress("/grid/now");
  ofxOscMessage pages;
  pages.setAddress("/grid/pages");

  if (!set.hasSet()) {
    // No set: clear a possibly-stale surface so the pads don't keep showing a
    // previous session's colours, names or quadrants.
    for (int i = 0; i < kGridCellCount; ++i) {
      cells.addInt32Arg(0);
      state.addInt32Arg(kPadEmpty);
      labels.addStringArg("");
    }
    quadrants.addInt32Arg(-1);
    for (int q = 0; q < 4; ++q) quadrants.addStringArg("");
    now.addStringArg("No set loaded");
    pages.addInt32Arg(0);
    pages.addInt32Arg(0);
    for (const auto* m : { &cells, &state, &labels, &quadrants, &now, &pages }) send(*m);
    return;
  }

  // Colours go out as authored; what each pad LOOKS like is the surface's call,
  // from the tier in /grid/state. The tiers keep the APC's facts -- the active
  // pad, a family that is not loaded (the engine refuses the press), a memory
  // config waiting for the bank -- and add the one a screen can show and LEDs
  // cannot: which 4x4 quadrant the performer is in.
  const bool memReady = isMemoryReady();
  const auto& active = synthPtr->getActiveSetCell();
  const bool activeIntact = synthPtr->isActiveSetCellPoseIntact();
  const int curPage = set.currentPage();
  const auto& cfgPath = synthPtr->getConfigSubsystem().getCurrentConfigPath();
  const std::string stem = cfgPath.empty() ? std::string{}
                                           : ofFilePath::getBaseName(cfgPath);
  const int curQuad = currentQuadrant();
  std::string activeName;
  for (int y = 0; y < kGridRows; ++y) {
    for (int x = 0; x < kGridCols; ++x) {
      const auto* cell = set.cellAt(x, y);
      if (!cell) {
        cells.addInt32Arg(0);
        state.addInt32Arg(kPadEmpty);
        labels.addStringArg("");
        continue;
      }
      const ofColor& c = cell->color;
      cells.addInt32Arg((static_cast<int32_t>(c.r) << 16) |
                        (static_cast<int32_t>(c.g) << 8) |
                         static_cast<int32_t>(c.b));
      const bool isConfigCell =
          (cell->kind == ofxMarkSynth::SetController::CellKind::Config);
      int tier = (curQuad < 0 || quadrantOf(x, y) == curQuad) ? kPadHere : kPadElsewhere;
      if ((!isConfigCell && !cell->config.empty() && cell->config != stem) ||
          (isConfigCell && cell->memoryDependent && !memReady)) {
        tier = kPadUnavailable;
      }
      if (active && active->page == curPage && active->x == x && active->y == y) {
        tier += activeIntact ? kPadActive : kPadActiveBroken;
        activeName = asciiOnly(padName(*cell));
      }
      state.addInt32Arg(tier);
      labels.addStringArg(wrapTwoLines(padName(*cell), kPadNameLineChars));
    }
  }

  quadrants.addInt32Arg(curQuad);
  for (int q = 0; q < 4; ++q) quadrants.addStringArg(asciiOnly(quadrantName(q)));

  // One line saying where the performer is: quadrant | pad | page.
  std::string line;
  const auto join = [&line](const std::string& part) {
    if (part.empty()) return;
    if (!line.empty()) line += "  |  ";
    line += part;
  };
  if (curQuad >= 0) join(asciiOnly(quadrantName(curQuad)));
  join(activeName);
  const std::string pageName = asciiOnly(set.pageName(curPage));
  join("page " + ofToString(curPage + 1) + (pageName.empty() ? "" : " " + pageName));
  now.addStringArg(line);

  const int nPages = std::min(set.pageCount(), kMaxSurfacePages);
  pages.addInt32Arg(nPages);
  pages.addInt32Arg(curPage + 1);  // 0-based here, 1-based on the wire
  for (int p = 0; p < nPages; ++p) pages.addStringArg(asciiOnly(set.pageName(p)));

  for (const auto* m : { &cells, &state, &labels, &quadrants, &now, &pages }) send(*m);
}

int OscController::currentQuadrant() const {
  if (!synthPtr) return -1;
  const auto& set = synthPtr->getSetController();
  if (!set.hasSet()) return -1;
  const int curPage = set.currentPage();
  if (const auto& active = synthPtr->getActiveSetCell(); active && active->page == curPage) {
    return quadrantOf(active->x, active->y);
  }
  // No pad played on this page yet (startup, a config loaded from the arrows):
  // the quadrant whose config is loaded, its home pad first.
  const auto& cfgPath = synthPtr->getConfigSubsystem().getCurrentConfigPath();
  if (cfgPath.empty()) return -1;
  const std::string stem = ofFilePath::getBaseName(cfgPath);
  int found = -1;
  for (const auto& cell : set.cellsForCurrentPage()) {
    if (cell.kind != ofxMarkSynth::SetController::CellKind::Config || cell.config != stem) continue;
    if (cell.home) return quadrantOf(cell.x, cell.y);
    if (found < 0) found = quadrantOf(cell.x, cell.y);
  }
  return found;
}

std::string OscController::quadrantName(int q) const {
  if (!synthPtr) return {};
  // A quadrant is named by its home pad -- the world's outer-corner safe config,
  // whose `world` is the audience name ("Minuet and trio") -- else by the
  // config most of its config pads load.
  std::map<std::string, int> configs;
  for (const auto& cell : synthPtr->getSetController().cellsForCurrentPage()) {
    if (quadrantOf(cell.x, cell.y) != q) continue;
    if (cell.home) {
      if (!cell.world.empty()) return cell.world;
      if (!cell.label.empty()) return cell.label;
      return cell.config;
    }
    if (cell.kind == ofxMarkSynth::SetController::CellKind::Config && !cell.config.empty()) {
      ++configs[cell.config];
    }
  }
  std::string best;
  int bestCount = 0;
  for (const auto& [config, n] : configs) {
    if (n > bestCount) { best = config; bestCount = n; }
  }
  return best;
}

void OscController::maybeActiveCellResync() {
  if (!synthPtr || !senderReady) return;
  // Compare the tracked {page,x,y,intact} tuple instead of repacking 64
  // colours per frame; on a change, re-push the grid the same way the
  // pageChanged listener does. A /grid/press from the surface itself lands
  // here too — the prompt brightening IS the press confirmation. No idle
  // guard: grid colours never fight a fader drag.
  const auto& active = synthPtr->getActiveSetCell();
  const int page = active ? active->page : -1;
  const int x = active ? active->x : -1;
  const int y = active ? active->y : -1;
  const bool intact = synthPtr->isActiveSetCellPoseIntact();
  if (page == lastActiveCellPage_ && x == lastActiveCellX_ &&
      y == lastActiveCellY_ && intact == lastActiveCellIntact_) {
    return;
  }
  lastActiveCellPage_ = page;
  lastActiveCellX_ = x;
  lastActiveCellY_ = y;
  lastActiveCellIntact_ = intact;
  sendGridState();
}

void OscController::cacheInputs() {
  inputIds_.clear();
  if (!synthPtr) return;
  for (const auto& [id, src] : synthPtr->getAudioAnalysisSources()) {
    if (!src) continue;
    // A lane that calibrates as another (the looper's) shares that lane's trim: one fader.
    if (!src->getChannelInfo().calibrateAs.empty()) continue;
    inputIds_.push_back(id);
    // First sight only: the rig persists across config loads, so a later load
    // must not adopt a live trim as the baseline.
    if (!inputBaselineDb_.contains(id)) inputBaselineDb_[id] = src->getChannelInfo().inputGainDb;
  }
  if (static_cast<int>(inputIds_.size()) > kInputSlots) {
    ofLogWarning("OscController") << inputIds_.size() << " audio sources, but the surface has "
                                  << kInputSlots << " trim slots; the rest are GUI-only";
  }
}

void OscController::setInputTrim(int slot, float db) {
  if (!synthPtr || slot < 0 || slot >= static_cast<int>(inputIds_.size())) return;
  if (auto src = synthPtr->getAudioAnalysisSource(inputIds_[slot])) src->setInputGainDb(db);
}

void OscController::sendInputState() {
  if (!synthPtr || !senderReady) return;
  for (int i = 0; i < kInputSlots; ++i) {
    const bool active = (i < static_cast<int>(inputIds_.size()));
    sendFloat("/input/" + ofToString(i) + "/active", active ? 1.0f : 0.0f);
    if (!active) continue;
    auto src = synthPtr->getAudioAnalysisSource(inputIds_[i]);
    if (!src) continue;
    const float db = src->getChannelInfo().inputGainDb;
    sendString("/input/" + ofToString(i) + "/name", asciiOnly(inputIds_[i]));
    sendFloat("/input/" + ofToString(i) + "/gain",
              std::clamp((db / kInputTrimRangeDb + 1.0f) * 0.5f, 0.0f, 1.0f));
    std::ostringstream text;
    text << std::showpos << std::fixed << std::setprecision(1) << db << " dB";
    sendString("/input/" + ofToString(i) + "/db", text.str());
  }
}

void OscController::streamInputLevels() {
  // Rides streamIndicators' 5 Hz tick. The post-trim analysis RMS, i.e. what
  // the mods actually hear, so a trim move shows on the meter at once.
  for (int i = 0; i < kInputSlots && i < static_cast<int>(inputIds_.size()); ++i) {
    auto src = synthPtr->getAudioAnalysisSource(inputIds_[i]);
    if (!src) continue;
    const float rms = src->getScalarValue(ofxAudioData::AnalysisScalar::rootMeanSquare);
    sendFloat("/input/" + ofToString(i) + "/level",
              std::clamp(rms / kInputMeterFullRms, 0.0f, 1.0f));
  }
}

void OscController::sendLoopState() {
  if (!synthPtr || !senderReady) return;
  const auto* looper = synthPtr->getLooper();
  sendFloat("/loop/active", looper ? 1.0f : 0.0f);
  if (looper) sendFloat("/loop/level", looper->getMonitorLevel());
  streamLoop();
}

void OscController::streamLoop() {
  // Rides streamIndicators' 5 Hz tick: the LOOP page's lamp, length and position bar.
  const auto* looper = synthPtr->getLooper();
  if (!looper) return;
  sendInt("/loop/state", static_cast<int>(looper->getState()));
  std::ostringstream text;
  text << std::fixed << std::setprecision(1) << looper->getLengthSec() << " s";
  sendString("/loop/length", text.str());
  sendFloat("/loop/pos", looper->getPosition01());
}

void OscController::sendFingerprintState() {
  if (!synthPtr || !senderReady) return;
  auto* player = synthPtr->getFingerprintPlayer();
  if (!player) return;
  const auto prints = player->list();
  for (int i = 0; i < kFingerprintSlots; ++i) {
    const std::string base = "/fingerprint/" + ofToString(i);
    const bool has = i < static_cast<int>(prints.size());
    sendFloat(base + "/active", has ? 1.0f : 0.0f);
    if (!has) continue;
    const auto& print = prints[static_cast<std::size_t>(i)];
    sendString(base + "/name", asciiOnly(print.name));
    std::ostringstream length;
    length << std::fixed << std::setprecision(1) << print.seconds << (print.carves ? " s erases" : " s");
    sendString(base + "/length", length.str());
  }
  const auto& placement = player->deskPlacement();
  const float least = ofxMarkSynth::fingerprint::MIN_SCALE;
  sendFloat("/fingerprint/size", std::clamp((placement.scale - least) / (1.0f - least), 0.0f, 1.0f));
  sendFloat("/fingerprint/opacity", std::clamp(placement.opacity, 0.0f, 1.0f));
  // A full push sends every lamp again, changed or not.
  lastCarriers_.clear();
  lastPlaceCell_ = -2;
  lastFingerprintStatus_.clear();
  lastPlaying_.fill(-1.0f);
  streamFingerprints();
}

void OscController::streamFingerprints() {
  // Rides streamIndicators' 5 Hz tick: the progress of what is playing, and each lamp when it changes.
  auto* player = synthPtr->getFingerprintPlayer();
  if (!player) return;
  const auto prints = player->list();
  const auto playing = player->nowPlaying();
  const int shown = std::min(static_cast<int>(prints.size()), kFingerprintSlots);
  for (int i = 0; i < shown; ++i) {
    // Fired twice, the bar follows the one fired last: the least far through.
    float progress = 0.0f;
    bool any = false;
    for (const auto& one : playing) {
      if (one.name != prints[static_cast<std::size_t>(i)].name) continue;
      progress = any ? std::min(progress, one.progress) : one.progress;
      any = true;
    }
    if (any) progress = std::max(progress, 0.02f);   // a bar that has just begun still shows
    if (progress == lastPlaying_[static_cast<std::size_t>(i)]) continue;
    lastPlaying_[static_cast<std::size_t>(i)] = progress;
    sendFloat("/fingerprint/" + ofToString(i) + "/playing", progress);
  }

  // The carrier row: AUTO and the drawn layers of the world now playing, the one chosen, and
  // whether ink laid on each is seen now.
  const auto carriers = synthPtr->getFingerprintCarriers();
  const std::string chosen = synthPtr->getFingerprintDeskCarrier();
  int chosenSlot = 0;
  std::array<std::string, kCarrierSlots> names;
  std::array<int, kCarrierSlots> states {};
  names[0] = "AUTO";
  states[0] = 1;
  for (int k = 1; k < kCarrierSlots && k <= static_cast<int>(carriers.size()); ++k) {
    const auto& carrier = carriers[static_cast<std::size_t>(k - 1)];
    names[static_cast<std::size_t>(k)] = wrapTwoLines(carrier.name, kStripNameLineChars);
    states[static_cast<std::size_t>(k)] = carrier.alpha >= ofxMarkSynth::Synth::FINGERPRINT_CARRIER_SHOWS ? 1 : 2;
    if (carrier.name == chosen) chosenSlot = k;
  }
  std::string key = ofToString(chosenSlot);
  for (int k = 0; k < kCarrierSlots; ++k) {
    key += "|" + names[static_cast<std::size_t>(k)] + ":" + ofToString(states[static_cast<std::size_t>(k)]);
  }
  if (key != lastCarriers_) {
    lastCarriers_ = key;
    ofxOscMessage message;
    message.setAddress("/fingerprint/carriers");
    message.addInt32Arg(chosenSlot);
    for (const auto& name : names) message.addStringArg(name);
    for (const int state : states) message.addInt32Arg(state);
    send(message);
  }

  // The place: the cell of the square nearest the place set, or -1 where it was drawn.
  const auto& placement = player->deskPlacement();
  int cell = -1;
  if (placement.x >= 0.0f && placement.y >= 0.0f) {
    const int column = std::clamp(static_cast<int>(placement.x * kPlaceGrid), 0, kPlaceGrid - 1);
    const int row = std::clamp(static_cast<int>(placement.y * kPlaceGrid), 0, kPlaceGrid - 1);
    cell = row * kPlaceGrid + column;
  }
  if (cell != lastPlaceCell_) {
    lastPlaceCell_ = cell;
    sendInt("/fingerprint/place", cell);
  }

  std::string status;
  if (prints.empty()) {
    status = "NO FINGERPRINTS IN THIS SESSION";
  } else if (!ofxMarkSynth::calibrate::synthIsLive(*synthPtr)) {
    status = "ASLEEP: A PRESS WAITS FOR THE WAKE";
  } else if (!playing.empty()) {
    status = ofToString(playing.size()) + " PLAYING";
  } else {
    status = ofToString(prints.size()) + " LOADED";
  }
  if (status != lastFingerprintStatus_) {
    lastFingerprintStatus_ = status;
    sendString("/fingerprint/status", status);
  }
}

void OscController::cacheAgencyMods() {
  agencyModNames_.clear();
  if (!synthPtr) return;
  for (const auto& [name, mod] : synthPtr->getMods()) {
    if (std::dynamic_pointer_cast<ofxMarkSynth::AgencyControllerMod>(mod)) {
      agencyModNames_.push_back(name);
    }
  }
  std::sort(agencyModNames_.begin(), agencyModNames_.end());  // stable slot order
}

std::shared_ptr<ofxMarkSynth::AgencyControllerMod> OscController::agencyMod(int slot) {
  if (!synthPtr || slot < 0 || slot >= static_cast<int>(agencyModNames_.size())) {
    return nullptr;
  }
  const auto& mods = synthPtr->getMods();
  auto it = mods.find(agencyModNames_[slot]);
  if (it == mods.end()) return nullptr;
  return std::dynamic_pointer_cast<ofxMarkSynth::AgencyControllerMod>(it->second);
}

void OscController::streamIndicators() {
  if (!synthPtr || !senderReady) return;
  const uint64_t now = ofGetElapsedTimeMillis();
  if (now - lastStreamMs_ < kIndicatorIntervalMs) return;
  lastStreamMs_ = now;

  sendFloat("/agency/level", std::clamp(synthPtr->getAgency(), 0.0f, 1.0f));

  for (int i = 0; i < kAgencySlots; ++i) {
    auto mod = agencyMod(i);
    if (!mod) continue;
    // Meter = RE-ARM: elapsed fraction of the budget-modulated cooldown, so "near
    // the top" = about to be able to fire and full = re-armed. A never-fired
    // controller (infinite sinceTrigger) reads full.
    const float cooldown = mod->getLastCooldownSecs();
    const float since = mod->getSecondsSinceTrigger();
    const float charge = (!std::isfinite(since) || cooldown <= 0.0f)
        ? 1.0f
        : std::clamp(since / cooldown, 0.0f, 1.0f);
    sendFloat("/agency/" + ofToString(i) + "/budget", charge);
    sendFloat("/agency/" + ofToString(i) + "/armed", (charge >= 1.0f) ? 1.0f : 0.0f);
  }
  streamInputLevels();
  streamLoop();
  streamFingerprints();
}

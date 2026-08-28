#include "OscController.h"

#include "subsystem/SynthSubsystems.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <optional>
#include <string>

#include "ofMain.h"
#include "ofxMarkSynth.h"
#include "processMods/AgencyControllerMod.hpp"

const std::array<std::string, 7> OscController::kIntentNames = {
  // The 7 poles (2026-07-12: Ordered dropped, Chaotic solo last), fader order. /intent/0../6.
  "Dense", "Sparse", "Still", "Agitated", "Persistent", "Ephemeral", "Chaotic"
};

OscController::OscController() = default;

OscController::~OscController() {
  exit();
}

void OscController::update() {
  if (!synthPtr) return;
  pollIncoming();
  streamIndicators();
  maybePeriodicSync();
}

void OscController::exit() {
  // ofxOscReceiver / ofxOscSender tear themselves down on destruction; here we
  // just stop touching the synth.
  listening = false;
  senderReady = false;
  synthPtr.reset();
}

void OscController::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synth) {
  synthPtr = synth;
  cacheAgencyMods();
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
  if (receiver.setup(kReceivePort)) {
    listening = true;
    ofLogNotice("OscController") << "Listening for OSC on UDP " << kReceivePort;
  } else {
    ofLogWarning("OscController") << "Could not bind OSC receive port "
                                  << kReceivePort << " (already in use?)";
  }
  return listening;
}

void OscController::ensureSender(const std::string& host) {
  if (sender.setup(host, kSendPort)) {
    senderReady = true;
    ofLogNotice("OscController") << "Echoing OSC state to " << host << ":" << kSendPort;
  } else {
    senderReady = false;
    ofLogWarning("OscController") << "Could not set up OSC sender to " << host;
  }
}

void OscController::pollIncoming() {
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
  }
}

void OscController::maybePeriodicSync() {
  if (!synthPtr || !senderReady) return;
  const uint64_t now = ofGetElapsedTimeMillis();
  if (now - lastFullSyncMs_ < kFullSyncIntervalMs) return;
  // Hold off if the surface sent control traffic recently: re-pushing mid-drag
  // would echo a slightly-stale value back and fight the performer's finger.
  // The /sync heartbeat is excluded (it never stamps lastControlInMs_), so an
  // idle-but-connected surface still gets re-synced. This idle gate doubles as
  // echo-suppression, which is why no per-parameter guard is needed.
  if (now - lastControlInMs_ < kIdleGuardMs) return;
  lastFullSyncMs_ = now;
  sendCurrentState();
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

  // Strip a leading "Agency"/"agency" from a controller name (the panel title
  // already says AGENCY) so the on-screen labels are short and unambiguous.
  std::string agencyShortName(const std::string& name) {
    std::string s = name;
    if (s.size() >= 6) {
      const std::string head = s.substr(0, 6);
      if (head == "Agency" || head == "agency") s.erase(0, 6);
    }
    while (!s.empty() && (s.front() == ' ' || s.front() == '-' || s.front() == '_')) {
      s.erase(0, 1);
    }
    return s.empty() ? name : s;
  }
}  // namespace

void OscController::handleMessage(const ofxOscMessage& m) {
  if (!synthPtr) return;
  const std::string& addr = m.getAddress();

  // === Set-pages grid surface (tab 2) ===
  // Handled before the generic single-float path below because /grid/press
  // carries two ints and /grid/home may carry none. All three are no-ops unless
  // a set is loaded — when hasSet() is false the pads/GUI/iPad keep today's
  // buttonGrid behaviour untouched.
  if (addr == "/grid/press") {
    if (!synthPtr->getSetController().hasSet() || m.getNumArgs() < 2) return;
    const int x = m.getArgAsInt32(0);
    const int y = m.getArgAsInt32(1);
    // A touchscreen tap is deliberate — no hold-to-confirm on this surface.
    if (const auto* cell = synthPtr->getSetController().cellAt(x, y)) {
      synthPtr->loadSetCellConfig(cell->config);
      ofLogNotice("OscController") << "Grid cell load (" << x << "," << y
                                   << "): " << cell->config;
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

void OscController::sendFloat(const std::string& addr, float value) {
  ofxOscMessage m;
  m.setAddress(addr);
  m.addFloatArg(value);
  sender.sendMessage(m, false);
}

void OscController::sendString(const std::string& addr, const std::string& value) {
  ofxOscMessage m;
  m.setAddress(addr);
  m.addStringArg(value);
  sender.sendMessage(m, false);
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
  // hides them — instead of leaving stale faders/labels behind.
  const int nLayers = static_cast<int>(alphas.size());
  for (int i = 0; i < kSurfaceLayers; ++i) {
    const bool active = (i < nLayers);
    sendFloat("/layer/" + ofToString(i) + "/active", active ? 1.0f : 0.0f);
    if (active) {
      ofParameter<float>& a = alphas.getFloat(i);
      sendFloat("/layer/" + ofToString(i) + "/alpha", normOf(a));
      sendString("/layer/" + ofToString(i) + "/name", a.getName());
    }
  }
  for (int i = 0; i < static_cast<int>(pausePtrs.size()) && i < kSurfaceLayers; ++i) {
    if (pausePtrs[i]) {
      sendFloat("/layer/" + ofToString(i) + "/pause", pausePtrs[i]->get() ? 1.0f : 0.0f);
    }
  }

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
  sender.sendMessage(impacts, false);

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

  // Set-pages grid tab: cell colours + current page (or a clear when no set).
  sendGridState();
}

bool OscController::isMemoryReady() const {
  return synthPtr && MemoryReadyPolicy::isReady(*synthPtr);
}

void OscController::sendGridState() {
  if (!synthPtr || !senderReady) return;

  const auto& set = synthPtr->getSetController();
  ofxOscMessage cells;
  cells.setAddress("/grid/cells");

  if (set.hasSet()) {
    const bool memReady = isMemoryReady();
    // ONE message, 64 int32 in row-major order (y=0..7, x=0..7): 0xRRGGBB per
    // assigned cell, 0 for an unassigned pad. memoryDependent cells are dimmed
    // to kMemoryDimFactor until the bank fills (same policy as the APC pads).
    for (int y = 0; y < kGridRows; ++y) {
      for (int x = 0; x < kGridCols; ++x) {
        int32_t packed = 0;
        if (const auto* cell = set.cellAt(x, y)) {
          ofColor c = cell->color;
          if (cell->memoryDependent && !memReady) {
            c.r = static_cast<unsigned char>(c.r * kMemoryDimFactor);
            c.g = static_cast<unsigned char>(c.g * kMemoryDimFactor);
            c.b = static_cast<unsigned char>(c.b * kMemoryDimFactor);
          }
          packed = (static_cast<int32_t>(c.r) << 16)
                 | (static_cast<int32_t>(c.g) << 8)
                 |  static_cast<int32_t>(c.b);
        }
        cells.addInt32Arg(packed);
      }
    }
    sender.sendMessage(cells, false);

    ofxOscMessage page;
    page.setAddress("/grid/page");
    page.addInt32Arg(set.currentPage() + 1);  // 0-based here, 1-based on the wire
    sender.sendMessage(page, false);
  } else {
    // No set: clear a possibly-stale surface with one all-zero message so the
    // tab-2 cells don't keep showing a previous session's colours.
    for (int i = 0; i < kGridCellCount; ++i) cells.addInt32Arg(0);
    sender.sendMessage(cells, false);
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
}

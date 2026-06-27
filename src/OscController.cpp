#include "OscController.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

#include "ofMain.h"
#include "ofxMarkSynth.h"

const std::array<std::string, 6> OscController::kIntentNames = {
  "Energy", "Density", "Structure", "Chaos", "Persistence", "Stillness"
};

OscController::OscController() = default;

OscController::~OscController() {
  exit();
}

void OscController::update() {
  if (!synthPtr) return;
  pollIncoming();
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
  if (!listening) startReceiver();
  // Push the new config's values so an already-connected surface snaps to them
  // (master alpha in particular is not serialised and resets to 1.0 each load).
  if (senderReady) sendCurrentState();
}

void OscController::onSynthWillUnload() {
  // Keep the socket bound across reloads; just drop the synth reference so we
  // never touch parameters mid-swap (mirrors the MIDI controllers).
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

    // Learn (or relearn) the surface's address from any inbound traffic, so we
    // can echo state back without a hard-coded client IP.
    const std::string host = m.getRemoteHost();
    const bool newHost = (!host.empty() && host != remoteHost);
    if (newHost) {
      remoteHost = host;
      ensureSender(host);
    }

    // Handshake: the surface sends /sync when it enters control mode (see its
    // root script), so we push current state immediately rather than waiting
    // for the first fader touch.
    if (m.getAddress() == "/sync") {
      if (senderReady) sendCurrentState();
      continue;
    }

    // First contact via an ordinary control message: sync the surface once too.
    if (newHost && senderReady) sendCurrentState();

    handleMessage(m);
  }
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
    for (char c : mid) {
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    outIdx = std::stoi(mid);
    return true;
  }
}  // namespace

void OscController::handleMessage(const ofxOscMessage& m) {
  if (!synthPtr || m.getNumArgs() < 1) return;
  const std::string& addr = m.getAddress();
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
    if (auto* p = synthParam("agency")) setNormalized(*p, v);
  } else if (addr == "/synth/audiogain") {
    if (auto* p = synthParam("AudioResp")) setNormalized(*p, v);
  } else if (addr == "/synth/motiongain") {
    if (auto* p = synthParam("VideoResp")) setNormalized(*p, v);
  }
}

ofParameter<float>* OscController::layerAlphaParam(int i) {
  auto& alphas = synthPtr->getRenderSubsystem().getLayerAlphaParameters();
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
  if (!g.contains("Intent Strength")) return nullptr;
  return &g.getFloat("Intent Strength");
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
  const auto& pausePtrs = render.getLayerPauseParamPtrs();
  if (i < 0 || i >= static_cast<int>(pausePtrs.size()) || !pausePtrs[i]) return;
  // toggleLayerPause() flips; only flip when the desired state differs so an
  // absolute toggle value from the surface lands deterministically.
  if (pausePtrs[i]->get() != paused) render.toggleLayerPause(i);
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
  auto& alphas = render.getLayerAlphaParameters();
  const auto& pausePtrs = render.getLayerPauseParamPtrs();

  const int nLayers = static_cast<int>(alphas.size());
  for (int i = 0; i < nLayers; ++i) {
    ofParameter<float>& a = alphas.getFloat(i);
    sendFloat("/layer/" + ofToString(i) + "/alpha", normOf(a));
    sendString("/layer/" + ofToString(i) + "/name", a.getName());
  }
  for (int i = 0; i < static_cast<int>(pausePtrs.size()); ++i) {
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
  if (g.contains("Intent Strength")) {
    sendFloat("/intent/strength", normOf(g.getFloat("Intent Strength")));
  }

  if (auto* p = synthParam("agency"))    sendFloat("/synth/agency", normOf(*p));
  if (auto* p = synthParam("AudioResp")) sendFloat("/synth/audiogain", normOf(*p));
  if (auto* p = synthParam("VideoResp")) sendFloat("/synth/motiongain", normOf(*p));
}

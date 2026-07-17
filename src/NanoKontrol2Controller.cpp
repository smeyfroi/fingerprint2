#include "NanoKontrol2Controller.h"

#include <cmath>

#include "FaderPickup.h"
#include "FaderTakeover.h"
#include "MidiPortScan.h"
#include "ofMain.h"
#include "controller/HibernationController.hpp"

NanoKontrol2Controller::NanoKontrol2Controller() {
  for (auto& fs : faderStates) {
    fs.lastMidiValue = -1.0f;
  }
}

NanoKontrol2Controller::~NanoKontrol2Controller() {
  exit();
}

bool NanoKontrol2Controller::tryConnect() {
  if (connected) return true;

  midiIn.listInPorts();
  midiOut.listOutPorts();

  int inPort = findMidiInPort(midiIn, kPortPattern);
  if (inPort >= 0)
    ofLogNotice("NanoKontrol2Controller") << "Found input port: " << midiIn.getInPortName(inPort);
  int outPort = findMidiOutPort(midiOut, kPortPattern);
  if (outPort >= 0)
    ofLogNotice("NanoKontrol2Controller") << "Found output port: " << midiOut.getOutPortName(outPort);

  if (inPort < 0 || outPort < 0) {
    ofLogNotice("NanoKontrol2Controller") << "nanoKONTROL2 not found";
    return false;
  }

  if (!midiIn.openPort(inPort)) {
    ofLogWarning("NanoKontrol2Controller") << "Failed to open input port";
    return false;
  }
  midiIn.addListener(this);

  if (!midiOut.openPort(outPort)) {
    ofLogWarning("NanoKontrol2Controller") << "Failed to open output port";
    midiIn.removeListener(this);
    midiIn.closePort();
    return false;
  }

  connected = true;
  ofLogNotice("NanoKontrol2Controller") << "Connected to nanoKONTROL2";

  // Visibly clear everything on connect so unused buttons aren't lit from
  // a prior session and our cache starts fresh.
  clearAllManagedLeds();

  return true;
}

void NanoKontrol2Controller::disconnect() {
  if (!connected) return;

  // Best-effort: turn off every LED we drive before closing.
  clearAllManagedLeds();

  midiIn.removeListener(this);
  midiIn.closePort();
  midiOut.closePort();
  connected = false;

  ofLogNotice("NanoKontrol2Controller") << "Disconnected from nanoKONTROL2";
}

void NanoKontrol2Controller::update() {
  if (!synthPtr) return;

  drainCCEvents();

  if (connected) {
    pollAndUpdateLeds();
  }
}

void NanoKontrol2Controller::exit() {
  disconnect();
  synthPtr.reset();
}

void NanoKontrol2Controller::onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synth) {
  this->synthPtr = synth;

  if (!connected) {
    tryConnect();
  }

  // Reset pickup state on every synth load so sliders must re-acquire.
  resetFaderPickupStates();

  if (!connected) {
    ofLogNotice("NanoKontrol2Controller") << "Synth loaded but nanoKONTROL2 not connected";
    return;
  }

  // Start from a known dark state and let the next pollAndUpdateLeds() repaint.
  clearAllManagedLeds();
  rewindFlashUntilMs = 0;
  ffwdFlashUntilMs = 0;
}

void NanoKontrol2Controller::onSynthWillUnload() {
  if (connected) {
    clearAllManagedLeds();
  }

  resetFaderPickupStates();
  synthPtr.reset();
  rewindFlashUntilMs = 0;
  ffwdFlashUntilMs = 0;
}

void NanoKontrol2Controller::newMidiMessage(ofxMidiMessage& message) {
  if (!connected) return;

  // Queue CC events for processing on the main thread. This avoids touching
  // ofParameter / OpenGL-adjacent state from the MIDI listener thread.
  if (message.status == MIDI_CONTROL_CHANGE) {
    ccEventRing.push({ message.control, message.value });
  }
}

void NanoKontrol2Controller::drainCCEvents() {
  ccEventRing.drain([this](const CCEvent& event) {
    if (event.cc >= kSliderCCFirst && event.cc <= kSliderCCLast) {
      handleSliderCC(event.cc, event.value);
    } else if (event.cc >= kKnobCCFirst && event.cc <= kKnobCCLast) {
      handleKnobCC(event.cc, event.value);
    } else {
      handleButtonCC(event.cc, event.value);
    }
  });
}

void NanoKontrol2Controller::handleSliderCC(int cc, int value) {
  if (!synthPtr) return;

  int faderIndex = cc - kSliderCCFirst;
  if (faderIndex < 0 || faderIndex >= kSliderCount) return;

  auto& render = synthPtr->getRenderSubsystem();

  // Rightmost fader drives the master composite alpha; the rest ride GROUPS
  // when the config authors a chains manifest (room / voice1 / ... — the same
  // strips the iPad shows), else per-layer alphas by index. All go through the
  // same FaderTakeover pickup below.
  ofParameter<float>* paramPtr = nullptr;
  if (faderIndex == kMasterAlphaFaderIndex) {
    paramPtr = &render.getMasterAlphaParameter();
  } else {
    ofParameterGroup& alphas = render.hasChainManifest()
        ? render.getChainAlphaParameters()
        : render.getLayerAlphaParameters();
    if (faderIndex >= static_cast<int>(alphas.size())) return;
    paramPtr = &alphas.getFloat(faderIndex);
  }

  applyPickup(*paramPtr, value, faderStates[faderIndex].lastMidiValue);
}

void NanoKontrol2Controller::handleKnobCC(int cc, int value) {
  if (!synthPtr) return;
  // Only the master-column knob is mapped; the other seven are ignored for now.
  if (cc != kPreviewGainKnobCC) return;

  // Drive the GUI's texture-preview gain through the same value-scaling takeover
  // as the faders, so the knob picks the parameter up smoothly rather than
  // jumping it on first touch.
  applyPickup(synthPtr->getPreviewGainParameter(), value, previewGainKnobState.lastMidiValue);
}

void NanoKontrol2Controller::handleButtonCC(int cc, int value) {
  if (!synthPtr) return;

  // S buttons (CC 32..39): unused — no press action.
  if (cc >= kSButtonCCFirst && cc <= kSButtonCCLast) {
    return;
  }

  // R buttons (CC 64..71): LED-only "layer exists" indicator, no press action.
  if (cc >= kRButtonCCFirst && cc <= kRButtonCCLast) {
    return;
  }

  // Press-only: ignore release (value == 0).
  if (value == 0) return;

  // M buttons (CC 48..55): toggle pause for the corresponding strip — a GROUP
  // when the config authors a chains manifest, else the layer.
  if (cc >= kMButtonCCFirst && cc <= kMButtonCCLast) {
    int idx = cc - kMButtonCCFirst;
    auto& render = synthPtr->getRenderSubsystem();
    const bool groups = render.hasChainManifest();
    const auto& pauseParamPtrs = groups ? render.getChainPauseParamPtrs()
                                        : render.getLayerPauseParamPtrs();
    if (idx < static_cast<int>(pauseParamPtrs.size()) && pauseParamPtrs[idx]) {
      if (groups) render.toggleChainPause(idx);
      else render.toggleLayerPause(idx);
    }
    return;
  }

  // Transport row.
  uint64_t nowMs = ofGetElapsedTimeMillis();
  switch (cc) {
    case kPlayButtonCC:
      synthPtr->keyPressed(OF_KEY_SPACE);
      break;
    case kStopButtonCC:
      synthPtr->keyPressed('H');
      break;
    case kRewindButtonCC:
      synthPtr->keyPressed(OF_KEY_LEFT);
      rewindFlashUntilMs = nowMs + kFlashDurationMs;
      break;
    case kFFwdButtonCC:
      synthPtr->keyPressed(OF_KEY_RIGHT);
      ffwdFlashUntilMs = nowMs + kFlashDurationMs;
      break;
    case kRecordButtonCC:
      synthPtr->saveImage();
      break;
    default:
      break;
  }
}

void NanoKontrol2Controller::pollAndUpdateLeds() {
  if (!connected || !synthPtr) return;

  // Strip LEDs track whatever the strips are bound to: groups when the config
  // authors a chains manifest, layers otherwise.
  auto& render = synthPtr->getRenderSubsystem();
  const auto& pauseParamPtrs = render.hasChainManifest()
      ? render.getChainPauseParamPtrs()
      : render.getLayerPauseParamPtrs();

  // R buttons (CC 64..71): lit iff layer exists in current config.
  // (Moved here from the S buttons — bottom-of-strip indicator next to fader.)
  // The rightmost button belongs to the master alpha fader and is always lit,
  // matching the "active strip" cue the layer faders get.
  for (int i = 0; i < kRButtonCount; ++i) {
    bool lit = (i == kMasterAlphaFaderIndex)
               ? true
               : (i < static_cast<int>(pauseParamPtrs.size()) && pauseParamPtrs[i] != nullptr);
    setLed(kRButtonCCFirst + i, lit);
  }

  // S buttons (CC 32..39): now unused — keep them dark.
  for (int i = 0; i < kSButtonCount; ++i) {
    setLed(kSButtonCCFirst + i, false);
  }

  // M buttons (CC 48..55): lit iff layer paused.
  for (int i = 0; i < kMButtonCount; ++i) {
    bool paused = i < static_cast<int>(pauseParamPtrs.size())
                  && pauseParamPtrs[i]
                  && pauseParamPtrs[i]->get();
    setLed(kMButtonCCFirst + i, paused);
  }

  // Transport LEDs.
  auto hibState = synthPtr->getHibernationState();
  using S = ofxMarkSynth::HibernationController::State;
  setLed(kPlayButtonCC, hibState == S::ACTIVE || hibState == S::FADING_IN);
  setLed(kStopButtonCC, hibState == S::HIBERNATED || hibState == S::FADING_OUT);
  setLed(kRecordButtonCC, synthPtr->getRuntimeSubsystem().getActiveSaveCount() > 0);

  // Rewind / FFwd: always-on except briefly dark during press flash.
  uint64_t nowMs = ofGetElapsedTimeMillis();
  bool rewindLit = nowMs >= rewindFlashUntilMs;
  bool ffwdLit = nowMs >= ffwdFlashUntilMs;
  setLed(kRewindButtonCC, rewindLit);
  setLed(kFFwdButtonCC, ffwdLit);
}

void NanoKontrol2Controller::setLed(int cc, bool lit) {
  if (!connected) return;

  int desired = lit ? 127 : 0;
  auto it = lastSentLedValue.find(cc);
  if (it != lastSentLedValue.end() && it->second == desired) {
    return;
  }

  midiOut.sendControlChange(kMidiChannel, cc, desired);
  lastSentLedValue[cc] = desired;
}

void NanoKontrol2Controller::clearAllManagedLeds() {
  if (!connected) return;

  // Every CC we drive plus all unused button CCs the device might be self-lit on.
  // Sending 0 to each forces a known dark state regardless of prior state.
  static constexpr int kClearCCs[] = {
    // S row, M row, R row (R is unused but cleared for visible dark)
    32, 33, 34, 35, 36, 37, 38, 39,
    48, 49, 50, 51, 52, 53, 54, 55,
    64, 65, 66, 67, 68, 69, 70, 71,
    // Transport
    41, 42, 43, 44, 45, 46,
    // Track ◀ / ▶
    58, 59,
    // Marker ◀ / ■ / ▶  (61 / 60 / 62)
    60, 61, 62,
  };

  // Force the cache to be re-sent by clearing it first.
  lastSentLedValue.clear();

  for (int cc : kClearCCs) {
    midiOut.sendControlChange(kMidiChannel, cc, 0);
    lastSentLedValue[cc] = 0;
  }
}

void NanoKontrol2Controller::resetFaderPickupStates() {
  for (auto& fs : faderStates) {
    fs.lastMidiValue = -1.0f;
  }
  previewGainKnobState.lastMidiValue = -1.0f;
}

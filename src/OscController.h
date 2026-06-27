#pragma once

#include <array>
#include <memory>
#include <string>

#include "ofParameter.h"
#include "ofxOsc.h"

namespace ofxMarkSynth {
  class Synth;
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
///   /master/alpha       fader   -> master composite alpha
///   /intent/<i>         fader   -> intent activation i (0..5: Energy..Stillness)
///   /intent/strength    fader   -> master intent strength
///   /synth/agency       fader   -> "agency"
///   /synth/audiogain    fader   -> "AudioResp"
///   /synth/motiongain   fader   -> "VideoResp"
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

  ofxOscReceiver receiver;
  ofxOscSender   sender;
  bool listening   = false;
  bool senderReady = false;
  std::string remoteHost;   // learned from the first inbound packet

  // /intent/0../5 in on-screen order; resolved by name against the intent group.
  static const std::array<std::string, 6> kIntentNames;

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
  void sendFloat(const std::string& addr, float value);
  void sendString(const std::string& addr, const std::string& value);
  static float normOf(ofParameter<float>& p);
};

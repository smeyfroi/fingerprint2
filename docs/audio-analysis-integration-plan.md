# Audio analysis: integrate every window instead of point-sampling

**Status:** analysis + plan, nothing implemented yet.
**Why this exists:** to be folded into the in-flight multi-channel audio stream work. The
multi-channel changes touch the same files and the same `analyseBuffer()` / Gist boundary,
so the two efforts should land together. Multi-channel intersection points are called out
inline with **[MULTICHANNEL]** tags.

---

## TL;DR

Gist runs once per audio buffer on the **audio thread** and writes its result into a single
shared `scalarValues[]` member, overwriting the previous buffer. The **graphics thread**
then reads whatever happens to be in that array once per rendered frame. So the render loop
*point-samples an arbitrary audio window* rather than integrating every window the analyser
produces. With a 512-sample window (~11 ms) and 60 fps frames (~16.7 ms), roughly **1 in 3
analysis windows is overwritten and never seen**; at 30 fps it's 2 in 3. Onset detection,
the one-euro filters, and "data updated" gating are all compromised, and there's an
unsynchronised cross-thread data race on top.

The fix: hand **every** per-window feature frame (with an audio-domain timestamp) across to
the consumer via a lock-free queue, and step the filters/detectors once per drained frame.

---

## Current architecture (as built)

Active source tree is **`ofxAudioData`**. The `ofxAudioAnalysisClient` / `LocalGistClient`
tree is the older, now-unused twin — ignore it except as reference.

Session currently runs **file mode** with `"audioBufferSize": 512`
(`session-config.reference.json`), and the config comment explicitly calls that the
"analysis FFT window".

Source construction: `ofxMarkSynth/src/core/Synth.cpp` → `createAudioAnalysisSource()`
(file → `AudioFileSource`, mic → `AudioDeviceSource`; both get `audioBufferSize`).

### Two threads, loosely coupled

**Audio thread** — `AudioFileSource::process()` (`ofxAudioData/src/AudioFileSource.cpp`) or
`AudioDeviceSource::audioIn()` (`ofxAudioData/src/AudioDeviceSource.cpp`), once per buffer:
- calls `AudioAnalysisSourceBase::analyseBuffer()`
  (`ofxAudioData/src/AudioAnalysisSourceBase.cpp:9`)
- which runs `gist.processAudio(...)` and writes raw features straight into the plain
  `scalarValues[]` member array (and reassigns the `mfcc` vector).
- **Each buffer overwrites the previous one. Nothing accumulates.**

**Graphics thread** — `Processor::update()` (`ofxAudioData/src/Processor.cpp:58`) called from
`AudioDataSourceMod::update()`
(`ofxMarkSynth/src/sourceMods/AudioDataSourceMod.cpp:305`), once per rendered frame:
- reads the *current* `scalarValues` snapshot,
- stamps `lastUpdateTimestamp = ofGetElapsedTimef()`, steps the one-euro filters, computes
  divergences (`Processor::updateCurrentValues()`, `Processor.cpp:84`),
- runs `detectOnset1` / `detectTimbreChange1` / `detectPitchChange1`.

`ofxGist::processAudio` (`ofxGist/src/ofxGist.cpp:24`) sets the Gist frame size to the buffer
size and runs **one FFT frame over the whole buffer** — so feature time-resolution is dictated
by the device buffer size, not chosen for analysis.

---

## The defect, in numbers

- 512 samples @ 44.1 kHz ≈ **11.6 ms** per window; @ 48 kHz ≈ **10.7 ms**.
- One render frame @ 60 fps = **16.7 ms**.

Audio windows are produced faster than the render loop reads them, so ~1.5 windows arrive per
frame and **~1 in 3 is overwritten before it's ever read**. At 30 fps (heavy GPU load), 2 of 3
are dropped. *Which* window survives is set by the arbitrary phase between the audio callback
and vsync — non-deterministic.

Consequences:

1. **Onset/transient detection reads non-adjacent, randomly-phased windows.**
   `complexSpectralDifference` is a frame-to-frame delta; computing it across dropped/repeated
   windows makes the magnitudes meaningless vs Gist's intent, and transients in dropped windows
   are never detected.
2. **One-euro filters are stepped at the wrong rate with the wrong clock** — fed at render rate
   using wall-clock `ofGetElapsedTimef()`, not at window rate using an audio clock. Smoothing
   therefore depends on framerate, not on the signal.
3. **`isDataUpdated()` doesn't detect new audio** — `lastUpdateTimestamp` is set every frame
   regardless, so the gate (`AudioDataSourceMod.cpp:318`) really means "did a frame happen."
4. **Defeats the config's own goal** — the shared 512 window is meant so a recorded take and its
   replay "analyse identically," but the analysis is then non-deterministically subsampled by the
   render loop, so replay does *not* reproduce the same downstream triggers.

### Secondary issues found
- **Data race:** `scalarValues` (and the reassigned `mfcc` vector) are written on the audio
  thread and read on the graphics thread with no atomics/lock. The 9 scalars can be read as a
  torn mix of two windows; the `mfcc` reassignment can reallocate mid-read (UB).
- **[MULTICHANNEL] Interleaved-stereo analysis bug.** `ofxGist::processAudio` sets the frame
  size to `numFrames` but hands Gist the full *interleaved* multichannel buffer
  (`ofxGist.cpp:24`). With >1 input channel this analyses interleaved L/R as if mono. Mic mode
  (`AudioDeviceSource`) opens `numInputChannels = channelCount`, which can be 2. **This is
  squarely in the multi-channel work's path** — see Phase 4.

---

## Plan

Core idea: **consume every analysis window the audio thread produces**, with an audio-domain
timestamp, instead of point-sampling the latest. Keep all filtering/detection on the graphics
thread so the GUI-bound `ofParameter` thresholds are never touched from the audio thread.

### Phase 1 — Per-window feature queue (the actual fix)
1. In `AudioAnalysisSourceBase`, replace the single `scalarValues` member with a **bounded
   lock-free SPSC ring** of feature frames. Each frame = `{ scalarValues, mfcc snapshot,
   audioTimestamp }`, where `audioTimestamp` is a running sample-counter / sampleRate
   (monotonic, jitter-free). `analyseBuffer()` pushes one frame per window.
2. Add `drainFrames()` to the source interface (`IAudioAnalysisSource.hpp`). In
   `Processor::update()`, **drain all queued frames** and step the one-euro filters + run
   `detectOnset/Timbre/Pitch` once *per drained frame*, using each frame's audio timestamp as
   the filter `dt`. Restores true per-hop deltas, catches every transient, makes smoothing
   framerate-independent. Existing filter coefficients keep their meaning (the one-euro filter
   self-adjusts from timestamps — they were only "seeded at 60 Hz").
3. Fix `isDataUpdated` to mean "≥1 new frame drained."
4. The data race disappears as a side effect (SPSC ring; mfcc copied into each frame).

### Phase 2 — Trigger emission semantics (DECISION NEEDED)
Today a render frame emits at most one onset. After draining, several windows (and onsets) can
fall in one render frame. Pick one for `AudioDataSourceMod::update()`:
- **(a) Coalesce** to the strongest onset/timbre/pitch per render frame — simplest; downstream
  `AgencyController` already paces events.
- **(b) Latch & emit each** event from the drained frames — most faithful; more triggers reach
  the Mod network.

### Phase 3 — Decouple analysis hop from device buffer (optional, larger)
Push audio into a ring and run a fixed analysis hop/window with overlap (e.g. 1024 window /
512 hop) chosen for analysis quality rather than device latency. Defer unless Phase 1 is
insufficient.
**[MULTICHANNEL]** If the multi-channel work introduces a ring/mixdown stage anyway, this phase
can share that buffer plumbing — coordinate so we don't build two rings.

### Phase 4 — Mono downmix before Gist (small, independent) **[MULTICHANNEL — primary overlap]**
Downmix (or channel-select) to mono before `gist.processAudio`, fixing the interleaved-stereo
analysis bug. This is the natural place for the two efforts to meet: the multi-channel session
decides the channel policy (downmix vs select vs per-channel analysis), and this plan just needs
*a* mono frame per window to feed Gist. If per-channel analysis is wanted, the feature frame in
Phase 1 generalises to per-channel `scalarValues` — worth deciding jointly before Phase 1 locks
the frame struct.

---

## Risks
- **Re-tuning.** Seeing every window (vs ~⅔) changes onset density and filter response, so the
  frozen trigger baselines will likely need a re-tune. The old tuning partly compensated for the
  framerate-dependent artefact this fix removes. (Relates to the existing motion/audio
  trigger-tuning TODO.)
- **Audio-thread cost.** Phase 1 keeps Gist where it already runs; only adds an allocation-free
  ring push. Filtering/detection stays on the graphics thread.
- **Frame-struct coupling with multi-channel.** Decide mono-vs-per-channel feature frames
  (Phase 4) before finalising the Phase 1 ring element type.

## Verification
- Deterministic-replay check: same WAV + start position should yield an identical per-frame
  trigger sequence regardless of forced fps (30/60/120). Log drained-frame count and trigger
  times to confirm no windows dropped and the sequence is fps-invariant.

---

## File index (load these first)
- `apps/myApps/fingerprint2/session-config.reference.json` — `audioBufferSize`, file/mic mode
- `ofxMarkSynth/src/core/Synth.cpp` — `createAudioAnalysisSource()`
- `ofxMarkSynth/src/sourceMods/AudioDataSourceMod.{hpp,cpp}` — consumer / emit + gate
- `ofxAudioData/src/AudioAnalysisSourceBase.{hpp,cpp}` — `analyseBuffer()`, `scalarValues`
- `ofxAudioData/src/AudioDeviceSource.{hpp,cpp}` — mic capture, `audioIn()`
- `ofxAudioData/src/AudioFileSource.{hpp,cpp}` — file playback, `process()`
- `ofxAudioData/src/Processor.{hpp,cpp}` — one-euro filters, divergence, onset/timbre/pitch
- `ofxAudioData/src/IAudioAnalysisSource.hpp` — source interface (add `drainFrames()`)
- `ofxGist/src/ofxGist.cpp` — `processAudio()` (frame-size + interleaved-channel handling)
</content>

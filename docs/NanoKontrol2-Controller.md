# Korg nanoKONTROL2 Controller Implementation

## Overview

This document describes the Korg nanoKONTROL2 USB-MIDI controller integration with fingerprint2. The nanoKONTROL2 is a small, portable surface providing 8 sliders, 8 knobs, 24 channel buttons (S/M/R), and a transport strip. fingerprint2 uses a subset of these to mirror the most useful per-strip and transport functions from the larger surfaces, freeing the APC Mini and LC XL3 for other roles.

Status: implemented in `src/NanoKontrol2Controller.h` and `src/NanoKontrol2Controller.cpp`.

**Relationship with the other controllers:**
- **Launch Control XL 3** — Detailed parameter control (encoders for audio analysis, faders for intents/layers, OLED display).
- **APC Mini MK2** — Visual config grid navigation only (56 RGB pads, hold-to-confirm jump). Faders, bottom pad row, and Track buttons are intentionally unused.
- **Korg nanoKONTROL2** — Strip alpha + pause control + ergonomic transport row. Owns the strip alpha / pause / prev-next-config / save / hibernate roles that previously lived on the APC Mini.

Throughout, a "strip" is whatever the loaded config binds the channel strips to: **chain groups** when the config authors a chains manifest (room / voice1 / ... — the same strips the iPad shows), **layers** otherwise. The branch is taken per message via `RenderSubsystem::hasChainManifest()`.

All three controllers can operate simultaneously without conflict — each owns its own input range and writes the same shared `ofParameter` targets through the shared value-scaling takeover pattern (`FaderTakeover.h`).

---

## Hardware Reference

### Device Identification

| Property | Value |
|----------|-------|
| Port name pattern | `"nanoKONTROL2"` (substring match) |
| MIDI channel | 1 (factory Scene 1) |
| Class-compliant | Yes — no driver required on macOS (CoreMIDI) |

### Physical Layout

```
┌──────────────────────────────────────────────────────────┐
│  [Knobs 1-8]                                             │
│   ◯  ◯  ◯  ◯  ◯  ◯  ◯  ◯                  [Transport]    │
│  CC CC CC CC CC CC CC CC                                 │
│  16 17 18 19 20 21 22 23           ◀◀  ▶▶               │
│                                    43  44               │
│  [S buttons — top]                                       │
│  ▢  ▢  ▢  ▢  ▢  ▢  ▢  ▢            ▶   ⏹   ⏺           │
│  CC CC CC CC CC CC CC CC          41  42  45            │
│  32 33 34 35 36 37 38 39                                 │
│                                  [Cycle] [Marker ◀ ■ ▶] │
│  [M buttons — middle]              46     61 60 62      │
│  ▢  ▢  ▢  ▢  ▢  ▢  ▢  ▢                                  │
│  CC CC CC CC CC CC CC CC          [Track ◀ ▶]            │
│  48 49 50 51 52 53 54 55           58  59               │
│                                                          │
│  [R buttons — bottom]                                    │
│  ▢  ▢  ▢  ▢  ▢  ▢  ▢  ▢                                  │
│  CC CC CC CC CC CC CC CC                                 │
│  64 65 66 67 68 69 70 71                                 │
│                                                          │
│  [Sliders 1-8]                                           │
│  │  │  │  │  │  │  │  │                                  │
│  │  │  │  │  │  │  │  │                                  │
│  CC CC CC CC CC CC CC CC                                 │
│   0  1  2  3  4  5  6  7                                 │
└──────────────────────────────────────────────────────────┘
```

### MIDI CC Map (factory Scene 1, channel 1)

All controls send Control Change messages on channel 1. Buttons send value 127 on press and 0 on release.

| Control | CC | Function in fingerprint2 |
|---------|-----|--------------------------|
| Slider 1-7 | 0-6 | Strip alpha — chain alphas when the config has a chains manifest, layer alphas otherwise (value-scaling takeover) |
| Slider 8 | 7 | Master composite alpha (value-scaling takeover) |
| Knob 1-7 | 16-22 | **Unused** |
| Knob 8 | 23 | Texture-preview gain (value-scaling takeover) |
| S button 1-8 | 32-39 | **Unused** — held dark |
| M button 1-8 | 48-55 | Toggle strip pause; LED reflects pause state |
| R button 1-8 | 64-71 | **LED only** — lit when strip N exists; R8 always lit (master-alpha cue) |
| Track ◀ / ▶ | 58 / 59 | **Unused** |
| Cycle | 46 | **Unused** |
| Marker ◀ / ■ / ▶ | 61 / 60 / 62 | **Unused** |
| Rewind ⏪ | 43 | Previous config (`OF_KEY_LEFT`) |
| Fast-Fwd ⏩ | 44 | Next config (`OF_KEY_RIGHT`) |
| Stop ⏹ | 42 | Hibernate (`H` key) |
| Play ▶ | 41 | Wake / unhibernate (`SPACE` key) |
| Record ⏺ | 45 | Manual image save |

---

## Critical Prerequisite: External LED Mode

**The device must be flashed to "External LED Mode" once, before LED feedback will work.** Without this prep, the sliders and button presses still work as inputs, but every LED will be self-driven by the device (lit while pressed, dark when released) and will ignore everything our code sends.

### One-time setup

1. Install **Korg Kontrol Editor** (free download from korg.com — macOS / Windows).
2. Plug in the nanoKONTROL2 and launch Kontrol Editor.
3. Click **Communication → Read Scene Data** to load the current device state.
4. In the Common tab, set **LED Mode = External**.
5. Click **Communication → Write Scene Data** to flash the change back to the device.
6. Optionally save the scene as a `.nktrl2_data` file for backup (see `korg-set-external-led-mode.nktrl2_data` in the project root).

This setting persists across reboots and unplugs. You only need to do it once per device.

### LED control protocol (External Mode)

Once in External mode, the controller's LEDs respond to incoming MIDI:

- **Channel:** 1
- **Message type:** Control Change
- **CC number:** same CC the button transmits on input (e.g. send CC 41 to light the Play LED)
- **Value:** 127 = on, 0 = off
- **No SysEx required**

fingerprint2 caches the last-sent state per CC and only re-sends when the desired state changes, keeping MIDI bandwidth minimal during the per-frame poll.

---

## Feature Mapping

### Sliders 1-7 — Strip Alpha; Slider 8 — Master Alpha

Sliders 1-7 drive strip alphas. The binding branches per message on `RenderSubsystem::hasChainManifest()`: when the loaded config authors a chains manifest they write `getChainAlphaParameters()` (the chain-group strips the iPad shows), otherwise `getLayerAlphaParameters()` by index. Slider 8 is reserved: it always drives the master composite alpha via `getMasterAlphaParameter()` (`kMasterAlphaFaderIndex`).

| Slider | CC | Function |
|--------|-----|----------|
| 1-7 | 0-6 | Strip 1-7 alpha (chain or layer) |
| 8 | 7 | Master composite alpha |

**Value-scaling takeover (Ableton-style):** there is no pickup window. The first CC after a takeover reset only baselines the fader position — the parameter does not move. From then on, fader movement is scaled against the runway remaining on the side it moves toward (`FaderTakeover::valueScale` in `FaderTakeover.h`, applied through `applyPickup` in `FaderPickup.h`): pushing up spreads the remaining upward travel over the parameter's remaining headroom, and pulling down scales the parameter proportionally, so **pulling the fader to the bottom always brings the parameter to 0**. At either endpoint fader and parameter align exactly and then track 1:1. Takeover state resets per control on every config load and unload (`onSynthDidLoad` / `onSynthWillUnload`), so the first touch after a reload baselines instead of jumping the parameter.

**Recovery gesture:** if a fader has drifted far from its parameter, swipe it to the bottom (the parameter reaches 0 there) and ride it back up — from the aligned endpoint the two track together.

If strip N does not exist in the current config (index beyond the bound alpha group's size), slider N is a silent no-op on move.

### Knob 8 — Texture-Preview Gain

Only the rightmost knob (CC 23, sitting above the master-alpha fader) is mapped: it drives the GUI's texture-preview gain (`Synth::getPreviewGainParameter()` — thumbnails and hover/probe popups) through the same value-scaling takeover as the faders, so it picks the parameter up smoothly rather than jumping it on first touch. Knobs 1-7 (CC 16-22) are ignored.

### S Buttons (top row) — Unused

CCs 32-39 have no press action, and the per-frame LED poll holds the row dark. (The layer-existence indicator that used to live here moved to the R buttons, putting the light on the bottom button of each channel strip, next to the fader.)

### M Buttons (middle row) — Strip Pause Toggle

Pressing an M button toggles pause for the corresponding strip — `RenderSubsystem::toggleChainPause(int)` when the config authors a chains manifest, else `toggleLayerPause(int)`. The keyboard `1`-`8` shortcuts follow the same chain-vs-layer binding, so key N and M button N always point at the same strip.

| M Button | CC | Function | LED |
|----------|-----|----------|-----|
| 1-8 | 48-55 | Toggle strip N pause (chain or layer) | Lit when strip N is paused |

Presses on a strip that doesn't exist in the current config are ignored. LED state is polled each frame from the pause parameter, so changes from other controllers or the keyboard reflect here within one update cycle.

### R Buttons (bottom row) — Strip Existence Indicator

The R buttons are **LED-only** — pressing them does nothing. R1-R7 are lit when the corresponding strip exists in the current config (its pause parameter pointer is non-null). R8 is **always lit**: it belongs to the master-alpha fader, which is always live, and gets the same "active strip" cue the other faders do.

| R Button | CC | LED |
|----------|-----|-----|
| 1-7 | 64-70 | Lit when strip N exists (chain or layer) |
| 8 | 71 | Always lit — master-alpha cue |

This makes the row a live readout of how many strips the current performance config exposes (and therefore how many sliders are actually doing anything).

### Transport Strip

The transport row is the primary motivation for adding this controller — its layout is more ergonomic for performance than the LC XL3's transport buttons.

| Button | CC | Function | LED behavior |
|--------|-----|----------|--------------|
| Rewind ⏪ | 43 | Previous config (`synthPtr->keyPressed(OF_KEY_LEFT)`) | Always lit; brief flash-off (~120ms) on press |
| Fast-Fwd ⏩ | 44 | Next config (`synthPtr->keyPressed(OF_KEY_RIGHT)`) | Always lit; brief flash-off on press |
| Stop ⏹ | 42 | Hibernate (`synthPtr->keyPressed('H')`) | Lit when hibernation state is `HIBERNATED` or `FADING_OUT` |
| Play ▶ | 41 | Wake / unhibernate (`synthPtr->keyPressed(OF_KEY_SPACE)`) | Lit when hibernation state is `ACTIVE` or `FADING_IN` |
| Record ⏺ | 45 | Manual image save (`synthPtr->saveImage()`) | Lit while `runtime.getActiveSaveCount() > 0` |

**Why route through `keyPressed`:** going through `Synth::keyPressed(...)` rather than calling `hibernationController->wake()` directly ensures the paused-flag, time-tracker, and per-config side-effects in `Synth.cpp` are applied identically to keyboard-driven hibernation. This mirrors how `ApcMiniController` invokes prev/next config.

**Play/Stop edge cases:** `HibernationController::hibernate()` and `wake()` return `false` and do nothing when called against the matching steady-state. Pressing Play while already active or Stop while already hibernated is a safe no-op.

**Cycle / Marker / Track-arrow buttons:** CCs 46, 58, 59, 60, 61, 62 are intentionally unused and explicitly cleared on connect.

---

## LED State Polling

LED state is recomputed every frame in `pollAndUpdateLeds()`, called from `update()` on the main thread after the MIDI event ring buffer is drained. Per-CC cached state ensures we only emit a MIDI message when the *desired* state changes.

The Rewind/FFwd LEDs use an `until-ms` timestamp to force them dark for ~120ms on press, then snap back to their default-on state on the next poll.

**Resync on config load:** every `onSynthDidLoad` clears all managed LEDs to a known dark state (`clearAllManagedLeds()`, which also empties the send cache), then the next `pollAndUpdateLeds()` repaints them from the new config's parameters — so the strip-existence and pause LEDs always reflect the freshly loaded config, never the previous one.

---

## Connection Lifecycle

`NanoKontrol2Controller` follows the same lifecycle as the other controllers (matching `ApcMiniController`'s shape):

| Method | Called by `ofApp` | Behavior |
|--------|-------------------|----------|
| `update()` | `ofApp::update()` | Drain MIDI ring buffer, poll + send LED updates |
| `exit()` | `ofApp::exit()` | Clear all managed LEDs, close MIDI ports |
| `onSynthDidLoad()` | After synth config loads | (Re)connect if needed; reset fader/knob takeover state; clear all managed LEDs so the next poll repaints from the new config |
| `onSynthWillUnload()` | Before synth config unloads | Clear managed LEDs; reset takeover state; drop the synth reference |
| `newMidiMessage()` | ofxMidi listener (MIDI thread) | Push event into ring buffer |

On `tryConnect()`, the controller sends `value=0` to every CC it ever touches (controlled and unused alike), so unused buttons go visibly dark regardless of prior state from the device itself or another host.

The connect attempt is retried on each `onSynthDidLoad`, so the device can be hot-plugged: replug and reload a config and it reconnects.

---

## Threading

`newMidiMessage()` runs on the MIDI listener thread; all `ofParameter` mutation, `keyPressed` dispatch, and `ofxMidiOut` sends happen on the main thread.

The handoff uses a lock-free, drop-on-full SPSC ring buffer (`CCEvent` struct in a `MidiEventRing<CCEvent, 64>`, the same ring shared by the other MIDI surfaces). Events are drained in `update()` before LEDs are polled.

---

## Implementation Status

### Phase 1: Core Input
- [x] Create `NanoKontrol2Controller.h` / `.cpp` matching the existing controller pattern
- [x] Device detection via port name substring match
- [x] Connect/disconnect handling with retry on synth load
- [x] Ring-buffer threading for MIDI events
- [x] Add to `ofApp` lifecycle (setup, update, exit)

### Phase 2: Sliders + Knob
- [x] Map CC 0-6 to strip alphas (chain or layer, branching on chains manifest) and CC 7 to master composite alpha
- [x] Map CC 23 (rightmost knob) to texture-preview gain
- [x] Value-scaling takeover (Ableton-style, `FaderTakeover.h` / `FaderPickup.h`)
- [x] Takeover reset on synth load/unload
- [x] Silent no-op for missing strips

### Phase 3: Channel Buttons
- [x] S buttons (CC 32-39): unused, held dark
- [x] M buttons (CC 48-55): toggle strip pause; LED reflects pause state
- [x] R buttons (CC 64-71): LED-only, lit when strip exists; R8 always-on master cue

### Phase 4: Transport
- [x] Play → `OF_KEY_SPACE` (wake); LED reflects hibernation state
- [x] Stop → `'H'` (hibernate); LED reflects hibernation state
- [x] Rewind → `OF_KEY_LEFT` (prev config); LED always-on + flash
- [x] Fast-Fwd → `OF_KEY_RIGHT` (next config); LED always-on + flash
- [x] Record → `saveImage()`; LED while save in progress

### Phase 5: LED Output
- [x] Per-CC cached state, only emit on change
- [x] Clear all managed CCs on connect (including unused ones)
- [x] Clear + repaint all managed LEDs on every config load
- [x] Per-frame poll for strip-existence, strip-pause, hibernation, save-count state

### Open / future
- [ ] Knob input (CC 16-22) — currently unused; could be mapped to additional named params
- [ ] Track arrow buttons (CC 58/59) — currently unused; could become bank-shift for sliders beyond the first seven strips

---

## Implementation Notes

- Implementation: `src/NanoKontrol2Controller.h`, `src/NanoKontrol2Controller.cpp`
- The factory Scene 1 mapping is assumed throughout. If you reflash the device's MIDI mapping (CC numbers, channels) via Kontrol Editor, the constants in `NanoKontrol2Controller.h` need to match.
- LEDs require **External LED Mode** — see prerequisite section above.

## References

- Korg nanoKONTROL2 product page: https://www.korg.com/products/computergear/nanokontrol2/
- Korg Kontrol Editor (LED Mode flash + scene backup): https://www.korg.com/products/computergear/nanokontrol_studio/editor.php
- Backup scene file in project root: `korg-set-external-led-mode.nktrl2_data`
- Existing controller implementations: `src/MidiController.cpp` (Launch Control XL 3), `src/ApcMiniController.cpp` (APC Mini MK2)
- Render subsystem API: `addons/ofxMarkSynth/src/subsystem/SynthSubsystems.hpp`
- Hibernation state machine: `addons/ofxMarkSynth/src/controller/HibernationController.hpp`
- ofxMidi documentation: https://github.com/danomatika/ofxMidi

## Related

- [MIDI-Controller.md](MIDI-Controller.md) — Launch Control XL 3 (detailed parameter control)
- [APC-Mini-Controller.md](APC-Mini-Controller.md) — config grid + synth fader trio
- [Performer guide](../../../../addons/ofxMarkSynth/docs/performer-guide.md) — the live controls these surfaces drive

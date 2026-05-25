# Korg nanoKONTROL2 Controller Implementation

## Overview

This document describes the Korg nanoKONTROL2 USB-MIDI controller integration with fingerprint2. The nanoKONTROL2 is a small, portable surface providing 8 sliders, 8 knobs, 24 channel buttons (S/M/R), and a transport strip. fingerprint2 uses a subset of these to mirror the most useful per-layer and transport functions from the larger surfaces, freeing the APC Mini and LC XL3 for other roles.

Status: implemented in `src/NanoKontrol2Controller.h` and `src/NanoKontrol2Controller.cpp`.

**Relationship with the other controllers:**
- **Launch Control XL 3** — Detailed parameter control (encoders for audio analysis, faders for intents/layers, OLED display).
- **APC Mini MK2** — Visual config grid navigation only (56 RGB pads, hold-to-confirm jump). Faders, bottom pad row, and Track buttons are intentionally unused.
- **Korg nanoKONTROL2** — Layer alpha + mute control + ergonomic transport row. Owns the layer alpha / pause / prev-next-config / save / hibernate roles that previously lived on the APC Mini.

All three controllers can operate simultaneously without conflict — each owns its own input range and writes the same shared `ofParameter` targets through the standard pickup/soft-takeover pattern.

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
| Slider 1-8 | 0-7 | Layer alpha (with pickup) |
| Knob 1-8 | 16-23 | **Unused** |
| S button 1-8 | 32-39 | **LED only** — lit when layer N exists |
| M button 1-8 | 48-55 | Toggle layer pause (mute); LED reflects pause state |
| R button 1-8 | 64-71 | **Unused** |
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

### Sliders 1-8 — Layer Alpha

Sliders are bound to `Synth::getRenderSubsystem().getLayerAlphaParameters()` — the same parameter group the LC XL3 (in Shift mode) writes to.

| Slider | CC | Function |
|--------|-----|----------|
| 1 | 0 | Layer 1 alpha |
| 2 | 1 | Layer 2 alpha |
| 3 | 2 | Layer 3 alpha |
| 4 | 3 | Layer 4 alpha |
| 5 | 4 | Layer 5 alpha |
| 6 | 5 | Layer 6 alpha |
| 7 | 6 | Layer 7 alpha |
| 8 | 7 | Layer 8 alpha |

**Pickup / soft-takeover:** sliders use 5% pickup (`kPickupThreshold`) — when the physical slider position is more than 5% away from the parameter's current normalized value, moving it has no effect until you sweep through the pickup window. Pickup state resets per slider on every `onSynthDidLoad`, so reloading a config requires re-engaging each slider.

If layer N does not exist in the current config (i.e. `getLayerPauseParamPtrs()[N] == nullptr`), slider N is a silent no-op on move.

### S Buttons (top row) — Layer Existence Indicator

The S buttons are **LED-only** — pressing them does nothing. Their LEDs reflect whether the corresponding layer exists in the current config.

| S Button | CC | LED |
|----------|-----|-----|
| 1-8 | 32-39 | Lit when `getLayerPauseParamPtrs()[N] != nullptr` |

This makes the row a live readout of how many layers the current performance config exposes (and therefore how many sliders are actually doing anything).

### M Buttons (middle row) — Layer Mute Toggle

Pressing an M button toggles the corresponding layer's pause state via `RenderSubsystem::toggleLayerPause(int)` — same call previously triggered by the APC Mini bottom-row pads, and still reachable via the keyboard `1`-`8` shortcuts.

| M Button | CC | Function | LED |
|----------|-----|----------|-----|
| 1-8 | 48-55 | Toggle layer N pause | Lit when layer N is paused |

LED state is polled each frame from the pause parameter, so changes from other controllers or the keyboard reflect here within one update cycle.

### R Buttons (bottom row) — Unused

CCs 64-71 are intentionally unused. Their LEDs are explicitly cleared on connect and never written to again, so the row stays visibly dark.

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

---

## Connection Lifecycle

`NanoKontrol2Controller` follows the same lifecycle as the other controllers (matching `ApcMiniController`'s shape):

| Method | Called by `ofApp` | Behavior |
|--------|-------------------|----------|
| `update()` | `ofApp::update()` | Drain MIDI ring buffer, poll + send LED updates |
| `exit()` | `ofApp::exit()` | Clear all managed LEDs, close MIDI ports |
| `onSynthDidLoad()` | After synth config loads | (Re)connect if needed; reset slider pickup state |
| `onSynthWillUnload()` | Before synth config unloads | Clear the synth reference |
| `newMidiMessage()` | ofxMidi listener (MIDI thread) | Push event into ring buffer |

On `tryConnect()`, the controller sends `value=0` to every CC it ever touches (controlled and unused alike), so unused buttons go visibly dark regardless of prior state from the device itself or another host.

The connect attempt is retried on each `onSynthDidLoad`, so the device can be hot-plugged: replug and reload a config and it reconnects.

---

## Threading

`newMidiMessage()` runs on the MIDI listener thread; all `ofParameter` mutation, `keyPressed` dispatch, and `ofxMidiOut` sends happen on the main thread.

The handoff uses a lock-free ring buffer (`ButtonEvent` struct, same pattern as `MidiController::buttonEventBuffer`). Events are drained in `update()` before LEDs are polled.

---

## Implementation Status

### Phase 1: Core Input
- [x] Create `NanoKontrol2Controller.h` / `.cpp` matching the existing controller pattern
- [x] Device detection via port name substring match
- [x] Connect/disconnect handling with retry on synth load
- [x] Ring-buffer threading for MIDI events
- [x] Add to `ofApp` lifecycle (setup, update, exit)

### Phase 2: Sliders
- [x] Map CC 0-7 to layer alpha parameters
- [x] Soft-takeover pickup (5% threshold)
- [x] Pickup reset on synth reload
- [x] Silent no-op for missing layers

### Phase 3: Channel Buttons
- [x] S buttons (CC 32-39): LED-only, lit when layer exists
- [x] M buttons (CC 48-55): toggle layer pause; LED reflects pause state
- [x] R buttons (CC 64-71): unused, cleared

### Phase 4: Transport
- [x] Play → `OF_KEY_SPACE` (wake); LED reflects hibernation state
- [x] Stop → `'H'` (hibernate); LED reflects hibernation state
- [x] Rewind → `OF_KEY_LEFT` (prev config); LED always-on + flash
- [x] Fast-Fwd → `OF_KEY_RIGHT` (next config); LED always-on + flash
- [x] Record → `saveImage()`; LED while save in progress

### Phase 5: LED Output
- [x] Per-CC cached state, only emit on change
- [x] Clear all managed CCs on connect (including unused ones)
- [x] Per-frame poll for layer-pause, hibernation, save-count state

### Open / future
- [ ] Knob input (CC 16-23) — currently unused; could be mapped to additional named params
- [ ] Track arrow buttons (CC 58/59) — currently unused; could become bank-shift for sliders beyond layer 8

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

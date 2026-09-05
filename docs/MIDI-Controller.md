# MIDI Controller Reference

## Launch Control XL 3 - fingerprint2 Mapping

This document describes the MIDI controller mapping for the Novation Launch Control XL 3 in DAW mode.

**Companion controllers:** fingerprint2 also integrates the Akai APC Mini MK2 (`APC-Mini-Controller.md`) and the Korg nanoKONTROL2 (`NanoKontrol2-Controller.md`). All three can run simultaneously without conflict — each writes to the same shared `ofParameter` targets through pickup/soft-takeover.

---

## Scope

There is no shift mode and no transport handling on this controller any more. Play/Pause, Hibernate, Save Image, and Previous/Next config all live on the Korg nanoKONTROL2, and layer control lives on the APC Mini. The Novation is: faders → Intent, encoders → audio-analysis nudge, top-row intent indicator LEDs, top-row intent LEDs.

---

## Top Row Buttons (1-8) - Intent Indicators

These buttons are **LED indicators only** (button presses do nothing):

| Button | Indicator |
|--------|-----------|
| 1-7 | Intent pole 1-7 |
| 8 | Master intent strength |

LED behavior:
- Buttons 1-7 light in their **axis-pair hue** — the same hues as the GUI's Intents panel (presence coral, motion cyan, order violet, memory green), one hue per bipolar pair
- **Bright** = pole value > 0.0 and master strength > 0.0
- **Dim** = pole armed but value == 0.0, or master strength == 0.0
- Button 8 is the master itself: **amber** when master strength > 0.0, dim amber at zero
- **Off** = no intent parameters loaded

---

## Bottom Row Buttons (9-16) - retired

These recalled Mod Snapshot slots 1-8 (CC 45-52) until **2026-09-03**. They are now unassigned and stay dark.

Why they went: this row was the only surface anywhere that addressed a snapshot slot by its **position**, and the engine's `ModSnapshotManager::NUM_SLOTS = 8` existed to match the eight buttons rather than for any reason of its own. Snapshots are driven from the APC Mini performance grid now — as scene cells that apply a whole group at once — so the cap was pure friction: three of the four performance quadrants need more than eight slots. The cap is now 64 and the row is free for a future job.

---

## Encoders (Knobs)

> **Note:** Synth Agency, Audio Response, and Video Response previously lived on encoders 1, 9, and 17 (column 1 of the encoder grid). Those three controls moved to APC Mini faders 1-3 — see `APC-Mini-Controller.md`. Encoders 1, 9, and 17 are now unused and held dark.

### Row 1 (Top)
| Encoder | Function |
|---------|----------|
| 1 | (unused) |
| 2 | (unused) |
| 3 | Min Pitch |
| 4 | Max Pitch |
| 5 | Min Spectral Centroid |
| 6 | Max Spectral Centroid |
| 7 | (unused) |
| 8 | (unused) |

### Row 2 (Middle)
| Encoder | Function |
|---------|----------|
| 9 | (unused) |
| 10 | (unused) |
| 11 | Min RMS |
| 12 | Max RMS |
| 13 | Min Spectral Crest |
| 14 | Max Spectral Crest |
| 15 | (unused) |
| 16 | (unused) |

### Row 3 (Bottom)
| Encoder | Function |
|---------|----------|
| 17 | (unused) |
| 18 | (unused) |
| 19 | (unused) |
| 20 | (unused) |
| 21 | Min Zero Crossing Rate |
| 22 | Max Zero Crossing Rate |
| 23 | (unused) |
| 24 | (unused) |

---

## Faders

Faders always map to the Intent system:

| Fader | Function |
|-------|----------|
| 1-7 | Intent poles 1-7 (in intent-group order) |
| 8 | Master Intent Strength |

Faders use **value-scaling takeover** (`FaderPickup.h`, shared with the APC Mini and nanoKONTROL2): the movement is scaled against the runway remaining on the side the fader is travelling, so pulling down ducks the parameter immediately and the endpoints (0 / max) self-align. There is no gate to cross — the first message after a config load only records where the fader sits, and every message after that moves the parameter.

---

## OLED Display

### Stationary Display (Permanent)
- **Line 1**: Current config filename
- **Line 2**: Timer — countdown (time remaining, `-` prefix once overrun) when the config has a duration, otherwise config running time
- **Line 3**: Status indicators
  - `REC` - Recording in progress
  - `<n> SAV` - Image saves in progress (count)
  - `REC <n> SAV` - Both active

### Temporary Display (Overlay)
Shows briefly when controls are used:
- **Intent Faders**: "<parameter name>" / "0.123" (rate-limited; `[PICKUP]` tags the one baselining message after a config load, where the fader records its position without moving the parameter)
- **Encoders (audio nudge)**: parameter name and value while turning — `ARM <value>` on first touch or after a pause, `[CLUTCH LO]`/`[CLUTCH HI]` with the exit threshold at the ends of travel, and `<value>  x<mult>` (with a `[MIN]`/`[MAX]` tag at range limits) while nudging

---

## LED Colors

### Button LEDs
| State | Color |
|-------|-------|
| Bottom row (9-16) | Off — retired, see above |
| Intent pole > 0.0 (master > 0.0) | Bright axis hue (coral / cyan / violet / green) |
| Intent pole == 0.0, or master == 0.0 | Dim axis hue |
| Master strength (button 8) | Amber (dim at zero) |
| Button Pressed | White |
| No intent parameters | Off |

### Encoder LEDs
| Encoder | Color |
|---------|-------|
| Pitch (3-4) | Blue / Cyan |
| Spectral (5-6, 13-14) | Purple / Magenta |
| RMS (11-12) | Blue / Cyan |
| Zero Crossing (21-22) | Purple / Magenta |
| Unused | Off |

---

## Technical Notes

- Controller runs in **DAW mode** for LED and display control
- The device is opened once and stays connected across config switches: `MidiController` owns the DAW input port, `ofxLaunchControlXL3Leds` owns the matching DAW output port, and the OLED shares it. Nothing is bound to Synth parameters — faders and encoders resolve them per MIDI message — so a config swap needs no rebinding
- Auto-temp-display is disabled for all faders, encoders, and buttons (only our custom temporary overlays are shown)
- All handled CCs are on MIDI channel 1
- Fader CCs: 5-12; encoder CCs: 13-36 (DAW mode)
- Top row button CCs: 37-44 (intent indicators)
- Bottom row button CCs: 45-52 (unassigned since the snapshot row retired; LEDs stay dark)

---

## Related

- [APC-Mini-Controller.md](APC-Mini-Controller.md) — config grid + synth fader trio
- [NanoKontrol2-Controller.md](NanoKontrol2-Controller.md) — layer alpha/mute + transport
- [Performer guide](../../../../addons/ofxMarkSynth/docs/performer-guide.md) — the live controls these surfaces drive

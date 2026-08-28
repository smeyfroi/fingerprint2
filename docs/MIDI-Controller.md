# MIDI Controller Reference

## Launch Control XL 3 - fingerprint2 Mapping

This document describes the MIDI controller mapping for the Novation Launch Control XL 3 in DAW mode.

**Companion controllers:** fingerprint2 also integrates the Akai APC Mini MK2 (`APC-Mini-Controller.md`) and the Korg nanoKONTROL2 (`NanoKontrol2-Controller.md`). All three can run simultaneously without conflict — each writes to the same shared `ofParameter` targets through pickup/soft-takeover.

---

## Scope

There is no shift mode and no transport handling on this controller any more. Play/Pause, Hibernate, Save Image, and Previous/Next config all live on the Korg nanoKONTROL2, and layer control lives on the APC Mini. The Novation is: faders → Intent, encoders → audio-analysis nudge, top-row intent indicator LEDs, bottom-row snapshot recall.

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

## Bottom Row Buttons (9-16) - Mod Snapshots

| Button | Function |
|--------|----------|
| 9-16 | Load Snapshot 1-8 |

Pressing a button loads that snapshot slot immediately — these buttons are **load only**; there is no save gesture on this controller.

LED behavior:
- **Dim white** = slot holds a saved snapshot
- **Off** = slot is empty
- **Bright white** = while the button is physically held (reverts to the slot's occupancy colour on release)

Occupancy is polled every frame via `Synth::isModSnapshotSlotOccupied`, which lazy-loads the config's snapshot file from disk — so the LEDs are correct even when the GUI's snapshot panel has never been opened (headless), and they follow snapshots being saved or cleared live in the GUI.

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

Faders use **pickup mode** (soft takeover) - you must move the fader past the current parameter value before it takes effect.

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
- **Snapshot**: "Snapshot" / "1-8" on press (shown whether or not the slot is occupied)
- **Intent Faders**: "<parameter name>" / "0.123" (shows `[PICKUP]` until engaged; rate-limited)
- **Encoders (audio nudge)**: parameter name and value while turning — `ARM <value>` on first touch or after a pause, `[CLUTCH LO]`/`[CLUTCH HI]` with the exit threshold at the ends of travel, and `<value>  x<mult>` (with a `[MIN]`/`[MAX]` tag at range limits) while nudging

---

## LED Colors

### Button LEDs
| State | Color |
|-------|-------|
| Snapshot slot occupied (bottom row) | Dim white |
| Snapshot slot empty (bottom row) | Off |
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
- Auto-temp-display is disabled for all faders, encoders, and buttons (only our custom temporary overlays are shown)
- All handled CCs are on MIDI channel 1
- Fader CCs: 5-12; encoder CCs: 13-36 (DAW mode)
- Top row button CCs: 37-44 (intent indicators)
- Bottom row button CCs: 45-52 (mod snapshots)

---

## Related

- [APC-Mini-Controller.md](APC-Mini-Controller.md) — config grid + synth fader trio
- [NanoKontrol2-Controller.md](NanoKontrol2-Controller.md) — layer alpha/mute + transport
- [Performer guide](../../../../addons/ofxMarkSynth/docs/performer-guide.md) — the live controls these surfaces drive

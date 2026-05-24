# MIDI Controller Reference

## Launch Control XL 3 - fingerprint2 Mapping

This document describes the MIDI controller mapping for the Novation Launch Control XL 3 in DAW mode.

**Companion controllers:** fingerprint2 also integrates the Akai APC Mini MK2 (`APC-Mini-Controller.md`) and the Korg nanoKONTROL2 (`NanoKontrol2-Controller.md`). All three can run simultaneously without conflict — each writes to the same shared `ofParameter` targets through pickup/soft-takeover.

---

## Shift Mode

The controller has two modes controlled by the **Shift** button:

| Mode | Top Row Buttons | Bottom Row Buttons | Faders | Play Button | Record Button |
|------|-----------------|--------------------|--------|-------------|---------------|
| **Off** | Intent Indicators | Load Snapshot 1-8 | Intent Parameters | Pause/Play | Save Image |
| **On** | Intent Indicators | Load Snapshot 1-8 | Layer Alpha 1-8 | Hibernate | Save Image |

---

## Transport Buttons

| Button | Shift Off | Shift On |
|--------|-----------|----------|
| **Shift** | Toggle to Shift On | Toggle to Shift Off |
| **Play** | Pause/Play | Hibernate |
| **Record** | Save Image | Save Image |
| **Track Left** | Previous Config | Previous Config |
| **Track Right** | Next Config | Next Config |

---

## Top Row Buttons (1-8) - Intent Indicators

These buttons are **LED indicators only** (button presses do nothing):

| Button | Indicator |
|--------|-----------|
| 1-7 | Intent activation 1-7 |
| 8 | Master intent strength |

LED behavior:
- **Bright yellow** = value > 0.0 and master strength > 0.0
- **Dim yellow** = parameter exists but value == 0.0 (or master strength == 0.0)
- **Off** = no matching intent parameter exists

---

## Bottom Row Buttons (9-16) - Mod Snapshots

| Button | Function |
|--------|----------|
| 9-16 | Load Snapshot 1-8 |

LED behavior:
- **Red** = always on (snapshot buttons always active)
- **White** = flashes while pressed

---

## Encoders (Knobs)

### Row 1 (Top)
| Encoder | Function |
|---------|----------|
| 1 | Synth Agency |
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
| 9 | Audio Agency Response |
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
| 17 | Video Agency Response |
| 18 | (unused) |
| 19 | (unused) |
| 20 | (unused) |
| 21 | Min Zero Crossing Rate |
| 22 | Max Zero Crossing Rate |
| 23 | (unused) |
| 24 | (unused) |

---

## Faders

Faders control different parameters depending on shift mode:

| Fader | Shift Off | Shift On |
|-------|-----------|----------|
| 1-7 | Intent Activation 1-7 | Layer Alpha 1-7 |
| 8 | Master Intent Strength | Layer Alpha 8 |

Faders use **pickup mode** (soft takeover) - you must move the fader past the current parameter value before it takes effect.

---

## OLED Display

### Stationary Display (Permanent)
- **Line 1**: Current config filename
- **Line 2**: Status indicators
  - `REC` - Recording in progress
  - `SAV` - Image save in progress
  - `REC SAV` - Both active

### Temporary Display (Overlay)
Shows briefly when controls are used:
- **Shift**: "Shift" / "On" or "Off"
- **Snapshot**: "Snapshot" / "1-8"
- **Intent Faders (Shift Off)**: "<Intent Name> Activation" / "0.123" (shows `[PICKUP]` until engaged)
- **Layer Faders (Shift On)**: "<Layer Name>" / "0.123" (shows `[PICKUP]` until engaged; includes APC Mini layer faders mirrored to the Novation OLED)
- **Transport**: "Transport" / "Pause/Play" or "Hibernate"

- **Save**: "Action" / "Save Image"
- **Config**: "Config" / "Previous" or "Next"

---

## LED Colors

### Button LEDs
| State | Color |
|-------|-------|
| Snapshot Buttons (bottom row) | Red |
| Intent > 0.0 | Bright yellow |
| Intent == 0.0 (exists) | Dim yellow |
| Button Pressed | White |
| No intent parameter | Off |

### Encoder LEDs
| Encoder | Color |
|---------|-------|
| Agency (1), Audio Resp (9), Video Resp (17) | Red |
| Pitch (3-4) | Blue / Cyan |
| Spectral (5-6, 13-14) | Purple / Magenta |
| RMS (11-12) | Blue / Cyan |
| Zero Crossing (21-22) | Purple / Magenta |
| Unused | Off |

---

## Technical Notes

- Controller runs in **DAW mode** for LED and display control
- Auto-temp-display is disabled for all faders, encoders, and buttons (only our custom temporary overlays are shown)
- All button CCs are on MIDI channel 1, except Shift (channel 7)
- Transport button CCs: Shift=63, Play=116, Record=118, Track Left=103, Track Right=102
- Top row button CCs: 37-44 (intent indicators)
- Bottom row button CCs: 45-52 (mod snapshots)

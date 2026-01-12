# Akai APC Mini MK2 Controller Implementation

## Overview

This document describes the Akai APC Mini MK2 MIDI controller integration with fingerprint2. The APC Mini provides an 8x8 RGB pad grid ideal for visual performance config navigation, plus 9 faders for layer alpha control.

Status: implemented in `src/ApcMiniController.h` and `src/ApcMiniController.cpp`.

**Relationship with existing Launch Control XL:**
- Launch Control XL: Detailed parameter control (encoders for audio analysis, faders for intents/layers, OLED display)
- APC Mini MK2: Visual config grid navigation (56 RGB pads for config selection, 8 RGB pads for layer toggle, faders for layer alphas)

Both controllers can operate simultaneously, each serving a distinct purpose.

**Reference implementation:** JavaScript library at `/Users/steve/Development/opensource/akai-apc-mini-mk2`

---

## Hardware LED Limitations

**IMPORTANT:** The physical buttons on the APC Mini MK2 have single-color LEDs, NOT RGB:

| Button Group | Notes | LED Color | Controllable |
|--------------|-------|-----------|--------------|
| Track Buttons 1-8 | 100-107 (0x64-0x6B) | **RED only** | On/Off/Blink only |
| Scene Launch 1-8 | 112-119 (0x70-0x77) | **GREEN only** | On/Off/Blink only |
| Shift | 122 (0x7A) | None | N/A |
| Pad Grid | 0-63 | **Full RGB** | 128 colors or custom RGB via SysEx |

Because of this limitation, **layer toggle buttons are implemented on the bottom row of the RGB pad grid (notes 0-7)** instead of relying on the physical Track Buttons for feedback. This allows proper visual feedback.

The physical Track Buttons (100-107) are still accepted as alternate layer-toggle inputs, but their red-only LEDs are not suitable as the primary layer feedback channel.

---

## Hardware Reference

### Device Identification

| Property | Value |
|----------|-------|
| Port name pattern | `"APC mini mk2 Contr"` or `"APC mini mk2 MIDI 1"` |
| Manufacturer ID | `0x47` (Akai) |
| Device ID | `0x7F` |
| Model ID | `0x4F` |

### Physical Layout

```
┌─────────────────────────────────────────────────────────┐
│                                                         │
│  [Pad Grid 8x8]                          [Side Buttons] │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┐      ┌─────────────┐ │
│  │56 │57 │58 │59 │60 │61 │62 │63 │ Row8 │ Clip Stop   │ │ Note 112
│  ├───┼───┼───┼───┼───┼───┼───┼───┤      │ (Note 112)  │ │
│  │48 │49 │50 │51 │52 │53 │54 │55 │ Row7 │ Solo        │ │ Note 113
│  ├───┼───┼───┼───┼───┼───┼───┼───┤      │ (Note 113)  │ │
│  │40 │41 │42 │43 │44 │45 │46 │47 │ Row6 │ Mute        │ │ Note 114
│  ├───┼───┼───┼───┼───┼───┼───┼───┤      │ (Note 114)  │ │
│  │32 │33 │34 │35 │36 │37 │38 │39 │ Row5 │ Rec Arm     │ │ Note 115
│  ├───┼───┼───┼───┼───┼───┼───┼───┤      │ (Note 115)  │ │
│  │24 │25 │26 │27 │28 │29 │30 │31 │ Row4 │ Select      │ │ Note 116
│  ├───┼───┼───┼───┼───┼───┼───┼───┤      │ (Note 116)  │ │
│  │16 │17 │18 │19 │20 │21 │22 │23 │ Row3 │ Drum        │ │ Note 117
│  ├───┼───┼───┼───┼───┼───┼───┼───┤      │ (Note 117)  │ │
│  │ 8 │ 9 │10 │11 │12 │13 │14 │15 │ Row2 │ Note        │ │ Note 118
│  ├───┼───┼───┼───┼───┼───┼───┼───┤      │ (Note 118)  │ │
│  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ Row1 │ Stop All    │ │ Note 119
│  └───┴───┴───┴───┴───┴───┴───┴───┘      └─────────────┘ │
│   C1  C2  C3  C4  C5  C6  C7  C8                        │
│                                                         │
│  [Bottom Buttons]                                       │
│  ┌──────┬─────┬──────┬────────┬───┬───┬───┬───┐        │
│  │Volume│ Pan │ Send │ Device │ ▲ │ ▼ │ ◄ │ ► │ [Shift]│
│  │ 100  │ 101 │ 102  │  103   │104│105│106│107│  122   │
│  └──────┴─────┴──────┴────────┴───┴───┴───┴───┘        │
│                                                         │
│  [Faders]                                               │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┬───┐                 │
│  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │                 │
│  │CC │CC │CC │CC │CC │CC │CC │CC │CC │                 │
│  │48 │49 │50 │51 │52 │53 │54 │55 │56 │                 │
│  └───┴───┴───┴───┴───┴───┴───┴───┴───┘                 │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

### MIDI Note/CC Mapping

#### Pad Grid (Notes 0-63)
- **Layout**: 8 columns x 8 rows
- **Note calculation**: `note = x + (7 - y) * 8` where x, y are 0-based
- **Important orientation**: in `fingerprint2`, **`y=0` means the top row** (notes 56–63). The physical bottom row is `y=7` (notes 0–7).
- **Coordinate to note examples**:
  - Top-left (0,0) = Note 56
  - Top-right (7,0) = Note 63
  - Bottom-left (0,7) = Note 0
  - Bottom-right (7,7) = Note 7
- **JS library naming**: `pad11` to `pad88` (1-based, `padXY` where X=column, Y=row from bottom)

**Note-to-coordinate conversion:**
```cpp
// Note to x,y (0-based, y=0 is top row)
int x = note % 8;
int y = 7 - (note / 8);

// x,y to note
int note = x + (7 - y) * 8;
```

#### Side Buttons (Notes 112-119)
| Note | Name | JS Key |
|------|------|--------|
| 112 | Clip Stop | `clipStop` |
| 113 | Solo | `solo` |
| 114 | Mute | `mute` |
| 115 | Rec Arm | `recArm` |
| 116 | Select | `select` |
| 117 | Drum | `drum` |
| 118 | Note | `note` |
| 119 | Stop All Clips | `stopAllClips` |

#### Bottom Buttons (Notes 100-107, 122)
| Note | Name | JS Key |
|------|------|--------|
| 100 | Volume | `volume` |
| 101 | Pan | `pan` |
| 102 | Send | `send` |
| 103 | Device | `device` |
| 104 | Arrow Up | `arrowUp` |
| 105 | Arrow Down | `arrowDown` |
| 106 | Arrow Left | `arrowLeft` |
| 107 | Arrow Right | `arrowRight` |
| 122 | Shift | `shift` |

#### Faders (CC 48-56)
| CC | Index | JS Key |
|----|-------|--------|
| 48 | 0 | `fader0` |
| 49 | 1 | `fader1` |
| 50 | 2 | `fader2` |
| 51 | 3 | `fader3` |
| 52 | 4 | `fader4` |
| 53 | 5 | `fader5` |
| 54 | 6 | `fader6` |
| 55 | 7 | `fader7` |
| 56 | 8 | `fader8` |

In `fingerprint2`, fader mapping is:
- CC 48-55: layer alpha (layers 1-8)
- CC 56 (master): `Synth Agency`

---

## LED Control Protocol

### Indexed Color Mode (Simple, No Sysex)

Send a Note On message where velocity = color index from the 128-color palette.

```cpp
// Set pad to color index
midiOut.sendNoteOn(channel, note, colorIndex);

// Turn pad off
midiOut.sendNoteOn(channel, note, 0);
```

**Brightness/blink control**: The JS library sends `noteOn(note, colorIndex, brightness)` where brightness is the MIDI channel (0-15). Values > 6 enable hardware blinking.

### RGB Color Mode (Requires Sysex)

For full RGB control, use sysex messages:

**Sysex message structure:**
```
F0 47 7F 4F <messageType> <lenMSB> <lenLSB> <data...> F7
│  │  │  │  │             │        │        │         └─ End sysex
│  │  │  │  │             │        │        └─ Payload data
│  │  │  │  │             │        └─ Length LSB (length % 128)
│  │  │  │  │             └─ Length MSB (length / 128)
│  │  │  │  └─ Message type ID
│  │  │  └─ Model ID (APC Mini MK2 = 0x4F)
│  │  └─ Device ID (0x7F = all devices)
│  └─ Manufacturer ID (Akai = 0x47)
└─ Start sysex
```

**Set RGB color (messageType = 0x24):**
```cpp
// Data format for each pad range:
// padFrom, padTo, rMSB, rLSB, gMSB, gLSB, bMSB, bLSB

// Helper to convert 0-255 to MSB/LSB pair:
uint8_t msb = value / 128;
uint8_t lsb = value % 128;

// Example: Set pad 0 to RGB(255, 128, 64)
// Data: [0, 0, 1, 127, 1, 0, 0, 64]
// Full message: F0 47 7F 4F 24 00 08 00 00 01 7F 01 00 00 40 F7
```

**Batch updates**: Can set multiple pad ranges in a single sysex message.

**Reliability note (important):** In practice, large RGB SysEx payloads and/or rapid back-to-back SysEx bursts can be dropped or only partially applied by the APC Mini MK2 (or the OS MIDI stack). This shows up as stale LEDs surviving a clear, or config pads failing to repaint.

To keep LED updates reliable in fingerprint2:
- Split RGB updates into small chunks (currently `8` pads per SysEx).
- Pace chunks slightly (currently `ofSleepMillis(1)` between chunks).
- When loading a new performance/config layout, clear all pads first, then repaint.
- If an individual pad gets "stuck" off, the safest recovery is to reload the performance/config (or restart the app). If the device appears to stop responding to RGB updates entirely, power-cycle the APC Mini MK2.

**Read fader positions (messageType = 0x60):**
```cpp
// Request: F0 47 7F 4F 60 00 04 41 09 01 04 F7
// Response comes as sysex with messageType = 0x61 containing 9 fader values
```

### 128-Color Indexed Palette

```cpp
// colors.js palette - index to hex color
const uint32_t kApcColorPalette[128] = {
  0x000000, // 0 - Black (off)
  0x1E1E1E, // 1 - Dark gray
  0x7F7F7F, // 2 - Gray
  0xFFFFFF, // 3 - White
  0xFF4C4C, // 4 - Light red
  0xFF0000, // 5 - Red
  0x590000, // 6 - Dark red
  0x190000, // 7 - Very dark red
  0xFFBD6C, // 8 - Light orange
  0xFF5400, // 9 - Orange
  0x591D00, // 10 - Dark orange
  0x271B00, // 11 - Very dark orange
  0xFFFF4C, // 12 - Light yellow
  0xFFFF00, // 13 - Yellow
  0x595900, // 14 - Dark yellow
  0x191900, // 15 - Very dark yellow
  0x88FF4C, // 16 - Light lime
  0x54FF00, // 17 - Lime
  0x1D5900, // 18 - Dark lime
  0x142B00, // 19 - Very dark lime
  0x4CFF4C, // 20 - Light green
  0x00FF00, // 21 - Green
  0x005900, // 22 - Dark green
  0x001900, // 23 - Very dark green
  0x4CFF5E, // 24 - Light spring green
  0x00FF19, // 25 - Spring green
  0x00590D, // 26 - Dark spring green
  0x001902, // 27 - Very dark spring green
  0x4CFF88, // 28 - Light cyan-green
  0x00FF55, // 29 - Cyan-green
  0x00591D, // 30 - Dark cyan-green
  0x001F12, // 31 - Very dark cyan-green
  0x4CFFB7, // 32 - Light aqua
  0x00FF99, // 33 - Aqua
  0x005935, // 34 - Dark aqua
  0x001912, // 35 - Very dark aqua
  0x4CC3FF, // 36 - Light sky blue
  0x00A9FF, // 37 - Sky blue
  0x004152, // 38 - Dark sky blue
  0x001019, // 39 - Very dark sky blue
  0x4C88FF, // 40 - Light blue
  0x0055FF, // 41 - Blue
  0x001D59, // 42 - Dark blue
  0x000819, // 43 - Very dark blue
  0x4C4CFF, // 44 - Light indigo
  0x0000FF, // 45 - Indigo
  0x000059, // 46 - Dark indigo
  0x000019, // 47 - Very dark indigo
  0x874CFF, // 48 - Light purple
  0x5400FF, // 49 - Purple
  0x190064, // 50 - Dark purple
  0x0F0030, // 51 - Very dark purple
  0xFF4CFF, // 52 - Light magenta
  0xFF00FF, // 53 - Magenta
  0x590059, // 54 - Dark magenta
  0x190019, // 55 - Very dark magenta
  0xFF4C87, // 56 - Light pink
  0xFF0054, // 57 - Pink
  0x59001D, // 58 - Dark pink
  0x220013, // 59 - Very dark pink
  0xFF1500, // 60 - Red-orange
  0x993500, // 61 - Brown
  0x795100, // 62 - Olive
  0x436400, // 63 - Olive green
  0x033900, // 64 - Forest green
  0x005735, // 65 - Teal
  0x00547F, // 66 - Steel blue
  0x0000FF, // 67 - Blue (duplicate)
  0x00454F, // 68 - Dark teal
  0x2500CC, // 69 - Blue-purple
  0x7F7F7F, // 70 - Gray (duplicate)
  0x202020, // 71 - Darker gray
  0xFF0000, // 72 - Red (duplicate)
  0xBDFF2D, // 73 - Yellow-green
  0xAFED06, // 74 - Lime yellow
  0x64FF09, // 75 - Bright lime
  0x108B00, // 76 - Medium green
  0x00FF87, // 77 - Spring cyan
  0x00A9FF, // 78 - Sky blue (duplicate)
  0x002AFF, // 79 - Royal blue
  0x3F00FF, // 80 - Blue-indigo
  0x7A00FF, // 81 - Violet
  0xB21A7D, // 82 - Rose
  0x402100, // 83 - Dark brown
  0xFF4A00, // 84 - Orange-red
  0x88E106, // 85 - Yellow-lime
  0x72FF15, // 86 - Bright lime green
  0x00FF00, // 87 - Green (duplicate)
  0x3BFF26, // 88 - Light green
  0x59FF71, // 89 - Mint green
  0x38FFCC, // 90 - Turquoise
  0x5B8AFF, // 91 - Cornflower blue
  0x3151C6, // 92 - Medium blue
  0x877FE9, // 93 - Lavender
  0xD31DFF, // 94 - Bright purple
  0xFF005D, // 95 - Hot pink
  0xFF7F00, // 96 - Amber
  0xB9B000, // 97 - Yellow-olive
  0x90FF00, // 98 - Chartreuse
  0x835D07, // 99 - Tan
  0x392B00, // 100 - Dark brown
  0x144C10, // 101 - Dark green
  0x0D5038, // 102 - Dark teal
  0x15152A, // 103 - Navy
  0x16205A, // 104 - Dark navy
  0x693C1C, // 105 - Rust
  0xA8000A, // 106 - Dark red
  0xDE513D, // 107 - Coral
  0xD86A1C, // 108 - Dark orange
  0xFFE126, // 109 - Gold
  0x9EE12F, // 110 - Yellow-green
  0x67B50F, // 111 - Olive lime
  0x1E1E30, // 112 - Dark blue-gray
  0xDCFF6B, // 113 - Pale yellow
  0x80FFBD, // 114 - Pale green
  0x9A99FF, // 115 - Pale blue
  0x8E66FF, // 116 - Pale purple
  0x404040, // 117 - Medium gray
  0x757575, // 118 - Light gray
  0xE0FFFF, // 119 - Pale cyan
  0xA00000, // 120 - Dark red
  0x350000, // 121 - Very dark red
  0x1AD000, // 122 - Bright green
  0x074200, // 123 - Dark green
  0xB9B000, // 124 - Olive (duplicate)
  0x3F3100, // 125 - Dark olive
  0xB35F00, // 126 - Burnt orange
  0x4B1502, // 127 - Dark rust
};
```

**Useful color indices for our application:**
| Purpose | Index | Color | Hex |
|---------|-------|-------|-----|
| Off/Empty | 0 | Black | `#000000` |
| Available config (dim) | 1 | Dark gray | `#1E1E1E` |
| Available config | 2 | Gray | `#7F7F7F` |
| Current config | 21 | Green | `#00FF00` |
| Hold progress | 13 | Yellow | `#FFFF00` |
| Current + playing | 87 | Green | `#00FF00` |
| Paused/Hibernated | 9 | Orange | `#FF5400` |
| Error | 5 | Red | `#FF0000` |

---

## Feature Mapping

### Pad Grid Layout

The 8x8 pad grid is divided into two sections:

```
┌────┬────┬────┬────┬────┬────┬────┬────┐
│ 56 │ 57 │ 58 │ 59 │ 60 │ 61 │ 62 │ 63 │ Config Row 7
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 48 │ 49 │ 50 │ 51 │ 52 │ 53 │ 54 │ 55 │ Config Row 6
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 40 │ 41 │ 42 │ 43 │ 44 │ 45 │ 46 │ 47 │ Config Row 5
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 32 │ 33 │ 34 │ 35 │ 36 │ 37 │ 38 │ 39 │ Config Row 4
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 24 │ 25 │ 26 │ 27 │ 28 │ 29 │ 30 │ 31 │ Config Row 3
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 16 │ 17 │ 18 │ 19 │ 20 │ 21 │ 22 │ 23 │ Config Row 2
├────┼────┼────┼────┼────┼────┼────┼────┤
│  8 │  9 │ 10 │ 11 │ 12 │ 13 │ 14 │ 15 │ Config Row 1
├────┼────┼────┼────┼────┼────┼────┼────┤
│ L1 │ L2 │ L3 │ L4 │ L5 │ L6 │ L7 │ L8 │ LAYER TOGGLE (notes 0-7)
└────┴────┴────┴────┴────┴────┴────┴────┘
```

- **Bottom row (notes 0-7)**: Layer pause toggle buttons (RGB, using white dim/bright)
- **Rows 1-7 (notes 8-63)**: Config selection grid (56 slots for configs)

### Layer Toggle Row (Bottom Row, Notes 0-7)

The bottom row of the pad grid is dedicated to layer pause toggle buttons.

| Pad | Note | Function | LED Color |
|-----|------|----------|-----------|
| L1 | 0 | Toggle Layer 1 pause | Bright white (active) / Dim white (paused) / Off (no layer) |
| L2 | 1 | Toggle Layer 2 pause | Bright white (active) / Dim white (paused) / Off (no layer) |
| L3 | 2 | Toggle Layer 3 pause | Bright white (active) / Dim white (paused) / Off (no layer) |
| L4 | 3 | Toggle Layer 4 pause | Bright white (active) / Dim white (paused) / Off (no layer) |
| L5 | 4 | Toggle Layer 5 pause | Bright white (active) / Dim white (paused) / Off (no layer) |
| L6 | 5 | Toggle Layer 6 pause | Bright white (active) / Dim white (paused) / Off (no layer) |
| L7 | 6 | Toggle Layer 7 pause | Bright white (active) / Dim white (paused) / Off (no layer) |
| L8 | 7 | Toggle Layer 8 pause | Bright white (active) / Dim white (paused) / Off (no layer) |

### Config Grid (Rows 1-7, Notes 8-63)

Each pad in rows 1-7 represents one performance config (up to 56 configs).

**Grid mapping source of truth:** the resolved config grid lives in `ofxMarkSynth::PerformanceNavigator`.
- `PerformanceNavigator::loadFromFolder()` parses each config JSON once (on load) and extracts `buttonGrid` metadata.
- Both the `ofxMarkSynth` ImGui pad-grid UI and the APC controller read from the same resolved mapping.

**Config JSON schema:**
- `buttonGrid.x`: `0..7`
- `buttonGrid.y`: `0..6` (7 config rows; `y=0` is top row)
- `buttonGrid.color`: `"#RRGGBB"`
- `buttonGrid.y=7` is the physical bottom row (notes 0-7) and reserved for layer toggles; it will be treated as invalid and auto-assigned.

**Auto-assignment:** missing/out-of-range/conflicting `buttonGrid` entries are auto-assigned in row-major order, **top-left → bottom-right**.

**LED states:**
| State | Color | Notes |
|-------|-------|-------|
| No config at this slot | Off | |
| Config available (not current) | Dim version of config color | Uses `kConfigDimFactor` (`0.20`) |
| Current config | Full config color | Current config is full-strength (even when hibernated) |
| Hold-to-confirm in progress | Amber | Overrides everything while held |

**Interaction:**
1. Press and hold a pad to initiate jump to that config
2. Hold for 400ms (same as `PerformanceNavigator::HOLD_THRESHOLD_MS`)
3. LED turns amber while held
4. On release before threshold: cancel
5. On threshold reached: trigger `PerformanceNavigator::jumpTo(index)`

### Faders: Layer Alpha Control

Faders 0-7 control layer alphas, matching the Launch Control XL's Shift mode.

| Fader | CC | Function |
|-------|-------|----------|
| 0 | 48 | Layer 1 alpha |
| 1 | 49 | Layer 2 alpha |
| 2 | 50 | Layer 3 alpha |
| 3 | 51 | Layer 4 alpha |
| 4 | 52 | Layer 5 alpha |
| 5 | 53 | Layer 6 alpha |
| 6 | 54 | Layer 7 alpha |
| 7 | 55 | Layer 8 alpha |
| 8 | 56 | (Reserved for future use) |

**Implementation notes:**
- Use pickup/soft-takeover mode to avoid parameter jumps
- Bind to `Synth::getLayerAlphaParameters()` (same as Launch Control XL)
- Fader 8 (CC 56) is reserved but not mapped initially

### Side Buttons (Notes 112-119)

Currently unused in fingerprint2.
- They are cleared/dimmed as part of `ApcMiniController::clearAllLeds()` / `dimInactiveControls()`.
- (Future) Could be mapped to snapshot slots or other functions.

### Track Buttons (Notes 100-107)

The physical Track Buttons have **RED-only** LEDs (no RGB).

In fingerprint2:
- Notes `106`/`107` (◄/►) are used as alternate prev/next config inputs (they forward to `OF_KEY_LEFT`/`OF_KEY_RIGHT`, so hold-to-confirm behavior comes from `PerformanceNavigator`).
- Other track buttons are currently unused.

Primary layer control + feedback remains on the **RGB pad grid bottom row** (notes 0-7).

---

## Implementation Phases

### Phase 1: Basic Setup & Connection
- [x] Create `ApcMiniController.h` and `ApcMiniController.cpp`
- [x] Add device detection using ofxMidi (port name matching)
- [x] Implement connect/disconnect handling
- [x] Add to `ofApp` lifecycle (setup, update, exit)
- [x] Test basic MIDI input/output

### Phase 2: LED Control
- [x] Implement indexed LED control (Note On velocity) for non-RGB buttons
- [x] Implement RGB pad LED control via SysEx (`setPadRgb`/`setPadRgbBatch`)
- [x] Add reliability measures (chunked SysEx sends, retry window, held-pad amber refresh)
- [x] Track and batch LED state updates (per-pad caching + queued updates)

### Phase 3: Config Grid Navigation
- [x] Map pad notes (0-63) to performance grid coordinates
- [x] Implement hold-to-confirm for pad presses
- [x] Connect to `PerformanceNavigator::jumpTo()`
- [x] Implement LED feedback showing:
  - Available configs (from `PerformanceNavigator::getGridConfigIndex(x, y)`)
  - Current config (from `PerformanceNavigator::getCurrentIndex()`)
  - Hold progress (amber)
- [x] Resolve grid mapping inside `ofxMarkSynth::PerformanceNavigator` so GUI + APC share one mapping

### Phase 4: Fader Layer Control
- [x] Map CC 48-55 to layer alpha parameters
- [x] Implement pickup/soft-takeover mode
- [x] Connect to `Synth::getLayerAlphaParameters()`
- [x] Handle Synth reload (rebind parameters on config change)

### Phase 5: Side Buttons
- [ ] (Optional) Map notes 112-119 to snapshot slots or other functions

### Phase 6: Arrow Button Navigation
- [x] Map notes 106-107 to prev/next config (forward to `OF_KEY_LEFT`/`OF_KEY_RIGHT`)
- [x] Use `PerformanceNavigator` hold-to-confirm via key press/release

### Phase 7: Polish & Integration
- [x] Handle hot-plug/unplug of controller
- [x] Add graceful degradation if controller not present
- [x] Coordinate with Launch Control XL (avoid conflicts)

---

## Implementation Notes

- Implementation: `src/ApcMiniController.h`, `src/ApcMiniController.cpp`
- Config grid mapping: `ofxMarkSynth::PerformanceNavigator` is the source of truth (shared with the `ofxMarkSynth` ImGui pad-grid UI)
- Pads (notes 8-63): RGB via SysEx, hold-to-confirm (amber while held)
- Layers (notes 0-7): toggle pause slots with bright/dim white
- Track buttons (notes 100-107): only 106/107 (◄/►) used as prev/next inputs; the rest are unused
- Side buttons (notes 112-119) and Shift (122): currently unused/dimmed

## References

- JavaScript reference implementation: `/Users/steve/Development/opensource/akai-apc-mini-mk2`
- Existing Launch Control XL implementation: `src/MidiController.cpp`
- PerformanceNavigator API: `addons/ofxMarkSynth/src/config/PerformanceNavigator.hpp`
- LayerController API: `addons/ofxMarkSynth/src/controller/LayerController.hpp`
- ofxMidi documentation: https://github.com/danomatika/ofxMidi

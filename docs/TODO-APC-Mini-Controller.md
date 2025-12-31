# TODO: Akai APC Mini MK2 Controller Implementation

## Overview

This document describes the implementation plan for integrating the Akai APC Mini MK2 MIDI controller with fingerprint2. The APC Mini provides an 8x8 RGB pad grid ideal for visual config playlist navigation, plus 9 faders for layer alpha control.

**Relationship with existing Launch Control XL:**
- Launch Control XL: Detailed parameter control (encoders for audio analysis, faders for intents/layers, OLED display)
- APC Mini MK2: Visual config grid navigation (64 RGB pads for config selection, faders for layer alphas)

Both controllers can operate simultaneously, each serving a distinct purpose.

**Reference implementation:** JavaScript library at `/Users/steve/Development/opensource/akai-apc-mini-mk2`

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
- **Coordinate to note examples**:
  - Bottom-left (0,0) = Note 56
  - Bottom-right (7,0) = Note 63
  - Top-left (0,7) = Note 0
  - Top-right (7,7) = Note 7
- **JS library naming**: `pad11` to `pad88` (1-based, `padXY` where X=column, Y=row from bottom)

**Note-to-coordinate conversion:**
```cpp
// Note to x,y (0-based, y=0 is bottom row)
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

**Batch updates**: Can set multiple pad ranges in a single sysex message. Recommended batch size is 32 pads to avoid dropped frames.

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

### Pad Grid: Config Playlist Navigation

Each pad represents one config file from the PerformanceNavigator playlist (up to 64 configs).

**Grid layout** (reading order matches file sort order):
```
Config indices (left-to-right, bottom-to-top like Ableton):
┌────┬────┬────┬────┬────┬────┬────┬────┐
│ 56 │ 57 │ 58 │ 59 │ 60 │ 61 │ 62 │ 63 │ Row 8
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 48 │ 49 │ 50 │ 51 │ 52 │ 53 │ 54 │ 55 │ Row 7
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 40 │ 41 │ 42 │ 43 │ 44 │ 45 │ 46 │ 47 │ Row 6
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 32 │ 33 │ 34 │ 35 │ 36 │ 37 │ 38 │ 39 │ Row 5
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 24 │ 25 │ 26 │ 27 │ 28 │ 29 │ 30 │ 31 │ Row 4
├────┼────┼────┼────┼────┼────┼────┼────┤
│ 16 │ 17 │ 18 │ 19 │ 20 │ 21 │ 22 │ 23 │ Row 3
├────┼────┼────┼────┼────┼────┼────┼────┤
│  8 │  9 │ 10 │ 11 │ 12 │ 13 │ 14 │ 15 │ Row 2
├────┼────┼────┼────┼────┼────┼────┼────┤
│  0 │  1 │  2 │  3 │  4 │  5 │  6 │  7 │ Row 1
└────┴────┴────┴────┴────┴────┴────┴────┘
```

**Note**: Config index maps directly to MIDI note number for simplicity.

**LED states:**
| State | Color | Index | Animation |
|-------|-------|-------|-----------|
| No config at this slot | Off | 0 | None |
| Config available | Dim gray | 1 | None |
| Current config (playing) | Bright green | 21 | None |
| Current config (paused) | Orange | 9 | None |
| Current config (hibernated) | Dim orange | 10 | Slow pulse |
| Hold-to-confirm in progress | Yellow | 13 | Pulse (progress) |
| Config loading | White | 3 | Blink |

**Interaction:**
1. Press and hold a pad to initiate jump to that config
2. Hold for 400ms (same as PerformanceNavigator.HOLD_THRESHOLD_MS)
3. LED pulses during hold to show progress
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

### Side Buttons: Snapshot Slots

| Button | Note | Function |
|--------|------|----------|
| Clip Stop | 112 | Load Snapshot 1 |
| Solo | 113 | Load Snapshot 2 |
| Mute | 114 | Load Snapshot 3 |
| Rec Arm | 115 | Load Snapshot 4 |
| Select | 116 | Load Snapshot 5 |
| Drum | 117 | Load Snapshot 6 |
| Note | 118 | Load Snapshot 7 |
| Stop All Clips | 119 | Load Snapshot 8 |

**LED feedback:**
- Off when snapshot slot is empty
- On (bright) when snapshot is available
- Blink briefly on press

### Bottom Buttons: Navigation & Utility

| Button | Note | Function |
|--------|------|----------|
| Volume | 100 | (Reserved - could toggle fader mode) |
| Pan | 101 | (Reserved) |
| Send | 102 | (Reserved) |
| Device | 103 | (Reserved) |
| Arrow Up | 104 | (Reserved) |
| Arrow Down | 105 | (Reserved) |
| Arrow Left | 106 | Previous config (hold-to-confirm) |
| Arrow Right | 107 | Next config (hold-to-confirm) |
| Shift | 122 | Modifier key |

**Arrow Left/Right behavior:**
- Same hold-to-confirm as pad grid
- Calls `PerformanceNavigator::beginHold(PREV/NEXT, MIDI_SOURCE)`
- On release: `PerformanceNavigator::endHold(MIDI_SOURCE)`

---

## Implementation Phases

### Phase 1: Basic Setup & Connection
- [ ] Create `ApcMiniController.h` and `ApcMiniController.cpp`
- [ ] Add device detection using ofxMidi (port name matching)
- [ ] Implement connect/disconnect handling
- [ ] Add to `ofApp` lifecycle (setup, update, exit)
- [ ] Test basic MIDI input/output

### Phase 2: LED Control
- [ ] Implement indexed color LED control (Note On messages)
- [ ] Implement RGB color LED control via sysex (optional, for richer visuals)
- [ ] Create LED state management (track current colors, batch updates)
- [ ] Add helper functions for common LED patterns (pulse, blink)
- [ ] Test LED feedback with manual color setting

### Phase 3: Config Grid Navigation
- [ ] Map pad notes (0-63) to config indices
- [ ] Implement hold-to-confirm for pad presses
- [ ] Connect to `PerformanceNavigator::jumpTo()`
- [ ] Implement LED feedback showing:
  - Available configs (from `PerformanceNavigator::getConfigs()`)
  - Current config (from `PerformanceNavigator::getCurrentIndex()`)
  - Hold progress animation
- [ ] Handle config load/unload events to refresh grid

### Phase 4: Fader Layer Control
- [ ] Map CC 48-55 to layer alpha parameters
- [ ] Implement pickup/soft-takeover mode
- [ ] Connect to `Synth::getLayerAlphaParameters()`
- [ ] Handle Synth reload (rebind parameters on config change)

### Phase 5: Side Buttons (Snapshots)
- [ ] Map notes 112-119 to snapshot slots
- [ ] Implement momentary press handling
- [ ] Connect to `Synth::loadModSnapshotSlot()`
- [ ] Add LED feedback for available snapshots

### Phase 6: Arrow Button Navigation
- [ ] Map notes 106-107 to prev/next config
- [ ] Implement hold-to-confirm matching `PerformanceNavigator`
- [ ] Add LED feedback during hold

### Phase 7: Polish & Integration
- [ ] Handle hot-plug/unplug of controller
- [ ] Add graceful degradation if controller not present
- [ ] Coordinate with Launch Control XL (avoid conflicts)
- [ ] Test full workflow with real performance configs

---

## Class Design

### Header: `ApcMiniController.h`

```cpp
#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "ofxMidi.h"
#include "ofxMarkSynth.h"

class ApcMiniController : public ofxMidiListener {
public:
  // === MIDI Constants ===
  
  // Device identification
  static constexpr const char* kDeviceNamePattern1 = "APC mini mk2 Contr";
  static constexpr const char* kDeviceNamePattern2 = "APC mini mk2 MIDI 1";
  
  // Sysex identifiers
  static constexpr uint8_t kManufacturerId = 0x47;  // Akai
  static constexpr uint8_t kDeviceId = 0x7F;
  static constexpr uint8_t kModelId = 0x4F;
  
  // Pad grid notes (0-63)
  static constexpr int kPadNoteFirst = 0;
  static constexpr int kPadNoteLast = 63;
  static constexpr int kPadCount = 64;
  
  // Side button notes (112-119)
  static constexpr int kSideButtonNoteFirst = 112;
  static constexpr int kSideButtonNoteLast = 119;
  static constexpr int kSideButtonCount = 8;
  
  // Bottom button notes
  static constexpr int kVolumeButtonNote = 100;
  static constexpr int kPanButtonNote = 101;
  static constexpr int kSendButtonNote = 102;
  static constexpr int kDeviceButtonNote = 103;
  static constexpr int kArrowUpNote = 104;
  static constexpr int kArrowDownNote = 105;
  static constexpr int kArrowLeftNote = 106;
  static constexpr int kArrowRightNote = 107;
  static constexpr int kShiftButtonNote = 122;
  
  // Fader CCs (48-56)
  static constexpr int kFaderCCFirst = 48;
  static constexpr int kFaderCCLast = 56;
  static constexpr int kFaderCount = 9;
  
  // Timing
  static constexpr uint64_t kHoldThresholdMs = 400;  // Match PerformanceNavigator
  
  // === Color Indices (from APC palette) ===
  static constexpr uint8_t kColorOff = 0;
  static constexpr uint8_t kColorDimGray = 1;
  static constexpr uint8_t kColorGray = 2;
  static constexpr uint8_t kColorWhite = 3;
  static constexpr uint8_t kColorGreen = 21;
  static constexpr uint8_t kColorYellow = 13;
  static constexpr uint8_t kColorOrange = 9;
  static constexpr uint8_t kColorDimOrange = 10;
  static constexpr uint8_t kColorRed = 5;

  ApcMiniController();
  ~ApcMiniController();
  
  // Lifecycle
  bool setup();  // Returns true if device found
  void update();
  void exit();
  
  // Synth connection
  void onSynthDidLoad(const std::shared_ptr<ofxMarkSynth::Synth>& synthPtr);
  void onSynthWillUnload();
  
  // ofxMidiListener
  void newMidiMessage(ofxMidiMessage& message) override;
  
  // State queries
  bool isConnected() const { return connected; }

private:
  // MIDI I/O
  ofxMidiIn midiIn;
  ofxMidiOut midiOut;
  bool connected = false;
  
  // Synth reference
  std::shared_ptr<ofxMarkSynth::Synth> synthPtr;
  
  // === LED State ===
  std::array<uint8_t, kPadCount> padColors;
  std::array<uint8_t, kSideButtonCount> sideButtonColors;
  
  // === Hold State ===
  struct HoldState {
    bool active = false;
    int note = -1;
    uint64_t startTimeMs = 0;
  };
  HoldState currentHold;
  
  // === Fader State ===
  struct FaderState {
    float lastValue = -1.0f;  // For pickup mode
    bool pickedUp = false;
  };
  std::array<FaderState, kFaderCount> faderStates;
  
  // === Message Handling ===
  void handleNoteOn(int note, int velocity);
  void handleNoteOff(int note);
  void handleCC(int cc, int value);
  
  // === Pad Grid ===
  void updatePadGrid();
  void onPadPressed(int padIndex);
  void onPadReleased(int padIndex);
  int padNoteToConfigIndex(int note) const { return note; }  // Direct mapping
  int configIndexToPadNote(int index) const { return index; }
  
  // === Side Buttons ===
  void onSideButtonPressed(int buttonIndex);
  void updateSideButtonLeds();
  
  // === Arrow Buttons ===
  void onArrowLeftPressed();
  void onArrowLeftReleased();
  void onArrowRightPressed();
  void onArrowRightReleased();
  
  // === Faders ===
  void onFaderMoved(int faderIndex, float normalizedValue);
  void bindFadersToLayerAlphas();
  void unbindFaders();
  
  // === LED Control ===
  void setPadColor(int note, uint8_t colorIndex);
  void setSideButtonColor(int buttonIndex, uint8_t colorIndex);
  void setAllPadsColor(uint8_t colorIndex);
  void sendNoteOn(int note, int velocity);
  
  // === Sysex (for RGB mode, optional) ===
  void sendSysex(uint8_t messageType, const std::vector<uint8_t>& data);
  void setPadRgb(int note, uint8_t r, uint8_t g, uint8_t b);
  
  // === Utility ===
  static std::pair<uint8_t, uint8_t> toMsbLsb(uint8_t value);
};
```

---

## Integration Points

### With Synth

```cpp
// In ofApp::setup() or when Synth is created:
apcMiniController.onSynthDidLoad(synthPtr);

// In ofApp::exit() or before Synth is destroyed:
apcMiniController.onSynthWillUnload();

// In ofApp::update():
apcMiniController.update();
```

### With PerformanceNavigator

```cpp
// Access via Synth:
auto& nav = synthPtr->getPerformanceNavigator();

// Get config list:
const auto& configs = nav.getConfigs();
int configCount = nav.getConfigCount();

// Get current state:
int currentIndex = nav.getCurrentIndex();
bool hasConfigs = nav.hasConfigs();

// Navigate (after hold-to-confirm):
nav.jumpTo(index);

// For arrow buttons, use the existing hold system:
// We'll need to add a new HoldSource for APC Mini
nav.beginHold(PerformanceNavigator::HoldAction::NEXT, PerformanceNavigator::HoldSource::APC_MINI);
nav.endHold(PerformanceNavigator::HoldSource::APC_MINI);
```

**Required modification to PerformanceNavigator:**
Add `APC_MINI` to `HoldSource` enum:
```cpp
enum class HoldSource { NONE, KEYBOARD, MOUSE, APC_MINI };
```

### With LayerController

```cpp
// Access layer alpha parameters:
ofParameterGroup& alphas = synthPtr->getLayerAlphaParameters();

// Get individual parameter:
ofParameter<float>& layerAlpha = alphas.getFloat(layerIndex);

// Bind with pickup:
// Store last known value, only update parameter when fader crosses it
```

### With ModSnapshotManager

```cpp
// Load snapshot:
synthPtr->loadModSnapshotSlot(slotIndex);  // 0-7
```

---

## ofxMidi Integration Notes

### Device Detection

```cpp
bool ApcMiniController::setup() {
  // List available devices
  midiIn.listInPorts();
  midiOut.listOutPorts();
  
  // Find APC Mini by name
  int inPort = -1, outPort = -1;
  
  for (int i = 0; i < midiIn.getNumInPorts(); i++) {
    std::string name = midiIn.getInPortName(i);
    if (name.find(kDeviceNamePattern1) != std::string::npos ||
        name.find(kDeviceNamePattern2) != std::string::npos) {
      inPort = i;
      break;
    }
  }
  
  for (int i = 0; i < midiOut.getNumOutPorts(); i++) {
    std::string name = midiOut.getOutPortName(i);
    if (name.find(kDeviceNamePattern1) != std::string::npos ||
        name.find(kDeviceNamePattern2) != std::string::npos) {
      outPort = i;
      break;
    }
  }
  
  if (inPort < 0 || outPort < 0) {
    ofLogWarning("ApcMiniController") << "Device not found";
    return false;
  }
  
  midiIn.openPort(inPort);
  midiIn.addListener(this);
  midiOut.openPort(outPort);
  
  connected = true;
  return true;
}
```

### Sending MIDI

```cpp
void ApcMiniController::sendNoteOn(int note, int velocity) {
  if (!connected) return;
  midiOut.sendNoteOn(1, note, velocity);  // Channel 1
}

void ApcMiniController::sendSysex(uint8_t messageType, const std::vector<uint8_t>& data) {
  if (!connected) return;
  
  auto [lenMsb, lenLsb] = toMsbLsb(static_cast<uint8_t>(data.size()));
  
  std::vector<uint8_t> message;
  message.push_back(0xF0);  // Sysex start
  message.push_back(kManufacturerId);
  message.push_back(kDeviceId);
  message.push_back(kModelId);
  message.push_back(messageType);
  message.push_back(lenMsb);
  message.push_back(lenLsb);
  message.insert(message.end(), data.begin(), data.end());
  message.push_back(0xF7);  // Sysex end
  
  midiOut.sendMidiBytes(message);
}
```

---

## Open Questions

To be resolved when hardware arrives:

1. **RGB vs Indexed colors**: Should we use sysex RGB for richer visuals, or stick with indexed colors for simplicity? RGB allows smooth animations but requires more code.

2. **Fader pickup sensitivity**: What threshold feels right for soft-takeover? (e.g., must fader come within 5% of current value before taking control)

3. **Animation frame rate**: For pulsing LEDs during hold-to-confirm, what update rate works well? (Start with 30fps)

4. **Multiple pages**: If more than 64 configs, should we implement page navigation using arrow up/down buttons?

5. **Shift button behavior**: What should Shift modify? Ideas:
   - Shift + Pad = immediate jump (no hold required)
   - Shift + Side button = toggle layer pause instead of snapshot load
   - Shift + Arrow = skip 8 configs at a time

6. **Coordination with Launch Control XL**: Both controllers can control layer alphas. Should they:
   - Mirror each other (changes on one reflected on other's faders)?
   - Operate independently (last touch wins)?
   - Have the APC Mini defer to Launch Control XL when shift mode is active?

---

## Testing Checklist

### Phase 1: Connection
- [ ] Device detected on USB connect
- [ ] Device releases cleanly on disconnect
- [ ] Hot-plug works (reconnect after unplug)
- [ ] No crashes when device not present

### Phase 2: LEDs
- [ ] All 64 pads can be lit
- [ ] All side buttons can be lit
- [ ] Colors match expected palette
- [ ] Reset (all off) works

### Phase 3: Config Grid
- [ ] Pads show available configs
- [ ] Current config is highlighted
- [ ] Hold-to-confirm triggers at 400ms
- [ ] Early release cancels
- [ ] Config actually changes
- [ ] Grid updates after config change

### Phase 4: Faders
- [ ] Faders control layer alphas
- [ ] Pickup mode prevents jumps
- [ ] Works after config reload

### Phase 5: Snapshots
- [ ] Side buttons load snapshots
- [ ] LEDs reflect snapshot availability

### Phase 6: Arrow Navigation
- [ ] Left arrow = previous config
- [ ] Right arrow = next config
- [ ] Hold-to-confirm works
- [ ] Blocked at boundaries (first/last config)

---

## References

- JavaScript reference implementation: `/Users/steve/Development/opensource/akai-apc-mini-mk2`
- Existing Launch Control XL implementation: `src/MidiController.cpp`
- PerformanceNavigator API: `addons/ofxMarkSynth/src/config/PerformanceNavigator.hpp`
- LayerController API: `addons/ofxMarkSynth/src/controller/LayerController.hpp`
- ofxMidi documentation: https://github.com/danomatika/ofxMidi

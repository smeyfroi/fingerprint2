# TouchOSC iPad control surface

`sharksynth.tosc` is the iPad control surface for driving MarkSynth over OSC,
alongside the MIDI controllers and the GUI. It pairs with
[`../src/OscController.h`](../src/OscController.h) /
[`.cpp`](../src/OscController.cpp).

Open it in **TouchOSC** (Hexler, the modern `.tosc` app) on the iPad. The file is
a single zlib-compressed XML document — to edit it, open it in the TouchOSC
editor, change it there, and re-export over the top.

## Connection (set in TouchOSC → Connections → OSC, slot 1)

- Protocol **UDP**
- **Host** = the Mac's LAN IP
- **Send Port `8000`** (iPad → Mac; `OscController` listens here)
- **Receive Port `9000`** (Mac → iPad; `OscController` echoes state here)

Both devices on the same Wi-Fi; the app needs the
`com.apple.security.network.{server,client}` entitlements (already set).

## Address map

All control values are normalised **0..1** on the wire; `OscController` scales
them to each parameter's real range (and back), exactly like the MIDI
controllers. Every interactive widget is **Send + Receive**, feedback off.

| Address | Dir | Target |
|---|---|---|
| `/layer/<i>/alpha` | ⇄ | layer `i` composite alpha (`i` = 0..6) |
| `/layer/<i>/pause` | ⇄ | layer `i` pause toggle |
| `/layer/<i>/name` | ← | layer `i` name (relabels the strip) |
| `/layer/<i>/active` | ← | 0 hides / 1 shows strip `i` (config-defined layers) |
| `/master/alpha` | ⇄ | master composite alpha |
| `/intent/<i>` | ⇄ | intent activation `i` (0..5 = Energy, Density, Structure, Chaos, Persistence, Stillness) |
| `/intent/strength` | ⇄ | master intent strength |
| `/synth/agency` | ⇄ | `agency` |
| `/synth/audiogain` | ⇄ | `AudioResp` |
| `/synth/motiongain` | ⇄ | `VideoResp` |
| `/sync` | → | handshake (see below) |

## Behaviour baked into the surface (root Lua script)

- **Handshake:** on entering Control mode the surface sends `/sync` a few times
  over ~3 s, so the Mac learns the iPad's address and pushes current state
  without needing a first fader touch. It then goes quiet (no perpetual
  heartbeat that would fight live edits).
- **Inactive layers:** `OscController` sends `/layer/<i>/active` for all 7
  strips on every config load; the surface **hides** the strips a config
  doesn't define.

## Layout

Single portrait page, three bands: **LAYERS** (7 alpha faders + pause toggles +
name labels, master-α at right) · **INTENT** (6 activation faders + strength) ·
**SYNTH** (agency / audio gain / motion gain).

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

Every widget message sends/receives on **all connections** (the "∞" option), so
you can map several — e.g. **connection 0 = home Wi-Fi, connection 1 = travel
router** — and the surface uses whichever is live, no per-venue reconfiguring.
Unreachable connections just fail silently.

Both devices on the same Wi-Fi/LAN. The app needs the
`com.apple.security.network.{server,client}` entitlements (already set) **and**
macOS **Local Network** permission granted to the app (System Settings → Privacy
& Security → Local Network) — required for the Mac→iPad feedback direction. A
Developer-ID-signed build (Debug or Release) keeps that grant across rebuilds.

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
| `/agency/level` | ← | overall agency level (read-only meter) |
| `/agency/<i>/budget` | ← | controller `i` charge-to-fire = budget ÷ threshold (read-only) |
| `/agency/<i>/armed` | ← | controller `i` armed (budget ≥ threshold; lights the fire-line) |
| `/agency/<i>/name` | ← | controller `i` name (`Agency` prefix stripped) |
| `/agency/<i>/active` | ← | 0 hides / 1 shows controller slot `i` |
| `/agency/<i>/force` | → | force-trigger controller `i` (momentary) |
| `/sync` | → | heartbeat / discovery (see below) |

## Sync behaviour (surface Lua + `OscController`)

- **Heartbeat / discovery:** the surface pings `/sync` on connect and every
  ~2 s thereafter, so the Mac learns — and relearns, after an app restart or an
  iPad IP change — the surface's address. The Mac treats `/sync` as keepalive
  only: it pushes state on **first contact** and on **config load**, never on
  the heartbeat itself, so the heartbeat can't fight live edits.
- **Slow state sync:** beyond those pushes, `OscController` re-pushes the full
  control state every ~2 s **while the surface is idle** (no control message for
  ~1.5 s), so parameter moves from the desktop GUI or a MIDI controller reach
  the surface. The idle gate keeps it from yanking a fader you're mid-drag on,
  and doubles as a robustness backstop if a first-contact push is ever missed.
- **Agency meters:** each controller meter shows **charge-to-fire** (budget ÷
  its trigger threshold), so near-the-top = about to fire and full = armed; the
  amber bar at the top of the meter is the trigger line and lights when armed.
  `Force` triggers immediately regardless of budget.
- **Inactive layers / agency slots:** `OscController` sends `/layer/<i>/active`
  (all 7 strips) and `/agency/<i>/active` (all 4 slots) on every config load;
  the surface **hides** the ones a config doesn't define.

## Layout

Single portrait page, three bands: **LAYERS** (7 alpha faders + pause toggles +
name labels, master-α at right) · **INTENT** (6 activation faders + strength) ·
**SYNTH** (agency / audio gain / motion gain).

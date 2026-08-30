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
| `/layer/<i>/state` | ← | int 0..7 — strip `i`'s **R/M/S lamps** (see Strip state) |
| `/master/alpha` | ⇄ | master composite alpha |
| `/intent/<i>` | ⇄ | intent pole `i` (0..6 = Dense, Sparse, Still, Agitated, Persistent, Ephemeral, Chaotic) |
| `/intent/impacts` | ← | ONE msg, 7 int32 in fader order: −1 unmeasured, 0 below-noise, 1/2/3 moderate/solid/strong. The root script rides pole-fader brightness on it |
| `/intent/strength` | ⇄ | master intent strength |
| `/synth/agency` | ⇄ | `LiveAgency` (the address keeps its old name for layout compatibility) |
| `/synth/audiogain` | ⇄ | `AudioResp` |
| `/synth/motiongain` | ⇄ | `VideoResp` |
| `/agency/level` | ← | overall agency level (read-only meter) |
| `/agency/<i>/budget` | ← | controller `i` charge-to-fire = budget ÷ threshold (read-only) |
| `/agency/<i>/armed` | ← | controller `i` armed (budget ≥ threshold; lights the fire-line) |
| `/agency/<i>/name` | ← | controller `i` name (`Agency` prefix stripped) |
| `/agency/<i>/active` | ← | 0 hides / 1 shows controller slot `i` |
| `/agency/<i>/force` | → | force-trigger controller `i` (momentary) |
| `/grid/press` | → | tap set cell `x y` — dispatched by cell kind: Config / Snapshot / Scene (no set → ignored) |
| `/grid/page` | ⇄ | → switch to 1-based page; ← current page (highlights the page button) |
| `/grid/home` | → | load the set's designated home config |
| `/grid/cells` | ← | ONE msg, 64 `0xRRGGBB` int32 (row-major `y`=0..7, `x`=0..7); 0 = dark |
| `/sync` | → | heartbeat / discovery (see below) |

> **Known layout gap:** the host sends all **64** grid cells (`y`=0..7 — the meta
> row was retired and row 7 became a cell row), but `sharksynth.tosc` still only
> has 56 `cell_<x>_<y>` buttons (`y`=0..6) and its root script clamps to 56. The
> **eighth row of set cells is neither shown nor pressable on the iPad.** Adding
> row 7 in the TouchOSC editor (eight more `cell_<x>_7` buttons, cloned from row
> 6) and raising the clamp to 64 closes it. Nothing host-side needs changing.

The `/grid/*` addresses drive the **SET** band (see Layout). They are live only
while the Synth has a set loaded (`session-config.json` `setName`); with no set
the pads/GUI/iPad keep their `buttonGrid` behaviour. Cell colours (including the
memory-dependent 25% dim) are computed host-side and pushed as `/grid/cells`;
the layout's root script recolours each `cell_<x>_<y>` button by name.

## Strip state (R/M/S) — the same three facts as the Korg

The nanoKONTROL2 tells the performer three things per strip, and the desktop GUI
now mirrors them as three pips under each group fader:

| Lamp | Lit when |
|---|---|
| **R** | a strip exists here |
| **M** | the chain is **PARKED** (paused) |
| **S** | the chain is **AUDIBLE** (its alpha is above zero) |

The state that matters most is the one with **only R lit** — *armed and waiting*:
running, but silent. That is what the crossover gesture is aiming at, and it must
not look like either "playing" or "parked".

`/layer/<i>/state` carries all three as one int, bit-packed exactly as the Korg
lights them:

Bits: `1` = R exists · `2` = M parked · `4` = S audible. So the values a surface
actually sees are:

| Value | Reads as |
|---|---|
| `0` | no strip here |
| `1` | **armed and waiting** — running, silent |
| `5` | playing |
| `3` | parked |
| `7` | parked, picture still held on screen |

Unlike `/layer/<i>/alpha` and `/layer/<i>/pause` — which only travel on the three
full-state pushes, so they can be up to ~2 s stale and are suppressed entirely
while you are touching the surface — `state` is **delta-tracked and pushed the
instant it changes**. That is safe here precisely because no interactive widget
binds it: it can never fight a finger on a fader. It uses the Korg's own
audibility threshold (`NanoKontrol2Controller::kAudibleAlphaEpsilon`), so the
iPad and the hardware cannot disagree about where audible begins.

### What the layout does with it — one editor change, no new widgets

The host side ships today; **the surface does not render it yet.** No new widget
is needed: `sharksynth.tosc` already carries a `pause` button and a `name` label
inside every `layer<i>` group, and its root script already proves that Lua can
set `.color` on a button and `.textColor` on a label. Only the root script needs
the branch.

In the TouchOSC editor, open the **root `group`**'s script and paste this into
`onReceiveOSC`, anywhere before the final `return false`:

```lua
  -- /layer/<n>/state: the strip's R/M/S lamps packed into one int, exactly as
  -- the nanoKONTROL2 lights them -- 1 = exists (R), 2 = parked (M), 4 = audible
  -- (S). Colour the pause button and the name label so the strip says the same
  -- thing as the Korg and the desktop GUI's pips. Bits are pulled arithmetically,
  -- matching the /grid/cells unpacking above.
  local s = path:match('^/layer/(%d+)/state$')
  if s then
    local args = message[2]
    local state = (#args >= 1) and args[1].value or 0
    local exists  = state % 2 == 1
    local parked  = math.floor(state / 2) % 2 == 1
    local audible = math.floor(state / 4) % 2 == 1
    local strip = self.children['layer' .. s]
    if strip and exists then
      local c
      if parked then      c = Color(1.00, 0.69, 0.19, 1.0)   -- amber: PARKED
      elseif audible then c = Color(0.36, 0.82, 0.44, 1.0)   -- green: playing
      else                c = Color(0.34, 0.36, 0.42, 1.0)   -- dim: armed and waiting
      end
      if strip.children.pause then strip.children.pause.color = c end
      if strip.children.name then strip.children.name.textColor = c end
    end
    return true
  end
```

`return true` is correct here — nothing else receives this address, so consuming
it cannot starve a widget. (The `/layer/<i>/alpha` and `/layer/<i>/pause`
branches would need `return false`; this one has no widget behind it.)

Two things to keep in mind when editing:

- **Reach the widgets through the strip group, never `findByName`.** `fader`,
  `pause` and `name` each occur seven times (and `name` again in the four
  `agency<i>` groups), so `self:findByName('pause', true)` is ambiguous.
  `self.children['layer'..s].children.pause` is not.
- The `layer<i>` **group itself is not usable as a lamp**: it is authored with
  `background = 0` and `color.a = 0`, so setting its `.color` paints nothing.
  Strip existence stays where it already is — `/layer/<i>/active` drives the
  group's `.visible`.

If you would rather keep the `pause` button its neutral grey, the alternative is
one small non-interactive **BUTTON** per strip, added inside each `layer<i>`
group (name it `state`, `interactive = 0`, `background = 1`; there is free space
at roughly `y = 284..288`, below the pause button). Attach **no OSC message** to
it — Lua drives it — and change the two assignment lines above to
`strip.children.state.color = c`.

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
- **Strip state:** `/layer/<i>/state` is the exception to the slow sync — it is
  delta-tracked per frame and pushed the moment a strip parks, un-parks, or
  crosses the audibility threshold, however that change was made (iPad, GUI,
  nanoKONTROL2, a scene press). See **Strip state** above.

## Layout

Portrait canvas, four bands top-to-bottom: **LAYERS** (7 alpha faders + pause
toggles + name labels, master-α at right) · **INTENT** (7 activation faders +
strength) · **SYNTH** (agency / audio gain / motion gain) · **SET** (8×7 grid of
`cell_<x>_<y>` buttons + a page row `page_1..4` + `HOME`).

The SET band sits **below** the control bands on one enlarged canvas (the
documented fallback for the set-pages tab): the existing surface is untouched, so
its proven layer/agency hide-show and intent-impact colouring keep working. The
`gridTab` group is a self-contained unit — a ready-made pager page — so it can be
promoted to a native TouchOSC **Pager** tab in the editor later (a pager's pages
are just groups) without touching the OSC wiring.

# TouchOSC to Open Stage Control: the mapping

Written on 14 November 2026. An assessment. Nothing here is built, installed or run.

Expanded on 16 November 2026: section 11 now compares the wider iPad market, distinguishes
bundled artwork from runtime thumbnail delivery, and reassesses maintenance risk. Its
recommendations supersede the original alternatives ranking. Sections 1–10 remain the
Open Stage Control implementation assessment, not a tested migration plan.

## Summary

The owner asked on 14 November 2026 how difficult a swap of the iPad surface to Open Stage
Control is. TouchOSC has no image control, and the owner wants pad and fingerprint pictures
on the iPad.

| Question | Answer |
|---|---|
| How difficult | About 5 to 6 days for a like-for-like port of the 5 tabs, after a spike of 1 day |
| Why the number is small | The host's OSC protocol stays as it is. The layout becomes a plain JSON tree |
| Where the days go | The generator (2 days), the custom module and its tests (1.25 days), the host and the launch scripts (0.75 day), checks (0.5 day), rounds on the iPad with the owner (1 to 1.5 days) |
| What changes in operation | A server process runs on the Mac. The iPad opens a web page from it. The app talks to the server on the Mac itself |
| The main gain | Real pictures on the iPad, read from files on the Mac |
| The main unknown | Latency and the feel of faders and pads in a browser on the iPad. No figures exist |
| The spike | The PRINTS tab alone, with real fingerprint pictures, in a launch with `--osc-ports 8010 9010`. TouchOSC stays as it is for normal launches |

The project's repository holds an `AI_POLICY.md` that prohibits "anything AI-related" in the
project. The GPL-3.0 licence permits the use of the software. Do not send the project
patches, reports or questions about work that an AI wrote.

Each fact about Open Stage Control has a mark:

| Mark | Meaning |
|---|---|
| R | Read on a page or in a source file. The source key follows the mark |
| I | Inferred from facts that were read. Not seen to work |

## 1. Sources

Documentation pages are under `https://openstagecontrol.ammd.net/docs/`. Source files are in
`https://framagit.org/jean-emmanuel/open-stage-control`, branch `master`, read on 14 November 2026.

| Key | Source |
|---|---|
| D-intro | `getting-started/introduction/` |
| D-server | `getting-started/server-configuration/` |
| D-node | `getting-started/running-with-node/` |
| D-mech | `widgets/general-mechanics/` |
| D-props | `widgets/properties-reference/` |
| D-adv | `widgets/advanced-syntaxes/` |
| D-script | `widgets/scripting/` |
| D-canvas | `widgets/canvas/` |
| D-frag | `widgets/fragments/` |
| D-cm | `custom-module/custom-module/` |
| D-ex | `custom-module/examples/` |
| D-css | `customization/css-tips/` |
| D-client | `user-interface/client/` |
| D-copt | `client-options/` |
| D-remote | `remote-control/` |
| D-log | `changelog/` |
| S-session | `src/client/managers/session/session.mjs` |
| S-server | `src/server/node/server.mjs` |
| S-udp | `src/server/node/osc/udp.mjs` |
| S-ipc | `src/client/ipc/index.mjs` and `src/server/node/ipc/client.mjs` |
| S-osc | `src/client/osc.mjs` and `src/client/managers/widgets.mjs` |
| S-slider | `src/client/widgets/sliders/slider.mjs` |
| S-image | `src/client/widgets/frames/image.mjs` |
| S-drag | `src/client/events/drag.mjs` |
| S-html | `src/html/client.html` and `src/scss/reset.scss`, `index.scss` |
| S-pkg | `package.json`, `README.md`, `AI_POLICY.md` |
| API | `https://framagit.org/api/v4/projects/jean-emmanuel%2Fopen-stage-control` (releases, commits, contributors) |
| F-n | Forum topic `https://openstagecontrol.discourse.group/t/n` |
| TN3179 | Apple technote TN3179, "Understanding local network privacy" |

## 2. The project

| Fact | Mark |
|---|---|
| Version 1.31.2, released on 8 November 2026. 8 releases in the 12 months before it | R API |
| Licence GPL-3.0 | R S-pkg |
| The contributor API is heavily dominated by one maintainer. Its author records are not a count of independent maintainers; see the fresh check in section 11 | R API |
| The repository moved from GitHub to Framagit in December 2025 | R API, D-log 1.29.9 |
| `AI_POLICY.md` in the repository says: "Anything AI-related is strictly prohibited from this project." | R S-pkg |
| Three parts: a server, a launcher and a client. The client is a web application that the server serves | R D-intro |
| The server runs in Electron, or headless under Node.js 16 or later from the `node` package | R D-intro, D-node, S-pkg |
| Client browsers: Firefox 75 or later, Chrome 75 or later, "iOS devices must be of version 11 or higher" | R D-intro |
| The Mac application is not notarised. The page links a Gatekeeper workaround for the "damaged and can't be opened" error | R D-intro |

On this Mac: macOS 26.6.2, Node v26.12.1 and npm in `/opt/homebrew/bin`. Open Stage Control is not installed.

## 3. Processes and the flow of OSC

| Part | Today (TouchOSC) | With Open Stage Control |
|---|---|---|
| Surface | The TouchOSC app on the iPad, with the layout file stored on the iPad | A browser page on the iPad, loaded from the Mac at each connection |
| Extra process on the Mac | None | The Open Stage Control server, headless: `node <package> --no-gui --load surface.json --custom-module surface.js --port 8080 --osc-port 9000 --send 127.0.0.1:8000 --read-only` (R D-server, D-node) |
| iPad to Mac | UDP from the iPad to port 8000 of the app | One WebSocket from the browser to port 8080 of the server (R S-ipc), then UDP from the server to `127.0.0.1:8000` |
| Mac to iPad | UDP from the app to port 9000 of the iPad | UDP from the app to `127.0.0.1:9000`, then the same WebSocket |
| Who learns whose address | The app learns the iPad from the first packet | The app learns `127.0.0.1` from the first `/sync` of the custom module. The server sends from its one UDP port, which is also its input port (R S-udp) |
| Layout update | The owner opens the TouchOSC editor and sends the file | A reload of the page. The server gives every client the session named by `--load` (R D-server) |

Local Network permission (the fault recorded on 2 October):

| Fact | Mark |
|---|---|
| "Sending a UDP unicast" to a local network address requires local network access. That is what the app does today | R TN3179 |
| "Listening for and accepting incoming TCP connections" does not require it. That is all the server does for the iPad | R TN3179 |
| A local network is "an IP network associated with a broadcast-capable network interface", so loopback traffic between the app and the server is outside the rule | I from TN3179 |
| "Command-line tools run from Terminal or over SSH, including any child processes they spawn" are allowed automatically. The server publishes itself by Bonjour, which needs the access, and gets it this way when a script starts it | R TN3179, S-udp |
| On the iPad, traffic from Safari does not require local network access | R TN3179 |

Connection loss:

| Event | What happens | Mark |
|---|---|---|
| The iPad sleeps or leaves the network | The WebSocket closes. The client tries again every 500 ms and shows a notice after 1 s | R S-ipc |
| It returns within 60 s | The same client record continues. Both ends queue messages while apart and send them on return | R S-ipc |
| It returns after 60 s, or the page reloads | The server has destroyed the record. The page is a new client and its widgets hold no state | R S-ipc |
| State for a new client | The custom module hears `sessionOpened` and can send to that client alone | R D-ex |
| The app restarts | The module's next `/sync` is the first contact, and the app pushes its state | I from our host code |
| The server stops | The surface is dead until the server starts again. The app, the GUI and the MIDI controllers continue | I |
| A menu entry "Prevent sleep (mobile only)" exists. The package uses `nosleep.js` | R D-client, S-pkg |

## 4. Mechanisms used below

| Mechanism | What it gives | Mark |
|---|---|---|
| `address` and `preArgs` | A widget sends `/address ...preArgs ...value`. A message with the same address and preArgs sets the widget | R D-mech |
| No echo | A received value sets the widget with `send: false` | R S-osc |
| No fight under a finger | A slider that is touched queues a received value until release | R S-slider |
| `OSC{address, default}` in a property | The property follows the last value received at that address. Two or more arguments arrive as an array | R D-adv, S-osc |
| Dynamic properties | 54 properties update with no rebuild of the widget. They include `label`, `visible`, `css`, `html`, the colours, `values`, `left`, `top`, `width` and `height` | R D-props |
| `/EDIT id properties` | Any property of a widget set by an OSC message, also from the custom module | R D-remote |
| Custom module | A JavaScript file in the server. `oscInFilter` and `oscOutFilter` see every message. `receive()` sends to clients, to one client if asked. `send()` sends to a target. `setInterval`, `loadJSON` and Node modules are available. The file reloads when it changes | R D-cm |
| `send()` in a widget script | Works only when a user interaction started the script. A reply to a received message must come from the custom module | R D-script, F-3914 |
| Numbers | Sent as floats unless `typeTags` says otherwise. `decimals` is 2 by default | R D-props |
| Images | An `image` widget takes a file path, a URL or base64 as its value. Any `css` can use `url()`. Paths resolve against the session folder, the theme folder, `--remote-root`, then as absolute paths. The server serves svg, jpg, jpeg, png, apng, gif, webp, tiff, bmp and ico | R D-props, D-css, S-server |
| `reload` to an image widget | Fetches the file again | R D-props, S-image |
| Base64 frames | The examples push JPEG frames of a video stream to an image widget with `receive()` | R D-ex |
| `canvas` widget | Drawing with the Canvas API in `onDraw`, touches in `onTouch` with a pointer id for each finger | R D-canvas |
| Multi-touch | Each finger of a touch event is tracked by its identifier | R S-drag |
| Session file | JSON: `createdWith`, `version`, `type`, and `content`, a tree of widget objects with `widgets` or `tabs`. Old versions convert at load | R S-session |
| Clients | Widgets with the same address, preArgs and targets synchronise between clients. `bypass` stops it for a widget | R D-mech |
| Full screen on iOS | "On iOS you have to add the page to your home screen to be able to launch it in fullscreen." The page declares `apple-mobile-web-app-capable` | R F-2335, S-html |
| Menu | The root widget's `hideMenu` hides it. `--read-only` stops editing and saving | R F-3246, D-server |
| Browser gestures | The page sets `user-scalable=no`, `user-select: none`, `touch-action: none` and `overscroll-behavior: none` | R S-html |

## 5. Messages

The host's protocol stays as it is. "Module" is the custom module.

### iPad to host

| Address | Today | Open Stage Control |
|---|---|---|
| `/sync` | Root script, at load and every 2 s | Module, `setInterval` (a widget script cannot send unasked, R D-script) |
| `/layer/<i>/alpha`, `/master/alpha`, `/intent/<i>`, `/intent/strength`, `/synth/agency`, `/synth/audiogain`, `/synth/motiongain`, `/input/<i>/gain`, `/loop/level`, `/fingerprint/size`, `/fingerprint/opacity` | FADER, float 0 to 1 | `fader` with that address and `decimals: 4` |
| `/layer/<i>/pause` | Toggle BUTTON | `button`, mode `toggle` |
| `/agency/<i>/force`, `/input/<i>/reset`, `/grid/home`, `/loop/record`, `/loop/stop`, `/fingerprint/<i>/fire`, `/fingerprint/stop`, `/fingerprint/asdrawn`, `/take/mark` | Momentary BUTTON, 1 then 0 | `button`, mode `tap`, which sends `on` once (R D-props). The host takes a value over 0.5 or no value as a press |
| `/grid/press x y`, `/grid/release x y` | BUTTON with 2 messages | `button`, mode `push`, `preArgs: [x, y]`. It sends on and off at one address, so the module renames the off to `/grid/release` |
| `/grid/page n` | 16 BUTTONs | `switch` with dynamic `values`, or 16 `button`s |
| `/fingerprint/carrier k` | 8 BUTTONs | 8 `button`s, mode `tap`, `on: k`. A `switch` cannot colour one item amber |
| `/fingerprint/place cell` | 25 BUTTONs | 25 `button`s as today, or one `xy` or `canvas` pad that sends 2 floats, which the host already takes |
| `/calibrate/press step` | No control sends it | `button` with a string `on`, if wanted |

Integer arguments: the host reads them with `getArgAsInt32`, which converts a float (ofxOsc source in
this tree). `typeTags: "ii"` on the pads keeps the wire the same as today.

### Host to iPad

| Address | Today | Open Stage Control |
|---|---|---|
| Fader and toggle values (the addresses above) | The widget's own receive | The widget's own receive |
| `/layer/<i>/name`, `/agency/<i>/name`, `/input/<i>/name`, `/input/<i>/db`, `/grid/now`, `/loop/length`, `/fingerprint/<i>/name`, `/fingerprint/<i>/length`, `/fingerprint/status`, `/mix/heading` | LABEL text. The script splits a 2-line name over 2 labels | `text` widget at that address, `wrap: true`. One widget holds both lines (I: `\n` in a text value breaks the line) |
| `/layer/<i>/active`, `/agency/<i>/active`, `/input/<i>/active`, `/fingerprint/<i>/active` | Script sets `visible` on a group | `visible: "OSC{/layer/<i>/active, 0}"` on the strip's `panel` |
| `/layer/<i>/state` | Script colours the pause button and the name | Module sends `/EDIT` with 2 colours, or the strip takes `colorWidget: "#{...OSC{/layer/<i>/state}...}"` |
| `/intent/impacts` (7 ints) | Script sets fader and label brightness | Each fader's `colorWidget` is a `#{}` formula on `OSC{/intent/impacts}`, which arrives as an array |
| `/agency/level`, `/agency/<i>/budget`, `/input/<i>/level`, `/loop/pos`, `/fingerprint/<i>/playing` | Read-only FADER | `fader` with `interaction: false` |
| `/agency/<i>/armed` | BUTTON used as a lamp | `led` |
| `/grid/cells`, `/grid/state`, `/grid/labels` (64 values each) | Script keeps 3 tables and redraws 64 pads: colour, tier, ring, text | Module keeps the 3 tables and sends each changed pad its label, colour and a tier class by `/EDIT`. Tiers become CSS (opacity and grey). The ring is a CSS outline on the active pad |
| `/grid/quadrants` | Script sets 4 names and frame colours | 4 `panel`s with `label` and `colorStroke` from `OSC{}` |
| `/grid/pages` | Script shows, names and colours 16 buttons | Module sets the `values` of the page `switch` and its value |
| `/loop/active`, `/loop/state` | Script sets a lamp, 2 captions and colours | Module maps the 2 values to 3 labels and a colour and sends `/EDIT`, or `#{}` formulas |
| `/fingerprint/carriers` | Script sets 8 buttons: shown, name, colour | Module sends `/EDIT` to the 8 buttons |
| `/fingerprint/place` | Script moves a ring | The `xy` pad follows the value, or a CSS class on the chosen cell |

### New, for pictures

| Message | From | Use |
|---|---|---|
| `/grid/pictures` (64 file paths) | Host. It knows each pad's preview file (`Synth::getSetCellPreviewPath()`) | Module sets each pad's `css` to `background-image: url(path)` |
| `/fingerprint/<i>/picture` (a file path) | Host. The player holds the picture as a texture today, so it must write a PNG or JPEG beside the fingerprint | An `image` widget, or the fire button's `css` |
| A picture of the canvas, as a file and `reload`, or as base64 | Host, about once a second | The ground of the place pad on PRINTS (I: built from the mechanisms above, not seen) |

## 6. The tabs

Widget counts are from `build_layout.py inventory` today.

| Tab | Today | Open Stage Control |
|---|---|---|
| Tab row | 5 BUTTONs and 5 LABELs. The root script polls the buttons each frame and shows one group | The root widget holds 5 `tab`s. The client draws the tab bar. Hidden tabs cost less than hidden panels (R F-2654) |
| SET | 301 nodes. Each pad is 4 nodes: a swatch, a clear button and 2 text labels | About 90 widgets. A pad is 1 `button` with a wrapped label, a colour and a picture. 4 `panel`s with grid layout hold 16 pads each |
| MIX | 56 nodes: 7 strips, the master, 7 poles and the strength | The same parts. A strip is a `panel` with a `fader`, a `button` and a `text` |
| LIVE | 88 nodes: 3 response faders, 4 inputs, the agency level, 8 controller slots | The same parts |
| LOOP | 14 nodes | The same parts |
| PRINTS | 126 nodes: 12 slots, 8 carriers, 25 place cells, 2 faders, 2 buttons | 12 picture buttons with a progress bar each, 8 carrier buttons, 1 place pad over the canvas, 2 faders, 2 buttons |

## 7. The root Lua script, function by function

| Lua today (435 lines) | New home |
|---|---|
| `init`, `update`: `/sync` at load and every 2 s | Module: `init` and `setInterval` |
| `update`: poll the tab buttons. `showPage` | Gone. Tabs are a widget |
| `renderGrid`: tiers, colours, 2-line names, ring, quadrant frames | Module for the tables and the diff. CSS for the tiers and the ring |
| `renderPages` | Module |
| `setActive` | `OSC{}` in `visible` |
| `/layer/<n>/state` and `/layer/<n>/name` branches | `#{}` formula or module. The name split goes |
| `/intent/impacts` branch | `#{}` formula |
| `renderLoop` | Module or `#{}` formulas |
| `renderCarriers`, `renderPlace` | Module |
| The final `return false`, which lets a message reach its widget | Not needed. The module returns the message to let it pass |
| Not in the script: state for a surface that connects late | Module: keep the last arguments for each address and replay them to a client at `sessionOpened` |

## 8. Things that exist only for TouchOSC

| Item | Fate |
|---|---|
| The zlib XML format, `SCHEMA.md`, clone-and-rewrite of nodes, the 13 mutations of `build_layout.py` | Kept and frozen while TouchOSC stays as a fallback. The new generator writes JSON from a description of the layout |
| The swatch label behind each pad, because a momentary button shows its colour only while pressed | Gone |
| 2 labels for a 2-line name, because a label does not break lines | Gone |
| The duplicate-name check, because scripts reach widgets by name | Becomes a check that every `id` is unique. Widgets with one id act as clones (R D-props) |
| `test_script.lua` and the stub of TouchOSC, 128 checks | A test of the module under `node --test`, with a stub for `receive` and `send` |
| `build_layout.py preview`, an SVG sketch of a page | A picture of the real client from a headless browser |
| The send from the TouchOSC editor to the iPad | Gone. The iPad loads the session from the server |
| Connections 0 and 1 for two networks | The iPad opens the Mac by address or by its Bonjour name (I) |

## 9. Running both surfaces from one host

| Fact | Source |
|---|---|
| The host answers one remote host. A packet from another host makes it change target and push the whole state | `OscController::pollIncoming()` |
| TouchOSC and the server would each send `/sync` every 2 s, so the host would change target and push the state about once a second | I from that code |
| `--osc-ports IN OUT` gives a launch other ports. Then the server is its only surface and the iPad's TouchOSC is not connected in that launch | `src/main.cpp` |
| The module could relay between the host and TouchOSC, so the host sees one remote | I from D-ex "Simple bridge" |
| A second target in the host would let both run in one launch | Our code, about half a day |

## 10. Not confirmed

| Item | Why it matters |
|---|---|
| Latency and jitter from an iPad over Wi-Fi. No figures found | Fader rides on the groups |
| The feel of a fader and of 2 or more fingers on an iPad. The code tracks each finger. Not seen on a device | The whole surface |
| Whether the Node package runs under Node 26. `package.json` says 16 or later | The headless server |
| Whether a 400 ms hold on a pad does anything in Safari. No `-webkit-touch-callout` rule was seen in the 3 style files read | Pad holds |
| The speed of the client with about 250 widgets and 64 background pictures on the owner's iPad | SET |
| Whether `\n` in a `text` value or a button `label` breaks the line | Names |
| Whether a Home Screen page on the iPad reconnects after sleep as Safari does. One forum report of a stale cache with a Home Screen bookmark (F-4142, December 2025) | Shows |
| How the client behaves when the session file changes under `--load`. Fragment files update the page when they change (R D-frag) | The update workflow |

## 11. Deeper iPad alternatives review — 16 November 2026

This review covers a replacement for the whole surface and an image-pad-only addition.
It uses vendor manuals, release records, public source repositories and Apple's public
UK App Store lookup API. No candidate was installed or tested on an iPad. A documented
feature is not evidence of acceptable performance with this instrument.

**Conclusion:** Open Stage Control remains the strongest documented match for the whole
surface with changing thumbnails. There are more credible iPad options than the earlier
ranking showed: **Lemur, OSC/PILOT, Ctrl for iPad, Ctrl Suite with Ctrl Player, and Companion's
web surface**. None has yet demonstrated both our entire workflow and clearly stronger
maintenance continuity. Companion offers the clearest broader ecosystem for image buttons;
Lemur is the first native performance surface to test; Ctrl Suite is the additional commercial
system worth evaluating if vendor support is a priority. These are assessment judgments.

### 11.1 What counts as image support here

There are three different capabilities:

1. **Bundled artwork:** ship pad pictures with a layout before playing.
2. **Runtime selection:** choose among pictures already available to the controller when
   the grid page changes.
3. **Runtime delivery:** display a newly created or changed picture from the Mac without
   opening an editor, transferring a new project or interrupting performance.

The first capability alone solves illustrated preset buttons. For our evolving grids and
fingerprints, the third is the useful target. A slow canvas preview is a separate, optional
requirement; full video streaming should not be required just to deliver thumbnails.

The current surface also needs independent pad-down and pad-up messages: a short press
previews a cell and a 400 ms hold applies it. It needs several simultaneous faders, incoming
names/colours/visibility, 64-value grid messages, page changes, and recovery after sleep or
an app restart. These requirements come from [our OSC contract](README.md), not product
marketing. A picture widget and an OSC-send checkbox do not establish a replacement.

### 11.2 Comparison

“Unverified” means no sufficient documented path was established, not that the feature is
impossible. Native means an installed iPad app; it does not by itself establish the internal
rendering technology or performance. Sources and qualifications follow the table.

| Option | iPad route | Image delivery from the Mac | Full-surface fit | Maintenance assessment |
|---|---|---|---|---|
| Open Stage Control | Browser; server on Mac | Documented file/URL/base64 images and reload | Closest documented fit; custom module handles our state messages | Active but concentrated in one maintainer; source and runtime can be retained |
| Lemur, MIDI Kinetics version | Native; direct OSC, with vendor-documented wired option | Project images documented; runtime arrival of new files unverified | Strong scripting and multitouch; most promising native candidate | Active commercial revival; engineering redundancy not established |
| OSC/PILOT v1 | Native; desktop editor | Embedded images and runtime file-path control; iPad file access/URL loading unverified | Strong controls; adapter needed for our compound state and distinct press/release addresses | Recent updates; staffing unknown; closed source |
| OSCPilot v2 | Native Player; desktop editor | Artwork documented; runtime replacement unverified | General OSC input/feedback and v1 feature parity unverified | Newly shipped Player; account-based desktop activation adds a dependency |
| Ctrl for iPad | Native, including editor | Button/background images; remote image replacement unverified | OSC APIs and controls; our bulk state and generated-layout workflow need validation | Maintained commercial app; small company, unknown product-specific staffing |
| Ctrl Suite + Ctrl Player | Native Player; Engine on Mac | Managed image assets; arbitrary new-thumbnail ingestion unverified | Processes can adapt OSC and control appearance; substantial integration layer | Commercial services and named company leadership; no proven support SLA |
| Bitfocus Companion | Browser; Companion on Mac | Generic HTTP module fetches and polls image URLs | Good image-pad surface; documented web UI is a button grid, not a free-form mixer | Broader open-source ecosystem; community support, not a commercial SLA |
| Mira + Max | Native; Max on Mac | Max's supported `fpic` image object; changing-image workload needs testing | Can adapt protocol in Max; adds another runtime | Established vendor, but native app last updated in 2020 |
| Touch Portal + OSC integration | Native client; desktop service | Custom/dynamic icons are possible through its ecosystem | OSC, feedback, image updates and simultaneous faders need an end-to-end proof | More than one component/vendor to maintain; not a demonstrated risk reduction |
| Custom web or native surface | Our software | We can design the required delivery path | Exact fit is possible, but every behaviour becomes our responsibility | Reduces dependence on a controller vendor; transfers maintenance to us |

### 11.3 Open Stage Control: active, but the concentration is real

A fresh [Framagit releases API](https://framagit.org/api/v4/projects/jean-emmanuel%2Fopen-stage-control/releases?per_page=3)
check returned **1.31.2, 8 November 2026**, preceded by 1.31.1 on 30 August and 1.31.0 on
24 August. This is current maintenance, not an abandoned project.

The [contributors endpoint](https://framagit.org/api/v4/projects/jean-emmanuel%2Fopen-stage-control/repository/contributors?per_page=100)
returned 15 author records representing 2,000 commits. Three records named `jean-emmanuel`
accounted for 1,967, or **98.35% of that returned population**. This is not a full-history
commit count or 15 independent maintainers. It supports the concentration concern without
repeating the previous assessment's inconsistent counts.

Its [image properties](https://openstagecontrol.ammd.net/docs/widgets/properties-reference/)
explicitly support URLs, base64, local paths, caching control and reload. The
[architecture](https://openstagecontrol.ammd.net/docs/getting-started/introduction/) puts
OSC handling and file serving on the Mac and the interface in the browser. That makes new
Mac-generated pictures a natural part of the design. The exact migration remains in
sections 3–10.

There are two distinct continuity risks:

- **Obtaining and starting a known version:** retaining the release package, source,
  compatible runtime, session and custom module gives us substantial control.
- **Future compatibility fixes:** retaining those files does not freeze iPadOS, Safari or
  macOS. If the maintainer stops, we would need to fix problems, engage someone who can,
  or migrate. An available source tree makes this possible, not cost-free.

The previous statement that a Safari failure necessarily affects every browser surface was
too broad. [WebKit issue 308073](https://bugs.webkit.org/show_bug.cgi?id=308073), still marked
NEW when checked, reports a local WebSocket reconnect failure after backgrounding Safari
26.3, while other browsers on the same device worked. That is a reason to test Safari,
Home Screen mode and another browser on our actual iPad. It is not proof that current
versions all fail, or that all web controllers share precisely the same fault.

**Judgment:** pinning and retaining Open Stage Control is a reasonable deployment strategy,
but it does not remove the single-maintainer risk. Avoid a large private fork initially;
keep instrument-specific adaptation in our module and keep the host protocol independent.

### 11.4 Lemur: the strongest native performance candidate

The current MIDI Kinetics product is distinct from the discontinued Liine release. The
vendor's [November 2026 transition statement](https://www.midikinetics.com/announcement/lemur-rerelease/)
confirms ongoing development and a separate purchase for the new product. Its
[changelog](https://github.com/MIDIKinetics/LemurChangelog) documents 5.8 in August and 5.9
in November. Apple's live lookup confirmed iPad **5.9.0, released 2 November 2026**; the
cached store page was older.

The [product documentation](https://www.midikinetics.com/lemur/) establishes an Image widget,
multitouch Canvas, scripting, arrays, dynamic properties and XML projects. The
[store description](https://apps.apple.com/gb/app/lemur/id6739544164) also describes MIDI/OSC
connectivity over Wi-Fi or USB via the desktop software. These are valuable capabilities
for our interface and a wired performance setup.

However, adding images to a project does not establish loading a new thumbnail from the
Mac at runtime. No documented URL/file ingestion API for that workflow was verified. The
earlier “no live picture” verdict was too categorical; the accurate result is **unverified**.
Likewise, its specific base64/Canvas image-selection claim should not be treated as a tested
integration.

**Best use:** a complete native surface if thumbnails can be prepared and transferred with
each set. For evolving fingerprint images, prove new-image delivery before porting the
five tabs. Commercial ownership and recent releases are positive signals; neither the
public pages nor a company name establish multiple engineers able to maintain Lemur.

### 11.5 OSC/PILOT: distinguish v1 from the new v2

**v1 is a credible candidate that deserved more attention.** Its
[manual](https://oscpilot.com/pages/docs) documents PNG/JPEG images embedded in `.plt` files,
`/OSCpilot/FilePath` for runtime image changes, custom input/output arguments, momentary
buttons and a repeat interval usable for a heartbeat. That is useful for this instrument.
The unresolved issue is where a runtime image path resolves on an iPad, how new files get
there, and whether a URL is accepted. A Mac pathname sent over OSC is not file transfer.

The [release notes](https://forum.oscpilot.com/t/osc-pilot-release-notes-change-log/98) show
2026 fixes including an image memory leak and an OSC receive problem on macOS. Apple's live
lookup confirmed iPad **1.15.5, 31 August 2026**. The
[iPad release history](https://apps.apple.com/gb/app/osc-pilot/id1616363234) explicitly includes
working NDI support. NDI could serve a live preview, but using video for dozens of still
pads would add complexity; that is an engineering judgment, not a measured limitation.

**v2 is a separate evaluation.** The [new product page](https://untitledaudiosyndicate.com/oscpilot)
shows a USD 39.99 desktop editor and a free iPad/iPhone Player. Apple's live lookup found
Player **1.1, 13 November 2026**. The [v2 guide](https://untitledaudiosyndicate.com/oscpilot/guide/content/index.html)
documents `.op2` layouts, PNG/SVG artwork, touch-through decorative images, OSC output and
Syndicate-specific feedback. It does not establish the arbitrary OSC receive/property API
we need. Do not assume that v1's runtime image command or NDI support exists in v2.

The same guide says desktop offline operation lasts while the signed activation remains
valid; the product page mentions signing in again for renewal. The validity period and
behaviour after expiry need clarification. This is an operational dependency even though
the purchase is described as one-time. Player itself is advertised as requiring no account.

**Judgment:** test v1 alongside Lemur if native controls are preferred. Track v2, but its
newness and missing published feedback details make it premature to rank above either.
No evidence of a substantially larger engineering team was established for either version.

### 11.6 ON LX: two additional commercial iPad options

**Ctrl for iPad** is a self-contained app with editing on the device. Its
[store listing](https://apps.apple.com/gb/app/ctrl-for-ipad/id1247514528) describes custom
images, sliders and OSC, with a five-minute output limit in the free trial. Apple's live
lookup found **2.6.0, 30 October 2026**; the indexed page still displayed 2.5.1. The UK
listing showed a £99.99 Pro purchase; verify the required image feature tier in-app before
buying, as the [historical release notes](https://docs.onlx.ltd/ctrl-for-ipad/getting-started/release-notes)
associate button images with Pro.

Its [button OSC API](https://docs.onlx.ltd/ctrl-for-ipad/controls/buttons/push-button)
documents enable, theme, label and flash changes. This does not establish remote image
replacement. The older web manual lags the app, so missing documentation is a question to
resolve, not proof that current 2.6 cannot do it. Also verify that a generated workspace can
be imported reliably and that incoming state updates never execute outgoing actions.

**Ctrl Suite + Ctrl Player** is different: Designer creates the layout, an Engine runs on
the Mac, and Player displays it on the iPad. The
[system manual](https://docs.onlx.ltd/ctrl-suite) documents this architecture. The
[button reference](https://docs.onlx.ltd/ctrl-suite/ctrl-designer/nodes/interfaces/components/interaction/button)
has normal/selected images, separate press/release commands and links to remotely changing
appearance through Processes. The [Media Hub](https://docs.onlx.ltd/ctrl-suite/ctrl-designer/introduction-to-the-user-interface/media-hub)
manages and replaces shared image assets. None of these alone proves unattended ingestion
of newly generated Mac thumbnails during playback.

[ON LX](https://onlx.ltd/about/) describes a company operating since 2013, names two leaders,
and offers integration and development services. This gives a commercial route for a
supported integration. It does not establish product-specific maintainer redundancy or a
contractual response time. [Suite licensing](https://onlx.ltd/ctrl-suite/) separates Core
from Pro, includes a Player and limited entities, and includes 12 months of updates;
Inputs and Processes are listed under Pro. Our feedback adapter likely needs Pro: that is
an inference to check, not a price quote. Native Player **2.4.2, 20 July 2026** was confirmed
through Apple.

**Judgment:** Ctrl Suite is the most relevant newly found commercial system to investigate
when support matters more than a lightweight setup. Ask for a working demonstration of our
thumbnail flow and OSC feedback before committing to a licence or integration work.

### 11.7 Companion: a significant correction, particularly for pads

The earlier assessment said images had to be loaded by hand. That missed the
[Generic HTTP module's Image from URL feedback](https://github.com/bitfocus/companion-module-generic-http/blob/master/companion/HELP.md):
it fetches an image, fits it to a button, supports variables in the URL and provides a
configurable polling interval. We can therefore serve thumbnails from the Mac and have
Companion update them without a project transfer. This describes a supported mechanism;
our 64-pad workload remains untested.

The [Generic OSC module](https://github.com/bitfocus/companion-module-generic-osc/blob/master/companion/HELP.md)
supports multi-argument messages and received-message feedback. Its
[tablet web interface](https://companion.free/user-guide/beta/interactive-buttons/web-buttons/)
is documented as a button view. The current beta guide warns that Web Buttons is not a
Surface and cannot follow page changes configured on buttons. Choose and test the right
view/version rather than assuming our page navigation maps directly. There is no documented
free-form bank of continuous touch faders in that view.

Maintenance is broader than Open Stage Control's visible author concentration: Companion
has an established [open-source project](https://github.com/bitfocus/companion) and integration
ecosystem. But its [own documentation](https://companion.free/user-guide/beta/) calls it
community driven. [Bitfocus](https://bitfocus.io/about/) distinguishes that from its commercial
Buttons product with paid support and full-time development. Do not equate free Companion
with a support contract.

**Judgment:** shortlist Companion if the immediate requirement is image pads, with the mixer
on another surface. For the whole five-tab instrument, the button-grid model remains a
substantial compromise. A split surface also needs host routing work: our host currently
learns one remote address, so independent `/sync` senders can displace one another
(`src/OscController.cpp`, `pollIncoming()`). Use a deliberate relay or add multiple targets;
do not simply start both and assume feedback reaches both.

### 11.8 Other options screened

| Option | Findings and reason not to prioritise |
|---|---|
| Mira + Max | [Mira supports `fpic` and multitouch](https://cycling74.com/products/mira), and the [native app](https://apps.apple.com/us/app/mira-controller/id649586480) supports USB on Mac. Apple still reports 1.2.18 from 29 October 2020. A larger vendor is not evidence of recent maintenance of this particular client. Worth considering if Max is already part of the setup, otherwise it adds a runtime and a patch to maintain. |
| Touch Portal | [Official site](https://touchportal.com/) supports iOS/desktop use and points to third-party plugins. An [OSC plugin exists](https://byreinier.com/), as does a [Dynamic Icons plugin](https://github.com/spdermn02/TouchPortal-Dynamic-Icons). Mac compatibility, bidirectional bulk state, image refresh and simultaneous fader feel were not established as one working solution. It moves some risk into plugins. |
| Chataigne | [Core](https://github.com/benkuper/Chataigne) supports OSC and browser dashboards. The [web dashboard](https://github.com/manuelmitasch/chataigne-web-dashboard) is a separate project. Useful where Chataigne already routes a show, but no evidence collected establishes a stronger maintenance arrangement or a better thumbnail/multitouch fit than Open Stage Control. |
| MobMuPlat | [Official project](https://danieliglesia.com/mobmuplat/) supports images, OSC and JSON layouts, using Pure Data; its source is available. It is an additional programming environment, not an established improvement in commercial support or image distribution. |
| DeviceJockey | [Current site](https://devicejockey.de/) offers iPad/iPhone/Mac via TestFlight. Its [Companion guide](https://devicejockey.de/companion/) describes images, sliders and OSC feedback. The [team page](https://devicejockey.de/about/) describes supporters around the creator. Interesting watchlist candidate, but beta distribution and unproven continuity do not solve this concern now. |
| Jockey OSC & MIDI | [Current listing](https://apps.apple.com/us/app/jockey-osc-midi/id1553621603) documents two-way OSC, editable controls and recent releases by an individual developer. Arbitrary pad images were not established. Distinct from DeviceJockey. |
| MetaGrid Pro | Its [integration overview](https://metagrid.app/integrations/) focuses on MIDI and desktop automation. The [vendor's OSC statement](https://forum.metagrid.app/t/applescript-javascript-osc/976) describes a planned release cycle. No sufficiently documented shipped generic OSC/thumbnail workflow was verified; a roadmap promise does not qualify it. |
| TouchOSC itself | The [current control reference](https://hexler.net/touchosc/manual/controls) still has no image control. Retain it as the known working fallback. |

MIDI-only surfaces, remote desktop products and dedicated mixer/DAW remotes do not directly
replace this arbitrary OSC protocol. They may be parts of a different design, but adding
bridges or emulating mouse input does not by itself solve our requirements.

### 11.9 What “better supported” should mean in this decision

| Evidence | What it establishes | What it does not establish |
|---|---|---|
| Recent release and relevant bug fixes | Someone is maintaining the product now | That someone else can take over |
| Company, support inbox, active community | Routes for help | Guaranteed response time or resolution |
| Source, build instructions, saved packages | A possible recovery or continuation path | That maintaining a fork is cheap |
| Perpetual purchase | Commercial licence model | Indefinite activation-server access or OS compatibility |
| Native iPad app | A different deployment route from Safari | Freedom from iPadOS regressions or App Store dependency |

For this project, continuity is best improved by keeping the host's control/state contract
separate from the chosen surface, using ordinary thumbnail files with stable identities,
and retaining a known working TouchOSC path. For runtime image delivery, prefer versioned
image URLs or filenames so that a replacement image cannot silently stay in a cache.
These are proposed design choices, not changes made by this assessment.

A custom web surface would let us own that delivery path and keep the dependency set small.
It would still need proper multitouch, touch cancellation, state reconciliation and iPad
browser testing. A custom native surface trades those browser issues for iOS development
and distribution work. Either is a defensible long-term choice if we want to own this part
of the instrument, but neither is a ready-made, better-supported product.

### 11.10 Recommended evaluation order and decision gates

1. **Test Open Stage Control as the functional baseline.** Extend the proposed PRINTS spike
   with a representative image grid and several faders. Do not commit to the full port on
   the strength of a static screenshot.
2. **Test Lemur as the native alternative.** Establish whether prepared images are enough.
   If new images during playing are required, prove that path first. Test OSC/PILOT v1 next
   if it offers a workable image path or NDI preview that Lemur cannot.
3. **Evaluate Ctrl Suite for a supported commercial implementation.** Require a demonstration
   of live thumbnail ingestion, our OSC feedback and independent touches, plus clear
   licence, offline-operation and support terms. Ctrl for iPad may be enough for prepared sets.
4. **Use Companion as the image-pad option** if splitting pads from faders is acceptable.
   It merits a real test, not exclusion on image support. Include routing and page behaviour.
5. **Keep OSCPilot v2 on the shortlist conditionally.** Confirm general incoming OSC,
   runtime images and offline activation before treating it as a v1 replacement.

Use the same acceptance test for candidates:

| Test | Pass condition |
|---|---|
| New thumbnail | Create a new file on the Mac while playing. The correct pad updates without editor/project transfer. Repeat with changed contents and rapid page changes. |
| Real image load | 64 grid pictures plus 12 fingerprint pictures at intended size; no mistaken pad/image associations, disruptive stalls or growing memory use. |
| Gestures | Hold a pad while moving two faders. Short taps still preview; 400 ms holds apply once; release and touch cancellation cannot leave a pad held. |
| Feedback | Names, colours, active rings, hidden controls and meter/progress values follow the host; updates do not echo commands or fight held faders. |
| Recovery | Background/sleep/wake the iPad; interrupt Wi-Fi; restart the host and any bridge. State converges and stale queued press/fire commands are not replayed. |
| Timing | Compare against TouchOSC on the same iPad/network. Measure typical and tail latency/jitter during fader movement and image refresh; use the owner's playing judgment too. |
| Operation | Cold-start with internet disconnected, reconnect with a changed IP, and confirm wired networking where intended. For activated products, distinguish internet-free startup from expiry behaviour. |
| Maintenance | Recreate the setup from retained layout/assets/runtime on a clean machine; document what still depends on the vendor or App Store. |

No generic latency numbers or new migration time estimates are justified before this test.
The five-to-six-day estimate in the opening Summary is the earlier Open Stage Control planning estimate,
not evidence that any candidate has already met these gates.

### 11.11 Freshness and limits of the evidence

Version checks used Apple's live public lookup endpoint on 16 November 2026, for example
[Lemur](https://itunes.apple.com/lookup?id=6739544164&country=gb),
[OSC/PILOT](https://itunes.apple.com/lookup?id=1616363234&country=gb),
[OSCPilot2 Player](https://itunes.apple.com/lookup?id=6825646158&country=gb),
[Ctrl for iPad](https://itunes.apple.com/lookup?id=1247514528&country=gb),
[Ctrl Player](https://itunes.apple.com/lookup?id=6747518244&country=gb), and
[Mira](https://itunes.apple.com/lookup?id=649586480&country=gb).
Several indexed App Store pages and vendor manuals lagged these records. Version availability
was checked for the UK store; prices and in-app purchase tiers can vary and should be checked
at purchase. Free download does not mean unrestricted free use.

The Open Stage Control release/contributor endpoints were read directly because the web
index could not fetch them. Some ON LX Process reference pages were inaccessible, and no
vendor was contacted. Missing documented features remain unverified. None of the comparisons
claims measured reliability, support response time, contractual support or a known successor
maintainer. The engineering recommendations are inferences from the cited capabilities and
our existing OSC implementation.

# TouchOSC to Open Stage Control: the mapping

Written on 14 November 2026. An assessment. Nothing here is built, installed or run.

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
| One maintainer wrote 1,945 of the commits. 15 contributors in all | R API |
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

## 11. The health of the project, and the alternatives

Researched on 14 November 2026, after the owner asked: "is the project currently maintained
etc? it's not going to disappear? are there any better alternatives?" Each fact was read at
its source unless it has the mark (I), for inferred. Nothing was installed or run.

### Open Stage Control

| Subject | Fact |
|---|---|
| Age and rhythm | First commit in April 2014, first release in March 2016. Releases in each year since 2016: 46, 33, 69, 34, 39, 37, 39, 12, 6, 24, 7. The dip is 2023 and 2024 |
| Who writes it | One person wrote 98% of 6,449 commits. He says: "I only develop on my free time". Patreon brings EUR 67.51 a month from 15 members. No roadmap was found |
| Community | In 3 months: 112 forum posts by 21 people. The maintainer answers in a median of 0.6 hours. 4 issues are open of 820 |
| Use on stage | The grandMA3 lighting manual documents it. Forum users name a school theatre and stage shows |
| Why it left GitHub | For a host that "cares for privacy and freedom and doesn't promote AI". The forum refuses posts that an AI wrote |
| Self-contained | Yes. The Node package of 1.31.2 is 4.1 MB, has no native code and loads local files only |
| Old releases | Packages before 1.29.8 are gone from GitHub and from the site. The source is on Framagit and in Software Heritage. There is no active fork |
| What breaks first | Safari on iPadOS. iOS 26 broke the connection until 26.4. WebKit bug 308073, a LAN WebSocket that hangs after Safari resumes, is open |

Verdict: the risk of losing what is installed is low. Keep the 1.31.2 package and a Node
installer. The risk that fixes stop is medium: one maintainer in his spare time, no successor
and no paid support. The likely failure in 3 to 5 years is an iPadOS update, and that failure
hits every surface that is a web page.

### Alternatives

| Route | A picture on a control | Faders under several fingers | Layout from a script | Link |
|---|---|---|---|---|
| Lemur 5.9.0 for iPad (MIDI Kinetics, 2 November 2026, USD 99.99) | PNG as base64 inside the project. A script draws one by name. New pictures need a project sync (I) | Native | XML | OSC by Wi-Fi or USB |
| OSC/PILOT for iPad 1.15.5 (free; desktop USD 49.99) | An image widget whose file path an OSC message changes. An NDI video widget | "Multi-Touch fully supported" | JSON | OSC by UDP |
| MobMuPlat 1.84 (2019, free) | A live `image <path>`. The file must be on the iPad | Not confirmed | JSON | OSC by UDP |
| Mira 1.2.18 for Max (2020, USD 9.99) | The `fpic` object, fed by Max (I) | Not confirmed | A Max patcher (I) | Wi-Fi or USB, with Max as the bridge (I) |
| Chataigne 1.10.4 dashboard (free, GPL-3.0) | A background image and a Syphon texture streamed to the page. Picture buttons not confirmed | Not confirmed | Not confirmed | A browser and a WebSocket |
| Bitfocus Companion 5.1.3 (free, MIT) with Stream Deck keys | An image library filled by hand. OSC and HTTP set text and colours only | None | JSON or YAML | USB to the keys, or web buttons |
| Stream Deck driven by the instrument itself | A 96 px JPEG on each key of the XL. Elgato publishes the USB protocol | None: keys only | The instrument draws it (I) | USB |
| Sidecar, Duet, Luna: the iPad as a display of the app's own window | The app's own picture | A tap is a click and a drag is a scroll. Finger touch in Sidecar needs macOS 27 | The app draws it | USB |
| A web page that the instrument serves | Any picture or live frame (I) | To be written (I) | To be written | HTTP and a WebSocket. The same Safari risk (I) |

TouchOSC 1.5.4 still has no image control.

Ranking for this use:

1. Open Stage Control, with its version pinned, headless. It is the one route that meets each
   need on paper. Its risks are Safari on iPadOS and the feel of the faders, which nobody has
   measured.
2. Lemur. It has native faders, OSC over USB and a vendor with 3 releases since August 2026.
   Its pictures travel inside the project, so each session needs a sync (I). It has no live
   picture and its source is closed.
3. TouchOSC as it is, with Stream Deck keys for the pictures. Nothing that works today
   changes, and the link is a wire. The pictures leave the iPad. Not seen working.

No route is clearly better than Open Stage Control. Try Lemur if the faders in a browser
disappoint.

Not confirmed: whether iPadOS 26.4 or 27 cures WebKit bug 308073; whether an OSC message can
choose the picture that Lemur draws; where a file must be on an iPad for OSC/PILOT; a wired
IP link for the web routes.


# `.tosc` file schema — what `sharksynth.tosc` actually is

Everything here was derived by reading the shipped file, not from Hexler
documentation. It exists so the next session does not have to derive it again:
in July a pass rebuilt this layout programmatically and did not commit the
generator, and the knowledge was lost — the following session fell back to
asking the owner to hand-paste Lua into the TouchOSC editor.

The tool that encodes all of this is [`build_layout.py`](build_layout.py).
**Edit the layout with that, not by hand.**

```
./build_layout.py roundtrip      # decompress -> recompress, prove byte-identity
./build_layout.py dump -o x.xml  # the decompressed XML
./build_layout.py inventory      # every widget: type / path / frame / OSC binding
./build_layout.py apply          # apply the mutations (idempotent, safe to re-run)
./build_layout.py diff A B       # structural diff of two .tosc files
./build_layout.py test           # run the shipped root Lua against a stub TouchOSC
```

---

## 1. The container

A `.tosc` is **one raw zlib stream and nothing else** — no header, no magic
bytes, no archive, no member table.

| | |
|---|---|
| Bytes on disk | 10994 (10039 before the page split, 9407 before row 7) |
| Decompressed | 364523 characters / 364525 bytes of UTF-8 XML |
| zlib header | `78 da` — CMF/FLG for **level 9**, 32K window |
| Encoding | UTF-8. Exactly one non-ASCII character: an em dash (U+2014) in `gridHeader`'s default text, `SET — TAP A CELL TO LOAD`. So character count and byte count differ by 2 — do not conflate them. |

```python
xml = zlib.decompress(Path("sharksynth.tosc").read_bytes()).decode("utf-8")
blob = zlib.compress(xml.encode("utf-8"), 9)
```

**Level 9 reproduces the shipped file byte for byte.** `build_layout.py
roundtrip` asserts it, and it holds both for the July file and for the one this
pass wrote. Levels 0–8 all produce different (still valid) bytes, so if you
compress at any other level you lose the ability to prove a no-op edit changed
nothing. Always use 9.

## 2. The document

```xml
<?xml version='1.0' encoding='UTF-8'?>
<lexml version="6"><node ID="…" type="GROUP">…</node></lexml>
```

- `lexml` version **6** — TouchOSC MK2.
- Exactly **one** `<node>` under `<lexml>`: the root group. Its `frame` **is the
  canvas size** (`w=640 h=924`); there is no separate document-size record. The
  canvas is the whole document — a page is *not* a canvas of its own (§7), so
  the one number here is what the iPad scales to fit.
- No pretty printing. The whole file is one line apart from newlines *inside*
  the root script's text.
- The single-quoted declaration followed by `\n` is Python `ElementTree`'s
  signature — this file has been machine-generated since at least commit
  9507c4a, and every pass since has preserved the convention.

### Escaping

Text nodes escape **`&`, `<`, `>` only** (`&amp;` `&lt;` `&gt;`). Apostrophes
and double quotes are left **raw** — the file contains 35 unescaped `'`
characters, all inside the Lua. This matches `ElementTree`'s text escaping
exactly. `build_layout.escape()` / `.unescape()` implement it; use them, because
`html.escape` or `xml.sax.saxutils.escape` with entities would corrupt the
round-trip.

## 3. Node shape

Every widget is a `<node>`. Child order is fixed and you can rely on it:

```
<node [ID="uuid"] type="GROUP|BUTTON|FADER|LABEL">
  <includes />        (root only)
  <properties> … </properties>     ← the node's OWN properties, always first
  <values> … </values>
  <messages> … </messages>         (absent on non-interactive containers)
  <children> <node/>… </children>  (GROUP only)
</node>
```

Because a child's `<properties>` can only appear after `<children>`, the **first**
`<properties>` inside a node's span is always its own. `build_layout.Span`
relies on this; it is what makes safe text surgery possible without a full parse.

This layout: **164 nodes** — 86 `BUTTON`, 40 `LABEL`, 24 `FADER`, 14 `GROUP`.

### `ID` attributes are optional

Only **84 of 164** nodes carry `ID="<uuid4>"`. The 80 without are the `gridTab`
children — the SET band, itself added programmatically in commit 636d46f — plus
`ctlTab` and the tab row, and they have loaded on the iPad ever since. **TouchOSC
does not require an ID**; it assigns one on load. New nodes are therefore cloned
ID-less, matching their siblings, rather than inventing UUIDs.

Nor are the IDs that *are* present unique: `layer0` and `gridTab` both carry
`ba40ca28-2f1b-43ab-8095-790280786449` (83 distinct IDs across 84 attributes), a
leftover of the clone that made the SET band. The file has loaded that way for
months, which is the second reason not to treat IDs as identity: **names are the
identity in this document**, and the checks police names, not IDs.

### Properties

```xml
<property type="r"><key>frame</key><value><x>16</x><y>420</y><w>72</w><h>52</h></value></property>
<property type="c"><key>color</key><value><r>0.09</r><g>0.09</g><b>0.11</b><a>1</a></value></property>
<property type="s"><key>name</key><value>cell_0_7</value></property>
<property type="b"><key>visible</key><value>1</value></property>
```

| `type` | Value shape | Keys seen in this layout |
|---|---|---|
| `b` | `0`/`1` | `background` `bar` `cursor` `grabFocus` `grid` `interactive` `locked` `metaActive` `outline` `press` `release` `textClip` `valuePosition` `visible` |
| `i` | integer | `barDisplay` `buttonType` `cursorDisplay` `font` `gridSteps` `orientation` `outlineStyle` `pointerPriority` `response` `responseFactor` `shape` `textAlignH` `textAlignV` `textLength` `textSize` |
| `f` | float | `cornerRadius` |
| `s` | string | `name` `script` |
| `c` | `<r><g><b><a>` 0..1 floats | `color` `gridColor` `textColor` |
| `r` | `<x><y><w><h>` ints | `frame` |

**Frames are relative to the parent group**, and integers. A group **clips** its
children, so growing a group's contents means growing the group too (this is
what forced the canvas change in §6).

**`background=0` means the node paints nothing**, whatever its `color`. The
`layer<i>` groups are authored `background=0` with `color.a=0`, so they cannot
be used as lamps — that is why the strip-state colouring drives the `pause`
button and the `name` label instead.

### Values

```xml
<values><value><key>x</key><locked>0</locked><lockedDefaultCurrent>0</lockedDefaultCurrent>
  <default>0</default><defaultPull>0</defaultPull></value>…</values>
```

Three keys are used: `touch` (all 159 nodes), `x` (108 — faders and buttons),
`text` (38 — labels). `default` on a `text` value is the label's authored
caption.

### Messages

119 `<osc>` blocks, no `<midi>`.

```xml
<messages><osc>
  <enabled>1</enabled><send>1</send><receive>0</receive>
  <feedback>0</feedback><noDuplicates>0</noDuplicates>
  <connections>1111111111</connections>
  <triggers><trigger><var>x</var><condition>RISE</condition></trigger></triggers>
  <path><partial><type>CONSTANT</type><conversion>STRING</conversion>
        <value>/grid/press</value><scaleMin>0</scaleMin><scaleMax>1</scaleMax></partial></path>
  <arguments><partial><type>CONSTANT</type><conversion>INTEGER</conversion>
        <value>0</value>…</partial>
             <partial><type>CONSTANT</type><conversion>INTEGER</conversion>
        <value>7</value>…</partial></arguments>
</osc></messages>
```

- `<enabled>` gates the whole message. It is `1` on all but two: the `tab_<n>`
  buttons keep a well-formed but **disabled** `/tab <n>` message, because a page
  switch is surface-local and the host has no page of its own to hear about.
  Cloning a live message and switching this one flag was safer than inventing a
  messageless `BUTTON`, a shape this file has no example of.
- `<connections>1111111111</connections>` — all ten connection slots, on every
  widget in this layout. That is the "∞" option in the editor, and it is what
  lets the surface work on several networks without reconfiguring.
- `<path>` and `<arguments>` are both lists of `<partial>`s, concatenated.
  `type` is `CONSTANT` (literal `value`) or `VALUE` (the named widget value,
  e.g. `x` or `text`). `conversion` is `STRING` / `INTEGER` / `FLOAT`.
- `<triggers>` conditions in use: `RISE` (71 — buttons firing on press) and
  `ANY` (50 — faders and labels).
- A set cell's `x`/`y` live **only** in its two CONSTANT INTEGER argument
  partials. Renaming `cell_6_6` to `cell_6_7` without also bumping the second
  partial would produce a button that looks right and presses the wrong pad —
  `build_layout.clone_cell` changes both and asserts the shape first.

## 4. The root Lua script

One `<property type="s"><key>script</key><value>…</value></property>` on the
**root group**, escaped as in §2. There is no separate script section, and no
other node in this layout carries one.

Entry points TouchOSC calls: `init()`, `update()` (per frame, ~60 fps),
`onReceiveOSC(message, connections)`.

- `message[1]` is the address string, `message[2]` the argument list;
  `args[i].value` is the payload.
- **Returning `true` consumes the message** — children never see it. Returning
  `false` lets it fall through to the widgets bound to that address. So
  `/layer/<i>/alpha` and `/layer/<i>/pause` must keep falling through to their
  fader and button, while `/layer/<i>/state`, which no widget binds, returns
  `true`. The script's final `return false` is what makes the whole thing work;
  `build_layout.py check` asserts it is still the last statement.

### The duplicate-name hazard

`findByName` searches the **whole tree**, so it is only usable for names that are
globally unique. This layout ships seven names that are not:

| Name | Count | Where |
|---|---|---|
| `name` | 11 | one per `layer0..6` strip **and** one per `agency0..3` slot |
| `fader` | 7 | one per `layer<i>` strip |
| `pause` | 7 | one per `layer<i>` strip |
| `armed` `budget` `force` `forceLabel` | 4 each | one per `agency<i>` slot |

`self:findByName('pause', true)` is therefore ambiguous and must never be used.
Reach these through their parent group instead — since the page split that is
`pageChild('layer'..i).children.pause`, i.e. via `ctlTab` — which is the same
lookup the long-standing `setActive` helper always used, one level deeper. Names
that *are* unique (`cell_<x>_<y>`, `page_<n>`, `intent<i>`, `gridTab`) are safely
reachable by `findByName`, and the existing `/grid/cells`, `/grid/page` and
`/intent/impacts` branches rely on that.

**`findByName(name, true)` recurses to any depth**, which the file proves: the
`/grid/cells` branch has always resolved `cell_<x>_<y>` from the root, and those
cells are `gridTab`'s children, not the root's. That is why the page split did
not disturb the `intent<i>` lookups when it pushed those faders a level down —
only the two `self.children[...]` lookups had to move.

`build_layout.py check` compares the name census before and after an edit and
fails only on a **new** collision, so the seven inherited ones do not drown the
signal.

## 5. Naming conventions

| Pattern | What |
|---|---|
| `layer<i>` | strip group, `i`=0..6; children `name` (LABEL), `fader` (FADER), `pause` (BUTTON) |
| `agency<i>` | controller slot group, `i`=0..3; children `name` `armed` `budget` `force` `forceLabel` |
| `intent<i>` / `intent<i>Label` | pole fader and its caption, `i`=0..6, at root level |
| `ctlTab` | page 1: everything above the SET band — the LAYERS, INTENT and SYNTH bands, wrapped in one group so a page switch can put them all away at once |
| `gridTab` | page 2: the SET band group — self-contained since the day it was written, which is what made the split cheap |
| `tab_<n>` / `tabLabel_<n>` | the page tab row, `n`=**1**-based, at root level below both pages |
| `cell_<x>_<y>` | set pad, `x`=0..7 `y`=0..7, **row-major in document order** |
| `page_<n>` / `pageLabel_<n>` | page buttons, `n`=**1**-based |
| `home` / `homeLabel`, `masterAlpha`, `intentStrength`, `agencyLevel` | singletons |

Note the inconsistency and keep it: **cells are 0-based, pages are 1-based** —
that matches the wire (`/grid/press x y` is 0-based, `/grid/page` is 1-based).

## 6. Grid geometry, and why the canvas grew

Measured from the file, not assumed. Coordinates are relative to `gridTab`,
which sits at `(0, 860)` in the canvas.

| | |
|---|---|
| Cell size | 72 × 52 |
| Column pitch | 76 (x = 16, 92, 168, 244, 320, 396, 472, 548) |
| Row pitch | **56** (y = 28, 84, 140, 196, 252, 308, 364, **420**) |
| Header | `gridHeader` y 4..22 |

`build_layout.grid_geometry()` derives the pitch from the existing rows and
refuses to continue if they are not evenly spaced, so row 7's position is
computed rather than guessed.

**The eighth row did not fit.** Row 7 occupies y 420..472, and the page row was
authored at y 430..470 — a 42 px collision, with `pageLabel_*` at 472..486 right
behind it. Reclaimable slack elsewhere came to only 52 px against the 56 needed
(28 above `gridTab`, 10 below it inside the canvas, 14 of bottom padding inside
`gridTab`), and using all of it would have left zero margins on three edges.

Rather than shrink the pads — which would have rewritten all 56 existing cell
frames and cut the finger target from 52 px to 45 — the page row moved **down
exactly one row pitch**, preserving the vertical rhythm:

| | Before | After |
|---|---|---|
| `page_1..4`, `home` | y 430 | y **486** |
| `pageLabel_1..4`, `homeLabel` | y 472 | y **528** |
| `gridTab` h | 500 | **556** |
| **canvas h** | 1370 | **1426** |

Not one of the 56 original cells moved. The cost was that the canvas got 4 %
taller — and that 4 % is what prompted the page split in §7, which took it back
and a great deal more.

`build_layout.py check` verifies afterwards that no two `gridTab` children
overlap, that the deepest child (542) is inside `gridTab` (556), and that
`gridTab`'s bottom is inside the canvas. Only **direct** children are measured:
a grandchild's frame is in its own parent's coordinate system, so comparing it
with the group two levels up would be meaningless.

## 7. Pages

### What the format offers

MK2 has a native paged container. A **`PAGER`** control holds `GROUP` children —
its pages — and shows one at a time:

| Where | Key | Type | What |
|---|---|---|---|
| pager | `tabbar` | `b` | show the tab bar |
| pager | `tabbarSize` | `i` | its thickness |
| pager | `tabbarDoubleTap` | `b` | require a double tap to change page |
| pager | `tabLabels` | `b` | draw each page's `tabLabel` on the bar |
| pager | `textSizeOff` / `textSizeOn` | `i` | tab-bar font sizes |
| pager | value `page` | `i` | the active page, 0-based |
| page (a `GROUP`) | `tabLabel` | `s` | its caption on the bar |
| page | `tabColorOff` / `tabColorOn` | `c` | its tab-bar button |
| page | `textColorOff` / `textColorOn` | `c` | its tab-bar caption |

**A page is not a canvas.** The root frame is still the only document size, so
pages do not each get their own; what they buy is that the canvas only has to be
as tall as the *tallest* page instead of all of them stacked.

### Why this file does not use one

**Nothing in this document is a `PAGER`**, so unlike every other edit the tool
makes, a pager could not be cloned from a proven example — it would have to be
invented. And the published property tables are demonstrably incomplete against
this very file: they list 15 properties for `GROUP` and omit `shape`, which all
164 nodes here carry, and `metaActive`, which the root carries. A layout that
fails to load is only discovered on the iPad, and this tool cannot get there.

So the split is built from vocabulary the file already proves:

- **two sibling `GROUP`s in the same rect** — `ctlTab` (0,0,640,840) and
  `gridTab` (0,0,640,556) — with exactly one `visible` at a time;
- **a tab row below both**, `tab_<n>` / `tabLabel_<n>`, cloned from the set-page
  row with `<enabled>0</enabled>`;
- **the root Lua** doing the switching, via the same `.visible` mechanism
  `/layer/<i>/active` has driven since the surface was written.

The device behaviour this rests on: *"For a control to be associated with a
pointer, its `visible` and `interactive` properties have to both be `true`"* —
Hexler's scripting manual. The page underneath therefore cannot be pressed
through the one on top. Note that `interactive` does **not** cascade — every
`layer<i>` group here is authored `interactive=0` while its fader and button
work fine — so this is a claim about `visible` specifically, and it is the one
thing on the iPad worth checking first.

### Geometry

| | Before | After |
|---|---|---|
| `ctlTab` | — | (0, 0, 640, **840**) — 832 deepest child + the band's own 8px top margin |
| `gridTab` | (0, **860**, 640, 556) | (0, **0**, 640, 556) |
| `tab_1` / `tab_2` | — | y **854**, 300 wide at x 16 / 320 |
| `tabLabel_1` / `tabLabel_2` | — | y **896** (the set-page row's own +42) |
| **canvas h** | 1426 | **924** |

Every one of the 82 re-parented nodes kept its frame **unchanged**: they were
already relative to the root's origin, and `ctlTab` sits at (0,0).

924 is close to as far as this is worth taking. The iPad scales the canvas to
fit, so while the canvas is taller than the screen's aspect ratio the gain is
1426/924 = **1.54x**; a 640-wide canvas stops being height-limited at about
h=853 on a 4:3 iPad, so a third page — splitting LAYERS off from INTENT, the
other half of the original question — would buy roughly 8 % more and cost the
performer the two bands side by side. Two pages is where the return is.

## 8. How to edit safely

The rule that makes this reviewable: **modify the existing XML as text.** Do not
re-serialise it with ElementTree, even though ElementTree wrote it originally —
attribute order, self-closing-tag style and escaping are all reproducible only by
accident, and a diff you cannot read is a diff you cannot trust.

`build_layout.py` therefore splices at exact anchors and asserts each anchor is
unique before using it, and uses ElementTree only to parse, validate and
inventory. The proof that this works:

1. `roundtrip` — decompress and recompress with no edits gives back the exact
   original bytes.
2. Reverse-apply — undo each mutation by hand on the edited XML and you get the
   previous file back **character for character**, recompressing to its exact
   previous byte count. It has now held twice: the eighth row reproduced the
   July file at 9407 bytes, and the page split reproduced the row-7 file at
   10039. Anything the tool had disturbed in passing would show up here.
3. `test` — the root Lua is a third of what this layout *is*, and nothing else
   in the repo runs it. [`test_script.lua`](test_script.lua) stubs enough of
   TouchOSC to execute the shipped script against the layout's real node tree,
   and drives every branch through the addresses the host actually sends. It is
   what proved the page split had not orphaned a lookup: reverting either of the
   two rebased ones fails it in three places.

All three are cheap. Run them.

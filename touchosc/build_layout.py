#!/usr/bin/env python3
"""Programmatic surgery on sharksynth.tosc, the TouchOSC iPad control surface.

The .tosc is a single zlib stream wrapping one XML document (see SCHEMA.md).
It is the owner's live performance surface: positions, colours, the hues matched
to the Novation axis pairs and the /sync handshake are all hand-tuned. So this
tool NEVER regenerates the layout -- it edits the existing XML **as text**,
splicing at exact anchors, so every byte it does not deliberately touch survives
unchanged. ElementTree is used only to parse, validate and inventory; never to
re-serialise the document.

    ./build_layout.py roundtrip           # decompress -> recompress, prove identity
    ./build_layout.py dump -o out.xml     # the decompressed XML
    ./build_layout.py inventory           # widget tree: type / name / frame / OSC
    ./build_layout.py apply               # apply every mutation below (idempotent)
    ./build_layout.py apply --dry-run     # ...and report without writing
    ./build_layout.py diff A B            # structural diff of two .tosc or .xml
    ./build_layout.py test                # run the shipped root Lua (needs `lua`)

Mutations `apply` performs, each idempotent and independently skippable:

  state-lamps   the /layer/<n>/state branch in the root group's Lua, colouring
                each strip's pause button and name label (amber parked / green
                playing / dim armed-and-waiting) from the host's bit-packed int.
  grid-row-7    the eighth row of set cells: cell_<x>_7 for x=0..7, cloned from
                row 6 byte-for-byte with only name, frame.y and the OSC y
                argument changed.
  grid-clamp    the root script's /grid/cells loop, 56 -> 64 cells.
  grid-reflow   makes room for row 7: the page row, its labels and HOME move
                down exactly one row pitch; gridTab and the canvas grow to match.
                Nothing overlaps and no existing cell moves.  See SCHEMA.md
                "Why the canvas grew" for the measurements.
  page-split    the second page: everything above the SET band is wrapped in a
                new `ctlTab` group and gridTab moves up onto the same rect, so
                the two are alternatives rather than a 1426px column.
  page-tabs     the CONTROL / SET tab row beneath both pages, cloned from the
                set-page row with its OSC message disabled.
  page-canvas   shrinks the canvas onto the taller page (1426 -> 924), which is
                the whole point: every widget renders ~1.54x larger.
  page-script   showPage()/pageChild() in the root Lua, the tab poll in
                update(), and the strip/slot lookups rebased through ctlTab.

  v3-pages      (2026-10-02) the three-tab surface, SET / MIX / LIVE: the SET grid
                as four framed quadrants of named swatch pads under transparent
                touch buttons, 16 page buttons and an outlined HOME; MIX with
                taller faders and two-line names; LIVE with the response faders,
                four input trims and eight agency slots.  Supersedes the grid
                and page mutations above, which skip once it is in.
  v3-script     the root Lua for it, replaced whole.

Everything is recoverable from git -- the .tosc is versioned, so this tool
writes no backup files.  `git checkout touchosc/sharksynth.tosc` undoes it.
"""

from __future__ import annotations

import argparse
import difflib
import re
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
DEFAULT_TOSC = HERE / "sharksynth.tosc"

# The layout was first emitted by Python's ElementTree, and every later pass has
# kept its conventions: single-quoted XML declaration + newline, no pretty
# printing, and text escaped for & < > only (apostrophes and quotes left raw).
XML_DECL = "<?xml version='1.0' encoding='UTF-8'?>\n"
# Level 9 reproduces the shipped file byte for byte; see `roundtrip`.
ZLIB_LEVEL = 9


# --------------------------------------------------------------------------- #
# container: zlib in, zlib out
# --------------------------------------------------------------------------- #

def read_xml(path: Path) -> str:
    """Decompressed XML text of a .tosc (a .xml path is passed through)."""
    blob = path.read_bytes()
    if path.suffix == ".xml" or blob.lstrip()[:5] == b"<?xml":
        return blob.decode("utf-8")
    return zlib.decompress(blob).decode("utf-8")


def write_tosc(path: Path, xml: str) -> bytes:
    data = zlib.compress(xml.encode("utf-8"), ZLIB_LEVEL)
    path.write_bytes(data)
    return data


def escape(text: str) -> str:
    """Escape exactly as ElementTree escapes a text node -- and as this file does."""
    return text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def unescape(text: str) -> str:
    return text.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&")


# --------------------------------------------------------------------------- #
# raw-text node index
# --------------------------------------------------------------------------- #

_NODE_TOKEN = re.compile(r"<node[\s>]|</node>")


class Span:
    """One <node>...</node> region of the raw XML, with its own properties."""

    __slots__ = ("start", "end", "depth", "xml")

    def __init__(self, xml: str, start: int, end: int, depth: int):
        self.xml, self.start, self.end, self.depth = xml, start, end, depth

    @property
    def text(self) -> str:
        return self.xml[self.start:self.end]

    @property
    def own_properties(self) -> tuple[int, int]:
        """Span of this node's OWN <properties> block.

        Safe because the serialisation order is fixed: <node> [<includes/>]
        <properties> <values> [<messages>] [<children>].  A child's properties
        can only appear after <children>, i.e. after this first match.
        """
        i = self.xml.index("<properties>", self.start, self.end)
        j = self.xml.index("</properties>", i, self.end) + len("</properties>")
        return i, j

    def prop(self, key: str) -> str | None:
        """Raw inner text of one property's <value>, or None."""
        lo, hi = self.own_properties
        m = re.search(
            r"<property type=\"[a-z]\"><key>" + re.escape(key) + r"</key><value>(.*?)</value></property>",
            self.xml[lo:hi], re.DOTALL,
        )
        return m.group(1) if m else None

    @property
    def name(self) -> str | None:
        return self.prop("name")

    @property
    def type(self) -> str:
        m = re.match(r"<node[^>]*type=\"([A-Z]+)\"", self.text)
        return m.group(1) if m else "?"

    @property
    def frame(self) -> dict[str, int] | None:
        raw = self.prop("frame")
        if raw is None:
            return None
        return {k: int(v) for k, v in re.findall(r"<([xywh])>(-?\d+)</\1>", raw)}


def index_nodes(xml: str) -> list[Span]:
    """Every node span, in document order of its closing tag."""
    stack: list[int] = []
    spans: list[Span] = []
    for m in _NODE_TOKEN.finditer(xml):
        if m.group(0) == "</node>":
            start = stack.pop()
            spans.append(Span(xml, start, m.end(), len(stack)))
        else:
            stack.append(m.start())
    if stack:
        raise ValueError("unbalanced <node> tags")
    return sorted(spans, key=lambda s: s.start)


def find_node(xml: str, name: str) -> Span:
    """The single node whose own `name` property is `name`."""
    hits = [s for s in index_nodes(xml) if s.name == name]
    if len(hits) != 1:
        raise KeyError(f"expected exactly one node named {name!r}, found {len(hits)}")
    return hits[0]


# --------------------------------------------------------------------------- #
# property edits
# --------------------------------------------------------------------------- #

def set_frame(xml: str, node: "str | Span", **fields: int) -> str:
    """Rewrite named components of one node's frame, leaving the rest alone."""
    span = find_node(xml, node) if isinstance(node, str) else node
    node_name = span.name
    frame = span.frame
    if frame is None:
        raise KeyError(f"{node_name} has no frame")
    frame.update(fields)
    lo, hi = span.own_properties
    old = re.search(
        r"<property type=\"r\"><key>frame</key><value>.*?</value></property>",
        xml[lo:hi],
    )
    if not old:
        raise KeyError(f"{node_name}: no frame property")
    new = (
        '<property type="r"><key>frame</key><value>'
        f"<x>{frame['x']}</x><y>{frame['y']}</y><w>{frame['w']}</w><h>{frame['h']}</h>"
        "</value></property>"
    )
    a, b = lo + old.start(), lo + old.end()
    return xml[:a] + new + xml[b:]


def get_script(xml: str) -> tuple[str, int, int]:
    """The root group's Lua, unescaped, plus the span of its escaped form."""
    key = "<key>script</key><value>"
    i = xml.index(key) + len(key)
    j = xml.index("</value>", i)
    return unescape(xml[i:j]), i, j


def set_script(xml: str, lua: str) -> str:
    _, i, j = get_script(xml)
    return xml[:i] + escape(lua) + xml[j:]


# --------------------------------------------------------------------------- #
# mutation: the /layer/<n>/state lamps
# --------------------------------------------------------------------------- #

# Verbatim from README.md's "What the layout does with it".  Reached through the
# strip group rather than findByName because `pause` and `name` each occur seven
# times; `self.children[...]` is the same lookup setActive already relies on.
STATE_BRANCH = """\
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
"""

# Anchor: the last of the two existing /active branches.  Putting `state` right
# after it keeps the /layer/... handling together and leaves the fall-through to
# `return false` -- and therefore /layer/<i>/alpha and /pause reaching their
# widgets -- exactly as it was.
STATE_ANCHOR = "  local a = path:match('^/agency/(%d+)/active$')\n" \
               "  if a then setActive('agency', a, message[2]); return true end\n"


def mutate_state_lamps(xml: str) -> tuple[str, str]:
    lua, _, _ = get_script(xml)
    if "/layer/(%d+)/state" in lua:
        return xml, "state-lamps: already present, skipped"
    if STATE_ANCHOR not in lua:
        raise RuntimeError("state-lamps: anchor (the /agency/<n>/active branch) not found")
    lua = lua.replace(STATE_ANCHOR, STATE_ANCHOR + STATE_BRANCH, 1)
    return set_script(xml, lua), "state-lamps: inserted after the /agency/<n>/active branch"


# --------------------------------------------------------------------------- #
# mutation: the /grid/cells clamp, 56 -> 64
# --------------------------------------------------------------------------- #

CLAMP_EDITS = [
    ("    for i = 1, math.min(#args, 56) do",
     "    for i = 1, math.min(#args, 64) do"),
    ("    -- Set-pages grid (tab 2). One host message, 56 packed 0xRRGGBB ints in",
     "    -- Set-pages grid (tab 2). One host message, 64 packed 0xRRGGBB ints in"),
    ("    -- row-major order (y=0..6, x=0..7); recolour each cell_<x>_<y>. 0 = dark.",
     "    -- row-major order (y=0..7, x=0..7); recolour each cell_<x>_<y>. 0 = dark."),
]


def mutate_grid_clamp(xml: str) -> tuple[str, str]:
    lua, _, _ = get_script(xml)
    if "math.min(#args, 64)" in lua:
        return xml, "grid-clamp: already 64, skipped"
    for old, new in CLAMP_EDITS:
        if old not in lua:
            raise RuntimeError(f"grid-clamp: expected line not found: {old.strip()!r}")
        lua = lua.replace(old, new, 1)
    return set_script(xml, lua), "grid-clamp: /grid/cells loop 56 -> 64 (comments too)"


# --------------------------------------------------------------------------- #
# mutation: the eighth row of set cells
# --------------------------------------------------------------------------- #

def grid_geometry(xml: str) -> dict:
    """Derive the cell pitch and the next row's y from the rows that exist."""
    frames = {}
    for span in index_nodes(xml):
        m = re.fullmatch(r"cell_(\d+)_(\d+)", span.name or "")
        if m:
            frames[(int(m.group(1)), int(m.group(2)))] = span.frame
    if not frames:
        raise RuntimeError("no cell_<x>_<y> buttons found")
    rows = sorted({y for _, y in frames})
    cols = sorted({x for x, _ in frames})
    ys = [frames[(cols[0], y)]["y"] for y in rows]
    pitches = {b - a for a, b in zip(ys, ys[1:])}
    if len(pitches) != 1:
        raise RuntimeError(f"cell rows are not evenly pitched: {sorted(pitches)}")
    pitch = pitches.pop()
    heights = {f["h"] for f in frames.values()}
    if len(heights) != 1:
        raise RuntimeError(f"cell heights differ: {sorted(heights)}")
    return {
        "rows": rows, "cols": cols, "pitch": pitch,
        "height": heights.pop(), "last_row": rows[-1],
        "last_row_y": ys[-1], "next_row_y": ys[-1] + pitch,
        "frames": frames,
    }


def clone_cell(src: str, x: int, src_y: int, dst_y: int, y_px: int) -> str:
    """One row-6 button re-badged for row 7: name, frame.y and the OSC y arg."""
    out = src
    old_name, new_name = f"cell_{x}_{src_y}", f"cell_{x}_{dst_y}"
    anchor = f"<key>name</key><value>{old_name}</value>"
    if out.count(anchor) != 1:
        raise RuntimeError(f"{old_name}: name anchor not unique")
    out = out.replace(anchor, f"<key>name</key><value>{new_name}</value>", 1)

    frame = re.search(r"<key>frame</key><value><x>(-?\d+)</x><y>(-?\d+)</y>"
                      r"<w>(\d+)</w><h>(\d+)</h></value>", out)
    if not frame:
        raise RuntimeError(f"{old_name}: frame not found")
    out = out[:frame.start()] + (
        f"<key>frame</key><value><x>{frame.group(1)}</x><y>{y_px}</y>"
        f"<w>{frame.group(3)}</w><h>{frame.group(4)}</h></value>"
    ) + out[frame.end():]

    # <arguments> is two CONSTANT INTEGER partials: the cell's x then its y.
    def partial(v: int) -> str:
        return ("<partial><type>CONSTANT</type><conversion>INTEGER</conversion>"
                f"<value>{v}</value><scaleMin>0</scaleMin><scaleMax>1</scaleMax></partial>")

    old_args = f"<arguments>{partial(x)}{partial(src_y)}</arguments>"
    new_args = f"<arguments>{partial(x)}{partial(dst_y)}</arguments>"
    if out.count(old_args) != 1:
        raise RuntimeError(f"{old_name}: /grid/press arguments not in the expected shape")
    return out.replace(old_args, new_args, 1)


def mutate_grid_row(xml: str) -> tuple[str, str]:
    geo = grid_geometry(xml)
    src_y, dst_y = geo["last_row"], geo["last_row"] + 1
    if dst_y >= 8:
        return xml, f"grid-row: rows 0..{src_y} already present, skipped"

    tail = find_node(xml, f"cell_{geo['cols'][-1]}_{src_y}")
    clones = "".join(
        clone_cell(find_node(xml, f"cell_{x}_{src_y}").text, x, src_y, dst_y, geo["next_row_y"])
        for x in geo["cols"]
    )
    xml = xml[:tail.end] + clones + xml[tail.end:]
    return xml, (f"grid-row: added cell_0_{dst_y}..cell_{geo['cols'][-1]}_{dst_y} "
                 f"at y={geo['next_row_y']} (pitch {geo['pitch']}, h {geo['height']}), "
                 f"cloned from row {src_y}")


# --------------------------------------------------------------------------- #
# mutation: reflow so the new row does not land on the page buttons
# --------------------------------------------------------------------------- #

REFLOW_MOVES = ["page_1", "pageLabel_1", "page_2", "pageLabel_2",
                "page_3", "pageLabel_3", "page_4", "pageLabel_4",
                "home", "homeLabel"]


def mutate_grid_reflow(xml: str) -> tuple[str, str]:
    geo = grid_geometry(xml)
    pitch = geo["pitch"]
    # Where the bottom cell row ends once row 7 exists -- whether or not it does
    # yet, so the reflow can run before or after grid-row-7 and settles either way.
    wanted = max(geo["last_row"], 7)
    bottom = geo["frames"][(geo["cols"][0], geo["rows"][0])]["y"] \
        + wanted * pitch + geo["height"]
    page = find_node(xml, "page_1").frame
    if page["y"] >= bottom:
        return xml, "grid-reflow: page row already clears the last cell row, skipped"

    for name in REFLOW_MOVES:
        f = find_node(xml, name).frame
        xml = set_frame(xml, name, y=f["y"] + pitch)
    grid = find_node(xml, "gridTab").frame
    xml = set_frame(xml, "gridTab", h=grid["h"] + pitch)
    root_frame = index_nodes(xml)[0].frame
    xml = set_frame(xml, index_nodes(xml)[0], h=root_frame["h"] + pitch)
    return xml, (f"grid-reflow: page row + HOME moved down {pitch}px "
                 f"({page['y']} -> {page['y'] + pitch}); gridTab h {grid['h']} -> "
                 f"{grid['h'] + pitch}; canvas h {root_frame['h']} -> {root_frame['h'] + pitch}")


# --------------------------------------------------------------------------- #
# mutation: two pages -- the control bands and the SET grid share one rect
# --------------------------------------------------------------------------- #

# MK2 *does* have a native paged container: a PAGER control whose children are
# page GROUPs, each carrying tabLabel / tabColorOn / tabColorOff / textColorOn /
# textColorOff, with an integer `page` value on the pager itself.  This document
# contains no PAGER, so every byte of one would have to be invented rather than
# cloned -- and the published property tables are provably incomplete against
# this very file (they omit `shape`, which all 159 nodes here carry, and
# `metaActive`, which the root carries).  See SCHEMA.md "Pages".
#
# So the split is built from vocabulary the file already proves: two sibling
# GROUPs occupying the same rect, exactly one visible, switched by the root Lua
# -- the same `.visible` mechanism /layer/<i>/active has driven for months.
# Promoting the pair to a real PAGER is an editor operation; this tool can adopt
# the result afterwards.

PAGE_GROUP = "ctlTab"       # page 1: LAYERS / INTENT / SYNTH
GRID_GROUP = "gridTab"      # page 2: the SET grid, already self-contained
PAGE_GROUPS = (PAGE_GROUP, GRID_GROUP)

# All four numbers are gridTab's own page-row rhythm, reused so the tab row
# reads as the same kind of object as the set-page row it sits under.
TAB_GAP = 14                # gridTab: last cell row 472 -> page_1 486
TAB_LABEL_OFFSET = 42       # gridTab: page_1 486 -> pageLabel_1 528
TAB_MARGIN = 14             # gridTab: pageLabel_1 bottom 542 -> group bottom 556
TAB_X = (16, 320)           # the layout's 16px side margins, split in two
TAB_W = 300
TAB_TEXT = ("CONTROL", "SET")

# Cool grey, deliberately not the amber the set-page row uses: a tab is a
# different kind of switch from a set page, and the two rows sit near each other.
TAB_ON, TAB_OFF = (0.8, 0.84, 0.92, 1), (0.2, 0.21, 0.26, 1)
TAB_TEXT_ON, TAB_TEXT_OFF = (0.85, 0.85, 0.9, 1), (0.4, 0.42, 0.48, 1)


def root_children(xml: str) -> list[Span]:
    return [s for s in index_nodes(xml) if s.depth == 1]


def direct_children(xml: str, parent: Span) -> list[Span]:
    """Spans one level below `parent` -- the only ones whose frames are in its
    coordinate system.  A grandchild's frame is relative to its own group, so
    measuring containment against those would be meaningless."""
    return [s for s in index_nodes(xml)
            if parent.start < s.start < parent.end and s.depth == parent.depth + 1]


def set_bool(xml: str, node: "str | Span", key: str, on: bool) -> str:
    span = find_node(xml, node) if isinstance(node, str) else node
    lo, hi = span.own_properties
    pat = '<property type="b"><key>' + re.escape(key) + r"</key><value>[01]</value></property>"
    m = re.search(pat, xml[lo:hi])
    if not m:
        raise KeyError(f"{span.name}: no boolean property {key!r}")
    new = f'<property type="b"><key>{key}</key><value>{1 if on else 0}</value></property>'
    return xml[:lo + m.start()] + new + xml[lo + m.end():]


def _colour(key: str, rgba) -> str:
    r, g, b, a = rgba
    return (f'<property type="c"><key>{key}</key><value>'
            f"<r>{r}</r><g>{g}</g><b>{b}</b><a>{a}</a></value></property>")


def _swap_colour(node_xml: str, key: str, rgba: tuple) -> str:
    pat = ('<property type="c"><key>' + re.escape(key)
           + r"</key><value><r>[-\d.]+</r><g>[-\d.]+</g><b>[-\d.]+</b><a>[-\d.]+</a></value></property>")
    hits = re.findall(pat, node_xml)
    if len(hits) != 1:
        raise RuntimeError(f"page-tabs: {key} colour not unique in the clone source")
    return re.sub(pat, _colour(key, rgba), node_xml, count=1)


def _swap_frame(node_xml: str, x: int, y: int, w: int, h: int) -> str:
    pat = r"<key>frame</key><value><x>-?\d+</x><y>-?\d+</y><w>\d+</w><h>\d+</h></value>"
    if len(re.findall(pat, node_xml)) != 1:
        raise RuntimeError("page-tabs: frame not unique in the clone source")
    return re.sub(pat, f"<key>frame</key><value><x>{x}</x><y>{y}</y><w>{w}</w><h>{h}</h></value>",
                  node_xml, count=1)


def _swap_name(node_xml: str, old: str, new: str) -> str:
    anchor = f"<key>name</key><value>{old}</value>"
    if node_xml.count(anchor) != 1:
        raise RuntimeError(f"page-tabs: name anchor {old!r} not unique")
    return node_xml.replace(anchor, f"<key>name</key><value>{new}</value>", 1)


def mutate_page_split(xml: str) -> tuple[str, str]:
    """Wrap the control bands in their own group, and stack the SET grid on it."""
    if any(s.name == PAGE_GROUP for s in index_nodes(xml)):
        return xml, f"page-split: {PAGE_GROUP} already present, skipped"

    kids = root_children(xml)
    if kids[-1].name != GRID_GROUP:
        raise RuntimeError(f"page-split: expected {GRID_GROUP} to be the last root child, "
                           f"found {kids[-1].name!r}")
    moving = kids[:-1]
    top = min(k.frame["y"] for k in moving)
    bottom = max(k.frame["y"] + k.frame["h"] for k in moving)
    height = bottom + top          # mirror the band's top margin below it
    root = index_nodes(xml)[0]
    width = root.frame["w"]

    open_tag = "<children>"
    i = xml.index(open_tag, root.start) + len(open_tag)
    if i != moving[0].start:
        raise RuntimeError("page-split: the root's <children> does not open on its first child")

    # The wrapper is layer0's own <properties>/<values> verbatim -- a transparent,
    # non-interactive container whose every key, type and value this file already
    # proves -- with only the name changed here and the frame set below.  No ID:
    # 75 nodes already ship without one (SCHEMA.md "ID attributes are optional"),
    # and layer0's ID is in any case not unique (gridTab carries the same UUID).
    template = find_node(xml, "layer0")
    lo, hi = template.own_properties
    props = _swap_name(xml[lo:hi], "layer0", PAGE_GROUP)
    values = xml[hi:xml.index("<children>", hi, template.end)]

    moved = xml[i:moving[-1].end]
    xml = (xml[:i] + f'<node type="GROUP">{props}{values}<children>'
           + moved + "</children></node>" + xml[moving[-1].end:])

    xml = set_frame(xml, PAGE_GROUP, x=0, y=0, w=width, h=height)
    xml = set_frame(xml, GRID_GROUP, y=0)          # the two pages share one rect
    xml = set_bool(xml, GRID_GROUP, "visible", False)   # CONTROL is the page on load
    return xml, (f"page-split: {len(moving)} root children wrapped in {PAGE_GROUP} "
                 f"(0,0,{width},{height}); {GRID_GROUP} y 860 -> 0, visible -> 0. "
                 f"No moved child's frame changed -- they were already relative to (0,0)")


def mutate_page_tabs(xml: str) -> tuple[str, str]:
    """The always-visible tab row under the two pages."""
    if any(s.name == "tab_1" for s in index_nodes(xml)):
        return xml, "page-tabs: tab row already present, skipped"
    if not all(any(s.name == n for s in index_nodes(xml)) for n in PAGE_GROUPS):
        return xml, "page-tabs: no page split yet, skipped"

    pages = [find_node(xml, n) for n in PAGE_GROUPS]
    y = max(p.frame["y"] + p.frame["h"] for p in pages) + TAB_GAP
    btn_src, lab_src = find_node(xml, "page_1").text, find_node(xml, "pageLabel_1").text
    lab_h = find_node(xml, "pageLabel_1").frame["h"]
    btn_h = find_node(xml, "page_1").frame["h"]

    # The set-page row is the right thing to clone: same size, same corner
    # radius, same outline, and its OSC block is the shape we need to neutralise.
    # These tabs are surface-local -- the host has no page of its own to hear
    # about -- so the message is kept for shape and switched off at <enabled>.
    old_args = ("<arguments><partial><type>CONSTANT</type><conversion>INTEGER</conversion>"
                "<value>1</value><scaleMin>0</scaleMin><scaleMax>1</scaleMax></partial></arguments>")
    nodes = []
    for i, (x, text) in enumerate(zip(TAB_X, TAB_TEXT), start=1):
        b = _swap_name(btn_src, "page_1", f"tab_{i}")
        b = _swap_frame(b, x, y, TAB_W, btn_h)
        b = _swap_colour(b, "color", TAB_ON if i == 1 else TAB_OFF)
        for old, new in (("<enabled>1</enabled>", "<enabled>0</enabled>"),
                         ("<value>/grid/page</value>", "<value>/tab</value>"),
                         (old_args, old_args.replace("<value>1</value>", f"<value>{i}</value>"))):
            if b.count(old) != 1:
                raise RuntimeError(f"page-tabs: {old!r} not unique in the page_1 clone")
            b = b.replace(old, new, 1)

        lb = _swap_name(lab_src, "pageLabel_1", f"tabLabel_{i}")
        lb = _swap_frame(lb, x, y + TAB_LABEL_OFFSET, TAB_W, lab_h)
        lb = _swap_colour(lb, "textColor", TAB_TEXT_ON if i == 1 else TAB_TEXT_OFF)
        caption = "<key>text</key><locked>0</locked><lockedDefaultCurrent>1</lockedDefaultCurrent>" \
                  "<default>1</default>"
        if lb.count(caption) != 1:
            raise RuntimeError("page-tabs: the label's text default is not in the expected shape")
        lb = lb.replace(caption, caption.replace("<default>1</default>",
                                                 f"<default>{escape(text)}</default>"), 1)
        nodes += [b, lb]

    # Last in document order, so the tab row draws above whichever page is up.
    tail = find_node(xml, GRID_GROUP).end
    xml = xml[:tail] + "".join(nodes) + xml[tail:]
    return xml, (f"page-tabs: tab_1/tabLabel_1 {TAB_TEXT[0]!r} and tab_2/tabLabel_2 "
                 f"{TAB_TEXT[1]!r} added at root, y={y} (buttons) / {y + TAB_LABEL_OFFSET} "
                 f"(labels), cloned from the set-page row with OSC disabled")


def mutate_page_canvas(xml: str) -> tuple[str, str]:
    """Shrink the canvas onto the taller page -- the whole point of the split."""
    if not any(s.name == PAGE_GROUP for s in index_nodes(xml)):
        return xml, "page-canvas: no page split yet, skipped"
    root = index_nodes(xml)[0]
    deepest = max(k.frame["y"] + k.frame["h"] for k in root_children(xml))
    want = deepest + TAB_MARGIN
    if root.frame["h"] == want:
        return xml, f"page-canvas: canvas already {root.frame['w']}x{want}, skipped"
    was = root.frame["h"]
    xml = set_frame(xml, index_nodes(xml)[0], h=want)
    return xml, (f"page-canvas: canvas h {was} -> {want} (deepest root child {deepest} "
                 f"+ {TAB_MARGIN} margin) -- widgets render {was / want:.2f}x larger")


# --------------------------------------------------------------------------- #
# mutation: the page switcher in the root Lua
# --------------------------------------------------------------------------- #

PAGE_BLOCK = """
-- Pages. The surface is two full-height pages sharing one rect at the top of the
-- canvas -- ctlTab (LAYERS / INTENT / SYNTH) and gridTab (the SET grid) -- with
-- exactly one visible at a time and a tab row below them that always is. That is
-- what buys each page the whole screen, instead of every band being scaled down
-- to fit one 1426px canvas. TouchOSC associates a pointer with a control only
-- when its visible property is true, so the page that is put away cannot be
-- pressed through the one on top.
local PAGES = {'ctlTab', 'gridTab'}
local currentPage = 1

local function showPage(p)
  currentPage = p
  for i = 1, #PAGES do
    local on = (i == p)
    local page = self.children[PAGES[i]]
    if page then page.visible = on end
    local tab = self.children['tab_' .. i]
    if tab then
      if on then tab.color = Color(0.80, 0.84, 0.92, 1.0)
      else tab.color = Color(0.20, 0.21, 0.26, 1.0) end
    end
    local label = self.children['tabLabel_' .. i]
    if label then
      if on then label.textColor = Color(0.85, 0.85, 0.90, 1.0)
      else label.textColor = Color(0.40, 0.42, 0.48, 1.0) end
    end
  end
end

-- The layer strips and the agency slots sit on page 1 now, so they are the
-- root's GRANDchildren. Reach them through the page group and never by name:
-- `fader`, `pause` and `name` are not unique (see SCHEMA.md).
local function pageChild(name)
  local page = self.children[PAGES[1]]
  return page and page.children[name] or nil
end
"""

PAGE_SCRIPT_EDITS = [
    # showPage and pageChild must exist before init() and setActive close over them.
    ("local frameCount = 0\n",
     "local frameCount = 0\n" + PAGE_BLOCK),
    ("function init()\n  sendOSC('/sync')\nend\n",
     "function init()\n  showPage(1)\n  sendOSC('/sync')\nend\n"),
    ("""    sendOSC('/sync')
  end
end
""",
     """    sendOSC('/sync')
  end
  -- Page tabs. A child button cannot call back into the root script, so poll the
  -- two of them here instead: a finger holds x = 1 for several frames at 60fps,
  -- and the currentPage guard makes the switch fire once per press.
  for i = 1, #PAGES do
    local tab = self.children['tab_' .. i]
    if tab and tab.values.x == 1 and i ~= currentPage then showPage(i) end
  end
end
"""),
    ("  local node = self.children[prefix .. n]\n",
     "  local node = pageChild(prefix .. n)\n"),
    ("    local strip = self.children['layer' .. s]\n",
     "    local strip = pageChild('layer' .. s)\n"),
]


def mutate_page_script(xml: str) -> tuple[str, str]:
    lua, _, _ = get_script(xml)
    if "showPage" in lua:
        return xml, "page-script: page switcher already present, skipped"
    for old, new in PAGE_SCRIPT_EDITS:
        if lua.count(old) != 1:
            raise RuntimeError(f"page-script: anchor not unique ({lua.count(old)} hits): "
                               f"{old.strip().splitlines()[0]!r}")
        lua = lua.replace(old, new, 1)
    return set_script(xml, lua), ("page-script: showPage/pageChild spliced in; init() shows "
                                  "page 1; update() polls the tabs; setActive and the state "
                                  "lamps now reach the strips through ctlTab")


# --------------------------------------------------------------------------- #
# v3: three tabs -- SET / MIX / LIVE (2026-10-02)
# --------------------------------------------------------------------------- #
#
# The owner's audit of the two-tab surface: the set page row out-shouted the
# pads (amber against pads the host had dimmed and TouchOSC dimmed again), the
# pads could not be told apart or placed in their 4x4 quadrant, names were
# truncated everywhere, four agency slots could not hold the eight a config
# carries, four page buttons could not reach a five-page set, and the audio
# trims lived only in the desktop GUI.
#
# Unlike the earlier passes this one re-lays-out every page, so it works on
# whole page groups: each is split into its head and its child nodes, the
# children are edited (frames, a property or two) or cloned, and the group is
# joined back. Every existing widget keeps its bytes apart from the properties
# named here -- OSC bindings, hues and fader styling survive -- and every new
# widget is a clone of an existing one of the same kind, so nothing is
# invented. Clones drop their ID attribute (optional; see SCHEMA.md).

V3_PAGES = ("gridTab", "ctlTab", "liveTab")          # SET, MIX, LIVE -- tab order
V3_TAB_TEXT = ("SET", "MIX", "LIVE")
V3_PAGE_H = 840                                       # every page fills the rect
V3_TAB_X, V3_TAB_W = (16, 222, 428), 196

# SET tab geometry. Two quadrant columns 296 wide with a 16px gutter fill the
# 608px between the 16px margins; inside a quadrant, a 6px inset and a 72x66
# pad pitch leave a 4px gap between 68x62 pads.
Q_X, Q_Y, Q_W, Q_H = (16, 328), (56, 356), 296, 272
Q_NAME_Y = (34, 334)
PAD_INSET, PAD_PITCH_X, PAD_PITCH_Y, PAD_W, PAD_H = 6, 72, 66, 68, 62
PAGE_ROW_Y, PAGE_ROW_PITCH, PAGE_BTN_W, PAGE_BTN_H, PAGE_PITCH_X = 640, 44, 72, 40, 76
SURFACE_PAGES = 16
HOME_FRAME = (484, 740, 140, 44)

SWATCH_EMPTY = (0.07, 0.07, 0.08, 1)
QFRAME_DIM = (0.3, 0.3, 0.34, 1)
PAD_PRESS = (1, 1, 1, 0.45)
HOME_AMBER = (1.0, 0.55, 0.0, 1)


def _strip_ids(t: str) -> str:
    return re.sub(r'<node ID="[^"]*" ', "<node ", t)


def _own_props(t: str) -> tuple[int, int]:
    i = t.index("<properties>")
    return i, t.index("</properties>", i) + len("</properties>")


def n_set(t: str, key: str, typ: str, inner: str) -> str:
    """Set (or add) one of a node's OWN properties, on the node's own text."""
    i, j = _own_props(t)
    props = t[i:j]
    pat = re.compile(r'<property type="' + typ + r'"><key>' + re.escape(key)
                     + r"</key><value>.*?</value></property>", re.S)
    new = f'<property type="{typ}"><key>{key}</key><value>{inner}</value></property>'
    props = pat.sub(lambda _m: new, props, count=1) if pat.search(props) \
        else props.replace("</properties>", new + "</properties>")
    return t[:i] + props + t[j:]


def n_frame(t: str, x: int, y: int, w: int, h: int) -> str:
    return n_set(t, "frame", "r", f"<x>{x}</x><y>{y}</y><w>{w}</w><h>{h}</h>")


def n_colour(t: str, key: str, rgba) -> str:
    r, g, b, a = rgba
    return n_set(t, key, "c", f"<r>{r}</r><g>{g}</g><b>{b}</b><a>{a}</a>")


def n_bool(t: str, key: str, on: bool) -> str:
    return n_set(t, key, "b", "1" if on else "0")


def n_int(t: str, key: str, v: int) -> str:
    return n_set(t, key, "i", str(v))


def n_name(t: str, name: str) -> str:
    return n_set(t, "name", "s", name)


def n_text(t: str, text: str) -> str:
    """A leaf LABEL's authored text."""
    pat = re.compile(r"(<key>text</key><locked>0</locked><lockedDefaultCurrent>1"
                     r"</lockedDefaultCurrent><default>).*?(</default>)", re.S)
    if len(pat.findall(t)) != 1:
        raise RuntimeError(f"{Span(t, 0, len(t), 0).name}: text default not unique")
    return pat.sub(lambda m: m.group(1) + escape(text) + m.group(2), t, count=1)


def n_osc(t: str, old: str, new: str) -> str:
    anchor = f"<value>{old}</value>"
    if t.count(anchor) != 1:
        raise RuntimeError(f"OSC path {old!r} not unique in clone source")
    return t.replace(anchor, f"<value>{new}</value>", 1)


def _nm(t: str) -> str:
    return Span(t, 0, len(t), 0).name or "?"


def split_group(t: str) -> tuple[str, list[str]]:
    spans = index_nodes(t)
    if spans[0].start != 0 or spans[0].end != len(t):
        raise RuntimeError("split_group: not a single node")
    return t[:t.index("<children>")], [s.text for s in spans if s.depth == 1]


def join_group(head: str, kids: list[str]) -> str:
    return head + "<children>" + "".join(kids) + "</children></node>"


def _by_name(kids: list[str]) -> dict[str, str]:
    out = {}
    for k in kids:
        n = _nm(k)
        if n in out:
            raise RuntimeError(f"duplicate sibling {n!r}")
        out[n] = k
    return out


def _edit_kids(group: str, edits: dict) -> str:
    """Apply fn(child_text) -> child_text to named children of a group."""
    head, kids = split_group(group)
    out = []
    for k in kids:
        fn = edits.get(_nm(k))
        out.append(fn(k) if fn else k)
    return join_group(head, out)


def _label(src: str, name: str, frame, text: str, size: int | None = None) -> str:
    t = n_name(_strip_ids(src), name)
    t = n_frame(t, *frame)
    t = n_text(t, text)
    return n_int(t, "textSize", size) if size else t


def _no_osc(t: str) -> str:
    """A clone that must never send or receive: drop its <messages> block."""
    return re.sub(r"<messages>.*?</messages>", "", t, count=1, flags=re.S)


def _lit_swatch(toggle_src: str, name: str, frame) -> str:
    """A pad's colour: a toggle BUTTON held ON. TouchOSC draws every widget's
    background heavily dimmed (a label's included -- the first v3 swatches came
    out at about a third of their authored colour on the iPad), and only a
    button that is on paints its colour at full strength, as the amber PARKED
    pause buttons always have. No OSC, not interactive, x = 1 from the file and
    re-asserted by the script."""
    t = _no_osc(n_name(_strip_ids(toggle_src), name))
    t = n_bool(n_bool(n_frame(t, *frame), "interactive", False), "outline", False)
    t = n_int(n_colour(t, "color", SWATCH_EMPTY), "buttonType", 1)
    pat = re.compile(r"(<value><key>x</key><locked>0</locked><lockedDefaultCurrent>0"
                     r"</lockedDefaultCurrent><default>)0(</default>)")
    if len(pat.findall(t)) != 1:
        raise RuntimeError(f"{name}: x default not in the expected shape")
    return pat.sub(lambda m: m.group(1) + "1" + m.group(2), t, count=1)


def _build_set(grid: str, label_src: str, toggle_src: str) -> str:
    head, kids = split_group(grid)
    k = _by_name(kids)
    head = n_frame(head + "<children></children></node>", 0, 0, 640, V3_PAGE_H)
    head = head[:head.index("<children>")]

    hdr = n_frame(k["gridHeader"], 16, 6, 608, 26)
    hdr = n_int(n_text(hdr, "SET"), "textSize", 14)
    out = [hdr]

    # Quadrant frames (outline only) and their captions, under everything else.
    for q in range(4):
        qx, qy = Q_X[q % 2], Q_Y[q // 2]
        f = _label(label_src, f"qframe_{q}", (qx, qy, Q_W, Q_H), "")
        f = n_bool(n_bool(f, "background", False), "outline", True)
        out.append(n_colour(n_int(f, "outlineStyle", 0), "color", QFRAME_DIM))
    for q in range(4):
        qn = _label(label_src, f"qname_{q}", (Q_X[q % 2], Q_NAME_Y[q // 2], Q_W, 20), "", 12)
        out.append(n_int(qn, "textAlignH", 1))

    def pad_frame(x, y):
        q = (0 if y < 4 else 2) + (0 if x < 4 else 1)
        return (Q_X[q % 2] + PAD_INSET + (x % 4) * PAD_PITCH_X,
                Q_Y[q // 2] + PAD_INSET + (y % 4) * PAD_PITCH_Y, PAD_W, PAD_H)

    # Each pad is three layers: its colour (a lit toggle, see _lit_swatch), its
    # name on two labels -- TouchOSC labels do not break lines, so the script
    # splits the host's two-line name across t1/t2 -- and on top the original
    # button, transparent at rest, for the touch.
    for y in range(8):
        for x in range(8):
            out.append(_lit_swatch(toggle_src, f"sw_{x}_{y}", pad_frame(x, y)))
    for y in range(8):
        for x in range(8):
            px, py, pw, ph = pad_frame(x, y)
            for line, ly in (("t1", py + 8), ("t2", py + ph // 2)):
                t = _label(label_src, f"{line}_{x}_{y}", (px, ly, pw, ph // 2 - 8), "", 10)
                out.append(n_colour(t, "textColor", (1, 1, 1, 1)))
    for y in range(8):
        for x in range(8):
            c = n_frame(k[f"cell_{x}_{y}"], *pad_frame(x, y))
            c = n_bool(n_bool(c, "background", False), "outline", False)
            out.append(n_colour(c, "color", PAD_PRESS))

    fx, fy, fw, fh = pad_frame(0, 0)
    ring = _label(label_src, "padRing", (fx - 3, fy - 3, fw + 6, fh + 6), "")
    ring = n_bool(n_bool(ring, "background", False), "outline", True)
    ring = n_colour(n_int(ring, "outlineStyle", 0), "color", (1, 1, 1, 1))
    out.append(n_bool(ring, "visible", False))

    # Sixteen page buttons in two rows of eight, the label ON the button.
    for p in range(1, SURFACE_PAGES + 1):
        row, col = divmod(p - 1, 8)
        frame = (16 + col * PAGE_PITCH_X, PAGE_ROW_Y + row * PAGE_ROW_PITCH, PAGE_BTN_W, PAGE_BTN_H)
        if f"page_{p}" in k:
            btn = k[f"page_{p}"]
        else:
            btn = n_name(_strip_ids(k["page_1"]), f"page_{p}")
            btn = btn.replace(
                "<conversion>INTEGER</conversion><value>1</value>",
                f"<conversion>INTEGER</conversion><value>{p}</value>", 1)
        btn = n_colour(n_frame(btn, *frame), "color", TAB_OFF)
        out.append(btn)
        lab = k.get(f"pageLabel_{p}") or n_name(_strip_ids(k["pageLabel_1"]), f"pageLabel_{p}")
        lab = n_text(n_frame(lab, *frame), str(p))
        out.append(n_colour(lab, "textColor", TAB_TEXT_OFF))

    home = n_frame(k["home"], *HOME_FRAME)
    out.append(n_colour(n_bool(home, "background", False), "color", HOME_AMBER))
    hl = n_int(n_frame(k["homeLabel"], *HOME_FRAME), "textSize", 12)
    out.append(n_colour(hl, "textColor", HOME_AMBER))
    return join_group(head, out)


MOVE_TO_LIVE = ("hdrSynth", "agencyLabel", "agency", "audiogainLabel", "audiogain",
                "motiongainLabel", "motiongain", "hdrAgency", "agencyLevelLabel",
                "agencyLevel", "agency0", "agency1", "agency2", "agency3")


def _build_mix(ctl: str) -> tuple[str, dict[str, str]]:
    head, kids = split_group(ctl)
    k = _by_name(kids)
    moved = {n: k.pop(n) for n in MOVE_TO_LIVE}
    head = n_frame(head + "<children></children></node>", 0, 0, 640, V3_PAGE_H)
    head = head[:head.index("<children>")]

    out = [n_text(n_frame(k["hdrLayers"], 12, 8, 300, 20), "GROUPS")]
    # Strips: a two-line name on top, then a taller fader -- the room the SYNTH
    # band used to take. Labels do not break lines, so the name is two labels
    # and the root script splits the host's wrapped name across them; `name`
    # stops taking /layer/<i>/name itself (receive off) so the raw two-line
    # string never lands on it.
    for i in range(7):
        g = n_frame(k[f"layer{i}"], 12 + 78 * i, 32, 72, 384)
        g = _edit_kids(g, {
            "name": lambda t: n_int(n_frame(t, 2, 0, 68, 18), "textSize", 12)
                              .replace("<receive>1</receive>", "<receive>0</receive>", 1),
            "fader": lambda t: n_frame(t, 16, 40, 40, 290),
            "pause": lambda t: n_frame(t, 14, 338, 44, 40),
        })
        head_g, kids_g = split_group(g)
        name2 = _no_osc(n_name(_strip_ids(_by_name(kids_g)["name"]), "name2"))
        name2 = n_text(n_frame(name2, 2, 18, 68, 18), "")
        kids_g.insert(1, name2)
        out.append(join_group(head_g, kids_g))
    out.append(n_frame(k["masterAlphaLabel"], 558, 32, 72, 36))
    out.append(n_frame(k["masterAlpha"], 574, 72, 40, 290))
    out.append(n_frame(k["hdrIntent"], 12, 432, 200, 20))
    for i in range(7):
        out.append(n_frame(k[f"intent{i}Label"], 8 + 66 * i, 456, 64, 20))
        out.append(n_frame(k[f"intent{i}"], 20 + 66 * i, 480, 40, 340))
    out.append(n_frame(k["strengthLabel"], 558, 456, 72, 20))
    out.append(n_frame(k["intentStrength"], 574, 480, 40, 340))
    leftover = set(k) - {_nm(t) for t in out}
    if leftover:
        raise RuntimeError(f"mix: unplaced children {sorted(leftover)}")
    return join_group(head, out), moved


def _agency_slot(src: str, j: int) -> str:
    g = src if j < 4 else _strip_ids(src)
    if j >= 4:
        g = n_name(g, f"agency{j}")
        for leaf in ("name", "armed", "budget", "force"):
            g = n_osc(g, f"/agency/0/{leaf}", f"/agency/{j}/{leaf}")
    col, row = j % 4, j // 4
    g = n_frame(g, 56 + 146 * col, 330 + 254 * row, 140, 248)
    edits = {
        "name": lambda t: n_int(n_frame(t, 2, 0, 136, 30), "textSize", 12),
        "armed": lambda t: n_frame(t, 6, 34, 28, 4),
        "budget": lambda t: n_frame(t, 12, 36, 16, 206),
        "force": lambda t: n_frame(t, 40, 36, 98, 206),
        "forceLabel": lambda t: n_int(n_frame(t, 40, 36, 98, 206), "textSize", 14),
    }
    if j >= 4:
        edits["name"] = lambda t: n_int(n_text(n_frame(t, 2, 0, 136, 30), f"A{j + 1}"),
                                        "textSize", 12)
    return _edit_kids(g, edits)


def _input_strip(i: int, agency0: str, layer_fader: str) -> str:
    """A trim strip, assembled from parts the file already has: agency0's slot
    (name label, meter, momentary button and its caption) plus a layer fader."""
    head, kids = split_group(_strip_ids(agency0))
    k = _by_name(kids)
    head = n_frame(n_name(head + "<children></children></node>", f"input{i}"),
                   330 + 76 * i, 32, 72, 270)
    head = head[:head.index("<children>")]
    name = n_osc(k["name"], "/agency/0/name", f"/input/{i}/name")
    name = n_int(n_text(n_frame(name, 2, 0, 68, 20), f"in{i + 1}"), "textSize", 12)
    level = n_name(n_osc(k["budget"], "/agency/0/budget", f"/input/{i}/level"), "level")
    level = n_frame(level, 4, 24, 12, 200)
    gain = n_osc(_strip_ids(layer_fader), "/layer/0/alpha", f"/input/{i}/gain")
    gain = n_colour(n_frame(n_name(gain, "gain"), 20, 24, 44, 200), "color", (0.95, 0.65, 0.1, 1))
    db = n_name(n_osc(_strip_ids(k["name"]), "/agency/0/name", f"/input/{i}/db"), "db")
    db = n_text(n_frame(db, 0, 228, 72, 16), "")
    reset = n_name(n_osc(k["force"], "/agency/0/force", f"/input/{i}/reset"), "reset")
    reset = n_frame(reset, 10, 248, 52, 22)
    cap = n_text(n_frame(n_name(k["forceLabel"], "resetLabel"), 10, 248, 52, 22), "RESET")
    return join_group(head, [name, level, gain, db, reset, cap])


def _build_live(ctl_head: str, moved: dict[str, str], layer0: str) -> str:
    head = n_name(ctl_head + "<children></children></node>", "liveTab")
    head = n_bool(n_frame(head, 0, 0, 640, V3_PAGE_H), "visible", False)
    head = _strip_ids(head[:head.index("<children>")])
    m = moved
    out = [n_text(n_frame(m["hdrSynth"], 12, 8, 300, 20), "RESPONSE")]
    for cap, fader, x, text in (("agencyLabel", "agency", 12, "Agency"),
                                ("audiogainLabel", "audiogain", 90, "Audio"),
                                ("motiongainLabel", "motiongain", 168, "Motion")):
        out.append(n_text(n_frame(m[cap], x, 32, 72, 20), text))
        out.append(n_frame(m[fader], x + 16, 56, 40, 220))
    out.append(_label(m["hdrSynth"], "hdrInputs", (330, 8, 300, 20), "INPUTS"))
    _, lkids = split_group(layer0)
    fader = _by_name(lkids)["fader"]
    for i in range(4):
        out.append(_input_strip(i, m["agency0"], fader))
    out.append(n_text(n_frame(m["hdrAgency"], 12, 306, 300, 20), "AGENCY"))
    out.append(n_text(n_frame(m["agencyLevelLabel"], 8, 330, 40, 20), "level"))
    out.append(n_frame(m["agencyLevel"], 20, 354, 16, 476))
    for j in range(8):
        out.append(_agency_slot(m[f"agency{j}"] if j < 4 else m["agency0"], j))
    return join_group(head, out)


def mutate_v3_pages(xml: str) -> tuple[str, str]:
    if any(s.name == "liveTab" for s in index_nodes(xml)):
        return xml, "v3-pages: liveTab already present, skipped"
    root = index_nodes(xml)[0]
    kids = root_children(xml)
    names = [k.name for k in kids]
    if names != [PAGE_GROUP, GRID_GROUP, "tab_1", "tabLabel_1", "tab_2", "tabLabel_2"]:
        raise RuntimeError(f"v3-pages: unexpected root children {names}")
    ctl, grid = kids[0].text, kids[1].text
    tab1, lab1, tab2, lab2 = (k.text for k in kids[2:])
    label_src = find_node(xml, "pageLabel_1").text

    _, ctl_kids = split_group(ctl)
    layer0 = _by_name(ctl_kids)["layer0"]
    mix, moved = _build_mix(ctl)
    live = _build_live(ctl[:ctl.index("<children>")], moved, layer0)
    toggle_src = _by_name(split_group(layer0)[1])["pause"]
    sett = n_bool(_build_set(grid, label_src, toggle_src), "visible", True)
    mix = n_bool(mix, "visible", False)

    tabs = []
    for i, (x, text) in enumerate(zip(V3_TAB_X, V3_TAB_TEXT), start=1):
        b = tab1 if i == 1 else n_name(_strip_ids(tab2), f"tab_{i}") if i == 3 else tab2
        if i == 3:
            b = b.replace("<conversion>INTEGER</conversion><value>2</value>",
                          "<conversion>INTEGER</conversion><value>3</value>", 1)
        f = Span(b, 0, len(b), 0).frame
        b = n_colour(n_frame(b, x, f["y"], V3_TAB_W, f["h"]), "color", TAB_ON if i == 1 else TAB_OFF)
        lb = lab1 if i == 1 else n_name(_strip_ids(lab2), f"tabLabel_{i}") if i == 3 else lab2
        lf = Span(lb, 0, len(lb), 0).frame
        lb = n_text(n_frame(lb, x, lf["y"], V3_TAB_W, lf["h"]), text)
        lb = n_colour(lb, "textColor", TAB_TEXT_ON if i == 1 else TAB_TEXT_OFF)
        tabs += [b, lb]

    i = xml.index("<children>", root.start) + len("<children>")
    j = kids[-1].end
    xml = xml[:i] + sett + mix + live + "".join(tabs) + xml[j:]
    return xml, ("v3-pages: SET (gridTab: quadrant frames + names, 64 swatches under "
                 "transparent pads, active ring, 16 page buttons, outlined HOME) / MIX "
                 "(ctlTab: GROUPS + INTENT, taller faders, two-line names) / LIVE (liveTab: "
                 "RESPONSE, 4 INPUT trims, 8 AGENCY slots); tabs SET / MIX / LIVE")


V3_SCRIPT = r"""-- sharksynth root script, v3 (2026-10-02): three tabs, SET / MIX / LIVE.
--
-- Heartbeat: ping /sync on connect and every ~2s so the host learns our
-- address and can send feedback, no matter when the app launches. The host
-- treats /sync as keepalive only (it pushes state on first contact and on
-- config load, NOT on every ping), so this does not fight live edits.
local frameCount = 0

-- Pages: three full-height groups sharing the rect above the tab row, exactly
-- one visible. TouchOSC associates a pointer with a control only when it is
-- visible, so a page that is put away cannot be pressed through the one up.
-- gridTab = SET, ctlTab = MIX, liveTab = LIVE (the names predate the tabs).
local PAGES = {'gridTab', 'ctlTab', 'liveTab'}
local currentPage = 1

local function showPage(p)
  currentPage = p
  for i = 1, #PAGES do
    local on = (i == p)
    local page = self.children[PAGES[i]]
    if page then page.visible = on end
    local tab = self.children['tab_' .. i]
    if tab then
      if on then tab.color = Color(0.80, 0.84, 0.92, 1.0)
      else tab.color = Color(0.20, 0.21, 0.26, 1.0) end
    end
    local label = self.children['tabLabel_' .. i]
    if label then
      if on then label.textColor = Color(0.85, 0.85, 0.90, 1.0)
      else label.textColor = Color(0.40, 0.42, 0.48, 1.0) end
    end
  end
end

-- Widgets live inside the page groups, and `fader`, `pause`, `name` repeat in
-- every strip, so reach them through their page and never by name.
local function onPage(page, name)
  local g = self.children[page]
  return g and g.children[name] or nil
end
local function pageChild(name)
  return onPage('ctlTab', name) or onPage('liveTab', name)
end

-- ---- SET tab -------------------------------------------------------------
-- The host sends each pad's authored colour (/grid/cells), its tier
-- (/grid/state: 0 empty, 1 here, 2 another quadrant, 3 unavailable; +8 the
-- active pad, +16 the active pad whose pose has since moved) and its name
-- (/grid/labels). They arrive as separate messages, so keep all three and
-- redraw from the lot whenever any of them lands.
local gridColor, gridState, gridLabel = {}, {}, {}
local curQuad = -1

local function unpackColour(packed)
  local r = math.floor(packed / 65536) % 256
  local g = math.floor(packed / 256) % 256
  local b = packed % 256
  return r / 255, g / 255, b / 255
end

local function renderGrid()
  local page = self.children.gridTab
  if not page then return end
  local ring = page.children.padRing
  local ringShown = false
  for i = 1, 64 do
    local x = (i - 1) % 8
    local y = math.floor((i - 1) / 8)
    local sw = page.children['sw_' .. x .. '_' .. y]
    if sw then
      local st = gridState[i] or 0
      local tier = st % 8
      local active = math.floor(st / 8) % 2 == 1
      local moved = math.floor(st / 16) % 2 == 1
      local r, g, b = unpackColour(gridColor[i] or 0)
      local k, grey, text = 1.0, 0.0, 1.0
      sw.values.x = 1   -- a lit toggle is the only widget that shows its colour at full strength
      if tier == 0 then
        sw.color = Color(0.07, 0.07, 0.08, 1.0)
      else
        -- here: as authored. another quadrant: two-thirds of the way to its own
        -- grey and darker, still readable. unavailable: dark.
        if tier == 2 then grey, k, text = 0.65, 0.6, 0.6
        elseif tier == 3 then grey, k, text = 0.3, 0.3, 0.45 end
        local l = (r + g + b) / 3
        r = (r + (l - r) * grey) * k
        g = (g + (l - g) * grey) * k
        b = (b + (l - b) * grey) * k
        sw.color = Color(r, g, b, 1.0)
      end
      -- White text on every pad (owner, 2026-10-02), dimmer off the current
      -- quadrant. Labels do not break lines, so a two-line name is split over
      -- t1/t2; a one-line name sits in t1, moved to the middle of the pad.
      local t1 = page.children['t1_' .. x .. '_' .. y]
      local t2 = page.children['t2_' .. x .. '_' .. y]
      if t1 and t2 then
        local name = gridLabel[i] or ''
        local a, b2 = name:match('^(.-)\n(.*)$')
        if a then
          t1.values.text, t2.values.text = a, b2
          t1.frame.y = sw.frame.y + 8
        else
          t1.values.text, t2.values.text = name, ''
          t1.frame.y = sw.frame.y + math.floor((sw.frame.h - t1.frame.h) / 2)
        end
        t1.textColor = Color(1.0, 1.0, 1.0, text)
        t2.textColor = Color(1.0, 1.0, 1.0, text)
      end
      if (active or moved) and ring then
        ring.frame.x = sw.frame.x - 3
        ring.frame.y = sw.frame.y - 3
        ring.frame.w = sw.frame.w + 6
        ring.frame.h = sw.frame.h + 6
        if active then ring.color = Color(1.0, 1.0, 1.0, 1.0)
        else ring.color = Color(0.55, 0.55, 0.6, 1.0) end
        ringShown = true
      end
    end
  end
  if ring then ring.visible = ringShown end
  for q = 0, 3 do
    local on = (q == curQuad)
    local f = page.children['qframe_' .. q]
    if f then
      if on then f.color = Color(0.92, 0.92, 0.96, 1.0)
      else f.color = Color(0.30, 0.30, 0.34, 1.0) end
    end
    local n = page.children['qname_' .. q]
    if n then
      if on then n.textColor = Color(0.95, 0.95, 0.98, 1.0)
      else n.textColor = Color(0.45, 0.46, 0.52, 1.0) end
    end
  end
end

local function renderPages(count, cur, names)
  local page = self.children.gridTab
  if not page then return end
  for p = 1, 16 do
    local btn = page.children['page_' .. p]
    local lab = page.children['pageLabel_' .. p]
    local shown = (p <= count)
    if btn then btn.visible = shown end
    if lab then
      lab.visible = shown
      local name = names[p] or ''
      if name ~= '' then lab.values.text = p .. ' ' .. name
      else lab.values.text = tostring(p) end
    end
    -- TouchOSC draws an idle button's colour dimmed, so the current page
    -- reads by its text: bright white on the brighter button, grey elsewhere.
    if p == cur then
      if btn then btn.color = Color(0.80, 0.84, 0.92, 1.0) end
      if lab then lab.textColor = Color(1.0, 1.0, 1.0, 1.0) end
    else
      if btn then btn.color = Color(0.20, 0.21, 0.26, 1.0) end
      if lab then lab.textColor = Color(0.50, 0.52, 0.58, 1.0) end
    end
  end
end

function init()
  showPage(1)
  sendOSC('/sync')
end

function update()
  frameCount = frameCount + 1
  if frameCount >= 120 then   -- ~2s at 60fps
    frameCount = 0
    sendOSC('/sync')
  end
  -- Page tabs. A child button cannot call back into the root script, so poll
  -- them here: a finger holds x = 1 for several frames at 60fps, and the
  -- currentPage guard makes the switch fire once per press.
  for i = 1, #PAGES do
    local tab = self.children['tab_' .. i]
    if tab and tab.values.x == 1 and i ~= currentPage then showPage(i) end
  end
end

-- /layer|agency|input/<n>/active 0|1 -> hide/show that strip or slot.
local function setActive(prefix, n, args)
  local active = (#args >= 1) and (args[1].value ~= 0) or false
  local node = pageChild(prefix .. n)
  if node then node.visible = active end
end

function onReceiveOSC(message, connections)
  local path = message[1]
  local args = message[2]
  local n = path:match('^/layer/(%d+)/active$')
  if n then setActive('layer', n, args); return true end
  local a = path:match('^/agency/(%d+)/active$')
  if a then setActive('agency', a, args); return true end
  local inp = path:match('^/input/(%d+)/active$')
  if inp then setActive('input', inp, args); return true end
  -- /layer/<n>/state: the strip's R/M/S lamps packed into one int, exactly as
  -- the nanoKONTROL2 lights them -- 1 = exists (R), 2 = parked (M), 4 = audible
  -- (S). Colour the pause button and the name label so the strip says the same
  -- thing as the Korg and the desktop GUI's pips.
  local s = path:match('^/layer/(%d+)/state$')
  if s then
    local state = (#args >= 1) and args[1].value or 0
    local exists  = state % 2 == 1
    local parked  = math.floor(state / 2) % 2 == 1
    local audible = math.floor(state / 4) % 2 == 1
    local strip = onPage('ctlTab', 'layer' .. s)
    if strip and exists then
      local c
      if parked then      c = Color(1.00, 0.69, 0.19, 1.0)   -- amber: PARKED
      elseif audible then c = Color(0.36, 0.82, 0.44, 1.0)   -- green: playing
      else                c = Color(0.34, 0.36, 0.42, 1.0)   -- dim: armed and waiting
      end
      if strip.children.pause then strip.children.pause.color = c end
      if strip.children.name then strip.children.name.textColor = c end
      if strip.children.name2 then strip.children.name2.textColor = c end
    end
    return true
  end
  -- /layer/<n>/name: the host wraps long group names onto two lines with a
  -- newline; labels do not break lines, so split it over name / name2.
  local ln = path:match('^/layer/(%d+)/name$')
  if ln then
    local strip = onPage('ctlTab', 'layer' .. ln)
    local name = (#args >= 1) and args[1].value or ''
    local a, b = name:match('^(.-)\n(.*)$')
    if strip then
      if strip.children.name then strip.children.name.values.text = a or name end
      if strip.children.name2 then strip.children.name2.values.text = b or '' end
    end
    return true
  end
  if path == '/mix/heading' then
    local hdr = onPage('ctlTab', 'hdrLayers')
    if hdr and #args >= 1 then hdr.values.text = args[1].value end
    return true
  end
  if path == '/intent/impacts' then
    -- Measured intent surface for the active config (one int per pole fader:
    -- -1 unmeasured, 0 below-noise, 1/2/3 moderate/solid/strong). Impact rides
    -- BRIGHTNESS of the axis-pair hue; labels dim in step.
    local axis = {
      {1.0, 0.47, 0.35}, {1.0, 0.47, 0.35},   -- presence: coral
      {0.35, 0.78, 1.0}, {0.35, 0.78, 1.0},   -- motion:   cyan
      {0.43, 0.86, 0.55}, {0.43, 0.86, 0.55}, -- memory:   green
      {0.75, 0.51, 1.0},                      -- chaotic:  violet (solo)
    }
    local dim = { [-1]=1.0, [0]=0.22, [1]=0.55, [2]=0.8, [3]=1.0 }
    for i = 1, math.min(#args, 7) do
      local d = dim[args[i].value] or 1.0
      local h = axis[i]
      local c = Color(h[1]*d, h[2]*d, h[3]*d, 1.0)
      local fader = self:findByName('intent'..(i-1), true)
      if fader then fader.color = c end
      local label = self:findByName('intent'..(i-1)..'Label', true)
      if label then label.textColor = c end
    end
    return true
  end
  if path == '/grid/cells' then
    -- 64 authored 0xRRGGBB ints, row-major (y=0..7, x=0..7); 0 = no pad.
    for i = 1, math.min(#args, 64) do gridColor[i] = args[i].value end
    renderGrid()
    return true
  end
  if path == '/grid/state' then
    for i = 1, math.min(#args, 64) do gridState[i] = args[i].value end
    renderGrid()
    return true
  end
  if path == '/grid/labels' then
    for i = 1, math.min(#args, 64) do gridLabel[i] = args[i].value end
    renderGrid()
    return true
  end
  if path == '/grid/quadrants' then
    curQuad = (#args >= 1) and args[1].value or -1
    local page = self.children.gridTab
    for q = 0, 3 do
      local lab = page and page.children['qname_' .. q]
      if lab then lab.values.text = (args[q + 2] and args[q + 2].value) or '' end
    end
    renderGrid()
    return true
  end
  if path == '/grid/now' then
    local hdr = onPage('gridTab', 'gridHeader')
    if hdr and #args >= 1 then hdr.values.text = args[1].value end
    return true
  end
  if path == '/grid/pages' then
    local count = (#args >= 1) and args[1].value or 0
    local cur = (#args >= 2) and args[2].value or 0
    local names = {}
    for p = 1, count do names[p] = (args[p + 2] and args[p + 2].value) or '' end
    renderPages(count, cur, names)
    return true
  end
  if path == '/grid/page' then
    return true   -- pre-v3 hosts; the page row now rides /grid/pages
  end
  return false
end
"""


def mutate_v3_script(xml: str) -> tuple[str, str]:
    lua, _, _ = get_script(xml)
    if lua == V3_SCRIPT:
        return xml, "v3-script: already current, skipped"
    return set_script(xml, V3_SCRIPT), (f"v3-script: root Lua replaced ({len(lua.splitlines())} "
                                        f"-> {len(V3_SCRIPT.splitlines())} lines)")


def _is_v3(xml: str) -> bool:
    return any(s.name == "liveTab" for s in index_nodes(xml))


def _pre_v3(fn):
    """The two-tab mutations assume an evenly pitched 8x8; v3's quadrant gutter
    breaks that on purpose, so once v3 is in they have nothing left to do."""
    def wrapped(xml):
        if _is_v3(xml):
            return xml, f"{fn.__name__.replace('mutate_', '').replace('_', '-')}: superseded by v3, skipped"
        return fn(xml)
    return wrapped


MUTATIONS = [
    ("state-lamps", _pre_v3(mutate_state_lamps)),
    ("grid-clamp", _pre_v3(mutate_grid_clamp)),
    ("grid-reflow", _pre_v3(mutate_grid_reflow)),
    ("grid-row-7", _pre_v3(mutate_grid_row)),
    ("page-split", _pre_v3(mutate_page_split)),
    ("page-tabs", _pre_v3(mutate_page_tabs)),
    ("page-canvas", mutate_page_canvas),
    ("page-script", _pre_v3(mutate_page_script)),
    ("v3-pages", mutate_v3_pages),
    ("v3-script", mutate_v3_script),
]


# --------------------------------------------------------------------------- #
# inventory + structural diff
# --------------------------------------------------------------------------- #

def inventory(xml: str) -> dict[str, dict]:
    """path -> {type, frame, osc} for every node, keyed by name-path."""
    root = ET.fromstring(xml).find("node")
    out: dict[str, dict] = {}

    def prop(node, key):
        for pr in node.find("properties").findall("property"):
            if pr.find("key").text == key:
                v = pr.find("value")
                if len(list(v)):
                    return {c.tag: c.text for c in v}
                return v.text
        return None

    def osc(node):
        msgs = node.find("messages")
        if msgs is None:
            return None
        paths = []
        for o in msgs.findall("osc"):
            bits = [p.findtext("value", "") for p in o.find("path").findall("partial")]
            argnode = o.find("arguments")
            args = ([p.findtext("value", "") for p in argnode.findall("partial")]
                    if argnode is not None else [])
            off = "" if o.findtext("enabled") == "1" else "[off] "
            paths.append(off + "".join(bits) + (" " + " ".join(args) if args else ""))
        return "; ".join(paths) or None

    def walk(node, path):
        name = prop(node, "name") or "?"
        here = f"{path}/{name}"
        f = prop(node, "frame") or {}
        out[here] = {
            "type": node.get("type"),
            "frame": tuple(f.get(k) for k in "xywh") if f else None,
            "osc": osc(node),
            "color": prop(node, "color"),
            "visible": prop(node, "visible"),
        }
        kids = node.find("children")
        for k in (kids.findall("node") if kids is not None else []):
            walk(k, here)

    walk(root, "")
    return out


def structural_diff(old_xml: str, new_xml: str) -> str:
    a, b = inventory(old_xml), inventory(new_xml)
    lines: list[str] = []
    added = [k for k in b if k not in a]
    removed = [k for k in a if k not in b]
    changed = [k for k in a if k in b and a[k] != b[k]]

    # A re-parented node looks like one removal plus one addition, which for a
    # whole band is a hundred lines of noise hiding the few real changes.  Pair
    # them back up: same trailing path below the root, byte-identical record.
    moves = []
    for k in list(removed):
        if "/" not in k[1:]:
            continue
        tail = k[k.index("/", 1):]
        hits = [j for j in added if j.endswith(tail) and b[j] == a[k]]
        if len(hits) == 1:
            moves.append((k, hits[0]))
            removed.remove(k)
            added.remove(hits[0])

    lines.append(f"nodes: {len(a)} -> {len(b)}  (+{len(added)} / -{len(removed)} / "
                 f"~{len(changed)} / moved {len(moves)})")
    buckets: dict[tuple[str, str], list[str]] = {}
    for old, new in moves:
        tail = old[old.index("/", 1):]
        buckets.setdefault((old[:-len(tail)], new[:-len(tail)]), []).append(tail)
    for (src_path, dst), tails in sorted(buckets.items()):
        lines.append(f"  > {len(tails)} nodes re-parented {src_path or '/'} -> {dst}, "
                     f"frames unchanged (e.g. {', '.join(sorted(tails)[:3])})")
    for k in added:
        lines.append(f"  + {k}  {b[k]['type']} frame={b[k]['frame']} osc={b[k]['osc']}")
    for k in removed:
        lines.append(f"  - {k}  {a[k]['type']}")
    for k in changed:
        deltas = [f"{f}: {a[k][f]} -> {b[k][f]}" for f in a[k] if a[k][f] != b[k][f]]
        lines.append(f"  ~ {k}  " + "; ".join(deltas))

    old_lua, _, _ = get_script(old_xml)
    new_lua, _, _ = get_script(new_xml)
    if old_lua != new_lua:
        lines.append(f"\nroot script: {len(old_lua.splitlines())} -> "
                     f"{len(new_lua.splitlines())} lines")
        lines += list(difflib.unified_diff(
            old_lua.splitlines(), new_lua.splitlines(),
            "root script (before)", "root script (after)", lineterm="", n=1))
    else:
        lines.append("\nroot script: unchanged")
    return "\n".join(lines)


# --------------------------------------------------------------------------- #
# checks
# --------------------------------------------------------------------------- #

def name_counts(xml: str) -> dict[str, int]:
    counts: dict[str, int] = {}
    for span in index_nodes(xml):
        if span.name:
            counts[span.name] = counts.get(span.name, 0) + 1
    return counts


def check_v3(xml: str, notes: list[str]) -> list[str]:
    """The three-tab layout: pages, pads, buttons, tabs and the script's hooks."""
    root = index_nodes(xml)[0]
    rw, rh = root.frame["w"], root.frame["h"]
    names = [s.name for s in root_children(xml)]
    want = list(V3_PAGES) + ["tab_1", "tabLabel_1", "tab_2", "tabLabel_2", "tab_3", "tabLabel_3"]
    notes.append(f"root children: {names}" + (" -- as expected" if names == want
                                               else f" -- EXPECTED {want}"))
    pages = [find_node(xml, n) for n in V3_PAGES]
    bad = [p.name for p in pages if (p.frame["x"], p.frame["y"], p.frame["w"], p.frame["h"])
           != (0, 0, rw, V3_PAGE_H)]
    notes.append(f"pages share one rect (0,0,{rw},{V3_PAGE_H})"
                 + (f" -- MISPLACED: {bad}" if bad else ""))
    lit = [p.name for p in pages if p.prop("visible") == "1"]
    notes.append(f"exactly one page visible on load: {lit}"
                 + ("" if lit == [V3_PAGES[0]] else " -- WARNING"))
    for p in pages:
        kids = [s for s in direct_children(xml, p) if s.frame]
        spill = [s.name for s in kids
                 if s.frame["x"] < 0 or s.frame["y"] < 0
                 or s.frame["x"] + s.frame["w"] > rw or s.frame["y"] + s.frame["h"] > V3_PAGE_H]
        notes.append(f"{p.name}: {len(kids)} children"
                     + (f" -- OVERFLOWS: {spill}" if spill else ", all inside the page"))
        # Group children (strips, slots) must sit inside their own group too.
        for g in kids:
            if g.type != "GROUP":
                continue
            out = [c.name for c in direct_children(xml, g) if c.frame and (
                c.frame["x"] + c.frame["w"] > g.frame["w"] or c.frame["y"] + c.frame["h"] > g.frame["h"])]
            if out:
                notes.append(f"  WARNING {p.name}/{g.name} children overflow it: {out}")

    grid = find_node(xml, "gridTab")
    gk = {s.name: s for s in direct_children(xml, grid)}
    cells = [n for n in gk if re.fullmatch(r"cell_\d_\d", n)]
    sws = [n for n in gk if re.fullmatch(r"sw_\d_\d", n)]
    notes.append(f"SET: {len(cells)}/64 pads, {len(sws)}/64 swatches")
    misfit = [n for n in cells if gk.get("sw" + n[4:]) is None
              or gk["sw" + n[4:]].frame != gk[n].frame]
    notes.append("every pad sits exactly on its swatch" if not misfit
                 else f"WARNING pad/swatch frames differ: {misfit[:6]}")
    order = [s.name for s in direct_children(xml, grid)]
    behind = all(order.index("sw" + n[4:]) < order.index(n) for n in cells)
    notes.append("swatches draw behind their pads" if behind else "WARNING a swatch draws over its pad")
    touch = [gk[n] for n in gk if n.startswith(("cell_", "page_")) or n == "home"]
    boxes = [(s.name, s.frame["x"], s.frame["y"], s.frame["x"] + s.frame["w"],
              s.frame["y"] + s.frame["h"]) for s in touch]
    clashes = [f"{p[0]}/{q[0]}" for i, p in enumerate(boxes) for q in boxes[i + 1:]
               if p[1] < q[3] and q[1] < p[3] and p[2] < q[4] and q[2] < p[4]]
    notes.append(f"no two of the {len(touch)} SET touch targets overlap" if not clashes
                 else f"WARNING touch targets overlap: {clashes[:6]}")
    for q in range(4):
        f = gk[f"qframe_{q}"].frame
        inside = [n for n in cells
                  if ((0 if int(n[7]) < 4 else 2) + (0 if int(n[5]) < 4 else 1)) == q]
        stray = [n for n in inside if not (f["x"] <= gk[n].frame["x"]
                 and gk[n].frame["x"] + gk[n].frame["w"] <= f["x"] + f["w"]
                 and f["y"] <= gk[n].frame["y"]
                 and gk[n].frame["y"] + gk[n].frame["h"] <= f["y"] + f["h"])]
        if len(inside) != 16 or stray:
            notes.append(f"WARNING quadrant {q}: {len(inside)} pads, outside frame: {stray}")
    notes.append("each quadrant frame holds its 16 pads")

    live = find_node(xml, "liveTab")
    lk = [s.name for s in direct_children(xml, live)]
    notes.append(f"LIVE: {sum(n.startswith('agency') and n[6:].isdigit() for n in lk)} agency "
                 f"slots, {sum(n.startswith('input') for n in lk)} input strips")

    tabs = [find_node(xml, n) for n in want[3:]]
    top = min(t.frame["y"] for t in tabs)
    base = max(t.frame["y"] + t.frame["h"] for t in tabs)
    notes.append(f"tab row y={top}..{base}"
                 + (" -- clear of the pages and inside the canvas"
                    if top >= V3_PAGE_H and base <= rh else " -- WARNING"))
    sends = [t.name for t in tabs if "<enabled>1</enabled>" in t.text]
    notes.append("tab buttons send nothing to the host" if not sends
                 else f"WARNING tabs have live OSC: {sends}")

    lua, _, _ = get_script(xml)
    for hook in ("showPage", "/layer/(%d+)/state", "/layer/(%d+)/name", "/input/(%d+)/active", "/grid/state",
                 "/grid/labels", "/grid/quadrants", "/grid/now", "/grid/pages", "/mix/heading"):
        if hook not in lua:
            notes.append(f"WARNING root script lacks {hook}")
    notes.append("root script: every v3 address handled; falls through to `return false` "
                 + ("(intact)" if lua.rstrip().endswith("return false\nend") else "-- CHECK"))
    return notes


def check(xml: str, baseline: str | None = None) -> list[str]:
    """Everything that must hold for TouchOSC to load the result."""
    notes = []
    ET.fromstring(xml)  # raises on malformed XML
    notes.append("XML parses")

    # findByName searches the whole tree, so a name that repeats is only safe if
    # nothing resolves it that way.  What matters is that WE did not add a new
    # collision: the layout already ships several by design (see SCHEMA.md).
    now = name_counts(xml)
    was = name_counts(baseline) if baseline is not None else {}
    inherited = {n for n, c in was.items() if c > 1}
    dupes = {n: c for n, c in now.items() if c > 1}
    # v3's input strips repeat their children the way the layer strips always
    # have (`name`/`fader`/`pause`): reached through the strip, never by name.
    by_design = {"level", "gain", "db", "reset", "resetLabel", "name2"} if _is_v3(xml) else set()
    fresh = {n: c for n, c in dupes.items() if n not in inherited and n not in by_design}
    if fresh:
        notes.append(f"WARNING new duplicate names, unsafe for findByName: {fresh}")
    else:
        notes.append(f"no NEW duplicate names (pre-existing, by design: "
                     f"{ {n: dupes[n] for n in sorted(inherited & set(dupes))} })")
    if _is_v3(xml):
        return check_v3(xml, notes)

    geo = grid_geometry(xml)
    missing = [(x, y) for y in range(8) for x in range(8)
               if (x, y) not in geo["frames"]]
    notes.append(f"grid: {len(geo['frames'])}/64 cells"
                 + (f", missing {missing}" if missing else ", complete"))

    # No child may overflow or overlap inside gridTab.  Only DIRECT children
    # share a group's coordinate system, so they are the only frames it makes
    # sense to measure against it.
    grid = find_node(xml, "gridTab")
    gh = grid.frame["h"]
    boxes = [(s.name, s.frame["x"], s.frame["y"],
              s.frame["x"] + s.frame["w"], s.frame["y"] + s.frame["h"])
             for s in direct_children(xml, grid) if s.frame]
    over = [b[0] for b in boxes if b[4] > gh]
    notes.append(f"gridTab h={gh}, deepest child bottom={max(b[4] for b in boxes)}"
                 + (f" -- OVERFLOWS: {over}" if over else " -- fits"))
    clashes = []
    for i, p in enumerate(boxes):
        for q in boxes[i + 1:]:
            if p[1] < q[3] and q[1] < p[3] and p[2] < q[4] and q[2] < p[4]:
                clashes.append(f"{p[0]}/{q[0]}")
    notes.append("no overlapping widgets in gridTab" if not clashes
                 else f"WARNING overlaps: {clashes}")

    root = index_nodes(xml)[0]
    rh = root.frame["h"]
    gf = grid.frame
    notes.append(f"canvas {root.frame['w']}x{rh}, gridTab bottom={gf['y'] + gf['h']}"
                 + (" -- fits" if gf["y"] + gf["h"] <= rh else " -- OVERFLOWS canvas"))

    # The page split: two groups on one rect, a tab row clear of both, and
    # exactly one page up in the authored file.
    names = [s.name for s in root_children(xml)]
    if PAGE_GROUP in names:
        want = [PAGE_GROUP, GRID_GROUP, "tab_1", "tabLabel_1", "tab_2", "tabLabel_2"]
        notes.append(f"root children: {names}"
                     + (" -- as expected" if names == want else f" -- EXPECTED {want}"))
        pages = [find_node(xml, n) for n in PAGE_GROUPS]
        bad = [p.name for p in pages
               if (p.frame["x"], p.frame["y"], p.frame["w"]) != (0, 0, root.frame["w"])]
        notes.append(f"pages share one rect at (0,0,{root.frame['w']}): "
                     + str({p.name: p.frame["h"] for p in pages})
                     + (f" -- MISPLACED: {bad}" if bad else ""))
        lit = [p.name for p in pages if p.prop("visible") == "1"]
        notes.append(f"exactly one page visible on load: {lit}"
                     + ("" if len(lit) == 1 else " -- WARNING"))
        # The control page is hand-tuned authored geometry -- hdrAgency and
        # agencyLevelLabel have always overlapped by 2px -- so only overflow is
        # worth asserting there; overlap stays a gridTab-only invariant.
        page = find_node(xml, PAGE_GROUP)
        kids = [s for s in direct_children(xml, page) if s.frame]
        deep = max(s.frame["y"] + s.frame["h"] for s in kids)
        spill = [s.name for s in kids if s.frame["y"] + s.frame["h"] > page.frame["h"]]
        notes.append(f"{PAGE_GROUP} h={page.frame['h']}, {len(kids)} children, "
                     f"deepest bottom={deep}"
                     + (f" -- OVERFLOWS: {spill}" if spill else " -- fits"))
        tabs = [find_node(xml, n) for n in ("tab_1", "tabLabel_1", "tab_2", "tabLabel_2")]
        top = min(t.frame["y"] for t in tabs)
        base = max(t.frame["y"] + t.frame["h"] for t in tabs)
        pbot = max(p.frame["y"] + p.frame["h"] for p in pages)
        notes.append(f"tab row y={top}..{base}, deeper page ends at {pbot}"
                     + (" -- clear of both pages and inside the canvas"
                        if top >= pbot and base <= rh
                        else " -- WARNING tab row clashes with a page or overflows"))
        sends = [t.name for t in tabs if "<enabled>1</enabled>" in t.text]
        notes.append("tab buttons send nothing to the host"
                     if not sends else f"WARNING tabs have live OSC: {sends}")

    lua, _, _ = get_script(xml)
    clamp = re.search(r"math\.min\(#args, (\d+)\)", lua.split("/grid/cells")[-1])
    notes.append(f"root script: /grid/cells clamp = {clamp.group(1) if clamp else 'NOT FOUND'}")
    notes.append("root script: /layer/<n>/state branch "
                 + ("present" if "/layer/(%d+)/state" in lua else "ABSENT"))
    notes.append("root script: falls through to `return false` for "
                 "/layer/<i>/alpha and /pause "
                 + ("(intact)" if lua.rstrip().endswith("return false\nend") else "-- CHECK"))
    notes.append("root script: page switcher "
                 + ("present" if "showPage" in lua else "ABSENT"))
    stale = [f for f in ("self.children[prefix .. n]", "self.children['layer' .. s]")
             if f in lua]
    notes.append("root script: strip and slot lookups rebased through the page group"
                 if not stale else
                 f"WARNING root script still resolves strips at root level: {stale}")
    return notes


# --------------------------------------------------------------------------- #
# commands
# --------------------------------------------------------------------------- #

def cmd_roundtrip(args) -> int:
    path = Path(args.file)
    blob = path.read_bytes()
    xml = zlib.decompress(blob).decode("utf-8")
    again = zlib.compress(xml.encode("utf-8"), ZLIB_LEVEL)
    print(f"{path.name}: {len(blob)} bytes -> {len(xml)} chars of XML")
    print(f"zlib header: {blob[:2].hex()}  (level {ZLIB_LEVEL})")
    if again == blob:
        print("ROUND-TRIP OK: recompressed output is byte-identical to the shipped file")
        ok = True
    else:
        ok = zlib.decompress(again) == xml.encode("utf-8")
        print(f"recompressed to {len(again)} bytes, NOT byte-identical "
              f"({'XML-equivalent' if ok else 'AND NOT XML-EQUIVALENT'})")
    ET.fromstring(xml)
    print(f"XML parses; {len(index_nodes(xml))} nodes")
    return 0 if again == blob else (1 if not ok else 0)


def cmd_dump(args) -> int:
    xml = read_xml(Path(args.file))
    if args.out:
        Path(args.out).write_text(xml, encoding="utf-8")
        print(f"wrote {args.out} ({len(xml)} chars)")
    else:
        sys.stdout.write(xml)
    return 0


def cmd_inventory(args) -> int:
    for path, info in inventory(read_xml(Path(args.file))).items():
        print(f"{info['type']:7} {path:44} {str(info['frame']):28} {info['osc'] or ''}")
    return 0


def cmd_diff(args) -> int:
    print(structural_diff(read_xml(Path(args.a)), read_xml(Path(args.b))))
    return 0


def cmd_test(args) -> int:
    """Run the layout's own root Lua against test_script.lua's stub TouchOSC.

    The .tosc carries a 163-line script that nothing else in this repo executes,
    so a page split that rebased half its lookups would otherwise be unverifiable
    short of the iPad.  This gets most of the way: the real node tree, the real
    script, and every branch driven through the addresses the host actually sends.
    """
    import shutil
    import subprocess
    import tempfile

    lua_bin = shutil.which("lua") or shutil.which("lua5.4")
    harness = HERE / "test_script.lua"
    if lua_bin is None:
        print(f"SKIP: no `lua` on PATH (brew install lua) -- {harness.name} not run")
        return 0
    xml = read_xml(Path(args.file))
    root = ET.fromstring(xml).find("node")

    def prop(node, key):
        for pr in node.find("properties").findall("property"):
            if pr.find("key").text == key:
                v = pr.find("value")
                return {c.tag: c.text for c in v} if len(list(v)) else v.text
        return None

    lines: list[str] = []

    def emit(node, depth):
        pad = "  " * depth
        f = prop(node, "frame") or {}
        frame = "{" + ", ".join(f"{k} = {f.get(k, 0)}" for k in "xywh") + "}"
        lines.append(f'{pad}node("{prop(node, "name")}", '
                     f'{"true" if prop(node, "visible") == "1" else "false"}, {frame}, {{')
        kids = node.find("children")
        for k in (kids.findall("node") if kids is not None else []):
            emit(k, depth + 1)
        lines.append(f"{pad}}}),")

    emit(root, 1)
    with tempfile.TemporaryDirectory() as tmp:
        Path(tmp, "tree.lua").write_text("return " + "\n".join(lines).rstrip(",") + "\n")
        Path(tmp, "script.lua").write_text(get_script(xml)[0])
        return subprocess.call([lua_bin, str(harness), tmp])


def cmd_apply(args) -> int:
    path = Path(args.file)
    before = read_xml(path)
    xml = before
    print(f"-- {path.name}: {len(before)} chars, {len(index_nodes(before))} nodes")
    for name, fn in MUTATIONS:
        xml, note = fn(xml)
        print(f"   {note}")
    if xml == before:
        print("-- nothing to do; the layout already carries every mutation")
        return 0

    print("\n-- checks")
    for note in check(xml, before):
        print(f"   {note}")
    print("\n-- structural diff")
    print(structural_diff(before, xml))

    if args.dry_run:
        print("\n-- dry run, nothing written")
        return 0
    out = Path(args.out) if args.out else path
    data = write_tosc(out, xml)
    assert zlib.decompress(data).decode("utf-8") == xml, "recompress/decompress mismatch"
    print(f"\n-- wrote {out} ({len(data)} bytes, verified to decompress back to the "
          f"exact XML above)")
    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    def add(name, fn, **kw):
        s = sub.add_parser(name, **kw)
        s.set_defaults(fn=fn)
        return s

    for name, fn in (("roundtrip", cmd_roundtrip), ("dump", cmd_dump),
                     ("inventory", cmd_inventory), ("apply", cmd_apply),
                     ("test", cmd_test)):
        s = add(name, fn)
        s.add_argument("--file", default=str(DEFAULT_TOSC))
        if name in ("dump", "apply"):
            s.add_argument("-o", "--out")
        if name == "apply":
            s.add_argument("--dry-run", action="store_true")

    s = add("diff", cmd_diff)
    s.add_argument("a")
    s.add_argument("b")

    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())

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


MUTATIONS = [
    ("state-lamps", mutate_state_lamps),
    ("grid-clamp", mutate_grid_clamp),
    ("grid-reflow", mutate_grid_reflow),
    ("grid-row-7", mutate_grid_row),
    ("page-split", mutate_page_split),
    ("page-tabs", mutate_page_tabs),
    ("page-canvas", mutate_page_canvas),
    ("page-script", mutate_page_script),
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
    fresh = {n: c for n, c in dupes.items() if n not in inherited}
    if fresh:
        notes.append(f"WARNING new duplicate names, unsafe for findByName: {fresh}")
    else:
        notes.append(f"no NEW duplicate names (pre-existing, by design: "
                     f"{ {n: dupes[n] for n in sorted(inherited & set(dupes))} })")

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
        lines.append(f'{pad}node("{prop(node, "name")}", '
                     f'{"true" if prop(node, "visible") == "1" else "false"}, {{')
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

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


MUTATIONS = [
    ("state-lamps", mutate_state_lamps),
    ("grid-clamp", mutate_grid_clamp),
    ("grid-reflow", mutate_grid_reflow),
    ("grid-row-7", mutate_grid_row),
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
            paths.append("".join(bits) + (" " + " ".join(args) if args else ""))
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

    lines.append(f"nodes: {len(a)} -> {len(b)}  "
                 f"(+{len(added)} / -{len(removed)} / ~{len(changed)})")
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

    # No child may overflow or overlap inside gridTab.
    grid = find_node(xml, "gridTab")
    gh = grid.frame["h"]
    boxes = []
    for span in index_nodes(xml):
        if grid.start < span.start < grid.end and span.frame:
            f = span.frame
            boxes.append((span.name, f["x"], f["y"], f["x"] + f["w"], f["y"] + f["h"]))
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

    lua, _, _ = get_script(xml)
    clamp = re.search(r"math\.min\(#args, (\d+)\)", lua.split("/grid/cells")[-1])
    notes.append(f"root script: /grid/cells clamp = {clamp.group(1) if clamp else 'NOT FOUND'}")
    notes.append("root script: /layer/<n>/state branch "
                 + ("present" if "/layer/(%d+)/state" in lua else "ABSENT"))
    notes.append("root script: falls through to `return false` for "
                 "/layer/<i>/alpha and /pause "
                 + ("(intact)" if lua.rstrip().endswith("return false\nend") else "-- CHECK"))
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
                     ("inventory", cmd_inventory), ("apply", cmd_apply)):
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

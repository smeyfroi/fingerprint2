-- Runs the root Lua that sharksynth.tosc actually ships against a stub TouchOSC
-- and the layout's real node tree. `./build_layout.py test` extracts both
-- (script.lua, tree.lua) to a temp dir and calls this with its path.
--
-- The stub models only what the script touches, so a pass here is no substitute
-- for the iPad. What it does prove: the script is valid Lua, every OSC branch
-- resolves the widget it means to on the page it now lives on, the SET tab
-- draws each pad from its colour, tier and name, and the tab poll switches
-- pages exactly once per press. See README.md "What to check on the iPad".

-- A stub TouchOSC just rich enough to run the shipped root script: node tree
-- straight out of the .tosc (name, visible, frame), Color/sendOSC stubs,
-- findByName recursing the way Hexler documents it.
local Node = {}
Node.__index = Node
function Node:findByName(name, recursive)
  for _, c in ipairs(self.kids) do
    if c.name == name then return c end
  end
  if recursive then
    for _, c in ipairs(self.kids) do
      local hit = c:findByName(name, true)
      if hit then return hit end
    end
  end
  return nil
end

function node(name, visible, frame, kids)
  local n = setmetatable({name = name, visible = visible, frame = frame, kids = kids,
                          values = {x = 0, text = ""}, children = {}}, Node)
  for _, c in ipairs(kids) do n.children[c.name] = c end
  return n
end

Color = function(r, g, b, a) return {r = r, g = g, b = b, a = a} end
local sent = {}
sendOSC = function(p) sent[#sent + 1] = p end

self = dofile(arg[1] .. "/tree.lua")
dofile(arg[1] .. "/script.lua")

local fails, checks = 0, 0
local function is(what, got, want)
  checks = checks + 1
  if got ~= want then
    fails = fails + 1
    print(string.format("  FAIL %-52s got %s, wanted %s", what, tostring(got), tostring(want)))
  end
end
local function msg(path, ...)
  local args = {}
  for _, v in ipairs({...}) do args[#args + 1] = {value = v} end
  return {path, args}
end
local SET, MIX, LIVE = self.children.gridTab, self.children.ctlTab, self.children.liveTab

print("-- init: SET is the first tab")
init()
is("SET visible after init", SET.visible, true)
is("MIX hidden after init", MIX.visible, false)
is("LIVE hidden after init", LIVE.visible, false)
is("tab_1 lit", self.children.tab_1.color.r, 0.80)
is("tab_2 dark", self.children.tab_2.color.r, 0.20)
is("tab_3 dark", self.children.tab_3.color.r, 0.20)
is("init pinged /sync", sent[1], "/sync")

print("-- tabs: MIX, LIVE, back to SET, once per press")
self.children.tab_2.values.x = 1
update()
is("MIX shown", MIX.visible, true)
is("SET put away", SET.visible, false)
update()
self.children.tab_2.values.x = 0
update()
is("release does not switch back", MIX.visible, true)
self.children.tab_3.values.x = 1
update()
is("LIVE shown", LIVE.visible, true)
is("MIX put away", MIX.visible, false)
is("tab_3 lit", self.children.tab_3.color.r, 0.80)
self.children.tab_3.values.x = 0
self.children.tab_1.values.x = 1
update()
is("SET shown again", SET.visible, true)
is("LIVE put away", LIVE.visible, false)
self.children.tab_1.values.x = 0

print("-- /active hides the strip or slot on its own page")
is("consumed", onReceiveOSC(msg("/layer/3/active", 0), {}), true)
is("MIX layer3 hidden", MIX.children.layer3.visible, false)
onReceiveOSC(msg("/layer/3/active", 1), {})
is("MIX layer3 back", MIX.children.layer3.visible, true)
onReceiveOSC(msg("/agency/6/active", 0), {})
is("LIVE agency6 hidden", LIVE.children.agency6.visible, false)
onReceiveOSC(msg("/input/3/active", 0), {})
is("LIVE input3 hidden", LIVE.children.input3.visible, false)

print("-- /layer/<i>/state lamps")
local strip = MIX.children.layer2
onReceiveOSC(msg("/layer/2/state", 5), {})            -- exists + audible
is("playing = green pause", strip.children.pause.color.g, 0.82)
is("playing = green name", strip.children.name.textColor.g, 0.82)
is("playing = green name2", strip.children.name2.textColor.g, 0.82)
onReceiveOSC(msg("/layer/2/state", 3), {})            -- exists + parked
is("parked = amber", strip.children.pause.color.r, 1.00)
onReceiveOSC(msg("/layer/2/state", 1), {})            -- exists, silent
is("armed and waiting = dim", strip.children.pause.color.r, 0.34)

print("-- /layer/<i>/name split over two labels")
is("consumed", onReceiveOSC(msg("/layer/1/name", "voice-2-\nfluid"), {}), true)
is("name line 1", MIX.children.layer1.children.name.values.text, "voice-2-")
is("name line 2", MIX.children.layer1.children.name2.values.text, "fluid")
onReceiveOSC(msg("/layer/1/name", "room"), {})
is("short name, one line", MIX.children.layer1.children.name.values.text, "room")
is("short name clears line 2", MIX.children.layer1.children.name2.values.text, "")

print("-- /mix/heading")
onReceiveOSC(msg("/mix/heading", "LAYERS"), {})
is("heading follows the host", MIX.children.hdrLayers.values.text, "LAYERS")

print("-- /intent/impacts")
local impacts = {}
for i = 1, 7 do impacts[i] = 3 end
onReceiveOSC(msg("/intent/impacts", table.unpack(impacts)), {})
is("intent0 full coral", MIX.children.intent0.color.r, 1.0)
is("intent6 full violet", MIX.children.intent6.color.b, 1.0)

print("-- SET pads: colour, tier, name")
local cells, state, labels = {}, {}, {}
for i = 1, 64 do cells[i], state[i], labels[i] = 0xFF8000, 1, "pad " .. i end
labels[5] = "Answer and\nretrace"   -- (4,0): a two-line name
cells[2], state[2] = 0, 0          -- (1,0): no pad
state[1] = 1 + 8                   -- (0,0): here, the active pad
state[8] = 2                       -- (7,0): another quadrant
state[64] = 3                      -- (7,7): unavailable
onReceiveOSC(msg("/grid/cells", table.unpack(cells)), {})
onReceiveOSC(msg("/grid/state", table.unpack(state)), {})
onReceiveOSC(msg("/grid/labels", table.unpack(labels)), {})
local sw = function(x, y) return SET.children["sw_" .. x .. "_" .. y] end
is("here = authored colour", sw(0, 0).color.r, 1.0)
is("row 7 reached", sw(0, 7).color.r, 1.0)
is("empty pad = dark", sw(1, 0).color.r, 0.07)
is("elsewhere is darker", sw(7, 0).color.r < 0.7, true)
is("elsewhere is greyer", sw(7, 0).color.r - sw(7, 0).color.b < 1.0 * 0.6, true)
is("unavailable is darkest", sw(7, 7).color.r < sw(7, 0).color.r, true)
local t = function(line, x, y) return SET.children[line .. "_" .. x .. "_" .. y] end
is("swatch held on (full-strength colour)", sw(0, 0).values.x, 1)
is("one-line name in t1", t("t1", 3, 0).values.text, "pad 4")
is("one-line name leaves t2 empty", t("t2", 3, 0).values.text, "")
is("one-line name centred", t("t1", 3, 0).frame.y > sw(3, 0).frame.y + 8, true)
is("two-line name, first line", t("t1", 4, 0).values.text, "Answer and")
is("two-line name, second line", t("t2", 4, 0).values.text, "retrace")
is("first line at the top", t("t1", 4, 0).frame.y, sw(4, 0).frame.y + 8)
is("pad text is white", t("t1", 0, 0).textColor.g, 1.0)
is("pad text dimmer elsewhere", t("t1", 7, 0).textColor.a < 1.0, true)
is("ring shown on the active pad", SET.children.padRing.visible, true)
is("ring around it", SET.children.padRing.frame.x, sw(0, 0).frame.x - 3)
state[1] = 1
onReceiveOSC(msg("/grid/state", table.unpack(state)), {})
is("no active pad, no ring", SET.children.padRing.visible, false)
is("script leaves the touch pads alone", SET.children.cell_0_0.color, nil)

print("-- quadrants, now line, pages")
onReceiveOSC(msg("/grid/quadrants", 1, "Toccata", "Minuet and trio", "Fugue", ""), {})
is("current quadrant framed bright", SET.children.qframe_1.color.r, 0.92)
is("others dim", SET.children.qframe_0.color.r, 0.30)
is("quadrant named", SET.children.qname_1.values.text, "Minuet and trio")
onReceiveOSC(msg("/grid/now", "Minuet and trio  |  Water  |  page 1 baroque suite"), {})
is("now line", SET.children.gridHeader.values.text, "Minuet and trio  |  Water  |  page 1 baroque suite")
onReceiveOSC(msg("/grid/pages", 5, 2, "suite", "trio options", "toccata", "fugue", "chaconne"), {})
is("page 5 reachable", SET.children.page_5.visible, true)
is("page 6 hidden", SET.children.page_6.visible, false)
is("current page lit", SET.children.page_2.color.r, 0.80)
is("other pages quiet", SET.children.page_1.color.r, 0.20)
is("page named", SET.children.pageLabel_2.values.text, "2 trio options")
is("current page text bright", SET.children.pageLabel_2.textColor.r, 1.0)
is("other page text grey", SET.children.pageLabel_1.textColor.r, 0.50)
is("legacy /grid/page consumed", onReceiveOSC(msg("/grid/page", 3), {}), true)

print("-- fall-through to the widgets is intact")
is("/layer/0/alpha falls through", onReceiveOSC(msg("/layer/0/alpha", 0.5), {}), false)
is("/layer/0/pause falls through", onReceiveOSC(msg("/layer/0/pause", 1), {}), false)
is("/input/0/gain falls through", onReceiveOSC(msg("/input/0/gain", 0.5), {}), false)
is("/agency/7/budget falls through", onReceiveOSC(msg("/agency/7/budget", 0.5), {}), false)
is("/master/alpha falls through", onReceiveOSC(msg("/master/alpha", 0.5), {}), false)

print("-- heartbeat")
local before = #sent
for _ = 1, 120 do update() end
is("still pinging /sync", #sent > before, true)

print(string.format("\n%d checks, %d failures", checks, fails))
os.exit(fails == 0 and 0 or 1)

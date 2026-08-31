-- Runs the root Lua that sharksynth.tosc actually ships against a stub TouchOSC
-- and the layout's real node tree. `./build_layout.py test` extracts both
-- (script.lua, tree.lua) to a temp dir and calls this with its path.
--
-- The stub models only what the script touches, so a pass here is no substitute
-- for the iPad. What it does prove: the script is valid Lua, every OSC branch
-- still resolves the widget it means to now that the bands are a level deeper,
-- and the tab poll switches pages exactly once per press. See README.md
-- "What to check on the iPad after sending this" for what it cannot prove.

-- A stub TouchOSC just rich enough to run the shipped root script: node tree
-- straight out of the .tosc, Color/sendOSC stubs, findByName recursing the way
-- Hexler documents it.
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

function node(name, visible, kids)
  local n = setmetatable({name = name, visible = visible, kids = kids,
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

print("-- init")
init()
is("ctlTab visible after init", self.children.ctlTab.visible, true)
is("gridTab hidden after init", self.children.gridTab.visible, false)
is("tab_1 lit", self.children.tab_1.color.r, 0.80)
is("tab_2 dark", self.children.tab_2.color.r, 0.20)
is("tabLabel_1 bright", self.children.tabLabel_1.textColor.r, 0.85)
is("init pinged /sync", sent[1], "/sync")

print("-- tab_2 pressed, then update()")
self.children.tab_2.values.x = 1
update()
is("gridTab shown", self.children.gridTab.visible, true)
is("ctlTab put away", self.children.ctlTab.visible, false)
is("tab_2 lit", self.children.tab_2.color.r, 0.80)
is("tab_1 dark", self.children.tab_1.color.r, 0.20)

print("-- finger still down: no repeat, no flicker")
update()
is("gridTab still shown", self.children.gridTab.visible, true)
self.children.tab_2.values.x = 0
update()
is("release does not switch back", self.children.gridTab.visible, true)

print("-- back to CONTROL")
self.children.tab_1.values.x = 1
update()
is("ctlTab shown", self.children.ctlTab.visible, true)
is("gridTab put away", self.children.gridTab.visible, false)
self.children.tab_1.values.x = 0

print("-- /layer/<i>/active through the new page group")
is("consumed", onReceiveOSC(msg("/layer/3/active", 0), {}), true)
is("layer3 hidden", self.children.ctlTab.children.layer3.visible, false)
onReceiveOSC(msg("/layer/3/active", 1), {})
is("layer3 back", self.children.ctlTab.children.layer3.visible, true)

print("-- /agency/<i>/active")
onReceiveOSC(msg("/agency/2/active", 0), {})
is("agency2 hidden", self.children.ctlTab.children.agency2.visible, false)

print("-- /layer/<i>/state lamps")
local strip = self.children.ctlTab.children.layer2
onReceiveOSC(msg("/layer/2/state", 5), {})            -- exists + audible
is("playing = green pause", strip.children.pause.color.g, 0.82)
is("playing = green name", strip.children.name.textColor.g, 0.82)
onReceiveOSC(msg("/layer/2/state", 3), {})            -- exists + parked
is("parked = amber", strip.children.pause.color.r, 1.00)
onReceiveOSC(msg("/layer/2/state", 1), {})            -- exists, silent
is("armed and waiting = dim", strip.children.pause.color.r, 0.34)

print("-- /intent/impacts still resolves a level deeper")
local impacts = {}
for i = 1, 7 do impacts[i] = 3 end
onReceiveOSC(msg("/intent/impacts", table.unpack(impacts)), {})
is("intent0 full coral", self.children.ctlTab.children.intent0.color.r, 1.0)
is("intent6 full violet", self.children.ctlTab.children.intent6.color.b, 1.0)
is("intent0Label coloured", self.children.ctlTab.children.intent0Label.textColor.r, 1.0)

print("-- /grid/cells all 64")
local cells = {}
for i = 1, 64 do cells[i] = 0xFF8000 end
onReceiveOSC(msg("/grid/cells", table.unpack(cells)), {})
is("cell_0_0 lit", self.children.gridTab.children.cell_0_0.color.r, 1.0)
is("cell_7_7 lit (row 7 reached)", self.children.gridTab.children.cell_7_7.color.r, 1.0)

print("-- /grid/page")
onReceiveOSC(msg("/grid/page", 3), {})
is("page_3 bright", self.children.gridTab.children.page_3.color.r, 1.0)
is("page_1 dim", self.children.gridTab.children.page_1.color.r, 0.28)

print("-- fall-through is intact")
is("/layer/0/alpha falls through", onReceiveOSC(msg("/layer/0/alpha", 0.5), {}), false)
is("/layer/0/pause falls through", onReceiveOSC(msg("/layer/0/pause", 1), {}), false)
is("/master/alpha falls through", onReceiveOSC(msg("/master/alpha", 0.5), {}), false)

print("-- heartbeat")
local before = #sent
for _ = 1, 120 do update() end
is("still pinging /sync", #sent > before, true)

print(string.format("\n%d checks, %d failures", checks, fails))
os.exit(fails == 0 and 0 or 1)

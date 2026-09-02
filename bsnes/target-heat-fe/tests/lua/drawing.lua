frames = 0
expected_commands = 12

assert(not pcall(gui.circle, 20, 20, -1))
assert(not pcall(gui.ellipse, 20, 20, 4, -1))

local bit = {band = function(value, mask) return value & mask end}
local i, pause = 1, 0
local animnotupdating = {3}
local hittime, airhittime = {1}, {0}
local statetype, movetype = {0x40}, {0}
local groundrecoverytime, airrecoverytime = {0}, {0}
local animelem, animelemtime = {{2, 2}}, {{4, 4}}
animnotupdating[i] = (hittime[i] > 0 or airhittime[i] > 0) and
  (bit.band(statetype[i], 0x10) == 0x10 and
    (groundrecoverytime[i] > 0 or airrecoverytime[i] > 0) or
   bit.band(statetype[i], 0x40) == 0x40 or
   bit.band(movetype[i], 0x20) == 0x20 or
   bit.band(statetype[i], 0x20) == 0x20) and
  animnotupdating[i] +
    ((animelem[i][1] == animelem[i][2] and
      animelemtime[i][1] == animelemtime[i][2] and
      animelemtime[i][1] ~= -1 and pause == 0) and 1 or 0) or 0
assert(type(animnotupdating[i]) == "number" and animnotupdating[i] == 4)

function on_before_frame()
  gui.box(8, 8, 24, 20, {fill = 0x4000ff00, outline = 0xffff0000})
  gui.circle(32, 20, 6, {fill = 0xffff00ff, outline = 0xffffffff})
  gui.ellipse(48, 20, 8, 4, {fill = 0xffffff00, outline = 0xffffffff})
  gui.line(56, 8, 72, 24, 0xff00ffff)
  gui.pixel(80, 16, 0xffff00ff)
  gui.text(96, 8, 0x2a, {color = 0xffffffff, align = "right", font = "pixel"})
end

function on_frame()
  frames = frames + 1
  gui.box(-8, -8, 80, 40, {fill = 0x4000ff00, outline = 0xffff0000, thickness = 2})
  gui.circle(128, 112, 16, {fill = 0x400000ff, outline = 0xffffffff, thickness = 2})
  gui.ellipse(64, 80, 20, 10, {fill = 0x40ff0000, outline = 0xffffff00})
  gui.line(0, 0, 255, 223, 0xff00ffff, 1)
  gui.pixel(128, 112, 0xffffffff)
  gui.text(128, 8, 0x2a, {
    color = 0xffffffff, outline = 0xff000000, size = 12,
    align = "center", font = "pixel",
  })
  gui.window("Lua drawing test", {width = 180})
  gui.label("fixed-width overlay")
  assert(not gui.button("Test button", {id = "test", width = 100}))
end

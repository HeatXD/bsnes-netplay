# Lua scripting

heat-fe embeds Lua 5.4 and runs one script at a time. Open it from **Tools > Lua
Scripting**, or pass `--lua script.lua` on the command line. `on_frame()` runs
after each emulated frame and before the game and overlay are drawn.

```lua
function on_frame()
  local hp = memory.read_u8(0x7e1000)
  gui.text(8, 8, string.format("HP: %d", hp))
end
```

## Memory

Addresses use the 24-bit SNES system bus. Multi-byte reads and writes advance
through consecutive bus addresses. Writes affect the running machine, including
mapped ROM, but never change the game file.

```lua
memory.read_u8(address)
memory.read_u16_le(address)  memory.read_u16_be(address)
memory.read_u24_le(address)  memory.read_u24_be(address)
memory.read_u32_le(address)  memory.read_u32_be(address)

memory.write_u8(address, value)
memory.write_u16_le(address, value)  memory.write_u16_be(address, value)
memory.write_u24_le(address, value)  memory.write_u24_be(address, value)
memory.write_u32_le(address, value)  memory.write_u32_be(address, value)

memory.read_bit(address, bit)
memory.write_bit(address, bit, enabled)
```

Bits are numbered 0 through 7. Lua 5.4's native integer and bitwise operators
can be used for everything more elaborate.

## Drawing

Drawing coordinates start at the top-left of the game and scale with it. The
logical area is 256 by 224 with overscan crop, or 256 by 240 without it. Colors
are `0xAARRGGBB`; drawings are clipped to the game.

```lua
gui.box(x1, y1, x2, y2, {
  fill = 0x4000ff00,
  outline = 0xffff0000,
  thickness = 1
})
gui.line(x1, y1, x2, y2, color, thickness)
gui.pixel(x, y, color)
gui.text(x, y, value, {
  color = 0xffffffff,
  outline = 0xff000000,
  size = 13
})
```

## Input

Ports are numbered 1 through 3. Controls can be addressed by their displayed
name or their zero-based index. These are the values supplied to the core for
the current frame; scripts cannot inject input.

```lua
input.value(1, "Right")
input.held(1, "B")
input.value(1, 4)
```

## Files

File paths are relative to the script's data directory shown in the Lua window.
Absolute paths and `..` traversal are rejected. The Lua `debug`, `io`, `os`,
package-loading, `dofile`, and `loadfile` APIs are not exposed.

```lua
file.write("frame-data.csv", "frame,x,y\n")
file.append("frame-data.csv", "1,20,40\n")
local contents = file.read("frame-data.csv")
local directory = file.directory()
```

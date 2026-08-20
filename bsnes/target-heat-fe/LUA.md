# Lua scripting

The heat frontend embeds Lua 5.4 for game overlays, memory tools, input displays,
and frame-data collection. Open **Tools > Lua Scripting**, choose a `.lua` file,
and use **Reload** after changing it on disk. The same window contains the
script's output console.

Only one script runs at a time. A load or runtime error stops the script and is
shown in the Lua Scripting window. Reloading creates a fresh Lua state.

## Frame callback

A script may do setup work when it is loaded and define callbacks for work that
repeats:

```lua
local frames = 0

function on_frame()
  frames = frames + 1
  gui.text(4, 4, "frame " .. frames)
end
```

`on_before_frame()` runs after physical controllers are sampled but immediately
before the emulator runs. Use it to decide the inputs for the upcoming frame.
`on_frame()` runs after an emulated frame and before the frontend draws its UI.
Drawing commands are cleared before every call, so an overlay must draw all of
its current shapes each frame.

## Console

Lua's normal `print()` function writes to the output console in the Lua
Scripting window. Multiple values are separated by tabs. The equivalent
explicit API is:

```lua
console.log("player", player_number, "x", player_x)
console.clear()
```

The window provides **Copy** and **Clear** buttons and automatically follows new
output. It retains up to 64 KiB across reloads so the output immediately before
an error is not lost. Errors are appended automatically. This is an output
console, not an interactive Lua prompt.

## Drawing

Drawing coordinates are SNES game coordinates. `(0, 0)` is the top-left of the
game image, normally sized 256x224 or 256x240. The frontend scales the overlay
with the game and clips it to the game image.

Colors are integers in `0xAARRGGBB` form: alpha, red, green, then blue. An alpha
of `0x00` is transparent and `0xff` is opaque.

```lua
gui.box(x1, y1, x2, y2, options)
gui.circle(center_x, center_y, radius, options)
gui.ellipse(center_x, center_y, radius_x, radius_y, options)
```

Box, circle, and ellipse options are:

```lua
{
  fill = 0x4000ff00,       -- default: transparent
  outline = 0xffff0000,    -- default: opaque white
  thickness = 2            -- default: 1
}
```

The other primitives are:

```lua
gui.line(x1, y1, x2, y2, color, thickness) -- thickness defaults to 1
gui.pixel(x, y, color)
gui.text(x, y, value, options)
```

Text accepts any value Lua can convert to a string. Its options are:

```lua
{
  color = 0xffffffff,      -- default: opaque white
  outline = 0xff000000,    -- default: opaque black
  size = 13                -- game-space pixels
}
```

Use `string.format()` when displaying hexadecimal or decimal values:

```lua
gui.text(4, 20, string.format("HP: %d  flags: %02x", hp, flags))
```

## Memory

The root `memory` table accesses the 24-bit SNES CPU bus. Dedicated subtables
access physical memory directly and use offsets starting at zero:

| Lua table | Size | Contents |
|---|---:|---|
| `memory` | `0x1000000` | 24-bit CPU bus |
| `memory.wram` | `0x20000` | 128 KiB work RAM |
| `memory.vram` | `0x10000` | 64 KiB video RAM |
| `memory.cgram` | `0x200` | 512-byte color palette RAM |
| `memory.oam` | `0x220` | 544-byte sprite attribute memory |
| `memory.apuram` | `0x10000` | 64 KiB SPC/APU RAM |

Every table exposes its byte length as `.size` and has the same operations:

```lua
read_u8(address)
read_u16_le(address)       read_u16_be(address)
read_u24_le(address)       read_u24_be(address)
read_u32_le(address)       read_u32_be(address)

write_u8(address, value)
write_u16_le(address, value)  write_u16_be(address, value)
write_u24_le(address, value)  write_u24_be(address, value)
write_u32_le(address, value)  write_u32_be(address, value)

read_bit(address, bit)                 -- bit is 0 through 7; returns boolean
write_bit(address, bit, boolean)
```

Examples:

```lua
local opcode = memory.read_u8(0x008000)
local player_x = memory.wram.read_u16_le(0x1234)
local tile = memory.vram.read_u8(0x4000)
memory.apuram.write_bit(0x200, 3, true)
```

Bus writes to mapped ROM affect only the running emulator's mapped copy, never
the ROM file. Direct-domain writes bypass the CPU/PPU register interface and
take effect immediately. An out-of-range access stops the script with an error.

## Input

Scripts can inspect or override the values supplied to an emulated controller:

```lua
input.value(port, name_or_index)
input.held(port, name_or_index)
input.set(port, name_or_index, value)
input.clear(port, name_or_index)
input.clear_all()
```

Ports are numbered 1 through 3. Input names are case-insensitive and depend on
the connected device. Numeric indices are zero-based. `value()` returns the raw
integer input value; `held()` returns a boolean.

`set()` accepts a boolean or a signed 16-bit integer. The override remains in
effect every frame until `clear()` or `clear_all()` removes it. Controls without
an override continue to use their physical controller values. Stopping,
reloading, or encountering a script error removes every override automatically.

Use `on_before_frame()` when an input must be chosen for an exact frame:

```lua
local frame = 0

function on_before_frame()
  frame = frame + 1
  input.set(1, "Right", frame <= 30)
  input.set(1, "B", frame == 10)
end

function on_frame()
  if input.held(1, "B") then
    gui.circle(220, 190, 5, {fill = 0xff00ff00, outline = 0xffffffff})
  end
end
```

An override may also be established during script setup or `on_frame()`; it is
then applied to the next emulated frame. Numeric input indices are useful for
devices with axes, while names are clearer for standard controllers.

## Files

Standard Lua `io` is intentionally unavailable. The confined file API keeps
each script's data under the frontend configuration directory in
`Scripts/<script-name>/`:

```lua
file.write("frames.txt", "start\n")       -- replaces the file
file.append("frames.txt", "1,12,44\n")   -- appends
local contents = file.read("frames.txt")
local directory = file.directory()
local exists = file.exists("frames.txt")
local entries = file.list()               -- sorted names in the data root
local nested = file.list("trials")        -- sorted names in a subdirectory
local removed = file.remove("old.txt")
```

Paths must be relative and cannot contain `..`, a drive name, or an absolute
root. Subdirectories inside the script data directory are allowed. Writing a
file creates missing parent directories. `remove()` deletes one file or empty
directory and never recursively removes a directory tree. It returns `false`
when the path did not exist or could not be removed. Listing a path that is not
a directory raises an error; use `exists()` first when that is expected. A
missing file reads as an empty string, so `exists()` also distinguishes it from
a real empty file.

## Save states

Scripts can use the same nine numbered state slots as the emulator UI:

```lua
savestate.save(slot)
savestate.load(slot)
savestate.exists(slot)
savestate.remove(slot)
savestate.save_file("trial-progress.bst")
savestate.load_file("trial-start.bst")
```

Slots must be integers from 1 through 9. Every operation returns a boolean.
These are normal persistent per-game states, so scripts should avoid silently
overwriting a slot the player uses.

`save_file()` writes beneath the script's confined data directory. `load_file()`
first looks there and, if no such path exists, looks beside the `.lua` file.
This lets a script keep writable progress separately while a modder distributes
read-only starting states next to the script. Paths are relative, may use
subdirectories, and cannot contain `..`, a drive name, or an absolute root.

File states use heat-fe's `.bst` format and must match the currently loaded ROM
and serialization settings. States from BizHawk, other emulators, and bsnes
`.bsz` containers are not interchangeable. A numbered heat-fe `.bst` can be
copied beside a script and loaded directly.

A state contains the emulated machine only. Loading one does not rewind Lua
variables, console output, drawing commands, or input overrides. A normal state
load also creates the emulator's undo state and resumes emulation.

## Emulator control

The basic emulator-control API is:

```lua
emu.loaded()       -- boolean
emu.paused()       -- boolean
emu.pause()        -- returns false when no game is loaded
emu.resume()
emu.reset()
emu.power()
emu.frame()        -- number of completed frontend frames
emu.game()         -- current game title, or an empty string
```

Pause, resume, reset, and power return whether a game was loaded. If pause is
called from a frame callback, the frame already in progress completes before
the frontend becomes idle. A paused script receives no callbacks until the
game is resumed from the UI or another frontend control.

## Complete overlay example

Replace the example WRAM addresses with the addresses for the game being
studied:

```lua
local player_x_address = 0x1000
local player_y_address = 0x1002

function on_frame()
  local x = memory.wram.read_u16_le(player_x_address)
  local y = memory.wram.read_u16_le(player_y_address)

  gui.box(x - 8, y - 32, x + 8, y, {
    fill = 0x3000ff00,
    outline = 0xff00ff00,
    thickness = 1,
  })
  gui.circle(x, y, 3, {
    fill = 0xffffff00,
    outline = 0xff000000,
  })
  gui.text(4, 4, string.format("x=%d y=%d", x, y), {
    color = 0xffffffff,
    outline = 0xff000000,
    size = 12,
  })
end
```

## Available Lua libraries

The base, coroutine, table, string, math, and UTF-8 libraries are available.
Lua 5.4's arithmetic, logical, and bitwise operators work normally.

For safety, `debug`, `io`, `os`, `package`, `require`, `dofile`, and `loadfile`
are unavailable. Scripts cannot load native libraries or execute programs.

Current limitations: one script at a time, one frontend font for all overlay
text, no image drawing, no interactive console, and no built-in binary
formatter.

frames = 0
before_frames = 0
expected_controls = 1

assert(emu.loaded())
assert(not emu.paused())
assert(type(emu.frame()) == "number")
assert(#emu.game() > 0)
assert(not pcall(savestate.save, 0))
assert(not pcall(savestate.load, 10))
package_memory = memory.wram.read_u8(0x1fff0)

function on_before_frame()
  before_frames = before_frames + 1
  if before_frames == 1 then
    assert(emu.pause() and emu.paused())
    assert(emu.resume() and not emu.paused())
    assert(emu.reset())
    assert(emu.power())

    memory.wram.write_u8(0x1fff0, package_memory ~ 0xff)
    assert(savestate.load_file("_heat_fe_package_state.bst"))
    packaged_state_loaded = memory.wram.read_u8(0x1fff0) == package_memory and 1 or 0

    original_memory = memory.wram.read_u8(0x1fff0)
    assert(savestate.save_file("progress.bst"))
    assert(file.exists("progress.bst"))
    memory.wram.write_u8(0x1fff0, original_memory ~ 0xff)
    assert(savestate.load_file("progress.bst"))
    data_state_loaded = memory.wram.read_u8(0x1fff0) == original_memory and 1 or 0

    assert(savestate.save(9))
    assert(savestate.exists(9))
    memory.wram.write_u8(0x1fff0, original_memory ~ 0xff)
    memory_changed = memory.wram.read_u8(0x1fff0) == (original_memory ~ 0xff) and 1 or 0
    emu_controls = 1
  else
    assert(savestate.load(9))
    memory_restored = memory.wram.read_u8(0x1fff0) == original_memory and 1 or 0
    assert(savestate.remove(9))
    state_removed = not savestate.exists(9) and 1 or 0
    assert(file.remove("progress.bst"))
  end
end

function on_frame()
  frames = frames + 1
end

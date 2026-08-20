frames = 0

memory.write_u32_le(0x7e1ff0, 0x78563412)
assert(memory.read_u8(0x7e1ff0) == 0x12)
assert(memory.read_u16_le(0x7e1ff0) == 0x3412)
assert(memory.read_u16_be(0x7e1ff0) == 0x1234)
assert(memory.read_u24_le(0x7e1ff0) == 0x563412)
assert(memory.read_u24_be(0x7e1ff0) == 0x123456)
assert(memory.read_u32_le(0x7e1ff0) == 0x78563412)
assert(memory.read_u32_be(0x7e1ff0) == 0x12345678)

memory.write_u16_be(0x7e1ff4, 0xabcd)
assert(memory.read_u16_be(0x7e1ff4) == 0xabcd)
memory.write_u24_le(0x7e1ff6, 0xabcdef)
assert(memory.read_u24_le(0x7e1ff6) == 0xabcdef)
memory.write_u32_be(0x7e1ff9, 0x89abcdef)
assert(memory.read_u32_be(0x7e1ff9) == 0x89abcdef)

memory.write_u8(0x7e1ffe, 0x55)
memory.write_bit(0x7e1ffe, 1, true)
assert(memory.read_u8(0x7e1ffe) == 0x57)
assert(memory.read_bit(0x7e1ffe, 1))
memory.write_bit(0x7e1ffe, 0, false)
assert(memory.read_u8(0x7e1ffe) == 0x56)
assert(not pcall(memory.read_u8, -1))
assert(not pcall(memory.read_u16_le, 0xffffff))

local rom = memory.read_u8(0x008000)
memory.write_u8(0x008000, rom ~ 0xff)
assert(memory.read_u8(0x008000) == (rom ~ 0xff))
memory.write_u8(0x008000, rom)

function on_frame()
  frames = frames + 1
end

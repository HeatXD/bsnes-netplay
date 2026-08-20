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

assert(memory.size == 0x1000000)
assert(memory.wram.size == 0x20000)
assert(memory.vram.size == 0x10000)
assert(memory.cgram.size == 0x200)
assert(memory.oam.size == 0x220)
assert(memory.apuram.size == 0x10000)

local function roundtrip(domain, address, value)
  local original = domain.read_u32_le(address)
  domain.write_u32_le(address, value)
  assert(domain.read_u32_le(address) == value)
  domain.write_u32_le(address, original)
end

roundtrip(memory.wram, 0x1fff0, 0x12345678)
roundtrip(memory.vram, 0xfff0, 0x23456789)
roundtrip(memory.apuram, 0xfff0, 0x3456789a)

local cgram = memory.cgram.read_u16_le(0x1f0)
memory.cgram.write_u16_le(0x1f0, 0x3456)
assert(memory.cgram.read_u16_le(0x1f0) == 0x3456)
memory.cgram.write_u8(0x1f1, 0xff)
assert(memory.cgram.read_u8(0x1f1) == 0x7f)
memory.cgram.write_u16_le(0x1f0, cgram)

local oam = memory.oam.read_u16_le(0x210)
memory.oam.write_u16_le(0x210, 0xa55a)
assert(memory.oam.read_u16_le(0x210) == 0xa55a)
memory.oam.write_u16_le(0x210, oam)

local apu = memory.apuram.read_u8(0xffee)
memory.apuram.write_bit(0xffee, 3, true)
assert(memory.apuram.read_bit(0xffee, 3))
memory.apuram.write_u8(0xffee, apu)

for _, domain in ipairs({memory.wram, memory.vram, memory.cgram, memory.oam, memory.apuram}) do
  assert(not pcall(domain.read_u8, domain.size))
  assert(not pcall(domain.read_u16_le, domain.size - 1))
end

domains_verified = 5

function on_frame()
  frames = frames + 1
end

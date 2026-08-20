#include "emucore.hpp"

#include <sfc/sfc.hpp>
#undef platform

uint32_t EmuCore::memorySize(MemoryDomain domain) const {
  switch(domain) {
  case MemoryDomain::Bus: return 0x1000000;
  case MemoryDomain::WRAM: return 0x20000;
  case MemoryDomain::VRAM: return 0x10000;
  case MemoryDomain::CGRAM: return 0x200;
  case MemoryDomain::OAM: return 0x220;
  case MemoryDomain::APURAM: return 0x10000;
  }
  return 0;
}

uint8_t EmuCore::readMemory(MemoryDomain domain, uint32_t address) const {
  if(!loaded() || address >= memorySize(domain)) return 0;
  using namespace SuperFamicom;
  switch(domain) {
  case MemoryDomain::Bus: return cpu.readDisassembler(address);
  case MemoryDomain::WRAM: return cpu.wram[address];
  case MemoryDomain::VRAM:
    return SuperFamicom::system.fastPPU() ? ppufast.debuggerReadVRAM(address) : ppu.debuggerReadVRAM(address);
  case MemoryDomain::CGRAM:
    return SuperFamicom::system.fastPPU() ? ppufast.debuggerReadCGRAM(address) : ppu.debuggerReadCGRAM(address);
  case MemoryDomain::OAM:
    return SuperFamicom::system.fastPPU() ? ppufast.debuggerReadOAM(address) : ppu.debuggerReadOAM(address);
  case MemoryDomain::APURAM: return dsp.apuram[address];
  }
  return 0;
}

void EmuCore::writeMemory(MemoryDomain domain, uint32_t address, uint8_t value) {
  if(!loaded() || address >= memorySize(domain)) return;
  using namespace SuperFamicom;
  switch(domain) {
  case MemoryDomain::Bus:
    Memory::GlobalWriteEnable = true;
    bus.write(address, value);
    Memory::GlobalWriteEnable = false;
    return;
  case MemoryDomain::WRAM: cpu.wram[address] = value; return;
  case MemoryDomain::VRAM:
    if(SuperFamicom::system.fastPPU()) ppufast.debuggerWriteVRAM(address, value);
    else ppu.debuggerWriteVRAM(address, value);
    return;
  case MemoryDomain::CGRAM:
    if(SuperFamicom::system.fastPPU()) ppufast.debuggerWriteCGRAM(address, value);
    else ppu.debuggerWriteCGRAM(address, value);
    return;
  case MemoryDomain::OAM:
    if(SuperFamicom::system.fastPPU()) ppufast.debuggerWriteOAM(address, value);
    else ppu.debuggerWriteOAM(address, value);
    return;
  case MemoryDomain::APURAM: dsp.apuram[address] = value; return;
  }
}

uint8_t EmuCore::readBus(uint32_t address) const {
  return readMemory(MemoryDomain::Bus, address & 0xffffff);
}

void EmuCore::writeBus(uint32_t address, uint8_t value) {
  writeMemory(MemoryDomain::Bus, address & 0xffffff, value);
}

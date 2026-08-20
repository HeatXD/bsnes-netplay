#include "emucore.hpp"

#include <sfc/sfc.hpp>
#undef platform

uint8_t EmuCore::readBus(uint32_t address) const {
  if(!loaded()) return 0;
  return SuperFamicom::cpu.readDisassembler(address & 0xffffff);
}

void EmuCore::writeBus(uint32_t address, uint8_t value) {
  if(!loaded()) return;
  SuperFamicom::Memory::GlobalWriteEnable = true;
  SuperFamicom::bus.write(address & 0xffffff, value);
  SuperFamicom::Memory::GlobalWriteEnable = false;
}

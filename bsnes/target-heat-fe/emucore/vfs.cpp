// Emulator::Platform's file side: every open() the core makes, answered out of
// memory, a pak folder, or disk.

#include "impl.hpp"

#include "resources.hpp"

auto EmuCore::Impl::media(uint id) -> Media* {
  switch(id) {
  case SuperFamicomID: return &superFamicom;
  case GameBoyID: return &gameBoy;
  case BSMemoryID: return &bsMemory;
  case SufamiTurboAID: return &sufamiTurboA;
  case SufamiTurboBID: return &sufamiTurboB;
  }
  return nullptr;
}

auto EmuCore::Impl::savePath(const Media& media, const string& name) -> string {
  // a pak folder keeps its own saves, as bsnes does
  if(media.pak()) return {media.location, name};
  if(savesDir.empty()) return {Location::notsuffix(media.location), ".", name};
  return {savesDir.c_str(), "/", media.name(), ".", name};
}

auto EmuCore::Impl::openSave(const Media& media, const string& name, vfs::file::mode mode) -> shared_pointer<vfs::file> {
  string path = savePath(media, name);
  if(mode == vfs::file::mode::read && !file::exists(path)) return {};
  // a deleted or unplugged saves folder would otherwise swallow the write
  if(mode != vfs::file::mode::read && !media.pak() && !savesDir.empty()) {
    directory::create(savesDir.c_str());
  }
  return vfs::fs::file::open(path, mode);
}

// Most dumps ship no firmware, so fall back to Firmware/dsp1b.program.rom. The
// manifest already names the chip, so read that rather than re-deriving it.
auto EmuCore::Impl::openFirmware(const string& name, vfs::file::mode mode) -> shared_pointer<vfs::file> {
  if(firmwareDir.empty() || mode != vfs::file::mode::read) return {};

  auto part = name.split(".", 1L);            // "upd7725" and "program.rom"
  auto content = part(1).split(".", 1L)(0);   // "program"

  for(auto memory : sfcDocument["game/board"].find("memory")) {
    if(memory["type"].text() != "ROM") continue;
    if(memory["architecture"].text().downcase() != part(0)) continue;
    if(memory["content"].text().downcase() != content) continue;

    string path = {firmwareDir.c_str(), "/", memory["identifier"].text().downcase(), ".", part(1)};
    if(!file::exists(path)) return {};
    return vfs::fs::file::open(path, mode);
  }
  return {};
}

// firmware rides in the ROM image, so its size is what names the chip
auto EmuCore::Impl::openSuperFamicom(string name, vfs::file::mode mode) -> shared_pointer<vfs::file> {
  auto slice = [&](uint offset, uint size) -> shared_pointer<vfs::file> {
    return vfs::memory::file::open(&superFamicom.firmware.data()[offset], size);
  };
  const uint firmware = superFamicom.firmware.size();

  if(mode == vfs::file::mode::read) {
    if(name == "arm6.program.rom" && firmware == 0x28000) return slice(0x00000, 0x20000);
    if(name == "arm6.data.rom" && firmware == 0x28000) return slice(0x20000, 0x08000);
    if(name == "hg51bs169.data.rom" && firmware == 0xc00) return slice(0, 0xc00);
    if(name == "lr35902.boot.rom" && firmware == 0x100) return slice(0, 0x100);
    if(name == "upd7725.program.rom" && firmware == 0x2000) return slice(0x0000, 0x1800);
    if(name == "upd7725.data.rom" && firmware == 0x2000) return slice(0x1800, 0x0800);
    if(name == "upd96050.program.rom" && firmware == 0xd000) return slice(0x0000, 0xc000);
    if(name == "upd96050.data.rom" && firmware == 0xd000) return slice(0xc000, 0x1000);
    if(name.endsWith(".program.rom") || name.endsWith(".data.rom") || name.endsWith(".boot.rom")) {
      return openFirmware(name, mode);
    }
  }

  // MSU-1 streams off disk beside the ROM, and is far too large to hold
  if(!superFamicom.pak()) {
    if(name == "msu1/data.rom") {
      return vfs::fs::file::open({Location::notsuffix(superFamicom.location), ".msu"}, mode);
    }
    if(name.match("msu1/track*.pcm")) {
      name.trimLeft("msu1/track", 1L);
      return vfs::fs::file::open({Location::notsuffix(superFamicom.location), name}, mode);
    }
  }

  return {};
}

auto EmuCore::Impl::open(uint id, string name, vfs::file::mode mode, bool required) -> shared_pointer<vfs::file> {
  auto result = openMedia(id, name, mode);
  // the core runs the coprocessor on zeroes rather than refusing, so note it
  if(!result && required) missing.append(missing ? ", " : "", name);
  return result;
}

auto EmuCore::Impl::openMedia(uint id, string name, vfs::file::mode mode) -> shared_pointer<vfs::file> {
  const bool read = mode == vfs::file::mode::read;

  if(id == SystemID) {
    if(name == "ipl.rom" && read) return vfs::memory::file::open(iplrom, sizeof(iplrom));
    if(name == "boards.bml" && read) return vfs::memory::file::open(Boards, sizeof(Boards));
    return {};
  }

  Media* slot = media(id);
  if(!slot || !*slot) return {};

  if(read) {
    if(name == "manifest.bml") {
      return vfs::memory::file::open(slot->manifest.data<uint8_t>(), slot->manifest.size());
    }
    if(name == "program.rom") {
      return vfs::memory::file::open(slot->program.data(), slot->program.size());
    }
  }
  // BS Memory writes to its flash in memory only, never back to the card image
  if(id == BSMemoryID && name == "program.flash") {
    return vfs::memory::file::open(slot->program.data(), slot->program.size());
  }

  if(id == SuperFamicomID) {
    if(read && name == "data.rom") {
      return vfs::memory::file::open(slot->data.data(), slot->data.size());
    }
    if(read && name == "expansion.rom") {
      return vfs::memory::file::open(slot->expansion.data(), slot->expansion.size());
    }
    if(auto result = openSuperFamicom(name, mode)) return result;
  }

  // a pak folder holds its own files, saves included
  if(slot->pak()) return vfs::fs::file::open({slot->location, name}, mode);
  return openSave(*slot, name, mode);
}

// an empty slot leaves the core to map the cartridge without it
auto EmuCore::Impl::load(uint id, string, string, vector<string>) -> Emulator::Platform::Load {
  if(id == SuperFamicomID) return {id, info.region};
  Media* slot = media(id);
  if(slot && *slot) return {id, ""};
  return {};
}

// Getting a game off disk and into memory, before the core sees anything.

#include "impl.hpp"

#include <heuristics/heuristics.hpp>
#include <heuristics/heuristics.cpp>
#include <heuristics/super-famicom.cpp>
#include <heuristics/game-boy.cpp>
#include <heuristics/bs-memory.cpp>
#include <heuristics/sufami-turbo.cpp>

#include <lzma/lzma.hpp>

namespace {
// nall only splits paths on '/', so a windows path would come back whole
auto normalize(const std::string& path) -> string {
  return string{path.c_str()}.transform("\\", "/");
}

constexpr struct { const char* suffix; EmuCore::Medium medium; } MediaSuffixes[] = {
  {".sfc", EmuCore::Medium::SuperFamicom}, {".smc", EmuCore::Medium::SuperFamicom},
  {".fig", EmuCore::Medium::SuperFamicom}, {".swc", EmuCore::Medium::SuperFamicom},
  {".gb",  EmuCore::Medium::GameBoy},      {".gbc", EmuCore::Medium::GameBoy},
  {".bs",  EmuCore::Medium::BSMemory},
  {".st",  EmuCore::Medium::SufamiTurbo},
};

auto mediumOfSuffix(const string& suffix, EmuCore::Medium& medium) -> bool {
  for(auto& entry : MediaSuffixes) {
    if(suffix == entry.suffix) { medium = entry.medium; return true; }
  }
  return false;
}

// 7z hides its entry names, so the ROM itself has to say what it is. The Game
// Boy logo is checked by the boot ROM and the Sufami Turbo header is fixed, so
// both are reliable; BS Memory carries no marker and falls back.
auto mediumOfData(const vector<uint8_t>& rom) -> EmuCore::Medium {
  static const uint8_t GameBoyLogo[] = {0xce, 0xed, 0x66, 0x66, 0xcc, 0x0d, 0x00, 0x0b};
  if(rom.size() >= 0x134
  && memory::compare(&rom.data()[0x104], GameBoyLogo, sizeof(GameBoyLogo)) == 0) {
    return EmuCore::Medium::GameBoy;
  }
  if(rom.size() >= 0x20000 && memory::compare(rom.data(), "BANDAI SFC-ADX", 14) == 0) {
    return EmuCore::Medium::SufamiTurbo;
  }
  return EmuCore::Medium::SuperFamicom;
}

auto loadFile(const string& location) -> vector<uint8_t> {
  auto suffix = Location::suffix(location).downcase();
  if(suffix == ".zip") {
    Decode::ZIP archive;
    if(!archive.open(location)) return {};
    EmuCore::Medium medium;
    for(auto& entry : archive.file) {
      if(mediumOfSuffix(Location::suffix(entry.name).downcase(), medium)) {
        return archive.extract(entry);
      }
    }
    return {};
  }
  if(suffix == ".7z") return LZMA::extract(location);
  return file::read(location);
}

// a pak reads its parts from the folder; a loose ROM may have a .bml beside it
auto loadParts(const string& location, const vector<string>& pakFiles,
               string& manifest) -> vector<uint8_t> {
  vector<uint8_t> rom;
  if(location.endsWith("/")) {
    manifest = file::read({location, "manifest.bml"});
    for(auto& part : pakFiles) {
      // the wildcards cover firmware, which is named after its processor
      if(part.beginsWith("*")) {
        for(auto& filename : directory::files(location, part)) {
          rom.append(file::read({location, filename}));
        }
      } else {
        rom.append(file::read({location, part}));
      }
    }
  } else {
    manifest = file::read({Location::notsuffix(location), ".bml"});
    rom = loadFile(location);
  }
  return rom;
}

// every slot medium is a manifest plus one program ROM
template<typename Heuristic>
auto loadCart(Media& slot, const string& location, uint minSize,
              const vector<string>& pakFiles) -> bool {
  string manifest;
  auto rom = loadParts(location, pakFiles, manifest);
  if(rom.size() < minSize) return false;

  slot.location = location;
  slot.manifest = manifest ? manifest : Heuristic(rom, location).manifest();
  slot.program = rom;
  return true;
}
}  // namespace

EmuCore::Medium EmuCore::mediumOf(const std::string& path) {
  const string location = normalize(path);
  const auto suffix = Location::suffix(location).downcase();
  Medium medium = Medium::SuperFamicom;

  if(suffix == ".zip") {
    Decode::ZIP archive;
    if(archive.open(location)) {
      for(auto& entry : archive.file) {
        if(mediumOfSuffix(Location::suffix(entry.name).downcase(), medium)) break;
      }
    }
    return medium;
  }
  if(suffix == ".7z") return mediumOfData(LZMA::extract(location));

  // a pak folder has no suffix, and is always a base cartridge
  mediumOfSuffix(suffix, medium);
  return medium;
}

std::string EmuCore::loadError() const { return (const char*)impl->error; }
std::string EmuCore::missingFiles() const { return (const char*)impl->missing; }

auto EmuCore::Impl::loadSuperFamicom(const string& location) -> bool {
  string manifest;
  auto rom = loadParts(location, {"program.rom", "data.rom", "expansion.rom",
                                  "*.boot.rom", "*.program.rom", "*.data.rom"}, manifest);
  if(rom.size() < 0x8000) return false;

  // strip a copier header if present
  if((rom.size() & 0x7fff) == 512) {
    memory::move(&rom[0], &rom[512], rom.size() - 512);
    rom.resize(rom.size() - 512);
  }

  auto heuristics = Heuristics::SuperFamicom(rom, location);
  superFamicom.location = location;
  superFamicom.manifest = manifest ? manifest : heuristics.manifest();
  sfcDocument = BML::unserialize(superFamicom.manifest);

  // the window title is the file name, not the cartridge header
  info.title = superFamicom.name();
  info.headerTitle = heuristics.title();
  info.region = heuristics.videoRegion();
  info.board = heuristics.board();
  info.checksum = Hash::SHA256(rom).digest();
  info.romSize = heuristics.romSize();
  info.ramSize = heuristics.ramSize() + heuristics.expansionRamSize();

  uint offset = 0;
  auto take = [&](uint size, vector<uint8_t>& out) {
    if(!size) return;
    out.resize(size);
    memory::copy(&out[0], &rom[offset], size);
    offset += size;
  };
  take(heuristics.programRomSize(), superFamicom.program);
  take(heuristics.dataRomSize(), superFamicom.data);
  take(heuristics.expansionRomSize(), superFamicom.expansion);
  take(heuristics.firmwareRomSize(), superFamicom.firmware);
  return true;
}

bool EmuCore::load(const GameSpec& spec) {
  unload();
  impl->error = "";
  impl->missing = "";

  if(spec.superFamicom.empty()) {
    impl->error = "no base cartridge";
    return false;
  }

  // all media must be in memory first: the core asks for slots mid-load
  Impl& core = *impl;
  const char* failed = nullptr;
  auto want = [&](const std::string& path) { return !failed && !path.empty(); };

  if(!core.loadSuperFamicom(normalize(spec.superFamicom))) failed = "cartridge";
  if(want(spec.gameBoy)
  && !loadCart<Heuristics::GameBoy>(core.gameBoy, normalize(spec.gameBoy), 0x4000, {"program.rom"})) {
    failed = "Game Boy ROM";
  }
  if(want(spec.bsMemory)
  && !loadCart<Heuristics::BSMemory>(core.bsMemory, normalize(spec.bsMemory), 0x8000, {"program.rom", "program.flash"})) {
    failed = "BS Memory ROM";
  }
  if(want(spec.sufamiTurboA)
  && !loadCart<Heuristics::SufamiTurbo>(core.sufamiTurboA, normalize(spec.sufamiTurboA), 0x20000, {"program.rom"})) {
    failed = "Sufami Turbo cartridge";
  }
  if(want(spec.sufamiTurboB)
  && !loadCart<Heuristics::SufamiTurbo>(core.sufamiTurboB, normalize(spec.sufamiTurboB), 0x20000, {"program.rom"})) {
    failed = "second Sufami Turbo cartridge";
  }
  if(failed) {
    core.error = {"could not read the ", failed};
    unload();
    return false;
  }

  // must precede load(): the cartridge reads PreferHLE while mapping chips
  core.applyHacks();
  if(!core.emulator->load()) {
    core.error = "the core rejected this cartridge";
    unload();
    return false;
  }

  auto addSlot = [&](const char* label, const Media& slot) {
    if(slot) core.slotCache.push_back({label, (const char*)slot.name()});
  };
  addSlot("Game Boy", core.gameBoy);
  addSlot("BS Memory", core.bsMemory);
  addSlot("Sufami Turbo A", core.sufamiTurboA);
  addSlot("Sufami Turbo B", core.sufamiTurboB);

  for(int port = 0; port < PortCount; port++) {
    impl->emulator->connect(port, impl->connected[port]);
  }
  impl->emulator->power();
  return true;
}

void EmuCore::unload() {
  impl->emulator->unload();
  impl->superFamicom = {};
  impl->gameBoy = {};
  impl->bsMemory = {};
  impl->sufamiTurboA = {};
  impl->sufamiTurboB = {};
  impl->sfcDocument = {};
  impl->info = {};
  impl->slotCache.clear();
}

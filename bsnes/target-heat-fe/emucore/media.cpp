// Getting a game off disk and into memory, before the core sees anything.

#include "impl.hpp"

#include <heuristics/heuristics.hpp>
#include <heuristics/heuristics.cpp>
#include <heuristics/super-famicom.cpp>
#include <heuristics/game-boy.cpp>
#include <heuristics/bs-memory.cpp>
#include <heuristics/sufami-turbo.cpp>

#include <lzma/lzma.hpp>
#include <nall/beat/single/apply.hpp>

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

// bsnes asks per patch whether the ROM is headered; here it is a setting
auto applyPatchIPS(vector<uint8_t>& data, const vector<uint8_t>& patch, bool headered) -> bool {
  if(patch.size() < 8) return false;
  if(memory::compare(patch.data(), "PATCH", 5) != 0) return false;

  for(uint index = 5;;) {
    if(index == patch.size() - 6
    && memory::compare(&patch.data()[index], "EOF", 3) == 0) {
      uint32_t truncate = patch[index + 3] << 16 | patch[index + 4] << 8 | patch[index + 5];
      data.resize(truncate);
      return true;
    }
    if(index == patch.size() - 3
    && memory::compare(&patch.data()[index], "EOF", 3) == 0) return true;
    if(index >= patch.size()) break;

    int32_t offset = patch(index++, 0) << 16 | patch(index++, 0) << 8 | patch(index++, 0);
    if(headered) offset -= 512;
    uint16_t length = patch(index++, 0) << 8 | patch(index++, 0);

    if(length == 0) {  // run-length record: a repeat count and one fill byte
      uint16_t repeat = patch(index++, 0) << 8 | patch(index++, 0);
      uint8_t fill = patch(index++, 0);
      while(repeat--) {
        if(offset >= 0) data(offset) = fill;
        offset++;
      }
    } else {
      while(length--) {
        if(offset >= 0) data(offset) = patch(index, 0);
        offset++;
        index++;
      }
    }
  }
  // the EOF marker was not where it should be, but the data is already patched
  return true;
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

struct Patches { vector<uint8_t> ips, bps; };

// one archive scan answers both suffixes; a loose patch sits beside the ROM
auto readPatches(const string& location, const std::string& patchesDir) -> Patches {
  Patches patches;
  if(location.endsWith("/")) {
    patches.ips = file::read({location, "patch.ips"});
    patches.bps = file::read({location, "patch.bps"});
    return patches;
  }

  if(Location::suffix(location).downcase() == ".zip") {
    Decode::ZIP archive;
    if(archive.open(location)) {
      for(auto& entry : archive.file) {
        if(!patches.ips && entry.name.iendsWith(".ips")) patches.ips = archive.extract(entry);
        if(!patches.bps && entry.name.iendsWith(".bps")) patches.bps = archive.extract(entry);
      }
    }
    if(patches.ips || patches.bps) return patches;
  }

  const string stem = patchesDir.empty()
    ? Location::notsuffix(location)
    : string{patchesDir.c_str(), "/", Location::prefix(location)};
  patches.ips = file::read({stem, ".ips"});
  patches.bps = file::read({stem, ".bps"});
  return patches;
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

}  // namespace

// every slot medium is a manifest plus one program ROM
template<typename Heuristic>
auto EmuCore::Impl::loadCart(Media& slot, const string& location, uint minSize,
                             const vector<string>& pakFiles,
                             const vector<string>& databases) -> bool {
  string manifest;
  auto rom = loadParts(location, pakFiles, manifest);
  if(rom.size() < minSize) return false;

  slot.location = location;
  applyPatches(slot, rom, location);
  slot.manifest = manifest ? manifest : Heuristic(rom, location).manifest();
  slot.program = rom;
  if(!manifest && !databaseDir.empty()) {
    lookupDatabase(slot, databases, Hash::SHA256(rom).digest());
  }
  return true;
}

auto EmuCore::Impl::applyPatches(Media& slot, vector<uint8_t>& rom,
                                 const string& location) -> void {
  auto patches = readPatches(location, patchesDir);

  if(auto& patch = patches.ips) {
    if(applyPatchIPS(rom, patch, ipsHeadered)) slot.patched = true;
    else patchError = {"the .ips patch for ", Location::file(location), " is malformed"};
  }
  if(auto& patch = patches.bps) {
    string manifest, error;
    if(auto output = Beat::Single::apply(rom, patch, manifest, error); output && !error) {
      rom = move(*output);
      slot.patched = true;
    } else {
      patchError = {error ? error : string{"the .bps patch could not be applied"},
                    " -- this patch wants the headerless ROM"};
    }
  }
}

// a hit means the dump is known good, so its manifest beats the heuristic
auto EmuCore::Impl::lookupDatabase(Media& slot, const vector<string>& databases,
                                   const string& sha256, const string& headerTitle) -> void {
  if(databaseDir.empty()) return;

  Markup::Node game;
  for(auto& database : databases) {
    auto text = string::read({databaseDir.c_str(), "/", database, ".bml"});
    // parsing the whole database to answer one hash is the expensive way round
    if(!text.find(sha256)) continue;
    if((game = BML::unserialize(text)[{"game(sha256=", sha256, ")"}])) break;
  }
  if(!game) return;

  slot.manifest = BML::serialize(game);
  // the database omits the header title, which the per-title hotfixes match on
  if(headerTitle) slot.manifest.append("  title: ", headerTitle, "\n");
  slot.verified = true;
}

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
std::string EmuCore::patchError() const { return (const char*)impl->patchError; }
bool EmuCore::patched() const {
  for(const Media* slot : impl->allMedia()) if(slot->patched) return true;
  return false;
}

// verified only if every medium in the machine was found in the database
bool EmuCore::verified() const {
  if(!loaded()) return false;
  for(const Media* slot : impl->allMedia()) {
    if(*slot && !slot->verified) return false;
  }
  return true;
}

void EmuCore::setIpsHeadered(bool headered) { impl->ipsHeadered = headered; }

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

  superFamicom.location = location;
  applyPatches(superFamicom, rom, location);

  auto heuristics = Heuristics::SuperFamicom(rom, location);
  info.checksum = Hash::SHA256(rom).digest();
  superFamicom.manifest = manifest ? manifest : heuristics.manifest();
  // a hand-written manifest beside the ROM wins over the database
  if(!manifest) lookupDatabase(superFamicom, {"Super Famicom"}, info.checksum, heuristics.title());
  sfcDocument = BML::unserialize(superFamicom.manifest);

  // the window title is the file name, not the cartridge header
  info.title = superFamicom.name();
  info.headerTitle = heuristics.title();
  info.region = heuristics.videoRegion();
  info.board = heuristics.board();
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
  impl->patchError = "";

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
  && !core.loadCart<Heuristics::GameBoy>(core.gameBoy, normalize(spec.gameBoy), 0x4000,
                                         {"program.rom"}, {"Game Boy", "Game Boy Color"})) {
    failed = "Game Boy ROM";
  }
  if(want(spec.bsMemory)
  && !core.loadCart<Heuristics::BSMemory>(core.bsMemory, normalize(spec.bsMemory), 0x8000,
                                          {"program.rom", "program.flash"}, {"BS Memory"})) {
    failed = "BS Memory ROM";
  }
  if(want(spec.sufamiTurboA)
  && !core.loadCart<Heuristics::SufamiTurbo>(core.sufamiTurboA, normalize(spec.sufamiTurboA),
                                             0x20000, {"program.rom"}, {"Sufami Turbo"})) {
    failed = "Sufami Turbo cartridge";
  }
  if(want(spec.sufamiTurboB)
  && !core.loadCart<Heuristics::SufamiTurbo>(core.sufamiTurboB, normalize(spec.sufamiTurboB),
                                             0x20000, {"program.rom"}, {"Sufami Turbo"})) {
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
  for(Media* slot : impl->allMedia()) *slot = {};
  impl->sfcDocument = {};
  impl->info = {};
  impl->slotCache.clear();
}

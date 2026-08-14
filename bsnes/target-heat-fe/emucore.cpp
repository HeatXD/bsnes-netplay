#include "emucore.hpp"

#include <emulator/emulator.hpp>
#include <sfc/interface/interface.hpp>
#include <nall/directory.hpp>
using namespace nall;

#include <heuristics/heuristics.hpp>
#include <heuristics/heuristics.cpp>
#include <heuristics/super-famicom.cpp>
#include <heuristics/game-boy.cpp>
#include <heuristics/bs-memory.cpp>

#include "resources.hpp"

#include <array>
#include <cstdlib>

namespace {
constexpr uint SuperFamicomID = 1;
constexpr uint GameBoyID = 2;

// per-title hack overrides ported from target-bsnes's hackCompatibility();
// games that break under fast PPU/DSP timing. region is null to match any.
struct Hotfix { const char* title; const char* region; bool disablePPU; bool disableDSP; };
constexpr Hotfix Hotfixes[] = {
  {"AIR STRIKE PATROL",       nullptr, true,  false},
  {"DESERT FIGHTER",          nullptr, true,  false},
  {"マーヴェラス",             nullptr, true,  false},
  {"SFC クレヨンシンチャン",    nullptr, true,  false},
  {"Winter olympics",         nullptr, true,  false},
  {"WORLD CUP STRIKER",       nullptr, true,  false},
  {"KOUSHIEN_2",              nullptr, false, true},
  {"RENDERING RANGER R2",     nullptr, false, true},
  {"BUBSY II",                "PAL",   false, true},
};

// board() encodes the expansion chip as its first "-"-separated component
auto chipName(string board) -> string {
  auto first = board.trimRight("#A", 1L).split("-")(0);
  if(first == "ARM") return "ST018 (ARM6)";
  if(first == "NEC") return "DSP-1B";
  if(first == "EXNEC") return "ST010/ST011";
  if(first == "GSU") return "SuperFX";
  if(first == "HITACHI") return "Cx4";
  if(first == "SA1") return "SA-1";
  if(first == "SDD1") return "S-DD1";
  if(first == "SPC7110" || first == "EXSPC7110") return "SPC7110";
  if(first == "BS") return "BS-X";
  if(first == "OBC1") return "OBC1";
  if(first == "GB") return "Super Game Boy";
  return "None";
}

auto sizeText(uint bytes) -> string {
  if(!bytes) return "None";
  if(bytes >= 1024 * 1024) return {bytes / (1024 * 1024), " MiB"};
  return {bytes / 1024, " KiB"};
}
}

struct EmuCore::Impl : Emulator::Platform {
  EmuCore& owner;
  Emulator::Interface* emulator = nullptr;

  struct {
    string title;
    string headerTitle;
    string region;
    string board;
    string manifest;
    string checksum;
    string location;
    uint romSize = 0;
    uint ramSize = 0;
    vector<uint8_t> program;
    vector<uint8_t> data;
    vector<uint8_t> expansion;
    vector<uint8_t> firmware;
  } superFamicom;

  // cached hack settings, applied on load/power/reset since several only
  // take effect then; PPU/DSP fast mode may be overridden by a hotfix below
  struct {
    bool ppuFast = true;
    bool ppuNoSpriteLimit = false;
    uint mode7Scale = 1;
    bool dspFast = true;
    bool dspCubic = false;
    bool coprocessorDelayedSync = true;
    bool coprocessorPreferHLE = false;
    bool hotfixes = true;
  } hacks;
  string activeHotfix;

  std::array<uint32_t, 32768> palette{};
  std::vector<uint32_t> videoOut;
  std::vector<float> audioOut;
  std::array<std::array<int16_t, EmuCore::MaxInputs>, EmuCore::PortCount> state{};
  std::array<int, EmuCore::PortCount> connected{EmuCore::Gamepad, EmuCore::Gamepad};

  // the core rebuilds these as fresh nall vectors on every call
  std::array<std::vector<EmuCore::DeviceInfo>, EmuCore::PortCount> deviceCache;
  std::array<std::vector<EmuCore::InputInfo>, EmuCore::DeviceCount> inputCache;

  Impl(EmuCore& owner) : owner(owner) {
    Emulator::platform = this;
    emulator = new SuperFamicom::Interface;
    videoOut.resize(EmuCore::MaxWidth * EmuCore::MaxHeight);
    audioOut.reserve(2048);
    buildPalette();

    for(int port = 0; port < EmuCore::PortCount; port++) {
      for(auto& device : emulator->devices(port)) {
        deviceCache[port].push_back({(int)device.id, (const char*)device.name});
      }
    }
    for(int device = 0; device < EmuCore::DeviceCount; device++) {
      for(auto& input : emulator->inputs(device)) {
        if((int)inputCache[device].size() >= EmuCore::MaxInputs) break;
        inputCache[device].push_back({(const char*)input.name, (int)input.type});
      }
    }
  }

  ~Impl() {
    if(emulator) emulator->unload();
    delete emulator;
    Emulator::platform = nullptr;
  }

  auto buildPalette() -> void;
  // recomputes per-title overrides and pushes all cached hacks to the core
  auto applyHacks() -> void;

  //Emulator::Platform
  auto open(uint id, string name, vfs::file::mode mode, bool required) -> shared_pointer<vfs::file> override;
  auto load(uint id, string name, string type, vector<string> options) -> Emulator::Platform::Load override;
  auto videoFrame(const uint16* data, uint pitch, uint width, uint height, uint scale) -> void override;
  auto audioFrame(const double* samples, uint channels) -> void override;
  auto inputPoll(uint port, uint device, uint input) -> int16 override;
};

auto EmuCore::Impl::buildPalette() -> void {
  // bsnes's ramp, luminance and saturation left at 1.0. Only 32 levels are
  // distinct, so this is a table rather than 98304 pow() calls.
  static constexpr double gamma = 1.5;

  uint32_t ramp[32];
  for(uint level : range(32)) {
    uint16 value = level << 3 | level >> 2;
    value = value << 8 | value;
    if(value <= 32767) value = uint16(32767 * pow(value / 32767.0, gamma));
    ramp[level] = value >> 8;
  }

  for(uint color : range(32768)) {
    palette[color] = 0xff000000
                   | ramp[(color >> 10) & 31] << 16
                   | ramp[(color >> 5) & 31] << 8
                   | ramp[(color >> 0) & 31];
  }
}

auto EmuCore::Impl::applyHacks() -> void {
  bool ppuFast = hacks.ppuFast;
  bool dspFast = hacks.dspFast;
  activeHotfix = "";

  if(hacks.hotfixes) {
    for(auto& hf : Hotfixes) {
      if(superFamicom.headerTitle != hf.title) continue;
      if(hf.region && superFamicom.region != hf.region) continue;
      if(hf.disablePPU && ppuFast) { ppuFast = false; activeHotfix = {"PPU fast mode disabled for ", hf.title}; }
      if(hf.disableDSP && dspFast) { dspFast = false; activeHotfix = {"DSP fast mode disabled for ", hf.title}; }
    }
  }

  emulator->configure("Hacks/PPU/Fast", ppuFast);
  emulator->configure("Hacks/PPU/NoSpriteLimit", hacks.ppuNoSpriteLimit);
  emulator->configure("Hacks/PPU/Mode7/Scale", hacks.mode7Scale);
  emulator->configure("Hacks/DSP/Fast", dspFast);
  emulator->configure("Hacks/DSP/Cubic", hacks.dspCubic);
  emulator->configure("Hacks/Coprocessor/DelayedSync", hacks.coprocessorDelayedSync);
  emulator->configure("Hacks/Coprocessor/PreferHLE", hacks.coprocessorPreferHLE);
}

auto EmuCore::Impl::open(uint id, string name, vfs::file::mode mode, bool required) -> shared_pointer<vfs::file> {
  if(name == "ipl.rom" && mode == vfs::file::mode::read) {
    return vfs::memory::file::open(iplrom, sizeof(iplrom));
  }
  if(name == "boards.bml" && mode == vfs::file::mode::read) {
    return vfs::memory::file::open(Boards, sizeof(Boards));
  }

  if(id == SuperFamicomID) {
    if(name == "manifest.bml" && mode == vfs::file::mode::read) {
      return vfs::memory::file::open(superFamicom.manifest.data<uint8_t>(), superFamicom.manifest.size());
    }
    if(name == "program.rom" && mode == vfs::file::mode::read) {
      return vfs::memory::file::open(superFamicom.program.data(), superFamicom.program.size());
    }
    if(name == "data.rom" && mode == vfs::file::mode::read) {
      return vfs::memory::file::open(superFamicom.data.data(), superFamicom.data.size());
    }
    if(name == "expansion.rom" && mode == vfs::file::mode::read) {
      return vfs::memory::file::open(superFamicom.expansion.data(), superFamicom.expansion.size());
    }
    if(name == "arm6.program.rom" || name == "hg51bs169.program.rom"
    || name == "upd7725.program.rom" || name == "upd96050.program.rom") {
      return vfs::memory::file::open(superFamicom.firmware.data(), superFamicom.firmware.size());
    }

    // save RAM sits next to the ROM
    string path = {Location::notsuffix(superFamicom.location), ".", name};
    if(mode == vfs::file::mode::read && !file::exists(path)) return {};
    return vfs::fs::file::open(path, mode);
  }

  return {};
}

auto EmuCore::Impl::load(uint id, string, string, vector<string>) -> Emulator::Platform::Load {
  if(id == SuperFamicomID) return {id, superFamicom.region};
  if(id == GameBoyID) return {id, ""};
  return {};
}

auto EmuCore::Impl::videoFrame(const uint16* data, uint pitch, uint width, uint height, uint scale) -> void {
  if(!owner.onVideo) return;

  // drop the 8 overscan lines top and bottom
  uint multiplier = height / 240;
  data += 8 * (pitch >> 1) * multiplier;
  height -= 16 * multiplier;

  if(width > (uint)EmuCore::MaxWidth) width = EmuCore::MaxWidth;
  if(height > (uint)EmuCore::MaxHeight) height = EmuCore::MaxHeight;

  for(uint y : range(height)) {
    const uint16* src = data + y * (pitch >> 1);
    uint32_t* dst = videoOut.data() + y * width;
    for(uint x : range(width)) dst[x] = palette[src[x] & 0x7fff];
  }

  owner.onVideo(videoOut.data(), (int)width, (int)height);
}

// the core calls this once per resampled sample, ~800 times a frame; runFrame
// hands the frontend the whole block instead
auto EmuCore::Impl::audioFrame(const double* samples, uint channels) -> void {
  audioOut.push_back((float)samples[0]);
  audioOut.push_back((float)samples[channels > 1 ? 1 : 0]);
}

auto EmuCore::Impl::inputPoll(uint port, uint device, uint input) -> int16 {
  if(port >= state.size()) return 0;
  if(input >= EmuCore::MaxInputs) return 0;
  return state[port][input];
}

EmuCore::EmuCore() : impl(std::make_unique<Impl>(*this)) {}
EmuCore::~EmuCore() = default;

bool EmuCore::loaded() const { return impl->emulator->loaded(); }
std::string EmuCore::title() const { return (const char*)impl->superFamicom.title; }
std::string EmuCore::headerTitle() const { return (const char*)impl->superFamicom.headerTitle; }

std::string EmuCore::manifest() const { return (const char*)impl->superFamicom.manifest; }
std::string EmuCore::region() const { return (const char*)impl->superFamicom.region; }
std::string EmuCore::board() const { return (const char*)impl->superFamicom.board; }
std::string EmuCore::romSizeText() const { return (const char*)sizeText(impl->superFamicom.romSize); }
std::string EmuCore::ramSizeText() const { return (const char*)sizeText(impl->superFamicom.ramSize); }
std::string EmuCore::expansionChip() const { return (const char*)chipName(impl->superFamicom.board); }
std::string EmuCore::checksum() const { return (const char*)impl->superFamicom.checksum; }

bool EmuCore::setOption(const std::string& name, const std::string& value) {
  auto& h = impl->hacks;
  bool on = value == "true" || value == "1";

  if(name == "Frontend/Hotfixes") { h.hotfixes = on; impl->applyHacks(); return true; }
  if(name == "Hacks/PPU/Fast") { h.ppuFast = on; impl->applyHacks(); return true; }
  if(name == "Hacks/PPU/NoSpriteLimit") { h.ppuNoSpriteLimit = on; impl->applyHacks(); return true; }
  if(name == "Hacks/PPU/Mode7/Scale") { h.mode7Scale = std::atoi(value.c_str()); impl->applyHacks(); return true; }
  if(name == "Hacks/DSP/Fast") { h.dspFast = on; impl->applyHacks(); return true; }
  if(name == "Hacks/DSP/Cubic") { h.dspCubic = on; impl->applyHacks(); return true; }
  if(name == "Hacks/Coprocessor/DelayedSync") { h.coprocessorDelayedSync = on; impl->applyHacks(); return true; }
  if(name == "Hacks/Coprocessor/PreferHLE") { h.coprocessorPreferHLE = on; impl->applyHacks(); return true; }

  return impl->emulator->configure(name.c_str(), value.c_str());
}

std::string EmuCore::activeHotfix() const { return (const char*)impl->activeHotfix; }

// 1364 master cycles a scanline over 262 lines NTSC, 312 PAL, at the region's
// cpu clock. Unloaded reports NTSC.
double EmuCore::refreshRate() const {
  if(impl->superFamicom.region == "PAL") return 21281370.0 / (1364.0 * 312.0);
  return 21477272.0 / (1364.0 * 262.0);
}

void EmuCore::setAudioFrequency(double hz) { Emulator::audio.setFrequency(hz); }
void EmuCore::setSpeedScale(double scale) { Emulator::audio.setSpeedScale(scale); }

void EmuCore::setInput(int port, int index, int16_t value) {
  if(port < 0 || port >= PortCount) return;
  if(index < 0 || index >= MaxInputs) return;
  impl->state[port][index] = value;
}

const std::vector<EmuCore::DeviceInfo>& EmuCore::devices(int port) const {
  static const std::vector<DeviceInfo> none;
  if(port < 0 || port >= PortCount) return none;
  return impl->deviceCache[port];
}

const std::vector<EmuCore::InputInfo>& EmuCore::inputs(int deviceId) const {
  static const std::vector<InputInfo> none;
  if(deviceId < 0 || deviceId >= DeviceCount) return none;
  return impl->inputCache[deviceId];
}

int EmuCore::connectedDevice(int port) const {
  if(port < 0 || port >= PortCount) return None;
  return impl->connected[port];
}

void EmuCore::connect(int port, int deviceId) {
  if(port < 0 || port >= PortCount) return;
  impl->connected[port] = deviceId;
  impl->state[port].fill(0);
  if(impl->emulator->loaded()) impl->emulator->connect(port, deviceId);
}

void EmuCore::power() { impl->applyHacks(); impl->emulator->power(); }
void EmuCore::reset() { impl->applyHacks(); impl->emulator->reset(); }
void EmuCore::runFrame() {
  impl->audioOut.clear();
  impl->emulator->run();
  if(onAudio && !impl->audioOut.empty()) {
    onAudio(impl->audioOut.data(), (int)(impl->audioOut.size() / 2));
  }
}

void EmuCore::unload() {
  impl->emulator->unload();
  impl->superFamicom.location = "";
}

bool EmuCore::loadSuperFamicom(const std::string& path) {
  // nall's Location:: helpers only split on '/', so a windows path would come
  // back whole and end up in the window title and the manifest label
  string location = string{path.c_str()}.transform("\\", "/");
  auto rom = file::read(location);
  if(rom.size() < 0x8000) return false;

  // strip a copier header if present
  if((rom.size() & 0x7fff) == 512) {
    memory::move(&rom[0], &rom[512], rom.size() - 512);
    rom.resize(rom.size() - 512);
  }

  auto heuristics = Heuristics::SuperFamicom(rom, location);
  auto& sfc = impl->superFamicom;
  // bsnes titles its window with the file name, not the cartridge header
  sfc.title = Location::prefix(location);
  sfc.headerTitle = heuristics.title();
  sfc.region = heuristics.videoRegion();
  sfc.board = heuristics.board();
  sfc.manifest = heuristics.manifest();
  sfc.checksum = Hash::SHA256(rom).digest();
  sfc.location = location;
  sfc.romSize = heuristics.romSize();
  sfc.ramSize = heuristics.ramSize() + heuristics.expansionRamSize();

  uint offset = 0;
  auto take = [&](uint size, vector<uint8_t>& out) {
    if(!size) return;
    out.resize(size);
    memory::copy(&out[0], &rom[offset], size);
    offset += size;
  };
  take(heuristics.programRomSize(), sfc.program);
  take(heuristics.dataRomSize(), sfc.data);
  take(heuristics.expansionRomSize(), sfc.expansion);
  take(heuristics.firmwareRomSize(), sfc.firmware);

  impl->emulator->unload();
  if(!impl->emulator->load()) return false;

  for(int port = 0; port < PortCount; port++) {
    impl->emulator->connect(port, impl->connected[port]);
  }
  impl->applyHacks();
  impl->emulator->power();
  return true;
}

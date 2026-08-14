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

// Boards[] and iplrom[] both live here.
#include "resources.hpp"

#include <array>

namespace {
constexpr uint SuperFamicomID = 1;
constexpr uint GameBoyID = 2;
}

struct EmuCore::Impl : Emulator::Platform {
  EmuCore& owner;
  Emulator::Interface* emulator = nullptr;

  struct {
    string title;
    string region;
    string manifest;
    string location;
    vector<uint8_t> program;
    vector<uint8_t> data;
    vector<uint8_t> expansion;
    vector<uint8_t> firmware;
  } superFamicom;

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

  //Emulator::Platform
  auto open(uint id, string name, vfs::file::mode mode, bool required) -> shared_pointer<vfs::file> override;
  auto load(uint id, string name, string type, vector<string> options) -> Emulator::Platform::Load override;
  auto videoFrame(const uint16* data, uint pitch, uint width, uint height, uint scale) -> void override;
  auto audioFrame(const double* samples, uint channels) -> void override;
  auto inputPoll(uint port, uint device, uint input) -> int16 override;
};

auto EmuCore::Impl::buildPalette() -> void {
  // bsnes's ramp with luminance and saturation left at 1.0
  static constexpr double gamma = 1.5;

  auto curve = [](uint16 value) -> uint16 {
    return value > 32767 ? value : uint16(32767 * pow(value / 32767.0, gamma));
  };

  for(uint color : range(32768)) {
    uint16 r = (color >> 10) & 31;
    uint16 g = (color >> 5) & 31;
    uint16 b = (color >> 0) & 31;

    r = r << 3 | r >> 2; r = r << 8 | r << 0;
    g = g << 3 | g >> 2; g = g << 8 | g << 0;
    b = b << 3 | b >> 2; b = b << 8 | b << 0;

    palette[color] = 0xff000000 | (curve(r) >> 8 << 16) | (curve(g) >> 8 << 8) | (curve(b) >> 8);
  }
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

void EmuCore::power() { impl->emulator->power(); }
void EmuCore::reset() { impl->emulator->reset(); }
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
  sfc.region = heuristics.videoRegion();
  sfc.manifest = heuristics.manifest();
  sfc.location = location;

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
  impl->emulator->power();
  return true;
}

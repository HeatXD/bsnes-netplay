// Lifecycle, core options and the input plumbing.

#include "impl.hpp"

#include <cstdlib>

namespace {
// per-title overrides for games that break under fast PPU/DSP timing;
// region is null to match any
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
}  // namespace

EmuCore::Impl::Impl(EmuCore& owner) : owner(owner) {
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

EmuCore::Impl::~Impl() {
  if(emulator) emulator->unload();
  delete emulator;
  Emulator::platform = nullptr;
}

auto EmuCore::Impl::applyHacks() -> void {
  bool ppuFast = hacks.ppuFast;
  bool dspFast = hacks.dspFast;
  activeHotfix = "";

  // unconditional: these games are broken under fast timing and the
  // hotfixes toggle never re-enables it for them
  for(auto& hf : Hotfixes) {
    if(info.headerTitle != hf.title) continue;
    if(hf.region && info.region != hf.region) continue;
    if(hf.disablePPU && ppuFast) { ppuFast = false; activeHotfix = {"PPU fast mode disabled for ", hf.title}; }
    if(hf.disableDSP && dspFast) { dspFast = false; activeHotfix = {"DSP fast mode disabled for ", hf.title}; }
  }

  emulator->configure("Hacks/Hotfixes", hacks.hotfixes);
  emulator->configure("Hacks/PPU/Fast", ppuFast);
  emulator->configure("Hacks/PPU/NoSpriteLimit", hacks.ppuNoSpriteLimit);
  emulator->configure("Hacks/PPU/Mode7/Scale", hacks.mode7Scale);
  emulator->configure("Hacks/DSP/Fast", dspFast);
  emulator->configure("Hacks/DSP/Cubic", hacks.dspCubic);
  emulator->configure("Hacks/Coprocessor/DelayedSync", hacks.coprocessorDelayedSync);
  emulator->configure("Hacks/Coprocessor/PreferHLE", hacks.coprocessorPreferHLE);

  // a hotfix can take fast mode away, so the frame skip has to follow it here
  ppuFastActive = ppuFast;
  emulator->setFrameSkip(ppuFast ? frameSkip : 0);
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

int16_t EmuCore::inputValue(int port, int index) const {
  if(port < 0 || port >= PortCount || index < 0 || index >= MaxInputs) return 0;
  return impl->state[port][index];
}

EmuCore::EmuCore() : impl(std::make_unique<Impl>(*this)) {}
EmuCore::~EmuCore() = default;

bool EmuCore::loaded() const { return impl->emulator->loaded(); }
std::string EmuCore::version() { return (const char*)Emulator::Version; }

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
  if(impl->info.region == "PAL") return 21281370.0 / (1364.0 * 312.0);
  return 21477272.0 / (1364.0 * 262.0);
}

void EmuCore::setSavesDirectory(const std::string& dir) { impl->savesDir = dir; }
void EmuCore::setFirmwareDirectory(const std::string& dir) { impl->firmwareDir = dir; }
void EmuCore::setDatabaseDirectory(const std::string& dir) { impl->databaseDir = dir; }
void EmuCore::setPatchesDirectory(const std::string& dir) { impl->patchesDir = dir; }

// the core writes every dirty memory out through the same path unload uses
void EmuCore::saveMemory() {
  if(!loaded()) return;
  impl->emulator->save();
}

void EmuCore::setAudioFrequency(double hz) { Emulator::audio.setFrequency(hz); }
void EmuCore::setSpeedScale(double scale) { Emulator::audio.setSpeedScale(scale); }
void EmuCore::setAudioBalance(double balance) { Emulator::audio.setBalance(balance); }
void EmuCore::setFrameSkip(int frames) {
  impl->frameSkip = frames < 0 ? 0 : frames;
  impl->emulator->setFrameSkip(impl->ppuFastActive ? impl->frameSkip : 0);
}

void EmuCore::setInput(int port, int index, int16_t value) {
  if(port < 0 || port >= PortCount) return;
  if(index < 0 || index >= MaxInputs) return;
  impl->state[port][index] = value;
}

int EmuCore::playersFor(int deviceId) const {
  if(deviceId == SuperMultitap) return MaxPlayers;
  return deviceId == Justifiers ? 2 : 1;  // chained guns, one cursor each
}

int EmuCore::inputStride(int deviceId) const {
  return (int)inputs(deviceId).size() / playersFor(deviceId);
}

bool EmuCore::isPointer(int deviceId) const {
  const auto& list = inputs(deviceId);
  return !list.empty() && list[0].type == Axis;
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

// powering an unloaded core walks cartridge state that does not exist yet
void EmuCore::power() {
  if(!loaded()) return;
  impl->applyHacks();
  impl->emulator->power();
}

void EmuCore::reset() {
  if(!loaded()) return;
  impl->applyHacks();
  impl->emulator->reset();
}

// the core downgrades Fast to Strict itself where it corrupts (sfc/system/system.cpp:24)
bool EmuCore::deterministicStates() { return co_serializable(); }

std::vector<uint8_t> EmuCore::serialize(bool synchronize) {
  if(!loaded()) return {};
  serializer s = impl->emulator->serialize(synchronize);
  if(!s.size()) return {};
  return std::vector<uint8_t>(s.data(), s.data() + s.size());
}

// the core checks its header but not the buffer length, so a short blob reads off the end
std::vector<EmuCore::StateComponent> EmuCore::stateMap(bool synchronize) {
  std::vector<StateComponent> map;
  if(!loaded()) return map;
  for(auto& part : impl->emulator->serializeMap(synchronize)) {
    map.push_back({(const char*)part.name, (int)part.offset, (int)part.size, part.hostState});
  }
  return map;
}

bool EmuCore::unserialize(const std::vector<uint8_t>& state) {
  if(!loaded() || state.size() < 8) return false;

  auto field = [&](int index) {
    const uint8_t* at = state.data() + index * 4;
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
  };
  if(field(0) != 0x31545342) return false;  // "BST1", as the core stamps it
  if(field(1) != state.size()) return false;

  serializer s{state.data(), (uint)state.size()};
  return impl->emulator->unserialize(s);
}

void EmuCore::runFrame() {
  impl->audioOut.clear();
  impl->emulator->run();
  if(onAudio && !impl->audioOut.empty()) {
    onAudio(impl->audioOut.data(), (int)(impl->audioOut.size() / 2));
  }
}

void EmuCore::setRunAhead(bool enabled) {
  if(loaded()) impl->emulator->setRunAhead(enabled);
}

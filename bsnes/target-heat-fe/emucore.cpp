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
#include <filter/filter.hpp>

#include <array>

namespace {
constexpr uint SuperFamicomID = 1;
constexpr uint GameBoyID = 2;

struct FilterEntry {
  const char* name;
  Filter::Size size;
  Filter::Render render;
  // input dimensions the filter was written for; 0 means always eligible.
  // frames outside this (hires, HD mode 7) fall back to None, same as bsnes.
  uint maxWidth, maxHeight;
};

constexpr FilterEntry Filters[] = {
  {"None",              &Filter::None::size,           &Filter::None::render,           0,   0},
  {"Scanlines (Light)",  &Filter::ScanlinesLight::size, &Filter::ScanlinesLight::render, 512, 240},
  {"Scanlines (Dark)",   &Filter::ScanlinesDark::size,  &Filter::ScanlinesDark::render,  512, 240},
  {"Scanlines (Black)",  &Filter::ScanlinesBlack::size, &Filter::ScanlinesBlack::render, 512, 240},
  {"Pixellate 2x",       &Filter::Pixellate2x::size,    &Filter::Pixellate2x::render,    512, 480},
  {"Scale2x",            &Filter::Scale2x::size,        &Filter::Scale2x::render,        256, 240},
  {"2xSaI",               &Filter::_2xSaI::size,        &Filter::_2xSaI::render,         256, 240},
  {"Super 2xSaI",        &Filter::Super2xSaI::size,     &Filter::Super2xSaI::render,     256, 240},
  {"Super Eagle",        &Filter::SuperEagle::size,     &Filter::SuperEagle::render,     256, 240},
  {"LQ2x",               &Filter::LQ2x::size,           &Filter::LQ2x::render,           256, 240},
  {"HQ2x",               &Filter::HQ2x::size,           &Filter::HQ2x::render,           256, 240},
  {"NTSC (RF)",          &Filter::NTSC_RF::size,        &Filter::NTSC_RF::render,        512, 480},
  {"NTSC (Composite)",   &Filter::NTSC_Composite::size, &Filter::NTSC_Composite::render, 512, 480},
  {"NTSC (S-Video)",     &Filter::NTSC_SVideo::size,    &Filter::NTSC_SVideo::render,    512, 480},
  {"NTSC (RGB)",         &Filter::NTSC_RGB::size,       &Filter::NTSC_RGB::render,       512, 480},
};
constexpr int FilterCount = sizeof(Filters) / sizeof(Filters[0]);
}

struct EmuCore::Impl : Emulator::Platform {
  EmuCore& owner;
  Emulator::Interface* emulator = nullptr;

  struct {
    string title;
    string headerTitle;
    string region;
    string manifest;
    string location;
    vector<uint8_t> program;
    vector<uint8_t> data;
    vector<uint8_t> expansion;
    vector<uint8_t> firmware;
  } superFamicom;

  std::array<uint32_t, 32768> palette{};
  std::vector<uint32_t> videoOut;      // final cropped frame, tightly packed
  std::vector<uint32_t> filterScratch; // pre-crop filter output, resized per frame
  std::vector<float> audioOut;

  bool overscanCrop = true;
  int videoGamma = 150, videoLuminance = 100, videoSaturation = 100;
  int filterIndex = 0;  // index into Filters[]
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

// Mirrors bsnes's own Program::updateVideoPalette (target-bsnes/program/video.cpp):
// saturation mixes channels via a per-pixel grayscale, then gamma, then luminance.
auto EmuCore::Impl::buildPalette() -> void {
  const double gamma = videoGamma / 100.0;
  const double luminance = videoLuminance / 100.0;
  const double saturation = videoSaturation / 100.0;
  auto clamp16 = [](double v) -> uint16 { return v > 65535.0 ? uint16(65535) : uint16(v); };

  if(saturation == 1.0) {
    // fast path: with saturation pinned at 1.0 each channel only depends on
    // its own 5-bit value, so a 32-entry ramp stands in for all 32768 entries.
    uint32_t ramp[32];
    for(uint level : range(32)) {
      uint16 value = level << 3 | level >> 2;
      value = value << 8 | value;
      if(value <= 32767) value = uint16(32767 * pow(value / 32767.0, gamma));
      if(luminance != 1.0) value = clamp16(value * luminance);
      ramp[level] = value >> 8;
    }
    for(uint color : range(32768)) {
      palette[color] = 0xff000000
                     | ramp[(color >> 10) & 31] << 16
                     | ramp[(color >> 5) & 31] << 8
                     | ramp[(color >> 0) & 31];
    }
    return;
  }

  // slow path: saturation mixes channels, so the ramp shortcut no longer
  // applies and all 32768 entries need computing individually.
  for(uint color : range(32768)) {
    uint16 r = (color >> 10) & 31;
    uint16 g = (color >> 5) & 31;
    uint16 b = (color >> 0) & 31;
    r = r << 3 | r >> 2; r = r << 8 | r;
    g = g << 3 | g >> 2; g = g << 8 | g;
    b = b << 3 | b >> 2; b = b << 8 | b;

    uint16 grayscale = clamp16((r + g + b) / 3.0);
    double inverse = 1.0 - saturation > 0.0 ? 1.0 - saturation : 0.0;
    r = clamp16(r * saturation + grayscale * inverse);
    g = clamp16(g * saturation + grayscale * inverse);
    b = clamp16(b * saturation + grayscale * inverse);

    if(gamma != 1.0) {
      if(r <= 32767) r = uint16(32767 * pow(r / 32767.0, gamma));
      if(g <= 32767) g = uint16(32767 * pow(g / 32767.0, gamma));
      if(b <= 32767) b = uint16(32767 * pow(b / 32767.0, gamma));
    }

    if(luminance != 1.0) {
      r = clamp16(r * luminance);
      g = clamp16(g * luminance);
      b = clamp16(b * luminance);
    }

    palette[color] = 0xff000000 | (r >> 8) << 16 | (g >> 8) << 8 | (b >> 8);
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

  // HD mode 7 and hires/interlaced frames outside a filter's working size
  // fall back to the identity filter, same as bsnes's own filterSelect
  const FilterEntry* filter = &Filters[filterIndex];
  bool eligible = scale == 1 && filter->maxWidth
                && width <= filter->maxWidth && height <= filter->maxHeight;
  if(filterIndex != 0 && !eligible) filter = &Filters[0];

  uint filterWidth = width, filterHeight = height;
  filter->size(filterWidth, filterHeight);

  filterScratch.resize((size_t)filterWidth * filterHeight);
  filter->render(palette.data(), filterScratch.data(), filterWidth * (uint)sizeof(uint32_t),
                 data, pitch, width, height);

  const uint32_t* src = filterScratch.data();
  uint outWidth = filterWidth, outHeight = filterHeight;

  if(overscanCrop) {
    // crop scales with whatever the filter did to the vertical resolution;
    // this must run after filtering or a 2x/scanline filter halves the wrong
    // count of lines
    uint cropLines = 8 * (filterHeight / 240);
    src += (size_t)cropLines * filterWidth;
    outHeight -= cropLines * 2;
  }

  if(outWidth > (uint)EmuCore::MaxWidth) outWidth = EmuCore::MaxWidth;
  if(outHeight > (uint)EmuCore::MaxHeight) outHeight = EmuCore::MaxHeight;

  for(uint y : range(outHeight)) {
    memory::copy(videoOut.data() + y * outWidth, src + y * filterWidth, outWidth * sizeof(uint32_t));
  }

  owner.onVideo(videoOut.data(), (int)outWidth, (int)outHeight);
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

bool EmuCore::setOption(const std::string& name, const std::string& value) {
  return impl->emulator->configure(name.c_str(), value.c_str());
}

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

void EmuCore::setOverscanCrop(bool crop) { impl->overscanCrop = crop; }

void EmuCore::setPaletteAdjust(int gammaPercent, int luminancePercent, int saturationPercent) {
  impl->videoGamma = gammaPercent;
  impl->videoLuminance = luminancePercent;
  impl->videoSaturation = saturationPercent;
  impl->buildPalette();
}

void EmuCore::setFilter(const std::string& name) {
  for(int i = 0; i < FilterCount; i++) {
    if(name == Filters[i].name) { impl->filterIndex = i; return; }
  }
  impl->filterIndex = 0;
}

std::vector<std::string> EmuCore::filterNames() const {
  std::vector<std::string> names;
  for(int i = 0; i < FilterCount; i++) names.push_back(Filters[i].name);
  return names;
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
  sfc.headerTitle = heuristics.title();
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

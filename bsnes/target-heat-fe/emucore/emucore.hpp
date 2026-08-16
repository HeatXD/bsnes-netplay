#pragma once

// Frontend-facing view of the bsnes core. Deliberately free of nall and
// windows.h so the SDL3 and imgui code never sees them.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class EmuCore {
public:
  // starting size, not a cap: 640 clears SNES_NTSC_OUT_WIDTH(256) == 602, the
  // widest CPU filter output. HD mode 7 grows both buffer and texture past it.
  static constexpr int MaxWidth = 640;
  static constexpr int MaxHeight = 480;

  // controller ports 1 and 2; the expansion port is not mapped
  static constexpr int PortCount = 2;
  // super multitap is the widest device, at 4 pads
  static constexpr int MaxInputs = 64;
  // per device, not per console: five players is a multitap plus the other port
  static constexpr int MaxPlayers = 4;
  static constexpr int DeviceCount = 9;

  // matches SuperFamicom::Gamepad's enum order
  enum Button { Up, Down, Left, Right, B, A, Y, X, L, R, Select, Start, ButtonCount };

  // matches SuperFamicom::ID::Device
  enum Device { None, Gamepad, Mouse, SuperMultitap, SuperScope, Justifier, Justifiers };

  // matches Emulator::Interface::Input::Type
  enum InputType { Hat, ButtonInput, Trigger, Control, Axis, Rumble };

  struct DeviceInfo { int id; std::string name; };
  struct InputInfo { std::string name; int type; };

  // the Super Famicom cartridge is the base; the rest fill slots it asks for
  struct GameSpec {
    std::string superFamicom;
    std::string gameBoy;
    std::string bsMemory;
    std::string sufamiTurboA;
    std::string sufamiTurboB;
  };

  struct SlotInfo { std::string label; std::string game; };

  // only this layer can see inside an archive, so it picks the slot
  enum class Medium { SuperFamicom, GameBoy, BSMemory, SufamiTurbo };
  static Medium mediumOf(const std::string& location);

  // the bsnes core's own version
  static std::string version();

  EmuCore();
  ~EmuCore();

  EmuCore(const EmuCore&) = delete;
  auto operator=(const EmuCore&) -> EmuCore& = delete;

  // a location is a ROM, a .zip/.7z holding one, or a pak folder ending in '/'
  bool load(const GameSpec& spec);
  // why the last load() failed, phrased for the status line
  std::string loadError() const;
  // a load succeeds without these, running the coprocessor on zeroes
  std::string missingFiles() const;
  void unload();
  bool loaded() const;

  void power();
  void reset();
  void runFrame();

  // the file name, as bsnes titles its window
  std::string title() const;
  // the cartridge header's own title, which hotfixes match on
  std::string headerTitle() const;

  // heuristics summary for the Cartridge window; empty when nothing is loaded
  std::string manifest() const;
  std::string region() const;
  std::string board() const;
  std::string romSizeText() const;
  std::string ramSizeText() const;
  std::string expansionChip() const;
  std::string checksum() const;
  // the media sitting in the base cartridge's slots
  const std::vector<SlotInfo>& slots() const;

  // core setting, by the interface's own name: "Hacks/PPU/Fast" and friends.
  // "Frontend/Hotfixes" is a pseudo key for the per-title hotfix toggle,
  // which isn't a real Emulator::Interface option
  bool setOption(const std::string& name, const std::string& value);
  // non-empty when a per-title hotfix is currently overriding a hack
  std::string activeHotfix() const;

  // 60.098Hz for NTSC carts, 50.007Hz for PAL ones
  double refreshRate() const;

  // empty means save RAM sits next to the ROM (the default)
  void setSavesDirectory(const std::string& dir);
  // holds dsp1b.program.rom and friends, for carts that ship no firmware
  void setFirmwareDirectory(const std::string& dir);
  void setAudioFrequency(double hz);
  // slowdown factor: pass 1/N for Nx speed, so output stays at one rate
  void setSpeedScale(double scale);
  // frames to drop between rendered ones; only the fast PPU honours it
  void setFrameSkip(int frames);
  void setInput(int port, int index, int16_t value);

  // video: overscan crop, palette adjustment (percentages match bsnes's own
  // slider ranges: gamma 100-200, luminance 0-100, saturation 0-200), and
  // the CPU filter, selected by name so a settings file survives reordering
  void setOverscanCrop(bool crop);
  void setPaletteAdjust(int gammaPercent, int luminancePercent, int saturationPercent);
  void setFilter(const std::string& name);
  std::vector<std::string> filterNames() const;

  // controllers a device carries; only the multitap holds more than one
  int playersFor(int deviceId) const;

  // what the core actually supports, rather than a hardcoded list
  const std::vector<DeviceInfo>& devices(int port) const;
  const std::vector<InputInfo>& inputs(int deviceId) const;
  void connect(int port, int deviceId);
  int connectedDevice(int port) const;

  // both fire from whichever thread calls runFrame(); onAudio gets the whole
  // frame's worth of samples in one call
  std::function<void(const uint32_t* argb, int width, int height)> onVideo;
  std::function<void(const float* interleaved, int frames)> onAudio;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

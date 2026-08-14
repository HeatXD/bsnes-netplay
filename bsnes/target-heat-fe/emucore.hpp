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
  static constexpr int MaxWidth = 512;
  static constexpr int MaxHeight = 480;

  // controller ports 1 and 2; the expansion port is not mapped
  static constexpr int PortCount = 2;
  // super multitap is the widest device, at 4 pads
  static constexpr int MaxInputs = 64;
  static constexpr int DeviceCount = 9;

  // matches SuperFamicom::Gamepad's enum order
  enum Button { Up, Down, Left, Right, B, A, Y, X, L, R, Select, Start, ButtonCount };

  // matches SuperFamicom::ID::Device
  enum Device { None, Gamepad, Mouse, SuperMultitap, SuperScope, Justifier, Justifiers };

  // matches Emulator::Interface::Input::Type
  enum InputType { Hat, ButtonInput, Trigger, Control, Axis, Rumble };

  struct DeviceInfo { int id; std::string name; };
  struct InputInfo { std::string name; int type; };

  EmuCore();
  ~EmuCore();

  EmuCore(const EmuCore&) = delete;
  auto operator=(const EmuCore&) -> EmuCore& = delete;

  bool loadSuperFamicom(const std::string& path);
  void unload();
  bool loaded() const;

  void power();
  void reset();
  void runFrame();

  // the file name, as bsnes titles its window
  std::string title() const;
  // the cartridge header's own title, which hotfixes match on
  std::string headerTitle() const;

  // core setting, by the interface's own name: "Hacks/PPU/Fast" and friends
  bool setOption(const std::string& name, const std::string& value);
  // 60.098Hz for NTSC carts, 50.007Hz for PAL ones
  double refreshRate() const;

  // empty means save RAM sits next to the ROM (the default)
  void setSavesDirectory(const std::string& dir);
  void setAudioFrequency(double hz);
  // slowdown factor: pass 1/N for Nx speed, so output stays at one rate
  void setSpeedScale(double scale);
  void setInput(int port, int index, int16_t value);

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

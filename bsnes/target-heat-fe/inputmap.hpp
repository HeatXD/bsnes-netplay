#pragma once

#include "emucore.hpp"
#include "settings.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

struct Binding {
  enum Type : int { None = 0, Key, PadButton, PadAxis };

  Type type = None;
  int code = 0;
  int direction = 0;  // PadAxis only: +1 or -1

  // the pad names its own face buttons, so nintendo layouts read B/A/Y/X
  std::string label(SDL_Gamepad* pad = nullptr) const;
};

SDL_Gamepad* resolvePad(const std::vector<SDL_Gamepad*>& pads, int index);

// Two slots per input so a port can be driven by keyboard and pad at once.
// Bindings are kept per device, so switching a port to a multitap and back
// does not clobber the gamepad mapping. Which pad each port reads comes from
// Settings::padIndex.
class InputMap {
public:
  static constexpr int Ports = EmuCore::PortCount;
  static constexpr int Slots = 2;

  InputMap();

  void loadDefaults();
  bool load(const std::string& path);
  bool save(const std::string& path) const;

  Binding& binding(int port, int device, int index, int slot) {
    return bindings[port][device][index][slot];
  }
  const Binding& binding(int port, int device, int index, int slot) const {
    return bindings[port][device][index][slot];
  }

  // frame is the emulated frame count, so turbo timing tracks the emulator's
  // own clock and stays correct under fast forward
  void apply(EmuCore& core, const std::vector<SDL_Gamepad*>& pads,
            const Settings& settings, long long frame) const;

  // pad events from another controller are ignored, so pad 1 cannot bind port 2
  static bool capture(const SDL_Event& event, Binding& out, SDL_JoystickID pad = 0);

private:
  using Slotted = std::array<Binding, Slots>;
  using PerInput = std::array<Slotted, EmuCore::MaxInputs>;
  using PerDevice = std::array<PerInput, EmuCore::DeviceCount>;

  std::array<PerDevice, Ports> bindings{};
};

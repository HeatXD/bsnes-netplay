#pragma once

#include "emucore/emucore.hpp"
#include "settings.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <string>
#include <vector>

struct Binding {
  enum Type : int { None = 0, Key, PadButton, PadAxis, MouseButton };

  Type type = None;
  int code = 0;
  int direction = 0;  // PadAxis only: +1 or -1
  // Key hotkeys only: modifiers that must be held with it, matched exactly
  int mods = 0;

  // the pad names its own face buttons, so nintendo layouts read B/A/Y/X
  std::string label(SDL_Gamepad* pad = nullptr) const;
};

SDL_Gamepad* resolvePad(const std::vector<SDL_Gamepad*>& pads, int index);

// everything a binding can be tested against, gathered once a frame
struct InputSample {
  const bool* keys = nullptr;         // SDL_GetKeyboardState
  uint32_t mouseButtons = 0;          // SDL_GetMouseState mask
  int mods = 0;                       // ctrl, shift, alt and gui, sides collapsed
  int mouseDx = 0, mouseDy = 0;  // relative motion, for the aiming devices
  bool mouseCaptured = false;

  static InputSample poll(bool captured);
};

// the ctrl/shift/alt/gui bits of an SDL modifier mask, left and right merged
int normalizeMods(SDL_Keymod mods);
bool isModifier(SDL_Scancode code);

// whether a binding is held right now
bool bindingActive(const Binding& binding, const InputSample& sample, SDL_Gamepad* pad);

inline int padSlot(int port, int player) { return port * EmuCore::MaxPlayers + player; }

// pads with an unplugged controller are left empty rather than removed
inline int livePadCount(const std::vector<SDL_Gamepad*>& pads) {
  int live = 0;
  for(SDL_Gamepad* pad : pads) live += pad != nullptr;
  return live;
}

// Bindings are kept per device, so switching a port to a multitap and back does
// not clobber the gamepad mapping. Which pad each port reads comes from
// Settings::padIndex.
class InputMap {
public:
  static constexpr int Ports = EmuCore::PortCount;
  // interchangeable; the defaults fill them with a key, a pad button and a stick
  static constexpr int Slots = 3;
  // each input carries a second set of slots that auto-fires while held
  static constexpr int TurboSlot = Slots;
  static constexpr int SlotCount = Slots * 2;
  // a hotkey takes a key and a pad button, so a stick alone can drive the app
  static constexpr int HotkeySlots = 2;

  InputMap();

  void loadDefaults();
  // the two tabs reset independently, so neither wipes the other's page
  void loadButtonDefaults();
  void loadHotkeyDefaults();
  // an aiming device's buttons sit past its axes, so the core has to place them
  void loadPointerDefaults(const EmuCore& core);
  bool load(const std::string& path);
  bool save(const std::string& path) const;

  Binding& binding(int port, int device, int index, int slot) {
    return bindings[port][device][index][slot];
  }
  const Binding& binding(int port, int device, int index, int slot) const {
    return bindings[port][device][index][slot];
  }

  Binding& hotkey(int index, int slot) { return hotkeys[index][slot]; }
  const Binding& hotkey(int index, int slot) const { return hotkeys[index][slot]; }
  // false when the config predates bindings, so its scancodes can be migrated
  bool hasHotkeys() const { return hotkeysLoaded; }
  void migrateHotkeys(const int* scancodes, int count);

  // any mapping fires it, and any pad may be the one pressing it
  bool hotkeyHeld(int index, const InputSample& sample,
                  const std::vector<SDL_Gamepad*>& pads) const;

  // frame is the emulated frame count, so turbo timing tracks the emulator's
  // own clock and stays correct under fast forward
  void apply(EmuCore& core, const std::vector<SDL_Gamepad*>& pads,
            const Settings& settings, const InputSample& sample, long long frame) const;

  // pad events from another controller are ignored, so pad 1 cannot bind port 2
  static bool capture(const SDL_Event& event, Binding& out, SDL_JoystickID pad = 0,
                      bool chords = false);

private:
  using Slotted = std::array<Binding, SlotCount>;
  using PerInput = std::array<Slotted, EmuCore::MaxInputs>;
  using PerDevice = std::array<PerInput, EmuCore::DeviceCount>;

  std::array<PerDevice, Ports> bindings{};
  std::array<std::array<Binding, HotkeySlots>, HotkeyCount> hotkeys{};
  bool hotkeysLoaded = false;
};

#include "inputmap.hpp"

#include "util.hpp"

namespace {
constexpr int AxisThreshold = 16000;

constexpr SDL_Scancode DefaultKeys[EmuCore::ButtonCount] = {
  SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
  SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S,
  SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RETURN
};

// the left stick drives the d-pad directions, as a stick-only pad has no hat
constexpr struct { SDL_GamepadAxis axis; int direction; } DefaultStick[] = {
  {SDL_GAMEPAD_AXIS_LEFTY, -1},  // Up
  {SDL_GAMEPAD_AXIS_LEFTY, +1},  // Down
  {SDL_GAMEPAD_AXIS_LEFTX, -1},  // Left
  {SDL_GAMEPAD_AXIS_LEFTX, +1},  // Right
};

// SNES face layout maps to the xbox diamond rotated: B is south, A is east.
constexpr SDL_GamepadButton DefaultPad[EmuCore::ButtonCount] = {
  SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
  SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
  SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
  SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
  SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
  SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START
};
}

namespace {
// SDL's own names are mapping-file tokens: "dpup", "leftshoulder"
constexpr const char* PadButtonNames[] = {
  "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3", "LB", "RB",
  "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
};
static_assert(SDL_GAMEPAD_BUTTON_DPAD_RIGHT == 14, "SDL gamepad buttons reordered");

constexpr const char* FaceLabels[] = {
  nullptr, "A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle",
};
static_assert(SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE == 8, "SDL face labels reordered");

constexpr const char* MouseButtonNames[] = {
  nullptr, "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse X1", "Mouse X2",
};

constexpr const char* PadAxisNames[] = {
  "Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "LT", "RT",
};
static_assert(SDL_GAMEPAD_AXIS_COUNT == 6, "SDL gamepad axes reordered");

template<size_t N>
const char* lookup(const char* const (&names)[N], int index) {
  return index >= 0 && (size_t)index < N ? names[index] : nullptr;
}

const char* faceLabel(SDL_Gamepad* pad, SDL_GamepadButton button) {
  if(!pad) return nullptr;
  return lookup(FaceLabels, SDL_GetGamepadButtonLabel(pad, button));
}
}  // namespace

Controller* resolvePad(std::vector<Controller>& pads, int index) {
  return index >= 0 && (size_t)index < pads.size() && pads[index] ? &pads[index] : nullptr;
}

const Controller* resolvePad(const std::vector<Controller>& pads, int index) {
  return index >= 0 && (size_t)index < pads.size() && pads[index] ? &pads[index] : nullptr;
}

int normalizeMods(SDL_Keymod mods) {
  int out = 0;
  if(mods & SDL_KMOD_CTRL) out |= SDL_KMOD_LCTRL;
  if(mods & SDL_KMOD_SHIFT) out |= SDL_KMOD_LSHIFT;
  if(mods & SDL_KMOD_ALT) out |= SDL_KMOD_LALT;
  if(mods & SDL_KMOD_GUI) out |= SDL_KMOD_LGUI;
  return out;
}

InputSample InputSample::poll(bool captured) {
  InputSample sample;
  sample.keys = SDL_GetKeyboardState(nullptr);
  sample.mods = normalizeMods(SDL_GetModState());
  float dx = 0.0f, dy = 0.0f;
  // relative state is only meaningful while the pointer is locked to us
  sample.mouseButtons = captured ? SDL_GetRelativeMouseState(&dx, &dy)
                                 : SDL_GetMouseState(nullptr, nullptr);
  sample.mouseDx = (int)dx;
  sample.mouseDy = (int)dy;
  sample.mouseCaptured = captured;
  return sample;
}

bool isModifier(SDL_Scancode code) {
  return code == SDL_SCANCODE_LCTRL || code == SDL_SCANCODE_RCTRL
      || code == SDL_SCANCODE_LSHIFT || code == SDL_SCANCODE_RSHIFT
      || code == SDL_SCANCODE_LALT || code == SDL_SCANCODE_RALT
      || code == SDL_SCANCODE_LGUI || code == SDL_SCANCODE_RGUI;
}

bool bindingActive(const Binding& binding, const InputSample& sample, const Controller* pad) {
  const bool* keys = sample.keys;
  switch(binding.type) {
    case Binding::Key:
      return keys && keys[binding.code];
    case Binding::MouseButton:
      // uncaptured, a click on a menu would reach the game as well
      return sample.mouseCaptured
          && (sample.mouseButtons & SDL_BUTTON_MASK(binding.code)) != 0;
    case Binding::PadButton:
      return pad && pad->gamepad
          && SDL_GetGamepadButton(pad->gamepad, (SDL_GamepadButton)binding.code);
    case Binding::PadAxis: {
      if(!pad || !pad->gamepad) return false;
      const int value = SDL_GetGamepadAxis(pad->gamepad, (SDL_GamepadAxis)binding.code);
      return binding.direction > 0 ? value > AxisThreshold : value < -AxisThreshold;
    }
    case Binding::JoyButton:
      return pad && pad->joystick && SDL_GetJoystickButton(pad->joystick, binding.code);
    case Binding::JoyAxis: {
      if(!pad || !pad->joystick) return false;
      const int value = SDL_GetJoystickAxis(pad->joystick, binding.code);
      return binding.direction > 0 ? value > AxisThreshold : value < -AxisThreshold;
    }
    case Binding::JoyHat:
      return pad && pad->joystick
          && (SDL_GetJoystickHat(pad->joystick, binding.code) & binding.direction) != 0;
    default:
      return false;
  }
}

std::string Binding::label(const Controller* pad) const {
  switch(type) {
    case Key: {
      const char* name = SDL_GetScancodeName((SDL_Scancode)code);
      std::string prefix;
      if(mods & SDL_KMOD_LCTRL) prefix += "Ctrl+";
      if(mods & SDL_KMOD_LSHIFT) prefix += "Shift+";
      if(mods & SDL_KMOD_LALT) prefix += "Alt+";
      if(mods & SDL_KMOD_LGUI) prefix += "Gui+";
      return prefix + ((name && *name) ? name : "Key");
    }
    case MouseButton: {
      if(const char* name = lookup(MouseButtonNames, code)) return name;
      return "Mouse " + std::to_string(code);
    }
    case PadButton: {
      const auto button = (SDL_GamepadButton)code;
      if(const char* face = faceLabel(pad ? pad->gamepad : nullptr, button)) return face;
      if(const char* name = lookup(PadButtonNames, code)) return name;
      const char* raw = SDL_GetGamepadStringForButton(button);
      return raw ? raw : "Pad";
    }
    case PadAxis: {
      const auto axis = (SDL_GamepadAxis)code;
      const char* name = lookup(PadAxisNames, code);
      if(!name) name = SDL_GetGamepadStringForAxis(axis);
      // a trigger only travels one way, so its sign is noise
      const bool trigger = axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                        || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
      return std::string(name ? name : "axis") + (trigger ? "" : direction > 0 ? " +" : " -");
    }
    case JoyButton: return "Button " + std::to_string(code + 1);
    case JoyAxis:
      return "Axis " + std::to_string(code + 1) + (direction > 0 ? " +" : " -");
    case JoyHat: {
      const char* directionName = direction == SDL_HAT_UP ? "Up"
                                : direction == SDL_HAT_DOWN ? "Down"
                                : direction == SDL_HAT_LEFT ? "Left"
                                : direction == SDL_HAT_RIGHT ? "Right" : "Direction";
      return "Hat " + std::to_string(code + 1) + " " + directionName;
    }
    default: return "unbound";
  }
}

InputMap::InputMap() { loadDefaults(); }

void InputMap::loadDefaults() {
  loadButtonDefaults();
  loadHotkeyDefaults();
}

void InputMap::loadHotkeyDefaults() {
  hotkeys = {};
  for(int i = 0; i < HotkeyCount; i++) {
    if(HotkeyDefault(i) == SDL_SCANCODE_UNKNOWN) continue;
    hotkeys[i][0] = {Binding::Key, (int)HotkeyDefault(i), 0};
  }
}

void InputMap::loadButtonDefaults() {
  bindings = {};

  // slot 0 keyboard, slot 1 pad, slot 2 stick; only port 1 gets keyboard keys
  for(int button = 0; button < EmuCore::ButtonCount; button++) {
    bindings[0][EmuCore::Gamepad][button][0] = {Binding::Key, (int)DefaultKeys[button], 0};
    for(int port = 0; port < Ports; port++) {
      bindings[port][EmuCore::Gamepad][button][1] = {Binding::PadButton, (int)DefaultPad[button], 0};
      if(button > EmuCore::Right) continue;  // the stick only drives directions
      const auto& stick = DefaultStick[button];
      bindings[port][EmuCore::Gamepad][button][2] = {Binding::PadAxis, (int)stick.axis, stick.direction};
    }
  }

  // every multitap player gets the same buttons and reads its own pad; only the
  // first inherits the keyboard, which cannot be shared four ways
  for(int player = 0; player < EmuCore::MaxPlayers; player++) {
    for(int button = 0; button < EmuCore::ButtonCount; button++) {
      const int index = player * EmuCore::ButtonCount + button;
      for(int port = 0; port < Ports; port++) {
        bindings[port][EmuCore::SuperMultitap][index][1] =
            bindings[port][EmuCore::Gamepad][button][1];
        bindings[port][EmuCore::SuperMultitap][index][2] =
            bindings[port][EmuCore::Gamepad][button][2];
        if(player > 0) continue;
        bindings[port][EmuCore::SuperMultitap][index][0] =
            bindings[port][EmuCore::Gamepad][button][0];
      }
    }
  }
}

void InputMap::apply(EmuCore& core, const std::vector<Controller>& pads,
                     const Settings& settings, const InputSample& sample,
                     long long frame) const {
  // half-period on, half-period off; at least 2 frames so it never latches on
  const long long period = SDL_max(2, (long long)(core.refreshRate() / SDL_max(1, settings.turboRate) + 0.5));
  const bool turboPhaseOff = (frame % period) >= period / 2;

  for(int port = 0; port < Ports; port++) {
    const int device = core.connectedDevice(port);
    if(device < 0 || device >= EmuCore::DeviceCount) continue;
    const auto& deviceInputs = core.inputs(device);
    const int count = (int)deviceInputs.size();
    const int players = SDL_max(1, core.playersFor(device));
    // a multitap is four controllers in a row, chained justifiers two guns
    const int stride = count / players;
    const bool pointer = core.isPointer(device);

    // once per player, not per input: a multitap would repeat this twelve times
    const Controller* playerPads[EmuCore::MaxPlayers] = {};
    for(int player = 0; player < players && player < EmuCore::MaxPlayers; player++) {
      playerPads[player] = resolvePad(pads, settings.padIndex[padSlot(port, player)]);
    }

    for(int button = 0; button < count; button++) {
      const int player = stride > 0 ? SDL_min(button / stride, players - 1) : 0;
      const Controller* pad = playerPads[SDL_min(player, EmuCore::MaxPlayers - 1)];

      // uncaptured, the cursor would drift with every stray desktop movement
      if(deviceInputs[button].type == EmuCore::Axis) {
        int16_t motion = 0;
        if(pointer && sample.mouseCaptured && player == 0) {
          motion = (int16_t)(button % stride == 0 ? sample.mouseDx : sample.mouseDy);
        }
        core.setInput(port, button, motion);
        continue;
      }

      bool pressed = false, turbo = false;
      for(int slot = 0; slot < Slots; slot++) {
        const Slotted& slots = bindings[port][device][button];
        pressed = pressed || bindingActive(slots[slot], sample, pad);
        turbo = turbo || bindingActive(slots[TurboSlot + slot], sample, pad);
      }

      // a held turbo bind replaces the plain one rather than adding to it
      if(turbo) pressed = !turboPhaseOff;
      core.setInput(port, button, pressed ? 1 : 0);
    }
  }
}

// the buttons sit past the axes, in the core's order: trigger first
void InputMap::loadPointerDefaults(const EmuCore& core) {
  constexpr int Buttons[] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

  for(int device = 0; device < EmuCore::DeviceCount; device++) {
    if(!core.isPointer(device)) continue;
    const auto& inputs = core.inputs(device);
    const int stride = core.inputStride(device);

    // there is one mouse, so a chained second gun is left for the user to bind
    int taken = 0;
    for(int index = 0; index < stride && taken < 3; index++) {
      if(inputs[index].type == EmuCore::Axis) continue;  // the pointer drives these
      for(int port = 0; port < Ports; port++) {
        bindings[port][device][index][0] = {Binding::MouseButton, Buttons[taken], 0};
      }
      taken++;
    }
  }
}

bool InputMap::hotkeyHeld(int index, const InputSample& sample,
                          const std::vector<Controller>& pads) const {
  if(index < 0 || index >= HotkeyCount) return false;

  for(int slot = 0; slot < HotkeySlots; slot++) {
    const Binding& binding = hotkeys[index][slot];
    if(binding.type == Binding::None) continue;

    // a hotkey belongs to the app, so any pad may press it
    bool held = bindingActive(binding, sample, nullptr);
    for(const Controller& pad : pads) held = held || bindingActive(binding, sample, &pad);
    // a chord is exact, so a bare key does not fire while a modifier is down
    if(binding.type == Binding::Key && sample.mods != binding.mods) held = false;
    if(held) return true;
  }
  return false;
}

// hotkeys added since that config was written keep their defaults
void InputMap::migrateHotkeys(const int* scancodes, int count) {
  for(int i = 0; i < count && i < HotkeyCount; i++) {
    hotkeys[i] = {};
    if(scancodes[i] > 0) hotkeys[i][0] = {Binding::Key, scancodes[i], 0};
  }
  hotkeysLoaded = true;
}

bool InputMap::capture(const SDL_Event& event, Binding& out, const Controller* pad, bool chords) {
  if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if(event.key.scancode == SDL_SCANCODE_ESCAPE) return false;
    // delete clears the binding rather than binding the delete key itself
    if(event.key.scancode == SDL_SCANCODE_DELETE
    || event.key.scancode == SDL_SCANCODE_BACKSPACE) {
      out = {};
      return true;
    }
    // a bare modifier would end the capture before the key it belongs to arrives
    if(chords && isModifier(event.key.scancode)) return false;
    out = {Binding::Key, (int)event.key.scancode, 0,
           chords ? normalizeMods(event.key.mod) : 0};
    return true;
  }
  if(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    out = {Binding::MouseButton, (int)event.button.button, 0};
    return true;
  }
  if(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    if(pad && event.gbutton.which != pad->id()) return false;
    out = {Binding::PadButton, (int)event.gbutton.button, 0};
    return true;
  }
  if(event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
    if(pad && event.gaxis.which != pad->id()) return false;
    if(SDL_abs(event.gaxis.value) < AxisThreshold) return false;
    out = {Binding::PadAxis, (int)event.gaxis.axis, event.gaxis.value > 0 ? 1 : -1};
    return true;
  }
  // A mapped gamepad also produces raw joystick events. Ignore those duplicates
  // so its portable standardized binding wins; unknown devices use them.
  if(event.type == SDL_EVENT_JOYSTICK_BUTTON_DOWN) {
    if((pad && (pad->mapped() || event.jbutton.which != pad->id()))
    || (!pad && SDL_IsGamepad(event.jbutton.which))) return false;
    out = {Binding::JoyButton, (int)event.jbutton.button, 0};
    return true;
  }
  if(event.type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
    if((pad && (pad->mapped() || event.jaxis.which != pad->id()))
    || (!pad && SDL_IsGamepad(event.jaxis.which))) return false;
    if(SDL_abs(event.jaxis.value) < AxisThreshold) return false;
    out = {Binding::JoyAxis, (int)event.jaxis.axis, event.jaxis.value > 0 ? 1 : -1};
    return true;
  }
  if(event.type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
    if((pad && (pad->mapped() || event.jhat.which != pad->id()))
    || (!pad && SDL_IsGamepad(event.jhat.which)) || event.jhat.value == SDL_HAT_CENTERED) {
      return false;
    }
    // Diagonals contain two bits. Capturing either cardinal component makes the
    // resulting binding useful when the hat later reports that diagonal too.
    const int direction = event.jhat.value & SDL_HAT_UP ? SDL_HAT_UP
                        : event.jhat.value & SDL_HAT_DOWN ? SDL_HAT_DOWN
                        : event.jhat.value & SDL_HAT_LEFT ? SDL_HAT_LEFT : SDL_HAT_RIGHT;
    out = {Binding::JoyHat, (int)event.jhat.hat, direction};
    return true;
  }
  return false;
}

bool InputMap::save(const std::string& path) const {
  std::string text;

  // keyed by index rather than name, because input names vary per device
  for(int port = 0; port < Ports; port++) {
    for(int device = 0; device < EmuCore::DeviceCount; device++) {
      for(int index = 0; index < EmuCore::MaxInputs; index++) {
        for(int slot = 0; slot < SlotCount; slot++) {
          const Binding& b = bindings[port][device][index][slot];
          if(b.type == Binding::None) continue;
          char line[128];
          int n = SDL_snprintf(line, sizeof(line), "%d %d %d %d %d %d %d\n",
                               port, device, index, slot, (int)b.type, b.code, b.direction);
          text.append(line, n);
        }
      }
    }
  }
  for(int index = 0; index < HotkeyCount; index++) {
    for(int slot = 0; slot < HotkeySlots; slot++) {
      const Binding& b = hotkeys[index][slot];
      if(b.type == Binding::None) continue;
      char line[128];
      int n = SDL_snprintf(line, sizeof(line), "h %d %d %d %d %d %d\n",
                           index, slot, (int)b.type, b.code, b.direction, b.mods);
      text.append(line, n);
    }
  }
  return writeText(path, text);
}

bool InputMap::load(const std::string& path) {
  const std::string text = readText(path);
  if(text.empty()) return false;

  size_t pos = 0;
  while(pos < text.size()) {
    size_t end = text.find('\n', pos);
    if(end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;

    if(line[0] == 'h') {
      int index = 0, slot = 0, type = 0, code = 0, dir = 0, mods = 0;
      // a config written before chords carries five fields, not six
      if(SDL_sscanf(line.c_str() + 1, "%d %d %d %d %d %d",
                    &index, &slot, &type, &code, &dir, &mods) < 5) continue;
      if(index < 0 || index >= HotkeyCount) continue;
      if(slot < 0 || slot >= HotkeySlots) continue;
      // the first line clears the defaults, so an unbound hotkey stays unbound
      if(!hotkeysLoaded) { hotkeys = {}; hotkeysLoaded = true; }
      hotkeys[index][slot] = {(Binding::Type)type, code, dir, mods};
      continue;
    }

    int port = 0, device = 0, index = 0, slot = 0, type = 0, code = 0, dir = 0;
    if(SDL_sscanf(line.c_str(), "%d %d %d %d %d %d %d",
                  &port, &device, &index, &slot, &type, &code, &dir) != 7) continue;
    if(port < 0 || port >= Ports) continue;
    if(device < 0 || device >= EmuCore::DeviceCount) continue;
    if(index < 0 || index >= EmuCore::MaxInputs) continue;
    if(slot < 0 || slot >= SlotCount) continue;

    bindings[port][device][index][slot] = {(Binding::Type)type, code, dir};
  }
  return true;
}

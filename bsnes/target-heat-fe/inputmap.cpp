#include "inputmap.hpp"

#include "util.hpp"

namespace {
constexpr int AxisThreshold = 16000;

constexpr SDL_Scancode DefaultKeys[EmuCore::ButtonCount] = {
  SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
  SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_A, SDL_SCANCODE_S,
  SDL_SCANCODE_Q, SDL_SCANCODE_W, SDL_SCANCODE_RSHIFT, SDL_SCANCODE_RETURN
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

SDL_Gamepad* resolvePad(const std::vector<SDL_Gamepad*>& pads, int index) {
  return index >= 0 && (size_t)index < pads.size() ? pads[index] : nullptr;
}

std::string Binding::label(SDL_Gamepad* pad) const {
  switch(type) {
    case Key: {
      const char* name = SDL_GetScancodeName((SDL_Scancode)code);
      return (name && *name) ? name : "Key";
    }
    case PadButton: {
      const auto button = (SDL_GamepadButton)code;
      if(const char* face = faceLabel(pad, button)) return face;
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
    default: return "unbound";
  }
}

InputMap::InputMap() { loadDefaults(); }

void InputMap::loadDefaults() {
  bindings = {};

  // slot 0 keyboard, slot 1 pad; only port 1 gets keyboard keys by default
  for(int button = 0; button < EmuCore::ButtonCount; button++) {
    bindings[0][EmuCore::Gamepad][button][0] = {Binding::Key, (int)DefaultKeys[button], 0};
    for(int port = 0; port < Ports; port++) {
      bindings[port][EmuCore::Gamepad][button][1] = {Binding::PadButton, (int)DefaultPad[button], 0};
    }
  }

  // a multitap's first pad is the same controller, so mirror the port defaults
  for(int button = 0; button < EmuCore::ButtonCount; button++) {
    for(int port = 0; port < Ports; port++) {
      for(int slot = 0; slot < Slots; slot++) {
        bindings[port][EmuCore::SuperMultitap][button][slot] =
            bindings[port][EmuCore::Gamepad][button][slot];
      }
    }
  }
}

void InputMap::apply(EmuCore& core, const std::vector<SDL_Gamepad*>& pads,
                     const Settings& settings, long long frame) const {
  const bool* keys = SDL_GetKeyboardState(nullptr);

  // half-period on, half-period off; at least 2 frames so it never latches on
  const long long period = SDL_max(2, (long long)(core.refreshRate() / SDL_max(1, settings.turboRate) + 0.5));
  const bool turboPhaseOff = (frame % period) >= period / 2;

  for(int port = 0; port < Ports; port++) {
    SDL_Gamepad* pad = resolvePad(pads, settings.padIndex[port]);
    const int device = core.connectedDevice(port);
    if(device < 0 || device >= EmuCore::DeviceCount) continue;
    const int count = (int)core.inputs(device).size();

    for(int button = 0; button < count; button++) {
      bool pressed = false;

      for(int slot = 0; slot < Slots && !pressed; slot++) {
        const Binding& b = bindings[port][device][button][slot];
        switch(b.type) {
          case Binding::Key:
            pressed = keys[b.code];
            break;
          case Binding::PadButton:
            if(pad) pressed = SDL_GetGamepadButton(pad, (SDL_GamepadButton)b.code);
            break;
          case Binding::PadAxis:
            if(pad) {
              int value = SDL_GetGamepadAxis(pad, (SDL_GamepadAxis)b.code);
              pressed = b.direction > 0 ? value > AxisThreshold : value < -AxisThreshold;
            }
            break;
          default:
            break;
        }
      }

      if(pressed && device == EmuCore::Gamepad && button < EmuCore::ButtonCount
      && (settings.turboMask[port] & (1 << button)) && turboPhaseOff) {
        pressed = false;
      }
      core.setInput(port, button, pressed ? 1 : 0);
    }
  }
}

bool InputMap::capture(const SDL_Event& event, Binding& out, SDL_JoystickID pad) {
  if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
    if(event.key.scancode == SDL_SCANCODE_ESCAPE) return false;
    out = {Binding::Key, (int)event.key.scancode, 0};
    return true;
  }
  if(event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) {
    if(pad && event.gbutton.which != pad) return false;
    out = {Binding::PadButton, (int)event.gbutton.button, 0};
    return true;
  }
  if(event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
    if(pad && event.gaxis.which != pad) return false;
    if(SDL_abs(event.gaxis.value) < AxisThreshold) return false;
    out = {Binding::PadAxis, (int)event.gaxis.axis, event.gaxis.value > 0 ? 1 : -1};
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
        for(int slot = 0; slot < Slots; slot++) {
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

    int port = 0, device = 0, index = 0, slot = 0, type = 0, code = 0, dir = 0;
    if(SDL_sscanf(line.c_str(), "%d %d %d %d %d %d %d",
                  &port, &device, &index, &slot, &type, &code, &dir) != 7) continue;
    if(port < 0 || port >= Ports) continue;
    if(device < 0 || device >= EmuCore::DeviceCount) continue;
    if(index < 0 || index >= EmuCore::MaxInputs) continue;
    if(slot < 0 || slot >= Slots) continue;

    bindings[port][device][index][slot] = {(Binding::Type)type, code, dir};
  }
  return true;
}

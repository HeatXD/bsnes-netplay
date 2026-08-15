#include "ui.hpp"

namespace {
// only the face and shoulder buttons make sense to auto-fire
bool turboEligible(int button) {
  return button >= EmuCore::B && button <= EmuCore::R;
}
}  // namespace

void App::drawBindingTable(int device) {
  const auto& deviceInputs = core.inputs(device);
  const bool turboCapable = device == EmuCore::Gamepad;
  SDL_Gamepad* pad = portPad(mapPort, mapPlayer);
  const bool* keys = SDL_GetKeyboardState(nullptr);

  // a multitap's inputs are four controllers in a row; show one at a time
  const int players = playerCount((int)deviceInputs.size());
  const int first = players > 1 ? mapPlayer * EmuCore::ButtonCount : 0;
  const int last = players > 1 ? first + EmuCore::ButtonCount : (int)deviceInputs.size();

  // content sizing would let the long pad labels swallow the row
  if(!ImGui::BeginTable("bindings", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) return;

  ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Keyboard");
  ImGui::TableSetupColumn(pad ? SDL_GetGamepadName(pad) : "Controller (none)");
  ImGui::TableSetupColumn("Turbo", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableHeadersRow();

  for(int b = first; b < last; b++) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    // multitap inputs are named "Port 3 - Up"; the player picker already says which
    const std::string& name = deviceInputs[b].name;
    const size_t dash = name.rfind(" - ");
    ImGui::TextUnformatted(dash == std::string::npos ? name.c_str() : name.c_str() + dash + 3);

    // pointer and analog inputs have no key or button to bind
    const bool analog = deviceInputs[b].type == EmuCore::Axis
                     || deviceInputs[b].type == EmuCore::Rumble;

    for(int slot = 0; slot < InputMap::Slots; slot++) {
      ImGui::TableNextColumn();
      if(analog) { ImGui::TextDisabled("n/a"); continue; }

      const int id = b * InputMap::Slots + slot;
      const Binding& binding = input.binding(mapPort, device, b, slot);
      ImGui::PushID(id);

      // light the binding while it is held, so a press is visible even when
      // the pad turns out to be the wrong one
      const bool held = capturing != id && bindingActive(binding, keys, pad);
      if(held) ImGui::PushStyleColor(ImGuiCol_Button, accentColor());

      const std::string label = capturing == id ? "..." : binding.label(pad);
      if(ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) capturing = id;

      if(held) ImGui::PopStyleColor();
      ImGui::PopID();
    }

    ImGui::TableNextColumn();
    if(turboCapable && turboEligible(b)) {
      bool on = (settings.turboMask[mapPort] & (1 << b)) != 0;
      ImGui::PushID(1000 + b);
      if(ImGui::Checkbox("##turbo", &on)) {
        if(on) settings.turboMask[mapPort] |= (1 << b);
        else settings.turboMask[mapPort] &= ~(1 << b);
        settings.save(settingsCfg);
      }
      ImGui::PopID();
    } else {
      ImGui::TextDisabled("-");
    }
  }
  ImGui::EndTable();
}

void App::drawDevicePicker() {
  const auto& portDevices = core.devices(mapPort);
  int deviceIndex = 0;
  for(int i = 0; i < (int)portDevices.size(); i++) {
    if(portDevices[i].id == core.connectedDevice(mapPort)) deviceIndex = i;
  }
  auto deviceName = [](void* data, int index) {
    return (*(const std::vector<EmuCore::DeviceInfo>*)data)[index].name.c_str();
  };

  if(ImGui::Combo("Device", &deviceIndex, deviceName, (void*)&portDevices,
                  (int)portDevices.size())
  && deviceIndex < (int)portDevices.size()) {
    core.connect(mapPort, portDevices[deviceIndex].id);
    settings.devices[mapPort] = portDevices[deviceIndex].id;
    settings.save(settingsCfg);
    capturing = -1;
    mapPlayer = 0;
  }
}

void App::drawControllerPicker() {
  // entry 0 is None, so the combo index is the pad index shifted by one
  auto padName = [](void* data, int index) -> const char* {
    if(index == 0) return "None";
    const auto& pads = *(const std::vector<SDL_Gamepad*>*)data;
    SDL_Gamepad* pad = pads[index - 1];
    const char* name = pad ? SDL_GetGamepadName(pad) : nullptr;
    static char label[128];
    SDL_snprintf(label, sizeof(label), "%d: %s", index,
                 pad ? (name ? name : "unnamed") : "(disconnected)");
    return label;
  };

  const int slot = padSlot(mapPort, mapPlayer);
  int current = settings.padIndex[slot] + 1;
  if(current < 0 || current > (int)pads.size()) current = 0;
  if(ImGui::Combo("Controller", &current, padName, (void*)&pads, (int)pads.size() + 1)) {
    settings.padIndex[slot] = current - 1;
    settings.save(settingsCfg);
    capturing = -1;
  }
}

void App::restoreInputDefaults() {
  const Settings defaults;
  input.loadDefaults();
  settings.turboRate = defaults.turboRate;
  for(int port = 0; port < EmuCore::PortCount; port++) {
    settings.turboMask[port] = defaults.turboMask[port];
    settings.devices[port] = defaults.devices[port];
    core.connect(port, settings.devices[port]);
  }
  for(int i = 0; i < EmuCore::PortCount * EmuCore::MaxPlayers; i++) {
    settings.padIndex[i] = defaults.padIndex[i];
  }
  capturing = -1;
  mapPlayer = 0;
}

void App::drawInputTab() {
  ImGui::Combo("Port", &mapPort, "Port 1\0Port 2\0");
  drawDevicePicker();

  // a multitap carries four controllers, each with its own pad and bindings
  const int players = portPlayers(mapPort);
  if(mapPlayer >= players) mapPlayer = 0;
  if(players > 1) {
    auto playerName = [](void*, int index) -> const char* {
      static char label[24];
      SDL_snprintf(label, sizeof(label), "Player %d", index + 1);
      return label;
    };
    ImGui::Combo("Multitap player", &mapPlayer, playerName, nullptr, players);
  }
  drawControllerPicker();

  ImGui::TextUnformatted(capturing >= 0 ? "press a key or pad button, esc to cancel"
                                        : "click a binding to rebind it");
  ImGui::Separator();
  drawBindingTable(core.connectedDevice(mapPort));

  ImGui::Separator();
  ImGui::SliderInt("Turbo rate (Hz)", &settings.turboRate, 1, 30);
  if(ImGui::IsItemDeactivatedAfterEdit()) settings.save(settingsCfg);

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##input")) {
    restoreInputDefaults();
    input.save(inputCfg);
    settings.save(settingsCfg);
  }
  ImGui::SameLine();
  int connected = 0;
  for(SDL_Gamepad* pad : pads) connected += pad != nullptr;
  ImGui::Text("%d gamepad(s)", connected);
}

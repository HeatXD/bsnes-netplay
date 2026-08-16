#include "ui.hpp"


// the core names a multitap's inputs "Port 3 - Up": controller, then button
std::string App::playerLabel(int device, int player) const {
  const std::string& name = core.inputs(device)[player * EmuCore::ButtonCount].name;
  const size_t dash = name.rfind(" - ");
  return dash == std::string::npos ? name : name.substr(0, dash);
}

// one row of bind buttons: the plain set, or the turbo set that auto-fires
void App::drawBindingRow(int device, int b, bool turbo) {
  SDL_Gamepad* pad = portPad(mapPort, mapPlayer);
  const bool* keys = SDL_GetKeyboardState(nullptr);

  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  const std::string& name = core.inputs(device)[b].name;
  const size_t dash = name.rfind(" - ");
  const std::string label = (dash == std::string::npos ? name : name.substr(dash + 3))
                          + (turbo ? " turbo" : "");
  ImGui::TextUnformatted(label.c_str());

  for(int slot = 0; slot < InputMap::Slots; slot++) {
    ImGui::TableNextColumn();
    const int id = b * InputMap::SlotCount + (turbo ? InputMap::TurboSlot : 0) + slot;
    const Binding& binding = input.binding(mapPort, device, b, id % InputMap::SlotCount);
    ImGui::PushID(id);

    // held bindings light up, so a press shows even on the wrong pad
    const bool held = capturing != id && bindingActive(binding, keys, pad);
    if(held) ImGui::PushStyleColor(ImGuiCol_Button, accentColor());

    const std::string text = capturing == id ? "..." : binding.label(pad);
    if(ImGui::Button(text.c_str(), ImVec2(-1.0f, 0.0f))) capturing = id;

    if(held) ImGui::PopStyleColor();
    ImGui::PopID();
  }
}

void App::drawBindingTable(int device) {
  const auto& deviceInputs = core.inputs(device);

  // a multitap's inputs are four controllers in a row; show one at a time
  const int players = core.playersFor(device);
  const int first = mapPlayer * EmuCore::ButtonCount;
  const int last = players > 1 ? first + EmuCore::ButtonCount : (int)deviceInputs.size();

  // content sizing would let the long pad labels swallow the row
  if(!ImGui::BeginTable("bindings", 1 + InputMap::Slots,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) return;

  ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed);
  // every slot takes a key or a pad button alike, so they are numbered, not typed
  for(int slot = 0; slot < InputMap::Slots; slot++) {
    char label[16];
    SDL_snprintf(label, sizeof(label), "Mapping #%d", slot + 1);
    ImGui::TableSetupColumn(label);
  }
  ImGui::TableHeadersRow();

  auto bindable = [&](int b) {
    return deviceInputs[b].type != EmuCore::Axis && deviceInputs[b].type != EmuCore::Rumble;
  };

  for(int b = first; b < last; b++) {
    if(bindable(b)) { drawBindingRow(device, b, false); continue; }

    // pointer and analog inputs have no key or button to bind
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(deviceInputs[b].name.c_str());
    for(int slot = 0; slot < InputMap::Slots; slot++) {
      ImGui::TableNextColumn();
      ImGui::TextDisabled("n/a");
    }
  }

  // the turbo sets sit together below rather than doubling up every button
  for(int b = first; b < last; b++) {
    if(bindable(b)) drawBindingRow(device, b, true);
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

  // the core names a multitap's four controllers Port 2..Port 5
  const int device = core.connectedDevice(mapPort);
  const int players = core.playersFor(device);
  if(mapPlayer >= players) mapPlayer = 0;
  if(players > 1) {
    if(ImGui::BeginCombo("Multitap slot", playerLabel(device, mapPlayer).c_str())) {
      for(int player = 0; player < players; player++) {
        if(ImGui::Selectable(playerLabel(device, player).c_str(), player == mapPlayer)) {
          mapPlayer = player;
        }
      }
      ImGui::EndCombo();
    }
  }
  drawControllerPicker();

  ImGui::TextUnformatted(capturing >= 0
      ? "press a key or pad button, delete to unbind, esc to cancel"
      : "click a binding to rebind it");
  ImGui::Separator();
  drawBindingTable(device);

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
  ImGui::Text("%d gamepad(s)", livePadCount(pads));
}

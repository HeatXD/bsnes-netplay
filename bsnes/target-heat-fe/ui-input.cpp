#include "ui.hpp"

void App::drawBindingTable(int device) {
  const auto& deviceInputs = core.inputs(device);

  // content sizing would let the long pad labels swallow the row
  if(!ImGui::BeginTable("bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) return;

  ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("Keyboard");
  ImGui::TableSetupColumn("Gamepad");
  ImGui::TableHeadersRow();

  for(int b = 0; b < (int)deviceInputs.size(); b++) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(deviceInputs[b].name.c_str());

    // pointer and analog inputs have no key or button to bind
    const bool analog = deviceInputs[b].type == EmuCore::Axis
                     || deviceInputs[b].type == EmuCore::Rumble;

    for(int slot = 0; slot < InputMap::Slots; slot++) {
      ImGui::TableNextColumn();
      if(analog) { ImGui::TextDisabled("n/a"); continue; }

      const int id = b * InputMap::Slots + slot;
      ImGui::PushID(id);
      const std::string label = capturing == id
          ? "..." : input.binding(mapPort, device, b, slot).label();
      if(ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) capturing = id;
      ImGui::PopID();
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
  }
}

void App::drawInputTab() {
  ImGui::Combo("Port", &mapPort, "Port 1 Port 2 ");
  drawDevicePicker();

  ImGui::TextUnformatted(capturing >= 0 ? "press a key or pad button, esc to cancel"
                                        : "click a binding to rebind it");
  ImGui::Separator();
  drawBindingTable(core.connectedDevice(mapPort));

  ImGui::Separator();
  if(ImGui::Button("Restore defaults")) {
    input.loadDefaults();
    input.save(inputCfg);
  }
  ImGui::SameLine();
  ImGui::Text("%d gamepad(s)", (int)pads.size());
}

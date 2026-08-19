#include "ui.hpp"

void App::restoreHotkeyDefaults() {
  input.loadHotkeyDefaults();
  capturingHotkey = -1;
}

void App::drawHotkeysTab() {
  ImGui::TextUnformatted(capturingHotkey >= 0
      ? "press a key, mouse or pad button, delete to unbind, esc to cancel"
      : "click a mapping to rebind it; hold ctrl, shift or alt for a chord");
  ImGui::Separator();

  if(ImGui::BeginTable("hotkeys", 1 + InputMap::HotkeySlots,
                       ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
    for(int slot = 0; slot < InputMap::HotkeySlots; slot++) {
      char label[16];
      SDL_snprintf(label, sizeof(label), "Mapping #%d", slot + 1);
      ImGui::TableSetupColumn(label);
    }
    ImGui::TableHeadersRow();

    for(int i = 0; i < HotkeyCount; i++) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(HotkeyName(i));

      for(int slot = 0; slot < InputMap::HotkeySlots; slot++) {
        ImGui::TableNextColumn();
        const int id = i * InputMap::HotkeySlots + slot;
        ImGui::PushID(1000 + id);
        const Binding& binding = input.hotkey(i, slot);
        const std::string label = capturingHotkey == id ? "..." : binding.label();
        if(ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) capturingHotkey = id;
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##hotkeys")) {
    restoreHotkeyDefaults();
    input.save(inputCfg);
  }
}

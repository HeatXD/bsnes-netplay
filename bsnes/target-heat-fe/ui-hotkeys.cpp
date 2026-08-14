#include "ui.hpp"

void App::drawHotkeysTab() {
  ImGui::TextUnformatted(capturingHotkey >= 0 ? "press a key, esc to cancel"
                                              : "click a hotkey to rebind it");
  ImGui::Separator();

  if(ImGui::BeginTable("hotkeys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Key");
    ImGui::TableHeadersRow();

    for(int i = 0; i < HotkeyCount; i++) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(HotkeyNames[i]);
      ImGui::TableNextColumn();
      ImGui::PushID(1000 + i);
      const char* name = SDL_GetScancodeName((SDL_Scancode)settings.hotkeys[i]);
      const char* label = capturingHotkey == i ? "..." : (name && *name ? name : "unbound");
      if(ImGui::Button(label, ImVec2(-1.0f, 0.0f))) capturingHotkey = i;
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

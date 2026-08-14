#include "ui.hpp"

namespace {
void drawField(const char* label, const std::string& value) {
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine(140.0f);
  ImGui::TextUnformatted(value.c_str());
}
}

void App::drawCartridgeWindow() {
  if(!showCartridge) return;

  placeFloating(80.0f, 60.0f, 460.0f, 400.0f);
  if(ImGui::Begin("Cartridge", &showCartridge)) {
    if(!core.loaded()) {
      ImGui::TextWrapped("No game loaded.");
    } else {
      drawField("Title", core.headerTitle());
      drawField("Region", core.region());
      drawField("Board", core.board());
      drawField("ROM size", core.romSizeText());
      drawField("RAM size", core.ramSizeText());
      drawField("Expansion chip", core.expansionChip());
      drawField("SHA-256", core.checksum());

      const std::string hotfix = core.activeHotfix();
      if(!hotfix.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Hotfix: %s", hotfix.c_str());
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextDisabled("Manifest");
      if(ImGui::BeginChild("manifest", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        const std::string manifest = core.manifest();
        ImGui::InputTextMultiline("##manifesttext", const_cast<char*>(manifest.c_str()),
          manifest.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
      }
      ImGui::EndChild();
    }
  }
  ImGui::End();
}

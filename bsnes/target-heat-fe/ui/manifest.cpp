#include "ui.hpp"

namespace {
void drawField(const char* label, const std::string& value) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::TextDisabled("%s", label);
  ImGui::TableSetColumnIndex(1);
  ImGui::TextWrapped("%s", value.c_str());
}
}

void App::drawManifestWindow() {
  if(!showManifest) return;

  placeFloating(520.0f, 460.0f);
  if(ImGui::Begin("Manifest", &showManifest)) {
    if(!core.loaded()) {
      ImGui::TextWrapped("No game loaded.");
      ImGui::End();
      return;
    }

    if(ImGui::BeginTable("##summary", 2, ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 125.0f);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
      drawField("Title", core.headerTitle());
      drawField("Region", core.region());
      drawField("Board", core.board());
      drawField("ROM size", core.romSizeText());
      drawField("RAM size", core.ramSizeText());
      drawField("Expansion chip", core.expansionChip());
      drawField("SHA-256", core.checksum());
      drawField("Verified", core.verified()
        ? "yes, this dump is in the games database"
        : "no, the board layout is guessed from the ROM");
      if(core.patched()) drawField("Patch", "applied on load");
      for(const auto& slot : core.slots()) drawField(slot.label.c_str(), slot.game);
      ImGui::EndTable();
    }

    const std::string hotfix = core.activeHotfix();
    if(!hotfix.empty()) {
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Hotfix: %s", hotfix.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    // one tab per medium in the machine, base cartridge first
    if(ImGui::BeginTabBar("manifests")) {
      for(const auto& entry : core.manifestList()) {
        if(!ImGui::BeginTabItem(entry.label.c_str())) continue;
        ImGui::InputTextMultiline("##manifesttext", const_cast<char*>(entry.text.c_str()),
          entry.text.size() + 1, ImVec2(-1, -1), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
  }
  ImGui::End();
}

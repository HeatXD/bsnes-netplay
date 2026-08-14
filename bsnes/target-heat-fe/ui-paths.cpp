#include "ui.hpp"

void App::drawPathsTab() {
  ImGui::TextUnformatted("Games folder");
  ImGui::TextWrapped("%s", settings.gamesDir.empty() ? "(not set)" : settings.gamesDir.c_str());
  if(ImGui::Button("Browse##games")) openFolderDialog(dirPick);
  ImGui::SameLine();
  if(ImGui::Button("Rescan")) scanGames();
  ImGui::SameLine();
  ImGui::Text("%d games", (int)games.size());

  ImGui::Separator();
  ImGui::TextUnformatted("Screenshots folder");
  ImGui::TextWrapped("%s", settings.shotsDir.empty() ? "(config folder)" : settings.shotsDir.c_str());
  if(ImGui::Button("Browse##shots")) openFolderDialog(shotDirPick);

  ImGui::Separator();
  ImGui::TextUnformatted("Saves folder");
  ImGui::TextWrapped("%s", settings.savesDir.empty() ? "(next to the ROM)" : settings.savesDir.c_str());
  if(ImGui::Button("Browse##saves")) openFolderDialog(savesDirPick);
  ImGui::SameLine();
  if(!settings.savesDir.empty() && ImGui::Button("Reset##saves")) {
    settings.savesDir.clear();
    core.setSavesDirectory(settings.savesDir);
    settings.save(settingsCfg);
  }

  ImGui::Separator();
  ImGui::TextWrapped("Config: %s", settingsCfg.c_str());
  const bool portable = portableMode();
  ImGui::TextUnformatted(portable ? "Portable: settings live next to the exe"
                                  : "Portable: off, using the user profile");
  if(!portable && ImGui::Button("Make portable")) {
    if(const char* base = SDL_GetBasePath()) {
      settings.save(std::string(base) + "settings.cfg");
      input.save(std::string(base) + "input.cfg");
      showMessage("portable config written, restart to use it");
    }
  }
}

#include "ui.hpp"

// every path defaults to empty, so the games folder goes too
void App::restorePathDefaults() {
  const Settings defaults;
  settings.gamesDir = defaults.gamesDir;
  settings.shotsDir = defaults.shotsDir;
  settings.savesDir = defaults.savesDir;
  settings.firmwareDir = defaults.firmwareDir;
  settings.sgbBios = defaults.sgbBios;
  settings.bsxBios = defaults.bsxBios;
  settings.stBios = defaults.stBios;
  core.setSavesDirectory(settings.savesDir);
  core.setFirmwareDirectory(firmwareDir());
  scanGames();
  gameSelected = 0;
}

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
  ImGui::TextUnformatted("Firmware folder");
  ImGui::TextWrapped("%s", firmwareDir().c_str());
  ImGui::TextWrapped("Holds dsp1b.program.rom and the like, for carts whose dump"
                     " leaves the coprocessor firmware out.");
  if(ImGui::Button("Browse##firmware")) openFolderDialog(firmwareDirPick);
  ImGui::SameLine();
  if(!settings.firmwareDir.empty() && ImGui::Button("Reset##firmware")) {
    settings.firmwareDir.clear();
    core.setFirmwareDirectory(firmwareDir());
    settings.save(settingsCfg);
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Base cartridges");
  ImGui::TextWrapped("Game Boy, BS-X and Sufami Turbo games run inside one of these.");
  for(const BiosSlot& slot : BiosSlots) {
    std::string& path = settings.*slot.path;
    ImGui::Text("%s: %s", slot.label, path.empty() ? "(not set)" : fileName(path).c_str());
    ImGui::PushID(slot.id);
    if(ImGui::Button("Browse")) openMediaDialog(this->*slot.pick, "SNES ROMs", romFilterPattern());
    if(!path.empty()) {
      ImGui::SameLine();
      if(ImGui::Button("Clear")) {
        path.clear();
        settings.save(settingsCfg);
      }
    }
    ImGui::PopID();
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

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##paths")) {
    restorePathDefaults();
    settings.save(settingsCfg);
  }
}

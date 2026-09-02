#include "ui.hpp"

// the rows sharing one shape: value, Browse, and a Reset that re-pushes it
namespace {
struct FolderRow {
  const char* label;
  const char* unset;  // shown when the path is empty, unless one is resolved for it
  const char* help;
  std::string Settings::* path;
  std::string (App::*resolved)() const;  // shown when the setting is empty
  FilePick App::* pick;
  void (App::*push)();  // null when nothing downstream caches the path
};

const FolderRow FolderRows[] = {
    {"Saves folder", "(next to the ROM)", nullptr, &Settings::savesDir, nullptr, &App::savesDirPick,
     &App::pushSavesDir},
    {"Patches folder", "(beside the ROM)",
     "A .bps or .ips named after the game is applied as it loads.", &Settings::patchesDir, nullptr,
     &App::patchesDirPick, &App::pushPatchesDir},
    {"States folder", nullptr, "Quick, managed, and auto-resume states, per game.",
     &Settings::statesDir, &App::statesDir, &App::statesDirPick, nullptr},
    {"Cheats folder", "(beside the ROM)", "One .cht file per game.", &Settings::cheatsDir, nullptr,
     &App::cheatsDirPick, nullptr},
    {"Games database folder", nullptr, "A game found here is verified and uses its board layout.",
     &Settings::databaseDir, &App::databaseDirShown, &App::databaseDirPick,
     &App::refreshDatabaseDir},
    {"Shaders folder", nullptr, "Holds the .shader packages the Video tab lists.",
     &Settings::shadersDir, &App::shadersDir, &App::shadersDirPick, nullptr},
};
}  // namespace

// every path defaults to empty, so the games folder goes too
void App::restorePathDefaults() {
  const Settings defaults;
  settings.gamesDir = defaults.gamesDir;
  settings.shotsDir = defaults.shotsDir;
  settings.savesDir = defaults.savesDir;
  settings.firmwareDir = defaults.firmwareDir;
  settings.patchesDir = defaults.patchesDir;
  settings.databaseDir = defaults.databaseDir;
  settings.statesDir = defaults.statesDir;
  settings.cheatsDir = defaults.cheatsDir;
  settings.shadersDir = defaults.shadersDir;
  for(int i = 0; i < EmuCore::MediumCount; i++) { settings.recentDir[i].clear(); }
  core.setPatchesDirectory(settings.patchesDir);
  refreshDatabaseDir();
  settings.sgbBios = defaults.sgbBios;
  settings.bsxBios = defaults.bsxBios;
  settings.stBios = defaults.stBios;
  core.setSavesDirectory(settings.savesDir);
  core.setFirmwareDirectory(firmwareDir());
  scanGames();
  gameSelected = 0;
}

void App::drawPathsTab() {
  ImGui::TextDisabled("Games folder");
  ImGui::TextWrapped("%s", settings.gamesDir.empty() ? "(not set)" : settings.gamesDir.c_str());
  if(ImGui::Button("Browse##games")) { openFolderDialog(dirPick); }
  ImGui::SameLine();
  if(ImGui::Button("Rescan")) { scanGames(); }
  ImGui::SameLine();
  ImGui::Text("%d games", (int)games.size());

  ImGui::Separator();
  ImGui::TextDisabled("Screenshots folder");
  ImGui::TextWrapped("%s",
                     settings.shotsDir.empty() ? "(config folder)" : settings.shotsDir.c_str());
  if(ImGui::Button("Browse##shots")) { openFolderDialog(shotDirPick); }

  for(const FolderRow& row : FolderRows) {
    ImGui::Separator();
    ImGui::TextDisabled("%s", row.label);
    std::string& dir = settings.*row.path;
    const std::string shown = !dir.empty()   ? dir
                              : row.resolved ? (this->*row.resolved)()
                              : row.unset    ? row.unset
                                             : "";
    ImGui::TextWrapped("%s", shown.c_str());
    if(row.help) { ImGui::TextWrapped("%s", row.help); }

    ImGui::PushID(row.label);
    if(ImGui::Button("Browse")) { openFolderDialog(this->*row.pick); }
    if(!dir.empty()) {
      ImGui::SameLine();
      if(ImGui::Button("Reset")) {
        dir.clear();
        if(row.push) { (this->*row.push)(); }
        settings.save(settingsCfg);
      }
    }
    ImGui::PopID();
  }

  ImGui::Separator();
  ImGui::TextDisabled("Firmware folder");
  ImGui::TextWrapped("%s", firmwareDir().c_str());
  ImGui::TextWrapped("Holds dsp1b.program.rom and the like, for dumps that omit it.");
  if(ImGui::Button("Browse##firmware")) { openFolderDialog(firmwareDirPick); }
  ImGui::SameLine();
  if(!settings.firmwareDir.empty() && ImGui::Button("Reset##firmware")) {
    settings.firmwareDir.clear();
    core.setFirmwareDirectory(firmwareDir());
    settings.save(settingsCfg);
  }

  ImGui::Separator();
  ImGui::TextDisabled("Base cartridges");
  ImGui::TextWrapped("Game Boy, BS-X and Sufami Turbo games run inside one of these.");
  for(const BiosSlot& slot : BiosSlots) {
    std::string& path = settings.*slot.path;
    ImGui::Text("%s: %s", slot.label, path.empty() ? "(not set)" : fileName(path).c_str());
    ImGui::PushID(slot.id);
    if(ImGui::Button("Browse")) {
      openMediaDialog(this->*slot.pick, "SNES ROMs", romFilterPattern());
    }
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

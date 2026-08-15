#include "ui.hpp"

#include "imgui_internal.h"  // BeginViewportSideBar, for the status bar

void placeFloating(float offsetX, float offsetY, float w, float h) {
  const ImGuiViewport* view = ImGui::GetMainViewport();
  const bool detach = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
  const float x = detach ? view->Pos.x + view->Size.x + 12.0f : view->WorkPos.x + offsetX;
  const float y = detach ? view->Pos.y + offsetY : view->WorkPos.y + offsetY;
  ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
}

namespace {
constexpr uint64_t MessageMs = 6000;

const struct { const char* name; void (App::*draw)(); } SettingsTabs[] = {
  {"Video",         &App::drawVideoTab},
  {"Audio",         &App::drawAudioTab},
  {"Input",         &App::drawInputTab},
  {"Hotkeys",       &App::drawHotkeysTab},
  {"Emulator",      &App::drawEmulatorTab},
  {"Enhancements",  &App::drawEnhancementsTab},
  {"Customization", &App::drawCustomizationTab},
  {"Paths",         &App::drawPathsTab},
};
}  // namespace

void App::drawFileMenu() {
  if(ImGui::MenuItem("Open ROM...", "Ctrl+O")) openRomDialog();
  // with no game the list is already the home screen
  if(ImGui::MenuItem("Games List", nullptr, showGames, core.loaded())) showGames = !showGames;

  if(ImGui::BeginMenu("Recent", !settings.recent.empty())) {
    for(const std::string& rom : settings.recent) {
      if(ImGui::MenuItem(fileName(rom).c_str())) loadRom(rom);
    }
    ImGui::Separator();
    if(ImGui::MenuItem("Clear")) { settings.recent.clear(); settings.save(settingsCfg); }
    ImGui::EndMenu();
  }

  ImGui::Separator();
  if(ImGui::MenuItem("Close Game", nullptr, false, core.loaded())) unloadRom();
  if(ImGui::MenuItem("Quit", hotkeyShortcut(HkQuit))) running = false;
}

// hotkey scancode as a menu shortcut label; empty when unbound
const char* App::hotkeyShortcut(Hotkey key) const {
  const char* name = SDL_GetScancodeName((SDL_Scancode)settings.hotkeys[key]);
  return name && *name ? name : nullptr;
}

// the slow down and speed up hotkeys walk this same list
void App::drawSpeedMenu() {
  for(int i = 0; i < SpeedCount; i++) {
    if(ImGui::MenuItem(SpeedNames[i], nullptr, speedIndex == i)) setSpeed(i);
  }
}

void App::drawEmulationMenu() {
  if(ImGui::MenuItem("Pause", hotkeyShortcut(HkPause), paused, core.loaded())) paused = !paused;
  if(ImGui::MenuItem("Frame Advance", hotkeyShortcut(HkFrameAdvance), false, core.loaded())) {
    paused = true;
    advanceOneFrame();
  }
  if(ImGui::MenuItem("Fast Forward", hotkeyShortcut(HkFastForward), fastForward, core.loaded())) {
    toggleFastForward();
  }
  if(ImGui::BeginMenu("Speed", core.loaded())) { drawSpeedMenu(); ImGui::EndMenu(); }
  if(ImGui::MenuItem("Mute", hotkeyShortcut(HkMute), settings.mute)) {
    settings.mute = !settings.mute;
    settings.save(settingsCfg);
  }
  ImGui::Separator();
  if(ImGui::MenuItem("Reset", hotkeyShortcut(HkReset), false, core.loaded())) reset();
  if(ImGui::MenuItem("Power Cycle", hotkeyShortcut(HkPowerCycle), false, core.loaded())) powerCycle();
}

void App::drawSettingsMenu() {
  for(int i = 0; i < IM_ARRAYSIZE(SettingsTabs); i++) {
    if(ImGui::MenuItem(SettingsTabs[i].name)) { showSettings = true; settingsTab = i; }
  }
  ImGui::Separator();
  if(ImGui::MenuItem("Show Status Bar", nullptr, &settings.showStatus)) {
    settings.save(settingsCfg);
  }
}

void App::drawMenuBar() {
  if(!ImGui::BeginMainMenuBar()) return;

  if(ImGui::BeginMenu("File"))      { drawFileMenu();      ImGui::EndMenu(); }
  if(ImGui::BeginMenu("Emulation")) { drawEmulationMenu(); ImGui::EndMenu(); }
  if(ImGui::BeginMenu("Settings"))  { drawSettingsMenu();  ImGui::EndMenu(); }

  if(ImGui::BeginMenu("Tools")) {
    if(ImGui::MenuItem("Diagnostics", nullptr, showTools)) showTools = !showTools;
    if(ImGui::MenuItem("Cartridge", nullptr, showCartridge, core.loaded())) {
      showCartridge = !showCartridge;
    }
    if(ImGui::MenuItem("Save Screenshot", "F12", false, core.loaded())) takeScreenshot();
    ImGui::EndMenu();
  }

  if(ImGui::BeginMenu("Help")) {
    if(ImGui::MenuItem("About")) showAbout = true;
    ImGui::EndMenu();
  }

  ImGui::EndMainMenuBar();
}

// A viewport side bar, so the work area the game fills already excludes it.
void App::drawStatusBar() {
  if(!settings.showStatus) return;

  if(!status.empty() && SDL_GetTicks() - messageTime > MessageMs) status.clear();

  ImGuiViewport* view = ImGui::GetMainViewport();
  if(ImGui::BeginViewportSideBar("##status", view, ImGuiDir_Down, ImGui::GetFrameHeight(),
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_MenuBar)) {
    if(ImGui::BeginMenuBar()) {
      ImGui::TextUnformatted(!status.empty() ? status.c_str()
                             : core.loaded() ? gameTitle.c_str() : "no game");

      char text[96];
      SDL_snprintf(text, sizeof(text), "%dx%d  %.1f fps%s",
                   shell.frameWidth, shell.frameHeight, fps,
                   fastForward ? "  [ff]" : paused ? "  [paused]" : "");
      const float width = ImGui::CalcTextSize(text).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - width - 12.0f);
      ImGui::TextUnformatted(text);
      ImGui::EndMenuBar();
    }
  }
  ImGui::End();
}

// wide enough that the tab bar never elides a label, at whatever font size
static float settingsWidth() {
  const ImGuiStyle& style = ImGui::GetStyle();
  float tabs = 0.0f;
  for(const auto& tab : SettingsTabs) {
    tabs += ImGui::CalcTextSize(tab.name).x + style.FramePadding.x * 2.0f
          + style.ItemInnerSpacing.x;
  }
  return SDL_max(520.0f, tabs + style.WindowPadding.x * 2.0f + style.ScrollbarSize);
}

void App::drawSettingsWindow() {
  if(!showSettings) return;

  placeFloating(40.0f, 40.0f, settingsWidth(), 480.0f);
  if(!ImGui::Begin("Settings", &showSettings)) { ImGui::End(); return; }

  if(ImGui::BeginTabBar("settingstabs")) {
    for(int i = 0; i < IM_ARRAYSIZE(SettingsTabs); i++) {
      const ImGuiTabItemFlags flags = settingsTab == i ? ImGuiTabItemFlags_SetSelected : 0;
      if(ImGui::BeginTabItem(SettingsTabs[i].name, nullptr, flags)) {
        (this->*SettingsTabs[i].draw)();
        ImGui::EndTabItem();
      }
    }
    ImGui::EndTabBar();
  }

  settingsTab = -1;
  ImGui::End();
}

void App::drawUi() {
  // fullscreen is the game only; the hotkey brings the chrome back
  if(!fullscreen()) {
    drawMenuBar();
    drawStatusBar();
  }

  if(core.loaded()) {
    shell.drawGame(settings);
    drawGamesWindow();
  } else {
    drawGamesHome();
  }

  drawSettingsWindow();
  drawToolsWindow();
  drawCartridgeWindow();
  drawAboutWindow();
}

#include "ui.hpp"

#include "imgui_internal.h"  // BeginViewportSideBar, for the status bar

void placeFloating(float w, float h) {
  const ImGuiViewport* view = ImGui::GetMainViewport();
  const float x = view->WorkPos.x + SDL_max(0.0f, (view->WorkSize.x - w) / 2.0f);
  const float y = view->WorkPos.y + SDL_max(0.0f, (view->WorkSize.y - h) / 2.0f);
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
  {"Compatibility", &App::drawCompatibilityTab},
  {"Customization", &App::drawCustomizationTab},
  {"Paths",         &App::drawPathsTab},
};

// "(empty)", or when the slot was written
std::string stateStamp(int64_t seconds) {
  if(seconds == 0) return "(empty)";
  SDL_DateTime when;
  if(!SDL_TimeToDateTime(SDL_SECONDS_TO_NS(seconds), &when, true)) return "(saved)";
  char text[32];
  SDL_snprintf(text, sizeof(text), "(%04d-%02d-%02d %02d:%02d)",
               when.year, when.month, when.day, when.hour, when.minute);
  return text;
}
}  // namespace

void App::drawFileMenu() {
  if(ImGui::MenuItem("Open ROM...", "Ctrl+O")) openRomDialog();
  if(ImGui::MenuItem("Open Game Pak...")) openFolderDialog(pakPick);
  if(ImGui::MenuItem("Open Sufami Turbo Pair...")) openSufamiPairDialog();
  // with no game the list is already the home screen
  if(ImGui::MenuItem("Games List", nullptr, showGames, core.loaded())) showGames = !showGames;

  if(ImGui::BeginMenu("Recent", !settings.recent.empty())) {
    for(const std::string& rom : settings.recent) {
      if(ImGui::MenuItem(recentLabel(rom).c_str())) loadRom(rom);
    }
    ImGui::Separator();
    if(ImGui::MenuItem("Clear")) { settings.recent.clear(); settings.save(settingsCfg); }
    ImGui::EndMenu();
  }

  ImGui::Separator();
  if(ImGui::MenuItem("Close Game", hotkeyShortcut(HkUnloadGame).c_str(), false, core.loaded())) {
    unloadRom();
  }
  if(ImGui::MenuItem("Quit", hotkeyShortcut(HkQuit).c_str())) running = false;
}

// the first mapping, as a menu shortcut label; empty when unbound
std::string App::hotkeyShortcut(Hotkey key) const {
  const Binding& binding = input.hotkey(key, 0);
  return binding.type == Binding::None ? std::string() : binding.label();
}

void App::setWindowScale(int scale) {
  settings.windowScale = SDL_clamp(scale, 0, MaxWindowScale);
  settings.save(settingsCfg);
  shell.shrinkToFit(settings);
}

std::string windowScaleLabel(int scale, const Settings& settings) {
  if(scale <= 0) return "Fit window";
  char label[24];
  SDL_snprintf(label, sizeof(label), "%dx (%dp)", scale, (int)videoHeight(settings) * scale);
  return label;
}

void App::drawWindowSizeMenu() {
  const int maxScale = shell.maxScale(settings);
  for(int scale = 0; scale <= maxScale; scale++) {
    const std::string label = windowScaleLabel(scale, settings);
    if(ImGui::MenuItem(label.c_str(), nullptr, settings.windowScale == scale)) {
      setWindowScale(scale);
    }
  }
  ImGui::Separator();
  if(ImGui::MenuItem("Shrink window to size")) shell.shrinkToFit(settings);
  if(ImGui::MenuItem("Center window")) shell.center(settings);
}

// the slow down and speed up hotkeys walk this same list
void App::drawSpeedMenu() {
  for(int i = 0; i < SpeedCount; i++) {
    if(ImGui::MenuItem(SpeedNames[i], nullptr, speedIndex == i)) setSpeed(i);
  }
}

// one menu for both directions: the slot list and its labels are the same
void App::drawStateMenu(bool loading) {
  for(int slot = 1; slot <= StateSlots; slot++) {
    const std::string name = slotName(slot);
    // one stat per slot: the stamp and the enable flag both come from it
    const int64_t when = stateTime(name);
    const std::string label = "Slot " + name + " " + stateStamp(when);
    // loading an empty slot only prints a message, so it stays disabled
    if(ImGui::MenuItem(label.c_str(), nullptr, stateSlot == slot, !loading || when != 0)) {
      stateSlot = slot;
      if(loading) loadState(name); else saveState(name);
    }
  }
  if(!loading) return;

  ImGui::Separator();
  if(ImGui::MenuItem("Undo Last Save", hotkeyShortcut(HkUndoState).c_str(),
                     false, hasState("undo"))) loadState("undo");
  if(ImGui::MenuItem("Redo Last Undo", hotkeyShortcut(HkRedoState).c_str(),
                     false, hasState("redo"))) loadState("redo");
  if(ImGui::MenuItem("Auto-resume State", nullptr, false, hasState("auto"))) loadState("auto");
  ImGui::Separator();
  if(ImGui::MenuItem("Remove All States")) confirmRemoveStates = true;
}

void App::drawRemoveStatesPrompt() {
  if(confirmRemoveStates && !ImGui::IsPopupOpen("Remove states")) {
    ImGui::OpenPopup("Remove states");
  }
  if(!ImGui::BeginPopupModal("Remove states", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) return;

  ImGui::Text("Permanently remove every state for %s?", gameTitle.c_str());
  ImGui::Separator();
  if(ImGui::Button("Remove")) {
    removeAllStates();
    confirmRemoveStates = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if(ImGui::Button("Cancel")) {
    confirmRemoveStates = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::drawEmulationMenu() {
  if(ImGui::MenuItem("Pause", hotkeyShortcut(HkPause).c_str(), paused, core.loaded())) paused = !paused;
  if(ImGui::MenuItem("Frame Advance", hotkeyShortcut(HkFrameAdvance).c_str(), false, core.loaded())) {
    paused = true;
    advanceOneFrame();
  }
  if(ImGui::MenuItem("Fast Forward", hotkeyShortcut(HkFastForward).c_str(), fastForward, core.loaded())) {
    toggleFastForward();
  }
  if(ImGui::BeginMenu("Speed", core.loaded())) { drawSpeedMenu(); ImGui::EndMenu(); }
  if(ImGui::MenuItem("Mute", hotkeyShortcut(HkMute).c_str(), settings.mute)) {
    settings.mute = !settings.mute;
    settings.save(settingsCfg);
  }
  ImGui::Separator();
  if(ImGui::BeginMenu("Save State", core.loaded())) { drawStateMenu(false); ImGui::EndMenu(); }
  if(ImGui::BeginMenu("Load State", core.loaded())) { drawStateMenu(true); ImGui::EndMenu(); }

  ImGui::Separator();
  if(ImGui::MenuItem("Reset", hotkeyShortcut(HkReset).c_str(), false, core.loaded())) reset();
  if(ImGui::MenuItem("Power Cycle", hotkeyShortcut(HkPowerCycle).c_str(), false, core.loaded())) powerCycle();
}

void App::drawSettingsMenu() {
  for(int i = 0; i < IM_ARRAYSIZE(SettingsTabs); i++) {
    if(ImGui::MenuItem(SettingsTabs[i].name)) { showSettings = true; settingsTab = i; }
  }
  ImGui::Separator();
  if(ImGui::BeginMenu("Window Size", !fullscreen())) { drawWindowSizeMenu(); ImGui::EndMenu(); }
  if(ImGui::MenuItem("Fullscreen", hotkeyShortcut(HkFullscreen).c_str(), fullscreen())) {
    toggleFullscreen();
  }
  if(ImGui::MenuItem("Capture Mouse", hotkeyShortcut(HkMouseCapture).c_str(), mouseCaptured)) {
    toggleMouseCapture();
  }
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
    if(ImGui::MenuItem("Lua Scripting", nullptr, showScripting)) showScripting = !showScripting;
    if(ImGui::MenuItem("Diagnostics", nullptr, showTools)) showTools = !showTools;
    if(ImGui::MenuItem("Manifest", nullptr, showManifest, core.loaded())) {
      showManifest = !showManifest;
    }
    if(ImGui::MenuItem("Save Screenshot", hotkeyShortcut(HkScreenshot).c_str(),
                       false, core.loaded())) takeScreenshot();
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
      if(core.loaded()) {
        // bsnes marks the same thing with an icon
        const bool clean = core.verified();
        ImGui::TextColored(clean ? ImVec4(0.4f, 0.85f, 0.4f, 1.0f)
                                 : ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "%s", clean ? "*" : "?");
        tip(clean ? "A known clean game image; PCB emulation is exact."
                  : "Not a verified game image; PCB emulation relies on heuristics.");
        ImGui::SameLine();
      }
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

// tabs clip rather than scroll, so being a pixel short truncates a label;
// the per-tab term mirrors TabItemCalcSize()
static float settingsWidth() {
  const ImGuiStyle& style = ImGui::GetStyle();
  float tabs = 0.0f;
  for(const auto& tab : SettingsTabs) {
    tabs += ImGui::CalcTextSize(tab.name).x + style.FramePadding.x * 2.0f + 1.0f
          + style.ItemInnerSpacing.x;
  }
  return SDL_max(520.0f, tabs + style.WindowPadding.x * 2.0f + style.ScrollbarSize
                       + style.WindowBorderSize * 2.0f);
}

void App::drawSettingsWindow() {
  if(!showSettings) return;

  // measured every frame, so a resize or a font change cannot squeeze the tab bar
  const float width = settingsWidth();
  placeFloating(width, 480.0f);
  ImGui::SetNextWindowSizeConstraints(ImVec2(width, 240.0f), ImVec2(FLT_MAX, FLT_MAX));
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
    // frame advance is a paused game being looked at, so it stays undimmed
    const bool dim = settings.videoDimming && emulationIdle() && !frameAdvance;
    shell.drawGame(settings, dim ? 0xff808080u : 0xffffffffu);
    scripting.drawOverlay();
    drawGamesWindow();
  } else {
    drawGamesHome();
  }
  drawSettingsWindow();
  drawToolsWindow();
  drawScriptingWindow();
  drawManifestWindow();
  drawAboutWindow();
  drawUnverifiedPrompt();
  drawRemoveStatesPrompt();
}

#include "ui.hpp"

void App::drawScriptingWindow() {
  if(!showScripting) return;

  placeFloating(520.0f, 360.0f);
  if(!ImGui::Begin("Lua Scripting", &showScripting)) { ImGui::End(); return; }

  if(ImGui::Button("Open...")) openScriptDialog();
  ImGui::SameLine();
  ImGui::BeginDisabled(scripting.path().empty());
  if(ImGui::Button("Reload")) scripting.reload();
  ImGui::SameLine();
  if(ImGui::Button("Stop")) scripting.stop();
  ImGui::EndDisabled();

  ImGui::Separator();
  ImGui::Text("State: %s", scripting.running() ? "running" : "stopped");
  ImGui::TextWrapped("Script: %s", scripting.path().empty() ? "none" : scripting.path().c_str());
  if(!scripting.path().empty()) ImGui::TextWrapped("Data: %s", scripting.dataDirectory().c_str());
  if(!scripting.error().empty()) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", scripting.error().c_str());
  }

  ImGui::SeparatorText("Console");
  ImGui::BeginDisabled(scripting.console().empty());
  if(ImGui::Button("Copy")) ImGui::SetClipboardText(scripting.console().c_str());
  ImGui::SameLine();
  if(ImGui::Button("Clear")) scripting.clearConsole();
  ImGui::EndDisabled();
  const bool scroll = scripting.takeConsoleScroll();
  if(ImGui::BeginChild("##luaconsole", ImVec2(0, 0), ImGuiChildFlags_Borders,
                       ImGuiWindowFlags_HorizontalScrollbar)) {
    if(scripting.console().empty()) ImGui::TextDisabled("print output appears here");
    else ImGui::TextUnformatted(scripting.console().c_str());
    if(scroll) ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();

  ImGui::End();
}

// a device SDL cannot map never becomes a gamepad, so it is listed but unread
void App::drawGamepadDiagnostics() {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&count);
  ImGui::Text("Joysticks: %d, opened as gamepads: %d", count, livePadCount(pads));

  for(int i = 0; ids && i < count; i++) {
    const char* name = SDL_GetJoystickNameForID(ids[i]);
    if(SDL_IsGamepad(ids[i])) {
      ImGui::Text("  %s", name ? name : "?");
    } else {
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "  %s - no gamepad mapping",
                         name ? name : "?");
    }
  }
  if(ids) SDL_free(ids);

  for(size_t i = 0; i < pads.size(); i++) {
    SDL_Gamepad* pad = pads[i];
    if(!pad) { ImGui::Text("pad %d: empty", (int)i + 1); continue; }
    ImGui::Text("pad %d: %04x:%04x", (int)i + 1,
                SDL_GetGamepadVendor(pad), SDL_GetGamepadProduct(pad));

    std::string held;
    for(int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++) {
      if(!SDL_GetGamepadButton(pad, (SDL_GamepadButton)b)) continue;
      const char* label = SDL_GetGamepadStringForButton((SDL_GamepadButton)b);
      if(!held.empty()) held += " ";
      held += label ? label : "?";
    }
    ImGui::SameLine();
    ImGui::TextUnformatted(held.empty() ? "  (nothing held)" : ("  " + held).c_str());

    // whatever the mapping leaves out never reaches the frontend at all
    ImGui::PushID((int)i);
    if(ImGui::TreeNode("mapping")) {
      if(char* mapping = SDL_GetGamepadMapping(pad)) {
        ImGui::TextWrapped("%s", mapping);
        SDL_free(mapping);
      } else {
        ImGui::TextUnformatted("none");
      }
      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void App::drawToolsWindow() {
  if(!showTools) return;

  placeFloating(380.0f, 250.0f);
  if(!ImGui::Begin("Diagnostics", &showTools)) { ImGui::End(); return; }

  ImGui::Text("Game: %s", core.loaded() ? gameTitle.c_str() : "none");
  ImGui::Text("Resolution: %d x %d", shell.frameWidth, shell.frameHeight);
  ImGui::Text("Frame rate: %.1f fps (target %.3f)", fps, core.refreshRate());
  ImGui::Text("Audio queued: %d", (int)SDL_GetAudioStreamQueued(shell.audio));
  ImGui::Text("Pacing target: %d", shell.paceTarget(settings));
  ImGui::Text("State: %s%s", core.loaded() ? (paused ? "paused" : "running") : "idle",
              fastForward ? " (fast forward)" : "");
  ImGui::Separator();
  drawGamepadDiagnostics();

  ImGui::Separator();
  ImGui::BeginDisabled(!core.loaded());
  if(ImGui::Button("Save Screenshot")) takeScreenshot();
  ImGui::SameLine();
  if(ImGui::Button("Reset")) reset();
  ImGui::EndDisabled();
  if(!status.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s", status.c_str()); }

  ImGui::End();
}

void App::drawGamesList() {
  if(settings.gamesDir.empty()) {
    ImGui::TextWrapped("No games folder set.");
    if(ImGui::Button("Choose folder")) openFolderDialog(dirPick);
    ImGui::SameLine();
    if(ImGui::Button("Open ROM...")) openRomDialog();
    return;
  }

  ImGui::TextWrapped("%s", settings.gamesDir.c_str());
  if(ImGui::Button("Rescan")) scanGames();
  ImGui::SameLine();
  if(ImGui::Button("Change folder")) openFolderDialog(dirPick);
  ImGui::SameLine();
  if(ImGui::Button("Open ROM...")) openRomDialog();
  ImGui::SameLine();
  ImGui::Text(games.size() == 1 ? "%d game" : "%d games", (int)games.size());
  if(ImGui::Button("Play selected") && gameSelected < (int)games.size()) {
    if(loadRom(games[gameSelected].second)) showGames = false;
  }
  ImGui::Separator();

  if(ImGui::BeginListBox("##games", ImVec2(-1.0f, -1.0f))) {
    ImGuiListClipper clipper;
    clipper.Begin((int)games.size());
    while(clipper.Step()) {
      for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        if(ImGui::Selectable(games[i].first.c_str(), gameSelected == i)) gameSelected = i;
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
          if(loadRom(games[i].second)) showGames = false;
        }
      }
    }
    ImGui::EndListBox();
  }
}

void App::drawGamesWindow() {
  if(!showGames) return;

  placeFloating(440.0f, 420.0f);
  if(ImGui::Begin("Games", &showGames)) drawGamesList();
  ImGui::End();
}

void App::drawGamesHome() {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoSavedSettings
                               | ImGuiWindowFlags_NoBringToFrontOnFocus
                               | ImGuiWindowFlags_NoNavFocus;
  if(ImGui::Begin("##home", nullptr, flags)) drawGamesList();
  ImGui::End();
}

void App::drawAboutWindow() {
  if(!showAbout) return;

  placeFloating(340.0f, 150.0f);
  if(ImGui::Begin("About", &showAbout)) {
    ImGui::TextUnformatted(AppName);
    ImGui::Separator();
    ImGui::TextWrapped("Custom bsnes frontend, brought to you by HeatXD.");
    ImGui::Text("bsnes %s", EmuCore::version().c_str());
    // SameBoy is the Super Game Boy core; GB_VERSION comes from gb/version.mk
    ImGui::Text("SameBoy %s", GB_VERSION);
    ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
    ImGui::Text("SDL %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
  }
  ImGui::End();
}

void App::drawUnverifiedPrompt() {
  if(!unverifiedPrompt) return;

  const char* id = "Unverified game";
  if(!ImGui::IsPopupOpen(id)) ImGui::OpenPopup(id);

  const ImGuiViewport* view = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(view->WorkPos.x + view->WorkSize.x / 2.0f,
                                 view->WorkPos.y + view->WorkSize.y / 2.0f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if(!ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

  ImGui::Text("%s is not in the games database; its board layout is guessed.",
              gameTitle.c_str());
  ImGui::Separator();

  if(ImGui::Button("Run it")) {
    unverifiedPrompt = false;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if(ImGui::Button("Always run")) {
    unverifiedPrompt = false;
    settings.warnUnverified = false;
    settings.save(settingsCfg);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if(ImGui::Button("Cancel")) {
    unverifiedPrompt = false;
    unloadRom();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

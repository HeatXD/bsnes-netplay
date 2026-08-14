#include "ui.hpp"

void App::drawToolsWindow() {
  if(!showTools) return;

  placeFloating(60.0f, 480.0f, 380.0f, 250.0f);
  if(!ImGui::Begin("Diagnostics", &showTools)) { ImGui::End(); return; }

  ImGui::Text("Game: %s", core.loaded() ? gameTitle.c_str() : "none");
  ImGui::Text("Resolution: %d x %d", shell.frameWidth, shell.frameHeight);
  ImGui::Text("Frame rate: %.1f fps (target %.3f)", fps, core.refreshRate());
  ImGui::Text("Audio queued: %d", (int)SDL_GetAudioStreamQueued(shell.audio));
  ImGui::Text("Pacing target: %d", shell.paceTarget(settings));
  ImGui::Text("State: %s%s", core.loaded() ? (paused ? "paused" : "running") : "idle",
              fastForward ? " (fast forward)" : "");
  ImGui::Separator();
  if(ImGui::Button("Save Screenshot")) takeScreenshot();
  ImGui::SameLine();
  if(ImGui::Button("Reset")) reset();
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
  ImGui::Text("%d", (int)games.size());
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

  placeFloating(40.0f, 40.0f, 440.0f, 420.0f);
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

  placeFloating(100.0f, 120.0f, 340.0f, 150.0f);
  if(ImGui::Begin("About", &showAbout)) {
    ImGui::TextUnformatted(AppName);
    ImGui::Separator();
    ImGui::TextWrapped("Custom bsnes frontend, brought to you by HeatXD.");
    ImGui::Text("Dear ImGui %s", IMGUI_VERSION);
    ImGui::Text("SDL %d.%d.%d", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_MICRO_VERSION);
  }
  ImGui::End();
}

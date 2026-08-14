#include "app.hpp"

#include "imgui_impl_opengl3.h"

#include <algorithm>

void App::scanGames() {
  games.clear();
  if(settings.gamesDir.empty()) return;

  int count = 0;
  char** found = SDL_GlobDirectory(settings.gamesDir.c_str(), nullptr, 0, &count);
  if(!found) return;

  for(int i = 0; i < count; i++) {
    if(!isRom(found[i])) continue;
    games.emplace_back(fileStem(found[i]), settings.gamesDir + "/" + found[i]);
  }
  SDL_free(found);
  std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
    return SDL_strcasecmp(a.first.c_str(), b.first.c_str()) < 0;
  });
}

bool App::loadRom(const std::string& path) {
  if(core.loadSuperFamicom(path)) {
    gameTitle = core.title();
    SDL_SetWindowTitle(shell.window, (gameTitle + " - " + AppName).c_str());
    settings.addRecent(path);
    settings.save(settingsCfg);
    paused = false;
    applySpeed();
    showMessage("loaded " + gameTitle);
    return true;
  }
  showMessage("failed to load " + fileName(path));
  return false;
}

void App::unloadRom() {
  core.unload();
  gameTitle.clear();
  shell.clearFrame();
  showGames = false;
  SDL_SetWindowTitle(shell.window, AppName);
}

// filters == nullptr opens a folder picker instead
void App::openPick(FilePick& pick, const SDL_DialogFileFilter* filters, const char* dir) {
  Guard guard(pick.mutex);
  if(pick.open) return;
  pick.open = true;
  if(filters) SDL_ShowOpenFileDialog(onPicked, &pick, shell.window, filters, 2, dir, false);
  else        SDL_ShowOpenFolderDialog(onPicked, &pick, shell.window, dir, false);
}

const char* App::gamesDirOrNull() const {
  return settings.gamesDir.empty() ? nullptr : settings.gamesDir.c_str();
}

void App::openRomDialog() {
  const SDL_DialogFileFilter filters[] = {{"SNES ROMs", romFilterPattern()}, {"All files", "*"}};
  openPick(romPick, filters, gamesDirOrNull());
}

void App::openFontDialog() {
  const SDL_DialogFileFilter filters[] = {{"Fonts", "ttf;otf;ttc"}, {"All files", "*"}};
  openPick(fontPick, filters, nullptr);
}

void App::applyPreset() {
  switch(settings.theme) {
    case 1: ImGui::StyleColorsLight(); break;
    case 2: ImGui::StyleColorsClassic(); break;
    default: ImGui::StyleColorsDark(); break;
  }

  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  // panels sit over the running game, so they must not be see-through
  style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  style.Colors[ImGuiCol_PopupBg].w = 1.0f;
  style.Colors[ImGuiCol_MenuBarBg].w = 1.0f;
}

// the accent drives everything interactive; the greys come from the preset
void App::applyAccent() {
  ImGuiStyle& style = ImGui::GetStyle();
  const ImVec4 accent = accentColor();
  auto tint = [&](ImGuiCol index, float alpha) {
    style.Colors[index] = ImVec4(accent.x, accent.y, accent.z, alpha);
  };
  tint(ImGuiCol_CheckMark, 1.0f);
  tint(ImGuiCol_SliderGrab, 0.85f);
  tint(ImGuiCol_SliderGrabActive, 1.0f);
  tint(ImGuiCol_Button, 0.45f);
  tint(ImGuiCol_ButtonHovered, 0.75f);
  tint(ImGuiCol_ButtonActive, 1.0f);
  tint(ImGuiCol_Header, 0.45f);
  tint(ImGuiCol_HeaderHovered, 0.75f);
  tint(ImGuiCol_HeaderActive, 1.0f);
  tint(ImGuiCol_Tab, 0.45f);
  tint(ImGuiCol_TabHovered, 0.75f);
  tint(ImGuiCol_TabSelected, 1.0f);
  tint(ImGuiCol_TitleBgActive, 0.75f);
  tint(ImGuiCol_FrameBgHovered, 0.45f);
  tint(ImGuiCol_FrameBgActive, 0.65f);
  tint(ImGuiCol_SeparatorHovered, 0.75f);
  tint(ImGuiCol_SeparatorActive, 1.0f);
  tint(ImGuiCol_ResizeGrip, 0.35f);
  tint(ImGuiCol_ResizeGripHovered, 0.75f);
  tint(ImGuiCol_ResizeGripActive, 1.0f);
  tint(ImGuiCol_TextSelectedBg, 0.45f);
  tint(ImGuiCol_NavCursor, 1.0f);

  if(settings.textColor != FollowTheme) style.Colors[ImGuiCol_Text] = unpackColor(settings.textColor);
}

void App::applyTheme() {
  applyPreset();
  applyAccent();
}

// Only safe between frames: rebuilding the atlas invalidates the font texture.
void App::applyFont() {
  fontDirty = false;

  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();

  ImFontConfig config;
  config.SizePixels = (float)settings.fontSize;
  config.RasterizerMultiply = settings.fontWeight / 100.0f;

  ImFont* font = nullptr;
  // AddFontFromFileTTF asserts on a missing file rather than returning null
  if(!settings.fontPath.empty() && pathExists(settings.fontPath)) {
    font = io.Fonts->AddFontFromFileTTF(settings.fontPath.c_str(), config.SizePixels, &config);
  }
  if(!font) {
    if(!settings.fontPath.empty()) showMessage("could not load font " + fileName(settings.fontPath));
    io.Fonts->AddFontDefault(&config);
  }

  io.Fonts->Build();
  ImGui_ImplOpenGL3_DestroyFontsTexture();  // recreated by the next NewFrame
}

void App::takeScreenshot() {
  std::string dir = settings.shotsDir.empty() ? configDir() : settings.shotsDir;
  if(!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';

  char stamp[32];
  SDL_snprintf(stamp, sizeof(stamp), "shot-%" SDL_PRIu64 ".bmp", SDL_GetTicks());
  const std::string name = dir + stamp;
  showMessage(shell.saveFrame(name) ? "saved " + name : "screenshot failed");
}


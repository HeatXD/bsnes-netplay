#include "app.hpp"

#include "imgui_impl_opengl3.h"

#include <algorithm>

const BiosSlot BiosSlots[3] = {
  {"Super Game Boy", "##sgb", &App::sgbBiosPick, &Settings::sgbBios},
  {"BS-X",           "##bsx", &App::bsxBiosPick, &Settings::bsxBios},
  {"Sufami Turbo",   "##st",  &App::stBiosPick,  &Settings::stBios},
};

App::App() : scripting(*this) {}
App::~App() = default;

// beside the exe first, so a portable copy carries its own database
std::string App::databaseDir() const {
  if(!settings.databaseDir.empty()) return settings.databaseDir;
  if(const char* base = SDL_GetBasePath()) {
    const std::string beside = std::string(base) + "Database";
    if(isDirectory(beside)) return beside;
  }
  return configDir() + "Database";
}

std::string App::shadersDir() const {
  if(!settings.shadersDir.empty()) return settings.shadersDir;
  if(const char* base = SDL_GetBasePath()) {
    const std::string beside = std::string(base) + "Shaders";
    if(isDirectory(beside)) return beside;
  }
  return configDir() + "Shaders";
}

void App::applyShader() {
  std::vector<ShaderParam> overrides;
  for(const ShaderSetting& param : settings.shaderParams) {
    overrides.push_back({param.name, param.value, {}});
  }
  if(!shell.shader.load(settings.videoShader, overrides)) {
    showMessage("shader: " + shell.shader.failure);
  }
  applyVideoFilter();
  // a shader switched on while paused has no frame of its own yet
  shell.repushVideo();
}

void App::applyVideoFilter() {
  core.setFilter(shell.shader.active() ? "None" : settings.videoFilter);
}

void App::saveShaderParams() {
  settings.shaderParams.clear();
  for(const ShaderParam& param : shell.shader.params) {
    if(param.value != param.stock) settings.shaderParams.push_back({param.name, param.value});
  }
}

void App::rememberDir(const std::string& path) {
  const std::string normal = normalPath(path);
  if(std::string dir = parentDir(normal); !dir.empty()) {
    settings.recentDir[(int)EmuCore::mediumOf(normal)] = dir;
  }
}

void App::scanGames() {
  games.clear();
  if(settings.gamesDir.empty()) return;

  int count = 0;
  char** found = SDL_GlobDirectory(settings.gamesDir.c_str(), nullptr, 0, &count);
  if(!found) return;

  for(int i = 0; i < count; i++) {
    const std::string path = settings.gamesDir + "/" + found[i];
    // a game pak is a folder, recognised the way the core does: by its manifest
    if(isDirectory(path)) {
      if(pathExists(path + "/manifest.bml")) games.emplace_back(found[i], pakPath(path));
      continue;
    }
    if(!isRom(found[i])) continue;
    games.emplace_back(fileStem(found[i]), path);
  }
  SDL_free(found);
  std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
    return SDL_strcasecmp(a.first.c_str(), b.first.c_str()) < 0;
  });
}

bool App::loadRom(const std::string& entry) {
  if(netplayActive()) {
    showMessage("cannot change games during netplay");
    return false;
  }
  if(movieActive()) {
    showMessage("stop the movie before changing games");
    return false;
  }
  auto [first, second] = splitPair(entry);
  if(isDirectory(first)) first = pakPath(first);

  // slot media ride in a base cartridge, set once under Paths
  EmuCore::GameSpec spec;
  const char* system = nullptr;
  switch(EmuCore::mediumOf(first)) {
  case EmuCore::Medium::GameBoy:
    spec.gameBoy = first;
    spec.superFamicom = settings.sgbBios;
    system = "Super Game Boy";
    break;
  case EmuCore::Medium::BSMemory:
    spec.bsMemory = first;
    spec.superFamicom = settings.bsxBios;
    system = "BS-X";
    break;
  case EmuCore::Medium::SufamiTurbo:
    spec.sufamiTurboA = first;
    spec.sufamiTurboB = second;
    spec.superFamicom = settings.stBios;
    system = "Sufami Turbo";
    break;
  case EmuCore::Medium::SuperFamicom:
    spec.superFamicom = first;
    break;
  }

  if(system && spec.superFamicom.empty()) {
    showMessage(std::string(system) + " games need its base cartridge, set under Paths");
    return false;
  }

  if(!core.load(spec)) {
    showMessage("failed to load " + fileName(first) + ": " + core.loadError());
    return false;
  }

  resetTimeline();
  undoState.clear();
  redoState.clear();
  gameTitle = fileStem(first);
  gameLocation = first;
  loadCheats();
  cheatCandidates.clear();
  cheatCandidateSelected = -1;
  SDL_SetWindowTitle(shell.window, (gameTitle + " - " + AppName).c_str());
  settings.addRecent(entry);
  rememberDir(first);
  settings.save(settingsCfg);
  paused = false;
  speedIndex = SpeedNormal;
  applySpeed();
  autoSaveMark = SDL_GetTicks();

  // held before the first frame, so cancelling means nothing ever ran
  unverifiedPrompt = settings.warnUnverified && !core.verified();

  const std::string patchError = core.patchError();
  const std::string missing = core.missingFiles();

  std::string note;
  auto add = [&](const std::string& text) { note += (note.empty() ? ", " : "; ") + text; };
  if(core.patched()) add("patch applied");
  if(!patchError.empty()) add(patchError);
  if(!missing.empty()) add("missing " + missing);

  showMessage((core.verified() ? "loaded verified " : "loaded ") + gameTitle + note);
  // after the message, so a failed resume is what the status line ends on
  if(settings.autoStateOnLoad && settings.autoStateOnUnload && hasState("auto")) {
    loadState("auto");
  }
  return true;
}

// relative mode frees the pointer from the screen edges, which aiming needs
void App::toggleMouseCapture() {
  mouseCaptured = !mouseCaptured;
  SDL_SetWindowRelativeMouseMode(shell.window, mouseCaptured);
  // the accumulated delta from before the grab would arrive as one jump
  if(mouseCaptured) SDL_GetRelativeMouseState(nullptr, nullptr);
  showMessage(mouseCaptured ? "mouse captured, press the capture hotkey to release"
                            : "mouse released");
}

// a crash between here and unload costs one interval, not the session
void App::saveMemoryTick() {
  if(!settings.autoSaveMemory || !core.loaded()) return;

  const uint64_t now = SDL_GetTicks();
  if(now - autoSaveMark < (uint64_t)settings.autoSaveInterval * 1000) return;
  autoSaveMark = now;
  core.saveMemory();
}

void App::resetTimeline() {
  rewinding = false;
  rewindCounter = 0;
  std::deque<std::vector<uint8_t>>().swap(rewindHistory);
}

void App::setRewinding(bool enabled) {
  if(!enabled) {
    rewinding = false;
    rewindCounter = 0;
    return;
  }
  if(!core.loaded() || fastForward || movieActive() || netplayActive()) {
    return;
  }
  if(settings.rewindFrequency == 0) {
    showMessage("enable rewind under Emulator settings first");
    return;
  }
  if(rewindHistory.empty()) {
    showMessage("rewind history is empty");
    return;
  }
  rewinding = true;
  rewindCounter = SDL_max(1, settings.rewindFrequency / 4) - 1;
  if(shell.audio) SDL_ClearAudioStream(shell.audio);
}

void App::captureRewind() {
  const bool unavailable = rewinding
                        || movieActive()
                        || settings.rewindFrequency == 0
                        || !EmuCore::deterministicStates();
  if(unavailable) {
    return;
  }
  if(++rewindCounter < settings.rewindFrequency) return;
  rewindCounter = 0;

  std::vector<uint8_t> state = core.serialize(false);
  if(state.empty()) return;
  while((int)rewindHistory.size() >= settings.rewindLength) rewindHistory.pop_front();
  rewindHistory.push_back(std::move(state));
}

void App::stepRewind() {
  if(!rewinding) return;
  if(++rewindCounter < SDL_max(1, settings.rewindFrequency / 4)) return;
  rewindCounter = 0;

  if(rewindHistory.empty()) {
    rewinding = false;
    showMessage("rewind history exhausted");
    return;
  }
  std::vector<uint8_t> state = std::move(rewindHistory.back());
  rewindHistory.pop_back();
  if(!core.unserialize(state)) {
    resetTimeline();
    showMessage("rewind state could not be restored");
  }
}

void App::advanceEmulation() {
  if(netplayActive()) { netplayRun(); return; }

  captureRewind();
  stepRewind();

  const bool runAhead = settings.runAheadFrames > 0
                     && !fastForward
                     && !rewinding
                     && !movieActive()
                     && !scripting.running()
                     && EmuCore::deterministicStates();
  if(!runAhead) {
    scripting.runBeforeFrame();
    core.runFrame();
    emulatedFrames++;
    scripting.runFrame();
    return;
  }

  core.setRunAhead(true);
  core.runFrame();
  emulatedFrames++;
  const std::vector<uint8_t> state = core.serialize(false);
  for(int frame = 1; frame < settings.runAheadFrames; frame++) core.runFrame();
  core.setRunAhead(false);
  core.runFrame();

  if(state.empty() || !core.unserialize(state)) {
    settings.runAheadFrames = 0;
    settings.save(settingsCfg);
    showMessage("run-ahead disabled after a state restore failure");
  }
}

void App::pushEnhancements() {
  // this set includes every hack netplayApplyDeterministicSettings() forces
  // (entropy, overclocks, PPU/DSP fast paths, ...); pushing it mid-session
  // would silently replace the agreed-on values with the user's own and
  // desync every peer. netplayStop() clears netplay.mode before calling this.
  if(netplayActive()) return;
  core.setOption("Frontend/Hotfixes", flag(settings.hackHotfixes));
  core.setOption("Hacks/Entropy", EntropyNames[settings.hackEntropy]);
  core.setOption("Hacks/CPU/Overclock", std::to_string(settings.hackCpuOverclock));
  core.setOption("Hacks/CPU/FastMath", flag(settings.hackCpuFastMath));
  core.setOption("Hacks/PPU/Fast", flag(settings.hackPpuFast));
  core.setOption("Hacks/PPU/Deinterlace", flag(settings.hackPpuDeinterlace));
  core.setOption("Hacks/PPU/NoSpriteLimit", flag(settings.hackPpuNoSpriteLimit));
  core.setOption("Hacks/PPU/NoVRAMBlocking", flag(settings.hackPpuNoVRAMBlocking));
  core.setOption("Hacks/PPU/Mode7/Scale", std::to_string(settings.hackMode7Scale));
  core.setOption("Hacks/PPU/Mode7/Perspective", flag(settings.hackMode7Perspective));
  core.setOption("Hacks/PPU/Mode7/Supersample", flag(settings.hackMode7Supersample));
  core.setOption("Hacks/PPU/Mode7/Mosaic", flag(settings.hackMode7Mosaic));
  core.setOption("Hacks/DSP/Fast", flag(settings.hackDspFast));
  core.setOption("Hacks/DSP/Cubic", flag(settings.hackDspCubic));
  core.setOption("Hacks/DSP/EchoShadow", flag(settings.hackDspEchoShadow));
  core.setOption("Hacks/Coprocessor/DelayedSync", flag(settings.hackCoprocessorDelayedSync));
  core.setOption("Hacks/Coprocessor/PreferHLE", flag(settings.hackCoprocessorPreferHLE));
  core.setOption("Hacks/SA1/Overclock", std::to_string(settings.hackSa1Overclock));
  core.setOption("Hacks/SuperFX/Overclock", std::to_string(settings.hackSuperFxOverclock));
}

void App::unloadRom() {
  if(netplayActive()) netplayStop();
  if(movieActive()) {
    showMessage("stop the movie before closing the game");
    return;
  }
  // the game is still in the core, so this has to happen before the unload
  if(settings.autoStateOnUnload) saveState("auto", true);
  saveCheats();
  core.setCheats({});
  resetTimeline();
  core.unload();
  gameTitle.clear();
  gameLocation.clear();
  cheats.clear();
  // the memory slots belong to the machine that just went away
  undoState.clear(); undoState.shrink_to_fit();
  redoState.clear(); redoState.shrink_to_fit();
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

// the folder this medium was last opened from, falling back to the games folder
const char* App::startDirFor(EmuCore::Medium medium) {
  const std::string& remembered = settings.recentDir[(int)medium];
  if(!remembered.empty() && isDirectory(remembered)) return remembered.c_str();
  return gamesDirOrNull();
}

void App::openRomDialog() {
  const SDL_DialogFileFilter filters[] = {{"SNES ROMs", romFilterPattern()}, {"All files", "*"}};
  openPick(romPick, filters, startDirFor(EmuCore::Medium::SuperFamicom));
}

void App::openMediaDialog(FilePick& pick, const char* label, const char* extensions,
                          EmuCore::Medium medium) {
  const SDL_DialogFileFilter filters[] = {{label, extensions}, {"All files", "*"}};
  openPick(pick, filters, startDirFor(medium));
}

void App::openSufamiPairDialog() {
  sufamiPending.clear();
  openMediaDialog(sufamiAPick, "Sufami Turbo ROMs", "st;zip;7z", EmuCore::Medium::SufamiTurbo);
}

void App::openFontDialog() {
  const SDL_DialogFileFilter filters[] = {{"Fonts", "ttf;otf;ttc"}, {"All files", "*"}};
  openPick(fontPick, filters, nullptr);
}

void App::openScriptDialog() {
  const SDL_DialogFileFilter filters[] = {{"Lua scripts", "lua"}, {"All files", "*"}};
  openPick(scriptPick, filters, nullptr);
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

void App::triggerHotkey(int index) {
  bool enhanced = false;  // set by the cases that change an enhancement setting

  switch(index) {
  case HkPause: if(core.loaded() && !netplayActive()) paused = !paused; break;
  case HkReset: if(core.loaded()) reset(); break;
  case HkFastForward: if(!rewinding) toggleFastForward(); break;
  case HkFullscreen: toggleFullscreen(); break;
  case HkScreenshot: takeScreenshot(); break;
  case HkFrameAdvance:
    if(core.loaded() && !netplayActive()) { paused = true; advanceOneFrame(); }
    break;
  case HkPowerCycle: if(core.loaded()) powerCycle(); break;
  case HkMute:
    settings.mute = !settings.mute;
    settings.save(settingsCfg);
    break;
  case HkQuit:
    if(movieActive()) {
      showMessage("stop the movie before quitting");
    } else {
      running = false;
    }
    break;
  case HkSpeedDown: setSpeed(speedIndex - 1); break;
  case HkSpeedUp: setSpeed(speedIndex + 1); break;
  case HkUnloadGame: if(core.loaded()) unloadRom(); break;
  case HkMouseCapture: toggleMouseCapture(); break;
  case HkMode7Down:
  case HkMode7Up:
    settings.hackMode7Scale = SDL_clamp(settings.hackMode7Scale
                                        + (index == HkMode7Up ? 1 : -1), 1, MaxMode7Scale);
    showMessage("HD mode 7 scale " + std::to_string(settings.hackMode7Scale) + "x");
    enhanced = true;
    break;
  case HkSaveState: if(core.loaded()) saveState(slotName(stateSlot)); break;
  case HkLoadState: if(core.loaded()) loadState(slotName(stateSlot)); break;
  case HkUndoState: if(core.loaded()) loadState("undo"); break;
  case HkRedoState: if(core.loaded()) loadState("redo"); break;
  case HkSlotDown: if(core.loaded()) setStateSlot(stateSlot - 1); break;
  case HkSlotUp: if(core.loaded()) setStateSlot(stateSlot + 1); break;
  case HkSupersample:
    settings.hackMode7Supersample = !settings.hackMode7Supersample;
    showMessage(std::string("supersampling ") + (settings.hackMode7Supersample ? "on" : "off"));
    enhanced = true;
    break;
  case HkRewind: break;
  }

  if(enhanced) {
    pushEnhancements();
    settings.save(settingsCfg);
  }
}

// polled, not event driven: a hotkey may be a pad button or a chord
void App::pollHotkeys() {
  const bool typing = ImGui::GetIO().WantCaptureKeyboard;
  const bool rebinding = capturing >= 0 || capturingHotkey >= 0;

  for(int i = 0; i < HotkeyCount; i++) {
    const bool held = !typing && !rebinding
                   && input.hotkeyHeld(i, sample, pads);
    if(i == HkRewind) {
      if(held != hotkeyWasHeld[i]) setRewinding(held);
    } else if(held && !hotkeyWasHeld[i]) {
      triggerHotkey(i);
    }
    hotkeyWasHeld[i] = held;
  }
}

void App::takeScreenshot() {
  std::string dir = settings.shotsDir.empty() ? configDir() : settings.shotsDir;
  if(!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';

  char stamp[32];
  SDL_snprintf(stamp, sizeof(stamp), "shot-%" SDL_PRIu64 ".bmp", SDL_GetTicks());
  const std::string name = dir + stamp;
  if(settings.screenshotLua) {
    pendingScreenshot = name;
    return;
  }
  showMessage(shell.saveFrame(name) ? "saved " + name : "screenshot failed");
}

void App::finishScreenshot(bool saved) {
  const std::string name = pendingScreenshot;
  pendingScreenshot.clear();
  showMessage(saved ? "saved " + name : "screenshot failed");
}

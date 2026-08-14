#include "emucore.hpp"
#include "inputmap.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include "imgui.h"
#include "imgui_internal.h"  // BeginViewportSideBar, for the status bar
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <algorithm>
#include <cstdio>  // freopen, for reattaching the console on windows
#include <functional>
#include <string>
#include <vector>

namespace {

// path helpers over SDL rather than std::filesystem; SDL globs return '/'
// separators but dialogs hand back native ones
std::string fileName(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string fileStem(const std::string& path) {
  const std::string name = fileName(path);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

bool pathExists(const std::string& path) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path.c_str(), &info);
}

struct Guard {
  SDL_Mutex* mutex;
  explicit Guard(SDL_Mutex* mutex) : mutex(mutex) { SDL_LockMutex(mutex); }
  ~Guard() { SDL_UnlockMutex(mutex); }
};

#ifdef _WIN32
// A windowed build has no console, so borrow the shell's when there is one.
// Declared by hand to keep windows.h out of the frontend.
extern "C" __declspec(dllimport) int __stdcall AttachConsole(unsigned long processId);
extern "C" __declspec(dllimport) void* __stdcall GetStdHandle(unsigned long id);

void attachParentConsole() {
  // a pipe or file redirect is already inherited; reopening would steal it
  void* out = GetStdHandle((unsigned long)-11);
  if(out && out != (void*)-1) return;

  if(!AttachConsole((unsigned long)-1)) return;
  freopen("CONOUT$", "w", stdout);
  freopen("CONOUT$", "w", stderr);
}
#else
void attachParentConsole() {}
#endif

// macOS offers a 2.1 legacy context or a 3.2+ core one, with nothing in
// between, so it needs a different request and a different shader header
#ifdef __APPLE__
constexpr int GlMajor = 3, GlMinor = 2;
constexpr const char* GlslVersion = "#version 150";
#else
constexpr int GlMajor = 3, GlMinor = 0;
constexpr const char* GlslVersion = "#version 130";
#endif

constexpr int AudioRate = 48000;
constexpr double SnesHz = 60.098;
constexpr int MinLatencyMs = 16;
constexpr int MaxLatencyMs = 200;
constexpr int MaxRecent = 8;
constexpr int MinFontSize = 8;
constexpr int MaxFontSize = 32;
constexpr int DefaultFontSize = 13;
constexpr uint64_t MessageMs = 6000;
constexpr const char* AppName = "bsnes heat-fe";
// imgui's own dark-theme blue, packed as 0xRRGGBB
constexpr int DefaultAccent = 0x4296fa;
// percent, fed to the rasterizer; 100 is the font's own weight
constexpr int MinFontWeight = 50;
constexpr int MaxFontWeight = 500;
constexpr int DefaultFontWeight = 100;
// a text colour of -1 means follow the theme instead
constexpr int FollowTheme = -1;

ImVec4 unpackColor(int rgb) {
  return ImVec4(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f,
                (rgb & 0xff) / 255.0f, 1.0f);
}

int packColor(const float rgb[3]) {
  return (int)(rgb[0] * 255.0f + 0.5f) << 16
       | (int)(rgb[1] * 255.0f + 0.5f) << 8
       | (int)(rgb[2] * 255.0f + 0.5f);
}

enum Hotkey { HkPause, HkReset, HkFastForward, HkFullscreen, HkScreenshot, HotkeyCount };

const char* const HotkeyNames[HotkeyCount] = {
  "Pause", "Reset", "Fast Forward", "Fullscreen", "Screenshot"
};

constexpr SDL_Scancode DefaultHotkeys[HotkeyCount] = {
  SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
  SDL_SCANCODE_F11, SDL_SCANCODE_F12
};

const char* const RomExts[] = {"sfc", "smc", "fig", "swc", "bs", "st", "gb", "gbc"};

bool isRom(const std::string& name) {
  const size_t dot = name.find_last_of('.');
  if(dot == std::string::npos) return false;

  std::string ext = name.substr(dot + 1);
  for(char& c : ext) c = (char)SDL_tolower((unsigned char)c);
  for(const char* candidate : RomExts) {
    if(ext == candidate) return true;
  }
  return false;
}

const char* romFilterPattern() {
  static const std::string pattern = [] {
    std::string out;
    for(const char* ext : RomExts) {
      if(!out.empty()) out += ';';
      out += ext;
    }
    return out;
  }();
  return pattern.c_str();
}

// SDL dialogs are async and may call back off the main thread
struct FilePick {
  SDL_Mutex* mutex = SDL_CreateMutex();
  std::string path;
  bool ready = false;
  bool open = false;

  ~FilePick() { SDL_DestroyMutex(mutex); }
};

void SDLCALL onPicked(void* userdata, const char* const* filelist, int) {
  auto* pick = (FilePick*)userdata;
  Guard guard(pick->mutex);
  pick->open = false;
  if(filelist && filelist[0]) {
    pick->path = filelist[0];
    pick->ready = true;
  }
}

std::string takePick(FilePick& pick) {
  Guard guard(pick.mutex);
  if(!pick.ready) return {};
  pick.ready = false;
  return pick.path;
}

// A settings.cfg next to the exe switches to portable mode: everything is read
// and written there instead of the user profile.
const std::string& configDir() {
  static const std::string dir = [] {
    if(const char* base = SDL_GetBasePath()) {
      if(pathExists(std::string(base) + "settings.cfg")) return std::string(base);
    }
    char* pref = SDL_GetPrefPath("bsnes-netplay", "imgui");
    std::string path = pref ? pref : "";
    if(pref) SDL_free(pref);
    return path;
  }();
  return dir;
}

bool portableMode() {
  static const bool portable = [] {
    const char* base = SDL_GetBasePath();
    return base && configDir() == base;
  }();
  return portable;
}

std::string prefFile(const char* name) { return configDir() + name; }

std::string readText(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  if(!data) return {};
  std::string text((const char*)data, size);
  SDL_free(data);
  return text;
}

void writeText(const std::string& path, const std::string& text) {
  SDL_SaveFile(path.c_str(), text.data(), text.size());
}

struct Settings {
  int latencyMs = 48;
  int volume = 100;
  bool mute = false;
  bool aspectCorrect = true;
  bool integerScale = false;  // default is a fractional fit that fills the window
  bool linearFilter = false;
  int windowScale = 0;  // 0 = fit to window
  bool pauseUnfocused = true;
  int fastForwardSpeed = 4;
  bool showStatus = true;
  int theme = 0;  // 0 dark, 1 light, 2 classic
  int accent = DefaultAccent;
  int textColor = FollowTheme;
  int fontSize = DefaultFontSize;
  int fontWeight = DefaultFontWeight;
  std::string fontPath;  // empty means imgui's built-in font
  std::string gamesDir;
  std::string shotsDir;
  std::vector<std::string> recent;
  int hotkeys[HotkeyCount] = {};
  int devices[EmuCore::PortCount] = {EmuCore::Gamepad, EmuCore::Gamepad};

  Settings() {
    for(int i = 0; i < HotkeyCount; i++) hotkeys[i] = (int)DefaultHotkeys[i];
  }

  void load(const std::string& path);
  void save(const std::string& path) const;
  void addRecent(const std::string& rom);
};

void Settings::load(const std::string& path) {
  const std::string text = readText(path);
  size_t pos = 0;
  while(pos < text.size()) {
    size_t end = text.find('\n', pos);
    if(end == std::string::npos) end = text.size();
    std::string line = text.substr(pos, end - pos);
    pos = end + 1;
    while(!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();

    const size_t space = line.find(' ');
    if(space == std::string::npos) continue;
    const std::string key = line.substr(0, space), value = line.substr(space + 1);
    const int number = SDL_atoi(value.c_str());

    if(key == "latency") latencyMs = std::clamp(number, MinLatencyMs, MaxLatencyMs);
    else if(key == "volume") volume = std::clamp(number, 0, 200);
    else if(key == "mute") mute = number != 0;
    else if(key == "aspect") aspectCorrect = number != 0;
    else if(key == "integer") integerScale = number != 0;
    else if(key == "linear") linearFilter = number != 0;
    else if(key == "windowscale") windowScale = std::clamp(number, 0, 5);
    else if(key == "pauseunfocused") pauseUnfocused = number != 0;
    else if(key == "ffspeed") fastForwardSpeed = std::clamp(number, 2, 16);
    else if(key == "showstatus") showStatus = number != 0;
    else if(key == "theme") theme = std::clamp(number, 0, 2);
    else if(key == "accent") accent = std::clamp(number, 0, 0xffffff);
    else if(key == "textcolor") textColor = std::clamp(number, FollowTheme, 0xffffff);
    else if(key == "fontsize") fontSize = std::clamp(number, MinFontSize, MaxFontSize);
    else if(key == "fontweight") fontWeight = std::clamp(number, MinFontWeight, MaxFontWeight);
    else if(key == "font") fontPath = value;
    else if(key == "gamesdir") gamesDir = value;
    else if(key == "shotsdir") shotsDir = value;
    else if(key == "recent") { if(recent.size() < MaxRecent) recent.push_back(value); }
    else {
      auto indexed = [&](const char* prefix, int* slots, int count) {
        if(key.rfind(prefix, 0) != 0) return false;
        const int index = SDL_atoi(key.c_str() + SDL_strlen(prefix));
        if(index >= 0 && index < count) slots[index] = number;
        return true;
      };
      indexed("device", devices, EmuCore::PortCount)
          || indexed("hotkey", hotkeys, HotkeyCount);
    }
  }
}

void Settings::save(const std::string& path) const {
  std::string text;
  auto add = [&](const char* key, const std::string& value) {
    text += key; text += ' '; text += value; text += '\n';
  };
  auto addInt = [&](const char* key, int value) { add(key, std::to_string(value)); };

  addInt("latency", latencyMs);
  addInt("volume", volume);
  addInt("mute", mute);
  addInt("aspect", aspectCorrect);
  addInt("integer", integerScale);
  addInt("linear", linearFilter);
  addInt("windowscale", windowScale);
  addInt("pauseunfocused", pauseUnfocused);
  addInt("ffspeed", fastForwardSpeed);
  addInt("showstatus", showStatus);
  addInt("theme", theme);
  addInt("accent", accent);
  addInt("textcolor", textColor);
  addInt("fontsize", fontSize);
  addInt("fontweight", fontWeight);
  add("font", fontPath);
  add("gamesdir", gamesDir);
  add("shotsdir", shotsDir);
  for(int i = 0; i < EmuCore::PortCount; i++) addInt(("device" + std::to_string(i)).c_str(), devices[i]);
  for(int i = 0; i < HotkeyCount; i++) addInt(("hotkey" + std::to_string(i)).c_str(), hotkeys[i]);
  for(const std::string& rom : recent) add("recent", rom);

  writeText(path, text);
}

void Settings::addRecent(const std::string& rom) {
  recent.erase(std::remove(recent.begin(), recent.end(), rom), recent.end());
  recent.insert(recent.begin(), rom);
  if(recent.size() > MaxRecent) recent.resize(MaxRecent);
}

struct Shell {
  SDL_Window* window = nullptr;
  SDL_GLContext gl = nullptr;
  GLuint texture = 0;
  SDL_AudioStream* audio = nullptr;

  std::vector<float> audioBuffer;
  // valid until the core's next frame, which is enough for a screenshot
  const uint32_t* lastPixels = nullptr;
  int frameWidth = 0;
  int frameHeight = 0;
  float audioGain = 1.0f;
  Settings* settings = nullptr;

  bool init();
  void shutdown();
  int paceTarget() const;
  void pace();
  void pushVideo(const uint32_t* argb, int width, int height);
  void pushAudio();
  void clearFrame() { lastPixels = nullptr; frameWidth = frameHeight = 0; }
  void drawGame();
  bool saveFrame(const std::string& path) const;
  bool saveWindow(const std::string& path) const;
};

bool Shell::init() {
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinor);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

  window = SDL_CreateWindow(AppName, 878, 224 * 3 + 24,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
  if(!window) {
    SDL_Log("window creation failed: %s", SDL_GetError());
    return false;
  }

  gl = SDL_GL_CreateContext(window);
  if(!gl) {
    SDL_Log("gl context failed: %s", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(window, gl);
  // audio is the master clock, so vsync must not also gate the loop
  SDL_GL_SetSwapInterval(0);

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, EmuCore::MaxWidth, EmuCore::MaxHeight, 0,
               GL_BGRA, GL_UNSIGNED_BYTE, nullptr);

  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = AudioRate;
  audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
  if(!audio) {
    SDL_Log("audio open failed: %s", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(audio);
  return true;
}

void Shell::shutdown() {
  if(audio) SDL_DestroyAudioStream(audio);
  if(texture) glDeleteTextures(1, &texture);
  if(gl) SDL_GL_DestroyContext(gl);
  if(window) SDL_DestroyWindow(window);
  SDL_Quit();
}

// Block until the device drains below the configured backlog. This paces the
// whole loop to real time: the SNES runs at 60.098Hz, not the display's rate.
int Shell::paceTarget() const {
  return settings->latencyMs * AudioRate / 1000 * 2 * (int)sizeof(float);
}

void Shell::pace() {
  while(SDL_GetAudioStreamQueued(audio) > paceTarget()) SDL_Delay(1);
}

void Shell::pushVideo(const uint32_t* argb, int width, int height) {
  frameWidth = width;
  frameHeight = height;
  lastPixels = argb;

  glBindTexture(GL_TEXTURE_2D, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, argb);
}

void Shell::pushAudio() {
  if(audioBuffer.empty()) return;
  const float gain = settings->mute ? 0.0f : settings->volume / 100.0f;
  if(gain != audioGain) {
    audioGain = gain;
    SDL_SetAudioStreamGain(audio, gain);
  }
  SDL_PutAudioStreamData(audio, audioBuffer.data(), (int)(audioBuffer.size() * sizeof(float)));
}

// Integer-scaled and centred in the viewport work area, which is what is left
// under the main menu bar. Drawn through imgui's background list.
void Shell::drawGame() {
  if(frameWidth <= 0 || frameHeight <= 0) return;

  const ImGuiViewport* view = ImGui::GetMainViewport();
  const float availW = view->WorkSize.x, availH = view->WorkSize.y;
  if(availW <= 0.0f || availH <= 0.0f) return;

  // NTSC pixels are not square; 8/7 stretches 256x224 out to 4:3
  const float aspect = settings->aspectCorrect ? 8.0f / 7.0f : 1.0f;

  float scale;
  if(settings->windowScale > 0) {
    scale = (float)settings->windowScale;
  } else {
    const float fit = std::min(availW / (frameWidth * aspect), availH / (float)frameHeight);
    scale = settings->integerScale ? SDL_max(1.0f, SDL_floorf(fit)) : fit;
  }
  const float w = frameWidth * scale * aspect;
  const float h = frameHeight * scale;

  const GLint filter = settings->linearFilter ? GL_LINEAR : GL_NEAREST;
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

  const ImVec2 p0(view->WorkPos.x + (availW - w) / 2.0f, view->WorkPos.y + (availH - h) / 2.0f);
  const ImVec2 p1(p0.x + w, p0.y + h);
  const ImVec2 uv1((float)frameWidth / EmuCore::MaxWidth, (float)frameHeight / EmuCore::MaxHeight);
  ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)texture, p0, p1, ImVec2(0, 0), uv1);
}

bool saveBmp(const void* pixels, int w, int h, const std::string& path, bool flip) {
  SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888,
                                               (void*)pixels, w * (int)sizeof(uint32_t));
  if(!surface) return false;
  const bool ok = (!flip || SDL_FlipSurface(surface, SDL_FLIP_VERTICAL))
               && SDL_SaveBMP(surface, path.c_str());
  SDL_DestroySurface(surface);
  return ok;
}

bool Shell::saveFrame(const std::string& path) const {
  if(!lastPixels || frameWidth <= 0 || frameHeight <= 0) return false;
  return saveBmp(lastPixels, frameWidth, frameHeight, path, false);
}

// GL's origin is bottom-left, so the rows come back upside down
bool Shell::saveWindow(const std::string& path) const {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  if(w <= 0 || h <= 0) return false;

  std::vector<uint32_t> pixels((size_t)w * h);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());

  return saveBmp(pixels.data(), w, h, path, true);
}

struct App {
  Shell shell;
  Settings settings;
  InputMap input;
  EmuCore core;

  std::string inputCfg, settingsCfg;
  std::vector<SDL_Gamepad*> pads;
  std::vector<std::pair<std::string, std::string>> games;  // label, path
  std::string status;
  uint64_t messageTime = 0;
  std::string gameTitle;

  FilePick romPick, dirPick, shotDirPick, fontPick;

  bool running = true;
  bool fontDirty = false;
  bool paused = false;
  bool fastForward = false;
  bool showSettings = false;
  bool showTools = false;
  bool showGames = false;
  bool showAbout = false;
  int settingsTab = -1;
  int mapPort = 0;
  int capturing = -1;      // emulator button slot being rebound
  int capturingHotkey = -1;
  int gameSelected = 0;
  double fps = 0.0;
  long long totalSamples = 0;

  void scanGames();
  bool loadRom(const std::string& path);
  void unloadRom();
  void applySpeed() { core.setSpeedScale(fastForward ? 1.0 / settings.fastForwardSpeed : 1.0); }
  void toggleFastForward() { fastForward = !fastForward; applySpeed(); }
  void reset() { core.reset(); paused = false; }
  void powerCycle() { core.power(); paused = false; }

  bool fullscreen() const {
    return (SDL_GetWindowFlags(shell.window) & SDL_WINDOW_FULLSCREEN) != 0;
  }
  void toggleFullscreen() { SDL_SetWindowFullscreen(shell.window, !fullscreen()); }
  void openRomDialog();
  void openFolderDialog(FilePick& pick);
  void openFontDialog();
  void takeScreenshot();

  ImVec4 accentColor() const { return unpackColor(settings.accent); }
  void applyTheme();
  void applyFont();

  void showMessage(const std::string& text) {
    status = text;
    messageTime = SDL_GetTicks();
    SDL_Log("%s", text.c_str());
  }

  void drawMenuBar();
  void drawStatusBar();
  void drawSettingsWindow();
  void drawToolsWindow();
  void drawCustomizationTab();
  void drawGamesList();
  void drawGamesWindow();
  void drawGamesHome();
  void drawAboutWindow();
  void drawUi();
};

void App::scanGames() {
  games.clear();
  if(settings.gamesDir.empty()) return;

  // SDL_GlobDirectory walks the whole tree and hands back '/' separated
  // paths relative to the root
  int count = 0;
  char** found = SDL_GlobDirectory(settings.gamesDir.c_str(), nullptr, 0, &count);
  if(!found) return;

  for(int i = 0; i < count; i++) {
    const std::string relative = found[i];
    if(!isRom(relative)) continue;
    games.emplace_back(fileStem(relative), settings.gamesDir + "/" + relative);
  }
  SDL_free(found);
  std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
    return std::lexicographical_compare(
        a.first.begin(), a.first.end(), b.first.begin(), b.first.end(),
        [](unsigned char x, unsigned char y) { return SDL_tolower(x) < SDL_tolower(y); });
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

void App::openRomDialog() {
  Guard guard(romPick.mutex);
  if(romPick.open) return;
  romPick.open = true;
  const SDL_DialogFileFilter filters[] = {
    {"SNES ROMs", romFilterPattern()},
    {"All files", "*"},
  };
  SDL_ShowOpenFileDialog(onPicked, &romPick, shell.window, filters, 2,
                         settings.gamesDir.empty() ? nullptr : settings.gamesDir.c_str(), false);
}

void App::openFolderDialog(FilePick& pick) {
  Guard guard(pick.mutex);
  if(pick.open) return;
  pick.open = true;
  SDL_ShowOpenFolderDialog(onPicked, &pick, shell.window,
                           settings.gamesDir.empty() ? nullptr : settings.gamesDir.c_str(), false);
}

void App::openFontDialog() {
  Guard guard(fontPick.mutex);
  if(fontPick.open) return;
  fontPick.open = true;
  const SDL_DialogFileFilter filters[] = {
    {"Fonts", "ttf;otf;ttc"},
    {"All files", "*"},
  };
  SDL_ShowOpenFileDialog(onPicked, &fontPick, shell.window, filters, 2, nullptr, false);
}

void App::applyTheme() {
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

  // the accent drives everything interactive; the greys come from the preset
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

// Only safe between frames: rebuilding the atlas invalidates the font texture.
void App::applyFont() {
  fontDirty = false;

  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();

  ImFontConfig config;
  config.SizePixels = (float)settings.fontSize;
  // there is no real bold without a second font file; this thickens the glyphs
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

  const std::string name = dir + "shot-" + std::to_string(SDL_GetTicks()) + ".bmp";
  showMessage(shell.saveFrame(name) ? "saved " + name : "screenshot failed");
}

void App::drawMenuBar() {
  if(!ImGui::BeginMainMenuBar()) return;

  if(ImGui::BeginMenu("File")) {
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
    if(ImGui::MenuItem("Quit", "Alt+F4")) running = false;
    ImGui::EndMenu();
  }

  if(ImGui::BeginMenu("Emulation")) {
    if(ImGui::MenuItem("Pause", nullptr, paused, core.loaded())) paused = !paused;
    if(ImGui::MenuItem("Fast Forward", nullptr, fastForward, core.loaded())) toggleFastForward();
    ImGui::Separator();
    if(ImGui::MenuItem("Reset", nullptr, false, core.loaded())) reset();
    if(ImGui::MenuItem("Power Cycle", nullptr, false, core.loaded())) powerCycle();
    ImGui::EndMenu();
  }

  if(ImGui::BeginMenu("Settings")) {
    static const char* tabs[] = {"Video", "Audio", "Input", "Hotkeys", "Emulator",
                                 "Customization", "Paths"};
    for(int i = 0; i < IM_ARRAYSIZE(tabs); i++) {
      if(ImGui::MenuItem(tabs[i])) { showSettings = true; settingsTab = i; }
    }
    ImGui::Separator();
    ImGui::MenuItem("Show Status Bar", nullptr, &settings.showStatus);
    ImGui::EndMenu();
  }

  if(ImGui::BeginMenu("Tools")) {
    if(ImGui::MenuItem("Diagnostics", nullptr, showTools)) showTools = !showTools;
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

// Placed outside the host viewport so multi-viewport gives it a real OS window.
void placeFloating(float offsetX, float offsetY, float w, float h) {
  const ImGuiViewport* view = ImGui::GetMainViewport();
  const bool detach = (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
  const float x = detach ? view->Pos.x + view->Size.x + 12.0f : view->WorkPos.x + offsetX;
  const float y = detach ? view->Pos.y + offsetY : view->WorkPos.y + offsetY;
  ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_FirstUseEver);
}

void App::drawSettingsWindow() {
  if(!showSettings) return;

  placeFloating(40.0f, 40.0f, 520.0f, 440.0f);
  if(!ImGui::Begin("Settings", &showSettings)) { ImGui::End(); return; }

  if(ImGui::BeginTabBar("settingstabs")) {
    auto tabFlags = [&](int index) -> ImGuiTabItemFlags {
      return settingsTab == index ? ImGuiTabItemFlags_SetSelected : 0;
    };

    if(ImGui::BeginTabItem("Video", nullptr, tabFlags(0))) {
      bool dirty = false;
      dirty |= ImGui::Checkbox("Aspect correction (8:7)", &settings.aspectCorrect);
      dirty |= ImGui::Checkbox("Integer scaling (crisp, leaves borders)", &settings.integerScale);
      dirty |= ImGui::Checkbox("Linear filtering (smooths fractional scales)", &settings.linearFilter);

      static const char* scales[] = {"Fit window", "1x", "2x", "3x", "4x", "5x"};
      dirty |= ImGui::Combo("Window scale", &settings.windowScale, scales, IM_ARRAYSIZE(scales));

      ImGui::Separator();
      ImGui::Text("Output: %d x %d", shell.frameWidth, shell.frameHeight);
      if(dirty) settings.save(settingsCfg);
      ImGui::EndTabItem();
    }

    if(ImGui::BeginTabItem("Audio", nullptr, tabFlags(1))) {
      bool dirty = false;
      // this is the pacing target, not a cosmetic number. saving on release
      // only, or a drag rewrites the file every frame
      ImGui::SliderInt("Latency (ms)", &settings.latencyMs, MinLatencyMs, MaxLatencyMs);
      dirty |= ImGui::IsItemDeactivatedAfterEdit();
      ImGui::SliderInt("Volume (%)", &settings.volume, 0, 200);
      dirty |= ImGui::IsItemDeactivatedAfterEdit();
      dirty |= ImGui::Checkbox("Mute", &settings.mute);
      ImGui::Separator();
      ImGui::Text("Frequency: %d Hz", AudioRate);
      ImGui::Text("Queued: %d bytes", (int)SDL_GetAudioStreamQueued(shell.audio));
      ImGui::TextWrapped("Audio is the master clock. Latency sets the backlog the loop drains to,"
                         " which is what paces the emulator to 60.098Hz.");
      if(dirty) settings.save(settingsCfg);
      ImGui::EndTabItem();
    }

    if(ImGui::BeginTabItem("Input", nullptr, tabFlags(2))) {
      ImGui::Combo("Port", &mapPort, "Port 1\0Port 2\0");

      // the core decides which accessories each port accepts
      const std::vector<EmuCore::DeviceInfo> portDevices = core.devices(mapPort);
      int deviceIndex = 0;
      for(int i = 0; i < (int)portDevices.size(); i++) {
        if(portDevices[i].id == core.connectedDevice(mapPort)) deviceIndex = i;
      }
      std::string deviceItems;
      for(const auto& info : portDevices) { deviceItems += info.name; deviceItems += '\0'; }

      if(ImGui::Combo("Device", &deviceIndex, deviceItems.c_str())
      && deviceIndex < (int)portDevices.size()) {
        core.connect(mapPort, portDevices[deviceIndex].id);
        settings.devices[mapPort] = portDevices[deviceIndex].id;
        settings.save(settingsCfg);
        capturing = -1;
      }

      const int device = core.connectedDevice(mapPort);
      const std::vector<EmuCore::InputInfo> deviceInputs = core.inputs(device);

      ImGui::TextUnformatted(capturing >= 0 ? "press a key or pad button, esc to cancel"
                                            : "click a binding to rebind it");
      ImGui::Separator();

      // stretch the two binding columns evenly; sizing them by content lets the
      // long pad labels swallow the row, and a fixed name column clips the
      // multitap's "Port 2 - Select" or any larger font
      if(ImGui::BeginTable("bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Keyboard");
        ImGui::TableSetupColumn("Gamepad");
        ImGui::TableHeadersRow();

        for(int b = 0; b < (int)deviceInputs.size(); b++) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(deviceInputs[b].name.c_str());

          // pointer and analog inputs have no key or button to bind
          const bool analog = deviceInputs[b].type == EmuCore::Axis
                           || deviceInputs[b].type == EmuCore::Rumble;

          for(int slot = 0; slot < InputMap::Slots; slot++) {
            ImGui::TableNextColumn();
            if(analog) { ImGui::TextDisabled("n/a"); continue; }

            const int id = b * InputMap::Slots + slot;
            ImGui::PushID(id);
            const std::string label = capturing == id
                ? "..." : input.binding(mapPort, device, b, slot).label();
            if(ImGui::Button(label.c_str(), ImVec2(-1.0f, 0.0f))) capturing = id;
            ImGui::PopID();
          }
        }
        ImGui::EndTable();
      }

      ImGui::Separator();
      if(ImGui::Button("Restore defaults")) {
        input.loadDefaults();
        input.save(inputCfg);
      }
      ImGui::SameLine();
      ImGui::Text("%d gamepad(s)", (int)pads.size());
      ImGui::EndTabItem();
    }

    if(ImGui::BeginTabItem("Hotkeys", nullptr, tabFlags(3))) {
      ImGui::TextUnformatted(capturingHotkey >= 0 ? "press a key, esc to cancel"
                                                  : "click a hotkey to rebind it");
      ImGui::Separator();

      if(ImGui::BeginTable("hotkeys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Key");
        ImGui::TableHeadersRow();

        for(int i = 0; i < HotkeyCount; i++) {
          ImGui::TableNextRow();
          ImGui::TableNextColumn();
          ImGui::TextUnformatted(HotkeyNames[i]);
          ImGui::TableNextColumn();
          ImGui::PushID(1000 + i);
          const char* name = SDL_GetScancodeName((SDL_Scancode)settings.hotkeys[i]);
          const char* label = capturingHotkey == i ? "..." : (name && *name ? name : "unbound");
          if(ImGui::Button(label, ImVec2(-1.0f, 0.0f))) capturingHotkey = i;
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if(ImGui::BeginTabItem("Emulator", nullptr, tabFlags(4))) {
      bool dirty = false;
      dirty |= ImGui::Checkbox("Pause when window is unfocused", &settings.pauseUnfocused);
      if(ImGui::SliderInt("Fast forward speed", &settings.fastForwardSpeed, 2, 16, "%dx")) applySpeed();
      dirty |= ImGui::IsItemDeactivatedAfterEdit();
      dirty |= ImGui::Checkbox("Show status bar", &settings.showStatus);
      ImGui::Separator();
      ImGui::TextWrapped("Save RAM is written next to the ROM file. Game Boy titles run only"
                         " through Super Game Boy, which needs the SGB BIOS cartridge.");
      if(dirty) settings.save(settingsCfg);
      ImGui::EndTabItem();
    }

    if(ImGui::BeginTabItem("Customization", nullptr, tabFlags(5))) {
      drawCustomizationTab();
      ImGui::EndTabItem();
    }

    if(ImGui::BeginTabItem("Paths", nullptr, tabFlags(6))) {
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
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  settingsTab = -1;
  ImGui::End();
}

void App::drawCustomizationTab() {
  bool dirty = false;

  static const char* themes[] = {"Dark", "Light", "Classic"};
  if(ImGui::Combo("Theme", &settings.theme, themes, IM_ARRAYSIZE(themes))) {
    applyTheme();
    dirty = true;
  }

  const ImVec4 current = accentColor();
  float accent[3] = {current.x, current.y, current.z};
  if(ImGui::ColorEdit3("Accent", accent, ImGuiColorEditFlags_NoInputs)) {
    settings.accent = packColor(accent);
    applyTheme();
  }
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::TextUnformatted("buttons, tabs, sliders and selections");

  bool follow = settings.textColor == FollowTheme;
  if(ImGui::Checkbox("Text colour follows theme", &follow)) {
    const ImVec4& text = ImGui::GetStyle().Colors[ImGuiCol_Text];
    const float rgb[3] = {text.x, text.y, text.z};
    settings.textColor = follow ? FollowTheme : packColor(rgb);
    applyTheme();
    dirty = true;
  }
  if(!follow) {
    const ImVec4 chosen = unpackColor(settings.textColor);
    float text[3] = {chosen.x, chosen.y, chosen.z};
    if(ImGui::ColorEdit3("Text", text, ImGuiColorEditFlags_NoInputs)) {
      settings.textColor = packColor(text);
      applyTheme();
    }
    dirty |= ImGui::IsItemDeactivatedAfterEdit();
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Font");
  ImGui::TextWrapped("%s", settings.fontPath.empty() ? "(built-in)" : settings.fontPath.c_str());
  if(ImGui::Button("Browse##font")) openFontDialog();
  ImGui::SameLine();
  if(ImGui::Button("Use built-in")) {
    settings.fontPath.clear();
    fontDirty = dirty = true;
  }

  ImGui::SliderInt("Size", &settings.fontSize, MinFontSize, MaxFontSize, "%dpx");
  if(ImGui::IsItemDeactivatedAfterEdit()) fontDirty = dirty = true;

  ImGui::SliderInt("Weight", &settings.fontWeight, MinFontWeight, MaxFontWeight, "%d%%");
  if(ImGui::IsItemDeactivatedAfterEdit()) fontDirty = dirty = true;

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##look")) {
    settings.theme = 0;
    settings.accent = DefaultAccent;
    settings.textColor = FollowTheme;
    settings.fontSize = DefaultFontSize;
    settings.fontWeight = DefaultFontWeight;
    settings.fontPath.clear();
    applyTheme();
    fontDirty = dirty = true;
  }
  ImGui::TextWrapped("The font file is remembered by path. If it moves, the built-in"
                     " font is used instead. Weight thickens the glyphs rather than"
                     " switching to a bold face; for real bold, pick a bold font file.");

  if(dirty) settings.save(settingsCfg);
}

void App::drawToolsWindow() {
  if(!showTools) return;

  placeFloating(60.0f, 480.0f, 380.0f, 250.0f);
  if(!ImGui::Begin("Diagnostics", &showTools)) { ImGui::End(); return; }

  ImGui::Text("Game: %s", core.loaded() ? gameTitle.c_str() : "none");
  ImGui::Text("Resolution: %d x %d", shell.frameWidth, shell.frameHeight);
  ImGui::Text("Frame rate: %.1f fps (target %.3f)", fps, SnesHz);
  ImGui::Text("Audio queued: %d", (int)SDL_GetAudioStreamQueued(shell.audio));
  ImGui::Text("Pacing target: %d", shell.paceTarget());
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

// With no game loaded the viewport would just be black, so the library fills it.
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

void App::drawUi() {
  // fullscreen is the game only; the hotkey brings the chrome back
  if(!fullscreen()) {
    drawMenuBar();
    drawStatusBar();
  }

  if(core.loaded()) {
    shell.drawGame();
    drawGamesWindow();
  } else {
    drawGamesHome();
  }

  drawSettingsWindow();
  drawToolsWindow();
  drawAboutWindow();
}

}  // namespace

int main(int argc, char** argv) {
  attachParentConsole();

  int frameLimit = 0;
  bool fast = false;
  std::string uiShot;
  std::string uiScreen = "game";
  bool uiFullscreen = false;
  std::string romPath;
  int shotTab = -1;
  int shotW = 0, shotH = 0;

  for(int i = 1; i < argc; i++) {
    if(SDL_strcmp(argv[i], "--fast") == 0) { fast = true; continue; }
    if(SDL_strcmp(argv[i], "--frames") == 0 && i + 1 < argc) { frameLimit = SDL_atoi(argv[++i]); continue; }
    if(SDL_strcmp(argv[i], "--ui-shot") == 0 && i + 1 < argc) { uiShot = argv[++i]; continue; }
    if(SDL_strcmp(argv[i], "--ui-screen") == 0 && i + 1 < argc) { uiScreen = argv[++i]; continue; }
    if(SDL_strcmp(argv[i], "--ui-tab") == 0 && i + 1 < argc) { shotTab = SDL_atoi(argv[++i]); continue; }
    if(SDL_strcmp(argv[i], "--ui-fullscreen") == 0) { uiFullscreen = true; continue; }
    if(SDL_strcmp(argv[i], "--ui-size") == 0 && i + 2 < argc) {
      shotW = SDL_atoi(argv[i + 1]);
      shotH = SDL_atoi(argv[i + 2]);
      i += 2;
      continue;
    }
    if(argv[i][0] != '-' && romPath.empty()) romPath = argv[i];
  }

  App app;
  app.shell.settings = &app.settings;
  if(!app.shell.init()) {
    app.shell.shutdown();
    return 1;
  }

  app.inputCfg = prefFile("input.cfg");
  app.settingsCfg = prefFile("settings.cfg");
  app.input.load(app.inputCfg);
  app.settings.load(app.settingsCfg);
  for(int port = 0; port < EmuCore::PortCount; port++) {
    app.core.connect(port, app.settings.devices[port]);
  }
  app.scanGames();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  // real OS windows for the panels; a screenshot can only read the main
  // framebuffer, so shots keep everything merged into the host window
  if(uiShot.empty()) {
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = true;
  }

  app.applyTheme();

  ImGui_ImplSDL3_InitForOpenGL(app.shell.window, app.shell.gl);
  ImGui_ImplOpenGL3_Init(GlslVersion);
  app.fontDirty = true;

  {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for(int i = 0; ids && i < count; i++) {
      if(SDL_Gamepad* pad = SDL_OpenGamepad(ids[i])) app.pads.push_back(pad);
    }
    if(ids) SDL_free(ids);
  }

  app.core.setAudioFrequency(AudioRate);
  app.core.onVideo = [&](const uint32_t* argb, int width, int height) {
    app.shell.pushVideo(argb, width, height);
  };
  app.core.onAudio = [&](const float* frame, int frames) {
    app.shell.audioBuffer.insert(app.shell.audioBuffer.end(), frame, frame + frames * 2);
    app.totalSamples += frames;
  };

  if(!romPath.empty()) app.loadRom(romPath);

  if(frameLimit && uiShot.empty() && !app.core.loaded()) {
    SDL_Log("no rom loaded");
    app.shell.shutdown();
    return 1;
  }

  auto renderUi = [&]() {
    if(app.fontDirty) app.applyFont();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    app.drawUi();
    ImGui::Render();

    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(app.shell.window, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  };

  app.showSettings = uiScreen == "settings";
  app.showTools = uiScreen == "tools";
  app.showGames = uiScreen == "games";
  app.showAbout = uiScreen == "about";
  app.settingsTab = shotTab;

  if(!uiShot.empty()) {
    if(shotW > 0 && shotH > 0) SDL_SetWindowSize(app.shell.window, shotW, shotH);
    if(uiFullscreen) app.toggleFullscreen();

    // warm the emulator so the shot shows the UI over a real frame
    for(int i = 0; app.core.loaded() && i < 180; i++) {
      app.shell.audioBuffer.clear();
      app.core.runFrame();
    }

    // early passes settle window sizing and the viewport work area; --frames
    // asks for more, which is how layout that drifts per frame shows up
    const int passes = frameLimit > 0 ? frameLimit : 3;
    for(int pass = 0; pass < passes; pass++) {
      app.settingsTab = pass == 0 ? shotTab : -1;
      renderUi();
    }

    // read before swapping, or the back buffer is already gone
    const bool ok = app.shell.saveWindow(uiShot);
    SDL_Log("ui shot %s: %s", ok ? "saved" : "FAILED", uiShot.c_str());

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    app.core.unload();
    app.shell.shutdown();
    return ok ? 0 : 1;
  }

  int frames = 0;
  uint64_t fpsMark = SDL_GetTicksNS();
  int fpsFrames = 0;
  uint64_t ffMark = SDL_GetTicksNS();
  bool inFrame = false;

  // `modal` means we were called from the event watch during an OS window
  // drag or resize, where frame timing is irregular and audio would stutter.
  auto stepFrame = [&](bool modal) {
    if(inFrame) return;  // the event watch can fire while we are already drawing
    inFrame = true;

    const bool focused = (SDL_GetWindowFlags(app.shell.window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool stopped = !app.core.loaded() || app.paused
                      || (app.settings.pauseUnfocused && !focused && !frameLimit);

    auto advance = [&]() {
      if(io.WantCaptureKeyboard) {
        for(int port = 0; port < InputMap::Ports; port++) {
          for(int b = 0; b < EmuCore::MaxInputs; b++) app.core.setInput(port, b, 0);
        }
      } else {
        app.input.apply(app.core, app.pads);
      }

      app.shell.audioBuffer.clear();
      app.core.runFrame();
      frames++;
      fpsFrames++;
    };

    // One clock for every caller. During an OS drag the event watch and the
    // main loop can both land here, so a second wall-clock path would stack
    // frames on top of these and run the game fast.
    if(!stopped) {
      if(!fast) app.shell.pace();
      ffMark = SDL_GetTicksNS();

      advance();
      app.shell.pushAudio();
    } else if(!modal) {
      SDL_Delay(8);  // no audio to pace against while stopped
    }

    const uint64_t now = SDL_GetTicksNS();
    if(now - fpsMark >= 500000000ull) {
      app.fps = fpsFrames * 1e9 / (now - fpsMark);
      fpsMark = now;
      fpsFrames = 0;
    }

    renderUi();

    if(io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      SDL_GL_MakeCurrent(app.shell.window, app.shell.gl);  // popups leave their context current
    }
    SDL_GL_SwapWindow(app.shell.window);

    inFrame = false;
  };

  // Windows blocks inside its own message pump while a window is dragged or
  // resized, so SDL_PollEvent never returns. Drive frames from a watch instead.
  std::function<void()> modalStep = [&]() { stepFrame(true); };
  SDL_AddEventWatch([](void* data, SDL_Event* event) -> bool {
    if(event->type == SDL_EVENT_WINDOW_EXPOSED
    || event->type == SDL_EVENT_WINDOW_RESIZED) {
      (*(std::function<void()>*)data)();
    }
    return true;
  }, &modalStep);

  while(app.running) {
    if(frameLimit && frames >= frameLimit) break;

    SDL_Event event;
    while(SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);

      if(event.type == SDL_EVENT_QUIT) app.running = false;
      if(event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
      && event.window.windowID == SDL_GetWindowID(app.shell.window)) app.running = false;

      if(event.type == SDL_EVENT_GAMEPAD_ADDED) {
        if(SDL_Gamepad* pad = SDL_OpenGamepad(event.gdevice.which)) app.pads.push_back(pad);
      }
      if(event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for(size_t i = 0; i < app.pads.size(); i++) {
          if(SDL_GetGamepadID(app.pads[i]) != event.gdevice.which) continue;
          SDL_CloseGamepad(app.pads[i]);
          app.pads.erase(app.pads.begin() + i);
          break;
        }
      }

      // rebinding swallows the next input
      if(app.capturing >= 0) {
        if(event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE) {
          app.capturing = -1;
        } else {
          Binding b;
          if(InputMap::capture(event, b)) {
            app.input.binding(app.mapPort, app.core.connectedDevice(app.mapPort),
                              app.capturing / InputMap::Slots,
                              app.capturing % InputMap::Slots) = b;
            app.input.save(app.inputCfg);
            app.capturing = -1;
          }
        }
        continue;
      }

      if(app.capturingHotkey >= 0) {
        if(event.type == SDL_EVENT_KEY_DOWN) {
          if(event.key.scancode != SDL_SCANCODE_ESCAPE) {
            app.settings.hotkeys[app.capturingHotkey] = (int)event.key.scancode;
            app.settings.save(app.settingsCfg);
          }
          app.capturingHotkey = -1;
        }
        continue;
      }

      if(event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && !io.WantCaptureKeyboard) {
        const int key = (int)event.key.scancode;
        if(event.key.scancode == SDL_SCANCODE_O && (event.key.mod & SDL_KMOD_CTRL)) {
          app.openRomDialog();
        } else if(key == app.settings.hotkeys[HkPause] && app.core.loaded()) {
          app.paused = !app.paused;
        } else if(key == app.settings.hotkeys[HkReset] && app.core.loaded()) {
          app.reset();
        } else if(key == app.settings.hotkeys[HkFastForward]) {
          app.toggleFastForward();
        } else if(key == app.settings.hotkeys[HkScreenshot]) {
          app.takeScreenshot();
        } else if(key == app.settings.hotkeys[HkFullscreen]) {
          app.toggleFullscreen();
        }
      }
    }

    if(std::string picked = takePick(app.romPick); !picked.empty()) app.loadRom(picked);
    if(std::string picked = takePick(app.dirPick); !picked.empty()) {
      app.settings.gamesDir = picked;
      app.settings.save(app.settingsCfg);
      app.scanGames();
    }
    if(std::string picked = takePick(app.shotDirPick); !picked.empty()) {
      app.settings.shotsDir = picked;
      app.settings.save(app.settingsCfg);
    }
    if(std::string picked = takePick(app.fontPick); !picked.empty()) {
      app.settings.fontPath = picked;
      app.settings.save(app.settingsCfg);
      app.fontDirty = true;
    }

    stepFrame(false);
  }

  SDL_Log("platform viewports: %d (1 = host window only)",
          ImGui::GetPlatformIO().Viewports.Size);
  SDL_Log("ran %d frames, last frame %dx%d, %.1f samples/frame (expect %.1f)",
          frames, app.shell.frameWidth, app.shell.frameHeight,
          frames ? (double)app.totalSamples / frames : 0.0, AudioRate / SnesHz);

  app.input.save(app.inputCfg);
  app.settings.save(app.settingsCfg);

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();

  for(SDL_Gamepad* pad : app.pads) SDL_CloseGamepad(pad);
  app.core.unload();
  app.shell.shutdown();
  return 0;
}

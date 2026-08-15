#pragma once

#include "emucore.hpp"
#include "inputmap.hpp"
#include "settings.hpp"
#include "shell.hpp"
#include "util.hpp"

#include "imgui.h"

#include <string>
#include <utility>
#include <vector>


inline ImVec4 unpackColor(int rgb) {
  return ImVec4(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f,
                (rgb & 0xff) / 255.0f, 1.0f);
}

inline int packColor(const float rgb[3]) {
  return (int)(rgb[0] * 255.0f + 0.5f) << 16
       | (int)(rgb[1] * 255.0f + 0.5f) << 8
       | (int)(rgb[2] * 255.0f + 0.5f);
}

struct App;

// shared by the Paths tab and the pick draining, so the two cannot drift
struct BiosSlot {
  const char* label;
  const char* id;
  FilePick App::* pick;
  std::string Settings::* path;
};
extern const BiosSlot BiosSlots[3];

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

  FilePick romPick, dirPick, shotDirPick, savesDirPick, fontPick, pakPick, firmwareDirPick;
  FilePick sgbBiosPick, bsxBiosPick, stBiosPick;
  FilePick sufamiAPick, sufamiBPick;
  std::string sufamiPending;

  bool running = true;
  bool fontDirty = false;
  bool paused = false;
  bool fastForward = false;
  bool frameAdvance = false;  // one-shot: advance a frame, then stay paused
  bool showSettings = false;
  bool showTools = false;
  bool showGames = false;
  bool showAbout = false;
  bool showCartridge = false;
  int settingsTab = -1;
  int mapPort = 0;
  int mapPlayer = 0;  // which of a multitap's controllers is being mapped
  int capturing = -1;      // emulator button slot being rebound
  int capturingHotkey = -1;
  int gameSelected = 0;
  // not persisted: bsnes drops back to normal speed for every game loaded
  int speedIndex = SpeedNormal;
  double fps = 0.0;
  long long totalSamples = 0;
  long long emulatedFrames = 0;  // turbo's clock; must not depend on wall time

  void scanGames();
  // a path, or a Sufami Turbo pair joined by '|', as the recent list stores it
  bool loadRom(const std::string& entry);
  void unloadRom();
  void applySpeed() {
    double scale = SpeedScales[speedIndex];
    if(fastForward) scale /= settings.fastForwardSpeed;
    core.setSpeedScale(scale);
  }
  void setSpeed(int index) {
    speedIndex = SDL_clamp(index, 0, SpeedCount - 1);
    applySpeed();
    showMessage(std::string("speed ") + SpeedNames[speedIndex]);
  }
  void toggleFastForward() { fastForward = !fastForward; applySpeed(); }
  void reset() { core.reset(); paused = false; }
  void powerCycle() { core.power(); paused = false; }
  void advanceOneFrame() { frameAdvance = true; }

  // any window we own, not just the main one: an imgui viewport panel takes
  // focus away from it while staying part of this app
  bool focused() const { return SDL_GetKeyboardFocus() != nullptr; }
  bool fullscreen() const {
    return (SDL_GetWindowFlags(shell.window) & SDL_WINDOW_FULLSCREEN) != 0;
  }
  void toggleFullscreen() { SDL_SetWindowFullscreen(shell.window, !fullscreen()); }
  void openPick(FilePick& pick, const SDL_DialogFileFilter* filters, const char* dir);
  const char* gamesDirOrNull() const;
  SDL_Gamepad* portPad(int port, int player) const {
    return resolvePad(pads, settings.padIndex[padSlot(port, player)]);
  }
  // how many controllers the port's device carries; a multitap carries four
  int portPlayers(int port) const {
    return playerCount((int)core.inputs(core.connectedDevice(port)).size());
  }
  // unset means a Firmware folder beside the config, as bsnes locates its own
  std::string firmwareDir() const {
    return settings.firmwareDir.empty() ? configDir() + "Firmware" : settings.firmwareDir;
  }
  void openRomDialog();
  void openFolderDialog(FilePick& pick) { openPick(pick, nullptr, gamesDirOrNull()); }
  void openFontDialog();
  void openMediaDialog(FilePick& pick, const char* label, const char* extensions);
  void openSufamiPairDialog();
  void takeScreenshot();

  ImVec4 accentColor() const { return unpackColor(settings.accent); }
  void applyPreset();
  void applyAccent();
  void applyTheme();
  void applyFont();

  void showMessage(const std::string& text) {
    status = text;
    messageTime = SDL_GetTicks();
    SDL_Log("%s", text.c_str());
  }

  const char* hotkeyShortcut(Hotkey key) const;
  void drawFileMenu();
  void drawSpeedMenu();
  void drawEmulationMenu();
  void drawSettingsMenu();
  void drawMenuBar();
  void drawStatusBar();
  void drawSettingsWindow();
  void drawGamepadDiagnostics();
  void drawToolsWindow();
  void restoreVideoDefaults();
  void drawVideoTab();
  void restoreAudioDefaults();
  void drawAudioTab();
  void drawDevicePicker();
  void drawControllerPicker();
  void drawBindingTable(int device);
  void restoreInputDefaults();
  void drawInputTab();
  void restoreHotkeyDefaults();
  void drawHotkeysTab();
  void restoreEmulatorDefaults();
  void drawEmulatorTab();
  void restoreEnhancementDefaults();
  void drawEnhancementsTab();
  void drawCartridgeWindow();
  bool drawColourSection();
  bool drawFontSection();
  void restoreAppearanceDefaults();
  void drawCustomizationTab();
  void restorePathDefaults();
  void drawPathsTab();
  void drawGamesList();
  void drawGamesWindow();
  void drawGamesHome();
  void drawAboutWindow();
  void drawUi();
};


#pragma once

#include "emucore/emucore.hpp"
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
  FilePick patchesDirPick, databaseDirPick, statesDirPick;
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
  bool showManifest = false;
  int settingsTab = -1;
  int mapPort = 0;
  int mapPlayer = 0;  // which of a multitap's controllers is being mapped
  int capturing = -1;      // emulator button slot being rebound
  int capturingHotkey = -1;  // hotkey index * HotkeySlots + slot
  int gameSelected = 0;
  int stateSlot = 1;  // 1..StateSlots; a session choice, not a setting
  // undo and redo never outlive the session, so they are held rather than written
  std::vector<uint8_t> undoState, redoState;
  bool confirmRemoveStates = false;
  // not persisted: every game starts at normal speed
  int speedIndex = SpeedNormal;
  double fps = 0.0;
  long long totalSamples = 0;
  // sampled once a frame, or the relative mouse delta gets split in two
  InputSample sample;
  bool mouseCaptured = false;
  bool hotkeyWasHeld[HotkeyCount] = {};
  uint64_t autoSaveMark = 0;
  // a load that still has to be confirmed; the game is in the core but held
  bool unverifiedPrompt = false;
  long long emulatedFrames = 0;  // turbo's clock; must not depend on wall time

  void scanGames();
  // a path, or a Sufami Turbo pair joined by '|', as the recent list stores it
  bool loadRom(const std::string& entry);
  void unloadRom();
  void applySpeed() {
    double scale = SpeedScales[speedIndex];
    // unlimited has no target rate to resample towards
    if(fastForward && !settings.fastForwardUnlimited) scale /= settings.fastForwardSpeed;
    core.setSpeedScale(scale);
    core.setFrameSkip(fastForward ? settings.fastForwardFrameSkip : 0);
  }
  // unlimited fast forward runs with no audio clock driving the loop
  bool unpaced() const { return fastForward && settings.fastForwardUnlimited; }
  bool muted() const {
    return settings.mute || (settings.muteUnfocused && !focused())
        || (fastForward && settings.fastForwardMute);
  }
  // a limited fast forward plays at 65%: decimated audio is harsh
  float audioGain() const {
    if(muted()) return 0.0f;
    const bool limited = fastForward && !settings.fastForwardUnlimited;
    return settings.volume / 100.0f * (limited ? 0.65f : 1.0f);
  }
  void setSpeed(int index) {
    speedIndex = SDL_clamp(index, 0, SpeedCount - 1);
    applySpeed();
    showMessage(std::string("speed ") + SpeedNames[speedIndex]);
  }
  void toggleFastForward() { fastForward = !fastForward; applySpeed(); }
  // skew trims a card that runs fast or slow until the backlog stops drifting
  void applyAudioTuning() {
    core.setAudioFrequency(AudioRate + settings.audioSkew);
    core.setAudioBalance(SDL_clamp((settings.audioBalance - 50) / 50.0, -1.0, 1.0));
  }
  void reset() { core.reset(); paused = false; }
  void powerCycle() { core.power(); paused = false; }
  void advanceOneFrame() { frameAdvance = true; }

  // any window we own, not just the main one: an imgui viewport panel takes
  // focus away from it while staying part of this app
  bool focused() const { return SDL_GetKeyboardFocus() != nullptr; }
  bool fullscreen() const { return shell.fullscreen(); }
  void toggleFullscreen() {
    const bool enter = !fullscreen();
    // fullscreen claims whichever display the window sits on, so move it first
    if(enter) shell.moveToDisplay(settings);
    SDL_SetWindowFullscreen(shell.window, enter);
  }
  void toggleMouseCapture();
  // hover help, suppressed by the tool tips setting
  void tip(const char* text) const {
    if(settings.showToolTips) ImGui::SetItemTooltip("%s", text);
  }
  void openPick(FilePick& pick, const SDL_DialogFileFilter* filters, const char* dir);
  const char* gamesDirOrNull() const;
  SDL_Gamepad* portPad(int port, int player) const {
    return resolvePad(pads, settings.padIndex[padSlot(port, player)]);
  }
  // unset means a Firmware folder beside the config
  std::string firmwareDir() const {
    return settings.firmwareDir.empty() ? configDir() + "Firmware" : settings.firmwareDir;
  }
  // resolving this stats the disk, so readers take the cache below
  std::string databaseDir() const;
  std::string databaseDirCache;
  void refreshDatabaseDir() {
    databaseDirCache = databaseDir();
    core.setDatabaseDirectory(databaseDirCache);
  }
  // each folder row re-pushes its path to the core after a change
  void pushSavesDir() { core.setSavesDirectory(settings.savesDir); }
  void pushPatchesDir() { core.setPatchesDirectory(settings.patchesDir); }
  void pushSerialization() {
    core.setOption("System/Serialization/Method", SerializationNames[settings.serialization]);
  }
  // the Paths row shows this resolved, the way databaseDirCache is shown
  std::string databaseDirShown() const { return databaseDirCache; }
  // the folder a picker for this medium should reopen in
  void rememberDir(const std::string& path);
  void openRomDialog();
  void openFolderDialog(FilePick& pick) { openPick(pick, nullptr, gamesDirOrNull()); }
  void openFontDialog();
  void openMediaDialog(FilePick& pick, const char* label, const char* extensions,
                       EmuCore::Medium medium = EmuCore::Medium::SuperFamicom);
  const char* startDirFor(EmuCore::Medium medium);
  void openSufamiPairDialog();
  void takeScreenshot();
  void saveMemoryTick();

  //states.cpp
  // resolved states folder, and the per-game folder holding the slots
  std::string statesDir() const;
  std::string stateFolder() const;
  std::string statePath(const std::string& name) const;
  // slots are named by number; undo, redo and auto are named outright
  static std::string slotName(int slot);
  static std::string stateLabel(const std::string& name);
  int64_t stateTime(const std::string& name) const;
  bool hasState(const std::string& name) const;
  bool saveState(const std::string& name, bool quiet = false);
  // null unless the slot is one of the memory-resident ones
  std::vector<uint8_t>* memorySlot(const std::string& name);
  const std::vector<uint8_t>* memorySlot(const std::string& name) const;
  bool loadState(const std::string& name);
  bool removeState(const std::string& name);
  void removeAllStates();
  void setStateSlot(int slot);
  void pollHotkeys();
  void triggerHotkey(int index);

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

  std::string hotkeyShortcut(Hotkey key) const;
  void setWindowScale(int scale);
  void drawFileMenu();
  void drawWindowSizeMenu();
  void drawSpeedMenu();
  void drawStateMenu(bool loading);
  void drawRemoveStatesPrompt();
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
  std::string playerLabel(int device, int player) const;
  void drawBindingRow(int device, int input, bool turbo);
  void drawBindingTable(int device);
  void restoreInputDefaults();
  void drawInputTab();
  void restoreHotkeyDefaults();
  void drawHotkeysTab();
  void restoreEmulatorDefaults();
  void drawEmulatorTab();
  // every Hacks/* option in one push; the core caches them, so both tabs and
  // startup have to say the whole set
  void pushEnhancements();
  void restoreEnhancementDefaults();
  void drawEnhancementsTab();
  void restoreCompatibilityDefaults();
  void drawCompatibilityTab();
  // cartridge summary plus every loaded medium's manifest
  void drawManifestWindow();
  void drawUnverifiedPrompt();
  // one definition of "not emulating", for the frame loop and the dimming alike
  bool emulationIdle() const {
    return !core.loaded() || (paused && !frameAdvance) || unverifiedPrompt
        || (settings.defocusPolicy == DefocusPause && !focused());
  }
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


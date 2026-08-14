#pragma once

#include "emucore.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <vector>

constexpr int MinLatencyMs = 16;
constexpr int MaxLatencyMs = 200;
constexpr int MinFontSize = 8;
constexpr int MaxFontSize = 32;
constexpr int DefaultFontSize = 13;
// percent, fed to the rasterizer; 100 is the font's own weight
constexpr int MinFontWeight = 50;
constexpr int MaxFontWeight = 500;
constexpr int DefaultFontWeight = 100;
// imgui's own dark-theme blue, packed as 0xRRGGBB
constexpr int DefaultAccent = 0x4296fa;
// a text colour of -1 means follow the theme instead
constexpr int FollowTheme = -1;

enum Hotkey { HkPause, HkReset, HkFastForward, HkFullscreen, HkScreenshot, HotkeyCount };

extern const char* const HotkeyNames[HotkeyCount];

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
  bool hackPpuFast = true;
  bool hackPpuNoSpriteLimit = false;
  int hackMode7Scale = 1;
  bool hackDspFast = true;
  bool hackDspCubic = false;
  bool hackCoprocessorDelayedSync = true;
  bool hackCoprocessorPreferHLE = false;
  bool hackHotfixes = true;
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
  int hotkeys[HotkeyCount] = {SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
                              SDL_SCANCODE_F11, SDL_SCANCODE_F12};
  int devices[EmuCore::PortCount] = {EmuCore::Gamepad, EmuCore::Gamepad};

  void applyKey(const std::string& key, const std::string& value);
  void load(const std::string& path);
  void save(const std::string& path) const;
  void addRecent(const std::string& rom);
};

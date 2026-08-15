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

// appended to, never reordered: the config file stores these by index
enum Hotkey { HkPause, HkReset, HkFastForward, HkFullscreen, HkScreenshot,
             HkFrameAdvance, HkPowerCycle, HkMute, HkQuit,
             HkSpeedDown, HkSpeedUp, HotkeyCount };

extern const char* const HotkeyNames[HotkeyCount];

// window-unfocused behaviour while a game runs
enum Defocus { DefocusPause, DefocusBlockInput, DefocusAllowInput, DefocusCount };

extern const char* const DefocusNames[DefocusCount];

// how the frame fills the window, matching bsnes's own Output menu
enum Output { OutputCenter, OutputScale, OutputStretch, OutputCount };

extern const char* const OutputNames[OutputCount];

// emulation speed presets
enum Speed { SpeedHalf, SpeedSlow, SpeedNormal, SpeedFast, SpeedDouble, SpeedCount };

extern const char* const SpeedNames[SpeedCount];
extern const double SpeedScales[SpeedCount];

struct Settings {
  int latencyMs = 48;
  int volume = 100;
  bool mute = false;
  bool muteUnfocused = false;
  std::string audioDevice;  // empty means the system default
  bool aspectCorrect = true;
  int outputMode = OutputScale;
  // bsnes ships the "Blur" shader on by default, which is just GL_LINEAR
  bool linearFilter = true;
  int windowScale = 0;  // 0 = fit to window
  bool overscanCrop = true;
  bool hiresBlur = false;
  int videoGamma = 150;       // percent; bsnes's own default and range (100-200)
  int videoLuminance = 100;   // percent, 0-100
  int videoSaturation = 100;  // percent, 0-200
  std::string videoFilter = "None";
  int defocusPolicy = DefocusPause;
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
  std::string savesDir;  // empty means next to the ROM
  std::vector<std::string> recent;
  int hotkeys[HotkeyCount] = {SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
                              SDL_SCANCODE_F11, SDL_SCANCODE_F12,
                              SDL_SCANCODE_F1, SDL_SCANCODE_F6, SDL_SCANCODE_F8, 0};
  int devices[EmuCore::PortCount] = {EmuCore::Gamepad, EmuCore::Gamepad};
  int turboRate = 10;  // Hz
  int turboMask[EmuCore::PortCount] = {0, 0};  // bit per EmuCore::Button

  void applyKey(const std::string& key, const std::string& value);
  void load(const std::string& path);
  void save(const std::string& path) const;
  void addRecent(const std::string& rom);
};

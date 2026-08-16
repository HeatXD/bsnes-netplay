#pragma once

#include "emucore/emucore.hpp"

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
constexpr int MaxWindowScale = 9;
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

// how the frame fills the window
enum Output { OutputCenter, OutputScale, OutputStretch, OutputCount };

extern const char* const OutputNames[OutputCount];

// memory and register randomization at startup
enum Entropy { EntropyNone, EntropyLow, EntropyHigh, EntropyCount };

// these are the core's own "Hacks/Entropy" values, not just combo labels
extern const char* const EntropyNames[EntropyCount];

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
  bool linearFilter = true;
  int windowScale = 0;  // 0 = fit to window
  bool overscanCrop = true;
  bool hiresBlur = false;
  int videoGamma = 150;       // percent, 100-200
  int videoLuminance = 100;   // percent, 0-100
  int videoSaturation = 100;  // percent, 0-200
  std::string videoFilter = "None";
  int defocusPolicy = DefocusPause;
  int fastForwardSpeed = 4;
  bool fastForwardUnlimited = true;
  int fastForwardFrameSkip = 9;
  bool fastForwardMute = false;
  bool hackPpuFast = true;
  bool hackPpuDeinterlace = true;
  bool hackPpuNoSpriteLimit = false;
  bool hackPpuNoVRAMBlocking = false;
  int hackMode7Scale = 1;
  bool hackMode7Perspective = true;
  bool hackMode7Supersample = false;
  bool hackMode7Mosaic = true;
  bool hackDspFast = true;
  bool hackDspCubic = false;
  bool hackDspEchoShadow = false;
  bool hackCoprocessorDelayedSync = true;
  bool hackCoprocessorPreferHLE = false;
  bool hackHotfixes = true;
  int hackEntropy = EntropyLow;
  bool hackCpuFastMath = false;
  int hackCpuOverclock = 100;      // percent
  int hackSa1Overclock = 100;
  int hackSuperFxOverclock = 100;
  bool showStatus = true;
  bool showToolTips = true;
  int theme = 0;  // 0 dark, 1 light, 2 classic
  int accent = DefaultAccent;
  int textColor = FollowTheme;
  int fontSize = DefaultFontSize;
  int fontWeight = DefaultFontWeight;
  std::string fontPath;  // empty means imgui's built-in font
  std::string gamesDir;
  std::string shotsDir;
  std::string savesDir;     // empty means next to the ROM
  std::string firmwareDir;  // empty means a Firmware folder beside the config
  // base cartridges the slot media ride in
  std::string sgbBios;
  std::string bsxBios;
  std::string stBios;
  std::vector<std::string> recent;
  int hotkeys[HotkeyCount] = {SDL_SCANCODE_F2, SDL_SCANCODE_F3, SDL_SCANCODE_F4,
                              SDL_SCANCODE_F11, SDL_SCANCODE_F12,
                              SDL_SCANCODE_F1, SDL_SCANCODE_F6, SDL_SCANCODE_F8, 0,
                              SDL_SCANCODE_F9, SDL_SCANCODE_F10};
  int devices[EmuCore::PortCount] = {EmuCore::Gamepad, EmuCore::Gamepad};
  // Which opened pad drives each player, -1 for none; one stick can enumerate
  // as several devices, so the working one is chosen rather than assumed. A
  // multitap puts four players on one port, so this is per player, not port.
  int padIndex[EmuCore::PortCount * EmuCore::MaxPlayers] = {0, -1, -1, -1,
                                                            1, -1, -1, -1};
  int turboRate = 8;  // Hz
  int turboMask[EmuCore::PortCount] = {0, 0};  // bit per EmuCore::Button

  void applyKey(const std::string& key, const std::string& value);
  void load(const std::string& path);
  void save(const std::string& path) const;
  void addRecent(const std::string& rom);
};

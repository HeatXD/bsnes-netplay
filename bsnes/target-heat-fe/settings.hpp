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
// bsnes' own range: the resampler is fed the device rate plus this many Hz
constexpr int MaxMode7Scale = 8;
// imgui's own dark-theme blue, packed as 0xRRGGBB
constexpr int DefaultAccent = 0x4296fa;
// a text colour of -1 means follow the theme instead
constexpr int FollowTheme = -1;

// appended to, never reordered: the config file stores these by index
enum Hotkey { HkPause, HkReset, HkFastForward, HkFullscreen, HkScreenshot,
             HkFrameAdvance, HkPowerCycle, HkMute, HkQuit,
             HkSpeedDown, HkSpeedUp,
             HkUnloadGame, HkMouseCapture,
             HkMode7Down, HkMode7Up, HkSupersample, HotkeyCount };

// how a hotkey's several mappings combine: any one, or all of them at once
enum Logic { LogicOr, LogicAnd, LogicCount };

extern const char* const LogicNames[LogicCount];

const char* HotkeyName(int index);
// SDL_SCANCODE_UNKNOWN where bsnes ships the action unbound too
SDL_Scancode HotkeyDefault(int index);

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
  // Hz added to the resampler's target rate, to trim a drifting sound card
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
  bool warnUnverified = false;
  bool autoSaveMemory = true;
  int autoSaveInterval = 30;  // seconds
  // IPS says nothing about copier headers, so the user tells us once
  bool ipsHeadered = false;
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
  std::string patchesDir;   // empty means beside the ROM
  std::string databaseDir;  // empty means a Database folder beside the exe
  // base cartridges the slot media ride in
  std::string sgbBios;
  std::string bsxBios;
  std::string stBios;
  std::vector<std::string> recent;
  // per medium, so a Game Boy ROM cannot send the Sufami Turbo dialog astray
  std::string recentDir[EmuCore::MediumCount];
  // the bindings live in InputMap; only the combining rule is a setting
  int hotkeyLogic = LogicOr;
  // a pre-binding config's scancodes; the count marks which ones it carried
  int legacyHotkeys[HotkeyCount] = {};
  int legacyHotkeyCount = 0;
  int devices[EmuCore::PortCount] = {EmuCore::Gamepad, EmuCore::Gamepad, EmuCore::None};
  // Which opened pad drives each player, -1 for none; one stick can enumerate
  // as several devices, so the working one is chosen rather than assumed. A
  // multitap puts four players on one port, so this is per player, not port.
  int padIndex[EmuCore::PortCount * EmuCore::MaxPlayers] = {0, -1, -1, -1,
                                                            1, -1, -1, -1,
                                                            -1, -1, -1, -1};
  int turboRate = 8;  // Hz

  void applyKey(const std::string& key, const std::string& value);
  void load(const std::string& path);
  void save(const std::string& path) const;
  void addRecent(const std::string& rom);
};

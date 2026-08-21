#include "settings.hpp"

#include "util.hpp"

#include <algorithm>

namespace { constexpr int MaxRecent = 8; }

// name and default key together; appended to, never reordered
namespace {
constexpr struct { const char* name; SDL_Scancode key; } Hotkeys[HotkeyCount] = {
  {"Pause",                SDL_SCANCODE_F2},
  {"Reset",                SDL_SCANCODE_F3},
  {"Fast Forward",         SDL_SCANCODE_F4},
  {"Fullscreen",           SDL_SCANCODE_F11},
  {"Screenshot",           SDL_SCANCODE_F12},
  {"Frame Advance",        SDL_SCANCODE_F1},
  {"Power Cycle",          SDL_SCANCODE_F6},
  {"Mute",                 SDL_SCANCODE_F8},
  {"Quit",                 SDL_SCANCODE_UNKNOWN},
  {"Slow Down",            SDL_SCANCODE_F9},
  {"Speed Up",             SDL_SCANCODE_F10},
  {"Unload Game",          SDL_SCANCODE_UNKNOWN},
  {"Toggle Mouse Capture", SDL_SCANCODE_F5},
  {"Decrease HD Mode 7",   SDL_SCANCODE_UNKNOWN},
  {"Increase HD Mode 7",   SDL_SCANCODE_UNKNOWN},
  {"Toggle Supersampling", SDL_SCANCODE_UNKNOWN},
  {"Save State",           SDL_SCANCODE_UNKNOWN},
  {"Load State",           SDL_SCANCODE_UNKNOWN},
  {"Undo Last Save",       SDL_SCANCODE_UNKNOWN},
  {"Redo Last Undo",       SDL_SCANCODE_UNKNOWN},
  {"Previous State Slot",  SDL_SCANCODE_UNKNOWN},
  {"Next State Slot",      SDL_SCANCODE_UNKNOWN},
  {"Rewind",               SDL_SCANCODE_UNKNOWN},
};
}  // namespace

const char* HotkeyName(int index) { return Hotkeys[index].name; }
SDL_Scancode HotkeyDefault(int index) { return Hotkeys[index].key; }

const char* const DefocusNames[DefocusCount] = {
  "Pause emulation", "Block input", "Allow input"
};

const char* const OutputNames[OutputCount] = {
  "Center (whole pixels)", "Scale (fill the window)", "Stretch (ignore aspect)"
};

const char* const EntropyNames[EntropyCount] = {"None", "Low", "High"};

const char* const SerializationNames[SerialCount] = {"Fast", "Strict"};

const char* const SpeedNames[SpeedCount] = {"50%", "75%", "100%", "150%", "200%"};

// the core is given a slowdown factor, so these are the reciprocals
const double SpeedScales[SpeedCount] = {2.0, 4.0 / 3.0, 1.0, 2.0 / 3.0, 0.5};

// One line per setting, shared by load and save, so adding one is a single edit.
namespace {
struct IntField { const char* key; int Settings::* field; int min, max; };
struct BoolField { const char* key; bool Settings::* field; };
struct StrField { const char* key; std::string Settings::* field; bool path; };

const IntField IntFields[] = {
  {"latency",     &Settings::latencyMs,        MinLatencyMs,   MaxLatencyMs},
  {"volume",      &Settings::volume,           0,              200},
  {"windowscale", &Settings::windowScale,      0,              MaxWindowScale},
  {"ffspeed",     &Settings::fastForwardSpeed, 2,              16},
  {"ffframeskip", &Settings::fastForwardFrameSkip, 0,          9},
  {"rewindfrequency", &Settings::rewindFrequency, 0,            60},
  {"rewindlength", &Settings::rewindLength,       10,           320},
  {"runahead",     &Settings::runAheadFrames,     0,            4},
  {"mode7scale",  &Settings::hackMode7Scale,   1,              MaxMode7Scale},
  {"entropy",     &Settings::hackEntropy,      0,              EntropyCount - 1},
  {"cpuoverclock",   &Settings::hackCpuOverclock,    100,      400},
  {"sa1overclock",   &Settings::hackSa1Overclock,    100,      400},
  {"superfxoverclock", &Settings::hackSuperFxOverclock, 100,   800},
  {"theme",       &Settings::theme,            0,              2},
  {"accent",      &Settings::accent,           0,              0xffffff},
  {"textcolor",   &Settings::textColor,        FollowTheme,    0xffffff},
  {"fontsize",    &Settings::fontSize,         MinFontSize,    MaxFontSize},
  {"fontweight",  &Settings::fontWeight,       MinFontWeight,  MaxFontWeight},
  {"gamma",       &Settings::videoGamma,       100,            200},
  {"luminance",   &Settings::videoLuminance,   0,              100},
  {"saturation",  &Settings::videoSaturation,  0,              200},
  {"defocus",     &Settings::defocusPolicy,    0,              DefocusCount - 1},
  {"turborate",   &Settings::turboRate,        1,              30},
  {"output",      &Settings::outputMode,       0,              OutputCount - 1},
  {"audioskew",   &Settings::audioSkew,        -MaxAudioSkew,  MaxAudioSkew},
  {"audiobalance", &Settings::audioBalance,    0,              100},
  {"autosaveinterval", &Settings::autoSaveInterval, 5,         600},
  {"serialization", &Settings::serialization,  0,              SerialCount - 1},
};

const BoolField BoolFields[] = {
  {"mute",           &Settings::mute},
  {"muteunfocused",  &Settings::muteUnfocused},
  {"aspect",         &Settings::aspectCorrect},
  {"linear",         &Settings::linearFilter},
  {"showstatus",     &Settings::showStatus},
  {"overscancrop",   &Settings::overscanCrop},
  {"hiresblur",      &Settings::hiresBlur},
  {"ppufast",        &Settings::hackPpuFast},
  {"deinterlace",    &Settings::hackPpuDeinterlace},
  {"nospritelimit",  &Settings::hackPpuNoSpriteLimit},
  {"novramblocking", &Settings::hackPpuNoVRAMBlocking},
  {"mode7perspective", &Settings::hackMode7Perspective},
  {"mode7supersample", &Settings::hackMode7Supersample},
  {"mode7mosaic",    &Settings::hackMode7Mosaic},
  {"dspfast",        &Settings::hackDspFast},
  {"dspcubic",       &Settings::hackDspCubic},
  {"echoshadow",     &Settings::hackDspEchoShadow},
  {"delayedsync",    &Settings::hackCoprocessorDelayedSync},
  {"preferhle",      &Settings::hackCoprocessorPreferHLE},
  {"hotfixes",       &Settings::hackHotfixes},
  {"fastmath",       &Settings::hackCpuFastMath},
  {"ffunlimited",    &Settings::fastForwardUnlimited},
  {"ffmute",         &Settings::fastForwardMute},
  {"rewindmute",     &Settings::rewindMute},
  {"tooltips",       &Settings::showToolTips},
  {"dimming",        &Settings::videoDimming},
  {"screenshotlua",  &Settings::screenshotLua},
  {"warnunverified", &Settings::warnUnverified},
  {"autosavememory", &Settings::autoSaveMemory},
  {"ipsheadered",    &Settings::ipsHeadered},
  {"autostateunload", &Settings::autoStateOnUnload},
  {"autostateload",  &Settings::autoStateOnLoad},
  {"cheatsenabled",  &Settings::cheatsEnabled},
};

const StrField StrFields[] = {
  {"font",        &Settings::fontPath,    false},
  {"gamesdir",    &Settings::gamesDir,    true},
  {"shotsdir",    &Settings::shotsDir,    true},
  {"videofilter", &Settings::videoFilter, false},
  {"savesdir",    &Settings::savesDir,    true},
  {"firmwaredir", &Settings::firmwareDir, true},
  {"audiodevice", &Settings::audioDevice, false},
  {"sgbbios",     &Settings::sgbBios,     true},
  {"bsxbios",     &Settings::bsxBios,     true},
  {"stbios",      &Settings::stBios,      true},
  {"patchesdir",  &Settings::patchesDir,  true},
  {"databasedir", &Settings::databaseDir, true},
  {"statesdir",   &Settings::statesDir,   true},
  {"cheatsdir",   &Settings::cheatsDir,   true},
  {"display",     &Settings::displayName, false},
  {"shader",      &Settings::videoShader, true},
  {"shadersdir",  &Settings::shadersDir,  true},
};
}  // namespace

void Settings::applyKey(const std::string& key, const std::string& value) {
  const int number = SDL_atoi(value.c_str());

  for(const IntField& f : IntFields) {
    if(key == f.key) { this->*f.field = SDL_clamp(number, f.min, f.max); return; }
  }
  for(const BoolField& f : BoolFields) {
    if(key == f.key) { this->*f.field = number != 0; return; }
  }
  for(const StrField& f : StrFields) {
    if(key == f.key) { this->*f.field = f.path ? normalPath(value) : value; return; }
  }

  // "name=value"; only the selected shader's overrides are kept
  if(key == "shaderparam") {
    const size_t equals = value.find('=');
    if(equals != std::string::npos) {
      const std::string name = value.substr(0, equals);
      for(ShaderSetting& param : shaderParams) {
        if(param.name == name) { param.value = value.substr(equals + 1); return; }
      }
      shaderParams.push_back({name, value.substr(equals + 1)});
    }
    return;
  }

  if(key == "recent") {
    const std::string rom = normalPath(value);
    if(recent.size() < MaxRecent
    && std::find(recent.begin(), recent.end(), rom) == recent.end()) {
      recent.push_back(rom);
    }
    return;
  }

  auto indexed = [&](const char* prefix, int* slots, int count) {
    if(key.rfind(prefix, 0) != 0) return false;
    const int index = SDL_atoi(key.c_str() + SDL_strlen(prefix));
    if(index >= 0 && index < count) slots[index] = number;
    return true;
  };
  if(key.rfind("hotkey", 0) == 0 && SDL_isdigit((unsigned char)key[6])) {
    const int index = SDL_atoi(key.c_str() + 6);
    if(index >= 0 && index < HotkeyCount) {
      legacyHotkeys[index] = number;
      legacyHotkeyCount = SDL_max(legacyHotkeyCount, index + 1);
    }
    return;
  }
  if(key.rfind("recentdir", 0) == 0) {
    const int index = SDL_atoi(key.c_str() + 9);
    if(index >= 0 && index < EmuCore::MediumCount) recentDir[index] = normalPath(value);
    return;
  }
  if(!indexed("device", devices, EmuCore::PortCount)) {
    indexed("pad", padIndex, EmuCore::PortCount * EmuCore::MaxPlayers);
  }
}

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
    applyKey(line.substr(0, space), line.substr(space + 1));
  }
}

void Settings::save(const std::string& path) const {
  std::string text;
  auto add = [&](const char* key, const std::string& value) {
    text += key; text += ' '; text += value; text += '\n';
  };
  auto addInt = [&](const char* key, int value) {
    char digits[16];
    SDL_itoa(value, digits, 10);
    add(key, digits);
  };
  auto addIndexed = [&](const char* prefix, int index, int value) {
    char key[24];
    SDL_snprintf(key, sizeof(key), "%s%d", prefix, index);
    addInt(key, value);
  };

  for(const IntField& f : IntFields) addInt(f.key, this->*f.field);
  for(const BoolField& f : BoolFields) addInt(f.key, this->*f.field);
  for(const StrField& f : StrFields) add(f.key, this->*f.field);
  for(int i = 0; i < EmuCore::PortCount; i++) addIndexed("device", i, devices[i]);
  for(int i = 0; i < EmuCore::PortCount * EmuCore::MaxPlayers; i++) addIndexed("pad", i, padIndex[i]);
  for(int i = 0; i < EmuCore::MediumCount; i++) {
    char key[16];
    SDL_snprintf(key, sizeof(key), "recentdir%d", i);
    add(key, recentDir[i]);
  }
  for(const std::string& rom : recent) add("recent", rom);
  for(const ShaderSetting& param : shaderParams) add("shaderparam", param.name + "=" + param.value);

  writeText(path, text);
}

void Settings::addRecent(const std::string& path) {
  const std::string rom = normalPath(path);
  recent.erase(std::remove(recent.begin(), recent.end(), rom), recent.end());
  recent.insert(recent.begin(), rom);
  if(recent.size() > MaxRecent) recent.resize(MaxRecent);
}

#include "settings.hpp"

#include "util.hpp"

#include <algorithm>

namespace { constexpr int MaxRecent = 8; }

const char* const HotkeyNames[HotkeyCount] = {
  "Pause", "Reset", "Fast Forward", "Fullscreen", "Screenshot",
  "Frame Advance", "Power Cycle", "Mute", "Quit",
  "Slow Down", "Speed Up"
};

const char* const DefocusNames[DefocusCount] = {
  "Pause emulation", "Block input", "Allow input"
};

const char* const OutputNames[OutputCount] = {
  "Center (whole pixels)", "Scale (fill the window)", "Stretch (ignore aspect)"
};

const char* const EntropyNames[EntropyCount] = {"None", "Low", "High"};

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
  {"mode7scale",  &Settings::hackMode7Scale,   1,              8},
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
  {"tooltips",       &Settings::showToolTips},
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
  if(!indexed("device", devices, EmuCore::PortCount)
  && !indexed("hotkey", hotkeys, HotkeyCount)) {
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
  for(int i = 0; i < HotkeyCount; i++) addIndexed("hotkey", i, hotkeys[i]);
  for(int i = 0; i < EmuCore::PortCount * EmuCore::MaxPlayers; i++) addIndexed("pad", i, padIndex[i]);
  for(int i = 0; i < EmuCore::MediumCount; i++) {
    char key[16];
    SDL_snprintf(key, sizeof(key), "recentdir%d", i);
    add(key, recentDir[i]);
  }
  for(const std::string& rom : recent) add("recent", rom);

  writeText(path, text);
}

void Settings::addRecent(const std::string& path) {
  const std::string rom = normalPath(path);
  recent.erase(std::remove(recent.begin(), recent.end(), rom), recent.end());
  recent.insert(recent.begin(), rom);
  if(recent.size() > MaxRecent) recent.resize(MaxRecent);
}

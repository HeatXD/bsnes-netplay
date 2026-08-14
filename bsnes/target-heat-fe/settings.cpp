#include "settings.hpp"

#include "util.hpp"

#include <algorithm>

namespace { constexpr int MaxRecent = 8; }

const char* const HotkeyNames[HotkeyCount] = {
  "Pause", "Reset", "Fast Forward", "Fullscreen", "Screenshot"
};

void Settings::applyKey(const std::string& key, const std::string& value) {
  const int number = SDL_atoi(value.c_str());

if(key == "latency") latencyMs = SDL_clamp(number, MinLatencyMs, MaxLatencyMs);
  else if(key == "volume") volume = SDL_clamp(number, 0, 200);
  else if(key == "mute") mute = number != 0;
  else if(key == "aspect") aspectCorrect = number != 0;
  else if(key == "integer") integerScale = number != 0;
  else if(key == "linear") linearFilter = number != 0;
  else if(key == "windowscale") windowScale = SDL_clamp(number, 0, 5);
  else if(key == "pauseunfocused") pauseUnfocused = number != 0;
  else if(key == "ffspeed") fastForwardSpeed = SDL_clamp(number, 2, 16);
  else if(key == "showstatus") showStatus = number != 0;
  else if(key == "theme") theme = SDL_clamp(number, 0, 2);
  else if(key == "accent") accent = SDL_clamp(number, 0, 0xffffff);
  else if(key == "textcolor") textColor = SDL_clamp(number, FollowTheme, 0xffffff);
  else if(key == "fontsize") fontSize = SDL_clamp(number, MinFontSize, MaxFontSize);
  else if(key == "fontweight") fontWeight = SDL_clamp(number, MinFontWeight, MaxFontWeight);
  else if(key == "font") fontPath = value;
  else if(key == "gamesdir") gamesDir = normalPath(value);
  else if(key == "shotsdir") shotsDir = normalPath(value);
  else if(key == "recent") {
    const std::string rom = normalPath(value);
    if(recent.size() < MaxRecent
    && std::find(recent.begin(), recent.end(), rom) == recent.end()) {
      recent.push_back(rom);
    }
  }
  else {
    auto indexed = [&](const char* prefix, int* slots, int count) {
      if(key.rfind(prefix, 0) != 0) return false;
      const int index = SDL_atoi(key.c_str() + SDL_strlen(prefix));
      if(index >= 0 && index < count) slots[index] = number;
      return true;
    };
    if(!indexed("device", devices, EmuCore::PortCount)) {
      indexed("hotkey", hotkeys, HotkeyCount);
    }
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
  for(int i = 0; i < EmuCore::PortCount; i++) addIndexed("device", i, devices[i]);
  for(int i = 0; i < HotkeyCount; i++) addIndexed("hotkey", i, hotkeys[i]);
  for(const std::string& rom : recent) add("recent", rom);

  writeText(path, text);
}

void Settings::addRecent(const std::string& path) {
  const std::string rom = normalPath(path);
  recent.erase(std::remove(recent.begin(), recent.end(), rom), recent.end());
  recent.insert(recent.begin(), rom);
  if(recent.size() > MaxRecent) recent.resize(MaxRecent);
}

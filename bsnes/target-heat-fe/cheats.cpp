#include "app.hpp"

#include <algorithm>
#include <cctype>

namespace {
std::string trim(std::string text) {
  const size_t first = text.find_first_not_of(" \t\r\n");
  if(first == std::string::npos) { return {}; }
  return text.substr(first, text.find_last_not_of(" \t\r\n") + 1 - first);
}

bool hexText(const std::string& text) {
  if(text.empty()) { return false; }
  for(char ch : text) {
    if(!std::isxdigit((unsigned char)ch)) { return false; }
  }
  return true;
}

uint32_t hexValue(const std::string& text) {
  uint32_t value = 0;
  for(char ch : text) {
    value <<= 4;
    value |= ch <= '9' ? ch - '0' : ch - 'a' + 10;
  }
  return value;
}

std::string hex(uint32_t value, int digits) {
  char text[16];
  SDL_snprintf(text, sizeof(text), "%0*x", digits, value);
  return text;
}

bool decodeSnes(std::string& code) {
  if(code.size() == 9 && code[4] == '-') {
    code.erase(4, 1);
    if(!hexText(code)) { return false; }
    const std::string alphabet = "df4709156bc8a23e";
    for(char& ch : code) {
      const size_t at = alphabet.find(ch);
      if(at == std::string::npos) { return false; }
      ch = "0123456789abcdef"[at];
    }
    const uint32_t r = hexValue(code);
    const uint32_t address =
        (!!(r & 0x002000) << 23) | (!!(r & 0x001000) << 22) | (!!(r & 0x000800) << 21) |
        (!!(r & 0x000400) << 20) | (!!(r & 0x000020) << 19) | (!!(r & 0x000010) << 18) |
        (!!(r & 0x000008) << 17) | (!!(r & 0x000004) << 16) | (!!(r & 0x800000) << 15) |
        (!!(r & 0x400000) << 14) | (!!(r & 0x200000) << 13) | (!!(r & 0x100000) << 12) |
        (!!(r & 0x000002) << 11) | (!!(r & 0x000001) << 10) | (!!(r & 0x008000) << 9) |
        (!!(r & 0x004000) << 8) | (!!(r & 0x080000) << 7) | (!!(r & 0x040000) << 6) |
        (!!(r & 0x020000) << 5) | (!!(r & 0x010000) << 4) | (!!(r & 0x000200) << 3) |
        (!!(r & 0x000100) << 2) | (!!(r & 0x000080) << 1) | (!!(r & 0x000040) << 0);
    code = hex(address, 6) + "=" + hex(r >> 24, 2);
    return true;
  }
  if(code.size() == 8 && hexText(code)) {
    const uint32_t value = hexValue(code);
    code = hex(value >> 8, 6) + "=" + hex(value & 0xff, 2);
    return true;
  }
  if(code.size() == 9 && code[6] == '=') { return hexText(code.substr(0, 6) + code.substr(7, 2)); }
  if(code.size() == 12 && code[6] == '=' && code[9] == '?') {
    return hexText(code.substr(0, 6) + code.substr(7, 2) + code.substr(10, 2));
  }
  return false;
}

bool decodeGb(std::string& code) {
  auto nibble = [](char ch) { return ch <= '9' ? ch - '0' : ch - 'a' + 10; };
  if(code.size() == 7 && code[3] == '-') {
    code.erase(3, 1);
    if(!hexText(code)) { return false; }
    const uint32_t data = nibble(code[0]) << 4 | nibble(code[1]);
    const uint32_t address = (nibble(code[5]) ^ 15) << 12 | nibble(code[2]) << 8 |
                             nibble(code[3]) << 4 | nibble(code[4]);
    code = hex(address, 4) + "=" + hex(data, 2);
    return true;
  }
  if(code.size() == 11 && code[3] == '-' && code[7] == '-') {
    code.erase(7, 1);
    code.erase(3, 1);
    if(!hexText(code)) { return false; }
    const uint32_t data = nibble(code[0]) << 4 | nibble(code[1]);
    const uint32_t address = (nibble(code[5]) ^ 15) << 12 | nibble(code[2]) << 8 |
                             nibble(code[3]) << 4 | nibble(code[4]);
    uint8_t compare = (uint8_t)(nibble(code[6]) << 4 | nibble(code[8]));
    compare = (uint8_t)(compare >> 2 | compare << 6);
    compare ^= 0xba;
    code = hex(address, 4) + "=" + hex(compare, 2) + "?" + hex(data, 2);
    return true;
  }
  if(code.size() == 8 && hexText(code) && code.compare(0, 2, "01") == 0) {
    const uint32_t address = hexValue(code.substr(6, 2)) << 8 | hexValue(code.substr(4, 2));
    code = hex(address, 4) + "=" + code.substr(2, 2);
    return true;
  }
  if(code.size() == 7 && code[4] == '=') { return hexText(code.substr(0, 4) + code.substr(5, 2)); }
  if(code.size() == 10 && code[4] == '=' && code[7] == '?') {
    return hexText(code.substr(0, 4) + code.substr(5, 2) + code.substr(8, 2));
  }
  return false;
}
}  // namespace

std::string App::cheatPath() const {
  if(gameLocation.empty()) { return {}; }
  if(!settings.cheatsDir.empty()) { return settings.cheatsDir + "/" + gameTitle + ".cht"; }
  if(isDirectory(gameLocation)) { return pakPath(gameLocation) + "cheats.bml"; }
  return parentDir(gameLocation) + "/" + gameTitle + ".cht";
}

void App::loadCheats() {
  cheats.clear();
  CheatEntry entry;
  const std::string text = readText(cheatPath());
  size_t at = 0;
  while(at < text.size()) {
    size_t end = text.find('\n', at);
    if(end == std::string::npos) { end = text.size(); }
    std::string line = text.substr(at, end - at);
    if(!line.empty() && line.back() == '\r') { line.pop_back(); }
    at = end + 1;
    if(line == "cheat") {
      if(!entry.name.empty() && !entry.code.empty()) { cheats.push_back(entry); }
      entry = {};
    } else if(line.compare(0, 8, "  name: ") == 0) {
      entry.name = line.substr(8);
    } else if(line.compare(0, 8, "  code: ") == 0) {
      entry.code = line.substr(8);
    } else if(trim(line) == "enable") {
      entry.enabled = true;
    }
  }
  if(!entry.name.empty() && !entry.code.empty()) { cheats.push_back(entry); }
  std::sort(cheats.begin(), cheats.end(), [](const CheatEntry& a, const CheatEntry& b) {
    return SDL_strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
  cheatSelected = -1;
  cheatsDirty = false;
  applyCheats();
}

void App::saveCheats() {
  if(!cheatsDirty) { return; }
  const std::string path = cheatPath();
  if(path.empty()) { return; }
  if(cheats.empty()) {
    SDL_RemovePath(path.c_str());
    cheatsDirty = false;
    return;
  }
  std::string text;
  for(const CheatEntry& cheat : cheats) {
    text += "cheat\n  name: " + cheat.name + "\n  code: " + cheat.code + "\n";
    if(cheat.enabled) { text += "  enable\n"; }
    text += "\n";
  }
  if(ensureDir(parentDir(path)) && writeText(path, text)) { cheatsDirty = false; }
}

void App::applyCheats() {
  // a locally-applied memory patch is invisible to peers and desyncs them
  // the instant it touches anything the checksum covers
  if(netplayActive()) {
    showMessage("cheats cannot be changed during netplay");
    return;
  }
  std::vector<std::string> codes;
  if(settings.cheatsEnabled) {
    for(const CheatEntry& cheat : cheats) {
      if(cheat.enabled) { codes.push_back(cheat.code); }
    }
  }
  core.setCheats(codes);
}

bool App::normalizeCheatCode(std::string& text) const {
  std::vector<std::string> codes;
  size_t at = 0;
  while(at <= text.size()) {
    size_t end = text.find_first_of("+\r\n", at);
    if(end == std::string::npos) { end = text.size(); }
    std::string code = trim(text.substr(at, end - at));
    for(char& ch : code) { ch = (char)std::tolower((unsigned char)ch); }
    if(!code.empty()) {
      if(!(core.gameBoyLoaded() ? decodeGb(code) : decodeSnes(code))) { return false; }
      codes.push_back(code);
    }
    if(end == text.size()) { break; }
    at = end + 1;
  }
  if(codes.empty()) { return false; }
  text.clear();
  for(const std::string& code : codes) { text += (text.empty() ? "" : "+") + code; }
  return true;
}

std::vector<CheatEntry> App::findDatabaseCheats() const {
  std::vector<CheatEntry> found;
  const std::string text = readText(databaseDir() + "/Cheat Codes.bml");
  if(text.empty()) { return found; }
  size_t start = std::string::npos;
  for(const std::string& hash : core.hashes()) {
    start = text.find("cartridge sha256:" + hash);
    if(start != std::string::npos) { break; }
  }
  if(start == std::string::npos) { return found; }
  const size_t finish = text.find("\ncartridge sha256:", start + 1);
  const std::string block = text.substr(start, finish - start);

  CheatEntry entry;
  size_t at = 0;
  while(at < block.size()) {
    size_t end = block.find('\n', at);
    if(end == std::string::npos) { end = block.size(); }
    std::string line = block.substr(at, end - at);
    if(!line.empty() && line.back() == '\r') { line.pop_back(); }
    at = end + 1;
    if(line == "  cheat") {
      if(!entry.name.empty() && !entry.code.empty()) { found.push_back(entry); }
      entry = {};
    } else if(line.compare(0, 16, "    description:") == 0) {
      entry.name = line.substr(16);
    } else if(line.compare(0, 9, "    code:") == 0) {
      entry.code = line.substr(9);
      size_t slash = 0;
      int separator = 0;
      while((slash = entry.code.find('/', slash)) != std::string::npos) {
        entry.code[slash] = separator++ == 0 ? '=' : '?';
        slash++;
      }
    }
  }
  if(!entry.name.empty() && !entry.code.empty()) { found.push_back(entry); }
  return found;
}

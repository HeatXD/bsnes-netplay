#include "util.hpp"

namespace {
const char* const RomExts[] = {"sfc", "smc", "fig", "swc", "bs", "st", "gb", "gbc",
                               "zip", "7z"};
}

// a game pak path ends in a separator, and is named by its folder
std::string fileName(const std::string& path) {
  const size_t end = path.find_last_not_of("/\\");
  if(end == std::string::npos) return path;
  const size_t slash = path.find_last_of("/\\", end);
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  return path.substr(start, end + 1 - start);
}

std::string fileStem(const std::string& path) {
  const std::string name = fileName(path);
  const size_t dot = name.find_last_of('.');
  return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string normalPath(const std::string& path) {
  std::string out = path;
  for(char& c : out) if(c == '\\') c = '/';
  return out;
}

bool pathExists(const std::string& path) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path.c_str(), &info);
}

bool isDirectory(const std::string& path) {
  SDL_PathInfo info;
  return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::string pakPath(const std::string& dir) {
  std::string path = normalPath(dir);
  if(!path.empty() && path.back() != '/') path += '/';
  return path;
}

std::pair<std::string, std::string> splitPair(const std::string& entry) {
  const char* bar = SDL_strchr(entry.c_str(), '|');
  if(!bar) return {entry, {}};
  return {std::string(entry.c_str(), bar), std::string(bar + 1)};
}

std::string recentLabel(const std::string& entry) {
  const auto pair = splitPair(entry);
  if(pair.second.empty()) return fileName(pair.first);
  return fileName(pair.first) + " + " + fileName(pair.second);
}

bool isRom(const char* name) {
  const char* ext = SDL_strrchr(name, '.');
  if(!ext) return false;

  for(const char* candidate : RomExts) {
    if(SDL_strcasecmp(ext + 1, candidate) == 0) return true;
  }
  return false;
}

const char* romFilterPattern() {
  static const std::string pattern = [] {
    std::string out;
    for(const char* ext : RomExts) {
      if(!out.empty()) out += ';';
      out += ext;
    }
    return out;
  }();
  return pattern.c_str();
}

const std::string& configDir() {
  static const std::string dir = [] {
    if(const char* base = SDL_GetBasePath()) {
      if(pathExists(std::string(base) + "settings.cfg")) return std::string(base);
    }
    char* pref = SDL_GetPrefPath("bsnes-netplay", "imgui");
    std::string path = pref ? pref : "";
    if(pref) SDL_free(pref);
    return path;
  }();
  return dir;
}

bool portableMode() {
  static const bool portable = [] {
    const char* base = SDL_GetBasePath();
    return base && configDir() == base;
  }();
  return portable;
}

std::string prefFile(const char* name) { return configDir() + name; }

std::string readText(const std::string& path) {
  size_t size = 0;
  void* data = SDL_LoadFile(path.c_str(), &size);
  if(!data) return {};
  std::string text((const char*)data, size);
  SDL_free(data);
  return text;
}

bool writeText(const std::string& path, const std::string& text) {
  return SDL_SaveFile(path.c_str(), text.data(), text.size());
}

void SDLCALL onPicked(void* userdata, const char* const* filelist, int) {
  auto* pick = (FilePick*)userdata;
  Guard guard(pick->mutex);
  pick->open = false;
  pick->path = filelist && filelist[0] ? filelist[0] : "";
  pick->ready = true;
}

bool takePick(FilePick& pick, std::string& path) {
  Guard guard(pick.mutex);
  if(!pick.ready) return false;
  pick.ready = false;
  path = pick.path;
  return true;
}

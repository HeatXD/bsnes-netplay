#include "util.hpp"

namespace {
const char* const RomExts[] = {"sfc", "smc", "fig", "swc", "bs", "st", "gb", "gbc"};
}

std::string fileName(const std::string& path) {
  const size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
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
  if(filelist && filelist[0]) {
    pick->path = filelist[0];
    pick->ready = true;
  }
}

std::string takePick(FilePick& pick) {
  Guard guard(pick.mutex);
  if(!pick.ready) return {};
  pick.ready = false;
  return pick.path;
}

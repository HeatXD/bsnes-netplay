#pragma once

#include <SDL3/SDL.h>

#include <string>

inline constexpr const char* AppName = "bsnes heat-fe";

std::string fileName(const std::string& path);
std::string fileStem(const std::string& path);
// globs return '/', dialogs return native separators; compare like with like
std::string normalPath(const std::string& path);
bool pathExists(const std::string& path);

bool isRom(const char* name);
const char* romFilterPattern();

const std::string& configDir();
bool portableMode();
std::string prefFile(const char* name);

std::string readText(const std::string& path);
bool writeText(const std::string& path, const std::string& text);

struct Guard {
  SDL_Mutex* mutex;
  explicit Guard(SDL_Mutex* mutex) : mutex(mutex) { SDL_LockMutex(mutex); }
  ~Guard() { SDL_UnlockMutex(mutex); }
};

// SDL dialogs are async and may call back off the main thread
struct FilePick {
  SDL_Mutex* mutex = SDL_CreateMutex();
  std::string path;
  bool ready = false;
  bool open = false;

  ~FilePick() { SDL_DestroyMutex(mutex); }
};

void SDLCALL onPicked(void* userdata, const char* const* filelist, int);
std::string takePick(FilePick& pick);

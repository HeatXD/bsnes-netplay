#include "shell.hpp"

#include "util.hpp"

#include "imgui.h"

// macOS has no 3.0 compatibility context, only 2.1 legacy or 3.2+ core
namespace {
#ifdef __APPLE__
constexpr int GlMajor = 3, GlMinor = 2;
#else
constexpr int GlMajor = 3, GlMinor = 0;
#endif
}



bool Shell::initVideo() {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, GlMajor);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, GlMinor);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

  window = SDL_CreateWindow(AppName, 878, 224 * 3 + 24,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
  if(!window) {
    SDL_Log("window creation failed: %s", SDL_GetError());
    return false;
  }

  gl = SDL_GL_CreateContext(window);
  if(!gl) {
    SDL_Log("gl context failed: %s", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(window, gl);
  // audio is the master clock, so vsync must not also gate the loop
  SDL_GL_SetSwapInterval(0);

  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  textureWidth = EmuCore::MaxWidth;
  textureHeight = EmuCore::MaxHeight;
  // GL_RGB, not GL_RGBA: the 2xSaI family blends through a 0xFEFEFE mask that
  // drops the alpha byte, so interpolated pixels arrive fully transparent and
  // would blend into the background. bsnes sidesteps this with an XRGB surface.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0,
               GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
  return true;
}

namespace {
// resolves a remembered device name back to an id; falls back to the system
// default when the name is empty or the device is gone (renumbered on reboot)
SDL_AudioDeviceID findPlaybackDevice(const std::string& name) {
  if(name.empty()) return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;

  int count = 0;
  SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
  SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
  for(int i = 0; i < count; i++) {
    const char* devName = SDL_GetAudioDeviceName(ids[i]);
    if(devName && name == devName) { found = ids[i]; break; }
  }
  if(ids) SDL_free(ids);
  return found;
}
}  // namespace

bool Shell::initAudio(const Settings& settings) {
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = AudioRate;
  audio = SDL_OpenAudioDeviceStream(findPlaybackDevice(settings.audioDevice), &spec, nullptr, nullptr);
  if(!audio) {
    SDL_Log("audio open failed: %s", SDL_GetError());
    return false;
  }
  SDL_ResumeAudioStreamDevice(audio);
  return true;
}

// open the replacement before dropping the old stream: audio is the master
// clock, so a failed switch has to leave the working one in place or the loop
// runs unpaced
bool Shell::reopenAudio(const Settings& settings) {
  SDL_AudioStream* previous = audio;
  audio = nullptr;
  if(!initAudio(settings)) {
    audio = previous;
    return false;
  }
  if(previous) SDL_DestroyAudioStream(previous);
  audioGain = -1.0f;  // force the new stream to pick up the current gain
  return true;
}

std::vector<std::string> Shell::listPlaybackDevices() {
  std::vector<std::string> names;
  int count = 0;
  SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
  for(int i = 0; i < count; i++) {
    if(const char* name = SDL_GetAudioDeviceName(ids[i])) names.emplace_back(name);
  }
  if(ids) SDL_free(ids);
  return names;
}

bool Shell::init(const Settings& settings) {
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  return initVideo() && initAudio(settings);
}

void Shell::shutdown() {
  if(audio) SDL_DestroyAudioStream(audio);
  if(texture) glDeleteTextures(1, &texture);
  if(gl) SDL_GL_DestroyContext(gl);
  if(window) SDL_DestroyWindow(window);
  SDL_Quit();
}

// Audio is the master clock; draining to a byte backlog paces NTSC and PAL alike.
int Shell::paceTarget(const Settings& settings) const {
  return settings.latencyMs * AudioRate / 1000 * 2 * (int)sizeof(float);
}

int Shell::displayFrameMs() const {
  const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
  if(!mode || mode->refresh_rate <= 0.0f) return 16;
  return SDL_max(1, (int)(1000.0f / mode->refresh_rate));
}

void Shell::pace(const Settings& settings) {
  constexpr int bytesPerSecond = AudioRate * 2 * (int)sizeof(float);
  const int target = paceTarget(settings);

  // the backlog is already a duration, so sleep it off in one go rather than
  // polling; the loop only runs again if the sleep came up short
  while(true) {
    const int over = SDL_GetAudioStreamQueued(audio) - target;
    if(over <= 0) return;
    SDL_DelayNS((Uint64)over * SDL_NS_PER_SECOND / bytesPerSecond);
  }
}

void Shell::pushVideo(const uint32_t* argb, int width, int height) {
  frameWidth = width;
  frameHeight = height;
  lastPixels = argb;

  glBindTexture(GL_TEXTURE_2D, texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  // HD mode 7 scales the frame past the initial allocation; grow rather than
  // crop, and never shrink, so a mode change doesn't reallocate every frame
  if(width > textureWidth || height > textureHeight) {
    textureWidth = SDL_max(textureWidth, width);
    textureHeight = SDL_max(textureHeight, height);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
  }
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, argb);
}

void Shell::pushAudio(const Settings& settings, const float* samples, int frames, bool focused) {
  if(frames <= 0) return;
  const bool silent = settings.mute || (settings.muteUnfocused && !focused);
  const float gain = silent ? 0.0f : settings.volume / 100.0f;
  if(gain != audioGain) {
    audioGain = gain;
    SDL_SetAudioStreamGain(audio, gain);
  }
  SDL_PutAudioStreamData(audio, samples, frames * 2 * (int)sizeof(float));
}

void Shell::drawGame(const Settings& settings) {
  if(frameWidth <= 0 || frameHeight <= 0) return;

  const ImGuiViewport* view = ImGui::GetMainViewport();
  const float availW = view->WorkSize.x, availH = view->WorkSize.y;
  if(availW <= 0.0f || availH <= 0.0f) return;

  // Geometry comes from the canonical SNES frame, never from the filtered
  // pixel count: a filter changes resolution, not the picture's shape. NTSC
  // is 602 wide and scanlines 480 tall, and sizing off those would stretch
  // them. NTSC pixels are not square either; 8/7 takes 256x224 out to 4:3.
  const float videoW = 256.0f * (settings.aspectCorrect ? 8.0f / 7.0f : 1.0f);
  const float videoH = settings.overscanCrop ? 224.0f : 240.0f;

  float scale;
  if(settings.windowScale > 0) {
    scale = (float)settings.windowScale;
  } else {
    const float fit = SDL_min(availW / videoW, availH / videoH);
    scale = settings.integerScale ? SDL_max(1.0f, SDL_floorf(fit)) : fit;
  }
  const float w = videoW * scale;
  const float h = videoH * scale;

  const GLint filter = settings.linearFilter ? GL_LINEAR : GL_NEAREST;
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);

  const ImVec2 p0(view->WorkPos.x + (availW - w) / 2.0f, view->WorkPos.y + (availH - h) / 2.0f);
  const ImVec2 p1(p0.x + w, p0.y + h);
  const ImVec2 uv1((float)frameWidth / textureWidth, (float)frameHeight / textureHeight);
  ImGui::GetBackgroundDrawList()->AddImage((ImTextureID)(intptr_t)texture, p0, p1, ImVec2(0, 0), uv1);
}

namespace {
bool saveBmp(const void* pixels, int w, int h, const std::string& path, bool flip) {
  SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888,
                                               (void*)pixels, w * (int)sizeof(uint32_t));
  if(!surface) return false;
  const bool ok = (!flip || SDL_FlipSurface(surface, SDL_FLIP_VERTICAL))
               && SDL_SaveBMP(surface, path.c_str());
  SDL_DestroySurface(surface);
  return ok;
}
}  // namespace

bool Shell::saveFrame(const std::string& path) const {
  if(!lastPixels || frameWidth <= 0 || frameHeight <= 0) return false;
  return saveBmp(lastPixels, frameWidth, frameHeight, path, false);
}

// GL's origin is bottom-left, so the rows come back upside down
bool Shell::saveWindow(const std::string& path) const {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  if(w <= 0 || h <= 0) return false;

  std::vector<uint32_t> pixels((size_t)w * h);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());

  return saveBmp(pixels.data(), w, h, path, true);
}


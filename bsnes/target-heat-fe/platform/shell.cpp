#include "shell.hpp"

#include "../util.hpp"

#include "imgui.h"

namespace {
// the quark packages are #version 150, and macOS has nothing between it and 2.1
void requestGl(bool core) {
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, core ? 2 : 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, core ? SDL_GL_CONTEXT_PROFILE_CORE : 0);
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
}
}  // namespace

bool Shell::initVideo() {
  requestGl(true);
  window = SDL_CreateWindow(AppName, 878, 224 * 3 + 24, SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
  if(!window) {
    SDL_Log("window creation failed: %s", SDL_GetError());
    return false;
  }

  gl = SDL_GL_CreateContext(window);
#ifndef __APPLE__
  // a driver with no 3.2 still runs everything but the shader pipeline
  if(!gl) {
    SDL_Log("gl 3.2 core unavailable (%s), falling back", SDL_GetError());
    requestGl(false);
    gl = SDL_GL_CreateContext(window);
    glslVersion = "#version 130";
  }
#endif
  if(!gl) {
    SDL_Log("gl context failed: %s", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(window, gl);
  if(!shader.init()) { SDL_Log("gl shader pipeline unavailable on this driver"); }
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
  // would blend into the background.
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE,
               nullptr);
  return true;
}

namespace {
// resolves a remembered device name back to an id; falls back to the system
// default when the name is empty or the device is gone (renumbered on reboot)
SDL_AudioDeviceID findPlaybackDevice(const std::string& name) {
  if(name.empty()) { return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK; }

  int count = 0;
  SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
  SDL_AudioDeviceID found = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
  for(int i = 0; i < count; i++) {
    const char* devName = SDL_GetAudioDeviceName(ids[i]);
    if(devName && name == devName) {
      found = ids[i];
      break;
    }
  }
  if(ids) { SDL_free(ids); }
  return found;
}
}  // namespace

bool Shell::initAudio(const Settings& settings) {
  SDL_AudioSpec spec{};
  spec.format = SDL_AUDIO_F32;
  spec.channels = 2;
  spec.freq = AudioRate;
  audio =
      SDL_OpenAudioDeviceStream(findPlaybackDevice(settings.audioDevice), &spec, nullptr, nullptr);
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
  if(previous) { SDL_DestroyAudioStream(previous); }
  audioGain = -1.0f;  // force the new stream to pick up the current gain
  return true;
}

std::vector<std::string> Shell::listPlaybackDevices() {
  std::vector<std::string> names;
  int count = 0;
  SDL_AudioDeviceID* ids = SDL_GetAudioPlaybackDevices(&count);
  for(int i = 0; i < count; i++) {
    if(const char* name = SDL_GetAudioDeviceName(ids[i])) { names.emplace_back(name); }
  }
  if(ids) { SDL_free(ids); }
  return names;
}

bool Shell::init(const Settings& settings) {
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
    return false;
  }
  if(!initVideo()) { return false; }
  moveToDisplay(settings);  // SDL places a new window on the primary display
  return initAudio(settings);
}

void Shell::shutdown() {
  if(audio) { SDL_DestroyAudioStream(audio); }
  if(gl) { shader.shutdown(); }
  if(texture) { glDeleteTextures(1, &texture); }
  if(gl) { SDL_GL_DestroyContext(gl); }
  if(window) { SDL_DestroyWindow(window); }
  SDL_Quit();
}

// Audio is the master clock; draining to a byte backlog paces NTSC and PAL
// alike.
int Shell::paceTarget(const Settings& settings) const {
  return settings.latencyMs * AudioRate / 1000 * 2 * (int)sizeof(float);
}

int Shell::displayFrameMs() const {
  const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
  if(!mode || mode->refresh_rate <= 0.0f) { return 16; }
  return SDL_max(1, (int)(1000.0f / mode->refresh_rate));
}

void Shell::pace(const Settings& settings) {
  constexpr int bytesPerSecond = AudioRate * 2 * (int)sizeof(float);
  const int target = paceTarget(settings);

  // the backlog is already a duration, so sleep it off in one go rather than
  // polling; the loop only runs again if the sleep came up short
  while(true) {
    const int over = SDL_GetAudioStreamQueued(audio) - target;
    if(over <= 0) { return; }
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureWidth, textureHeight, 0, GL_BGRA,
                 GL_UNSIGNED_BYTE, nullptr);
  }
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, argb);
  // the texture above is oversized, and quark shaders index texels off
  // sourceSize
  shader.pushFrame(argb, width, height);
}

void Shell::pushAudio(const Settings& settings, const float* samples, int frames, float gain,
                      bool unpaced) {
  if(frames <= 0) { return; }
  if(gain != audioGain) {
    audioGain = gain;
    SDL_SetAudioStreamGain(audio, gain);
  }
  // unpaced the emulator outruns the device, so cap the backlog instead
  if(unpaced && SDL_GetAudioStreamQueued(audio) >= paceTarget(settings)) { return; }
  SDL_PutAudioStreamData(audio, samples, frames * 2 * (int)sizeof(float));
}

void Shell::drawGame(const Settings& settings, unsigned tint) {
  if(frameWidth <= 0 || frameHeight <= 0) { return; }

  ImGuiViewport* view = ImGui::GetMainViewport();
  const float availW = view->WorkSize.x, availH = view->WorkSize.y;
  if(availW <= 0.0f || availH <= 0.0f) { return; }

  const float videoW = videoWidth(settings);
  const float videoH = videoHeight(settings);

  float w, h;
  if(settings.windowScale > 0) {
    w = videoW * settings.windowScale;
    h = videoH * settings.windowScale;
  } else if(settings.outputMode == OutputStretch) {
    w = availW;
    h = availH;
  } else {
    const float fit = fitScale(settings, availW, availH);
    // below 1x there is no whole multiple to round to, so Center scales instead
    const float scale = settings.outputMode == OutputCenter && fit >= 1.0f ? SDL_floorf(fit) : fit;
    w = videoW * scale;
    h = videoH * scale;
  }
  drawWidth = (int)(w + 0.5f);
  drawHeight = (int)(h + 0.5f);

  const ImVec2 p0(view->WorkPos.x + (availW - w) / 2.0f, view->WorkPos.y + (availH - h) / 2.0f);
  const ImVec2 p1(p0.x + w, p0.y + h);
  drawX = p0.x;
  drawY = p0.y;

  GLuint present = texture;
  // Half a texel in on every side: the texture is larger than the frame, so
  // CLAMP_TO_EDGE guards its edge, not the picture's, and sampling right at
  // frameWidth would blend the last column with never-written texels.
  ImVec2 uv0(0.5f / textureWidth, 0.5f / textureHeight);
  ImVec2 uv1((frameWidth - 0.5f) / textureWidth, (frameHeight - 0.5f) / textureHeight);
  GLint filter = settings.linearFilter ? GL_LINEAR : GL_NEAREST;

  if(shader.active()) {
    // the chain renders at device resolution, which is what a CRT mask needs
    const float dpi = pixelScale();
    const GLuint output =
        shader.render(SDL_max(1, (int)(w * dpi + 0.5f)), SDL_max(1, (int)(h * dpi + 0.5f)));
    if(output) {
      present = output;
      uv0 = ImVec2(0.0f, 0.0f);
      uv1 = ImVec2(1.0f, 1.0f);
      filter = (GLint)shader.outputFilter;
    }
  }

  glBindTexture(GL_TEXTURE_2D, present);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  ImGui::GetBackgroundDrawList(view)->AddImage((ImTextureID)(intptr_t)present, p0, p1, uv0, uv1,
                                               (ImU32)tint);
}

float Shell::pixelScale() const {
  int points = 0, pixels = 0;
  SDL_GetWindowSize(window, &points, nullptr);
  SDL_GetWindowSizeInPixels(window, &pixels, nullptr);
  return points > 0 ? (float)pixels / points : 1.0f;
}

void Shell::shrinkToFit(const Settings& settings) {
  if(fullscreen()) { return; }
  SDL_RestoreWindow(window);  // a maximized window ignores a resize

  const ImGuiViewport* view = ImGui::GetMainViewport();
  // the bars sit outside the work area, so their height has to be added back
  const float chrome = view->Size.y - view->WorkSize.y;
  const float videoW = videoWidth(settings);
  const float videoH = videoHeight(settings);

  float scale = (float)settings.windowScale;
  if(scale <= 0.0f) {
    // no fixed scale picked, so shrink to the whole multiple already on screen
    scale = SDL_max(1.0f, SDL_floorf(fitScale(settings, view->WorkSize.x, view->WorkSize.y)));
  }
  SDL_SetWindowSize(window, (int)(videoW * scale + 0.5f), (int)(videoH * scale + chrome + 0.5f));
}

namespace {
// resolves a remembered display name back to an id, as findPlaybackDevice does
// for audio; 0 when the name is empty or that monitor has been unplugged
SDL_DisplayID findDisplay(const std::string& name) {
  if(name.empty()) { return 0; }

  int count = 0;
  SDL_DisplayID* ids = SDL_GetDisplays(&count);
  SDL_DisplayID found = 0;
  for(int i = 0; i < count; i++) {
    const char* displayName = SDL_GetDisplayName(ids[i]);
    if(displayName && name == displayName) {
      found = ids[i];
      break;
    }
  }
  if(ids) { SDL_free(ids); }
  return found;
}
}  // namespace

std::vector<std::string> Shell::listDisplays() {
  std::vector<std::string> names;
  int count = 0;
  SDL_DisplayID* ids = SDL_GetDisplays(&count);
  for(int i = 0; i < count; i++) {
    if(const char* name = SDL_GetDisplayName(ids[i])) { names.emplace_back(name); }
  }
  if(ids) { SDL_free(ids); }
  return names;
}

// an unplugged monitor falls back to the window's own, rather than parking the
// window off the desktop
SDL_DisplayID Shell::chosenDisplay(const Settings& settings) const {
  const SDL_DisplayID display = findDisplay(settings.displayName);
  return display ? display : SDL_GetDisplayForWindow(window);
}

void Shell::centerOn(SDL_DisplayID display) {
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED_DISPLAY(display),
                        SDL_WINDOWPOS_CENTERED_DISPLAY(display));
}

void Shell::moveToDisplay(const Settings& settings) {
  if(settings.displayName.empty()) { return; }
  if(fullscreen()) { return; }
  const SDL_DisplayID display = chosenDisplay(settings);
  if(display == SDL_GetDisplayForWindow(window)) { return; }
  centerOn(display);
}

void Shell::center(const Settings& settings) {
  if(fullscreen()) { return; }
  SDL_RestoreWindow(window);  // a maximized window already fills the display
  centerOn(chosenDisplay(settings));
}

// the window shrinkToFit would build, measured against the display it sits on
int Shell::maxScale(const Settings& settings) const {
  const ImGuiViewport* view = ImGui::GetMainViewport();
  const float chrome = view->Size.y - view->WorkSize.y;
  SDL_Rect usable{};
  SDL_GetDisplayUsableBounds(chosenDisplay(settings), &usable);

  return SDL_clamp((int)fitScale(settings, (float)usable.w, usable.h - chrome), 1, MaxWindowScale);
}

namespace {
bool saveBmp(const void* pixels, int w, int h, const std::string& path, bool flip) {
  SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, (void*)pixels,
                                               w * (int)sizeof(uint32_t));
  if(!surface) { return false; }
  const bool ok =
      (!flip || SDL_FlipSurface(surface, SDL_FLIP_VERTICAL)) && SDL_SaveBMP(surface, path.c_str());
  SDL_DestroySurface(surface);
  return ok;
}
}  // namespace

bool Shell::saveFrame(const std::string& path) const {
  if(!lastPixels || frameWidth <= 0 || frameHeight <= 0) { return false; }
  return saveBmp(lastPixels, frameWidth, frameHeight, path, false);
}

// Reads the displayed game rectangle after the background draw list has been
// rendered by itself. This keeps Lua drawings while excluding frontend chrome.
bool Shell::saveGameView(const std::string& path) const {
  if(drawWidth <= 0 || drawHeight <= 0) { return false; }

  int windowW = 0, windowH = 0, pixelW = 0, pixelH = 0;
  SDL_GetWindowSize(window, &windowW, &windowH);
  SDL_GetWindowSizeInPixels(window, &pixelW, &pixelH);
  if(windowW <= 0 || windowH <= 0 || pixelW <= 0 || pixelH <= 0) { return false; }

  const ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;
  const float sx = (float)pixelW / windowW;
  const float sy = (float)pixelH / windowH;
  const float localX = drawX - viewportPos.x;
  const float localY = drawY - viewportPos.y;
  const int left = SDL_clamp((int)SDL_floorf(localX * sx), 0, pixelW);
  const int top = SDL_clamp((int)SDL_floorf(localY * sy), 0, pixelH);
  const int right = SDL_clamp((int)SDL_ceilf((localX + drawWidth) * sx), 0, pixelW);
  const int bottom = SDL_clamp((int)SDL_ceilf((localY + drawHeight) * sy), 0, pixelH);
  const int w = right - left, h = bottom - top;
  if(w <= 0 || h <= 0) { return false; }

  std::vector<uint32_t> pixels((size_t)w * h);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(left, pixelH - bottom, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
  return saveBmp(pixels.data(), w, h, path, true);
}

// GL's origin is bottom-left, so the rows come back upside down
bool Shell::saveWindow(const std::string& path) const {
  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(window, &w, &h);
  if(w <= 0 || h <= 0) { return false; }

  std::vector<uint32_t> pixels((size_t)w * h);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());

  return saveBmp(pixels.data(), w, h, path, true);
}

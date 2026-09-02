#pragma once

#include "emucore/emucore.hpp"
#include "settings.hpp"
#include "shader.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <string>
#include <vector>

constexpr int AudioRate = 48000;

// the canonical frame all geometry uses: a filter changes resolution, not shape
inline float videoWidth(const Settings& settings) {
  return 256.0f * (settings.aspectCorrect ? 8.0f / 7.0f : 1.0f);
}

inline float videoHeight(const Settings& settings) {
  return settings.overscanCrop ? 224.0f : 240.0f;
}

// how many times the frame fits in a box, whichever axis runs out first
inline float fitScale(const Settings& settings, float availWidth, float availHeight) {
  return SDL_min(availWidth / videoWidth(settings), availHeight / videoHeight(settings));
}

struct Shell {
  SDL_Window* window = nullptr;
  SDL_GLContext gl = nullptr;
  GLuint texture = 0;
  SDL_AudioStream* audio = nullptr;
  Shader shader;
  // what imgui's backend is told; follows whichever context we ended up with
  const char* glslVersion = "#version 150";

  // valid until the core's next frame, which is enough for a screenshot
  const uint32_t* lastPixels = nullptr;
  int frameWidth = 0;
  int frameHeight = 0;
  // the on-screen rectangle, after scaling
  int drawWidth = 0;
  int drawHeight = 0;
  float drawX = 0.0f;
  float drawY = 0.0f;
  // grows to fit HD mode 7, whose frames outrun the filters' worst case
  int textureWidth = 0;
  int textureHeight = 0;
  float audioGain = 1.0f;

  bool initVideo();
  bool initAudio(const Settings& settings);
  // reopens the stream on a different device; the new stream starts with an
  // empty backlog, so the loop runs briefly fast until it refills
  bool reopenAudio(const Settings& settings);
  bool init(const Settings& settings);
  void shutdown();
  int paceTarget(const Settings& settings) const;
  // ms per frame of whatever display the window is on, for redraw throttling
  int displayFrameMs() const;
  void pace(const Settings& settings);
  void pushVideo(const uint32_t* argb, int width, int height);

  // re-uploads the frame on screen, for a shader switched on while paused
  void repushVideo() { shader.pushFrame(lastPixels, frameWidth, frameHeight); }

  // unpaced means no audio clock, so a full backlog is dropped rather than
  // queued
  void pushAudio(const Settings& settings, const float* samples, int frames, float gain,
                 bool unpaced);
  // playback device names, for the settings picker
  static std::vector<std::string> listPlaybackDevices();
  // display names, likewise
  static std::vector<std::string> listDisplays();

  bool fullscreen() const { return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0; }

  void clearFrame() {
    lastPixels = nullptr;
    frameWidth = frameHeight = 0;
    drawWidth = drawHeight = 0;
    drawX = drawY = 0.0f;
  }

  // window pixels per logical point, for shading at the display's resolution
  float pixelScale() const;
  // tint multiplies the frame, which is how the idle dimming is applied
  void drawGame(const Settings& settings, unsigned tint = 0xffffffffu);
  void shrinkToFit(const Settings& settings);
  // recentres on the configured display, or the one the window is already on
  void center(const Settings& settings);
  // moves the window to the configured display, leaving its size alone
  void moveToDisplay(const Settings& settings);
  // largest whole multiple whose window still fits the display, chrome included
  int maxScale(const Settings& settings) const;
  bool saveFrame(const std::string& path) const;
  bool saveGameView(const std::string& path) const;
  bool saveWindow(const std::string& path) const;

 private:
  // the configured display while it is still attached, else the window's own
  SDL_DisplayID chosenDisplay(const Settings& settings) const;
  void centerOn(SDL_DisplayID display);
};

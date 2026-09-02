#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <string>
#include <vector>

// one manifest `settings` entry, substituted wherever a source says `#in
// <name>`
struct ShaderParam {
  std::string name;
  std::string value;
  std::string stock;  // the manifest's own value, for restoring
};

struct ShaderTexture {
  GLuint texture = 0;
  int width = 0;
  int height = 0;
  GLuint format = GL_RGBA8;
  GLuint filter = GL_LINEAR;
  GLuint wrap = GL_CLAMP_TO_BORDER;
};

struct ShaderPass {
  GLuint program = 0;
  GLuint vertex = 0, geometry = 0, fragment = 0;
  GLuint framebuffer = 0;
  ShaderTexture target;
  std::vector<ShaderTexture> pixmaps;
  int absoluteWidth = 0, absoluteHeight = 0;
  double relativeWidth = 0.0, relativeHeight = 0.0;
  int phase = 0, modulo = 300;
  GLint attribVertex = -1, attribPosition = -1, attribTexCoord = -1;
};

// a quark-format multipass chain: the same .shader packages target-bsnes loads
struct Shader {
  // resolves the GL entry points; false when the context is too old to run any
  bool init();

  bool supported() const { return entryPoints; }

  bool active() const { return !passes.empty(); }

  size_t passCount() const { return passes.size(); }

  // `folder` is a .shader package; overrides replace manifest `settings` values
  bool load(const std::string& folder, const std::vector<ShaderParam>& overrides);
  void unload();
  // releases the shared quad as well; the GL context must still be current
  void shutdown();

  // uploads one emulated frame and advances the history ring
  void pushFrame(const uint32_t* argb, int width, int height);
  bool hasFrame() const;
  // runs the chain; returns the texture to present, or 0 when there is nothing
  GLuint render(int outputWidth, int outputHeight);

  std::string path;     // the loaded package, empty when none
  std::string failure;  // why the last load or render gave up
  std::string log;      // compiler and linker output, for the UI
  std::vector<ShaderParam> params;

  // chain runs, not redraws; the UI never reads it, the shader test does
  uint64_t renderCount = 0;

  GLuint outputFilter = GL_LINEAR;
  int outputWidth = 0,
      outputHeight = 0;  // size of the texture render() returned

 private:
  bool entryPoints = false;
  GLuint vao = 0, vbo[3] = {};
  int maxUnits = 8;
  GLuint cached = 0;
  int cachedWidth = 0, cachedHeight = 0;
  bool fresh = false;  // a frame has arrived that the chain has not seen
  std::vector<ShaderPass> passes;
  // ring[head] is the newest frame; walking forward goes back in time
  std::vector<ShaderTexture> ring;
  int head = 0;
  GLuint inputFormat = GL_RGBA8, inputFilter = GL_LINEAR, inputWrap = GL_CLAMP_TO_BORDER;

  bool buildPass(ShaderPass& pass, const struct ShaderNode& node, const std::string& folder);
  void sizeTarget(ShaderPass& pass, int width, int height);
  void drawQuad(const ShaderPass& pass, int width, int height);
  void releasePass(ShaderPass& pass);
};

// the .shader packages in `dir`, by folder name, sorted
std::vector<std::string> shaderList(const std::string& dir);
// "CRT-Geom.shader" -> "CRT-Geom"
std::string shaderLabel(const std::string& folder);

// decodes a PNG to tightly packed RGBA8; null when unsupported or malformed
uint8_t* decodeImage(const void* data, size_t size, int& width, int& height);
void freeImage(void* pixels);

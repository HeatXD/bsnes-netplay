#include "shader.hpp"

#include "util.hpp"

#include <algorithm>

namespace {

// only GL 1.1 is exported on Windows, so the rest is resolved through SDL
struct GlApi {
  PFNGLCREATESHADERPROC CreateShader;
  PFNGLSHADERSOURCEPROC ShaderSource;
  PFNGLCOMPILESHADERPROC CompileShader;
  PFNGLGETSHADERIVPROC GetShaderiv;
  PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
  PFNGLATTACHSHADERPROC AttachShader;
  PFNGLDETACHSHADERPROC DetachShader;
  PFNGLDELETESHADERPROC DeleteShader;
  PFNGLCREATEPROGRAMPROC CreateProgram;
  PFNGLLINKPROGRAMPROC LinkProgram;
  PFNGLGETPROGRAMIVPROC GetProgramiv;
  PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
  PFNGLUSEPROGRAMPROC UseProgram;
  PFNGLDELETEPROGRAMPROC DeleteProgram;
  PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
  PFNGLUNIFORM1IPROC Uniform1i;
  PFNGLUNIFORM4FPROC Uniform4f;
  PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
  PFNGLGETATTRIBLOCATIONPROC GetAttribLocation;
  PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
  PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
  PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
  PFNGLGENBUFFERSPROC GenBuffers;
  PFNGLBINDBUFFERPROC BindBuffer;
  PFNGLBUFFERDATAPROC BufferData;
  PFNGLDELETEBUFFERSPROC DeleteBuffers;
  PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
  PFNGLBINDVERTEXARRAYPROC BindVertexArray;
  PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
  PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
  PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
  PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
  PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
  PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;
  PFNGLACTIVETEXTUREPROC ActiveTexture;
};

GlApi gl{};

bool loadGl() {
  bool ok = true;
  auto get = [&](const char* name) {
    SDL_FunctionPointer fn = SDL_GL_GetProcAddress(name);
    if(!fn) ok = false;
    return fn;
  };
#define GET(field, name) gl.field = (decltype(gl.field))get(name)
  GET(CreateShader, "glCreateShader");
  GET(ShaderSource, "glShaderSource");
  GET(CompileShader, "glCompileShader");
  GET(GetShaderiv, "glGetShaderiv");
  GET(GetShaderInfoLog, "glGetShaderInfoLog");
  GET(AttachShader, "glAttachShader");
  GET(DetachShader, "glDetachShader");
  GET(DeleteShader, "glDeleteShader");
  GET(CreateProgram, "glCreateProgram");
  GET(LinkProgram, "glLinkProgram");
  GET(GetProgramiv, "glGetProgramiv");
  GET(GetProgramInfoLog, "glGetProgramInfoLog");
  GET(UseProgram, "glUseProgram");
  GET(DeleteProgram, "glDeleteProgram");
  GET(GetUniformLocation, "glGetUniformLocation");
  GET(Uniform1i, "glUniform1i");
  GET(Uniform4f, "glUniform4f");
  GET(UniformMatrix4fv, "glUniformMatrix4fv");
  GET(GetAttribLocation, "glGetAttribLocation");
  GET(EnableVertexAttribArray, "glEnableVertexAttribArray");
  GET(DisableVertexAttribArray, "glDisableVertexAttribArray");
  GET(VertexAttribPointer, "glVertexAttribPointer");
  GET(GenBuffers, "glGenBuffers");
  GET(BindBuffer, "glBindBuffer");
  GET(BufferData, "glBufferData");
  GET(DeleteBuffers, "glDeleteBuffers");
  GET(GenVertexArrays, "glGenVertexArrays");
  GET(BindVertexArray, "glBindVertexArray");
  GET(DeleteVertexArrays, "glDeleteVertexArrays");
  GET(GenFramebuffers, "glGenFramebuffers");
  GET(BindFramebuffer, "glBindFramebuffer");
  GET(FramebufferTexture2D, "glFramebufferTexture2D");
  GET(CheckFramebufferStatus, "glCheckFramebufferStatus");
  GET(DeleteFramebuffers, "glDeleteFramebuffers");
  GET(ActiveTexture, "glActiveTexture");
#undef GET
  return ok;
}

}  // namespace

// the subset of BML the manifests use: indentation nests, one name: value a line
struct ShaderNode {
  std::string name;
  std::string value;
  std::vector<ShaderNode> children;

  const ShaderNode* find(const char* key) const {
    for(const ShaderNode& child : children) {
      if(child.name == key) return &child;
    }
    return nullptr;
  }
  std::string text(const char* key) const {
    const ShaderNode* child = find(key);
    return child ? child->value : std::string();
  }
};

namespace {

struct ManifestLine {
  int indent = 0;
  std::string name, value;
};

std::string trimmed(const std::string& text) {
  const size_t first = text.find_first_not_of(" \t\r");
  if(first == std::string::npos) return {};
  return text.substr(first, text.find_last_not_of(" \t\r") + 1 - first);
}

std::vector<ManifestLine> splitManifest(const std::string& text) {
  std::vector<ManifestLine> lines;
  size_t pos = 0;
  while(pos < text.size()) {
    size_t end = text.find('\n', pos);
    if(end == std::string::npos) end = text.size();
    const std::string raw = text.substr(pos, end - pos);
    pos = end + 1;

    const size_t first = raw.find_first_not_of(" \t\r");
    if(first == std::string::npos) continue;
    if(raw.compare(first, 2, "//") == 0) continue;

    ManifestLine line;
    line.indent = (int)first;
    const std::string body = trimmed(raw);
    const size_t colon = body.find(':');
    if(colon == std::string::npos) {
      line.name = body;
    } else {
      line.name = trimmed(body.substr(0, colon));
      line.value = trimmed(body.substr(colon + 1));
    }
    lines.push_back(line);
  }
  return lines;
}

// indentation counts characters, so a tab-indented manifest nests like a spaced one
size_t buildNodes(const std::vector<ManifestLine>& lines, size_t index, int indent,
                  std::vector<ShaderNode>& out) {
  while(index < lines.size() && lines[index].indent >= indent) {
    if(out.empty() && lines[index].indent > indent) break;
    if(lines[index].indent > indent) {
      index = buildNodes(lines, index, lines[index].indent, out.back().children);
      continue;
    }
    ShaderNode node;
    node.name = lines[index].name;
    node.value = lines[index].value;
    out.push_back(std::move(node));
    index++;
  }
  return index;
}

ShaderNode parseManifest(const std::string& text) {
  const std::vector<ManifestLine> lines = splitManifest(text);
  ShaderNode root;
  buildNodes(lines, 0, 0, root.children);
  return root;
}

GLuint parseFormat(const std::string& format) {
  if(format == "r32i") return GL_R32I;
  if(format == "r32ui") return GL_R32UI;
  if(format == "rgba8") return GL_RGBA8;
  if(format == "rgb10a2") return GL_RGB10_A2;
  if(format == "rgba12") return GL_RGBA12;
  if(format == "rgba16") return GL_RGBA16;
  if(format == "rgba16f") return GL_RGBA16F;
  if(format == "rgba32f") return GL_RGBA32F;
  // srgb8 falls through: those packages linearize themselves, and ruby agrees
  return GL_RGBA8;
}

GLuint uploadFormat(GLuint format) {
  if(format == GL_R32I || format == GL_R32UI) return GL_RED_INTEGER;
  return GL_BGRA;
}

GLuint uploadType(GLuint format) {
  if(format == GL_R32I || format == GL_R32UI) return GL_UNSIGNED_INT;
  if(format == GL_RGB10_A2) return GL_UNSIGNED_INT_2_10_10_10_REV;
  return GL_UNSIGNED_INT_8_8_8_8_REV;
}

GLuint parseFilter(const std::string& filter) {
  return filter == "nearest" ? GL_NEAREST : GL_LINEAR;
}

GLuint parseWrap(const std::string& wrap) {
  if(wrap == "edge") return GL_CLAMP_TO_EDGE;
  if(wrap == "repeat") return GL_REPEAT;
  return GL_CLAMP_TO_BORDER;
}

// "320 px" is absolute, "6.25%" is relative to the pass feeding this one
void parseSize(const std::string& text, int& absolute, double& relative) {
  absolute = 0;
  relative = 0.0;
  if(text.empty()) return;
  if(text.back() == '%') {
    relative = SDL_atof(text.substr(0, text.size() - 1).c_str()) / 100.0;
  } else {
    absolute = SDL_atoi(text.c_str());
  }
}

void applyParameters(GLuint filter, GLuint wrap) {
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLint)filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLint)filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLint)wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLint)wrap);
}

const char* const DefaultVertex =
  "#version 150\n"
  "in vec4 position;\n"
  "in vec2 texCoord;\n"
  "out Vertex { vec2 texCoord; } vertexOut;\n"
  "void main() {\n"
  "  gl_Position = position;\n"
  "  vertexOut.texCoord = texCoord;\n"
  "}\n";

const char* const DefaultFragment =
  "#version 150\n"
  "uniform sampler2D source[];\n"
  "in Vertex { vec2 texCoord; };\n"
  "out vec4 fragColor;\n"
  "void main() { fragColor = texture(source[0], texCoord); }\n";

// `#in name` becomes that setting's #define; an unknown one blanks the line
std::string substitute(const std::string& source, const std::vector<ShaderParam>& params) {
  std::string out;
  out.reserve(source.size());
  size_t pos = 0;
  while(true) {
    size_t end = source.find('\n', pos);
    const bool last = end == std::string::npos;
    if(last) end = source.size();
    std::string line = source.substr(pos, end - pos);

    std::string probe = line;
    if(const size_t comment = probe.find("//"); comment != std::string::npos) {
      probe.resize(comment);
    }
    probe = trimmed(probe);
    if(probe.compare(0, 4, "#in ") == 0) {
      const std::string name = trimmed(probe.substr(4));
      line.clear();
      for(const ShaderParam& param : params) {
        if(param.name == name) { line = "#define " + name + " " + param.value; break; }
      }
    }
    out += line;
    if(last) break;
    out += '\n';
    pos = end + 1;
  }
  return out;
}

std::string shaderInfoLog(GLuint shader) {
  GLint length = 0;
  gl.GetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
  if(length <= 1) return {};
  std::string text((size_t)length, '\0');
  gl.GetShaderInfoLog(shader, length, &length, text.data());
  text.resize((size_t)length);
  return text;
}

std::string programInfoLog(GLuint program) {
  GLint length = 0;
  gl.GetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
  if(length <= 1) return {};
  std::string text((size_t)length, '\0');
  gl.GetProgramInfoLog(program, length, &length, text.data());
  text.resize((size_t)length);
  return text;
}

}  // namespace

std::vector<std::string> shaderList(const std::string& dir) {
  std::vector<std::string> found;
  if(dir.empty()) return found;

  int count = 0;
  char** names = SDL_GlobDirectory(dir.c_str(), "*.shader", SDL_GLOB_CASEINSENSITIVE, &count);
  if(!names) return found;
  for(int i = 0; i < count; i++) {
    if(isDirectory(dir + "/" + names[i])) found.emplace_back(names[i]);
  }
  SDL_free(names);
  std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
    return SDL_strcasecmp(a.c_str(), b.c_str()) < 0;
  });
  return found;
}

std::string shaderLabel(const std::string& folder) {
  std::string name = fileName(folder);
  const std::string suffix = ".shader";
  if(name.size() > suffix.size()
  && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
    name.resize(name.size() - suffix.size());
  }
  return name;
}

bool Shader::init() {
  if(entryPoints) return true;
  if(!loadGl()) return false;

  GLint units = 0;
  glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
  maxUnits = units > 0 ? (int)units : 8;

  gl.GenVertexArrays(1, &vao);
  gl.GenBuffers(3, vbo);
  entryPoints = vao != 0;
  return entryPoints;
}

void Shader::releasePass(ShaderPass& pass) {
  for(ShaderTexture& pixmap : pass.pixmaps) {
    if(pixmap.texture) glDeleteTextures(1, &pixmap.texture);
  }
  pass.pixmaps.clear();
  if(pass.target.texture) glDeleteTextures(1, &pass.target.texture);
  if(pass.framebuffer) gl.DeleteFramebuffers(1, &pass.framebuffer);
  for(GLuint* stage : {&pass.vertex, &pass.geometry, &pass.fragment}) {
    if(*stage) { gl.DetachShader(pass.program, *stage); gl.DeleteShader(*stage); }
  }
  if(pass.program) gl.DeleteProgram(pass.program);
  pass = ShaderPass{};
}

void Shader::unload() {
  if(entryPoints) {
    gl.UseProgram(0);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    for(ShaderPass& pass : passes) releasePass(pass);
    for(ShaderTexture& frame : ring) {
      if(frame.texture) glDeleteTextures(1, &frame.texture);
    }
  }
  passes.clear();
  ring.clear();
  params.clear();
  head = 0;
  path.clear();
  cached = 0;
  cachedWidth = cachedHeight = 0;
  fresh = false;
  renderCount = 0;
  outputWidth = outputHeight = 0;
  outputFilter = GL_LINEAR;
  inputFormat = GL_RGBA8;
  inputFilter = GL_LINEAR;
  inputWrap = GL_CLAMP_TO_BORDER;
}

void Shader::shutdown() {
  unload();
  if(!entryPoints) return;
  gl.DeleteBuffers(3, vbo);
  gl.DeleteVertexArrays(1, &vao);
  for(GLuint& buffer : vbo) buffer = 0;
  vao = 0;
  entryPoints = false;
}

bool Shader::load(const std::string& folder, const std::vector<ShaderParam>& overrides) {
  failure.clear();
  log.clear();
  unload();
  if(folder.empty()) return true;
  if(!entryPoints) {
    failure = "this OpenGL context is too old for GLSL shaders";
    return false;
  }

  const std::string base = pakPath(folder);
  const std::string text = readText(base + "manifest.bml");
  if(text.empty()) {
    failure = "no manifest.bml in " + folder;
    return false;
  }
  const ShaderNode manifest = parseManifest(text);

  if(const ShaderNode* node = manifest.find("settings")) {
    for(const ShaderNode& child : node->children) {
      ShaderParam param{child.name, child.value, child.value};
      for(const ShaderParam& override : overrides) {
        if(override.name == param.name) { param.value = override.value; break; }
      }
      params.push_back(param);
    }
  }

  int historySize = 0;
  if(const ShaderNode* node = manifest.find("input")) {
    if(const ShaderNode* child = node->find("history")) {
      historySize = SDL_atoi(child->value.c_str());
    }
    if(const ShaderNode* child = node->find("format")) inputFormat = parseFormat(child->value);
    if(const ShaderNode* child = node->find("filter")) inputFilter = parseFilter(child->value);
    if(const ShaderNode* child = node->find("wrap")) inputWrap = parseWrap(child->value);
  }
  bool outputFilterSet = false;
  if(const ShaderNode* node = manifest.find("output")) {
    if(const ShaderNode* child = node->find("filter")) {
      outputFilter = parseFilter(child->value);
      outputFilterSet = true;
    }
  }

  for(const ShaderNode& node : manifest.children) {
    if(node.name != "program") continue;
    passes.emplace_back();
    if(!buildPass(passes.back(), node, base)) {
      unload();
      return false;
    }
  }
  if(passes.empty()) {
    failure = shaderLabel(folder) + " declares no program";
    unload();
    return false;
  }

  // without an output node ruby blits with the last pass's own filter
  if(!outputFilterSet) outputFilter = passes.back().target.filter;

  ring.resize((size_t)SDL_clamp(historySize, 0, 32) + 1);
  for(ShaderTexture& frame : ring) {
    frame.format = inputFormat;
    frame.filter = inputFilter;
    frame.wrap = inputWrap;
    glGenTextures(1, &frame.texture);
  }
  head = 0;
  path = folder;
  return true;
}

bool Shader::buildPass(ShaderPass& pass, const ShaderNode& node, const std::string& folder) {
  pass.target.filter = parseFilter(node.text("filter"));
  pass.target.wrap = parseWrap(node.text("wrap"));
  pass.target.format = parseFormat(node.text("format"));
  const int modulo = SDL_atoi(node.text("modulo").c_str());
  pass.modulo = modulo > 0 ? modulo : 300;
  parseSize(node.text("width"), pass.absoluteWidth, pass.relativeWidth);
  parseSize(node.text("height"), pass.absoluteHeight, pass.relativeHeight);

  pass.program = gl.CreateProgram();
  gl.GenFramebuffers(1, &pass.framebuffer);

  struct Stage { const char* key; GLenum type; const char* fallback; GLuint* out; };
  const Stage stages[] = {
    {"vertex",   GL_VERTEX_SHADER,   DefaultVertex,   &pass.vertex},
    {"geometry", GL_GEOMETRY_SHADER, nullptr,         &pass.geometry},
    {"fragment", GL_FRAGMENT_SHADER, DefaultFragment, &pass.fragment},
  };
  for(const Stage& stage : stages) {
    const std::string file = node.text(stage.key);
    const std::string label = file.empty() ? std::string(stage.key) : file;
    std::string source;
    if(!file.empty()) {
      source = readText(folder + file);
      if(source.empty()) {
        failure = "cannot read " + file;
        return false;
      }
      source = substitute(source, params);
    } else if(stage.fallback) {
      source = stage.fallback;
    } else {
      continue;
    }

    const GLuint shader = gl.CreateShader(stage.type);
    const char* text = source.c_str();
    gl.ShaderSource(shader, 1, &text, nullptr);
    gl.CompileShader(shader);
    GLint status = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(const std::string info = shaderInfoLog(shader); !info.empty()) {
      log += label + ":\n" + info + "\n";
    }
    if(status == GL_FALSE) {
      failure = label + " failed to compile";
      gl.DeleteShader(shader);
      return false;
    }
    gl.AttachShader(pass.program, shader);
    *stage.out = shader;
  }

  gl.LinkProgram(pass.program);
  GLint status = GL_FALSE;
  gl.GetProgramiv(pass.program, GL_LINK_STATUS, &status);
  if(const std::string info = programInfoLog(pass.program); !info.empty()) {
    log += "link:\n" + info + "\n";
  }
  if(status == GL_FALSE) {
    failure = "link failed in " + shaderLabel(folder);
    return false;
  }

  pass.attribVertex = gl.GetAttribLocation(pass.program, "vertex");
  pass.attribPosition = gl.GetAttribLocation(pass.program, "position");
  pass.attribTexCoord = gl.GetAttribLocation(pass.program, "texCoord");

  for(const ShaderNode& child : node.children) {
    if(child.name != "pixmap") continue;
    ShaderTexture pixmap;
    pixmap.filter = child.find("filter") ? parseFilter(child.text("filter")) : pass.target.filter;
    pixmap.wrap = child.find("wrap") ? parseWrap(child.text("wrap")) : pass.target.wrap;
    pixmap.format = child.find("format") ? parseFormat(child.text("format")) : pass.target.format;

    const std::vector<uint8_t> file = readBytes(folder + child.value);
    uint8_t* pixels = file.empty() ? nullptr
                    : decodeImage(file.data(), file.size(), pixmap.width, pixmap.height);
    if(!pixels) {
      log += "pixmap " + child.value + " could not be read\n";
      continue;
    }
    glGenTextures(1, &pixmap.texture);
    glBindTexture(GL_TEXTURE_2D, pixmap.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)pixmap.format, pixmap.width, pixmap.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    applyParameters(pixmap.filter, pixmap.wrap);
    freeImage(pixels);
    pass.pixmaps.push_back(pixmap);
  }
  return true;
}

bool Shader::hasFrame() const {
  return !ring.empty() && ring[(size_t)head].width > 0;
}

namespace {
void uploadFrame(ShaderTexture& frame, const uint32_t* argb, int width, int height) {
  glBindTexture(GL_TEXTURE_2D, frame.texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if(frame.width != width || frame.height != height) {
    frame.width = width;
    frame.height = height;
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint)frame.format, width, height, 0,
                 uploadFormat(frame.format), uploadType(frame.format), argb);
    applyParameters(frame.filter, frame.wrap);
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                    uploadFormat(frame.format), uploadType(frame.format), argb);
  }
}
}  // namespace

void Shader::pushFrame(const uint32_t* argb, int width, int height) {
  if(!entryPoints || ring.empty() || !argb || width <= 0 || height <= 0) return;

  // stepping the head backwards reuses the oldest slot, so no frame is copied
  head = (head + (int)ring.size() - 1) % (int)ring.size();
  uploadFrame(ring[(size_t)head], argb, width, height);
  // an unwritten slot is an incomplete texture with a 1/0 size, so seed them all
  for(ShaderTexture& frame : ring) {
    if(frame.width == 0) uploadFrame(frame, argb, width, height);
  }
  fresh = true;
}

void Shader::sizeTarget(ShaderPass& pass, int width, int height) {
  if(pass.target.texture && pass.target.width == width && pass.target.height == height) return;
  pass.target.width = width;
  pass.target.height = height;

  if(!pass.target.texture) glGenTextures(1, &pass.target.texture);
  glBindTexture(GL_TEXTURE_2D, pass.target.texture);
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint)pass.target.format, width, height, 0,
               uploadFormat(pass.target.format), uploadType(pass.target.format), nullptr);
  applyParameters(pass.target.filter, pass.target.wrap);

  gl.BindFramebuffer(GL_FRAMEBUFFER, pass.framebuffer);
  gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                          pass.target.texture, 0);
}

void Shader::drawQuad(const ShaderPass& pass, int width, int height) {
  glViewport(0, 0, width, height);

  const GLfloat u = (GLfloat)width, v = (GLfloat)height;
  const GLfloat identity[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  const GLfloat projection[16] = {
    2.0f / u, 0.0f,     0.0f, 0.0f,
    0.0f,     2.0f / v, 0.0f, 0.0f,
    0.0f,     0.0f,    -1.0f, 0.0f,
   -1.0f,    -1.0f,     0.0f, 1.0f,
  };
  const GLfloat vertices[16] = {0,0,0,1, u,0,0,1, 0,v,0,1, u,v,0,1};
  const GLfloat positions[16] = {-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};
  const GLfloat texCoords[8] = {0,0, 1,0, 0,1, 1,1};

  auto matrix = [&](const char* name, const GLfloat* values) {
    const GLint location = gl.GetUniformLocation(pass.program, name);
    if(location >= 0) gl.UniformMatrix4fv(location, 1, GL_FALSE, values);
  };
  matrix("modelView", identity);
  matrix("projection", projection);
  matrix("modelViewProjection", projection);

  gl.BindVertexArray(vao);
  struct Attrib { GLint location; const GLfloat* data; GLsizeiptr size; GLint components; };
  const Attrib attribs[] = {
    {pass.attribVertex,   vertices,  (GLsizeiptr)sizeof(vertices),  4},
    {pass.attribPosition, positions, (GLsizeiptr)sizeof(positions), 4},
    {pass.attribTexCoord, texCoords, (GLsizeiptr)sizeof(texCoords), 2},
  };
  for(int i = 0; i < 3; i++) {
    if(attribs[i].location < 0) continue;
    gl.BindBuffer(GL_ARRAY_BUFFER, vbo[i]);
    gl.BufferData(GL_ARRAY_BUFFER, attribs[i].size, attribs[i].data, GL_STREAM_DRAW);
    gl.EnableVertexAttribArray((GLuint)attribs[i].location);
    gl.VertexAttribPointer((GLuint)attribs[i].location, attribs[i].components,
                           GL_FLOAT, GL_FALSE, 0, nullptr);
  }
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  for(const Attrib& attrib : attribs) {
    if(attrib.location >= 0) gl.DisableVertexAttribArray((GLuint)attrib.location);
  }
}

GLuint Shader::render(int width, int height) {
  if(!entryPoints || passes.empty() || !hasFrame() || width <= 0 || height <= 0) {
    outputWidth = outputHeight = 0;
    return 0;
  }
  // bsnes runs the chain once an emulated frame, so a redraw must not advance phase
  const bool advance = fresh;
  if(!advance && cached && width == cachedWidth && height == cachedHeight) return cached;
  fresh = false;
  cachedWidth = width;
  cachedHeight = height;
  renderCount++;

  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_CULL_FACE);

  // source[0] is the newest pass output; the input frame ends up at the far end
  std::vector<ShaderTexture> sources;
  sources.push_back(ring[(size_t)head]);

  for(ShaderPass& pass : passes) {
    int targetWidth = pass.absoluteWidth ? pass.absoluteWidth : width;
    int targetHeight = pass.absoluteHeight ? pass.absoluteHeight : height;
    if(pass.relativeWidth > 0.0) targetWidth = (int)(sources[0].width * pass.relativeWidth);
    if(pass.relativeHeight > 0.0) targetHeight = (int)(sources[0].height * pass.relativeHeight);
    targetWidth = SDL_max(1, targetWidth);
    targetHeight = SDL_max(1, targetHeight);

    sizeTarget(pass, targetWidth, targetHeight);
    gl.BindFramebuffer(GL_FRAMEBUFFER, pass.framebuffer);
    if(gl.CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      const std::string name = shaderLabel(path);
      gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
      unload();
      failure = "this driver cannot render a pass of " + name;
      outputWidth = outputHeight = 0;
      return 0;
    }
    gl.UseProgram(pass.program);

    auto scalar = [&](const char* name, GLint value) {
      const GLint location = gl.GetUniformLocation(pass.program, name);
      if(location >= 0) gl.Uniform1i(location, value);
    };
    auto size4f = [&](const char* name, int w, int h) {
      const GLint location = gl.GetUniformLocation(pass.program, name);
      if(location >= 0) gl.Uniform4f(location, (GLfloat)w, (GLfloat)h, 1.0f / w, 1.0f / h);
    };

    scalar("phase", pass.phase);
    scalar("historyLength", (int)ring.size() - 1);
    scalar("sourceLength", (int)sources.size());
    scalar("pixmapLength", (int)pass.pixmaps.size());
    size4f("targetSize", targetWidth, targetHeight);
    size4f("outputSize", width, height);

    // a sampler the pass never names costs no unit, which is what fits CRT-Royale
    int unit = 0;
    auto bind = [&](const char* array, int index, const ShaderTexture& texture) {
      char name[40];
      SDL_snprintf(name, sizeof(name), "%s[%d]", array, index);
      char sizeName[48];
      SDL_snprintf(sizeName, sizeof(sizeName), "%sSize[%d]", array, index);
      size4f(sizeName, texture.width, texture.height);

      const GLint location = gl.GetUniformLocation(pass.program, name);
      if(location < 0 || unit >= maxUnits) return;
      gl.Uniform1i(location, unit);
      gl.ActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, texture.texture);
      applyParameters(texture.filter, texture.wrap);
      unit++;
    };

    for(size_t i = 1; i < ring.size(); i++) {
      bind("history", (int)i - 1, ring[((size_t)head + i) % ring.size()]);
    }
    for(size_t i = 0; i < sources.size(); i++) bind("source", (int)i, sources[i]);
    for(size_t i = 0; i < pass.pixmaps.size(); i++) bind("pixmap", (int)i, pass.pixmaps[i]);

    drawQuad(pass, targetWidth, targetHeight);
    if(advance) pass.phase = (pass.phase + 1) % pass.modulo;
    sources.insert(sources.begin(), pass.target);
  }

  gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
  gl.UseProgram(0);
  gl.BindVertexArray(0);
  gl.BindBuffer(GL_ARRAY_BUFFER, 0);
  gl.ActiveTexture(GL_TEXTURE0);

  outputWidth = sources[0].width;
  outputHeight = sources[0].height;
  cached = sources[0].texture;
  return cached;
}

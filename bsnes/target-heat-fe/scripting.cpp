#include "scripting.hpp"

#include "app.hpp"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

LuaEngine::LuaEngine(App& app) : app(app) {}
LuaEngine::~LuaEngine() { close(); }

void LuaEngine::close() {
  if(state) lua_close(state);
  state = nullptr;
  active = false;
  commands.clear();
}

bool LuaEngine::failFromStack(const char* prefix) {
  const char* detail = state ? lua_tostring(state, -1) : nullptr;
  lastError = std::string(prefix) + (detail ? detail : "unknown error");
  if(state) lua_pop(state, 1);
  active = false;
  commands.clear();
  app.showMessage(lastError);
  return false;
}

bool LuaEngine::load(const std::string& path) {
  close();
  scriptPath = normalPath(path);
  lastError.clear();

  state = luaL_newstate();
  if(!state) {
    lastError = "Lua: could not create the interpreter";
    app.showMessage(lastError);
    return false;
  }
  const luaL_Reg libraries[] = {
    {LUA_GNAME, luaopen_base},
    {LUA_COLIBNAME, luaopen_coroutine},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_MATHLIBNAME, luaopen_math},
    {LUA_UTF8LIBNAME, luaopen_utf8},
    {nullptr, nullptr},
  };
  for(const luaL_Reg* library = libraries; library->func; library++) {
    luaL_requiref(state, library->name, library->func, true);
    lua_pop(state, 1);
  }
  for(const char* name : {"debug", "io", "os", "package", "require", "dofile", "loadfile"}) {
    lua_pushnil(state);
    lua_setglobal(state, name);
  }
  registerApi();

  if(luaL_loadfile(state, scriptPath.c_str()) != LUA_OK) return failFromStack("Lua load: ");
  if(lua_pcall(state, 0, 0, 0) != LUA_OK) return failFromStack("Lua start: ");

  active = true;
  app.showMessage("running " + fileName(scriptPath));
  return true;
}

LuaEngine& LuaEngine::from(lua_State* state) {
  lua_getfield(state, LUA_REGISTRYINDEX, "heat-fe.lua");
  auto* engine = (LuaEngine*)lua_touserdata(state, -1);
  lua_pop(state, 1);
  return *engine;
}

namespace {
EmuCore::MemoryDomain memoryDomain(lua_State* state, int upvalue) {
  return (EmuCore::MemoryDomain)lua_tointeger(state, lua_upvalueindex(upvalue));
}

uint32_t checkedAddress(lua_State* state, const EmuCore& core,
                        EmuCore::MemoryDomain domain, int bytes) {
  const lua_Integer value = luaL_checkinteger(state, 1);
  const uint32_t size = core.memorySize(domain);
  luaL_argcheck(state, value >= 0 && (lua_Unsigned)value < size, 1,
                "address is outside the memory domain");
  luaL_argcheck(state, (uint32_t)value <= size - bytes, 1,
                "access crosses the end of the memory domain");
  return (uint32_t)value;
}
}

int LuaEngine::readMemory(lua_State* state) {
  LuaEngine& engine = from(state);
  if(!engine.app.core.loaded()) return luaL_error(state, "no game is loaded");

  const int bytes = (int)lua_tointeger(state, lua_upvalueindex(1));
  const auto domain = memoryDomain(state, 3);
  const uint32_t address = checkedAddress(state, engine.app.core, domain, bytes);
  const bool little = lua_toboolean(state, lua_upvalueindex(2));
  lua_Unsigned value = 0;
  for(int i = 0; i < bytes; i++) {
    const int shift = little ? i * 8 : (bytes - i - 1) * 8;
    value |= (lua_Unsigned)engine.app.core.readMemory(domain, address + i) << shift;
  }
  lua_pushinteger(state, (lua_Integer)value);
  return 1;
}

int LuaEngine::writeMemory(lua_State* state) {
  LuaEngine& engine = from(state);
  if(!engine.app.core.loaded()) return luaL_error(state, "no game is loaded");

  const lua_Unsigned value = (lua_Unsigned)luaL_checkinteger(state, 2);
  const int bytes = (int)lua_tointeger(state, lua_upvalueindex(1));
  const auto domain = memoryDomain(state, 3);
  const uint32_t address = checkedAddress(state, engine.app.core, domain, bytes);
  const bool little = lua_toboolean(state, lua_upvalueindex(2));
  for(int i = 0; i < bytes; i++) {
    const int shift = little ? i * 8 : (bytes - i - 1) * 8;
    engine.app.core.writeMemory(domain, address + i, (uint8_t)(value >> shift));
  }
  return 0;
}

int LuaEngine::readBit(lua_State* state) {
  LuaEngine& engine = from(state);
  if(!engine.app.core.loaded()) return luaL_error(state, "no game is loaded");
  const auto domain = memoryDomain(state, 1);
  const uint32_t address = checkedAddress(state, engine.app.core, domain, 1);
  const int bit = (int)luaL_checkinteger(state, 2);
  luaL_argcheck(state, bit >= 0 && bit < 8, 2, "bit must be from 0 to 7");
  lua_pushboolean(state, (engine.app.core.readMemory(domain, address) >> bit) & 1);
  return 1;
}

int LuaEngine::writeBit(lua_State* state) {
  LuaEngine& engine = from(state);
  if(!engine.app.core.loaded()) return luaL_error(state, "no game is loaded");
  const auto domain = memoryDomain(state, 1);
  const uint32_t address = checkedAddress(state, engine.app.core, domain, 1);
  const int bit = (int)luaL_checkinteger(state, 2);
  luaL_argcheck(state, bit >= 0 && bit < 8, 2, "bit must be from 0 to 7");
  uint8_t value = engine.app.core.readMemory(domain, address);
  if(lua_toboolean(state, 3)) value |= (uint8_t)(1u << bit);
  else value &= (uint8_t)~(1u << bit);
  engine.app.core.writeMemory(domain, address, value);
  return 0;
}

namespace {
uint32_t tableColor(lua_State* state, int table, const char* name, uint32_t fallback) {
  if(!lua_istable(state, table)) return fallback;
  lua_getfield(state, table, name);
  const uint32_t value = lua_isinteger(state, -1)
                       ? (uint32_t)lua_tointeger(state, -1) : fallback;
  lua_pop(state, 1);
  return value;
}

float tableNumber(lua_State* state, int table, const char* name, float fallback) {
  if(!lua_istable(state, table)) return fallback;
  lua_getfield(state, table, name);
  const float value = lua_isnumber(state, -1) ? (float)lua_tonumber(state, -1) : fallback;
  lua_pop(state, 1);
  return value;
}

ImU32 imguiColor(uint32_t argb) {
  return IM_COL32((argb >> 16) & 0xff, (argb >> 8) & 0xff, argb & 0xff, argb >> 24);
}
}

int LuaEngine::drawBox(lua_State* state) {
  LuaEngine& engine = from(state);
  DrawCommand command{DrawCommand::Box};
  command.x1 = (float)luaL_checknumber(state, 1);
  command.y1 = (float)luaL_checknumber(state, 2);
  command.x2 = (float)luaL_checknumber(state, 3);
  command.y2 = (float)luaL_checknumber(state, 4);
  command.color = tableColor(state, 5, "fill", 0);
  command.outline = tableColor(state, 5, "outline", 0xffffffffu);
  command.thickness = tableNumber(state, 5, "thickness", 1.0f);
  engine.commands.push_back(std::move(command));
  return 0;
}

int LuaEngine::drawLine(lua_State* state) {
  LuaEngine& engine = from(state);
  DrawCommand command{DrawCommand::Line};
  command.x1 = (float)luaL_checknumber(state, 1);
  command.y1 = (float)luaL_checknumber(state, 2);
  command.x2 = (float)luaL_checknumber(state, 3);
  command.y2 = (float)luaL_checknumber(state, 4);
  command.color = (uint32_t)luaL_checkinteger(state, 5);
  command.thickness = (float)luaL_optnumber(state, 6, 1.0);
  engine.commands.push_back(std::move(command));
  return 0;
}

int LuaEngine::drawPixel(lua_State* state) {
  LuaEngine& engine = from(state);
  DrawCommand command{DrawCommand::Pixel};
  command.x1 = (float)luaL_checknumber(state, 1);
  command.y1 = (float)luaL_checknumber(state, 2);
  command.color = (uint32_t)luaL_checkinteger(state, 3);
  engine.commands.push_back(std::move(command));
  return 0;
}

int LuaEngine::drawText(lua_State* state) {
  LuaEngine& engine = from(state);
  DrawCommand command{DrawCommand::Text};
  command.x1 = (float)luaL_checknumber(state, 1);
  command.y1 = (float)luaL_checknumber(state, 2);
  size_t length = 0;
  const char* text = luaL_tolstring(state, 3, &length);
  command.text.assign(text, length);
  lua_pop(state, 1);
  command.color = tableColor(state, 4, "color", 0xffffffffu);
  command.outline = tableColor(state, 4, "outline", 0xff000000u);
  command.size = tableNumber(state, 4, "size", 13.0f);
  engine.commands.push_back(std::move(command));
  return 0;
}

namespace {
int inputIndex(lua_State* state, App& app, int port) {
  if(lua_isinteger(state, 2)) {
    const int index = (int)lua_tointeger(state, 2);
    luaL_argcheck(state, index >= 0 && index < EmuCore::MaxInputs, 2, "input index is out of range");
    return index;
  }

  const char* wanted = luaL_checkstring(state, 2);
  const int device = app.core.connectedDevice(port);
  const auto& inputs = app.core.inputs(device);
  for(int i = 0; i < (int)inputs.size(); i++) {
    if(SDL_strcasecmp(inputs[i].name.c_str(), wanted) == 0) return i;
  }
  luaL_error(state, "unknown input '%s'", wanted);
  return 0;
}
}

int LuaEngine::inputValue(lua_State* state) {
  LuaEngine& engine = from(state);
  const int port = (int)luaL_checkinteger(state, 1) - 1;
  luaL_argcheck(state, port >= 0 && port < EmuCore::PortCount, 1, "port must be from 1 to 3");
  lua_pushinteger(state, engine.app.core.inputValue(port, inputIndex(state, engine.app, port)));
  return 1;
}

int LuaEngine::inputHeld(lua_State* state) {
  LuaEngine& engine = from(state);
  const int port = (int)luaL_checkinteger(state, 1) - 1;
  luaL_argcheck(state, port >= 0 && port < EmuCore::PortCount, 1, "port must be from 1 to 3");
  lua_pushboolean(state, engine.app.core.inputValue(port, inputIndex(state, engine.app, port)) != 0);
  return 1;
}

std::string LuaEngine::dataDirectory() const {
  return configDir() + "Scripts/" + fileStem(scriptPath);
}

namespace {
std::string scriptFile(lua_State* state, LuaEngine& engine) {
  std::string path = normalPath(luaL_checkstring(state, 1));
  if(path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) {
    luaL_argerror(state, 1, "path must be relative to the script data directory");
  }
  size_t start = 0;
  while(start <= path.size()) {
    const size_t end = path.find('/', start);
    const std::string part = path.substr(start, end - start);
    if(part == "..") luaL_argerror(state, 1, "path cannot leave the script data directory");
    if(end == std::string::npos) break;
    start = end + 1;
  }
  return engine.dataDirectory() + "/" + path;
}
}

int LuaEngine::fileRead(lua_State* state) {
  LuaEngine& engine = from(state);
  const std::string path = scriptFile(state, engine);
  const std::vector<uint8_t> bytes = readBytes(path);
  lua_pushlstring(state, (const char*)bytes.data(), bytes.size());
  return 1;
}

int LuaEngine::fileWrite(lua_State* state) {
  LuaEngine& engine = from(state);
  const std::string path = scriptFile(state, engine);
  size_t size = 0;
  const char* bytes = luaL_checklstring(state, 2, &size);
  const bool append = lua_toboolean(state, lua_upvalueindex(1));
  if(!ensureDir(parentDir(path))) return luaL_error(state, "could not create the data directory");

  SDL_IOStream* file = SDL_IOFromFile(path.c_str(), append ? "ab" : "wb");
  if(!file) return luaL_error(state, "could not open '%s'", path.c_str());
  const bool wrote = SDL_WriteIO(file, bytes, size) == size;
  const bool closed = SDL_CloseIO(file);
  if(!wrote || !closed) return luaL_error(state, "could not write '%s'", path.c_str());
  lua_pushboolean(state, true);
  return 1;
}

int LuaEngine::fileDirectory(lua_State* state) {
  const std::string path = from(state).dataDirectory();
  lua_pushlstring(state, path.data(), path.size());
  return 1;
}

void LuaEngine::registerApi() {
  lua_pushlightuserdata(state, this);
  lua_setfield(state, LUA_REGISTRYINDEX, "heat-fe.lua");
  const struct { const char* name; int bytes; bool little; bool write; } functions[] = {
    {"read_u8", 1, true, false},
    {"read_u16_le", 2, true, false}, {"read_u16_be", 2, false, false},
    {"read_u24_le", 3, true, false}, {"read_u24_be", 3, false, false},
    {"read_u32_le", 4, true, false}, {"read_u32_be", 4, false, false},
    {"write_u8", 1, true, true},
    {"write_u16_le", 2, true, true}, {"write_u16_be", 2, false, true},
    {"write_u24_le", 3, true, true}, {"write_u24_be", 3, false, true},
    {"write_u32_le", 4, true, true}, {"write_u32_be", 4, false, true},
  };

  auto addMemoryDomain = [&](EmuCore::MemoryDomain domain) {
    for(const auto& function : functions) {
      lua_pushinteger(state, function.bytes);
      lua_pushboolean(state, function.little);
      lua_pushinteger(state, (lua_Integer)domain);
      lua_pushcclosure(state, function.write ? writeMemory : readMemory, 3);
      lua_setfield(state, -2, function.name);
    }
    lua_pushinteger(state, (lua_Integer)domain);
    lua_pushcclosure(state, readBit, 1);
    lua_setfield(state, -2, "read_bit");
    lua_pushinteger(state, (lua_Integer)domain);
    lua_pushcclosure(state, writeBit, 1);
    lua_setfield(state, -2, "write_bit");
    lua_pushinteger(state, app.core.memorySize(domain));
    lua_setfield(state, -2, "size");
  };

  lua_newtable(state);
  addMemoryDomain(EmuCore::MemoryDomain::Bus);
  const struct { const char* name; EmuCore::MemoryDomain domain; } domains[] = {
    {"wram", EmuCore::MemoryDomain::WRAM}, {"vram", EmuCore::MemoryDomain::VRAM},
    {"cgram", EmuCore::MemoryDomain::CGRAM}, {"oam", EmuCore::MemoryDomain::OAM},
    {"apuram", EmuCore::MemoryDomain::APURAM},
  };
  for(const auto& domain : domains) {
    lua_newtable(state);
    addMemoryDomain(domain.domain);
    lua_setfield(state, -2, domain.name);
  }
  lua_setglobal(state, "memory");

  lua_newtable(state);
  const luaL_Reg gui[] = {
    {"box", drawBox}, {"line", drawLine}, {"pixel", drawPixel}, {"text", drawText},
    {nullptr, nullptr},
  };
  luaL_setfuncs(state, gui, 0);
  lua_setglobal(state, "gui");

  lua_newtable(state);
  const luaL_Reg input[] = {{"value", inputValue}, {"held", inputHeld}, {nullptr, nullptr}};
  luaL_setfuncs(state, input, 0);
  lua_setglobal(state, "input");

  lua_newtable(state);
  lua_pushcfunction(state, fileRead);
  lua_setfield(state, -2, "read");
  lua_pushboolean(state, false);
  lua_pushcclosure(state, fileWrite, 1);
  lua_setfield(state, -2, "write");
  lua_pushboolean(state, true);
  lua_pushcclosure(state, fileWrite, 1);
  lua_setfield(state, -2, "append");
  lua_pushcfunction(state, fileDirectory);
  lua_setfield(state, -2, "directory");
  lua_setglobal(state, "file");
}

bool LuaEngine::reload() {
  if(scriptPath.empty()) return false;
  const std::string path = scriptPath;
  return load(path);
}

void LuaEngine::stop() {
  close();
  lastError.clear();
  if(!scriptPath.empty()) app.showMessage("stopped " + fileName(scriptPath));
}

bool LuaEngine::runFrame() {
  if(!active || !state) return false;
  commands.clear();

  lua_getglobal(state, "on_frame");
  if(lua_isnil(state, -1)) {
    lua_pop(state, 1);
    return true;
  }
  if(!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    lua_pushliteral(state, "on_frame must be a function");
    return failFromStack("Lua frame: ");
  }
  if(lua_pcall(state, 0, 0, 0) != LUA_OK) return failFromStack("Lua frame: ");
  return true;
}

void LuaEngine::drawOverlay() {
  if(commands.empty() || app.shell.drawWidth <= 0 || app.shell.drawHeight <= 0) return;

  const float sx = app.shell.drawWidth / 256.0f;
  const float sy = app.shell.drawHeight / videoHeight(app.settings);
  const ImVec2 origin(app.shell.drawX, app.shell.drawY);
  auto point = [&](float x, float y) { return ImVec2(origin.x + x * sx, origin.y + y * sy); };

  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  draw->PushClipRect(origin, ImVec2(origin.x + app.shell.drawWidth,
                                    origin.y + app.shell.drawHeight), true);
  for(const DrawCommand& command : commands) {
    const ImVec2 p1 = point(command.x1, command.y1);
    if(command.type == DrawCommand::Box) {
      const ImVec2 p2 = point(command.x2, command.y2);
      if(command.color >> 24) draw->AddRectFilled(p1, p2, imguiColor(command.color));
      if(command.outline >> 24) {
        draw->AddRect(p1, p2, imguiColor(command.outline), 0.0f, 0,
                      SDL_max(1.0f, command.thickness * sy));
      }
    } else if(command.type == DrawCommand::Line) {
      draw->AddLine(p1, point(command.x2, command.y2), imguiColor(command.color),
                    SDL_max(1.0f, command.thickness * sy));
    } else if(command.type == DrawCommand::Pixel) {
      draw->AddRectFilled(p1, point(command.x1 + 1.0f, command.y1 + 1.0f),
                          imguiColor(command.color));
    } else {
      const float size = command.size * sy;
      if(command.outline >> 24) {
        for(int y = -1; y <= 1; y++) for(int x = -1; x <= 1; x++) {
          if(x || y) draw->AddText(nullptr, size, ImVec2(p1.x + x, p1.y + y),
                                   imguiColor(command.outline), command.text.c_str());
        }
      }
      draw->AddText(nullptr, size, p1, imguiColor(command.color), command.text.c_str());
    }
  }
  draw->PopClipRect();
}

int64_t LuaEngine::globalInteger(const char* name) const {
  if(!state) return 0;
  lua_getglobal(state, name);
  const int64_t value = lua_isinteger(state, -1) ? (int64_t)lua_tointeger(state, -1) : 0;
  lua_pop(state, 1);
  return value;
}

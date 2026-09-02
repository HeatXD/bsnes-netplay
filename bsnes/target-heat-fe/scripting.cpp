#include "scripting.hpp"

#include "app.hpp"

#include <algorithm>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

LuaEngine::LuaEngine(App& app) : app(app) {
  const int inputs = EmuCore::PortCount * EmuCore::MaxInputs;
  inputOverrides.resize(inputs);
  physicalInput.resize(inputs);
  inputOverrideSet.resize(inputs);
}
LuaEngine::~LuaEngine() { close(); }

void LuaEngine::clearInputOverrides() {
  if(havePhysicalInput) {
    for(int port = 0; port < EmuCore::PortCount; port++) {
      for(int input = 0; input < EmuCore::MaxInputs; input++) {
        const int slot = port * EmuCore::MaxInputs + input;
        if(inputOverrideSet[slot]) app.core.setInput(port, input, physicalInput[slot]);
      }
    }
  }
  std::fill(inputOverrideSet.begin(), inputOverrideSet.end(), 0);
  havePhysicalInput = false;
  inBeforeFrame = false;
}

void LuaEngine::close() {
  clearInputOverrides();
  if(state) lua_close(state);
  state = nullptr;
  active = false;
  beforeFramePrepared = false;
  commands.clear();
  windows.clear();
  clickedWidgets.clear();
  currentWindow = -1;
}

bool LuaEngine::failFromStack(const char* prefix) {
  const char* detail = state ? lua_tostring(state, -1) : nullptr;
  lastError = std::string(prefix) + (detail ? detail : "unknown error");
  if(state) lua_pop(state, 1);
  appendConsole(lastError);
  clearInputOverrides();
  active = false;
  beforeFramePrepared = false;
  commands.clear();
  windows.clear();
  clickedWidgets.clear();
  currentWindow = -1;
  app.showMessage(lastError);
  return false;
}

void LuaEngine::appendConsole(std::string text) {
  constexpr size_t MaxConsoleBytes = 64 * 1024;
  if(text.empty() || text.back() != '\n') text += '\n';
  if(text.size() > MaxConsoleBytes) {
    text.resize(MaxConsoleBytes - 20);
    text += "\n[output truncated]\n";
  }
  if(consoleText.size() + text.size() > MaxConsoleBytes) {
    const size_t excess = consoleText.size() + text.size() - MaxConsoleBytes;
    const size_t line = consoleText.find('\n', excess);
    consoleText.erase(0, line == std::string::npos ? consoleText.size() : line + 1);
  }
  consoleText += text;
  consoleScroll = true;
}

void LuaEngine::clearConsole() {
  consoleText.clear();
  consoleScroll = false;
}

bool LuaEngine::takeConsoleScroll() {
  const bool scroll = consoleScroll;
  consoleScroll = false;
  return scroll;
}

bool LuaEngine::load(const std::string& path) {
  if(app.netplayActive()) {
    app.showMessage("Lua scripting is disabled during netplay");
    return false;
  }
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
  const bool signedValue = lua_toboolean(state, lua_upvalueindex(4));
  if(signedValue && bytes < (int)sizeof(lua_Integer) && (value & ((lua_Unsigned)1 << (bytes * 8 - 1)))) {
    value |= ~(((lua_Unsigned)1 << (bytes * 8)) - 1);
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

std::string tableString(lua_State* state, int table, const char* name,
                        const char* fallback = "") {
  if(!lua_istable(state, table)) return fallback;
  lua_getfield(state, table, name);
  std::string value = lua_isstring(state, -1) ? lua_tostring(state, -1) : fallback;
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

int LuaEngine::drawCircle(lua_State* state) {
  LuaEngine& engine = from(state);
  DrawCommand command{DrawCommand::Ellipse};
  command.x1 = (float)luaL_checknumber(state, 1);
  command.y1 = (float)luaL_checknumber(state, 2);
  command.x2 = command.y2 = (float)luaL_checknumber(state, 3);
  luaL_argcheck(state, command.x2 >= 0.0f, 3, "radius cannot be negative");
  command.color = tableColor(state, 4, "fill", 0);
  command.outline = tableColor(state, 4, "outline", 0xffffffffu);
  command.thickness = tableNumber(state, 4, "thickness", 1.0f);
  engine.commands.push_back(std::move(command));
  return 0;
}

int LuaEngine::drawEllipse(lua_State* state) {
  LuaEngine& engine = from(state);
  DrawCommand command{DrawCommand::Ellipse};
  command.x1 = (float)luaL_checknumber(state, 1);
  command.y1 = (float)luaL_checknumber(state, 2);
  command.x2 = (float)luaL_checknumber(state, 3);
  command.y2 = (float)luaL_checknumber(state, 4);
  luaL_argcheck(state, command.x2 >= 0.0f, 3, "horizontal radius cannot be negative");
  luaL_argcheck(state, command.y2 >= 0.0f, 4, "vertical radius cannot be negative");
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
  const std::string align = tableString(state, 4, "align", "left");
  if(align == "center") command.align = DrawCommand::Center;
  else if(align == "right") command.align = DrawCommand::Right;
  else luaL_argcheck(state, align == "left", 4, "align must be 'left', 'center', or 'right'");
  const std::string font = tableString(state, 4, "font", "pixel");
  command.pixelFont = font == "pixel";
  luaL_argcheck(state, command.pixelFont || font == "ui", 4,
                "font must be 'pixel' or 'ui'");
  engine.commands.push_back(std::move(command));
  return 0;
}

int LuaEngine::guiWindow(lua_State* state) {
  LuaEngine& engine = from(state);
  size_t length = 0;
  const char* title = luaL_checklstring(state, 1, &length);
  luaL_argcheck(state, length > 0, 1, "window title cannot be empty");
  WindowCommand window;
  window.title.assign(title, length);
  window.width = tableNumber(state, 2, "width", 0.0f);
  window.height = tableNumber(state, 2, "height", 0.0f);
  luaL_argcheck(state, window.width >= 0.0f, 2, "width cannot be negative");
  luaL_argcheck(state, window.height >= 0.0f, 2, "height cannot be negative");
  engine.windows.push_back(std::move(window));
  engine.currentWindow = (int)engine.windows.size() - 1;
  return 0;
}

int LuaEngine::guiLabel(lua_State* state) {
  LuaEngine& engine = from(state);
  luaL_argcheck(state, engine.currentWindow >= 0, 1, "call gui.window before gui.label");
  size_t length = 0;
  const char* value = luaL_tolstring(state, 1, &length);
  WindowWidget widget;
  widget.type = WindowWidget::Label;
  widget.text.assign(value, length);
  lua_pop(state, 1);
  engine.windows[engine.currentWindow].widgets.push_back(std::move(widget));
  return 0;
}

int LuaEngine::guiButton(lua_State* state) {
  LuaEngine& engine = from(state);
  luaL_argcheck(state, engine.currentWindow >= 0, 1, "call gui.window before gui.button");
  size_t length = 0;
  const char* label = luaL_checklstring(state, 1, &length);
  WindowWidget widget;
  widget.type = WindowWidget::Button;
  widget.text.assign(label, length);
  widget.width = tableNumber(state, 2, "width", 0.0f);
  widget.height = tableNumber(state, 2, "height", 0.0f);
  luaL_argcheck(state, widget.width >= 0.0f, 2, "width cannot be negative");
  luaL_argcheck(state, widget.height >= 0.0f, 2, "height cannot be negative");
  std::string id = tableString(state, 2, "id", widget.text.c_str());
  widget.key = engine.windows[engine.currentWindow].title + '\x1f' + id;
  const bool clicked = engine.clickedWidgets.erase(widget.key) != 0;
  engine.windows[engine.currentWindow].widgets.push_back(std::move(widget));
  lua_pushboolean(state, clicked);
  return 1;
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

int LuaEngine::inputSet(lua_State* state) {
  LuaEngine& engine = from(state);
  const int port = (int)luaL_checkinteger(state, 1) - 1;
  luaL_argcheck(state, port >= 0 && port < EmuCore::PortCount, 1, "port must be from 1 to 3");
  const int input = inputIndex(state, engine.app, port);
  lua_Integer value = lua_isboolean(state, 3) ? lua_toboolean(state, 3)
                                               : luaL_checkinteger(state, 3);
  luaL_argcheck(state, value >= -32768 && value <= 32767, 3,
                "input value must fit in a signed 16-bit integer");
  const int slot = port * EmuCore::MaxInputs + input;
  engine.inputOverrides[slot] = (int16_t)value;
  engine.inputOverrideSet[slot] = 1;
  if(engine.inBeforeFrame) engine.app.core.setInput(port, input, (int16_t)value);
  return 0;
}

int LuaEngine::inputClear(lua_State* state) {
  LuaEngine& engine = from(state);
  const int port = (int)luaL_checkinteger(state, 1) - 1;
  luaL_argcheck(state, port >= 0 && port < EmuCore::PortCount, 1, "port must be from 1 to 3");
  const int input = inputIndex(state, engine.app, port);
  const int slot = port * EmuCore::MaxInputs + input;
  engine.inputOverrideSet[slot] = 0;
  if(engine.havePhysicalInput) engine.app.core.setInput(port, input, engine.physicalInput[slot]);
  return 0;
}

int LuaEngine::inputClearAll(lua_State* state) {
  LuaEngine& engine = from(state);
  for(int port = 0; port < EmuCore::PortCount; port++) {
    for(int input = 0; input < EmuCore::MaxInputs; input++) {
      const int slot = port * EmuCore::MaxInputs + input;
      if(engine.inputOverrideSet[slot] && engine.havePhysicalInput) {
        engine.app.core.setInput(port, input, engine.physicalInput[slot]);
      }
      engine.inputOverrideSet[slot] = 0;
    }
  }
  return 0;
}

int LuaEngine::consoleLog(lua_State* state) {
  LuaEngine& engine = from(state);
  std::string line;
  const int values = lua_gettop(state);
  for(int index = 1; index <= values; index++) {
    size_t length = 0;
    const char* value = luaL_tolstring(state, index, &length);
    if(index > 1) line += '\t';
    line.append(value, length);
    lua_pop(state, 1);
  }
  engine.appendConsole(std::move(line));
  return 0;
}

int LuaEngine::consoleClear(lua_State* state) {
  from(state).clearConsole();
  return 0;
}

namespace {
std::string checkedStateSlot(lua_State* state) {
  const int slot = (int)luaL_checkinteger(state, 1);
  luaL_argcheck(state, slot >= 1 && slot <= StateSlots, 1, "slot must be from 1 to 9");
  return App::slotName(slot);
}
}

int LuaEngine::saveState(lua_State* state) {
  LuaEngine& engine = from(state);
  lua_pushboolean(state, engine.app.saveState(checkedStateSlot(state)));
  return 1;
}

int LuaEngine::loadState(lua_State* state) {
  LuaEngine& engine = from(state);
  lua_pushboolean(state, engine.app.loadState(checkedStateSlot(state)));
  return 1;
}

int LuaEngine::hasState(lua_State* state) {
  LuaEngine& engine = from(state);
  lua_pushboolean(state, engine.app.hasState(checkedStateSlot(state)));
  return 1;
}

int LuaEngine::removeState(lua_State* state) {
  LuaEngine& engine = from(state);
  lua_pushboolean(state, engine.app.removeState(checkedStateSlot(state)));
  return 1;
}

int LuaEngine::emuLoaded(lua_State* state) {
  lua_pushboolean(state, from(state).app.core.loaded());
  return 1;
}

int LuaEngine::emuPaused(lua_State* state) {
  lua_pushboolean(state, from(state).app.paused);
  return 1;
}

int LuaEngine::emuPause(lua_State* state) {
  App& app = from(state).app;
  if(app.core.loaded()) app.paused = true;
  lua_pushboolean(state, app.core.loaded());
  return 1;
}

int LuaEngine::emuResume(lua_State* state) {
  App& app = from(state).app;
  if(app.core.loaded()) app.paused = false;
  lua_pushboolean(state, app.core.loaded());
  return 1;
}

int LuaEngine::emuReset(lua_State* state) {
  App& app = from(state).app;
  const bool loaded = app.core.loaded();
  if(loaded) app.reset();
  lua_pushboolean(state, loaded);
  return 1;
}

int LuaEngine::emuPower(lua_State* state) {
  App& app = from(state).app;
  const bool loaded = app.core.loaded();
  if(loaded) app.powerCycle();
  lua_pushboolean(state, loaded);
  return 1;
}

int LuaEngine::emuFrame(lua_State* state) {
  lua_pushinteger(state, from(state).app.emulatedFrames);
  return 1;
}

int LuaEngine::emuGame(lua_State* state) {
  const std::string& game = from(state).app.gameTitle;
  lua_pushlstring(state, game.data(), game.size());
  return 1;
}

std::string LuaEngine::dataDirectory() const {
  return configDir() + "Scripts/" + fileStem(scriptPath);
}

namespace {
std::string checkedScriptPath(lua_State* state) {
  std::string path = normalPath(luaL_checkstring(state, 1));
  if(path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) {
    luaL_argerror(state, 1, "path must be relative");
  }
  size_t start = 0;
  while(start <= path.size()) {
    const size_t end = path.find('/', start);
    const std::string part = path.substr(start, end - start);
    if(part == "..") luaL_argerror(state, 1, "path cannot leave the script directory");
    if(end == std::string::npos) break;
    start = end + 1;
  }
  return path;
}

std::string scriptFile(lua_State* state, LuaEngine& engine) {
  return engine.dataDirectory() + "/" + checkedScriptPath(state);
}

std::string packagedScriptFile(lua_State* state, LuaEngine& engine) {
  const std::string directory = parentDir(engine.path());
  const std::string relative = checkedScriptPath(state);
  return directory.empty() ? relative : directory + "/" + relative;
}
}

int LuaEngine::saveStateFile(lua_State* state) {
  LuaEngine& engine = from(state);
  lua_pushboolean(state, engine.app.saveStateFile(scriptFile(state, engine)));
  return 1;
}

int LuaEngine::loadStateFile(lua_State* state) {
  LuaEngine& engine = from(state);
  const std::string dataPath = scriptFile(state, engine);
  const std::string path = pathExists(dataPath) ? dataPath : packagedScriptFile(state, engine);
  lua_pushboolean(state, engine.app.loadStateFile(path));
  return 1;
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

int LuaEngine::fileExists(lua_State* state) {
  LuaEngine& engine = from(state);
  const std::string path = scriptFile(state, engine);
  lua_pushboolean(state, SDL_GetPathInfo(path.c_str(), nullptr));
  return 1;
}

namespace {
SDL_EnumerationResult SDLCALL collectScriptFile(void* userdata, const char*, const char* name) {
  ((std::vector<std::string>*)userdata)->push_back(name);
  return SDL_ENUM_CONTINUE;
}
}

int LuaEngine::fileList(lua_State* state) {
  LuaEngine& engine = from(state);
  const bool root = lua_isnoneornil(state, 1);
  const std::string path = root ? engine.dataDirectory() : scriptFile(state, engine);
  if(root && !ensureDir(path)) return luaL_error(state, "could not create the data directory");
  SDL_PathInfo info{};
  if(!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_DIRECTORY) {
    return luaL_error(state, "path is not a directory");
  }

  std::vector<std::string> files;
  if(!SDL_EnumerateDirectory(path.c_str(), collectScriptFile, &files)) {
    return luaL_error(state, "could not list the directory");
  }
  std::sort(files.begin(), files.end());
  lua_createtable(state, (int)files.size(), 0);
  for(size_t index = 0; index < files.size(); index++) {
    lua_pushlstring(state, files[index].data(), files[index].size());
    lua_rawseti(state, -2, (lua_Integer)index + 1);
  }
  return 1;
}

int LuaEngine::fileRemove(lua_State* state) {
  LuaEngine& engine = from(state);
  const std::string path = scriptFile(state, engine);
  const bool exists = SDL_GetPathInfo(path.c_str(), nullptr);
  lua_pushboolean(state, exists && SDL_RemovePath(path.c_str()));
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
  const struct { const char* name; int bytes; bool little; bool write; bool signedValue; } functions[] = {
    {"read_u8", 1, true, false, false},
    {"read_u16_le", 2, true, false, false}, {"read_u16_be", 2, false, false, false},
    {"read_u24_le", 3, true, false, false}, {"read_u24_be", 3, false, false, false},
    {"read_u32_le", 4, true, false, false}, {"read_u32_be", 4, false, false, false},
    {"read_s8", 1, true, false, true},
    {"read_s16_le", 2, true, false, true}, {"read_s16_be", 2, false, false, true},
    {"read_s24_le", 3, true, false, true}, {"read_s24_be", 3, false, false, true},
    {"read_s32_le", 4, true, false, true}, {"read_s32_be", 4, false, false, true},
    {"write_u8", 1, true, true, false},
    {"write_u16_le", 2, true, true, false}, {"write_u16_be", 2, false, true, false},
    {"write_u24_le", 3, true, true, false}, {"write_u24_be", 3, false, true, false},
    {"write_u32_le", 4, true, true, false}, {"write_u32_be", 4, false, true, false},
    {"write_s8", 1, true, true, true},
    {"write_s16_le", 2, true, true, true}, {"write_s16_be", 2, false, true, true},
    {"write_s24_le", 3, true, true, true}, {"write_s24_be", 3, false, true, true},
    {"write_s32_le", 4, true, true, true}, {"write_s32_be", 4, false, true, true},
  };

  auto addMemoryDomain = [&](EmuCore::MemoryDomain domain) {
    for(const auto& function : functions) {
      lua_pushinteger(state, function.bytes);
      lua_pushboolean(state, function.little);
      lua_pushinteger(state, (lua_Integer)domain);
      lua_pushboolean(state, function.signedValue);
      lua_pushcclosure(state, function.write ? writeMemory : readMemory, 4);
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
    {"box", drawBox}, {"circle", drawCircle}, {"ellipse", drawEllipse},
    {"line", drawLine}, {"pixel", drawPixel}, {"text", drawText},
    {"window", guiWindow}, {"label", guiLabel}, {"button", guiButton},
    {nullptr, nullptr},
  };
  luaL_setfuncs(state, gui, 0);
  lua_setglobal(state, "gui");

  lua_newtable(state);
  const luaL_Reg input[] = {
    {"value", inputValue}, {"held", inputHeld}, {"set", inputSet},
    {"clear", inputClear}, {"clear_all", inputClearAll}, {nullptr, nullptr},
  };
  luaL_setfuncs(state, input, 0);
  lua_setglobal(state, "input");

  lua_pushcfunction(state, consoleLog);
  lua_setglobal(state, "print");
  lua_newtable(state);
  const luaL_Reg console[] = {
    {"log", consoleLog}, {"clear", consoleClear}, {nullptr, nullptr},
  };
  luaL_setfuncs(state, console, 0);
  lua_setglobal(state, "console");

  lua_newtable(state);
  const luaL_Reg savestate[] = {
    {"save", saveState}, {"load", loadState}, {"exists", hasState},
    {"remove", removeState}, {"save_file", saveStateFile},
    {"load_file", loadStateFile}, {nullptr, nullptr},
  };
  luaL_setfuncs(state, savestate, 0);
  lua_setglobal(state, "savestate");

  lua_newtable(state);
  const luaL_Reg emu[] = {
    {"loaded", emuLoaded}, {"paused", emuPaused}, {"pause", emuPause},
    {"resume", emuResume}, {"reset", emuReset}, {"power", emuPower},
    {"frame", emuFrame}, {"game", emuGame}, {nullptr, nullptr},
  };
  luaL_setfuncs(state, emu, 0);
  lua_setglobal(state, "emu");

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
  lua_pushcfunction(state, fileExists);
  lua_setfield(state, -2, "exists");
  lua_pushcfunction(state, fileList);
  lua_setfield(state, -2, "list");
  lua_pushcfunction(state, fileRemove);
  lua_setfield(state, -2, "remove");
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
  if(!beforeFramePrepared) {
    commands.clear();
    windows.clear();
    currentWindow = -1;
  }
  beforeFramePrepared = false;

  lua_getglobal(state, "on_frame");
  if(lua_isnil(state, -1)) {
    lua_pop(state, 1);
    clickedWidgets.clear();
    return true;
  }
  if(!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    lua_pushliteral(state, "on_frame must be a function");
    return failFromStack("Lua frame: ");
  }
  if(lua_pcall(state, 0, 0, 0) != LUA_OK) return failFromStack("Lua frame: ");
  clickedWidgets.clear();
  return true;
}

bool LuaEngine::runBeforeFrame() {
  if(!active || !state) return false;

  commands.clear();
  windows.clear();
  currentWindow = -1;
  beforeFramePrepared = true;

  for(int port = 0; port < EmuCore::PortCount; port++) {
    for(int input = 0; input < EmuCore::MaxInputs; input++) {
      const int slot = port * EmuCore::MaxInputs + input;
      physicalInput[slot] = app.core.inputValue(port, input);
      if(inputOverrideSet[slot]) app.core.setInput(port, input, inputOverrides[slot]);
    }
  }
  havePhysicalInput = true;
  inBeforeFrame = true;

  lua_getglobal(state, "on_before_frame");
  if(lua_isnil(state, -1)) {
    lua_pop(state, 1);
    inBeforeFrame = false;
    return true;
  }
  if(!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    lua_pushliteral(state, "on_before_frame must be a function");
    beforeFramePrepared = false;
    return failFromStack("Lua before frame: ");
  }
  if(lua_pcall(state, 0, 0, 0) != LUA_OK) {
    beforeFramePrepared = false;
    return failFromStack("Lua before frame: ");
  }
  inBeforeFrame = false;
  return true;
}

void LuaEngine::drawOverlay() {
  if((commands.empty() && windows.empty()) || app.shell.drawWidth <= 0
      || app.shell.drawHeight <= 0) return;

  const float sx = app.shell.drawWidth / 256.0f;
  const float sy = app.shell.drawHeight / videoHeight(app.settings);
  const ImVec2 origin(app.shell.drawX, app.shell.drawY);
  auto point = [&](float x, float y) { return ImVec2(origin.x + x * sx, origin.y + y * sy); };

  // The game is always drawn on the main viewport. Keep the overlay on that
  // same draw list even if ImGui's current window belongs to another viewport,
  // so clean screenshots can capture the game and overlay together.
  ImDrawList* draw = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
  for(const DrawCommand& command : commands) {
    ImVec2 p1 = point(command.x1, command.y1);
    if(command.type == DrawCommand::Box) {
      const ImVec2 p2 = point(command.x2, command.y2);
      if(command.color >> 24) draw->AddRectFilled(p1, p2, imguiColor(command.color));
      if(command.outline >> 24) {
        draw->AddRect(p1, p2, imguiColor(command.outline), 0.0f, 0,
                      SDL_max(1.0f, command.thickness * sy));
      }
    } else if(command.type == DrawCommand::Ellipse) {
      const ImVec2 radius(command.x2 * sx, command.y2 * sy);
      if(command.color >> 24) draw->AddEllipseFilled(p1, radius, imguiColor(command.color));
      if(command.outline >> 24) {
        draw->AddEllipse(p1, radius, imguiColor(command.outline), 0.0f, 0,
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
      ImFont* font = command.pixelFont && app.luaPixelFont ? app.luaPixelFont : ImGui::GetFont();
      const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, command.text.c_str());
      if(command.align == DrawCommand::Center) p1.x -= extent.x * 0.5f;
      else if(command.align == DrawCommand::Right) p1.x -= extent.x;
      if(command.outline >> 24) {
        for(int y = -1; y <= 1; y++) for(int x = -1; x <= 1; x++) {
          if(x || y) draw->AddText(font, size, ImVec2(p1.x + x, p1.y + y),
                                   imguiColor(command.outline), command.text.c_str());
        }
      }
      draw->AddText(font, size, p1, imguiColor(command.color), command.text.c_str());
    }
  }

  for(const WindowCommand& window : windows) {
    if(window.width > 0.0f || window.height > 0.0f) {
      ImGui::SetNextWindowSize(ImVec2(window.width, window.height), ImGuiCond_FirstUseEver);
    }
    const std::string id = window.title + "###lua-window-" + window.title;
    const ImGuiWindowFlags flags = window.width == 0.0f && window.height == 0.0f
                                 ? ImGuiWindowFlags_AlwaysAutoResize : 0;
    if(ImGui::Begin(id.c_str(), nullptr, flags)) {
      for(const WindowWidget& widget : window.widgets) {
        if(widget.type == WindowWidget::Label) {
          ImGui::TextUnformatted(widget.text.c_str());
        } else {
          const std::string label = widget.text + "###" + widget.key;
          if(ImGui::Button(label.c_str(), ImVec2(widget.width, widget.height))) {
            clickedWidgets.insert(widget.key);
          }
        }
      }
    }
    ImGui::End();
  }
}

int64_t LuaEngine::globalInteger(const char* name) const {
  if(!state) return 0;
  lua_getglobal(state, name);
  const int64_t value = lua_isinteger(state, -1) ? (int64_t)lua_tointeger(state, -1) : 0;
  lua_pop(state, 1);
  return value;
}

#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct App;
struct lua_State;

class LuaEngine {
public:
  explicit LuaEngine(App& app);
  ~LuaEngine();

  LuaEngine(const LuaEngine&) = delete;
  auto operator=(const LuaEngine&) -> LuaEngine& = delete;

  bool load(const std::string& path);
  bool reload();
  void stop();
  bool runFrame();
  void drawOverlay();

  bool running() const { return active; }
  const std::string& path() const { return scriptPath; }
  const std::string& error() const { return lastError; }
  int64_t globalInteger(const char* name) const;
  int commandCount() const { return (int)commands.size(); }
  std::string dataDirectory() const;

private:
  App& app;
  lua_State* state = nullptr;
  std::string scriptPath;
  std::string lastError;
  bool active = false;

  struct DrawCommand {
    enum Type { Box, Line, Pixel, Text } type;
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    uint32_t color = 0xffffffffu, outline = 0;
    float thickness = 1.0f, size = 13.0f;
    std::string text;
  };
  std::vector<DrawCommand> commands;

  void close();
  bool failFromStack(const char* prefix);
  void registerApi();

  static LuaEngine& from(lua_State* state);
  static int readMemory(lua_State* state);
  static int writeMemory(lua_State* state);
  static int readBit(lua_State* state);
  static int writeBit(lua_State* state);
  static int drawBox(lua_State* state);
  static int drawLine(lua_State* state);
  static int drawPixel(lua_State* state);
  static int drawText(lua_State* state);
  static int inputValue(lua_State* state);
  static int inputHeld(lua_State* state);
  static int fileRead(lua_State* state);
  static int fileWrite(lua_State* state);
  static int fileDirectory(lua_State* state);
};

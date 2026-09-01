#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
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
  bool runBeforeFrame();
  bool runFrame();
  void drawOverlay();

  bool running() const { return active; }
  const std::string& path() const { return scriptPath; }
  const std::string& error() const { return lastError; }
  int64_t globalInteger(const char* name) const;
  int commandCount() const { return (int)commands.size(); }
  std::string dataDirectory() const;
  const std::string& console() const { return consoleText; }
  void clearConsole();
  bool takeConsoleScroll();

private:
  App& app;
  lua_State* state = nullptr;
  std::string scriptPath;
  std::string lastError;
  bool active = false;
  bool inBeforeFrame = false;
  bool havePhysicalInput = false;
  std::vector<int16_t> inputOverrides;
  std::vector<int16_t> physicalInput;
  std::vector<uint8_t> inputOverrideSet;
  std::string consoleText;
  bool consoleScroll = false;

  struct DrawCommand {
    enum Type { Box, Ellipse, Line, Pixel, Text } type;
    float x1 = 0.0f, y1 = 0.0f, x2 = 0.0f, y2 = 0.0f;
    uint32_t color = 0xffffffffu, outline = 0;
    float thickness = 1.0f, size = 13.0f;
    enum TextAlign { Left, Center, Right } align = Left;
    bool pixelFont = false;
    std::string text;
  };
  std::vector<DrawCommand> commands;

  struct WindowWidget {
    enum Type { Label, Button } type = Label;
    std::string text;
    std::string key;
    float width = 0.0f, height = 0.0f;
  };
  struct WindowCommand {
    std::string title;
    float width = 0.0f, height = 0.0f;
    std::vector<WindowWidget> widgets;
  };
  std::vector<WindowCommand> windows;
  int currentWindow = -1;
  std::unordered_set<std::string> clickedWidgets;

  void close();
  void clearInputOverrides();
  void appendConsole(std::string text);
  bool failFromStack(const char* prefix);
  void registerApi();

  static LuaEngine& from(lua_State* state);
  static int readMemory(lua_State* state);
  static int writeMemory(lua_State* state);
  static int readBit(lua_State* state);
  static int writeBit(lua_State* state);
  static int drawBox(lua_State* state);
  static int drawCircle(lua_State* state);
  static int drawEllipse(lua_State* state);
  static int drawLine(lua_State* state);
  static int drawPixel(lua_State* state);
  static int drawText(lua_State* state);
  static int guiWindow(lua_State* state);
  static int guiLabel(lua_State* state);
  static int guiButton(lua_State* state);
  static int inputValue(lua_State* state);
  static int inputHeld(lua_State* state);
  static int inputSet(lua_State* state);
  static int inputClear(lua_State* state);
  static int inputClearAll(lua_State* state);
  static int consoleLog(lua_State* state);
  static int consoleClear(lua_State* state);
  static int saveState(lua_State* state);
  static int loadState(lua_State* state);
  static int hasState(lua_State* state);
  static int removeState(lua_State* state);
  static int saveStateFile(lua_State* state);
  static int loadStateFile(lua_State* state);
  static int emuLoaded(lua_State* state);
  static int emuPaused(lua_State* state);
  static int emuPause(lua_State* state);
  static int emuResume(lua_State* state);
  static int emuReset(lua_State* state);
  static int emuPower(lua_State* state);
  static int emuFrame(lua_State* state);
  static int emuGame(lua_State* state);
  static int fileRead(lua_State* state);
  static int fileWrite(lua_State* state);
  static int fileExists(lua_State* state);
  static int fileList(lua_State* state);
  static int fileRemove(lua_State* state);
  static int fileDirectory(lua_State* state);
};

#pragma once

#include <string>
#include <vector>

enum class Mode {
  Run,
  UiShot,
  StateTest,
  DeterminismTest,
  TimelineTest,
  HotkeyTest,
  LuaTest,
  ShaderTest,
  CheatTest,
  MovieTest
};

struct Options {
  Mode mode = Mode::Run;
  std::string romPath;
  std::string uiShot;
  std::string uiScreen = "game";
  std::string luaTest;
  std::string luaScript;
  int frameLimit = 0;
  int warmFrames = 180;
  int shotTab = -1;
  int shotW = 0;
  int shotH = 0;
  bool fast = false;
  bool uiFullscreen = false;
  int netplayPort = 0;
  int netplayLocal = -1;
  bool netplaySpectate = false;
  int netplaySpectatePlayers = 2;
  std::vector<std::string> netplayRemotes;
  std::vector<std::string> netplaySpectators;
  std::string netplayRecord;
  std::string playMovie;
  std::string weyveServer;
  bool weyveCreate = false;
  bool weyveBrowse = false;
  std::string weyveJoin;
  bool weyveSelectFirstGame = false;
  bool weyveAutoStart = false;
  int weyveDemoteAtCount = 0;

  bool needsRom() const;
};

Options parseArgs(int argc, char** argv);

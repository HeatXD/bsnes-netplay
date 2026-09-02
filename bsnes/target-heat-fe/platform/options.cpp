#include "options.hpp"

#include <SDL3/SDL.h>

bool Options::needsRom() const {
  return mode == Mode::StateTest || mode == Mode::DeterminismTest || mode == Mode::TimelineTest ||
         mode == Mode::CheatTest || mode == Mode::MovieTest ||
         (mode == Mode::Run && (frameLimit || netplayPort || !playMovie.empty()));
}

Options parseArgs(int argc, char** argv) {
  Options opt;
  for(int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    const bool hasValue = i + 1 < argc;

    if(SDL_strcmp(arg, "--fast") == 0) {
      opt.fast = true;
    } else if(SDL_strcmp(arg, "--state-test") == 0) {
      opt.mode = Mode::StateTest;
    } else if(SDL_strcmp(arg, "--determinism-test") == 0) {
      opt.mode = Mode::DeterminismTest;
    } else if(SDL_strcmp(arg, "--timeline-test") == 0) {
      opt.mode = Mode::TimelineTest;
    } else if(SDL_strcmp(arg, "--hotkey-test") == 0) {
      opt.mode = Mode::HotkeyTest;
    } else if(SDL_strcmp(arg, "--shader-test") == 0) {
      opt.mode = Mode::ShaderTest;
    } else if(SDL_strcmp(arg, "--cheat-test") == 0) {
      opt.mode = Mode::CheatTest;
    } else if(SDL_strcmp(arg, "--movie-test") == 0) {
      opt.mode = Mode::MovieTest;
    } else if(SDL_strcmp(arg, "--lua-test") == 0 && hasValue) {
      opt.luaTest = argv[++i];
      opt.mode = Mode::LuaTest;
    } else if(SDL_strcmp(arg, "--lua") == 0 && hasValue) {
      opt.luaScript = argv[++i];
    } else if(SDL_strcmp(arg, "--ui-fullscreen") == 0) {
      opt.uiFullscreen = true;
    } else if(SDL_strcmp(arg, "--frames") == 0 && hasValue) {
      opt.frameLimit = SDL_atoi(argv[++i]);
    } else if(SDL_strcmp(arg, "--ui-shot") == 0 && hasValue) {
      opt.uiShot = argv[++i];
      opt.mode = Mode::UiShot;
    } else if(SDL_strcmp(arg, "--ui-screen") == 0 && hasValue) {
      opt.uiScreen = argv[++i];
    } else if(SDL_strcmp(arg, "--ui-tab") == 0 && hasValue) {
      opt.shotTab = SDL_atoi(argv[++i]);
    } else if(SDL_strcmp(arg, "--ui-warm") == 0 && hasValue) {
      opt.warmFrames = SDL_atoi(argv[++i]);
    } else if(SDL_strcmp(arg, "--ui-size") == 0 && i + 2 < argc) {
      opt.shotW = SDL_atoi(argv[i + 1]);
      opt.shotH = SDL_atoi(argv[i + 2]);
      i += 2;
    } else if(SDL_strcmp(arg, "--netplay-port") == 0 && hasValue) {
      opt.netplayPort = SDL_atoi(argv[++i]);
    } else if(SDL_strcmp(arg, "--netplay-local") == 0 && hasValue) {
      opt.netplayLocal = SDL_atoi(argv[++i]);
    } else if(SDL_strcmp(arg, "--netplay-remote") == 0 && hasValue) {
      opt.netplayRemotes.push_back(argv[++i]);
    } else if(SDL_strcmp(arg, "--netplay-spectate") == 0) {
      opt.netplaySpectate = true;
    } else if(SDL_strcmp(arg, "--netplay-spectate-players") == 0 && hasValue) {
      opt.netplaySpectatePlayers = SDL_atoi(argv[++i]);
    } else if(SDL_strcmp(arg, "--netplay-spectator-remote") == 0 && hasValue) {
      opt.netplaySpectators.push_back(argv[++i]);
    } else if(SDL_strcmp(arg, "--netplay-record") == 0 && hasValue) {
      opt.netplayRecord = argv[++i];
    } else if(SDL_strcmp(arg, "--play-movie") == 0 && hasValue) {
      opt.playMovie = argv[++i];
    } else if(SDL_strcmp(arg, "--weyve-server") == 0 && hasValue) {
      opt.weyveServer = argv[++i];
    } else if(SDL_strcmp(arg, "--weyve-create") == 0) {
      opt.weyveCreate = true;
    } else if(SDL_strcmp(arg, "--weyve-browse") == 0) {
      opt.weyveBrowse = true;
    } else if(SDL_strcmp(arg, "--weyve-join") == 0 && hasValue) {
      opt.weyveJoin = argv[++i];
    } else if(SDL_strcmp(arg, "--weyve-select-first-game") == 0) {
      opt.weyveSelectFirstGame = true;
    } else if(SDL_strcmp(arg, "--weyve-auto-start") == 0) {
      opt.weyveAutoStart = true;
    } else if(SDL_strcmp(arg, "--weyve-demote-at-count") == 0 && hasValue) {
      opt.weyveDemoteAtCount = SDL_atoi(argv[++i]);
    } else if(arg[0] != '-' && opt.romPath.empty()) {
      opt.romPath = arg;
    }
  }
  return opt;
}

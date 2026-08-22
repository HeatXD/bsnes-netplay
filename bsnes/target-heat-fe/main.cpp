#include "app.hpp"
#include "tests.hpp"

#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <string>

namespace {

constexpr int IdleDelayMs = 32;

// what this invocation is for; the harnesses are mutually exclusive
enum class Mode {
  Run, UiShot, StateTest, DeterminismTest, TimelineTest,
  HotkeyTest, LuaTest, ShaderTest, CheatTest, MovieTest
};

struct Options {
  Mode mode = Mode::Run;
  std::string romPath;
  std::string uiShot;
  std::string uiScreen = "game";
  std::string luaTest;
  std::string luaScript;
  int frameLimit = 0;
  int warmFrames = 180;  // emulated before a ui shot, so it lands on a real frame
  int shotTab = -1;
  int shotW = 0, shotH = 0;
  bool fast = false;
  bool uiFullscreen = false;

  // manual verification only: starts a Direct P2P session right after load,
  // so two instances can be driven headlessly with --frames as the harness
  int netplayPort = 0;
  int netplayLocal = -1;
  bool netplaySpectate = false;
  int netplaySpectatePlayers = 2;
  std::vector<std::string> netplayRemotes;
  std::vector<std::string> netplaySpectators;
  // manual verification only: records from session start and writes a .bsv
  // directly (skipping the save dialog) once --frames stops the run
  std::string netplayRecord;
  std::string playMovie;  // manual verification only: plays a .bsv from startup
  // manual verification only: exercises the room browser headlessly
  std::string weyveServer;  // "host:port"
  bool weyveCreate = false;
  bool weyveBrowse = false;
  std::string weyveJoin;  // room code
  bool weyveSelectFirstGame = false;
  bool weyveAutoStart = false;
  int weyveDemoteAtCount = 0;  // manual verification only: force the newest member to spectate

  // the modes that drive the emulator itself; the hotkey matcher needs no game
  bool needsRom() const {
    return mode == Mode::StateTest || mode == Mode::DeterminismTest || mode == Mode::TimelineTest
        || mode == Mode::CheatTest || mode == Mode::MovieTest
        || (mode == Mode::Run && (frameLimit || netplayPort || !playMovie.empty()));
  }
};

Options parseArgs(int argc, char** argv) {
  Options opt;
  for(int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    const bool hasValue = i + 1 < argc;

    if(SDL_strcmp(arg, "--fast") == 0) opt.fast = true;
    else if(SDL_strcmp(arg, "--state-test") == 0) opt.mode = Mode::StateTest;
    else if(SDL_strcmp(arg, "--determinism-test") == 0) opt.mode = Mode::DeterminismTest;
    else if(SDL_strcmp(arg, "--timeline-test") == 0) opt.mode = Mode::TimelineTest;
    else if(SDL_strcmp(arg, "--hotkey-test") == 0) opt.mode = Mode::HotkeyTest;
    else if(SDL_strcmp(arg, "--shader-test") == 0) opt.mode = Mode::ShaderTest;
    else if(SDL_strcmp(arg, "--cheat-test") == 0) opt.mode = Mode::CheatTest;
    else if(SDL_strcmp(arg, "--movie-test") == 0) opt.mode = Mode::MovieTest;
    else if(SDL_strcmp(arg, "--lua-test") == 0 && hasValue) {
      opt.luaTest = argv[++i];
      opt.mode = Mode::LuaTest;
    }
    else if(SDL_strcmp(arg, "--lua") == 0 && hasValue) opt.luaScript = argv[++i];
    else if(SDL_strcmp(arg, "--ui-fullscreen") == 0) opt.uiFullscreen = true;
    else if(SDL_strcmp(arg, "--frames") == 0 && hasValue) opt.frameLimit = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--ui-shot") == 0 && hasValue) {
      opt.uiShot = argv[++i];
      opt.mode = Mode::UiShot;
    }
    else if(SDL_strcmp(arg, "--ui-screen") == 0 && hasValue) opt.uiScreen = argv[++i];
    else if(SDL_strcmp(arg, "--ui-tab") == 0 && hasValue) opt.shotTab = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--ui-warm") == 0 && hasValue) opt.warmFrames = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--ui-size") == 0 && i + 2 < argc) {
      opt.shotW = SDL_atoi(argv[i + 1]);
      opt.shotH = SDL_atoi(argv[i + 2]);
      i += 2;
    }
    else if(SDL_strcmp(arg, "--netplay-port") == 0 && hasValue) opt.netplayPort = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--netplay-local") == 0 && hasValue) opt.netplayLocal = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--netplay-remote") == 0 && hasValue) opt.netplayRemotes.push_back(argv[++i]);
    else if(SDL_strcmp(arg, "--netplay-spectate") == 0) opt.netplaySpectate = true;
    else if(SDL_strcmp(arg, "--netplay-spectate-players") == 0 && hasValue) opt.netplaySpectatePlayers = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--netplay-spectator-remote") == 0 && hasValue) opt.netplaySpectators.push_back(argv[++i]);
    else if(SDL_strcmp(arg, "--netplay-record") == 0 && hasValue) opt.netplayRecord = argv[++i];
    else if(SDL_strcmp(arg, "--play-movie") == 0 && hasValue) opt.playMovie = argv[++i];
    else if(SDL_strcmp(arg, "--weyve-server") == 0 && hasValue) opt.weyveServer = argv[++i];
    else if(SDL_strcmp(arg, "--weyve-create") == 0) opt.weyveCreate = true;
    else if(SDL_strcmp(arg, "--weyve-browse") == 0) opt.weyveBrowse = true;
    else if(SDL_strcmp(arg, "--weyve-join") == 0 && hasValue) opt.weyveJoin = argv[++i];
    else if(SDL_strcmp(arg, "--weyve-select-first-game") == 0) opt.weyveSelectFirstGame = true;
    else if(SDL_strcmp(arg, "--weyve-auto-start") == 0) opt.weyveAutoStart = true;
    else if(SDL_strcmp(arg, "--weyve-demote-at-count") == 0 && hasValue) opt.weyveDemoteAtCount = SDL_atoi(argv[++i]);
    else if(arg[0] != '-' && opt.romPath.empty()) opt.romPath = arg;
  }
  return opt;
}

void initImGui(App& app, bool viewports) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  // a screenshot can only read the main framebuffer, so shots keep the panels
  // merged into the host window instead of giving them real OS windows
  if(viewports) {
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoTaskBarIcon = true;
  }

  app.applyTheme();
  ImGui_ImplSDL3_InitForOpenGL(app.shell.window, app.shell.gl);
  ImGui_ImplOpenGL3_Init(app.shell.glslVersion);
  app.fontDirty = true;
}

void shutdownImGui() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

// the escape hatch for a pad SDL has no mapping for, which is otherwise dead
void loadGamepadMappings() {
  const std::string path = configDir() + "gamecontrollerdb.txt";
  if(!pathExists(path)) return;
  SDL_Log("gamepad mappings from %s: %d", path.c_str(),
          SDL_AddGamepadMappingsFromFile(path.c_str()));
}

// SDL also queues an ADDED event for every pad already attached at startup, so
// opening one twice would list the same stick under two slots
void addPad(App& app, SDL_JoystickID id) {
  for(const Controller& pad : app.pads) {
    if(pad && pad.id() == id) return;
  }
  Controller opened;
  if(SDL_IsGamepad(id)) {
    opened.gamepad = SDL_OpenGamepad(id);
    if(opened.gamepad) opened.joystick = SDL_GetGamepadJoystick(opened.gamepad);
  } else {
    opened.joystick = SDL_OpenJoystick(id);
  }
  if(!opened) return;

  // reuse a slot left by an unplugged pad before growing the list
  for(Controller& slot : app.pads) {
    if(!slot) { slot = opened; return; }
  }
  app.pads.push_back(opened);
}

void openGamepads(App& app) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetJoysticks(&count);
  for(int i = 0; ids && i < count; i++) addPad(app, ids[i]);
  if(ids) SDL_free(ids);
}

// Drives the frame loop. Lives as a struct so the event watch can call back
// into it through a plain pointer.
struct Frontend {
  App& app;
  const Options& opt;

  int frames = 0;
  int fpsFrames = 0;
  uint64_t fpsMark = SDL_GetTicksNS();
  bool inFrame = false;
  bool screensaverInhibited = false;
  bool weyveGamePicked = false;  // manual verification only
  bool weyveRoomReported = false;
  bool weyveStarted = false;
  bool weyveDemoted = false;
  int weyveDemotedFrames = 0;
  std::string weyveBlocked;

  void renderUi();
  void advance();
  bool stepFrame();
  void handleWindowEvent(const SDL_Event& event);
  void handleGamepadEvent(const SDL_Event& event);
  bool handleRebind(const SDL_Event& event);
  void handleHotkey(const SDL_Event& event);
  void handleEvent(const SDL_Event& event);
  void drainPicks();
  int runLoop();
  int runUiShot();
};

void Frontend::renderUi() {
  if(app.fontDirty) app.applyFont();

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  app.drawUi();
  ImGui::Render();

  int w = 0, h = 0;
  SDL_GetWindowSizeInPixels(app.shell.window, &w, &h);
  glViewport(0, 0, w, h);
  glClearColor(0.f, 0.f, 0.f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImDrawData* drawData = ImGui::GetDrawData();

  if(!app.pendingScreenshot.empty()) {
    // The background list contains the game texture and Lua overlay. Render it
    // alone for a clean capture, then redraw the complete UI for presentation.
    ImDrawList* background = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
    ImDrawData capture;
    capture.Valid = true;
    capture.DisplayPos = drawData->DisplayPos;
    capture.DisplaySize = drawData->DisplaySize;
    capture.FramebufferScale = drawData->FramebufferScale;
    capture.OwnerViewport = drawData->OwnerViewport;
    capture.AddDrawList(background);
    ImGui_ImplOpenGL3_RenderDrawData(&capture);
    app.finishScreenshot(app.shell.saveGameView(app.pendingScreenshot));
    glClear(GL_COLOR_BUFFER_BIT);
  }

  ImGui_ImplOpenGL3_RenderDrawData(drawData);
}

void Frontend::advance() {
  const bool blockInput = app.settings.defocusPolicy == DefocusBlockInput && !app.focused();

  if(app.netplayActive()) {
    // netplayRun() (called from advanceEmulation below) supplies every port's
    // input straight from GekkoNet, including on resimulated frames
  } else if(ImGui::GetIO().WantCaptureKeyboard || blockInput) {
    for(int port = 0; port < InputMap::Ports; port++) {
      for(int b = 0; b < EmuCore::MaxInputs; b++) app.core.setInput(port, b, 0);
    }
  } else {
    app.input.apply(app.core, app.pads, app.settings, app.sample, app.emulatedFrames);
  }

  app.advanceEmulation();
  frames++;
  fpsFrames++;
}

// Returns true when nothing was emulated, so the caller knows to idle. Also
// runs re-entrantly from the event watch during an OS drag or resize.
bool Frontend::stepFrame() {
  if(inFrame) return false;  // the watch can fire while we are already drawing
  inFrame = true;

  // one read a frame: the relative delta is consumed by whoever asks first
  app.sample = InputSample::poll(app.mouseCaptured);
  app.pollHotkeys();
  app.weyvePoll();

  if(!weyveRoomReported && app.weyveInRoom()) {
    uint32_t length = 0;
    const char* id = weyve_room_id(app.weyve.client, &length);
    SDL_Log("weyve room id: %.*s", (int)length, id);
    weyveRoomReported = true;
  }

  // manual verification only: pick a game and start once the room is ready
  if(opt.weyveSelectFirstGame && !weyveGamePicked && app.weyveInRoom() && !app.games.empty()) {
    weyveGamePicked = true;
    SDL_Log("weyve selected game: %s", app.games[0].first.c_str());
    app.weyveSelectGame(0);
  }
  if(opt.weyveDemoteAtCount && !weyveDemoted && app.weyveInRoom() && weyve_is_host(app.weyve.client)) {
    uint32_t count = 0;
    const uint32_t* members = weyve_members(app.weyve.client, &count);
    if((int)count >= opt.weyveDemoteAtCount) {
      weyveDemoted = true;
      uint32_t newest = 0;
      for(uint32_t i = 0; i < count; i++) newest = SDL_max(newest, members[i]);
      app.weyveSetRole(newest, "spec");
    }
  }
  if(weyveDemoted && weyveDemotedFrames < 60) weyveDemotedFrames++;  // let the role change propagate
  if(opt.weyveAutoStart && !weyveStarted && app.weyveInRoom() && weyve_is_host(app.weyve.client)) {
    const std::string blocked = app.weyveStartBlockedReason();
    if(blocked != weyveBlocked) {
      weyveBlocked = blocked;
      SDL_Log("weyve start: %s", blocked.empty() ? "ready" : blocked.c_str());
    }
    if((!opt.weyveDemoteAtCount || weyveDemotedFrames >= 60) && blocked.empty()) {
      weyveStarted = true;
      app.weyveStartGame();
    }
  }

  // --frames has to keep running while unfocused, or a headless run stalls
  const bool stopped = app.emulationIdle()
                    && !(opt.frameLimit && !app.paused && app.core.loaded());

  if(!stopped) advance();
  app.frameAdvance = false;
  app.saveMemoryTick();

  // screensaver comes back the instant emulation isn't actually running
  const bool running = app.core.loaded() && !stopped;
  if(running != screensaverInhibited) {
    if(running) SDL_DisableScreenSaver(); else SDL_EnableScreenSaver();
    screensaverInhibited = running;
  }

  const uint64_t now = SDL_GetTicksNS();
  if(now - fpsMark >= 500000000ull) {
    app.fps = fpsFrames * 1e9 / (now - fpsMark);
    fpsMark = now;
    fpsFrames = 0;
  }

  renderUi();

  if(ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
    SDL_GL_MakeCurrent(app.shell.window, app.shell.gl);  // popups leave their context current
  }
  SDL_GL_SwapWindow(app.shell.window);

  // One clock for every caller: the watch and the main loop both land here, so
  // a second wall-clock path would stack frames and run the game fast. Waiting
  // after the present means the next poll sees fresh events rather than ones
  // that went stale during the wait.
  if(!stopped && !opt.fast && !app.unpaced()) app.shell.pace(app.settings);

  inFrame = false;
  return stopped;
}

void Frontend::handleWindowEvent(const SDL_Event& event) {
  if(event.type == SDL_EVENT_QUIT) {
    if(app.movieActive()) {
      app.showMessage("stop the movie before quitting");
    } else {
      app.running = false;
    }
  }
  if(event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
  && event.window.windowID == SDL_GetWindowID(app.shell.window)) {
    if(app.movieActive()) {
      app.showMessage("stop the movie before quitting");
    } else {
      app.running = false;
    }
  }
  if(event.type == SDL_EVENT_DROP_FILE && event.drop.data) app.loadRom(event.drop.data);
}

void Frontend::handleGamepadEvent(const SDL_Event& event) {
  if(event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_JOYSTICK_ADDED) {
    addPad(app, event.type == SDL_EVENT_GAMEPAD_ADDED ? event.gdevice.which
                                                      : event.jdevice.which);
    return;
  }
  if(event.type != SDL_EVENT_GAMEPAD_REMOVED && event.type != SDL_EVENT_JOYSTICK_REMOVED) return;
  const SDL_JoystickID id = event.type == SDL_EVENT_GAMEPAD_REMOVED ? event.gdevice.which
                                                                    : event.jdevice.which;

  // the slot is emptied rather than erased, so unplugging one pad never
  // renumbers the others out from under whoever picked them
  for(Controller& pad : app.pads) {
    if(!pad || pad.id() != id) continue;
    if(pad.gamepad) SDL_CloseGamepad(pad.gamepad);
    else SDL_CloseJoystick(pad.joystick);
    pad = {};
    break;
  }
}

// returns true when the event was swallowed by a pending rebind
bool Frontend::handleRebind(const SDL_Event& event) {
  const bool escape = event.type == SDL_EVENT_KEY_DOWN
                   && event.key.scancode == SDL_SCANCODE_ESCAPE;

  if(app.capturing >= 0) {
    // only the pad this port reads may bind it, so two sticks stay distinct
    const Controller* owner = app.portPad(app.mapPort, app.mapPlayer);
    Binding b;
    if(escape) app.capturing = -1;
    else if(InputMap::capture(event, b, owner)) {
      app.input.binding(app.mapPort, app.core.connectedDevice(app.mapPort),
                        app.capturing / InputMap::SlotCount,
                        app.capturing % InputMap::SlotCount) = b;
      app.input.save(app.inputCfg);
      app.capturing = -1;
    }
    return true;
  }

  if(app.capturingHotkey >= 0) {
    Binding b;
    if(escape) app.capturingHotkey = -1;
    else if(InputMap::capture(event, b, nullptr, true)) {
      app.input.hotkey(app.capturingHotkey / InputMap::HotkeySlots,
                       app.capturingHotkey % InputMap::HotkeySlots) = b;
      app.input.save(app.inputCfg);
      app.capturingHotkey = -1;
    }
    return true;
  }

  return false;
}

// Ctrl+O is a menu accelerator rather than a hotkey, so it stays event driven
void Frontend::handleHotkey(const SDL_Event& event) {
  if(event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
  if(ImGui::GetIO().WantCaptureKeyboard) return;

  if(event.key.scancode == SDL_SCANCODE_O && (event.key.mod & SDL_KMOD_CTRL)) {
    app.openRomDialog();
  }
}

void Frontend::handleEvent(const SDL_Event& event) {
  handleWindowEvent(event);
  handleGamepadEvent(event);
  if(handleRebind(event)) return;
  handleHotkey(event);
}

void Frontend::drainPicks() {
  std::string picked;

  if(takePick(app.romPick, picked) && !picked.empty()) app.loadRom(picked);
  if(takePick(app.pakPick, picked) && !picked.empty()) app.loadRom(picked);

  // cancelling the second cartridge loads slot A alone rather than nothing
  if(takePick(app.sufamiAPick, picked)) {
    app.sufamiPending = picked;
    if(!picked.empty()) {
      app.openMediaDialog(app.sufamiBPick, "Sufami Turbo ROMs", "st;zip;7z",
                          EmuCore::Medium::SufamiTurbo);
    }
  }
  if(takePick(app.sufamiBPick, picked) && !app.sufamiPending.empty()) {
    app.loadRom(picked.empty() ? app.sufamiPending : app.sufamiPending + "|" + picked);
    app.sufamiPending.clear();
  }

  if(takePick(app.dirPick, picked) && !picked.empty()) {
    app.settings.gamesDir = normalPath(picked);
    app.settings.save(app.settingsCfg);
    app.scanGames();
  }
  if(takePick(app.shotDirPick, picked) && !picked.empty()) {
    app.settings.shotsDir = normalPath(picked);
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.savesDirPick, picked) && !picked.empty()) {
    app.settings.savesDir = normalPath(picked);
    app.core.setSavesDirectory(app.settings.savesDir);
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.patchesDirPick, picked) && !picked.empty()) {
    app.settings.patchesDir = normalPath(picked);
    app.core.setPatchesDirectory(app.settings.patchesDir);
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.databaseDirPick, picked) && !picked.empty()) {
    app.settings.databaseDir = normalPath(picked);
    app.refreshDatabaseDir();
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.statesDirPick, picked) && !picked.empty()) {
    app.settings.statesDir = normalPath(picked);
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.cheatsDirPick, picked) && !picked.empty()) {
    app.settings.cheatsDir = normalPath(picked);
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.shadersDirPick, picked) && !picked.empty()) {
    app.settings.shadersDir = normalPath(picked);
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.firmwareDirPick, picked) && !picked.empty()) {
    app.settings.firmwareDir = normalPath(picked);
    app.core.setFirmwareDirectory(app.firmwareDir());
    app.settings.save(app.settingsCfg);
  }
  if(takePick(app.fontPick, picked) && !picked.empty()) {
    app.settings.fontPath = picked;
    app.settings.save(app.settingsCfg);
    app.fontDirty = true;
  }
  if(takePick(app.scriptPick, picked) && !picked.empty()) {
    if(app.movieActive()) {
      app.showMessage("stop the movie before loading a Lua script");
    } else {
      app.scripting.load(picked);
      app.showScripting = true;
    }
  }
  if(takePick(app.movieOpenPick, picked) && !picked.empty()) {
    app.playMovieFile(picked);
  }
  if(takePick(app.movieSavePick, picked)) {
    const bool saved = !picked.empty() && app.writeMovieFile(picked);
    app.clearMovie();
    if(saved) {
      app.showMessage("movie recorded");
    } else if(picked.empty()) {
      app.showMessage("movie not recorded");
    } else {
      app.showMessage("movie could not be recorded");
    }
  }

  for(const BiosSlot& slot : BiosSlots) {
    if(takePick(app.*slot.pick, picked) && !picked.empty()) {
      app.settings.*slot.path = normalPath(picked);
      app.settings.save(app.settingsCfg);
    }
  }
}

int Frontend::runLoop() {
  // Windows blocks inside its own message pump while a window is dragged or
  // resized, so SDL_PollEvent never returns. Drive frames from a watch instead.
  SDL_AddEventWatch([](void* data, SDL_Event* event) -> bool {
    if(event->type == SDL_EVENT_WINDOW_EXPOSED
    || event->type == SDL_EVENT_WINDOW_RESIZED) {
      ((Frontend*)data)->stepFrame();
    }
    return true;
  }, this);

  while(app.running) {
    if(opt.frameLimit && frames >= opt.frameLimit) break;

    SDL_Event event;
    bool busy = false;
    while(SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      handleEvent(event);
      busy = true;
    }

    drainPicks();

    // With nothing emulating there is no audio clock, so the redraw rate is
    // the only thing setting idle cpu use. A dragged panel is moved by imgui
    // once per redraw, so while events arrive follow the display the window is
    // on; anything slower visibly trails the cursor, anything faster is thrown
    // away by the compositor.
    if(stepFrame()) SDL_Delay(busy ? app.shell.displayFrameMs() : IdleDelayMs);
  }

  if(app.netplay.instance.size() || app.netplay.desyncCount) {
    SDL_Log("netplay: instance %s, %u desync event(s), stopped %s, "
            "desync detection %s, state cache %d entries",
            app.netplay.instance.c_str(), app.netplay.desyncCount,
            app.netplayActive() ? "no" : "yes",
            app.netplay.detectDesyncs ? "on" : "off",
            (int)app.netplay.stateCache.size());
  }
  if(!opt.weyveServer.empty()) {
    SDL_Log("weyve: connected=%d inRoom=%d roomListCount=%d",
            app.weyveConnected(), app.weyveInRoom(), (int)app.weyve.roomList.size());
    for(const WeyveRoomListing& room : app.weyve.roomList) {
      SDL_Log("weyve room: id=%s members=%u passworded=%d game=\"%s\" host=\"%s\"",
              room.id.c_str(), room.members, room.passworded, room.game.c_str(), room.host.c_str());
    }
  }
  if(!opt.netplayRecord.empty() && app.movieMode == MovieMode::Recording) {
    const bool wrote = app.writeMovieFile(opt.netplayRecord);
    SDL_Log("netplay movie: %s, %d inputs -> %s",
            wrote ? "wrote" : "FAILED", (int)app.movieInput.size(), opt.netplayRecord.c_str());
  }
  if(!opt.playMovie.empty()) {
    // hostState (cothread stacks) never compares between two processes; skip
    // it exactly as netplayChecksum does, or every run "differs" trivially
    const std::vector<uint8_t> finalState = app.core.serialize(false);
    uint32_t sum = 2166136261u;
    for(const EmuCore::StateComponent& part : app.core.stateMap(false)) {
      if(part.hostState) continue;
      for(int i = 0; i < part.size && part.offset + i < (int)finalState.size(); i++) {
        sum = (sum ^ finalState[part.offset + i]) * 16777619u;
      }
    }
    SDL_Log("movie playback: mode %d, position %d/%d, checksum %08x",
            (int)app.movieMode, (int)app.moviePosition, (int)app.movieInput.size(), sum);
  }
  SDL_Log("platform viewports: %d (1 = host window only)",
          ImGui::GetPlatformIO().Viewports.Size);
  SDL_Log("ran %d frames, last frame %dx%d, %.1f samples/frame (expect %.1f)",
          frames, app.shell.frameWidth, app.shell.frameHeight,
          frames ? (double)app.totalSamples / frames : 0.0, AudioRate / app.core.refreshRate());

  app.input.save(app.inputCfg);
  app.settings.save(app.settingsCfg);
  return 0;
}

int Frontend::runUiShot() {
  if(opt.shotW > 0 && opt.shotH > 0) SDL_SetWindowSize(app.shell.window, opt.shotW, opt.shotH);
  if(opt.uiFullscreen) app.toggleFullscreen();

  // warm the emulator so the shot shows the UI over a real frame
  for(int i = 0; app.core.loaded() && i < opt.warmFrames; i++) {
    for(int port = 0; port < EmuCore::PortCount; port++) {
      for(int input = 0; input < EmuCore::MaxInputs; input++) {
        app.core.setInput(port, input, 0);
      }
    }
    app.scripting.runBeforeFrame();
    app.core.runFrame();
    app.scripting.runFrame();
  }

  // early passes settle window sizing; --frames asks for more to catch layout
  // that drifts per frame
  const int passes = opt.frameLimit > 0 ? opt.frameLimit : 3;
  for(int pass = 0; pass < passes; pass++) {
    if(app.weyveConnected()) {
      SDL_Delay(1);
      app.weyvePoll();
    }
    if(pass == 0 && opt.shotTab >= 0) app.settingsTab = opt.shotTab;
    renderUi();
  }

  // read before swapping, or the back buffer is already gone
  const bool ok = app.shell.saveWindow(opt.uiShot);
  SDL_Log("ui shot %s: %s", ok ? "saved" : "FAILED", opt.uiShot.c_str());
  return ok ? 0 : 1;
}

// The core caches these rather than reading Settings per frame, so a loaded
// config has to be pushed in explicitly; without this it keeps its own
// defaults until the matching tab happens to be opened.
void applySettingsToCore(App& app) {
  const Settings& s = app.settings;

  app.core.setSavesDirectory(s.savesDir);
  app.core.setFirmwareDirectory(app.firmwareDir());
  app.refreshDatabaseDir();
  app.core.setPatchesDirectory(s.patchesDir);
  app.core.setIpsHeadered(s.ipsHeadered);
  app.pushSerialization();
  app.core.setOverscanCrop(s.overscanCrop);
  app.core.setPaletteAdjust(s.videoGamma, s.videoLuminance, s.videoSaturation);
  app.core.setFilter(s.videoFilter);
  app.core.setOption("Video/BlurEmulation", flag(s.hiresBlur));
  app.pushEnhancements();
}

void loadConfigs(App& app) {
  app.inputCfg = prefFile("input.cfg");
  app.settingsCfg = prefFile("settings.cfg");
  // the aiming devices are laid out by the core, so their defaults wait for it
  app.input.loadPointerDefaults(app.core);
  app.input.load(app.inputCfg);
  app.settings.load(app.settingsCfg);
  // a config written before hotkeys became bindings carries bare scancodes
  if(!app.input.hasHotkeys() && app.settings.legacyHotkeyCount > 0) {
    app.input.migrateHotkeys(app.settings.legacyHotkeys, app.settings.legacyHotkeyCount);
    app.input.save(app.inputCfg);
  }
  applySettingsToCore(app);

  SDL_strlcpy(app.weyveHostInput, app.settings.weyveHost.c_str(), sizeof(app.weyveHostInput));
  SDL_snprintf(app.weyvePortInput, sizeof(app.weyvePortInput), "%d", app.settings.weyvePort);
  SDL_strlcpy(app.weyveNicknameInput, app.settings.weyveNickname.c_str(), sizeof(app.weyveNicknameInput));

  for(int port = 0; port < EmuCore::PortCount; port++) {
    app.core.connect(port, app.settings.devices[port]);
  }
}

void wireCore(App& app) {
  app.applyAudioTuning();
  app.core.onVideo = [&app](const uint32_t* argb, int width, int height) {
    app.shell.pushVideo(argb, width, height);
  };
  app.core.onAudio = [&app](const float* samples, int frames) {
    app.shell.pushAudio(app.settings, samples, frames, app.audioGain(), app.unpaced());
    app.totalSamples += frames;
  };
  app.core.onInputPoll = [&app](int port, int device, int input, int16_t value) {
    return app.pollMovieInput(port, device, input, value);
  };
}

// --ui-screen decides which panel is already open when the frame loop starts
void openRequestedPanel(App& app, const Options& opt) {
  app.showSettings = opt.uiScreen == "settings";
  app.showTools = opt.uiScreen == "tools";
  app.showGames = opt.uiScreen == "games";
  app.showAbout = opt.uiScreen == "about";
  // cartridge is the old name for the same window
  app.showManifest = opt.uiScreen == "manifest" || opt.uiScreen == "cartridge";
  app.showScripting = opt.uiScreen == "scripting";
  app.showNetplay = opt.uiScreen == "netplay" || opt.uiScreen == "weyve";
  if(opt.uiScreen == "weyve") app.weyve.focusTab = true;
  app.showStateManager = opt.uiScreen == "state-manager";
  app.showCheats = opt.uiScreen == "cheats";
  app.showCheatFinder = opt.uiScreen == "cheat-finder";
  if(opt.shotTab >= 0) app.settingsTab = opt.shotTab;
}

void shutdownApp(App& app) {
  shutdownImGui();
  for(Controller& pad : app.pads) {
    if(pad.gamepad) SDL_CloseGamepad(pad.gamepad);
    else if(pad.joystick) SDL_CloseJoystick(pad.joystick);
  }
  app.weyveDisconnect();
  // quitting with a game up otherwise skips the auto-resume state
  if(app.core.loaded()) app.unloadRom();
  app.shell.shutdown();
}

}  // namespace

int main(int argc, char** argv) {
  const Options opt = parseArgs(argc, argv);

  App app;
  // settings must be loaded before initAudio, which needs the remembered device
  loadConfigs(app);

  if(!app.shell.init(app.settings)) {
    app.shell.shutdown();
    return 1;
  }

  app.applyShader();  // needs the GL context, so not with the rest of the config
  app.scanGames();
  initImGui(app, opt.mode != Mode::UiShot);
  loadGamepadMappings();
  openGamepads(app);
  wireCore(app);
  openRequestedPanel(app, opt);

  if(!opt.romPath.empty()) app.loadRom(opt.romPath);
  if(!opt.luaScript.empty()) app.scripting.load(opt.luaScript);
  if(opt.netplayPort && opt.netplaySpectate) {
    app.netplayStart(opt.netplayPort, -1, opt.netplayRemotes, {}, opt.netplaySpectatePlayers);
  } else if(opt.netplayPort && opt.netplayLocal >= 0) {
    app.netplayStart(opt.netplayPort, opt.netplayLocal, opt.netplayRemotes, opt.netplaySpectators);
    if(!opt.netplayRecord.empty()) app.beginMovieRecording(false);
  }
  if(!opt.playMovie.empty()) app.playMovieFile(opt.playMovie);
  if(!opt.weyveServer.empty()) {
    const size_t colon = opt.weyveServer.find(':');
    const std::string host = opt.weyveServer.substr(0, colon);
    const uint16_t port = (uint16_t)SDL_atoi(opt.weyveServer.c_str() + colon + 1);
    app.weyveConnect(host, port);
    if(opt.weyveCreate) app.weyveCreateRoom(true);
    else if(!opt.weyveJoin.empty()) app.weyveJoinRoom(opt.weyveJoin, "");
    if(opt.weyveBrowse) app.weyveListRooms();
  }
  if(opt.needsRom() && !app.core.loaded()) {
    SDL_Log("no rom loaded");
    shutdownApp(app);
    return 1;
  }

  Frontend frontend{app, opt};
  int code = 0;
  switch(opt.mode) {
    case Mode::UiShot:          code = frontend.runUiShot(); break;
    case Mode::StateTest:       code = runStateTest(app, opt.warmFrames, opt.frameLimit); break;
    case Mode::DeterminismTest: code = runDeterminismTest(app, opt.frameLimit); break;
    case Mode::TimelineTest:    code = runTimelineTest(app, opt.warmFrames); break;
    case Mode::HotkeyTest:      code = runHotkeyTest(app); break;
    case Mode::LuaTest:         code = runLuaTest(app, opt.luaTest); break;
    case Mode::ShaderTest:      code = runShaderTest(app, opt.warmFrames); break;
    case Mode::CheatTest:       code = runCheatTest(app); break;
    case Mode::MovieTest:       code = runMovieTest(app, opt.warmFrames); break;
    case Mode::Run:             code = frontend.runLoop(); break;
  }

  shutdownApp(app);
  return code;
}

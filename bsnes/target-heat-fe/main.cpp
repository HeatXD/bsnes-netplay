#include "app.hpp"

#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"

#include <string>

namespace {

constexpr int IdleDelayMs = 32;

struct Options {
  std::string romPath;
  std::string uiShot;
  std::string uiScreen = "game";
  int frameLimit = 0;
  int warmFrames = 180;  // emulated before a ui shot, so it lands on a real frame
  int shotTab = -1;
  int shotW = 0, shotH = 0;
  bool fast = false;
  bool uiFullscreen = false;
  bool stateTest = false;
  bool determinismTest = false;
  bool hotkeyTest = false;
};

Options parseArgs(int argc, char** argv) {
  Options opt;
  for(int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    const bool hasValue = i + 1 < argc;

    if(SDL_strcmp(arg, "--fast") == 0) opt.fast = true;
    else if(SDL_strcmp(arg, "--state-test") == 0) opt.stateTest = true;
    else if(SDL_strcmp(arg, "--determinism-test") == 0) opt.determinismTest = true;
    else if(SDL_strcmp(arg, "--hotkey-test") == 0) opt.hotkeyTest = true;
    else if(SDL_strcmp(arg, "--ui-fullscreen") == 0) opt.uiFullscreen = true;
    else if(SDL_strcmp(arg, "--frames") == 0 && hasValue) opt.frameLimit = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--ui-shot") == 0 && hasValue) opt.uiShot = argv[++i];
    else if(SDL_strcmp(arg, "--ui-screen") == 0 && hasValue) opt.uiScreen = argv[++i];
    else if(SDL_strcmp(arg, "--ui-tab") == 0 && hasValue) opt.shotTab = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--ui-warm") == 0 && hasValue) opt.warmFrames = SDL_atoi(argv[++i]);
    else if(SDL_strcmp(arg, "--ui-size") == 0 && i + 2 < argc) {
      opt.shotW = SDL_atoi(argv[i + 1]);
      opt.shotH = SDL_atoi(argv[i + 2]);
      i += 2;
    }
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
  ImGui_ImplOpenGL3_Init(GlslVersion);
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
  for(SDL_Gamepad* pad : app.pads) {
    if(pad && SDL_GetGamepadID(pad) == id) return;
  }
  SDL_Gamepad* opened = SDL_OpenGamepad(id);
  if(!opened) return;

  // reuse a slot left by an unplugged pad before growing the list
  for(SDL_Gamepad*& slot : app.pads) {
    if(!slot) { slot = opened; return; }
  }
  app.pads.push_back(opened);
}

void openGamepads(App& app) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
  for(int i = 0; ids && i < count; i++) addPad(app, ids[i]);
  if(ids) SDL_free(ids);
}

// FNV-1a over the frame the core last handed the shell
uint64_t frameHash(const App& app) {
  uint64_t hash = 1469598103934665603ull;
  const int pixels = app.shell.frameWidth * app.shell.frameHeight;
  for(int i = 0; i < pixels; i++) hash = (hash ^ app.shell.lastPixels[i]) * 1099511628211ull;
  return hash;
}

uint64_t blobHash(const std::vector<uint8_t>& blob) {
  uint64_t hash = 1469598103934665603ull;
  for(uint8_t byte : blob) hash = (hash ^ byte) * 1099511628211ull;
  return hash;
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
  int runStateTest();
  int runDeterminismTest();
  int runHotkeyTest();
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
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Frontend::advance() {
  const bool blockInput = app.settings.defocusPolicy == DefocusBlockInput && !app.focused();

  if(ImGui::GetIO().WantCaptureKeyboard || blockInput) {
    for(int port = 0; port < InputMap::Ports; port++) {
      for(int b = 0; b < EmuCore::MaxInputs; b++) app.core.setInput(port, b, 0);
    }
  } else {
    app.input.apply(app.core, app.pads, app.settings, app.sample, app.emulatedFrames);
  }

  app.core.runFrame();
  app.emulatedFrames++;
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
  if(event.type == SDL_EVENT_QUIT) app.running = false;
  if(event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
  && event.window.windowID == SDL_GetWindowID(app.shell.window)) app.running = false;
  if(event.type == SDL_EVENT_DROP_FILE && event.drop.data) app.loadRom(event.drop.data);
}

void Frontend::handleGamepadEvent(const SDL_Event& event) {
  if(event.type == SDL_EVENT_GAMEPAD_ADDED) {
    addPad(app, event.gdevice.which);
    return;
  }
  if(event.type != SDL_EVENT_GAMEPAD_REMOVED) return;

  // the slot is emptied rather than erased, so unplugging one pad never
  // renumbers the others out from under whoever picked them
  for(SDL_Gamepad*& pad : app.pads) {
    if(!pad || SDL_GetGamepadID(pad) != event.gdevice.which) continue;
    SDL_CloseGamepad(pad);
    pad = nullptr;
    break;
  }
}

// returns true when the event was swallowed by a pending rebind
bool Frontend::handleRebind(const SDL_Event& event) {
  const bool escape = event.type == SDL_EVENT_KEY_DOWN
                   && event.key.scancode == SDL_SCANCODE_ESCAPE;

  if(app.capturing >= 0) {
    // only the pad this port reads may bind it, so two sticks stay distinct
    SDL_Gamepad* owner = app.portPad(app.mapPort, app.mapPlayer);
    Binding b;
    if(escape) app.capturing = -1;
    else if(InputMap::capture(event, b, owner ? SDL_GetGamepadID(owner) : 0)) {
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
    else if(InputMap::capture(event, b, 0, true)) {
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
    app.refreshStatesDir();
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
  for(int i = 0; app.core.loaded() && i < opt.warmFrames; i++) app.core.runFrame();

  // early passes settle window sizing; --frames asks for more to catch layout
  // that drifts per frame
  const int passes = opt.frameLimit > 0 ? opt.frameLimit : 3;
  for(int pass = 0; pass < passes; pass++) {
    app.settingsTab = pass == 0 ? opt.shotTab : -1;
    renderUi();
  }

  // read before swapping, or the back buffer is already gone
  const bool ok = app.shell.saveWindow(opt.uiShot);
  SDL_Log("ui shot %s: %s", ok ? "saved" : "FAILED", opt.uiShot.c_str());
  return ok ? 0 : 1;
}

// save, diverge, restore: the same frames must come back, blob and file alike
int Frontend::runStateTest() {
  auto hashFrame = [&] { return frameHash(app); };
  // a restored machine has not drawn yet, so only what it renders next says anything
  auto replay = [&](int count) {
    for(int i = 0; i < count; i++) app.core.runFrame();
    return hashFrame();
  };

  const int settle = opt.frameLimit > 0 ? opt.frameLimit : 60;
  const uint64_t at = replay(opt.warmFrames);

  const std::vector<uint8_t> state = app.core.serialize();
  if(state.empty()) { SDL_Log("state test: FAIL, the core refused to serialize"); return 1; }
  SDL_Log("state size %d bytes, frame %016llx", (int)state.size(), (unsigned long long)at);

  const uint64_t ahead = replay(settle);
  if(ahead == at) { SDL_Log("state test: FAIL, %d frames changed nothing", settle); return 1; }

  const bool restored = app.core.unserialize(state);
  const bool blob = restored && replay(settle) == ahead;
  SDL_Log("blob restore %s: %d frames replay to %016llx",
          blob ? "ok" : "FAILED", settle, (unsigned long long)ahead);

  // and again through the file layer, header, folders and all
  const bool wrote = app.saveState("statetest", true);
  const uint64_t further = replay(settle);
  const bool read = app.loadState("statetest");
  const bool filed = wrote && read && replay(settle) == further;
  SDL_Log("file round trip %s: %s", filed ? "ok" : "FAILED",
          app.statePath("statetest").c_str());
  app.removeState("statetest");

  // a state from another game must be refused rather than half-applied
  std::vector<uint8_t> damaged = state;
  damaged.resize(damaged.size() / 2);
  const bool refused = !app.core.unserialize(damaged);
  SDL_Log("truncated state refused: %s", refused ? "ok" : "FAILED");

  const bool pass = blob && filed && refused;
  SDL_Log("state test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// same inputs, same result: what netplay's rollback rests on
int Frontend::runDeterminismTest() {
  const int frames = opt.frameLimit > 0 ? opt.frameLimit : 600;

  // a fixed button stream, held eight frames at a time so the game reacts to it
  auto press = [&](int frame) {
    uint32_t bits = 2166136261u + (uint32_t)(frame / 8) * 16777619u;
    bits ^= bits >> 13; bits *= 1274126177u; bits ^= bits >> 16;
    for(int b = 0; b < EmuCore::ButtonCount; b++) app.core.setInput(0, b, (bits >> b) & 1);
  };
  // every frame folded in, not just the last: a divergence that heals still fails
  auto run = [&](int first, int count) {
    uint64_t rolling = 1469598103934665603ull;
    for(int frame = first; frame < first + count; frame++) {
      press(frame);
      app.core.runFrame();
      rolling = (rolling ^ frameHash(app)) * 1099511628211ull;
    }
    return rolling;
  };
  // the cothread stacks are host memory; everything else must be identical
  auto guestHash = [&](const std::vector<uint8_t>& blob) {
    uint64_t hash = 1469598103934665603ull;
    for(const auto& part : app.core.stateMap(false)) {
      if(part.hostState) continue;
      for(int i = part.offset; i < part.offset + part.size && i < (int)blob.size(); i++) {
        hash = (hash ^ blob[i]) * 1099511628211ull;
      }
    }
    return hash;
  };
  auto reportDiff = [&](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if(a.size() != b.size()) { SDL_Log("  sizes differ: %d vs %d", (int)a.size(), (int)b.size()); return; }
    for(const auto& part : app.core.stateMap(false)) {
      const int end = SDL_min(part.offset + part.size, (int)a.size());
      for(int i = part.offset; i < end; i++) {
        if(a[i] == b[i]) continue;
        SDL_Log("  %-12s differs at +%d%s", part.name.c_str(), i - part.offset,
                part.hostState ? "  (host state, expected)" : "  <-- machine state");
        break;
      }
    }
  };
  // both captures, so the rollback answer is not confused with the quick-state one
  auto rollbackReplay = [&](bool synchronize) {
    app.core.setOption("Hacks/Entropy", "None");
    app.core.power();
    run(0, frames / 2);
    const std::vector<uint8_t> save = app.core.serialize(synchronize);
    const uint64_t ahead = run(frames / 2, frames - frames / 2);
    const std::vector<uint8_t> aheadState = app.core.serialize(synchronize);

    if(!app.core.unserialize(save)) { SDL_Log("  restore refused"); return false; }
    const uint64_t again = run(frames / 2, frames - frames / 2);
    const std::vector<uint8_t> againState = app.core.serialize(synchronize);
    const bool sameFrames = again == ahead;
    const bool sameGuest = guestHash(againState) == guestHash(aheadState);
    SDL_Log("replay across a %s restore: frames %s, machine state %s (%016llx)",
            synchronize ? "synchronized" : "deterministic",
            sameFrames ? "match" : "DIFFER", sameGuest ? "matches" : "DIFFERS",
            (unsigned long long)ahead);
    if(againState != aheadState) reportDiff(aheadState, againState);
    return sameFrames && sameGuest;
  };

  bool pass = true;
  for(const char* entropy : {"None", "Low", "High"}) {
    app.core.setOption("Hacks/Entropy", entropy);
    app.core.power();
    const uint64_t first = run(0, frames);
    const uint64_t firstState = guestHash(app.core.serialize(false));
    app.core.power();
    const uint64_t second = run(0, frames);
    // randomised RAM the game never draws still lands in the state
    const bool sameFrames = first == second;
    const bool sameState = firstState == guestHash(app.core.serialize(false));

    // only None promises anything; the others are there to randomise
    SDL_Log("entropy %-4s: %d frames %016llx %s, machine state %s%s", entropy, frames,
            (unsigned long long)first, sameFrames ? "repeat" : "DIFFER",
            sameState ? "repeats" : "differs",
            SDL_strcmp(entropy, "None") == 0 ? "" : " (expected, it randomises)");
    if(SDL_strcmp(entropy, "None") == 0) pass = pass && sameFrames && sameState;
  }

  // the rollback shape: restore, feed the same inputs, land in the same place
  SDL_Log("libco deterministic states: %s", EmuCore::deterministicStates() ? "yes" : "no");
  const bool synced = rollbackReplay(true);
  const bool unsynced = rollbackReplay(false);
  // only the deterministic capture promises this
  pass = pass && unsynced;
  (void)synced;

  // for diffing two runs of the exe; the machine hash is what a peer checksums
  app.core.setOption("Hacks/Entropy", "None");
  app.core.power();
  const uint64_t signature = run(0, frames);
  SDL_Log("signature frames %016llx machine %016llx", (unsigned long long)signature,
          (unsigned long long)guestHash(app.core.serialize(false)));
  SDL_Log("determinism test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// hotkeyHeld against a fabricated keyboard, since SDL's own state cannot be driven
int Frontend::runHotkeyTest() {
  bool keys[SDL_SCANCODE_COUNT] = {};
  InputSample sample;
  sample.keys = keys;
  bool pass = true;

  auto set = [&](SDL_Scancode a, SDL_Scancode b, int mods) {
    for(bool& key : keys) key = false;
    if(a != SDL_SCANCODE_UNKNOWN) keys[a] = true;
    if(b != SDL_SCANCODE_UNKNOWN) keys[b] = true;
    sample.mods = mods;
  };
  auto expect = [&](const char* what, int index, int logic, bool want) {
    const bool got = app.input.hotkeyHeld(index, sample, app.pads, logic);
    SDL_Log("%-30s %d (want %d)%s", what, got, want, got == want ? "" : "  <-- WRONG");
    pass = pass && got == want;
  };

  // two slots, the old way of building a chord: both keys held under And logic
  app.input.hotkey(HkPause, 0) = {Binding::Key, SDL_SCANCODE_LCTRL, 0, 0};
  app.input.hotkey(HkPause, 1) = {Binding::Key, SDL_SCANCODE_F, 0, 0};

  set(SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, 0);
  expect("or, neither held", HkPause, LogicOr, false);
  set(SDL_SCANCODE_LCTRL, SDL_SCANCODE_UNKNOWN, 0);
  expect("or, one held", HkPause, LogicOr, true);
  expect("and, one held", HkPause, LogicAnd, false);
  set(SDL_SCANCODE_LCTRL, SDL_SCANCODE_F, 0);
  expect("and, both held", HkPause, LogicAnd, true);

  // one slot holding a real chord, which is what the rebinder now records
  app.input.hotkey(HkReset, 0) = {Binding::Key, SDL_SCANCODE_2, 0, normalizeMods(SDL_KMOD_LSHIFT)};
  app.input.hotkey(HkReset, 1) = {};

  set(SDL_SCANCODE_2, SDL_SCANCODE_UNKNOWN, 0);
  expect("shift+2 bound, 2 alone", HkReset, LogicOr, false);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LSHIFT, normalizeMods(SDL_KMOD_LSHIFT));
  expect("shift+2 bound, shift+2", HkReset, LogicOr, true);
  set(SDL_SCANCODE_2, SDL_SCANCODE_RSHIFT, normalizeMods(SDL_KMOD_RSHIFT));
  expect("shift+2 bound, right shift", HkReset, LogicOr, true);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LCTRL, normalizeMods(SDL_KMOD_LCTRL));
  expect("shift+2 bound, ctrl+2", HkReset, LogicOr, false);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LSHIFT, normalizeMods((SDL_Keymod)(SDL_KMOD_LSHIFT | SDL_KMOD_LCTRL)));
  expect("shift+2 bound, ctrl+shift+2", HkReset, LogicOr, false);

  // a plain key must not fire while a modifier is down, or the chord is ambiguous
  app.input.hotkey(HkQuit, 0) = {Binding::Key, SDL_SCANCODE_2, 0, 0};
  app.input.hotkey(HkQuit, 1) = {};
  set(SDL_SCANCODE_2, SDL_SCANCODE_UNKNOWN, 0);
  expect("2 bound, 2 alone", HkQuit, LogicOr, true);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LSHIFT, normalizeMods(SDL_KMOD_LSHIFT));
  expect("2 bound, shift+2", HkQuit, LogicOr, false);

  SDL_Log("pads connected during this test: %d", (int)app.pads.size());
  SDL_Log("hotkey logic test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
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
  app.refreshStatesDir();
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
}

// --ui-screen decides which panel is already open when the frame loop starts
void openRequestedPanel(App& app, const Options& opt) {
  app.showSettings = opt.uiScreen == "settings";
  app.showTools = opt.uiScreen == "tools";
  app.showGames = opt.uiScreen == "games";
  app.showAbout = opt.uiScreen == "about";
  // cartridge is the old name for the same window
  app.showManifest = opt.uiScreen == "manifest" || opt.uiScreen == "cartridge";
  app.settingsTab = opt.shotTab;
}

void shutdownApp(App& app) {
  shutdownImGui();
  for(SDL_Gamepad* pad : app.pads) if(pad) SDL_CloseGamepad(pad);
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

  app.scanGames();
  initImGui(app, opt.uiShot.empty());
  loadGamepadMappings();
  openGamepads(app);
  wireCore(app);
  openRequestedPanel(app, opt);

  if(!opt.romPath.empty()) app.loadRom(opt.romPath);
  if((opt.frameLimit || opt.stateTest || opt.determinismTest)
  && opt.uiShot.empty() && !app.core.loaded()) {
    SDL_Log("no rom loaded");
    shutdownApp(app);
    return 1;
  }

  Frontend frontend{app, opt};
  const int code = opt.hotkeyTest ? frontend.runHotkeyTest()
                 : opt.determinismTest ? frontend.runDeterminismTest()
                 : opt.stateTest  ? frontend.runStateTest()
                 : opt.uiShot.empty() ? frontend.runLoop()
                 : frontend.runUiShot();

  shutdownApp(app);
  return code;
}

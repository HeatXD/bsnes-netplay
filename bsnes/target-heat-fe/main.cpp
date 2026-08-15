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
};

Options parseArgs(int argc, char** argv) {
  Options opt;
  for(int i = 1; i < argc; i++) {
    const char* arg = argv[i];
    const bool hasValue = i + 1 < argc;

    if(SDL_strcmp(arg, "--fast") == 0) opt.fast = true;
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
// opening one twice would list the same stick under two ports
void addPad(App& app, SDL_JoystickID id) {
  for(SDL_Gamepad* pad : app.pads) {
    if(SDL_GetGamepadID(pad) == id) return;
  }
  if(SDL_Gamepad* pad = SDL_OpenGamepad(id)) app.pads.push_back(pad);
}

void openGamepads(App& app) {
  int count = 0;
  SDL_JoystickID* ids = SDL_GetGamepads(&count);
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
  glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
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
    app.input.apply(app.core, app.pads, app.settings, app.emulatedFrames);
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

  const bool unfocusedPause = app.settings.defocusPolicy == DefocusPause
                           && !app.focused() && !opt.frameLimit;
  const bool stopped = !app.core.loaded() || (app.paused && !app.frameAdvance) || unfocusedPause;

  if(!stopped) advance();
  app.frameAdvance = false;

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
  if(!stopped && !opt.fast) app.shell.pace(app.settings);

  inFrame = false;
  return stopped;
}

void Frontend::handleWindowEvent(const SDL_Event& event) {
  if(event.type == SDL_EVENT_QUIT) app.running = false;
  if(event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
  && event.window.windowID == SDL_GetWindowID(app.shell.window)) app.running = false;
}

void Frontend::handleGamepadEvent(const SDL_Event& event) {
  if(event.type == SDL_EVENT_GAMEPAD_ADDED) {
    addPad(app, event.gdevice.which);
    return;
  }
  if(event.type != SDL_EVENT_GAMEPAD_REMOVED) return;

  for(size_t i = 0; i < app.pads.size(); i++) {
    if(SDL_GetGamepadID(app.pads[i]) != event.gdevice.which) continue;
    SDL_CloseGamepad(app.pads[i]);
    app.pads.erase(app.pads.begin() + i);
    break;
  }
}

// returns true when the event was swallowed by a pending rebind
bool Frontend::handleRebind(const SDL_Event& event) {
  const bool escape = event.type == SDL_EVENT_KEY_DOWN
                   && event.key.scancode == SDL_SCANCODE_ESCAPE;

  if(app.capturing >= 0) {
    // only the pad this port reads may bind it, so two sticks stay distinct
    SDL_Gamepad* owner = app.portPad(app.mapPort);
    Binding b;
    if(escape) app.capturing = -1;
    else if(InputMap::capture(event, b, owner ? SDL_GetGamepadID(owner) : 0)) {
      app.input.binding(app.mapPort, app.core.connectedDevice(app.mapPort),
                        app.capturing / InputMap::Slots,
                        app.capturing % InputMap::Slots) = b;
      app.input.save(app.inputCfg);
      app.capturing = -1;
    }
    return true;
  }

  if(app.capturingHotkey >= 0) {
    if(event.type == SDL_EVENT_KEY_DOWN) {
      if(!escape) {
        app.settings.hotkeys[app.capturingHotkey] = (int)event.key.scancode;
        app.settings.save(app.settingsCfg);
      }
      app.capturingHotkey = -1;
    }
    return true;
  }

  return false;
}

void Frontend::handleHotkey(const SDL_Event& event) {
  if(event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
  if(ImGui::GetIO().WantCaptureKeyboard) return;

  const int key = (int)event.key.scancode;
  if(event.key.scancode == SDL_SCANCODE_O && (event.key.mod & SDL_KMOD_CTRL)) {
    app.openRomDialog();
  } else if(key == app.settings.hotkeys[HkPause] && app.core.loaded()) {
    app.paused = !app.paused;
  } else if(key == app.settings.hotkeys[HkReset] && app.core.loaded()) {
    app.reset();
  } else if(key == app.settings.hotkeys[HkFastForward]) {
    app.toggleFastForward();
  } else if(key == app.settings.hotkeys[HkScreenshot]) {
    app.takeScreenshot();
  } else if(key == app.settings.hotkeys[HkFullscreen]) {
    app.toggleFullscreen();
  } else if(key == app.settings.hotkeys[HkFrameAdvance] && app.core.loaded()) {
    app.paused = true;
    app.advanceOneFrame();
  } else if(key == app.settings.hotkeys[HkPowerCycle] && app.core.loaded()) {
    app.powerCycle();
  } else if(key == app.settings.hotkeys[HkMute]) {
    app.settings.mute = !app.settings.mute;
    app.settings.save(app.settingsCfg);
  } else if(key == app.settings.hotkeys[HkQuit]) {
    app.running = false;
  } else if(key == app.settings.hotkeys[HkSpeedDown]) {
    app.setSpeed(app.speedIndex - 1);
  } else if(key == app.settings.hotkeys[HkSpeedUp]) {
    app.setSpeed(app.speedIndex + 1);
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
    if(!picked.empty()) app.openMediaDialog(app.sufamiBPick, "Sufami Turbo ROMs", "st;zip;7z");
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

// The core caches these rather than reading Settings per frame, so a loaded
// config has to be pushed in explicitly; without this it keeps its own
// defaults until the matching tab happens to be opened.
void applySettingsToCore(App& app) {
  const Settings& s = app.settings;
  auto flag = [](bool on) { return on ? "true" : "false"; };
  char number[16];

  app.core.setSavesDirectory(s.savesDir);
  app.core.setFirmwareDirectory(app.firmwareDir());
  app.core.setOverscanCrop(s.overscanCrop);
  app.core.setPaletteAdjust(s.videoGamma, s.videoLuminance, s.videoSaturation);
  app.core.setFilter(s.videoFilter);
  app.core.setOption("Video/BlurEmulation", flag(s.hiresBlur));

  app.core.setOption("Hacks/PPU/Fast", flag(s.hackPpuFast));
  app.core.setOption("Hacks/PPU/NoSpriteLimit", flag(s.hackPpuNoSpriteLimit));
  SDL_itoa(s.hackMode7Scale, number, 10);
  app.core.setOption("Hacks/PPU/Mode7/Scale", number);
  app.core.setOption("Hacks/DSP/Fast", flag(s.hackDspFast));
  app.core.setOption("Hacks/DSP/Cubic", flag(s.hackDspCubic));
  app.core.setOption("Hacks/Coprocessor/DelayedSync", flag(s.hackCoprocessorDelayedSync));
  app.core.setOption("Hacks/Coprocessor/PreferHLE", flag(s.hackCoprocessorPreferHLE));
  app.core.setOption("Frontend/Hotfixes", flag(s.hackHotfixes));
}

void loadConfigs(App& app) {
  app.inputCfg = prefFile("input.cfg");
  app.settingsCfg = prefFile("settings.cfg");
  app.input.load(app.inputCfg);
  app.settings.load(app.settingsCfg);
  applySettingsToCore(app);

  for(int port = 0; port < EmuCore::PortCount; port++) {
    app.core.connect(port, app.settings.devices[port]);
  }
}

void wireCore(App& app) {
  app.core.setAudioFrequency(AudioRate);
  app.core.onVideo = [&app](const uint32_t* argb, int width, int height) {
    app.shell.pushVideo(argb, width, height);
  };
  app.core.onAudio = [&app](const float* samples, int frames) {
    app.shell.pushAudio(app.settings, samples, frames, app.focused());
    app.totalSamples += frames;
  };
}

// --ui-screen decides which panel is already open when the frame loop starts
void openRequestedPanel(App& app, const Options& opt) {
  app.showSettings = opt.uiScreen == "settings";
  app.showTools = opt.uiScreen == "tools";
  app.showGames = opt.uiScreen == "games";
  app.showAbout = opt.uiScreen == "about";
  app.showCartridge = opt.uiScreen == "cartridge";
  app.settingsTab = opt.shotTab;
}

void shutdownApp(App& app) {
  shutdownImGui();
  for(SDL_Gamepad* pad : app.pads) SDL_CloseGamepad(pad);
  app.core.unload();
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
  if(opt.frameLimit && opt.uiShot.empty() && !app.core.loaded()) {
    SDL_Log("no rom loaded");
    shutdownApp(app);
    return 1;
  }

  Frontend frontend{app, opt};
  const int code = opt.uiShot.empty() ? frontend.runLoop() : frontend.runUiShot();

  shutdownApp(app);
  return code;
}

#include "../bsnes.hpp"
#include "platform.cpp"
#include "game.cpp"
#include "game-pak.cpp"
#include "game-rom.cpp"
#include "paths.cpp"
#include "states.cpp"
#include "movies.cpp"
#include "rewind.cpp"
#include "netplay-stress.cpp"
#include "netplay.cpp"
#include "netplay-weyve.cpp"
#include "video.cpp"
#include "audio.cpp"
#include "input.cpp"
#include "utility.cpp"
#include "patch.cpp"
#include "hacks.cpp"
#include "filter.cpp"
#include "viewport.cpp"
Program program;

auto Program::create() -> void {
  Emulator::platform = this;

  presentation.create();
  presentation.setVisible();
  presentation.viewport.setFocused();

  settingsWindow.create();
  videoSettings.create();
  audioSettings.create();
  inputSettings.create();
  hotkeySettings.create();
  pathSettings.create();
  emulatorSettings.create();
  enhancementSettings.create();
  compatibilitySettings.create();
  driverSettings.create();
  netplaySettings.create();

  toolsWindow.create();
  cheatFinder.create();
  cheatDatabase.create();
  cheatWindow.create();
  cheatEditor.create();
  stateWindow.create();
  stateManager.create();
  manifestViewer.create();

  netplayWindow.create();
  weyveWindow.create();
  weyveHostSettings.create();

  if(settings.general.crashed) {
    MessageDialog(
      "Driver crash detected. Hardware drivers have been disabled.\n"
      "Please reconfigure drivers in the advanced settings panel."
    ).setAlignment(*presentation).information();
    settings.video.driver = "None";
    settings.audio.driver = "None";
    settings.input.driver = "None";
  }

  settings.general.crashed = true;
  settings.save();
  updateVideoDriver(presentation);
  updateAudioDriver(presentation);
  updateInputDriver(presentation);
  settings.general.crashed = false;
  settings.save();

  driverSettings.videoDriverChanged();
  driverSettings.audioDriverChanged();
  driverSettings.inputDriverChanged();

  if(gameQueue) load();
  if(startFullScreen && emulator->loaded()) {
    toggleVideoFullScreen();
  }

  Application::onMain({&Program::main, this});
}

auto Program::main() -> void {
  updateStatus();
  video.poll();

  // before inactive() is sampled, or this frame runs the emulator on freed ROM
  if(pendingUnload) {
    pendingUnload = false;
    unload();
  }

  if(Application::modal()) {
    audio.clear();
    //keep netplay alive during window drags/resizes, muted since frame pacing is irregular here
    mute |= Mute::Modal;
    netplayRun();
    weyvePoll();
    return;
  }
  mute &= ~Mute::Modal;

  inputManager.poll();
  inputManager.pollHotkeys();

  static bool previouslyInactive = true;
  bool currentlyInactive = inactive();

  // check if emulator has transitioned from active to inactive or vice versa
  if(previouslyInactive != currentlyInactive) {
    previouslyInactive = currentlyInactive;
    Application::setScreenSaver(currentlyInactive || settings.general.screenSaver);
  }

  if(currentlyInactive) {
    audio.clear();
    weyvePoll();
    usleep(20 * 1000);
    if(settings.emulator.runAhead.frames == 0) viewportRefresh();
    return;
  }

  weyvePoll();
  if(netplayRun()) return;

  rewindRun();

  if(!settings.emulator.runAhead.frames || fastForwarding || rewinding) {
    emulator->run();
  } else {
    emulator->setRunAhead(true);
    emulator->run();
    auto state = emulator->serialize(0);
    if(settings.emulator.runAhead.frames >= 2) emulator->run();
    if(settings.emulator.runAhead.frames >= 3) emulator->run();
    if(settings.emulator.runAhead.frames >= 4) emulator->run();
    emulator->setRunAhead(false);
    emulator->run();
    state.setMode(serializer::Mode::Load);
    emulator->unserialize(state);
  }

  if(emulatorSettings.autoSaveMemory.checked()) {
    auto currentTime = chrono::timestamp();
    if(currentTime - autoSaveTime >= settings.emulator.autoSaveMemory.interval) {
      autoSaveTime = currentTime;
      emulator->save();
    }
  }
}

auto Program::quit() -> void {
  //make closing the program feel more responsive
  presentation.setVisible(false);
  Application::processEvents();

  //in case the emulator was closed prior to initialization completing:
  settings.general.crashed = false;

  unload();
  settings.save();
  video.reset();
  audio.reset();
  input.reset();

  #if defined(PLATFORM_WINDOWS)
  //in rare cases, when Application::exit() calls exit(0), a crash will occur.
  //this seems to be due to the internal state of certain ruby drivers.
  auto processID = GetCurrentProcessId();
  auto handle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, true, processID);
  TerminateProcess(handle, 0);
  #endif

  Application::exit();
}

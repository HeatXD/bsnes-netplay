#include "tests.hpp"

namespace {
constexpr uint64_t FnvBasis = 1469598103934665603ull;
constexpr uint64_t FnvPrime = 1099511628211ull;

uint64_t fnv(uint64_t hash, uint64_t value) { return (hash ^ value) * FnvPrime; }

// FNV-1a over the frame the core last handed the shell
uint64_t frameHash(const App& app) {
  uint64_t hash = FnvBasis;
  const int pixels = app.shell.frameWidth * app.shell.frameHeight;
  for(int i = 0; i < pixels; i++) hash = fnv(hash, app.shell.lastPixels[i]);
  return hash;
}
// a scratch state folder for as long as it is alive, so a self test never
// writes into the states the user owns, whatever path it returns by
struct ScratchStates {
  App& app;
  const std::string previous;

  explicit ScratchStates(App& app) : app(app), previous(app.settings.statesDir) {
    app.settings.statesDir = configDir() + "States-selftest";
  }
  ~ScratchStates() {
    app.removeAllStates();
    SDL_RemovePath(app.statesDir().c_str());
    app.settings.statesDir = previous;
  }
};
}  // namespace

// save, diverge, restore: the same frames must come back, blob and file alike
int runStateTest(App& app, int warmFrames, int frames) {
  // a restored machine has not drawn yet, so only what it renders next says anything
  auto replay = [&](int count) {
    for(int i = 0; i < count; i++) app.core.runFrame();
    return frameHash(app);
  };

  const int settle = frames > 0 ? frames : 60;
  const uint64_t at = replay(warmFrames);

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
  const ScratchStates scratch(app);
  const bool wrote = app.saveState("statetest", true);
  const uint64_t further = replay(settle);
  const bool read = app.loadState("statetest");
  const bool filed = wrote && read && replay(settle) == further;
  SDL_Log("file round trip %s: %s", filed ? "ok" : "FAILED",
          app.statePath("statetest").c_str());

  const bool managedSaved = app.saveState("Managed/original", true);
  const bool managedListed = app.availableStates(true).size() == 1;
  const bool managedRenamed = app.renameState("Managed/original", "Managed/renamed")
                           && app.hasState("Managed/renamed")
                           && !app.hasState("Managed/original");
  const bool managedRemoved = app.removeState("Managed/renamed");
  SDL_RemovePath((app.stateFolder() + "/Managed").c_str());
  const bool managed = managedSaved && managedListed && managedRenamed && managedRemoved;
  SDL_Log("managed states %s", managed ? "ok" : "FAILED");

  // undo and redo are memory slots: loading must be reversible, and redoing
  // must land back where the undo came from
  app.saveState("statetest", true);
  const uint64_t loop = replay(settle);
  app.loadState("statetest");
  const bool undone = app.loadState("undo");
  const bool redone = app.loadState("redo");
  const bool reversible = undone && redone && replay(settle) == loop;
  SDL_Log("undo and redo %s, held in memory (%d and %d bytes)",
          reversible ? "ok" : "FAILED", (int)app.undoState.size(), (int)app.redoState.size());
  // the guard clears the slots it knows about; this one is the test's own
  app.removeState("statetest");

  // a state from another game must be refused rather than half-applied
  std::vector<uint8_t> damaged = state;
  damaged.resize(damaged.size() / 2);
  const bool refused = !app.core.unserialize(damaged);
  SDL_Log("truncated state refused: %s", refused ? "ok" : "FAILED");

  const bool pass = blob && filed && managed && reversible && refused;
  SDL_Log("state test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

// same inputs, same result: what netplay's rollback rests on
int runDeterminismTest(App& app, int frames) {
  if(frames <= 0) frames = 600;
  // the layout is fixed for a cartridge, so it is read once, not per hash
  const std::vector<EmuCore::StateComponent> map = app.core.stateMap(false);

  // a fixed button stream, held eight frames at a time so the game reacts to it
  auto press = [&](int frame) {
    uint32_t bits = 2166136261u + (uint32_t)(frame / 8) * 16777619u;
    bits ^= bits >> 13; bits *= 1274126177u; bits ^= bits >> 16;
    for(int b = 0; b < EmuCore::ButtonCount; b++) app.core.setInput(0, b, (bits >> b) & 1);
  };
  // every frame folded in, not just the last: a divergence that heals still fails
  auto run = [&](int first, int count) {
    uint64_t rolling = FnvBasis;
    for(int frame = first; frame < first + count; frame++) {
      press(frame);
      app.core.runFrame();
      rolling = fnv(rolling, frameHash(app));
    }
    return rolling;
  };
  // the cothread stacks are host memory; everything else must be identical
  auto guestHash = [&](const std::vector<uint8_t>& blob) {
    uint64_t hash = FnvBasis;
    for(const auto& part : map) {
      if(part.hostState) continue;
      for(int i = part.offset; i < part.offset + part.size && i < (int)blob.size(); i++) {
        hash = fnv(hash, blob[i]);
      }
    }
    return hash;
  };
  auto reportDiff = [&](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if(a.size() != b.size()) { SDL_Log("  sizes differ: %d vs %d", (int)a.size(), (int)b.size()); return; }
    for(const auto& part : map) {
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
  // the synchronized capture runs for its log line; only the deterministic
  // one promises to land in the same place
  rollbackReplay(true);
  pass = pass && rollbackReplay(false);

  // for diffing two runs of the exe; the machine hash is what a peer checksums
  app.core.setOption("Hacks/Entropy", "None");
  app.core.power();
  const uint64_t signature = run(0, frames);
  SDL_Log("signature frames %016llx machine %016llx", (unsigned long long)signature,
          (unsigned long long)guestHash(app.core.serialize(false)));
  SDL_Log("determinism test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int runTimelineTest(App& app, int warmFrames) {
  const int oldFrequency = app.settings.rewindFrequency;
  const int oldLength = app.settings.rewindLength;
  const int oldRunAhead = app.settings.runAheadFrames;
  auto restoreSettings = [&] {
    app.settings.rewindFrequency = oldFrequency;
    app.settings.rewindLength = oldLength;
    app.settings.runAheadFrames = oldRunAhead;
    app.core.setRunAhead(false);
    app.resetTimeline();
  };

  app.core.setOption("Hacks/Entropy", "None");
  app.core.power();
  for(int frame = 0; frame < warmFrames; frame++) app.core.runFrame();
  const std::vector<uint8_t> baseline = app.core.serialize(false);
  const std::vector<EmuCore::StateComponent> map = app.core.stateMap(false);
  auto guestHash = [&](const std::vector<uint8_t>& state) {
    uint64_t hash = FnvBasis;
    for(const auto& part : map) {
      if(part.hostState) continue;
      for(int i = part.offset; i < part.offset + part.size && i < (int)state.size(); i++) {
        hash = fnv(hash, state[i]);
      }
    }
    return hash;
  };
  auto run = [&](int frames) {
    for(int frame = 0; frame < frames; frame++) app.core.runFrame();
  };

  app.core.unserialize(baseline);
  run(3);
  const uint64_t futureFrame = frameHash(app);
  app.core.unserialize(baseline);
  run(1);
  const uint64_t oneFrameState = guestHash(app.core.serialize(false));

  app.core.unserialize(baseline);
  app.settings.rewindFrequency = 0;
  app.settings.runAheadFrames = 2;
  app.resetTimeline();
  app.advanceEmulation();
  const bool runAheadVideo = frameHash(app) == futureFrame;
  const bool runAheadState = guestHash(app.core.serialize(false)) == oneFrameState;
  SDL_Log("run-ahead: future frame %s, authoritative state %s",
          runAheadVideo ? "ok" : "FAILED", runAheadState ? "ok" : "FAILED");

  app.core.unserialize(baseline);
  run(20);
  const uint64_t twentyFrameState = guestHash(app.core.serialize(false));
  app.core.unserialize(baseline);
  app.settings.runAheadFrames = 0;
  app.settings.rewindFrequency = 5;
  app.settings.rewindLength = 3;
  app.resetTimeline();
  for(int frame = 0; frame < 25; frame++) app.advanceEmulation();
  const bool bounded = app.rewindHistory.size() == 3;
  app.setRewinding(true);
  app.advanceEmulation();
  app.advanceEmulation();
  const bool rewound = guestHash(app.core.serialize(false)) == twentyFrameState;
  SDL_Log("rewind: history bound %s, restored timeline %s",
          bounded ? "ok" : "FAILED", rewound ? "ok" : "FAILED");

  restoreSettings();
  const bool pass = !baseline.empty() && runAheadVideo && runAheadState && bounded && rewound;
  SDL_Log("timeline test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int runCheatTest(App& app) {
  const std::string oldDir = app.settings.cheatsDir;
  const std::string oldDatabaseDir = app.settings.databaseDir;
  const bool oldEnabled = app.settings.cheatsEnabled;
  const std::vector<CheatEntry> oldCheats = app.cheats;
  app.settings.cheatsDir = configDir() + "Cheats-selftest";
  app.settings.cheatsEnabled = true;

  const uint8_t original = app.core.readMemory(EmuCore::MemoryDomain::WRAM, 0);
  app.core.setCheats({"7e0000=42"});
  const bool applied = app.core.readMemory(EmuCore::MemoryDomain::WRAM, 0) == 0x42;
  app.core.setCheats({});
  const bool restored = app.core.readMemory(EmuCore::MemoryDomain::WRAM, 0) == original;

  std::string standard = "7E000042";
  const bool decoded = app.normalizeCheatCode(standard) && standard == "7e0000=42";
  app.settings.databaseDir = normalPath(configDir() + "../Database");
  const bool database = !app.findDatabaseCheats().empty();
  app.cheats = {{"Self test", standard, true}};
  app.cheatsDirty = true;
  app.saveCheats();
  app.cheats.clear();
  app.loadCheats();
  const bool persisted = app.cheats.size() == 1 && app.cheats[0].name == "Self test"
                      && app.cheats[0].code == standard && app.cheats[0].enabled;

  app.core.setCheats({});
  app.cheats.clear();
  SDL_RemovePath(app.cheatPath().c_str());
  SDL_RemovePath(app.settings.cheatsDir.c_str());
  app.settings.cheatsDir = oldDir;
  app.settings.databaseDir = oldDatabaseDir;
  app.settings.cheatsEnabled = oldEnabled;
  app.cheats = oldCheats;
  app.cheatsDirty = false;
  app.applyCheats();
  SDL_Log("cheats: apply %s, restore %s, decode %s, database %s, persistence %s",
          applied ? "ok" : "FAILED", restored ? "ok" : "FAILED",
          decoded ? "ok" : "FAILED", database ? "ok" : "FAILED",
          persisted ? "ok" : "FAILED");
  return applied && restored && decoded && database && persisted ? 0 : 1;
}

// hotkeyHeld against a fabricated keyboard, since SDL's own state cannot be driven
int runHotkeyTest(App& app) {
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
  auto expect = [&](const char* what, int index, bool want) {
    const bool got = app.input.hotkeyHeld(index, sample, app.pads);
    SDL_Log("%-30s %d (want %d)%s", what, got, want, got == want ? "" : "  <-- WRONG");
    pass = pass && got == want;
  };

  // two mappings on one action: either one fires it
  app.input.hotkey(HkPause, 0) = {Binding::Key, SDL_SCANCODE_G, 0, 0};
  app.input.hotkey(HkPause, 1) = {Binding::Key, SDL_SCANCODE_F, 0, 0};

  set(SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, 0);
  expect("two mappings, neither held", HkPause, false);
  set(SDL_SCANCODE_G, SDL_SCANCODE_UNKNOWN, 0);
  expect("two mappings, first held", HkPause, true);
  set(SDL_SCANCODE_F, SDL_SCANCODE_UNKNOWN, 0);
  expect("two mappings, second held", HkPause, true);

  // one mapping holding a chord, which is what the rebinder records
  app.input.hotkey(HkReset, 0) = {Binding::Key, SDL_SCANCODE_2, 0, normalizeMods(SDL_KMOD_LSHIFT)};
  app.input.hotkey(HkReset, 1) = {};

  set(SDL_SCANCODE_2, SDL_SCANCODE_UNKNOWN, 0);
  expect("shift+2 bound, 2 alone", HkReset, false);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LSHIFT, normalizeMods(SDL_KMOD_LSHIFT));
  expect("shift+2 bound, shift+2", HkReset, true);
  set(SDL_SCANCODE_2, SDL_SCANCODE_RSHIFT, normalizeMods(SDL_KMOD_RSHIFT));
  expect("shift+2 bound, right shift", HkReset, true);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LCTRL, normalizeMods(SDL_KMOD_LCTRL));
  expect("shift+2 bound, ctrl+2", HkReset, false);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LSHIFT, normalizeMods((SDL_Keymod)(SDL_KMOD_LSHIFT | SDL_KMOD_LCTRL)));
  expect("shift+2 bound, ctrl+shift+2", HkReset, false);

  // a plain key must not fire while a modifier is down, or the chord is ambiguous
  app.input.hotkey(HkQuit, 0) = {Binding::Key, SDL_SCANCODE_2, 0, 0};
  app.input.hotkey(HkQuit, 1) = {};
  set(SDL_SCANCODE_2, SDL_SCANCODE_UNKNOWN, 0);
  expect("2 bound, 2 alone", HkQuit, true);
  set(SDL_SCANCODE_2, SDL_SCANCODE_LSHIFT, normalizeMods(SDL_KMOD_LSHIFT));
  expect("2 bound, shift+2", HkQuit, false);

  // Unmapped HID devices arrive through SDL's raw joystick events. Ensure all
  // three bindable control shapes survive capture without real test hardware.
  auto expectRaw = [&](const char* what, SDL_Event event, Binding::Type type,
                       int code, int direction) {
    Binding binding;
    const bool captured = InputMap::capture(event, binding);
    const bool right = captured && binding.type == type && binding.code == code
                    && binding.direction == direction;
    SDL_Log("%-30s %d%s", what, right, right ? "" : "  <-- WRONG");
    pass = pass && right;
  };
  SDL_Event button{};
  button.type = SDL_EVENT_JOYSTICK_BUTTON_DOWN;
  button.jbutton.which = 0;
  button.jbutton.button = 4;
  expectRaw("raw joystick button", button, Binding::JoyButton, 4, 0);

  SDL_Event axis{};
  axis.type = SDL_EVENT_JOYSTICK_AXIS_MOTION;
  axis.jaxis.which = 0;
  axis.jaxis.axis = 2;
  axis.jaxis.value = -20000;
  expectRaw("raw joystick axis", axis, Binding::JoyAxis, 2, -1);

  SDL_Event hat{};
  hat.type = SDL_EVENT_JOYSTICK_HAT_MOTION;
  hat.jhat.which = 0;
  hat.jhat.hat = 1;
  hat.jhat.value = SDL_HAT_RIGHT;
  expectRaw("raw joystick hat", hat, Binding::JoyHat, 1, SDL_HAT_RIGHT);

  SDL_Log("pads connected during this test: %d", (int)app.pads.size());
  SDL_Log("hotkey logic test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int runLuaTest(App& app, const std::string& script) {
  auto setPhysicalInput = [&] {
    app.core.setInput(0, EmuCore::A, 0);
    app.core.setInput(0, EmuCore::B, 1);
    app.core.setInput(0, EmuCore::Right, 1);
    app.core.setInput(0, EmuCore::Y, 1);
  };
  setPhysicalInput();
  bool pass = app.scripting.load(script);
  pass = pass && app.scripting.running();
  const bool controlsTest = app.scripting.globalInteger("expected_controls") > 0;
  const std::string originalStatesDir = app.settings.statesDir;
  const std::string controlsDataDir = controlsTest ? app.scripting.dataDirectory() : std::string();
  const std::string controlsStateDir = controlsTest
                                     ? controlsDataDir + "/_state_test" : std::string();
  const std::string packagedState = controlsTest
                                  ? parentDir(script) + "/_heat_fe_package_state.bst" : std::string();
  if(controlsTest) {
    app.settings.statesDir = controlsStateDir;
    pass = app.saveStateFile(packagedState, true) && pass;
  }
  if(pass && app.scripting.globalInteger("expected_frame_error") > 0) {
    setPhysicalInput();
    const bool cleared = app.scripting.runBeforeFrame() && !app.scripting.runFrame()
                      && !app.scripting.running()
                      && app.scripting.commandCount() == 0
                      && app.core.inputValue(0, EmuCore::A) == 0
                      && app.scripting.console().find("intentional frame failure")
                         != std::string::npos
                      && !app.scripting.error().empty();
    SDL_Log("Lua error clears drawing and input overrides: %s", cleared ? "ok" : "FAILED");
    SDL_Log("Lua lifecycle test: %s", cleared ? "PASS" : "FAIL");
    return cleared ? 0 : 1;
  }
  for(int frame = 0; frame < 2; frame++) {
    setPhysicalInput();
    const bool ran = app.scripting.runBeforeFrame() && app.scripting.runFrame();
    pass = pass && ran;
  }
  pass = pass && app.scripting.globalInteger("frames") == 2;
  SDL_Log("Lua frame callback: %s", pass ? "ok" : "FAILED");
  if(app.scripting.globalInteger("domains_verified") > 0) {
    const bool domains = app.scripting.globalInteger("domains_verified") == 5;
    SDL_Log("Lua WRAM/VRAM/CGRAM/OAM/APU RAM domains: %s", domains ? "ok" : "FAILED");
    pass = pass && domains;
  }
  if(app.scripting.globalInteger("expected_input") > 0) {
    const bool input = app.scripting.globalInteger("b_held") == 1
                    && app.scripting.globalInteger("right_value") == 1
                    && app.scripting.globalInteger("right_index") == 1
                    && app.scripting.globalInteger("a_held") == 0;
    SDL_Log("Lua input inspection: %s", input ? "ok" : "FAILED");
    pass = pass && input;
  }
  const bool injectionTest = app.scripting.globalInteger("expected_injection") > 0;
  if(injectionTest) {
    const bool injected = app.scripting.globalInteger("before_frames") == 2
                       && app.scripting.globalInteger("first_a") == 1
                       && app.scripting.globalInteger("first_b") == 0
                       && app.scripting.globalInteger("first_right") == 0
                       && app.scripting.globalInteger("first_y") == 1
                       && app.scripting.globalInteger("second_a") == 0
                       && app.scripting.globalInteger("second_b") == 1
                       && app.scripting.globalInteger("second_right") == 1;
    SDL_Log("Lua input injection and clearing: %s", injected ? "ok" : "FAILED");
    pass = pass && injected;
  }
  if(app.scripting.globalInteger("expected_console") > 0) {
    const bool console = app.scripting.console()
                      == "start\t42\ttrue\tnil\nready\nframe\t1\nframe\t2\n";
    SDL_Log("Lua captured console output: %s", console ? "ok" : "FAILED");
    pass = pass && console;
  }
  if(controlsTest) {
    const bool controls = app.scripting.globalInteger("before_frames") == 2
                       && app.scripting.globalInteger("memory_changed") == 1
                       && app.scripting.globalInteger("memory_restored") == 1
                       && app.scripting.globalInteger("state_removed") == 1
                       && app.scripting.globalInteger("packaged_state_loaded") == 1
                       && app.scripting.globalInteger("data_state_loaded") == 1
                       && app.scripting.globalInteger("emu_controls") == 1;
    SDL_Log("Lua save-state and emulator controls: %s", controls ? "ok" : "FAILED");
    pass = pass && controls;
  }
  if(app.scripting.globalInteger("expected_commands") > 0) {
    const bool drawing = app.scripting.commandCount()
                      == app.scripting.globalInteger("expected_commands");
    SDL_Log("Lua drawing commands: %s", drawing ? "ok" : "FAILED");
    pass = pass && drawing;
  }
  const bool fileTest = app.scripting.globalInteger("expected_file") > 0;
  const std::string testData = fileTest ? app.scripting.dataDirectory() : std::string();
  if(fileTest) {
    const bool files = readText(testData + "/frames.txt") == "start\n1\n2\n";
    SDL_Log("Lua confined file output: %s", files ? "ok" : "FAILED");
    pass = pass && files;
  }

  const bool loadedAgain = app.scripting.reload();
  setPhysicalInput();
  const bool reloaded = loadedAgain && app.scripting.runBeforeFrame() && app.scripting.runFrame()
                     && app.scripting.globalInteger("frames") == 1;
  SDL_Log("Lua reload: %s", reloaded ? "ok" : "FAILED");
  pass = pass && reloaded;

  if(app.scripting.globalInteger("domains_verified") > 0) {
    const bool originalFastPpu = app.settings.hackPpuFast;
    app.scripting.stop();
    app.core.setOption("Hacks/PPU/Fast", originalFastPpu ? "false" : "true");
    app.core.power();
    const bool alternatePpu = app.scripting.load(script)
                           && app.scripting.globalInteger("domains_verified") == 5;
    SDL_Log("Lua memory domains with alternate PPU: %s", alternatePpu ? "ok" : "FAILED");
    pass = pass && alternatePpu;
    app.scripting.stop();
    app.core.setOption("Hacks/PPU/Fast", originalFastPpu ? "true" : "false");
    app.core.power();
  }

  app.scripting.stop();
  const bool inputRestored = !injectionTest
                          || (app.core.inputValue(0, EmuCore::A) == 0
                           && app.core.inputValue(0, EmuCore::B) == 1
                           && app.core.inputValue(0, EmuCore::Right) == 1);
  const bool stopped = !app.scripting.running() && !app.scripting.runBeforeFrame()
                    && !app.scripting.runFrame() && inputRestored;
  SDL_Log("Lua stop: %s", stopped ? "ok" : "FAILED");
  pass = pass && stopped;

  const bool reported = !app.scripting.load(script + ".missing")
                     && !app.scripting.error().empty();
  SDL_Log("Lua error reporting: %s", reported ? "ok" : "FAILED");
  pass = pass && reported;

  if(fileTest) {
    SDL_RemovePath((testData + "/frames.txt").c_str());
    SDL_RemovePath(testData.c_str());
  }
  if(controlsTest) {
    app.removeState("9");
    SDL_RemovePath(app.stateFolder().c_str());
    SDL_RemovePath(controlsStateDir.c_str());
    SDL_RemovePath(controlsDataDir.c_str());
    SDL_RemovePath(packagedState.c_str());
    app.settings.statesDir = originalStatesDir;
  }

  SDL_Log("Lua lifecycle test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

namespace {
// a frame the chain can actually chew on: flat colour hides sampling mistakes
std::vector<uint32_t> testFrame(int width, int height) {
  std::vector<uint32_t> pixels((size_t)width * height);
  for(int y = 0; y < height; y++) {
    for(int x = 0; x < width; x++) {
      pixels[(size_t)y * width + x] = 0xff000000u | (uint32_t)(x * 255 / width) << 16
                                    | (uint32_t)(y * 255 / height) << 8 | 0x40u;
    }
  }
  return pixels;
}

// reads the chain's own output, so nothing depends on a UI frame being drawn
uint64_t renderHash(Shader& shader, int width, int height) {
  const GLuint texture = shader.render(width, height);
  if(!texture) return 0;

  std::vector<uint32_t> pixels((size_t)shader.outputWidth * shader.outputHeight);
  glBindTexture(GL_TEXTURE_2D, texture);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());

  uint64_t hash = FnvBasis;
  size_t visible = 0;
  size_t litPixels = 0;
  size_t opaquePixels = 0;
  for(uint32_t pixel : pixels) {
    hash = fnv(hash, pixel);
    const bool lit = (pixel & 0x00ffffffu) != 0;
    const bool opaque = (pixel >> 24) >= 0x80;
    if(lit) {
      litPixels++;
    }
    if(opaque) {
      opaquePixels++;
    }
    if(lit && opaque) {
      visible++;
    }
  }
  if(visible == 0) {
    SDL_Log("shader output hidden: %d lit, %d opaque, %d visible of %d pixels",
            (int)litPixels, (int)opaquePixels, (int)visible, (int)pixels.size());
    return 0;
  }
  return hash;
}

// a package that will not compile, for the diagnostics check
std::string writeBrokenPackage() {
  const std::string dir = configDir() + "broken-selftest.shader";
  if(!ensureDir(dir)) return {};
  writeText(dir + "/manifest.bml", "program\n  fragment: broken.fs\n");
  writeText(dir + "/broken.fs",
            "#version 150\nout vec4 fragColor;\nvoid main() { !!! }\n");
  return dir;
}

void removeBrokenPackage(const std::string& dir) {
  SDL_RemovePath((dir + "/manifest.bml").c_str());
  SDL_RemovePath((dir + "/broken.fs").c_str());
  SDL_RemovePath(dir.c_str());
}
}  // namespace

// loads every package in the shaders folder, runs it, and checks it lets go
int runShaderTest(App& app, int warmFrames) {
  Shader& shader = app.shell.shader;
  if(!shader.supported()) {
    SDL_Log("shader test: FAIL, no OpenGL 3.2 on this driver");
    return 1;
  }

  bool pass = true;
  const bool refused = !shader.load(configDir() + "no-such.shader", {})
                    && !shader.active() && !shader.failure.empty();
  SDL_Log("missing package refused: %s", refused ? "ok" : "FAILED");
  pass = pass && refused;

  std::vector<uint32_t> frame;
  int frameWidth = 256;
  int frameHeight = 224;
  if(app.core.loaded()) {
    for(int i = 0; i < warmFrames; i++) {
      app.core.runFrame();
    }
    frameWidth = app.shell.frameWidth;
    frameHeight = app.shell.frameHeight;
    const size_t pixels = (size_t)frameWidth * frameHeight;
    if(app.shell.lastPixels && pixels > 0) {
      frame.assign(app.shell.lastPixels, app.shell.lastPixels + pixels);
    }
  }
  if(frame.empty()) {
    frame = testFrame(frameWidth, frameHeight);
  }
  const std::string dir = app.shadersDir();
  const std::vector<std::string> folders = shaderList(dir);
  SDL_Log("%d packages in %s", (int)folders.size(), dir.c_str());
  if(folders.empty()) {
    SDL_Log("shader test: FAIL, no packages to exercise");
    return 1;
  }

  for(const std::string& folder : folders) {
    const std::string path = normalPath(dir + "/" + folder);
    if(!shader.load(path, {})) {
      SDL_Log("%-24s FAILED: %s", folder.c_str(), shader.failure.c_str());
      if(!shader.log.empty()) SDL_Log("%s", shader.log.c_str());
      pass = false;
      continue;
    }
    shader.pushFrame(frame.data(), frameWidth, frameHeight);
    const uint64_t once = renderHash(shader, 640, 480);
    // a bare redraw must reuse the last chain run; a resize must not
    const uint64_t redraw = renderHash(shader, 640, 480);
    const bool cachedRedraw = shader.renderCount == 1 && redraw == once;
    const bool resized = renderHash(shader, 512, 448) != 0 && shader.renderCount == 2;
    const GLenum error = glGetError();

    // a full ring of the same picture has to match one seeded frame
    if(!shader.load(path, {})) { pass = false; continue; }
    for(int i = 0; i < 9; i++) {
      shader.pushFrame(frame.data(), frameWidth, frameHeight);
    }
    const bool coherent = renderHash(shader, 640, 480) == once;

    if(!once || !cachedRedraw || !resized || !coherent || error != GL_NO_ERROR) {
      SDL_Log("%-24s FAILED:%s%s%s%s%s", folder.c_str(), once ? "" : " blank frame",
              cachedRedraw ? "" : " redraw reran the chain", resized ? "" : " resize not redrawn",
              coherent ? "" : " history not seeded", error == GL_NO_ERROR ? "" : " gl error");
      pass = false;
      continue;
    }
    SDL_Log("%-24s ok, %d passes, %d x %d", folder.c_str(), (int)shader.passCount(),
            shader.outputWidth, shader.outputHeight);
  }

  // a compile failure has to leave the driver's message behind for the UI
  if(const std::string broken = writeBrokenPackage(); !broken.empty()) {
    const bool reported = !shader.load(broken, {}) && !shader.active()
                       && !shader.failure.empty() && !shader.log.empty();
    SDL_Log("compiler diagnostics kept: %s", reported ? "ok" : "FAILED");
    pass = pass && reported;
    removeBrokenPackage(broken);
  }

  // a package with parameters has to honour an override
  std::string withParams;
  for(const std::string& folder : folders) {
    if(!shader.load(normalPath(dir + "/" + folder), {})) continue;
    if(!shader.params.empty()) { withParams = folder; break; }
  }
  if(!withParams.empty()) {
    const std::string name = shader.params[0].name;
    const std::vector<ShaderParam> overrides = {{name, "0.25", {}}};
    const bool applied = shader.load(normalPath(dir + "/" + withParams), overrides)
                      && shader.params[0].value == "0.25"
                      && shader.params[0].stock != "0.25";
    SDL_Log("parameter override in %s: %s", withParams.c_str(), applied ? "ok" : "FAILED");
    pass = pass && applied;
  } else {
    SDL_Log("no package exposes parameters, override untested");
  }

  // the selection and its overrides have to survive settings.cfg
  Settings written;
  written.videoShader = dir + "/" + folders[0];
  written.shaderParams = {{"gamma", "2.5"}};
  const std::string temp = configDir() + "shader-selftest.cfg";
  written.save(temp);
  Settings reread;
  reread.load(temp);
  SDL_RemovePath(temp.c_str());
  const bool roundTrip = reread.videoShader == normalPath(written.videoShader)
                      && reread.shaderParams.size() == 1
                      && reread.shaderParams[0].name == "gamma"
                      && reread.shaderParams[0].value == "2.5";
  SDL_Log("settings round-trip: %s", roundTrip ? "ok" : "FAILED");
  pass = pass && roundTrip;

  shader.unload();
  const bool released = !shader.active() && shader.params.empty();
  SDL_Log("unload: %s", released ? "ok" : "FAILED");
  pass = pass && released;

  SDL_Log("shader test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int runMovieTest(App& app, int warmFrames) {
  const std::string path = configDir() + "movie-selftest.bsv";
  const std::string beginningPath = configDir() + "movie-beginning-selftest.bsv";
  const int configuredEntropy = app.settings.hackEntropy;
  app.settings.hackEntropy = EntropyHigh;
  const std::vector<EmuCore::StateComponent> map = app.core.stateMap(false);
  auto guestHash = [&](const std::vector<uint8_t>& state) {
    uint64_t hash = FnvBasis;
    for(const auto& part : map) {
      if(part.hostState) {
        continue;
      }
      for(int i = part.offset; i < part.offset + part.size && i < (int)state.size(); i++) {
        hash = fnv(hash, state[i]);
      }
    }
    return hash;
  };
  auto setInput = [&](int frame) {
    for(int button = 0; button < EmuCore::ButtonCount; button++) {
      app.core.setInput(0, button, ((frame / 8 + button * 3) % 11) == 0);
    }
  };
  app.core.setOption("Hacks/Entropy", "None");
  app.core.power();
  for(int frame = 0; frame < warmFrames; frame++) {
    app.core.runFrame();
  }

  app.beginMovieRecording(false);
  const int frames = 120;
  for(int frame = 0; frame < frames; frame++) {
    setInput(frame);
    app.core.runFrame();
  }
  const uint64_t recorded = frameHash(app);
  const uint64_t recordedState = guestHash(app.core.serialize(false));
  const size_t inputs = app.movieInput.size();
  app.movieMode = MovieMode::Inactive;
  const bool wrote = inputs > 0 && app.writeMovieFile(path);
  app.clearMovie();

  for(int button = 0; button < EmuCore::ButtonCount; button++) {
    app.core.setInput(0, button, 0);
  }
  const bool opened = wrote && app.playMovieFile(path);
  for(int frame = 0; opened && frame < frames; frame++) {
    app.core.runFrame();
  }
  const bool replayed = opened && frameHash(app) == recorded
                      && guestHash(app.core.serialize(false)) == recordedState
                      && app.movieMode == MovieMode::Inactive;

  app.beginMovieRecording(true);
  const int beginningFrames = 240;
  for(int frame = 0; frame < beginningFrames; frame++) {
    setInput(frame);
    app.core.runFrame();
  }
  const uint64_t beginningFrame = frameHash(app);
  const uint64_t beginningState = guestHash(app.core.serialize(false));
  const size_t beginningInputs = app.movieInput.size();
  app.movieMode = MovieMode::Inactive;
  const bool beginningWrote = beginningInputs > 0 && app.writeMovieFile(beginningPath);
  const std::vector<uint8_t> beginningFile = readBytes(beginningPath);
  const bool stateLess = beginningFile.size() >= 8
                      && beginningFile[4] == 0
                      && beginningFile[5] == 0
                      && beginningFile[6] == 0
                      && beginningFile[7] == 0;
  app.clearMovie();
  const bool beginningOpened = beginningWrote && stateLess && app.playMovieFile(beginningPath);
  for(int frame = 0; beginningOpened && frame < beginningFrames; frame++) {
    app.core.runFrame();
  }
  const bool beginningReplayed = beginningOpened && frameHash(app) == beginningFrame
                              && guestHash(app.core.serialize(false)) == beginningState
                              && app.movieMode == MovieMode::Inactive;

  app.core.power();
  const uint64_t firstRandomPower = guestHash(app.core.serialize(false));
  app.core.power();
  const bool entropyRestored = guestHash(app.core.serialize(false)) != firstRandomPower;

  std::vector<uint8_t> damaged = readBytes(path);
  if(!damaged.empty()) {
    damaged[0] = 'X';
  }
  const std::string broken = configDir() + "movie-broken-selftest.bsv";
  const bool damagedWritten = !damaged.empty() && writeBytes(broken, damaged.data(), damaged.size());
  app.clearMovie();
  const bool refused = damagedWritten && !app.playMovieFile(broken);
  const std::string empty = configDir() + "movie-empty-selftest.bsv";
  const uint8_t emptyMovie[8] = {'B', 'S', 'V', '1', 0, 0, 0, 0};
  const uint64_t beforeEmpty = guestHash(app.core.serialize(false));
  const bool emptyWritten = writeBytes(empty, emptyMovie, sizeof(emptyMovie));
  const bool emptyRefused = emptyWritten && !app.playMovieFile(empty)
                         && guestHash(app.core.serialize(false)) == beforeEmpty;
  SDL_RemovePath(path.c_str());
  SDL_RemovePath(beginningPath.c_str());
  SDL_RemovePath(broken.c_str());
  SDL_RemovePath(empty.c_str());
  app.clearMovie();
  app.settings.hackEntropy = configuredEntropy;
  app.core.setOption("Hacks/Entropy", EntropyNames[configuredEntropy]);

  const bool pass = wrote && replayed && beginningWrote
                 && beginningReplayed && entropyRestored && refused && emptyRefused;
  SDL_Log("movie round trip: %s, %d inputs", replayed ? "ok" : "FAILED", (int)inputs);
  SDL_Log("movie from reset: %s, %d inputs", beginningReplayed ? "ok" : "FAILED",
          (int)beginningInputs);
  SDL_Log("configured entropy restored: %s", entropyRestored ? "ok" : "FAILED");
  SDL_Log("invalid movies refused without mutation: %s",
          refused && emptyRefused ? "ok" : "FAILED");
  SDL_Log("movie test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

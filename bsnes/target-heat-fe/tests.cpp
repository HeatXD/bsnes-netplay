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

  const bool pass = blob && filed && reversible && refused;
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

  SDL_Log("pads connected during this test: %d", (int)app.pads.size());
  SDL_Log("hotkey logic test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

int runLuaTest(App& app, const std::string& script) {
  app.core.setInput(0, EmuCore::B, 1);
  app.core.setInput(0, EmuCore::Right, 1);
  bool pass = app.scripting.load(script);
  pass = pass && app.scripting.running();
  if(pass && app.scripting.globalInteger("expected_frame_error") > 0) {
    const bool cleared = !app.scripting.runFrame() && !app.scripting.running()
                      && app.scripting.commandCount() == 0
                      && !app.scripting.error().empty();
    SDL_Log("Lua error clears drawing commands: %s", cleared ? "ok" : "FAILED");
    SDL_Log("Lua lifecycle test: %s", cleared ? "PASS" : "FAIL");
    return cleared ? 0 : 1;
  }
  pass = pass && app.scripting.runFrame() && app.scripting.runFrame();
  pass = pass && app.scripting.globalInteger("frames") == 2;
  SDL_Log("Lua frame callback: %s", pass ? "ok" : "FAILED");
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

  const bool reloaded = app.scripting.reload() && app.scripting.runFrame()
                     && app.scripting.globalInteger("frames") == 1;
  SDL_Log("Lua reload: %s", reloaded ? "ok" : "FAILED");
  pass = pass && reloaded;

  app.scripting.stop();
  const bool stopped = !app.scripting.running() && !app.scripting.runFrame();
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

  SDL_Log("Lua lifecycle test: %s", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

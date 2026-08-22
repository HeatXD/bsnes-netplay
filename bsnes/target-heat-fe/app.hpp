#pragma once

#include "emucore/emucore.hpp"
#include "inputmap.hpp"
#include "settings.hpp"
#include "shell.hpp"
#include "scripting.hpp"
#include "util.hpp"

#include "imgui.h"

#include <gekkonet.h>
#include <weyvelength.h>

#include <deque>
#include <string>
#include <utility>
#include <vector>


inline ImVec4 unpackColor(int rgb) {
  return ImVec4(((rgb >> 16) & 0xff) / 255.0f, ((rgb >> 8) & 0xff) / 255.0f,
                (rgb & 0xff) / 255.0f, 1.0f);
}

inline int packColor(const float rgb[3]) {
  return (int)(rgb[0] * 255.0f + 0.5f) << 16
       | (int)(rgb[1] * 255.0f + 0.5f) << 8
       | (int)(rgb[2] * 255.0f + 0.5f);
}

struct App;

struct StateEntry {
  std::string name;
  std::string label;
  int64_t time = 0;
};

struct CheatEntry {
  std::string name;
  std::string code;
  bool enabled = false;
};

struct CheatCandidate {
  uint32_t address = 0;
  uint32_t value = 0;
  int size = 0;
};

enum class MovieMode { Inactive, Playing, Recording };

// SNES buttons, in EmuCore::Button order; one player's worth per network packet
constexpr int NetplayButtonCount = EmuCore::ButtonCount;

struct NetplayPeer {
  int id = 0;
  GekkoPlayerType type = GekkoLocalPlayer;
  bool connected = false;
  GekkoNetworkStats stats{};
  std::string addr;       // Direct transport: "ip:port"
  uint32_t weyveId = 0;   // Weyvelength transport: member id
};

struct NetplayStateSnapshot {
  bool valid = false;
  int frame = 0;
  uint32_t checksum = 0;
  std::vector<uint8_t> data;
};

struct Netplay {
  enum Mode { Inactive, Running } mode = Inactive;
  enum Transport { Direct, Weyvelength } transport = Direct;

  std::vector<NetplayPeer> peers;
  std::vector<uint16_t> inputs;  // this tick's confirmed buttons, one mask per port
  // Recent saves used for desync dumps.
  std::deque<NetplayStateSnapshot> stateCache;
  static constexpr int StateCacheFrames = 32;

  GekkoConfig config{};
  GekkoSession* session = nullptr;
  std::string instance;  // "p<port>" or "w<room>", for log lines and dump filenames
  int localPlayer = 0;   // this peer's player index, 0..numPlayers-1
  int localActorId = 0;  // the GekkoNet actor handle for that player
  bool detectDesyncs = false;
  // Offline runahead is restored when the session ends.
  int savedRunAheadFrames = 0;
  bool rollback = false;
  bool spectatorPaused = false;
  bool recordInput = false;
  int localDelay = 0;
  int localRunAhead = 0;
  double speedScale = 1.0;
  uint32_t desyncCount = 0;
  std::deque<std::string> log;
  static constexpr int LogLimit = 200;
};

struct WeyveKnownName {
  uint32_t id = 0;
  std::string name;
};

enum class NetplayEntryRole { Player, Spectator };

struct NetplayRemoteEntry {
  NetplayEntryRole role = NetplayEntryRole::Player;
  int playerNumber = 1;  // Player role only; 1-based, never equal to App::netplayLocalPlayer
  char ip[64] = "127.0.0.1";
  char port[8] = "7000";
};

// one row of a room-browser result; copied out of the client's borrowed
// storage immediately on WEYVE_EVENT_ROOM_LIST, since that storage lasts only
// until the next weyve_poll
struct WeyveRoomListing {
  std::string id;
  uint32_t members = 0;
  bool passworded = false;
  std::string game;  // "listing" key "game", empty if the host hasn't set one
  std::string host;  // "listing" key "host"; empty on a legacy or unset host
};

struct Weyve {
  WeyveClient* client = nullptr;
  std::string lastError;
  uint64_t idleSince = 0;
  static constexpr uint64_t IdleTimeoutMs = 5 * 60 * 1000;

  bool pendingListed = false;
  bool pendingCreate = false;
  bool connectAttempted = false;
  bool focusTab = false;
  bool openHostSettings = false;
  bool temporarySessionPassword = false;
  bool rolesDirty = true;  // host reassigns roles on the next poll
  uint32_t lastStartToken = 0;
  uint32_t lastStopToken = 0;
  uint32_t selectedMember = 0;
  int localRollback = 8;
  int localDelay = 2;
  int rollbackBaseline = 8;
  int delayBaseline = 2;
  int spectatorDelay = 0;
  std::string lastGameHash;

  std::vector<WeyveKnownName> knownNames;  // remembered after a member leaves

  static constexpr int LogLimit = 200;
  std::deque<std::string> log;
  char chatInput[256] = {};

  std::vector<WeyveRoomListing> roomList;  // last weyve_list_rooms reply

  static constexpr int PlayerCap = 5;
};

// shared by the Paths tab and the pick draining, so the two cannot drift
struct BiosSlot {
  const char* label;
  const char* id;
  FilePick App::* pick;
  std::string Settings::* path;
};
extern const BiosSlot BiosSlots[3];

struct App {
  App();
  ~App();

  Shell shell;
  Settings settings;
  InputMap input;
  EmuCore core;
  LuaEngine scripting;

  std::string inputCfg, settingsCfg;
  std::vector<Controller> pads;
  std::vector<std::pair<std::string, std::string>> games;  // label, path
  std::string status;
  uint64_t messageTime = 0;
  std::string gameTitle;
  std::string gameLocation;

  FilePick romPick, dirPick, shotDirPick, savesDirPick, fontPick, pakPick, firmwareDirPick;
  FilePick scriptPick, movieOpenPick, movieSavePick;
  FilePick patchesDirPick, databaseDirPick, statesDirPick, cheatsDirPick, shadersDirPick;
  FilePick sgbBiosPick, bsxBiosPick, stBiosPick;
  FilePick sufamiAPick, sufamiBPick;
  std::string sufamiPending;

  bool running = true;
  bool fontDirty = false;
  bool paused = false;
  bool fastForward = false;
  bool rewinding = false;
  bool frameAdvance = false;  // one-shot: advance a frame, then stay paused
  bool showSettings = false;
  bool showTools = false;
  bool showGames = false;
  bool showAbout = false;
  bool showManifest = false;
  bool showScripting = false;
  bool showStateManager = false;
  bool showCheats = false;
  bool showCheatFinder = false;
  int settingsTab = 0;
  int mapPort = 0;
  int mapPlayer = 0;  // which of a multitap's controllers is being mapped
  int capturing = -1;      // emulator button slot being rebound
  int capturingHotkey = -1;  // hotkey index * HotkeySlots + slot
  int gameSelected = 0;
  int stateSlot = 1;  // 1..StateSlots; a session choice, not a setting
  bool stateManagerManaged = true;
  std::string stateManagerSelection;
  char stateManagerName[64] = {};
  bool confirmRemoveState = false;
  std::vector<CheatEntry> cheats;
  bool cheatsDirty = false;
  int cheatSelected = -1;
  char cheatName[96] = {};
  char cheatCode[512] = {};
  bool cheatEditEnabled = false;
  bool confirmRemoveCheat = false;
  std::string cheatError;
  std::vector<CheatEntry> databaseCheats;
  std::vector<bool> databaseCheatSelected;
  std::vector<CheatCandidate> cheatCandidates;
  char cheatSearchValue[32] = {};
  int cheatSearchSize = 0;
  int cheatSearchMode = 0;
  int cheatCandidateSelected = -1;
  // undo and redo never outlive the session, so they are held rather than written
  std::vector<uint8_t> undoState, redoState;
  bool confirmRemoveStates = false;
  // not persisted: every game starts at normal speed
  int speedIndex = SpeedNormal;
  double fps = 0.0;
  long long totalSamples = 0;
  // sampled once a frame, or the relative mouse delta gets split in two
  InputSample sample;
  bool mouseCaptured = false;
  bool hotkeyWasHeld[HotkeyCount] = {};
  uint64_t autoSaveMark = 0;
  // a load that still has to be confirmed; the game is in the core but held
  bool unverifiedPrompt = false;
  long long emulatedFrames = 0;  // turbo's clock; must not depend on wall time
  std::deque<std::vector<uint8_t>> rewindHistory;
  int rewindCounter = 0;
  MovieMode movieMode = MovieMode::Inactive;
  std::vector<uint8_t> movieState;
  std::vector<int16_t> movieInput;
  size_t moviePosition = 0;
  bool movieSavePending = false;
  int movieDevices[EmuCore::PortCount] = {EmuCore::Gamepad, EmuCore::Gamepad};
  int moviePreviousDevices[EmuCore::PortCount] = {};
  bool movieDevicesChanged = false;
  bool moviePlaybackFinished = false;
  bool movieDeterministic = false;
  Netplay netplay;
  Weyve weyve;
  bool showNetplay = false;
  bool pendingNetplayUnload = false;
  int netplayTab = 0;  // Netplay window: 0 direct, 1 weyvelength
  char netplayPortInput[8] = "7000";
  int netplayLocalPlayer = 1;  // 1-based; which slot *this* peer occupies, picked by the user
  bool netplayDirectSpectator = false;
  int netplaySpectatorPlayers = 2;
  std::vector<NetplayRemoteEntry> netplayRemotes;
  char weyveHostInput[128] = {};
  char weyvePortInput[8] = "5555";
  char weyveJoinCode[32] = {};
  char weyveJoinPassword[64] = {};
  char weyveRoomPassword[64] = {};
  char weyveNicknameInput[32] = {};

  void scanGames();
  // a path, or a Sufami Turbo pair joined by '|', as the recent list stores it
  bool loadRom(const std::string& entry);
  void unloadRom();
  void applySpeed() {
    double scale = SpeedScales[speedIndex];
    // unlimited has no target rate to resample towards
    if(fastForward && !settings.fastForwardUnlimited) scale /= settings.fastForwardSpeed;
    core.setSpeedScale(scale);
    core.setFrameSkip(fastForward ? settings.fastForwardFrameSkip : 0);
  }
  // unlimited fast forward runs with no audio clock driving the loop
  bool unpaced() const { return fastForward && settings.fastForwardUnlimited; }
  bool muted() const {
    return settings.mute || (settings.muteUnfocused && !focused())
        || (fastForward && settings.fastForwardMute)
        || (rewinding && settings.rewindMute)
        || netplay.rollback;
  }
  // a limited fast forward plays at 65%: decimated audio is harsh
  float audioGain() const {
    if(muted()) return 0.0f;
    const bool limited = fastForward && !settings.fastForwardUnlimited;
    return settings.volume / 100.0f * (limited || rewinding ? 0.65f : 1.0f);
  }
  void setSpeed(int index) {
    if(netplayActive()) return;  // GekkoNet's timesync owns the speed scale
    speedIndex = SDL_clamp(index, 0, SpeedCount - 1);
    applySpeed();
    showMessage(std::string("speed ") + SpeedNames[speedIndex]);
  }
  void toggleFastForward() {
    if(netplayActive()) return;
    fastForward = !fastForward;
    applySpeed();
  }
  // skew trims a card that runs fast or slow until the backlog stops drifting
  void applyAudioTuning() {
    core.setAudioFrequency(AudioRate + settings.audioSkew);
    core.setAudioBalance(SDL_clamp((settings.audioBalance - 50) / 50.0, -1.0, 1.0));
  }
  void reset() {
    if(movieActive() || netplayActive()) {
      showMessage("reset is not available while a movie or netplay session is active");
      return;
    }
    core.reset();
    resetTimeline();
    paused = false;
  }
  void powerCycle() {
    if(movieActive() || netplayActive()) {
      showMessage("power cycle is not available while a movie or netplay session is active");
      return;
    }
    core.power();
    resetTimeline();
    paused = false;
  }
  void advanceOneFrame() { frameAdvance = true; }

  // any window we own, not just the main one: an imgui viewport panel takes
  // focus away from it while staying part of this app
  bool focused() const { return SDL_GetKeyboardFocus() != nullptr; }
  bool fullscreen() const { return shell.fullscreen(); }
  void toggleFullscreen() {
    const bool enter = !fullscreen();
    // fullscreen claims whichever display the window sits on, so move it first
    if(enter) shell.moveToDisplay(settings);
    SDL_SetWindowFullscreen(shell.window, enter);
  }
  void toggleMouseCapture();
  // hover help, suppressed by the tool tips setting
  void tip(const char* text) const {
    if(settings.showToolTips) ImGui::SetItemTooltip("%s", text);
  }
  void openPick(FilePick& pick, const SDL_DialogFileFilter* filters, const char* dir);
  const char* gamesDirOrNull() const;
  const Controller* portPad(int port, int player) const {
    return resolvePad(pads, settings.padIndex[padSlot(port, player)]);
  }
  // unset means a Shaders folder beside the exe, then one beside the config
  std::string shadersDir() const;
  // rebuilds the GLSL chain from the selected package and its overrides
  void applyShader();
  void applyVideoFilter();
  // keeps only the parameters the user moved away from the manifest's values
  void saveShaderParams();
  // unset means a Firmware folder beside the config
  std::string firmwareDir() const {
    return settings.firmwareDir.empty() ? configDir() + "Firmware" : settings.firmwareDir;
  }
  // resolving this stats the disk, so readers take the cache below
  std::string databaseDir() const;
  std::string databaseDirCache;
  void refreshDatabaseDir() {
    databaseDirCache = databaseDir();
    core.setDatabaseDirectory(databaseDirCache);
  }
  // each folder row re-pushes its path to the core after a change
  void pushSavesDir() { core.setSavesDirectory(settings.savesDir); }
  void pushPatchesDir() { core.setPatchesDirectory(settings.patchesDir); }
  void pushSerialization() {
    core.setOption("System/Serialization/Method", SerializationNames[settings.serialization]);
  }
  // the Paths row shows this resolved, the way databaseDirCache is shown
  std::string databaseDirShown() const { return databaseDirCache; }
  // the folder a picker for this medium should reopen in
  void rememberDir(const std::string& path);
  void openRomDialog();
  void openFolderDialog(FilePick& pick) { openPick(pick, nullptr, gamesDirOrNull()); }
  void openFontDialog();
  void openScriptDialog();
  void openMediaDialog(FilePick& pick, const char* label, const char* extensions,
                       EmuCore::Medium medium = EmuCore::Medium::SuperFamicom);
  const char* startDirFor(EmuCore::Medium medium);
  void openSufamiPairDialog();
  void takeScreenshot();
  std::string pendingScreenshot;
  void finishScreenshot(bool saved);
  void saveMemoryTick();

  void resetTimeline();
  void setRewinding(bool enabled);
  void captureRewind();
  void stepRewind();
  void advanceEmulation();

  //movies.cpp
  void openMovieDialog();
  void beginMovieRecording(bool fromBeginning);
  bool playMovieFile(const std::string& path);
  bool writeMovieFile(std::string path);
  void stopMovie();
  void clearMovie();
  void restoreMovieDevices();
  int16_t pollMovieInput(int port, int device, int input, int16_t physical);
  bool movieActive() const {
    return movieMode != MovieMode::Inactive || movieSavePending;
  }

  //netplay.cpp
  bool netplayActive() const { return netplay.mode == Netplay::Running; }
  void netplayApplyDeterministicSettings();
  void netplayBeginSession(int numPlayers, bool detectDesyncs, int maxSpectators = 0,
                           bool localSpectating = false, int spectatorDelay = -1,
                           int rollbackFrames = -1, int localDelay = -1);
  void netplayStart(int port, int local, const std::vector<std::string>& remotes,
                    const std::vector<std::string>& spectators = {}, int spectatorPlayers = 2);
  void netplayStop();
  void netplayRun();
  void netplaySetLocalDelay(int frames);
  void netplaySetRunAhead(int frames);
  void netplayPollLocalInput();
  int16_t netplayGetInput(int port, int device, int input);
  void netplayTimesync();
  void netplayLog(std::string line);
  uint32_t netplayChecksum(const std::vector<uint8_t>& state);
  void netplayCacheState(int frame, uint32_t checksum, const std::vector<uint8_t>& state);
  void netplayDumpState(int frame, const char* tag, uint32_t checksum, const std::vector<uint8_t>& data);

  //netplay-weyve.cpp
  bool weyveConnect(const std::string& host, uint16_t port);
  void weyveDisconnect();
  bool weyveConnected() const { return weyve.client != nullptr; }
  bool weyveSessionActive() const {
    return netplayActive() && netplay.transport == Netplay::Weyvelength;
  }
  bool weyveInRoom() const;
  void weyveCreateRoom(bool listed);
  void weyveJoinRoom(const std::string& id, const std::string& password);
  void weyveLeaveRoom();
  void weyveResetRoomState();
  void weyvePoll();
  void netplayStartWeyve();
  void weyveLog(std::string line);
  void weyveRememberName(uint32_t memberId, const std::string& name);
  std::string weyveNameOf(uint32_t memberId) const;
  std::string weyveRoomData(const std::string& key) const;
  std::string weyveMemberData(uint32_t memberId, const std::string& key) const;
  std::string weyveRoleOf(uint32_t memberId) const;
  std::string weyveRoleLabel(const std::string& role) const;
  std::vector<uint32_t> weyvePlayerOrder() const;
  void weyveAutoAssignRoles();
  void weyveSetRole(uint32_t memberId, const std::string& role);
  void weyveSetBaseline(int rollback, int delay);
  void weyveSetLocalRollback(int frames);
  void weyveSetLocalDelay(int frames);
  void weyveKick(uint32_t memberId);
  void weyveTransferHost(uint32_t memberId);
  void weyveStartGame();
  void weyveStopGame();
  std::string weyveStartBlockedReason() const;
  // searches the existing Games-tab library (settings.gamesDir) for a plain
  // ROM file matching this content hash; empty when not found locally
  std::string weyveHasGame(const std::string& hash) const;
  void weyveRescanGames();
  std::string weyveGameFingerprint() const;
  void weyveSelectGame(uint32_t gamesIndex);  // indexes App::games, not a netplay-only list
  void weyveCopyRoomCode() const;
  void weyvePublishHostListing();  // republishes our name under the "host" listing key
  void weyveListRooms();
  void weyveClearRoomList();

  //states.cpp
  // resolved states folder, and the per-game folder holding the slots
  std::string statesDir() const;
  std::string stateFolder() const;
  std::string statePath(const std::string& name) const;
  // slots are named by number; undo, redo and auto are named outright
  static std::string slotName(int slot);
  static std::string stateLabel(const std::string& name);
  int64_t stateTime(const std::string& name) const;
  bool hasState(const std::string& name) const;
  bool saveState(const std::string& name, bool quiet = false);
  bool saveStateFile(const std::string& path, bool quiet = false);
  // null unless the slot is one of the memory-resident ones
  std::vector<uint8_t>* memorySlot(const std::string& name);
  const std::vector<uint8_t>* memorySlot(const std::string& name) const;
  bool loadState(const std::string& name);
  bool loadStateFile(const std::string& path);
  bool removeState(const std::string& name);
  bool renameState(const std::string& from, const std::string& to);
  std::vector<StateEntry> availableStates(bool managed) const;
  void removeAllStates();
  void setStateSlot(int slot);
  void pollHotkeys();
  void triggerHotkey(int index);

  ImVec4 accentColor() const { return unpackColor(settings.accent); }
  void applyPreset();
  void applyAccent();
  void applyTheme();
  void applyFont();

  void showMessage(const std::string& text) {
    status = text;
    messageTime = SDL_GetTicks();
    SDL_Log("%s", text.c_str());
  }

  std::string hotkeyShortcut(Hotkey key) const;
  void setWindowScale(int scale);
  void drawFileMenu();
  void drawWindowSizeMenu();
  void drawOutputMenu();
  void drawFilterMenu();
  void drawSpeedMenu();
  void drawStateMenu(bool loading);
  void drawRemoveStatesPrompt();
  void drawEmulationMenu();
  void drawMovieMenu();
  void drawShaderMenu();
  void drawSettingsMenu();
  void drawMenuBar();
  void drawStatusBar();
  void drawSettingsWindow();
  void drawGamepadDiagnostics();
  void drawToolsWindow();
  void drawScriptingWindow();
  void drawStateManagerWindow();
  void drawCheatsWindow();
  void drawCheatFinderWindow();
  void restoreVideoDefaults();
  void drawShaderSection(bool& dirty);
  void drawVideoTab();
  void restoreAudioDefaults();
  void drawAudioTab();
  void drawDevicePicker();
  void drawControllerPicker();
  std::string playerLabel(int device, int player) const;
  void drawBindingRow(int device, int input, bool turbo);
  void drawBindingTable(int device);
  void restoreInputDefaults();
  void drawInputTab();
  void restoreHotkeyDefaults();
  void drawHotkeysTab();
  void restoreEmulatorDefaults();
  void drawEmulatorTab();
  // every Hacks/* option in one push; the core caches them, so both tabs and
  // startup have to say the whole set
  void pushEnhancements();
  void restoreEnhancementDefaults();
  void drawEnhancementsTab();
  void restoreCompatibilityDefaults();
  void drawCompatibilityTab();
  void restoreNetplayDefaults();
  void drawNetplayTab();
  // cartridge summary plus every loaded medium's manifest
  void drawManifestWindow();
  void drawUnverifiedPrompt();
  // one definition of "not emulating", for the frame loop and the dimming alike
  bool emulationIdle() const {
    return !core.loaded() || (!netplayActive() && paused && !frameAdvance) || unverifiedPrompt
        || (!netplayActive() && settings.defocusPolicy == DefocusPause && !focused());
  }
  bool drawColourSection();
  bool drawFontSection();
  void restoreAppearanceDefaults();
  void drawCustomizationTab();
  void restorePathDefaults();
  void drawPathsTab();
  void drawGamesList();
  void drawGamesWindow();
  void drawGamesHome();
  void drawAboutWindow();
  void drawNetplayWindow();

  std::string cheatPath() const;
  void loadCheats();
  void saveCheats();
  void applyCheats();
  bool normalizeCheatCode(std::string& code) const;
  std::vector<CheatEntry> findDatabaseCheats() const;
  void drawUi();
};

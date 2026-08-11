struct Program : Lock, Emulator::Platform {
  Application::Namespace tr{"Program"};

  //program.cpp
  auto create() -> void;
  auto main() -> void;
  auto quit() -> void;
  bool pendingUnload = false;  // unloading mid-frame frees ROM the emulator thread reads

  //platform.cpp
  auto open(uint id, string name, vfs::file::mode mode, bool required) -> shared_pointer<vfs::file> override;
  auto load(uint id, string name, string type, vector<string> options = {}) -> Emulator::Platform::Load override;
  auto videoFrame(const uint16* data, uint pitch, uint width, uint height, uint scale) -> void override;
  auto audioFrame(const double* samples, uint channels) -> void override;
  auto inputPoll(uint port, uint device, uint input) -> int16 override;
  auto inputRumble(uint port, uint device, uint input, bool enable) -> void override;

  //game.cpp
  auto load() -> void;
  auto loadFile(string location) -> vector<uint8_t>;
  auto loadSuperFamicom(string location) -> bool;
  auto loadGameBoy(string location) -> bool;
  auto loadBSMemory(string location) -> bool;
  auto loadSufamiTurboA(string location) -> bool;
  auto loadSufamiTurboB(string location) -> bool;
  auto save() -> void;
  auto reset() -> void;
  auto power() -> void;
  auto unload() -> void;
  auto verified() const -> bool;

  //game-pak.cpp
  auto openPakSuperFamicom(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openPakGameBoy(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openPakBSMemory(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openPakSufamiTurboA(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openPakSufamiTurboB(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;

  //game-rom.cpp
  auto openRomSuperFamicom(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openRomGameBoy(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openRomBSMemory(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openRomSufamiTurboA(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;
  auto openRomSufamiTurboB(string name, vfs::file::mode mode) -> shared_pointer<vfs::file>;

  //paths.cpp
  auto path(string type, string location, string extension = "") -> string;
  auto gamePath() -> string;
  auto cheatPath() -> string;
  auto statePath() -> string;
  auto screenshotPath() -> string;

  //states.cpp
  struct State {
    string name;
    uint64_t date;
    static const uint Signature;
  };
  auto availableStates(string type) -> vector<State>;
  auto hasState(string filename) -> bool;
  auto loadStateData(string filename) -> vector<uint8_t>;
  auto loadState(string filename) -> bool;
  auto saveState(string filename) -> bool;
  auto saveUndoState() -> bool;
  auto saveRedoState() -> bool;
  auto removeState(string filename) -> bool;
  auto renameState(string from, string to) -> bool;

  //movies.cpp
  struct Movie {
    enum Mode : uint { Inactive, Playing, Recording } mode = Mode::Inactive;
    serializer state;
    vector<int16> input;
  } movie;
  auto movieMode(Movie::Mode) -> void;
  auto moviePlay() -> void;
  auto movieRecord(bool fromBeginning) -> void;
  auto movieStop() -> void;

  //rewind.cpp
  struct Rewind {
    enum Mode : uint { Playing, Rewinding } mode = Mode::Playing;
    vector<serializer> history;
    uint length = 0;
    uint frequency = 0;
    uint counter = 0;  //in frames
  } rewind;
  auto rewindMode(Rewind::Mode) -> void;
  auto rewindReset() -> void;
  auto rewindRun() -> void;

  // netplay.cpp
  struct Netplay {
    enum Mode : uint { Inactive, Running, Stress } mode = Mode::Inactive;
    // picks the GekkoNet adapter and which Peer address field is live
    enum Transport : uint { Direct, Weyvelength } transport = Transport::Direct;
    enum Device : uint { None = 0, Gamepad = 1, Multitap = 3};
    // netplay peer
    struct Peer {
      uint8 id = 0;
      GekkoPlayerType type = GekkoLocalPlayer;
      struct connection {
        string addr;
      } conn;
      uint32 weyveId = 0;  // Weyvelength path: address is this id, not conn.addr
    };
    struct ChecksumRange {
      uint offset = 0;
      uint size = 0;
    };
    struct StateSnapshot {
      bool valid = false;
      int frame = 0;
      uint32 checksum = 0;
      vector<uint8> data;
    };
    struct Buttons {
      union u {
        struct btn {
          uint16_t b      : 1;
          uint16_t y      : 1;
          uint16_t select : 1;
          uint16_t start  : 1;
          uint16_t up     : 1;
          uint16_t down   : 1;
          uint16_t left   : 1;
          uint16_t right  : 1;
          uint16_t a      : 1;
          uint16_t x      : 1;
          uint16_t l      : 1;
          uint16_t r      : 1;
        } btn;
        int16 value;
      } u;
    };
    enum SnesButton: uint {
      Up, Down, Left, Right, B, A, Y, X, L, R, Select, Start, Count
    };
    vector<GekkoNetworkStats> netStats;
    vector<Buttons> inputs;
    vector<Peer> peers;
    vector<StateSnapshot> stateCache;
    vector<ChecksumRange> checksumRanges;
    vector<Emulator::SerializeComponent> stateMap;
    //only frames within the rollback window are ever compared; each slot holds a full state
    enum : uint { StateCacheFrames = 32, DesyncDumpLimit = 3 };
    string instance = "single";
    string report;
    bool detectDesyncs = false;
    uint desyncCount = 0;
    uint32 lastChecksum = 0;
    uint crossPeerDesyncCount = 0;
    GekkoConfig config = {};
    GekkoSession* session = nullptr;
    uint counter = 0;
    double speedScale = 1.0;
    uint8 localRunAhead = 0;
  } netplay;
  auto netplayMode(Netplay::Mode) -> void;
  auto netplayApplyDeterministicSettings() -> void;
  auto netplaySetRunAhead(uint8 frames) -> void;
  auto netplayApplyRunAhead() -> void;
  auto netplayStart(uint16 port, uint8 local, uint8 rollback, uint8 delay, vector<string>& remotes, vector<string>& spectator, bool detectDesyncs) -> void;
  auto netplayStressStart(uint8 players, uint8 checkDistance) -> void;
  auto netplayRandomInput(uint player) -> Netplay::Buttons;
  auto netplayStop() -> void;
  auto netplayRun() -> bool;
  auto netplayDesyncPath() -> string;
  auto netplayReport(const string& line) -> void;
  auto netplayBeginDiagnostics(bool detectDesyncs) -> void;
  auto netplayBuildStateMap() -> void;
  auto netplayPrintStateMap() -> void;
  auto netplayResetDesyncDirectory() -> void;
  auto netplayStateChecksum(const uint8_t* data, uint size) -> uint32_t;
  auto netplayCacheState(int frame, uint32 checksum, const uint8* data, uint size) -> void;
  auto netplayReportLocalDesync(int frame, uint32 checksumA, const vector<uint8>& bufA, uint32 checksumB, const vector<uint8>& bufB) -> void;
  auto netplayDumpState(int frame, const string& tag, uint32 checksum, const vector<uint8>& data) -> void;
  auto netplayPollLocalInput(Netplay::Buttons& localInput) -> void;
  auto netplayGetInput(uint port, uint device, uint button) -> int16;
  auto netplayTimesync() -> void;
  auto netplayBeginSession(int numPlayers, uint8 rollback, uint8 delay, int maxSpectators, bool detectDesyncs, bool isSpectating, uint spectatorDelay = 5 * 60) -> void;

  //netplay-weyve.cpp
  struct Weyve {
    struct GameEntry {
      string title;
      string path;
      string hash;
    };

    WeyveClient* client = nullptr;
    string lastGameHash;  // last game_hash we checked our library against
    string lastError;
    uint64 idleSince = 0;
    enum : uint { IdleTimeout = 5 * 60 };
    uint32 lastStartToken = 0;
    uint32 lastStopToken = 0;
    uint8 localRollback = 8;
    uint8 localDelay = 2;
    struct Published { string key; string value; };
    vector<Published> published;
    bool pendingListed = false;  // applied once WEYVE_EVENT_ROOM_ID_ASSIGNED fires
    bool rolesDirty = true;  // host reassigns roles on the next poll
    bool libraryScanned = false;
    string lastGamesFolder;  // folder the library was scanned from
    vector<GameEntry> library;  // scanned from settings.weyvelength.gamesFolder, sorted by title
    enum : uint { LogLimit = 200, PlayerCap = 5 };
    vector<string> log;  // lobby event/chat feed, oldest first
    uint logEpoch = 0;  // bumped whenever the feed is cleared
    struct KnownName { uint32 id; string nickname; };
    vector<KnownName> knownNames;
    struct PendingIdentityLog { uint32 id; string suffix; uint64 queuedAt; };
    vector<PendingIdentityLog> pendingIdentityLogs;  // lines waiting on a nickname
  } weyve;
  auto weyveConnect(string host, uint16 port) -> bool;
  auto weyveDisconnect() -> void;
  auto weyveCreateRoom(bool listed) -> void;
  auto weyvePublish(const string& key, const string& value) -> void;
  auto weyveMarkActive() -> void;
  auto weyveListRooms() -> void;
  auto weyveJoinRoom(string id, string password) -> void;
  auto weyveLeaveRoom() -> void;
  auto weyveResetRoomState() -> void;
  auto weyveSessionActive() -> bool;
  auto weyveConnected() -> bool;
  auto weyveRoleOf(uint32 memberId) -> string;
  auto weyveRoleLabel(const string& role) -> string;
  auto weyvePlayerOrder() -> vector<uint32>;
  auto weyveCompactRoles(const vector<uint32>& players) -> void;
  auto weyveAutoAssignRoles() -> void;
  auto weyveSetRole(uint32 memberId, string role) -> void;
  auto weyveSetBaseline(uint8 rollback, uint8 delay) -> void;
  auto weyveKick(uint32 memberId) -> void;
  auto weyveStartBlockedReason() -> string;
  auto weyveStartGame() -> void;
  auto weyveStopGame() -> void;
  auto weyveSetLocalRollback(uint8 value) -> void;
  auto weyveSetLocalDelay(uint8 value) -> void;
  auto weyvePoll() -> void;
  auto weyveLog(string line) -> void;
  auto weyveLogIdentity(uint32 id, string suffix) -> void;
  auto weyveRoomDataString(const string& key) -> string;
  auto weyveMemberDataString(uint32 memberId, const string& key) -> string;
  auto weyveCopyRoomCode() -> void;
  auto weyveRememberName(uint32 id, const string& nickname) -> void;
  auto weyveNicknameOf(uint32 id) -> string;
  auto weyveGameFingerprint() -> string;
  auto weyveScanLibrary() -> void;
  auto weyveLibraryStale() -> bool;
  auto weyveHasGame(const string& hash) -> maybe<string>;
  auto weyveSelectGame(uint index) -> void;
  auto netplayStartWeyve() -> void;

  //video.cpp
  auto updateVideoDriver(Window parent) -> void;
  auto updateVideoExclusive() -> void;
  auto updateVideoBlocking() -> void;
  auto updateVideoFlush() -> void;
  auto updateVideoMonitor() -> void;
  auto updateVideoFormat() -> void;
  auto updateVideoShader() -> void;
  auto updateVideoPalette() -> void;
  auto updateVideoEffects() -> void;
  auto toggleVideoFullScreen() -> void;
  auto toggleVideoPseudoFullScreen() -> void;

  //audio.cpp
  auto updateAudioDriver(Window parent) -> void;
  auto updateAudioExclusive() -> void;
  auto updateAudioDevice() -> void;
  auto updateAudioBlocking() -> void;
  auto updateAudioDynamic() -> void;
  auto updateAudioFrequency() -> void;
  auto updateAudioLatency() -> void;
  auto updateAudioEffects() -> void;

  //input.cpp
  auto updateInputDriver(Window parent) -> void;

  //utility.cpp
  auto openGame(BrowserDialog& dialog) -> string;
  auto openFile(BrowserDialog& dialog) -> string;
  auto saveFile(BrowserDialog& dialog) -> string;
  auto selectPath() -> string;
  auto showMessage(string text) -> void;
  auto showFrameRate(string text) -> void;
  auto updateStatus() -> void;
  auto captureScreenshot() -> bool;
  auto inactive() -> bool;
  auto focused() -> bool;

  //patch.cpp
  auto appliedPatch() const -> bool;
  auto applyPatchIPS(vector<uint8_t>& data, string location) -> bool;
  auto applyPatchBPS(vector<uint8_t>& data, string location) -> bool;

  //hacks.cpp
  auto hackCompatibility() -> void;
  auto hackPatchMemory(vector<uint8_t>& data) -> void;

  //filter.cpp
  auto filterSelect(uint& width, uint& height, uint scale) -> Filter::Render;

  //viewport.cpp
  auto viewportSize(uint& width, uint& height, uint scale) -> void;
  auto viewportRefresh() -> void;

public:
  struct Game {
    explicit operator bool() const { return (bool)location; }

    string option;
    string location;
    string manifest;
    Markup::Node document;
    boolean patched;
    boolean verified;
  };

  struct SuperFamicom : Game {
    string title;
    string region;
    vector<uint8_t> program;
    vector<uint8_t> data;
    vector<uint8_t> expansion;
    vector<uint8_t> firmware;
  } superFamicom;

  struct GameBoy : Game {
    vector<uint8_t> program;
  } gameBoy;

  struct BSMemory : Game {
    vector<uint8_t> program;
  } bsMemory;

  struct SufamiTurbo : Game {
    vector<uint8_t> program;
  } sufamiTurboA, sufamiTurboB;

  vector<string> gameQueue;

  uint32_t palette[32768];
  uint32_t paletteDimmed[32768];

  struct Screenshot {
    const uint16* data = nullptr;
    uint pitch  = 0;
    uint width  = 0;
    uint height = 0;
    uint scale  = 0;
  } screenshot;

  bool frameAdvanceLock = false;

  uint64 autoSaveTime;

  uint64 statusTime;
  string statusMessage;
  string statusFrameRate;

  bool startFullScreen = false;
  uint stressTestFrameLimit = 0;
  bool stressTestEnabled = false;
  uint8 stressTestCheckDistance = 8;
  uint8 stressTestPlayers = 2;
  uint64 stressTestSeed = 0;

  struct Mute { enum : uint {
    Always      = 1 << 1,
    Unfocused   = 1 << 2,
    FastForward = 1 << 3,
    Rewind      = 1 << 4,
    Modal       = 1 << 5,
  };};
  uint mute = 0;

  bool fastForwarding = false;
  bool rewinding = false;
};

extern Program program;

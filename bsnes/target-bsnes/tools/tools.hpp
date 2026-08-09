struct CheatCandidate {
  uint32_t address;
  uint32_t data;
  uint32_t size;
  uint32_t mode;
  uint32_t span;
};

struct CheatFinder : VerticalLayout {
  auto create() -> void;
  auto restart() -> void;
  auto refresh() -> void;
  auto eventScan() -> void;
  auto eventClear() -> void;
  auto read(uint32_t size, uint32_t address) -> uint32_t;
  auto compare(uint32_t mode, uint32_t x, uint32_t y) -> bool;

public:
  vector<CheatCandidate> candidates;

  TableView searchList{this, Size{~0, ~0}};
  HorizontalLayout controlLayout{this, Size{~0, 0}};
    Label searchLabel{&controlLayout, Size{0, 0}};
    LineEdit searchValue{&controlLayout, Size{~0, 0}};
    ComboButton searchSize{&controlLayout, Size{0, 0}};
    ComboButton searchMode{&controlLayout, Size{0, 0}};
    ComboButton searchSpan{&controlLayout, Size{0, 0}};
    Button searchScan{&controlLayout, Size{80, 0}};
    Button searchClear{&controlLayout, Size{80, 0}};
};

struct Cheat {
  auto operator==(const Cheat& compare) const -> bool {
    return name == compare.name && code == compare.code && enable == compare.enable;
  }

  auto operator<(const Cheat& compare) const -> bool {
    return string::icompare(name, compare.name) < 0;
  }

  string name;
  string code;
  bool enable;
};

struct CheatDatabase : Window {
  auto create() -> void;
  auto findCheats() -> void;
  auto addCheats() -> void;

public:
  VerticalLayout layout{this};
    ListView cheatList{&layout, Size{~0, ~0}};
    HorizontalLayout controlLayout{&layout, Size{~0, 0}};
      Button selectAllButton{&controlLayout, Size{100_sx, 0}};
      Button unselectAllButton{&controlLayout, Size{100_sx, 0}};
      Widget spacer{&controlLayout, Size{~0, 0}};
      Button addCheatsButton{&controlLayout, Size{100_sx, 0}};
};

struct CheatWindow : Window {
  auto create() -> void;
  auto show(Cheat cheat = {}) -> void;
  auto doChange() -> void;
  auto doAccept() -> void;

public:
  VerticalLayout layout{this};
    TableLayout tableLayout{&layout, Size{~0, ~0}};
      Label nameLabel{&tableLayout, Size{0, 0}};
      LineEdit nameValue{&tableLayout, Size{~0, 0}};
      Label codeLabel{&tableLayout, Size{0, 0}};
      TextEdit codeValue{&tableLayout, Size{~0, ~0}};
    HorizontalLayout controlLayout{&layout, Size{~0, 0}};
      Widget controlSpacer{&controlLayout, Size{~0, 0}};
      CheckLabel enableOption{&controlLayout, Size{0, 0}};
      Button acceptButton{&controlLayout, Size{80_sx, 0}};
      Button cancelButton{&controlLayout, Size{80_sx, 0}};
};

struct CheatEditor : VerticalLayout {
  auto create() -> void;
  auto refresh() -> void;
  auto addCheat(Cheat cheat) -> void;
  auto editCheat(Cheat cheat) -> void;
  auto removeCheats() -> void;
  auto loadCheats() -> void;
  auto saveCheats() -> void;
  auto synchronizeCodes() -> void;

  auto decodeSNES(string& code) -> bool;
  auto decodeGB(string& code) -> bool;

public:
  vector<Cheat> cheats;
  uint64_t activateTimeout = 0;

  TableView cheatList{this, Size{~0, ~0}};
  HorizontalLayout controlLayout{this, Size{~0, 0}};
    Button findCheatsButton{&controlLayout, Size{120_sx, 0}};
    Widget spacer{&controlLayout, Size{~0, 0}};
    CheckLabel enableCheats{&controlLayout, Size{0, 0}};
    Button addButton{&controlLayout, Size{80_sx, 0}};
    Button editButton{&controlLayout, Size{80_sx, 0}};
    Button removeButton{&controlLayout, Size{80_sx, 0}};
};

struct StateWindow : Window {
  auto create() -> void;
  auto show(string name = {}) -> void;
  auto doChange() -> void;
  auto doAccept() -> void;

public:
  VerticalLayout layout{this};
    HorizontalLayout nameLayout{&layout, Size{~0, 0}};
      Label nameLabel{&nameLayout, Size{40_sx, 0}};
      LineEdit nameValue{&nameLayout, Size{~0, 0}};
    HorizontalLayout controlLayout{&layout, Size{~0, 0}};
      Widget spacer{&controlLayout, Size{~0, 0}};
      Button acceptButton{&controlLayout, Size{80_sx, 0}};
      Button cancelButton{&controlLayout, Size{80_sx, 0}};
};

struct StateManager : VerticalLayout, Lock {
  auto create() -> void;
  auto type() const -> string;
  auto loadStates() -> void;
  auto createState(string name) -> void;
  auto modifyState(string name) -> void;
  auto removeStates() -> void;
  auto updateSelection() -> void;
  auto stateEvent(string name) -> void;

public:
  enum class SortBy : uint {
    NameAscending,
    NameDescending,
    DateAscending,
    DateDescending,
  } sortBy = SortBy::NameAscending;

  HorizontalLayout stateLayout{this, Size{~0, ~0}};
    TableView stateList{&stateLayout, Size{~0, ~0}};
    VerticalLayout previewLayout{&stateLayout, Size{0, ~0}};
      HorizontalLayout categoryLayout{&previewLayout, Size{~0, 0}};
        Label categoryLabel{&categoryLayout, Size{0, 0}};
        ComboButton categoryOption{&categoryLayout, Size{~0, 0}};
      Canvas statePreviewSeparator1{&previewLayout, Size{~0, 1}};
      Label statePreviewLabel{&previewLayout, Size{~0, 0}};
      Canvas statePreview{&previewLayout, Size{256, 224}};
      Widget stateSpacer{&previewLayout, Size{~0, ~0}};
      Canvas statePreviewSeparator2{&previewLayout, Size{~0, 1}};
  HorizontalLayout controlLayout{this, Size{~0, 0}};
    Button loadButton{&controlLayout, Size{80_sx, 0}};
    Button saveButton{&controlLayout, Size{80_sx, 0}};
    Widget spacer{&controlLayout, Size{~0, 0}};
    Button addButton{&controlLayout, Size{80_sx, 0}};
    Button editButton{&controlLayout, Size{80_sx, 0}};
    Button removeButton{&controlLayout, Size{80_sx, 0}};
};

struct ManifestViewer : VerticalLayout {
  auto create() -> void;
  auto loadManifest() -> void;
  auto selectManifest() -> void;

public:
  HorizontalLayout manifestLayout{this, Size{~0, 0}};
    Label manifestLabel{&manifestLayout, Size{0, 0}};
    ComboButton manifestOption{&manifestLayout, Size{~0, 0}};
  Canvas manifestSpacer{this, Size{~0, 1}};
  HorizontalLayout informationLayout{this, Size{~0, 0}};
    Canvas typeIcon{&informationLayout, Size{16, 16}};
    Label nameLabel{&informationLayout, Size{~0, 0}};
  #if 0 && defined(Hiro_SourceEdit)
  SourceEdit manifestView{this, Size{~0, ~0}};
  #else
  TextEdit manifestView{this, Size{~0, ~0}};
  #endif
};

struct ToolsWindow : Window {
  auto create() -> void;
  auto setVisible(bool visible = true) -> ToolsWindow&;
  auto show(int index) -> void;

public:
  VerticalLayout layout{this};
    HorizontalLayout panelLayout{&layout, Size{~0, ~0}};
      ListView panelList{&panelLayout, Size{160_sx, ~0}};
      VerticalLayout panelContainer{&panelLayout, Size{~0, ~0}};
};

struct NetplayWindow : Window {
    enum class Role : uint {
        Player1,
        Player2, 
        Player3,
        Player4,
        Player5,
        Spectator
    };

    auto create() -> void;
    auto setVisible(bool visible = true) -> NetplayWindow&;
    auto show() -> void;

private:
    struct PlayerEntry {
        bool isSpectator = false;
        uint playerNumber = 0;
        string ip;
        string port;
    };

    auto updateRole(Role role) -> void;
    auto autoPopulatePlayerList() -> void;
    auto addPlayer() -> void;
    auto addSpectator() -> void;
    auto removeSelectedPlayer() -> void;
    auto startSession() -> void;
    auto roleToPlayerIndex(Role role) -> uint8;

    auto isValidIP(const string& ip) -> bool;
    auto isLoopbackIP(const string& ip) -> bool;
    auto isValidRemoteIP(const string& ip) -> bool;
    auto isValidPort(const string& port) -> bool;
    auto setValidationColor(LineEdit& field, bool valid, bool hasText) -> void;
    auto roleText(const PlayerEntry& entry) -> string;
    auto rowText(const PlayerEntry& entry) -> string;
    auto rebuildList() -> void;
    auto selectedIndex() -> maybe<uint>;
    auto updateEditingState() -> void;
    auto updateSelectedIP(const string& ip) -> void;
    auto updateSelectedPort(const string& port) -> void;
    auto sortPlayerList() -> void;

    vector<PlayerEntry> entries;

    Role currentRole = Role::Player1;
    bool devMode = false;

    VerticalLayout layout{this};
    
    // Role selection
    Label roleLabel{&layout, Size{~0, 0}};
    HorizontalLayout roleLayout{&layout, Size{~0, 0}};
      RadioLabel rolePlayer1{&roleLayout, Size{80_sx, 0}};
      RadioLabel rolePlayer2{&roleLayout, Size{80_sx, 0}};
      RadioLabel rolePlayer3{&roleLayout, Size{80_sx, 0}};
      RadioLabel rolePlayer4{&roleLayout, Size{80_sx, 0}};
      RadioLabel rolePlayer5{&roleLayout, Size{80_sx, 0}};
      RadioLabel roleSpectator{&roleLayout, Size{80_sx, 0}};
    Group roleGroup{&rolePlayer1, &rolePlayer2, &rolePlayer3, &rolePlayer4, &rolePlayer5, &roleSpectator};
    Canvas roleSpacer{&layout, Size{~0, 1}};
    
    // Connection info header
    Label connectionLabel{&layout, Size{~0, 0}};
    
    // Port configuration
    HorizontalLayout portLayout{&layout, Size{~0, 0}};
      Label portLabel{&portLayout, Size{100_sx, 0}};
      LineEdit portValue{&portLayout, Size{100_sx, 0}};
      Widget portSpacer1{&portLayout, Size{10_sx, 0}};
      // Spectator player count (only visible for spectators, same row as port)
      Label spectatorPlayerCountLabel{&portLayout, Size{130_sx, 0}};
      LineEdit spectatorPlayerCountValue{&portLayout, Size{60_sx, 0}};
      Widget portSpacer2{&portLayout, Size{~0, 0}};
    
    // Sliders layout - horizontal
    HorizontalLayout slidersLayout{&layout, Size{~0, 0}};
      // Rollback frames
      Label rollbackLabel{&slidersLayout, Size{110_sx, 0}};
      HorizontalSlider rollbackSlider{&slidersLayout, Size{~0, 0}};
      Label rollbackValue{&slidersLayout, Size{35_sx, 0}};
      // Spacing between sliders
      Widget sliderMiddleSpacer{&slidersLayout, Size{15_sx, 0}};
      // Input delay
      Label delayLabel{&slidersLayout, Size{80_sx, 0}};
      HorizontalSlider delaySlider{&slidersLayout, Size{~0, 0}};
      Label delayValue{&slidersLayout, Size{35_sx, 0}};
    
    Canvas sliderSpacer{&layout, Size{~0, 1}};
    
    // Remote players section
    Label remotePlayersLabel{&layout, Size{~0, 0}};
    ListView remotePlayersList{&layout, Size{~0, ~0}};
    
    // Editing section
    Label editingLabel{&layout, Size{~0, 0}};
    HorizontalLayout editLayout{&layout, Size{~0, 0}};
      Label editIPLabel{&editLayout, Size{35_sx, 0}};
      LineEdit editIPValue{&editLayout, Size{~0, 0}};
      Widget editSpacer1{&editLayout, Size{5_sx, 0}};
      Label editPortLabel{&editLayout, Size{40_sx, 0}};
      LineEdit editPortValue{&editLayout, Size{100_sx, 0}};
      Widget editSpacer2{&editLayout, Size{~0, 0}};
    
    // Action buttons for list
    HorizontalLayout remoteButtonsLayout{&layout, Size{~0, 0}};
      Button btnAddPlayer{&remoteButtonsLayout, Size{100_sx, 0}};
      Button btnAddSpectator{&remoteButtonsLayout, Size{120_sx, 0}};
      Button btnRemove{&remoteButtonsLayout, Size{100_sx, 0}};
      Widget remoteSpacer{&remoteButtonsLayout, Size{~0, 0}};
    
    // Spacer to push bottom controls to end
    Canvas bottomSpacer{&layout, Size{~0, 1}};
    
    // Bottom controls layout
    HorizontalLayout bottomControlsLayout{&layout, Size{~0, 0}};
      // Dev mode checkbox
      CheckLabel devModeCheck{&bottomControlsLayout, Size{0, 0}};
      Widget bottomCheckSpacer{&bottomControlsLayout, Size{10_sx, 0}};
      CheckLabel desyncCheck{&bottomControlsLayout, Size{0, 0}};
      Widget bottomMiddleSpacer{&bottomControlsLayout, Size{~0, 0}};
      // Action buttons
      Button btnStart{&bottomControlsLayout, Size{120_sx, 0}};
      Button btnCancel{&bottomControlsLayout, Size{80_sx, 0}};

public:
    struct Configuration {
        uint localPort = 55435;
        uint rollbackframes = 8;
        uint localDelay = 2;
        uint spectatorPlayerCount = 2;
        bool desyncDetection = false;
    } config;
};

struct WeyveWindow : Window {
    auto create() -> void;
    auto setVisible(bool visible = true) -> WeyveWindow&;
    auto show() -> void;
    auto refresh() -> void;

private:
    auto attemptConnect() -> void;
    auto createRoomPressed() -> void;
    auto joinRoomPressed() -> void;
    auto refreshRoomList() -> void;
    auto sendChat() -> void;
    auto memberRow(uint32_t id, bool roomHasGame, uint32_t hostId, uint32_t selfId) -> string;
    auto selectedMemberId() -> maybe<uint32_t>;

    uint gameLibrarySize = 0;
    Timer refreshTimer;
    bool lastConnected = false;
    bool lastInRoom = false;
    int lastIsHost = -1;
    bool gameAutoSelectDone = false;
    vector<string> lastMemberRows;
    vector<string> lastRoomListRows;
    string lastNonHostGame;
    string lastStartReason;
    int lastRunning = -1;
    uint lastRoleCount = 0;
    uint8 lastShownRollback = 0;
    uint8 lastShownDelay = 0;

    VerticalLayout layout{this};

    VerticalLayout connectScreen{&layout, Size{~0, ~0}};
      Label connectStatus{&connectScreen, Size{~0, 0}};
      Button btnRetry{&connectScreen, Size{~0, 0}};
      Widget connectSpacer{&connectScreen, Size{~0, ~0}};  // vertical layouts need one Maximum-height cell

    VerticalLayout browseScreen{&layout, Size{~0, ~0}};
      HorizontalLayout browseButtons{&browseScreen, Size{~0, 0}};
        Button btnRefreshRooms{&browseButtons, Size{100_sx, 0}};
        Button btnCreateRoom{&browseButtons, Size{100_sx, 0}};
        CheckLabel publicCheck{&browseButtons, Size{0, 0}};
        Widget browseSpacer{&browseButtons, Size{~0, 0}};
        Button btnDisconnect{&browseButtons, Size{100_sx, 0}};
      ListView roomList{&browseScreen, Size{~0, ~0}};
      HorizontalLayout joinLayout{&browseScreen, Size{~0, 0}};
        Label joinCodeLabel{&joinLayout, Size{70_sx, 0}};
        LineEdit joinCodeValue{&joinLayout, Size{~0, 0}};
        Label joinPasswordLabel{&joinLayout, Size{70_sx, 0}};
        LineEdit joinPasswordValue{&joinLayout, Size{~0, 0}};
        Button btnJoinRoom{&joinLayout, Size{100_sx, 0}};

    VerticalLayout lobbyScreen{&layout, Size{~0, ~0}};
      HorizontalLayout roomHeader{&lobbyScreen, Size{~0, 0}};
        Label roomLabel{&roomHeader, Size{~0, 0}};
        Button btnCopyCode{&roomHeader, Size{0, 0}};
      HorizontalLayout gameLayout{&lobbyScreen, Size{~0, 0}};
        Label gameSelectLabel{&gameLayout, Size{0, 0}};
        ComboButton gameCombo{&gameLayout, Size{~0, 0}};  // enabled host-only
        Button btnHostSettings{&gameLayout, Size{0, 0}};  // host-only, opens WeyveHostSettings popup
      HorizontalLayout localLayout{&lobbyScreen, Size{~0, 0}};
        Label rollbackLabel{&localLayout, Size{0, 0}};
        LineEdit rollbackValue{&localLayout, Size{~0, 0}};
        Label delayLabel{&localLayout, Size{0, 0}};
        LineEdit delayValue{&localLayout, Size{~0, 0}};
        Label runAheadLabel{&localLayout, Size{0, 0}};  // local only, never synced
        LineEdit runAheadValue{&localLayout, Size{~0, 0}};
      ListView memberList{&lobbyScreen, Size{~0, 70_sy}};
      HorizontalLayout roleLayout{&lobbyScreen, Size{~0, 0}};  // host-only
        ComboButton roleCombo{&roleLayout, Size{100_sx, 0}};
        Button btnAssignRole{&roleLayout, Size{90_sx, 0}};
        Button btnKick{&roleLayout, Size{70_sx, 0}};
        Widget roleSpacer{&roleLayout, Size{~0, 0}};
      ListView eventLog{&lobbyScreen, Size{~0, ~0}};
      HorizontalLayout chatLayout{&lobbyScreen, Size{~0, 0}};
        LineEdit chatValue{&chatLayout, Size{~0, 0}};
        Button btnChatSend{&chatLayout, Size{80_sx, 0}};
      HorizontalLayout lobbyButtons{&lobbyScreen, Size{~0, 0}};
        Button btnStart{&lobbyButtons, Size{100_sx, 0}};  // host-only
        Button btnStop{&lobbyButtons, Size{100_sx, 0}};  // host-only, ends the session for everyone
        Label startStatus{&lobbyButtons, Size{~0, 0}};  // host-only; fills the spacer role, shows why Start is disabled
        Button btnLeave{&lobbyButtons, Size{100_sx, 0}};

    uint loggedLines = 0;
};

struct WeyveHostSettings : Window {
    auto create() -> void;
    auto setVisible(bool visible = true) -> WeyveHostSettings&;
    auto show() -> void;
    auto refresh() -> void;

private:
    Timer refreshTimer;
    int lastListed = -1;
    int lastOpen = -1;
    int lastDesync = -1;
    int lastRunning = -1;

    VerticalLayout layout{this};
      HorizontalLayout roomSettingsLayout{&layout, Size{~0, 0}};
        CheckLabel listedCheck{&roomSettingsLayout, Size{0, 0}};
        CheckLabel openCheck{&roomSettingsLayout, Size{0, 0}};
        CheckLabel desyncCheck{&roomSettingsLayout, Size{0, 0}};
        Widget roomSettingsSpacer{&roomSettingsLayout, Size{~0, 0}};
      HorizontalLayout passwordLayout{&layout, Size{~0, 0}};
        Label passwordLabel{&passwordLayout, Size{70_sx, 0}};
        LineEdit passwordValue{&passwordLayout, Size{~0, 0}};
        Button btnSetPassword{&passwordLayout, Size{80_sx, 0}};
      HorizontalLayout baselineLayout{&layout, Size{~0, 0}};
        Label rollbackMinLabel{&baselineLayout, Size{0, 0}};
        LineEdit rollbackMinValue{&baselineLayout, Size{~0, 0}};
        Label delayMinLabel{&baselineLayout, Size{0, 0}};
        LineEdit delayMinValue{&baselineLayout, Size{~0, 0}};
      HorizontalLayout spectatorLayout{&layout, Size{~0, 0}};
        Label spectatorDelayLabel{&spectatorLayout, Size{0, 0}};
        LineEdit spectatorDelayValue{&spectatorLayout, Size{~0, 0}};
      Widget spacer{&layout, Size{~0, ~0}};
};

namespace Instances { extern Instance<CheatDatabase> cheatDatabase; }
extern CheatFinder cheatFinder;
extern CheatDatabase& cheatDatabase;
namespace Instances { extern Instance<CheatWindow> cheatWindow; }
extern CheatWindow& cheatWindow;
extern CheatEditor cheatEditor;
namespace Instances { extern Instance<StateWindow> stateWindow; }
extern StateWindow& stateWindow;
extern StateManager stateManager;
extern ManifestViewer manifestViewer;
namespace Instances { extern Instance<ToolsWindow> toolsWindow; }
extern ToolsWindow& toolsWindow;
namespace Instances { extern Instance<NetplayWindow> netplayWindow; }
extern NetplayWindow& netplayWindow;
namespace Instances { extern Instance<WeyveWindow> weyveWindow; }
extern WeyveWindow& weyveWindow;
namespace Instances { extern Instance<WeyveHostSettings> weyveHostSettings; }
extern WeyveHostSettings& weyveHostSettings;
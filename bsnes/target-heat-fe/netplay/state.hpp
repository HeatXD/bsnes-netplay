#pragma once

#include "../emucore/emucore.hpp"

#include <gekkonet.h>
#include <weyvelength.h>

#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <vector>

constexpr int NetplayButtonCount = EmuCore::ButtonCount;

struct NetplayPeer {
  int id = 0;
  GekkoPlayerType type = GekkoLocalPlayer;
  bool connected = false;
  GekkoNetworkStats stats{};
  std::string addr;
  uint32_t weyveId = 0;
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
  std::vector<uint16_t> inputs;
  std::deque<NetplayStateSnapshot> stateCache;
  static constexpr int StateCacheFrames = 32;

  GekkoConfig config{};
  GekkoSession* session = nullptr;
  std::string instance;
  int localPlayer = 0;
  int localActorId = 0;
  bool detectDesyncs = false;
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
  int playerNumber = 1;
  char ip[64] = "127.0.0.1";
  char port[8] = "7000";
};

struct WeyveRoomListing {
  std::string id;
  uint32_t members = 0;
  bool joinable = true;
  bool passworded = false;
  bool running = false;
  bool statusKnown = false;
  std::string game;
  std::string host;
};

struct WeyveConnectAttempt {
  std::atomic<bool> complete = false;
  WeyveClient* client = nullptr;

  ~WeyveConnectAttempt() {
    if(client) { weyve_client_destroy(client); }
  }
};

struct Weyve {
  WeyveClient* client = nullptr;
  std::shared_ptr<WeyveConnectAttempt> connecting;
  std::string lastError;
  uint64_t idleSince = 0;
  uint64_t roomListRequestedAt = 0;
  static constexpr uint64_t IdleTimeoutMs = 5 * 60 * 1000;

  bool pendingListed = false;
  bool pendingCreate = false;
  bool pendingLeave = false;
  bool connectAttempted = false;
  bool focusTab = false;
  bool openHostSettings = false;
  bool rolesDirty = true;
  uint32_t lastStartToken = 0;
  uint32_t lastStopToken = 0;
  uint32_t selectedMember = 0;
  int localRollback = 8;
  int localDelay = 2;
  int rollbackBaseline = 8;
  int delayBaseline = 2;
  int spectatorDelay = 300;
  std::string lastGameHash;

  std::vector<WeyveKnownName> knownNames;
  static constexpr int LogLimit = 200;
  std::deque<std::string> log;
  char chatInput[256] = {};
  std::vector<WeyveRoomListing> roomList;

  static constexpr int PlayerCap = 5;
  static constexpr int SpectatorCap = 32;
};

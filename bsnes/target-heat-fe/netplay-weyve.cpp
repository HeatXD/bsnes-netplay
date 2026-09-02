#include "app.hpp"

#include <algorithm>
#include <thread>
#include <cstdlib>

namespace {

App* gWeyveApp = nullptr;

void weyveAdapterSend(GekkoNetAddress* addr, const char* data, int length) {
  if(!gWeyveApp || !gWeyveApp->weyve.client) return;
  uint32_t id = 0;
  SDL_memcpy(&id, addr->data, sizeof(id));
  weyve_send_p2p(gWeyveApp->weyve.client, id, data, (uint32_t)length);
}

std::vector<GekkoNetResult*> weyveResults;

GekkoNetResult** weyveAdapterReceive(int* length) {
  weyveResults.clear();  // GekkoNet frees the previous batch itself, via free_data
  if(gWeyveApp && gWeyveApp->weyve.client) {
    uint32_t from = 0, len = 0;
    while(const uint8_t* data = weyve_next_p2p(gWeyveApp->weyve.client, &from, &len)) {
      GekkoNetResult* result = (GekkoNetResult*)std::malloc(sizeof(GekkoNetResult));
      result->addr.data = std::malloc(sizeof(uint32_t));
      SDL_memcpy(result->addr.data, &from, sizeof(uint32_t));
      result->addr.size = sizeof(uint32_t);
      result->data_len = len;
      result->data = std::malloc(len);
      SDL_memcpy(result->data, data, len);
      weyveResults.push_back(result);
    }
  }
  *length = (int)weyveResults.size();
  return weyveResults.data();
}

void weyveAdapterFree(void* dataPtr) { std::free(dataPtr); }

GekkoNetAdapter weyveAdapter{weyveAdapterSend, weyveAdapterReceive, weyveAdapterFree};

std::string bytes(const char* data, uint32_t len) { return data && len ? std::string(data, len) : std::string(); }

std::string weyveRoomErrorText(WeyveRoomError code) {
  switch(code) {
  case WEYVE_ROOM_ERROR_ALREADY_IN_ROOM: return "You are already in a room";
  case WEYVE_ROOM_ERROR_NO_SUCH_ROOM:    return "No room with that code";
  case WEYVE_ROOM_ERROR_NOT_IN_ROOM:     return "You are not in a room";
  case WEYVE_ROOM_ERROR_NOT_HOST:        return "Only the host can do that";
  case WEYVE_ROOM_ERROR_BAD_ROOM_DATA:   return "The server rejected that room data";
  case WEYVE_ROOM_ERROR_NO_SUCH_MEMBER:  return "That member is no longer in the room";
  case WEYVE_ROOM_ERROR_ROOM_CLOSED:     return "That room is not joinable right now";
  case WEYVE_ROOM_ERROR_BAD_PASSWORD:    return "Wrong password";
  case WEYVE_ROOM_ERROR_BANNED:          return "You are banned from that room";
  case WEYVE_ROOM_ERROR_RATE_LIMITED:    return "Too many attempts, try again shortly";
  }
  return "Room error " + std::to_string((int)code);
}

// FNV-1a over raw file bytes; not cryptographic, just a stable content key
std::string hashFile(const std::string& path) {
  const std::vector<uint8_t> data = readBytes(path);
  if(data.empty()) return "";
  uint64_t hash = 1469598103934665603ull;
  for(uint8_t byte : data) hash = (hash ^ byte) * 1099511628211ull;
  char hex[17];
  SDL_snprintf(hex, sizeof(hex), "%016llx", (unsigned long long)hash);
  return hex;
}

}  // namespace

bool App::weyveConnect(const std::string& host, uint16_t port) {
  if(weyve.client || weyve.connecting) weyveDisconnect();

  weyve.connectAttempted = true;
  weyve.lastError.clear();
  weyve.connecting = std::make_shared<WeyveConnectAttempt>();
  const auto attempt = weyve.connecting;
  std::thread([attempt, host, port] {
    WeyveClient* client = weyve_client_create();
    if(!weyve_connect(client, host.c_str(), port)) {
      weyve_client_destroy(client);
      client = nullptr;
    }
    attempt->client = client;
    attempt->complete.store(true, std::memory_order_release);
  }).detach();
  return true;
}

void App::weyveDisconnect() {
  weyve.connecting.reset();
  if(!weyve.client) return;
  const bool wasInRoom = weyveInRoom();
  if(weyveSessionActive()) {
    netplayStop();
  }
  if(wasInRoom) pendingNetplayUnload = true;
  weyve_client_destroy(weyve.client);
  weyve.client = nullptr;
  weyveClearRoomList();
}

bool App::weyveInRoom() const {
  if(!weyve.client) return false;
  uint32_t len = 0;
  weyve_room_id(weyve.client, &len);
  return len > 0;
}

void App::weyveCreateRoom(bool listed) {
  if(!weyve.client) return;
  weyve.idleSince = SDL_GetTicks();
  weyve.pendingListed = listed;
  weyve.pendingCreate = true;
  weyve_create_room(weyve.client);
}

void App::weyveJoinRoom(const std::string& id, const std::string& password) {
  if(!weyve.client) return;
  weyve.idleSince = SDL_GetTicks();
  weyve.pendingCreate = false;
  weyve.lastError.clear();
  weyve_join_room(weyve.client, id.c_str(), password.empty() ? nullptr : password.c_str());
}

void App::weyveResetRoomState() {
  weyve.pendingLeave = false;
  weyve.lastGameHash.clear();
  weyve.lastStartToken = 0;
  weyve.lastStopToken = 0;
  weyve.selectedMember = 0;
  weyve.knownNames.clear();
  weyve.log.clear();
  weyve.rolesDirty = true;
}

void App::weyveLeaveRoom() {
  if(!weyve.client || weyve.pendingLeave) return;
  if(weyve_is_host(weyve.client)) weyveStopGame();
  if(weyveSessionActive()) netplayStop();
  pendingNetplayUnload = true;
  weyve.pendingLeave = weyve_leave_room(weyve.client);
}

void App::weyveLog(std::string line) {
  weyve.log.push_back(line);
  while((int)weyve.log.size() > Weyve::LogLimit) weyve.log.pop_front();
}

std::string App::weyveRoomData(const std::string& key) const {
  if(!weyve.client) return "";
  uint32_t len = 0;
  const char* value = weyve_room_data(weyve.client, key.c_str(), &len);
  return bytes(value, len);
}

std::string App::weyveMemberData(uint32_t memberId, const std::string& key) const {
  if(!weyve.client) return "";
  uint32_t len = 0;
  const char* value = weyve_member_data(weyve.client, memberId, key.c_str(), &len);
  return bytes(value, len);
}

std::string App::weyveRoleOf(uint32_t memberId) const {
  const std::string role = weyveRoomData("role:" + std::to_string(memberId));
  return role.empty() ? "spec" : role;
}

std::string App::weyveRoleLabel(const std::string& role) const {
  if(role == "spec") return "Spectator";
  return "Player " + std::to_string(SDL_atoi(role.c_str()) + 1);
}

// slot order, host-assigned; slot N is GekkoNet player index N once a session starts
std::vector<uint32_t> App::weyvePlayerOrder() const {
  std::vector<uint32_t> players;
  if(!weyve.client) return players;
  uint32_t count = 0;
  const uint32_t* members = weyve_members(weyve.client, &count);
  std::vector<std::pair<int, uint32_t>> entries;
  for(uint32_t i = 0; i < count; i++) {
    const std::string role = weyveRoleOf(members[i]);
    if(role == "spec") continue;
    entries.push_back({SDL_atoi(role.c_str()), members[i]});
  }
  std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) { return a.first < b.first; });
  for(auto& entry : entries) players.push_back(entry.second);
  return players;
}

std::vector<uint32_t> App::weyveSessionPlayerOrder() const {
  std::vector<uint32_t> players;
  const std::string encoded = weyveRoomData("session_players");
  size_t start = 0;
  while(start < encoded.size()) {
    const size_t end = encoded.find(',', start);
    const std::string item = encoded.substr(start, end - start);
    if(!item.empty()) players.push_back((uint32_t)std::strtoul(item.c_str(), nullptr, 10));
    if(end == std::string::npos) break;
    start = end + 1;
  }
  return players.empty() ? weyvePlayerOrder() : players;
}

int App::weyveSpectatorRelay(uint32_t memberId, const std::vector<uint32_t>& players) const {
  if(!weyve.client || players.empty()) return -1;
  uint32_t count = 0;
  const uint32_t* members = weyve_members(weyve.client, &count);
  const size_t first = memberId % players.size();
  for(size_t offset = 0; offset < players.size(); offset++) {
    const size_t player = (first + offset) % players.size();
    for(uint32_t member = 0; member < count; member++) {
      if(players[player] == members[member]) return (int)player;
    }
  }
  return -1;
}

void App::weyveAutoAssignRoles() {
  if(!weyve.client || !weyve_is_host(weyve.client)) return;
  uint32_t count = 0;
  const uint32_t* members = weyve_members(weyve.client, &count);
  std::vector<uint32_t> players = weyvePlayerOrder();

  if(weyveSessionActive()) {
    for(uint32_t i = 0; i < count; i++) {
      const std::string key = "role:" + std::to_string(members[i]);
      if(weyveRoomData(key).empty()) weyve_set_room_data(weyve.client, key.c_str(), "spec");
    }
    return;
  }

  for(uint32_t i = 0; i < count; i++) {
    if(!weyveRoomData("role:" + std::to_string(members[i])).empty()) continue;
    const bool play = players.size() < Weyve::PlayerCap;
    const std::string value = play ? std::to_string(players.size()) : "spec";
    weyve_set_room_data(weyve.client, ("role:" + std::to_string(members[i])).c_str(), value.c_str());
    if(play) players.push_back(members[i]);
  }
  // slots stay contiguous, so a departed player's number is reused rather than left open
  for(size_t i = 0; i < players.size(); i++) {
    if(SDL_atoi(weyveRoleOf(players[i]).c_str()) == (int)i) continue;
    weyve_set_room_data(weyve.client, ("role:" + std::to_string(players[i])).c_str(),
                        std::to_string(i).c_str());
  }
}

void App::weyveSetRole(uint32_t memberId, const std::string& role) {
  if(!weyve.client || !weyve_is_host(weyve.client)) return;
  if(role != "spec") {
    // slots are unique: whoever already holds this one swaps into ours
    uint32_t count = 0;
    const uint32_t* members = weyve_members(weyve.client, &count);
    for(uint32_t i = 0; i < count; i++) {
      if(members[i] == memberId || weyveRoleOf(members[i]) != role) continue;
      weyve_set_room_data(weyve.client, ("role:" + std::to_string(members[i])).c_str(),
                          weyveRoleOf(memberId).c_str());
      break;
    }
  }
  weyve_set_room_data(weyve.client, ("role:" + std::to_string(memberId)).c_str(), role.c_str());
}

void App::weyveRememberName(uint32_t memberId, const std::string& name) {
  if(name.empty()) return;
  for(WeyveKnownName& known : weyve.knownNames) {
    if(known.id != memberId) continue;
    known.name = name;
    return;
  }
  weyve.knownNames.push_back({memberId, name});
}

std::string App::weyveNameOf(uint32_t memberId) const {
  if(weyve.client) {
    uint32_t length = 0;
    const char* name = memberId == weyve_id(weyve.client)
                     ? weyve_name(weyve.client, &length)
                     : weyve_peer_name(weyve.client, memberId, &length);
    if(name && length) return std::string(name, length);
  }
  for(const WeyveKnownName& known : weyve.knownNames) {
    if(known.id == memberId) return known.name;
  }
  return "Player";
}

void App::weyveSetBaseline(int rollback, int delay) {
  if(!weyve.client || !weyve_is_host(weyve.client) || weyveSessionActive()) return;
  weyve.rollbackBaseline = SDL_clamp(rollback, 0, 32);
  weyve.delayBaseline = SDL_clamp(delay, 0, 10);
  weyve_set_room_data(weyve.client, "rollback_min", std::to_string(weyve.rollbackBaseline).c_str());
  weyve_set_room_data(weyve.client, "delay_min", std::to_string(weyve.delayBaseline).c_str());
  weyveSetLocalRollback(weyve.localRollback);
  weyveSetLocalDelay(weyve.localDelay);
}

void App::weyveSetLocalRollback(int frames) {
  const int floor = SDL_max(weyve.rollbackBaseline, SDL_atoi(weyveRoomData("rollback_min").c_str()));
  weyve.localRollback = SDL_clamp(SDL_max(frames, floor), 0, 32);
  if(weyve.client) {
    weyve_set_member_data(weyve.client, "rollback", std::to_string(weyve.localRollback).c_str());
  }
}

void App::weyveSetLocalDelay(int frames) {
  const int floor = SDL_max(weyve.delayBaseline, SDL_atoi(weyveRoomData("delay_min").c_str()));
  weyve.localDelay = SDL_clamp(SDL_max(frames, floor), 0, 10);
  if(weyve.client) {
    weyve_set_member_data(weyve.client, "delay", std::to_string(weyve.localDelay).c_str());
  }
  if(weyveSessionActive()) netplaySetLocalDelay(weyve.localDelay);
}

void App::weyveKick(uint32_t memberId) {
  if(weyve.client && weyve_is_host(weyve.client)) weyve_kick_member(weyve.client, memberId);
}

void App::weyveTransferHost(uint32_t memberId) {
  if(weyve.client && weyve_is_host(weyve.client) && memberId != weyve_id(weyve.client)) {
    weyve_transfer_host(weyve.client, memberId);
  }
}

std::string App::weyveStartBlockedReason() const {
  if(!weyve.client || !weyve_is_host(weyve.client)) return "";
  const std::string targetHash = weyveRoomData("game_hash");
  if(targetHash.empty()) return "pick a game first";

  uint32_t count = 0;
  const uint32_t* members = weyve_members(weyve.client, &count);
  for(uint32_t i = 0; i < count; i++) {
    if(weyveMemberData(members[i], "hasGame") != "1") {
      return weyveNameOf(members[i]) + " doesn't have this game yet";
    }
  }
  if(weyvePlayerOrder().empty()) return "need at least 1 player";
  return "";
}

void App::weyveStartGame() {
  if(!weyve.client || !weyve_is_host(weyve.client)) return;
  const std::string reason = weyveStartBlockedReason();
  if(!reason.empty()) { showMessage("Cannot start: " + reason); return; }

  weyve_set_room_data(weyve.client, "spectator_delay",
                      std::to_string(weyve.spectatorDelay).c_str());
  weyve_set_room_listing(weyve.client, "running", "1");
  std::string sessionPlayers;
  for(uint32_t player : weyvePlayerOrder()) {
    if(!sessionPlayers.empty()) sessionPlayers += ',';
    sessionPlayers += std::to_string(player);
  }
  weyve_set_room_data(weyve.client, "session_players", sessionPlayers.c_str());
  const uint32_t token = (uint32_t)SDL_atoi(weyveRoomData("start_token").c_str()) + 1;
  weyve_set_room_data(weyve.client, "start_token", std::to_string(token).c_str());
}

void App::weyveStopGame() {
  if(!weyve.client || !weyve_is_host(weyve.client)) return;
  const uint32_t token = (uint32_t)SDL_atoi(weyveRoomData("stop_token").c_str()) + 1;
  weyve_set_room_data(weyve.client, "stop_token", std::to_string(token).c_str());
  weyve_set_room_listing(weyve.client, "running", "0");
  weyve_set_room_joinable(weyve.client, true);
  weyve.rolesDirty = true;
}

// searches App::games -- the same library the Games tab scans from
// settings.gamesDir -- for a plain ROM file matching this content hash.
// Pak folders and archive members have no single file to hash, so only plain
// files are candidates; sharing those is the common case for netplay anyway.
std::string App::weyveHasGame(const std::string& hash) const {
  if(hash.empty()) return "";
  for(const auto& entry : games) {
    const std::string& path = entry.second;
    if(isDirectory(path) || !pathExists(path)) continue;
    if(hashFile(path) == hash) return path;
  }
  return "";
}

void App::weyveRescanGames() {
  scanGames();
  const std::string targetHash = weyveRoomData("game_hash");
  if(targetHash.empty()) {
    weyveLog("Rescanned games: no game is selected");
    return;
  }

  const bool hasGame = !weyveHasGame(targetHash).empty();
  weyve.lastGameHash = targetHash;
  weyve_set_member_data(weyve.client, "hasGame", hasGame ? "1" : "0");
  weyveLog(hasGame ? "Rescanned games: selected game found"
                   : "Rescanned games: selected game still missing");
}

std::string App::weyveGameFingerprint() const {
  return gameLocation.empty() ? "" : hashFile(gameLocation);
}

void App::weyveSelectGame(uint32_t gamesIndex) {
  if(!weyve.client || !weyve_is_host(weyve.client) || gamesIndex >= games.size()) return;
  const auto& entry = games[gamesIndex];
  weyve_set_room_data(weyve.client, "game", entry.first.c_str());
  weyve_set_room_data(weyve.client, "game_hash", hashFile(entry.second).c_str());
  weyve_set_room_listing(weyve.client, "game", entry.first.c_str());
}

void App::weyvePublishHostListing() {
  if(!weyve.client || !weyve_is_host(weyve.client)) return;
  uint32_t len = 0;
  const char* name = weyve_name(weyve.client, &len);
  weyve_set_room_listing(weyve.client, "host", bytes(name, len).c_str());
  weyve_set_room_listing(weyve.client, "running", weyveSessionActive() ? "1" : "0");
}

void App::weyveListRooms() {
  if(!weyve.client) return;
  weyve.roomListRequestedAt = SDL_GetTicks();
  weyve_list_rooms(weyve.client, nullptr, 0);
}

void App::weyveClearRoomList() {
  weyve.roomList.clear();
}

void App::weyveCopyRoomCode() const {
  if(!weyve.client) return;
  uint32_t len = 0;
  const char* id = weyve_room_id(weyve.client, &len);
  if(len) SDL_SetClipboardText(bytes(id, len).c_str());
}

void App::weyvePoll() {
  if(weyve.connecting && weyve.connecting->complete.load(std::memory_order_acquire)) {
    const auto attempt = std::move(weyve.connecting);
    if(!attempt->client) {
      weyve.lastError = "could not connect";
      return;
    }
    weyve.client = attempt->client;
    attempt->client = nullptr;
    weyve.idleSince = SDL_GetTicks();
    weyve_set_name(weyve.client, settings.weyveNickname.c_str());
    weyve.focusTab = true;
    weyve.localRollback = settings.netplayRollback;
    weyve.localDelay = settings.netplayDelay;
    weyve.rollbackBaseline = 0;
    weyve.delayBaseline = 0;
    weyve.spectatorDelay = settings.netplaySpectatorDelay;
    weyve.log.clear();
    weyve.lastStartToken = 0;
    weyve.lastStopToken = 0;
    weyveListRooms();
  }
  if(!weyve.client) return;

  if(weyveInRoom()) {
    uint32_t count = 0;
    const uint32_t* members = weyve_members(weyve.client, &count);
    for(uint32_t i = 0; i < count; i++) {
      uint32_t length = 0;
      const char* name = weyve_peer_name(weyve.client, members[i], &length);
      weyveRememberName(members[i], bytes(name, length));
    }
  }

  const bool wasInRoom = weyveInRoom();
  if(!weyve_poll(weyve.client)) {
    if(weyveSessionActive()) {
      netplayStop();
    }
    if(wasInRoom) pendingNetplayUnload = true;
    weyve_client_destroy(weyve.client);
    weyve.client = nullptr;
    const uint64_t elapsed = SDL_GetTicks() - weyve.roomListRequestedAt;
    weyve.lastError = weyve.roomListRequestedAt && elapsed < 5000
      ? "Server protocol is incompatible with this client"
      : "The server closed the connection";
    weyve.roomListRequestedAt = 0;
    return;
  }

  WeyveEvent event;
  while(weyve_next(weyve.client, &event)) {
    switch(event.type) {
    case WEYVE_EVENT_ROOM_ID_ASSIGNED:
      weyve.rolesDirty = true;
      weyveResetRoomState();
      weyveLog("You joined the room");
      break;
    case WEYVE_EVENT_ROOM_ERROR:
      weyve.pendingCreate = false;
      weyve.pendingListed = false;
      weyve.pendingLeave = false;
      weyve.lastError = weyveRoomErrorText(event.data.room_error.code);
      weyveLog(weyve.lastError);
      break;
    case WEYVE_EVENT_CHAT: {
      weyveLog(weyveNameOf(event.data.chat.from) + ": "
             + bytes(event.data.chat.text, event.data.chat.text_len));
      break;
    }
    case WEYVE_EVENT_PEER_JOINED: {
      weyve.rolesDirty = true;
      const std::string name = bytes(event.data.peer_joined.name, event.data.peer_joined.name_len);
      weyveRememberName(event.data.peer_joined.id, name);
      weyveLog(weyveNameOf(event.data.peer_joined.id) + " joined");
      break;
    }
    case WEYVE_EVENT_PEER_LEFT: {
      weyveRemoveNetplayPeer(event.data.peer_left.id);
      if(event.data.peer_left.id == weyve_id(weyve.client)) {
        if(weyveSessionActive()) netplayStop();
        pendingNetplayUnload = true;
        weyveResetRoomState();
        weyveListRooms();
        break;
      }
      weyve.rolesDirty = true;
      if(weyve_is_host(weyve.client)) {
        const std::string key = "role:" + std::to_string(event.data.peer_left.id);
        weyve_set_room_data(weyve.client, key.c_str(), "");
      }
      if(weyve.selectedMember == event.data.peer_left.id) weyve.selectedMember = 0;
      weyveLog(weyveNameOf(event.data.peer_left.id) + " left");
      break;
    }
    case WEYVE_EVENT_HOST_CHANGED: {
      weyve.rolesDirty = true;
      weyveLog(weyveNameOf(event.data.host_changed.id) + " is now the host");
      if(event.data.host_changed.id == weyve_id(weyve.client)) {
        weyvePublishHostListing();
        if(weyve.pendingCreate) {
          weyveSetBaseline(settings.netplayRollback, settings.netplayDelay);
          weyve_set_room_data(weyve.client, "spectator_delay",
                              std::to_string(weyve.spectatorDelay).c_str());
          weyve_set_room_data(weyve.client, "desync",
                              settings.netplayDesyncDetection ? "1" : "0");
          if(weyve.pendingListed) weyve_set_room_listed(weyve.client, true);
          weyve.pendingCreate = false;
          weyve.pendingListed = false;
        }
      }
      break;
    }
    case WEYVE_EVENT_ROOM_DATA_CHANGED: {
      const std::string key = bytes(event.data.room_data.key, event.data.room_data.key_len);
      const std::string value = bytes(event.data.room_data.value, event.data.room_data.value_len);
      if(key == "game") weyveLog("Game: " + value);
      else if(key == "rollback_min") {
        weyve.rollbackBaseline = SDL_atoi(value.c_str());
        weyveLog("Rollback baseline: " + value);
      } else if(key == "delay_min") {
        weyve.delayBaseline = SDL_atoi(value.c_str());
        weyveLog("Input delay baseline: " + value);
      } else if(key == "spectator_delay") {
        weyve.spectatorDelay = SDL_atoi(value.c_str());
        weyveLog("Spectator delay: " + value + " frames");
      } else if(key == "desync") {
        weyveLog(value == "1" ? "Desync detection enabled" : "Desync detection disabled");
      }
      else if(key.rfind("role:", 0) == 0) weyve.rolesDirty = true;
      break;
    }
    case WEYVE_EVENT_MEMBER_DATA_CHANGED: {
      const std::string key = bytes(event.data.member_data.key, event.data.member_data.key_len);
      if(event.data.member_data.id == weyve_id(weyve.client)) break;  // our own echo
      const std::string value = bytes(event.data.member_data.value, event.data.member_data.value_len);
      if(key == "hasGame") {
        weyveLog(weyveNameOf(event.data.member_data.id)
               + (value == "1" ? " has this game" : " doesn't have this game"));
      } else if(key == "rollback" || key == "delay") {
        weyveLog(weyveNameOf(event.data.member_data.id) + " set " + key + " to " + value);
      }
      break;
    }
    case WEYVE_EVENT_KICKED:
      if(weyveSessionActive()) netplayStop();
      pendingNetplayUnload = true;
      weyveResetRoomState();
      weyveLog("You were kicked from the room");
      weyveListRooms();
      break;
    case WEYVE_EVENT_BANNED:
      if(weyveSessionActive()) netplayStop();
      pendingNetplayUnload = true;
      weyveResetRoomState();
      weyveLog("You were banned from the room");
      weyveListRooms();
      break;
    case WEYVE_EVENT_ROOM_ACCESS_CHANGED:
      weyveLog(std::string("Room access: ") + (event.data.room_access.open ? "open" : "closed")
               + (event.data.room_access.passworded ? ", password required" : ", no password"));
      break;
    case WEYVE_EVENT_ROOM_LISTED_CHANGED:
      weyveLog(event.data.room_listed.listed ? "Room is now public" : "Room is now private");
      break;
    case WEYVE_EVENT_ROOM_LIST: {
      // copied out immediately: the client's storage only lasts until the next weyve_poll
      weyve.roomList.clear();
      weyve.roomListRequestedAt = 0;
      const uint32_t count = weyve_room_list_count(weyve.client);
      for(uint32_t i = 0; i < count; i++) {
        WeyveRoomListing listing;
        uint32_t idLen = 0;
        const char* id = weyve_room_list_id(weyve.client, i, &idLen);
        listing.id = bytes(id, idLen);
        listing.members = weyve_room_list_members(weyve.client, i);
        listing.joinable = weyve_room_list_joinable(weyve.client, i);
        listing.passworded = weyve_room_list_passworded(weyve.client, i);
        uint32_t valueLen = 0;
        const char* game = weyve_room_list_listing(weyve.client, i, "game", &valueLen);
        listing.game = bytes(game, valueLen);
        valueLen = 0;
        const char* host = weyve_room_list_listing(weyve.client, i, "host", &valueLen);
        listing.host = bytes(host, valueLen);
        valueLen = 0;
        const char* running = weyve_room_list_listing(weyve.client, i, "running", &valueLen);
        listing.statusKnown = valueLen > 0;
        listing.running = bytes(running, valueLen) == "1";
        weyve.roomList.push_back(std::move(listing));
      }
      break;
    }
    default:
      break;
    }
  }

  if(!weyveInRoom()) {
    // browsing without joining still costs the server a connection slot
    if(SDL_GetTicks() - weyve.idleSince >= Weyve::IdleTimeoutMs) {
      weyveDisconnect();
      weyve.lastError = "Disconnected after idling in the room browser";
    }
    return;
  }
  weyve.idleSince = SDL_GetTicks();

  if(weyve.rolesDirty && weyve_is_host(weyve.client)) {
    weyve.rolesDirty = false;
    weyveAutoAssignRoles();
  }

  if(weyve_is_host(weyve.client)) {
    uint32_t nameLen = 0;
    const char* nameBytes = weyve_name(weyve.client, &nameLen);
    const std::string name = bytes(nameBytes, nameLen);
    uint32_t listingLen = 0;
    const char* hostBytes = weyve_room_listing(weyve.client, "host", &listingLen);
    std::string listedHost = bytes(hostBytes, listingLen);
    if(!name.empty() && listedHost != name) weyve_set_room_listing(weyve.client, "host", name.c_str());

    std::string game = weyveRoomData("game");
    if(game.empty() && !games.empty()) {
      weyveSelectGame(0);
    } else if(!game.empty()) {
      listingLen = 0;
      const char* gameBytes = weyve_room_listing(weyve.client, "game", &listingLen);
      std::string listedGame = bytes(gameBytes, listingLen);
      if(listedGame != game) weyve_set_room_listing(weyve.client, "game", game.c_str());
    }
  }

  const std::string targetHash = weyveRoomData("game_hash");
  if(targetHash != weyve.lastGameHash) {
    weyve.lastGameHash = targetHash;
    const bool hasGame = !weyveHasGame(targetHash).empty();
    weyve_set_member_data(weyve.client, "hasGame", hasGame ? "1" : "0");
  }
  const std::string rollbackFloor = weyveRoomData("rollback_min");
  const std::string delayFloor = weyveRoomData("delay_min");
  const std::string spectatorDelay = weyveRoomData("spectator_delay");
  if(!rollbackFloor.empty()) weyve.rollbackBaseline = SDL_atoi(rollbackFloor.c_str());
  if(!delayFloor.empty()) weyve.delayBaseline = SDL_atoi(delayFloor.c_str());
  if(!spectatorDelay.empty()) weyve.spectatorDelay = SDL_atoi(spectatorDelay.c_str());
  if(weyve.localRollback < weyve.rollbackBaseline) {
    weyveSetLocalRollback(weyve.localRollback);
  }
  if(weyve.localDelay < weyve.delayBaseline) {
    weyveSetLocalDelay(weyve.localDelay);
  }
  weyve_set_member_data(weyve.client, "rollback", std::to_string(weyve.localRollback).c_str());
  weyve_set_member_data(weyve.client, "delay", std::to_string(weyve.localDelay).c_str());

  const uint32_t startToken = (uint32_t)SDL_atoi(weyveRoomData("start_token").c_str());
  const uint32_t stopToken = (uint32_t)SDL_atoi(weyveRoomData("stop_token").c_str());
  if(startToken != weyve.lastStartToken) {
    weyve.lastStartToken = startToken;
    if(startToken > stopToken && !netplayActive()) {
      weyveLog("Host started the session");
      netplayStartWeyve();
    }
  }
  if(stopToken != weyve.lastStopToken) {
    weyve.lastStopToken = stopToken;
    if(stopToken >= startToken && weyveSessionActive()) {
      netplayStop();
      pendingNetplayUnload = true;
      weyveLog("Host ended the session");
    }
  }
  weyveSyncSpectators();
}

void App::netplayStartWeyve() {
  if(netplayActive() || !weyve.client) return;
  if(movieActive()) { showMessage("stop the movie before starting netplay"); return; }

  const std::string targetHash = weyveRoomData("game_hash");
  if(targetHash.empty()) return;
  if(weyveGameFingerprint() != targetHash) {
    const std::string path = weyveHasGame(targetHash);
    if(!path.empty()) loadRom(path);
    if(weyveGameFingerprint() != targetHash) {
      showMessage("Cannot start: you don't have this game");
      return;
    }
  }

  const std::vector<uint32_t> players = weyveSessionPlayerOrder();
  const int numPlayers = (int)players.size();
  if(numPlayers < 1) return;

  const uint32_t selfId = weyve_id(weyve.client);
  int local = -1;
  for(int i = 0; i < numPlayers; i++) if(players[i] == selfId) local = i;
  const bool spectating = local < 0;

  uint32_t count = 0;
  const uint32_t* members = weyve_members(weyve.client, &count);
  std::vector<uint32_t> spectatorIds;
  for(uint32_t i = 0; i < count; i++) {
    if(weyveRoleOf(members[i]) == "spec") spectatorIds.push_back(members[i]);
  }
  std::sort(spectatorIds.begin(), spectatorIds.end());

  uint32_t roomLen = 0;
  const char* roomId = weyve_room_id(weyve.client, &roomLen);
  netplay.transport = Netplay::Weyvelength;
  netplay.instance = "w" + bytes(roomId, roomLen);
  const bool detectDesyncs = weyveRoomData("desync") == "1";

  int localSpectators = 0;
  if(!spectating) {
    for(uint32_t spectatorId : spectatorIds) {
      if(weyveSpectatorRelay(spectatorId, players) == local) localSpectators++;
    }
  }

  gWeyveApp = this;
  const std::string spectatorDelayText = weyveRoomData("spectator_delay");
  const int spectatorDelay = spectatorDelayText.empty() ? settings.netplaySpectatorDelay
                                                        : SDL_atoi(spectatorDelayText.c_str());
  netplayBeginSession(numPlayers, detectDesyncs,
                      spectating ? 0 : Weyve::SpectatorCap, spectating, spectatorDelay,
                      weyve.localRollback, weyve.localDelay);
  gekko_net_adapter_set(netplay.session, &weyveAdapter);

  netplay.peers.clear();
  if(spectating) {
    NetplayPeer peer;
    peer.type = GekkoRemotePlayer;
    const int relay = weyveSpectatorRelay(selfId, players);
    if(relay < 0) {
      netplayStop();
      showMessage("Cannot spectate: no players remain in the session");
      return;
    }
    peer.weyveId = players[relay];
    GekkoNetAddress addr{&peer.weyveId, sizeof(peer.weyveId)};
    peer.id = gekko_add_actor(netplay.session, GekkoRemotePlayer, &addr);
    netplay.peers.push_back(peer);
    netplay.localPlayer = -1;
    netplay.localActorId = 0;
    netplay.mode = Netplay::Running;
    netplayLog("weyvelength: room " + netplay.instance + " spectating");
    return;
  }

  for(int i = 0; i < numPlayers; i++) {
    NetplayPeer peer;
    if(i == local) {
      peer.type = GekkoLocalPlayer;
      peer.id = gekko_add_actor(netplay.session, GekkoLocalPlayer, nullptr);
      netplay.localPlayer = i;
      netplay.localActorId = peer.id;
    } else {
      peer.type = GekkoRemotePlayer;
      peer.weyveId = players[i];
      GekkoNetAddress addr{&peer.weyveId, sizeof(peer.weyveId)};
      peer.id = gekko_add_actor(netplay.session, GekkoRemotePlayer, &addr);
    }
    netplay.peers.push_back(peer);
  }
  netplaySetLocalDelay(weyve.localDelay);

  for(uint32_t spectatorId : spectatorIds) {
    if(weyveSpectatorRelay(spectatorId, players) != local) continue;
    NetplayPeer peer;
    peer.type = GekkoSpectator;
    peer.weyveId = spectatorId;
    GekkoNetAddress addr{&peer.weyveId, sizeof(peer.weyveId)};
    peer.id = gekko_add_actor(netplay.session, GekkoSpectator, &addr);
    netplay.peers.push_back(peer);
  }

  netplay.mode = Netplay::Running;
  netplayLog("weyvelength: room " + netplay.instance + " local id " + std::to_string(selfId)
           + " spectators " + std::to_string(localSpectators));
}

void App::weyveSyncSpectators() {
  if(!weyveSessionActive() || netplay.localPlayer < 0 || !netplay.session) return;

  const std::vector<uint32_t> players = weyveSessionPlayerOrder();
  const int numPlayers = (int)players.size();
  if(numPlayers <= 0 || netplay.localPlayer >= numPlayers) return;

  uint32_t count = 0;
  const uint32_t* members = weyve_members(weyve.client, &count);
  for(uint32_t i = 0; i < count; i++) {
    const uint32_t memberId = members[i];
    if(weyveRoleOf(memberId) != "spec"
       || weyveSpectatorRelay(memberId, players) != netplay.localPlayer) continue;

    const bool exists = std::any_of(netplay.peers.begin(), netplay.peers.end(),
      [memberId](const NetplayPeer& peer) {
        return peer.type == GekkoSpectator && peer.weyveId == memberId;
      });
    if(exists) continue;

    NetplayPeer peer;
    peer.type = GekkoSpectator;
    peer.weyveId = memberId;
    GekkoNetAddress addr{&peer.weyveId, sizeof(peer.weyveId)};
    peer.id = gekko_add_actor(netplay.session, GekkoSpectator, &addr);
    if(peer.id < 0) {
      netplayLog("could not add late spectator " + std::to_string(memberId));
      continue;
    }
    netplay.peers.push_back(peer);
    netplayLog("added late spectator " + std::to_string(memberId));
  }
}

void App::weyveRemoveNetplayPeer(uint32_t memberId) {
  if(!weyveSessionActive() || !netplay.session) return;
  bool restartSpectator = false;
  for(auto peer = netplay.peers.begin(); peer != netplay.peers.end();) {
    if(peer->weyveId != memberId || peer->type == GekkoLocalPlayer) {
      ++peer;
      continue;
    }
    restartSpectator |= netplay.localPlayer < 0 && peer->type == GekkoRemotePlayer;
    gekko_disconnect_actor(netplay.session, peer->id);
    netplayLog("removed room peer " + std::to_string(memberId));
    peer = netplay.peers.erase(peer);
  }
  if(restartSpectator) {
    netplayStop();
    netplayStartWeyve();
  }
}

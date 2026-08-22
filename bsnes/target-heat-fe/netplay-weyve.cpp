#include "app.hpp"

#include <algorithm>
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
  if(weyve.client) weyveDisconnect();

  weyve.lastError.clear();
  weyve.idleSince = SDL_GetTicks();
  weyve.client = weyve_client_create();
  if(!weyve_connect(weyve.client, host.c_str(), port)) {
    weyve_client_destroy(weyve.client);
    weyve.client = nullptr;
    weyve.lastError = "could not connect";
    return false;
  }
  weyve_set_name(weyve.client, settings.weyveNickname.c_str());
  weyve.log.clear();
  weyve.lastStartToken = 0;
  weyve.lastStopToken = 0;
  return true;
}

void App::weyveDisconnect() {
  if(!weyve.client) return;
  if(weyveSessionActive()) netplayStop();
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
  weyve_create_room(weyve.client);
}

void App::weyveJoinRoom(const std::string& id, const std::string& password) {
  if(!weyve.client) return;
  weyve.idleSince = SDL_GetTicks();
  weyve.lastError.clear();
  weyve_join_room(weyve.client, id.c_str(), password.empty() ? nullptr : password.c_str());
}

void App::weyveResetRoomState() {
  weyve.lastGameHash.clear();
  weyve.lastStartToken = 0;
  weyve.lastStopToken = 0;
  weyve.log.clear();
  weyve.rolesDirty = true;
}

void App::weyveLeaveRoom() {
  if(!weyve.client) return;
  if(weyve_is_host(weyve.client)) weyveStopGame();
  if(netplayActive() && netplay.transport == Netplay::Weyvelength) netplayStop();
  weyve_leave_room(weyve.client);
  weyveResetRoomState();
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

void App::weyveAutoAssignRoles() {
  if(!weyve.client || !weyve_is_host(weyve.client)) return;
  uint32_t count = 0;
  const uint32_t* members = weyve_members(weyve.client, &count);
  std::vector<uint32_t> players = weyvePlayerOrder();

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
      uint32_t nameLen = 0;
      const char* name = weyve_peer_name(weyve.client, members[i], &nameLen);
      return bytes(name, nameLen) + " doesn't have this game yet";
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
                      std::to_string(settings.netplaySpectatorDelay).c_str());
  const uint32_t token = (uint32_t)SDL_atoi(weyveRoomData("start_token").c_str()) + 1;
  weyve_set_room_data(weyve.client, "start_token", std::to_string(token).c_str());
  weyve_set_room_joinable(weyve.client, false);
}

void App::weyveStopGame() {
  if(!weyve.client || !weyve_is_host(weyve.client)) return;
  const uint32_t token = (uint32_t)SDL_atoi(weyveRoomData("stop_token").c_str()) + 1;
  weyve_set_room_data(weyve.client, "stop_token", std::to_string(token).c_str());
  weyve_set_room_joinable(weyve.client, true);
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
}

void App::weyveListRooms() {
  if(!weyve.client) return;
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
  if(!weyve.client) return;

  if(!weyve_poll(weyve.client)) {
    if(weyveSessionActive()) netplayStop();
    weyve_client_destroy(weyve.client);
    weyve.client = nullptr;
    weyve.lastError = "The server closed the connection";
    return;
  }

  WeyveEvent event;
  while(weyve_next(weyve.client, &event)) {
    switch(event.type) {
    case WEYVE_EVENT_ROOM_ID_ASSIGNED:
      weyve.rolesDirty = true;
      weyveResetRoomState();
      weyveLog("You joined the room");
      weyvePublishHostListing();  // a no-op unless we're the (new room's) host
      if(weyve.pendingListed) {
        weyve.pendingListed = false;
        weyve_set_room_listed(weyve.client, true);
      }
      break;
    case WEYVE_EVENT_ROOM_ERROR:
      weyve.lastError = weyveRoomErrorText(event.data.room_error.code);
      weyveLog(weyve.lastError);
      break;
    case WEYVE_EVENT_CHAT: {
      uint32_t nameLen = 0;
      const char* name = weyve_peer_name(weyve.client, event.data.chat.from, &nameLen);
      weyveLog(bytes(name, nameLen) + ": " + bytes(event.data.chat.text, event.data.chat.text_len));
      break;
    }
    case WEYVE_EVENT_PEER_JOINED:
      weyve.rolesDirty = true;
      weyveLog(bytes(event.data.peer_joined.name, event.data.peer_joined.name_len) + " joined");
      break;
    case WEYVE_EVENT_PEER_LEFT: {
      weyve.rolesDirty = true;
      uint32_t nameLen = 0;
      const char* name = weyve_peer_name(weyve.client, event.data.peer_left.id, &nameLen);
      weyveLog(bytes(name, nameLen) + " left");
      break;
    }
    case WEYVE_EVENT_HOST_CHANGED: {
      weyve.rolesDirty = true;
      uint32_t nameLen = 0;
      const char* name = weyve_peer_name(weyve.client, event.data.host_changed.id, &nameLen);
      weyveLog(bytes(name, nameLen) + " is now the host");
      if(event.data.host_changed.id == weyve_id(weyve.client)) weyvePublishHostListing();
      break;
    }
    case WEYVE_EVENT_ROOM_DATA_CHANGED: {
      const std::string key = bytes(event.data.room_data.key, event.data.room_data.key_len);
      const std::string value = bytes(event.data.room_data.value, event.data.room_data.value_len);
      if(key == "game") weyveLog("Game: " + value);
      else if(key.rfind("role:", 0) == 0) weyve.rolesDirty = true;
      break;
    }
    case WEYVE_EVENT_MEMBER_DATA_CHANGED: {
      const std::string key = bytes(event.data.member_data.key, event.data.member_data.key_len);
      if(event.data.member_data.id == weyve_id(weyve.client)) break;  // our own echo
      if(key == "hasGame") {
        const std::string value = bytes(event.data.member_data.value, event.data.member_data.value_len);
        uint32_t nameLen = 0;
        const char* name = weyve_peer_name(weyve.client, event.data.member_data.id, &nameLen);
        weyveLog(bytes(name, nameLen) + (value == "1" ? " has this game" : " doesn't have this game"));
      }
      break;
    }
    case WEYVE_EVENT_KICKED:
      if(weyveSessionActive()) netplayStop();
      weyveResetRoomState();
      weyveLog("You were kicked from the room");
      break;
    case WEYVE_EVENT_BANNED:
      if(weyveSessionActive()) netplayStop();
      weyveResetRoomState();
      weyveLog("You were banned from the room");
      break;
    case WEYVE_EVENT_ROOM_ACCESS_CHANGED:
      weyveLog(event.data.room_access.open ? "Room opened" : "Room closed");
      break;
    case WEYVE_EVENT_ROOM_LISTED_CHANGED:
      weyveLog(event.data.room_listed.listed ? "Room is now public" : "Room is now private");
      break;
    case WEYVE_EVENT_ROOM_LIST: {
      // copied out immediately: the client's storage only lasts until the next weyve_poll
      weyve.roomList.clear();
      const uint32_t count = weyve_room_list_count(weyve.client);
      for(uint32_t i = 0; i < count; i++) {
        WeyveRoomListing listing;
        uint32_t idLen = 0;
        listing.id = bytes(weyve_room_list_id(weyve.client, i, &idLen), idLen);
        listing.members = weyve_room_list_members(weyve.client, i);
        listing.passworded = weyve_room_list_passworded(weyve.client, i);
        uint32_t valueLen = 0;
        listing.game = bytes(weyve_room_list_listing(weyve.client, i, "game", &valueLen), valueLen);
        valueLen = 0;
        listing.host = bytes(weyve_room_list_listing(weyve.client, i, "host", &valueLen), valueLen);
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

  const std::string targetHash = weyveRoomData("game_hash");
  if(targetHash != weyve.lastGameHash) {
    weyve.lastGameHash = targetHash;
    const bool hasGame = !weyveHasGame(targetHash).empty();
    weyve_set_member_data(weyve.client, "hasGame", hasGame ? "1" : "0");
  }
  weyve_set_member_data(weyve.client, "rollback", std::to_string(settings.netplayRollback).c_str());
  weyve_set_member_data(weyve.client, "delay", std::to_string(settings.netplayDelay).c_str());

  const uint32_t startToken = (uint32_t)SDL_atoi(weyveRoomData("start_token").c_str());
  if(startToken != weyve.lastStartToken) {
    weyve.lastStartToken = startToken;
    if(!netplayActive()) { weyveLog("Host started the session"); netplayStartWeyve(); }
  }
  const uint32_t stopToken = (uint32_t)SDL_atoi(weyveRoomData("stop_token").c_str());
  if(stopToken != weyve.lastStopToken) {
    weyve.lastStopToken = stopToken;
    if(weyveSessionActive()) { netplayStop(); weyveLog("Host ended the session"); }
  }
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

  const std::vector<uint32_t> players = weyvePlayerOrder();
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

  // spectators round-robin across players so no single one relays the whole room
  int localSpectators = 0;
  if(!spectating) {
    for(size_t s = 0; s < spectatorIds.size(); s++) if((int)(s % numPlayers) == local) localSpectators++;
  }

  gWeyveApp = this;
  const std::string spectatorDelayText = weyveRoomData("spectator_delay");
  const int spectatorDelay = spectatorDelayText.empty() ? settings.netplaySpectatorDelay
                                                        : SDL_atoi(spectatorDelayText.c_str());
  netplayBeginSession(numPlayers, detectDesyncs, localSpectators, spectating, spectatorDelay);
  gekko_net_adapter_set(netplay.session, &weyveAdapter);

  netplay.peers.clear();
  if(spectating) {
    // one remote player carries us; round-robin by our own rank among spectators
    size_t rank = 0;
    for(size_t s = 0; s < spectatorIds.size(); s++) if(spectatorIds[s] == selfId) rank = s;
    NetplayPeer peer;
    peer.type = GekkoRemotePlayer;
    peer.weyveId = players[rank % players.size()];
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
  netplaySetLocalDelay(settings.netplayDelay);

  for(size_t s = 0; s < spectatorIds.size(); s++) {
    if((int)(s % numPlayers) != local) continue;
    NetplayPeer peer;
    peer.type = GekkoSpectator;
    peer.weyveId = spectatorIds[s];
    GekkoNetAddress addr{&peer.weyveId, sizeof(peer.weyveId)};
    peer.id = gekko_add_actor(netplay.session, GekkoSpectator, &addr);
    netplay.peers.push_back(peer);
  }

  netplay.mode = Netplay::Running;
  netplayLog("weyvelength: room " + netplay.instance + " local id " + std::to_string(selfId)
           + " spectators " + std::to_string(localSpectators));
}

static auto weyveAdapterSend(GekkoNetAddress* addr, const char* data, int length) -> void {
    uint32_t id = 0;
    memcpy(&id, addr->data, sizeof(id));
    weyve_send_p2p(program.weyve.client, id, data, (uint32_t)length);
}

static vector<GekkoNetResult*> weyveResults;

static auto weyveAdapterReceive(int* length) -> GekkoNetResult** {
    weyveResults.reset();

    uint32_t from = 0, len = 0;
    while(const uint8_t* data = weyve_next_p2p(program.weyve.client, &from, &len)) {
        auto* result = (GekkoNetResult*)malloc(sizeof(GekkoNetResult));
        result->addr.data = malloc(sizeof(uint32_t));
        memcpy(result->addr.data, &from, sizeof(uint32_t));
        result->addr.size = sizeof(uint32_t);
        result->data_len = len;
        result->data = malloc(len);
        memcpy(result->data, data, len);
        weyveResults.append(result);
    }

    *length = (int)weyveResults.size();
    return weyveResults.data();
}

static auto weyveAdapterFree(void* dataPtr) -> void {
    free(dataPtr);
}

static GekkoNetAdapter weyveAdapter{weyveAdapterSend, weyveAdapterReceive, weyveAdapterFree};

auto Program::weyveSessionActive() -> bool {
    return netplay.mode != Netplay::Mode::Inactive && netplay.transport == Netplay::Transport::Weyvelength;
}

auto Program::weyveConnected() -> bool {
    return weyve.client != nullptr;
}

static auto weyveRoomErrorText(uint code) -> string {
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
    return {"Room error ", code};
}

auto Program::weyveConnect(string host, uint16 port) -> bool {
    if(weyve.client) weyveDisconnect();

    weyve.lastError = "";
    weyve.idleSince = chrono::timestamp();
    weyve.client = weyve_client_create();
    if(!weyve_connect(weyve.client, host.data(), port)) {
        weyve_client_destroy(weyve.client);
        weyve.client = nullptr;
        return false;
    }

    weyve.log.reset();
    weyve.lastStartToken = 0;
    weyve.lastStopToken = 0;
    return true;
}

auto Program::weyveDisconnect() -> void {
    if(!weyve.client) return;
    if(weyveSessionActive()) netplayStop();
    weyve_client_destroy(weyve.client);
    weyve.client = nullptr;
}

auto Program::weyveCreateRoom(bool listed) -> void {
    if(!weyve.client) return;
    weyveMarkActive();
    weyve.pendingListed = listed;
    weyve_create_room(weyve.client);
}

auto Program::weyveListRooms() -> void {
    if(!weyve.client) return;
    weyveMarkActive();
    weyve_list_rooms(weyve.client, nullptr, 0);
}

// member data only travels when it changes; the cache is per room
auto Program::weyvePublish(const string& key, const string& value) -> void {
    for(auto& entry : weyve.published) {
        if(entry.key != key) continue;
        if(entry.value == value) return;
        entry.value = value;
        weyve_set_member_data(weyve.client, key.data(), value.data());
        return;
    }
    weyve.published.append({key, value});
    weyve_set_member_data(weyve.client, key.data(), value.data());
}

auto Program::weyveMarkActive() -> void {
    weyve.idleSince = chrono::timestamp();
}

auto Program::weyveJoinRoom(string id, string password) -> void {
    if(!weyve.client) return;
    weyveMarkActive();
    weyve.lastError = "";
    weyve_join_room(weyve.client, id.data(), password.data());
}

auto Program::weyveResetRoomState() -> void {
    weyve.published.reset();  // member data is per room
    weyve.lastGameHash = "";
    weyve.lastStartToken = 0;
    weyve.lastStopToken = 0;
    weyve.pendingIdentityLogs.reset();
    weyve.knownNames.reset();
    weyve.log.reset();
    weyve.logEpoch++;  // a line count alone can't prove the feed changed
}

auto Program::weyveLeaveRoom() -> void {
    if(!weyve.client) return;

    if(weyve_is_host(weyve.client)) weyveStopGame();
    if(weyveSessionActive()) netplayStop();

    weyve_leave_room(weyve.client);
    weyveResetRoomState();
}

// slots stay contiguous, so the index is also the GekkoNet handle
auto Program::weyvePlayerOrder() -> vector<uint32> {
    struct Entry { uint slot; uint32 id; };
    vector<Entry> entries;
    vector<uint32> players;
    if(!weyve.client) return players;

    uint32 count = 0;
    const uint32* members = weyve_members(weyve.client, &count);
    for(uint32 i = 0; i < count; i++) {
        string role = weyveRoleOf(members[i]);
        if(role == "spec") continue;
        entries.append({(uint)role.natural(), members[i]});
    }
    entries.sort([](const Entry& a, const Entry& b) { return a.slot < b.slot; });
    for(auto& entry : entries) players.append(entry.id);
    return players;
}

// host-only: keeps slot N on port N after departures
auto Program::weyveCompactRoles(const vector<uint32>& players) -> void {
    for(uint i = 0; i < players.size(); i++) {
        if(weyveRoleOf(players[i]).natural() == i) continue;
        string key = {"role:", players[i]};
        weyve_set_room_data(weyve.client, key.data(), string{i}.data());
    }
}

// host-only
auto Program::weyveAutoAssignRoles() -> void {
    uint32 count = 0;
    const uint32* members = weyve_members(weyve.client, &count);
    auto players = weyvePlayerOrder();
    uint assigned = players.size();

    for(uint32 i = 0; i < count; i++) {
        if(weyveRoomDataString({"role:", members[i]})) continue;

        string key = {"role:", members[i]};
        bool play = assigned < Weyve::PlayerCap;
        weyve_set_room_data(weyve.client, key.data(), play ? string{assigned}.data() : "spec");
        if(play) players.append(members[i]), assigned++;
    }

    weyveCompactRoles(players);
}

auto Program::weyveSetRole(uint32 memberId, string role) -> void {
    if(!weyve.client || !weyve_is_host(weyve.client)) return;

    if(role != "spec") {
        // slots are unique, so this swaps the two members
        uint32 count = 0;
        const uint32* members = weyve_members(weyve.client, &count);
        for(uint32 i = 0; i < count; i++) {
            if(members[i] == memberId) continue;
            if(weyveRoleOf(members[i]) != role) continue;
            string previousRole = weyveRoleOf(memberId);
            string otherKey = {"role:", members[i]};
            weyve_set_room_data(weyve.client, otherKey.data(), previousRole.data());
            break;
        }
    }

    string key = {"role:", memberId};
    weyve_set_room_data(weyve.client, key.data(), role.data());
}

auto Program::weyveSetBaseline(uint8 rollback, uint8 delay) -> void {
    if(!weyve.client || !weyve_is_host(weyve.client)) return;
    weyve_set_room_data(weyve.client, "rollback_min", string{rollback}.data());
    weyve_set_room_data(weyve.client, "delay_min", string{delay}.data());
}

auto Program::weyveKick(uint32 memberId) -> void {
    if(!weyve.client || !weyve_is_host(weyve.client)) return;
    weyve_kick_member(weyve.client, memberId);
}

auto Program::weyveTransferHost(uint32 memberId) -> void {
    if(!weyve.client || !weyve_is_host(weyve.client)) return;
    if(memberId == weyve_id(weyve.client)) return;
    weyve_transfer_host(weyve.client, memberId);
}

auto Program::weyveCopyRoomCode() -> void {
    if(!weyve.client) return;
    uint32 len = 0;
    const char* id = weyve_room_id(weyve.client, &len);
    if(!len) return;
    string code = {string_view{id, len}};

#if defined(PLATFORM_WINDOWS)
    if(OpenClipboard(nullptr)) {
        EmptyClipboard();
        auto wide = utf16_t(code);
        uint bytes = (wcslen(wide) + 1) * sizeof(wchar_t);
        if(HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            memcpy(GlobalLock(handle), (const wchar_t*)wide, bytes);
            GlobalUnlock(handle);
            SetClipboardData(CF_UNICODETEXT, handle);
        }
        CloseClipboard();
    }
#endif

    weyveLog({"Copied room code ", code});
}

auto Program::weyveRoomDataString(const string& key) -> string {
    if(!weyve.client) return "";
    uint32 len = 0;
    const char* val = weyve_room_data(weyve.client, key.data(), &len);
    return val && len ? string{string_view{val, len}} : string{};
}

auto Program::weyveMemberDataString(uint32 memberId, const string& key) -> string {
    if(!weyve.client) return "";
    uint32 len = 0;
    const char* val = weyve_member_data(weyve.client, memberId, key.data(), &len);
    return val && len ? string{string_view{val, len}} : string{};
}

auto Program::weyveRoleOf(uint32 memberId) -> string {
    auto role = weyveRoomDataString({"role:", memberId});
    return role ? role : string{"spec"};
}

auto Program::weyveRoleLabel(const string& role) -> string {
    return role == "spec" ? string{"Spectator"} : string{"Player ", role.natural() + 1};
}

// raw file bytes, so both hash paths agree
static auto weyveHashFile(const string& path) -> string {
    auto data = file::read(path);
    if(!data) return "";
    return nall::Hash::CRC32(data).digest();
}

// remembered so a member can still be named after they leave
auto Program::weyveRememberName(uint32 id, const string& nickname) -> void {
    for(auto& known : weyve.knownNames) {
        if(known.id != id) continue;
        known.nickname = nickname;
        return;
    }
    weyve.knownNames.append({id, nickname});
}

auto Program::weyveNicknameOf(uint32 id) -> string {
    auto nickname = weyveMemberDataString(id, "nickname");
    for(auto& known : weyve.knownNames) {
        if(known.id != id) continue;
        if(nickname) known.nickname = nickname;
        return known.nickname;
    }
    if(!nickname) return {"Player #", id};
    weyve.knownNames.append({id, nickname});
    return nickname;
}

auto Program::weyveGameFingerprint() -> string {
    if(!superFamicom) return "";
    return weyveHashFile(superFamicom.location);
}

auto Program::weyveScanLibrary() -> void {
    weyve.library.reset();
    weyve.libraryScanned = true;
    weyve.lastGamesFolder = settings.weyvelength.gamesFolder;
    if(!settings.weyvelength.gamesFolder) return;

    string rootFolder = settings.weyvelength.gamesFolder;
    if(!rootFolder.endsWith("/")) rootFolder.append("/");
    for(auto& pattern : {"*.sfc", "*.smc"}) {
        for(auto& name : directory::rfiles(rootFolder, pattern)) {
            string path = {rootFolder, name};
            weyve.library.append({Location::prefix(Location::file(name)), path, weyveHashFile(path)});
        }
    }
    weyve.library.sort([](const Weyve::GameEntry& a, const Weyve::GameEntry& b) {
        return string::icompare(a.title, b.title) < 0;
    });
}

auto Program::weyveLibraryStale() -> bool {
    return !weyve.libraryScanned || settings.weyvelength.gamesFolder != weyve.lastGamesFolder;
}

auto Program::weyveHasGame(const string& hash) -> maybe<string> {
    if(!hash) return nothing;
    if(weyveLibraryStale()) weyveScanLibrary();
    for(auto& entry : weyve.library) {
        if(entry.hash == hash) return entry.path;
    }
    return nothing;
}

auto Program::weyveSelectGame(uint index) -> void {
    if(!weyve.client || !weyve_is_host(weyve.client)) return;
    if(index >= weyve.library.size()) return;
    auto& entry = weyve.library[index];
    weyve_set_room_data(weyve.client, "game", entry.title.data());
    weyve_set_room_data(weyve.client, "game_hash", entry.hash.data());
    weyve_set_room_listing(weyve.client, "game", entry.title.data());
}

// empty if clear to start, else why not
auto Program::weyveStartBlockedReason() -> string {
    if(!weyve.client || !weyve_is_host(weyve.client)) return "";

    string targetHash = weyveRoomDataString("game_hash");
    if(!targetHash) return "pick a game first";

    uint32 count = 0;
    const uint32* members = weyve_members(weyve.client, &count);
    for(uint32 i = 0; i < count; i++) {
        if(weyveMemberDataString(members[i], "hasGame") != "1") {
            return {weyveNicknameOf(members[i]), " doesn't have this game yet"};
        }
    }
    if(weyvePlayerOrder().size() < 1) return "need at least 1 player";
    return "";
}

auto Program::weyveStartGame() -> void {
    if(!weyve.client || !weyve_is_host(weyve.client)) return;

    string reason = weyveStartBlockedReason();
    if(reason) {
        showMessage({"Cannot start: ", reason});
        return;
    }

    uint32 token = weyveRoomDataString("start_token").natural() + 1;
    weyve_set_room_data(weyve.client, "start_token", string{token}.data());
    weyve_set_room_joinable(weyve.client, false);
}

auto Program::weyveSetLocalRollback(uint8 value) -> void {
    uint8 floor = (uint8)weyveRoomDataString("rollback_min").natural();
    weyve.localRollback = max(floor, value);
}

auto Program::weyveSetLocalDelay(uint8 value) -> void {
    uint8 floor = (uint8)weyveRoomDataString("delay_min").natural();
    weyve.localDelay = max(floor, value);
    if(!weyveSessionActive()) return;
    for(auto& peer : netplay.peers) {
        if(peer.type != GekkoLocalPlayer) continue;
        gekko_set_local_delay(netplay.session, peer.id, weyve.localDelay);
        break;
    }
}

auto Program::weyveStopGame() -> void {
    if(!weyve.client || !weyve_is_host(weyve.client)) return;

    uint32 token = weyveRoomDataString("stop_token").natural() + 1;
    weyve_set_room_data(weyve.client, "stop_token", string{token}.data());
    weyve_set_room_joinable(weyve.client, true);
}

auto Program::weyveLog(string line) -> void {
    weyve.log.append(line);
    if(weyve.log.size() > Weyve::LogLimit) weyve.log.removeLeft();
}

auto Program::weyveLogIdentity(uint32 id, string suffix) -> void {
    if(weyveMemberDataString(id, "nickname")) {
        weyveLog({weyveNicknameOf(id), suffix});
    } else {
        weyve.pendingIdentityLogs.append({id, suffix, chrono::timestamp()});
    }
}

auto Program::weyvePoll() -> void {
    if(!weyve.client) return;

    if(!weyve_poll(weyve.client)) {
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
            if(weyve.pendingListed) {
                weyve.pendingListed = false;
                weyve_set_room_listed(weyve.client, true);
            }
            break;
        case WEYVE_EVENT_ROOM_ERROR:
            weyve.lastError = weyveRoomErrorText(event.data.room_error.code);
            weyveLog(weyve.lastError);
            break;
        case WEYVE_EVENT_CHAT:
            weyveLog({weyveNicknameOf(event.data.chat.from), ": ", string{string_view{event.data.chat.text, event.data.chat.text_len}}});
            break;
        case WEYVE_EVENT_PEER_JOINED:
            weyve.rolesDirty = true;
            weyveLogIdentity(event.data.peer_joined.id, " joined");
            break;
        case WEYVE_EVENT_PEER_LEFT:
            weyve.rolesDirty = true;
            weyveLog({weyveNicknameOf(event.data.peer_left.id), " left"});
            break;
        case WEYVE_EVENT_HOST_CHANGED:
            weyve.rolesDirty = true;
            weyveLogIdentity(event.data.host_changed.id, " is now the host");
            if(event.data.host_changed.id == weyve_id(weyve.client)) {
                weyve_set_room_listing(weyve.client, "host", settings.weyvelength.nickname.data());
            }
            break;
        case WEYVE_EVENT_ROOM_DATA_CHANGED: {
            string key{string_view{event.data.room_data.key, event.data.room_data.key_len}};
            string value{string_view{event.data.room_data.value, event.data.room_data.value_len}};
            if(key == "rollback_min") weyveLog({"Rollback baseline: ", value});
            else if(key == "delay_min") weyveLog({"Delay baseline: ", value});
            else if(key == "game") weyveLog({"Game: ", value});
            else if(key.beginsWith("role:")) {
                weyve.rolesDirty = true;
                weyveLogIdentity(key.slice(5).natural(), {" is now ", weyveRoleLabel(value)});
            }
            break;
        }
        case WEYVE_EVENT_MEMBER_DATA_CHANGED: {
            string key{string_view{event.data.member_data.key, event.data.member_data.key_len}};
            string value{string_view{event.data.member_data.value, event.data.member_data.value_len}};
            if(key == "nickname") {
                weyveRememberName(event.data.member_data.id, value);
                for(uint i = 0; i < weyve.pendingIdentityLogs.size();) {
                    if(weyve.pendingIdentityLogs[i].id == event.data.member_data.id) {
                        weyveLog({value, weyve.pendingIdentityLogs[i].suffix});
                        weyve.pendingIdentityLogs.remove(i);
                    } else i++;
                }
            }
            if(event.data.member_data.id == weyve_id(weyve.client)) break;  // our own echo
            if(key == "hasGame") {
                weyveLog({weyveNicknameOf(event.data.member_data.id), value == "1" ? " has this game" : " doesn't have this game"});
            } else if(key == "rollback") {
                weyveLog({weyveNicknameOf(event.data.member_data.id), " set their rollback to ", value});
            } else if(key == "delay") {
                weyveLog({weyveNicknameOf(event.data.member_data.id), " set their delay to ", value});
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
        default:
            break;
        }
    }

    if(weyve.pendingIdentityLogs) {
        uint64 now = chrono::timestamp();
        for(uint i = 0; i < weyve.pendingIdentityLogs.size();) {
            auto& pending = weyve.pendingIdentityLogs[i];
            if(now - pending.queuedAt >= 2) {
                weyveLog({weyveNicknameOf(pending.id), pending.suffix});
                weyve.pendingIdentityLogs.remove(i);
            } else i++;
        }
    }

    uint32 idLen = 0;
    weyve_room_id(weyve.client, &idLen);

    // browsing without joining anything just costs the server a slot; a room keeps us
    if(!idLen) {
        if(chrono::timestamp() - weyve.idleSince >= Weyve::IdleTimeout) {
            weyveDisconnect();
            weyve.lastError = "Disconnected after idling in the room browser";
        }
        return;
    }
    weyveMarkActive();

    if(weyve.rolesDirty && weyve_is_host(weyve.client)) {
        weyve.rolesDirty = false;
        weyveAutoAssignRoles();
    }

    weyvePublish("nickname", settings.weyvelength.nickname);

    string targetHash = weyveRoomDataString("game_hash");
    if(targetHash != weyve.lastGameHash || weyveLibraryStale()) {
        weyve.lastGameHash = targetHash;
        bool hasGame = (bool)weyveHasGame(targetHash);
        weyve_set_member_data(weyve.client, "hasGame", hasGame ? "1" : "0");
        if(targetHash && !hasGame) {
            weyveLog({"You don't have this game locally (looking for hash ", targetHash,
                ", scanned ", (uint)weyve.library.size(), " games in ", settings.weyvelength.gamesFolder, ")"});
        }
    }

    uint8 rollbackFloor = (uint8)weyveRoomDataString("rollback_min").natural();
    uint8 delayFloor = (uint8)weyveRoomDataString("delay_min").natural();
    if(weyve.localRollback < rollbackFloor) weyveSetLocalRollback(rollbackFloor);
    if(weyve.localDelay < delayFloor) weyveSetLocalDelay(delayFloor);
    weyvePublish("rollback", {weyve.localRollback});
    weyvePublish("delay", {weyve.localDelay});

    uint32 token = weyveRoomDataString("start_token").natural();
    if(token != weyve.lastStartToken) {
        weyve.lastStartToken = token;
        if(netplay.mode == Netplay::Mode::Inactive) {
            weyveLog("Host started the session");
            netplayStartWeyve();
        }
    }

    uint32 stopToken = weyveRoomDataString("stop_token").natural();
    if(stopToken != weyve.lastStopToken) {
        weyve.lastStopToken = stopToken;
        if(weyveSessionActive()) {
            netplayStop();
            pendingUnload = true;  // deferred; see Program::main()
            weyveLog("Host ended the session");
        }
    }
}

auto Program::netplayStartWeyve() -> void {
    if(netplay.mode != Netplay::Mode::Inactive) return;
    if(!weyve.client) return;

    string targetHash = weyveRoomDataString("game_hash");
    if(!targetHash || weyveGameFingerprint() != targetHash) {
        if(auto path = weyveHasGame(targetHash)) {
            gameQueue = {string{"Auto;", path()}};
            load();
        }
        if(weyveGameFingerprint() != targetHash) {  // load() above may have fixed it
            netplayReport("weyvelength: refusing to start, missing the room's game");
            showMessage("Cannot start: you don't have this game");
            return;
        }
    }

    uint32 count = 0;
    const uint32* members = weyve_members(weyve.client, &count);
    uint32 selfId = weyve_id(weyve.client);

    auto rolePlayers = weyvePlayerOrder();
    vector<uint32> spectatorIds;
    for(uint32 i = 0; i < count; i++) {
        if(weyveRoleOf(members[i]) == "spec") spectatorIds.append(members[i]);
    }
    // sort so every client derives the same assignment
    spectatorIds.sort([](uint32 a, uint32 b) { return a < b; });

    int numPlayers = rolePlayers.size();
    if(numPlayers < 1) return;

    uint8 local = 255;
    for(int i = 0; i < numPlayers; i++) if(rolePlayers[i] == selfId) local = i;
    bool isSpectating = local == 255;

    // round-robin over the players instead of piling onto player 0
    uint localSpectators = 0;
    uint spectatorSlot = 0;
    for(uint s = 0; s < spectatorIds.size(); s++) {
        if(spectatorIds[s] == selfId) spectatorSlot = s;
        if(!isSpectating && s % numPlayers == local) localSpectators++;
    }

    uint8 rollback = weyve.localRollback;
    uint8 delay = weyve.localDelay;

    uint32 roomIdLen = 0;
    const char* roomIdPtr = weyve_room_id(weyve.client, &roomIdLen);
    netplay.instance = {"w", string{string_view{roomIdPtr, roomIdLen}}};
    netplayReport({"weyvelength: room ", netplay.instance, " local id ", selfId});
    bool detectDesyncs = weyveRoomDataString("desync") == "1";
    uint spectatorDelay = weyveRoomDataString("spectator_delay").natural();
    if(!spectatorDelay) spectatorDelay = 5 * 60;
    netplay.transport = Netplay::Transport::Weyvelength;
    netplayBeginSession(numPlayers, rollback, delay, localSpectators, detectDesyncs, isSpectating, spectatorDelay);
    gekko_net_adapter_set(netplay.session, &weyveAdapter);

    auto addPeer = [&](GekkoPlayerType type, uint32 weyveId) {
        auto peer = Netplay::Peer();
        peer.type = type;
        peer.weyveId = weyveId;
        auto addr = GekkoNetAddress{&peer.weyveId, sizeof(peer.weyveId)};
        peer.id = gekko_add_actor(netplay.session, type, &addr);
        netplay.peers.append(peer);
    };

    if(!isSpectating) {
        for(int i = 0; i < numPlayers; i++) {
            if(i != local) { addPeer(GekkoRemotePlayer, rolePlayers[i]); continue; }
            auto peer = Netplay::Peer();
            peer.type = GekkoLocalPlayer;
            peer.id = gekko_add_actor(netplay.session, GekkoLocalPlayer, nullptr);
            netplay.peers.append(peer);
        }
        gekko_set_local_delay(netplay.session, local, delay);

        for(uint s = 0; s < spectatorIds.size(); s++) {
            if(s % numPlayers == local) addPeer(GekkoSpectator, spectatorIds[s]);
        }
    } else {
        // spectate sessions accept exactly one remote
        addPeer(GekkoRemotePlayer, rolePlayers[spectatorSlot % numPlayers]);
    }

    netplayMode(Netplay::Running);
}

auto WeyveWindow::memberRow(uint32_t id, bool roomHasGame, uint32_t hostId, uint32_t selfId) -> string {
    string label;
    if(id == hostId) label.append("[host] ");
    if(id == selfId) label.append("[you] ");
    label.append(program.weyveNicknameOf(id), " | ", program.weyveRoleLabel(program.weyveRoleOf(id)));

    if(roomHasGame && program.weyveMemberDataString(id, "hasGame") != "1") label.append(" | no game");

    return label;
}

auto WeyveWindow::selectedMemberId() -> maybe<uint32_t> {
    auto item = memberList.selected();
    if(!item) return nothing;
    uint32 count = 0;
    const uint32* members = weyve_members(program.weyve.client, &count);
    if(item.offset() >= count) return nothing;
    return members[item.offset()];
}

// snaps the field back when the value was clamped; empty means mid-edit
static auto weyveSyncField(LineEdit& edit, uint8 value) -> void {
    string text = edit.text().strip();
    if(text && text.natural() != value) edit.setText({value});
}

auto WeyveWindow::selectedRoomCode() -> string {
    if(string code = joinCodeValue.text().strip()) return code;
    if(!program.weyve.client) return {};
    if(auto item = roomList.selected()) {
        uint32 len = 0;
        const char* id = weyve_room_list_id(program.weyve.client, item.offset(), &len);
        return {string_view{id, len}};
    }
    return {};
}

// screens start out visible, so this has to run before the first transition too
auto WeyveWindow::applyScreens(bool connected, bool inRoom) -> void {
    bool showBrowse = connected && !inRoom;
    if(connectScreen.visible() == !connected && browseScreen.visible() == showBrowse
    && lobbyScreen.visible() == inRoom) return;
    connectScreen.setVisible(!connected);
    browseScreen.setVisible(showBrowse);
    lobbyScreen.setVisible(inRoom);
    layout.resize();
}

// recomputed from both inputs, so no control depends on which changed last
auto WeyveWindow::applyLobbyControls(bool sessionLive, bool isHost) -> void {
    rollbackValue.setEnabled(!sessionLive);  // only Delay is adjustable mid-session
    roleLayout.setEnabled(!sessionLive);
    gameCombo.setEnabled(isHost && !sessionLive);
    btnStop.setEnabled(sessionLive);
    roleLayout.setVisible(isHost);
    btnHostSettings.setVisible(isHost);
    btnStart.setVisible(isHost);
    btnStop.setVisible(isHost);
}

auto WeyveWindow::updateJoinControls() -> void {
    btnJoinRoom.setEnabled((bool)selectedRoomCode());
}

auto WeyveWindow::updateRoleControls() -> void {
    bool hasSelection = (bool)memberList.selected();
    roleCombo.setEnabled(hasSelection);
    btnAssignRole.setEnabled(hasSelection);
    btnKick.setEnabled(hasSelection);
}

auto WeyveWindow::attemptConnect() -> void {
    if(program.weyve.client) return;

    string host = settings.weyvelength.host;
    uint16 port = (uint16)settings.weyvelength.port;
    if(!host || !port) {
        connectStatus.setText("Set a server in Settings > Netplay first.");
        return;
    }

    connectStatus.setText({"Connecting to ", host, ":", port, " ..."});
    if(!program.weyveConnect(host, port)) {
        connectStatus.setText({"Could not connect to ", host, ":", port, "."});
        return;
    }
    refresh();
}

auto WeyveWindow::createRoomPressed() -> void {
    program.weyveCreateRoom(publicCheck.checked());
}

auto WeyveWindow::joinRoomPressed() -> void {
    if(string code = selectedRoomCode()) {
        program.weyveJoinRoom(code, joinPasswordValue.text().strip());
    }
}

auto WeyveWindow::refreshRoomList() -> void {
    program.weyveListRooms();
}

auto WeyveWindow::sendChat() -> void {
    string text = chatValue.text().strip();
    if(text && program.weyve.client) weyve_send_chat(program.weyve.client, text.data());
    chatValue.setText("");
}

auto WeyveWindow::create() -> void {
    layout.setPadding(5_sx, 5_sx);

    // hidden screens must collapse or they still claim space
    connectScreen.setCollapsible();
    browseScreen.setCollapsible();
    lobbyScreen.setCollapsible();

    connectStatus.setText("Connecting...");
    btnRetry.setText("Retry").setIcon(Icon::Device::Network).onActivate([&] { attemptConnect(); });

    nicknameLabel.setText("Name:");
    nicknameValue.setText(settings.weyvelength.nickname).setToolTip(
        "Name other players see in the member list and chat."
    ).onChange([&] {
        settings.weyvelength.nickname = nicknameValue.text().strip();
    });

    btnRefreshRooms.setText("Refresh").setToolTip("Re-query the server for public rooms.")
        .onActivate([&] { refreshRoomList(); });
    btnCreateRoom.setText("Create Room").setToolTip("Create a room and become its host.")
        .onActivate([&] { createRoomPressed(); });
    publicCheck.setText("Public").setChecked(true).setToolTip(
        "List the new room publicly so anyone can find it.\n"
        "Uncheck to make it join-by-code only."
    );
    joinCodeLabel.setText("Code:");
    joinCodeValue.setToolTip("Join a room directly by its code, listed or not.\nLeave empty to join the room selected below.")
        .onChange([&] { program.weyveMarkActive(); updateJoinControls(); });
    joinPasswordLabel.setText("Password:");
    joinPasswordValue.setToolTip("Only needed for rooms marked as locked.");
    btnJoinRoom.setText("Join").onActivate([&] { joinRoomPressed(); });
    roomList.onChange([&] { program.weyveMarkActive(); updateJoinControls(); });
    roomList.onActivate([&] { joinRoomPressed(); });

    btnCopyCode.setText("Copy Code").setToolTip("Copy the room code to the clipboard so you can share it.")
        .onActivate([&] { program.weyveCopyRoomCode(); });

    runAheadLabel.setText("Run-Ahead:");
    runAheadValue.setText({program.netplay.localRunAhead}).setToolTip(
        "Frames to simulate ahead to hide input lag (0-4).\n"
        "Local only: it is never synced, and each player can pick their own.\n"
        "Costs CPU, since every run-ahead frame re-simulates the emulator."
    ).onChange([&] {
        program.netplaySetRunAhead((uint8)runAheadValue.text().strip().natural());
        weyveSyncField(runAheadValue, program.netplay.localRunAhead);
    });

    rollbackLabel.setText("Rollback:");
    rollbackValue.setText("8").setToolTip(
        "How many frames may be rolled back and re-simulated.\n"
        "Cannot go below the room's value; locked once the session starts."
    ).onChange([&] {
        program.weyveSetLocalRollback((uint8)rollbackValue.text().strip().natural());
        weyveSyncField(rollbackValue, program.weyve.localRollback);
    });
    delayLabel.setText("Delay:");
    delayValue.setText({program.weyve.localDelay}).setToolTip(
        "Frames of input delay you add locally. Higher means fewer rollbacks but laggier input.\n"
        "Cannot go below the room's value; this is the only setting adjustable mid-session."
    ).onChange([&] {
        program.weyveSetLocalDelay((uint8)delayValue.text().strip().natural());
        weyveSyncField(delayValue, program.weyve.localDelay);
    });

    memberList.setToolTip("Everyone in the room. Numeric ids are shown per member.");
    memberList.onChange([&] { updateRoleControls(); });
    roleCombo.setToolTip("Pick a slot, then press Assign Role. Player N is controller port N.");
    btnAssignRole.setText("Assign Role").setToolTip(
        "Give the selected member this role.\n"
        "Slots are unique: assigning an occupied one swaps the two members."
    ).onActivate([&] {
        if(auto id = selectedMemberId()) {
            uint sel = roleCombo.selected().offset();
            program.weyveSetRole(id(), sel == 0 ? string{"spec"} : string{sel - 1});
        }
    });
    btnKick.setText("Kick").setToolTip("Remove the selected member from the room.").onActivate([&] {
        if(auto id = selectedMemberId()) program.weyveKick(id());
    });

    gameSelectLabel.setText("Game:");
    gameCombo.setToolTip(
        "Game everyone will play, from your netplay games folder.\n"
        "Host only; each member auto-loads their own copy by content hash."
    ).onChange([&] {
        gameAutoSelectDone = true;
        program.weyveSelectGame(gameCombo.selected().offset());
    });
    btnHostSettings.setText("Settings").setIcon(Icon::Action::Settings)
        .setToolTip("Room settings: visibility, password, and the rollback/delay floor.")
        .onActivate([&] { weyveHostSettings.show(); });

    btnStart.setText("Start").setIcon(Icon::Media::Play)
        .setToolTip("Load the game and start the session for everyone in the room.")
        .onActivate([&] { program.weyveStartGame(); });
    btnStop.setText("Stop").setIcon(Icon::Media::Stop)
        .setToolTip("End the session and unload the game for everyone.")
        .onActivate([&] { program.weyveStopGame(); });
    btnLeave.setText("Leave Room").setToolTip(
        "Leave the room. As host this also ends the session for everyone."
    ).onActivate([&] { program.weyveLeaveRoom(); });

    chatValue.setToolTip("Press Enter to send. Room events appear in the same feed.");
    chatValue.onActivate([&] { sendChat(); });
    btnChatSend.setText("Send").onActivate([&] { sendChat(); });

    refreshTimer.onActivate([&] { refresh(); }).setInterval(200);

    setTitle("Online Rooms (Weyvelength)");
    setSize({560_sx, 476_sy});
    setAlignment({0.5, 0.5});
    onClose([&] { setVisible(false); });

    refresh();
}

auto WeyveWindow::refresh() -> void {
    auto client = program.weyve.client;
    bool connected = client != nullptr;

    uint32 roomLen = 0;
    if(connected) weyve_room_id(client, &roomLen);
    bool inRoom = connected && roomLen > 0;

    applyScreens(connected, inRoom);

    if(connected != lastConnected || inRoom != lastInRoom) {
        bool enteringBrowse = (connected && !inRoom) && !(lastConnected && !lastInRoom);
        bool enteringRoom = inRoom && !lastInRoom;
        lastConnected = connected;
        lastInRoom = inRoom;
        if(!connected && program.weyve.lastError) connectStatus.setText(program.weyve.lastError);
        if(enteringBrowse) refreshRoomList();
        if(enteringRoom) gameAutoSelectDone = false;
    }
    if(!connected) return;

    if(!inRoom) {
        vector<string> rows;
        uint32 count = weyve_room_list_count(client);
        for(uint32 i = 0; i < count; i++) {
            uint32 idLen = 0;
            const char* id = weyve_room_list_id(client, i, &idLen);
            uint32 members = weyve_room_list_members(client, i);
            bool locked = weyve_room_list_passworded(client, i);

            uint32 gameLen = 0;
            const char* game = weyve_room_list_listing(client, i, "game", &gameLen);
            string gameName = game && gameLen ? string{string_view{game, gameLen}} : string{"?"};

            rows.append({string{string_view{id, idLen}}, " | ", gameName,
                " | ", members, " player(s)", locked ? " | locked" : ""});
        }
        if(rows != lastRoomListRows) {
            lastRoomListRows = rows;
            roomList.reset();
            for(auto& row : rows) roomList.append(ListViewItem().setText(row));
            roomList.resizeColumn();
            updateJoinControls();
        }

        if(browseStatus.text() != program.weyve.lastError) {
            // tinted only while it says something, so the empty spacer stays invisible
            bool failed = (bool)program.weyve.lastError;
            browseStatus.setText(program.weyve.lastError);
            browseStatus.setBackgroundColor(failed ? Color{255, 224, 224} : Color{});
            browseStatus.setForegroundColor(failed ? Color{144, 0, 0} : Color{});
        }
        if(nicknameValue.text().strip() != settings.weyvelength.nickname) nicknameValue.setText(settings.weyvelength.nickname);
        return;
    }

    bool sessionLive = program.weyveSessionActive();
    bool isHost = weyve_is_host(client);
    applyLobbyControls(sessionLive, isHost);

    string roomGameHash = program.weyveRoomDataString("game_hash");

    if(isHost) {
        if(program.weyveLibraryStale()) program.weyveScanLibrary();
        if(gameLibrarySize != program.weyve.library.size()) {
            gameLibrarySize = program.weyve.library.size();
            gameCombo.reset();
            for(auto& entry : program.weyve.library) gameCombo.append(ComboButtonItem().setText(entry.title));
        }
        if(!gameAutoSelectDone && program.weyve.library.size() && !roomGameHash) {
            program.weyveSelectGame(0);  // retried until a pick or a confirmed hash lands
        }
        if(roomGameHash) gameAutoSelectDone = true;

        string status = sessionLive ? string{"Session running"} : program.weyveStartBlockedReason();
        btnStart.setEnabled(!status);
        if(status != lastStartStatus) {
            lastStartStatus = status;
            startStatus.setText(status);
        }

        uint32 memberCount = 0;
        weyve_members(client, &memberCount);
        uint slots = min(memberCount, (uint32)Program::Weyve::PlayerCap);
        if(slots != lastRoleCount) {
            lastRoleCount = slots;
            roleCombo.reset();
            roleCombo.append(ComboButtonItem().setText("Spectator"));
            for(uint i = 1; i <= slots; i++) roleCombo.append(ComboButtonItem().setText({"Player ", i}));
        }
    } else {
        string game = program.weyveRoomDataString("game");
        if(game != lastNonHostGame) {
            lastNonHostGame = game;
            gameCombo.reset();
            if(game) gameCombo.append(ComboButtonItem().setText(game));
        }
    }

    uint32 roomIdLen = 0;
    const char* roomId = weyve_room_id(client, &roomIdLen);
    roomLabel.setText({"Room ", string{string_view{roomId, roomIdLen}}, isHost ? " (you are host)" : ""});

    uint32 count = 0;
    const uint32* members = weyve_members(client, &count);
    uint32 hostId = weyve_host_id(client);
    uint32 selfId = weyve_id(client);
    vector<string> memberRows;
    for(uint32 i = 0; i < count; i++) memberRows.append(memberRow(members[i], (bool)roomGameHash, hostId, selfId));
    if(memberRows != lastMemberRows) {
        if(memberRows.size() == lastMemberRows.size()) {
            // ping digits churn every tick; patching keeps the selection and avoids flicker
            for(uint i : range(memberRows.size())) {
                if(memberRows[i] != lastMemberRows[i]) memberList.item(i).setText(memberRows[i]);
            }
        } else {
            memberList.reset();
            for(auto& row : memberRows) memberList.append(ListViewItem().setText(row));
            updateRoleControls();
        }
        memberList.resizeColumn();
        lastMemberRows = move(memberRows);

        // no per-row tooltip on Windows, so ids go on the list itself
        string ids;
        for(uint32 i = 0; i < count; i++) {
            ids.append(program.weyveNicknameOf(members[i]), " - id ", members[i], "\n");
        }
        memberList.setToolTip(ids.stripRight());
    }

    if(loggedLines != program.weyve.log.size() || lastLogEpoch != program.weyve.logEpoch) {
        loggedLines = program.weyve.log.size();
        lastLogEpoch = program.weyve.logEpoch;
        eventLog.reset();
        for(uint i = loggedLines; i-- > 0;) eventLog.append(ListViewItem().setText(program.weyve.log[i]));
        eventLog.resizeColumn();
    }

    weyveSyncField(rollbackValue, program.weyve.localRollback);
    weyveSyncField(delayValue, program.weyve.localDelay);
    weyveSyncField(runAheadValue, program.netplay.localRunAhead);
}

auto WeyveWindow::setVisible(bool visible) -> WeyveWindow& {
    refreshTimer.setEnabled(visible);
    if(visible) {
        Application::processEvents();
        attemptConnect();
        refresh();
    } else {
        weyveHostSettings.setVisible(false);
        if(program.weyve.client) {
            uint32 roomLen = 0;
            weyve_room_id(program.weyve.client, &roomLen);
            if(roomLen) program.weyveLeaveRoom();  // as host this ends it for everyone
        }
    }
    return Window::setVisible(visible), *this;
}

auto WeyveWindow::show() -> void {
    setVisible();
    setFocused();
}

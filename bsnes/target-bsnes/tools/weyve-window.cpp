// room-wide values are passed in; the caller hoists them out of the loop
auto WeyveWindow::memberRow(uint32_t id, bool roomHasGame, uint32_t hostId, uint32_t selfId) -> string {
    string label = {program.weyveNicknameOf(id), "  ", program.weyveRoleLabel(program.weyveRoleOf(id))};
    if(id == hostId) label.append(" (host)");
    if(id == selfId) label.append(" (you)");

    if(roomHasGame && program.weyveMemberDataString(id, "hasGame") != "1") label.append("  [no game]");

    if(program.weyveSessionActive()) {
        for(auto& peer : program.netplay.peers) {
            if(peer.weyveId != id || peer.type != GekkoRemotePlayer) continue;
            auto& stats = program.netplay.netStats[peer.id];
            label.append({"  [ping ", (uint)stats.last_ping, "ms jitter ", (uint)stats.jitter, "ms]"});
            break;
        }
    }
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
    if(!program.weyve.client) return;

    string code = joinCodeValue.text().strip();
    if(!code) {
        if(auto item = roomList.selected()) {
            uint32 len = 0;
            const char* id = weyve_room_list_id(program.weyve.client, item.offset(), &len);
            code = string{string_view{id, len}};
        }
    }
    if(!code) return;
    program.weyveJoinRoom(code, joinPasswordValue.text().strip());
}

auto WeyveWindow::refreshRoomList() -> void {
    if(program.weyve.client) weyve_list_rooms(program.weyve.client, nullptr, 0);
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

    btnRefreshRooms.setText("Refresh").setToolTip("Re-query the server for public rooms.")
        .onActivate([&] { refreshRoomList(); });
    btnCreateRoom.setText("Create Room").setToolTip("Create a room and become its host.")
        .onActivate([&] { createRoomPressed(); });
    publicCheck.setText("Public").setChecked(true).setToolTip(
        "List the new room publicly so anyone can find it.\n"
        "Uncheck to make it join-by-code only."
    );
    btnDisconnect.setText("Disconnect").setToolTip("Disconnect from the room server.")
        .onActivate([&] { program.weyveDisconnect(); refresh(); });
    joinCodeLabel.setText("Code:");
    joinCodeValue.setToolTip("Join a room directly by its code, listed or not.\nLeave empty to join the room selected above.");
    joinPasswordLabel.setText("Password:");
    joinPasswordValue.setToolTip("Only needed for rooms marked as locked.");
    btnJoinRoom.setText("Join").onActivate([&] { joinRoomPressed(); });

    btnCopyCode.setText("Copy Code").setToolTip("Copy the room code to the clipboard so you can share it.")
        .onActivate([&] { program.weyveCopyRoomCode(); });

    runAheadLabel.setText("Run-Ahead:");
    runAheadValue.setText({settings.emulator.runAhead.frames}).setToolTip(
        "Frames to simulate ahead to hide input lag (0-4).\n"
        "Local only: it is never synced, and each player can pick their own.\n"
        "Costs CPU, since every run-ahead frame re-simulates the emulator."
    ).onChange([&] {
        settings.emulator.runAhead.frames = min(4u, (uint)runAheadValue.text().strip().natural());
    });

    rollbackLabel.setText("Rollback:");
    rollbackValue.setText("8").setToolTip(
        "How many frames may be rolled back and re-simulated.\n"
        "Cannot go below the room's value; locked once the session starts."
    ).onChange([&] {
        uint8 floor = (uint8)program.weyveRoomDataString("rollback_min").natural();
        program.weyve.localRollback = max(floor, (uint8)rollbackValue.text().strip().natural());
    });
    delayLabel.setText("Delay:");
    delayValue.setText("2").setToolTip(
        "Frames of input delay you add locally. Higher means fewer rollbacks but laggier input.\n"
        "Cannot go below the room's value; this is the only setting adjustable mid-session."
    ).onChange([&] {
        uint8 floor = (uint8)program.weyveRoomDataString("delay_min").natural();
        program.weyve.localDelay = max(floor, (uint8)delayValue.text().strip().natural());
        program.weyveApplyLocalDelay();  // takes effect immediately mid-session
    });

    memberList.setToolTip("Everyone in the room. Numeric ids are shown per member.");
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

    if(connected != lastConnected || inRoom != lastInRoom) {
        bool enteringBrowse = (connected && !inRoom) && !(lastConnected && !lastInRoom);
        bool enteringRoom = inRoom && !lastInRoom;
        lastConnected = connected;
        lastInRoom = inRoom;
        connectScreen.setVisible(!connected);
        browseScreen.setVisible(connected && !inRoom);
        lobbyScreen.setVisible(inRoom);
        layout.resize();
        if(enteringBrowse) refreshRoomList();
        if(enteringRoom) {
            gameAutoSelectDone = false;
            lastIsHost = -1;
        }
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
                " | ", members, " players", locked ? " | locked" : ""});
        }
        if(rows != lastRoomListRows) {
            lastRoomListRows = rows;
            roomList.reset();
            for(auto& row : rows) roomList.append(ListViewItem().setText(row));
        }
        return;
    }

    bool sessionLive = program.weyveSessionActive();
    if((int)sessionLive != lastRunning) {
        lastRunning = sessionLive;
        rollbackValue.setEnabled(!sessionLive);  // only Delay is adjustable mid-session
        btnStop.setEnabled(sessionLive);
        gameCombo.setEnabled(!sessionLive && weyve_is_host(client));
        roleLayout.setEnabled(!sessionLive);
    }

    bool isHost = weyve_is_host(client);
    if((int)isHost != lastIsHost) {
        lastIsHost = isHost;
        gameCombo.setEnabled(isHost && !sessionLive);
        btnHostSettings.setVisible(isHost);
        roleLayout.setVisible(isHost);
        btnStart.setVisible(isHost);
        btnStop.setVisible(isHost);
        if(!isHost) { startStatus.setText(""); lastStartReason = ""; }
    }

    string roomGameHash = program.weyveRoomDataString("game_hash");

    if(isHost) {
        if(!program.weyve.libraryScanned) program.weyveScanLibrary();
        if(gameLibrarySize != program.weyve.library.size()) {
            gameLibrarySize = program.weyve.library.size();
            gameCombo.reset();
            for(auto& entry : program.weyve.library) gameCombo.append(ComboButtonItem().setText(entry.title));
        }
        if(!gameAutoSelectDone && program.weyve.library.size() && !roomGameHash) {
            program.weyveSelectGame(0);  // retry until it lands; a manual pick or a confirmed hash stops this
        }
        if(roomGameHash) gameAutoSelectDone = true;

        string reason = sessionLive ? string{} : program.weyveStartBlockedReason();
        if(reason != lastStartReason) {
            lastStartReason = reason;
            btnStart.setEnabled(!sessionLive && !reason);
            startStatus.setText(sessionLive ? string{"Session running"} : reason);
        }

        // cap slots at how many members could actually play
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
        lastMemberRows = memberRows;
        // ping digits churn every tick; keep the selection across the rebuild
        auto selected = memberList.selected();
        int selectedOffset = selected ? (int)selected.offset() : -1;
        memberList.reset();
        for(auto& row : memberRows) memberList.append(ListViewItem().setText(row));
        if(selectedOffset >= 0 && selectedOffset < (int)memberRows.size()) {
            memberList.item(selectedOffset).setSelected();
        }

        // no per-row tooltip on Windows, so ids go on the list itself
        string ids;
        for(uint32 i = 0; i < count; i++) {
            ids.append(program.weyveNicknameOf(members[i]), " - id ", members[i], "\n");
        }
        memberList.setToolTip(ids.stripRight());
    }

    if(loggedLines != program.weyve.log.size()) {
        loggedLines = program.weyve.log.size();
        eventLog.reset();
        for(uint i = loggedLines; i-- > 0;) eventLog.append(ListViewItem().setText(program.weyve.log[i]));
    }

    if(program.weyve.localRollback != lastShownRollback) {
        lastShownRollback = program.weyve.localRollback;
        rollbackValue.setText({lastShownRollback});
    }
    if(program.weyve.localDelay != lastShownDelay) {
        lastShownDelay = program.weyve.localDelay;
        delayValue.setText({lastShownDelay});
    }
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
            if(roomLen) program.weyveLeaveRoom();  // also stops the session, and ends it for everyone if we host
        }
    }
    return Window::setVisible(visible), *this;
}

auto WeyveWindow::show() -> void {
    setVisible();
    setFocused();
}

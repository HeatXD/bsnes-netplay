// every control here is host-only
static auto weyveHostClient() -> WeyveClient* {
    auto client = program.weyve.client;
    return client && weyve_is_host(client) ? client : nullptr;
}

auto WeyveHostSettings::create() -> void {
    layout.setPadding(5_sx, 5_sx);

    listedCheck.setText("Room Listed").setToolTip(
        "Show this room in the public room browser.\n"
        "When off, players can still join by typing the room code."
    ).onToggle([&] {
        if(auto client = weyveHostClient()) weyve_set_room_listed(client, listedCheck.checked());
    });
    openCheck.setText("Room Open").setToolTip(
        "Allow new members to join.\n"
        "Closed automatically while a session is running."
    ).onToggle([&] {
        if(auto client = weyveHostClient()) weyve_set_room_joinable(client, openCheck.checked());
    });
    desyncCheck.setText("Desync Detection").setToolTip(
        "Have GekkoNet compare state checksums between peers to catch desyncs.\n"
        "Costs bandwidth and CPU, so leave it off unless you're debugging."
    ).onToggle([&] {
        if(auto client = weyveHostClient()) weyve_set_room_data(client, "desync", desyncCheck.checked() ? "1" : "0");
    });

    passwordLabel.setText("Password:");
    passwordValue.setToolTip("Leave empty and press Set to remove the password.");
    btnSetPassword.setText("Set").onActivate([&] {
        if(auto client = weyveHostClient()) weyve_set_room_password(client, passwordValue.text().strip().data());
        passwordValue.setText("");
    });

    rollbackMinLabel.setText("Room Rollback:");
    rollbackMinValue.setText("8").setToolTip(
        "Minimum rollback frames for everyone in the room.\n"
        "Clients may raise their own value but never go below this."
    ).onChange([&] {
        program.weyveSetBaseline((uint8)rollbackMinValue.text().strip().natural(), (uint8)delayMinValue.text().strip().natural());
    });
    delayMinLabel.setText("Room Delay:");
    delayMinValue.setText("2").setToolTip(
        "Minimum input delay for everyone in the room.\n"
        "Clients may raise their own value but never go below this."
    ).onChange([&] {
        program.weyveSetBaseline((uint8)rollbackMinValue.text().strip().natural(), (uint8)delayMinValue.text().strip().natural());
    });

    spectatorDelayLabel.setText("Spectator Delay:");
    spectatorDelayValue.setText("300").setToolTip(
        "Frames spectators lag behind live play (300 = 5 seconds at 60fps).\n"
        "Higher absorbs network jitter; lower is more current but stutters more."
    ).onChange([&] {
        if(auto client = weyveHostClient()) {
            uint frames = spectatorDelayValue.text().strip().natural();
            weyve_set_room_data(client, "spectator_delay", string{frames}.data());
        }
    });

    refreshTimer.onActivate([&] { refresh(); }).setInterval(200);

    setTitle("Room Settings");
    setSize({480_sx, 160_sy});
    setAlignment({0.5, 0.5});
    setDismissable();
    onClose([&] { setVisible(false); });
}

auto WeyveHostSettings::refresh() -> void {
    auto client = weyveHostClient();
    if(!client) return;

    bool running = program.weyveSessionActive();
    if((int)running != lastRunning) {
        lastRunning = running;
        rollbackMinValue.setEnabled(!running);  // baseline locks once the session is live
        delayMinValue.setEnabled(!running);
        spectatorDelayValue.setEnabled(!running);
    }

    bool listed = weyve_room_listed(client);
    bool open = weyve_room_joinable(client);
    if((int)listed != lastListed) { lastListed = listed; listedCheck.setChecked(listed); }
    if((int)open != lastOpen) { lastOpen = open; openCheck.setChecked(open); }

    bool desync = program.weyveRoomDataString("desync") == "1";
    if((int)desync != lastDesync) {
        lastDesync = desync;
        desyncCheck.setChecked(desync);
    }
    desyncCheck.setEnabled(!running);
}

auto WeyveHostSettings::setVisible(bool visible) -> WeyveHostSettings& {
    refreshTimer.setEnabled(visible);
    if(visible) {
        lastListed = -1;
        lastOpen = -1;
        refresh();
    }
    return Window::setVisible(visible), *this;
}

auto WeyveHostSettings::show() -> void {
    setVisible();
    setFocused();
}

auto NetplayWindow::isValidIP(const string& ip) -> bool {
    auto parts = ip.split(".");
    if(parts.size() != 4) return false;
    
    for(auto& part : parts) {
        if(!part) return false;
        uint num = part.natural();
        if(num > 255) return false;
        if(part != "0" && num == 0) return false;
    }
    return true;
}

auto NetplayWindow::isLoopbackIP(const string& ip) -> bool {
    auto parts = ip.split(".");
    return (parts.size() == 4 && parts[0].natural() == 127);
}

auto NetplayWindow::isValidPort(const string& port) -> bool {
    if(!port) return false;
    uint portNum = port.natural();
    return (portNum >= 1 && portNum <= 65535);
}

auto NetplayWindow::isValidRemoteIP(const string& ip) -> bool {
    return isValidIP(ip) && (devMode || !isLoopbackIP(ip));
}

auto NetplayWindow::setValidationColor(LineEdit& field, bool valid, bool hasText) -> void {
    field.setBackgroundColor(hasText && !valid ? Color{255, 200, 200} : Color{});
}

auto NetplayWindow::formatEntry(const string& role, const string& ip, const string& port) -> string {
    return {role, " | ", ip, " | ", port};
}

auto NetplayWindow::parseEntry(const string& text, string& role, string& ip, string& port) -> bool {
    auto parts = text.split("|");
    if(parts.size() < 3) return false;
    role = parts[0].strip();
    ip = parts[1].strip();
    port = parts[2].strip();
    return true;
}

auto NetplayWindow::updateEditingState() -> void {
    auto item = remotePlayersList.selected();
    bool hasSelection = (bool)item;

    btnRemove.setEnabled(hasSelection);
    editIPValue.setEnabled(hasSelection);
    editPortValue.setEnabled(hasSelection);

    string role, ip, port;
    if(hasSelection && parseEntry(item.text(), role, ip, port)) {
        editingLabel.setText({"Editing ", role, ":"});
        editIPValue.setText(ip);
        editPortValue.setText(port);
    } else {
        editingLabel.setText("Select an entry to edit");
        editIPValue.setText("");
        editPortValue.setText("");
    }
}

auto NetplayWindow::updateSelectedIP(const string& ip) -> void {
    auto item = remotePlayersList.selected();
    if(!item) return;

    string role, oldIP, port;
    if(!parseEntry(item.text(), role, oldIP, port)) return;

    remotePlayersList.item(item.offset()).setText(formatEntry(role, ip, port));
}

auto NetplayWindow::updateSelectedPort(const string& port) -> void {
    auto item = remotePlayersList.selected();
    if(!item) return;

    string role, ip, oldPort;
    if(!parseEntry(item.text(), role, ip, oldPort)) return;

    remotePlayersList.item(item.offset()).setText(formatEntry(role, ip, port));
}

auto NetplayWindow::sortPlayerList() -> void {
    vector<string> entries;
    for(uint i = 0; i < remotePlayersList.itemCount(); i++) {
        entries.append(remotePlayersList.item(i).text());
    }
    
    // Sort: Players by number first, then spectators
    entries.sort([](const string& a, const string& b) -> bool {
        auto aParts = a.split("|");
        auto bParts = b.split("|");
        if(aParts.size() < 1 || bParts.size() < 1) return false;

        string aPlayer = aParts[0].strip();
        string bPlayer = bParts[0].strip();
        
        bool aIsPlayer = aPlayer.beginsWith("Player ");
        bool bIsPlayer = bPlayer.beginsWith("Player ");
        
        // Players come before spectators
        if(aIsPlayer && !bIsPlayer) return true;
        if(!aIsPlayer && bIsPlayer) return false;
        
        // Both players - sort by number
        if(aIsPlayer && bIsPlayer) {
            uint aNum = aPlayer.trimLeft("Player ", 1L).natural();
            uint bNum = bPlayer.trimLeft("Player ", 1L).natural();
            return aNum < bNum;
        }
        
        // Both spectators - maintain order
        return false;
    });
    
    // Rebuild list
    remotePlayersList.reset();
    for(auto& entry : entries) {
        remotePlayersList.append(ListViewItem().setText(entry));
    }
}

auto NetplayWindow::create() -> void {
    layout.setPadding(5_sx, 5_sx);

    roleLabel.setText("Your Role:").setFont(Font().setBold());
    rolePlayer1.setText("Player 1").onActivate([&] { updateRole(Role::Player1); });
    rolePlayer2.setText("Player 2").onActivate([&] { updateRole(Role::Player2); });
    rolePlayer3.setText("Player 3").onActivate([&] { updateRole(Role::Player3); });
    rolePlayer4.setText("Player 4").onActivate([&] { updateRole(Role::Player4); });
    rolePlayer5.setText("Player 5").onActivate([&] { updateRole(Role::Player5); });
    roleSpectator.setText("Spectator").onActivate([&] { updateRole(Role::Spectator); });
    
    roleSpacer.setColor({192, 192, 192});

    connectionLabel.setText("Your Setup:").setFont(Font().setBold());

    portLabel.setText("Your Port:");
    portValue.setText("55435").onChange([&] {
        string port = portValue.text().strip();
        bool valid = isValidPort(port);
        setValidationColor(portValue, valid, (bool)port);
        if(valid) config.localPort = port.natural();
    });
    
    spectatorPlayerCountLabel.setText("Players in session:");
    spectatorPlayerCountValue.setText("2").onChange([&] {
        config.spectatorPlayerCount = spectatorPlayerCountValue.text().strip().integer();
    });

    rollbackLabel.setText("Rollback Frames:");
    rollbackValue.setText("8").setAlignment(0.5);
    rollbackSlider.setLength(13).setPosition(8).onChange([&] {
        uint frames = rollbackSlider.position();
        rollbackValue.setText({frames});
        config.rollbackframes = frames;
    });
    
    delayLabel.setText("Input Delay:");
    delayValue.setText("2").setAlignment(0.5);
    delaySlider.setLength(11).setPosition(2).onChange([&] {
        uint delay = delaySlider.position();
        delayValue.setText({delay});
        config.localDelay = delay;
    });
    
    sliderSpacer.setColor({192, 192, 192});

    remotePlayersLabel.setText("Other Players:").setFont(Font().setBold());
    remotePlayersList.onChange([&] { updateEditingState(); });
    
    editIPLabel.setText("IP:");
    editIPValue.setText("").setEnabled(false).onChange([&] {
        string ip = editIPValue.text().strip();
        bool valid = isValidRemoteIP(ip);
        setValidationColor(editIPValue, valid, (bool)ip);
        if(valid) updateSelectedIP(ip);
    });
    
    editPortLabel.setText("Port:");
    editPortValue.setText("").setEnabled(false).onChange([&] {
        string port = editPortValue.text().strip();
        bool valid = isValidPort(port);
        
        setValidationColor(editPortValue, valid, (bool)port);
        if(valid) updateSelectedPort(port);
    });
    
    btnAddPlayer.setText("Add Player").onActivate([&] { addPlayer(); });
    btnAddSpectator.setText("Add Spectator").onActivate([&] { addSpectator(); });
    btnRemove.setText("Remove").onActivate([&] { removeSelectedPlayer(); }).setEnabled(false);

    editingLabel.setFont(Font().setBold());

    rolePlayer1.setChecked();
    updateRole(Role::Player1);
    
    bottomSpacer.setColor({192, 192, 192});
    
    devModeCheck.setText("Dev Mode").setChecked(false).onToggle([&] {
        devMode = devModeCheck.checked();
        string ip = editIPValue.text().strip();
        if(ip) setValidationColor(editIPValue, isValidRemoteIP(ip), true);
    });

    btnStart.setText("Start Netplay").setIcon(Icon::Media::Play).onActivate([&] { startSession(); });
    btnCancel.setText("Cancel").setIcon(Icon::Action::Quit).onActivate([&] { doClose(); });

    setTitle("Netplay Setup");
    setSize({500_sx, 365_sy});
    setAlignment({0.5, 0.5});
    onClose([&] { setVisible(false); });
}

auto NetplayWindow::updateRole(Role role) -> void {
    currentRole = role;
    bool isSpectator = (role == Role::Spectator);
    
    if(isSpectator) {
        remotePlayersLabel.setText("Player to Spectate:");
        
        remotePlayersList.reset();
        remotePlayersList.append(ListViewItem().setText(formatEntry("Any Player", "127.0.0.1", "55435")));
        remotePlayersList.item(0).setSelected();

        btnAddPlayer.setEnabled(false);
        btnAddSpectator.setEnabled(false);

        spectatorPlayerCountLabel.setVisible(true);
        spectatorPlayerCountValue.setVisible(true);
        slidersLayout.setVisible(false);
    } else {
        remotePlayersLabel.setText("Other Players:");
        autoPopulatePlayerList();

        btnAddPlayer.setEnabled(true);
        btnAddSpectator.setEnabled(true);

        spectatorPlayerCountLabel.setVisible(false);
        spectatorPlayerCountValue.setVisible(false);
        slidersLayout.setVisible(true);
    }

    updateEditingState();
}

auto NetplayWindow::autoPopulatePlayerList() -> void {
    remotePlayersList.reset();
    uint myPlayerNum = roleToPlayerIndex(currentRole) + 1;
    uint maxPlayer = myPlayerNum < 2 ? 2 : myPlayerNum;

    for(uint p = 1; p <= maxPlayer; p++) {
        if(p != myPlayerNum) {
            remotePlayersList.append(ListViewItem().setText(formatEntry({"Player ", p}, "127.0.0.1", "55435")));
        }
    }
}

auto NetplayWindow::addPlayer() -> void {
    uint myPlayerNum = roleToPlayerIndex(currentRole) + 1;
    vector<uint> usedPlayers{myPlayerNum};
    
    for(uint i = 0; i < remotePlayersList.itemCount(); i++) {
        string role, ip, port;
        if(parseEntry(remotePlayersList.item(i).text(), role, ip, port)) {
            if(role.beginsWith("Player ")) {
                usedPlayers.append(role.trimLeft("Player ", 1L).natural());
            }
        }
    }

    for(uint p = 1; p <= 5; p++) {
        if(!usedPlayers.find(p)) {
            remotePlayersList.append(ListViewItem().setText(formatEntry({"Player ", p}, "127.0.0.1", "55435")));
            sortPlayerList();
            remotePlayersList.resizeColumn();
            updateEditingState();
            return;
        }
    }
    
    MessageDialog()
        .setTitle("Maximum Players")
        .setText("All 5 player slots are already in use.\n\nYou can add spectators instead.")
        .information();
}

auto NetplayWindow::addSpectator() -> void {
    remotePlayersList.append(ListViewItem().setText(formatEntry("Spectator", "127.0.0.1", "55435")));
    remotePlayersList.resizeColumn();
    updateEditingState();
}

auto NetplayWindow::removeSelectedPlayer() -> void {
    auto item = remotePlayersList.selected();
    if(!item) return;

    if(currentRole == Role::Spectator) {
        MessageDialog()
            .setTitle("Cannot Remove")
            .setText("As a spectator, you must have a player to connect to.\n\n"
                     "You can edit the IP and port, but cannot remove this entry.")
            .information();
        return;
    }

    string role, ip, port;
    if(!parseEntry(item.text(), role, ip, port)) return;

    if(role.beginsWith("Player ")) {
        uint myPlayerNum = roleToPlayerIndex(currentRole) + 1;
        uint pNum = role.trimLeft("Player ", 1L).natural();

        if(pNum < myPlayerNum) {
            MessageDialog()
                .setTitle("Cannot Remove Player")
                .setText({"You selected Player ", myPlayerNum, ", so you must have players 1-",
                         myPlayerNum - 1, " configured.\n\n",
                         "You can only remove players with numbers higher than yours."})
                .information();
            return;
        }

        uint playerCount = 1;
        for(uint i = 0; i < remotePlayersList.itemCount(); i++) {
            string r, dummy1, dummy2;
            if(parseEntry(remotePlayersList.item(i).text(), r, dummy1, dummy2) && r != "Spectator") {
                playerCount++;
            }
        }

        if(playerCount <= 2) {
            MessageDialog()
                .setTitle("Minimum Players Required")
                .setText("You must have at least 2 players in a netplay session.\n\n"
                         "Cannot remove this player as it would leave only 1 player total.")
                .information();
            return;
        }
    }

    remotePlayersList.remove(item);
    remotePlayersList.resizeColumn();
    updateEditingState();
}

auto NetplayWindow::startSession() -> void {
    if(!config.localPort || config.localPort > 65535) {
        MessageDialog()
            .setTitle("Invalid Port")
            .setText("Please enter a valid port number (1-65535).")
            .information();
        return;
    }

    uint8 localPlayer = roleToPlayerIndex(currentRole);
    bool isSpectator = (localPlayer == 255);
    uint myPlayerNum = localPlayer + 1;

    if(isSpectator && (config.spectatorPlayerCount < 2 || config.spectatorPlayerCount > 5)) {
        MessageDialog()
            .setTitle("Invalid Player Count")
            .setText("Please specify how many players are in the session (2-5).")
            .information();
        return;
    }

    vector<string> remotes, spectators;

    for(uint i = 0; i < remotePlayersList.itemCount(); i++) {
        string player, ip, port;
        if(!parseEntry(remotePlayersList.item(i).text(), player, ip, port)) continue;
        if(!ip || !port) continue;

        if(!isValidRemoteIP(ip)) {
            MessageDialog()
                .setTitle("Invalid IP Address")
                .setText({"Loopback addresses (127.x.x.x) are not allowed for netplay.\n\n",
                         "Player: ", player, "\nIP: ", ip, "\n\n",
                         "Please enter the actual network IP address of the remote player.\n",
                         "Or enable Developer Mode to allow loopback addresses."})
                .information();
            return;
        }

        if(player == "Spectator") {
            spectators.append({ip, ":", port});
        } else if(player.beginsWith("Player ")) {
            uint playerNum = player.trimLeft("Player ", 1L).natural();
            if(playerNum != myPlayerNum) {
                remotes.append({ip, ":", port});
            }
        }
    }

    if(remotes.size() == 0) {
        MessageDialog()
            .setTitle("No Connection Info")
            .setText(isSpectator
                ? "You need to connect to one player to spectate.\n\nEnter the IP address of any player in the session."
                : "You need to add at least one other player.\n\nEach player should enter the IP addresses of ALL other players.\nThis is a peer-to-peer connection - everyone connects to everyone.")
            .information();
        return;
    }

    // Add spectator fillers
    vector<string> finalRemotes = remotes;
    if(isSpectator && config.spectatorPlayerCount > 1) {
        for(uint i = 0; i < config.spectatorPlayerCount - 1; i++) {
            finalRemotes.append("SPEC_FILLER");
        }
    }

    program.netplayStart(config.localPort, localPlayer, config.rollbackframes,
                        config.localDelay, finalRemotes, spectators);
    doClose();
}

auto NetplayWindow::roleToPlayerIndex(Role role) -> uint8 {
    switch(role) {
        case Role::Player1: return 0;
        case Role::Player2: return 1;
        case Role::Player3: return 2;
        case Role::Player4: return 3;
        case Role::Player5: return 4;
        case Role::Spectator: return 255;
    }
    return 0;
}

auto NetplayWindow::setVisible(bool visible) -> NetplayWindow& {
    if(visible) {
        Application::processEvents();
    }
    return Window::setVisible(visible), *this;
}

auto NetplayWindow::show() -> void {
    setVisible();
    setFocused();
}
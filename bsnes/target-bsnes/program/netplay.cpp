auto Program::netplayMode(Netplay::Mode mode) -> void {
    if(netplay.mode == mode) return;
    if(mode == Netplay::Running) {
        // be sure the contollers are connected
        emulator->connect(0, Netplay::Device::Gamepad);
        // depending on the player count either add a normal gamepad or a multitap
        emulator->connect(1, netplay.config.num_players > 2 ? Netplay::Device::Multitap : Netplay::Device::Gamepad);
        // disable input when unfocused
        inputSettings.blockInput.setChecked();
        // we don't want entropy
        emulator->configure("Hacks/Entropy", "None");
        // power cycle to match all peers
        emulator->power();
    }
    netplay.mode = mode;
}

auto Program::netplayStart(uint16 port, uint8 local, uint8 rollback, uint8 delay, vector<string>& remotes, vector<string> &spectators) -> void {
    if(netplay.mode != Netplay::Mode::Inactive) return;

    int numPlayers = remotes.size();

    // add local player
    if(local < 5) numPlayers++;

    const int inpBufferLength = numPlayers > 2 ? 5 : numPlayers;
    for(int i = 0; i < inpBufferLength; i++) {
        netplay.inputs.append(Netplay::Buttons());
    }

    const int stateSize = emulator->serialize(0).size();

    netplay.config.num_players = numPlayers;
    netplay.config.input_size = sizeof(Netplay::Buttons);
    netplay.config.state_size = stateSize;
    netplay.config.max_spectators = spectators.size();
    netplay.config.input_prediction_window = rollback;
    netplay.config.spectator_delay = 90;

    netplay.netStats.resize(inpBufferLength);

    gekko_create(&netplay.session);
    gekko_start(netplay.session, &netplay.config);
    gekko_net_adapter_set(netplay.session, gekko_default_adapter(port));

    if(local < numPlayers) {
        // player? connect to all peers in the mesh network
        bool localAdded = false;
        for(int i = 0; i < numPlayers; i++) {
            
            auto peer = Netplay::Peer();
            peer.nickname = {"P", i + 1};
            if(i == local) {
                // add local player
                peer.id = gekko_add_actor(netplay.session, LocalPlayer, nullptr);
                peer.conn.addr = "localhost";
                netplay.peers.append(peer);
                // if local is the first player connect to all spectators
                if(local == 0) {
                    for(int j = 0; j < spectators.size(); j++) {
                        auto sPeer = Netplay::Peer();
                        sPeer.nickname = "spectator";
                        sPeer.conn.addr = spectators[j];
                        auto remote = GekkoNetAddress{ (void*)sPeer.conn.addr.data(), sPeer.conn.addr.size() };
                        sPeer.id = gekko_add_actor(netplay.session, Spectator, &remote);
                        netplay.peers.append(sPeer);
                    }
                }
                // set local delay
                gekko_set_local_delay(netplay.session, local, delay);
                netplay.localDelay = delay;
                localAdded = true;
                continue;
            }
            // add remote player
            peer.conn.addr = remotes[localAdded ? i - 1 : i]; // skip local player entry
            auto remote = GekkoNetAddress{ (void*)peer.conn.addr.data(), peer.conn.addr.size() };
            peer.id = gekko_add_actor(netplay.session, RemotePlayer, &remote);
            netplay.peers.append(peer);
        }
    }else{
        // spectator? only connect to the first peer
        auto peer = Netplay::Peer();
        peer.nickname = "P1";
        peer.conn.addr = remotes[0];
        auto remote = GekkoNetAddress{ (void*)peer.conn.addr.data(), peer.conn.addr.size() };
        peer.id = gekko_add_actor(netplay.session, RemotePlayer, &remote);
        netplay.peers.append(peer);
    }

    netplayMode(Netplay::Running);
}

auto Program::netplayStop() -> void {
    if (netplay.mode == Netplay::Mode::Inactive) return;

    netplayMode(Netplay::Inactive);
    
    gekko_destroy(&netplay.session);
    netplay.session = nullptr; 

    netplay.config = {};
    netplay.counter = 0;

    netplay.peers.reset();
    netplay.inputs.reset();
    netplay.netStats.reset();

    inputSettings.pauseEmulation.setChecked();

    program.mute &= ~Mute::Always;
}

auto Program::netplayRun() -> bool {
    if (netplay.mode != Netplay::Mode::Running) return false;

    gekko_network_poll(netplay.session);

    netplay.counter++;
    float framesAhead = gekko_frames_ahead(netplay.session);
    if (framesAhead - netplay.localDelay >= 1.0f && netplay.counter % 180 == 0) {
        // rift syncing first attempt
        // kinda hacky.... when i can find a way to just slow down the frequency of the simulation, ill fix this. 
        netplayHaltFrame();
        return true;
    }

    for(int i = 0; i < netplay.peers.size(); i++) {
        if(netplay.peers[i].conn.addr != "localhost"){
            if(netplay.peers[i].nickname != "spectator"){
                uint8 peerId = netplay.peers[i].id;
                gekko_network_stats(netplay.session, peerId, &netplay.netStats[peerId]);
            }
            continue;
        };
        Netplay::Buttons input = {};
        netplayPollLocalInput(input);
        gekko_add_local_input(netplay.session, netplay.peers[i].id, &input);
    }
    
    int count = 0;
    auto events = gekko_session_events(netplay.session, &count);
    for(int i = 0; i < count; i++) {
        auto event = events[i];
        int type = event->type;
        //print("EV: ", type);
        if (event->type == PlayerDisconnected) {
            auto disco = event->data.disconnected;
            showMessage({"Peer Disconnected: ", disco.handle});
            continue;
        }
        if (event->type == PlayerConnected) {
            auto conn = event->data.connected;
            showMessage({"Peer Connected: ", conn.handle});
            continue;
        }
        if (event->type == SessionStarted) {
            showMessage({"Netplay Session Started"});
            continue;
        }
    }

    count = 0;
    auto updates = gekko_update_session(netplay.session, &count);
    for (int i = 0; i < count; i++) {
        auto ev = updates[i];
        auto serial = serializer();

        switch (ev->type) {
        case SaveEvent:
            // save the state ourselves
            serial = emulator->serialize(0);
            // pass the frame number so we can maybe use it later to get the right state
            *ev->data.save.checksum = 0; // maybe can be helpful later.
            *ev->data.save.state_len = serial.size();
            memcpy(ev->data.save.state, serial.data(), serial.size());
            break;
        case LoadEvent:
            serial = serializer(ev->data.load.state, ev->data.load.state_len);
            emulator->unserialize(serial);
            program.mute |= Mute::Always;
            emulator->setRunAhead(true);
            break;
        case AdvanceEvent:
            if (!ev->data.adv.rolling_back) {
                emulator->setRunAhead(false);
                program.mute &= ~Mute::Always;
            }
            memcpy(netplay.inputs.data(), ev->data.adv.inputs, sizeof(Netplay::Buttons) * netplay.config.num_players);
            emulator->run();
            break;
        }
    }
    return true;
}
auto Program::netplayPollLocalInput(Netplay::Buttons &localInput) -> void {
    localInput.u.value = 0;
    if (focused() || inputSettings.allowInput().checked()) {
        inputManager.poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::B)) localInput.u.btn.b = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::Y)) localInput.u.btn.y = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::Select)) localInput.u.btn.select = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::Start)) localInput.u.btn.start = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::Up)) localInput.u.btn.up = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::Down)) localInput.u.btn.down = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::Left)) localInput.u.btn.left = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::Right)) localInput.u.btn.right = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::A)) localInput.u.btn.a = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::X)) localInput.u.btn.x = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::L)) localInput.u.btn.l = mapping->poll();
        if (auto mapping = inputManager.mapping(0, 1, Netplay::SnesButton::R)) localInput.u.btn.r = mapping->poll();
    }
}

auto Program::netplayGetInput(uint port, uint device, uint button) -> int16 {
    if(device == Netplay::Device::Multitap) {
        port += (button / Netplay::SnesButton::Count);
        button = button % Netplay::SnesButton::Count;
    }

    switch (button) {
        case Netplay::SnesButton::B: return netplay.inputs[port].u.btn.b;
        case Netplay::SnesButton::Y: return netplay.inputs[port].u.btn.y;
        case Netplay::SnesButton::Select: return netplay.inputs[port].u.btn.select;
        case Netplay::SnesButton::Start: return netplay.inputs[port].u.btn.start;
        case Netplay::SnesButton::Up: return netplay.inputs[port].u.btn.up;
        case Netplay::SnesButton::Down: return netplay.inputs[port].u.btn.down;
        case Netplay::SnesButton::Left: return netplay.inputs[port].u.btn.left;
        case Netplay::SnesButton::Right: return netplay.inputs[port].u.btn.right;
        case Netplay::SnesButton::A: return netplay.inputs[port].u.btn.a;
        case Netplay::SnesButton::X: return netplay.inputs[port].u.btn.x;
        case Netplay::SnesButton::L: return netplay.inputs[port].u.btn.l;
        case Netplay::SnesButton::R: return netplay.inputs[port].u.btn.r;
    default:
        return 0;
    }
}

auto Program::netplayHaltFrame() -> void {
    auto state = emulator->serialize(0);
    emulator->run();
    state.setMode(serializer::Mode::Load);
    emulator->unserialize(state);
}

auto Program::netplayMode(Netplay::Mode mode) -> void {
    if(netplay.mode == mode) return;
    if(mode == Netplay::Running) {
        // disable input when unfocused
        inputSettings.blockInput.setChecked();
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

    // connect controllers
    emulator->connect(0, Netplay::Device::Gamepad);
    emulator->connect(1, numPlayers > 2 ? Netplay::Device::Multitap : Netplay::Device::Gamepad);

    // force deterministic emulator settings so all peers match
    emulator->configure("Hacks/Entropy", "None");
    emulator->configure("Hacks/Hotfixes", "true");
    emulator->configure("Hacks/CPU/Overclock", "100");
    emulator->configure("Hacks/CPU/FastMath", "false");
    emulator->configure("Hacks/PPU/Fast", "true");
    emulator->configure("Hacks/PPU/NoSpriteLimit", "false");
    emulator->configure("Hacks/PPU/NoVRAMBlocking", "false");
    emulator->configure("Hacks/PPU/RenderCycle", "512");
    emulator->configure("Hacks/DSP/Fast", "true");
    emulator->configure("Hacks/DSP/Cubic", "false");
    emulator->configure("Hacks/DSP/EchoShadow", "false");
    emulator->configure("Hacks/Coprocessor/DelayedSync", "true");
    emulator->configure("Hacks/Coprocessor/PreferHLE", "false");
    emulator->configure("Hacks/SA1/Overclock", "100");
    emulator->configure("Hacks/SuperFX/Overclock", "100");

    // power cycle with deterministic settings
    emulator->power();

    // calculate state size AFTER power with forced settings
    const int stateSize = emulator->serialize(0).size();

    netplay.config.num_players = numPlayers;
    netplay.config.input_size = sizeof(Netplay::Buttons);
    netplay.config.state_size = stateSize;
    netplay.config.max_spectators = spectators.size();
    netplay.config.input_prediction_window = rollback;
    netplay.config.spectator_delay = 5 * 60;

    netplay.netStats.resize(inpBufferLength);

    bool isSpectating = local >= numPlayers;

    gekko_create(&netplay.session, isSpectating ? GekkoSpectateSession : GekkoGameSession);
    gekko_start(netplay.session, &netplay.config);
    gekko_net_adapter_set(netplay.session, gekko_default_adapter(port));

    if(!isSpectating) {
        // add all players in index order
        int remoteIdx = 0;
        for(int i = 0; i < numPlayers; i++) {
            auto peer = Netplay::Peer();
            peer.nickname = {"P", i + 1};
            if(i == local) {
                peer.id = gekko_add_actor(netplay.session, GekkoLocalPlayer, nullptr);
                peer.conn.addr = "localhost";
            } else {
                peer.conn.addr = remotes[remoteIdx++];
                auto addr = GekkoNetAddress{ (void*)peer.conn.addr.data(), peer.conn.addr.size() };
                peer.id = gekko_add_actor(netplay.session, GekkoRemotePlayer, &addr);
            }
            netplay.peers.append(peer);
        }
        gekko_set_local_delay(netplay.session, local, delay);

        // add spectators
        for(int i = 0; i < spectators.size(); i++) {
            auto peer = Netplay::Peer();
            peer.nickname = "spectator";
            peer.conn.addr = spectators[i];
            auto addr = GekkoNetAddress{ (void*)peer.conn.addr.data(), peer.conn.addr.size() };
            peer.id = gekko_add_actor(netplay.session, GekkoSpectator, &addr);
            netplay.peers.append(peer);
        }
    } else {
        // spectator: connect to the host player
        auto peer = Netplay::Peer();
        peer.nickname = "P1";
        peer.conn.addr = remotes[0];
        auto addr = GekkoNetAddress{ (void*)peer.conn.addr.data(), peer.conn.addr.size() };
        peer.id = gekko_add_actor(netplay.session, GekkoRemotePlayer, &addr);
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
    netplay.speedScale = 1.0;

    netplay.peers.reset();
    netplay.inputs.reset();
    netplay.netStats.reset();

    inputSettings.pauseEmulation.setChecked();

    program.mute &= ~Mute::Always;

    // restore normal audio speed
    Emulator::audio.setSpeedScale(1.0);
}

auto Program::netplayRun() -> bool {
    if (netplay.mode != Netplay::Mode::Running) return false;

    gekko_network_poll(netplay.session);

    netplay.counter++;
    netplayTimesync();

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
        if (event->type == GekkoPlayerDisconnected) {
            auto disco = event->data.disconnected;
            showMessage({"Peer Disconnected: ", disco.handle});
            continue;
        }
        if (event->type == GekkoPlayerConnected) {
            auto conn = event->data.connected;
            showMessage({"Peer Connected: ", conn.handle});
            continue;
        }
        if (event->type == GekkoSessionStarted) {
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
        case GekkoSaveEvent:
            // save the state ourselves
            serial = emulator->serialize(0);
            // pass the frame number so we can maybe use it later to get the right state
            *ev->data.save.checksum = 0; // maybe can be helpful later.
            *ev->data.save.state_len = serial.size();
            memcpy(ev->data.save.state, serial.data(), serial.size());
            break;
        case GekkoLoadEvent:
            serial = serializer(ev->data.load.state, ev->data.load.state_len);
            emulator->unserialize(serial);
            program.mute |= Mute::Always;
            emulator->setRunAhead(true);
            break;
        case GekkoAdvanceEvent:
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

auto Program::netplayTimesync() -> void {
    const float DEADZONE = 0.5f;
    const double STRENGTH = 0.002;
    const double MIN_SPEED = 0.99;
    const double MAX_SPEED = 1.01;
    const double LERP = 0.15;

    float framesAhead = gekko_frames_ahead(netplay.session);

    double targetScale = 1.0;
    if(framesAhead >= DEADZONE || framesAhead <= -DEADZONE) {
        targetScale = 1.0 + framesAhead * STRENGTH;
        targetScale = max(MIN_SPEED, min(MAX_SPEED, targetScale));
    }

    netplay.speedScale += (targetScale - netplay.speedScale) * LERP;
    Emulator::audio.setSpeedScale(netplay.speedScale);
}

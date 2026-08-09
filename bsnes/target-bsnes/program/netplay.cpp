auto Program::netplayMode(Netplay::Mode mode) -> void {
    if(netplay.mode == mode) return;
    if(mode == Netplay::Running || mode == Netplay::Stress) {
        // disable input when unfocused
        inputSettings.blockInput.setChecked();
    }
    netplay.mode = mode;
}

auto Program::netplayApplyDeterministicSettings() -> void {
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
}

// connection-agnostic session bring-up; callers still set the adapter and add actors
auto Program::netplayBeginSession(int numPlayers, uint8 rollback, uint8 delay, int maxSpectators, bool detectDesyncs, bool isSpectating, uint spectatorDelay) -> void {
    const int inpBufferLength = numPlayers > 2 ? 5 : numPlayers;
    for(int i = 0; i < inpBufferLength; i++) {
        netplay.inputs.append(Netplay::Buttons());
    }

    // a lone player has no port 2, and inputs[] is sized to match
    emulator->connect(0, Netplay::Device::Gamepad);
    emulator->connect(1, numPlayers > 2 ? Netplay::Device::Multitap
                       : numPlayers > 1 ? Netplay::Device::Gamepad
                       : Netplay::Device::None);

    // force deterministic emulator settings so all peers match
    netplayApplyDeterministicSettings();

    // power cycle with deterministic settings
    emulator->power();

    // calculate state size AFTER power with forced settings
    const int stateSize = emulator->serialize(0).size();

    netplay.config.num_players = numPlayers;
    netplay.config.input_size = sizeof(Netplay::Buttons);
    netplay.config.state_size = stateSize;
    netplay.config.max_spectators = maxSpectators;
    netplay.config.input_prediction_window = rollback;
    netplay.config.spectator_delay = spectatorDelay;
    netplay.config.desync_detection = detectDesyncs;

    netplay.netStats.resize(inpBufferLength);
    netplayBeginDiagnostics(detectDesyncs);
    netplayReport({"session: ", emulator->title(), " players ", numPlayers,
        " rollback ", rollback, " delay ", delay, " state ", stateSize, " bytes",
        detectDesyncs ? "" : " (desync detection off)"});

    gekko_create(&netplay.session, isSpectating ? GekkoSpectateSession : GekkoGameSession);
    gekko_start(netplay.session, &netplay.config);
}

auto Program::netplayStart(uint16 port, uint8 local, uint8 rollback, uint8 delay, vector<string>& remotes, vector<string> &spectators, bool detectDesyncs) -> void {
    if(netplay.mode != Netplay::Mode::Inactive) return;

    netplay.instance = {"p", port};
    netplay.transport = Netplay::Transport::Direct;

    int numPlayers = remotes.size();

    // add local player
    if(local < 5) numPlayers++;

    bool isSpectating = local >= numPlayers;

    netplayReport({"p2p: port ", port, " local player ", local});
    netplayBeginSession(numPlayers, rollback, delay, spectators.size(), detectDesyncs, isSpectating);
    gekko_net_adapter_set(netplay.session, gekko_default_adapter(port));

    if(!isSpectating) {
        // add all players in index order
        int remoteIdx = 0;
        for(int i = 0; i < numPlayers; i++) {
            auto peer = Netplay::Peer();
            if(i == local) {
                peer.type = GekkoLocalPlayer;
                peer.id = gekko_add_actor(netplay.session, GekkoLocalPlayer, nullptr);
            } else {
                peer.type = GekkoRemotePlayer;
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
            peer.type = GekkoSpectator;
            peer.conn.addr = spectators[i];
            auto addr = GekkoNetAddress{ (void*)peer.conn.addr.data(), peer.conn.addr.size() };
            peer.id = gekko_add_actor(netplay.session, GekkoSpectator, &addr);
            netplay.peers.append(peer);
        }
    } else {
        // spectator: connect to the host player
        auto peer = Netplay::Peer();
        peer.type = GekkoRemotePlayer;
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
    netplay.transport = Netplay::Transport::Direct;
    netplay.counter = 0;
    netplay.speedScale = 1.0;

    netplay.peers.reset();
    netplay.inputs.reset();
    netplay.netStats.reset();
    netplay.stateCache.reset();
    netplay.checksumRanges.reset();
    netplayLogger.stop();

    inputSettings.pauseEmulation.setChecked();

    video.setBlocking(settings.video.blocking);
    audio.setBlocking(settings.audio.blocking);
    program.mute &= ~Mute::Always;

    // restore normal audio speed
    Emulator::audio.setSpeedScale(1.0);
}

auto Program::netplayRun() -> bool {
    if (netplay.mode != Netplay::Mode::Running && netplay.mode != Netplay::Mode::Stress) return false;

    if(netplay.mode == Netplay::Running) {
        gekko_network_poll(netplay.session);
    }

    netplay.counter++;

    if(stressTestFrameLimit && netplay.counter >= stressTestFrameLimit) {
        netplayLogger.log({"[netplay] finished after ", netplay.counter, " frames, ",
            netplay.desyncCount, " local resim desync(s), final checksum ", hex(netplay.lastChecksum, 8L), "\n"});
        netplayStop();
        quit();
        return true;
    }

    if(netplay.mode == Netplay::Running) {
        netplayTimesync();
    }

    for(int i = 0; i < netplay.peers.size(); i++) {
        auto& peer = netplay.peers[i];
        switch(peer.type) {
        case GekkoLocalPlayer: {
            Netplay::Buttons input = {};
            if(netplay.mode == Netplay::Stress) {
                input = netplayRandomInput(peer.id);
            } else {
                netplayPollLocalInput(input);
            }
            gekko_add_local_input(netplay.session, peer.id, &input);
            break;
        }
        case GekkoRemotePlayer:
            if(peer.id >= 0 && peer.id < netplay.netStats.size()) {
                gekko_network_stats(netplay.session, peer.id, &netplay.netStats[peer.id]);
            }
            break;
        case GekkoSpectator:
            break;
        }
    }
    
    int count = 0;
    auto events = gekko_session_events(netplay.session, &count);
    for(int i = 0; i < count; i++) {
        auto event = events[i];
        switch(event->type) {
        case GekkoPlayerDisconnected:
            showMessage({"Peer Disconnected: ", event->data.disconnected.handle});
            netplayLogger.log({"[netplay] peer disconnected: ", event->data.disconnected.handle, "\n"});
            break;
        case GekkoPlayerConnected:
            showMessage({"Peer Connected: ", event->data.connected.handle});
            netplayLogger.log({"[netplay] peer connected: ", event->data.connected.handle, "\n"});
            break;
        case GekkoSessionStarted:
            showMessage({"Netplay Session Started"});
            netplayLogger.log("[netplay] session started\n");
            break;
        case GekkoDesyncDetected: {
            auto& desync = event->data.desynced;
            showMessage({"Desync Detected! Frame: ", desync.frame, " Peer: ", desync.remote_handle});
            netplayReport({"cross-peer desync frame ", desync.frame, " peer ", desync.remote_handle,
                " local ", hex(desync.local_checksum, 8L), " remote ", hex(desync.remote_checksum, 8L)});

            uint slotIndex = netplay.stateCache.size() ? (uint)desync.frame % netplay.stateCache.size() : 0;
            if(netplay.stateCache.size() && netplay.stateCache[slotIndex].valid
            && netplay.stateCache[slotIndex].frame == desync.frame) {
                if(++netplay.crossPeerDesyncCount <= Netplay::DesyncDumpLimit) {
                    netplayDumpState(desync.frame, "netplay-local", desync.local_checksum, netplay.stateCache[slotIndex].data);
                }
            } else {
                netplayReport({"  frame ", desync.frame, " state no longer cached"});
            }
            break;
        }
        default:
            break;
        }
    }

    count = 0;
    auto updates = gekko_update_session(netplay.session, &count);
    for (int i = 0; i < count; i++) {
        auto ev = updates[i];
        auto serial = serializer();

        switch (ev->type) {
        case GekkoSaveEvent:
            serial = emulator->serialize(0);
            *ev->data.save.checksum = netplay.detectDesyncs ? netplayStateChecksum(serial.data(), serial.size()) : 0;
            *ev->data.save.state_len = serial.size();
            memcpy(ev->data.save.state, serial.data(), serial.size());
            netplayCacheState(ev->data.save.frame, *ev->data.save.checksum, serial.data(), serial.size());
            break;
        case GekkoLoadEvent:
            serial = serializer(ev->data.load.state, ev->data.load.state_len);
            emulator->unserialize(serial);
            program.mute |= Mute::Always;
            emulator->setRollback(true);
            break;
        case GekkoAdvanceEvent:
            if (!ev->data.adv.rolling_back) {
                emulator->setRollback(false);
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
    if(port >= netplay.inputs.size()) return 0;  // unoccupied port reads as neutral

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

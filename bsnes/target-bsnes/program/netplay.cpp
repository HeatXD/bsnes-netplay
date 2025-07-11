auto Program::netplayMode(Netplay::Mode mode) -> void {
    if(netplay.mode == mode) return;
    if(mode == Netplay::Running) {
        // be sure the contollers are connected
        emulator->connect(0, 1);
        emulator->connect(1, 1);
        // disable input when unfocused
        inputSettings.blockInput.setChecked();
        // we don't want entropy
        emulator->configure("Hacks/Entropy", "None");
        // power cycle to match all peers
        emulator->power();
    }
    netplay.mode = mode;
}

auto Program::netplayStart(uint16 port, uint8 local, uint8 rollback, uint8 delay, string remoteAddr, vector<string> &spectators) -> void {
    if(netplay.mode != Netplay::Mode::Inactive) return;

    netplay.peers.reset();
    netplay.inputs.reset();
    netplay.states.reset();
    netplay.stats = {};

    const int numPlayers = 2;

    for(int i = 0; i < numPlayers; i++) {
        netplay.inputs.append(Netplay::Buttons());
    }

    netplay.config.num_players = numPlayers;
    netplay.config.input_size = sizeof(Netplay::Buttons);
    netplay.config.state_size = sizeof(int32);
    netplay.config.max_spectators = spectators.size();
    netplay.config.input_prediction_window = rollback;
    netplay.config.spectator_delay = 90;

    netplay.states.resize(netplay.config.input_prediction_window + 2);

    gekko_create(&netplay.session);
    gekko_start(netplay.session, &netplay.config);
    gekko_net_adapter_set(netplay.session, gekko_default_adapter(port));

    if(local == 0) {
        auto peer1 = Netplay::Peer();
        auto peer2 = Netplay::Peer();

        peer1.id = gekko_add_actor(netplay.session, LocalPlayer, nullptr);
        peer1.nickname = "P1";
        peer1.conn.addr = "localhost";

        peer2.nickname = "P2";
        peer2.conn.addr = remoteAddr;
        
        auto remAddr = peer2.conn.addr;
        auto remote = GekkoNetAddress{ (void*)remAddr.data(), remAddr.size() };
        peer2.id = gekko_add_actor(netplay.session, RemotePlayer, &remote);

        netplay.peers.append(peer1);
        netplay.peers.append(peer2);

        // add the expected spectators
        for(int i = 0; i < netplay.config.max_spectators; i++) {
            auto specPeer = Netplay::Peer();
            
            specPeer.nickname = "Spectator";
            specPeer.conn.addr = spectators[i];
        
            auto specAddr = specPeer.conn.addr;
            auto spec = GekkoNetAddress{ (void*)specAddr.data(), specAddr.size() };
            specPeer.id = gekko_add_actor(netplay.session, Spectator, &spec);

            netplay.peers.append(specPeer);
        }
    } else if (local == 1){
        auto peer1 = Netplay::Peer();
        auto peer2 = Netplay::Peer();

        peer1.nickname = "P1";
        peer1.conn.addr = remoteAddr;

        auto remAddr = peer1.conn.addr;
        auto remote = GekkoNetAddress{ (void *)remAddr.data(), remAddr.size() };
        peer1.id = gekko_add_actor(netplay.session, RemotePlayer, &remote);

        peer2.id = gekko_add_actor(netplay.session, LocalPlayer, nullptr);
        peer2.nickname = "P2";
        peer2.conn.addr = "localhost";

        netplay.peers.append(peer1);
        netplay.peers.append(peer2);
    }else{
        auto peer = Netplay::Peer();

        peer.nickname = "P1";
        peer.conn.addr = remoteAddr;
        auto remAddr = peer.conn.addr;
        auto remote = GekkoNetAddress{ (void *)remAddr.data(), remAddr.size() };
        peer.id = gekko_add_actor(netplay.session, RemotePlayer, &remote);

        netplay.peers.append(peer);
    }

    gekko_set_local_delay(netplay.session, local, delay);

    netplayMode(Netplay::Running);

    netplay.poller.init(netplay.session);
    netplay.poller.start();

    netplay.localDelay = delay;
}

auto Program::netplayStop() -> void {
    if (netplay.mode == Netplay::Mode::Inactive)
        return;

    netplayMode(Netplay::Inactive);

    netplay.poller.stop();

    gekko_destroy(netplay.session);

    netplay.session = nullptr;
    netplay.config = {};

    netplay.counter = 0;
    netplay.stallCounter = 0;

    inputSettings.pauseEmulation.setChecked();
}

auto Program::netplayRun() -> bool {
    if (netplay.mode != Netplay::Mode::Running) return false;

    netplay.counter++;

    netplay.poller.with_session([this](GekkoSession* session) {
        float framesAhead = gekko_frames_ahead(session);
        if(framesAhead - netplay.localDelay >= 1.0f && netplay.counter % 180 == 0) {
            // rift syncing first attempt
            // kinda hacky. there's prolly a better way but im clueless.
            auto volume = Emulator::audio.volume();
            Emulator::audio.setVolume(volume * 0.25f);
            netplayHaltFrame();
            Emulator::audio.setVolume(volume);
            return true;
        }

        for(int i = 0; i < netplay.peers.size(); i++) {
            if(netplay.peers[i].conn.addr != "localhost"){
                if(netplay.peers[i].nickname != "Spectator"){
                    gekko_network_stats(session, netplay.peers[i].id, &netplay.stats);
                }
                continue;
            };
            Netplay::Buttons input = {};
            netplayPollLocalInput(input);
            gekko_add_local_input(session, netplay.peers[i].id, &input);
        }
        
        int count = 0;
        auto events = gekko_session_events(session, &count);
        for(int i = 0; i < count; i++) {
            auto event = events[i];
            int type = event->type;
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
        auto updates = gekko_update_session(session, &count);
        for (int i = 0; i < count; i++) {
            auto ev = updates[i];
            int frame = 0, cframe = 0;
            auto serial = serializer();

            switch (ev->type) {
            case SaveEvent:
                // save the state ourselves
                serial = emulator->serialize(0);
                frame = ev->data.save.frame % netplay.states.size();
                netplay.states[frame].set(serial.data(), serial.size());
                // pass the frame number so we can later use it to get the right state
                *ev->data.save.checksum = 0; // maybe can be helpful later.
                *ev->data.save.state_len = sizeof(int32);
                memcpy(ev->data.save.state, &ev->data.save.frame, sizeof(int32));
                break;
            case LoadEvent:
                //print("Load frame:", ev->data.load.frame, "\n");
                frame = ev->data.load.frame % netplay.states.size();
                serial = serializer(netplay.states[frame].data(), netplay.states[frame].size());
                emulator->unserialize(serial);
                program.mute |= Mute::Always;
                emulator->setRunAhead(true);
                break;
            case AdvanceEvent:
                if(emulator->runAhead()) {
                    cframe = count - 1;
                    if(cframe == i || (updates[cframe]->type == SaveEvent && i == cframe - 1)) {
                        emulator->setRunAhead(false);
                        program.mute &= ~Mute::Always;
                    }
                }
                memcpy(netplay.inputs.data(), ev->data.adv.inputs, sizeof(Netplay::Buttons) * netplay.config.num_players);
                emulator->run();
                break;
            }
        }

        // handle stalling due to various reasons including spectator wait.
        if(count == 0) {
            netplay.stallCounter++;
            if (netplay.stallCounter > 10) program.mute |= Mute::Always;
        }else{
            if(netplay.stallCounter > 10) program.mute &= ~Mute::Always;
            netplay.stallCounter = 0;
        }

        return true;
    });
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

auto Program::netplayGetInput(uint port, uint button) -> int16 {
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

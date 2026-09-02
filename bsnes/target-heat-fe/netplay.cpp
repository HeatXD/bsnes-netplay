#include "app.hpp"

#include <exception>

namespace {
constexpr uint64_t FnvBasis = 1469598103934665603ull;
constexpr uint64_t FnvPrime = 1099511628211ull;

// forced identical on every peer; without this two builds diverge from frame
// one
void applyDeterministic(EmuCore& core) {
  core.setOption("Hacks/Entropy", "None");
  core.setOption("Frontend/Hotfixes", "true");
  core.setOption("Hacks/CPU/Overclock", "100");
  core.setOption("Hacks/CPU/FastMath", "false");
  core.setOption("Hacks/PPU/Fast", "true");
  core.setOption("Hacks/PPU/NoSpriteLimit", "false");
  core.setOption("Hacks/PPU/NoVRAMBlocking", "false");
  core.setOption("Hacks/PPU/RenderCycle", "512");
  core.setOption("Hacks/DSP/Fast", "true");
  core.setOption("Hacks/DSP/Cubic", "false");
  core.setOption("Hacks/DSP/EchoShadow", "false");
  core.setOption("Hacks/Coprocessor/DelayedSync", "true");
  core.setOption("Hacks/Coprocessor/PreferHLE", "false");
  core.setOption("Hacks/SA1/Overclock", "100");
  core.setOption("Hacks/SuperFX/Overclock", "100");
}

// folds a multitap's port/button pair onto the flat player-slot input mask
int16_t buttonFromMask(uint16_t mask, int button) {
  if(button < 0 || button >= EmuCore::ButtonCount) { return 0; }
  return (mask >> button) & 1;
}

bool validDirectAddress(const std::string& address) {
  const size_t colon = address.rfind(':');
  if(colon == std::string::npos || colon == 0 || colon + 1 == address.size()) { return false; }
  if(address.find(':') != colon) { return false; }

  const std::string portText = address.substr(colon + 1);
  if(portText.size() > 5) { return false; }
  for(char c : portText) {
    if(c < '0' || c > '9') { return false; }
  }
  const int port = SDL_atoi(portText.c_str());
  if(port < 1 || port > 65535) { return false; }

  const std::string ip = address.substr(0, colon);
  if(ip.back() == '.') { return false; }
  int octets = 0;
  size_t start = 0;
  while(start < ip.size()) {
    const size_t dot = ip.find('.', start);
    const size_t end = dot == std::string::npos ? ip.size() : dot;
    if(end == start || end - start > 3) { return false; }
    int value = 0;
    for(size_t i = start; i < end; i++) {
      if(ip[i] < '0' || ip[i] > '9') { return false; }
      value = value * 10 + ip[i] - '0';
    }
    if(value > 255 || ++octets > 4) { return false; }
    if(dot == std::string::npos) { break; }
    start = dot + 1;
  }
  return octets == 4;
}
}  // namespace

void App::netplayApplyDeterministicSettings() { applyDeterministic(core); }

uint32_t App::netplayChecksum(const std::vector<uint8_t>& state) {
  if(!netplay.detectDesyncs) { return 0; }
  uint64_t hash = FnvBasis;
  // hostState ranges are cothread stacks: real memory, but never comparable
  // between two machines, so they are skipped rather than desyncing on nothing
  for(const EmuCore::StateComponent& part : core.stateMap(false)) {
    if(part.hostState) { continue; }
    for(int i = 0; i < part.size && part.offset + i < (int)state.size(); i++) {
      hash = (hash ^ state[part.offset + i]) * FnvPrime;
    }
  }
  return (uint32_t)(hash ^ (hash >> 32));
}

void App::netplayCacheState(int frame, uint32_t checksum, const std::vector<uint8_t>& state) {
  netplay.stateCache.push_back({true, frame, checksum, state});
  while((int)netplay.stateCache.size() > Netplay::StateCacheFrames) {
    netplay.stateCache.pop_front();
  }
}

void App::netplayDumpState(int frame, const char* tag, uint32_t checksum,
                           const std::vector<uint8_t>& data) {
  const std::string dir = configDir() + "Netplay/";
  const std::string path = dir + netplay.instance + "-" + tag + "-frame" + std::to_string(frame) +
                           "-" + std::to_string(checksum) + ".bst";
  if(ensureDir(dir) && writeBytes(path, data.data(), data.size())) {
    netplayLog(std::string("dumped ") + tag + " state to " + path);
  }
}

void App::netplayLog(std::string line) {
  netplay.log.push_back(line);
  while((int)netplay.log.size() > Netplay::LogLimit) { netplay.log.pop_front(); }
  showMessage(line);
}

// connection-agnostic bring-up; the caller has already set the adapter and
// still has to add actors and call netplayMode-equivalent (set netplay.mode)
void App::netplayBeginSession(int numPlayers, bool detectDesyncs, int maxSpectators,
                              bool localSpectating, int spectatorDelay, int rollbackFrames,
                              int localDelay) {
  if(scripting.running()) { scripting.stop(); }
  // GekkoNet owns rollback/runahead for the session; HeatFE's own offline
  // run-ahead would resimulate a second time on top of it, so it is parked
  // here and put back by netplayStop()
  netplay.savedRunAheadFrames = settings.runAheadFrames;
  settings.runAheadFrames = 0;

  core.connect(0, EmuCore::Gamepad);
  core.connect(1, numPlayers > 2   ? EmuCore::SuperMultitap
                  : numPlayers > 1 ? EmuCore::Gamepad
                                   : EmuCore::None);

  core.setCheats({});
  netplayApplyDeterministicSettings();
  core.power();

  const std::vector<uint8_t> initialState = core.serialize(false);
  const int stateSize = (int)SDL_max(initialState.size(), core.serialize(true).size());
  if(!initialState.empty()) { core.unserialize(initialState); }
  const int slots = numPlayers > 2 ? EmuCore::MaxPlayers + 1 : numPlayers;
  netplay.inputs.assign(slots, 0);
  netplay.detectDesyncs = detectDesyncs;
  netplay.rollback = false;
  netplay.recordInput = false;
  netplay.spectatorPaused = false;
  netplay.desyncCount = 0;
  netplay.speedScale = 1.0;
  netplay.localDelay = localDelay >= 0 ? localDelay : settings.netplayDelay;
  netplay.localRunAhead = settings.netplayRunAhead;
  netplay.stateCache.clear();
  netplay.log.clear();

  netplay.config = {};
  netplay.config.num_players = (unsigned char)numPlayers;
  netplay.config.input_size = sizeof(uint16_t);
  netplay.config.state_size = stateSize;
  const int rollback = rollbackFrames >= 0 ? rollbackFrames : settings.netplayRollback;
  netplay.config.input_prediction_window = (unsigned char)SDL_clamp(rollback, 0, 32);
  netplay.config.desync_detection = detectDesyncs;
  netplay.config.max_spectators = (unsigned char)maxSpectators;
  netplay.config.spectator_delay =
      (unsigned)(spectatorDelay >= 0 ? spectatorDelay : settings.netplaySpectatorDelay);

  netplayLog("session: " + gameTitle + " players " + std::to_string(numPlayers) + " rollback " +
             std::to_string(netplay.config.input_prediction_window) + " delay " +
             std::to_string(netplay.localDelay) + " runahead " +
             std::to_string(netplay.localRunAhead) + " state " + std::to_string(stateSize) +
             " bytes" + (detectDesyncs ? "" : " (desync detection off)"));

  gekko_create(&netplay.session, localSpectating ? GekkoSpectateSession : GekkoGameSession);
  gekko_start(netplay.session, &netplay.config);
  gekko_set_runahead(netplay.session, (unsigned char)netplay.localRunAhead);
}

void App::netplayStart(int port, int local, const std::vector<std::string>& remotes,
                       const std::vector<std::string>& spectators, int spectatorPlayers) {
  if(netplayActive()) { return; }
  if(!core.loaded()) {
    showMessage("load a game before starting netplay");
    return;
  }
  if(movieActive()) {
    showMessage("stop the movie before starting netplay");
    return;
  }

  const bool localSpectating = local < 0;
  const int numPlayers = localSpectating ? spectatorPlayers : (int)remotes.size() + 1;
  if(numPlayers < 1 || numPlayers > EmuCore::MaxPlayers + 1) { return; }
  if(localSpectating ? remotes.empty() : (local >= numPlayers)) { return; }
  if(port < 1 || port > 65535) {
    showMessage("enter a local port from 1 to 65535");
    return;
  }
  for(const std::string& address : remotes) {
    if(validDirectAddress(address)) { continue; }
    showMessage("invalid player address: " + address + " (use IPv4:port)");
    return;
  }
  for(const std::string& address : spectators) {
    if(validDirectAddress(address)) { continue; }
    showMessage("invalid spectator address: " + address + " (use IPv4:port)");
    return;
  }

  netplay.transport = Netplay::Direct;
  netplay.instance = "p" + std::to_string(port);
  netplayBeginSession(numPlayers, settings.netplayDesyncDetection,
                      localSpectating ? 0 : (int)spectators.size(), localSpectating);
  try {
    gekko_net_adapter_set(netplay.session, gekko_default_adapter((uint16_t)port));
  } catch(const std::exception& error) {
    netplayStop();
    showMessage(std::string("could not open the netplay port: ") + error.what());
    return;
  }

  if(localSpectating) {
    // connect straight to one player; that peer's own session relays us the
    // game
    NetplayPeer peer;
    peer.type = GekkoRemotePlayer;
    peer.addr = remotes.front();
    GekkoNetAddress addr{(void*)peer.addr.data(), (unsigned)peer.addr.size()};
    peer.id = gekko_add_actor(netplay.session, GekkoRemotePlayer, &addr);
    netplay.peers.push_back(peer);
    netplay.localPlayer = -1;
    netplay.localActorId = 0;
    netplay.mode = Netplay::Running;
    netplayLog("p2p: spectating, " + std::to_string(numPlayers) + " players");
    return;
  }

  int remoteIdx = 0;
  for(int i = 0; i < numPlayers; i++) {
    NetplayPeer peer;
    if(i == local) {
      peer.type = GekkoLocalPlayer;
      peer.id = gekko_add_actor(netplay.session, GekkoLocalPlayer, nullptr);
      netplay.localPlayer = i;
      netplay.localActorId = peer.id;
    } else {
      peer.type = GekkoRemotePlayer;
      peer.addr = remotes[remoteIdx++];
      GekkoNetAddress addr{(void*)peer.addr.data(), (unsigned)peer.addr.size()};
      peer.id = gekko_add_actor(netplay.session, GekkoRemotePlayer, &addr);
    }
    netplay.peers.push_back(peer);
  }
  netplaySetLocalDelay(settings.netplayDelay);

  for(const std::string& spectatorAddr : spectators) {
    NetplayPeer peer;
    peer.type = GekkoSpectator;
    peer.addr = spectatorAddr;
    GekkoNetAddress addr{(void*)peer.addr.data(), (unsigned)peer.addr.size()};
    peer.id = gekko_add_actor(netplay.session, GekkoSpectator, &addr);
    netplay.peers.push_back(peer);
  }

  netplay.mode = Netplay::Running;
  netplayLog("p2p: port " + std::to_string(port) + " local player " + std::to_string(local + 1) +
             " spectators " + std::to_string(spectators.size()));
}

void App::netplaySetLocalDelay(int frames) {
  netplay.localDelay = SDL_clamp(frames, 0, 10);
  if(netplay.session && netplay.localPlayer >= 0) {
    gekko_set_local_delay(netplay.session, netplay.localActorId, (unsigned char)netplay.localDelay);
  }
}

void App::netplaySetRunAhead(int frames) {
  netplay.localRunAhead = SDL_clamp(frames, 0, 4);
  if(netplay.session) { gekko_set_runahead(netplay.session, (unsigned char)netplay.localRunAhead); }
}

void App::netplayStop() {
  if(!netplayActive() && !netplay.session) { return; }

  netplay.mode = Netplay::Inactive;
  gekko_destroy(&netplay.session);
  netplay.session = nullptr;

  netplay.peers.clear();
  netplay.inputs.clear();
  netplay.stateCache.clear();
  netplay.config = {};
  netplay.rollback = false;
  core.setRollback(false);
  netplay.recordInput = false;
  netplay.spectatorPaused = false;

  // the ports go back to whatever the Input tab actually has configured
  core.connect(0, settings.devices[0]);
  core.connect(1, settings.devices[1]);
  pushEnhancements();
  applyCheats();
  settings.runAheadFrames = netplay.savedRunAheadFrames;
  core.setSpeedScale(1.0);
  applySpeed();
}

// bit i of the local player's mask is EmuCore::Button i; the multitap folds
// four players' worth of that same mask onto ports 1..4's worth of buttons
int16_t App::netplayGetInput(int port, int device, int input) {
  int slot = port;
  int button = input;
  if(device == EmuCore::SuperMultitap) {
    slot = 1 + input / EmuCore::ButtonCount;
    button = input % EmuCore::ButtonCount;
  }
  if(slot < 0 || slot >= (int)netplay.inputs.size()) { return 0; }
  return buttonFromMask(netplay.inputs[slot], button);
}

void App::netplayPollLocalInput() {
  if(!netplayActive() || netplay.localPlayer < 0) { return; }
  const uint16_t mask =
      input.pollButtons(0, 0, pads, settings, sample, emulatedFrames, core.refreshRate());
  gekko_add_local_input(netplay.session, netplay.localActorId, (void*)&mask);
}

void App::netplayTimesync() {
  constexpr float Deadzone = 0.5f;
  constexpr double Strength = 0.002;
  constexpr double MinSpeed = 0.99, MaxSpeed = 1.01;
  constexpr double Lerp = 0.15;

  const float framesAhead = gekko_frames_ahead(netplay.session);
  double target = 1.0;
  if(framesAhead >= Deadzone || framesAhead <= -Deadzone) {
    target = SDL_clamp(1.0 + framesAhead * Strength, MinSpeed, MaxSpeed);
  }
  netplay.speedScale += (target - netplay.speedScale) * Lerp;
  core.setSpeedScale(netplay.speedScale);
}

void App::netplayRun() {
  if(!netplayActive()) { return; }

  gekko_network_poll(netplay.session);
  netplayTimesync();
  netplayPollLocalInput();

  for(NetplayPeer& peer : netplay.peers) {
    if(peer.type == GekkoLocalPlayer) { continue; }
    gekko_network_stats(netplay.session, peer.id, &peer.stats);
  }

  int count = 0;
  GekkoSessionEvent** events = gekko_session_events(netplay.session, &count);
  for(int i = 0; i < count; i++) {
    const GekkoSessionEvent* event = events[i];
    switch(event->type) {
      case GekkoPlayerConnected:
        for(NetplayPeer& peer : netplay.peers) {
          if(peer.id == event->data.connected.handle) { peer.connected = true; }
        }
        netplayLog("peer connected: " + std::to_string(event->data.connected.handle));
        break;
      case GekkoPlayerDisconnected:
        for(NetplayPeer& peer : netplay.peers) {
          if(peer.id == event->data.disconnected.handle) { peer.connected = false; }
        }
        netplayLog("peer disconnected: " + std::to_string(event->data.disconnected.handle));
        break;
      case GekkoSessionStarted:
        netplayLog("session started");
        break;
      case GekkoSpectatorPaused:
        netplay.spectatorPaused = true;
        netplayLog("buffering the spectator delay");
        break;
      case GekkoSpectatorUnpaused:
        netplay.spectatorPaused = false;
        netplayLog("spectator playback started");
        break;
      case GekkoDesyncDetected: {
        if(!netplay.detectDesyncs) { break; }
        const auto& desync = event->data.desynced;
        netplay.desyncCount++;
        netplayLog("desync at frame " + std::to_string(desync.frame) + " with peer " +
                   std::to_string(desync.remote_handle));
        for(const NetplayStateSnapshot& snap : netplay.stateCache) {
          if(!snap.valid || snap.frame != desync.frame) { continue; }
          netplayDumpState(desync.frame, "local", desync.local_checksum, snap.data);
          break;
        }
        break;
      }
      default:
        break;
    }
  }

  count = 0;
  GekkoGameEvent** updates = gekko_update_session(netplay.session, &count);
  bool advancedTimeline = false;
  for(int i = 0; i < count; i++) {
    const GekkoGameEvent* event = updates[i];
    switch(event->type) {
      case GekkoSaveEvent: {
        std::vector<uint8_t> state;
        if(event->data.save.portable) {
          const std::vector<uint8_t> live = core.serialize(false);
          state = core.serialize(true);
          if(live.empty() || !core.unserialize(live)) {
            netplayLog("could not restore after creating spectator state");
            netplayStop();
            return;
          }
        } else {
          state = core.serialize(false);
        }
        const uint32_t checksum = event->data.save.portable ? 0 : netplayChecksum(state);
        *event->data.save.checksum = checksum;
        *event->data.save.state_len = (unsigned)state.size();
        SDL_memcpy(event->data.save.state, state.data(), state.size());
        if(netplay.detectDesyncs) { netplayCacheState(event->data.save.frame, checksum, state); }
        break;
      }
      case GekkoLoadEvent: {
        std::vector<uint8_t> state(event->data.load.state,
                                   event->data.load.state + event->data.load.state_len);
        if(netplay.localPlayer < 0) {
          const std::vector<uint8_t> local = core.serialize(false);
          if(local.size() == state.size()) {
            for(const EmuCore::StateComponent& part : core.stateMap(false)) {
              if(!part.hostState || part.offset < 0 || part.size < 0) { continue; }
              const size_t offset = (size_t)part.offset;
              const size_t size = (size_t)part.size;
              if(offset > state.size() || size > state.size() - offset) { continue; }
              SDL_memcpy(state.data() + offset, local.data() + offset, size);
            }
          }
        }
        if(!core.unserialize(state)) {
          netplayLog("could not load the synchronized netplay state");
          netplayStop();
          return;
        }
        netplay.rollback = true;
        netplay.recordInput = false;
        core.setRollback(true);
        break;
      }
      case GekkoAdvanceEvent: {
        netplay.recordInput = !event->data.adv.rolling_back && !advancedTimeline;
        if(netplay.recordInput) { advancedTimeline = true; }
        netplay.rollback = event->data.adv.rolling_back || event->data.adv.running_ahead;
        core.setRollback(netplay.rollback);
        const size_t bytes = sizeof(uint16_t) * netplay.config.num_players;
        if(bytes <= event->data.adv.input_len) {
          SDL_memcpy(netplay.inputs.data(), event->data.adv.inputs, bytes);
        }
        for(int port = 0; port < 2; port++) {
          const int device = core.connectedDevice(port);
          for(int b = 0; b < (int)core.inputs(device).size(); b++) {
            core.setInput(port, b, netplayGetInput(port, device, b));
          }
        }
        core.runFrame();
        if(netplay.recordInput) { emulatedFrames++; }
        netplay.recordInput = false;
        break;
      }
      default:
        break;
    }
  }
}

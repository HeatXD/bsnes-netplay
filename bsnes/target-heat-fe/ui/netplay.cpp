// Settings > Netplay tab and the Netplay window: direct P2P setup,
// the Weyvelength lobby, and the live session log.

#include "ui.hpp"

#include <algorithm>

namespace {
void drawDirectTab(App& app);
void drawWeyveTab(App& app);
}
void App::restoreNetplayDefaults() {
  const Settings defaults;
  settings.netplayRollback = defaults.netplayRollback;
  settings.netplayDelay = defaults.netplayDelay;
  settings.netplayRunAhead = defaults.netplayRunAhead;
  settings.netplaySpectatorDelay = defaults.netplaySpectatorDelay;
  settings.netplayDesyncDetection = defaults.netplayDesyncDetection;
  settings.weyveHost = defaults.weyveHost;
  settings.weyvePort = defaults.weyvePort;
  settings.weyveNickname = defaults.weyveNickname;
}

void App::drawNetplayTab() {
  bool dirty = false;
  ImGui::SliderInt("Rollback frames", &settings.netplayRollback, 0, 32);
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("How far the session may resimulate to hide latency.");
  if(ImGui::SliderInt("Input delay", &settings.netplayDelay, 0, 10)) {
    netplaySetLocalDelay(settings.netplayDelay);
  }
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("Frames a local input is held before use, trading lag for fewer mispredictions.");
  if(ImGui::SliderInt("Runahead", &settings.netplayRunAhead, 0, 4)) {
    netplaySetRunAhead(settings.netplayRunAhead);
  }
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("Local speculative frames used to reduce perceived input latency.");
  dirty |= ImGui::Checkbox("Desync detection", &settings.netplayDesyncDetection);
  tip("Checksums the emulated state every save frame and compares it with peers.\n"
      "Costs performance; leave off unless hunting a desync.");

  ImGui::Separator();
  ImGui::TextUnformatted("Weyvelength");
  ImGui::TextDisabled("Server and room actions are in the Netplay window.");
  ImGui::InputText("Nickname", weyveNicknameInput, sizeof(weyveNicknameInput));
  if(ImGui::IsItemDeactivatedAfterEdit()) {
    settings.weyveNickname = weyveNicknameInput;
    if(weyve.client) {
      weyve_set_name(weyve.client, settings.weyveNickname.c_str());
      weyvePublishHostListing();
    }
    dirty = true;
  }
  ImGui::TextDisabled("A room's game list is the Games tab's own library (Settings > Paths);\n"
                      "each player auto-loads their own copy by content hash.");

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##netplay")) {
    restoreNetplayDefaults();
    netplaySetLocalDelay(settings.netplayDelay);
    netplaySetRunAhead(settings.netplayRunAhead);
    dirty = true;
  }
  if(dirty) settings.save(settingsCfg);
}

namespace {

// next player number unused by the local slot or another Player-role row
int nextFreePlayerNumber(App& app) {
  for(int candidate = 1; candidate <= EmuCore::MaxPlayers + 1; candidate++) {
    if(candidate == app.netplayLocalPlayer) continue;
    bool used = false;
    for(const NetplayRemoteEntry& entry : app.netplayRemotes) {
      used |= entry.role == NetplayEntryRole::Player && entry.playerNumber == candidate;
    }
    if(!used) return candidate;
  }
  return app.netplayLocalPlayer == 1 ? 2 : 1;
}

std::string entryAddr(const NetplayRemoteEntry& entry) {
  return std::string(entry.ip) + ":" + entry.port;
}

void drawSpectateFields(App& app) {
  ImGui::InputInt("Players in session", &app.netplaySpectatorPlayers, 1, 1);
  app.tip("Must match the number of players in the session you're watching.");
  app.netplaySpectatorPlayers = SDL_clamp(app.netplaySpectatorPlayers, 1, EmuCore::MaxPlayers + 1);
  if(app.netplayRemotes.empty()) app.netplayRemotes.push_back({});
  NetplayRemoteEntry& entry = app.netplayRemotes.front();
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputText("Player's IP", entry.ip, sizeof(entry.ip));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.0f);
  ImGui::InputText("Port##spec", entry.port, sizeof(entry.port), ImGuiInputTextFlags_CharsDecimal);
  app.tip("Address of any one player in that session; the game is relayed through them.");
}

void drawHostFields(App& app) {
  ImGui::TextUnformatted("Your player:");
  app.tip("The player slot you occupy in this session.");
  for(int p = 1; p <= EmuCore::MaxPlayers + 1; p++) {
    ImGui::SameLine();
    if(ImGui::RadioButton(("Player " + std::to_string(p)).c_str(), app.netplayLocalPlayer == p)) {
      app.netplayLocalPlayer = p;
      // the slot just claimed can't also be listed as a remote player
      for(NetplayRemoteEntry& entry : app.netplayRemotes) {
        if(entry.role == NetplayEntryRole::Player && entry.playerNumber == p) {
          entry.playerNumber = nextFreePlayerNumber(app);
        }
      }
    }
  }

  ImGui::TextUnformatted("Other players and spectators:");
  app.tip("Player entries play and need a player number; Spectator entries only watch.");
  int removeIndex = -1;
  for(int i = 0; i < (int)app.netplayRemotes.size(); i++) {
    NetplayRemoteEntry& entry = app.netplayRemotes[(size_t)i];
    ImGui::PushID(i);
    ImGui::SetNextItemWidth(90.0f);
    int role = (int)entry.role;
    if(ImGui::Combo("##role", &role, "Player\0Spectator\0")) entry.role = (NetplayEntryRole)role;
    ImGui::SameLine();
    if(entry.role == NetplayEntryRole::Player) {
      ImGui::SetNextItemWidth(60.0f);
      ImGui::InputInt("##num", &entry.playerNumber, 0, 0);
      entry.playerNumber = SDL_clamp(entry.playerNumber, 1, EmuCore::MaxPlayers + 1);
      if(entry.playerNumber == app.netplayLocalPlayer) {
        // stay off the local slot rather than silently dropping the edit
        entry.playerNumber = nextFreePlayerNumber(app);
      }
      ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputText("IP##ip", entry.ip, sizeof(entry.ip));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputText("Port##port", entry.port, sizeof(entry.port), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if(ImGui::Button("Remove")) removeIndex = i;
    ImGui::PopID();
  }
  if(removeIndex >= 0) app.netplayRemotes.erase(app.netplayRemotes.begin() + removeIndex);

  if(ImGui::Button("Add Player")) {
    NetplayRemoteEntry entry;
    entry.role = NetplayEntryRole::Player;
    entry.playerNumber = nextFreePlayerNumber(app);
    app.netplayRemotes.push_back(entry);
  }
  app.tip("Adds another player slot to this session.");
  ImGui::SameLine();
  if(ImGui::Button("Add Spectator")) {
    NetplayRemoteEntry entry;
    entry.role = NetplayEntryRole::Spectator;
    app.netplayRemotes.push_back(entry);
  }
  app.tip("Adds someone who watches without controlling the game.");
}

void drawDirectTab(App& app) {
  ImGui::InputText("Local port", app.netplayPortInput, sizeof(app.netplayPortInput),
                   ImGuiInputTextFlags_CharsDecimal);
  app.tip("UDP port this instance listens on.");
  ImGui::Checkbox("Spectate a session", &app.netplayDirectSpectator);
  app.tip("Watch instead of playing; connects to one player's address below.");

  bool hasSpectator = false;
  for(const NetplayRemoteEntry& entry : app.netplayRemotes) {
    hasSpectator |= entry.role == NetplayEntryRole::Spectator;
  }
  if(!app.netplayDirectSpectator && hasSpectator) {
    ImGui::InputInt("Spectator delay", &app.settings.netplaySpectatorDelay, 60, 300);
    app.settings.netplaySpectatorDelay = SDL_clamp(app.settings.netplaySpectatorDelay, 0, 3600);
    app.tip("Frames spectators watch behind live play; 0 follows it live.");
    if(ImGui::IsItemDeactivatedAfterEdit()) app.settings.save(app.settingsCfg);
  }

  if(app.netplayDirectSpectator) drawSpectateFields(app);
  else drawHostFields(app);

  ImGui::Separator();
  const bool canStart = app.core.loaded()
                     && (!app.netplayDirectSpectator || !app.netplayRemotes.empty());
  if(!app.core.loaded()) ImGui::TextDisabled("Load a game first.");
  ImGui::BeginDisabled(!canStart);
  if(ImGui::Button("Start session")) {
    const int port = SDL_atoi(app.netplayPortInput);
    if(app.netplayDirectSpectator) {
      app.netplayStart(port, -1, {entryAddr(app.netplayRemotes.front())}, {}, app.netplaySpectatorPlayers);
    } else {
      // sort by player number, local slot included, so its index falls out of the sort
      struct Slot { int playerNumber; std::string addr; bool local; };
      std::vector<Slot> slots{{app.netplayLocalPlayer, "", true}};
      std::vector<std::string> spectators;
      for(const NetplayRemoteEntry& entry : app.netplayRemotes) {
        if(entry.role == NetplayEntryRole::Spectator) spectators.push_back(entryAddr(entry));
        else slots.push_back({entry.playerNumber, entryAddr(entry), false});
      }
      std::sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) {
        return a.playerNumber < b.playerNumber;
      });
      int local = 0;
      std::vector<std::string> remotes;
      for(int i = 0; i < (int)slots.size(); i++) {
        if(slots[(size_t)i].local) local = i; else remotes.push_back(slots[(size_t)i].addr);
      }
      app.netplayStart(port, local, remotes, spectators);
    }
  }
  ImGui::EndDisabled();
}

void drawRoomMembers(App& app) {
  if(!app.weyve.client) return;
  uint32_t count = 0;
  const uint32_t* members = weyve_members(app.weyve.client, &count);
  const bool host = weyve_is_host(app.weyve.client);
  const uint32_t selfId = weyve_id(app.weyve.client);

  if(!ImGui::BeginTable("##weyve-members", host ? 4 : 2,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) return;
  ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 90.0f);
  if(host) {
    ImGui::TableSetupColumn("##kick", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("##host", ImGuiTableColumnFlags_WidthFixed, 90.0f);
  }
  ImGui::TableHeadersRow();

  for(uint32_t i = 0; i < count; i++) {
    ImGui::PushID((int)members[i]);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    uint32_t nameLen = 0;
    const char* name = weyve_peer_name(app.weyve.client, members[i], &nameLen);
    ImGui::Text("%s%s", name ? std::string(name, nameLen).c_str() : "?",
               members[i] == selfId ? " (you)" : "");
    ImGui::TableSetColumnIndex(1);
    const std::string role = app.weyveRoleOf(members[i]);
    if(host) {
      const std::string label = app.weyveRoleLabel(role);
      if(ImGui::BeginCombo("##role", label.c_str())) {
        for(int p = 0; p < Weyve::PlayerCap; p++) {
          const std::string value = std::to_string(p);
          if(ImGui::Selectable(app.weyveRoleLabel(value).c_str(), role == value)) app.weyveSetRole(members[i], value);
        }
        if(ImGui::Selectable("Spectator", role == "spec")) app.weyveSetRole(members[i], "spec");
        ImGui::EndCombo();
      }
    } else {
      ImGui::TextUnformatted(app.weyveRoleLabel(role).c_str());
    }
    if(host) {
      ImGui::TableSetColumnIndex(2);
      if(members[i] != selfId && ImGui::Button("Kick")) app.weyveKick(members[i]);
      ImGui::TableSetColumnIndex(3);
      if(members[i] != selfId && ImGui::Button("Make host")) app.weyveTransferHost(members[i]);
    }
    ImGui::PopID();
  }
  ImGui::EndTable();
}

void drawRoomLobby(App& app) {
  uint32_t idLen = 0;
  const char* id = weyve_room_id(app.weyve.client, &idLen);
  ImGui::Text("Room %s", std::string(id, idLen).c_str());
  ImGui::SameLine();
  if(ImGui::Button("Copy code")) app.weyveCopyRoomCode();
  ImGui::SameLine();
  if(ImGui::Button("Leave room")) { app.weyveLeaveRoom(); return; }

  const bool host = weyve_is_host(app.weyve.client);
  if(host) {
    if(app.games.empty()) ImGui::TextDisabled("(no games found; check the Games tab's folder)");
    const std::string current = app.weyveRoomData("game");
    if(ImGui::BeginCombo("Game", current.empty() ? "(none)" : current.c_str())) {
      for(uint32_t i = 0; i < app.games.size(); i++) {
        if(ImGui::Selectable(app.games[i].first.c_str())) app.weyveSelectGame(i);
      }
      ImGui::EndCombo();
    }
    app.tip("Every player auto-loads this by content hash from their own Games folder.");
    bool desync = app.weyveRoomData("desync") == "1";
    if(ImGui::Checkbox("Desync detection", &desync)) {
      weyve_set_room_data(app.weyve.client, "desync", desync ? "1" : "0");
    }
    ImGui::InputInt("Spectator delay", &app.settings.netplaySpectatorDelay, 60, 300);
    app.settings.netplaySpectatorDelay = SDL_clamp(app.settings.netplaySpectatorDelay, 0, 3600);
    app.tip("Frames spectators watch behind live play, shared when the room starts; 0 follows it live.");
    if(ImGui::IsItemDeactivatedAfterEdit()) {
      app.settings.save(app.settingsCfg);
      weyve_set_room_data(app.weyve.client, "spectator_delay",
                          std::to_string(app.settings.netplaySpectatorDelay).c_str());
    }
    if(app.weyveRoomData("spectator_delay").empty()) {
      weyve_set_room_data(app.weyve.client, "spectator_delay",
                          std::to_string(app.settings.netplaySpectatorDelay).c_str());
    }
  } else {
    ImGui::Text("Game: %s", app.weyveRoomData("game").c_str());
    const std::string delay = app.weyveRoomData("spectator_delay");
    ImGui::Text("Spectator delay: %s frames", delay.empty() ? "0" : delay.c_str());
  }

  drawRoomMembers(app);

  if(host) {
    const std::string blocked = app.weyveStartBlockedReason();
    ImGui::BeginDisabled(!blocked.empty());
    if(ImGui::Button(app.netplayActive() ? "Restart session" : "Start session")) app.weyveStartGame();
    ImGui::EndDisabled();
    if(!blocked.empty()) { ImGui::SameLine(); ImGui::TextDisabled("%s", blocked.c_str()); }
    if(app.netplayActive()) { ImGui::SameLine(); if(ImGui::Button("Stop session")) app.weyveStopGame(); }
  }

  ImGui::Separator();
  ImGui::BeginChild("##weyve-log", ImVec2(0.0f, 120.0f), ImGuiChildFlags_Borders);
  for(const std::string& line : app.weyve.log) ImGui::TextWrapped("%s", line.c_str());
  if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::InputText("##chat", app.weyve.chatInput, sizeof(app.weyve.chatInput));
  ImGui::SameLine();
  if(ImGui::Button("Send") && app.weyve.chatInput[0]) {
    weyve_send_chat(app.weyve.client, app.weyve.chatInput);
    app.weyve.chatInput[0] = '\0';
  }
}

void drawWeyveTab(App& app) {
  if(!app.weyve.client) {
    ImGui::InputText("Server", app.weyveHostInput, sizeof(app.weyveHostInput));
    app.tip("Address of the Weyvelength room server.");
    ImGui::InputText("Port", app.weyvePortInput, sizeof(app.weyvePortInput), ImGuiInputTextFlags_CharsDecimal);
    if(ImGui::Button("Connect")) {
      app.settings.weyveHost = app.weyveHostInput;
      app.settings.weyvePort = SDL_atoi(app.weyvePortInput);
      app.settings.save(app.settingsCfg);
      app.weyveConnect(app.weyveHostInput, (uint16_t)SDL_atoi(app.weyvePortInput));
    }
    if(!app.weyve.lastError.empty()) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", app.weyve.lastError.c_str());
    return;
  }

  if(!app.weyveInRoom()) {
    if(ImGui::Button("Create room")) app.weyveCreateRoom(true);
    ImGui::SameLine();
    if(ImGui::Button("Disconnect")) app.weyveDisconnect();
    ImGui::Separator();
    ImGui::InputText("Room code", app.weyveJoinCode, sizeof(app.weyveJoinCode));
    ImGui::InputText("Password", app.weyveJoinPassword, sizeof(app.weyveJoinPassword), ImGuiInputTextFlags_Password);
    if(ImGui::Button("Join room")) app.weyveJoinRoom(app.weyveJoinCode, app.weyveJoinPassword);
    app.tip("Roles (player or spectator) are assigned automatically once you're in.");
    if(!app.weyve.lastError.empty()) ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", app.weyve.lastError.c_str());

    ImGui::Separator();
    if(ImGui::Button("Refresh room list")) app.weyveListRooms();
    if(ImGui::BeginTable("##weyve-rooms", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Room", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableSetupColumn("Locked", ImGuiTableColumnFlags_WidthFixed, 55.0f);
      ImGui::TableSetupColumn("##join", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableHeadersRow();
      for(size_t i = 0; i < app.weyve.roomList.size(); i++) {
        const WeyveRoomListing& room = app.weyve.roomList[i];
        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // the host name comes from Weyvelength's own identity, not a guess of ours
        const std::string host = room.host.empty() ? "(unknown host)" : room.host;
        const std::string game = room.game.empty() ? "(no game selected)" : room.game;
        ImGui::Text("%s - hosted by %s", game.c_str(), host.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%u", room.members);
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(room.passworded ? "yes" : "no");
        ImGui::TableSetColumnIndex(3);
        if(ImGui::Button("Join")) {
          SDL_strlcpy(app.weyveJoinCode, room.id.c_str(), sizeof(app.weyveJoinCode));
          if(!room.passworded) app.weyveJoinRoom(room.id, "");
        }
        if(room.passworded) app.tip("Locked; fills the code above so you can enter its password.");
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    return;
  }

  drawRoomLobby(app);
}

}  // namespace

void App::drawNetplayWindow() {
  if(!showNetplay) return;
  placeFloating(560.0f, 480.0f);
  if(!ImGui::Begin("Netplay", &showNetplay)) { ImGui::End(); return; }

  if(netplayActive()) {
    ImGui::Text("%s session active, instance %s", netplay.transport == Netplay::Direct ? "Direct" : "Weyvelength",
               netplay.instance.c_str());
    if(netplay.localPlayer < 0) {
      ImGui::TextUnformatted(netplay.spectatorPaused ? "You are spectating (buffering)"
                                                     : "You are spectating");
      tip("Playback waits until the spectator delay has buffered, then follows live play.");
    } else {
      ImGui::Text("You are player %d of %d", netplay.localPlayer + 1,
                  (int)netplay.config.num_players);
    }
    ImGui::Text("Rollback %u  spectator delay %u  desyncs %u",
               netplay.config.input_prediction_window, netplay.config.spectator_delay,
               netplay.desyncCount);
    if(netplay.localPlayer >= 0) {
      ImGui::SetNextItemWidth(180.0f);
      if(ImGui::SliderInt("Input delay##live", &settings.netplayDelay, 0, 10)) {
        netplaySetLocalDelay(settings.netplayDelay);
      }
      bool saveTuning = ImGui::IsItemDeactivatedAfterEdit();
      tip("Applies immediately to your local player.");
      ImGui::SetNextItemWidth(180.0f);
      if(ImGui::SliderInt("Runahead##live", &settings.netplayRunAhead, 0, 4)) {
        netplaySetRunAhead(settings.netplayRunAhead);
      }
      saveTuning |= ImGui::IsItemDeactivatedAfterEdit();
      tip("Applies immediately to the active netplay session.");
      if(saveTuning) settings.save(settingsCfg);
    }
    if(ImGui::BeginTable("##netplay-peers", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Peer", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 75.0f);
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 85.0f);
      ImGui::TableSetupColumn("Jitter", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableSetupColumn("Sent / received", ImGuiTableColumnFlags_WidthFixed, 110.0f);
      ImGui::TableHeadersRow();
      for(const NetplayPeer& peer : netplay.peers) {
        if(peer.type == GekkoLocalPlayer) continue;
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        std::string label = peer.addr;
        if(netplay.transport == Netplay::Weyvelength && weyve.client) {
          uint32_t length = 0;
          const char* name = weyve_peer_name(weyve.client, peer.weyveId, &length);
          label = name ? std::string(name, length) : std::to_string(peer.weyveId);
        }
        ImGui::TextUnformatted(label.empty() ? ("Peer " + std::to_string(peer.id)).c_str() : label.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(peer.type == GekkoSpectator ? "Spectator" : "Player");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(peer.connected ? "Connected" : "Waiting");
        ImGui::TableSetColumnIndex(3);
        if(peer.connected) ImGui::Text("%u / %.0f ms", peer.stats.last_ping, peer.stats.avg_ping);
        else ImGui::TextUnformatted("--");
        ImGui::TableSetColumnIndex(4);
        if(peer.connected) ImGui::Text("%.1f ms", peer.stats.jitter);
        else ImGui::TextUnformatted("--");
        ImGui::TableSetColumnIndex(5);
        if(peer.connected) ImGui::Text("%.1f / %.1f KB/s", peer.stats.kb_sent, peer.stats.kb_received);
        else ImGui::TextUnformatted("--");
      }
      ImGui::EndTable();
    }
    tip("Ping shows the latest and average RTT; traffic is sent and received kilobytes per second.");
    if(ImGui::Button("Stop session")) netplayStop();
    ImGui::Separator();
    ImGui::BeginChild("##netplay-log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    for(const std::string& line : netplay.log) ImGui::TextWrapped("%s", line.c_str());
    ImGui::EndChild();
    ImGui::End();
    return;
  }

  if(ImGui::BeginTabBar("##netplay-tabs")) {
    if(ImGui::BeginTabItem("Direct P2P")) { drawDirectTab(*this); ImGui::EndTabItem(); }
    if(ImGui::BeginTabItem("Weyvelength Rooms")) { drawWeyveTab(*this); ImGui::EndTabItem(); }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

// Settings > Netplay tab and the Netplay window: direct P2P setup,
// the Weyvelength lobby, and the live session log.

#include "ui.hpp"

#include <algorithm>

namespace {
void drawDirectTab(App& app);
void drawWeyveTab(App& app);
}  // namespace

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
  tip("Frames a local input is held before use, trading lag for fewer "
      "mispredictions.");
  if(ImGui::SliderInt("Runahead", &settings.netplayRunAhead, 0, 4)) {
    netplaySetRunAhead(settings.netplayRunAhead);
  }
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("Local speculative frames used to reduce perceived input latency.");
  dirty |= ImGui::Checkbox("Desync detection", &settings.netplayDesyncDetection);
  tip("Checksums the emulated state every save frame and compares it with "
      "peers.\n"
      "Costs performance; leave off unless hunting a desync.");

  ImGui::Separator();
  ImGui::TextUnformatted("Weyvelength");
  ImGui::InputText("Server", weyveHostInput, sizeof(weyveHostInput));
  if(ImGui::IsItemDeactivatedAfterEdit()) {
    settings.weyveHost = weyveHostInput;
    if(!weyve.client) { weyve.connectAttempted = false; }
    dirty = true;
  }
  ImGui::InputText("Port", weyvePortInput, sizeof(weyvePortInput),
                   ImGuiInputTextFlags_CharsDecimal);
  if(ImGui::IsItemDeactivatedAfterEdit()) {
    settings.weyvePort = SDL_clamp(SDL_atoi(weyvePortInput), 1, 65535);
    SDL_snprintf(weyvePortInput, sizeof(weyvePortInput), "%d", settings.weyvePort);
    if(!weyve.client) { weyve.connectAttempted = false; }
    dirty = true;
  }
  ImGui::InputText("Nickname", weyveNicknameInput, sizeof(weyveNicknameInput));
  if(ImGui::IsItemDeactivatedAfterEdit()) {
    settings.weyveNickname = weyveNicknameInput;
    if(weyve.client) {
      weyve_set_name(weyve.client, settings.weyveNickname.c_str());
      weyvePublishHostListing();
    }
    dirty = true;
  }
  ImGui::TextDisabled("The room browser connects automatically with these settings.");
  ImGui::TextDisabled(
      "A room's game list is the Games tab's own library (Settings > Paths);\n"
      "each player auto-loads their own copy by content hash.");
  ImGui::Separator();
  if(ImGui::Button("Restore defaults##netplay")) {
    restoreNetplayDefaults();
    netplaySetLocalDelay(settings.netplayDelay);
    netplaySetRunAhead(settings.netplayRunAhead);
    SDL_strlcpy(weyveHostInput, settings.weyveHost.c_str(), sizeof(weyveHostInput));
    SDL_snprintf(weyvePortInput, sizeof(weyvePortInput), "%d", settings.weyvePort);
    SDL_strlcpy(weyveNicknameInput, settings.weyveNickname.c_str(), sizeof(weyveNicknameInput));
    if(!weyve.client) { weyve.connectAttempted = false; }
    dirty = true;
  }
  if(dirty) { settings.save(settingsCfg); }
}

namespace {

// next player number unused by the local slot or another Player-role row
int nextFreePlayerNumber(App& app) {
  for(int candidate = 1; candidate <= EmuCore::MaxPlayers + 1; candidate++) {
    if(candidate == app.netplayLocalPlayer) { continue; }
    bool used = false;
    for(const NetplayRemoteEntry& entry : app.netplayRemotes) {
      used |= entry.role == NetplayEntryRole::Player && entry.playerNumber == candidate;
    }
    if(!used) { return candidate; }
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
  if(app.netplayRemotes.empty()) { app.netplayRemotes.push_back({}); }
  NetplayRemoteEntry& entry = app.netplayRemotes.front();
  ImGui::SetNextItemWidth(140.0f);
  ImGui::InputText("Player's IP", entry.ip, sizeof(entry.ip));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(70.0f);
  ImGui::InputText("Port##spec", entry.port, sizeof(entry.port), ImGuiInputTextFlags_CharsDecimal);
  app.tip(
      "Address of any one player in that session; the game is relayed through "
      "them.");
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
  app.tip(
      "Player entries play and need a player number; Spectator entries only "
      "watch.");
  int removeIndex = -1;
  for(int i = 0; i < (int)app.netplayRemotes.size(); i++) {
    NetplayRemoteEntry& entry = app.netplayRemotes[(size_t)i];
    ImGui::PushID(i);
    ImGui::SetNextItemWidth(90.0f);
    int role = (int)entry.role;
    if(ImGui::Combo("##role", &role, "Player\0Spectator\0")) {
      entry.role = (NetplayEntryRole)role;
    }
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
    ImGui::InputText("Port##port", entry.port, sizeof(entry.port),
                     ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if(ImGui::Button("Remove")) { removeIndex = i; }
    ImGui::PopID();
  }
  if(removeIndex >= 0) { app.netplayRemotes.erase(app.netplayRemotes.begin() + removeIndex); }

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

  bool saveSettings = false;
  if(ImGui::BeginTable("##direct-tuning", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(82.0f);
    ImGui::SliderInt("Rollback", &app.settings.netplayRollback, 0, 32);
    saveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    app.tip("Frames the session may roll back to correct late input.");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(72.0f);
    ImGui::SliderInt("Input delay", &app.settings.netplayDelay, 0, 10);
    saveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    app.tip("Frames of local input delay.");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(62.0f);
    ImGui::SliderInt("Runahead", &app.settings.netplayRunAhead, 0, 4);
    saveSettings |= ImGui::IsItemDeactivatedAfterEdit();
    app.tip("Local speculative frames used to reduce perceived input latency.");
    ImGui::EndTable();
  }
  saveSettings |= ImGui::Checkbox("Desync detection", &app.settings.netplayDesyncDetection);
  app.tip("Compare state checksums with peers; enable when diagnosing a desync.");
  bool hasSpectator = false;
  for(const NetplayRemoteEntry& entry : app.netplayRemotes) {
    hasSpectator |= entry.role == NetplayEntryRole::Spectator;
  }
  if(!app.netplayDirectSpectator && hasSpectator) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("Spectator delay", &app.settings.netplaySpectatorDelay, 60, 300);
    app.settings.netplaySpectatorDelay = SDL_clamp(app.settings.netplaySpectatorDelay, 0, 3600);
    app.tip(
        "Frames spectators watch behind live play; 300 is about 5 seconds, and "
        "0 is live.");
    saveSettings |= ImGui::IsItemDeactivatedAfterEdit();
  }
  if(saveSettings) { app.settings.save(app.settingsCfg); }

  if(app.netplayDirectSpectator) {
    drawSpectateFields(app);
  } else {
    drawHostFields(app);
  }

  ImGui::Separator();
  const bool canStart =
      app.core.loaded() && (!app.netplayDirectSpectator || !app.netplayRemotes.empty());
  if(!app.core.loaded()) { ImGui::TextDisabled("Load a game first."); }
  ImGui::BeginDisabled(!canStart);
  if(ImGui::Button("Start session")) {
    const int port = SDL_atoi(app.netplayPortInput);
    if(app.netplayDirectSpectator) {
      app.netplayStart(port, -1, {entryAddr(app.netplayRemotes.front())}, {},
                       app.netplaySpectatorPlayers);
    } else {
      // sort by player number, local slot included, so its index falls out of
      // the sort
      struct Slot {
        int playerNumber;
        std::string addr;
        bool local;
      };

      std::vector<Slot> slots{{app.netplayLocalPlayer, "", true}};
      std::vector<std::string> spectators;
      for(const NetplayRemoteEntry& entry : app.netplayRemotes) {
        if(entry.role == NetplayEntryRole::Spectator) {
          spectators.push_back(entryAddr(entry));
        } else {
          slots.push_back({entry.playerNumber, entryAddr(entry), false});
        }
      }
      std::sort(slots.begin(), slots.end(),
                [](const Slot& a, const Slot& b) { return a.playerNumber < b.playerNumber; });
      int local = 0;
      std::vector<std::string> remotes;
      for(int i = 0; i < (int)slots.size(); i++) {
        if(slots[(size_t)i].local) {
          local = i;
        } else {
          remotes.push_back(slots[(size_t)i].addr);
        }
      }
      app.netplayStart(port, local, remotes, spectators);
    }
  }
  ImGui::EndDisabled();
}

void drawRoomMembers(App& app) {
  if(!app.weyve.client) { return; }
  uint32_t count = 0;
  const uint32_t* members = weyve_members(app.weyve.client, &count);
  const bool host = weyve_is_host(app.weyve.client);
  const uint32_t hostId = weyve_host_id(app.weyve.client);
  const uint32_t selfId = weyve_id(app.weyve.client);
  const bool roomHasGame = !app.weyveRoomData("game_hash").empty();

  ImGui::Text("Members (%u)", count);
  const float controlsHeight =
      host ? ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y : 0.0f;
  ImGui::BeginChild("##weyve-member-list", ImVec2(0.0f, -controlsHeight), ImGuiChildFlags_Borders);
  const bool running = app.weyveSessionActive();
  if(ImGui::BeginTable("##weyve-members", running ? 3 : 2,
                       ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    if(running) { ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 58.0f); }
    ImGui::TableHeadersRow();
    for(uint32_t i = 0; i < count; i++) {
      const uint32_t memberId = members[i];
      std::string label;
      if(memberId == hostId) { label += "[host] "; }
      if(memberId == selfId) { label += "[you] "; }
      label += app.weyveNameOf(memberId);
      if(roomHasGame && app.weyveMemberData(memberId, "hasGame") != "1") { label += " (no game)"; }

      ImGui::PushID((int)memberId);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if(ImGui::Selectable(label.c_str(), app.weyve.selectedMember == memberId)) {
        app.weyve.selectedMember = memberId;
      }
      if(ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", label.c_str()); }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(app.weyveRoleLabel(app.weyveRoleOf(memberId)).c_str());
      if(running) {
        ImGui::TableNextColumn();
        const NetplayPeer* found = nullptr;
        for(const NetplayPeer& peer : app.netplay.peers) {
          if(peer.weyveId == memberId && peer.type != GekkoLocalPlayer) {
            found = &peer;
            break;
          }
        }
        if(found && found->connected) {
          ImGui::Text("%u ms", found->stats.last_ping);
          if(ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Average %.0f ms\nJitter %.1f ms\nSent %.1f KB/s\nReceived "
                "%.1f KB/s",
                found->stats.avg_ping, found->stats.jitter, found->stats.kb_sent,
                found->stats.kb_received);
          }
        } else {
          ImGui::TextUnformatted(memberId == selfId ? "local" : "--");
        }
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();

  if(!host) { return; }
  bool selectedFound = false;
  for(uint32_t i = 0; i < count; i++) { selectedFound |= members[i] == app.weyve.selectedMember; }
  ImGui::BeginDisabled(!selectedFound || app.netplayActive());
  const std::string selectedRole =
      selectedFound ? app.weyveRoleOf(app.weyve.selectedMember) : "spec";
  const float kickWidth = ImGui::CalcTextSize("Kick").x + ImGui::GetStyle().FramePadding.x * 2.0f;
  ImGui::SetNextItemWidth(-(kickWidth + ImGui::GetStyle().ItemSpacing.x));
  if(ImGui::BeginCombo("##selected-role", selectedFound ? app.weyveRoleLabel(selectedRole).c_str()
                                                        : "Select a member")) {
    if(app.weyve.selectedMember != selfId && ImGui::Selectable("Host")) {
      app.weyveTransferHost(app.weyve.selectedMember);
    }
    if(ImGui::Selectable("Spectator", selectedRole == "spec")) {
      app.weyveSetRole(app.weyve.selectedMember, "spec");
    }
    for(int p = 0; p < Weyve::PlayerCap; p++) {
      const std::string value = std::to_string(p);
      if(ImGui::Selectable(app.weyveRoleLabel(value).c_str(), selectedRole == value)) {
        app.weyveSetRole(app.weyve.selectedMember, value);
      }
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(app.weyve.selectedMember == selfId);
  if(ImGui::Button("Kick")) { app.weyveKick(app.weyve.selectedMember); }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
}

void drawRoomFeed(App& app) {
  ImGui::TextUnformatted("Room feed");
  const float chatHeight = ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("##weyve-log", ImVec2(0.0f, -chatHeight), ImGuiChildFlags_Borders);
  for(const std::string& line : app.weyve.log) { ImGui::TextWrapped("%s", line.c_str()); }
  if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) { ImGui::SetScrollHereY(1.0f); }
  ImGui::EndChild();
  const float sendWidth = ImGui::CalcTextSize("Send").x + ImGui::GetStyle().FramePadding.x * 2.0f;
  ImGui::SetNextItemWidth(-(sendWidth + ImGui::GetStyle().ItemSpacing.x));
  const bool send = ImGui::InputText("##chat", app.weyve.chatInput, sizeof(app.weyve.chatInput),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if((send || ImGui::Button("Send")) && app.weyve.chatInput[0]) {
    weyve_send_chat(app.weyve.client, app.weyve.chatInput);
    app.weyve.chatInput[0] = '\0';
  }
}

void drawWeyveHostSettings(App& app) {
  const ImVec2 parentPos = ImGui::GetWindowPos();
  const ImVec2 parentSize = ImGui::GetWindowSize();
  ImGui::SetNextWindowPos(
      ImVec2(parentPos.x + parentSize.x * 0.5f, parentPos.y + parentSize.y * 0.5f),
      ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(480.0f, 0.0f), ImGuiCond_Appearing);
  if(!ImGui::BeginPopup("Host settings")) { return; }

  const bool running = app.weyveSessionActive();
  if(ImGui::BeginTable("##weyve-room-settings", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 135.0f);
    ImGui::TableSetupColumn("##control", ImGuiTableColumnFlags_WidthStretch);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Public listing");
    ImGui::TableNextColumn();
    bool listed = weyve_room_listed(app.weyve.client);
    if(ImGui::Checkbox("Listed##room", &listed)) {
      weyve_set_room_listed(app.weyve.client, listed);
    }
    app.tip(
        "Show the room in the public browser; private rooms remain joinable by "
        "code.");

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Joining");
    ImGui::TableNextColumn();
    bool open = weyve_room_joinable(app.weyve.client);
    if(ImGui::Checkbox("Open##room", &open)) { weyve_set_room_joinable(app.weyve.client, open); }
    app.tip(running ? "Allow new members to join this session as spectators."
                    : "Allow new members to join the room.");

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("State checks");
    ImGui::TableNextColumn();
    bool desync = app.weyveRoomData("desync") == "1";
    ImGui::BeginDisabled(running);
    if(ImGui::Checkbox("Desync detection##room", &desync)) {
      weyve_set_room_data(app.weyve.client, "desync", desync ? "1" : "0");
    }
    ImGui::EndDisabled();
    app.tip(
        "Compare state checksums between players; enable only when "
        "investigating desyncs.");

    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Password");
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(running);
    const float setWidth = ImGui::CalcTextSize("Set").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetNextItemWidth(-(setWidth + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##room-password", "Empty removes it", app.weyveRoomPassword,
                             sizeof(app.weyveRoomPassword), ImGuiInputTextFlags_Password);
    app.tip("Set a join password, or leave this empty and press Set to remove it.");
    ImGui::SameLine();
    if(ImGui::Button("Set##room-password")) {
      weyve_set_room_password(app.weyve.client, app.weyveRoomPassword);
      app.weyveRoomPassword[0] = '\0';
    }
    ImGui::EndDisabled();
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Session floors");
  ImGui::BeginDisabled(running);
  if(ImGui::BeginTable("##weyve-baselines", 2, ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 135.0f);
    ImGui::TableSetupColumn("##control", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Rollback");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(180.0f);
    int rollback = app.weyve.rollbackBaseline;
    if(ImGui::InputInt("##rollback-floor", &rollback, 1, 4)) {
      app.weyveSetBaseline(rollback, app.weyve.delayBaseline);
    }
    app.tip("Minimum rollback frames each player must use.");
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Input delay");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(180.0f);
    int delay = app.weyve.delayBaseline;
    if(ImGui::InputInt("##delay-floor", &delay, 1, 2)) {
      app.weyveSetBaseline(app.weyve.rollbackBaseline, delay);
    }
    app.tip("Minimum input delay each player must use.");
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Spectator delay");
    ImGui::TableNextColumn();
    ImGui::SetNextItemWidth(180.0f);
    int spectatorDelay = app.weyve.spectatorDelay;
    if(ImGui::InputInt("##spectator-delay", &spectatorDelay, 60, 300)) {
      app.weyve.spectatorDelay = SDL_clamp(spectatorDelay, 0, 3600);
      app.settings.netplaySpectatorDelay = app.weyve.spectatorDelay;
      app.settings.save(app.settingsCfg);
      weyve_set_room_data(app.weyve.client, "spectator_delay",
                          std::to_string(app.weyve.spectatorDelay).c_str());
    }
    app.tip(
        "Frames spectators remain behind live play; 300 is about 5 seconds, "
        "and 0 is live.");
    ImGui::EndTable();
  }
  ImGui::EndDisabled();
  ImGui::EndPopup();
}

void drawWeyveLocalSettings(App& app) {
  if(ImGui::BeginTable("##weyve-local-settings", 3, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Rollback");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    int rollback = app.weyve.localRollback;
    ImGui::BeginDisabled(app.weyveSessionActive());
    if(ImGui::InputInt("##local-rollback", &rollback, 1, 4)) {
      app.weyveSetLocalRollback(rollback);
    }
    ImGui::EndDisabled();
    app.tip("Your rollback frames; cannot go below the host's floor.");
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Delay");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    int delay = app.weyve.localDelay;
    if(ImGui::InputInt("##local-delay", &delay, 1, 2)) { app.weyveSetLocalDelay(delay); }
    app.tip("Your input delay; applies live and cannot go below the host's floor.");
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Runahead");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if(ImGui::InputInt("##local-runahead", &app.settings.netplayRunAhead, 1, 2)) {
      app.settings.netplayRunAhead = SDL_clamp(app.settings.netplayRunAhead, 0, 4);
      app.netplaySetRunAhead(app.settings.netplayRunAhead);
      app.settings.save(app.settingsCfg);
    }
    app.tip("Local speculative frames; applies live and is not shared.");
    ImGui::EndTable();
  }
}

void drawRoomLobby(App& app) {
  uint32_t idLen = 0;
  const char* id = weyve_room_id(app.weyve.client, &idLen);
  ImGui::Text("Room %s", std::string(id, idLen).c_str());
  ImGui::SameLine();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float copyWidth = ImGui::CalcTextSize("Copy code").x + style.FramePadding.x * 2.0f;
  const float rescanWidth = ImGui::CalcTextSize("Rescan games").x + style.FramePadding.x * 2.0f;
  const float buttonWidth = copyWidth + style.ItemSpacing.x + rescanWidth;
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                       SDL_max(0.0f, ImGui::GetContentRegionAvail().x - buttonWidth));
  if(ImGui::Button("Copy code")) { app.weyveCopyRoomCode(); }
  ImGui::SameLine();
  if(ImGui::Button("Rescan games")) { app.weyveRescanGames(); }
  app.tip(
      "Rescan the Games folder and immediately report whether you now have the "
      "selected game.");

  const bool host = weyve_is_host(app.weyve.client);
  if(host) {
    if(app.games.empty()) { ImGui::TextDisabled("(no games found; check the Games tab's folder)"); }
    const std::string current = app.weyveRoomData("game");
    if(ImGui::BeginTable("##weyve-game-row", 3, ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableSetupColumn("##game", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("##settings", ImGuiTableColumnFlags_WidthFixed);
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted("Game");
      ImGui::TableNextColumn();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::BeginDisabled(app.weyveSessionActive());
      if(ImGui::BeginCombo("##weyve-game", current.empty() ? "(none)" : current.c_str())) {
        for(uint32_t i = 0; i < app.games.size(); i++) {
          if(ImGui::Selectable(app.games[i].first.c_str())) { app.weyveSelectGame(i); }
        }
        ImGui::EndCombo();
      }
      ImGui::EndDisabled();
      app.tip(
          "Every player auto-loads this by content hash from their own Games "
          "folder.");
      ImGui::TableNextColumn();
      if(ImGui::Button("Host settings") || app.weyve.openHostSettings) {
        app.weyve.openHostSettings = false;
        ImGui::OpenPopup("Host settings");
      }
      drawWeyveHostSettings(app);
      ImGui::EndTable();
    }
  } else {
    const std::string game = app.weyveRoomData("game");
    ImGui::TextWrapped("Game: %s", game.empty() ? "(none)" : game.c_str());
  }
  drawWeyveLocalSettings(app);

  const float footerHeight = ImGui::GetFrameHeightWithSpacing();
  const float bodyHeight = SDL_max(
      140.0f, ImGui::GetContentRegionAvail().y - footerHeight - ImGui::GetStyle().ItemSpacing.y);
  ImGui::BeginChild("##weyve-lobby-region", ImVec2(0.0f, bodyHeight), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollbar);
  if(ImGui::BeginTable("##weyve-lobby-body", 2,
                       ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_SizingStretchSame,
                       ImVec2(0.0f, 0.0f))) {
    ImGui::TableNextColumn();
    drawRoomMembers(app);
    ImGui::TableNextColumn();
    drawRoomFeed(app);
    ImGui::EndTable();
  }
  ImGui::EndChild();

  const std::string blocked = host ? app.weyveStartBlockedReason() : "";
  if(ImGui::BeginTable("##weyve-lobby-actions", 3, ImGuiTableFlags_SizingFixedFit)) {
    ImGui::TableSetupColumn("##session", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##leave", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableNextColumn();
    if(host) {
      if(app.weyveSessionActive()) {
        if(ImGui::Button("Stop")) { app.weyveStopGame(); }
      } else {
        ImGui::BeginDisabled(!blocked.empty());
        if(ImGui::Button("Start")) { app.weyveStartGame(); }
        ImGui::EndDisabled();
      }
    }
    ImGui::TableNextColumn();
    if(app.weyveSessionActive()) {
      ImGui::TextDisabled("Session running");
    } else if(host && !blocked.empty()) {
      ImGui::TextDisabled("%s", blocked.c_str());
    }
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(app.weyve.pendingLeave);
    if(ImGui::Button("Leave room")) { app.weyveLeaveRoom(); }
    ImGui::EndDisabled();
    ImGui::EndTable();
  }
}

void connectWeyveFromSettings(App& app) {
  app.weyve.connectAttempted = true;
  if(app.settings.weyveHost.empty()) {
    app.weyve.lastError = "Set a server in Settings > Netplay";
    return;
  }
  app.weyveConnect(app.settings.weyveHost, (uint16_t)app.settings.weyvePort);
}

void drawWeyveTab(App& app) {
  if(!app.weyve.client) {
    if(!app.weyve.connectAttempted) { connectWeyveFromSettings(app); }
    if(app.weyve.client) { return; }
    if(app.weyve.connecting) {
      ImGui::TextDisabled("Connecting...");
      return;
    }
    ImGui::TextColored(
        ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s",
        app.weyve.lastError.empty() ? "Could not connect" : app.weyve.lastError.c_str());
    if(ImGui::Button("Reconnect")) { connectWeyveFromSettings(app); }
    return;
  }

  if(!app.weyveInRoom()) {
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputText("Nickname", app.weyveNicknameInput, sizeof(app.weyveNicknameInput));
    if(ImGui::IsItemDeactivatedAfterEdit()) {
      app.settings.weyveNickname = app.weyveNicknameInput;
      weyve_set_name(app.weyve.client, app.settings.weyveNickname.c_str());
      app.settings.save(app.settingsCfg);
    }
    app.tip(
        "The name shown to other people in rooms and in your hosted room "
        "listing.");
    if(ImGui::Button("Create room")) { app.weyveCreateRoom(true); }
    ImGui::Separator();
    ImGui::InputText("Room code", app.weyveJoinCode, sizeof(app.weyveJoinCode));
    ImGui::InputText("Password", app.weyveJoinPassword, sizeof(app.weyveJoinPassword),
                     ImGuiInputTextFlags_Password);
    if(ImGui::Button("Join room")) { app.weyveJoinRoom(app.weyveJoinCode, app.weyveJoinPassword); }
    app.tip(
        "Roles (player or spectator) are assigned automatically once you're "
        "in.");
    if(!app.weyve.lastError.empty()) {
      ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", app.weyve.lastError.c_str());
    }

    ImGui::Separator();
    if(ImGui::Button("Refresh room list")) { app.weyveListRooms(); }
    if(ImGui::BeginTable("##weyve-rooms", 5,
                         ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                             ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("Game Name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Host name", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 65.0f);
      ImGui::TableSetupColumn("Players", ImGuiTableColumnFlags_WidthFixed, 60.0f);
      ImGui::TableSetupColumn("Locked", ImGuiTableColumnFlags_WidthFixed, 55.0f);
      ImGui::TableHeadersRow();
      for(size_t i = 0; i < app.weyve.roomList.size(); i++) {
        const WeyveRoomListing& room = app.weyve.roomList[i];
        const bool locked = !room.joinable || room.passworded;
        ImGui::PushID((int)i);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const std::string game = room.game.empty() ? "(no game selected)" : room.game;
        const bool selected = room.id == app.weyveJoinCode;
        if(ImGui::Selectable(
               game.c_str(), selected,
               ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
          SDL_strlcpy(app.weyveJoinCode, room.id.c_str(), sizeof(app.weyveJoinCode));
          if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !locked) {
            app.weyveJoinRoom(room.id, "");
          }
        }
        ImGui::TableSetColumnIndex(1);
        const std::string host = room.host.empty() ? "(unknown host)" : room.host;
        ImGui::TextUnformatted(host.c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(!room.statusKnown ? "Unknown" : room.running ? "Running" : "Lobby");
        ImGui::TableSetColumnIndex(3);
        ImGui::Text("%u", room.members);
        ImGui::TableSetColumnIndex(4);
        ImGui::TextUnformatted(locked ? "yes" : "no");
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    app.tip(
        "Select a room to fill its code; double-click an unlocked room to join "
        "it.");
    return;
  }

  drawRoomLobby(app);
}

}  // namespace

void App::drawNetplayWindow() {
  if(!showNetplay) { return; }
  if(netplayActive()) { netplayTab = netplay.transport == Netplay::Direct ? 0 : 1; }
  placeFloating(560.0f, 480.0f);
  const char* title = netplayTab == 0 ? "Direct P2P" : "Weyvelength";
  const bool visible = ImGui::Begin(title, &showNetplay);
  if(!showNetplay) {
    ImGui::End();
    weyveDisconnect();
    weyve.connectAttempted = false;
    return;
  }
  if(netplayTab == 1 && !weyve.client && !weyve.connectAttempted) {
    weyve.focusTab = true;
    connectWeyveFromSettings(*this);
  }
  if(netplayTab == 1 && !weyve.client && !weyve.lastError.empty()) { weyve.focusTab = true; }
  if(!visible) {
    ImGui::End();
    return;
  }

  const bool weyveRoomSession =
      netplayActive() && netplay.transport == Netplay::Weyvelength && weyveInRoom();
  if(netplayActive() && !weyveRoomSession) {
    ImGui::Text("%s session active, instance %s",
                netplay.transport == Netplay::Direct ? "Direct" : "Weyvelength",
                netplay.instance.c_str());
    if(netplay.localPlayer < 0) {
      ImGui::TextUnformatted(netplay.spectatorPaused ? "You are spectating (buffering)"
                                                     : "You are spectating");
      tip("Playback waits until the spectator delay has buffered, then follows "
          "live play.");
    } else {
      ImGui::Text("You are player %d of %d", netplay.localPlayer + 1,
                  (int)netplay.config.num_players);
    }
    ImGui::Text("Rollback %u  spectator delay %u  desyncs %u",
                netplay.config.input_prediction_window, netplay.config.spectator_delay,
                netplay.desyncCount);
    if(netplay.localPlayer >= 0) {
      int delay =
          netplay.transport == Netplay::Weyvelength ? weyve.localDelay : settings.netplayDelay;
      ImGui::SetNextItemWidth(180.0f);
      if(ImGui::SliderInt("Input delay##live", &delay, 0, 10)) {
        if(netplay.transport == Netplay::Weyvelength) {
          weyveSetLocalDelay(delay);
        } else {
          settings.netplayDelay = delay;
          netplaySetLocalDelay(delay);
        }
      }
      bool saveTuning = ImGui::IsItemDeactivatedAfterEdit();
      tip("Applies immediately to your local player.");
      ImGui::SetNextItemWidth(180.0f);
      if(ImGui::SliderInt("Runahead##live", &settings.netplayRunAhead, 0, 4)) {
        netplaySetRunAhead(settings.netplayRunAhead);
      }
      saveTuning |= ImGui::IsItemDeactivatedAfterEdit();
      tip("Applies immediately to the active netplay session.");
      if(saveTuning) { settings.save(settingsCfg); }
    }
    if(netplay.transport == Netplay::Weyvelength && weyve.client && weyve_is_host(weyve.client)) {
      if(ImGui::Button("Host settings")) { ImGui::OpenPopup("Host settings"); }
      drawWeyveHostSettings(*this);
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
        if(peer.type == GekkoLocalPlayer) { continue; }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        std::string label = peer.addr;
        if(netplay.transport == Netplay::Weyvelength && weyve.client) {
          label = weyveNameOf(peer.weyveId);
        }
        ImGui::TextUnformatted(label.empty() ? "Peer" : label.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(peer.type == GekkoSpectator ? "Spectator" : "Player");
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(peer.connected ? "Connected" : "Waiting");
        ImGui::TableSetColumnIndex(3);
        if(peer.connected) {
          ImGui::Text("%u / %.0f ms", peer.stats.last_ping, peer.stats.avg_ping);
        } else {
          ImGui::TextUnformatted("--");
        }
        ImGui::TableSetColumnIndex(4);
        if(peer.connected) {
          ImGui::Text("%.1f ms", peer.stats.jitter);
        } else {
          ImGui::TextUnformatted("--");
        }
        ImGui::TableSetColumnIndex(5);
        if(peer.connected) {
          ImGui::Text("%.1f / %.1f KB/s", peer.stats.kb_sent, peer.stats.kb_received);
        } else {
          ImGui::TextUnformatted("--");
        }
      }
      ImGui::EndTable();
    }
    tip("Ping shows the latest and average RTT; traffic is sent and received "
        "kilobytes per second.");
    if(ImGui::Button("Stop session")) {
      if(netplay.transport == Netplay::Weyvelength && weyve.client && weyve_is_host(weyve.client)) {
        weyveStopGame();
      } else {
        netplayStop();
      }
    }
    ImGui::Separator();
    ImGui::BeginChild("##netplay-log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
    for(const std::string& line : netplay.log) { ImGui::TextWrapped("%s", line.c_str()); }
    ImGui::EndChild();
    ImGui::End();
    return;
  }

  if(netplayTab == 0) {
    drawDirectTab(*this);
  } else {
    drawWeyveTab(*this);
  }
  ImGui::End();
}

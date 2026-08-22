// Settings > Netplay tab and the Direct P2P setup and session window.

#include "ui.hpp"

#include <algorithm>

void App::restoreNetplayDefaults() {
  const Settings defaults;
  settings.netplayRollback = defaults.netplayRollback;
  settings.netplayDelay = defaults.netplayDelay;
  settings.netplayRunAhead = defaults.netplayRunAhead;
  settings.netplaySpectatorDelay = defaults.netplaySpectatorDelay;
  settings.netplayDesyncDetection = defaults.netplayDesyncDetection;
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
  if(ImGui::Button("Restore defaults##netplay")) {
    restoreNetplayDefaults();
    netplaySetLocalDelay(settings.netplayDelay);
    netplaySetRunAhead(settings.netplayRunAhead);
    dirty = true;
  }
  if(dirty) settings.save(settingsCfg);
}

namespace {

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
      for(NetplayRemoteEntry& entry : app.netplayRemotes) {
        if(entry.role == NetplayEntryRole::Player && entry.playerNumber == p) {
          entry.playerNumber = nextFreePlayerNumber(app);
        }
      }
    }
  }

  ImGui::TextUnformatted("Other players and spectators:");
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
      if(entry.playerNumber == app.netplayLocalPlayer) entry.playerNumber = nextFreePlayerNumber(app);
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
  ImGui::SameLine();
  if(ImGui::Button("Add Spectator")) {
    NetplayRemoteEntry entry;
    entry.role = NetplayEntryRole::Spectator;
    app.netplayRemotes.push_back(entry);
  }
}

void drawDirectTab(App& app) {
  ImGui::InputText("Local port", app.netplayPortInput, sizeof(app.netplayPortInput),
                   ImGuiInputTextFlags_CharsDecimal);
  app.tip("UDP port this instance listens on.");
  ImGui::Checkbox("Spectate a session", &app.netplayDirectSpectator);

  bool hasSpectator = false;
  for(const NetplayRemoteEntry& entry : app.netplayRemotes) {
    hasSpectator |= entry.role == NetplayEntryRole::Spectator;
  }
  if(!app.netplayDirectSpectator && hasSpectator) {
    ImGui::InputInt("Spectator delay", &app.settings.netplaySpectatorDelay, 60, 300);
    app.settings.netplaySpectatorDelay = SDL_clamp(app.settings.netplaySpectatorDelay, 0, 3600);
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

}  // namespace

void App::drawNetplayWindow() {
  if(!showNetplay) return;
  placeFloating(560.0f, 480.0f);
  if(!ImGui::Begin("Netplay", &showNetplay)) { ImGui::End(); return; }

  if(!netplayActive()) {
    drawDirectTab(*this);
    ImGui::End();
    return;
  }

  ImGui::Text("Direct session active, instance %s", netplay.instance.c_str());
  if(netplay.localPlayer < 0) {
    ImGui::TextUnformatted(netplay.spectatorPaused ? "You are spectating (buffering)"
                                                   : "You are spectating");
  } else {
    ImGui::Text("You are player %d of %d", netplay.localPlayer + 1,
                (int)netplay.config.num_players);
  }
  ImGui::Text("Rollback %u  spectator delay %u  desyncs %u",
              netplay.config.input_prediction_window, netplay.config.spectator_delay,
              netplay.desyncCount);
  if(ImGui::BeginTable("##netplay-peers", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Peer", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, 75.0f);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 85.0f);
    ImGui::TableHeadersRow();
    for(const NetplayPeer& peer : netplay.peers) {
      if(peer.type == GekkoLocalPlayer) continue;
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(peer.addr.empty() ? "Peer" : peer.addr.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(peer.type == GekkoSpectator ? "Spectator" : "Player");
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(peer.connected ? "Connected" : "Waiting");
      ImGui::TableSetColumnIndex(3);
      if(peer.connected) ImGui::Text("%u / %.0f ms", peer.stats.last_ping, peer.stats.avg_ping);
      else ImGui::TextUnformatted("--");
    }
    ImGui::EndTable();
  }
  if(ImGui::Button("Stop session")) netplayStop();
  ImGui::Separator();
  ImGui::BeginChild("##netplay-log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
  for(const std::string& line : netplay.log) ImGui::TextWrapped("%s", line.c_str());
  ImGui::EndChild();
  ImGui::End();
}

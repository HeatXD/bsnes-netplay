#include "../ui.hpp"

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

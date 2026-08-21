#include "ui.hpp"

#include <algorithm>
#include <cstdlib>

namespace {
void sortCheats(std::vector<CheatEntry>& cheats) {
  std::sort(cheats.begin(), cheats.end(), [](const CheatEntry& a, const CheatEntry& b) {
    return SDL_strcasecmp(a.name.c_str(), b.name.c_str()) < 0;
  });
}
}

void App::drawCheatsWindow() {
  if(!showCheats) return;
  placeFloating(620.0f, 500.0f);
  if(!ImGui::Begin("Cheats", &showCheats)) { ImGui::End(); return; }

  if(!core.loaded()) {
    ImGui::TextDisabled("Load a game to edit its cheats.");
    ImGui::End();
    return;
  }

  if(ImGui::Checkbox("Enable cheats", &settings.cheatsEnabled)) {
    settings.save(settingsCfg);
    applyCheats();
  }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", cheatPath().c_str());
  if(ImGui::Button("Import from database...")) {
    databaseCheats = findDatabaseCheats();
    databaseCheatSelected.assign(databaseCheats.size(), true);
    if(databaseCheats.empty()) showMessage("no database cheats found for this game");
    else ImGui::OpenPopup("Cheat database");
  }

  if(ImGui::BeginTable("##cheats", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                     | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                       ImVec2(0.0f, 190.0f))) {
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 35.0f);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();
    for(int i = 0; i < (int)cheats.size(); i++) {
      CheatEntry& cheat = cheats[(size_t)i];
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      if(ImGui::Checkbox("##on", &cheat.enabled)) {
        cheatsDirty = true;
        saveCheats();
        applyCheats();
      }
      ImGui::TableSetColumnIndex(1);
      if(ImGui::Selectable(cheat.name.c_str(), cheatSelected == i,
                           ImGuiSelectableFlags_SpanAllColumns)) {
        cheatSelected = i;
        SDL_strlcpy(cheatName, cheat.name.c_str(), sizeof(cheatName));
        SDL_strlcpy(cheatCode, cheat.code.c_str(), sizeof(cheatCode));
        cheatEditEnabled = cheat.enabled;
        cheatError.clear();
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::InputText("Name", cheatName, sizeof(cheatName));
  ImGui::TextDisabled("Code(s)");
  ImGui::InputTextMultiline("##codes", cheatCode, sizeof(cheatCode), ImVec2(-1.0f, 75.0f));
  ImGui::Checkbox("Enable this cheat", &cheatEditEnabled);
  ImGui::TextDisabled(core.gameBoyLoaded()
                    ? "Game Genie, GameShark, or address=data; one code per line."
                    : "Game Genie, Pro Action Replay, or address=data; one code per line.");

  auto accept = [&](bool replace) {
    std::string name = cheatName;
    const size_t first = name.find_first_not_of(" \t");
    const size_t last = name.find_last_not_of(" \t");
    name = first == std::string::npos ? std::string() : name.substr(first, last + 1 - first);
    std::string code = cheatCode;
    if(name.empty()) { cheatError = "A cheat needs a name."; return; }
    if(!normalizeCheatCode(code)) { cheatError = "The code format is not valid for this game."; return; }
    const CheatEntry changed{name, code, cheatEditEnabled};
    if(replace && cheatSelected >= 0 && cheatSelected < (int)cheats.size()) {
      cheats[(size_t)cheatSelected] = changed;
    } else {
      cheats.push_back(changed);
    }
    sortCheats(cheats);
    cheatSelected = -1;
    for(int i = 0; i < (int)cheats.size(); i++) {
      if(cheats[(size_t)i].name == name && cheats[(size_t)i].code == code) cheatSelected = i;
    }
    SDL_strlcpy(cheatName, name.c_str(), sizeof(cheatName));
    SDL_strlcpy(cheatCode, code.c_str(), sizeof(cheatCode));
    cheatError.clear();
    cheatsDirty = true;
    saveCheats();
    applyCheats();
  };

  if(ImGui::Button("Add")) accept(false);
  ImGui::SameLine();
  ImGui::BeginDisabled(cheatSelected < 0 || cheatSelected >= (int)cheats.size());
  if(ImGui::Button("Update")) accept(true);
  ImGui::SameLine();
  if(ImGui::Button("Remove")) confirmRemoveCheat = true;
  ImGui::EndDisabled();
  ImGui::SameLine();
  if(ImGui::Button("Clear form")) {
    cheatSelected = -1;
    cheatName[0] = cheatCode[0] = 0;
    cheatEditEnabled = false;
    cheatError.clear();
  }
  if(!cheatError.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", cheatError.c_str());
    ImGui::PopStyleColor();
  }

  if(confirmRemoveCheat) ImGui::OpenPopup("Remove cheat");
  if(ImGui::BeginPopupModal("Remove cheat", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Permanently remove the selected cheat?");
    if(ImGui::Button("Remove")) {
      if(cheatSelected >= 0 && cheatSelected < (int)cheats.size()) {
        cheats.erase(cheats.begin() + cheatSelected);
        cheatsDirty = true;
        saveCheats();
        applyCheats();
      }
      cheatSelected = -1;
      confirmRemoveCheat = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) {
      confirmRemoveCheat = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowSize(ImVec2(560.0f, 380.0f), ImGuiCond_Appearing);
  if(ImGui::BeginPopupModal("Cheat database", nullptr)) {
    ImGui::Text("%d cheats found", (int)databaseCheats.size());
    if(ImGui::Button("Select all")) {
      for(size_t i = 0; i < databaseCheatSelected.size(); i++) databaseCheatSelected[i] = true;
    }
    ImGui::SameLine();
    if(ImGui::Button("Select none")) {
      for(size_t i = 0; i < databaseCheatSelected.size(); i++) databaseCheatSelected[i] = false;
    }
    if(ImGui::BeginChild("##database", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f),
                         ImGuiChildFlags_Borders)) {
      for(int i = 0; i < (int)databaseCheats.size(); i++) {
        ImGui::PushID(i);
        bool selected = databaseCheatSelected[(size_t)i];
        if(ImGui::Checkbox(databaseCheats[(size_t)i].name.c_str(), &selected)) {
          databaseCheatSelected[(size_t)i] = selected;
        }
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
    if(ImGui::Button("Add selected")) {
      for(size_t i = 0; i < databaseCheats.size(); i++) {
        if(!databaseCheatSelected[i]) continue;
        const CheatEntry& candidate = databaseCheats[i];
        bool duplicate = false;
        for(const CheatEntry& existing : cheats) {
          if(existing.code == candidate.code) { duplicate = true; break; }
        }
        if(!duplicate) cheats.push_back(candidate);
      }
      sortCheats(cheats);
      cheatsDirty = true;
      saveCheats();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::End();
}

void App::drawCheatFinderWindow() {
  if(!showCheatFinder) return;
  placeFloating(520.0f, 460.0f);
  if(!ImGui::Begin("Cheat Finder", &showCheatFinder)) { ImGui::End(); return; }
  if(!core.loaded()) {
    ImGui::TextDisabled("Load a game to search its memory.");
    ImGui::End();
    return;
  }

  ImGui::SetNextItemWidth(120.0f);
  ImGui::InputText("Value", cheatSearchValue, sizeof(cheatSearchValue));
  ImGui::SameLine();
  const char* sizes[] = {"Byte", "Word", "Long"};
  ImGui::SetNextItemWidth(80.0f);
  ImGui::Combo("##size", &cheatSearchSize, sizes, IM_ARRAYSIZE(sizes));
  ImGui::SameLine();
  const char* modes[] = {"=", "!=", ">=", "<=", ">", "<"};
  ImGui::SetNextItemWidth(55.0f);
  ImGui::Combo("##mode", &cheatSearchMode, modes, IM_ARRAYSIZE(modes));

  auto read = [&](uint32_t offset) {
    uint32_t value = 0;
    for(int byte = 0; byte <= cheatSearchSize; byte++) {
      value |= (uint32_t)core.readMemory(EmuCore::MemoryDomain::WRAM, offset + byte) << (byte * 8);
    }
    return value;
  };
  auto matches = [&](uint32_t value, uint32_t wanted) {
    if(cheatSearchMode == 0) return value == wanted;
    if(cheatSearchMode == 1) return value != wanted;
    if(cheatSearchMode == 2) return value >= wanted;
    if(cheatSearchMode == 3) return value <= wanted;
    if(cheatSearchMode == 4) return value > wanted;
    return value < wanted;
  };
  auto scan = [&] {
    std::string valueText = cheatSearchValue;
    if(!valueText.empty() && valueText[0] == '$') valueText = "0x" + valueText.substr(1);
    if(!valueText.empty() && valueText[0] == '#') valueText.erase(0, 1);
    const int base = valueText.compare(0, 2, "0x") == 0 || valueText.compare(0, 2, "0X") == 0 ? 16 : 10;
    if(base == 16) valueText.erase(0, 2);
    char* end = nullptr;
    const uint32_t mask = cheatSearchSize == 0 ? 0xff : cheatSearchSize == 1 ? 0xffff : 0xffffff;
    const uint32_t wanted = (uint32_t)std::strtoul(valueText.c_str(), &end, base) & mask;
    if(valueText.empty() || !end || *end) { showMessage("enter a decimal, $hex, or 0xhex value"); return; }
    std::vector<CheatCandidate> found;
    if(cheatCandidates.empty()) {
      const uint32_t limit = core.memorySize(EmuCore::MemoryDomain::WRAM) - cheatSearchSize;
      for(uint32_t offset = 0; offset < limit && found.size() < 4096; offset++) {
        const uint32_t value = read(offset);
        if(matches(value, wanted)) found.push_back({0x7e0000 + offset, value, cheatSearchSize});
      }
    } else {
      for(const CheatCandidate& candidate : cheatCandidates) {
        const uint32_t offset = candidate.address - 0x7e0000;
        if(offset + cheatSearchSize >= core.memorySize(EmuCore::MemoryDomain::WRAM)) continue;
        const uint32_t value = read(offset);
        if(matches(value, wanted)) found.push_back({candidate.address, value, cheatSearchSize});
      }
    }
    cheatCandidates = std::move(found);
    cheatCandidateSelected = -1;
  };

  if(ImGui::Button("Scan")) scan();
  ImGui::SameLine();
  ImGui::BeginDisabled(cheatCandidates.empty());
  if(ImGui::Button("Clear")) {
    cheatCandidates.clear();
    cheatCandidateSelected = -1;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%d candidates%s", (int)cheatCandidates.size(),
                      cheatCandidates.size() == 4096 ? " (limit reached)" : "");

  if(ImGui::BeginTable("##candidates", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                                         | ImGuiTableFlags_ScrollY,
                       ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 2.0f))) {
    ImGui::TableSetupColumn("Address");
    ImGui::TableSetupColumn("Value");
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin((int)cheatCandidates.size());
    while(clipper.Step()) {
      for(int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
        const CheatCandidate& candidate = cheatCandidates[(size_t)i];
        char address[16], value[32];
        SDL_snprintf(address, sizeof(address), "%06x", candidate.address);
        SDL_snprintf(value, sizeof(value), "%0*x (%u)", (candidate.size + 1) * 2,
                     candidate.value, candidate.value);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if(ImGui::Selectable(address, cheatCandidateSelected == i,
                             ImGuiSelectableFlags_SpanAllColumns)) cheatCandidateSelected = i;
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(value);
      }
    }
    ImGui::EndTable();
  }

  ImGui::BeginDisabled(cheatCandidateSelected < 0
                    || cheatCandidateSelected >= (int)cheatCandidates.size());
  if(ImGui::Button("Send to editor")) {
    const CheatCandidate& candidate = cheatCandidates[(size_t)cheatCandidateSelected];
    std::string codes;
    for(int byte = 0; byte <= candidate.size; byte++) {
      char code[16];
      SDL_snprintf(code, sizeof(code), "%06x=%02x", candidate.address + byte,
                   (candidate.value >> (byte * 8)) & 0xff);
      codes += (codes.empty() ? "" : "\n") + std::string(code);
    }
    showCheats = true;
    cheatSelected = -1;
    cheatName[0] = 0;
    SDL_strlcpy(cheatCode, codes.c_str(), sizeof(cheatCode));
    cheatEditEnabled = false;
  }
  ImGui::EndDisabled();
  ImGui::End();
}

#include "ui.hpp"

namespace {
std::string stateDate(int64_t value) {
  if(value == 0) { return "Empty"; }
  SDL_DateTime local;
  if(!SDL_TimeToDateTime(SDL_SECONDS_TO_NS(value), &local, true)) { return "Saved"; }
  char text[32];
  SDL_snprintf(text, sizeof(text), "%04d-%02d-%02d %02d:%02d", local.year, local.month, local.day,
               local.hour, local.minute);
  return text;
}

bool validStateName(const char* name) {
  if(!name || !*name) { return false; }
  for(const char* p = name; *p; p++) {
    if((unsigned char)*p < 32 || SDL_strchr("\\\"/:*?<>|", *p)) { return false; }
  }
  return true;
}
}  // namespace

void App::drawStateManagerWindow() {
  if(!showStateManager) { return; }
  placeFloating(560.0f, 430.0f);
  if(!ImGui::Begin("State Manager", &showStateManager)) {
    ImGui::End();
    return;
  }

  if(!core.loaded()) {
    ImGui::TextDisabled("Load a game to manage its states.");
    ImGui::End();
    return;
  }
  if(netplayActive()) {
    ImGui::TextDisabled("Loading or saving over the live machine is disabled during netplay;");
    ImGui::TextDisabled("removing or renaming a state file is still fine.");
  }

  if(ImGui::RadioButton("Managed", stateManagerManaged)) {
    stateManagerManaged = true;
    stateManagerSelection.clear();
  }
  ImGui::SameLine();
  if(ImGui::RadioButton("Quick slots", !stateManagerManaged)) {
    stateManagerManaged = false;
    stateManagerSelection.clear();
  }

  const std::vector<StateEntry> states = availableStates(stateManagerManaged);
  if(ImGui::BeginTable("##states", 2,
                       ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                           ImGuiTableFlags_Resizable,
                       ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing() * 4.0f))) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 145.0f);
    ImGui::TableHeadersRow();
    for(const StateEntry& state : states) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      const bool selected = stateManagerSelection == state.name;
      if(ImGui::Selectable(
             state.label.c_str(), selected,
             ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
        stateManagerSelection = state.name;
        SDL_strlcpy(stateManagerName, state.label.c_str(), sizeof(stateManagerName));
        if(ImGui::IsMouseDoubleClicked(0) && state.time && !netplayActive()) {
          loadState(state.name);
        }
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(stateDate(state.time).c_str());
    }
    ImGui::EndTable();
  }

  const StateEntry* selected = nullptr;
  for(const StateEntry& state : states) {
    if(state.name == stateManagerSelection) {
      selected = &state;
      break;
    }
  }
  ImGui::BeginDisabled(!selected || selected->time == 0 || netplayActive());
  if(ImGui::Button("Load")) { loadState(selected->name); }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!selected);
  if(ImGui::Button("Remove")) { confirmRemoveState = true; }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!selected || netplayActive());
  if(ImGui::Button("Save")) { saveState(selected->name); }
  ImGui::EndDisabled();

  if(stateManagerManaged) {
    ImGui::Separator();
    ImGui::SetNextItemWidth(-150.0f);
    ImGui::InputText("Name", stateManagerName, sizeof(stateManagerName));
    const bool valid = validStateName(stateManagerName);
    const std::string target = valid ? "Managed/" + std::string(stateManagerName) : std::string();
    const bool duplicate = valid && hasState(target) && target != stateManagerSelection;
    ImGui::BeginDisabled(!valid || duplicate || netplayActive());
    if(ImGui::Button("Add current")) {
      if(saveState(target)) { stateManagerSelection = target; }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!selected);
    if(ImGui::Button("Rename") && renameState(stateManagerSelection, target)) {
      stateManagerSelection = target;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if(duplicate) { ImGui::TextDisabled("A managed state already has that name."); }
  }

  if(confirmRemoveState) { ImGui::OpenPopup("Remove state"); }
  if(ImGui::BeginPopupModal("Remove state", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Permanently remove %s?", selected ? selected->label.c_str() : "this state");
    if(ImGui::Button("Remove") && selected) {
      removeState(selected->name);
      stateManagerSelection.clear();
      confirmRemoveState = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) {
      confirmRemoveState = false;
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::End();
}

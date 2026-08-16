#include "ui.hpp"

void App::restoreEmulatorDefaults() {
  const Settings defaults;
  settings.defocusPolicy = defaults.defocusPolicy;
  settings.fastForwardSpeed = defaults.fastForwardSpeed;
  settings.showStatus = defaults.showStatus;
  applySpeed();
}

void App::drawEmulatorTab() {
  bool dirty = false;
  dirty |= ImGui::Combo("When window is unfocused", &settings.defocusPolicy,
                        DefocusNames, DefocusCount);
  if(ImGui::SliderInt("Fast forward speed", &settings.fastForwardSpeed, 2, 16, "%dx")) applySpeed();
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  dirty |= ImGui::Checkbox("Show status bar", &settings.showStatus);
  ImGui::Separator();
  ImGui::TextWrapped("Save RAM goes to the saves folder under Paths, or next to the ROM.");

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##emulator")) {
    restoreEmulatorDefaults();
    dirty = true;
  }
  if(dirty) settings.save(settingsCfg);
}

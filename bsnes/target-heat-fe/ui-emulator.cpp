#include "ui.hpp"

void App::drawEmulatorTab() {
  bool dirty = false;
  dirty |= ImGui::Combo("When window is unfocused", &settings.defocusPolicy,
                        DefocusNames, DefocusCount);
  if(ImGui::SliderInt("Fast forward speed", &settings.fastForwardSpeed, 2, 16, "%dx")) applySpeed();
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  dirty |= ImGui::Checkbox("Show status bar", &settings.showStatus);
  ImGui::Separator();
  ImGui::TextWrapped("Save RAM is written next to the ROM file. Game Boy titles run only"
                     " through Super Game Boy, which needs the SGB BIOS cartridge.");
  if(dirty) settings.save(settingsCfg);
}

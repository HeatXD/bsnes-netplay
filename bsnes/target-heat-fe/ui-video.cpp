#include "ui.hpp"

void App::drawVideoTab() {
  bool dirty = false;
  dirty |= ImGui::Checkbox("Aspect correction (8:7)", &settings.aspectCorrect);
  dirty |= ImGui::Checkbox("Integer scaling (crisp, leaves borders)", &settings.integerScale);
  dirty |= ImGui::Checkbox("Linear filtering (smooths fractional scales)", &settings.linearFilter);

  static const char* scales[] = {"Fit window", "1x", "2x", "3x", "4x", "5x"};
  dirty |= ImGui::Combo("Window scale", &settings.windowScale, scales, IM_ARRAYSIZE(scales));

  ImGui::Separator();
  ImGui::Text("Output: %d x %d", shell.frameWidth, shell.frameHeight);
  if(dirty) settings.save(settingsCfg);
}

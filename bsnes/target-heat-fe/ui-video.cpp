#include "ui.hpp"

void App::drawVideoTab() {
  // core-side state (overscan/palette) starts at hardcoded defaults that
  // match Settings's own defaults; push a loaded settings file's actual
  // values in on the tab's first draw, not just on the next slider tweak
  static bool synced = false;
  if(!synced) {
    synced = true;
    core.setOverscanCrop(settings.overscanCrop);
    core.setPaletteAdjust(settings.videoGamma, settings.videoLuminance, settings.videoSaturation);
  }

  bool dirty = false;
  dirty |= ImGui::Checkbox("Aspect correction (8:7)", &settings.aspectCorrect);
  dirty |= ImGui::Checkbox("Integer scaling (crisp, leaves borders)", &settings.integerScale);
  dirty |= ImGui::Checkbox("Linear filtering (smooths fractional scales)", &settings.linearFilter);

  static const char* scales[] = {"Fit window", "1x", "2x", "3x", "4x", "5x"};
  dirty |= ImGui::Combo("Window scale", &settings.windowScale, scales, IM_ARRAYSIZE(scales));

  ImGui::Separator();
  if(ImGui::Checkbox("Crop overscan (8 lines top/bottom)", &settings.overscanCrop)) {
    core.setOverscanCrop(settings.overscanCrop);
    dirty = true;
  }

  ImGui::Separator();
  ImGui::TextUnformatted("Color Adjustment");
  bool paletteDirty = false;
  paletteDirty |= ImGui::SliderInt("Gamma", &settings.videoGamma, 100, 200, "%d%%");
  paletteDirty |= ImGui::SliderInt("Luminance", &settings.videoLuminance, 0, 100, "%d%%");
  paletteDirty |= ImGui::SliderInt("Saturation", &settings.videoSaturation, 0, 200, "%d%%");
  if(paletteDirty) {
    core.setPaletteAdjust(settings.videoGamma, settings.videoLuminance, settings.videoSaturation);
    dirty = true;
  }

  ImGui::Separator();
  ImGui::Text("Output: %d x %d", shell.frameWidth, shell.frameHeight);
  if(dirty) settings.save(settingsCfg);
}

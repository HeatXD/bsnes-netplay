#include "ui.hpp"

void App::restoreVideoDefaults() {
  const Settings defaults;
  settings.aspectCorrect = defaults.aspectCorrect;
  settings.outputMode = defaults.outputMode;
  settings.linearFilter = defaults.linearFilter;
  settings.windowScale = defaults.windowScale;
  settings.overscanCrop = defaults.overscanCrop;
  settings.hiresBlur = defaults.hiresBlur;
  settings.videoGamma = defaults.videoGamma;
  settings.videoLuminance = defaults.videoLuminance;
  settings.videoSaturation = defaults.videoSaturation;
  settings.videoFilter = defaults.videoFilter;
  core.setOverscanCrop(settings.overscanCrop);
  core.setOption("Video/BlurEmulation", flag(settings.hiresBlur));
  core.setPaletteAdjust(settings.videoGamma, settings.videoLuminance, settings.videoSaturation);
  core.setFilter(settings.videoFilter);
}

void App::drawVideoTab() {
  bool dirty = false;
  dirty |= ImGui::Checkbox("Aspect correction (8:7)", &settings.aspectCorrect);
  dirty |= ImGui::Checkbox("Linear filtering (smooths fractional scales)", &settings.linearFilter);
  dirty |= ImGui::Combo("Output", &settings.outputMode, OutputNames, OutputCount);

  if(ImGui::BeginCombo("Window scale",
                       windowScaleLabel(settings.windowScale, settings).c_str())) {
    for(int scale = 0; scale <= shell.maxScale(settings); scale++) {
      const std::string label = windowScaleLabel(scale, settings);
      if(ImGui::Selectable(label.c_str(), settings.windowScale == scale)) setWindowScale(scale);
    }
    ImGui::EndCombo();
  }
  if(ImGui::Button("Shrink window to size")) shell.shrinkToFit(settings);

  ImGui::Separator();
  if(ImGui::Checkbox("Crop overscan (8 lines top/bottom)", &settings.overscanCrop)) {
    core.setOverscanCrop(settings.overscanCrop);
    dirty = true;
  }
  if(ImGui::Checkbox("Hires blur emulation (blends 512-wide modes)", &settings.hiresBlur)) {
    core.setOption("Video/BlurEmulation", flag(settings.hiresBlur));
    dirty = true;
  }

  ImGui::Separator();
  ImGui::TextDisabled("Color Adjustment");
  bool paletteDirty = false;
  paletteDirty |= ImGui::SliderInt("Gamma", &settings.videoGamma, 100, 200, "%d%%");
  paletteDirty |= ImGui::SliderInt("Luminance", &settings.videoLuminance, 0, 100, "%d%%");
  paletteDirty |= ImGui::SliderInt("Saturation", &settings.videoSaturation, 0, 200, "%d%%");
  if(paletteDirty) {
    core.setPaletteAdjust(settings.videoGamma, settings.videoLuminance, settings.videoSaturation);
    dirty = true;
  }

  ImGui::Separator();
  const std::vector<std::string> filterNames = core.filterNames();
  std::vector<const char*> items;
  int current = 0;
  for(size_t i = 0; i < filterNames.size(); i++) {
    items.push_back(filterNames[i].c_str());
    if(filterNames[i] == settings.videoFilter) current = (int)i;
  }
  if(ImGui::Combo("Filter", &current, items.data(), (int)items.size())) {
    settings.videoFilter = filterNames[current];
    core.setFilter(settings.videoFilter);
    dirty = true;
  }

  ImGui::Separator();
  ImGui::Text("Console output: %d x %d", shell.frameWidth, shell.frameHeight);
  ImGui::Text("Window output: %d x %d", shell.drawWidth, shell.drawHeight);

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##video")) {
    restoreVideoDefaults();
    dirty = true;
  }
  if(dirty) settings.save(settingsCfg);
}

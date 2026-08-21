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
  settings.videoShader = defaults.videoShader;
  settings.shaderParams = defaults.shaderParams;
  settings.videoDimming = defaults.videoDimming;
  settings.screenshotLua = defaults.screenshotLua;
  settings.displayName = defaults.displayName;
  core.setOverscanCrop(settings.overscanCrop);
  core.setOption("Video/BlurEmulation", flag(settings.hiresBlur));
  core.setPaletteAdjust(settings.videoGamma, settings.videoLuminance, settings.videoSaturation);
  core.setFilter(settings.videoFilter);
  applyShader();
}

void App::drawShaderSection(bool& dirty) {
  ImGui::TextDisabled("Shader");
  if(!shell.shader.supported()) {
    ImGui::TextWrapped("This driver has no OpenGL 3.2, so shaders are unavailable.");
    return;
  }

  const std::string dir = shadersDir();
  const std::string current = settings.videoShader.empty() ? "None"
                            : shaderLabel(settings.videoShader);
  if(ImGui::BeginCombo("Shader", current.c_str())) {
    if(ImGui::Selectable("None", settings.videoShader.empty())) {
      settings.videoShader.clear();
      settings.shaderParams.clear();
      applyShader();
      dirty = true;
    }
    for(const std::string& folder : shaderList(dir)) {
      const std::string path = normalPath(dir + "/" + folder);
      if(ImGui::Selectable(shaderLabel(folder).c_str(), settings.videoShader == path)) {
        settings.videoShader = path;
        settings.shaderParams.clear();
        applyShader();
        dirty = true;
      }
    }
    ImGui::EndCombo();
  }
  tip("Multipass GLSL packages from the Shaders folder, in bsnes' own format.");

  if(!settings.videoShader.empty() && ImGui::Button("Reload##shader")) {
    applyShader();
    if(shell.shader.active()) showMessage("shader reloaded");
  }

  if(!shell.shader.failure.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
    ImGui::TextWrapped("%s", shell.shader.failure.c_str());
    ImGui::PopStyleColor();
    if(!shell.shader.log.empty()) ImGui::TextWrapped("%s", shell.shader.log.c_str());
    return;
  }
  if(!shell.shader.active()) return;

  if(shell.shader.outputWidth > 0) {
    ImGui::Text("%d passes, output %d x %d", (int)shell.shader.passCount(),
                shell.shader.outputWidth, shell.shader.outputHeight);
  } else {
    ImGui::Text("%d passes", (int)shell.shader.passCount());
  }

  if(shell.shader.params.empty()) return;
  ImGui::TextDisabled("Parameters");
  // reloading replaces the parameter list, so the edit is applied after the loop
  int edited = -1;
  std::string value;
  for(size_t i = 0; i < shell.shader.params.size(); i++) {
    const ShaderParam& param = shell.shader.params[i];
    char buffer[64];
    SDL_strlcpy(buffer, param.value.c_str(), sizeof(buffer));
    ImGui::PushID((int)i);
    if(ImGui::InputText(param.name.c_str(), buffer, sizeof(buffer),
                        ImGuiInputTextFlags_EnterReturnsTrue)) {
      edited = (int)i;
      value = buffer;
    }
    ImGui::PopID();
  }
  ImGui::TextWrapped("Press Enter to rebuild the chain with the new value.");
  if(ImGui::Button("Reset parameters")) {
    settings.shaderParams.clear();
    applyShader();
    dirty = true;
  }
  if(edited >= 0) {
    shell.shader.params[(size_t)edited].value = value;
    saveShaderParams();
    applyShader();
    dirty = true;
  }
}

void App::drawVideoTab() {
  bool dirty = false;
  bool geometryDirty = ImGui::Checkbox("Aspect correction (8:7)", &settings.aspectCorrect);
  dirty |= geometryDirty;
  dirty |= ImGui::Checkbox("Linear filtering (smooths fractional scales)", &settings.linearFilter);
  dirty |= ImGui::Combo("Output", &settings.outputMode, OutputNames, OutputCount);

  if(ImGui::BeginCombo("Window scale",
                       windowScaleLabel(settings.windowScale, settings).c_str())) {
    const int maxScale = shell.maxScale(settings);
    for(int scale = 0; scale <= maxScale; scale++) {
      const std::string label = windowScaleLabel(scale, settings);
      if(ImGui::Selectable(label.c_str(), settings.windowScale == scale)) setWindowScale(scale);
    }
    ImGui::EndCombo();
  }
  const char* displayLabel = settings.displayName.empty() ? "Follow the window"
                                                          : settings.displayName.c_str();
  if(ImGui::BeginCombo("Display", displayLabel)) {
    if(ImGui::Selectable("Follow the window", settings.displayName.empty())) {
      settings.displayName.clear();
      dirty = true;
    }
    // two identical monitors report the same name, so the row index owns the id
    const std::vector<std::string> displays = Shell::listDisplays();
    for(int i = 0; i < (int)displays.size(); i++) {
      ImGui::PushID(i);
      if(ImGui::Selectable(displays[i].c_str(), settings.displayName == displays[i])) {
        settings.displayName = displays[i];
        shell.moveToDisplay(settings);
        dirty = true;
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  tip("Which monitor the window and fullscreen use.");

  if(ImGui::Button("Shrink window to size")) shell.shrinkToFit(settings);
  ImGui::SameLine();
  if(ImGui::Button("Center window")) shell.center(settings);

  ImGui::Separator();
  if(ImGui::Checkbox("Crop overscan (8 lines top/bottom)", &settings.overscanCrop)) {
    core.setOverscanCrop(settings.overscanCrop);
    geometryDirty = true;
    dirty = true;
  }
  if(ImGui::Checkbox("Hires blur emulation (blends 512-wide modes)", &settings.hiresBlur)) {
    core.setOption("Video/BlurEmulation", flag(settings.hiresBlur));
    dirty = true;
  }

  ImGui::Separator();
  dirty |= ImGui::Checkbox("Dim video when idle", &settings.videoDimming);
  tip("Halves the brightness while emulation is paused or stopped.");
  dirty |= ImGui::Checkbox("Include Lua drawings in screenshots", &settings.screenshotLua);
  tip("Captures the displayed game and Lua overlay without menus or tool windows.");

  ImGui::Separator();
  ImGui::TextDisabled("Colour Adjustment");
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
  ImGui::BeginDisabled(shell.shader.active());
  if(ImGui::Combo("Filter", &current, items.data(), (int)items.size())) {
    settings.videoFilter = filterNames[current];
    applyVideoFilter();
    dirty = true;
  }
  ImGui::EndDisabled();
  tip(shell.shader.active() ? "Saved but bypassed while a shader is active."
                            : "CPU filter used when no GLSL shader is active.");

  ImGui::Separator();
  drawShaderSection(dirty);

  ImGui::Separator();
  ImGui::Text("Console output: %d x %d", shell.frameWidth, shell.frameHeight);
  ImGui::Text("Window output: %d x %d", shell.drawWidth, shell.drawHeight);

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##video")) {
    restoreVideoDefaults();
    dirty = true;
  }
  if(geometryDirty && settings.windowScale > 0) shell.shrinkToFit(settings);
  if(dirty) settings.save(settingsCfg);
}

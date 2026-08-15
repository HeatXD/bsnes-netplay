#include "ui.hpp"

namespace {
void push(App& app, const char* key, bool value) {
  app.core.setOption(key, value ? "true" : "false");
}
}

void App::restoreEnhancementDefaults() {
  const Settings defaults;
  settings.hackPpuFast = defaults.hackPpuFast;
  settings.hackPpuNoSpriteLimit = defaults.hackPpuNoSpriteLimit;
  settings.hackMode7Scale = defaults.hackMode7Scale;
  settings.hackDspFast = defaults.hackDspFast;
  settings.hackDspCubic = defaults.hackDspCubic;
  settings.hackCoprocessorDelayedSync = defaults.hackCoprocessorDelayedSync;
  settings.hackCoprocessorPreferHLE = defaults.hackCoprocessorPreferHLE;
  settings.hackHotfixes = defaults.hackHotfixes;
  push(*this, "Hacks/PPU/Fast", settings.hackPpuFast);
  push(*this, "Hacks/PPU/NoSpriteLimit", settings.hackPpuNoSpriteLimit);
  core.setOption("Hacks/PPU/Mode7/Scale", std::to_string(settings.hackMode7Scale));
  push(*this, "Hacks/DSP/Fast", settings.hackDspFast);
  push(*this, "Hacks/DSP/Cubic", settings.hackDspCubic);
  push(*this, "Hacks/Coprocessor/DelayedSync", settings.hackCoprocessorDelayedSync);
  push(*this, "Hacks/Coprocessor/PreferHLE", settings.hackCoprocessorPreferHLE);
  push(*this, "Frontend/Hotfixes", settings.hackHotfixes);
}

void App::drawEnhancementsTab() {
  ImGui::TextDisabled("PPU (video)");
  if(ImGui::Checkbox("Fast mode##ppu", &settings.hackPpuFast)) push(*this, "Hacks/PPU/Fast", settings.hackPpuFast);
  ImGui::SameLine();
  ImGui::TextDisabled("(applies on next power cycle or game load)");
  if(ImGui::Checkbox("No sprite limit", &settings.hackPpuNoSpriteLimit)) {
    push(*this, "Hacks/PPU/NoSpriteLimit", settings.hackPpuNoSpriteLimit);
  }
  if(ImGui::SliderInt("Mode 7 scale", &settings.hackMode7Scale, 1, 8)) {
    core.setOption("Hacks/PPU/Mode7/Scale", std::to_string(settings.hackMode7Scale));
  }

  ImGui::Spacing();
  ImGui::TextDisabled("DSP (audio)");
  if(ImGui::Checkbox("Fast mode##dsp", &settings.hackDspFast)) push(*this, "Hacks/DSP/Fast", settings.hackDspFast);
  if(ImGui::Checkbox("Cubic interpolation", &settings.hackDspCubic)) {
    push(*this, "Hacks/DSP/Cubic", settings.hackDspCubic);
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Coprocessors");
  if(ImGui::Checkbox("Fast mode##coprocessor", &settings.hackCoprocessorDelayedSync)) {
    push(*this, "Hacks/Coprocessor/DelayedSync", settings.hackCoprocessorDelayedSync);
  }
  if(ImGui::Checkbox("Prefer HLE", &settings.hackCoprocessorPreferHLE)) {
    push(*this, "Hacks/Coprocessor/PreferHLE", settings.hackCoprocessorPreferHLE);
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Compatibility");
  if(ImGui::Checkbox("Per-title hotfixes", &settings.hackHotfixes)) {
    push(*this, "Frontend/Hotfixes", settings.hackHotfixes);
  }
  const std::string hotfix = core.activeHotfix();
  if(!hotfix.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Active: %s", hotfix.c_str());
  }

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##enhancements")) {
    restoreEnhancementDefaults();
    settings.save(settingsCfg);
  }
}

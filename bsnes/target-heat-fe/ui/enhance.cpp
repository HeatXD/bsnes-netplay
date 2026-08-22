#include "ui.hpp"

void App::restoreEnhancementDefaults() {
  const Settings defaults;
  settings.hackPpuFast = defaults.hackPpuFast;
  settings.hackPpuDeinterlace = defaults.hackPpuDeinterlace;
  settings.hackPpuNoSpriteLimit = defaults.hackPpuNoSpriteLimit;
  settings.hackMode7Scale = defaults.hackMode7Scale;
  settings.hackMode7Perspective = defaults.hackMode7Perspective;
  settings.hackMode7Supersample = defaults.hackMode7Supersample;
  settings.hackMode7Mosaic = defaults.hackMode7Mosaic;
  settings.hackDspFast = defaults.hackDspFast;
  settings.hackDspCubic = defaults.hackDspCubic;
  settings.hackCoprocessorDelayedSync = defaults.hackCoprocessorDelayedSync;
  settings.hackCoprocessorPreferHLE = defaults.hackCoprocessorPreferHLE;
  settings.hackCpuOverclock = defaults.hackCpuOverclock;
  settings.hackSa1Overclock = defaults.hackSa1Overclock;
  settings.hackSuperFxOverclock = defaults.hackSuperFxOverclock;
  settings.hackHotfixes = defaults.hackHotfixes;
}

void App::drawEnhancementsTab() {
  bool dirty = false;

  if(netplayActive()) {
    ImGui::TextDisabled("Changes here are queued; netplay holds these hacks at their");
    ImGui::TextDisabled("deterministic values until the session ends.");
  }
  ImGui::TextDisabled("Overclocking");
  ImGui::SliderInt("CPU", &settings.hackCpuOverclock, 100, 400, "%d%%");
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("Reduces slowdown; breaks games that depend on the real chip's timing.");
  ImGui::SliderInt("SA-1", &settings.hackSa1Overclock, 100, 400, "%d%%");
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt("SuperFX", &settings.hackSuperFxOverclock, 100, 800, "%d%%");
  dirty |= ImGui::IsItemDeactivatedAfterEdit();

  ImGui::Spacing();
  ImGui::TextDisabled("PPU (video)");
  dirty |= ImGui::Checkbox("Fast mode##ppu", &settings.hackPpuFast);
  ImGui::SameLine();
  ImGui::TextDisabled("(applies on next power cycle or game load)");
  ImGui::BeginDisabled(!settings.hackPpuFast);
  dirty |= ImGui::Checkbox("Deinterlace", &settings.hackPpuDeinterlace);
  tip("Renders interlaced modes at full height instead of one field at a time.");
  dirty |= ImGui::Checkbox("No sprite limit", &settings.hackPpuNoSpriteLimit);
  ImGui::SliderInt("Mode 7 scale", &settings.hackMode7Scale, 1, 8, "%dx");
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  dirty |= ImGui::Checkbox("Mode 7 perspective correction", &settings.hackMode7Perspective);
  dirty |= ImGui::Checkbox("Mode 7 supersampling", &settings.hackMode7Supersample);
  dirty |= ImGui::Checkbox("Mode 7 HD to SD mosaic", &settings.hackMode7Mosaic);
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::TextDisabled("DSP (audio)");
  dirty |= ImGui::Checkbox("Fast mode##dsp", &settings.hackDspFast);
  dirty |= ImGui::Checkbox("Cubic interpolation", &settings.hackDspCubic);

  ImGui::Spacing();
  ImGui::TextDisabled("Coprocessors");
  dirty |= ImGui::Checkbox("Fast mode##coprocessor", &settings.hackCoprocessorDelayedSync);
  dirty |= ImGui::Checkbox("Prefer HLE", &settings.hackCoprocessorPreferHLE);
  tip("Always use HLE when available, rather than only when firmware is missing.");

  ImGui::Spacing();
  ImGui::TextDisabled("Game Enhancements");
  dirty |= ImGui::Checkbox("Per-title hotfixes", &settings.hackHotfixes);
  tip("Corrects bugs that shipped in commercial games and occurred on hardware too.");
  const std::string hotfix = core.activeHotfix();
  if(!hotfix.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "Active: %s", hotfix.c_str());
  }

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##enhancements")) {
    restoreEnhancementDefaults();
    dirty = true;
  }
  if(dirty) {
    pushEnhancements();
    settings.save(settingsCfg);
  }
}

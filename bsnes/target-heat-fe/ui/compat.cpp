#include "ui.hpp"

void App::restoreCompatibilityDefaults() {
  const Settings defaults;
  settings.hackEntropy = defaults.hackEntropy;
  settings.hackCpuFastMath = defaults.hackCpuFastMath;
  settings.hackPpuNoVRAMBlocking = defaults.hackPpuNoVRAMBlocking;
  settings.hackDspEchoShadow = defaults.hackDspEchoShadow;
}

void App::drawCompatibilityTab() {
  bool dirty = false;

  if(netplayActive()) {
    ImGui::TextDisabled("Changes here are queued; netplay holds these hacks at their");
    ImGui::TextDisabled("deterministic values until the session ends.");
  }
  ImGui::TextDisabled("Entropy (randomization)");
  dirty |= ImGui::Combo("Startup state", &settings.hackEntropy, EntropyNames, EntropyCount);
  tip("None suits old homebrew, Low matches a real SNES, High stresses new software.");

  ImGui::Spacing();
  ImGui::TextDisabled("CPU (processor)");
  dirty |= ImGui::Checkbox("Fast math", &settings.hackCpuFastMath);
  tip("Returns multiplication and division results immediately, as old emulators did.");

  ImGui::Spacing();
  ImGui::TextDisabled("PPU (video)");
  dirty |= ImGui::Checkbox("No VRAM blocking", &settings.hackPpuNoVRAMBlocking);
  tip("Reproduces an old ZSNES/Snes9X bug some ROM hacks render incorrectly without.");

  ImGui::Spacing();
  ImGui::TextDisabled("DSP (audio)");
  dirty |= ImGui::Checkbox("Echo shadow RAM", &settings.hackDspEchoShadow);
  tip("Reproduces a ZSNES bug that older Super Mario World hacks crash without.");

  ImGui::Separator();
  ImGui::TextWrapped("These take effect the next time a game is loaded.");

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##compatibility")) {
    restoreCompatibilityDefaults();
    dirty = true;
  }
  if(dirty) {
    pushEnhancements();
    settings.save(settingsCfg);
  }
}

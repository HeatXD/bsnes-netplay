#include "ui.hpp"

void App::restoreEmulatorDefaults() {
  const Settings defaults;
  settings.defocusPolicy = defaults.defocusPolicy;
  settings.fastForwardSpeed = defaults.fastForwardSpeed;
  settings.fastForwardUnlimited = defaults.fastForwardUnlimited;
  settings.fastForwardFrameSkip = defaults.fastForwardFrameSkip;
  settings.fastForwardMute = defaults.fastForwardMute;
  settings.showStatus = defaults.showStatus;
  settings.showToolTips = defaults.showToolTips;
  settings.warnUnverified = defaults.warnUnverified;
  settings.autoSaveMemory = defaults.autoSaveMemory;
  settings.autoSaveInterval = defaults.autoSaveInterval;
  settings.serialization = defaults.serialization;
  settings.autoStateOnUnload = defaults.autoStateOnUnload;
  settings.autoStateOnLoad = defaults.autoStateOnLoad;
  pushSerialization();
  settings.ipsHeadered = defaults.ipsHeadered;
  core.setIpsHeadered(settings.ipsHeadered);
  applySpeed();
}

void App::drawEmulatorTab() {
  bool dirty = false;
  dirty |= ImGui::Combo("When window is unfocused", &settings.defocusPolicy,
                        DefocusNames, DefocusCount);

  ImGui::Spacing();
  ImGui::TextDisabled("Fast Forward");
  ImGui::BeginDisabled(settings.fastForwardUnlimited);
  if(ImGui::SliderInt("Limiter", &settings.fastForwardSpeed, 2, 16, "%dx")) applySpeed();
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();
  tip("The top speed fast forward runs at.");
  if(ImGui::Checkbox("Unlimited (as fast as the machine allows)",
                     &settings.fastForwardUnlimited)) {
    applySpeed();
    dirty = true;
  }
  if(ImGui::SliderInt("Frame skip", &settings.fastForwardFrameSkip, 0, 9, "%d frames")) {
    applySpeed();
  }
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("Skipping frames raises the top fast forward rate; needs the fast PPU.");
  dirty |= ImGui::Checkbox("Mute while fast forwarding", &settings.fastForwardMute);

  ImGui::Spacing();
  ImGui::TextDisabled("Memory");
  dirty |= ImGui::Checkbox("Auto-save memory periodically", &settings.autoSaveMemory);
  tip("Writes save RAM on a timer, so a crash costs one interval at most.");
  ImGui::BeginDisabled(!settings.autoSaveMemory);
  ImGui::SliderInt("Interval", &settings.autoSaveInterval, 5, 600, "%d s");
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::EndDisabled();

  ImGui::Spacing();
  ImGui::TextDisabled("Save States");
  if(ImGui::Combo("Serialization", &settings.serialization,
                  SerializationNames, SerialCount)) {
    pushSerialization();
    dirty = true;
  }
  tip("Strict winds the coprocessors further forward first; slower, but safer.");
  dirty |= ImGui::Checkbox("Save an auto-resume state when a game is closed",
                           &settings.autoStateOnUnload);
  ImGui::BeginDisabled(!settings.autoStateOnUnload);
  dirty |= ImGui::Checkbox("Resume from it when the game is loaded again",
                           &settings.autoStateOnLoad);
  ImGui::EndDisabled();
  tip("The auto-resume state has a slot of its own.");

  ImGui::Spacing();
  ImGui::TextDisabled("Games");
  dirty |= ImGui::Checkbox("Warn on unverified games", &settings.warnUnverified);
  tip("An unverified image has its board layout guessed from the ROM.");
  if(ImGui::Checkbox("IPS patches expect a headered ROM", &settings.ipsHeadered)) {
    core.setIpsHeadered(settings.ipsHeadered);
    dirty = true;
  }
  tip("Flip this and reload if an IPS patch misbehaves; BPS ignores it.");

  ImGui::Spacing();
  ImGui::TextDisabled("Interface");
  dirty |= ImGui::Checkbox("Show status bar", &settings.showStatus);
  dirty |= ImGui::Checkbox("Show tool tips", &settings.showToolTips);
  ImGui::Separator();
  ImGui::TextWrapped("Save RAM goes to the saves folder under Paths, or next to the ROM.");
  ImGui::TextWrapped("States go to the states folder, one file per slot per game.");

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##emulator")) {
    restoreEmulatorDefaults();
    dirty = true;
  }
  if(dirty) settings.save(settingsCfg);
}

#include "ui.hpp"

void App::restoreAudioDefaults() {
  const Settings defaults;
  settings.latencyMs = defaults.latencyMs;
  settings.volume = defaults.volume;
  settings.mute = defaults.mute;
  settings.muteUnfocused = defaults.muteUnfocused;
  settings.audioDevice = defaults.audioDevice;
  settings.audioSkew = defaults.audioSkew;
  settings.audioBalance = defaults.audioBalance;
  applyAudioTuning();
  shell.reopenAudio(settings);
}

void App::drawAudioTab() {
  bool dirty = false;
  // save on release only, or a drag rewrites the file every frame
  ImGui::SliderInt("Latency (ms)", &settings.latencyMs, MinLatencyMs, MaxLatencyMs);
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt("Volume (%)", &settings.volume, 0, 200);
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  if(ImGui::SliderInt("Balance", &settings.audioBalance, 0, 100,
                      settings.audioBalance == 50 ? "centred" : "%d")) applyAudioTuning();
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("0 is hard left, 100 hard right.");
  if(ImGui::SliderInt("Skew (Hz)", &settings.audioSkew, -MaxAudioSkew, MaxAudioSkew, "%+d")) {
    applyAudioTuning();
  }
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  tip("Raise it if the sound card runs slow and the backlog keeps growing.");
  dirty |= ImGui::Checkbox("Mute", &settings.mute);
  dirty |= ImGui::Checkbox("Mute when window is unfocused", &settings.muteUnfocused);

  const std::vector<std::string> devices = Shell::listPlaybackDevices();
  int current = -1;  // -1 is the system default
  for(int i = 0; i < (int)devices.size(); i++) {
    if(devices[i] == settings.audioDevice) current = i;
  }
  const std::string label = current >= 0 ? devices[current] : "System default";
  if(ImGui::BeginCombo("Output device", label.c_str())) {
    if(ImGui::Selectable("System default", current < 0)) {
      settings.audioDevice.clear();
      shell.reopenAudio(settings);
      settings.save(settingsCfg);
    }
    for(int i = 0; i < (int)devices.size(); i++) {
      ImGui::PushID(i);
      if(ImGui::Selectable(devices[i].c_str(), i == current)) {
        settings.audioDevice = devices[i];
        shell.reopenAudio(settings);
        settings.save(settingsCfg);
      }
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }

  ImGui::Separator();
  ImGui::Text("Frequency: %d Hz (resampling to %d)", AudioRate, AudioRate + settings.audioSkew);
  ImGui::Text("Queued: %d bytes", (int)SDL_GetAudioStreamQueued(shell.audio));
  ImGui::TextWrapped("Audio paces the emulator; latency is the backlog it drains to.");

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##audio")) {
    restoreAudioDefaults();
    dirty = true;
  }
  if(dirty) settings.save(settingsCfg);
}

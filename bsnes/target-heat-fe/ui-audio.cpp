#include "ui.hpp"

void App::drawAudioTab() {
  bool dirty = false;
  // save on release only, or a drag rewrites the file every frame
  ImGui::SliderInt("Latency (ms)", &settings.latencyMs, MinLatencyMs, MaxLatencyMs);
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SliderInt("Volume (%)", &settings.volume, 0, 200);
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  dirty |= ImGui::Checkbox("Mute", &settings.mute);
  ImGui::Separator();
  ImGui::Text("Frequency: %d Hz", AudioRate);
  ImGui::Text("Queued: %d bytes", (int)SDL_GetAudioStreamQueued(shell.audio));
  ImGui::TextWrapped("Audio is the master clock. Latency sets the backlog the loop drains to,"
                     " which is what paces the emulator to 60.098Hz on NTSC carts and"
                     " 50.007Hz on PAL ones.");
  if(dirty) settings.save(settingsCfg);
}

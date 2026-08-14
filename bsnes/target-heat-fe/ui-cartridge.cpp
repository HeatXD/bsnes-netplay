#include "ui.hpp"

void App::drawCartridgeWindow() {
  if(!showCartridge) return;

  placeFloating(80.0f, 60.0f, 460.0f, 400.0f);
  if(ImGui::Begin("Cartridge", &showCartridge)) {
    ImGui::TextWrapped("The manifest viewer is not wired up yet.");
  }
  ImGui::End();
}

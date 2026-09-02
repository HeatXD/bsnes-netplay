#include "ui.hpp"

bool App::drawColourSection() {
  bool dirty = false;

  ImGui::TextDisabled("Colours");
  static const char* themes[] = {"Dark", "Light", "Classic"};
  if(ImGui::Combo("Theme", &settings.theme, themes, IM_ARRAYSIZE(themes))) {
    applyTheme();
    dirty = true;
  }

  const ImVec4 current = accentColor();
  float accent[3] = {current.x, current.y, current.z};
  if(ImGui::ColorEdit3("Accent", accent, ImGuiColorEditFlags_NoInputs)) {
    settings.accent = packColor(accent);
    applyTheme();
  }
  dirty |= ImGui::IsItemDeactivatedAfterEdit();
  ImGui::SameLine();
  ImGui::TextDisabled("buttons, tabs, sliders and selections");

  bool follow = settings.textColor == FollowTheme;
  if(ImGui::Checkbox("Text colour follows theme", &follow)) {
    const ImVec4& text = ImGui::GetStyle().Colors[ImGuiCol_Text];
    const float rgb[3] = {text.x, text.y, text.z};
    settings.textColor = follow ? FollowTheme : packColor(rgb);
    applyTheme();
    dirty = true;
  }
  if(follow) { return dirty; }

  const ImVec4 chosen = unpackColor(settings.textColor);
  float text[3] = {chosen.x, chosen.y, chosen.z};
  if(ImGui::ColorEdit3("Text", text, ImGuiColorEditFlags_NoInputs)) {
    settings.textColor = packColor(text);
    applyTheme();
  }
  return dirty | ImGui::IsItemDeactivatedAfterEdit();
}

bool App::drawFontSection() {
  bool dirty = false;

  ImGui::TextDisabled("Font");
  ImGui::TextWrapped("%s", settings.fontPath.empty() ? "(built-in)" : settings.fontPath.c_str());
  if(ImGui::Button("Browse##font")) { openFontDialog(); }
  ImGui::SameLine();
  if(ImGui::Button("Use built-in")) {
    settings.fontPath.clear();
    fontDirty = dirty = true;
  }

  ImGui::SliderInt("Size", &settings.fontSize, MinFontSize, MaxFontSize, "%dpx");
  if(ImGui::IsItemDeactivatedAfterEdit()) { fontDirty = dirty = true; }

  ImGui::SliderInt("Weight", &settings.fontWeight, MinFontWeight, MaxFontWeight, "%d%%");
  if(ImGui::IsItemDeactivatedAfterEdit()) { fontDirty = dirty = true; }

  return dirty;
}

void App::restoreAppearanceDefaults() {
  const Settings defaults;
  settings.theme = defaults.theme;
  settings.accent = defaults.accent;
  settings.textColor = defaults.textColor;
  settings.fontSize = defaults.fontSize;
  settings.fontWeight = defaults.fontWeight;
  settings.fontPath = defaults.fontPath;
  applyTheme();
  fontDirty = true;
}

void App::drawCustomizationTab() {
  bool dirty = drawColourSection();

  ImGui::Separator();
  dirty |= drawFontSection();

  ImGui::Separator();
  if(ImGui::Button("Restore defaults##look")) {
    restoreAppearanceDefaults();
    dirty = true;
  }
  ImGui::TextWrapped("Weight thickens the glyphs rather than switching to a bold face.");

  if(dirty) { settings.save(settingsCfg); }
}

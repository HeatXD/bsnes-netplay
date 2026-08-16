#pragma once

#include "../app.hpp"

// first-use placement for a floating panel, centred on the main window
void placeFloating(float w, float h);

// "3x (672p)", 0 being fit-to-window; shared so the two pickers cannot differ
std::string windowScaleLabel(int scale, const Settings& settings);

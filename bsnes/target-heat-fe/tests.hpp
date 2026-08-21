#pragma once

// Verification harnesses drive App directly without drawing or pumping events.

#include "app.hpp"

int runStateTest(App& app, int warmFrames, int frames);
int runDeterminismTest(App& app, int frames);
int runTimelineTest(App& app, int warmFrames);
int runHotkeyTest(App& app);
int runLuaTest(App& app, const std::string& script);
int runShaderTest(App& app);
int runCheatTest(App& app);

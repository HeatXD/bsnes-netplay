#pragma once

// The verification harnesses behind --state-test, --determinism-test and
// --hotkey-test. None of them draw or pump events; they drive App directly.

#include "app.hpp"

int runStateTest(App& app, int warmFrames, int frames);
int runDeterminismTest(App& app, int frames);
int runHotkeyTest(App& app);

// Milestone 1: the Gold Box Library and asset viewer.
//
// Finds the player's games on the SD card, lists each game's DAX files and
// their blocks, and shows any block: EGA pictures drawn through the game
// canvas (ui/frame.*), anything else as a hex dump. This is how the file
// formats get checked against Tom's real game files on real boards before
// the game engine is built on them. Also holds the engine's Settings page.
#pragma once

#include <cstdint>

#include "app/settings.h"

namespace viewer {

struct Env {
    const char* version;      // "v0.1.0"
    const char* build;        // git commit
    void (*recalibrate)();    // runs the touch calibration (redraws after)
    void (*apply_rotation)(bool flipped);
};

void begin(const Env& env, Settings& settings);
void tick();

} // namespace viewer

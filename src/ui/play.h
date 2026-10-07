// Play Test (milestone 4): a new game of Curse run by its own area scripts
// from the player's files - the opening, events, text, menus and pictures
// as the party walks. No party yet: anything that needs characters
// (combat, treasure, checks) is passed over and logged.
#pragma once

#include <cstdint>

#include "engine/games.h"
#include "engine/geo.h"
#include "engine/picture.h"

namespace play {

bool available(games::Game g);

// Loads the shared pieces and starts a new game. nullptr = ready, else why
// not. Draws into c.
const char* open(const char* data_dir, games::Game g, pic::Canvas& c);
void close();

enum class Act : uint8_t { TurnLeft, TurnRight, TurnAround, Forward, StepLeft, StepRight, Area, Look };

// Draws the whole game screen.
void draw(pic::Canvas& c);
// A key; false if it does nothing now. While text or a one-choice menu
// waits, any key goes on.
bool act(Act a, pic::Canvas& c);
// A tap on the canvas (pixel x, y): menu words, list lines, "press a key".
void tap(int x, int y, pic::Canvas& c);
// Moves printing text and pauses on. Call often.
void tick(uint32_t now_ms, pic::Canvas& c);

// Canvas rows changed since the last call (y1 == 0: none).
void take_dirty(int& y0, int& y1);

// For the companion panel
const geo::Map* map();
int pos_x();
int pos_y();
int dir();
void describe(char* line1, char* line2, int cap);

} // namespace play

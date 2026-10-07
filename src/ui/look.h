// Screen Test (milestone 2): the game's own look, drawn from the player's
// files - the title sequence and credits, a text window and the menu line,
// the screen frames and their tiles.
//
// The frame's tile layout and the program's strings come from the game's
// program (START.EXE, engine/profile.h), the credits from GAME.OVR, tiles
// and font from 8X8D1.DAX, the pictures from TITLE.DAX - so only games with
// a known release have one (Curse so far).
#pragma once

#include <cstdint>

#include "engine/games.h"
#include "engine/picture.h"

namespace look {

// True if this game has a screen test (a known program release).
bool available(games::Game g);

// Loads what the pages need from /GOLDBOX/<data_dir>/. Returns nullptr when
// ready, else a sentence saying what's missing (for the player).
const char* open(const char* data_dir, games::Game g);
void close();

int pages();
const char* page_name(int page);
// One line about the release found, e.g. "START.EXE, GOG release"
const char* source();

// Canvas rows that changed (y1 == 0: none).
struct Rows {
    int y0 = 0, y1 = 0;
};

// Shows page `page` from its start (the whole canvas changes).
void enter(int page, pic::Canvas& c);
// Moves animated pages on (title sequence, text printing). Call often.
Rows tick(uint32_t now_ms, pic::Canvas& c);
// A tap on the canvas at pixel (x, y): skips a title picture, goes on after
// "press any key", picks from the menu line.
Rows tap(int x, int y, uint32_t now_ms, pic::Canvas& c);

} // namespace look

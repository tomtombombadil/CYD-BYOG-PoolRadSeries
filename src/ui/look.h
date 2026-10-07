// Screen test (milestone 2): the game's own screen frame and font, drawn
// from the player's files - the outer border, the exploring screen (3D
// view, party and text areas), combat, and the frame tiles.
//
// The frame's tile layout is read from the game's program on the card
// (START.EXE, engine/profile.h) and its tiles and font from 8X8D1.DAX, so
// only games with a known release have one (Curse so far).
#pragma once

#include <cstddef>

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

// Draws page `page` into the canvas (EGA palette).
void draw(int page, pic::Canvas& c);

} // namespace look

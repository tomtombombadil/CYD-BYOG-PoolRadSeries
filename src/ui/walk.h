// Walk Test (milestone 3): the game's 3D areas from the player's files -
// every map the area scripts load, with the wall sets they load for it -
// walked with the exploring controls (turn, step, side-step, turn around)
// and the AREA map. No events yet: walls and doors only (locked doors stop
// the party).
#pragma once

#include <cstdint>

#include "engine/games.h"
#include "engine/geo.h"
#include "engine/picture.h"

namespace walk {

bool available(games::Game g);

// Loads the shared pieces and finds the maps. nullptr = ready, else why not.
const char* open(const char* data_dir, games::Game g);
void close();

enum class Act : uint8_t { TurnLeft, TurnRight, TurnAround, Forward, StepLeft, StepRight, Area, NextMap, PrevMap };

// Draws the whole game screen.
void draw(pic::Canvas& c);
// Does a move; redraws the canvas. False if nothing changed.
bool act(Act a, pic::Canvas& c);
// A tap on the canvas (pixel x, y): the party panel = next map.
bool tap(int x, int y, pic::Canvas& c);

// For the companion panel
const geo::Map* map();
int pos_x();
int pos_y();
int dir();
// "Map 3 of 18", "Area 2, GEO 3 (script 3)"
void describe(char* line1, char* line2, int cap);

} // namespace walk

// The game screen: one 320x200 canvas of palette indexes (like the
// original EGA/VGA screen), turned into panel colours when presented.
//
// 320x240 panels: the canvas is shown 1:1 at the top; 40 rows stay below.
// 480x320 panels: either 1:1 (centred, room left round it) or scaled 1.5x
// to 480x300 (nearest neighbour). Which one the games will use is still
// Tom's call - the asset viewer and Settings let him try both on the board.
#pragma once

#include <cstdint>

#include "engine/picture.h"
#include "ui.h"

namespace frame {

enum class Scale : uint8_t { One, OneAndHalf };

// Allocates the canvas (64,000 bytes). False if memory is short.
bool begin();

pic::Canvas& canvas();

// Palette entries 0-255 (RGB). set_ega_palette() sets 0-15 and makes 16
// (EGA "transparent") black; a 256-colour picture sets what it needs.
void set_palette(int index, const pic::Rgb& c);
void set_ega_palette();

void set_scale(Scale s);    // OneAndHalf only takes effect on 480-wide panels
Scale scale();

// Game layout: the canvas at the panel's top left (1:1), leaving the right
// strip of a 480x320 panel for the Gold Box Companion. Otherwise centred.
void set_left(bool left);

// Where the canvas lands on the panel.
ui::Rect area();

// Draws the canvas (all of it, or canvas rows y0..y1-1).
void present();
void present_rows(int y0, int y1);

// While the journal book is open the game screen waits on the card
// (v0.52.0): park() writes the canvas to path and frees its 64,000 bytes
// (false: nothing written, the canvas stays); unpark() reads it back
// (false: no memory - the canvas has no pixels then; unreadable: blank).
// Nothing may draw on the canvas while it is parked.
bool park(const char* path);
bool unpark(const char* path);
bool parked();

// A palette entry as the panel shows it (RGB565, the usual byte order)
uint16_t colour(int index);

// Panel point -> canvas point. False if outside the canvas.
bool to_canvas(int px, int py, int& cx, int& cy);

} // namespace frame

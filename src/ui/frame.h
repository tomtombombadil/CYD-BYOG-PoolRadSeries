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

// Palette entries 0-255 (RGB). Index 16 (transparent) shows as black.
void set_palette(int index, const pic::Rgb& c);
void set_ega_palette();

void set_scale(Scale s);    // OneAndHalf only takes effect on 480-wide panels
Scale scale();

// Where the canvas lands on the panel.
ui::Rect area();

// Draws the canvas (all of it, or canvas rows y0..y1-1).
void present();
void present_rows(int y0, int y1);

// Panel point -> canvas point. False if outside the canvas.
bool to_canvas(int px, int py, int& cx, int& cy);

} // namespace frame

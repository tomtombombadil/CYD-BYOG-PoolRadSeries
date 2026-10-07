// The engine's own screens: drawing helpers and touch input on top of
// LovyanGFX. No LVGL - the games draw their own screens into an 8-bit
// canvas (ui/frame.*), and the few engine screens are simple key grids.
//
// Layout is always derived from the panel size (320x240 or 480x320 in
// landscape), never hard-coded for one board.
#pragma once

#include <cstdint>

#include "boards/board_select.h"
#include "style.h"

namespace ui {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct Tap { int x, y; };

void begin(LGFX& gfx);
LGFX& gfx();

int width();
int height();
bool large();          // a 480x320 panel

int header_h();        // title bar
int key_h();           // a row of keys
int gap();             // space between keys

// Touch. A tap counts on release, at the point where the stylus came DOWN
// (readings drift as the stylus lifts). A press starts only after two
// readings in a row agree within a few pixels, and ends after two empty
// readings (filters the XPT2046's stray samples).
// Call often (every loop); returns true once per tap.
bool poll_tap(Tap& out);
// True while the panel is being pressed (after the filter).
bool pressed();

// ---- drawing --------------------------------------------------------------
void clear();
// Title bar across the top. back = draw the "<" back key at its left.
void header(const char* title, bool back);
Rect back_rect();      // the header's "<" key (valid when drawn with back)

enum class KeyStyle : uint8_t { Normal, Lit, Dim };
void key(const Rect& r, const char* label, KeyStyle s = KeyStyle::Normal);
// Two-line key: label + smaller muted line under it.
// left_inset: room kept free at the key's left (for a picture); the text
// is centred in the rest.
void key2(const Rect& r, const char* label, const char* sub, KeyStyle s = KeyStyle::Normal, int left_inset = 0);
// The fill colour of a key in style s (to draw pictures on it)
uint16_t key_fill(KeyStyle s);

enum class Font : uint8_t { Small, Normal, Mono };
void text(int x, int y, const char* s, uint16_t color = style::kText, Font f = Font::Normal);
void text_center(const Rect& r, const char* s, uint16_t color = style::kText, Font f = Font::Normal);
int text_width(const char* s, Font f = Font::Normal);
int line_h(Font f = Font::Normal);

// The bottom key row: n equal keys across the screen; returns key i's rect.
Rect bottom_key(int i, int n);
// A grid of keys filling the area between the header and the bottom row.
// cols x rows; returns cell i (row-major).
Rect grid_cell(int i, int cols, int rows, bool leave_bottom_row = true);

} // namespace ui

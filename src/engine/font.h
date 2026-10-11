// The games' own 8x8 font.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Every game so far keeps it in block 201 of an 8X8D*.DAX file (8X8D1 in
// PoolRad and Curse, 8X8D5 in Secret, 8X8D0 in Darkness): 177 glyphs of
// 8 bytes, one byte a pixel row, bit 7 = leftmost pixel, 1 = ink.
// PoolRad, Curse and Secret carry the identical font; Darkness's differs.
//
// Glyphs 0-63 are text: a character's glyph is its upper-case ASCII code
// modulo 64 (the games print in capitals: 'A' = 1, ' ' = 32, '0' = 48).
// Glyphs 64-176 are other small pieces (symbols and frame bits).
//
// The screen is a grid of 40 x 25 cells of 8 x 8 pixels; text is placed by
// cell.
#pragma once

#include <cstdint>

#include "dax.h"
#include "picture.h"

namespace font {

constexpr int kGlyphs = 177;
constexpr uint8_t kBlockId = 201;
constexpr uint32_t kBlockBytes = kGlyphs * 8;
constexpr int kCols = pic::kScreenW / 8;   // 40
constexpr int kRows = pic::kScreenH / 8;   // 25

struct Font {
    uint8_t glyph[kGlyphs][8];
    bool loaded = false;
    // The engine's Clear font in place of the game's letters (set_clear)
    bool    clear = false;
    uint8_t saved[26 + 32][8];         // the game's glyphs 1-26 and 32-63 while it is
};

// The engine's own Clear font (Tom, 2026-10-11: easier to read on the small
// screens; drawn for the engine - tools/fonts/clear8.txt, nothing from the
// games): upright bold capitals, digits and punctuation in place of glyphs
// 1-26 and 32-63, and real lower case (glyph kGlyphs + 0..25). The game's
// other glyphs (0, 27-31, 64-176: symbols, frame bits) stay its own.
extern const uint8_t kClearUpper[26][8];
extern const uint8_t kClearLower[26][8];
extern const uint8_t kClearText[32][8];
// Puts the Clear font in (on) or the game's own back (off). With a canvas:
// the text already on it is redrawn in the other font too (mid-game,
// Tom 2026-10-11) - every 8 x 8 cell holding exactly a text glyph of the
// old font in one colour on black (or black on one colour, the menu's
// choice) gets the new font's glyph in the same colours. (Lower case
// printed in capitals by the game's font stays capitals.)
void set_clear(Font& f, bool on, pic::Canvas* redraw = nullptr);

// Loads block 201 from an open DAX file. False if the file has no such
// block or it isn't 1416 bytes.
bool load(dax::ByteSource& src, const dax::Index& idx, Font& out);

// The glyph a character prints with (the game's font: capitals only).
int glyph_of(char c);
// The same in font f: with the Clear font, lower case has glyphs of its own
int glyph_of(const Font& f, char c);

// Draws glyph g with its top-left at pixel (x, y). bg < 0 = leave the
// paper pixels as they are.
void draw_glyph(pic::Canvas& c, const Font& f, int g, int x, int y, uint8_t fg, int bg);

// Prints text from cell (col, row) rightwards; stops at the right edge.
// Returns the column after the last character.
int draw_text(pic::Canvas& c, const Font& f, const char* s, int col, int row, uint8_t fg, int bg);

} // namespace font

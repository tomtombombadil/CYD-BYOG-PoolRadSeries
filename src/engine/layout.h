// The games' screen frame: the ornamental border and dividers around the
// 3D view, text and party areas, built from 8x8 tiles.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// The screen is 40 x 25 cells of 8 x 8 pixels (font.h). The tiles are 40
// small EGA pictures in one block (Curse: 8X8D1.DAX block 202 - a picture
// block 8 pixels high, 1 column wide, 40 frames). Which tile goes in which
// cell comes from tables in the game's own program (profile.h), read from
// the player's copy:
//   frame tile = 30 + table value (0-9), the 3D view's own frame tile =
//   20 + table value.
//
// Screens (layout learned from coab, Curse of the Azure Bonds):
//   outer()    border round rows 0-23, inside (rows 1-22, cols 1-38)
//              cleared; row 24 is the game's prompt / menu line
//   bar(row)   a full-width divider (e.g. row 16 above the text area)
//   explore()  outer + bar at row 16 + a divider down column 16 (rows
//              0-16) + the 3D view's own frame (rows / cols 2-14); the 3D
//              view itself is cells 3-13 (88 x 88 pixels)
//   combat()   border with dividers at columns 0, 22 and 39, rows 0-22
#pragma once

#include <cstdint>

#include "dax.h"
#include "exepack.h"
#include "picture.h"
#include "profile.h"

namespace layout {

constexpr int kTiles = 40;
constexpr int kFrameTile = 30;      // + table value
constexpr int kViewTile = 20;       // + table value
constexpr uint8_t kMask = 13;       // tile pixels in this colour aren't drawn

struct Tiles {
    uint8_t px[kTiles][64];          // palette indexes, row by row
    bool loaded = false;
};

// Loads the tiles: an EGA picture block 8 high, 1 column wide, 40 frames.
bool load_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Tiles& out);

struct Tables {
    uint8_t top[40], bar[40], bottom[40];
    uint8_t left[24], right[24];
    uint8_t view_split[17];
    uint8_t view_top[15], view_bottom[15], view_left[15], view_right[15];
    uint8_t combat_left[23], combat_split[23], combat_right[23];
    bool loaded = false;
};

enum class Status : uint8_t { Ok, NotPacked, ReadError, BadData, BadTables };
const char* status_text(Status s);

// Reads the tables from the game's program (one span of a few hundred
// bytes, unpacked straight from the file). BadTables: the values don't look
// like tile numbers (a different release with the same size?).
Status load_tables(dax::ByteSource& program, const exepack::Info& info, const profile::Profile& p, Tables& out);

// Tile t (0-39) with its top-left at cell (col, row).
void tile(pic::Canvas& c, const Tiles& t, int n, int col, int row);

void outer(pic::Canvas& c, const Tables& tb, const Tiles& t);
void bar(pic::Canvas& c, const Tables& tb, const Tiles& t, int row);
void explore(pic::Canvas& c, const Tables& tb, const Tiles& t);
void combat(pic::Canvas& c, const Tables& tb, const Tiles& t);

} // namespace layout

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
//
// Pool of Radiance has no frame tables: its code draws boxes (corner /
// junction tile, side tile, top and bottom tile - block 202's 20, 21, 22;
// screen_facts.md 1.1). The profile's variant.box says so; the same calls
// then draw Pool's boxes on the same rows and columns.
//
// Secret of the Silver Blades has no tables either: its code draws bars
// (secret_facts.md 3) - one tile for every horizontal bar (full width,
// corners included), the verticals a cap tile under a bar, a side tile and
// a cap over the next bar; the 3D view / picture frame an edge tile along
// its top and bottom and cap / side / cap down its sides. Its tiles are
// 52-59 of 101: Tiles keeps 0-19 and a window of 20 from `first` (the
// profile's variant.tile_window) in its 40 slots.
#pragma once

#include <cstdint>

#include "dax.h"
#include "exepack.h"
#include "picture.h"
#include "profile.h"

namespace layout {

constexpr int kTiles = 45;          // (Death Knights: its block 202's 45 - frame, moons, area map)
constexpr int kOldTiles = 40;       // (the slots the other games' tables may name)
constexpr int kFrameTile = 30;      // + table value
constexpr int kViewTile = 20;       // + table value
constexpr uint8_t kMask = 13;       // tile pixels in this colour aren't drawn

struct Tiles {
    uint8_t px[kTiles][64];          // palette indexes, row by row
    uint8_t count = 0;               // slots filled (Curse 40, Pool 31)
    uint8_t first = 20;              // the block's tile in slot 20 (Secret 40: slots 20-39 = tiles 40-59)
    uint8_t window = 20;             // slots from `first` (Death Knights 25: its tiles 20-44)
    bool loaded = false;
    // VGA values (Darkness on a 256-colour screen): colour 13 is drawn too -
    // only the original's EGA mode leaves it out of the frame
    // (darkness_facts.md 7 1.5)
    bool vga = false;
    // Death Knights: its tile routine draws colour 13 too (the red moon's
    // full disc - DOSBox)
    bool solid = false;
    // The slot of the block's tile n (-1: not kept)
    int slot(int n) const {
        if (n < 20) return n;
        return n >= first && n < first + window ? n - first + 20 : -1;
    }
    // The block's tile in slot s
    int tile_of(int s) const { return s < 20 ? s : s - 20 + first; }
};

// Loads the tiles: an EGA picture block 8 high, 1 column wide, 40 frames
// (Pool: 31; Secret 101; at least 23); tiles 0-19 and 20 from `first`.
bool load_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Tiles& out, uint8_t first = 20,
                uint8_t window = 20);

// Loads a VGA block of 8 x 8 tiles (Darkness: 8X8D0's area map tiles, a
// BORDERS style) into slots slot0.. (at most n): ega - in their EGA
// colours, values under the block's first entry as they are (the
// original's EGA mode); else their values (a VGA screen: out.vga). pal: the
// block's palette entries (nullptr: not wanted)
bool load_vga_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Tiles& out, int slot0, int n,
                    bool ega, pic::VgaPalette* pal = nullptr);

struct Tables {
    uint8_t top[40], bar[40], bottom[40];
    uint8_t left[24], right[24];
    uint8_t view_split[17];
    uint8_t view_top[15], view_bottom[15], view_left[15], view_right[15];
    uint8_t combat_left[23], combat_split[23], combat_right[23];
    uint8_t box[3] = {};             // Pool: boxes drawn with these tiles instead (box[0] != 0)
    // Secret: bars drawn with these tiles (bars[0] != 0; block numbers):
    // bar, cap under a bar, side, cap over a bar; the view frame's edge,
    // its top cap, side, bottom cap
    uint8_t bars[8] = {};
    // Champions: in bars mode the top bar (row 0) is `top`'s tiles (20 +
    // value; its moons' cells set by the game from their phases)
    bool top_row = false;
    // Darkness: the frame drawn by its bar routine from BORDERS.DAX's pieces
    // in slots 20-30 (0-3 corners, 4 / 5 the bars, 6 / 7 junctions, 9 the
    // row-16 bar's left end - darkness_facts.md 7.1)
    bool chain = false;
    // The frame tables' tile = this + value (Curse 30; Gateway 20 - its
    // values 0-13 for the frame, 14-19 the 3D view's: gateway_facts.md 4)
    uint8_t frame_base = kFrameTile;
    // Gateway: the bamboo's joint pieces put next to where bars meet (its
    // frame routines, GAME.OVR 0x4BC7B on - gateway_facts.md 4): tile 27
    // on a side two rows (exploring: one and two) from a bar, 33 two
    // columns either side of a divider on the bars it meets
    bool joints = false;
    uint8_t char_split[13] = {};     // Gateway's View Character column 19, rows 8-20
    // Death Knights (its START image 0x10F0 on): the tables name block 202's
    // tiles themselves (no base); the sides are tile 7 (the outer frame's
    // rows 1-22), the 3D view's frame corners 0-3 with 4 across and 7 down;
    // View Character's columns 0 / 39 by row (char_left / char_right), bars
    // of tile 4 at rows 8, 16 and 20; the top row's moons are the game's
    bool direct = false;
    uint8_t char_left[24] = {}, char_right[24] = {};
    // Treasures (port facts P7): its frames are pieces of BORD.DAX block 7
    // (tr_pieces, tr_size bytes: runs of BORD block 0 tile numbers drawn
    // across or down from a cell, 0xFF a gap, 0xFE a piece's end), each
    // screen its own pieces (its img 0x1B38 exploring, 0x1BE6 the border,
    // 0x1C5B the border with the row-16 bar, 0x1CE7 a fight); the 64 tiles
    // in tr_tiles (64 bytes each: EGA colours, colour 13 not drawn - or a
    // VGA screen's values, tr_vga); the screen cleared first with tr_clear
    // (Treasures' VGA background, BORD 1; nullptr: black)
    bool tr = false;
    const uint8_t* tr_pieces = nullptr;
    uint16_t tr_size = 0;
    uint16_t tr_at[24] = {};
    uint8_t  tr_count = 0;
    const uint8_t* tr_tiles = nullptr;
    bool     tr_vga = false;
    void (*tr_clear)(pic::Canvas& c, int x, int y, int w, int h) = nullptr;
    bool loaded = false;
};

// Treasures: the frame pieces' starts in tb.tr_pieces (the block as read)
void tr_set_pieces(Tables& tb, const uint8_t* bytes, uint16_t n);
// Treasures: BORD.DAX block `block`'s 64 VGA tiles into out (64 x 64 bytes):
// ega - their EGA colours (the original's EGA mode), else their values;
// pal: the block's palette entries (nullptr: not wanted)
bool load_tr_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, uint8_t* out, bool ega,
                   pic::VgaPalette* pal = nullptr);

enum class Status : uint8_t { Ok, NotPacked, ReadError, BadData, BadTables };
const char* status_text(Status s);

// Reads the tables from the game's program (one span of a few hundred
// bytes, unpacked straight from the file). BadTables: the values don't look
// like tile numbers (a different release with the same size?).
Status load_tables(dax::ByteSource& program, const exepack::Info& info, const profile::Profile& p, Tables& out);

// The block's tile n (0-19 or a kept one) with its top-left at cell (col, row).
void tile(pic::Canvas& c, const Tiles& t, int n, int col, int row);

void outer(pic::Canvas& c, const Tables& tb, const Tiles& t);
void bar(pic::Canvas& c, const Tables& tb, const Tiles& t, int row);
void explore(pic::Canvas& c, const Tables& tb, const Tiles& t);
void combat(pic::Canvas& c, const Tables& tb, const Tiles& t);
// A box's border with its corners at cells (c0, r0) and (c1, r1) (Pool's
// framebox; a game with frame tables: nothing)
void box(pic::Canvas& c, const Tables& tb, const Tiles& t, int c0, int r0, int c1, int r1);
// A 13 x 13 cell frame with its top-left corner at cell (col, row) round an
// 88 x 88 picture (cells col + 1 .. col + 11): the 3D view's own frame
// tiles moved there (a game with frame tables), Pool's box otherwise. (The
// engine's View Character portrait box, v0.84.0.)
void picture_frame(pic::Canvas& c, const Tables& tb, const Tiles& t, int col, int row);
// Secret's bars anywhere (bars mode; nothing otherwise): a full-width bar
// at `row`; a vertical at `col` between the bars at rows r0 and r1 (caps:
// the cap tiles next to them)
void bars_row(pic::Canvas& c, const Tables& tb, const Tiles& t, int row);
void bars_col(pic::Canvas& c, const Tables& tb, const Tiles& t, int col, int r0, int r1, bool caps = true);
// The credits screen: the border and bars at `rows` (0: none); with boxes
// (Pool) the border goes down to row 24
void credits(pic::Canvas& c, const Tables& tb, const Tiles& t, const uint8_t rows[2]);
// Gateway's View Character frame (GAME.OVR 0x4C06C): the outer frame, bars
// at rows 8, 16 and 20, column 19 from row 8 to 20
void character(pic::Canvas& c, const Tables& tb, const Tiles& t);

} // namespace layout

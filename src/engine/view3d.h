// The 3D view: what the party sees, built from 8x8 tiles like the games
// do, and the AREA map shown in the same window.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// Behaviour learned from coab (Curse of the Azure Bonds); own code.
//
// The view is cells 3-13 of rows and columns (88 x 88 pixels at 24, 24):
//   sky (rows 24-67, the area's sky colour), a black line, ground (colour
//   8), then SKY.DAX block 252 (the horizon, 88 x 48, colour 13 see-through)
//   from y 64.
// Walls come from up to three wall sets (WALLDEFn.DAX: per set 5 wall
// pieces x 156 tile numbers, in 10 groups - one per place a wall can be
// seen: far front, far left / right sides, middle front / sides, near front
// / sides, far corner) and are drawn far to near. Tile numbers:
//   1-45 the common tiles (8X8D1.DAX block 203), 46-115 set 1's tiles,
//   116-185 set 2's, 186-255 set 3's (8X8Dn.DAX, 70 tiles a set), 256-295
//   the frame tiles (8X8D1 block 202: arrows 256-259, map pieces 260-275).
//   A set's own numbers (from 45 up) are stored as if for set 1 and moved up
//   by 70 / 140 when loaded into set 2 / 3. A block of 2 or 3 parts numbers
//   its later parts' tiles on from the first's (46-185 / 46-255) and every
//   part moves by the first part's amount.
#pragma once

#include <cstdint>

#include "dax.h"
#include "geo.h"
#include "layout.h"
#include "picture.h"

namespace view3d {

constexpr int kCell0 = 3;            // the view's first cell (row and column)
constexpr int kCells = 11;
constexpr int kVgaTiles = 255;       // Darkness: tiles 1-255 of an 8X8D1 block
constexpr int kSetTiles = 70;
constexpr int kCommonTiles = 45;
constexpr int kPieces = 5;           // wall pieces in a set
constexpr int kPieceTiles = 156;
constexpr uint32_t kWallSetBytes = kPieces * kPieceTiles;   // 780

struct TileSet {
    uint8_t px[kSetTiles][32];       // 8 x 8, two pixels a byte (high nibble first)
    int     count = 0;
};

struct WallSet {
    uint8_t id[kPieces][kPieceTiles];
    bool    loaded = false;
};

struct World {
    TileSet common;                  // tiles 1-45
    TileSet sets[3];
    WallSet walls[3];
    pic::Header horizon_hdr;
    uint8_t     horizon[48 * 44];    // 88 x 48 pixels, packed
    bool        has_horizon = false;
    const layout::Tiles* frame = nullptr;   // tiles 256-295 (arrows, map pieces)
    // Pools of Darkness (darkness_facts.md 7.3): one 8X8D1 block of 256
    // tiles numbered as the walls use them (1-45 in `common`, 46-255 in the
    // sets - no moves), its area map's frames walls 0-15 then arrows 16-19,
    // and an 88 x 88 background (BACK1 indoors, SKYGRND outdoors; 4-bit
    // packed, on the heap: back_free)
    bool     dk = false;
    // On a VGA screen (Darkness on the boards with PSRAM; set before the
    // loads): the tiles a byte a pixel in vga_px (255 x 64 bytes, heap)
    // instead of the sets above, `back` a byte a pixel too
    bool     vga = false;
    uint8_t* vga_px = nullptr;
    uint8_t* back = nullptr;
    uint8_t  back_id = 0;                    // which background `back` holds (file kind << 4 | block; 0 none)
};
void back_free(World& w);
// The VGA tiles' memory let go (vga_px; the background too)
void vga_free(World& w);

// An 8x8 tile block (a picture block 8 high, 1 column, N frames).
bool load_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, TileSet& out);

// Loads WALLDEF block `block` into wall set `set` (1-3) and on: a block of
// n x 780 bytes fills sets set .. set + n - 1 (up to set 3). *count = n.
bool load_walls(dax::ByteSource& src, const dax::Index& idx, int set, uint8_t block, World& w, int* count);

// The 8X8Dn block holding the tiles for part i of a WALLDEF block of n
// parts: the same number when n = 1, else block * 10 + i + 1 - block 0
// counting as 10 (101, 102, ...: Pool's program).
int tiles_block(uint8_t walldef_block, int n, int i);

bool load_horizon(dax::ByteSource& src, const dax::Index& idx, uint8_t block, World& w);

// Darkness: 8X8D1 block `block` (VGA, 256 tiles) into w's tile sets, in
// their EGA colours (w.vga: their values, in vga_px); w.dk set. pal: the
// block's palette entries (nullptr: not wanted)
bool load_vga_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, World& w,
                    pic::VgaPalette* pal = nullptr);
// Darkness: a background picture (BACK1 / SKYGRND, 88 x 88 VGA) into
// w.back in its EGA colours (w.vga: its values), remembered as `id` (not
// read again while it's the one held - pal then untouched)
bool load_back(dax::ByteSource& src, const dax::Index& idx, uint8_t block, uint8_t id, World& w,
               pic::VgaPalette* pal = nullptr);

// Draws tile number `id` at view cell (row, col) (0-10), colour 13 not drawn.
void draw_tile(pic::Canvas& c, const World& w, int id, int row, int col);

// The view from square (x, y) facing dir (0, 2, 4, 6).
void draw(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir, uint8_t sky);
// Pool of Radiance's background (no horizon picture; screen_facts.md 4):
// four flat bands - sky / ceiling (y 24-66), a line (67), a band (68-69)
// and the floor (70-111)
struct Bands { uint8_t sky, line, band, floor; };
void draw(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir, const Bands& b);
// Pool's bands from the sky colour and the two bytes its scripts set
// (VM 0xC059 floor, 0xC05F line / band; screen_facts.md 4): the line
// black for 0x08 / 0x41 else white, the band dark grey for 0x08 else light
// grey, the floor dark grey for 0x0B, blue for 0x06, else brown
Bands pool_bands(uint8_t sky, uint8_t floor_byte, uint8_t line_byte);

// The AREA map: 11 x 11 squares round the party (kept inside the map), the
// party as an arrow.
void draw_area_map(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir);

} // namespace view3d

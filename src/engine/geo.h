// GEO: a 3D area's map - the walls and doors on each square's four sides.
// The DAX games' maps are 16 x 16 squares; The Dark Queen of Krynn's
// (GEO.GLB) are any size up to kMaxSquares.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Block (1026 bytes; learned from coab, own code): 2 bytes skipped, then
// four 256-byte planes, a byte per square at x + y * 16:
//   plane 0: north wall type (high nibble), east (low nibble)
//   plane 1: south (high), west (low)
//   plane 2: a flags byte (>= 0x80 = under a roof: the indoor sky colour)
//   plane 3: a 2-bit door state per side: north bits 0-1, east 2-3,
//            south 4-5, west 6-7
// Wall type 0 = open; 1-15 = a wall drawn with wall set (t - 1) / 5, its
// piece (t - 1) % 5. A side with a wall is passable as its door state says:
// 0 solid, 1 open (a door or a passage), 2 locked, 3 (barred / magically
// locked). The map wraps at its edges.
//
// The Dark Queen of Krynn's maps (GEO.GLB entries by id; queen_facts_2.md
// 3): u8 width, u8 height, 6 header bytes (two triples of wall set ids -
// the 8X8DB / 8X8DC sets for wall types 1-5, 6-10, 11-15; 0xFF none), then
// the same four planes of width x height bytes (a byte per square at
// x + y * width). They don't wrap: a square off the map has no walls, and
// a step off it is the caller's (step() says so).
//
// Directions are the games': 0 north (y - 1), 2 east (x + 1), 4 south,
// 6 west; odd numbers are the diagonals.
#pragma once

#include <cstdint>

#include "dax.h"

namespace geo {

constexpr int kSize = 16;
constexpr uint32_t kBlockBytes = 1026;
// The largest map the engine takes (HLIB maps; Dark Queen's largest is
// 28 x 28 = 784 squares)
constexpr int kMaxSquares = 1024;
constexpr int kHlibHeader = 8;

enum Dir : uint8_t { North = 0, East = 2, South = 4, West = 6 };

int dx(int dir);
int dy(int dir);
const char* dir_name(int dir);       // "N", "NE", ...

struct Map {
    uint8_t plane[4][256];
    // A map bigger than 16 x 16 squares (HLIB): its four planes of w x h
    // bytes on the heap (release() frees it; a copy of the Map shares it)
    uint8_t* big = nullptr;
    uint8_t  w = kSize, h = kSize;
    bool     wraps = true;            // (the DAX games' maps)
    uint8_t  head[6] = {};            // HLIB maps: the header's wall set ids
    bool loaded = false;

    int squares() const { return w * h; }
    uint8_t*       pl(int k) { return big ? big + k * squares() : plane[k]; }
    const uint8_t* pl(int k) const { return big ? big + k * squares() : plane[k]; }
    bool on(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }
};

bool load(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Map& out);
// An HLIB map: the entry at pos (len bytes) of GEO.GLB (hlib::find_entry)
bool load_hlib(dax::ByteSource& src, uint32_t pos, uint32_t len, Map& out);
// Frees a big map's planes (the Map is 16 x 16 again, not loaded)
void release(Map& m);

// Wall type (0-15) on side `dir` (0, 2, 4, 6) of square (x, y); wraps (a
// map that doesn't: 0 off the map).
int wall(const Map& m, int x, int y, int dir);
// Door state (0-3) on that side.
int door(const Map& m, int x, int y, int dir);
uint8_t flags(const Map& m, int x, int y);

// 0 solid, 1 open, 2 locked, 3 barred: what stepping out of (x, y) towards
// dir meets (1 where there is no wall).
int passage(const Map& m, int x, int y, int dir);

// The square a step from (x, y) towards dir comes to (wrapped on a map
// that wraps); false when it leaves a map that doesn't (x, y kept)
bool step(const Map& m, int& x, int& y, int dir);

// Opens a locked door (door_facts.md): the side on (x, y) and the same wall
// seen from the next square (its opposite side) become 1 - each only when
// its square is on the map (no wrapping, as the original). The map in
// memory only: reloading the area locks it again.
void unlock(Map& m, int x, int y, int dir);

} // namespace geo

// GEO: a 3D area's map - 16 x 16 squares, the walls and doors on each
// square's four sides.
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
// Directions are the games': 0 north (y - 1), 2 east (x + 1), 4 south,
// 6 west; odd numbers are the diagonals.
#pragma once

#include <cstdint>

#include "dax.h"

namespace geo {

constexpr int kSize = 16;
constexpr uint32_t kBlockBytes = 1026;

enum Dir : uint8_t { North = 0, East = 2, South = 4, West = 6 };

int dx(int dir);
int dy(int dir);
const char* dir_name(int dir);       // "N", "NE", ...

struct Map {
    uint8_t plane[4][256];
    bool loaded = false;
};

bool load(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Map& out);

// Wall type (0-15) on side `dir` (0, 2, 4, 6) of square (x, y); wraps.
int wall(const Map& m, int x, int y, int dir);
// Door state (0-3) on that side.
int door(const Map& m, int x, int y, int dir);
uint8_t flags(const Map& m, int x, int y);

// 0 solid, 1 open, 2 locked, 3 barred: what stepping out of (x, y) towards
// dir meets (1 where there is no wall).
int passage(const Map& m, int x, int y, int dir);

} // namespace geo

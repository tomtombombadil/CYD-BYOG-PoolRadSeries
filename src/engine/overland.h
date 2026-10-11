// Pool of Radiance's wilderness (the Project's claude/poolrad_overland_facts.md):
// one 44 x 36 map of icon numbers kept in the program, of which the three
// wilderness areas are windows 16 columns wide (side by side, a column
// shared at each seam). The left box shows 5 x 5 of its 24 x 24 icons with
// the party - a rider - on the middle one; the arrow keys move it a square
// north, east, south or west (12 hours a step), unless the square is sea,
// coast or the stinking river.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// The map, the special squares and the colour maps are read from the
// player's program; the profile says where (facts only, own code).
#pragma once

#include <cstddef>
#include <cstdint>

namespace overland {

constexpr int kW = 44, kH = 36;          // the map, in squares
constexpr int kView = 5;                 // the squares shown each way
constexpr int kSpecials = 9;

// A test of an area word (the engine's script memory) for a special square
enum class Op : uint8_t { Never, Always, Ne, Bit, NoBit, Above };
struct Test {
    uint16_t word;
    Op       op;
    uint8_t  value;
};
// A special square shows another icon while any of its tests holds
struct Special {
    Test test[3];
};

// Where its numbers are (data segment offsets) and its rules, per release
struct Facts {
    uint16_t map;                        // the map, 44 a row
    uint16_t columns;                    // the three areas' first columns
    uint16_t specials;                   // the special squares: x, y, icon (words; 9)
    uint16_t heal[2], heal_map[2];       // the frames the healed land recolours, their colour maps
    uint8_t  heal_n[2];
    uint16_t healed;                     // the area word: the land healed (the river passable)
    uint8_t  river[2];                   // the stinking river's bytes (a range) ...
    uint8_t  river_also;                 // ... and one more
    const uint8_t* blocked;              // the bytes no step goes onto: (first, last) pairs
    uint8_t  n_blocked;
    Special  special[kSpecials];
    uint32_t river_words;                // GAME.OVR: "The river's stench drives you back."
};

// What is read from the program
struct Data {
    uint8_t  map[kW * kH];
    uint8_t  cols[3];
    uint16_t spec[kSpecials][3];
    uint8_t  heal[2][24];
    uint8_t  heal_map[2][16];
};

using ReadDs = bool (*)(void* ctx, uint16_t ds, uint8_t* out, size_t n);
bool read(Data& d, const Facts& f, ReadDs read, void* ctx);

// The global column of a local x in area `area` (0-2)
int column(const Data& d, int area, int x);
// The window's first square (the party's square in its middle where it can)
void window(int col, int row, int* x0, int* y0);

// The icon frame a square shows (0-255; -1 none), special squares as the
// area words say. get(ctx, word) reads the script memory.
using GetWord = uint16_t (*)(void* ctx, uint16_t word);
int frame(const Data& d, const Facts& f, int col, int row, GetWord get, void* ctx);

// A colour of a frame once the land is healed
uint8_t healed_colour(const Data& d, const Facts& f, int frame, uint8_t colour);

// A step from local (x, y) in area `area` facing dir (0 N, 2 E, 4 S, 6 W):
// Moved (x, y changed), Blocked (nothing happens), River (the stench drives
// them back: the river not passable yet)
enum class Step : uint8_t { Moved, Blocked, River };
Step step(const Data& d, const Facts& f, int area, int* x, int* y, int dir, bool healed);

} // namespace overland

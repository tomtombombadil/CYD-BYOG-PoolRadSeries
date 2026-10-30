// The Dark Queen of Krynn's 3D view and area map (queen_facts_2.md 4, 5),
// drawn from the player's 8X8DB / 8X8DC wall sets, BACK backdrops and
// TOPVIEW tiles (HLIB libraries; hlpic.h draws their parts).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_hlib.cpp.
//
// The view is 88 x 88 (the other games' box). A backdrop (BACK.TLB's
// picture of the look's number, 88 x 88) first, then the walls far to
// near. A wall type t (1-15) is piece (t - 1) % 5 of the map's wall set
// head[(t - 1) / 5] (geo::Map::head: an 8X8DB / 8X8DC id; 0xFF none). A
// set holds five pieces in 47 parts - pieces 0 and 1 ten parts, 2-4 nine
// (no second 8 x 8) - one a slot of the view:
//   f3 the front 3 squares on (8 wide, at 40, 40)     l2 / r2 the sides 2 on (32, 32 / 48, 32)
//   f2 2 squares on (24 wide, at 32, 32)              l1 / r1 1 on (16, 16 / 56, 16)
//   f1 1 square on (56 wide, at 16, 16)               l0 / r0 the party's square (0, 0 / 72, 0)
// a part drawn at its slot moved by its place (hlpic::place_x / y). For
// each distance, far first: the fronts of the squares beside the middle
// (5 columns each side 3 on, 2 at 2 on, 1 at 1 on; outside in) at the
// slot moved a front's width a column, then the side walls - the
// middle's, and 1 square on also the columns beside it (left sides left
// of the middle, right sides right) moved by the front's width.
// (Checked against DOSBox: 97-99% of the view's pixels on six views of
// a forest; the rest, in the far band, not explained yet.)
#pragma once

#include <cstdint>

#include "geo.h"
#include "hlib.h"
#include "hlpic.h"
#include "picture.h"

namespace qview {

constexpr int kView = 88;
constexpr uint8_t kNoSet = 0xFF;

// What the view is drawn with: the wall sets for wall types 1-5, 6-10,
// 11-15 and the backdrop (BACK.TLB id; 0: none - black)
struct Look {
    uint8_t  sets[3] = {kNoSet, kNoSet, kNoSet};
    uint16_t back = 0;
};
// The map's own sets (its header's first triple) with this backdrop
Look look_of(const geo::Map& m, uint16_t back);

// The libraries: the wall sets are looked for in walls[0] (8X8DB), then
// walls[1] (8X8DC); either may be null
struct Libs {
    hlib::ByteSource* walls[2] = {nullptr, nullptr};
    hlib::ByteSource* back = nullptr;
    hlib::ByteSource* topview = nullptr;
};

// The part of piece `piece` (0-4) for slot `slot` (Slot) in a set: its
// entry number in the set's library (1-47); -1 for a slot the piece hasn't
enum Slot : uint8_t { kF3, kF3b, kL2, kR2, kF2, kL1, kR1, kF1, kL0, kR0, kSlots };
int part_of(int piece, int slot);

// Draws the view of square (x, y) facing dir (0, 2, 4, 6) with its top left
// at (x0, y0). col: the colours as they come (the screen's 16-colour
// table; reset first); pal: the VGA colours (nullptr: not wanted). False:
// nothing could be read.
bool draw(pic::Canvas& c, const Libs& libs, const geo::Map& m, int x, int y, int dir, const Look& look,
          hlpic::Colours& col, pic::VgaPalette* pal = nullptr, int x0 = 24, int y0 = 24);

// The exploring screen's frame (queen_facts_2.md 6): FRAME.TLB's pieces at
// their places - the top, left, right and bottom bars, the view's frame,
// the compass (its needle: the north, south, west, east pictures 25-28) - and
// GEN.TLB's lower marble, in the colours of FRAME's colour part
// `colours` (its four looks: entries 41-44; DOSBox showed 42); the three
// boxes (party list, place / time, text) filled with entry 23. moons:
// the three moons' phases (0-3; -1: none drawn). The party menu's screen
// (dir < 0, no moons): the bars and the lower marble only. The libraries one at a
// time (frame, then gen; either null: left out).
constexpr int kFrameColours = 42;
bool draw_frame(pic::Canvas& c, hlib::ByteSource* frame, hlib::ByteSource* gen, int dir, const int moons[3],
                hlpic::Colours& col, pic::VgaPalette* pal = nullptr, int colours = kFrameColours);
// The compass alone (its dial and the needle for dir) - a turn
bool draw_compass(pic::Canvas& c, hlib::ByteSource& frame, int dir, hlpic::Colours& col, int colours = kFrameColours,
                  pic::VgaPalette* pal = nullptr);
// The boxes' insides (x, y, w, h): the party list, place / time, the text
struct Box { int16_t x, y, w, h; };
constexpr Box kPartyBox{136, 8, 176, 88}, kPlaceBox{205, 103, 107, 25}, kTextBox{8, 135, 304, 49};

// The area map: TOPVIEW's tiles - 1 + walls (bit 0 north, 1 east, 2 south,
// 3 west), the party's arrow 17 + dir / 2 - 11 x 11 squares round the
// party, the window kept on the map
bool draw_area_map(pic::Canvas& c, const Libs& libs, const geo::Map& m, int x, int y, int dir,
                   hlpic::Colours& col, int x0 = 24, int y0 = 24, pic::VgaPalette* pal = nullptr);

} // namespace qview

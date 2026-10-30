// HLIB pictures on the game canvas (The Dark Queen of Krynn - hlib.h's
// parts): a part drawn where it belongs, through its colours' EGA table on
// a 16-colour screen (the original's EGA mode: every pixel its palette
// entry's EGA colour, queen_facts.md 5) or as palette entries on a VGA
// canvas (Canvas::vga), its transparent pixels left alone.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_hlib.cpp.
//
// Where a part goes (queen_facts_2.md 4): its header's two signed words
// are the part's place negated, the first the rows, the second the
// columns - FRAME.TLB's pieces carry their screen place that way (the
// right bar's 0, -312: column 312), a wall's its place in its slot of the
// 3D view, a sprite its hot spot. place() gives it.
#pragma once

#include <cstdint>

#include "hlib.h"
#include "picture.h"

namespace hlpic {

// A part's place relative to an anchor: (anchor x - p.y, anchor y - p.x)
inline int place_x(const hlib::Part& p, int anchor_x) { return anchor_x - p.y; }
inline int place_y(const hlib::Part& p, int anchor_y) { return anchor_y - p.x; }

// What the pixels' palette entries become: ega[e] the EGA colour of entry
// e (entries no colours set: e & 15); on a VGA canvas the entry itself
struct Colours {
    uint8_t ega[256];
    void reset();                     // every entry e -> e & 15
};
// A colour entry (a 0x18 part / a picture's entry 0) into c (its EGA
// nibbles for its entries) and pal (its RGB, 6-bit DAC values made 8-bit,
// for a VGA screen; nullptr: not wanted - a pal already holding entries
// keeps them, its range made to take both). False: not a colour entry.
bool load_colours(hlib::ByteSource& src, uint32_t pos, uint32_t len, Colours& c, pic::VgaPalette* pal);

// A rectangle parts are clipped to (the 3D view's box ...)
struct Clip {
    int x0 = 0, y0 = 0, x1 = pic::kScreenW, y1 = pic::kScreenH;   // [x0, x1) x [y0, y1)
};

// Draws a part with its top left at (x, y): VGA kinds (0x10 / 0x11 / 0x12
// / 0x15 / 0x17) and EGA kinds 0x00 / 0x01 / 0xD2 (their colours as they
// are). Uses a 0.5 KB decoder on the heap while it draws. False: not a
// drawable part, a read error, or no memory.
bool draw(hlib::ByteSource& src, const hlib::Part& p, const Colours& col, pic::Canvas& c, int x, int y,
          const Clip& clip = Clip{});

// A picture library's (an HLIB entry of TILEs: entry 0 its colours) part
// `part` (1..), its colours into col first - drawn at its place from the
// anchor. False: no such part.
bool draw_picture_part(hlib::ByteSource& src, uint32_t pos, uint32_t len, int part, Colours& col,
                       pic::VgaPalette* pal, pic::Canvas& c, int anchor_x, int anchor_y, const Clip& clip = Clip{});

} // namespace hlpic

// HLIB libraries: the storage of The Dark Queen of Krynn (and Unlimited
// Adventures) in place of the older games' DAX files - .TLB files hold
// pictures ("tiles"), .GLB files data (scripts, maps, monsters, sounds).
// Facts from the player's own files (the Project's claude/queen_facts.md);
// nothing here comes from the games.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_hlib.cpp.
// Reads through dax::ByteSource like the DAX code; nothing allocates and
// nothing holds a whole entry: entries are found by reading a few header
// bytes, pictures are decoded a row at a time.
//
// A library (all little-endian):
//   "HLIB", u32 size (the whole library), u16 count, u8 0, u8 flag,
//   4-char kind ("TILE", "DATA", "DIG4", or "HLIB": a library of libraries),
//   then count + 1 u32 offsets from the library's start (the last = size).
//   flag 1: entry 0 is a directory - u16 n, then n x (u16 id, u16 entry) -
//   and the game asks for entries by id (like a DAX block number).
//   An entry may itself be a library (a picture: TILE entries in an HLIB).
//
// Picture parts (TILE entries): an 8-byte header - u16 height, i16 x, i16 y
// (the hot spot; may be negative), u8 width in units, u8 kind - then the
// pixels. VGA kinds count the width in 4-pixel units and keep the pixels
// in mode X order (plane p = the pixels at x % 4 == p); EGA kinds count
// 8-pixel units and keep bit planes. A part's data is padded to an even
// length.
//   0x10 VGA, the 4 planes one after the other (h x units bytes each)
//   0x15 the same, 0xFF = transparent
//   0x11 the same with a mask before it (4 planes of 0x00 drawn / 0xFF not)
//   0x12 VGA run-length coded (below); each row = its 4 planes in turn
//   0x17 VGA sprite: plane by plane, each row segments - a byte c: 0 = end
//        of the row; c >= 0x80: skip 256 - c pixels (of the plane), then a
//        count byte n; c < 0x80: n = c (no skip) - then n pixels
//   0x00 EGA, 4 bit planes one after the other (h x units bytes each)
//   0x01 the same after a mask plane (bit set = transparent)
//   0xD2 EGA run-length coded; each row = its 4 planes in turn
//   0x03 EGA sprite (not decoded yet)
//   0x18 / 0x08 colours (VGA / EGA file) - see Colours
//   0x19 not decoded yet (6-byte records; an animation's repeated frame?)
// Run-length coding: a byte c < 0x80 = c + 1 bytes as they are; else the
// next byte 257 - c times (DAX's runs are 256 - c).
//
// Colours (a picture library's entry 0, or a 0x18 / 0x08 part): u8 flags,
// u8 0, u16 first, u16 count, u8 cycles, u8 0x18 (VGA) / 0x08 (EGA). VGA:
// count x RGB (8 bits each) for palette entries first.., cycles x 4 bytes
// (colour cycling), then count EGA colours as nibbles (high nibble first) -
// the 16-colour version of each entry. The EGA install's pictures are the
// VGA ones with every pixel put through that table (checked pixel for
// pixel), so the engine draws the VGA files in 16 colours the same way.
// EGA files keep only the first 16 RGB triples (48 bytes).
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace hlib {

using dax::ByteSource;

enum class Status : uint8_t {
    Ok,
    ReadError,     // the source gave fewer bytes than needed
    NotHlib,       // no "HLIB" header there
    BadOffsets,    // the entry offsets don't fit the library
};
const char* status_text(Status s);

constexpr uint32_t kHeaderSize = 16;

// One library: a file, or a library inside an entry of another
struct Library {
    uint32_t base = 0;        // where its "HLIB" is in the source
    uint32_t size = 0;        // its bytes
    uint16_t count = 0;       // entries (the directory included)
    bool     directory = false;
    char     kind[5] = {};    // "TILE", "DATA", "DIG4", "HLIB"

    int first_item() const { return directory ? 1 : 0; }
};

// Opens the library at `base`; `room` = the bytes it may take (the file's
// size, or the entry's length when nested). The offsets are checked lazily.
Status open(ByteSource& src, uint32_t base, uint32_t room, Library& out);

// Entry i's place in the source; false when i is out of range or its
// offsets don't fit the library
bool entry(ByteSource& src, const Library& lib, int i, uint32_t& pos, uint32_t& len);

// Is the entry a library itself?
bool is_library(ByteSource& src, uint32_t pos, uint32_t len);

// The directory: how many ids, and item k's id and entry index
int  dir_count(ByteSource& src, const Library& lib);
bool dir_item(ByteSource& src, const Library& lib, int k, uint16_t& id, uint16_t& index);
// The entry with this id (with no directory, the id is the entry's index);
// -1 when there is none
int find(ByteSource& src, const Library& lib, uint16_t id);
// The id of entry i (the directory's, else i); -1 for the directory itself
int id_of(ByteSource& src, const Library& lib, int i);

// ---- Pictures --------------------------------------------------------------

enum Kind : uint8_t {
    kVgaPlanar = 0x10, kVgaMasked = 0x11, kVgaRle = 0x12, kVgaKeyed = 0x15, kVgaSprite = 0x17,
    kVgaColours = 0x18, kShapes = 0x19,
    kEgaPlanar = 0x00, kEgaMasked = 0x01, kEgaSprite = 0x03, kEgaColours = 0x08, kEgaRle = 0xD2,
};
constexpr int kPartHeader = 8;
constexpr int kMaxWidth = 320;    // pixels a row (the decoder's buffers)

struct Part {
    uint32_t pos = 0, len = 0;     // the whole entry (header included)
    uint16_t height = 0;
    int16_t  x = 0, y = 0;
    uint8_t  units = 0, kind = 0;

    bool ega() const;                                   // an EGA kind (8-pixel units, bit planes)
    int  width() const { return units * (ega() ? 8 : 4); }
    bool colours() const { return kind == kVgaColours || kind == kEgaColours; }
    bool drawable() const;                              // the decoder knows its pixels
};
bool read_part(ByteSource& src, uint32_t pos, uint32_t len, Part& out);

// A colour entry: entry 0 of a picture library, or a 0x18 / 0x08 part
struct Colours {
    uint16_t first = 0, count = 0;
    uint8_t  cycles = 0;
    bool     vga = false;          // count RGB triples + EGA nibbles; else 16 RGB only
    uint32_t rgb_pos = 0;          // the RGB triples
    uint32_t ega_pos = 0;          // the nibbles (VGA only)

    int rgb_count() const { return vga ? count : 16; }
};
bool read_colours(ByteSource& src, uint32_t pos, uint32_t len, Colours& out);
// RGB of colour i (0.. rgb_count() - 1) into rgb[3]
bool colour_rgb(ByteSource& src, const Colours& c, int i, uint8_t* rgb);
// The EGA colours of entries first.. first + count - 1 into ega[0..count) -
// a VGA file's table; false for an EGA file (its pixels are EGA already)
bool ega_table(ByteSource& src, const Colours& c, uint8_t* ega, int cap);

// Decodes a part's pixels a row at a time, top to bottom. VGA kinds give
// palette indexes, EGA kinds colours 0-15. seen[x] (optional) = 1 where the
// row has a pixel, 0 where it is transparent (px keeps 0 there).
class Pixels {
public:
    Pixels(ByteSource& src, const Part& p);
    bool ok() const { return ok_; }
    int  width() const { return w_; }
    int  rows_done() const { return row_; }
    bool row(uint8_t* px, uint8_t* seen = nullptr);

private:
    // A buffered forward reader over part of the source
    struct Cursor {
        ByteSource* src = nullptr;
        uint32_t pos = 0, end = 0;
        uint8_t  buf[32];
        uint8_t  n = 0, at = 0;
        bool     bad = false;
        void set(ByteSource& s, uint32_t p, uint32_t e) { src = &s; pos = p; end = e; n = at = 0; bad = false; }
        int  next();
    };
    bool rle_bytes(uint8_t* out, int n);
    bool sprite_row(int plane, uint8_t* px, uint8_t* seen);
    bool skip_sprite_row(int plane);

    ByteSource& src_;
    Part     p_;
    int      w_ = 0, row_ = 0;
    bool     ok_ = false;
    Cursor   cur_[4];              // RLE: [0]; a VGA sprite: one a plane
    int      run_ = 0, rep_ = -1;  // RLE state
    uint8_t  tmp_[kMaxWidth];
};

// ---- Data entries ----------------------------------------------------------

// Streams an entry's bytes (GLB entries are stored as they are) - the same
// calls as dax::RleReader, so code that reads DAX blocks byte by byte can
// read HLIB entries too
class EntryReader {
public:
    EntryReader(ByteSource& src, uint32_t pos, uint32_t len) : src_(src), pos_(pos), left_(len), len_(len) {}
    int      next();
    size_t   read(uint8_t* buf, size_t n);
    size_t   skip(size_t n);
    uint32_t produced() const { return len_ - left_; }
    bool     error() const { return error_; }

private:
    ByteSource& src_;
    uint32_t pos_, left_, len_;
    bool     error_ = false;
};

// The place of the entry with this id in a file's top library, in one call
// (ECL.GLB's scripts, GEO.GLB's maps ...)
bool find_entry(ByteSource& src, uint16_t id, uint32_t& pos, uint32_t& len);

} // namespace hlib

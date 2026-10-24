// Gold Box EGA pictures and the 8-bit canvas the engine draws into.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// The engine draws everything into one 320x200 canvas of palette indexes,
// like the original game's EGA screen; ui/frame.* turns it into display
// colours (and scales it on 480x320 panels). Index 16 = transparent.
//
// EGA picture block (format confirmed for Curse of the Azure Bonds; to be
// checked against Tom's own Pool of Radiance / Silver Blades files with the
// asset viewer - Pools of Darkness's VGA pictures are a different format):
//   u16 height        pixel rows
//   u16 width         in 8-pixel columns
//   u16 x, u16 y      default position, in 8-pixel cells (meaning unconfirmed)
//   u8  frames        pictures in the block (animations, icon sets)
//   u8  extra[8]      unknown
//   frames x height x (width * 4) bytes: two pixels a byte, high nibble first
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace pic {

constexpr int kScreenW = 320;
constexpr int kScreenH = 200;
constexpr uint8_t kTransparent = 16;

struct Rgb { uint8_t r, g, b; };

// The standard 16-colour EGA palette (index -> RGB).
extern const Rgb kEga[16];

// A screen's pixels: palette indexes, a byte each (bpp 8) or two a byte
// (bpp 4: the left pixel in the high nibble; EGA's 16 colours - v0.87.0,
// Tom 2026-10-12 "focus on memory": the game screen in half the 64 KB).
// Pixels go through get / set (the callers clip); px is the raw memory.
struct Canvas {
    uint8_t* px;          // stride() * h bytes
    int w, h;
    uint8_t bpp = 8;
    // The screen shows VGA pictures in their own 256 colours (Pools of
    // Darkness on the boards with PSRAM: an 8-bit canvas whose values are
    // the VGA palette's entries); false: in their EGA colours, as the
    // original's EGA mode (any depth - darkness_facts.md 7)
    bool vga = false;
    // Treasures in 256 colours: its background picture (BORD 1, w bytes a
    // row, bg_rows rows) - a fill with colour 0 puts the picture back there
    // instead of black, as the original's clear (its img 0x17A3; rows from
    // bg_rows on stay black); fill_black is black always. 8-bit canvas only;
    // nullptr: none
    const uint8_t* bg = nullptr;
    int bg_rows = 0;
    int stride() const { return bpp == 4 ? (w + 1) >> 1 : w; }
    size_t bytes() const { return static_cast<size_t>(stride()) * h; }
    uint8_t get(int x, int y) const
    {
        if (bpp != 4) return px[y * w + x];
        const uint8_t b = px[y * stride() + (x >> 1)];
        return (x & 1) ? b & 0x0F : b >> 4;
    }
    void set(int x, int y, uint8_t c)
    {
        if (bpp != 4) {
            px[y * w + x] = c;
            return;
        }
        uint8_t& b = px[y * stride() + (x >> 1)];
        b = (x & 1) ? static_cast<uint8_t>((b & 0xF0) | (c & 0x0F)) : static_cast<uint8_t>((b & 0x0F) | (c << 4));
    }
    // n pixels of row y from x, unpacked (a byte each), out / in
    void get_row(int x, int y, int n, uint8_t* out) const;
    void put_row(int x, int y, int n, const uint8_t* in);
    void clear(uint8_t c);
    void fill(int x, int y, int w, int h, uint8_t c);
    void fill_black(int x, int y, int fw, int fh)
    {
        const uint8_t* was = bg;
        bg = nullptr;
        fill(x, y, fw, fh, 0);
        bg = was;
    }
};

constexpr size_t kHeaderSize = 17;

// Gateway to the Savage Frontier's pictures (gateway_facts.md 3; the same
// first 9 bytes, then):
//   u8  flags         bit 0: 8 bytes follow (not used on EGA), bit 1: the EGA
//                     colour map follows, bit 3: 24 bytes follow (VGA
//                     colours, not used on EGA); bit 2 always set
//   [8 bytes]         (bit 0)
//   [16 bytes]        (bit 1) the colour map: 32 nibbles, low nibble first -
//                     pixel value v (0-31) is drawn in EGA colour map[v]
//   [24 bytes]        (bit 3)
//   frames x height x width x 5 bytes: each row's five bit planes (plane 0
//   first, w bytes each, the leftmost pixel in a byte's top bit) - a pixel's
//   value is the planes' bits (plane p = bit p)
// A picture without a map is drawn with the last map read (its program
// keeps one: sf_map). Checked pixel for pixel against the original in
// DOSBox (EGA): TITLE.DAX's pictures.
struct Header {
    uint16_t height = 0;
    uint16_t width_cols = 0;
    uint16_t x_cell = 0, y_cell = 0;
    uint8_t  frames = 0;
    uint8_t  extra[8] = {};
    uint8_t  sf = 0;                 // Savage Frontier: its flags byte (0: the 4-bit format)
    uint8_t  pre = 0;                // SF: header bytes past the first 17 (the colour map's rest ...)

    int width_px() const { return width_cols * 8; }
    // One frame's pixels as packed nibbles (in memory: draw_pixels)
    uint32_t frame_bytes() const { return static_cast<uint32_t>(height) * width_cols * 4; }
    // One frame as stored in the block (SF: five planes a row)
    uint32_t stored_bytes() const { return static_cast<uint32_t>(height) * width_cols * (sf ? 5 : 4); }
};

// Parses the 17 header bytes. True only if the sizes make sense for a block
// of raw_size bytes and the frames fill it exactly (true of every picture in
// Tom's PoolRad, Curse and Secret files; looser checks took some Pools of
// Darkness blocks for pictures). *extra_bytes is always 0 when it's true.
// A Savage Frontier picture (above) is taken too: sf set.
bool parse_header(const uint8_t* p, uint32_t raw_size, Header& out, uint32_t* extra_bytes = nullptr);

// The Savage Frontier header's flags: the bytes its parts take
int sf_extra(uint8_t flags);
// The colour map last read (SF pictures without one are drawn with it):
// 16 bytes, 32 nibbles; reset to colours 0-15 as themselves, 16-31 black
uint8_t* sf_map();
void sf_map_reset();
// Raw value 13 kept as 13 (the see-through value) rather than mapped - the
// default; off while a head or body picture is drawn (Gateway's vault
// keeper's body: its 13s through its map, DOSBox)
void sf_keep13(bool on);
// Five planes of `cols` bytes (one row) as packed nibbles (cols * 4 bytes)
// through a colour map
void sf_row(const uint8_t* planes, int cols, const uint8_t* map, uint8_t* packed);

// Draws frame `frame` of a picture whose header has been read from r (r is
// positioned just after the header). Pixels equal to mask (0-15) are left
// out; pass -1 to draw every pixel. Clipped to the canvas.
// Returns false if the block ran out of data.
bool draw(dax::RleReader& r, const Header& h, int frame, Canvas& c, int x, int y, int mask = -1);

// Reads frame `frame` into out (h.frame_bytes(): packed nibbles), r
// positioned just after the 17 header bytes (an SF picture's colour map
// read on the way and converted through)
bool read_frame(dax::RleReader& r, const Header& h, int frame, uint8_t* out);
// The same frame by frame: begin_frames after the 17 header bytes (an SF
// picture's colour map into map, 16 bytes), then next_frame for each
bool begin_frames(dax::RleReader& r, const Header& h, uint8_t* map);
bool next_frame(dax::RleReader& r, const Header& h, const uint8_t* map, uint8_t* out);

// Packed pixels already in memory (h.frame_bytes() bytes of one frame).
// mask: a colour left out (-1: none); from / to: a colour drawn as another
// (sprites: 0 is see-through and 13 is drawn black)
void draw_pixels(const uint8_t* data, const Header& h, Canvas& c, int x, int y, int mask = -1, int from = -1,
                 int to = 0);

// ---- Animations ------------------------------------------------------------
// Event pictures and sprites (PICn, FINALn, SPRITn ... in Curse): a series of
// frames, each with its own header (format learned from coab; Tom's real
// Curse PIC1.DAX blocks didn't parse as single pictures):
//   u8  frames
//   per frame:
//     u32 delay       how long it shows (game ticks)
//     u16 height, u16 width (8-px columns), u16 x, u16 y, u8 unknown
//     u8  extra[8]
//     height x width * 4 bytes of packed pixels
// In PIC and FINAL files every frame after the first is stored XORed with
// the first frame's packed bytes (all but the last byte, as the original
// program does it) - only the changes from the first frame are non-zero.
// Gateway's (the Savage Frontier format above): per frame u32 delay, the
// first 9 header bytes, the flags and their parts, the planes; frames past
// the first carry no colour map (the first frame's) and are stored XORed
// with the first frame's planes.
constexpr size_t kAnimFrameHeader = 21;
constexpr int kMaxAnimFrames = 32;

struct Anim {
    int      frames = 0;
    Header   frame[kMaxAnimFrames];      // frames = 1 in each
    uint32_t delay[kMaxAnimFrames];
    uint32_t data_at[kMaxAnimFrames];    // offset of the frame's pixels in the block
    uint8_t  map[16] = {};               // SF: the colour map (frame[0].sf set)
};

// Reads the whole block from r (positioned at its start). True only if the
// frame headers chain up to exactly raw_size bytes. sf: Gateway's format
bool parse_anim(dax::RleReader& r, uint32_t raw_size, Anim& out, bool sf = false);

// Draws one frame. xor_first: the block is from a PIC / FINAL file.
// Needs a scratch buffer of the first frame's size when xor_first and
// frame > 0 (allocated here, freed before returning). False on short data
// or no memory.
bool draw_anim(dax::ByteSource& src, const dax::Index& idx, const dax::Entry& e, const Anim& a, int frame,
               bool xor_first, Canvas& c, int x, int y, int mask = -1, int from = -1, int to = 0);

// ---- VGA pictures (Pools of Darkness) -------------------------------------
// Worked out from Tom's GOG Darkness files (2026-10-06); TITLE, COMSPR,
// CHEAD and BORDERS blocks fill exactly:
//   u8  height        pixel rows
//   u8  width         in 8-pixel columns
//   u16 x, u16 y      (zero so far)
//   u16 frames        (8X8D0 block 202: 20 tiles; 8X8D1's wall sets 256)
//   u8  first         first palette index the picture sets
//   u8  count - 1     palette entries it sets
//   count x 3 bytes   palette, 6-bit VGA values (0-63)
//   (count + 1) / 2   one nibble per entry: its EGA colour (for EGA cards -
//                     the original's EGA mode draws exactly these: TITLE.DAX
//                     in DOSBox, 2026-10-18; high nibble first)
//   4 bytes           unknown (zero so far)
//   frames x height x width bytes: one palette index per pixel
constexpr size_t kVgaHeaderSize = 10;

struct VgaHeader {
    uint8_t  height = 0;
    uint8_t  width_cols = 0;
    uint16_t frames = 0;
    uint8_t  first = 0;
    uint16_t count = 0;            // palette entries
    uint32_t pixels_at = 0;        // offset of the first frame's pixels

    int width_px() const { return width_cols * 8; }
    uint32_t frame_bytes() const { return static_cast<uint32_t>(height) * width_px(); }
};

// The palette entries a VGA block sets, at their own indexes: rgb[first ..
// first + count - 1] (8-bit RGB from the block's 6-bit values)
struct VgaPalette {
    Rgb rgb[256];
    int first = 0, count = 0;
};

// Parses the 10 header bytes; true only if the block's size matches exactly.
bool parse_vga_header(const uint8_t* p, uint32_t raw_size, VgaHeader& out);

// Reads the palette entries from r (positioned just after the 10-byte
// header) into rgb[first .. first + count - 1] as 8-bit RGB.
bool read_vga_palette(dax::RleReader& r, const VgaHeader& h, Rgb* rgb256);

// Reads the EGA nibbles from r (positioned just after the palette) into
// ega[first .. first + count - 1] (0-15)
bool read_vga_ega(dax::RleReader& r, const VgaHeader& h, uint8_t* ega256);

// Draws frame `frame`; r positioned at the block start. Pixels equal to mask
// (0-255) are left out; -1 draws all. map (256 entries; nullptr: none) turns
// each pixel into the colour drawn - an EGA screen's colours (read_vga_ega).
bool draw_vga(dax::RleReader& r, const VgaHeader& h, int frame, Canvas& c, int x, int y, int mask = -1,
              const uint8_t* map = nullptr);

// A whole VGA block drawn as the game's screen shows it: in its EGA colours
// (the original's EGA mode - Pools of Darkness on the classic boards), on
// a VGA canvas (c.vga) its palette indexes; the palette entries it sets
// into pal (nullptr: not wanted). False: not a VGA picture block or short
// data.
bool draw_vga_block(dax::ByteSource& src, const dax::Index& idx, const dax::Entry& e, int frame, Canvas& c, int x,
                    int y, int mask = -1, VgaPalette* pal = nullptr);

// ---- Darkness's animations (PIC1, SPRIT1; darkness_facts.md 7 4.2) --------
// Their own header: u16 height, u16 width in 8-px columns, u16 x, u16 y, u8
// frames - 1 (at most 8), u8 first palette entry, u8 count (not count - 1),
// count x 3 palette, count / 2 EGA nibbles, 4 bytes, u8 key frame, u8 the
// frame the animation goes back to, then per frame a u16 packed size and a
// u8 delay (tenths of a second; 99: it stops there); then the frames, each
// packed - a byte c <= 0x7F: the next c + 1 bytes as they are, c >= 0x80:
// the next byte 257 - c times - and every frame but the key frame XORed
// with the key frame.
constexpr int kDkMaxFrames = 9;
struct DkAnim {
    uint16_t height = 0, width_cols = 0;
    uint16_t x = 0, y = 0;           // where in the view (px from its corner; the files' 0, 0)
    uint8_t  frames = 0, first = 0, key = 0, loop = 0;
    uint16_t count = 0;
    uint16_t size[kDkMaxFrames] = {};
    uint8_t  delay[kDkMaxFrames] = {};
    uint32_t data_at = 0;            // the first frame's packed bytes
    uint8_t  ega[256] = {};          // each value's EGA colour (values under 16: themselves)
    int width_px() const { return width_cols * 8; }
};
// Reads the header (r at the block's start); true only if the frames'
// packed sizes end exactly at the block's end. pal: its palette entries
// (nullptr: not wanted)
bool parse_dk_anim(dax::RleReader& r, uint32_t raw_size, DkAnim& a, VgaPalette* pal = nullptr);
// Frame k at (x, y): in its EGA colours, on a VGA canvas (c.vga) the
// values. sprite: an approaching monster's (0 and 13 swapped, then 13
// see-through); mask: another see-through value (-1: none)
bool draw_dk_anim(dax::ByteSource& src, const dax::Index& idx, const dax::Entry& e, const DkAnim& a, int frame,
                  Canvas& c, int x, int y, bool sprite = false, int mask = -1);

} // namespace pic

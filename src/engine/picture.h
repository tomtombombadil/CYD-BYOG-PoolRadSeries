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

struct Canvas {
    uint8_t* px;          // w * h palette indexes
    int w, h;
    void clear(uint8_t c);
    void fill(int x, int y, int w, int h, uint8_t c);
};

constexpr size_t kHeaderSize = 17;

struct Header {
    uint16_t height = 0;
    uint16_t width_cols = 0;
    uint16_t x_cell = 0, y_cell = 0;
    uint8_t  frames = 0;
    uint8_t  extra[8] = {};

    int width_px() const { return width_cols * 8; }
    uint32_t frame_bytes() const { return static_cast<uint32_t>(height) * width_cols * 4; }
};

// Parses the 17 header bytes. True only if the sizes make sense for a block
// of raw_size bytes and the frames fill it exactly (true of every picture in
// Tom's PoolRad, Curse and Secret files; looser checks took some Pools of
// Darkness blocks for pictures). *extra_bytes is always 0 when it's true.
bool parse_header(const uint8_t* p, uint32_t raw_size, Header& out, uint32_t* extra_bytes = nullptr);

// Draws frame `frame` of a picture whose header has been read from r (r is
// positioned just after the header). Pixels equal to mask (0-15) are left
// out; pass -1 to draw every pixel. Clipped to the canvas.
// Returns false if the block ran out of data.
bool draw(dax::RleReader& r, const Header& h, int frame, Canvas& c, int x, int y, int mask = -1);

// Packed pixels already in memory (h.frame_bytes() bytes of one frame).
void draw_pixels(const uint8_t* data, const Header& h, Canvas& c, int x, int y, int mask = -1);

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
constexpr size_t kAnimFrameHeader = 21;
constexpr int kMaxAnimFrames = 32;

struct Anim {
    int      frames = 0;
    Header   frame[kMaxAnimFrames];      // frames = 1 in each
    uint32_t delay[kMaxAnimFrames];
    uint32_t data_at[kMaxAnimFrames];    // offset of the frame's pixels in the block
};

// Reads the whole block from r (positioned at its start). True only if the
// frame headers chain up to exactly raw_size bytes.
bool parse_anim(dax::RleReader& r, uint32_t raw_size, Anim& out);

// Draws one frame. xor_first: the block is from a PIC / FINAL file.
// Needs a scratch buffer of the first frame's size when xor_first and
// frame > 0 (allocated here, freed before returning). False on short data
// or no memory.
bool draw_anim(dax::ByteSource& src, const dax::Index& idx, const dax::Entry& e, const Anim& a, int frame,
               bool xor_first, Canvas& c, int x, int y, int mask = -1);

// ---- VGA pictures (Pools of Darkness) -------------------------------------
// Worked out from Tom's GOG Darkness files (2026-10-06); TITLE, COMSPR,
// CHEAD and BORDERS blocks fill exactly:
//   u8  height        pixel rows
//   u8  width         in 8-pixel columns
//   u16 x, u16 y      (zero so far)
//   u8  frames
//   u8  unknown
//   u8  first         first palette index the picture sets
//   u8  count - 1     palette entries it sets
//   count x 3 bytes   palette, 6-bit VGA values (0-63)
//   (count + 1) / 2   one nibble per entry: its EGA colour (for EGA cards)
//   4 bytes           unknown (zero so far)
//   frames x height x width bytes: one palette index per pixel
constexpr size_t kVgaHeaderSize = 10;

struct VgaHeader {
    uint8_t  height = 0;
    uint8_t  width_cols = 0;
    uint8_t  frames = 0;
    uint8_t  first = 0;
    uint16_t count = 0;            // palette entries
    uint32_t pixels_at = 0;        // offset of the first frame's pixels

    int width_px() const { return width_cols * 8; }
    uint32_t frame_bytes() const { return static_cast<uint32_t>(height) * width_px(); }
};

// Parses the 10 header bytes; true only if the block's size matches exactly.
bool parse_vga_header(const uint8_t* p, uint32_t raw_size, VgaHeader& out);

// Reads the palette entries from r (positioned just after the 10-byte
// header) into rgb[first .. first + count - 1] as 8-bit RGB.
bool read_vga_palette(dax::RleReader& r, const VgaHeader& h, Rgb* rgb256);

// Draws frame `frame`; r positioned at the block start. Pixels equal to mask
// (0-255) are left out; -1 draws all.
bool draw_vga(dax::RleReader& r, const VgaHeader& h, int frame, Canvas& c, int x, int y, int mask = -1);

} // namespace pic

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
// of raw_size bytes (some data, and frames * frame_bytes fits). *extra_bytes
// (optional) gets how many bytes are left over after the last frame.
bool parse_header(const uint8_t* p, uint32_t raw_size, Header& out, uint32_t* extra_bytes = nullptr);

// Draws frame `frame` of a picture whose header has been read from r (r is
// positioned just after the header). Pixels equal to mask (0-15) are left
// out; pass -1 to draw every pixel. Clipped to the canvas.
// Returns false if the block ran out of data.
bool draw(dax::RleReader& r, const Header& h, int frame, Canvas& c, int x, int y, int mask = -1);

} // namespace pic

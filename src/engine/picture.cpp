#include "picture.h"

#include <cstring>

namespace pic {

const Rgb kEga[16] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xAA}, {0x00, 0xAA, 0x00}, {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00}, {0xAA, 0x00, 0xAA}, {0xAA, 0x55, 0x00}, {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55}, {0x55, 0x55, 0xFF}, {0x55, 0xFF, 0x55}, {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55}, {0xFF, 0x55, 0xFF}, {0xFF, 0xFF, 0x55}, {0xFF, 0xFF, 0xFF},
};

void Canvas::clear(uint8_t c) { memset(px, c, static_cast<size_t>(w) * h); }

void Canvas::fill(int x, int y, int fw, int fh, uint8_t c)
{
    if (x < 0) { fw += x; x = 0; }
    if (y < 0) { fh += y; y = 0; }
    if (x + fw > w) fw = w - x;
    if (y + fh > h) fh = h - y;
    if (fw <= 0 || fh <= 0) return;
    for (int row = 0; row < fh; ++row) memset(px + (y + row) * w + x, c, fw);
}

bool parse_header(const uint8_t* p, uint32_t raw_size, Header& out, uint32_t* extra_bytes)
{
    if (raw_size < kHeaderSize) return false;
    out.height = static_cast<uint16_t>(p[0] | (p[1] << 8));
    out.width_cols = static_cast<uint16_t>(p[2] | (p[3] << 8));
    out.x_cell = static_cast<uint16_t>(p[4] | (p[5] << 8));
    out.y_cell = static_cast<uint16_t>(p[6] | (p[7] << 8));
    out.frames = p[8];
    memcpy(out.extra, p + 9, 8);
    // Nothing in these games is bigger than the screen.
    if (out.height == 0 || out.height > kScreenH) return false;
    if (out.width_cols == 0 || out.width_cols > kScreenW / 8) return false;
    if (out.frames == 0) return false;
    const uint64_t need = kHeaderSize + static_cast<uint64_t>(out.frames) * out.frame_bytes();
    if (need > raw_size) return false;
    if (extra_bytes) *extra_bytes = static_cast<uint32_t>(raw_size - need);
    return true;
}

bool draw(dax::RleReader& r, const Header& h, int frame, Canvas& c, int x, int y, int mask)
{
    if (frame < 0 || frame >= h.frames) return false;
    const uint32_t skip = static_cast<uint32_t>(frame) * h.frame_bytes();
    if (r.skip(skip) != skip) return false;
    const int row_bytes = h.width_cols * 4;
    for (int row = 0; row < h.height; ++row) {
        const int py = y + row;
        const bool row_in = py >= 0 && py < c.h;
        uint8_t* line = row_in ? c.px + py * c.w : nullptr;
        for (int i = 0; i < row_bytes; ++i) {
            const int b = r.next();
            if (b < 0) return false;
            if (!row_in) continue;
            const int px = x + i * 2;
            const uint8_t hi = static_cast<uint8_t>(b >> 4), lo = static_cast<uint8_t>(b & 0x0F);
            if (px >= 0 && px < c.w && hi != mask) line[px] = hi;
            if (px + 1 >= 0 && px + 1 < c.w && lo != mask) line[px + 1] = lo;
        }
    }
    return true;
}

} // namespace pic

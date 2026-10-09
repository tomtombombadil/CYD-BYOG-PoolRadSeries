#include "picture.h"

#include <cstring>
#include <new>

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
    if (need != raw_size) return false;
    if (extra_bytes) *extra_bytes = 0;
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

void draw_pixels(const uint8_t* data, const Header& h, Canvas& c, int x, int y, int mask, int from, int to)
{
    const int row_bytes = h.width_cols * 4;
    for (int row = 0; row < h.height; ++row) {
        const int py = y + row;
        if (py < 0 || py >= c.h) continue;
        uint8_t* line = c.px + py * c.w;
        const uint8_t* src = data + row * row_bytes;
        for (int i = 0; i < row_bytes; ++i) {
            const int px = x + i * 2;
            const uint8_t hi = static_cast<uint8_t>(src[i] >> 4), lo = static_cast<uint8_t>(src[i] & 0x0F);
            if (px >= 0 && px < c.w && hi != mask) line[px] = hi == from ? static_cast<uint8_t>(to) : hi;
            if (px + 1 >= 0 && px + 1 < c.w && lo != mask) line[px + 1] = lo == from ? static_cast<uint8_t>(to) : lo;
        }
    }
}

bool parse_anim(dax::RleReader& r, uint32_t raw_size, Anim& out)
{
    out.frames = 0;
    const int n = r.next();
    if (n <= 0 || n > kMaxAnimFrames) return false;
    uint32_t at = 1;
    for (int f = 0; f < n; ++f) {
        uint8_t hd[kAnimFrameHeader];
        if (r.read(hd, sizeof hd) != sizeof hd) return false;
        at += kAnimFrameHeader;
        Header& h = out.frame[f];
        out.delay[f] = static_cast<uint32_t>(hd[0] | (hd[1] << 8) | (hd[2] << 16)) | (static_cast<uint32_t>(hd[3]) << 24);
        h.height = static_cast<uint16_t>(hd[4] | (hd[5] << 8));
        h.width_cols = static_cast<uint16_t>(hd[6] | (hd[7] << 8));
        h.x_cell = static_cast<uint16_t>(hd[8] | (hd[9] << 8));
        h.y_cell = static_cast<uint16_t>(hd[10] | (hd[11] << 8));
        memcpy(h.extra, hd + 13, 8);
        h.frames = 1;
        if (h.height == 0 || h.height > kScreenH || h.width_cols == 0 || h.width_cols > kScreenW / 8) return false;
        out.data_at[f] = at;
        const uint32_t fb = h.frame_bytes();
        if (at + fb > raw_size || r.skip(fb) != fb) return false;
        at += fb;
    }
    if (at != raw_size) return false;
    out.frames = n;
    return true;
}

bool draw_anim(dax::ByteSource& src, const dax::Index& idx, const dax::Entry& e, const Anim& a, int frame,
               bool xor_first, Canvas& c, int x, int y, int mask, int from, int to)
{
    if (frame < 0 || frame >= a.frames) return false;
    const Header& h = a.frame[frame];
    const uint32_t n = h.frame_bytes();
    uint8_t* buf = new (std::nothrow) uint8_t[n];
    if (!buf) return false;
    bool ok;
    {
        dax::RleReader r(src, idx, e);
        ok = r.skip(a.data_at[frame]) == a.data_at[frame] && r.read(buf, n) == n;
    }
    if (ok && xor_first && frame > 0) {
        const uint32_t n0 = a.frame[0].frame_bytes();
        uint8_t* first = new (std::nothrow) uint8_t[n0];
        if (!first) {
            ok = false;
        } else {
            dax::RleReader r(src, idx, e);
            ok = r.skip(a.data_at[0]) == a.data_at[0] && r.read(first, n0) == n0;
            const uint32_t m = (n < n0 ? n : n0);
            for (uint32_t i = 0; ok && i + 1 < m; ++i) buf[i] ^= first[i];
            delete[] first;
        }
    }
    if (ok) draw_pixels(buf, h, c, x, y, mask, from, to);
    delete[] buf;
    return ok;
}

bool parse_vga_header(const uint8_t* p, uint32_t raw_size, VgaHeader& out)
{
    if (raw_size < kVgaHeaderSize) return false;
    out.height = p[0];
    out.width_cols = p[1];
    out.frames = p[6];
    out.first = p[8];
    out.count = static_cast<uint16_t>(p[9] + 1);
    if (out.height == 0 || out.width_cols == 0 || out.width_cols > kScreenW / 8 || out.frames == 0) return false;
    if (out.first + out.count > 256) return false;
    out.pixels_at = static_cast<uint32_t>(kVgaHeaderSize + out.count * 3 + (out.count + 1) / 2 + 4);
    const uint64_t need = out.pixels_at + static_cast<uint64_t>(out.frames) * out.frame_bytes();
    return need == raw_size;
}

bool read_vga_palette(dax::RleReader& r, const VgaHeader& h, Rgb* rgb256)
{
    for (int i = 0; i < h.count; ++i) {
        uint8_t v[3];
        if (r.read(v, 3) != 3) return false;
        // 6-bit to 8-bit: 63 -> 255
        rgb256[h.first + i] = Rgb{static_cast<uint8_t>(v[0] << 2 | v[0] >> 4), static_cast<uint8_t>(v[1] << 2 | v[1] >> 4),
                                  static_cast<uint8_t>(v[2] << 2 | v[2] >> 4)};
    }
    return true;
}

bool draw_vga(dax::RleReader& r, const VgaHeader& h, int frame, Canvas& c, int x, int y, int mask)
{
    if (frame < 0 || frame >= h.frames) return false;
    const uint32_t skip = h.pixels_at + static_cast<uint32_t>(frame) * h.frame_bytes();
    if (r.skip(skip) != skip) return false;
    const int w = h.width_px();
    for (int row = 0; row < h.height; ++row) {
        const int py = y + row;
        uint8_t* line = (py >= 0 && py < c.h) ? c.px + py * c.w : nullptr;
        for (int i = 0; i < w; ++i) {
            const int b = r.next();
            if (b < 0) return false;
            const int px = x + i;
            if (line && px >= 0 && px < c.w && b != mask) line[px] = static_cast<uint8_t>(b);
        }
    }
    return true;
}

} // namespace pic

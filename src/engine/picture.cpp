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

void Canvas::clear(uint8_t c)
{
    memset(px, bpp == 4 ? static_cast<uint8_t>((c & 0x0F) * 0x11) : c, bytes());
}

void Canvas::fill(int x, int y, int fw, int fh, uint8_t c)
{
    if (x < 0) { fw += x; x = 0; }
    if (y < 0) { fh += y; y = 0; }
    if (x + fw > w) fw = w - x;
    if (y + fh > h) fh = h - y;
    if (fw <= 0 || fh <= 0) return;
    if (bpp != 4) {
        for (int row = 0; row < fh; ++row) memset(px + (y + row) * w + x, c, fw);
        return;
    }
    const uint8_t two = static_cast<uint8_t>((c & 0x0F) * 0x11);
    for (int row = y; row < y + fh; ++row) {
        int a = x, b = x + fw;              // [a, b)
        if (a & 1) set(a++, row, c);
        if ((b & 1) && b > a) set(--b, row, c);
        if (b > a) memset(px + row * stride() + (a >> 1), two, static_cast<size_t>(b - a) >> 1);
    }
}

void Canvas::get_row(int x, int y, int n, uint8_t* out) const
{
    if (bpp != 4) {
        memcpy(out, px + y * w + x, static_cast<size_t>(n));
        return;
    }
    for (int i = 0; i < n; ++i) out[i] = get(x + i, y);
}

void Canvas::put_row(int x, int y, int n, const uint8_t* in)
{
    if (bpp != 4) {
        memcpy(px + y * w + x, in, static_cast<size_t>(n));
        return;
    }
    for (int i = 0; i < n; ++i) set(x + i, y, in[i]);
}

int sf_extra(uint8_t flags)
{
    return ((flags & 1) ? 8 : 0) + ((flags & 2) ? 16 : 0) + ((flags & 8) ? 24 : 0);
}

namespace {
uint8_t g_sf_map[16] = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};

// A Savage Frontier picture's colour map from its header's bytes past the
// flags (`part`: what of them is in hand, from the byte after the flags)
// into map, and the program's map with it; without one the program's
void sf_take_map(uint8_t flags, const uint8_t* part, uint8_t* map)
{
    if (flags & 2) memcpy(g_sf_map, part + ((flags & 1) ? 8 : 0), 16);
    memcpy(map, g_sf_map, 16);
}
} // namespace

uint8_t* sf_map() { return g_sf_map; }
void sf_map_reset()
{
    for (int i = 0; i < 8; ++i) g_sf_map[i] = static_cast<uint8_t>((2 * i) | (2 * i + 1) << 4);
    memset(g_sf_map + 8, 0, 8);
}

void sf_row(const uint8_t* planes, int cols, const uint8_t* map, uint8_t* packed)
{
    for (int col = 0; col < cols; ++col) {
        for (int k = 0; k < 8; k += 2) {
            uint8_t two = 0;
            for (int half = 0; half < 2; ++half) {
                const int bit = 7 - (k + half);
                int v = 0;
                for (int p = 0; p < 5; ++p) v |= ((planes[p * cols + col] >> bit) & 1) << p;
                const uint8_t m = static_cast<uint8_t>((map[v >> 1] >> ((v & 1) * 4)) & 0x0F);
                two = static_cast<uint8_t>(two | (half ? m : m << 4));
            }
            packed[col * 4 + k / 2] = two;
        }
    }
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
    out.sf = out.pre = 0;
    // Nothing in these games is bigger than the screen.
    if (out.height == 0 || out.height > kScreenH) return false;
    if (out.width_cols == 0 || out.width_cols > kScreenW / 8) return false;
    if (out.frames == 0) return false;
    const uint64_t need = kHeaderSize + static_cast<uint64_t>(out.frames) * out.frame_bytes();
    if (need != raw_size) {
        // Gateway's format: the flags byte (bit 2 set, no high bits) and its parts
        const uint8_t flags = p[9];
        if (!(flags & 4) || (flags & 0xF0)) return false;
        const int head = 10 + sf_extra(flags);
        if (head < static_cast<int>(kHeaderSize)) return false;
        out.sf = flags;
        if (head + static_cast<uint64_t>(out.frames) * out.stored_bytes() != raw_size) {
            out.sf = 0;
            return false;
        }
        out.pre = static_cast<uint8_t>(head - kHeaderSize);
    }
    if (extra_bytes) *extra_bytes = 0;
    return true;
}

bool begin_frames(dax::RleReader& r, const Header& h, uint8_t* map)
{
    if (!h.sf) return true;
    uint8_t part[48];
    memcpy(part, h.extra + 1, 7);                  // (the bytes after the flags in the 17)
    if (h.pre > sizeof part - 7 || r.read(part + 7, h.pre) != h.pre) return false;
    sf_take_map(h.sf, part, map);
    return true;
}

bool next_frame(dax::RleReader& r, const Header& h, const uint8_t* map, uint8_t* out)
{
    if (!h.sf) return r.read(out, h.frame_bytes()) == h.frame_bytes();
    const int cols = h.width_cols;
    uint8_t planes[5 * 4];
    uint8_t* big = cols > 4 ? new (std::nothrow) uint8_t[5u * cols] : nullptr;
    uint8_t* p = cols > 4 ? big : planes;
    if (!p) return false;
    bool ok = true;
    for (int row = 0; ok && row < h.height; ++row) {
        ok = r.read(p, 5u * cols) == 5u * cols;
        if (ok) sf_row(p, cols, map, out + row * cols * 4);
    }
    delete[] big;
    return ok;
}

bool read_frame(dax::RleReader& r, const Header& h, int frame, uint8_t* out)
{
    if (frame < 0 || frame >= h.frames) return false;
    uint8_t map[16];
    if (!begin_frames(r, h, map)) return false;
    const uint32_t skip = static_cast<uint32_t>(frame) * h.stored_bytes();
    if (r.skip(skip) != skip) return false;
    return next_frame(r, h, map, out);
}

bool draw(dax::RleReader& r, const Header& h, int frame, Canvas& c, int x, int y, int mask)
{
    if (frame < 0 || frame >= h.frames) return false;
    uint8_t map[16];
    if (!begin_frames(r, h, map)) return false;
    const uint32_t skip = static_cast<uint32_t>(frame) * h.stored_bytes();
    if (r.skip(skip) != skip) return false;
    const int row_bytes = h.width_cols * 4;
    uint8_t* planes = nullptr;
    uint8_t* packed = nullptr;
    if (h.sf) {
        planes = new (std::nothrow) uint8_t[9u * h.width_cols];
        if (!planes) return false;
        packed = planes + 5 * h.width_cols;
    }
    bool ok = true;
    for (int row = 0; ok && row < h.height; ++row) {
        const int py = y + row;
        const bool row_in = py >= 0 && py < c.h;
        if (h.sf) {
            if (r.read(planes, 5u * h.width_cols) != 5u * h.width_cols) {
                ok = false;
                break;
            }
            sf_row(planes, h.width_cols, map, packed);
        }
        for (int i = 0; i < row_bytes; ++i) {
            const int b = h.sf ? packed[i] : r.next();
            if (b < 0) {
                ok = false;
                break;
            }
            if (!row_in) continue;
            const int px = x + i * 2;
            const uint8_t hi = static_cast<uint8_t>(b >> 4), lo = static_cast<uint8_t>(b & 0x0F);
            if (px >= 0 && px < c.w && hi != mask) c.set(px, py, hi);
            if (px + 1 >= 0 && px + 1 < c.w && lo != mask) c.set(px + 1, py, lo);
        }
    }
    delete[] planes;
    return ok;
}

void draw_pixels(const uint8_t* data, const Header& h, Canvas& c, int x, int y, int mask, int from, int to)
{
    const int row_bytes = h.width_cols * 4;
    for (int row = 0; row < h.height; ++row) {
        const int py = y + row;
        if (py < 0 || py >= c.h) continue;
        const uint8_t* src = data + row * row_bytes;
        for (int i = 0; i < row_bytes; ++i) {
            const int px = x + i * 2;
            const uint8_t hi = static_cast<uint8_t>(src[i] >> 4), lo = static_cast<uint8_t>(src[i] & 0x0F);
            if (px >= 0 && px < c.w && hi != mask) c.set(px, py, hi == from ? static_cast<uint8_t>(to) : hi);
            if (px + 1 >= 0 && px + 1 < c.w && lo != mask) c.set(px + 1, py, lo == from ? static_cast<uint8_t>(to) : lo);
        }
    }
}

namespace {
// Gateway's animations: per frame u32 delay, 9 header bytes, the flags and
// their parts, the planes
bool parse_sf_anim(dax::RleReader& r, uint32_t raw_size, Anim& out)
{
    out.frames = 0;
    const int n = r.next();
    if (n <= 0 || n > kMaxAnimFrames) return false;
    uint32_t at = 1;
    for (int f = 0; f < n; ++f) {
        uint8_t hd[14];
        if (r.read(hd, sizeof hd) != sizeof hd) return false;
        at += sizeof hd;
        Header& h = out.frame[f];
        out.delay[f] = static_cast<uint32_t>(hd[0] | (hd[1] << 8) | (hd[2] << 16)) | (static_cast<uint32_t>(hd[3]) << 24);
        h.height = static_cast<uint16_t>(hd[4] | (hd[5] << 8));
        h.width_cols = static_cast<uint16_t>(hd[6] | (hd[7] << 8));
        h.x_cell = static_cast<uint16_t>(hd[8] | (hd[9] << 8));
        h.y_cell = static_cast<uint16_t>(hd[10] | (hd[11] << 8));
        h.frames = 1;
        h.sf = hd[13];
        h.pre = 0;
        if (!(h.sf & 4) || (h.sf & 0xF0) || h.height == 0 || h.height > kScreenH || h.width_cols == 0 ||
            h.width_cols > kScreenW / 8)
            return false;
        const int ext = sf_extra(h.sf);
        uint8_t part[48];
        if (at + ext > raw_size || r.read(part, ext) != static_cast<uint32_t>(ext)) return false;
        if (f == 0) sf_take_map(h.sf, part, out.map);
        at += ext;
        out.data_at[f] = at;
        const uint32_t fb = h.stored_bytes();
        if (at + fb > raw_size || r.skip(fb) != fb) return false;
        at += fb;
    }
    if (at != raw_size) return false;
    out.frames = n;
    return true;
}
} // namespace

bool parse_anim(dax::RleReader& r, uint32_t raw_size, Anim& out, bool sf)
{
    if (sf) return parse_sf_anim(r, raw_size, out);
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
    if (h.sf) {
        // Gateway's: row by row - the frame's planes (past the first: XORed
        // with the first frame's, read alongside) through the first frame's map
        const int cols = h.width_cols;
        const bool x0 = xor_first && frame > 0 && a.frame[0].width_cols == cols;
        dax::RleReader r(src, idx, e), r0(src, idx, e);
        if (r.skip(a.data_at[frame]) != a.data_at[frame]) return false;
        if (x0 && r0.skip(a.data_at[0]) != a.data_at[0]) return false;
        const uint32_t n = 5u * cols;
        uint8_t* planes = new (std::nothrow) uint8_t[2 * n + 4u * cols];
        if (!planes) return false;
        uint8_t* first = planes + n;
        uint8_t* packed = first + n;
        Header one = h;
        one.height = 1;
        bool ok = true;
        for (int row = 0; ok && row < h.height; ++row) {
            ok = r.read(planes, n) == n;
            if (ok && x0 && row < a.frame[0].height) {
                ok = r0.read(first, n) == n;
                for (uint32_t i = 0; ok && i < n; ++i) planes[i] ^= first[i];
            }
            if (!ok) break;
            sf_row(planes, cols, a.map, packed);
            draw_pixels(packed, one, c, x, y + row, mask, from, to);
        }
        delete[] planes;
        return ok;
    }
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
    out.frames = static_cast<uint16_t>(p[6] | p[7] << 8);
    out.first = p[8];
    out.count = static_cast<uint16_t>(p[9] + 1);
    if (out.height == 0 || out.width_cols == 0 || out.width_cols > kScreenW / 8 || out.frames == 0) return false;
    if (out.first + out.count > 256) return false;
    // (4 bytes after the nibbles - 32 when the entries are 0x20 and 0x70 on:
    // Darkness's program skips that many there - its 8X8D1 wall tiles,
    // darkness_facts.md 7.3)
    const uint32_t gap = out.first == 0x20 && out.count == 0x70 ? 32 : 4;
    out.pixels_at = static_cast<uint32_t>(kVgaHeaderSize + out.count * 3 + (out.count + 1) / 2 + gap);
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

bool read_vga_ega(dax::RleReader& r, const VgaHeader& h, uint8_t* ega256)
{
    for (int i = 0; i < h.count; i += 2) {
        const int b = r.next();
        if (b < 0) return false;
        ega256[h.first + i] = static_cast<uint8_t>(b >> 4);
        if (i + 1 < h.count) ega256[h.first + i + 1] = static_cast<uint8_t>(b & 0x0F);
    }
    return true;
}

bool draw_vga_block(dax::ByteSource& src, const dax::Index& idx, const dax::Entry& e, int frame, Canvas& c, int x,
                    int y, int mask, Rgb* rgb256)
{
    uint8_t hdr[kVgaHeaderSize];
    VgaHeader h;
    uint8_t map[256];
    {
        dax::RleReader r(src, idx, e);
        if (r.read(hdr, sizeof hdr) != sizeof hdr || !parse_vga_header(hdr, e.raw_size, h)) return false;
        if (rgb256 ? !read_vga_palette(r, h, rgb256) : r.skip(h.count * 3u) != h.count * 3u) return false;
        for (int i = 0; i < 256; ++i) map[i] = static_cast<uint8_t>(i & 0x0F);
        if (!read_vga_ega(r, h, map)) return false;
    }
    dax::RleReader r(src, idx, e);
    return draw_vga(r, h, frame, c, x, y, mask, c.bpp == 4 ? map : nullptr);
}

bool draw_vga(dax::RleReader& r, const VgaHeader& h, int frame, Canvas& c, int x, int y, int mask, const uint8_t* map)
{
    if (frame < 0 || frame >= h.frames) return false;
    const uint32_t skip = h.pixels_at + static_cast<uint32_t>(frame) * h.frame_bytes();
    if (r.skip(skip) != skip) return false;
    const int w = h.width_px();
    for (int row = 0; row < h.height; ++row) {
        const int py = y + row;
        const bool line = py >= 0 && py < c.h;
        for (int i = 0; i < w; ++i) {
            const int b = r.next();
            if (b < 0) return false;
            const int px = x + i;
            if (line && px >= 0 && px < c.w && b != mask) c.set(px, py, map ? map[b] : static_cast<uint8_t>(b));
        }
    }
    return true;
}

bool parse_dk_anim(dax::RleReader& r, uint32_t raw_size, DkAnim& a)
{
    a = DkAnim{};
    uint8_t h[11];
    if (r.read(h, sizeof h) != sizeof h) return false;
    a.height = static_cast<uint16_t>(h[0] | h[1] << 8);
    a.width_cols = static_cast<uint16_t>(h[2] | h[3] << 8);
    a.x = static_cast<uint16_t>(h[4] | h[5] << 8);
    a.y = static_cast<uint16_t>(h[6] | h[7] << 8);
    a.frames = static_cast<uint8_t>(h[8] + 1);
    a.first = h[9];
    a.count = h[10];
    if (!a.height || !a.width_cols || a.width_cols > kScreenW / 8 || a.frames > kDkMaxFrames || !a.count ||
        a.first + a.count > 256)
        return false;
    if (r.skip(a.count * 3u) != a.count * 3u) return false;
    for (int i = 0; i < 256; ++i) a.ega[i] = static_cast<uint8_t>(i & 15);
    for (int i = 0; i < a.count; i += 2) {
        const int b = r.next();
        if (b < 0) return false;
        a.ega[a.first + i] = static_cast<uint8_t>(b >> 4);
        if (i + 1 < a.count) a.ega[a.first + i + 1] = static_cast<uint8_t>(b & 15);
    }
    uint8_t t[6];
    if (r.read(t, 6) != 6) return false;
    a.key = t[4];
    a.loop = t[5];
    uint32_t total = 0;
    for (int k = 0; k < a.frames; ++k) {
        uint8_t f[3];
        if (r.read(f, 3) != 3) return false;
        a.size[k] = static_cast<uint16_t>(f[0] | f[1] << 8);
        a.delay[k] = f[2];
        total += a.size[k];
    }
    a.data_at = r.produced();
    if (a.key >= a.frames) a.key = 0;
    if (a.loop >= a.frames) a.loop = 0;
    return a.data_at + total == raw_size;
}

namespace {
// One frame's packing on top of the block's own (257 - c runs)
struct FrameUnpack {
    dax::RleReader& r;
    int run = 0, rep = -1;
    int next()
    {
        while (run == 0) {
            const int c = r.next();
            if (c < 0) return -1;
            if (c <= 0x7F) {
                run = c + 1;
                rep = -1;
            } else {
                run = 257 - c;
                rep = r.next();
                if (rep < 0) return -1;
            }
        }
        --run;
        return rep >= 0 ? rep : r.next();
    }
};
} // namespace

bool draw_dk_anim(dax::ByteSource& src, const dax::Index& idx, const dax::Entry& e, const DkAnim& a, int frame,
                  Canvas& c, int x, int y, bool sprite, int mask)
{
    if (frame < 0 || frame >= a.frames) return false;
    auto at = [&](int k) {
        uint32_t o = a.data_at;
        for (int i = 0; i < k; ++i) o += a.size[i];
        return o;
    };
    dax::RleReader rk(src, idx, e);
    if (rk.skip(at(a.key)) != at(a.key)) return false;
    FrameUnpack key{rk};
    dax::RleReader rf(src, idx, e);
    const bool xored = frame != a.key;
    if (xored && rf.skip(at(frame)) != at(frame)) return false;
    FrameUnpack fr{rf};
    const int w = a.width_px();
    for (int row = 0; row < a.height; ++row) {
        const int py = y + row;
        for (int i = 0; i < w; ++i) {
            int v = key.next();
            if (v < 0) return false;
            if (xored) {
                const int f = fr.next();
                if (f < 0) return false;
                v ^= f;
            }
            if (sprite) {
                if (v == 0) v = 13;
                else if (v == 13) v = 0;
                if (v == 13) continue;
            }
            if (v == mask) continue;
            const int px = x + i;
            if (py >= 0 && py < c.h && px >= 0 && px < c.w) c.set(px, py, c.bpp == 4 ? a.ega[v] : static_cast<uint8_t>(v));
        }
    }
    return true;
}

} // namespace pic

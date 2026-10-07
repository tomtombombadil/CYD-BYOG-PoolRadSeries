#include "icon.h"

#include <cstring>
#include <new>

#include "png.h"

namespace icon {

namespace {

bool rd(dax::ByteSource& s, uint32_t pos, void* buf, size_t n)
{
    return s.read_at(pos, static_cast<uint8_t*>(buf), n) == n;
}
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

bool read_u16(dax::ByteSource& s, uint32_t pos, uint16_t& v)
{
    uint8_t b[2];
    if (!rd(s, pos, b, 2)) return false;
    v = u16(b);
    return true;
}
bool read_u32(dax::ByteSource& s, uint32_t pos, uint32_t& v)
{
    uint8_t b[4];
    if (!rd(s, pos, b, 4)) return false;
    v = u32(b);
    return true;
}

// A candidate image: a DIB this can draw, or a PNG. Fills w / h / bits.
bool check_image(dax::ByteSource& s, uint32_t off, uint32_t size, Found& f)
{
    uint8_t h[40];
    if (size < sizeof h || off + size > s.size() || !rd(s, off, h, sizeof h)) return false;
    if (h[0] == 0x89 && h[1] == 'P') {
        png::Info pi;
        if (!png::probe(s, off, size, pi)) return false;
        f.offset = off;
        f.size = size;
        f.w = pi.w;
        f.h = pi.h;
        f.bits = 32;
        f.png = true;
        return true;
    }
    if (u32(h) != 40) return false;
    const int32_t w = static_cast<int32_t>(u32(h + 4));
    const int32_t hh = static_cast<int32_t>(u32(h + 8)) / 2;
    const int bits = u16(h + 14);
    if (u32(h + 16) != 0) return false;                 // compressed
    if (w < 1 || w > kMaxDib || hh < 1 || hh > kMaxDib) return false;
    if (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32) return false;
    f.offset = off;
    f.size = size;
    f.w = w;
    f.h = hh;
    f.bits = bits;
    f.png = false;
    return true;
}

// Is a better than b? Bigger first, then more colours, then a DIB (cheaper)
bool better(const Found& a, const Found& b)
{
    if (b.w == 0) return true;
    if (a.w * a.h != b.w * b.h) return a.w * a.h > b.w * b.h;
    if (a.bits != b.bits) return a.bits > b.bits;
    return !a.png && b.png;
}

// ---- PE resources ----------------------------------------------------------

struct Pe {
    dax::ByteSource* s = nullptr;
    uint32_t root = 0;              // resource directory, file offset
    uint32_t sec_at = 0;
    uint16_t nsec = 0;
};

bool rva_to_off(const Pe& pe, uint32_t rva, uint32_t& off)
{
    for (int i = 0; i < pe.nsec; ++i) {
        uint8_t sh[40];
        if (!rd(*pe.s, pe.sec_at + i * 40u, sh, sizeof sh)) return false;
        const uint32_t vsize = u32(sh + 8), va = u32(sh + 12), raw = u32(sh + 16), ptr = u32(sh + 20);
        const uint32_t span = vsize > raw ? vsize : raw;
        if (rva >= va && rva < va + span) {
            off = ptr + (rva - va);
            return true;
        }
    }
    return false;
}

bool open_pe(dax::ByteSource& s, Pe& pe)
{
    uint8_t mz[2];
    uint32_t at;
    if (!rd(s, 0, mz, 2) || mz[0] != 'M' || mz[1] != 'Z' || !read_u32(s, 0x3C, at)) return false;
    uint8_t sig[4];
    if (!rd(s, at, sig, 4) || memcmp(sig, "PE\0\0", 4) != 0) return false;
    const uint32_t coff = at + 4;
    uint16_t opt_size, magic;
    if (!read_u16(s, coff + 2, pe.nsec) || !read_u16(s, coff + 16, opt_size)) return false;
    const uint32_t opt = coff + 20;
    if (!read_u16(s, opt, magic)) return false;
    const uint32_t dd = opt + (magic == 0x20B ? 112 : 96);
    uint32_t res_rva;
    if (!read_u32(s, dd + 2 * 8, res_rva) || res_rva == 0) return false;
    pe.s = &s;
    pe.sec_at = opt + opt_size;
    if (pe.nsec > 96) return false;
    return rva_to_off(pe, res_rva, pe.root);
}

// The entry for `id` (or the first entry if id < 0) in the directory at
// dir; *data = its OffsetToData
bool dir_find(const Pe& pe, uint32_t dir, int id, uint32_t& data)
{
    uint16_t named, ids;
    if (!read_u16(*pe.s, dir + 12, named) || !read_u16(*pe.s, dir + 14, ids)) return false;
    const int n = named + ids;
    if (n > 4096) return false;
    for (int k = 0; k < n; ++k) {
        uint8_t e[8];
        if (!rd(*pe.s, dir + 16 + k * 8u, e, 8)) return false;
        const uint32_t name = u32(e);
        if (id < 0 || (!(name & 0x80000000u) && name == static_cast<uint32_t>(id))) {
            data = u32(e + 4);
            return true;
        }
    }
    return false;
}

// Resource type / name (name < 0: the first) / first language
bool resource(const Pe& pe, int type, int name, uint32_t& off, uint32_t& size)
{
    uint32_t d1, d2, d3;
    if (!dir_find(pe, pe.root, type, d1) || !(d1 & 0x80000000u)) return false;
    if (!dir_find(pe, pe.root + (d1 & 0x7FFFFFFFu), name, d2) || !(d2 & 0x80000000u)) return false;
    if (!dir_find(pe, pe.root + (d2 & 0x7FFFFFFFu), -1, d3) || (d3 & 0x80000000u)) return false;
    uint8_t de[8];
    if (!rd(*pe.s, pe.root + d3, de, 8)) return false;
    size = u32(de + 4);
    return rva_to_off(pe, u32(de), off);
}

constexpr int kRtIcon = 3, kRtGroupIcon = 14;

} // namespace

bool find_in_ico(dax::ByteSource& s, Found& out)
{
    out = Found{};
    uint8_t h[6];
    if (!rd(s, 0, h, 6) || u16(h) != 0 || u16(h + 2) != 1) return false;
    const int n = u16(h + 4);
    if (n == 0 || n > 64) return false;
    for (int i = 0; i < n; ++i) {
        uint8_t e[16];
        if (!rd(s, 6 + i * 16u, e, 16)) return false;
        Found f;
        if (check_image(s, u32(e + 12), u32(e + 8), f) && better(f, out)) out = f;
    }
    return out.w > 0;
}

bool find_in_pe(dax::ByteSource& s, Found& out)
{
    out = Found{};
    Pe pe;
    if (!open_pe(s, pe)) return false;
    uint32_t goff, gsize;
    if (!resource(pe, kRtGroupIcon, -1, goff, gsize) || gsize < 6) return false;
    uint8_t h[6];
    if (!rd(s, goff, h, 6) || u16(h + 2) != 1) return false;
    const int n = u16(h + 4);
    if (n == 0 || n > 64 || 6u + n * 14u > gsize) return false;
    for (int i = 0; i < n; ++i) {
        uint8_t e[14];
        if (!rd(s, goff + 6 + i * 14u, e, 14)) return false;
        uint32_t off, size;
        Found f;
        if (resource(pe, kRtIcon, u16(e + 12), off, size) && check_image(s, off, size, f) && better(f, out))
            out = f;
    }
    return out.w > 0;
}

bool find(dax::ByteSource& s, Found& out)
{
    uint8_t h[4];
    if (rd(s, 0, h, 4) && h[0] == 0 && h[1] == 0 && h[2] == 1 && h[3] == 0) return find_in_ico(s, out);
    return find_in_pe(s, out);
}

namespace {

// Turns source rows (top to bottom) into out_w x out_h rows: averages when
// shrinking, repeats when growing
class Resampler {
public:
    Resampler(int in_w, int in_h, int out_w, int out_h, RowFn fn, void* ctx)
        : iw_(in_w), ih_(in_h), ow_(out_w), oh_(out_h), fn_(fn), ctx_(ctx) {}
    bool begin()
    {
        sum_ = new (std::nothrow) uint32_t[ow_ * 5];
        out_ = new (std::nothrow) uint8_t[ow_ * 4];
        if (!sum_ || !out_) return false;
        clear();
        return true;
    }
    ~Resampler()
    {
        delete[] sum_;
        delete[] out_;
    }
    void row(int sy, const uint8_t* rgba)
    {
        if (oh_ <= ih_) {
            const int oy = sy * oh_ / ih_;
            if (oy != cur_ && cur_ >= 0) flush();
            cur_ = oy;
            add(rgba);
            if (sy == ih_ - 1) flush();
        } else {
            // every output row whose source is this one
            for (int oy = (sy * oh_ + ih_ - 1) / ih_; oy < oh_ && oy * ih_ / oh_ == sy; ++oy) {
                add(rgba);
                cur_ = oy;
                flush();
            }
        }
    }

private:
    void clear() { memset(sum_, 0, sizeof(uint32_t) * ow_ * 5); }
    void add(const uint8_t* rgba)
    {
        if (ow_ <= iw_) {
            for (int x = 0; x < iw_; ++x) acc(x * ow_ / iw_, rgba + x * 4);
        } else {
            for (int ox = 0; ox < ow_; ++ox) acc(ox, rgba + (ox * iw_ / ow_) * 4);
        }
    }
    void acc(int ox, const uint8_t* p)
    {
        uint32_t* s = sum_ + ox * 5;
        // colours weighted by alpha, so see-through pixels don't darken edges
        s[0] += p[0] * p[3];
        s[1] += p[1] * p[3];
        s[2] += p[2] * p[3];
        s[3] += p[3];
        s[4] += 1;
    }
    void flush()
    {
        for (int ox = 0; ox < ow_; ++ox) {
            const uint32_t* s = sum_ + ox * 5;
            uint8_t* o = out_ + ox * 4;
            if (s[3] == 0 || s[4] == 0) {
                o[0] = o[1] = o[2] = o[3] = 0;
            } else {
                o[0] = static_cast<uint8_t>(s[0] / s[3]);
                o[1] = static_cast<uint8_t>(s[1] / s[3]);
                o[2] = static_cast<uint8_t>(s[2] / s[3]);
                o[3] = static_cast<uint8_t>(s[3] / s[4]);
            }
        }
        fn_(cur_, out_, ow_, ctx_);
        clear();
    }

    int iw_, ih_, ow_, oh_;
    RowFn fn_;
    void* ctx_;
    uint32_t* sum_ = nullptr;
    uint8_t*  out_ = nullptr;
    int cur_ = -1;
};

bool png_row(int y, const uint8_t* rgba, void* ctx)
{
    static_cast<Resampler*>(ctx)->row(y, rgba);
    return true;
}

// The DIB's rows, top to bottom, into r
bool dib_rows(dax::ByteSource& s, const Found& f, Resampler& r)
{
    uint8_t h[40];
    if (!rd(s, f.offset, h, sizeof h)) return false;
    const int bits = f.bits;
    uint32_t colours = u32(h + 32);
    if (bits <= 8 && (colours == 0 || colours > (1u << bits))) colours = 1u << bits;
    if (bits > 8) colours = 0;
    uint8_t pal[256 * 4];
    if (colours && !rd(s, f.offset + 40, pal, colours * 4)) return false;
    const uint32_t stride = ((f.w * bits + 31) / 32) * 4;
    const uint32_t mstride = ((f.w + 31) / 32) * 4;
    const uint32_t pix = f.offset + 40 + colours * 4;
    const uint32_t mask = pix + stride * f.h;
    if (mask + mstride * f.h > f.offset + f.size) return false;

    uint8_t* row = new (std::nothrow) uint8_t[stride + mstride + f.w * 4];
    if (!row) return false;
    uint8_t* mrow = row + stride;
    uint8_t* out = mrow + mstride;
    // 32-bit images with no alpha at all use the mask (older icons)
    bool any_alpha = false;
    if (bits == 32) {
        for (int y = 0; y < f.h && !any_alpha; ++y) {
            if (!rd(s, pix + y * stride, row, stride)) { delete[] row; return false; }
            for (int x = 0; x < f.w; ++x)
                if (row[x * 4 + 3]) { any_alpha = true; break; }
        }
    }
    bool ok = true;
    for (int y = 0; y < f.h && ok; ++y) {
        const int src_y = f.h - 1 - y;       // bottom-up
        if (!rd(s, pix + src_y * stride, row, stride) || !rd(s, mask + src_y * mstride, mrow, mstride)) {
            ok = false;
            break;
        }
        uint8_t* o = out;
        for (int x = 0; x < f.w; ++x, o += 4) {
            const bool clear = (mrow[x >> 3] >> (7 - (x & 7))) & 1;
            uint8_t b, g, rr, a = clear ? 0 : 255;
            if (bits == 32) {
                b = row[x * 4]; g = row[x * 4 + 1]; rr = row[x * 4 + 2];
                if (any_alpha) a = row[x * 4 + 3];
            } else if (bits == 24) {
                b = row[x * 3]; g = row[x * 3 + 1]; rr = row[x * 3 + 2];
            } else {
                int idx;
                if (bits == 8) idx = row[x];
                else if (bits == 4) idx = (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
                else idx = (row[x >> 3] >> (7 - (x & 7))) & 1;
                if (static_cast<uint32_t>(idx) >= colours) idx = 0;
                b = pal[idx * 4]; g = pal[idx * 4 + 1]; rr = pal[idx * 4 + 2];
            }
            o[0] = rr; o[1] = g; o[2] = b; o[3] = a;
        }
        r.row(y, out);
    }
    delete[] row;
    return ok;
}

} // namespace

bool render(dax::ByteSource& s, const Found& f, int out_w, int out_h, uint8_t* window, RowFn row, void* ctx)
{
    if (f.w < 1 || f.h < 1 || out_w < 1 || out_h < 1 || out_w > kMaxOut || out_h > kMaxOut) return false;
    Resampler r(f.w, f.h, out_w, out_h, row, ctx);
    if (!r.begin()) return false;
    if (f.png) return window && png::decode(s, f.offset, f.size, window, png_row, &r);
    return dib_rows(s, f, r);
}

} // namespace icon

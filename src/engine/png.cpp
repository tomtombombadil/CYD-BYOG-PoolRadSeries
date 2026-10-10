#include "png.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "inflate.h"

namespace png {

namespace {

const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};

uint32_t be32(const uint8_t* p) { return static_cast<uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int channels(int colour)
{
    switch (colour) {
    case 0: return 1;
    case 2: return 3;
    case 3: return 1;
    case 4: return 2;
    case 6: return 4;
    }
    return 0;
}

// The IDAT chunks' data, one byte at a time
class Idat : public inflate::Input {
public:
    Idat(dax::ByteSource& s, uint32_t first_chunk, uint32_t end) : s_(s), next_(first_chunk), end_(end) {}
    int byte() override
    {
        while (left_ == 0) {
            if (!next_chunk()) return -1;
        }
        if (buf_pos_ >= buf_len_) {
            uint32_t want = left_ < sizeof buf_ ? left_ : static_cast<uint32_t>(sizeof buf_);
            if (s_.read_at(pos_, buf_, want) != want) return -1;
            buf_pos_ = 0;
            buf_len_ = want;
        }
        --left_;
        ++pos_;
        return buf_[buf_pos_++];
    }
private:
    bool next_chunk()
    {
        if (done_ || next_ + 12 > end_) return false;
        uint8_t h[8];
        if (s_.read_at(next_, h, 8) != 8) return false;
        const uint32_t len = be32(h);
        if (memcmp(h + 4, "IDAT", 4) != 0) {
            done_ = true;              // IDATs are consecutive
            return false;
        }
        if (len > end_ - next_ - 12) return false;        // (no wrap on a damaged length)
        pos_ = next_ + 8;
        left_ = len;
        buf_pos_ = buf_len_ = 0;
        next_ = pos_ + len + 4;        // past the CRC
        return true;
    }

    dax::ByteSource& s_;
    uint32_t next_, end_;
    uint32_t pos_ = 0, left_ = 0;
    uint8_t  buf_[256];
    uint32_t buf_pos_ = 0, buf_len_ = 0;
    bool     done_ = false;
};

int paeth(int a, int b, int c)
{
    const int p = a + b - c;
    const int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
}

// Unfilters rows as the bytes arrive and hands them on as RGBA
class Rows : public inflate::Output {
public:
    Rows(const Info& in, const uint8_t* pal, const uint8_t* trns, int ntrns, RowFn fn, void* ctx)
        : in_(in), pal_(pal), trns_(trns), ntrns_(ntrns), fn_(fn), ctx_(ctx)
    {
        bpp_ = channels(in.colour);
        stride_ = static_cast<size_t>(in.w) * bpp_;
        mem_ = new (std::nothrow) uint8_t[stride_ * 2 + 1 + static_cast<size_t>(in.w) * 4];
        if (mem_) {
            cur_ = mem_;                       // filter byte + stride
            prev_ = mem_ + stride_ + 1;
            rgba_ = prev_ + stride_;
            memset(prev_, 0, stride_);
        }
    }
    ~Rows() override { delete[] mem_; }
    bool ok() const { return mem_ != nullptr; }
    bool complete() const { return y_ >= in_.h; }

    bool put(uint8_t b) override
    {
        if (y_ >= in_.h) return false;         // more data than rows
        cur_[fill_++] = b;
        if (fill_ < stride_ + 1) return true;
        fill_ = 0;
        if (!unfilter()) return false;
        to_rgba();
        const bool go_on = fn_(y_, rgba_, ctx_);
        memcpy(prev_, cur_ + 1, stride_);
        ++y_;
        return go_on && y_ < in_.h;
    }

private:
    bool unfilter()
    {
        uint8_t* x = cur_ + 1;
        const int f = cur_[0];
        for (size_t i = 0; i < stride_; ++i) {
            const int a = i >= static_cast<size_t>(bpp_) ? x[i - bpp_] : 0;
            const int b = prev_[i];
            const int c = i >= static_cast<size_t>(bpp_) ? prev_[i - bpp_] : 0;
            int v = x[i];
            switch (f) {
            case 0: break;
            case 1: v += a; break;
            case 2: v += b; break;
            case 3: v += (a + b) / 2; break;
            case 4: v += paeth(a, b, c); break;
            default: return false;
            }
            x[i] = static_cast<uint8_t>(v);
        }
        return true;
    }
    void to_rgba()
    {
        const uint8_t* x = cur_ + 1;
        uint8_t* o = rgba_;
        for (int i = 0; i < in_.w; ++i, o += 4) {
            switch (in_.colour) {
            case 0: o[0] = o[1] = o[2] = x[i]; o[3] = 255; break;
            case 4: o[0] = o[1] = o[2] = x[i * 2]; o[3] = x[i * 2 + 1]; break;
            case 2: o[0] = x[i * 3]; o[1] = x[i * 3 + 1]; o[2] = x[i * 3 + 2]; o[3] = 255; break;
            case 6: memcpy(o, x + i * 4, 4); break;
            case 3: {
                const int k = x[i];
                o[0] = pal_[k * 3]; o[1] = pal_[k * 3 + 1]; o[2] = pal_[k * 3 + 2];
                o[3] = k < ntrns_ ? trns_[k] : 255;
                break;
            }
            }
        }
    }

    Info in_;
    const uint8_t* pal_;
    const uint8_t* trns_;
    int ntrns_;
    RowFn fn_;
    void* ctx_;
    int bpp_ = 0;
    size_t stride_ = 0, fill_ = 0;
    uint8_t* mem_ = nullptr;
    uint8_t* cur_ = nullptr;
    uint8_t* prev_ = nullptr;
    uint8_t* rgba_ = nullptr;
    int y_ = 0;
};

} // namespace

bool probe(dax::ByteSource& src, uint32_t at, uint32_t size, Info& info)
{
    info = Info{};
    uint8_t h[33];
    if (size < sizeof h || src.read_at(at, h, sizeof h) != sizeof h) return false;
    if (memcmp(h, kSig, 8) != 0 || be32(h + 8) != 13 || memcmp(h + 12, "IHDR", 4) != 0) return false;
    info.w = static_cast<int>(be32(h + 16));
    info.h = static_cast<int>(be32(h + 20));
    info.bit_depth = h[24];
    info.colour = h[25];
    info.interlace = h[28];
    if (h[26] != 0 || h[27] != 0) return false;            // compression / filter method
    if (info.w < 1 || info.w > kMaxSize || info.h < 1 || info.h > kMaxSize) return false;
    if (info.bit_depth != 8 || channels(info.colour) == 0 || info.interlace != 0) return false;
    return true;
}

bool decode(dax::ByteSource& src, uint32_t at, uint32_t size, uint8_t* window, RowFn row, void* ctx)
{
    Info info;
    if (!probe(src, at, size, info)) return false;
    const uint32_t end = at + size;

    // PLTE / tRNS come before the first IDAT
    static uint8_t pal[256 * 3], trns[256];
    int ntrns = 0;
    memset(pal, 0, sizeof pal);
    uint32_t p = at + 8, first_idat = 0;
    while (p + 12 <= end) {
        uint8_t h[8];
        if (src.read_at(p, h, 8) != 8) return false;
        const uint32_t len = be32(h);
        if (len > end - p - 12) return false;             // (no wrap on a damaged length)
        if (memcmp(h + 4, "IDAT", 4) == 0) {
            first_idat = p;
            break;
        }
        if (memcmp(h + 4, "PLTE", 4) == 0 && len <= sizeof pal && src.read_at(p + 8, pal, len) != len) return false;
        if (memcmp(h + 4, "tRNS", 4) == 0 && info.colour == 3 && len <= sizeof trns) {
            if (src.read_at(p + 8, trns, len) != len) return false;
            ntrns = static_cast<int>(len);
        }
        if (memcmp(h + 4, "IEND", 4) == 0) return false;
        p += 12 + len;
    }
    if (!first_idat) return false;

    Idat in(src, first_idat, end);
    const int cmf = in.byte(), flg = in.byte();             // zlib header
    if (cmf < 0 || flg < 0 || (cmf & 15) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) return false;
    Rows out(info, pal, trns, ntrns, row, ctx);
    if (!out.ok()) return false;
    inflate::raw(in, out, window);      // stops early when the last row is out
    return out.complete();
}

} // namespace png

#include "hlib.h"

#include <cstring>

namespace hlib {

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

bool read_exact(ByteSource& src, uint32_t pos, uint8_t* buf, size_t n) { return src.read_at(pos, buf, n) == n; }

} // namespace

const char* status_text(Status s)
{
    switch (s) {
    case Status::Ok:         return "OK";
    case Status::ReadError:  return "Read error";
    case Status::NotHlib:    return "Not an HLIB library";
    case Status::BadOffsets: return "Entry offsets outside the library";
    }
    return "?";
}

Status open(ByteSource& src, uint32_t base, uint32_t room, Library& out)
{
    out = Library{};
    uint8_t h[kHeaderSize];
    if (room < kHeaderSize + 4) return Status::NotHlib;
    if (!read_exact(src, base, h, sizeof h)) return Status::ReadError;
    if (memcmp(h, "HLIB", 4) != 0) return Status::NotHlib;
    const uint32_t size = le32(h + 4);
    const uint16_t count = le16(h + 8);
    if (size > room || size < kHeaderSize + 4u * (count + 1u)) return Status::BadOffsets;
    out.base = base;
    out.size = size;
    out.count = count;
    out.directory = h[11] == 1 && count > 0;
    memcpy(out.kind, h + 12, 4);
    out.kind[4] = 0;
    for (char& c : out.kind)
        if (c && (c < 32 || c > 126)) c = '?';
    return Status::Ok;
}

bool entry(ByteSource& src, const Library& lib, int i, uint32_t& pos, uint32_t& len)
{
    if (i < 0 || i >= lib.count) return false;
    uint8_t b[8];
    if (!read_exact(src, lib.base + kHeaderSize + 4u * i, b, 8)) return false;
    const uint32_t a = le32(b), e = le32(b + 4);
    if (a > e || e > lib.size || a < kHeaderSize + 4u * (lib.count + 1u)) return false;
    pos = lib.base + a;
    len = e - a;
    return true;
}

bool is_library(ByteSource& src, uint32_t pos, uint32_t len)
{
    uint8_t m[4];
    return len >= kHeaderSize + 4 && read_exact(src, pos, m, 4) && memcmp(m, "HLIB", 4) == 0;
}

int dir_count(ByteSource& src, const Library& lib)
{
    uint32_t pos, len;
    if (!lib.directory || !entry(src, lib, 0, pos, len) || len < 2) return 0;
    uint8_t b[2];
    if (!read_exact(src, pos, b, 2)) return 0;
    const uint32_t n = le16(b);
    return 2 + n * 4 <= len ? static_cast<int>(n) : static_cast<int>((len - 2) / 4);
}

bool dir_item(ByteSource& src, const Library& lib, int k, uint16_t& id, uint16_t& index)
{
    uint32_t pos, len;
    if (k < 0 || k >= dir_count(src, lib) || !entry(src, lib, 0, pos, len)) return false;
    uint8_t b[4];
    if (!read_exact(src, pos + 2 + 4u * k, b, 4)) return false;
    id = le16(b);
    index = le16(b + 2);
    return true;
}

int find(ByteSource& src, const Library& lib, uint16_t id)
{
    if (!lib.directory) return id < lib.count ? id : -1;
    uint32_t pos, len;
    if (!entry(src, lib, 0, pos, len) || len < 2) return -1;
    const int n = dir_count(src, lib);
    // (read in small batches: the directory is a few hundred bytes at most)
    uint8_t b[64];
    for (int k = 0; k < n; k += 16) {
        const int m = n - k < 16 ? n - k : 16;
        if (!read_exact(src, pos + 2 + 4u * k, b, 4u * m)) return -1;
        for (int j = 0; j < m; ++j)
            if (le16(b + 4 * j) == id) {
                const int index = le16(b + 4 * j + 2);
                return index > 0 && index < lib.count ? index : -1;
            }
    }
    return -1;
}

int id_of(ByteSource& src, const Library& lib, int i)
{
    if (!lib.directory) return i;
    if (i == 0) return -1;
    const int n = dir_count(src, lib);
    for (int k = 0; k < n; ++k) {
        uint16_t id, index;
        if (dir_item(src, lib, k, id, index) && index == i) return id;
    }
    return i;
}

// ---- Pictures --------------------------------------------------------------

bool Part::ega() const
{
    return kind == kEgaPlanar || kind == kEgaMasked || kind == kEgaSprite || kind == kEgaColours || kind == kEgaRle;
}

bool Part::drawable() const
{
    switch (kind) {
    case kVgaPlanar: case kVgaMasked: case kVgaRle: case kVgaKeyed: case kVgaSprite:
    case kEgaPlanar: case kEgaMasked: case kEgaRle:
        return height > 0 && units > 0 && width() <= kMaxWidth;
    default:
        return false;
    }
}

bool read_part(ByteSource& src, uint32_t pos, uint32_t len, Part& out)
{
    out = Part{};
    uint8_t h[kPartHeader];
    if (len < kPartHeader || !read_exact(src, pos, h, sizeof h)) return false;
    out.pos = pos;
    out.len = len;
    out.height = le16(h);
    out.x = static_cast<int16_t>(le16(h + 2));
    out.y = static_cast<int16_t>(le16(h + 4));
    out.units = h[6];
    out.kind = h[7];
    return true;
}

bool read_colours(ByteSource& src, uint32_t pos, uint32_t len, Colours& out)
{
    out = Colours{};
    uint8_t h[8];
    if (len < 8 || !read_exact(src, pos, h, 8) || h[1] != 0) return false;
    if (h[7] != kVgaColours && h[7] != kEgaColours) return false;
    out.first = le16(h + 2);
    out.count = le16(h + 4);
    out.cycles = h[6];
    out.vga = h[7] == kVgaColours;
    out.rgb_pos = pos + 8;
    if (out.vga) {
        const uint32_t rgb = 3u * out.count, cyc = 4u * out.cycles, nib = (out.count + 1u) / 2;
        if (out.first + out.count > 256 || 8 + rgb + cyc + nib > len) return false;
        out.ega_pos = pos + 8 + rgb + cyc;
    } else if (len < 8 + 48) {
        return false;
    }
    return true;
}

bool colour_rgb(ByteSource& src, const Colours& c, int i, uint8_t* rgb)
{
    if (i < 0 || i >= c.rgb_count()) return false;
    return read_exact(src, c.rgb_pos + 3u * i, rgb, 3);
}

bool ega_table(ByteSource& src, const Colours& c, uint8_t* ega, int cap)
{
    if (!c.vga || cap < c.count) return false;
    uint8_t b[32];
    for (int i = 0; i < c.count; i += 64) {
        const int m = c.count - i < 64 ? c.count - i : 64;
        if (!read_exact(src, c.ega_pos + i / 2, b, (m + 1) / 2)) return false;
        for (int j = 0; j < m; ++j) ega[i + j] = (j & 1) ? b[j / 2] & 15 : b[j / 2] >> 4;
    }
    return true;
}

int Pixels::Cursor::next()
{
    if (at == n) {
        if (pos >= end) return -1;
        const uint32_t want = end - pos < sizeof buf ? end - pos : sizeof buf;
        const size_t got = src->read_at(pos, buf, want);
        if (got == 0) { bad = true; return -1; }
        pos += static_cast<uint32_t>(got);
        n = static_cast<uint8_t>(got);
        at = 0;
    }
    return buf[at++];
}

Pixels::Pixels(ByteSource& src, const Part& p) : src_(src), p_(p)
{
    if (!p.drawable()) return;
    w_ = p.width();
    const uint32_t data = p.pos + kPartHeader, end = p.pos + p.len;
    const uint32_t plane = static_cast<uint32_t>(p.height) * p.units;    // bytes a plane
    uint32_t need = 0;
    switch (p.kind) {
    case kVgaPlanar: case kVgaKeyed: need = plane * 4; break;
    case kVgaMasked: need = plane * 8; break;
    case kEgaPlanar: need = plane * 4; break;
    case kEgaMasked: need = plane * 5; break;
    default: break;
    }
    if (need && kPartHeader + need > p.len) return;
    if (p.kind == kVgaRle || p.kind == kEgaRle) cur_[0].set(src, data, end);
    if (p.kind == kVgaSprite) {
        // Each plane's rows follow the one before: find where planes 1-3
        // start by reading through the rows once
        cur_[0].set(src, data, end);
        for (int pl = 1; pl < 4; ++pl) {
            for (int r = 0; r < p.height; ++r)
                if (!skip_sprite_row(0)) return;
            const uint32_t at = cur_[0].pos - (cur_[0].n - cur_[0].at);
            cur_[pl].set(src, at, end);
        }
        cur_[0].set(src, data, end);
    }
    ok_ = true;
}

bool Pixels::rle_bytes(uint8_t* out, int n)
{
    Cursor& c = cur_[0];
    for (int i = 0; i < n; ++i) {
        if (run_ == 0) {
            const int b = c.next();
            if (b < 0) return false;
            if (b < 0x80) {
                run_ = b + 1;
                rep_ = -1;
            } else {
                run_ = 257 - b;
                rep_ = c.next();
                if (rep_ < 0) return false;
            }
        }
        int v = rep_;
        if (v < 0 && (v = c.next()) < 0) return false;
        out[i] = static_cast<uint8_t>(v);
        --run_;
    }
    return true;
}

bool Pixels::skip_sprite_row(int plane)
{
    Cursor& c = cur_[plane];
    for (int guard = 0; guard < 4 * kMaxWidth; ++guard) {
        const int s = c.next();
        if (s < 0) return false;
        if (s == 0) return true;
        int n = s;
        if (s >= 0x80 && (n = c.next()) < 0) return false;
        for (int k = 0; k < n; ++k)
            if (c.next() < 0) return false;
    }
    return false;
}

bool Pixels::sprite_row(int plane, uint8_t* px, uint8_t* seen)
{
    Cursor& c = cur_[plane];
    const int q = p_.units;
    int x = 0;
    for (int guard = 0; guard < 4 * kMaxWidth; ++guard) {
        const int s = c.next();
        if (s < 0) return false;
        if (s == 0) return true;
        int n = s;
        if (s >= 0x80) {
            x += 256 - s;
            if ((n = c.next()) < 0) return false;
        }
        for (int k = 0; k < n; ++k, ++x) {
            const int v = c.next();
            if (v < 0) return false;
            if (x < q) {
                px[x * 4 + plane] = static_cast<uint8_t>(v);
                if (seen) seen[x * 4 + plane] = 1;
            }
        }
    }
    return false;
}

bool Pixels::row(uint8_t* px, uint8_t* seen)
{
    if (!ok_ || row_ >= p_.height) return false;
    const int r = row_++;
    const int q = p_.units;
    const uint32_t data = p_.pos + kPartHeader;
    const uint32_t plane = static_cast<uint32_t>(p_.height) * q;
    memset(px, 0, w_);
    if (seen) memset(seen, 1, w_);
    switch (p_.kind) {
    case kVgaPlanar:
    case kVgaKeyed:
    case kVgaMasked: {
        const uint32_t pix = p_.kind == kVgaMasked ? data + plane * 4 : data;
        for (int pl = 0; pl < 4; ++pl) {
            if (!read_exact(src_, pix + pl * plane + static_cast<uint32_t>(r) * q, tmp_, q)) return ok_ = false;
            for (int x = 0; x < q; ++x) px[x * 4 + pl] = tmp_[x];
            if (p_.kind == kVgaKeyed && seen)
                for (int x = 0; x < q; ++x) seen[x * 4 + pl] = tmp_[x] != 0xFF;
            if (p_.kind == kVgaMasked) {
                if (!read_exact(src_, data + pl * plane + static_cast<uint32_t>(r) * q, tmp_, q)) return ok_ = false;
                for (int x = 0; x < q; ++x)
                    if (tmp_[x]) {
                        px[x * 4 + pl] = 0;
                        if (seen) seen[x * 4 + pl] = 0;
                    }
            }
        }
        if (p_.kind == kVgaKeyed)
            for (int x = 0; x < w_; ++x)
                if (px[x] == 0xFF) px[x] = 0;
        return true;
    }
    case kVgaRle: {
        if (!rle_bytes(tmp_, w_)) return ok_ = false;
        for (int pl = 0; pl < 4; ++pl)
            for (int x = 0; x < q; ++x) px[x * 4 + pl] = tmp_[pl * q + x];
        return true;
    }
    case kVgaSprite: {
        if (seen) memset(seen, 0, w_);
        for (int pl = 0; pl < 4; ++pl)
            if (!sprite_row(pl, px, seen)) return ok_ = false;
        return true;
    }
    case kEgaPlanar:
    case kEgaMasked:
    case kEgaRle: {
        const int planes = p_.kind == kEgaMasked ? 5 : 4;
        if (p_.kind == kEgaRle) {
            if (!rle_bytes(tmp_, q * 4)) return ok_ = false;
        } else {
            for (int pl = 0; pl < planes; ++pl)
                if (!read_exact(src_, data + pl * plane + static_cast<uint32_t>(r) * q, tmp_ + pl * q, q)) return ok_ = false;
        }
        const uint8_t* bits = p_.kind == kEgaMasked ? tmp_ + q : tmp_;
        for (int u = 0; u < q; ++u)
            for (int b = 0; b < 8; ++b) {
                const int m = 7 - b, x = u * 8 + b;
                if (p_.kind == kEgaMasked && (tmp_[u] >> m) & 1) {
                    if (seen) seen[x] = 0;
                    continue;
                }
                uint8_t v = 0;
                for (int pl = 0; pl < 4; ++pl) v |= static_cast<uint8_t>(((bits[pl * q + u] >> m) & 1) << pl);
                px[x] = v;
            }
        return true;
    }
    default:
        return false;
    }
}

// ---- Data entries ----------------------------------------------------------

int EntryReader::next()
{
    uint8_t b;
    return read(&b, 1) == 1 ? b : -1;
}

size_t EntryReader::read(uint8_t* buf, size_t n)
{
    if (n > left_) n = left_;
    if (n == 0) return 0;
    const size_t got = src_.read_at(pos_, buf, n);
    if (got < n) error_ = true;
    pos_ += static_cast<uint32_t>(got);
    left_ -= static_cast<uint32_t>(got);
    return got;
}

size_t EntryReader::skip(size_t n)
{
    if (n > left_) n = left_;
    pos_ += static_cast<uint32_t>(n);
    left_ -= static_cast<uint32_t>(n);
    return n;
}

bool find_entry(ByteSource& src, uint16_t id, uint32_t& pos, uint32_t& len)
{
    Library lib;
    if (open(src, 0, src.size(), lib) != Status::Ok) return false;
    const int i = find(src, lib, id);
    return i >= 0 && entry(src, lib, i, pos, len);
}

} // namespace hlib

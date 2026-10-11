#include "gbc_icons.h"

#include <cstdio>
#include <cstring>

#include "picture.h"

namespace gbc_icons {

namespace {

uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

int ega_of(int r, int g, int b)
{
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 16; ++i) {
        const int dr = r - pic::kEga[i].r, dg = g - pic::kEga[i].g, db = b - pic::kEga[i].b;
        const int dd = dr * dr + dg * dg + db * db;
        if (dd < bd) bd = dd, best = i;
    }
    return best;
}

} // namespace

bool bmp_to_pic4(dax::ByteSource& src, uint8_t* out)
{
    uint8_t h[54];
    if (src.size() < sizeof h || src.read_at(0, h, sizeof h) != sizeof h || h[0] != 'B' || h[1] != 'M') return false;
    const uint32_t off = u32(h + 10);
    const int w = static_cast<int32_t>(u32(h + 18));
    const int hh = static_cast<int32_t>(u32(h + 22));
    const int bpp = u16(h + 28);
    const uint32_t comp = u32(h + 30);
    const int height = hh < 0 ? -hh : hh;
    if (w != kSq || height != kSq || (bpp != 24 && bpp != 32) || (comp != 0 && comp != 3)) return false;
    const int bytes = bpp / 8;
    const uint32_t stride = (static_cast<uint32_t>(w) * bytes + 3) & ~3u;
    memset(out, 0, kPicBytes);
    uint8_t row[kSq * 4];
    for (int y = 0; y < kSq; ++y) {
        const int file_row = hh < 0 ? y : kSq - 1 - y;          // bottom-up unless the height is negative
        if (src.read_at(off + static_cast<uint32_t>(file_row) * stride, row, static_cast<size_t>(w) * bytes) !=
            static_cast<size_t>(w) * bytes)
            return false;
        for (int x = 0; x < kSq; ++x) {
            const uint8_t* p = row + x * bytes;                 // B, G, R (, A)
            int v = ega_of(p[2], p[1], p[0]);
            if (v == 8) v = 0;                                  // the background: see-through
            else if (v == 0) v = 8;                             // black (the fight's colours swap 0 and 8)
            uint8_t& b = out[y * (kSq / 2) + x / 2];
            b = static_cast<uint8_t>(x & 1 ? (b & 0xF0) | v : (b & 0x0F) | (v << 4));
        }
    }
    return true;
}

void parse(const char* text, size_t len, Choices& out)
{
    out = Choices{};
    size_t i = 0;
    while (i < len) {
        size_t e = i;
        while (e < len && text[e] != '\n' && text[e] != '\r') ++e;
        const char* line = text + i;
        const size_t n = e - i;
        const char* eq = static_cast<const char*>(memchr(line, '=', n));
        if (eq && eq > line && eq - line < 16) {
            int v = 0;
            for (const char* p = eq + 1; p < line + n && *p >= '0' && *p <= '9'; ++p) v = v * 10 + (*p - '0');
            char name[16] = {};
            memcpy(name, line, static_cast<size_t>(eq - line));
            if (v > 0 && v <= kMaxPairs) set(out, name, v);
        }
        i = e + 1;
    }
}

size_t write(const Choices& c, char* out, size_t cap)
{
    size_t n = 0;
    for (int i = 0; i < c.count && n < cap; ++i) {
        const int k = snprintf(out + n, cap - n, "%s=%d\n", c.name[i], c.pair[i]);
        if (k < 0 || static_cast<size_t>(k) >= cap - n) break;
        n += static_cast<size_t>(k);
    }
    return n;
}

int find(const Choices& c, const char* name)
{
    for (int i = 0; i < c.count; ++i)
        if (strncmp(c.name[i], name, 15) == 0) return c.pair[i];
    return 0;
}

void set(Choices& c, const char* name, int pair)
{
    for (int i = 0; i < c.count; ++i)
        if (strncmp(c.name[i], name, 15) == 0) {
            if (pair > 0) {
                c.pair[i] = static_cast<uint8_t>(pair);
            } else {
                for (int k = i; k + 1 < c.count; ++k) {
                    memcpy(c.name[k], c.name[k + 1], 16);
                    c.pair[k] = c.pair[k + 1];
                }
                --c.count;
            }
            return;
        }
    if (pair <= 0 || !name[0]) return;
    if (c.count == kMaxChoices) set(c, c.name[0], 0);          // full: the oldest goes
    strncpy(c.name[c.count], name, 15);
    c.name[c.count][15] = 0;
    c.pair[c.count++] = static_cast<uint8_t>(pair);
}

} // namespace gbc_icons

#include "journal.h"

#include <cstring>

namespace journal {

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }

uint32_t index_at(const Info& info) { return 8u + 2u * static_cast<uint32_t>(info.widths); }
uint32_t record_size(const Info& info) { return 4u + 4u * static_cast<uint32_t>(info.widths); }

bool upper_eq(const char* s, const char* word)
{
    for (; *word; ++s, ++word) {
        char c = *s;
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        if (c != *word) return false;
    }
    return true;
}

bool is_space(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

// After a keyword at s: spaces / '#', then digits that end before the
// text does or at a non-digit. Returns the number, or -1.
int number_after(const char* s)
{
    while (is_space(*s) || *s == '#') ++s;
    if (*s < '0' || *s > '9') return -1;
    int n = 0, k = 0;
    while (*s >= '0' && *s <= '9' && k < 4) {
        n = n * 10 + (*s - '0');
        ++s;
        ++k;
    }
    if (*s >= '0' && *s <= '9') return -1;
    return n;
}

} // namespace

bool read_info(dax::ByteSource& src, Info& out)
{
    uint8_t h[8 + 2 * kMaxWidths];
    if (src.read_at(0, h, 8) != 8 || memcmp(h, "GBJ1", 4) != 0 || h[4] != 1) return false;
    out.widths = h[5];
    out.entries = u16(h + 6);
    if (out.widths < 1 || out.widths > kMaxWidths) return false;
    if (src.read_at(8, h + 8, 2u * out.widths) != 2u * out.widths) return false;
    for (int i = 0; i < out.widths; ++i) out.width[i] = u16(h + 8 + 2 * i);
    return index_at(out) + record_size(out) * static_cast<uint32_t>(out.entries) <= src.size();
}

int pick_width(const Info& info, int room)
{
    int best = -1, narrowest = -1;
    for (int i = 0; i < info.widths; ++i) {
        if (info.width[i] <= room && (best < 0 || info.width[i] > info.width[best])) best = i;
        if (narrowest < 0 || info.width[i] < info.width[narrowest]) narrowest = i;
    }
    return best >= 0 ? best : narrowest;
}

bool find(dax::ByteSource& src, const Info& info, char kind, int number, int wi, Picture& out)
{
    if (wi < 0 || wi >= info.widths) return false;
    const uint32_t rs = record_size(info);
    uint8_t r[4 + 4 * kMaxWidths];
    for (int e = 0; e < info.entries; ++e) {
        if (src.read_at(index_at(info) + rs * static_cast<uint32_t>(e), r, rs) != rs) return false;
        if (r[0] != static_cast<uint8_t>(kind) || u16(r + 2) != number) continue;
        const uint32_t at = u32(r + 4 + 4 * wi);
        uint8_t p[2 + 2 + 32 + 4];
        if (src.read_at(at, p, sizeof p) != sizeof p) return false;
        out.w = u16(p);
        out.h = u16(p + 2);
        for (int i = 0; i < 16; ++i) out.palette[i] = u16(p + 4 + 2 * i);
        out.data_len = u32(p + 36);
        out.data_at = at + sizeof p;
        return out.w > 0 && out.w <= kMaxWidth && out.h > 0 && out.data_at + out.data_len <= src.size();
    }
    return false;
}

bool rows(dax::ByteSource& src, const Picture& p, int first, int count, RowFn fn, void* ctx)
{
    uint8_t row[kMaxWidth];
    uint8_t buf[256];
    uint32_t pos = p.data_at, end = p.data_at + p.data_len;
    size_t have = 0, used = 0;
    int y = 0, x = 0;
    const int last = first + count;
    while (y < last && y < p.h) {
        if (used == have) {
            if (pos >= end) return false;
            const uint32_t n = end - pos < sizeof buf ? end - pos : static_cast<uint32_t>(sizeof buf);
            have = src.read_at(pos, buf, n);
            if (have == 0) return false;
            pos += static_cast<uint32_t>(have);
            used = 0;
        }
        const uint8_t b = buf[used++];
        int run = (b >> 4) + 1;
        if (x + run > p.w) return false;          // a run never crosses a row
        if (y >= first) memset(row + x, b & 15, static_cast<size_t>(run));
        x += run;
        if (x == p.w) {
            if (y >= first) fn(y, row, p.w, ctx);
            ++y;
            x = 0;
        }
    }
    return true;
}

char find_mention(const char* text, int* number)
{
    char kind = 0;
    for (const char* s = text; *s; ++s) {
        if (upper_eq(s, "JOURNAL")) {
            // "ENTRY" within a few words: "JOURNAL ENTRY", "JOURNAL AS ENTRY"
            const char* t = s + 7;
            for (int k = 0; k < 12 && *t; ++k, ++t) {
                if (upper_eq(t, "ENTRY")) {
                    const int n = number_after(t + 5);
                    if (n >= 0) {
                        kind = 'J';
                        *number = n;
                    }
                    break;
                }
            }
        } else if (upper_eq(s, "TAVERN TALE")) {
            const int n = number_after(s + 11);
            if (n >= 0) {
                kind = 'T';
                *number = n;
            }
        }
    }
    return kind;
}

} // namespace journal

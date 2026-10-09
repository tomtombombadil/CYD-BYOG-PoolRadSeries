#include "journal.h"

#include <cstdlib>
#include <cstring>
#include <new>

#include "jpeg.h"
#include "pdf.h"

namespace journal {

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }


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

// ---- palette ----------------------------------------------------------------
// 216 colours of a 6 x 6 x 6 cube (levels 0, 51 ... 255), then 40 greys

void palette_rgb(int i, uint8_t* r, uint8_t* g, uint8_t* b)
{
    if (i < 216) {
        *r = static_cast<uint8_t>((i / 36) * 51);
        *g = static_cast<uint8_t>((i / 6 % 6) * 51);
        *b = static_cast<uint8_t>((i % 6) * 51);
    } else {
        const int v = (i - 216) * 255 / 39;
        *r = *g = *b = static_cast<uint8_t>(v);
    }
}

uint8_t index_of(int r, int g, int b)
{
    const int lum = (r + g + b) / 3;
    const bool blue = b - r > 30;
    if ((!blue && lum > 185) || (blue && lum > 225)) return 215;        // paper: white
    const int mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const int mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    if (mx - mn < 14) return static_cast<uint8_t>(216 + (lum * 39 + 127) / 255);
    return static_cast<uint8_t>(((r + 25) / 51) * 36 + ((g + 25) / 51) * 6 + (b + 25) / 51);
}

const Table* find_table(uint32_t pdf_size, const char* pdf_id)
{
    for (int i = 0; i < kTableCount; ++i)
        if (kTables[i]->pdf_size == pdf_size && strcmp(kTables[i]->pdf_id, pdf_id) == 0) return kTables[i];
    return nullptr;
}

// ---- making JOURNAL.DAT -----------------------------------------------------

namespace {

constexpr uint32_t kHeader = 12 + 32 + 768;

void put16(uint8_t* p, unsigned v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v)
{
    for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// One page: its pieces, decoded a band of MCU rows at a time
struct PageWork {
    const Table* t;
    const uint32_t* base;          // each piece's data offset
    Output* out;
    int list[64];                  // pieces on this page
    int n = 0;
    int x0 = 0, x1 = 0;            // the columns the pieces cover
    int bottom = 0;
    int band_y = -1, band_h = 16;
    uint8_t* band = nullptr;       // band_h rows of x1 - x0 indexes
    uint8_t* tmp = nullptr;        // one piece's rows of a band
    bool ok = true;
};

void flush(PageWork& w)
{
    if (w.band_y < 0) return;
    const int bw = w.x1 - w.x0;
    for (int k = 0; k < w.n && w.ok; ++k) {
        const Piece& p = w.t->pieces[w.list[k]];
        const int r0 = p.y > w.band_y ? p.y : w.band_y;
        const int r1 = p.y + p.h < w.band_y + w.band_h ? p.y + p.h : w.band_y + w.band_h;
        if (r1 <= r0) continue;
        for (int r = r0; r < r1; ++r)
            memcpy(w.tmp + static_cast<size_t>(r - r0) * p.w, w.band + static_cast<size_t>(r - w.band_y) * bw + (p.x - w.x0), p.w);
        w.ok = w.out->write_at(w.base[w.list[k]] + static_cast<uint32_t>(r0 - p.y) * p.w, w.tmp,
                               static_cast<size_t>(r1 - r0) * p.w);
    }
}

int want_mcu(int x, int y, int mw, int mh, void* ctx)
{
    PageWork& w = *static_cast<PageWork*>(ctx);
    if (y >= w.bottom) return -1;
    for (int k = 0; k < w.n; ++k) {
        const Piece& p = w.t->pieces[w.list[k]];
        if (x < p.x + p.w && x + mw > p.x && y < p.y + p.h && y + mh > p.y) return 1;
    }
    return 0;
}

bool got_block(int x, int y, int bw_, int bh, const uint8_t* rgb, void* ctx)
{
    PageWork& w = *static_cast<PageWork*>(ctx);
    if (y != w.band_y) {
        flush(w);
        w.band_y = y;
    }
    const int bw = w.x1 - w.x0;
    for (int r = 0; r < bh && r < w.band_h; ++r) {
        const uint8_t* s = rgb + static_cast<size_t>(r) * bw_ * 3;
        for (int c = 0; c < bw_; ++c, s += 3) {
            const int px = x + c;
            if (px < w.x0 || px >= w.x1) continue;
            w.band[static_cast<size_t>(r) * bw + (px - w.x0)] = index_of(s[0], s[1], s[2]);
        }
    }
    return w.ok;
}

} // namespace

bool make(dax::ByteSource& src, const Table& t, Output& out, Progress progress, void* ctx)
{
    pdf::Doc* doc = new (std::nothrow) pdf::Doc;
    int* pages = new (std::nothrow) int[pdf::kMaxPages];
    uint32_t* base = new (std::nothrow) uint32_t[t.n_pieces];
    uint8_t* pool = static_cast<uint8_t*>(malloc(jpeg::kPoolSize));
    bool ok = doc && pages && base && pool && pdf::open(src, *doc);
    int n_pages = ok ? pdf::pages(src, *doc, pages, pdf::kMaxPages) : 0;

    // The header (without its "GBJ2") and the tables
    uint32_t at = kHeader + 8u * static_cast<uint32_t>(t.n_entries) + 8u * static_cast<uint32_t>(t.n_pieces);
    if (ok) {
        uint8_t h[kHeader] = {};
        h[4] = 1;
        put16(h + 6, static_cast<unsigned>(t.n_entries));
        put16(h + 8, static_cast<unsigned>(t.n_pieces));
        memcpy(h + 12, t.pdf_id, strlen(t.pdf_id) < 32 ? strlen(t.pdf_id) : 32);
        for (int i = 0; i < 256; ++i) palette_rgb(i, h + 44 + 3 * i, h + 45 + 3 * i, h + 46 + 3 * i);
        ok = out.write_at(0, h, sizeof h);
        for (int e = 0; ok && e < t.n_entries; ++e) {
            uint8_t r[8] = {static_cast<uint8_t>(t.entries[e].kind), 0};
            put16(r + 2, t.entries[e].number);
            put16(r + 4, t.entries[e].first);
            put16(r + 6, t.entries[e].count);
            ok = out.write_at(kHeader + 8u * static_cast<uint32_t>(e), r, 8);
        }
        for (int i = 0; ok && i < t.n_pieces; ++i) {
            base[i] = at;
            uint8_t r[8];
            put16(r, t.pieces[i].w);
            put16(r + 2, t.pieces[i].h);
            put32(r + 4, at);
            ok = out.write_at(kHeader + 8u * static_cast<uint32_t>(t.n_entries) + 8u * static_cast<uint32_t>(i), r, 8);
            at += static_cast<uint32_t>(t.pieces[i].w) * t.pieces[i].h;
        }
    }

    // The pages the table uses, in order
    int used[pdf::kMaxPages] = {};
    int n_used = 0;
    for (int i = 0; i < t.n_pieces; ++i) {
        const int p = t.pieces[i].page;
        bool seen = false;
        for (int k = 0; k < n_used; ++k) seen = seen || used[k] == p;
        if (!seen && n_used < pdf::kMaxPages) used[n_used++] = p;
    }
    for (int i = 1; i < n_used; ++i)
        for (int k = i; k > 0 && used[k - 1] > used[k]; --k) {
            const int tmp = used[k - 1];
            used[k - 1] = used[k];
            used[k] = tmp;
        }

    for (int u = 0; ok && u < n_used; ++u) {
        const int page = used[u];
        pdf::Image img;
        ok = page >= 1 && page <= n_pages && pdf::page_image(src, *doc, pages[page - 1], img) && img.jpeg;
        if (!ok) break;
        PageWork* w = new (std::nothrow) PageWork;
        ok = w != nullptr;
        if (!ok) break;
        w->t = &t;
        w->base = base;
        w->out = &out;
        w->x0 = 1 << 30;
        int maxw = 0;
        for (int i = 0; i < t.n_pieces && w->n < 64; ++i) {
            const Piece& p = t.pieces[i];
            if (p.page != page) continue;
            w->list[w->n++] = i;
            if (p.x < w->x0) w->x0 = p.x;
            if (p.x + p.w > w->x1) w->x1 = p.x + p.w;
            if (p.y + p.h > w->bottom) w->bottom = p.y + p.h;
            if (p.w > maxw) maxw = p.w;
        }
        jpeg::Info ji;
        ok = jpeg::probe(src, img.data_at, img.data_len, pool, ji) && w->x1 <= ji.width && w->bottom <= ji.height;
        if (ok) {
            w->band_h = ji.mcu_h;
            w->band = static_cast<uint8_t*>(malloc(static_cast<size_t>(w->x1 - w->x0) * w->band_h));
            w->tmp = static_cast<uint8_t*>(malloc(static_cast<size_t>(maxw) * w->band_h));
            ok = w->band && w->tmp &&
                 jpeg::decode(src, img.data_at, img.data_len, pool, want_mcu, got_block, w) && w->ok;
            if (ok) {
                flush(*w);
                ok = w->ok;
            }
        }
        free(w->band);
        free(w->tmp);
        delete w;
        if (progress) progress(u + 1, n_used, ctx);
    }
    if (ok) ok = out.write_at(0, reinterpret_cast<const uint8_t*>("GBJ2"), 4);
    free(pool);
    delete[] base;
    delete[] pages;
    delete doc;
    return ok;
}

// ---- reading it -------------------------------------------------------------

bool read_info(dax::ByteSource& src, Info& out)
{
    uint8_t h[kHeader];
    if (src.read_at(0, h, sizeof h) != sizeof h || memcmp(h, "GBJ2", 4) != 0 || h[4] != 1) return false;
    out.entries = u16(h + 6);
    out.pieces = u16(h + 8);
    memcpy(out.pdf_id, h + 12, 32);
    out.pdf_id[32] = 0;
    memcpy(out.palette, h + 44, 768);
    return kHeader + 8u * static_cast<uint32_t>(out.entries + out.pieces) <= src.size();
}

bool find(dax::ByteSource& src, const Info& info, char kind, int number, int* first, int* count)
{
    for (int e = 0; e < info.entries; ++e) {
        uint8_t r[8];
        if (src.read_at(kHeader + 8u * static_cast<uint32_t>(e), r, 8) != 8) return false;
        if (r[0] != static_cast<uint8_t>(kind) || u16(r + 2) != number) continue;
        *first = u16(r + 4);
        *count = u16(r + 6);
        return *first + *count <= info.pieces && *count > 0;
    }
    return false;
}

bool piece(dax::ByteSource& src, const Info& info, int i, PieceInfo& out)
{
    if (i < 0 || i >= info.pieces) return false;
    uint8_t r[8];
    if (src.read_at(kHeader + 8u * static_cast<uint32_t>(info.entries + i), r, 8) != 8) return false;
    out.w = u16(r);
    out.h = u16(r + 2);
    out.offset = u32(r + 4);
    return out.w > 0 && out.h > 0 && out.offset + static_cast<uint32_t>(out.w) * out.h <= src.size();
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

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

// ---- the Gold Box Companion's journal (Steam / SNEG) ---------------------------

namespace {

// Game.dat a byte at a time, through a small buffer
struct Reader {
    dax::ByteSource& src;
    uint32_t pos = 0, buf_at = 0;
    size_t buf_n = 0;
    uint8_t buf[512];
    explicit Reader(dax::ByteSource& s, uint32_t at = 0) : src(s), pos(at) {}
    int get()
    {
        if (pos < buf_at || pos >= buf_at + buf_n) {
            buf_at = pos;
            buf_n = src.read_at(pos, buf, sizeof buf);
            if (buf_n == 0) return -1;
        }
        return buf[pos++ - buf_at];
    }
    // Whether `word` comes next (moves past it if so)
    bool next_is(const char* word)
    {
        const uint32_t save = pos;
        for (; *word; ++word)
            if (get() != static_cast<uint8_t>(*word)) {
                pos = save;
                return false;
            }
        return true;
    }
};

bool is_blank(int c) { return c == ' ' || c == '\t' || c == '\r'; }

} // namespace

int gbc_entries(dax::ByteSource& dat, GbcEntry* out, int max)
{
    Reader r(dat);
    int n = 0;
    for (int c = r.get(); c >= 0 && n < max; c = r.get()) {
        if (c != '<' || !r.next_is("journal ")) continue;
        int num = 0, digits = 0;
        for (c = r.get(); c >= '0' && c <= '9' && digits < 4; c = r.get(), ++digits) num = num * 10 + (c - '0');
        if (c != '>' || digits == 0) continue;
        GbcEntry& e = out[n];
        e = GbcEntry{};
        e.number = num;
        e.at = r.pos;
        // To "</journal>"
        bool closed = false;
        for (c = r.get(); c >= 0; c = r.get()) {
            if (c == '<' && r.next_is("/journal>")) {
                closed = true;
                break;
            }
        }
        if (!closed) break;
        e.len = r.pos - 10 - e.at;
        // A short "( ... )" note: a picture entry
        char t[160];
        const size_t tl = gbc_text(dat, e, t, sizeof t);
        e.picture = tl > 1 && tl < 120 && t[0] == '(' && t[tl - 1] == ')';
        ++n;
    }
    return n;
}

size_t gbc_text(dax::ByteSource& dat, const GbcEntry& e, char* out, size_t cap)
{
    if (cap == 0) return 0;
    Reader r(dat, e.at);
    size_t n = 0;
    bool line_empty = true;       // nothing yet on this line
    bool blank = false;           // blanks since the last text on this line
    int breaks = 0;               // line ends since the last text
    auto put = [&](char ch) {
        if (n + 1 < cap) out[n++] = ch;
    };
    while (r.pos < e.at + e.len) {
        const int c = r.get();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            if (!line_empty) breaks = 1;
            else if (breaks > 0) breaks = 2;          // an empty line: a new paragraph
            line_empty = true;
            blank = false;
            continue;
        }
        if (is_blank(c)) {
            blank = !line_empty;                      // (leading blanks go)
            continue;
        }
        if (line_empty && n > 0) put(breaks >= 2 ? '\n' : ' ');
        else if (blank) put(' ');
        line_empty = false;
        blank = false;
        breaks = 0;
        put(static_cast<char>(c));
    }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\r')) --n;
    out[n] = 0;
    return n;
}

namespace {

// A whole JPEG into a picture piece, a band of MCU rows at a time
struct WholeWork {
    Output* out;
    uint32_t base = 0;
    int w = 0, h = 0;
    int band_y = -1, band_h = 16;
    uint8_t* band = nullptr;
    bool ok = true;
};

void whole_flush(WholeWork& w)
{
    if (w.band_y < 0 || !w.ok) return;
    int rows = w.band_h;
    if (w.band_y + rows > w.h) rows = w.h - w.band_y;
    if (rows <= 0) return;
    w.ok = w.out->write_at(w.base + static_cast<uint32_t>(w.band_y) * w.w, w.band, static_cast<size_t>(rows) * w.w);
}

bool whole_block(int x, int y, int bw, int bh, const uint8_t* rgb, void* ctx)
{
    WholeWork& w = *static_cast<WholeWork*>(ctx);
    if (y != w.band_y) {
        whole_flush(w);
        w.band_y = y;
        memset(w.band, 215, static_cast<size_t>(w.w) * w.band_h);
    }
    for (int r = 0; r < bh && r < w.band_h; ++r) {
        const uint8_t* s = rgb + static_cast<size_t>(r) * bw * 3;
        for (int c = 0; c < bw; ++c, s += 3) {
            const int px = x + c;
            if (px >= w.w) break;
            w.band[static_cast<size_t>(r) * w.w + px] = index_of(s[0], s[1], s[2]);
        }
    }
    return w.ok;
}

} // namespace

bool make_gbc(dax::ByteSource& dat, GbcPictures& pics, const char* id, Output& out, Progress progress, void* ctx)
{
    GbcEntry* es = new (std::nothrow) GbcEntry[kMaxGbcEntries];
    uint8_t* table = nullptr;
    char* text = static_cast<char*>(malloc(16384));
    uint8_t* pool = static_cast<uint8_t*>(malloc(jpeg::kPoolSize));
    const int n = es ? gbc_entries(dat, es, kMaxGbcEntries) : 0;
    bool ok = es && text && pool && n > 0;
    if (ok) table = static_cast<uint8_t*>(calloc(static_cast<size_t>(n) * 16, 1));
    ok = ok && table;
    // The data after the header and the tables; each entry one piece
    uint32_t at = kHeader + 16u * static_cast<uint32_t>(n);
    for (int i = 0; ok && i < n; ++i) {
        const GbcEntry& e = es[i];
        uint8_t* er = table + 8 * i;
        uint8_t* pr = table + 8 * n + 8 * i;
        er[0] = 'J';
        put16(er + 2, static_cast<unsigned>(e.number));
        put16(er + 4, static_cast<unsigned>(i));
        put16(er + 6, 1);
        bool done = false;
        if (e.picture) {
            dax::ByteSource* js = pics.open(e.number);
            jpeg::Info ji;
            if (js && jpeg::probe(*js, 0, js->size(), pool, ji) && ji.width > 0 && ji.width <= 1700 && ji.height > 0 &&
                ji.height < 65536) {
                WholeWork w;
                w.out = &out;
                w.base = at;
                w.w = ji.width;
                w.h = ji.height;
                w.band_h = ji.mcu_h;
                w.band = static_cast<uint8_t*>(malloc(static_cast<size_t>(w.w) * w.band_h));
                ok = w.band && jpeg::decode(*js, 0, js->size(), pool, nullptr, whole_block, &w) && w.ok;
                if (ok) {
                    whole_flush(w);
                    ok = w.ok;
                }
                free(w.band);
                if (ok) {
                    put16(pr, static_cast<unsigned>(w.w));
                    put16(pr + 2, static_cast<unsigned>(w.h));
                    put32(pr + 4, at);
                    at += static_cast<uint32_t>(w.w) * w.h;
                    done = true;
                }
            }
        }
        if (ok && !done) {
            // Text (a picture entry without its picture keeps its note)
            const size_t len = gbc_text(dat, e, text, 16384);
            const size_t keep = len > 0 ? len : 1;
            if (len == 0) text[0] = ' ';
            ok = out.write_at(at, reinterpret_cast<const uint8_t*>(text), keep);
            put16(pr, 0);
            put16(pr + 2, static_cast<unsigned>(keep));
            put32(pr + 4, at);
            at += static_cast<uint32_t>(keep);
        }
        if (progress) progress(i + 1, n, ctx);
    }
    if (ok) {
        uint8_t h[kHeader] = {};
        h[4] = 2;
        put16(h + 6, static_cast<unsigned>(n));
        put16(h + 8, static_cast<unsigned>(n));
        const size_t il = strlen(id);
        memcpy(h + 12, id, il < 32 ? il : 32);
        for (int i = 0; i < 256; ++i) palette_rgb(i, h + 44 + 3 * i, h + 45 + 3 * i, h + 46 + 3 * i);
        ok = out.write_at(0, h, sizeof h) && out.write_at(kHeader, table, static_cast<size_t>(n) * 16);
    }
    if (ok) ok = out.write_at(0, reinterpret_cast<const uint8_t*>("GBJ2"), 4);
    free(table);
    free(pool);
    free(text);
    delete[] es;
    return ok;
}

// ---- reading it -------------------------------------------------------------

bool read_info(dax::ByteSource& src, Info& out)
{
    uint8_t h[kHeader];
    if (src.read_at(0, h, sizeof h) != sizeof h || memcmp(h, "GBJ2", 4) != 0 || (h[4] != 1 && h[4] != 2)) return false;
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
    if (out.w == 0) return out.h > 0 && out.offset + static_cast<uint32_t>(out.h) <= src.size();     // text
    return out.h > 0 && out.offset + static_cast<uint32_t>(out.w) * out.h <= src.size();
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

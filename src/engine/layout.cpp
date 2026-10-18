#include "layout.h"

#include <cstring>
#include <initializer_list>

namespace layout {

namespace {

constexpr int kCols = pic::kScreenW / 8;    // 40

void put(pic::Canvas& c, const Tiles& t, int n, int col, int row)
{
    n = t.slot(n);
    if (n < 0 || n >= t.count || !t.loaded) return;
    const uint8_t* s = t.px[n];
    for (int y = 0; y < 8; ++y) {
        const int py = row * 8 + y;
        if (py < 0 || py >= c.h) continue;
        for (int x = 0; x < 8; ++x) {
            const int px = col * 8 + x;
            const uint8_t v = s[y * 8 + x];
            if (px >= 0 && px < c.w && v != kMask) c.set(px, py, v);
        }
    }
}

// Secret's bars (secret_facts.md 3): a horizontal bar, full width
void hbar(pic::Canvas& c, const Tables& tb, const Tiles& t, int row)
{
    for (int col = 0; col < kCols; ++col) put(c, t, tb.bars[0], col, row);
}

// A vertical between the bars at rows r0 and r1: a cap under the first, the
// side, a cap over the second (caps false: the side all the way)
void vbar(pic::Canvas& c, const Tables& tb, const Tiles& t, int col, int r0, int r1, bool caps = true)
{
    for (int row = r0 + 1; row < r1; ++row) {
        int n = tb.bars[2];
        if (caps && row == r0 + 1) n = tb.bars[1];
        else if (caps && row == r1 - 1) n = tb.bars[3];
        put(c, t, n, col, row);
    }
}

// The 3D view's / a picture's 13 x 13 frame with its top-left at (col, row)
void view_bars(pic::Canvas& c, const Tables& tb, const Tiles& t, int col, int row)
{
    for (int i = 0; i <= 12; ++i) {
        put(c, t, tb.bars[4], col + i, row);
        put(c, t, tb.bars[4], col + i, row + 12);
    }
    for (int i = 1; i <= 11; ++i) {
        const int n = i == 1 ? tb.bars[5] : i == 11 ? tb.bars[7] : tb.bars[6];
        put(c, t, n, col, row + i);
        put(c, t, n, col + 12, row + i);
    }
}

bool all_below(const uint8_t* p, int from, int to, int limit)
{
    for (int i = from; i < to; ++i)
        if (p[i] >= limit) return false;
    return true;
}

} // namespace

const char* status_text(Status s)
{
    switch (s) {
    case Status::Ok:        return "ok";
    case Status::NotPacked: return "program not in the expected format";
    case Status::ReadError: return "read error";
    case Status::BadData:   return "program file is damaged";
    case Status::BadTables: return "frame tables not found in this program";
    }
    return "?";
}

bool load_vga_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Tiles& out, int slot0, int n, bool ega)
{
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    uint8_t hdr[pic::kVgaHeaderSize];
    pic::VgaHeader h;
    uint8_t map[256];
    for (int i = 0; i < 256; ++i) map[i] = static_cast<uint8_t>(ega ? i & 15 : i);
    dax::RleReader r(src, idx, *e);
    if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_vga_header(hdr, e->raw_size, h) || h.height != 8 ||
        h.width_cols != 1 || r.skip(h.count * 3u) != h.count * 3u)
        return false;
    if (ega && !pic::read_vga_ega(r, h, map)) return false;
    if (!ega && r.skip((h.count + 1) / 2u) != (h.count + 1) / 2u) return false;
    const uint32_t gap = h.pixels_at - (pic::kVgaHeaderSize + h.count * 3u + (h.count + 1) / 2u);
    if (r.skip(gap) != gap) return false;
    int k = 0;
    for (; k < h.frames && k < n && slot0 + k < kTiles; ++k) {
        uint8_t px[64];
        if (r.read(px, sizeof px) != sizeof px) return false;
        for (int i = 0; i < 64; ++i) out.px[slot0 + k][i] = map[px[i]];
    }
    if (slot0 + k > out.count) out.count = static_cast<uint8_t>(slot0 + k);
    out.first = 20;
    out.loaded = true;
    return k > 0;
}

bool load_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Tiles& out, uint8_t first)
{
    out.loaded = false;
    out.first = first < 20 ? 20 : first;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    uint8_t hdr[pic::kHeaderSize];
    if (r.read(hdr, sizeof hdr) != sizeof hdr) return false;
    pic::Header h;
    if (!pic::parse_header(hdr, e->raw_size, h)) return false;
    if (h.height != 8 || h.width_cols != 1 || h.frames < 23) return false;
    out.count = 0;
    for (int n = 0; n < h.frames; ++n) {
        uint8_t packed[32];
        if (r.read(packed, sizeof packed) != sizeof packed) return false;
        const int s = out.slot(n);
        if (s < 0) {
            if (n >= out.first) break;       // past the window: done
            continue;
        }
        for (int i = 0; i < 32; ++i) {
            out.px[s][i * 2] = packed[i] >> 4;
            out.px[s][i * 2 + 1] = packed[i] & 15;
        }
        out.count = static_cast<uint8_t>(s + 1);
    }
    if (out.count < 23) return false;
    out.loaded = true;
    return true;
}

Status load_tables(dax::ByteSource& program, const exepack::Info& info, const profile::Profile& p, Tables& out)
{
    out.loaded = false;
    if (p.variant.box[0] || p.variant.bars[0] || p.variant.vga) {
        // Pool: boxes; Secret: bars; Darkness: its bar routine - no tables
        // to read
        memcpy(out.box, p.variant.box, sizeof out.box);
        memcpy(out.bars, p.variant.bars, sizeof out.bars);
        out.chain = p.variant.vga;
        out.loaded = true;
        return Status::Ok;
    }
    const profile::FrameTables& f = p.frame;
    struct Slice { uint16_t at; uint8_t* dst; uint8_t len; };
    const Slice s[] = {
        {f.top, out.top, 40}, {f.bar, out.bar, 40}, {f.bottom, out.bottom, 40},
        {f.left, out.left, 24}, {f.right, out.right, 24}, {f.view_split, out.view_split, 17},
        {f.view_top, out.view_top, 15}, {f.view_bottom, out.view_bottom, 15},
        {f.view_left, out.view_left, 15}, {f.view_right, out.view_right, 15},
        {f.combat_left, out.combat_left, 23}, {f.combat_split, out.combat_split, 23},
        {f.combat_right, out.combat_right, 23},
    };
    // One span covering every table: unpacking walks the whole program, so
    // do it once
    uint32_t lo = 0xFFFFFFFF, hi = 0;
    for (const Slice& x : s) {
        if (x.at < lo) lo = x.at;
        if (x.at + x.len > hi) hi = x.at + x.len;
    }
    uint8_t span[512];
    if (hi - lo > sizeof span) return Status::BadTables;
    switch (exepack::read(program, info, p.data_base + lo, span, hi - lo)) {
    case exepack::Status::Ok:        break;
    case exepack::Status::ReadError: return Status::ReadError;
    case exepack::Status::BadData:   return Status::BadData;
    default:                         return Status::NotPacked;
    }
    for (const Slice& x : s) memcpy(x.dst, span + (x.at - lo), x.len);

    // Sanity: every value used must name a tile
    const int fmax = kTiles - kFrameTile, vmax = kTiles - kViewTile;
    if (!all_below(out.top, 0, 40, fmax) || !all_below(out.bar, 0, 40, fmax) || !all_below(out.bottom, 0, 40, fmax) ||
        !all_below(out.left, 0, 24, fmax) || !all_below(out.right, 0, 24, fmax) ||
        !all_below(out.view_split, 0, 17, fmax) ||
        !all_below(out.view_top, 2, 15, vmax) || !all_below(out.view_bottom, 2, 15, vmax) ||
        !all_below(out.view_left, 2, 15, vmax) || !all_below(out.view_right, 2, 15, vmax) ||
        !all_below(out.combat_left, 0, 23, fmax) || !all_below(out.combat_split, 0, 23, fmax) ||
        !all_below(out.combat_right, 0, 23, fmax))
        return Status::BadTables;
    out.loaded = true;
    return Status::Ok;
}

void tile(pic::Canvas& c, const Tiles& t, int n, int col, int row) { put(c, t, n, col, row); }

namespace {
// Darkness's bar routine (darkness_facts.md 7.1): the start piece, the bar
// between, the end piece - across when the rows are the same, else down
constexpr int kPiece = 20;          // BORDERS' piece 0 in the tiles
void chain_bar(pic::Canvas& c, const Tiles& t, int c1, int r1, int c2, int r2, int sp, int ep)
{
    if (r1 == r2) {
        put(c, t, kPiece + sp, c1, r1);
        for (int col = c1 + 1; col < c2; ++col) put(c, t, kPiece + 4, col, r1);
        put(c, t, kPiece + ep, c2, r1);
    } else {
        put(c, t, kPiece + sp, c1, r1);
        for (int row = r1 + 1; row < r2; ++row) put(c, t, kPiece + 5, c1, row);
        put(c, t, kPiece + ep, c1, r2);
    }
}
// Its tables (DS 0x0DAA outer, 0x0DE4 fights; 0x0DC4 the divider and the
// box): rows of col1 row1 col2 row2 start end
void chain_rows(pic::Canvas& c, const Tiles& t, const uint8_t (*rows)[6], int n)
{
    for (int i = 0; i < n; ++i) chain_bar(c, t, rows[i][0], rows[i][1], rows[i][2], rows[i][3], rows[i][4], rows[i][5]);
}
constexpr uint8_t kChainOuter[][6] = {{0, 0, 39, 0, 0, 1}, {0, 0, 0, 23, 0, 2}, {39, 0, 39, 23, 1, 3}, {0, 23, 39, 23, 2, 3}};
constexpr uint8_t kChainFight[][6] = {{0, 0, 39, 0, 0, 1}, {0, 0, 0, 22, 0, 2}, {39, 0, 39, 22, 1, 3},
                                      {0, 22, 39, 22, 2, 3}, {22, 0, 22, 22, 6, 7}};
void chain_box(pic::Canvas& c, const Tiles& t, int c0, int r0, int c1, int r1)
{
    const uint8_t rows[][6] = {{uint8_t(c0), uint8_t(r0), uint8_t(c1), uint8_t(r0), 0, 1},
                               {uint8_t(c0), uint8_t(r1), uint8_t(c1), uint8_t(r1), 2, 3},
                               {uint8_t(c0), uint8_t(r0), uint8_t(c0), uint8_t(r1), 0, 2},
                               {uint8_t(c1), uint8_t(r0), uint8_t(c1), uint8_t(r1), 1, 3}};
    chain_rows(c, t, rows, 4);
}
} // namespace

// Pool: the border of a box whose corners are cells (c0, r0) and (c1, r1)
void box(pic::Canvas& c, const Tables& tb, const Tiles& t, int c0, int r0, int c1, int r1)
{
    if (tb.chain) {
        chain_box(c, t, c0, r0, c1, r1);
        return;
    }
    if (!tb.box[0]) return;
    for (int col = c0 + 1; col < c1; ++col) {
        put(c, t, tb.box[2], col, r0);
        put(c, t, tb.box[2], col, r1);
    }
    for (int row = r0 + 1; row < r1; ++row) {
        put(c, t, tb.box[1], c0, row);
        put(c, t, tb.box[1], c1, row);
    }
    put(c, t, tb.box[0], c0, r0);
    put(c, t, tb.box[0], c1, r0);
    put(c, t, tb.box[0], c0, r1);
    put(c, t, tb.box[0], c1, r1);
}

void outer(pic::Canvas& c, const Tables& tb, const Tiles& t)
{
    if (tb.chain) {
        c.fill(8, 8, 38 * 8, 22 * 8, 0);
        chain_rows(c, t, kChainOuter, 4);
        return;
    }
    if (tb.bars[0]) {
        c.fill(8, 8, 38 * 8, 22 * 8, 0);
        hbar(c, tb, t, 0);
        hbar(c, tb, t, 23);
        vbar(c, tb, t, 0, 0, 23);
        vbar(c, tb, t, kCols - 1, 0, 23);
        return;
    }
    if (tb.box[0]) {
        c.fill(8, 8, 38 * 8, 22 * 8, 0);
        box(c, tb, t, 0, 0, kCols - 1, 23);
        return;
    }
    c.fill(8, 8, 38 * 8, 22 * 8, 0);
    for (int col = 0; col < kCols; ++col) put(c, t, kFrameTile + tb.top[col], col, 0);
    for (int row = 0; row <= 22; ++row) {
        put(c, t, kFrameTile + tb.left[row], 0, row);
        put(c, t, kFrameTile + tb.right[row], kCols - 1, row);
    }
    for (int col = 0; col < kCols; ++col) put(c, t, kFrameTile + tb.bottom[col], col, 23);
}

void bar(pic::Canvas& c, const Tables& tb, const Tiles& t, int row)
{
    if (tb.chain) {
        chain_bar(c, t, 0, row, kCols - 1, row, 9, 7);      // (img 0x1519: pieces 9 and 7)
        return;
    }
    if (tb.bars[0]) {
        // (the outer verticals get their caps on each side of it)
        hbar(c, tb, t, row);
        for (int col : {0, kCols - 1}) {
            put(c, t, tb.bars[3], col, row - 1);
            put(c, t, tb.bars[1], col, row + 1);
        }
        return;
    }
    if (tb.box[0]) {
        for (int col = 1; col < kCols - 1; ++col) put(c, t, tb.box[2], col, row);
        put(c, t, tb.box[0], 0, row);
        put(c, t, tb.box[0], kCols - 1, row);
        return;
    }
    for (int col = 0; col < kCols; ++col) put(c, t, kFrameTile + tb.bar[col], col, row);
}

void explore(pic::Canvas& c, const Tables& tb, const Tiles& t)
{
    outer(c, tb, t);
    bar(c, tb, t, 16);
    if (tb.chain) {
        chain_bar(c, t, 16, 0, 16, 16, 6, 7);
        chain_box(c, t, 2, 2, 14, 14);
        return;
    }
    if (tb.bars[0]) {
        vbar(c, tb, t, 16, 0, 16);
        view_bars(c, tb, t, 2, 2);
        return;
    }
    if (tb.box[0]) {
        // Pool (screen_facts.md 3.2): the left box and the 3D view's own
        box(c, tb, t, 0, 0, 16, 16);
        box(c, tb, t, 2, 2, 14, 14);
        return;
    }
    for (int row = 0; row <= 16; ++row) put(c, t, kFrameTile + tb.view_split[row], 16, row);
    for (int i = 2; i <= 14; ++i) {
        put(c, t, kViewTile + tb.view_top[i], i, 2);
        put(c, t, kViewTile + tb.view_bottom[i], i, 14);
        put(c, t, kViewTile + tb.view_left[i], 2, i);
        put(c, t, kViewTile + tb.view_right[i], 14, i);
    }
}

void picture_frame(pic::Canvas& c, const Tables& tb, const Tiles& t, int col, int row)
{
    if (tb.chain) {
        chain_box(c, t, col, row, col + 12, row + 12);
        return;
    }
    if (tb.bars[0]) {
        view_bars(c, tb, t, col, row);
        return;
    }
    if (tb.box[0]) {
        box(c, tb, t, col, row, col + 12, row + 12);
        return;
    }
    for (int i = 2; i <= 14; ++i) {
        const int k = i - 2;
        put(c, t, kViewTile + tb.view_top[i], col + k, row);
        put(c, t, kViewTile + tb.view_bottom[i], col + k, row + 12);
        put(c, t, kViewTile + tb.view_left[i], col, row + k);
        put(c, t, kViewTile + tb.view_right[i], col + 12, row + k);
    }
}

void combat(pic::Canvas& c, const Tables& tb, const Tiles& t)
{
    c.fill(0, 0, pic::kScreenW, 24 * 8, 0);
    if (tb.chain) {
        chain_rows(c, t, kChainFight, 5);
        return;
    }
    if (tb.bars[0]) {
        // (Secret's fight screen not seen yet: bars at rows 0 and 22 and
        // verticals at columns 0, 22 and 39, as Curse's)
        for (int row : {0, 22})
            for (int col = 0; col < kCols; ++col) put(c, t, tb.bars[0], col, row);
        for (int col : {0, 22, kCols - 1}) vbar(c, tb, t, col, 0, 22);
        return;
    }
    if (tb.box[0]) {
        // Pool (screen_facts.md 7): the battlefield and the side panel
        box(c, tb, t, 0, 0, 22, 22);
        box(c, tb, t, 22, 0, kCols - 1, 22);
        return;
    }
    for (int col = 0; col < kCols; ++col) put(c, t, kFrameTile + tb.top[col], col, 0);
    for (int row = 0; row <= 22; ++row) {
        put(c, t, kFrameTile + tb.combat_left[row], 0, row);
        put(c, t, kFrameTile + tb.combat_split[row], 22, row);
        put(c, t, kFrameTile + tb.combat_right[row], kCols - 1, row);
    }
    for (int col = 0; col < kCols; ++col) put(c, t, kFrameTile + tb.bottom[col], col, 22);
}

void bars_row(pic::Canvas& c, const Tables& tb, const Tiles& t, int row)
{
    if (tb.bars[0]) hbar(c, tb, t, row);
}

void bars_col(pic::Canvas& c, const Tables& tb, const Tiles& t, int col, int r0, int r1, bool caps)
{
    if (tb.bars[0]) vbar(c, tb, t, col, r0, r1, caps);
}

void credits(pic::Canvas& c, const Tables& tb, const Tiles& t, const uint8_t rows[2])
{
    if (tb.bars[0]) {
        // Secret (secret_facts.md 3): the title on row 0, a bar at rows[0]
        // and row 23, the sides with no caps
        c.fill(0, 0, pic::kScreenW, pic::kScreenH, 0);
        hbar(c, tb, t, rows[0]);
        hbar(c, tb, t, 23);
        vbar(c, tb, t, 0, rows[0], 23, false);
        vbar(c, tb, t, kCols - 1, rows[0], 23, false);
        if (rows[1]) hbar(c, tb, t, rows[1]);
        return;
    }
    if (tb.box[0]) {
        // Pool (screen_facts.md 5): boxes down to row 24
        c.fill(0, 0, pic::kScreenW, pic::kScreenH, 0);
        box(c, tb, t, 0, 0, kCols - 1, 24);
    } else {
        outer(c, tb, t);
    }
    for (int i = 0; i < 2; ++i)
        if (rows[i]) bar(c, tb, t, rows[i]);
}

} // namespace layout

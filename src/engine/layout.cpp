#include "layout.h"

#include <cstring>

namespace layout {

namespace {

constexpr int kCols = pic::kScreenW / 8;    // 40

void put(pic::Canvas& c, const Tiles& t, int n, int col, int row)
{
    if (n < 0 || n >= t.count || !t.loaded) return;
    const uint8_t* s = t.px[n];
    for (int y = 0; y < 8; ++y) {
        const int py = row * 8 + y;
        if (py < 0 || py >= c.h) continue;
        uint8_t* line = c.px + py * c.w;
        for (int x = 0; x < 8; ++x) {
            const int px = col * 8 + x;
            const uint8_t v = s[y * 8 + x];
            if (px >= 0 && px < c.w && v != kMask) line[px] = v;
        }
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

bool load_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Tiles& out)
{
    out.loaded = false;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    uint8_t hdr[pic::kHeaderSize];
    if (r.read(hdr, sizeof hdr) != sizeof hdr) return false;
    pic::Header h;
    if (!pic::parse_header(hdr, e->raw_size, h)) return false;
    if (h.height != 8 || h.width_cols != 1 || h.frames < 23) return false;
    out.count = static_cast<uint8_t>(h.frames < kTiles ? h.frames : kTiles);
    for (int n = 0; n < out.count; ++n) {
        uint8_t packed[32];
        if (r.read(packed, sizeof packed) != sizeof packed) return false;
        for (int i = 0; i < 32; ++i) {
            out.px[n][i * 2] = packed[i] >> 4;
            out.px[n][i * 2 + 1] = packed[i] & 15;
        }
    }
    out.loaded = true;
    return true;
}

Status load_tables(dax::ByteSource& program, const exepack::Info& info, const profile::Profile& p, Tables& out)
{
    out.loaded = false;
    if (p.variant.box[0]) {
        // Pool: boxes, no tables to read
        memcpy(out.box, p.variant.box, sizeof out.box);
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

// Pool: the border of a box whose corners are cells (c0, r0) and (c1, r1)
void box(pic::Canvas& c, const Tables& tb, const Tiles& t, int c0, int r0, int c1, int r1)
{
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

void credits(pic::Canvas& c, const Tables& tb, const Tiles& t, const uint8_t rows[2])
{
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

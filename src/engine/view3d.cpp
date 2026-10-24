#include "view3d.h"

#include <cstdlib>
#include <cstring>

namespace view3d {

namespace {

// The 10 groups of a wall piece: where each starts in the 156 tile numbers,
// its size in cells, and where it goes in the view (row, column)
struct Group {
    uint8_t first, cols, rows;
    int8_t  row, col;
};
const Group kGroups[10] = {
    {0, 1, 2, 4, 5},      // 0 far front
    {2, 1, 4, 3, 4},      // 1 far left side
    {6, 1, 4, 3, 6},      // 2 far right side
    {10, 3, 4, 3, 4},     // 3 middle front
    {22, 2, 8, 1, 2},     // 4 middle left side
    {38, 2, 8, 1, 7},     // 5 middle right side
    {54, 7, 8, 1, 2},     // 6 near front
    {110, 2, 11, 0, 0},   // 7 near left side
    {132, 2, 11, 0, 9},   // 8 near right side
    {154, 1, 2, 4, 5},    // 9 far corner (between two far fronts)
};

constexpr uint8_t kMask = 13;
constexpr int kFrameBase = 0x100;     // tiles 256 on: the frame tiles

void put_packed(pic::Canvas& c, const uint8_t* px, int x0, int y0, uint8_t mask1, uint8_t mask2)
{
    for (int y = 0; y < 8; ++y) {
        const int py = y0 + y;
        if (py < 0 || py >= c.h) continue;
        for (int x = 0; x < 8; ++x) {
            const int pxx = x0 + x;
            const uint8_t b = px[y * 4 + x / 2];
            const uint8_t v = (x & 1) ? (b & 15) : (b >> 4);
            if (v != mask1 && v != mask2 && pxx >= 0 && pxx < c.w) c.set(pxx, py, v);
        }
    }
}

void put_frame_tile(pic::Canvas& c, const layout::Tiles& t, int n, int x0, int y0, uint8_t mask1, uint8_t mask2)
{
    n = t.slot(n);
    if (n < 0 || n >= t.count || !t.loaded) return;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const uint8_t v = t.px[n][y * 8 + x];
            if (v != mask1 && v != mask2) c.set(x0 + x, y0 + y, v);
        }
}

// Treasures' tile pool: the 4-bit tile sets' own memory first (kPoolOwn
// tiles), then tr_more (a VGA screen: tr_more only, 64 bytes a tile)
constexpr int kPoolOwn = 4 * kSetTiles;
int tr_bytes(const World& w) { return w.vga ? 64 : 32; }
const uint8_t* tr_tile(const World& w, int p)
{
    if (!w.vga && p < kPoolOwn) return p < kSetTiles ? w.common.px[p] : w.sets[p / kSetTiles - 1].px[p % kSetTiles];
    const int k = p - (w.vga ? 0 : kPoolOwn);
    return w.tr_more && k < w.tr_more_cap ? w.tr_more + static_cast<size_t>(k) * tr_bytes(w) : nullptr;
}

void draw_tr_tile(pic::Canvas& c, const World& w, int slot, int id, int row, int col)
{
    const int p = w.tr_map[slot][id & 0xFF];
    if (!p) return;
    const uint8_t* px = tr_tile(w, p - 1);
    if (!px) return;
    const int x0 = (kCell0 + col) * 8, y0 = (kCell0 + row) * 8;
    if (!w.vga) {
        put_packed(c, px, x0, y0, kMask, kMask);
        return;
    }
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const uint8_t v = px[y * 8 + x];
            if (v != kMask && x0 + x >= 0 && x0 + x < c.w && y0 + y >= 0 && y0 + y < c.h) c.set(x0 + x, y0 + y, v);
        }
}

// Draws wall piece `type` (1-15) of group g with its top left at view cell
// (row, col)
void set_tile(pic::Canvas& c, const World& w, int set, int id, int row, int col);

void piece(pic::Canvas& c, const World& w, int group, int type, int row, int col)
{
    if (type < 1 || type > 15) return;
    const int set = (type - 1) / kPieces;
    const WallSet& ws = w.walls[set];
    if (!ws.loaded) return;
    const uint8_t* ids = ws.id[(type - 1) % kPieces];
    const Group& g = kGroups[group];
    int i = g.first;
    for (int r = row; r < row + g.rows; ++r)
        for (int cc = col; cc < col + g.cols; ++cc, ++i)
            if (ids[i] && r >= 0 && r < kCells && cc >= 0 && cc < kCells) {
                if (w.per_set) set_tile(c, w, set, ids[i], r, cc);
                else if (w.tr) draw_tr_tile(c, w, w.tr_base[set], ids[i], r, cc);
                else draw_tile(c, w, ids[i], r, cc);
            }
}

int left_of(int dir) { return (dir + 6) & 7; }
int right_of(int dir) { return (dir + 2) & 7; }
bool on_map(int x, int y) { return x >= 0 && x < geo::kSize && y >= 0 && y < geo::kSize; }

// Two squares ahead: fronts along the row (with the corners between them)
// and the side walls
void far(pic::Canvas& c, const World& w, const geo::Map& m, int bx, int by, int dir)
{
    for (int side = 0; side < 2; ++side) {
        const int along = side == 0 ? left_of(dir) : right_of(dir);
        const int other = side == 0 ? right_of(dir) : left_of(dir);
        const int step = side == 0 ? -2 : 2, corner = side == 0 ? 1 : -1;
        int x = bx, y = by, col = 0, prev = 0;
        for (int k = 0; k < 4; ++k) {
            const int t = geo::wall(m, x, y, dir);
            if (!on_map(x, y) && geo::wall(m, x, y, other) == 0) prev = 0;
            if (t) {
                if (prev) piece(c, w, 9, prev, kGroups[9].row, kGroups[9].col + col + corner);
                prev = t;
                piece(c, w, 0, t, kGroups[0].row, kGroups[0].col + col);
            } else {
                if (prev && geo::wall(m, x - geo::dx(along), y - geo::dy(along), along))
                    piece(c, w, 9, prev, kGroups[9].row, kGroups[9].col + col + corner);
                prev = 0;
            }
            col += step;
            x += geo::dx(along);
            y += geo::dy(along);
        }
    }
    for (int side = 0; side < 2; ++side) {
        const int along = side == 0 ? left_of(dir) : right_of(dir);
        const int group = side == 0 ? 1 : 2;
        const int step = side == 0 ? -2 : 2, nudge = side == 0 ? -1 : 1;
        int x = bx, y = by, col = 0;
        for (int k = 0; k < 3; ++k) {
            const int s = geo::wall(m, x, y, along);
            if (s) piece(c, w, group, s, kGroups[group].row, kGroups[group].col + col + (k ? nudge : 0));
            col += step;
            x += geo::dx(along);
            y += geo::dy(along);
        }
    }
}

// One square ahead
void middle(pic::Canvas& c, const World& w, const geo::Map& m, int bx, int by, int dir)
{
    for (int side = 0; side < 2; ++side) {
        const int out = side == 0 ? left_of(dir) : right_of(dir);
        const int back = side == 0 ? right_of(dir) : left_of(dir);
        const int group = side == 0 ? 4 : 5;
        int x = bx + 2 * geo::dx(out), y = by + 2 * geo::dy(out);
        int col = side == 0 ? -6 : 6;
        for (int k = 0; k < 3; ++k) {
            const int t = geo::wall(m, x, y, dir);
            if (t) piece(c, w, 3, t, kGroups[3].row, kGroups[3].col + col);
            const int s = geo::wall(m, x, y, out);
            if (s) piece(c, w, group, s, kGroups[group].row, kGroups[group].col + col);
            col += side == 0 ? 3 : -3;
            x += geo::dx(back);
            y += geo::dy(back);
        }
    }
}

// The party's own square and its neighbours
void near(pic::Canvas& c, const World& w, const geo::Map& m, int bx, int by, int dir)
{
    for (int side = 0; side < 2; ++side) {
        const int out = side == 0 ? left_of(dir) : right_of(dir);
        const int back = side == 0 ? right_of(dir) : left_of(dir);
        const int group = side == 0 ? 7 : 8;
        int x = bx + geo::dx(out), y = by + geo::dy(out);
        int col = side == 0 ? -7 : 7;
        for (int k = 0; k < 2; ++k) {
            const int t = geo::wall(m, x, y, dir);
            if (t) piece(c, w, 6, t, kGroups[6].row, kGroups[6].col + col);
            const int s = geo::wall(m, x, y, out);
            if (s) piece(c, w, group, s, kGroups[group].row, kGroups[group].col + col);
            col += side == 0 ? 7 : -7;
            x += geo::dx(back);
            y += geo::dy(back);
        }
    }
}

} // namespace

void free_extra(TileSet& t)
{
    free(t.extra);
    t.extra = nullptr;
    t.extra_count = 0;
}

bool load_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, TileSet& out, bool big)
{
    out.count = 0;
    free_extra(out);
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    uint8_t hdr[pic::kHeaderSize];
    pic::Header h;
    if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_header(hdr, e->raw_size, h)) return false;
    if (h.height != 8 || h.width_cols != 1 || h.frames < 1) return false;
    const int n = h.frames < kSetTiles ? h.frames : kSetTiles;
    uint8_t map[16];
    if (!pic::begin_frames(r, h, map)) return false;
    for (int i = 0; i < n; ++i)
        if (!pic::next_frame(r, h, map, out.px[i])) return false;
    out.count = n;
    if (big && h.frames > kSetTiles) {
        const int more = h.frames - kSetTiles;
        out.extra = static_cast<uint8_t(*)[32]>(malloc(static_cast<size_t>(more) * 32));
        if (!out.extra) return true;            // (the walls' far tiles left out)
        for (int i = 0; i < more; ++i)
            if (!pic::next_frame(r, h, map, out.extra[i])) break;
            else out.extra_count = i + 1;
    }
    return true;
}

int tiles_block(uint8_t walldef_block, int n, int i)
{
    // (Pool's loader: a multi-part block 0 counts as 10 - Phlan's area 3,
    // WALLDEF3 block 0's tiles are 8X8D3 101-103; v0.104.2)
    if (n == 1) return walldef_block;
    return (walldef_block == 0 ? 10 : walldef_block) * 10 + i + 1;
}

bool load_walls(dax::ByteSource& src, const dax::Index& idx, int set, uint8_t block, World& w, int* count)
{
    if (count) *count = 0;
    if (set < 1 || set > 3) return false;
    const dax::Entry* e = idx.find(block);
    if (!e || e->raw_size == 0 || e->raw_size % kWallSetBytes != 0) return false;
    const int n = static_cast<int>(e->raw_size / kWallSetBytes);
    if (set + n - 1 > 3) return false;
    dax::RleReader r(src, idx, *e);
    for (int i = 0; i < n; ++i) {
        WallSet& ws = w.walls[set - 1 + i];
        if (r.read(&ws.id[0][0], kWallSetBytes) != kWallSetBytes) return false;
        // Numbers from 45 up are this set's own tiles: move them to its range.
        // Every part of a block moves by its first set's amount - a 2- or
        // 3-part block's later parts already number their tiles after the
        // first part's (as the games do it). Darkness: the numbers are the
        // tile block's own (no moves)
        const int shift = w.dk || w.per_set ? 0 : (set - 1) * kSetTiles;
        for (int p = 0; p < kPieces; ++p)
            for (int k = 0; k < kPieceTiles; ++k)
                if (ws.id[p][k] >= kCommonTiles) ws.id[p][k] = static_cast<uint8_t>(ws.id[p][k] + shift);
        ws.loaded = true;
    }
    if (count) *count = n;
    return true;
}

bool load_horizon(dax::ByteSource& src, const dax::Index& idx, uint8_t block, World& w)
{
    w.has_horizon = false;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    uint8_t hdr[pic::kHeaderSize];
    if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_header(hdr, e->raw_size, w.horizon_hdr)) return false;
    if (w.horizon_hdr.frame_bytes() > sizeof w.horizon) return false;
    if (!pic::read_frame(r, w.horizon_hdr, 0, w.horizon)) return false;
    w.has_horizon = true;
    return true;
}

bool load_tr_walls(dax::ByteSource& src, const dax::Index& idx, int slot, int block, World& w)
{
    if (slot < 0 || slot > 2) return false;
    w.tr = w.dk = true;
    if (block < 0) {
        // (the slot emptied: its walls stay, its tiles go - ov43+0xFC0)
        w.tr_block[slot] = 0;
        return true;
    }
    const dax::Entry* e = idx.find(static_cast<uint8_t>(block));
    if (!e || e->raw_size == 0 || e->raw_size % kWallSetBytes != 0) return false;
    const int n = static_cast<int>(e->raw_size / kWallSetBytes);
    dax::RleReader r(src, idx, *e);
    // (ovr 0x48926: sets 0, 1 ... into slots slot, slot + 1 ... while there are slots)
    for (int i = 0; i < n && slot + i < 3; ++i) {
        WallSet& ws = w.walls[slot + i];
        if (r.read(&ws.id[0][0], kWallSetBytes) != kWallSetBytes) return false;
        ws.loaded = true;
        w.tr_base[slot + i] = static_cast<uint8_t>(slot);
    }
    w.tr_block[slot] = static_cast<uint8_t>(block);
    return true;
}

void tr_pool_reset(World& w)
{
    memset(w.tr_map, 0, sizeof w.tr_map);
    w.tr_used = 0;
}

void tr_free(World& w)
{
    free(w.tr_more);
    w.tr_more = nullptr;
    w.tr_more_cap = 0;
    tr_pool_reset(w);
}

void back_free(World& w)
{
    free(w.back);
    w.back = nullptr;
    w.back_id = 0;
}

void vga_free(World& w)
{
    back_free(w);
    tr_free(w);
    free(w.vga_px);
    w.vga_px = nullptr;
}

namespace {
// A VGA block's header, its palette (into pal; nullptr: skipped) and its
// EGA nibbles read into map (vga: map = the values themselves)
bool vga_start(dax::RleReader& r, const dax::Entry& e, pic::VgaHeader& h, uint8_t map[256], pic::VgaPalette* pal,
               bool vga)
{
    uint8_t hdr[pic::kVgaHeaderSize];
    for (int i = 0; i < 256; ++i) map[i] = static_cast<uint8_t>(i & 15);
    if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_vga_header(hdr, e.raw_size, h)) return false;
    if (pal ? !pic::read_vga_palette(r, h, pal->rgb) : r.skip(h.count * 3u) != h.count * 3u) return false;
    if (pal) {
        pal->first = h.first;
        pal->count = h.count;
    }
    if (!pic::read_vga_ega(r, h, map)) return false;
    if (vga)
        for (int i = 0; i < 256; ++i) map[i] = static_cast<uint8_t>(i);
    return
           r.skip(h.pixels_at - (pic::kVgaHeaderSize + h.count * 3u + (h.count + 1) / 2u)) ==
               h.pixels_at - (pic::kVgaHeaderSize + h.count * 3u + (h.count + 1) / 2u);
}
} // namespace

bool load_vga_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, World& w, pic::VgaPalette* pal)
{
    w.dk = true;
    w.common.count = 0;
    for (auto& ts : w.sets) ts.count = 0;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    if (w.vga && !w.vga_px) {
        w.vga_px = static_cast<uint8_t*>(malloc(kVgaTiles * 64));
        if (!w.vga_px) return false;
    }
    dax::RleReader r(src, idx, *e);
    pic::VgaHeader h;
    uint8_t map[256];
    if (!vga_start(r, *e, h, map, pal, w.vga) || h.height != 8 || h.width_cols != 1) return false;
    for (int t = 0; t < h.frames && t < 256; ++t) {
        uint8_t px[64];
        if (r.read(px, sizeof px) != sizeof px) return false;
        if (t == 0) continue;                       // (tile 0: never drawn)
        if (w.vga) {
            // (a byte a pixel; the counts as for the 4-bit sets)
            memcpy(w.vga_px + (t - 1) * 64, px, 64);
            if (t <= kCommonTiles) {
                w.common.count = t;
            } else {
                const int k = t - (kCommonTiles + 1);
                w.sets[k / kSetTiles].count = k % kSetTiles + 1;
            }
            continue;
        }
        uint8_t* out;
        if (t <= kCommonTiles) {
            out = w.common.px[t - 1];
            w.common.count = t;
        } else {
            const int k = t - (kCommonTiles + 1);
            TileSet& ts = w.sets[k / kSetTiles];
            out = ts.px[k % kSetTiles];
            ts.count = k % kSetTiles + 1;
        }
        for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(map[px[i * 2]] << 4 | map[px[i * 2 + 1]]);
    }
    return true;
}

bool load_ega_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, World& w)
{
    w.dk = true;
    w.common.count = 0;
    for (auto& ts : w.sets) ts.count = 0;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    uint8_t hdr[pic::kHeaderSize];
    if (r.read(hdr, sizeof hdr) != sizeof hdr || hdr[0] != 8 || hdr[1] != 0 || hdr[2] != 1 || hdr[3] != 0) return false;
    const int frames = static_cast<int>((e->raw_size - pic::kHeaderSize) / 32);
    for (int t = 0; t < frames && t < 256; ++t) {
        uint8_t px[32];
        if (r.read(px, sizeof px) != sizeof px) return false;
        if (t == 0) continue;                       // (tile 0: never drawn)
        uint8_t* out;
        if (t <= kCommonTiles) {
            out = w.common.px[t - 1];
            w.common.count = t;
        } else {
            const int k = t - (kCommonTiles + 1);
            TileSet& ts = w.sets[k / kSetTiles];
            out = ts.px[k % kSetTiles];
            ts.count = k % kSetTiles + 1;
        }
        memcpy(out, px, sizeof px);
    }
    return true;
}

bool load_ega_back(dax::ByteSource& src, const dax::Index& idx, uint8_t block, uint8_t id, World& w, bool over)
{
    if (w.back && w.back_id == id) return true;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    uint8_t hdr[pic::kHeaderSize];
    pic::Header h;
    if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_header(hdr, e->raw_size, h) ||
        h.width_px() != kCells * 8 || h.height > kCells * 8)
        return false;
    const int half = kCells * 4;                    // (bytes a row: two pixels a byte)
    if (!w.back) w.back = static_cast<uint8_t*>(malloc(kCells * 8 * half));
    if (!w.back) return false;
    if (over && !w.back_id) return false;
    w.back_id = 0;
    uint8_t row[kCells * 4];
    for (int y = 0; y < h.height; ++y) {
        if (r.read(row, sizeof row) != sizeof row) return false;
        uint8_t* out = w.back + y * half;
        if (!over) {
            memcpy(out, row, sizeof row);
            continue;
        }
        for (int i = 0; i < half; ++i) {
            const uint8_t hi = row[i] >> 4, lo = row[i] & 15;
            if (hi != kMask) out[i] = static_cast<uint8_t>((out[i] & 0x0F) | hi << 4);
            if (lo != kMask) out[i] = static_cast<uint8_t>((out[i] & 0xF0) | lo);
        }
    }
    w.back_id = id;
    return true;
}

bool load_tr_tiles(dax::ByteSource& src, const dax::Index& idx, int slot, uint8_t block, World& w, pic::VgaPalette* pal)
{
    if (slot < 0 || slot > 2) return false;
    // The tiles the slot's walls use
    bool want[256] = {};
    int n = 0;
    for (int k = 0; k < 3; ++k) {
        if (!w.walls[k].loaded || w.tr_base[k] != slot) continue;
        for (int p = 0; p < kPieces; ++p)
            for (int i = 0; i < kPieceTiles; ++i) {
                const uint8_t id = w.walls[k].id[p][i];
                if (id && !want[id]) {
                    want[id] = true;
                    ++n;
                }
            }
    }
    memset(w.tr_map[slot], 0, sizeof w.tr_map[slot]);
    if (!n) return true;
    // Room in the pool
    const int own = w.vga ? 0 : kPoolOwn;
    const int more = w.tr_used + n - own;
    if (more > w.tr_more_cap) {
        uint8_t* m = static_cast<uint8_t*>(realloc(w.tr_more, static_cast<size_t>(more) * tr_bytes(w)));
        if (!m) return false;
        w.tr_more = m;
        w.tr_more_cap = static_cast<uint16_t>(more);
    }
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    pic::VgaHeader h;
    uint8_t map[256];
    if (!vga_start(r, *e, h, map, pal, w.vga) || h.height != 8 || h.width_cols != 1) return false;
    for (int t = 0; t < h.frames && t < 256; ++t) {
        uint8_t px[64];
        if (r.read(px, sizeof px) != sizeof px) return false;
        if (!want[t]) continue;
        const int p = w.tr_used++;
        w.tr_map[slot][t] = static_cast<uint16_t>(p + 1);
        uint8_t* out = const_cast<uint8_t*>(tr_tile(w, p));
        if (!out) return false;
        if (w.vga) memcpy(out, px, 64);
        else
            for (int i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(map[px[i * 2]] << 4 | map[px[i * 2 + 1]]);
    }
    return true;
}

bool draw_vga_window(dax::ByteSource& src, const dax::Index& idx, uint8_t block, pic::Canvas& c, int sx, int sy, int w,
                     int h, int dx, int dy, bool vga, pic::VgaPalette* pal)
{
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    pic::VgaHeader hd;
    uint8_t map[256];
    if (!vga_start(r, *e, hd, map, pal, vga)) return false;
    const int pw = hd.width_px();
    if (sx < 0 || sy < 0 || sx + w > pw || sy + h > hd.height) return false;
    if (r.skip(static_cast<size_t>(sy) * pw) != static_cast<size_t>(sy) * pw) return false;
    uint8_t row[320];
    if (pw > static_cast<int>(sizeof row)) return false;
    for (int y = 0; y < h; ++y) {
        if (r.read(row, static_cast<size_t>(pw)) != static_cast<size_t>(pw)) return false;
        for (int x = 0; x < w; ++x) c.set(dx + x, dy + y, map[row[sx + x]]);
    }
    return true;
}

void draw_marker(pic::Canvas& c, const World& w, int n, int x, int y)
{
    if (w.frame) put_frame_tile(c, *w.frame, n, x, y, 8, kMask);
}

bool load_back(dax::ByteSource& src, const dax::Index& idx, uint8_t block, uint8_t id, World& w, pic::VgaPalette* pal)
{
    if (w.back && w.back_id == id) return true;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    pic::VgaHeader h;
    uint8_t map[256];
    if (!vga_start(r, *e, h, map, pal, w.vga) || h.height != kCells * 8 || h.width_px() != kCells * 8) return false;
    if (!w.back) w.back = static_cast<uint8_t*>(malloc(kCells * 8 * kCells * (w.vga ? 8 : 4)));
    if (!w.back) return false;
    w.back_id = 0;
    if (w.vga) {
        // (a byte a pixel)
        if (r.read(w.back, kCells * 8 * kCells * 8) != kCells * 8 * kCells * 8) return false;
        w.back_id = id;
        return true;
    }
    for (int i = 0; i < kCells * 8 * kCells * 4; ++i) {
        const int a = r.next(), b = r.next();
        if (a < 0 || b < 0) return false;
        w.back[i] = static_cast<uint8_t>(map[a] << 4 | map[b]);
    }
    w.back_id = id;
    return true;
}

void draw_tile(pic::Canvas& c, const World& w, int id, int row, int col)
{
    const int x0 = (kCell0 + col) * 8, y0 = (kCell0 + row) * 8;
    if (w.vga_px && id >= 1 && id < kFrameBase) {
        // (a VGA screen: the values, 13 see-through - darkness_facts.md 7.2)
        const int k = id - (kCommonTiles + 1);
        if (id <= kCommonTiles ? id - 1 >= w.common.count : k % kSetTiles >= w.sets[k / kSetTiles].count) return;
        const uint8_t* p = w.vga_px + (id - 1) * 64;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                const uint8_t v = p[y * 8 + x];
                if (v != kMask && x0 + x >= 0 && x0 + x < c.w && y0 + y >= 0 && y0 + y < c.h) c.set(x0 + x, y0 + y, v);
            }
    } else if (id >= 1 && id <= kCommonTiles) {
        if (id - 1 < w.common.count) put_packed(c, w.common.px[id - 1], x0, y0, kMask, kMask);
    } else if (id > kCommonTiles && id < kFrameBase) {
        const int k = id - (kCommonTiles + 1);
        const TileSet& ts = w.sets[k / kSetTiles];
        if (k % kSetTiles < ts.count) put_packed(c, ts.px[k % kSetTiles], x0, y0, kMask, kMask);
    } else if (id >= kFrameBase && w.frame) {
        put_frame_tile(c, *w.frame, id - kFrameBase, x0, y0, kMask, kMask);
    }
}

namespace {

// Gateway: a wall set's tile (46 on: its own, from 0; 1-45 the common ones)
void set_tile(pic::Canvas& c, const World& w, int set, int id, int row, int col)
{
    if (id <= kCommonTiles) {
        draw_tile(c, w, id, row, col);
        return;
    }
    const TileSet& ts = w.sets[set];
    const int k = id - (kCommonTiles + 1);
    const int x0 = (kCell0 + col) * 8, y0 = (kCell0 + row) * 8;
    if (k < kSetTiles) {
        if (k < ts.count) put_packed(c, ts.px[k], x0, y0, kMask, kMask);
    } else if (k - kSetTiles < ts.extra_count) {
        put_packed(c, ts.extra[k - kSetTiles], x0, y0, kMask, kMask);
    }
}

void walls(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir)
{
    dir &= 6;
    // Far to near: two squares ahead, one, then the party's own row
    for (int step = 2; step >= 0; --step) {
        const int bx = x + step * geo::dx(dir), by = y + step * geo::dy(dir);
        if (step == 2) far(c, w, m, bx, by, dir);
        else if (step == 1) middle(c, w, m, bx, by, dir);
        else near(c, w, m, bx, by, dir);
    }
}

} // namespace

void draw(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir, uint8_t sky)
{
    const int x0 = kCell0 * 8, y0 = kCell0 * 8, wpx = kCells * 8;
    if (w.dk && w.back && w.vga) {
        for (int row = 0; row < wpx; ++row) c.put_row(x0, y0 + row, wpx, w.back + row * wpx);
        walls(c, w, m, x, y, dir);
        return;
    }
    if (w.dk && w.back) {
        // Darkness: the background picture, then the walls over it
        for (int row = 0; row < wpx; ++row)
            for (int col = 0; col < wpx; ++col) {
                const uint8_t b = w.back[row * (wpx / 2) + col / 2];
                c.set(x0 + col, y0 + row, (col & 1) ? (b & 15) : (b >> 4));
            }
        walls(c, w, m, x, y, dir);
        return;
    }
    c.fill(x0, y0, wpx, 44, sky);
    c.fill(x0, y0 + 44, wpx, 2, 0);
    c.fill(x0, y0 + 46, wpx, 42, 8);
    if (w.has_horizon) pic::draw_pixels(w.horizon, w.horizon_hdr, c, x0, 64, kMask);
    walls(c, w, m, x, y, dir);
}

Bands pool_bands(uint8_t sky, uint8_t floor_byte, uint8_t line_byte)
{
    Bands b{sky, 15, 7, 6};
    if (line_byte == 0x08 || line_byte == 0x41) b.line = 0;
    if (line_byte == 0x08) b.band = 8;
    if (floor_byte == 0x0B) b.floor = 8;
    else if (floor_byte == 0x06) b.floor = 1;
    return b;
}

void draw(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir, const Bands& b)
{
    const int x0 = kCell0 * 8, y0 = kCell0 * 8, wpx = kCells * 8;
    c.fill(x0, y0, wpx, 43, b.sky);
    c.fill(x0, y0 + 43, wpx, 1, b.line);
    c.fill(x0, y0 + 44, wpx, 2, b.band);
    c.fill(x0, y0 + 46, wpx, 42, b.floor);
    walls(c, w, m, x, y, dir);
}

void draw_area_map(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir)
{
    if (!w.frame) return;
    auto clamp = [](int v) { return v < 0 ? 0 : v > 5 ? 5 : v; };
    const int ox = clamp(x - 5), oy = clamp(y - 5);
    c.fill(kCell0 * 8, kCell0 * 8, kCells * 8, kCells * 8, 0);
    for (int r = 0; r < kCells; ++r)
        for (int col = 0; col < kCells; ++col) {
            const int mx = col + ox, my = r + oy;
            int bits = 0;
            if (geo::wall(m, mx, my, 0)) bits |= 1;
            if (geo::wall(m, mx, my, 2)) bits |= 2;
            if (geo::wall(m, mx, my, 4)) bits |= 4;
            if (geo::wall(m, mx, my, 6)) bits |= 8;
            // (Darkness: walls in frames 0-15, the arrows 16-19)
            put_frame_tile(c, *w.frame, (w.map_base ? w.map_base : w.dk ? 0 : 4) + bits, (kCell0 + col) * 8,
                           (kCell0 + r) * 8, kMask, kMask);
        }
    put_frame_tile(c, *w.frame, (w.map_base ? w.map_base + 16 : w.dk ? 16 : 0) + ((dir & 7) >> 1),
                   (kCell0 + x - ox) * 8, (kCell0 + y - oy) * 8, 8, kMask);
}

} // namespace view3d

#include "view3d.h"

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
            if (v != mask1 && v != mask2 && pxx >= 0 && pxx < c.w) c.px[py * c.w + pxx] = v;
        }
    }
}

void put_frame_tile(pic::Canvas& c, const layout::Tiles& t, int n, int x0, int y0, uint8_t mask1, uint8_t mask2)
{
    if (n < 0 || n >= layout::kTiles || !t.loaded) return;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            const uint8_t v = t.px[n][y * 8 + x];
            if (v != mask1 && v != mask2) c.px[(y0 + y) * c.w + x0 + x] = v;
        }
}

// Draws wall piece `type` (1-15) of group g with its top left at view cell
// (row, col)
void piece(pic::Canvas& c, const World& w, int group, int type, int row, int col)
{
    if (type < 1 || type > 15) return;
    const WallSet& ws = w.walls[(type - 1) / kPieces];
    if (!ws.loaded) return;
    const uint8_t* ids = ws.id[(type - 1) % kPieces];
    const Group& g = kGroups[group];
    int i = g.first;
    for (int r = row; r < row + g.rows; ++r)
        for (int cc = col; cc < col + g.cols; ++cc, ++i)
            if (ids[i] && r >= 0 && r < kCells && cc >= 0 && cc < kCells) draw_tile(c, w, ids[i], r, cc);
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

bool load_tiles(dax::ByteSource& src, const dax::Index& idx, uint8_t block, TileSet& out)
{
    out.count = 0;
    const dax::Entry* e = idx.find(block);
    if (!e) return false;
    dax::RleReader r(src, idx, *e);
    uint8_t hdr[pic::kHeaderSize];
    pic::Header h;
    if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_header(hdr, e->raw_size, h)) return false;
    if (h.height != 8 || h.width_cols != 1 || h.frames < 1) return false;
    const int n = h.frames < kSetTiles ? h.frames : kSetTiles;
    for (int i = 0; i < n; ++i)
        if (r.read(out.px[i], 32) != 32) return false;
    out.count = n;
    return true;
}

int tiles_block(uint8_t walldef_block, int n, int i)
{
    return n == 1 ? walldef_block : walldef_block * 10 + i + 1;
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
        // Numbers from 45 up are this set's own tiles: move them to its range
        const int shift = (set - 1 + i) * kSetTiles;
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
    if (r.read(w.horizon, w.horizon_hdr.frame_bytes()) != w.horizon_hdr.frame_bytes()) return false;
    w.has_horizon = true;
    return true;
}

void draw_tile(pic::Canvas& c, const World& w, int id, int row, int col)
{
    const int x0 = (kCell0 + col) * 8, y0 = (kCell0 + row) * 8;
    if (id >= 1 && id <= kCommonTiles) {
        if (id - 1 < w.common.count) put_packed(c, w.common.px[id - 1], x0, y0, kMask, kMask);
    } else if (id > kCommonTiles && id < kFrameBase) {
        const int k = id - (kCommonTiles + 1);
        const TileSet& ts = w.sets[k / kSetTiles];
        if (k % kSetTiles < ts.count) put_packed(c, ts.px[k % kSetTiles], x0, y0, kMask, kMask);
    } else if (id >= kFrameBase && w.frame) {
        put_frame_tile(c, *w.frame, id - kFrameBase, x0, y0, kMask, kMask);
    }
}

void draw(pic::Canvas& c, const World& w, const geo::Map& m, int x, int y, int dir, uint8_t sky)
{
    const int x0 = kCell0 * 8, y0 = kCell0 * 8, wpx = kCells * 8;
    c.fill(x0, y0, wpx, 44, sky);
    c.fill(x0, y0 + 44, wpx, 2, 0);
    c.fill(x0, y0 + 46, wpx, 42, 8);
    if (w.has_horizon) pic::draw_pixels(w.horizon, w.horizon_hdr, c, x0, 64, kMask);
    dir &= 6;
    // Far to near: two squares ahead, one, then the party's own row
    for (int step = 2; step >= 0; --step) {
        const int bx = x + step * geo::dx(dir), by = y + step * geo::dy(dir);
        if (step == 2) far(c, w, m, bx, by, dir);
        else if (step == 1) middle(c, w, m, bx, by, dir);
        else near(c, w, m, bx, by, dir);
    }
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
            put_frame_tile(c, *w.frame, 4 + bits, (kCell0 + col) * 8, (kCell0 + r) * 8, kMask, kMask);
        }
    put_frame_tile(c, *w.frame, (dir & 7) >> 1, (kCell0 + x - ox) * 8, (kCell0 + y - oy) * 8, 8, kMask);
}

} // namespace view3d

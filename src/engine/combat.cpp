#include "combat.h"

#include <cstring>

namespace combat {

namespace {

constexpr int kDx[9] = {0, 1, 1, 1, 0, -1, -1, -1, 0};
constexpr int kDy[9] = {-1, -1, 0, 1, 1, 1, 0, -1, 0};

constexpr uint8_t kPlainFloor = 22;    // ground value - 1 ("plain floor" picture)
constexpr uint8_t kTable = 0x1A, kChair = 0x1B, kBody = 0x1F;

// Record fields
constexpr int kHp = 0x1A4, kHpMax = 0x78, kHealth = 0x195, kInCombat = 0x196, kTeam = 0x197;
constexpr int kSize = 0xDE, kDexFull = 0x17, kMove = 0x1A5, kHalf1 = 0x11C, kHalf2 = 0x11D;
constexpr int kHit = 0x199, kAc = 0x19A, kAcBehind = 0x19B;
constexpr int kDice = 0x19E, kSides = 0x1A0, kBonus = 0x1A2;
constexpr int kControl = 0xF7;
constexpr int kHd = 0xE5, kFighterLevel = 0x10B, kThiefLevel = 0x10F;


// ---- Indoors: a map square's side codes
struct Sides {
    const geo::Map* m;
    int px, py;
    // 0 open, 1 wall, 3 door (one square's own side)
    int code(int x, int y, int dir) const
    {
        if (x < 0 || y < 0 || x >= geo::kSize || y >= geo::kSize)
            return y == py && (dir == 2 || dir == 6) ? 0 : 1;
        if (geo::wall(*m, x, y, dir) == 0) return 0;
        return geo::door(*m, x, y, dir) == 0 ? 1 : 3;
    }
    // Both sides of the edge
    int flag(int x, int y, int dir) const
    {
        return code(x, y, dir) | code(x + geo::dx(dir), y + geo::dy(dir), (dir + 4) & 7);
    }
};

struct Painter {
    Battle& b;
    int x0, y0;
    void set(int c, int r, int t) const
    {
        const int x = x0 + c, y = y0 + r;
        if (x >= 0 && x < kW && y >= 0 && y < kH) b.ground[y][x] = static_cast<uint8_t>(t + 1);
    }
    int get(int c, int r) const
    {
        const int x = x0 + c, y = y0 + r;
        return x >= 0 && x < kW && y >= 0 && y < kH ? b.ground[y][x] - 1 : -1;
    }
};

void draw_block(Battle& b, const Sides& s, int mx, int my, int ddx, int ddy, create::Dice& d)
{
    const Painter p{b, 21 + 6 * ddx + 5 * ddy, 10 + 5 * ddy};
    const int N = s.flag(mx, my, 0), E = s.flag(mx, my, 2), S = s.flag(mx, my, 4), W = s.flag(mx, my, 6);
    // 1. The floor and the west edge
    for (int r = 2; r <= 4; ++r)
        for (int c = 0; c <= 5; ++c) p.set(c, r, kPlainFloor);
    if (W == 1) {
        for (int r = 2; r <= 4; ++r) {
            p.set(r - 1, r, 4);
            p.set(r, r, 3);
            p.set(r + 1, r, 13);
        }
    } else if (W == 3) {
        p.set(1, 2, 8);
        p.set(5, 4, 0);
    }
    // 2. The north edge's middle
    for (int c = 3; c <= 4; ++c) {
        p.set(c, 0, N == 1 ? 5 : kPlainFloor);
        p.set(c, 1, N == 1 ? 10 : kPlainFloor);
    }
    // 3. The north-west corner
    const bool X = s.flag(mx, my - 1, 6) == 0 && s.flag(mx - 1, my, 0) == 0;
    int t;
    if (N == 0) t = W == 0 ? kPlainFloor : W == 3 ? 13 : (X ? 0 : 13);
    else t = W == 0 ? (X ? 15 : 5) : (X ? 18 : 2);
    p.set(1, 0, t);
    p.set(2, 0, N == 0 ? kPlainFloor : N == 3 ? 17 : 5);
    if (W == 0) t = N == 0 ? kPlainFloor : (X ? 16 : 10);
    else if (W == 3) t = X ? 20 : 7;
    else t = X ? 1 : 3;
    p.set(1, 1, t);
    if (W == 1) t = N == 0 ? 13 : N == 3 ? 21 : 6;
    else t = N == 0 ? kPlainFloor : N == 3 ? 23 : 10;
    p.set(2, 1, t);
    // 4. The north-east corner
    const int P = s.flag(mx, my - 1, 2), Q = s.flag(mx + 1, my, 0);
    p.set(5, 0, N == 0 ? (P == 1 ? 4 : kPlainFloor) : N == 3 ? 15 : 5);
    if (N == 0) {
        if (P == 0) t = kPlainFloor;
        else if (P == 3) t = E == 0 && Q != 0 ? 24 : 1;
        else t = E != 0 ? 3 : (Q != 0 ? 11 : 7);
    } else {
        t = E != 0 ? 9 : Q != 0 ? 5 : P == 0 ? 17 : 19;
    }
    p.set(6, 0, t);
    p.set(5, 1, N == 0 ? kPlainFloor : N == 3 ? 16 : 10);
    if (N == 0) t = P == 0 ? kPlainFloor : E != 0 ? 4 : Q == 0 ? 8 : 12;
    else t = E != 0 ? 14 : Q == 0 ? 23 : 10;
    p.set(6, 1, t);
    // 5. Tables and chairs in rooms
    if (mx >= 0 && my >= 0 && mx < geo::kSize && my < geo::kSize && (geo::flags(*s.m, mx, my) & 0x40)) {
        const int walls = (N == 1) + (E == 1) + (S == 1) + (W == 1);
        bool room = true;
        if (walls == 0) room = false;
        if (N == 1 && S == 1 && !(E == 1 && W == 1)) room = false;
        if (E == 1 && W == 1 && !(N == 1 && S == 1)) room = false;
        if (N == 3 || E == 3 || S == 3 || W == 3) room = false;
        if (room)
            for (int a = 2; a <= 3; ++a)
                for (int bb = 2; bb <= 4; ++bb) {
                    const int c = a + bb, r = bb;
                    if (p.get(c, r) != kPlainFloor || d.roll(10, 1) > 5) continue;
                    p.set(c, r, kTable - 1);
                    // Chairs round it (the neighbours that are plain floor)
                    for (int dir = 0; dir < 8; dir += 2)
                        if (p.get(c + kDx[dir], r + kDy[dir]) == kPlainFloor && d.roll(10, 1) <= 9)
                            p.set(c + kDx[dir], r + kDy[dir], kChair - 1);
                }
    }
}

bool on_field(int x, int y) { return x >= 0 && x < kW && y >= 0 && y < kH; }

} // namespace

int dx(int dir) { return dir >= 0 && dir <= 8 ? kDx[dir] : 0; }
int dy(int dir) { return dir >= 0 && dir <= 8 ? kDy[dir] : 0; }

bool read_tables(Tables& t, const TableAt& at, ReadDs read, void* ctx)
{
    return read(ctx, at.ground, &t.ground[0][0], sizeof t.ground) &&
           read(ctx, at.fallback, &t.fallback[0][0], sizeof t.fallback) &&
           read(ctx, at.formation, &t.formation[0][0], sizeof t.formation) &&
           read(ctx, at.facing, t.facing, sizeof t.facing) && read(ctx, at.start_x, t.start_x, sizeof t.start_x) &&
           read(ctx, at.start_y, t.start_y, sizeof t.start_y) &&
           read(ctx, at.shapes, &t.shapes[0][0][0], sizeof t.shapes) &&
           read(ctx, at.terrain, t.terrain, sizeof t.terrain) && read(ctx, at.turn, t.turn, sizeof t.turn);
}

bool Fighter::has(uint8_t type) const
{
    for (int i = 0; n_aff && i < *n_aff; ++i)
        if (aff[i][0] == type) return true;
    return false;
}

int squares(int size, int* sx, int* sy)
{
    switch (size) {
    case 2: sx[0] = 0; sy[0] = 0; sx[1] = 0; sy[1] = 1; return 2;
    case 3: sx[0] = 0; sy[0] = 0; sx[1] = 1; sy[1] = 0; return 2;
    case 4:
        sx[0] = 0; sy[0] = 0; sx[1] = 1; sy[1] = 0; sx[2] = 0; sy[2] = 1; sx[3] = 1; sy[3] = 1;
        return 4;
    case 0: return 0;
    default: sx[0] = 0; sy[0] = 0; return 1;
    }
}

void build_indoors(Battle& b, const Tables& t, const geo::Map& m, int px, int py, create::Dice& d)
{
    (void)t;
    memset(b.ground, 0, sizeof b.ground);
    b.indoors = true;
    const Sides s{&m, px, py};
    for (int ddy = -2; ddy <= 2; ++ddy)
        for (int ddx = -6; ddx <= 6; ++ddx) draw_block(b, s, px + ddx, py + ddy, ddx, ddy, d);
}

void build_outdoors(Battle& b, const Tables& t, int city, create::Dice& d)
{
    b.indoors = false;
    const uint8_t plain = 0x36 + 1;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) b.ground[y][x] = plain;
    const int f = city >= 0 && city < 33 ? t.terrain[city] : 0;
    // A stream running down and to the right
    const int chance = (f & 0x10) ? 75 : (f & 0x20) ? 35 : 0;
    if (chance && d.roll(100, 1) <= chance) {
        int col = 34 - 5 * d.roll(4, 1);
        while ((col + 2) % 7 != 0) --col;
        for (int y = 0; y < kH; ++y, ++col) {
            if (col >= 0 && col < kW) b.ground[y][col] = static_cast<uint8_t>(0x3C + d.roll(2, 1) - 1);
            if (col + 1 >= 0 && col + 1 < kW) {
                b.ground[y][col + 1] = static_cast<uint8_t>(0x3E + d.roll(2, 1) - 1);
                if (d.roll(20, 1) == 1) {
                    b.ground[y][col + 1] = 0x40;
                    if (y + 1 < kH) b.ground[y + 1][col + 1] = 0x41;
                }
            }
        }
    }
    // Trees and logs
    if (!(f & 0x80)) {
        int n = 10;
        if (f & 0x02) n -= 5;
        if (f & 0x04) n -= 2;
        if (f & 0x40) n += 5;
        if (f & 0x08) n += 10;
        if (n < 0) n = 1;
        for (int y = 1; y < kH; ++y)
            for (int x = 0; x < kW; ++x) {
                if (b.ground[y][x] != plain || b.ground[y - 1][x] != plain || d.roll(100, 1) > n) continue;
                if (d.roll(100, 1) <= n) {
                    b.ground[y][x] = static_cast<uint8_t>(0x2A + d.roll(2, 1) - 1);
                } else {
                    b.ground[y - 1][x] = static_cast<uint8_t>(0x20 + d.roll(5, 1) - 1);
                    b.ground[y][x] = static_cast<uint8_t>(0x25 + d.roll(5, 1) - 1);
                }
            }
    }
    // Scattered ground: ponds, rocks, grass, pebbles
    int D = 50;
    if (f & 0x10) D += 10;
    if (f & 0x20) D += 30;
    if (f & 0x40) D += 20;
    if (f & 0x04) D -= 10;
    if (f & 0x02) D -= 20;
    if (f & 0x80) D -= 50;
    static const uint8_t kBand[5][5] = {{0, 0, 0, 30, 15}, {0, 1, 5, 14, 10}, {0, 2, 5, 10, 5}, {10, 2, 10, 10, 1},
                                        {15, 5, 15, 10, 1}};
    int band = -1;
    if (D >= -30 && D <= 9) band = 0;
    else if (D >= 10 && D <= 29) band = 1;
    else if (D >= 30 && D <= 69) band = 2;
    else if (D >= 70 && D <= 89) band = 3;
    else if (D >= 90 && D <= 110) band = 4;
    if (band < 0) return;
    static const uint8_t kFirst[5] = {0x3A, 0x30, 0x2C, 0x37, 0x32}, kKinds[5] = {2, 2, 4, 3, 4};
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            if (b.ground[y][x] != plain) continue;
            const int r = d.roll(100, 1);
            int sum = 0;
            for (int k = 0; k < 5; ++k) {
                sum += kBand[band][k];
                if (r <= sum) {
                    b.ground[y][x] = static_cast<uint8_t>(kFirst[k] + d.roll(kKinds[k], 1) - 1);
                    break;
                }
            }
        }
}

const uint8_t* tile(const Battle& b, const Tables& t, int x, int y)
{
    static const uint8_t kOff[4] = {1, 0, 0xFF, 0};
    if (!on_field(x, y)) return kOff;
    const int g = b.ground[y][x];
    return g > 0 && g < kGroundValues ? t.ground[g] : kOff;
}

void occupancy(Battle& b)
{
    memset(b.who, 0, sizeof b.who);
    for (int i = 0; i < b.n; ++i) {
        const Fighter& f = b.f[i];
        int sx[4], sy[4];
        const int n = squares(f.size, sx, sy);
        for (int k = 0; k < n; ++k)
            if (on_field(f.x + sx[k], f.y + sy[k])) b.who[f.y + sy[k]][f.x + sx[k]] = static_cast<uint8_t>(i + 1);
    }
}

namespace {

// Can fighter i stand with its top-left square at (x, y)
bool fits(const Battle& b, const Tables& t, int i, int size, int x, int y)
{
    int sx[4], sy[4];
    const int n = squares(size, sx, sy);
    for (int k = 0; k < n; ++k) {
        const int xx = x + sx[k], yy = y + sy[k];
        if (!on_field(xx, yy) || b.ground[yy][xx] == 0 || tile(b, t, xx, yy)[0] == 0xFF) return false;
        if (b.who[yy][xx] && b.who[yy][xx] != i + 1) return false;
    }
    return true;
}

} // namespace

void place(Battle& b, const Tables& t, int facing, int distance, SolidSide solid, void* ctx)
{
    const int f0 = (facing / 2) & 3;
    bool used[2][4][6][11] = {};
    int width[2] = {};
    for (int i = 0; i < b.n; ++i)
        if (b.f[i].up()) ++width[b.f[i].team() ? 1 : 0];
    for (int k = 0; k < 2; ++k) width[k] = (width[k] + 1) / 2;
    memset(b.who, 0, sizeof b.who);
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        const int team = f.team() ? 1 : 0;
        const int tf = team ? (f0 + 2) & 3 : f0;           // the team's half facing
        f.facing = team ? (t.facing[f0] + 4) & 7 : t.facing[f0];
        int size = f.rec[kSize] & 7;
        if (size < 1 || size > 4) size = 1;
        if (f.member >= 0) size = 1;
        const int tx0 = team ? distance * geo::dx(f0 * 2) : 0, ty0 = team ? distance * geo::dy(f0 * 2) : 0;
        bool placed = false;
        for (int at = 0; at < 4 && !placed; ++at) {
            int tx = tx0, ty = ty0;
            if (at > 0) {
                const int dir = t.fallback[tf][at];
                if (solid && solid(ctx, dir)) continue;
                tx += geo::dx(dir);
                ty += geo::dy(dir);
            }
            const int ff = (t.formation[tf][at] / 2) & 3;
            const int back = t.facing[(ff + 2) & 3], right = t.facing[(ff + 1) & 3], left = t.facing[(ff + 3) & 3];
            const int shape = at == 1 ? 4 : tf;
            const int si = at == 0 ? ff : 4 + ff;
            int extra_rank = 0;
            for (int rank = 0; rank < 12 && !placed; ++rank) {
                const int cr = rank + extra_rank;
                const int cx = t.start_x[si] + cr * kDx[back], cy = t.start_y[si] + cr * kDy[back];
                if (cx < 0 || cx > 10 || cy < 0 || cy > 5) break;          // past the grid: the next attempt
                for (int count = 1;; ++count) {
                    const int side = count / 2;
                    const int dir = count % 2 == 0 ? right : left;
                    const int gx = count == 1 ? cx : cx + side * kDx[dir];
                    const int gy = count == 1 ? cy : cy + side * kDy[dir];
                    if (gx < 0 || gx > 10 || gy < 0 || gy > 5) break;
                    if (rank == 0 && count > width[team]) break;
                    if (rank > 0 && count > 11) break;
                    const uint8_t* row = t.shapes[shape][gy];
                    if (gx < row[0] || gx > row[1] || used[team][at][gy][gx]) continue;
                    const int x = gx + 6 * tx + 5 * ty + 22, y = gy + 5 * ty + 10;
                    if (!fits(b, t, i, f.up() ? size : 1, x, y)) continue;
                    used[team][at][gy][gx] = true;
                    f.x = x;
                    f.y = y;
                    placed = true;
                    break;
                }
                // The party facing east or west: a second rank stands two back
                if (!placed && rank == 0 && at == 0 && team == 0 && (tf == 1 || tf == 3)) {
                    bool open = !solid;
                    for (int k = 1; k < 4 && !open; ++k)
                        if (!solid(ctx, t.fallback[tf][k])) open = true;
                    if (open) extra_rank = 1;
                }
            }
        }
        if (!placed) {
            f.size = 0;
            if (f.member < 0) f.gone = true;          // a monster with no room: out of the fight
            continue;
        }
        if (f.up()) {
            f.size = size;
            int sx[4], sy[4];
            const int n = squares(size, sx, sy);
            for (int k = 0; k < n; ++k) b.who[f.y + sy[k]][f.x + sx[k]] = static_cast<uint8_t>(i + 1);
        } else {
            f.size = 0;
            if (f.member >= 0) {
                f.ground = b.ground[f.y][f.x];
                b.ground[f.y][f.x] = kBody;
            }
        }
    }
}

int direction(int fx, int fy, int tx, int ty)
{
    const int ddx = tx - fx, ddy = ty - fy;
    if (!ddx && !ddy) return 8;
    // The 8 directions by angle: compare |dx| and |dy| (2.414 ~ 12/5)
    const int ax = ddx < 0 ? -ddx : ddx, ay = ddy < 0 ? -ddy : ddy;
    int hx = ddx > 0 ? 1 : ddx < 0 ? -1 : 0, hy = ddy > 0 ? 1 : ddy < 0 ? -1 : 0;
    if (ax * 5 > ay * 12) hy = 0;
    else if (ay * 5 > ax * 12) hx = 0;
    for (int d = 0; d < 8; ++d)
        if (kDx[d] == hx && kDy[d] == hy) return d;
    return 8;
}

namespace {

bool path_xy(const Battle& b, const Tables& t, int x0, int y0, int x, int y, bool ignore_walls, int* length)
{
    const int eye = tile(b, t, x0, y0)[1];
    const int ddx = x - x0 > 0 ? x - x0 : x0 - x, ddy = y - y0 > 0 ? y - y0 : y0 - y;
    const int sx = x > x0 ? 1 : -1, sy = y > y0 ? 1 : -1;
    int err = ddx - ddy, len = 0;
    bool seen = true;
    while (x0 != x || y0 != y) {
        const int e2 = 2 * err;
        bool mx = false, my = false;
        if (e2 > -ddy) {
            err -= ddy;
            x0 += sx;
            mx = true;
        }
        if (e2 < ddx) {
            err += ddx;
            y0 += sy;
            my = true;
        }
        len += mx && my ? 3 : 2;
        if (!ignore_walls && (x0 != x || y0 != y) && tile(b, t, x0, y0)[2] > eye) seen = false;
    }
    if (length) *length = len;
    return seen;
}

} // namespace

bool path(const Battle& b, const Tables& t, int a, int x, int y, bool ignore_walls, int* length)
{
    return path_xy(b, t, b.f[a].x, b.f[a].y, x, y, ignore_walls, length);
}

bool range(const Battle& b, const Tables& t, int a, int c, bool ignore_walls, int* sq)
{
    const Fighter& tg = b.f[c];
    int sx[4], sy[4];
    int n = squares(tg.size ? tg.size : 1, sx, sy);
    int best = 9999;
    bool seen = false;
    for (int k = 0; k < n; ++k) {
        int len;
        const bool s = path(b, t, a, tg.x + sx[k], tg.y + sy[k], ignore_walls, &len);
        // Also from the attacker's other squares
        int ax[4], ay[4];
        const int m = squares(b.f[a].size ? b.f[a].size : 1, ax, ay);
        for (int j = 1; j < m; ++j) {
            const int ddx = tg.x + sx[k] - (b.f[a].x + ax[j]), ddy = tg.y + sy[k] - (b.f[a].y + ay[j]);
            const int adx = ddx < 0 ? -ddx : ddx, ady = ddy < 0 ? -ddy : ddy;
            const int l2 = 2 * (adx > ady ? adx : ady) + (adx < ady ? adx : ady);
            if (l2 < len) len = l2;
        }
        if (len < best || (len == best && s)) {
            best = len;
            seen = s;
        }
    }
    if (sq) *sq = best / 2;
    return seen;
}

bool adjacent(const Battle& b, int a, int c)
{
    int ax[4], ay[4], cx[4], cy[4];
    const int na = squares(b.f[a].size ? b.f[a].size : 1, ax, ay);
    const int nc = squares(b.f[c].size ? b.f[c].size : 1, cx, cy);
    for (int i = 0; i < na; ++i)
        for (int j = 0; j < nc; ++j) {
            const int ddx = b.f[a].x + ax[i] - (b.f[c].x + cx[j]), ddy = b.f[a].y + ay[i] - (b.f[c].y + cy[j]);
            if (ddx >= -1 && ddx <= 1 && ddy >= -1 && ddy <= 1) return true;
        }
    return false;
}

int step_cost(const Battle& b, const Tables& t, int i, int dir, bool* edge, int* blocker)
{
    if (edge) *edge = false;
    if (blocker) *blocker = -1;
    if (dir < 0 || dir > 7) return 0xFF;
    const Fighter& f = b.f[i];
    int sx[4], sy[4];
    const int n = squares(f.size ? f.size : 1, sx, sy);
    int cost = 0;
    for (int k = 0; k < n; ++k) {
        const int x = f.x + sx[k] + kDx[dir], y = f.y + sy[k] + kDy[dir];
        if (!on_field(x, y) || b.ground[y][x] == 0) {
            if (edge) *edge = true;
            return 0xFF;
        }
        const int w = b.who[y][x];
        if (w && w != i + 1) {
            if (blocker) *blocker = w - 1;
            return 0xFF;
        }
        const int c = tile(b, t, x, y)[0];
        if (c == 0xFF) return 0xFF;
        if (c > cost) cost = c;
    }
    return cost * (dir % 2 ? 3 : 2);
}

void step(Battle& b, int i, int dir)
{
    Fighter& f = b.f[i];
    f.x += kDx[dir];
    f.y += kDy[dir];
    f.facing = dir;
    f.received = f.turns = 0;
    occupancy(b);
}

bool helpless(const Battle& b, const Fighter& f)
{
    if (!b.fx) return false;
    for (uint8_t h : b.fx->held)
        if (h && f.has(h)) return true;
    return false;
}

namespace {

int find_aff(const Fighter& f, uint8_t type)
{
    for (int i = 0; f.n_aff && i < *f.n_aff; ++i)
        if (f.aff[i][0] == type) return i;
    return -1;
}

void drop_aff(Fighter& f, int i)
{
    for (int k = i; k + 1 < *f.n_aff; ++k) memcpy(f.aff[k], f.aff[k + 1], party::kAffectSize);
    --*f.n_aff;
    memset(f.aff[*f.n_aff], 0, party::kAffectSize);
}

void give_aff(Fighter& f, int type, int minutes, int data, bool call)
{
    if (!f.n_aff) return;
    const int i = find_aff(f, static_cast<uint8_t>(type));
    if (i >= 0 && (f.aff[i][1] | f.aff[i][2] << 8) > 0) drop_aff(f, i);
    if (*f.n_aff >= f.max_aff) return;
    uint8_t* a = f.aff[(*f.n_aff)++];
    memset(a, 0, party::kAffectSize);
    a[0] = static_cast<uint8_t>(type);
    a[1] = static_cast<uint8_t>(minutes);
    a[2] = static_cast<uint8_t>(minutes >> 8);
    a[3] = static_cast<uint8_t>(data);
    a[4] = call ? 1 : 0;
}

} // namespace

void uncharm(uint8_t* rec, const uint8_t* a) { rec[0x197] = static_cast<uint8_t>((a[3] & 0x40) >> 6); }

namespace {
const Facts* g_fx = nullptr;         // the fight's effects, for saving throws (set as rounds start)
}

TurnFx turn_effects(Battle& b, int i)
{
    Fighter& f = b.f[i];
    if (!b.fx || !f.up()) return TurnFx::None;
    const Facts& fx = *b.fx;
    if (fx.fumbling && f.has(fx.fumbling)) {
        f.moves = f.attacks[0] = f.attacks[1] = 0;
        return TurnFx::Fumbling;
    }
    const int s = fx.sticks ? find_aff(f, fx.sticks) : -1;
    if (s >= 0) {
        // The snakes go down by its attacks this round; when they're no more
        // than its attacks, they're gone
        const int att = f.attacks[0] + f.attacks[1];
        int snakes = f.aff[s][3] - att;
        if (snakes <= att) {
            drop_aff(f, s);
        } else {
            f.aff[s][3] = static_cast<uint8_t>(snakes);
            f.moves = f.attacks[0] = f.attacks[1] = 0;
            return TurnFx::Snakes;
        }
    }
    if (fx.silence && f.can_use) {
        bool hushed = f.has(fx.silence);
        for (int c = 0; !hushed && c < b.n; ++c) {
            const Fighter& o = b.f[c];
            if (c == i || !o.size || !o.has(fx.silence)) continue;
            const int ddx = o.x - f.x, ddy = o.y - f.y;
            hushed = ddx >= -1 && ddx <= 1 && ddy >= -1 && ddy <= 1;
        }
        if (hushed) {
            f.can_use = false;
            f.can_cast = false;
            return TurnFx::Silenced;
        }
    }
    return TurnFx::None;
}

void tick(Battle& b)
{
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        for (int k = 0; f.n_aff && k < *f.n_aff;) {
            const int m = f.aff[k][1] | f.aff[k][2] << 8;
            if (m == 0) {
                ++k;
            } else if (m <= 1) {
                if (b.fx && b.fx->charm && f.aff[k][0] == b.fx->charm) {
                    uncharm(f.rec, f.aff[k]);           // back to their own side
                    f.target = -1;
                }
                if (b.fx && b.fx->fear && f.aff[k][0] == b.fx->fear) {
                    f.fleeing = false;                  // the fear is over
                    if (f.aff[k][3] & 1) f.quick = false;
                }
                drop_aff(f, k);
            } else {
                f.aff[k][1] = static_cast<uint8_t>(m - 1);
                f.aff[k][2] = static_cast<uint8_t>((m - 1) >> 8);
                ++k;
            }
        }
    }
}

int dex_reaction(int dex)
{
    if (dex <= 2) return -4;
    if (dex <= 5) return dex - 6;
    if (dex <= 15) return 0;
    if (dex <= 18) return dex - 15;
    if (dex <= 20) return 3;
    if (dex <= 23) return 4;
    return 5;
}

int attacks_this_round(int half, int round) { return (half + (round % 2 ? 1 : 0)) / 2; }

void start_round(Battle& b, create::Dice& d)
{
    g_fx = b.fx;
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        f.attacked = false;
        f.swept = false;
        f.can_cast = true;
        f.can_use = true;
        f.coughed = false;
        if (!f.up() || !f.size) {
            f.delay = f.moves = f.attacks[0] = f.attacks[1] = 0;
            continue;
        }
        int delay = d.roll(6, 1) + dex_reaction(f.rec[kDexFull]);
        if (delay < 1) delay = 1;
        if (b.surprise & (f.team() ? 4 : 2)) delay -= 6;
        if (delay < 0 || delay > 20) delay = 0;
        f.delay = delay;
        if (helpless(b, f)) f.delay = 0;
        int mv = f.rec[kMove];
        if (mv < 1 || mv > 96) mv = 1;
        f.moves = mv * 2;
        int half1 = f.rec[kHalf1], half2 = f.rec[kHalf2];
        if (half1 < 1) half1 = 2;
        if (b.fx && f.has(b.fx->haste)) {
            f.moves *= 2;
            half1 *= 2;
            half2 *= 2;
        }
        if (b.fx && f.has(b.fx->slow)) {
            f.moves /= 2;
            half1 /= 2;
            half2 /= 2;
        }
        f.attacks[0] = attacks_this_round(half1, b.round);
        f.attacks[1] = attacks_this_round(half2, b.round);
        if (b.fx && b.fx->entangle && f.has(b.fx->entangle)) f.moves = 0;      // entangled: no moving
    }
    b.surprise = 0;
}

int next(Battle& b, create::Dice& d)
{
    int best = -1, best_delay = 0, best_roll = -1;
    for (int i = 0; i < b.n; ++i) {
        const Fighter& f = b.f[i];
        if (f.delay <= 0 || !f.up() || !f.size) continue;
        const int r = d.roll(100, 1);
        if (f.delay > best_delay) {
            best = i;
            best_delay = f.delay;
            best_roll = r;
        } else if (f.delay == best_delay && r >= best_roll) {
            best = i;
            best_roll = r;
        }
    }
    return best;
}

bool damage(Battle& b, int c, int amount)
{
    Fighter& f = b.f[c];
    f.can_cast = false;
    if (amount <= 0 || !f.up()) return false;
    const int hp = f.rec[kHp];
    if (hp > amount) {
        f.rec[kHp] = static_cast<uint8_t>(hp - amount);
        return false;
    }
    const int over = amount - hp;
    f.rec[kHp] = 0;
    if (f.status() == party::Animated || over > 9) {
        f.rec[kHealth] = party::Dead;
    } else if (over == 0) {
        f.rec[kHealth] = party::Unconscious;
    } else {
        f.rec[kHealth] = party::Dying;
        f.bleeding = over;
    }
    f.rec[kInCombat] = 0;
    f.delay = 0;
    f.moves = 0;
    f.attacks[0] = f.attacks[1] = 0;
    f.guarding = false;
    // Off the field: a party member leaves a body
    if (f.size) {
        f.size = 0;
        if (f.member >= 0 && on_field(f.x, f.y) && b.ground[f.y][f.x] != 0x1E && b.ground[f.y][f.x] != 0x1C) {
            f.ground = b.ground[f.y][f.x];
            b.ground[f.y][f.x] = kBody;
        }
        occupancy(b);
    }
    return true;
}

Down down_state(const Fighter& f)
{
    switch (f.status()) {
    case party::Unconscious: return Down::Unconscious;
    case party::Dying: return Down::Dying;
    case party::Dead: case party::Stoned: case party::Gone: return Down::Dead;
    default: return Down::No;
    }
}

Attack attack(Battle& b, int a, int c, const items::Names* names, create::Dice& d, bool from_behind)
{
    Attack out;
    Fighter& at = b.f[a];
    Fighter& tg = b.f[c];
    b.no_action = b.round + 15;
    at.attacked = true;
    // The target turns to face the attacker
    const int dir = direction(at.x, at.y, tg.x, tg.y);
    if (!from_behind && tg.received < 2 && dir < 8) tg.facing = (dir + 4) & 7;
    if (dir < 8) at.facing = dir;
    int turn = dir < 8 ? ((dir - tg.facing) & 7) : 0;
    if (turn > 4) turn = 8 - turn;
    tg.turns = (tg.turns + turn) & 7;
    ++tg.received;
    const bool stab = !from_behind && can_backstab(b, a, c, names);
    const bool behind = from_behind || stab || (tg.received > 1 && dir == tg.facing && tg.turns > 4);
    out.behind = behind && !stab;
    out.backstab = stab;
    int ac = tg.rec[behind ? kAcBehind : kAc];
    if (stab) ac -= 4;
    const int times = stab ? (at.rec[kThiefLevel] - 1) / 4 + 2 : 1;
    // Large targets: the weapon's large dice
    const bool large = (tg.rec[kSize] & 0x80) || (tg.rec[kSize] & 7) > 1;
    int dice1 = at.rec[kDice], sides1 = at.rec[kSides], bonus1 = static_cast<int8_t>(at.rec[kBonus]);
    if (large && names) {
        const int w = weapon(at, *names);
        if (w >= 0) {
            const items::TypeInfo& ti = names->type(at.items[w][0x2E]);
            dice1 = ti.dice_large;
            sides1 = ti.sides_large;
            bonus1 += ti.bonus_large - ti.bonus;
        }
    }
    int side = at.team() ? b.to_hit_monsters : b.to_hit_party;
    if (b.fx) {
        const Facts& fx = *b.fx;
        if (at.has(fx.bless)) ++side;
        if (at.has(fx.curse)) --side;
        // A prayer helps its caster's side and hinders the other
        for (int i = 0; i < b.n; ++i) {
            const int k = find_aff(b.f[i], fx.prayer);
            if (k < 0) continue;
            side += (b.f[i].aff[k][3] >> 4) == at.team() ? 1 : -1;
            break;
        }
        if (fx.bestow && at.has(fx.bestow)) side -= 4;
        if (fx.blinded && at.has(fx.blinded)) side -= 4;
        if (fx.animals_blind && at.rec[0x11A] == 19 && tg.has(fx.animals_blind)) side -= 4;
        if (fx.blinded && tg.has(fx.blinded)) ac -= 4;         // a blind target: easier
        // Faerie Fire: the stored AC + 2 (to AC 0 at most) - the original's
        // own rule, which makes the target harder to hit
        if (fx.faerie && tg.has(fx.faerie)) ac = ac < 58 ? ac + 2 : 60;
        const int al = at.rec[0x11B];
        if (tg.has(fx.prot_evil) && al >= 6) side -= 2;
        if (tg.has(fx.prot_good) && al <= 2) side -= 2;
        // Attacking makes them seen
        const int inv = find_aff(at, fx.invisible);
        if (inv >= 0) drop_aff(at, inv);
        // A helpless target: one cruel blow
        if (helpless(b, tg)) {
            out.slain = true;
            at.attacks[0] = at.attacks[1] = 0;
            Hit h;
            h.hit = true;
            h.damage = tg.hp() + 5;
            out.hits[out.n++] = h;
            out.any = true;
            out.down = damage(b, c, h.damage);
            return out;
        }
    }
    for (int slot = 1; slot >= 0; --slot) {
        while (at.attacks[slot] > 0 && tg.up() && out.n < 8) {
            --at.attacks[slot];
            const int roll = d.roll(20, 1);
            bool hit = roll == 20 || (roll != 1 && roll + static_cast<int8_t>(at.rec[kHit]) + side >= ac);
            // Blinking, once it has acted this round: not there to be hit
            if (b.fx && b.fx->blink && tg.has(b.fx->blink) && tg.delay == 0) hit = false;
            Hit h;
            h.hit = hit;
            if (hit) {
                const int n = slot ? at.rec[kDice + 1] : dice1;
                const int s = slot ? at.rec[kSides + 1] : sides1;
                const int bo = slot ? static_cast<int8_t>(at.rec[kBonus + 1]) : bonus1;
                int dmg = (n && s ? d.roll(s, n) : 0) + bo;
                if (dmg < 0) dmg = 0;
                dmg *= times;
                h.damage = dmg;
                out.any = true;
                if (damage(b, c, dmg)) out.down = true;
            }
            out.hits[out.n++] = h;
        }
    }
    return out;
}

bool can_backstab(const Battle& b, int a, int c, const items::Names* names)
{
    const Fighter& at = b.f[a];
    const Fighter& tg = b.f[c];
    if (!at.rec[kThiefLevel] || tg.received < 2 || (tg.rec[kSize] & 0x80) || (tg.rec[kSize] & 7) > 1) return false;
    if (direction(at.x, at.y, tg.x, tg.y) != tg.facing) return false;
    if (names && b.fx) {
        const int w = weapon(at, *names);
        if (w >= 0) {
            bool ok = false;
            for (uint8_t t : b.fx->backstab_weapons)
                if (t && at.items[w][0x2E] == t) ok = true;
            if (!ok) return false;
        }
    }
    return true;
}

bool free_attack_ok(const Battle& b, const Tables& t, int e, int mover)
{
    const Fighter& en = b.f[e];
    const Fighter& m = b.f[mover];
    if (helpless(b, en) || !range(b, t, e, mover, false, nullptr)) return false;
    if (en.delay > 0 || en.received == 0) return true;
    const int dir = direction(en.x, en.y, m.x, m.y);
    if (dir > 7) return true;
    int off = (dir - en.facing) & 7;
    if (off > 4) off = 8 - off;
    return off <= 2;
}

int sweep(const Battle& b, int a, int target, int* out, int cap)
{
    const Fighter& at = b.f[a];
    const int level = at.member >= 0 ? at.rec[kFighterLevel] : 0;
    if (level <= 0 || at.swept || at.attacks[0] >= level) return 0;
    const Fighter& tg = b.f[target];
    if (tg.rec[kHd] != 0 || !adjacent(b, a, target)) return 0;
    int n = 0;
    out[n++] = target;
    int all = 1;
    for (int i = 0; i < b.n; ++i) {
        const Fighter& o = b.f[i];
        if (i == target || !o.up() || !o.size || o.team() == at.team() || o.rec[kHd] != 0 || !adjacent(b, a, i)) continue;
        ++all;
        if (n < level && n < cap) out[n++] = i;
    }
    return all > at.attacks[0] ? n : 0;
}

int weapon(const Fighter& f, const items::Names& names)
{
    for (int i = 0; f.items && i < f.n_items; ++i)
        if (f.items[i][0x34] && names.type(f.items[i][0x2E]).slot == items::kSlotWeapon) return i;
    return -1;
}

int missile(const Battle& b, const Fighter& f, int* ammo)
{
    *ammo = -1;
    if (!b.names) return 0;
    const int w = weapon(f, *b.names);
    if (w < 0) return 0;
    const items::TypeInfo& ti = b.names->type(f.items[w][0x2E]);
    if (ti.range <= 1) return 0;
    if (ti.flags & 0x81) {
        // A launcher: its readied arrows / quarrels
        for (int k = 0; k < f.n_items; ++k) {
            const int ty = f.items[k][0x2E];
            if (!f.items[k][0x34]) continue;
            if (((ti.flags & 0x01) && ty == b.arrow) || ((ti.flags & 0x80) && ty == b.quarrel)) *ammo = k;
        }
        if (*ammo < 0) return 0;
    } else if (ti.flags & 0x10) {
        *ammo = w;                                   // thrown: the weapon goes
    }                                                // (else it shoots without ammunition: a sling)
    return ti.range - 1 > 1 ? ti.range - 1 : 1;
}

int money_exp(const int m[7])
{
    const long copper = m[0] + 10L * m[1] + 100L * m[2] + 200L * m[3] + 1000L * m[4];
    return static_cast<int>(copper / 200 + 250L * m[5] + 2200L * m[6]);
}

int award(Battle& b, int total)
{
    int survivors = 0;
    for (int i = 0; i < b.party_size; ++i)
        if (b.f[i].up() && b.f[i].status() != party::Animated) ++survivors;
    if (!survivors) return 0;
    const int share = total / survivors;
    for (int i = 0; i < b.party_size; ++i) {
        Fighter& f = b.f[i];
        if (!f.up() || f.status() == party::Animated) continue;
        const uint8_t* r = f.rec;
        int add = share;
        const int str = r[0x11], in = r[0x13], wis = r[0x15], dex = r[0x17];
        int classes = 0;
        for (int k = 0; k < 8; ++k)
            if (r[0x109 + k]) ++classes;
        switch (r[0x75]) {
        case 0: if (wis > 15) add += share / 10; break;
        case 2: if (str > 15) add += share / 10; break;
        case 3: if (str > 15 && wis > 15) add += share / 10; break;
        case 4: if (str > 15 && in > 15 && wis > 15) add += share / 10; break;
        case 5: if (in > 15) add += share / 10; break;
        case 6: if (dex > 15) add += share / 10; break;
        default:
            if (r[0x75] >= 8 && classes > 1) add = share / classes;
            break;
        }
        uint32_t e = r[0x127] | r[0x128] << 8 | r[0x129] << 16 | static_cast<uint32_t>(r[0x12A]) << 24;
        e += static_cast<uint32_t>(add);
        for (int k = 0; k < 4; ++k) f.rec[0x127 + k] = static_cast<uint8_t>(e >> (8 * k));
    }
    return share;
}

int standing(const Battle& b, int team)
{
    int n = 0;
    for (int i = 0; i < b.n; ++i)
        if (!b.f[i].gone && b.f[i].up() && b.f[i].size && (b.f[i].team() ? 1 : 0) == team) ++n;
    return n;
}

bool anyone_dying(const Battle& b)
{
    for (int i = 0; i < b.party_size; ++i)
        if (b.f[i].status() == party::Dying) return true;
    return false;
}

int bandage(Battle& b)
{
    for (int i = 0; i < b.party_size; ++i)
        if (b.f[i].status() == party::Dying) {
            b.f[i].rec[kHealth] = party::Unconscious;
            b.f[i].bleeding = 0;
            return i;
        }
    return -1;
}

bool end_round(Battle& b)
{
    ++b.round;
    // The enemies' health
    long now = 0, most = 0;
    for (int i = 0; i < b.n; ++i) {
        const Fighter& f = b.f[i];
        if (!f.team() || f.gone) continue;
        most += f.rec[kHpMax];
        if (f.up()) now += f.rec[kHp];
    }
    b.enemy_health = most ? static_cast<int>(20 * now / most) * 5 : 0;
    // Bleeding
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        if (f.status() != party::Dying) continue;
        if (++f.bleeding > 9) f.rec[kHealth] = party::Dead;
    }
    return standing(b, 0) == 0 || standing(b, 1) == 0 || b.round >= b.no_action;
}

Plan think(Battle& b, const Tables& t, int i, create::Dice& d)
{
    Plan p;
    Fighter& f = b.f[i];
    if (!f.up() || !f.size || f.delay <= 0) return p;
    const int my = f.team() ? 1 : 0;
    auto enemy = [&](int c) {
        return c >= 0 && c < b.n && b.f[c].up() && b.f[c].size && (b.f[c].team() ? 1 : 0) != my &&
               !(b.fx && b.f[c].has(b.fx->invisible));
    };
    if (f.fleeing) {
        // Away from the nearest enemy, off the field's edge
        if (f.moves < 2) return p;
        int near = -1, best = 9999;
        for (int c = 0; c < b.n; ++c) {
            int sq;
            if (!b.f[c].up() || !b.f[c].size || (b.f[c].team() ? 1 : 0) == my) continue;
            range(b, t, i, c, true, &sq);
            if (sq < best) {
                best = sq;
                near = c;
            }
        }
        int base = near >= 0 ? (direction(f.x, f.y, b.f[near].x, b.f[near].y) + 4) & 7 : 0;
        if (base > 7) base = 0;
        static const int kAway[5] = {0, 1, -1, 2, -2};
        for (int k : kAway) {
            const int dir = (base + k + 8) & 7;
            bool edge;
            const int cost = step_cost(b, t, i, dir, &edge, nullptr);
            if (edge) {
                p.act = Act::Flee;
                return p;
            }
            if (cost == 0xFF || cost > f.moves) continue;
            p.act = Act::Step;
            p.dir = dir;
            return p;
        }
        return p;
    }
    // The target: kept while it's an enemy in sight
    if (!enemy(f.target) || !range(b, t, i, f.target, false, nullptr)) {
        f.target = -1;
        int cand[kMaxFighters], n = 0;
        for (int c = 0; c < b.n; ++c)
            if (enemy(c) && range(b, t, i, c, false, nullptr)) cand[n++] = c;
        if (n) f.target = cand[d.roll(n, 1) - 1];
        if (f.target < 0) {
            // None in sight: the nearest, walls or not
            int best = 9999;
            for (int c = 0; c < b.n; ++c) {
                int sq;
                if (!enemy(c)) continue;
                range(b, t, i, c, true, &sq);
                if (sq < best) {
                    best = sq;
                    f.target = c;
                }
            }
        }
    }
    if (f.target < 0) {
        p.act = Act::Guard;
        return p;
    }
    // Next to an enemy: attack it (the target first)
    if ((f.attacks[0] > 0 || f.attacks[1] > 0)) {
        if (adjacent(b, i, f.target)) {
            p.act = Act::Attack;
            p.target = f.target;
            return p;
        }
        for (int c = 0; c < b.n; ++c)
            if (enemy(c) && adjacent(b, i, c)) {
                f.target = c;
                p.act = Act::Attack;
                p.target = c;
                return p;
            }
        // A missile weapon: the target in reach and sight, else one there
        int ammo = -1;
        const int reach = missile(b, f, &ammo);
        if (reach > 0) {
            int sq;
            int shot = -1;
            if (range(b, t, i, f.target, false, &sq) && sq <= reach) {
                shot = f.target;
            } else {
                int cand[kMaxFighters], n = 0;
                for (int c = 0; c < b.n; ++c)
                    if (enemy(c) && range(b, t, i, c, false, &sq) && sq <= reach) cand[n++] = c;
                if (n) shot = cand[d.roll(n, 1) - 1];
            }
            if (shot >= 0) {
                f.target = shot;
                p.act = Act::Attack;
                p.target = shot;
                p.missile = true;
                p.ammo = ammo;
                return p;
            }
        }
    } else {
        return p;
    }
    // An unarmoured magic-user doesn't go forward: it guards
    if (f.rec[0x10E] > 0 && b.names && f.items) {
        bool armour = false;
        for (int k = 0; k < f.n_items && !armour; ++k)
            armour = f.items[k][0x34] && b.names->type(f.items[k][0x2E]).slot == items::kSlotArmour;
        if (!armour) {
            p.act = Act::Guard;
            return p;
        }
    } else if (f.rec[0x10E] > 0 && !f.items) {
        p.act = Act::Guard;
        return p;
    }
    // A step toward it: straight, else a little to either side
    if (f.moves < 2) return p;
    const Fighter& tg = b.f[f.target];
    const int base = direction(f.x, f.y, tg.x, tg.y);
    if (base > 7) return p;
    static const int kTry[5] = {0, 1, -1, 2, -2};
    int ox[4], oy[4];
    const int n0 = squares(tg.size ? tg.size : 1, ox, oy);
    int now_best = 9999;
    for (int k = 0; k < n0; ++k) {
        const int ddx = tg.x + ox[k] - f.x, ddy = tg.y + oy[k] - f.y;
        const int dd = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
        if (dd < now_best) now_best = dd;
    }
    for (int k : kTry) {
        const int dir = (base + k + 8) & 7;
        const int cost = step_cost(b, t, i, dir, nullptr, nullptr);
        if (cost == 0xFF || cost > f.moves) continue;
        // Not further away than now
        const int nx = f.x + kDx[dir], ny = f.y + kDy[dir];
        if (b.ground[ny][nx] == kCloudGround && !saving_throw(f, 0, 0, d)) continue;   // a cloud: only after a test save
        if (b.ground[ny][nx] == kPoisonGround && f.rec[0xE5] < 7) continue;            // poison: never (below 7 Hit Dice)
        int best = 9999;
        for (int j = 0; j < n0; ++j) {
            const int ddx = tg.x + ox[j] - nx, ddy = tg.y + oy[j] - ny;
            const int dd = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
            if (dd < best) best = dd;
        }
        if (best > now_best) continue;
        p.act = Act::Step;
        p.dir = dir;
        return p;
    }
    p.act = Act::Guard;
    return p;
}

bool flee(Battle& b, const Tables& t, int i, create::Dice& d)
{
    Fighter& f = b.f[i];
    const int my = f.team() ? 1 : 0;
    int fastest = -1;
    for (int c = 0; c < b.n; ++c) {
        const Fighter& e = b.f[c];
        if (!e.up() || !e.size || (e.team() ? 1 : 0) == my) continue;
        if (!range(b, t, c, i, true, nullptr) && false) continue;
        if (e.rec[kMove] > fastest) fastest = e.rec[kMove];
    }
    const int mine = f.rec[kMove];
    const bool away = fastest < 0 || mine > fastest || (mine == fastest && d.roll(2, 1) == 1);
    f.delay = 0;
    f.moves = 0;
    if (!away) return false;
    f.rec[kHealth] = party::Running;
    f.rec[kInCombat] = 0;
    f.size = 0;
    occupancy(b);
    return true;
}

int in_area(const Battle& b, const Tables& t, int x, int y, int r, int* out, int cap)
{
    int n = 0;
    for (int i = 0; i < b.n && n < cap; ++i) {
        const Fighter& f = b.f[i];
        if (!f.size || f.gone) continue;
        int sx[4], sy[4];
        const int m = squares(f.size, sx, sy);
        for (int k = 0; k < m; ++k) {
            int len;
            if (path_xy(b, t, x, y, f.x + sx[k], f.y + sy[k], false, &len) && len <= 2 * r + 1) {
                out[n++] = i;
                break;
            }
        }
    }
    return n;
}

bool saving_throw(const Fighter& f, int type, int bonus, create::Dice& d)
{
    const int r = d.roll(20, 1);
    if (r == 1) return false;
    if (r == 20) return true;
    if (type < 0 || type > 4) type = 4;
    if (g_fx) {
        if (g_fx->bestow && f.has(g_fx->bestow)) bonus -= 4;
        if (g_fx->blinded && f.has(g_fx->blinded)) bonus -= 4;
    }
    return r + bonus + static_cast<int8_t>(f.rec[0x186]) >= f.rec[0xDF + type];
}

// The caster's level for the spell's kind (as spells::power, from the record)
int power_of(const uint8_t* r, const classes::Tables& st, int s, bool item)
{
    if (item && st.spell_class(s) != 3) return 6;
    const int cl = r[0x109], pa = r[0x10C], ra = r[0x10D], mu = r[0x10E];
    if (cl == 0 && mu == 0 && pa < 9 && ra < 8) return 6;
    auto most = [](int a, int b) { return a > b ? a : b; };
    switch (st.spell_class(s)) {
    case 0: return most(cl, pa - 8);
    case 1: return most(ra - 7, 0);
    case 2: return most(mu, ra - 8);
    case 3: return 12;
    default: return 0;
    }
}

namespace {

int sleep_cost(const Fighter& f)
{
    switch (f.rec[0xE5]) {
    case 0: case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    case 4: return 6;
    case 5: return f.rec[0x74] == 0 ? 10 : 20;
    default: return 20;
    }
}

} // namespace

int cast(Battle& b, const classes::Tables& st, int caster, int spell, const FightSpell& fs, const int* targets,
         int n, create::Dice& d, SpellLine* out, int cap, int pw)
{
    int lines = 0;
    auto say = [&](int who, Did did, int amount) {
        if (lines < cap) out[lines++] = SpellLine{static_cast<uint8_t>(who), did, amount};
    };
    Fighter& me = b.f[caster];
    const spells::Entry e = spells::entry(st, spell);
    if (pw <= 0) pw = power_of(me.rec, st, spell);
    int minutes = e.lasts + e.lasts_level * pw;
    const bool rolled = fs.does != SpellDoes::Heal && fs.does != SpellDoes::Damage && fs.does != SpellDoes::Bolt &&
                        fs.n && fs.sides;
    if (rolled) minutes = (d.roll(fs.sides, fs.n) + fs.plus) * (fs.per == 4 ? 10 : 1);
    const int my = me.team() ? 1 : 0;
    // The targets this kind of spell takes
    int who[kMaxFighters], m = 0;
    for (int k = 0; k < n && m < kMaxFighters; ++k) {
        const int i = targets[k];
        if (i < 0 || i >= b.n) continue;
        const int side = b.f[i].team() ? 1 : 0;
        if (fs.does == SpellDoes::Ours && side != my) continue;
        if (fs.does == SpellDoes::Theirs && side == my) continue;
        who[m++] = i;
    }
    if (m == 0) return 0;
    b.no_action = b.round + 15;
    switch (fs.does) {
    case SpellDoes::NotYet: return 0;
    case SpellDoes::Affect:
    case SpellDoes::Ours:
    case SpellDoes::Theirs:
    case SpellDoes::Prayer:
    case SpellDoes::Mirror:
    case SpellDoes::Hold: {
        int bonus = 0;
        if (fs.does == SpellDoes::Hold) bonus = m == 1 ? (spell == 0x17 ? -2 : -3) : m == 2 ? -1 : 0;
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            // The hold spells take persons only (but Hold Monsters)
            if (fs.does == SpellDoes::Hold && spell != 0x5E && (f.rec[0x11A] > 1 || f.rec[0xDE] > 1)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            // A touch (Bestow Curse): the blow has to land
            if (e.range == -1 && e.on_save == 1) {
                const int roll = d.roll(20, 1);
                if (roll == 1 || (roll != 20 && roll + static_cast<int8_t>(me.rec[kHit]) < f.rec[kAc])) {
                    say(who[k], Did::Unaffected, 0);
                    continue;
                }
            }
            if (e.on_save != 0 && saving_throw(f, e.save, bonus, d) && e.on_save == 1) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            int data = pw;
            if (fs.does == SpellDoes::Prayer) data = my * 16 + pw;
            if (fs.does == SpellDoes::Mirror) data = (d.roll(4, 1) << 4) + pw;
            if (e.affect) give_aff(f, e.affect, minutes, data, false);
            if (fs.word) say(who[k], Did::Word, 0);
        }
        break;
    }
    case SpellDoes::Haste: {
        int left = pw;
        for (int k = 0; k < m && left > 0; ++k) {
            Fighter& f = b.f[who[k]];
            if ((f.team() ? 1 : 0) != my) continue;
            --left;
            const int sl = b.fx ? find_aff(f, b.fx->slow) : -1;
            if (sl >= 0) {
                drop_aff(f, sl);
                continue;
            }
            if (e.affect) give_aff(f, e.affect, minutes, pw, false);
            if (fs.word) say(who[k], Did::Word, 0);
        }
        break;
    }
    case SpellDoes::Heal:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            const int st2 = f.status();
            if (st2 != party::Okay && st2 != party::Animated && st2 != party::Unconscious && st2 != party::Dying) continue;
            const int amount = (fs.n && fs.sides ? d.roll(fs.sides, fs.n) : 0) + fs.plus;
            int hp = f.hp() + amount;
            if (hp > f.hp_max()) hp = f.hp_max();
            f.rec[kHp] = static_cast<uint8_t>(hp);
            if (st2 == party::Dying) {
                f.rec[kHealth] = party::Unconscious;
                f.bleeding = 0;
            }
            say(who[k], fs.word ? Did::Word : Did::Healed, amount);
        }
        break;
    case SpellDoes::Charm:
        // A person only (humanoid, not large); a save as the table says; the
        // caster's side from now on, run by the computer
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            // Charm Monsters (kind 1): the last one picked is unaffected, and
            // (the original's rule) so are non-persons and large ones
            if (fs.kind == 1 && (k == m - 1 || f.rec[0x11A] > 1 || f.rec[0xDE] > 1)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            if (fs.kind != 1 && (f.rec[0x11A] > 1 || f.rec[0xDE] > 1)) {
                say(who[k], Did::Word2, 0);
                continue;
            }
            if (e.on_save != 0 && saving_throw(f, e.save, 0, d) && e.on_save == 1) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            if (!b.fx || !b.fx->charm) continue;
            const int caster_side = b.f[caster].team() ? 1 : 0, own = f.team() ? 1 : 0;
            give_aff(f, b.fx->charm, minutes, (caster_side << 7) | (own << 6) | 0x20 | (pw & 0x1F), false);
            f.rec[0x197] = static_cast<uint8_t>(caster_side);
            f.target = -1;
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Poison:
        // (the cloud is laid by the caller: lay_cloud(poison); these are those inside)
        for (int k = 0; k < m; ++k)
            if (breathe_poison(b, who[k], d)) {
                say(who[k], Did::Word, 0);
                say(who[k], Did::Down, 0);
            }
        break;
    case SpellDoes::Cloud:
        // (the cloud itself is laid by the caller: lay_cloud; these are those inside)
        for (int k = 0; k < m; ++k) {
            const Did r = breathe_cloud(b, who[k], d);
            if (r != Did::Unaffected) say(who[k], r, 0);
        }
        break;
    case SpellDoes::Fear:
        // Those that fail a save run (the computer runs them) until it wears off
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || who[k] == caster) continue;
            if (saving_throw(f, e.save, 0, d)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            const bool made_quick = f.member >= 0 && f.rec[0xF7] < 0x80 && !f.quick;
            if (b.fx && b.fx->fear) give_aff(f, b.fx->fear, minutes, made_quick ? 1 : 0, false);
            f.fleeing = true;
            if (made_quick) f.quick = true;
            f.target = -1;
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Cone:
    case SpellDoes::Bolt:
    case SpellDoes::Damage:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            int count = fs.n, plus = fs.plus;
            if (fs.per == 6) {
                count = pw;
                plus += pw;
            }
            if (fs.per == 1) plus += pw;
            if (fs.per == 2) {
                count = (pw + 1) / 2;
                plus += count;
            }
            if (fs.per == 3) count = pw;
            if (fs.per == 5) count = d.roll(3, 1) * 2 + 1;
            int dmg = (count && fs.sides ? d.roll(fs.sides, count) : 0) + plus;
            if (e.range == -1) {
                // A touch: a blow that has to land
                const int roll = d.roll(20, 1);
                if (roll == 1 || (roll != 20 && roll + static_cast<int8_t>(me.rec[kHit]) < f.rec[kAc])) {
                    say(who[k], Did::Misses, 0);
                    continue;
                }
            }
            if (e.on_save != 0 && saving_throw(f, e.save, 0, d)) {
                if (e.on_save == 1) dmg = 0;
                else if (e.on_save == 2) dmg /= 2;
            }
            if (dmg <= 0) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            say(who[k], Did::Damage, dmg);
            if (damage(b, who[k], dmg)) say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Slay:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            if (!saving_throw(f, e.save, 0, d)) {
                say(who[k], Did::Word, 0);
                damage(b, who[k], f.hp() + 10);
                f.rec[kHealth] = party::Dead;
                say(who[k], Did::Fallen, 0);
                continue;
            }
            const int dmg = d.roll(fs.sides, fs.n) + fs.plus;
            say(who[k], Did::Damage, dmg);
            if (damage(b, who[k], dmg)) say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Kill:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || saving_throw(f, 0, 0, d)) continue;
            say(who[k], Did::Word, 0);
            damage(b, who[k], f.hp() + 10);
            f.rec[kHealth] = party::Dead;
            say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Fumble:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || !b.fx) continue;
            if (!saving_throw(f, e.save, 0, d)) {
                give_aff(f, b.fx->fumbling, pw, 0, false);
                f.moves = f.attacks[0] = f.attacks[1] = 0;
                say(who[k], Did::Word, 0);
            } else {
                give_aff(f, b.fx->slow, pw, 0, false);
                say(who[k], Did::Word2, 0);
            }
            // (the original then saves again: clumsy again, or unaffected)
            if (!saving_throw(f, e.save, 0, d)) {
                give_aff(f, b.fx->fumbling, pw, pw, false);
                say(who[k], Did::Word, 0);
            } else {
                say(who[k], Did::Unaffected, 0);
            }
        }
        break;
    case SpellDoes::Feeble:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            // This save only: a cleric -1, a magic-user +4, others +2 on the number needed
            const int adj = f.rec[0x75] == 0 ? -1 : f.rec[0x75] == 5 ? 4 : 2;
            const uint8_t was = f.rec[0xE3];
            f.rec[0xE3] = static_cast<uint8_t>(was + adj);
            const bool saved = saving_throw(f, 4, 0, d);
            f.rec[0xE3] = was;
            if (saved) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            if (b.fx && b.fx->feeble) give_aff(f, b.fx->feeble, 0, pw, false);
            f.rec[0x13] = f.rec[0x15] = 7;          // Intelligence, Wisdom in use (a recalculation makes them 3)
            f.can_cast = false;
            f.spell = 0;
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Entangle:
        if (b.indoors) break;                       // outdoors only
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || !b.fx) continue;
            if (saving_throw(f, e.save, 0, d)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            // (the original reads a row past the table's end for how long:
            // 6 + 3 a level at level 6)
            give_aff(f, b.fx->entangle, 24, 0, false);
            f.moves = 0;
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Faerie:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            if (k == m - 1 || f.rec[0x11A] > 1 || f.rec[0xDE] > 1 || saving_throw(f, e.save, 0, d)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            if (e.affect) give_aff(f, e.affect, minutes, pw, false);
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Snakes:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || !b.fx) continue;
            if (f.rec[0xE5] >= 6) {
                say(who[k], Did::Word2, 0);
                continue;
            }
            give_aff(f, b.fx->sticks, minutes, pw, false);
            f.moves = f.attacks[0] = f.attacks[1] = 0;
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::SnakeCharm: {
        // Snakes on either side, as many as the caster's hit points cover
        int budget = me.hp();
        for (int c = 0; c < b.n; ++c) {
            Fighter& f = b.f[c];
            if (!f.up() || !f.size || f.rec[0x11A] != 14 || f.hp() > budget) continue;
            budget -= f.hp();
            if (e.affect) give_aff(f, e.affect, 0, pw, false);
            say(c, Did::Word, 0);
        }
        break;
    }
    case SpellDoes::CureBlind:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            const int at = b.fx && b.fx->blinded ? find_aff(f, b.fx->blinded) : -1;
            if (at < 0) continue;
            drop_aff(f, at);
            say(who[k], Did::Word2, 0);
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Sleep: {
        int budget = d.roll(4, 4);
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            const int cost = sleep_cost(f);
            if (!f.up() || f.status() == party::Animated || (e.affect && f.has(static_cast<uint8_t>(e.affect))) ||
                cost > budget)
                continue;
            budget -= cost;
            if (e.affect) give_aff(f, e.affect, minutes, pw, false);
            if (fs.word) say(who[k], Did::Word, 0);
        }
        break;
    }
    }
    return lines;
}

int bolt_line(const Battle& b, const Tables& t, int caster, int tx, int ty, int len, int* out, int cap)
{
    const Fighter& me = b.f[caster];
    int ddx = tx - me.x, ddy = ty - me.y;
    if (!ddx && !ddy) return 0;
    const int dir = direction(me.x, me.y, tx, ty);
    int n = 0;
    int x = tx, y = ty;
    for (int k = 0; k < len && n < cap; ++k) {
        if (x < 0 || y < 0 || x >= kW || y >= kH || b.ground[y][x] == 0 || tile(b, t, x, y)[0] == 0xFF) break;
        const int w = b.who[y][x];
        if (w) {
            bool seen = false;
            for (int j = 0; j < n; ++j)
                if (out[j] == w - 1) seen = true;
            if (!seen) out[n++] = w - 1;
        }
        x += kDx[dir];
        y += kDy[dir];
    }
    return n;
}

const FightSpell* fight_spell(const FightSpell* table, int n, int spell)
{
    for (int i = 0; table && i < n; ++i)
        if (table[i].spell == spell) return &table[i];
    return nullptr;
}

bool spell_targets(Battle& b, const Tables& t, const classes::Tables& st, int i, const FightSpell& fs, int sp, int pw,
                   create::Dice& d, int* targets, int* n_targets)
{
    *n_targets = 0;
    Fighter& f = b.f[i];
    if (fs.does == SpellDoes::NotYet) return false;
    const spells::Entry e = spells::entry(st, sp);
    const int my = f.team() ? 1 : 0;
    int reach = e.range == -1 ? 1 : e.range + e.range_level * pw;
    if (reach < 1) reach = 1;
    const int aim = e.aim & 0x0F;
    // For their own good
    if (fs.does == SpellDoes::Heal) {
        if (f.hp() * 2 >= f.hp_max()) return false;
        targets[(*n_targets)++] = i;
        return true;
    }
    if (e.targets == spells::kParty && fs.does != SpellDoes::Theirs) {
        // Their whole side
        if (e.affect && f.has(static_cast<uint8_t>(e.affect))) return false;
        for (int c = 0; c < b.n && *n_targets < kMaxFighters; ++c)
            if (b.f[c].up() && b.f[c].size && (b.f[c].team() ? 1 : 0) == my) targets[(*n_targets)++] = c;
        return *n_targets > 0;
    }
    if (aim == 0 || fs.does == SpellDoes::Ours || fs.does == SpellDoes::Prayer || fs.does == SpellDoes::Mirror ||
        fs.does == SpellDoes::Haste) {
        if (e.affect && f.has(static_cast<uint8_t>(e.affect))) return false;
        if (aim >= 8 && aim <= 14) {
            *n_targets = in_area(b, t, f.x, f.y, e.aim & 7, targets, kMaxFighters);
        } else {
            targets[(*n_targets)++] = i;
        }
        return true;
    }
    // Against the enemy: one in reach and sight
    int cand[kMaxFighters], nc = 0;
    for (int c = 0; c < b.n; ++c) {
        const Fighter& o = b.f[c];
        int sq;
        if (!o.up() || !o.size || (o.team() ? 1 : 0) == my || (b.fx && o.has(b.fx->invisible))) continue;
        if (!range(b, t, i, c, false, &sq) || sq > reach) continue;
        cand[nc++] = c;
    }
    if (!nc) return false;
    const int tg = cand[d.roll(nc, 1) - 1];
    if (fs.does == SpellDoes::Bolt) {
        *n_targets = bolt_line(b, t, i, b.f[tg].x, b.f[tg].y, 7, targets, kMaxFighters);
    } else if ((aim >= 8 && aim <= 14) || fs.does == SpellDoes::Cloud) {
        int in[kMaxFighters];
        const int ni = fs.does == SpellDoes::Cloud ? cloud_fighters(b, b.f[tg].x, b.f[tg].y, in, kMaxFighters)
                                                   : in_area(b, t, b.f[tg].x, b.f[tg].y, e.aim & 7, in, kMaxFighters);
        for (int k = 0; k < ni; ++k)
            if ((b.f[in[k]].team() ? 1 : 0) == my && !saving_throw(b.f[in[k]], e.save, my ? 8 : -2, d)) return false;
        for (int k = 0; k < ni; ++k) targets[(*n_targets)++] = in[k];
    } else {
        const int want = aim >= 1 && aim <= 4 ? (e.aim & 3) + 1 : 1;
        targets[(*n_targets)++] = tg;
        for (int k = 0; k < nc && *n_targets < want; ++k)
            if (cand[k] != tg) targets[(*n_targets)++] = cand[k];
    }
    return *n_targets > 0;
}

int choose_spell(Battle& b, const Tables& t, const classes::Tables& st, int i, const FightSpell* table, int n_table,
                 create::Dice& d, int* targets, int* n_targets)
{
    *n_targets = 0;
    Fighter& f = b.f[i];
    uint8_t list[84];
    int n = 0;
    for (int k = 0; k < 84; ++k) {
        const uint8_t v = f.rec[0x1E + k];
        if (v && !(v & 0x80)) list[n++] = v;
    }
    if (!n) return 0;
    const int rounds = d.roll(7, 1);
    for (int r = 0, level = 7; r < rounds; ++r, --level)
        for (int pick = 0; pick < 3; ++pick) {
            const int sp = list[d.roll(n, 1) - 1];
            const FightSpell* fs = fight_spell(table, n_table, sp);
            if (!fs || fs->does == SpellDoes::NotYet) continue;
            const spells::Entry e = spells::entry(st, sp);
            if (e.when == 0 || e.priority < level) continue;
            if (spell_targets(b, t, st, i, *fs, sp, power_of(f.rec, st, sp), d, targets, n_targets)) return sp;
        }
    *n_targets = 0;
    return 0;
}

int choose_item(Battle& b, const Tables& t, const classes::Tables& st, int i, const items::Names& names,
                const FightSpell* table, int n_table, create::Dice& d, int* targets, int* n_targets)
{
    *n_targets = 0;
    Fighter& f = b.f[i];
    if (!f.items || !f.n_items) return -1;
    const int rounds = d.roll(7, 1);
    for (int r = 0, level = 7; r < rounds; ++r, --level)
        for (int k = 0; k < f.n_items; ++k) {
            const uint8_t* it = f.items[k];
            const int slot = names.type(it[0x2E]).slot;
            if (!it[0x34] || (slot >= 11 && slot <= 13) || it[0x3E] >= 0x80 || !it[0x3D]) continue;
            const int sp = it[0x3D] & 0x7F;
            const FightSpell* fs = fight_spell(table, n_table, sp);
            if (!fs || fs->does == SpellDoes::NotYet) continue;
            const int judge = sp > 0x38 ? sp - 0x17 : sp;
            const spells::Entry e = spells::entry(st, judge);
            if (spells::entry(st, sp).when == 0 || e.priority < level) continue;
            if (spell_targets(b, t, st, i, *fs, sp, power_of(f.rec, st, sp, true), d, targets, n_targets)) return k;
        }
    *n_targets = 0;
    return -1;
}

Morale morale(Battle& b, const Tables& t, int i)
{
    Fighter& f = b.f[i];
    if (f.rec[kControl] < 0x80) return Morale::Fight;
    int m = (f.rec[kControl] & 0x7F) * 2;
    if (m > 102) m = 0;
    int adj = 0;
    if (b.fx && f.has(b.fx->bless)) adj += 5;
    if (b.fx && f.has(b.fx->curse)) adj -= 5;
    m += adj;
    const int lost = f.hp_max() ? 100 - f.hp() * 100 / f.hp_max() : 0;
    if (!(m < lost || m == 0)) return Morale::Fight;
    const int m2 = b.enemy_health + adj;
    if (!(m2 < 100 - b.morale_base || m2 == 0 || !f.team())) return Morale::Fight;
    int fastest = 0;
    for (int c = 0; c < b.n; ++c) {
        const Fighter& o = b.f[c];
        if (o.up() && o.size && o.team() != f.team() && o.rec[kMove] > fastest) fastest = o.rec[kMove];
    }
    (void)t;
    if (fastest <= f.rec[kMove]) return Morale::Flee;
    if (f.rec[0x13] > 5) return Morale::Surrender;
    return Morale::Fight;
}

int turn_undead(Battle& b, const Tables& t, int cleric, create::Dice& d, int* out, int cap)
{
    Fighter& me = b.f[cleric];
    me.turned_undead = true;
    const int lv = me.rec[0x109];
    const int col = lv >= 1 && lv <= 8 ? lv : lv <= 13 ? 9 : 10;
    int count = d.roll(12, 1);
    const int roll = d.roll(20, 1);
    int extra = 6, n = 0;
    const int my = me.team() ? 1 : 0;
    while (count > 0 && n < cap) {
        // The weakest undead in sight
        int best = -1, type = 13;
        for (int i = 0; i < b.n; ++i) {
            const Fighter& f = b.f[i];
            const int ut = f.rec[0xE9];
            if (!f.up() || !f.size || f.fleeing || (f.team() ? 1 : 0) == my || ut == 0 || ut >= type) continue;
            if (!range(b, t, cleric, i, false, nullptr)) continue;
            best = i;
            type = ut;
        }
        if (best < 0) break;
        const int v = static_cast<int8_t>(t.turn[type * 10 + col]);
        if (roll < (v < 0 ? -v : v)) break;
        Fighter& f = b.f[best];
        if (v > 0) {
            f.fleeing = true;
            out[n++] = best;
        } else {
            f.rec[kHealth] = party::Gone;
            f.rec[kInCombat] = 0;
            f.size = 0;
            occupancy(b);
            out[n++] = best + 1000;
        }
        if (extra > 0) --extra;
        if (--count == 0 && extra > 0 && v <= 0) ++count;
    }
    return n;
}

Outcome finish(Battle& b, const Monster* monsters)
{
    (void)monsters;
    Outcome o;
    bool fled = false, killed = true, won = false;
    for (int i = 0; i < b.party_size; ++i) {
        const Fighter& f = b.f[i];
        const int s = f.status();
        if (s == party::Running) fled = true;
        if ((s == party::Running || s == party::Animated || s == party::Okay) && f.rec[kControl] < 0x80) killed = false;
        if (s == party::Okay || s == party::Animated) won = true;
    }
    if (won) fled = false;
    o.result = killed ? Lost : fled ? Fled : Won;
    // The beaten enemies: experience and their coins
    for (int i = b.party_size; i < b.n; ++i) {
        const Fighter& f = b.f[i];
        if (f.gone || !f.team() || f.status() == party::Okay || f.status() == party::Running) continue;
        const int base = static_cast<int16_t>(f.rec[0x13C] | f.rec[0x13D] << 8);
        o.exp += f.rec[0x13E] * f.rec[0x12C] + base;
        for (int m = 0; m < 7; ++m) o.money[m] += f.rec[0xFB + m * 2] | f.rec[0xFC + m * 2] << 8;
    }
    return o;
}

// ---- Pictures in flight
namespace {
template <size_t N> bool listed(const uint8_t (&l)[N], int v)
{
    for (uint8_t x : l)
        if (x == v) return true;
    return false;
}
} // namespace

Shot shot_kind(const Facts& fx, int t)
{
    if (listed(fx.shot_pointed, t)) return Shot::Pointed;
    if (listed(fx.shot_spinning, t)) return Shot::Spinning;
    if (listed(fx.shot_flask, t)) return Shot::Flask;
    if (listed(fx.shot_sling, t)) return Shot::Sling;
    return Shot::Rock;
}

Flight spell_flight(int pic, int delay)
{
    Flight fl;
    fl.pic = static_cast<uint8_t>(pic);
    fl.frames = 4;
    fl.delay = static_cast<uint8_t>(delay);
    // ready, ready mirrored, attack mirrored, attack
    fl.slot[0] = 0;
    fl.slot[1] = 2;
    fl.slot[2] = 3;
    fl.slot[3] = 1;
    return fl;
}

Flight shot_flight(const Battle& b, const Fighter& f, int ammo, int dir)
{
    Shot kind = Shot::Rock;
    if (b.fx && b.names && f.items) {
        const int w = weapon(f, *b.names);
        if (w >= 0 && shot_kind(*b.fx, f.items[w][0x2E]) == Shot::Sling) kind = Shot::Sling;
        else if (ammo >= 0 && ammo < f.n_items) kind = shot_kind(*b.fx, f.items[ammo][0x2E]);
        else if (w >= 0) kind = shot_kind(*b.fx, f.items[w][0x2E]);
    }
    Flight fl;
    switch (kind) {
    case Shot::Pointed:
        // One picture by direction: up / slanted / across, the attack picture
        // for the other way, mirrored for the slants to the west
        fl.frames = 1;
        fl.delay = 10;
        fl.sound = 0x0C;
        if (dir & 1) {
            fl.pic = 1;
            fl.slot[0] = static_cast<uint8_t>((dir == 3 || dir == 5 ? 1 : 0) | (dir == 5 || dir == 7 ? 2 : 0));
        } else {
            fl.pic = static_cast<uint8_t>(dir & 3);
            fl.slot[0] = dir >= 4 ? 1 : 0;
        }
        break;
    case Shot::Spinning:
        fl = spell_flight(3, 50);
        fl.sound = 9;
        break;
    case Shot::Flask:
        fl = spell_flight(4, 50);
        fl.sound = 6;
        break;
    case Shot::Sling:
    case Shot::Rock:
        fl.pic = kind == Shot::Sling ? 8 : 7;
        fl.frames = 2;
        fl.slot[1] = 1;
        fl.delay = kind == Shot::Sling ? 10 : 20;
        fl.sound = kind == Shot::Sling ? 6 : 9;
        break;
    }
    return fl;
}

void flight_begin(FlightPath& p, int x0, int y0, int x1, int y1)
{
    p.x = x0 * 3;
    p.y = y0 * 3;
    p.tx = x1 * 3;
    p.ty = y1 * 3;
    p.ax = p.tx > p.x ? p.tx - p.x : p.x - p.tx;
    p.ay = p.ty > p.y ? p.ty - p.y : p.y - p.ty;
    p.sx = p.tx > p.x ? 1 : p.tx < p.x ? -1 : 0;
    p.sy = p.ty > p.y ? 1 : p.ty < p.y ? -1 : 0;
    p.err = 0;
    const int steps = p.ax > p.ay ? p.ax : p.ay;
    p.left = steps - 1 >= 2 ? steps - 1 : 0;
}

bool flight_step(FlightPath& p)
{
    if (p.left <= 0) return false;
    --p.left;
    // One cell along the longer axis, the shorter one when its share adds up
    if (p.ax >= p.ay) {
        p.x += p.sx;
        p.err += 2 * p.ay;
        if (p.err >= p.ax) {
            p.y += p.sy;
            p.err -= 2 * p.ax;
        }
    } else {
        p.y += p.sy;
        p.err += 2 * p.ax;
        if (p.err >= p.ay) {
            p.x += p.sx;
            p.err -= 2 * p.ay;
        }
    }
    return true;
}

// ---- The computer's weapon
int weapon_rating(const items::Names& names, const uint8_t* it, int hands_used)
{
    const items::TypeInfo& ti = names.type(it[0x2E]);
    int r = ti.dice * ti.sides;
    const int plus = static_cast<int8_t>(it[0x32]);
    if (plus > 0) r += 8 * plus;
    if (ti.bonus > 0) r += 2 * ti.bonus;
    if (ti.flags & 0x08) r += (ti.attacks - 1) * 2;
    if (ti.hands <= 1) r += 3;
    if (ti.hands + hands_used > 3 || it[0x36]) r = 0;
    return r;
}

namespace {
bool enemy_next_to(const Battle& b, int i)
{
    for (int k = 0; k < b.n; ++k)
        if (k != i && b.f[k].up() && b.f[k].size && b.f[k].team() != b.f[i].team() && adjacent(b, i, k)) return true;
    return false;
}
bool thrown_melee(const items::TypeInfo& ti) { return ti.range > 1 && (ti.flags & 0x14) == 0x14; }
} // namespace

bool missile_in_melee(const Battle& b, int i)
{
    const Fighter& f = b.f[i];
    if (!b.names || !f.items) return false;
    const int w = weapon(f, *b.names);
    if (w < 0) return false;
    const items::TypeInfo& ti = b.names->type(f.items[w][0x2E]);
    return ti.range > 1 && (ti.flags & 0x08) && !thrown_melee(ti) && enemy_next_to(b, i);
}

int choose_weapon(const Battle& b, int i)
{
    const Fighter& f = b.f[i];
    if (!b.names || !f.items) return kKeep;
    const items::Names& n = *b.names;
    const uint8_t* r = f.rec;
    // The hands besides the weapon and shield held now
    int hands = r[0x185];
    for (int k = 0; k < f.n_items; ++k) {
        if (!f.items[k][0x34]) continue;
        const items::TypeInfo& ti = n.type(f.items[k][0x2E]);
        if (ti.slot == items::kSlotWeapon || ti.slot == 1) hands -= ti.hands;
    }
    if (hands < 0) hands = 0;
    int best_missile = -1, missile_r = 1, best_melee = -1;
    int melee_r = r[0x11E] * r[0x120] + (static_cast<int8_t>(r[0x122]) > 0 ? 2 * static_cast<int8_t>(r[0x122]) : 0);
    for (int k = 0; k < f.n_items; ++k) {
        const items::TypeInfo& ti = n.type(f.items[k][0x2E]);
        if (ti.slot != items::kSlotWeapon || !(ti.classes & r[0x12B])) continue;
        const int rating = weapon_rating(n, f.items[k], hands);
        if ((ti.flags & 0x18) && rating > missile_r) {
            best_missile = k;
            missile_r = rating;
        }
        if (!(ti.flags & 0x08) && rating > melee_r) {
            best_melee = k;
            melee_r = rating;
        }
    }
    int want = best_melee;
    if (best_missile >= 0 && missile_r > melee_r / 2) {
        const items::TypeInfo& ti = n.type(f.items[best_missile][0x2E]);
        // What it shoots: itself (thrown), its readied arrows / quarrels, or nothing (a sling)
        bool loaded = (ti.flags & 0x10) != 0 || (ti.flags & 0xFF) == 0x0A;
        for (int k = 0; !loaded && k < f.n_items; ++k) {
            if (!f.items[k][0x34]) continue;
            const int t = f.items[k][0x2E];
            if (((ti.flags & 0x01) && t == b.arrow) || ((ti.flags & 0x80) && t == b.quarrel)) loaded = true;
        }
        if (loaded && (thrown_melee(ti) || !enemy_next_to(b, i))) want = best_missile;
    }
    const int held = weapon(f, n);
    if (held == want || (held >= 0 && f.items[held][0x36])) return kKeep;
    return want;
}

// ---- Clouds that stay on the field
namespace {
// A cloud's squares: Stinking Cloud 2 x 2 from its square, Cloudkill it and the 8 round it
constexpr int kCloudDx[4] = {0, 1, 1, 0}, kCloudDy[4] = {0, 0, 1, 1};
int cloud_squares(bool poison) { return poison ? 9 : 4; }
int sq_dx(bool poison, int k) { return poison ? kDx[(k + 8) % 9] : kCloudDx[k]; }
int sq_dy(bool poison, int k) { return poison ? kDy[(k + 8) % 9] : kCloudDy[k]; }
// What the ground under a cloud square really is (another cloud's memory)
int ground_under(const Battle& b, int x, int y)
{
    const int g = b.ground[y][x];
    if (g != kCloudGround && g != kPoisonGround) return g;
    for (int c = 0; c < b.n_clouds; ++c) {
        const Battle::Cloud& cl = b.clouds[c];
        for (int k = 0; k < cloud_squares(cl.poison); ++k)
            if ((cl.present >> k & 1) && cl.x + sq_dx(cl.poison, k) == x && cl.y + sq_dy(cl.poison, k) == y)
                return cl.ground[k];
    }
    return g;
}
} // namespace

bool in_cloud(const Battle& b, int x, int y) { return on_field(x, y) && b.ground[y][x] == kCloudGround; }
bool in_poison(const Battle& b, int x, int y) { return on_field(x, y) && b.ground[y][x] == kPoisonGround; }

int cloud_fighters(const Battle& b, int x, int y, int* out, int cap, bool poison)
{
    int n = 0;
    for (int k = 0; k < cloud_squares(poison); ++k) {
        const int xx = x + sq_dx(poison, k), yy = y + sq_dy(poison, k);
        if (!on_field(xx, yy) || !b.who[yy][xx]) continue;
        const int f = b.who[yy][xx] - 1;
        bool seen = false;
        for (int j = 0; j < n; ++j) seen = seen || out[j] == f;
        if (!seen && n < cap) out[n++] = f;
    }
    return n;
}

bool lay_cloud(Battle& b, const Tables& t, int x, int y, int rounds, bool poison)
{
    if (b.n_clouds >= Battle::kMaxClouds) return false;
    Battle::Cloud& cl = b.clouds[b.n_clouds];
    cl = Battle::Cloud{};
    cl.x = static_cast<uint8_t>(x);
    cl.y = static_cast<uint8_t>(y);
    cl.poison = poison;
    cl.rounds = static_cast<uint8_t>(rounds < 1 ? 1 : rounds > 255 ? 255 : rounds);
    for (int k = 0; k < cloud_squares(poison); ++k) {
        const int xx = x + sq_dx(poison, k), yy = y + sq_dy(poison, k);
        if (!on_field(xx, yy) || !b.ground[yy][xx] || tile(b, t, xx, yy)[0] == 0xFF) continue;
        cl.ground[k] = static_cast<uint8_t>(ground_under(b, xx, yy));
        cl.present = static_cast<uint16_t>(cl.present | 1 << k);
    }
    for (int k = 0; k < cloud_squares(poison); ++k)
        if (cl.present >> k & 1) b.ground[y + sq_dy(poison, k)][x + sq_dx(poison, k)] = poison ? kPoisonGround : kCloudGround;
    ++b.n_clouds;
    return true;
}

Did breathe_cloud(Battle& b, int i, create::Dice& d)
{
    Fighter& f = b.f[i];
    if (!f.up() || helpless(b, f)) return Did::Unaffected;
    if (saving_throw(f, 0, 0, d)) {
        if (b.fx && b.fx->cough) give_aff(f, b.fx->cough, 1, 0, false);      // coughing for a round
        return Did::Word2;
    }
    if (b.fx) give_aff(f, b.fx->held[3], d.roll(4, 1) + 1, 0, false);
    return Did::Word;
}

bool breathe_poison(Battle& b, int i, create::Dice& d)
{
    Fighter& f = b.f[i];
    if (!f.up()) return false;
    const int hd = f.rec[0xE5];
    bool dies = false;
    if (hd <= 4) dies = true;
    else if (hd == 5) dies = !saving_throw(f, 0, -4, d);
    else if (hd == 6) dies = !saving_throw(f, 0, 0, d);
    if (!dies) return false;
    damage(b, i, f.hp() + 10);                       // dead
    return true;
}

int clouds_round(Battle& b)
{
    int gone = 0;
    for (int c = 0; c < b.n_clouds;) {
        Battle::Cloud& cl = b.clouds[c];
        if (cl.rounds > 1) {
            --cl.rounds;
            ++c;
            continue;
        }
        // It clears: the ground back (a body where a fallen party member lies)
        for (int k = 0; k < cloud_squares(cl.poison); ++k) {
            if (!(cl.present >> k & 1)) continue;
            const int xx = cl.x + sq_dx(cl.poison, k), yy = cl.y + sq_dy(cl.poison, k);
            b.ground[yy][xx] = cl.ground[k];
            for (int j = 0; j < b.n; ++j) {
                Fighter& o = b.f[j];
                if (o.member >= 0 && !o.size && !o.up() && o.x == xx && o.y == yy) {
                    o.ground = cl.ground[k];
                    b.ground[yy][xx] = 0x1F;
                }
            }
        }
        for (int j = c; j + 1 < b.n_clouds; ++j) b.clouds[j] = b.clouds[j + 1];
        --b.n_clouds;
        ++gone;
    }
    // The clouds still there stay cloud where they overlapped the one gone
    for (int c = 0; gone && c < b.n_clouds; ++c) {
        const Battle::Cloud& cl = b.clouds[c];
        for (int k = 0; k < cloud_squares(cl.poison); ++k)
            if (cl.present >> k & 1)
                b.ground[cl.y + sq_dy(cl.poison, k)][cl.x + sq_dx(cl.poison, k)] = cl.poison ? kPoisonGround : kCloudGround;
    }
    return gone;
}

// ---- Cones
namespace {
// The squares of a straight line from (x0, y0) to (x1, y1), the first left out
template <typename F> void line_squares(int x0, int y0, int x1, int y1, F each)
{
    const int ax = x1 > x0 ? x1 - x0 : x0 - x1, ay = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x1 > x0 ? 1 : x1 < x0 ? -1 : 0, sy = y1 > y0 ? 1 : y1 < y0 ? -1 : 0;
    int x = x0, y = y0, err = 0;
    while (x != x1 || y != y1) {
        int dir_x = 0, dir_y = 0;
        if (ax >= ay) {
            x += sx;
            dir_x = sx;
            err += 2 * ay;
            if (err >= ax) {
                y += sy;
                dir_y = sy;
                err -= 2 * ax;
            }
        } else {
            y += sy;
            dir_y = sy;
            err += 2 * ax;
            if (err >= ay) {
                x += sx;
                dir_x = sx;
                err -= 2 * ay;
            }
        }
        if (!each(x, y, dir_x, dir_y)) return;
    }
}
} // namespace

int cone(const Battle& b, const Tables& t, int caster, int tx, int ty, int reach, int rays, int* out, int cap)
{
    const Fighter& me = b.f[caster];
    const int x0 = me.x, y0 = me.y;
    if (tx == x0 && ty == y0) return 0;
    // The steps from the caster to the target, then the same steps on
    int sdx[64], sdy[64], ns = 0, cost = 0;
    line_squares(x0, y0, tx, ty, [&](int, int, int dx2, int dy2) {
        if (ns < 64) {
            sdx[ns] = dx2;
            sdy[ns] = dy2;
            ++ns;
        }
        cost += dx2 && dy2 ? 3 : 2;
        return true;
    });
    int ex = tx, ey = ty;
    for (int k = 0; ns && cost < reach * 2; k = (k + 1) % ns) {
        const int nx = ex + sdx[k], ny = ey + sdy[k];
        if (nx <= 0 || nx >= kW - 1 || ny <= 0 || ny >= kH - 1) break;
        ex = nx;
        ey = ny;
        cost += sdx[k] && sdy[k] ? 3 : 2;
    }
    // No further than the last square before a wall
    int lx = x0, ly = y0, last_dir = 8;
    line_squares(x0, y0, ex, ey, [&](int x, int y, int dx2, int dy2) {
        if (!on_field(x, y) || tile(b, t, x, y)[0] == 0xFF) return false;
        lx = x;
        ly = y;
        for (int dd = 0; dd < 8; ++dd)
            if (kDx[dd] == dx2 && kDy[dd] == dy2) last_dir = dd;
        return true;
    });
    if (lx == x0 && ly == y0) return 0;
    int n = 0;
    auto take = [&](int x, int y) {
        if (!on_field(x, y) || !b.who[y][x]) return;
        const int f = b.who[y][x] - 1;
        if (f == caster) return;
        for (int j = 0; j < n; ++j)
            if (out[j] == f) return;
        if (n < cap) out[n++] = f;
    };
    auto ray = [&](int x1, int y1) {
        if (x1 < 0) x1 = 0;
        if (x1 >= kW) x1 = kW - 1;
        if (y1 < 0) y1 = 0;
        if (y1 >= kH) y1 = kH - 1;
        line_squares(x0, y0, x1, y1, [&](int x, int y, int, int) {
            take(x, y);
            return true;
        });
    };
    ray(lx, ly);
    if (last_dir < 8) {
        const int right = ((last_dir & ~1) + 2) & 7, left = (((last_dir + 1) & ~1) + 6) & 7;
        if (rays >= 2) ray(lx + kDx[right], ly + kDy[right]);
        if (rays >= 3) ray(lx + kDx[left], ly + kDy[left]);
    }
    return n;
}

} // namespace combat

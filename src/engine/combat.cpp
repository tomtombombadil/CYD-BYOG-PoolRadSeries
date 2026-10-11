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

// A prayer's reach: its holder's effect counts for those within 6 squares
// of it (coab's facts: calc_affect_effect - the prayer's range 6, in sight);
// the holder itself always (no field's tables: everyone, as before)
bool prayer_reaches(const Battle& b, int holder, int c)
{
    if (holder == c || !b.tables) return true;
    const Fighter& h = b.f[holder];
    const Fighter& f = b.f[c];
    if (!h.size || !f.size) return false;
    int len = 0;
    return path_xy(b, *b.tables, h.x, h.y, f.x, f.y, false, &len) && len <= 2 * 6 + 1;
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

namespace {
const Facts* g_fx = nullptr;         // the fight's effects, for saving throws (set as rounds start)
const Battle* g_b = nullptr;         // the fight (saving throws: protection from evil next to them)
create::Dice* g_dice = nullptr;      // (a troll's fall: its rounds before it gets up)
int g_kind = 0;                      // the damage's kind now (saving throws: resist fire / cold)
int g_actor = -1;                    // whose turn it is (saving throws: protection from evil / good)

bool hasx(const Fighter& f, uint8_t type) { return type && f.has(type); }
} // namespace

void set_actor(const Battle& b, int i)
{
    g_b = &b;
    g_fx = b.fx;
    g_actor = i;
}

bool hidden(const Battle& b, int viewer, int target)
{
    if (!b.fx || target < 0 || target >= b.n) return false;
    const Fighter& t = b.f[target];
    if (hasx(t, b.fx->blink) && t.delay == 0) return true;
    const bool sees = viewer >= 0 && viewer < b.n && hasx(b.f[viewer], b.fx->mon.detect);
    // Invisibility to Animals: an animal (monster type 19) doesn't see it
    if (b.fx->animals_blind && viewer >= 0 && viewer < b.n && b.f[viewer].rec[0x11A] == 19 &&
        t.has(b.fx->animals_blind) && !sees)
        return true;
    if (!hasx(t, b.fx->invisible)) return false;
    return !sees;
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

namespace {

// A hold let go: the holder's effect goes, and its victim (the effect's
// data) may move again (and breathe: engulfed)
void let_go(Battle& b, Fighter& holder, uint8_t type)
{
    const int k = type ? find_aff(holder, type) : -1;
    if (k < 0) return;
    const int v = holder.aff[k][3];
    drop_aff(holder, k);
    if (!b.fx || v >= b.n) return;
    Fighter& vf = b.f[v];
    int j = find_aff(vf, b.fx->mon.held_fast);
    if (b.fx->mon.held_fast && j >= 0) drop_aff(vf, j);
    j = find_aff(vf, b.fx->mon.suffocate);
    if (type == b.fx->mon.engulfing && b.fx->mon.suffocate && j >= 0) drop_aff(vf, j);
}

// Out of the fight (down, stoned, gone): off the field (a party member
// leaves a body), the holds it had let go, a troll's regeneration stops -
// and fallen to anything but fire or acid, it gets up again later
void fall(Battle& b, int c)
{
    Fighter& f = b.f[c];
    f.rec[kInCombat] = 0;
    f.delay = 0;
    f.moves = 0;
    f.attacks[0] = f.attacks[1] = 0;
    f.guarding = false;
    if (f.size) {
        f.down_size = f.size;
        f.size = 0;
        if (f.member >= 0 && on_field(f.x, f.y) && b.ground[f.y][f.x] != 0x1E && b.ground[f.y][f.x] != 0x1C) {
            f.ground = b.ground[f.y][f.x];
            b.ground[f.y][f.x] = kBody;
        }
        occupancy(b);
    }
    if (!b.fx) return;
    const MonFx& m = b.fx->mon;
    let_go(b, f, m.engulfing);
    let_go(b, f, m.hugging);
    const uint8_t regen[2] = {m.regen_wait, m.regen};
    for (uint8_t t : regen) {
        const int k = t ? find_aff(f, t) : -1;
        if (k >= 0) drop_aff(f, k);
    }
    if (hasx(f, m.troll_death) && m.troll_up && !(g_kind & 0x11) && f.status() != party::Stoned &&
        f.status() != party::Gone)
        give_aff(f, m.troll_up, g_dice ? g_dice->roll(6, 3) : 10, 0xFF, true);
}

} // namespace

void uncharm(uint8_t* rec, const uint8_t* a) { rec[0x197] = static_cast<uint8_t>((a[3] & 0x40) >> 6); }

namespace {

// An effect taken away (run out, dispelled): what its going undoes - a
// charm's side, fear's flight, going berserk
void end_effect(Battle& b, Fighter& f, int k)
{
    const uint8_t type = f.aff[k][0], data = f.aff[k][3];
    if (b.fx) {
        const Facts& fx = *b.fx;
        if (fx.charm && type == fx.charm) {
            uncharm(f.rec, f.aff[k]);           // back to their own side
            f.target = -1;
        }
        if (fx.fear && type == fx.fear) {
            f.fleeing = false;                  // the fear is over
            if (data & 1) f.quick = false;
        }
        if (fx.sp.berserk && type == fx.sp.berserk) {
            f.rec[0x197] = static_cast<uint8_t>(data & 1);     // its own side again
            if (data & 0x10) f.quick = false;
            f.target = -1;
        }
        // A hold taken away (dispelled): its victim (the data) goes free
        if ((fx.mon.engulfing && type == fx.mon.engulfing) || (fx.mon.hugging && type == fx.mon.hugging)) {
            if (data < b.n) {
                Fighter& vf = b.f[data];
                int j = fx.mon.held_fast ? find_aff(vf, fx.mon.held_fast) : -1;
                if (j >= 0) drop_aff(vf, j);
                j = fx.mon.suffocate && type == fx.mon.engulfing ? find_aff(vf, fx.mon.suffocate) : -1;
                if (j >= 0) drop_aff(vf, j);
            }
        }
        // Animate Dead taken away: they collapse, dead, on their own side
        if (fx.sp.animated && type == fx.sp.animated && f.status() == party::Animated) {
            drop_aff(f, k);
            f.rec[kHealth] = party::Dead;
            f.rec[kHp] = 0;
            f.rec[kTeam] = static_cast<uint8_t>(data >> 4);
            f.rec[0xE9] = 0;
            f.rec[0x11A] = 0;
            f.rec[kMove] = 12;
            f.rec[kControl] = f.was_control;
            f.target = -1;
            fall(b, static_cast<int>(&f - b.f));
            return;
        }
    }
    drop_aff(f, k);
}

// Back on its feet on its own square (a troll, the slow-poisoned, the
// animated): all its hit points when `full`; false when the square isn't free
bool stand_up(Battle& b, int i, bool full)
{
    Fighter& f = b.f[i];
    if (f.status() == party::Stoned || f.status() == party::Gone || f.gone) return false;
    int sx[4], sy[4];
    const int sq = squares(f.down_size ? f.down_size : 1, sx, sy);
    for (int q = 0; q < sq; ++q) {
        const int x = f.x + sx[q], y = f.y + sy[q];
        if (!on_field(x, y) || b.ground[y][x] == 0 || (b.who[y][x] && b.who[y][x] != i + 1)) return false;
    }
    f.size = f.down_size ? f.down_size : 1;
    if (full) f.rec[kHp] = f.rec[kHpMax];
    f.rec[kHealth] = party::Okay;
    f.rec[kInCombat] = 1;
    f.bleeding = 0;
    // A party member's body goes from the ground
    if (f.member >= 0 && on_field(f.x, f.y) && b.ground[f.y][f.x] == kBody && f.ground) b.ground[f.y][f.x] = static_cast<uint8_t>(f.ground);
    occupancy(b);
    return true;
}

} // namespace

TurnFx turn_effects(Battle& b, int i)
{
    Fighter& f = b.f[i];
    if (!b.fx || !f.up()) return TurnFx::None;
    set_actor(b, i);
    g_kind = 0;
    const Facts& fx = *b.fx;
    // Engulfed: a turn's breath less; none left - suffocated
    const int sf = fx.mon.suffocate ? find_aff(f, fx.mon.suffocate) : -1;
    if (sf >= 0) {
        if (f.aff[sf][3] == 0) {
            f.rec[kHp] = 0;
            f.rec[kHealth] = party::Dead;
            fall(b, i);
            return TurnFx::Suffocates;
        }
        --f.aff[sf][3];
    }
    if (fx.fumbling && f.has(fx.fumbling)) {
        f.moves = f.attacks[0] = f.attacks[1] = 0;
        return TurnFx::Fumbling;
    }
    // (the original's order: the restrained - snakes -, silence, then confusion)
    const int s = fx.sticks ? find_aff(f, fx.sticks) : -1;
    if (s >= 0) {
        // More snakes than its attacks this round: they go down by its
        // attacks; else they're gone - either way it fights them this turn
        // (coab's facts, listing ovr013:00DC: "is fighting with snakes", the
        // actions cleared)
        const int att = f.attacks[0] + f.attacks[1];
        if (f.aff[s][3] > att) f.aff[s][3] = static_cast<uint8_t>(f.aff[s][3] - att);
        else drop_aff(f, s);
        f.moves = f.attacks[0] = f.attacks[1] = 0;
        return TurnFx::Snakes;
    }
    bool silenced = false;
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
            silenced = true;
        }
    }
    // Confused (no spell on the way): d100 - it runs, its turn is lost, it
    // goes berserk, or it's only enraged; then a save at -2 ends it
    const int cf = fx.sp.confuse && g_dice && !f.spell ? find_aff(f, fx.sp.confuse) : -1;
    if (cf >= 0) {
        create::Dice& d = *g_dice;
        const int r = d.roll(100, 1);
        const bool made_quick = f.member >= 0 && f.rec[kControl] < 0x80 && !f.quick;
        TurnFx out;
        if (r <= 10) {
            drop_aff(f, cf);
            if (fx.fear) give_aff(f, fx.fear, 10, made_quick ? 1 : 0, true);
            f.fleeing = true;
            f.quick = true;
            f.target = -1;
            return TurnFx::RunsAway;
        }
        if (r <= 60) {
            f.moves = f.attacks[0] = f.attacks[1] = 0;
            out = TurnFx::Confused;
        } else if (r <= 80) {
            // At the nearest creature, friend or foe: on the other side from it for a round
            int near = -1, best = 9999;
            for (int c = 0; c < b.n; ++c) {
                const Fighter& o = b.f[c];
                if (c == i || !o.up() || !o.size) continue;
                const int ddx = o.x > f.x ? o.x - f.x : f.x - o.x, ddy = o.y > f.y ? o.y - f.y : f.y - o.y;
                const int dist = ddx > ddy ? ddx : ddy;
                if (dist < best) {
                    best = dist;
                    near = c;
                }
            }
            if (fx.sp.berserk)
                give_aff(f, fx.sp.berserk, 1, (f.team() ? 1 : 0) | (made_quick ? 0x10 : 0), true);
            f.quick = true;
            if (near >= 0) {
                f.rec[0x197] = static_cast<uint8_t>(b.f[near].team() ? 0 : 1);
                f.target = near;
            }
            out = TurnFx::Berserk;
        } else {
            out = TurnFx::Enraged;
        }
        if (saving_throw(f, 4, -2, d)) {
            const int k = find_aff(f, fx.sp.confuse);
            if (k >= 0) drop_aff(f, k);
        }
        return out;
    }
    if (silenced) return TurnFx::Silenced;
    return TurnFx::None;
}

int tick(Battle& b, Event* out, int cap)
{
    int n = 0;
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        bool regen = false, rise = false, sting = false, slow_over = false, con_up = false;
        for (int k = 0; f.n_aff && k < *f.n_aff;) {
            const int m = f.aff[k][1] | f.aff[k][2] << 8;
            if (m == 0) {
                ++k;
            } else if (m <= 1) {
                if (b.fx && b.fx->mon.regen_wait && f.aff[k][0] == b.fx->mon.regen_wait) regen = true;
                if (b.fx && b.fx->mon.troll_up && f.aff[k][0] == b.fx->mon.troll_up) rise = true;
                if (b.fx && b.fx->sp.poison_damage && f.aff[k][0] == b.fx->sp.poison_damage) sting = true;
                if (b.fx && b.fx->sp.slow_poison && f.aff[k][0] == b.fx->sp.slow_poison) slow_over = true;
                if (b.fx && b.fx->sp.con_regen && f.aff[k][0] == b.fx->sp.con_regen) con_up = true;
                end_effect(b, f, k);
            } else {
                f.aff[k][1] = static_cast<uint8_t>(m - 1);
                f.aff[k][2] = static_cast<uint8_t>((m - 1) >> 8);
                ++k;
            }
        }
        if (!b.fx) continue;
        const MonFx& mf = b.fx->mon;
        // Slowed poison: a hit point every 10 minutes (down to 1); run out
        // while still poisoned - "dies from poison"
        if (sting && !slow_over && hasx(f, b.fx->sp.slow_poison)) {
            give_aff(f, b.fx->sp.poison_damage, 10, 0xFF, true);
            if (f.hp() > 1) f.rec[kHp] = static_cast<uint8_t>(f.hp() - 1);
        }
        if (slow_over && hasx(f, mf.poisoned)) {
            const int pd = find_aff(f, b.fx->sp.poison_damage);
            if (pd >= 0) drop_aff(f, pd);
            if (f.up()) {
                f.rec[kHp] = 0;
                f.rec[kHealth] = party::Dead;
                fall(b, i);
                if (n < cap) {
                    out[n] = Event{};
                    out[n].who = static_cast<uint8_t>(i);
                    out[n++].ev = Ev::DiesPoison;
                }
            }
        }
        // Constitution 20+: its 60 run out - a hit point back, and 60 more
        // (for the living, unconscious or dying who aren't at their most)
        if (con_up) {
            give_aff(f, b.fx->sp.con_regen, 60, 0xFF, true);
            const int st = f.status();
            if (f.hp() < f.hp_max() && (st == party::Okay || st == party::Animated || st == party::Unconscious ||
                                         st == party::Dying)) {
                f.rec[kHp] = static_cast<uint8_t>(f.hp() + 1);
                if (n < cap) {
                    out[n] = Event{};
                    out[n].who = static_cast<uint8_t>(i);
                    out[n++].ev = Ev::Regen;
                }
            }
        }
        // A troll: its wait over, it regenerates; fallen, it gets up again
        // with all its hit points when its square is free (else a round later)
        if (regen && mf.regen) give_aff(f, mf.regen, 0, 0xFF, false);
        if (rise) {
            // (a troll: all its hit points; the slow-poisoned: those they have)
            const bool full = !hasx(f, b.fx->sp.slow_poison);
            if (!f.up() && stand_up(b, i, full)) {
                if (n < cap) {
                    out[n].who = static_cast<uint8_t>(i);
                    out[n++].ev = f.team() ? Ev::StandsUp : Ev::GetsUp;
                }
            } else if (!f.up() && f.status() != party::Stoned && f.status() != party::Gone) {
                give_aff(f, mf.troll_up, 1, 0xFF, true);
            }
        }
    }
    // The round's end: regeneration, 3 hit points a round
    for (int i = 0; b.fx && b.fx->mon.regen && i < b.n; ++i) {
        Fighter& f = b.f[i];
        if (!f.up() || !f.has(b.fx->mon.regen)) continue;
        const int hp = f.hp() + 3;
        f.rec[kHp] = static_cast<uint8_t>(hp > f.hp_max() ? f.hp_max() : hp);
    }
    return n;
}

void battle_start(Battle& b)
{
    g_b = &b;
    g_fx = b.fx;
    if (!b.fx || !b.fx->invisible) return;
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        if (f.size && f.up() && hasx(f, b.fx->mon.start_invisible)) give_aff(f, b.fx->invisible, 255, 0xFF, false);
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
    g_b = &b;
    g_dice = &d;
    g_actor = -1;
    g_kind = 0;
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        f.spell = f.spell_n = 0;                // a spell begun last round is gone (the original's)
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
        if (b.fx && hasx(f, b.fx->mon.held_fast)) f.moves = 0;                 // engulfed, hugged
    }
    b.surprise = 0;
}

int next(Battle& b, create::Dice& d)
{
    int best = -1, best_delay = 0, best_roll = -1;
    for (int i = 0; i < b.n; ++i) {
        const Fighter& f = b.f[i];
        if (f.delay <= 0 || !f.up() || !f.size || helpless(b, f)) continue;      // the held lose their turns
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
    fall(b, c);
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

namespace {

// The item a fighter's blows count with (monster_fx_facts 2.3): the readied
// weapon - a bow's / crossbow's readied arrows / quarrels; none: natural
const uint8_t* blow_item(const Battle& b, const Fighter& f, const items::Names* names)
{
    if (!names) return nullptr;
    const int w = weapon(f, *names);
    if (w < 0) return nullptr;
    const items::TypeInfo& ti = names->type(f.items[w][0x2E]);
    if (ti.flags & 0x81)
        for (int k = 0; k < f.n_items; ++k) {
            const int ty = f.items[k][0x2E];
            if (f.items[k][0x34] && (((ti.flags & 0x01) && ty == b.arrow) || ((ti.flags & 0x80) && ty == b.quarrel)))
                return f.items[k];
        }
    return f.items[w];
}

// A troll hurt: it starts to regenerate (3 rounds on)
void troll_hurt(Fighter& f, const MonFx& m)
{
    if (hasx(f, m.regen_hit) && m.regen_wait && !hasx(f, m.regen) && !f.has(m.regen_wait))
        give_aff(f, m.regen_wait, 3, 0xFF, true);
}

Event* add_event(Attack& out, int who, Ev ev, int to = -1)
{
    if (out.n_ev >= 8) return nullptr;
    Event& e = out.ev[out.n_ev++];
    e = Event{};
    e.who = static_cast<uint8_t>(who);
    e.ev = ev;
    e.to = static_cast<int8_t>(to);
    e.hit = static_cast<int8_t>(out.n - 1);
    return &e;
}

// After a hit (monster_fx_facts 3): the attacker's on-hit specials - attack
// 1's (slot 0) or attack 2's (slot 1) - while the target is in the fight
void after_hit(Battle& b, int a, int c, int slot, int roll, int hits1, create::Dice& d, Attack& out)
{
    Fighter& at = b.f[a];
    Fighter& tg = b.f[c];
    const MonFx& m = b.fx->mon;
    // A touch's damage (the general damage routine: resistances, no save)
    auto touch = [&](int amount, int kind) {
        if (!tg.up()) return;
        Harm h;
        h.kind = kind;
        bool dn = false;
        const int done = harm(b, c, amount, h, d, &dn);
        g_kind = 0;
        Event* e = done > 0 ? add_event(out, c, Ev::Damage) : nullptr;
        if (e) {
            e->amount = static_cast<int16_t>(done);
            e->kind = static_cast<uint8_t>(kind);
        }
        if (dn) out.down = true;
    };
    // Poison: a save or poisoned and killed
    auto poison = [&](int bonus) {
        if (!tg.up() || saving_throw(tg, 0, bonus, d)) return;
        add_event(out, c, Ev::Poisoned);
        if (m.poisoned) give_aff(tg, m.poisoned, 0, 0xFF, false);
        tg.rec[kHp] = 0;
        tg.rec[kHealth] = party::Dead;
        fall(b, c);
        out.down = true;
    };
    // Held fast (engulfed, hugged): no more moving this turn
    auto hold = [&](uint8_t holding) {
        give_aff(tg, m.held_fast, 0, c, false);
        tg.moves = 0;
        give_aff(at, holding, 0, c, true);
    };
    if (slot == 0) {
        if (hasx(at, m.fire_touch)) touch(d.roll(10, 2), 9);
        if (hasx(at, m.acid_bite)) touch(d.roll(4, 1), 0x10);
        if (hasx(at, m.engulf) && hits1 == 2 && tg.up() && m.held_fast && m.suffocate && m.engulfing &&
            !tg.has(m.held_fast) && !tg.has(m.suffocate)) {
            add_event(out, a, Ev::Engulfs, c);
            hold(m.engulfing);
            give_aff(tg, m.suffocate, 0, d.roll(4, 2), false);
        }
        if (hasx(at, m.hug) && roll >= 18 && tg.up() && m.held_fast && m.hugging && !at.has(m.hugging)) {
            add_event(out, a, Ev::Hugs, c);
            hold(m.hugging);
        }
        if (hasx(at, m.draco_touch) && m.paralyzed && tg.up() && !saving_throw(tg, 0, 0, d)) {
            give_aff(tg, m.paralyzed, 0, 0xFF, false);
            add_event(out, c, Ev::ParalyzedLow);
        }
        if (hasx(at, m.chill)) touch(d.roll(8, 2), 0x0A);
        // Dispel Evil's blow: the evil dispelled (the caster's Dispel Evil goes with it)
        const SpellFx& sx = b.fx->sp;
        if (hasx(at, sx.evil_bane) && tg.up()) {
            if ((tg.rec[0x14B] & 1) && !saving_throw(tg, 4, 0, d)) {
                add_event(out, c, Ev::Dispelled);
                tg.rec[kHp] = 0;
                tg.rec[kHealth] = party::Gone;
                fall(b, c);
                out.down = true;
                const uint8_t both[2] = {sx.evil_ward, sx.evil_bane};
                for (uint8_t t : both) {
                    const int k = t ? find_aff(at, t) : -1;
                    if (k >= 0) drop_aff(at, k);
                }
            } else {
                add_event(out, c, Ev::ResistsDispel);
            }
        }
    } else {
        if (hasx(at, m.poison)) poison(0);
        if (hasx(at, m.poison_m2)) poison(-2);
        if (hasx(at, m.kreen_bite) && m.paralyzed && tg.up() && !saving_throw(tg, 0, 0, d)) {
            give_aff(tg, m.paralyzed, d.roll(8, 2), 12, false);
            add_event(out, c, Ev::Paralyzed);
        }
        if (hasx(at, m.fire_touch)) touch(d.roll(10, 2), 9);
    }
}

} // namespace

Attack attack(Battle& b, int a, int c, const items::Names* names, create::Dice& d, bool from_behind)
{
    Attack out;
    Fighter& at = b.f[a];
    Fighter& tg = b.f[c];
    set_actor(b, a);
    g_dice = &d;
    g_kind = 0;
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
    // Coughing (a stinking cloud): its rear AC 2 worse (AC 10 at most worse;
    // worse than 10 becomes 10 - the original's), from any side
    const bool coughing = b.fx && hasx(tg, b.fx->cough);
    if (coughing) ac = tg.rec[kAcBehind] > 0x34 ? tg.rec[kAcBehind] - 2 : 0x32;
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
            if (k < 0 || !prayer_reaches(b, i, a)) continue;
            side += (b.f[i].aff[k][3] >> 4) == at.team() ? 1 : -1;
            break;
        }
        const bool sees = hasx(at, fx.mon.detect);
        if (fx.bestow && at.has(fx.bestow)) side -= 4;
        if (fx.blinded && at.has(fx.blinded)) side -= 4;
        if (fx.animals_blind && at.rec[0x11A] == 19 && tg.has(fx.animals_blind) && !sees) side -= 4;
        if (hasx(tg, fx.invisible) && !sees) side -= 4;              // an invisible target
        if (hasx(tg, fx.sp.evil_ward) && (at.rec[0x14B] & 1)) side -= 7;   // Dispel Evil against the evil
        // The races: dwarves +1 against orcs, gnomes +1 against their foes;
        // dwarves and gnomes -4 to be hit by giants and trolls of size 2,
        // gnomes by kobold-kind (type 1) of size 2
        if (hasx(at, fx.race.dwarf_orc) && (tg.rec[0x14B] & 4)) ++side;
        if (hasx(at, fx.race.gnome_foe) && (tg.rec[0x14B] & 2)) ++side;
        if ((at.rec[0xDE] & 0x7F) == 2) {
            if (hasx(tg, fx.race.giants) && (at.rec[0x11A] == 2 || at.rec[0x11A] == 10)) side -= 4;
            if (hasx(tg, fx.race.gnome_extra) && at.rec[0x11A] == 1) side -= 4;
        }
        if (fx.blinded && tg.has(fx.blinded) && !coughing) ac -= 4;     // a blind target: easier (coughing: lost)
        if (fx.sp.shield && tg.has(fx.sp.shield) && ac < 0x39) ac = 0x39;   // Shield: AC 3 at worst
        // Faerie Fire: the stored AC + 2 (to AC 0 at most) - the original's
        // own rule, which makes the target harder to hit
        if (fx.faerie && tg.has(fx.faerie)) ac = ac < 58 ? ac + 2 : 60;
        // (protection from evil / good: the original's -2 to hit comes before
        // the roll and is lost; only the saves count)
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
    const uint8_t* item = blow_item(b, at, names);
    const int plus = item ? static_cast<int8_t>(item[0x32]) : 0;
    const bool armed = names && weapon(at, *names) >= 0;
    int hits1 = 0;                              // attack 1's hits (engulfing takes both)
    for (int slot = 1; slot >= 0; --slot) {
        while (at.attacks[slot] > 0 && tg.up() && out.n < 8) {
            --at.attacks[slot];
            const int roll = d.roll(20, 1);
            bool hit = roll == 20 || (roll != 1 && roll + static_cast<int8_t>(at.rec[kHit]) + side >= ac);
            // Blinking, once it has acted this round: not there to be hit
            if (b.fx && b.fx->blink && tg.has(b.fx->blink) && tg.delay == 0) hit = false;
            Hit h;
            h.hit = hit;
            if (!hit) {
                out.hits[out.n++] = h;
                continue;
            }
            const int n = slot ? at.rec[kDice + 1] : dice1;
            const int s = slot ? at.rec[kSides + 1] : sides1;
            const int bo = slot ? static_cast<int8_t>(at.rec[kBonus + 1]) : bonus1;
            int dmg = (n && s ? d.roll(s, n) : 0) + bo;
            if (dmg < 0) dmg = 0;
            dmg *= times;
            // A ranger against giants: their ranger level more
            if (b.fx && hasx(at, b.fx->race.ranger_giant) && (tg.rec[0x14B] & 8)) dmg += at.rec[0x10D];
            bool bane = false;
            if (b.fx) {
                const MonFx& m = b.fx->mon;
                // Ray of Enfeeblement on the attacker: a quarter off (coab's
                // facts: three_quarters_damage, the attacker's checks first)
                if (b.fx->sp.enfeeble && at.has(b.fx->sp.enfeeble)) dmg -= dmg / 4;
                // The salamander's heat
                if (hasx(at, m.heat) && !hasx(tg, m.heat_proof[0]) && !hasx(tg, m.heat_proof[1]) &&
                    !hasx(tg, m.heat_proof[2]))
                    dmg += d.roll(6, 1);
                // The target's defences against a blow: missiles dodged
                const bool far = !adjacent(b, a, c);
                bool dodged = false;
                if (armed && far && hasx(tg, m.missile_proof) && item && plus == 0) dodged = true;
                if (!dodged && armed && far && hasx(tg, m.missile_dodge) && d.roll(100, 1) <= 60) dodged = true;
                if (dodged) {
                    out.avoided[out.n] = true;
                    h.hit = false;
                    out.hits[out.n++] = h;
                    continue;
                }
                troll_hurt(tg, m);
                if (hasx(tg, m.need_magic) && (!item || plus == 0) && (at.rec[0x74] > 0 || at.rec[kHd] < 4)) dmg = 0;
                if (hasx(tg, m.stop_normal)) {
                    if (!item || plus <= 0) dmg = 0;
                    else if (plus <= 2) dmg /= 2;
                }
                if (hasx(tg, m.half)) dmg /= 2;
                if (hasx(tg, m.pierce_one) && item && names && names->type(item[0x2E]).blow == 1) dmg = 1;
                // A blessed quarrel slays it outright
                if (hasx(tg, m.blessed_bane) && item && item[0x2E] == b.quarrel && m.blessed_word &&
                    item[0x31] == m.blessed_word)
                    bane = true;
            }
            h.damage = dmg;
            out.any = true;
            if (slot == 0) ++hits1;
            if (bane) {
                tg.rec[kHp] = 0;
                tg.rec[kHealth] = party::Gone;
                fall(b, c);
                out.down = true;
            } else if (damage(b, c, dmg)) {
                out.down = true;
            }
            out.hits[out.n++] = h;
            // A fire shield: one hitting it from next to it takes twice the blow (magic)
            if (b.fx && dmg > 0 && hasx(tg, b.fx->sp.zap) && at.up() && adjacent(b, a, c)) {
                add_event(out, a, Ev::Zapped);
                Harm zh;
                zh.kind = 8;
                bool dn = false;
                const int done = harm(b, a, (2 * dmg) & 0xFF, zh, d, &dn);
                g_kind = 0;
                Event* e = done > 0 ? add_event(out, a, Ev::Damage) : nullptr;
                if (e) {
                    e->amount = static_cast<int16_t>(done);
                    e->kind = 8;
                }
                if (dn) add_event(out, a, Ev::Down);
                if (!at.up()) break;
            }
            if (b.fx && tg.up()) after_hit(b, a, c, slot, roll, hits1, d, out);
        }
    }
    return out;
}

bool resists(Battle& b, int c, int effect, const Harm& h, create::Dice& d)
{
    if (!b.fx || c < 0 || c >= b.n) return false;
    const Fighter& f = b.f[c];
    const Facts& fx = *b.fx;
    const MonFx& m = fx.mon;
    const bool magic = effect != 0 || (h.kind & 8);
    // Magic resistance by the caster's level (byte sums: a high level wraps
    // round to always resisted, as in the original)
    auto chance = [&](uint8_t type, int base) {
        if (!magic || !hasx(f, type)) return false;
        const int v = (base + (11 - h.level) * 5) & 0xFF;
        return d.roll(100, 1) <= v;
    };
    if (chance(m.mr50, 50) || chance(m.mr15, 15)) return true;
    const bool sleep_charm = effect > 0 && ((m.sleep && effect == m.sleep) || (fx.charm && effect == fx.charm));
    const bool paralysis = effect > 0 && m.paralyzed && effect == m.paralyzed;
    if (hasx(f, m.elf_sleep) && sleep_charm && d.roll(100, 1) <= 90) return true;
    if (hasx(f, m.charm_sleep) && sleep_charm) return true;
    if (hasx(f, m.no_paralyze) && paralysis) return true;
    if (hasx(f, m.no_cold) && (h.kind & 2)) return true;
    if (hasx(f, m.no_fire) && (h.kind & 1)) return true;
    if (hasx(f, m.mind) && (sleep_charm || paralysis || (effect > 0 && m.poisoned && effect == m.poisoned))) return true;
    if (hasx(f, fx.race.halfelf) && sleep_charm && d.roll(100, 1) <= 30) return true;      // half-elves: 30%
    if (hasx(f, m.globe) && h.spell > 0 && h.spell_level < 4) return true;
    if (hasx(f, m.no_magic) && magic) return true;
    return false;
}

int harm(Battle& b, int c, int amount, const Harm& h, create::Dice& d, bool* down, bool* resisted)
{
    if (down) *down = false;
    if (resisted) *resisted = false;
    if (c < 0 || c >= b.n) return 0;
    Fighter& f = b.f[c];
    g_b = &b;
    g_fx = b.fx;
    g_dice = &d;
    g_kind = h.kind;
    int dmg = amount;
    b.lost_image = -1;
    if (b.fx && f.up() && dmg > 0) {
        const MonFx& m = b.fx->mon;
        bool safe = false;
        // Mirror Image: a spell at picked creatures may take an image instead
        // (d(images + 1) over 1) - the effect's whole data byte counts down,
        // gone at 0 (the original's; weapons never meet the images)
        const int mi = b.fx->mirror && h.spell > 0 && h.single ? find_aff(f, b.fx->mirror) : -1;
        if (mi >= 0 && d.roll((f.aff[mi][3] >> 4) + 1, 1) > 1) {
            b.lost_image = c;
            if (--f.aff[mi][3] == 0) drop_aff(f, mi);
            return 0;
        }
        // Shield: Magic Missile stopped
        if (b.fx->sp.shield && h.spell == 0x0F && f.has(b.fx->sp.shield)) safe = true;
        // The target's resistances, in the original's order
        if (hasx(f, m.efreet) && (h.kind & 1)) {
            dmg -= h.dice;
            if (dmg < h.dice) dmg = h.dice;
        }
        if (hasx(f, m.resist_cold) && (h.kind & 2)) dmg /= 2;
        if (hasx(f, m.resist_fire) && (h.kind & 1)) dmg /= 2;
        auto chance = [&](uint8_t type, int base) {
            if (!(h.kind & 8) || !hasx(f, type)) return false;
            return d.roll(100, 1) <= ((base + (11 - h.level) * 5) & 0xFF);
        };
        if (chance(m.mr50, 50) || chance(m.mr15, 15)) safe = true;
        if (hasx(f, m.no_fire) && (h.kind & 1)) safe = true;
        troll_hurt(f, m);
        if (hasx(f, m.no_cold) && (h.kind & 2)) safe = true;
        if (!safe && hasx(f, m.fire_cold) && (h.kind & 3)) {
            const bool saved = saving_throw(f, 4, 0, d);
            if (saved && h.on_save != 0) safe = true;
            else dmg /= 2;
        }
        if (hasx(f, m.absorb_elec) && (h.kind & 4)) {
            safe = true;
            d.roll(8, 1);                       // (rolled, not used)
            f.rec[kHp] = static_cast<uint8_t>(f.rec[kHp] + 8);
        }
        if (hasx(f, m.no_magic) && (h.kind & 8)) safe = true;
        if (hasx(f, m.death_ward) && (h.kind & 0x40)) safe = true;
        if (hasx(f, m.no_elec) && (h.kind & 4)) safe = true;
        if (hasx(f, m.globe) && h.spell > 0 && h.spell_level < 4) safe = true;
        if (safe) {
            dmg = 0;
            if (resisted) *resisted = true;
        }
    }
    bool saved = false;
    if (dmg > 0 && h.save >= 0 && h.on_save && saving_throw(f, h.save, h.bonus, d)) {
        saved = true;
        if (h.on_save == 1) dmg = 0;
        else if (h.on_save == 2) dmg /= 2;
    }
    // Not saved: the fire shields double fire (hot) / cold (cold)
    if (!saved && dmg > 0 && b.fx) {
        if ((h.kind & 1) && hasx(f, b.fx->sp.hot)) dmg *= 2;
        else if ((h.kind & 2) && hasx(f, b.fx->sp.cold)) dmg *= 2;
    }
    if (dmg > 0) {
        const bool dn = damage(b, c, dmg);
        if (down) *down = dn;
    }
    g_kind = 0;                         // (the kind is this harm's only: later saves don't see it)
    return dmg;
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
    // The share: the whole party's but the animated dead (the original's
    // calc_battle_exp - those out of the fight still count, and get none)
    int members = 0;
    for (int i = 0; i < b.party_size; ++i)
        if (b.f[i].status() != party::Animated) ++members;
    if (!members) return 0;
    const int share = total / members;
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

namespace {

bool seen_enemy(const Battle& b, int i, int c)
{
    const int my = b.f[i].team() ? 1 : 0;
    return c >= 0 && c < b.n && b.f[c].up() && b.f[c].size && (b.f[c].team() ? 1 : 0) != my && !hidden(b, i, c);
}

// The computer's target: kept while it's an enemy in sight, else one in
// sight at random, else the nearest (walls or not); -1 none
int pick_target(Battle& b, const Tables& t, int i, create::Dice& d)
{
    Fighter& f = b.f[i];
    if (seen_enemy(b, i, f.target) && range(b, t, i, f.target, false, nullptr)) return f.target;
    f.target = -1;
    int cand[kMaxFighters], n = 0;
    for (int c = 0; c < b.n; ++c)
        if (seen_enemy(b, i, c) && range(b, t, i, c, false, nullptr)) cand[n++] = c;
    if (n) f.target = cand[d.roll(n, 1) - 1];
    if (f.target < 0) {
        int best = 9999;
        for (int c = 0; c < b.n; ++c) {
            int sq;
            if (!seen_enemy(b, i, c)) continue;
            range(b, t, i, c, true, &sq);
            if (sq < best) {
                best = sq;
                f.target = c;
            }
        }
    }
    return f.target;
}

} // namespace

Plan think(Battle& b, const Tables& t, int i, create::Dice& d)
{
    Plan p;
    Fighter& f = b.f[i];
    if (!f.up() || !f.size || f.delay <= 0) return p;
    const int my = f.team() ? 1 : 0;
    auto enemy = [&](int c) { return seen_enemy(b, i, c); };
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
    pick_target(b, t, i, d);
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
    // No enemy that can reach it: away; else its movement against the
    // fastest enemy in the fight (facts 4.6)
    int fastest = -1;
    bool reached = false;
    for (int c = 0; c < b.n; ++c) {
        const Fighter& e = b.f[c];
        if (!e.up() || !e.size || (e.team() ? 1 : 0) == my) continue;
        if (range(b, t, c, i, true, nullptr)) reached = true;
        if (e.rec[kMove] > fastest) fastest = e.rec[kMove];
    }
    const int mine = f.rec[kMove];
    const bool away = !reached || fastest < 0 || mine > fastest || (mine == fastest && d.roll(2, 1) == 1);
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

bool teleport(Battle& b, const Tables& t, int i, int x, int y)
{
    Fighter& f = b.f[i];
    if (!f.up() || !f.size || !fits(b, t, i, f.size, x, y)) return false;
    // Held fast: the one holding it lets go
    if (b.fx && hasx(f, b.fx->mon.held_fast))
        for (int c = 0; c < b.n; ++c) {
            Fighter& o = b.f[c];
            const uint8_t holds[2] = {b.fx->mon.engulfing, b.fx->mon.hugging};
            for (uint8_t h : holds) {
                const int k = h ? find_aff(o, h) : -1;
                if (k >= 0 && o.aff[k][3] == i) let_go(b, o, h);
            }
        }
    f.x = x;
    f.y = y;
    occupancy(b);
    return true;
}

bool saving_throw(const Fighter& f, int type, int bonus, create::Dice& d)
{
    if (type < 0 || type > 4) type = 4;
    // Immune to poison, paralysis and death magic: every such save made
    if (g_fx && type == 0 && hasx(f, g_fx->mon.mind)) return true;
    const int r = d.roll(20, 1);
    if (r == 1) return false;
    if (r == 20) return true;
    if (g_fx) {
        const Facts& fx = *g_fx;
        if (fx.bestow && f.has(fx.bestow)) bonus -= 4;
        if (fx.blinded && f.has(fx.blinded)) bonus -= 4;
        if (fx.sp.shield && f.has(fx.sp.shield)) ++bonus;
        // A prayer: +1 on its caster's side, -1 on the other
        for (int c = 0; fx.prayer && g_b && c < g_b->n; ++c) {
            const int k = find_aff(g_b->f[c], fx.prayer);
            int me = -1;
            for (int j = 0; k >= 0 && j < g_b->n && me < 0; ++j)
                if (&g_b->f[j] == &f) me = j;
            if (k < 0 || (me >= 0 && !prayer_reaches(*g_b, c, me))) continue;
            bonus += (g_b->f[c].aff[k][3] >> 4) == f.team() ? 1 : -1;
            break;
        }
        // Dwarves', gnomes', halflings' Constitution: against spells and wands
        if ((type == 4 || type == 2) && hasx(f, fx.race.con_save)) {
            const int con = f.rec[0x19];
            bonus += con >= 4 && con <= 6 ? 1 : con >= 7 && con <= 10 ? 2 : con >= 11 && con <= 13 ? 3 : con >= 14 && con <= 17 ? 4
                     : con >= 18 && con <= 20 ? 5 : 0;
        }
        if ((g_kind & 2) && hasx(f, fx.mon.resist_cold)) bonus += 3;
        if ((g_kind & 1) && hasx(f, fx.mon.resist_fire)) bonus += 3;
        if ((g_kind & 2) && hasx(f, fx.sp.hot)) bonus += 2;           // the fire shields
        if ((g_kind & 1) && hasx(f, fx.sp.cold)) bonus += 2;
        // Protection from evil / good: +2 when the one acting is evil / good
        if (g_b && g_actor >= 0 && g_actor < g_b->n) {
            const int al = g_b->f[g_actor].rec[0x11B];
            if ((al == 0 || al == 3 || al == 6) && hasx(f, fx.prot_good)) bonus += 2;
            if (al == 2 || al == 5 || al == 8) {
                bool warded = hasx(f, fx.prot_evil) || hasx(f, fx.mon.prot_evil_near);
                for (int c = 0; !warded && fx.mon.prot_evil_near && c < g_b->n; ++c) {
                    const Fighter& o = g_b->f[c];
                    if (&o == &f || !o.size || !f.size || !o.has(fx.mon.prot_evil_near)) continue;
                    const int ddx = o.x - f.x, ddy = o.y - f.y;
                    warded = ddx >= -1 && ddx <= 1 && ddy >= -1 && ddy <= 1;
                }
                if (warded) bonus += 2;
            }
        }
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
    // The levels that count (classes::skill_level): a human who changed
    // class adds the old class's levels once the new class has passed them
    // (coab's facts: spellMaxTargetCount uses the skill levels)
    int k = 0;
    while (k < 7 && r[0x109 + k] == 0) ++k;
    const bool past = r[0x74] == 7 && r[0x109 + k] > r[0xE6];
    auto skill = [&](int cls) { return r[0x109 + cls] + (past ? r[0x111 + cls] : 0); };
    switch (st.spell_class(s)) {
    case 0: return most(skill(0), skill(3) - 8);
    case 1: return most(skill(4) - 7, 0);
    case 2: return most(skill(5), skill(4) - 8);
    case 3: return 12;
    default: return 0;
    }
}

namespace {

int dispel_chance(int L, int lvl);

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
    b.n_bolt = 0;
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
        // Bless in a fight: not on those with an enemy next to them (coab's
        // facts: CastTeamSpell, spell 1 in combat)
        if (fs.does == SpellDoes::Ours && spell == 0x01) {
            bool near = false;
            for (int c = 0; c < b.n && !near; ++c)
                near = c != i && b.f[c].up() && b.f[c].size && (b.f[c].team() ? 1 : 0) != side && adjacent(b, i, c);
            if (near) continue;
        }
        who[m++] = i;
    }
    // A fireball outdoors: those within 2 squares of the square aimed at
    // (indoors the spell's area, 3; coab's facts: the fireball's routine
    // rebuilds its targets when the area isn't a dungeon)
    if ((spell == 0x2F || spell == 0x40) && fs.does == SpellDoes::Damage && !b.indoors && b.tables && b.aim_x >= 0)
        m = in_area(b, *b.tables, b.aim_x, b.aim_y, 2, who, kMaxFighters);
    if (m == 0) return 0;
    b.no_action = b.round + 15;
    set_actor(b, caster);
    g_dice = &d;
    g_kind = 0;
    // Casting makes them seen
    const int inv = b.fx && b.fx->invisible ? find_aff(me, b.fx->invisible) : -1;
    if (inv >= 0) drop_aff(me, inv);
    // Magic resistance and the damage routine: the spell's kind, the caster's level
    Harm hs;
    hs.kind = fs.kind;
    hs.level = pw;
    hs.spell = spell;
    hs.spell_level = e.level;
    {
        // At picked creatures (aim 1-5, 15), not an area (the original's flag)
        const int aim = e.aim & 0x0F;
        hs.single = (aim >= 1 && aim <= 5) || aim == 0x0F;
    }
    // A spell's effect on fighter k: magic resistance first ("is Unaffected")
    auto resisted = [&](int k, int effect) {
        if (!resists(b, who[k], effect ? effect : -1, hs, d)) return false;
        say(who[k], Did::Unaffected, 0);
        return true;
    };
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
            if (resisted(k, e.affect)) continue;
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
    case SpellDoes::Haste:
    case SpellDoes::Slow: {
        // The first `level` of the caster's side (Slow: of the other side);
        // one with the opposite effect is cured of it instead ("is Cured",
        // word2) - coab's facts: RemoveComplimentSpellFirst
        const bool slow = fs.does == SpellDoes::Slow;
        int left = pw;
        for (int k = 0; k < m && left > 0; ++k) {
            Fighter& f = b.f[who[k]];
            if (((f.team() ? 1 : 0) == my) == slow) continue;
            --left;
            const int sl = b.fx ? find_aff(f, slow ? b.fx->haste : b.fx->slow) : -1;
            if (sl >= 0) {
                drop_aff(f, sl);
                if (fs.word2) say(who[k], Did::Word2, 0);
                continue;
            }
            if (resisted(k, e.affect)) continue;
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
            if (resisted(k, b.fx ? b.fx->charm : 0)) continue;
            if (e.on_save != 0 && saving_throw(f, e.save, 0, d) && e.on_save == 1) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            if (!b.fx || !b.fx->charm) continue;
            const int caster_side = b.f[caster].team() ? 1 : 0;
            int own = f.team() ? 1 : 0;
            const int was = find_aff(f, b.fx->charm);
            if (was >= 0) own = (f.aff[was][3] & 0x40) >> 6;      // charmed again: still their own side underneath
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
            if (resisted(k, b.fx ? b.fx->fear : 0)) continue;
            if (saving_throw(f, e.save, 0, d)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            const int was = b.fx && b.fx->fear ? find_aff(f, b.fx->fear) : -1;
            const bool made_quick =
                (f.member >= 0 && f.rec[0xF7] < 0x80 && !f.quick) || (was >= 0 && (f.aff[was][3] & 1));
            if (b.fx && b.fx->fear) give_aff(f, b.fx->fear, minutes, made_quick ? 1 : 0, false);
            f.fleeing = true;
            if (made_quick) f.quick = true;
            f.target = -1;
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Bolt:
        if (b.tables && b.aim_x >= 0 && fs.reach) {
            // The original's bolt (curse_finish_facts.md 2): one roll, for the
            // one on the square aimed at and all on the path (an item's fixed
            // stroke: the path takes its plus alone); each saves for half
            const int count = fs.per == 3 ? pw : fs.n;
            const int roll = ((count && fs.sides ? d.roll(fs.sides, count) : 0) + fs.plus) & 0xFF;
            const int on_path = fs.per == 0 ? fs.plus : roll;
            Harm h = hs;
            h.dice = count;
            h.save = e.on_save ? e.save : -1;
            h.on_save = e.on_save;
            auto hit = [&](int c, int amount) {
                if (c < 0 || c >= b.n || !b.f[c].up()) return;
                bool dn = false;
                const int done = harm(b, c, amount, h, d, &dn);
                say(c, done > 0 ? Did::Damage : Did::Unaffected, done);
                if (dn) say(c, Did::Down, 0);
            };
            const int on = on_field(b.aim_x, b.aim_y) ? b.who[b.aim_y][b.aim_x] : 0;
            if (on) hit(on - 1, roll);
            int hits[32];
            const int nh = bolt_path(b, *b.tables, caster, b.aim_x, b.aim_y, fs.reach, fs.per != 0, hits, 32, b.bolt,
                                     &b.n_bolt, 16);
            for (int k = 0; k < nh; ++k) hit(hits[k], on_path);
            break;
        }
        [[fallthrough]];
    case SpellDoes::Cone:
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
            // The damage routine: its resistances, then the save
            Harm h = hs;
            h.dice = count;
            h.save = e.on_save ? e.save : -1;
            h.on_save = e.on_save;
            bool dn = false;
            const int done = harm(b, who[k], dmg, h, d, &dn);
            if (b.lost_image == who[k]) {
                say(who[k], Did::LostImage, 0);
                continue;
            }
            if (done <= 0) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            say(who[k], Did::Damage, done);
            if (dn) say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Slay:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            if (resisted(k, 0)) continue;
            if (!saving_throw(f, e.save, 0, d)) {
                say(who[k], Did::Word, 0);
                damage(b, who[k], f.hp() + 10);
                f.rec[kHealth] = party::Dead;
                say(who[k], Did::Fallen, 0);
                continue;
            }
            Harm h = hs;
            h.dice = fs.n;
            bool dn = false;
            const int done = harm(b, who[k], d.roll(fs.sides, fs.n) + fs.plus, h, d, &dn);
            if (done <= 0) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            say(who[k], Did::Damage, done);
            if (dn) say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Kill:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || (b.fx && resisted(k, b.fx->mon.poisoned)) || saving_throw(f, 0, 0, d)) continue;
            say(who[k], Did::Word, 0);
            // poisoned (data 0xFF), then killed: Slow / Neutralize Poison can
            // bring them back (listing ovr013:1407, the poison attack)
            if (b.fx && b.fx->mon.poisoned) give_aff(f, b.fx->mon.poisoned, 0, 0xFF, false);
            damage(b, who[k], f.hp() + 10);
            f.rec[kHealth] = party::Dead;
            say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Fumble:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || !b.fx || resisted(k, b.fx->fumbling)) continue;
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
            if (!f.up() || resisted(k, b.fx ? b.fx->feeble : 0)) continue;
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
            if (!f.up() || !b.fx || resisted(k, b.fx->entangle)) continue;
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
            if (resisted(k, e.affect)) continue;
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
            if (resisted(k, b.fx->sticks)) continue;
            // The handler at once (as at its turns): more snakes than its
            // attacks left this round - they go down by those, else gone;
            // either way its actions this round are gone
            const int att = f.attacks[0] + f.attacks[1];
            say(who[k], Did::Word, 0);
            if (pw > att) give_aff(f, b.fx->sticks, minutes, pw - att, false);
            f.moves = f.attacks[0] = f.attacks[1] = 0;
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
    case SpellDoes::GiantStrength: {
        // Strength 21 when more than their own ("is stronger"); the effect
        // either way (then their own Strength: the original leaves it unset)
        const int own = me.rec[0x10], own00 = me.rec[0x1D];
        const bool more = 21 > own;
        const int data = more ? 121 : own == 18 ? own00 + 1 : own + 100;
        if (b.fx && b.fx->sp.giant) give_aff(me, b.fx->sp.giant, minutes, data, true);
        if (more) say(caster, Did::Word, 0);
        break;
    }
    case SpellDoes::Defoliate:
        // Plants (monster type 18) take it all, not saving; the rest as
        // when saved (the table's: nothing / half / all) - nothing said for none
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            int dmg = d.roll(fs.sides, fs.n) + fs.plus;
            if (f.rec[0x11A] != 18) {
                if (e.on_save == 1) dmg = 0;
                else if (e.on_save == 2) dmg /= 2;
            }
            if (dmg <= 0) continue;
            Harm h = hs;
            h.dice = fs.n;
            h.save = -1;
            bool dn = false;
            const int done = harm(b, who[k], dmg, h, d, &dn);
            if (done <= 0) continue;
            say(who[k], Did::Damage, done);
            if (dn) say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Enlarge: {
        // Strength by the caster's level (18 ... 22), only when more than theirs
        const int data = spells::enlarge_data(pw);
        int str = 18, str00 = 0;
        if (data > 101) str = data - 100;
        else str00 = data - 1;
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            const int now = f.rec[0x11], now00 = f.rec[0x1C];
            if (!data || !(str > now || (str == 18 && now == 18 && str00 > now00))) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            if (b.fx && b.fx->sp.enlarge) give_aff(f, b.fx->sp.enlarge, minutes, data, false);
            say(who[k], Did::Word, 0);
        }
        break;
    }
    case SpellDoes::Reduce:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            const int at = b.fx && b.fx->sp.enlarge ? find_aff(f, b.fx->sp.enlarge) : -1;
            if (at < 0 || saving_throw(f, 4, 0, d)) continue;       // (nothing said)
            drop_aff(f, at);
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::RemoveCurse:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            const int at = b.fx && b.fx->bestow ? find_aff(f, b.fx->bestow) : -1;
            if (at >= 0) {
                drop_aff(f, at);
                say(who[k], Did::Word2, 0);         // "is Cured"
                say(who[k], Did::Word, 0);          // "is un-cursed"
                continue;
            }
            // Else the first cursed item comes off (still cursed); a monster's are its group's: not
            for (int j = 0; f.member >= 0 && f.items && j < f.n_items; ++j)
                if (f.items[j][0x36]) {
                    if (f.items[j][0x34] && f.items[j][0x3E] == 0x80 && f.items[j][0x3D]) {
                        const int fx = find_aff(f, f.items[j][0x3D]);     // its effect comes off too
                        if (fx >= 0) drop_aff(f, fx);
                    }
                    f.items[j][0x34] = 0;
                    say(who[k], Did::Word3, 0);
                    break;
                }
        }
        break;
    case SpellDoes::Confuse: {
        // The first 2d8 of those in the area
        const int most = d.roll(8, 2);
        for (int k = 0; k < m && k < most; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || !b.fx || !b.fx->sp.confuse) continue;
            if (resisted(k, b.fx->sp.confuse)) continue;
            if (saving_throw(f, e.save, 0, d)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            give_aff(f, b.fx->sp.confuse, minutes, 0, false);
            say(who[k], Did::Word, 0);
        }
        break;
    }
    case SpellDoes::DispelEvil:
        if (!b.fx) break;
        if (b.fx->sp.evil_ward) give_aff(me, b.fx->sp.evil_ward, pw, pw, false);
        if (b.fx->sp.evil_bane) give_aff(me, b.fx->sp.evil_bane, pw, pw, false);
        say(caster, Did::Word, 0);
        break;
    case SpellDoes::FireShield: {
        if (!b.fx) break;
        const int flame = b.flame ? b.flame : d.roll(10, 1) > 5 ? 1 : 2;
        b.flame = 0;
        const uint8_t shield = flame == 1 ? b.fx->sp.hot : b.fx->sp.cold;
        if (shield) give_aff(me, shield, minutes, 0, false);
        if (b.fx->sp.zap) give_aff(me, b.fx->sp.zap, minutes, 0, false);
        if (flame == 1) say(caster, Did::Word, 0);   // (the cold one: nothing said)
        break;
    }
    case SpellDoes::Teleport: break;                // (the caller moves the caster: teleport())
    case SpellDoes::Dispel: {
        // Every effect of the first one (but those with data 0xFF) a chance,
        // as many passes as there are in the area (the original's); then
        // the clouds round the square aimed at
        Fighter& f = b.f[who[0]];
        for (int pass = 0; pass < m; ++pass) {
            bool any = false;
            for (int k = 0; f.n_aff && k < *f.n_aff;) {
                const int data = f.aff[k][3];
                if (data == 0xFF || d.roll(100, 1) > dispel_chance(pw, data & 0x0F)) {
                    ++k;
                    continue;
                }
                end_effect(b, f, k);
                any = true;
            }
            if (any) say(who[0], Did::Word, 0);
        }
        if (b.aim_x >= 0) dispel_clouds(b, b.aim_x, b.aim_y, pw, d);
        break;
    }
    case SpellDoes::SlowPoison:
        // The poisoned (dead by it) get back up with 1 hit point while it lasts
        for (int k = 0; k < m && b.fx; ++k) {
            Fighter& f = b.f[who[k]];
            if (f.status() == party::Animated || !hasx(f, b.fx->mon.poisoned)) continue;
            if (f.hp() == 0) f.rec[kHp] = 1;
            if (b.fx->sp.slow_poison) give_aff(f, b.fx->sp.slow_poison, minutes, 0xFF, true);
            say(who[k], Did::Word, 0);
            if (!f.up()) {
                if (stand_up(b, who[k], false)) say(who[k], Did::Risen, 0);
                else if (b.fx->mon.troll_up) give_aff(f, b.fx->mon.troll_up, 1, 0xFF, true);    // (a round later)
            }
            if (b.fx->sp.poison_damage) give_aff(f, b.fx->sp.poison_damage, 10, 0xFF, true);
        }
        break;
    case SpellDoes::Hammer:
        // (the hammer itself: the caller, rules::keep_hammer)
        if (b.fx && b.fx->sp.hammer) give_aff(me, b.fx->sp.hammer, pw, pw, true);
        break;
    case SpellDoes::Animate: {
        // Up to the caster's level: the dead (characters, not monsters) back
        // up on the caster's side, run by the computer, until the fight ends
        int left = pw;
        for (int c = 0; c < b.n && left > 0 && b.fx; ++c) {
            Fighter& f = b.f[c];
            if (f.status() != party::Dead || f.rec[0x11A] != 0 || f.gone) continue;
            const int old_team = f.team() ? 1 : 0;
            if (!stand_up(b, c, true)) continue;
            --left;
            f.rec[kTeam] = me.rec[kTeam];
            f.quick = true;
            f.target = -1;
            f.rec[0xE9] = 2;                                // undead
            f.rec[kMove] = 6;
            f.rec[0xDD] = 0;                                // attack level 0
            memset(f.rec + 0x1E, 0, 84);                    // no spells
            f.was_control = f.rec[kControl];
            f.rec[kControl] = f.rec[kControl] > 0x7F ? 0xB2 : 0xB3;
            f.rec[0x11A] = 4;                               // animated dead
            f.rec[kHealth] = party::Animated;
            if (b.fx->sp.animated) give_aff(f, b.fx->sp.animated, 0, old_team << 4 | (pw & 0x0F), true);
            say(c, Did::Risen, 0);
            say(c, Did::Word, 0);
        }
        break;
    }
    case SpellDoes::Restore:
        // (the level itself: the caller, create::restore)
        for (int k = 0; k < m; ++k)
            if (b.f[who[k]].rec[0xE7] > 0) say(who[k], Did::Word, 0);
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
            if (resisted(k, e.affect)) continue;
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

namespace {

// The original's path stepper: half squares (straight 2, slanted 3), the
// count kept in a byte
struct Stepper {
    int x, y, tx, ty, dx, dy, sx, sy, err = 0, steps = 0;
    Stepper(int x0, int y0, int x1, int y1)
        : x(x0), y(y0), tx(x1), ty(y1), dx(x1 > x0 ? x1 - x0 : x0 - x1), dy(y1 > y0 ? y1 - y0 : y0 - y1),
          sx((x1 > x0) - (x1 < x0)), sy((y1 > y0) - (y1 < y0))
    {
    }
    bool step()
    {
        if (dx >= dy) {
            if (x == tx) return false;
            x += sx;
            err += 2 * dy;
            steps += 2;
            if (err >= dx) {
                y += sy;
                err -= 2 * dx;
                steps += 1;
            }
        } else {
            if (y == ty) return false;
            y += sy;
            err += 2 * dx;
            steps += 2;
            if (err >= dy) {
                x += sx;
                err -= 2 * dy;
                steps += 1;
            }
        }
        steps &= 0xFF;
        return true;
    }
};

} // namespace

int bolt_path(const Battle& b, const Tables& t, int caster, int tx, int ty, int length, bool near_rule, int* hits,
              int hit_cap, BoltSeg* segs, int* n_segs, int seg_cap)
{
    int nh = 0;
    *n_segs = 0;
    const int cx = b.f[caster].x, cy = b.f[caster].y;
    if (cx == tx && cy == ty) return 0;
    // The square's move cost (-1: off the field) and who's on it (0 none, fighter + 1)
    auto cost = [&](int x, int y) { return on_field(x, y) && b.ground[y][x] ? tile(b, t, x, y)[0] : -1; };
    auto who = [&](int x, int y) { return on_field(x, y) ? static_cast<int>(b.who[y][x]) : 0; };
    int Tx = tx, Ty = ty, sign = 1, prev = 0;
    int last = who(tx, ty);
    bool wall = false, near = near_rule;
    int budget = (length * 2) & 0xFF;
    while (budget > 0) {
        Stepper p(Tx, Ty, Tx + (Tx - cx) * sign * budget, Ty + (Ty - cy) * sign * budget);
        int px = Tx, py = Ty;                       // where the segment's picture starts
        while (true) {
            if (p.x != p.tx || p.y != p.ty) {
                while (true) {
                    const bool moved = p.step();
                    const int g = cost(p.x, p.y), c = who(p.x, p.y);
                    if (g == 1) wall = false;
                    if (!(moved && (c == 0 || c == last) && g >= 0 && g <= 1 && p.steps < budget)) break;
                }
            }
            const int g = cost(p.x, p.y), c = who(p.x, p.y);
            if (g < 0) budget = 0;
            if (*n_segs < seg_cap) {
                BoltSeg& s = segs[(*n_segs)++];
                s.x0 = static_cast<uint8_t>(px);
                s.y0 = static_cast<uint8_t>(py);
                s.x1 = static_cast<uint8_t>(p.x < 0 ? 0 : p.x >= kW ? kW - 1 : p.x);
                s.y1 = static_cast<uint8_t>(p.y < 0 ? 0 : p.y >= kH ? kH - 1 : p.y);
            }
            px = p.x;
            py = p.y;
            wall = g == 0xFF && b.indoors && !wall;
            if (c > 0 && c != last && nh < hit_cap) hits[nh++] = c - 1;
            last = c;
            if (wall) {
                // A bounce: back toward the caster from here
                Tx = p.x;
                Ty = p.y;
                Stepper q(Tx, Ty, cx, cy);
                while (q.step()) {
                }
                if (near && q.steps <= 8) p.steps = (p.steps + 8) & 0xFF;
                sign = -sign;
                near = false;
                last = 0;
            }
            const int used = (p.steps - prev) & 0xFF;
            budget = used < budget ? budget - used : 0;
            prev = p.steps;
            if (wall || budget == 0) break;
        }
    }
    return nh;
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
        if (fs.does == SpellDoes::FireShield && b.fx && hasx(f, b.fx->sp.zap)) return false;     // shielded already
        if (fs.does == SpellDoes::Teleport) return false;
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
        if (!o.up() || !o.size || (o.team() ? 1 : 0) == my || hidden(b, i, c)) continue;
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
    } else if (aim == 5) {
        // One by one at random (repeats lower the budget) until 2+ pass it
        const bool by_size = fs.does == SpellDoes::Faerie;
        int budget = by_size ? pw : d.roll(4, 2);
        targets[(*n_targets)++] = tg;
        for (int tries = 0; tries < 64 && budget > 0 && *n_targets < kMaxFighters &&
                            !picks_done(b, targets, *n_targets, budget, by_size);
             ++tries) {
            const int c = cand[d.roll(nc, 1) - 1];
            bool again = false;
            for (int k = 0; k < *n_targets; ++k) again = again || targets[k] == c;
            if (again) --budget;
            else targets[(*n_targets)++] = c;
        }
    } else {
        const int want = aim >= 1 && aim <= 4 ? (e.aim & 3) + 1 : 1;
        targets[(*n_targets)++] = tg;
        for (int k = 0; k < nc && *n_targets < want; ++k)
            if (cand[k] != tg) targets[(*n_targets)++] = cand[k];
    }
    return *n_targets > 0;
}

int pick_cost(const Fighter& f, bool by_size)
{
    if (by_size) {
        switch (f.rec[kSize]) {
        case 1: return 1;
        case 2: case 3: return 2;
        case 4: return 4;
        default: return 0;
        }
    }
    switch (f.rec[kHd]) {
    case 0: case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    default: return 8;
    }
}

bool picks_done(const Battle& b, const int* picked, int n, int budget, bool by_size)
{
    int total = 0;
    for (int k = 0; k < n; ++k) total += pick_cost(b.f[picked[k]], by_size);
    return n >= 2 && total > budget;
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
    cl.level = cl.rounds;                   // (the caster's level is its rounds)
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
    // poisoned (data 0xFF) and dead: Slow / Neutralize Poison can bring them
    // back (listing ovr024 in_poison_cloud: effect 0x37 in every branch)
    if (b.fx && b.fx->mon.poisoned) give_aff(f, b.fx->mon.poisoned, 0, 0xFF, false);
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
                const int st = o.status();
                const bool body = st == party::Unconscious || st == party::Dying || st == party::Dead ||
                                  st == party::Stoned;     // (not one who fled)
                if (o.member >= 0 && !o.size && body && o.x == xx && o.y == yy) {
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

namespace {

// Dispel Magic's chance against an effect (or cloud) of level `lvl` by a
// dispeller of level L (spell_facts.md 0x29)
int dispel_chance(int L, int lvl)
{
    if (L > lvl) return 50 + 5 * (L - lvl);
    if (L < lvl) return 50 - 2 * (lvl - L);
    return 50;
}

} // namespace

int dispel_clouds(Battle& b, int x, int y, int level, create::Dice& d)
{
    int ended = 0;
    for (int c = 0; c < b.n_clouds; ++c) {
        Battle::Cloud& cl = b.clouds[c];
        if (cl.resisted || cl.rounds <= 1) continue;
        bool near = false;
        for (int k = 0; k < cloud_squares(cl.poison) && !near; ++k) {
            if (!(cl.present >> k & 1)) continue;
            const int ddx = cl.x + sq_dx(cl.poison, k) - x, ddy = cl.y + sq_dy(cl.poison, k) - y;
            near = ddx >= -1 && ddx <= 1 && ddy >= -1 && ddy <= 1;
        }
        if (!near) continue;
        if (d.roll(100, 1) <= dispel_chance(level, cl.level)) {
            cl.rounds = 1;                  // it clears as the round ends
            ++ended;
        } else {
            cl.resisted = true;
        }
    }
    return ended;
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

// ---- The computer's special actions (monster_fx_facts.md 4)
void release_holds(Battle& b, int i)
{
    if (!b.fx || i < 0 || i >= b.n) return;
    Fighter& f = b.f[i];
    const MonFx& m = b.fx->mon;
    let_go(b, f, m.engulfing);
    let_go(b, f, m.hugging);
    const uint8_t mine[2] = {m.suffocate, m.held_fast};
    for (uint8_t t : mine) {
        const int k = t ? find_aff(f, t) : -1;
        if (k >= 0) drop_aff(f, k);
    }
}

namespace {

// The spell rows' targeting, the computer choosing: an enemy it sees
// within `reach` squares (not one held, when skip_held), at random
int special_target(Battle& b, const Tables& t, int i, int reach, bool skip_held, create::Dice& d)
{
    int cand[kMaxFighters], n = 0;
    for (int c = 0; c < b.n; ++c) {
        int sq;
        if (!seen_enemy(b, i, c) || (skip_held && helpless(b, b.f[c]))) continue;
        if (!range(b, t, i, c, false, &sq) || sq > reach) continue;
        cand[n++] = c;
    }
    return n ? cand[d.roll(n, 1) - 1] : -1;
}

// The nearest enemy it sees (the beholder's)
int nearest_enemy(const Battle& b, const Tables& t, int i)
{
    int best = 9999, who = -1;
    for (int c = 0; c < b.n; ++c) {
        int sq;
        if (!seen_enemy(b, i, c) || !range(b, t, i, c, false, &sq)) continue;
        if (sq < best) {
            best = sq;
            who = c;
        }
    }
    return who;
}

int squares_to(const Battle& b, const Tables& t, int i, int c)
{
    int sq = 9999;
    range(b, t, i, c, true, &sq);
    return sq;
}

// A readied item with that name word (the medusa's mirror)
bool readied_word(const Fighter& f, uint8_t word)
{
    for (int k = 0; f.items && k < f.n_items; ++k)
        if (f.items[k][0x34] && (f.items[k][0x2F] == word || f.items[k][0x30] == word || f.items[k][0x31] == word))
            return true;
    return false;
}

} // namespace

int special(Battle& b, const Tables& t, int i, create::Dice& d, Event* out, int cap, bool* ends_turn)
{
    *ends_turn = false;
    if (!b.fx || i < 0 || i >= b.n) return 0;
    Fighter& f = b.f[i];
    if (!f.up() || !f.size) return 0;
    const MonFx& m = b.fx->mon;
    set_actor(b, i);
    g_dice = &d;
    g_kind = 0;
    int n = 0;
    auto ev = [&](int who, Ev e, int to = -1) -> Event* {
        if (n >= cap) return nullptr;
        Event& x = out[n++];
        x = Event{};
        x.who = static_cast<uint8_t>(who);
        x.ev = e;
        x.to = static_cast<int8_t>(to);
        return &x;
    };
    // A picture flying from `from` to the square (COMSPR pic, frames, ms a step)
    auto fly = [&](int from, int x, int y, int pic, int frames, int delay) {
        Event* e = ev(from, Ev::Fly);
        if (!e) return;
        e->x = static_cast<uint8_t>(x);
        e->y = static_cast<uint8_t>(y);
        e->pic = static_cast<uint8_t>(pic);
        e->frames = static_cast<uint8_t>(frames);
        e->delay = static_cast<uint8_t>(delay);
    };
    auto hurt = [&](int c, int amount, const Harm& h) {
        if (!b.f[c].up()) return;
        bool dn = false;
        const int done = harm(b, c, amount, h, d, &dn);
        Event* e = ev(c, done > 0 ? Ev::Damage : Ev::Unaffected);
        if (e) {
            e->amount = static_cast<int16_t>(done);
            e->kind = static_cast<uint8_t>(h.kind);
        }
        if (dn) ev(c, Ev::Down);
        g_kind = 0;
    };
    // Out of the fight (stoned, disintegrated, killed)
    auto remove = [&](int c, int status, Ev said) {
        Fighter& v = b.f[c];
        v.rec[kHp] = 0;
        v.rec[kHealth] = static_cast<uint8_t>(status);
        fall(b, c);
        ev(c, said);
    };
    // A breath's count: 3 a fight (set on every turn of the first round)
    auto breaths = [&](uint8_t type) -> uint8_t* {
        const int k = find_aff(f, type);
        if (k < 0) return nullptr;
        if (b.round == 0) f.aff[k][3] = 3;
        return &f.aff[k][3];
    };
    Harm breath;
    breath.save = 3;
    breath.on_save = 2;

    // The stone gaze (the hooded medusa): a save against petrification or
    // stoned; a readied mirror sends it back at the medusa
    if (hasx(f, m.stone_gaze)) {
        int tg = special_target(b, t, i, 12, false, d);
        if (tg >= 0) {
            ev(i, Ev::Gazes);
            fly(i, b.f[tg].x, b.f[tg].y, 5, 4, 45);
            if (hasx(f, m.mirror_gaze) && m.mirror_word && readied_word(b.f[tg], m.mirror_word)) {
                ev(tg, Ev::Reflects);
                fly(tg, f.x, f.y, 5, 4, 45);
                tg = i;
            }
            if (!saving_throw(b.f[tg], 1, 0, d)) remove(tg, party::Stoned, Ev::GazeStoned);
            if (!f.up()) return n;
        }
    }
    // The ankheg's acid: once a fight (its acid bite goes with it); the turn is over
    if (hasx(f, m.ankheg_spit)) {
        const int tg = pick_target(b, t, i, d);
        const int roll = d.roll(100, 1);
        if (roll <= 25 && tg >= 0 && squares_to(b, t, i, tg) < 4) {
            *ends_turn = true;
            ev(i, Ev::SpitsAcid);
            fly(i, b.f[tg].x, b.f[tg].y, 10, 1, 30);
            hurt(tg, d.roll(4, 8), breath);
            const uint8_t gone[2] = {m.ankheg_spit, m.acid_bite};
            for (uint8_t ty : gone) {
                const int k = ty ? find_aff(f, ty) : -1;
                if (k >= 0) drop_aff(f, k);
            }
            return n;
        }
    }
    // The giant slug's acid: 30% of its turns at one within 6 squares
    // (its maximum hit points, a save for half); it fights on
    if (hasx(f, m.slug_spit)) {
        const int tg = special_target(b, t, i, 12, false, d);
        const int roll = d.roll(100, 1);
        if (tg >= 0 && squares_to(b, t, i, tg) < 7) {
            if (roll <= 30) {
                ev(i, Ev::SpitsAcid);
                fly(i, b.f[tg].x, b.f[tg].y, 10, 1, 30);
                hurt(tg, f.hp_max(), breath);
            } else {
                ev(i, Ev::SpitsMisses);
            }
        }
    }
    // The beholder's eyes: up to 4, the nearest enemy; by its range the
    // first unused ray (disintegrate 2, stone 3, death 4, wounds 5 squares),
    // else the first unused spell (Fear, Slow, Sleep - the caller casts it)
    if (hasx(f, m.rays)) {
        bool used[7] = {};
        int tg = nearest_enemy(b, t, i);
        for (int pass = 0; pass < 4 && tg >= 0; ++pass) {
            const int r = squares_to(b, t, i, tg);
            Fighter& v = b.f[tg];
            if (!used[0] && r <= 2) {
                used[0] = true;
                ev(i, Ev::RayDisintegrate);
                fly(i, v.x, v.y, 5, 1, 30);
                if (!saving_throw(v, 3, 0, d)) remove(tg, party::Gone, Ev::Disintegrated);
            } else if (!used[1] && r <= 3) {
                used[1] = true;
                ev(i, Ev::RayStone);
                fly(i, v.x, v.y, 10, 1, 30);
                if (!saving_throw(v, 1, 0, d)) remove(tg, party::Stoned, Ev::Stoned);
            } else if (!used[2] && r <= 4) {
                used[2] = true;
                ev(i, Ev::RayDeath);
                fly(i, v.x, v.y, 5, 1, 30);
                if (!saving_throw(v, 0, 0, d)) remove(tg, party::Dead, Ev::Killed);
            } else if (!used[3] && r <= 5) {
                used[3] = true;
                ev(i, Ev::RayWounds);
                fly(i, v.x, v.y, 5, 1, 30);
                Harm h;
                hurt(tg, d.roll(8, 2) + 1, h);
            } else {
                int k = 0;
                while (k < 3 && (used[4 + k] || !m.ray_spells[k])) ++k;
                if (k == 3) break;
                used[4 + k] = true;
                Event* e = ev(i, Ev::Cast);
                if (e) e->amount = m.ray_spells[k];
                continue;
            }
            tg = nearest_enemy(b, t, i);
        }
    }
    // Acid breath (the black dragon): 3 a fight, a line of 6 squares - not
    // when one of its own side is on it; its maximum hit points, a save for half
    if (hasx(f, m.acid_breath)) {
        uint8_t* left = breaths(m.acid_breath);
        const int tg = left && *left ? special_target(b, t, i, 6, true, d) : -1;
        if (tg >= 0) {
            int on[kMaxFighters];
            const int k = cone(b, t, i, b.f[tg].x, b.f[tg].y, 6, 1, on, kMaxFighters);
            bool friend_on = false;
            for (int j = 0; j < k; ++j) friend_on = friend_on || b.f[on[j]].team() == f.team();
            if (k && !friend_on) {
                ev(i, Ev::BreathesAcid);
                fly(i, b.f[tg].x, b.f[tg].y, 5, 1, 30);
                Harm h = breath;
                h.kind = 0x30;
                const int amount = f.hp_max();
                for (int j = 0; j < k; ++j) hurt(on[j], amount, h);
                --*left;
                *ends_turn = true;
                return n;
            }
        }
    }
    // Fire breath (the dracolich): 3 a fight, 3 lines of 9 squares, friends
    // or not; its maximum hit points, a save for half
    if (hasx(f, m.fire_breath)) {
        uint8_t* left = breaths(m.fire_breath);
        const int tg = left && *left ? special_target(b, t, i, 6, true, d) : -1;
        if (tg >= 0) {
            int on[kMaxFighters];
            const int k = cone(b, t, i, b.f[tg].x, b.f[tg].y, 9, 3, on, kMaxFighters);
            ev(i, Ev::BreathesFire);
            fly(i, b.f[tg].x, b.f[tg].y, 5, 1, 30);
            Harm h = breath;
            h.kind = 0x21;
            const int amount = f.hp_max();
            for (int j = 0; j < k; ++j) hurt(on[j], amount, h);
            --*left;
            *ends_turn = true;
            return n;
        }
    }
    // The hell hound: half its turns, next to its pick: 7 fire (a save for half)
    if (hasx(f, m.hound_fire)) {
        const int tg = special_target(b, t, i, 12, false, d);
        const int roll = d.roll(100, 1);
        if (roll <= 50 && tg >= 0 && squares_to(b, t, i, tg) < 2) {
            *ends_turn = true;
            ev(i, Ev::HoundFire);
            fly(i, b.f[tg].x, b.f[tg].y, 10, 1, 30);
            Harm h = breath;
            h.kind = 1;
            hurt(tg, 7, h);
            return n;
        }
    }
    // Thrown lightning (Tyranthraxus): the first 4 rounds, 16d6 to the one
    // aimed at, a second 16d6 to those on the bolt on from it (10 squares);
    // a save against spells for half
    if (hasx(f, m.lightning) && b.round < 4) {
        *ends_turn = true;
        ev(i, Ev::ThrowsLightning);
        const int inv = b.fx->invisible ? find_aff(f, b.fx->invisible) : -1;
        if (inv >= 0) drop_aff(f, inv);
        const int tg = special_target(b, t, i, 10, false, d);
        if (tg >= 0) {
            // As Lightning Bolt (its routine), but 10 squares long, no near rule, a second roll for the path
            Harm h;
            h.kind = 0x0C;
            h.level = 6;
            h.dice = 16;
            h.save = 4;
            h.on_save = 2;
            const int tx = b.f[tg].x, ty = b.f[tg].y;
            Event* e = ev(i, Ev::BoltFly);
            if (e) {
                e->x = static_cast<uint8_t>(tx);
                e->y = static_cast<uint8_t>(ty);
                e->pic = 6;
            }
            hurt(tg, d.roll(6, 16), h);
            int on[32];
            const int k = bolt_path(b, t, i, tx, ty, 10, false, on, 32, b.bolt, &b.n_bolt, 16);
            const int second = d.roll(6, 16);
            for (int j = 0; j < k; ++j) hurt(on[j], second, h);
        }
        return n;
    }
    return n;
}

} // namespace combat

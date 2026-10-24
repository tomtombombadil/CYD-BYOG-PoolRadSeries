#include "combat.h"

#include "magic.h"
#include "rules.h"

#include <cstdlib>
#include <cstring>

namespace combat {

// ---- A monster's record: its group's, with its own changes over it
uint8_t Monster::get(int off) const
{
    if (own) return own[off];
    for (int k = 0; k < n_ch; ++k)
        if (ch_off[k] == off) return ch_val[k];
    return base ? base[off] : 0;
}

void Monster::set(int off, uint8_t v)
{
    if (own) {
        own[off] = v;
        return;
    }
    const uint8_t was = base ? base[off] : 0;
    for (int k = 0; k < n_ch; ++k)
        if (ch_off[k] == off) {
            if (v == was) {                 // back as its group's: no longer a change
                --n_ch;
                ch_off[k] = ch_off[n_ch];
                ch_val[k] = ch_val[n_ch];
            } else {
                ch_val[k] = v;
            }
            return;
        }
    if (v == was) return;
    if (n_ch < kMonsterChanges) {
        ch_off[n_ch] = static_cast<uint16_t>(off);
        ch_val[n_ch++] = v;
        return;
    }
    if (uint8_t* r = whole()) r[off] = v;
}

uint8_t* Monster::whole()
{
    if (own) return own;
    own = static_cast<uint8_t*>(malloc(party::kRecordSize));
    if (!own) return nullptr;
    if (base) memcpy(own, base, party::kRecordSize);
    else memset(own, 0, party::kRecordSize);
    for (int k = 0; k < n_ch; ++k) own[ch_off[k]] = ch_val[k];
    n_ch = 0;
    return own;
}

bool Monster::room_for_aff(int n)
{
    if (n <= aff_cap) return true;
    if (n > kMonsterAffects) return false;
    int cap = aff_cap + 2;
    if (cap < n) cap = n;
    if (cap > kMonsterAffects) cap = kMonsterAffects;
    void* p = realloc(aff, static_cast<size_t>(cap) * party::kAffectSize);
    if (!p) return false;
    aff = static_cast<uint8_t(*)[party::kAffectSize]>(p);
    memset(aff[aff_cap], 0, static_cast<size_t>(cap - aff_cap) * party::kAffectSize);
    aff_cap = static_cast<uint8_t>(cap);
    return true;
}

void Monster::release()
{
    free(own);
    free(aff);
    own = nullptr;
    aff = nullptr;
    n_aff = 0;
    aff_cap = 0;
    n_ch = 0;
}

void Rec::read(uint8_t* out, int off, int n) const
{
    if (p_) memcpy(out, p_ + off, static_cast<size_t>(n));
    else if (m_ && m_->own) memcpy(out, m_->own + off, static_cast<size_t>(n));
    else if (m_) {
        if (m_->base) memcpy(out, m_->base + off, static_cast<size_t>(n));
        else memset(out, 0, static_cast<size_t>(n));
        for (int k = 0; k < m_->n_ch; ++k)
            if (m_->ch_off[k] >= off && m_->ch_off[k] < off + n) out[m_->ch_off[k] - off] = m_->ch_val[k];
    }
}

void Rec::write(const uint8_t* in, int off, int n) const
{
    if (p_) memmove(p_ + off, in, static_cast<size_t>(n));
    else if (m_)
        for (int k = 0; k < n; ++k) m_->set(off + k, in[k]);
}

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
constexpr int kLevels = 0x109, kExp = 0x127, kHpRolled = 0x12C, kDrained = 0xE7, kDrainedHp = 0xE8, kCha = 0x14;


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
    t.by_name = false;                  // (Curse: the class bits - was left unset, read as whatever was there)
    t.turn_cols = at.turn_cols;
    for (int k = 0; k < 5; ++k) t.reserve_mon[k] = t.reserve_pic[k] = 0;
    if (at.reserve && !(read(ctx, at.reserve, t.reserve_mon, 5) && read(ctx, static_cast<uint16_t>(at.reserve + 5), t.reserve_pic, 5)))
        return false;
    return read(ctx, at.ground, &t.ground[0][0], sizeof t.ground) &&
           read(ctx, at.fallback, &t.fallback[0][0], sizeof t.fallback) &&
           read(ctx, at.formation, &t.formation[0][0], sizeof t.formation) &&
           read(ctx, at.facing, t.facing, sizeof t.facing) && read(ctx, at.start_x, t.start_x, sizeof t.start_x) &&
           read(ctx, at.start_y, t.start_y, sizeof t.start_y) &&
           read(ctx, at.shapes, &t.shapes[0][0][0], sizeof t.shapes) &&
           (!at.terrain || read(ctx, at.terrain, t.terrain, sizeof t.terrain)) &&     // (Pool: none)
           ((t.turn_known = at.turn != 0)
                ? read(ctx, at.turn, t.turn, at.turn_cols == 11 ? sizeof t.turn : 13 * 10 + 11)
                : true) &&
           (!at.race_names || (t.by_name = read(ctx, at.race_names, &t.foe_names[0][0], sizeof t.foe_names)));
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

void build_outdoors_dk(Battle& b, const Tables& t, bool river, create::Dice& d)
{
    b.indoors = false;
    const int W = 50, H = 25;
    auto at = [&](int x, int y) -> uint8_t& { return b.ground[y][x]; };
    // (a square's picture: the ground entry's fourth byte - 0x25 the plain grass)
    auto plain = [&](int x, int y) { const uint8_t* g = tile(b, t, x, y); return g && g[3] == 0x25; };
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) b.ground[y][x] = 0x45;
    const int f = t.terrain[15];
    // The river (ovr 0xBEE9): from a column 34 - 5d4 (brought down to one 2
    // short of a multiple of 7) running down and right; a ford (0xBE24)
    // when a d20 is at most the rows since the last one
    if (river) {
        int col = 0x22 - d.roll(4, 5);
        while ((col + 2) % 7 > 0) --col;
        int since = 1;
        for (int y = 0; y < H; ++y) {
            if (col > 0x31) continue;
            if (col >= 0) at(col, y) = 0x34;
            if (col < 0x31) {
                if (d.roll(20, 1) <= since) {
                    const int c = col + 1;
                    if (c >= 0) at(c, y) = 0x3A;
                    if (c < 0x31) at(c + 1, y) = static_cast<uint8_t>(0x3A + d.roll(2, 1));
                    if (c + 1 < 0x31) at(c + 2, y) = static_cast<uint8_t>(0x3A + d.roll(2, 1));
                    if (c + 2 < 0x31) at(c + 3, y) = 0x3D;
                    since = 1;
                } else {
                    ++since;
                    if (col + 1 >= 0) at(col + 1, y) = static_cast<uint8_t>(0x34 + d.roll(2, 1));
                    if (col + 1 < 0x31 && col + 2 >= 0) at(col + 2, y) = 0x37;
                    if (col + 2 < 0x31 && col + 3 >= 0) at(col + 3, y) = 0x37;
                    if (col + 3 < 0x31 && col + 4 >= 0) at(col + 4, y) = static_cast<uint8_t>(0x37 + d.roll(2, 1));
                }
            }
            if (col + 4 < 0x31 && col + 5 >= 0) at(col + 5, y) = 0x34;
            ++col;
        }
    }
    // Trees and logs (ovr 0xC087): column by column
    if (!(f & 0x80)) {
        int n = 5;
        if (f & 0x02) n -= 3;
        if (f & 0x04) n -= 3;
        if (f & 0x40) n += 2;
        if (f & 0x08) n += 5;
        if (n < 0) n = 1;
        for (int x = 0; x < W; ++x)
            for (int y = 1; y < H; ++y) {
                if (!plain(x, y) || !plain(x, y - 1) || n < d.roll(100, 1)) continue;
                if (n < d.roll(100, 1)) {
                    at(x, y - 1) = static_cast<uint8_t>(0x1F + d.roll(5, 1));
                    at(x, y) = static_cast<uint8_t>(0x24 + d.roll(5, 1));
                } else {
                    at(x, y) = static_cast<uint8_t>(0x29 + d.roll(2, 1));
                }
            }
    }
    // Scattered ground (ovr 0xC39C, a square 0xC21E): by the density's band
    int D = 50;
    if (f & 0x10) D += 10;
    if (f & 0x20) D += 30;
    if (f & 0x40) D += 20;
    if (f & 0x04) D -= 10;
    if (f & 0x02) D -= 20;
    if (f & 0x80) D -= 50;
    static const uint8_t kBand[5][5] = {{0, 0, 0, 0x1E, 0x0F}, {0, 1, 5, 0x14, 0x0A}, {0, 2, 5, 0x0A, 5},
                                        {0x0A, 2, 0x0A, 0x0A, 1}, {0x0F, 5, 0x0F, 0x0A, 1}};
    int band = -1;
    if (D >= -30 && D <= 9) band = 0;
    else if (D >= 10 && D <= 29) band = 1;
    else if (D >= 30 && D <= 69) band = 2;
    else if (D >= 70 && D <= 89) band = 3;
    else if (D >= 90 && D <= 110) band = 4;
    if (band < 0) return;
    const uint8_t* k = kBand[band];
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y) {
            if (!plain(x, y)) continue;
            const int r = d.roll(100, 1);
            if (r <= k[0]) at(x, y) = static_cast<uint8_t>(0x31 + d.roll(2, 1));
            else if (r <= k[0] + k[1]) at(x, y) = 0x2C;
            else if (r <= k[0] + k[1] + 1) at(x, y) = static_cast<uint8_t>(0x2C + d.roll(5, 1));
            else if (r <= k[0] + k[1] + k[2] + k[3]) at(x, y) = 0x44;
            else if (r <= k[0] + k[1] + k[2] + k[3] + k[4]) at(x, y) = static_cast<uint8_t>(0x3D + d.roll(6, 1));
        }
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

int pool_terrain(int t, bool special)
{
    struct Range { uint8_t lo, hi, f; };
    // First match wins, in this order (its code's compare chain)
    static const Range kChain[] = {
        {1, 2, 0x01}, {0x28, 0x29, 0x01}, {0x4F, 0x56, 0x01}, {0x5B, 0x5C, 0x01}, {0x2B, 0x2B, 0x01}, {0xF1, 0xF1, 0x01},
        {0xA8, 0xB1, 0x21},
        {0xB9, 0xB9, 0x11}, {0xBC, 0xC0, 0x11},
        {3, 4, 0x03}, {6, 9, 0x03}, {0x13, 0x15, 0x03}, {0x17, 0x19, 0x03},
        {7, 7, 0x02}, {0x0A, 0x12, 0x02}, {0x16, 0x16, 0x02}, {0x1A, 0x24, 0x02}, {0xC9, 0xD0, 0x02},
        {0xD1, 0xD5, 0x12},
        {0x25, 0x25, 0x04}, {0x39, 0x39, 0x04}, {0x40, 0x40, 0x04}, {0x9A, 0x9E, 0x04}, {0xA4, 0xA5, 0x04},
        {0xB4, 0xB4, 0x04}, {0xF3, 0xF3, 0x04}, {0xEF, 0xF0, 0x04}, {0xFA, 0xFA, 0x04}, {0xFC, 0xFC, 0x04},
        {0x27, 0x2C, 0x05}, {0x99, 0x99, 0x05}, {0x9F, 0x9F, 0x05},
        {0x2D, 0x2E, 0x44}, {0x33, 0x38, 0x44}, {0x43, 0x4D, 0x44}, {0x5D, 0x61, 0x44},
        {0xB5, 0xB8, 0x14}, {0xBA, 0xBA, 0x14}, {0xC1, 0xC8, 0x14},
        {0x2F, 0x32, 0x40}, {0x62, 0x65, 0x40}, {0xF2, 0xF2, 0x40},
        {0x26, 0x26, 0x41},
        {0xB2, 0xB3, 0x60},
        {0x3A, 0x3C, 0x48}, {0x41, 0x42, 0x48},
        {0x3D, 0x3F, 0x08}, {0x79, 0x7C, 0x08}, {0x87, 0x89, 0x08}, {0x8B, 0x8B, 0x08}, {0x8D, 0x8D, 0x08},
        {0x90, 0x93, 0x08}, {0x95, 0x95, 0x08}, {0x97, 0x98, 0x08}, {0xA0, 0xA2, 0x08}, {0xA6, 0xA7, 0x08},
        {0xD6, 0xD8, 0x08}, {0xEE, 0xEE, 0x08},
        {0x57, 0x5A, 0x88}, {0xA3, 0xA3, 0x88}, {0xE0, 0xE0, 0x88},
        {0x7C, 0x7D, 0x09}, {0x8A, 0x8A, 0x09}, {0x8C, 0x8C, 0x09}, {0x8E, 0x8F, 0x09}, {0x94, 0x94, 0x09},
        {0x96, 0x96, 0x09},
        {0x7E, 0x85, 0x28}, {0xE5, 0xE7, 0x28},
        {0xD9, 0xDF, 0x18},
        {0x4E, 0x4E, 0x20}, {0xF5, 0xF5, 0x20}, {0xE8, 0xE9, 0x20},
        {0x6D, 0x6D, 0x80}, {0xF4, 0xF6, 0x80},
        {0x6E, 0x71, 0x81}, {0x73, 0x78, 0x81},
        {0x66, 0x6C, 0x90}, {0x72, 0x72, 0x90},
        {0xE1, 0xE4, 0xA0}, {0xEA, 0xED, 0xA0},
    };
    int f = 0;
    for (const Range& r : kChain)
        if (t >= r.lo && t <= r.hi) {
            f = r.f;
            break;
        }
    if ((f & 0x80) && special) f = (f - 0x80) | 1;
    return f;
}

void build_outdoors_pool(Battle& b, int f, create::Dice& d)
{
    b.indoors = false;
    const uint8_t plain = 0x36;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) b.ground[y][x] = plain;
    auto set = [&](int x, int y, uint8_t v) {
        if (x >= 0 && x < kW && y >= 0 && y < kH) b.ground[y][x] = v;
    };
    // The stream (and a crossing, always one)
    const int chance = (f & 0x10) ? 75 : (f & 0x20) ? 35 : 0;
    if (chance && d.roll(100, 1) <= chance) {
        int col = 34 - 5 * d.roll(4, 1);
        while ((col + 2) % 7 != 0) --col;
        const int start = col;
        bool crossing = false;
        for (int row = 0; row < kH && col <= 49; ++row, ++col) {
            set(col, row, 0x32);
            set(col + 1, row, 0x33);
            if (d.roll(20, 1) == 1) {
                set(col, row, 0x34);
                set(col + 1, row, 0x35);
                crossing = true;
            }
        }
        if (!crossing) {
            const int r = 16 - d.roll(9, 1), c = start + r;
            set(c, r, 0x34);
            set(c + 1, r, 0x35);
        }
    }
    // Trees (the tops above)
    int n = 1;
    if (f & 0x02) n = 1;
    if (f & 0x04) n = 3;
    if (f & 0x40) n = 4;
    if (f & 0x08) n = 7;
    if (f & 0x80) n = 0;
    for (int y = 1; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            if (b.ground[y][x] != plain || b.ground[y - 1][x] != plain || d.roll(100, 1) > n) continue;
            int r = d.roll(10, 1);
            r = r > 8 ? 4 + d.roll(2, 1) : (r + 1) / 2;
            if (f & 0x40) r = 3 + d.roll(3, 1);
            b.ground[y][x] = static_cast<uint8_t>(0x23 + r);
            if (r < 5) b.ground[y - 1][x] = static_cast<uint8_t>(0x1F + r);
        }
    // The rest scattered
    int A = 0, B = 6, C = 15, D = 40, E = 0;
    if (f & 0x02) {
        C = 45;
        D = 10;
    }
    if (f & 0x04) C = 25;
    if (f & 0x40) {
        D = 5;
        E = 30;
    }
    if (f & 0x80) {
        A = 15;
        D = 25;
        C = 10;
    }
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            if (b.ground[y][x] != plain) continue;
            const int v = d.roll(255, 1);
            if (v <= A) {
                const int r = d.roll(4, 1);
                if (r == 4 && y > 0 && b.ground[y - 1][x] == plain) {
                    b.ground[y - 1][x] = 0x41;
                    b.ground[y][x] = 0x40;
                } else if (r < 4) {
                    b.ground[y][x] = static_cast<uint8_t>(0x3C + r);
                }
            } else if (v <= A + B) {
                b.ground[y][x] = static_cast<uint8_t>(0x36 + d.roll(3, 1));
            } else if (v <= A + B + C) {
                b.ground[y][x] = static_cast<uint8_t>(0x29 + d.roll(4, 1));
            } else if (v <= A + B + C + E) {
                b.ground[y][x] = static_cast<uint8_t>(0x38 + ((d.roll(10, 1) - 1) / 3 + 1));
            } else if (v <= A + B + C + E + D) {
                int r = d.roll(4, 1);
                if (r == 4 && !(f & 0x40)) r = d.roll(3, 1);
                b.ground[y][x] = static_cast<uint8_t>(0x2D + r);
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

void place(Battle& b, const Tables& t, int facing, int distance, SolidSide solid, void* ctx, int sides)
{
    const int party_f0 = (facing / 2) & 3;
    bool used[2][4][6][11] = {};
    int width[2] = {};
    for (int i = 0; i < b.n; ++i)
        if (b.f[i].up()) ++width[b.f[i].team() ? 1 : 0];
    // Darkness (GAME.OVR 0xCF9C): the enemies come from the sides SETUP
    // MONSTER named (bits 0-3 north, east, south, west), one after another -
    // east first -, each placed as if the party faced that way; a side's
    // rank (count + 2n - 1) / 2n wide for n sides
    sides &= 15;
    int n_sides = 0;
    for (int k = 0; k < 4; ++k) n_sides += (sides >> k) & 1;
    width[0] = (width[0] + 1) / 2;
    width[1] = n_sides ? (width[1] + 2 * n_sides - 1) / (2 * n_sides) : (width[1] + 1) / 2;
    // (the original's 0xCD6D: each change of facing starts the placing
    // grids afresh, and enemies on another side than the party faces come
    // at least 1 square away)
    int side = 0, last_f0 = party_f0;
    memset(b.who, 0, sizeof b.who);
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        const int team = f.team() ? 1 : 0;
        const int grid = team;
        int f0 = party_f0;
        if (team && n_sides) {
            do side = (side + 1) & 3;
            while (!((sides >> side) & 1));
            f0 = side;
        }
        if (n_sides && f0 != last_f0) memset(used, 0, sizeof used);
        last_f0 = f0;
        const int dist = n_sides && distance == 0 && f0 != party_f0 ? 1 : distance;
        const int tf = team ? (f0 + 2) & 3 : f0;           // the team's half facing
        f.facing = team ? (t.facing[f0] + 4) & 7 : t.facing[f0];
        int size = f.rec[kSize] & 7;
        if (size < 1 || size > 4) size = 1;
        if (f.member >= 0) size = 1;
        const int tx0 = team ? dist * geo::dx(f0 * 2) : 0, ty0 = team ? dist * geo::dy(f0 * 2) : 0;
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
                    if (gx < row[0] || gx > row[1] || used[grid][at][gy][gx]) continue;
                    const int x = gx + 6 * tx + 5 * ty + 22, y = gy + 5 * ty + 10;
                    if (!fits(b, t, i, f.up() ? size : 1, x, y)) continue;
                    used[grid][at][gy][gx] = true;
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

Fighter* add_fighter(Battle& b)
{
    if (!b.f || b.n >= b.cap) return nullptr;
    Fighter& f = b.f[b.n++];
    f = Fighter{};
    return &f;
}

bool helpless(const Battle& b, const Fighter& f)
{
    if (!b.fx) return false;
    for (uint8_t h : b.fx->held)
        if (h && f.has(h)) return true;
    return false;
}

bool stunned(const Battle& b, const Fighter& f) { return b.fx && b.fx->sp.stun && f.has(b.fx->sp.stun); }

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

// An image of Mirror Image taken (effect k): Curse's whole data byte counts
// down, gone at 0; Secret's images (data >> 4) one fewer, gone at none
// (its handler GAME.OVR 0x117CE)
// The clouds' ground values (the game's, else Curse's)
static uint8_t cloud_g(const Battle& b) { return b.fx && b.fx->cloud_ground ? b.fx->cloud_ground : kCloudGround; }
static uint8_t poison_g(const Battle& b) { return b.fx && b.fx->poison_ground ? b.fx->poison_ground : kPoisonGround; }

// An item's name words (0x2F-0x31) include w (0: never)
static bool has_word(const uint8_t* it, uint8_t w)
{
    return w && (it[0x2F] == w || it[0x30] == w || it[0x31] == w);
}

static void lose_image(Battle& b, Fighter& f, int k)
{
    if (b.fx && (b.fx->secret || b.fx->gw.mirror)) {
        const int images = (f.aff[k][3] >> 4) - 1;
        if (images <= 0) drop_aff(f, k);
        else f.aff[k][3] = static_cast<uint8_t>(images << 4 | (f.aff[k][3] & 0x0F));
        return;
    }
    if (--f.aff[k][3] == 0) drop_aff(f, k);
}

void give_aff(Fighter& f, int type, int minutes, int data, bool call)
{
    if (!f.n_aff) return;
    const int i = find_aff(f, static_cast<uint8_t>(type));
    if (i >= 0 && (f.aff[i][1] | f.aff[i][2] << 8) > 0) drop_aff(f, i);
    if (*f.n_aff >= f.max_aff) return;
    if (Monster* m = f.rec.monster()) {     // (a monster's effects grow as they come)
        if (!m->room_for_aff(*f.n_aff + 1)) return;
        f.aff = m->aff;
    }
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

bool stand_up(Battle& b, int i, bool full);

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
    let_go(b, f, m.sucking);
    let_go(b, f, m.holding);
    let_go(b, f, b.fx->gw.sucking);
    let_go(b, f, b.fx->gw.constrict);
    const uint8_t regen[2] = {m.regen_wait, m.regen};
    for (uint8_t t : regen) {
        const int k = t ? find_aff(f, t) : -1;
        if (k >= 0) drop_aff(f, k);
    }
    // Gateway's boar (0x63 first in its death list - coab's sub_3BEE8):
    // dying under 6 below, or unconscious, it gets up at once with 6 - that
    // many hit points (6), its square free ("stands up and grins"), and falls
    // dead d4 + 1 rounds on (0x5F); the 0x63 goes
    const Facts::GateFx& gw = b.fx->gw;
    const int fo = gw.fights_on && gw.falls_dead ? find_aff(f, gw.fights_on) : -1;
    if (fo >= 0) {
        int amount = 0;
        if (f.status() == party::Dying && f.bleeding < 6) amount = 6 - f.bleeding;
        if (f.status() == party::Unconscious) amount = 6;
        if (amount > 0 && stand_up(b, c, false)) {
            f.rec[kHp] = static_cast<uint8_t>(amount);
            give_aff(f, gw.falls_dead, (g_dice ? g_dice->roll(4, 1) : 1) + 1, 0xFF, true);
            const int k = find_aff(f, gw.fights_on);
            if (k >= 0) drop_aff(f, k);
            b.risen = c;
            return;
        }
    }
    if (hasx(f, m.troll_death) && m.troll_up && !(g_kind & 0x11) && f.status() != party::Stoned &&
        f.status() != party::Gone)
        give_aff(f, m.troll_up, g_dice ? g_dice->roll(6, 3) : 10, 0xFF, true);
}

} // namespace

void uncharm(const Rec& rec, const uint8_t* a) { rec[0x197] = static_cast<uint8_t>((a[3] & 0x40) >> 6); }

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
        if ((fx.mon.engulfing && type == fx.mon.engulfing) || (fx.mon.hugging && type == fx.mon.hugging) ||
            (fx.mon.sucking && type == fx.mon.sucking) || (fx.mon.holding && type == fx.mon.holding) ||
            (fx.gw.sucking && type == fx.gw.sucking) || (fx.gw.constrict && type == fx.gw.constrict)) {
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
    // Gateway: held fast - no moving, no spells this turn (its turn-start
    // list 15 1E 0B 0D 4D 3A; handler GAME.OVR 0x130DA)
    if (fx.gw.ways && fx.mon.held_fast && hasx(f, fx.mon.held_fast)) {
        f.moves = 0;
        f.can_cast = false;
    }
    // Berserk (the Berserker sword; the turn-start list, after silence,
    // clouds, charm, suffocation): the listing's spl_berzerk (ovr013:17B3)
    // makes it computer-run, control 0xB3 (an NPC's 0xB2), takes as its
    // target the SECOND entry of sub_738D8's list of every combatant on the
    // field sorted by range (byte_1D1C4 = DS 6EB4, entries from 6EB1 three
    // bytes each - the first, at range 0, is the wearer itself: the nearest
    // other creature, friend or foe), no spells, and puts it on the side
    // against that target ("goes berzerk")
    if (fx.berserk && f.has(fx.berserk) && f.size) {
        f.quick = true;
        f.rec[0x198] = 1;
        f.rec[kControl] = f.rec[kControl] <= 0x7F || f.rec[kControl] == 0xB3 ? 0xB3 : 0xB2;
        int near = -1, best = 9999;
        for (int c = 0; c < b.n; ++c) {
            const Fighter& o = b.f[c];
            if (c == i || !o.size || o.gone) continue;
            int sq = 9999;
            if (b.tables) {
                if (!range(b, *b.tables, i, c, false, &sq)) continue;
            } else {
                const int ddx = o.x > f.x ? o.x - f.x : f.x - o.x, ddy = o.y > f.y ? o.y - f.y : f.y - o.y;
                sq = ddx > ddy ? ddx : ddy;
            }
            if (sq < best) {
                best = sq;
                near = c;
            }
        }
        if (near >= 0) {
            f.target = near;
            f.can_cast = false;
            f.rec[kTeam] = static_cast<uint8_t>(b.f[near].team() ^ 1);
            return TurnFx::Berzerk;
        }
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
        bool regen = false, rise = false, sting = false, slow_over = false, con_up = false, dies = false;
        int weak_due = 0, sick_due = 0, ill_over = -1;
        for (int k = 0; f.n_aff && k < *f.n_aff;) {
            const int m = f.aff[k][1] | f.aff[k][2] << 8;
            const uint8_t ty = f.aff[k][0];
            if (m == 0) {
                ++k;
            } else if (m <= 1 && b.fx && ((b.fx->sp.weak && ty == b.fx->sp.weak) || (b.fx->sp.sick && ty == b.fx->sp.sick))) {
                // The disease's course renews itself (60 / 10 minutes)
                const bool weak = ty == b.fx->sp.weak;
                f.aff[k][1] = static_cast<uint8_t>(weak ? 60 : 10);
                f.aff[k][2] = 0;
                ++(weak ? weak_due : sick_due);
                ++k;
            } else if (m <= 1) {
                if (b.fx && b.fx->sp.ill && ty == b.fx->sp.ill) ill_over = f.aff[k][3];
                if (b.fx && b.fx->mon.regen_wait && f.aff[k][0] == b.fx->mon.regen_wait) regen = true;
                if (b.fx && b.fx->mon.troll_up && f.aff[k][0] == b.fx->mon.troll_up) rise = true;
                if (b.fx && b.fx->sp.poison_damage && f.aff[k][0] == b.fx->sp.poison_damage) sting = true;
                if (b.fx && b.fx->sp.slow_poison && f.aff[k][0] == b.fx->sp.slow_poison) slow_over = true;
                if (b.fx && b.fx->sp.con_regen && f.aff[k][0] == b.fx->sp.con_regen) con_up = true;
                if (b.fx && b.fx->gw.falls_dead && f.aff[k][0] == b.fx->gw.falls_dead) dies = true;
                end_effect(b, f, k);
            } else {
                f.aff[k][1] = static_cast<uint8_t>(m - 1);
                f.aff[k][2] = static_cast<uint8_t>((m - 1) >> 8);
                ++k;
            }
        }
        if (!b.fx) continue;
        const MonFx& mf = b.fx->mon;
        // Cause Disease run out: weakness and sickness begin (coab's facts,
        // listing ovr013:1038 / 10C2) - each a step now, then every 60 / 10
        if (ill_over >= 0 && b.fx->sp.weak && b.fx->sp.sick) {
            give_aff(f, b.fx->sp.weak, 60, ill_over, true);
            give_aff(f, b.fx->sp.sick, 10, ill_over, true);
            ++weak_due;
            ++sick_due;
        }
        auto ev_of = [&](Ev e, int amount) {
            if (!out || n >= cap) return;
            out[n] = Event{};
            out[n].who = static_cast<uint8_t>(i);
            out[n].ev = e;
            out[n++].amount = static_cast<int16_t>(amount);
        };
        auto helpless_now = [&]() {
            const uint8_t h = b.fx->held[3];
            if (h && !f.has(h)) give_aff(f, h, 0, 0xFF, false);
        };
        for (; weak_due > 0; --weak_due) {
            // Weakness: the Strength in use - 1, "is weakened"; at 3 helpless
            if (f.rec[0x11] > 3) {
                --f.rec[0x11];
                ev_of(Ev::Weakened, 0);
            } else {
                helpless_now();
            }
        }
        for (; sick_due > 0; --sick_due) {
            // Sickness: a point of damage while over 1 HP ("takes 1 point of damage"), else helpless
            if (f.hp() > 1) {
                damage(b, i, 1);
                ev_of(Ev::Damage, 1);
                // (no damage kind: the original's message still says "from Magic" - its test is
                // flags & 8 == flags, true for 0)
                if (out && n > 0 && out[n - 1].ev == Ev::Damage) out[n - 1].kind = 8;
            } else {
                helpless_now();
            }
        }
        // The elves' stealth (95 in 100) and the Ring of Invisibility:
        // invisible again for the next round (the original's round-end
        // list: 0x48 before 0x38)
        if (b.fx->invisible && b.fx->items.elvenkind && hasx(f, b.fx->items.elvenkind) && !f.has(b.fx->invisible) &&
            g_dice && g_dice->roll(100, 1) <= 95)
            give_aff(f, b.fx->invisible, 1, 12, false);
        if (b.fx->items.ring_invisible && b.fx->invisible && hasx(f, b.fx->items.ring_invisible) &&
            !f.has(b.fx->invisible))
            give_aff(f, b.fx->invisible, 1, 12, false);
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
        // Gateway's boar (0x5F, GAME.OVR - coab's sub_3BE06): its rounds over,
        // still in the fight - "Falls dead"
        if (dies && f.up()) {
            f.rec[kHp] = 0;
            f.rec[kHealth] = party::Dead;
            fall(b, i);
            if (n < cap) {
                out[n] = Event{};
                out[n].who = static_cast<uint8_t>(i);
                out[n++].ev = Ev::FallsDead;
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
    // Gateway's Vaalgamon (0xA1, GAME.OVR 0x15365 - the round's end list's
    // last): hurt, a hit point back - "regenerates"
    for (int i = 0; b.fx && b.fx->gw.regen1 && i < b.n; ++i) {
        Fighter& f = b.f[i];
        if (!f.up() || !f.has(b.fx->gw.regen1) || f.hp() >= f.hp_max()) continue;
        f.rec[kHp] = static_cast<uint8_t>(f.hp() + 1);
        if (out && n < cap) {
            out[n] = Event{};
            out[n].who = static_cast<uint8_t>(i);
            out[n++].ev = Ev::Regenerates;
        }
    }
    return n;
}

void battle_start(Battle& b, create::Dice* d)
{
    g_b = &b;
    g_fx = b.fx;
    if (d) g_dice = d;
    if (!b.fx) return;
    for (int i = 0; i < b.n; ++i) {
        Fighter& f = b.f[i];
        // Displacement: the first attack on them misses again this fight
        const int k = b.fx->items.displace ? find_aff(f, b.fx->items.displace) : -1;
        if (k >= 0) f.aff[k][3] &= 0x0F;
        if (!b.fx->invisible) continue;
        if (f.size && f.up() && hasx(f, b.fx->mon.start_invisible)) give_aff(f, b.fx->invisible, 255, 0xFF, false);
        // The elves' stealth (95 in 100) and the Ring of Invisibility:
        // invisible (for the round; again at each round's end - tick)
        if (b.fx->items.elvenkind && hasx(f, b.fx->items.elvenkind) && !f.has(b.fx->invisible) && g_dice &&
            g_dice->roll(100, 1) <= 95)
            give_aff(f, b.fx->invisible, 1, 12, false);
        if (b.fx->items.ring_invisible && hasx(f, b.fx->items.ring_invisible) && !f.has(b.fx->invisible))
            give_aff(f, b.fx->invisible, 1, 12, false);
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

int shot_item(const Battle& b, const Fighter& f, bool* can)
{
    *can = false;
    if (!b.names) return -1;
    const int w = weapon(f, *b.names);
    if (w < 0) return -1;
    const items::TypeInfo& ti = b.names->type(f.items[w][0x2E]);
    if (ti.range <= 1) return -1;
    // (the original's: a thrown weapon itself; a launcher (8) its arrows (1)
    // or quarrels (0x80), even when none are readied; a sling (just 2 | 8) nothing)
    int item = (ti.flags & 0x10) ? w : -1;
    if (ti.flags & 0x08) {
        int arrows = -1, quarrels = -1;
        for (int k = 0; k < f.n_items; ++k) {
            if (!f.items[k][0x34]) continue;
            if (f.items[k][0x2E] == b.arrow) arrows = k;
            if (f.items[k][0x2E] == b.quarrel) quarrels = k;
        }
        if (ti.flags & 0x01) item = arrows;
        if (ti.flags & 0x80) item = quarrels;
    }
    *can = item >= 0 || ti.flags == 0x0A;
    return item;
}

// Slot 1's attacks this round (coab's facts: reclac_attacks): half attacks
// from the record (a monster with none in slot 1 has none - the spiders
// bite with slot 2 only); a missile weapon readied that can shoot (a thrown
// one, a launcher with its arrows / quarrels, a sling) the ITEMS file's
// number for it instead, at least 2; haste x 2, slow / 2; no more attacks
// than the missiles it has
int slot1_attacks(const Battle& b, const Fighter& f, bool* ranged)
{
    int half1 = f.rec[kHalf1];
    int missiles = 0;
    *ranged = false;
    if (b.names && f.items) {
        const int w = weapon(f, *b.names);
        const items::TypeInfo* ti = w >= 0 ? &b.names->type(f.items[w][0x2E]) : nullptr;
        if (ti && ti->range > 1) {
            const uint8_t* shot = (ti->flags & 0x10) ? f.items[w] : nullptr;
            if (ti->flags & 0x08)
                for (int k = 0; k < f.n_items; ++k) {
                    const int ty = f.items[k][0x2E];
                    if (f.items[k][0x34] && (((ti->flags & 0x01) && ty == b.arrow) || ((ti->flags & 0x80) && ty == b.quarrel)))
                        shot = f.items[k];
                }
            if (shot || ti->flags == 0x0A) {
                half1 = ti->attacks < 2 ? 2 : ti->attacks;
                missiles = shot ? shot[0x39] : 0;
                *ranged = true;
            }
        }
    }
    if (b.fx && f.has(b.fx->haste)) half1 *= 2;
    if (b.fx && f.has(b.fx->slow)) half1 /= 2;
    int n = attacks_this_round(half1, b.round);
    if (missiles > 0 && missiles < n) n = missiles;
    return n;
}

void recount_attacks(Battle& b, int i)
{
    Fighter& f = b.f[i];
    bool ranged = false;
    const int now = f.attacks[0];
    const int n = slot1_attacks(b, f, &ranged);
    // Never back the attacks already used: once it has attacked, only fewer
    // (or, in melee, up to twice what was left)
    if (!f.attacked || n < now || (n < now * 2 && !ranged)) f.attacks[0] = n;
}

int start_round(Battle& b, create::Dice& d, Event* out, int cap)
{
    int n_ev = 0;
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
        // Hasted: a year older the first time ("ages"; the effect's data +
        // 0x10 marks it - listing ovr013:0BA5)
        const int hk = b.fx && b.fx->haste ? find_aff(f, b.fx->haste) : -1;
        if (hk >= 0 && !(f.aff[hk][3] & 0x10)) {
            f.aff[hk][3] = static_cast<uint8_t>(f.aff[hk][3] + 0x10);
            const int age = (f.rec[0x76] | f.rec[0x77] << 8) + 1;
            f.rec[0x76] = static_cast<uint8_t>(age);
            f.rec[0x77] = static_cast<uint8_t>(age >> 8);
            if (out && n_ev < cap) {
                out[n_ev] = Event{};
                out[n_ev].who = static_cast<uint8_t>(i);
                out[n_ev++].ev = Ev::Ages;
            }
        }
        int delay = d.roll(6, 1) + dex_reaction(f.rec[kDexFull]);
        if (delay < 1) delay = 1;
        if (b.surprise & (f.team() + 1)) delay -= 6;       // (the word: 1 our side, 2 the enemies - the listing's sub_3E000)
        if (delay < 0 || delay > 20) delay = 0;
        f.delay = delay;
        if (helpless(b, f) || stunned(b, f)) f.delay = 0;
        int mv = f.rec[kMove];
        if (mv < 1 || mv > 96) mv = 1;
        f.moves = mv * 2;
        if (b.fx && f.has(b.fx->haste)) f.moves *= 2;
        if (b.fx && f.has(b.fx->slow)) f.moves /= 2;
        bool ranged = false;
        f.attacks[0] = slot1_attacks(b, f, &ranged);
        int half2 = f.rec[kHalf2];
        // Darkness's bulette (0x63, facts L5): its handler sets 8 half-attacks
        // one round in four, else 4 - written over at once in the original (it
        // never leaps); with the rules fixes as meant (Tom, 2026-10-23; SPEC 12)
        if (b.fx && b.fx->dk.rake && rules::fixes() && hasx(f, b.fx->dk.rake)) half2 = d.roll(4, 1) == 1 ? 8 : 4;
        if (b.fx && f.has(b.fx->haste)) half2 *= 2;
        if (b.fx && f.has(b.fx->slow)) half2 /= 2;
        f.attacks[1] = attacks_this_round(half2, b.round);
        if (b.weather < 2 && !(b.weather_free && f.has(b.weather_free)))
            f.moves = static_cast<int16_t>((f.moves & 0xFF) * (b.weather == 0 ? 3 : 2) / 5);
        if (b.fx && b.fx->entangle && f.has(b.fx->entangle)) f.moves = 0;      // entangled: no moving
        if (b.fx && hasx(f, b.fx->mon.held_fast)) f.moves = 0;                 // engulfed, hugged
    }
    b.surprise = 0;
    // Pool's fear auras (its effect group 8, the round's start;
    // poolrad_combat_facts.md 4): each one in the fight on the other side
    // saves or is struck with fear
    if (b.fx && (b.fx->mon.dragon_fear || b.fx->mon.mummy_fear)) {
        const MonFx& m = b.fx->mon;
        auto said = [&](int who, Ev e) {
            if (!out || n_ev >= cap) return;
            out[n_ev] = Event{};
            out[n_ev].who = static_cast<uint8_t>(who);
            out[n_ev++].ev = e;
        };
        // (its side's count: DS 0x6814 + side, which the fight's end also
        // tests for 0 - those standing, as far as read)
        int side_count[2] = {};
        for (int i = 0; i < b.n; ++i)
            if (b.f[i].up() && b.f[i].size) ++side_count[b.f[i].team() ? 1 : 0];
        for (int i = 0; i < b.n; ++i) {
            Fighter& f = b.f[i];
            const bool dragon = hasx(f, m.dragon_fear), mummy = hasx(f, m.mummy_fear);
            if (!dragon && !mummy) continue;
            // The mummy's works once a fight: every mummy's aura goes with the first
            if (mummy)
                for (int k = 0; k < b.n; ++k) {
                    const int j = find_aff(b.f[k], m.mummy_fear);
                    if (j >= 0) drop_aff(b.f[k], j);
                }
            for (int c = 0; c < b.n; ++c) {
                Fighter& v = b.f[c];
                if (!v.up() || v.team() == f.team()) continue;
                if (dragon) {
                    // Every round: Hit Dice 0-3 paralysed (for the fight), 4-5 cursed, 6 and up unaffected
                    const int hd = static_cast<int8_t>(v.rec[kHd]);
                    if (hd > 5 || saving_throw(v, 4, 0, d)) continue;
                    if (hd <= 3) {
                        if (m.paralyzed && !v.has(m.paralyzed)) {
                            give_aff(v, m.paralyzed, 0, 12, false);
                            said(c, Ev::FearParalyzed);
                        }
                    } else if (b.fx->curse && !v.has(b.fx->curse)) {
                        give_aff(v, b.fx->curse, 0, 12, false);
                        said(c, Ev::FearWeakened);
                    }
                } else {
                    // Once: d4 rounds; the save +1 for a side of 6 or more, +2 for humans
                    const int bonus = (side_count[v.team() ? 1 : 0] >= 6 ? 1 : 0) + (v.rec[0x74] == 7 ? 2 : 0);
                    if (!m.paralyzed || saving_throw(v, 0, bonus, d)) continue;
                    give_aff(v, m.paralyzed, d.roll(4, 1), 12, false);
                    said(c, Ev::FearParalyzed);
                }
            }
        }
    }
    // Darkness's dragon fear (R14: effect 0x64, GAME.OVR 0x1CC4B - phase 8 at
    // the fight's set-up, ovr 0xD4AD, and phase 0x18 at each round's start,
    // ovr 0x83C9): each one of the other side up and in the fight saves
    // against petrification at -4, else (when not yet) terrified for the
    // fight - its blows -2 to hit and -2 damage
    if (b.fx && b.fx->dk.dragon_fear && b.fx->dk.feared) {
        const auto& k = b.fx->dk;
        for (int pass = b.round == 0 ? 2 : 1; pass > 0; --pass)
            for (int i = 0; i < b.n; ++i) {
                const Fighter& f = b.f[i];
                if (!f.size || !f.up() || !hasx(f, k.dragon_fear)) continue;
                for (int c = 0; c < b.n; ++c) {
                    Fighter& v = b.f[c];
                    if (!v.size || v.status() != party::Okay || v.team() == f.team()) continue;
                    if (saving_throw(v, 1, -4, d) || hasx(v, k.feared)) continue;
                    give_aff(v, k.feared, 0, 0, false);
                    if (out && n_ev < cap) {
                        out[n_ev] = Event{};
                        out[n_ev].who = static_cast<uint8_t>(c);
                        out[n_ev++].ev = Ev::Terrified;
                    }
                }
            }
    }
    return n_ev;
}

int next(Battle& b, create::Dice& d)
{
    int best = -1, best_delay = 0, best_roll = -1;
    for (int i = 0; i < b.n; ++i) {
        const Fighter& f = b.f[i];
        if (f.delay <= 0 || !f.up() || !f.size || helpless(b, f) || stunned(b, f)) continue;  // the held lose their turns
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
    // Pool's vampire at 0 hit points: "Turns gaseous and escapes" - away
    // (as fled), not down
    if (b.fx && hasx(f, b.fx->mon.gas_escape)) {
        f.rec[kHealth] = party::Running;
        f.escaped = true;
        fall(b, c);
        return true;
    }
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
    const Facts::GateFx& gw = b.fx->gw;
    // Held fast (engulfed, hugged): no more moving this turn (Gateway's
    // handler, called at once: no spells either)
    auto hold = [&](uint8_t holding) {
        give_aff(tg, m.held_fast, 0, c, false);
        tg.moves = 0;
        if (gw.ways) tg.can_cast = false;
        give_aff(at, holding, 0, c, true);
    };
    // Pool's: a level or two drained; the mummy's disease (no save: the
    // rot's clock 10 days, the rot itself d6 months - its data's high
    // nibble - checked every 30 days); paralysis (the ghoul's not on elves)
    auto pool_hit = [&]() {
        if (hasx(at, m.drain1) && tg.up()) drain(b, c, 1, out.ev, 8, &out.n_ev, out.n - 1);
        if (hasx(at, m.drain2) && tg.up()) drain(b, c, 2, out.ev, 8, &out.n_ev, out.n - 1);
        if (hasx(at, m.rot_touch) && m.rot_clock && m.rot && tg.up() && !tg.has(m.rot_clock)) {
            give_aff(tg, m.rot_clock, 14400, 0xFF, true);
            give_aff(tg, m.rot, 43200, (d.roll(6, 1) << 4) + 0x0F, true);
            add_event(out, c, Ev::Diseased);
        }
        auto paralyze = [&](int rounds, int bonus) {
            if (!m.paralyzed || !tg.up() || saving_throw(tg, 0, bonus, d)) return;
            give_aff(tg, m.paralyzed, rounds, 12, false);
            add_event(out, c, Ev::Paralyzed);
        };
        if (hasx(at, m.ghoul) && tg.rec[0x74] != 2) paralyze(63, 0);
        if (slot == 1 && hasx(at, m.drider)) paralyze(d.roll(9, 1) + 10, -2);
    };
    if (slot == 0) {
        pool_hit();
        if (hasx(at, m.fire_touch)) touch(d.roll(10, 2), 9);
        if (hasx(at, m.acid_bite)) touch(d.roll(4, 1), 0x10);
        // (Gateway's, GAME.OVR 0x1A47A: a d10 of 4 or less at each hit, not
        // while it engulfs one already - Curse's both hits)
        const bool engulf_ok = hasx(at, m.engulf) && (gw.ways ? d.roll(10, 1) <= 4 && !at.has(m.engulfing) : hits1 == 2);
        if (engulf_ok && tg.up() && m.held_fast && m.suffocate && m.engulfing &&
            !tg.has(m.held_fast) && !tg.has(m.suffocate)) {
            add_event(out, a, Ev::Engulfs, c);
            hold(m.engulfing);
            give_aff(tg, m.suffocate, 0, d.roll(4, 2), false);
        }
        if (hasx(at, m.hug) && roll >= 18 && tg.up() && m.held_fast && m.hugging && !at.has(m.hugging) &&
            !(gw.ways && tg.has(m.held_fast))) {
            add_event(out, a, Ev::Hugs, c);
            hold(m.hugging);
        }
        if (hasx(at, m.draco_touch) && m.paralyzed && tg.up() && !saving_throw(tg, 0, 0, d)) {
            give_aff(tg, m.paralyzed, 0, 0xFF, false);
            add_event(out, c, Ev::ParalyzedLow);
        }
        if (hasx(at, m.chill)) touch(d.roll(8, 2), 0x0A);
        // Gateway's after attack 1's hit (its list 4F 50 91 39 60 7A 7B 95 9A
        // 9B 40 43 9F): the stirge's and the squid's holds (a d10 of 4 or
        // less), the harpy's charm, the deadly poison, the paralysing bite,
        // the mummy's disease
        if (gw.ways) {
            auto latch_gw = [&](uint8_t own, uint8_t holding, Ev said, int minutes) {
                if (!hasx(at, own) || !holding || !m.held_fast) return;
                if (d.roll(10, 1) > 4 || !at.up() || !tg.up() || at.has(holding) || tg.has(m.held_fast)) return;
                add_event(out, a, said, c);
                give_aff(tg, m.held_fast, 0, c, false);
                tg.moves = 0;
                tg.can_cast = false;
                give_aff(at, holding, minutes, c, true);
            };
            // (with the fixes the stirge holds 12 hit points' blood: Pool's time / 16)
            latch_gw(gw.stirge, gw.sucking, Ev::DrainsBlood, rules::fixes() ? 0xCF : 0);
            if (hasx(at, gw.harpy) && tg.up() && b.fx->charm && !tg.has(b.fx->charm) && !saving_throw(tg, 4, 0, d)) {
                give_aff(tg, b.fx->charm, 0, (at.team() ? 0x80 : 0) | 3, true);
                tg.rec[kTeam] = static_cast<uint8_t>(at.team());
                tg.target = -1;
                add_event(out, c, Ev::Charmed);
            }
            latch_gw(gw.squid, gw.constrict, Ev::Constricts, 0);
            if (hasx(at, m.poison)) poison(0);
            if (hasx(at, m.kreen_bite) && m.paralyzed && tg.up() && !saving_throw(tg, 0, 0, d)) {
                give_aff(tg, m.paralyzed, m.para_rounds ? m.para_rounds : d.roll(8, 2), 12, false);
                add_event(out, c, Ev::Paralyzed);
            }
            // The mummy (GAME.OVR 0x152A3): a save against poison or "is
            // diseased" - Cause Disease's handler at once: weakness (60, the
            // Strength in use - 1 "is weakened", at 3 helpless) and sickness
            // (10, a point of damage while over 1 HP, else helpless)
            const SpellFx& ill = b.fx->sp;
            if (hasx(at, gw.mummy) && tg.up() && ill.weak && ill.sick && !saving_throw(tg, 0, 0, d)) {
                add_event(out, c, Ev::Diseased);
                give_aff(tg, ill.weak, 60, 0, true);
                give_aff(tg, ill.sick, 10, 0, true);
                auto helpless_now = [&]() {
                    const uint8_t hl = b.fx->held[3];
                    if (hl && !tg.has(hl)) give_aff(tg, hl, 0, 0xFF, false);
                };
                if (tg.rec[0x11] > 3) {
                    --tg.rec[0x11];
                    add_event(out, c, Ev::Weakened);
                } else {
                    helpless_now();
                }
                if (tg.hp() > 1) {
                    damage(b, c, 1);
                    Event* e = add_event(out, c, Ev::Damage);
                    if (e) {
                        e->amount = 1;
                        e->kind = 8;
                    }
                } else {
                    helpless_now();
                }
            }
        }
        // Secret's: the -2 poison and the paralysis after attack 1's hits
        // too (its list after attack 1: 0x41 0x56 0x58 - GAME.OVR 0x36289)
        if (m.secret_ways) {
            if (hasx(at, m.poison_m2)) poison(-2);
            if (hasx(at, m.kreen_bite) && m.paralyzed && tg.up() && !saving_throw(tg, 0, 0, d)) {
                give_aff(tg, m.paralyzed, m.para_rounds ? m.para_rounds : d.roll(8, 2), 12, false);
                add_event(out, c, Ev::Paralyzed);
            }
        }
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
        // Pool's, attack 1: the stirge drinks (its victim held), the
        // mantis holds on (both: its effect's data the victim; the
        // stirge's time / 16 the blood it can take), the tiger rakes
        auto latch = [&](uint8_t holding, int minutes) {
            if (!holding || !m.held_fast || !tg.up()) return;
            give_aff(at, holding, minutes, c, true);
            give_aff(tg, m.held_fast, 0, 0xFF, false);
            tg.moves = 0;
        };
        if (hasx(at, m.blood)) latch(m.sucking, 0xCF);
        if (hasx(at, m.rake) && hits1 == 2 && tg.up()) {
            add_event(out, a, Ev::Rakes);
            out.rake = true;
        }
        if (hasx(at, m.bite_hold) && tg.up() && tg.rec[0x11] < 19) {     // (the Strength in use under 19)
            latch(m.holding, 0);
            add_event(out, c, Ev::HeldFast);
        }
    } else {
        if (hasx(at, m.poison) && !b.fx->gw.ways) poison(0);
        if (hasx(at, m.poison_p4)) poison(4);
        if (hasx(at, m.poison_p2)) poison(2);
        if (!m.dk_ways && hasx(at, m.kreen_bite) && m.paralyzed && tg.up() && !saving_throw(tg, 0, 0, d)) {
            // (Secret's: 10 rounds - GAME.OVR 0x10CDF)
            give_aff(tg, m.paralyzed, m.para_rounds ? m.para_rounds : d.roll(8, 2), 12, false);
            add_event(out, c, Ev::Paralyzed);
        }
        if (!m.dk_ways && hasx(at, m.poison_m2)) poison(-2);
        if (hasx(at, m.fire_touch)) touch(d.roll(10, 2), 9);
        pool_hit();
    }
    // Darkness's fire after a hit: a save against spells for half
    auto fire_hit = [&](int dice) {
        if (!tg.up()) return;
        Harm h;
        h.kind = 9;
        h.save = 4;
        h.on_save = 2;
        bool dn = false;
        const int done = harm(b, c, d.roll(6, dice) & 0xFF, h, d, &dn);
        g_kind = 0;
        Event* e = done > 0 ? add_event(out, c, Ev::Damage) : nullptr;
        if (e) {
            e->amount = static_cast<int16_t>(done);
            e->kind = 9;
        }
        if (dn) out.down = true;
    };
    if (hasx(at, m.fire2d6_hit)) fire_hit(2);
    if (slot == 1 && hasx(at, m.fire4d6_2)) fire_hit(4);
    if (slot == 1 && hasx(at, m.fire2d6_2)) fire_hit(2);
    // Secret's (secret_facts_2.md 15): the cockatrice's touch - a save
    // against petrification or stoned; the remorhaz swallows its victim
    // whole on a natural 20
    auto gone = [&](int status, Ev said) {
        add_event(out, c, said, a);             // (to: the one that did it)
        tg.rec[kHp] = 0;
        tg.rec[kHealth] = static_cast<uint8_t>(status);
        fall(b, c);
        out.down = true;
    };
    if (hasx(at, m.petrify_hit) && tg.up() && !saving_throw(tg, 1, 0, d)) gone(party::Stoned, Ev::Stoned);
    if (hasx(at, m.swallow) && tg.up() && roll == 20) gone(party::Dead, Ev::Swallowed);
}

} // namespace

namespace {
// Darkness's Kalistes (0x74, ovr 0x1D924 - the blow's damage): by its blow's
// number in this attack a weapon's die + 4 + its Strength's bonus - 1 Two-Handed
// Sword 1d10, 2 Bastard Sword 2d4, 3 Long Sword 1d8, 4 Scimitar 1d8, 5
// Dagger 1d4, 6 Spear 1d6 (each "+4"); past the sixth its own blow
int kalistes_blow(const Fighter& at, create::Dice& d, int dmg)
{
    static constexpr uint8_t kN[6] = {1, 2, 1, 1, 1, 1}, kSides[6] = {10, 4, 8, 8, 4, 6};
    const int k = at.blows - 1;
    if (k < 0 || k >= 6) return dmg;
    return d.roll(kSides[k], kN[k]) + 4 + rules::strength_damage(at.rec[0x125], at.rec[0x11], at.rec[0x1C]);
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
    b.risen = -1;
    b.no_action = b.round + 15;
    at.attacked = true;
    at.blows = 0;                               // (Darkness: counted from each attack - DS 0xAED7, zeroed at ovr 0x1FA62)
    at.swept = true;                            // (no sweep after any attack this round: the original zeroes its sweep count)
    // The original's order (the listing's sub_3F94D, called before the attack
    // itself, sub_3F9DB): the attack is counted first - attacks received + 1,
    // and the angle between the target's facing (before it turns) and where
    // the attacker stands added to its facing changes (free attacks on one
    // stepping away aren't counted). Then the target turns: to face the
    // attacker on its first attack this round; on later ones (a normal
    // attack) it turns about (+4) - the original does so for a target on the
    // screen, and the engine brings every target into view before an attack.
    const int dir = direction(at.x, at.y, tg.x, tg.y);
    if (!from_behind) {
        ++tg.received;
        int turn = dir < 8 ? ((((dir + 4) & 7) - tg.facing) & 7) : 0;
        if (turn > 4) turn = 8 - turn;
        tg.turns = (tg.turns + turn) & 7;
        if (tg.received < 2) {
            if (dir < 8) tg.facing = (dir + 4) & 7;
        } else {
            tg.facing = (tg.facing + 4) & 7;
        }
    }
    if (dir < 8) at.facing = dir;
    const bool stab = !from_behind && can_backstab(b, a, c, names);
    const bool behind = from_behind || stab || (tg.received > 1 && dir == tg.facing && tg.turns > 4);
    out.behind = behind && !stab;
    out.backstab = stab;
    int ac = tg.rec[behind ? kAcBehind : kAc];
    // Secret's Boots of Speed: AC 2 better against each attack (its phase
    // 0xB handler adds to the record's AC - SPEC 12)
    if (b.fx && b.fx->items.boots && hasx(tg, b.fx->items.boots)) ac += 2;
    // Coughing (a stinking cloud): its rear AC 2 worse (AC 10 at most worse;
    // worse than 10 becomes 10 - the original's), from any side
    const bool coughing = b.fx && hasx(tg, b.fx->cough);
    if (coughing) ac = tg.rec[kAcBehind] > 0x34 ? tg.rec[kAcBehind] - 2 : 0x32;
    if (stab) ac -= 4;
    // A missile weapon readied (its range over 1): beyond a third of its
    // range the target's AC is 2 better, beyond two thirds 5 better (the
    // distance through walls; coab's facts, combat_rules_facts 3.4)
    if (names && b.tables) {
        const int w = weapon(at, *names);
        const int reach = w >= 0 ? names->type(at.items[w][0x2E]).range : 0;
        if (reach > 1) {
            int dist = 0;
            range(b, *b.tables, a, c, true, &dist);
            const int third = (reach - 1) / 3;
            if (dist > third) {
                dist -= third;
                ac += 2;
            }
            if (dist > third) ac += 3;
        }
    }
    const int times = !stab ? 1 : b.fx && b.fx->backstab_plain ? at.rec[kThiefLevel] / 4 + 2 : (at.rec[kThiefLevel] - 1) / 4 + 2;
    // Large targets: the weapon's large dice
    const bool large = tg.rec[kSize] > 0x80 || (tg.rec[kSize] & 7) > 1;    // (0x80 itself isn't: the original's)
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
        // Secret's program never applies these at the roll (its handlers 0x116CE
        // invisibility and 0x1105F Dispel Evil run in phases before the d20 is
        // rolled or at the holder's own turn - the d20 they lower is rolled
        // after): AD&D's only with rules::fixes()
        const bool at_roll = !fx.secret || rules::fixes();
        if (at_roll && hasx(tg, fx.invisible) && !sees) side -= 4;              // an invisible target
        if (at_roll && hasx(tg, fx.sp.evil_ward) && (at.rec[0x14B] & 1)) side -= 7;   // Dispel Evil against the evil
        // The races: dwarves +1 against orcs, gnomes +1 against their foes;
        // dwarves and gnomes -4 to be hit by giants and trolls of size 2,
        // gnomes by kobold-kind (type 1) of size 2
        if (b.tables && b.tables->by_name) {
            // Pool's by name (its ovr13 handlers): the target a man-sized
            // humanoid (type 1, size 1) named as a foe; giants: a humanoid
            // of size 2 attacking - the name compared that of the one the
            // defender fights (its target: the original's); bugbears and
            // gnolls (type 1) any size
            const Tables& tt = *b.tables;
            auto named = [&](const Rec& r, int from, int n) {
                uint8_t rec[16];
                r.read(rec, 0, 16);
                for (int k = from; k < from + n; ++k)
                    if (tt.foe_names[k][0] && tt.foe_names[k][0] < 16 && !memcmp(rec, tt.foe_names[k], tt.foe_names[k][0] + 1))
                        return true;
                return false;
            };
            if (hasx(at, fx.race.dwarf_orc) && tg.rec[0x11A] == 1 && tg.rec[kSize] == 1 && named(tg.rec, 4, 8)) ++side;
            if (hasx(at, fx.race.gnome_foe) && tg.rec[0x11A] == 1 && tg.rec[kSize] == 1 && named(tg.rec, 0, 4)) ++side;
            if (hasx(tg, fx.race.giants) && at.rec[0x11A] == 1 && (at.rec[kSize] & 0x7F) == 2 && tg.target >= 0 &&
                tg.target < b.n && named(b.f[tg.target].rec, 12, 8))
                side -= 4;
            if (hasx(tg, fx.race.gnome_extra) && at.rec[0x11A] == 1) {
                uint8_t at_name[16];
                at.rec.read(at_name, 0, 16);
                for (const uint8_t* nm : tt.dodge_names)
                    if (nm[0] && nm[0] < 16 && !memcmp(at_name, nm, nm[0] + 1)) side -= 4;
            }
        } else if (fx.race.secret_flags) {
            // Secret's (GAME.OVR 0x11703 / 0x110A1, the attacker's phase):
            // dwarves +1 against flags 1 & 0x80, gnomes against flags 2 & 4
            if (hasx(at, fx.race.dwarf_orc) && (tg.rec[0x14B] & 0x80)) ++side;
            if (hasx(at, fx.race.gnome_foe) && (tg.flags2 & 4)) ++side;
            // Its dwarves' / gnomes' -4 against flags 1 & 4 / & 0x20 (handlers
            // 0x12269 / 0x1228E) come before the roll (phase 0xB, 0x16259) and
            // are lost: only with the rules fixes
            if (rules::fixes()) {
                if (hasx(tg, fx.race.giants) && (at.rec[0x14B] & 4)) side -= 4;
                if (hasx(tg, fx.race.gnome_extra) && (at.rec[0x14B] & 0x20)) side -= 4;
            }
        } else {
            if (hasx(at, fx.race.dwarf_orc) && (tg.rec[0x14B] & 4)) ++side;
            if (hasx(at, fx.race.gnome_foe) && (tg.rec[0x14B] & 2)) ++side;
            if ((at.rec[0xDE] & 0x7F) == 2) {
                if (hasx(tg, fx.race.giants) && (at.rec[0x11A] == 2 || at.rec[0x11A] == 10)) side -= 4;
                if (hasx(tg, fx.race.gnome_extra) && at.rec[0x11A] == 1) side -= 4;
            }
        }
        if (fx.blinded && tg.has(fx.blinded) && !coughing) ac -= 4;     // a blind target: easier (coughing: lost)
        // Darkness (R6, R14): a dancing target's AC 4 worse (0x76, ovr 0x1DB21);
        // a terrified attacker -2 to hit (0x78, ovr 0x1DBDF)
        if (fx.dk.dancing && tg.has(fx.dk.dancing)) ac -= 4;
        if (fx.dk.feared && at.has(fx.dk.feared)) side -= 2;
        if (fx.sp.shield && tg.has(fx.sp.shield) && ac < 0x39) ac = 0x39;   // Shield: AC 3 at worst
        // Faerie Fire: the stored AC + 2 (to AC 0 at most) - the original's
        // own rule, which makes the target harder to hit
        if (fx.faerie && tg.has(fx.faerie)) {
            // (Secret's handler, GAME.OVR 0x130DA: the target's AC 2 worse - easier to hit)
            if (fx.secret) ac -= 2;
            else ac = ac < 58 ? ac + 2 : 60;
        }
        // (protection from evil / good: the original's -2 to hit comes before
        // the roll and is lost; only the saves count)
        // Attacking makes them seen
        const int inv = find_aff(at, fx.invisible);
        if (inv >= 0) drop_aff(at, inv);
        // A helpless target: one cruel blow
        if (helpless(b, tg)) {
            out.slain = true;
            if (at.attacks[1] <= 0 && at.attacks[0] > 0) out.shots = 1;     // (the blow counts slot 1's)
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
    // The attacker's magic weapon against this kind of creature (its effect
    // while readied; the target's monster type 0x11A): Flame Tongue - trolls
    // +1, types 9 / 12 +2, the animated dead +3; Frost Brand - fire creatures
    // +3; Dragon Slayer - dragons +2 to hit, its own damage (coab's facts,
    // behaviour_facts.md items). To the roll (not a 1) and the damage.
    int wbonus = 0;
    bool slayer = false, flame = false, giant = false;
    if (b.fx) {
        const ItemFx& ix = b.fx->items;
        const int kind = tg.rec[0x11A];
        // (the Flame Tongue's blows are fire and magic, 9 - both games' handler
        // sets the damage kind: a troll it fells stays down)
        flame = ix.flame_tongue && at.has(ix.flame_tongue);
        if (flame) wbonus += kind == 10 ? 1 : (kind == 9 || kind == 12) ? 2 : kind == 4 ? 3 : 0;
        if (ix.frost_brand && at.has(ix.frost_brand) && kind == 8) wbonus += 3;
        if (ix.dragon_slayer && at.has(ix.dragon_slayer) && kind == 3) slayer = true;
        if (ix.giant_slayer && at.has(ix.giant_slayer) && (tg.flags2 & 1)) giant = true;
        if (ix.vs_undead && at.has(ix.vs_undead) && kind == 4) wbonus += 2;
    }
    for (int slot = 1; slot >= 0; --slot) {
        while (at.attacks[slot] > 0 && tg.up() && out.n < 8) {
            --at.attacks[slot];
            if (slot == 0) ++out.shots;
            if (at.blows < 255) ++at.blows;
            const int roll = d.roll(20, 1);
            const int extra = roll > 1 ? wbonus + (slayer ? 2 : 0) + (giant ? 1 : 0) : 0;
            bool hit = roll == 20 || (roll != 1 && roll + static_cast<int8_t>(at.rec[kHit]) + side + extra >= ac);
            // Blinking, once it has acted this round: not there to be hit
            // (Secret's 0x11C9D runs only at the blinker's own turn and for being
            // seen - never at the roll: AD&D's with rules::fixes())
            if (b.fx && b.fx->blink && tg.has(b.fx->blink) && tg.delay == 0 && (!b.fx->secret || rules::fixes())) hit = false;
            // Displacement: the first attack (past a 1) on it this fight misses, even a 20
            if (b.fx && b.fx->items.displace && roll > 1 && !(b.fx->items.displace_unused && !rules::fixes())) {
                const int k = find_aff(tg, b.fx->items.displace);
                if (k >= 0 && !(tg.aff[k][3] & 0x10)) {
                    tg.aff[k][3] |= 0x10;
                    hit = false;
                }
            }
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
            if (b.fx && b.fx->dk.kalistes && hasx(at, b.fx->dk.kalistes)) {
                dmg = kalistes_blow(at, d, dmg);
                if (at.blows >= 1 && at.blows <= 6) h.weapon = at.blows;
            }
            // (a terrified attacker: -2 damage, not under 0 - R14)
            if (b.fx && b.fx->dk.feared && at.has(b.fx->dk.feared)) dmg -= 2;
            if (dmg < 0) dmg = 0;
            dmg *= times;
            // The magic weapon's own damage (a dragon slayer's: 3 x d12 + 4 + Strength's)
            dmg += wbonus;
            if (slayer) dmg = d.roll(12, 1) * 3 + 4 + rules::strength_damage(at.rec[0x125], at.rec[0x11], at.rec[0x1C]);
            // Secret's giant slaying: 6d6 + Strength's (GAME.OVR 0x13162)
            if (giant) dmg = d.roll(6, 6) + rules::strength_damage(at.rec[0x125], at.rec[0x11], at.rec[0x1C]);
            // A ranger against giants: their ranger level more
            // (Secret's only next to it: GAME.OVR 0x1427D, the distance at most 1)
            if (b.fx && hasx(at, b.fx->race.ranger_giant) && (tg.rec[0x14B] & 8) &&
                (!b.fx->race.secret_flags || adjacent(b, a, c)))
                dmg += at.rec[0x10D];
            bool bane = false, bolt_bane = false;
            if (b.fx) {
                const MonFx& m = b.fx->mon;
                // Ray of Enfeeblement on the attacker: a quarter off (coab's
                // facts: three_quarters_damage, the attacker's checks first)
                if (b.fx->sp.enfeeble && at.has(b.fx->sp.enfeeble)) dmg -= dmg / 4;
                // The salamander's heat
                if (hasx(at, m.heat) && !hasx(tg, m.heat_proof[0]) && !hasx(tg, m.heat_proof[1]) &&
                    !hasx(tg, m.heat_proof[2]))
                    dmg += d.roll(6, 1);
                const Facts::GateFx& gw = b.fx->gw;
                // Gateway's (0x98, GAME.OVR 0x14FD8 - the attacker's list): half on
                // one whose armour has the Glacier's name word
                if (hasx(at, gw.glacier) && names) {
                    const int ar = items::readied_in(tg.items, tg.n_items, *names, items::kSlotArmour);
                    if (ar >= 0 && has_word(tg.items[ar], gw.glacier_word)) dmg /= 2;
                }
                // Secret: Mirror Image takes weapon blows too (its handler 0x11751 is
                // in the blow's damage phase 5, GAME.OVR 0x15098) - d(images + 1) over 1
                // (Gateway's too: 0x123FD)
                if ((b.fx->secret || gw.mirror) && b.fx->mirror) {
                    const int mi = find_aff(tg, b.fx->mirror);
                    if (mi >= 0 && d.roll((tg.aff[mi][3] >> 4) + 1, 1) > 1) {
                        lose_image(b, tg, mi);
                        out.image[out.n] = true;
                        h.hit = false;
                        out.hits[out.n++] = h;
                        continue;
                    }
                }
                // The target's defences against a blow: missiles dodged
                const bool far = !adjacent(b, a, c);
                bool dodged = false;
                if (armed && far && hasx(tg, m.missile_proof) && item && plus == 0) dodged = true;
                if (!dodged && armed && far && hasx(tg, m.missile_dodge) && d.roll(100, 1) <= 60) dodged = true;
                // Gateway's giants (0x78): boulders dodged half the time
                if (!dodged && armed && far && hasx(tg, gw.boulder_dodge) && item &&
                    (item[0x2E] == gw.boulder[0] || item[0x2E] == gw.boulder[1]) && d.roll(100, 1) <= 50)
                    dodged = true;
                if (dodged) {
                    out.avoided[out.n] = true;
                    h.hit = false;
                    out.hits[out.n++] = h;
                    continue;
                }
                troll_hurt(tg, m);
                // Gateway's skeletons (0x73, GAME.OVR 0x144ED): half from a weapon
                // whose type's byte 7 is 0 or 1
                if (hasx(tg, gw.half_edged) && item && names && names->type(item[0x2E]).blow <= 1) dmg /= 2;
                if (hasx(tg, m.need_magic) && (!item || plus == 0) && (at.rec[0x74] > 0 || at.rec[kHd] < 4)) dmg = 0;
                // Gateway's undead (0x75, GAME.OVR 0x1459C): holy water 1d6 + 1
                const bool holy = gw.holy_water && item && item[0x2E] == gw.holy_water;
                if (hasx(tg, gw.holy) && holy) dmg = d.roll(6, 1) + 1;
                // (Gateway's 0x3C, GAME.OVR 0x13127: holy water gets through)
                if (hasx(tg, m.stop_normal) && !holy) {
                    if (!item || plus <= 0) dmg = 0;
                    else if (plus <= 2) dmg /= 2;
                }
                if (hasx(tg, m.half)) dmg /= 2;
                // Secret's iron golem: a +3 weapon or better, else nothing (even a monster's blow);
                // Darkness's: +1 / +2 / +3, a monster's bare blow from (N - 1) x 2 + 5 Hit Dice
                auto plus_ok = [&](int need) {
                    if (item) return plus >= need;
                    return m.plus_hd && at.rec[0x74] == 0 && at.rec[kHd] >= (need - 1) * 2 + 5;
                };
                if (hasx(tg, m.need_plus3) && !plus_ok(3)) dmg = 0;
                if (hasx(tg, m.need_plus2) && !plus_ok(2)) dmg = 0;
                if (hasx(tg, m.need_plus1) && !plus_ok(1)) dmg = 0;
                if (hasx(tg, m.half_under3) && item && plus < 3) dmg /= 2;
                if (hasx(tg, m.half_under4) && item && plus < 4) dmg /= 2;
                if (hasx(tg, m.half_pierce) && item && names && names->type(item[0x2E]).blow <= 1) dmg /= 2;
                if (hasx(tg, m.half_crush) && item && names && names->type(item[0x2E]).blow > 0x7F) dmg /= 2;
                // Secret's giant slug: a blunt weapon's blow (its type's byte 7 & 0x80) nothing
                if (hasx(tg, m.blunt_proof) && item && names && (names->type(item[0x2E]).blow & 0x80)) dmg = 0;
                if (hasx(tg, m.pierce_one) && item && names && names->type(item[0x2E]).blow == 1) dmg = 1;
                // Gateway's (0x97 / 0x99, GAME.OVR 0x14F82 / 0x15052 - the target's
                // list's last): twice from a weapon with that name word (a byte)
                if (hasx(tg, gw.ice) && item && has_word(item, gw.ice_word)) dmg = (dmg * 2) & 0xFF;
                if (hasx(tg, gw.stone) && item && has_word(item, gw.stone_word)) dmg = (dmg * 2) & 0xFF;
                // Pool's undead: only magic (any plus) or silver hurts the
                // wight; the wraith takes half from +0 silver, nothing from
                // other +0 weapons or none; a flask of oil burns the mummy
                // (the blow's item, as blow_item: the arrows from a bow)
                const bool silver = item && m.silver_word && item[0x31] == m.silver_word;
                if (hasx(tg, m.silver_only) && !(item && (plus != 0 || silver))) dmg = 0;
                if (hasx(tg, m.silver_half) && (!item || plus == 0)) dmg = silver ? dmg / 2 : 0;
                if (hasx(tg, m.oil_bane) && item && m.oil_flask && item[0x2E] == m.oil_flask) dmg = d.roll(8, 3);
                // (its handler: fire / magic blows a point more a die - the Flame Tongue's)
                else if (hasx(tg, m.oil_bane) && flame) dmg += n;
                // A blessed quarrel slays it outright
                if (hasx(tg, m.blessed_bane) && item && item[0x2E] == b.quarrel && m.blessed_word &&
                    item[0x31] == m.blessed_word)
                    bane = true;
                // Darkness's (0x52, ovr 0x1C994): a blessed bolt (its item type) slays it
                if (b.fx->dk.blessed && hasx(tg, b.fx->dk.blessed) && item && item[0x2E] == b.fx->dk.blessed_type)
                    bolt_bane = true;
            }
            h.damage = dmg;
            out.any = true;
            if (slot == 0) ++hits1;
            // A flask of oil burns (Tom, 2026-10-12: "the flasks of oil should
            // do fire damage" - the originals made it a plain 2d6 blow): its
            // blow is fire, so a troll it brings down stays down
            const bool oil = item && names && names->flask() && item[0x2E] == names->flask();
            const int blow_kind = oil ? 1 : flame ? 9 : 0;
            if (blow_kind) g_kind = blow_kind;
            if (bane) {
                tg.rec[kHp] = 0;
                tg.rec[kHealth] = party::Gone;
                fall(b, c);
                out.down = true;
            } else if (bolt_bane) {
                tg.rec[kHp] = 0;
                tg.rec[kHealth] = party::Dead;
                fall(b, c);
            } else if (damage(b, c, dmg)) {
                out.down = true;
                // (Gateway's boar back up at once)
                if (b.risen == c) {
                    add_event(out, c, tg.team() ? Ev::StandsUp : Ev::GetsUp);
                    b.risen = -1;
                }
            }
            if (blow_kind) g_kind = 0;
            out.hits[out.n++] = h;
            if (bolt_bane) add_event(out, c, Ev::SlainBolt);
            // Darkness's vorpal blow (0x73, ovr 0x1D8C0 - after attack 1's hit):
            // a d20 of 17 or more beheads it, but not one with flags 2 & 0x20
            if (b.fx && slot == 0 && b.fx->dk.vorpal && hasx(at, b.fx->dk.vorpal) && tg.up() && d.roll(20, 1) >= 17 &&
                !(tg.flags2 & 0x20)) {
                tg.rec[kHp] = 0;
                tg.rec[kHealth] = party::Dead;
                fall(b, c);
                add_event(out, c, Ev::Beheaded);
            }
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
            if (out.rake) {
                // Pool's tiger (its ovr14 routine): both claws hit - two
                // rakes of 2d4 at once (the attack made again with them)
                out.rake = false;
                const int keep[2] = {at.attacks[0], at.attacks[1]};
                const uint8_t keep_dice = at.rec[kDice + 1], keep_sides = at.rec[kSides + 1];
                at.attacks[0] = 0;
                at.attacks[1] = 2;
                at.rec[kDice + 1] = 2;
                at.rec[kSides + 1] = 4;
                const Attack more = attack(b, a, c, names, d, from_behind);
                at.attacks[0] = keep[0];
                at.attacks[1] = keep[1];
                at.rec[kDice + 1] = keep_dice;
                at.rec[kSides + 1] = keep_sides;
                const int base = out.n;
                for (int k = 0; k < more.n && out.n < 8; ++k) {
                    out.avoided[out.n] = more.avoided[k];
                    out.hits[out.n++] = more.hits[k];
                }
                for (int k = 0; k < more.n_ev && out.n_ev < 8; ++k) {
                    out.ev[out.n_ev] = more.ev[k];
                    if (out.ev[out.n_ev].hit >= 0) out.ev[out.n_ev].hit = static_cast<int8_t>(out.ev[out.n_ev].hit + base);
                    ++out.n_ev;
                }
                out.any = out.any || more.any;
                out.down = out.down || more.down;
            }
        }
    }
    return out;
}

namespace {
// The class a drained level comes off (the original's pick, its ovr13
// routine): the highest level, of those the one whose level needed the
// most experience (a later class on a tie); that experience in *exp
int drain_class(const Battle& b, const Rec& r, int32_t* exp)
{
    int best = 0, best_l = 0;
    int32_t best_e = 0;
    for (int k = 0; k < 8; ++k) {
        const int l = r[kLevels + k];
        if (l < best_l) continue;
        const int32_t e = l > 1 && l < 13 ? b.level_exp[k][l] : 0;
        if (e >= best_e || e == -1) {
            best = k;
            best_l = l;
            best_e = e;
        }
    }
    *exp = best_e;
    return best;
}
} // namespace

int (*g_dk_drain)(Battle& b, int c, int n) = nullptr;

void drain(Battle& b, int c, int n, Event* out, int cap, int* n_out, int hit)
{
    Fighter& f = b.f[c];
    const Rec r = f.rec;
    auto said = [&](Ev e, int amount) {
        if (!out || *n_out >= cap) return;
        Event& x = out[(*n_out)++];
        x = Event{};
        x.who = static_cast<uint8_t>(c);
        x.ev = e;
        x.hit = static_cast<int8_t>(hit);
        x.amount = static_cast<int16_t>(amount);
    };
    // Out (said whatever: the original's - the dead, stoned and gone stay so)
    auto put_down = [&](int status, Ev e) {
        said(e, 0);
        const int st = f.status();
        if (st == party::Stoned || st == party::Dead || st == party::Gone) return;
        r[kHealth] = static_cast<uint8_t>(status);
        r[kHp] = 0;
        fall(b, c);
    };
    if (g_dk_drain) {
        said(Ev::DkDrained, n);
        const int res = g_dk_drain(b, c, n);
        if (res == 2) put_down(party::Gone, Ev::DkDrainDies);
        else if (res == 1) put_down(party::Unconscious, Ev::DkDrainCollapses);
        return;
    }
    said(Ev::Drained, n);
    if (f.status() == party::Animated) {
        put_down(party::Gone, Ev::DeadForever);
        r[kHpMax] = 0;
        return;
    }
    for (int k = 0; k < n; ++k) {
        if (f.status() == party::Gone) continue;
        ++r[kDrained];
        // A level's share of the hit points
        int sum = 0;
        for (int j = 0; j < 8; ++j) sum += r[kLevels + j];
        const int per = sum ? r[kHpMax] / sum : 0;
        for (int j = 0; j < per; ++j) {
            if (r[kHpMax]) --r[kHpMax];
            if (r[kHpRolled]) --r[kHpRolled];
            if (r[kHp]) --r[kHp];
            ++r[kDrainedHp];
        }
        int32_t e = 0;
        if (r[kHd] < 2) {
            put_down(party::Gone, Ev::DeadForever);
            r[kHpMax] = 0;
            const int cls = drain_class(b, r, &e);
            if (r[kLevels + cls] > 0 && n == 2) r[kLevels + cls] = 0;
        } else {
            const int cls = drain_class(b, r, &e);
            --r[kLevels + cls];
            if (r[kHp] == 0 && f.status() != party::Dead) put_down(party::Dead, Ev::Killed);
        }
        const int cls = drain_class(b, r, &e);
        r[kHd] = r[kLevels + cls];
        for (int j = 0; j < 4; ++j) r[kExp + j] = static_cast<uint8_t>(static_cast<uint32_t>(e) >> (8 * j));
    }
}

int turn_holds(Battle& b, int i, create::Dice& d, Event* out, int cap, int* bite, bool* go_on)
{
    *bite = -1;
    if (go_on) *go_on = false;
    if (!b.fx || i < 0 || i >= b.n) return 0;
    Fighter& f = b.f[i];
    const MonFx& m = b.fx->mon;
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
    // The stirge: blood while both are in the fight and it isn't full
    // (Gateway's with rules::fixes(): its 0x96 never drinks in the original)
    const Facts::GateFx& gw = b.fx->gw;
    const bool gw_fix = rules::fixes() && gw.ways;
    const uint8_t suck = m.sucking ? m.sucking : gw_fix ? gw.sucking : 0;
    const int s = suck ? find_aff(f, suck) : -1;
    if (s >= 0) {
        const int v = f.aff[s][3];
        if (!f.up() || v >= b.n || !b.f[v].up()) {
            let_go(b, f, suck);
        } else {
            ev(i, m.sucking ? Ev::SucksBlood : Ev::DrainsBlood, v);
            set_actor(b, i);
            const int amount = d.roll(4, 1);
            const bool dn = damage(b, v, amount);
            Event* e = ev(v, Ev::Damage);
            if (e) e->amount = static_cast<int16_t>(amount);
            if (dn) ev(v, Ev::Down);
            const int k = find_aff(f, suck);
            if (k >= 0 && b.f[v].up()) {
                const int t = f.aff[k][1] | f.aff[k][2] << 8;
                if ((t >> 4) > amount) {
                    const int left = t - (amount << 4);
                    f.aff[k][1] = static_cast<uint8_t>(left);
                    f.aff[k][2] = static_cast<uint8_t>(left >> 8);
                } else {
                    let_go(b, f, suck);
                }
            } else if (k >= 0) {
                let_go(b, f, suck);
            }
        }
    }
    // Gateway's squid with rules::fixes(): it squeezes the one it holds - two
    // of its handler's 1d2 (GAME.OVR 0x1ABA3, never reached in the original);
    // its turn goes on
    const int q = gw_fix && gw.constrict ? find_aff(f, gw.constrict) : -1;
    if (q >= 0) {
        const int v = f.aff[q][3];
        if (!f.up() || v >= b.n || !b.f[v].up()) {
            let_go(b, f, gw.constrict);
        } else {
            ev(i, Ev::Constricts, v);
            set_actor(b, i);
            const int amount = d.roll(2, 2);
            const bool dn = damage(b, v, amount);
            Event* e = ev(v, Ev::Damage);
            if (e) e->amount = static_cast<int16_t>(amount);
            if (dn) ev(v, Ev::Down);
            if (!b.f[v].up()) let_go(b, f, gw.constrict);
            if (go_on) *go_on = true;
        }
    }
    // The mantis: its bite on the one it holds
    const int h = m.holding ? find_aff(f, m.holding) : -1;
    if (h >= 0) {
        const int v = f.aff[h][3];
        if (!f.up() || v >= b.n || !b.f[v].up()) let_go(b, f, m.holding);
        else *bite = v;
    }
    return n;
}

Attack bite_attack(Battle& b, int a, int c, const items::Names* names, create::Dice& d)
{
    Fighter& at = b.f[a];
    const int keep[2] = {at.attacks[0], at.attacks[1]};
    const uint8_t keep_dice = at.rec[kDice], keep_sides = at.rec[kSides];
    at.attacks[0] = 1;
    at.attacks[1] = 0;
    at.rec[kDice] = 3;
    at.rec[kSides] = 4;
    Attack out = attack(b, a, c, names, d, false);
    at.attacks[0] = keep[0];
    at.attacks[1] = keep[1];
    at.rec[kDice] = keep_dice;
    at.rec[kSides] = keep_sides;
    if (b.fx && !b.f[c].up()) let_go(b, at, b.fx->mon.holding);
    return out;
}

bool resists(Battle& b, int c, int effect, const Harm& h, create::Dice& d)
{
    if (!b.fx || c < 0 || c >= b.n) return false;
    const Fighter& f = b.f[c];
    const Facts& fx = *b.fx;
    const MonFx& m = fx.mon;
    const bool magic = effect != 0 || (h.kind & 8);
    // Gateway: Mirror Image first in its magic resistance list (0x1C, GAME.OVR
    // 0x123FD) - a hostile spell at picked creatures may take an image
    // ("lost an image"; the spell's effect cancelled)
    if (fx.gw.mirror && fx.mirror && h.spell > 0 && h.single && h.hostile) {
        const int mi = find_aff(f, fx.mirror);
        if (mi >= 0 && d.roll((f.aff[mi][3] >> 4) + 1, 1) > 1) {
            lose_image(b, b.f[c], mi);
            b.lost_image = c;
            return true;
        }
    }
    // Magic resistance by the caster's level (byte sums: a high level wraps
    // round to always resisted, as in the original)
    auto chance = [&](uint8_t type, int base) {
        if (!magic || !hasx(f, type)) return false;
        const int v = (m.mr_low ? base - (11 - h.level) * 5 : base + (11 - h.level) * 5) & 0xFF;
        return d.roll(100, 1) <= v;
    };
    if (chance(m.mr50, 50) || chance(m.mr15, 15)) return true;
    // Secret's: the record's own magic resistance (GAME.OVR 0x35FCA)
    if (magic && f.magic_res && d.roll(100, 1) <= ((f.magic_res + (11 - h.level) * 5) & 0xFF)) return true;
    const bool sleep_charm = effect > 0 && ((m.sleep && effect == m.sleep) || (fx.charm && effect == fx.charm));
    const bool paralysis = effect > 0 && m.paralyzed && effect == m.paralyzed;
    if (hasx(f, m.elf_sleep) && sleep_charm && d.roll(100, 1) <= 90) return true;
    if (hasx(f, m.charm_sleep) && sleep_charm) return true;
    // Secret's Periapt of Health: immune to disease (AD&D's; its program's
    // handler does nothing - with rules::fixes() only)
    if (rules::fixes() && fx.items.no_disease && hasx(f, fx.items.no_disease) && effect > 0 &&
        (effect == fx.sp.ill || effect == fx.sp.weak || effect == fx.sp.sick))
        return true;
    // Darkness's 0x4C (GAME.OVR 0x1C661, phase 9): the disease does nothing (R14)
    if (fx.dk.no_disease && hasx(f, fx.dk.no_disease) && effect > 0 && effect == fx.dk.disease) return true;
    if (hasx(f, m.no_paralyze) && paralysis) return true;
    if (hasx(f, m.no_cold) && (h.kind & 2)) return true;
    if (hasx(f, m.no_fire) && (h.kind & 1)) return true;
    if (hasx(f, m.mind) && (sleep_charm || paralysis || (effect > 0 && m.poisoned && effect == m.poisoned))) return true;
    // Secret's: immune to fear; to poison and paralysis
    if (hasx(f, m.no_fear) && effect > 0 && fx.fear && effect == fx.fear) return true;
    if (hasx(f, m.no_poison) && (paralysis || (effect > 0 && m.poisoned && effect == m.poisoned))) return true;
    if (hasx(f, fx.race.halfelf) && sleep_charm && d.roll(100, 1) <= 30) return true;      // half-elves: 30%
    if (hasx(f, m.globe) && h.spell > 0 && h.spell_level < 4) return true;
    if (hasx(f, m.ward8) && h.spell > 0 && h.spell_level < 8) return true;
    // Secret's Globe of Invulnerability (0x39, GAME.OVR 0x123D3): up to the 4th level
    if (hasx(f, fx.globe_major) && h.spell > 0 && h.spell_level <= 4) return true;
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
    b.risen = -1;
    if (b.fx && f.up() && dmg > 0) {
        const MonFx& m = b.fx->mon;
        bool safe = false;
        // Mirror Image: a spell at picked creatures may take an image instead
        // (d(images + 1) over 1) - the effect's whole data byte counts down,
        // gone at 0 (the original's; weapons never meet the images)
        const int mi = b.fx->mirror && h.spell > 0 && h.single && (!b.fx->gw.mirror || h.hostile) ? find_aff(f, b.fx->mirror) : -1;
        const int images = mi < 0 ? 0 : b.fx->mirror_plain ? f.aff[mi][3] : f.aff[mi][3] >> 4;
        if (mi >= 0 && d.roll(images + 1, 1) > 1) {
            b.lost_image = c;
            lose_image(b, f, mi);
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
        // Secret's ring of fire resistance: fire without magic does nothing,
        // magic fire 2 a die less, at least 1 a die
        const uint8_t ring = b.fx->items.fire_ring;
        if (ring && hasx(f, ring) && (h.kind & 1)) {
            if (!(h.kind & 8)) safe = true;
            else {
                const int n = h.dice > 0 ? h.dice : 1;
                dmg -= 2 * n;
                if (dmg < n) dmg = n;
            }
        }
        if (hasx(f, m.oil_bane) && (h.kind & 9)) dmg += h.dice;    // (Pool's mummy: a point more a die)
        auto chance = [&](uint8_t type, int base) {
            if (!(h.kind & 8) || !hasx(f, type)) return false;
            // (Pool's code takes the level's part away: weak casters get through more easily)
            const int odds = m.mr_low ? base - (11 - h.level) * 5 : base + (11 - h.level) * 5;
            return d.roll(100, 1) <= (odds & 0xFF);
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
        // Secret's iron golem (GAME.OVR 0x13354): fire and magic heals it by
        // the damage (to its maximum), electricity slows it 3 rounds; no
        // damage of any kind
        if (hasx(f, m.golem)) {
            if ((h.kind & 1) && (h.kind & 8)) {
                const int hp = f.hp() + dmg;
                f.rec[kHp] = static_cast<uint8_t>(hp > f.hp_max() ? f.hp_max() : hp);
            } else if ((h.kind & 4) && b.fx->slow) {
                give_aff(f, b.fx->slow, 3, 0xFF, false);
            }
            safe = true;
        }
        // Secret's dragons: no damage from breath (GAME.OVR 0x1310F)
        if (hasx(f, m.no_breath) && (h.kind & 0x20)) safe = true;
        if (hasx(f, m.no_magic) && (h.kind & 8)) safe = true;
        if (hasx(f, m.death_ward) && (h.kind & 0x40)) safe = true;
        if (hasx(f, m.no_elec) && (h.kind & 4)) safe = true;
        if (hasx(f, m.globe) && h.spell > 0 && h.spell_level < 4) safe = true;
        if (hasx(f, m.ward8) && h.spell > 0 && h.spell_level < 8) safe = true;
        if (b.fx && hasx(f, b.fx->globe_major) && h.spell > 0 && h.spell_level <= 4) safe = true;
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
    // (man-sized or smaller: the size byte without its top bit 1 or less - the original's test)
    if (!at.rec[kThiefLevel] || tg.received < 2 || (tg.rec[kSize] & 0x7F) > 1) return false;
    if (direction(at.x, at.y, tg.x, tg.y) != tg.facing) return false;
    if (names && b.fx) {
        const int w = weapon(at, *names);
        if (w >= 0) {
            bool ok = false;
            for (uint8_t t : b.fx->backstab_weapons)
                if (t && at.items[w][0x2E] == t) ok = true;
            if (!ok) return false;
        }
        // (Pool: no body armour but leather)
        if (b.fx->backstab_armour) {
            const int ar = items::readied_in(at.items, at.n_items, *names, items::kSlotArmour);
            if (ar >= 0 && at.items[ar][0x2E] != b.fx->backstab_armour) return false;
        }
    }
    return true;
}

bool free_attack_ok(const Battle& b, const Tables& t, int e, int mover)
{
    const Fighter& en = b.f[e];
    const Fighter& m = b.f[mover];
    if (helpless(b, en) || !range(b, t, e, mover, false, nullptr)) return false;
    // (one with the Dragon Slayer's or the Robe of Vermin's effect never - the
    // original's; Pool's code asks the same of its stirge drinking and its
    // mantis holding on: those never)
    if (b.fx && (hasx(en, b.fx->items.dragon_slayer) || hasx(en, b.fx->items.vermin) ||
                 hasx(en, b.fx->mon.sucking) || hasx(en, b.fx->mon.holding)))
        return false;
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
    // The sweep count: the attack level (record 0xDD - the fighter level of
    // a character or a monster of a race, else 1; coab reclac_player_values)
    const int level = at.rec[0xDD];
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
    // (the last readied one: the original's recalculation puts each readied
    // item in its slot in turn - coab reclac_player_values, rules::recalc -
    // so a monster's record with two readied, Gateway's gnolls' long sword
    // and short bow, fights with the bow)
    int w = -1;
    for (int i = 0; f.items && i < f.n_items; ++i)
        if (f.items[i][0x34] && names.type(f.items[i][0x2E]).slot == items::kSlotWeapon) w = i;
    return w;
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

int money_exp(const int m[7], int plat_xp)
{
    // Each kind on its own, the divisions rounded down one by one (the
    // listing's calc_battle_exp, ovr006:0248-02D9: the pool's copper / 200,
    // silver / 20, electrum / 2, gold, platinum x 5, gems x 250, jewellery x 2200)
    return static_cast<int>(m[0] / 200L + m[1] / 20L + m[2] / 2L + m[3] + static_cast<long>(plat_xp) * m[4] + 250L * m[5] + 2200L * m[6]);
}

int award(Battle& b, int total, bool give)
{
    // The share: among those still in the fight and not animated dead (the
    // original's calc_battle_exp divides by the party less its count kept in
    // sub_2D556, which counts the members out of the fight (in_combat 0) OR
    // animated - the listing at ovr006:06D5-06E8, before the fallen are put
    // back on their feet); those out of the fight get none
    int members = 0;
    for (int i = 0; i < b.party_size; ++i)
        if (b.f[i].up() && b.f[i].status() != party::Animated) ++members;
    if (!members) return 0;
    const int share = total / members;
    for (int i = 0; give && i < b.party_size; ++i) {
        Fighter& f = b.f[i];
        if (!f.up() || f.status() == party::Animated) continue;
        const Rec r = f.rec;
        int add = share;
        const int str = r[0x11], in = r[0x13], wis = r[0x15], dex = r[0x17];
        int classes = 0;
        for (int k = 0; k < 8; ++k)
            if (r[0x109 + k]) ++classes;
        switch (r[0x75]) {
        case 0: if (wis >= (b.fx && b.fx->cleric_xp_wis && !rules::fixes() ? b.fx->cleric_xp_wis : 16)) add += share / 10; break;
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
        if (b.ground[ny][nx] == cloud_g(b) && !saving_throw(f, 0, 0, d)) continue;   // a cloud: only after a test save
        if (b.ground[ny][nx] == poison_g(b) && f.rec[0xE5] < 7) continue;            // poison: never (below 7 Hit Dice)
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
    const int r = d.roll(20, 1);
    if (r == 1) return false;
    if (r == 20) return true;
    // Immune to poison, paralysis and death magic: every such save made
    // (the effect works on the roll after the natural 1 / 20: a 1 still fails)
    if (g_fx && type == 0 && hasx(f, g_fx->mon.mind)) return true;
    if (g_fx && type == 0 && hasx(f, g_fx->mon.no_poison)) return true;     // (Secret's dreadlord)
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
        // (Secret's program never applies it - 0x61 is in no effect phase;
        // only with the rules fixes there)
        if ((type == 4 || type == 2) && hasx(f, fx.race.con_save) && (!fx.race.secret_flags || rules::fixes())) {
            const int con = f.rec[0x19];
            bonus += con >= 4 && con <= 6 ? 1 : con >= 7 && con <= 10 ? 2 : con >= 11 && con <= 13 ? 3 : con >= 14 && con <= 17 ? 4
                     : con >= 18 && con <= 20 ? 5 : 0;
        }
        if ((g_kind & 2) && hasx(f, fx.mon.resist_cold)) bonus += 3;
        if ((g_kind & 1) && hasx(f, fx.mon.resist_fire)) bonus += 3;
        if ((g_kind & 1) && hasx(f, fx.items.fire_ring)) bonus += 4;      // (Secret's ring of fire resistance)
        if (hasx(f, fx.items.luck)) ++bonus;                              // (Secret's luck stone)
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
    // (a byte, compared unsigned: a total below 0 wraps round to a save made -
    // the listing's do_saving_throw)
    return ((r + bonus + static_cast<int8_t>(f.rec[0x186])) & 0xFF) >= f.rec[0xDF + type];
}

// The caster's level for the spell's kind (as spells::power, from the record)
int power_of(const Rec& r, const classes::Tables& st, int s, bool item)
{
    if (item && st.spell_class(s) != 3) return 6;
    const int cl = r[0x109], pa = r[0x10C], ra = r[0x10D], mu = r[0x10E];
    // (Secret, GAME.OVR 0x3AFD7: no paladin in either test - spells::power)
    const bool pal_rule = !st.lay.secret || rules::fixes();
    if (cl == 0 && mu == 0 && (!pal_rule || pa < 9) && ra < 8) return 6;
    auto most = [](int a, int b) { return a > b ? a : b; };
    // The levels that count (classes::skill_level): a human who changed
    // class adds the old class's levels once the new class has passed them
    // (coab's facts: spellMaxTargetCount uses the skill levels)
    int k = 0;
    while (k < 7 && r[0x109 + k] == 0) ++k;
    const bool past = r[0x74] == 7 && r[0x109 + k] > r[0xE6];
    auto skill = [&](int cls) { return r[0x109 + cls] + (past ? r[0x111 + cls] : 0); };
    switch (st.spell_class(s)) {
    case 0: return most(skill(0), skill(3) - (pal_rule ? 8 : 0));
    case 1: return most(skill(4) - 7, 0);
    case 2: return most(skill(5), skill(4) - 8);
    case 3: return 12;
    default: return 0;
    }
}

namespace {

int dispel_chance(int L, int lvl);

int sleep_cost(const Fighter& f, bool secret)
{
    switch (f.rec[0xE5]) {
    case 0: case 1: return 1;
    case 2: return 2;
    case 3: return 4;
    case 4: return 6;
    // (Secret, GAME.OVR 0x2EB9C: 10 for its race 0 - which no record has; its
    // monsters are race 7 - so 20)
    case 5: return f.rec[0x74] == 0 && !secret ? 10 : 20;
    default: return 20;
    }
}

} // namespace

// Darkness's Meteor Swarm (R8; GAME.OVR 0x2FBEC): four meteors, the k-th
// landing on a diagonal of the square aimed at (x / y offsets -1 1 -1 1 /
// -1 -1 1 1), else straight above / below it, beside it, on it - the first
// the caster's line reaches; each strikes those it lands on and one square
// beyond (10d4, no save) and bursts on those within 1 of it (10d4, a save
// for half). The landing squares stay in the Battle for the pictures.
static int meteors(Battle& b, int caster, const FightSpell& fs, const Harm& hs, create::Dice& d, SpellLine* out, int cap)
{
    int lines = 0;
    auto say = [&](int who, Did did, int amount) {
        if (lines < cap) out[lines++] = SpellLine{static_cast<uint8_t>(who), did, amount};
    };
    b.n_meteors = 0;
    if (!b.tables || b.aim_x < 0) return 0;
    const Tables& t = *b.tables;
    static const int8_t kMdx[4] = {-1, 1, -1, 1}, kMdy[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; ++k) {
        const int tries[4][2] = {{b.aim_x + kMdx[k], b.aim_y + kMdy[k]}, {b.aim_x, b.aim_y + kMdy[k]},
                                 {b.aim_x + kMdx[k], b.aim_y}, {b.aim_x, b.aim_y}};
        int x = -1, y = -1;
        for (const auto& tr : tries) {
            if (!on_field(tr[0], tr[1]) || !b.ground[tr[1]][tr[0]] || tile(b, t, tr[0], tr[1])[0] == 0xFF) continue;
            if (!path(b, t, caster, tr[0], tr[1], false, nullptr)) continue;
            x = tr[0];
            y = tr[1];
            break;
        }
        if (x < 0) continue;
        b.meteor_x[b.n_meteors] = static_cast<uint8_t>(x);
        b.meteor_y[b.n_meteors++] = static_cast<uint8_t>(y);
        int struck[kMaxFighters], ns = 0;
        if (b.who[y][x]) struck[ns++] = b.who[y][x] - 1;
        int hits[32];
        BoltSeg segs[16];
        int nseg = 0;
        const int nh = bolt_path(b, t, caster, x, y, 1, false, hits, 32, segs, &nseg, 16);
        for (int j = 0; j < nh && ns < kMaxFighters; ++j) {
            bool seen = false;
            for (int q = 0; q < ns; ++q) seen = seen || struck[q] == hits[j];
            if (!seen) struck[ns++] = hits[j];
        }
        int area[kMaxFighters];
        const int na = in_area(b, t, x, y, 1, area, kMaxFighters);
        for (int j = 0; j < ns + na; ++j) {
            const bool direct = j < ns;
            const int c = direct ? struck[j] : area[j - ns];
            if (!direct) {
                bool seen = false;
                for (int q = 0; q < ns; ++q) seen = seen || struck[q] == c;
                if (seen) continue;
            }
            if (c < 0 || c >= b.n || !b.f[c].up()) continue;
            Harm h = hs;
            h.dice = fs.n;
            h.save = direct ? -1 : 4;
            h.on_save = direct ? 0 : 2;
            bool dn = false;
            const int done = harm(b, c, d.roll(fs.sides, fs.n) & 0xFF, h, d, &dn);
            say(c, done > 0 ? Did::Damage : Did::Unaffected, done);
            if (dn) say(c, Did::Down, 0);
        }
    }
    return lines;
}

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
    // (Secret's Delayed Blast Fireball, 0x73, the same routine)
    if ((spell == 0x2F || spell == 0x40 || spell == 0x73) && fs.does == SpellDoes::Damage && !b.indoors && b.tables &&
        b.aim_x >= 0)
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
        hs.hostile = e.foe != 0;
    }
    // A spell's effect on fighter k: magic resistance first ("is Unaffected")
    auto resisted = [&](int k, int effect) {
        b.lost_image = -1;
        if (!resists(b, who[k], effect ? effect : -1, hs, d)) return false;
        say(who[k], b.lost_image == who[k] ? Did::LostImage : Did::Unaffected, 0);
        b.lost_image = -1;
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
            // The hold spells take persons only (but Hold Monsters). Secret
            // (GAME.OVR 0x2E42C): Hold Person never holds its races 0 and 7 (the
            // engine's race 0), saying nothing; no size rule
            if (fs.does == SpellDoes::Hold && b.fx && b.fx->secret) {
                if ((spell == 0x17 || spell == 0x31) && f.rec[0x74] == 0) continue;
            } else if (fs.does == SpellDoes::Hold && spell != 0x5E && (f.rec[0x11A] > 1 || f.rec[0xDE] > 1)) {
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
            if (fs.does == SpellDoes::Mirror) data = b.fx && b.fx->mirror_plain ? d.roll(4, 1) : (d.roll(4, 1) << 4) + pw;
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
            if (fs.kind == 1 && b.fx && b.fx->mon.poisoned) {
                // Pool's 0x3A (ovr23+2E4D): the poisoned lose the poison and Slow
                // Poison instead, nothing said
                const int at = find_aff(f, b.fx->mon.poisoned);
                if (at >= 0) {
                    drop_aff(f, at);
                    const int sp = b.fx->sp.slow_poison ? find_aff(f, b.fx->sp.slow_poison) : -1;
                    if (sp >= 0) drop_aff(f, sp);
                    continue;
                }
            }
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
            // Secret (GAME.OVR 0x2E313): only the last one picked for Charm
            // Monsters; Charm Person (0x2E73D) takes its races under 7 (the
            // engine's races but 0) or flags 2 & 2 - no size rule
            const bool sec = b.fx && b.fx->secret;
            if (fs.kind == 1 && (k == m - 1 || (!sec && (f.rec[0x11A] > 1 || f.rec[0xDE] > 1)))) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            // (Charm Person or Mammal, 0x60 - GAME.OVR 0x31D6B: or flags 1, record 0x15D (here 0x14B), & 2)
            const bool person = f.rec[0x74] != 0 || (spell == 0x60 ? (f.rec[0x14B] & 2) : (f.flags2 & 2));
            if (fs.kind != 1 && (sec ? !person : f.rec[0x11A] > 1 || f.rec[0xDE] > 1)) {
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
            if (b.risen == who[k]) say(who[k], Did::Risen, 0);
            b.risen = -1;
        }
        break;
    case SpellDoes::Slay:
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            if (resisted(k, 0)) continue;
            if (f.flags2 & 8) {                             // (Secret's: the unliving - GAME.OVR 0x30D9B)
                say(who[k], Did::Unaffected, 0);
                continue;
            }
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
                // (Secret, GAME.OVR 0x3141C: the first one "is Befuddled" - its word3)
                say(who[k], fs.word3 ? Did::Word3 : Did::Word, 0);
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
            int adj = f.rec[0x75] == 0 ? -1 : f.rec[0x75] == 5 ? 4 : 2;
            if (b.fx && b.fx->secret) {
                // Secret's (GAME.OVR 0x31B94) by the class byte: clerics (C/F, C/T) -1;
                // rangers +1 at 8 Hit Dice, +2 from 9; paladins +4 from 9; magic-users
                // (F/MU, F/MU/T, MU/T) +4 a human, else +2; C/F/MU, C/R, C/MU +2; any
                // other class (fighters, thieves - most monsters) "is unaffected"
                const int hd = f.rec[0xE5];
                bool can = true;
                switch (f.rec[0x75]) {
                case 0: case 8: case 12: adj = -1; break;
                case 4: adj = hd == 8 ? 1 : hd >= 9 ? 2 : 0; break;
                case 3: adj = hd >= 9 ? 4 : 0; break;
                case 5: case 13: case 15: case 16: adj = f.rec[0x74] == 7 ? 4 : 2; break;
                case 9: case 10: case 11: adj = 2; break;
                default: can = false; break;
                }
                if (!can) {
                    say(who[k], Did::Unaffected, 0);
                    continue;
                }
            }
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
        // outdoors only; a game with no entangling effect (Secret: its routine
        // GAME.OVR 0x30E46 is empty) - nothing at all
        if (b.indoors || !b.fx || !b.fx->entangle) break;
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
            // (Secret, GAME.OVR 0x2E313: the last one picked, else a save - no size or type rule)
            const bool sec = b.fx && b.fx->secret;
            if (k == m - 1 || (!sec && (f.rec[0x11A] > 1 || f.rec[0xDE] > 1)) || saving_throw(f, e.save, 0, d)) {
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
            // (Secret, GAME.OVR 0x30AE6: the caster "turns sticks into snakes")
            say(b.fx->secret ? caster : who[k], Did::Word, 0);
            if (pw > att) give_aff(f, b.fx->sticks, minutes, pw - att, false);
            f.moves = f.attacks[0] = f.attacks[1] = 0;
        }
        break;
    case SpellDoes::SnakeCharm: {
        // Snakes on either side, as many as the caster's hit points cover
        int budget = me.hp();
        for (int c = 0; c < b.n; ++c) {
            Fighter& f = b.f[c];
            // (Secret, GAME.OVR 0x2EDED: a snake is flags 1 - record 0x15D, here 0x14B - & 0x10,
            // not already charmed)
            const bool snake = b.fx && b.fx->secret ? (f.rec[0x14B] & 0x10) && !(e.affect && f.has(static_cast<uint8_t>(e.affect)))
                                                    : f.rec[0x11A] == 14;
            if (!f.up() || !f.size || !snake || f.hp() > budget) continue;
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
        // either way (then their own Strength: the original leaves it unset).
        // Pool: against the Strength in use, the gift 21 either way
        const bool pool = st.lay.pool;
        const int own = me.rec[pool ? 0x11 : 0x10], own00 = me.rec[pool ? 0x1C : 0x1D];
        const bool more = 21 > own;
        const int data = more || pool ? 121 : own == 18 ? own00 + 1 : own + 100;
        if (b.fx && b.fx->sp.giant) give_aff(me, b.fx->sp.giant, minutes, data, true);
        if (more) say(caster, Did::Word, 0);
        break;
    }
    case SpellDoes::Strength:
        // (Gateway: the camp's Strength - spells.cpp Does::Strength, Curse's rules)
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            auto had = [&](int cl) { return f.rec[0x109 + cl] > 0 || f.rec[0x111 + cl] > 0; };
            int die = 0;
            if (had(classes::MagicUser)) die = 4;
            if (had(classes::Cleric) || had(classes::Thief)) die = 6;
            if (had(classes::Fighter)) die = 8;
            if (!die) continue;
            const int bonus = d.roll(die, 1);
            int str = f.rec[0x11] + bonus, str00 = 0;
            if (str > 18) {
                if (had(classes::Fighter) || had(classes::Paladin) || had(classes::Ranger)) {
                    str00 = f.rec[0x1C] + (str - 18) * 10;
                    if (str00 > 100) str00 = 100;
                }
                str = 18;
            }
            if (!(str > f.rec[0x11] || (str == 18 && str00 > f.rec[0x1C]))) continue;
            give_aff(f, e.affect, minutes, 100 + bonus, false);
        }
        break;
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
        const int data = spells::enlarge_data(pw, st.lay.secret);
        int str = 18, str00 = 0;
        if (data > 101) str = data - 100;
        else str00 = data - 1;
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            const int now = f.rec[0x11], now00 = f.rec[0x1C];
            bool more = data && (str > now || (str == 18 && now == 18 && str00 > now00));
            // Secret (GAME.OVR 0x372FC): against the OWN Strength (0x10, its 18/xx 0x1D)
            if (st.lay.secret) more = str > f.rec[0x10] || (str == 18 && str00 > f.rec[0x1D]);
            if (st.lay.pool) {
                // Pool (ovr23+12A5): the effect either way, "is stronger" when it's more
                if (b.fx && b.fx->sp.enlarge) give_aff(f, b.fx->sp.enlarge, minutes, data, true);
                if (more) say(who[k], Did::Word, 0);
                continue;
            }
            if (!more) {
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
                    if (f.items[j][0x34] && items::passive(f.items[j]) == items::Passive::Effect && f.items[j][0x3D]) {
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
        // (Pool, poolrad_combat_facts.md 3.2: every target in the area, once each)
        const bool pool = st.lay.pool;
        for (int pass = 0; pass < m; ++pass) {
            Fighter& f = b.f[who[pool ? pass : 0]];
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
            if (any) say(who[pool ? pass : 0], Did::Word, 0);
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
            for (int k = 0; k < 84; ++k) f.rec[0x1E + k] = 0;   // no spells
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
    case SpellDoes::FullHeal:
        // Secret's Heal (GAME.OVR 0x2F699; its Barkskin runs it too - the
        // handler table's entry for both - and gives no effect): disease (its
        // list DS 0x1B3E: helpless and the three of the disease's course),
        // blindness, feeblemind cured, then the most hit points less the dice
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            const int st2 = f.status();
            if (st2 != party::Okay && st2 != party::Animated && st2 != party::Unconscious && st2 != party::Dying) continue;
            if (b.fx) {
                bool ill = false;
                const uint8_t ills[4] = {b.fx->held[3], b.fx->sp.ill, b.fx->sp.weak, b.fx->sp.sick};
                for (uint8_t dz : ills) {
                    const int at = dz ? find_aff(f, dz) : -1;
                    if (at >= 0) {
                        drop_aff(f, at);
                        ill = true;
                    }
                }
                if (ill && fs.word3) say(who[k], Did::Word3, 0);
                int at = b.fx->blinded ? find_aff(f, b.fx->blinded) : -1;
                if (at >= 0) {
                    drop_aff(f, at);
                    if (fs.word2) say(who[k], Did::Word2, 0);
                }
                at = b.fx->feeble ? find_aff(f, b.fx->feeble) : -1;
                if (at >= 0) {
                    drop_aff(f, at);
                    if (fs.word4) say(who[k], Did::Word4, 0);
                }
            }
            const int target = f.hp_max() - d.roll(fs.sides, fs.n);
            if (target <= f.hp()) continue;
            f.rec[kHp] = static_cast<uint8_t>(target);
            if (st2 == party::Dying) {
                f.rec[kHealth] = party::Unconscious;
                f.bleeding = 0;
            }
            say(who[k], fs.word ? Did::Word : Did::Healed, target);
        }
        break;
    case SpellDoes::HarmTouch:
        // Harm (GAME.OVR 0x3054D): diseased, then the hit points down to the dice
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            if (resisted(k, e.affect)) continue;
            if (b.fx && b.fx->sp.ill) give_aff(f, b.fx->sp.ill, 0, 0, false);
            if (fs.word) say(who[k], Did::Word, 0);
            const int left = d.roll(fs.sides, fs.n);
            // (the original, GAME.OVR 0x305DB: the damage HP - 1d4 as a byte - under
            // the roll it wraps round to over 250: dead. AD&D's - nothing then -
            // with rules::fixes())
            if (f.hp() == left || (f.hp() < left && rules::fixes())) continue;
            Harm h = hs;
            h.dice = fs.n;
            bool dn = false;
            const int done = harm(b, who[k], (f.hp() - left) & 0xFF, h, d, &dn);
            if (done > 0) say(who[k], Did::Damage, done);
            if (dn) say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::Youth:
        // The Elixir of Youth (GAME.OVR 0x30813): its user only (0x7D38)
        for (int k = 0; k < 1; ++k) {
            Fighter& f = me;
            who[k] = caster;
            int age = (f.rec[0x76] | f.rec[0x77] << 8) - (d.roll(fs.sides, fs.n) + fs.plus);
            const bool floor = age < 18;
            if (floor) age = 18;
            f.rec[0x76] = static_cast<uint8_t>(age);
            f.rec[0x77] = static_cast<uint8_t>(age >> 8);
            say(who[k], floor ? Did::Word2 : Did::Word, 0);
        }
        break;
    case SpellDoes::DeathSpell: {
        // Secret's (GAME.OVR 0x31F30): 2 x 4d20 points spent in the targets'
        // order, by Hit Dice - 0 1, 1 2, 2-3 8, 4-5 16, 6 16 (max HP over 51:
        // 20), 7-8 20, more never; no save; flags 2's 8 unharmed
        int points = d.roll(20, 4) * 2;
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            const int hd = f.rec[0xE5];
            const int cost = hd == 0 ? 1 : hd == 1 ? 2 : hd <= 3 ? 8 : hd <= 5 ? 16 : hd == 6 ? (f.hp_max() > 0x33 ? 20 : 16)
                           : hd <= 8 ? 20 : 0xA2;
            if ((f.flags2 & 8) || points < cost) continue;
            points -= cost;
            say(who[k], Did::Word, 0);
            damage(b, who[k], f.hp() + 10);
            f.rec[kHealth] = party::Dead;
            say(who[k], Did::Fallen, 0);
        }
        break;
    }
    case SpellDoes::Disintegrate:
    case SpellDoes::Petrify:
        // Secret's (GAME.OVR 0x3205C, 0x32323): magic resistance, a save
        // against spells, else dead / stoned
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || resisted(k, 0) || saving_throw(f, e.save, 0, d)) continue;
            say(who[k], Did::Word, 0);
            damage(b, who[k], f.hp() + 10);
            f.rec[kHealth] = fs.does == SpellDoes::Petrify ? party::Stoned : party::Dead;
            say(who[k], Did::Fallen, 0);
        }
        break;
    case SpellDoes::Unstone:
        // Secret's Stone to Flesh in a fight (GAME.OVR 0x32120): back up, 1 HP
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (f.status() != party::Stoned) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            f.rec[kHealth] = party::Dead;                   // (stand_up won't lift the stoned)
            if (!stand_up(b, who[k], false)) {
                f.rec[kHealth] = party::Stoned;
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            f.rec[kHp] = 1;
            say(who[k], Did::Risen, 0);
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Stun:
        // Secret's Power Word Stun (GAME.OVR 0x32739): no save; rounds by the
        // hit points left
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            const int hp = f.hp();
            const int rounds = hp >= 1 && hp <= 30 ? d.roll(4, 4) : hp >= 31 && hp <= 60 ? d.roll(4, 2)
                             : hp >= 61 && hp <= 90 ? d.roll(4, 1) : 0;
            if (rounds > 0 && e.affect) give_aff(f, e.affect, rounds, 0, false);
            say(who[k], Did::Word, 0);
        }
        break;
    case SpellDoes::Blade:
        // Darkness's Blade Barrier at the cast (R2; the knives laid by the
        // caller): those in it - magic resistance, a save against spells,
        // else "is cut by knives" and 8d8 (no save for half)
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up() || resisted(k, 0)) continue;
            if (saving_throw(f, e.save, 0, d)) continue;
            say(who[k], Did::Word, 0);
            Harm h = hs;
            h.dice = fs.n;
            bool dn = false;
            const int done = harm(b, who[k], d.roll(fs.sides, fs.n) & 0xFF, h, d, &dn);
            if (done > 0) say(who[k], Did::Damage, done);
            if (dn) say(who[k], Did::Down, 0);
        }
        break;
    case SpellDoes::EnergyDrain:
        // (the level itself and its words: the caller, drain() - no save, no
        // blow, no magic resistance: R3)
        break;
    case SpellDoes::Destroy:
        // Darkness's Destruction (R4): no save
        for (int k = 0; k < m && k < 1; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            if ((f.flags2 & 8) || resists(b, who[k], 0, hs, d)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            say(who[k], Did::Word, 0);
            f.rec[kHp] = 0;
            f.rec[kHealth] = party::Gone;
            fall(b, who[k]);
            say(who[k], Did::Fallen, 0);
        }
        break;
    case SpellDoes::MassCharm: {
        // Darkness's (R5; GAME.OVR 0x2F761): Hit Dice against 2 x level, in
        // the targets' order, until the count is exactly reached; persons only,
        // not large, of the other side ("is unaffected" - word2, not counted);
        // one that would pass the count is left out; a save against spells at
        // -2, else charmed (its side the caster's)
        const int budget = 2 * pw;
        int used = 0;
        const int caster_side = me.team() ? 1 : 0;
        for (int k = 0; k < m && used != budget; ++k) {
            Fighter& f = b.f[who[k]];
            if (!f.up()) continue;
            if (!(f.flags2 & 2) || (f.rec[kSize] & 0x7F) > 1 || (f.team() ? 1 : 0) == caster_side) {
                say(who[k], Did::Word2, 0);
                continue;
            }
            const int hd = f.rec[kHd];
            if (used + hd > budget) continue;
            used += hd;
            if (resisted(k, b.fx ? b.fx->charm : 0)) continue;
            if (saving_throw(f, e.save, -2, d)) {
                say(who[k], Did::Unaffected, 0);
                continue;
            }
            if (!b.fx || !b.fx->charm) continue;
            // (the data as the engine's Charm Person: its own side kept under it for when it ends)
            give_aff(f, b.fx->charm, minutes, (caster_side << 7) | ((f.team() ? 1 : 0) << 6) | 0x20 | (pw & 0x1F), false);
            f.rec[0x197] = static_cast<uint8_t>(caster_side);
            f.target = -1;
            say(who[k], Did::Word, 0);
        }
        break;
    }
    case SpellDoes::Dance: {
        // Otto's Irresistible Dance (R6; GAME.OVR 0x2F8FB): the touch's blow,
        // then 1d4 + 1 rounds of lost turns and another 1d4 + 1 of dancing;
        // no save
        Fighter& f = b.f[who[0]];
        if (!f.up() || !b.fx) break;
        const int roll = d.roll(20, 1);
        if (roll == 1 || (roll != 20 && roll + static_cast<int8_t>(me.rec[kHit]) < f.rec[kAc])) {
            say(who[0], Did::Misses, 0);
            break;
        }
        if (resisted(0, b.fx->fumbling)) break;
        if (b.fx->fumbling) give_aff(f, b.fx->fumbling, d.roll(fs.sides, fs.n) + fs.plus, pw, false);
        if (b.fx->dk.dancing) give_aff(f, b.fx->dk.dancing, d.roll(fs.sides, fs.n) + fs.plus, pw, false);
        say(who[0], Did::Word, 0);
        break;
    }
    case SpellDoes::WordBlind: {
        // Power Word Blind (R7; GAME.OVR 0x2F9BF): those of HP max 100 or less
        // while the total stays at 100 or less; no save. The original works
        // out (1d4 + 1) rounds (x 10 for a total under 51) and never uses it -
        // its effect lasts the row's time, 0: for good. AD&D's with the rules
        // fixes (SPEC 12)
        int total = 0;
        for (int k = 0; k < m; ++k) {
            const Fighter& f = b.f[who[k]];
            if (f.hp_max() <= 100 && total + f.hp_max() <= 100) total += f.hp_max();
        }
        int rounds = minutes;
        if (rules::fixes()) {
            rounds = d.roll(4, 1) + 1;
            if (total < 51) rounds *= 10;
        }
        int sum = 0;
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            if (f.hp_max() > 100 || sum + f.hp_max() > 100) continue;
            sum += f.hp_max();
            if (!b.fx || resisted(k, b.fx->blinded)) continue;
            give_aff(f, b.fx->blinded, rounds, 0, false);
            say(who[k], Did::Word, 0);
        }
        break;
    }
    case SpellDoes::WordKill: {
        // Power Word Kill (R9; GAME.OVR 0x2FEB2): the first of 11-60 HP max
        // alone; else every one of 10 or less; no save
        auto slay = [&](int k) {
            if (resists(b, who[k], 0, hs, d)) return;
            Fighter& f = b.f[who[k]];
            say(who[k], Did::Word, 0);
            damage(b, who[k], f.hp() + 10);
            f.rec[kHealth] = party::Dead;
            say(who[k], Did::Fallen, 0);
        };
        // With the rules fixes AD&D's (1st edition; Tom, 2026-10-23 - SPEC
        // 12): by hit points now - the first of 11-60 alone, else those of
        // 10 or less while their total stays at 120 or less
        if (rules::fixes()) {
            const int h0 = b.f[who[0]].hp();
            if (h0 > 10 && h0 <= 60) {
                if (b.f[who[0]].up()) slay(0);
                break;
            }
            int total = 0;
            for (int k = 0; k < m; ++k) {
                const int h = b.f[who[k]].hp();
                if (!b.f[who[k]].up() || h > 10 || total + h > 120) continue;
                total += h;
                slay(k);
            }
            break;
        }
        if (b.f[who[0]].hp_max() > 10 && b.f[who[0]].hp_max() <= 60) {
            if (b.f[who[0]].up()) slay(0);
            break;
        }
        for (int k = 0; k < m; ++k)
            if (b.f[who[k]].up() && b.f[who[k]].hp_max() <= 10) slay(k);
        break;
    }
    case SpellDoes::Meteors:
        lines += meteors(b, caster, fs, hs, d, out + lines, cap - lines);
        break;
    case SpellDoes::Summon:
        break;                          // (the caller: the fight's reserve)
    case SpellDoes::Restore:
        // (the level itself: the caller, create::restore)
        for (int k = 0; k < m; ++k)
            if (b.f[who[k]].rec[0xE7] > 0) say(who[k], Did::Word, 0);
        break;
    case SpellDoes::Sleep: {
        int budget = d.roll(4, 4);
        for (int k = 0; k < m; ++k) {
            Fighter& f = b.f[who[k]];
            const int cost = sleep_cost(f, b.fx && b.fx->secret);
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
    if (fs.does == SpellDoes::Youth || fs.does == SpellDoes::Unstone) return false;      // (never the computer's)
    // For their own good
    if (fs.does == SpellDoes::Heal || fs.does == SpellDoes::FullHeal) {
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
            const bool scroll = (slot >= 11 && slot <= 13) ||
                                (b.fx && b.fx->items.scrolls[0] &&
                                 (it[0x2E] == b.fx->items.scrolls[0] || it[0x2E] == b.fx->items.scrolls[1]));
            if (!it[0x34] || scroll || it[0x3E] >= 0x80 || !it[0x3D]) continue;
            // (Pool: the spell is the id less 0x17 over 0x38, and judged so - ovr10+04CB;
            // Curse: the id itself, judged by the row 0x17 back)
            const int sp = magic::item_spell(it, st.lay.pool);
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
    // Gateway's black dragon (0x9E, its morale list 01 02 0B 9E - GAME.OVR
    // 0x15220): in the fight, under 20 hit points and its side's count (DS
    // 0x824C + side: those in the fight) 1 or less - it "Surrenders"; the
    // enemy's adds 1 to the area word (b.yielded: the caller's)
    if (b.fx && hasx(f, b.fx->gw.surrender) && f.up() && f.hp() < 20) {
        int count = 0;
        for (int c = 0; c < b.n; ++c)
            if (b.f[c].up() && b.f[c].size && b.f[c].team() == f.team()) ++count;
        if (count <= 1) {
            b.yielded = f.team() == 1;
            return Morale::Surrender;
        }
    }
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

// Pools of Darkness's (the fights' rules facts R13; overlay 0xA1, GAME.OVR
// 0x1F71A): the cleric's level or the paladin's - 2, 9-13 -> 9, 14+ -> 10,
// less the script's penalty (index 0: never); one d20; the undead in sight
// not yet turned, the highest type up to a limit (13 first): its value sets
// the count (D*: 1d6 + 6, else 1d12) the first time; the roll at least the
// value: turned (> 0) or destroyed (0, D* -1), the count down (D*'s extra 6);
// too low: the limit - 1 and a weaker type next
static int turn_undead_dk(Battle& b, const Tables& t, int cleric, create::Dice& d, int* out, int cap)
{
    Fighter& me = b.f[cleric];
    ++me.undead_turns;
    const int cl = me.rec[kLevels], pa = me.rec[kLevels + 3] - 2;
    const int lv = cl > pa ? cl : pa;
    int col = lv <= 8 ? lv : lv < 14 ? 9 : 10;
    col = b.turn_minus > col ? 0 : col - b.turn_minus;
    const int roll = d.roll(20, 1);
    int count = -1, extra = 6, limit = 13, n = 0;
    const int my = me.team() ? 1 : 0;
    while (n < cap) {
        int best = -1, type = 0;
        for (int i = 0; i < b.n; ++i) {
            const Fighter& f = b.f[i];
            const int ut = f.rec[0xE9];
            if (!f.up() || !f.size || f.fleeing || (f.team() ? 1 : 0) == my || ut <= type || ut > limit) continue;
            if (!range(b, t, cleric, i, false, nullptr)) continue;
            best = i;
            type = ut;
        }
        if (best < 0 || count == 0) break;
        const int v = static_cast<int8_t>(t.turn[type * 11 + col]);
        if (count < 0) count = v < 0 ? d.roll(6, 1) + 6 : d.roll(12, 1);
        if (roll < (v < 0 ? -v : v)) {
            if (--limit < 1) break;
            continue;
        }
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
        if (--count == 0 && extra > 0 && v < 0) ++count;
    }
    return n;
}

int turn_undead(Battle& b, const Tables& t, int cleric, create::Dice& d, int* out, int cap)
{
    Fighter& me = b.f[cleric];
    me.turned_undead = true;
    if (!t.turn_known) return 0;
    if (t.turn_cols == 11) return turn_undead_dk(b, t, cleric, d, out, cap);
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
        const int v = static_cast<int8_t>(t.turn[type * (t.turn_cols ? t.turn_cols : 10) + col]);   // (0: unset - Curse's 10)
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
        if (--count == 0 && extra > 0 && v < 0) ++count;    // (D* only: the listing's turn_undead tests < 0)
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
    if (listed(fx.shot_pointed, t) || t == fx.shot_pointed2) return Shot::Pointed;
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
    const Rec r = f.rec;
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
uint8_t knives_g(const Battle& b) { return b.fx ? b.fx->dk.knives : 0; }
uint8_t ground_of(const Battle& b, const Battle::Cloud& cl)
{
    return cl.poison ? poison_g(b) : cl.knives ? knives_g(b) : cloud_g(b);
}
// What the ground under a cloud square really is (another cloud's memory)
int ground_under(const Battle& b, int x, int y)
{
    const int g = b.ground[y][x];
    if (g != cloud_g(b) && g != poison_g(b) && (!knives_g(b) || g != knives_g(b))) return g;
    for (int c = 0; c < b.n_clouds; ++c) {
        const Battle::Cloud& cl = b.clouds[c];
        for (int k = 0; k < cloud_squares(cl.poison); ++k)
            if ((cl.present >> k & 1) && cl.x + sq_dx(cl.poison, k) == x && cl.y + sq_dy(cl.poison, k) == y)
                return cl.ground[k];
    }
    return g;
}
} // namespace

bool in_cloud(const Battle& b, int x, int y) { return on_field(x, y) && b.ground[y][x] == cloud_g(b); }
bool in_poison(const Battle& b, int x, int y) { return on_field(x, y) && b.ground[y][x] == poison_g(b); }
bool in_knives(const Battle& b, int x, int y) { return knives_g(b) && on_field(x, y) && b.ground[y][x] == knives_g(b); }

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

bool lay_cloud(Battle& b, const Tables& t, int x, int y, int rounds, bool poison, bool knives)
{
    if (b.n_clouds >= Battle::kMaxClouds) return false;
    Battle::Cloud& cl = b.clouds[b.n_clouds];
    cl = Battle::Cloud{};
    cl.x = static_cast<uint8_t>(x);
    cl.y = static_cast<uint8_t>(y);
    cl.poison = poison;
    cl.knives = knives && !poison;
    cl.rounds = static_cast<uint8_t>(rounds < 1 ? 1 : rounds > 255 ? 255 : rounds);
    cl.level = cl.knives ? static_cast<uint8_t>(cl.rounds / 3) : cl.rounds;   // (the caster's level: its rounds; the knives' a third)
    for (int k = 0; k < cloud_squares(poison); ++k) {
        const int xx = x + sq_dx(poison, k), yy = y + sq_dy(poison, k);
        if (!on_field(xx, yy) || !b.ground[yy][xx] || tile(b, t, xx, yy)[0] == 0xFF) continue;
        cl.ground[k] = static_cast<uint8_t>(ground_under(b, xx, yy));
        cl.present = static_cast<uint16_t>(cl.present | 1 << k);
    }
    for (int k = 0; k < cloud_squares(poison); ++k)
        if (cl.present >> k & 1) b.ground[y + sq_dy(poison, k)][x + sq_dx(poison, k)] = ground_of(b, cl);
    ++b.n_clouds;
    return true;
}

Did breathe_cloud(Battle& b, int i, create::Dice& d)
{
    Fighter& f = b.f[i];
    if (!f.up() || helpless(b, f)) return Did::Unaffected;
    // Secret (GAME.OVR 0x36A3A): the undead (record 0xF2, here 0xE9), those
    // immune to poison (0x63) and the unliving (flags 2 & 8) breathe nothing;
    // its effects can't be dispelled (data 0xFF)
    const bool sec = b.fx && b.fx->secret;
    if (sec && (f.rec[0xE9] || hasx(f, b.fx->mon.no_poison) || (f.flags2 & 8))) return Did::Unaffected;
    const int data = sec ? 0xFF : 0;
    if (saving_throw(f, 0, 0, d)) {
        if (b.fx && b.fx->cough) give_aff(f, b.fx->cough, 1, data, false);      // coughing for a round
        return Did::Word2;
    }
    if (b.fx) give_aff(f, b.fx->held[3], d.roll(4, 1) + 1, data, false);
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

// Darkness's Blade Barrier (R2; GAME.OVR 0x328A2): the spell's kind 8,
// magic resistance (its cloud's caster level), then 8d8 - no save
int knives_cut(Battle& b, int i, create::Dice& d, bool* down)
{
    *down = false;
    Fighter& f = b.f[i];
    if (!b.fx || !f.up() || !in_knives(b, f.x, f.y)) return 0;
    int level = 12;
    for (int c = 0; c < b.n_clouds; ++c)
        if (b.clouds[c].knives) level = b.clouds[c].level;
    Harm h;
    h.kind = 8;
    h.level = level;
    h.spell = 0x65;
    h.spell_level = 6;
    if (resists(b, i, 0, h, d)) return 0;
    h.dice = b.fx->dk.knife_n;
    return harm(b, i, d.roll(b.fx->dk.knife_sides, b.fx->dk.knife_n) & 0xFF, h, d, down);
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
                b.ground[cl.y + sq_dy(cl.poison, k)][cl.x + sq_dx(cl.poison, k)] = ground_of(b, cl);
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
    let_go(b, f, m.sucking);
    let_go(b, f, m.holding);
    // (Gateway's list 0D 3A 8B 90 96 - GAME.OVR img 0x5F10: the stirge's hold
    // goes before it can drink; with rules::fixes() it holds on and drinks)
    if (!rules::fixes()) let_go(b, f, b.fx->gw.sucking);
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
    auto fly = [&](int from, int x, int y, int pic, int frames, int delay, bool silent = false) {
        Event* e = ev(from, Ev::Fly);
        if (!e) return;
        e->kind = silent ? 1 : 0;               // (Fly: 1 = no sound of its own)
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
        if (b.round == 0 || (m.secret_ways && f.aff[k][3] > 3)) f.aff[k][3] = 3;   // (Secret: over 3 too)
        return &f.aff[k][3];
    };
    Harm breath;
    breath.save = 3;
    breath.on_save = 2;
    // Secret's pseudo-spells' reaches (0: Curse's for each)
    const int reach_far = m.far_reach, reach_near = m.near_reach;
    // (Secret: a breath not with one of its own side on its path)
    auto friend_on = [&](const int* on, int k) {
        for (int j = 0; j < k; ++j)
            if (b.f[on[j]].team() == f.team()) return true;
        return false;
    };

    // The stone gaze (the hooded medusa): a save against petrification or
    // stoned; a readied mirror sends it back at the medusa
    if (hasx(f, m.stone_gaze)) {
        int tg = special_target(b, t, i, reach_far ? reach_far : 12, false, d);
        if (tg >= 0) {
            ev(i, Ev::Gazes);
            // (Darkness's gazes fly the lightning's picture, COMSPR 5 - its
            // slot 18, ovr 0x1BE0C; no sound: facts L7)
            const int gp = b.fx->dk.gaze_pic ? b.fx->dk.gaze_pic : 5;
            fly(i, b.f[tg].x, b.f[tg].y, gp, 4, 45, b.fx->dk.gaze_pic != 0);
            // (Secret's: sent back by its target's effect 0x48 - GAME.OVR 0x1250A)
            if (hasx(f, m.mirror_gaze) && ((m.mirror_word && readied_word(b.f[tg], m.mirror_word)) ||
                                           (m.reflect && hasx(b.f[tg], m.reflect)))) {
                ev(tg, Ev::Reflects);
                fly(tg, f.x, f.y, gp, 4, 45, b.fx->dk.gaze_pic != 0);
                tg = i;
            }
            if (!saving_throw(b.f[tg], 1, 0, d)) remove(tg, party::Stoned, Ev::GazeStoned);
            if (!f.up()) return n;
        }
    }
    const Facts::DarkFx& dk = b.fx->dk;
    // Darkness's paralysing gaze (0x72, ovr 0x1D6B4): "gazes..." at its pick;
    // sent back by a mirror (0x48) when the gazer has 0x3B, as the stone gaze;
    // a save against petrification at +3 - or gaze-proof (0x7E: the gaze's
    // kind 0x10 always saved) - else paralysed for 10 rounds ("paralyzed")
    if (dk.para_gaze && hasx(f, dk.para_gaze) && m.paralyzed) {
        int tg = special_target(b, t, i, reach_far ? reach_far : 12, false, d);
        if (tg >= 0) {
            ev(i, Ev::ParaGazes);
            const int gp = dk.gaze_pic ? dk.gaze_pic : 5;           // (ovr 0x1D735: slot 18)
            fly(i, b.f[tg].x, b.f[tg].y, gp, 4, 45, dk.gaze_pic != 0);
            if (hasx(f, m.mirror_gaze) && m.reflect && hasx(b.f[tg], m.reflect)) {
                ev(tg, Ev::Reflects);
                fly(tg, f.x, f.y, gp, 4, 45, dk.gaze_pic != 0);
                tg = i;
            }
            Fighter& v = b.f[tg];
            const bool proof = dk.gaze_proof && hasx(v, dk.gaze_proof);
            if (v.up() && !proof && !saving_throw(v, 1, 3, d)) {
                give_aff(v, m.paralyzed, 10, 12, false);
                ev(tg, Ev::Paralyzed);
            }
            if (!f.up()) return n;
        }
    }
    // Darkness's vampire gaze (0x7C, ovr 0x30898 - among the breaths): its
    // pick within 10 squares, "gazes", then Charm Person on it (its save; the
    // gaze-proof make it - kind 0x10) - the caller casts it at `to`; the turn is over
    if (dk.vamp_gaze && hasx(f, dk.vamp_gaze) && b.fx->charm) {
        const int tg = special_target(b, t, i, 10, false, d);
        if (tg >= 0) {
            *ends_turn = true;
            ev(i, Ev::VampGazes);
            fly(i, b.f[tg].x, b.f[tg].y, 5, 4, 50);         // (ovr 0x30917: slot 17 - the spell's - 50 a step)
            if (!(dk.gaze_proof && hasx(b.f[tg], dk.gaze_proof))) {
                Event* e = ev(i, Ev::Cast, tg);
                if (e) e->amount = 0x0A;
            } else {
                ev(tg, Ev::Unaffected);
            }
            return n;
        }
    }
    // Pool's vampire: its charming gaze at its pick in sight - a save
    // against spells at -2 or charmed, on its side for the fight
    if (hasx(f, m.charm_gaze) && b.fx->charm) {
        const int tg = pick_target(b, t, i, d);
        if (tg >= 0 && range(b, t, i, tg, false, nullptr)) {
            ev(i, Ev::CharmGaze);
            fly(i, b.f[tg].x, b.f[tg].y, 5, 4, 45);
            Fighter& v = b.f[tg];
            Harm h;
            h.level = 12;
            if (!resists(b, tg, b.fx->charm, h, d) && !saving_throw(v, 4, -2, d)) {
                const int was = find_aff(v, b.fx->charm);
                const int own = was >= 0 ? (v.aff[was][3] & 0x40) >> 6 : (v.team() ? 1 : 0);
                give_aff(v, b.fx->charm, 0, (f.team() << 7) | (own << 6) | 0x0C, false);
                v.rec[kTeam] = static_cast<uint8_t>(f.team());
                v.target = -1;
                ev(tg, Ev::Charmed);
            }
        }
    }
    // Pool's lightning breath (its ovr23 routine; the bronze dragon): 3 a
    // fight - after the first round only half its turns - a bolt of 10
    // squares at one it can reach as Lightning Bolt would; its maximum hit
    // points, a save against breath for half; the turn is over
    if (hasx(f, m.lightning_breath) && (b.round == 0 || d.roll(100, 1) > 50)) {
        const int tg = special_target(b, t, i, 10, false, d);
        if (tg >= 0) {
            *ends_turn = true;
            ev(i, Ev::Breathes);
            Harm h = breath;
            h.kind = 4;
            const int tx = b.f[tg].x, ty = b.f[tg].y;
            Event* e = ev(i, Ev::BoltFly);
            if (e) {
                e->x = static_cast<uint8_t>(tx);
                e->y = static_cast<uint8_t>(ty);
                e->pic = 6;
            }
            const int amount = f.hp_max();
            hurt(tg, amount, h);
            int on[32];
            const int k = bolt_path(b, t, i, tx, ty, 10, false, on, 32, b.bolt, &b.n_bolt, 16);
            for (int j = 0; j < k; ++j)
                if (on[j] != tg) hurt(on[j], amount, h);
            const int left = find_aff(f, m.lightning_breath);
            if (left >= 0) {
                if (f.aff[left][3] > 0xFD) --f.aff[left][3];
                else drop_aff(f, left);
            }
            return n;
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
    if (hasx(f, m.slug_spit) && m.slug_secret) {
        // Secret's (GAME.OVR 0x13587): its pick within its pseudo-spell
        // 0x3D's reach; a chance of (11 - squares) x 10% - 10% in the first
        // round (its round count 0) - 4d8 acid, a breath save for half;
        // missed: "Spits and misses"
        const int tg = special_target(b, t, i, reach_far ? reach_far : 12, false, d);
        const int sq = tg >= 0 ? squares_to(b, t, i, tg) : 99;
        const int chance = tg < 0 || sq > 11 ? 0 : b.round == 0 ? 10 : (11 - sq) * 10;
        if (chance > 0 && d.roll(100, 1) <= chance) {
            ev(i, Ev::SpitsAcid);
            fly(i, b.f[tg].x, b.f[tg].y, 10, 1, 30);
            Harm h = breath;
            h.kind = 0x10;
            hurt(tg, d.roll(8, 4), h);
        } else if (chance > 0) {
            ev(i, Ev::SpitsMisses);
        }
    } else if (hasx(f, m.slug_spit)) {
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
        // (Darkness, GAME.OVR 0x23130: the computer's own pick - a random enemy
        // in sight, any distance - kept ray after ray while it's still in the
        // fight, else picked again; Curse / Pool / Secret: the nearest each time)
        const bool dk_pick = b.fx->dk.beholder != 0;
        int tg = dk_pick ? special_target(b, t, i, 0xFF, false, d) : nearest_enemy(b, t, i);
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
                if (!saving_throw(v, 1, 0, d)) remove(tg, party::Stoned, b.fx->dk.beholder ? Ev::RayStoned : Ev::Stoned);
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
            if (!dk_pick) tg = nearest_enemy(b, t, i);
            else if (!b.f[tg].up()) tg = special_target(b, t, i, 0xFF, false, d);
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
            if (k && !friend_on(on, k)) {
                ev(i, Ev::BreathesAcid);
                fly(i, b.f[tg].x, b.f[tg].y, 5, 1, 30);
                Harm h = breath;
                h.kind = m.acid_kind ? m.acid_kind : 0x30;
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
            // (Secret's: none on its path, or one of its own side - no breath; GAME.OVR 0x328D5)
            if (!m.secret_ways || (k && !friend_on(on, k))) {
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
    }
    // Darkness's big red dragon (GAME.OVR 0x305F4): the fire breath with
    // 24d10 + 12 (a byte's sum) in place of its hit points
    if (hasx(f, m.big_fire)) {
        uint8_t* left = breaths(m.big_fire);
        const int tg = left && *left ? special_target(b, t, i, 6, true, d) : -1;
        if (tg >= 0) {
            int on[kMaxFighters];
            const int k = cone(b, t, i, b.f[tg].x, b.f[tg].y, 9, 3, on, kMaxFighters);
            if (k && !friend_on(on, k)) {
                ev(i, Ev::BreathesFire);
                fly(i, b.f[tg].x, b.f[tg].y, 5, 1, 30);
                Harm h = breath;
                h.kind = 0x21;
                const int amount = (d.roll(10, 24) & 0xFF) + 12;
                for (int j = 0; j < k; ++j) hurt(on[j], amount, h);
                --*left;
                *ends_turn = true;
                return n;
            }
        }
    }
    // Darkness's green dragon (GAME.OVR 0x30261): poison breath - a line of
    // 3 squares toward its pick (pseudo-spell 0x3D's reach), then all within
    // 3 squares of the line's end but itself; its maximum hit points, a
    // breath save for half
    if (hasx(f, m.poison_breath)) {
        uint8_t* left = breaths(m.poison_breath);
        const int tg = left && *left ? special_target(b, t, i, 6, true, d) : -1;
        if (tg >= 0) {
            int line[kMaxFighters];
            const int k0 = cone(b, t, i, b.f[tg].x, b.f[tg].y, 3, 1, line, kMaxFighters);
            if (!friend_on(line, k0)) {
                // the line's end: 3 squares from it toward the pick
                const int dx = b.f[tg].x - f.x, dy = b.f[tg].y - f.y;
                const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
                const int steps = adx > ady ? adx : ady;
                const int ex = steps > 3 ? f.x + dx * 3 / steps : b.f[tg].x;
                const int ey = steps > 3 ? f.y + dy * 3 / steps : b.f[tg].y;
                int on[kMaxFighters];
                const int k = in_area(b, t, ex, ey, 3, on, kMaxFighters);
                ev(i, Ev::BreathesPoison);
                fly(i, ex, ey, 12, 1, 30);
                Harm h = breath;
                h.kind = 0x20;
                const int amount = f.hp_max();
                for (int j = 0; j < k; ++j)
                    if (on[j] != i) hurt(on[j], amount, h);
                --*left;
                *ends_turn = true;
                return n;
            }
        }
    }
    // Darkness's blue dragon and electric spider (GAME.OVR 0x3045C):
    // lightning - a bolt (Lightning Bolt's reach, 10 squares) at its pick,
    // its maximum hit points, a save for half; every turn while it has breaths
    if (hasx(f, m.dk_lightning)) {
        uint8_t* left = breaths(m.dk_lightning);
        const int tg = left && *left ? special_target(b, t, i, 10, false, d) : -1;
        if (tg >= 0) {
            *ends_turn = true;
            ev(i, Ev::BreathesLightning);
            Harm h = breath;
            h.kind = 0x24;
            const int tx = b.f[tg].x, ty = b.f[tg].y;
            Event* e = ev(i, Ev::BoltFly);
            if (e) {
                e->x = static_cast<uint8_t>(tx);
                e->y = static_cast<uint8_t>(ty);
                e->pic = 6;
            }
            const int amount = f.hp_max();
            hurt(tg, amount, h);
            int on[32];
            const int k = bolt_path(b, t, i, tx, ty, 10, false, on, 32, b.bolt, &b.n_bolt, 16);
            for (int j = 0; j < k; ++j)
                if (on[j] != tg) hurt(on[j], amount, h);
            --*left;
            return n;
        }
    }
    // Secret's cold breath (the white dragons; GAME.OVR 0x327F9): as the
    // fire breath, 3 lines of 7 squares
    if (hasx(f, m.cold_breath)) {
        uint8_t* left = breaths(m.cold_breath);
        const int tg = left && *left ? special_target(b, t, i, 6, true, d) : -1;
        if (tg >= 0) {
            int on[kMaxFighters];
            const int k = cone(b, t, i, b.f[tg].x, b.f[tg].y, 7, 3, on, kMaxFighters);
            // (Secret's: none on its path, or one of its own side - no breath; GAME.OVR 0x328D5)
            if (!m.secret_ways || (k && !friend_on(on, k))) {
                ev(i, Ev::BreathesCold);
                fly(i, b.f[tg].x, b.f[tg].y, 5, 1, 30);
                Harm h = breath;
                h.kind = 0x22;
                const int amount = f.hp_max();
                for (int j = 0; j < k; ++j) hurt(on[j], amount, h);
                --*left;
                *ends_turn = true;
                return n;
            }
        }
    }
    // Secret's pyrohydra (GAME.OVR 0x12D5F): while the round is within its
    // attacks byte, at one next to it: "Breathes Fire", 8 (a save for half)
    // - its round count 0 in the first round (set at the fight's setup,
    // GAME.OVR 0x10095, + 1 after each round): nothing then; its target the
    // one it last aimed at (its fight data's target, 0x12D97)
    if (hasx(f, m.pyro_fire) && b.round >= 1 && f.rec[0x11C] >= b.round) {
        const int tg = f.target >= 0 && f.target < b.n && b.f[f.target].up() && b.f[f.target].team() != f.team() ? f.target : -1;
        if (tg >= 0 && squares_to(b, t, i, tg) < 2) {
            ev(i, Ev::HoundFire);
            fly(i, b.f[tg].x, b.f[tg].y, 10, 1, 30);
            Harm h = breath;
            h.kind = 1;
            hurt(tg, 8, h);
        }
    }
    // Secret's umber hulk (GAME.OVR 0x12FAF): its gaze at one next to it -
    // a save against spells or confused 1d10 + 2 rounds ("stares blankly")
    if (hasx(f, m.confuse_gaze) && b.fx->sp.confuse) {
        const int tg = special_target(b, t, i, reach_near ? reach_near : 12, false, d);
        if (tg >= 0 && squares_to(b, t, i, tg) < 2) {
            ev(i, Ev::Gazes);
            if (b.fx->dk.gaze_pic) fly(i, b.f[tg].x, b.f[tg].y, b.fx->dk.gaze_pic, 4, 45, true);   // (ovr 0x1C4C6)
            else fly(i, b.f[tg].x, b.f[tg].y, 0x2D, 4, 45);
            Fighter& v = b.f[tg];
            if (!saving_throw(v, 4, 0, d)) {
                give_aff(v, b.fx->sp.confuse, d.roll(10, 1) + 2, f.rec[kTeam], false);
                ev(tg, Ev::StaresBlankly);
            }
            if (!f.up()) return n;
        }
    }
    // The hell hound: half its turns, next to its pick: 7 fire (a save for half)
    // (Secret's, GAME.OVR 0x13457: every turn at one next to it (its
    // pseudo-spell 0x44's reach), 7 fire and breath; it fights on)
    if (hasx(f, m.hound_fire)) {
        const int tg = special_target(b, t, i, reach_near ? reach_near : 12, false, d);
        const int roll = m.secret_ways ? 1 : d.roll(100, 1);
        if (roll <= 50 && tg >= 0 && squares_to(b, t, i, tg) < 2) {
            *ends_turn = !m.secret_ways;
            ev(i, Ev::HoundFire);
            fly(i, b.f[tg].x, b.f[tg].y, 10, 1, 30);
            Harm h = breath;
            h.kind = m.secret_ways ? 0x21 : 1;
            hurt(tg, 7, h);
            if (!m.secret_ways) return n;
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

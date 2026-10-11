#include "overland.h"

#include <cstring>

namespace overland {

bool read(Data& d, const Facts& f, ReadDs read, void* ctx)
{
    memset(&d, 0, sizeof d);
    uint8_t spec[kSpecials * 6];
    if (!read(ctx, f.map, d.map, sizeof d.map) || !read(ctx, f.columns, d.cols, sizeof d.cols) ||
        !read(ctx, f.specials, spec, sizeof spec))
        return false;
    for (int k = 0; k < kSpecials; ++k)
        for (int i = 0; i < 3; ++i) d.spec[k][i] = static_cast<uint16_t>(spec[k * 6 + i * 2] | spec[k * 6 + i * 2 + 1] << 8);
    for (int k = 0; k < 2; ++k) {
        const int n = f.heal_n[k] < sizeof d.heal[k] ? f.heal_n[k] : sizeof d.heal[k];
        if (f.heal[k] && (!read(ctx, f.heal[k], d.heal[k], n) || !read(ctx, f.heal_map[k], d.heal_map[k], 16)))
            return false;
    }
    return true;
}

int column(const Data& d, int area, int x)
{
    return (area >= 0 && area < 3 ? d.cols[area] : 0) + x;
}

void window(int col, int row, int* x0, int* y0)
{
    int x = col - 2, y = row - 2;
    if (x < 0) x = 0;
    if (x > kW - kView) x = kW - kView;
    if (y < 0) y = 0;
    if (y > kH - kView) y = kH - kView;
    *x0 = x;
    *y0 = y;
}

namespace {
bool holds(const Test& t, GetWord get, void* ctx)
{
    if (t.op == Op::Never) return false;
    if (t.op == Op::Always) return true;
    const uint16_t v = get(ctx, t.word);
    switch (t.op) {
    case Op::Ne: return v != t.value;
    case Op::Bit: return (v & t.value) != 0;
    case Op::NoBit: return (v & t.value) == 0;
    case Op::Above: return v > t.value;
    default: return false;
    }
}
} // namespace

int frame(const Data& d, const Facts& f, int col, int row, GetWord get, void* ctx)
{
    if (col < 0 || col >= kW || row < 0 || row >= kH) return -1;
    int fr = d.map[row * kW + col] - 1;
    for (int k = 0; k < kSpecials; ++k) {
        if (d.spec[k][0] != col || d.spec[k][1] != row) continue;
        for (const Test& t : f.special[k].test)
            if (holds(t, get, ctx)) {
                fr = d.spec[k][2] - 1;
                break;
            }
        break;
    }
    return fr >= 0 && fr < 256 ? fr : -1;
}

uint8_t healed_colour(const Data& d, const Facts& f, int frame, uint8_t colour)
{
    for (int k = 0; k < 2; ++k)
        for (int i = 0; i < f.heal_n[k] && i < static_cast<int>(sizeof d.heal[k]); ++i)
            if (d.heal[k][i] == frame) return d.heal_map[k][colour & 15];
    return colour;
}

Step step(const Data& d, const Facts& f, int area, int* x, int* y, int dir, bool healed)
{
    static const int8_t kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int8_t kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    int nx = *x + kDx[dir & 7], ny = *y + kDy[dir & 7];
    if (nx < 0) nx = 0;
    if (nx > 15) nx = 15;
    if (ny < 0) ny = 0;
    if (ny > kH - 1) ny = kH - 1;
    const int col = column(d, area, nx);
    const uint8_t t = col < kW ? d.map[ny * kW + col] : 0;
    for (int k = 0; k < f.n_blocked; ++k)
        if (t >= f.blocked[k * 2] && t <= f.blocked[k * 2 + 1]) return Step::Blocked;
    if (!healed && ((t >= f.river[0] && t <= f.river[1]) || t == f.river_also)) return Step::River;
    *x = nx;
    *y = ny;
    return Step::Moved;
}

} // namespace overland

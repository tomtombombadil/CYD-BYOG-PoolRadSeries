#include "qview.h"

#include <initializer_list>

namespace qview {

namespace {

struct SlotPlace {
    int8_t x, y, w;          // where (in the 88 x 88 view) and the front's width
};
// f3 f3b l2 r2 f2 l1 r1 f1 l0 r0 (queen_facts_2.md 4)
constexpr SlotPlace kPlace[kSlots] = {
    {40, 40, 8}, {40, 40, 8}, {32, 32, 8}, {48, 32, 8}, {32, 32, 24},
    {16, 16, 16}, {56, 16, 16}, {16, 16, 56}, {0, 0, 16}, {72, 0, 16},
};

// A wall set found in the libraries
struct Set {
    hlib::ByteSource* src = nullptr;
    hlib::Library lib;
    uint8_t id = kNoSet;
    bool ok = false;
};

bool find_set(const Libs& libs, uint8_t id, Set& out)
{
    out = Set{};
    out.id = id;
    if (id == kNoSet) return false;
    for (hlib::ByteSource* s : libs.walls) {
        if (!s) continue;
        hlib::Library top;
        if (hlib::open(*s, 0, s->size(), top) != hlib::Status::Ok) continue;
        const int i = hlib::find(*s, top, id);
        uint32_t pos, len;
        if (i < 0 || !hlib::entry(*s, top, i, pos, len)) continue;
        if (hlib::open(*s, pos, len, out.lib) != hlib::Status::Ok) continue;
        out.src = s;
        out.ok = true;
        return true;
    }
    return false;
}

struct Drawer {
    pic::Canvas& c;
    const Libs& libs;
    const Look& look;
    hlpic::Colours& col;
    pic::VgaPalette* pal;
    int x0, y0;
    Set sets[3];
    int coloured = -1;          // the set whose colours are in col now
    bool any = false;

    void wall(int t, int slot, int dx)
    {
        if (t < 1 || t > 15) return;
        const int k = (t - 1) / 5;
        Set& s = sets[k];
        if (!s.ok) return;
        const int part = part_of((t - 1) % 5, slot);
        uint32_t pos, len;
        hlib::Part p;
        if (part < 0 || !hlib::entry(*s.src, s.lib, part, pos, len) || !hlib::read_part(*s.src, pos, len, p)) return;
        if (coloured != k) {
            uint32_t cp, cl;
            if (hlib::entry(*s.src, s.lib, 0, cp, cl)) hlpic::load_colours(*s.src, cp, cl, col, pal);
            coloured = k;
        }
        const SlotPlace& sp = kPlace[slot];
        hlpic::Clip clip{x0, y0, x0 + kView, y0 + kView};
        if (hlpic::draw(*s.src, p, col, c, hlpic::place_x(p, x0 + sp.x + dx), hlpic::place_y(p, y0 + sp.y), clip))
            any = true;
    }
};

} // namespace

Look look_of(const geo::Map& m, uint16_t back)
{
    Look l;
    for (int k = 0; k < 3; ++k) l.sets[k] = m.head[k];
    l.back = back;
    return l;
}

int part_of(int piece, int slot)
{
    if (piece < 0 || piece > 4 || slot < 0 || slot >= kSlots) return -1;
    if (piece < 2) return piece * 10 + 1 + slot;
    if (slot == kF3b) return -1;
    return 21 + (piece - 2) * 9 + (slot > kF3b ? slot - 1 : slot);
}

bool draw(pic::Canvas& c, const Libs& libs, const geo::Map& m, int x, int y, int dir, const Look& look,
          hlpic::Colours& col, pic::VgaPalette* pal, int x0, int y0)
{
    c.fill(x0, y0, kView, kView, 0);
    bool any = false;
    // The backdrop: BACK's picture of the look (its colours too)
    if (look.back && libs.back) {
        hlib::Library top;
        hlib::ByteSource& s = *libs.back;
        uint32_t pos, len;
        int i = -1;
        if (hlib::open(s, 0, s.size(), top) == hlib::Status::Ok) i = hlib::find(s, top, look.back);
        if (i >= 0 && hlib::entry(s, top, i, pos, len))
            any = hlpic::draw_picture_part(s, pos, len, 1, col, pal, c, x0, y0, hlpic::Clip{x0, y0, x0 + kView, y0 + kView});
    }
    Drawer d{c, libs, look, col, pal, x0, y0, {}, -1, false};
    for (int k = 0; k < 3; ++k) find_set(libs, look.sets[k], d.sets[k]);
    const int ahead = dir & 6, left = (ahead + 6) & 7, right = (ahead + 2) & 7;
    auto sq_x = [&](int a, int l) { return x + geo::dx(ahead) * a + geo::dx(right) * l; };
    auto sq_y = [&](int a, int l) { return y + geo::dy(ahead) * a + geo::dy(right) * l; };
    static const int8_t kFrontSlot[3] = {kF1, kF2, kF3}, kLeftSlot[3] = {kL0, kL1, kL2}, kRightSlot[3] = {kR0, kR1, kR2};
    static const int8_t kFronts[3] = {1, 2, 5}, kSides[3] = {0, 1, 0};
    for (int a = 2; a >= 0; --a) {
        const int fs = kFrontSlot[a], fw = kPlace[fs].w;
        for (int n = kFronts[a]; n >= 0; --n)
            for (int l : {-n, n}) {
                d.wall(geo::wall(m, sq_x(a, l), sq_y(a, l), ahead), fs, l * fw);
                if (n == 0) break;
            }
        for (int n = kSides[a]; n >= 0; --n)
            for (int l : {-n, n}) {
                if (l <= 0) d.wall(geo::wall(m, sq_x(a, l), sq_y(a, l), left), kLeftSlot[a], l * fw);
                if (l >= 0) d.wall(geo::wall(m, sq_x(a, l), sq_y(a, l), right), kRightSlot[a], l * fw);
                if (n == 0) break;
            }
    }
    return any || d.any;
}

namespace {

// Part i of a TILE library at its place
bool tile_part(hlib::ByteSource& s, const hlib::Library& lib, int i, hlpic::Colours& col, pic::Canvas& c)
{
    uint32_t pos, len;
    hlib::Part p;
    return hlib::entry(s, lib, i, pos, len) && hlib::read_part(s, pos, len, p) &&
           hlpic::draw(s, p, col, c, hlpic::place_x(p, 0), hlpic::place_y(p, 0));
}

} // namespace

bool draw_frame(pic::Canvas& c, hlib::ByteSource* frame, hlib::ByteSource* gen, int dir, const int moons[3],
                hlpic::Colours& col, pic::VgaPalette* pal, int colours)
{
    // FRAME's pieces (queen_facts_2.md 6): the colours, then the bars, the
    // view's frame, the compass and its needle, the moons
    static const int8_t kPieces[] = {1, 2, 3, 4, 12, 23};
    constexpr int kNeedle = 25, kMoon = 29;
    bool ok = true;
    hlib::Library lib;
    const bool fr = frame && hlib::open(*frame, 0, frame->size(), lib) == hlib::Status::Ok;
    uint32_t pos, len;
    if (fr && hlib::entry(*frame, lib, colours, pos, len)) hlpic::load_colours(*frame, pos, len, col, pal);
    if (gen) {
        // (the lower marble first: the view's frame and the compass go over it)
        hlib::Library gl;
        ok = hlib::open(*gen, 0, gen->size(), gl) == hlib::Status::Ok && tile_part(*gen, gl, 1, col, c);
    }
    if (frame) {
        ok = ok && fr;
        for (int i : kPieces) ok = ok && tile_part(*frame, lib, i, col, c);
        // (the needles: north, south, west, east)
        static const int8_t kNeedleOf[4] = {0, 3, 1, 2};
        if (ok && dir >= 0) ok = tile_part(*frame, lib, kNeedle + kNeedleOf[(dir & 7) >> 1], col, c);
        for (int k = 0; ok && moons && k < 3; ++k)
            if (moons[k] >= 0 && moons[k] < 4) ok = tile_part(*frame, lib, kMoon + 4 * k + moons[k], col, c);
    }
    // The boxes (the original fills them: entry 23 of the frame's colours)
    const uint8_t fill = c.vga && c.bpp == 8 ? 23 : col.ega[23];
    for (const Box& b : {kPartyBox, kPlaceBox, kTextBox}) c.fill(b.x, b.y, b.w, b.h, fill);
    return ok;
}

bool draw_compass(pic::Canvas& c, hlib::ByteSource& frame, int dir, hlpic::Colours& col, int colours)
{
    hlib::Library lib;
    if (hlib::open(frame, 0, frame.size(), lib) != hlib::Status::Ok) return false;
    uint32_t pos, len;
    if (hlib::entry(frame, lib, colours, pos, len)) hlpic::load_colours(frame, pos, len, col, nullptr);
    static const int8_t kNeedleOf[4] = {0, 3, 1, 2};
    return tile_part(frame, lib, 23, col, c) && tile_part(frame, lib, 25 + kNeedleOf[(dir & 7) >> 1], col, c);
}

bool draw_area_map(pic::Canvas& c, const Libs& libs, const geo::Map& m, int x, int y, int dir,
                   hlpic::Colours& col, int x0, int y0)
{
    constexpr int kCells = kView / 8;
    c.fill(x0, y0, kView, kView, 0);
    if (!libs.topview) return false;
    hlib::ByteSource& s = *libs.topview;
    hlib::Library lib;
    if (hlib::open(s, 0, s.size(), lib) != hlib::Status::Ok) return false;
    // (its colour entry first)
    for (int i = 0; i < lib.count; ++i) {
        uint32_t pos, len;
        if (hlib::entry(s, lib, i, pos, len) && hlpic::load_colours(s, pos, len, col, nullptr)) break;
    }
    auto clamp = [](int v, int side) {
        const int top = side > kCells ? side - kCells : 0;
        return v < 0 ? 0 : v > top ? top : v;
    };
    const int ox = clamp(x - kCells / 2, m.w), oy = clamp(y - kCells / 2, m.h);
    auto tile = [&](int t, int px, int py) {
        uint32_t pos, len;
        hlib::Part p;
        return hlib::entry(s, lib, t, pos, len) && hlib::read_part(s, pos, len, p) &&
               hlpic::draw(s, p, col, c, px, py, hlpic::Clip{x0, y0, x0 + kView, y0 + kView});
    };
    bool ok = true;
    for (int r = 0; r < kCells; ++r)
        for (int k = 0; k < kCells; ++k) {
            const int mx = ox + k, my = oy + r;
            if (!m.on(mx, my)) continue;
            const int bits = (geo::wall(m, mx, my, 0) ? 1 : 0) | (geo::wall(m, mx, my, 2) ? 2 : 0) |
                             (geo::wall(m, mx, my, 4) ? 4 : 0) | (geo::wall(m, mx, my, 6) ? 8 : 0);
            ok = tile(1 + bits, x0 + k * 8, y0 + r * 8) && ok;
        }
    if (m.on(x, y)) ok = tile(17 + ((dir & 7) >> 1), x0 + (x - ox) * 8, y0 + (y - oy) * 8) && ok;
    return ok;
}

} // namespace qview

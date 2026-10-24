// HLIB libraries (src/engine/hlib.*), The Dark Queen of Krynn's storage:
// synthetic libraries made here (nothing from the game). CI builds it with
//   g++ -std=c++17 -O1 -Wall -Werror -fsanitize=address,undefined -Isrc
//       -o test_hlib tools/host_tests/test_hlib.cpp src/engine/*.cpp
#include "engine/hlib.h"
#include "engine/hlpic.h"
#include "engine/qview.h"

#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, int v) { b.push_back(v & 255); b.push_back((v >> 8) & 255); }
static void put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 255); }

// A library of these entries; dir = (id, entry) pairs for a directory entry 0
static Bytes library(const char* kind, const std::vector<Bytes>& entries, const std::vector<std::pair<int, int>>* dir = nullptr)
{
    std::vector<Bytes> all;
    if (dir) {
        Bytes d;
        put16(d, static_cast<int>(dir->size()));
        for (auto& p : *dir) { put16(d, p.first); put16(d, p.second); }
        all.push_back(d);
    }
    for (auto& e : entries) all.push_back(e);
    const uint32_t head = 16 + 4 * static_cast<uint32_t>(all.size() + 1);
    uint32_t size = head;
    for (auto& e : all) size += static_cast<uint32_t>(e.size());
    Bytes b = {'H', 'L', 'I', 'B'};
    put32(b, size);
    put16(b, static_cast<int>(all.size()));
    b.push_back(0);
    b.push_back(dir ? 1 : 0);
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(kind[i]));
    uint32_t at = head;
    for (auto& e : all) { put32(b, at); at += static_cast<uint32_t>(e.size()); }
    put32(b, at);
    for (auto& e : all) b.insert(b.end(), e.begin(), e.end());
    return b;
}

static Bytes part_header(int h, int x, int y, int units, int kind)
{
    Bytes b;
    put16(b, h); put16(b, x); put16(b, y);
    b.push_back(static_cast<uint8_t>(units));
    b.push_back(static_cast<uint8_t>(kind));
    return b;
}
static void pad(Bytes& b) { if (b.size() & 1) b.push_back(0); }

// HLIB's run-length coding (runs of 257 - c), greedy
static Bytes rle(const Bytes& in)
{
    Bytes out;
    size_t i = 0;
    while (i < in.size()) {
        size_t run = 1;
        while (i + run < in.size() && in[i + run] == in[i] && run < 128) ++run;
        if (run >= 3) {
            out.push_back(static_cast<uint8_t>(257 - run));
            out.push_back(in[i]);
            i += run;
            continue;
        }
        size_t lit = 0;
        while (i + lit < in.size() && lit < 128) {
            size_t r = 1;
            while (i + lit + r < in.size() && in[i + lit + r] == in[i + lit] && r < 3) ++r;
            if (r >= 3) break;
            ++lit;
        }
        out.push_back(static_cast<uint8_t>(lit - 1));
        out.insert(out.end(), in.begin() + i, in.begin() + i + lit);
        i += lit;
    }
    return out;
}

// A picture of w x h palette indexes; -1 = transparent
using Img = std::vector<std::vector<int>>;

static Img random_img(std::mt19937& rng, int w, int h, int holes_percent, int colours)
{
    Img im(h, std::vector<int>(w));
    for (auto& row : im)
        for (int& v : row) v = static_cast<int>(rng() % 100) < holes_percent ? -1 : static_cast<int>(rng() % colours);
    return im;
}

// Mode X planes one after the other (transparent -> 0xFF)
static Bytes vga_planes(const Img& im)
{
    const int h = static_cast<int>(im.size()), w = static_cast<int>(im[0].size()), q = w / 4;
    Bytes b;
    for (int p = 0; p < 4; ++p)
        for (int r = 0; r < h; ++r)
            for (int x = 0; x < q; ++x) {
                const int v = im[r][x * 4 + p];
                b.push_back(v < 0 ? 0xFF : static_cast<uint8_t>(v));
            }
    return b;
}

// Plane by plane, each row: segments (skip, count, pixels), 0 at its end
static Bytes vga_sprite(const Img& im)
{
    const int h = static_cast<int>(im.size()), w = static_cast<int>(im[0].size()), q = w / 4;
    Bytes b;
    for (int p = 0; p < 4; ++p)
        for (int r = 0; r < h; ++r) {
            int x = 0;
            while (x < q) {
                int skip = 0;
                while (x + skip < q && im[r][(x + skip) * 4 + p] < 0) ++skip;
                if (x + skip >= q) break;
                while (skip > 128) {                     // (a skip byte goes up to 128)
                    b.push_back(0x80);
                    b.push_back(0);
                    skip -= 128;
                    x += 128;
                }
                int n = 0;
                while (x + skip + n < q && im[r][(x + skip + n) * 4 + p] >= 0 && n < 127) ++n;
                if (skip == 0) {
                    b.push_back(static_cast<uint8_t>(n));      // (c < 0x80: no skip)
                } else {
                    b.push_back(static_cast<uint8_t>(256 - skip));
                    b.push_back(static_cast<uint8_t>(n));
                }
                for (int k = 0; k < n; ++k) b.push_back(static_cast<uint8_t>(im[r][(x + skip + k) * 4 + p]));
                x += skip + n;
            }
            b.push_back(0);
        }
    return b;
}

// EGA bit planes of a 16-colour picture; mask plane first when masked
static Bytes ega_planes(const Img& im, bool masked)
{
    const int h = static_cast<int>(im.size()), w = static_cast<int>(im[0].size()), q = w / 8;
    Bytes b;
    for (int pl = masked ? -1 : 0; pl < 4; ++pl)
        for (int r = 0; r < h; ++r)
            for (int u = 0; u < q; ++u) {
                uint8_t v = 0;
                for (int k = 0; k < 8; ++k) {
                    const int c = im[r][u * 8 + k];
                    const int bit = pl < 0 ? c < 0 : c >= 0 && ((c >> pl) & 1);
                    v |= static_cast<uint8_t>(bit << (7 - k));
                }
                b.push_back(v);
            }
    return b;
}

static Bytes ega_rows(const Img& im)   // each row: its 4 planes in turn (for RLE)
{
    const int h = static_cast<int>(im.size()), w = static_cast<int>(im[0].size()), q = w / 8;
    Bytes b;
    for (int r = 0; r < h; ++r)
        for (int pl = 0; pl < 4; ++pl)
            for (int u = 0; u < q; ++u) {
                uint8_t v = 0;
                for (int k = 0; k < 8; ++k) v |= static_cast<uint8_t>(((im[r][u * 8 + k] >> pl) & 1) << (7 - k));
                b.push_back(v);
            }
    return b;
}

static Bytes vga_rows(const Img& im)   // each row: its 4 planes in turn (for RLE)
{
    const int h = static_cast<int>(im.size()), w = static_cast<int>(im[0].size()), q = w / 4;
    Bytes b;
    for (int r = 0; r < h; ++r)
        for (int p = 0; p < 4; ++p)
            for (int x = 0; x < q; ++x) b.push_back(static_cast<uint8_t>(im[r][x * 4 + p]));
    return b;
}

// Decodes the part at entry i of lib and compares with im (transparent
// pixels: seen = 0)
static bool same(dax::ByteSource& s, const hlib::Library& lib, int i, const Img& im, int kind)
{
    uint32_t pos, len;
    hlib::Part p;
    if (!hlib::entry(s, lib, i, pos, len) || !hlib::read_part(s, pos, len, p)) return false;
    if (p.kind != kind || p.width() != static_cast<int>(im[0].size()) || p.height != im.size()) return false;
    hlib::Pixels px(s, p);
    if (!px.ok()) return false;
    std::vector<uint8_t> row(p.width()), seen(p.width());
    for (int r = 0; r < p.height; ++r) {
        if (!px.row(row.data(), seen.data())) return false;
        for (int x = 0; x < p.width(); ++x) {
            const int v = im[r][x];
            if (v < 0 ? seen[x] != 0 : (seen[x] != 1 || row[x] != v)) return false;
        }
    }
    return !px.row(row.data());     // (no rows past the height)
}

static void test_library()
{
    // A data library with a directory, ids not in entry order
    Bytes a = {1, 2, 3}, b = {4, 5, 6, 7, 8}, c = {9};
    std::vector<std::pair<int, int>> dir = {{40, 2}, {7, 1}, {300, 3}};
    Bytes f = library("DATA", {a, b, c}, &dir);
    dax::MemorySource s(f.data(), static_cast<uint32_t>(f.size()));
    hlib::Library lib;
    CHECK(hlib::open(s, 0, s.size(), lib) == hlib::Status::Ok);
    CHECK(lib.count == 4 && lib.directory && strcmp(lib.kind, "DATA") == 0 && lib.first_item() == 1);
    CHECK(hlib::dir_count(s, lib) == 3);
    uint16_t id, index;
    CHECK(hlib::dir_item(s, lib, 2, id, index) && id == 300 && index == 3);
    CHECK(!hlib::dir_item(s, lib, 3, id, index));
    CHECK(hlib::find(s, lib, 7) == 1 && hlib::find(s, lib, 40) == 2 && hlib::find(s, lib, 300) == 3);
    CHECK(hlib::find(s, lib, 8) == -1);
    CHECK(hlib::id_of(s, lib, 2) == 40 && hlib::id_of(s, lib, 0) == -1);
    uint32_t pos, len;
    CHECK(hlib::entry(s, lib, 2, pos, len) && len == 5 && f[pos] == 4);
    CHECK(!hlib::entry(s, lib, 4, pos, len) && !hlib::entry(s, lib, -1, pos, len));
    CHECK(!hlib::is_library(s, pos, len));
    // By id in one call, then streamed like a DAX block
    CHECK(hlib::find_entry(s, 40, pos, len) && len == 5);
    hlib::EntryReader r(s, pos, len);
    CHECK(r.next() == 4 && r.skip(2) == 2);
    uint8_t buf[8];
    CHECK(r.read(buf, sizeof buf) == 2 && buf[0] == 7 && buf[1] == 8 && r.next() == -1 && r.produced() == 5 && !r.error());
    CHECK(!hlib::find_entry(s, 41, pos, len));
    // No directory: ids are the entries' indexes
    Bytes g = library("DIG4", {a, b});
    dax::MemorySource s2(g.data(), static_cast<uint32_t>(g.size()));
    CHECK(hlib::open(s2, 0, s2.size(), lib) == hlib::Status::Ok && !lib.directory && lib.first_item() == 0);
    CHECK(hlib::find(s2, lib, 1) == 1 && hlib::find(s2, lib, 2) == -1 && hlib::id_of(s2, lib, 1) == 1);
    // Damaged ones
    Bytes bad = f;
    bad[0] = 'X';
    dax::MemorySource s3(bad.data(), static_cast<uint32_t>(bad.size()));
    CHECK(hlib::open(s3, 0, s3.size(), lib) == hlib::Status::NotHlib);
    bad = f;
    bad[4] = 0xFF;    // size past the file
    dax::MemorySource s4(bad.data(), static_cast<uint32_t>(bad.size()));
    CHECK(hlib::open(s4, 0, s4.size(), lib) == hlib::Status::BadOffsets);
    bad = f;
    bad[16 + 8] = 0xF0;    // entry 2's offset past the library
    bad[16 + 9] = 0xFF;
    dax::MemorySource s5(bad.data(), static_cast<uint32_t>(bad.size()));
    CHECK(hlib::open(s5, 0, s5.size(), lib) == hlib::Status::Ok);
    CHECK(!hlib::entry(s5, lib, 1, pos, len) && !hlib::entry(s5, lib, 2, pos, len) && hlib::entry(s5, lib, 3, pos, len));
    dax::MemorySource s6(f.data(), 10);
    CHECK(hlib::open(s6, 0, s6.size(), lib) == hlib::Status::NotHlib);
}

static void test_pictures()
{
    std::mt19937 rng(1992);
    // A picture library: colours, then one part of every kind the decoder knows
    Bytes colours = {5, 0};
    put16(colours, 0x20);     // first
    put16(colours, 3);        // count
    colours.push_back(1);     // one colour cycle
    colours.push_back(hlib::kVgaColours);
    const uint8_t rgb[9] = {10, 20, 30, 40, 50, 60, 255, 0, 128};
    colours.insert(colours.end(), rgb, rgb + 9);
    colours.insert(colours.end(), {1, 2, 3, 4});   // the cycle
    colours.push_back(0x9C);                       // EGA: 9, 12, 5
    colours.push_back(0x50);

    const Img planar = random_img(rng, 24, 9, 0, 256);
    Bytes p10 = part_header(9, 3, -2, 6, hlib::kVgaPlanar);
    Bytes v = vga_planes(planar);
    p10.insert(p10.end(), v.begin(), v.end());

    const Img keyed = random_img(rng, 16, 7, 30, 255);
    Bytes p15 = part_header(7, 0, 0, 4, hlib::kVgaKeyed);
    v = vga_planes(keyed);
    p15.insert(p15.end(), v.begin(), v.end());

    Img masked = random_img(rng, 8, 5, 40, 256);
    Bytes p11 = part_header(5, 0, 0, 2, hlib::kVgaMasked);
    {
        Img m = masked;
        for (auto& row : m) for (int& x : row) x = x < 0 ? 0xFF : 0;
        v = vga_planes(m);
        p11.insert(p11.end(), v.begin(), v.end());
        Img px = masked;
        for (auto& row : px) for (int& x : row) if (x < 0) x = static_cast<int>(rng() % 256);   // (hidden pixels: anything)
        v = vga_planes(px);
        p11.insert(p11.end(), v.begin(), v.end());
    }

    // RLE: long runs and literals, a run crossing rows
    Img rimg(6, std::vector<int>(40));
    for (int r = 0; r < 6; ++r)
        for (int x = 0; x < 40; ++x) rimg[r][x] = r < 3 ? 7 : (r == 3 ? static_cast<int>(rng() % 256) : (x < 20 ? 1 : 2));
    Bytes p12 = part_header(6, 0, 0, 10, hlib::kVgaRle);
    v = rle(vga_rows(rimg));
    p12.insert(p12.end(), v.begin(), v.end());
    pad(p12);

    const Img sprite = random_img(rng, 64, 11, 55, 255);
    Bytes p17 = part_header(11, -40, -7, 16, hlib::kVgaSprite);
    v = vga_sprite(sprite);
    p17.insert(p17.end(), v.begin(), v.end());
    pad(p17);
    // a sprite whose rows are empty at the start, long skips
    Img sprite2(5, std::vector<int>(320, -1));
    sprite2[3][317] = 9;
    sprite2[4][0] = 1;
    sprite2[4][319] = 2;
    Bytes p17b = part_header(5, 0, 0, 80, hlib::kVgaSprite);
    v = vga_sprite(sprite2);
    p17b.insert(p17b.end(), v.begin(), v.end());

    const Img ega = random_img(rng, 16, 6, 0, 16);
    Bytes p00 = part_header(6, 0, 0, 2, hlib::kEgaPlanar);
    v = ega_planes(ega, false);
    p00.insert(p00.end(), v.begin(), v.end());

    const Img ega_m = random_img(rng, 24, 4, 35, 16);
    Bytes p01 = part_header(4, 0, 0, 3, hlib::kEgaMasked);
    v = ega_planes(ega_m, true);
    p01.insert(p01.end(), v.begin(), v.end());

    Img ega_r(5, std::vector<int>(16, 3));
    ega_r[2][5] = 12;
    Bytes pd2 = part_header(5, 0, 0, 2, hlib::kEgaRle);
    v = rle(ega_rows(ega_r));
    pd2.insert(pd2.end(), v.begin(), v.end());

    Bytes unknown = part_header(4, 0, 0, 4, hlib::kShapes);
    unknown.insert(unknown.end(), {1, 2, 3, 4, 5, 6});

    Bytes pic = library("TILE", {colours, p10, p15, p11, p12, p17, p17b, p00, p01, pd2, unknown});
    std::vector<std::pair<int, int>> dir = {{5, 1}};
    Bytes f = library("HLIB", {pic}, &dir);
    dax::MemorySource s(f.data(), static_cast<uint32_t>(f.size()));

    hlib::Library top, lib;
    CHECK(hlib::open(s, 0, s.size(), top) == hlib::Status::Ok && strcmp(top.kind, "HLIB") == 0);
    uint32_t pos, len;
    CHECK(hlib::entry(s, top, hlib::find(s, top, 5), pos, len) && hlib::is_library(s, pos, len));
    CHECK(hlib::open(s, pos, len, lib) == hlib::Status::Ok && lib.count == 11 && !lib.directory);

    // The colours
    CHECK(hlib::entry(s, lib, 0, pos, len));
    hlib::Colours c;
    CHECK(hlib::read_colours(s, pos, len, c) && c.vga && c.first == 0x20 && c.count == 3 && c.cycles == 1);
    uint8_t px[3];
    CHECK(hlib::colour_rgb(s, c, 2, px) && px[0] == 255 && px[1] == 0 && px[2] == 128);
    CHECK(!hlib::colour_rgb(s, c, 3, px));
    uint8_t eg[3];
    CHECK(hlib::ega_table(s, c, eg, 3) && eg[0] == 9 && eg[1] == 12 && eg[2] == 5);
    CHECK(!hlib::ega_table(s, c, eg, 2));
    hlib::Part part;
    CHECK(hlib::read_part(s, pos, len, part) && part.colours() && !part.drawable());
    // An EGA file's colours: 16 RGB triples only
    {
        Bytes e = {0, 0, 0x20, 0, 3, 0, 0, hlib::kEgaColours};
        for (int i = 0; i < 48; ++i) e.push_back(static_cast<uint8_t>(i));
        dax::MemorySource es(e.data(), static_cast<uint32_t>(e.size()));
        hlib::Colours ec;
        CHECK(hlib::read_colours(es, 0, static_cast<uint32_t>(e.size()), ec) && !ec.vga && ec.rgb_count() == 16);
        CHECK(hlib::colour_rgb(es, ec, 15, px) && px[0] == 45 && !hlib::ega_table(es, ec, eg, 3));
        CHECK(!hlib::read_colours(es, 0, 40, ec));
    }

    // Every kind decodes to its picture
    CHECK(same(s, lib, 1, planar, hlib::kVgaPlanar));
    CHECK(hlib::entry(s, lib, 1, pos, len) && hlib::read_part(s, pos, len, part) && part.x == 3 && part.y == -2 && !part.ega());
    CHECK(same(s, lib, 2, keyed, hlib::kVgaKeyed));
    CHECK(same(s, lib, 3, masked, hlib::kVgaMasked));
    CHECK(same(s, lib, 4, rimg, hlib::kVgaRle));
    CHECK(same(s, lib, 5, sprite, hlib::kVgaSprite));
    CHECK(hlib::entry(s, lib, 5, pos, len) && hlib::read_part(s, pos, len, part) && part.x == -40 && part.y == -7);
    CHECK(same(s, lib, 6, sprite2, hlib::kVgaSprite));
    CHECK(same(s, lib, 7, ega, hlib::kEgaPlanar));
    CHECK(hlib::entry(s, lib, 7, pos, len) && hlib::read_part(s, pos, len, part) && part.ega() && part.width() == 16);
    CHECK(same(s, lib, 8, ega_m, hlib::kEgaMasked));
    CHECK(same(s, lib, 9, ega_r, hlib::kEgaRle));
    CHECK(hlib::entry(s, lib, 10, pos, len) && hlib::read_part(s, pos, len, part) && !part.drawable());
    {
        hlib::Pixels nope(s, part);
        uint8_t row[16];
        CHECK(!nope.ok() && !nope.row(row));
    }

    // Short data: the decoder stops instead of reading past the part
    {
        Bytes cut = part_header(9, 0, 0, 6, hlib::kVgaPlanar);
        cut.insert(cut.end(), 100, 1);           // (216 needed)
        dax::MemorySource cs(cut.data(), static_cast<uint32_t>(cut.size()));
        hlib::Part cp;
        CHECK(hlib::read_part(cs, 0, static_cast<uint32_t>(cut.size()), cp));
        hlib::Pixels cpx(cs, cp);
        CHECK(!cpx.ok());
        Bytes cut2 = p17;
        cut2.resize(cut2.size() / 2);
        dax::MemorySource cs2(cut2.data(), static_cast<uint32_t>(cut2.size()));
        CHECK(hlib::read_part(cs2, 0, static_cast<uint32_t>(cut2.size()), cp));
        hlib::Pixels cpx2(cs2, cp);
        CHECK(!cpx2.ok());                       // (planes 1-3 not found)
        Bytes cut3 = p12;
        cut3.resize(20);
        dax::MemorySource cs3(cut3.data(), static_cast<uint32_t>(cut3.size()));
        CHECK(hlib::read_part(cs3, 0, static_cast<uint32_t>(cut3.size()), cp));
        hlib::Pixels cpx3(cs3, cp);
        std::vector<uint8_t> row(40);
        bool all = cpx3.ok();
        for (int r = 0; r < 6 && all; ++r) all = cpx3.row(row.data());
        CHECK(!all);
        // Too wide for the decoder's row buffers
        Bytes wide = part_header(1, 0, 0, 81, hlib::kVgaPlanar);
        dax::MemorySource ws(wide.data(), static_cast<uint32_t>(wide.size()));
        CHECK(hlib::read_part(ws, 0, static_cast<uint32_t>(wide.size()), cp) && !cp.drawable());
    }
}

// A VGA colour entry: entries first.. with these RGBs and EGA colours
static Bytes vga_colours(int first, const std::vector<int>& ega)
{
    Bytes c = {5, 0};
    put16(c, first);
    put16(c, static_cast<int>(ega.size()));
    c.push_back(0);
    c.push_back(hlib::kVgaColours);
    for (size_t i = 0; i < ega.size(); ++i) { c.push_back(static_cast<uint8_t>(10 * i)); c.push_back(7); c.push_back(255); }
    for (size_t i = 0; i < ega.size(); i += 2)
        c.push_back(static_cast<uint8_t>(ega[i] << 4 | (i + 1 < ega.size() ? ega[i + 1] : 0)));
    return c;
}

// A solid VGA part (kind 0x15) of one colour, its place (y, x negated)
static Bytes solid(int w, int h, int colour, int dy = 0, int dx = 0)
{
    Bytes p = part_header(h, -dy, -dx, w / 4, hlib::kVgaKeyed);
    Img im(h, std::vector<int>(w, colour));
    Bytes v = vga_planes(im);
    p.insert(p.end(), v.begin(), v.end());
    return p;
}

// Drawing HLIB parts (hlpic) and the 3D view / area map (qview) - a
// synthetic wall set, backdrop, map and area map tiles
static void test_view()
{
    // hlpic: a part at its place, through the EGA table, transparent pixels kept, clipped
    {
        Bytes cols = vga_colours(0x40, {9, 12});
        Bytes p = part_header(2, -3, -5, 1, hlib::kVgaKeyed);      // at (5, 3) from its anchor
        const uint8_t px[8] = {0x40, 0x41, 0xFF, 0x40, 0x41, 0x41, 0x40, 0xFF};   // plane by plane, 2 rows
        // (plane 0: rows 0, 1 = 0x40, 0x41; plane 1: 0xFF, 0x40; plane 2: 0x41, 0x41; plane 3: 0x40, 0xFF)
        p.insert(p.end(), px, px + 8);
        Bytes lib = library("TILE", {cols, p});
        dax::MemorySource s(lib.data(), static_cast<uint32_t>(lib.size()));
        static uint8_t mem[320 * 200];
        pic::Canvas c{mem, 320, 200, 4};
        c.clear(1);
        static hlpic::Colours col;
        col.reset();
        CHECK(col.ega[0x47] == 7);
        static pic::VgaPalette pal;
        CHECK(hlpic::draw_picture_part(s, 0, static_cast<uint32_t>(lib.size()), 1, col, &pal, c, 10, 20));
        CHECK(col.ega[0x40] == 9 && col.ega[0x41] == 12 && pal.first == 0x40 && pal.count == 2 && pal.rgb[0x41].r == 10);
        CHECK(c.get(15, 23) == 9 && c.get(16, 23) == 1 && c.get(17, 23) == 12 && c.get(18, 23) == 9);   // row 0 (plane 1 transparent)
        CHECK(c.get(15, 24) == 12 && c.get(16, 24) == 9 && c.get(18, 24) == 1);                      // row 1 (plane 3 transparent)
        c.clear(1);
        uint32_t pos, len;
        hlib::Library l;
        hlib::Part part;
        CHECK(hlib::open(s, 0, s.size(), l) == hlib::Status::Ok && hlib::entry(s, l, 1, pos, len) && hlib::read_part(s, pos, len, part));
        CHECK(hlpic::place_x(part, 10) == 15 && hlpic::place_y(part, 20) == 23);
        CHECK(hlpic::draw(s, part, col, c, 15, 23, hlpic::Clip{0, 0, 17, 24}));   // only (15, 23) and (16, 23) inside
        CHECK(c.get(15, 23) == 9 && c.get(17, 23) == 1 && c.get(15, 24) == 1);
        // a VGA canvas keeps the palette entries
        static uint8_t mem8[320 * 200];
        pic::Canvas v{mem8, 320, 200, 8};
        v.vga = true;
        v.clear(0);
        CHECK(hlpic::draw(s, part, col, v, 0, 0) && v.get(0, 0) == 0x40 && v.get(2, 0) == 0x41 && v.get(1, 0) == 0);
    }

    // qview's parts: pieces 0 / 1 ten slots, 2-4 nine (no second 8 x 8)
    CHECK(qview::part_of(0, qview::kF3) == 1 && qview::part_of(0, qview::kR0) == 10 && qview::part_of(1, qview::kF1) == 18);
    CHECK(qview::part_of(2, qview::kF3) == 21 && qview::part_of(2, qview::kF3b) == -1 && qview::part_of(2, qview::kL2) == 22);
    CHECK(qview::part_of(4, qview::kR0) == 47 && qview::part_of(5, 0) == -1);

    // A wall set: every slot a solid part of its own colour (entry 0x20 + piece);
    // piece 1's f1 smaller, placed in its slot
    std::vector<Bytes> parts = {vga_colours(0x20, {4, 14, 2, 3, 5})};
    static const int kSize[10][2] = {{8, 8}, {8, 8}, {8, 24}, {8, 24}, {24, 24}, {16, 56}, {16, 56}, {56, 56}, {16, 88}, {16, 88}};
    for (int piece = 0; piece < 5; ++piece)
        for (int slot = 0; slot < 10; ++slot) {
            if (piece >= 2 && slot == 1) continue;
            if (piece == 1 && slot == qview::kF1) parts.push_back(solid(8, 4, 0x21, 50, 24));
            else parts.push_back(solid(kSize[slot][0], kSize[slot][1], 0x20 + piece));
        }
    CHECK(parts.size() == 48);
    Bytes set = library("TILE", parts);
    std::vector<std::pair<int, int>> sdir = {{12, 1}};
    Bytes walls = library("HLIB", {set}, &sdir);
    // The backdrop: BACK id 4, 88 x 88 of entry 0x90 (EGA 6)
    Bytes bpic = library("TILE", {vga_colours(0x90, {6}), solid(88, 88, 0x90)});
    std::vector<std::pair<int, int>> bdir = {{4, 1}};
    Bytes backs = library("HLIB", {bpic}, &bdir);
    // A 12 x 9 map: sets (12, none, none); at (3, 4): north wall type 1 (piece 0),
    // east type 2 (piece 1); (3, 3) - one ahead - its west wall type 3
    const int mw = 12, mh = 9, n = mw * mh;
    Bytes g(geo::kHlibHeader + 4 * n, 0);
    g[0] = mw, g[1] = mh;
    g[2] = 12, g[3] = 0xFF, g[4] = 0xFF;
    g[geo::kHlibHeader + 3 + 4 * mw] = 0x12;               // plane 0: north 1, east 2
    g[geo::kHlibHeader + n + 3 + 3 * mw] = 0x03;           // plane 1: west 3
    dax::MemorySource gs(g.data(), static_cast<uint32_t>(g.size()));
    static geo::Map m;
    CHECK(geo::load_hlib(gs, 0, static_cast<uint32_t>(g.size()), m));
    dax::MemorySource ws(walls.data(), static_cast<uint32_t>(walls.size())), bs(backs.data(), static_cast<uint32_t>(backs.size()));
    qview::Libs libs;
    libs.walls[1] = &ws;          // (found in the second library: the first is null)
    libs.back = &bs;
    static uint8_t mem[320 * 200];
    pic::Canvas c{mem, 320, 200, 4};
    c.clear(1);
    static hlpic::Colours col;
    col.reset();
    const qview::Look look = qview::look_of(m, 4);
    CHECK(look.sets[0] == 12 && look.sets[1] == qview::kNoSet && look.back == 4);
    // Facing north from (3, 4): the north wall is the f1 front (piece 0: EGA 4),
    // the east wall the right near side (piece 1: EGA 14); the backdrop elsewhere
    CHECK(qview::draw(c, libs, m, 3, 4, geo::North, look, col));
    CHECK(c.get(24 + 40, 24 + 40) == 4 && c.get(24 + 80, 24 + 40) == 14 && c.get(24 + 5, 24 + 5) == 6);
    CHECK(c.get(23, 30) == 1 && c.get(24 + 88, 30) == 1);    // nothing outside the box
    // Facing west: the north wall is the right side (piece 0 r0: EGA 4), the
    // square ahead (2, 4) open - its front is nothing; the east wall behind
    c.clear(1);
    CHECK(qview::draw(c, libs, m, 3, 4, geo::West, look, col));
    CHECK(c.get(24 + 80, 24 + 40) == 4 && c.get(24 + 40, 24 + 40) == 6 && c.get(24 + 5, 24 + 40) == 6);
    // Facing south from (3, 2): (3, 3) one ahead - its west wall is the left
    // side 1 on (piece 2: EGA 2, at 56..72 - south's right is west); the north
    // wall of (3, 4) two ahead... not ours (south faces its south side)
    c.clear(1);
    CHECK(qview::draw(c, libs, m, 3, 2, geo::South, look, col));
    CHECK(c.get(24 + 60, 24 + 40) == 2 && c.get(24 + 20, 24 + 40) == 6);
    // Facing east from (3, 4): the north wall on the left (l0, EGA 4), the east
    // wall ahead is piece 1's f1: a small part placed in its slot (24, 50)
    c.clear(1);
    CHECK(qview::draw(c, libs, m, 3, 4, geo::East, look, col));
    CHECK(c.get(24 + 5, 24 + 40) == 4 && c.get(24 + 16 + 24, 24 + 16 + 50) == 14 && c.get(24 + 40, 24 + 40) == 6);
    // No sets found: the backdrop only; no backdrop: black
    qview::Look none;
    none.sets[0] = 99;
    c.clear(1);
    CHECK(!qview::draw(c, libs, m, 3, 4, geo::North, none, col) && c.get(24 + 40, 24 + 40) == 0);

    // The area map: TOPVIEW's tiles 1-16 (wall bits) and arrows 17-20
    std::vector<Bytes> tiles = {vga_colours(0x30, {0, 6, 14})};
    for (int t = 1; t <= 20; ++t) tiles.push_back(solid(8, 8, t <= 16 ? (t == 1 ? 0x30 : 0x31) : 0x32));
    Bytes tv = library("TILE", tiles);
    dax::MemorySource ts(tv.data(), static_cast<uint32_t>(tv.size()));
    libs.topview = &ts;
    c.clear(1);
    CHECK(qview::draw_area_map(c, libs, m, 3, 4, geo::North, col));
    // the window: x 0-10, y 0-8 (the map is 9 high); the party at (3, 4): the arrow
    CHECK(c.get(24 + 3 * 8 + 2, 24 + 4 * 8 + 2) == 14 && c.get(24 + 3 * 8 + 2, 24 + 3 * 8 + 2) == 6);
    CHECK(c.get(24 + 1, 24 + 1) == 0 && c.get(24 + 1, 24 + 9 * 8 + 1) == 0);    // open squares; below the map: black
    // a party far right: the window moves (x 1-11)
    c.clear(1);
    CHECK(qview::draw_area_map(c, libs, m, 11, 4, geo::East, col));
    CHECK(c.get(24 + 10 * 8 + 2, 24 + 4 * 8 + 2) == 14 && c.get(24 + 2 * 8 + 2, 24 + 4 * 8 + 2) == 6);
    geo::release(m);
}

int main()
{
    test_library();
    test_pictures();
    test_view();
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("test_hlib: all passed\n");
    return 0;
}

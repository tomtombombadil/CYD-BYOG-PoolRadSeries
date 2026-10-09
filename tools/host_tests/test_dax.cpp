// Host tests for the engine's file-format code (no game files needed: the
// tests build their own DAX files).
// CI builds it with g++ -std=c++17 -Wall -Werror -fsanitize=address,undefined
// -Isrc tools/host_tests/test_dax.cpp src/engine/*.cpp (see build.yml).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <tuple>
#include <vector>

#include "engine/dax.h"
#include "engine/ecl.h"
#include "engine/ecl_vm.h"
#include "engine/exepack.h"
#include "engine/geo.h"
#include "engine/icon.h"
#include "engine/inflate.h"
#include "engine/journal.h"
#include "engine/png.h"
#include "inflate_vectors.h"
#include "engine/layout.h"
#include "engine/printcalls.h"
#include "engine/profile.h"
#include "engine/text.h"
#include "engine/view3d.h"
#include "engine/font.h"
#include "engine/games.h"
#include "engine/picture.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

using Bytes = std::vector<uint8_t>;

static void put16(Bytes& b, uint32_t v) { b.push_back(v & 0xFF); b.push_back((v >> 8) & 0xFF); }
static void put32(Bytes& b, uint32_t v) { put16(b, v & 0xFFFF); put16(b, v >> 16); }

// Builds a DAX file holding the given raw blocks (compressed with dax::compress).
static Bytes make_dax(const std::vector<std::pair<uint8_t, Bytes>>& blocks)
{
    Bytes index, data;
    for (const auto& [id, raw] : blocks) {
        Bytes comp(raw.size() + raw.size() / 128 + 2);
        const size_t n = dax::compress(raw.data(), raw.size(), comp.data());
        comp.resize(n);
        index.push_back(id);
        put32(index, static_cast<uint32_t>(data.size()));
        put16(index, static_cast<uint32_t>(raw.size()));
        put16(index, static_cast<uint32_t>(comp.size()));
        data.insert(data.end(), comp.begin(), comp.end());
    }
    Bytes file;
    put16(file, static_cast<uint32_t>(index.size()));
    file.insert(file.end(), index.begin(), index.end());
    file.insert(file.end(), data.begin(), data.end());
    return file;
}

static void test_rle_known_bytes()
{
    // Hand-made stream: literal "AB", run of 4 x 'C', literal "D"
    const uint8_t comp[] = {0x01, 'A', 'B', 0xFC, 'C', 0x00, 'D'};
    Bytes file;
    put16(file, 9);
    file.push_back(7);                 // id
    put32(file, 0);
    put16(file, 7);                    // raw
    put16(file, sizeof comp);          // comp
    file.insert(file.end(), comp, comp + sizeof comp);

    dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    CHECK(dax::read_index(src, idx) == dax::Status::Ok);
    CHECK(idx.count == 1);
    const dax::Entry* e = idx.find(7);
    CHECK(e != nullptr);
    if (!e) return;
    uint8_t out[8] = {};
    CHECK(dax::load_block(src, idx, *e, out) == 7);
    CHECK(memcmp(out, "ABCCCCD", 7) == 0);
}

static void test_round_trip()
{
    std::mt19937 rng(1234);
    std::vector<std::pair<uint8_t, Bytes>> blocks;
    for (int b = 0; b < 40; ++b) {
        Bytes raw(rng() % 3000 + 1);
        // Mix of runs and noise, like real picture data
        for (size_t i = 0; i < raw.size();) {
            if (rng() % 2) {
                const size_t run = rng() % 200 + 1;
                const uint8_t v = static_cast<uint8_t>(rng());
                for (size_t k = 0; k < run && i < raw.size(); ++k) raw[i++] = v;
            } else {
                raw[i++] = static_cast<uint8_t>(rng());
            }
        }
        blocks.push_back({static_cast<uint8_t>(b * 3 + 1), raw});
    }
    const Bytes file = make_dax(blocks);
    dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    CHECK(dax::read_index(src, idx) == dax::Status::Ok);
    CHECK(idx.count == 40);
    for (const auto& [id, raw] : blocks) {
        const dax::Entry* e = idx.find(id);
        CHECK(e != nullptr);
        if (!e) continue;
        CHECK(e->raw_size == raw.size());
        Bytes out(raw.size());
        CHECK(dax::load_block(src, idx, *e, out.data()) == raw.size());
        CHECK(out == raw);
        // Streaming with skips gives the same bytes
        dax::RleReader r(src, idx, *e);
        const size_t half = raw.size() / 2;
        CHECK(r.skip(half) == half);
        for (size_t i = half; i < raw.size(); ++i) CHECK(r.next() == raw[i]);
        CHECK(r.next() == -1);
        CHECK(!r.error());
    }
    CHECK(idx.find(2) == nullptr);
}

static void test_bad_files()
{
    static dax::Index idx;
    const uint8_t tiny[] = {0x05};
    dax::MemorySource s1(tiny, sizeof tiny);
    CHECK(dax::read_index(s1, idx) == dax::Status::ReadError);

    const uint8_t not9[] = {0x05, 0x00, 1, 2, 3, 4, 5};
    dax::MemorySource s2(not9, sizeof not9);
    CHECK(dax::read_index(s2, idx) == dax::Status::BadIndex);

    // Index claims a block past the end of the file
    Bytes f;
    put16(f, 9);
    f.push_back(1);
    put32(f, 0);
    put16(f, 100);
    put16(f, 50);
    f.resize(f.size() + 10);
    dax::MemorySource s3(f.data(), static_cast<uint32_t>(f.size()));
    CHECK(dax::read_index(s3, idx) == dax::Status::BlockOutside);

    // A block whose compressed data ends early: decoding stops, no overrun
    const uint8_t comp[] = {0x7F, 'x'};   // promises 128 literal bytes, has 1
    Bytes g;
    put16(g, 9);
    g.push_back(1);
    put32(g, 0);
    put16(g, 128);
    put16(g, sizeof comp);
    g.insert(g.end(), comp, comp + sizeof comp);
    dax::MemorySource s4(g.data(), static_cast<uint32_t>(g.size()));
    CHECK(dax::read_index(s4, idx) == dax::Status::Ok);
    uint8_t out[128];
    CHECK(dax::load_block(s4, idx, idx.entries[0], out) == 1);

    // Output never goes past raw_size even if the data says more
    const uint8_t comp2[] = {0x83, 'z'};  // run of 125 into a 10-byte block
    Bytes h;
    put16(h, 9);
    h.push_back(1);
    put32(h, 0);
    put16(h, 10);
    put16(h, sizeof comp2);
    h.insert(h.end(), comp2, comp2 + sizeof comp2);
    dax::MemorySource s5(h.data(), static_cast<uint32_t>(h.size()));
    CHECK(dax::read_index(s5, idx) == dax::Status::Ok);
    uint8_t out2[11] = {};
    CHECK(dax::load_block(s5, idx, idx.entries[0], out2) == 10);
    CHECK(out2[10] == 0);
}

static Bytes make_picture(int height, int cols, int frames, int x, int y)
{
    Bytes raw;
    put16(raw, height);
    put16(raw, cols);
    put16(raw, x);
    put16(raw, y);
    raw.push_back(static_cast<uint8_t>(frames));
    for (int i = 0; i < 8; ++i) raw.push_back(0);
    for (int f = 0; f < frames; ++f)
        for (int r = 0; r < height; ++r)
            for (int c = 0; c < cols * 4; ++c) {
                // pixel colour = (frame + row + column) % 16
                const int p0 = (f + r + c * 2) % 16, p1 = (f + r + c * 2 + 1) % 16;
                raw.push_back(static_cast<uint8_t>(p0 << 4 | p1));
            }
    return raw;
}

static void test_picture()
{
    const Bytes raw = make_picture(10, 3, 2, 1, 2);
    const Bytes file = make_dax({{5, raw}});
    dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    CHECK(dax::read_index(src, idx) == dax::Status::Ok);
    const dax::Entry& e = idx.entries[0];

    static uint8_t px[pic::kScreenW * pic::kScreenH];
    pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
    for (int frame = 0; frame < 2; ++frame) {
        c.clear(pic::kTransparent);
        dax::RleReader r(src, idx, e);
        uint8_t hdr[pic::kHeaderSize];
        CHECK(r.read(hdr, sizeof hdr) == sizeof hdr);
        pic::Header h;
        uint32_t extra = 99;
        CHECK(pic::parse_header(hdr, e.raw_size, h, &extra));
        CHECK(extra == 0);
        CHECK(h.height == 10 && h.width_px() == 24 && h.frames == 2 && h.x_cell == 1 && h.y_cell == 2);
        CHECK(pic::draw(r, h, frame, c, 100, 50));
        bool ok = true;
        for (int row = 0; row < 10; ++row)
            for (int col = 0; col < 24; ++col)
                if (px[(50 + row) * pic::kScreenW + 100 + col] != (frame + row + col) % 16) ok = false;
        CHECK(ok);
        CHECK(px[49 * pic::kScreenW + 100] == pic::kTransparent);
        CHECK(px[50 * pic::kScreenW + 124] == pic::kTransparent);
    }

    // Clipping at every edge, and a mask colour
    c.clear(pic::kTransparent);
    {
        dax::RleReader r(src, idx, e);
        uint8_t hdr[pic::kHeaderSize];
        r.read(hdr, sizeof hdr);
        pic::Header h;
        CHECK(pic::parse_header(hdr, e.raw_size, h));
        CHECK(pic::draw(r, h, 0, c, -5, -3, 0));
        CHECK(px[0] == (3 + 5) % 16);
        // colour 0 (row 3, col 13 -> (3+13)%16 = 0) masked out: row 0 col 8 here
        CHECK(px[8] == pic::kTransparent);
    }
    c.clear(pic::kTransparent);
    {
        dax::RleReader r(src, idx, e);
        uint8_t hdr[pic::kHeaderSize];
        r.read(hdr, sizeof hdr);
        pic::Header h;
        pic::parse_header(hdr, e.raw_size, h);
        CHECK(pic::draw(r, h, 1, c, 310, 195));
        CHECK(px[199 * pic::kScreenW + 319] == (1 + 4 + 9) % 16);
    }

    // Headers that can't be pictures
    pic::Header h;
    Bytes bad = make_picture(10, 3, 2, 0, 0);
    CHECK(!pic::parse_header(bad.data(), static_cast<uint32_t>(bad.size() - 1), h));  // data short
    bad[8] = 0;
    CHECK(!pic::parse_header(bad.data(), static_cast<uint32_t>(bad.size()), h));      // no frames
    Bytes wide = make_picture(1, 41, 1, 0, 0);
    CHECK(!pic::parse_header(wide.data(), static_cast<uint32_t>(wide.size()), h));    // wider than the screen
}

// Animation block: 3 frames of 8x2 px (1 column, 2 rows = 8 bytes each);
// frames 1-2 stored XORed with frame 0 except the last byte.
static void test_anim()
{
    const uint8_t f0[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    const uint8_t f1[8] = {0x11, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0x77};
    const uint8_t f2[8] = {0x12, 0x34, 0x00, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    const uint8_t* frames[3] = {f0, f1, f2};
    for (int xor_mode = 0; xor_mode < 2; ++xor_mode) {
        Bytes raw;
        raw.push_back(3);
        for (int f = 0; f < 3; ++f) {
            put32(raw, 10 + f);    // delay
            put16(raw, 2);         // height
            put16(raw, 1);         // width (columns)
            put16(raw, 3);         // x
            put16(raw, 4);         // y
            raw.push_back(0);      // unknown
            for (int i = 0; i < 8; ++i) raw.push_back(0xEE);
            for (int i = 0; i < 8; ++i) {
                uint8_t b = frames[f][i];
                if (xor_mode && f > 0 && i < 7) b ^= f0[i];
                raw.push_back(b);
            }
        }
        const Bytes file = make_dax({{1, raw}});
        dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
        static dax::Index idx;
        CHECK(dax::read_index(src, idx) == dax::Status::Ok);
        const dax::Entry& e = idx.entries[0];
        static pic::Anim a;
        {
            dax::RleReader r(src, idx, e);
            CHECK(pic::parse_anim(r, e.raw_size, a));
        }
        CHECK(a.frames == 3 && a.delay[2] == 12 && a.frame[1].width_px() == 8 && a.frame[1].x_cell == 3);
        // A single-picture header must not accept it, nor an animation parse a picture
        uint8_t hdr[pic::kHeaderSize];
        { dax::RleReader r(src, idx, e); r.read(hdr, sizeof hdr); }
        pic::Header h;
        CHECK(!pic::parse_header(hdr, e.raw_size, h));
        static uint8_t px[pic::kScreenW * pic::kScreenH];
        pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
        for (int f = 0; f < 3; ++f) {
            c.clear(pic::kTransparent);
            CHECK(pic::draw_anim(src, idx, e, a, f, xor_mode == 1, c, 0, 0));
            bool ok = true;
            for (int row = 0; row < 2; ++row)
                for (int i = 0; i < 4; ++i) {
                    const uint8_t b = frames[f][row * 4 + i];
                    if (px[row * pic::kScreenW + i * 2] != (b >> 4) || px[row * pic::kScreenW + i * 2 + 1] != (b & 15)) ok = false;
                }
            CHECK(ok);
        }
    }
    // A picture block is not an animation
    const Bytes pr = make_picture(4, 2, 1, 0, 0);
    const Bytes file = make_dax({{2, pr}});
    dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    dax::read_index(src, idx);
    static pic::Anim a;
    dax::RleReader r(src, idx, idx.entries[0]);
    CHECK(!pic::parse_anim(r, idx.entries[0].raw_size, a));
}

// 256-colour picture (Pools of Darkness layout): 2 frames of 8x3 px,
// palette entries 40..43
static void test_vga()
{
    Bytes raw = {3, 1, 0, 0, 0, 0, 2, 0, 40, 3};
    const uint8_t pal[4][3] = {{63, 0, 0}, {0, 63, 0}, {0, 0, 63}, {21, 42, 63}};
    for (auto& p : pal) raw.insert(raw.end(), p, p + 3);
    raw.push_back(0x12);                     // EGA map, 4 nibbles
    raw.push_back(0x34);
    for (int i = 0; i < 4; ++i) raw.push_back(0);
    for (int f = 0; f < 2; ++f)
        for (int i = 0; i < 24; ++i) raw.push_back(static_cast<uint8_t>(40 + (i + f) % 4));
    const Bytes file = make_dax({{9, raw}});
    dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    CHECK(dax::read_index(src, idx) == dax::Status::Ok);
    const dax::Entry& e = idx.entries[0];
    pic::VgaHeader vh;
    CHECK(pic::parse_vga_header(raw.data(), e.raw_size, vh));
    CHECK(vh.height == 3 && vh.width_px() == 8 && vh.frames == 2 && vh.first == 40 && vh.count == 4);
    CHECK(!pic::parse_vga_header(raw.data(), e.raw_size - 1, vh));
    pic::Header h;
    CHECK(!pic::parse_header(raw.data(), e.raw_size, h));      // not an EGA picture
    static pic::Rgb rgb[256];
    {
        dax::RleReader r(src, idx, e);
        r.skip(pic::kVgaHeaderSize);
        CHECK(pic::read_vga_palette(r, vh, rgb));
    }
    CHECK(rgb[40].r == 255 && rgb[40].g == 0 && rgb[43].g == 170 && rgb[43].b == 255);
    static uint8_t px[pic::kScreenW * pic::kScreenH];
    pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
    c.clear(0);
    dax::RleReader r(src, idx, e);
    CHECK(pic::draw_vga(r, vh, 1, c, 10, 20));
    bool ok = true;
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 8; ++col)
            if (px[(20 + row) * pic::kScreenW + 10 + col] != 40 + (row * 8 + col + 1) % 4) ok = false;
    CHECK(ok);
}

// Font block: 177 glyphs; glyph g's rows are made-up patterns
static void test_font()
{
    Bytes raw(font::kBlockBytes);
    for (int g = 0; g < font::kGlyphs; ++g)
        for (int r = 0; r < 8; ++r) raw[g * 8 + r] = static_cast<uint8_t>((g * 7 + r * 13) & 0xFF);
    const Bytes file = make_dax({{200, Bytes(16, 1)}, {201, raw}});
    dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    CHECK(dax::read_index(src, idx) == dax::Status::Ok);
    static font::Font f;
    CHECK(font::load(src, idx, f) && f.loaded);
    CHECK(font::glyph_of('A') == 1 && font::glyph_of('a') == 1 && font::glyph_of(' ') == 32 &&
          font::glyph_of('0') == 48 && font::glyph_of('?') == 63);
    static uint8_t px[pic::kScreenW * pic::kScreenH];
    pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
    c.clear(9);
    CHECK(font::draw_text(c, f, "AB", 38, 24, 15, 0) == 40);
    CHECK(font::draw_text(c, f, "XYZ", 39, 0, 15, -1) == 40);    // clipped at the right edge
    bool ok = true;
    for (int g = 0; g < 2; ++g)
        for (int r = 0; r < 8; ++r)
            for (int b = 0; b < 8; ++b) {
                const bool ink = (raw[(1 + g) * 8 + r] >> (7 - b)) & 1;
                if (px[(192 + r) * pic::kScreenW + 304 + g * 8 + b] != (ink ? 15 : 0)) ok = false;
            }
    CHECK(ok);
    // transparent paper keeps what was there
    ok = true;
    for (int r = 0; r < 8; ++r)
        for (int b = 0; b < 8; ++b) {
            const bool ink = (raw[24 * 8 + r] >> (7 - b)) & 1;
            if (px[r * pic::kScreenW + 312 + b] != (ink ? 15 : 9)) ok = false;
        }
    CHECK(ok);
    // a file without a proper block 201
    const Bytes nofont = make_dax({{201, Bytes(100, 0)}});
    dax::MemorySource s2(nofont.data(), static_cast<uint32_t>(nofont.size()));
    CHECK(dax::read_index(s2, idx) == dax::Status::Ok);
    CHECK(!font::load(s2, idx, f));
}

static void test_games()
{
    using games::Game;
    CHECK(games::from_folder_name("POOLRAD") == Game::PoolOfRadiance);
    CHECK(games::from_folder_name("Pool of Radiance") == Game::PoolOfRadiance);
    CHECK(games::from_folder_name("Curse of the Azure Bonds") == Game::CurseOfTheAzureBonds);
    CHECK(games::from_folder_name("COAB") == Game::Unknown);
    CHECK(games::from_folder_name("cotab") == Game::CurseOfTheAzureBonds);
    CHECK(games::from_folder_name("SILVER") == Game::SecretOfTheSilverBlades);
    CHECK(games::from_folder_name("SECRET") == Game::SecretOfTheSilverBlades);
    CHECK(games::from_folder_name("CURSE") == Game::CurseOfTheAzureBonds);
    CHECK(games::from_folder_name("DARKNESS") == Game::PoolsOfDarkness);
    CHECK(games::from_folder_name("Secret_of_the_Silver_Blades") == Game::SecretOfTheSilverBlades);
    CHECK(games::from_folder_name("POOLDARK") == Game::PoolsOfDarkness);
    CHECK(games::from_folder_name("Pools of Darkness") == Game::PoolsOfDarkness);
    CHECK(games::from_folder_name("Hillsfar") == Game::Unknown);
    // The other Gold Box games: Tom's SD folder names and GOG's titles
    const struct { const char* name; Game g; } more[] = {
        {"CHAMPIONS", Game::ChampionsOfKrynn},
        {"Champions of Krynn", Game::ChampionsOfKrynn},
        {"DEATH", Game::DeathKnightsOfKrynn},
        {"Death Knights of Krynn", Game::DeathKnightsOfKrynn},
        {"QUEEN", Game::DarkQueenOfKrynn},
        {"The Dark Queen of Krynn", Game::DarkQueenOfKrynn},
        {"GATEWAY", Game::GatewayToTheSavageFrontier},
        {"Gateway to the Savage Frontier", Game::GatewayToTheSavageFrontier},
        {"TREASURE", Game::TreasuresOfTheSavageFrontier},
        {"Treasures of the Savage Frontier", Game::TreasuresOfTheSavageFrontier},
        {"UNLIMIT", Game::UnlimitedAdventures},
        {"Unlimited Adventures", Game::UnlimitedAdventures},
        {"FRUA", Game::UnlimitedAdventures},
    };
    for (const auto& m : more) CHECK(games::from_folder_name(m.name) == m.g);
    CHECK(games::main_series(Game::CurseOfTheAzureBonds));
    CHECK(!games::main_series(Game::DarkQueenOfKrynn));
    CHECK(!games::main_series(Game::Unknown));
    // Every game has its own list place, and the short titles are short
    for (int i = 1; i < games::kGameCount; ++i) {
        const Game g = static_cast<Game>(i);
        CHECK(strlen(games::short_title(g)) <= 18);
        CHECK(games::from_folder_name(games::folder_hint(g)) == g);
        CHECK(games::from_folder_name(games::title(g)) == g);
        for (int j = 1; j < i; ++j) CHECK(games::list_order(static_cast<Game>(j)) != games::list_order(g));
    }
    CHECK(games::list_order(Game::Unknown) > games::list_order(Game::UnlimitedAdventures));
}

// ---- EXEPACK ---------------------------------------------------------------
// A test packer: keeps image[0, prefix) as it is and packs the rest as fill
// runs (4+ equal bytes) and literal copies, the way the unpacker reads them
// (backwards). Header variant: 18 bytes with skip_len, or 16 without.
static Bytes make_exe(const Bytes& image, size_t prefix, bool hdr18, uint16_t skip = 1)
{
    Bytes packed(image.begin(), image.begin() + static_cast<long>(prefix));
    bool first = true;
    size_t i = prefix;
    while (i < image.size()) {
        size_t run = 1;
        while (i + run < image.size() && image[i + run] == image[i] && run < 0xFFFF) ++run;
        const uint8_t last = first ? 1 : 0;
        first = false;
        if (run >= 4) {
            packed.push_back(image[i]);
            put16(packed, static_cast<uint32_t>(run));    // low byte first going forwards
            packed.push_back(static_cast<uint8_t>(0xB0 | last));
            i += run;
        } else {
            size_t n = 0;
            while (i + n < image.size() && n < 0xFFFF) {
                size_t r2 = 1;
                while (i + n + r2 < image.size() && image[i + n + r2] == image[i + n]) ++r2;
                if (r2 >= 4) break;
                n += r2;
            }
            packed.insert(packed.end(), image.begin() + static_cast<long>(i), image.begin() + static_cast<long>(i + n));
            put16(packed, static_cast<uint32_t>(n));
            packed.push_back(static_cast<uint8_t>(0xB2 | last));
            i += n;
        }
    }
    while (packed.size() % 16) packed.push_back(0xFF);
    for (int k = 1; k < skip; ++k) packed.insert(packed.end(), 16, 0);
    const uint16_t cs = static_cast<uint16_t>(packed.size() / 16);

    Bytes exe;
    exe.push_back('M');
    exe.push_back('Z');
    put16(exe, 0);       // bytes in last page (not checked)
    put16(exe, 0);       // pages
    put16(exe, 0);       // relocations
    put16(exe, 2);       // header paragraphs
    put16(exe, 0);
    put16(exe, 0xFFFF);
    put16(exe, 0);       // ss
    put16(exe, 0);       // sp
    put16(exe, 0);       // checksum
    put16(exe, 18);      // ip: the stub after the 18-byte header
    put16(exe, cs);
    while (exe.size() < 32) exe.push_back(0);
    exe.insert(exe.end(), packed.begin(), packed.end());
    put16(exe, 0x2F);    // real ip
    put16(exe, 0);       // real cs
    put16(exe, 0);
    put16(exe, 0x200);
    put16(exe, 0x4000);  // real sp
    put16(exe, 0x1234);  // real ss
    put16(exe, static_cast<uint32_t>(image.size() / 16));
    if (hdr18) put16(exe, skip);
    exe.push_back('R');
    exe.push_back('B');
    for (int k = 0; k < 40; ++k) exe.push_back(0x90);    // "stub"
    return exe;
}

static Bytes test_image()
{
    // An unpacked prefix, then runs and literals; a multiple of 16 bytes
    Bytes img;
    std::mt19937 rng(7);
    for (int k = 0; k < 37; ++k) img.push_back(static_cast<uint8_t>(rng()));
    for (int block = 0; block < 30; ++block) {
        img.insert(img.end(), 40 + block, static_cast<uint8_t>(block * 9));
        for (int k = 0; k < 17; ++k) img.push_back(static_cast<uint8_t>(rng() | 1));
    }
    img.insert(img.end(), 300, 0);
    while (img.size() % 16) img.push_back(0x55);
    return img;
}

static void test_exepack()
{
    const Bytes img = test_image();
    for (int variant = 0; variant < 3; ++variant) {
        const Bytes exe = variant == 0 ? make_exe(img, 37, true) : variant == 1 ? make_exe(img, 37, false)
                                                                               : make_exe(img, 37, true, 3);
        CHECK(exe.size() < img.size());
        dax::MemorySource src(exe.data(), static_cast<uint32_t>(exe.size()));
        exepack::Info in;
        CHECK(exepack::parse(src, in) == exepack::Status::Ok);
        CHECK(in.image_size == img.size() && in.load_start == 32 && in.ip == 0x2F && in.ss == 0x1234 && in.sp == 0x4000);
        Bytes out(img.size());
        CHECK(exepack::read(src, in, 0, out.data(), out.size()) == exepack::Status::Ok);
        CHECK(out == img);
        // Any piece, including ones across the kept prefix and run edges
        bool ok = true;
        for (uint32_t pos = 0; pos + 50 <= img.size(); pos += 23) {
            uint8_t piece[50];
            if (exepack::read(src, in, pos, piece, sizeof piece) != exepack::Status::Ok ||
                memcmp(piece, img.data() + pos, sizeof piece) != 0)
                ok = false;
        }
        CHECK(ok);
        uint8_t one;
        CHECK(exepack::read(src, in, static_cast<uint32_t>(img.size()) - 1, &one, 1) == exepack::Status::Ok && one == img.back());
        CHECK(exepack::read(src, in, static_cast<uint32_t>(img.size()), &one, 1) == exepack::Status::BadData);
    }
    // Damaged: a command byte that isn't one
    Bytes bad = make_exe(img, 37, true);
    {
        dax::MemorySource src(bad.data(), static_cast<uint32_t>(bad.size()));
        exepack::Info in;
        CHECK(exepack::parse(src, in) == exepack::Status::Ok);
        uint32_t p = in.load_start + in.packed_end;
        while (bad[p - 1] == 0xFF) --p;
        bad[p - 1] = 0x42;
        uint8_t b;
        CHECK(exepack::read(src, in, 0, &b, 1) == exepack::Status::BadData);
    }
    // Not packed / not a program
    Bytes plain = make_exe(img, 37, true);
    plain[plain.size() - 41] = 'X';     // the "B" of "RB"
    dax::MemorySource s2(plain.data(), static_cast<uint32_t>(plain.size()));
    exepack::Info in;
    CHECK(exepack::parse(s2, in) == exepack::Status::NotPacked);
    const Bytes junk(100, 7);
    dax::MemorySource s3(junk.data(), static_cast<uint32_t>(junk.size()));
    CHECK(exepack::parse(s3, in) == exepack::Status::NotExe);
}

// ---- Screen frame ----------------------------------------------------------
static void test_layout()
{
    // Tiles: tile n, pixel (row r, column x) = (n + r + x) % 16 (make_picture)
    const Bytes file = make_dax({{201, Bytes(10, 0)}, {202, make_picture(8, 1, 40, 0, 0)}, {203, make_picture(8, 2, 40, 0, 0)}});
    dax::MemorySource dsrc(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    CHECK(dax::read_index(dsrc, idx) == dax::Status::Ok);
    static layout::Tiles t;
    CHECK(!layout::load_tiles(dsrc, idx, 203, t));     // 16 pixels wide
    CHECK(!layout::load_tiles(dsrc, idx, 201, t));
    CHECK(layout::load_tiles(dsrc, idx, 202, t) && t.loaded);
    CHECK(t.px[5][0] == 5 && t.px[5][9] == 7 && t.px[39][63] == (39 + 14) % 16);

    // A program whose data segment holds the tables: value = cell % 10
    // (view tables % 20)
    profile::Profile p{};
    p.data_base = 0x100;
    p.frame = {0x10, 0x40, 0x70, 0xA0, 0xC0, 0xE0, 0x100, 0x110, 0x120, 0x130, 0x140, 0x160, 0x180};
    Bytes img(0x400, 0);
    auto fill = [&](uint16_t at, int n, int mod) {
        for (int i = 0; i < n; ++i) img[p.data_base + at + i] = static_cast<uint8_t>(i % mod);
    };
    fill(0x10, 40, 10); fill(0x40, 40, 10); fill(0x70, 40, 10); fill(0xA0, 24, 10); fill(0xC0, 24, 10);
    fill(0xE0, 17, 10); fill(0x100, 15, 20); fill(0x110, 15, 20); fill(0x120, 15, 20); fill(0x130, 15, 20);
    fill(0x140, 23, 10); fill(0x160, 23, 10); fill(0x180, 23, 10);
    for (int k = 0; k < 0x100; ++k) img[k] = static_cast<uint8_t>(k * 3);     // code before DS
    const Bytes exe = make_exe(img, 0x100, true);     // code kept unpacked
    dax::MemorySource esrc(exe.data(), static_cast<uint32_t>(exe.size()));
    exepack::Info in;
    CHECK(exepack::parse(esrc, in) == exepack::Status::Ok);
    static layout::Tables tb;
    CHECK(layout::load_tables(esrc, in, p, tb) == layout::Status::Ok && tb.loaded);
    CHECK(tb.top[13] == 3 && tb.left[23] == 3 && tb.view_top[14] == 14 && tb.combat_right[22] == 2);

    static uint8_t px[pic::kScreenW * pic::kScreenH];
    pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
    auto at = [&](int x, int y) { return px[y * pic::kScreenW + x]; };
    // Expected pixel of tile n at (x, y) inside it; 13 is masked
    auto tp = [](int n, int x, int y) { return (n + y + x) % 16; };

    c.clear(9);
    layout::outer(c, tb, t);
    // top row, column 7: tile 30 + 7; pixel (2, 1)
    CHECK(at(7 * 8 + 2, 1) == tp(37, 2, 1));
    // left column, row 12 (value 2): tile 32
    CHECK(at(3, 12 * 8 + 4) == tp(32, 3, 4));
    // right column 39, row 5: tile 35
    CHECK(at(39 * 8 + 6, 5 * 8 + 0) == tp(35, 6, 0));
    // bottom row 23, column 21: tile 31
    CHECK(at(21 * 8 + 1, 23 * 8 + 1) == tp(31, 1, 1));
    // inside cleared, row 24 untouched
    CHECK(at(100, 100) == 0 && at(100, 24 * 8 + 3) == 9);
    // Pixels of colour 13 aren't drawn: top col 6 = tile 36, pixel (5, 4)
    // is (36 + 4 + 5) % 16 = 13
    CHECK(at(6 * 8 + 5, 4) == 9 && at(6 * 8 + 4, 4) == tp(36, 4, 4));

    c.clear(9);
    layout::explore(c, tb, t);
    CHECK(at(10 * 8 + 1, 16 * 8 + 1) == tp(30, 1, 1));            // bar row 16, col 10 (value 0)
    CHECK(at(16 * 8 + 2, 5 * 8 + 2) == tp(35, 2, 2));             // split col 16, row 5
    CHECK(at(9 * 8 + 0, 2 * 8 + 1) == tp(29, 0, 1));              // view top, col 9: 20 + 9
    CHECK(at(2 * 8 + 4, 12 * 8 + 0) == tp(32, 4, 0));             // view left, row 12: 20 + 12
    CHECK(at(14 * 8 + 0, 3 * 8 + 0) == tp(23, 0, 0));             // view right, row 3
    CHECK(at(8 * 8 + 4, 8 * 8 + 4) == 0);                         // the 3D view, cleared

    c.clear(9);
    layout::combat(c, tb, t);
    CHECK(at(22 * 8 + 1, 7 * 8 + 1) == tp(37, 1, 1));
    CHECK(at(5 * 8 + 1, 22 * 8 + 1) == tp(35, 1, 1));             // bottom bar on row 22
    CHECK(at(5, 23 * 8 + 2) == 0 && at(5, 24 * 8 + 2) == 9);

    // Values that can't be tiles: refused
    img[p.data_base + 0x10 + 4] = 10;
    const Bytes exe2 = make_exe(img, 0x100, true);
    dax::MemorySource e2(exe2.data(), static_cast<uint32_t>(exe2.size()));
    CHECK(exepack::parse(e2, in) == exepack::Status::Ok);
    CHECK(layout::load_tables(e2, in, p, tb) == layout::Status::BadTables && !tb.loaded);

    // The known release is found only by both sizes
    CHECK(profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432) != nullptr);
    CHECK(profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62400) == nullptr);
    CHECK(profile::find(games::Game::PoolOfRadiance, 57789, 62432) == nullptr);
    CHECK(profile::program_name(games::Game::CurseOfTheAzureBonds) != nullptr);
}

// ---- Text ------------------------------------------------------------------
// A font whose glyph g is a solid block with g's bits in row 0, so what was
// printed can be read back from the canvas.
static void make_test_font(font::Font& f)
{
    for (int g = 0; g < font::kGlyphs; ++g)
        for (int r = 0; r < 8; ++r) f.glyph[g][r] = r == 0 ? static_cast<uint8_t>(g) : 0xFF;
    f.loaded = true;
}

// The character printed at a cell: '.' if blank (all paper), '?' if unknown
static char cell_char(const pic::Canvas& c, int col, int row)
{
    const uint8_t* p = c.px + row * 8 * c.w + col * 8;
    if (p[c.w * 3] == 0) return '.';   // row 3 of the glyph is ink unless blank
    int g = 0;
    for (int b = 0; b < 8; ++b)
        if (p[b] != 0) g |= 0x80 >> b;
    if (g == 32) return ' ';
    if (g >= 1 && g <= 26) return static_cast<char>('A' + g - 1);
    if (g >= 33 && g < 64) return static_cast<char>(g);
    return '?';
}

static std::string row_text(const pic::Canvas& c, int row, int x0, int x1)
{
    std::string s;
    for (int x = x0; x <= x1; ++x) s += cell_char(c, x, row);
    return s;
}

static void test_text()
{
    static font::Font f;
    make_test_font(f);
    static uint8_t px[pic::kScreenW * pic::kScreenH];
    pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
    c.clear(7);

    // A 10 x 3 window at columns 2-11, rows 5-7
    const text::Region r{2, 5, 11, 7};
    text::Writer w;
    text::begin(w, c, "ONE TWO THREE, FOUR! FIVE SIX ABCDEFGHIJKLMN END", r, 15, true);
    CHECK(w.state == text::State::Writing);
    CHECK(c.px[5 * 8 * c.w + 2 * 8] == 0 && c.px[4 * 8 * c.w + 2 * 8] == 7);   // area cleared, not outside
    CHECK(text::step(w, c, f, 3) == text::State::Writing);
    CHECK(row_text(c, 5, 2, 11) == "ONE.......");
    CHECK(text::step(w, c, f, -1) == text::State::PageFull);
    CHECK(row_text(c, 5, 2, 11) == "ONE TWO ..");      // "THREE, " doesn't fit with its space
    CHECK(row_text(c, 6, 2, 11) == "THREE, ...");      // punctuation stays with its word
    CHECK(row_text(c, 7, 2, 11) == "FOUR! ....");      // "FIVE " would fit only without its space
    text::next_page(w, c);
    CHECK(row_text(c, 5, 2, 11) == "..........");
    CHECK(text::step(w, c, f, -1) == text::State::Done);
    CHECK(row_text(c, 5, 2, 11) == "FIVE SIX ."); // the leading space was dropped
    CHECK(row_text(c, 6, 2, 11) == "ABCDEFGHIJ");      // too long for a line: broken
    CHECK(row_text(c, 7, 2, 11) == "KLMN END..");
    CHECK(w.col == 10 && w.row == 7);
    // The next text goes on from the cursor
    text::begin(w, c, "XY", r, 15, false);
    CHECK(text::step(w, c, f, -1) == text::State::Done);
    CHECK(row_text(c, 7, 2, 11) == "KLMN ENDXY");
    CHECK(w.col == 2 && w.row == 8);                   // filled the line: cursor wraps
    text::begin(w, c, "", r, 15, false);
    CHECK(w.state == text::State::Done);

    // Pascal strings
    Bytes b = {5, 'H', 'E', 'L', 'L', 'O', 3, 'A', 1, 'B', 0, 9, 'S', 'H'};
    dax::MemorySource src(b.data(), static_cast<uint32_t>(b.size()));
    char out[text::kMaxString];
    CHECK(text::read_pascal(src, 0, out, sizeof out) && strcmp(out, "HELLO") == 0);
    CHECK(!text::read_pascal(src, 6, out, sizeof out));      // a control byte
    CHECK(!text::read_pascal(src, 10, out, sizeof out));     // empty
    CHECK(!text::read_pascal(src, 11, out, sizeof out));     // runs past the end
    CHECK(!text::read_pascal(src, 0, out, 4));               // too long for the buffer
    // ... and from a packed program
    Bytes img(0x200, 0);
    const char* msg = "Press any key";
    img[0x150] = static_cast<uint8_t>(strlen(msg));
    memcpy(&img[0x151], msg, strlen(msg));
    const Bytes exe = make_exe(img, 0x40, true);
    dax::MemorySource es(exe.data(), static_cast<uint32_t>(exe.size()));
    exepack::Info in;
    CHECK(exepack::parse(es, in) == exepack::Status::Ok);
    CHECK(text::read_pascal(es, in, 0x150, out, sizeof out) && strcmp(out, msg) == 0);
    CHECK(!text::read_pascal(es, in, 0x1F0, out, sizeof out));
}

static void test_menu()
{
    static font::Font f;
    make_test_font(f);
    static uint8_t px[pic::kScreenW * pic::kScreenH];
    pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
    c.clear(7);
    text::Menu m;
    text::build(m, "DO: ", "Area Cast View 2nd");
    CHECK(m.count == 4);
    CHECK(m.start[0] == 0 && m.end[0] == 3 && m.start[1] == 5 && m.end[1] == 8 && m.start[3] == 15 && m.end[3] == 17);
    CHECK(text::key(m, 2) == 'V' && text::key(m, 3) == '2' && text::key(m, 4) == 0);
    // Hits: the prompt is 4 columns; a choice's trailing space belongs to it
    CHECK(text::hit(m, 3) == -1 && text::hit(m, 4) == 0 && text::hit(m, 8) == 0 && text::hit(m, 9) == 1);
    CHECK(text::hit(m, 21) == 3 && text::hit(m, 22) == -1);
    m.selected = 1;
    text::draw(c, f, m);
    const int y = text::kMenuRow * 8 + 3;      // a solid row of every glyph
    auto at = [&](int col) { return c.px[y * c.w + col * 8]; };
    CHECK(at(0) == 13);                        // prompt colour
    CHECK(at(4) == 15 && at(5) == 10);         // key letter, rest of the word
    CHECK(at(9) == 0 && at(12) == 0);          // the chosen word: reversed (black ink)
    CHECK(c.px[(text::kMenuRow * 8) * c.w + 9 * 8 + 7] == 15 || c.px[(text::kMenuRow * 8) * c.w + 9 * 8] == 15);
    CHECK(at(19) == 15 && at(20) == 10);       // "2" is a key, "n" isn't
    CHECK(at(30) == 0 && c.px[(text::kMenuRow * 8 - 1) * c.w] == 7);   // rest of row 24 cleared, row 23 untouched
}

static void test_printcalls()
{
    // A fake overlay: code segment at 0x40, two strings, then two print calls
    Bytes ovr(0x40, 0x90);
    const uint32_t seg = 0x40;
    auto add_str = [&](const char* s) {
        const uint32_t off = static_cast<uint32_t>(ovr.size()) - seg;
        ovr.push_back(static_cast<uint8_t>(strlen(s)));
        ovr.insert(ovr.end(), s, s + strlen(s));
        return off;
    };
    const uint32_t a = add_str("credits"), b2 = add_str("by: someone");
    const uint32_t calls = static_cast<uint32_t>(ovr.size());
    auto call = [&](int col, int row, int fg, int bg, uint32_t off) {
        const uint8_t blk[printcalls::kBlock] = {
            0xB0, (uint8_t)col, 0x50, 0xB0, (uint8_t)row, 0x50, 0xB0, (uint8_t)fg, 0x50, 0xB0, (uint8_t)bg, 0x50,
            0x8D, 0x7E, 0xDB, 0x16, 0x57, 0xBF, (uint8_t)(off & 0xFF), (uint8_t)(off >> 8), 0x0E, 0x57,
            0x9A, 1, 2, 3, 4, 0x9A, 5, 6, 7, 8};
        ovr.insert(ovr.end(), blk, blk + sizeof blk);
    };
    call(2, 1, 10, 0, a);
    call(9, 2, 11, 0, b2);
    ovr.push_back(0x89);    // mov sp, bp ... the end
    ovr.insert(ovr.end(), 40, 0);
    dax::MemorySource src(ovr.data(), static_cast<uint32_t>(ovr.size()));
    printcalls::Line l[8];
    CHECK(printcalls::read(src, calls, seg, l, 8) == 2);
    CHECK(l[0].row == 1 && l[0].col == 2 && l[0].fg == 10 && strcmp(l[0].s, "credits") == 0);
    CHECK(l[1].row == 2 && l[1].col == 9 && l[1].fg == 11 && strcmp(l[1].s, "by: someone") == 0);
    CHECK(printcalls::read(src, calls, seg, l, 1) == 1);
    CHECK(printcalls::read(src, calls + 1, seg, l, 8) == 0);     // not on a call
    CHECK(printcalls::read(src, calls, seg + 3, l, 8) == 0);     // strings don't read
}

// ---- Icons -----------------------------------------------------------------
// A DIB icon image: pixel (x, y) colour from col(x, y) (RGB; for paletted
// images an index), see-through where clear(x, y)
static Bytes make_dib(int w, int bits, uint32_t (*col)(int, int), bool (*clear)(int, int), bool alpha)
{
    Bytes b;
    const int colours = bits <= 8 ? 1 << bits : 0;
    put32(b, 40); put32(b, w); put32(b, w * 2); put16(b, 1); put16(b, bits); put32(b, 0);
    put32(b, 0); put32(b, 0); put32(b, 0); put32(b, colours); put32(b, 0);
    for (int i = 0; i < colours; ++i) {      // palette: index i = grey-ish (i*16, i, 255-i)
        b.push_back(static_cast<uint8_t>(255 - i)); b.push_back(static_cast<uint8_t>(i)); b.push_back(static_cast<uint8_t>(i * 16)); b.push_back(0);
    }
    const int stride = ((w * bits + 31) / 32) * 4, mstride = ((w + 31) / 32) * 4;
    for (int y = w - 1; y >= 0; --y) {       // bottom-up
        Bytes row(stride, 0);
        for (int x = 0; x < w; ++x) {
            const uint32_t c = col(x, y);
            if (bits == 32) { row[x*4] = c & 0xFF; row[x*4+1] = (c >> 8) & 0xFF; row[x*4+2] = (c >> 16) & 0xFF; row[x*4+3] = alpha ? (clear(x, y) ? 0 : 200) : 0; }
            else if (bits == 24) { row[x*3] = c & 0xFF; row[x*3+1] = (c >> 8) & 0xFF; row[x*3+2] = (c >> 16) & 0xFF; }
            else if (bits == 8) row[x] = static_cast<uint8_t>(c);
            else if (bits == 4) row[x / 2] |= static_cast<uint8_t>((c & 15) << ((x & 1) ? 0 : 4));
            else row[x / 8] |= static_cast<uint8_t>((c & 1) << (7 - (x & 7)));
        }
        b.insert(b.end(), row.begin(), row.end());
    }
    for (int y = w - 1; y >= 0; --y) {
        Bytes row(mstride, 0);
        for (int x = 0; x < w; ++x)
            if (clear(x, y)) row[x / 8] |= static_cast<uint8_t>(0x80 >> (x & 7));
        b.insert(b.end(), row.begin(), row.end());
    }
    return b;
}

static uint32_t col_rgb(int x, int y) { return static_cast<uint32_t>(x * 4) << 16 | static_cast<uint32_t>(y * 4) << 8 | 0x33; }
static uint32_t col_idx(int x, int y) { return static_cast<uint32_t>((x + y) & 15); }
static bool clear_corner(int x, int y) { return x < 2 && y < 2; }

static Bytes make_ico(const std::vector<std::pair<int, Bytes>>& imgs)
{
    Bytes b;
    put16(b, 0); put16(b, 1); put16(b, static_cast<uint32_t>(imgs.size()));
    uint32_t off = 6 + 16 * static_cast<uint32_t>(imgs.size());
    for (const auto& im : imgs) {
        b.push_back(static_cast<uint8_t>(im.first)); b.push_back(static_cast<uint8_t>(im.first));
        b.push_back(0); b.push_back(0); put16(b, 1); put16(b, 32);
        put32(b, static_cast<uint32_t>(im.second.size())); put32(b, off);
        off += static_cast<uint32_t>(im.second.size());
    }
    for (const auto& im : imgs) b.insert(b.end(), im.second.begin(), im.second.end());
    return b;
}

// A minimal PE with one resource section: RT_ICON 1, 2 and RT_GROUP_ICON 1
static Bytes make_pe(const Bytes& icon1, const Bytes& icon2)
{
    const uint32_t kVa = 0x2000, kRaw = 0x200;
    Bytes rs;   // the resource section, offsets from its start
    auto dir = [&](const std::vector<std::pair<uint32_t, uint32_t>>& entries) {
        const uint32_t at = static_cast<uint32_t>(rs.size());
        for (int i = 0; i < 12; ++i) rs.push_back(0);
        put16(rs, 0); put16(rs, static_cast<uint32_t>(entries.size()));
        for (const auto& e : entries) { put32(rs, e.first); put32(rs, e.second); }
        return at;
    };
    auto patch32 = [&](uint32_t at, uint32_t v) { for (int i = 0; i < 4; ++i) rs[at + i] = static_cast<uint8_t>(v >> (i * 8)); };
    // root: type 3 -> A, type 14 -> B (patched below)
    const uint32_t root = dir({{3, 0}, {14, 0}});
    const uint32_t a = dir({{1, 0}, {2, 0}});
    const uint32_t a1 = dir({{1033, 0}}), a2 = dir({{1033, 0}});
    const uint32_t b = dir({{1, 0}});
    const uint32_t b1 = dir({{1033, 0}});
    auto data_entry = [&](uint32_t dir_at) {
        const uint32_t de = static_cast<uint32_t>(rs.size());
        for (int i = 0; i < 16; ++i) rs.push_back(0);
        patch32(dir_at + 16 + 4, de);
        return de;
    };
    const uint32_t de1 = data_entry(a1), de2 = data_entry(a2), deg = data_entry(b1);
    patch32(root + 16 + 4, 0x80000000u | a);
    patch32(root + 24 + 4, 0x80000000u | b);
    patch32(a + 16 + 4, 0x80000000u | a1);
    patch32(a + 24 + 4, 0x80000000u | a2);
    patch32(b + 16 + 4, 0x80000000u | b1);
    auto blob = [&](uint32_t de, const Bytes& data) {
        const uint32_t at = static_cast<uint32_t>(rs.size());
        rs.insert(rs.end(), data.begin(), data.end());
        patch32(de, kVa + at);
        patch32(de + 4, static_cast<uint32_t>(data.size()));
    };
    blob(de1, icon1);
    blob(de2, icon2);
    Bytes grp;
    put16(grp, 0); put16(grp, 1); put16(grp, 2);
    for (int i = 0; i < 2; ++i) {
        const Bytes& im = i ? icon2 : icon1;
        grp.push_back(0); grp.push_back(0); grp.push_back(0); grp.push_back(0); put16(grp, 1); put16(grp, 32);
        put32(grp, static_cast<uint32_t>(im.size())); put16(grp, static_cast<uint32_t>(i + 1));
    }
    blob(deg, grp);

    Bytes pe(0x40, 0);
    pe[0] = 'M'; pe[1] = 'Z'; pe[0x3C] = 0x40;
    pe.push_back('P'); pe.push_back('E'); pe.push_back(0); pe.push_back(0);
    put16(pe, 0x14C); put16(pe, 1); put32(pe, 0); put32(pe, 0); put32(pe, 0); put16(pe, 224); put16(pe, 0);
    const size_t opt = pe.size();
    pe.resize(opt + 224, 0);
    pe[opt] = 0x0B; pe[opt + 1] = 0x01;                     // PE32
    const size_t dd = opt + 96 + 2 * 8;                     // resource directory
    for (int i = 0; i < 4; ++i) { pe[dd + i] = static_cast<uint8_t>(kVa >> (i * 8)); pe[dd + 4 + i] = static_cast<uint8_t>(rs.size() >> (i * 8)); }
    const char name[8] = {'.', 'r', 's', 'r', 'c', 0, 0, 0};
    pe.insert(pe.end(), name, name + 8);
    put32(pe, static_cast<uint32_t>(rs.size())); put32(pe, kVa); put32(pe, static_cast<uint32_t>(rs.size())); put32(pe, kRaw);
    for (int i = 0; i < 16; ++i) pe.push_back(0);
    pe.resize(kRaw, 0);
    pe.insert(pe.end(), rs.begin(), rs.end());
    return pe;
}

// ---- inflate / PNG -----------------------------------------------------------
static Bytes inflate_pattern()
{
    Bytes d;
    for (int i = 0; i < 1200; ++i) d.push_back(static_cast<uint8_t>(((i * 7) ^ (i >> 4)) & 0xFF));
    for (int k = 0; k < 40; ++k) for (const char* c = "the gold box "; *c; ++c) d.push_back(static_cast<uint8_t>(*c));
    for (int k = 0; k < 3; ++k) for (int i = 0; i < 256; ++i) d.push_back(static_cast<uint8_t>(i));
    return d;
}

struct MemIn : inflate::Input {
    const uint8_t* p; size_t n, i = 0;
    MemIn(const uint8_t* d, size_t len) : p(d), n(len) {}
    int byte() override { return i < n ? p[i++] : -1; }
};
struct VecOut : inflate::Output {
    Bytes v; size_t limit = 1 << 20;
    bool put(uint8_t b) override { v.push_back(b); return v.size() < limit; }
};

static void test_inflate()
{
    static uint8_t window[inflate::kWindow];
    const Bytes want = inflate_pattern();
    for (int k = 0; k < 2; ++k) {
        const uint8_t* z = k ? kDeflateFixed : kDeflateDynamic;
        const size_t n = k ? sizeof kDeflateFixed : sizeof kDeflateDynamic;
        MemIn in(z, n);
        VecOut out;
        CHECK(inflate::raw(in, out, window));
        CHECK(out.v == want);
        // cut short: fails, never reads past the end
        MemIn shorter(z, n / 2);
        VecOut out2;
        CHECK(!inflate::raw(shorter, out2, window));
        // the output side can stop it
        MemIn again(z, n);
        VecOut out3;
        out3.limit = 100;
        CHECK(!inflate::raw(again, out3, window) && out3.v.size() == 100);
    }
    // stored block
    const Bytes stored = {0x01, 5, 0, 0xFA, 0xFF, 'h', 'e', 'l', 'l', 'o'};
    MemIn in(stored.data(), stored.size());
    VecOut out;
    CHECK(inflate::raw(in, out, window) && out.v == Bytes({'h', 'e', 'l', 'l', 'o'}));
    const Bytes bad = {0x01, 5, 0, 0xFB, 0xFF, 'h'};
    MemIn in2(bad.data(), bad.size());
    VecOut out2;
    CHECK(!inflate::raw(in2, out2, window));
}

static void put_be32(Bytes& b, uint32_t v)
{
    for (int i = 3; i >= 0; --i) b.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

static void png_chunk(Bytes& b, const char* type, const Bytes& data)
{
    put_be32(b, static_cast<uint32_t>(data.size()));
    b.insert(b.end(), type, type + 4);
    b.insert(b.end(), data.begin(), data.end());
    put_be32(b, 0);     // CRC (not checked)
}

// A PNG of w x h from raw pixel bytes (bpp bytes a pixel), each row with a
// different filter, IDAT split in two, deflate as stored blocks
static Bytes make_png(int w, int h, int colour, int bpp, const Bytes& raw, const Bytes& plte, const Bytes& trns)
{
    const size_t stride = static_cast<size_t>(w) * bpp;
    Bytes filtered;
    for (int y = 0; y < h; ++y) {
        const int f = y % 5;
        filtered.push_back(static_cast<uint8_t>(f));
        for (size_t i = 0; i < stride; ++i) {
            const int x = raw[y * stride + i];
            const int a = i >= static_cast<size_t>(bpp) ? raw[y * stride + i - bpp] : 0;
            const int b = y > 0 ? raw[(y - 1) * stride + i] : 0;
            const int c = (y > 0 && i >= static_cast<size_t>(bpp)) ? raw[(y - 1) * stride + i - bpp] : 0;
            int pred = 0;
            if (f == 1) pred = a;
            if (f == 2) pred = b;
            if (f == 3) pred = (a + b) / 2;
            if (f == 4) {
                const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                pred = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
            }
            filtered.push_back(static_cast<uint8_t>(x - pred));
        }
    }
    Bytes z = {0x78, 0x01};
    for (size_t at = 0; at < filtered.size(); at += 1000) {
        const size_t n = filtered.size() - at < 1000 ? filtered.size() - at : 1000;
        z.push_back(at + n >= filtered.size() ? 1 : 0);
        put16(z, static_cast<uint32_t>(n));
        put16(z, static_cast<uint32_t>(~n & 0xFFFF));
        z.insert(z.end(), filtered.begin() + static_cast<long>(at), filtered.begin() + static_cast<long>(at + n));
    }
    put_be32(z, 0);   // adler32 (not checked)
    Bytes pngb = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    Bytes ihdr;
    put_be32(ihdr, static_cast<uint32_t>(w));
    put_be32(ihdr, static_cast<uint32_t>(h));
    ihdr.push_back(8); ihdr.push_back(static_cast<uint8_t>(colour)); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    png_chunk(pngb, "IHDR", ihdr);
    if (!plte.empty()) png_chunk(pngb, "PLTE", plte);
    if (!trns.empty()) png_chunk(pngb, "tRNS", trns);
    const size_t half = z.size() / 2;
    png_chunk(pngb, "IDAT", Bytes(z.begin(), z.begin() + static_cast<long>(half)));
    png_chunk(pngb, "IDAT", Bytes(z.begin() + static_cast<long>(half), z.end()));
    png_chunk(pngb, "IEND", Bytes());
    return pngb;
}

static Bytes rgba_pattern(int w, int h)
{
    Bytes raw;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            raw.push_back(static_cast<uint8_t>(x * 3)); raw.push_back(static_cast<uint8_t>(y * 5));
            raw.push_back(static_cast<uint8_t>(x ^ y)); raw.push_back(static_cast<uint8_t>((x + y) % 3 ? 255 : 0));
        }
    return raw;
}

struct Collect {
    Bytes px;
    int w = 0, rows = 0, last_y = -1;
    bool order = true;
};
static bool collect_png(int y, const uint8_t* rgba, void* ctx)
{
    Collect& c = *static_cast<Collect*>(ctx);
    if (y != c.last_y + 1) c.order = false;
    c.last_y = y;
    c.px.insert(c.px.end(), rgba, rgba + c.w * 4);
    ++c.rows;
    return true;
}

static void test_png()
{
    static uint8_t window[inflate::kWindow];
    const int w = 37, h = 23;
    const Bytes raw = rgba_pattern(w, h);
    const Bytes img = make_png(w, h, 6, 4, raw, Bytes(), Bytes());
    dax::MemorySource s(img.data(), static_cast<uint32_t>(img.size()));
    png::Info info;
    CHECK(png::probe(s, 0, static_cast<uint32_t>(img.size()), info) && info.w == w && info.h == h && info.colour == 6);
    Collect c;
    c.w = w;
    CHECK(png::decode(s, 0, static_cast<uint32_t>(img.size()), window, collect_png, &c));
    CHECK(c.rows == h && c.order && c.px == raw);

    // palette + tRNS
    Bytes idx, plte, trns = {0, 128};
    for (int i = 0; i < 4; ++i) { plte.push_back(static_cast<uint8_t>(i * 60)); plte.push_back(10); plte.push_back(static_cast<uint8_t>(250 - i)); }
    for (int i = 0; i < 6 * 5; ++i) idx.push_back(static_cast<uint8_t>(i % 4));
    const Bytes pimg = make_png(6, 5, 3, 1, idx, plte, trns);
    dax::MemorySource ps(pimg.data(), static_cast<uint32_t>(pimg.size()));
    Collect pc;
    pc.w = 6;
    CHECK(png::decode(ps, 0, static_cast<uint32_t>(pimg.size()), window, collect_png, &pc));
    CHECK(pc.rows == 5 && pc.px[0 * 4 + 3] == 0 && pc.px[1 * 4 + 3] == 128 && pc.px[2 * 4 + 3] == 255);
    CHECK(pc.px[3 * 4] == 180 && pc.px[3 * 4 + 2] == 247);

    // damaged: a bad filter type
    Bytes bad = img;
    const size_t idat = 8 + 25;                          // first IDAT chunk
    bad[idat + 8 + 2 + 5] = 9;                           // first row's filter byte (after zlib + stored header)
    dax::MemorySource bs(bad.data(), static_cast<uint32_t>(bad.size()));
    Collect bc;
    bc.w = w;
    CHECK(!png::decode(bs, 0, static_cast<uint32_t>(bad.size()), window, collect_png, &bc));
    // interlaced: refused
    Bytes il = img;
    il[8 + 8 + 12] = 1;
    dax::MemorySource is(il.data(), static_cast<uint32_t>(il.size()));
    CHECK(!png::probe(is, 0, static_cast<uint32_t>(il.size()), info));
}

// ---- Icons -----------------------------------------------------------------
struct Rendered {
    Bytes px;
    int rows = 0, w = 0;
};
static void collect_icon(int, const uint8_t* rgba, int w, void* ctx)
{
    Rendered& r = *static_cast<Rendered*>(ctx);
    r.w = w;
    r.px.insert(r.px.end(), rgba, rgba + w * 4);
    ++r.rows;
}

static void test_icon()
{
    static uint8_t window[inflate::kWindow];
    const Bytes i16 = make_dib(16, 4, col_idx, clear_corner, false);
    const Bytes i32 = make_dib(32, 32, col_rgb, clear_corner, true);
    const Bytes i32m = make_dib(32, 32, col_rgb, clear_corner, false);   // no alpha: mask decides
    const Bytes i48 = make_dib(48, 24, col_rgb, clear_corner, false);
    const Bytes big = make_png(64, 64, 6, 4, rgba_pattern(64, 64), Bytes(), Bytes());

    const Bytes ico = make_ico({{16, i16}, {32, i32}, {48, i48}});
    dax::MemorySource s(ico.data(), static_cast<uint32_t>(ico.size()));
    icon::Found f;
    CHECK(icon::find(s, f) && f.w == 48 && f.bits == 24 && !f.png);    // the biggest
    Rendered r;
    CHECK(icon::render(s, f, 48, 48, nullptr, collect_icon, &r) && r.rows == 48 && r.w == 48);
    const uint8_t* p = r.px.data() + (5 * 48 + 7) * 4;                  // (7, 5)
    CHECK(p[0] == 28 && p[1] == 20 && p[2] == 0x33 && p[3] == 255);
    CHECK(r.px[3] == 0);                                                // see-through corner
    // halved: 2 x 2 blocks averaged; the corner block is all see-through
    Rendered half;
    CHECK(icon::render(s, f, 24, 24, nullptr, collect_icon, &half) && half.rows == 24 && half.w == 24);
    p = half.px.data() + (2 * 24 + 3) * 4;                              // source (6..7, 4..5)
    CHECK(p[0] == 26 && p[1] == 18 && p[3] == 255 && half.px[3] == 0);
    // doubled: pixels repeated
    Rendered dbl;
    CHECK(icon::render(s, f, 96, 96, nullptr, collect_icon, &dbl) && dbl.rows == 96);
    CHECK(memcmp(dbl.px.data() + (10 * 96 + 14) * 4, r.px.data() + (5 * 48 + 7) * 4, 4) == 0);
    CHECK(memcmp(dbl.px.data() + (11 * 96 + 15) * 4, r.px.data() + (5 * 48 + 7) * 4, 4) == 0);

    // 32-bit with alpha, and the 4-bit palette one, via an .ico of each
    const Bytes ico32 = make_ico({{32, i32}});
    dax::MemorySource s32(ico32.data(), static_cast<uint32_t>(ico32.size()));
    Rendered r32;
    CHECK(icon::find(s32, f) && f.bits == 32 && icon::render(s32, f, 32, 32, nullptr, collect_icon, &r32));
    p = r32.px.data() + (5 * 32 + 7) * 4;
    CHECK(p[0] == 28 && p[1] == 20 && p[3] == 200 && r32.px[3] == 0);
    const Bytes ico4 = make_ico({{16, i16}});
    dax::MemorySource s4(ico4.data(), static_cast<uint32_t>(ico4.size()));
    Rendered r4;
    CHECK(icon::find(s4, f) && f.bits == 4 && icon::render(s4, f, 16, 16, nullptr, collect_icon, &r4));
    p = r4.px.data() + (3 * 16 + 4) * 4;                                // index 7: (112, 7, 248)
    CHECK(p[0] == 112 && p[1] == 7 && p[2] == 248 && p[3] == 255);
    const Bytes icom = make_ico({{32, i32m}});
    dax::MemorySource sm(icom.data(), static_cast<uint32_t>(icom.size()));
    Rendered rm;
    CHECK(icon::find(sm, f) && icon::render(sm, f, 32, 32, nullptr, collect_icon, &rm));
    CHECK(rm.px[3] == 0 && rm.px[(10 * 32 + 10) * 4 + 3] == 255);

    // A PNG entry wins when it's the biggest; it needs the window
    const Bytes icop = make_ico({{32, i32}, {64, big}});
    dax::MemorySource sp(icop.data(), static_cast<uint32_t>(icop.size()));
    CHECK(icon::find(sp, f) && f.png && f.w == 64);
    Rendered rp;
    CHECK(!icon::render(sp, f, 64, 64, nullptr, collect_icon, &rp));
    CHECK(icon::render(sp, f, 64, 64, window, collect_icon, &rp) && rp.rows == 64);
    const Bytes want = rgba_pattern(64, 64);
    bool same = true;
    for (size_t i = 0; i < want.size(); i += 4) {
        if (want[i + 3] != rp.px[i + 3]) same = false;
        if (want[i + 3] && memcmp(&want[i], &rp.px[i], 3) != 0) same = false;
    }
    CHECK(same);
    Rendered rq;
    CHECK(icon::render(sp, f, 32, 32, window, collect_icon, &rq) && rq.rows == 32 && rq.w == 32);

    // In a PE (.dll)
    const Bytes pe = make_pe(i16, i32);
    dax::MemorySource spe(pe.data(), static_cast<uint32_t>(pe.size()));
    Rendered rpe;
    CHECK(icon::find(spe, f) && f.w == 32 && icon::render(spe, f, 32, 32, nullptr, collect_icon, &rpe));
    p = rpe.px.data() + (5 * 32 + 7) * 4;
    CHECK(p[0] == 28 && p[1] == 20 && p[3] == 200);
    const Bytes junk(300, 1);
    dax::MemorySource sj(junk.data(), static_cast<uint32_t>(junk.size()));
    CHECK(!icon::find(sj, f));
    Bytes png_only = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    png_only.resize(200, 0);
    const Bytes ico_bad = make_ico({{0, png_only}});
    dax::MemorySource sb(ico_bad.data(), static_cast<uint32_t>(ico_bad.size()));
    CHECK(!icon::find(sb, f));
}

// ---- ECL / GEO / 3D view ----------------------------------------------------
static void op_imm(Bytes& b, uint8_t v) { b.push_back(0); b.push_back(v); }
static void op_addr(Bytes& b, uint16_t a) { b.push_back(1); b.push_back(a & 0xFF); b.push_back(a >> 8); }

static void test_ecl()
{
    const profile::Profile* p = profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432);
    CHECK(p && p->ecl_ops);
    const ecl::OpSet& set = *p->ecl_ops;
    // Code layout (offsets from the code start = VM 0x8000):
    //   0: 5 entry points (4 bytes each); all but the first point at 'quiet'
    //  20: GOTO main                      (5 bytes)
    //  25: junk data the script jumps over
    //  31: main: LOAD FILES 7, 2, 255     (1 + 6)
    //  38: IF =                           (1)
    //  39: GOSUB sub                      (4)
    //  43: LOAD PIECES 3, 127, 9          (1 + 6)
    //  50: LOAD FILES 9, 2, 255           (not the first: ignored)
    //  57: VERTICAL MENU mem, str, 2 + 2 items, then EXIT
    //  quiet: EXIT
    Bytes code;
    const uint16_t kBase = 0x8000;
    for (int i = 0; i < 5; ++i) { code.push_back(0); op_addr(code, 0); }     // patched below
    code.push_back(0x01); op_addr(code, 0);                                   // GOTO main (patched)
    const size_t goto_at = code.size() - 3;
    for (int i = 0; i < 6; ++i) code.push_back(0xEE);                         // junk
    const size_t main_at = code.size();
    code.push_back(0x21); op_imm(code, 7); op_imm(code, 2); op_imm(code, 255);
    code.push_back(0x16);
    code.push_back(0x02); op_addr(code, 0);
    const size_t gosub_at = code.size() - 3;
    code.push_back(0x37); op_imm(code, 3); op_imm(code, 127); op_imm(code, 9);
    code.push_back(0x21); op_imm(code, 9); op_imm(code, 2); op_imm(code, 255);
    code.push_back(0x15); op_addr(code, 0x4B00); code.push_back(0x80); code.push_back(2); code.push_back(0x55); code.push_back(0x66);
    op_imm(code, 2); op_imm(code, 1); op_imm(code, 2);
    code.push_back(0x00);
    const size_t quiet = code.size();
    code.push_back(0x00);
    auto patch = [&](size_t at, size_t target) {
        code[at] = 1;
        code[at + 1] = static_cast<uint8_t>((kBase + target) & 0xFF);
        code[at + 2] = static_cast<uint8_t>((kBase + target) >> 8);
    };
    patch(1, 20);
    for (int i = 1; i < 5; ++i) patch(1 + i * 4, quiet);
    patch(goto_at, main_at);
    patch(gosub_at, quiet);
    Bytes block = {0x12, 0x34};
    block.insert(block.end(), code.begin(), code.end());

    ecl::Insn in;
    CHECK(ecl::decode(code.data(), static_cast<uint32_t>(code.size()), static_cast<uint32_t>(main_at), set, in));
    CHECK(in.op == 0x21 && in.count == 3 && in.ops[0].immediate() && in.ops[0].low == 7 && in.next == main_at + 7);
    CHECK(ecl::decode(code.data(), static_cast<uint32_t>(code.size()), static_cast<uint32_t>(quiet - 15), set, in));
    CHECK(in.op == 0x15 && in.count == 5 && in.next == quiet - 1);
    CHECK(!ecl::decode(code.data(), static_cast<uint32_t>(code.size()), 25, set, in));    // junk
    uint32_t ent[5];
    CHECK(ecl::entries(code.data(), static_cast<uint32_t>(code.size()), set, ent) && ent[0] == 20 && ent[4] == quiet);

    const ecl::MapLoad m = ecl::find_map_load(block.data(), static_cast<uint32_t>(block.size()), set);
    CHECK(m.has_geo() && m.geo == 7);
    CHECK(m.has_walls() && m.walls[0] == 3 && m.walls[1] == ecl::kNone && m.walls[2] == 9);
    // Without the GOTO's target reachable (entry to quiet only): nothing
    Bytes quiet_only = block;
    quiet_only[2 + 1] = static_cast<uint8_t>((kBase + quiet) & 0xFF);
    quiet_only[2 + 2] = static_cast<uint8_t>((kBase + quiet) >> 8);
    const ecl::MapLoad none = ecl::find_map_load(quiet_only.data(), static_cast<uint32_t>(quiet_only.size()), set);
    CHECK(!none.has_geo() && !none.has_walls());
}

// Packs A-Z, space and digits the way the scripts' strings are packed
static void op_str(Bytes& b, const char* t)
{
    std::vector<int> v;
    for (const char* c = t; *c; ++c) v.push_back(*c >= 'A' && *c <= 'Z' ? *c - 0x40 : *c);
    while (v.size() % 4) v.push_back(0);
    Bytes packed;
    for (size_t i = 0; i < v.size(); i += 4) {
        packed.push_back(static_cast<uint8_t>(v[i] << 2 | v[i + 1] >> 4));
        packed.push_back(static_cast<uint8_t>((v[i + 1] & 15) << 4 | v[i + 2] >> 2));
        packed.push_back(static_cast<uint8_t>((v[i + 2] & 3) << 6 | v[i + 3]));
    }
    b.push_back(0x80);
    b.push_back(static_cast<uint8_t>(packed.size()));
    b.insert(b.end(), packed.begin(), packed.end());
}

struct TestHost : ecl::Host {
    Bytes next;                 // the block NEWECL loads
    int map = -1, walls[4] = {-2, -2, -2, -2}, pic = -1, loads = 0, frames = 0;
    int sprite_id = -1, sprite_dist = -1, sprites = 0;
    bool load_script(int, uint8_t* code, uint32_t* len) override
    {
        ++loads;
        if (next.size() < 2) return false;
        memcpy(code, next.data() + 2, next.size() - 2);
        *len = static_cast<uint32_t>(next.size() - 2);
        return true;
    }
    void load_map(int g) override { map = g; }
    void load_walls(int set, int block) override { walls[set] = block; }
    void picture(int id, int) override { pic = id; }
    void redraw() override {}
    void anim_step() override { ++frames; }
    int wall_type(int, int, int) override { return 0; }      // open ground all round
    void sprite(int id, int distance) override
    {
        sprite_id = id;
        sprite_dist = distance;
        ++sprites;
    }
    void log(const char*) override {}
};

static void test_ecl_vm()
{
    {
        char out[16];
        const uint8_t hi[] = {0x20, 0x90};     // "HI"
        ecl::unpack_string(hi, 2, out, sizeof out);
        CHECK(strcmp(out, "HI") == 0);
    }
    const profile::Profile* p = profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432);
    CHECK(p && p->ecl_ops);
    if (!p || !p->ecl_ops) return;
    // first (entry 4):
    //   SAVE 5 -> 0x4C00; ADD [0x4C00] + 3 -> 0x4C01; COMPARE [0x4C01], 8;
    //   IF = PRINTCLEAR "HELLO"; IF != PRINT "WRONG"
    //   SAVE 9 -> party x; LOAD FILES 4, 2, 255; LOAD PIECES 3, 255, 9
    //   GOSUB clock; VERTICAL MENU 0x4C02 "PICK" 2 items "ONE" "TWO"
    //   RANDOM 3 -> 0x4C03; NEWECL 7
    // clock: ECL CLOCK 75 minutes (slot 1); PICTURE 12; RETURN
    // other entries: EXIT
    Bytes c;
    const uint16_t kBase = 0x8000;
    for (int i = 0; i < 5; ++i) { c.push_back(0); op_addr(c, 0); }
    auto patch = [&](size_t at, size_t target) {
        c[at] = 1;
        c[at + 1] = static_cast<uint8_t>((kBase + target) & 0xFF);
        c[at + 2] = static_cast<uint8_t>((kBase + target) >> 8);
    };
    const size_t first = c.size();
    c.push_back(0x09); op_imm(c, 5); op_addr(c, 0x4C00);
    c.push_back(0x04); op_addr(c, 0x4C00); op_imm(c, 3); op_addr(c, 0x4C01);
    c.push_back(0x03); op_addr(c, 0x4C01); op_imm(c, 8);
    c.push_back(0x16); c.push_back(0x12); op_str(c, "HELLO");
    c.push_back(0x17); c.push_back(0x11); op_str(c, "WRONG");
    c.push_back(0x09); op_imm(c, 9); op_addr(c, 0xC04B);
    c.push_back(0x21); op_imm(c, 4); op_imm(c, 2); op_imm(c, 255);
    c.push_back(0x37); op_imm(c, 3); op_imm(c, 255); op_imm(c, 9);
    c.push_back(0x02); op_addr(c, 0);
    const size_t gosub_at = c.size() - 3;
    c.push_back(0x15); op_addr(c, 0x4C02); op_str(c, "PICK"); op_imm(c, 2); op_str(c, "ONE"); op_str(c, "TWO");
    c.push_back(0x08); op_imm(c, 3); op_addr(c, 0x4C03);
    c.push_back(0x20); op_imm(c, 7);
    const size_t clock = c.size();
    c.push_back(0x34); op_imm(c, 75); op_imm(c, 1);
    c.push_back(0x0E); op_imm(c, 12);
    c.push_back(0x13);
    const size_t quiet = c.size();
    c.push_back(0x00);
    for (int i = 0; i < 4; ++i) patch(1 + i * 4, quiet);
    patch(1 + 4 * 4, first);
    patch(gosub_at, clock);

    ecl::GameState gs;
    TestHost host;
    host.next = {0, 0, 0x00};               // the block NEWECL loads: no entries (fails init)
    memcpy(gs.code, c.data(), c.size());
    gs.code_len = static_cast<uint32_t>(c.size());
    ecl::Vm vm(gs, host, *p->ecl_ops);
    CHECK(vm.init_script());
    CHECK(vm.entry(4) == kBase + first && vm.entry(0) == kBase + quiet);
    CHECK(vm.run(vm.entry(0)) == ecl::Stop::Stopped);

    ecl::Stop r = vm.run(vm.entry(4));
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Print && vm.clear() && strcmp(vm.text(), "HELLO") == 0);
    CHECK(vm.get(0x4C00) == 5 && vm.get(0x4C01) == 8 && vm.flag(0) && !vm.flag(1));
    r = vm.resume();
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::ListMenu);
    CHECK(strcmp(vm.prompt(), "PICK") == 0 && vm.items() == 2 && strcmp(vm.item(1), "TWO") == 0);
    CHECK(gs.x == 9 && gs.moved);
    CHECK(host.map == 4 && vm.get(0x4BC5) == 4);
    CHECK(host.walls[1] == 3 && host.walls[2] == -1 && host.walls[3] == 9);
    CHECK(host.pic == 12);
    CHECK(vm.get(0x4BC7) == 5 && vm.get(0x4BC8) == 1 && vm.get(0x4BC9) == 1);   // 1:15
    // The answer, then NEWECL whose block won't load: an error, nothing run
    r = vm.answer(1);
    CHECK(vm.get(0x4C02) == 1 && vm.get(0x4C03) <= 3);
    CHECK(host.loads == 1 && r == ecl::Stop::Error);

    // A loadable next block: NEWECL switches script
    Bytes nb = {0x12, 0x34};
    for (int i = 0; i < 5; ++i) { nb.push_back(0); op_addr(nb, kBase + 20); }
    nb.push_back(0x00);
    host.next = nb;
    memcpy(gs.code, c.data(), c.size());
    gs.code_len = static_cast<uint32_t>(c.size());
    CHECK(vm.init_script());
    r = vm.run(vm.entry(4));
    while (r == ecl::Stop::Waiting) r = vm.wait() == ecl::Wait::ListMenu ? vm.answer(0) : vm.resume();
    CHECK(r == ecl::Stop::NewScript && gs.script == 7 && gs.code_len == nb.size() - 2);
    CHECK(vm.entry(4) == kBase + 20);
    CHECK(vm.get(0x4C00) == 0);             // a new script clears its variables

    // CALL 6803: the picture's next frame, then the game's delay (speed 4 = 0.4 s)
    Bytes an;
    for (int i = 0; i < 5; ++i) { an.push_back(0); op_addr(an, kBase + 20); }
    an.push_back(0x2D); op_addr(an, 0x6803);
    an.push_back(0x00);
    memcpy(gs.code, an.data(), an.size());
    gs.code_len = static_cast<uint32_t>(an.size());
    CHECK(vm.init_script());
    r = vm.run(vm.entry(0));
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Pause && vm.pause_ms() == 400 && host.frames == 1);
    CHECK(vm.resume() == ecl::Stop::Stopped);

    // SETUP MONSTER sprite 7, at most 1 square away, picture 9: the sprite
    // one square off; APPROACH: the sprite next to the party, then (after a
    // pause) the picture
    Bytes mon;
    for (int i = 0; i < 5; ++i) { mon.push_back(0); op_addr(mon, kBase + 20); }
    mon.push_back(0x0C); op_imm(mon, 7); op_imm(mon, 1); op_imm(mon, 9);
    mon.push_back(0x0D);
    mon.push_back(0x00);
    memcpy(gs.code, mon.data(), mon.size());
    gs.code_len = static_cast<uint32_t>(mon.size());
    CHECK(vm.init_script());
    vm.set(0x4BE6, 1);
    host.pic = -1;
    r = vm.run(vm.entry(0));
    CHECK(host.sprites == 2 && host.sprite_id == 7 && host.sprite_dist == 0);
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Pause && host.pic == -1);
    CHECK(vm.resume() == ecl::Stop::Stopped && host.pic == 9);

    // ENCOUNTER MENU: 2 squares off (open ground, max 2), the far text, then
    // Combat Wait Flee Advance. Results all "1" (fight on Combat, Wait = both
    // wait, Advance = come closer)
    Bytes em;
    for (int i = 0; i < 5; ++i) { em.push_back(0); op_addr(em, kBase + 20); }
    em.push_back(0x29);
    op_imm(em, 3); op_imm(em, 2); op_imm(em, 4); op_addr(em, 0x4C05);
    for (int i = 0; i < 5; ++i) op_imm(em, 1);
    op_str(em, "NEAR"); op_str(em, "MID"); op_str(em, "FAR");
    op_imm(em, 6); op_imm(em, 9);
    em.push_back(0x00);
    memcpy(gs.code, em.data(), em.size());
    gs.code_len = static_cast<uint32_t>(em.size());
    CHECK(vm.init_script());
    vm.set(0x4BE6, 1);
    vm.set(0x4C05, 77);
    r = vm.run(vm.entry(0));
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Print && strcmp(vm.text(), "FAR") == 0);
    CHECK(host.sprite_id == 3 && host.sprite_dist == 2);
    r = vm.resume();
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Menu && strcmp(vm.item(3), "Advance") == 0);
    r = vm.answer(3);                       // Advance: one square closer, the menu again
    CHECK(host.sprite_dist == 1 && r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Print &&
          strcmp(vm.text(), "MID") == 0);
    r = vm.resume();
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Menu);
    r = vm.answer(1);                       // Wait: "Both sides wait."
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Print && strcmp(vm.text(), "Both sides wait.") == 0);
    r = vm.resume();                        // the text again, then the menu
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Print);
    r = vm.resume();
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Menu);
    CHECK(vm.get(0x4C05) == 77);
    r = vm.answer(0);                       // Combat
    CHECK(r == ecl::Stop::Stopped && vm.get(0x4C05) == 1);

    // An endless loop is stopped
    Bytes loop;
    for (int i = 0; i < 5; ++i) { loop.push_back(0); op_addr(loop, kBase + 20); }
    loop.push_back(0x01); op_addr(loop, kBase + 20);
    memcpy(gs.code, loop.data(), loop.size());
    gs.code_len = static_cast<uint32_t>(loop.size());
    CHECK(vm.init_script());
    CHECK(vm.run(vm.entry(0)) == ecl::Stop::Error);
}

// A JOURNAL.BIN like the converter writes: entries at two widths
static Bytes journal_picture(int w, int h, int seed)
{
    Bytes data;
    for (int y = 0; y < h; ++y) {
        int x = 0;
        while (x < w) {
            const int c = (x / 16 + y + seed) & 15;
            int run = 1 + ((x + y * 3 + seed) % 16);
            if (run > 16 - x % 16) run = 16 - x % 16;
            if (x + run > w) run = w - x;
            data.push_back(static_cast<uint8_t>((run - 1) << 4 | c));
            x += run;
        }
    }
    Bytes b;
    put16(b, w); put16(b, h);
    for (int i = 0; i < 16; ++i) put16(b, i * 0x1111);
    put32(b, static_cast<uint32_t>(data.size()));
    b.insert(b.end(), data.begin(), data.end());
    return b;
}
static int journal_expect(int x, int y, int seed) { return (x / 16 + y + seed) & 15; }

struct RowCheck { int seed, w, first, seen; bool ok; };
static void row_check(int y, const uint8_t* row, int w, void* ctx)
{
    RowCheck& r = *static_cast<RowCheck*>(ctx);
    if (w != r.w || y != r.first + r.seen) r.ok = false;
    for (int x = 0; x < w; ++x)
        if (row[x] != journal_expect(x, y, r.seed)) r.ok = false;
    ++r.seen;
}

static void test_journal()
{
    // Entries J1, T12, J31 at widths 310 and 470
    const char kinds[3] = {'J', 'T', 'J'};
    const int nums[3] = {1, 12, 31};
    const int widths[2] = {310, 470};
    Bytes f = {'G', 'B', 'J', '1', 1, 2};
    put16(f, 3);
    for (int w : widths) put16(f, w);
    const size_t index_at = f.size();
    f.resize(f.size() + 3 * (4 + 8));
    for (int e = 0; e < 3; ++e) {
        uint8_t* r = f.data() + index_at + e * 12;
        r[0] = static_cast<uint8_t>(kinds[e]);
        r[1] = 0;
        r[2] = static_cast<uint8_t>(nums[e]);
        r[3] = 0;
        for (int wi = 0; wi < 2; ++wi) {
            const uint32_t at = static_cast<uint32_t>(f.size());
            Bytes pic = journal_picture(widths[wi], 20 + e, e * 5 + wi);
            f.insert(f.end(), pic.begin(), pic.end());
            r = f.data() + index_at + e * 12;
            for (int i = 0; i < 4; ++i) r[4 + 4 * wi + i] = static_cast<uint8_t>(at >> (8 * i));
        }
    }
    dax::MemorySource src(f.data(), f.size());
    journal::Info info;
    CHECK(journal::read_info(src, info) && info.widths == 2 && info.entries == 3 && info.width[1] == 470);
    CHECK(journal::pick_width(info, 320) == 0 && journal::pick_width(info, 480) == 1 && journal::pick_width(info, 200) == 0);
    journal::Picture p;
    CHECK(!journal::find(src, info, 'T', 31, 0, p));
    CHECK(journal::find(src, info, 'J', 31, 1, p) && p.w == 470 && p.h == 22 && p.palette[3] == 0x3333);
    RowCheck rc{2 * 5 + 1, 470, 5, 0, true};
    CHECK(journal::rows(src, p, 5, 10, row_check, &rc) && rc.ok && rc.seen == 10);
    RowCheck all{2 * 5 + 1, 470, 0, 0, true};
    CHECK(journal::rows(src, p, 0, 1000, row_check, &all) && all.ok && all.seen == 22);
    // Data that ends early is bad
    journal::Picture cut = p;
    cut.data_len = 10;
    RowCheck rc2{2 * 5 + 1, 470, 0, 0, true};
    CHECK(!journal::rows(src, cut, 0, 22, row_check, &rc2));
    Bytes notj = {'G', 'B', 'J', '2', 1, 1, 0, 0, 0, 0};
    dax::MemorySource nsrc(notj.data(), notj.size());
    CHECK(!journal::read_info(nsrc, info));

    // Mentions in the games' text
    int n = 0;
    CHECK(journal::find_mention("YOU LISTEN, AND YOU RECORD IT IN JOURNAL ENTRY 31. 'PERHAPS", &n) == 'J' && n == 31);
    CHECK(journal::find_mention("YOU PLACE IT IN YOUR JOURNAL AS ENTRY 59.", &n) == 'J' && n == 59);
    CHECK(journal::find_mention("LOG IT AS JOURNAL \nENTRY 5.", &n) == 'J' && n == 5);
    CHECK(journal::find_mention("YOU OVERHEAR TAVERN TALE #12 ", &n) == 'T' && n == 12);
    CHECK(journal::find_mention("YOU OVERHEAR TAVERN TALE 7", &n) == 'T' && n == 7);
    CHECK(journal::find_mention("AND YOU RECORD IT IN JOURNAL ENTRY ", &n) == 0);
    CHECK(journal::find_mention("TAVERN TALES WITH YOU,", &n) == 0);
}

static Bytes make_geo(const std::vector<std::tuple<int, int, int, int, int>>& walls)
{
    // walls: x, y, dir, type, door
    Bytes raw(1026, 0);
    uint8_t* pl = raw.data() + 2;
    for (const auto& w : walls) {
        const int x = std::get<0>(w), y = std::get<1>(w), d = std::get<2>(w), t = std::get<3>(w), door = std::get<4>(w);
        const int i = x + y * 16;
        if (d == 0) pl[i] = static_cast<uint8_t>((pl[i] & 0x0F) | t << 4);
        if (d == 2) pl[i] = static_cast<uint8_t>((pl[i] & 0xF0) | t);
        if (d == 4) pl[256 + i] = static_cast<uint8_t>((pl[256 + i] & 0x0F) | t << 4);
        if (d == 6) pl[256 + i] = static_cast<uint8_t>((pl[256 + i] & 0xF0) | t);
        pl[768 + i] = static_cast<uint8_t>(pl[768 + i] | door << d);
    }
    pl[512 + 3 + 4 * 16] = 0x90;
    return raw;
}

static void test_geo_view()
{
    // A corridor: the party at (5, 9) facing north; a wall type 2 across the
    // front of (5, 7) - two squares ahead; a door (locked) east of (5, 9)
    const Bytes g = make_geo({{5, 7, 0, 2, 0}, {5, 9, 2, 1, 2}, {5, 9, 6, 1, 1}, {0, 0, 0, 3, 0}});
    const Bytes file = make_dax({{40, g}, {41, Bytes(10, 0)}});
    dax::MemorySource src(file.data(), static_cast<uint32_t>(file.size()));
    static dax::Index idx;
    CHECK(dax::read_index(src, idx) == dax::Status::Ok);
    static geo::Map m;
    CHECK(!geo::load(src, idx, 41, m));
    CHECK(geo::load(src, idx, 40, m) && m.loaded);
    CHECK(geo::wall(m, 5, 7, geo::North) == 2 && geo::wall(m, 5, 7, geo::East) == 0);
    CHECK(geo::passage(m, 5, 9, geo::East) == 2 && geo::passage(m, 5, 9, geo::West) == 1 && geo::passage(m, 5, 9, geo::North) == 1);
    CHECK(geo::door(m, 5, 9, geo::East) == 2 && geo::flags(m, 3, 4) == 0x90);
    CHECK(geo::wall(m, 16, 16, geo::North) == 3 && geo::wall(m, -16, 0, geo::North) == 3);      // wraps
    CHECK(geo::dx(geo::East) == 1 && geo::dy(geo::North) == -1 && strcmp(geo::dir_name(5), "SW") == 0);

    // A world: common tile n is solid colour n % 16 (colour 13 -> 12); wall
    // set 1, piece 2 (type 2) uses tile 9 everywhere, set 2 its own tile 46
    static view3d::World w;
    Bytes common;
    put16(common, 8); put16(common, 1); put16(common, 0); put16(common, 0); common.push_back(45);
    for (int i = 0; i < 8; ++i) common.push_back(0);
    for (int t = 0; t < 45; ++t) {
        int c = (t + 1) % 16;
        if (c == 13) c = 12;
        for (int b = 0; b < 32; ++b) common.push_back(static_cast<uint8_t>(c << 4 | c));
    }
    Bytes wd(780 * 2, 0);
    for (int k = 0; k < 156; ++k) wd[1 * 156 + k] = 9;          // set 1 piece 1 (type 2)
    for (int k = 0; k < 156; ++k) wd[780 + 0 * 156 + k] = 46;   // set 2 piece 0 (type 6): own tile 1
    const Bytes tiles = make_dax({{203, common}, {7, wd}});
    dax::MemorySource ts(tiles.data(), static_cast<uint32_t>(tiles.size()));
    CHECK(dax::read_index(ts, idx) == dax::Status::Ok);
    CHECK(view3d::load_tiles(ts, idx, 203, w.common) && w.common.count == 45);
    int n = 0;
    CHECK(!view3d::load_walls(ts, idx, 3, 7, w, &n));              // 2 parts don't fit from set 3
    CHECK(view3d::load_walls(ts, idx, 1, 7, w, &n) && n == 2 && w.walls[0].loaded && w.walls[1].loaded);
    CHECK(w.walls[1].id[0][0] == 46);                              // part 2 numbers its own tiles already
    CHECK(view3d::tiles_block(7, 1, 0) == 7 && view3d::tiles_block(14, 2, 1) == 142);

    static uint8_t px[pic::kScreenW * pic::kScreenH];
    pic::Canvas c{px, pic::kScreenW, pic::kScreenH};
    c.clear(1);
    view3d::draw(c, w, m, 5, 9, geo::North, 11);
    auto at = [&](int x, int y) { return px[y * pic::kScreenW + x]; };
    CHECK(at(30, 30) == 11 && at(30, 100) == 8 && at(30, 24 + 45) == 0);   // sky, ground, the line
    CHECK(at(1, 1) == 1);                                          // outside the view: untouched
    // The far front wall: view cells rows 4-5, column 5 = pixels (64-71, 56-71)
    CHECK(at(67, 60) == 9 && at(67, 70) == 9);
    // Turned east: the locked door is in the near right side wall (group 8)
    c.clear(1);
    view3d::draw(c, w, m, 5, 9, geo::East, 0);
    CHECK(at(30, 30) == 0);                                        // indoor sky
    // Type 1 = set 1 piece 0: not filled in (no tiles) -> nothing drawn there
    // Area map without frame tiles: nothing; with: arrow at the party
    static layout::Tiles ft;
    for (int t = 0; t < layout::kTiles; ++t)
        for (int i = 0; i < 64; ++i) ft.px[t][i] = static_cast<uint8_t>(t < 4 ? 14 : (t - 4) % 16 == 13 ? 12 : (t - 4) % 16);
    ft.loaded = true;
    w.frame = &ft;
    c.clear(1);
    view3d::draw_area_map(c, w, m, 5, 9, geo::East);
    // window: x from 0, y from 4 (9 - 5); the party at cell (5, 5) of the window
    CHECK(at(24 + 5 * 8 + 3, 24 + 5 * 8 + 3) == 14);
    // (5, 7) has a north wall: map piece 4 + 1 -> colour 1, at window (5, 3)
    CHECK(at(24 + 5 * 8 + 3, 24 + 3 * 8 + 3) == 1);
    CHECK(at(24 + 2 * 8 + 3, 24 + 2 * 8 + 3) == 0);                // nothing there: piece 4 -> colour 0
}

int main()
{
    test_rle_known_bytes();
    test_round_trip();
    test_bad_files();
    test_picture();
    test_anim();
    test_vga();
    test_font();
    test_games();
    test_exepack();
    test_layout();
    test_text();
    test_menu();
    test_printcalls();
    test_inflate();
    test_png();
    test_icon();
    test_ecl();
    test_ecl_vm();
    test_journal();
    test_geo_view();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_dax: all checks passed\n");
    return 0;
}

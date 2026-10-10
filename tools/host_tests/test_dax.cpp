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

#include "engine/classes.h"
#include "engine/create.h"
#include "engine/dax.h"
#include "engine/ecl.h"
#include "engine/ecl_vm.h"
#include "engine/exepack.h"
#include "engine/geo.h"
#include "engine/icon.h"
#include "engine/inflate.h"
#include "engine/journal.h"
#include "engine/jpeg.h"
#include "engine/pdf.h"
#include "jpeg_vectors.h"
#include "engine/png.h"
#include "inflate_vectors.h"
#include "engine/layout.h"
#include "engine/items.h"
#include "engine/magic.h"
#include "engine/party.h"
#include "engine/rules.h"
#include "engine/savegame.h"
#include "engine/printcalls.h"
#include "engine/profile.h"
#include "engine/text.h"
#include "engine/view3d.h"
#include "engine/font.h"
#include "engine/games.h"
#include "engine/picture.h"
#include "engine/spells.h"
#include "engine/combat.h"

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
    int sprite_id = -1, sprite_dist = -1, sprites = 0, items_block = -1;
    void load_items(int block, items::Ground&) override { items_block = block; }
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

// The sound driver: made-up byte code in the games' layout - a PC-speaker
// tone and a Tandy tone with a loop and a noise voice
static void test_sound()
{
    std::vector<uint8_t> seg(0x100, 0);              // offsets 0x100-0x1FF
    auto w16 = [&](int off, int v) { seg[off - 0x100] = static_cast<uint8_t>(v); seg[off - 0x100 + 1] = static_cast<uint8_t>(v >> 8); };
    auto ops = [&](int off, std::initializer_list<int> b) { for (int v : b) seg[off++ - 0x100] = static_cast<uint8_t>(v); };
    w16(0x100 + 8, 0x140);                           // PC table, sound 1, voice 0
    w16(0x110 + 8, 0x160);                           // Tandy table, sound 1, voice 0
    w16(0x110 + 8 + 6, 0x180);                       // ... voice 3 (noise)
    ops(0x140, {0xFF, 0x0A, 3, 0, 0xFF, 0x04, 0xE8, 0x03, 0xFF, 0x00, 5, 0, 0xFF, 0x0A, 0, 0, 0xFF, 0x00, 0, 0});
    ops(0x160, {0xFF, 0x0A, 0xFF, 0xFF, 0xFF, 0x04, 0x00, 0x10, 0xFF, 0x26, 3, 0,
                0xFF, 0x00, 2, 0, 0xFE, 0x26, 0x6C, 0x01, 0xFF, 0x0A, 0, 0, 0xFF, 0x00, 0, 0});
    ops(0x180, {0xFF, 0x04, 0x00, 0x01, 0xFF, 0x0A, 0x00, 0x80, 0xFF, 0x00, 4, 0, 0xFF, 0x0A, 0, 0, 0xFF, 0x00, 0, 0});
    const sound::Layout lay{0, 0x100, 0x200, 0x100, 0x110, 2, 0x2F, 0x33};
    sound::Player p;
    CHECK(p.set_data(lay, seg.data(), seg.size()));
    // The PC speaker: the first tick quiet (the games' divisor is still 0), then 1193182 / 1000 Hz for 4 ticks
    p.start(2, sound::Device::PcSpeaker);
    p.tick();
    CHECK(!p.out().speaker_on && p.busy());
    for (int k = 0; k < 4; ++k) {
        p.tick();
        CHECK(p.out().speaker_on && p.out().divisor == 1000);
    }
    p.tick();
    CHECK(!p.out().speaker_on && !p.busy());
    // Tandy: tone N 64 at full volume for 3 loops of 2 ticks; the noise white, the fastest, 14 dB down
    p.start(2, sound::Device::Tandy);
    p.tick();
    int on = 0;
    for (int k = 0; k < 10; ++k) {
        p.tick();
        if (p.out().atten[0] == 0) {
            ++on;
            CHECK(p.out().tone[0] == 64);
        }
        if (k == 0) CHECK(p.out().noise == 4 && p.out().atten[3] == 7);
    }
    CHECK(on == 5 && !p.busy());                     // (6 ticks of the loop; the first heard from the second)
    // Rendering: sound, then silence and done
    std::vector<uint8_t> buf(4096);
    p.start(2, sound::Device::Tandy);
    CHECK(p.render(buf.data(), 512, 22050, 255));
    bool moved = false;
    for (int i = 0; i < 512; ++i)
        if (buf[i] != 128) moved = true;
    CHECK(moved);
    bool more = true;
    for (int k = 0; k < 20 && more; ++k) more = p.render(buf.data(), 512, 22050, 255);
    CHECK(!more && buf[511] == 128);
    // A bad pointer in a table: refused
    w16(0x100 + 8, 0x300);
    sound::Player bad;
    CHECK(!bad.set_data(lay, seg.data(), seg.size()) && !bad.ready());
}

// Random treasure: a thousand items made with the Curse facts and made-up
// ready-made rows; each is one of the kinds, with its words, plus and weight
static void test_treasure()
{
    const profile::Profile* pr = profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432);
    CHECK(pr && pr->random_items);
    if (!pr || !pr->random_items) return;
    const treasure::Facts& f = *pr->random_items;
    treasure::Rows rows{};
    for (int r = 0; r < 7; ++r)
        for (int k = 0; k < 8; ++k) rows.r[r][k] = static_cast<uint16_t>(r * 10 + k + 1);
    create::Dice d(7);
    uint8_t it[items::kRecordSize];
    int scrolls = 0, potions = 0, weapons = 0, bracers = 0;
    for (int i = 0; i < 1000; ++i) {
        treasure::make(it, f, rows, d);
        const int t = it[0x2E];
        CHECK(it[0x36] == 0 && it[0x34] == 0);                 // never cursed or readied
        if (t == f.mu_scroll || t == f.cleric_scroll) {
            ++scrolls;
            const int n = it[0x30] - f.with_word;
            CHECK(n >= 1 && n <= 3 && it[0x32] == 1 && it[0x3C + n - 1] != 0);
            if (n < 3) CHECK(it[0x3C + n] == 0);
            for (int k = 0; k < n; ++k)
                if (t == f.cleric_scroll) CHECK(it[0x3C + k] >= 1 && it[0x3C + k] <= 0x4C);
        } else if (t == f.potion || t == f.giant || t == f.wand) {
            ++potions;
            CHECK(it[0x32] == 1 && it[0x33] == 1);
            const int row = it[0x2F] / 10;                     // the made-up rows' first word: row x 10 + 1
            CHECK((t == f.potion && (row == 0 || row == 2)) || (t == f.giant && row == 1) || (t == f.wand && row == 4));
        } else if (t == f.bracers) {
            ++bracers;
            CHECK((it[0x32] == 4 && it[0x2F] == f.ac6_word) || (it[0x32] == 6 && it[0x2F] == f.ac4_word));
        } else {
            ++weapons;
            CHECK(t >= 1 && t <= 0x5D && t != f.heavy_crossbow);
            CHECK(it[0x32] == 1 || it[0x32] == 2);               // (a javelin of lightning: row 6, +1)
            CHECK((it[0x37] | it[0x38] << 8) > 0);
        }
    }
    // About 25% magic-user scrolls, 7% clerics', 6% potions / wands, 60% the rest
    CHECK(scrolls > 250 && scrolls < 400 && potions > 25 && potions < 100 && weapons > 500 && bracers > 0);
}

// The scripts and the party: LOAD CHARACTER, the selected character's fields
// written, ROB, DAMAGE, FIND ITEM, DESTROY ITEMS, SPELL, WHO, ADD NPC, DUMP
struct PartyHost : TestHost {
    party::Party* p = nullptr;
    int changed = 0, killed = 0;
    bool add_npc(int id) override
    {
        if (p->count >= party::kMaxParty) return false;
        party::Character& c = p->m[p->count] = party::Character{};
        c.rec[0] = 3; c.rec[1] = 'N'; c.rec[2] = 'P'; c.rec[3] = 'C';
        c.rec[0x126] = static_cast<uint8_t>(id);
        p->selected = p->count++;
        return true;
    }
    void party_changed() override { ++changed; }
    void party_killed() override { ++killed; }
};

static void test_ecl_party()
{
    const profile::Profile* pr = profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432);
    CHECK(pr && pr->ecl_ops);
    if (!pr || !pr->ecl_ops) return;
    static party::Party pa;
    pa = party::Party{};
    pa.count = 3;
    for (int i = 0; i < 3; ++i) {
        party::Character& c = pa.m[i];
        c.rec[0] = 1; c.rec[1] = static_cast<uint8_t>('A' + i);
        c.rec[0x196] = 1; c.rec[0x1A4] = c.rec[0x78] = 20;
        c.rec[0x101] = 100;                                   // 100 gold
        for (int k = 0; k < 5; ++k) c.rec[0xDF + k] = 30;     // never saves (but on a 20)
    }
    pa.m[1].n_items = 2;
    pa.m[1].items[0][0x2E] = 40; pa.m[1].items[1][0x2E] = 41;
    pa.m[2].rec[0x1E + 4] = 7;                                // member 2 has spell 7 in place 4 (record 0x1E + 4)
    // The script:
    //   LOAD CHARACTER 1; COMPARE [0x7D00] 1; SAVE 250 -> 0x7CC1 (gold); LOAD CHARACTER 9;
    //   SAVE [0x7D00] -> 0x4C00 (0: not found); SAVE [0x7D00] -> 0x4C01 (again: 1);
    //   FIND ITEM 41; IF= SAVE 1 -> 0x4C02; DESTROY ITEMS 41; FIND ITEM 41; IF= SAVE 1 -> 0x4C03;
    //   SPELL 7 0x4C04 0x4C05; ROB 1 50 0; DAMAGE 0xE0 1 1 4 0 (everyone, 5 points, no save);
    //   WHO "WHO"; ADD NPC 22 100; EXIT
    Bytes c;
    const uint16_t kBase = 0x8000;
    for (int i = 0; i < 5; ++i) { c.push_back(0); op_addr(c, 0); }
    const size_t first = c.size();
    c.push_back(0x0A); op_imm(c, 1);
    c.push_back(0x09); op_imm(c, 250); op_addr(c, 0x7CC1);
    c.push_back(0x0A); op_imm(c, 9);
    c.push_back(0x09); op_addr(c, 0x7D00); op_addr(c, 0x4C00);
    c.push_back(0x09); op_addr(c, 0x7D00); op_addr(c, 0x4C01);
    c.push_back(0x32); op_imm(c, 41);
    c.push_back(0x16); c.push_back(0x09); op_imm(c, 1); op_addr(c, 0x4C02);
    c.push_back(0x40); op_imm(c, 41);
    c.push_back(0x32); op_imm(c, 41);
    c.push_back(0x16); c.push_back(0x09); op_imm(c, 1); op_addr(c, 0x4C03);
    c.push_back(0x3B); op_imm(c, 7); op_addr(c, 0x4C04); op_addr(c, 0x4C05);
    c.push_back(0x28); op_imm(c, 1); op_imm(c, 50); op_imm(c, 0);
    c.push_back(0x2E); op_imm(c, 0xE0); op_imm(c, 1); op_imm(c, 1); op_imm(c, 4); op_imm(c, 0);
    c.push_back(0x39); op_str(c, "WHO");
    c.push_back(0x36); op_imm(c, 22); op_imm(c, 100);
    c.push_back(0x00);
    for (int i = 0; i < 5; ++i) {
        c[1 + i * 4] = 1;
        c[2 + i * 4] = static_cast<uint8_t>((kBase + first) & 0xFF);
        c[3 + i * 4] = static_cast<uint8_t>((kBase + first) >> 8);
    }
    ecl::GameState gs;
    PartyHost host;
    host.p = &pa;
    memcpy(gs.code, c.data(), c.size());
    gs.code_len = static_cast<uint32_t>(c.size());
    static const char* const kWords[ecl::kScriptWords] = {"", "", " dies. ", " is hit FOR ", " points of Damage.",
                                                         "killed"};
    static ecl::Vm vm(gs, host, *pr->ecl_ops);
    vm.set_party(&pa);
    vm.set_words(kWords);
    CHECK(vm.init_script());
    ecl::Stop r = vm.run(vm.entry(4));
    // DAMAGE: a line each ("\n" before it), then "press <enter>"
    int lines = 0;
    while (r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Print) {
        if (strcmp(vm.text(), "\n") != 0) {
            CHECK(strstr(vm.text(), " is hit FOR 5 points of Damage.") != nullptr);
            ++lines;
        }
        r = vm.resume();
    }
    CHECK(lines == 3 && r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Key);
    CHECK(pa.m[1].rec[0x101] == 250 / 2 && pa.m[0].rec[0x101] == 50);   // gold set, then half robbed
    CHECK(vm.get(0x4C00) == 0 && vm.get(0x4C01) == 1);                  // not found once
    CHECK(vm.get(0x4C02) == 1 && vm.get(0x4C03) == 0 && pa.m[1].n_items == 1 && pa.m[1].items[0][0x2E] == 40);
    CHECK(vm.get(0x4C04) == 4 && vm.get(0x4C05) == 2);
    CHECK(pa.m[0].hp() == 15 && pa.m[2].hp() == 15);
    r = vm.resume();
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Who && strcmp(vm.prompt(), "WHO") == 0);
    r = vm.answer(2);
    // ADD NPC: the new member, selected, an NPC with morale 100 / 2; EXIT puts back LOAD CHARACTER's choice
    CHECK(r == ecl::Stop::Stopped && pa.count == 4 && pa.m[3].rec[0xF7] == (0x80 | 50) && vm.get(0x7F3E) == 4);
    CHECK(pa.selected == 0);                                             // restored to the start's
    CHECK(host.changed >= 3 && host.killed == 0);
    // Removal: LOAD CHARACTER i, SAVE 0 -> 0x7C00, SAVE 0 -> 0x7D00, LOAD CHARACTER i + 0x80; DUMP
    Bytes c2;
    for (int i = 0; i < 5; ++i) { c2.push_back(0); op_addr(c2, 0); }
    const size_t f2 = c2.size();
    c2.push_back(0x0A); op_imm(c2, 3);
    c2.push_back(0x09); op_imm(c2, 0); op_addr(c2, 0x7C00);
    c2.push_back(0x09); op_imm(c2, 0); op_addr(c2, 0x7D00);
    c2.push_back(0x0A); op_imm(c2, 0x83);
    c2.push_back(0x3E);
    c2.push_back(0x00);
    for (int i = 0; i < 5; ++i) {
        c2[1 + i * 4] = 1;
        c2[2 + i * 4] = static_cast<uint8_t>((kBase + f2) & 0xFF);
        c2[3 + i * 4] = static_cast<uint8_t>((kBase + f2) >> 8);
    }
    memcpy(gs.code, c2.data(), c2.size());
    gs.code_len = static_cast<uint32_t>(c2.size());
    CHECK(vm.init_script());
    pa.m[3].rec[0x196] = 1;
    r = vm.run(vm.entry(4));
    // The NPC (member 3) gone; the one before it (member 2) selected, then dumped too
    CHECK(r == ecl::Stop::Stopped && pa.count == 2 && vm.get(0x7F3E) == 2);
    // Everyone killed: "killed", press <enter>, the host told
    for (int i = 0; i < pa.count; ++i) pa.m[i].rec[0x1A4] = 3;
    Bytes c3;
    for (int i = 0; i < 5; ++i) { c3.push_back(0); op_addr(c3, 0); }
    const size_t f3 = c3.size();
    c3.push_back(0x2E); op_imm(c3, 0xE0); op_imm(c3, 1); op_imm(c3, 1); op_imm(c3, 19); op_imm(c3, 0);
    c3.push_back(0x00);
    for (int i = 0; i < 5; ++i) {
        c3[1 + i * 4] = 1;
        c3[2 + i * 4] = static_cast<uint8_t>((kBase + f3) & 0xFF);
        c3[3 + i * 4] = static_cast<uint8_t>((kBase + f3) >> 8);
    }
    memcpy(gs.code, c3.data(), c3.size());
    gs.code_len = static_cast<uint32_t>(c3.size());
    CHECK(vm.init_script());
    r = vm.run(vm.entry(4));
    bool said_killed = false, said_dies = false;
    while (r == ecl::Stop::Waiting) {
        if (vm.wait() == ecl::Wait::Print && strcmp(vm.text(), "killed") == 0) said_killed = true;
        if (vm.wait() == ecl::Wait::Print && strstr(vm.text(), " dies. ")) said_dies = true;
        r = vm.resume();
    }
    CHECK(said_killed && said_dies && host.killed == 1 && pa.m[0].health() == party::Dead);
    // PROGRAM 8 (the game won): the ending waits, the script is over; PROGRAM 5: nothing at all
    Bytes c4;
    for (int i = 0; i < 5; ++i) { c4.push_back(0); op_addr(c4, 0); }
    const size_t f4 = c4.size();
    c4.push_back(0x38); op_imm(c4, 5);
    c4.push_back(0x09); op_imm(c4, 7); op_addr(c4, 0x4C06);
    c4.push_back(0x38); op_imm(c4, 8);
    c4.push_back(0x09); op_imm(c4, 9); op_addr(c4, 0x4C07);
    c4.push_back(0x00);
    for (int i = 0; i < 5; ++i) {
        c4[1 + i * 4] = 1;
        c4[2 + i * 4] = static_cast<uint8_t>((kBase + f4) & 0xFF);
        c4[3 + i * 4] = static_cast<uint8_t>((kBase + f4) >> 8);
    }
    memcpy(gs.code, c4.data(), c4.size());
    gs.code_len = static_cast<uint32_t>(c4.size());
    CHECK(vm.init_script());
    r = vm.run(vm.entry(4));
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Won && vm.get(0x4C06) == 7);
    r = vm.resume();
    CHECK(r == ecl::Stop::Stopped && vm.get(0x4C07) != 9);
}

// AND / OR: the flags as the original sets them - 0 against the result
static void test_vm_and()
{
    const profile::Profile* p = profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432);
    if (!p || !p->ecl_ops) return;
    Bytes c;
    const uint16_t kBase = 0x8000;
    for (int i = 0; i < 5; ++i) { c.push_back(0); op_addr(c, 0); }
    const size_t first = c.size();
    c.push_back(0x2F); op_imm(c, 3); op_imm(c, 1); op_addr(c, 0x4C00);
    c.push_back(0x00);
    for (int i = 0; i < 5; ++i) {
        c[1 + i * 4] = 1;
        c[2 + i * 4] = static_cast<uint8_t>((kBase + first) & 0xFF);
        c[3 + i * 4] = static_cast<uint8_t>((kBase + first) >> 8);
    }
    static ecl::GameState gs;
    gs = ecl::GameState{};
    TestHost host;
    memcpy(gs.code, c.data(), c.size());
    gs.code_len = static_cast<uint32_t>(c.size());
    ecl::Vm vm(gs, host, *p->ecl_ops);
    CHECK(vm.init_script());
    vm.run(vm.entry(0));
    CHECK(vm.get(0x4C00) == 1 && !vm.flag(0) && vm.flag(1) && vm.flag(2) && !vm.flag(3) && vm.flag(4) && !vm.flag(5));

    // ON GOTO with a long table (more than an instruction's operands): the
    // 26th place; past the table when out of range
    for (int sel : {25, 40}) {
        Bytes g;
        for (int i = 0; i < 5; ++i) { g.push_back(0); op_addr(g, 0); }
        const size_t at = g.size();
        g.push_back(0x25); op_imm(g, sel); op_imm(g, 30);
        std::vector<size_t> slots;
        for (int i = 0; i < 30; ++i) { slots.push_back(g.size()); op_addr(g, 0); }
        const size_t after = g.size();
        g.push_back(0x09); op_imm(g, 7); op_addr(g, 0x4C01);          // (out of range: on to here)
        g.push_back(0x00);
        const size_t hit = g.size();
        g.push_back(0x09); op_imm(g, 9); op_addr(g, 0x4C01);
        g.push_back(0x00);
        for (int i = 0; i < 30; ++i) {
            const size_t t = i == 25 ? hit : after;
            g[slots[i] + 1] = static_cast<uint8_t>((kBase + t) & 0xFF);
            g[slots[i] + 2] = static_cast<uint8_t>((kBase + t) >> 8);
        }
        for (int i = 0; i < 5; ++i) {
            g[1 + i * 4] = 1;
            g[2 + i * 4] = static_cast<uint8_t>((kBase + at) & 0xFF);
            g[3 + i * 4] = static_cast<uint8_t>((kBase + at) >> 8);
        }
        gs = ecl::GameState{};
        memcpy(gs.code, g.data(), g.size());
        gs.code_len = static_cast<uint32_t>(g.size());
        ecl::Vm v2(gs, host, *p->ecl_ops);
        CHECK(v2.init_script());
        CHECK(v2.run(v2.entry(0)) == ecl::Stop::Stopped);
        CHECK(v2.get(0x4C01) == (sel == 25 ? 9 : 7));
    }
}

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
    static const char* const kWords[ecl::kScriptWords] = {"Both sides wait.", "The monsters flee.", " dies. ",
                                                         " is hit FOR ", " points of Damage.", "killed"};
    vm.set_words(kWords);
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

// A small PDF like the GOG journals: each page one JPEG (synthetic ones)
struct MemOut : journal::Output {
    Bytes b;
    bool write_at(uint32_t pos, const uint8_t* d, size_t n) override
    {
        if (b.size() < pos + n) b.resize(pos + n);
        memcpy(b.data() + pos, d, n);
        return true;
    }
};

static Bytes make_pdf()
{
    std::string s = "%PDF-1.4\n%\xe2\xe3\xcf\xd3\n";
    std::vector<size_t> off(10, 0);
    auto obj = [&](int n, const std::string& body) {
        off[n] = s.size();
        s += std::to_string(n) + " 0 obj\n" + body + "\nendobj\n";
    };
    auto image = [&](int n, int w, int h, const uint8_t* data, size_t len, int len_obj) {
        off[n] = s.size();
        s += std::to_string(n) + " 0 obj\n<</Type/XObject/Subtype/Image/Width " + std::to_string(w) + "/Height " +
             std::to_string(h) + "/BitsPerComponent 8/Filter/DCTDecode/ColorSpace/DeviceRGB/Length " +
             std::to_string(len_obj) + " 0 R>>stream\r\n";
        s.append(reinterpret_cast<const char*>(data), len);
        s += "\r\nendstream\nendobj\n";
        off[len_obj] = s.size();
        s += std::to_string(len_obj) + " 0 obj\n" + std::to_string(len) + "\nendobj\n";
    };
    obj(1, "<</Type/Catalog/Pages 2 0 R>>");
    obj(2, "<</Type/Pages/Kids[3 0 R 4 0 R]/Count 2/Resources<</XObject<</Im9 7 0 R>>>>>>");
    obj(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 31 23]/Resources<</XObject<</Im1 5 0 R>>/ProcSet[/PDF/ImageC]>>>>");
    obj(4, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 20 16]>>");       // inherits its /Resources
    image(5, kJpeg0W, kJpeg0H, kJpeg0, sizeof kJpeg0, 6);
    image(7, kJpeg1W, kJpeg1H, kJpeg1, sizeof kJpeg1, 8);
    const size_t xref = s.size();
    s += "xref\n0 9\n0000000000 65535 f \n";
    for (int n = 1; n <= 8; ++n) {
        char e[24];
        snprintf(e, sizeof e, "%010zu 00000 n \n", off[n]);
        s += e;
    }
    s += "trailer\n<</Size 9/Root 1 0 R/ID[<0A1B2C3D4E5F60718293A4B5C6D7E8F9><00112233445566778899AABBCCDDEEFF>]>>\n";
    s += "startxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return Bytes(s.begin(), s.end());
}

static void test_journal()
{
    // JPEG: decodes close to what PIL makes of it, and want() skips MCUs
    {
        dax::MemorySource src(kJpeg0, sizeof kJpeg0);
        static uint8_t pool[jpeg::kPoolSize];
        jpeg::Info info;
        CHECK(jpeg::probe(src, 0, sizeof kJpeg0, pool, info) && info.width == kJpeg0W && info.height == kJpeg0H &&
              info.mcu_w == 16 && info.mcu_h == 16);
        struct Got { std::vector<uint8_t> rgb; int blocks = 0; } got;
        got.rgb.assign(kJpeg0W * kJpeg0H * 3, 0);
        auto block = [](int x, int y, int w, int h, const uint8_t* rgb, void* ctx) {
            Got& g = *static_cast<Got*>(ctx);
            ++g.blocks;
            for (int r = 0; r < h; ++r)
                memcpy(&g.rgb[((y + r) * kJpeg0W + x) * 3], rgb + r * w * 3, w * 3);
            return true;
        };
        CHECK(jpeg::decode(src, 0, sizeof kJpeg0, pool, nullptr, block, &got));
        long diff = 0;
        for (size_t i = 0; i < got.rgb.size(); ++i) diff += std::abs(got.rgb[i] - kJpeg0Rgb[i]);
        CHECK(got.blocks == 12 && diff / static_cast<long>(got.rgb.size()) < 4);
        // Only the top-left MCU, then stop
        Got one;
        one.rgb.assign(kJpeg0W * kJpeg0H * 3, 0);
        auto want = [](int x, int y, int, int, void*) { return y > 0 ? -1 : (x == 0 ? 1 : 0); };
        CHECK(jpeg::decode(src, 0, sizeof kJpeg0, pool, want, block, &one) && one.blocks == 1);
    }

    // PDF: the pages in order, their pictures (the second inherits /Resources)
    Bytes pdf_bytes = make_pdf();
    dax::MemorySource psrc(pdf_bytes.data(), pdf_bytes.size());
    static pdf::Doc doc;
    CHECK(pdf::open(psrc, doc) && doc.root == 1 && strcmp(doc.id, "0a1b2c3d4e5f60718293a4b5c6d7e8f9") == 0);
    int pages[8];
    CHECK(pdf::pages(psrc, doc, pages, 8) == 2 && pages[0] == 3 && pages[1] == 4);
    pdf::Image im;
    CHECK(pdf::page_image(psrc, doc, 3, im) && im.jpeg && im.width == kJpeg0W && im.data_len == sizeof kJpeg0 &&
          memcmp(pdf_bytes.data() + im.data_at, kJpeg0, sizeof kJpeg0) == 0);
    CHECK(pdf::page_image(psrc, doc, 4, im) && im.width == kJpeg1W && im.data_len == sizeof kJpeg1);
    Bytes notpdf = {'%', 'P', 'S', '!'};
    dax::MemorySource nsrc(notpdf.data(), notpdf.size());
    CHECK(!pdf::open(nsrc, doc));
    CHECK(pdf::open(psrc, doc));

    // JOURNAL.DAT from a table: J1 = two pieces on page 1, T7 = one on page 2
    static const journal::Piece kPieces[] = {{1, 2, 3, 40, 20}, {1, 30, 30, 20, 18}, {2, 0, 5, 40, 27}};
    static const journal::EntryDef kEntries[] = {{'J', 1, 0, 2}, {'T', 7, 2, 1}};
    static const journal::Table kTable = {"test", static_cast<uint32_t>(pdf_bytes.size()),
                                          "0a1b2c3d4e5f60718293a4b5c6d7e8f9", kEntries, 2, kPieces, 3};
    MemOut out;
    int calls = 0;
    auto prog = [](int done, int total, void* ctx) { ++*static_cast<int*>(ctx); (void)done; (void)total; };
    CHECK(journal::make(psrc, kTable, out, prog, &calls) && calls == 2);
    dax::MemorySource jsrc(out.b.data(), out.b.size());
    journal::Info info;
    CHECK(journal::read_info(jsrc, info) && info.entries == 2 && info.pieces == 3 &&
          strcmp(info.pdf_id, "0a1b2c3d4e5f60718293a4b5c6d7e8f9") == 0);
    int first = -1, count = 0;
    CHECK(!journal::find(jsrc, info, 'J', 7, &first, &count));
    CHECK(journal::find(jsrc, info, 'T', 7, &first, &count) && first == 2 && count == 1);
    CHECK(journal::find(jsrc, info, 'J', 1, &first, &count) && first == 0 && count == 2);
    // Each piece holds the page's pixels there, as index_of() stores them
    const struct { const uint8_t* rgb; int w; } page_rgb[2] = {{kJpeg0Rgb, kJpeg0W}, {kJpeg1Rgb, kJpeg1W}};
    for (int i = 0; i < 3; ++i) {
        journal::PieceInfo pi;
        CHECK(journal::piece(jsrc, info, i, pi) && pi.w == kPieces[i].w && pi.h == kPieces[i].h);
        int close = 0, total = 0;
        for (int y = 0; y < pi.h; ++y)
            for (int x = 0; x < pi.w; ++x) {
                const uint8_t* p = page_rgb[kPieces[i].page - 1].rgb +
                                   ((kPieces[i].y + y) * page_rgb[kPieces[i].page - 1].w + kPieces[i].x + x) * 3;
                uint8_t r, g, b;
                journal::palette_rgb(out.b[pi.offset + y * pi.w + x], &r, &g, &b);
                const uint8_t want = journal::index_of(p[0], p[1], p[2]);
                uint8_t wr, wg, wb;
                journal::palette_rgb(want, &wr, &wg, &wb);
                close += std::abs(r - wr) + std::abs(g - wg) + std::abs(b - wb) <= 60;
                ++total;
            }
        CHECK(close * 100 >= total * 97);
    }
    // A table for another PDF: not found
    CHECK(journal::find_table(123, "00") == nullptr);
    // Half made (no "GBJ2"): not read
    Bytes half = out.b;
    half[0] = 0;
    dax::MemorySource hsrc(half.data(), half.size());
    CHECK(!journal::read_info(hsrc, info));
    // Palette: paper is white, ink is kept
    uint8_t r, g, b;
    journal::palette_rgb(journal::index_of(240, 236, 220), &r, &g, &b);
    CHECK(r == 255 && g == 255 && b == 255);
    journal::palette_rgb(journal::index_of(30, 110, 180), &r, &g, &b);
    CHECK(b > r + 60);

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
    // A locked door opened (Bash / Pick): this side and the next square's west side
    {
        static geo::Map u;
        u = m;
        geo::unlock(u, 5, 9, geo::East);
        CHECK(geo::door(u, 5, 9, geo::East) == 1 && geo::door(u, 6, 9, geo::West) == 1);
        CHECK(geo::door(u, 5, 9, geo::West) == geo::door(m, 5, 9, geo::West));     // the rest kept
        geo::unlock(u, 15, 3, geo::East);                                        // off the map: this side only
        CHECK(geo::door(u, 15, 3, geo::East) == 1);
        // Bash by Strength: 25 always, nothing below 3 never; a not-pickable
        // door is too strong for Str 17 (Bash goes); Pick needs a thief
        static party::Party bp;
        bp = party::Party{};
        bp.count = 1;
        bp.m[0].rec[0x11] = 25;
        create::Dice dd(5);
        bool gone = false;
        CHECK(rules::bash_door(bp, 2, dd, &gone) && rules::bash_door(bp, 3, dd, &gone) && !gone);
        bp.m[0].rec[0x11] = 2;
        CHECK(!rules::bash_door(bp, 2, dd, &gone) && !gone);
        bp.m[0].rec[0x11] = 17;
        CHECK(!rules::bash_door(bp, 3, dd, &gone) && gone);
        CHECK(!rules::has_thief(bp));
        bp.m[0].rec[0x109 + 6] = 1;
        CHECK(rules::has_thief(bp));
        bp.m[0].rec[0xEB] = 100;
        CHECK(rules::pick_lock(bp, dd));
        bp.m[0].rec[0x195] = party::Dead;
        CHECK(!rules::pick_lock(bp, dd));
        CHECK(rules::knock_member(bp, 0x1F) == -1);
        bp.m[0].rec[0x1E + 3] = 0x1F;
        CHECK(rules::knock_member(bp, 0x1F) == 0);
    }

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

// A made-up character record, items, effects, a saved game and the
// scripts reading the selected character
static void test_party()
{
    static party::Character ch;
    ch = party::Character{};
    uint8_t rec[party::kRecordSize] = {};
    rec[0] = 5;
    memcpy(rec + 1, "ALICE", 5);
    rec[0x10] = 17; rec[0x11] = 18;        // Str 17 / 18
    rec[0x13] = 15;                         // Int full
    rec[0x74] = 7; rec[0x75] = 2;           // human fighter
    rec[0x76] = 0x2C; rec[0x77] = 0x01;     // age 300
    rec[0x78] = 40; rec[0x1A4] = 31;        // HP 31 / 40
    rec[0x19A] = 55;                        // AC 5
    rec[0x10B] = 6;                         // fighter level 6
    rec[0xEC] = 33;                         // thief skill 3
    rec[0x103] = 0x2C; rec[0x104] = 0x01;   // 300 platinum
    rec[0x127] = 0x10; rec[0x128] = 0x27;   // 10000 xp
    rec[0x196] = 1;
    rec[0x1B] = 16;                         // Cha 16
    {
        dax::MemorySource src(rec, sizeof rec);
        CHECK(party::read_record(src, ch));
        dax::MemorySource shorter(rec, 100);
        party::Character bad;
        CHECK(!party::read_record(shorter, bad));
    }
    char name[20];
    ch.name(name, sizeof name);
    CHECK(strcmp(name, "ALICE") == 0);
    ch.name(name, 3);
    CHECK(strcmp(name, "AL") == 0);
    CHECK(ch.hp() == 31 && ch.hp_max() == 40 && ch.ac() == 5 && ch.race() == 7 && ch.cls() == 2);
    CHECK(ch.age() == 300 && ch.level(2) == 6 && ch.stat(0) == 18 && ch.stat_now(0) == 17);
    CHECK(ch.money(4) == 300 && ch.exp() == 10000 && ch.in_combat() && !ch.npc());

    uint8_t items[party::kItemSize * 2 + 5] = {};
    items[0] = 0xAA;
    items[party::kItemSize] = 0xBB;
    dax::MemorySource isrc(items, sizeof items);
    party::read_items(isrc, ch);
    CHECK(ch.n_items == 2 && ch.items[1][0] == 0xBB);
    uint8_t fx[party::kAffectSize * 3] = {};
    fx[party::kAffectSize * 2] = 0x7E;
    dax::MemorySource fsrc(fx, sizeof fx);
    party::read_affects(fsrc, ch);
    CHECK(ch.n_affects == 3 && ch.affects[2][0] == 0x7E);

    party::Party pt;
    uint16_t v = 0;
    CHECK(!party::script_value(pt, 0x72, &v));        // no party
    pt.m[0] = ch;
    pt.m[1] = ch;
    pt.m[1].rec[0x74] = 2;                             // an elf
    pt.count = 2;
    pt.selected = 1;
    CHECK(party::script_value(pt, 0x72, &v) && v == 2);
    CHECK(party::script_value(pt, 0x73, &v) && v == 2);
    CHECK(party::script_value(pt, 0xA7, &v) && v == 33);
    CHECK(party::script_value(pt, 0xC3, &v) && v == 300);
    CHECK(party::script_value(pt, 0x15, &v) && v == 15);
    CHECK(party::script_value(pt, 0x100, &v) && v == 1);
    CHECK(party::script_value(pt, 0x2B1, &v) && v == 1);
    CHECK(party::script_value(pt, 0x2CF, &v) && v == 50);
    CHECK(!party::script_value(pt, 0x2C9, &v));

    // A saved game
    std::vector<uint8_t> sv(savegame::kSize, 0);
    CHECK(savegame::kSize == 13149);
    sv[0] = 2;
    sv[1 + (0x4BE6 - 0x4B00) * 2] = 1;                // in a 3D area
    sv[1 + 0x800 + (0x7F3E - 0x7C00) * 2] = 2;         // party size
    sv[1 + 0x800 + 0x800 + 0x400] = 0x42;              // first script byte
    size_t o = 1 + 0x800 + 0x800 + 0x400 + 0x1E00;
    sv[o] = 7; sv[o + 1] = 13; sv[o + 2] = 4; sv[o + 3] = 1; sv[o + 4] = 0x80;
    o += 5;
    sv[o] = 0; sv[o + 1] = 1;
    o += 2;
    sv[o] = 3; sv[o + 2] = 1;                          // walls block 3, set 1
    sv[o + 4] = 0xFF; sv[o + 5] = 0xFF;                // none
    o += 12;
    sv[o++] = 2;
    sv[o] = 8; memcpy(&sv[o + 1], "CHRDATA1", 8);
    sv[o + 41] = 8; memcpy(&sv[o + 42], "CHRDATA2", 8);
    ecl::GameState gs;
    savegame::Header h;
    {
        dax::MemorySource src(sv.data(), static_cast<uint32_t>(sv.size()));
        CHECK(savegame::read(src, gs, h));
        dax::MemorySource shorter(sv.data(), 1000);
        ecl::GameState g2;
        CHECK(!savegame::read(shorter, g2, h));
        dax::MemorySource again(sv.data(), static_cast<uint32_t>(sv.size()));
        CHECK(savegame::read(again, gs, h));
    }
    CHECK(h.game_area == 2 && gs.game_area == 2 && gs.x == 7 && gs.y == 13 && gs.dir == 4);
    CHECK(gs.wall_ahead == 1 && gs.roof == 0x80 && h.state == 1 && gs.code[0] == 0x42);
    CHECK(h.wall_block[0] == 3 && h.wall_set[0] == 1 && h.wall_block[1] == -1);
    CHECK(h.count == 2 && strcmp(h.names[1], "CHRDATA2") == 0);
    const profile::Profile* p = profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432);
    if (p && p->ecl_ops) {
        TestHost host;
        ecl::Vm vm(gs, host, *p->ecl_ops);
        CHECK(vm.get(0x4BE6) == 1 && vm.get(0x7F3E) == 2 && vm.get(0x7D00) == 0);
        vm.set_party(&pt);
        CHECK(vm.get(0x7C72) == 2 && vm.get(0x7D00) == 1 && vm.get(0x7F3E) == 2);
    }

    // Writing gives back the same bytes
    struct VecSink : savegame::Sink {
        std::vector<uint8_t> v;
        bool put(const uint8_t* p, size_t n) override { v.insert(v.end(), p, p + n); return true; }
    } sink;
    CHECK(savegame::write(sink, gs, h));
    CHECK(sink.v.size() == savegame::kSize && sink.v == sv);
    char name2[16];
    savegame::char_file('B', 3, name2, sizeof name2);
    CHECK(strcmp(name2, "CHRDATB3") == 0);
    savegame::file_name('C', name2, sizeof name2);
    CHECK(strcmp(name2, "SAVGAMC.DAT") == 0);
    char dir[32];
    const char cfg[] = "E\r\nP\r\nC:\\SAVE\\\r\nF\r\n";
    savegame::save_dir(cfg, sizeof cfg - 1, dir, sizeof dir);
    CHECK(strcmp(dir, "SAVE") == 0);
    const char cfg2[] = "E\nC:\\GAMES\\CURSE\\SAVES\n";
    savegame::save_dir(cfg2, sizeof cfg2 - 1, dir, sizeof dir);
    CHECK(strcmp(dir, "GAMES/CURSE/SAVES") == 0);
    savegame::save_dir("E\nP\n", 4, dir, sizeof dir);
    CHECK(dir[0] == 0);
}

// Item names, types, the rules' recalculation, money, the shop in the VM
static void test_items()
{
    // Made-up words in 21-byte slots: 1 "Long Sword", 2 "+1", 3 "Long", 4 "Arrow"
    const char* words[] = {"Long Sword", "+1", "Long", "Arrow", "Flask of Oil"};
    std::vector<uint8_t> tab(21 * 5, 0);
    for (int i = 0; i < 5; ++i) {
        tab[i * 21] = static_cast<uint8_t>(strlen(words[i]));
        memcpy(&tab[i * 21 + 1], words[i], strlen(words[i]));
    }
    items::Names n;
    n.set_words(tab.data(), 5, 21);
    n.set_plural(items::Names::Plural{73, 28, 9, 86, 0x87, 0xB1});
    CHECK(strcmp(n.word(3), "Long") == 0 && n.word(0)[0] == 0 && n.word(9)[0] == 0);
    // Types: 36 a one-handed melee weapon d8; 58 armour AC 3 (0x80 + 57); 59 a shield
    std::vector<uint8_t> types(2 + items::kTypes * 16, 0);
    auto ty = [&](int t, int k) -> uint8_t& { return types[2 + t * 16 + k]; };
    ty(36, 0) = 0; ty(36, 1) = 1; ty(36, 9) = 1; ty(36, 10) = 8; ty(36, 13) = 0xFF; ty(36, 14) = 0x04;
    ty(58, 0) = 2; ty(58, 6) = 0x80 + 57;   // AC 3 = 57 ty(58, 13) = 0xFF;
    ty(59, 0) = 1; ty(59, 1) = 1; ty(59, 6) = 0x81; ty(59, 13) = 0xFF;
    ty(73, 0) = 10; ty(73, 13) = 0xFF;
    dax::MemorySource tsrc(types.data(), static_cast<uint32_t>(types.size()));
    CHECK(n.read_types(tsrc) && n.type(58).slot == 2 && n.type(58).ac == 0x80 + 57 && n.type(36).dice == 1);

    uint8_t sword[items::kRecordSize] = {}, plate[items::kRecordSize] = {}, shield[items::kRecordSize] = {},
            arrows[items::kRecordSize] = {};
    sword[0x2E] = 36; sword[0x31] = 1; sword[0x30] = 2; sword[0x32] = 1; sword[0x35] = 2;
    sword[0x37] = 60; sword[0x3A] = 15;
    plate[0x2E] = 58; plate[0x31] = 1; plate[0x37] = 0xC2; plate[0x38] = 1;   // weight 450
    plate[0x3A] = 0x90; plate[0x3B] = 1;                                         // value 400
    shield[0x2E] = 59; shield[0x31] = 1; shield[0x37] = 100;
    arrows[0x2E] = 73; arrows[0x31] = 4; arrows[0x39] = 10;
    char t[64];
    n.name(items::Item{sword}, t, sizeof t);
    CHECK(strcmp(t, "Long Sword") == 0);                 // word 2 ("+1") hidden until identified
    n.name(items::Item{sword}, t, sizeof t, true);
    CHECK(strcmp(t, "Long Sword +1") == 0);
    n.name(items::Item{arrows}, t, sizeof t);
    CHECK(strcmp(t, "10 Arrows") == 0);

    // A fighter: Str 18/00, Dex 17, base AC 10 (50), THAC0 20 (40), move 12
    static party::Character ch;
    ch = party::Character{};
    ch.rec[0x10] = ch.rec[0x11] = 18; ch.rec[0x1C] = ch.rec[0x1D] = 100; ch.rec[0x16] = ch.rec[0x17] = 17;
    ch.rec[0x73] = 40; ch.rec[0x74] = 7; ch.rec[0x10B] = 5; ch.rec[0xE4] = 12;
    ch.rec[0x11E] = 1; ch.rec[0x120] = 2; ch.rec[0x124] = 50; ch.rec[0x125] = 1; ch.rec[0x12B] = 0xFF;
    ch.rec[0x103] = 0x2C; ch.rec[0x104] = 0x01;          // 300 platinum
    const rules::ItemFacts f{73, 28, {41, 42, 43, 44, 37, 36}};
    CHECK(rules::strength_group(ch) == 23 && rules::dex_ac_bonus(ch) == 3 && rules::max_encumbrance(ch) == 3000);
    rules::recalc(ch, n, f);
    CHECK(ch.ac() == 7 && ch.thac0() == 17 && ch.dice() == 1 && ch.dice_sides() == 2 && ch.damage_bonus() == 6);
    CHECK(ch.encumbrance() == 300 && ch.movement() == 12);
    CHECK(rules::gold_worth(ch) == 1500);
    // Buy and ready a sword, plate and shield
    CHECK(rules::price(plate, 0x10) == 400 && rules::price(plate, 0x08) == 200 && rules::price(arrows, 0x10) == 1);
    CHECK(!rules::too_heavy(ch, plate, n, f));
    for (const uint8_t* it : {sword, plate, shield}) {
        CHECK(rules::add_item(ch, it));
        ch.items[ch.n_items - 1][0x34] = 1;
    }
    rules::pay(ch, 415);
    CHECK(rules::gold_worth(ch) == 1085);
    CHECK(ch.money(4) == 217 && ch.money(3) == 0);       // 84 platinum paid, 1 back in change
    rules::recalc(ch, n, f);
    CHECK(ch.ac() == -1);                                // plate 57 + shield 1 + dex 3 = 61
    CHECK(ch.thac0() == 16 && ch.dice() == 1 && ch.dice_sides() == 8 && ch.damage_bonus() == 7);   // +1 sword, Str 18/00
    CHECK(ch.movement() == 9 && ch.encumbrance() == 217 + 60 + 450 + 100);
    CHECK(items::readied_in(ch.items, ch.n_items, n, items::kSlotArmour) == 1);
    int pool[7] = {50, 0, 0, 0, 0, 0, 0};
    CHECK(rules::gold_worth(pool) == 0);
    pool[3] = 3;
    rules::pay(pool, 2);
    CHECK(rules::gold_worth(pool) == 1);

    // Pool and share: 2 player characters and an NPC
    party::Party pp;
    pp.m[0] = ch;
    pp.m[1] = ch;
    pp.m[2] = ch;
    pp.m[2].rec[0xF7] = 0x80;
    pp.count = 3;
    int pot[7] = {};
    rules::pool(pp, pot);
    CHECK(pot[4] == 434 && pp.m[0].money(4) == 0 && pp.m[2].money(4) == 217);
    CHECK(pp.m[0].encumbrance() == 610);
    pot[3] = 5;
    rules::share(pp, pot);
    CHECK(pp.m[0].money(4) == 217 && pp.m[1].money(4) == 217 && pp.m[0].money(3) == 3 && pp.m[1].money(3) == 2);
    CHECK(pot[3] == 0 && pot[4] == 0 && pp.m[0].encumbrance() == 610 + 220);

    // The VM: TREASURE sets out goods, COMBAT with the shop flag opens the shop
    const profile::Profile* p = profile::find(games::Game::CurseOfTheAzureBonds, 57789, 62432);
    if (!p || !p->ecl_ops) return;
    Bytes c;
    const uint16_t kBase = 0x8000;
    for (int i = 0; i < 5; ++i) { c.push_back(0); op_addr(c, kBase + 20); }
    c.push_back(0x09); op_imm(c, 1); op_addr(c, 0x7F6C);
    c.push_back(0x1C);
    c.push_back(0x27); for (int i = 0; i < 7; ++i) op_imm(c, i == 3 ? 9 : 0); op_imm(c, 5);
    c.push_back(0x24);
    c.push_back(0x24);                                   // a second COMBAT: no flag, no monsters
    c.push_back(0x00);
    ecl::GameState gs;
    TestHost host;
    memcpy(gs.code, c.data(), c.size());
    gs.code_len = static_cast<uint32_t>(c.size());
    items::Ground g;
    g.n = 3;
    ecl::Vm vm(gs, host, *p->ecl_ops);
    vm.set_ground(&g);
    CHECK(vm.init_script());
    ecl::Stop r = vm.run(kBase + 20);
    CHECK(r == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Shop);
    CHECK(g.n == 0 && g.money[3] == 9 && host.items_block == 5 && vm.get(0x7F6C) == 0);
    // The second COMBAT: the after-fight step (treasure)
    CHECK(vm.resume() == ecl::Stop::Waiting && vm.wait() == ecl::Wait::Treasure);
    CHECK(vm.resume() == ecl::Stop::Stopped);
    // The temple flag: COMBAT opens the temple
    Bytes tc;
    for (int i = 0; i < 5; ++i) { tc.push_back(0); op_addr(tc, kBase + 20); }
    tc.push_back(0x09); op_imm(tc, 1); op_addr(tc, 0x7EE2);
    tc.push_back(0x24);
    tc.push_back(0x00);
    ecl::GameState gt;
    memcpy(gt.code, tc.data(), tc.size());
    gt.code_len = static_cast<uint32_t>(tc.size());
    ecl::Vm vt(gt, host, *p->ecl_ops);
    CHECK(vt.init_script());
    r = vt.run(kBase + 20);
    CHECK(r == ecl::Stop::Waiting && vt.wait() == ecl::Wait::Temple && vt.get(0x7EE2) == 0);
    CHECK(vt.resume() == ecl::Stop::Stopped);
    // Game time passing, in minutes (effects run out by it)
    vt.take_minutes();
    vt.advance_clock(2, 1);
    vt.advance_clock(1, 5);
    vt.advance_clock(3, 1);
    CHECK(vt.take_minutes() == 75 && vt.take_minutes() == 0);
}

// Synthetic rule tables (made-up numbers in the games' layout) for the
// class rules and Create New Character
static void make_tables(classes::Tables& t)
{
    classes::Layout l{};
    l.lo = 0x1000; l.hi = 0x1C00;
    l.spells = 0x1000; l.spell_count = 10;
    l.thac0 = 0x1100; l.class_flags = 0x1170; l.class_masks = 0x1178; l.max_hit_dice = 0x1180;
    l.thief_base = 0x1190; l.thief_race = 0x1210; l.thief_dex = 0x1260; l.stat_limits = 0x1300;
    l.race_classes = 0x1380; l.race_ages = 0x13F0; l.age_brackets = 0x14D0; l.class_min = 0x1520;
    l.class_alignments = 0x1590; l.class_records = 0x1640; l.saves = 0x1960;
    static uint8_t ds[0xC00];
    memset(ds, 0, sizeof ds);
    auto at = [&](int off) -> uint8_t& { return ds[off - 0x1000]; };
    auto put32 = [&](int off, uint32_t v) { for (int i = 0; i < 4; ++i) at(off + i) = static_cast<uint8_t>(v >> (8 * i)); };
    // spells 1-3 cleric level 1, 4 cleric level 2, 5-6 magic-user 1, 7 magic-user 2, 8 druid 1
    const uint8_t sp[9][2] = {{0, 0}, {0, 1}, {0, 1}, {0, 1}, {0, 2}, {2, 1}, {2, 1}, {2, 2}, {1, 1}};
    for (int s = 1; s < 9; ++s) { at(0x1000 + s * 16) = sp[s][0]; at(0x1000 + s * 16 + 1) = sp[s][1]; }
    for (int c = 0; c < 8; ++c) {
        for (int lv = 0; lv <= 12; ++lv) at(0x1100 + c * 13 + lv) = static_cast<uint8_t>(40 + lv);
        at(0x1170 + c) = static_cast<uint8_t>(1 << c);
        at(0x1178 + c) = static_cast<uint8_t>(1 << c);
        at(0x1180 + c) = 10;
        const uint32_t first = c == classes::MagicUser ? 2500 : c == classes::Cleric ? 1500 : 2000;
        for (int lv = 1; lv <= 11; ++lv) put32(0x1640 + c * 99 + (lv - 1) * 4, first << (lv - 1));
        for (int lv = 2; lv <= 12; ++lv) {
            at(0x1640 + c * 99 + 44 + (lv - 2) * 5) = 1;
            if (lv >= 3) at(0x1640 + c * 99 + 44 + (lv - 2) * 5 + 1) = 1;
        }
        for (int lv = 0; lv <= 12; ++lv)
            for (int k = 0; k < 5; ++k) at(0x1960 + c * 60 + lv * 5 + k) = static_cast<uint8_t>((c == 2 ? 17 : 16) - lv);
    }
    for (int lv = 0; lv <= 12; ++lv)
        for (int s = 1; s <= 8; ++s) at(0x1190 + lv * 8 + s) = static_cast<uint8_t>(10 + lv * 5 + s);
    at(0x1210 + 1 * 8 + 2) = static_cast<uint8_t>(-50);   // dwarves: skill 2 far down
    for (int s = 1; s <= 5; ++s) at(0x1260 + 18 * 5 + s) = 5;
    for (int r = 0; r < 8; ++r) {
        const int o = 0x1300 + r * 16;
        at(o) = 3; at(o + 1) = 3; at(o + 2) = 18; at(o + 3) = 18; at(o + 4) = 100; at(o + 5) = 50;
        for (int i = 1; i < 6; ++i) { at(o + 4 + i * 2) = 3; at(o + 5 + i * 2) = 18; }
        for (int e = 0; e < 7; ++e) { at(0x13F0 + r * 28 + e * 4) = 15; at(0x13F0 + r * 28 + e * 4 + 2) = 1; at(0x13F0 + r * 28 + e * 4 + 3) = 4; }
        const uint16_t br[5] = {200, 300, 400, 500, 600};
        for (int b = 0; b < 5; ++b) { at(0x14D0 + r * 10 + b * 2) = static_cast<uint8_t>(br[b]); at(0x14D0 + r * 10 + b * 2 + 1) = static_cast<uint8_t>(br[b] >> 8); }
    }
    // dwarves: the first age bracket at 10 (every dwarf is past it: Str +1, Wis -1)
    at(0x14D0 + 1 * 10) = 10; at(0x14D0 + 1 * 10 + 1) = 0;
    const uint8_t human[4] = {3, 2, 5, 0}, elf[4] = {3, 13, 2, 5};
    memcpy(&at(0x1380 + 7 * 14), human, 4);
    memcpy(&at(0x1380 + 2 * 14), elf, 4);
    for (int c = 0; c < 17; ++c) {
        at(0x1520 + c * 6 + 0) = c == create::Fighter ? 9 : 3;
        at(0x1590 + c * 10) = 9;
        for (int a = 0; a < 9; ++a) at(0x1590 + c * 10 + 1 + a) = static_cast<uint8_t>(a);
    }
    at(0x1590 + create::Paladin * 10) = 1;
    CHECK(t.set(l, ds, sizeof ds));
}

static create::Facts make_facts()
{
    create::Facts f{};
    for (int i = 0; i < 6; ++i) f.icon_colours[i] = static_cast<uint8_t>(i);
    for (int c = 0; c < 8; ++c) { f.hp_dice[c] = c == classes::MagicUser ? 4 : 10; f.hp_count[c] = 1; }
    f.con_save = 0x61; f.dwarf_orc = 0x1A; f.giants = 0x2F; f.gnome_giant = 0x12; f.gnome_extra = 0x30;
    f.elf_sleep = 0x6B; f.halfelf = 0x7C; f.prot_evil = 0x08; f.ranger_giant = 0x86;
    f.mu_first[0] = 5; f.mu_first[1] = 6; f.mu_level2 = 7;
    return f;
}

// The items' rules in fights (behaviour_facts.md items, coab's facts): a
// missile weapon's rate of fire and its pile, the range's penalty, the magic
// weapons' bonuses by the target's kind, displacement, the ring's
// invisibility, the large and backstab size tests
static void test_item_combat()
{
    static combat::Tables t;
    memset(&t, 0, sizeof t);
    t.ground[0x37][0] = 1; t.ground[0x37][1] = 1;
    static combat::Battle b;
    b = combat::Battle{};
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = 0x37;
    static uint8_t rec[2][party::kRecordSize];
    static uint8_t aff[2][party::kMaxAffects][party::kAffectSize];
    static int naff[2];
    static uint8_t its[2][4][items::kRecordSize];
    memset(rec, 0, sizeof rec);
    memset(aff, 0, sizeof aff);
    memset(its, 0, sizeof its);
    naff[0] = naff[1] = 0;
    for (int i = 0; i < 2; ++i) {
        uint8_t* r = rec[i];
        r[0x196] = 1; r[0x197] = static_cast<uint8_t>(i); r[0xDE] = 1;
        r[0x78] = r[0x1A4] = 250; r[0x1A5] = 12; r[0x11C] = 2;
        r[0x199] = 40;                                       // a 10 hits AC 10
        r[0x19A] = r[0x19B] = 50;
        r[0x19E] = 1; r[0x1A0] = 6; r[0x17] = 10;
        b.f[i].rec = r;
        b.f[i].aff = aff[i];
        b.f[i].n_aff = &naff[i];
        b.f[i].max_aff = party::kMaxAffects;
        b.f[i].items = its[i];
        b.f[i].member = i == 0 ? 0 : -1;
        b.f[i].monster = i == 0 ? -1 : 0;
        b.f[i].size = 1;
        b.f[i].y = 10;
    }
    b.f[0].x = 10;
    b.f[1].x = 11;
    b.n = 2;
    b.party_size = 1;
    combat::occupancy(b);
    // The types: long bow 43 (2 hands, 1d6, 4 half attacks, range 22, arrows),
    // arrows 73, darts 9 (6 half attacks, range 6, thrown), sling 47 (2, range
    // 21), long sword 36 (1d8 / 1d12 large, melee)
    static items::Names names;
    std::vector<uint8_t> ty(2 + items::kTypes * 16, 0);
    auto def = [&](int k, int hands, int dice, int sides, int attacks, int range, int flags, int ld, int ls) {
        uint8_t* q = &ty[2 + k * 16];
        q[0] = 0; q[1] = (uint8_t)hands; q[2] = (uint8_t)ld; q[3] = (uint8_t)ls; q[5] = (uint8_t)attacks;
        q[9] = (uint8_t)dice; q[10] = (uint8_t)sides; q[12] = (uint8_t)range; q[13] = 0xFF; q[14] = (uint8_t)flags;
    };
    def(43, 2, 1, 6, 4, 22, 0x0B, 1, 6);
    def(9, 1, 1, 3, 6, 6, 0x1A, 1, 2);
    def(47, 1, 1, 4, 2, 21, 0x0A, 1, 6);
    def(36, 1, 1, 8, 0, 0, 0x04, 1, 12);
    ty[2 + 73 * 16] = 10;                                    // arrows: slot 10
    dax::MemorySource src(ty.data(), static_cast<uint32_t>(ty.size()));
    CHECK(names.read_types(src));
    static combat::Facts fx;
    memset(&fx, 0, sizeof fx);
    fx.invisible = 0x19;
    fx.haste = 0x27;
    fx.items.flame_tongue = 0x06; fx.items.dragon_slayer = 0x4B; fx.items.frost_brand = 0x4C;
    fx.items.displace = 0x59; fx.items.ring_invisible = 0x38;
    b.fx = &fx;
    b.names = &names;
    b.arrow = 73;
    b.quarrel = 28;
    b.tables = &t;
    create::Dice d(7);
    uint8_t (*it)[items::kRecordSize] = its[0];
    auto hold = [&](int type, int pile, bool arrows, int arrow_pile) {
        memset(its[0], 0, sizeof its[0]);
        it[0][0x2E] = (uint8_t)type; it[0][0x34] = 1; it[0][0x39] = (uint8_t)pile;
        b.f[0].n_items = 1;
        if (arrows) {
            it[1][0x2E] = 73; it[1][0x34] = 1; it[1][0x39] = (uint8_t)arrow_pile;
            b.f[0].n_items = 2;
        }
    };
    // Rate of fire: a bow with its arrows 2 a round, no more than the pile;
    // without arrows readied the fighter's own 1; Haste doubles; darts 3; a sling 1
    hold(43, 0, true, 12);
    combat::start_round(b, d);
    CHECK(b.f[0].attacks[0] == 2);
    it[1][0x39] = 1;
    combat::start_round(b, d);
    CHECK(b.f[0].attacks[0] == 1);
    it[1][0x34] = 0;
    combat::start_round(b, d);
    CHECK(b.f[0].attacks[0] == 1);
    it[1][0x34] = 1; it[1][0x39] = 12;
    aff[0][0][0] = 0x27; naff[0] = 1;                        // hasted
    combat::start_round(b, d);
    CHECK(b.f[0].attacks[0] == 4);
    naff[0] = 0;
    hold(9, 12, false, 0);
    combat::start_round(b, d);
    CHECK(b.f[0].attacks[0] == 3);
    it[0][0x39] = 2;
    combat::start_round(b, d);
    CHECK(b.f[0].attacks[0] == 2);
    hold(47, 0, false, 0);
    combat::start_round(b, d);
    CHECK(b.f[0].attacks[0] == 1);
    // After attacking this round a new weapon gives no more attacks
    hold(36, 0, false, 0);
    combat::start_round(b, d);
    b.f[0].attacked = true;
    hold(43, 0, true, 12);
    combat::recount_attacks(b, 0);
    CHECK(b.f[0].attacks[0] == 1);
    // The range: a bow's third is 7 squares; beyond it AC 2 better, beyond 14 5 better
    auto swing = [&](int a, int c) {
        b.f[a].attacks[0] = 1; b.f[a].attacks[1] = 0;
        b.f[c].received = 0;
        b.f[c].rec[0x1A4] = 250; b.f[c].rec[0x195] = 0; b.f[c].rec[0x196] = 1;
        return combat::attack(b, a, c, &names, d, false);
    };
    hold(43, 0, true, 50);
    const int dists[] = {1, 7, 8, 14, 15, 21};
    const int pens[] = {0, 0, 2, 2, 5, 5};
    for (int k = 0; k < 6; ++k) {
        b.f[1].x = 10 + dists[k];
        combat::occupancy(b);
        bool ok = true;
        for (int n = 0; n < 40; ++n) {
            create::Dice peek = d;
            const int r = peek.roll(20, 1);
            const bool want = r == 20 || (r != 1 && r + 40 >= 50 + pens[k]);
            if (swing(0, 1).hits[0].hit != want) ok = false;
        }
        CHECK(ok);
    }
    // Magic weapons by the target's kind (0x11A): Flame Tongue (effect 6)
    // trolls (10) +1, 9 / 12 +2, the animated dead (4) +3; Frost Brand (0x4C)
    // fire (8) +3; Dragon Slayer (0x4B) dragons (3): +2 and 3 x d12 + 4
    hold(36, 0, false, 0);
    b.f[1].x = 11;
    combat::occupancy(b);
    struct W { uint8_t fx; int kind, bonus; bool slayer; };
    const W ws[] = {{6, 10, 1, false}, {6, 9, 2, false}, {6, 12, 2, false}, {6, 4, 3, false}, {6, 3, 0, false},
                    {0x4C, 8, 3, false}, {0x4C, 10, 0, false}, {0x4B, 3, 2, true}, {0x4B, 8, 0, false}};
    for (const W& w : ws) {
        naff[0] = 1;
        memset(aff[0][0], 0, party::kAffectSize);
        aff[0][0][0] = w.fx; aff[0][0][3] = 0xFF;
        rec[1][0x11A] = (uint8_t)w.kind;
        bool ok = true;
        for (int n = 0; n < 30; ++n) {
            create::Dice peek = d;
            const int r = peek.roll(20, 1);
            const int extra = r > 1 ? w.bonus : 0;
            const bool want = r == 20 || (r != 1 && r + 40 + extra >= 50);
            const combat::Attack at = swing(0, 1);
            if (at.hits[0].hit != want) ok = false;
            if (want) {
                int dmg = peek.roll(6, 1) + (w.slayer ? 0 : w.bonus);       // (rec dice 1d6: the long sword's 1d8 isn't
                if (w.slayer) dmg = peek.roll(12, 1) * 3 + 4;               // in the record here)
                if (at.hits[0].damage != dmg) ok = false;
            }
        }
        CHECK(ok);
    }
    naff[0] = 0;
    rec[1][0x11A] = 0;
    // Displacement: its data's 0x10 set by the first attack (past a 1) - that one
    // misses even on a 20; a fight's start clears it
    naff[1] = 1;
    memset(aff[1][0], 0, party::kAffectSize);
    aff[1][0][0] = 0x59; aff[1][0][3] = 0xFF;
    combat::battle_start(b);
    CHECK(aff[1][0][3] == 0x0F);
    rec[0][0x199] = 100;                                     // all but a 1 would hit
    int misses = 0, swings = 0;
    for (int n = 0; n < 12; ++n) {
        create::Dice peek = d;
        const int r = peek.roll(20, 1);
        const combat::Attack at = swing(0, 1);
        ++swings;
        if (!at.hits[0].hit) {
            ++misses;
            if (r > 1) CHECK(misses == 1 || r == 1);
        }
        if (r > 1 && swings == 1) CHECK(!at.hits[0].hit);
    }
    CHECK((aff[1][0][3] & 0x10) != 0);
    rec[0][0x199] = 40;
    naff[1] = 0;
    // The Ring of Invisibility: invisible at the start, seen after attacking, again after the round
    naff[0] = 1;
    memset(aff[0][0], 0, party::kAffectSize);
    aff[0][0][0] = 0x38; aff[0][0][3] = 0xFF;
    combat::battle_start(b);
    CHECK(b.f[0].has(0x19));
    swing(0, 1);
    CHECK(!b.f[0].has(0x19));
    combat::tick(b);
    CHECK(b.f[0].has(0x19));
    combat::tick(b);
    CHECK(b.f[0].has(0x19));
    naff[0] = 0;
    // Large targets: size byte over 0x80, or & 7 over 1 (0x80 itself is man-sized): the large dice
    rec[0][0x19E] = 1; rec[0][0x1A0] = 8; rec[0][0x199] = 100;
    for (int sz : {0x80, 0x81, 0x02, 0x01}) {
        rec[1][0xDE] = (uint8_t)sz;
        bool ok = true;
        for (int n = 0; n < 20; ++n) {
            create::Dice peek = d;
            const int r = peek.roll(20, 1);
            const int dmg = peek.roll(sz > 0x80 || (sz & 7) > 1 ? 12 : 8, 1);
            const combat::Attack at = swing(0, 1);
            if (r != 1 && at.hits[0].damage != dmg) ok = false;
        }
        CHECK(ok);
    }
    // Backstab: man-sized or smaller is (size & 0x7F) 1 or less
    rec[0][0x10F] = 3;                                       // a thief
    hold(36, 0, false, 0);
    for (int sz : {0x01, 0x81, 0x02, 0x09}) {
        rec[1][0xDE] = (uint8_t)sz;
        b.f[1].received = 2;
        b.f[1].facing = 2;                                   // its back to the thief (west of it)
        CHECK(combat::can_backstab(b, 0, 1, nullptr) == ((sz & 0x7F) <= 1));
    }
}

static void test_create()
{
    static classes::Tables t;
    make_tables(t);
    const create::Facts f = make_facts();
    int list[16];

    CHECK(create::races(list, 16) == 6 && list[0] == 1 && list[5] == 7);
    CHECK(create::classes_for(t, 7, list, 16) == 3 && list[0] == 2 && list[1] == 5 && list[2] == 0);
    CHECK(create::classes_for(t, 2, list, 16) == 3 && list[0] == create::FighterMU);
    CHECK(create::alignments_for(t, create::Paladin, list, 16) == 1 && list[0] == 0);
    CHECK(create::alignments_for(t, create::Fighter, list, 16) == 9 && list[8] == 8);

    create::Dice d(1234);
    for (int i = 0; i < 200; ++i) {
        const int v = d.roll(6, 3);
        CHECK(v >= 3 && v <= 18);
    }

    // A human fighter: 25000 xp buys level 5 (2000 doubling)
    static party::Character a, b;
    create::Dice d1(77), d2(77);
    create::begin(a, t, f, d1, 7, 0, create::Fighter, 3);
    create::roll(a, t, f, d1);
    create::begin(b, t, f, d2, 7, 0, create::Fighter, 3);
    create::roll(b, t, f, d2);
    CHECK(memcmp(a.rec, b.rec, party::kRecordSize) == 0);        // same dice, same character
    CHECK(a.race() == 7 && a.cls() == create::Fighter && a.alignment() == 3 && a.sex() == 0);
    CHECK(a.level(classes::Fighter) == 5 && a.exp() == 25000);
    CHECK(a.rec[0x73] == 45 && a.rec[0xDF] == 12 && a.rec[0xE3] == 12);   // THAC0 / saves for level 5
    CHECK(a.rec[0x12B] == 1 << classes::Fighter);
    CHECK(a.age() >= 16 && a.age() <= 19);
    for (int i = 0; i < 6; ++i) CHECK(a.stat(i) >= 3 && a.stat(i) <= 18 && a.stat_now(i) == a.stat(i));
    CHECK(a.stat(0) >= 9);                                          // the class minimum
    CHECK(a.stat(0) != 18 || (a.str00() >= 1 && a.str00() <= 100));
    CHECK(a.money(4) == 300 && a.hp_max() >= 1 && a.hp() == a.hp_max());
    CHECK(a.n_affects == 0 && a.rec[0x145] == ((0 + 8) << 4));
    // A reroll starts again from level 1 and comes back to level 5
    create::roll(a, t, f, d1);
    CHECK(a.level(classes::Fighter) == 5 && a.exp() == 25000);
    // More experience: one more level, then nothing to train
    a.rec[0x127] = 0x40; a.rec[0x128] = 0x9C; a.rec[0x129] = 0;   // 40000
    CHECK(create::trainable(a, t) == 1 << classes::Fighter);
    const int hp = a.hp_max();
    CHECK(create::train(a, t, f, d1, false));
    CHECK(a.level(classes::Fighter) == 6 && a.rec[0x73] == 46 && a.hp_max() > hp);
    CHECK(!create::train(a, t, f, d1, false) && create::trainable(a, t) == 0);

    create::set_name(a, "A VERY LONG NAME INDEED");
    char name[20];
    a.name(name, sizeof name);
    CHECK(strlen(name) == 15 && strncmp(name, "A VERY LONG NAM", 15) == 0);

    // Constitution's hit points: fighters get extra at 17+
    a.rec[0x19] = 18;
    CHECK(create::con_hp_adj(a, t) == 4);
    a.rec[0x19] = 3;
    CHECK(create::con_hp_adj(a, t) == -2);

    // Modify Character: only as made (25000 xp); stats within limits, the
    // fighter's 18 going on into 18/01 .. 18/00, hit points within bounds
    {
        party::Character& m = b;
        auto set = [&](int i, int v) { m.rec[0x10 + i * 2] = m.rec[0x11 + i * 2] = static_cast<uint8_t>(v); };
        CHECK(create::can_modify(m));
        m.rec[0x127] = 0xA9;                                        // 25001
        CHECK(!create::can_modify(m));
        m.rec[0x127] = 0xA8;
        CHECK(create::can_modify(m) && m.exp() == 25000);
        set(0, 17); set(6, 0);
        create::modify_stat(m, t, f, 0, 1);
        CHECK(m.stat(0) == 18 && m.str00() == 1);
        create::modify_stat(m, t, f, 0, 1);
        CHECK(m.stat(0) == 18 && m.str00() == 2);
        set(6, 100);
        create::modify_stat(m, t, f, 0, 1);
        CHECK(m.str00() == 100);
        create::modify_stat(m, t, f, 0, -1);
        CHECK(m.stat(0) == 18 && m.str00() == 99);
        set(6, 1);
        create::modify_stat(m, t, f, 0, -1);
        CHECK(m.stat(0) == 18 && m.str00() == 0);
        create::modify_stat(m, t, f, 0, -1);
        CHECK(m.stat(0) == 17 && m.str00() == 0);
        set(0, 9);
        create::modify_stat(m, t, f, 0, -1);
        CHECK(m.stat(0) == 9);                                      // the fighter's minimum
        set(1, 18);
        create::modify_stat(m, t, f, 1, 1);
        CHECK(m.stat(1) == 18);
        set(1, 3);
        create::modify_stat(m, t, f, 1, -1);
        CHECK(m.stat(1) == 3);
        // Hit points: level 5 fighter, d10; Con 10: 5 .. 50; Con 18: 9 .. 70
        set(4, 10);
        CHECK(create::hp_least(m, t, f) == 5 && create::hp_most(m, t, f) == 50);
        m.rec[0x78] = 50;
        create::modify_hp(m, t, f, 1);
        CHECK(m.hp_max() == 50 && m.hp() == 50);
        m.rec[0x78] = 5;
        create::modify_hp(m, t, f, -1);
        CHECK(m.hp_max() == 5);
        set(4, 18);
        CHECK(create::hp_least(m, t, f) == 9 && create::hp_most(m, t, f) == 70);
        m.rec[0x78] = 70;
        create::modify_stat(m, t, f, 4, -1);                        // Con 17: at most 65
        CHECK(m.stat(4) == 17 && m.hp_max() == 65 && m.hp() == 65);
        create::modify_done(m, t, f);
        CHECK(m.rec[0x12C] == 65 - 5 * 3);
    }

    // Human Change: a human fighter with Str 15+ may become a cleric or a
    // magic-user (the fixture's classes for humans; no 9+ minimums there)
    {
        static party::Character h;
        h = b;
        create::Facts fc = f;
        fc.mu_change[0] = 5; fc.mu_change[1] = 6; fc.mu_change[2] = 7;
        h.rec[0x10] = h.rec[0x11] = 14;
        int cl[8];
        CHECK(create::can_change(h) && create::change_classes(h, t, cl, 8) == 0);   // Str 14: no
        h.rec[0x10] = h.rec[0x11] = 15;
        CHECK(create::change_classes(h, t, cl, 8) == 2 && cl[0] == create::MagicUser && cl[1] == create::Cleric);
        const int hd = h.rec[0xE5];
        create::change_class(h, t, fc, create::MagicUser);
        CHECK(h.exp() == 0 && h.level(classes::Fighter) == 0 && h.old_level(classes::Fighter) == 5 &&
              h.level(classes::MagicUser) == 1 && h.rec[0xE5] == 1 && h.rec[0xE6] == hd && h.cls() == create::MagicUser);
        CHECK(h.rec[0x79 + 4] && h.rec[0x79 + 5] && h.rec[0x79 + 6]);              // spells 5, 6, 7 known
        CHECK(!create::can_change(h) && !create::can_modify(h));
    }

    // A human magic-user: level 5 (2500 doubling), first spells and silent training's
    static party::Character m;
    create::Dice d3(5);
    create::begin(m, t, f, d3, 7, 1, create::MagicUser, 0);
    create::roll(m, t, f, d3);
    CHECK(m.level(classes::MagicUser) == 5 && m.sex() == 1);
    CHECK(m.rec[0x79 + 4] && m.rec[0x79 + 5] && m.rec[0x79 + 6] && !m.rec[0x79 + 0]);   // spells 5, 6, 7
    CHECK(m.rec[0x12D + 10] == 5 && m.rec[0x12D + 11] == 3);        // magic-user slots: 1 + 4, 3

    // A cleric: knows every cleric spell of the levels they have slots for
    static party::Character c;
    create::Dice d4(9);
    create::begin(c, t, f, d4, 7, 0, create::Cleric, 0);
    create::roll(c, t, f, d4);
    CHECK(c.level(classes::Cleric) == 6);
    CHECK(c.rec[0x79] && c.rec[0x7A] && c.rec[0x7B] && c.rec[0x7C] && !c.rec[0x7D] && !c.rec[0x80]);
    CHECK(c.rec[0x12D] >= 6 && c.rec[0x12D + 1] >= 4);

    // An elf fighter / magic-user: 12500 xp each, both classes level 4, the elf's effect
    static party::Character e;
    create::Dice d5(3);
    create::begin(e, t, f, d5, 2, 0, create::FighterMU, 0);
    create::roll(e, t, f, d5);
    CHECK(e.exp() == 12500 && e.level(classes::Fighter) == 4 && e.level(classes::MagicUser) == 4);
    CHECK(e.has_affect(0x6B) && e.n_affects == 1);
    CHECK(e.age() == 19);                                           // multi-classes: the dice's top
    CHECK(e.rec[0x12B] == ((1 << classes::Fighter) | (1 << classes::MagicUser)));

    // A dwarf: three effects; past the first age bracket (Str +1 / Wis -1 applied, still in limits)
    static party::Character w;
    create::Dice d6(11);
    create::begin(w, t, f, d6, 1, 0, create::Fighter, 0);
    CHECK(w.n_affects == 3 && w.has_affect(0x61) && w.has_affect(0x1A) && w.has_affect(0x2F));
    create::roll(w, t, f, d6);
    CHECK(w.stat(0) >= 9 && w.stat(0) <= 18);

    // Thief skills: base by level, the race's and dexterity's adjustments, never below 0
    static party::Character th;
    create::Dice d7(21);
    create::begin(th, t, f, d7, 7, 0, create::Thief, 0);
    th.rec[0x10B] = 0; th.rec[0x10F] = 3;                           // thief level 3
    th.rec[0x17] = 18;
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEA] == 10 + 15 + 1 + 5 && th.rec[0xEA + 5] == 10 + 15 + 6);
    th.rec[0x74] = 1;
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEA + 1] == 0);
    // The thief items (listing ovr026:0AEA): Gloves of Thievery (0x8B) - skill
    // 1 at 5th level below it, else +5; skill 2 at 7th below it, else +5; that
    // bonus on for skills 3-8 (0 when skill 2 was raised to 7th); Gauntlets of
    // Dexterity (0x82) - below 4th level the skills of 4th, else +10 on each,
    // added even to a skill the race made 0; the race's floor counts the bonus
    th.rec[0x74] = 7;
    th.n_items = 1;
    memset(th.items[0], 0, sizeof th.items[0]);
    th.items[0][0x34] = 1;
    th.items[0][0x3E] = 0x8B;
    auto base = [](int lv, int s) { return 10 + 5 * lv + s; };
    classes::thief_skills(th, t);                                   // level 3
    CHECK(th.rec[0xEA] == base(5, 1) + 5 && th.rec[0xEB] == base(7, 2) + 5 && th.rec[0xEC] == base(3, 3) + 5 &&
          th.rec[0xEF] == base(3, 6));
    th.rec[0x10F] = 6;
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEA] == base(6, 1) + 5 + 5 && th.rec[0xEB] == base(7, 2) + 5 && th.rec[0xEC] == base(6, 3) + 5);
    th.rec[0x10F] = 8;
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEA] == base(8, 1) + 10 && th.rec[0xEB] == base(8, 2) + 10 && th.rec[0xEC] == base(8, 3) + 10 &&
          th.rec[0xEF] == base(8, 6) + 5);
    th.rec[0x74] = 1;                                               // a dwarf: skill 2 -50, 52 < 50 + 5: 0
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEB] == 0);
    th.items[0][0x3E] = 0x82;                                       // gauntlets
    th.rec[0x74] = 7;
    th.rec[0x10F] = 3;
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEA] == base(4, 1) + 5 && th.rec[0xEF] == base(4, 6));
    th.rec[0x10F] = 5;
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEA] == base(5, 1) + 5 + 10 && th.rec[0xEF] == base(5, 6) + 10);
    th.rec[0x74] = 1;
    th.rec[0x10F] = 7;                                              // 47 < 50: 0, then the +10
    classes::thief_skills(th, t);
    CHECK(th.rec[0xEB] == 10);
    th.n_items = 0;

    // A Pool of Radiance record (285 bytes): the fields go where Curse keeps
    // them; stats held to the race's and sex's limits; no Animate Dead;
    // 300 platinum whatever they had; levels and experience as they were
    static uint8_t pool[create::kPoolRecordSize];
    memset(pool, 0, sizeof pool);
    pool[0] = 6; memcpy(pool + 1, "TESTER", 6);
    pool[0x10] = 20; pool[0x11] = 2; pool[0x12] = 12; pool[0x13] = 15; pool[0x14] = 16; pool[0x15] = 9;
    pool[0x16] = 99;
    pool[0x2E] = 7; pool[0x2F] = 2; pool[0x30] = 30; pool[0x32] = 33;
    pool[0x33 + 0x23] = 0x24; pool[0x33] = 1;
    pool[0x84] = 0x10; pool[0x88] = 0xFF;
    pool[0x96 + 2] = 4; pool[0x9E] = 1; pool[0xA0] = 1;
    pool[0xAC] = 0x28; pool[0xAD] = 0x23;                          // 9000 experience
    pool[0xBD] = 3; pool[0xBE] = 7; pool[0xC0] = 2; pool[0xC1] = 0x91;
    pool[0x11B] = 30;
    char pn[16];
    create::pool_name(pool, pn, sizeof pn);
    CHECK(strcmp(pn, "TESTER") == 0 && create::pool_is_pc(pool));
    static party::Character pc;
    pc.n_items = 3;
    create::from_pool(pool, pc, t);
    char cn[20];
    pc.name(cn, sizeof cn);
    CHECK(strcmp(cn, "TESTER") == 0 && pc.n_items == 0);
    CHECK(pc.stat(0) == 18 && pc.str00() == 50 && pc.stat(1) == 3 && pc.stat(2) == 12 && pc.stat(5) == 9);
    CHECK(pc.race() == 7 && pc.cls() == 2 && pc.level(2) == 4 && pc.sex() == 1 && pc.alignment() == 1);
    CHECK((pc.rec[0x127] | pc.rec[0x128] << 8) == 9000 && pc.hp() == 30 && pc.hp_max() == 33);
    CHECK(pc.rec[0x79] == 1 && pc.rec[0x79 + 0x23] == 0);
    CHECK((pc.rec[0x103] | pc.rec[0x104] << 8) == 300 && pc.rec[0xFB] == 0);
    CHECK(pc.rec[0x141] == 3 && pc.rec[0x142] == 7 && pc.rec[0x144] == 2 && pc.rec[0x145] == 0x91 && pc.rec[0xF7] == 0x10);
    CHECK(create::pool_effect_kept(0x61) && !create::pool_effect_kept(0x01));
    pool[0x84] = 0x80;
    CHECK(!create::pool_is_pc(pool));
}

static void test_item_piles()
{
    party::Character c;
    uint8_t arrows[items::kRecordSize] = {};
    arrows[0x2E] = 73; arrows[0x31] = 5; arrows[0x39] = 21; arrows[0x3A] = 2;   // 21 arrows, value 2
    uint8_t sword[items::kRecordSize] = {};
    sword[0x2E] = 7; sword[0x39] = 0; sword[0x3A] = 0x2C; sword[0x3B] = 1;      // value 300
    CHECK(rules::add_item(c, sword) && rules::add_item(c, arrows));
    // Halve: 21 -> 11 + 10, the new pile not readied
    c.items[1][0x34] = 1;
    CHECK(rules::halve(c, 1) && c.n_items == 3 && c.items[1][0x39] == 11 && c.items[2][0x39] == 10 && c.items[2][0x34] == 0);
    CHECK(!rules::halve(c, 0));                                  // a sword: one of it
    // Join them back (the sword stays as it is)
    CHECK(rules::join(c, 2) == 1 && c.n_items == 2 && c.items[1][0x39] == 21 && c.items[0][0x2E] == 7);
    // Join stops at 255 a pile
    uint8_t big[items::kRecordSize];
    memcpy(big, arrows, sizeof big);
    big[0x39] = 250;
    CHECK(rules::add_item(c, big) && rules::join(c, 2) == 1 && c.items[2][0x39] == 255 && c.items[1][0x39] == 16);
    // Different things don't join
    c.items[1][0x32] = 1;                                         // +1 arrows
    CHECK(rules::join(c, 1) == 0);
    rules::remove_item(c, 0);
    CHECK(c.n_items == 2 && c.items[0][0x2E] == 73);
    // Sell: half the value; arrows each, other piles / 20
    const rules::ItemFacts f{73, 28, {}};
    CHECK(rules::sell_value(sword, f) == 150);
    CHECK(rules::sell_value(arrows, f) == 21);                    // 21 x (2 / 2)
    uint8_t darts[items::kRecordSize];
    memcpy(darts, arrows, sizeof darts);
    darts[0x2E] = 9; darts[0x3A] = 100; darts[0x39] = 10;
    CHECK(rules::sell_value(darts, f) == 10 * 50 / 20);
}

static void test_temple()
{
    rules::CureFacts f{};
    f.blinded = 0x21; f.disease[0] = 0x1F; f.disease[1] = 0x22; f.poisoned = 0x37; f.slow_poison = 0x16;
    f.poison_damage = 0x0F; f.animate_dead = 0x20; f.curse = 0x24; f.feeblemind = 0x44;
    party::Character c;
    c.rec[0x78] = 30; c.rec[0x1A4] = 10;                    // HP 10 / 30
    c.n_affects = 3;
    c.affects[0][0] = 0x21; c.affects[1][0] = 0x37; c.affects[2][0] = 0x21;
    CHECK(rules::needs_cure(c, rules::kCureBlindness, f) && !rules::needs_cure(c, rules::kCureDisease, f));
    CHECK(rules::needs_cure(c, rules::kCureLight, f) && !rules::needs_cure(c, rules::kRaiseDead, f));
    create::Dice d(7);
    rules::apply_cure(c, rules::kCureBlindness, f, d);
    CHECK(c.n_affects == 1 && c.affects[0][0] == 0x37 && !rules::needs_cure(c, rules::kCureBlindness, f));
    rules::apply_cure(c, rules::kNeutralizePoison, f, d);
    CHECK(c.n_affects == 0);
    rules::apply_cure(c, rules::kCureLight, f, d);
    CHECK(c.hp() >= 11 && c.hp() <= 18);
    rules::apply_cure(c, rules::kHealCure, f, d);
    CHECK(c.hp() >= 26 && c.hp() <= 29);
    rules::heal(c, 100);
    CHECK(c.hp() == 30);
    // The dead: no healing; raised with 1 HP
    c.rec[0x195] = party::Dead; c.rec[0x1A4] = 0;
    rules::heal(c, 5);
    CHECK(c.hp() == 0 && rules::needs_cure(c, rules::kRaiseDead, f));
    rules::apply_cure(c, rules::kRaiseDead, f, d);
    CHECK(c.health() == party::Okay && c.hp() == 1 && c.in_combat());
    // A cursed item comes off
    c.n_items = 1; c.items[0][0x36] = 1; c.items[0][0x34] = 1;
    CHECK(rules::needs_cure(c, rules::kRemoveCurse, f));
    rules::apply_cure(c, rules::kRemoveCurse, f, d);
    CHECK(c.items[0][0x34] == 0);
    // Appraising
    CHECK(rules::gem_value(1) == 10 && rules::gem_value(50) == 50 && rules::gem_value(99) == 1000 && rules::gem_value(100) == 5000);
    for (int r = 1; r <= 100; r += 7) {
        const int v = rules::jewel_value(r, d);
        CHECK(v >= 100 && v < 12000);
    }
}

static void test_magic()
{
    static classes::Tables t;
    make_tables(t);
    static party::Party p;
    p = party::Party{};
    p.count = 1;
    party::Character& c = p.m[0];
    c.rec[0x109] = 3;                                         // a level 3 cleric
    c.rec[0x15] = 12;                                        // Wis 12
    c.rec[0x12D] = 2; c.rec[0x12E] = 1;                      // 2 first-level, 1 second-level spells a day
    c.rec[0x79 + 0] = c.rec[0x79 + 1] = c.rec[0x79 + 3] = 1;    // knows spells 1, 2, 4
    c.rec[0x79 + 4] = 1;                                       // and 5, a magic-user's (Int 0: can't use it)
    uint8_t ids[16];
    CHECK(magic::known(c, t, ids, 16) == 3 && ids[0] == 1 && ids[1] == 2 && ids[2] == 4);
    CHECK(magic::any_room(c, t) && magic::room(c, t, magic::Cleric, 1) == 2);
    CHECK(magic::add(c, t, 1) && magic::add(c, t, 2) && !magic::add(c, t, 3) && magic::add(c, t, 4) && !magic::add(c, t, 4));
    CHECK(magic::room(c, t, magic::Cleric, 1) == 0 && magic::memorizing(c));
    CHECK(magic::in_memory(c, t, true, ids, 16) == 3 && magic::in_memory(c, t, false, ids, 16) == 0);
    CHECK(magic::rest_minutes(c, t) == 4 * 60 + 4 * 15 && c.rec[0x72] == 4);
    // Cancel and again
    magic::cancel(c);
    CHECK(!magic::memorizing(c) && magic::room(c, t, magic::Cleric, 1) == 2);
    CHECK(magic::add(c, t, 1) && magic::add(c, t, 2) && magic::add(c, t, 4));
    magic::rest_minutes(c, t);
    // The rest: 4 hours' start, then 2 steps, 3 steps (a 1st), 6 steps (a 2nd)
    c.rec[0x78] = 20; c.rec[0x1A4] = 10;
    c.n_affects = 2;
    c.affects[0][0] = 0x21; c.affects[0][1] = 30;           // 30 minutes
    c.affects[1][0] = 0x22;                                  // lasting
    magic::Rest r;
    magic::begin(r);
    int learnt[3] = {}, n = 0, steps = 0;
    while (n < 3 && steps < 400) {
        const magic::Step st = magic::step(r, p, t);
        ++steps;
        if (st.learnt[0]) learnt[n++] = st.learnt[0];
    }
    CHECK(n == 3 && learnt[0] == 1 && learnt[1] == 2 && learnt[2] == 4 && steps == 59);
    CHECK(!magic::memorizing(c) && magic::in_memory(c, t, false, ids, 16) == 3);
    CHECK(c.n_affects == 1 && c.affects[0][0] == 0x22);       // the timed one ran out
    // A day's rest heals a point
    for (int k = steps; k < 288; ++k) magic::step(r, p, t);
    CHECK(c.hp() == 11);
    // Casting takes it out
    CHECK(magic::remove(c, 2) && !magic::remove(c, 2) && magic::in_memory(c, t, false, ids, 16) == 2);

    // Scrolls: type 61 a magic-user's scroll (slot 11), 62 a cleric's (slot 12)
    static items::Names names;
    std::vector<uint8_t> types(2 + items::kTypes * 16, 0);
    types[2 + 61 * 16] = 11;
    types[2 + 62 * 16] = 12;
    dax::MemorySource tsrc(types.data(), static_cast<uint32_t>(types.size()));
    CHECK(names.read_types(tsrc));
    magic::Scrolls sc;
    sc.names = &names;
    sc.one_spell = 0xD2;
    sc.read_magic = 0x10;
    party::Character& m = p.m[0];
    m = party::Character{};
    m.rec[0x109 + classes::MagicUser] = 3;
    m.rec[0x13] = 15;                                         // Int 15
    m.rec[0x137] = 2; m.rec[0x138] = 1;                       // magic-user: 2 first-level, 1 second-level a day
    m.rec[0x79 + 5 - 1] = 1;                                  // knows spell 5
    m.n_items = 3;
    m.items[0][0x2E] = 61; m.items[0][0x30] = 0xD4;           // "With 3 Spells": 5, 6, 7
    m.items[0][0x3C] = 5; m.items[0][0x3D] = 6; m.items[0][0x3E] = 7;
    m.items[1][0x2E] = 62; m.items[1][0x30] = 0xD2; m.items[1][0x3C] = 1;   // a cleric's: hidden
    m.items[1][0x35] = 4;
    m.items[2][0x2E] = 36;                                    // not a scroll
    CHECK(magic::is_scroll(sc, m.items[0]) && magic::is_scroll(sc, m.items[1]) && !magic::is_scroll(sc, m.items[2]));
    CHECK(magic::scroll_spells(m, t, sc, false, ids, 16) == 3 && ids[0] == 5 && ids[1] == 6 && ids[2] == 7);
    CHECK(magic::scribe(m, t, sc, 5) == magic::Scribe::Known);
    CHECK(magic::scribe(m, t, sc, 6) == magic::Scribe::Ok && magic::scribe(m, t, sc, 6) == magic::Scribe::Already);
    m.rec[0x138] = 0;
    CHECK(magic::scribe(m, t, sc, 7) == magic::Scribe::Cannot);  // no second-level spells
    CHECK(magic::scribing(m, sc) && magic::scroll_spells(m, t, sc, true, ids, 16) == 1 && ids[0] == 6);
    // Read Magic reads the hidden one
    magic::scribe(m, t, sc, 7);
    spells::add_affect(m, 0x10, 30, 0, false);
    CHECK(magic::scroll_spells(m, t, sc, false, ids, 16) == 4 && m.items[1][0x35] == 0);
    // The rest: 4 hours + a first-level spell; then it's in the book and off the scroll
    CHECK(magic::rest_minutes(m, t, sc) == 4 * 60 + 15);
    p.count = 1;
    magic::begin(r);
    int got = 0;
    for (int k = 0; k < 100 && !got; ++k) got = magic::step(r, p, t, sc).scribed[0];
    CHECK(got == 6 && m.rec[0x79 + 6 - 1] == 1 && m.items[0][0x3D] == 0 && m.items[0][0x30] == 0xD3 && m.n_items == 3);
    CHECK(!magic::scribing(m, sc));
    // Used up: the last spell scribed takes the scroll
    m.items[1][0x3C] = 1 | 0x80;
    m.rec[0x12D] = 1;
    magic::scribed(m, sc, 1, 0);
    CHECK(m.n_items == 2 && m.items[1][0x2E] == 36 && m.rec[0x79] == 1);
    // Training's new spell: first- and second-level magic-user spells not known
    m.rec[0x138] = 1;
    CHECK(magic::learnable(m, t, ids, 16) == 1 && ids[0] == 7);   // 5, 6 known now
    magic::learn(m, 7);
    CHECK(magic::learnable(m, t, ids, 16) == 0);
    // Cancelled
    m.items[0][0x3C] = 5 | 0x80;
    magic::cancel_scribes(m, sc);
    CHECK(!magic::scribing(m, sc) && m.items[0][0x3C] == 5);
    // Use: reading a scroll's spell takes it off; the last one takes the scroll
    CHECK(magic::scroll_list(m, t, sc, 0, ids, 3) == 2 && ids[0] == 5 && ids[1] == 7);
    CHECK(magic::scroll_list(m, t, sc, 1, ids, 3) == 0);                  // not a scroll
    CHECK(magic::reads_scroll(m, 100));                                   // a magic-user
    party::Character th{};
    CHECK(!magic::reads_scroll(th, 1));
    th.rec[0x109 + classes::Thief] = 10;
    CHECK(magic::reads_scroll(th, 75) && !magic::reads_scroll(th, 76));   // 3 times in 4
    magic::scroll_used(m, sc, 0, 7);
    CHECK(m.n_items == 2 && m.items[0][0x3E] == 0 && m.items[0][0x30] == 0xD2);
    magic::scroll_used(m, sc, 0, 5);
    CHECK(m.n_items == 1 && m.items[0][0x2E] == 36);
    // Magic items: a spell in the second effect byte, charges in the first
    uint8_t* wand = m.items[1];
    m.n_items = 2;
    memset(wand, 0, party::kItemSize);
    wand[0x2E] = 36; wand[0x3C] = 2; wand[0x3D] = 0x0F;
    CHECK(magic::usable(sc, wand) && magic::item_spell(wand) == 0x0F && !magic::usable(sc, m.items[0]));
    wand[0x3E] = 0x80;
    CHECK(!magic::usable(sc, wand));                                      // works while readied instead
    wand[0x3E] = 0;
    magic::used(m, 1);
    CHECK(m.n_items == 2 && wand[0x3C] == 1);
    wand[0x39] = 2;                                                       // a stack of two: one goes
    magic::used(m, 1);
    CHECK(m.n_items == 2 && wand[0x39] == 1 && wand[0x3C] == 1);
    magic::used(m, 1);
    CHECK(m.n_items == 1);                                                // the last charge
    // Worn while readied: 0x80 gives the effect in 0x3D, gone when put away
    {
        party::Character wc{};
        wc.n_items = 2;
        wc.items[0][0x3D] = 0x2A; wc.items[0][0x3E] = 0x80;
        CHECK(rules::worn(wc, 0, true) && wc.has_affect(0x2A) && wc.affects[0][1] == 0 && wc.affects[0][2] == 0);
        CHECK(rules::worn(wc, 0, true) && wc.n_affects == 1);           // not twice
        CHECK(rules::worn(wc, 0, false) && !wc.has_affect(0x2A));
        // 0x84: keyed to alignment 3, 2 points (0x23); the wrong alignment: put away, hurt
        wc.rec[0x11B] = 1; wc.rec[0x1A4] = 10; wc.rec[0x196] = 1;
        wc.items[1][0x3D] = 0x23; wc.items[1][0x3E] = 0x84; wc.items[1][0x34] = 1;
        CHECK(!rules::worn(wc, 1, true) && wc.items[1][0x34] == 0 && wc.hp() == 8);
        wc.rec[0x11B] = 3;
        wc.items[1][0x34] = 1;
        CHECK(rules::worn(wc, 1, true) && wc.hp() == 8);
    }
    uint8_t ever[party::kItemSize] = {};
    ever[0x3D] = 3;
    CHECK(!magic::use_charge(ever) && !magic::use_charge(ever));          // 0 charges: never runs out
}

// Casting outside combat: synthetic spells (made-up numbers in the games'
// 16-byte layout) and what each kind does to a made-up party
static void test_spells()
{
    static uint8_t ds[0x100];
    memset(ds, 0, sizeof ds);
    // spell: class, level, range, /lvl, lasts, /lvl, -, targets, -, -, affect, when
    auto sp = [&](int s, int lv, int lasts, int per, int targets, int affect) {
        uint8_t* e = ds + s * 16;
        e[0] = 0; e[1] = static_cast<uint8_t>(lv); e[4] = static_cast<uint8_t>(lasts); e[5] = static_cast<uint8_t>(per);
        e[7] = static_cast<uint8_t>(targets); e[10] = static_cast<uint8_t>(affect); e[11] = 2;
    };
    sp(1, 1, 6, 0, spells::kParty, 0x01);       // a party blessing
    sp(2, 1, 0, 0, spells::kMember, 0);         // wounds
    sp(3, 1, 0, 0, spells::kCombat, 0);         // for fights
    sp(4, 2, 0, 60, spells::kMember, 0x16);     // slow poison
    sp(5, 3, 0, 10, spells::kParty, 0x27);      // haste
    sp(6, 5, 0, 0, spells::kMember, 0);         // raise
    sp(7, 3, 0, 0, spells::kMember, 0);         // cure disease
    sp(8, 4, 0, 0, spells::kMember, 0);         // neutralize
    sp(9, 3, 0, 0, spells::kMember, 0);         // remove curse
    sp(10, 2, 2, 1, spells::kSelf, 0x1C);       // mirror images
    sp(11, 3, 0, 1, spells::kParty, 0x31);      // prayer
    classes::Layout l{};
    l.lo = 0; l.hi = 0x100; l.spells = 0; l.spell_count = 12;
    static classes::Tables t;
    CHECK(t.set(l, ds, sizeof ds));
    CHECK(spells::entry(t, 4).lasts_level == 60 && spells::entry(t, 3).targets == spells::kCombat);

    rules::CureFacts cf{};
    cf.blinded = 0x21; cf.poisoned = 0x37; cf.slow_poison = 0x16; cf.poison_damage = 0x0F; cf.animate_dead = 0x20;
    cf.curse = 0x24;
    spells::Facts f{2, 0x2A, {0x22, 0x2B, 0x32}, {{0, 0}, {0x2C, 0x1F}, {0x39, 0}}};
    static party::Party p;
    p = party::Party{};
    p.count = 3;
    for (int i = 0; i < 3; ++i) {
        party::Character& c = p.m[i];
        c.rec[0x109 + classes::Fighter] = 4;
        c.rec[0x78] = 20; c.rec[0x1A4] = 20; c.rec[0x196] = 1;
        c.rec[0x74] = 7; c.rec[0x18] = c.rec[0x19] = 10;
    }
    party::Character& me = p.m[0];
    me.rec[0x109 + classes::Fighter] = 0;
    me.rec[0x109 + classes::Cleric] = 3;            // a level 3 cleric casts
    CHECK(spells::power(me, t, 1) == 3 && spells::power(p.m[1], t, 1) == 6);
    CHECK(spells::lasts(t, 4, 3) == 180);
    create::Dice d(7);
    spells::Line out[16];
    auto cast = [&](int s, spells::Does does, int target, uint8_t n = 0, uint8_t sides = 0, uint8_t plus = 0) {
        const spells::CampSpell cs{static_cast<uint8_t>(s), does, n, sides, plus, does == spells::Does::Heal ? 0u : 0x1234u};
        return spells::cast(p, 0, target, cs, t, cf, f, d, out, 16);
    };
    // The blessing: everyone, the caster's level in its data; again: still one each
    CHECK(cast(1, spells::Does::Affect, -1) == 3 && out[2].who == 2 && out[2].what == spells::Said::Word);
    CHECK(cast(1, spells::Does::Affect, -1) == 3);
    for (int i = 0; i < 3; ++i) {
        const party::Character& c = p.m[i];
        CHECK(c.n_affects == 1 && c.affects[0][0] == 0x01 && c.affects[0][1] == 6 && c.affects[0][3] == 3);
    }
    // Wounds: the unconscious wake; the dead aren't healed
    party::Character& b = p.m[1];
    b.rec[0x1A4] = 0; b.rec[0x195] = party::Unconscious; b.rec[0x196] = 0;
    CHECK(cast(2, spells::Does::Heal, 1, 1, 8, 0) == 1 && out[0].what == spells::Said::Partly);
    CHECK(b.hp() >= 1 && b.hp() <= 8 && b.health() == party::Okay && b.in_combat());
    CHECK(cast(2, spells::Does::Heal, 1, 3, 8, 20) == 1 && out[0].what == spells::Said::Fully && b.hp() == 20);
    b.rec[0x195] = party::Dead;
    CHECK(cast(2, spells::Does::Heal, 1, 1, 8, 0) == 0);
    // Raised: 1 HP, a point of Constitution; an elf can't be
    CHECK(cast(6, spells::Does::Raise, 1) == 1 && out[0].what == spells::Said::Raised);
    CHECK(b.health() == party::Okay && b.hp() == 1 && b.rec[0x18] == 9 && b.rec[0x19] == 9 && b.in_combat());
    p.m[2].rec[0x195] = party::Dead; p.m[2].rec[0x74] = 2;
    CHECK(cast(6, spells::Does::Raise, 2) == 0 && p.m[2].health() == party::Dead);
    p.m[2].rec[0x195] = party::Okay; p.m[2].rec[0x74] = 7;
    // Slow poison: only for the poisoned (1 HP at least, the poison's harm held 10 minutes)
    CHECK(cast(4, spells::Does::SlowPoison, 1) == 0);
    spells::add_affect(b, 0x37, 0, 0, false);
    b.rec[0x1A4] = 0;
    CHECK(cast(4, spells::Does::SlowPoison, 1) == 1 && b.hp() == 1);
    int i16 = spells::find_affect(b, 0x16), i0f = spells::find_affect(b, 0x0F);
    CHECK(i16 >= 0 && (b.affects[i16][1] | b.affects[i16][2] << 8) == 180 && b.affects[i16][3] == 0xFF && b.affects[i16][4] == 1);
    CHECK(i0f >= 0 && b.affects[i0f][1] == 10);
    // Neutralize: the poison and its slowing gone
    CHECK(cast(8, spells::Does::Neutralize, 1) == 1 && out[0].what == spells::Said::Unpoisoned);
    CHECK(spells::find_affect(b, 0x37) < 0 && spells::find_affect(b, 0x16) < 0 && spells::find_affect(b, 0x0F) < 0);
    CHECK(cast(8, spells::Does::Neutralize, 1) == 1 && out[0].what == spells::Said::Unaffected);
    // Haste: the caster's level of members (3; here 2 by making the caster level 2): slowed ones cured instead
    me.rec[0x109 + classes::Cleric] = 2;
    spells::add_affect(b, 0x2A, 5, 0, false);
    CHECK(cast(5, spells::Does::Haste, -1) == 2 && out[0].who == 1 && out[0].what == spells::Said::Cured &&
          out[1].who == 0 && out[1].what == spells::Said::Word);
    CHECK(spells::find_affect(me, 0x27) >= 0 && spells::find_affect(b, 0x27) < 0 && spells::find_affect(b, 0x2A) < 0 &&
          spells::find_affect(p.m[2], 0x27) < 0);
    me.rec[0x109 + classes::Cleric] = 3;
    // Cure disease: weakness and what goes with it
    spells::add_affect(b, 0x2B, 0, 0, false);
    spells::add_affect(b, 0x2C, 0, 0, false);
    spells::add_affect(b, 0x1F, 0, 0, false);
    CHECK(cast(7, spells::Does::CureDisease, 1) == 1 && out[0].what == spells::Said::Cured);
    CHECK(spells::find_affect(b, 0x2B) < 0 && spells::find_affect(b, 0x2C) < 0 && spells::find_affect(b, 0x1F) < 0);
    // Remove curse: the curse, else a cursed item comes off
    spells::add_affect(b, 0x24, 0, 0, false);
    CHECK(cast(9, spells::Does::RemoveCurse, 1) == 2 && out[1].what == spells::Said::Uncursed);
    b.n_items = 1; b.items[0][0x36] = 1; b.items[0][0x34] = 1;
    CHECK(cast(9, spells::Does::RemoveCurse, 1) == 1 && out[0].what == spells::Said::ItemUncursed && b.items[0][0x34] == 0);
    // Mirror images: 1d4 in the data's top half; prayer: the side and level
    CHECK(cast(10, spells::Does::Mirror, 2) == 1 && out[0].who == 0);
    const int im = spells::find_affect(me, 0x1C);
    CHECK(im >= 0 && (me.affects[im][3] & 15) == 3 && (me.affects[im][3] >> 4) >= 1 && (me.affects[im][3] >> 4) <= 4 &&
          me.affects[im][1] == 5);
    CHECK(cast(11, spells::Does::Prayer, -1) == 3 && p.m[2].affects[spells::find_affect(p.m[2], 0x31)][3] == 3);
    // Not in the engine yet: nothing
    const int before = me.n_affects;
    CHECK(cast(1, spells::Does::NotYet, -1) == 0 && me.n_affects == before);
    // Who can cast
    CHECK(spells::can_cast(me));
    me.rec[0x196] = 0;
    CHECK(!spells::can_cast(me));
    me.rec[0x196] = 1;

    // Fix: wounds (spell 2, level 1: 1d8) held by the cleric and her two
    // first-level slots; 4 hours + 2 x 15 minutes; the healing shared out
    for (int i = 0; i < 3; ++i) { p.m[i].rec[0x195] = party::Okay; p.m[i].rec[0x1A4] = 20; }
    const spells::CampSpell camp[2] = {{2, spells::Does::Heal, 1, 8, 0, 0}, {1, spells::Does::Affect, 0, 0, 0, 0}};
    CHECK(spells::hp_lost(p) == 0 && spells::fix_plan(p, t, camp, 2, d).minutes == 0);
    for (int k = 0; k < 84; ++k) me.rec[0x1E + k] = 0;
    me.rec[0x1E] = 2;                  // one held
    me.rec[0x1F] = 2 | 0x80;           // one being memorized (doesn't count)
    me.rec[0x12D] = 2;                 // two first-level cleric spells a day
    p.m[1].rec[0x1A4] = 2;             // 18 lost
    p.m[2].rec[0x1A4] = 10;            // 10 lost
    const spells::FixPlan fp = spells::fix_plan(p, t, camp, 2, d);
    CHECK(spells::hp_lost(p) == 28 && fp.minutes == 240 + 30 && fp.heal >= 3 && fp.heal <= 24);
    // Less lost than the healers can heal (27 a first-level healer): a shorter rest
    p.m[1].rec[0x1A4] = 10; p.m[2].rec[0x1A4] = 20;
    CHECK(spells::fix_plan(p, t, camp, 2, d).minutes == 270 / 2);
    // Shared in party order: 10 to the first who needs it, then the rest
    p.m[1].rec[0x1A4] = 2; p.m[2].rec[0x1A4] = 10;
    CHECK(spells::fix_heal(p, 21) == 0 && p.m[1].hp() == 20 && p.m[2].hp() == 13);
    CHECK(spells::fix_heal(p, 50) == 43 && p.m[2].hp() == 20);

    // Enlarge (spell 4's row here: one member, effect 0x16): a level 3
    // caster gives 18/51 - more than Str 10, so "is stronger" and the
    // effect; the stats then read 18/51. Reduce takes it away (unless saved)
    party::Character& e = p.m[1];
    e.n_affects = 0;
    e.rec[0x10] = e.rec[0x11] = 10; e.rec[0x1C] = e.rec[0x1D] = 0;
    e.rec[0x195] = party::Okay; e.rec[0x196] = 1;
    CHECK(spells::enlarge_data(3) == 52 && spells::enlarge_data(7) == 119 && spells::enlarge_data(12) == 1);
    CHECK(cast(4, spells::Does::Enlarge, 1) == 1 && out[0].what == spells::Said::Word && e.n_affects == 1 &&
          e.affects[0][3] == 52);
    rules::ItemFacts rf{};
    rf.enlarge_fx = 0x16;
    rules::stats(e, rf);
    CHECK(e.stat(0) == 18 && e.str00() == 51 && e.rec[0x10] == 10);
    CHECK(cast(4, spells::Does::Enlarge, 1) == 1 && out[0].what == spells::Said::Unaffected);   // no more than now
    f.enlarge = 0x16;
    e.rec[0xE3] = 30;                                 // no save but a 20
    const int red = cast(4, spells::Does::Reduce, 1);
    CHECK(red == 0 || (red == 1 && out[0].what == spells::Said::Word && e.n_affects == 0));
    // Friends: the caster's Charisma + 2d4 for the effect's time
    me.n_affects = 0;
    me.rec[0x1A] = me.rec[0x1B] = 12;
    CHECK(cast(4, spells::Does::Friends, -1) == 1 && out[0].who == 0 && me.n_affects == 1);
    rules::ItemFacts ff{};
    ff.friends_fx = 0x16;
    rules::stats(me, ff);
    CHECK(me.stat(5) >= 14 && me.stat(5) <= 20 && me.rec[0x1A] == 12);
    // Strength on a fighter: d8 on Str 10 (no word)
    e.n_affects = 0;
    e.rec[0x10] = e.rec[0x11] = 10;
    CHECK(cast(4, spells::Does::Strength, 1) == 0 && e.n_affects == 1 && e.affects[0][3] >= 101 &&
          e.affects[0][3] <= 108);
    rules::ItemFacts sf{};
    sf.strength_fx = 0x16;
    rules::stats(e, sf);
    CHECK(e.stat(0) == 10 + e.affects[0][3] - 100);
    // A girdle (readied, effect 0x80 + 5, strength 19) and a cursed stone (0x80 + 13: Str 3)
    e.n_affects = 0;
    e.n_items = 1;
    memset(e.items[0], 0, sizeof e.items[0]);
    e.items[0][0x34] = 1; e.items[0][0x3E] = 0x85; e.items[0][0x3D] = 1;
    rules::stats(e, sf);
    CHECK(e.stat(0) == 19);
    e.items[0][0x3E] = 0x8D;
    rules::stats(e, sf);
    CHECK(e.stat(0) == 3 && e.str00() == 0);
    e.n_items = 0;
    rules::stats(e, sf);
    CHECK(e.stat(0) == 10);
    // The giant strength potion (the caster): Str 21 when more than their own - "is stronger"
    f.giant = 0x52;
    f.timed[0] = spells::Facts::Timed{4, 1, 4, 4, 10};
    me.n_affects = 0;
    me.rec[0x10] = me.rec[0x11] = 12;
    CHECK(cast(4, spells::Does::GiantStrength, -1) == 1 && out[0].who == 0 && out[0].what == spells::Said::Word &&
          me.n_affects == 1 && me.affects[0][0] == 0x52 && me.affects[0][3] == 121 && me.affects[0][1] >= 50 &&
          me.affects[0][1] <= 80);
    rules::ItemFacts gf{};
    gf.giant_fx = 0x52;
    rules::stats(me, gf);
    CHECK(me.stat(0) == 21);
    me.n_affects = 0;
    me.rec[0x10] = me.rec[0x11] = 22;                // stronger already: the effect, nothing said, no change
    CHECK(cast(4, spells::Does::GiantStrength, -1) == 0 && me.n_affects == 1 && me.affects[0][3] == 122);
    rules::stats(me, gf);
    CHECK(me.stat(0) == 22);
    me.rec[0x10] = me.rec[0x11] = 10;
    me.n_affects = 0;
    f.timed[0] = spells::Facts::Timed{};
    // Fire Shield: hot - the shield and the zap, "is protected"; cold - nothing said
    f.hot = 0x32; f.cold = 0x36; f.zap = 0x0F;
    const spells::CampSpell fsh{10, spells::Does::FireShield, 0, 0, 0, 0x99};
    CHECK(spells::cast(p, 0, -1, fsh, t, cf, f, d, out, 16, 0, 1) == 1 && out[0].what == spells::Said::Word &&
          me.n_affects == 2 && me.affects[0][0] == 0x32 && me.affects[1][0] == 0x0F && me.affects[0][1] == 2 + 3);
    me.n_affects = 0;
    CHECK(spells::cast(p, 0, -1, fsh, t, cf, f, d, out, 16, 0, 2) == 0 && me.n_affects == 2 && me.affects[0][0] == 0x36);
    me.n_affects = 0;
    // Rolled times: the listed spells; the rest the table's
    f.timed[1] = spells::Facts::Timed{5, 5, 4, 0, 1};
    f.timed[2] = spells::Facts::Timed{6, 0, 0, 1440, 1};
    for (int i = 0; i < 20; ++i) {
        const int r = spells::lasts_rolled(t, 5, 3, f, d);
        CHECK(r >= 5 && r <= 20);
    }
    CHECK(spells::lasts_rolled(t, 6, 3, f, d) == 1440 && spells::lasts_rolled(t, 4, 3, f, d) == 180);
}

// Combat: a made-up open field, made-up placement tables (the games'
// layout), fighters with made-up records
static void test_combat()
{
    static combat::Tables t;
    memset(&t, 0, sizeof t);
    t.ground[0x37][0] = 1; t.ground[0x37][1] = 1;             // floor: cost 1, eye 1
    t.ground[0x01][0] = 0xFF; t.ground[0x01][1] = 1; t.ground[0x01][2] = 2;   // wall
    t.ground[0x1F][0] = 1; t.ground[0x1F][1] = 1;             // a body
    const uint8_t facing[4] = {7, 2, 3, 6};
    memcpy(t.facing, facing, 4);
    for (int f = 0; f < 4; ++f)
        for (int k = 0; k < 4; ++k) {
            t.formation[f][k] = static_cast<uint8_t>(f * 2);
            t.fallback[f][k] = static_cast<uint8_t>(((f + 2) & 3) * 2);
        }
    for (int i = 0; i < 8; ++i) { t.start_x[i] = 5; t.start_y[i] = 2; }
    for (int sh = 0; sh < 5; ++sh)
        for (int r = 0; r < 6; ++r) { t.shapes[sh][r][0] = 0; t.shapes[sh][r][1] = 10; }
    static combat::Battle b;
    b = combat::Battle{};
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = 0x37;
    static uint8_t rec[8][party::kRecordSize];
    memset(rec, 0, sizeof rec);
    for (int i = 0; i < 7; ++i) {
        uint8_t* r = rec[i];
        r[0x196] = 1; r[0x197] = i >= 3; r[0xDE] = 1;
        r[0x78] = r[0x1A4] = 20;
        r[0x1A5] = 12;                                       // movement
        r[0x11C] = 2;                                        // an attack a round
        r[0x199] = 100;                                      // hits all but a 1
        r[0x19A] = 50; r[0x19B] = 48;                        // AC 10
        r[0x19E] = 1; r[0x1A0] = 6;                          // 1d6
        r[0x17] = 10;
        b.f[i].rec = r;
        b.f[i].member = i < 3 ? i : -1;
        b.f[i].monster = i < 3 ? -1 : i - 3;
    }
    b.n = 7;
    b.party_size = 3;
    combat::place(b, t, 2, 2, nullptr, nullptr);
    int on = 0, cells = 0;
    double px = 0, mx = 0;
    for (int i = 0; i < b.n; ++i)
        if (b.f[i].size) {
            ++on;
            (i < 3 ? px : mx) += b.f[i].x;
        }
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x)
            if (b.who[y][x]) ++cells;
    CHECK(on == 7 && cells == 7);
    CHECK(mx / 4 > px / 3);                                  // the enemies stand east of the party
    CHECK(b.f[0].facing == 2 && b.f[3].facing == 6);
    // Moving: 2 a straight step, 3 a diagonal one; others and edges block
    b.f[0].x = 10; b.f[0].y = 10; b.f[1].x = 11; b.f[1].y = 10;
    b.f[2].x = 0; b.f[2].y = 0;
    for (int i = 3; i < 7; ++i) { b.f[i].x = 20 + i; b.f[i].y = 10; }
    combat::occupancy(b);
    bool edge;
    int blocker;
    CHECK(combat::step_cost(b, t, 0, 0, &edge, &blocker) == 2 && combat::step_cost(b, t, 0, 1, &edge, &blocker) == 3);
    CHECK(combat::step_cost(b, t, 0, 2, &edge, &blocker) == 0xFF && blocker == 1);
    CHECK(combat::step_cost(b, t, 2, 7, &edge, &blocker) == 0xFF && edge);
    // Range and sight: 3 squares east = 3; a wall between hides
    int sq;
    CHECK(combat::path(b, t, 0, 13, 10, false, &sq) && sq == 6);
    CHECK(combat::path(b, t, 0, 12, 12, false, &sq) && sq == 6);          // 2 diagonal steps = 3 squares
    b.ground[10][12] = 0x01;
    CHECK(!combat::path(b, t, 0, 14, 10, false, &sq) && combat::path(b, t, 0, 14, 10, true, &sq));
    b.ground[10][12] = 0x37;
    CHECK(combat::direction(10, 10, 14, 10) == 2 && combat::direction(10, 10, 12, 8) == 1 &&
          combat::direction(10, 10, 10, 6) == 0 && combat::direction(10, 10, 11, 14) == 4);
    // Damage: 0 unconscious, below dying (bleeding), -10 dead
    CHECK(combat::damage(b, 4, 20) && b.f[4].status() == party::Unconscious && !b.f[4].up());
    CHECK(combat::damage(b, 5, 23) && b.f[5].status() == party::Dying && b.f[5].bleeding == 3);
    CHECK(combat::damage(b, 6, 30) && b.f[6].status() == party::Dead);
    CHECK(!combat::damage(b, 3, 5) && b.f[3].hp() == 15);
    // A party member down leaves a body
    CHECK(combat::damage(b, 2, 25) && b.ground[0][0] == 0x1F && b.f[2].size == 0);
    // Bleeding: past 9 they're dead; the fight goes on while both sides stand
    b.f[2].bleeding = 9;
    CHECK(!combat::end_round(b) && b.f[2].status() == party::Dead && b.f[5].status() == party::Dying);
    CHECK(combat::anyone_dying(b) == false && combat::bandage(b) == -1);
    // Initiative: everyone standing acts; the next is the highest delay
    create::Dice d(3);
    combat::start_round(b, d);
    CHECK(b.f[0].delay >= 1 && b.f[0].moves == 24 && b.f[0].attacks[0] == 1 && b.f[4].delay == 0);
    const int first = combat::next(b, d);
    CHECK(first >= 0 && b.f[first].up());
    for (int i = 0; i < b.n; ++i)
        if (b.f[i].up()) CHECK(b.f[i].delay <= b.f[first].delay);
    // The monster's turn: next to member 1? No - it steps west toward them
    combat::Plan pl = combat::think(b, t, 3, d);
    CHECK(pl.act == combat::Act::Step && (pl.dir == 6 || pl.dir == 5 || pl.dir == 7));
    // Adjacent: it attacks
    b.f[3].x = 12; b.f[3].y = 10;
    combat::occupancy(b);
    pl = combat::think(b, t, 3, d);
    CHECK(pl.act == combat::Act::Attack && pl.target == 1);
    b.f[3].attacks[0] = 1;
    const combat::Attack at = combat::attack(b, 3, 1, nullptr, d);
    CHECK(at.n == 1 && b.f[3].attacks[0] == 0 && b.no_action == b.round + 15);
    if (at.any) CHECK(b.f[1].hp() < 20);
    // Attacks a round: 3 half attacks = 1, 2, 1, 2
    CHECK(combat::attacks_this_round(3, 0) == 1 && combat::attacks_this_round(3, 1) == 2 &&
          combat::attacks_this_round(2, 1) == 1);
    CHECK(combat::dex_reaction(17) == 2 && combat::dex_reaction(4) == -2 && combat::dex_reaction(10) == 0);
    // The end: the beaten monsters' experience (per HP x rolled HP + base) and coins
    rec[4][0x13E] = 5; rec[4][0x12C] = 10; rec[4][0x13C] = 20;   // 70
    rec[5][0xFB + 3 * 2] = 30;                                    // 30 gold
    const combat::Outcome o = combat::finish(b, nullptr);
    CHECK(o.result == combat::Won && o.exp == 70 && o.money[3] == 30);
    const int m[7] = {0, 0, 0, 400, 0, 1, 0};
    CHECK(combat::money_exp(m) == 400 + 250);
    // Shared by the two standing (member 2 is dead): a fighter with Str 16 gets 10% more
    rec[0][0x75] = 2; rec[0][0x11] = 16;
    CHECK(combat::award(b, 1000) == 500);
    CHECK(rec[0][0x127] == (550 & 0xFF) && rec[0][0x128] == (550 >> 8) && rec[1][0x127] == (500 & 0xFF));
    // ---- Spells in fights (made-up spell lines in the games' layout)
    static uint8_t sds[0x100];
    memset(sds, 0, sizeof sds);
    auto sp = [&](int s2, int cls, int lv, int lasts, int aim, int on_save, int affect) {
        uint8_t* e = sds + s2 * 16;
        e[0] = static_cast<uint8_t>(cls); e[1] = static_cast<uint8_t>(lv); e[4] = static_cast<uint8_t>(lasts);
        e[6] = static_cast<uint8_t>(aim); e[8] = static_cast<uint8_t>(on_save); e[9] = 4;
        e[10] = static_cast<uint8_t>(affect); e[11] = 2;
    };
    sp(1, 2, 1, 0, 0x04, 0, 0);          // missiles: one target, no save
    sp(2, 2, 1, 5, 0x09, 0, 0x35);       // sleep: an area, the sleep effect
    sp(3, 0, 1, 0, 0x04, 0, 0);          // wounds cured
    sp(4, 0, 1, 2, 0x0A, 0, 0x01);       // a blessing for 2 rounds
    classes::Layout sl{};
    sl.lo = 0; sl.hi = 0x100; sl.spells = 0; sl.spell_count = 6;
    static classes::Tables st;
    CHECK(st.set(sl, sds, sizeof sds));
    static combat::Facts fx = {{0x33, 0x34, 0x35, 0x1F}, 0x01, 0x02, 0x31, 0x27, 0x2A, 0x19, 0x08, 0x09, 0x1C};
    static uint8_t mrec[4][party::kRecordSize];
    static uint8_t maff[4][8][party::kAffectSize];
    static int mnaff[4];
    memset(mrec, 0, sizeof mrec);
    memset(maff, 0, sizeof maff);
    static combat::Battle sb;
    sb = combat::Battle{};
    sb.fx = &fx;
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) sb.ground[y][x] = 0x37;
    for (int i = 0; i < 4; ++i) {
        uint8_t* r = mrec[i];
        r[0x196] = 1; r[0x197] = i >= 1; r[0xDE] = 1; r[0x78] = r[0x1A4] = 20; r[0xE5] = 1;
        r[0x1A5] = 12; r[0x11C] = 2; r[0x199] = 100; r[0x19A] = 50; r[0x19E] = 1; r[0x1A0] = 4;
        for (int k = 0; k < 5; ++k) r[0xDF + k] = 30;              // never saves (but on a 20)
        mnaff[i] = 0;
        combat::Fighter& f = sb.f[i];
        f.rec = r; f.aff = maff[i]; f.n_aff = &mnaff[i]; f.max_aff = 8;
        f.member = i == 0 ? 0 : -1;
        f.x = 10 + i; f.y = 10; f.size = 1;
    }
    sb.n = 4;
    sb.party_size = 1;
    mrec[0][0x10E] = 3;                                              // the caster: a 3rd level magic-user
    combat::occupancy(sb);
    combat::SpellLine sline[16];
    int targets[4] = {1, 2, 3, 0};
    const combat::FightSpell mm{1, combat::SpellDoes::Damage, 0, 4, 0, 2, 8, 0};
    int n = combat::cast(sb, st, 0, 1, mm, targets, 1, d, sline, 16);
    CHECK(n == 1 && sline[0].did == combat::Did::Damage && sline[0].amount >= 4 && sline[0].amount <= 10 &&
          sb.f[1].hp() == 20 - sline[0].amount);
    // Sleep: 4d4 Hit Dice' worth (1 each here) - both fall asleep and can't act
    const combat::FightSpell sleep{2, combat::SpellDoes::Sleep, 0, 0, 0, 0, 0, 0x1234};
    n = combat::cast(sb, st, 0, 2, sleep, targets + 1, 2, d, sline, 16);
    CHECK(n == 2 && sline[0].did == combat::Did::Word && combat::helpless(sb, sb.f[2]) && combat::helpless(sb, sb.f[3]));
    combat::start_round(sb, d);
    CHECK(sb.f[2].delay == 0 && sb.f[3].delay == 0 && sb.f[0].delay > 0);
    // A helpless target: one cruel blow (to -5: dying)
    sb.f[0].attacks[0] = 1;
    const combat::Attack slay = combat::attack(sb, 0, 2, nullptr, d);
    CHECK(slay.slain && slay.down && sb.f[2].status() == party::Dying && sb.f[0].attacks[0] == 0);
    // Healing a dying fighter: unconscious with hit points
    const combat::FightSpell cure{3, combat::SpellDoes::Heal, 1, 8, 0, 0, 0, 0};
    n = combat::cast(sb, st, 0, 3, cure, targets + 1, 1, d, sline, 16);   // (fighter 2)
    (void)n;
    int two = 2;
    n = combat::cast(sb, st, 0, 3, cure, &two, 1, d, sline, 16);
    CHECK(n == 1 && sline[0].did == combat::Did::Healed && sb.f[2].status() == party::Unconscious && sb.f[2].hp() > 0);
    // Bless on the caster's side only, for 2 rounds
    const combat::FightSpell bless{4, combat::SpellDoes::Ours, 0, 0, 0, 0, 0, 0x1234};
    int all[4] = {0, 1, 2, 3};
    n = combat::cast(sb, st, 0, 4, bless, all, 4, d, sline, 16);
    CHECK(n == 1 && sline[0].who == 0 && sb.f[0].has(0x01) && !sb.f[1].has(0x01));
    combat::tick(sb);
    CHECK(sb.f[0].has(0x01));
    combat::tick(sb);
    CHECK(!sb.f[0].has(0x01));
    // The area: fighters within 1 square of (11, 10)
    int in[8];
    CHECK(combat::in_area(sb, t, 11, 10, 1, in, 8) == 2);        // 0 and 1 (2 is down, 3 two squares off)
    // Turn undead: the weakest undead first; a positive value turns, 0 or less destroys
    mrec[1][0xE9] = 2; mrec[3][0xE9] = 1;
    mrec[3][0x196] = 1;
    memset(maff[3], 0, sizeof maff[3]); mnaff[3] = 0;
    t.turn[1 * 10 + 3] = 0xFF;                                     // type 1, a 3rd level cleric: destroyed (-1)
    t.turn[2 * 10 + 3] = 1;                                        // type 2: turned
    mrec[0][0x109] = 3;
    int turned[8];
    n = combat::turn_undead(sb, t, 0, d, turned, 8);
    CHECK(n >= 1 && turned[0] == 1003 && sb.f[3].status() == party::Gone);
    if (n > 1) CHECK(turned[1] == 1 && sb.f[1].fleeing);
    // ---- A fresh line: the caster (party) at (10, 10), three enemies east
    memset(mrec, 0, sizeof mrec);
    memset(maff, 0, sizeof maff);
    for (int i = 0; i < 4; ++i) {
        uint8_t* r = mrec[i];
        r[0x196] = 1; r[0x197] = i >= 1; r[0xDE] = 1; r[0x78] = r[0x1A4] = 20; r[0xE5] = 1;
        r[0x1A5] = 12; r[0x19A] = 50;
        for (int k = 0; k < 5; ++k) r[0xDF + k] = 30;
        mnaff[i] = 0;
        combat::Fighter& f = sb.f[i];
        f.fleeing = false;
        f.x = 10 + (i ? i + 1 : 0); f.y = 10; f.size = 1;
    }
    mrec[0][0x10E] = 5;
    combat::occupancy(sb);
    // A cone east from (10, 10) through (12, 10), 6 squares: the line takes
    // 12, 13, 14; one off the line only with a second ray (to the end a
    // square to the right: south); a wall cuts it short
    {
        int cn[8];
        CHECK(combat::cone(sb, t, 0, 12, 10, 6, 1, cn, 8) == 3);
        sb.f[3].x = 15; sb.f[3].y = 11;
        combat::occupancy(sb);
        CHECK(combat::cone(sb, t, 0, 12, 10, 6, 1, cn, 8) == 2);
        CHECK(combat::cone(sb, t, 0, 12, 10, 6, 2, cn, 8) == 3 && cn[2] == 3);
        sb.ground[10][13] = 0x01;
        CHECK(combat::cone(sb, t, 0, 12, 10, 6, 3, cn, 8) == 1 && cn[0] == 1);
        sb.ground[10][13] = 0x37;
        CHECK(combat::cone(sb, t, 0, 10, 10, 6, 3, cn, 8) == 0);          // no direction
        sb.f[3].x = 14; sb.f[3].y = 10;
        combat::occupancy(sb);
    }
    // A bolt from the first target on, away from the caster; a wall stops it
    int line[8];
    CHECK(combat::bolt_line(sb, t, 0, 12, 10, 7, line, 8) == 3 && line[0] == 1 && line[1] == 2 && line[2] == 3);
    sb.ground[10][14] = 0x01;
    sb.f[3].x = 15;
    combat::occupancy(sb);
    CHECK(combat::bolt_line(sb, t, 0, 12, 10, 7, line, 8) == 2);
    sb.ground[10][14] = 0x37;
    CHECK(combat::bolt_line(sb, t, 0, 10, 10, 7, line, 8) == 0);           // no direction
    // A bolt's damage: d6 a level (5), no save here
    sp(5, 2, 3, 0, 0x04, 0, 0);
    sp(6, 2, 2, 0, 0x04, 0, 0);
    sl.spell_count = 8;
    CHECK(st.set(sl, sds, sizeof sds));
    const combat::FightSpell bolt{5, combat::SpellDoes::Bolt, 0, 6, 0, 3, 4, 0};
    int three = 3;
    n = combat::cast(sb, st, 0, 5, bolt, &three, 1, d, sline, 16);
    CHECK(n >= 1 && sline[0].did == combat::Did::Damage && sline[0].amount >= 5 && sline[0].amount <= 30);
    mrec[3][0x1A4] = 20; mrec[3][0x195] = 0; sb.f[3].size = 1;
    combat::occupancy(sb);
    // A cloud: each in it chokes (helpless) or coughs (saved, Word2)
    const combat::FightSpell cloud{6, combat::SpellDoes::Cloud, 0, 0, 0, 0, 0, 0x1111, 0x2222};
    int two3[2] = {2, 3};
    n = combat::cast(sb, st, 0, 6, cloud, two3, 2, d, sline, 16);
    CHECK(n == 2);
    for (int k = 0; k < n; ++k) {
        const bool held = combat::helpless(sb, sb.f[sline[k].who]);
        CHECK((sline[k].did == combat::Did::Word && held) || (sline[k].did == combat::Did::Word2 && !held));
    }
    // Charm Person (spell 4's table entry: 2 rounds, no save): a person joins
    // the caster's side until it runs out; a large one is no person
    {
        fx.charm = 0x0B;
        const combat::FightSpell charm{4, combat::SpellDoes::Charm, 0, 0, 0, 0, 0, 0x3333, 0x4444};
        mrec[1][0x196] = 1; mrec[1][0x195] = 0; mrec[1][0x11A] = 0; mrec[1][0xDE] = 1; mnaff[1] = 0;
        mrec[2][0x196] = 1; mrec[2][0x195] = 0; mrec[2][0xDE] = 0x81; mnaff[2] = 0;
        int ch2[2] = {1, 2};
        n = combat::cast(sb, st, 0, 4, charm, ch2, 2, d, sline, 16);
        CHECK(n == 2 && sline[0].did == combat::Did::Word && sline[1].did == combat::Did::Word2);
        CHECK(sb.f[1].team() == 0 && sb.f[1].has(0x0B) && sb.f[2].team() == 1);
        combat::tick(sb);
        CHECK(sb.f[1].team() == 0);
        combat::tick(sb);
        CHECK(sb.f[1].team() == 1 && !sb.f[1].has(0x0B));
        mrec[2][0xDE] = 1;
        fx.charm = 0;
        // Fear (2 rounds): those failing their save flee until it's over
        fx.fear = 0x8E;
        const combat::FightSpell fear{4, combat::SpellDoes::Fear, 0, 0, 0, 0, 0, 0x5555};
        mnaff[1] = 0; sb.f[1].fleeing = false;
        int one = 1;
        n = combat::cast(sb, st, 0, 4, fear, &one, 1, d, sline, 16);
        CHECK(n == 1 && (sline[0].did == combat::Did::Word) == sb.f[1].fleeing);
        if (sb.f[1].fleeing) {
            CHECK(sb.f[1].has(0x8E));
            combat::tick(sb);
            combat::tick(sb);
            CHECK(!sb.f[1].fleeing && !sb.f[1].has(0x8E));
        }
        fx.fear = 0;
        // ---- spell_facts.md (v0.55.0)
        auto fresh = [&](int i) {
            mrec[i][0x196] = 1; mrec[i][0x195] = 0; mrec[i][0x1A4] = 20; mrec[i][0xE5] = 1; mrec[i][0x11A] = 0;
            mrec[i][0xDE] = 1; mnaff[i] = 0; sb.f[i].size = 1; sb.f[i].fleeing = false;
        };
        int one2 = 1;
        // Slay Living: slain (the skull, no words) or 2d8 + 1 when saved (saves need a 20 here)
        fresh(1);
        const combat::FightSpell slay{4, combat::SpellDoes::Slay, 2, 8, 1, 0, 8, 0x6666};
        n = combat::cast(sb, st, 0, 4, slay, &one2, 1, d, sline, 16);
        CHECK(n >= 1);
        if (sline[0].did == combat::Did::Word)
            CHECK(n == 2 && sline[1].did == combat::Did::Fallen && sb.f[1].status() == party::Dead && !sb.f[1].up());
        else
            CHECK(sline[0].did == combat::Did::Damage && sline[0].amount >= 3 && sline[0].amount <= 17);
        // Poison: poisoned and killed unless saved
        fresh(1);
        const combat::FightSpell poison{4, combat::SpellDoes::Kill, 0, 0, 0, 0, 0, 0x7777};
        n = combat::cast(sb, st, 0, 4, poison, &one2, 1, d, sline, 16);
        CHECK(n == 0 || (n == 2 && sline[0].did == combat::Did::Word && sline[1].did == combat::Did::Down &&
                         sb.f[1].status() == party::Dead));
        // Fumble: clumsy (its moves and attacks gone) or slowed, then clumsy again or unaffected
        fresh(1);
        fx.fumbling = 0x1B; fx.slow = 0x2A;
        sb.f[1].moves = 8; sb.f[1].attacks[0] = 1;
        const combat::FightSpell fumble{4, combat::SpellDoes::Fumble, 0, 0, 0, 0, 0, 0x8888, 0x9999};
        n = combat::cast(sb, st, 0, 4, fumble, &one2, 1, d, sline, 16);
        CHECK(n == 2 && (sline[0].did == combat::Did::Word || sline[0].did == combat::Did::Word2));
        CHECK(sb.f[1].has(0x1B) || sb.f[1].has(0x2A));
        if (sb.f[1].has(0x1B)) CHECK(combat::turn_effects(sb, 1) == combat::TurnFx::Fumbling && sb.f[1].moves == 0);
        // Entangle: nothing indoors; outdoors no moving (24 rounds)
        fresh(1);
        fx.entangle = 0x88;
        const combat::FightSpell ent{4, combat::SpellDoes::Entangle, 0, 0, 0, 0, 0, 0xAAAA};
        sb.indoors = true;
        CHECK(combat::cast(sb, st, 0, 4, ent, &one2, 1, d, sline, 16) == 0 && !sb.f[1].has(0x88));
        sb.indoors = false;
        n = combat::cast(sb, st, 0, 4, ent, &one2, 1, d, sline, 16);
        CHECK(n == 1 && (sline[0].did == combat::Did::Word) == sb.f[1].has(0x88));
        if (sb.f[1].has(0x88)) {
            combat::start_round(sb, d);
            CHECK(sb.f[1].moves == 0);
        }
        sb.indoors = true;
        // Sticks to Snakes: 6 Hit Dice smash them; else the turn goes while the snakes outnumber its attacks
        fresh(1);
        fx.sticks = 0x03;
        const combat::FightSpell sticks{4, combat::SpellDoes::Snakes, 0, 0, 0, 0, 0, 0xBBBB, 0xCCCC};
        mrec[1][0xE5] = 6;
        n = combat::cast(sb, st, 0, 4, sticks, &one2, 1, d, sline, 16);
        CHECK(n == 1 && sline[0].did == combat::Did::Word2 && !sb.f[1].has(0x03));
        mrec[1][0xE5] = 2;
        mrec[0][0x10E] = 5;
        n = combat::cast(sb, st, 0, 4, sticks, &one2, 1, d, sline, 16, 5);
        CHECK(n == 1 && sline[0].did == combat::Did::Word && sb.f[1].has(0x03));
        sb.f[1].attacks[0] = 1; sb.f[1].attacks[1] = 0;
        CHECK(combat::turn_effects(sb, 1) == combat::TurnFx::Snakes);       // 5 - 1 = 4 snakes > 1 attack
        sb.f[1].attacks[0] = 2;
        CHECK(combat::turn_effects(sb, 1) == combat::TurnFx::None && !sb.f[1].has(0x03));  // 4 - 2 = 2 <= 2: gone
        // Silence: the silenced one and those next to it can't cast or use items this turn
        fresh(1); fresh(2);
        fx.silence = 0x15;
        mnaff[1] = 0;
        const combat::FightSpell sil{4, combat::SpellDoes::Affect, 0, 0, 0, 0, 0, 0xDDDD};
        (void)sil;
        uint8_t* a = maff[1][mnaff[1]++];
        memset(a, 0, party::kAffectSize);
        a[0] = 0x15; a[1] = 5;
        sb.f[1].can_use = sb.f[2].can_use = true;
        sb.f[2].x = sb.f[1].x + 1; sb.f[2].y = sb.f[1].y;
        CHECK(combat::turn_effects(sb, 2) == combat::TurnFx::Silenced && !sb.f[2].can_cast);
        sb.f[2].x = sb.f[1].x + 3;
        sb.f[2].can_use = true; sb.f[2].can_cast = true;
        CHECK(combat::turn_effects(sb, 2) == combat::TurnFx::None && sb.f[2].can_cast);
        combat::occupancy(sb);
        fx.fumbling = fx.entangle = fx.sticks = fx.silence = 0;
        fresh(1); fresh(2);
    }
    // The computer's spells: missiles (priority 7, reach 6) at the party member in reach
    sds[1 * 16 + 13] = 7; sds[1 * 16 + 2] = 6;
    CHECK(st.set(sl, sds, sizeof sds));
    mrec[1][0x1E] = 1; mrec[1][0x10E] = 3;
    const combat::FightSpell table[2] = {mm, cure};
    int chosen[8], n_chosen = 0;
    CHECK(combat::choose_spell(sb, t, st, 1, table, 2, d, chosen, &n_chosen) == 1 && n_chosen == 1 && chosen[0] == 0);
    // Only a cure memorised and not hurt: nothing
    mrec[1][0x1E] = 3;
    CHECK(combat::choose_spell(sb, t, st, 1, table, 2, d, chosen, &n_chosen) == 0);
    // Morale: a monster (control 0x8A: 20%) badly hurt while its side is beaten flees,
    // unless the party is faster - then a clever one gives up
    mrec[2][0xF7] = 0x8A;
    sb.enemy_health = 10;
    sb.morale_base = 0;
    CHECK(combat::morale(sb, t, 2) == combat::Morale::Fight);
    mrec[2][0x1A4] = 5;
    CHECK(combat::morale(sb, t, 2) == combat::Morale::Flee);
    mrec[0][0x1A5] = 15; mrec[2][0x13] = 10;
    CHECK(combat::morale(sb, t, 2) == combat::Morale::Surrender);
    mrec[2][0x13] = 3;
    CHECK(combat::morale(sb, t, 2) == combat::Morale::Fight);
    mrec[2][0xF7] = 0x0A;                                                // party-controlled: never
    CHECK(combat::morale(sb, t, 2) == combat::Morale::Fight);
    // The computer's magic items: a readied wand of the missiles
    static items::Names inames;
    std::vector<uint8_t> itypes(2 + items::kTypes * 16, 0);
    dax::MemorySource isrc(itypes.data(), static_cast<uint32_t>(itypes.size()));
    CHECK(inames.read_types(isrc));
    static uint8_t mitems[3][items::kRecordSize];
    memset(mitems, 0, sizeof mitems);
    mitems[1][0x34] = 1; mitems[1][0x3C] = 5; mitems[1][0x3D] = 1;
    sb.f[1].items = mitems;
    sb.f[1].n_items = 2;
    CHECK(combat::choose_item(sb, t, st, 1, inames, table, 2, d, chosen, &n_chosen) == 1 && n_chosen == 1 &&
          chosen[0] == 0);
    mitems[1][0x34] = 0;                                                 // not readied: no
    CHECK(combat::choose_item(sb, t, st, 1, inames, table, 2, d, chosen, &n_chosen) == -1);
    // An item's spell at the item's level: 6 (a monster spell: the user's)
    CHECK(combat::power_of(mrec[1], st, 1, true) == 6 && combat::power_of(mrec[1], st, 1, false) == 3);
    // Missiles: a bow (type 41: range 10, arrows) with its arrows (73) readied;
    // a thrown axe (type 2: range 4, flag 0x10); a sling (type 47: range 8)
    itypes[2 + 41 * 16 + 12] = 10; itypes[2 + 41 * 16 + 14] = 0x0B;
    itypes[2 + 2 * 16 + 12] = 4; itypes[2 + 2 * 16 + 14] = 0x14;
    itypes[2 + 47 * 16 + 12] = 8; itypes[2 + 47 * 16 + 14] = 0x0A;
    dax::MemorySource isrc2(itypes.data(), static_cast<uint32_t>(itypes.size()));
    CHECK(inames.read_types(isrc2));
    sb.names = &inames;
    sb.arrow = 73;
    memset(mitems, 0, sizeof mitems);
    mitems[0][0x2E] = 41; mitems[0][0x34] = 1;
    mitems[1][0x2E] = 73; mitems[1][0x39] = 12;
    int ammo;
    CHECK(combat::missile(sb, sb.f[1], &ammo) == 0);                 // the arrows not readied
    mitems[1][0x34] = 1;
    CHECK(combat::missile(sb, sb.f[1], &ammo) == 9 && ammo == 1);
    mitems[0][0x2E] = 2;
    CHECK(combat::missile(sb, sb.f[1], &ammo) == 3 && ammo == 0);   // thrown: the axe goes
    mitems[0][0x2E] = 47;
    CHECK(combat::missile(sb, sb.f[1], &ammo) == 7 && ammo == -1);  // a sling: nothing goes
    // How shots look in flight: the sling's stone (2 pictures, sound 6)
    static const uint8_t pointed[6] = {9, 21, 100, 28, 31, 73}, spinning[3] = {2, 7, 14}, flask[2] = {85, 86},
                         sling[3] = {47, 98, 101};
    memcpy(fx.shot_pointed, pointed, 6); memcpy(fx.shot_spinning, spinning, 3);
    memcpy(fx.shot_flask, flask, 2); memcpy(fx.shot_sling, sling, 3);
    combat::Flight fl = combat::shot_flight(sb, sb.f[1], -1, 2);
    CHECK(fl.pic == 8 && fl.frames == 2 && fl.slot[0] == 0 && fl.slot[1] == 1 && fl.delay == 10 && fl.sound == 6);
    // a bow's arrow: one picture by direction (east: across; west: its attack
    // picture; south-west: the slant's attack picture mirrored), the whistle
    mitems[0][0x2E] = 41;
    fl = combat::shot_flight(sb, sb.f[1], 1, 2);
    CHECK(fl.pic == 2 && fl.frames == 1 && fl.slot[0] == 0 && fl.sound == 0x0C && fl.delay == 10);
    fl = combat::shot_flight(sb, sb.f[1], 1, 6);
    CHECK(fl.pic == 2 && fl.slot[0] == 1);
    fl = combat::shot_flight(sb, sb.f[1], 1, 4);
    CHECK(fl.pic == 0 && fl.slot[0] == 1);
    fl = combat::shot_flight(sb, sb.f[1], 1, 5);
    CHECK(fl.pic == 1 && fl.slot[0] == 3);
    fl = combat::shot_flight(sb, sb.f[1], 1, 7);
    CHECK(fl.pic == 1 && fl.slot[0] == 2);
    // a thrown axe spins (4 pictures); anything else flies as a rock
    mitems[0][0x2E] = 2;
    fl = combat::shot_flight(sb, sb.f[1], 0, 3);
    CHECK(fl.pic == 3 && fl.frames == 4 && fl.slot[1] == 2 && fl.slot[2] == 3 && fl.slot[3] == 1 && fl.sound == 9 &&
          fl.delay == 50);
    mitems[0][0x2E] = 5;
    fl = combat::shot_flight(sb, sb.f[1], 0, 3);
    CHECK(fl.pic == 7 && fl.frames == 2 && fl.delay == 20 && fl.sound == 9);
    mitems[0][0x2E] = 47;
    // The path: 8-pixel cells, a step short of the target; next door: 2 drawn
    combat::FlightPath fp;
    combat::flight_begin(fp, 2, 2, 5, 3);
    int nfp = 0, lx = 0, ly = 0;
    while (combat::flight_step(fp)) { ++nfp; lx = fp.x; ly = fp.y; }
    CHECK(nfp == 8 && lx == 14 && ly == 9);
    combat::flight_begin(fp, 2, 2, 3, 3);
    nfp = 0;
    while (combat::flight_step(fp)) ++nfp;
    CHECK(nfp == 2 && fp.x == 8 && fp.y == 8);
    combat::flight_begin(fp, 2, 2, 2, 2);
    CHECK(!combat::flight_step(fp));
    // The computer's weapon: a bow (2 hands, 1d6, 2 attacks) with readied
    // arrows rates 8, a long sword (1 hand, 1d8) 11 - the bow while no enemy
    // is next to them (over half the sword), the sword when one is
    {
        static uint8_t keep_items[3][items::kRecordSize], keep_rec[2][party::kRecordSize];
        memcpy(keep_items, mitems, sizeof mitems);
        memcpy(keep_rec, mrec, sizeof keep_rec);
        const int keep_n = sb.f[1].n_items, keep_x0 = sb.f[0].x, keep_x1 = sb.f[1].x, keep_y0 = sb.f[0].y,
                  keep_y1 = sb.f[1].y;
        auto ty = [&](int t, int slot, int hands, int dice, int sides, int attacks, int range, int flags) {
            uint8_t* q = &itypes[2 + t * 16];
            q[0] = (uint8_t)slot; q[1] = (uint8_t)hands; q[5] = (uint8_t)attacks; q[9] = (uint8_t)dice;
            q[10] = (uint8_t)sides; q[12] = (uint8_t)range; q[13] = 0xFF; q[14] = (uint8_t)flags;
        };
        ty(41, 0, 2, 1, 6, 2, 10, 0x0B);
        ty(36, 0, 1, 1, 8, 1, 0, 0x04);
        ty(73, 9, 0, 0, 0, 0, 0, 0);
        dax::MemorySource isrc3(itypes.data(), static_cast<uint32_t>(itypes.size()));
        CHECK(inames.read_types(isrc3));
        memset(mitems, 0, sizeof mitems);
        mitems[0][0x2E] = 41; mitems[0][0x34] = 1;
        mitems[1][0x2E] = 73; mitems[1][0x34] = 1; mitems[1][0x39] = 10;
        mitems[2][0x2E] = 36;
        sb.f[1].items = mitems;
        sb.f[1].n_items = 3;
        mrec[1][0x12B] = 0xFF; mrec[1][0x11E] = 1; mrec[1][0x120] = 2; mrec[1][0x185] = 2;
        mrec[1][0x195] = 0; mrec[1][0x196] = 1; mrec[0][0x196] = 1;
        CHECK(combat::weapon_rating(inames, mitems[0], 0) == 8 && combat::weapon_rating(inames, mitems[2], 0) == 11);
        sb.f[0].x = 10; sb.f[1].x = 12; sb.f[0].y = sb.f[1].y = 10;
        combat::occupancy(sb);
        CHECK(combat::choose_weapon(sb, 1) == combat::kKeep && !combat::missile_in_melee(sb, 1));
        sb.f[0].x = 11;
        combat::occupancy(sb);
        CHECK(combat::missile_in_melee(sb, 1) && combat::choose_weapon(sb, 1) == 2);
        mitems[0][0x36] = 1;                                             // a cursed bow stays
        CHECK(combat::choose_weapon(sb, 1) == combat::kKeep);
        mitems[0][0x36] = 0;
        sb.f[0].x = 10;
        combat::occupancy(sb);
        mitems[1][0x34] = 0;                                             // no arrows readied: the sword
        CHECK(combat::choose_weapon(sb, 1) == 2);
        mitems[0][0x34] = 0; mitems[2][0x34] = 1;                        // holding it: as it is
        CHECK(combat::choose_weapon(sb, 1) == combat::kKeep);
        mitems[1][0x34] = 1;                                             // arrows again: the bow
        CHECK(combat::choose_weapon(sb, 1) == 0);
        // Clouds: 2 x 2 from the square (a wall square stays as it is), the
        // fighters inside, gone after their rounds with the ground back
        t.ground[0x1E][0] = 1; t.ground[0x1E][1] = 1;
        sb.f[0].x = 10; sb.f[0].y = 10; sb.f[1].x = 11; sb.f[1].y = 11;
        combat::occupancy(sb);
        sb.ground[11][10] = 0x01;
        int inside[8];
        CHECK(combat::cloud_fighters(sb, 10, 10, inside, 8) == 2);
        CHECK(combat::lay_cloud(sb, t, 10, 10, 2) && sb.n_clouds == 1);
        CHECK(sb.ground[10][10] == 0x1E && sb.ground[10][11] == 0x1E && sb.ground[11][11] == 0x1E &&
              sb.ground[11][10] == 0x01);
        CHECK(combat::in_cloud(sb, 11, 10) && !combat::in_cloud(sb, 10, 11));
        // a second one over it remembers the floor, not the cloud
        CHECK(combat::lay_cloud(sb, t, 11, 10, 1) && sb.n_clouds == 2);
        CHECK(sb.clouds[1].ground[0] == 0x37);
        CHECK(combat::clouds_round(sb) == 1 && sb.n_clouds == 1);       // the second goes; the first stays cloud
        CHECK(sb.ground[10][11] == 0x1E && sb.ground[10][12] == 0x37 && sb.ground[11][12] == 0x37);
        CHECK(combat::clouds_round(sb) == 1 && sb.n_clouds == 0);
        CHECK(sb.ground[10][10] == 0x37 && sb.ground[10][11] == 0x37 && sb.ground[11][10] == 0x01);
        sb.ground[11][10] = 0x37;
        // Cloudkill: 3 x 3 round the square; Hit Dice 4 dies, 7 is unaffected
        t.ground[0x1C][0] = 1; t.ground[0x1C][1] = 1;
        CHECK(combat::cloud_fighters(sb, 11, 11, inside, 8, true) == 2);
        CHECK(combat::lay_cloud(sb, t, 11, 11, 1, true));
        CHECK(sb.ground[10][10] == 0x1C && sb.ground[12][12] == 0x1C && combat::in_poison(sb, 11, 11) &&
              !combat::in_cloud(sb, 11, 11));
        {
            create::Dice dd(5);
            mrec[1][0xE5] = 7;
            CHECK(!combat::breathe_poison(sb, 1, dd) && sb.f[1].up());
            mrec[1][0xE5] = 4;
            CHECK(combat::breathe_poison(sb, 1, dd) && sb.f[1].status() == party::Dead);
            mrec[1][0x195] = 0; mrec[1][0x196] = 1; mrec[1][0x1A4] = 20; sb.f[1].size = 1;
        }
        CHECK(combat::clouds_round(sb) == 1 && sb.ground[10][10] == 0x37 && sb.ground[12][12] == 0x37);
        combat::occupancy(sb);
        // breathing it: a save or helpless
        {
            create::Dice dd(3);
            int word = 0, word2 = 0;
            for (int k = 0; k < 40; ++k) {
                mrec[1][0x196] = 1; mrec[1][0x195] = 0;
                if (sb.f[1].n_aff) *sb.f[1].n_aff = 0;
                const combat::Did r = combat::breathe_cloud(sb, 1, dd);
                if (r == combat::Did::Word) ++word;
                if (r == combat::Did::Word2) ++word2;
            }
            CHECK(word + word2 == 40);
        }
        memcpy(mitems, keep_items, sizeof mitems);
        memcpy(mrec, keep_rec, sizeof keep_rec);
        sb.f[1].n_items = keep_n;
        sb.f[0].x = keep_x0; sb.f[1].x = keep_x1; sb.f[0].y = keep_y0; sb.f[1].y = keep_y1;
        combat::occupancy(sb);
    }
    // The computer shoots at the party member 2 squares off (not next to it)
    sb.f[1].attacks[0] = 1;
    sb.f[1].delay = 1;
    sb.f[1].target = -1;
    mrec[1][0x195] = 0; mrec[0][0x195] = 0;
    sb.f[0].x = 10; sb.f[1].x = 12;
    combat::occupancy(sb);
    const combat::Plan shot = combat::think(sb, t, 1, d);
    CHECK(shot.act == combat::Act::Attack && shot.missile && shot.target == 0);
    // An unarmoured magic-user without a shot doesn't step forward: it guards
    {
        const uint8_t keep_t = mitems[0][0x2E], keep_mu = mrec[1][0x10E];
        mitems[0][0x2E] = 36;                                           // (a sword: no shot)
        mrec[1][0x10E] = 3;
        sb.f[1].x = 14;
        sb.f[1].moves = 12;
        combat::occupancy(sb);
        CHECK(combat::think(sb, t, 1, d).act == combat::Act::Guard);
        mrec[1][0x10E] = 0;
        CHECK(combat::think(sb, t, 1, d).act == combat::Act::Step);
        mitems[0][0x2E] = keep_t; mrec[1][0x10E] = keep_mu;
        sb.f[1].x = 12;
        combat::occupancy(sb);
    }
    // Sweeps, backstabs, free attacks: the party member at (10, 10), three
    // weak enemies (under 1 Hit Die) round them
    sb.ground[10][14] = 0x37;
    const int sx4[4] = {10, 11, 11, 10}, sy4[4] = {10, 10, 11, 11};
    for (int i = 0; i < 4; ++i) {
        mrec[i][0x195] = 0; mrec[i][0x196] = 1; mrec[i][0x1A4] = 20; mrec[i][0xE5] = 0;
        memset(maff[i], 0, sizeof maff[i]); mnaff[i] = 0;
        sb.f[i].x = sx4[i]; sb.f[i].y = sy4[i]; sb.f[i].size = 1;
        sb.f[i].received = sb.f[i].turns = 0; sb.f[i].delay = 0;
        sb.f[i].items = nullptr; sb.f[i].n_items = 0;
    }
    combat::occupancy(sb);
    int swept[8];
    sb.f[0].attacks[0] = 1;
    CHECK(combat::sweep(sb, 0, 1, swept, 8) == 0);                 // not a fighter
    mrec[0][0x10B] = 3;                                            // a 3rd level fighter
    CHECK(combat::sweep(sb, 0, 1, swept, 8) == 3 && swept[0] == 1);
    sb.f[0].attacks[0] = 3;
    CHECK(combat::sweep(sb, 0, 1, swept, 8) == 0);                 // as many attacks as the level
    sb.f[0].attacks[0] = 1;
    mrec[2][0xE5] = 2;                                             // one of them stronger
    CHECK(combat::sweep(sb, 0, 1, swept, 8) == 2);
    // A thief straight behind one that's had an attack already
    sb.f[1].facing = 2;                                            // facing east, away from the thief
    sb.f[1].received = 2;
    CHECK(!combat::can_backstab(sb, 0, 1, nullptr));
    mrec[0][0x10F] = 5;
    CHECK(combat::can_backstab(sb, 0, 1, nullptr));
    sb.f[1].facing = 6;
    CHECK(!combat::can_backstab(sb, 0, 1, nullptr));
    // The free attack: an enemy that has acted and been attacked hits only with them in its front
    sb.f[1].received = 1;
    sb.f[1].facing = 2;
    CHECK(!combat::free_attack_ok(sb, t, 1, 0));
    sb.f[1].facing = 7;                                            // north-west: the thief (west) is in front
    CHECK(combat::free_attack_ok(sb, t, 1, 0));
    sb.f[1].facing = 2;
    sb.f[1].delay = 3;                                             // not acted yet
    CHECK(combat::free_attack_ok(sb, t, 1, 0));
}

// Monster special abilities (monster_fx_facts.md), on made-up fighters
static void test_monster_fx()
{
    static combat::Tables t;
    memset(&t, 0, sizeof t);
    t.ground[0x37][0] = 1; t.ground[0x37][1] = 1;
    static combat::Facts fx;
    fx = combat::Facts{};
    const uint8_t held[4] = {0x33, 0x34, 0x35, 0x1F};
    memcpy(fx.held, held, 4);
    fx.invisible = 0x19; fx.charm = 0x0B; fx.prot_evil = 0x08; fx.prot_good = 0x09;
    combat::MonFx& m = fx.mon;
    m.poison = 0x40; m.engulf = 0x39; m.chill = 0x7B; m.stop_normal = 0x3C; m.half = 0x51;
    m.no_fire = 0x70; m.absorb_elec = 0x54; m.resist_cold = 0x0A; m.efreet = 0x71; m.mr50 = 0x69;
    m.charm_sleep = 0x6C; m.mind = 0x7D; m.regen_hit = 0x65; m.regen_wait = 0x3B; m.regen = 0x62;
    m.troll_death = 0x64; m.troll_up = 0x66; m.start_invisible = 0x8A; m.detect = 0x18;
    m.acid_breath = 0x5A; m.rays = 0x57; m.suffocate = 0x0D; m.held_fast = 0x3A; m.engulfing = 0x8B;
    m.paralyzed = 0x34; m.poisoned = 0x37; m.sleep = 0x35;
    static uint8_t rec[6][party::kRecordSize];
    static uint8_t aff[6][combat::kMonsterAffects][party::kAffectSize];
    static int naff[6];
    static combat::Battle b;
    create::Dice d(11);
    // Fighters 0-2 the party (west), 3-5 monsters; all hit but on a 1, never save but on a 20
    auto setup = [&]() {
        b = combat::Battle{};
        b.fx = &fx;
        for (int y = 0; y < combat::kH; ++y)
            for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = 0x37;
        memset(rec, 0, sizeof rec);
        memset(aff, 0, sizeof aff);
        for (int i = 0; i < 6; ++i) {
            uint8_t* r = rec[i];
            r[0x196] = 1; r[0x197] = i >= 3; r[0xDE] = 1; r[0x78] = r[0x1A4] = 100; r[0xE5] = 1;
            r[0x1A5] = 12; r[0x11C] = 2; r[0x199] = 100; r[0x19A] = 50; r[0x19B] = 50;
            r[0x19E] = 1; r[0x1A0] = 1; r[0x1A2] = 9;              // 1d1 + 9 = 10
            for (int k = 0; k < 5; ++k) r[0xDF + k] = 30;
            naff[i] = 0;
            combat::Fighter& f = b.f[i];
            f = combat::Fighter{};
            f.rec = r; f.aff = aff[i]; f.n_aff = &naff[i]; f.max_aff = combat::kMonsterAffects;
            f.member = i < 3 ? i : -1;
            f.monster = i < 3 ? -1 : i - 3;
            f.x = i < 3 ? 10 : 11 + (i - 3) * 4; f.y = 10 + (i < 3 ? i * 3 : 0); f.size = 1;
        }
        b.n = 6;
        b.party_size = 3;
        combat::occupancy(b);
    };
    auto give = [&](int i, uint8_t type, int data = 0xFF) {
        uint8_t* a = aff[i][naff[i]++];
        a[0] = type; a[3] = static_cast<uint8_t>(data);
    };
    auto has_ev = [](const combat::Attack& a, combat::Ev e) {
        for (int k = 0; k < a.n_ev; ++k)
            if (a.ev[k].ev == e) return true;
        return false;
    };
    // Only magic weapons hurt it: bare hands do nothing; half damage halves
    setup();
    give(3, m.stop_normal);
    b.f[0].attacks[0] = 1;
    combat::Attack at = combat::attack(b, 0, 3, nullptr, d);
    CHECK(b.f[3].hp() == 100);
    setup();
    give(3, m.half);
    b.f[0].attacks[0] = 1;
    at = combat::attack(b, 0, 3, nullptr, d);
    CHECK(!at.hits[0].hit || (at.hits[0].damage == 5 && b.f[3].hp() == 95));
    // A poisonous bite (attack 2): a save or killed; immune to poison: always saved
    setup();
    give(3, m.poison);
    rec[3][0x19F] = 1; rec[3][0x1A1] = 1;
    b.f[3].attacks[1] = 1;
    at = combat::attack(b, 3, 0, nullptr, d);
    CHECK(!at.hits[0].hit || (has_ev(at, combat::Ev::Poisoned) && at.down && b.f[0].status() == party::Dead));
    setup();
    give(3, m.poison);
    give(0, m.mind);
    b.f[3].attacks[1] = 1;
    at = combat::attack(b, 3, 0, nullptr, d);
    CHECK(!has_ev(at, combat::Ev::Poisoned) && b.f[0].up());
    // The chill touch: 2d8 more, cold; resist cold halves it
    setup();
    give(3, m.chill);
    b.f[3].attacks[0] = 1;
    at = combat::attack(b, 3, 0, nullptr, d);
    if (at.hits[0].hit) {
        CHECK(has_ev(at, combat::Ev::Damage) && at.ev[0].kind == 0x0A && at.ev[0].amount >= 2 && at.ev[0].amount <= 16);
        CHECK(b.f[0].hp() == 100 - 10 - at.ev[0].amount);
    }
    // Engulfing: both swings of attack 1 hit - held fast, suffocating; let go
    // when the mound's next turn begins; suffocated at 0 breaths
    setup();
    give(3, m.engulf);
    b.f[3].x = 11;
    b.f[3].attacks[0] = 2;
    combat::occupancy(b);
    at = combat::attack(b, 3, 0, nullptr, d);
    if (at.hits[0].hit && at.hits[1].hit) {
        CHECK(has_ev(at, combat::Ev::Engulfs) && b.f[0].has(m.held_fast) && b.f[0].has(m.suffocate) &&
              b.f[3].has(m.engulfing));
        combat::release_holds(b, 3);
        CHECK(!b.f[0].has(m.held_fast) && !b.f[0].has(m.suffocate) && !b.f[3].has(m.engulfing));
    }
    setup();
    give(0, m.suffocate, 1);
    CHECK(combat::turn_effects(b, 0) == combat::TurnFx::None && aff[0][0][3] == 0);
    CHECK(combat::turn_effects(b, 0) == combat::TurnFx::Suffocates && b.f[0].status() == party::Dead);
    // The damage routine: immune to fire, lightning heals 8, resist cold
    // halves, the efreet's -1 a die (at least the dice)
    setup();
    give(3, m.no_fire);
    give(4, m.absorb_elec);
    give(5, m.resist_cold);
    combat::Harm h;
    h.kind = 1;
    bool down = false, res = false;
    CHECK(combat::harm(b, 3, 30, h, d, &down, &res) == 0 && res && b.f[3].hp() == 100);
    h.kind = 4;
    CHECK(combat::harm(b, 4, 30, h, d, &down, &res) == 0 && b.f[4].hp() == 108);
    h.kind = 2;
    CHECK(combat::harm(b, 5, 30, h, d, &down) == 15 && b.f[5].hp() == 85);
    aff[3][0][0] = m.efreet;
    h.kind = 1;
    h.dice = 6;
    CHECK(combat::harm(b, 3, 20, h, d, &down) == 14 && combat::harm(b, 3, 7, h, d, &down) == 6);
    // Magic resistance: 50% by the caster's level (0: always), charm / sleep immunity
    setup();
    give(3, m.mr50);
    give(4, m.charm_sleep);
    combat::Harm sp;
    sp.level = 0;
    CHECK(combat::resists(b, 3, 0x01, sp, d) && !combat::resists(b, 3, 0, sp, d));
    CHECK(combat::resists(b, 4, m.sleep, sp, d) && !combat::resists(b, 4, 0x01, sp, d));
    // The troll: hurt, it regenerates 3 rounds later (3 a round); fallen to
    // a blow it gets up again with all its hit points; to fire it stays down
    setup();
    give(3, m.regen_hit);
    give(3, m.troll_death);
    b.f[0].attacks[0] = 1;
    b.f[3].x = 11;
    combat::occupancy(b);
    at = combat::attack(b, 0, 3, nullptr, d);
    if (at.hits[0].hit) {
        CHECK(b.f[3].has(m.regen_wait) && b.f[3].hp() == 90);
        combat::tick(b);
        combat::tick(b);
        CHECK(!b.f[3].has(m.regen));
        combat::tick(b);
        CHECK(b.f[3].has(m.regen) && b.f[3].hp() == 93);
    }
    combat::start_round(b, d);
    CHECK(combat::damage(b, 3, 200) && !b.f[3].up() && b.f[3].has(m.troll_up) && !b.f[3].has(m.regen));
    combat::Event ev[4];
    int rose = 0;
    for (int r = 0; r < 20 && !rose; ++r) rose = combat::tick(b, ev, 4);
    CHECK(rose == 1 && ev[0].ev == combat::Ev::StandsUp && b.f[3].up() && b.f[3].hp() == 100 && b.f[3].size == 1);
    h = combat::Harm{};
    h.kind = 1;
    CHECK(combat::harm(b, 3, 200, h, d, &down) == 200 && down && !b.f[3].has(m.troll_up));
    // Invisible from the start: hidden but from those who see the invisible
    setup();
    give(3, m.start_invisible);
    give(1, m.detect);
    combat::battle_start(b);
    CHECK(b.f[3].has(fx.invisible) && combat::hidden(b, 0, 3) && !combat::hidden(b, 1, 3));
    // Acid breath: down the line at one within 6 (the dragon's maximum hit
    // points, a save for half), 3 a fight - not with a friend on the line
    setup();
    give(3, m.acid_breath);
    rec[3][0x78] = 48;
    b.f[3].x = 14; b.f[3].y = 10;
    b.f[4].x = 30; b.f[5].x = 34;
    combat::occupancy(b);
    combat::Event out[16];
    bool ends = false;
    int n = combat::special(b, t, 3, d, out, 16, &ends);
    CHECK(n >= 3 && ends && out[0].ev == combat::Ev::BreathesAcid && out[1].ev == combat::Ev::Fly);
    CHECK(out[2].who == 0 && out[2].ev == combat::Ev::Damage && out[2].amount == 48 && b.f[0].hp() == 52);
    CHECK(aff[3][0][3] == 2);
    b.f[4].x = 12;                                              // a friend on the line: no breath
    combat::occupancy(b);
    b.round = 1;
    n = combat::special(b, t, 3, d, out, 16, &ends);
    CHECK(n == 0 && !ends && aff[3][0][3] == 2);
    // The beholder's eyes: next to it, disintegrated (never saving), then the
    // next nearest; past 5 squares the spells (Fear, Slow, Sleep)
    setup();
    m.ray_spells[0] = 0x54; m.ray_spells[1] = 0x37; m.ray_spells[2] = 0x15;
    give(3, m.rays);
    b.f[3].x = 11;
    combat::occupancy(b);
    n = combat::special(b, t, 3, d, out, 16, &ends);
    CHECK(n >= 3 && out[0].ev == combat::Ev::RayDisintegrate && !ends);
    CHECK(b.f[0].status() == party::Gone && !b.f[0].up());
    int casts = 0;
    for (int k = 0; k < n; ++k) casts += out[k].ev == combat::Ev::Cast;
    CHECK(casts <= 3);
}

// The second batch of fight spells (spell_facts.md 2), on made-up fighters
static void test_spells_batch2()
{
    static combat::Tables t;
    memset(&t, 0, sizeof t);
    t.ground[0x37][0] = 1; t.ground[0x37][1] = 1;
    static combat::Facts fx;
    fx = combat::Facts{};
    const uint8_t held[4] = {0x33, 0x34, 0x35, 0x1F};
    memcpy(fx.held, held, 4);
    fx.fear = 0x8E; fx.bestow = 0x24; fx.charm = 0x0B; fx.bless = 0x01;
    fx.sp = combat::SpellFx{0x0C, 0x23, 0x89, 0x04, 0x91, 0x32, 0x36, 0x8F};
    fx.mon.held_fast = 0x3A; fx.mon.hugging = 0x90;
    static uint8_t rec[4][party::kRecordSize];
    static uint8_t aff[4][combat::kMonsterAffects][party::kAffectSize];
    static uint8_t its[4][1][items::kRecordSize];
    static int naff[4];
    static combat::Battle b;
    create::Dice d(5);
    auto setup = [&]() {
        b = combat::Battle{};
        b.fx = &fx;
        for (int y = 0; y < combat::kH; ++y)
            for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = 0x37;
        memset(rec, 0, sizeof rec);
        memset(aff, 0, sizeof aff);
        memset(its, 0, sizeof its);
        for (int i = 0; i < 4; ++i) {
            uint8_t* r = rec[i];
            r[0x196] = 1; r[0x197] = i >= 2; r[0xDE] = 1; r[0x78] = r[0x1A4] = 100; r[0xE5] = 1;
            r[0x1A5] = 12; r[0x11C] = 2; r[0x199] = 100; r[0x19A] = 50; r[0x19B] = 50;
            r[0x19E] = 1; r[0x1A0] = 1; r[0x1A2] = 9;
            r[0x11] = 12;
            for (int k = 0; k < 5; ++k) r[0xDF + k] = 30;
            naff[i] = 0;
            combat::Fighter& f = b.f[i];
            f = combat::Fighter{};
            f.rec = r; f.aff = aff[i]; f.n_aff = &naff[i]; f.max_aff = combat::kMonsterAffects;
            f.items = its[i]; f.n_items = 1;
            f.member = i < 2 ? i : -1;
            f.monster = i < 2 ? -1 : i - 2;
            f.x = 10 + i; f.y = 10; f.size = 1;
        }
        b.n = 4;
        b.party_size = 2;
        combat::occupancy(b);
    };
    static uint8_t sds[0x60 * 16];
    memset(sds, 0, sizeof sds);
    auto sp = [&](int s2, int lasts, int per, int aim, int on_save) {
        uint8_t* e = sds + s2 * 16;
        e[0] = 2; e[1] = 3; e[4] = static_cast<uint8_t>(lasts); e[5] = static_cast<uint8_t>(per);
        e[6] = static_cast<uint8_t>(aim); e[8] = static_cast<uint8_t>(on_save); e[9] = 4; e[11] = 2;
    };
    sp(0x0C, 0, 10, 4, 0);
    sp(0x0D, 0, 10, 4, 1);
    sp(0x2B, 0, 0, 4, 0);
    sp(0x52, 2, 1, 11, 1);
    sp(0x49, 0, 1, 0, 0);
    sp(0x55, 2, 1, 0, 0);
    sp(0x29, 0, 0, 9, 0);
    classes::Layout sl{};
    sl.lo = 0; sl.hi = sizeof sds; sl.spells = 0; sl.spell_count = 0x60;
    static classes::Tables st;
    CHECK(st.set(sl, sds, sizeof sds));
    combat::SpellLine out[16];
    int who[4] = {0, 1, 2, 3};
    // Enlarge (level 6: 18/00) on Strength 12; Reduce takes it (a failed save)
    setup();
    rec[0][0x10E] = 6;
    const combat::FightSpell enl{0x0C, combat::SpellDoes::Enlarge, 0, 0, 0, 0, 0, 1};
    int n = combat::cast(b, st, 0, 0x0C, enl, who + 1, 1, d, out, 16);
    CHECK(n == 1 && out[0].did == combat::Did::Word && b.f[1].has(0x0C) && aff[1][0][3] == 101 && aff[1][0][1] == 60);
    rec[1][0x11] = 18; rec[1][0x1C] = 100;
    n = combat::cast(b, st, 0, 0x0C, enl, who + 1, 1, d, out, 16);
    CHECK(n == 1 && out[0].did == combat::Did::Unaffected);
    const combat::FightSpell red{0x0D, combat::SpellDoes::Reduce, 0, 0, 0, 0, 0, 1};
    n = combat::cast(b, st, 0, 0x0D, red, who + 1, 1, d, out, 16);
    CHECK(n == 1 && !b.f[1].has(0x0C));
    // Remove Curse: the curse cured, else the cursed item off
    setup();
    aff[1][0][0] = 0x24; naff[1] = 1;
    its[1][0][0x36] = 1; its[1][0][0x34] = 1;
    const combat::FightSpell rc{0x2B, combat::SpellDoes::RemoveCurse, 0, 0, 0, 0, 0, 1, 2, 3};
    n = combat::cast(b, st, 0, 0x2B, rc, who + 1, 1, d, out, 16);
    CHECK(n == 2 && out[0].did == combat::Did::Word2 && out[1].did == combat::Did::Word && !b.f[1].has(0x24) &&
          its[1][0][0x34] == 1);
    n = combat::cast(b, st, 0, 0x2B, rc, who + 1, 1, d, out, 16);
    CHECK(n == 1 && out[0].did == combat::Did::Word3 && its[1][0][0x34] == 0 && its[1][0][0x36] == 1);
    // Confusion: confused for 2 + level rounds; its turns by d100
    setup();
    rec[0][0x10E] = 5;
    const combat::FightSpell cf{0x52, combat::SpellDoes::Confuse, 0, 0, 0, 0, 0, 1};
    n = combat::cast(b, st, 0, 0x52, cf, who + 2, 2, d, out, 16);
    CHECK(n >= 1 && b.f[2].has(0x23) == (out[0].did == combat::Did::Word));
    combat::start_round(b, d);
    int seen[9] = {};
    for (int r = 0; r < 60; ++r) {
        if (!b.f[2].has(0x23)) { aff[2][naff[2]][0] = 0x23; aff[2][naff[2]][1] = 9; ++naff[2]; }
        b.f[2].fleeing = false;
        rec[2][0x197] = 1;
        ++seen[static_cast<int>(combat::turn_effects(b, 2))];
        while (naff[2]) aff[2][--naff[2]][0] = 0;
    }
    CHECK(seen[static_cast<int>(combat::TurnFx::Confused)] > 0 && seen[static_cast<int>(combat::TurnFx::Berserk)] > 0 &&
          seen[static_cast<int>(combat::TurnFx::Enraged)] > 0 && seen[static_cast<int>(combat::TurnFx::RunsAway)] > 0);
    // Berserk: at the nearest (its own side's fighter 3? no - fighter 1 at 11), a round; then its side again
    setup();
    combat::start_round(b, d);
    aff[2][0][0] = 0x23; aff[2][0][1] = 9; naff[2] = 1;
    combat::TurnFx tf = combat::TurnFx::None;
    for (int r = 0; r < 200 && tf != combat::TurnFx::Berserk; ++r) {
        if (!b.f[2].has(0x23)) { aff[2][0][0] = 0x23; aff[2][0][1] = 9; naff[2] = 1; }
        b.f[2].fleeing = false;
        tf = combat::turn_effects(b, 2);
        if (tf != combat::TurnFx::Berserk) { naff[2] = 0; memset(aff[2], 0, sizeof aff[2]); }
    }
    CHECK(tf == combat::TurnFx::Berserk && b.f[2].has(0x89));
    combat::tick(b);
    CHECK(!b.f[2].has(0x89) && b.f[2].team() == 1);
    // Dispel Evil: the evil's attacks on the caster -7; its blow dispels them (they never save)
    setup();
    const combat::FightSpell de{0x49, combat::SpellDoes::DispelEvil, 0, 0, 0, 0, 0, 1};
    rec[0][0x10E] = 0; rec[0][0x109] = 9;
    n = combat::cast(b, st, 0, 0x49, de, who, 1, d, out, 16);
    CHECK(n == 1 && b.f[0].has(0x04) && b.f[0].has(0x91));
    rec[2][0x14B] = 1;
    b.f[0].x = 11; b.f[2].x = 12; b.f[1].x = 30;
    combat::occupancy(b);
    b.f[0].attacks[0] = 1;
    combat::Attack at = combat::attack(b, 0, 2, nullptr, d);
    CHECK(!at.hits[0].hit || (b.f[2].status() == party::Gone && at.down && !b.f[0].has(0x04)));
    // Fire shield: hot (said), the zap: one hitting it from next to it takes twice its blow
    setup();
    b.flame = 1;
    const combat::FightSpell fsh{0x55, combat::SpellDoes::FireShield, 0, 0, 0, 0, 0, 1};
    n = combat::cast(b, st, 0, 0x55, fsh, who, 1, d, out, 16);
    CHECK(n == 1 && b.f[0].has(0x32) && b.f[0].has(0x8F) && b.flame == 0);
    b.f[2].x = 11; b.f[1].x = 30;
    combat::occupancy(b);
    b.f[2].attacks[0] = 1;
    at = combat::attack(b, 2, 0, nullptr, d);
    CHECK(!at.hits[0].hit || (b.f[2].hp() == 80 && b.f[0].hp() == 90 && at.n_ev >= 2 && at.ev[0].ev == combat::Ev::Zapped));
    // Dispel Magic: a level 12 dispeller against level 1 effects (95%); the 0xFF ones stay
    setup();
    rec[0][0x10E] = 12;
    aff[3][0][0] = 0x01; aff[3][0][3] = 1;
    aff[3][1][0] = 0x40; aff[3][1][3] = 0xFF;
    naff[3] = 2;
    const combat::FightSpell dm{0x29, combat::SpellDoes::Dispel, 0, 0, 0, 0, 0, 1};
    n = combat::cast(b, st, 0, 0x29, dm, who + 3, 1, d, out, 16);
    CHECK(b.f[3].has(0x40) && (b.f[3].has(0x01) == (n == 0)));
    // Its clouds: the one next to the square aimed at ends as the round does
    CHECK(combat::lay_cloud(b, t, 20, 5, 1) && b.clouds[0].level == 1);
    b.clouds[0].rounds = 5;
    int cleared = 0;
    for (int r = 0; r < 5 && !cleared; ++r) { b.clouds[0].resisted = false; cleared = combat::dispel_clouds(b, 21, 5, 12, d); }
    CHECK(cleared == 1 && b.clouds[0].rounds == 1);
    // Dimension Door: to an empty square; a hug lets go
    setup();
    aff[0][0][0] = 0x3A; aff[0][0][3] = 0; naff[0] = 1;
    aff[3][0][0] = 0x90; aff[3][0][3] = 0; naff[3] = 1;
    CHECK(!combat::teleport(b, t, 0, 11, 10) && combat::teleport(b, t, 0, 20, 12));
    CHECK(b.f[0].x == 20 && b.who[12][20] == 1 && !b.f[0].has(0x3A) && !b.f[3].has(0x90));
    // Picking one by one: costs by size / Hit Dice; done with 2+ over the budget
    setup();
    rec[2][0xDE] = 1; rec[3][0xDE] = 4; rec[2][0xE5] = 3; rec[3][0xE5] = 1;
    CHECK(combat::pick_cost(b.f[2], true) == 1 && combat::pick_cost(b.f[3], true) == 4 &&
          combat::pick_cost(b.f[2], false) == 4 && combat::pick_cost(b.f[3], false) == 1);
    int two[2] = {2, 3};
    CHECK(!combat::picks_done(b, two, 1, 0, true) && combat::picks_done(b, two, 2, 4, true) &&
          !combat::picks_done(b, two, 2, 5, true));
    // Coughing: the rear AC 2 worse from any side (0x37 rear -> 0x35)
    fx.cough = 0x1E;
    aff[2][0][0] = 0x1E; naff[2] = 1;
    rec[2][0x19A] = 60; rec[2][0x19B] = 0x37;
    rec[0][0x199] = 0x35 - 2;                                   // hits AC 0x35 on a 2 or better... (d20 + 0x33)
    b.f[0].x = 11; b.f[2].x = 12; b.f[1].x = 30;
    combat::occupancy(b);
    int hits = 0;
    for (int r = 0; r < 40; ++r) {
        b.f[0].attacks[0] = 1;
        rec[2][0x1A4] = 100;
        hits += combat::attack(b, 0, 2, nullptr, d).any ? 1 : 0;
    }
    CHECK(hits > 30);                                           // (front AC 60 would need a 9)
    fx.cough = 0;
    // The original's bolt (curse_finish_facts.md 2.5): caster at x 10, row 10
    setup();
    t.ground[0x01][0] = 0xFF;
    for (int i = 1; i < 4; ++i) b.f[i].size = 0;
    b.f[0].x = 10; b.f[0].y = 10;
    b.indoors = true;
    combat::occupancy(b);
    int bh[16], nsg = 0;
    combat::BoltSeg sg[16];
    int nh = combat::bolt_path(b, t, 0, 13, 10, 7, true, bh, 16, sg, &nsg, 16);
    CHECK(nh == 0 && nsg == 1 && sg[0].x1 == 20 && sg[0].y1 == 10);                 // open floor: 7 squares on
    b.ground[10][16] = 0x01;                                                         // a wall 6 squares out
    nh = combat::bolt_path(b, t, 0, 13, 10, 7, true, bh, 16, sg, &nsg, 16);
    CHECK(nh == 1 && bh[0] == 0 && nsg >= 2 && sg[0].x1 == 16 && sg[1].x1 == 12);   // back: the caster hit
    b.indoors = false;                                                               // outdoors: no bounce
    nh = combat::bolt_path(b, t, 0, 13, 10, 7, true, bh, 16, sg, &nsg, 16);
    CHECK(nh == 0 && sg[nsg - 1].x1 == 20);
}

// Slow Poison, Spiritual Hammer, Animate Dead, Restoration
static void test_spells_batch3()
{
    static combat::Facts fx;
    fx = combat::Facts{};
    fx.mon.poisoned = 0x37; fx.mon.troll_up = 0x66;
    fx.sp.slow_poison = 0x16; fx.sp.poison_damage = 0x0F; fx.sp.hammer = 0x17; fx.sp.animated = 0x20;
    static uint8_t rec[3][party::kRecordSize];
    static uint8_t aff[3][combat::kMonsterAffects][party::kAffectSize];
    static int naff[3];
    static combat::Battle b;
    create::Dice d(9);
    b = combat::Battle{};
    b.fx = &fx;
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = 0x37;
    memset(rec, 0, sizeof rec);
    memset(aff, 0, sizeof aff);
    for (int i = 0; i < 3; ++i) {
        uint8_t* r = rec[i];
        r[0x196] = 1; r[0x197] = i == 2; r[0xDE] = 1; r[0x78] = r[0x1A4] = 20;
        naff[i] = 0;
        combat::Fighter& f = b.f[i];
        f = combat::Fighter{};
        f.rec = r; f.aff = aff[i]; f.n_aff = &naff[i]; f.max_aff = combat::kMonsterAffects;
        f.member = i < 2 ? i : -1;
        f.monster = i < 2 ? -1 : 0;
        f.x = 10 + i * 3; f.y = 10; f.size = 1;
    }
    b.n = 3;
    b.party_size = 2;
    combat::occupancy(b);
    static uint8_t sds[0x40 * 16];
    memset(sds, 0, sizeof sds);
    uint8_t* e = sds + 0x1A * 16;
    e[0] = 0; e[1] = 2; e[5] = 60; e[6] = 4; e[11] = 2;
    e = sds + 0x24 * 16;
    e[0] = 3; e[1] = 7; e[6] = 8; e[11] = 2;
    classes::Layout sl{};
    sl.lo = 0; sl.hi = sizeof sds; sl.spells = 0; sl.spell_count = 0x40;
    static classes::Tables st;
    CHECK(st.set(sl, sds, sizeof sds));
    combat::SpellLine out[16];
    // Slow Poison: the poisoned (dead) member gets back up with 1 hit point
    aff[1][0][0] = 0x37; aff[1][0][3] = 0xFF; naff[1] = 1;
    combat::damage(b, 1, 50);                                   // (off the field)
    rec[1][0x195] = party::Dead;
    CHECK(!b.f[1].up() && b.f[1].size == 0);
    rec[0][0x109] = 3;                                          // a 3rd level cleric
    int one = 1;
    const combat::FightSpell slow{0x1A, combat::SpellDoes::SlowPoison, 0, 0, 0, 0, 0, 1};
    int n = combat::cast(b, st, 0, 0x1A, slow, &one, 1, d, out, 16);
    CHECK(n == 2 && out[0].did == combat::Did::Word && out[1].did == combat::Did::Risen);
    CHECK(b.f[1].up() && b.f[1].hp() == 1 && b.f[1].size == 1 && b.f[1].has(0x16) && b.f[1].has(0x0F));
    // ... a hit point every 10 rounds (down to 1); run out while still poisoned: dies
    rec[1][0x1A4] = 5;
    combat::Event ev[4];
    for (int r = 0; r < 10; ++r) combat::tick(b, ev, 4);
    CHECK(b.f[1].hp() == 4 && b.f[1].has(0x0F));
    for (int k = 0; k < naff[1]; ++k)
        if (aff[1][k][0] == 0x16) { aff[1][k][1] = 2; aff[1][k][2] = 0; }
    combat::tick(b, ev, 4);
    n = combat::tick(b, ev, 4);
    CHECK(n == 1 && ev[0].ev == combat::Ev::DiesPoison && b.f[1].status() == party::Dead && !b.f[1].has(0x0F));
    // Animate Dead (by the monster): the dead member up on its side, animated
    rec[2][0x10E] = 0;
    int two = 2;
    const combat::FightSpell anim{0x24, combat::SpellDoes::Animate, 0, 0, 0, 0, 0, 1};
    rec[1][0xF7] = 0x00;
    n = combat::cast(b, st, 2, 0x24, anim, &two, 1, d, out, 16);
    CHECK(n == 2 && out[0].who == 1 && out[0].did == combat::Did::Risen && out[1].did == combat::Did::Word);
    CHECK(b.f[1].up() && b.f[1].team() == 1 && b.f[1].status() == party::Animated && b.f[1].hp() == 20 &&
          rec[1][0xF7] == 0xB3 && b.f[1].was_control == 0 && b.f[1].has(0x20) && aff[1][naff[1] - 1][3] >> 4 == 0);
    // Outside fights: the poison clock
    static party::Character ch;
    ch = party::Character{};
    rules::CureFacts cf{};
    cf.poisoned = 0x37; cf.slow_poison = 0x16; cf.poison_damage = 0x0F;
    ch.rec[0x1A4] = 10; ch.rec[0x195] = party::Okay;
    ch.affects[0][0] = 0x37; ch.affects[0][3] = 0xFF;
    ch.affects[1][0] = 0x16; ch.affects[1][1] = 120; ch.affects[1][3] = 0xFF;
    ch.affects[2][0] = 0x0F; ch.affects[2][1] = 10; ch.affects[2][3] = 0xFF;
    ch.n_affects = 3;
    CHECK(!rules::poison_clock(ch, 35, cf) && ch.rec[0x1A4] == 7);
    magic::tick_affects(ch, 35);
    CHECK(ch.n_affects == 3 && ch.affects[2][1] == 5 && ch.affects[1][1] == 85);
    CHECK(!rules::poison_clock(ch, 1, cf) && ch.rec[0x1A4] == 7);
    magic::tick_affects(ch, 1);
    CHECK(ch.affects[2][1] == 4);
    CHECK(!rules::poison_clock(ch, 4, cf) && ch.rec[0x1A4] == 6);
    magic::tick_affects(ch, 4);
    CHECK(ch.n_affects == 3 && ch.affects[2][1] == 10);
    CHECK(rules::poison_clock(ch, 200, cf) && ch.rec[0x195] == party::Dead && ch.n_affects == 1);
    // Constitution: the Girdle of the Dwarves rebuilds the hit points; 20+ heals
    {
        static party::Character gc;
        gc = party::Character{};
        rules::ItemFacts gf{};
        gf.con_regen_fx = 0x3E;
        for (int k = 0; k < 8; ++k) gf.max_hd[k] = 9;
        gc.rec[0x109 + 2] = 5;                               // a 5th level fighter
        gc.rec[0x195] = party::Okay;
        gc.rec[0x18] = gc.rec[0x19] = 18;
        gc.rec[0x12C] = 40;
        gc.rec[0x78] = 60;                                   // 40 + 5 x 4
        gc.rec[0x1A4] = 50;
        gc.items[0][0x3E] = 0x86;                            // code 6: Con +1, Cha -1
        gc.items[0][0x34] = 1;
        gc.n_items = 1;
        rules::stats(gc, gf);
        CHECK(gc.rec[0x19] == 19 && gc.rec[0x78] == 65 && gc.rec[0x1A4] == 55 && gc.n_affects == 0);
        gc.items[0][0x34] = 0;
        rules::stats(gc, gf);
        CHECK(gc.rec[0x19] == 18 && gc.rec[0x78] == 60 && gc.rec[0x1A4] == 50);
        gc.rec[0x1A4] = 3;
        gc.rec[0x18] = gc.rec[0x19] = 19;
        gc.rec[0x78] = 65;
        gc.items[0][0x34] = 1;
        rules::stats(gc, gf);                                // 20: the healing effect, 60 at a time
        CHECK(gc.rec[0x19] == 20 && gc.rec[0x78] == 65 && gc.rec[0x1A4] == 3 && gc.n_affects == 1 &&
              gc.affects[0][0] == 0x3E && gc.affects[0][1] == 60);
        CHECK(rules::con_regen(gc, 130, gf) == 2 && gc.rec[0x1A4] == 5);
        magic::tick_affects(gc, 130);
        CHECK(gc.n_affects == 1 && gc.affects[0][1] == 50);
        CHECK(rules::con_regen(gc, 49, gf) == 0);
        magic::tick_affects(gc, 49);
        CHECK(gc.affects[0][1] == 1);
        gc.items[0][0x34] = 0;
        rules::stats(gc, gf);
        CHECK(gc.rec[0x19] == 19 && gc.n_affects == 0);
        // a ranger who hasn't changed class: a level more; a magic-user 16+: 2 a level
        gc = party::Character{};
        gc.rec[0x109 + 4] = 3;
        gc.rec[0x109 + 5] = 3;
        gc.rec[0x18] = gc.rec[0x19] = 16;
        gc.rec[0x12C] = 20;
        gc.rec[0x78] = 20;
        gc.rec[0x1A4] = 20;
        gc.items[0][0x3E] = 0x86;
        gc.items[0][0x34] = 1;
        gc.n_items = 1;
        rules::stats(gc, gf);                                // Con 17: ranger 4 x 3 = 12, magic-user 3 x 2 = 6 -> 18 / 2
        CHECK(gc.rec[0x19] == 17 && gc.rec[0x78] == 29 && gc.rec[0x1A4] == 29);
    }
    // Spiritual Hammer: in hand while the effect lasts
    static party::Character hc;
    hc = party::Character{};
    static items::Names nm;
    rules::ItemFacts itf{};
    itf.hammer_fx = 0x17; itf.hammer_type = 20; itf.hammer_word = 20; itf.hammer_word2 = 243;
    CHECK(!rules::keep_hammer(hc, nm, itf) && hc.n_items == 0);
    hc.affects[0][0] = 0x17; hc.n_affects = 1;
    CHECK(rules::keep_hammer(hc, nm, itf) && hc.n_items == 1 && hc.items[0][0x2E] == 20 && hc.items[0][0x34] == 1 &&
          hc.items[0][0x31] == 243 && hc.items[0][0x32] == 1);
    CHECK(!rules::keep_hammer(hc, nm, itf) && hc.n_items == 1);
    hc.n_affects = 0;
    CHECK(!rules::keep_hammer(hc, nm, itf) && hc.n_items == 0);
    // Restoration: the lost hit points shared by the lost levels back
    static party::Character rc;
    rc = party::Character{};
    rc.rec[0xE7] = 2; rc.rec[0xE8] = 10; rc.rec[0x78] = 20; rc.rec[0x1A4] = 15; rc.rec[0x12C] = 20;
    CHECK(create::restore(rc, st) && rc.rec[0xE7] == 1 && rc.rec[0xE8] == 5 && rc.rec[0x78] == 25 && rc.rec[0x1A4] == 20);
    rc.rec[0xE7] = 0;
    CHECK(!create::restore(rc, st));
    // Detect Magic: "* " before magic and cursed items' names
    uint8_t it[items::kRecordSize] = {};
    char nmout[48];
    nm.detect = true;
    it[0x32] = 1;
    nm.name(items::Item{it}, nmout, sizeof nmout);
    CHECK(nmout[0] == '*');                                      // (no words here: the space trimmed)
    it[0x32] = 0;
    it[0x33] = 0xFF;                                            // (-1: no star - the original tests it signed)
    nm.name(items::Item{it}, nmout, sizeof nmout);
    CHECK(nmout[0] != '*');
    it[0x36] = 1;
    nm.name(items::Item{it}, nmout, sizeof nmout);
    CHECK(nmout[0] == '*');                                      // (no words here: the space trimmed)
    nm.detect = false;
    // A Ring of Wizardry readied: magic-user levels 1-3 doubled
    rc.rec[0x137] = 2; rc.rec[0x138] = 1; rc.rec[0x139] = 0; rc.rec[0x13A] = 1;
    classes::wizardry(rc, st, true);
    CHECK(rc.rec[0x137] == 4 && rc.rec[0x138] == 2 && rc.rec[0x139] == 0 && rc.rec[0x13A] == 1);
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
    test_vm_and();
    test_ecl_party();
    test_treasure();
    test_sound();
    test_journal();
    test_geo_view();
    test_party();
    test_items();
    test_item_piles();
    test_temple();
    test_magic();
    test_spells();
    test_combat();
    test_monster_fx();
    test_spells_batch2();
    test_spells_batch3();
    test_create();
    test_item_combat();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_dax: all checks passed\n");
    return 0;
}

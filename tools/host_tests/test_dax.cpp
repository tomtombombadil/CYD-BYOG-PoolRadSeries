// Host tests for the engine's file-format code (no game files needed: the
// tests build their own DAX files).
// CI builds it with g++ -std=c++17 -Wall -Werror -fsanitize=address,undefined
// -Isrc tools/host_tests/test_dax.cpp src/engine/*.cpp (see build.yml).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "engine/dax.h"
#include "engine/exepack.h"
#include "engine/layout.h"
#include "engine/printcalls.h"
#include "engine/text.h"
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
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_dax: all checks passed\n");
    return 0;
}

// Host tests for the engine's file-format code (no game files needed: the
// tests build their own DAX files).
// CI builds it with g++ -std=c++17 -Wall -Werror -fsanitize=address,undefined
// -Isrc tools/host_tests/test_dax.cpp src/engine/*.cpp (see build.yml).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "engine/dax.h"
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
}

int main()
{
    test_rle_known_bytes();
    test_round_trip();
    test_bad_files();
    test_picture();
    test_anim();
    test_games();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("test_dax: all checks passed\n");
    return 0;
}

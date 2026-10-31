// Damaged files for the decoders that read the player's own files (the
// journal PDFs, the Launcher's box art JPEGs): synthetic bytes made here,
// nothing from the games. Each must be refused (or read) without reading or
// writing outside its buffers and without endless recursion. CI builds it with
//   g++ -std=c++17 -O1 -Wall -Werror -fsanitize=address,undefined -Isrc
//       -o test_decoders tools/host_tests/test_decoders.cpp src/engine/*.cpp
#include "engine/dax.h"
#include "engine/jpeg_thumb.h"
#include "engine/pdf.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

using Bytes = std::vector<uint8_t>;

// ---- PDF --------------------------------------------------------------------

// zlib with one stored DEFLATE block (no compression needed)
static std::string zstored(const std::string& d)
{
    std::string z = "\x78\x01";
    z += '\x01';
    const uint16_t n = static_cast<uint16_t>(d.size());
    z += static_cast<char>(n & 255);
    z += static_cast<char>(n >> 8);
    z += static_cast<char>(~n & 255);
    z += static_cast<char>((~n >> 8) & 255);
    return z + d + std::string("\0\0\0\0", 4);
}

// A cross-reference stream row: type (1 byte), field 2 (2 bytes), field 3 (1 byte)
static std::string xrow(int type, uint32_t f2, int f3)
{
    std::string r;
    r += static_cast<char>(type);
    r += static_cast<char>((f2 >> 8) & 255);
    r += static_cast<char>(f2 & 255);
    r += static_cast<char>(f3);
    return r;
}

// A PDF 1.5 file: `objects` written as they are (offsets noted), then a
// cross-reference stream (object `xnum`) with these rows and /Index
static std::string xref_pdf(std::string body, std::vector<size_t>& off, int xnum,
                            const std::vector<std::string>& rows_of, const std::string& index, int size)
{
    std::string rows;
    for (const std::string& r : rows_of) rows += r;
    const std::string zs = zstored(rows);
    const size_t xat = body.size();
    off[static_cast<size_t>(xnum)] = xat;
    body += std::to_string(xnum) + " 0 obj\n<</Type/XRef/Size " + std::to_string(size) + "/W[1 2 1]" + index +
            "/Root 1 0 R/Filter/FlateDecode/Length " + std::to_string(zs.size()) + ">>stream\n" + zs +
            "\nendstream\nendobj\n";
    body += "startxref\n" + std::to_string(xat) + "\n%%EOF\n";
    return body;
}

static void test_pdf_negative_index()
{
    // /Index [-2 2]: rows for objects -2 and -1 - refused (they
    // were written before doc.offset's start)
    std::string s = "%PDF-1.5\n";
    std::vector<size_t> off(8, 0);
    off[1] = s.size();
    s += "1 0 obj\n<</Type/Catalog/Pages 1 0 R>>\nendobj\n";
    s = xref_pdf(s, off, 3, {xrow(1, 9, 0), xrow(1, 9, 0)}, "/Index[-2 2]", 4);
    Bytes b(s.begin(), s.end());
    dax::MemorySource src(b.data(), b.size());
    static pdf::Doc doc;
    doc = pdf::Doc{};
    CHECK(!pdf::open(src, doc));
    // a negative count too
    std::string t = "%PDF-1.5\n";
    std::vector<size_t> o2(8, 0);
    t = xref_pdf(t, o2, 3, {xrow(1, 9, 0)}, "/Index[0 -5]", 4);
    Bytes b2(t.begin(), t.end());
    dax::MemorySource s2(b2.data(), b2.size());
    doc = pdf::Doc{};
    CHECK(!pdf::open(s2, doc));
    // a negative /Size (no /Index)
    std::string u = "%PDF-1.5\n";
    std::vector<size_t> o3(8, 0);
    u = xref_pdf(u, o3, 3, {xrow(1, 9, 0)}, "", -7);
    Bytes b3(u.begin(), u.end());
    dax::MemorySource s3(b3.data(), b3.size());
    doc = pdf::Doc{};
    CHECK(!pdf::open(s3, doc));
}

static void test_pdf_object_stream_loop()
{
    // Object stream 5 holds objects 1 (the catalog) and 2; its /Length is
    // "2 0 R" - object 2, inside object stream 5 itself: loading object 1
    // loads 5, whose length loads 2, which loads 5 ... - it must stop
    std::string s = "%PDF-1.5\n";
    std::vector<size_t> off(8, 0);
    const std::string o1 = "<</Type/Catalog/Pages 3 0 R>>", o2 = "42";
    const std::string head = "1 0 2 " + std::to_string(o1.size() + 1) + " ";
    const std::string zs = zstored(head + o1 + "\n" + o2);
    off[5] = s.size();
    s += "5 0 obj\n<</Type/ObjStm/N 2/First " + std::to_string(head.size()) +
         "/Filter/FlateDecode/Length 2 0 R>>stream\n" + zs + "\nendstream\nendobj\n";
    // rows for objects 0..6: 1 and 2 in stream 5, 5 at its offset, 6 the xref stream
    std::vector<std::string> rows = {xrow(0, 0, 0), xrow(2, 5, 0), xrow(2, 5, 1), xrow(0, 0, 0), xrow(0, 0, 0),
                                     xrow(1, static_cast<uint32_t>(off[5]), 0)};
    const size_t xat = s.size();
    rows.push_back(xrow(1, static_cast<uint32_t>(xat), 0));
    s = xref_pdf(s, off, 6, rows, "", 7);
    Bytes b(s.begin(), s.end());
    dax::MemorySource src(b.data(), b.size());
    static pdf::Doc doc;
    doc = pdf::Doc{};
    CHECK(pdf::open(src, doc) && doc.root == 1 && (doc.offset[1] & pdf::kInStream) && (doc.offset[2] & pdf::kInStream));
    int pages[4];
    CHECK(pdf::pages(src, doc, pages, 4) == 0);
    pdf::Image im;
    CHECK(!pdf::page_image(src, doc, 1, im));
    CHECK(!pdf::page_image(src, doc, 2, im));
}

// ---- JPEG thumbnails ---------------------------------------------------------

static void seg(Bytes& b, uint8_t marker, const Bytes& body)
{
    b.push_back(0xFF);
    b.push_back(marker);
    const size_t len = body.size() + 2;
    b.push_back(static_cast<uint8_t>(len >> 8));
    b.push_back(static_cast<uint8_t>(len & 255));
    b.insert(b.end(), body.begin(), body.end());
}

static Bytes sof(int w, int h)
{
    return {8, static_cast<uint8_t>(h >> 8), static_cast<uint8_t>(h), static_cast<uint8_t>(w >> 8), static_cast<uint8_t>(w),
            1, 1, 0x11, 0};
}

static bool thumb_probe(const Bytes& b)
{
    dax::MemorySource src(b.data(), b.size());
    jthumb::Info info;
    return jthumb::probe(src, info);
}

static bool thumb_decode(const Bytes& b)
{
    dax::MemorySource src(b.data(), b.size());
    return jthumb::decode(src, [](int, const uint8_t*, int, void*) { return true; }, nullptr);
}

static void test_thumb_damaged()
{
    // A quantisation segment of 1024 bytes whose last table header (at
    // 910, 16-bit) says 128 bytes follow - only 113 do: refused, nothing
    // read past the segment's buffer
    {
        Bytes b = {0xFF, 0xD8};
        Bytes dqt;
        for (int t = 0; t < 14; ++t) {
            dqt.push_back(static_cast<uint8_t>(t & 3));
            dqt.insert(dqt.end(), 64, 1);
        }
        dqt.push_back(0x10);
        dqt.resize(1024, 1);
        seg(b, 0xDB, dqt);
        seg(b, 0xC0, sof(8, 8));
        b.push_back(0xFF);
        b.push_back(0xD9);
        CHECK(!thumb_decode(b));
    }
    // A Huffman table whose counts add up to more than 256 values
    {
        Bytes b = {0xFF, 0xD8};
        seg(b, 0xC0, sof(8, 8));
        Bytes dht = {0x00};
        for (int i = 0; i < 16; ++i) dht.push_back(i < 2 ? 150 : 0);
        dht.resize(dht.size() + 300, 0);
        seg(b, 0xC4, dht);
        b.push_back(0xFF);
        b.push_back(0xD9);
        CHECK(!thumb_decode(b));
    }
    // Segments too short for what they say: a frame of 3 components in 6
    // bytes, a scan header of 1 byte, a restart interval of none
    {
        Bytes b = {0xFF, 0xD8};
        seg(b, 0xC0, {8, 0, 8, 0, 8, 3});
        CHECK(!thumb_probe(b));
        Bytes c = {0xFF, 0xD8};
        seg(c, 0xC0, sof(8, 8));
        seg(c, 0xDA, {1});
        CHECK(!thumb_decode(c));
        Bytes d = {0xFF, 0xD8};
        seg(d, 0xDD, {});
        seg(d, 0xC0, sof(8, 8));
        CHECK(!thumb_decode(d));
    }
}

int main(int argc, char** argv)
{
    // (one group by name, else all)
    const char* only = argc > 1 ? argv[1] : "";
    if (!*only || !strcmp(only, "index")) test_pdf_negative_index();
    if (!*only || !strcmp(only, "loop")) test_pdf_object_stream_loop();
    if (!*only || !strcmp(only, "thumb")) test_thumb_damaged();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all decoder checks passed\n");
    return 0;
}

// The platform layer's PC side (tools/host): the files the game code reads
// and writes, as the board's card gives them. CI builds it with
//   g++ -std=c++17 -Wall -Werror -fsanitize=address,undefined -Itools/host -Isrc
//       -o test_platform tools/host_tests/test_platform.cpp tools/host/*.cpp src/engine/dax.cpp
#include "host.h"
#include "platform/file.h"
#include "platform/memlog.h"
#include "platform/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

uint32_t g_now = 1000;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

int main()
{
    char tmpl[] = "/tmp/test_platform_XXXXXX";
    const char* root = mkdtemp(tmpl);
    if (!root) return 1;
    pc::card_root() = root;
    CHECK(plat::card_ready());
    CHECK(pc::host_path("/GOLDBOX/A/B.DAX") == std::string(root) + "/A/B.DAX");
    CHECK(pc::host_path("/OTHER") == "/OTHER");
    // The clock is the driver's
    g_now = 1234;
    CHECK(plat::now_ms() == 1234);
    // A folder, a file written, read back, its size, seeking
    CHECK(!plat::exists("/GOLDBOX/GAME"));
    CHECK(plat::mkdir("/GOLDBOX/GAME") && plat::exists("/GOLDBOX/GAME"));
    std::vector<uint8_t> data(1500);
    for (size_t i = 0; i < data.size(); ++i) data[i] = static_cast<uint8_t>(i * 7 + 3);
    {
        plat::File w = plat::open("/GOLDBOX/GAME/F.BIN", "w");
        CHECK(static_cast<bool>(w) && !w.isDirectory());
        CHECK(w.write(data.data(), 1000) == 1000);
        w.close();
        CHECK(!w);
        plat::File a = plat::open("/GOLDBOX/GAME/F.BIN", "a");
        CHECK(a.write(data.data() + 1000, 500) == 500);
    }
    CHECK(pc::open_files() == 0);                    // (the last copy gone: closed)
    plat::File r = plat::open("/GOLDBOX/GAME/F.BIN");
    CHECK(static_cast<bool>(r) && r.size() == 1500);
    uint8_t b[16];
    CHECK(r.seek(1495) && r.read(b, 16) == 5 && b[0] == data[1495] && b[4] == data[1499]);
    // A DAX source over it: reads across its 512-byte buffer, past the end
    {
        plat::FileSource src(r);
        CHECK(src.size() == 1500);
        std::vector<uint8_t> got(1500);
        CHECK(src.read_at(0, got.data(), 1500) == 1500 && got == data);
        CHECK(src.read_at(1400, got.data(), 300) == 100 && got[0] == data[1400] && got[99] == data[1499]);
        CHECK(src.read_at(500, got.data(), 30) == 30 && memcmp(got.data(), data.data() + 500, 30) == 0);
        CHECK(src.read_at(1500, got.data(), 4) == 0);
    }
    // Copies share the file; close() closes it for all of them (as Arduino's)
    {
        plat::File copy = r;
        CHECK(pc::open_files() == 1);
        r.close();
        CHECK(!r && !copy);
        CHECK(pc::open_files() == 0);
    }
    // The card's 3 file slots: a fourth open fails
    {
        plat::File f1 = plat::open("/GOLDBOX/GAME/F.BIN");
        plat::File f2 = plat::open("/GOLDBOX/GAME/F.BIN");
        plat::File f3 = plat::open("/GOLDBOX/GAME/F.BIN");
        CHECK(f1 && f2 && f3 && pc::open_files() == 3);
        plat::File f4 = plat::open("/GOLDBOX/GAME/F.BIN");
        CHECK(!f4);
        f2.close();
        plat::File f5 = plat::open("/GOLDBOX/GAME/F.BIN");
        CHECK(static_cast<bool>(f5));
    }
    CHECK(pc::open_files() == 0);
    // A folder's entries, each with its own name; missing files
    {
        plat::File w = plat::open("/GOLDBOX/GAME/G.TXT", "w");
        CHECK(w.write(reinterpret_cast<const uint8_t*>("hi"), 2) == 2);
    }
    CHECK(plat::mkdir("/GOLDBOX/GAME/SUB"));
    plat::File dir = plat::open("/GOLDBOX/GAME");
    CHECK(dir.isDirectory());
    int files = 0, dirs = 0;
    bool seen_f = false, seen_g = false, seen_sub = false;
    for (plat::File e = dir.openNextFile(); e; e = dir.openNextFile()) {
        if (e.isDirectory()) ++dirs;
        else ++files;
        seen_f |= strcmp(e.name(), "F.BIN") == 0;
        seen_g |= strcmp(e.name(), "G.TXT") == 0 && e.size() == 2;
        seen_sub |= strcmp(e.name(), "SUB") == 0 && e.isDirectory();
    }
    CHECK(files == 2 && dirs == 1 && seen_f && seen_g && seen_sub);
    dir.close();
    CHECK(!plat::open("/GOLDBOX/GAME/NONE.BIN"));
    CHECK(plat::remove("/GOLDBOX/GAME/G.TXT") && !plat::exists("/GOLDBOX/GAME/G.TXT"));
    // A default File does nothing
    plat::File none;
    CHECK(!none && none.size() == 0 && none.read(b, 4) == 0 && !none.isDirectory() && !none.openNextFile());
    // The memory figures and the memory log: unknown / plenty on a PC
    CHECK(plat::free_heap() == 0 && plat::largest_block() == 0);
    CHECK(memlog::largest_kb() >= 999 || getenv("LOWMEM"));
    // (tidy up)
    plat::remove("/GOLDBOX/GAME/F.BIN");
    rmdir((std::string(root) + "/GAME/SUB").c_str());
    rmdir((std::string(root) + "/GAME").c_str());
    rmdir(root);
    if (failures) {
        printf("test_platform: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_platform: all checks passed\n");
    return 0;
}

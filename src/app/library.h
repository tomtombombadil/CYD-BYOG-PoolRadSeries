// The player's games on the SD card: /GOLDBOX/<folder>/ with the original
// DOS files in each folder. Also a ByteSource over an SD file so the engine's
// DAX code can read it.
#pragma once

#include <FS.h>
#include <cstdint>

#include "engine/dax.h"
#include "engine/games.h"

namespace library {

// How a game keeps its files.
//   Dax:  .DAX archives (the Pool of Radiance series, Champions and Death
//         Knights of Krynn, the Savage Frontier games) - readable.
//   Hlib: "HLIB" .TLB / .GLB libraries in DISK1-3 folders (The Dark Queen
//         of Krynn, Unlimited Adventures) - recognised, not readable yet.
enum class Format : uint8_t { Dax, Hlib };

struct GameDir {
    char         folder[40];      // folder name under /GOLDBOX
    char         data_dir[96];    // where the DAX files are, relative to /GOLDBOX:
                                  // the folder itself, or one folder inside it
                                  // (a whole GOG install copied as it is)
    games::Game  game;
    Format       format;
    int          dax_files;       // .DAX files (Dax), .TLB + .GLB files (Hlib)
    char         icon[128];       // the game's icon file, relative to /GOLDBOX ("" = none):
                                  // GOG's goggame-<id>.ico / .dll, or another .ico
                                  // (not GOG's generic Support.ico)
};

constexpr int kMaxGames = 16;
constexpr int kMaxFiles = 96;
constexpr int kNameLen  = 32;

enum class ScanResult : uint8_t { Ok, NoCard, NoRootFolder };

// Lists the game folders, sorted by games::list_order() then name: those
// holding .DAX files (in the folder or one folder down), and those holding
// .TLB / .GLB files (in the folder or up to two folders down - Dark Queen
// keeps them in DISK1-3). *n = how many.
ScanResult scan(GameDir* out, int max, int* n);

// The .DAX files in a game's data_dir, sorted by name.
int list_dax(const char* data_dir, char (*names)[kNameLen], int max);

// "/GOLDBOX/<data_dir>/<file>"
void path_of(const char* data_dir, const char* file, char* out, size_t cap);

// A DAX source over an open SD file. Keeps a 512-byte read-ahead buffer.
class FileSource : public dax::ByteSource {
public:
    explicit FileSource(fs::File f);
    size_t read_at(uint32_t pos, uint8_t* buf, size_t n) override;
    uint32_t size() const override { return size_; }
private:
    fs::File f_;
    uint32_t size_;
    uint8_t  cache_[512];
    uint32_t cache_pos_ = 0;
    uint32_t cache_len_ = 0;
};

} // namespace library

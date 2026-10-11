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
                                  // the folder itself, or one or two folders inside
                                  // it (a whole GOG install copied as it is; the
                                  // Steam / SNEG releases: <folder>/GAME/<SHORT>)
    games::Game  game;
    Format       format;
    int          dax_files;       // .DAX files (Dax), .TLB + .GLB files (Hlib)
    char         icon[128];       // the game's icon file, relative to /GOLDBOX ("" = none):
                                  // GOG's goggame-<id>.ico / .dll, or another .ico
                                  // (not GOG's generic Support.ico)
    char         journal[128];    // the journal PDF, relative to /GOLDBOX ("" = none)
    uint32_t     journal_size;
    char         gbc[48];         // the Steam / SNEG releases' Gold Box Companion: its folder
                                  // for this game in <folder>/GBC/Games/ ("02. Curse of the
                                  // Azure Bonds"; "" = none) - Game.dat has the journal's text
};

constexpr int kMaxGames = 16;
constexpr int kMaxFiles = 96;
constexpr int kNameLen  = 32;

enum class ScanResult : uint8_t { Ok, NoCard, NoRootFolder };

// Lists the game folders, sorted by games::list_order() then name: those
// holding .DAX files (in the folder or up to two folders down), and those
// holding .TLB / .GLB files (in the folder or up to three folders down - Dark
// Queen keeps them in DISK1-3, the SNEG release in GAME/DQK/DISK1-3). The
// journal PDF: in the folder, the data folder or Documentation/. *n = how many.
// Finds the games on the card. progress(line, replace_last, ctx), if
// given, hears what it finds as it goes ("Found Curse of the Azure Bonds").
using Progress = void (*)(const char* line, bool replace_last, void* ctx);
ScanResult scan(GameDir* out, int max, int* n, Progress progress = nullptr, void* ctx = nullptr);

// Everything the board makes from the player's files lives in
// /GOLDBOX/_CYD/ (Tom, 2026-10-09): the scan log (SCAN.TXT, readable on a
// PC), the library (LIBRARY.BIN, so boot needn't scan again - only Rescan
// Card scans), and per game a folder named like the game's own (icons at
// the screen's size, journal entries).
constexpr const char* kCacheDir = "_CYD";
void cache_path(const char* file, char* out, size_t cap);                     // /GOLDBOX/_CYD/<file>
void cache_path(const GameDir& g, const char* file, char* out, size_t cap);   // /GOLDBOX/_CYD/<folder>/<file>
bool make_cache_dirs(const GameDir& g);

bool save_library(const GameDir* games, int n);
// False when there is no saved library (or it is from an older firmware's
// format): scan then.
bool load_library(GameDir* out, int max, int* n);

// The .DAX files in a game's data_dir, sorted by name.
int list_dax(const char* data_dir, char (*names)[kNameLen], int max);

// "/GOLDBOX/<data_dir>/<file>"
void path_of(const char* data_dir, const char* file, char* out, size_t cap);
// "/GOLDBOX/<folder>/GBC/Games/<gbc>/<file>" (false: no Gold Box Companion)
bool gbc_path(const GameDir& g, const char* file, char* out, size_t cap);

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

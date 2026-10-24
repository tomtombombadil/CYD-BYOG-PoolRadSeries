// Saved games (Curse: SAVGAMA.DAT ... SAVGAMJ.DAT in the game's save
// folder, with the party's characters as CHRDATA1.SAV ... beside them).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Layout (learned from coab, checked on the GOG release's SAVE/SAVGAMA.DAT,
// 13,149 bytes; own code):
//   u8      the game area (ECL<n> ...)
//   0x800   area data (script memory 0x4B00-0x4EFF, a word each)
//   0x800   game data (0x7C00-0x7FFF)
//   0x400   the table (0x7A00-0x7BFF)
//   0x1E00  the running script's bytes (0x8000-)
//   5       party x, y, facing (0 / 2 / 4 / 6), wall type ahead, roof flags
//   u8, u8  the last game state, the game state
//   3 x (s16 WALLDEF block, s16 set)   the wall sets loaded
//   u8      characters in the party
//   8 x 41  their file names (Pascal strings, e.g. "CHRDATA1")
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"
#include "ecl_vm.h"
#include "party.h"

namespace savegame {

constexpr uint32_t kSize = 1 + 0x800 + 0x800 + 0x400 + 0x1E00 + 5 + 2 + 12 + 1 + 8 * 41;   // 13,149
// Pool of Radiance's saves (poolrad_facts.md 3.2) are the same without the
// wall sets (its scripts load them again): 13,137 bytes
constexpr uint32_t kPoolSize = kSize - 12;
// Secret of the Silver Blades' (secret_facts.md 7) are Curse's without the
// script's bytes: 5,469 bytes
constexpr uint32_t kSecretSize = kSize - 0x1E00;
// Pools of Darkness's (darkness_facts.md 32, S8): SAVGAMx.PTY - the scripts'
// 1,024-byte game block, 5 position bytes (x, y, facing 0-7, wall ahead,
// square flags), the old and current mode, the look (u16) and wall-set
// slot (u16), the count and 8 x 41 names: 1,364 bytes
constexpr uint32_t kDarknessBlock = 0x400;
constexpr uint32_t kDarknessSize = kDarknessBlock + 5 + 2 + 2 + 2 + 1 + 8 * 41;
// Death Knights of Krynn's (deathknights_facts.md 2; its save overlay GAME.OVR
// 0x144E3): SAVGAMx.PTY - Curse's .DAT with its memory as bytes (0x400 +
// 0x400 + 0x200), no script, four wall sets: 2,913 bytes
constexpr uint32_t kDkkSize = 1 + 0x400 + 0x400 + 0x200 + 5 + 2 + 16 + 1 + 8 * 41;
constexpr char kFirst = 'A', kLast = 'J';    // the games' ten save slots

struct Header {
    uint8_t game_area = 0;
    uint8_t last_state = 0, state = 0;   // (Darkness: its modes - 4 3D, 3 the overland)
    uint16_t look = 0, look_slot = 0;    // (Darkness: LOAD FILES' look, its wall-set slot)
    int16_t wall_block[3] = {}, wall_set[3] = {};
    int16_t wall_block4 = 0, wall_set4 = 0;  // (Death Knights: a fourth)
    int     count = 0;                       // characters
    char    names[party::kMaxParty][41] = {};
};

// Reads a saved game into gs (memory, script, position) and h. False if
// the file isn't one.
// wall_sets = false: Pool's layout (no wall sets; h's stay 0); script =
// false: Secret's (no script bytes; gs.code_len 0).
bool read(dax::ByteSource& src, ecl::GameState& gs, Header& h, bool wall_sets = true, bool script = true);

// Darkness's .PTY: the game block into gs.area1's first 1,024 bytes (the
// scripts' addresses 1-0x400), the position, modes, look and names
bool read_darkness(dax::ByteSource& src, ecl::GameState& gs, Header& h);

// Where write() puts the bytes (a file on the card, a buffer in tests)
class Sink {
public:
    virtual ~Sink() = default;
    virtual bool put(const uint8_t* p, size_t n) = 0;
};

// Writes a saved game: gs (memory, script, position), h (game area,
// states, wall sets, the party's file names). The characters' own files
// are written by the caller (their records as kept, items, effects).
bool write(Sink& out, const ecl::GameState& gs, const Header& h, bool wall_sets = true, bool script = true);
bool write_darkness(Sink& out, const ecl::GameState& gs, const Header& h);
// Death Knights' .PTY: the three blocks into gs.area1 / area2 / table's first
// 0x400 / 0x400 / 0x200 bytes (a byte an address), the position, modes, the
// four wall sets and the names
bool read_dkk(dax::ByteSource& src, ecl::GameState& gs, Header& h);
bool write_dkk(Sink& out, const ecl::GameState& gs, const Header& h);

// The save file's name for slot 'A'..'J' ("SAVGAMA.DAT"), and the n-th
// character's (1 ..: "CHRDATA1", + ".SAV" / ".SWG" / ".FX").
void file_name(char slot, char* out, size_t cap);
void file_name_darkness(char slot, char* out, size_t cap);      // "SAVGAMA.PTY"
void char_file(char slot, int n, char* out, size_t cap);

// The save folder from the game's configuration file (Curse: CURSE.CFG,
// lines "E", "P", "C:\SAVE\", "F"): the last line naming a folder (Pool's
// POOL.CFG names the game's folder first, then the save folder), without
// its drive and with '/' between folders ("SAVE"); "" if none. The games run
// with their own folder as drive C:, so it is a folder inside it.
// nth >= 0: the nth folder line instead (Darkness's POOL4.CFG names its own
// save folder first, then Secret's: nth 0)
void save_dir(const char* cfg, size_t len, char* out, size_t cap, int nth = -1);
// The Steam / SNEG releases run with the folder ABOVE the game's as C:
// (CURSE.CFG "C:\CURSE\SAVE\", the files in GAME/CURSE): when the save
// folder's first part is the game folder's own name (the data folder's last
// part, any case) and more follows, drops it ("CURSE/SAVE" -> "SAVE"); the
// game folder's own name alone (GOG Pool's POOL.CFG "C:\POOLRAD\": the
// saves are beside the game's files) becomes "". True if it did.
bool drop_own_folder(const char* data_dir, char* sub);

} // namespace savegame

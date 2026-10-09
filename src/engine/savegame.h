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
constexpr char kFirst = 'A', kLast = 'J';    // the games' ten save slots

struct Header {
    uint8_t game_area = 0;
    uint8_t last_state = 0, state = 0;
    int16_t wall_block[3] = {}, wall_set[3] = {};
    int     count = 0;                       // characters
    char    names[party::kMaxParty][41] = {};
};

// Reads a saved game into gs (memory, script, position) and h. False if
// the file isn't one.
bool read(dax::ByteSource& src, ecl::GameState& gs, Header& h);

// Where write() puts the bytes (a file on the card, a buffer in tests)
class Sink {
public:
    virtual ~Sink() = default;
    virtual bool put(const uint8_t* p, size_t n) = 0;
};

// Writes a saved game: gs (memory, script, position), h (game area,
// states, wall sets, the party's file names). The characters' own files
// are written by the caller (their records as kept, items, effects).
bool write(Sink& out, const ecl::GameState& gs, const Header& h);

// The save file's name for slot 'A'..'J' ("SAVGAMA.DAT"), and the n-th
// character's (1 ..: "CHRDATA1", + ".SAV" / ".SWG" / ".FX").
void file_name(char slot, char* out, size_t cap);
void char_file(char slot, int n, char* out, size_t cap);

// The save folder from the game's configuration file (Curse: CURSE.CFG,
// lines "E", "P", "C:\SAVE\", "F"): the line naming a folder, without its
// drive and with '/' between folders ("SAVE"); "" if none. The games run
// with their own folder as drive C:, so it is a folder inside it.
void save_dir(const char* cfg, size_t len, char* out, size_t cap);

} // namespace savegame

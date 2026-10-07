// What the engine knows about each game's program (START.EXE / GAME.EXE):
// which release it is and where in the unpacked program the few tables the
// engine reads from it sit. Only sizes and addresses live here - the
// tables themselves are always read from the player's own copy (bring your
// own game).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Addresses are offsets in the program's data segment (DS); data_base is
// where DS:0 lies in the unpacked program. A program is recognised by its
// file size and unpacked size together; other releases need their own
// entry (the engine says "a version this engine doesn't know yet").
#pragma once

#include <cstdint>

#include "ecl.h"
#include "games.h"

namespace profile {

// The screen frame's tile layout tables (see layout.h). Each is one byte a
// cell: the frame tile to draw there.
struct FrameTables {
    uint16_t top;            // row 0, 40 cells
    uint16_t bar;            // a full-width bar between areas, 40 cells
    uint16_t bottom;         // row 23, 40 cells
    uint16_t left;           // column 0, rows 0-23
    uint16_t right;          // column 39, rows 0-23
    uint16_t view_split;     // column 16, rows 0-16 (exploring)
    uint16_t view_top;       // the 3D view's own frame: row 2, cols 2-14 (indexed by column)
    uint16_t view_bottom;    //   row 14, cols 2-14
    uint16_t view_left;      //   col 2, rows 2-14 (indexed by row)
    uint16_t view_right;     //   col 14, rows 2-14
    uint16_t combat_left;    // combat: column 0, rows 0-22
    uint16_t combat_split;   //   column 22, rows 0-22
    uint16_t combat_right;   //   column 39, rows 0-22
};

// One step of the title sequence: a TITLE.DAX picture at a cell position,
// or (block 0) the credits screen; then a wait (a tap skips it).
struct TitleStep {
    uint8_t  block;          // 0 = credits
    uint8_t  row, col;       // where the picture's top left goes, in cells
    bool     clear;          // clear the screen first
    uint16_t wait_ms;        // 0 = go straight on
};

struct Profile {
    games::Game  game;
    const char*  release;        // shown to the player, e.g. "GOG"
    const char*  program;        // file name in the game folder
    uint32_t     program_size;   // bytes on disk
    uint32_t     image_size;     // unpacked bytes
    uint32_t     data_base;      // DS:0 in the unpacked program
    FrameTables  frame;
    const char*  tiles_file;     // the 8x8 tiles the frame is made of
    uint8_t      tiles_block;

    // Strings in the program: "Press any key to continue" (text windows)
    uint32_t     press_any_key;  // image offset of the Pascal string

    // The title sequence
    const char*      title_file;
    const TitleStep* title;
    int              title_steps;

    // GAME.OVR (the overlay file): the credits screen's print calls
    const char*  overlay;
    uint32_t     overlay_size;
    uint32_t     credits_at;     // first print call
    uint32_t     credits_base;   // its code segment's start in the file
    uint8_t      credits_bars[2];   // rows of the frame's bars on that screen

    // 3D areas: files GEOn / WALLDEFn / 8X8Dn / ECLn.DAX for areas
    // first_area .. last_area; the common wall tiles; the horizon picture
    uint8_t      first_area, last_area;
    uint8_t      common_tiles_block;   // in 8X8D1.DAX
    const char*  sky_file;
    uint8_t      horizon_block;
    const ecl::OpSet* ecl_ops;         // the script's opcodes (nullptr: unknown)

    // Scripts and the 3D view while playing
    uint16_t     sky_colours;          // DS offset of the 16 sky colours (area words
                                       // 0x4BFD / 0x4BFE pick one)
    uint8_t      start_area, start_script;   // a new game: ECL<area> block <script>
};

// The game's program file name (to look for it), or nullptr if no
// release of that game is known yet.
const char* program_name(games::Game g);

// The profile matching a program, or nullptr.
const Profile* find(games::Game g, uint32_t program_size, uint32_t image_size);

} // namespace profile

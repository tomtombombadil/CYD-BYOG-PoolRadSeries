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
};

// The game's program file name (to look for it), or nullptr if no
// release of that game is known yet.
const char* program_name(games::Game g);

// The profile matching a program, or nullptr.
const Profile* find(games::Game g, uint32_t program_size, uint32_t image_size);

} // namespace profile

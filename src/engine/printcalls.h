// Fixed screens the game prints from its own code (e.g. Curse's credits):
// read the string, its position and colours straight from the player's
// GAME.OVR, so none of it is copied into the engine.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// GAME.OVR is a Turbo Pascal overlay file ("FBOV"); a procedure's string
// constants sit in its code segment before the code. Such a screen is a
// run of identical 32-byte calls "print string S at row R, column C in
// colours F on B":
//   B0 cc 50   push column        B0 rr 50   push row
//   B0 ff 50   push colour        B0 bb 50   push background
//   8D 7E xx 16 57                the string's local copy
//   BF lo hi 0E 57                the constant: code segment offset hi:lo
//   9A + 4 bytes                  copy it
//   9A + 4 bytes                  print it
// The run ends at the first block that doesn't match. The profile gives
// where the run starts and where the code segment starts in the file.
#pragma once

#include <cstdint>

#include "dax.h"

namespace printcalls {

constexpr int kBlock = 32;

struct Line {
    uint8_t row, col, fg, bg;
    char    s[41];
};

// Reads the run of print calls at `at`. Returns how many lines (max at most),
// 0 if the first block doesn't match or a string can't be read.
int read(dax::ByteSource& ovr, uint32_t at, uint32_t seg_base, Line* out, int max);

} // namespace printcalls

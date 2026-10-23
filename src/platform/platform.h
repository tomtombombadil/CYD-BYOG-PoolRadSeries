// The platform layer (Tom, 2026-10-12: "this game engine be easily
// adaptable to other platforms, such as PortMaster"): everything the game
// code (src/engine/, src/game/) needs from the machine it runs on, and
// nothing more - the time, a log, the memory figures (here), the card's
// files (platform/file.h), sound out (platform/audio.h) and the memory log
// (platform/memlog.h).
//
// Implementations: src/hal/platform_esp32.cpp (the CYD boards: Arduino,
// the SD library, heap_caps) and tools/host/ (Linux: the playsim and the
// sweep tests run the game code on it). A port supplies its own - stdio
// files, SDL's clock and audio - plus a front end (screen, input).
#pragma once

#include <cstddef>
#include <cstdint>

namespace plat {

// Milliseconds since the start (wraps after ~49 days; compare differences)
uint32_t now_ms();

// A line (or part of one) for the developer's log: the board's serial
// port; stdout on a PC. printf's format.
void log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// The heap now, in bytes: free in all, and the largest single block a load
// can get (0: the platform can't tell)
size_t free_heap();
size_t largest_block();

// The machine shows the VGA games in their 256 colours (a byte-a-pixel
// game screen and its palette; the boards with PSRAM - Tom, 2026-10-12);
// false: in their own EGA mode's 16 (the classic boards)
bool vga_screen();

} // namespace plat

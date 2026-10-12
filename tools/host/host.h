// The platform layer on a PC (Linux): what the playsim and the sweep tests
// run the game code on (src/platform/*.h). The card is a host folder: paths
// under /GOLDBOX are files under pc::card_root(), as on the board, so
// path mistakes show up here too. The clock is the test driver's g_now
// (it moves it on as it likes); the log is stdout.
#pragma once

#include <cstdint>
#include <string>

extern uint32_t g_now;          // (defined by the driver: sim.cpp, sweep.cpp)

namespace pc {
// The folder that stands for /GOLDBOX
std::string& card_root();
// The host path of a card path
std::string host_path(const char* card_path);
// Files open now (the board's card has 3 slots - SLOTS=n to try others;
// one more fails, said on stderr; FSABORT=1 aborts there)
int open_files();
} // namespace pc

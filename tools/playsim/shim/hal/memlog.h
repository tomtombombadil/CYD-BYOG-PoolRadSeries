// The playsim's memory log: nothing (the PC's heap isn't the board's;
// sweep's memloop / HEAPLOG measure the PC side)
#pragma once
#include <cstdlib>
namespace memlog {
inline void begin(const char*, const char*) {}
inline void note(const char*) {}
inline void tick(bool) {}
// (plenty: the engine's make_room drops nothing here unless LOWMEM=kb
// pretends the largest block is that small - to try the recovery)
inline unsigned free_kb() { return 999; }
inline unsigned largest_kb()
{
    static const unsigned v = getenv("LOWMEM") ? static_cast<unsigned>(atoi(getenv("LOWMEM"))) : 999;
    return v;
}
}

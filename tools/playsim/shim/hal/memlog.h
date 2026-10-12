// The playsim's memory log: nothing (the PC's heap isn't the board's;
// sweep's memloop / HEAPLOG measure the PC side)
#pragma once
namespace memlog {
inline void begin(const char*, const char*) {}
inline void note(const char*) {}
inline void tick(bool) {}
inline unsigned free_kb() { return 0; }
inline unsigned largest_kb() { return 0; }
}

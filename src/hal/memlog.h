// The memory log (Tom, 2026-10-11, v0.85.7: "we have to be VERY aware of
// the state of the memory in use"): /GOLDBOX/_CYD/MEMORY.TXT, one line a
// moment worth knowing - "[h:mm:ss] what: free N KB, largest block N KB,
// lowest N KB" (largest block: the biggest single piece a load can get;
// lowest: the least free memory since the board started). This session's
// lines; the last session's are kept as MEMORY.OLD. Settings - Logs -
// Memory shows it.
#pragma once

namespace memlog {

// At start-up (the card mounted): the last session's log becomes MEMORY.OLD
void begin(const char* version, const char* board);
// A line now ("Play Test opened", "fight start", "area 2 block 1" ...)
void note(const char* what);
// Called often: a line every minute while `active`, and one at once when
// the largest block first drops under 24 KB (each time it goes back above)
void tick(bool active);
// The numbers now (KB)
unsigned free_kb();
unsigned largest_kb();

} // namespace memlog

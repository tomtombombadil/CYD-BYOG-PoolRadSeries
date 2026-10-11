// The title's lightning (Pool of Radiance; screen_facts.md 6): while its
// second title picture is up, now and then 1-3 flashes on one palette
// colour - shown as 9, 11, 15, 11, 9 for 150, 100, 50, 100 and 150 ms,
// then itself again. The original times the gaps by the CPU's speed; here
// they are 1-4 seconds.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
#pragma once

#include <cstdint>

namespace lightning {

struct State {
    uint32_t next = 0;        // when the next flashes start (0: not planned yet)
    uint32_t at = 0;          // when this flash started
    uint32_t seed = 12345;
    uint8_t  left = 0;        // flashes still to come (this one included)
};

// The colour to show in place of the flashing one at `now` (ms), or -1
int colour(State& s, uint32_t now);

} // namespace lightning

#include "lightning.h"

namespace lightning {

namespace {

constexpr uint8_t kColour[5] = {9, 11, 15, 11, 9};
constexpr uint16_t kUntil[5] = {150, 250, 300, 400, 550};    // ms from the flash's start

uint32_t rnd(State& s)
{
    s.seed = s.seed * 1103515245u + 12345u;
    return s.seed >> 16;
}

void plan(State& s, uint32_t now) { s.next = now + 1000 + rnd(s) % 3000; }

} // namespace

int colour(State& s, uint32_t now)
{
    if (s.left == 0) {
        if (s.next == 0) plan(s, now);
        if (static_cast<int32_t>(now - s.next) < 0) return -1;
        s.left = static_cast<uint8_t>(1 + rnd(s) % 3);
        s.at = now;
    }
    const uint32_t t = now - s.at;
    for (int i = 0; i < 5; ++i)
        if (t < kUntil[i]) return kColour[i];
    // This flash is over: the next one, or a gap
    if (--s.left > 0) {
        s.at = now;
        return kColour[0];
    }
    plan(s, now);
    return -1;
}

} // namespace lightning

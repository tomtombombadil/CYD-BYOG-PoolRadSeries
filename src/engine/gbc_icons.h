// The Gold Box Companion's combat icons, read from the player's own copy of
// the Companion on the card (Tom, 2026-10-11: its author's work - never in
// the engine): /GOLDBOX/GBC/DAX-Icons-EGA/001.bmp ..., 24 x 24 pictures in
// the 16 EGA colours on dark grey, in pairs (odd: ready, even: action).
// A character's pick is the engine's own note, by name, in
// _CYD/<folder>/ICONS.TXT ("NAME=n", n the pair from 1) - the games'
// character records only hold head / body / colour numbers.
// Plain C++, host-tested.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace gbc_icons {

constexpr int kSq = 24;
constexpr int kPicBytes = kSq * kSq / 2;      // 4-bit rows (as the engine's combat icons)
constexpr int kMaxPairs = 60;
constexpr const char* kFolder = "GBC/DAX-Icons-EGA";    // under /GOLDBOX

// A 24 x 24 BMP (24- or 32-bit, either row order) to 4-bit rows: the
// colours to EGA indexes (nearest), dark grey (the background) see-through
// (0), black to 8 (black in the fight's colours, where 0 and 8 swap).
bool bmp_to_pic4(dax::ByteSource& src, uint8_t* out);

// Picks: up to kMaxChoices names
constexpr int kMaxChoices = 32;
struct Choices {
    char    name[kMaxChoices][16] = {};
    uint8_t pair[kMaxChoices] = {};
    int     count = 0;
};
void parse(const char* text, size_t len, Choices& out);
size_t write(const Choices& c, char* out, size_t cap);
int find(const Choices& c, const char* name);          // the pair (1..), 0 none
void set(Choices& c, const char* name, int pair);      // pair 0: forget it

} // namespace gbc_icons

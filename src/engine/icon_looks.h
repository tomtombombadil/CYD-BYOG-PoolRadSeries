// Ready-made combat icons (Tom, 2026-10-11): the icon editor's gallery, an
// engine comfort (app/features.h CYD_ICON_GALLERY) - making an icon a value
// at a time with Next / Prev is the slowest part of making a character.
//
// A character's icon is the game's own pictures put together: one of the
// 14 heads (CHEAD.DAX) over one of the 32 bodies, each holding its weapon
// (CBODY.DAX), recoloured by six bytes - per part two colours, the low
// nibble the 1st colour, the high the 2nd (record 0x141 head, 0x142 body,
// 0x145-0x14A colours; alter_icon_facts.md 1.6). The tables here are only
// numbers made up for the engine - colour schemes (16-colour EGA indexes)
// and head / body / scheme combinations; the pictures always come from the
// player's own files. Plain C++, host-tested.
#pragma once

#include <cstdint>

namespace icon_looks {

constexpr int kHeads = 14;             // the games' head pictures
constexpr int kBodies = 32;            // and bodies (with their weapons)
constexpr int kSchemes = 24;           // colour schemes
constexpr int kLooks = 32;             // whole icons

// A colour scheme: the six colour bytes, in the record's order - body,
// arm, leg, hair (1st) / face (2nd), shield, weapon
struct Scheme {
    uint8_t c[6];
};
extern const Scheme kScheme[kSchemes];

// A whole icon: head, body (weapon), scheme
struct Look {
    uint8_t head, body, scheme;
};
extern const Look kLook[kLooks];

// The gallery's pages
enum class Page : uint8_t { Looks, Heads, Bodies, Colours };
constexpr int kPages = 4;

int count(Page p);
// Puts choice i of page p into a character record (the icon's bytes only;
// the size 0x144 stays the character's)
void apply(Page p, int i, uint8_t* rec);
// The choice on page p the record shows now, or -1
int match(Page p, const uint8_t* rec);
// The record's icon bytes with choice i of page p in place (out: head,
// body, the six colour bytes) - what the gallery shows in cell i
void preview(Page p, int i, const uint8_t* rec, uint8_t* head, uint8_t* body, uint8_t* colours);

} // namespace icon_looks

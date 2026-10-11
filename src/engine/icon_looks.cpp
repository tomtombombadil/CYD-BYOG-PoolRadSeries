#include "icon_looks.h"

#include <cstring>

namespace icon_looks {

namespace {

// (1st, 2nd) colour pairs to bytes: low nibble 1st, high 2nd
constexpr uint8_t P(int first, int second) { return static_cast<uint8_t>(first | (second << 4)); }

constexpr int kHead = 0x141, kBody = 0x142, kColours = 0x145;

} // namespace

// Made for the engine (looked at on the games' parts, Claude 2026-10-11).
// EGA colours: 1 blue 2 green 3 cyan 4 red 5 magenta 6 brown 7 light grey
// 8 black (shown in the fight palette) 9-15 the light ones, 14 yellow,
// 15 white. 0 is never used: it would show through.
const Scheme kScheme[kSchemes] = {
    //   body        arm         leg         hair / face  shield      weapon
    {{P(1, 9),   P(2, 10),  P(3, 11),  P(4, 12),  P(6, 14),  P(7, 15)}},   // the games' own (no change)
    {{P(7, 15),  P(7, 15),  P(8, 7),   P(6, 12),  P(14, 1),  P(7, 15)}},   // steel
    {{P(4, 12),  P(8, 4),   P(8, 6),   P(8, 12),  P(14, 4),  P(7, 15)}},   // crimson
    {{P(2, 10),  P(6, 2),   P(6, 8),   P(6, 12),  P(6, 2),   P(6, 7)}},    // forest
    {{P(8, 7),   P(8, 7),   P(8, 6),   P(8, 12),  P(7, 8),   P(7, 15)}},   // shadow
    {{P(15, 7),  P(15, 7),  P(15, 7),  P(6, 12),  P(14, 15), P(14, 6)}},   // white
    {{P(5, 13),  P(5, 13),  P(5, 13),  P(15, 12), P(14, 5),  P(6, 14)}},   // violet
    {{P(14, 6),  P(14, 15), P(1, 9),   P(14, 12), P(1, 14),  P(7, 15)}},   // gold
    {{P(3, 11),  P(1, 3),   P(1, 9),   P(14, 12), P(15, 3),  P(7, 15)}},   // sea
    {{P(6, 14),  P(6, 14),  P(6, 8),   P(8, 12),  P(4, 6),   P(7, 15)}},   // sand
    {{P(4, 6),   P(6, 4),   P(6, 8),   P(4, 12),  P(7, 6),   P(7, 15)}},   // rust
    {{P(10, 2),  P(10, 15), P(2, 10),  P(14, 12), P(15, 10), P(14, 15)}},  // spring
    {{P(7, 15),  P(7, 15),  P(8, 7),   P(8, 6),   P(14, 4),  P(7, 15)}},   // steel, darker skin
    {{P(1, 5),   P(14, 1),  P(1, 8),   P(8, 12),  P(14, 5),  P(14, 15)}},  // royal
    {{P(4, 14),  P(4, 12),  P(8, 4),   P(14, 12), P(4, 14),  P(14, 15)}},  // fire
    {{P(15, 11), P(11, 15), P(9, 11),  P(15, 12), P(15, 9),  P(11, 15)}},  // ice
    {{P(8, 5),   P(8, 5),   P(8, 5),   P(15, 7),  P(5, 8),   P(15, 7)}},   // night
    {{P(2, 6),   P(2, 6),   P(6, 2),   P(15, 12), P(2, 6),   P(6, 10)}},   // moss
    {{P(6, 12),  P(12, 12), P(6, 8),   P(8, 12),  P(6, 4),   P(7, 15)}},   // bare arms
    {{P(13, 5),  P(13, 15), P(5, 13),  P(14, 12), P(13, 15), P(6, 14)}},   // rose
    {{P(1, 9),   P(7, 15),  P(1, 8),   P(8, 6),   P(7, 1),   P(7, 15)}},   // guard, darker skin
    {{P(2, 10),  P(6, 2),   P(6, 8),   P(8, 6),   P(6, 2),   P(6, 7)}},    // forest, darker skin
    {{P(3, 1),   P(3, 11),  P(1, 8),   P(6, 12),  P(3, 11),  P(7, 15)}},   // teal
    {{P(7, 8),   P(4, 12),  P(8, 4),   P(15, 12), P(4, 7),   P(7, 15)}},   // grey and red
};

const Look kLook[kLooks] = {
    {0, 24, 1},  {5, 7, 2},   {9, 1, 3},   {8, 26, 10}, {6, 28, 6},  {0, 25, 5},  {1, 17, 4},  {3, 24, 7},
    {10, 18, 18}, {7, 1, 21}, {9, 22, 15}, {13, 28, 17}, {11, 30, 16}, {4, 20, 12}, {2, 4, 13}, {0, 19, 8},
    {9, 1, 11},  {2, 17, 19}, {6, 29, 14}, {13, 27, 15}, {12, 12, 23}, {4, 22, 10}, {5, 16, 22}, {0, 0, 0},
    {12, 24, 12}, {1, 7, 20}, {8, 1, 21},  {6, 30, 7},  {10, 9, 11}, {3, 11, 2},  {9, 21, 15}, {11, 31, 9},
};

int count(Page p)
{
    switch (p) {
    case Page::Looks: return kLooks;
    case Page::Heads: return kHeads;
    case Page::Bodies: return kBodies;
    case Page::Colours: return kSchemes;
    }
    return 0;
}

void preview(Page p, int i, const uint8_t* rec, uint8_t* head, uint8_t* body, uint8_t* colours)
{
    *head = rec[kHead];
    *body = rec[kBody];
    memmove(colours, rec + kColours, 6);         // (apply: the same bytes)
    if (i < 0 || i >= count(p)) return;
    switch (p) {
    case Page::Looks:
        *head = kLook[i].head;
        *body = kLook[i].body;
        memcpy(colours, kScheme[kLook[i].scheme].c, 6);
        break;
    case Page::Heads: *head = static_cast<uint8_t>(i); break;
    case Page::Bodies: *body = static_cast<uint8_t>(i); break;
    case Page::Colours: memcpy(colours, kScheme[i].c, 6); break;
    }
}

void apply(Page p, int i, uint8_t* rec)
{
    preview(p, i, rec, &rec[kHead], &rec[kBody], rec + kColours);
}

int match(Page p, const uint8_t* rec)
{
    const int n = count(p);
    for (int i = 0; i < n; ++i) {
        uint8_t h, b, c[6];
        preview(p, i, rec, &h, &b, c);
        if (h == rec[kHead] && b == rec[kBody] && memcmp(c, rec + kColours, 6) == 0) return i;
    }
    return -1;
}

} // namespace icon_looks

// The AD&D rules the games apply to a character (Curse first): the values
// worked out from stats and readied items, money, carrying.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// Rules learned from coab (Curse of the Azure Bonds); own code.
#pragma once

#include <cstdint>

#include "items.h"
#include "party.h"

namespace rules {

// Per-game item facts the rules need (from the profile)
struct ItemFacts {
    uint8_t arrow, quarrel;
    uint8_t elf_bonus[6];
};

// Strength as one number: 3-17, 18 = 18, 19-23 = 18/01-50, /51-75,
// /76-90, /91-99, /00; 19-25 = 24-30
int strength_group(const party::Character& c);
// Extra weight carried before slowing down (can be negative)
int max_encumbrance(const party::Character& c);
int dex_ac_bonus(const party::Character& c);

// Works out what the games keep up to date from stats and readied items:
// encumbrance, movement, AC, to-hit, damage dice / bonus, hands in use,
// attack level (into the record, as the games store them).
void recalc(party::Character& c, const items::Names& names, const ItemFacts& f);

// Money: worth in gold (copper 1, silver 10, electrum 100, gold 200,
// platinum 1000 copper; gems and jewellery not counted), and paying
// `gold` from the coins, making change as the games do
int gold_worth(const int money[7]);
int gold_worth(const party::Character& c);
void pay(int money[7], int gold);
void pay(party::Character& c, int gold);

// True if the character can't take the item (16 items, or too heavy).
bool too_heavy(party::Character& c, const uint8_t* item, const items::Names& names, const ItemFacts& f);
// Adds an item to a character (false: no room).
bool add_item(party::Character& c, const uint8_t* item);

// A shop's price for an item: its value moved by the shop's price factor
// (script word 0x7F6D: 1 = 1/16 ... 0x10 normal ... 0x80 = x8)
int price(const uint8_t* item, int factor);

} // namespace rules

// Random treasure items (the ECL TREASURE op's 0x80 + n): the kind rolled,
// then the 63-byte item record made as the games make it - a weapon or
// armour +1 / +2, arrows, bracers, a ring of protection, a scroll of 1-3
// spells, a potion or a wand (those from the program's own rows of ready
// made items, read from the player's copy).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// Rules learned from coab (the Curse reimplementation) and checked against
// the original program's listing; own code. The numbers (types, name words,
// weights, values, dice) are per-release facts in the profile.
#pragma once

#include <cstdint>

#include "create.h"
#include "items.h"

namespace treasure {

// A per-type number (weight, value per plus): pairs, then the default
struct ByType {
    const uint8_t*  type;
    const uint16_t* value;
    uint8_t         n;
    uint16_t        other;
    uint16_t of(int t) const;
};

struct Facts {
    // Item types
    uint8_t mu_scroll, cleric_scroll, potion, giant, wand, shield, arrow, ring, bracers, javelin;
    uint8_t heavy_crossbow;                 // rolled as a weapon: a shield instead
    uint8_t swords[5];                      // d10: 1-4, 5-7, 8, 9, 10
    uint8_t leather, padded, studded, ring_mail, plate;    // armour types (ring mail .. plate: "Mail")
    // Name words
    uint8_t plus_word;                      // "+1" = plus_word + 1 ...
    uint8_t armor_word, leather_word, mail_word, arrow_word, ring_word, of_prot_word;
    uint8_t bracers_word, of_word, ac6_word, ac4_word;
    uint8_t mu_word, cleric_word, with_word;     // "MU Scroll", "Clrc Scroll", "With N Spells" = with_word + N
    // Weights (count: arrows / quarrels 10, darts 5) and value per plus
    ByType   weight, value;
    uint8_t  bundle10[2], dart;             // types that come in tens / fives
    uint8_t  scroll_weight;
    uint16_t scroll_value;                  // x the spell's band
    // Scroll spells: per band 1-5, a die and what's added (magic-user, cleric)
    uint8_t mu_band[5][2], cleric_band[5][2];
};

// The program's rows of ready-made items: 7 rows of 8 words (w1, w2, w3,
// weight, value, effects 0x3C-0x3E); row 0 extra healing, 1 giant strength,
// 2 healing, 4 the wand, 6 the javelin of lightning
struct Rows {
    uint16_t r[7][8];
};

// One random item into rec (kRecordSize bytes)
void make(uint8_t* rec, const Facts& f, const Rows& rows, create::Dice& d);

} // namespace treasure

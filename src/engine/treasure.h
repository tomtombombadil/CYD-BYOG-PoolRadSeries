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
    // Pool of Radiance's ways (poolrad_combat_facts.md 6; all zero: Curse's)
    uint8_t bands;                          // the scrolls' bands (0: 5)
    bool    plus3;                          // a 20 on the plus's d20: +3 (Curse: 15-20 all +2)
    uint8_t ac2_word;                       // bracers AC 2 (+3)
    bool    keep_type;                      // a weapon roll of 48 / 49: the type rolled before (Curse: a shield)
    uint8_t misc_die;                       // 93-98: the type by this die (0: Curse's potion / giant / wand by d15)
    uint8_t misc[16];
    uint8_t last;                           // 99-100 (0: a shield)
    // The ready-made row a type takes: d(sides) + add - 1 picks a row's
    // first word number (the program's: row r = 1 + 8r; 0 none); sides 0: start[0]
    struct Pick {
        uint8_t type, sides, add;
        uint8_t start[11];
    };
    const Pick* picks;
    uint8_t     n_picks;
    uint8_t     javelin_start;              // the javelin of lightning's first word (0: Curse's row 6)
    uint8_t     plus2_start;                // the row made +2 (Pool's 0x61)
    // Secret of the Silver Blades makes its items another way (below; null: the above)
    const struct SecretFacts* secret = nullptr;
};

// Secret of the Silver Blades' random items (its TREASURE: GAME.OVR
// 0x2BD8 - the kind by dice tables; its create_item 0x34AB3): each item a
// kind roll through nested tables, then made by its type - a weapon, armour,
// a shield, a ring or bracers with a plus (d10), named by rules per type, a
// weight and a value a plus by type; a mage's or clerics' scroll of d3
// spells (a level die, then that level's spell from the program's tables);
// potions, the wand, the javelin of lightning from the program's rows (plus
// 1, nothing against saves)
struct Roll {
    uint8_t hi;                             // up to this roll ...
    uint8_t type;                           // ... the item type (0: table `sub`)
    uint8_t sub;
};
struct RollTable {
    uint8_t die, n;
    Roll    r[10];
};
struct NameRule {
    uint8_t type;
    uint8_t w3, w2;                         // words 3 and 2 (w2 0: the plus word there)
    uint8_t hidden;                         // the hidden-word bits (6 for the rest)
    uint8_t plus;                           // a fixed plus (0: the rolled one)
    uint8_t w1;                             // word 1 (0: the plus word when w2 isn't it)
    uint8_t saves;                          // plus against saves
};
struct SecretFacts {
    const RollTable* tables;                // [0] the d100 kind
    uint8_t          n_tables;
    uint8_t          plus_lo, plus_hi, plus_more[4];   // the types with a plus
    uint8_t          plus_die, plus_band[2];           // d10: up to band 0 +1, band 1 +2, else +3
    uint8_t          plus_word;                        // "+1" = plus_word + 1
    const NameRule*  names;
    uint8_t          n_names;
    ByType           weight;                // by type; `other`: the rest (in twenties)
    uint8_t          twenty[1];             // also in twenties (the dart)
    ByType           value;                 // a plus
    // Scrolls: the types (mage's, clerics'), the spells die, word 1 = count_word + n,
    // word 2, word 3 (mage's / clerics'), weight, value a spell level, the level
    // dice, the hidden bits; the spells per level: the program's tables (Rows::spells)
    uint8_t          mu_scroll, cleric_scroll, spells_die, count_word, scroll_word, mu_word, cleric_word;
    uint8_t          scroll_weight, scroll_value, mu_levels, cleric_levels, mu_hidden, cleric_hidden;
    uint8_t          mu_dice[7], cleric_dice[7];
    // The program's rows: a potion by d8 (its rows), the wand, the giant
    // strength potion, the javelin (d5 = 5)
    uint8_t          potion, potion_rows[8], wand, wand_row, giant, giant_row, javelin, javelin_row;
    // The spells' tables in the program's data segment (a level's [0] first)
    // and their strides
    uint16_t         mu_spells, cleric_spells;
    uint8_t          mu_stride, cleric_stride;
};

// The program's ready-made items as its array of words (w[1 + 8r] the
// first of row r: w1, w2, w3, weight, value, effects 0x3C-0x3E); Curse's 7
// rows: 0 extra healing, 1 giant strength, 2 healing, 4 the wand, 6 the
// javelin of lightning; Pool's 19
constexpr int kRowWords = 160;
struct Rows {
    uint16_t w[kRowWords];
    const uint16_t* row(int r) const { return &w[1 + 8 * r]; }
    // Secret: its scrolls' spells (the program's tables, 11 bytes a level:
    // [0] mage's, [1] clerics'; spell = spells[k][level - 1][roll])
    uint8_t spells[2][7][11] = {};
};

// One random item into rec (kRecordSize bytes). last: the type rolled
// before in this treasure (Pool's 48 / 49), updated; may be null
void make(uint8_t* rec, const Facts& f, const Rows& rows, create::Dice& d, uint8_t* last = nullptr);

} // namespace treasure

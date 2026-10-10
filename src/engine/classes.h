// Character classes: what a character's classes and levels give them -
// THAC0, saving throws, thief skills, spell slots, the spells clerics know -
// worked out from the rule tables in the player's own game program (Curse:
// START.EXE's data segment). Plain C++, host-tested.
//
// The tables are read from the program at run time (bring your own game):
// the profile names where they are (offsets in the data segment); this file
// only knows how the games use them (learned from coab, the Curse
// reimplementation, and checked against the program's code and GOG's
// sample party - own code).
#pragma once

#include <cstddef>
#include <cstdint>

#include "party.h"

namespace classes {

constexpr int kClasses = 8;            // cleric, druid, fighter, paladin, ranger, magic-user, thief, monk
enum Cls { Cleric = 0, Druid, Fighter, Paladin, Ranger, MagicUser, Thief, Monk };

// Where the rule tables are (data segment offsets) - per game, in the profile
struct Layout {
    uint16_t lo, hi;                   // the part of the data segment to read
    uint16_t spells;                   // 16 bytes a spell (byte 0 class: 0 cleric 1 druid 2 magic-user; 1 level)
    uint8_t  spell_count;
    uint16_t thac0;                    // [class][level 0-12]
    uint16_t class_flags;              // [class]: the bit a class sets in the character's class flags
    uint16_t class_masks;              // [class]: training masks
    uint16_t max_hit_dice;             // [class]: the level hit dice stop at
    uint16_t thief_base;               // + level * 8 + skill (1-8)
    uint16_t thief_race;               // + race * 8 + skill (signed)
    uint16_t thief_dex;                // + dexterity * 5 + skill (1-5, signed)
    uint16_t stat_limits;              // [race] 16 bytes: Str min m/f max m/f, Str00 max m/f, Int-Cha min max
    uint16_t race_classes;             // [race] 14 bytes: count, classes
    uint16_t race_ages;                // [race] 7 x (u16 base, u8 dice, u8 sides)
    uint16_t age_brackets;             // [race] 5 x u16
    uint16_t class_min;                // [class 0-16] 6 bytes: Str Int Wis Dex Con Cha minimums
    uint16_t class_alignments;         // [class 0-16] 10 bytes: count, alignments
    uint16_t class_records;            // [class] 99 bytes: experience for levels 2-12 (11 x i32), spell slots gained (11 x 5)
    uint16_t saves;                    // + class * 60 + level * 5 + save (level 1-12)
};

// A copy of the tables (read from the program)
class Tables {
public:
    Layout   lay{};
    // ds_lo..ds_hi of the data segment
    bool set(const Layout& l, const uint8_t* bytes, size_t n);
    uint8_t  u8(uint16_t ds) const { return ds >= lay.lo && ds < lay.hi ? data_[ds - lay.lo] : 0; }
    int8_t   s8(uint16_t ds) const { return static_cast<int8_t>(u8(ds)); }
    uint16_t u16(uint16_t ds) const { return static_cast<uint16_t>(u8(ds) | u8(static_cast<uint16_t>(ds + 1)) << 8); }
    int32_t  i32(uint16_t ds) const;

    int  thac0(int cls, int level) const { return s8(static_cast<uint16_t>(lay.thac0 + cls * 13 + level)); }
    int  max_hit_dice(int cls) const { return u8(static_cast<uint16_t>(lay.max_hit_dice + cls)); }
    int  save(int cls, int level, int kind) const { return u8(static_cast<uint16_t>(lay.saves + cls * 60 + level * 5 + kind)); }
    // Spell slots gained on reaching `level` (2-12): row level - 2
    int  slots_gained(int cls, int level, int col) const
    {
        return u8(static_cast<uint16_t>(lay.class_records + cls * 99 + 44 + (level - 2) * 5 + col));
    }
    // Experience needed to go from `level` (1-11) to the next; <= 0: no more
    int32_t exp_needed(int cls, int level) const
    {
        return i32(static_cast<uint16_t>(lay.class_records + cls * 99 + (level - 1) * 4));
    }
    int  spell_class(int spell) const { return u8(static_cast<uint16_t>(lay.spells + spell * 16)); }
    int  spell_level(int spell) const { return u8(static_cast<uint16_t>(lay.spells + spell * 16 + 1)); }

private:
    uint8_t data_[4096] = {};
};

// The level that counts for a class (a human who changed class adds the
// old one once the new class has passed it)
int skill_level(const party::Character& c, int cls);

// What the classes give: THAC0, hit dice, attacks, spell slots and the
// spells clerics know, saving throws, thief skills, class flags. (Items
// that change these - protection scrolls, rings - are applied elsewhere.)
void class_bonuses(party::Character& c, const Tables& t);

// Parts of it, for training and creation
void spell_slots(party::Character& c, const Tables& t);
// A Ring of Wizardry readied (on: magic-user levels 1-3 doubled) or put
// away (the slots without it; magic-user spells past them forgotten, the
// later ones first) - curse_finish_facts.md 5.3
void wizardry(party::Character& c, const Tables& t, bool on);
void saving_throws(party::Character& c, const Tables& t);
void thief_skills(party::Character& c, const Tables& t);

} // namespace classes

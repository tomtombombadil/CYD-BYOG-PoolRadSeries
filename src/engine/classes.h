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
    // Pool of Radiance's shapes (create_facts.md 2; all zero: Curse's):
    // THAC0 [class][11 levels 0-10], saves + class * 45 (levels 1-9),
    // class records of 56 bytes (8 x i32 experience for levels 2-9, then
    // 8 x 3 spell slots: the TOTAL for levels 2-9), race classes 12 bytes,
    // the spell table's class byte 0 cleric / 1 magic-user / 2 the items'
    // and monsters' (read as Curse's 0 / 2 / 3), no hit dice limit, money
    // dice per class (count, sides); the class flags / masks in a second
    // part of the data segment (lo2..hi2)
    bool     pool;
    uint16_t money_dice;
    uint16_t lo2, hi2;
    // Secret of the Silver Blades' shapes (secret_facts.md 9): 7 classes'
    // rows (no monk), levels to 18 - THAC0 [class][19 levels 0-18], saves +
    // class * 90 (levels 1-18), class records of 187 bytes (17 x i32
    // experience for levels 2-18, then 17 x 7 spell slots gained: spell
    // levels 1-7), 119 spells; its race-indexed tables in its own race
    // order (party::race_to_secret)
    bool     secret = false;
    // Pools of Darkness's (darkness_facts.md 37; set with secret, whose
    // rules it keeps unless a dk shape says otherwise): levels to 40 -
    // THAC0 [class][22 levels 0-21], saves + class * 105 + min(level, 21) *
    // 5, class records of 301 bytes (12 x u32 experience for levels 0-11,
    // past 11 level 11's + (level - 11) x xp_step[class] (7 x u32); spell
    // slots in all - not gains - at +0x26 + min(level, 29) x 9 + spell
    // level 1-9), 127 spells in Secret's shape with class codes 0 cleric, 1
    // druid, 2 magic-user, 3 items; thief skills to 18th level; its races
    // numbered its own way (party::race_to_darkness)
    bool     dk = false;
    uint16_t xp_step = 0;
    // Champions of Krynn's (champions_facts.md 4): Curse's shapes (8 class
    // columns, the knight in the monk's) but its race-indexed tables by its
    // own races (0 Silvanesti elf ... 6 human - the record's own race,
    // Character::ext / a creation code race_krynn), race classes 15 bytes a
    // race, ages 8 classes (32 bytes), alignments a mask a class (bit i =
    // the i-th of the list at DS lo2: LG LN NG TN CG CN), and experience by
    // rows of its own (krynn_xp_row: the cleric's god, the knight's order,
    // the magic-user's robe); the thief skills' race table read at the
    // original's place - the row of the race before (with rules::fixes():
    // the row of Curse's race it maps to)
    bool     krynn = false;
    // Champions (GAME.OVR 0x3A03A): the spells a cleric knows - for each
    // spell level 1-4 it has slots for, the list at this + level x 10
    // (from the 2nd byte, 10 at most, 0 ends)
    uint16_t cleric_lists = 0;
    // Gateway's training (its GAME.OVR 0x3EB10): each class's last level
    // (8 bytes at this offset, read through lo2..hi2: training stops when
    // the level reaches it) and its own race limits (create.cpp
    // gw_race_limited) instead of Curse's; 0: none
    uint16_t level_caps = 0;
};

// Champions: a race in creation's lists (the Curse number can't tell its two
// elves and two dwarves apart): kKrynnRace + its own race
constexpr int kKrynnRace = 100;

// A copy of the tables (read from the program; on the heap, as big as the
// game's tables are)
class Tables {
public:
    Layout   lay{};
    Tables() = default;
    Tables(const Tables& o) { *this = o; }
    Tables& operator=(const Tables& o);
    ~Tables() { delete[] data_; }
    // ds_lo..ds_hi of the data segment (and lo2..hi2: second)
    bool set(const Layout& l, const uint8_t* bytes, size_t n, const uint8_t* bytes2 = nullptr, size_t n2 = 0);
    // The same, taking over `owned` (new[]'d, hi - lo bytes) instead of a
    // copy - no second block while the tables load (deleted when false)
    bool adopt(const Layout& l, uint8_t* owned, size_t n, const uint8_t* bytes2 = nullptr, size_t n2 = 0);
    uint8_t  u8(uint16_t ds) const
    {
        if (data_ && ds >= lay.lo && ds < lay.hi) return data_[ds - lay.lo];
        return ds >= lay.lo2 && ds < lay.hi2 ? data2_[ds - lay.lo2] : 0;
    }
    int8_t   s8(uint16_t ds) const { return static_cast<int8_t>(u8(ds)); }
    uint16_t u16(uint16_t ds) const { return static_cast<uint16_t>(u8(ds) | u8(static_cast<uint16_t>(ds + 1)) << 8); }
    int32_t  i32(uint16_t ds) const;

    // The highest level the tables have (Curse 12, Pool 9, Secret 18; THAC0
    // one more row in Pool)
    int  top_level() const { return lay.dk ? 40 : lay.pool ? 9 : lay.secret ? 18 : 12; }
    // Classes with rows in the tables (Secret has no monk's)
    int  classes() const { return lay.secret ? 7 : kClasses; }
    int  thac0(int cls, int level) const
    {
        const int n = lay.dk ? 22 : lay.pool ? 11 : lay.secret ? 19 : 13;
        if (cls >= classes()) return 0;
        if (level > n - 1) level = n - 1;
        return s8(static_cast<uint16_t>(lay.thac0 + cls * n + level));
    }
    int  max_hit_dice(int cls) const
    {
        return lay.pool || cls >= classes() ? 99 : u8(static_cast<uint16_t>(lay.max_hit_dice + cls));
    }
    int  save(int cls, int level, int kind) const
    {
        if (cls >= classes()) return 20;
        if (lay.dk) return u8(static_cast<uint16_t>(lay.saves + cls * 105 + (level > 21 ? 21 : level) * 5 + kind));
        if (level > top_level()) level = top_level();
        return u8(static_cast<uint16_t>(lay.saves + cls * (lay.pool ? 45 : lay.secret ? 90 : 60) + level * 5 + kind));
    }
    int  record_size() const { return lay.dk ? 301 : lay.pool ? 56 : lay.secret ? 187 : 99; }
    // Spell levels a class record's slot rows have (Curse 5, Secret 7, Darkness 9)
    int  slot_levels() const { return lay.dk ? 9 : lay.secret ? 7 : 5; }
    // Darkness: the slots of spell level `sl` (1-9) a class has in all at `level`
    int  slots_at(int cls, int level, int sl) const
    {
        if (cls >= classes() || level < 1) return 0;
        if (level > 29) level = 29;
        return u8(static_cast<uint16_t>(lay.class_records + cls * 301 + 0x26 + level * 9 + sl));
    }
    // Spell slots gained on reaching `level` (2-top): row level - 2
    // (Champions: cls is its row - xp_row)
    int  slots_gained(int cls, int level, int col) const
    {
        if (cls >= (lay.krynn ? 10 : classes())) return 0;
        return u8(static_cast<uint16_t>(lay.class_records + cls * record_size() + (top_level() - 1) * 4 +
                                        (level - 2) * slot_levels() + col));
    }
    // A race-indexed table's row for the record's race (Curse's numbers;
    // Champions: kKrynnRace + its own, or Curse's turned into its own)
    int  race_row(int race) const
    {
        if (lay.krynn) return race >= kKrynnRace ? race - kKrynnRace : party::race_to_krynn(static_cast<uint8_t>(race));
        if (lay.dk) return party::race_to_darkness(static_cast<uint8_t>(race));
        return lay.secret ? party::race_to_secret(static_cast<uint8_t>(race)) : race;
    }
    // Pool: the spell slots a class has in all at `level` (2-9), level col + 1
    int  slots_total(int cls, int level, int col) const
    {
        return u8(static_cast<uint16_t>(lay.class_records + cls * 56 + 32 + (level - 2) * 3 + col));
    }
    // Champions (its GAME.OVR 0x39CC4): the experience / slot row of a class
    // for this character - a cleric's by its god (Paladine ... Mishakal 0,
    // the others 1), fighter 2, a knight's by the order (Crown 3, Sword 4,
    // Rose 5), ranger 6, a magic-user's by the robe (White 7, else 8), thief
    // 9 (the others: the class); not Champions: the class
    int  xp_row(const party::Character* c, int cls) const
    {
        if (!lay.krynn || !c) return cls;
        switch (cls) {
        case 0: return c->ext[party::kExtGod] <= 4 ? 0 : 1;
        case 2: return 2;
        case 7: return c->ext[party::kExtKnight] == 2 ? 4 : c->ext[party::kExtKnight] == 3 ? 5 : 3;
        case 4: return 6;
        case 5: return c->ext[party::kExtRobe] == 1 ? 7 : 8;
        case 6: return 9;
        default: return cls;
        }
    }
    // Experience needed to go from `level` (1-11) to the next; <= 0: no more
    // (Champions: by the character's row - xp_row)
    int32_t exp_needed(int cls, int level, const party::Character* c) const
    {
        if (level < 1 || level > top_level() - 1 || cls >= classes()) return -1;
        return exp_needed(xp_row(c, cls), level, true);
    }
    int32_t exp_needed(int cls, int level, bool row = false) const
    {
        if (level < 1 || level > top_level() - 1 || cls >= (row && lay.krynn ? 10 : classes())) return -1;
        if (lay.dk) {
            // (Darkness, ovr 0x39349: the table to 11th level, then steps)
            const uint16_t r = static_cast<uint16_t>(lay.class_records + cls * 301);
            const int next = level + 1;
            if (next <= 11) return i32(static_cast<uint16_t>(r + next * 4));
            return i32(static_cast<uint16_t>(r + 11 * 4)) + (next - 11) * i32(static_cast<uint16_t>(lay.xp_step + cls * 4));
        }
        return i32(static_cast<uint16_t>(lay.class_records + cls * record_size() + (level - 1) * 4));
    }
    // 0 cleric, 1 druid, 2 magic-user, 3 the items' and monsters' (Pool's numbers moved to these)
    // (Secret's rows start with a byte of their own: Curse's 15 follow; its
    // class codes 0 cleric, 1 druid, 3 magic-user, 4 the items' and monsters')
    uint16_t spell_row(int spell) const { return static_cast<uint16_t>(lay.spells + spell * 16 + (lay.secret ? 1 : 0)); }
    int  spell_class(int spell) const
    {
        const int v = u8(spell_row(spell));
        if (lay.dk) return v;               // (Darkness's own codes are these)
        if (lay.secret) return v >= 3 ? v - 1 : v;
        return lay.pool && v > 0 ? v + 1 : v;
    }
    int  spell_level(int spell) const { return u8(static_cast<uint16_t>(spell_row(spell) + 1)); }

private:
    uint8_t* data_ = nullptr;          // lay.hi - lay.lo bytes
    uint8_t  data2_[32] = {};
};

// The level that counts for a class (a human who changed class adds the
// old one once the new class has passed it)
int skill_level(const party::Character& c, int cls);

// What the classes give: THAC0, hit dice, attacks, spell slots and the
// spells clerics know, saving throws, thief skills, class flags. (Items
// that change these - protection scrolls, rings - are applied elsewhere.)
void class_bonuses(party::Character& c, const Tables& t);

// Spell slots of a kind (0 cleric, 1 druid, 2 magic-user) and spell level
// (1-5 in the record, Secret's 6 and 7 and Darkness's 8 and 9 in the extension)
int slots(const party::Character& c, int cls, int level);
// The spell book: spells 1-100 in the record, Secret's 101-117 and Darkness's
// 118-126 in the extension
bool in_book(const party::Character& c, int spell);
void to_book(party::Character& c, int spell);

// Parts of it, for training and creation
void spell_slots(party::Character& c, const Tables& t);
// A Ring of Wizardry readied (on: magic-user levels 1-3 doubled) or put
// away (the slots without it; magic-user spells past them forgotten, the
// later ones first) - curse_finish_facts.md 5.3
void wizardry(party::Character& c, const Tables& t, bool on);
void saving_throws(party::Character& c, const Tables& t);
void thief_skills(party::Character& c, const Tables& t);

} // namespace classes

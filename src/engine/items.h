// Items: the games' 63-byte item records (Curse), their names and the item
// type table (the ITEMS file).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Learned from coab (Curse of the Azure Bonds); own code. An item record:
//   0x00 a name (unused here: the games build it from the name words)
//   0x2E type (index into the ITEMS table), 0x2F / 0x30 / 0x31 name words
//   1-3 (indexes into the program's word list, 0 = none), 0x32 plus
//   (signed), 0x33 plus vs saves, 0x34 readied, 0x35 hidden words (bit 2
//   hides word 1, bit 1 word 2, bit 0 word 3 - until identified), 0x36
//   cursed, 0x37 weight (u16), 0x39 count, 0x3A value (u16), 0x3C-0x3E
//   effects
// The name: words 3, 2, 1 (those present and not hidden), with an "s" on
// one of them when there are 2 or more (the games' rules, below).
// The ITEMS file: 2 bytes, then 16 bytes per type: 0 slot (0 weapon, 1
// second hand, 2 armour ...), 1 hands, 2-4 damage vs large (dice, sides,
// bonus), 5 attacks, 6 armour value (0x80 + AC points), 9-11 damage vs
// man-sized, 12 range, 13 classes, 14 flags (1 uses arrows, 2 missile:
// dexterity to hit, 4 melee: strength to hit and damage, 0x10 thrown,
// 0x80 uses quarrels).
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace items {

constexpr size_t kRecordSize = 0x3F;
constexpr int kWords = 256;             // name words 1-255
constexpr int kTypes = 0x81;

struct Item {
    const uint8_t* r;                   // a record (kRecordSize bytes)
    int  type() const { return r[0x2E]; }
    int  word(int i) const { return r[0x2E + i]; }      // i = 1..3
    int  plus() const { return static_cast<int8_t>(r[0x32]); }
    bool readied() const { return r[0x34] != 0; }
    int  hidden() const { return r[0x35]; }
    bool cursed() const { return r[0x36] != 0; }
    int  weight() const { return r[0x37] | r[0x38] << 8; }
    int  count() const { return r[0x39]; }
    int  value() const { return r[0x3A] | r[0x3B] << 8; }
};

// What the item types are (the ITEMS file)
struct TypeInfo {
    uint8_t slot = 0xFF, hands = 0;
    uint8_t dice_large = 0, sides_large = 0;
    int8_t  bonus_large = 0;
    uint8_t attacks = 0;
    uint8_t ac = 0;                     // armour: 0x80 + its AC value (shield: 0x80 + 1)
    uint8_t dice = 0, sides = 0;
    int8_t  bonus = 0;
    uint8_t range = 0, classes = 0, flags = 0;
    uint8_t blow = 0;                   // byte 7: 1 piercing, 0x80 blunt
};
constexpr uint8_t kSlotWeapon = 0, kSlotArmour = 2;

// The name words (from the program) and item types (from ITEMS)
class Names {
public:
    // Reads the words: `count` Pascal strings in slots of `stride` bytes
    // (word 1 first) from a buffer holding them.
    void set_words(const uint8_t* table, int count, int stride);
    // Reads the ITEMS file.
    bool read_types(dax::ByteSource& src);
    const char* word(int i) const { return i > 0 && i < kWords && off_[i] ? text_ + off_[i] : ""; }
    const TypeInfo& type(int t) const { static const TypeInfo none; return t >= 0 && t < kTypes ? types_[t] : none; }
    bool has_words() const { return used_ > 1; }

    // The plural rules' special cases (per game, from the profile): the
    // missile types that take the "s" on their last word, the word that
    // stops that, and the flask of oil
    struct Plural { uint8_t arrow, quarrel, dart, flask, keep1, keep2; };
    void set_plural(const Plural& p) { plural_ = p; }
    int flask() const { return plural_.flask; }      // the flask of oil's item type

    // The item's name as the games print it ("Long Sword", "3 Arrows",
    // "Long Sword +1"), with the count in front when there is one.
    void name(const Item& it, char* out, size_t cap, bool all_words = false) const;
    // Detect Magic on someone in the party (curse_finish_facts.md 4): names
    // get "* " first for magic (plus or save plus above 0) and cursed items
    bool detect = false;

private:
    char     text_[3072] = {};          // the words, each 0-terminated
    uint16_t off_[kWords] = {};
    size_t   used_ = 1;
    TypeInfo types_[kTypes];
    Plural   plural_{73, 28, 9, 86, 0x87, 0xB1};
};

// Treasure on the ground / a shop's goods (TREASURE in the scripts): coins
// (copper, silver, electrum, gold, platinum, gems, jewellery) and items
constexpr int kMaxGround = 64;        // Curse's biggest shop: 58
struct Ground {
    int     money[7] = {};
    uint8_t item[kMaxGround][kRecordSize] = {};
    int     n = 0;
    void clear() { *this = Ground{}; }
    bool any_money() const { for (int m : money) if (m) return true; return false; }
};

// The readied item a character holds in `slot` (weapon, armour ...): its
// index in the list, or -1.
int readied_in(const uint8_t (*recs)[kRecordSize], int n, const Names& names, uint8_t slot);

// ---- What a readied item does (item byte 0x3E = 0x80 + code, 0x3D its
// value). The codes mean different things in each game
// (curse_finish_facts.md 5, poolrad_combat_facts.md 2.2): the game's table
// says which, by code.
enum class Passive : uint8_t {
    None,           // nothing (also: byte 0x3E under 0x80)
    Effect,         // while readied: effect 0x3D (Curse 0x80; Pool 0x80-0x82, 0x85, 0x86, 0x88, 0x8A, 0x8B)
    Wizardry,       // magic-user spell levels 1-3 doubled (Curse 0x81)
    Dexterity,      // Gauntlets of Dexterity (Curse 0x82)
    Alignment,      // keyed to alignment 0x3D & 0x0F, else (0x3D >> 4) damage (Curse / Pool 0x84)
    GiantGirdle,    // Strength by 0x3D: 18/00, 19 ... 24 (Curse 0x85)
    Dwarves,        // Constitution +1, Charisma -1, the dwarves' poison saves (Curse 0x86)
    Ioun,           // +1 to stat 0x3D under 18 (Curse 0x88)
    HammerOff,      // put away: the Spiritual Hammer's effect ends (Curse / Pool 0x89)
    Fumbling,       // Dexterity -2 (Curse 0x8A)
    Thievery,       // Gloves of Thievery (Curse 0x8B)
    Stupidity,      // Intelligence 7 (Curse 0x8C)
    Powerless,      // Strength and Intelligence 3 (Curse 0x8D)
    OgrePower,      // Strength 18/00 through the strength effect, "is stronger" (Pool 0x83)
    NeedGiant,      // only with giant strength (19+): "Must have Giant Strength" (Pool 0x87, the boulder)
};
constexpr int kPassiveCodes = 16;
// The game's table (16 codes; nullptr: Curse's) - set as a game opens
void set_passive(const Passive* codes);
Passive passive(const uint8_t* item);
// Curse's and Pool of Radiance's tables
extern const Passive kCursePassive[kPassiveCodes];
extern const Passive kPoolPassive[kPassiveCodes];

// ---- The originals' item mistakes, corrected when the player wants
// (engine comfort CYD_ITEM_FIXES). Pool of Radiance's "Long Sword +2 Flame
// Tongue" (Tom, 2026-10-12): its record has no flame tongue effect - its
// 0x3D is 0x16 (Use: Find Traps) and 0x3E 0, where Curse's same sword has
// the effect 0x06 with code 0x80 (poolrad_items_facts.md 7). Fixed it is
// the AD&D sword, as Curse's: +1, and through the effect +2 against
// regenerating creatures, +3 against cold-using / avian ones, +4 against
// undead, its blows fire (a troll it fells stays down).
struct FlameFix {
    uint8_t type = 0, word = 0;        // the item type, its first name word ("Flame Tongue"); 0: no fix
    uint8_t plus = 0, spell = 0;       // the original's plus and 0x3D
    uint8_t effect = 0, code = 0x80;   // fixed: +1, 0x3D the flame tongue effect, 0x3E this code
    uint8_t plus_word = 0, plus1_word = 0;   // the name's plus word (0x30): the original's ("+2"), +1's
};
// The game's sword (nullptr: none) and whether the fix is wanted
void set_flame_fix(const FlameFix* f, bool on);
bool flame_fix_on();
// One record made as wanted (fixed, or back to the original's bytes);
// `original` true: the original's bytes whatever is wanted (the files).
// True when it changed.
bool fix_item(uint8_t* rec, bool original = false);

} // namespace items

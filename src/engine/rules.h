// The AD&D rules the games apply to a character (Curse first): the values
// worked out from stats and readied items, money, carrying.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// Rules learned from coab (Curse of the Azure Bonds); own code.
#pragma once

#include <cstdint>

#include "create.h"
#include "items.h"
#include "party.h"

namespace rules {

// Per-game item facts the rules need (from the profile)
struct ItemFacts {
    uint8_t arrow, quarrel;
    uint8_t elf_bonus[6];
    // Effects in the stats: Strength, the giant strength potion's, Enlarge,
    // Friends, Feeblemind (0: none)
    uint8_t strength_fx = 0, giant_fx = 0, enlarge_fx = 0, friends_fx = 0, feeble_fx = 0;
    // Spiritual Hammer: its effect, the item's type and name words
    uint8_t hammer_fx = 0, hammer_type = 0, hammer_word = 0, hammer_word2 = 0;
};

// The stats as they stand (the games' recalculation; spell_facts.md and
// coab's facts): from each stat's own value, readied items that work on
// stats (third effect byte 0x80 + code: 5 giant strength, 8 a +1 to one
// stat under 18, 2 dexterity, 6 / 10 / 12 / 13 others) and the effects
// (Strength, Enlarge, the potion's, Friends, Feeblemind). Bytes 0x10 +
// 2 i hold each stat's own value and 0x11 + 2 i the one in use; for the
// 18/xx percentage the other way round (0x1D own, 0x1C in use).
void stats(party::Character& c, const ItemFacts& f);

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

// ---- Locked doors (door_facts.md): door state 2 locked, 3 not pickable
// Bash: each member in party order (everyone, as the original) tries by
// the Strength in use until one breaks it. A not-pickable door: those too
// weak for it make *bash_gone true (Bash is offered no more until a step).
bool bash_door(const party::Party& p, int state, create::Dice& d, bool* bash_gone);
// Pick (locked doors only): each okay member rolls d100 against Open Locks
// (record 0xEB) until one opens it
bool pick_lock(const party::Party& p, create::Dice& d);
// Pick is offered: someone has a thief level that counts (the thief level,
// or a former one their new class has passed)
bool has_thief(const party::Party& p);
// Knock is offered: the first member with Knock (0x1F) memorized; -1 none
int knock_member(const party::Party& p, int knock_spell);

// Money: worth in gold (copper 1, silver 10, electrum 100, gold 200,
// platinum 1000 copper; gems and jewellery not counted), and paying
// `gold` from the coins, making change as the games do
int gold_worth(const int money[7]);
int gold_worth(const party::Character& c);
void pay(int money[7], int gold);
void pay(party::Character& c, int gold);

// Coins: the party's player characters put all theirs in the pool; the
// pool shared out (each coin kind from jewellery down: equal shares, one
// more of the rest each while it lasts, then what's left to whoever can
// still carry it; the remainder stays in the pool). Encumbrance follows.
int max_load(const party::Character& c);       // 1500 + the strength allowance
void pool(party::Party& p, int money[7]);
void share(party::Party& p, int money[7]);

// True if the character can't take the item (16 items, or too heavy).
bool too_heavy(party::Character& c, const uint8_t* item, const items::Names& names, const ItemFacts& f);
// Adds an item to a character (false: no room).
bool add_item(party::Character& c, const uint8_t* item);

// A character's items (Items: Drop, Trade, Halve, Join)
void remove_item(party::Character& c, int i);
// An item that works while readied (its effect byte 0x3E 0x80 and up),
// readied (`on`) or put away: 0x80 - the effect in 0x3D is theirs while
// readied (for good: no time); 0x84 - keyed to an alignment (0x3D & 0x0F):
// the wrong one can't ready it (put away again) and takes 0x3D >> 4
// damage. False when the item didn't stay readied. (The others - stats,
// a ring of wizardry ... - are to come.)
bool worn(party::Character& c, int i, bool on);
// Spiritual Hammer (spell_facts.md 0x1C): while they have its effect, the
// hammer is in their items, readied (a new one when it's gone and there's
// room: +1, "Gains an item" - true); without it, the hammer goes
bool keep_hammer(party::Character& c, const items::Names& names, const ItemFacts& f);
// Halve: a pile of n becomes n - n/2 and a new pile of n/2 (not readied);
// false when it can't (one of it, or 16 items already)
bool halve(party::Character& c, int i);
// Join: the same items in other piles go onto pile i (255 a pile at most);
// how many piles were joined in
int join(party::Character& c, int i);
// Sell (a shop): what the shop gives, in gold - half the value; piles of
// arrows / quarrels count each one, other piles count / 20 of each
int sell_value(const uint8_t* item, const ItemFacts& f);

// ---- The temple (Curse; costs and effect ids from the game's code, via the profile)
enum Cure : uint8_t {
    kCureBlindness, kCureDisease, kCureLight, kCureSerious, kCureCritical, kHealCure, kNeutralizePoison, kRaiseDead,
    kRemoveCurse, kStoneToFlesh, kCures
};
struct CureFacts {
    uint16_t cost[kCures];            // gold
    uint8_t  blinded, disease[6], feeblemind, poisoned, slow_poison, poison_damage, animate_dead, curse;
};
// Removes a character's effects of a type; how many
int remove_affects(party::Character& c, uint8_t type);
// Slow Poison's clock as `minutes` pass outside fights (spell_facts.md
// 0x1A): a hit point every 10 minutes (down to 1); when Slow Poison runs
// out while they're still poisoned, they die ("dies from poison": true).
// Call before the effects' minutes are taken (magic::tick_affects).
bool poison_clock(party::Character& c, int minutes, const CureFacts& f);
// Whether the cure does anything for them (else the temple asks "cast cure
// anyway?"; wounds always do)
bool needs_cure(const party::Character& c, Cure cure, const CureFacts& f);
// The cure's work (paid for): wounds healed by the dice (light 1d8,
// serious 2d8+1, critical 3d8+3, Heal all but 1d4 and the blindness,
// diseases, feeblemind), effects removed, the dead raised (1 HP), a curse
// lifted (the effect, else the first cursed item comes off), stone to flesh
void apply_cure(party::Character& c, Cure cure, const CureFacts& f, create::Dice& d);
// Hit points back (up to the most), for the living, unconscious or dying;
// outside a fight the dying come round and the unconscious wake (able to
// fight again). False if they can't be healed (dead, stoned ...).
bool heal(party::Character& c, int amount);

// ---- Appraising gems and jewellery (the games' tables; gold)
int gem_value(int d100);
int jewel_value(int d100, create::Dice& d);

// A shop's price for an item: its value moved by the shop's price factor
// (script word 0x7F6D: 1 = 1/16 ... 0x10 normal ... 0x80 = x8)
int price(const uint8_t* item, int factor);

} // namespace rules

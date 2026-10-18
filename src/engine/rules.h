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
    uint8_t elf_bonus[8];
    // Secret's weapon part (GAME.OVR 0x38151) gives that +1 to anyone with
    // one of those weapons - its program has no race test there (an
    // original's mistake: applied only with fixes() off)
    bool elf_bonus_any_race = false;
    // Effects in the stats: Strength, the giant strength potion's, Enlarge,
    // Friends, Feeblemind (0: none)
    uint8_t strength_fx = 0, giant_fx = 0, enlarge_fx = 0, friends_fx = 0, feeble_fx = 0;
    uint8_t friends_max = 0;            // Friends' Charisma at most this (Pool 25; 0: no limit)
    // Spiritual Hammer: its effect, the item's type and name words
    uint8_t hammer_fx = 0, hammer_type = 0, hammer_word = 0, hammer_word2 = 0;
    uint8_t hammer_code = 0;            // its byte 0x3E (Curse 0xA0, Pool 0x89; 0: 0xA0)
    // Constitution: the healing effect at 20 and up (0: none), and each
    // class's last level with a hit die (the program's table) - a change of
    // Constitution in use rebuilds the hit points with them
    uint8_t con_regen_fx = 0;
    uint8_t max_hd[8] = {};
    // Berserk (the Berserker sword's effect): given, its handler makes the
    // wearer computer-run (quick) and control 0xB3 (an NPC's 0xB2); taken
    // away, control 0xB3 back to 0 and the party's side
    uint8_t berserk_fx = 0;
    // Secret of the Silver Blades: the +1 to hit with elf_bonus's weapons
    // for every race (the original's; AD&D's elves alone when fixes()),
    // armour's +3 movement only for magic armour, and the Boots of Speed's
    // effect doubling the movement
    bool    light_magic_armour = false, sell_secret = false;
    uint8_t speed_fx = 0;
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
// Strength's damage bonus (for those with the Strength flag 0x125) from a record
int strength_damage(const uint8_t* rec);
// Extra weight carried before slowing down (can be negative)
int max_encumbrance(const party::Character& c);
int dex_ac_bonus(const party::Character& c);

// Works out what the games keep up to date from stats and readied items:
// encumbrance, movement, AC, to-hit, damage dice / bonus, hands in use,
// attack level (into the record, as the games store them).
void recalc(party::Character& c, const items::Names& names, const ItemFacts& f);
// The same without the stats (the original's routine itself, which every
// combatant - monsters too - goes through at a battle's setup: coab's facts,
// the listing's sub_380E0 / sub_66C20)
void recalc_values(party::Character& c, const items::Names& names, const ItemFacts& f);

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
// `gold` from the coins, making change as the games do. round: half a
// gold piece rounds up (Pool's training: (copper + 100) / 200)
int gold_worth(const int money[7], bool round = false);
int gold_worth(const party::Character& c, bool round = false);
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
// Adds an item to a character (false: no room); an item the original got
// wrong comes in fixed when that's wanted (items::fix_item)
bool add_item(party::Character& c, const uint8_t* item);
// Every item of the character made as wanted (items::fix_item; original:
// the original's bytes, for the files) - a readied one put away and readied
// again around the change, so its effect comes or goes. True: any changed
// (recalc after).
bool fix_items(party::Character& c, bool original = false);

// A character's items (Items: Drop, Trade, Halve, Join)
void remove_item(party::Character& c, int i);
// An item that works while readied (its effect byte 0x3E 0x80 and up;
// items::passive says what each code does in this game), readied (`on`)
// or put away: Effect - the effect in 0x3D is theirs while readied (for
// good: no time); Alignment - keyed to an alignment (0x3D & 0x0F): the
// wrong one can't ready it (put away again) and takes 0x3D >> 4 damage;
// OgrePower (Pool) - Strength 18/00 through the strength effect while
// readied ("is stronger" when it's more); NeedGiant (Pool, the boulder) -
// put away again below Strength 19 ("Must have Giant Strength"). The stat
// items work through stats(). False when the item didn't stay readied;
// `says` what to tell the player.
enum class WornSays : uint8_t { Nothing, Stronger, NeedGiant };
bool worn(party::Character& c, int i, bool on, WornSays* says = nullptr);
// Pool of Radiance keeps a character's Strength and Charisma as they stand
// in the record, and in a strength effect's data the Strength it replaced
// (poolrad_combat_facts.md 3.2: the one in force; the others, data + 0x80,
// the Strength they'd give) and in Friends' the Charisma it replaced. The
// engine keeps the character's own values and each effect's gift (as
// Curse's): pool_stats_in turns a Pool character just read (record,
// effects) into that, pool_stats_out the other way for writing - the
// record's Strength 18/xx and Charisma (`pool`, a Pool record from
// party::to_pool) and the effects (`fx`, n_affects x kAffectSize bytes).
void pool_stats_in(party::Character& c, const ItemFacts& f);
void pool_stats_out(const party::Character& c, const ItemFacts& f, uint8_t* pool, uint8_t* fx);
// The game's berserk effect for worn() (0: none; set as the game opens)
void set_berserk_fx(uint8_t type);
// The game's rules for worn(): the data of an item's effect (Curse 0xFF:
// Dispel Magic leaves it; Pool 12), the strength effect Ogre Power gives
// (Pool 0x26; 0: none)
void set_worn_facts(uint8_t effect_data, uint8_t strength_fx);
// Spiritual Hammer (spell_facts.md 0x1C): while they have its effect, the
// hammer is in their items, readied (a new one when it's gone and there's
// room: +1, "Gains an item" - true); without it, the hammer goes
bool keep_hammer(party::Character& c, const items::Names& names, const ItemFacts& f);
// Halve: a pile of n becomes n - n/2 and a new pile of n/2 (not readied);
// false when it can't (one of it, or 16 items already)
bool halve(party::Character& c, int i);
// Join: the same items in other piles go onto pile i (255 a pile at most);
// how many piles were joined in
int join(party::Character& c, int i, bool secret = false);
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
    // Cause Disease's course (combat.h SpellFx): the disease, weakness, sickness; helpless
    uint8_t  ill, weak, sick, helpless;
};
// Removes a character's effects of a type; how many
int remove_affects(party::Character& c, uint8_t type);
// Slow Poison's clock as `minutes` pass outside fights (spell_facts.md
// 0x1A): a hit point every 10 minutes (down to 1); when Slow Poison runs
// out while they're still poisoned, they die ("dies from poison": true).
// Call before the effects' minutes are taken (magic::tick_affects).
bool poison_clock(party::Character& c, int minutes, const CureFacts& f);
// Cause Disease's course as `minutes` pass outside fights (coab's facts,
// listing ovr013:1038 / 10C2): the disease running out starts weakness (60
// minutes) and sickness (10), each renewing itself until cured - weakness:
// Strength in use - 1 ("is weakened"), at 3 helpless; sickness: a hit point
// while over 1, else helpless. What happened (the last of each); call before
// magic::tick_affects, as poison_clock.
struct DiseaseStep {
    int  weakened = 0, hurt = 0;        // how many times
    bool helpless = false;
};
DiseaseStep disease_clock(party::Character& c, int minutes, const CureFacts& f);
// Constitution 20 and up (its effect, given by stats()): a hit point back
// each time 60 minutes run out (curse_finish_facts.md 5.4). How many came
// back ("is fully healed" / "is partially healed"). Call before
// magic::tick_affects, as poison_clock.
int con_regen(party::Character& c, int minutes, const ItemFacts& f);
// Pool's mummy rot (poolrad_combat_facts.md 4) as `minutes` pass outside
// fights: each time the rot's 30 days run out "Rots..." - Charisma 2 less
// (3 at least) and a month less to go (its data's high nibble); with none
// left "Dies rots away" (gone). The disease's 10-day clock starts again
// each time it runs out. Call before magic::tick_affects.
struct RotStep {
    int  rots = 0;
    bool dies = false;
};
RotStep rot_clock(party::Character& c, int minutes, uint8_t rot, uint8_t clock);
// Pool's Manual of Bodily Health (poolrad_combat_facts.md 3.2, handlers
// 0x04 / 0x07) as `minutes` pass: the reading running out "starts to train"
// - 30 days of training (its effect); that running out "is hardier":
// Constitution + 1; at 20 and up the healing every hour (f.con_regen_fx),
// else the original's hit points (ovr13+0233: the hit points over the
// rolled ones shared out by the levels - a fighter's (Con - 15) a level,
// others' 2 over 15, 1 at 15 -, added to the most and the current; none at
// 17 and up for those without fighter levels). Call before
// magic::tick_affects, as poison_clock.
struct ManualStep {
    bool trains = false, hardier = false;
};
ManualStep manual_clock(party::Character& c, int minutes, uint8_t reading, uint8_t training, const ItemFacts& f);
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

// ---- Appraising gems and jewellery (the games' tables; gold). Secret's
// are Curse's halved (div 2: its gems 5 ... 2500, its jewels' ranges and
// bases halved - secret_facts_2.md 17)
int gem_value(int d100, int div = 1);
int jewel_value(int d100, create::Dice& d, int div = 1);

// A shop's price for an item: its value moved by the shop's price factor
// (script word 0x7F6D: 1 = 1/16 ... 0x10 normal ... 0x80 = x8)
int price(const uint8_t* item, int factor);

// The originals' rule mistakes fixed (Tom's Item Fixes switch, CYD_ITEM_FIXES:
// on by default): where a game's program never applies a rule AD&D has (e.g.
// Secret's phase lists leaving an effect out), the engine applies it while
// this is on and does as the original when it is off. Each such place says
// so and is listed in SPEC section 12.
void set_fixes(bool on);
bool fixes();

} // namespace rules

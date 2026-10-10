// Casting spells outside combat (the camp's Cast, Curse first): the spell
// table in the player's program (16 bytes a spell: class, level, range,
// duration, who it is cast on, the effect it gives), how strong a caster
// is, and what each spell does to the party. Which spell does what is a
// per-game table (the profile); the effects are kept in the characters'
// effect lists (.FX) as the games keep them - their workings in fights come
// with combat. Plain C++, host-tested. Rules learned from coab (the Curse
// reimplementation); own code.
#pragma once

#include <cstdint>

#include "classes.h"
#include "create.h"
#include "party.h"
#include "rules.h"

namespace spells {

// Who a spell is cast on (the table's byte 7)
enum Targets : uint8_t { kCombat = 0, kSelf = 1, kMember = 2, kParty = 4 };

// A spell's line in the program's table
struct Entry {
    int cls = 0, level = 0;
    int range = 0, range_level = 0;     // squares: fixed + per caster level
    int lasts = 0, lasts_level = 0;     // minutes: fixed + per caster level
    int targets = kCombat;
    int affect = 0;                     // the effect it gives (0: none)
    int when = 0;                       // 0 camp, 1 combat, 2 both
    int aim = 0;                        // fights (byte 6): low nibble 0 self, 1-4 that many targets,
                                        // 5 a group by size, 8-14 an area (radius & 7), 15 one or an area
    int on_save = 0;                    // a save: 0 no effect on it, 1 nothing, 2 half
    int save = 4;                       // which save (0 poison ... 4 spells)
    int delay = 0;                      // casting time (fights: / 3 = rounds' delay)
    int priority = 0;
};
Entry entry(const classes::Tables& t, int spell);

// The caster's level for the spell's kind (the games' "power": durations,
// some effects' strength)
int power(const party::Character& caster, const classes::Tables& t, int spell);
// How long its effect lasts (minutes)
int lasts(const classes::Tables& t, int spell, int power);
// How far it reaches in a fight (squares; touch spells 1)
int reach(const classes::Tables& t, int spell, int power);

// Effects in a character's list
void add_affect(party::Character& c, int type, int minutes, int data, bool call);
int  find_affect(const party::Character& c, int type);     // index, -1 if none

// What a spell does outside combat (the profile lists it per spell)
enum class Does : uint8_t {
    NotYet,         // its workings aren't in the engine yet (not cast)
    Affect,         // the effect on each target ("is protected")
    Prayer,         // ... with the caster's side in its data
    Mirror,         // ... with 1d4 images in its data
    Haste,          // ... slowed members cured instead; at most `power` members
    Heal,           // n dice of `sides` + plus hit points
    CureBlind, CureDisease, Neutralize, SlowPoison, RemoveCurse, Raise,
    Enlarge,        // Strength by the caster's level, if more than theirs (word; else "is unaffected")
    Reduce,         // a failed save takes Enlarge away (word); else nothing at all
    Friends,        // the caster's Charisma + 2d4 (word)
    Strength,       // + d4 / d6 / d8 by class (no word); fighters over 18: 18/xx
    Hammer,         // Spiritual Hammer: its effect on the caster (word; the caller hands over the hammer:
                    // rules::keep_hammer)
    Dispel,         // Dispel Magic: each of the target's effects (but data 0xFF) by level against level (word)
    Restore,        // Restoration: a lost level back (word; else nothing)
};
struct CampSpell {
    uint8_t  spell;
    Does     does;
    uint8_t  n, sides, plus;            // Heal's dice
    uint32_t word;                      // what's said ("is Blessed"; GAME.OVR offset, 0: nothing;
                                        // Heal: "is Healed" instead of fully / partly healed)
};

// What is said about a target, in order ("NAME is Blessed")
enum class Said : uint8_t { Word, Unaffected, Fully, Partly, Cured, CanSee, Unpoisoned, Raised, Uncursed, ItemUncursed };
struct Line {
    uint8_t who;                        // party member
    Said    what;
};

// The facts the spells need besides the cures' (effect numbers): the
// elves' race (can't be raised), being slowed (Haste cures it), the
// diseases Cure Disease takes away and what goes with each
struct Facts {
    uint8_t elf_race, slow;
    uint8_t disease[3], disease_with[3][2];
    uint8_t enlarge;                    // Enlarge's effect (Reduce takes it away)
    uint8_t hammer;                     // Spiritual Hammer's effect
};
// The Strength Enlarge gives at the caster's level, as an effect's data
// (18/xx: xx + 1; 19 and up: + 100); 0 at 12th level and above (18)
int enlarge_data(int level);

// Can the caster cast now ("is in no condition to cast any spells")
bool can_cast(const party::Character& caster);

// Casts `cs` (the spell taken out of the caster's memory) by party member
// `caster` on `target` (a member; for self / party spells it isn't used).
// Fills `out` with what's said; how many lines. `pw`: the caster's level
// for it (0: their own - an item's spell has its own, item_power).
int cast(party::Party& p, int caster, int target, const CampSpell& cs, const classes::Tables& t,
         const rules::CureFacts& cures, const Facts& f, create::Dice& d, Line* out, int cap, int pw = 0);

// The caster's level for a spell from an item: 6 (the monsters' spells -
// the items' own - as the caster's: 12 for those with spell levels)
int item_power(const party::Character& user, const classes::Tables& t, int spell);

// ---- Fix (the camp's): the party's healers heal everyone, resting between
// (the games: the cure spells in memory, then those memorized again over a
// rest of the time they need)
struct FixPlan {
    int minutes = 0;                    // the rest
    int heal = 0;                       // hit points to share out after it
};
int hp_lost(const party::Party& p);
// The rest and the healing (dice rolled now) for the cure spells (the camp
// table's Heal spells) held and memorized by the party's healers (okay)
FixPlan fix_plan(const party::Party& p, const classes::Tables& t, const CampSpell* camp, int n_camp, create::Dice& d);
// Shares `heal` out in party order, each up to their most; what's left
int fix_heal(party::Party& p, int heal);

} // namespace spells

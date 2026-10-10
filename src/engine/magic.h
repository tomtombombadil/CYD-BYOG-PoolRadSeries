// Spells in a character's memory, memorizing them, scribing scrolls and
// resting (Curse first): the list in the character record, how many of
// each level a character may hold, scrolls' spells copied into the spell
// book, the time a rest needs, and the rest itself, five minutes a step
// (healing, spells scribed and memorized one after another, effects
// running out). Plain C++, host-tested. Rules learned from coab (the Curse
// reimplementation); own code.
#pragma once

#include <cstdint>

#include "classes.h"
#include "items.h"
#include "party.h"

namespace magic {

constexpr int kListAt = 0x1E;       // the memorized list: 84 spell numbers (+ 0x80 while being memorized)
constexpr int kListSize = 84;
constexpr int kToLearnAt = 0x72;    // hours of rest before the first spell comes

enum SpellClass { Cleric = 0, Druid = 1, MagicUser = 2 };

// Whether the character can use spells of this one (the games' rule:
// clerics Wis 9+, druid spells for rangers past 6th level, magic-users'
// Int 9+)
bool can_use(const party::Character& c, const classes::Tables& t, int spell);

// Spells in the list: kind and level counted (learnt and being learnt)
int held(const party::Character& c, const classes::Tables& t, int cls, int level);
// How many more of that kind and level they may memorize
int room(const party::Character& c, const classes::Tables& t, int cls, int level);
bool any_room(const party::Character& c, const classes::Tables& t);

// The spells in memory (learning = false) or being memorized (true), by
// level then number; how many
int in_memory(const party::Character& c, const classes::Tables& t, bool learning, uint8_t* ids, int cap);
// The spells they know and can use (the spell book), by level then number
int known(const party::Character& c, const classes::Tables& t, uint8_t* ids, int cap);

// A new spell to learn (training a magic-user): the spells of levels
// they have slots for, that they can use and don't know yet
int learnable(const party::Character& c, const classes::Tables& t, uint8_t* ids, int cap);
void learn(party::Character& c, int spell);

// Starts memorizing a spell (false: no room)
bool add(party::Character& c, const classes::Tables& t, int spell);
// Forgets the spells being memorized
void cancel(party::Character& c);
bool memorizing(const party::Character& c);
// Takes a memorized spell out (cast); false if it isn't there
bool remove(party::Character& c, int spell);

// ---- Scrolls: up to three spells in an item's effect bytes (0x3C-0x3E;
// + 0x80 while being scribed); the item's second name word counts them
// ("With 1 Spell" ... - the scroll is used up below the first)
constexpr int kScrollAt = 0x3C;
struct Scrolls {
    const items::Names* names = nullptr;    // the item types (slots 11-13 are scrolls)
    uint8_t one_spell = 0;                  // the word "With 1 Spell"
    uint8_t read_magic = 0;                 // Read Magic's effect: scrolls can be read
};
bool is_scroll(const Scrolls& sc, const uint8_t* item);
// The spells on their scrolls they can read (identified, Read Magic on
// them, or a cleric with a clerics' scroll - which makes them known), or
// only those being scribed; by level then number
int scroll_spells(party::Character& c, const classes::Tables& t, const Scrolls& sc, bool scribing, uint8_t* ids,
                  int cap);
enum class Scribe : uint8_t { Ok, Known, Already, Cannot };
// Marks a scroll's spell for scribing (the first scroll holding it)
Scribe scribe(party::Character& c, const classes::Tables& t, const Scrolls& sc, int spell);
bool scribing(const party::Character& c, const Scrolls& sc);
void cancel_scribes(party::Character& c, const Scrolls& sc);
// The spell scribed: into the spell book, off the scroll (used up: gone)
void scribed(party::Character& c, const Scrolls& sc, int item, int slot);

// ---- Using items (Use on the items screen; fights too)
// A magic item that casts a spell: not a scroll, its spell in the second
// effect byte (0x3D, & 0x7F), the third under 0x80 (0x80 and up: it works
// while readied instead); the first (0x3C) its charges, 0: it never runs out
bool usable(const Scrolls& sc, const uint8_t* item);
inline int item_spell(const uint8_t* item) { return item[0x3D] & 0x7F; }
// A use taken off the item: one of the stack (count 0x39 over 1), else a
// charge; true when it's used up
bool use_charge(uint8_t* item);
// ... and off a character's item (used up: gone)
void used(party::Character& c, int item);
// The spells they can read on one scroll (as scroll_spells), by level
int scroll_list(party::Character& c, const classes::Tables& t, const Scrolls& sc, int item, uint8_t* ids, int cap);
// Can they read a scroll's spell: clerics and magic-users (either kind of
// scroll), thieves of 10th level and up 3 times in 4 (else "oops!")
bool reads_scroll(const party::Character& c, int d100);
// A spell read off a scroll: gone from it (the last line holding it); the
// scroll is used up below "With 1 Spell"
void scroll_used(party::Character& c, const Scrolls& sc, int item, int spell);

// The rest needed for what's being memorized and scribed (minutes): an
// hour's start (4 hours, 6 for spells past 2nd level) and 15 minutes a
// spell level; sets the record's hours-before-the-first-spell
int rest_minutes(party::Character& c, const classes::Tables& t, const Scrolls& sc = Scrolls{});

// ---- A rest, five minutes a step
struct Rest {
    int  steps = 0;                 // since the last healing (a day: 288 steps)
    int  hour = 0;                  // steps in this hour (12)
    int  wait[party::kMaxParty] = {};   // steps until the next spell
};

// What one step did
struct Step {
    bool healed = false;            // a day's rest: everyone 1 HP
    int  learnt[party::kMaxParty];  // a spell memorized this step (0: none)
    int  scribed[party::kMaxParty]; // a spell scribed this step (0: none)
};

// Starts a rest (the waits cleared)
void begin(Rest& r);
// Five minutes of rest for the party
// (`tick`: the effects' 5 minutes taken here - false: the caller's own clock
// takes them, with the repeating effects: poison, Constitution's healing)
Step step(Rest& r, party::Party& p, const classes::Tables& t, const Scrolls& sc = Scrolls{}, bool tick = true);

// Effects running out: `minutes` off each timed one (those at 0 last);
// how many ended
int tick_affects(party::Character& c, int minutes);

} // namespace magic

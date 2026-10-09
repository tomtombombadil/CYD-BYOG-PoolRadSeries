// Spells in a character's memory, memorizing them and resting (Curse
// first): the list in the character record, how many of each level a
// character may hold, the time a rest needs, and the rest itself, five
// minutes a step (healing, spells memorized one after another, effects
// running out). Plain C++, host-tested. Rules learned from coab (the Curse
// reimplementation); own code.
#pragma once

#include <cstdint>

#include "classes.h"
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

// Starts memorizing a spell (false: no room)
bool add(party::Character& c, const classes::Tables& t, int spell);
// Forgets the spells being memorized
void cancel(party::Character& c);
bool memorizing(const party::Character& c);
// Takes a memorized spell out (cast); false if it isn't there
bool remove(party::Character& c, int spell);

// The rest needed for what's being memorized (minutes): an hour's start
// (4 hours, 6 for spells past 2nd level) and 15 minutes a spell level;
// sets the record's hours-before-the-first-spell
int rest_minutes(party::Character& c, const classes::Tables& t);

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
};

// Starts a rest (the waits cleared)
void begin(Rest& r);
// Five minutes of rest for the party
Step step(Rest& r, party::Party& p, const classes::Tables& t);

// Effects running out: `minutes` off each timed one (those at 0 last);
// how many ended
int tick_affects(party::Character& c, int minutes);

} // namespace magic

// Making a new character (Create New Character) and training them, as the
// games do it (Curse first): the choices a race / class allows, rolling
// the stats, hit points, the first spells, and "silent" training to the
// level the starting experience buys. Plain C++, host-tested.
//
// Rules learned from coab (the Curse reimplementation) and, where checked,
// from the player's own program: the age effects on stats are the
// program's (coab has them a bracket off), the tables come from the
// program (classes::Tables). Own code.
#pragma once

#include <cstdint>

#include "classes.h"
#include "party.h"

namespace create {

// A dice roller (the games' "roll n dice of s sides"); tests pass their own
class Dice {
public:
    explicit Dice(uint32_t seed = 0x2545F491u) : s_(seed ? seed : 1) {}
    int roll(int sides, int count);
    int random(int n);                  // 0 .. n-1
private:
    uint32_t next();
    uint32_t s_;
};

// The class ids (0-7 single classes, 8-16 multi-classes), as the games number them
enum ClassId {
    Cleric = 0, Druid, Fighter, Paladin, Ranger, MagicUser, Thief, Monk,
    ClericFighter, ClericFighterMU, ClericRanger, ClericMU, ClericThief, FighterMU, FighterThief,
    FighterMUThief, MUThief
};

// The races a new character can be (Curse: dwarf, elf, gnome, half-elf,
// halfling, human - half-orcs aren't offered)
int races(int* out, int cap);
// The classes a race allows, the alignments a class allows (from the tables)
int classes_for(const classes::Tables& t, int race, int* out, int cap);
int alignments_for(const classes::Tables& t, int cls, int* out, int cap);

// Per-game facts that aren't tables (affect ids, spell ids, starting
// values) - from the profile
struct Facts {
    uint8_t  icon_colours[6];          // the default combat icon colours
    uint8_t  hp_dice[8], hp_count[8];  // a class's hit dice: sides, dice at level 1
    // effects given at creation (affect types)
    uint8_t  con_save, dwarf_orc, giants, gnome_giant, gnome_extra, elf_sleep, halfelf, prot_evil, ranger_giant;
    // spells: a new magic-user's four, and those silent training teaches at levels 2-5
    uint8_t  mu_first[4], mu_level2, mu_level3[2], mu_level4, mu_level5;
};

// Starts a character: race, sex, class, alignment chosen. Sets the
// record's defaults, the race's and class's effects, experience, levels,
// THAC0 / saves / thief skills for level 1, the age. Then roll().
void begin(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int race, int sex, int cls,
           int alignment);

// Rolls the stats (best of six 3d6 + 1 each, the age's effects, the race's
// and sex's limits, the class's minimums, exceptional strength), hit
// points, first spells, 300 platinum, then trains silently to the level
// the experience buys. Call again to reroll.
void roll(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d);

// Sets the name (15 characters at most).
void set_name(party::Character& c, const char* name);

// Hit points: the constitution adjustment (per class with hit dice left)
int con_hp_adj(const party::Character& c, const classes::Tables& t);

// One round of training: every class with the experience for its next
// level (and no race limit) goes up one; the class bonuses, hit points and
// (silent training) a magic-user's spells follow. False when nothing
// could be trained.
bool train(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, bool silent);

// The classes a character has the experience for (and no race limit), as
// class mask bits (the tables' class masks; a training hall's 0x7EA8 uses
// the same bits)
int trainable(const party::Character& c, const classes::Tables& t);
// Trains the classes in mask one level up: class bonuses, hit points
// (silent: a magic-user's spells, as at creation)
void train_classes(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int mask, bool silent);

} // namespace create

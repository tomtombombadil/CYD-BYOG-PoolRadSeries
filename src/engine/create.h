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
#ifdef CYD_TEST_HOOKS
// Tests only (never in the firmware): when set, every die asks it first -
// n sides; a value 0 .. n-1 is the die's result, -1 rolls as usual
extern int (*g_die_hook)(int n);
#endif

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
int races(int* out, int cap, bool secret = false);
// The classes a race allows, the alignments a class allows (from the tables)
int classes_for(const classes::Tables& t, int race, int* out, int cap);
// (Champions: its class masks and the god's - god_mask, GAME.OVR 0x21371)
int alignments_for(const classes::Tables& t, int cls, int* out, int cap, int god_mask = 0xFF);
// The race the tables' rows go by (Champions: classes::kKrynnRace + its own)
int race_code(const party::Character& c, const classes::Tables& t);

// Per-game facts that aren't tables (affect ids, spell ids, starting
// values) - from the profile
struct Facts {
    uint8_t  icon_colours[6];          // the default combat icon colours
    uint8_t  hp_dice[8], hp_count[8];  // a class's hit dice: sides, dice at level 1
    // effects given at creation (affect types)
    uint8_t  con_save, dwarf_orc, giants, gnome_giant, gnome_extra, elf_sleep, halfelf, prot_evil, ranger_giant;
    // spells: a new magic-user's four, and those silent training teaches at levels 2-5
    uint8_t  mu_first[4], mu_level2, mu_level3[2], mu_level4, mu_level5;
    uint8_t  mu_change[4];              // a human turned magic-user learns these
    uint8_t  poison_save;               // Pool: dwarves and halflings get this effect too (0: none)
    // Secret: silent training's spells by magic-user level, from level
    // mu_by_level_from (n levels, up to 8 spells each, 0 ends)
    const uint8_t (*mu_by_level)[8] = nullptr;
    uint8_t  mu_by_level_from = 0, mu_by_level_n = 0;
    // Darkness (GAME.OVR 0x162CE): a magic-user's book by its slots - every
    // magic-user spell of levels 1 .. mu_all_levels, the lists below for
    // slots of levels mu_by_slot_from on (up to 8 spells each, 0 ends), and
    // every magic-user spell with slots of level mu_all_slot
    const uint8_t (*mu_by_slot)[8] = nullptr;
    uint8_t  mu_by_slot_from = 0, mu_by_slot_n = 0, mu_all_levels = 0, mu_all_slot = 0;
    // Gateway (its createPlayer, GAME.OVR 0x2650E +0xF63): a new character's
    // experience - this for one class, shared evenly by two or three - and
    // platinum (0: the game's own - Curse's 25,000 and 300)
    uint32_t start_xp = 0;
    uint16_t platinum = 0;
    // Champions: a kender's second effect (fearless) and a new cleric's
    // spells (DS: a list from its second byte, 0 ends)
    uint8_t  kender_fear = 0;
    uint8_t  cleric_first[10] = {};
};

// Starts a character: race, sex, class, alignment chosen. Sets the
// record's defaults, the race's and class's effects, experience, levels,
// THAC0 / saves / thief skills for level 1, the age. Then roll().
// (Champions: god = the cleric's god, 1-7, 0 for none - Pick God)
void begin(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int race, int sex, int cls,
           int alignment, int god = 0);
// Champions' gods a new cleric of this race may pick (1-7; dwarves Reorx
// only - GAME.OVR 0x211B2) and the alignments the god allows (bit i: the
// i-th of the six - 0x21371)
int gods_for(const classes::Tables& t, int race, int* out, int cap);
int god_alignments(int god);

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

// Restoration (spell_facts.md 0x38): a lost level back - the hit points
// lost with the levels (record 0xE8) shared by them (0xE7) back to the
// maximum, current and rolled hit points; the class with the lowest level
// (then the least experience to its next level) that may go up rises one,
// its experience raised to that level's when below it. False: no level lost.
bool restore(party::Character& c, const classes::Tables& t);

// The classes a character has the experience for (and no race limit), as
// class mask bits (the tables' class masks; a training hall's 0x7EA8 uses
// the same bits)
int trainable(const party::Character& c, const classes::Tables& t);
// A training hall's choice (the original's train_player, not silent): of
// the classes ready, the one whose experience figure is the largest - the
// original compares them all at the level of the LAST class held - (all of
// them when none has a figure there); and the experience the character
// keeps: one under the figure for the level after the next when they have
// that much already (AD&D: one level at a time). The class mask (0: none
// ready); *exp_after: the experience to keep.
int train_pick(const party::Character& c, const classes::Tables& t, uint32_t* exp_after);
// Trains the classes in mask one level up: class bonuses, hit points
// (silent: a magic-user's spells, as at creation)
void train_classes(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int mask, bool silent);

// ---- Pool of Radiance characters (Add Character -> Pool; facts: the
// Project's claude/import_facts.md 3). A Pool record is 285 bytes; its
// control byte (0x84) under 0x80 is a player character.
constexpr int kPoolRecordSize = 0x11D;
// The name in a Pool record (a Pascal string, 15 at most)
void pool_name(const uint8_t* pool, char* out, size_t cap);
bool pool_is_pc(const uint8_t* pool);
// A Curse character from a Pool record, as the original converts it: the
// fields it reads, the stats held to the race's and sex's limits, Animate
// Dead taken out of the spell book, exactly 300 platinum (Pool's money
// dropped), no items, the class values recomputed. Levels and experience
// are kept as they are.
void from_pool(const uint8_t* pool, party::Character& c, const classes::Tables& t);
// The racial effects a Pool character's .SPC keeps (the rest are dropped)
bool pool_effect_kept(uint8_t type);

// ---- Human Change (the party menu, where training is offered; coab's
// facts): a human with no former class may take up another
bool can_change(const party::Character& c);
// The classes they may change to: the race's, not the present one; every
// stat the present class needs (a minimum of 9 or more) at 15 or more,
// every one the new class needs at 17 or more; the new class allows their
// alignment
int change_classes(const party::Character& c, const classes::Tables& t, int* out, int cap);
// Changes to class `cls` at level 1: experience 0, the present class's
// level kept as the former one, the hit dice count kept (record 0xE6),
// spell counts and the memorized list cleared (a cleric: one first-level
// spell; a magic-user: the profile's four spells), the class values
// recomputed. (Items the new class can't use: the caller unreadies them.)
void change_class(party::Character& c, const classes::Tables& t, const Facts& f, int cls);

// ---- Modify Character (the games' rules, facts from coab): a character
// who hasn't adventured (experience as made: 0, 8333, 12500 or 25000) and
// has no former class
// Pool (pool = true; poolrad_create_facts.md 5): experience 0 only.
// Darkness (dk; GAME.OVR 0x1675F): experience exactly 1,500,001, 750,000 or
// 500,000 (its former class not looked at).
// (krynn: Champions' - its starting experience figures, GAME.OVR 0x22B1A)
bool can_modify(const party::Character& c, bool pool = false, bool secret = false, bool dk = false, bool krynn = false);
// A stat (0 Str ... 5 Cha) a step up (dir > 0) or down: within the race's
// and sex's limits, down no lower than the class's minimums; a fighter,
// ranger or paladin at Strength 18 goes on into 18/01 ... 18/00 (and back
// down through it first). Constitution keeps the hit points within their
// bounds (hp_least / hp_most).
// Pool (t.lay.pool): down below the race's OR the class's minimum puts it
// at the race's minimum (its program's way - so a class minimum above the
// race's can be passed), and no multi-class cleric Wisdom 13.
void modify_stat(party::Character& c, const classes::Tables& t, const Facts& f, int stat, int dir);
// The hit points a step up or down, within their bounds
void modify_hp(party::Character& c, const classes::Tables& t, const Facts& f, int dir);
// The bounds for the levels held: the least (a point a hit die, with the
// Constitution adjustment) and the most (every die at its highest, with
// each class's Constitution bonus), averaged over the classes
int hp_least(const party::Character& c, const classes::Tables& t, const Facts& f);
int hp_most(const party::Character& c, const classes::Tables& t, const Facts& f);
// Keep: the class values follow the stats (spell slots, thief skills ...),
// and the "rolled" hit points (record 0x12C) are what's left after the
// Constitution bonus. Pool: its "modified" mark (Pool record 0x85 = 1).
void modify_done(party::Character& c, const classes::Tables& t, const Facts& f);

// ---- Pools of Darkness (the Project's claude/darkness_create_facts.md)
// The magic-user's book as its creation's save and Modify's Keep make it
// (GAME.OVR 0x165C6 then 0x162CE): its magic-user spells taken out, then
// given again by the slots it has (Facts::mu_by_slot ...)
void dk_mu_book(party::Character& c, const classes::Tables& t, const Facts& f);
// The starting kit's kind (GAME.OVR 0x19558): 1 fighter / paladin / ranger,
// 2 thief, 3 cleric, 4 magic-user (the first that fits, in that order)
int dk_kit_kind(const party::Character& c);
// The kit: up to 6 items (`ids`: the kind's item numbers, 0 none; `recs`:
// their 17-byte ITEM0 rows, type 0xFF none) - each identified, readied but
// the 4th, 50 of the fighters' 5th and the magic-users' 4th; the list in
// the original's order (the last given first)
void dk_kit(party::Character& c, int kind, const uint8_t ids[6], const uint8_t recs[6][17]);
// Hit points as the original's Constitution recalculation sets them (GAME.OVR
// 0xE2:0x89, stat 4 - 0x3307E): the rolled figure (record 0x12C) plus each
// class's Constitution bonus (fighters / paladins / rangers 15-19 Con - 14,
// 20 5, 21-23 6, 24-25 7; the others 15 1, 16+ 2; no penalties) times its
// levels (to one under its last hit die; rangers one more; a human's
// classes from their former level on), over the classes held
int dk_con_hp(const party::Character& c, const classes::Tables& t);
// That recalculation: the maximum set so, the hit points now moved by as much
void dk_con_recompute(party::Character& c, const classes::Tables& t);

// Darkness's level drain (GAME.OVR 0x1CF3B): n levels off, each from the
// class with the highest level (ties: the one whose level started at the
// least experience), the best levels / experience / HP remembered (ext),
// the experience halfway into the level below, that level's hit points
// taken (its roll, while the level is past a human's former one). Dies: no
// level left or HP max 0; Collapses: HP 0. The class values are the
// caller's to work out again.
enum class DrainResult : uint8_t { Lives, Collapses, Dies };
DrainResult dk_drain(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int n);
// The experience a Darkness level starts at (ovr 0x39349; 41+: 0x7FFFFFFF)
int32_t dk_level_exp(const classes::Tables& t, int cls, int level);

// Darkness's Restoration (GAME.OVR 0x2EBEC): a drained level back - the
// class whose next level starts at the least experience, its share of the
// best hit points, the experience (the best one when one level was lost,
// else that level's start), the class values, "able to train"
void dk_restore(party::Character& c, const classes::Tables& t);

} // namespace create

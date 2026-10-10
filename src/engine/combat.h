// Combat (Curse first): the battlefield, who stands where, and the rules of
// a fight - initiative, moving, attacking, damage, dying, the monsters'
// choices, and the experience at the end.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// Rules and layouts learned from coab (the Curse reimplementation; facts
// only, own code). The tables the rules use (the battlefield's ground
// pieces, the placement shapes, the outdoor terrain, the turn-undead table)
// are read from the player's own program through the profile.
//
// The battlefield is 50 x 25 squares of 24 x 24 pixels; a square holds a
// ground value (an index into the ground table: move cost, eye height,
// obstacle height, picture; 0 = off the field). Indoors it is drawn from
// the 3D map around the party (a slanted picture of it: a map square
// (dx, dy) from the party is a block of squares at (21 + 6dx + 5dy,
// 10 + 5dy)); outdoors it is a random field (streams, trees, rocks) by the
// place's terrain flags. Directions: 0 N, 1 NE, 2 E ... 7 NW (x right,
// y down), 8 none.
#pragma once

#include <cstddef>
#include <cstdint>

#include "classes.h"
#include "create.h"
#include "geo.h"
#include "items.h"
#include "party.h"
#include "spells.h"

namespace combat {

constexpr int kW = 50, kH = 25;
constexpr int kMaxMonsters = 63;
constexpr int kMaxFighters = party::kMaxParty + kMaxMonsters;
constexpr int kGroundValues = 0x43;
constexpr int kMaxGroups = 8;           // monster kinds a fight (icon slots 8 ...)
constexpr int kGroupItems = 8;
constexpr int kMonsterAffects = 8;

int dx(int dir);
int dy(int dir);

// ---- The program's tables (Curse: START.EXE's data segment)
struct Tables {
    uint8_t ground[kGroundValues][4];   // move cost (0xFF: can't), eye height, obstacle height, picture
    uint8_t fallback[4][4];             // [team facing][attempt 1-3]: dungeon facing to move the start
    uint8_t formation[4][4];            // [team facing][attempt]: the formation's facing (dungeon facing)
    uint8_t facing[4];                  // dungeon half-facing -> battlefield direction (N->NW ...)
    uint8_t start_x[8], start_y[8];     // a rank's centre in the 11 x 6 grid
    uint8_t shapes[5][6][2];            // allowed columns (min, max) a row; [4] = the fallback shape
    uint8_t terrain[33];                // outdoors: terrain flags a place (city)
    uint8_t turn[13 * 10 + 11];         // turn undead: [undead type * 10 + column]
};
// Where they are (data segment offsets), from the profile
struct TableAt {
    uint16_t ground, fallback, formation, facing, start_x, start_y, shapes, terrain, turn;
};
// Reads them: read(ctx, ds offset, out, n) gives the data segment's bytes
using ReadDs = bool (*)(void* ctx, uint16_t ds, uint8_t* out, size_t n);
bool read_tables(Tables& t, const TableAt& at, ReadDs read, void* ctx);

// The effects the fights' rules look at (per game, from the profile)
struct Facts {
    uint8_t held[4];                    // can't act, slain by any blow: snake charm, paralysed, asleep, helpless
    uint8_t bless, curse, prayer, haste, slow, invisible, prot_evil, prot_good, mirror;
    uint8_t backstab_weapons[6];        // the weapons a thief backstabs with (or none)
    // How a shot looks in flight, by the item type that flies: pointed (an
    // arrow's picture by direction), spinning, a flask, a sling's stone; the
    // rest a rock
    uint8_t shot_pointed[6], shot_spinning[3], shot_flask[2], shot_sling[3];
};

// ---- Monsters (LOAD MONSTER): a group per load (its items and icon), a
// record and effects per monster
struct Group {
    uint8_t id = 0, icon = 0;           // MON block, CPIC block
    uint8_t n_items = 0;
    uint8_t item[kGroupItems][items::kRecordSize] = {};
};
struct Monster {
    uint8_t rec[party::kRecordSize] = {};
    uint8_t aff[kMonsterAffects][party::kAffectSize] = {};
    int     n_aff = 0;
    uint8_t group = 0;
};

// ---- A fighter: a party member or a monster
struct Fighter {
    uint8_t* rec = nullptr;
    uint8_t (*aff)[party::kAffectSize] = nullptr;
    int*    n_aff = nullptr;
    int     max_aff = 0;
    uint8_t (*items)[items::kRecordSize] = nullptr;    // its items (a monster: its group's)
    int     n_items = 0;
    int     member = -1;                // party index, or -1 (a monster)
    int     monster = -1;               // index in the monsters, or -1
    int     icon = 0;                   // icon slot: party 0-7, monsters 8 + group
    int     x = 0, y = 0, size = 0;     // top-left square, footprint (0: not on the field)
    int     facing = 0;
    int     delay = 0;                  // initiative left (0: done this round)
    int     moves = 0;                  // half squares left
    int     attacks[2] = {};            // left this round (slot 1, slot 2)
    int     bleeding = 0;
    int     received = 0, turns = 0;    // attacks received this round, facing changes
    int     target = -1;
    int     ground = 0;                 // the square's ground before a body was left there
    bool    guarding = false, quick = false, attacked = false, turned_undead = false, can_cast = true;
    bool    swept = false;              // swept this round
    bool    gone = false;               // a monster there was no room for: not in the fight at all
    bool    fleeing = false;            // turned undead, panic: runs for the field's edge
    int     spell = 0;                  // a spell being cast (it goes off at its delay)
    int     spell_n = 0;                // the computer's targets for it
    uint8_t spell_t[24] = {};
    int     team() const { return rec[0x197]; }
    bool    up() const { return rec[0x196] != 0; }          // able to fight
    int     hp() const { return rec[0x1A4]; }
    int     hp_max() const { return rec[0x78]; }
    int     status() const { return rec[0x195]; }
    bool    has(uint8_t type) const;
};

enum Result : uint8_t { Won = 0, Lost = 0x80, Fled = 0x81 };

struct Battle {
    uint8_t ground[kH][kW] = {};
    uint8_t who[kH][kW] = {};           // fighter + 1 on that square (0: none)
    Fighter f[kMaxFighters];
    int     n = 0;
    int     party_size = 0;
    int     round = 0, no_action = 15;
    int     surprise = 0;               // bit 1: our side, bit 2: the enemies
    int     enemy_health = 100;         // %
    int     vx = 0, vy = 0;             // the view's top-left square (7 x 7 shown)
    bool    indoors = true;
    int     to_hit_party = 0, to_hit_monsters = 0;  // the scripts' bonuses
    const Facts* fx = nullptr;
    int     morale_base = 0;            // the script's morale word (0x7EC6)
    // Missiles: the item types (ITEMS) and the ammunition's types
    const items::Names* names = nullptr;
    uint8_t arrow = 0, quarrel = 0;
};

// Effects on a fighter
bool helpless(const Battle& b, const Fighter& f);
// Timed effects a round on (a minute); those run out go
void tick(Battle& b);

// The footprint's squares: size 1 one, 2 one wide two tall, 3 two wide
// one tall, 4 two by two
int squares(int size, int* sx, int* sy);

// Builds the field
void build_indoors(Battle& b, const Tables& t, const geo::Map& m, int px, int py, create::Dice& d);
void build_outdoors(Battle& b, const Tables& t, int city, create::Dice& d);
// The square's ground entry (0 when off the field)
const uint8_t* tile(const Battle& b, const Tables& t, int x, int y);

// Adds the fighters (party first, then the monsters) and places them:
// the party facing `facing` (dungeon 0, 2, 4, 6) at its square, the
// enemies `distance` map squares ahead facing back. side(dir) = whether
// the start square's side that way is a solid wall (fallbacks), or null
// outdoors. Monsters that can't be placed are left off the field (their
// size 0) and out of the fight; downed party members leave a body.
using SolidSide = bool (*)(void* ctx, int dir);
void place(Battle& b, const Tables& t, int facing, int distance, SolidSide solid, void* ctx);
void occupancy(Battle& b);

// Distance and sight: the path from fighter a to square (x, y) (2 a
// straight step, 3 a diagonal one); false when something on the way is
// higher than the line of sight (unless ignore_walls). Range in squares =
// path / 2.
bool path(const Battle& b, const Tables& t, int a, int x, int y, bool ignore_walls, int* length);
// Between two fighters: the nearest pair of their squares
bool range(const Battle& b, const Tables& t, int a, int c, bool ignore_walls, int* squares);
int  direction(int fx, int fy, int tx, int ty);     // 0-7 toward, 8 here
bool adjacent(const Battle& b, int a, int c);

// What a step costs (half squares; diagonal 1.5): the highest move cost of
// the squares the footprint would cover; 0xFF when it can't go there (a
// wall, the field's edge: *edge set, someone else there: *blocker set)
int step_cost(const Battle& b, const Tables& t, int i, int dir, bool* edge, int* blocker);
void step(Battle& b, int i, int dir);

// ---- A round
// Dex reaction adjustment (initiative, missiles)
int dex_reaction(int dex);
// Initiative for everyone (d6 + Dex reaction, the surprised side -6),
// moves (movement x 2) and attacks for the round
void start_round(Battle& b, create::Dice& d);
// The next to act (highest delay; ties by d100), -1 when the round is over
int next(Battle& b, create::Dice& d);
// Attacks this round: half attacks (record 0x11C slot 1, 0x11D slot 2) by
// round (3 half attacks = 1, 2, 1, 2 ...)
int attacks_this_round(int half, int round);

// ---- Attacking
struct Hit {
    bool hit = false;
    int  damage = 0;
};
struct Attack {
    Hit  hits[8];
    int  n = 0;
    bool any = false;
    bool behind = false;
    bool down = false;                  // the target went down
    bool slain = false;                 // a helpless target: "slays helpless ... with one cruel blow"
    bool backstab = false;              // "-Backstabs-"
};
// A backstab: a thief (backstabbing weapon or none) straight behind a man-
// sized target that has had an attack this round already: the rear AC 4
// worse, damage x ((thief level - 1) / 4 + 2)
bool can_backstab(const Battle& b, int a, int c, const items::Names* names);
// A free attack on one stepping away (leaving enemy e's side): e isn't held,
// sees them, and either hasn't acted this round, hasn't been attacked this
// round, or has them in its front (its facing +- 2)
bool free_attack_ok(const Battle& b, const Tables& t, int e, int mover);
// A fighter's sweep (fighters: their level, once a round): fewer attacks
// left than the level, the target under 1 Hit Die and next to them, more
// such enemies next to them than attacks left. The ones swept (the target
// first, up to the level); 0: no sweep.
int sweep(const Battle& b, int a, int target, int* out, int cap);
// Attacks fighter c with fighter a's attacks left (slot 2 then slot 1), as
// the games do: to-hit d20 (1 misses, 20 hits) + to-hit value + the side's
// bonus >= the target's AC (the rear AC from behind); damage dice + bonus
// (the large dice against large targets), applied at once. The target turns
// to face the attacker (fewer than 2 attacks received).
Attack attack(Battle& b, int a, int c, const items::Names* names, create::Dice& d, bool from_behind = false);
// Damage: HP 0 unconscious, -1..-9 dying (bleeding), -10 dead (HP kept 0
// when down); true when they went down
bool damage(Battle& b, int c, int amount);
enum class Down : uint8_t { No, Unconscious, Dying, Dead };
Down down_state(const Fighter& f);

// End of a round: the clock (a minute, as the caller does), bleeding
// (dying +1 a round, dead past 9); the enemy side's health; whether the
// fight is over (a side gone, or 15 rounds without an attack)
bool end_round(Battle& b);
int  standing(const Battle& b, int team);
bool anyone_dying(const Battle& b);
// Bandage: the first dying party member becomes unconscious (bleeding
// stops); their index or -1
int bandage(Battle& b);

// ---- The computer's turn (monsters, Quick): one decision at a time
enum class Act : uint8_t { Done, Attack, Step, Guard, Flee };
struct Plan {
    Act act = Act::Done;
    int target = -1;
    int dir = 8;
    bool missile = false;               // Attack: a shot / a throw (`ammo` goes)
    int ammo = -1;
};
// The readied missile weapon's reach (its range - 1, at least 1) when it can
// be used now: a bow / crossbow (type flags 1 / 0x80) with its arrows /
// quarrels readied, a thrown weapon (flag 0x10), another with a range (a
// sling); 0: none. `ammo`: the item a shot uses up (one of the pile; -1 none).
int missile(const Battle& b, const Fighter& f, int* ammo);

// ---- Pictures in flight (the combat sprites, COMSPR: 0-2 an arrow up /
// slanted / across, 3 an axe, 4 a flask, 5 a spell, 6 lightning, 7 a rock,
// 8 a sling stone, 9 sparkles, 10 a burst). A flight steps 8 pixels at a
// time (a third of a square), `delay` ms a step, cycling `frames` pictures.
struct Flight {
    uint8_t pic = 0, frames = 0, delay = 0, sound = 0;
    uint8_t slot[4] = {};               // the cycle: bit 0 the attack picture, bit 1 mirrored
};
enum class Shot : uint8_t { Pointed, Spinning, Flask, Sling, Rock };
Shot shot_kind(const Facts& fx, int item_type);
// What a shot by fighter f looks like (the readied sling's stone, else the
// item that flies - `ammo`, or the weapon) flying in direction `dir`, and
// its sound (the games' numbers: 0x0C a whistle, 6 a sling / flask, 9 a whoosh)
Flight shot_flight(const Battle& b, const Fighter& f, int ammo, int dir);
// A spell's flight (picture 5; lightning 6): the 4-picture cycle
Flight spell_flight(int pic, int delay);
// A flight's positions: from square (x0, y0) toward (x1, y1) in 8-pixel
// steps; the n-th position (1 = the first step) in 8-pixel cells, false past
// the last one drawn (a step short of the target; none when they're next to
// each other at most 2 cells apart)
struct FlightPath {
    int x = 0, y = 0;                   // the current cell
    int tx = 0, ty = 0, sx = 0, sy = 0, ax = 0, ay = 0, err = 0;
    int left = 0;                       // positions still to draw
};
void flight_begin(FlightPath& p, int x0, int y0, int x1, int y1);
bool flight_step(FlightPath& p);
// The computer's move: next to an enemy it attacks (its target first); else
// with a missile weapon it shoots at its target in reach and sight (or a
// random one there); else it steps toward its target; else it guards.
Plan think(Battle& b, const Tables& t, int i, create::Dice& d);
// Leaving the fight: gets away (faster than every enemy able to reach, or
// even and d2) - status Running, off the field
bool flee(Battle& b, const Tables& t, int i, create::Dice& d);

// ---- Spells in fights: what each does (the profile's table, per game)
enum class SpellDoes : uint8_t {
    NotYet,                             // its workings aren't in the engine yet (not cast)
    Affect,                             // the effect on its targets ("is protected")
    Ours, Theirs,                       // ... only on the caster's side / the other side (Bless, Curse)
    Heal,                               // dice of hit points
    Damage,                             // dice of damage (touch spells roll to hit; a save as the table says)
    Sleep,                              // 4d4 by Hit Dice: 1 a die to 1, 2, 4, 6, then 10 / 20
    Hold,                               // a save (-2 / -3 one target, -1 two, 0 more) or held
    Mirror, Haste, Prayer,              // effects with their own data
    Bolt,                               // damage along a line from the target away from the caster
    Cloud,                              // a save against poison or helpless 1d4 + 1 rounds (word / word2 saved)
};
struct FightSpell {
    uint8_t   spell;
    SpellDoes does;
    uint8_t   n, sides, plus;           // dice
    uint8_t   per;                      // 1: + the caster's level, 2: (level + 1) / 2 missiles of 1d4 + 1, 3: level dice,
                                        // 5: 3, 5 or 7 dice; effects: the dice are rounds (4: x 10)
    uint8_t   kind;                     // damage: 1 fire, 2 cold, 4 electricity, 8 magic, 0x10 acid
    uint32_t  word;                     // what's said ("is Blessed", "falls asleep"; GAME.OVR, 0: nothing;
                                        // Heal: "is Healed" instead of fully / partly healed)
    uint32_t  word2 = 0;                // ... when they saved (clouds: "starts to cough")
};
// What happened to each target, in order
enum class Did : uint8_t { Word, Damage, Unaffected, Misses, Healed, Down, Word2 };
struct SpellLine {
    uint8_t who;
    Did     did;
    int     amount;
};
// The fighters in an area: within r squares of (x, y)
int in_area(const Battle& b, const Tables& t, int x, int y, int r, int* out, int cap);
bool saving_throw(const Fighter& f, int type, int bonus, create::Dice& d);
// Casts the spell by fighter `caster` on `targets` (chosen as the spell's
// aim says); what it did. The spell left the caster's memory already.
// `pw`: the caster's level for it (0: their own; an item's: item_power).
int cast(Battle& b, const classes::Tables& st, int caster, int spell, const FightSpell& fs, const int* targets,
         int n, create::Dice& d, SpellLine* out, int cap, int pw = 0);
// The caster's level for a spell: by its kind; from an item 6 (the
// monsters' spells - the items' own - as the caster's)
int power_of(const uint8_t* rec, const classes::Tables& st, int spell, bool item = false);

// A lightning bolt's squares: from (tx, ty) on, away from the caster, up to
// `len` squares or a wall / the field's edge; the fighters on them
int bolt_line(const Battle& b, const Tables& t, int caster, int tx, int ty, int len, int* out, int cap);

// The computer's spells (monsters): d7 rounds of 3 random picks from the
// memorized list, from priority 7 down; a spell is taken when its priority
// (table byte 13) reaches the round's, and it has a use - an enemy in its
// reach and sight (an area that would catch a friend failing a test save:
// not), or for the caster's own good (hurt: healing; an effect they lack).
// The spell (0: none) and its targets.
int choose_spell(Battle& b, const Tables& t, const classes::Tables& st, int i, const FightSpell* table, int n_table,
                 create::Dice& d, int* targets, int* n_targets);
const FightSpell* fight_spell(const FightSpell* table, int n, int spell);
// The targets for a spell the computer thinks of casting (as choose_spell):
// false when it has no use now
bool spell_targets(Battle& b, const Tables& t, const classes::Tables& st, int i, const FightSpell& fs, int spell,
                   int pw, create::Dice& d, int* targets, int* n_targets);
// The computer's magic items (before its spells): d7 rounds from priority
// 7 down, the readied items that cast a spell (not scrolls) in order, each
// judged by the spell table's line 0x17 back for spells past 0x38 (the
// games' way); the first with a use. The item (-1: none), its targets.
int choose_item(Battle& b, const Tables& t, const classes::Tables& st, int i, const items::Names& names,
                const FightSpell* table, int n_table, create::Dice& d, int* targets, int* n_targets);

// Morale (monsters, NPCs): (control & 0x7F) x 2 (over 102: 0), Bless +5,
// Curse -5; when it's below the share of their hit points lost, their
// side's health is weighed against 100 - the script's morale (0x7EC6):
// below, they flee (when no enemy is faster) or surrender (Int over 5)
enum class Morale : uint8_t { Fight, Flee, Surrender };
Morale morale(Battle& b, const Tables& t, int i);

// Turn undead: one d20 for the attempt, d12 undead at most, the weakest
// in sight first (record 0xE9: undead type 1-12), the table by the
// cleric's level (1-8, 9-13, 14+): d20 >= |value| turns (positive: it
// flees) or destroys (0 or below; a few more may go). Out: the fighters
// turned (+1000 when destroyed); how many.
int turn_undead(Battle& b, const Tables& t, int cleric, create::Dice& d, int* out, int cap);

// ---- The end
struct Outcome {
    Result result = Won;
    int    exp = 0;                     // each survivor's share
    int    money[7] = {};               // the beaten monsters' coins
};
Outcome finish(Battle& b, const Monster* monsters);
// Experience for the pool's money: gold worth + 250 a gem + 2200 a piece of
// jewellery
int money_exp(const int money[7]);
// Shares `total` among the party members still standing (not animated):
// +10% for a high prime requisite (over 15), multi-classes divided by their
// classes; the share each got before that
int award(Battle& b, int total);
// The readied weapon among the fighter's items (-1: none)
int weapon(const Fighter& f, const items::Names& names);

// ---- The computer's weapon (Quick members, NPCs; the facts' 5.3)
// A weapon's worth: dice x sides + 8 x its plus + 2 x the type's damage
// bonus (when above 0) + 2 x (attacks - 1) for a launcher + 3 one-handed;
// 0 when cursed or when the hands (`hands_used` besides the weapon and
// shield) would pass 3
int weapon_rating(const items::Names& names, const uint8_t* item, int hands_used);
// The weapon fighter i should hold: each weapon its classes can use rated;
// the best missile weapon (a launcher with its arrows / quarrels readied, a
// sling, a thrown weapon) when it rates over half the best melee weapon
// (which must beat the bare hands' dice) and it can be used now (thrown, or
// no enemy next to them); else the best melee weapon (-1: bare hands).
// kKeep: as it is (already held, or the held one is cursed).
constexpr int kKeep = -2;
int choose_weapon(const Battle& b, int i);
// A pure missile weapon (a bow, crossbow, sling: not thrown) held with an
// enemy next to them
bool missile_in_melee(const Battle& b, int i);

} // namespace combat

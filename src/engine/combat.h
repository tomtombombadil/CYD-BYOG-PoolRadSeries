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
constexpr int kMonsterAffects = 12;    // the most in a monster file (9) and those a fight adds

int dx(int dir);
int dy(int dir);

// A bolt's segment (bolt_path): the picture flies from (x0, y0) to (x1, y1)
struct BoltSeg { uint8_t x0, y0, x1, y1; };

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

// The monsters' special abilities (monster_fx_facts.md): their effect types
struct MonFx {
    // After a hit: poison (+0 / -2: a save or killed), the thri-kreen's
    // paralysing bite, the dracolich's paralysing touch and chill (2d8 cold),
    // a fire touch (2d10), the ankheg's acid bite (1d4), engulfing, the owl
    // bear's hug; while a hit's damage is worked out: the salamander's heat
    // (+1d6, not against heat_proof)
    uint8_t poison, poison_m2, kreen_bite, draco_touch, chill, fire_touch, acid_bite, engulf, hug, heat;
    uint8_t heat_proof[3];
    // A weapon's hit: only magic weapons count (none / +0: nothing, +1-2
    // half), +0 weapons do nothing to it unless the attacker is a monster of
    // 4+ Hit Dice, half damage, piercing weapons do 1, a blessed quarrel
    // slays it, missiles from a +0 weapon are dodged, 60% of missiles dodged
    uint8_t stop_normal, need_magic, half, pierce_one, blessed_bane, missile_proof, missile_dodge;
    // Damage (spells, breath, specials): fire, cold, electricity none;
    // electricity heals 8; fire / cold a save for none (else half); fire
    // -1 a die; cold / fire half (+3 on its saves against them); death
    // magic none
    uint8_t no_fire, no_cold, no_elec, absorb_elec, fire_cold, efreet, resist_cold, resist_fire, death_ward;
    // Magic resistance: 50% / 15% (by the caster's level), no magic at all,
    // the minor globe (spells below level 4), the elves' 90% against sleep
    // and charm, no charm or sleep, no paralysis, no charm, sleep, paralysis
    // or poison (and every save against poison / paralysis made)
    uint8_t mr50, mr15, no_magic, globe, elf_sleep, charm_sleep, no_paralyze, mind;
    // The troll: hurt, it starts to regenerate (regen_wait 3 rounds, then
    // regen: 3 hit points a round); fallen to anything but fire or acid it
    // gets up again (troll_up: 3d6 rounds)
    uint8_t regen_hit, regen_wait, regen, troll_death, troll_up;
    // Invisible at the start; seeing the invisible; protection from evil
    // for those next to it
    uint8_t start_invisible, detect, prot_evil_near;
    // The computer's special actions: acid breath, the dracolich's fire
    // breath, the hell hound's fire, thrown lightning, the slug's and the
    // ankheg's acid, the stone gaze (reflected by a mirror when mirror_gaze),
    // the beholder's eye rays
    uint8_t acid_breath, fire_breath, hound_fire, lightning, slug_spit, ankheg_spit, stone_gaze, mirror_gaze, rays;
    // Effects they give: suffocating (engulfed), no moving, engulfing,
    // hugging, paralysed, poisoned, asleep
    uint8_t suffocate, held_fast, engulfing, hugging, paralyzed, poisoned, sleep;
    // The mirror's and the blessed quarrel's name words; the spells the
    // beholder casts when no ray fits (Fear, Slow, Sleep)
    uint8_t mirror_word, blessed_word;
    uint8_t ray_spells[3];
};

// More spells' effects (spell_facts.md 2): Enlarge, Confusion (and going
// berserk in it), Dispel Evil (attacks on the caster by the evil -7; its
// hits dispel them), the fire shields (hot, cold; zap: those hitting it
// from next to it take twice the damage)
// Slow Poison (slow_poison, its hourly poison_damage), Spiritual Hammer,
// Animate Dead (the animated), Constitution 20+'s healing (con_regen: a
// hit point each time its 60 runs out)
struct SpellFx {
    uint8_t enlarge, confuse, berserk, evil_ward, evil_bane, hot, cold, zap;
    uint8_t slow_poison, poison_damage, hammer, animated;
    uint8_t con_regen;
    uint8_t giant;                      // the giant strength potion's (its data: the Strength)
    uint8_t shield;                     // Shield: AC 3 at worst, saves +1, Magic Missile stopped
    uint8_t enfeeble;                   // Ray of Enfeeblement: its blows do damage - damage / 4
};

// The effects magic items give while readied (item 0x3E = 0x80: the effect
// in 0x3D; claude/behaviour_facts.md items, coab's facts): the Flame Tongue's
// bonus by the target's monster type (troll +1, types 9 / 12 +2, the
// animated dead +3), the Dragon Slayer's (dragons: +2 to hit, damage 3 x
// d12 + 4 + Strength's), the Frost Brand's (fire creatures +3), the Cloak of
// Displacement (the first attack on its wearer each fight misses), the Ring
// of Invisibility (invisible at a fight's start and at each round's end);
// the Robe of Vermin's effect - with it, or the Dragon Slayer's, no free
// attack on one stepping away (listing ovr014:0B37-0B68)
struct ItemFx {
    uint8_t flame_tongue, dragon_slayer, frost_brand, displace, ring_invisible, vermin;
};

// The effects the fights' rules look at (per game, from the profile)
struct Facts {
    uint8_t held[4];                    // can't act, slain by any blow: snake charm, paralysed, asleep, helpless
    uint8_t bless, curse, prayer, haste, slow, invisible, prot_evil, prot_good, mirror;
    uint8_t backstab_weapons[6];        // the weapons a thief backstabs with (or none)
    uint8_t charm;                      // Charm Person's effect (its data: the caster's side << 7, the
                                        // charmed one's own << 6, 0x20, the caster's level)
    uint8_t fear;                       // Fear's effect (they flee; data 1: a party member made Quick by it)
    uint8_t cough;                      // coughing in a stinking cloud (a round: no items, no spells)
    // How a shot looks in flight, by the item type that flies: pointed (an
    // arrow's picture by direction), spinning, a flask, a sling's stone; the
    // rest a rock
    uint8_t shot_pointed[6], shot_spinning[3], shot_flask[2], shot_sling[3];
    // More effects (spell_facts.md): Bestow Curse (its attacks and saves
    // -4), Blink (attacks on it miss once it has acted), fumbling (its turn
    // lost), Silence (no spells or items for it and those next to it),
    // Entangle (no moving), Sticks to Snakes (its turn lost while the
    // snakes last; data: snakes), Faerie Fire (stored AC + 2), blinded
    // (attacks -4, AC 4 worse, saves -4), invisible to animals (an animal's
    // attacks -4), Feeblemind
    uint8_t bestow, blink, fumbling, silence, entangle, sticks, faerie, blinded, animals_blind, feeble;
    MonFx   mon;
    SpellFx sp;
    // The races' and rangers' effects (created with the character; the
    // original's handlers): Constitution's bonus to saves against spells
    // and wands (dwarves, gnomes, halflings: Con 4-6 +1, 7-10 +2, 11-13 +3,
    // 14-17 +4, 18-20 +5), the half-elves' 30% against sleep and charm,
    // +1 to hit orcs (dwarves: target flag 0x14B & 4) and the gnomes'
    // foes (0x14B & 2), -4 to be hit by giants / trolls (dwarves, gnomes)
    // and by kobold-kind (gnomes: type 1) of size 2, rangers' level as
    // extra damage against giants (0x14B & 8)
    struct RaceFx {
        uint8_t con_save, halfelf, dwarf_orc, gnome_foe, giants, gnome_extra, ranger_giant;
    } race{};
    ItemFx  items;
    // Berserk (the Berserker sword's effect, the listing's spl_berzerk): at
    // the start of each of its turns the wearer takes the nearest other
    // creature as its target and goes to the side against it ("goes
    // berzerk"), computer-run, no spells that turn
    uint8_t berserk = 0;
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
    bool    can_use = true;             // items this round (coughing: not)
    bool    coughed = false;            // "is coughing" said this round
    bool    swept = false;              // swept this round
    bool    gone = false;               // a monster there was no room for: not in the fight at all
    bool    fleeing = false;            // turned undead, panic: runs for the field's edge
    int     down_size = 0;              // its footprint before it fell (a troll gets up again)
    uint8_t was_control = 0;            // its control byte before Animate Dead (back after the fight)
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
    int     surprise = 0;               // 1: our side surprised, 2: the enemies (the script's word 0x7ECB)
    int     enemy_health = 100;         // %
    int     vx = 0, vy = 0;             // the view's top-left square (7 x 7 shown)
    bool    indoors = true;
    int     to_hit_party = 0, to_hit_monsters = 0;  // the scripts' bonuses
    const Facts* fx = nullptr;
    int     morale_base = 0;            // the script's morale word (0x7EC6)
    // Missiles: the item types (ITEMS) and the ammunition's types
    const items::Names* names = nullptr;
    uint8_t arrow = 0, quarrel = 0;
    // Clouds on the field (Stinking Cloud, Cloudkill)
    struct Cloud {
        uint8_t  x = 0, y = 0, rounds = 0;
        bool     poison = false;        // Cloudkill (3 x 3, ground 0x1C), else Stinking Cloud (2 x 2, 0x1E)
        uint8_t  level = 0;             // its caster's (Dispel Magic)
        bool     resisted = false;      // a Dispel Magic failed on it
        uint16_t present = 0;           // bit k = square k is cloud
        uint8_t  ground[9] = {};        // what was there
    };
    static constexpr int kMaxClouds = 8;
    // Set by the caller before a cast: the square aimed at (Dispel Magic's
    // clouds), the fire shield's flame (1 hot, 2 cold, 0 the computer's pick)
    int     aim_x = -1, aim_y = -1, flame = 0;
    int     lost_image = -1;            // harm(): the one whose mirror image took it ("lost an image")
    // The fight's tables (set by the caller: a bolt's path), the last bolt's segments (its picture)
    const Tables* tables = nullptr;
    BoltSeg bolt[16];
    int     n_bolt = 0;
    Cloud   clouds[kMaxClouds];
    int     n_clouds = 0;
};

// Effects on a fighter
bool helpless(const Battle& b, const Fighter& f);
// Whether `viewer` can't see `target`: invisible (unless it sees the
// invisible), or blinking once it has acted this round
bool hidden(const Battle& b, int viewer, int target);
// The start of a fighter's turn (spell_facts.md 1.8): what an effect does
// to it. Silenced: no spells or items this turn (it may still fight);
// Snakes / Fumbling: its turn is lost
// Suffocates: engulfed too long - killed
// Confusion: Confused (the turn lost), RunsAway (it flees), Berserk (it
// attacks the nearest, friend or foe), Enraged (it acts as ever)
// Berzerk: the Berserker's effect - on the side against the nearest other creature (its target)
enum class TurnFx : uint8_t { None, Silenced, Snakes, Fumbling, Suffocates, Confused, RunsAway, Berserk, Enraged, Berzerk };
TurnFx turn_effects(Battle& b, int i);
// A charm's end: back to their own side (the effect's data, bit 6)
void uncharm(uint8_t* rec, const uint8_t* affect);
// What happened besides the plain blows: an attack's extras, the
// computer's special actions, the round's end. `who` says it (its name
// first), `to` the second name (or -1); `hit`: the attack's hit it follows.
enum class Ev : uint8_t {
    // said by `who` (of `to` when set)
    Engulfs, Hugs, BreathesAcid, BreathesFire, HoundFire, ThrowsLightning, SpitsAcid, SpitsMisses, Gazes,
    RayDisintegrate, RayStone, RayDeath, RayWounds,
    // a picture flying from `who` to (x, y): COMSPR `pic`, `frames` of it, `delay` ms a step
    Fly,
    // said of `who`
    Avoids, Poisoned, Paralyzed, ParalyzedLow, Reflects, Stoned, GazeStoned, Disintegrated, Killed, Damage,
    Unaffected, Down, Suffocates, StandsUp, GetsUp,
    // A bolt's picture along the Battle's bolt segments (from `who` to (x, y) first: COMSPR `pic`)
    BoltFly,
    // Dispel Evil's hit: "is dispelled" / "resists dispel evil"; a fire shield: "gets zapped"
    Dispelled, ResistsDispel, Zapped,
    // Slow Poison run out, still poisoned: "dies from poison"
    DiesPoison,
    // Constitution 20+: a hit point back - "is fully healed" / "is partially healed"
    Regen,
    // the beholder casts spell `amount` (the caller casts it, as the computer does)
    Cast,
};
struct Event {
    uint8_t who = 0;
    Ev      ev = Ev::Down;
    int8_t  to = -1;
    int8_t  hit = -1;
    int16_t amount = 0;                 // Damage: the points; Cast: the spell
    uint8_t kind = 0;                   // Damage: its kind (1 fire, 2 cold, 4 electricity, 8 magic, 0x10 acid)
    uint8_t x = 0, y = 0, pic = 0, frames = 0, delay = 0;
};
// Timed effects a round on (a minute); those run out go. Then the round's
// end: regeneration (3 hit points), a troll's getting up (events)
int tick(Battle& b, Event* out = nullptr, int cap = 0);
// The fight's start: those invisible from the start become so
void battle_start(Battle& b);

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
// What fighter f's readied missile weapon shoots (its range over 1): the
// item that flies (the thrown weapon itself, the readied arrows / quarrels;
// -1 none); *can: it can shoot (a sling needs nothing)
int shot_item(const Battle& b, const Fighter& f, bool* can);
// Slot 1's attacks this round by the readied weapon (a missile weapon: the
// ITEMS file's number, at least 2, no more than the missiles); *ranged: so
int slot1_attacks(const Battle& b, const Fighter& f, bool* ranged);
// Slot 1's attacks worked out again when the weapon may have changed in
// the round (the computer's choice, View, Use): never giving back those
// used - once it has attacked only fewer count (a blow up to twice as
// many) - coab's facts (reclac_attacks), combat_rules_facts 3.1
void recount_attacks(Battle& b, int i);

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
    bool avoided[8] = {};               // a hit dodged ("Avoids it"; counted a miss)
    Event ev[8];                        // the extras after hits (poison, paralysis, a touch's damage, engulfing ...)
    int  n_ev = 0;
    int  shots = 0;                     // the weapon's (slot 1) attacks made: a shot uses one missile each
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
// The general damage routine (spells, breath, specials, a monster's touch):
// the target's resistances, then its save if `save` >= 0 (`on_save` 1:
// none, 2: half). The damage done (0: none; *resisted when its defences
// took it all); *down when it went down.
struct Harm {
    int kind = 0;                       // 1 fire, 2 cold, 4 electricity, 8 magic, 0x10 acid, 0x20 breath, 0x40 death
    int level = 0;                      // the caster's level (magic resistance; 0: no spell)
    int dice = 0;                       // dice rolled (the efreet's -1 a die)
    int spell = 0, spell_level = 0;     // the spell (the minor globe)
    int save = -1, on_save = 0, bonus = 0;
    bool single = false;                // a spell at picked creatures (not an area): Mirror Image can take it
};
int harm(Battle& b, int c, int amount, const Harm& h, create::Dice& d, bool* down, bool* resisted = nullptr);
// Magic resistance against a spell's effect on fighter c (`effect`: the
// effect type it gives, 0 none); true: it's unaffected
bool resists(Battle& b, int c, int effect, const Harm& h, create::Dice& d);
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
// The start of the computer's action step: holds it had (engulfing,
// hugging, held fast, suffocating) let go
void release_holds(Battle& b, int i);
// The computer's special actions (breath, spit, gazes, rays, thrown
// lightning) at the start of its step: the events (none: nothing done);
// *ends_turn when it may do nothing more this turn
int special(Battle& b, const Tables& t, int i, create::Dice& d, Event* out, int cap, bool* ends_turn);

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
    Poison,                             // Cloudkill: those in it die by their Hit Dice (word: "is Poisoned")
    Charm,                              // a person (humanoid, man-sized; kind 1: any creature) joins the caster's side
                                        // (word; word2 not a person)
    Cone,                               // damage to those in a cone (cone(): 2 rays, the caster's (level + 1) / 2 squares)
    Fear,                               // those in a cone (3 rays, 6 squares) that fail a save flee (word)
    Slay,                               // a save or slain (word); saved: the dice of magic damage (Slay Living)
    Kill,                               // a save against poison or poisoned and killed (word, then "is killed")
    Fumble,                             // a save or clumsy (word, fumbling), saved slowed (word2); then a second
                                        // save: clumsy again, or unaffected
    Feeble,                             // a save (class-adjusted) or Int and Wis 7 for good (word)
    Entangle,                           // outdoors only: a save or no moving (word), 24 rounds
    Faerie,                             // the picked by size: the last one, non-persons and large ones unaffected,
                                        // else a save or highlighted (word)
    Snakes,                             // Sticks to Snakes: 6+ Hit Dice smash them (word2), else snakes (word)
    SnakeCharm,                         // snakes whose hit points the caster's cover, charmed for the fight (word)
    CureBlind,                          // blinded: "is Cured" (word2), "can see" (word)
    Enlarge,                            // Strength by the caster's level when more than theirs (word; else unaffected)
    Reduce,                             // a failed save takes Enlarge away (word); else nothing
    RemoveCurse,                        // Bestow Curse cured ("is Cured" word2, word), else the first cursed item comes
                                        // off (word3)
    Confuse,                            // the first 2d8 in the area: a save or confused (word)
    DispelEvil,                         // the caster: attacks by the evil -7, its hits dispel them (word)
    FireShield,                         // the caster: hot ("is protected", word) or cold (nothing said)
    Teleport,                           // Dimension Door: the caller moves the caster (teleport(); word)
    Dispel,                             // Dispel Magic: effects and clouds by level against level (word)
    SlowPoison,                         // the poisoned get back up (word), poisoned again when it runs out
    Hammer,                             // Spiritual Hammer: its effect on the caster (the caller hands over the hammer)
    Animate,                            // Animate Dead: the dead (not monsters) up on the caster's side (word)
    Restore,                            // Restoration: those with lost levels (word; the caller restores them)
    GiantStrength,                      // the potion: Strength 21 when more than their own (word); the effect
                                        // either way
    Defoliate,                          // the wand: a cone of 3 squares (1 ray), dice of damage - plants all of it,
                                        // the rest as when saved (the spell table's)
    Slow,                               // as Haste for the other side: at most the caster's level of them, a hasted
                                        // one cured of it instead (word2 "is Cured")
};
struct FightSpell {
    uint8_t   spell;
    SpellDoes does;
    uint8_t   n, sides, plus;           // dice
    uint8_t   per;                      // 1: + the caster's level, 2: (level + 1) / 2 missiles of 1d4 + 1, 3: level dice,
                                        // 5: 3, 5 or 7 dice, 6: level dice + the level; effects: the dice are rounds (4: x 10)
    uint8_t   kind;                     // damage: 1 fire, 2 cold, 4 electricity, 8 magic, 0x10 acid
    uint32_t  word;                     // what's said ("is Blessed", "falls asleep"; GAME.OVR, 0: nothing;
                                        // Heal: "is Healed" instead of fully / partly healed)
    uint32_t  word2 = 0;                // ... when they saved (clouds: "starts to cough")
    uint32_t  word3 = 0;                // a third ("has an item un-cursed")
    uint8_t   reach = 0;                // Bolt: its length (squares; the original's bolt with the square aimed at)
};
// What happened to each target, in order
// Fallen: the skull, no words; Risen: "gets back up" / "stands up and grins"
enum class Did : uint8_t { Word, Damage, Unaffected, Misses, Healed, Down, Word2, Fallen, Word3, Risen, LostImage };
struct SpellLine {
    uint8_t who;
    Did     did;
    int     amount;
};
// ---- Clouds that stay on the field (Stinking Cloud; facts: the Project's
// combat_fx_facts.md / combat_rules_facts.md 4.4): the square aimed at and
// those east, south-east and south of it - each that can be entered -
// become cloud (ground 0x1E, its picture) for `rounds` rounds (the
// caster's level); "The air clears a little..." when it goes.
// Cloudkill's poisonous cloud: the square aimed at and the 8 round it
// (ground 0x1C); those in it when it's laid, stepping in, and at each
// round's end: Hit Dice 0-4 die, 5 unless they save at -4, 6 unless they
// save, 7 and up are unaffected ("is Poisoned", "is killed").
constexpr uint8_t kCloudGround = 0x1E, kPoisonGround = 0x1C;
// The fighters a cloud at (x, y) would take in (each once)
int cloud_fighters(const Battle& b, int x, int y, int* out, int cap, bool poison = false);
// Dispel Magic on the clouds round (x, y): each a chance by level against
// its caster's (once: a failed one resists); how many end as the round does
int dispel_clouds(Battle& b, int x, int y, int level, create::Dice& d);
// Lays a cloud there (false: no room for another)
bool lay_cloud(Battle& b, const Tables& t, int x, int y, int rounds, bool poison = false);
bool in_cloud(const Battle& b, int x, int y);
bool in_poison(const Battle& b, int x, int y);
// Breathing the poison: true when it kills them (dead already, out of the fight)
bool breathe_poison(Battle& b, int i, create::Dice& d);
// Breathing it (laid on them, or stepping in): a save against poison or
// helpless d4 + 1 rounds (Word), saved (Word2: "starts to cough");
// Unaffected: already helpless or out
Did breathe_cloud(Battle& b, int i, create::Dice& d);
// A round over: clouds run out, their ground comes back (a fallen party
// member's body where one lies); how many went
int clouds_round(Battle& b);

// Dimension Door: fighter i to the square (x, y) when it can stand there
// (a hug or an engulfing holding it lets go); false: not moved
bool teleport(Battle& b, const Tables& t, int i, int x, int y);

// The fighters in an area: within r squares of (x, y)
int in_area(const Battle& b, const Tables& t, int x, int y, int r, int* out, int cap);
bool saving_throw(const Fighter& f, int type, int bonus, create::Dice& d);
// The fighter whose turn it is and the damage's kind (saving throws look at
// them: protection from evil / good, resist fire / cold)
void set_actor(const Battle& b, int i);
// Casts the spell by fighter `caster` on `targets` (chosen as the spell's
// aim says); what it did. The spell left the caster's memory already.
// `pw`: the caster's level for it (0: their own; an item's: item_power).
int cast(Battle& b, const classes::Tables& st, int caster, int spell, const FightSpell& fs, const int* targets,
         int n, create::Dice& d, SpellLine* out, int cap, int pw = 0);
// The caster's level for a spell: by its kind; from an item 6 (the
// monsters' spells - the items' own - as the caster's)
int power_of(const uint8_t* rec, const classes::Tables& st, int spell, bool item = false);

// A cone (Fear, Cone of Cold; coab's facts): the line from the caster
// through (tx, ty) carried on (its steps repeated) to `reach` squares
// (straight steps 2 half-squares, slanted 3), stopped by the field's edge
// and before a wall; the fighters (not the caster) on the line to its end
// and - rays 2 / 3 - on the lines to the end moved a square to the right
// / and the left of the line's last step
int cone(const Battle& b, const Tables& t, int caster, int tx, int ty, int reach, int rays, int* out, int cap);

// A lightning bolt's squares: from (tx, ty) on, away from the caster, up to
// `len` squares or a wall / the field's edge; the fighters on them
int bolt_line(const Battle& b, const Tables& t, int caster, int tx, int ty, int len, int* out, int cap);
// The bolt as the original runs it (curse_finish_facts.md 2): from the
// square aimed at on, away from the caster, `length` squares (a budget of
// half squares); it stops at each creature, ends at the field's edge,
// bounces back off a wall indoors (`near_rule`: a bounce within 4 squares
// of the caster costs 8 more - Lightning Bolt's), and the original's byte
// sums for what's left after a bounce. The fighters it hits in order (not
// the one on the square aimed at; after a bounce anyone again, the caster
// too) and its segments (the picture flies along each).
int bolt_path(const Battle& b, const Tables& t, int caster, int tx, int ty, int length, bool near_rule, int* hits,
              int hit_cap, BoltSeg* segs, int* n_segs, int seg_cap);

// Picking creatures one by one (spell aim 5: Faerie Fire, Charm Monsters;
// curse_finish_facts.md 7): a budget - Faerie Fire the caster's level,
// the others 2d4 - and each pick's cost: by size (1 -> 1, 2 / 3 -> 2, 4 ->
// 4, else 0) or by Hit Dice (0 / 1 -> 1, 2 -> 2, 3 -> 4, more -> 8). The
// picking ends once 2 or more are picked and their costs pass the budget.
int pick_cost(const Fighter& f, bool by_size);
bool picks_done(const Battle& b, const int* picked, int n, int budget, bool by_size);

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
// Shares `total` among the party (all but the animated dead: those who
// fell count too, as in the original) and gives it to those still standing
// (not animated): +10% for a high prime requisite (over 15), multi-classes
// divided by their classes; the share each got before that
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

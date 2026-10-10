#include "profile.h"

namespace profile {

namespace {

// Curse of the Azure Bonds, GOG release: START.EXE (EXEPACK, 18-byte
// header). Data segment at 0x48D0 of the unpacked program; table addresses
// as named in the coab reimplementation's notes (facts only, no code).
// Party menu: its 12 entries at 0xB133 of the unpacked program, each a
// string[40] and an "on" byte (Create New Character, Add Character to
// Party, Exit to DOS start on; the others follow the party); its prompt
// and the party list's headings in GAME.OVR's code segments.
// View Character: the class (27-byte slots, 18 with "unknown"), race,
// alignment, sex, coin and health names at 0xB898-0xBC37 of the unpacked
// program; the screen's words in GAME.OVR (as its code prints them).
// Items: 255 name words in 21-byte slots at 0xBC37 (word 1 first); the
// ITEMS file; arrows 73, quarrels 28, darts 9, flask of oil 86; elves'
// bow / sword bonus types 41-44, 37, 36 (coab's lists); the shop's words.
// Character rules: the program's data segment starts at image 0xABE0
// (coab's "seg600" addresses); its rule tables (THAC0 0x3E3A, thief
// skills 0x3EC0 / 0x3F20 / 0x3F33, race / class tables 0x3F88-0x41DA,
// per-class experience and spell slots 0x429B (99 bytes a class), saving
// throws 0x45BE, spells 0x37DC) - all checked against the GOG sample party
// (THAC0, saves, spell slots match exactly). Effect and spell numbers as
// coab names them.
// Items menu (GAME.OVR): " Use" ... " Id" at 0x28571-0x28596 after
// "Ready"; its messages (unready / readied / halve, drop, trade, the
// shop's sell and identify offers) - the strings the game's own code
// prints there.
// The shop's Take / Appraise and the temple (GAME.OVR): menus, the cure
// names and messages, the coins-left-behind warnings, appraising; the
// cures' prices and the effects they take away, a kept gem / jewel's type
// and name word - constants in the game's code (coab's notes, checked
// against the strings the code prints).
// Spells: the magic menu (START.EXE image 0xB033), level names (0xB480,
// 41-byte slots), spell names (0xD39F, 41-byte slots, spell 0 empty); the
// spell list and rest words in GAME.OVR; resting's encounter check: the
// area words 0x7ED2 (steps) / 0x7ED3 (chance).
// Combat (from coab's notes, checked against the program): the battlefield's
// ground table DS 0x26A4 (67 x 4 bytes), placement tables DS 0x02DC
// (fallbacks), 0x02EC (formation facings), 0x02FC (facing to battlefield
// direction), 0x0300 / 0x0308 (rank centres), 0x0310 (shapes), outdoor
// terrain flags DS 0x0354, the turn-undead table DS 0x0369; combat's
// words in GAME.OVR; the effects a fight's end takes away. Shots in flight
// by the flying item's type (coab's notes): pointed - darts 9, javelins 21,
// hornet's-nest darts 100, quarrels 28, spears 31, arrows 73; spinning -
// hand axes 2, clubs 7, glaives 14; flasks 85, 86; slings 47, 98, 101.
// Alter (camp): its menu, Select Exit / Place Exit (START.EXE image 0xB05C,
// 0xB086, 0xB0AF) and its words in GAME.OVR.
// Casting in camp: what each spell does outside combat (coab's notes on
// the spells' code; the words are the ones the code prints), the effects
// list's names (spell-named effects, then the named ones).
using spells::Does;
constexpr spells::CampSpell kCurseCamp[] = {
    {0x01, Does::Affect, 0, 0, 0, 0x2FD0A},        // Bless: "is Blessed"
    {0x03, Does::Heal, 1, 8, 0, 0},                // Cure Light Wounds
    {0x05, Does::Affect, 0, 0, 0, 0x2FDE3},        // Detect Magic: "is affected"
    {0x06, Does::Affect, 0, 0, 0, 0x2FE1C},        // Protection from Evil: "is protected"
    {0x07, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x08, Does::Affect, 0, 0, 0, 0x2FE56},        // Resist Cold: "is cold-resistant"
    {0x0B, Does::Affect, 0, 0, 0, 0x2FDE3},
    {0x0C, Does::NotYet, 0, 0, 0, 0},              // Enlarge (strength)
    {0x0E, Does::NotYet, 0, 0, 0, 0},              // Friends (charisma)
    {0x10, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x11, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x12, Does::Affect, 0, 0, 0, 0x2FDE3},        // Read Magic
    {0x13, Does::Affect, 0, 0, 0, 0x3022D},        // Shield: "is shielded"
    {0x16, Does::Affect, 0, 0, 0, 0x2FDE3},        // Find Traps
    {0x18, Does::Affect, 0, 0, 0, 0x3044A},        // Resist Fire: "is fire resistant"
    {0x1A, Does::SlowPoison, 0, 0, 0, 0x2FDE3},
    {0x1C, Does::NotYet, 0, 0, 0, 0},              // Spiritual Hammer (a weapon)
    {0x1D, Does::Affect, 0, 0, 0, 0x2FDE3},        // Detect Invisibility
    {0x1E, Does::Affect, 0, 0, 0, 0x3067C},        // Invisibility: "is invisible"
    {0x20, Does::Mirror, 0, 0, 0, 0x306EF},        // Mirror Image: "is duplicated"
    {0x23, Does::NotYet, 0, 0, 0, 0},              // Strength
    {0x25, Does::CureBlind, 0, 0, 0, 0},
    {0x27, Does::CureDisease, 0, 0, 0, 0},
    {0x29, Does::NotYet, 0, 0, 0, 0},              // Dispel Magic
    {0x2A, Does::Prayer, 0, 0, 0, 0x31544},        // Prayer: "is praying"
    {0x2B, Does::RemoveCurse, 0, 0, 0, 0},
    {0x2E, Does::NotYet, 0, 0, 0, 0},              // Dispel Magic
    {0x30, Does::Haste, 0, 0, 0, 0x31907},         // Haste: "is Hasted"
    {0x32, Does::Affect, 0, 0, 0, 0x3067C},        // Invisibility 10' Radius
    {0x34, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x35, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x36, Does::Affect, 0, 0, 0, 0x2FE1C},        // Protection from Normal Missiles
    {0x38, Does::NotYet, 0, 0, 0, 0},              // Restoration (lost levels)
    {0x3A, Does::Heal, 2, 8, 1, 0},                // Cure Serious Wounds
    {0x3E, Does::Heal, 2, 4, 2, 0x3206D},          // the items' healing: "is Healed"
    {0x63, Does::Heal, 2, 4, 2, 0x3379C},
    {0x43, Does::Neutralize, 0, 0, 0, 0},
    {0x45, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x47, Does::Heal, 3, 8, 3, 0},                // Cure Critical Wounds
    {0x4B, Does::Raise, 0, 0, 0, 0},
    {0x4D, Does::Affect, 0, 0, 0, 0x2FDE3},
    {0x50, Does::Affect, 0, 0, 0, 0x3067C},
    {0x55, Does::NotYet, 0, 0, 0, 0},              // Fire Shield (hot or cold)
    {0x58, Does::Affect, 0, 0, 0, 0x2FE1C},        // Minor Globe of Invulnerability
    {0x59, Does::RemoveCurse, 0, 0, 0, 0},
};
constexpr uint8_t kCurseSpellNamed[] = {
    0x01, 0x02, 0x05, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0E, 0x10, 0x11, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19,
    0x1C, 0x1D, 0x20, 0x21, 0x22, 0x24, 0x25, 0x26, 0x27, 0x29, 0x2A, 0x2D, 0x2E, 0x31, 0x33, 0x34, 0x35,
};
constexpr profile::EffectName kCurseNamed[] = {
    {0x04, 0x19E88}, {0x07, 0x19E94}, {0x1B, 0x19EA0}, {0x1F, 0x19EA9}, {0x23, 0x19EB2}, {0x2C, 0x19EBB},
    {0x32, 0x19EC9}, {0x36, 0x19ED9}, {0x37, 0x19EEA}, {0x3B, 0x19EF3}, {0x3D, 0x19F00}, {0x3F, 0x19F10},
    {0x44, 0x19F2F}, {0x45, 0x19F39}, {0x47, 0x19F4E}, {0x48, 0x19F58}, {0x49, 0x19F64}, {0x4D, 0x19F81},
    {0x59, 0x19F89},
};

// Spells in fights: what each does (coab's notes on the spells' code; the
// words the code prints). Damage kinds: 1 fire, 2 cold, 4 electricity,
// 8 magic, 0x10 acid; per: 1 + level, 2 (level + 1) / 2 missiles, 3 level dice.
using combat::SpellDoes;
constexpr combat::FightSpell kCurseFight[] = {
    {0x01, SpellDoes::Ours, 0, 0, 0, 0, 0, 0x2FD0A},        // Bless: "is Blessed"
    {0x02, SpellDoes::Theirs, 0, 0, 0, 0, 0, 0x2FD3C},      // Curse: "is Cursed"
    {0x03, SpellDoes::Heal, 1, 8, 0, 0, 0, 0},              // Cure Light Wounds
    {0x04, SpellDoes::Damage, 1, 8, 0, 0, 8, 0},            // Cause Light Wounds (a touch)
    {0x05, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FDE3},
    {0x06, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x07, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x08, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE56},
    {0x09, SpellDoes::Damage, 0, 0, 0, 1, 9, 0},            // Burning Hands: the caster's level
    {0x0B, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FDE3},
    {0x0F, SpellDoes::Damage, 0, 4, 0, 2, 8, 0},            // Magic Missile
    {0x10, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x11, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x13, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x3022D},      // Shield
    {0x14, SpellDoes::Damage, 1, 8, 0, 1, 0x0C, 0},         // Shocking Grasp: 1d8 + level
    {0x15, SpellDoes::Sleep, 0, 0, 0, 0, 0, 0x302AE},       // Sleep: "falls asleep"
    {0x17, SpellDoes::Hold, 0, 0, 0, 0, 0, 0x303F1},        // Hold Person: "is held"
    {0x18, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x3044A},
    {0x1D, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FDE3},
    {0x1E, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x3067C},      // Invisibility
    {0x20, SpellDoes::Mirror, 0, 0, 0, 0, 0, 0x306EF},
    {0x2A, SpellDoes::Prayer, 0, 0, 0, 0, 0, 0x31544},
    {0x2F, SpellDoes::Damage, 0, 6, 0, 3, 9, 0},            // Fireball: level d6
    {0x30, SpellDoes::Haste, 0, 0, 0, 0, 0, 0x31907},
    {0x31, SpellDoes::Hold, 0, 0, 0, 0, 0, 0x303F1},
    {0x32, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x3067C},
    {0x34, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x35, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x36, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x3A, SpellDoes::Heal, 2, 8, 1, 0, 0, 0},
    {0x42, SpellDoes::Damage, 2, 8, 1, 0, 8, 0},            // Cause Serious Wounds
    {0x45, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x47, SpellDoes::Heal, 3, 8, 3, 0, 0, 0},
    {0x48, SpellDoes::Damage, 3, 8, 3, 0, 8, 0},            // Cause Critical Wounds
    {0x4A, SpellDoes::Damage, 6, 8, 0, 0, 9, 0},            // Flame Strike
    {0x58, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FE1C},
    {0x21, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x3074F},      // Ray of Enfeeblement: "is weakened"
    {0x22, SpellDoes::Cloud, 0, 0, 0, 0, 0, 0x35E37, 0x35E27},  // Stinking Cloud: "chokes and gags from nausea" / "starts to cough"
    {0x0A, SpellDoes::Charm, 0, 0, 0, 0, 0, 0x2FED8, 0x2FECA},  // Charm Person: "is charmed" / "is unaffected"
    {0x51, SpellDoes::Charm, 0, 0, 0, 0, 1, 0x32786},       // Charm Monsters: "is charmed" (any creature, the table's targets)
    {0x54, SpellDoes::Fear, 0, 0, 0, 0, 0, 0x32A7A},        // Fear: "runs in terror" (a cone, 3 rays, 6 squares)
    {0x5B, SpellDoes::Poison, 0, 0, 0, 0, 0, 0x35E53},      // Cloudkill: "is Poisoned" (a 3 x 3 cloud)
    {0x5C, SpellDoes::Cone, 0, 4, 0, 6, 0x10, 0},           // Cone of Cold: level d4 + level (a cone, 2 rays; coab: acid)
    {0x26, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x30FB6},      // Cause Blindness: "is blind"
    {0x28, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x310A2},      // Cause Disease: "is diseased"
    {0x33, SpellDoes::Bolt, 0, 6, 0, 3, 4, 0},              // Lightning Bolt: level d6
    {0x37, SpellDoes::Theirs, 0, 0, 0, 0, 0, 0x31CD9},      // Slow: "is Slowed"
    {0x5E, SpellDoes::Hold, 0, 0, 0, 0, 0, 0x303F1},        // Hold Monster
    // The items' own spells (the spell table's monster spells)
    {0x39, SpellDoes::Haste, 5, 4, 0, 0, 0, 0x31ED7},       // speed: "is Speedy", 5d4 rounds
    {0x3C, SpellDoes::Bolt, 1, 6, 20, 0, 4, 0},             // a lightning stroke: 1d6 + 20
    {0x3D, SpellDoes::Theirs, 5, 4, 0, 0, 0, 0x32033},      // paralysis: "is paralyzed", 5d4 rounds
    {0x3E, SpellDoes::Heal, 2, 4, 2, 0, 0, 0x3206D},        // healing: "is Healed"
    {0x3F, SpellDoes::Ours, 2, 10, 0, 4, 0, 0x320C4},       // the party invisible: "is invisible", 2d10 x 10
    {0x40, SpellDoes::Damage, 0, 6, 0, 5, 9, 0},            // a fireball of 3, 5 or 7 dice
    {0x41, SpellDoes::Damage, 2, 4, 2, 0, 8, 0},            // the wand's missile: 2d4 + 2
    {0x63, SpellDoes::Heal, 2, 4, 2, 0, 0, 0x3379C},        // extra healing: "is Healed"
};

// Random treasure (Curse): item types, name words, weights, values per
// plus, the scrolls' spell bands (from the program's code)
constexpr uint8_t kCurseWeightType[] = {
    0x5D, 0x08, 0x4D, 0x06, 0x15, 0x09, 0x07, 0x25, 0x16, 0x1E,
    0x02, 0x14, 0x1D, 0x1F, 0x20, 0x21, 0x27, 0x2A, 0x2C, 0x2E, 0x3B,
    0x0A, 0x1A, 0x24, 0x01, 0x0D, 0x0E, 0x23, 0x0B, 0x10, 0x19, 0x1B, 0x29, 0x2F,
    0x04, 0x0F, 0x17, 0x22, 0x2B, 0x2D, 0x33, 0x03, 0x18, 0x28, 0x05, 0x0C, 0x11, 0x13, 0x32,
    0x12, 0x34, 0x26, 0x35, 0x37, 0x39, 0x36, 0x38, 0x3A};
constexpr uint16_t kCurseWeight[] = {
    1, 10, 10, 15, 20, 25, 30, 35, 40, 40,
    50, 50, 50, 50, 50, 50, 50, 50, 50, 50, 50,
    60, 60, 60, 75, 75, 75, 75, 80, 80, 80, 80, 80, 80,
    100, 100, 100, 100, 100, 100, 100, 125, 125, 125, 150, 150, 150, 150, 150,
    175, 200, 250, 250, 300, 350, 400, 400, 450};
static_assert(sizeof kCurseWeightType == sizeof kCurseWeight / 2, "weights");
constexpr uint8_t kCurseValueType[] = {0x3B, 0x49, 0x1C, 0x35, 0x36, 0x4D, 0x37, 0x38, 0x39, 0x3A};
constexpr uint16_t kCurseValue[] = {2500, 150, 150, 3000, 3000, 3000, 3500, 3500, 4000, 5000};
constexpr treasure::Facts kCurseRandom = {
    0x3D, 0x3E, 0x47, 0x54, 0x4F, 0x3B, 0x49, 0x5D, 0x4D, 0x15,
    0x2D,
    {0x24, 0x23, 0x22, 0x25, 0x26},
    0x32, 0x33, 0x34, 0x35, 0x3A,
    0xA1,
    0x31, 0x32, 0x30, 0x3D, 0x42, 0xE0,
    0x4F, 0xA7, 0xDD, 0xDE,
    0xD1, 0xD0, 0xD1,
    {kCurseWeightType, kCurseWeight, sizeof kCurseWeightType, 40},
    {kCurseValueType, kCurseValue, sizeof kCurseValueType, 2000},
    {0x49, 0x1C}, 0x09,
    25, 300,
    {{13, 8}, {7, 28}, {11, 44}, {9, 80}, {4, 90}},
    {{8, 0}, {7, 21}, {8, 36}, {5, 65}, {6, 70}},
};

// Title: picture 1 for 5 s; picture 2 with 3 on it at row 11, column 6 for
// 10 s; picture 4 at row 11 for 10 s; the credits for 10 s.
constexpr TitleStep kCurseTitle[] = {
    {1, 0, 0, true, 5000},
    {2, 0, 0, true, 0},
    {3, 11, 6, false, 10000},
    {4, 11, 0, false, 10000},
    {0, 0, 0, true, 10000},
};

// Curse's ECL opcodes (operand counts as coab lists them)
constexpr int8_t K3 = ecl::kCount3, K2 = ecl::kCount2;
const ecl::OpSet kCurseEcl = [] {
    ecl::OpSet s{};
    for (auto& v : s.sizes) v = ecl::kUnknown;
    const int8_t sizes[0x41] = {
        0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3,
        0, 1, 2, 2, 1, 1, 0, 4, K3, 0, 0, 0, 0,
        0, 0, 0, 1, 6, 2, 1, 3, 2, 4, 0, K2, K2,
        8, 3, 14, 3, K2, 6, 1, 5, 3, 3, 0, 1, 0,
        1, 3, 1, 3, 1, 1, 0, 3, 1, 0, 0, 1, 1,
    };
    for (int i = 0; i < 0x41; ++i) s.sizes[i] = sizes[i];
    s.sizes[0x34] = 2;   // ECL CLOCK and ADD NPC read 2 operands (coab's table
    s.sizes[0x36] = 2;   // says 1; with 2, more of Curse's scripts decode)
    s.exit = 0x00; s.go_to = 0x01; s.go_sub = 0x02; s.ret = 0x13; s.new_ecl = 0x20;
    s.on_goto = 0x25; s.on_gosub = 0x26; s.if_first = 0x16; s.if_last = 0x1B;
    s.load_files = 0x21; s.load_pieces = 0x37;
    return s;
}();

const Profile kProfiles[] = {
    {games::Game::CurseOfTheAzureBonds, "GOG", "START.EXE", 57789, 62432, 0x48D0,
     {0x6E60, 0x6E88, 0x6EB0, 0x6EF2, 0x6F1B, 0x6F0A, 0x6ED6, 0x6EE3, 0x6F31, 0x6F3E, 0x6F4D, 0x6F64, 0x6F7B},
     "8X8D1.DAX", 202,
     20830,
     "TITLE.DAX", kCurseTitle, sizeof kCurseTitle / sizeof kCurseTitle[0],
     "GAME.OVR", 272137, 706, 218, {3, 8},
     2, 6, 203, "SKY.DAX", 252, &kCurseEcl,
     0x6D9A, 2, 1,
     {0x79, 0x4CA1, 0x6D5A, 0x6D7A, 32, 0x50},
     {0xB133, 12, 42, 0x20111, 0x1EE80, 0x37E31, 0x37E36, "CURSE.CFG", 0x1F5AB, 0x1F5BD, 0x1F673, 0xB00A,
      0x1B382, 0x1B34E, 0x23595, 0x235A6, 0x235BF, 0x235D1, 0x235D6, 0x235D9, 0x235FD, 0x23617,
      0x1C506, 0x1C511, 0x2255C, 0x22562, 0x2256D, 0x2257C, 0x22586, 0x22591, 0x225A5, 0x32BF0,
      0x227F1, 0x22804, 0x2280D},
     {{0xB898, 27, 18}, {0xBA7E, 10, 8}, {0xBACE, 17, 9}, {0xBB67, 7, 2}, {0xBB75, 11, 7}, {0xBBC2, 13, 9},
      0x27094, 0x2709A, 0x2709F, 0x270BD, 0x270C5, 0x270D7, 0x276AA, 0x276B1, 0x276B8, 0x276C7,
      0x276D6, 0x276E8, 0x27BA8, 5},
     {{0xBC37, 21, 255}, "ITEMS", 73, 28, 9, 86, 0x87, 0xB1, {41, 42, 43, 44, 37, 36},
      0x773D, 0x7745, 0x3CF1A, 0x3CF20, 0x3CF26, 0x7D3F, 0x7D18, 0x7B5D, 0x2916E,
      0x2859A, 0x285A0, 0x2856B, 0x37AF6, 0x37AFD, 0x28EF7, 0x28F03, 0x28F0F, 0x28F1E, 0x38F44,
      0x270CA, 0x270D1},
     {0xABE0,
      {0x37DC, 0x47B0, 0x37DC, 0x65, 0x3E3A, 0x3EA2, 0x3EAA, 0x3EBB, 0x3EC0, 0x3F20, 0x3F33, 0x3F88, 0x3FFA,
       0x404E, 0x4124, 0x4174, 0x41DA, 0x429B, 0x45BE},
      0x081A, 0x0822, 0x3EC3,
      0x61, 0x1A, 0x2F, 0x12, 0x30, 0x6B, 0x7C, 0x08, 0x86,
      {0x0B, 0x12, 0x0C, 0x15}, 0x0F, {0x22, 0x10}, 0x1F, 0x2F,
      0x206B3, 0x206C8, 0x206D4, 0x206DF, 0x206C1, 0x20710, 0x2071F, 0x20730, 0x20736,
      {0x0B, 0x12, 0x15}, 0x3BB0D, 0x3BB1C, 0x3BB2F, 0x3BB36, 0x3BB4B},
     {0x28571, 0x28576, 0x2857D, 0x28583, 0x2858A, 0x28590, 0x28596, 0x27ECE, 0x285AB, 0x29221, 0x285BB, 0x285C1,
      0x285D6, 0x2915D, 0x297BC, 0x297CB, 0x297E2, 0x297F1, 0x297F7, 0x299FC, 0x29A44, 0x29A6A, 0x29A33, 0x206C1},
     {0x5882, 0x58AA, 0x5662, 0x567A,
      0x4DE2, 0x4E88, 0x4F6E, 0x4F80, 0x4F94, 0x4FA9, 0x53EC, 0x519E, 0x54DA, 0x55C6,
      0x4BA5, 0x4C1B, 0x4C2C, 0x4C3A, 0x4C48, 0x4C5A,
      0x4DD4, 0x4E77, 0x5191, 0x53DB, 0x54CB, 0x55B7,
      0x58D1, 0x591C, 0x7D64, 0x7DB7,
      0x2D532, 0x2D551, 0x2D558, 0x2D562, 0x2D568, 0x2D574, 0x2D58A, 0x2D58F, 0x2D594, 0x2D59E,
      0x2D5A9, 0x2D4EF, 0x2D502, 0x2D507, 0x2D50D, 0x2D51F,
      0x2C96D, 0x2C982, 0x2C989, 0x2C993, 0x2BE8D,
      0x24C2E, 0x24C4D, 0x24C65, 0x24C84, 0x24C9A, 0x24CA8, 0x24CB7, 0x24CC4, 0x24CDB},
     {{1000, 1000, 100, 350, 600, 5000, 1000, 5500, 3500, 2000},
      0x21, {0x1F, 0x22, 0x2B, 0x2C, 0x20, 0x39}, 0x44, 0x37, 0x16, 0x0F, 0x20, 0x24},
     70, 0x65, 0xD6,
     {0xB033, {0xB480, 41, 5}, {0xD39F, 41, 101},
      {0x27B87, 0x2A486, 0x2A490, 0x2A4BB, 0x2E177, 0x2E141, 0x19878, 0x198AB, 0x194FB, 0x19509, 0x1951C, 0x1952F,
       0x19890, 0x192C4, 0x192EB, 0x2B35E, 0x2B460, 0x2BA11, 0x2B5DD, 0x2B6A8, 0x2BA20},
      0x7ED2, 0x7ED3,
      {0x192DB, 0x1942B, 0x2E13C, 0x2F274, 0x2F21F, 0x2F235, 0x2F0EF, 0x39B78, 0x39B88, 0x36666, 0x2FECA, 0x30F71,
       0x32173, 0x324C5, 0x31597, 0x315A4, 0x19F93, 0x2E14A, 0x2A4A6, 0x2A4C7, 0x19A8D, 0x19AA5, 0x19AC1, 0x19AE4,
       0x19A77, 0x19B03, 0x192FB, 0x2B76C, 0x2A4B1, 0x2E151,
       0x2F23F, 0x2F249, 0x2F262, 0x294BC, 0x294C9, 0x294CF, 0x2A49C},
      kCurseCamp, sizeof kCurseCamp / sizeof kCurseCamp[0],
      {2, 0x2A, {0x22, 0x2B, 0x32}, {{0, 0}, {0x2C, 0x1F}, {0x39, 0}}},
      kCurseSpellNamed, sizeof kCurseSpellNamed, kCurseNamed, sizeof kCurseNamed / sizeof kCurseNamed[0], 0xD2, 0x10},
     {0xB05C, 0xB086, 0xB0AF,
      {0x1AC65, 0x1A792, 0x1A7C0, 0x1AA82, 0x1AA90, 0x1AAA7, 0x1AAAF, 0x1AAB7, 0x1AABD, 0x1A8D7, 0x1A8E4, 0x1A8C9,
       0x1A8F6, 0x1A908, 0x1A91D}},
     {{0x26A4, 0x02DC, 0x02EC, 0x02FC, 0x0300, 0x0308, 0x0310, 0x0354, 0x0369},
      {0xF349, 0xAC73, 0xAC79, 0xAC83, 0xAC88, 0xAC8E, 0xAC94,
       0xB04C, 0xB069, 0xB06F, 0xB446,
       0x16358, 0x16364, 0x1636C, 0x1637E, 0x1638A,
       0xB514, 0xB51B, 0xB527, 0xB530, 0x39ECF, 0xB72B, 0xB737, 0xB73D, 0xB745, 0xB74D,
       0x135B9, 0x135C5, 0x135D4, 0x135DC, 0x135EB, 0x135FF, 0x1360C, 0x13614, 0x1361D, 0x13627,
       0x13632, 0x1363F, 0x13649, 0x13676,
       0xAEE3, 0xAEFA, 0x14068, 0x14071, 0x16279, 0xBA77, 0xCD9D, 0xCDAF,
       0x3A9F8, 0x1429E, 0x380BA, 0x380C4, 0x380C7,
       0x665F, 0x6673, 0x66A3, 0x66B6, 0x66EA, 0x6703, 0x6716, 0x73CF, 0x7405,
       0x6B93, 0x6B9A, 0x6CA1, 0x6CB0, 0x6CB6, 0x6CC3, 0x6CD8,
       0x6CF0, 0x6D10, 0x699B, 0x69A3,
       0x36F1F, 0x36F26, 0x36F39, 0x36F52, 0x36F5C, 0x36F66, 0x36F77, 0x36F81, 0x36F99,
       0x372F5, 0x3442D, 0x3443B, 0x15AC6, 0x15AB6, 0x14556, 0x14566, 0x14570,
       0x1457D, 0x15583, 0x2F27A, 0x2F288, 0x30788, 0x10AE9, 0x32F30, 0x35E53, 0x106C3},
      {0x03, 0x0B, 0x0D, 0x15, 0x17, 0x1B, 0x1F, 0x23, 0x28, 0x33, 0x34, 0x35, 0x3A, 0x4D, 0x5B, 0x88, 0x8E,
       0x90},
      {{0x33, 0x34, 0x35, 0x1F}, 0x01, 0x02, 0x31, 0x27, 0x2A, 0x19, 0x08, 0x09, 0x1C,
       {0x07, 0x08, 0x23, 0x24, 0x25, 0x61}, 0x0B, 0x8E, 0x1E,
       {9, 21, 100, 28, 31, 73}, {2, 7, 14}, {85, 86}, {47, 98, 101}},
      kCurseFight, sizeof kCurseFight / sizeof kCurseFight[0]},
     {0x29A2, 0x29B3, 0xA28D, 0xA295, 0xA2A2, 0x3222},
     &kCurseRandom, 0x0830,
     {0x6990, 0x04A2, 0x12BB, 0x04A2, 0x054A, 21, 0x2F, 0x33}},
};

} // namespace

const char* program_name(games::Game g)
{
    for (const Profile& p : kProfiles)
        if (p.game == g) return p.program;
    return nullptr;
}

const Profile* find(games::Game g, uint32_t program_size, uint32_t image_size)
{
    for (const Profile& p : kProfiles)
        if (p.game == g && p.program_size == program_size && p.image_size == image_size) return &p;
    return nullptr;
}

} // namespace profile

#include "profile.h"

#include "text.h"

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
constexpr uint32_t kJ = text::kJoined;      // a word made of two (the profile's joined words)
constexpr spells::CampSpell kCurseCamp[] = {
    {0x01, Does::Affect, 0, 0, 0, 0x2FD0A},        // Bless: "is Blessed"
    {0x03, Does::Heal, 1, 8, 0, 0},                // Cure Light Wounds
    {0x05, Does::Affect, 0, 0, 0, 0x2FDE3},        // Detect Magic: "is affected"
    {0x06, Does::Affect, 0, 0, 0, 0x2FE1C},        // Protection from Evil: "is protected"
    {0x07, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x08, Does::Affect, 0, 0, 0, 0x2FE56},        // Resist Cold: "is cold-resistant"
    {0x0B, Does::Affect, 0, 0, 0, 0x2FDE3},
    {0x0C, Does::Enlarge, 0, 0, 0, 0x2FFA0},       // Enlarge: "is stronger" (spell_facts.md)
    {0x0D, Does::Reduce, 0, 0, 0, 0x300DE},        // Reduce: "has been reduced"
    {0x0E, Does::Friends, 0, 0, 0, 0x30183},       // Friends: "is friendly"
    {0x10, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x11, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x12, Does::Affect, 0, 0, 0, 0x2FDE3},        // Read Magic
    {0x13, Does::Affect, 0, 0, 0, 0x3022D},        // Shield: "is shielded"
    {0x16, Does::Affect, 0, 0, 0, 0x2FDE3},        // Find Traps
    {0x18, Does::Affect, 0, 0, 0, 0x3044A},        // Resist Fire: "is fire resistant"
    {0x1A, Does::SlowPoison, 0, 0, 0, 0x2FDE3},
    {0x1C, Does::Hammer, 0, 0, 0, 0x1045C},        // Spiritual Hammer: "Gains an item"
    {0x1D, Does::Affect, 0, 0, 0, 0x2FDE3},        // Detect Invisibility
    {0x1E, Does::Affect, 0, 0, 0, 0x3067C},        // Invisibility: "is invisible"
    {0x20, Does::Mirror, 0, 0, 0, 0x306EF},        // Mirror Image: "is duplicated"
    {0x23, Does::Strength, 0, 0, 0, 0},            // Strength (no word)
    {0x25, Does::CureBlind, 0, 0, 0, 0},
    {0x27, Does::CureDisease, 0, 0, 0, 0},
    {0x29, Does::Dispel, 0, 0, 0, 0x3118D},        // Dispel Magic: "is affected"
    {0x2A, Does::Prayer, 0, 0, 0, 0x31544},        // Prayer: "is praying"
    {0x2B, Does::RemoveCurse, 0, 0, 0, 0},
    {0x2E, Does::Dispel, 0, 0, 0, 0x3118D},
    {0x30, Does::Haste, 0, 0, 0, 0x31907},         // Haste: "is Hasted"
    {0x32, Does::Affect, 0, 0, 0, 0x3067C},        // Invisibility 10' Radius
    {0x34, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x35, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x36, Does::Affect, 0, 0, 0, 0x2FE1C},        // Protection from Normal Missiles
    {0x38, Does::Restore, 0, 0, 0, 0x31D11},       // Restoration: "is restored"
    {0x3A, Does::Heal, 2, 8, 1, 0},                // Cure Serious Wounds
    {0x3E, Does::Heal, 2, 4, 2, 0x3206D},          // the items' healing: "is Healed"
    {0x63, Does::Heal, 2, 4, 2, 0x3379C},
    {0x43, Does::Neutralize, 0, 0, 0, 0},
    {0x45, Does::Affect, 0, 0, 0, 0x2FE1C},
    {0x47, Does::Heal, 3, 8, 3, 0},                // Cure Critical Wounds
    {0x4B, Does::Raise, 0, 0, 0, 0},
    {0x4D, Does::Affect, 0, 0, 0, 0x2FDE3},
    {0x50, Does::Affect, 0, 0, 0, 0x3274C},        // Invisibility to Animals: "is invisible" (spell_facts.md)
    {0x55, Does::FireShield, 0, 0, 0, 0x32BD4},    // Fire Shield: "flame type: Hot Cold"; hot "is protected"
    {0x58, Does::Affect, 0, 0, 0, 0x2FE1C},        // Minor Globe of Invulnerability
    // The items' own spells (curse_finish_facts.md 10)
    {0x39, Does::Haste, 0, 0, 0, 0x31ED7},         // speed: a slowed one cured, else "is Speedy"
    {0x3B, Does::GiantStrength, 0, 0, 0, 0x31F60}, // giant strength: "is stronger"
    {0x3F, Does::Affect, 0, 0, 0, 0x320C4},        // the party invisible: "is invisible"
    {0x5F, Does::Affect, 0, 0, 0, 0},              // protection from dragon breath (nothing said)
    {0x60, Does::Affect, 0, 0, 0, 0},              // ... from paralysis
    {0x61, Does::Affect, 0, 0, 0, 0},              // invisibility
    {0x59, Does::RemoveCurse, 0, 0, 0, 0},
};

// Secret of the Silver Blades: Curse's spells in camp (the same numbers);
// its words where its GAME.OVR has the same text (its results are worded
// otherwise - most say nothing yet)
constexpr spells::CampSpell kSecretCamp[] = {
    {0x01, Does::Affect, 0, 0, 0, kJ | 0},        // Bless: "is Blessed"
    {0x03, Does::Heal, 1, 8, 0, 0},                // Cure Light Wounds
    {0x05, Does::Affect, 0, 0, 0, kJ | 2},        // Detect Magic: "is affected"
    {0x06, Does::Affect, 0, 0, 0, kJ | 3},        // Protection from Evil: "is protected"
    {0x07, Does::Affect, 0, 0, 0, kJ | 3},
    {0x08, Does::Affect, 0, 0, 0, kJ | 4},        // Resist Cold: "is cold-resistant"
    {0x0B, Does::Affect, 0, 0, 0, kJ | 2},
    {0x0C, Does::Enlarge, 0, 0, 0, kJ | 7},       // Enlarge: "is stronger" (spell_facts.md)
    {0x0D, Does::Reduce, 0, 0, 0, 0x2E930},        // Reduce: "has been reduced"
    {0x0E, Does::Friends, 0, 0, 0, kJ | 8},       // Friends: "is friendly"
    {0x10, Does::Affect, 0, 0, 0, kJ | 3},
    {0x11, Does::Affect, 0, 0, 0, kJ | 3},
    {0x12, Does::Affect, 0, 0, 0, kJ | 2},        // Read Magic
    {0x13, Does::Affect, 0, 0, 0, kJ | 9},        // Shield: "is shielded"
    {0x16, Does::Affect, 0, 0, 0, kJ | 2},        // Find Traps
    {0x18, Does::Affect, 0, 0, 0, kJ | 12},        // Resist Fire: "is fire resistant"
    {0x1A, Does::SlowPoison, 0, 0, 0, kJ | 2},
    {0x1C, Does::Hammer, 0, 0, 0, 0},        // Spiritual Hammer: "Gains an item"
    {0x1D, Does::Affect, 0, 0, 0, kJ | 2},        // Detect Invisibility
    {0x1E, Does::Affect, 0, 0, 0, kJ | 14},        // Invisibility: "is invisible"
    {0x20, Does::Mirror, 0, 0, 0, kJ | 15},        // Mirror Image: "is duplicated"
    {0x23, Does::Strength, 0, 0, 0, 0},            // Strength (no word)
    {0x25, Does::CureBlind, 0, 0, 0, 0},
    {0x27, Does::CureDisease, 0, 0, 0, 0},
    {0x29, Does::Dispel, 0, 0, 0, kJ | 2},        // Dispel Magic: "is affected"
    {0x2A, Does::Prayer, 0, 0, 0, kJ | 19},        // Prayer: "is praying"
    {0x2B, Does::RemoveCurse, 0, 0, 0, 0},
    {0x2E, Does::Dispel, 0, 0, 0, kJ | 2},
    {0x30, Does::Haste, 0, 0, 0, kJ | 22},         // Haste: "is Hasted"
    {0x32, Does::Affect, 0, 0, 0, kJ | 14},        // Invisibility 10' Radius
    {0x34, Does::Affect, 0, 0, 0, kJ | 3},
    {0x35, Does::Affect, 0, 0, 0, kJ | 3},
    {0x36, Does::Affect, 0, 0, 0, kJ | 3},        // Protection from Normal Missiles
    {0x3A, Does::Heal, 2, 8, 1, 0},                // Cure Serious Wounds
    {0x3E, Does::Heal, 2, 4, 2, kJ | 25},          // the items' healing: "is Healed"
    {0x63, Does::Heal, 2, 4, 2, kJ | 25},
    {0x43, Does::Neutralize, 0, 0, 0, 0},
    {0x45, Does::Affect, 0, 0, 0, kJ | 3},
    {0x47, Does::Heal, 3, 8, 3, 0},                // Cure Critical Wounds
    {0x4B, Does::Raise, 0, 0, 0, 0},
    {0x4D, Does::Affect, 0, 0, 0, kJ | 2},
    {0x50, Does::Affect, 0, 0, 0, kJ | 29},        // Invisibility to Animals: "is invisible" (spell_facts.md)
    {0x55, Does::FireShield, 0, 0, 0, kJ | 31},    // Fire Shield: "flame type: Hot Cold"; hot "is protected"
    {0x58, Does::Affect, 0, 0, 0, kJ | 3},        // Minor Globe of Invulnerability
    // The items' own spells (curse_finish_facts.md 10)
    {0x39, Does::Haste, 0, 0, 0, kJ | 22},         // speed: a slowed one cured, else "is Speedy"
    {0x3B, Does::GiantStrength, 0, 0, 0, kJ | 34}, // giant strength: "is stronger"
    {0x3F, Does::Youth, 1, 4, 1, 0x30803, 0x307EF},      // the Elixir of Youth
    {0x5F, Does::Affect, 0, 0, 0, kJ | 3},              // protection from dragon breath (nothing said)
    {0x61, Does::Affect, 0, 0, 0, 0},              // invisibility
    {0x59, Does::RemoveCurse, 0, 0, 0, 0},
    // Secret's own (secret_facts_2.md 14)
    {0x24, Does::FullHeal, 1, 4, 0, kJ | 25, kJ | 36, kJ | 35, 0x2F687},   // Heal
    {0x5A, Does::FullHeal, 1, 4, 0, kJ | 25, kJ | 36, kJ | 35, 0x2F687},   // Barkskin (the original: Heal's routine)
    {0x62, Does::Heal, 1, 8, 0, 0},                                        // the druids' Cure Light Wounds
    {0x70, Does::Affect, 0, 0, 0, kJ | 3},                                 // Globe of Invulnerability
    {0x71, Does::Unstone, 0, 0, 0, kJ | 43, kJ | 46},                      // Stone to Flesh
    {0x74, Does::Affect, 0, 0, 0, kJ | 29},                                // Mass Invisibility
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
    {0x30, SpellDoes::Haste, 0, 0, 0, 0, 0, 0x31907, 0x36666},     // Haste: "is Hasted"; a slowed one "is Cured"
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
    {0x51, SpellDoes::Charm, 0, 0, 0, 0, 1, 0x32786},       // Charm Monsters: "is charmed" (the last picked, non-persons
                                                            // and large ones unaffected - spell_facts.md 1.10)
    {0x54, SpellDoes::Fear, 0, 0, 0, 0, 0, 0x32A7A},        // Fear: "runs in terror" (a cone, 3 rays, 6 squares)
    {0x5B, SpellDoes::Poison, 0, 0, 0, 0, 0, 0x35E53},      // Cloudkill: "is Poisoned" (a 3 x 3 cloud)
    {0x5C, SpellDoes::Cone, 0, 4, 0, 6, 0x0A, 0},           // Cone of Cold: level d4 + level (a cone, 2 rays; cold
                                                            // and magic - spell_facts.md: coab says acid)
    {0x26, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x30FB6},      // Cause Blindness: "is blind"
    {0x28, SpellDoes::Affect, 1, 6, 0, 4, 0, 0x310A2},      // Cause Disease: "is diseased", d6 x 10 minutes
                                                            // (then the disease's course: combat::tick,
                                                            // rules::disease_clock)
    {0x33, SpellDoes::Bolt, 0, 6, 0, 3, 0x0C, 0, 0, 0, 7},  // Lightning Bolt: level d6, 7 squares (electricity and
                                                            // magic; curse_finish_facts.md 2)
    {0x37, SpellDoes::Slow, 0, 0, 0, 0, 0, 0x31CD9, 0x36666},       // Slow: "is Slowed"; a hasted one "is Cured"
    {0x5E, SpellDoes::Hold, 0, 0, 0, 0, 0, 0x303F1},        // Hold Monster
    // spell_facts.md (v0.55.0)
    {0x19, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x30489},      // Silence 15' Radius: "is silenced"
    {0x1B, SpellDoes::SnakeCharm, 0, 0, 0, 0, 0, 0x30577},  // Snake Charm: "is charmed"
    {0x25, SpellDoes::CureBlind, 0, 0, 0, 0, 0, 0x30F71, 0x36666},   // Cure Blindness: "is Cured", "can see"
    {0x2C, SpellDoes::Theirs, 0, 0, 0, 0, 0, 0x316BF},      // Bestow Curse: "has been cursed!" (a touch)
    {0x2D, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x316FD},      // Blink: "is blinking"
    {0x44, SpellDoes::Kill, 0, 0, 0, 0, 0, 0x112D8},        // Poison: "is Poisoned", then "is killed"
    {0x46, SpellDoes::Snakes, 0, 0, 0, 0, 0, 0xFFAB, 0x322FD},   // Sticks to Snakes: "is fighting with snakes" /
                                                            // "smashes them flat"
    {0x4C, SpellDoes::Slay, 2, 8, 1, 0, 8, 0x3259B},        // Slay Living: "is slain"; saved 2d8 + 1
    {0x4E, SpellDoes::Entangle, 0, 0, 0, 0, 0, 0x32663},    // Entangle: "is entangled" (outdoors)
    {0x4F, SpellDoes::Faerie, 0, 0, 0, 0, 0, 0x3271D},      // Faerie Fire: "is highlighted"
    {0x50, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x3274C},      // Invisibility to Animals: "is invisible"
    {0x4D, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2FDE3},      // Detect Magic (the druids'): "is affected"
    {0x56, SpellDoes::Fumble, 0, 0, 0, 0, 0, 0x32D97, 0x32DA1},  // Fumble: "is clumsy" / "is slowed"
    {0x57, SpellDoes::Damage, 3, 10, 0, 0, 0x0A, 0},        // Ice Storm: 3d10 cold (and magic), radius 2
    {0x5D, SpellDoes::Feeble, 0, 0, 0, 0, 0, 0x11465},      // Feeblemind: "is stupid"
    // spell_facts.md (v0.62.0)
    {0x0C, SpellDoes::Enlarge, 0, 0, 0, 0, 0, 0x2FFA0},     // Enlarge: "is stronger"
    {0x0D, SpellDoes::Reduce, 0, 0, 0, 0, 0, 0x300DE},      // Reduce: "has been reduced"
    {0x2B, SpellDoes::RemoveCurse, 0, 0, 0, 0, 0, 0x31597, 0x36666, 0x315A4},   // Remove Curse: "is un-cursed",
    {0x59, SpellDoes::RemoveCurse, 0, 0, 0, 0, 0, 0x31597, 0x36666, 0x315A4},   // "is Cured", "has an item un-cursed"
    {0x29, SpellDoes::Dispel, 0, 0, 0, 0, 0, 0x3118D},      // Dispel Magic: "is affected"
    {0x2E, SpellDoes::Dispel, 0, 0, 0, 0, 0, 0x3118D},
    {0x49, SpellDoes::DispelEvil, 0, 0, 0, 0, 0, 0x32422},  // Dispel Evil: "is affected"
    {0x52, SpellDoes::Confuse, 0, 0, 0, 0, 0, 0x32816},     // Confusion: "is confused"
    {0x53, SpellDoes::Teleport, 0, 0, 0, 0, 0, 0x328DC},    // Dimension Door: "teleports"
    {0x55, SpellDoes::FireShield, 0, 0, 0, 0, 0, 0x32BD4},  // Fire Shield: "is protected" (hot)
    {0x64, SpellDoes::Affect, 0, 0, 0, 0, 0, 0},            // Bestow Curse (the magic-users'): no effect at all
    // (v0.63.0)
    {0x1A, SpellDoes::SlowPoison, 0, 0, 0, 0, 0, 0x304C2},  // Slow Poison: "is affected"
    {0x1C, SpellDoes::Hammer, 0, 0, 0, 0, 0, 0},            // Spiritual Hammer (the caller: "Gains an item")
    {0x24, SpellDoes::Animate, 0, 0, 0, 0, 0, 0x30DC2},     // Animate Dead: "is animated"
    {0x5A, SpellDoes::Animate, 0, 0, 0, 0, 0, 0x30DC2},
    {0x38, SpellDoes::Restore, 0, 0, 0, 0, 0, 0x31D11},     // Restoration: "is restored"
    // The items' own spells (the spell table's monster spells)
    {0x39, SpellDoes::Haste, 5, 4, 0, 0, 0, 0x31ED7, 0x36666},     // speed: "is Speedy", 5d4 rounds
    {0x3C, SpellDoes::Bolt, 1, 6, 20, 0, 4, 0, 0, 0, 3},    // a lightning stroke: 1d6 + 20, the path 20, 3 squares
    {0x3D, SpellDoes::Theirs, 5, 4, 0, 0, 0, 0x32033},      // paralysis: "is paralyzed", 5d4 rounds
    {0x3E, SpellDoes::Heal, 2, 4, 2, 0, 0, 0x3206D},        // healing: "is Healed"
    {0x3F, SpellDoes::Ours, 2, 10, 0, 4, 0, 0x320C4},       // the party invisible: "is invisible", 2d10 x 10
    {0x40, SpellDoes::Damage, 0, 6, 0, 5, 9, 0},            // a fireball of 3, 5 or 7 dice
    {0x41, SpellDoes::Damage, 2, 4, 2, 0, 8, 0},            // the wand's missile: 2d4 + 2
    {0x63, SpellDoes::Heal, 2, 4, 2, 0, 0, 0x3379C},        // extra healing: "is Healed"
    // (curse_finish_facts.md 10)
    {0x3B, SpellDoes::GiantStrength, 1, 4, 4, 4, 0, 0x31F60},   // giant strength: "is stronger", (d4 + 4) x 10
    {0x5F, SpellDoes::Affect, 0, 0, 0, 0, 0, 0},            // protection from dragon breath (nothing said)
    {0x60, SpellDoes::Affect, 0, 0, 0, 0, 0, 0},            // ... from paralysis
    {0x61, SpellDoes::Affect, 0, 0, 0, 0, 0, 0},            // invisibility
    {0x62, SpellDoes::Defoliate, 6, 6, 0, 0, 0, 0},         // defoliation: 6d6, a cone of 3
};

// Secret of the Silver Blades: Curse's spells 1-100 (the same numbers),
// what each does as Curse's; its words where Secret's GAME.OVR has the same
// text (its spells' results are worded otherwise - most say nothing yet)
constexpr combat::FightSpell kSecretFight[] = {
    {0x01, SpellDoes::Ours, 0, 0, 0, 0, 0, kJ | 0},        // Bless: "is Blessed"
    {0x02, SpellDoes::Theirs, 0, 0, 0, 0, 0, kJ | 1},      // Curse: "is Cursed"
    {0x03, SpellDoes::Heal, 1, 8, 0, 0, 0, 0},              // Cure Light Wounds
    {0x04, SpellDoes::Damage, 1, 8, 0, 0, 8, 0},            // Cause Light Wounds (a touch)
    {0x05, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 2},
    {0x06, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x07, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x08, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 4},
    {0x09, SpellDoes::Damage, 0, 0, 0, 1, 9, 0},            // Burning Hands: the caster's level
    {0x0B, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 2},
    {0x0F, SpellDoes::Damage, 0, 4, 0, 2, 8, 0},            // Magic Missile
    {0x10, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x11, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x13, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 9},      // Shield
    {0x14, SpellDoes::Damage, 1, 8, 0, 1, 0x0C, 0},         // Shocking Grasp: 1d8 + level
    {0x15, SpellDoes::Sleep, 0, 0, 0, 0, 0, kJ | 10},       // Sleep: "falls asleep"
    {0x17, SpellDoes::Hold, 0, 0, 0, 0, 0, kJ | 11},        // Hold Person: "is held"
    {0x18, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 12},
    {0x1D, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 2},
    {0x1E, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 14},      // Invisibility
    {0x20, SpellDoes::Mirror, 0, 0, 0, 0, 0, kJ | 15},
    {0x2A, SpellDoes::Prayer, 0, 0, 0, 0, 0, kJ | 19},
    {0x2F, SpellDoes::Damage, 0, 6, 0, 3, 9, 0},            // Fireball: level d6
    {0x30, SpellDoes::Haste, 0, 0, 0, 0, 0, kJ | 22, 0},     // Haste: "is Hasted"; a slowed one "is Cured"
    {0x31, SpellDoes::Hold, 0, 0, 0, 0, 0, kJ | 11},
    {0x32, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 14},
    {0x34, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x35, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x36, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x3A, SpellDoes::Heal, 2, 8, 1, 0, 0, 0},
    {0x42, SpellDoes::Damage, 2, 8, 1, 0, 8, 0},            // Cause Serious Wounds
    {0x45, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x47, SpellDoes::Heal, 3, 8, 3, 0, 0, 0},
    {0x48, SpellDoes::Damage, 3, 8, 3, 0, 8, 0},            // Cause Critical Wounds
    {0x4A, SpellDoes::Damage, 6, 8, 0, 0, 9, 0},            // Flame Strike
    {0x58, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},
    {0x21, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 16},      // Ray of Enfeeblement: "is weakened"
    {0x22, SpellDoes::Cloud, 0, 0, 0, 0, 0, 0x36A08, 0x369F8},  // Stinking Cloud: "chokes and gags from nausea" / "starts to cough"
    {0x0A, SpellDoes::Charm, 0, 0, 0, 0, 0, kJ | 5, kJ | 6},  // Charm Person: "is charmed" / "is unaffected"
    {0x51, SpellDoes::Charm, 0, 0, 0, 0, 1, kJ | 5},       // Charm Monsters: "is charmed" (the last picked, non-persons
                                                            // and large ones unaffected - spell_facts.md 1.10)
    {0x54, SpellDoes::Fear, 0, 0, 0, 0, 0, 0x3109E},        // Fear: "runs in terror" (a cone, 3 rays, 6 squares)
    {0x5B, SpellDoes::Poison, 0, 0, 0, 0, 0, 0x36A24},      // Cloudkill: "is Poisoned" (a 3 x 3 cloud)
    {0x5C, SpellDoes::Cone, 0, 4, 0, 6, 0x0A, 0},           // Cone of Cold: level d4 + level (a cone, 2 rays; cold
                                                            // and magic - spell_facts.md: coab says acid)
    {0x26, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 17},      // Cause Blindness: "is blind"
    {0x28, SpellDoes::Affect, 1, 6, 0, 4, 0, kJ | 18},      // Cause Disease: "is diseased", d6 x 10 minutes
                                                            // (then the disease's course: combat::tick,
                                                            // rules::disease_clock)
    {0x33, SpellDoes::Bolt, 0, 6, 0, 3, 0x0C, 0, 0, 0, 7},  // Lightning Bolt: level d6, 7 squares (electricity and
                                                            // magic; curse_finish_facts.md 2)
    {0x37, SpellDoes::Slow, 0, 0, 0, 0, 0, kJ | 23, 0},       // Slow: "is Slowed"; a hasted one "is Cured"
    {0x5E, SpellDoes::Hold, 0, 0, 0, 0, 0, kJ | 11},        // Hold Monster
    // spell_facts.md (v0.55.0)
    {0x19, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 13},      // Silence 15' Radius: "is silenced"
    {0x1B, SpellDoes::SnakeCharm, 0, 0, 0, 0, 0, kJ | 5},  // Snake Charm: "is charmed"
    {0x25, SpellDoes::CureBlind, 0, 0, 0, 0, 0, 0x2F808, 0},   // Cure Blindness: "is Cured", "can see"
    {0x2C, SpellDoes::Theirs, 0, 0, 0, 0, 0, 0x2FEBB},      // Bestow Curse: "has been cursed!" (a touch)
    {0x2D, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 21},      // Blink: "is blinking"
    {0x44, SpellDoes::Kill, 0, 0, 0, 0, 0, 0x36A24},        // Poison: "is Poisoned", then "is killed"
    {0x46, SpellDoes::Snakes, 0, 0, 0, 0, 0, 0x30A9D, 0x30AB7},  // Sticks to Snakes: the caster "turns sticks into
                                                            // snakes" / "smashes them flat"
    {0x4C, SpellDoes::Slay, 2, 8, 1, 0, 8, kJ | 27},        // Slay Living: "is slain"; saved 2d8 + 1
    {0x4E, SpellDoes::Entangle, 0, 0, 0, 0, 0, 0},    // Entangle: "is entangled" (outdoors)
    {0x4F, SpellDoes::Faerie, 0, 0, 0, 0, 0, kJ | 28},      // Faerie Fire: "is highlighted"
    {0x50, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 29},      // Invisibility to Animals: "is invisible"
    {0x4D, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 2},      // Detect Magic (the druids'): "is affected"
    {0x56, SpellDoes::Fumble, 0, 0, 0, 0, 0, kJ | 32, kJ | 23, kJ | 33},  // Fumble: "is clumsy" / "is slowed" /
                                                            // the first failed save "is Befuddled"
    {0x57, SpellDoes::Damage, 3, 10, 0, 0, 0x0A, 0},        // Ice Storm: 3d10 cold (and magic), radius 2
    {0x5D, SpellDoes::Feeble, 0, 0, 0, 0, 0, 0},      // Feeblemind: "is stupid"
    // spell_facts.md (v0.62.0)
    {0x0C, SpellDoes::Enlarge, 0, 0, 0, 0, 0, kJ | 7},     // Enlarge: "is stronger"
    {0x0D, SpellDoes::Reduce, 0, 0, 0, 0, 0, 0x2E930},      // Reduce: "has been reduced"
    {0x2B, SpellDoes::RemoveCurse, 0, 0, 0, 0, 0, kJ | 20, 0, 0x2FDA0},   // Remove Curse: "is un-cursed",
    {0x59, SpellDoes::RemoveCurse, 0, 0, 0, 0, 0, kJ | 20, 0, 0x2FDA0},   // "is Cured", "has an item un-cursed"
    {0x29, SpellDoes::Dispel, 0, 0, 0, 0, 0, kJ | 2},      // Dispel Magic: "is affected"
    {0x2E, SpellDoes::Dispel, 0, 0, 0, 0, 0, kJ | 2},
    {0x49, SpellDoes::DispelEvil, 0, 0, 0, 0, 0, kJ | 2},  // Dispel Evil: "is affected"
    {0x52, SpellDoes::Confuse, 0, 0, 0, 0, 0, kJ | 30},     // Confusion: "is confused"
    {0x53, SpellDoes::Teleport, 0, 0, 0, 0, 0, 0x3100B},    // Dimension Door: "teleports"
    {0x55, SpellDoes::FireShield, 0, 0, 0, 0, 0, kJ | 31},  // Fire Shield: "is protected" (hot)
    {0x64, SpellDoes::Affect, 0, 0, 0, 0, 0, 0},            // Bestow Curse (the magic-users'): no effect at all
    // (v0.63.0)
    {0x1A, SpellDoes::SlowPoison, 0, 0, 0, 0, 0, kJ | 2},  // Slow Poison: "is affected"
    {0x1C, SpellDoes::Hammer, 0, 0, 0, 0, 0, 0},            // Spiritual Hammer (the caller: "Gains an item")
    {0x24, SpellDoes::FullHeal, 1, 4, 0, 0, 0, kJ | 25, kJ | 36, kJ | 35, 0, 0x2F687},  // Secret's Heal: "is Healed",
                                                            // "is no longer blind", "is cured", "regains intellect"
    {0x5A, SpellDoes::FullHeal, 1, 4, 0, 0, 0, kJ | 25, kJ | 36, kJ | 35, 0, 0x2F687},  // Barkskin: the original runs
                                                            // Heal's routine (no effect given)
    {0x38, SpellDoes::HarmTouch, 1, 4, 0, 0, 8, kJ | 18},       // Harm: "is Diseased", down to 1d4 hit points (magic -
                                                                // GAME.OVR 0x30560)
    // The items' own spells (the spell table's monster spells)
    {0x39, SpellDoes::Haste, 5, 4, 0, 0, 0, kJ | 22, 0},     // speed: "is Speedy", 5d4 rounds
    {0x3C, SpellDoes::Bolt, 1, 6, 20, 0, 4, 0, 0, 0, 3},    // a lightning stroke: 1d6 + 20, the path 20, 3 squares
    {0x3D, SpellDoes::Theirs, 5, 4, 0, 0, 0, kJ | 24},      // paralysis: "is paralyzed", 5d4 rounds
    {0x3E, SpellDoes::Heal, 2, 4, 2, 0, 0, kJ | 25},        // healing: "is Healed"
    {0x3F, SpellDoes::Youth, 1, 4, 1, 0, 0, 0x30803, 0x307EF},  // the Elixir of Youth: "becomes younger" /
                                                            // "is now 18 years old"
    {0x40, SpellDoes::Damage, 0, 6, 0, 5, 9, 0},            // a fireball of 3, 5 or 7 dice
    {0x41, SpellDoes::Damage, 2, 4, 2, 0, 8, 0},            // the wand's missile: 2d4 + 2
    {0x63, SpellDoes::Heal, 2, 4, 2, 0, 0, kJ | 25},        // extra healing: "is Healed"
    // (curse_finish_facts.md 10)
    {0x3B, SpellDoes::GiantStrength, 1, 4, 4, 4, 0, kJ | 34},   // giant strength: "is stronger", (d4 + 4) x 10
    {0x5F, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},            // protection from dragon breath (nothing said)
    {0x60, SpellDoes::Charm, 0, 0, 0, 0, 0, kJ | 5, kJ | 39},  // Charm Person or Mammal: "is Charmed" / "is not
                                                            // Charmed" (persons; mammals not told apart)
    {0x61, SpellDoes::Affect, 0, 0, 0, 0, 0, 0},            // invisibility
    {0x62, SpellDoes::Heal, 1, 8, 0, 0, 0, 0},              // the druids' Cure Light Wounds
    // Its 6th / 7th level spells past 100 (secret_facts_2.md 19; 101-109 have no workings)
    {0x6E, SpellDoes::DeathSpell, 0, 0, 0, 0, 8, 0x31F25},  // Death Spell: "drops dead"
    {0x6F, SpellDoes::Disintegrate, 0, 0, 0, 0, 8, 0x3204E},    // Disintegrate: "disintegrates"
    {0x70, SpellDoes::Affect, 0, 0, 0, 0, 0, kJ | 3},       // Globe of Invulnerability: "is Protected"
    {0x71, SpellDoes::Unstone, 0, 0, 0, 0, 0, kJ | 43},     // Stone to Flesh: "is no longer a statue"
    {0x72, SpellDoes::Petrify, 0, 0, 0, 0, 8, kJ | 44},     // Flesh to Stone: "is turned to stone"
    {0x73, SpellDoes::Damage, 0, 6, 0, 6, 9, 0},            // Delayed Blast Fireball: level d6 + level (at once)
    {0x74, SpellDoes::Ours, 0, 0, 0, 0, 0, kJ | 29},        // Mass Invisibility: "is Invisible (spell)"
    {0x75, SpellDoes::Stun, 0, 0, 0, 0, 0, kJ | 45},        // Power Word Stun: "is STUNNED"
};

// Pool's spells in fights and in camp (poolrad_combat_facts.md 3; the
// words: Pool's GAME.OVR strings the same code prints): Curse's that Pool
// has (Magic Missile as AD&D's: (level + 1) / 2 missiles of 1d4 + 1 - Tom,
// 2026-10-11; Pool's code was read as level / 2), a Lightning Bolt
// of 8, its item spells
constexpr combat::FightSpell kPoolFight[] = {
    {0x01, SpellDoes::Ours, 0, 0, 0, 0, 0, 0x2801C},        // Bless: "is Blessed"
    {0x02, SpellDoes::Theirs, 0, 0, 0, 0, 0, 0x2804E},      // Curse: "is Cursed"
    {0x03, SpellDoes::Heal, 1, 8, 0, 0, 0, 0},              // Cure Light Wounds
    {0x04, SpellDoes::Damage, 1, 8, 0, 0, 8, 0},            // Cause Light Wounds (a touch)
    {0x05, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x280F5},
    {0x06, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2812E},
    {0x07, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2812E},
    {0x08, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x28168},
    {0x09, SpellDoes::Damage, 0, 0, 0, 1, 9, 0},            // Burning Hands: the caster's level
    {0x0B, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x280F5},
    {0x0F, SpellDoes::Damage, 0, 4, 0, 2, 8, 0},            // Magic Missile: (level + 1) / 2 missiles (Tom: the AD&D rule)
    {0x10, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2812E},
    {0x11, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2812E},
    {0x13, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x284A5},      // Shield
    {0x14, SpellDoes::Damage, 1, 8, 0, 1, 0x0C, 0},         // Shocking Grasp: 1d8 + level
    {0x15, SpellDoes::Sleep, 0, 0, 0, 0, 0, 0x28526},       // Sleep: "falls asleep"
    {0x17, SpellDoes::Hold, 0, 0, 0, 0, 0, 0x28668},        // Hold Person: "is held"
    {0x18, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x287EF},
    {0x1D, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x280F5},
    {0x1E, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x28A21},      // Invisibility
    {0x20, SpellDoes::Mirror, 0, 0, 0, 0, 0, 0x28A94},
    {0x2A, SpellDoes::Prayer, 0, 0, 0, 0, 0, 0x294E3},
    {0x2F, SpellDoes::Damage, 0, 6, 0, 3, 9, 0},            // Fireball: level d6
    {0x30, SpellDoes::Haste, 0, 0, 0, 0, 0, 0x298A0, 0x2BF62},
    {0x31, SpellDoes::Hold, 0, 0, 0, 0, 0, 0x28668},
    {0x32, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x28A21},
    {0x34, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2812E},
    {0x35, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2812E},
    {0x36, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2812E},
    {0x21, SpellDoes::Affect, 0, 0, 0, 0, 0, 0xFD2D},       // Ray of Enfeeblement: "is weakened"
    {0x22, SpellDoes::Cloud, 0, 0, 0, 0, 0, 0x2B9B5, 0x2B9A5},  // Stinking Cloud
    {0x0A, SpellDoes::Charm, 0, 0, 0, 0, 0, 0x10AC7, 0x281DC},  // Charm Person: "is charmed" / "is unaffected"
    {0x26, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x2926C},      // Cause Blindness: "is blind"
    {0x28, SpellDoes::Affect, 1, 6, 0, 4, 0, 0x29358},      // Cause Disease: "is diseased", d6 x 10 minutes
    {0x33, SpellDoes::Bolt, 0, 6, 0, 3, 0x0C, 0, 0, 0, 8},  // Lightning Bolt: level d6, 8 squares (Pool)
    {0x37, SpellDoes::Slow, 0, 0, 0, 0, 0, 0x29C17, 0x2BF62},
    {0x19, SpellDoes::Affect, 0, 0, 0, 0, 0, 0xF35E},       // Silence 15' Radius
    {0x1B, SpellDoes::SnakeCharm, 0, 0, 0, 0, 0, 0x10AC7},
    {0x25, SpellDoes::CureBlind, 0, 0, 0, 0, 0, 0x29227, 0x2BF62},
    {0x2C, SpellDoes::Theirs, 0, 0, 0, 0, 0, 0x2960B},      // Bestow Curse (a touch)
    {0x2D, SpellDoes::Affect, 0, 0, 0, 0, 0, 0x29649},      // Blink
    {0x0C, SpellDoes::Enlarge, 0, 0, 0, 0, 0, 0x11BA6},
    {0x0D, SpellDoes::Reduce, 0, 0, 0, 0, 0, 0x28375},
    {0x2B, SpellDoes::RemoveCurse, 0, 0, 0, 0, 0, 0x29536, 0x2BF62, 0},
    {0x29, SpellDoes::Dispel, 0, 0, 0, 0, 0, 0x280F5},
    {0x2E, SpellDoes::Dispel, 0, 0, 0, 0, 0, 0x280F5},
    {0x1A, SpellDoes::SlowPoison, 0, 0, 0, 0, 0, 0x280F5},
    {0x1C, SpellDoes::Hammer, 0, 0, 0, 0, 0, 0},
    {0x24, SpellDoes::Animate, 0, 0, 0, 0, 0, 0x2907B},
    {0x38, SpellDoes::Restore, 0, 0, 0, 0, 0, 0x29C4F},
    // The items' own spells
    {0x39, SpellDoes::Haste, 5, 4, 0, 0, 0, 0x29E10, 0x2BF62},
    {0x3A, SpellDoes::Heal, 1, 4, 8, 0, 1, 0x29E5B},        // Pool's: the poison taken away (kind 1), else d4 + 8, "is Healed"
    {0x3B, SpellDoes::GiantStrength, 1, 4, 4, 4, 0, 0x29EF1},   // giant strength: "is stronger", (d4 + 4) x 10
    {0x3C, SpellDoes::Bolt, 1, 6, 20, 0, 4, 0, 0, 0, 3},
    {0x3D, SpellDoes::Theirs, 5, 4, 0, 0, 0, 0x29FA4},
    {0x3E, SpellDoes::Heal, 2, 4, 2, 0, 0, 0x29E5B},
    {0x3F, SpellDoes::Ours, 2, 10, 0, 4, 0, 0x28A21},
    {0x40, SpellDoes::Damage, 0, 6, 0, 5, 9, 0},
    {0x41, SpellDoes::Damage, 2, 4, 2, 0, 8, 0},
};
constexpr spells::CampSpell kPoolCamp[] = {
    {0x01, Does::Affect, 0, 0, 0, 0x2801C},
    {0x03, Does::Heal, 1, 8, 0, 0},
    {0x05, Does::Affect, 0, 0, 0, 0x280F5},
    {0x06, Does::Affect, 0, 0, 0, 0x2812E},
    {0x07, Does::Affect, 0, 0, 0, 0x2812E},
    {0x08, Does::Affect, 0, 0, 0, 0x28168},
    {0x0B, Does::Affect, 0, 0, 0, 0x280F5},
    {0x0C, Does::Enlarge, 0, 0, 0, 0x11BA6},
    {0x0D, Does::Reduce, 0, 0, 0, 0x28375},
    {0x0E, Does::Friends, 0, 0, 0, 0x283E4},
    {0x10, Does::Affect, 0, 0, 0, 0x2812E},
    {0x11, Does::Affect, 0, 0, 0, 0x2812E},
    {0x12, Does::Affect, 0, 0, 0, 0x280F5},
    {0x13, Does::Affect, 0, 0, 0, 0x284A5},
    {0x16, Does::Affect, 0, 0, 0, 0x280F5},
    {0x18, Does::Affect, 0, 0, 0, 0x287EF},
    {0x1A, Does::SlowPoison, 0, 0, 0, 0x280F5},
    {0x1C, Does::Hammer, 0, 0, 0, 0xF42D},
    {0x1D, Does::Affect, 0, 0, 0, 0x280F5},
    {0x1E, Does::Affect, 0, 0, 0, 0x28A21},
    {0x20, Does::Mirror, 0, 0, 0, 0x28A94},
    {0x23, Does::Strength, 0, 0, 0, 0},
    {0x25, Does::CureBlind, 0, 0, 0, 0},
    {0x27, Does::CureDisease, 0, 0, 0, 0},
    {0x29, Does::Dispel, 0, 0, 0, 0x280F5},
    {0x2A, Does::Prayer, 0, 0, 0, 0x294E3},
    {0x2B, Does::RemoveCurse, 0, 0, 0, 0},
    {0x2E, Does::Dispel, 0, 0, 0, 0x280F5},
    {0x30, Does::Haste, 0, 0, 0, 0x298A0},
    {0x32, Does::Affect, 0, 0, 0, 0x28A21},
    {0x34, Does::Affect, 0, 0, 0, 0x2812E},
    {0x35, Does::Affect, 0, 0, 0, 0x2812E},
    {0x36, Does::Affect, 0, 0, 0, 0x2812E},
    {0x38, Does::Restore, 0, 0, 0, 0x29C4F},
    {0x39, Does::Haste, 0, 0, 0, 0x29E10},
    {0x3A, Does::HealOrUnpoison, 1, 4, 8, 0x29E5B}, // Pool's: the poison taken away, else d4 + 8 "is Healed"
    {0x3B, Does::GiantStrength, 0, 0, 0, 0x29EF1},  // giant strength: "is stronger" (ovr23+2EE5)
    {0x3E, Does::Heal, 2, 4, 2, 0x29E5B},
    {0x3F, Does::Affect, 0, 0, 0, 0x28A21},
    {0x43, Does::Reading, 0, 0, 0, 0x2A0AF},        // the Manual of Bodily Health: "is Reading" (ovr23+30A5)
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

// Random treasure (Secret; the facts in tools: GAME.OVR 0x2BD8, its
// create_item 0x34AB3, the rows at DS 0x0A2A, the scrolls' spells DS 0x585C
// / 0x580C): the kind by nested dice, names, weights and values by type
using treasure::Roll;
constexpr treasure::RollTable kSecretRolls[] = {
    {100, 7, {{31, 0, 1}, {62, 0x27, 0}, {70, 0x28, 0}, {80, 0x25, 0}, {85, 0x3B, 0}, {95, 0, 6}, {100, 0, 7}}},
    {100, 4, {{20, 0, 2}, {44, 0, 3}, {60, 0, 4}, {100, 0, 5}}},
    {10, 5, {{2, 0x06, 0}, {6, 0x08, 0}, {8, 0x46, 0}, {9, 0x0F, 0}, {10, 0x1D, 0}}},
    {3, 3, {{1, 0x05, 0}, {2, 0x0C, 0}, {3, 0x1E, 0}}},
    {20, 7, {{3, 0x07, 0}, {6, 0x18, 0}, {9, 0x16, 0}, {12, 0x19, 0}, {14, 0x17, 0}, {17, 0x1C, 0}, {20, 0x0F, 0}}},
    {4, 2, {{3, 0, 8}, {4, 0, 9}}},
    {20, 6, {{3, 0x1F, 0}, {11, 0x23, 0}, {16, 0x24, 0}, {17, 0x20, 0}, {18, 0x21, 0}, {20, 0x22, 0}}},
    {6, 4, {{2, 0x32, 0}, {4, 0x2F, 0}, {5, 0x34, 0}, {6, 0x35, 0}}},
    {20, 6, {{8, 0x12, 0}, {13, 0x11, 0}, {14, 0x0D, 0}, {15, 0x10, 0}, {18, 0x13, 0}, {20, 0x14, 0}}},
    {8, 6, {{1, 0x01, 0}, {3, 0x04, 0}, {5, 0x09, 0}, {6, 0x0A, 0}, {7, 0x0E, 0}, {8, 0x47, 0}}},
};
constexpr treasure::NameRule kSecretNames[] = {
    {0x1D, 0x1D, 0x1C, 4, 0, 0, 0},                // Staff Sling +n
    {0x1F, 0, 0x30, 4, 0, 0, 0},                   // Leather Armor +n
    {0x20, 0, 0x2F, 4, 0, 0, 0}, {0x21, 0, 0x2F, 4, 0, 0, 0}, {0x22, 0, 0x2F, 4, 0, 0, 0},
    {0x23, 0, 0x2F, 4, 0, 0, 0}, {0x24, 0, 0x2F, 4, 0, 0, 0},   // Ring / Scale / Chain / Banded / Plate Mail +n
    {0x32, 0, 0x5A, 6, 4, 0x6B, 0},                // Bracers of AC 6 (+4)
    {0x3B, 0x2E, 0x31, 6, 1, 0, 1},                // Ring Of Prot +1 (1 against saves)
};
constexpr uint8_t kSecretWeightType[] = {0x01, 0x11, 0x06, 0x0E, 0x0F, 0x17, 0x19, 0x25, 0x1D, 0x09, 0x08,
                                         0x10, 0x18, 0x46, 0x1F, 0x47, 0x04, 0x32, 0x05, 0x0A, 0x12, 0x16,
                                         0x07, 0x0D, 0x13, 0x14, 0x20, 0x21, 0x22, 0x23, 0x24, 0x1C, 0x3B};
constexpr uint16_t kSecretWeight[] = {75, 75, 50, 50, 50, 50, 50, 50, 50, 125, 100, 100, 100, 150, 150, 175, 10,
                                      10, 5, 60, 60, 80, 20, 40, 35, 250, 250, 400, 300, 350, 450, 1, 1};
static_assert(sizeof kSecretWeightType == sizeof kSecretWeight / 2, "weights");
constexpr uint8_t kSecretValueType[] = {0x0C, 0x05, 0x1E, 0x25, 0x20, 0x21, 0x22, 0x23, 0x24, 0x32};
constexpr uint16_t kSecretValue[] = {50, 50, 50, 1250, 1500, 1500, 1750, 2000, 2500, 1500};
constexpr treasure::SecretFacts kSecretMake = {
    kSecretRolls, sizeof kSecretRolls / sizeof kSecretRolls[0],
    0x01, 0x25, {0x32, 0x46, 0x47, 0x3B},
    10, {6, 9}, 0x6E,
    kSecretNames, sizeof kSecretNames / sizeof kSecretNames[0],
    {kSecretWeightType, kSecretWeight, sizeof kSecretWeightType, 4}, {0x05},
    {kSecretValueType, kSecretValue, sizeof kSecretValueType, 1000},
    0x27, 0x28, 3, 0x63, 0x27, 0x28, 0x26, 25, 150, 7, 6, 4, 0,
    {10, 7, 11, 10, 4, 5, 3}, {8, 7, 8, 6, 6, 2, 0},
    0x2F, {0, 0, 2, 2, 3, 3, 5, 5}, 0x34, 4, 0x35, 1, 0x07, 6,
    0x585C, 0x580C, 11, 10,
};
constexpr treasure::Facts kSecretRandom = [] {
    treasure::Facts f{};
    f.secret = &kSecretMake;
    return f;
}();

// Random treasure (Pool; poolrad_combat_facts.md 6, its ovr4 / ovr22 code):
// Curse's types, words, weights and values; 3 scroll bands of its own; a
// 20 makes +3 (bracers AC 2); 93-98 a d16 of its kinds, 99-100 type 0x45,
// each kind's ready-made row (some by a die) from its 19
constexpr treasure::Facts::Pick kPoolPicks[] = {
    {0x3F, 0, 0, {0x49}},
    {0x43, 0, 0, {0x91}},
    {0x45, 10, 0, {0x29, 0x79, 0x79, 0x79, 0x79, 0x79, 0x81, 0x81, 0x81, 0x81}},
    {0x4E, 3, 1, {0x31, 0x41, 0x71, 0}},                  // (d3 + 1: 2, 3, 4 - none)
    {0x4F, 0, 0, {0x39}},
    {0x54, 0, 0, {0x09}},
    {0x5C, 0, 0, {0x61}},
    {0x47, 11, 0, {0x11, 0x11, 0x11, 0x11, 0x11, 0x01, 0x01, 0x01, 0x19, 0x51, 0}},
    {0x46, 10, 0, {0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x89, 0x89, 0x89}},
};
constexpr treasure::Facts kPoolRandom = {
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
    {{13, 8}, {7, 28}, {11, 44}, {0, 0}, {0, 0}},
    {{8, 0}, {7, 21}, {9, 35}, {0, 0}, {0, 0}},
    3, true, 0xDC, true,
    16, {0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x47, 0x46, 0x46, 0x54, 0x4E, 0x4E, 0x4F, 0x4F, 0x5C, 0x43},
    0x45,
    kPoolPicks, sizeof kPoolPicks / sizeof kPoolPicks[0],
    0x90, 0x61,
};

// Pool's wilderness (poolrad_overland_facts.md): the map, the areas'
// first columns, the special squares and when they show their other icon
// (the area words, engine numbering), the healed land's colour maps, the
// bytes no step goes onto (sea, coast, the lake round the pyramid), the
// stinking river until the land is healed
constexpr uint8_t kPoolBlocked[] = {0x4E, 0x4E, 0x7E, 0x80, 0x82, 0x84, 0xA8, 0xAC, 0xAE, 0xB3,
                                    0xBC, 0xBC, 0xDD, 0xDE, 0xE1, 0xE9, 0xF5, 0xF5};
using OvOp = overland::Op;
constexpr overland::Facts kPoolOverland = {
    0x3648, 0x3C78, 0x0C36, {0x0900, 0x090B}, {0x092D, 0x093D}, {11, 18},
    0x4CA2, {0x66, 0x6C}, 0x72,
    kPoolBlocked, sizeof kPoolBlocked / 2,
    {{{{0, OvOp::Never, 0}}},
     {{{0, OvOp::Never, 0}}},
     {{{0x4C8C, OvOp::Ne, 0xFF}}},
     {{{0x4CD2, OvOp::Ne, 0}, {0x4C7C, OvOp::Bit, 8}, {0x4C9F, OvOp::Ne, 0}}},
     {{{0x4CA0, OvOp::NoBit, 2}}},
     {{{0, OvOp::Always, 0}}},
     {{{0x4CA0, OvOp::NoBit, 1}}},
     {{{0x4CB3, OvOp::Above, 0xFD}}},
     {{{0x4CB3, OvOp::Above, 0xFD}}}},
    0x16EBF,
};

// Title: picture 1 for 5 s; picture 2 with 3 on it at row 11, column 6 for
// 10 s; picture 4 at row 11 for 10 s (with sound 0x0D); the credits for
// 10 s.
constexpr TitleStep kCurseTitle[] = {
    {1, 0, 0, true, 5000, 0},
    {2, 0, 0, true, 0, 0},
    {3, 11, 6, false, 10000, 0},
    {4, 11, 0, false, 10000, 0x0D},
    {0, 0, 0, true, 10000, 0},
};

// Curse's ECL opcodes (operand counts as coab lists them)
constexpr int8_t K3 = ecl::kCount3, K2 = ecl::kCount2;
constexpr ecl::OpSet kCurseEcl = [] {
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

// Pool of Radiance, GOG release (the Steam / SNEG START.EXE is the same
// file): START.EXE 65,231 bytes, EXEPACK, unpacked 68,592; its data
// segment at image 0xC7C0 (poolrad_facts.md 1.1). Found from the player's
// files (poolrad_facts.md, screen_facts.md; facts only):
// - frames: boxes drawn by code - block 202's tile 20 at every corner and
//   junction, 21 down the sides, 22 along the tops and bottoms;
// - title: TITLE.DAX picture 1 for 5 s, picture 2 for 5 s with lightning
//   on colour 1 now and then, then the credits for 10 s (29-byte print
//   calls in GAME.OVR from 0x32C, the overlay's code at 0xD4; boxes down
//   to row 24 with a bar at row 4); no version line, no demo;
// - areas: ECL / GEO / WALLDEF / 8X8D 1-8; no SKY.DAX - the 3D view's
//   background is four flat bands: the sky / ceiling colour through the
//   table at DS 0x0C26 (area words 0x49FD / 0x49FE, as Curse's), then a
//   white line, a light grey band and a brown floor (each area's start);
// - scripts: Curse's opcodes 0x00-0x3D (ECL CLOCK reads 1 operand, ADD NPC
//   2), loaded at 0x9900 with its own memory map (ecl::vm_address);
// - a new game starts in ECL3 block 0.
constexpr uint32_t kP = text::kInProgram;     // a word in the program, not GAME.OVR

constexpr TitleStep kPoolTitle[] = {
    {1, 0, 0, true, 5000, 0},
    {2, 0, 0, true, 5000, 0},
    {0, 0, 0, true, 10000, 0},
};

constexpr ecl::OpSet kPoolEcl = [] {
    ecl::OpSet s = kCurseEcl;
    for (int i = 0x3E; i < 0x41; ++i) s.sizes[i] = ecl::kUnknown;
    s.sizes[0x34] = 1;   // ECL CLOCK (poolrad_facts.md 4.3)
    s.sizes[0x36] = 2;   // ADD NPC
    s.sizes[0x0C] = 3;   // SETUP MONSTER
    s.pool_map = true;
    s.redraw_call = 0x2C90;
    s.sound_call = 0xBA03;
    s.program_ends = false;
    s.program_kills = false;
    return s;
}();

// Secret's spell results (secret_facts_2.md 14): its spell routines put
// "is " (START image 0xF96D) before a word - mostly the effect's own name
// from its effects list (image 0x10047, 38 bytes a name), else one of its
// "Healed" / "Affected" / "Unaffected" / "Protected" (0xFA6D ... 0xFD6D)
// or a word in the routine's code (GAME.OVR). Index = kJ | i.
constexpr uint32_t kSxIs = kP | 0xF96D;
constexpr uint32_t sx_effect(int n) { return kP | (0x10047u + 38u * static_cast<uint32_t>(n)); }
// Secret's silent training (a new character trained to its experience):
// a magic-user's spells at levels 6-9 (GAME.OVR 0x22921; its level 6 list
// has Cure Light Wounds, as the original's)
constexpr uint8_t kSecretMuLevels[4][8] = {
    {0x0C, 0x0F, 0x1E, 0x03, 0x22, 0x2F, 0x33, 0x36},
    {0x0E, 0x10, 0x1D, 0x23, 0x2D, 0x30, 0x51, 0},
    {0x0A, 0x09, 0x20, 0x37, 0x55, 0, 0, 0},
    {0x32, 0x34, 0x52, 0x58, 0x5E, 0, 0, 0},
};

constexpr profile::JoinedWord kSecretJoined[] = {
    {kSxIs, sx_effect(1)},          //  0 is Blessed
    {kSxIs, sx_effect(2)},          //  1 is Cursed
    {kSxIs, kP | 0xFB6D},           //  2 is Affected
    {kSxIs, kP | 0xFD6D},           //  3 is Protected
    {kSxIs, sx_effect(10)},         //  4 is Resistant to Cold
    {kSxIs, sx_effect(11)},         //  5 is Charmed
    {kSxIs, kP | 0xFC6D},           //  6 is Unaffected
    {kSxIs, sx_effect(12)},         //  7 is Enlarged
    {kSxIs, sx_effect(14)},         //  8 is Friendly
    {kSxIs, sx_effect(17)},         //  9 is Shielded
    {0x2EB0C, sx_effect(53)},       // 10 falls Asleep
    {kSxIs, sx_effect(52)},         // 11 is Held
    {kSxIs, sx_effect(20)},         // 12 is Fire Resistant (spell)
    {kSxIs, sx_effect(21)},         // 13 is Silenced
    {kSxIs, 0x2EEE5},               // 14 is invisible
    {kSxIs, sx_effect(28)},         // 15 is Duplicated
    {kSxIs, sx_effect(29)},         // 16 is Weakened
    {kSxIs, sx_effect(33)},         // 17 is Blind
    {kSxIs, sx_effect(34)},         // 18 is Diseased
    {kSxIs, sx_effect(49)},         // 19 is Affected by Prayer
    {kSxIs, 0x2FD96},               // 20 is un-cursed
    {kSxIs, sx_effect(37)},         // 21 is Blinking
    {kSxIs, sx_effect(39)},         // 22 is Hastened
    {kSxIs, sx_effect(42)},         // 23 is Slowed
    {kSxIs, 0x30752},               // 24 is paralyzed
    {kSxIs, kP | 0xFA6D},           // 25 is Healed
    {kSxIs, 0x30CAD},               // 26 is raised
    {kSxIs, 0x30D67},               // 27 is slain
    {kSxIs, sx_effect(71)},         // 28 is Magically Highlighted
    {kSxIs, sx_effect(25)},         // 29 is Invisible (spell)
    {kSxIs, sx_effect(35)},         // 30 is Confused
    {kSxIs, 0x311F4},               // 31 is protected (Fire Shield)
    {kSxIs, 0x313C0},               // 32 is clumsy
    {kSxIs, sx_effect(27)},         // 33 is Befuddled
    {kP | 0xFE6D, sx_effect(113)},  // 34 gains Giant Strength
    {kSxIs, 0x2F671},               // 35 is cured
    {kSxIs, 0x2F677},               // 36 is no longer blind
    {kSxIs, 0x30914},               // 37 is unpoisoned
    {kSxIs, 0x3091F},               // 38 is unaffected
    {kSxIs, kJ | 40},               // 39 is not Charmed
    {0x31D45, sx_effect(11)},       // 40 (not Charmed)
    {0x327EF, 0x327E5},             // 41 breathes cold (the dragons' breath, GAME.OVR 0x327F9)
    {0x327EF, 0x327EA},             // 42 breathes Fire
    {kSxIs, 0x32100},               // 43 is no longer a statue
    {kSxIs, 0x32313},               // 44 is turned to stone
    {kSxIs, sx_effect(106)},        // 45 is STUNNED
    {kSxIs, 0x32113},               // 46 is not stoned.
};

// Secret of the Silver Blades, GOG release (secret_facts.md; facts only):
// START.EXE 72,097 bytes, EXEPACK, unpacked 79,824; its data segment at
// image 0xDE20 (DS offsets below are from there).
// - frame: bars drawn by code (no tables) with block 202's tiles 52-59:
//   55 every horizontal bar, 52 / 53 / 54 the verticals' top cap, side and
//   bottom cap, 59 / 56 / 57 / 58 the 3D view frame's edge, top cap, side,
//   bottom cap - so 202's tiles 40-59 are kept (tile_window 40);
// - the font: 8X8D5.DAX block 201 (8X8D1 has no block 201);
// - title: TITLE.DAX picture 1 for 5 s, picture 2 for 10 s, picture 3 at
//   row 5, column 2 on it for 10 s, picture 2 again with 4 at row 5,
//   column 5 for 5 s, then the credits for 10 s (32-byte print calls in
//   GAME.OVR from 0x379, the code segment at 0xD2; a bar at row 1, the
//   title on row 0); the version line's words at image 0x2D / 0x64 (times
//   measured in DOSBox, roughly);
// - areas: ECL / GEO / WALLDEF / 8X8D 1-3, SKY.DAX as Curse's, its 16 sky
//   colours at DS 0x0AA8 (the same bytes as Curse's);
// - scripts: Curse's opcodes and operand counts (the interpreter's handlers
//   read the same ones) but 0x3C reads none and 0x41 (new) reads 2; its
//   areas 1-6 (0x7F12, as Curse's) use the numbered files two by two
//   ((area + 1) / 2) and block numbers are unique across a family's files
//   (area 2's NEW ECL 33 is ECL1's) - ecl::OpSet::area_by_block;
// - a saved game's Begin with no script running starts ECL1 block 16 (the
//   party in Priam's house at 3,3 facing south, as the original).
constexpr TitleStep kSecretTitle[] = {
    {1, 0, 0, true, 5000, 0},
    {2, 0, 0, true, 10000, 0},
    {3, 5, 2, false, 10000, 0},
    {2, 0, 0, true, 0, 0},
    {4, 5, 5, false, 5000, 0},
    {0, 0, 0, true, 10000, 0},
};

constexpr ecl::OpSet kSecretEcl = [] {
    ecl::OpSet s = kCurseEcl;
    s.sizes[0x3C] = 0;
    s.sizes[0x41] = 2;
    s.area_by_block = true;
    s.secret_fields = true;
    s.vault_word = 0x4CFC;          // (secret_facts_2.md 24)
    s.xp_award = true;              // (op 0x41: GAME.OVR 0x43FF)
    // CALL 2DCB follows the scripts' moves of the party (SAVE x, y, facing
    // to 0xC04B-D, then the call), as Curse's CALL 2E10: the 3D view again
    s.redraw_call = 0x2DCB;
    return s;
}();

// Pools of Darkness (darkness_facts.md 5; its interpreter, GAME.OVR overlay
// at 0x246): Curse's operand counts but 0x0C 4 (SETUP MONSTER's sides),
// 0x1D none (PRESS KEY), 0x1F 1 (JOURNAL n), 0x21 2 (LOAD FILES: map,
// look), 0x22 none (YES NO), 0x23 none (END - it ends the stream), 0x27
// 4 then as many as the 4th says (TREASURE), 0x29 none, 0x2C 1 (SOUND),
// 0x31 2 then as many as the 2nd (WHAT MENU), 0x37 2 (FIND NPC - no LOAD
// PIECES), 0x41 2 (EXPERIENCE): every one of its 56 blocks decodes
constexpr ecl::OpSet kDarknessEcl = [] {
    ecl::OpSet s = kCurseEcl;
    s.sizes[0x0C] = 4;
    s.sizes[0x1D] = 0;
    s.sizes[0x1F] = 1;
    s.sizes[0x21] = 2;
    s.sizes[0x22] = 0;
    s.sizes[0x23] = 0;
    s.sizes[0x27] = ecl::kCount4;
    s.sizes[0x29] = 0;
    s.sizes[0x2C] = 1;
    s.sizes[0x31] = K2;
    s.sizes[0x37] = 2;
    s.sizes[0x41] = 2;
    s.load_pieces = 0xFF;           // (none: 0x37 is FIND NPC)
    s.end_op = 0x23;
    s.files_look = true;
    s.byte_map = true;
    s.vault_word = 0x31;            // (COMBAT with no monsters: game byte 0x31 the vault)
    return s;
}();

// Darkness's title: TITLE.DAX's three VGA pictures - the Forgotten Realms
// one, the game's, the credits (a picture of its own: no print calls)
constexpr TitleStep kDarknessTitle[] = {
    {1, 0, 0, true, 5000, 0},
    {2, 0, 0, true, 5000, 0},
    {3, 0, 0, true, 10000, 0},
};

const Profile kProfiles[] = {
    {games::Game::CurseOfTheAzureBonds, "GOG", "START.EXE", 57789, 62432, 0x48D0,
     {0x6E60, 0x6E88, 0x6EB0, 0x6EF2, 0x6F1B, 0x6F0A, 0x6ED6, 0x6EE3, 0x6F31, 0x6F3E, 0x6F4D, 0x6F64, 0x6F7B},
     "8X8D1.DAX", 202,
     20830,
     "TITLE.DAX", kCurseTitle, sizeof kCurseTitle / sizeof kCurseTitle[0], 0x06, 0x25,
     "GAME.OVR", 272137, 706, 218, {3, 8},
     2, 6, 203, "SKY.DAX", 252, &kCurseEcl,
     0x6D9A, 2, 1,
     {0x79, 0x4CA1, 0x6D5A, 0x6D7A, 32, 0x50},
     {0xB133, 12, 42, 0x20111, 0x1EE80, 0x37E31, 0x37E36, "CURSE.CFG", 0x1F5AB, 0x1F5BD, 0x1F673, 0xB00A,
      0x1B382, 0x1B34E, 0x23595, 0x235A6, 0x235BF, 0x235D1, 0x235D6, 0x235D9, 0x235FD, 0x23617,
      0x1C506, 0x1C511, 0x2255C, 0x22562, 0x2256D, 0x2257C, 0x22586, 0x22591, 0x225A5, 0x32BF0,
      0x227F1, 0x22804, 0x2280D, 0x1B7AB, 0x1C514},
     {{0xB898, 27, 18}, {0xBA7E, 10, 8}, {0xBACE, 17, 9}, {0xBB67, 7, 2}, {0xBB75, 11, 7}, {0xBBC2, 13, 9},
      0x27094, 0x2709A, 0x2709F, 0x270BD, 0x270C5, 0x270D7, 0x276AA, 0x276B1, 0x276B8, 0x276C7,
      0x276D6, 0x276E8, 0x27BA8, 5},
     {{0xBC37, 21, 255}, "ITEMS", 73, 28, 9, 86, 0x87, 0xB1, {41, 42, 43, 44, 37, 36},
      0x773D, 0x7745, 0x3CF1A, 0x3CF20, 0x3CF26, 0x7D3F, 0x7D18, 0x7B5D, 0x2916E,
      0x2859A, 0x285A0, 0x2856B, 0x37AF6, 0x37AFD, 0x28EF7, 0x28F03, 0x28F0F, 0x28F1E, 0x38F44,
      0x270CA, 0x270D1, {0x26, 0x92, 0x0C, 0x0E, 0x44, 0x3E},
      {0x17, 20, 20, 243}, {0x05, 0x05, 0x0B, 0x4D},   // Spiritual Hammer: its effect, the Hammer's type, words "Hammer", "Spiritual"
      0x4D},                                            // berserk (the listing's spl_berzerk)
     {0xABE0,
      {0x37DC, 0x47B0, 0x37DC, 0x65, 0x3E3A, 0x3EA2, 0x3EAA, 0x3EBB, 0x3EC0, 0x3F20, 0x3F33, 0x3F88, 0x3FFA,
       0x404E, 0x4124, 0x4174, 0x41DA, 0x429B, 0x45BE},
      0x081A, 0x0822, 0x3EC3,
      0x61, 0x1A, 0x2F, 0x12, 0x30, 0x6B, 0x7C, 0x08, 0x86,
      // (silent training's level 3: Stinking Cloud and Charm Person - the program's
      // own, as the GOG sample party's books show; Human Change: Enlarge too)
      {0x0B, 0x12, 0x0C, 0x15}, 0x0F, {0x22, 0x0A}, 0x1F, 0x2F,
      0x206B3, 0x206C8, 0x206D4, 0x206DF, 0x206C1, 0x20710, 0x2071F, 0x20730, 0x20736,
      {0x0B, 0x12, 0x0C, 0x15}, 0x3BB0D, 0x3BB1C, 0x3BB2F, 0x3BB36, 0x3BB4B},
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
      0x21, {0x1F, 0x22, 0x2B, 0x2C, 0x32, 0x39}, 0x44, 0x37, 0x16, 0x0F, 0x20, 0x24, 0x22, 0x2B, 0x2C, 0x1F},
     70, 0x65, 0xD6,
     {0xB033, {0xB480, 41, 5}, {0xD39F, 41, 101},
      {0x27B87, 0x2A486, 0x2A490, 0x2A4BB, 0x2E177, 0x2E141, 0x19878, 0x198AB, 0x194FB, 0x19509, 0x1951C, 0x1952F,
       0x19890, 0x192C4, 0x192EB, 0x2B35E, 0x2B460, 0x2BA11, 0x2B5DD, 0x2B6A8, 0x2BA20},
      0x7ED2, 0x7ED3,
      {0x192DB, 0x1942B, 0x2E13C, 0x2F274, 0x2F21F, 0x2F235, 0x2F0EF, 0x39B78, 0x39B88, 0x36666, 0x2FECA, 0x30F71,
       0x32173, 0x324C5, 0x31597, 0x315A4, 0x19F93, 0x2E14A, 0x2A4A6, 0x2A4C7, 0x19A8D, 0x19AA5, 0x19AC1, 0x19AE4,
       0x19A77, 0x19B03, 0x192FB, 0x2B76C, 0x2A4B1, 0x2E151,
       0x2F23F, 0x2F249, 0x2F262, 0x294BC, 0x294C9, 0x294CF, 0x2A49C,
       0x2F26B},
      kCurseCamp, sizeof kCurseCamp / sizeof kCurseCamp[0],
      {2, 0x2A, {0x22, 0x2B, 0x32}, {{0, 0}, {0x2C, 0x1F}, {0x39, 0}}, 0x0C, 0x17, 0x32, 0x36, 0x8F, 0x92,
       // rolled times (curse_finish_facts.md 10): cause disease d6 x 10, speed / paralysis 5d4,
       // giant strength (d4 + 4) x 10, the party invisible (d10 + 10) x 10, Neutralize Poison 1440
       {{0x28, 1, 6, 0, 10}, {0x39, 5, 4, 0, 1}, {0x3D, 5, 4, 0, 1}, {0x3B, 1, 4, 4, 10}, {0x3F, 1, 10, 10, 10},
        {0x43, 0, 0, 1440, 1}}},
      kCurseSpellNamed, sizeof kCurseSpellNamed, kCurseNamed, sizeof kCurseNamed / sizeof kCurseNamed[0], 0xD2, 0x10},
     {0xB05C, 0xB086, 0xB0AF,
      {0x1AC65, 0x1A792, 0x1A7C0, 0x1AA82, 0x1AA90, 0x1AAA7, 0x1AAAF, 0x1AAB7, 0x1AABD, 0x1A8D7, 0x1A8E4, 0x1A8C9,
       0x1A8F6, 0x1A908, 0x1A91D, 0x1AC6D, 0x1AC77, 0x1AC86, 0x1AC96, 0x1ACA1},
      {0xB32C, 0xB355, 0xB37E, 0xB3A7, 0xB3D0},
      {0x23E3B, 0x23E3F, 0x23E4E, 0x23E52, 0x23E58, 0x23E71, 0x23E76, 0x23E9B}},
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
       0x1457D, 0x15583, 0x2F27A, 0x2F288, 0x30788, 0x10AE9, 0x32F30, 0x35E53, 0x106C3, 0x1038C, 0xFFAB,
       0x17391, 0x17B6E, 0x10DE3, 0x11388, 0x123FD, 0x10255,
       0x175E3, 0x175FC, 0x1760E, 0x17629, 0x17633, 0x1764F,
       0x337F3, 0x337FC, 0x33809, 0x33B82, 0x33B8D, 0x33CCB, 0x33EAB, 0x34038,
       0x34155, 0x374C8, 0x374DC,
       0x1089C, 0x10892, 0x108A8, 0x108B5, 0x128DF, 0x128EC, 0x12850, 0x32BBE, 0x32BCB, 0x32BE2, 0x32BF0,
       0x1045C, 0x1077B, 0x103ED, 0x2FDE3, 0x10618,
       0x10F13, 0x10A87, 0x1168D},
      // (Curse's own list at its DS 0x02AA - poolrad_combat_facts.md 8: 0x89 / 0x8B, not 0x4D)
      {0x03, 0x0B, 0x0D, 0x15, 0x17, 0x1B, 0x23, 0x28, 0x33, 0x34, 0x35, 0x3A, 0x5B, 0x88, 0x89, 0x8B, 0x8E,
       0x90, 0x1F},
      {{0x33, 0x34, 0x35, 0x1F}, 0x01, 0x02, 0x31, 0x27, 0x2A, 0x19, 0x08, 0x09, 0x1C,
       {0x07, 0x08, 0x23, 0x24, 0x25, 0x61}, 0x0B, 0x8E, 0x1E,
       {9, 21, 100, 28, 31, 73}, {2, 7, 20}, {85, 86}, {47, 98, 101},     // (spinning: 20 the hammer - Curse's code tests 0x14)
       0x24, 0x25, 0x1B, 0x15, 0x88, 0x03, 0x07, 0x21, 0x45, 0x44,
       // The monsters' special abilities (monster_fx_facts.md)
       {0x40, 0x46, 0x43, 0x7A, 0x7B, 0x4F, 0x50, 0x39, 0x60, 0x67, {0x14, 0x36, 0x3D},
        0x3C, 0x77, 0x51, 0x55, 0x82, 0x29, 0x68,
        0x70, 0x6E, 0x87, 0x54, 0x52, 0x71, 0x0A, 0x14, 0x85,
        0x69, 0x6A, 0x81, 0x3F, 0x6B, 0x6C, 0x6D, 0x7D,
        0x65, 0x3B, 0x62, 0x64, 0x66,
        0x8A, 0x18, 0x2D,
        0x5A, 0x80, 0x83, 0x84, 0x56, 0x79, 0x53, 0x7F, 0x57,
        0x0D, 0x3A, 0x8B, 0x90, 0x34, 0x37, 0x35,
        0x76, 0x87, {0x54, 0x37, 0x15}},
       // More spells' effects (spell_facts.md 2): enlarge, confuse, berserk, dispel evil (0x04, 0x91),
       // the fire shields (hot 0x32, cold 0x36, the zap 0x8F); Ray of Enfeeblement's 0x1D
       {0x0C, 0x23, 0x89, 0x04, 0x91, 0x32, 0x36, 0x8F, 0x16, 0x0F, 0x17, 0x20, 0x3E, 0x92, 0x11, 0x1D, 0x22, 0x2B, 0x2C},
       // The races' and rangers' effects (as created: Constitution saves, half-elf
       // resistance, dwarf vs orcs, gnome's foes, vs giants, gnome's other, ranger vs giants)
       {0x61, 0x7C, 0x1A, 0x12, 0x2F, 0x30, 0x86},
       // The magic items' own effects: Flame Tongue, Dragon Slayer, Frost Brand, displacement,
       // the ring's invisibility, the Robe of Vermin (behaviour_facts.md items)
       {0x06, 0x4B, 0x4C, 0x59, 0x38, 0x4A, 0, 0x48},
       0x4D},                                           // berserk (the Berserker sword's)
      kCurseFight, sizeof kCurseFight / sizeof kCurseFight[0]},
     {0x29A2, 0x29B3, 0xA28D, 0xA295, 0xA2A2, 0x3222},
     &kCurseRandom, 0x0830,
     {0x6990, 0x04A2, 0x12BB, 0x04A2, 0x054A, 21, 0x2F, 0x33},
     {0x18B8B, 0x18B90, 0x18B96, 0x18B9D, 0x18BA3, 0x1F},
     {{0x265C6, 0x265F5, 0x2662A, 0x26660, 0x266B5, 0x266E6, 0x2671F, 0x26754, 0x2678E, 0x267C0, 0x267FA, 0x26831,
       0x2683E, 0x2686C, 0x268A1, 0x268D4, 0x268DA, 0x26901, 0x26936, 0x26969, 0x2698D, 0x269C2, 0x269FD, 0x26A33},
      {4, 4, 4, 4, 4, 4}, {0x4A, 0x4B}, 0x4D, 0x41, 0x41, 0x7A, 0x0AAA, 0x4CFD, 0x7EA8}},
    // Pool of Radiance (being built: what's named here; the rest follows
    // as its screens are done - zero = not known yet)
    {.game = games::Game::PoolOfRadiance, .release = "GOG", .program = "START.EXE",
     .program_size = 65231, .image_size = 68592, .data_base = 0xC7C0,
     .frame = {},
     .tiles_file = "8X8D1.DAX", .tiles_block = 202,
     .press_any_key = 0x77DE,
     .title_file = "TITLE.DAX", .title = kPoolTitle, .title_steps = sizeof kPoolTitle / sizeof kPoolTitle[0],
     .title_version = 0, .title_menu = 0,
     .overlay = "GAME.OVR", .overlay_size = 206105, .credits_at = 0x32C, .credits_base = 0xD4, .credits_bars = {4, 0},
     .first_area = 1, .last_area = 8, .common_tiles_block = 203, .sky_file = nullptr, .horizon_block = 0,
     .ecl_ops = &kPoolEcl,
     .sky_colours = 0x0C26, .start_area = 3, .start_script = 0,
     .wild = {},
     // The party menu: 11 entries (no Human Change), its words; "Name" and
     // "AC  HP" in the program; POOL.CFG's last folder line names the saves;
     // no "Add from where?" (its own characters only), no Overwrite question
     .party = {0xCDD9, 11, 42, 0x19A62, 0x1F573, kP | 0x12FB, kP | 0x1300, "POOL.CFG", 0x1FCAB, 0x1FCBD, 0x1FD11,
               0xCCC8, 0x1968C, 0x19654,
               0, 0, 0x1C9CE, 0, 0x1C9E9, 0, 0, 0, 0, 0, 0x1B851, 0x1B857, 0x1B862, 0x1B871, 0x1B87B, 0x1B886,
               0x1B89A, 0x2D979, 0x1B9ED, 0x1BA01, 0x1BA14, 0, 0},
     .view = {{0xD4F8, 27, 18}, {0xD6C3, 10, 8}, {0xD713, 17, 9}, {0xD7AC, 7, 2}, {0xD7BA, 11, 7}, {0xD807, 13, 9},
              0x2103A, 0x21040, 0x21045, 0x21063, 0x2106B, 0x2107D, 0x215F8, 0x215FF, 0x21606, 0x21615,
              0x21624, 0x21636, 0x21AC2, 5},
     // Items: the same item types as Curse's for arrows, quarrels, darts,
     // the flask of oil and the elves' weapons (GBC's item lists)
     .items = {{0xD87C, 21, 255}, "ITEMS", 73, 28, 9, 86, 0, 0xB1, {41, 42, 43, 44, 37, 36},
               0x6CF8, 0x6D00, 0x2D657, 0x2D65D, 0x2D663, 0x716F, 0x7148, 0x7023, 0x22763,
               0x21EEC, 0x21EF2, 0x21EBD, kP | 0x0FCD, kP | 0x0FD4, 0x224CE, 0x224DA, 0x224E6, 0x224F5, kP | 0x23B3,
               0x21070, 0x21077,
               // (poolrad_combat_facts.md 2.2 / 3.2: Strength in the record with
               // 0x26 Ogre Power / Strength / giant strength and 0x0C Enlarge, both
               // giving the most of them; Friends 0x0E; Constitution 20's 0x3E. The
               // Spiritual Hammer: effect 0x17, type 20, words "Hammer" / "Spiritual")
               {0, 0x26, 0x0C, 0x0E, 0, 0x3E}, {0x17, 20, 20, 243}, {}, 0,
               items::kPoolPassive, 12, 0x89, 0x11BA6, 0x11D77, {0x04, 0x07}, {0xEDCA, 0xEE83},
               // The Flame Tongue (type 36, word 0xF1): +2 (name word 0xA3) with 0x3D
               // 0x16 in Pool's files; fixed as Curse's - +1 (word 0xA2), the effect
               // 0x06, code 0x80
               {0x24, 0xF1, 2, 0x16, 0x06, 0x80, 0xA3, 0xA2}},
     // Character rules (create_facts.md): the tables' shapes are Pool's
     // (classes::Layout pool); flags / masks at DS 0x5D6 / 0x5DE
     .create = {0xC7C0,
                {0x31FA, 0x43DB, 0x31FA, 68, 0x3C7C, 0x5D6, 0x5DE, 0, 0x3CF3, 0x3D3B, 0x3D4E, 0x3DA3, 0x3E17,
                 0x3E5B, 0x3F31, 0x3F81, 0x3FE7, 0x40A3, 0x426E, true, 0x4263, 0x5D6, 0x5E6},
                0x08B6, 0x08BE, 0x3CF6,
                0x61, 0x1A, 0x2F, 0x12, 0x30, 0x6B, 0x7C, 0, 0,
                {0x0B, 0x12, 0x13, 0x15}, 0, {0, 0}, 0, 0,
                0x19E70, 0x19E7E, 0x19E8A, 0x19E95, kP | 0x37E1, 0x19EC4, 0x19EDA, 0, 0,
                {0, 0, 0, 0}, 0, 0, 0, 0, 0,
                0x5A, true, 0x1CD57, 0x286F, 0x287D, "CHARLIST.TXT"},
     .item_words = {0x21EC3, 0x21EC8, 0x21ECF, 0x21ED5, 0x21EDC, 0x21EE2, 0x21EE8, 0x21D5A, 0x21EFD, 0x22816, 0x21F0D,
                    0x21F13, 0x21F28, 0x22752, 0x22CC5, 0x22CD4, 0x22CEB, 0x22CFA, 0x22D00, 0x22F02, 0x22F4A, 0x22F70,
                    0x22F39, kP | 0x37E1},
     .shop_words = {0x51B6, 0x51DE, 0x4F2D, 0x4F44,
                    0x47B1, 0x4857, 0x493D, 0x494F, 0x4963, 0, 0x4CB6, 0x4A7C, 0x4DA4, 0x4E90,
                    0x4577, 0x45ED, 0x45FE, 0x460C, 0x461A, 0x462C,
                    0x47A3, 0x4846, 0x4A6F, 0x4CA5, 0x4D95, 0x4E81,
                    0x5205, 0x5250, 0x7194, 0x71E7,
                    0x265AC, 0x265CB, 0x265D2, 0x265DC, 0x265E2, 0x265EE, 0x26604, 0x26609, 0x2660E, 0x26618,
                    0x26623, 0x26569, 0x2657C, 0x26581, 0x26587, 0x26599,
                    0x23160, kP | 0x37E1, 0x2595A, 0x25964, 0x24E64,
                    0x1C26D, 0x1C28C, 0x1C2A4, 0x1C2C3, 0x1C2D9, 0x1C2E7, 0x1C2F6, 0x1C303, 0x1C31A},
     // The temple (poolrad_combat_facts.md 7): Curse's prices, no Heal
     .cures = {{1000, 1000, 100, 350, 600, 0, 1000, 5500, 3500, 2000},
               0x21, {0x1F, 0x22, 0x2B, 0x2C, 0x32, 0x39}, 0, 0x37, 0x16, 0x0F, 0x20, 0x24, 0x22, 0x2B, 0x2C, 0x1F},
     .gem_type = 70, .gem_word = 0x65, .jewel_word = 0xD6,
     // Spells: 57 names (spell 0 isn't one); the rest words "Rest daYs Hours
     // Mins Inc Dec Exit" differ from Curse's (not used yet)
     .magic = {0xCCF1, {0xD262, 41, 5}, {0xF0A9, 41, 57},
               {0x21AAD, 0x23973, kP | 0xE3CE, 0, 0x27053, 0x2701D, 0x180F0, 0x18123, 0x17DA1, 0x17DAF, 0, 0x17DC2,
                0x18108, 0x17B53, 0x17B7A, 0x243AF, 0x244A5, 0x24A36, 0x24623, 0x246E3, 0x24A45},
               0x7ED2, 0x7ED3,
               {0x17B6A, 0x17CBA, 0x27018, 0x27C16, 0, 0x27BE0, 0x27A96, kP | 0x2EC6, kP | 0x2ED6, 0x2BF62, 0x281DC,
                0x29227, 0, 0, 0x29536, 0, 0x187D0, 0x27026, 0x23995, 0, 0x18300, 0x18318, 0x18334, 0x18357, 0x182EA,
                0x18376, 0x17B8A, 0x247A7, 0x239A0, 0x2702D, 0x27BEA, 0x27BF4, 0x27C0D, 0x22AB1, 0x22ABE, 0, 0x2398B, 0},
               kPoolCamp, sizeof kPoolCamp / sizeof kPoolCamp[0],
               {2, 0x2A, {0x22, 0x2B, 0x32}, {{0, 0}, {0x2C, 0x1F}, {0x39, 0}}, 0x0C, 0x17, 0, 0, 0, 0x26,
                {{0x28, 1, 6, 0, 10}, {0x39, 5, 4, 0, 1}, {0x3D, 5, 4, 0, 1}, {0x3F, 1, 10, 10, 10},
                 {0x3B, 1, 4, 4, 10}, {0x43, 0, 0, 1440, 1}}},
               nullptr, 0, nullptr, 0, 0xD2, 0x10},
     .alter = {0xCD1A, 0xCD44, 0xCD6D,
               {0x19441, 0x18F71, 0x18F9F, 0x19261, 0x1926F, 0x19286, 0x1928E, 0x19296, 0x1929C, 0x190B6, 0x190C3,
                0x190A8, 0x190D5, 0x190E7, 0x190FC, 0x19449, 0x19466, 0x19475, 0x19457, 0x19485},
               // (Pics: "Monsters on  ", "Portraits on  ", "Portraits off  ", "Monsters off  ", "Exit")
               {0xCFA8, 0xCFD1, 0xCFFA, 0xD023, 0xD04C},
               {0x1D148, 0x1D14C, 0x1D15B, 0x1D15F, 0x1D165, 0x1D16B, 0x1D170, 0x1D19A}},
     // Fights (poolrad_combat_facts.md 1, 2): Curse's tables but the ground
     // table's place, no terrain table (Pool's outdoor fields are made by
     // code), turn undead at DS 0x0447; the effects Pool has (the same
     // number often means something else in Pool: those are 0 here)
     .fight = {{0x2744, 0x02D2, 0x02E2, 0x02F2, 0x02F6, 0x02FE, 0x0306, 0, 0x0447, 0x0342},
               {0xE3EF, 0x9E00, 0x9E06, 0x9E10, 0x9E15, 0x9E1B, 0x9E21, 0xA1BB, 0xA1D8, 0, 0xA537, 0x15264, 0x15270,
                0x15278, 0x1528A, 0x15296, 0xA605, 0xA60C, 0xA618, 0xA621, kP | 0x321D, 0xA8D6, 0xA8E2, 0xA8E8, 0xA8F0,
                0xA8F8, 0x12A2D, 0x12A39, 0x12A48, 0x12A50, 0x12A5F, 0x12A73, 0x12A80, 0x12A88, 0x12A91, 0x12A9B,
                0x12AA6, 0x12AB3, 0x12ABD, 0x12AEA, 0xA05F, 0xA076, 0x1344A, 0x13453, 0x15185, 0xABC0, 0xBC7A,
                0xBC8C, 0xA7FA, 0x13676, kP | 0x156B, kP | 0x1575, kP | 0x1578, 0x5DFD, 0, 0x5DEA, 0x5E11, 0x5E45,
                0x5E5E, 0x5E71, 0x6A55, 0x6A8B, 0x629E, 0x62A5, 0x63AC, 0x63BB, 0x63C1, 0x63CE, 0x63E3, 0x63FB,
                0x641B, 0x6085, 0x608D, 0x2C171, 0x2C178, 0x2C18B, 0x2C1A4, 0x2C1AE, 0x2C1B8, 0x2C1C9, 0x2C1D3,
                0x2C1EB, 0x2C532, 0x2A31D, 0x2A32B, 0x14C00, 0x14BF0, 0x1392B, 0x1393B, 0x13945, 0x13952, 0x148A8,
                0x27C1C, 0x27C2A, 0x28B11, 0xF903, 0, 0x1018A, 0xF6B4, 0xF35E, 0, 0, 0, 0xFBFD, 0x10239, 0x29FA4,
                0, 0, 0, 0, 0x108FE, 0, 0, 0x108E8, 0x108F1, 0x108FE, 0x1186C, 0, 0, 0, 0, 0, 0x2C732, 0x2C746,
                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x2D979, 0xF42D, 0xF76C, 0xF3BF, 0x280F5, 0xF60D, 0xFD2D, 0xF8A1, 0,
                0x10D4A, 0x10D42, 0x10D3A, 0x10D56, 0x1103B, 0x103B9, 0x10583, 0x161C4, 0x10ABE, 0x10AC7,
                0x2A0E7, 0x11500, 0x106DA, 0x106F1, 0xFFDD, 0xFFE5, 0xFEC3, 0xFECB},
               {},                                       // (no list of effects a fight's end takes away)
               {{0x33, 0x34, 0x35, 0x1F}, 0x01, 0x02, 0x31, 0x27, 0x2A, 0x19, 0x08, 0x09, 0x1C,
                {0x07, 0x08, 0x23, 0x24, 0x25, 0}, 0x0B, 0, 0x1E,
                {9, 21, 28, 31, 73, 0}, {2, 7, 20}, {85, 86}, {0, 0, 0},
                0x24, 0x25, 0, 0x15, 0, 0, 0, 0x21, 0, 0,
                {0x40, 0x46, 0x43, 0, 0, 0x4F, 0x50, 0, 0, 0, {0, 0, 0},
                 0, 0x77, 0, 0, 0, 0x29, 0x68,
                 0x70, 0x6E, 0x5B, 0, 0, 0x71, 0x0A, 0x14, 0,
                 0x69, 0x6A, 0, 0, 0x6B, 0x6C, 0x6D, 0x7D,
                 0x65, 0x3B, 0x62, 0x64, 0x66,
                 0, 0x18, 0x2D,
                 0, 0, 0, 0, 0, 0x79, 0x53, 0x7F, 0,
                 0, 0x3A, 0, 0, 0x34, 0x37, 0x35,
                 0x76, 0, {0, 0, 0},
                 // Pool's own (poolrad_combat_facts.md 4): drains, the mummy's disease, paralysis,
                 // the stirge, the mantis, the tiger; silver, oil; the vampire's gaze, the
                 // dragon's breath, the vampire's escape; the fear auras; its magic resistance
                 0x55, 0x56, 0x57, 0x32, 0x39, 0x44, 0x45,
                 0x4C, 0x4B, 0x4D, 0x4A, 0x49,
                 0x60, 0x7B, 0xB1, 0x7A, 0x56,
                 0x54, 0x58, 0x67,
                 0x51, 0x52,
                 1, 0x41, 0x42},
                {0x0C, 0, 0, 0, 0, 0, 0, 0, 0x16, 0x0F, 0x17, 0x20, 0x3E, 0, 0x11, 0x1D, 0x22, 0x2B, 0x2C},
                {0x61, 0x7C, 0x1A, 0x12, 0x2F, 0x30, 0},
                {0x06, 0, 0, 0x59, 0x38, 0, 0x03, 0x48},
                0, true, 0x32, true},
               kPoolFight, sizeof kPoolFight / sizeof kPoolFight[0]},
     .script_words = {0x2C1A, 0x2C2B, 0x9554, 0x955C, 0x9569, 0x347C},
     .random_items = &kPoolRandom, .random_rows = 0x0972,
     // The sound driver: Curse's, 0xBD further on in its segment (poolrad_facts.md 1.5)
     .sound = {0x8AE0, 0x055F, 0x1378, 0x055F, 0x0607, 21, 0x2F, 0x33},
     .door = {0x17417, 0x1741C, 0x17422, 0x17429, 0x1742F, 0x1F},
     // The ending's words (GAME.OVR, its ovr19): 3, 6 and 4 lines
     .won = {{0x207B1, 0x207D5, 0x207FA, 0x20818, 0x2083C, 0x20861, 0x20888, 0x208A8, 0x208CD, 0x208D6, 0x208FD,
              0x20925, 0x20938},
             {3, 6, 4}},
     .variant = {{20, 21, 22}, 29, true, 15, 7, 6, 1, false, true, true, true, 0x3648, 25, true, true},
     .overland = &kPoolOverland},
    // Secret of the Silver Blades (being built: the screens and the maps so
    // far; zero = not known yet)
    {.game = games::Game::SecretOfTheSilverBlades, .release = "GOG", .program = "START.EXE",
     .program_size = 72097, .image_size = 79824, .data_base = 0xDE20,
     .frame = {},
     .tiles_file = "8X8D1.DAX", .tiles_block = 202,
     .press_any_key = 0x589F,
     .title_file = "TITLE.DAX", .title = kSecretTitle, .title_steps = sizeof kSecretTitle / sizeof kSecretTitle[0],
     .title_version = 0x2D, .title_menu = 0x64,
     .overlay = "GAME.OVR", .overlay_size = 278528, .credits_at = 0x379, .credits_base = 0xD2, .credits_bars = {1, 0},
     .first_area = 1, .last_area = 3, .common_tiles_block = 203, .sky_file = "SKY.DAX", .horizon_block = 252,
     .ecl_ops = &kSecretEcl,
     .sky_colours = 0x0AA8, .start_area = 1, .start_script = 17,
     .wild = {},
     // The party menu: 13 entries (Initialize Mouse/Joystick the 12th),
     // its words (the same strings as Curse's where noted in
     // secret_facts.md 4); BLADES.CFG names the save folder
     .party = {0xE4E0, 13, 42, 0x1D6B3, 0x2609A, 0x38909, 0x3890E, "BLADES.CFG", 0x26791, 0x267A3, 0x26859,
               0xE1F7, 0, 0x1D0FB,
               // ("Add from where? ", "Secret Curse Exit" ...)
               0x209E0, 0x209F1, 0x20A03, 0x20A15, 0x20A1A, 0x20A1D, 0x20A41, 0x20A5B, 0x24665, 0x24670, 0x1F8F2, 0x1F8F8,
               0x1F903, 0x1F912, 0x1F91C, 0x1F927, 0x1F93B, 0x3120D,
               0x1FB9B, 0x1FBCF, 0x1FBD8, 0x238F2, 0x24673},   // (Modify: "Add Subtract Keep Exit")
     // View Character's name tables (races in Secret's own order: the
     // engine reorders them); its words where they are Curse's
     .view = {{0xE9AB, 27, 18}, {0xEB91, 9, 8}, {0xEBD9, 17, 9}, {0xEC72, 7, 2}, {0xEC80, 11, 7}, {0xECCD, 13, 9},
              // (its own words: "Status:", " years" after the age, "Level",
              // "Experience:", "Armor Class", "Hit Points ", the View menu's "Exit")
              0, 0x27169, 0x27184, 0x27172, 0x27178, 0x27161, 0x278B8, 0x278AC, 0x278D0, 0x278E8, 0x278C4,
              0x278D9, 0x27E4B, 5},
     // Items (secret_facts.md 11): 124 name words of 21 bytes (its names
     // are the records' own - items::Names::stored_names), the ITEMS type
     // file; arrows 30, quarrels 12, darts 5; the elves' bows and swords
     // 22-25, 18, 19 (its types are its words' numbers); the menu word
     .items = {.words = {0xED42, 21, 124}, .types_file = "ITEMS", .arrow = 30, .quarrel = 12, .dart = 5,
               // (types 0x16-0x1A, 0x13, 0x12: GAME.OVR 0x382C6)
               .elf_bonus = {22, 23, 24, 25, 26, 18, 19},
               // (its GAME.OVR words where Curse's are; no "Weapon" / "Armor")
               .buy_items = 0x8532, .buy = 0x853A, .list_next = 0x3E5AF, .list_prev = 0x3E5B5,
               .list_exit = 0x3E5BB, .shop_menu = 0x8C6C, .shop_menu_money = 0x8C45, .no_money = 0x8962,
               .overloaded = 0x28FAD, .title = 0x2846B, .heading = 0x28471, .ready = 0x28433, .yes = 0x38574,
               .no = 0x3857B, .cursed = 0x28D33, .wrong_class = 0x28D40, .already = 0x28D4C,
               .hands_full = 0x28D5B, .plural_s = 0x399AA,
               // (its effects in the stats: Strength 0x26, giant strength 0x71,
               // Enlarge 0x0C - as the fights' facts; the readied items' codes:
               // items::kSecretPassive; the Boots of Speed's 0x4A)
               .stat_fx = {0x26, 0x71, 0x0C, 0x0E, 0x44, 0x3E},
               .passive = items::kSecretPassive,
               .light_magic_armour = true, .speed_fx = 0x4A, .sell_secret = true},
     // Character rules (secret_facts.md 9): the tables in Secret's shapes
     // (classes::Layout secret: 7 classes, levels to 18, 119 spells, its
     // own race order), the rest of character making not known yet
     .create = {.ds_image = 0xDE20,
                .tables = {0x449C, 0x5810, 0x449C, 119, 0x4C0C, 0x4C92, 0x4C99, 0x4CA9, 0x4CAD, 0x4D3D, 0x4D48,
                           0x4D9D, 0x4DFF, 0x4E45, 0x4EFF, 0x4F45, 0x4FAB, 0x506C, 0x5592, false, 0, 0, 0, true},
                // the hit dice: level 1's count, the sides (7 classes; its
                // training's roll - the better of two - as Curse's)
                .hp_count = 0x9B0, .hp_dice = 0x9B7,
                .icon_colours = 0x4CB0,
                // the races' effects (GAME.OVR 0x1DF40 - secret_facts_2.md 21): halflings,
                // dwarves and gnomes 0x61, dwarves 0x1A 0x2F, gnomes 0x07 0x30 (not 0x2F), elves
                // 0x5F, half-elves 0x12; a paladin's Protection from Evil, a ranger's giant-class
                // bonus (also its Add's Curse characters, GAME.OVR 0x2580D - 20)
                .con_save = 0x61, .dwarf_orc = 0x1A, .giants = 0x2F, .gnome_giant = 0x07, .gnome_extra = 0x30,
                .elf_sleep = 0x5F, .halfelf = 0x12, .prot_evil = 0x08, .ranger_giant = 0x69,
                // a new magic-user's Detect Magic, Read Magic, Shield, Sleep
                .mu_first = {0x0B, 0x12, 0x13, 0x15},
                .pick_race = 0x1DCDC, .pick_gender = 0x1DCF1, .pick_class = 0x1DCFD, .pick_alignment = 0x1DD08,
                .select = 0x1DCEA, .reroll = 0x1DD39, .char_name = 0x1DD48, .save_q = 0x1DD59, .qmark = 0x1DD5F,
                .mu_change = {0x0B, 0x12, 0x0C, 0x15},          // (Human Change's, GAME.OVR 0x3D081)
                .pick_new = 0x3CD66, .no_qualify = 0x3CD54, .change_select = 0x3CD76, .now_first = 0x3CD7D,
                .dot = 0x3CD92,
                .mu_by_level = kSecretMuLevels, .mu_by_level_from = 6, .mu_by_level_n = 4},
     // Items' words (the same text as Curse's; the shop's Id price words not
     // found - its shops: later)
     .item_words = {0x28439, 0x2843E, 0x2844E, 0x28454, 0x2845B, 0x28461, 0x28467, 0x281AC, 0x2847C, 0x29078,
                    0x2848C, 0x284AC, 0x284C1, 0x28F9C, 0x29C21, 0x29C30, 0x2A139, 0x29CCE, 0x29D12, 0, 0x2A159,
                    0x2A17F, 0x2A148, 0x22A97, 0x28445},
     // The shop's Take / Appraise and the temple (secret_facts_2.md 17; its
     // temple overlay GAME.OVR 0x5AFE): menus, the cures' names (START.EXE's
     // data, 41 bytes each), no prices, each cure's own words; the
     // shopkeeper's / priest's coins words; Appraise; Take (" will you
     // take? " with its own space); the training hall's
     .shop_words = {0x66BD, 0x66E5, 0x64A1, 0x64B9,
                    kP | 0xDF6C, kP | 0xDF95, kP | 0xDFBE, kP | 0xDFE7, kP | 0xE010, kP | 0xE039, kP | 0xE062,
                    kP | 0xE08B, kP | 0xE0B4, kP | 0xE0DD,
                    0, 0, 0, 0, 0, kP | 0xDF62,
                    0x5AFE, 0x5B91, 0x5F95, 0x6220, 0x6336, 0x6405,
                    0x670C, 0x6757, 0x8C91, 0x8CE4,
                    0x35324, 0x35343, 0x3534A, 0x35354, 0x3535A, 0x35366, 0x3537C, 0x35381, 0x35386, 0x35390,
                    0x3539B, 0x352E1, 0x352F4, 0x352F9, 0x352FF, 0x35311,
                    0x34724, 0x34739, 0x3475B, 0x3474A, 0x33BD0,
                    0x21F82, 0, 0x21FA1, 0x21FC0, 0x21FD6, 0x21FE4, 0x21FF3, 0x22000, 0x22017,
                    0x5B0B, 0x5C86, 0x5C90, 0x5CA9, 0x5CCC, 0x5CD8, 0x5CEF, 0x5FA5, 0x5FB6, 0x620A, 0x6310, 0x6416},
     // The effects cures take away, in Secret's numbers (its temple is
     // free): blindness 0x21, diseases 0x1F 0x22 0x2B 0x2C (its temple's
     // list), feeblemind 0x44, poison 0x37, Slow Poison 0x16, poison's
     // damage 0x0F, Bestow Curse 0x24; the disease's course 0x22 / 0x2B /
     // 0x2C, helpless 0x1F
     .cures = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
               0x21, {0x1F, 0x22, 0x2B, 0x2C, 0, 0}, 0x44, 0x37, 0x16, 0x0F, 0, 0x24, 0x22, 0x2B, 0x2C, 0x1F},
     // A kept gem / jewel: type 47, words 122 / 123 (its Appraise's)
     .gem_type = 47, .gem_word = 0x7A, .jewel_word = 0x7B,
     // Camp magic (secret_facts.md 13): its menu, level names "1st".."7th" +
     // " Level", 119 spell names of 35 bytes, its words; effect names by
     // number (its View's table); the confirmations "Memorize" + " these
     // spells?"; the effects its cures and timed spells use in its numbers
     .magic = {.menu = 0xE220, .levels = {0xE805, 4, 7}, .names = {0x112AB, 35, 119},
               .words = {0x2ACB3, 0x2AC5E, 0x2AC68, 0x2AC9D, 0x2C821, 0x2C7EB, 0, 0, 0, 0x1AAEF, 0x1AB02,
                         0x1AB15, 0, 0x1A8B7, 0x1A8DE, 0x2BC18, 0x2BD1C, 0x2C3C3, 0x2BE99, 0x2BF60, 0x2C3D2},
               .rest_period = 0x7ED2, .rest_chance = 0x7ED3,
               .cast_words = {0x1A8CE, 0x1AA1E, 0x2C7E6, 0x2DA2C, 0x2D9C0, 0x2D9D6, 0x2D88B, 0x3ABA2, 0x3ABB2, kJ | 35,
                              0x37EA2, 0x2F808, kJ | 37, kJ | 26, kJ | 20, 0x2FDA0, 0x1B5DB, 0x2C7F4, 0x2AC88, 0x2ACA9, 0x1B095, 0x1B0AD,
                              0x1B0C9, 0x1B0ED, 0, 0, 0x1A8EE, 0x2C024, 0x2AC93, 0x2C7FB, 0x2D9E0, 0x2DA0A,
                              0x2DA23, 0x298FE, 0x2990B, 0x29911, 0x2AC74, 0},
               .camp = kSecretCamp, .n_camp = sizeof kSecretCamp / sizeof kSecretCamp[0],
               .facts = {2, 0x2A, {0x22, 0x2B, 0}, {{0, 0}, {0x2C, 0x1F}, {0, 0}}, 0x0C, 0x17, 0x32, 0x36, 0x70, 0x71,
                         // (its time routine GAME.OVR 0x2D5E2: Slow Poison 3780 minutes; no
                         // Curse's 0x3F / 0x43 cases - those are other spells here)
                         {{0x28, 1, 6, 0, 10}, {0x39, 5, 4, 0, 1}, {0x3D, 5, 4, 0, 1}, {0x3B, 1, 4, 4, 10},
                          {0x1A, 0, 0, 3780, 1}, {}}},
               .spell_named = nullptr, .n_spell_named = 0, .named = nullptr, .n_named = 0,
               .scroll_one_spell = 100, .read_magic = 0x10,
               .level_word = 0xE7FA, .effect_names = {0x10047, 38, 0x75}, .these_spells = 0xE1E8,
               // (its scrolls: types 39 / 40 - GAME.OVR 0x32A8B; their count word 0x2F,
               // "1 Spell" = 100 - 0x32C39; a reader's spells only - 0x2CF28)
               .scroll_types = {39, 40}, .scroll_count_at = 0x2F, .scroll_readers = true},
     // Fights (secret_facts.md 12): Curse's tables at Secret's places, no
     // terrain table, its turn undead table not found yet (no Turn); the
     // words where they are found; the effects by Secret's numbers (GBC's
     // effect lists and Secret's own View names: many of Curse's numbers
     // mean something else there - those are 0, or Secret's number for the
     // same thing)
     // Alter (camp): "Order Drop Speed Icon Level Exit" (no prompt; no Pics),
     // the party order's and the icon editor's menus, Level's choices
     .alter = {0xE249, 0xE48E, 0xE4B7,
               {0, 0x1BAFD, 0x1BB2B, 0x1BED6, 0x1BEE4, 0x1BEFB, 0x1BF03, 0x1BF0B, 0x1BF11, 0x1BC58, 0x1BC65,
                0x1BC4A, 0x1BC77, 0x1BC89, 0x1BC9E, 0, 0, 0, 0, 0},
               {0xE702, 0xE72B, 0xE754, 0xE77D, 0xE7A6},
               {0x21326, 0x2132A, 0x21339, 0x2133D, 0x21343, 0x2135C, 0x21361, 0x21386},
               kP | 0xE272},
     .fight = {{0x1A9E, 0x0350, 0x0360, 0x0370, 0x0374, 0x037C, 0x0384, 0, 0, 0},
              {               0x10016, 0xC0E5, 0xC0EB, 0xC0F5, 0xC0FA, 0, 0xC100, 0xC54A, 0xC56A, 0xC570, 0xC8EB, 0x17B38, 0x17B44,
               0x17B4C, 0x17B5E, 0x17B6A, 0xC9B8, 0xC9BF, 0xC9CB, 0xC9D4, 0x3AF83, 0xCC50, 0xCC5C, 0xCC62, 0xCC6A, 0xCC72,
               0x150F8, 0x15104, 0x15113, 0x1511B, 0x1512A, 0x1513E, 0x1514B, 0x15153, 0x1515C, 0x15166, 0x15171, 0x1517E, 0x15188,
               0x151B5, 0xC324, 0xC33B, 0x15C0A, 0x15C13, 0x17A4F, 0xCF82, 0xE211, 0xE223, 0x3B710, 0x15E29, 0x38DBE, 0x38DC8,
               0x38DCB, 0x73F8, 0, 0x740C, kP | 0xE130, 0x741F, 0x7438, 0x7F80, 0x81E0, 0x8216, 0x784E, 0x7855, 0x7989,
               0x7998, 0x799E, 0x79AB, 0x79C0, 0x79D8, 0x79F8, 0x765F, 0x7667, 0x37AAF, 0x37AB6, 0x37AC9, 0x37AE2, 0x37AEC,
               0x37AF6, 0x37B07, 0x37B11, 0x37B29, 0x37EA2, 0x32C5E, 0x32C6C, 0x17433, 0x17423, 0, 0, 0, 0,
               0x16E5E, 0x2DA32, 0x2DA40, 0x2EFF8, 0x138FE, 0x3156A, 0x36A24, 0x1184E, 0, 0x10FB2, 0, 0, 0x11FC9,
               0, 0, 0, 0, 0, 0, 0x12831, 0, 0, 0x12404, 0x1240D, 0x1241A, 0x1356D,
               0x13576, 0, kJ | 42, 0x12D51, 0, 0, 0, 0x11A94, 0x11A8A, 0x11AA0, 0x11AAD, 0x11935, 0x11942,
               0x144E5, 0x311DE, 0x311EB, 0x311FF, 0x3120D, 0, 0, 0x11434, 0, 0x11740, 0, 0x11CC8, 0,
               0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
               0, 0, 0, 0, 0, kP | 0xE146, kJ | 41, 0x128A6, 0x12FA0,
               0x3239A, 0x323BC, 0x13D5C, 0x128C3, 0x128DB
               },
               // (its DS 0x0A9A list)
               {0x03, 0x0B, 0x15, 0x17, 0x1B, 0x1E, 0x1F, 0x33, 0x34, 0x35, 0x5B, 0x6A, 0x6B, 0x6F},
               {.held = {0x33, 0x34, 0x35, 0x1F}, .bless = 0x01, .curse = 0x02, .prayer = 0x31, .haste = 0x27,
                .slow = 0x2A, .invisible = 0x19, .prot_evil = 0x08, .prot_good = 0x09, .mirror = 0x1C,
                .backstab_weapons = {3, 4, 17, 18, 19, 0}, .charm = 0x0B, .fear = 0x6F, .cough = 0x1E,
                .shot_pointed = {5, 7, 0, 12, 14, 30}, .shot_spinning = {2, 3, 6}, .shot_flask = {0, 0},
                .shot_sling = {28, 0, 29},
                .bestow = 0x24, .blink = 0x25, .fumbling = 0x1B, .silence = 0x15, .entangle = 0, .sticks = 0x03,
                .faerie = 0x47, .blinded = 0x21, .animals_blind = 0x45, .feeble = 0x44,
                // Its monsters' effects (MON1SPC): deadly poison 0x40 / -2 0x56, paralysis on a hit
                // 0x58, immune to nonmagical weapons 0x67, fire 0x5D, cold 0x62, electricity 0x06,
                // the iron golem's spell immunity 0x4F, the gazes 0x3A / 0x3B, breath 0x68 / 0x53 /
                // 0x50, the slug's spit 0x51, charm and sleep 0x60
                .mon = {.poison = 0x40, .poison_m2 = 0x56, .kreen_bite = 0x58, .heat_proof = {0x14, 0x36, 0x3D},
                        .need_magic = 0x67,
                        .no_fire = 0x5D, .no_cold = 0x62, .no_elec = 0x06,
                        .no_magic = 0x4F, .globe = 0x3F, .elf_sleep = 0x5F, .charm_sleep = 0x60,
                        .detect = 0x18, .prot_evil_near = 0x2D,
                        .fire_breath = 0x68, .hound_fire = 0x50, .slug_spit = 0x51, .stone_gaze = 0x3A,
                        .mirror_gaze = 0x3B,
                        .paralyzed = 0x34, .poisoned = 0x37, .sleep = 0x35,
                        // Secret's own (secret_facts_2.md 15): the iron golem, cockatrice,
                        // remorhaz, pyrohydra, umber hulk, the white dragons' cold breath, the
                        // dreadlord's immunities (fear; charm and sleep; poison and paralysis)
                        .need_plus3 = 0x3C, .petrify_hit = 0x41, .swallow = 0x42, .pyro_fire = 0x43,
                        .confuse_gaze = 0x46, .cold_breath = 0x53, .no_fear = 0x5C, .no_poison = 0x63,
                        .slug_secret = true,
                        // (secret_facts_2.md 29.monsters: 0x5A is the slug's blunt-weapon
                        // immunity, not an acid breath; 0x49 the dragons' breath immunity;
                        // the iron golem's damage rule; the gaze's mirror effect 0x48;
                        // the pseudo-spells 0x3D / 0x44's reaches; paralysis 10 rounds)
                        .blunt_proof = 0x5A, .no_breath = 0x49, .golem = 0x4F, .reflect = 0x48,
                        .far_reach = 6, .near_reach = 1, .para_rounds = 10, .secret_ways = true},
                .sp = {.enlarge = 0x0C, .confuse = 0x23, .berserk = 0x4D, .evil_ward = 0x04, .evil_bane = 0x20,
                       .hot = 0x32, .cold = 0x36, .zap = 0x70, .slow_poison = 0x16, .poison_damage = 0x0F,
                       .hammer = 0x17, .animated = 0, .con_regen = 0x3E, .giant = 0x71, .shield = 0x11,
                       .enfeeble = 0x1D, .ill = 0x22, .weak = 0x2B, .sick = 0x2C, .stun = 0x6A},
                // the races (GAME.OVR 0x10CCB's handlers by its effect phases, 0x3624E): its
                // own flag tests (secret_flags); 0x61 (Constitution) is in no phase and 0x2F /
                // 0x30 change the d20 before it's rolled (phase 0xB) - applied only with the
                // rules fixes (rules::fixes)
                .race = {0x61, 0x12, 0x1A, 0x07, 0x2F, 0x30, 0x69, true},
                .items = {.displace = 0x59, .ring_invisible = 0x38, .giant_slayer = 0x4B, .luck = 0x4E, .fire_ring = 0x3D,
                          .boots = 0x4A, .no_disease = 0x4C, .displace_unused = true,
                          .scrolls = {39, 40}},
                .berserk = 0x6B, .secret = true, .globe_major = 0x39,
                .cloud_ground = 0x1D, .poison_ground = 0x1B, .cleric_xp_wis = 15},
               kSecretFight, sizeof kSecretFight / sizeof kSecretFight[0]},
     // The script machine's words (GAME.OVR): the encounter's "Both sides
     // wait." / "The monsters flee.", DAMAGE's " dies. " / " is hit FOR " /
     // " points of Damage." / "The entire party is killed!", op 0x41's
     // "Congratulations the party gains experience!" / "Congratulations " /
     // " gains experience!"
     .script_words = {0x30F8, 0x3109, 0xB158, 0xB160, 0xB16D, 0x3971, 0, 0, 0, 0, 0, 0x43AF, 0x43DB, 0x43EC},
     // Random treasure (TREASURE 0x80 + n): Secret's own making (kSecretMake),
     // its rows of ready-made items at DS 0x0A2A (row r: word 1 + 8 r of DS 0x0A28)
     .random_items = &kSecretRandom, .random_rows = 0x0A2A,
     // The sound driver: Curse's in START.EXE (segment 0x7E4), 13 sounds
     // (the same byte code but the missile's), its Tandy byte code past
     // the driver's code (secret_facts_2.md 22)
     .sound = {0x7E40, 0x04A0, 0x1090, 0x04A0, 0x0508, 13, 0x2F, 0x33, 0x302C, 0x3400},
     .variant = {.program_words = true, .tile_window = 40, .bars = {55, 52, 53, 54, 59, 56, 57, 58}, .secret_files = true,
                 .bigpic_from = 0x70, .treasure_pic = 60, .camp_pic = 59, .appraise_div = 2, .fight_unswapped = true},
     .overland = nullptr,
     .font_file = "8X8D5.DAX",
     .joined = kSecretJoined, .n_joined = sizeof kSecretJoined / sizeof kSecretJoined[0],
     // The vault: its overlay's "vault.dat" and menu words (GAME.OVR 0x50B2)
     .vault = {"VAULT.DAT", 0x50BC, 0x50C1, 0x50C7},
     // The version screen (START.EXE's first code segment: 0x1E9 prints
     // the name at row 22 and the version at 23, colour 0 on 10, then the
     // menu with the prompt at 0x55)
     .title_name = 0x05, .title_prompt = 0x55},
    // Pools of Darkness, GOG release (darkness_facts.md; being built -
    // Tom, 2026-10-17): GAME.EXE 64,203 bytes, EXEPACK, unpacked 72,784; its
    // data segment at image 0x9E70; one file a kind (ECL1, GEO1 ...); VGA
    // pictures (their EGA colours on the classic boards); the font in
    // 8X8D0.DAX; the version line "POOLS OF DARKNESS 1.10 " and "Play Demo"
    // (image 0xC625 / 0xC9AB)
    {.game = games::Game::PoolsOfDarkness, .release = "GOG", .program = "GAME.EXE",
     .program_size = 64203, .image_size = 72784, .data_base = 0x9E70,
     .frame = {},
     .tiles_file = "8X8D0.DAX", .tiles_block = 202,
     .press_any_key = 0xAC82,
     .title_file = "TITLE.DAX", .title = kDarknessTitle, .title_steps = sizeof kDarknessTitle / sizeof kDarknessTitle[0],
     .title_version = 0xC625, .title_menu = 0xC9AB,
     .overlay = "GAME.OVR", .overlay_size = 264086,
     .first_area = 1, .last_area = 1,
     .ecl_ops = &kDarknessEcl,
     .start_area = 1, .start_script = 16,           // (a new game: block 16, Phlan's docks)
     // The party menu (darkness_facts.md 34): 13 entries of 42 at DS
     // 0x11B9; its words mostly in the program's data (kP: image =
     // 0x9E70 + DS) - "Select", "load which game: ", the list's headings
     // (GAME.OVR), POOL4.CFG, "save which game: ", the slots, "Saving...",
     // the camp's menu, Add's "add from where? " "Pools Secret Exit" "add a
     // character: " "Add", Drop's words (GAME.OVR), "Yes No", Modify's;
     // Load's "load from where? "
     .party = {.items = 0xB029, .count = 13, .stride = 42, .choose = kP | 0xC9FD, .load_which = kP | 0xC796,
               .name = 0x34652, .ac_hp = 0x34657, .cfg = "POOL4.CFG", .save_which = kP | 0xC7BF,
               .slots = kP | 0xCD2F, .saving = 0x1317B, .camp_menu = 0xD115, .makes_camp = kP | 0xBFBF,
               .add_from = kP | 0xBAC8, .add_sources = kP | 0xCC8B, .add_prompt = kP | 0xBAF1, .add = kP | 0xCCB4,
               .drop = 0x16445, .forever = 0x1644B, .sure = 0x16456, .yes_no = kP | 0xC9D4,
               .cant_modify = kP | 0xBA9F, .keep_exit = kP | 0xCC62,
               .load_from = kP | 0xC76D, .load_sources = kP | 0xCC8B},
     // View Character's name tables (DS 0x0E57 classes, 0x103D races in its
     // own order, 0x107C alignments, 0x1115 sex, 0x1123 its three coins,
     // 0x1144 health) and its words (GAME.OVR 0x23891 on)
     .view = {{0xACC7, 27, 18}, {0xAEAD, 9, 7}, {0xAEEC, 17, 9}, {0xAF85, 7, 2}, {0xAF93, 11, 3}, {0xAFB4, 13, 9},
              0, 0x23899, 0x238B4, 0x238A2, 0x238A8, 0x23891, 0x23FEB, 0x23FDF, 0x24003, 0x2401B, 0x23FF7,
              0x2400C, 0, 5},
     // Items (darkness_facts.md 31): 125 name words of 21 bytes (DS 0x3354;
     // Secret's numbering - arrows 30, quarrels 12, darts 5), the ITEMS type
     // file without a head; "Items" (DS 0x1738)
     .items = {.words = {0xD1C4, 21, 125}, .types_file = "ITEMS", .arrow = 30, .quarrel = 12, .dart = 5,
               .title = kP | 0xB5A8, .types_head = 0},
     // The script machine's words (darkness_facts.md S10): DAMAGE's lines,
     // PRESS KEY's prompt, JOURNAL's sentence, WHAT MENU's question, "Yes No"
     .script_words = {0, 0, 0x7B42, 0x7B4A, 0x7B57, 0x19C2, kP | 0xAC82, 0x261D, 0x2640, 0x1212, kP | 0xC9D4},
     .variant = {.unfinished = true, .program_words = true, .bigpic_from = 0xF0, .camp_pic = 128, .vga = true, .darkness_files = true,
                 .party_menu_col = 9},
     .font_file = "8X8D0.DAX"},
};

} // namespace

const ViewFacts& view_facts(games::Game g)
{
    // (secret_facts_2.md 16; Curse's words at the same places coab's View
    // prints them; Pool has no paladin's powers)
    static const ViewFacts kCurse = {{0x27B87, 0x27B8F, 0, 0x27B96, 0x27B9C, 0x27BA2, 0x29C57, 0x29C63, 0x29C80,
                                      0x29C8A, 0x29FE4, 0x2A70B, 0x2A717, 0x2A725, 0x2A815, 0x2A821, 0x2A831,
                                      0x2A83F},
                                     0x8C, 0x8D};
    static const ViewFacts kPool = {{0x21AAD, 0x21AB5, 0, 0x21ABC, 0, 0, 0x23154, 0x23160, 0x23175, 0x2317F,
                                     0x234D1, 0, 0, 0, 0, 0, 0, 0},
                                    0, 0};
    static const ViewFacts kSecret = {{0x27E21, 0x27E29, 0x27E30, 0x27E39, 0x27E3F, 0x27E45, 0x2A3AC, 0x2A3B8,
                                       0x2A3D5, 0x2A3DF, 0x2A767, 0x2AEF8, 0x2AF04, 0x2AF12, 0x2B004, 0x2B010,
                                       0x2B020, 0x2B02E, 0x2A754},
                                      0x6D, 0x6E};
    // Darkness: its menu one string (DS 0x2D50), the questions in its data
    // segment and GAME.OVR (Lay = Secret's Heal; effects 0x6D / 0x6E as Secret's)
    static const ViewFacts kDarkness = {{0, 0, 0, 0, 0, 0, kP | 0xB504, kP | 0xB4DB, kP | 0xB3E5, kP | 0xB52D,
                                         kP | 0xB7BD, 0x26B9A, 0x26BA6, 0x26BB4, 0x26C9F, 0x26CAB, 0x26CBB,
                                         0x26CC9, kP | 0xB7E6},
                                        0x6D, 0x6E, kP | 0xCBC0};
    static const ViewFacts kNone = {};
    switch (g) {
    case games::Game::CurseOfTheAzureBonds: return kCurse;
    case games::Game::PoolOfRadiance: return kPool;
    case games::Game::SecretOfTheSilverBlades: return kSecret;
    case games::Game::PoolsOfDarkness: return kDarkness;
    default: return kNone;
    }
}

const char* program_name(games::Game g)
{
    for (const Profile& p : kProfiles)
        if (p.game == g) return p.program;
    return nullptr;
}

bool title_flash_step(const Profile& p, int step)
{
    if (!p.variant.title_flash || step < 0 || step >= p.title_steps || !p.title[step].block) return false;
    for (int k = step + 1; k < p.title_steps; ++k)
        if (p.title[k].block) return false;
    return true;
}

bool ready(games::Game g)
{
    for (const Profile& p : kProfiles)
        if (p.game == g && !p.variant.unfinished) return true;
    return false;
}

const Profile* find(games::Game g, uint32_t program_size, uint32_t image_size)
{
    for (const Profile& p : kProfiles)
        if (p.game == g && p.program_size == program_size && p.image_size == image_size) return &p;
    return nullptr;
}

bool has_item_fixes(games::Game g)
{
    for (const Profile& p : kProfiles)
        if (p.game == g && (p.items.flame.type || p.variant.secret_files)) return true;   // (Secret: rules::fixes)
    return false;
}

} // namespace profile

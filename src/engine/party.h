// The party: the player's characters, as the games keep them in their save
// files (Curse first).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// A character is kept as the game's own record (Curse: 422 bytes, the
// .SAV / .GUY file), so saving writes back exactly what was read; the
// accessors below read the fields the engine uses. Its items (.SWG: 63
// bytes each) and effects (.FX: 9 bytes each) are kept the same way.
// Layout learned from coab (Curse of the Azure Bonds) and checked on the
// GOG release's sample party (SAVE/CHRDATA1-6.SAV); own code.
//
// Curse record (offsets in bytes):
//   0x00 name (Pascal string, 15 max)
//   0x10 Str, Int, Wis, Dex, Con, Cha, Str00 - each current, full
//   0x73 THAC0 (signed), 0x74 race, 0x75 class, 0x76 age (u16), 0x78 HP max
//   0xDF saving throws (5), 0xE5 hit dice, 0xEA thief skills (8)
//   0xF7 control (>= 0x80 an NPC), 0xFB money: copper, silver, electrum,
//   gold, platinum, gems, jewellery (u16 each)
//   0x109 class levels (8: cleric, druid, fighter, paladin, ranger,
//   magic-user, thief, monk), 0x111 levels before a class change (8)
//   0x119 sex, 0x11B alignment, 0x127 experience (u32), 0x13C, 0x13E,
//   0x192 flags, 0x195 health (0 okay ... 6 dead, 7 stoned, 8 gone),
//   0x187 encumbrance (u16), 0x196 in the party's fights (1), 0x197 side
//   (0 ours), 0x199 to-hit bonus (THAC0 shown as 60 - this), 0x19A AC
//   (shown as 60 - this), 0x19E / 0x1A0 / 0x1A2 damage dice, sides, bonus
//   (signed), 0x1A4 HP, 0x1A5 movement
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace party {

constexpr size_t kRecordSize = 0x1A6;   // Curse
constexpr size_t kItemSize = 0x3F;
constexpr size_t kAffectSize = 9;
constexpr int    kMaxItems = 16;
constexpr int    kMaxAffects = 32;
constexpr int    kMaxParty = 8;
constexpr int    kNameMax = 15;

enum Health : uint8_t { Okay = 0, Animated, TempGone, Running, Unconscious, Dying, Dead, Stoned, Gone };

// What Secret of the Silver Blades' 439-byte record has and Curse's
// layout hasn't (party::from_secret keeps it here, to_secret writes it back):
// spells 101-117 in the book, the 6th / 7th level cleric slots, Secret
// 0x13E-0x146 (druid 6-7 and a group of 7 not known yet), the 6th / 7th
// level magic-user slots, the bytes 0x6D, 0x15E (flags 2), 0x15F and 0x1A5
// (magic resistance)
constexpr size_t kExtSize = 35;
enum Ext : uint8_t {
    kExtBook = 0,                       // spells 101-117 (17)
    kExtCleric67 = 17,                  // Secret 0x137-0x138
    kExtDruidMore = 19,                 // Secret 0x13E-0x146 (9)
    kExtMage67 = 28,                    // Secret 0x14C-0x14D
    kExt6D = 30, kExtFlags2 = 31, kExt15F = 32, kExtMagicRes = 33,
    kExtRace = 34,                      // Secret's own race number (its races are numbered otherwise)
};

struct Character {
    uint8_t rec[kRecordSize] = {};
    uint8_t ext[kExtSize] = {};          // Secret's fields Curse has no place for (Ext)
    uint8_t items[kMaxItems][kItemSize] = {};
    int     n_items = 0;
    uint8_t affects[kMaxAffects][kAffectSize] = {};
    int     n_affects = 0;

    void name(char* out, size_t cap) const;
    int  hp() const { return rec[0x1A4]; }
    int  hp_max() const { return rec[0x78]; }
    int  ac_raw() const { return rec[0x19A]; }
    int  ac() const { return 60 - rec[0x19A]; }     // as the games show it
    int  race() const { return rec[0x74]; }
    int  cls() const { return rec[0x75]; }
    int  sex() const { return rec[0x119]; }
    int  alignment() const { return rec[0x11B]; }
    int  age() const { return rec[0x76] | rec[0x77] << 8; }
    int  level(int cls_index) const { return rec[0x109 + cls_index]; }
    int  old_level(int cls_index) const { return rec[0x111 + cls_index]; }
    int  str00() const { return rec[0x1C]; }
    int  thac0() const { return 60 - rec[0x199]; }
    int  dice() const { return rec[0x19E]; }
    int  dice_sides() const { return rec[0x1A0]; }
    int  damage_bonus() const { return static_cast<int8_t>(rec[0x1A2]); }
    int  encumbrance() const { return rec[0x187] | rec[0x188] << 8; }
    int  movement() const { return rec[0x1A5]; }
    bool has_affect(uint8_t type) const
    {
        for (int i = 0; i < n_affects; ++i)
            if (affects[i][0] == type) return true;
        return false;
    }
    int  stat(int i) const { return rec[0x10 + i * 2 + 1]; }   // full value: 0 Str ... 5 Cha, 6 Str00
    int  stat_now(int i) const { return rec[0x10 + i * 2]; }
    uint32_t exp() const { return rec[0x127] | rec[0x128] << 8 | rec[0x129] << 16 | static_cast<uint32_t>(rec[0x12A]) << 24; }
    int  money(int i) const { return rec[0xFB + i * 2] | rec[0xFC + i * 2] << 8; }
    int  health() const { return rec[0x195]; }
    bool in_combat() const { return rec[0x196] != 0; }
    bool enemy() const { return rec[0x197] != 0; }
    bool npc() const { return rec[0xF7] >= 0x80; }
};

struct Party {
    Character m[kMaxParty];
    int count = 0;
    int selected = 0;
    Character* sel() { return count ? &m[selected] : nullptr; }
    const Character* sel() const { return count ? &m[selected] : nullptr; }
    void clear() { count = selected = 0; }
};

// Reads a character record (.SAV / .GUY): false if the file is too short.
bool read_record(dax::ByteSource& src, Character& out);
// Its items (.SWG) and effects (.FX): as many whole records as the file
// holds (up to the limits).
void read_items(dax::ByteSource& src, Character& out);
void read_affects(dax::ByteSource& src, Character& out);

// Pool of Radiance's characters (poolrad_facts.md 3.3): a 285-byte record
// (.SAV / .CHA), items in .ITM (63 bytes each, as Curse's), effects in
// .SPC (9 bytes, as Curse's .FX). The engine works on Curse's layout, so a
// Pool record is moved into it field by field when read and back when
// written: one value per stat (both of Curse's), the 21 memorized spells
// (in place, in Curse's list), 55 spell book bytes, cleric / magic-user
// slots for levels 1-3, the portrait's head / body; its in-memory pointers
// are written as 0, its item count from the items kept.
constexpr size_t kPoolRecordSize = 0x11D;
constexpr int    kPoolMemorized = 21;
// Secret of the Silver Blades' characters (secret_facts.md 7): a 439-byte
// record (.SAV / .WHO), items in .STF (67 bytes each: Curse's 63 and 4
// more, zero), effects in .SFX (9 bytes, as Curse's .FX). Moved into
// Curse's layout field by field (75 memorized spells in place, spells
// 1-100 of the book, levels 1-5 of the spell slots ...); what has no place
// there is kept in Character::ext; its pointers are written as 0, its item
// count from the items kept. Its races are numbered otherwise (0 tribble,
// elf, half-elf, dwarf, gnome, halfling, human, 7 monster; Curse: 0
// monster, dwarf, elf, gnome, half-elf, halfling, half-orc, 7 human): the
// record gets Curse's number.
uint8_t race_from_secret(uint8_t secret);
uint8_t race_to_secret(uint8_t curse);
constexpr size_t kSecretRecordSize = 0x1B7;
constexpr int    kSecretMemorized = 75;
constexpr size_t kSecretItemSize = 0x43;
// Record format: Curse's, Pool's or Secret's
enum class Format : uint8_t { Curse, Pool, Secret };
bool read_record(dax::ByteSource& src, Character& out, Format f);
void from_pool(const uint8_t* pool, Character& out);              // out.rec only
void from_pool(const uint8_t* pool, uint8_t* rec);                 // a record (kRecordSize bytes) of Curse's layout
void to_pool(const Character& c, uint8_t out[kPoolRecordSize]);
void from_secret(const uint8_t* secret, Character& out);          // out.rec and out.ext
void from_secret(const uint8_t* secret, uint8_t* rec);             // a record of Curse's layout (no ext)
void to_secret(const Character& c, uint8_t out[kSecretRecordSize]);
// Items as a game's file holds them: `stride` bytes a record, the first
// kItemSize kept (Secret's .STF: kSecretItemSize)
void read_items(dax::ByteSource& src, Character& out, uint32_t stride);
// The record as the game's file holds it (*n: its size)
const uint8_t* record_bytes(const Character& c, Format f, uint8_t* buf, size_t* n);

// What the scripts read at 0x7C00 + off about the selected character
// (race, class, thief skills, money ...). False if off isn't a character
// field (the scripts' memory holds it).
// pool: Pool of Radiance's program (fewer fields: Int, race, class, the
// magic-user's level, sex and 0xE4 are the scripts' memory there)
bool script_value(const Party& p, uint16_t off, uint16_t* out, bool pool = false);
// The same for any record (a loaded monster's too); index = its place in the
// list (0x7EB1 / 0x7EB4)
bool script_value(const uint8_t* rec, int index, uint16_t off, uint16_t* out, bool pool = false);
// What a script's write at 0x7C00 + off does to the selected character:
// money, the control byte (over 0xB2: - 0x32), 0x7CF7 / 0x7CF9, spell slots
// (0x7C20-0x7C70: record off - 1), 0x7D00 >= 0x80 out of action (0x87
// stoned), 0x7D0C the side (0, 0x80 the computer's, 0x81 the enemy's).
void script_set(Party& p, uint16_t off, uint16_t v);
void script_set(uint8_t* rec, uint16_t off, uint16_t v);
// A member leaves (NPCs, DUMP, a script's removal): the one before is selected
void remove(Party& p, int i);

} // namespace party

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

struct Character {
    uint8_t rec[kRecordSize] = {};
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

// What the scripts read at 0x7C00 + off about the selected character
// (race, class, thief skills, money ...). False if off isn't a character
// field (the scripts' memory holds it).
bool script_value(const Party& p, uint16_t off, uint16_t* out);
// The same for any record (a loaded monster's too); index = its place in the
// list (0x7EB1 / 0x7EB4)
bool script_value(const uint8_t* rec, int index, uint16_t off, uint16_t* out);
// What a script's write at 0x7C00 + off does to the selected character:
// money, the control byte (over 0xB2: - 0x32), 0x7CF7 / 0x7CF9, spell slots
// (0x7C20-0x7C70: record off - 1), 0x7D00 >= 0x80 out of action (0x87
// stoned), 0x7D0C the side (0, 0x80 the computer's, 0x81 the enemy's).
void script_set(Party& p, uint16_t off, uint16_t v);
void script_set(uint8_t* rec, uint16_t off, uint16_t v);
// A member leaves (NPCs, DUMP, a script's removal): the one before is selected
void remove(Party& p, int i);

} // namespace party

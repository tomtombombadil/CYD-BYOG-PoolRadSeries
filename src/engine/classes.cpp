#include "classes.h"

#include <cstring>

namespace classes {

namespace {

// Curse record offsets (party.h lists the rest)
constexpr int kThac0 = 0x73, kSpellBook = 0x79, kSaves = 0xDF, kHitDice = 0xE5, kMultiLevel = 0xE6;
constexpr int kThief = 0xEA, kLevels = 0x109, kOldLevels = 0x111, kAttacks = 0x11C, kClassFlags = 0x12B;
constexpr int kCastCount = 0x12D;          // [3][5]: cleric, druid, magic-user spell levels 1-5
constexpr int kStrFull = 0x11, kWisFull = 0x15, kDexFull = 0x17, kConFull = 0x19;
constexpr int kAnimateDead = 0x24;         // not one clerics learn by levelling (Curse)

uint8_t& cast(party::Character& c, int cls, int lvl) { return c.rec[kCastCount + cls * 5 + lvl]; }

void learn(party::Character& c, int spell)
{
    if (spell >= 1 && spell <= 100) c.rec[kSpellBook + spell - 1] = 1;
}

// A cleric's extra spells for wisdom
void wisdom_spells(party::Character& c)
{
    const int w = c.rec[kWisFull];
    if (w > 12 && cast(c, 0, 0) > 0) ++cast(c, 0, 0);
    if (w > 13 && cast(c, 0, 0) > 0) ++cast(c, 0, 0);
    if (w > 14 && cast(c, 0, 1) > 0) ++cast(c, 0, 1);
    if (w > 15 && cast(c, 0, 1) > 0) ++cast(c, 0, 1);
    if (w > 16 && cast(c, 0, 2) > 0) ++cast(c, 0, 2);
    if (w > 17 && cast(c, 0, 3) > 0) ++cast(c, 0, 3);
}

} // namespace

bool Tables::set(const Layout& l, const uint8_t* bytes, size_t n)
{
    if (l.hi <= l.lo || static_cast<size_t>(l.hi - l.lo) > sizeof data_ || n < static_cast<size_t>(l.hi - l.lo)) return false;
    lay = l;
    memcpy(data_, bytes, l.hi - l.lo);
    return true;
}

int32_t Tables::i32(uint16_t ds) const
{
    return static_cast<int32_t>(static_cast<uint32_t>(u16(ds)) | static_cast<uint32_t>(u16(static_cast<uint16_t>(ds + 2))) << 16);
}

int skill_level(const party::Character& c, int cls)
{
    int lv = c.level(cls);
    if (c.race() == 7) {
        int k = 0;
        while (k < 7 && c.level(k) == 0) ++k;
        if (c.level(k) > c.rec[kMultiLevel]) lv += c.old_level(cls);
    }
    return lv;
}

void spell_slots(party::Character& c, const Tables& t)
{
    for (int i = 0; i < 15; ++i) c.rec[kCastCount + i] = 0;
    for (int cls = Cleric; cls <= Monk; ++cls) {
        const int lv = skill_level(c, cls);
        if (lv <= 0) continue;
        switch (cls) {
        case Cleric:
            cast(c, 0, 0) += 1;
            for (int L = 2; L <= lv && L <= 12; ++L)
                for (int k = 0; k < 5; ++k) cast(c, 0, k) += t.slots_gained(Cleric, L, k);
            wisdom_spells(c);
            for (int s = 1; s < t.lay.spell_count; ++s)
                if (t.spell_class(s) == 0 && t.spell_level(s) >= 1 && t.spell_level(s) <= 5 &&
                    cast(c, 0, t.spell_level(s) - 1) > 0 && s != kAnimateDead)
                    learn(c, s);
            break;
        case Paladin:
            if (lv > 8) {
                for (int L = 9; L <= lv && L <= 12; ++L)
                    for (int k = 0; k < 5; ++k) cast(c, 0, k) += t.slots_gained(Paladin, L, k);
                for (int s = 1; s < t.lay.spell_count; ++s)
                    if (t.spell_class(s) == 0 && t.spell_level(s) >= 1 && t.spell_level(s) <= 5 &&
                        cast(c, 0, t.spell_level(s) - 1) > 0)
                        learn(c, s);
            }
            break;
        case Ranger:
            if (lv > 7) {
                for (int L = 8; L <= lv && L <= 12; ++L) {
                    for (int k = 0; k < 3; ++k) cast(c, 1, k) += t.slots_gained(Ranger, L, k);
                    for (int k = 3; k < 5; ++k) cast(c, 2, k - 3) += t.slots_gained(Ranger, L, k);
                }
                for (int s = 1; s < t.lay.spell_count; ++s)
                    if (t.spell_class(s) == 1) learn(c, s);
            }
            break;
        case MagicUser:
            cast(c, 2, 0) += 1;
            for (int L = 2; L <= lv && L <= 12; ++L)
                for (int k = 0; k < 5; ++k) cast(c, 2, k) += t.slots_gained(MagicUser, L, k);
            break;
        default:
            break;
        }
    }
}

void saving_throws(party::Character& c, const Tables& t)
{
    for (int kind = 0; kind < 5; ++kind) {
        int best = 20;
        for (int cls = 0; cls < kClasses; ++cls) {
            const int lv = c.level(cls);
            if (lv <= 0) continue;
            const int v = t.save(cls, lv > 12 ? 12 : lv, kind);
            if (v < best) best = v;
        }
        c.rec[kSaves + kind] = static_cast<uint8_t>(best);
    }
    // Dwarves and halflings: a poison bonus by their constitution (added to
    // the poison number, as the games do); everyone with Con 19+
    const int con = c.rec[kConFull];
    int add = 0;
    if (c.race() == 1 || c.race() == 5) {
        if (con >= 4 && con <= 6) add = 1;
        else if (con >= 7 && con <= 10) add = 2;
        else if (con >= 11 && con <= 13) add = 3;
        else if (con >= 14 && con <= 17) add = 4;
        else if (con == 18) add = 5;
    }
    if (con == 19 || con == 20) add += 1;
    else if (con == 21 || con == 22) add += 2;
    else if (con == 23 || con == 24) add += 3;
    else if (con == 25) add += 4;
    c.rec[kSaves] = static_cast<uint8_t>(c.rec[kSaves] + add);
}

void thief_skills(party::Character& c, const Tables& t)
{
    int lv = skill_level(c, Thief);
    if (lv > 12) lv = 12;
    const int dex = c.rec[kDexFull];
    for (int skill = 1; skill <= 8; ++skill) {
        const int race_adj = t.s8(static_cast<uint16_t>(t.lay.thief_race + c.race() * 8 + skill));
        const int base = t.u8(static_cast<uint16_t>(t.lay.thief_base + lv * 8 + skill));
        int v;
        if (race_adj < 0 && base < -race_adj) {
            v = 0;
        } else {
            v = base + race_adj;
            if (skill < 6) v += t.s8(static_cast<uint16_t>(t.lay.thief_dex + dex * 5 + skill));
        }
        c.rec[kThief + skill - 1] = static_cast<uint8_t>(v);
    }
}

void class_bonuses(party::Character& c, const Tables& t)
{
    int thac0 = 0, hd = c.rec[kHitDice];
    for (int cls = 0; cls < kClasses; ++cls) {
        const int lv = c.level(cls);
        const int v = t.thac0(cls, lv > 12 ? 12 : lv);
        if (v > thac0) thac0 = v;
        if (lv > hd) hd = lv;
    }
    c.rec[kThac0] = static_cast<uint8_t>(thac0);
    c.rec[kHitDice] = static_cast<uint8_t>(hd);
    if (c.level(Fighter) >= 7 || c.level(Paladin) >= 7 || c.level(Ranger) >= 8) c.rec[kAttacks] = 3;
    spell_slots(c, t);
    saving_throws(c, t);
    if (c.level(Thief) > 0) thief_skills(c, t);
    int flags = 0;
    for (int cls = 0; cls < kClasses; ++cls)
        if (c.level(cls) > 0 || (c.old_level(cls) > 0 && c.old_level(cls) < hd))
            flags += t.u8(static_cast<uint16_t>(t.lay.class_flags + cls));
    c.rec[kClassFlags] = static_cast<uint8_t>(flags);
    (void)kStrFull;
    (void)kLevels;
    (void)kOldLevels;
}

} // namespace classes

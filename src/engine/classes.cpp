#include "classes.h"

#include "items.h"
#include "rules.h"

#include <cstring>
#include <initializer_list>
#include <new>

namespace classes {

namespace {

// Curse record offsets (party.h lists the rest)
constexpr int kThac0 = 0x73, kSpellBook = 0x79, kSaves = 0xDF, kHitDice = 0xE5, kMultiLevel = 0xE6;
constexpr int kThief = 0xEA, kLevels = 0x109, kOldLevels = 0x111, kAttacks = 0x11C, kClassFlags = 0x12B;
constexpr int kCastCount = 0x12D;          // [3][5]: cleric, druid, magic-user spell levels 1-5
constexpr int kStrFull = 0x11, kWisFull = 0x15, kDexFull = 0x17, kConFull = 0x19;
constexpr int kAnimateDead = 0x24;         // not one clerics learn by levelling (Curse)

// Spell slots of spell level lvl + 1 (0 cleric, 1 druid, 2 magic-user):
// levels 1-5 in the record, Secret's 6 and 7 in its extension
uint8_t& cast(party::Character& c, int cls, int lvl)
{
    if (lvl < 5) return c.rec[kCastCount + cls * 5 + lvl];
    static const uint8_t kAt[3] = {party::kExtCleric67, party::kExtDruidMore, party::kExtMage67};
    static const uint8_t kAt89[3] = {party::kExtCleric89, party::kExtDruid89, party::kExtMage89};
    return lvl < 7 ? c.ext[kAt[cls] + (lvl - 5)] : c.ext[kAt89[cls] + (lvl - 7)];
}

void clear_slots(party::Character& c, const Tables& t)
{
    for (int i = 0; i < 15; ++i) c.rec[kCastCount + i] = 0;
    if (t.slot_levels() > 5)
        for (int cls = 0; cls < 3; ++cls)
            for (int lvl = 5; lvl < t.slot_levels(); ++lvl) cast(c, cls, lvl) = 0;
}

void learn(party::Character& c, int spell) { to_book(c, spell); }

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

bool Tables::set(const Layout& l, const uint8_t* bytes, size_t n, const uint8_t* bytes2, size_t n2)
{
    if (l.hi <= l.lo || static_cast<size_t>(l.hi - l.lo) > 8192 || n < static_cast<size_t>(l.hi - l.lo)) return false;
    const size_t m2 = l.hi2 > l.lo2 ? static_cast<size_t>(l.hi2 - l.lo2) : 0;
    if (m2 > sizeof data2_ || (m2 && (!bytes2 || n2 < m2))) return false;
    uint8_t* d = new (std::nothrow) uint8_t[l.hi - l.lo];
    if (!d) return false;
    delete[] data_;
    data_ = d;
    lay = l;
    memcpy(data_, bytes, l.hi - l.lo);
    if (m2) memcpy(data2_, bytes2, m2);
    return true;
}

bool Tables::adopt(const Layout& l, uint8_t* owned, size_t n, const uint8_t* bytes2, size_t n2)
{
    const size_t m2 = l.hi2 > l.lo2 ? static_cast<size_t>(l.hi2 - l.lo2) : 0;
    if (!owned || l.hi <= l.lo || static_cast<size_t>(l.hi - l.lo) > 8192 || n < static_cast<size_t>(l.hi - l.lo) ||
        m2 > sizeof data2_ || (m2 && (!bytes2 || n2 < m2))) {
        delete[] owned;
        return false;
    }
    delete[] data_;
    data_ = owned;
    lay = l;
    if (m2) memcpy(data2_, bytes2, m2);
    return true;
}

Tables& Tables::operator=(const Tables& o)
{
    if (this == &o) return *this;
    delete[] data_;
    data_ = nullptr;
    lay = o.lay;
    if (o.data_ && o.lay.hi > o.lay.lo) {
        data_ = new (std::nothrow) uint8_t[o.lay.hi - o.lay.lo];
        if (data_) memcpy(data_, o.data_, o.lay.hi - o.lay.lo);
        else lay = Layout{};
    }
    memcpy(data2_, o.data2_, sizeof data2_);
    return *this;
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

// Pool (create_facts.md 2): a level-1 cleric or magic-user has one 1st
// level slot; from level 2 the table's TOTALS for the level (cleric and
// magic-user, levels 1-3); a cleric's Wisdom bonus, and every cleric
// spell of the levels they can cast (Animate Dead too)
static void pool_slots(party::Character& c, const Tables& t)
{
    for (int i = 0; i < 15; ++i) c.rec[kCastCount + i] = 0;
    for (const int cls : {static_cast<int>(Cleric), static_cast<int>(MagicUser)}) {
        const int lv = c.level(cls), row = cls == Cleric ? 0 : 2;
        if (lv <= 0) continue;
        if (lv == 1) cast(c, row, 0) = 1;
        else
            for (int k = 0; k < 3; ++k) cast(c, row, k) = static_cast<uint8_t>(t.slots_total(cls, lv > 9 ? 9 : lv, k));
        if (cls != Cleric) continue;
        wisdom_spells(c);
        for (int s = 1; s < t.lay.spell_count; ++s)
            if (t.spell_class(s) == 0 && t.spell_level(s) >= 1 && t.spell_level(s) <= 3 &&
                cast(c, 0, t.spell_level(s) - 1) > 0)
                learn(c, s);
    }
}

// Darkness (darkness_facts.md 37; ovr 0x3809F, 0x3860C, 0x3874F): the
// class records' totals at the level (to 29th); a cleric's Wisdom bonus,
// no 6th level under Wisdom 17, no 7th under 18, every cleric spell of the
// levels they have; a paladin past 8th the cleric slots 1-4 of its own
// record; a ranger past 7th druid 1-3 (columns 1-3) and magic-user 1-2
// (columns 5-6); a magic-user's from its record, then no 6th under
// Intelligence 12, 7th under 14, 8th under 16, 9th under 18
static void dk_slots(party::Character& c, const Tables& t)
{
    clear_slots(c, t);
    const int wis = c.rec[kWisFull], intel = c.rec[0x13];
    int lv = skill_level(c, Cleric);
    if (lv > 0) {
        for (int k = 0; k < 9; ++k) cast(c, 0, k) = static_cast<uint8_t>(t.slots_at(Cleric, lv, k + 1));
    }
    lv = skill_level(c, Paladin);
    if (lv > 8)
        for (int k = 0; k < 4; ++k)
            cast(c, 0, k) = static_cast<uint8_t>(cast(c, 0, k) + t.slots_at(Paladin, lv, k + 1));
    if (skill_level(c, Cleric) > 0) {
        wisdom_spells(c);
        if (wis < 17) cast(c, 0, 5) = 0;
        if (wis < 18) cast(c, 0, 6) = 0;
    }
    lv = skill_level(c, Ranger);
    if (lv > 7) {
        for (int k = 0; k < 3; ++k) cast(c, 1, k) = static_cast<uint8_t>(cast(c, 1, k) + t.slots_at(Ranger, lv, k + 1));
        for (int k = 0; k < 2; ++k) cast(c, 2, k) = static_cast<uint8_t>(cast(c, 2, k) + t.slots_at(Ranger, lv, k + 5));
    }
    lv = skill_level(c, MagicUser);
    if (lv > 0) {
        for (int k = 0; k < 9; ++k) cast(c, 2, k) = static_cast<uint8_t>(cast(c, 2, k) + t.slots_at(MagicUser, lv, k + 1));
        static const int kNeed[4] = {12, 14, 16, 18};
        for (int k = 0; k < 4; ++k)
            if (intel < kNeed[k]) cast(c, 2, 5 + k) = 0;
    }
    // The spells clerics (and paladins) and rangers know: those of the levels they have slots for
    for (int s = 1; s < t.lay.spell_count; ++s) {
        const int k = t.spell_class(s), lvl = t.spell_level(s);
        if (lvl < 1 || lvl > 9 || k > 2) continue;
        if (k == 0 && cast(c, 0, lvl - 1) > 0) learn(c, s);
        else if (k == 1 && cast(c, 1, lvl - 1) > 0) learn(c, s);
        else if (k == 2 && skill_level(c, Ranger) > 7 && skill_level(c, MagicUser) <= 0 && cast(c, 2, lvl - 1) > 0)
            learn(c, s);
    }
}

namespace {
// Champions' cleric spells (GAME.OVR 0x3A03A): for each spell level 1-4
// with cleric slots, the spells of that level's list
void krynn_cleric_spells(party::Character& c, const Tables& t, int top = 4)
{
    for (int L = 1; L <= top && t.lay.cleric_lists; ++L) {
        if (cast(c, 0, L - 1) <= 0) continue;
        for (int i = 1; i <= 10; ++i) {
            const int s = t.u8(static_cast<uint16_t>(t.lay.cleric_lists + L * 10 + i));
            if (!s) break;
            learn(c, s);
        }
    }
}
// Death Knights (its GAME.OVR ovl 0xE5 +0, the cleric's part +0x71E): a
// cleric 1 slot of 1st level (2 with a god past the fourth), the gains of
// its row from 2nd level, the Wisdom bonus, the lists' spells of the levels
// it has; a paladin past 8th its table's cleric slots 1-4 and their lists; a
// knight of the Sword / Rose past 5th its order's row (1-7) and the lists; a
// ranger past 7th its table's druid 1-4 and magic-user 1-4, and every druid
// spell of the levels it has slots for; a magic-user 1 slot of 1st level and
// its robe's row (1-8)
void dkk_slots(party::Character& c, const Tables& t)
{
    clear_slots(c, t);
    int lv = c.level(Cleric);
    if (lv > 0) {
        const int row = t.xp_row(&c, Cleric);
        cast(c, 0, 0) = static_cast<uint8_t>(row ? 2 : 1);
        for (int L = 2; L <= lv; ++L)
            for (int k = 0; k < 7; ++k) cast(c, 0, k) = static_cast<uint8_t>(cast(c, 0, k) + t.slots_gained(row, L, k));
        wisdom_spells(c);
        krynn_cleric_spells(c, t, 7);
    }
    lv = c.level(Paladin);
    if (lv > 8 && t.lay.paladin_slots) {
        for (int L = 9; L <= lv; ++L)
            for (int k = 0; k < 4; ++k)
                cast(c, 0, k) = static_cast<uint8_t>(cast(c, 0, k) +
                                                     t.u8(static_cast<uint16_t>(t.lay.paladin_slots + L * 8 + k + 1)));
        krynn_cleric_spells(c, t, 4);
    }
    lv = c.level(Monk);
    if (lv > 5 && c.ext[party::kExtKnight] != 1) {
        const int row = c.ext[party::kExtKnight] == 2 ? 4 : 5;
        for (int L = 6; L <= lv; ++L)
            for (int k = 0; k < 7; ++k) cast(c, 0, k) = static_cast<uint8_t>(cast(c, 0, k) + t.slots_gained(row, L, k));
        krynn_cleric_spells(c, t, 7);
    }
    lv = c.level(Ranger);
    if (lv > 7 && t.lay.ranger_slots) {
        for (int L = 8; L <= lv; ++L)
            for (int k = 0; k < 8; ++k) {
                const int v = t.u8(static_cast<uint16_t>(t.lay.ranger_slots + L * 8 + k + 1));
                if (k < 4) cast(c, 1, k) = static_cast<uint8_t>(cast(c, 1, k) + v);
                else cast(c, 2, k - 4) = static_cast<uint8_t>(cast(c, 2, k - 4) + v);
            }
        for (int s = 1; s < t.lay.spell_count; ++s) {
            const int lvl = t.spell_level(s);
            if (t.spell_class(s) == 1 && lvl >= 1 && lvl <= 8 && cast(c, 1, lvl - 1) > 0) learn(c, s);
        }
    }
    lv = c.level(MagicUser);
    if (lv > 0) {
        const int row = t.xp_row(&c, MagicUser);
        cast(c, 2, 0) = static_cast<uint8_t>(cast(c, 2, 0) + 1);
        for (int L = 2; L <= lv; ++L)
            for (int k = 0; k < 8; ++k) cast(c, 2, k) = static_cast<uint8_t>(cast(c, 2, k) + t.slots_gained(row, L, k));
    }
}
// The Dark Queen of Krynn (its slots routine, ovl 0xB +0x5327, and the
// cleric's +0x514F; queen_facts_4.md 5): its tables' slots in all at the
// level (to 29th) - a cleric's by its god's row, the Wisdom bonus, no 6th
// level under Wisdom 17, no 7th under 18, the lists of the levels it has;
// a knight of the Sword / Rose past 5th its order's row (cleric 1-7) and
// the lists; a paladin past 8th the paladin row's cleric 1-4 and every
// cleric spell of the levels it has; a ranger past 7th its row's druid 1-4
// (columns 1-4) and magic-user 1-4 (5-8), every druid spell of the levels
// it has and the magic-user spells of the levels it has both kinds of
// slots for; a magic-user 1 + its robe's row (1-9), none of 6th under
// Intelligence 12, 7th under 14, 8th under 16, 9th under 18
void queen_slots(party::Character& c, const Tables& t)
{
    clear_slots(c, t);
    int lv = c.level(Cleric);
    if (lv > 0) {
        const int row = t.xp_row(&c, Cleric);
        for (int k = 0; k < 7; ++k) cast(c, 0, k) = static_cast<uint8_t>(t.queen_slots_at(row, lv, k + 1));
        wisdom_spells(c);
        if (c.rec[kWisFull] < 17) cast(c, 0, 5) = 0;
        if (c.rec[kWisFull] < 18) cast(c, 0, 6) = 0;
        krynn_cleric_spells(c, t, 7);
    }
    lv = c.level(Monk);
    if (lv > 5 && c.ext[party::kExtKnight] != 1) {
        const int row = c.ext[party::kExtKnight] == 2 ? 4 : 5;
        for (int k = 0; k < 7; ++k)
            cast(c, 0, k) = static_cast<uint8_t>(cast(c, 0, k) + t.queen_slots_at(row, lv, k + 1));
        krynn_cleric_spells(c, t, 7);
    }
    lv = c.level(Paladin);
    if (lv > 8) {
        for (int k = 0; k < 4; ++k)
            cast(c, 0, k) = static_cast<uint8_t>(cast(c, 0, k) + t.queen_slots_at(10, lv, k + 1));
        for (int s = 1; s < 127 && s < t.lay.spell_count; ++s) {
            const int lvl = t.spell_level(s);
            if (t.spell_class(s) == 0 && lvl >= 1 && lvl <= 9 && cast(c, 0, lvl - 1) > 0) learn(c, s);
        }
    }
    lv = c.level(Ranger);
    if (lv > 7) {
        for (int k = 0; k < 4; ++k) {
            cast(c, 1, k) = static_cast<uint8_t>(cast(c, 1, k) + t.queen_slots_at(6, lv, k + 1));
            cast(c, 2, k) = static_cast<uint8_t>(cast(c, 2, k) + t.queen_slots_at(6, lv, k + 5));
        }
        for (int s = 1; s < 127 && s < t.lay.spell_count; ++s) {
            const int k = t.spell_class(s), lvl = t.spell_level(s);
            if (lvl < 1 || lvl > 9) continue;
            // (a magic-user spell wants both kinds of slots of its level)
            if ((k == 1 || (k == 2 && cast(c, 2, lvl - 1) > 0)) && cast(c, 1, lvl - 1) > 0) learn(c, s);
        }
    }
    lv = c.level(MagicUser);
    if (lv > 0) {
        const int row = t.xp_row(&c, MagicUser);
        cast(c, 2, 0) = static_cast<uint8_t>(cast(c, 2, 0) + 1);
        for (int k = 0; k < 9; ++k)
            cast(c, 2, k) = static_cast<uint8_t>(cast(c, 2, k) + t.queen_slots_at(row, lv, k + 1));
        static const int kNeed[4] = {12, 14, 16, 18};
        for (int k = 0; k < 4; ++k)
            if (c.rec[0x13] < kNeed[k]) cast(c, 2, 5 + k) = 0;
    }
}
} // namespace

void spell_slots(party::Character& c, const Tables& t)
{
    if (t.lay.queen) {
        queen_slots(c, t);
        for (int i = 0; i < c.n_items; ++i)
            if (c.items[i][0x34] && items::passive(c.items[i]) == items::Passive::Wizardry) wizardry(c, t, true);
        return;
    }
    if (t.lay.dkk) {
        dkk_slots(c, t);
        for (int i = 0; i < c.n_items; ++i)
            if (c.items[i][0x34] && items::passive(c.items[i]) == items::Passive::Wizardry) wizardry(c, t, true);
        return;
    }
    if (t.lay.pool) {
        pool_slots(c, t);
        return;
    }
    if (t.lay.dk) {
        dk_slots(c, t);
        for (int i = 0; i < c.n_items; ++i)
            if (c.items[i][0x34] && items::passive(c.items[i]) == items::Passive::Wizardry) wizardry(c, t, true);
        return;
    }
    clear_slots(c, t);
    const int top = t.top_level(), cols = t.slot_levels();
    for (int cls = Cleric; cls <= Monk; ++cls) {
        const int lv = skill_level(c, cls);
        if (lv <= 0) continue;
        switch (cls) {
        case Cleric:
            cast(c, 0, 0) += 1;
            // (Champions: a neutral god's new cleric's 2nd slot, made at its
            // creation - GAME.OVR 0x2237E - stays until the slots are worked
            // out again at its training)
            if (t.lay.krynn && lv == 1 && c.ext[party::kExtGod] > 4) cast(c, 0, 0) += 1;
            for (int L = 2; L <= lv && L <= top; ++L)
                for (int k = 0; k < cols; ++k) cast(c, 0, k) += t.slots_gained(t.xp_row(&c, Cleric), L, k);
            wisdom_spells(c);
            if (t.lay.krynn) {
                krynn_cleric_spells(c, t);
                break;
            }
            // Secret (GAME.OVR 0x3C5DD): no 6th level spells under Wisdom 17,
            // no 7th under 18
            if (t.lay.secret && c.rec[kWisFull] < 17) cast(c, 0, 5) = 0;
            if (t.lay.secret && c.rec[kWisFull] < 18) cast(c, 0, 6) = 0;
            // (Secret's 0x24 is Heal, a cleric spell like the rest)
            for (int s = 1; s < t.lay.spell_count; ++s)
                if (t.spell_class(s) == 0 && t.spell_level(s) >= 1 && t.spell_level(s) <= t.slot_levels() &&
                    cast(c, 0, t.spell_level(s) - 1) > 0 && (s != kAnimateDead || t.lay.secret))
                    learn(c, s);
            break;
        case Paladin:
            if (lv > 8) {
                for (int L = 9; L <= lv && L <= top; ++L)
                    for (int k = 0; k < cols; ++k) cast(c, 0, k) += t.slots_gained(Paladin, L, k);
                for (int s = 1; s < t.lay.spell_count; ++s)
                    if (t.spell_class(s) == 0 && t.spell_level(s) >= 1 && t.spell_level(s) <= t.slot_levels() &&
                        cast(c, 0, t.spell_level(s) - 1) > 0)
                        learn(c, s);
            }
            break;
        case Ranger:
            if (lv > 7) {
                for (int L = 8; L <= lv && L <= top; ++L) {
                    for (int k = 0; k < 3; ++k) cast(c, 1, k) += t.slots_gained(t.xp_row(&c, Ranger), L, k);
                    for (int k = 3; k < 5; ++k) cast(c, 2, k - 3) += t.slots_gained(t.xp_row(&c, Ranger), L, k);
                }
                // the druid spells of the levels they have slots for (Curse's
                // are all 1st level; Secret's ranger leaves its 2nd level
                // ones out until then - the GOG party's ranger)
                // (Secret, GAME.OVR 0x3C083: the magic-users' too, of the levels
                // they have magic-user slots for)
                for (int s = 1; s < t.lay.spell_count; ++s) {
                    const int k = t.spell_class(s), lvl = t.spell_level(s);
                    if (lvl < 1 || lvl > t.slot_levels()) continue;
                    if ((k == 1 || (k == 2 && t.lay.secret)) && cast(c, k, lvl - 1) > 0) learn(c, s);
                }
            }
            break;
        case Monk:
            // Champions' knights of the Sword / Rose past 5th level: cleric
            // slots by their order's row from 6th, the cleric's lists
            // (GAME.OVR 0x3A065)
            if (t.lay.krynn && lv > 5 && c.ext[party::kExtKnight] >= 2) {
                const int row = c.ext[party::kExtKnight] == 2 ? 4 : 5;
                for (int L = 6; L <= lv && L <= top; ++L)
                    for (int k = 0; k < cols; ++k) cast(c, 0, k) += t.slots_gained(row, L, k);
                krynn_cleric_spells(c, t);
            }
            break;
        case MagicUser:
            cast(c, 2, 0) += 1;
            for (int L = 2; L <= lv && L <= top; ++L)
                for (int k = 0; k < cols; ++k) cast(c, 2, k) += t.slots_gained(t.xp_row(&c, MagicUser), L, k);
            // Secret (GAME.OVR 0x3C609): no 6th level spells under Intelligence
            // 12, no 7th under 14
            if (t.lay.secret && c.rec[0x13] < 12) cast(c, 2, 5) = 0;
            if (t.lay.secret && c.rec[0x13] < 14) cast(c, 2, 6) = 0;
            break;
        default:
            break;
        }
    }
    // Rings of Wizardry readied (Curse's item effect 0x81): magic-user levels 1-3 doubled, each
    for (int i = 0; i < c.n_items; ++i)
        if (c.items[i][0x34] && items::passive(c.items[i]) == items::Passive::Wizardry) wizardry(c, t, true);
}

void wizardry(party::Character& c, const Tables& t, bool on)
{
    if (on) {
        // (Secret's ready routine, GAME.OVR 0x28BDD: the 5th level's alone - record 0x14B)
        const int from = t.lay.secret || t.lay.queen ? 4 : 0, to = t.lay.secret || t.lay.queen ? 5 : 3;
        for (int k = from; k < to; ++k) cast(c, 2, k) = static_cast<uint8_t>(cast(c, 2, k) * 2);
        return;
    }
    // Off: the slots as they are without it; magic-user spells past them forgotten (the later first)
    spell_slots(c, t);
    int held[5] = {};
    for (int k = 0; k < 84; ++k) {
        uint8_t& e = c.rec[0x1E + k];
        const int s = e & 0x7F;
        if (!s || t.spell_class(s) != 2) continue;
        const int lvl = t.spell_level(s);
        if (lvl < 1 || lvl > 5) continue;
        if (++held[lvl - 1] > cast(c, 2, lvl - 1)) e = 0;
    }
}

void saving_throws(party::Character& c, const Tables& t)
{
    // Darkness (ovr 0x387B0, its level at 0x392EE): a class's level is the
    // higher of its own and - once the new class has passed the old ones
    // (0x38C8A: the highest level over record 0x139) - its former level
    bool former = false;
    if (t.lay.dk) {
        int top = 0;
        for (int cls = 0; cls < 7; ++cls)
            if (c.level(cls) > top) top = c.level(cls);
        former = top > c.rec[kMultiLevel];
    }
    for (int kind = 0; kind < 5; ++kind) {
        int best = 20;
        for (int cls = 0; cls < kClasses; ++cls) {
            int lv = c.level(cls);
            if (former && c.old_level(cls) > lv) lv = c.old_level(cls);
            if (lv <= 0) continue;
            const int v = t.save(cls, lv, kind);
            if (v < best) best = v;
        }
        // Champions (GAME.OVR 0x3A8E2): after its class loop the former
        // class's check runs with the loop's last index - the knight's - and
        // its former level, so a knight above it (every knight: no former
        // level) takes row 7's entry for that level (level 0: 11 10 10 14
        // 11) when lower. Game Fixes: not (SPEC 12)
        if (t.lay.krynn && !t.lay.dkk && !t.lay.queen && !rules::fixes() && c.level(Monk) > c.old_level(Monk)) {
            const int v = t.save(Monk, c.old_level(Monk), kind);
            if (v < best) best = v;
        }
        c.rec[kSaves + kind] = static_cast<uint8_t>(best);
    }
    // Dwarves and halflings: a poison bonus by their constitution (added to
    // the poison number, as the games do); everyone with Con 19+
    const int con = c.rec[kConFull];
    int add = 0;
    bool girdle = false;                        // (a readied Girdle of the Dwarves, Curse's item effect 0x86: as a dwarf)
    for (int i = 0; i < c.n_items; ++i)
        if (c.items[i][0x34] && items::passive(c.items[i]) == items::Passive::Dwarves) girdle = true;
    // (Secret: the girdle's alone - its dwarves' and halflings' bonus is the
    // Constitution effect, 0x61, kept in the saves the record holds)
    if (((c.race() == 1 || c.race() == 5) && !t.lay.secret) || girdle) {
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
    // (Pool: none here - its dwarves and halflings have an effect for it, 0x5A;
    // the sample dwarf's and halfling's saves are the table's)
    if (!t.lay.pool) c.rec[kSaves] = static_cast<uint8_t>(c.rec[kSaves] + add);
}

// The thief skills' race row. Champions (its GAME.OVR 0x3ABC5) reads its own
// race (0 Silvanesti elf ...) as a row of a table that is Curse's (rows by
// Curse's race: 1 dwarf, 2 elf ...) one row on - a Silvanesti elf gets the
// dwarf's, a kender the half-orc's (thief_race is set to that place); with
// Game Fixes (rules::fixes) each race the row of the Curse race it is
int thief_race_row(const party::Character& c, const Tables& t)
{
    if (!t.lay.krynn) return t.race_row(c.race());
    if (t.lay.queen) return c.ext[party::kExtRace];                 // (the Dark Queen's own table, by its race)
    if (rules::fixes()) return c.race() > 0 ? c.race() - 1 : 0;     // (thief_race is one row on)
    return c.ext[party::kExtRace];
}

void thief_skills(party::Character& c, const Tables& t)
{
    int lv = skill_level(c, Thief);
    if (lv > t.top_level()) lv = t.top_level();
    if ((t.lay.dk || t.lay.dkk || t.lay.queen) && lv > 18) lv = 18;   // (Darkness's table: to 18th level, ovr 0x38A11; Death Knights' too)
    const int dex = c.rec[kDexFull];
    // The first readied Gauntlets of Dexterity (item effect 0x82) or Gloves
    // of Thievery (0x8B; curse_finish_facts.md 5.4)
    int code = 0;
    for (int i = 0; i < c.n_items && !code; ++i) {
        if (!c.items[i][0x34]) continue;
        const items::Passive k = items::passive(c.items[i]);
        if (k == items::Passive::Dexterity) code = 2;
        else if (k == items::Passive::Thievery) code = 11;
    }
    // The gauntlets: below 4th level the skills of 4th, else +10 on each
    // (added last, even to a skill the race made 0); the gloves: skill 1 at
    // 5th level when below it (no bonus), else +5, skill 2 the same at 7th;
    // that bonus (from 0, as coab) goes on to skills 3-8 - listing
    // ovr026:0AEA, behaviour_facts.md items
    bool plus10 = code == 2;
    if (code == 2 && lv < 4) {
        lv = 4;
        plus10 = false;
    }
    int bonus = 0;
    for (int skill = 1; skill <= 8; ++skill) {
        int use = lv;
        if (code == 11 && (skill == 1 || skill == 2)) {
            const int need = skill == 1 ? 5 : 7;
            if (lv < need) {
                use = need;
                bonus = 0;
            } else {
                bonus = 5;
            }
        }
        const int race_adj = t.s8(static_cast<uint16_t>(t.lay.thief_race + thief_race_row(c, t) * 8 + skill));
        const int base = t.u8(static_cast<uint16_t>(t.lay.thief_base + use * 8 + skill));
        int v;
        if (race_adj < 0 && base < -race_adj + bonus) {      // (the original's test: the bonus on the race's side)
            v = 0;
        } else {
            v = bonus + base + race_adj;
            if (skill < 6) v += t.s8(static_cast<uint16_t>(t.lay.thief_dex + dex * 5 + skill));
        }
        if (plus10) v += 10;
        c.rec[kThief + skill - 1] = static_cast<uint8_t>(v);
    }
}

void class_bonuses(party::Character& c, const Tables& t)
{
    int thac0 = 0, hd = c.rec[kHitDice];
    for (int cls = 0; cls < kClasses; ++cls) {
        const int lv = c.level(cls);
        const int v = t.thac0(cls, lv);
        if (v > thac0) thac0 = v;
        if (lv > hd) hd = lv;
    }
    c.rec[kThac0] = static_cast<uint8_t>(thac0);
    c.rec[kHitDice] = static_cast<uint8_t>(hd);
    if (t.lay.pool) {
        // Pool (create_facts.md 2.1): a fighter's attacks set by the level, 3 / 2 over 6
        if (c.level(Fighter) > 0) c.rec[kAttacks] = c.level(Fighter) > 6 ? 3 : 2;
    } else if (c.level(Fighter) >= 7 || c.level(Paladin) >= 7 || c.level(Ranger) >= 8 ||
               (t.lay.krynn && c.level(Monk) >= 7)) {
        // (Champions' knights as fighters - GAME.OVR 0x3A3DE)
        c.rec[kAttacks] = 3;
        // Secret's levels go on: 2 a round past 12th (rangers 14th) - GAME.OVR 0x3C249
        if (t.lay.secret && (c.level(Fighter) > 12 || c.level(Paladin) > 12 || c.level(Ranger) > 14)) c.rec[kAttacks] = 4;
        // (Death Knights, ovl 0xE5 +0x570: fighters, paladins and knights past 12th; no ranger's)
        if (t.lay.dkk && (c.level(Fighter) > 12 || c.level(Paladin) > 12 || c.level(Monk) > 12)) c.rec[kAttacks] = 4;
        // (the Dark Queen, ovl 0xB +0x5947: fighters, paladins and knights past 12th, rangers past 14th)
        if (t.lay.queen && (c.level(Fighter) > 12 || c.level(Paladin) > 12 || c.level(Monk) > 12 || c.level(Ranger) > 14))
            c.rec[kAttacks] = 4;
    }
    // Champions' gods (GAME.OVR 0x3A40A): Kiri-Jolith's clerics, and
    // Reorx's dwarves, +1 to hit
    if (t.lay.krynn && (c.ext[party::kExtGod] == 3 || (c.ext[party::kExtGod] == 6 &&
                                                       (c.ext[party::kExtRace] == 3 || c.ext[party::kExtRace] == 4))))
        ++c.rec[kThac0];
    spell_slots(c, t);
    saving_throws(c, t);
    if (c.level(Thief) > 0) thief_skills(c, t);
    int flags = 0;
    for (int cls = 0; cls < kClasses; ++cls)
        if (c.level(cls) > 0 || (c.old_level(cls) > 0 && c.old_level(cls) < hd))
            flags += t.u8(static_cast<uint16_t>(t.lay.class_flags + cls));
    c.rec[kClassFlags] = static_cast<uint8_t>(flags);
    // A human whose new class has passed the old one: the old classes count
    // too - their THAC0, 3 attacks for an old fighter / paladin past 6th or
    // ranger past 7th level, a former thief's skills (the original's)
    if (c.race() == 7) {
        int k = 0;
        while (k < 7 && c.level(k) == 0) ++k;
        if (c.level(k) > c.rec[kMultiLevel]) {
            for (int cls = 0; cls < kClasses; ++cls) {
                const int old = c.old_level(cls);
                const int v = t.thac0(cls, old);
                if (v > c.rec[kThac0]) c.rec[kThac0] = static_cast<uint8_t>(v);
            }
            if (c.old_level(Fighter) > 6 || c.old_level(Paladin) > 6 || c.old_level(Ranger) > 7) c.rec[kAttacks] = 3;
            if (t.lay.secret && (c.old_level(Fighter) > 12 || c.old_level(Paladin) > 12 || c.old_level(Ranger) > 14))
                c.rec[kAttacks] = 4;            // (Secret's GAME.OVR 0x3C3CE)
            if (c.old_level(Thief) > 0) thief_skills(c, t);
        }
    }
    (void)kStrFull;
    (void)kLevels;
    (void)kOldLevels;
}


int slots(const party::Character& c, int cls, int level)
{
    if (cls < 0 || cls > 2 || level < 1 || level > 9) return 0;
    return cast(const_cast<party::Character&>(c), cls, level - 1);
}

bool in_book(const party::Character& c, int spell)
{
    if (spell >= 1 && spell <= 100) return c.rec[kSpellBook + spell - 1] != 0;
    if (spell > 100 && spell <= 117) return c.ext[party::kExtBook + spell - 101] != 0;   // (Secret's)
    if (spell > 117 && spell <= 126) return c.ext[party::kExtBook2 + spell - 118] != 0;  // (Darkness's)
    return false;
}

void to_book(party::Character& c, int spell)
{
    if (spell >= 1 && spell <= 100) c.rec[kSpellBook + spell - 1] = 1;
    else if (spell > 100 && spell <= 117) c.ext[party::kExtBook + spell - 101] = 1;   // (Secret's)
    else if (spell > 117 && spell <= 126) c.ext[party::kExtBook2 + spell - 118] = 1;  // (Darkness's)
}

int queen_class(int k)
{
    static const int8_t kTo[8] = {0, -1, 2, 3, 4, 5, 6, 1};
    return k >= 0 && k < 8 ? kTo[k] : -1;
}

namespace {
// A class row of the Dark Queen's 17-row tables for the engine's class k
// (0-16: the multi-classes are numbered alike)
int queen_row(int k) { return k < 8 ? queen_class(k) : k; }
} // namespace

bool queen_pack(Tables& t, const Layout& ds, DsReader read, void* ctx)
{
    // The packed copy's parts in order (queen_facts_4.md 4)
    constexpr int kSpells = 138;
    constexpr size_t kThiefFrom = 0x641C, kThiefTo = 0x6560;   // thief skills by level, race, Dexterity
    Layout l{};
    size_t at = 0;
    auto take = [&](uint16_t& field, size_t n) {
        field = static_cast<uint16_t>(at);
        at += n;
    };
    take(l.spells, kSpells * 16);
    take(l.thac0, 8 * 22);
    take(l.class_flags, 8);
    take(l.class_masks, 8);
    take(l.max_hit_dice, 8);
    take(l.q_hp_count, 8);
    take(l.q_hp_sides, 8);
    take(l.thief_base, kThiefTo - kThiefFrom);
    l.thief_race = static_cast<uint16_t>(l.thief_base + (ds.thief_race - kThiefFrom));
    l.thief_dex = static_cast<uint16_t>(l.thief_base + (ds.thief_dex - kThiefFrom));
    take(l.stat_limits, 7 * 16);
    take(l.race_classes, 7 * 15);
    take(l.race_ages, 7 * 32);
    take(l.age_brackets, 7 * 10);
    take(l.class_min, 17 * 6);
    take(l.class_alignments, 34);
    take(l.saves, 8 * 110);
    take(l.q_xp, 11 * 76);
    take(l.q_xp_step, 11 * 4);
    take(l.q_slot_map, 11);
    take(l.q_slots, 8 * 261);
    take(l.cleric_lists, 7 * 10);
    l.cleric_lists = static_cast<uint16_t>(l.cleric_lists - 11);   // (+ level x 10 + i, both from 1)
    take(l.q_caps, 7 * 8);
    take(l.q_con_hp, 26);
    take(l.q_mu_lists, 2 * 90);
    l.lo = 0;
    l.hi = static_cast<uint16_t>(at);
    l.lo2 = l.hi;
    l.hi2 = static_cast<uint16_t>(l.hi + 6);
    l.spell_count = kSpells;
    l.krynn = true;
    l.queen = true;

    uint8_t* b = new (std::nothrow) uint8_t[at];
    if (!b) return false;
    memset(b, 0, at);
    bool ok = true;
    // Spells: its 22-byte rows (class word, level, range, range a level,
    // time, time a level, aim, targets, -, on save, -, save word, effect,
    // when, -, delay, priority, foe, -, picture) into Death Knights' 16
    // (picture, class, level, range, ...)
    {
        uint8_t row[22];
        for (int s = 0; ok && s < kSpells; ++s) {
            ok = read(ctx, static_cast<uint16_t>(ds.spells + s * 22), row, sizeof row);
            uint8_t* o = b + l.spells + s * 16;
            const uint8_t cls = row[0];
            o[0] = row[21];
            // (its class 3: a god's special spell - kept apart, 5: spell_class 4)
            o[1] = cls == 0 ? 0 : cls == 1 ? 1 : cls == 2 ? 3 : cls == 3 ? 5 : 4;
            o[2] = row[2];
            o[3] = row[3];
            o[4] = row[4];
            o[5] = row[5];
            o[6] = row[6];
            o[7] = row[7];
            o[8] = row[8];
            o[9] = row[10];
            o[10] = row[12];
            o[11] = row[14];
            o[12] = row[15];
            o[13] = row[17];
            o[14] = row[18];
            o[15] = row[19];
        }
    }
    // Class-indexed bytes: its order to the engine's
    uint8_t q[7];
    auto by_class = [&](uint16_t from, uint16_t to) {
        ok = ok && read(ctx, from, q, 7);
        for (int k = 0; k < 8; ++k) b[to + k] = queen_class(k) >= 0 ? q[queen_class(k)] : 0;
    };
    by_class(ds.class_flags, l.class_flags);
    by_class(ds.class_masks, l.class_masks);
    by_class(ds.max_hit_dice, l.max_hit_dice);
    by_class(ds.q_hp_count, l.q_hp_count);
    by_class(ds.q_hp_sides, l.q_hp_sides);
    {
        uint8_t row[110];
        for (int k = 0; ok && k < 8; ++k) {
            const int qk = queen_class(k);
            if (qk < 0) continue;
            ok = read(ctx, static_cast<uint16_t>(ds.thac0 + qk * 22), row, 22);
            memcpy(b + l.thac0 + k * 22, row, 22);
            ok = ok && read(ctx, static_cast<uint16_t>(ds.saves + qk * 110), row, 110);
            memcpy(b + l.saves + k * 110, row, 110);
        }
    }
    ok = ok && read(ctx, static_cast<uint16_t>(kThiefFrom), b + l.thief_base, kThiefTo - kThiefFrom);
    ok = ok && read(ctx, ds.stat_limits, b + l.stat_limits, 7 * 16);
    ok = ok && read(ctx, ds.age_brackets, b + l.age_brackets, 7 * 10);
    for (int r = 0; ok && r < 7; ++r) {
        // races' classes: a count, then u16 class numbers (its knight 1 -
        // the engine's 7)
        uint8_t row[29];
        ok = read(ctx, static_cast<uint16_t>(ds.race_classes + r * 29), row, sizeof row);
        uint8_t* o = b + l.race_classes + r * 15;
        int n = row[0] > 14 ? 14 : row[0];
        o[0] = static_cast<uint8_t>(n);
        for (int i = 0; i < n; ++i) {
            const int c = row[1 + i * 2];
            o[1 + i] = static_cast<uint8_t>(c == 1 ? 7 : c == 7 ? 1 : c);
        }
        // ages: 7 classes' entries (u16 base, count, sides)
        uint8_t ages[28];
        ok = ok && read(ctx, static_cast<uint16_t>(ds.race_ages + r * 28), ages, sizeof ages);
        for (int k = 0; k < 8; ++k)
            if (queen_class(k) >= 0) memcpy(b + l.race_ages + r * 32 + k * 4, ages + queen_class(k) * 4, 4);
        uint8_t caps[7];
        ok = ok && read(ctx, static_cast<uint16_t>(ds.q_caps + r * 7), caps, sizeof caps);
        for (int k = 0; k < 8; ++k) b[l.q_caps + r * 8 + k] = queen_class(k) >= 0 ? caps[queen_class(k)] : 0;
    }
    for (int k = 0; ok && k < 17; ++k) {
        const int qr = queen_row(k);
        if (qr >= 0) ok = read(ctx, static_cast<uint16_t>(ds.class_min + qr * 6), b + l.class_min + k * 6, 6);
    }
    // alignments: rows of a count and u16 alignments (0 LG ... 8 CE) - for
    // each class, then again for dwarves - as masks over LG LN NG TN CG CN
    static const uint8_t kList[6] = {0, 1, 3, 4, 6, 7};
    for (int k = 0; ok && k < 34; ++k) {
        const int qr = queen_row(k % 17);
        if (qr < 0) continue;
        uint8_t row[21];
        ok = read(ctx, static_cast<uint16_t>(ds.class_alignments + ((k >= 17 ? 17 : 0) + qr) * 21), row, sizeof row);
        uint8_t m = 0;
        for (int i = 0; i < row[0] && i < 10; ++i)
            for (int j = 0; j < 6; ++j)
                if (row[1 + i * 2] == kList[j] && row[2 + i * 2] == 0) m = static_cast<uint8_t>(m | 1 << j);
        b[l.class_alignments + k] = m;
    }
    ok = ok && read(ctx, ds.q_xp, b + l.q_xp, 11 * 76) && read(ctx, ds.q_xp_step, b + l.q_xp_step, 11 * 4) &&
         read(ctx, ds.q_slot_map, b + l.q_slot_map, 11) && read(ctx, ds.q_slots, b + l.q_slots, 8 * 261) &&
         read(ctx, static_cast<uint16_t>(ds.cleric_lists + 11), b + l.cleric_lists + 11, 70) &&
         read(ctx, ds.q_con_hp, b + l.q_con_hp, 26) &&
         read(ctx, static_cast<uint16_t>(ds.cleric_lists + 11 + 90), b + l.q_mu_lists, 2 * 90);
    if (!ok) {
        delete[] b;
        return false;
    }
    return t.adopt(l, b, at, kList, sizeof kList);
}

} // namespace classes

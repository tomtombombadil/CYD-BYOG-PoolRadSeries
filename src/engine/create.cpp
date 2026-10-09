#include "create.h"

#include <cstring>

namespace create {

namespace {

// Curse record offsets (party.h lists the rest)
constexpr int kStats = 0x10, kThac0 = 0x73, kRace = 0x74, kClass = 0x75, kAge = 0x76, kHpMax = 0x78;
constexpr int kSpellBook = 0x79, kFieldDE = 0xDE, kBaseMove = 0xE4, kHitDice = 0xE5, kMultiLevel = 0xE6;
constexpr int kMoney = 0xFB, kLevels = 0x109, kSex = 0x119, kAlign = 0x11B, kAttacks = 0x11C;
constexpr int kDiceBase = 0x11E, kSidesBase = 0x120, kBaseAc = 0x124, kUsesStr = 0x125, kModId = 0x126;
constexpr int kExp = 0x127, kHpRolled = 0x12C, kCastCount = 0x12D, kIconId = 0x143, kIconSize = 0x144;
constexpr int kIconColours = 0x145, kCures = 0x191, kInCombat = 0x196, kHp = 0x1A4;

// Constitution's hit point adjustment by Con (coab's table)
constexpr int8_t kConHp[26] = {0, 0, 0, -2, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};

// The levels each class starts with: bits cleric, druid, fighter,
// paladin, ranger, magic-user, thief, monk
constexpr uint8_t kStartLevels[17] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
                                      0x05, 0x25, 0x11, 0x21, 0x41, 0x24, 0x44, 0x64, 0x60};

void put16(uint8_t* r, int o, int v)
{
    r[o] = static_cast<uint8_t>(v);
    r[o + 1] = static_cast<uint8_t>(v >> 8);
}

void put32(uint8_t* r, int o, uint32_t v)
{
    for (int i = 0; i < 4; ++i) r[o + i] = static_cast<uint8_t>(v >> (8 * i));
}

void add_affect(party::Character& c, uint8_t type)
{
    if (!type || c.n_affects >= party::kMaxAffects) return;
    uint8_t* a = c.affects[c.n_affects++];
    memset(a, 0, party::kAffectSize);
    a[0] = type;                 // 1-2 minutes: 0 = lasting
    a[3] = 0xFF;                 // its value (the games pass 0xFF for these)
}

void learn(party::Character& c, int spell)
{
    if (spell >= 1 && spell <= 100) c.rec[kSpellBook + spell - 1] = 1;
}

int classes_held(const party::Character& c)
{
    int n = 0;
    for (int k = 0; k < classes::kClasses; ++k)
        if (c.level(k) > 0) ++n;
    return n;
}

// The games' race / class level limits (coab's rules)
bool race_limited(const party::Character& c, int cls, int lvl)
{
    const int str = c.stat(0), intel = c.stat(1);
    switch (c.race()) {
    case 1:                                     // dwarf
        return cls == Fighter && (lvl == 9 || (lvl == 8 && str == 17) || (lvl == 7 && str < 17));
    case 2:                                     // elf
        if (cls == Fighter) return lvl == 7 || (lvl == 6 && str == 17) || (lvl == 5 && str < 17);
        if (cls == MagicUser) return lvl == 11 || (lvl == 9 && intel < 17) || (lvl == 10 && intel == 17);
        return false;
    case 3:                                     // gnome
        return cls == Fighter && (lvl == 6 || (lvl == 5 && str < 18));
    case 4:                                     // half-elf
        if (cls == Cleric) return lvl == 5;
        if (cls == Fighter || cls == Ranger || cls == MagicUser)
            return lvl == 8 || (lvl == 7 && str == 17) || (lvl == 6 && str < 17);
        return false;
    case 5:                                     // halfling
        return cls == Fighter && (lvl == 6 || (lvl == 5 && str == 17) || (lvl == 4 && str < 17));
    default:
        return false;
    }
}

// Hit points rolled for the classes in `mask`: the better of two rolls
// of the class's dice (more dice at level 1 for some); fixed amounts past
// the hit dice
int hp_roll(const party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int mask)
{
    int total = 0;
    for (int k = 0; k < classes::kClasses; ++k) {
        const int lv = c.level(k);
        if (lv <= 0 || !(t.u8(static_cast<uint16_t>(t.lay.class_masks + k)) & mask)) continue;
        if (lv < t.max_hit_dice(k)) {
            const int n = lv > 1 ? 1 : f.hp_count[k];
            const int a = d.roll(f.hp_dice[k], n), b = d.roll(f.hp_dice[k], n);
            total += a > b ? a : b;
        } else if (k == 2 || k == 3) {
            total = 3;
        } else if (k == 4 || k == 0 || k == 6) {
            total = 2;
        } else if (k == 5) {
            total = 1;
        }
    }
    return total;
}

int stat_full(const party::Character& c, int i) { return c.rec[kStats + i * 2 + 1]; }
void set_stat(party::Character& c, int i, int v)
{
    if (v < 0) v = 0;
    c.rec[kStats + i * 2] = static_cast<uint8_t>(v);
    c.rec[kStats + i * 2 + 1] = static_cast<uint8_t>(v);
}

} // namespace

uint32_t Dice::next()
{
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
}

int Dice::random(int n) { return n > 0 ? static_cast<int>(next() % static_cast<uint32_t>(n)) : 0; }

int Dice::roll(int sides, int count)
{
    int t = 0;
    for (int i = 0; i < count; ++i) t += sides > 0 ? random(sides) + 1 : 0;
    return t;
}

int races(int* out, int cap)
{
    static const int kRaces[] = {1, 2, 3, 4, 5, 7};
    int n = 0;
    for (int r : kRaces)
        if (n < cap) out[n++] = r;
    return n;
}

int classes_for(const classes::Tables& t, int race, int* out, int cap)
{
    const uint16_t at = static_cast<uint16_t>(t.lay.race_classes + race * 14);
    int n = t.u8(at);
    if (n > 13) n = 13;
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) out[k++] = t.u8(static_cast<uint16_t>(at + 1 + i));
    return k;
}

int alignments_for(const classes::Tables& t, int cls, int* out, int cap)
{
    const uint16_t at = static_cast<uint16_t>(t.lay.class_alignments + cls * 10);
    int n = t.u8(at);
    if (n > 9) n = 9;
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) out[k++] = t.u8(static_cast<uint16_t>(at + 1 + i));
    return k;
}

int con_hp_adj(const party::Character& c, const classes::Tables& t)
{
    int adj = 0;
    const int con = stat_full(c, 4);
    for (int k = 0; k <= classes::Monk; ++k) {
        const int lv = c.level(k);
        if (lv <= 0 || lv >= t.max_hit_dice(k)) continue;
        adj += con >= 0 && con < 26 ? kConHp[con] : 0;
        const int cls = c.cls();
        if (cls == Fighter || cls == Paladin || cls == Ranger) {
            if (con == 17) adj += 1;
            else if (con == 18) adj += 2;
            else if (con == 19 || con == 20) adj += 3;
            else if (con >= 21 && con <= 23) adj += 4;
            else if (con == 24 || con == 25) adj += 5;
        }
        if (k == classes::Ranger && lv == 1) adj *= 2;
    }
    return adj;
}

void begin(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int race, int sex, int cls,
           int alignment)
{
    c = party::Character{};
    uint8_t* r = c.rec;
    for (int i = 0; i < 6; ++i) r[kIconColours + i] = static_cast<uint8_t>(((f.icon_colours[i] + 8) << 4) + f.icon_colours[i]);
    r[kBaseAc] = 50;
    r[kThac0] = 40;
    r[kInCombat] = 1;
    r[kFieldDE] = 1;
    r[kModId] = static_cast<uint8_t>(d.random(256));
    r[kIconId] = 0x0A;

    r[kRace] = static_cast<uint8_t>(race);
    r[kIconSize] = race == 1 || race == 3 || race == 5 ? 1 : 2;
    switch (race) {
    case 5: add_affect(c, f.con_save); break;
    case 1:
        add_affect(c, f.con_save);
        add_affect(c, f.dwarf_orc);
        add_affect(c, f.giants);
        break;
    case 3:
        add_affect(c, f.con_save);
        add_affect(c, f.gnome_giant);
        add_affect(c, f.giants);
        add_affect(c, f.gnome_extra);
        break;
    case 2: add_affect(c, f.elf_sleep); break;
    case 4: add_affect(c, f.halfelf); break;
    default: break;
    }
    r[kSex] = static_cast<uint8_t>(sex);

    r[kClass] = static_cast<uint8_t>(cls);
    r[kHitDice] = 1;
    const uint8_t lv = cls >= 0 && cls < 17 ? kStartLevels[cls] : 0;
    int n = 0;
    for (int k = 0; k < 8; ++k)
        if (lv & (1 << k)) {
            r[kLevels + k] = 1;
            ++n;
        }
    put32(r, kExp, n >= 3 ? 8333u : n == 2 ? 12500u : 25000u);
    if (r[kLevels + classes::Paladin]) {
        r[kCures] = 1;
        add_affect(c, f.prot_evil);
    }
    if (r[kLevels + classes::Ranger]) add_affect(c, f.ranger_giant);

    r[kAlign] = static_cast<uint8_t>(alignment);

    // Age: the race's dice for the class; a fixed age for multi-classes
    auto age_entry = [&](int e, int* base, int* count, int* sides) {
        const uint16_t at = static_cast<uint16_t>(t.lay.race_ages + race * 28 + e * 4);
        *base = t.u16(at);
        *count = t.u8(static_cast<uint16_t>(at + 2));
        *sides = t.u8(static_cast<uint16_t>(at + 3));
    };
    int base, count, sides;
    if (cls <= Monk) {
        age_entry(cls, &base, &count, &sides);
        put16(r, kAge, d.roll(sides, count) + base);
    } else {
        const int e = cls == FighterThief ? 2 : (cls == FighterMU || cls == FighterMUThief || cls == MUThief) ? 6 : 0;
        age_entry(e, &base, &count, &sides);
        put16(r, kAge, base + count * sides);
    }
    classes::class_bonuses(c, t);
}

void roll(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d)
{
    uint8_t* r = c.rec;
    const int race = c.race(), sex = c.sex() ? 1 : 0, cls = c.cls(), age = c.age();
    // Back to level 1, no spells, no money (a reroll starts again)
    for (int k = 0; k < 8; ++k)
        if (r[kLevels + k]) r[kLevels + k] = 1;
    r[kHitDice] = 1;
    memset(r + kSpellBook, 0, 100);
    memset(r + kMoney, 0, 14);
    const int n = classes_held(c);
    put32(r, kExp, n >= 3 ? 8333u : n == 2 ? 12500u : 25000u);

    for (int i = 0; i < 7; ++i) set_stat(c, i, 0);
    for (int k = 0; k < 6; ++k)
        for (int i = 0; i < 6; ++i) {
            const int v = d.roll(6, 3) + 1;
            if (v > stat_full(c, i)) set_stat(c, i, v);
        }
    // The age's effects (the program's: per stat, for each age bracket the
    // character is past)
    static const int8_t kAgeEffect[6][5] = {
        {1, -1, -2, -1, 0},    // Str
        {0, 1, 0, 1, 0},       // Int
        {-1, 1, 1, 1, 0},      // Wis
        {0, 0, -2, -1, 0},     // Dex
        {0, -1, -1, -1, 0},    // Con
        {0, 0, 0, 0, 0},       // Cha
    };
    const uint16_t lim = static_cast<uint16_t>(t.lay.stat_limits + race * 16);
    for (int i = 0; i < 6; ++i) {
        int v = stat_full(c, i);
        for (int b = 0; b < 5; ++b)
            if (v > 0 && t.u16(static_cast<uint16_t>(t.lay.age_brackets + race * 10 + b * 2)) < age) v += kAgeEffect[i][b];
        int lo, hi;
        if (i == 0) {
            lo = t.u8(static_cast<uint16_t>(lim + sex));
            hi = t.u8(static_cast<uint16_t>(lim + 2 + sex));
        } else {
            lo = t.u8(static_cast<uint16_t>(lim + 4 + i * 2));
            hi = t.u8(static_cast<uint16_t>(lim + 5 + i * 2));
        }
        if (v > hi) v = hi;
        if (v < lo) v = lo;
        const int mn = t.u8(static_cast<uint16_t>(t.lay.class_min + cls * 6 + i));
        if (v < mn) v = mn;
        if (i == 2 && v < 13 && cls >= ClericFighter && cls <= ClericThief) v = 13;   // multi-class clerics
        set_stat(c, i, v);
        if (i == 0 && v == 18 && (c.level(classes::Fighter) || c.level(classes::Ranger) || c.level(classes::Paladin))) {
            int e = d.random(100) + 1;
            const int emax = t.u8(static_cast<uint16_t>(lim + 4 + sex));
            if (e > emax) e = emax;
            set_stat(c, 6, e);
        }
    }

    r[kHp] = r[kHpMax];
    r[kAttacks] = 2;
    r[kDiceBase] = 1;
    r[kSidesBase] = 2;
    r[kUsesStr] = 1;
    r[kBaseMove] = 12;
    memset(r + kCastCount, 0, 15);
    int held = 0;
    for (int k = 0; k < 8; ++k) {
        if (!c.level(k)) continue;
        ++held;
        if (k == classes::Cleric) {
            r[kCastCount] = 1;
            classes::spell_slots(c, t);
            for (int s = 1; s < t.lay.spell_count; ++s)
                if (t.spell_class(s) == 0 && t.spell_level(s) == 1) learn(c, s);
        } else if (k == classes::MagicUser) {
            r[kCastCount + 10] = 1;
            for (uint8_t s : f.mu_first) learn(c, s);
        }
    }
    put16(r, kMoney + 4 * 2, 300);              // 300 platinum

    const int rolled = hp_roll(c, t, f, d, 0xFF);
    int hp = rolled;
    const int adj = con_hp_adj(c, t);
    if (held < 1) held = 1;
    if (adj < 0) hp = hp > -adj + held ? (hp + adj) / held : 1;
    else hp = (hp + adj) / held;
    if (hp < 1) hp = 1;
    r[kHpMax] = static_cast<uint8_t>(hp);
    r[kHp] = r[kHpMax];
    r[kHpRolled] = static_cast<uint8_t>(rolled / held);

    classes::class_bonuses(c, t);
    while (train(c, t, f, d, true)) {
    }
}

void set_name(party::Character& c, const char* name)
{
    size_t n = strlen(name);
    if (n > party::kNameMax) n = party::kNameMax;
    memset(c.rec, 0, 16);
    c.rec[0] = static_cast<uint8_t>(n);
    memcpy(c.rec + 1, name, n);
}

bool train(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, bool silent)
{
    uint8_t* r = c.rec;
    int mask = 0;
    const uint32_t xp = c.exp();
    for (int k = 0; k < classes::kClasses; ++k) {
        const int lv = c.level(k);
        if (lv <= 0 || lv >= 12 || race_limited(c, k, lv)) continue;
        const int32_t need = t.exp_needed(k, lv);
        if (need > 0 && static_cast<uint32_t>(need) <= xp) mask |= t.u8(static_cast<uint16_t>(t.lay.class_masks + k));
    }
    if (!mask) return false;
    int held = 0;
    for (int k = 0; k < classes::kClasses; ++k) {
        if (c.level(k) <= 0) continue;
        ++held;
        if (t.u8(static_cast<uint16_t>(t.lay.class_masks + k)) & mask) ++r[kLevels + k];
    }
    classes::class_bonuses(c, t);
    if (silent) {
        switch (c.level(classes::MagicUser)) {
        case 2: learn(c, f.mu_level2); break;
        case 3:
            learn(c, f.mu_level3[0]);
            learn(c, f.mu_level3[1]);
            break;
        case 4: learn(c, f.mu_level4); break;
        case 5: learn(c, f.mu_level5); break;
        default: break;
        }
    }
    if (r[kHitDice] <= r[kMultiLevel]) return true;
    const int gain = hp_roll(c, t, f, d, mask);
    if (held < 1) held = 1;
    int inc = gain / held;
    if (inc == 0) inc = 1;
    r[kHpRolled] = static_cast<uint8_t>(r[kHpRolled] + inc);
    int up = (gain + con_hp_adj(c, t)) / held;
    if (up < 1) up = 1;
    const int lost = r[kHpMax] - r[kHp];
    r[kHpMax] = static_cast<uint8_t>(r[kHpMax] + up);
    r[kHp] = static_cast<uint8_t>(r[kHpMax] - lost);
    return true;
}

} // namespace create

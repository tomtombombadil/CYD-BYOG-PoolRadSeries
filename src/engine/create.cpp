#include "create.h"

#include "rules.h"

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
constexpr int kOldLevels = 0x111, kListAt = 0x1E, kListSize = 84;   // former levels, the memorized spells

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

void learn(party::Character& c, int spell) { classes::to_book(c, spell); }

// The experience a new character starts with: 25,000 for one class, 12,500
// for two, 8,333 for three - and for the magic-user / thief, as the
// original gives it (its createPlayer: 8,333 for C/F/MU, F/MU/T and MU/T)
uint32_t start_exp(int cls, bool secret = false)
{
    // (Secret's: 200,000, 100,000, 66,667 - secret_facts_2.md 21)
    if (cls < ClericFighter) return secret ? 200000u : 25000u;
    if (cls == ClericFighterMU || cls == FighterMUThief || cls == MUThief) return secret ? 66667u : 8333u;
    return secret ? 100000u : 12500u;
}

// The games' race / class level limits for training (the original's
// train_player: a half-elf magic-user's by Intelligence, as an elf's)
bool race_limited(const party::Character& c, int cls, int lvl, bool pool = false, bool secret = false, bool dk = false)
{
    const int str = c.stat(0), intel = c.stat(1);
    if (pool && cls == MagicUser) return false;         // (Pool's train_player: none for magic-users)
    // Darkness stops gnome clerics at 7 (and half-elves' at 5 as the rest;
    // no dwarf / elf cleric limit - darkness_facts.md 36)
    if (dk && cls == Cleric && c.race() == 3 && lvl >= 7) return true;
    // Secret's training also stops demihuman clerics (secret_facts_2.md 18):
    // dwarves at 8, elves and gnomes at 7 (half-elves at 5, as Curse's)
    if (secret && !dk && cls == Cleric) {
        const int r = c.race();
        if ((r == 1 && lvl >= 8) || ((r == 2 || r == 3) && lvl >= 7)) return true;
    }
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
        if (cls == Fighter || cls == Ranger) return lvl == 8 || (lvl == 7 && str == 17) || (lvl == 6 && str < 17);
        if (cls == MagicUser) return lvl == 8 || (lvl == 7 && intel == 17) || (lvl == 6 && intel < 17);
        return false;
    case 5:                                     // halfling
        return cls == Fighter && (lvl == 6 || (lvl == 5 && str == 17) || (lvl == 4 && str < 17));
    default:
        return false;
    }
}

// Restoration's own limits (the original's, a routine of its own): fewer
// than training's - no dwarf fighter 9, no elf magic-user, half-elves'
// clerics and fighters only
bool restore_limited(const party::Character& c, int cls, int lvl)
{
    const int str = c.stat(0);
    switch (c.race()) {
    case 1: return cls == Fighter && ((lvl == 8 && str == 17) || (lvl == 7 && str < 17));
    case 2: return cls == Fighter && (lvl == 7 || (lvl == 6 && str == 17) || (lvl == 5 && str < 17));
    case 3: return cls == Fighter && (lvl == 6 || (lvl == 5 && str < 18));
    case 4:
        if (cls == Cleric) return lvl == 5;
        return cls == Fighter && (lvl == 8 || (lvl == 7 && str == 17) || (lvl == 6 && str < 17));
    case 5: return cls == Fighter && (lvl == 6 || (lvl == 5 && str == 17) || (lvl == 4 && str < 17));
    default: return false;
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
        if (t.lay.pool) {
            // Pool (create_facts.md 1.12): one roll of the class's dice every
            // level; at level 1 a roll under 2/3 of the die is raised to it
            int v = d.roll(f.hp_dice[k], f.hp_count[k]);
            const int least = f.hp_dice[k] * 2 / 3;
            if (lv == 1 && v < least) v = least;
            total += v;
        } else if (lv < t.max_hit_dice(k)) {
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

// Secret's hit points (GAME.OVR 0x21DDC - creation and training): for each
// class in `mask` below its last hit die the better of two rolls of its
// dice (level 1: DS 0x9B0's count) and its Constitution figure (0x21A99:
// the table by the Constitution in use, fighters / paladins / rangers more
// from 17 - by the class being rolled) times that count; past the hit dice
// 3 / 2 / 1 (the last such class's, as Curse's). The rolls and the
// Constitution are each divided by the classes held (the rolls at least
// 1); the Constitution's byte is divided unsigned - a multi-class's
// penalty comes out a large gain (with the rules fixes: signed)
void secret_hp(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int mask, int held)
{
    uint8_t* r = c.rec;
    int rolled = 0, con = 0;
    const int cv = stat_full(c, 4);
    for (int k = 0; k < 7; ++k) {
        const int lv = c.level(k);
        if (lv <= 0 || !(t.u8(static_cast<uint16_t>(t.lay.class_masks + k)) & mask)) continue;
        if (lv < t.max_hit_dice(k)) {
            const int n = lv > 1 ? 1 : f.hp_count[k];
            const int a = d.roll(f.hp_dice[k], n), b = d.roll(f.hp_dice[k], n);
            rolled += a > b ? a : b;
            int cb = cv >= 0 && cv < 26 ? kConHp[cv] : 0;
            if (k == 2 || k == 3 || k == 4)
                cb += cv == 17 ? 1 : cv == 18 ? 2 : cv == 19 || cv == 20 ? 3 : cv >= 21 && cv <= 23 ? 4 : cv >= 24 && cv <= 25 ? 5 : 0;
            con += cb * n;
        } else if (k == 2 || k == 3) {
            rolled = 3;
        } else if (k == 4 || k == 0 || k == 6) {
            rolled = 2;
        } else if (k == 5) {
            rolled = 1;
        }
    }
    if (held < 1) held = 1;
    int inc = (rolled & 0xFF) / held;
    if (inc < 1) inc = 1;
    r[kHpRolled] = static_cast<uint8_t>(r[kHpRolled] + inc);
    int cp = rules::fixes() ? static_cast<int8_t>(con & 0xFF) / held : (con & 0xFF) / held;
    // (the original's gain can be below 1 - Constitution 3: none or less;
    // AD&D's at least 1 a level with the rules fixes)
    if (rules::fixes() && inc + cp < 1) cp = 1 - inc;
    const int lost = (r[kHpMax] - r[kHp]) & 0xFF;
    r[kHpMax] = static_cast<uint8_t>(r[kHpMax] + inc + cp);
    r[kHp] = static_cast<uint8_t>(r[kHpMax] - lost);
}
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

#ifdef CYD_TEST_HOOKS
int (*g_die_hook)(int n) = nullptr;
#endif

int Dice::random(int n)
{
#ifdef CYD_TEST_HOOKS
    if (g_die_hook && n > 0) {
        const int v = g_die_hook(n);
        if (v >= 0) {
            next();                     // (the sequence moves on as usual)
            return v < n ? v : n - 1;
        }
    }
#endif
    return n > 0 ? static_cast<int>(next() % static_cast<uint32_t>(n)) : 0;
}

int Dice::roll(int sides, int count)
{
    int t = 0;
    for (int i = 0; i < count; ++i) t += sides > 0 ? random(sides) + 1 : 0;
    return t;
}

int races(int* out, int cap, bool secret)
{
    // (Secret lists its own order: elf, half-elf, dwarf, gnome, halfling, human)
    static const int kRaces[] = {1, 2, 3, 4, 5, 7};
    static const int kSecretRaces[] = {2, 4, 1, 3, 5, 7};
    int n = 0;
    for (int r : secret ? kSecretRaces : kRaces)
        if (n < cap) out[n++] = r;
    return n;
}

int classes_for(const classes::Tables& t, int race, int* out, int cap)
{
    const int stride = t.lay.pool ? 12 : 14;
    const uint16_t at = static_cast<uint16_t>(t.lay.race_classes + t.race_row(race) * stride);
    int n = t.u8(at);
    if (n > stride - 1) n = stride - 1;
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
    if (t.lay.pool) {
        // Pool (create_facts.md 2.5): the table's figure per class held; a
        // single-class fighter +1 over 16 and +1 more over 17
        for (int k = 0; k <= classes::Monk; ++k)
            if (c.level(k) > 0) adj += con >= 0 && con < 26 ? kConHp[con] : 0;
        if (c.cls() == Fighter) adj += (con > 16) + (con > 17);
        return adj;
    }
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
    r[0x13F] = r[0x140] = t.lay.pool ? 1 : 0;      // (Pool: the portrait's head and body, from 1)
    if (race == 1 || race == 5) add_affect(c, f.poison_save);     // (Pool's dwarves and halflings)
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
        if (!t.lay.secret) add_affect(c, f.giants);     // (Secret's gnomes: 0x30, 0x07, 0x61 only)
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
    for (int k = 0; k < 8; ++k)
        if (lv & (1 << k)) r[kLevels + k] = 1;
    put32(r, kExp, t.lay.pool ? 0 : start_exp(cls, t.lay.secret));
    if (r[kLevels + classes::Paladin]) {
        r[kCures] = 1;
        add_affect(c, f.prot_evil);
    }
    if (r[kLevels + classes::Ranger]) add_affect(c, f.ranger_giant);

    r[kAlign] = static_cast<uint8_t>(alignment);

    // Age: the race's dice for the class; a fixed age for multi-classes
    auto age_entry = [&](int e, int* base, int* count, int* sides) {
        const uint16_t at = static_cast<uint16_t>(t.lay.race_ages + t.race_row(race) * 28 + e * 4);
        *base = t.u16(at);
        *count = t.u8(static_cast<uint16_t>(at + 2));
        *sides = t.u8(static_cast<uint16_t>(at + 3));
    };
    int base, count, sides;
    if (cls <= Monk) {
        age_entry(cls, &base, &count, &sides);
        put16(r, kAge, d.roll(sides, count) + base);
    } else {
        // (the entry: Curse's thief one for the magic-users, Pool's and Secret's
        // magic-user one - Secret's GAME.OVR 0x1E8F5: DS 0x4E59 = entry 5)
        const int mu = t.lay.pool || t.lay.secret ? 5 : 6;
        const int e = cls == FighterThief ? 2 : (cls == FighterMU || cls == FighterMUThief || cls == MUThief) ? mu : 0;
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
    const bool pool = t.lay.pool;
    put32(r, kExp, pool ? 0 : start_exp(cls, t.lay.secret));

    // Secret adds its races' adjustments to each roll (GAME.OVR 0x1EA28):
    // dwarves Con +1 Cha -1, elves Dex +1 Con -1, halflings Str -1 Dex +1
    auto race_adj = [&](int i) {
        if (!t.lay.secret) return 0;
        switch (race) {
        case 1: return i == 4 ? 1 : i == 5 ? -1 : 0;
        case 2: return i == 3 ? 1 : i == 4 ? -1 : 0;
        case 5: return i == 0 ? -1 : i == 3 ? 1 : 0;
        default: return 0;
        }
    };
    for (int i = 0; i < 7; ++i) set_stat(c, i, 0);
    for (int k = 0; k < 6; ++k)
        for (int i = 0; i < 6; ++i) {
            const int v = d.roll(6, 3) + (pool ? 0 : 1) + race_adj(i);     // (Pool: no +1)
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
    const uint16_t lim = static_cast<uint16_t>(t.lay.stat_limits + t.race_row(race) * 16);
    for (int i = 0; i < 6; ++i) {
        int v = stat_full(c, i);
        for (int b = 0; b < 5; ++b) {
            const bool past = t.u16(static_cast<uint16_t>(t.lay.age_brackets + t.race_row(race) * 10 + b * 2)) < age;
            // Secret's Wisdom: -1 while NOT past the first bracket - the young
            // (GAME.OVR 0x1EEB9: the decrement skipped when the bracket < age)
            if (t.lay.secret && i == 2 && b == 0) {
                if (!past) --v;
                continue;
            }
            if (v > 0 && past) v += kAgeEffect[i][b];
        }
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
        if (!pool && i == 2 && v < 13 && cls >= ClericFighter && cls <= ClericThief) v = 13;   // multi-class clerics (Curse)
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
        } else if (k == classes::Thief && t.lay.secret) {
            // Secret's thieves start with 2d6 gold (GAME.OVR 0x1F589, record
            // 0x109; DOSBox: a new human thief with GOLD 7)
            put16(r, kMoney + 3 * 2, d.roll(6, 2));
        }
    }
    if (pool) {
        // Pool: gold only, 10 x the classes' money dice (a byte), shared
        // by the classes held (create_facts.md 1.11)
        int sum = 0, n = 0;
        for (int k = 0; k < 8; ++k)
            if (c.level(k)) {
                sum += d.roll(t.u8(static_cast<uint16_t>(t.lay.money_dice + 2 * k + 1)),
                              t.u8(static_cast<uint16_t>(t.lay.money_dice + 2 * k)));
                ++n;
            }
        put16(r, kMoney + 3 * 2, n ? (sum & 0xFF) * 10 / n : 0);
    } else if (!t.lay.secret) {
        put16(r, kMoney + 4 * 2, 300);          // 300 platinum (Secret's start with none)
    }

    if (t.lay.secret) {
        // Secret's creation: its hit point routine on nothing (GAME.OVR 0x1F5B4)
        r[kHpMax] = r[kHp] = r[kHpRolled] = 0;
        secret_hp(c, t, f, d, 0xFF, held);
        classes::class_bonuses(c, t);
        while (train(c, t, f, d, true)) {
        }
        return;
    }
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
    if (!pool)
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

int trainable(const party::Character& c, const classes::Tables& t)
{
    int mask = 0;
    const uint32_t xp = c.exp();
    for (int k = 0; k < classes::kClasses; ++k) {
        const int lv = c.level(k);
        if (lv <= 0 || lv >= t.top_level() || race_limited(c, k, lv, t.lay.pool, t.lay.secret, t.lay.dk)) continue;
        const int32_t need = t.exp_needed(k, lv);
        if (need > 0 && static_cast<uint32_t>(need) <= xp) mask |= t.u8(static_cast<uint16_t>(t.lay.class_masks + k));
    }
    return mask;
}

int train_pick(const party::Character& c, const classes::Tables& t, uint32_t* exp_after)
{
    // The experience figure to go on from `lvl` (none past the table)
    auto need_at = [&](int k, int lvl) -> int32_t { return t.exp_needed(k, lvl); };
    const uint32_t xp = c.exp();
    int ready = 0, last = 0;
    int32_t keep = 0;
    for (int k = 0; k < classes::kClasses; ++k) {
        const int lv = c.level(k);
        if (lv <= 0) continue;
        last = lv;
        if (race_limited(c, k, lv, t.lay.pool, t.lay.secret, t.lay.dk)) continue;
        const int32_t need = need_at(k, lv);
        if (need <= 0 || static_cast<uint32_t>(need) > xp) continue;
        ready |= t.u8(static_cast<uint16_t>(t.lay.class_masks + k));
        const int32_t after = need_at(k, lv + 1);
        if (after > 0 && xp >= static_cast<uint32_t>(after) && after > keep) keep = after - 1;
    }
    if (t.lay.pool) {
        // Pool (create_facts.md 3): every ready class trains, a level each
        *exp_after = keep > 0 ? static_cast<uint32_t>(keep) : xp;
        return ready;
    }
    int pick = -1;
    int32_t most = 0;
    for (int k = 0; k < classes::kClasses; ++k)
        if (t.u8(static_cast<uint16_t>(t.lay.class_masks + k)) & ready) {
            const int32_t need = need_at(k, last);
            if (need > most) {
                most = need;
                pick = k;
            }
        }
    if (pick >= 0) {
        ready = t.u8(static_cast<uint16_t>(t.lay.class_masks + pick));
        const int32_t after = need_at(pick, last + 1);
        if (after > 0 && xp >= static_cast<uint32_t>(after) && after > keep) keep = after - 1;
    }
    *exp_after = keep > 0 ? static_cast<uint32_t>(keep) : xp;
    return ready;
}

void train_classes(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, int mask, bool silent)
{
    uint8_t* r = c.rec;
    int held = 0;
    for (int k = 0; k < classes::kClasses; ++k) {
        if (c.level(k) <= 0) continue;
        ++held;
        if (t.u8(static_cast<uint16_t>(t.lay.class_masks + k)) & mask) {
            ++r[kLevels + k];
            // A level lost (record 0xE7, its hit points 0xE8) is made up by
            // one trained: its share of the lost hit points no longer owed
            // (the original's train_player)
            if (r[0xE7] > 0) {
                r[0xE8] = static_cast<uint8_t>(r[0xE8] - r[0xE8] / r[0xE7]);
                --r[0xE7];
            }
        }
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
        // Secret's: whole lists at levels 6-9 (GAME.OVR 0x22921)
        const int lv = c.level(classes::MagicUser) - f.mu_by_level_from;
        if (f.mu_by_level && lv >= 0 && lv < f.mu_by_level_n)
            for (uint8_t s : f.mu_by_level[lv])
                if (s) learn(c, s);
    }
    if (r[kHitDice] <= r[kMultiLevel]) return;
    if (t.lay.secret) {
        secret_hp(c, t, f, d, mask, held);
        return;
    }
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
}

bool restore(party::Character& c, const classes::Tables& t)
{
    uint8_t* r = c.rec;
    const int lost = r[0xE7];
    if (lost <= 0) return false;
    const int hp = r[0xE8] / lost;
    r[kHpMax] = static_cast<uint8_t>(r[kHpMax] + hp);
    r[kHp] = static_cast<uint8_t>(r[kHp] + hp);
    r[kHpRolled] = static_cast<uint8_t>(r[kHpRolled] + hp);
    r[0xE8] = static_cast<uint8_t>(r[0xE8] - hp);
    --r[0xE7];
    int pick = -1, lim_level = 13;
    int32_t lim_exp = 10000000, need_pick = 0;
    for (int k = 0; k <= classes::Monk; ++k) {
        const int lv = c.level(k);
        if (lv <= 0) continue;
        const int32_t need = t.exp_needed(k, lv);
        if (lv <= lim_level && need > 0 && need < lim_exp && !restore_limited(c, k, lv)) {
            pick = k;
            lim_level = lv;
            lim_exp = need;
            need_pick = need;
        }
    }
    if (pick >= 0) {
        ++r[kLevels + pick];
        if (c.exp() < static_cast<uint32_t>(need_pick))
            for (int i = 0; i < 4; ++i) r[kExp + i] = static_cast<uint8_t>(need_pick >> (8 * i));
        classes::class_bonuses(c, t);
    }
    return true;
}

bool train(party::Character& c, const classes::Tables& t, const Facts& f, Dice& d, bool silent)
{
    const int mask = trainable(c, t);
    if (!mask) return false;
    train_classes(c, t, f, d, mask, silent);
    return true;
}

// ---- Human Change
namespace {
int present_class(const party::Character& c)
{
    for (int k = 0; k <= classes::Monk; ++k)
        if (c.level(k) > 0) return k;
    return -1;
}
} // namespace

bool can_change(const party::Character& c)
{
    if (c.race() != 7) return false;                       // humans only
    for (int k = 0; k <= classes::Monk; ++k)
        if (c.old_level(k) > 0) return false;
    return present_class(c) >= 0;
}

int change_classes(const party::Character& c, const classes::Tables& t, int* out, int cap)
{
    if (!can_change(c)) return 0;
    const int now = present_class(c);
    for (int i = 0; i < 6; ++i)
        if (t.u8(static_cast<uint16_t>(t.lay.class_min + now * 6 + i)) >= 9 && c.rec[kStats + i * 2] < 15) return 0;
    int list[17];
    const int nl = classes_for(t, c.race(), list, 17);
    int n = 0;
    for (int j = 0; j < nl && n < cap; ++j) {
        const int k = list[j];
        if (k == now || k > classes::Monk) continue;
        bool ok = true;
        for (int i = 0; i < 6 && ok; ++i)
            if (t.u8(static_cast<uint16_t>(t.lay.class_min + k * 6 + i)) >= 9 && c.rec[kStats + i * 2] < 17) ok = false;
        int al[9];
        const int na = alignments_for(t, k, al, 9);
        bool align = false;
        for (int a = 0; a < na; ++a) align = align || al[a] == c.alignment();
        if (ok && align) out[n++] = k;
    }
    return n;
}

void pool_name(const uint8_t* pool, char* out, size_t cap)
{
    if (!cap) return;
    size_t n = pool[0] > 15 ? 15 : pool[0];
    if (n > cap - 1) n = cap - 1;
    memcpy(out, pool + 1, n);
    out[n] = 0;
}

bool pool_is_pc(const uint8_t* pool) { return pool[0x84] <= 0x7F; }

bool pool_effect_kept(uint8_t type)
{
    static const uint8_t kKept[] = {0x12, 0x1A, 0x2F, 0x30, 0x61, 0x6B, 0x7C};
    for (uint8_t k : kKept)
        if (k == type) return true;
    return false;
}

void from_pool(const uint8_t* p, party::Character& c, const classes::Tables& t)
{
    c = party::Character{};
    uint8_t* r = c.rec;
    // Byte runs copied as they are: Pool offset, Curse offset, length
    static const uint16_t kRuns[][3] = {
        {0x00, 0x00, 16},   {0x2D, 0x73, 1},   {0x2E, 0x74, 1},   {0x2F, 0x75, 1},   {0x30, 0x76, 2},
        {0x32, 0x78, 1},    {0x33, 0x79, 56},  {0x6B, 0xDD, 1},   {0x6C, 0xDE, 1},   {0x6D, 0xDF, 5},
        {0x72, 0xE4, 1},    {0x73, 0xE5, 1},   {0x73, 0xE6, 1},   {0x74, 0xE7, 1},   {0x75, 0xE8, 1},
        {0x76, 0xE9, 1},    {0x77, 0xEA, 8},   {0x83, 0xF6, 1},   {0x84, 0xF7, 1},   {0x85, 0xF8, 1},
        {0x86, 0xF9, 2},    {0x96, 0x109, 8},  {0x9E, 0x119, 1},  {0x9F, 0x11A, 1},  {0xA0, 0x11B, 1},
        {0xA1, 0x11C, 8},   {0xA9, 0x124, 1},  {0xAA, 0x125, 1},  {0xAB, 0x126, 1},  {0xAC, 0x127, 4},
        {0xB0, 0x12B, 1},   {0xB1, 0x12C, 1},  {0xB2, 0x12D, 3},  {0xB5, 0x137, 3},  {0xB8, 0x13C, 2},
        {0xBA, 0x13E, 3},   {0xBD, 0x141, 2},  {0xC0, 0x144, 7},  {0x100, 0x185, 1}, {0x101, 0x186, 1},
        {0x102, 0x187, 2},  {0x10C, 0x195, 1}, {0x10D, 0x196, 1}, {0x10E, 0x197, 1}, {0x110, 0x199, 1},
        {0x111, 0x19A, 2},  {0x113, 0x19C, 8}, {0x11B, 0x1A4, 1}, {0x11C, 0x1A5, 1},
    };
    for (const auto& run : kRuns) memcpy(r + run[1], p + run[0], run[2]);
    if (r[0] > 15) r[0] = 15;
    // The stats, held to the race's and sex's limits
    const int race = r[kRace], sex = r[kSex] ? 1 : 0;
    const uint16_t lim = static_cast<uint16_t>(t.lay.stat_limits + t.race_row(race) * 16);
    for (int i = 0; i < 6; ++i) {
        int v = p[0x10 + i], lo, hi;
        if (i == 0) {
            lo = t.u8(static_cast<uint16_t>(lim + sex));
            hi = t.u8(static_cast<uint16_t>(lim + 2 + sex));
        } else {
            lo = t.u8(static_cast<uint16_t>(lim + 4 + i * 2));
            hi = t.u8(static_cast<uint16_t>(lim + 5 + i * 2));
        }
        if (hi > 0 && v > hi) v = hi;
        if (v < lo) v = lo;
        set_stat(c, i, v);
    }
    int e = p[0x16];
    const int emax = t.u8(static_cast<uint16_t>(lim + 4 + sex));
    if (e > emax) e = emax;
    set_stat(c, 6, e);
    r[kSpellBook + 0x23] = 0;                   // no Animate Dead (spell 0x24) in Curse's book
    memset(r + kMoney, 0, 14);
    put16(r, kMoney + 4 * 2, 300);              // 300 platinum, whatever they had
    r[kIconId] = 0x0A;                          // (a slot when they join)
    classes::class_bonuses(c, t);
}

void change_class(party::Character& c, const classes::Tables& t, const Facts& f, int cls)
{
    const int now = present_class(c);
    if (now < 0 || cls < 0 || cls > classes::Monk) return;
    uint8_t* r = c.rec;
    put32(r, kExp, 0);
    r[kAttacks] = 2;
    r[kOldLevels + now] = r[kLevels + now];
    r[kMultiLevel] = r[kHitDice];
    r[kHitDice] = 1;
    r[kLevels + now] = 0;
    r[kLevels + cls] = 1;
    r[kClass] = static_cast<uint8_t>(cls);
    memset(r + kCastCount, 0, 15);
    if (cls == classes::Cleric) r[kCastCount] = 1;
    if (cls == classes::MagicUser) {
        r[kCastCount + 10] = 1;
        for (uint8_t s : f.mu_change) learn(c, s);
    }
    memset(r + kListAt, 0, kListSize);
    classes::class_bonuses(c, t);
}

// ---- Modify Character
namespace {
// A class's Constitution bonus to each hit die (the fighters' extra for 17 and up)
int class_con_bonus(int con, int cls)
{
    int b = con >= 0 && con < 26 ? kConHp[con] : 0;
    if (cls == Fighter || cls == Paladin || cls == Ranger) {
        if (con == 17) b += 1;
        else if (con == 18) b += 2;
        else if (con == 19 || con == 20) b += 3;
        else if (con >= 21 && con <= 23) b += 4;
        else if (con >= 24) b += 5;
    }
    return b;
}
} // namespace

bool can_modify(const party::Character& c, bool pool, bool secret)
{
    const uint32_t e = c.exp();
    if (pool) return e == 0;
    // (Secret's: its own starting experience - GAME.OVR 0x1FBF0)
    if (secret) return (e == 0 || e == 66667 || e == 100000 || e == 200000) && c.rec[kMultiLevel] == 0;
    return (e == 0 || e == 8333 || e == 12500 || e == 25000) && c.rec[kMultiLevel] == 0;
}

// Secret's Modify bounds (GAME.OVR 0x21B18 / 0x21BF1): the least - the
// levels (rangers + 1) and each class's Constitution figure (0x21A99) over
// the classes; the most - each class below its last hit die its levels x
// (its die + Constitution: 3 -2, 4-6 -1, 15 +1, 16 +2, 17-19 +2, fighters /
// paladins / rangers Con - 14 - 0x1F86B), past it a fixed figure that
// replaces the sum (as its code does), over the classes; bytes
static int secret_con_fig(int con, int k)
{
    int v = con >= 0 && con < 26 ? kConHp[con] : 0;
    if (k == 2 || k == 3 || k == 4)
        v += con == 17 ? 1 : con == 18 ? 2 : con == 19 || con == 20 ? 3 : con >= 21 && con <= 23 ? 4 : con >= 24 && con <= 25 ? 5 : 0;
    return v;
}
static int secret_hp_least(const party::Character& c)
{
    int sum = 0, n = 0, con = 0;
    for (int k = 0; k < 7; ++k) {
        const int lv = c.level(k);
        if (lv <= 0) continue;
        sum += lv + (k == 4);
        ++n;
        con += secret_con_fig(stat_full(c, 4), k);
    }
    if (!n) return 1;
    sum &= 0xFF;
    con = static_cast<int8_t>(con & 0xFF);
    if (con < 0 && sum <= n - con) return 1;
    return static_cast<uint8_t>((sum + con) / n);
}
static int secret_hp_most(const party::Character& c, const classes::Tables& t)
{
    const int con = stat_full(c, 4);
    int most = 0, n = 0;
    for (int k = 0; k < 7; ++k) {
        const int lv = c.level(k);
        if (lv <= 0) continue;
        ++n;
        int b = con == 3 ? -2 : con <= 6 ? -1 : con <= 14 ? 0 : con == 15 ? 1 : con == 16 ? 2 : 0;
        if (con >= 17 && con <= 19) b = k == 2 || k == 3 || k == 4 ? con - 14 : 2;
        static const int kDie[7] = {8, 8, 10, 10, 8, 4, 6};
        if (lv < t.max_hit_dice(k)) {
            most += (k == 4 ? lv + 1 : lv) * (b + kDie[k]);
        } else {
            const int x = lv - t.max_hit_dice(k) + 1;
            static const int kPast[7] = {0x48, 0x70, 0x58, 0x58, 0x58, 0x2C, 0x3C};
            most = k == 1 ? kPast[1] : k == 5 ? x + kPast[5] : 2 * x + kPast[k];
        }
        most &= 0xFF;
    }
    return n ? most / n : 1;
}

int hp_least(const party::Character& c, const classes::Tables& t, const Facts& f)
{
    if (t.lay.secret) return secret_hp_least(c);
    int total = 0, n = 0;
    for (int k = 0; k <= classes::Monk; ++k) {
        const int lv = c.level(k);
        if (lv <= 0) continue;
        total += lv + f.hp_count[k] - 1;
        ++n;
    }
    if (!n) return 1;
    const int adj = con_hp_adj(c, t);
    int v;
    if (adj < 0) v = total > -adj + n ? (total + adj) / n : 1;
    else v = (total + adj) / n;
    return v < 1 ? 1 : v;
}

int hp_most(const party::Character& c, const classes::Tables& t, const Facts& f)
{
    if (t.lay.secret) return secret_hp_most(c, t);
    int total = 0, n = 0;
    const int con = stat_full(c, 4);
    if (t.lay.pool) {
        // Pool (create_facts.md 2.6): every die at its highest (rangers and
        // monks a die more), then the Constitution adjustment as at creation
        for (int k = 0; k <= classes::Monk; ++k) {
            const int lv = c.level(k);
            if (lv <= 0) continue;
            ++n;
            total += f.hp_dice[k] * (lv + f.hp_count[k] - 1);
        }
        if (!n) return 1;
        const int adj = con_hp_adj(c, t);
        const int v = adj < 0 ? (total > -adj + n ? (total + adj) / n : 1) : (total + adj) / n;
        return v < 1 ? 1 : v > 255 ? 255 : v;
    }
    for (int k = 0; k <= classes::Monk; ++k) {
        int lv = c.level(k);
        if (lv <= 0) continue;
        ++n;
        if (lv >= t.max_hit_dice(k)) lv = t.max_hit_dice(k) - 1;     // (no newly made character gets there)
        total += (class_con_bonus(con, k) + f.hp_dice[k]) * (lv + f.hp_count[k] - 1);
    }
    if (!n) return 1;
    const int v = total / n;
    return v < 1 ? 1 : v > 255 ? 255 : v;
}

void modify_hp(party::Character& c, const classes::Tables& t, const Facts& f, int dir)
{
    uint8_t* r = c.rec;
    int hp = r[kHpMax] + (dir > 0 ? 1 : dir < 0 ? -1 : 0);
    const int lo = hp_least(c, t, f), hi = hp_most(c, t, f);
    if (hp > hi) hp = hi;
    if (hp < lo) hp = lo;
    r[kHpMax] = static_cast<uint8_t>(hp);
    r[kHp] = r[kHpMax];
}

void modify_stat(party::Character& c, const classes::Tables& t, const Facts& f, int i, int dir)
{
    if (i < 0 || i > 5 || !dir) return;
    const int race = c.race(), sex = c.sex() ? 1 : 0, cls = c.cls();
    const uint16_t lim = static_cast<uint16_t>(t.lay.stat_limits + t.race_row(race) * 16);
    int lo, hi;
    if (i == 0) {
        lo = t.u8(static_cast<uint16_t>(lim + sex));
        hi = t.u8(static_cast<uint16_t>(lim + 2 + sex));
    } else {
        lo = t.u8(static_cast<uint16_t>(lim + 4 + i * 2));
        hi = t.u8(static_cast<uint16_t>(lim + 5 + i * 2));
    }
    const bool strong = c.level(classes::Fighter) || c.level(classes::Ranger) || c.level(classes::Paladin);
    int v = stat_full(c, i);
    const int was = v;
    if (dir > 0) {
        ++v;
        if (v > hi) v = hi;
        if (v < lo) v = lo;
        set_stat(c, i, v);
        if (i == 0 && t.lay.pool) {
            // Pool's program (ovr17 +2581): past the maximum, a fighter's
            // 18/xx goes up instead (so 17 -> 18 first, then 18/01)
            if (was + 1 > hi && was + 1 > 18 && strong) {
                const int emax = t.u8(static_cast<uint16_t>(lim + 4 + sex));
                if (stat_full(c, 6) < emax) set_stat(c, 6, stat_full(c, 6) + 1);
            }
        } else if (i == 0) {
            if (v == 18 && strong) {
                int e = stat_full(c, 6) + 1;
                const int emax = t.u8(static_cast<uint16_t>(lim + 4 + sex));
                if (e > emax) e = emax;
                set_stat(c, 6, e);
            } else {
                set_stat(c, 6, 0);
            }
        }
    } else if (i == 0 && stat_full(c, 6) > 0) {
        set_stat(c, 6, stat_full(c, 6) - 1);         // through the exceptional strength first
    } else {
        --v;
        const int mn = t.u8(static_cast<uint16_t>(t.lay.class_min + cls * 6 + i));
        if (t.lay.pool) {
            if (v < lo || v < mn) v = lo;           // (Pool's program: the race's minimum)
        } else {
            if (v < lo) v = lo;
            if (v < mn) v = mn;
            if (i == 2 && v < 13 && cls >= ClericFighter && cls <= ClericThief) v = 13;
        }
        if (v > hi) v = hi;
        set_stat(c, i, v);
    }
    if (i == 4) modify_hp(c, t, f, 0);              // the hit points' bounds follow Constitution
}

void modify_done(party::Character& c, const classes::Tables& t, const Facts& f)
{
    (void)f;
    classes::class_bonuses(c, t);
    uint8_t* r = c.rec;
    const int con = stat_full(c, 4);
    int base = 0, n = 0;
    for (int k = 0; k <= classes::Monk; ++k) {
        const int lv = c.level(k);
        if (lv <= 0) continue;
        ++n;
        const int cb = class_con_bonus(con, k);
        if (lv < t.max_hit_dice(k)) base += (k == classes::Ranger ? lv + 1 : lv) * cb;
        else base += (t.max_hit_dice(k) - 1) * cb;
    }
    if (n) base /= n;
    const int rolled = r[kHpMax] - base;
    // (Pool's Keep leaves the rolled figure as made, and sets its "modified"
    // mark, record 0x85; Secret's Modify never touches its rolled figure 0x131)
    if (t.lay.pool) r[0xF8] = 1;
    else if (t.lay.secret) return;
    else r[kHpRolled] = static_cast<uint8_t>(rolled < 0 ? 0 : rolled);
}

} // namespace create

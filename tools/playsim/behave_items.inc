// Behaviour checks: items (included by sweep.cpp with -DBEHAVE_ITEMS).
// What each item does when readied and used, against the original's rules
// (claude/behaviour_facts.md: items). The item records come from the
// player's own files (ITEMS_BIN: 63-byte records made in scratch space);
// the numeric tables the checks expect (Strength, Dexterity, the AD&D
// armour and weapon tables, class bits ...) from the scratch oracle file
// (ORACLE_DIR/oracle_items.txt) - nothing of the game's in the repo.
//
// Every record is checked one by one: readied alone (to-hit, damage, dice,
// AC, rear AC, movement, weight, hands, saves, stats), the ready rules
// (classes, slots, hands, cursed), in a fight (the hit roll needed, the
// damage, missiles), used (spells, charges), and the shop / Halve / Join.

#include <map>

static std::vector<uint8_t> b_items;           // all the records
static int b_n_items = 0;

static bool b_load_items()
{
    const char* file = getenv("ITEMS_BIN");
    FILE* in = fopen(file ? file : "/tmp/claude-0/sw/items.bin", "rb");
    if (!in) return false;
    uint8_t r[63];
    b_items.clear();
    while (fread(r, 1, 63, in) == 63) b_items.insert(b_items.end(), r, r + 63);
    fclose(in);
    b_n_items = static_cast<int>(b_items.size() / 63);
    return b_n_items > 0;
}

static const uint8_t* b_rec(int k) { return &b_items[static_cast<size_t>(k) * 63]; }

// ---- The oracle: "key v1 v2 ..." lines (a key may repeat: each line a row)
static std::map<std::string, std::vector<std::vector<int>>> b_or;

static bool b_load_oracle()
{
    const char* dir = getenv("ORACLE_DIR");
    const std::string p = std::string(dir ? dir : "/tmp/claude-0/bt") + "/oracle_items.txt";
    FILE* f = fopen(p.c_str(), "r");
    if (!f) return false;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        char* s = strtok(line, " \t\r\n");
        if (!s) continue;
        std::vector<int> v;
        const std::string key = s;
        while ((s = strtok(nullptr, " \t\r\n"))) v.push_back(atoi(s));
        b_or[key].push_back(v);
    }
    fclose(f);
    return !b_or.empty();
}

// Value i of the (first) row `key`
static int ov(const char* key, int i)
{
    auto it = b_or.find(key);
    if (it == b_or.end() || it->second.empty() || i < 0 || i >= static_cast<int>(it->second[0].size())) return -9999;
    return it->second[0][i];
}
// The row of `key` whose first value is `first` (nullptr: none)
static const std::vector<int>* orow(const char* key, int first)
{
    auto it = b_or.find(key);
    if (it == b_or.end()) return nullptr;
    for (const auto& v : it->second)
        if (!v.empty() && v[0] == first) return &v;
    return nullptr;
}

// The name of record k (all words), for the report lines
static const char* b_nm(const uint8_t* r)
{
    static char t[64];
    play::names->name(items::Item{r}, t, sizeof t, true);
    return t;
}

// ---- The game: Curse (the sample party: 1 a paladin, 2 a dwarf fighter /
// thief, 4 a cleric, 5 a magic-user) or Pool of Radiance (behaviour checks
// with GAME=1: 0 a fighter, 3 a cleric, 4 a magic-user, 5 a thief)
static bool b_pool = false;
static int bm_mu = 5, bm_cl = 4, bm_th = 2;

// What a readied item's code does in each game - from the facts
// (curse_finish_facts.md 5.2, poolrad_combat_facts.md 2.2 /
// poolrad_items_facts.md 2), not from the engine's table
enum BKind { BNone, BEffect, BWiz, BDex, BAlign, BGirdle, BDwarf, BIoun, BHammer, BFumble, BThief, BStupid, BPowerless,
             BOgre, BNeedGiant };
static BKind b_kind(const uint8_t* r)
{
    if (r[0x3E] < 0x80) return BNone;
    const int code = r[0x3E] & 0x7F;
    static const BKind curse[14] = {BEffect, BWiz, BDex, BNone, BAlign, BGirdle, BDwarf, BNone,
                                    BIoun, BHammer, BFumble, BThief, BStupid, BPowerless};
    static const BKind pool[12] = {BEffect, BEffect, BEffect, BOgre, BAlign, BEffect,
                                   BEffect, BNeedGiant, BEffect, BHammer, BEffect, BEffect};
    if (b_pool) return code < 12 ? pool[code] : BNone;
    return code < 14 ? curse[code] : BNone;
}
// The spell an item casts: its id (Pool: 0x17 less over 0x38 - poolrad_items_facts.md 1)
static int b_spell(const uint8_t* r)
{
    const int s = r[0x3D] & 0x7F;
    return b_pool && s > 0x38 ? s - 0x17 : s;
}
// What the character needs for the item's own rule: its alignment (a
// keyed item), giant strength (the boulder)
static void b_suit(party::Character& ch, const uint8_t* r)
{
    if (b_kind(r) == BAlign) ch.rec[0x11B] = static_cast<uint8_t>(r[0x3D] & 0x0F);
    if (b_kind(r) == BNeedGiant) ch.rec[0x10] = ch.rec[0x11] = 19;
}

// ---- A test character: party member `who` as loaded, no items, no effects
static party::Character b_saved[party::kMaxParty];

static party::Character& b_fresh(int who)
{
    party::Character& ch = play::pt->m[who];
    ch = b_saved[who];
    ch.n_items = 0;
    memset(ch.items, 0, sizeof ch.items);
    ch.n_affects = 0;
    memset(ch.affects, 0, sizeof ch.affects);
    play::pt->selected = who;
    rules::recalc(ch, *play::names, play::d->facts);
    return ch;
}

// Adds a copy of rec (readied or not, never cursed unless asked); its index
static int b_give(party::Character& ch, const uint8_t* rec, bool readied, bool keep_curse = false)
{
    const int i = ch.n_items;
    if (!rules::add_item(ch, rec)) return -1;
    ch.items[i][0x34] = readied ? 1 : 0;
    if (!keep_curse) ch.items[i][0x36] = 0;
    return i;
}

// The first record of a type with no plus and no effects (a plain one)
static const uint8_t* b_plain_of_type(int type)
{
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (r[0x2E] == type && r[0x32] == 0 && r[0x3C] == 0 && r[0x3D] == 0 && r[0x3E] == 0) return r;
    }
    return nullptr;
}
// The first record of a type with this plus
static const uint8_t* b_of_type(int type, int plus)
{
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (r[0x2E] == type && static_cast<int8_t>(r[0x32]) == plus) return r;
    }
    return nullptr;
}

// A character's only readied item: `rec` (any other items gone)
static void b_hold_only(party::Character& ch, const uint8_t* rec)
{
    using namespace play;
    ch.n_items = 0;
    if (rec) {
        rules::add_item(ch, rec);
        ch.items[0][0x34] = 1;
        ch.items[0][0x36] = 0;
    }
    rules::recalc(ch, *names, d->facts);
}

// ---- What the original works out from the readied items (coab
// ovr025 reclac_player_values, CalculateAttackValues, CalcArmorWeightEffect,
// sub_662A6, calc_movement), written from the rules with the oracle's tables
struct BVals {
    int hit, dmg, dice, sides, ac, behind, move, weight, hands, save;
};

static int b_str_group(int s, int e)
{
    if (s <= 17) return s;
    if (s == 18) return e == 0 ? 18 : e <= 50 ? 19 : e <= 75 ? 20 : e <= 90 ? 21 : e <= 99 ? 22 : 23;
    return s + 5;
}

static BVals b_expect(const party::Character& c)
{
    using namespace play;
    const uint8_t* r = c.rec;
    BVals v{};
    const int grp = b_str_group(r[0x11], r[0x1C]);
    const bool uses = r[0x125] != 0;
    const int shit = uses ? ov("str_hit", grp) : 0, sdmg = uses ? ov("str_dmg", grp) : 0;
    const int dex = r[0x17];
    const uint8_t* weapon = nullptr;
    const uint8_t* arrows = nullptr;
    const uint8_t* quarrels = nullptr;
    for (int i = 0; i < c.n_items; ++i) {
        const uint8_t* it = c.items[i];
        int w = it[0x37] | it[0x38] << 8;
        if (it[0x39] > 0) w *= it[0x39];
        v.weight += w;
        if (!it[0x34]) continue;
        const items::TypeInfo& t = names->type(it[0x2E]);
        if (t.slot == 0) weapon = it;
        if (it[0x2E] == d->facts.arrow) arrows = it;
        if (it[0x2E] == d->facts.quarrel) quarrels = it;
        v.hands += t.hands;
    }
    for (int m = 0; m < 7; ++m) v.weight += c.money(m);
    v.dice = r[0x11E];
    v.sides = r[0x120];
    v.dmg = static_cast<int8_t>(r[0x122]);
    v.hit = static_cast<int8_t>(r[0x73]);
    if (!weapon) {
        v.hit += shit;
        v.dmg += sdmg;
    } else {
        const items::TypeInfo& t = names->type(weapon[0x2E]);
        if (t.flags & 0x02) v.hit += ov("dex_missile", dex);
        v.dmg = t.bonus;
        if (t.flags & 0x04) {
            v.hit += shit;
            v.dmg += sdmg;
        }
        int plus = static_cast<int8_t>(weapon[0x32]);
        if ((t.flags & 0x80) && quarrels) plus += static_cast<int8_t>(quarrels[0x32]);
        if ((t.flags & 0x01) && arrows) plus += static_cast<int8_t>(arrows[0x32]);
        v.dmg += plus;
        if (c.race() == 2)
            for (int k = 0; k < 6; ++k)
                if (weapon[0x2E] == ov("elf_types", k)) {
                    ++plus;
                    break;
                }
        v.hit += plus;
        v.dice = t.dice;
        v.sides = t.sides;
    }
    // AC: Dex, shield, small items' plus, rings (the best), armour (the best: plus + its value)
    int b[5] = {ov("dex_ac", dex), 0, 0, 0, 0};
    bool magic_armour = false;
    v.move = r[0xE4];
    for (int i = 0; i < c.n_items; ++i) {
        const uint8_t* it = c.items[i];
        if (!it[0x34]) continue;
        const items::TypeInfo& t = names->type(it[0x2E]);
        const int plus = static_cast<int8_t>(it[0x32]);
        if (t.slot == 2) {
            const int w = it[0x37] | it[0x38] << 8;
            v.move = w <= 150 ? r[0xE4] : w <= 399 ? 9 : 6;
            if (v.move != 0 && v.move <= 9) v.move += 3;
        }
        if (t.ac < 0x80) continue;
        const int a = t.ac & 0x7F;
        if (t.slot == 1) b[1] = plus + a;
        else if (a == 0) {
            if (t.slot == 9) b[3] = plus > b[3] ? plus : b[3];
            else b[2] += plus;
            v.save += static_cast<int8_t>(it[0x33]);
        } else if (plus + a > b[4]) {
            b[4] = plus + a;
            if (plus > 0 && t.slot == 2) magic_armour = true;
        }
    }
    if (magic_armour) b[3] = 0;
    int over = v.weight - ov("str_enc", grp);
    if (over < 0) over = 0;
    const int can = over <= 0x200 ? v.move : over <= 0x300 ? 9 : over <= 0x400 ? 6 : 3;
    if (can < v.move) v.move = can;
    if (b[4] < r[0x124]) b[4] = r[0x124];
    v.ac = b[0] + b[1] + b[2] + b[3] + b[4];
    v.behind = b[4] + b[2] + b[3] - 2;
    v.hands &= 0xFF;
    return v;
}

static BVals b_engine(const party::Character& c)
{
    BVals v{};
    v.hit = static_cast<int8_t>(c.rec[0x199]);
    v.dmg = c.damage_bonus();
    v.dice = c.dice();
    v.sides = c.dice_sides();
    v.ac = c.rec[0x19A];
    v.behind = c.rec[0x19B];
    v.move = c.rec[0x1A5];
    v.weight = c.encumbrance();
    v.hands = c.rec[0x185];
    v.save = static_cast<int8_t>(c.rec[0x186]);
    return v;
}

// Compares the engine's values with the expected ones: one line
static bool b_values(const char* what, const party::Character& c)
{
    const BVals e = b_expect(c), g = b_engine(c);
    const bool ok = e.hit == g.hit && e.dmg == g.dmg && e.dice == g.dice && e.sides == g.sides && e.ac == g.ac &&
                    e.behind == g.behind && e.move == g.move && e.weight == g.weight && e.hands == g.hands &&
                    e.save == g.save;
    if (ok)
        return expect("items", what, true, "hit %d dmg %+d %dd%d AC %d rear %d move %d weight %d hands %d saves %+d", g.hit,
                      g.dmg, g.dice, g.sides, 60 - g.ac, 60 - g.behind, g.move, g.weight, g.hands, g.save);
    return expect("items", what, false,
                  "engine hit %d dmg %+d %dd%d AC %d rear %d move %d weight %d hands %d saves %+d; expected hit %d dmg %+d "
                  "%dd%d AC %d rear %d move %d weight %d hands %d saves %+d",
                  g.hit, g.dmg, g.dice, g.sides, 60 - g.ac, 60 - g.behind, g.move, g.weight, g.hands, g.save, e.hit, e.dmg,
                  e.dice, e.sides, 60 - e.ac, 60 - e.behind, e.move, e.weight, e.hands, e.save);
}

// ---- 1. Every record readied alone (and launchers with their ammunition)
static void b_readied_values()
{
    using namespace play;
    const uint8_t* arrows = b_plain_of_type(d->facts.arrow);
    const uint8_t* quarrels = b_plain_of_type(d->facts.quarrel);
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const items::TypeInfo& t = names->type(r[0x2E]);
        char what[96];
        // A fighter (Str 18/53, Dex 17) and an elf (fighter / magic-user, 18/25, Dex 16)
        for (int who : {1, 3}) {
            party::Character& ch = b_fresh(who);
            b_give(ch, r, true);
            if ((t.flags & 0x01) && arrows) b_give(ch, arrows, true);
            if ((t.flags & 0x80) && quarrels) b_give(ch, quarrels, true);
            rules::recalc(ch, *names, d->facts);
            snprintf(what, sizeof what, "record %d %s readied (%s)", k, b_nm(r), who == 1 ? "fighter" : "elf");
            b_values(what, ch);
        }
    }
    // Launchers with magic ammunition: the arrows' / quarrels' plus to hit and damage
    const uint8_t* arrows1 = b_of_type(d->facts.arrow, 1);
    const uint8_t* arrows3 = b_of_type(d->facts.arrow, 3);
    const uint8_t* quarrels1 = b_of_type(d->facts.quarrel, 1);
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const items::TypeInfo& t = names->type(r[0x2E]);
        if (t.slot != 0 || !(t.flags & 0x81)) continue;
        const uint8_t* ammo[2] = {(t.flags & 0x01) ? arrows1 : quarrels1, (t.flags & 0x01) ? arrows3 : nullptr};
        for (const uint8_t* a : ammo) {
            if (!a) continue;
            party::Character& ch = b_fresh(1);
            b_give(ch, r, true);
            b_give(ch, a, true);
            rules::recalc(ch, *names, d->facts);
            const int hit0 = static_cast<int8_t>(ch.rec[0x199]), dmg0 = ch.damage_bonus();
            char what[96];
            snprintf(what, sizeof what, "record %d %s with %s", k, b_nm(r), b_nm(a));
            b_values(what, ch);
            // ... and the plus counted once more than with plain ammunition
            ch.items[1][0x32] = 0;
            rules::recalc(ch, *names, d->facts);
            const int p = static_cast<int8_t>(a[0x32]);
            snprintf(what, sizeof what, "record %d %s: the ammunition's +%d to hit and damage", k, b_nm(r), p);
            expect("items", what, hit0 - static_cast<int8_t>(ch.rec[0x199]) == p && dmg0 - ch.damage_bonus() == p,
                   "hit %d -> %d, damage %+d -> %+d", static_cast<int8_t>(ch.rec[0x199]), hit0, ch.damage_bonus(), dmg0);
        }
    }
    // A launcher's plus with the wrong ammunition doesn't count it (a bow with quarrels)
    {
        const uint8_t* bow = b_plain_of_type(43);
        if (bow && quarrels1) {
            party::Character& ch = b_fresh(1);
            b_give(ch, bow, true);
            b_give(ch, quarrels1, true);
            rules::recalc(ch, *names, d->facts);
            b_values("long bow with +1 quarrels: the quarrels' plus doesn't count", ch);
        }
    }
}

// ---- 1b. The rules against AD&D: armour AC, shields, magic armour and rings,
// bracers, weapon dice, two weapons' worth of hands
static void b_adnd_tables()
{
    using namespace play;
    // The ITEMS table's damage against AD&D's (PHB): man-sized and large
    for (int type = 1; type < 128; ++type) {
        const std::vector<int>* w = orow("wdmg", type);
        if (!w) continue;
        const items::TypeInfo& t = names->type(type);
        char what[96];
        snprintf(what, sizeof what, "type %d damage dice as AD&D (S-M %dd%d%+d, L %dd%d%+d)", type, (*w)[1], (*w)[2], (*w)[3],
                 (*w)[4], (*w)[5], (*w)[6]);
        const bool same = t.dice == (*w)[1] && t.sides == (*w)[2] && t.bonus == (*w)[3] && t.dice_large == (*w)[4] &&
                          t.sides_large == (*w)[5] && t.bonus_large == (*w)[6];
        // (the game's own table: a difference is the original's, not the engine's - noted, not failed)
        printf("%s items: %s - the game's table S-M %dd%d%+d L %dd%d%+d\n", same ? "PASS" : "NOTE", what, t.dice, t.sides,
               t.bonus, t.dice_large, t.sides_large, t.bonus_large);
    }
    // Armour alone: AC as AD&D's armour table (no Dex: a Dex 10 character)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const std::vector<int>* a = orow("armour", r[0x2E]);
        if (!a) continue;
        party::Character& ch = b_fresh(1);
        ch.rec[0x16] = ch.rec[0x17] = 10;
        b_give(ch, r, true);
        rules::recalc(ch, *names, d->facts);
        const int want = (*a)[1] - static_cast<int8_t>(r[0x32]);
        char what[96];
        snprintf(what, sizeof what, "record %d %s: AC %d (AD&D %d - plus)", k, b_nm(r), want, (*a)[1]);
        expect("items", what, ch.ac() == want, "AC %d", ch.ac());
        // ... with a shield: 1 better (and its plus)
        const uint8_t* shield = b_plain_of_type(59);
        if (shield) {
            b_give(ch, shield, true);
            rules::recalc(ch, *names, d->facts);
            snprintf(what, sizeof what, "record %d %s + Shield: AC %d", k, b_nm(r), want - 1);
            expect("items", what, ch.ac() == want - 1, "AC %d", ch.ac());
            // rear AC: without the shield, 2 worse
            snprintf(what, sizeof what, "record %d %s + Shield: rear AC %d (no shield, 2 worse)", k, b_nm(r), want + 2);
            expect("items", what, 60 - ch.rec[0x19B] == want + 2, "rear AC %d", 60 - ch.rec[0x19B]);
        }
    }
    // Rings of protection: AC and saves better by the plus, the best of two
    // rings only, nothing with magic armour (AD&D DMG; coab sub_662A6)
    {
        const uint8_t* ring3 = nullptr;
        const uint8_t* ring2 = nullptr;
        for (int k = 0; k < b_n_items; ++k)
            if (b_rec(k)[0x2E] == 93) (b_rec(k)[0x32] == 3 ? ring3 : ring2) = b_rec(k);
        const uint8_t* plain = b_plain_of_type(55);
        const uint8_t* magic = b_of_type(55, 1);
        if (ring3 && ring2 && plain && magic) {
            party::Character& ch = b_fresh(1);
            ch.rec[0x16] = ch.rec[0x17] = 10;
            b_give(ch, ring3, true);
            rules::recalc(ch, *names, d->facts);
            expect("items", "Ring of Prot. +3 alone: AC 7, saves its record's (0x33)",
                   ch.ac() == 7 && static_cast<int8_t>(ch.rec[0x186]) == static_cast<int8_t>(ring3[0x33]),
                   "AC %d saves %+d", ch.ac(), static_cast<int8_t>(ch.rec[0x186]));
            b_give(ch, ring2, true);
            rules::recalc(ch, *names, d->facts);
            expect("items", "two rings of protection (+3, +2): AC from the best only", ch.ac() == 7, "AC %d", ch.ac());
            b_give(ch, plain, true);
            rules::recalc(ch, *names, d->facts);
            expect("items", "ring +3 with plain chain mail: AC 5 - 3 = 2", ch.ac() == 2, "AC %d", ch.ac());
            ch.items[2][0x34] = 0;
            b_give(ch, magic, true);
            rules::recalc(ch, *names, d->facts);
            expect("items", "ring +3 with Chain Mail +1: the ring's AC doesn't count (AC 4)", ch.ac() == 4, "AC %d", ch.ac());
            expect("items", "ring +3 with Chain Mail +1: the ring's saves still count (its 0x33)",
                   static_cast<int8_t>(ch.rec[0x186]) == static_cast<int8_t>(ring3[0x33]), "saves %+d",
                   static_cast<int8_t>(ch.rec[0x186]));
        }
    }
    // Bracers: as armour of AC (10 - plus) - the better of bracers and armour, never both
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (r[0x2E] != 77) continue;
        const int plus = static_cast<int8_t>(r[0x32]);
        party::Character& ch = b_fresh(1);
        ch.rec[0x16] = ch.rec[0x17] = 10;
        b_give(ch, r, true);
        rules::recalc(ch, *names, d->facts);
        const int want = plus > 0 ? 10 - plus : 10;
        char what[96];
        snprintf(what, sizeof what, "record %d %s (plus %d): AC %d", k, b_nm(r), plus, want);
        expect("items", what, ch.ac() == want, "AC %d", ch.ac());
        const uint8_t* plate = b_plain_of_type(58);
        if (plate) {
            b_give(ch, plate, true);
            rules::recalc(ch, *names, d->facts);
            const int both = want < 3 ? want : 3;
            snprintf(what, sizeof what, "record %d %s with plate mail: AC %d (the better, not both)", k, b_nm(r), both);
            expect("items", what, ch.ac() == both, "AC %d", ch.ac());
        }
    }
    // Heavy armour slows: over 150 weight movement 12 (base) -> 12 if <= 150, 9+3 = 12 for 151-399?
    // (the original: 151-399 -> 9, 400+ -> 6, then +3 when 9 or less: 12 / 9)
    {
        const int weights[] = {100, 150, 151, 399, 400, 450};
        const int want[] = {12, 12, 12, 12, 9, 9};
        const uint8_t* plate = b_plain_of_type(58);
        for (int j = 0; plate && j < 6; ++j) {
            party::Character& ch = b_fresh(1);
            const int i = b_give(ch, plate, true);
            ch.items[i][0x37] = static_cast<uint8_t>(weights[j]);
            ch.items[i][0x38] = static_cast<uint8_t>(weights[j] >> 8);
            rules::recalc(ch, *names, d->facts);
            char what[64];
            snprintf(what, sizeof what, "armour weighing %d: movement %d", weights[j], want[j]);
            expect("items", what, ch.movement() == want[j], "movement %d", ch.movement());
        }
    }
    // Carrying: past the Strength allowance + 512 movement 9, + 768 6, more 3
    {
        party::Character& ch = b_fresh(1);
        const int grp = b_str_group(ch.rec[0x11], ch.rec[0x1C]);
        const int allow = ov("str_enc", grp);
        const int loads[] = {allow + 0x200, allow + 0x201, allow + 0x300, allow + 0x301, allow + 0x400, allow + 0x401};
        const int want[] = {12, 9, 9, 6, 6, 3};
        for (int m = 0; m < 7; ++m) ch.rec[0xFB + m * 2] = ch.rec[0xFC + m * 2] = 0;   // (no other coins)
        for (int j = 0; j < 6; ++j) {
            ch.rec[0xFB + 4 * 2] = static_cast<uint8_t>(loads[j]);
            ch.rec[0xFC + 4 * 2] = static_cast<uint8_t>(loads[j] >> 8);
            rules::recalc(ch, *names, d->facts);
            char what[64];
            snprintf(what, sizeof what, "carrying %d coins (allowance %d): movement %d", loads[j], allow, want[j]);
            expect("items", what, ch.movement() == want[j], "movement %d", ch.movement());
        }
    }
}

// ---- 2. The ready rules (ovr020 ready_Item): the class test against the
// class bits, one weapon / shield / armour ..., two rings, one pile of
// arrows / quarrels, two hands; a cursed item can't be put away
static bool b_ready(int i)
{
    using namespace play;
    party::Character& ch = *pt->sel();
    view_from = Screen::Game;
    screen = Screen::Items;
    ready_item(i, C);
    note_until = 0;
    screen = Screen::Game;
    return ch.items[i][0x34] != 0;
}

static void b_ready_rules()
{
    using namespace play;
    // The class bits each sample character has (the program's table): every class with a level
    for (int m = 0; m < pt->count; ++m) {
        const party::Character& ch = b_saved[m];
        int want = 0;
        for (int k = 0; k < 8; ++k)
            if (ch.level(k) > 0 || (ch.old_level(k) > 0 && ch.old_level(k) < ch.rec[0xE5])) want += ov("class_bits", k);
        char what[64];
        snprintf(what, sizeof what, "member %d's class bits (0x12B)", m);
        expect("items", what, ch.rec[0x12B] == want, "%02X, expected %02X", ch.rec[0x12B], want);
    }
    // Every record by every single class
    static const char* cls_name[8] = {"cleric", "druid", "fighter", "paladin", "ranger", "magic-user", "thief", "monk"};
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const items::TypeInfo& t = names->type(r[0x2E]);
        int ok_for = 0, wrong = 0;
        for (int cl = 0; cl < 8; ++cl) {
            if (cl == 1 || cl == 7) continue;           // (no druids or monks among the player's classes)
            party::Character& ch = b_fresh(1);
            ch.rec[0x12B] = static_cast<uint8_t>(ov("class_bits", cl));
            b_suit(ch, r);
            const int i = b_give(ch, r, false);
            rules::recalc(ch, *names, d->facts);
            const bool may = (t.classes & ov("class_bits", cl)) != 0;
            const bool got = b_ready(i);
            if (got == may) ++ok_for;
            else {
                ++wrong;
                char what[96];
                snprintf(what, sizeof what, "record %d %s readied by a %s", k, b_nm(r), cls_name[cl]);
                expect("items", what, false, "%s, the table says %s", got ? "readied" : "refused", may ? "yes" : "no");
            }
        }
        if (!wrong) {
            char what[96];
            snprintf(what, sizeof what, "record %d %s: ready by class as the table (classes %02X)", k, b_nm(r), t.classes);
            expect("items", what, true);
        }
    }
    // AD&D's class rules the table should keep: magic-users no armour or shields, clerics no edged weapons
    for (int type = 1; type < 128; ++type) {
        const items::TypeInfo& t = names->type(type);
        if (t.slot == 0xFF) continue;
        if ((t.slot == 1 || (t.slot == 2 && t.ac >= 0x80)) && (t.classes & ov("class_bits", 5))) {
            char what[64];
            snprintf(what, sizeof what, "type %d (armour / shield): not for magic-users", type);
            expect("items", what, false, "classes %02X", t.classes);
        }
    }
    {
        static const int edged[] = {1, 2, 8, 30, 34, 35, 36, 37, 38, 41, 42, 43, 44, 46};
        bool ok = true;
        for (int type : edged)
            if (names->type(type).classes & ov("class_bits", 0)) ok = false;
        expect("items", "clerics: no edged weapons or bows (the ITEMS table)", ok);
        static const int blunt[] = {7, 12, 20, 23, 33};
        ok = true;
        for (int type : blunt)
            if (!(names->type(type).classes & ov("class_bits", 0))) ok = false;
        expect("items", "clerics: club, flail, hammer, mace, staff (the ITEMS table)", ok);
    }
    // Slots: a second weapon / shield / armour / cloak ... is "already using"
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const items::TypeInfo& t = names->type(r[0x2E]);
        if (t.slot > 9 || (t.classes & 0x48) == 0) continue;
        party::Character& ch = b_fresh(1);
        ch.rec[0x12B] = 0xFF;
        b_suit(ch, r);
        // the hands: a weapon and a shield may make 3 - keep to items of no hands for this
        const int a = b_give(ch, r, false), bb = b_give(ch, r, false), c3 = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        const bool first = b_ready(a);
        const bool second = b_ready(bb);
        const bool third = b_ready(c3);
        char what[96];
        if (t.slot == 9) {
            snprintf(what, sizeof what, "record %d %s: two may be worn, not three", k, b_nm(r));
            expect("items", what, first && second && !third, "%d %d %d", first, second, third);
        } else {
            snprintf(what, sizeof what, "record %d %s: one at a time (slot %d)", k, b_nm(r), t.slot);
            expect("items", what, first && !second && !third, "%d %d %d", first, second, third);
        }
    }
    // Ammunition: one pile of arrows and one of quarrels readied at a time
    for (int type : {static_cast<int>(d->facts.arrow), static_cast<int>(d->facts.quarrel)}) {
        const uint8_t* r = b_plain_of_type(type);
        if (!r) continue;
        party::Character& ch = b_fresh(1);
        const int a = b_give(ch, r, false), bb = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        const bool first = b_ready(a), second = b_ready(bb);
        char what[64];
        snprintf(what, sizeof what, "%s: a second pile is \"already using\"", b_nm(r));
        expect("items", what, first && !second, "%d %d", first, second);
    }
    // Hands: a two-handed weapon and a shield don't go together (either way round)
    {
        const uint8_t* two = b_plain_of_type(38);
        const uint8_t* shield = b_plain_of_type(59);
        const uint8_t* sword = b_plain_of_type(36);
        const uint8_t* potion = nullptr;
        for (int k = 0; k < b_n_items && !potion; ++k)
            if (b_rec(k)[0x2E] == 71 && b_rec(k)[0x3D]) potion = b_rec(k);
        if (two && shield && sword) {
            party::Character& ch = b_fresh(1);
            int a = b_give(ch, two, false), bb = b_give(ch, shield, false);
            rules::recalc(ch, *names, d->facts);
            bool x = b_ready(a), y = b_ready(bb);
            expect("items", "two-handed sword, then a shield: \"Your hands are full!\"", x && !y, "%d %d", x, y);
            ch = b_fresh(1);
            a = b_give(ch, shield, false);
            bb = b_give(ch, two, false);
            rules::recalc(ch, *names, d->facts);
            x = b_ready(a);
            y = b_ready(bb);
            expect("items", "a shield, then a two-handed sword: hands full", x && !y, "%d %d", x, y);
            ch = b_fresh(1);
            a = b_give(ch, sword, false);
            bb = b_give(ch, shield, false);
            rules::recalc(ch, *names, d->facts);
            x = b_ready(a);
            y = b_ready(bb);
            expect("items", "long sword and shield: both readied", x && y, "%d %d", x, y);
            if (potion) {
                const int p = b_give(ch, potion, false);
                rules::recalc(ch, *names, d->facts);
                const bool z = b_ready(p);
                expect("items", "sword + shield, then a potion (1 hand): hands full", !z, "%d", z);
            }
        }
    }
    // Cursed: readied it stays ("It's Cursed"); not cursed it comes off
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!r[0x36]) continue;
        party::Character& ch = b_fresh(1);
        ch.rec[0x12B] = 0xFF;
        const int i = b_give(ch, r, false, true);
        rules::recalc(ch, *names, d->facts);
        const bool on = b_ready(i);
        const bool still = b_ready(i);
        char what[96];
        snprintf(what, sizeof what, "record %d %s (cursed): readied, then can't be put away", k, b_nm(r));
        expect("items", what, on && still, "readied %d, after Ready again %d", on, still);
    }
}

// ---- 3. Names: the words shown (hidden words until identified), the
// plural "s", Detect Magic's star
static void b_names_detect()
{
    using namespace play;
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        // Hidden words: bit 4 hides word 1, 2 word 2, 1 word 3 (coab Item.GenerateName)
        char full[64], shown[64];
        names->name(items::Item{r}, full, sizeof full, true);
        names->name(items::Item{r}, shown, sizeof shown);
        std::string want;
        if (r[0x39] > 0) want = std::to_string(r[0x39]) + " ";
        bool any = false;
        for (int w = 3; w >= 1; --w) {
            const int word = r[0x2E + w];
            const int bit = w == 1 ? 4 : w == 2 ? 2 : 1;
            if (!word || (r[0x35] & bit)) continue;
            if (any) want += " ";
            any = true;
            want += names->word(word);
        }
        // (the plural "s" is the names' own rule - compare without it when counted)
        std::string got = shown;
        bool ok = got == want;
        if (!ok && r[0x39] >= 2) {
            // allow exactly one "s" added at the end of a word
            for (size_t p = 0; p < got.size() && !ok; ++p)
                if (got[p] == 's' && (p + 1 == got.size() || got[p + 1] == ' ') && got.substr(0, p) + got.substr(p + 1) == want)
                    ok = true;
        }
        char what[96];
        snprintf(what, sizeof what, "record %d name shown \"%s\"", k, shown);
        expect("items", what, ok, "expected \"%s\" (hidden %d)", want.c_str(), r[0x35]);
        // Detect Magic: "* " when plus > 0, saves plus > 0 or cursed (signed tests)
        names->detect = true;
        char det[64];
        names->name(items::Item{r}, det, sizeof det);
        names->detect = false;
        const bool magic = static_cast<int8_t>(r[0x32]) > 0 || static_cast<int8_t>(r[0x33]) > 0 || r[0x36];
        snprintf(what, sizeof what, "record %d %s: Detect Magic %s", k, full, magic ? "stars it" : "doesn't star it");
        expect("items", what, (strncmp(det, "* ", 2) == 0) == magic, "\"%s\"", det);
    }
    // 0x33 as a signed byte: 0x80 and up aren't magic
    uint8_t t[63];
    memcpy(t, b_rec(0), 63);
    t[0x32] = 0;
    t[0x33] = 0x90;
    t[0x36] = 0;
    names->detect = true;
    char det[64];
    names->name(items::Item{t}, det, sizeof det);
    names->detect = false;
    expect("items", "Detect Magic: saves plus 0x90 (negative) gets no star", strncmp(det, "* ", 2) != 0, "\"%s\"", det);
}

// ---- 5. Money: sell, buy, identify, appraise; Halve, Join; weight
static void b_money()
{
    using namespace play;
    // Sell (coab ShopSellItem): half the value; piles of arrows / quarrels each one, others count / 20 of each
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const int value = r[0x3A] | r[0x3B] << 8, n = r[0x39];
        int want = value > 0 ? value / 2 : 0;
        if (n > 1) {
            if (r[0x2E] != d->facts.arrow && r[0x2E] != d->facts.quarrel) want = n * want / 20;
            else want *= n;
        }
        const int got = rules::sell_value(r, d->facts);
        char what[96];
        snprintf(what, sizeof what, "record %d %s sells for %d gold", k, b_nm(r), want);
        expect("items", what, got == want, "%d", got);
    }
    // Buy: the value by the shop's price factor (1/16 ... x8; 0 counts 1)
    {
        uint8_t t[63];
        memcpy(t, b_rec(22), 63);
        const int factors[] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
        for (int value : {0, 15, 400, 20000})
            for (int f : factors) {
                t[0x3A] = static_cast<uint8_t>(value);
                t[0x3B] = static_cast<uint8_t>(value >> 8);
                const int v = value ? value : 1;
                const int want = f == 0x10 ? v : f < 0x10 ? v * f / 16 : v * (f / 16);
                char what[64];
                snprintf(what, sizeof what, "value %d at price factor %d: %d", value, f, want);
                expect("items", what, rules::price(t, f) == want, "%d", rules::price(t, f));
            }
    }
    // Gems and jewellery (the appraisal tables)
    {
        const int* g = nullptr;
        (void)g;
        int lo = 1;
        for (int j = 0; j + 1 < static_cast<int>(b_or["gems"][0].size()); j += 2) {
            const int top = b_or["gems"][0][j], val = b_or["gems"][0][j + 1];
            bool ok = true;
            for (int roll = lo; roll <= top; ++roll)
                if (rules::gem_value(roll) != val) ok = false;
            char what[64];
            snprintf(what, sizeof what, "gem rolls %d-%d: %d gp", lo, top, val);
            expect("items", what, ok);
            lo = top + 1;
        }
        lo = 1;
        const std::vector<int>& jw = b_or["jewels"][0];
        for (size_t j = 0; j + 2 < jw.size(); j += 3) {
            const int top = jw[j], base = jw[j + 1], range = jw[j + 2];
            bool ok = true;
            for (int roll = lo; roll <= top; ++roll) {
                dice_faces({1});
                const int a = rules::jewel_value(roll, rng);
                dice_faces({range});
                const int b2 = rules::jewel_value(roll, rng);
                if (a != base || b2 != base + range - 1) ok = false;
            }
            dice_usual();
            char what[80];
            snprintf(what, sizeof what, "jewellery rolls %d-%d: %d + 0..%d gp", lo, top, base, range - 1);
            expect("items", what, ok);
            lo = top + 1;
        }
    }
    // Halve (coab halve_items): count / 2 to a new pile, not readied; a single can't
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (r[0x39] < 2) continue;
        party::Character& ch = b_fresh(1);
        const int i = b_give(ch, r, true);
        const int n = r[0x39];
        const bool ok = rules::halve(ch, i);
        char what[96];
        snprintf(what, sizeof what, "record %d %s halved: %d and %d (new pile not readied)", k, b_nm(r), n - n / 2, n / 2);
        expect("items", what,
               ok && ch.n_items == 2 && ch.items[0][0x39] == n - n / 2 && ch.items[1][0x39] == n / 2 && ch.items[1][0x34] == 0 &&
                   ch.items[0][0x34] == 1,
               "%d piles: %d (readied %d), %d (readied %d)", ch.n_items, ch.items[0][0x39], ch.items[0][0x34],
               ch.items[1][0x39], ch.items[1][0x34]);
        // Join puts them back together
        rules::join(ch, 0);
        if (r[0x3C] >= 2) {                     // (charges 2 or more: Join leaves them - ovr20 / Pool's ovr20+19B7)
            snprintf(what, sizeof what, "record %d %s (%d charges): not joined again", k, b_nm(r), r[0x3C]);
            expect("items", what, ch.n_items == 2, "%d piles", ch.n_items);
            continue;
        }
        snprintf(what, sizeof what, "record %d %s joined again: one pile of %d", k, b_nm(r), n);
        expect("items", what, ch.n_items == 1 && ch.items[0][0x39] == n, "%d piles, %d", ch.n_items, ch.items[0][0x39]);
    }
    {
        party::Character& ch = b_fresh(1);
        const uint8_t* sword = b_plain_of_type(36);
        const int i = b_give(ch, sword, false);
        expect("items", "Halve a single long sword: can't", !rules::halve(ch, i) && ch.n_items == 1, "%d items", ch.n_items);
        // Join: the same items only (type, words, plus, saves, cursed, weight, effects; charges under 2)
        const uint8_t* a10 = b_plain_of_type(d->facts.arrow);
        const uint8_t* a1 = b_of_type(d->facts.arrow, 1);
        ch = b_fresh(1);
        b_give(ch, a10, false);
        b_give(ch, a10, false);
        b_give(ch, a1, false);
        rules::join(ch, 0);
        expect("items", "Join: plain arrows join, +1 arrows don't", ch.n_items == 2 && ch.items[0][0x39] == 2 * a10[0x39],
               "%d piles, first %d", ch.n_items, ch.items[0][0x39]);
        // 255 at most: the rest stays in the other pile (coab join_items)
        uint8_t big[63];
        memcpy(big, a10, 63);
        big[0x39] = 200;
        ch = b_fresh(1);
        b_give(ch, big, false);
        b_give(ch, big, false);
        rules::join(ch, 0);
        expect("items", "Join 200 + 200 arrows: 255 and 145", ch.n_items == 2 && ch.items[0][0x39] == 255 && ch.items[1][0x39] == 145,
               "%d piles: %d, %d", ch.n_items, ch.items[0][0x39], ch.n_items > 1 ? ch.items[1][0x39] : 0);
        // A pile of potions with 3 doses each doesn't join (charges 2 or more)
        uint8_t pot[63];
        memcpy(pot, b_rec(96), 63);
        for (int k = 0; k < b_n_items; ++k)
            if (b_rec(k)[0x3C] >= 2 && b_rec(k)[0x3D] && b_rec(k)[0x3E] < 0x80 && names->type(b_rec(k)[0x2E]).slot > 9) {
                memcpy(pot, b_rec(k), 63);      // (a potion of several doses, whichever the game has)
                break;
            }
        pot[0x39] = 2;
        ch = b_fresh(1);
        b_give(ch, pot, false);
        b_give(ch, pot, false);
        rules::join(ch, 0);
        expect("items", "Join: items with 2+ charges don't join", ch.n_items == 2, "%d piles", ch.n_items);
    }
    // Weight: each item's weight times its count (single items count 1), and the coins
    {
        party::Character& ch = b_fresh(1);
        int want = 0;
        for (int k = 0; k < 16; ++k) {
            const uint8_t* r = b_rec(k * 12 % b_n_items);
            b_give(ch, r, false);
            want += (r[0x37] | r[0x38] << 8) * (r[0x39] > 0 ? r[0x39] : 1);
        }
        for (int m = 0; m < 7; ++m) want += ch.money(m);
        rules::recalc(ch, *names, d->facts);
        expect("items", "weight carried: items x count + every coin", ch.encumbrance() == want, "%d, expected %d", ch.encumbrance(),
               want);
        // Can't take a 17th item, nor one past the load (1500 + allowance)
        uint8_t feather[63];
        memcpy(feather, b_rec(22), 63);
        feather[0x37] = feather[0x38] = 0;
        expect("items", "a 17th item: too many", rules::too_heavy(ch, feather, *names, d->facts));
        ch = b_fresh(1);
        rules::recalc(ch, *names, d->facts);
        const int room = rules::max_load(ch) - ch.encumbrance();
        uint8_t heavy[63];
        memcpy(heavy, b_rec(22), 63);
        heavy[0x37] = static_cast<uint8_t>(room);
        heavy[0x38] = static_cast<uint8_t>(room >> 8);
        const bool fits = !rules::too_heavy(ch, heavy, *names, d->facts);
        heavy[0x37] = static_cast<uint8_t>(room + 1);
        heavy[0x38] = static_cast<uint8_t>((room + 1) >> 8);
        const bool over = rules::too_heavy(ch, heavy, *names, d->facts);
        expect("items", "an item up to the load fits, one more is too heavy", fits && over, "fits %d over %d (room %d)", fits, over,
               room);
    }
}

// ---- 4. In a fight (the inn's fight of save A): the weapon's numbers
// where they count - the hit roll needed, the damage against man-sized and
// large, missiles (rate of fire, ammunition, range), the weapons' and
// armour's own effects
static const int kA = 1, kT = 6;            // the attacker (MARK, a paladin) and a monster

// Only fighters a and t on the field: a at (12, 12) facing east, t dx squares east of it
static void b_place(int a, int t, int dx)
{
    combat::Battle& b = play::fg->b;
    for (int i = 0; i < b.n; ++i)
        if (i != a && i != t) b.f[i].size = 0;
    b.f[a].size = b.f[t].size = 1;
    b.f[a].x = 12;
    b.f[a].y = 12;
    b.f[a].facing = 2;
    b.f[t].x = 12 + dx;
    b.f[t].y = 12;
    b.f[t].facing = 6;
    combat::occupancy(b);
}

// A target that won't fall: AC (stored), size byte, monster type, no effects
static void b_target(int t, int ac, int size_byte = 1, int mtype = 0)
{
    combat::Fighter& f = play::fg->b.f[t];
    f.rec[0x19A] = f.rec[0x19B] = static_cast<uint8_t>(ac);
    f.rec[0x1A4] = f.rec[0x78] = 250;
    f.rec[0x195] = 0;
    f.rec[0x196] = 1;
    f.rec[0xDE] = static_cast<uint8_t>(size_byte);
    f.rec[0x11A] = static_cast<uint8_t>(mtype);
    if (f.n_aff) *f.n_aff = 0;
    f.received = f.turns = 0;
    f.facing = 6;
}

// The party member's items as the fight sees them
static void b_sync(int a)
{
    combat::Fighter& f = play::fg->b.f[a];
    f.n_items = play::pt->m[f.member].n_items;
}

// One swing (slot 1) with these die faces: hit?, damage
static combat::Attack b_swing(int a, int t, std::initializer_list<int> faces)
{
    using namespace play;
    combat::Fighter& f = fg->b.f[a];
    f.attacks[0] = 1;
    f.attacks[1] = 0;
    fg->b.f[t].received = 0;
    dice_faces(faces);
    const combat::Attack at = combat::attack(fg->b, a, t, names, rng, false);
    dice_usual();
    return at;
}

static bool b_in_fight()
{
    using namespace play;
    if (!boot() || !start_fight()) return false;
    fg->b.to_hit_party = fg->b.to_hit_monsters = 0;
    return true;
}

// Missile helpers (coab ovr014 reclac_attacks, ovr025 GetCurrentAttackItem /
// is_weapon_ranged): the readied weapon shoots when its range is over 1 and
// it has what it shoots (a thrown weapon itself, a bow its arrows, a
// crossbow its quarrels, a sling nothing)
static const uint8_t* b_shot_item(const party::Character& ch, bool* found)
{
    using namespace play;
    *found = false;
    const uint8_t* w = nullptr;
    const uint8_t* arrows = nullptr;
    const uint8_t* quarrels = nullptr;
    for (int i = 0; i < ch.n_items; ++i) {
        const uint8_t* it = ch.items[i];
        if (!it[0x34]) continue;
        if (names->type(it[0x2E]).slot == 0) w = it;
        if (it[0x2E] == d->facts.arrow) arrows = it;
        if (it[0x2E] == d->facts.quarrel) quarrels = it;
    }
    if (!w || names->type(w[0x2E]).range <= 1) return nullptr;
    const int fl = names->type(w[0x2E]).flags;
    const uint8_t* item = nullptr;
    if (fl & 0x10) item = w;
    if (fl & 0x08) {
        if (fl & 0x01) item = arrows;
        if (fl & 0x80) item = quarrels;
    }
    *found = item != nullptr || fl == 0x0A;
    return item;
}

// The attacks a round with what's readied (round `round`)
static int b_expect_attacks(const party::Character& ch, int round)
{
    using namespace play;
    bool found = false;
    const uint8_t* item = b_shot_item(ch, &found);
    int half = ch.rec[0x11C];
    const uint8_t* w = nullptr;
    for (int i = 0; i < ch.n_items; ++i)
        if (ch.items[i][0x34] && names->type(ch.items[i][0x2E]).slot == 0) w = ch.items[i];
    if (found) {
        half = names->type(w[0x2E]).attacks;
        if (half < 2) half = 2;
    }
    int n = (half + (round & 1)) / 2;
    if (found && item) {
        const int ammo = item[0x39] > 1 ? item[0x39] : 1;
        if (ammo < n && item[0x39] > 0) n = ammo;
    }
    return n;
}

static void b_fight_weapons()
{
    using namespace play;
    if (!b_in_fight()) {
        expect("items", "the inn's fight", false);
        return;
    }
    combat::Battle& b = fg->b;
    const uint8_t* arrows = b_plain_of_type(d->facts.arrow);
    const uint8_t* quarrels = b_plain_of_type(d->facts.quarrel);
    // Every weapon record: the d20 needed (target AC - to-hit value), 1 misses,
    // the dice and bonus against man-sized and large (coab PC_CanHitTarget,
    // sub_3E192, AttackTarget01's large dice)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const items::TypeInfo& t = names->type(r[0x2E]);
        if (t.slot != 0) continue;
        for (int who : {kA, 3}) {
            party::Character& ch = b_fresh(who);
            b_give(ch, r, true);
            if (t.flags & 0x01) b_give(ch, arrows, true);
            if (t.flags & 0x80) b_give(ch, quarrels, true);
            rules::recalc(ch, *names, d->facts);
            b_sync(who);
            const BVals e = b_expect(ch);
            const int ac = e.hit + 10;                 // a roll of 10 needed
            b_place(who, kT, 1);
            b_target(kT, ac);
            const combat::Attack m9 = b_swing(who, kT, {9});
            b_target(kT, ac);
            const combat::Attack h10 = b_swing(who, kT, {10, 99, 99, 99, 99});
            b_target(kT, ac);
            const combat::Attack h10l = b_swing(who, kT, {10, 1, 1, 1, 1});
            b_target(kT, 0x3C + 10);                   // AC -10: only a 20
            const combat::Attack n20 = b_swing(who, kT, {20, 1, 1, 1});
            b_target(kT, 0);
            const combat::Attack n1 = b_swing(who, kT, {1});
            int hi = e.dice * e.sides + e.dmg, lo = e.dice + e.dmg;
            if (hi < 0) hi = 0;
            if (lo < 0) lo = 0;
            char what[128];
            snprintf(what, sizeof what, "record %d %s (%s): needs %d on d20 vs AC %d, %dd%d%+d", k, b_nm(r),
                     who == kA ? "fighter" : "elf", 10, 60 - ac, e.dice, e.sides, e.dmg);
            const bool ok = m9.n == 1 && !m9.hits[0].hit && h10.n == 1 && h10.hits[0].hit && h10.hits[0].damage == hi &&
                            h10l.hits[0].damage == lo && n20.n == 1 && n20.hits[0].hit && n1.n == 1 && !n1.hits[0].hit;
            expect("items", what, ok, "9: %s, 10: %s %d / %d, natural 20 %s, natural 1 %s", m9.hits[0].hit ? "hit" : "miss",
                   h10.hits[0].hit ? "hit" : "miss", h10.hits[0].damage, h10l.hits[0].damage, n20.hits[0].hit ? "hit" : "miss",
                   n1.hits[0].hit ? "hit" : "miss");
            // Large: the weapon's large dice, its bonus in place of the man-sized one
            if (who == kA) {
                b_target(kT, ac, 2);
                const combat::Attack L = b_swing(who, kT, {10, 99, 99, 99, 99});
                int lhi = t.dice_large * t.sides_large + e.dmg - t.bonus + t.bonus_large;
                if (lhi < 0) lhi = 0;
                snprintf(what, sizeof what, "record %d %s against large: %dd%d%+d", k, b_nm(r), t.dice_large, t.sides_large,
                         e.dmg - t.bonus + t.bonus_large);
                expect("items", what, L.n == 1 && L.hits[0].hit && L.hits[0].damage == lhi, "%d, expected %d",
                       L.n ? L.hits[0].damage : -1, lhi);
            }
        }
    }
    // Missile range: beyond a third of the weapon's range the target's AC is
    // 2 better, beyond two thirds 5 (coab RangedDefenseBonus); the distance
    // ignores walls
    for (int type = 1; type < 128; ++type) {
        const items::TypeInfo& t = names->type(type);
        if (t.slot != 0 || t.range <= 1) continue;
        const uint8_t* r = b_plain_of_type(type);
        if (!r) continue;
        party::Character& ch = b_fresh(kA);
        b_give(ch, r, true);
        if (t.flags & 0x01) b_give(ch, arrows, true);
        if (t.flags & 0x80) b_give(ch, quarrels, true);
        rules::recalc(ch, *names, d->facts);
        b_sync(kA);
        const BVals e = b_expect(ch);
        const int third = (t.range - 1) / 3;
        const int dists[] = {1, third, third + 1, 2 * third, 2 * third + 1, t.range - 1};
        for (int dist : dists) {
            if (dist < 1 || dist > 30) continue;
            const int pen = dist > 2 * third ? 5 : dist > third ? 2 : 0;
            const int ac = e.hit + 10;
            b_place(kA, kT, dist);
            b_target(kT, ac);
            const combat::Attack miss = b_swing(kA, kT, {9 + pen});
            b_target(kT, ac);
            const combat::Attack hit = b_swing(kA, kT, {10 + pen, 1, 1, 1});
            char what[96];
            snprintf(what, sizeof what, "%s (range %d) at %d squares: needs %d (range -%d)", b_nm(r), t.range, dist, 10 + pen,
                     pen);
            expect("items", what, !miss.hits[0].hit && hit.hits[0].hit, "%d misses: %s, %d hits: %s", 9 + pen,
                   miss.hits[0].hit ? "no" : "yes", 10 + pen, hit.hits[0].hit ? "yes" : "no");
        }
    }
    // Rate of fire: a missile weapon with what it shoots gives its own
    // attacks a round (half attacks, at least 2), no more than the pile holds
    // (coab reclac_attacks); otherwise the character's (fighters 7+: 3/2)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const items::TypeInfo& t = names->type(r[0x2E]);
        if (t.slot != 0 || t.range <= 1) continue;
        for (int pile : {0, 1, 2, 12}) {
            party::Character& ch = b_fresh(kA);
            const int i = b_give(ch, r, true);
            if (r[0x39] > 0) ch.items[i][0x39] = static_cast<uint8_t>(pile ? pile : r[0x39]);
            int ammo = -1;
            if (t.flags & 0x01) ammo = b_give(ch, arrows, true);
            if (t.flags & 0x80) ammo = b_give(ch, quarrels, true);
            if (ammo >= 0 && pile) ch.items[ammo][0x39] = static_cast<uint8_t>(pile);
            if (pile && ammo < 0 && r[0x39] == 0) continue;     // (a single weapon: no pile to vary)
            rules::recalc(ch, *names, d->facts);
            b_sync(kA);
            b_place(kA, kT, 3);
            for (int round : {0, 1}) {
                b.round = round;
                combat::start_round(b, rng);
                const int want = b_expect_attacks(ch, round);
                char what[112];
                snprintf(what, sizeof what, "record %d %s%s: %d attack(s) in round %d", k, b_nm(r),
                         pile ? (std::string(" pile ") + std::to_string(pile)).c_str() : "", want, round);
                expect("items", what, b.f[kA].attacks[0] == want, "%d", b.f[kA].attacks[0]);
            }
            // Without its ammunition: the character's own attacks
            if (ammo >= 0) {
                ch.items[ammo][0x34] = 0;
                rules::recalc(ch, *names, d->facts);
                b.round = 0;
                combat::start_round(b, rng);
                char what[96];
                snprintf(what, sizeof what, "record %d %s without ammunition: the character's 1 attack", k, b_nm(r));
                expect("items", what, b.f[kA].attacks[0] == (ch.rec[0x11C] / 2), "%d", b.f[kA].attacks[0]);
            }
        }
    }
    // Shots use up what flies: one arrow / quarrel / dart ... for each attack
    // made (count - the attacks); a thrown weapon that also strikes (flags 4
    // and 0x10) falls to the ground when the last goes, others are lost
    // (coab AttackTarget, listing ovr014:1BB3-1C62)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const items::TypeInfo& t = names->type(r[0x2E]);
        if (t.slot != 0 || t.range <= 1) continue;
        party::Character& ch = b_fresh(kA);
        b_give(ch, r, true);
        int ammo = 0;
        if (t.flags & 0x01) ammo = b_give(ch, arrows, true);
        if (t.flags & 0x80) ammo = b_give(ch, quarrels, true);
        rules::recalc(ch, *names, d->facts);
        b_sync(kA);
        b_place(kA, kT, 3);
        b.round = 0;
        combat::start_round(b, rng);
        bool found = false;
        const uint8_t* fly = b_shot_item(ch, &found);
        const int before = fly ? fly[0x39] : 0;
        const int swings = b.f[kA].attacks[0];
        fg->n_thrown = 0;                               // (the ground keeps 6: a clean start each)
        const int thrown0 = fg->n_thrown;
        const int n0 = ch.n_items;
        b_target(kT, 0x3C + 30);                        // nothing but a 20 hits
        fg->cur = kA;
        dice_faces({2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2});
        strike(C, kT);
        dice_usual();
        fg->pages = fg->at = 0;
        char what[112];
        if (!found) {
            snprintf(what, sizeof what, "record %d %s: nothing to shoot", k, b_nm(r));
            expect("items", what, true);
            continue;
        }
        if (fly && before > swings) {
            int left = -1;
            for (int i = 0; i < ch.n_items; ++i)
                if (ch.items[i][0x2E] == fly[0x2E] && ch.items[i][0x34]) left = ch.items[i][0x39];
            snprintf(what, sizeof what, "record %d %s: %d shot(s) use %d of %d", k, b_nm(r), swings, swings, before);
            expect("items", what, left == before - swings, "%d left", left);
        } else if (fly) {
            const bool lands = (t.flags & 0x14) == 0x14 && fly[0x3E] < 0x80;
            snprintf(what, sizeof what, "record %d %s: the last one thrown is %s", k, b_nm(r), lands ? "on the ground" : "gone");
            expect("items", what, ch.n_items == n0 - 1 && (fg->n_thrown > thrown0) == lands, "items %d -> %d, on the ground %d",
                   n0, ch.n_items, fg->n_thrown - thrown0);
        } else {
            snprintf(what, sizeof what, "record %d %s: shoots with nothing used up", k, b_nm(r));
            expect("items", what, ch.n_items == n0, "items %d -> %d", n0, ch.n_items);
        }
    }
    // Backstab weapons (coab CanBackStabTarget, listing ovr014:28D7): none,
    // the club, dagger, broad / long / short sword, drow long sword; a target
    // of size (0xDE & 0x7F) 1 or less
    {
        static const int stab[] = {7, 8, 35, 36, 37, 97};
        for (int type = 1; type < 128; ++type) {
            const items::TypeInfo& t = names->type(type);
            if (t.slot != 0) continue;
            const uint8_t* r = b_plain_of_type(type);
            if (!r) r = b_of_type(type, static_cast<int8_t>(b_rec(0)[0x32]));
            if (!r) continue;
            bool want = false;
            for (int s : stab) want |= s == type;
            party::Character& ch = b_fresh(bm_th);            // the thief
            b_give(ch, r, true);
            rules::recalc(ch, *names, d->facts);
            b_sync(bm_th);
            b_place(bm_th, kT, 1);
            b_target(kT, 50);
            fg->b.f[kT].facing = 2;                            // its back to the thief
            fg->b.f[kT].received = 2;
            const bool got = combat::can_backstab(fg->b, bm_th, kT, names);
            char what[80];
            snprintf(what, sizeof what, "%s: %s", b_nm(r), want ? "backstabs" : "no backstab");
            expect("items", what, got == want);
        }
        party::Character& ch = b_fresh(bm_th);
        rules::recalc(ch, *names, d->facts);
        b_sync(bm_th);
        b_place(bm_th, kT, 1);
        for (int sz : {0x01, 0x81, 0x02, 0x09}) {
            b_target(kT, 50, sz);
            fg->b.f[kT].facing = 2;
            fg->b.f[kT].received = 2;
            const bool want = (sz & 0x7F) <= 1;
            char what[64];
            snprintf(what, sizeof what, "backstab bare-handed, target size byte %02X: %s", sz, want ? "yes" : "no");
            expect("items", what, combat::can_backstab(fg->b, bm_th, kT, names) == want);
        }
    }
}

// The weapons' and armour's own effects in a fight (0x3E = 0x80: effect
// 0x3D while readied): Flame Tongue, Frost Brand, Dragon Slayer (coab
// ovr013 sub_3A17A / sub_3B772 / sub_3B71A: to the roll and the damage by
// the target's monster type 0x11A), the Cloak of Displacement (the first
// attack on its wearer each fight misses: sub_3BA55), the Ring of
// Invisibility (invisible at the start and each round's end: sub_3B27B)
static void b_fight_effects()
{
    using namespace play;
    if (!b_in_fight()) return;
    combat::Battle& b = fg->b;
    struct Special {
        int k;                                  // the record
        int types[4], bonus[4];                 // monster types and the bonus each
    };
    // (records from the player's files: found by their effect byte)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (b_kind(r) != BEffect || names->type(r[0x2E]).slot != 0) continue;
        const int fx = r[0x3D];
        party::Character& ch = b_fresh(kA);
        const int i = b_give(ch, r, false, true);
        rules::recalc(ch, *names, d->facts);
        b_sync(kA);
        b_ready(i);
        b_sync(kA);
        const BVals e = b_expect(ch);
        char what[128];
        snprintf(what, sizeof what, "record %d %s readied: its effect %02X on the wielder", k, b_nm(r), fx);
        expect("items", what, ch.has_affect(static_cast<uint8_t>(fx)));
        for (int mtype = 0; mtype <= 20; ++mtype) {
            int bonus = 0;
            bool slayer = false;
            // (Curse: 0x06 Flame Tongue, 0x4C Frost Brand, 0x4B Dragon Slayer;
            // Pool: 0x06 the same, 0x03 +2 against undead - poolrad_combat_facts.md 2.2)
            const bool known = fx == 6 || (b_pool ? fx == 3 : (fx == 0x4B || fx == 0x4C));
            if (fx == 6) bonus = mtype == 10 ? 1 : (mtype == 9 || mtype == 12) ? 2 : mtype == 4 ? 3 : 0;
            if (b_pool && fx == 3) bonus = mtype == 4 ? 2 : 0;
            if (!b_pool && fx == 0x4C) bonus = mtype == 8 ? 3 : 0;
            if (!b_pool && fx == 0x4B && mtype == 3) {
                bonus = 2;
                slayer = true;
            }
            if (!known) continue;
            const int ac = e.hit + 10;
            b_place(kA, kT, 1);
            b_target(kT, ac, 1, mtype);
            const combat::Attack m = b_swing(kA, kT, {9 - bonus});
            b_target(kT, ac, 1, mtype);
            const combat::Attack h = b_swing(kA, kT, {10 - bonus, 99, 99, 99, 99});
            const int sdmg = ov("str_dmg", b_str_group(ch.rec[0x11], ch.rec[0x1C]));
            const int want = slayer ? 12 * 3 + 4 + sdmg : e.dice * e.sides + e.dmg + bonus;
            snprintf(what, sizeof what, "record %d %s vs monster type %d: %+d to hit, damage %d at most", k, b_nm(r), mtype,
                     bonus, want);
            expect("items", what, !m.hits[0].hit && h.hits[0].hit && h.hits[0].damage == want, "%d: %s, %d: %s %d",
                   9 - bonus, m.hits[0].hit ? "hit" : "miss", 10 - bonus, h.hits[0].hit ? "hit" : "miss", h.hits[0].damage);
        }
    }
    // Displacement: the first attack on the wearer each fight misses, even a 20
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (b_kind(r) != BEffect || r[0x3D] != 0x59) continue;
        party::Character& ch = b_fresh(kA);
        const int i = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        b_sync(kA);
        b_ready(i);
        b_sync(kA);
        b_place(kT, kA, 1);
        b.f[kA].rec[0x196] = 1;
        combat::battle_start(b);
        const combat::Attack first = b_swing(kT, kA, {20, 1, 1});
        const combat::Attack second = b_swing(kT, kA, {20, 1, 1});
        char what[96];
        snprintf(what, sizeof what, "record %d %s: the first attack on its wearer misses (a 20), the next hits", k, b_nm(r));
        expect("items", what, first.n == 1 && !first.hits[0].hit && second.n == 1 && second.hits[0].hit, "first %s, second %s",
               first.hits[0].hit ? "hit" : "miss", second.hits[0].hit ? "hit" : "miss");
    }
    // Ring of Invisibility: invisible when the fight starts and again at each round's end
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (b_kind(r) != BEffect || r[0x3D] != 0x38) continue;
        party::Character& ch = b_fresh(kA);
        const int i = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        b_sync(kA);
        b_ready(i);
        b_sync(kA);
        b_place(kA, kT, 1);
        combat::battle_start(b);
        const uint8_t inv = b.fx ? b.fx->invisible : 0;
        const bool at_start = ch.has_affect(inv);
        b_target(kT, 50);
        b_swing(kA, kT, {20, 1});
        const bool after_attack = ch.has_affect(inv);
        combat::tick(b);
        const bool next_round = ch.has_affect(inv);
        char what[96];
        snprintf(what, sizeof what, "record %d %s: invisible at the start, not after attacking, again after the round", k,
                 b_nm(r));
        expect("items", what, at_start && !after_attack && next_round, "%d %d %d", at_start, after_attack, next_round);
    }
    // The elves' stealth (effect 0x48, both games' handler: Pool's Cloak of
    // Elvenkind): invisible at the start and after each round on 95 or less
    // of a d100, not on 96 or more
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (b_kind(r) != BEffect || r[0x3D] != 0x48) continue;
        party::Character& ch = b_fresh(kA);
        const int i = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        b_sync(kA);
        b_ready(i);
        b_sync(kA);
        b_place(kA, kT, 1);
        const uint8_t inv = b.fx ? b.fx->invisible : 0;
        dice_faces({95});
        combat::battle_start(b, &rng);
        const bool on95 = ch.has_affect(inv);
        b_target(kT, 50);
        b_swing(kA, kT, {20, 1});
        dice_faces({96});
        combat::tick(b);
        const bool on96 = ch.has_affect(inv);
        dice_faces({95});
        combat::tick(b);
        const bool again = ch.has_affect(inv);
        dice_usual();
        char what[112];
        snprintf(what, sizeof what, "record %d %s: invisible at the start on 95, not on 96 after attacking, again on 95", k,
                 b_nm(r));
        expect("items", what, on95 && !on96 && again, "%d %d %d", on95, on96, again);
    }
}

// ---- 6. Items that work while readied (0x3E 0x80 + code, 0x3D its
// parameter; curse_finish_facts 5, coab CalcStatBonuses / calc_items_effects):
// the stats each gives from the OWN stats, the effect while readied, the
// Ring of Wizardry's spell slots, the thief-skill items, cursed items and
// Remove Curse
struct BStats {
    int s[6];
    int str00;
};

// What the original makes of the own stats and the readied items (no spells)
static BStats b_stats_expect(const party::Character& c)
{
    const uint8_t* r = c.rec;
    BStats e{};
    int str = r[0x10], s00 = r[0x1D], forced_str = -1;
    int own[6];
    for (int i = 0; i < 6; ++i) own[i] = r[0x10 + 2 * i];
    int v[6] = {own[0], own[1], own[2], own[3], own[4], own[5]};
    int forced_int = -1;
    for (int i = 0; i < c.n_items; ++i) {
        const uint8_t* it = c.items[i];
        if (!it[0x34]) continue;
        const BKind kind = b_kind(it);
        const int p = it[0x3D];
        int b = 0, b00 = 0;
        if (kind == BGirdle && p <= 6) {
            b = p == 0 ? 18 : 18 + p;
            b00 = p == 0 ? 100 : 0;
        }
        if (kind == BOgre) {                        // (Pool: 18/00 through its strength effect)
            b = 18;
            b00 = 100;
        }
        if (kind == BIoun && p == 0 && own[0] < 18) b = own[0] + 1;
        if (b > str || (b == 18 && b00 > s00)) {
            str = b;
            s00 = b00;
        }
        if (kind == BPowerless) {
            forced_str = 3;
            forced_int = 3;
        }
        if (kind == BStupid) forced_int = 7;        // (the last such item's counts)
        if (kind == BIoun && p >= 1 && p <= 5 && own[p] < 18) ++v[p];
        if (kind == BDex) v[3] += own[3] <= 6 ? 4 : own[3] <= 13 ? 2 : 1;
        if (kind == BFumble) v[3] -= 2;
        if (kind == BDwarf) {
            ++v[4];
            --v[5];
        }
    }
    e.s[0] = forced_str >= 0 ? 3 : str;
    e.str00 = forced_str >= 0 ? 0 : s00;
    for (int i = 1; i < 6; ++i) e.s[i] = v[i] < 0 ? 0 : v[i];
    if (forced_int >= 0) e.s[1] = forced_int;
    return e;
}

static void b_worn_items()
{
    using namespace play;
    // The stat items, readied by characters of different own stats
    struct Own {
        int str, s00, in, wis, dex, con, cha;
    };
    const Own owns[] = {{12, 0, 12, 12, 12, 12, 12}, {17, 0, 17, 17, 17, 17, 17}, {18, 50, 18, 18, 18, 18, 18},
                        {5, 0, 5, 5, 5, 15, 5}, {18, 100, 14, 14, 14, 16, 14}};
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (r[0x3E] <= 0x80) continue;
        const int code = r[0x3E] & 0x7F;
        const BKind kind = b_kind(r);
        // (the effect / wizardry / thievery items elsewhere; the keyed and the
        // boulder's own rules: behave pool)
        if (kind == BEffect || kind == BWiz || kind == BThief || kind == BAlign || kind == BNeedGiant || kind == BNone) continue;
        for (const Own& o : owns) {
            party::Character& ch = b_fresh(kA);
            uint8_t* q = ch.rec;
            q[0x10] = q[0x11] = static_cast<uint8_t>(o.str);
            q[0x1D] = q[0x1C] = static_cast<uint8_t>(o.s00);
            const int ov6[6] = {o.str, o.in, o.wis, o.dex, o.con, o.cha};
            for (int s = 1; s < 6; ++s) q[0x10 + 2 * s] = q[0x11 + 2 * s] = static_cast<uint8_t>(ov6[s]);
            q[0x19] = static_cast<uint8_t>(o.con + 1);     // (a Constitution change: the HP rebuilt for this one)
            ch.rec[0x12B] = 0xFF;
            const int i = b_give(ch, r, false, true);
            rules::recalc(ch, *names, d->facts);
            const int hp0 = ch.hp_max();
            const bool on = b_ready(i);
            const BStats e = b_stats_expect(ch);
            char what[128];
            snprintf(what, sizeof what, "record %d %s (code %d %d) on own %d/%d %d %d %d %d %d", k, b_nm(r), code, r[0x3D], o.str,
                     o.s00, o.in, o.wis, o.dex, o.con, o.cha);
            const bool ok = on && ch.rec[0x11] == e.s[0] && ch.rec[0x1C] == e.str00 && ch.rec[0x13] == e.s[1] &&
                            ch.rec[0x15] == e.s[2] && ch.rec[0x17] == e.s[3] && ch.rec[0x19] == e.s[4] && ch.rec[0x1B] == e.s[5];
            expect("items", what, ok, "in use %d/%d %d %d %d %d %d, expected %d/%d %d %d %d %d %d", ch.rec[0x11], ch.rec[0x1C],
                   ch.rec[0x13], ch.rec[0x15], ch.rec[0x17], ch.rec[0x19], ch.rec[0x1B], e.s[0], e.str00, e.s[1], e.s[2], e.s[3],
                   e.s[4], e.s[5]);
            // Constitution's hit points follow (a paladin of 5th level: 15-19 x (Con - 14) a level)
            if (ch.rec[0x19] != o.con && o.con >= 15 && ch.rec[0x19] <= 19) {
                const int want = hp0 + 5 * (ch.rec[0x19] - o.con);
                snprintf(what, sizeof what, "record %d %s: Con %d -> %d, HP max %d -> %d", k, b_nm(r), o.con, ch.rec[0x19], hp0,
                         want);
                expect("items", what, ch.hp_max() == want, "%d", ch.hp_max());
            }
            // Put away (not cursed): all as before
            if (!r[0x36]) {
                b_ready(i);
                snprintf(what, sizeof what, "record %d %s put away: own stats again", k, b_nm(r));
                expect("items", what,
                       ch.rec[0x11] == o.str && ch.rec[0x1C] == o.s00 && ch.rec[0x17] == o.dex && ch.rec[0x19] == o.con &&
                           ch.hp_max() == hp0,
                       "Str %d/%d Dex %d Con %d HP %d", ch.rec[0x11], ch.rec[0x1C], ch.rec[0x17], ch.rec[0x19], ch.hp_max());
            }
        }
    }
    // Two stat items together (an ioun stone +1 Dex and gauntlets of dexterity: both count)
    {
        const uint8_t* ioun = nullptr;
        const uint8_t* gaunt = nullptr;
        for (int k = 0; k < b_n_items; ++k) {
            if (b_kind(b_rec(k)) == BIoun && b_rec(k)[0x3D] == 3) ioun = b_rec(k);
            if (b_kind(b_rec(k)) == BDex) gaunt = b_rec(k);
        }
        if (ioun && gaunt) {
            party::Character& ch = b_fresh(kA);
            ch.rec[0x16] = ch.rec[0x17] = 12;
            const int a = b_give(ch, ioun, false), b2 = b_give(ch, gaunt, false);
            rules::recalc(ch, *names, d->facts);
            b_ready(a);
            b_ready(b2);
            expect("items", "Ioun Stone (Dex) + Gauntlets of Dexterity on Dex 12: 15", ch.rec[0x17] == 15, "%d", ch.rec[0x17]);
            expect("items", "... and its AC: Dex 15 = 1 better", ch.rec[0x19A] == 51, "AC %d", ch.ac());
        }
    }
    // Effect items (code 0): the effect 0x3D while readied (data 0xFF, no time), gone when put away
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (b_kind(r) != BEffect) continue;
        party::Character& ch = b_fresh(kA);
        ch.rec[0x12B] = 0xFF;
        const int i = b_give(ch, r, false, true);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        const int fx = r[0x3D];
        const int data = b_pool ? 12 : 0xFF;        // (Pool's generic handler: data 12)
        int at = -1;
        for (int j = 0; j < ch.n_affects; ++j)
            if (ch.affects[j][0] == fx) at = j;
        char what[96];
        snprintf(what, sizeof what, "record %d %s readied: effect %02X, data %02X, no time", k, b_nm(r), fx, data);
        expect("items", what, at >= 0 && ch.affects[at][3] == data && ch.affects[at][1] == 0 && ch.affects[at][2] == 0,
               "found %d", at);
        const bool still = b_ready(i);
        bool gone = true;
        for (int j = 0; j < ch.n_affects; ++j)
            if (ch.affects[j][0] == fx) gone = false;
        if (r[0x36]) {
            snprintf(what, sizeof what, "record %d %s (cursed): stays readied, the effect stays", k, b_nm(r));
            expect("items", what, still && !gone);
        } else {
            snprintf(what, sizeof what, "record %d %s put away: the effect goes", k, b_nm(r));
            expect("items", what, !still && gone);
        }
    }
    // Remove Curse (the temple): the first cursed item comes off (still cursed), its effect / stats with it
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!r[0x36]) continue;
        party::Character& ch = b_fresh(kA);
        ch.rec[0x12B] = 0xFF;
        const uint8_t s0[6] = {ch.rec[0x11], ch.rec[0x13], ch.rec[0x15], ch.rec[0x17], ch.rec[0x19], ch.rec[0x1B]};
        const int i = b_give(ch, r, false, true);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        rules::CureFacts cf{};
        rules::apply_cure(ch, rules::kRemoveCurse, cf, rng);
        rules::recalc(ch, *names, d->facts);
        bool fx_gone = true;
        if (b_kind(r) == BEffect)
            for (int j = 0; j < ch.n_affects; ++j)
                if (ch.affects[j][0] == r[0x3D]) fx_gone = false;
        const bool stats = ch.rec[0x11] == s0[0] && ch.rec[0x13] == s0[1] && ch.rec[0x17] == s0[3];
        char what[96];
        snprintf(what, sizeof what, "record %d %s: Remove Curse puts it away (still cursed), its workings gone", k, b_nm(r));
        expect("items", what, ch.items[i][0x34] == 0 && ch.items[i][0x36] && fx_gone && stats, "readied %d cursed %d fx gone %d stats %d",
               ch.items[i][0x34], ch.items[i][0x36], fx_gone, stats);
    }
    // Ring of Wizardry (code 1): magic-user slots of levels 1-3 doubled; put
    // away, rebuilt from the table, and the spells past them forgotten (later first)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (b_kind(r) != BWiz) continue;
        party::Character& ch = b_fresh(bm_mu);                      // the magic-user
        uint8_t slots[5];
        memcpy(slots, &ch.rec[0x137], 5);
        const int i = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        bool ok = true;
        for (int l = 0; l < 5; ++l) ok &= ch.rec[0x137 + l] == (l < 3 ? slots[l] * 2 : slots[l]);
        char what[96];
        snprintf(what, sizeof what, "record %d %s readied: magic-user slots 1-3 doubled (%d %d %d %d %d)", k, b_nm(r), slots[0],
                 slots[1], slots[2], slots[3], slots[4]);
        expect("items", what, ok, "%d %d %d %d %d", ch.rec[0x137], ch.rec[0x138], ch.rec[0x139], ch.rec[0x13A], ch.rec[0x13B]);
        b_ready(i);
        ok = true;
        for (int l = 0; l < 5; ++l) ok &= ch.rec[0x137 + l] == slots[l];
        snprintf(what, sizeof what, "record %d %s put away: the slots as before", k, b_nm(r));
        expect("items", what, ok, "%d %d %d %d %d", ch.rec[0x137], ch.rec[0x138], ch.rec[0x139], ch.rec[0x13A], ch.rec[0x13B]);
        // Memorized past the slots: the extra ones forgotten when it comes off
        b_ready(i);
        if (!load_magic()) continue;
        int first = -1;
        for (int s = 1; s <= 0x38 && first < 0; ++s)
            if (mrules->tables.spell_class(s) == 2 && spells::entry(mrules->tables, s).level == 1) first = s;
        int n1 = 0;
        for (int m = 0; m < magic::kListSize; ++m) ch.rec[magic::kListAt + m] = 0;
        for (int m = 0; m < slots[0] * 2; ++m) ch.rec[magic::kListAt + m] = static_cast<uint8_t>(first), ++n1;
        b_ready(i);
        int left = 0;
        for (int m = 0; m < magic::kListSize; ++m) left += ch.rec[magic::kListAt + m] == first;
        snprintf(what, sizeof what, "record %d %s put away with %d 1st level spells memorized: %d kept (the first ones)", k,
                 b_nm(r), n1, slots[0]);
        bool first_kept = true;
        for (int m = 0; m < slots[0]; ++m) first_kept &= ch.rec[magic::kListAt + m] == first;
        expect("items", what, left == slots[0] && first_kept, "%d kept", left);
    }
    // The thief-skill items (code 2 Gauntlets of Dexterity, 11 Gloves of
    // Thievery; the first readied one counts): gauntlets - below 4th level the
    // skills of 4th, else +10 each; gloves - skill 1 at 5th level (or +5),
    // skill 2 at 7th (or +5), the +5 carried on to skills 3-8
    if (load_magic()) {
        for (int k = 0; k < b_n_items; ++k) {
            const uint8_t* r = b_rec(k);
            const int code = b_kind(r) == BDex ? 2 : b_kind(r) == BThief ? 11 : 0;
            if (!code) continue;
            for (int lvl : {1, 3, 4, 5, 6, 7, 9}) {
                party::Character& ch = b_fresh(bm_th);              // the thief
                ch.rec[0x109 + 6] = static_cast<uint8_t>(lvl);
                ch.rec[0x16] = ch.rec[0x17] = 12;                  // (Dex 12: gauntlets make it 14)
                const int i = b_give(ch, r, false);
                rules::recalc(ch, *names, d->facts);
                b_ready(i);
                uint8_t got[8];
                memcpy(got, &ch.rec[0xEA], 8);
                // The skills without the item at a given level (Dex as the item leaves it)
                auto base = [&](int at_level, uint8_t* out) {
                    party::Character t = ch;
                    t.items[i][0x34] = 0;
                    t.rec[0x109 + 6] = static_cast<uint8_t>(at_level);
                    classes::thief_skills(t, mrules->tables);
                    memcpy(out, &t.rec[0xEA], 8);
                };
                uint8_t want[8];
                if (code == 2) {
                    base(lvl < 4 ? 4 : lvl, want);
                    if (lvl >= 4)
                        for (int s = 0; s < 8; ++s) want[s] = static_cast<uint8_t>(want[s] + 10);
                } else {
                    uint8_t b0[8], b5[8], b7[8];
                    base(lvl, b0);
                    base(lvl < 5 ? 5 : lvl, b5);
                    base(lvl < 7 ? 7 : lvl, b7);
                    const int carry1 = lvl >= 5 ? 5 : 0, carry2 = lvl >= 7 ? 5 : 0;
                    want[0] = static_cast<uint8_t>(b5[0] + carry1);
                    want[1] = static_cast<uint8_t>(b7[1] + carry2);
                    for (int s = 2; s < 8; ++s) want[s] = static_cast<uint8_t>(b0[s] + carry2);
                }
                char what[112];
                snprintf(what, sizeof what, "record %d %s on a %d-level thief: skills %d %d %d %d %d %d %d %d", k, b_nm(r), lvl,
                         want[0], want[1], want[2], want[3], want[4], want[5], want[6], want[7]);
                expect("items", what, memcmp(got, want, 8) == 0, "%d %d %d %d %d %d %d %d", got[0], got[1], got[2], got[3], got[4],
                       got[5], got[6], got[7]);
            }
        }
    }
}

// ---- 7. Using items (item_use_facts 1-2, curse_finish_facts 10): which
// items are used and how, charges, the item spells outside fights, the
// caster level 6, scrolls (who reads them, the spell off the scroll)
static magic::Scrolls b_scrolls()
{
    return play::scroll_facts();
}

// Uses item i of member `who` on the items screen (outside fights), drives
// the cast to its end: "Cast Spell on whom" -> `target`, "Use it?" -> `answer`
static void b_use(int who, int i, int target, char answer)
{
    using namespace play;
    pt->selected = who;
    view_from = Screen::Game;
    screen = Screen::Items;
    use_item(i, C);
    for (int k = 0; k < 400; ++k) {
        if (screen == Screen::YesNo) {
            tap_word(answer);
            continue;
        }
        if (screen != Screen::Cast) break;
        if (cr.stage == CastRun::Whom) {
            pt->selected = target;
            cr.target = target;
            do_cast(C);
            continue;
        }
        g_now += 5000;
        play::tick(g_now, C);
    }
    note_until = 0;
    screen = Screen::Game;
    load_magic();                               // (the use may have let the tables go: back for the checks)
}

static void b_use_items()
{
    using namespace play;
    if (!boot()) return;
    if (!load_magic()) {
        expect("items", "the spell tables", false);
        return;
    }
    const magic::Scrolls sc = b_scrolls();
    // Which are used: scrolls (slots 11-13) by their spell list; the rest when
    // 0x3D is a spell and 0x3E under 0x80 (coab UseMagicItem, Item.IsScroll)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        const int slot = names->type(r[0x2E]).slot;
        const bool scroll = slot >= 11 && slot <= 13;
        const bool usable = !scroll && r[0x3D] != 0 && r[0x3E] < 0x80;
        char what[96];
        snprintf(what, sizeof what, "record %d %s: %s", k, b_nm(r), scroll ? "a scroll" : usable ? "used: its spell" : "not used");
        expect("items", what,
               magic::is_scroll(sc, r) == scroll && magic::usable(sc, r) == usable && (!usable || magic::item_spell(r, b_pool) == b_spell(r)));
    }
    // (what the program's spell table says of each item's spell - for the report)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::usable(sc, r)) continue;
        const int s = b_spell(r);
        const spells::Entry e = spells::entry(play::mrules->tables, s);
        printf("NOTE items: record %d %s: spell %02X class %d targets %d when %d affect %02X lasts %d+%d/level aim %d save %d/%d\n",
               k, b_nm(r), s, play::mrules->tables.spell_class(s), e.targets, e.when, e.affect, e.lasts, e.lasts_level, e.aim,
               e.on_save, e.save);
    }
    // The caster level of an item's spell: 6 (the monsters' own spells: the
    // user's - 6 without casting levels, else 12)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::usable(sc, r)) continue;
        const int s = b_spell(r);
        for (int who : {kA, bm_cl, bm_mu}) {
            const party::Character& ch = b_saved[who];
            const bool casts = ch.level(classes::Cleric) || ch.level(classes::MagicUser) || ch.level(classes::Paladin) >= 9 ||
                               ch.level(classes::Ranger) >= 8;
            const int want = play::mrules->tables.spell_class(s) != 3 ? 6 : casts ? 12 : 6;
            char what[96];
            snprintf(what, sizeof what, "record %d %s (spell %02X) used by member %d: caster level %d", k, b_nm(r), s, who, want);
            expect("items", what, spells::item_power(ch, play::mrules->tables, s) == want && combat::power_of(ch.rec, play::mrules->tables, s, true) == want, "%d / %d",
                   spells::item_power(ch, play::mrules->tables, s), combat::power_of(ch.rec, play::mrules->tables, s, true));
        }
    }
    // Every usable record used outside a fight
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::usable(sc, r)) continue;
        const int s = b_spell(r);
        const spells::Entry e = spells::entry(play::mrules->tables, s);
        const items::TypeInfo& t = names->type(r[0x2E]);
        const int who = (t.classes & ov("class_bits", 2)) ? kA : bm_mu;   // (a magic-user for the magic-users' wands)
        char what[128];
        // Not readied: "Must be Readied", nothing used
        {
            party::Character& ch = b_fresh(who);
            const int i = b_give(ch, r, false);
            rules::recalc(ch, *names, d->facts);
            b_use(who, i, who, 'N');
            snprintf(what, sizeof what, "record %d %s not readied: nothing used", k, b_nm(r));
            expect("items", what, ch.n_items == 1 && memcmp(ch.items[0], r, 0x34) == 0 && ch.items[0][0x3C] == r[0x3C]);
        }
        party::Character& ch = b_fresh(who);
        if (ch.hp_max() < 30) ch.rec[0x78] = 30;        // (room for a potion's healing: Pool's saved fighter has 7)
        ch.rec[0x1A4] = static_cast<uint8_t>(ch.hp_max() - 20);
        const int hp0 = ch.hp();
        const int i = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        if (!b_ready(i)) {
            snprintf(what, sizeof what, "record %d %s readied by member %d", k, b_nm(r), who);
            expect("items", what, false);
            continue;
        }
        const int n_aff0 = ch.n_affects;
        const int charges = r[0x3C], count = r[0x39];
        // The use taken: one of a pile, else a charge (0: never runs out), gone at 0
        auto used_ok = [&]() {
            if (count > 1) return ch.n_items == 1 && ch.items[0][0x39] == count - 1 && ch.items[0][0x3C] == charges;
            if (charges == 0) return ch.n_items == 1 && ch.items[0][0x3C] == 0;
            if (charges == 1) return ch.n_items == 0;
            return ch.n_items == 1 && ch.items[0][0x3C] == charges - 1;
        };
        if (e.targets == spells::kCombat) {
            // "That Item is a combat-only item... Use it?" No: nothing; Yes: a use for nothing
            b_use(who, i, who, 'N');
            snprintf(what, sizeof what, "record %d %s (spell %02X, fights only) outside a fight, Use it? No: kept", k, b_nm(r), s);
            expect("items", what, ch.n_items == 1 && ch.items[0][0x3C] == charges, "items %d charges %d", ch.n_items,
                   ch.n_items ? ch.items[0][0x3C] : -1);
            b_use(who, i, who, 'Y');
            snprintf(what, sizeof what, "record %d %s outside a fight, Use it? Yes: a use gone, nothing done", k, b_nm(r));
            expect("items", what, used_ok() && ch.hp() == hp0 && ch.n_affects == n_aff0, "items %d charges %d hp %d effects %d",
                   ch.n_items, ch.n_items ? ch.items[0][0x3C] : -1, ch.hp(), ch.n_affects);
            continue;
        }
        // Cast: the dice forced to their middle-ish faces
        if (s == 0x39) {
            dice_faces({4, 4, 4, 4, 4});
        } else if (s == 0x3B) {
            dice_faces({3});
        } else if (s == 0x3F) {
            dice_faces({5, 5, 5, 5, 5, 5, 5, 5});
        } else if (s == 0x3E || s == 0x63) {
            dice_faces({4, 4});
        }
        b_use(who, i, who, 'Y');
        dice_usual();
        snprintf(what, sizeof what, "record %d %s (spell %02X) used: %s", k, b_nm(r), s,
                 count > 1 ? "one of the pile" : charges == 0 ? "never runs out" : charges == 1 ? "gone" : "a charge less");
        expect("items", what, used_ok(), "items %d count %d charges %d", ch.n_items, ch.n_items ? ch.items[0][0x39] : -1,
               ch.n_items ? ch.items[0][0x3C] : -1);
        const int pw = spells::item_power(ch, play::mrules->tables, s);
        auto aff_time = [&](const party::Character& c, int type) {
            for (int j = 0; j < c.n_affects; ++j)
                if (c.affects[j][0] == type) return c.affects[j][1] | c.affects[j][2] << 8;
            return -1;
        };
        switch (s) {
        case 0x3E:
        case 0x63:
            snprintf(what, sizeof what, "record %d %s: heals 2d4 + 2 (4 + 4 + 2 = 10)", k, b_nm(r));
            expect("items", what, ch.hp() == hp0 + 10, "HP %d -> %d", hp0, ch.hp());
            break;
        case 0x39:
            snprintf(what, sizeof what, "record %d %s: hasted for 5d4 (20) minutes", k, b_nm(r));
            expect("items", what, aff_time(ch, e.affect) == 20, "effect %02X time %d", e.affect, aff_time(ch, e.affect));
            break;
        case 0x3B: {
            snprintf(what, sizeof what, "record %d %s: Strength 21 for d4 x 10 + 40 (70) minutes", k, b_nm(r));
            const int g = d->prof->magic.facts.giant;
            expect("items", what, ch.rec[0x11] == 21 && aff_time(ch, g) == 70, "Str %d/%d, effect %02X time %d", ch.rec[0x11],
                   ch.rec[0x1C], g, aff_time(ch, g));
            break;
        }
        case 0x3F: {
            bool all = true;
            for (int m = 0; m < pt->count; ++m) all &= aff_time(pt->m[m], e.affect) == 150;
            snprintf(what, sizeof what, "record %d %s: the whole party invisible for (d10 + 10) x 10 (150) minutes", k, b_nm(r));
            expect("items", what, all, "member 0 time %d", aff_time(pt->m[0], e.affect));
            for (int m = 0; m < pt->count; ++m) pt->m[m].n_affects = 0;
            break;
        }
        case 0x43:
            if (b_pool) {                               // Pool's Manual of Bodily Health: "is Reading" for a day
                snprintf(what, sizeof what, "record %d %s: reading for a day (effect %02X, 1440 minutes)", k, b_nm(r),
                         e.affect);
                expect("items", what, aff_time(ch, e.affect) == 1440, "time %d", aff_time(ch, e.affect));
                break;
            }
            [[fallthrough]];
        default: {
            const int want = e.lasts + e.lasts_level * pw;
            snprintf(what, sizeof what, "record %d %s: effect %02X for %d minutes (the table at level %d)", k, b_nm(r), e.affect,
                     want, pw);
            expect("items", what, e.affect == 0 || aff_time(ch, e.affect) == want, "time %d", aff_time(ch, e.affect));
        }
        }
    }
    // Speed on the slowed: the slow is cured, nothing else
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::usable(sc, r) || b_spell(r) != 0x39) continue;
        party::Character& ch = b_fresh(kA);
        const uint8_t slow = d->prof->magic.facts.slow;
        spells::add_affect(ch, slow, 10, 0, true);
        const int i = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        b_use(kA, i, kA, 'Y');
        const int haste = spells::entry(play::mrules->tables, 0x39).affect;
        char what[96];
        snprintf(what, sizeof what, "record %d %s on the slowed: the slow cured, no haste", k, b_nm(r));
        expect("items", what, !ch.has_affect(slow) && !ch.has_affect(static_cast<uint8_t>(haste)));
        break;
    }
    // Giant strength on someone already stronger than 21: no change
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::usable(sc, r) || b_spell(r) != 0x3B) continue;
        party::Character& ch = b_fresh(kA);
        ch.rec[0x10] = ch.rec[0x11] = 22;
        const int i = b_give(ch, r, false);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        b_use(kA, i, kA, 'Y');
        char what[96];
        snprintf(what, sizeof what, "record %d %s on Str 22: stays 22", k, b_nm(r));
        expect("items", what, ch.rec[0x11] == 22, "%d", ch.rec[0x11]);
        break;
    }
    // Scrolls: who can read them (magic-users and clerics either kind; a
    // thief of 10th level on d100 75 or less; anyone else "oops!")
    {
        party::Character t = b_saved[kA];
        if (!b_pool) expect("items", "a paladin of 5th level can't read a scroll", !magic::reads_scroll(t, 1));
        t = b_saved[bm_mu];
        expect("items", "a magic-user reads scrolls", magic::reads_scroll(t, 100));
        t = b_saved[bm_cl];
        expect("items", "a cleric reads scrolls", magic::reads_scroll(t, 100));
        t = b_saved[bm_th];
        t.rec[0x10F] = 9;
        expect("items", "a 9th level thief can't", !magic::reads_scroll(t, 1));
        t.rec[0x10F] = 10;
        expect("items", "a 10th level thief: d100 75 reads, 76 doesn't", magic::reads_scroll(t, 75) && !magic::reads_scroll(t, 76));
    }
    // The spell read off a scroll: the last line holding it emptied, the
    // count word down one; under "With 1 Spell" the scroll is gone
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::is_scroll(sc, r)) continue;
        party::Character& ch = b_fresh(bm_mu);
        int i = b_give(ch, r, true);
        ch.items[i][0x35] = 0;
        int n = 0;
        for (int l = 0; l < 3; ++l) n += r[0x3C + l] != 0;
        bool ok = true;
        int left = n;
        for (int l = 2; l >= 0 && ok; --l) {
            const int sp = ch.n_items ? ch.items[0][0x3C + l] & 0x7F : 0;
            if (!sp) continue;
            const int word = ch.items[0][0x30];
            magic::scroll_used(ch, sc, 0, sp);
            --left;
            if (word - 1 < sc.one_spell) ok &= ch.n_items == 0;
            else ok &= ch.n_items == 1 && ch.items[0][0x30] == word - 1 && ch.items[0][0x3C + l] == 0;
        }
        char what[96];
        snprintf(what, sizeof what, "record %d %s: each spell read goes, then the scroll", k, b_nm(r));
        expect("items", what, ok && (ch.n_items == 0 || r[0x30] - n >= sc.one_spell), "items %d", ch.n_items);
        (void)i;
    }
    // Only an identified scroll lists its spells; a cleric reads a clerics' scroll (it's known then)
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::is_scroll(sc, r) || !r[0x35]) continue;
        party::Character& ch = b_fresh(bm_mu);
        b_give(ch, r, true);
        uint8_t ids[3];
        const int n = magic::scroll_list(ch, play::mrules->tables, sc, 0, ids, 3);
        char what[96];
        snprintf(what, sizeof what, "record %d %s (unidentified): no spells to read for a magic-user", k, b_nm(r));
        expect("items", what, n == 0, "%d", n);
    }
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (!magic::is_scroll(sc, r) || names->type(r[0x2E]).slot != 12) continue;
        party::Character& ch = b_fresh(bm_cl);
        b_give(ch, r, true);
        ch.items[0][0x35] = 6;
        uint8_t ids[3];
        const int n = magic::scroll_list(ch, play::mrules->tables, sc, 0, ids, 3);
        char what[96];
        snprintf(what, sizeof what, "record %d %s (hidden) read by a cleric: identified, its spells listed", k, b_nm(r));
        expect("items", what, n > 0 && ch.items[0][0x35] == 0, "%d spells, hidden %d", n, ch.items[0][0x35]);
    }
}

// ---- 8. Using an item in a fight (item_use_facts 2-3): the spell goes off
// at once (no casting time), the use is taken, the turn is over; the wands'
// and the necklace's damage on the monsters
static int b_fu_stage = 0, b_fu_item = -1, b_fu_round = -1;
static int b_fu_hp[combat::kMaxFighters];
static const uint8_t* b_fu_rec = nullptr;
static int b_fu_charges = 0;

static int b_fu_menu()
{
    using namespace play;
    combat::Fighter& f = fg->b.f[fg->cur];
    if (fg->cur != kA) return 0;                    // (the others go Quick)
    if (b_fu_stage == 0) {
        // (everyone standing kept so: Pool's inn fight dies to one Fireball, and
        // the user, in the blast of its own ball there, went down - the test over)
        for (int i = 0; i < fg->b.n; ++i)
            if (fg->b.f[i].up()) fg->b.f[i].rec[0x78] = fg->b.f[i].rec[0x1A4] = 200;
        for (int i = 0; i < fg->b.n; ++i) b_fu_hp[i] = fg->b.f[i].hp();
        b_fu_round = fg->b.round;
        b_fu_stage = 1;
        if (!load_magic()) return 0;
        fight_use(b_fu_item, magic::item_spell(pt->m[kA].items[b_fu_item], b_pool), false, C);
        return 1;
    }
    if (b_fu_stage == 1) {
        b_fu_stage = 2;
        const uint8_t* r = b_fu_rec;
        const int s = b_spell(r);
        party::Character& ch = pt->m[kA];
        char what[128];
        snprintf(what, sizeof what, "record %d %s used in a fight: the turn over at once (next turn a round later)",
                 static_cast<int>((r - b_rec(0)) / 63), b_nm(r));
        expect("items", what, fg->b.round > b_fu_round, "used in round %d, back in round %d", b_fu_round, fg->b.round);
        const bool used = r[0x3C] == 1 ? ch.n_items == 0 : ch.n_items == 1 && ch.items[0][0x3C] == b_fu_charges - 1;
        snprintf(what, sizeof what, "record %d %s used in a fight: a charge gone", static_cast<int>((r - b_rec(0)) / 63), b_nm(r));
        expect("items", what, used, "items %d charges %d", ch.n_items, ch.n_items ? ch.items[0][0x3C] : -1);
        // The damage on the monsters (only this item hurt them)
        int hurt = 0, lo = 9999, hi = 0;
        for (int i = 0; i < fg->b.n; ++i) {
            if (fg->b.f[i].member >= 0) continue;
            const int lost = b_fu_hp[i] - fg->b.f[i].hp();
            if (lost <= 0) continue;
            ++hurt;
            lo = lost < lo ? lost : lo;
            hi = lost > hi ? lost : hi;
        }
        int want_lo = 0, want_hi = 0, most = 99;
        bool down_ok = true;                         // (a monster may go down: its loss is then its HP)
        switch (s) {
        case 0x41: want_lo = 4; want_hi = 10; most = 1; break;          // 2d4 + 2, one target, no save
        case 0x2F: want_lo = 3; want_hi = 36; break;                    // 6d6 (level 6), half on a save
        case 0x33: want_lo = 3; want_hi = 72; break;                    // the same - twice for one the bolt's
                                                                        // return leg hits again (curse_finish_facts 2.4)
        case 0x40: want_lo = 1; want_hi = 42; break;                    // 3, 5 or 7 d6, half on a save
        case 0x57: want_lo = 3; want_hi = 30; break;                    // 3d10
        case 0x62: want_lo = 0; want_hi = 0; most = 0; break;          // defoliation: only plants hurt
        default: down_ok = false; most = 99; break;
        }
        if (s == 0x3D) {
            const uint8_t para = d->prof->fight.facts.mon.paralyzed;
            int held = 0;
            for (int i = 0; i < fg->b.n; ++i)
                if (fg->b.f[i].member < 0 && fg->b.f[i].has(para)) ++held;
            snprintf(what, sizeof what, "record %d %s: no damage, the paralysed (%d) by their saves", static_cast<int>((r - b_rec(0)) / 63),
                     b_nm(r), held);
            expect("items", what, hurt == 0);
        } else if (down_ok) {
            snprintf(what, sizeof what, "record %d %s (spell %02X): %d hurt (at most %d), %d-%d each (%d-%d)",
                     static_cast<int>((r - b_rec(0)) / 63), b_nm(r), s, hurt, most, hurt ? lo : 0, hi, want_lo, want_hi);
            const bool ok = hurt <= most && (most == 0 || hurt >= 1) && (hurt == 0 || (hi <= want_hi && lo >= (want_lo > 16 ? 1 : 1)));
            expect("items", what, ok && (hurt == 0 || lo >= 1));
        }
    }
    return 0;
}

static void b_fight_use()
{
    using namespace play;
    if (!boot()) return;
    std::vector<int> usable;                        // (found before the boots: each one frees the names)
    {
        const magic::Scrolls sc = scroll_facts();
        for (int k = 0; k < b_n_items; ++k)
            if (magic::usable(sc, b_rec(k))) usable.push_back(k);
    }
    for (int k : usable) {
        const uint8_t* r = b_rec(k);
        if (!boot()) return;
        if (!load_magic()) return;
        const int s = b_spell(r);
        if (spells::entry(mrules->tables, s).targets != spells::kCombat) continue;   // (the self / party ones: as outside)
        party::Character& ch = pt->m[kA];
        ch.n_items = 0;
        ch.rec[0x12B] = 0xFF;                       // (any class: the wand for the test)
        const int i = b_give(ch, r, true);
        rules::recalc(ch, *names, d->facts);
        // The others out of it: only the item hurts the monsters
        for (int m = 0; m < pt->count; ++m)
            if (m != kA) pt->m[m].rec[0x195] = party::Unconscious, pt->m[m].rec[0x196] = 0;
        b_fu_stage = 0;
        b_fu_item = i;
        b_fu_rec = r;
        b_fu_charges = r[0x3C];
        on_menu = b_fu_menu;
        god = true;
        if (!start_fight()) {
            expect("items", "the inn's fight", false);
            continue;
        }
        pump(400000);
        on_menu = nullptr;
        god = false;
        if (b_fu_stage < 2) {
            char what[96];
            snprintf(what, sizeof what, "record %d %s used in a fight", k, b_nm(r));
            expect("items", what, false, "stage %d", b_fu_stage);
        }
    }
}

// ---- 9. The cursed weapons' and robes' own effects: the Robe of Vermin
// (effect 0x4A: a spell miscasts 1 in 2 - "miscasts", nothing cast, the
// spell kept; coab ovr023 sub_5D2E1), the Berserker sword (0x4D: readied
// outside a fight its wearer goes Quick, control 0xB3 - coab AffectBerzerk),
// and those with Dragon Slayer (0x4B) or 0x4A get no free attack on one
// stepping away (coab ovr014 move_step_away_attack)
static void b_cursed_effects()
{
    using namespace play;
    if (!boot()) return;
    for (int m = 0; m < pt->count; ++m) b_saved[m] = pt->m[m];
    // Robe of Vermin: a camp spell with the d2 a 1
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (r[0x3E] != 0x80 || r[0x3D] != 0x4A) continue;
        if (!load_magic()) return;
        party::Character& ch = b_fresh(bm_mu);
        const int i = b_give(ch, r, false, true);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        int sp = 0;
        for (int s = 1; s <= 0x38 && !sp; ++s) {
            const spells::CampSpell* cs = camp_spell(s);
            const spells::Entry e = spells::entry(mrules->tables, s);
            if (cs && cs->does == spells::Does::Affect && e.affect && e.level <= 2 && mrules->tables.spell_class(s) == 2 &&
                (e.targets == spells::kSelf || e.targets == spells::kMember))
                sp = s;
        }
        if (sp) ch.rec[magic::kListAt] = static_cast<uint8_t>(sp);     // (memorized)
        if (!sp) {
            expect("items", "a camp spell memorized by the magic-user", false);
            break;
        }
        uint8_t ids[magic::kListSize];
        auto count = [&]() { return magic::in_memory(ch, mrules->tables, false, ids, magic::kListSize); };
        const int held = count();
        const int fx = spells::entry(mrules->tables, sp).affect;
        cr = CastRun{};
        cr.caster = 5;
        pt->selected = 5;
        view_from = Screen::Game;
        dice_faces({1, 1, 1, 1});
        choose_spell(sp, C);
        for (int n = 0; n < 200; ++n) {
            if (screen != Screen::Cast) break;
            if (cr.stage == CastRun::Whom) {
                cr.target = 5;
                do_cast(C);
                continue;
            }
            g_now += 5000;
            play::tick(g_now, C);
        }
        dice_usual();
        screen = Screen::Game;
        load_magic();
        char what[112];
        snprintf(what, sizeof what, "record %d %s worn, a spell (%02X) with the d2 a 1: miscasts - no effect, the spell kept", k,
                 b_nm(r), sp);
        const int after = count();
        expect("items", what, !ch.has_affect(static_cast<uint8_t>(fx)) && after == held, "effect %s, spells %d -> %d",
               ch.has_affect(static_cast<uint8_t>(fx)) ? "given" : "not given", held, after);
    }
    // Berserker: readied, Quick and control 0xB3
    for (int k = 0; k < b_n_items; ++k) {
        const uint8_t* r = b_rec(k);
        if (r[0x3E] != 0x80 || r[0x3D] != 0x4D) continue;
        party::Character& ch = b_fresh(kA);
        const int i = b_give(ch, r, false, true);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        char what[96];
        snprintf(what, sizeof what, "record %d %s readied: the wearer goes berserk (control 0xB3)", k, b_nm(r));
        expect("items", what, ch.rec[0xF7] == 0xB3, "control %02X", ch.rec[0xF7]);
    }
    // No free attack from one with Dragon Slayer's effect (or 0x4A)
    if (start_fight()) {
        combat::Battle& b = fg->b;
        for (uint8_t fx : {static_cast<uint8_t>(0x4B), static_cast<uint8_t>(0x4A)}) {
            party::Character& ch = b_fresh(kA);
            spells::add_affect(ch, fx, 0, 0xFF, true);
            b_sync(kA);
            b_place(kA, kT, 1);
            b.f[kA].delay = 5;                         // (it hasn't acted: a free attack due)
            b.f[kA].rec[0x196] = 1;
            const bool free = combat::free_attack_ok(b, fg->t, kA, kT);
            char what[96];
            snprintf(what, sizeof what, "a party member with effect %02X: no free attack on a monster stepping away", fx);
            expect("items", what, !free);
        }
    }
}

static void behave_items()
{
    using namespace play;
    if (!b_load_items()) {
        expect("items", "item records", false, "no ITEMS_BIN");
        return;
    }
    if (!b_load_oracle()) {
        expect("items", "oracle", false, "no ORACLE_DIR/oracle_items.txt");
        return;
    }
    if (!boot()) {
        expect("items", "boot", false);
        return;
    }
    for (int m = 0; m < pt->count; ++m) b_saved[m] = pt->m[m];
    b_pool = pool_files();
    if (b_pool) {
        bm_mu = 4;
        bm_cl = 3;
        bm_th = 5;
    }
    // The records as the player gets them: an item the original got wrong
    // fixed when that's on (CYD_ITEM_FIXES; Pool's Flame Tongue)
    for (int k = 0; k < b_n_items; ++k)
        if (items::fix_item(&b_items[static_cast<size_t>(k) * 63])) {
            char what[96];
            snprintf(what, sizeof what, "record %d %s: fixed as AD&D (+%d, effect %02X)", k, b_nm(b_rec(k)), b_rec(k)[0x32],
                     b_rec(k)[0x3D]);
            expect("items", what, b_rec(k)[0x32] == 1 && b_kind(b_rec(k)) == BEffect);
        }
    // ---- A magic weapon's plus: +1 to hit AND +1 damage (the long sword, type 36)
    {
        const uint8_t* plain = b_plain_of_type(36);
        if (!expect("items", "a plain long sword in the game's items", plain != nullptr)) return;
        party::Character& ch = pt->m[1];             // a fighter of the sample party
        b_hold_only(ch, plain);
        const int hit0 = ch.rec[0x199], dmg0 = ch.damage_bonus(), dice0 = ch.dice(), sides0 = ch.dice_sides();
        uint8_t plus1[63];
        memcpy(plus1, plain, 63);
        plus1[0x32] = 1;
        b_hold_only(ch, plus1);
        expect("items", "+1 long sword: to hit +1", ch.rec[0x199] == hit0 + 1, "to-hit value %d -> %d", hit0, ch.rec[0x199]);
        expect("items", "+1 long sword: damage +1", ch.damage_bonus() == dmg0 + 1, "damage bonus %d -> %d", dmg0,
               ch.damage_bonus());
        expect("items", "+1 long sword: the long sword's dice", ch.dice() == dice0 && ch.dice_sides() == sides0, "%dd%d",
               ch.dice(), ch.dice_sides());
        const items::TypeInfo& ti = names->type(36);
        expect("items", "long sword: 1d8 against man-sized (the ITEMS table)", dice0 == ti.dice && sides0 == ti.sides,
               "%dd%d, table %dd%d", dice0, sides0, ti.dice, ti.sides);
    }
    const char* only = getenv("ITEMS_ONLY");        // (a part: values, adnd, ready, names, money, ...)
    auto part = [&](const char* p) { return !only || strstr(only, p); };
    if (part("values")) b_readied_values();
    if (part("adnd")) b_adnd_tables();
    if (part("ready")) b_ready_rules();
    if (part("names")) b_names_detect();
    if (part("money")) b_money();
    if (part("worn")) b_worn_items();
    if (part("use")) b_use_items();
    if (part("fightuse")) b_fight_use();
    if (part("cursed")) b_cursed_effects();
    if (part("fight")) b_fight_weapons();
    if (part("effects")) b_fight_effects();
    for (int m = 0; pt && m < pt->count; ++m) pt->m[m] = b_saved[m];
}

// ---- 10. The ITEMTEST characters (sweep's testchars mode) as the player
// takes them: read by Add Character, every item readied alone by its owner
// (readied or the game's own refusal, the values and stats as the rules
// make them), every usable one used (a use taken), then a fight with them
// all, each with what they can ready. ITEMTEST_REPORT=file: a line an item.
static void behave_itemtest()
{
    using namespace play;
    if (!b_load_oracle()) {
        expect("itemtest", "oracle", false, "no ORACLE_DIR/oracle_items.txt");
        return;
    }
    if (!boot()) {
        expect("itemtest", "boot", false);
        return;
    }
    b_pool = pool_files();
    if (b_pool) {
        bm_mu = 4;
        bm_cl = 3;
        bm_th = 5;
    }
    FILE* rep = getenv("ITEMTEST_REPORT") ? fopen(getenv("ITEMTEST_REPORT"), "w") : nullptr;
    find_guys();                                    // (the game's own characters, as its Add Character)
    pt->clear();
    int listed = 0;
    for (int i = 0; i < d->guys; ++i) {
        if (strncmp(d->guy_name[i], "ITEMTEST", 8)) continue;
        ++listed;
        const int n0 = pt->count;
        add_character(i, C);
        note_until = 0;
        screen = Screen::Game;
        char what[64];
        snprintf(what, sizeof what, "%s (%s) added", d->guy_name[i], d->guy_file[i]);
        expect("itemtest", what, pt->count == n0 + 1, "party %d", pt->count);
    }
    if (!expect("itemtest", "ITEMTEST characters on the card", listed > 0)) {
        if (rep) fclose(rep);
        return;
    }
    vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
    if (!load_magic()) {
        expect("itemtest", "the spell tables", false);
        if (rep) fclose(rep);
        return;
    }
    const magic::Scrolls sc = scroll_facts();
    static party::Character base[party::kMaxParty];
    for (int m = 0; m < pt->count; ++m) base[m] = pt->m[m];
    int n_items = 0, n_used = 0;
    for (int m = 0; m < pt->count; ++m) {
        char who[20];
        base[m].name(who, sizeof who);
        if (rep) fprintf(rep, "== %s: %d items, Str %d/%d Dex %d Con %d, AC %d, THAC0-ish hit %d\n", who, base[m].n_items,
                         base[m].rec[0x11], base[m].rec[0x1C], base[m].rec[0x17], base[m].rec[0x19], 60 - base[m].rec[0x19A],
                         static_cast<int8_t>(base[m].rec[0x199]));
        for (int j = 0; j < base[m].n_items; ++j) {
            ++n_items;
            pt->m[m] = base[m];
            pt->selected = m;
            party::Character& ch = pt->m[m];
            for (int q = 0; q < ch.n_items; ++q) ch.items[q][0x34] = 0;
            rules::recalc(ch, *names, d->facts);
            const BVals v0 = b_engine(ch);
            const int s0[6] = {ch.rec[0x11], ch.rec[0x13], ch.rec[0x15], ch.rec[0x17], ch.rec[0x19], ch.rec[0x1B]};
            uint8_t r[63];
            memcpy(r, ch.items[j], 63);
            char nm[64];
            names->name(items::Item{r}, nm, sizeof nm, true);
            const items::TypeInfo& t = names->type(r[0x2E]);
            const BKind kind = b_kind(r);
            const bool classes_ok = (ch.rec[0x12B] & t.classes) != 0;
            const bool giant_ok = kind != BNeedGiant || ch.rec[0x10] >= 19;
            const bool align_ok = kind != BAlign || (r[0x3D] & 0x0F) == ch.alignment();
            const bool want_on = classes_ok && giant_ok && align_ok;
            const bool on = b_ready(j);
            char what[160];
            snprintf(what, sizeof what, "%s: %s %s", who, nm, want_on ? "readied" : "refused (the game's rule)");
            expect("itemtest", what, on == want_on, "readied %d (classes %02X / %02X, Str %d, alignment %d)", on, ch.rec[0x12B],
                   t.classes, ch.rec[0x10], ch.alignment());
            if (on) {
                snprintf(what, sizeof what, "%s: %s readied - to hit, damage, AC, move, weight, saves as the rules", who, nm);
                b_values(what, ch);
                if (kind != BEffect && kind != BWiz && kind != BThief && kind != BAlign && kind != BNeedGiant && kind != BNone) {
                    const BStats e = b_stats_expect(ch);
                    snprintf(what, sizeof what, "%s: %s readied - the stats in use", who, nm);
                    expect("itemtest", what,
                           ch.rec[0x11] == e.s[0] && ch.rec[0x1C] == e.str00 && ch.rec[0x13] == e.s[1] &&
                               ch.rec[0x15] == e.s[2] && ch.rec[0x17] == e.s[3] && ch.rec[0x19] == e.s[4] &&
                               ch.rec[0x1B] == e.s[5],
                           "%d/%d %d %d %d %d %d, expected %d/%d %d %d %d %d %d", ch.rec[0x11], ch.rec[0x1C], ch.rec[0x13],
                           ch.rec[0x15], ch.rec[0x17], ch.rec[0x19], ch.rec[0x1B], e.s[0], e.str00, e.s[1], e.s[2], e.s[3],
                           e.s[4], e.s[5]);
                }
                if (kind == BEffect && r[0x3D]) {
                    snprintf(what, sizeof what, "%s: %s readied - its effect %02X on", who, nm, r[0x3D]);
                    expect("itemtest", what, ch.has_affect(r[0x3D]));
                }
            }
            const BVals v1 = b_engine(ch);
            const char* sn[6] = {"Str", "Int", "Wis", "Dex", "Con", "Cha"};
            const int s1[6] = {ch.rec[0x11], ch.rec[0x13], ch.rec[0x15], ch.rec[0x17], ch.rec[0x19], ch.rec[0x1B]};
            if (rep) {
                fprintf(rep, "  %-34s %s", nm, on ? "readied" : "NOT readied");
                if (on) {
                    if (v1.hit != v0.hit) fprintf(rep, ", to hit %+d", v1.hit - v0.hit);
                    if (v1.dmg != v0.dmg) fprintf(rep, ", damage %+d (%+d in all)", v1.dmg - v0.dmg, v1.dmg);
                    if (v1.dice != v0.dice || v1.sides != v0.sides) fprintf(rep, ", dice %dd%d", v1.dice, v1.sides);
                    if (v1.ac != v0.ac) fprintf(rep, ", AC %+d", -(v1.ac - v0.ac));
                    if (v1.save != v0.save) fprintf(rep, ", saves %+d", v1.save - v0.save);
                    if (v1.move != v0.move) fprintf(rep, ", move %d", v1.move);
                    for (int s = 0; s < 6; ++s)
                        if (s1[s] != s0[s]) fprintf(rep, ", %s %d -> %d", sn[s], s0[s], s1[s]);
                    if (s1[0] == 18 && ch.rec[0x1C] != base[m].rec[0x1C]) fprintf(rep, " (18/%02d)", ch.rec[0x1C] % 100);
                    for (int a = 0; a < ch.n_affects; ++a) fprintf(rep, ", effect %02X", ch.affects[a][0]);
                }
            }
            // Used (readied; Yes to a fights-only item outside a fight): a use taken
            if (on && magic::usable(sc, r)) {
                ++n_used;
                const int hp0 = ch.hp(), aff0 = ch.n_affects, n0 = ch.n_items, cnt = r[0x39], chg = r[0x3C];
                // (room for healing)
                if (ch.hp_max() > 1) ch.rec[0x1A4] = static_cast<uint8_t>(ch.hp_max() / 2 > 0 ? ch.hp_max() / 2 : 1);
                const int hpu = ch.hp();
                b_use(m, j, m, 'Y');
                const int k = ch.n_items == n0 ? j : -1;    // (the list keeps its order; one gone: shorter)
                const bool taken = (n0 == ch.n_items + 1 && k < 0) || (k >= 0 && cnt > 1 && ch.items[k][0x39] == cnt - 1) ||
                                   (k >= 0 && chg > 1 && ch.items[k][0x3C] == chg - 1) || (k >= 0 && chg == 0);
                snprintf(what, sizeof what, "%s: %s used (spell %02X): a use taken", who, nm, b_spell(r));
                expect("itemtest", what, taken, "items %d -> %d, count %d charges %d", n0, ch.n_items, k >= 0 ? ch.items[k][0x39] : -1,
                       k >= 0 ? ch.items[k][0x3C] : -1);
                if (rep) {
                    fprintf(rep, "; used: spell %02X", b_spell(r));
                    if (ch.hp() != hpu) fprintf(rep, ", HP %d -> %d", hpu, ch.hp());
                    for (int a = aff0; a < ch.n_affects; ++a)
                        fprintf(rep, ", effect %02X for %d min", ch.affects[a][0], ch.affects[a][1] | ch.affects[a][2] << 8);
                    for (int s = 0; s < 6; ++s) {
                        const int now = ch.rec[0x11 + 2 * s];
                        if (now != s1[s]) fprintf(rep, ", %s %d -> %d", sn[s], s1[s], now);
                    }
                    if (k < 0) fprintf(rep, ", gone");
                    else if (cnt > 1) fprintf(rep, ", %d left", ch.items[k][0x39]);
                    else if (chg) fprintf(rep, ", %d charges left", ch.items[k][0x3C]);
                }
                (void)hp0;
            }
            if (rep) fprintf(rep, "\n");
        }
        pt->m[m] = base[m];
    }
    char sum[64];
    snprintf(sum, sizeof sum, "%d items readied one by one, %d used", n_items, n_used);
    expect("itemtest", sum, true);
    // A fight with them all: each readies what it can (the first that fits each slot)
    for (int m = 0; m < pt->count; ++m) {
        pt->m[m] = base[m];
        pt->selected = m;
        party::Character& ch = pt->m[m];
        for (int j = 0; j < ch.n_items; ++j) {
            const BKind kind = b_kind(ch.items[j]);
            if (ch.items[j][0x36] || kind == BNeedGiant || kind == BAlign) continue;    // (no cursed, no refusals)
            b_ready(j);
        }
    }
    god = true;
    if (!expect("itemtest", "a fight with the ITEMTEST party", start_fight())) {
        god = false;
        if (rep) fclose(rep);
        return;
    }
    pump(800000);
    god = false;
    snprintf(sum, sizeof sum, "the ITEMTEST party's fight to its end");
    expect("itemtest", sum, screen != Screen::Fight, "screen %d", static_cast<int>(screen));
    if (rep) fclose(rep);
}

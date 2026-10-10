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
            expect("items", "Ring of Prot. +3 alone: AC 7, saves +3", ch.ac() == 7 && static_cast<int8_t>(ch.rec[0x186]) == 3,
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
            expect("items", "ring +3 with Chain Mail +1: the ring's saves still count (+3)",
                   static_cast<int8_t>(ch.rec[0x186]) == 3, "saves %+d", static_cast<int8_t>(ch.rec[0x186]));
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
            party::Character& ch = b_fresh(2);                // the dwarf fighter / thief
            b_give(ch, r, true);
            rules::recalc(ch, *names, d->facts);
            b_sync(2);
            b_place(2, kT, 1);
            b_target(kT, 50);
            fg->b.f[kT].facing = 2;                            // its back to the thief
            fg->b.f[kT].received = 2;
            const bool got = combat::can_backstab(fg->b, 2, kT, names);
            char what[80];
            snprintf(what, sizeof what, "%s: %s", b_nm(r), want ? "backstabs" : "no backstab");
            expect("items", what, got == want);
        }
        party::Character& ch = b_fresh(2);
        rules::recalc(ch, *names, d->facts);
        b_sync(2);
        b_place(2, kT, 1);
        for (int sz : {0x01, 0x81, 0x02, 0x09}) {
            b_target(kT, 50, sz);
            fg->b.f[kT].facing = 2;
            fg->b.f[kT].received = 2;
            const bool want = (sz & 0x7F) <= 1;
            char what[64];
            snprintf(what, sizeof what, "backstab bare-handed, target size byte %02X: %s", sz, want ? "yes" : "no");
            expect("items", what, combat::can_backstab(fg->b, 2, kT, names) == want);
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
        if (r[0x3E] != 0x80 || names->type(r[0x2E]).slot != 0) continue;
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
            if (fx == 6) bonus = mtype == 10 ? 1 : (mtype == 9 || mtype == 12) ? 2 : mtype == 4 ? 3 : 0;
            if (fx == 0x4C) bonus = mtype == 8 ? 3 : 0;
            if (fx == 0x4B && mtype == 3) {
                bonus = 2;
                slayer = true;
            }
            if (fx != 6 && fx != 0x4B && fx != 0x4C) continue;
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
        if (r[0x3E] != 0x80 || r[0x3D] != 0x59) continue;
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
        if (r[0x3E] != 0x80 || r[0x3D] != 0x38) continue;
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
        if (it[0x3E] <= 0x80 || !it[0x34]) continue;
        const int code = it[0x3E] & 0x7F, p = it[0x3D];
        int b = 0, b00 = 0;
        if (code == 5 && p <= 6) {
            b = p == 0 ? 18 : 18 + p;
            b00 = p == 0 ? 100 : 0;
        }
        if (code == 8 && p == 0 && own[0] < 18) b = own[0] + 1;
        if (b > str || (b == 18 && b00 > s00)) {
            str = b;
            s00 = b00;
        }
        if (code == 13) {
            forced_str = 3;
            forced_int = 3;
        }
        if (code == 12) forced_int = 7;             // (the last such item's counts)
        if (code == 8 && p >= 1 && p <= 5 && own[p] < 18) ++v[p];
        if (code == 2) v[3] += own[3] <= 6 ? 4 : own[3] <= 13 ? 2 : 1;
        if (code == 10) v[3] -= 2;
        if (code == 6) {
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
        if (code == 0 || code == 1 || code == 11) continue;
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
            if (b_rec(k)[0x3E] == 0x88 && b_rec(k)[0x3D] == 3) ioun = b_rec(k);
            if (b_rec(k)[0x3E] == 0x82) gaunt = b_rec(k);
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
        if (r[0x3E] != 0x80) continue;
        party::Character& ch = b_fresh(kA);
        ch.rec[0x12B] = 0xFF;
        const int i = b_give(ch, r, false, true);
        rules::recalc(ch, *names, d->facts);
        b_ready(i);
        const int fx = r[0x3D];
        int at = -1;
        for (int j = 0; j < ch.n_affects; ++j)
            if (ch.affects[j][0] == fx) at = j;
        char what[96];
        snprintf(what, sizeof what, "record %d %s readied: effect %02X, data 0xFF, no time", k, b_nm(r), fx);
        expect("items", what, at >= 0 && ch.affects[at][3] == 0xFF && ch.affects[at][1] == 0 && ch.affects[at][2] == 0,
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
        if (r[0x3E] == 0x80)
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
        if (r[0x3E] != 0x81) continue;
        party::Character& ch = b_fresh(5);                          // the magic-user
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
            const int code = r[0x3E] & 0x7F;
            if (r[0x3E] < 0x80 || (code != 2 && code != 11)) continue;
            for (int lvl : {1, 3, 4, 5, 6, 7, 9}) {
                party::Character& ch = b_fresh(2);                  // the dwarf fighter / thief
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
    if (part("fight")) b_fight_weapons();
    if (part("effects")) b_fight_effects();
    for (int m = 0; pt && m < pt->count; ++m) pt->m[m] = b_saved[m];
}

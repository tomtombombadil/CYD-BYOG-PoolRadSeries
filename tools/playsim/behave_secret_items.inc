// Behaviour checks: Secret of the Silver Blades - items (included by
// behave_secret.inc inside namespace bsecret; ONLY=allitems). Expected values
// from Secret's own program and tables (the oracle, scratch:
// oracle_secret_items.txt from secret_items_oracle.py) and the Project's
// claude/secret_facts.md / secret_facts_2.md - never from the engine's own
// code. Every item record of the game (ITEM1-3, MON1ITM) one by one: what it
// does readied (its values, its stats, its effect, its code), whether each
// class may ready it, using it (charges, scrolls), its price; Secret's own
// item effects in fights; Join; the random items its TREASURE makes.
// ITEMS_ONLY= values, codes, ready, use, scrolls, money, names, treasure, fights.

const char* PI = "items";

// ---- Every item record of the game files
struct SItem {
    uint8_t     r[items::kRecordSize];
    std::string from;                   // "ITEM1 99 3"
};
std::vector<SItem> s_items;

void s_load_items()
{
    s_items.clear();
    const std::string folder = game_dir.substr(game_dir.rfind('/') + 1);
    for (const char* fn : {"ITEM1", "ITEM2", "ITEM3", "MON1ITM"}) {
        char path[300];
        snprintf(path, sizeof path, "/GOLDBOX/%s/%s.DAX", folder.c_str(), fn);
        plat::File f = plat::open(path, "r");
        if (!f) continue;
        plat::FileSource src(f);
        static dax::Index dir;
        if (dax::read_index(src, dir) != dax::Status::Ok) {
            f.close();
            continue;
        }
        for (int i = 0; i < dir.count; ++i) {
            std::vector<uint8_t> blk(dir.entries[i].raw_size + 64);
            const uint32_t n = dax::load_block(src, dir, dir.entries[i], blk.data());
            for (uint32_t o = 0; o + items::kRecordSize <= n; o += items::kRecordSize) {
                SItem s;
                memcpy(s.r, blk.data() + o, items::kRecordSize);
                if (!s.r[0x2E]) continue;
                char w[40];
                snprintf(w, sizeof w, "%s %d %u", fn, dir.entries[i].id, static_cast<unsigned>(o / items::kRecordSize));
                s.from = w;
                s_items.push_back(s);
            }
        }
        f.close();
    }
}

std::string s_name(const uint8_t* r)
{
    char nm[64];
    names->name(items::Item{r}, nm, sizeof nm, true);
    return nm;
}

// ---- The oracle's tables
struct SType {
    int slot, hands, dl, sl, bl, attacks, ac, blow, d, s, b, range, classes, flags;
};
SType s_type(int t)
{
    SType x{};
    const auto* r = b_row("it_type", t);
    if (!r || r->size() < 15) return x;
    const auto& v = *r;
    x = SType{v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14]};
    return x;
}
int s_group(int str, int e)
{
    const std::vector<int>* r = nullptr;
    if (str == 18) {
        // (its rows: the 18/xx bands' edges)
        const int edges[] = {0, 1, 50, 51, 75, 76, 90, 91, 99, 100};
        int pick = 0;
        for (int k : edges)
            if (k <= e) pick = k;
        r = b_row("it_strgroup", 18, pick);
    } else {
        r = b_row("it_strgroup", str, 0);
    }
    return r && r->size() > 2 ? (*r)[2] : 0;
}
int s_str(int g, int col)               // col 1 hit, 2 damage, 3 the load allowance
{
    const auto* r = b_row("it_str", g);
    return r && static_cast<int>(r->size()) > col ? (*r)[col] : 0;
}
int s_dex(int dx, int col)              // col 1 AC, 2 missiles
{
    const auto* r = b_row("it_dex", dx);
    return r && static_cast<int>(r->size()) > col ? (*r)[col] : 0;
}
bool s_in(const char* key, int v)
{
    const auto& rows = b_rows(key);
    if (rows.empty()) return false;
    for (int x : rows[0])
        if (x == v) return true;
    return false;
}
int s_col(const char* key, int i)
{
    const auto& rows = b_rows(key);
    return rows.empty() || i >= static_cast<int>(rows[0].size()) ? 0 : rows[0][i];
}

// ---- A test character: party member 0's record made plain
// (record offsets: the engine's Curse layout, which Secret's record is moved into)
party::Character s_base;
party::Character& s_fresh(int race, int str, int e, int dex, const int cls_level[8])
{
    party::Character& c = pt->m[0];
    c = s_base;
    c.n_items = 0;
    memset(c.items, 0, sizeof c.items);
    c.n_affects = 0;
    memset(c.affects, 0, sizeof c.affects);
    c.rec[0x74] = static_cast<uint8_t>(race);
    c.rec[0x10] = c.rec[0x11] = static_cast<uint8_t>(str);
    c.rec[0x1D] = c.rec[0x1C] = static_cast<uint8_t>(e);
    c.rec[0x16] = c.rec[0x17] = static_cast<uint8_t>(dex);
    int hd = 0;
    for (int k = 0; k < 8; ++k) {
        c.rec[0x109 + k] = static_cast<uint8_t>(cls_level[k]);
        c.rec[0x111 + k] = 0;
        hd = std::max(hd, cls_level[k]);
    }
    c.rec[0xE5] = static_cast<uint8_t>(hd);
    c.rec[0x11E] = 1;                   // unarmed 1d2 + 0
    c.rec[0x120] = 2;
    c.rec[0x122] = 0;
    c.rec[0x124] = 50;                  // base AC 10
    c.rec[0x125] = 1;                   // Strength counts
    c.rec[0xE4] = 12;                   // base movement
    for (int m2 = 0; m2 < 14; ++m2) c.rec[0xFB + m2] = 0;      // no coins
    c.rec[0x195] = party::Okay;
    c.rec[0x196] = 1;
    c.rec[0x1A4] = c.rec[0x78] = 60;
    pt->selected = 0;
    classes::class_bonuses(c, m->tables);
    c.rec[0x73] = 50;                   // THAC0 byte (the record's: higher better)
    rules::recalc(c, *names, d->facts);
    return c;
}

// ---- 1. Readied values: Secret's own routines (GAME.OVR 0x3A292 the
// recalculation, 0x38151 the weapon, 0x3833F armour and movement, 0x383C9
// AC, 0x384E7 the load, 0x3A73B movement), from the oracle's tables
struct SVals {
    int hit, dmg, dice, sides, ac, behind, move, weight, save;
};
SVals s_expect(const party::Character& c)
{
    const uint8_t* r = c.rec;
    SVals v{};
    int weight = 0;
    const uint8_t* weapon = nullptr;
    const uint8_t* arrows = nullptr;
    const uint8_t* quarrels = nullptr;
    for (int i = 0; i < c.n_items; ++i) {
        const uint8_t* it = c.items[i];
        int w = it[0x37] | it[0x38] << 8;
        if (it[0x39] > 0) w *= it[0x39];
        weight += w;
        if (!it[0x34]) continue;
        if (s_type(it[0x2E]).slot == 0) weapon = it;
        if (it[0x2E] == 30) arrows = it;            // (Secret's arrows / quarrels: types 0x1E, 0x0C)
        if (it[0x2E] == 12) quarrels = it;
    }
    for (int m2 = 0; m2 < 7; ++m2) weight += c.money(m2);
    v.weight = weight;
    const int g = s_group(r[0x11], r[0x1C]);
    const int dex = r[0x17];
    v.hit = static_cast<int8_t>(r[0x73]);
    v.dmg = static_cast<int8_t>(r[0x122]);
    v.dice = r[0x11E];
    v.sides = r[0x120];
    int bonus[5] = {s_dex(dex, 1), 0, 0, 0, 0};
    if (!weapon) {
        v.hit += r[0x125] ? s_str(g, 1) : 0;
        v.dmg += r[0x125] ? s_str(g, 2) : 0;
    } else {
        const SType t = s_type(weapon[0x2E]);
        if (t.flags & 2) v.hit += s_dex(dex, 2);
        v.dmg = t.b;
        if (t.flags & 4) {
            v.hit += r[0x125] ? s_str(g, 1) : 0;
            v.dmg += r[0x125] ? s_str(g, 2) : 0;
        }
        int plus = static_cast<int8_t>(weapon[0x32]);
        if ((t.flags & 0x80) && quarrels) plus += static_cast<int8_t>(quarrels[0x32]);
        if ((t.flags & 0x01) && arrows) plus += static_cast<int8_t>(arrows[0x32]);
        v.dmg += plus;
        // every race in the original; an elf's alone with the mistakes fixed
        if (s_in("it_plus1_types", weapon[0x2E]) && (c.rec[0x74] == 2 || !rules::fixes())) ++plus;
        v.hit += plus;
        v.dice = t.d;
        v.sides = t.s;
    }
    int move = r[0xE4];
    bool magic_armour = false;
    for (int i = 0; i < c.n_items; ++i) {
        const uint8_t* it = c.items[i];
        if (!it[0x34]) continue;
        const SType t = s_type(it[0x2E]);
        const int plus = static_cast<int8_t>(it[0x32]);
        if (t.slot == 2) {
            const int w = it[0x37] | it[0x38] << 8;
            move = w <= s_col("it_armour_move", 0) ? r[0xE4] : w <= s_col("it_armour_move", 1) ? s_col("it_armour_move", 2)
                                                                                                 : s_col("it_armour_move", 3);
            if (plus != 0 && move != 0 && move <= 9) move += s_col("it_armour_move", 4);
        }
        if (t.ac > 0x7F) {
            const int a = t.ac & 0x7F;
            if (t.slot == 1) bonus[1] = plus + a;
            else if (a == 0) {
                if (t.slot == 9) bonus[3] = std::max(bonus[3], plus);
                else bonus[2] += plus;
                v.save += static_cast<int8_t>(it[0x33]);
            } else if (plus + a > bonus[4]) {
                bonus[4] = plus + a;
                if (plus > 0 && t.slot == 2) magic_armour = true;
            }
        }
    }
    if (magic_armour) bonus[3] = 0;
    int over = weight - s_str(g, 3);
    if (over < 0) over = 0;
    const int can = over <= s_col("it_load", 0) ? move : over <= s_col("it_load", 1) ? s_col("it_load", 3)
                    : over <= s_col("it_load", 2) ? s_col("it_load", 4) : s_col("it_load", 5);
    if (can < move) move = can;
    if (c.has_affect(0x4A)) move *= 2;          // (the boots' effect: 0x3A7FC)
    v.move = move;
    if (bonus[4] < r[0x124]) bonus[4] = r[0x124];
    v.ac = bonus[0] + bonus[1] + bonus[2] + bonus[3] + bonus[4];
    v.behind = bonus[4] + bonus[2] + bonus[3] - 2;
    return v;
}
SVals s_engine(const party::Character& c)
{
    const uint8_t* r = c.rec;
    return SVals{static_cast<int8_t>(r[0x199]), static_cast<int8_t>(r[0x1A2]), r[0x19E], r[0x1A0], r[0x19A], r[0x19B],
                 r[0x1A5], r[0x187] | r[0x188] << 8, static_cast<int8_t>(r[0x186])};
}
bool s_same(const char* what, const party::Character& c)
{
    const SVals e = s_expect(c), g = s_engine(c);
    const bool ok = e.hit == g.hit && e.dmg == g.dmg && e.dice == g.dice && e.sides == g.sides && e.ac == g.ac &&
                    e.behind == g.behind && e.move == g.move && e.weight == g.weight && e.save == g.save;
    return expect(PI, what, ok, "engine hit %d dmg %d %dd%d AC %d/%d move %d weight %d save %d; the original's "
                                "%d %d %dd%d %d/%d %d %d %d",
                  g.hit, g.dmg, g.dice, g.sides, g.ac, g.behind, g.move, g.weight, g.save, e.hit, e.dmg, e.dice, e.sides,
                  e.ac, e.behind, e.move, e.weight, e.save);
}

void s_readied_values()
{
    // A human and an elf fighter (Strength 18/76, Dex 16; Strength 12, Dex
    // 17), each record readied alone, the mistakes fixed and not
    const int lv[8] = {0, 0, 9, 0, 0, 0, 0, 0};
    struct Who {
        int race, str, e, dex;
        const char* what;
    } whos[] = {{7, 18, 76, 16, "human 18/76"}, {2, 12, 0, 17, "elf 12"}};
    const bool fx = rules::fixes();
    for (int pass = 0; pass < 2; ++pass) {
        rules::set_fixes(pass == 0);
        for (const Who& w : whos)
            for (const SItem& s : s_items) {
                party::Character& c = s_fresh(w.race, w.str, w.e, w.dex, lv);
                rules::add_item(c, s.r);
                c.items[0][0x34] = 1;
                rules::worn(c, 0, true);
                rules::recalc(c, *names, d->facts);
                char t[160];
                snprintf(t, sizeof t, "readied %s (%s), %s, fixes %s", s_name(s.r).c_str(), s.from.c_str(), w.what,
                         pass == 0 ? "on" : "off");
                s_same(t, c);
            }
    }
    rules::set_fixes(fx);
    // Launchers with their ammunition's plus: every bow / crossbow / sling
    // record with the best arrows and quarrels
    const uint8_t* best_arrow = nullptr;
    const uint8_t* best_quarrel = nullptr;
    for (const SItem& s : s_items) {
        if (s.r[0x2E] == 30 && (!best_arrow || static_cast<int8_t>(s.r[0x32]) > static_cast<int8_t>(best_arrow[0x32])))
            best_arrow = s.r;
        if (s.r[0x2E] == 12 && (!best_quarrel || static_cast<int8_t>(s.r[0x32]) > static_cast<int8_t>(best_quarrel[0x32])))
            best_quarrel = s.r;
    }
    for (const SItem& s : s_items) {
        const SType t = s_type(s.r[0x2E]);
        if (t.slot != 0 || !(t.flags & 0x81)) continue;
        party::Character& c = s_fresh(7, 17, 0, 18, lv);
        rules::add_item(c, s.r);
        c.items[0][0x34] = 1;
        if (best_arrow && (t.flags & 1)) {
            rules::add_item(c, best_arrow);
            c.items[c.n_items - 1][0x34] = 1;
        }
        if (best_quarrel && (t.flags & 0x80)) {
            rules::add_item(c, best_quarrel);
            c.items[c.n_items - 1][0x34] = 1;
        }
        rules::recalc(c, *names, d->facts);
        char tx[160];
        snprintf(tx, sizeof tx, "%s (%s) with its ammunition", s_name(s.r).c_str(), s.from.c_str());
        s_same(tx, c);
    }
    // Armour's movement: plain heavy armour (no +3) against magic armour (+3)
    {
        uint8_t plate[items::kRecordSize] = {};
        plate[0x2E] = 36;
        plate[0x31] = 36;
        plate[0x37] = 450 & 0xFF;
        plate[0x38] = 450 >> 8;
        party::Character& c = s_fresh(7, 17, 0, 12, lv);
        rules::add_item(c, plate);
        c.items[0][0x34] = 1;
        rules::recalc(c, *names, d->facts);
        expect(PI, "plain plate mail (450): movement 6 - no +3 (GAME.OVR 0x38399)", c.rec[0x1A5] == 6, "%d", c.rec[0x1A5]);
        c.items[0][0x32] = 1;
        rules::recalc(c, *names, d->facts);
        expect(PI, "plate mail +1 (450): movement 9", c.rec[0x1A5] == 9, "%d", c.rec[0x1A5]);
        c.items[0][0x37] = 300 & 0xFF;
        c.items[0][0x38] = 300 >> 8;
        c.items[0][0x32] = 0;
        rules::recalc(c, *names, d->facts);
        expect(PI, "plain armour of 300: movement 9", c.rec[0x1A5] == 9, "%d", c.rec[0x1A5]);
    }
    // The load: every band (weight over Strength's allowance)
    for (int load : {0, 512, 513, 768, 769, 1024, 1025}) {
        party::Character& c = s_fresh(7, 16, 0, 12, lv);
        const int allow = s_str(s_group(16, 0), 3);
        c.rec[0xFB + 3 * 2] = static_cast<uint8_t>((allow + load) & 0xFF);       // (gold coins weigh 1 each)
        c.rec[0xFC + 3 * 2] = static_cast<uint8_t>((allow + load) >> 8);
        rules::recalc(c, *names, d->facts);
        char t[80];
        snprintf(t, sizeof t, "carrying %d over the allowance", load);
        s_same(t, c);
    }
}

// ---- 2. The readied items' codes (0x3E 0x80 + code; GAME.OVR 0x28B84
// ready, 0x374A8 the stats): 0 the effect in 0x3D on and off, 1 the ring of
// wizardry (5th level), 3 Ogre Power 18/00, 5 the girdle
int s_find(int type, int code)
{
    for (size_t k = 0; k < s_items.size(); ++k)
        if (s_items[k].r[0x2E] == type && (code < 0 || s_items[k].r[0x3E] == code)) return static_cast<int>(k);
    return -1;
}
bool s_ready(int i)
{
    view_from = Screen::Game;
    screen = Screen::Items;
    ready_item(i, C);
    note_until = 0;
    screen = Screen::Game;
    return pt->sel()->items[i][0x34] != 0;
}

void s_codes()
{
    const int lv[8] = {0, 0, 9, 0, 0, 0, 0, 0};
    // Every code-0 item: its effect while readied, gone when put away
    for (const SItem& s : s_items) {
        if (s.r[0x3E] != 0x80 || !s.r[0x3D]) continue;
        party::Character& c = s_fresh(7, 15, 0, 12, lv);
        c.rec[0x12B] = 0xFF;                                    // (any class)
        rules::add_item(c, s.r);
        c.items[0][0x34] = 0;                                   // (monsters' items come readied)
        const bool on = s_ready(0) && c.has_affect(s.r[0x3D]);
        s_ready(0);
        const bool off = !c.items[0][0x34] && !c.has_affect(s.r[0x3D]);
        char t[120];
        snprintf(t, sizeof t, "%s (%s): effect 0x%02X readied, gone put away", s_name(s.r).c_str(), s.from.c_str(),
                 s.r[0x3D]);
        expect(PI, t, on && off);
    }
    // Strength items on Strength 15 (and 18/50): code 3 18/00, code 5 by 0x3D
    for (const SItem& s : s_items) {
        const int code = s.r[0x3E];
        if (code != 0x83 && code != 0x85) continue;
        for (int own : {15, 18}) {
            const int own00 = own == 18 ? 50 : 0;
            party::Character& c = s_fresh(7, own, own00, 12, lv);
            c.rec[0x12B] = 0xFF;
            rules::add_item(c, s.r);
            c.items[0][0x34] = 0;
            s_ready(0);
            int want = own, want00 = own00;
            if (code == 0x83) {
                want = s_col("it_ogre", 0);
                want00 = s_col("it_ogre", 1);
            } else {
                const int a = s.r[0x3D];
                want = a == 0 ? s_col("it_girdle", 0) : s_col("it_girdle", 1 + a);
                want00 = a == 0 ? s_col("it_girdle", 1) : 0;
            }
            if (want < own || (want == own && want00 < own00)) {
                want = own;
                want00 = own00;
            }
            const bool ok = c.rec[0x11] == want && c.rec[0x1C] == want00;
            s_ready(0);
            const bool back = c.rec[0x11] == own && c.rec[0x1C] == own00;
            char t[120];
            snprintf(t, sizeof t, "%s (%s) on Strength %d: %d/%02d, back when put away", s_name(s.r).c_str(),
                     s.from.c_str(), own, want, want00);
            expect(PI, t, ok && back, "%d/%02d", c.rec[0x11], c.rec[0x1C]);
        }
    }
    // Ogre Power and the girdle together: the most of them
    {
        const int og = s_find(72, 0x83), gi = s_find(42, 0x85);
        if (og >= 0 && gi >= 0) {
            party::Character& c = s_fresh(7, 15, 0, 12, lv);
            c.rec[0x12B] = 0xFF;
            rules::add_item(c, s_items[og].r);
            rules::add_item(c, s_items[gi].r);
            s_ready(0);
            s_ready(1);
            expect(PI, "Ogre Power and the Girdle of Giant Strength (0x3D 5): Strength 23 (DOSBox)", c.rec[0x11] == 23,
                   "%d", c.rec[0x11]);
            // DOSBox (ITEMTEST1, fighter 11, own Strength 15): THAC0 10 -> 7 with Ogre Power, damage 1D2+6
        }
    }
    // The ring of wizardry: a magic-user of 12th level - the 5th level's
    // spells doubled, the others as they were; put away, the 5th level's
    // memorized past the count are forgotten
    const int ring = s_find(65, 0x81);
    if (ring >= 0 && load_magic()) {
        const int mu[8] = {0, 0, 0, 0, 0, 12, 0, 0};
        party::Character& c = s_fresh(7, 12, 0, 12, mu);
        c.rec[0x13] = c.rec[0x12] = 18;
        classes::spell_slots(c, mrules->tables);
        uint8_t before[5];
        for (int k = 0; k < 5; ++k) before[k] = c.rec[0x137 + k];
        rules::add_item(c, s_items[ring].r);
        s_ready(0);
        bool ok = c.rec[0x137 + 4] == 2 * before[4];
        for (int k = 0; k < 4; ++k) ok = ok && c.rec[0x137 + k] == before[k];
        expect(PI, "the Ring of Wizardry readied: 5th level slots doubled, 1st-4th as they were (GAME.OVR 0x28BDD)", ok,
               "%d %d %d %d %d (before %d %d %d %d %d)", c.rec[0x137], c.rec[0x138], c.rec[0x139], c.rec[0x13A],
               c.rec[0x13B], before[0], before[1], before[2], before[3], before[4]);
        // as many 5th level spells memorized as it allows, and a 1st level one
        int n5 = 0, s5 = 0, s1 = 0;
        for (int s = 1; s < mrules->tables.lay.spell_count && (!s5 || !s1); ++s)
            if (mrules->tables.spell_class(s) == 2) {
                if (!s5 && mrules->tables.spell_level(s) == 5) s5 = s;
                if (!s1 && mrules->tables.spell_level(s) == 1) s1 = s;
            }
        for (int k = 0; k < 84; ++k) c.rec[0x1E + k] = 0;
        for (int k = 0; k < c.rec[0x13B] && k < 20; ++k) c.rec[0x1E + n5++] = static_cast<uint8_t>(s5);
        c.rec[0x1E + n5] = static_cast<uint8_t>(s1);
        s_ready(0);
        int held5 = 0, held1 = 0;
        for (int k = 0; k < 84; ++k) {
            held5 += c.rec[0x1E + k] == s5;
            held1 += c.rec[0x1E + k] == s1;
        }
        expect(PI, "the Ring of Wizardry put away: back to the 5th level's count, the spells past it forgotten",
               c.rec[0x13B] == before[4] && held5 == before[4] && held1 == 1, "slots %d held %d / %d", c.rec[0x13B],
               held5, held1);
    }
    // The Boots of Speed: twice the movement (0x3A7FC; DOSBox: 12 -> 24)
    const int boots = s_find(45, 0x80);
    if (boots >= 0) {
        party::Character& c = s_fresh(7, 15, 0, 12, lv);
        c.rec[0x12B] = 0xFF;
        rules::add_item(c, s_items[boots].r);
        s_ready(0);
        expect(PI, "the Boots of Speed: movement 24 (DOSBox: 12 -> 24), the View's AC unchanged",
               c.rec[0x1A5] == 24 && c.rec[0x19A] == 50, "move %d AC %d", c.rec[0x1A5], c.rec[0x19A]);
        s_ready(0);
        expect(PI, "the Boots of Speed put away: movement 12", c.rec[0x1A5] == 12, "%d", c.rec[0x1A5]);
    }
}

// ---- 3. Who may ready what (the ITEMS row's classes against the class
// flags, DS 0x4C92; one a slot, two rings, one pile of arrows, two hands)
void s_ready_rules()
{
    const char* cls_name[7] = {"cleric", "druid", "fighter", "paladin", "ranger", "magic-user", "thief"};
    const auto& fl = b_rows("it_class_flags");
    for (int k : {0, 2, 3, 4, 5, 6}) {
        int lv[8] = {};
        lv[k] = 9;
        int bad = 0, n = 0;
        std::string first;
        const int flag = !fl.empty() && static_cast<int>(fl[0].size()) > k ? fl[0][k] : 0;
        {
            party::Character& c = s_fresh(7, 17, 0, 12, lv);
            char t[80];
            snprintf(t, sizeof t, "a %s's class flags", cls_name[k]);
            expect(PI, t, c.rec[0x12B] == flag, "%02X, the table's %02X", c.rec[0x12B], flag);
        }
        for (const SItem& s : s_items) {
            party::Character& c = s_fresh(7, 17, 0, 12, lv);
            rules::add_item(c, s.r);
            c.items[0][0x36] = 0;
            c.items[0][0x34] = 0;
            const bool want = (s_type(s.r[0x2E]).classes & flag) != 0 && s_type(s.r[0x2E]).hands <= 2;
            const bool got = s_ready(0);
            ++n;
            if (want != got) {
                ++bad;
                if (first.empty()) first = s_name(s.r) + " (" + s.from + ")";
            }
        }
        char t[100];
        snprintf(t, sizeof t, "a %s readies the items its class may use (%d records)", cls_name[k], n);
        expect(PI, t, bad == 0, "%d wrong, first %s", bad, first.c_str());
    }
    // one a slot, two rings, the third refused, two hands
    const int lv[8] = {0, 0, 9, 0, 0, 0, 0, 0};
    const int sword = s_find(18, -1), shield = s_find(37, -1), two_h = s_find(20, -1);
    const int ring1 = s_find(59, -1), ring2 = s_find(46, -1), ring3 = s_find(65, -1);
    if (sword >= 0 && shield >= 0 && two_h >= 0) {
        party::Character& c = s_fresh(7, 17, 0, 12, lv);
        rules::add_item(c, s_items[two_h].r);
        rules::add_item(c, s_items[shield].r);
        rules::add_item(c, s_items[sword].r);
        for (int k = 0; k < 3; ++k) c.items[k][0x36] = 0;
        expect(PI, "a two-handed sword readied", s_ready(0));
        expect(PI, "then a shield: hands full", !s_ready(1));
        expect(PI, "then a long sword: already using a weapon", !s_ready(2));
        s_ready(0);
        expect(PI, "a shield and a long sword", s_ready(1) && s_ready(2));
        expect(PI, "then the two-handed sword: refused", !s_ready(0));
    }
    if (ring1 >= 0 && ring2 >= 0 && ring3 >= 0) {
        party::Character& c = s_fresh(7, 17, 0, 12, lv);
        c.rec[0x12B] = 0xFF;
        rules::add_item(c, s_items[ring1].r);
        rules::add_item(c, s_items[ring2].r);
        rules::add_item(c, s_items[ring3].r);
        expect(PI, "two rings, the third refused", s_ready(0) && s_ready(1) && !s_ready(2));
    }
    // one pile of arrows
    std::vector<int> arrows;
    for (size_t k = 0; k < s_items.size() && arrows.size() < 2; ++k)
        if (s_items[k].r[0x2E] == 30) arrows.push_back(static_cast<int>(k));
    if (arrows.size() == 2) {
        party::Character& c = s_fresh(7, 17, 0, 12, lv);
        rules::add_item(c, s_items[arrows[0]].r);
        rules::add_item(c, s_items[arrows[1]].r);
        expect(PI, "one pile of arrows readied, a second refused", s_ready(0) && !s_ready(1));
    }
    // a cursed item stays readied
    if (sword >= 0) {
        party::Character& c = s_fresh(7, 17, 0, 12, lv);
        rules::add_item(c, s_items[sword].r);
        c.items[0][0x34] = 1;
        c.items[0][0x36] = 1;
        s_ready(0);
        expect(PI, "a cursed sword can't be put away (\"It's Cursed!\")", c.items[0][0x34] != 0);
    }
}

// ---- 4. Using items (GAME.OVR 0x2994A): a scroll (types 39 / 40) is read,
// else an item with 0x3D's spell and 0x3E under 0x80; a use takes one of a
// pile, else a charge (0x3C; 0 never runs out), gone at 0
void s_use()
{
    const magic::Scrolls sc = scroll_facts();
    for (const SItem& s : s_items) {
        const bool scroll = s.r[0x2E] == 39 || s.r[0x2E] == 40;
        const bool usable = !scroll && s.r[0x3D] != 0 && s.r[0x3E] < 0x80;
        char t[120];
        snprintf(t, sizeof t, "%s (%s): %s", s_name(s.r).c_str(), s.from.c_str(),
                 scroll ? "a scroll" : usable ? "used: its spell" : "not used");
        expect(PI, t, magic::is_scroll(sc, s.r) == scroll && magic::usable(sc, s.r) == usable &&
                          (!usable || magic::item_spell(s.r) == (s.r[0x3D] & 0x7F)));
        if (!usable) continue;
        uint8_t r[items::kRecordSize];
        memcpy(r, s.r, sizeof r);
        int uses = 0;
        bool gone = false;
        for (; uses < 300 && !gone; ++uses) gone = magic::use_charge(r);
        const int want = s.r[0x3C] == 0 ? 300 : (s.r[0x39] > 1 ? s.r[0x39] - 1 : 0) + s.r[0x3C];
        snprintf(t, sizeof t, "%s (%s): %d uses (0x3C %d, count %d)", s_name(s.r).c_str(), s.from.c_str(), want, s.r[0x3C],
                 s.r[0x39]);
        expect(PI, t, uses == want && (s.r[0x3C] == 0 || gone), "%d", uses);
    }
}


// Uses item i of member `who` on the items screen (outside fights), the
// cast driven to its end on `target` (behave_items.inc's b_use)
void s_use_item(int who, int i, int target)
{
    pt->selected = who;
    view_from = Screen::Game;
    screen = Screen::Items;
    use_item(i, C);
    for (int k = 0; k < 400; ++k) {
        if (screen == Screen::YesNo) {
            tap_word('Y');
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
    load_magic();
}

// The potions and the elixir used outside a fight (their spells' handlers,
// secret_facts_2.md 14: 62 / 99 2d4 + 2 "is Healed" - GAME.OVR 0x30795,
// 0x31E9D; 59 giant strength: Strength 21 in the stats - 0x3067A, 0x377C3;
// 57 speed: Hastened - 0x305ED; 63 the elixir: age - (1d4 + 1) - 0x30813;
// 97 invisibility: Invisible (spell) - 0x31E33), a dose used each time
void s_use_effects()
{
    if (!load_magic()) return;
    const int lv[8] = {0, 0, 9, 0, 0, 0, 0, 0};
    for (const SItem& s : s_items) {
        const int sp = s.r[0x3D];
        if (s.r[0x3E] >= 0x80 || !(sp == 62 || sp == 99 || sp == 59 || sp == 57 || sp == 63 || sp == 97)) continue;
        for (int dice = 1; dice <= 2; ++dice) {
            party::Character& c = s_fresh(7, 15, 0, 12, lv);
            c.rec[0x78] = 60;
            c.rec[0x1A4] = 30;
            c.rec[0x76] = 40;
            c.rec[0x77] = 0;
            c.rec[0x12B] = 0xFF;
            rules::add_item(c, s.r);
            c.items[0][0x34] = 1;
            c.items[0][0x35] = 0;
            rules::recalc(c, *names, d->facts);
            if (dice == 1) dice_high();
            else dice_low();
            s_use_item(0, 0, 0);
            dice_usual();
            create::g_die_hook = nullptr;
            const int age = c.rec[0x76] | c.rec[0x77] << 8;
            bool ok = false;
            char w[140];
            if (sp == 62 || sp == 99) {
                const int want = 30 + (dice == 1 ? 10 : 4);
                ok = c.hp() == want;
                snprintf(w, sizeof w, "%s (%s): healed to %d (2d4 + 2, dice %s)", s_name(s.r).c_str(), s.from.c_str(), want,
                         dice == 1 ? "high" : "low");
            } else if (sp == 59) {
                ok = c.rec[0x11] == 21;
                snprintf(w, sizeof w, "%s (%s): Strength 21", s_name(s.r).c_str(), s.from.c_str());
            } else if (sp == 57) {
                ok = c.has_affect(0x27);
                snprintf(w, sizeof w, "%s (%s): Hastened (0x27)", s_name(s.r).c_str(), s.from.c_str());
            } else if (sp == 63) {
                const int want = 40 - (dice == 1 ? 5 : 2);
                ok = age == want;
                snprintf(w, sizeof w, "%s (%s): 40 years to %d", s_name(s.r).c_str(), s.from.c_str(), want);
            } else {
                ok = c.has_affect(0x19);
                snprintf(w, sizeof w, "%s (%s): Invisible (0x19)", s_name(s.r).c_str(), s.from.c_str());
            }
            const int doses = s.r[0x3C];
            const bool dose_ok = doses <= 1 ? c.n_items == 0 : c.n_items == 1 && c.items[0][0x3C] == doses - 1;
            expect(PI, w, ok && dose_ok, "HP %d Str %d age %d effects %d, %d items", c.hp(), c.rec[0x11], age,
                   c.n_affects, c.n_items);
        }
    }
}

// ---- 5. Scrolls (GAME.OVR 0x2CF28 the list, 0x2C64B who reads which
// spell, 0x32ACA a spell read off: its last line, word 1 down, gone below
// "1 Spell")
void s_scrolls()
{
    if (!load_magic()) return;
    const magic::Scrolls sc = scroll_facts();
    const classes::Tables& t = mrules->tables;
    struct Reader {
        int cls, level;
        const char* what;
    } readers[] = {{5, 9, "magic-user 9"}, {0, 9, "cleric 9"}, {2, 9, "fighter 9"}, {6, 11, "thief 11"},
                   {6, 10, "thief 10"}, {4, 9, "ranger 9"}, {3, 9, "paladin 9"}};
    for (const SItem& s : s_items) {
        if (s.r[0x2E] != 39 && s.r[0x2E] != 40) continue;
        for (const Reader& rd : readers) {
            int lv[8] = {};
            lv[rd.cls] = rd.level;
            party::Character& c = s_fresh(7, 12, 0, 12, lv);
            c.rec[0x13] = c.rec[0x12] = 12;
            c.rec[0x15] = c.rec[0x14] = 12;
            rules::add_item(c, s.r);
            c.items[0][0x35] = 0;                          // (identified)
            uint8_t ids[8];
            const int n = magic::scroll_list(c, t, sc, 0, ids, 8);
            int want = 0;
            for (int k = 0; k < 3; ++k) {
                const int sp = s.r[0x3C + k] & 0x7F;
                if (!sp) continue;
                const int cl = t.spell_class(sp);
                const int cls = rd.cls, L = rd.level;
                bool ok = false;
                if (cl == 0) ok = cls == 0 || (cls == 3 && L > 8);
                else if (cl == 1) ok = cls == 4 && L > 6;
                else if (cl == 2) ok = cls == 5 || (cls == 4 && L > 8) || (cls == 6 && L > 10);
                want += ok;
            }
            char tx[120];
            snprintf(tx, sizeof tx, "%s (%s) read by a %s: %d spells", s_name(s.r).c_str(), s.from.c_str(), rd.what, want);
            expect(PI, tx, n == want, "%d", n);
        }
        // spells read off: each from its line, word 1 down, gone below "1 Spell"
        const int lv[8] = {0, 0, 0, 0, 0, 9, 0, 0};
        party::Character& c = s_fresh(7, 12, 0, 12, lv);
        rules::add_item(c, s.r);
        int spells = 0;
        for (int k = 0; k < 3; ++k) spells += (s.r[0x3C + k] & 0x7F) != 0;
        bool words_ok = true;
        for (int read = 0; read < 3 && c.n_items; ++read) {
            int sp = 0;
            for (int k = 0; k < 3 && !sp; ++k) sp = c.items[0][0x3C + k] & 0x7F;
            if (!sp) break;
            const int w = c.items[0][0x2F];
            magic::scroll_used(c, sc, 0, sp);
            if (c.n_items && c.items[0][0x2F] != w - 1) words_ok = false;
        }
        char tx[140];
        snprintf(tx, sizeof tx, "%s (%s): read to the end (%d spells), word 1 down each, gone under \"1 Spell\"",
                 s_name(s.r).c_str(), s.from.c_str(), spells);
        const bool gone_ok = (s.r[0x2F] - spells < 100) ? c.n_items == 0 : c.n_items == 1;
        expect(PI, tx, words_ok && gone_ok, "%d items, word 1 %d", c.n_items, c.n_items ? c.items[0][0x2F] : -1);
    }
}

// ---- 6. Prices: Sell (GAME.OVR 0x29D61): value / 2, arrows / quarrels
// x count / 20, the other piles x count; Join (0x2971B); Halve (0x2928B)
void s_money()
{
    const int div = s_col("it_sell", 0), per = s_col("it_sell", 1);
    for (const SItem& s : s_items) {
        int v = (s.r[0x3A] | s.r[0x3B] << 8) / div;
        if (s.r[0x39] > 1) {
            if (s.r[0x2E] == 30 || s.r[0x2E] == 12) v = ((s.r[0x39] * v) & 0xFFFF) / per;
            else v = (s.r[0x39] * v) & 0xFFFF;
        }
        char t[120];
        snprintf(t, sizeof t, "%s (%s) sells for %d", s_name(s.r).c_str(), s.from.c_str(), v);
        expect(PI, t, rules::sell_value(s.r, d->facts) == v, "%d", rules::sell_value(s.r, d->facts));
    }
    // Join: arrows +1 of two weights (ITEM1's 4 and 3) go together; a single
    // item (no count) can't be joined
    const int lv[8] = {0, 0, 9, 0, 0, 0, 0, 0};
    std::vector<int> plus1;
    for (size_t k = 0; k < s_items.size(); ++k)
        if (s_items[k].r[0x2E] == 30 && s_items[k].r[0x32] == 1) plus1.push_back(static_cast<int>(k));
    for (size_t a = 0; a < plus1.size(); ++a)
        for (size_t b = a + 1; b < plus1.size(); ++b) {
            const uint8_t* x = s_items[plus1[a]].r;
            const uint8_t* y = s_items[plus1[b]].r;
            party::Character& c = s_fresh(7, 17, 0, 12, lv);
            rules::add_item(c, x);
            rules::add_item(c, y);
            const bool same = x[0x2F] == y[0x2F] && x[0x30] == y[0x30] && x[0x31] == y[0x31] && x[0x33] == y[0x33] &&
                              x[0x36] == y[0x36] && y[0x3C] < 2;
            rules::join(c, 0, true);
            const int want_n = same ? 1 : 2;
            char t[160];
            snprintf(t, sizeof t, "Join %s (%s, weight %d) with %s (%s, weight %d): %s", s_name(x).c_str(),
                     s_items[plus1[a]].from.c_str(), x[0x37], s_name(y).c_str(), s_items[plus1[b]].from.c_str(), y[0x37],
                     same ? "one pile" : "not");
            expect(PI, t, c.n_items == want_n && (!same || c.items[0][0x39] == std::min(255, x[0x39] + y[0x39])),
                   "%d items, count %d", c.n_items, c.items[0][0x39]);
        }
    {
        const int sword = s_find(18, -1);
        party::Character& c = s_fresh(7, 17, 0, 12, lv);
        rules::add_item(c, s_items[sword].r);
        rules::add_item(c, s_items[sword].r);
        expect(PI, "Join two swords (no count): \"That cannot be joined with other items.\"",
               rules::join(c, 0, true) == 0 && c.n_items == 2);
    }
    // Halve: every counted record
    for (const SItem& s : s_items) {
        if (s.r[0x39] < 2) continue;
        party::Character& c = s_fresh(7, 17, 0, 12, lv);
        rules::add_item(c, s.r);
        c.items[0][0x34] = 1;
        const bool ok = rules::halve(c, 0);
        const int half = s.r[0x39] / 2;
        char t[120];
        snprintf(t, sizeof t, "Halve %s (%s): %d and %d", s_name(s.r).c_str(), s.from.c_str(), s.r[0x39] - half, half);
        expect(PI, t, ok && c.n_items == 2 && c.items[0][0x39] == s.r[0x39] - half && c.items[1][0x39] == half &&
                          !c.items[1][0x34]);
    }
}

// ---- 7. Secret's item effects in fights (its phase lists, GAME.OVR
// 0x3624E; the handlers DS 0x87EA + 4 x (effect - 1))
void s_fights()
{
    if (!new_fight()) {
        expect(PI, "a fight", false);
        return;
    }
    const combat::ItemFx& ix = d->prof->fight.facts.items;
    const int a = 0, t = 6;
    // Giant slaying (0x4B): against a giant (flags 2 & 1) 6d6 + Strength's,
    // the d20 + 1; against others the weapon's own
    for (int giant : {1, 0}) {
        put(a, 0, 10, 10, 60, 9);
        give(a, ix.giant_slayer);
        put(t, 1, 11, 10, 200, 12);
        F(t).flags2 = static_cast<uint8_t>(giant);
        combat::Fighter& f = F(a);
        f.attacks[0] = 1;
        f.attacks[1] = 0;
        f.rec[0x199] = 40;
        f.rec[0x19E] = 1;
        f.rec[0x1A0] = 8;
        f.rec[0x1A2] = 0;
        f.rec[0x11] = 18;
        f.rec[0x1C] = 100;                  // (Strength 18/00: damage + 6)
        f.rec[0x125] = 1;
        F(t).rec[0x19A] = F(t).rec[0x19B] = 50;
        F(t).received = 0;
        F(t).turns = 0;
        s_dice(1, 15);                      // d20 15: hits either way; dice high
        const combat::Attack r = combat::attack(fg->b, a, t, names, rng, false);
        create::g_die_hook = nullptr;
        const int dmg = r.n && r.hits[0].hit ? r.hits[0].damage : -1;
        const int want = giant ? 6 * 6 + s_str(s_group(18, 100), 2) : 8;
        char w[100];
        snprintf(w, sizeof w, "giant slaying against %s: %d damage (dice high)", giant ? "a giant" : "another", want);
        expect(PI, w, dmg == want, "%d", dmg);
        // the d20 + 1
        put(t, 1, 11, 10, 200, 12);
        F(t).flags2 = static_cast<uint8_t>(giant);
        F(t).rec[0x19A] = F(t).rec[0x19B] = 50;
        F(t).received = 0;
        F(t).turns = 0;
        f.attacks[0] = 1;
        f.attacks[1] = 0;
        f.rec[0x199] = 40;
        s_dice(1, 9);                       // 9 + 40 = 49: only the + 1 makes it
        const combat::Attack r2 = combat::attack(fg->b, a, t, names, rng, false);
        create::g_die_hook = nullptr;
        snprintf(w, sizeof w, "giant slaying against %s: a 9 needing 10 %s", giant ? "a giant" : "another",
                 giant ? "hits" : "misses");
        expect(PI, w, r2.n == 1 && r2.hits[0].hit == (giant != 0));
    }
    // The Boots of Speed (0x4A): AC 2 better against an attack
    {
        put(t, 0, 11, 10, 60);
        give(t, ix.boots);
        F(t).rec[0x19A] = F(t).rec[0x19B] = 50;
        put(a, 1, 10, 10, 60, 5);
        const int hits = hit_with(a, t, 6);     // 45 + 6 = 51: hits AC 50, not 52
        expect(PI, "the Boots of Speed: AC 2 better against an attack (phase 0xB, 0x13130)", hits == 0, "%d hits", hits);
        put(t, 0, 11, 10, 60);
        F(t).rec[0x19A] = F(t).rec[0x19B] = 50;
        expect(PI, "... without them the same blow hits", hit_with(a, t, 6) == 1);
    }
    // The luck stone (0x4E): + 1 to every save (phase 0xC, 0x1333F)
    {
        put(t, 0, 11, 10, 60);
        give(t, ix.luck);
        for (int k = 0; k < 5; ++k) F(t).rec[0xDF + k] = 12;
        F(t).rec[0x186] = 0;
        int made = 0;
        for (int kind = 0; kind < 5; ++kind) {
            s_dice(1, 11);
            made += combat::saving_throw(F(t), kind, 0, rng);
        }
        create::g_die_hook = nullptr;
        expect(PI, "the Stone of Good Luck: an 11 makes a save of 12 (each of the five kinds)", made == 5, "%d", made);
        put(t, 0, 11, 10, 60);
        for (int k = 0; k < 5; ++k) F(t).rec[0xDF + k] = 12;
        F(t).rec[0x186] = 0;
        s_dice(1, 11);
        expect(PI, "... without it, not", !combat::saving_throw(F(t), 0, 0, rng));
        create::g_die_hook = nullptr;
    }
    // The ring of fire resistance (0x3D): fire without magic nothing, magic
    // fire 2 a die less (at least 1 a die)
    {
        struct Case {
            int kind, amount, dice, want;
        } cases[] = {{1, 20, 4, 0}, {0x21, 30, 0, 0}, {9, 30, 5, 20}, {9, 6, 5, 5}, {9, 12, 3, 6}, {2, 20, 4, 20}};
        for (const Case& k : cases) {
            put(t, 0, 11, 10, 200);
            give(t, ix.fire_ring);
            combat::Harm h;
            h.kind = k.kind;
            h.dice = k.dice;
            bool down = false;
            const int got = combat::harm(fg->b, t, k.amount, h, rng, &down);
            char w[100];
            snprintf(w, sizeof w, "the ring of fire resistance: %d of kind 0x%02X (%d dice) - %d", k.amount, k.kind, k.dice,
                     k.want);
            expect(PI, w, got == k.want, "%d", got);
        }
    }
    // Displacement (0x59): the original never makes an attack miss (its
    // handler acts at its wearer's own turn); with the mistakes fixed the
    // first attack on it a fight misses
    {
        const bool fx = rules::fixes();
        for (int on : {1, 0}) {
            rules::set_fixes(on != 0);
            put(t, 0, 11, 10, 60);
            give(t, ix.displace);
            F(t).rec[0x19A] = F(t).rec[0x19B] = 50;
            put(a, 1, 10, 10, 60, 5);
            const int hits = hit_with(a, t, 20);
            char w[100];
            snprintf(w, sizeof w, "displacement, fixes %s: the first attack (a 20) %s", on ? "on" : "off",
                     on ? "misses" : "hits");
            expect(PI, w, hits == (on ? 0 : 1), "%d hits", hits);
        }
        rules::set_fixes(fx);
    }
    // The Periapt of Health (0x4C): disease resisted with the mistakes fixed
    // (AD&D's), not in the original (an empty handler)
    {
        const bool fx = rules::fixes();
        for (int on : {1, 0}) {
            rules::set_fixes(on != 0);
            put(t, 0, 11, 10, 60);
            give(t, ix.no_disease);
            combat::Harm h;
            h.level = 9;
            const bool res = combat::resists(fg->b, t, d->prof->fight.facts.sp.ill, h, rng);
            char w[100];
            snprintf(w, sizeof w, "the Periapt of Health, fixes %s: disease %s", on ? "on" : "off", on ? "resisted" : "not");
            expect(PI, w, res == (on != 0));
        }
        rules::set_fixes(fx);
    }
    // A mirror / silver shield (0x48): readied, a stone gaze sent back
    // (GAME.OVR 0x12522) - the effect is what the gaze looks for
    expect(PI, "the mirror's effect is the one the stone gaze looks for (0x48)", ix.reflect == 0x48);
    // The ring of invisibility (0x38): invisible at a fight's start (phase 8)
    {
        put(t, 0, 11, 10, 60);
        give(t, ix.ring_invisible);
        combat::battle_start(fg->b, &rng);
        expect(PI, "the Ring of Invisibility: invisible as the fight starts (phase 8, 0x12387)",
               F(t).has(d->prof->fight.facts.invisible));
    }
    create::g_die_hook = nullptr;
}

// ---- 8. Random items (TREASURE 0x80 + n; GAME.OVR 0x2BD8, create_item
// 0x34AB3): every band of every roll table gives its type, each type made
// as the original makes it
struct SKind {
    std::vector<int> faces;
    int type;
};
void s_make(std::vector<int> faces, int rest, uint8_t* rec, const treasure::Rows& rows)
{
    b_faces = faces;
    b_face_at = 0;
    b_dice_mode = rest;
    create::g_die_hook = b_die;
    treasure::make(rec, *d->prof->random_items, rows, rng, nullptr);
    create::g_die_hook = nullptr;
}
void s_treasure()
{
    const profile::Profile* p = d->prof;
    if (!p->random_items || !p->random_items->secret) {
        expect(PI, "random items made in Secret", false);
        return;
    }
    // the program's rows and spells (the oracle's: as play reads them)
    treasure::Rows rows{};
    for (int r = 0; r < 7; ++r) {
        const auto* o = b_row("it_row", r);
        for (int k = 0; o && k < 8; ++k) rows.w[1 + 8 * r + k] = static_cast<uint16_t>((*o)[1 + k]);
    }
    for (int lv = 1; lv <= 7; ++lv) {
        const auto* mu = b_row("it_muspells", lv);
        for (size_t k = 1; mu && k < mu->size(); ++k) {
            if (k <= 10) rows.spells[0][lv - 1][k] = static_cast<uint8_t>((*mu)[k]);
            else if (lv < 7) rows.spells[0][lv][0] = static_cast<uint8_t>((*mu)[k]);     // (its 11th: the next's [0])
        }
        const auto* cl = b_row("it_clspells", lv);
        for (size_t k = 1; cl && k < cl->size() && k <= 10; ++k) rows.spells[1][lv - 1][k] = static_cast<uint8_t>((*cl)[k]);
    }
    // The kinds (d100 1-31 weapons: d100 1-20 d10, 21-44 d3, 45-60 d20, 61-100 d4 ...)
    const SKind kinds[] = {
        {{1, 1, 1}, 6},   {{1, 20, 2}, 6},  {{1, 1, 3}, 8},   {{1, 1, 6}, 8},   {{1, 1, 7}, 70},  {{1, 1, 8}, 70},
        {{1, 1, 9}, 15},  {{1, 1, 10}, 29}, {{1, 21, 1}, 5},  {{1, 44, 2}, 12}, {{1, 30, 3}, 30}, {{31, 45, 3}, 7},
        {{1, 60, 4}, 24}, {{1, 50, 6}, 24}, {{1, 50, 7}, 22}, {{1, 50, 9}, 22}, {{1, 50, 10}, 25}, {{1, 50, 12}, 25},
        {{1, 50, 13}, 23}, {{1, 50, 14}, 23}, {{1, 50, 15}, 28}, {{1, 50, 17}, 28}, {{1, 50, 18}, 15},
        {{1, 50, 20}, 15}, {{1, 61, 1, 1}, 18}, {{1, 100, 3, 8}, 18}, {{1, 70, 1, 9}, 17}, {{1, 70, 2, 13}, 17},
        {{1, 70, 1, 14}, 13}, {{1, 70, 1, 15}, 16}, {{1, 70, 1, 16}, 19}, {{1, 70, 1, 18}, 19}, {{1, 70, 1, 19}, 20},
        {{1, 70, 1, 20}, 20}, {{1, 70, 4, 1}, 1}, {{1, 70, 4, 2}, 4}, {{1, 70, 4, 3}, 4}, {{1, 70, 4, 4}, 9},
        {{1, 70, 4, 5}, 9}, {{1, 70, 4, 6}, 10}, {{1, 70, 4, 7}, 14}, {{1, 70, 4, 8}, 71}, {{32}, 39}, {{62}, 39},
        {{63}, 40}, {{70}, 40}, {{71}, 37}, {{80}, 37}, {{81}, 59}, {{85}, 59}, {{86, 1}, 31}, {{86, 3}, 31},
        {{86, 4}, 35}, {{90, 11}, 35}, {{90, 12}, 36}, {{90, 16}, 36}, {{90, 17}, 32}, {{90, 18}, 33}, {{95, 19}, 34},
        {{95, 20}, 34}, {{96, 1}, 50}, {{96, 2}, 50}, {{97, 3}, 47}, {{99, 4}, 47}, {{100, 5}, 52}, {{100, 6}, 53},
    };
    std::vector<int> seen_types;
    for (const SKind& k : kinds) {
        uint8_t rec[items::kRecordSize];
        s_make(k.faces, 1, rec, rows);
        std::string f;
        for (int x : k.faces) f += " " + std::to_string(x);
        char w[100];
        snprintf(w, sizeof w, "random item, dice%s: type %d", f.c_str(), k.type);
        expect(PI, w, rec[0x2E] == k.type, "%d", rec[0x2E]);
        if (std::find(seen_types.begin(), seen_types.end(), k.type) == seen_types.end()) seen_types.push_back(k.type);
    }
    auto pre_of = [&](int type) -> std::vector<int> {
        for (const SKind& k : kinds)
            if (k.type == type) return k.faces;
        return {};
    };
    // Each type made: the plus die (1 / 6 / 7 / 9 / 10: +1 +1 +2 +2 +3) and
    // what create_item gives it (GAME.OVR 0x34B2F-0x34F60)
    const int weights_t[] = {1, 17, 6, 14, 15, 23, 25, 37, 29, 9, 8, 16, 24, 70, 31, 71, 4, 50, 5, 10, 18, 22, 7, 13, 19,
                             20, 32, 33, 34, 35, 36, 28, 59};
    const int weights_w[] = {75, 75, 50, 50, 50, 50, 50, 50, 50, 125, 100, 100, 100, 150, 150, 175, 10, 10, 5, 60, 60, 80,
                             20, 40, 35, 250, 250, 400, 300, 350, 450, 1, 1};
    auto weight_of = [&](int type) {
        for (size_t k = 0; k < sizeof weights_t / sizeof weights_t[0]; ++k)
            if (weights_t[k] == type) return weights_w[k];
        return 4;
    };
    auto value_of = [](int type) {
        switch (type) {
        case 12: case 5: case 30: return 50;
        case 37: return 1250;
        case 32: case 33: case 50: return 1500;
        case 34: return 1750;
        case 35: return 2000;
        case 36: return 2500;
        default: return 1000;
        }
    };
    for (int type : seen_types) {
        if (type == 39 || type == 40 || type == 47 || type == 52 || type == 53) continue;
        for (int pd : {1, 6, 7, 9, 10}) {
            std::vector<int> faces = pre_of(type);
            faces.push_back(pd);
            if (type == 7) faces.push_back(1);  // (the javelin's d5: not lightning)
            uint8_t rec[items::kRecordSize];
            s_make(faces, 1, rec, rows);
            int plus = pd <= 6 ? 1 : pd <= 9 ? 2 : 3;
            int w3 = type, w2 = 0x6E + plus, w1 = 0, hidden = 6, saves = 0;
            if (type == 29) { w2 = 28; w1 = 0x6E + plus; hidden = 4; }
            if (type == 31) { w2 = 48; w1 = 0x6E + plus; hidden = 4; }
            if (type >= 32 && type <= 36) { w2 = 47; w1 = 0x6E + plus; hidden = 4; }
            if (type == 50) { plus = 4; w2 = 90; w1 = 107; }
            if (type == 59) { plus = 1; w3 = 46; w2 = 49; w1 = 0x6F; saves = 1; }
            const int weight = weight_of(type);
            const int count = weight == 4 || type == 5 ? 20 : 0;
            const int value = plus * value_of(type);
            const bool ok = rec[0x2E] == type && rec[0x31] == w3 && rec[0x30] == w2 && rec[0x2F] == w1 &&
                            rec[0x35] == hidden && rec[0x32] == plus && rec[0x33] == saves &&
                            (rec[0x37] | rec[0x38] << 8) == weight && rec[0x39] == count &&
                            (rec[0x3A] | rec[0x3B] << 8) == value && !rec[0x34] && !rec[0x36] && !rec[0x3C] &&
                            !rec[0x3D] && !rec[0x3E];
            char w[140];
            snprintf(w, sizeof w, "random %s (type %d, plus die %d): +%d, weight %d, count %d, value %d",
                     s_name(rec).c_str(), type, pd, plus, weight, count, value);
            expect(PI, w, ok, "words %d %d %d hidden %d plus %d saves %d weight %d count %d value %d", rec[0x31], rec[0x30],
                   rec[0x2F], rec[0x35], rec[0x32], rec[0x33], rec[0x37] | rec[0x38] << 8, rec[0x39],
                   rec[0x3A] | rec[0x3B] << 8);
        }
    }
    auto row_ok = [&](const uint8_t* rec, int r, const char* what) {
        const auto* o = b_row("it_row", r);
        bool ok = o != nullptr;
        if (o) {
            const auto& v = *o;
            ok = rec[0x2F] == (v[1] & 0xFF) && rec[0x30] == (v[2] & 0xFF) && rec[0x31] == (v[3] & 0xFF) &&
                 (rec[0x37] | rec[0x38] << 8) == v[4] && (rec[0x3A] | rec[0x3B] << 8) == v[5] && rec[0x3C] == v[6] &&
                 rec[0x3D] == v[7] && rec[0x3E] == v[8] && rec[0x32] == 1 && rec[0x33] == 0 && rec[0x39] == 0 &&
                 rec[0x35] == 6;
        }
        char w[120];
        snprintf(w, sizeof w, "random %s: %s (row %d)", s_name(rec).c_str(), what, r);
        expect(PI, w, ok);
    };
    {
        std::vector<int> f = pre_of(7);
        f.push_back(1);
        f.push_back(5);
        uint8_t rec[items::kRecordSize];
        s_make(f, 1, rec, rows);
        expect(PI, "a random javelin, d5 5: the javelin of lightning", rec[0x2E] == 7);
        row_ok(rec, 6, "the javelin of lightning");
    }
    // Potions (d8: 1-2 row 0, 3-4 row 2, 5-6 row 3, 7-8 row 5), the wand (row 4), giant strength (row 1)
    for (int p8 = 1; p8 <= 8; ++p8) {
        uint8_t rec[items::kRecordSize];
        s_make({97, 3, p8}, 1, rec, rows);
        const int r = p8 <= 2 ? 0 : p8 <= 4 ? 2 : p8 <= 6 ? 3 : 5;
        char w[40];
        snprintf(w, sizeof w, "a potion, d8 %d", p8);
        row_ok(rec, r, w);
    }
    for (int k : {5, 6}) {
        uint8_t rec[items::kRecordSize];
        s_make({100, k}, 1, rec, rows);
        row_ok(rec, k == 5 ? 4 : 1, k == 5 ? "the wand" : "giant strength");
    }
    // Scrolls: d3 spells, each a level die and that level's spell; value
    // 150 a level; plus 1, weight 25; the mage's hidden 4, the clerics' 0
    for (int mu = 1; mu >= 0; --mu)
        for (int lv = 1; lv <= (mu ? 7 : 6); ++lv) {
            const auto* sp = b_row(mu ? "it_muspells" : "it_clspells", lv);
            if (!sp) continue;
            for (size_t k = 1; k < sp->size(); ++k) {
                uint8_t rec[items::kRecordSize];
                s_make({mu ? 40 : 65, 1, lv, static_cast<int>(k)}, 0, rec, rows);
                const bool ok = rec[0x2E] == (mu ? 39 : 40) && rec[0x2F] == 100 && rec[0x30] == 39 &&
                                rec[0x31] == (mu ? 40 : 38) && rec[0x32] == 1 && rec[0x37] == 25 && rec[0x39] == 0 &&
                                (rec[0x3A] | rec[0x3B] << 8) == 150 * lv && rec[0x3C] == (*sp)[k] && !rec[0x3D] &&
                                !rec[0x3E] && rec[0x35] == (mu ? 4 : 0);
                char w[120];
                snprintf(w, sizeof w, "a random %s scroll: level %d roll %zu - spell %d", mu ? "mage's" : "clerics'", lv, k,
                         (*sp)[k]);
                expect(PI, w, ok, "type %d words %d %d %d spell %d value %d hidden %d", rec[0x2E], rec[0x2F], rec[0x30],
                       rec[0x31], rec[0x3C], rec[0x3A] | rec[0x3B] << 8, rec[0x35]);
            }
        }
    {
        uint8_t rec[items::kRecordSize];
        s_make({40, 3, 7, 3, 1, 1, 2, 2}, 0, rec, rows);
        const auto* l7 = b_row("it_muspells", 7);
        const auto* l1 = b_row("it_muspells", 1);
        const auto* l2 = b_row("it_muspells", 2);
        const bool ok = l7 && l1 && l2 && rec[0x2F] == 102 && rec[0x3C] == (*l7)[3] && rec[0x3D] == (*l1)[1] &&
                        rec[0x3E] == (*l2)[2] && (rec[0x3A] | rec[0x3B] << 8) == 150 * (7 + 1 + 2);
        expect(PI, "a random mage's scroll of 3 spells: levels 7, 1, 2 - value 1500, \"3 Spells\"", ok,
               "words %d spells %d %d %d value %d", rec[0x2F], rec[0x3C], rec[0x3D], rec[0x3E], rec[0x3A] | rec[0x3B] << 8);
    }
    // In the game: TREASURE 0x80 + n gives n items (its scripts' 0x81, 0x82, 0x84 ...)
    for (int n : {1, 2, 4, 6}) {
        items::Ground g;
        g.clear();
        host->random_items(n, g);
        char w[60];
        snprintf(w, sizeof w, "TREASURE 0x%02X: %d random items on the ground", 0x80 + n, n);
        bool real = g.n == n;
        for (int k = 0; k < g.n; ++k) real = real && g.item[k][0x2E] != 0;
        expect(PI, w, real, "%d", g.n);
    }
}

// ---- 9. Names (secret_facts_2.md 25; DOSBox 2026-10-18: ITEMTEST1's list)
void s_names()
{
    const char* shown[][2] = {{"ITEM1 16 13", "MIRROR"},
                              {"ITEM1 18 7", "GAUNTLETS OF OGRE POWER"},
                              {"ITEM1 20 0", "CLOAK OF DISPLACEMENT"},
                              {"ITEM1 99 13", "RING OF INVISIBILITY"},
                              {"ITEM1 99 22", "GIRDLE OF GIANT STRENGTH"},
                              {"ITEM1 99 1", "TWO-HANDED SWORD OF VS GIANTS"},
                              {"ITEM1 99 7", "PERIAPT OF HEALTH"},
                              {"ITEM1 99 8", "STONE OF GOOD LUCK"},
                              {"ITEM1 99 12", "RING OF FIRE RESISTANCE"},
                              {"ITEM1 99 23", "BOOTS OF SPEED"},
                              {"ITEM2 55 0", "LONG SWORD VS GIANTS"},
                              {"ITEM3 81 1", "SILVER SHIELD +5"},
                              {"MON1ITM 77 2", "SILVER SHIELD +3"},
                              {"ITEM1 17 0", "WAND OF MAGIC MISSILES"},
                              {"ITEM1 18 6", "WAND OF ICE STORM"},
                              {"ITEM1 19 0", "SCROLL OF PROT DRAGON BREATH"}};
    for (const auto& s : shown) {
        const SItem* it = nullptr;
        for (const SItem& x : s_items)
            if (x.from == s[0]) it = &x;
        if (!it) {
            expect(PI, s[0], false, "no such record");
            continue;
        }
        std::string nm = s_name(it->r);
        for (char& ch : nm) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
        char w[100];
        snprintf(w, sizeof w, "%s's name: %s (DOSBox)", s[0], s[1]);
        expect(PI, w, nm == s[1], "%s", nm.c_str());
    }
    for (const SItem& x : s_items)
        if (x.from == "ITEM1 18 10") {
            const std::string nm = s_name(x.r);
            expect(PI, "ITEM1 18 10's name: 30 Arrows +1 (DOSBox, the treasure)", nm == "30 Arrows +1", "%s", nm.c_str());
        }
}

void all_items()
{
    if (!b_oracle_load("secret_items")) {
        expect(PI, "oracle file", false, "no oracle_secret_items.txt");
        return;
    }
    if (!boot()) {                          // (out of any fight the parts before left)
        expect(PI, "boot", false);
        return;
    }
    s_load_items();
    expect(PI, "the game's item records", s_items.size() > 200, "%zu", s_items.size());
    s_base = pt->m[0];
    const char* only = getenv("ITEMS_ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("values")) s_readied_values();
    if (want("codes")) s_codes();
    if (want("ready")) s_ready_rules();
    if (want("use")) s_use();
    if (want("potions")) s_use_effects();
    if (want("scrolls")) s_scrolls();
    if (want("money")) s_money();
    if (want("names")) s_names();
    if (want("treasure")) s_treasure();
    if (want("fights")) s_fights();
    pt->m[0] = s_base;
}

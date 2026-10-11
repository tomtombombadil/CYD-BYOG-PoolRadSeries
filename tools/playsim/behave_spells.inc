// Behaviour checks: spells (included by sweep.cpp with -DBEHAVE_SPELLS).
// Every spell Curse has - the clerics', druids', magic-users' and the
// monsters' / items' (0x39-0x63) - against the original (claude/
// behaviour_facts.md: spells; spell_facts.md, combat_rules_facts.md,
// curse_finish_facts.md; coab's ovr023 / ovr024 / ovr013 facts, the AD&D
// tables). The numeric tables the checks expect (the spell table's rows
// as the original has them, spell slots, Sleep's costs, Enlarge's
// Strengths) are read at run time from ORACLE_DIR/oracle_spells.txt
// (scratch space, never the repo); the formulas are written here.

static const char* const SP = "spells";

// ---- The oracle
struct SpRow {
    int v[17] = {};                     // id class level range range_lvl dur dur_lvl field6 targets on_save save
                                        // affect when delay priority fe ff
    bool ok = false;
};
static SpRow sp_row[0x65];
static int sp_clslots[13][5], sp_muslots[13][5];
static int sp_wis[19][2];               // Wisdom: the spell level index and the extra (0: none)
static int sp_sleep[8];
static int sp_enl[13][2];
static bool sp_oracle_ok = false;

static bool sp_load_oracle()
{
    const char* dir = getenv("ORACLE_DIR");
    std::string path = std::string(dir ? dir : "/tmp/claude-0/bt") + "/oracle_spells.txt";
    FILE* in = fopen(path.c_str(), "r");
    if (!in) return false;
    memset(sp_wis, -1, sizeof sp_wis);
    char line[256];
    while (fgets(line, sizeof line, in)) {
        char tag[16];
        int a[17] = {};
        const int n = sscanf(line, "%15s %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", tag, &a[0], &a[1], &a[2], &a[3],
                             &a[4], &a[5], &a[6], &a[7], &a[8], &a[9], &a[10], &a[11], &a[12], &a[13], &a[14], &a[15], &a[16]);
        if (n < 2 || tag[0] == '#') continue;
        if (!strcmp(tag, "row") && n == 18 && a[0] >= 1 && a[0] <= 0x64) {
            memcpy(sp_row[a[0]].v, a, sizeof a);
            sp_row[a[0]].ok = true;
        } else if (!strcmp(tag, "clslots") && n == 7 && a[0] >= 1 && a[0] <= 12) {
            for (int k = 0; k < 5; ++k) sp_clslots[a[0]][k] = a[1 + k];
        } else if (!strcmp(tag, "muslots") && n == 7 && a[0] >= 1 && a[0] <= 12) {
            for (int k = 0; k < 5; ++k) sp_muslots[a[0]][k] = a[1 + k];
        } else if (!strcmp(tag, "wisbonus") && n == 4 && a[0] <= 18) {
            sp_wis[a[0]][0] = a[1];
            sp_wis[a[0]][1] = a[2];
        } else if (!strcmp(tag, "sleepcost") && n == 3 && a[0] >= 0 && a[0] < 8) {
            sp_sleep[a[0]] = a[1];
        } else if (!strcmp(tag, "enlarge") && n == 4 && a[0] >= 1 && a[0] <= 12) {
            sp_enl[a[0]][0] = a[1];
            sp_enl[a[0]][1] = a[2];
        }
    }
    fclose(in);
    int rows = 0;
    for (int s = 1; s <= 0x64; ++s) rows += sp_row[s].ok;
    return rows == 0x64;
}
// The original's row for spell s (field k as the oracle lists them, 1 = class)
static int orow(int s, int k) { return s >= 1 && s <= 0x64 ? sp_row[s].v[k] : 0; }
enum { OCls = 1, OLvl, ORange, ORangeL, ODur, ODurL, OAim, OTargets, OOnSave, OSave, OAffect, OWhen, ODelay, OPrio, OFe, OFf };

// ---- The dice: d20s and d100s as set, every other die its highest or lowest
static int sp_d20 = 0, sp_d100 = 0, sp_mode = 1;     // (0: as usual)
static int sp_die(int n)
{
    if (n == 20 && sp_d20) return sp_d20 - 1;
    if (n == 100 && sp_d100) return sp_d100 - 1;
    if (sp_mode == 1) return n - 1;
    if (sp_mode == 2) return 0;
    return -1;
}
// mode 1: other dice high, 2: low; d20 / d100: those faces (0: as the mode)
static void sp_dice(int mode, int d20 = 0, int d100 = 0)
{
    sp_mode = mode;
    sp_d20 = d20;
    sp_d100 = d100;
    create::g_die_hook = sp_die;
}

// The caster's level as the original works it out (coab spellMaxTargetCount;
// spell_facts.md 1.2) from the levels - the oracle's formula
static int sp_level_rule(const party::Character& c, int s, bool item)
{
    const int cls = orow(s, OCls);
    int lvl;
    if (c.level(classes::Cleric) == 0 && c.level(classes::MagicUser) == 0 && c.level(classes::Paladin) < 9 &&
        c.level(classes::Ranger) < 8)
        lvl = 6;
    else {
        auto sk = [&](int k) { return classes::skill_level(c, k); };
        auto mx = [](int a, int b) { return a > b ? a : b; };
        switch (cls) {
        case 0: lvl = mx(sk(classes::Cleric), sk(classes::Paladin) - 8); break;
        case 1: lvl = mx(sk(classes::Ranger) - 7, 0); break;
        case 2: lvl = mx(sk(classes::MagicUser), sk(classes::Ranger) - 8); break;
        case 3: lvl = 12; break;
        default: lvl = 0; break;
        }
    }
    if (item && cls != 3) lvl = 6;
    return lvl;
}

// ---- 1. The spell table: every row as the original's
static void sp_check_table()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    int bad_rows = 0;
    for (int s = 1; s <= 0x64; ++s) {
        char diff[300] = "";
        int nd = 0;
        static const char* const names[16] = {"class", "level", "range", "range/lvl", "time", "time/lvl", "aim", "targets",
                                              "on save", "save", "effect", "when", "delay", "priority", "AI target",
                                              "AI area"};
        for (int i = 0; i < 16; ++i) {
            const int got = t.u8(static_cast<uint16_t>(t.lay.spells + s * 16 + i));
            const int want = orow(s, 1 + i) & 0xFF;
            if (got != want && nd < 6) {
                ++nd;
                const size_t l = strlen(diff);
                snprintf(diff + l, sizeof diff - l, "%s%s %d (original %d)", l ? ", " : "", names[i], got, want);
            }
        }
        // ... and the engine's reading of it
        const spells::Entry e = spells::entry(t, s);
        const bool read_ok = e.cls == orow(s, OCls) && e.level == orow(s, OLvl) && e.range == orow(s, ORange) &&
                             e.range_level == orow(s, ORangeL) && e.lasts == orow(s, ODur) &&
                             e.lasts_level == orow(s, ODurL) && e.aim == orow(s, OAim) &&
                             e.targets == orow(s, OTargets) && e.on_save == orow(s, OOnSave) &&
                             e.save == orow(s, OSave) && e.affect == orow(s, OAffect) && e.when == orow(s, OWhen) &&
                             e.delay == orow(s, ODelay) && e.priority == orow(s, OPrio);
        char what[48];
        snprintf(what, sizeof what, "table row %02X as the original's", s);
        if (!expect(SP, what, nd == 0 && read_ok, "%s%s", diff, read_ok ? "" : " (spells::entry reads it wrong)")) ++bad_rows;
    }
    (void)bad_rows;
}

// ---- 2. The caster's level, range and time by level, for every spell
static void sp_check_levels()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    // Casters: (cleric, druid, fighter, paladin, ranger, magic-user, thief) levels, race, old levels, multi level
    struct Who {
        const char* what;
        int lv[8];
        int race;
        int old[8];
        int multi;
    };
    const Who who[] = {
        {"a 1st level cleric", {1, 0, 0, 0, 0, 0, 0, 0}, 7, {}, 0},
        {"a 9th level cleric", {9, 0, 0, 0, 0, 0, 0, 0}, 7, {}, 0},
        {"a 7th level magic-user", {0, 0, 0, 0, 0, 7, 0, 0}, 7, {}, 0},
        {"a 12th level magic-user", {0, 0, 0, 0, 0, 12, 0, 0}, 7, {}, 0},
        {"a 9th level fighter (no spells)", {0, 0, 9, 0, 0, 0, 0, 0}, 7, {}, 0},
        {"a 10th level paladin", {0, 0, 0, 10, 0, 0, 0, 0}, 7, {}, 0},
        {"an 11th level ranger", {0, 0, 0, 0, 11, 0, 0, 0}, 7, {}, 0},
        {"a cleric 5 / magic-user 6 (half-elf)", {5, 0, 0, 0, 0, 6, 0, 0}, 4, {}, 0},
        {"a human magic-user 6, once a 5th level cleric", {0, 0, 0, 0, 0, 6, 0, 0}, 7, {5, 0, 0, 0, 0, 0, 0, 0}, 5},
        {"a human fighter 8, once a 7th level magic-user", {0, 0, 8, 0, 0, 0, 0, 0}, 7, {0, 0, 0, 0, 0, 7, 0, 0}, 7},
    };
    for (const Who& w : who) {
        static party::Character c;
        c = party::Character{};
        for (int k = 0; k < 8; ++k) {
            c.rec[0x109 + k] = static_cast<uint8_t>(w.lv[k]);
            c.rec[0x111 + k] = static_cast<uint8_t>(w.old[k]);
        }
        c.rec[0x74] = static_cast<uint8_t>(w.race);
        c.rec[0xE6] = static_cast<uint8_t>(w.multi);
        int bad_camp = 0, bad_fight = 0, bad_item = 0, first_camp = 0, first_fight = 0, first_item = 0;
        int ex_c = 0, got_c = 0, ex_f = 0, got_f = 0, ex_i = 0, got_i = 0;
        for (int s = 1; s <= 0x64; ++s) {
            const int want = sp_level_rule(c, s, false), want_item = sp_level_rule(c, s, true);
            const int camp = spells::power(c, t, s), fight = combat::power_of(c.rec, t, s, false);
            const int item_c = spells::item_power(c, t, s), item_f = combat::power_of(c.rec, t, s, true);
            if (camp != want && !bad_camp++) { first_camp = s; ex_c = want; got_c = camp; }
            if (fight != want && !bad_fight++) { first_fight = s; ex_f = want; got_f = fight; }
            if ((item_c != want_item || item_f != want_item) && !bad_item++) {
                first_item = s;
                ex_i = want_item;
                got_i = item_c != want_item ? item_c : item_f;
            }
        }
        char what[120];
        snprintf(what, sizeof what, "caster level, camp: %s", w.what);
        expect(SP, what, !bad_camp, "%d spells wrong, first %02X: %d (original %d)", bad_camp, first_camp, got_c, ex_c);
        snprintf(what, sizeof what, "caster level, fights: %s", w.what);
        expect(SP, what, !bad_fight, "%d spells wrong, first %02X: %d (original %d)", bad_fight, first_fight, got_f, ex_f);
        snprintf(what, sizeof what, "caster level, items: %s", w.what);
        expect(SP, what, !bad_item, "%d spells wrong, first %02X: %d (original %d)", bad_item, first_item, got_i, ex_i);
    }
    // Range and time by level: range = fixed + per level x level (0 with an
    // aim: 1; touch -1: 1); time = fixed + per level x level (minutes)
    int bad_r = 0, bad_t = 0;
    char first_r[120] = "", first_t[120] = "";
    for (int s = 1; s <= 0x64; ++s)
        for (int lv : {1, 4, 7, 12}) {
            int r = orow(s, ORange) + orow(s, ORangeL) * lv;
            if (r == 0 && orow(s, OAim) != 0) r = 1;
            if (r == -1 || orow(s, ORange) == -1) r = 1;
            const int got = spells::reach(t, s, lv);
            if (got != r && !bad_r++) snprintf(first_r, sizeof first_r, "%02X at level %d: %d (original %d)", s, lv, got, r);
            const int tm = orow(s, ODur) + orow(s, ODurL) * lv;
            const int gt = spells::lasts(t, s, lv);
            if (gt != tm && !bad_t++) snprintf(first_t, sizeof first_t, "%02X at level %d: %d (original %d)", s, lv, gt, tm);
        }
    expect(SP, "range by level, every spell (fixed + per level; touch 1)", !bad_r, "%d wrong, first %s", bad_r, first_r);
    expect(SP, "time by level, every spell (fixed + per level minutes)", !bad_t, "%d wrong, first %s", bad_t, first_t);
    // The rolled times outside fights (coab GetSpellAffectTimeout): Cause
    // Disease d6 x 10, 0x39 / 0x3D 5d4, 0x3B d4 x 10 + 40, 0x3F (d10 + 10) x 10,
    // Neutralize Poison 1440
    struct Rolled {
        int spell, low, high;
    };
    const Rolled rolled[] = {{0x28, 10, 60}, {0x39, 5, 20}, {0x3D, 5, 20}, {0x3B, 50, 80}, {0x3F, 110, 200}, {0x43, 1440, 1440}};
    for (const Rolled& r : rolled) {
        sp_dice(2);
        const int lo = spells::lasts_rolled(t, r.spell, 9, d->prof->magic.facts, rng);
        sp_dice(1);
        const int hi = spells::lasts_rolled(t, r.spell, 9, d->prof->magic.facts, rng);
        char what[80];
        snprintf(what, sizeof what, "spell %02X's rolled time outside fights", r.spell);
        expect(SP, what, lo == r.low && hi == r.high, "dice low %d high %d (original %d .. %d)", lo, hi, r.low, r.high);
    }
}

// ---- 3. Memorizing: spell slots (the class tables, the cleric's Wisdom),
// the rest it takes (coab ovr016: 4 hours, 6 past 2nd level, + 15 minutes a
// spell level being memorized or scribed)
static void sp_check_memory()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    for (int cls : {classes::Cleric, classes::MagicUser}) {
        int bad = 0;
        char first[120] = "";
        for (int lv = 1; lv <= 12; ++lv)
            for (int wis = 9; wis <= 18; ++wis) {
                if (cls == classes::MagicUser && wis != 9) continue;
                static party::Character c;
                c = party::Character{};
                c.rec[0x109 + cls] = static_cast<uint8_t>(lv);
                c.rec[0x74] = 7;
                c.rec[0x14] = c.rec[0x15] = static_cast<uint8_t>(wis);
                c.rec[0x12] = c.rec[0x13] = 18;
                classes::spell_slots(c, t);
                int want[5];
                for (int k = 0; k < 5; ++k) want[k] = cls == classes::Cleric ? sp_clslots[lv][k] : sp_muslots[lv][k];
                if (cls == classes::Cleric)
                    for (int w = 13; w <= wis; ++w)
                        if (sp_wis[w][0] >= 0 && want[sp_wis[w][0]] > 0) want[sp_wis[w][0]] += sp_wis[w][1];
                const int base = cls == classes::Cleric ? 0 : 2;
                for (int k = 0; k < 5; ++k) {
                    const int got = c.rec[0x12D + base * 5 + k];
                    if (got != want[k] && !bad++)
                        snprintf(first, sizeof first, "level %d Wis %d: %d spells of level %d (original %d)", lv, wis, got,
                                 k + 1, want[k]);
                }
            }
        expect(SP, cls == classes::Cleric ? "cleric spell slots by level and Wisdom (PHB)" : "magic-user spell slots by level (PHB)",
               !bad, "%d wrong, first %s", bad, first);
    }
    // The rest: memorize these and the time needed
    struct Case {
        std::vector<int> spells;
        int minutes;
        const char* what;
    };
    const Case cases[] = {
        {{0x0F}, 4 * 60 + 15, "one 1st level spell: 4 hours + 15 minutes"},
        {{0x0F, 0x0F, 0x1E}, 4 * 60 + 15 * 4, "1st, 1st, 2nd level: 4 hours + 60 minutes"},
        {{0x0F, 0x2F}, 6 * 60 + 15 * 4, "1st and 3rd level: 6 hours + 60 minutes"},
        {{0x2F, 0x2F, 0x5C}, 6 * 60 + 15 * 11, "3rd, 3rd, 5th level: 6 hours + 165 minutes"},
    };
    for (const Case& cs : cases) {
        static party::Character c;
        c = party::Character{};
        c.rec[0x10E] = 12;                   // a 12th level magic-user, Int 18
        c.rec[0x74] = 7;
        c.rec[0x12] = c.rec[0x13] = 18;
        c.rec[0x195] = party::Okay;
        c.rec[0x196] = 1;
        classes::spell_slots(c, t);
        for (int s : cs.spells) c.rec[0x79 + s - 1] = 1;
        bool added = true;
        for (int s : cs.spells) added = added && magic::add(c, t, s);
        const int m = magic::rest_minutes(c, t);
        char what[120];
        snprintf(what, sizeof what, "rest to memorize %s", cs.what);
        expect(SP, what, added && m == cs.minutes, "%d minutes (original %d)%s", m, cs.minutes, added ? "" : ", not all added");
        // A rest that long memorizes them all (five minutes a step)
        static party::Party one;
        one.count = 1;
        one.m[0] = c;
        magic::Rest r;
        magic::begin(r);
        int steps = 0;
        while (magic::memorizing(one.m[0]) && steps < 2000) {
            magic::step(r, one, t);
            ++steps;
        }
        snprintf(what, sizeof what, "a rest memorizes %s within that time", cs.what);
        expect(SP, what, !magic::memorizing(one.m[0]) && steps * 5 <= cs.minutes, "%d minutes of rest (the rest asked %d)",
               steps * 5, cs.minutes);
    }
    // Scribing: a scroll's spell into the spell book over a rest (coab ovr016 / ovr021 rest_scribe):
    // the time as for memorizing (a 3rd level spell: 6 hours + 45 minutes); then the spell is in the
    // book and gone from the scroll (a scroll of one spell used up)
    {
        const magic::Scrolls sc = scroll_facts();
        // a magic-user's scroll from the player's items (ITEMS_BIN): the first scroll holding a magic-user spell
        int scroll_type = -1;
        {
            const char* file = getenv("ITEMS_BIN");
            FILE* in = fopen(file ? file : "/tmp/claude-0/sw/items.bin", "rb");
            uint8_t r[63];
            while (in && scroll_type < 0 && fread(r, 1, 63, in) == 63)
                if (magic::is_scroll(sc, r) && r[0x3C] && r[0x3C] < 0x80 && t.spell_class(r[0x3C]) == 2) scroll_type = r[0x2E];
            if (in) fclose(in);
        }
        static party::Character c;
        c = party::Character{};
        c.rec[0x10E] = 5;
        c.rec[0x74] = 7;
        c.rec[0x12] = c.rec[0x13] = 18;
        c.rec[0x195] = party::Okay;
        c.rec[0x196] = 1;
        classes::spell_slots(c, t);
        uint8_t scroll[63] = {};
        scroll[0x2E] = static_cast<uint8_t>(scroll_type);
        scroll[0x30] = sc.one_spell;
        scroll[0x3C] = 0x2F;
        rules::add_item(c, scroll);
        c.items[0][0x35] = 0;                         // (identified: its spells are known)
        const magic::Scribe r = magic::scribe(c, t, sc, 0x2F);
        const int m = magic::rest_minutes(c, t, sc);
        expect(SP, "scribe Fireball (3rd level) from a scroll: 6 hours + 45 minutes of rest",
               scroll_type >= 0 && r == magic::Scribe::Ok && m == 6 * 60 + 45, "scribe %d minutes %d", (int)r, m);
        static party::Party one;
        one.count = 1;
        one.m[0] = c;
        magic::Rest rr;
        magic::begin(rr);
        int steps = 0;
        while (magic::scribing(one.m[0], sc) && steps < 2000) {
            magic::step(rr, one, t, sc);
            ++steps;
        }
        expect(SP, "a rest scribes it: in the spell book, the one-spell scroll used up",
               one.m[0].rec[0x79 + 0x2F - 1] == 1 && one.m[0].n_items == 0 && steps * 5 <= m, "book %d items %d after %d minutes",
               one.m[0].rec[0x79 + 0x2F - 1], one.m[0].n_items, steps * 5);
        // Not without a slot of that level: a 4th level magic-user can't scribe a 3rd level spell
        c.rec[0x10E] = 4;
        classes::spell_slots(c, t);
        c.items[0][0x3C] = 0x2F;
        expect(SP, "scribe: no 3rd level spells (a 4th level magic-user) - can't", magic::scribe(c, t, sc, 0x2F) == magic::Scribe::Cannot);
    }
}

// ---- A fight to cast in: save A's party at the inn's fight, the field
// made open floor, everyone off it until a check places them
static int sp_floor = 0;
static bool sp_new_fight()
{
    using namespace play;
    sp_dice(0);
    if (!boot() || !start_fight() || !load_magic() || !fg) return false;
    combat::Battle& b = fg->b;
    if (b.n < 8) return false;
    sp_floor = b.ground[b.f[0].y][b.f[0].x];
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = static_cast<uint8_t>(sp_floor);
    for (int i = 0; i < b.n; ++i) b.f[i].size = 0;
    b.n_clouds = 0;
    b.aim_x = b.aim_y = -1;
    b.tables = &fg->t;
    combat::occupancy(b);
    return true;
}
static combat::Fighter& F(int i) { return play::fg->b.f[i]; }
// A plain creature at (x, y): `team`, HP, saves 15 (the d20 decides), no effects,
// a man-sized person of 1 Hit Die, AC 10, neutral
static void sp_put(int i, int team, int x, int y, int hp = 200)
{
    combat::Fighter& f = F(i);
    f.size = 1;
    f.x = x;
    f.y = y;
    f.rec[0x197] = static_cast<uint8_t>(team);
    f.rec[0x1A4] = f.rec[0x78] = static_cast<uint8_t>(hp);
    f.rec[0x195] = party::Okay;
    f.rec[0x196] = 1;
    for (int k = 0; k < 5; ++k) f.rec[0xDF + k] = 15;
    f.rec[0x186] = 0;
    if (f.n_aff) *f.n_aff = 0;
    f.rec[0x11A] = 1;
    f.rec[0xDE] = 1;
    f.rec[0xE5] = 1;
    f.rec[0x11B] = 4;
    f.rec[0x14B] = 0;
    f.rec[0x19A] = f.rec[0x19B] = 50;
    f.rec[0x199] = 100;                 // (touches land unless a check says)
    f.delay = 5;
    f.spell = 0;
    f.fleeing = false;
    combat::occupancy(play::fg->b);
}
// The caster: only `cls` at `level` (a human)
static void sp_caster(int i, int cls, int level)
{
    combat::Fighter& f = F(i);
    for (int k = 0; k < 8; ++k) f.rec[0x109 + k] = f.rec[0x111 + k] = 0;
    if (cls >= 0) f.rec[0x109 + cls] = static_cast<uint8_t>(level);
    f.rec[0x74] = 7;
    f.rec[0xE6] = 0;
}
static const combat::FightSpell* sp_fs(int s)
{
    const auto& fl = play::d->prof->fight;
    return combat::fight_spell(fl.spells, fl.n_spells, s);
}
static combat::SpellLine sp_l[24];
static int sp_n = 0;
static int sp_cast(int caster, int s, std::initializer_list<int> tg, int pw = 0)
{
    using namespace play;
    sp_n = 0;
    const combat::FightSpell* fs = sp_fs(s);
    if (!fs) return -1;
    int t[24], n = 0;
    for (int i : tg) t[n++] = i;
    sp_n = combat::cast(fg->b, mrules->tables, caster, s, *fs, t, n, rng, sp_l, 24, pw);
    return sp_n;
}
static int sp_aff(int i, int type)                  // index in its effects, -1
{
    const combat::Fighter& f = F(i);
    for (int k = 0; f.n_aff && k < *f.n_aff; ++k)
        if (f.aff[k][0] == type) return k;
    return -1;
}
static void sp_give(int i, int type, int minutes, int data, bool call)
{
    combat::Fighter& f = F(i);
    if (!f.n_aff || *f.n_aff >= f.max_aff) return;
    uint8_t* a = f.aff[(*f.n_aff)++];
    memset(a, 0, party::kAffectSize);
    a[0] = static_cast<uint8_t>(type);
    a[1] = static_cast<uint8_t>(minutes);
    a[2] = static_cast<uint8_t>(minutes >> 8);
    a[3] = static_cast<uint8_t>(data);
    a[4] = call ? 1 : 0;
}
static int sp_aff_time(int i, int type) { const int k = sp_aff(i, type); return k < 0 ? -1 : F(i).aff[k][1] | F(i).aff[k][2] << 8; }
static int sp_aff_data(int i, int type) { const int k = sp_aff(i, type); return k < 0 ? -1 : F(i).aff[k][3]; }
static int sp_said(int who, combat::Did did)        // how many such lines
{
    int n = 0;
    for (int k = 0; k < sp_n; ++k) n += sp_l[k].who == who && sp_l[k].did == did;
    return n;
}
static int sp_amount(int who)
{
    for (int k = 0; k < sp_n; ++k)
        if (sp_l[k].who == who && sp_l[k].did == combat::Did::Damage) return sp_l[k].amount;
    return 0;
}

// The fighters a check uses: the caster (a party fighter), and others
static int sp_c = 0, sp_t[6];
static bool sp_pick()
{
    combat::Battle& b = play::fg->b;
    int n = 0;
    sp_c = -1;
    for (int i = 0; i < b.n; ++i) {
        if (sp_c < 0 && b.f[i].member >= 0) sp_c = i;
        else if (n < 6) sp_t[n++] = i;
    }
    return sp_c >= 0 && n == 6;
}

// ---- 4. Spells in fights: damage, saves, targets, effects
struct SpDmg {
    int spell, cls, level;
    int lo, hi;                         // the damage with every die low / high (the original's formula)
    int kind;                           // the damage's kind (1 fire, 2 cold, 4 elec, 8 magic)
    char save;                          // 'N' no save, 'H' half, 'Z' none on a save
    bool touch;
};
static void sp_fight_damage()
{
    using namespace play;
    // (L + 1) / 2 missiles of d4 + 1 ...
    auto mm = [](int L) { return (L + 1) / 2; };
    const SpDmg cases[] = {
        {0x04, classes::Cleric, 3, 1, 8, 8, 'N', true},                         // Cause Light Wounds 1d8, a touch
        {0x09, classes::MagicUser, 1, 1, 1, 9, 'N', false},                     // Burning Hands: the level
        {0x09, classes::MagicUser, 7, 7, 7, 9, 'N', false},
        {0x0F, classes::MagicUser, 1, mm(1) * 2, mm(1) * 5, 8, 'N', false},     // Magic Missile
        {0x0F, classes::MagicUser, 4, mm(4) * 2, mm(4) * 5, 8, 'N', false},
        {0x0F, classes::MagicUser, 9, mm(9) * 2, mm(9) * 5, 8, 'N', false},
        {0x14, classes::MagicUser, 5, 1 + 5, 8 + 5, 0x0C, 'N', true},           // Shocking Grasp 1d8 + level
        {0x2F, classes::MagicUser, 5, 5, 30, 9, 'H', false},                    // Fireball: level d6
        {0x2F, classes::MagicUser, 12, 12, 72, 9, 'H', false},
        {0x40, classes::MagicUser, 5, 3, 42, 9, 'H', false},                    // the wand's fireball: (d3 x 2 + 1) d6
        {0x41, classes::MagicUser, 5, 4, 10, 8, 'N', false},                    // the wand's missile 2d4 + 2
        {0x42, classes::Cleric, 7, 3, 17, 8, 'N', false},                       // Cause Serious Wounds 2d8 + 1 (no touch)
        {0x48, classes::Cleric, 9, 6, 27, 8, 'Z', true},                        // Cause Critical Wounds 3d8 + 3, a touch
        {0x4A, classes::Cleric, 9, 6, 48, 9, 'H', false},                       // Flame Strike 6d8
        {0x57, classes::MagicUser, 7, 3, 30, 0x0A, 'N', false},                 // Ice Storm 3d10 cold + magic
        {0x5C, classes::MagicUser, 9, 9 + 9, 9 + 36, 0x0A, 'H', false},         // Cone of Cold: level + level d4
        {0x5C, classes::MagicUser, 12, 12 + 12, 12 + 48, 0x0A, 'H', false},
    };
    for (const SpDmg& c : cases) {
        char what[100];
        const combat::FightSpell* fs = sp_fs(c.spell);
        snprintf(what, sizeof what, "fight spell %02X in the engine", c.spell);
        if (!expect(SP, what, fs && fs->does != combat::SpellDoes::NotYet)) continue;
        snprintf(what, sizeof what, "spell %02X's damage kind", c.spell);
        expect(SP, what, fs->kind == c.kind, "%d (original %d)", fs->kind, c.kind);
        // high and low dice, the target failing its save (d20 2)
        for (int pass = 0; pass < 2; ++pass) {
            if (!sp_new_fight() || !sp_pick()) {
                expect(SP, "a fight", false);
                return;
            }
            sp_caster(sp_c, c.cls, c.level);
            sp_put(sp_c, 0, 10, 12);
            sp_put(sp_t[0], 1, 12, 12);
            if (c.spell == 0x5C) fg->b.aim_x = 12, fg->b.aim_y = 12;
            sp_dice(pass ? 1 : 2, 2, 100);
            sp_cast(sp_c, c.spell, {sp_t[0]});
            const int want = pass ? c.hi : c.lo;
            const int got = 200 - F(sp_t[0]).hp();
            snprintf(what, sizeof what, "spell %02X at level %d, dice %s, save failed: damage", c.spell, c.level,
                     pass ? "high" : "low");
            expect(SP, what, got == want, "%d (original %d)", got, want);
        }
        // the save made (d20 20): half / none / no difference
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, c.cls, c.level);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 12, 12);
        sp_dice(1, 19, 100);
        F(sp_t[0]).rec[0xDF + 4] = 2;              // (19 saves vs spells; touches land with 19)
        sp_cast(sp_c, c.spell, {sp_t[0]});
        const int got = 200 - F(sp_t[0]).hp();
        const int want = c.save == 'H' ? c.hi / 2 : c.save == 'Z' ? 0 : c.hi;
        snprintf(what, sizeof what, "spell %02X, the save made: %s", c.spell,
                 c.save == 'H' ? "half damage" : c.save == 'Z' ? "no damage" : "no save, full damage");
        expect(SP, what, got == want, "%d (original %d)", got, want);
        if (c.touch) {
            // a touch that misses: nothing
            if (!sp_new_fight() || !sp_pick()) return;
            sp_caster(sp_c, c.cls, c.level);
            sp_put(sp_c, 0, 10, 12);
            sp_put(sp_t[0], 1, 11, 12);
            F(sp_c).rec[0x199] = static_cast<uint8_t>(-30);
            F(sp_t[0]).rec[0x19A] = 55;              // AC 5: 10 - 30 < 55
            sp_dice(1, 10, 100);
            sp_cast(sp_c, c.spell, {sp_t[0]});
            snprintf(what, sizeof what, "touch spell %02X: a missed touch does nothing", c.spell);
            expect(SP, what, F(sp_t[0]).hp() == 200, "HP %d of 200", F(sp_t[0]).hp());
        } else if (orow(c.spell, ORange) != -1) {
            // not a touch: no roll to hit (the caster can't hit AC -10 with a 10)
            if (!sp_new_fight() || !sp_pick()) return;
            sp_caster(sp_c, c.cls, c.level);
            sp_put(sp_c, 0, 10, 12);
            sp_put(sp_t[0], 1, 12, 12);
            F(sp_c).rec[0x199] = static_cast<uint8_t>(-30);
            F(sp_t[0]).rec[0x19A] = 70;
            if (c.spell == 0x5C) fg->b.aim_x = 12, fg->b.aim_y = 12;
            sp_dice(1, 2, 100);
            sp_cast(sp_c, c.spell, {sp_t[0]});
            snprintf(what, sizeof what, "spell %02X needs no roll to hit", c.spell);
            expect(SP, what, 200 - F(sp_t[0]).hp() == c.hi, "damage %d (original %d)", 200 - F(sp_t[0]).hp(), c.hi);
        }
    }
    // Resistances: Resist Fire halves fire damage (and +3 on the save), Resist Cold the same for cold
    struct Res {
        int spell, cls, level, effect_spell;
        int full;
    };
    const Res res[] = {{0x2F, classes::MagicUser, 6, 0x18, 36}, {0x57, classes::MagicUser, 7, 0x08, 30}};
    for (const Res& r : res) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, r.cls, r.level);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 12, 12);
        sp_give(sp_t[0], orow(r.effect_spell, OAffect), 10, 1, false);
        sp_dice(1, 2, 100);
        sp_cast(sp_c, r.spell, {sp_t[0]});
        char what[100];
        snprintf(what, sizeof what, "spell %02X on one with spell %02X's effect: half damage", r.spell, r.effect_spell);
        expect(SP, what, 200 - F(sp_t[0]).hp() == r.full / 2, "%d (original %d)", 200 - F(sp_t[0]).hp(), r.full / 2);
    }
    // Shield stops Magic Missile
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::MagicUser, 5);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 12, 12);
    sp_give(sp_t[0], orow(0x13, OAffect), 10, 1, false);
    sp_dice(1, 2, 100);
    sp_cast(sp_c, 0x0F, {sp_t[0]});
    expect(SP, "Magic Missile on one with Shield: no damage", F(sp_t[0]).hp() == 200, "HP %d of 200", F(sp_t[0]).hp());
    // The Minor Globe: spells below 4th level do nothing, 4th and up do
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::MagicUser, 9);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 12, 12);
    sp_put(sp_t[1], 1, 12, 14);
    sp_give(sp_t[0], orow(0x58, OAffect), 10, 1, false);
    sp_give(sp_t[1], orow(0x58, OAffect), 10, 1, false);
    sp_dice(1, 2, 100);
    sp_cast(sp_c, 0x2F, {sp_t[0]});
    fg->b.aim_x = 12, fg->b.aim_y = 14;
    sp_cast(sp_c, 0x5C, {sp_t[1]});
    expect(SP, "Minor Globe: a Fireball (3rd level) does nothing", F(sp_t[0]).hp() == 200, "HP %d", F(sp_t[0]).hp());
    expect(SP, "Minor Globe: a Cone of Cold (5th level) does its damage", 200 - F(sp_t[1]).hp() == 9 + 36, "damage %d (45)",
           200 - F(sp_t[1]).hp());
    // Slay Living: a failed save slays; a made one 2d8 + 1 (all of it)
    for (int pass = 0; pass < 2; ++pass) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, classes::Cleric, 9);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 12, 12);
        sp_dice(1, pass ? 20 : 2, 100);
        sp_cast(sp_c, 0x4C, {sp_t[0]});
        if (!pass)
            expect(SP, "Slay Living, the save failed: slain", F(sp_t[0]).status() == party::Dead && !F(sp_t[0]).up(),
                   "status %d", F(sp_t[0]).status());
        else
            expect(SP, "Slay Living, the save made: 2d8 + 1 (dice high 17), not halved", 200 - F(sp_t[0]).hp() == 17,
                   "damage %d", 200 - F(sp_t[0]).hp());
    }
    // Poison (cleric 4): a failed save against poison kills; a made one nothing
    for (int pass = 0; pass < 2; ++pass) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, classes::Cleric, 9);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 11, 12);
        sp_dice(1, pass ? 20 : 2, 100);
        sp_cast(sp_c, 0x44, {sp_t[0]});
        if (!pass)
            expect(SP, "Poison, the save failed: killed", F(sp_t[0]).status() == party::Dead, "status %d HP %d",
                   F(sp_t[0]).status(), F(sp_t[0]).hp());
        else
            expect(SP, "Poison, the save made: nothing", F(sp_t[0]).status() == party::Okay && F(sp_t[0]).hp() == 200,
                   "status %d HP %d", F(sp_t[0]).status(), F(sp_t[0]).hp());
    }
    // Defoliation: plants take 6d6, others nothing
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::Cleric, 9);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 11, 12);
    sp_put(sp_t[1], 1, 12, 12);
    F(sp_t[0]).rec[0x11A] = 18;
    sp_dice(1, 2, 100);
    sp_cast(sp_c, 0x62, {sp_t[0], sp_t[1]});
    expect(SP, "Wand of Defoliation: a plant takes 6d6 (36 high)", 200 - F(sp_t[0]).hp() == 36, "%d", 200 - F(sp_t[0]).hp());
    expect(SP, "Wand of Defoliation: not a plant, nothing", F(sp_t[1]).hp() == 200, "HP %d", F(sp_t[1]).hp());
}

// Healing in fights: the dice, the most HP, the dying made unconscious
static void sp_fight_heal()
{
    using namespace play;
    struct H {
        int spell, lo, hi;
    };
    const H cases[] = {{0x03, 1, 8}, {0x3A, 3, 17}, {0x47, 6, 27}, {0x3E, 4, 10}, {0x63, 4, 10}};
    for (const H& c : cases)
        for (int pass = 0; pass < 2; ++pass) {
            if (!sp_new_fight() || !sp_pick()) return;
            sp_caster(sp_c, classes::Cleric, 9);
            sp_put(sp_c, 0, 10, 12);
            sp_put(sp_t[0], 0, 11, 12, 100);
            F(sp_t[0]).rec[0x1A4] = 50;
            sp_dice(pass ? 1 : 2, 10, 100);
            sp_cast(sp_c, c.spell, {sp_t[0]});
            char what[80];
            snprintf(what, sizeof what, "healing spell %02X, dice %s", c.spell, pass ? "high" : "low");
            const int want = pass ? c.hi : c.lo;
            expect(SP, what, F(sp_t[0]).hp() - 50 == want, "%d HP (original %d)", F(sp_t[0]).hp() - 50, want);
        }
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::Cleric, 9);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 0, 11, 12, 30);
    F(sp_t[0]).rec[0x1A4] = 28;
    sp_dice(1, 10, 100);
    sp_cast(sp_c, 0x47, {sp_t[0]});
    expect(SP, "Cure Critical Wounds: no more than their most HP", F(sp_t[0]).hp() == 30, "HP %d of 30", F(sp_t[0]).hp());
    // The dying: healed, they are unconscious (out of the fight still)
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::Cleric, 9);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 0, 11, 12, 30);
    combat::damage(fg->b, sp_t[0], 33);          // 3 below 0: dying
    const bool dying = F(sp_t[0]).status() == party::Dying;
    sp_dice(1, 10, 100);
    sp_cast(sp_c, 0x03, {sp_t[0]});
    expect(SP, "Cure Light Wounds on the dying: unconscious, not back in the fight",
           dying && F(sp_t[0]).status() == party::Unconscious && !F(sp_t[0]).up(), "status %d up %d (was dying %d)",
           F(sp_t[0]).status(), (int)F(sp_t[0]).up(), (int)dying);
    // The dead: nothing
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::Cleric, 9);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 0, 11, 12, 30);
    combat::damage(fg->b, sp_t[0], 50);
    sp_dice(1, 10, 100);
    sp_cast(sp_c, 0x47, {sp_t[0]});
    expect(SP, "Cure Critical Wounds on the dead: nothing", F(sp_t[0]).status() == party::Dead && F(sp_t[0]).hp() == 0,
           "status %d HP %d", F(sp_t[0]).status(), F(sp_t[0]).hp());
}

// Effects in fights: which effect, how long (minutes = rounds), its data,
// the save
struct SpEff {
    int spell, cls, level;
    int team;                           // the target's side (0 the caster's)
    char save;                          // 'Z' a save leaves them unaffected, 'N' no save (or ignored)
    int minutes;                        // -1: the row's fixed + per level; -2: rolled (lo / hi below)
    int lo, hi;
    int data;                           // -1: the level; -2: don't check
    int effect;                         // 0: the row's
    bool self;                          // on the caster
};
static void sp_fight_effects()
{
    using namespace play;
    const int C = classes::Cleric, M = classes::MagicUser, R = classes::Ranger;
    const SpEff cases[] = {
        {0x01, C, 5, 0, 'N', -1, 0, 0, -1, 0, false},       // Bless
        {0x02, C, 5, 1, 'N', -1, 0, 0, -1, 0, false},       // Curse
        {0x05, C, 5, 0, 'N', -1, 0, 0, -1, 0, true},        // Detect Magic
        {0x06, C, 5, 0, 'N', -1, 0, 0, -1, 0, false},       // Protection from Evil
        {0x07, C, 5, 0, 'N', -1, 0, 0, -1, 0, false},
        {0x08, C, 5, 0, 'N', -1, 0, 0, -1, 0, false},       // Resist Cold
        {0x0A, M, 5, 1, 'Z', -1, 0, 0, -2, 0, false},       // Charm Person
        {0x0B, M, 5, 0, 'N', -1, 0, 0, -1, 0, true},
        {0x10, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},
        {0x11, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},
        {0x13, M, 5, 0, 'N', -1, 0, 0, -1, 0, true},        // Shield
        {0x15, M, 5, 1, 'N', -1, 0, 0, -1, 0, false},       // Sleep
        {0x17, C, 5, 1, 'Z', -1, 0, 0, -1, 0, false},       // Hold Person
        {0x18, C, 5, 0, 'N', -1, 0, 0, -1, 0, false},       // Resist Fire
        {0x19, C, 5, 1, 'N', -1, 0, 0, -1, 0, false},       // Silence (the save ignored)
        {0x1D, M, 5, 0, 'N', -1, 0, 0, -1, 0, true},
        {0x1E, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},       // Invisibility
        {0x20, M, 5, 0, 'N', -1, 0, 0, -2, 0, true},        // Mirror Image
        {0x21, M, 5, 1, 'Z', -1, 0, 0, -1, 0, false},       // Ray of Enfeeblement
        {0x26, C, 5, 1, 'Z', -1, 0, 0, -1, 0, false},       // Cause Blindness
        {0x28, C, 5, 1, 'Z', -2, 10, 60, -1, 0, false},     // Cause Disease: d6 x 10 minutes
        {0x2A, C, 5, 0, 'N', -1, 0, 0, 5, 0, true},         // Prayer: data side x 16 + level
        {0x2C, C, 5, 1, 'Z', -1, 0, 0, -1, 0, false},       // Bestow Curse
        {0x2D, M, 5, 0, 'N', -1, 0, 0, -1, 0, true},        // Blink
        {0x30, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},       // Haste
        {0x31, M, 5, 1, 'Z', -1, 0, 0, -1, 0, false},       // Hold Person (MU)
        {0x32, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},
        {0x34, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},
        {0x35, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},
        {0x36, M, 5, 0, 'N', -1, 0, 0, -1, 0, false},       // Protection from Normal Missiles
        {0x37, M, 5, 1, 'N', -1, 0, 0, -1, 0, false},       // Slow
        {0x39, C, 5, 0, 'N', -2, 5, 20, -2, 0, true},       // speed: 5d4
        {0x3D, C, 5, 1, 'Z', -2, 5, 20, -2, 0, false},      // paralysis: 5d4 (save vs poison)
        {0x3F, C, 5, 0, 'N', -2, 20, 200, -2, 0, false},    // the party invisible: 2d10 x 10 in a fight
        {0x45, C, 7, 0, 'N', -1, 0, 0, -1, 0, false},
        {0x49, C, 9, 0, 'N', -1, 0, 0, -2, 0, true},        // Dispel Evil
        {0x4F, R, 10, 1, 'Z', -1, 0, 0, -1, 0, false},      // Faerie Fire (a druid's: ranger 10 = 3)
        {0x4D, R, 10, 0, 'N', -1, 0, 0, -1, 0, true},       // Detect Magic (the druids')
        {0x50, R, 10, 0, 'N', -1, 0, 0, -1, 0, false},      // Invisibility to Animals
        {0x51, M, 9, 1, 'Z', -1, 0, 0, -2, 0, false},       // Charm Monsters
        {0x52, M, 9, 1, 'Z', -1, 0, 0, 0, 0, false},        // Confusion: data 0
        {0x54, M, 9, 1, 'Z', -1, 0, 0, -2, 0, false},       // Fear
        {0x56, M, 9, 1, 'Z', -1, 0, 0, -2, 0, false},       // Fumble
        {0x58, M, 9, 0, 'N', -1, 0, 0, -1, 0, true},        // Minor Globe
        {0x5D, M, 9, 1, 'Z', -1, 0, 0, -1, 0, false},       // Feeblemind
        {0x5E, M, 9, 1, 'Z', -1, 0, 0, -1, 0, false},       // Hold Monsters
        {0x5F, C, 5, 0, 'N', -1, 0, 0, -2, 0, true},
        {0x60, C, 5, 0, 'N', -1, 0, 0, -2, 0, true},
        {0x61, C, 5, 0, 'N', -1, 0, 0, -2, 0, true},
    };
    const combat::Facts& fx = d->prof->fight.facts;
    for (const SpEff& c : cases) {
        char what[120];
        const combat::FightSpell* fs = sp_fs(c.spell);
        snprintf(what, sizeof what, "fight spell %02X in the engine", c.spell);
        if (!expect(SP, what, fs && fs->does != combat::SpellDoes::NotYet)) continue;
        int effect = c.effect ? c.effect : orow(c.spell, OAffect);
        if (c.spell == 0x52) effect = 0x23;               // (confuse; coab's code says 0x2C - spell_facts.md)
        if (c.spell == 0x54) effect = fx.fear;             // (the fear effect 0x8E)
        if (c.spell == 0x56) effect = fx.fumbling;         // (fumbling 0x1B)
        if (c.spell == 0x5F || c.spell == 0x60 || c.spell == 0x61 || c.spell == 0x3F || c.spell == 0x39 || c.spell == 0x3D)
            effect = orow(c.spell, OAffect);
        if (c.spell == 0x49) effect = fx.sp.evil_bane;
        if (!effect) continue;
        // the level the original counts
        static party::Character probe;
        probe = party::Character{};
        probe.rec[0x109 + c.cls] = static_cast<uint8_t>(c.level);
        probe.rec[0x74] = 7;
        const int L = sp_level_rule(probe, c.spell, false);
        for (int pass = 0; pass < (c.save == 'Z' ? 2 : 1) + (c.minutes == -2 ? 1 : 0); ++pass) {
            if (!sp_new_fight() || !sp_pick()) return;
            sp_caster(sp_c, c.cls, c.level);
            sp_put(sp_c, 0, 10, 12);
            const int tg = c.self ? sp_c : sp_t[0];
            if (!c.self) sp_put(tg, c.team, 11, 12);
            const bool saves = c.save == 'Z' && pass == 1;
            const bool high = c.minutes == -2 && pass == (c.save == 'Z' ? 2 : 1);
            // (a failed save: d20 5 - a 2 with the Hold spells' -3 would wrap round to a save made, the
            // original's byte sum - checked below)
            sp_dice(high ? 1 : 2, saves ? 20 : 5, 100);
            if (c.spell == 0x4F || c.spell == 0x51) {
                // (picked one by one: the last picked is unaffected - a second one picked last)
                sp_put(sp_t[1], 1, 13, 12);
                sp_cast(sp_c, c.spell, {tg, sp_t[1]});
            } else
                sp_cast(sp_c, c.spell, {tg});
            const int got = sp_aff(tg, effect);
            if (saves) {
                snprintf(what, sizeof what, "spell %02X, the save made: unaffected", c.spell);
                expect(SP, what, got < 0 && sp_said(tg, combat::Did::Unaffected) > 0, "effect %s, said unaffected %d",
                       got < 0 ? "none" : "given", sp_said(tg, combat::Did::Unaffected));
                continue;
            }
            snprintf(what, sizeof what, "spell %02X at level %d: effect %02X%s", c.spell, L, effect,
                     c.save == 'Z' ? " (the save failed)" : "");
            if (!expect(SP, what, got >= 0, "effects on the target: %d", F(tg).n_aff ? *F(tg).n_aff : -1)) break;
            const int tm = sp_aff_time(tg, effect), da = sp_aff_data(tg, effect);
            int want_t = orow(c.spell, ODur) + orow(c.spell, ODurL) * L;
            if (c.spell == 0x49) want_t = L;
            if (c.minutes == -2) want_t = high ? c.hi : c.lo;
            snprintf(what, sizeof what, "spell %02X at level %d: lasts %s%s", c.spell, L,
                     c.minutes == -2 ? (high ? "(dice high) " : "(dice low) ") : "", want_t ? "" : "(0: till removed)");
            expect(SP, what, tm == want_t, "%d minutes (original %d)", tm, want_t);
            if (c.data != -2) {
                const int want_d = c.data == -1 ? L : c.data;
                snprintf(what, sizeof what, "spell %02X: the effect's data", c.spell);
                expect(SP, what, da == want_d, "%d (original %d)", da, want_d);
            }
        }
    }
}

// Who a spell takes: sides, how many, persons only, the area
static void sp_fight_targets()
{
    using namespace play;
    combat::Battle* b = nullptr;
    // Bless: the caster's side only, and (in a fight) not those with an enemy next to them
    if (!sp_new_fight() || !sp_pick()) return;
    b = &fg->b;
    sp_caster(sp_c, classes::Cleric, 5);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 0, 12, 12);          // a friend, alone
    sp_put(sp_t[1], 0, 20, 12);          // a friend next to an enemy
    sp_put(sp_t[2], 1, 21, 12);          // that enemy
    sp_dice(1, 10, 100);
    sp_cast(sp_c, 0x01, {sp_t[0], sp_t[1], sp_t[2]});
    expect(SP, "Bless: a friend is blessed", sp_aff(sp_t[0], 1) >= 0);
    expect(SP, "Bless: not the enemy", sp_aff(sp_t[2], 1) < 0);
    expect(SP, "Bless: not a friend with an enemy next to them (in a fight; coab CastTeamSpell)", sp_aff(sp_t[1], 1) < 0);
    // Curse: the other side only
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::Cleric, 5);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 0, 12, 12);
    sp_put(sp_t[1], 1, 14, 12);
    sp_dice(1, 10, 100);
    sp_cast(sp_c, 0x02, {sp_t[0], sp_t[1]});
    expect(SP, "Curse: the other side cursed, not the caster's", sp_aff(sp_t[1], 2) >= 0 && sp_aff(sp_t[0], 2) < 0);
    // Haste: the caster's side, at most `level` of them; a slowed one is cured instead
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::MagicUser, 2);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 0, 11, 12);
    sp_give(sp_t[0], 0x2A, 5, 1, false);
    sp_put(sp_t[1], 0, 12, 12);
    sp_put(sp_t[2], 0, 13, 12);
    sp_put(sp_t[3], 1, 14, 12);
    sp_dice(1, 10, 100);
    sp_cast(sp_c, 0x30, {sp_t[3], sp_t[0], sp_t[1], sp_t[2]});
    expect(SP, "Haste: a slowed one is cured, not hasted", sp_aff(sp_t[0], 0x2A) < 0 && sp_aff(sp_t[0], 0x27) < 0);
    expect(SP, "Haste: the next of the caster's side hasted", sp_aff(sp_t[1], 0x27) >= 0);
    expect(SP, "Haste at level 2: no third one", sp_aff(sp_t[2], 0x27) < 0);
    expect(SP, "Haste: not the enemy", sp_aff(sp_t[3], 0x27) < 0);
    // Slow: the other side, at most `level` of them; a hasted one's Haste is cured instead (coab
    // RemoveComplimentSpellFirst)
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::MagicUser, 2);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 11, 12);
    sp_give(sp_t[0], 0x27, 5, 1, false);
    sp_put(sp_t[1], 1, 12, 12);
    sp_put(sp_t[2], 1, 13, 12);
    sp_put(sp_t[3], 0, 14, 12);
    sp_dice(1, 10, 100);
    sp_cast(sp_c, 0x37, {sp_t[3], sp_t[0], sp_t[1], sp_t[2]});
    expect(SP, "Slow: a hasted enemy's Haste is cured, not slowed", sp_aff(sp_t[0], 0x27) < 0 && sp_aff(sp_t[0], 0x2A) < 0,
           "haste %d slow %d", sp_aff(sp_t[0], 0x27), sp_aff(sp_t[0], 0x2A));
    expect(SP, "Slow: the next enemy slowed", sp_aff(sp_t[1], 0x2A) >= 0);
    expect(SP, "Slow at level 2: no third enemy", sp_aff(sp_t[2], 0x2A) < 0);
    expect(SP, "Slow: not the caster's side", sp_aff(sp_t[3], 0x2A) < 0);
    // Hold Person: persons only (monster type 0 / 1, not large); the save's bonus by count
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::Cleric, 5);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 11, 12);
    F(sp_t[0]).rec[0x11A] = 7;
    sp_put(sp_t[1], 1, 12, 12);
    F(sp_t[1]).rec[0xDE] = 2;
    sp_dice(1, 2, 100);
    sp_cast(sp_c, 0x17, {sp_t[0], sp_t[1]});
    expect(SP, "Hold Person: not a non-person (monster type 7)", sp_aff(sp_t[0], 0x34) < 0);
    expect(SP, "Hold Person: not a large one", sp_aff(sp_t[1], 0x34) < 0);
    sp_dice(1, 5, 100);
    sp_cast(sp_c, 0x5E, {sp_t[0]});
    expect(SP, "Hold Monsters: a non-person too", sp_aff(sp_t[0], 0x34) >= 0);
    // The save's total is a byte compared unsigned (listing ovr024:12F1 do_saving_throw): a 2 with -3 is
    // 255 - saved
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::MagicUser, 9);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 11, 12);
    sp_dice(1, 2, 100);
    sp_cast(sp_c, 0x31, {sp_t[0]});
    expect(SP, "Hold Person (MU) on one, d20 2 at -3: the byte wraps - saved", sp_aff(sp_t[0], 0x34) < 0);
    struct HoldBonus {
        int spell, n, bonus;
    };
    const HoldBonus hb[] = {{0x17, 1, -2}, {0x31, 1, -3}, {0x5E, 1, -3}, {0x17, 2, -1}, {0x31, 3, 0}, {0x5E, 4, 0}};
    for (const HoldBonus& h : hb) {
        // save number 14: a 16 saves with bonus -2 (16 - 2 = 14), not with -3
        for (int roll : {14 - h.bonus, 13 - h.bonus}) {
            if (!sp_new_fight() || !sp_pick()) return;
            sp_caster(sp_c, classes::MagicUser, 9);
            sp_caster(sp_c, h.spell == 0x17 ? classes::Cleric : classes::MagicUser, 9);
            sp_put(sp_c, 0, 10, 12);
            int tg[4];
            for (int k = 0; k < h.n; ++k) {
                sp_put(sp_t[k], 1, 11 + k, 12);
                F(sp_t[k]).rec[0xDF + 4] = 14;
                tg[k] = sp_t[k];
            }
            sp_dice(1, roll, 100);
            const combat::FightSpell* fs = sp_fs(h.spell);
            sp_n = combat::cast(fg->b, mrules->tables, sp_c, h.spell, *fs, tg, h.n, rng, sp_l, 24, 0);
            const bool held = sp_aff(sp_t[0], 0x34) >= 0;
            const bool want = roll < 14 - h.bonus;
            char what[100];
            snprintf(what, sizeof what, "spell %02X on %d: the save at %+d (d20 %d against 14: %s)", h.spell, h.n, h.bonus, roll,
                     want ? "held" : "saved");
            expect(SP, what, held == want, "held %d", (int)held);
        }
    }
    // Charm Person: persons only ("is unaffected")
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::MagicUser, 5);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 11, 12);
    F(sp_t[0]).rec[0x11A] = 5;
    sp_dice(1, 2, 100);
    sp_cast(sp_c, 0x0A, {sp_t[0]});
    expect(SP, "Charm Person: not a non-person", sp_aff(sp_t[0], 0x0B) < 0 && F(sp_t[0]).team() == 1);
    sp_put(sp_t[1], 1, 12, 12);
    sp_cast(sp_c, 0x0A, {sp_t[1]});
    expect(SP, "Charm Person: a person charmed joins the caster's side", sp_aff(sp_t[1], 0x0B) >= 0 && F(sp_t[1]).team() == 0);
    // Sleep: 4d4 of Hit Dice costs (1 HD 1, 2 2, 3 4, 4 6, 5 10 a monster / 20, more 20)
    for (int hd = 0; hd <= 6; ++hd) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, classes::MagicUser, 5);
        sp_put(sp_c, 0, 10, 12);
        for (int k = 0; k < 6; ++k) {
            sp_put(sp_t[k], 1, 11 + k, 12);
            F(sp_t[k]).rec[0xE5] = static_cast<uint8_t>(hd);
            F(sp_t[k]).rec[0x74] = 0;           // (race 0: a monster)
        }
        sp_dice(1, 10, 100);                    // 4d4 = 16
        sp_cast(sp_c, 0x15, {sp_t[0], sp_t[1], sp_t[2], sp_t[3], sp_t[4], sp_t[5]});
        int asleep = 0;
        for (int k = 0; k < 6; ++k) asleep += sp_aff(sp_t[k], 0x35) >= 0;
        int want = 0, budget = 16;
        for (int k = 0; k < 6; ++k)
            if (sp_sleep[hd] <= budget) {
                budget -= sp_sleep[hd];
                ++want;
            }
        char what[80];
        snprintf(what, sizeof what, "Sleep (4d4 = 16) on six of %d Hit Dice", hd);
        expect(SP, what, asleep == want, "%d asleep (original %d)", asleep, want);
    }
    // The areas: those within the spell's radius of the square (aim 8-14: & 7)
    struct Area {
        int spell, r;
    };
    const Area areas[] = {{0x01, 2}, {0x15, 1}, {0x29, 1}, {0x2F, 3}, {0x30, 2}, {0x37, 2}, {0x57, 2}, {0x52, 3}, {0x4E, 3}};
    for (const Area& a : areas) {
        const int r = orow(a.spell, OAim) & 7;
        char what[80];
        snprintf(what, sizeof what, "spell %02X's area: radius %d (the table's aim %d)", a.spell, a.r, orow(a.spell, OAim));
        expect(SP, what, r == a.r && orow(a.spell, OAim) >= 8);
        if (!sp_new_fight() || !sp_pick()) return;
        // in_area: one on each square at distance r and r + 1 on a straight line
        sp_put(sp_t[0], 1, 20 + a.r, 12);
        sp_put(sp_t[1], 1, 20 + a.r + 1, 12);
        sp_put(sp_t[2], 1, 20 - a.r, 12 - a.r);    // slanted: r diagonal steps
        int in[24];
        const int n = combat::in_area(fg->b, fg->t, 20, 12, a.r, in, 24);
        bool has0 = false, has1 = false, has2 = false;
        for (int k = 0; k < n; ++k) {
            has0 = has0 || in[k] == sp_t[0];
            has1 = has1 || in[k] == sp_t[1];
            has2 = has2 || in[k] == sp_t[2];
        }
        snprintf(what, sizeof what, "spell %02X's area: %d squares straight in, %d out", a.spell, a.r, a.r + 1);
        expect(SP, what, has0 && !has1);
        (void)has2;
    }
    // Fireball outdoors: radius 2 (coab sub_5F782: the targets rebuilt round the square), indoors 3
    for (int outdoors = 0; outdoors < 2; ++outdoors) {
        if (!sp_new_fight() || !sp_pick()) return;
        fg->b.indoors = !outdoors;
        sp_caster(sp_c, classes::MagicUser, 6);
        sp_put(sp_c, 0, 5, 12);
        sp_put(sp_t[0], 1, 20, 12);
        sp_put(sp_t[1], 1, 23, 12);           // 3 squares from the centre
        sp_dice(1, 2, 100);
        int in[24];
        const int n = combat::in_area(fg->b, fg->t, 20, 12, orow(0x2F, OAim) & 7, in, 24);
        fg->b.aim_x = 20, fg->b.aim_y = 12;
        const combat::FightSpell* fs = sp_fs(0x2F);
        sp_n = combat::cast(fg->b, mrules->tables, sp_c, 0x2F, *fs, in, n, rng, sp_l, 24, 0);
        char what[80];
        snprintf(what, sizeof what, "Fireball %s: one 3 squares from the centre %s", outdoors ? "outdoors" : "indoors",
                 outdoors ? "untouched (radius 2)" : "burnt (radius 3)");
        expect(SP, what, (F(sp_t[1]).hp() < 200) == !outdoors, "HP %d", F(sp_t[1]).hp());
    }
    // Lightning Bolt: level d6 on the square aimed at and on along the line, 7 squares, half on a save
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, classes::MagicUser, 6);
    sp_put(sp_c, 0, 10, 12);
    sp_put(sp_t[0], 1, 13, 12);
    sp_put(sp_t[1], 1, 15, 12);
    sp_put(sp_t[2], 1, 17, 12);
    sp_put(sp_t[3], 1, 19, 12);
    sp_put(sp_t[4], 1, 21, 12);
    sp_dice(1, 2, 100);
    fg->b.aim_x = 13, fg->b.aim_y = 12;
    sp_cast(sp_c, 0x33, {sp_t[0]});
    {
        const int d0 = 200 - F(sp_t[0]).hp(), d1 = 200 - F(sp_t[1]).hp(), d2 = 200 - F(sp_t[2]).hp(),
                  d3 = 200 - F(sp_t[3]).hp(), d4 = 200 - F(sp_t[4]).hp();
        expect(SP, "Lightning Bolt (level 6, dice high): 36 on the target square and at 2, 4, 6 squares beyond",
               d0 == 36 && d1 == 36 && d2 == 36 && d3 == 36, "%d %d %d %d", d0, d1, d2, d3);
        expect(SP, "Lightning Bolt: 7 squares from the square aimed at (none 8 beyond)", d4 == 0, "%d", d4);
    }
    // Cone of Cold: a cone of (level + 1) / 2 squares, 2 rays
    {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, classes::MagicUser, 9);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 15, 12);           // 5 squares: in
        sp_put(sp_t[1], 1, 16, 12);           // 6: out
        int in[24];
        const int n = combat::cone(fg->b, fg->t, sp_c, 15, 12, (9 + 1) / 2, 2, in, 24);
        bool a = false, bb = false;
        for (int k = 0; k < n; ++k) {
            a = a || in[k] == sp_t[0];
            bb = bb || in[k] == sp_t[1];
        }
        expect(SP, "Cone of Cold at level 9: 5 squares long", a && !bb, "in: 5th %d 6th %d", (int)a, (int)bb);
    }
    (void)b;
}

static void sp_check_fights()
{
    sp_fight_damage();
    sp_fight_heal();
    sp_fight_effects();
    sp_fight_targets();
}
// ---- 5. Spells in camp (spells::cast, as the camp's Cast runs it)
static spells::Line sp_cl[24];
static int sp_cn = 0;
static const spells::CampSpell* sp_camp(int s)
{
    const auto& m = play::d->prof->magic;
    for (int k = 0; k < m.n_camp; ++k)
        if (m.camp[k].spell == s) return &m.camp[k];
    return nullptr;
}
static party::Character& sp_member(int i) { return play::pt->m[i]; }
// Party member i: only class `cls` at `level`, all HP, nothing on them
static void sp_member(int i, int cls, int level, int hp = 100)
{
    party::Character& c = sp_member(i);
    for (int k = 0; k < 8; ++k) c.rec[0x109 + k] = c.rec[0x111 + k] = 0;
    if (cls >= 0) c.rec[0x109 + cls] = static_cast<uint8_t>(level);
    c.rec[0x74] = 7;
    c.rec[0xE6] = 0;
    c.rec[0x1A4] = c.rec[0x78] = static_cast<uint8_t>(hp);
    c.rec[0x195] = party::Okay;
    c.rec[0x196] = 1;
    c.rec[0x197] = 0;
    c.n_affects = 0;
    for (int k = 0; k < 5; ++k) c.rec[0xDF + k] = 15;
    c.rec[0x186] = 0;
}
static int sp_ccast(int caster, int target, int s, int flame = 0)
{
    using namespace play;
    sp_cn = 0;
    const spells::CampSpell* cs = sp_camp(s);
    if (!cs) return -1;
    sp_cn = spells::cast(*pt, caster, target, *cs, mrules->tables, d->prof->cures, d->prof->magic.facts, rng, sp_cl, 24, 0,
                         flame);
    return sp_cn;
}
static int sp_csaid(int who, spells::Said s)
{
    int n = 0;
    for (int k = 0; k < sp_cn; ++k) n += sp_cl[k].who == who && sp_cl[k].what == s;
    return n;
}
static int sp_caff(int i, int type, int* minutes = nullptr, int* data = nullptr)
{
    const party::Character& c = sp_member(i);
    for (int k = 0; k < c.n_affects; ++k)
        if (c.affects[k][0] == type) {
            if (minutes) *minutes = c.affects[k][1] | c.affects[k][2] << 8;
            if (data) *data = c.affects[k][3];
            return k;
        }
    return -1;
}

static void sp_check_camp()
{
    using namespace play;
    const int C = classes::Cleric, M = classes::MagicUser;
    if (!boot() || !load_magic()) return;
    // Every spell that can be cast outside fights (when camp / both, a target
    // other than "combat") is in the engine
    {
        int missing = 0;
        char list[200] = "";
        for (int s = 1; s <= 0x64; ++s) {
            if (orow(s, OWhen) == 1 || orow(s, OTargets) == 0) continue;
            // (Animate Dead 0x24 / 0x5A: monsters' only - no character learns it
            // and no item of Curse casts it, so it is never cast in camp)
            if (s == 0x24 || s == 0x5A) continue;
            const spells::CampSpell* cs = sp_camp(s);
            if (cs && cs->does != spells::Does::NotYet) continue;
            ++missing;
            const size_t l = strlen(list);
            snprintf(list + l, sizeof list - l, " %02X", s);
        }
        expect(SP, "every spell castable in camp is in the engine", !missing, "%d not:%s", missing, list);
    }
    // Healing: the dice, never past their most
    struct H {
        int spell, lo, hi;
    };
    for (const H& h : {H{0x03, 1, 8}, H{0x3A, 3, 17}, H{0x47, 6, 27}, H{0x3E, 4, 10}, H{0x63, 4, 10}})
        for (int pass = 0; pass < 2; ++pass) {
            sp_member(4, C, 9);
            sp_member(1, classes::Fighter, 5, 100);
            sp_member(1).rec[0x1A4] = 40;
            sp_dice(pass ? 1 : 2);
            // (the items' healing 0x3E / 0x63 is on the user: the row's target "self")
            if (orow(h.spell, OTargets) == 1) sp_ccast(1, 1, h.spell);
            else sp_ccast(4, 1, h.spell);
            const int got = sp_member(1).hp() - 40, want = pass ? h.hi : h.lo;
            char what[80];
            snprintf(what, sizeof what, "camp: healing spell %02X, dice %s", h.spell, pass ? "high" : "low");
            expect(SP, what, got == want, "%d (original %d)", got, want);
        }
    // Effects: on whom, which, how long, the data
    struct E {
        int spell, cls, level;
        int data;                       // -1 the level, -2 don't check
        int targets;                    // 1 self, 2 one member, 4 the party (the row's)
    };
    const E effects[] = {
        {0x01, C, 7, -1, 4},  {0x05, C, 7, -1, 1},  {0x06, C, 7, -1, 2},  {0x07, C, 7, -1, 2},  {0x08, C, 7, -1, 2},
        {0x0B, M, 7, -1, 1},  {0x10, M, 7, -1, 2},  {0x11, M, 7, -1, 2},  {0x12, M, 7, -1, 1},  {0x13, M, 7, -1, 1},
        {0x16, C, 7, -1, 1},  {0x18, C, 7, -1, 2},  {0x1D, M, 7, -1, 1},  {0x1E, M, 7, -1, 2},  {0x20, M, 7, -2, 1},
        {0x2A, C, 7, 7, 4},   {0x30, M, 7, -1, 4},  {0x32, M, 7, -1, 4},  {0x34, M, 7, -1, 2},  {0x35, M, 7, -1, 2},
        {0x36, M, 7, -1, 2},  {0x45, C, 9, -1, 2},  {0x4D, classes::Ranger, 10, -1, 1}, {0x50, classes::Ranger, 10, -1, 2},
        {0x58, M, 9, -1, 1},  {0x5F, C, 5, 12, 1},  {0x60, C, 5, 12, 1},  {0x61, C, 5, 12, 1},
    };
    for (const E& e : effects) {
        for (int i = 0; i < pt->count; ++i) sp_member(i, classes::Fighter, 3);
        sp_member(4, e.cls, e.level);
        static party::Character probe;
        probe = sp_member(4);
        const int L = sp_level_rule(probe, e.spell, false);
        sp_dice(1, 10, 100);
        const int n = sp_ccast(4, 1, e.spell);
        const int eff = orow(e.spell, OAffect);
        char what[100];
        snprintf(what, sizeof what, "camp: spell %02X in the engine", e.spell);
        if (!expect(SP, what, n >= 0 && eff)) continue;
        int who_ok = 1;
        for (int i = 0; i < pt->count; ++i) {
            const bool want = e.targets == 4 || (e.targets == 1 && i == 4) || (e.targets == 2 && i == 1);
            if ((sp_caff(i, eff) >= 0) != want) who_ok = 0;
        }
        snprintf(what, sizeof what, "camp: spell %02X on %s", e.spell,
                 e.targets == 4 ? "the whole party" : e.targets == 1 ? "the caster" : "the member picked");
        expect(SP, what, who_ok);
        int tm = -1, da = -1;
        sp_caff(e.targets == 2 ? 1 : 4, eff, &tm, &da);
        const int want_t = orow(e.spell, ODur) + orow(e.spell, ODurL) * L;
        snprintf(what, sizeof what, "camp: spell %02X at level %d lasts %d minutes", e.spell, L, want_t);
        expect(SP, what, tm == want_t, "%d", tm);
        if (e.data != -2) {
            const int want_d = e.data == -1 ? L : e.data;
            snprintf(what, sizeof what, "camp: spell %02X's effect data", e.spell);
            expect(SP, what, da == want_d, "%d (original %d)", da, want_d);
        }
    }
    // Mirror Image: d4 images (data (d4 << 4) + level)
    sp_member(5, M, 7);
    sp_dice(1);
    sp_ccast(5, 5, 0x20);
    {
        int da = -1;
        sp_caff(5, 0x1C, nullptr, &da);
        expect(SP, "camp: Mirror Image data (d4 << 4) + level (dice high: 4 images, level 7)", da == (4 << 4) + 7, "%d", da);
    }
    // Haste in camp: the first `level` members; a slowed one cured ("is Cured")
    for (int i = 0; i < pt->count; ++i) sp_member(i, classes::Fighter, 3);
    sp_member(5, M, 3);
    spells::add_affect(sp_member(0), 0x2A, 10, 1, false);
    sp_ccast(5, 0, 0x30);
    {
        int hasted = 0;
        for (int i = 0; i < pt->count; ++i) hasted += sp_caff(i, 0x27) >= 0;
        expect(SP, "camp: Haste at level 3 - the slowed first member cured, not hasted",
               sp_caff(0, 0x2A) < 0 && sp_caff(0, 0x27) < 0 && sp_csaid(0, spells::Said::Cured) == 1);
        expect(SP, "camp: Haste at level 3 - two more hasted (3 in all counted)", hasted == 2, "%d hasted", hasted);
    }
    // Enlarge: Strength by the caster's level, only when more than theirs
    for (int lv = 1; lv <= 12; ++lv) {
        for (int i = 0; i < pt->count; ++i) sp_member(i, classes::Fighter, 3);
        sp_member(5, M, lv);
        sp_member(1).rec[0x10] = sp_member(1).rec[0x11] = 10;
        sp_member(1).rec[0x1C] = sp_member(1).rec[0x1D] = 0;
        sp_ccast(5, 1, 0x0C);
        int da = -1, tm = -1;
        const bool on = sp_caff(1, 0x0C, &tm, &da) >= 0;
        const int s = sp_enl[lv][0], s00 = sp_enl[lv][1];
        const int want_d = s == 18 ? s00 + 1 : s + 100;
        char what[100];
        snprintf(what, sizeof what, "camp: Enlarge at level %d on Str 10: Str %d/%02d, %d minutes", lv, s, s00, 10 * lv);
        expect(SP, what, on && da == want_d && tm == 10 * lv && sp_csaid(1, spells::Said::Word) == 1, "on %d data %d (original %d) time %d",
               (int)on, da, want_d, tm);
    }
    for (int i = 0; i < pt->count; ++i) sp_member(i, classes::Fighter, 3);
    sp_member(5, M, 3);
    sp_member(1).rec[0x10] = sp_member(1).rec[0x11] = 18;
    sp_member(1).rec[0x1C] = sp_member(1).rec[0x1D] = 60;
    sp_ccast(5, 1, 0x0C);
    expect(SP, "camp: Enlarge at level 3 (18/51) on 18/60: \"is unaffected\"",
           sp_caff(1, 0x0C) < 0 && sp_csaid(1, spells::Said::Unaffected) == 1);
    // Reduce: a failed save takes Enlarge away ("has been reduced"); saved: nothing at all
    for (int pass = 0; pass < 2; ++pass) {
        sp_member(1, classes::Fighter, 3);
        sp_member(5, M, 3);
        spells::add_affect(sp_member(1), 0x0C, 30, 52, false);
        sp_dice(1, pass ? 20 : 2);
        sp_ccast(5, 1, 0x0D);
        if (!pass)
            expect(SP, "camp: Reduce, the save failed - Enlarge gone", sp_caff(1, 0x0C) < 0 && sp_csaid(1, spells::Said::Word) == 1);
        else
            expect(SP, "camp: Reduce, the save made - nothing, nothing said", sp_caff(1, 0x0C) >= 0 && sp_cn == 0);
    }
    // Friends: the caster's Charisma + 2d4 for `level` minutes
    sp_member(5, M, 4);
    sp_dice(1);
    sp_ccast(5, 5, 0x0E);
    {
        int tm = -1, da = -1;
        sp_caff(5, 0x0E, &tm, &da);
        expect(SP, "camp: Friends - data 2d4 (8 high), 4 minutes at level 4", da == 8 && tm == 4, "data %d time %d", da, tm);
    }
    // Strength: the die by class (magic-user d4, cleric / thief d6, fighter d8), 60 x level minutes, data 100 + roll
    struct St {
        int cls, die;
    };
    for (const St& s : {St{M, 4}, St{C, 6}, St{classes::Thief, 6}, St{classes::Fighter, 8}}) {
        sp_member(1, s.cls, 3);
        sp_member(1).rec[0x10] = sp_member(1).rec[0x11] = 10;
        sp_member(5, M, 5);
        sp_dice(1);
        sp_ccast(5, 1, 0x23);
        int tm = -1, da = -1;
        sp_caff(1, 0x26, &tm, &da);
        char what[100];
        snprintf(what, sizeof what, "camp: Strength on class %d - d%d (high), 300 minutes at level 5", s.cls, s.die);
        expect(SP, what, da == 100 + s.die && tm == 300 && sp_cn == 0, "data %d time %d lines %d", da, tm, sp_cn);
    }
    // Dispel Magic: each effect (but data 0xFF) by level against level
    struct Dc {
        int L, lvl, chance;
    };
    for (const Dc& dc : {Dc{9, 5, 70}, Dc{5, 9, 42}, Dc{6, 6, 50}}) {
        for (int roll : {dc.chance, dc.chance + 1}) {
            sp_member(1, classes::Fighter, 3);
            sp_member(4, C, dc.L);
            spells::add_affect(sp_member(1), 0x01, 30, dc.lvl, false);
            spells::add_affect(sp_member(1), 0x3B, 0, 0xFF, false);
            sp_dice(1, 10, roll);
            sp_ccast(4, 1, 0x29);
            char what[100];
            snprintf(what, sizeof what, "camp: Dispel Magic level %d on a level %d effect, d100 %d (chance %d%%)", dc.L, dc.lvl, roll,
                     dc.chance);
            const bool gone = sp_caff(1, 0x01) < 0;
            expect(SP, what, gone == (roll <= dc.chance) && sp_caff(1, 0x3B) >= 0, "removed %d, the 0xFF one kept %d", (int)gone,
                   (int)(sp_caff(1, 0x3B) >= 0));
        }
    }
    // Fire Shield: hot - 0x32 and the zap 0x8F, "is protected"; cold - 0x36 and 0x8F, nothing said; 2 + level minutes
    for (int flame = 1; flame <= 2; ++flame) {
        sp_member(5, M, 9);
        sp_ccast(5, 5, 0x55, flame);
        int tm = -1, tz = -1;
        const bool sh = sp_caff(5, flame == 1 ? 0x32 : 0x36, &tm) >= 0, zap = sp_caff(5, 0x8F, &tz) >= 0;
        char what[100];
        snprintf(what, sizeof what, "camp: Fire Shield %s - its effect and the zap for 11 minutes, %s", flame == 1 ? "hot" : "cold",
                 flame == 1 ? "\"is protected\"" : "nothing said");
        expect(SP, what, sh && zap && tm == 11 && tz == 11 && sp_cn == (flame == 1 ? 1 : 0), "shield %d zap %d time %d/%d lines %d",
               (int)sh, (int)zap, tm, tz, sp_cn);
    }
    // The cures
    sp_member(1, classes::Fighter, 3);
    sp_member(4, C, 9);
    spells::add_affect(sp_member(1), 0x21, 0, 9, false);
    sp_ccast(4, 1, 0x25);
    expect(SP, "camp: Cure Blindness - \"is Cured\", \"can see\"",
           sp_caff(1, 0x21) < 0 && sp_csaid(1, spells::Said::Cured) == 1 && sp_csaid(1, spells::Said::CanSee) == 1);
    sp_member(1, classes::Fighter, 3);
    spells::add_affect(sp_member(1), 0x2B, 60, 9, true);
    spells::add_affect(sp_member(1), 0x2C, 10, 9, true);
    spells::add_affect(sp_member(1), 0x1F, 0, 0xFF, false);
    sp_ccast(4, 1, 0x27);
    expect(SP, "camp: Cure Disease - weakness (0x2B) cured, with 0x2C and helpless (0x1F)",
           sp_caff(1, 0x2B) < 0 && sp_caff(1, 0x2C) < 0 && sp_caff(1, 0x1F) < 0 && sp_csaid(1, spells::Said::Cured) == 1);
    sp_member(1, classes::Fighter, 3);
    spells::add_affect(sp_member(1), 0x22, 30, 9, true);
    sp_ccast(4, 1, 0x27);
    expect(SP, "camp: Cure Disease - the disease (0x22) cured", sp_caff(1, 0x22) < 0 && sp_csaid(1, spells::Said::Cured) == 1);
    // Neutralize Poison
    sp_member(1, classes::Fighter, 3);
    spells::add_affect(sp_member(1), 0x37, 0, 0xFF, false);
    sp_member(1).rec[0x1A4] = 0;
    sp_member(1).rec[0x195] = party::Dead;
    sp_member(1).rec[0x196] = 0;
    sp_ccast(4, 1, 0x43);
    expect(SP, "camp: Neutralize Poison on the poisoned dead - 1 HP, okay, \"is unpoisoned\"",
           sp_caff(1, 0x37) < 0 && sp_member(1).hp() == 1 && sp_member(1).health() == party::Okay && sp_csaid(1, spells::Said::Unpoisoned) == 1,
           "HP %d status %d", sp_member(1).hp(), sp_member(1).health());
    sp_member(1, classes::Fighter, 3);
    sp_ccast(4, 1, 0x43);
    expect(SP, "camp: Neutralize Poison on the unpoisoned - \"is unaffected\"", sp_csaid(1, spells::Said::Unaffected) == 1);
    // Slow Poison: the poisoned - effect 0x16 (data 0xFF) 60 x level, poison damage 0x0F every 10 minutes
    sp_member(1, classes::Fighter, 3);
    spells::add_affect(sp_member(1), 0x37, 0, 0xFF, false);
    sp_member(1).rec[0x1A4] = 0;
    sp_ccast(4, 1, 0x1A);
    {
        int tm = -1, da = -1, tp = -1;
        sp_caff(1, 0x16, &tm, &da);
        sp_caff(1, 0x0F, &tp);
        expect(SP, "camp: Slow Poison at level 9 - 540 minutes, data 0xFF, poison damage every 10, 1 HP",
               tm == 540 && da == 0xFF && tp == 10 && sp_member(1).hp() == 1, "time %d data %d pd %d HP %d", tm, da, tp, sp_member(1).hp());
    }
    sp_member(1, classes::Fighter, 3);
    sp_ccast(4, 1, 0x1A);
    expect(SP, "camp: Slow Poison on the unpoisoned - nothing, nothing said", sp_member(1).n_affects == 0 && sp_cn == 0);
    // Remove Curse: Bestow Curse cured ("is Cured", "is un-cursed"); else the first cursed item comes off
    sp_member(1, classes::Fighter, 3);
    spells::add_affect(sp_member(1), 0x24, 30, 5, false);
    sp_ccast(4, 1, 0x2B);
    expect(SP, "camp: Remove Curse - Bestow Curse cured, \"is Cured\", \"is un-cursed\"",
           sp_caff(1, 0x24) < 0 && sp_csaid(1, spells::Said::Cured) == 1 && sp_csaid(1, spells::Said::Uncursed) == 1);
    // Raise Dead: the dead (not elves, Con > 0) - okay, 1 HP, Con - 1
    for (int race : {7, 2}) {
        sp_member(1, classes::Fighter, 3);
        sp_member(1).rec[0x74] = static_cast<uint8_t>(race);
        sp_member(1).rec[0x18] = sp_member(1).rec[0x19] = 12;
        sp_member(1).rec[0x1A4] = 0;
        sp_member(1).rec[0x195] = party::Dead;
        sp_member(1).rec[0x196] = 0;
        sp_ccast(4, 1, 0x4B);
        if (race == 7)
            expect(SP, "camp: Raise Dead on a dead human - okay, 1 HP, Con 12 -> 11, \"is raised\"",
                   sp_member(1).health() == party::Okay && sp_member(1).hp() == 1 && sp_member(1).rec[0x18] == 11 && sp_csaid(1, spells::Said::Raised) == 1,
                   "status %d HP %d Con %d", sp_member(1).health(), sp_member(1).hp(), sp_member(1).rec[0x18]);
        else
            expect(SP, "camp: Raise Dead on a dead elf - nothing", sp_member(1).health() == party::Dead && sp_cn == 0);
    }
    // Giant strength (the potion, 0x3B): Strength 21, (d4 + 4) x 10 minutes
    sp_member(1, classes::Fighter, 3);
    sp_member(1).rec[0x10] = sp_member(1).rec[0x11] = 16;
    sp_dice(2);
    {
        const spells::CampSpell* cs = sp_camp(0x3B);
        if (cs) sp_cn = spells::cast(*pt, 1, 1, *cs, mrules->tables, d->prof->cures, d->prof->magic.facts, rng, sp_cl, 24, 12);
        int tm = -1, da = -1;
        sp_caff(1, 0x92, &tm, &da);
        expect(SP, "camp: the giant strength potion - data 121 (Str 21), 50 minutes with a low d4, \"is stronger\"",
               da == 121 && tm == 50 && sp_csaid(1, spells::Said::Word) == 1, "data %d time %d", da, tm);
    }
    // Spiritual Hammer: its effect on the caster, `level` minutes
    sp_member(4, C, 6);
    sp_ccast(4, 4, 0x1C);
    {
        int tm = -1, da = -1;
        sp_caff(4, 0x17, &tm, &da);
        expect(SP, "camp: Spiritual Hammer at level 6 - its effect for 6 minutes, data 6", tm == 6 && da == 6, "time %d data %d", tm,
               da);
    }
    // The stats with the spells' effects (the recalculation; spell_facts.md 0x0C, 0x0E, 0x23, 0x3B, 0x5D)
    struct Rc {
        const char* what;
        int effect, data;
        int str, str00, stat, stat_idx;     // the Strength (str00) or stat stat_idx (-1: Str) wanted
    };
    const Rc rcs[] = {
        {"Enlarge 18/51 on Str 10: 18/51", 0x0C, 52, 18, 51, 0, -1},
        {"Enlarge 20 on Str 10: 20", 0x0C, 120, 20, 0, 0, -1},
        {"Strength +3 on Str 10: 13", 0x26, 103, 13, 0, 0, -1},
        {"giant strength (21) on Str 10: 21", 0x92, 121, 21, 0, 0, -1},
        {"Friends +5 on Cha 10: 15", 0x0E, 5, 0, 0, 15, 5},
        {"Feeblemind: Int 3", 0x44, 9, 0, 0, 3, 1},
        {"Feeblemind: Wis 3", 0x44, 9, 0, 0, 3, 2},
    };
    for (const Rc& r : rcs) {
        sp_member(1, classes::Thief, 3);
        party::Character& c = sp_member(1);
        for (int k = 0; k < 6; ++k) c.rec[0x10 + 2 * k] = c.rec[0x11 + 2 * k] = 10;
        c.rec[0x1C] = c.rec[0x1D] = 0;
        c.n_items = 0;
        spells::add_affect(c, r.effect, 30, r.data, false);
        rules::recalc(c, *names, d->facts);
        char what[100];
        snprintf(what, sizeof what, "stats: %s", r.what);
        if (r.stat_idx < 0)
            expect(SP, what, c.stat(0) == r.str && c.str00() == r.str00, "Str %d/%02d", c.stat(0), c.str00());
        else
            expect(SP, what, c.stat(r.stat_idx) == r.stat, "%d", c.stat(r.stat_idx));
    }
    // The camp clock: an effect of N minutes is gone after N minutes (coab: removed when the time left <= the step)
    sp_member(1, classes::Fighter, 3);
    spells::add_affect(sp_member(1), 0x01, 6, 1, false);
    spells::add_affect(sp_member(1), 0x3B, 0, 0xFF, false);
    magic::tick_affects(sp_member(1), 5);
    const bool after5 = sp_caff(1, 0x01) >= 0;
    magic::tick_affects(sp_member(1), 1);
    expect(SP, "camp clock: a 6-minute effect lasts 6 minutes; time 0 stays", after5 && sp_caff(1, 0x01) < 0 && sp_caff(1, 0x3B) >= 0);
}
// ---- 6. What the effects do in fights: to hit, AC, saves, damage, actions
// One blow by a (to-hit 0) with d20 `roll` at b (stored AC `ac`): hit?
static bool sp_blow(int a, int c, int roll, int ac, int tohit = 0)
{
    using namespace play;
    combat::Fighter& at = F(a);
    at.rec[0x199] = static_cast<uint8_t>(tohit);
    at.attacks[0] = 1;
    at.attacks[1] = 0;
    at.rec[0x19E] = 1;
    at.rec[0x1A0] = 1;
    at.rec[0x1A2] = 0;
    F(c).rec[0x19A] = F(c).rec[0x19B] = static_cast<uint8_t>(ac);
    F(c).received = 0;
    fg->b.to_hit_party = fg->b.to_hit_monsters = 0;
    sp_dice(1, roll, 100);
    const combat::Attack r = combat::attack(fg->b, a, c, nullptr, rng);
    return r.n > 0 && r.hits[0].hit;
}
// A save by fighter i (type, its number 12) with d20 `roll`, `actor` acting
static bool sp_saves(int i, int type, int roll, int actor)
{
    using namespace play;
    for (int k = 0; k < 5; ++k) F(i).rec[0xDF + k] = 12;
    combat::set_actor(fg->b, actor);
    sp_dice(1, roll, 100);
    return combat::saving_throw(F(i), type, 0, rng);
}
// 40 points of damage of `kind`, half on a save vs spells (number 12), d20 `roll`: what was taken
static int sp_harm_saved(int i, int kind, int roll, int actor)
{
    using namespace play;
    for (int k = 0; k < 5; ++k) F(i).rec[0xDF + k] = 12;
    combat::set_actor(fg->b, actor);
    sp_dice(1, roll, 100);
    combat::Harm h;
    h.kind = kind;
    h.save = 4;
    h.on_save = 2;
    return combat::harm(fg->b, i, 40, h, rng, nullptr);
}

static void sp_check_effects()
{
    using namespace play;
    if (!sp_new_fight() || !sp_pick()) return;
    const int A = sp_t[0], T = sp_t[1], O = sp_t[2];
    auto reset = [&]() {
        sp_put(A, 0, 10, 12);
        sp_put(T, 1, 11, 12);
        sp_put(O, 1, 30, 20);
    };
    // To hit: d20 + to-hit + effects >= the stored AC
    struct Hit {
        const char* what;
        int on;                         // 0 the attacker, 1 the target
        int effect, data;
        int roll, ac;
        bool hits;
        int tohit = 0;
    };
    const Hit hits[] = {
        {"no effect: 10 against 11 misses", -1, 0, 0, 10, 11, false},
        {"no effect: 10 against 10 hits", -1, 0, 0, 10, 10, true},
        {"Bless: +1 (10 against 11 hits)", 0, 0x01, 5, 10, 11, true},
        {"Curse: -1 (10 against 10 misses)", 0, 0x02, 5, 10, 10, false},
        {"Prayer on the attacker's side: +1", 0, 0x31, 0x05, 10, 11, true},
        {"Prayer on the other side: -1", 1, 0x31, 0x15, 10, 10, false},
        {"Bestow Curse: -4 (13 against 10 misses)", 0, 0x24, 5, 13, 10, false},
        {"blinded attacker: -4 (13 against 10 misses)", 0, 0x21, 5, 13, 10, false},
        {"blinded target: its AC 4 worse (7 against 10 hits)", 1, 0x21, 5, 7, 10, true},
        {"invisible target: -4 (13 against 10 misses)", 1, 0x19, 5, 13, 10, false},
        {"Shield: AC 3 at worst (to-hit 40, 10 against AC 10 misses)", 1, 0x11, 5, 10, 50, false, 40},
        {"Shield: better armour stays (to-hit 40, 18 against AC 0 misses)", 1, 0x11, 5, 18, 60, false, 40},
        {"Faerie Fire: the stored AC + 2 - harder to hit (11 against 10 misses)", 1, 0x07, 5, 11, 10, false},
        {"Blink, after it has acted: missed (19 against 2)", 1, 0x25, 5, 19, 2, false},
    };
    for (const Hit& h : hits) {
        reset();
        if (h.on == 0) sp_give(A, h.effect, 10, h.data, false);
        if (h.on == 1) sp_give(T, h.effect, 10, h.data, false);
        if (h.effect == 0x25) F(T).delay = 0;
        const bool got = sp_blow(A, T, h.roll, h.ac, h.tohit);
        char what[120];
        snprintf(what, sizeof what, "to hit: %s", h.what);
        expect(SP, what, got == h.hits, "%s", got ? "hit" : "missed");
    }
    // Prayer reaches those within 6 squares of the one praying (coab calc_affect_effect: prayer's range 6)
    for (int far = 0; far < 2; ++far) {
        reset();
        sp_put(sp_t[3], 0, far ? 40 : 15, 12);          // the one praying (the attacker's side)
        sp_give(sp_t[3], 0x31, 10, 0x05, false);
        const bool got = sp_blow(A, T, 10, 11);
        char what[120];
        snprintf(what, sizeof what, "to hit: Prayer %s squares from the one praying: %s", far ? "30" : "5", far ? "no +1" : "+1");
        expect(SP, what, got == !far, "%s", got ? "hit" : "missed");
        *F(sp_t[3]).n_aff = 0;
        F(sp_t[3]).size = 0;
        combat::occupancy(fg->b);
    }
    // Invisible to animals: an animal's attack -4
    reset();
    F(A).rec[0x11A] = 19;
    sp_give(T, 0x45, 10, 5, false);
    expect(SP, "to hit: invisible to animals - an animal's attack -4 (13 against 10 misses)", !sp_blow(A, T, 13, 10));
    // Dispel Evil: attacks by the dispellable (record 0x14B bit 0) -7
    reset();
    F(A).rec[0x14B] = 1;
    sp_give(T, 0x04, 10, 5, false);
    expect(SP, "to hit: Dispel Evil - an evil extra-planar one's attack -7 (16 against 10 misses)", !sp_blow(A, T, 16, 10));
    // Saving throws: d20 + effects >= the save's number (12 here)
    struct Sv {
        const char* what;
        int effect, data;
        int roll;
        bool saved;
        int actor_align;
    };
    const Sv saves[] = {
        {"no effect: 12 saves", 0, 0, 12, true, 4},
        {"no effect: 11 fails", 0, 0, 11, false, 4},
        {"Shield: +1 (11 saves)", 0x11, 5, 11, true, 4},
        {"Prayer, own side: +1 (11 saves)", 0x31, 0x15, 11, true, 4},
        {"Prayer, other side: -1 (12 fails)", 0x31, 0x05, 12, false, 4},
        {"Bestow Curse: -4 (15 fails)", 0x24, 5, 15, false, 4},
        {"blinded: -4 (15 fails)", 0x21, 5, 15, false, 4},
        {"Protection from Evil against an evil one: +2 (10 saves)", 0x08, 5, 10, true, 2},
        {"Protection from Evil against a neutral one: nothing (11 fails)", 0x08, 5, 11, false, 4},
        {"Protection from Good against a good one: +2 (10 saves)", 0x09, 5, 10, true, 0},
        {"Protection from Evil 10' radius against an evil one: +2 (10 saves)", 0x2D, 5, 10, true, 2},
    };
    for (const Sv& s : saves) {
        reset();
        F(A).rec[0x11B] = static_cast<uint8_t>(s.actor_align);
        if (s.effect) sp_give(T, s.effect, 10, s.data, false);
        const bool got = sp_saves(T, 4, s.roll, A);
        char what[120];
        snprintf(what, sizeof what, "saves: %s", s.what);
        expect(SP, what, got == s.saved, "%s", got ? "saved" : "failed");
    }
    // Saves against a kind of damage (40 points, half on a save at 12)
    struct Sk {
        const char* what;
        int effect, kind, roll, want;
    };
    const Sk ksaves[] = {
        {"Resist Fire against fire: halved, and +3 on the save (9 saves): 10", 0x14, 9, 9, 10},
        {"Resist Fire against cold: nothing (11 fails): 40", 0x14, 0x0A, 11, 40},
        {"Resist Cold against cold: halved, +3 (9 saves): 10", 0x0A, 0x0A, 9, 10},
        {"hot Fire Shield against cold: +2 (10 saves): 20", 0x32, 0x0A, 10, 20},
        {"cold Fire Shield against fire: +2 (10 saves): 20", 0x36, 9, 10, 20},
        {"hot Fire Shield against fire, the save failed: doubled: 80", 0x32, 9, 2, 80},
        {"cold Fire Shield against cold, the save failed: doubled: 80", 0x36, 0x0A, 2, 80},
        {"hot Fire Shield against fire, saved: half, not doubled: 20", 0x32, 9, 19, 20},
    };
    for (const Sk& k : ksaves) {
        reset();
        sp_give(T, k.effect, 10, 5, false);
        const int got = sp_harm_saved(T, k.kind, k.roll, A);
        char what[120];
        snprintf(what, sizeof what, "saves: %s", k.what);
        expect(SP, what, got == k.want, "%d", got);
    }
    // Damage with no save: Resist Fire halves fire, not magic
    struct Dm {
        const char* what;
        int effect, kind, amount, want;
    };
    const Dm dmg[] = {
        {"Resist Fire: fire damage halved", 0x14, 9, 20, 10},
        {"Resist Cold: cold damage halved", 0x0A, 0x0A, 20, 10},
        {"Resist Fire: magic damage whole", 0x14, 8, 20, 20},
    };
    for (const Dm& m : dmg) {
        reset();
        sp_give(T, m.effect, 10, 5, false);
        combat::Harm h;
        h.kind = m.kind;
        sp_dice(1, 10, 100);
        const int got = combat::harm(fg->b, T, m.amount, h, rng, nullptr);
        char what[100];
        snprintf(what, sizeof what, "damage: %s", m.what);
        expect(SP, what, got == m.want, "%d (original %d)", got, m.want);
    }
    // Ray of Enfeeblement: the weakened one's blows do 3/4 (damage - damage / 4; coab three_quarters_damage)
    reset();
    sp_give(A, 0x1D, 10, 5, false);
    {
        combat::Fighter& at = F(A);
        at.rec[0x199] = 100;
        at.attacks[0] = 1;
        at.attacks[1] = 0;
        at.rec[0x19E] = 1;
        at.rec[0x1A0] = 12;
        at.rec[0x1A2] = 0;
        sp_dice(1, 15, 100);
        combat::attack(fg->b, A, T, nullptr, rng);
        expect(SP, "Ray of Enfeeblement: a 12-point blow does 9", 200 - F(T).hp() == 9, "%d", 200 - F(T).hp());
    }
    // Protection from Normal Missiles: a missile from a weapon with no plus is avoided
    // (needs an item: checked by the items part)
    // Haste / Slow: twice / half the moves and attacks a round
    reset();
    F(A).rec[0x1A5] = 12;
    F(A).rec[0x11C] = 2;
    sp_give(A, 0x27, 10, 5, false);
    F(T).rec[0x1A5] = 12;
    F(T).rec[0x11C] = 2;
    sp_give(T, 0x2A, 10, 5, false);
    fg->b.round = 0;
    sp_dice(1, 10, 100);
    combat::start_round(fg->b, rng);
    expect(SP, "Haste: moves 12 -> 48 half squares, 1 attack -> 2", F(A).moves == 48 && F(A).attacks[0] == 2, "moves %d attacks %d",
           F(A).moves, F(A).attacks[0]);
    expect(SP, "Slow: moves 12 -> 12 half squares, 1 attack -> 1 in 2 rounds", F(T).moves == 12, "moves %d attacks %d", F(T).moves,
           F(T).attacks[0]);
    // The turn's start: Sticks to Snakes (coab's facts, listing ovr013:00DC): snakes > its attacks: snakes
    // less its attacks; else the snakes are gone - either way "is fighting with snakes" and the turn lost
    struct Sn {
        int snakes, att;
        int left;                       // -1: gone
    };
    for (const Sn& s : {Sn{5, 1, 4}, Sn{3, 2, 1}, Sn{2, 2, -1}, Sn{1, 2, -1}}) {
        reset();
        sp_give(T, 0x03, 10, s.snakes, false);
        F(T).attacks[0] = s.att;
        F(T).attacks[1] = 0;
        F(T).moves = 12;
        const combat::TurnFx tf = combat::turn_effects(fg->b, T);
        const int left = sp_aff_data(T, 0x03);
        char what[120];
        snprintf(what, sizeof what, "Sticks to Snakes: %d snakes, %d attacks - %s, the turn lost", s.snakes, s.att,
                 s.left < 0 ? "gone" : "fewer");
        expect(SP, what, tf == combat::TurnFx::Snakes && left == s.left && F(T).attacks[0] == 0 && F(T).moves == 0,
               "turn fx %d snakes left %d attacks %d", (int)tf, left, F(T).attacks[0]);
    }
    // ... and cast: the handler runs at once (the snakes less its attacks left; the turn's actions gone)
    for (const Sn& s : {Sn{9, 1, 8}, Sn{2, 2, -1}}) {
        reset();
        sp_caster(A, classes::Cleric, s.snakes);
        F(T).attacks[0] = s.att;
        F(T).attacks[1] = 0;
        F(T).moves = 12;
        sp_dice(1, 10, 100);
        sp_cast(A, 0x46, {T});
        const int left = sp_aff_data(T, 0x03);
        char what[120];
        snprintf(what, sizeof what, "Sticks to Snakes cast at level %d on one with %d attacks left: %s, its actions gone", s.snakes,
                 s.att, s.left < 0 ? "no snakes left" : "snakes left");
        expect(SP, what, left == s.left && F(T).attacks[0] == 0 && F(T).moves == 0 && sp_said(T, combat::Did::Word) == 1,
               "snakes %d attacks %d moves %d", left, F(T).attacks[0], F(T).moves);
    }
    // Silence: the silenced one and those next to it can't cast or use items at their turn
    reset();
    sp_give(T, 0x15, 10, 5, false);
    sp_put(sp_t[3], 1, 12, 12);
    F(T).can_cast = F(T).can_use = true;
    F(sp_t[3]).can_cast = F(sp_t[3]).can_use = true;
    const combat::TurnFx s1 = combat::turn_effects(fg->b, T), s2 = combat::turn_effects(fg->b, sp_t[3]);
    expect(SP, "Silence: no spells or items for the silenced one and one next to it",
           s1 == combat::TurnFx::Silenced && s2 == combat::TurnFx::Silenced && !F(T).can_cast && !F(sp_t[3]).can_cast);
    // Fumbling: the turn lost; entangled: no moving
    reset();
    sp_give(T, 0x1B, 10, 5, false);
    expect(SP, "Fumble: the turn lost", combat::turn_effects(fg->b, T) == combat::TurnFx::Fumbling);
    reset();
    sp_give(T, 0x88, 10, 0, false);
    F(T).rec[0x1A5] = 12;
    sp_dice(1, 10, 100);
    combat::start_round(fg->b, rng);
    expect(SP, "Entangle: no moving", F(T).moves == 0 && F(T).up(), "moves %d", F(T).moves);
    // Held / asleep: no turn, any blow slays
    reset();
    sp_give(T, 0x35, 10, 5, false);
    sp_dice(1, 10, 100);
    combat::start_round(fg->b, rng);
    expect(SP, "Sleep: the sleeper doesn't act (no initiative)", F(T).delay == 0);
    F(A).attacks[0] = 1;
    const combat::Attack sl = combat::attack(fg->b, A, T, nullptr, rng);
    expect(SP, "Sleep: a blow slays the sleeper", sl.slain && !F(T).up());
    // A spell being cast is lost when its caster is hurt
    reset();
    F(A).spell = 0x0F;
    F(A).can_cast = true;
    combat::damage(fg->b, A, 1);
    expect(SP, "casting: hurt before the spell goes off - it can't be cast (lost)", !F(A).can_cast);
    // The fight's clock: an effect of N rounds is gone after N rounds
    reset();
    sp_give(T, 0x01, 3, 5, false);
    combat::tick(fg->b);
    combat::tick(fg->b);
    const bool on2 = sp_aff(T, 0x01) >= 0;
    combat::tick(fg->b);
    expect(SP, "fight clock: a 3-round effect lasts 3 rounds", on2 && sp_aff(T, 0x01) < 0);
}
// ---- 6b. Spells with workings of their own (fights; coab ovr023 / ovr013 / ovr024)
static void sp_check_special()
{
    using namespace play;
    char what[140];
    // Poison (cleric 4) and Cloudkill kill by poison: the dead carry the poisoned effect (0x37, data 0xFF;
    // listing ovr013:1407 / ovr024 in_poison_cloud) - so Slow Poison / Neutralize Poison bring them back
    if (sp_new_fight() && sp_pick()) {
        sp_caster(sp_c, classes::Cleric, 9);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 11, 12);
        sp_dice(1, 2, 100);
        sp_cast(sp_c, 0x44, {sp_t[0]});
        expect(SP, "Poison: the slain is poisoned (effect 0x37, data 0xFF)",
               F(sp_t[0]).status() == party::Dead && sp_aff_data(sp_t[0], 0x37) == 0xFF, "status %d poisoned data %d",
               F(sp_t[0]).status(), sp_aff_data(sp_t[0], 0x37));
    }
    for (int hd = 4; hd <= 7; ++hd) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_put(sp_t[0], 1, 11, 12);
        F(sp_t[0]).rec[0xE5] = static_cast<uint8_t>(hd);
        F(sp_t[0]).rec[0xDF] = 14;                       // save vs poison: 14
        sp_dice(1, 16, 100);                             // 16 - 4 = 12 fails at -4, 16 makes it at 0
        const bool died = combat::breathe_poison(fg->b, sp_t[0], rng);
        const bool want = hd <= 4 || hd == 5;
        snprintf(what, sizeof what, "Cloudkill on %d Hit Dice (d20 16, save 14): %s", hd,
                 hd <= 4 ? "dies" : hd == 5 ? "dies (save at -4)" : hd == 6 ? "saved" : "unaffected");
        expect(SP, what, died == want && (!died || sp_aff_data(sp_t[0], 0x37) == 0xFF), "died %d poisoned data %d", (int)died,
               sp_aff_data(sp_t[0], 0x37));
    }
    // ... and Slow Poison in a fight brings the poisoned dead back up (1 HP), poisoned still
    if (sp_new_fight() && sp_pick()) {
        sp_caster(sp_c, classes::Cleric, 9);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 0, 12, 12);
        sp_give(sp_t[0], 0x37, 0, 0xFF, false);
        combat::damage(fg->b, sp_t[0], 250);
        sp_dice(1, 10, 100);
        sp_cast(sp_c, 0x1A, {sp_t[0]});
        expect(SP, "Slow Poison in a fight: the poisoned dead gets back up with 1 HP",
               F(sp_t[0]).up() && F(sp_t[0]).hp() == 1 && sp_aff_time(sp_t[0], 0x16) == 540, "up %d HP %d time %d",
               (int)F(sp_t[0]).up(), F(sp_t[0]).hp(), sp_aff_time(sp_t[0], 0x16));
    }
    // Stinking Cloud: a save vs poison or helpless d4 + 1 rounds; saved: coughing
    for (int pass = 0; pass < 2; ++pass) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_put(sp_t[0], 1, 11, 12);
        sp_dice(pass ? 1 : 2, pass ? 20 : 2, 100);
        const combat::Did r = combat::breathe_cloud(fg->b, sp_t[0], rng);
        if (!pass)
            expect(SP, "Stinking Cloud, the save failed: helpless d4 + 1 (2 low)",
                   r == combat::Did::Word && sp_aff_time(sp_t[0], 0x1F) == 2, "time %d", sp_aff_time(sp_t[0], 0x1F));
        else
            expect(SP, "Stinking Cloud, saved: coughing", r == combat::Did::Word2 && sp_aff(sp_t[0], 0x1E) >= 0);
    }
    // Fire Shield: one hitting the shielded from next to it takes twice the blow (magic)
    if (sp_new_fight() && sp_pick()) {
        sp_put(sp_t[0], 0, 10, 12);
        sp_put(sp_t[1], 1, 11, 12);
        sp_give(sp_t[0], 0x8F, 10, 0, false);
        combat::Fighter& at = F(sp_t[1]);
        at.rec[0x199] = 100;
        at.attacks[0] = 1;
        at.attacks[1] = 0;
        at.rec[0x19E] = 1;
        at.rec[0x1A0] = 6;
        at.rec[0x1A2] = 0;
        sp_dice(1, 15, 100);
        combat::attack(fg->b, sp_t[1], sp_t[0], nullptr, rng);
        expect(SP, "Fire Shield: a 6-point blow - the shielded takes 6, the attacker 12",
               200 - F(sp_t[0]).hp() == 6 && 200 - F(sp_t[1]).hp() == 12, "%d / %d", 200 - F(sp_t[0]).hp(), 200 - F(sp_t[1]).hp());
    }
    // Feeblemind: this save's number moved by class (cleric -1, magic-user +4, others +2; save 12 here)
    struct Fb {
        int cls, roll;
        bool saved;
    };
    for (const Fb& f : {Fb{0, 11, true}, Fb{0, 10, false}, Fb{5, 15, false}, Fb{5, 16, true}, Fb{2, 13, false}, Fb{2, 14, true}}) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, classes::MagicUser, 9);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 11, 12);
        F(sp_t[0]).rec[0x75] = static_cast<uint8_t>(f.cls);
        F(sp_t[0]).rec[0xDF + 4] = 12;
        sp_dice(1, f.roll, 100);
        sp_cast(sp_c, 0x5D, {sp_t[0]});
        const bool feeble = sp_aff(sp_t[0], 0x44) >= 0;
        snprintf(what, sizeof what, "Feeblemind on class %d, d20 %d against 12: %s", f.cls, f.roll, f.saved ? "saved" : "feebleminded");
        expect(SP, what, feeble == !f.saved && (!feeble || (F(sp_t[0]).rec[0x13] == 7 && F(sp_t[0]).rec[0x15] == 7)), "effect %d Int %d Wis %d",
               (int)feeble, F(sp_t[0]).rec[0x13], F(sp_t[0]).rec[0x15]);
    }
    // Mirror Image: a spell at picked creatures takes an image (d(images + 1) over 1); an area spell doesn't
    for (int pass = 0; pass < 3; ++pass) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, classes::MagicUser, 5);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 12, 12);
        sp_give(sp_t[0], 0x1C, 10, (2 << 4) + 5, false);
        sp_dice(pass == 1 ? 2 : 1, 2, 100);
        if (pass < 2) sp_cast(sp_c, 0x0F, {sp_t[0]});
        else {
            fg->b.aim_x = 12, fg->b.aim_y = 12;
            sp_cast(sp_c, 0x2F, {sp_t[0]});
        }
        const int hurt = 200 - F(sp_t[0]).hp(), data = sp_aff_data(sp_t[0], 0x1C);
        if (pass == 0)
            expect(SP, "Mirror Image: Magic Missile with d3 over 1 - an image lost, no damage",
                   hurt == 0 && data == (2 << 4) + 4 && sp_said(sp_t[0], combat::Did::LostImage) == 1, "damage %d data %02X", hurt, data);
        else if (pass == 1)
            expect(SP, "Mirror Image: Magic Missile with d3 = 1 - the caster is hit", hurt > 0 && data == (2 << 4) + 5, "damage %d data %02X",
                   hurt, data);
        else
            expect(SP, "Mirror Image: a Fireball (an area) never meets the images", hurt > 0 && data == (2 << 4) + 5, "damage %d data %02X",
                   hurt, data);
    }
    // Dispel Magic in a fight: the first target's effects by level, 0xFF ones kept
    for (int roll : {70, 71}) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, classes::Cleric, 9);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 12, 12);
        sp_give(sp_t[0], 0x01, 10, 5, false);
        sp_give(sp_t[0], 0x3B, 0, 0xFF, false);
        sp_dice(1, 10, roll);
        sp_cast(sp_c, 0x29, {sp_t[0]});
        snprintf(what, sizeof what, "Dispel Magic (level 9) in a fight on a level 5 effect, d100 %d (70%%)", roll);
        expect(SP, what, (sp_aff(sp_t[0], 0x01) < 0) == (roll <= 70) && sp_aff(sp_t[0], 0x3B) >= 0);
    }
    // Animate Dead: up to `level` dead characters (monster type 0) rise on the caster's side; not monsters
    if (sp_new_fight() && sp_pick()) {
        sp_caster(sp_c, classes::Cleric, 9);                 // (a monster spell: level 12)
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 14, 12);
        F(sp_t[0]).rec[0x11A] = 0;
        sp_put(sp_t[1], 1, 16, 12);
        F(sp_t[1]).rec[0x11A] = 5;
        combat::damage(fg->b, sp_t[0], 250);
        combat::damage(fg->b, sp_t[1], 250);
        sp_dice(1, 10, 100);
        sp_cast(sp_c, 0x24, {sp_c});
        expect(SP, "Animate Dead: a dead character rises on the caster's side, animated",
               F(sp_t[0]).up() && F(sp_t[0]).team() == 0 && F(sp_t[0]).status() == party::Animated && F(sp_t[0]).hp() == 200,
               "up %d team %d status %d HP %d", (int)F(sp_t[0]).up(), F(sp_t[0]).team(), F(sp_t[0]).status(), F(sp_t[0]).hp());
        expect(SP, "Animate Dead: not a dead monster", !F(sp_t[1]).up());
    }
    // Entangle: outdoors only; a save or no moving (24 rounds - spell_facts.md's choice for the original's
    // past-the-table time)
    for (int outdoors = 0; outdoors < 2; ++outdoors) {
        if (!sp_new_fight() || !sp_pick()) return;
        fg->b.indoors = !outdoors;
        sp_caster(sp_c, classes::Ranger, 10);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 12, 12);
        sp_dice(1, 2, 100);
        sp_cast(sp_c, 0x4E, {sp_t[0]});
        snprintf(what, sizeof what, "Entangle %s", outdoors ? "outdoors: entangled" : "indoors: nothing");
        expect(SP, what, outdoors ? sp_aff_time(sp_t[0], 0x88) == 24 : sp_aff(sp_t[0], 0x88) < 0, "time %d", sp_aff_time(sp_t[0], 0x88));
    }
    // Picking by budget (Faerie Fire by size, Charm Monsters by Hit Dice)
    if (sp_new_fight() && sp_pick()) {
        sp_put(sp_t[0], 1, 12, 12);
        int bad = 0;
        const int by_size[5] = {0, 1, 2, 2, 4}, by_hd[6] = {1, 1, 2, 4, 8, 8};
        for (int s = 1; s <= 4; ++s) {
            F(sp_t[0]).rec[0xDE] = static_cast<uint8_t>(s);
            bad += combat::pick_cost(F(sp_t[0]), true) != by_size[s];
        }
        for (int h = 0; h <= 5; ++h) {
            F(sp_t[0]).rec[0xE5] = static_cast<uint8_t>(h);
            bad += combat::pick_cost(F(sp_t[0]), false) != by_hd[h];
        }
        expect(SP, "picking costs: by size 1 / 2 / 2 / 4, by Hit Dice 1 1 2 4 8", !bad, "%d wrong", bad);
    }
    // Snake Charm: snakes (monster type 14) on either side as far as the caster's HP go
    if (sp_new_fight() && sp_pick()) {
        sp_caster(sp_c, classes::Cleric, 5);
        sp_put(sp_c, 0, 10, 12, 30);
        for (int k = 0; k < 3; ++k) {
            sp_put(sp_t[k], k == 2 ? 0 : 1, 12 + 2 * k, 12, 12);
            F(sp_t[k]).rec[0x11A] = 14;
        }
        sp_dice(1, 10, 100);
        sp_cast(sp_c, 0x1B, {sp_t[0]});
        expect(SP, "Snake Charm (30 HP): two snakes of 12 HP charmed, not the third",
               sp_aff(sp_t[0], 0x33) >= 0 && sp_aff(sp_t[1], 0x33) >= 0 && sp_aff(sp_t[2], 0x33) < 0);
    }
    // Dispel Evil: its holder's first hit on an evil extra-planar one that fails a save: dispelled
    if (sp_new_fight() && sp_pick()) {
        sp_caster(sp_t[0], classes::Cleric, 9);
        sp_put(sp_t[0], 0, 10, 12);
        sp_put(sp_t[1], 1, 11, 12);
        F(sp_t[1]).rec[0x14B] = 1;
        sp_dice(1, 10, 100);
        sp_cast(sp_t[0], 0x49, {sp_t[0]});
        F(sp_t[1]).rec[0xDF + 4] = 15;
        sp_dice(1, 14, 100);
        combat::Fighter& at = F(sp_t[0]);
        at.rec[0x199] = 100;
        at.attacks[0] = 1;
        at.attacks[1] = 0;
        combat::attack(fg->b, sp_t[0], sp_t[1], nullptr, rng);
        expect(SP, "Dispel Evil: a hit on an evil extra-planar one (save failed) dispels it", !F(sp_t[1]).up() &&
               sp_aff(sp_t[0], 0x91) < 0, "status %d", F(sp_t[1]).status());
    }
    // Remove Curse in a fight: the first cursed item comes off
    if (sp_new_fight() && sp_pick()) {
        int m = -1;
        for (int k = 0; k < 6 && m < 0; ++k)
            if (F(sp_t[k]).member >= 0) m = sp_t[k];
        if (m >= 0) {
            party::Character& ch = pt->m[F(m).member];
            uint8_t ring[63] = {};
            ring[0x2E] = 1;
            if (ch.n_items == 0) rules::add_item(ch, ring);
            F(m).items = ch.items;
            F(m).n_items = ch.n_items;
            for (int j = 0; j < ch.n_items; ++j) ch.items[j][0x36] = 0;
            sp_caster(sp_c, classes::Cleric, 9);
            sp_put(sp_c, 0, 10, 12);
            sp_put(m, 0, 11, 12);
            F(m).items[0][0x36] = 1;
            F(m).items[0][0x34] = 1;
            F(m).items[0][0x3E] = 0;
            sp_dice(1, 10, 100);
            sp_cast(sp_c, 0x2B, {m});
            expect(SP, "Remove Curse in a fight: the cursed item comes off (still cursed)",
                   F(m).items[0][0x34] == 0 && F(m).items[0][0x36] == 1 && sp_said(m, combat::Did::Word3) == 1);
        }
    }
    // Restoration: a lost level back with its share of the lost HP (camp)
    if (boot() && load_magic()) {
        party::Character& c = pt->m[1];
        const int lv = c.level(classes::Fighter) ? classes::Fighter : -1;
        int cls = lv;
        for (int k = 0; cls < 0 && k < 8; ++k)
            if (c.level(k)) cls = k;
        const int before = c.level(cls), hp = c.hp_max();
        c.rec[0xE7] = 2;
        c.rec[0xE8] = 12;
        const spells::CampSpell* cs = sp_camp(0x38);
        pt->m[4].rec[0x109] = 13;
        sp_cn = cs ? spells::cast(*pt, 4, 1, *cs, mrules->tables, d->prof->cures, d->prof->magic.facts, rng, sp_cl, 24) : 0;
        expect(SP, "Restoration: 2 levels lost (12 HP): a level back, 6 HP back, 1 still lost",
               c.level(cls) == before + 1 && c.hp_max() == hp + 6 && c.rec[0xE7] == 1 && c.rec[0xE8] == 6, "level %d -> %d HP %d -> %d",
               before, c.level(cls), hp, c.hp_max());
    }
}

// ---- 6b2. Cause Disease's course and Haste's aging (coab's facts; listing ovr013:1038 weaken, 10C2
// the sickness, 0BA5 spl_age)
static void sp_check_course()
{
    using namespace play;
    // In a fight: the disease (0x22) running out - weakness 0x2B (60) and sickness 0x2C (10) begin,
    // Strength in use - 1 ("is weakened"), 1 HP ("takes 1 point of damage"); then every 60 / 10 rounds
    if (sp_new_fight() && sp_pick()) {
        const int T = sp_t[0];
        sp_put(T, 1, 12, 12, 50);
        F(T).rec[0x11] = 12;
        sp_give(T, 0x22, 1, 5, true);
        combat::Event ev[8];
        int n = combat::tick(fg->b, ev, 8);
        bool weak_ev = false, hurt_ev = false;
        for (int k = 0; k < n; ++k) {
            weak_ev = weak_ev || (ev[k].who == T && ev[k].ev == combat::Ev::Weakened);
            hurt_ev = hurt_ev || (ev[k].who == T && ev[k].ev == combat::Ev::Damage && ev[k].amount == 1);
        }
        expect(SP, "Cause Disease run out (a fight): weakness 60 and sickness 10 begin, Str 12 -> 11, HP 50 -> 49, said",
               sp_aff(T, 0x22) < 0 && sp_aff_time(T, 0x2B) == 60 && sp_aff_time(T, 0x2C) == 10 && F(T).rec[0x11] == 11 &&
                   F(T).hp() == 49 && weak_ev && hurt_ev,
               "weak %d sick %d Str %d HP %d events %d/%d", sp_aff_time(T, 0x2B), sp_aff_time(T, 0x2C), F(T).rec[0x11], F(T).hp(),
               (int)weak_ev, (int)hurt_ev);
        for (int r = 0; r < 60; ++r) combat::tick(fg->b, ev, 8);
        expect(SP, "the disease's course, 60 rounds on: 6 more HP, Str 1 less",
               F(T).hp() == 43 && F(T).rec[0x11] == 10 && sp_aff(T, 0x2B) >= 0 && sp_aff(T, 0x2C) >= 0, "HP %d Str %d", F(T).hp(),
               F(T).rec[0x11]);
        F(T).rec[0x1A4] = 1;
        for (int r = 0; r < 10; ++r) combat::tick(fg->b, ev, 8);
        expect(SP, "the disease's course at 1 HP: no damage, helpless", F(T).hp() == 1 && sp_aff(T, 0x1F) >= 0, "HP %d", F(T).hp());
    }
    // In camp: the same on the camp's clock; Cure Disease ends it
    if (boot() && load_magic()) {
        party::Character& c = pt->m[1];
        c.n_affects = 0;
        c.rec[0x1A4] = c.rec[0x78] = 60;
        c.rec[0x11] = 14;
        spells::add_affect(c, 0x22, 30, 5, true);
        auto pass = [&](int minutes) {
            rules::DiseaseStep s{};
            for (int k = 0; k < minutes / 5; ++k) {
                const rules::DiseaseStep t = rules::disease_clock(c, 5, d->prof->cures);
                s.weakened += t.weakened;
                s.hurt += t.hurt;
                magic::tick_affects(c, 5);
            }
            return s;
        };
        const rules::DiseaseStep a = pass(30);
        expect(SP, "camp: Cause Disease (30 minutes) run out - Str 14 -> 13, 1 HP, weakness and sickness on",
               a.weakened == 1 && a.hurt == 1 && c.rec[0x11] == 13 && c.hp() == 59 && sp_caff(1, 0x2B) >= 0 && sp_caff(1, 0x2C) >= 0,
               "weakened %d hurt %d Str %d HP %d", a.weakened, a.hurt, c.rec[0x11], c.hp());
        const rules::DiseaseStep b2 = pass(120);
        expect(SP, "camp: two hours on - Str 2 less, 12 HP less", b2.weakened == 2 && b2.hurt == 12 && c.rec[0x11] == 11 && c.hp() == 47,
               "weakened %d hurt %d Str %d HP %d", b2.weakened, b2.hurt, c.rec[0x11], c.hp());
        sp_member(4, classes::Cleric, 9);
        sp_ccast(4, 1, 0x27);
        const rules::DiseaseStep c3 = pass(120);
        expect(SP, "camp: Cure Disease ends the course", c3.weakened == 0 && c3.hurt == 0 && sp_caff(1, 0x2B) < 0 && sp_caff(1, 0x2C) < 0,
               "weakened %d hurt %d", c3.weakened, c3.hurt);
    }
    // Haste: a year older the first round ("ages"), not again
    if (sp_new_fight() && sp_pick()) {
        const int T = sp_t[0];
        sp_put(T, 0, 12, 12);
        sp_give(T, 0x27, 10, 5, false);
        const int age0 = F(T).rec[0x76] | F(T).rec[0x77] << 8;
        combat::Event ev[8];
        sp_dice(1, 10, 100);
        int n = combat::start_round(fg->b, rng, ev, 8);
        bool aged_ev = false;
        for (int k = 0; k < n; ++k) aged_ev = aged_ev || (ev[k].who == T && ev[k].ev == combat::Ev::Ages);
        const int age1 = F(T).rec[0x76] | F(T).rec[0x77] << 8;
        n = combat::start_round(fg->b, rng, ev, 8);
        const int age2 = F(T).rec[0x76] | F(T).rec[0x77] << 8;
        expect(SP, "Haste: a year older the first round (\"ages\"), not the next", age1 == age0 + 1 && aged_ev && age2 == age1 && n == 0,
               "age %d -> %d -> %d", age0, age1, age2);
    }
}

// ---- 6c. The Robe of Vermin's effect (0x4A) on a caster: a d2 of 1 miscasts - nothing cast, the
// memorized spell kept (listing ovr023:1403); a 2 casts as ever
static void sp_check_miscast()
{
    using namespace play;
    const uint8_t vermin = d->prof->fight.facts.items.vermin;
    if (!expect(SP, "the Robe of Vermin's effect in the profile", vermin == 0x4A, "%02X", vermin)) return;
    for (int face : {1, 2}) {
        // In a fight: Shield (0x13, no casting time) by a magic-user with the effect
        if (!sp_new_fight() || !sp_pick()) return;
        combat::Fighter& f = F(sp_c);
        sp_caster(sp_c, classes::MagicUser, 5);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 30, 12);
        sp_give(sp_c, vermin, 0, 0xFF, false);
        memset(f.rec + magic::kListAt, 0, magic::kListSize);
        f.rec[magic::kListAt] = 0x13;
        fg->cur = sp_c;
        fg->pages = 0;
        sp_dice(0);
        dice_faces({face});
        fight_spell_chosen(0x13, C);
        if (fg->st == FSt::Aim) spell_target(C, sp_c);
        const bool kept = f.rec[magic::kListAt] == 0x13, shielded = sp_aff(sp_c, 0x11) >= 0;
        char what[120];
        snprintf(what, sizeof what, "Robe of Vermin, a fight, d2 %d: %s", face, face == 1 ? "miscast - no Shield, the spell kept" : "Shield cast");
        expect(SP, what, face == 1 ? (kept && !shielded) : (!kept && shielded), "kept %d shielded %d", (int)kept, (int)shielded);
    }
    for (int face : {1, 2}) {
        // In camp: Shield again
        if (!boot() || !load_magic()) return;
        sp_member(5, classes::MagicUser, 5);
        spells::add_affect(sp_member(5), vermin, 0, 0xFF, false);
        memset(sp_member(5).rec + magic::kListAt, 0, magic::kListSize);
        sp_member(5).rec[magic::kListAt] = 0x13;
        cr = CastRun{};
        cr.caster = 5;
        pt->selected = 5;
        view_from = Screen::Game;
        screen = Screen::SpellList;
        sp_dice(0);
        dice_faces({face});
        choose_spell(0x13, C);
        for (int n = 0; n < 200 && screen == Screen::Cast; ++n) {
            g_now += 5000;
            play::tick(g_now, C);
        }
        const bool kept = sp_member(5).rec[magic::kListAt] == 0x13, shielded = sp_caff(5, 0x11) >= 0;
        char what[120];
        snprintf(what, sizeof what, "Robe of Vermin, camp, d2 %d: %s", face, face == 1 ? "\"miscasts\" - no Shield, the spell kept" : "Shield cast");
        expect(SP, what, face == 1 ? (kept && !shielded) : (!kept && shielded), "kept %d shielded %d", (int)kept, (int)shielded);
        screen = Screen::Game;
    }
}

// ---- 7. Spells the engine doesn't do yet (fights): every spell that can be
// cast in a fight (when: combat / both) must be in the fight table
static void sp_check_notyet()
{
    using namespace play;
    int missing = 0;
    char list[300] = "";
    for (int s = 1; s <= 0x64; ++s) {
        if (orow(s, OWhen) == 0) continue;
        const combat::FightSpell* fs = sp_fs(s);
        if (fs && fs->does != combat::SpellDoes::NotYet) continue;
        ++missing;
        const size_t l = strlen(list);
        snprintf(list + l, sizeof list - l, " %02X", s);
    }
    expect(SP, "every spell castable in fights is in the engine", !missing, "%d not:%s", missing, list);
    // Camp-only spells in a fight: "Camp Only Spell" (the table's when = camp)
    int camp_only = 0;
    for (int s = 1; s <= 0x64; ++s)
        if (orow(s, OWhen) == 0 && sp_fs(s)) ++camp_only;
    expect(SP, "no camp-only spell is cast in fights", camp_only == 0, "%d in the fight table", camp_only);
}

static void behave_spells()
{
    using namespace play;
    if (!sp_load_oracle()) {
        expect(SP, "the oracle (ORACLE_DIR/oracle_spells.txt)", false, "missing or incomplete");
        return;
    }
    sp_oracle_ok = true;
    if (!boot()) {
        expect(SP, "boot", false);
        return;
    }
    if (!load_magic()) {
        expect(SP, "the rule tables", false);
        return;
    }
    const char* only = getenv("SPONLY");
    auto want = [&](const char* p) { return !only || strstr(only, p); };
    if (want("table")) sp_check_table();
    if (want("level")) sp_check_levels();
    if (want("memory")) sp_check_memory();
    if (want("fight")) sp_check_fights();
    if (want("camp")) sp_check_camp();
    if (want("effects")) sp_check_effects();
    if (want("special")) sp_check_special();
    if (want("miscast")) sp_check_miscast();
    if (want("course")) sp_check_course();
    if (want("notyet")) sp_check_notyet();
    sp_dice(0);
    create::g_die_hook = nullptr;
}

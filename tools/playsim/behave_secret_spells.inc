// Behaviour checks: Secret of the Silver Blades - spells (included by
// behave_secret.inc inside namespace bsecret; ONLY=allspells). Expected values
// from Secret's own program and tables (the oracle, scratch: oracle_secret_spells.txt
// from secret_spells_oracle.py) and the Project's claude/secret_facts.md /
// secret_facts_2.md - never from the engine's own code. The formulas the
// original's routines use (GAME.OVR offsets beside each) are written here;
// its numbers come from the oracle.

namespace sspells {

const char* S = "secret spells";
char w[160];

// ---- The oracle: the spell table's row as the original reads it (DS 0x449D
// + 16 x spell: class, level, range, range a level, time, time a level, aim,
// targets, on a save, save, effect, when, delay, priority, AI target, AI area)
enum { OCls = 1, OLvl, ORange, ORangeL, ODur, ODurL, OAim, OTargets, OOnSave, OSave, OAffect, OWhen, ODelay, OPrio };
int orow(int s, int k)
{
    const auto* r = b_row("sp_row", s);
    return r && k < static_cast<int>(r->size()) ? (*r)[k] : -99;
}
// The original's caster level for a spell (GAME.OVR 0x3AFD7): no cleric or
// magic-user levels and a ranger under 8: 6; else by the row's class - 0
// clerics: the cleric's level or the paladin's (Secret: no "- 8"), 1 druids:
// the ranger's - 7 (at least 0), 3 magic-users: the magic-user's or the
// ranger's - 8, 4 items / monsters: 12; from an item (not class 4): 6.
// (rules::fixes(): the paladin as AD&D - from 9th level, level - 8)
int level_rule(const party::Character& c, int s, bool item)
{
    const int cls = orow(s, OCls);
    int lvl = 0;
    const bool fix = rules::fixes();
    if (c.level(classes::Cleric) == 0 && c.level(classes::MagicUser) == 0 && c.level(classes::Ranger) < 8 &&
        (!fix || c.level(classes::Paladin) < 9))
        lvl = 6;
    else {
        auto mx = [](int a, int b) { return a > b ? a : b; };
        const int pal = c.level(classes::Paladin) - (fix ? 8 : 0);
        switch (cls) {
        case 0: lvl = mx(c.level(classes::Cleric), pal); break;
        case 1: lvl = mx(c.level(classes::Ranger) - 7, 0); break;
        case 3: lvl = mx(c.level(classes::MagicUser), c.level(classes::Ranger) - 8); break;
        case 4: lvl = 12; break;
        default: lvl = 0; break;
        }
    }
    if (item && cls != 4) lvl = 6;
    return lvl;
}
// The original's time for spell s at caster level L (GAME.OVR 0x2D5E2):
// its special spells' dice (all highest: the dice's sides), else the row's
int dur_rule(int s, int L, bool high)
{
    for (const auto& r : b_rows("sp_dur_fixed"))
        if (r[0] == s) return r[1];
    for (const auto& r : b_rows("sp_dur_dice"))
        if (r[0] == s) return (high ? r[1] * r[2] : r[1]) * r[4] + r[3];     // (n dice x mult + plus)
    return orow(s, ODur) + orow(s, ODurL) * L;
}

// ---- Dice: d20 / d100 set, the others highest (1), lowest (2)
int d20f = 0, d100f = 0, dmode = 1;
int die(int n)
{
    if (n == 20 && d20f) return d20f - 1;
    if (n == 100 && d100f) return d100f - 1;
    return dmode == 1 ? n - 1 : 0;
}
void dice(int mode, int d20 = 0, int d100 = 0)
{
    dmode = mode;
    d20f = d20;
    d100f = d100;
    create::g_die_hook = die;
}
void dice_off() { create::g_die_hook = nullptr; }

// ---- Party members for camp casts
party::Character& mem(int i) { return play::pt->m[i]; }
void member(int i, int cls, int level, int hp = 60)
{
    party::Character& c = mem(i);
    for (int k = 0; k < 8; ++k) c.rec[0x109 + k] = c.rec[0x111 + k] = 0;
    if (cls >= 0) c.rec[0x109 + cls] = static_cast<uint8_t>(level);
    c.rec[0x74] = 7;
    c.ext[party::kExtRace] = 6;
    c.rec[0xE6] = 0;
    c.rec[0x1A4] = c.rec[0x78] = static_cast<uint8_t>(hp);
    c.rec[0x195] = party::Okay;
    c.rec[0x196] = 1;
    c.rec[0x197] = 0;
    c.n_affects = 0;
    for (int k = 0; k < 5; ++k) c.rec[0xDF + k] = 15;
    c.rec[0x186] = 0;
    for (int k = 0; k < 6; ++k) c.rec[0x10 + 2 * k] = c.rec[0x11 + 2 * k] = 12;
    c.rec[0x1C] = c.rec[0x1D] = 0;
}
spells::Line cl[24];
int cn = 0;
const spells::CampSpell* camp(int s) { return play::camp_spell(s); }
int ccast(int caster, int target, int s, int flame = 0)
{
    using namespace play;
    cn = 0;
    const spells::CampSpell* cs = camp(s);
    if (!cs) return -1;
    cn = spells::cast(*pt, caster, target, *cs, mrules->tables, d->prof->cures, d->prof->magic.facts, rng, cl, 24, 0, flame);
    return cn;
}
int csaid(int who, spells::Said s)
{
    int n = 0;
    for (int k = 0; k < cn; ++k) n += cl[k].who == who && cl[k].what == s;
    return n;
}
int caff(int i, int type, int* minutes = nullptr, int* data = nullptr)
{
    const party::Character& c = mem(i);
    for (int k = 0; k < c.n_affects; ++k)
        if (c.affects[k][0] == type) {
            if (minutes) *minutes = c.affects[k][1] | c.affects[k][2] << 8;
            if (data) *data = c.affects[k][3];
            return k;
        }
    return -1;
}

// ---- Fighters (bsecret's put / cast / aff_time; F(i))
void caster(int i, int cls, int level)
{
    combat::Fighter& f = F(i);
    for (int k = 0; k < 8; ++k) f.rec[0x109 + k] = f.rec[0x111 + k] = 0;
    f.rec[0x109 + cls] = static_cast<uint8_t>(level);
    f.rec[0x74] = 7;
    f.rec[0xE6] = 0;
}
int faff(int i, int type, int* data = nullptr)
{
    const combat::Fighter& f = F(i);
    for (int k = 0; f.n_aff && k < *f.n_aff; ++k)
        if (f.aff[k][0] == type) {
            if (data) *data = f.aff[k][3];
            return f.aff[k][1] | f.aff[k][2] << 8;
        }
    return -1;
}
int lines_of(int who, combat::Did d)
{
    int n = 0;
    for (int k = 0; k < nl; ++k) n += L[k].who == who && L[k].did == d;
    return n;
}
const combat::FightSpell* fspell(int s)
{
    const auto& fl = play::d->prof->fight;
    return combat::fight_spell(fl.spells, fl.n_spells, s);
}

// =====================================================================
// 1. The spell table: every row as the original reads it, and the engine's
// reading of it (spells::entry); its classes (Secret's codes: 0 cleric, 1
// druid, 3 magic-user, 4 items / monsters - the engine's 0 / 1 / 2 / 3)
void table_rows()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    for (int s = 1; s <= 117; ++s) {
        if (!b_row("sp_row", s)) {
            expect(S, "oracle row", false, "spell %d", s);
            continue;
        }
        const spells::Entry e = spells::entry(t, s);
        static const int kCls[5] = {0, 1, -1, 2, 3};
        const int oc = orow(s, OCls);
        const int want_cls = oc >= 0 && oc <= 4 ? kCls[oc] : -1;
        // Secret's targets: 0 combat, 1 self, 2 a member, 3 the party (its camp
        // routine GAME.OVR 0x2D8DE - Curse's party is 4)
        const int ot = orow(s, OTargets);
        const int want_t = ot == 3 ? spells::kParty : ot;
        char diff[200] = "";
        auto chk = [&](const char* n, int got, int want) {
            if (got == want) return;
            const size_t l = strlen(diff);
            snprintf(diff + l, sizeof diff - l, " %s %d (original %d)", n, got, want);
        };
        chk("class", t.spell_class(s), want_cls);
        chk("level", e.level, orow(s, OLvl));
        chk("range", e.range, orow(s, ORange));
        chk("range/lvl", e.range_level, orow(s, ORangeL));
        chk("time", e.lasts, orow(s, ODur));
        chk("time/lvl", e.lasts_level, orow(s, ODurL));
        chk("aim", e.aim, orow(s, OAim));
        chk("targets", e.targets, want_t);
        chk("on save", e.on_save, orow(s, OOnSave));
        chk("save", e.save, orow(s, OSave));
        chk("effect", e.affect, orow(s, OAffect));
        chk("when", e.when, orow(s, OWhen));
        chk("delay", e.delay, orow(s, ODelay));
        chk("priority", e.priority, orow(s, OPrio));
        snprintf(w, sizeof w, "spell %d's row as the original reads it", s);
        expect(S, w, !diff[0], "%s", diff);
    }
}

// 2. The caster's level for every spell, by class (GAME.OVR 0x3AFD7), both
// the camp's (spells::power, item_power) and the fight's (combat::power_of)
void caster_levels()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    struct Who {
        int cls, lvl;
    } who[] = {{classes::Cleric, 9}, {classes::MagicUser, 11}, {classes::Ranger, 10}, {classes::Ranger, 7},
               {classes::Paladin, 10}, {classes::Paladin, 8}, {classes::Fighter, 9}, {classes::Thief, 12}};
    for (int fx = 0; fx < 2; ++fx) {
        rules::set_fixes(fx == 1);
        int bad = 0;
        char first[160] = "";
        for (const Who& h : who)
            for (int s = 1; s <= 117; ++s)
                for (int item = 0; item < 2; ++item) {
                    if (orow(s, OCls) == 2) continue;          // (no spell has class 2)
                    member(0, h.cls, h.lvl);
                    const party::Character& c = mem(0);
                    const int want = level_rule(c, s, item);
                    const int got = item ? spells::item_power(c, t, s) : spells::power(c, t, s);
                    const int got2 = combat::power_of(c.rec, t, s, item);
                    if ((got != want || got2 != want) && !bad++)
                        snprintf(first, sizeof first, "class %d level %d spell %d item %d: %d / %d, the original's %d", h.cls,
                                 h.lvl, s, item, got, got2, want);
                }
        snprintf(w, sizeof w, "the caster's level for every spell and caster (fixes %s)", fx ? "on" : "off");
        expect(S, w, bad == 0, "%d wrong; %s", bad, first);
    }
    rules::set_fixes(true);
}

// 3. Times: every spell's effect time at levels 1-14 outside fights
// (GAME.OVR 0x2D5E2: Slow Poison its own fixed time; Cause Disease, the
// items' speed / paralysis / giant strength rolled - dice all highest)
void times()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    int bad = 0;
    char first[120] = "";
    for (int s = 1; s <= 117; ++s)
        for (int L = 1; L <= 14; ++L) {
            dice(1);
            const int got = spells::lasts_rolled(t, s, L, d->prof->magic.facts, rng);
            dice_off();
            const int want = dur_rule(s, L, true);
            if (got != want && !bad++) snprintf(first, sizeof first, "spell %d level %d: %d, the original's %d", s, L, got, want);
        }
    expect(S, "every spell's time (dice highest)", bad == 0, "%d wrong; %s", bad, first);
}

// 4. Camp casting: whom a spell is for - Secret's targets 3 is the whole
// party (GAME.OVR 0x2D949: the party list), 1 the caster, 2 the one chosen
void camp_targets()
{
    using namespace play;
    if (pt->count < 3) return;
    for (int s : {1, 42, 48, 50, 116}) {
        for (int i = 0; i < pt->count; ++i) member(i, classes::Fighter, 5);
        member(0, orow(s, OCls) == 0 ? classes::Cleric : classes::MagicUser, 14);
        dice(1);
        ccast(0, 0, s);
        dice_off();
        const int eff = orow(s, OAffect);
        int got = 0;
        for (int i = 0; i < pt->count; ++i) got += caff(i, eff) >= 0;
        snprintf(w, sizeof w, "camp: spell %d (targets: the party) on every member", s);
        expect(S, w, got == pt->count, "%d of %d", got, pt->count);
    }
}

// 5. Memorizing: the spell slots by class, level and Wisdom / Intelligence
// (GAME.OVR 0x3BE4B, clerics 0x3C477, magic-users 0x3C609), and the
// spells the class routine puts in the book (clerics and paladins every
// cleric spell of a level they have slots for; rangers the druids' and the
// magic-users' of the levels they have slots for - 0x3C083)
void want_slots(int cls, int L, int wis, int intel, int out[3][8])
{
    memset(out, 0, sizeof(int) * 24);
    auto row = [&](int k, int lv) { return b_row("sp_slots", k, lv); };
    if (cls == classes::Cleric && L > 0) {
        out[0][1] = 1;
        for (int lv = 2; lv <= L; ++lv)
            if (const auto* r = row(classes::Cleric, lv))
                for (int c = 1; c <= 7; ++c) out[0][c] += (*r)[1 + c];
        for (const auto& r : b_rows("sp_wis"))
            if (wis >= r[0] && out[0][r[1]] > 0) ++out[0][r[1]];
        const auto& c67 = b_rows("sp_cl67");
        if (!c67.empty()) {
            if (wis < c67[0][0]) out[0][6] = 0;
            if (wis < c67[0][1]) out[0][7] = 0;
        }
    }
    if (cls == classes::Paladin && L > 8)
        for (int lv = 9; lv <= L; ++lv)
            if (const auto* r = row(classes::Paladin, lv))
                for (int c = 1; c <= 7; ++c) out[0][c] += (*r)[1 + c];
    if (cls == classes::Ranger && L > 7)
        for (int lv = 8; lv <= L; ++lv)
            if (const auto* r = row(classes::Ranger, lv)) {
                for (int c = 1; c <= 3; ++c) out[1][c] += (*r)[1 + c];
                for (int c = 4; c <= 5; ++c) out[2][c - 3] += (*r)[1 + c];
            }
    if (cls == classes::MagicUser && L > 0) {
        out[2][1] = 1;
        for (int lv = 2; lv <= L; ++lv)
            if (const auto* r = row(classes::MagicUser, lv))
                for (int c = 1; c <= 7; ++c) out[2][c] += (*r)[1 + c];
        const auto& m67 = b_rows("sp_mu67");
        if (!m67.empty()) {
            if (intel < m67[0][0]) out[2][6] = 0;
            if (intel < m67[0][1]) out[2][7] = 0;
        }
    }
}
void slots_and_books()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    const int kinds[] = {classes::Cleric, classes::Paladin, classes::Ranger, classes::MagicUser};
    int bad = 0, bad_book = 0;
    char first[160] = "", first_b[160] = "";
    for (int cls : kinds)
        for (int L = 1; L <= 18; ++L)
            for (int st : {9, 12, 13, 14, 15, 16, 17, 18}) {
                member(0, cls, L);
                party::Character& c = mem(0);
                memset(c.rec + 0x79, 0, 100);
                memset(c.ext + party::kExtBook, 0, 17);
                c.rec[0x12] = c.rec[0x13] = c.rec[0x14] = c.rec[0x15] = static_cast<uint8_t>(st);
                classes::class_bonuses(c, t);
                int want[3][8];
                want_slots(cls, L, st, st, want);
                for (int k = 0; k < 3; ++k)
                    for (int lv = 1; lv <= 7; ++lv)
                        if (classes::slots(c, k, lv) != want[k][lv] && !bad++)
                            snprintf(first, sizeof first, "class %d level %d stat %d: kind %d level %d: %d, the original's %d", cls,
                                     L, st, k, lv, classes::slots(c, k, lv), want[k][lv]);
                // the book
                for (int s = 1; s <= 117; ++s) {
                    const int oc = orow(s, OCls), lv = orow(s, OLvl);
                    bool learns = false;
                    if (lv >= 1 && lv <= 7) {
                        if ((cls == classes::Cleric || cls == classes::Paladin) && oc == 0) learns = want[0][lv] > 0;
                        if (cls == classes::Ranger && oc == 1) learns = want[1][lv] > 0;
                        if (cls == classes::Ranger && oc == 3) learns = want[2][lv] > 0;
                    }
                    if (classes::in_book(c, s) != learns && !bad_book++)
                        snprintf(first_b, sizeof first_b, "class %d level %d stat %d: spell %d in the book %d, the original's %d", cls,
                                 L, st, s, classes::in_book(c, s), learns);
                }
            }
    expect(S, "spell slots by class, level, Wisdom / Intelligence", bad == 0, "%d wrong; %s", bad, first);
    expect(S, "the spells the class routine puts in the book", bad_book == 0, "%d wrong; %s", bad_book, first_b);
}

// 6. Who can use (memorize, cast) a spell (GAME.OVR 0x2C64B): clerics' -
// Wisdom 9+ and a cleric or a paladin over 8th level; druids' - Wisdom 9+
// and a ranger over 6th level; magic-users' - Intelligence 9+ and a
// magic-user (a human in armour: not in fights) or a ranger over 8th
// level; the items' - nobody
void who_can_use()
{
    using namespace play;
    const classes::Tables& t = mrules->tables;
    struct Who {
        int cls, lvl;
    } who[] = {{classes::Cleric, 1}, {classes::Paladin, 8}, {classes::Paladin, 9}, {classes::Ranger, 6},
               {classes::Ranger, 7}, {classes::Ranger, 9}, {classes::MagicUser, 1}, {classes::Fighter, 9},
               {classes::Thief, 12}};
    int bad = 0;
    char first[160] = "";
    for (const Who& h : who)
        for (int st : {8, 9})
            for (int s = 1; s <= 117; ++s) {
                member(0, h.cls, h.lvl);
                party::Character& c = mem(0);
                c.rec[0x12] = c.rec[0x13] = c.rec[0x14] = c.rec[0x15] = static_cast<uint8_t>(st);
                const int oc = orow(s, OCls);
                bool want = false;
                if (oc == 0) want = st > 8 && (h.cls == classes::Cleric || (h.cls == classes::Paladin && h.lvl > 8));
                if (oc == 1) want = st > 8 && h.cls == classes::Ranger && h.lvl > 6;
                if (oc == 3) want = st > 8 && (h.cls == classes::MagicUser || (h.cls == classes::Ranger && h.lvl > 8));
                if (orow(s, OLvl) < 1 || orow(s, OLvl) > 7) continue;    // (no slots for those: never memorized)
                if (magic::can_use(c, t, s) != want && !bad++)
                    snprintf(first, sizeof first, "class %d level %d stat %d spell %d: %d, the original's %d", h.cls, h.lvl, st, s,
                             magic::can_use(c, t, s), want);
            }
    expect(S, "who can use each spell", bad == 0, "%d wrong; %s", bad, first);
}

// =====================================================================
// 7. Fights: the effect every spell gives through the original's general
// routine (GAME.OVR 0x2D691: a save as the row says - none / "is
// Unaffected" / half - then the row's effect, its time by 0x2D5E2, data
// the caster's level): cast at 9th level (a ranger of 16 for the druids'),
// saves failed (d20 2), then made (d20 20)
int fcaster_class(int s) { return orow(s, OCls) == 0 ? classes::Cleric : orow(s, OCls) == 1 ? classes::Ranger : classes::MagicUser; }
int fcaster_level(int s) { return orow(s, OCls) == 1 ? 16 : 9; }
// The spell's level for a caster set up so (GAME.OVR 0x3AFD7)
int flevel(int s)
{
    const int c = orow(s, OCls);
    return c == 1 ? 16 - 7 : c == 4 ? 12 : 9;
}
// Sets up the caster (0) and the target (6 an enemy / 1 an ally / the
// caster for the self spells - the row's AI target byte, aim 0)
int setup_cast(int s, int hp = 60)
{
    put(0, 0, 10, 10, 60);
    caster(0, fcaster_class(s), fcaster_level(s));
    F(0).rec[0x199] = 100;                          // (any touch lands but a 1)
    put(1, 0, 10, 11, hp);
    put(6, 1, 13, 10, hp);
    if ((orow(s, OAim) & 0x0F) == 0) return 0;
    return orow(s, 15) == 1 ? 6 : 1;
}
// Spells whose own routine gives something else than the general routine
// (each checked on its own below)
bool own_routine(int s)
{
    switch (s) {
    case 3: case 12: case 13: case 14: case 21: case 26: case 27: case 28: case 32: case 35: case 36: case 37: case 39:
    case 41: case 43: case 46: case 48: case 55: case 56: case 57: case 59: case 61: case 62: case 63: case 67: case 70:
    case 73: case 75: case 76: case 78: case 79: case 81: case 82: case 84: case 85: case 86: case 89: case 90: case 93:
    case 96: case 98: case 99: case 110: case 111: case 112: case 113: case 114: case 116: case 117: case 10: case 33:
    case 23: case 49: case 94: case 34: case 91:
        return true;
    default:
        return false;
    }
}
void general_effects()
{
    if (!new_fight()) {
        expect(S, "a fight to cast in", false);
        return;
    }
    for (int s = 1; s <= 117; ++s) {
        const int eff = orow(s, OAffect);
        if (eff <= 0 || own_routine(s)) continue;
        const combat::FightSpell* fs = fspell(s);
        if (!fs) {
            if (orow(s, OWhen) != 0 && orow(s, OLvl) >= 1) {
                snprintf(w, sizeof w, "fight: spell %d is in the engine", s);
                expect(S, w, false);
            }
            continue;
        }
        for (int saved = 0; saved < 2; ++saved) {
            const int t = setup_cast(s);
            dice(1, saved ? 20 : 2);
            cast(0, s, {t});
            dice_off();
            int data = -1;
            const int got = faff(t, eff, &data);
            const int on_save = orow(s, OOnSave);
            const bool want_none = saved && on_save == 1;
            const int want_t = dur_rule(s, flevel(s), true);
            snprintf(w, sizeof w, "fight: spell %d gives effect %d for %d (save %s)", s, eff, want_t, saved ? "made" : "failed");
            if (want_none) expect(S, w, got < 0, "given (%d)", got);
            else expect(S, w, got == want_t && data == flevel(s), "time %d data %d (the original's %d / %d)", got, data, want_t,
                        flevel(s));
        }
    }
}

// =====================================================================
// 8. The spells with routines of their own, in fights
// A monster's Secret race (the record's 0x6B: 7 a monster, 6 a human) as the
// engine keeps it (party::race_from_secret: the monster 0, the human 7)
void race_of(int i, int secret_race) { F(i).rec[0x74] = party::race_from_secret(static_cast<uint8_t>(secret_race)); }
void own_routines()
{
    if (!new_fight()) {
        expect(S, "a fight to cast in", false);
        return;
    }
    const int c = 0, t = 6, t2 = 7;
    // Charm Person (GAME.OVR 0x2E71C): a person - Secret's race under 7 (or
    // flags 2 & 2) - else "is Unaffected"; a failed save: charmed (0x0B)
    for (int race : {6, 7}) {
        setup_cast(10);
        race_of(t, race);
        F(t).rec[0xDE] = 1;
        dice(1, 2);
        cast(c, 10, {t});
        dice_off();
        const bool charmed = faff(t, 0x0B) >= 0;
        snprintf(w, sizeof w, "Charm Person on a %s (Secret race %d)", race == 6 ? "human" : "monster", race);
        expect(S, w, charmed == (race == 6), "charmed %d", charmed);
    }
    {   // a large human: charmed all the same (no size rule)
        setup_cast(10);
        race_of(t, 6);
        F(t).rec[0xDE] = 0x82;
        dice(1, 2);
        cast(c, 10, {t});
        dice_off();
        expect(S, "Charm Person on a large human", faff(t, 0x0B) >= 0);
    }
    // Hold Person (23, 49 - GAME.OVR 0x2E313): Secret's races 0 and 7 never
    // (no size rule); Hold Monsters (94) anyone
    for (int s : {23, 49, 94})
        for (int race : {6, 7})
            for (int size : {1, 0x82}) {
                setup_cast(s);
                race_of(t, race);
                F(t).rec[0xDE] = static_cast<uint8_t>(size);
                dice(1, 5);                         // (5: failed after the -3 - 2 would wrap round to a save made)
                cast(c, s, {t});
                dice_off();
                const bool held = faff(t, 0x34) >= 0;
                const bool want = s == 94 || race == 6;
                snprintf(w, sizeof w, "spell %d on a %s of size %d", s, race == 6 ? "human" : "monster", size & 7);
                expect(S, w, held == want, "held %d", held);
            }
    // Faerie Fire (79) and Charm Monsters (81): the last one picked is
    // unaffected; the rest save (no race or size rule)
    for (int s : {79, 81}) {
        setup_cast(s);
        put(t2, 1, 14, 10, 60);
        race_of(t, 7);
        F(t).rec[0xDE] = 0x83;
        dice(1, 2);
        cast(c, s, {t, t2});
        dice_off();
        const int eff = orow(s, OAffect);
        snprintf(w, sizeof w, "spell %d: a large monster picked first is affected, the last one not", s);
        expect(S, w, faff(t, eff) >= 0 && faff(t2, eff) < 0, "%d / %d", faff(t, eff), faff(t2, eff));
    }
    // Faerie Fire's effect (0x47, its handler 0x130DA): the target's AC 2
    // worse while attacked (to hit 45 against AC 50 needs a 5: a 3 then hits)
    {
        setup_cast(79);
        put(t2, 1, 14, 10, 60);
        dice(1, 2);
        cast(c, 79, {t, t2});
        dice_off();
        put(1, 0, 12, 10, 60);
        const int hits = hit_with(1, t, 3);
        expect(S, "Faerie Fire: AC 2 worse (a 3 hits where a 5 was needed)", faff(t, 0x47) >= 0 && hits > 0, "%d hits", hits);
    }
    // Sleep (GAME.OVR 0x2EB13): 4d4 Hit Dice' worth (costs by Hit Dice; a
    // 5 Hit Dice one costs 20 - Secret's race 0, never met, 10)
    {
        const auto& cost = b_rows("sp_sleep_cost");
        const auto& bud = b_rows("sp_sleep_budget");
        for (int hd : {1, 2, 3, 4, 5, 6}) {
            setup_cast(21);
            race_of(t, 7);
            F(t).rec[0xE5] = static_cast<uint8_t>(hd);
            dice(1, 2);
            cast(c, 21, {t});
            dice_off();
            const int budget = bud.empty() ? 0 : bud[0][0] * bud[0][1];
            const int need = cost.empty() ? 99 : cost[0][hd];
            const bool asleep = faff(t, 0x35) >= 0;
            snprintf(w, sizeof w, "Sleep: a %d Hit Dice monster (budget %d, cost %d)", hd, budget, need);
            expect(S, w, asleep == (need <= budget), "asleep %d", asleep);
        }
    }
    // Snake Charm (GAME.OVR 0x2EDBE): the snakes (record flags 1 0x15D & 0x10)
    // their hit points within the caster's
    {
        setup_cast(27);
        F(t).rec[0x14B] = 0x50;                     // (the giant snake's flags)
        F(t).rec[0x1A4] = 20;
        put(t2, 1, 14, 10, 20);
        F(t2).rec[0x14B] = 0;
        cast(c, 27, {c});
        expect(S, "Snake Charm: a snake (flags 0x10) charmed, not another", faff(t, 0x33) >= 0 && faff(t2, 0x33) < 0, "%d / %d",
               faff(t, 0x33), faff(t2, 0x33));
    }
    // Enlarge (GAME.OVR 0x2E7FA): the Strength by the caster's level (the
    // oracle's), when more than the target's OWN Strength (record 0x10 /
    // 0x1D - 0x372FC); its data 18/xx: xx + 1, else Strength + 100
    for (int L : {1, 3, 6, 7, 10, 12, 14}) {
        setup_cast(12);
        caster(c, classes::MagicUser, L);
        F(1).rec[0x10] = F(1).rec[0x11] = 12;
        F(1).rec[0x1C] = F(1).rec[0x1D] = 0;
        cast(c, 12, {1});
        const auto* r = b_row("sp_enlarge", L);
        const int want = !r ? -1 : (*r)[1] == 18 ? (*r)[2] + 1 : (*r)[1] + 100;
        int data = -1;
        const int got = faff(1, 0x0C, &data);
        snprintf(w, sizeof w, "Enlarge at level %d: its Strength", L);
        expect(S, w, got >= 0 && data == want, "data %d, the original's %d", data, want);
    }
    {   // against the own Strength, not the one in use (an Enlarged 18/76 own 12: a 3rd level's 18/51 goes on)
        setup_cast(12);
        caster(c, classes::MagicUser, 3);
        F(1).rec[0x10] = 12;
        F(1).rec[0x11] = 18;
        F(1).rec[0x1D] = 0;
        F(1).rec[0x1C] = 76;
        cast(c, 12, {1});
        expect(S, "Enlarge: against the target's own Strength", faff(1, 0x0C) >= 0);
    }
    // Harm (GAME.OVR 0x3054D): diseased (0x22), then the damage HP - 1d4 as a
    // byte - more than it has (255) when its HP are under the roll (rules::fixes:
    // AD&D's - nothing then)
    for (int fx = 0; fx < 2; ++fx) {
        rules::set_fixes(fx == 1);
        setup_cast(56, 3);
        dice(1);
        cast(c, 56, {t});
        dice_off();
        const bool down = !F(t).up() || F(t).hp() == 0;
        snprintf(w, sizeof w, "Harm on 3 hit points, the roll 4 (fixes %s)", fx ? "on" : "off");
        expect(S, w, down == (fx == 0) && faff(t, 0x22) >= 0, "down %d HP %d", down, F(t).hp());
        setup_cast(56, 40);
        dice(1);
        cast(c, 56, {t});
        dice_off();
        snprintf(w, sizeof w, "Harm on 40 hit points: down to the roll (fixes %s)", fx ? "on" : "off");
        expect(S, w, F(t).hp() == 4, "HP %d", F(t).hp());
    }
    rules::set_fixes(true);
    // Globe of Invulnerability (112's effect 0x39, handler 0x123D3): spells of
    // the 4th level and lower do nothing on it; Minor Globe (0x3F) the 3rd
    for (int globe : {0x39, 0x3F})
        for (int s : {47, 87, 92}) {             // Fireball 3rd, Ice Storm 4th, Cone of Cold 5th
            setup_cast(s, 100);
            give(t, globe);
            dice(1, 2);
            cast(c, s, {t});
            dice_off();
            const bool hurt = F(t).hp() < 100;
            const bool want = orow(s, OLvl) > (globe == 0x39 ? 4 : 3);
            snprintf(w, sizeof w, "effect %02X against spell %d (level %d)", globe, s, orow(s, OLvl));
            expect(S, w, hurt == want, "hurt %d", hurt);
        }
    // Sticks to Snakes (GAME.OVR 0x30AC9): the caster "turns sticks into
    // snakes" (word about the caster); a target of 6 Hit Dice "smashes them flat"
    {
        setup_cast(70);
        F(t).rec[0xE5] = 3;
        cast(c, 70, {t});
        const combat::FightSpell* fs = fspell(70);
        expect(S, "Sticks to Snakes: said of the caster, its word", lines_of(c, combat::Did::Word) == 1 && fs &&
                                                                         fs->word == 0x30A9D, "word %X", fs ? fs->word : 0);
    }
    // Entangle (78): its routine is empty (GAME.OVR 0x30E46) - nothing at all
    {
        setup_cast(78);
        const bool indoors = fg->b.indoors;
        fg->b.indoors = false;
        F(t).moves = 6;
        dice(1, 2);
        cast(c, 78, {t});
        dice_off();
        fg->b.indoors = indoors;
        expect(S, "Entangle: nothing (an empty routine)", F(t).n_aff && *F(t).n_aff == 0 && F(t).moves == 6, "%d effects, moves %d",
               F(t).n_aff ? *F(t).n_aff : -1, F(t).moves);
    }
    // The clouds on the field: Stinking Cloud's ground and Cloudkill's (GAME.OVR
    // 0x2F39F, 0x31A43 - Secret's ground table's own pictures)
    {
        const auto* g = b_row("sp_ground", b_rows("sp_ground").empty() ? -1 : b_rows("sp_ground")[0][0]);
        fg->b.n_clouds = 0;
        combat::lay_cloud(fg->b, fg->t, 4, 4, 3, false);
        combat::lay_cloud(fg->b, fg->t, 8, 4, 3, true);
        expect(S, "Stinking Cloud's and Cloudkill's ground", g && fg->b.ground[4][4] == (*g)[0] && fg->b.ground[4][8] == (*g)[1],
               "%d / %d", fg->b.ground[4][4], fg->b.ground[4][8]);
        expect(S, "a fighter in each cloud is in it", combat::in_cloud(fg->b, 4, 4) && combat::in_poison(fg->b, 8, 4));
    }
    // Stinking Cloud: the unliving (flags 2 & 8) breathe nothing (GAME.OVR 0x36AC4)
    {
        setup_cast(34);
        F(t).flags2 = 8;
        dice(1, 2);
        const combat::Did r = combat::breathe_cloud(fg->b, t, rng);
        dice_off();
        expect(S, "Stinking Cloud: the unliving unaffected", r == combat::Did::Unaffected && faff(t, 0x1F) < 0);
        F(t).flags2 = 0;
        dice(1, 2);
        combat::breathe_cloud(fg->b, t, rng);
        dice_off();
        int data = -1;
        expect(S, "Stinking Cloud: helpless d4 + 1 (data 0xFF)", faff(t, 0x1F, &data) == 5 && data == 0xFF, "data %d", data);
    }
    // At the roll (secret_facts_2.md 29: Secret's effect phases): invisibility
    // (-4), Dispel Evil against the evil (-7) and Blink (a miss) never touch the
    // roll in the original - AD&D's with rules::fixes()
    for (int fx = 0; fx < 2; ++fx) {
        rules::set_fixes(fx == 1);
        struct Case {
            const char* what;
            int effect, d20;
        } cases[] = {{"invisibility", 0x19, 6}, {"Dispel Evil", 0x04, 10}, {"Blink", 0x25, 19}};
        for (const Case& k : cases) {
            put(t, 0, 11, 10, 60);
            give(t, k.effect);
            F(t).delay = 0;
            put(1, 1, 12, 10, 60);
            F(1).rec[0x14B] = 1;                    // (an evil one: flags 1 & 1)
            const int hits = hit_with(1, t, k.d20);
            snprintf(w, sizeof w, "%s at the roll (fixes %s)", k.what, fx ? "on: AD&D's" : "off: the original's");
            expect(S, w, (hits > 0) == (fx == 0), "%d hits", hits);
        }
    }
    rules::set_fixes(true);
}

// =====================================================================
// 9. Camp: the spells with routines of their own
void give_member(int i, int type, int data = 0xFF)
{
    party::Character& c = mem(i);
    spells::add_affect(c, type, 0, data, false);
}
void camp_own()
{
    using namespace play;
    if (pt->count < 3) return;
    const auto& cures = d->prof->cures;
    // Heal (36, GAME.OVR 0x2F699) and Barkskin (90: the same routine - no
    // effect given): the most HP less 1d4; blindness, disease, feeblemind cured
    for (int s : {36, 90}) {
        member(0, s == 36 ? classes::Cleric : classes::Ranger, 16);
        member(1, classes::Fighter, 5, 60);
        mem(1).rec[0x1A4] = 10;
        give_member(1, cures.blinded);
        give_member(1, 0x22);
        give_member(1, 0x1F);
        dice(1);
        ccast(0, 1, s);
        dice_off();
        snprintf(w, sizeof w, "camp: spell %d heals to the most less 1d4 (dice high: 56)", s);
        expect(S, w, mem(1).hp() == 56, "HP %d", mem(1).hp());
        snprintf(w, sizeof w, "camp: spell %d cures blindness, disease (helpless too)", s);
        expect(S, w, caff(1, cures.blinded) < 0 && caff(1, 0x22) < 0 && caff(1, 0x1F) < 0);
        snprintf(w, sizeof w, "camp: spell %d gives no effect", s);
        expect(S, w, caff(1, 0x0D) < 0 && mem(1).n_affects == 0, "%d effects", mem(1).n_affects);
    }
    // Slow Poison (26, GAME.OVR 0x2ED24): a poisoned one - 0 HP to 1, okay
    // again (status 0, in combat), effect 0x16 for 3780 minutes (data 0xFF)
    // and the poison's damage 0x0F for 10; not poisoned: nothing
    {
        member(0, classes::Cleric, 9);
        member(1, classes::Fighter, 5, 60);
        mem(1).rec[0x1A4] = 0;
        mem(1).rec[0x195] = party::Dead;
        mem(1).rec[0x196] = 0;
        give_member(1, cures.poisoned);
        ccast(0, 1, 26);
        int t = 0, dt = 0, t2 = 0;
        expect(S, "camp: Slow Poison on the poisoned dead: okay with 1 HP", mem(1).health() == party::Okay && mem(1).hp() == 1 &&
                                                                              mem(1).in_combat(),
               "status %d HP %d", mem(1).health(), mem(1).hp());
        expect(S, "camp: Slow Poison's effects", caff(1, 0x16, &t, &dt) >= 0 && t == dur_rule(26, 9, true) && dt == 0xFF &&
                                                     caff(1, 0x0F, &t2) >= 0 && t2 == 10,
               "%d (%d) data %d, 0x0F %d", t, dur_rule(26, 9, true), dt, t2);
        member(1, classes::Fighter, 5, 60);
        ccast(0, 1, 26);
        expect(S, "camp: Slow Poison on one not poisoned: nothing", mem(1).n_affects == 0);
    }
    // Strength (35, GAME.OVR 0x2F477): + d4 magic-user, d6 cleric / thief, d8
    // fighter (the last that fits); over 18: 18 - a fighter's or ranger's
    // 18/xx + 10 a point (at most 100; rules::fixes: a paladin's too); when
    // more than the OWN Strength: effect 0x26, data 100 + the roll
    struct St {
        int cls, lvl, die;
        bool pct;
    } sts[] = {{classes::MagicUser, 5, 4, false}, {classes::Cleric, 5, 6, false}, {classes::Thief, 5, 6, false},
               {classes::Fighter, 5, 8, true}, {classes::Ranger, 9, 0, true}};
    for (const St& x : sts) {
        member(0, classes::MagicUser, 9);
        member(1, x.cls, x.lvl, 60);
        mem(1).rec[0x10] = mem(1).rec[0x11] = 16;
        dice(1);
        ccast(0, 1, 35);
        dice_off();
        int data = -1;
        const bool got = caff(1, 0x26, nullptr, &data) >= 0;
        snprintf(w, sizeof w, "camp: Strength on class %d (die d%d)", x.cls, x.die);
        if (x.die) expect(S, w, got && data == 100 + x.die, "effect %d data %d", got, data);
    }
    // Enlarge (12) in camp: 23 at 12th level and above; against the own Strength
    {
        member(0, classes::MagicUser, 14);
        member(1, classes::Fighter, 5, 60);
        ccast(0, 1, 12);
        int data = -1;
        expect(S, "camp: Enlarge at 14th level: Strength 23", caff(1, 0x0C, nullptr, &data) >= 0 && data == 123, "data %d", data);
    }
    // Neutralize Poison (67, GAME.OVR 0x3092A): 0 HP to 1, 0x37 0x16 0x0F
    // gone, okay, in combat, "is unpoisoned"; not poisoned "is unaffected"
    {
        member(0, classes::Cleric, 9);
        member(1, classes::Fighter, 5, 60);
        mem(1).rec[0x1A4] = 0;
        mem(1).rec[0x195] = party::Dead;
        give_member(1, cures.poisoned);
        give_member(1, 0x16);
        give_member(1, 0x0F);
        ccast(0, 1, 67);
        expect(S, "camp: Neutralize Poison", mem(1).health() == party::Okay && mem(1).hp() == 1 && mem(1).n_affects == 0 &&
                                                  csaid(1, spells::Said::Unpoisoned) == 1);
        ccast(0, 1, 67);
        expect(S, "camp: Neutralize Poison on one not poisoned: \"is unaffected\"", csaid(1, spells::Said::Unaffected) == 1);
    }
    // Raise Dead (75, GAME.OVR 0x30CB4): the dead (status 6) or animated (1)
    // with own Constitution over 0 and not an elf (Secret's race 1): poison
    // gone, okay, own Constitution - 1, 1 HP, "is raised"
    for (int elf = 0; elf < 2; ++elf) {
        member(0, classes::Cleric, 9);
        member(1, classes::Fighter, 5, 60);
        mem(1).rec[0x1A4] = 0;
        mem(1).rec[0x195] = party::Dead;
        mem(1).rec[0x18] = mem(1).rec[0x19] = 14;
        if (elf) {
            mem(1).rec[0x74] = party::race_from_secret(1);
            mem(1).ext[party::kExtRace] = 1;
        }
        give_member(1, cures.poisoned);
        ccast(0, 1, 75);
        const bool raised = mem(1).health() == party::Okay;
        snprintf(w, sizeof w, "camp: Raise Dead on a dead %s", elf ? "elf: not" : "human");
        expect(S, w, elf ? !raised : raised && mem(1).hp() == 1 && mem(1).rec[0x18] == 13 && caff(1, cures.poisoned) < 0,
               "status %d HP %d Con %d", mem(1).health(), mem(1).hp(), mem(1).rec[0x18]);
    }
    // Stone to Flesh (113, GAME.OVR 0x32120): the stoned okay, 1 HP, "is no
    // longer a statue"; else "is not stoned."
    {
        member(0, classes::MagicUser, 14);
        member(1, classes::Fighter, 5, 60);
        mem(1).rec[0x195] = party::Stoned;
        mem(1).rec[0x196] = 0;
        ccast(0, 1, 113);
        expect(S, "camp: Stone to Flesh", mem(1).health() == party::Okay && mem(1).hp() == 1 && mem(1).in_combat() &&
                                               csaid(1, spells::Said::Word) == 1);
        ccast(0, 1, 113);
        expect(S, "camp: Stone to Flesh on one not stoned: \"is not stoned.\"", csaid(1, spells::Said::Word2) == 1);
    }
    // Cure Blindness (37, GAME.OVR 0x2F810): blindness gone, "can see" alone
    {
        member(0, classes::Cleric, 9);
        member(1, classes::Fighter, 5, 60);
        give_member(1, cures.blinded);
        ccast(0, 1, 37);
        expect(S, "camp: Cure Blindness", caff(1, cures.blinded) < 0 && csaid(1, spells::Said::CanSee) == 1 && cn == 1, "%d lines", cn);
    }
    // The Elixir of Youth (63, GAME.OVR 0x30813): age - (1d4 + 1) (dice high: 5); under 18: 18
    for (int age : {40, 20}) {
        member(0, classes::Fighter, 5);
        for (int i = 0; i < pt->count; ++i) {
            mem(i).rec[0x76] = static_cast<uint8_t>(age);
            mem(i).rec[0x77] = 0;
        }
        dice(1);
        ccast(0, 0, 63);
        dice_off();
        const int want = age - 5 < 18 ? 18 : age - 5;
        snprintf(w, sizeof w, "camp: the Elixir of Youth at %d (its user only - 0x7D38)", age);
        expect(S, w, mem(0).age() == want && mem(1).age() == age, "age %d / %d, the original's %d / %d", mem(0).age(), mem(1).age(),
               want, age);
    }
}

} // namespace sspells

void all_spells()
{
    using namespace sspells;
    if (!b_oracle_load("secret_spells")) {
        expect(S, "oracle file", false, "no oracle_secret_spells.txt");
        return;
    }
    if (!boot() || !load_magic()) {
        expect(S, "boot", false);
        return;
    }
    rules::set_fixes(true);
    table_rows();
    caster_levels();
    times();
    camp_targets();
    slots_and_books();
    who_can_use();
    general_effects();
    own_routines();
    camp_own();
}

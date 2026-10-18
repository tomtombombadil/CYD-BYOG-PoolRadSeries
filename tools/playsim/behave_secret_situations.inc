// Behaviour checks: Secret of the Silver Blades - situations (included by
// behave_secret.inc inside namespace bsecret; ONLY=allsituations). Expected values
// from Secret's own program and tables (the oracle, scratch) and the
// Project's claude/secret_facts.md / secret_facts_2.md - never from the
// engine's own code.

// ---- A probe (ENDLESS=mon,copies): the GOG party (bare-handed) against a
// monster, Quick, the party kept up as the events sweep does - the other
// side's hit points every 25 rounds (are the long fights a rule's fault?)
int probe_last_round = -1;
int probe_menu()
{
    if (play::fg && play::fg->b.round != probe_last_round && play::fg->b.round % (getenv("EVERY") ? atoi(getenv("EVERY")) : 25) == 0) {
        probe_last_round = play::fg->b.round;
        int hp = 0, up = 0;
        for (int i = 0; i < play::fg->b.n; ++i) {
            const combat::Fighter& f = play::fg->b.f[i];
            if (f.team() == 1 && f.up() && !f.gone) {
                hp += f.hp();
                ++up;
            }
        }
        printf("  round %d: the other side %d up, %d hit points\n", play::fg->b.round, up, hp);
    }
    return 0;
}
void endless_probe(const char* list)
{
    if (!boot()) {
        expect(P, "probe boot", false);
        return;
    }
    fight_clear_monsters();
    for (const char* q = list; q && *q;) {
        int mon = 0, copies = 1, n = 0;
        if (sscanf(q, "%dx%d%n", &mon, &copies, &n) < 2) break;
        fight_load_monster(mon, copies, mon);
        q += n;
        if (*q == ',') ++q;
    }
    fight_start(C);
    god = true;
    for (int k = 0; k < 40 && play::fg; ++k) {
        pump(20000);
        if (!play::fg) break;
        int hp = 0, up = 0, slow = 0;
        for (int i = 0; i < play::fg->b.n; ++i) {
            const combat::Fighter& f = play::fg->b.f[i];
            if (f.team() == 1 && f.up() && !f.gone) {
                hp += f.hp();
                ++up;
            }
        }
        std::string fx;
        for (int i = 0; i < play::fg->b.n; ++i) {
            const combat::Fighter& f = play::fg->b.f[i];
            if (f.team() != 0) continue;
            char t[40];
            snprintf(t, sizeof t, " [%d/%d", f.attacks[0], f.attacks[1]);
            fx += t;
            for (int a = 0; f.n_aff && a < *f.n_aff; ++a) {
                snprintf(t, sizeof t, " %02X", f.aff[a][0]);
                fx += t;
            }
            fx += "]";
        }
        (void)slow;
        if (k == 6)
            for (int i = 0; i < play::fg->b.n; ++i) {
                const combat::Fighter& f = play::fg->b.f[i];
                if (f.gone || !f.up()) continue;
                combat::Battle copy = play::fg->b;
                create::Dice dd = rng;
                const combat::Plan pl = combat::think(copy, play::fg->t, i, dd);
                printf("   #%d %s team %d at %d,%d size %d hp %d status %d flee %d moves %d plan %d target %d\n", i,
                       (const char*)&f.rec[1], f.team(), f.x, f.y, f.size, f.hp(), f.rec[0x195], (int)f.fleeing, f.moves,
                       (int)pl.act, pl.target);
            }
        printf("  round %d: the other side %d up, %d hit points; party%s\n", play::fg->b.round, up, hp, fx.c_str());
    }
    god = false;
    printf("  probe %s: %s\n", list, play::fg ? "not ended" : "ended");
}

// ---- The races' and rangers' effects in a fight (Secret's handlers,
// GAME.OVR 0x10CCB overlay; its effect phases, the dispatcher 0x3624E):
// at the roll (phase 0xA, the attacker's) the gnome's 0x07 +1 when the
// target's flags 2 (record 0x15E) & 4 (0x110A1), the dwarf's 0x1A +1 when
// its flags 1 (0x15D) & 0x80 (0x11703); the dwarf's / gnome's 0x2F / 0x30
// (-4 against flags 1 & 4 / & 0x20) are only in phases 0xB (before the
// roll, which overwrites the d20 they change) and 0xF (a turn's start) - the
// roll's own target phase 0x10 has no case: nothing. The ranger's 0x69
// (phase 4, 0x1427D): + ranger level to the damage when the target's flags
// 1 & 8 and the distance (0x151:0xCA, half-squares / 2) is at most 1. The
// Constitution effect 0x61 is in no phase: never applied (saves 0x36F1F
// call phase 0xC only).
int hit_roll(int a, int c, int d20, int* dmg = nullptr)
{
    combat::Fighter& f = F(a);
    f.attacks[0] = 1;
    f.attacks[1] = 0;
    f.rec[0x199] = 45;               // (roll + 45 against AC 50: a 5 or more hits)
    f.rec[0x19E] = 1;
    f.rec[0x1A0] = 4;
    f.rec[0x1A2] = 0;
    F(c).received = 0;
    F(c).turns = 0;
    s_dice(1, d20);
    const combat::Attack r = combat::attack(fg->b, a, c, names, rng, false);
    create::g_die_hook = nullptr;
    int hits = 0, d = 0;
    for (int k = 0; k < r.n; ++k) {
        hits += r.hits[k].hit;
        d += r.hits[k].hit ? r.hits[k].damage : 0;
    }
    if (dmg) *dmg = d;
    return hits;
}
void race_effects()
{
    if (!new_fight()) {
        expect(P, "a fight", false);
        return;
    }
    const auto& rc = d->prof->create;
    const int a = 0, t = 6;
    char w[120];
    // The dwarf (0x1A) and the gnome (0x07) attacking: the d20 4 (one short)
    struct Foe { const char* who; uint8_t eff; int f1, f2; bool plus; };
    const Foe foes[] = {
        {"dwarf vs flags 1 0x80", rc.dwarf_orc, 0x80, 0, true},
        {"dwarf vs a giant (flags 1 0x2C)", rc.dwarf_orc, 0x2C, 1, false},
        {"dwarf vs flags 1 4 alone", rc.dwarf_orc, 0x04, 0, false},
        {"gnome vs the bugbear (flags 1 0x28, 2 4)", rc.gnome_giant, 0x28, 4, true},
        {"gnome vs a worg (flags 1 0x42)", rc.gnome_giant, 0x42, 0, false},
        {"gnome vs flags 1 2 alone", rc.gnome_giant, 0x02, 0, false},
    };
    for (const Foe& fo : foes) {
        put(a, 0, 10, 10, 60);
        give(a, fo.eff);
        put(t, 1, 11, 10, 60, 4);
        F(t).rec[0x14B] = static_cast<uint8_t>(fo.f1);
        F(t).flags2 = static_cast<uint8_t>(fo.f2);
        const int hits = hit_roll(a, t, 4);
        snprintf(w, sizeof w, "the %s: +1 to hit %s", fo.who, fo.plus ? "(a 4 hits)" : "none (a 4 misses)");
        expect(P, w, (hits > 0) == fo.plus, "%d hits", hits);
    }
    // Attacked: a dwarf (0x2F) / gnome (0x30) by a giant (flags 1 0x2C, size 2):
    // the original - nothing at the roll (a 5 hits); with the rules fixes -4
    for (int fix = 0; fix < 2; ++fix)
        for (int g = 0; g < 2; ++g) {
            rules::set_fixes(fix);
            put(a, 1, 10, 10, 60, 10);
            F(a).rec[0x14B] = 0x2C;
            F(a).flags2 = 1;
            F(a).rec[0xDE] = 2;
            put(t, 0, 11, 10, 60);
            give(t, g ? rc.gnome_extra : rc.giants);
            const int hits = hit_roll(a, t, 8);
            snprintf(w, sizeof w, "a giant against a %s, fixes %s: %s", g ? "gnome (0x30)" : "dwarf (0x2F)", fix ? "on" : "off",
                     fix ? "-4 (an 8 misses)" : "no -4 at the roll (an 8 hits)");
            expect(P, w, (hits > 0) == !fix, "%d hits", hits);
        }
    rules::set_fixes(true);
    // The ranger (0x69, ranger level 0x115 = 7): + 7 against flags 1 & 8 next to it; not from afar
    for (int far = 0; far < 2; ++far)
        for (int giant = 0; giant < 2; ++giant) {
            put(a, 0, 10, 10, 60);
            give(a, rc.ranger_giant);
            for (int k = 0; k < 8; ++k) F(a).rec[0x109 + k] = 0;
            F(a).rec[0x109 + classes::Ranger] = 7;
            put(t, 1, far ? 14 : 11, 10, 200, 8);
            F(t).rec[0x14B] = giant ? 0x2C : 0x00;
            int dmg = 0;
            hit_roll(a, t, 15, &dmg);
            const int want = 4 + (giant && !far ? 7 : 0);
            snprintf(w, sizeof w, "the ranger against %s %s: damage", giant ? "a giant" : "a non-giant", far ? "4 squares off" : "next to it");
            expect(P, w, dmg == want, "%d, the original's %d", dmg, want);
        }
    // The Constitution effect (0x61; Con 17: +4 against wands and spells): the
    // original none (the save 12, the d20 10 fails); with the rules fixes made
    for (int fix = 0; fix < 2; ++fix)
        for (int kind = 0; kind < 5; ++kind) {
            rules::set_fixes(fix);
            put(a, 0, 10, 10, 60);
            give(a, rc.con_save);
            F(a).rec[0x19] = F(a).rec[0x18] = 17;
            for (int k = 0; k < 5; ++k) F(a).rec[0xDF + k] = 12;
            F(a).rec[0x186] = 0;
            s_dice(1, 10);
            const bool made = combat::saving_throw(F(a), kind, 0, rng);
            create::g_die_hook = nullptr;
            const bool want = fix && (kind == 2 || kind == 4);
            snprintf(w, sizeof w, "the Constitution effect, fixes %s: a save of kind %d, a 10 against 12", fix ? "on" : "off", kind);
            expect(P, w, made == want, "%s, the original's %s", made ? "made" : "failed", want ? "made" : "failed");
        }
    rules::set_fixes(true);
    // The elf (0x5F) and half-elf (0x12): 90% / 30% against sleep (0x35) and charm (0x0B)
}

// ---- Every class at every level: spell slots and books, attacks, thief
// skills, item flags (Secret's class routine GAME.OVR 0x3C1B1 and its
// helpers: slots 0x3BE4B / 0x3C477 / 0x3C609, thief skills 0x3C911)
const int kSecretToCurseRace[8] = {0, 2, 4, 1, 3, 5, 7, 0};
const std::vector<int>* srow(const char* key, int a) { return b_row(key, a); }
int sval(const char* key, int idx)
{
    const auto& r = b_rows(key);
    return r.empty() || idx < 0 || idx >= static_cast<int>(r[0].size()) ? 0 : r[0][idx];
}
// Expected slots [row 0 cleric, 1 druid, 2 magic-user][spell level 0-6]
void want_slots(int k, int L, int wis, int intel, int out[3][7])
{
    memset(out, 0, sizeof(int) * 21);
    auto gained = [&](const char* key, int lv, int sl) {
        const auto* r = b_row(key, lv);
        return r && sl + 1 < static_cast<int>(r->size()) ? (*r)[1 + sl] : 0;
    };
    if (k == 0) {
        out[0][0] = 1;
        for (int lv = 2; lv <= L; ++lv)
            for (int sl = 0; sl < 7; ++sl) out[0][sl] += gained("sit_slots_cleric", lv, sl);
        const int at[6] = {0, 0, 1, 1, 2, 3};
        for (int i = 0; i < 6; ++i)
            if (wis > 12 + i && out[0][at[i]] > 0) ++out[0][at[i]];
        if (wis < 17) out[0][5] = 0;
        if (wis < 18) out[0][6] = 0;
    } else if (k == 3 && L > 8) {
        for (int lv = 9; lv <= L; ++lv)
            for (int sl = 0; sl < 7; ++sl) out[0][sl] += gained("sit_slots_paladin", lv, sl);
    } else if (k == 4 && L > 7) {
        for (int lv = 8; lv <= L; ++lv) {
            for (int sl = 0; sl < 3; ++sl) out[1][sl] += gained("sit_slots_ranger", lv, sl);
            for (int sl = 3; sl < 5; ++sl) out[2][sl - 3] += gained("sit_slots_ranger", lv, sl);
        }
    } else if (k == 5) {
        out[2][0] = 1;
        for (int lv = 2; lv <= L; ++lv)
            for (int sl = 0; sl < 7; ++sl) out[2][sl] += gained("sit_slots_mu", lv, sl);
        if (intel < 12) out[2][5] = 0;
        if (intel < 14) out[2][6] = 0;
    }
}
void class_tables()
{
    static party::Character c;
    char w[140];
    int nfail_slots = 0, nfail_book = 0, nfail_att = 0, nslots = 0;
    for (int k = 0; k < 7; ++k)
        for (int L = 1; L <= 18 && L <= top_level(k); ++L)
            for (int ws : {9, 12, 13, 14, 15, 16, 17, 18}) {
                bare(c, 7, k);
                int lv[8] = {};
                lv[k] = L;
                levels(c, lv);
                set_stat(c, 1, ws);       // (Intelligence and Wisdom the same here)
                set_stat(c, 2, ws);
                c.rec[0x11C] = 2;
                memset(c.rec + 0x79, 0, 100);
                memset(c.ext, 0, sizeof c.ext);
                classes::class_bonuses(c, m->tables);
                int want[3][7];
                want_slots(k, L, ws, ws, want);
                bool ok = true;
                for (int row = 0; row < 3; ++row)
                    for (int sl = 0; sl < 7; ++sl) ok = ok && classes::slots(c, row, sl + 1) == want[row][sl];
                ++nslots;
                if (!ok) {
                    ++nfail_slots;
                    snprintf(w, sizeof w, "class %d level %d Wis/Int %d: spell slots", k, L, ws);
                    char got[120];
                    int n = 0;
                    for (int row = 0; row < 3; ++row) {
                        for (int sl = 0; sl < 7; ++sl) n += snprintf(got + n, sizeof got - n, "%d", classes::slots(c, row, sl + 1));
                        n += snprintf(got + n, sizeof got - n, " / ");
                        for (int sl = 0; sl < 7; ++sl) n += snprintf(got + n, sizeof got - n, "%d", want[row][sl]);
                        n += snprintf(got + n, sizeof got - n, "  ");
                    }
                    if (nfail_slots <= 25) expect(P, w, false, "engine / original per row: %s", got);
                }
                // The book: every cleric (code 0) / druid (1) / magic-user (3) spell of a
                // level with slots in its row - clerics and paladins the cleric row,
                // rangers druid and magic-user (0x3BEC8, 0x3BF87, 0x3C07D); a magic-user none
                bool bok = true;
                int bad = 0;
                for (int sp = 1; sp <= 117; ++sp) {
                    const auto* r = srow("sit_spell", sp);
                    if (!r) continue;
                    const int code = (*r)[1], sl = (*r)[2];
                    bool want_b = false;
                    if (sl >= 1 && sl <= 7) {
                        if ((k == 0 || k == 3) && code == 0) want_b = want[0][sl - 1] > 0;
                        if (k == 4 && code == 1) want_b = want[1][sl - 1] > 0;
                        if (k == 4 && code == 3) want_b = want[2][sl - 1] > 0;
                    }
                    if (classes::in_book(c, sp) != want_b) {
                        bok = false;
                        if (!bad) bad = sp;
                    }
                }
                if (!bok) {
                    ++nfail_book;
                    snprintf(w, sizeof w, "class %d level %d Wis %d: the spell book", k, L, ws);
                    if (nfail_book <= 25) expect(P, w, false, "first wrong spell %d (engine %s it)", bad, classes::in_book(c, bad) ? "has" : "hasn't");
                }
                // Attacks (0x121): fighters / paladins > 6 three, > 12 four; rangers > 7 / > 14
                if (ws == 9) {
                    int att = 2;
                    if (k == 2 || k == 3) att = L > 12 ? 4 : L > 6 ? 3 : 2;
                    if (k == 4) att = L > 14 ? 4 : L > 7 ? 3 : 2;
                    if (c.rec[0x11C] != att) {
                        ++nfail_att;
                        snprintf(w, sizeof w, "class %d level %d: attacks (halves)", k, L);
                        expect(P, w, false, "%d, the original's %d", c.rec[0x11C], att);
                    }
                    // Item flags (0x130): the classes' DS 0x4C92 bytes summed
                    const int fl = sval("sit_classflags", k);
                    if (c.rec[0x12B] != fl) {
                        snprintf(w, sizeof w, "class %d level %d: item flags", k, L);
                        expect(P, w, false, "%d, the original's %d", c.rec[0x12B], fl);
                    }
                }
            }
    snprintf(w, sizeof w, "spell slots, %d class / level / Wisdom cases", nslots);
    expect(P, w, nfail_slots == 0, "%d wrong", nfail_slots);
    expect(P, "spell books (every class, level, Wisdom)", nfail_book == 0, "%d wrong", nfail_book);
    expect(P, "attacks by level (every class)", nfail_att == 0, "%d wrong", nfail_att);
    // Thief skills: every race, level 1-18, Dexterity 3-19 (0x3C911 - no
    // item takes part, no extra bonus)
    int nth = 0, nth_bad = 0;
    for (int sr = 1; sr <= 6; ++sr) {
        const auto* ra = srow("sit_thiefrace", sr);
        for (int L = 1; L <= 18; ++L)
            for (int dx = 3; dx <= 19; ++dx) {
                bare(c, kSecretToCurseRace[sr], 6);
                c.ext[party::kExtRace] = static_cast<uint8_t>(sr);
                int lv[8] = {};
                lv[6] = L;
                levels(c, lv);
                set_stat(c, 3, dx);
                classes::class_bonuses(c, m->tables);
                const auto* bs = srow("sit_thiefbase", L);
                const auto* dd = srow("sit_thiefdex", dx);
                bool ok = ra && bs && dd;
                int bad = 0, gotv = 0, wantv = 0;
                for (int sk = 1; ok && sk <= 8; ++sk) {
                    const int adj = (*ra)[sk], base = (*bs)[sk];
                    int v;
                    if (adj < 0 && base < -adj) v = 0;
                    else {
                        v = adj + base;
                        if (sk < 6) v += (*dd)[sk];
                    }
                    v &= 0xFF;
                    if (c.rec[0xEA + sk - 1] != v) {
                        ok = false;
                        bad = sk;
                        gotv = c.rec[0xEA + sk - 1];
                        wantv = v;
                    }
                }
                ++nth;
                if (!ok) {
                    ++nth_bad;
                    snprintf(w, sizeof w, "thief skills: race %d level %d Dex %d skill %d", sr, L, dx, bad);
                    if (nth_bad <= 10) expect(P, w, false, "%d, the original's %d", gotv, wantv);
                }
            }
    }
    snprintf(w, sizeof w, "thief skills, %d race / level / Dexterity cases", nth);
    expect(P, w, nth_bad == 0, "%d wrong", nth_bad);
}

// ---- Hit points (GAME.OVR 0x21DDC, training and creation): for each class
// trained below its last hit die the better of two rolls (level 1 the
// count DS 0x9B0) and Constitution (DS 0x5052, + fighters' extra 17+) x
// that count; past it 3 / 2 / 1; rolls / held (at least 1) and
// Constitution / held each - the Constitution's byte unsigned (the rules
// fixes: signed)
const int kMultiBits[17] = {0x01, 0, 0x04, 0x08, 0x10, 0x20, 0x40, 0, 0x05, 0x25, 0x11, 0x21, 0x41, 0x24, 0x44, 0x64, 0x60};
int con_fig(int con, int k)
{
    const auto& r = b_rows("sit_conhp");
    int v = r.empty() || con < 0 || con > 25 ? 0 : r[0][con];
    if (k == 2 || k == 3 || k == 4)
        v += con == 17 ? 1 : con == 18 ? 2 : con == 19 || con == 20 ? 3 : con >= 21 && con <= 23 ? 4 : con >= 24 && con <= 25 ? 5 : 0;
    return v;
}
// The gain for classes `mask` (levels already raised); *inc the rolled part
int want_gain(const int lv[8], int mask, int con, bool high, int held, int* inc)
{
    int rolled = 0, cs = 0;
    for (int k = 0; k < 7; ++k) {
        if (lv[k] <= 0 || !(sval("sit_classmask", k) & mask)) continue;
        if (lv[k] < sval("sit_maxhd", k)) {
            const int n = lv[k] > 1 ? 1 : sval("sit_hpcount", k);
            rolled += high ? n * sval("sit_hpdice", k) : n;
            cs += con_fig(con, k) * n;
        } else {
            rolled = k == 2 || k == 3 ? 3 : k == 4 || k == 0 || k == 6 ? 2 : 1;
        }
    }
    int i = (rolled & 0xFF) / held;
    if (i < 1) i = 1;
    *inc = i;
    int cp = rules::fixes() ? static_cast<int8_t>(cs & 0xFF) / held : (cs & 0xFF) / held;
    if (rules::fixes() && i + cp < 1) cp = 1 - i;       // (AD&D: at least 1 a level)
    return i + cp;
}
void hit_points()
{
    static party::Character c;
    char w[140];
    int n = 0, bad = 0;
    for (int fix = 0; fix < 2; ++fix)
        for (int cls = 0; cls < 17; ++cls) {
            if (!kMultiBits[cls]) continue;
            int held = 0;
            for (int k = 0; k < 7; ++k) held += (kMultiBits[cls] >> k) & 1;
            for (int con : {3, 6, 9, 15, 16, 17, 18, 19, 20, 21, 24})
                for (int high = 0; high < 2; ++high)
                    for (int k = 0; k < 7; ++k) {
                        if (!((kMultiBits[cls] >> k) & 1)) continue;
                        for (int L = 1; L < 18 && L < top_level(k); ++L) {
                            rules::set_fixes(fix);
                            bare(c, 7, cls < 8 ? cls : 2);
                            c.rec[0x75] = static_cast<uint8_t>(cls);
                            int lv[8] = {};
                            for (int j = 0; j < 7; ++j)
                                if ((kMultiBits[cls] >> j) & 1) lv[j] = j == k ? L : 1;
                            levels(c, lv);
                            set_stat(c, 4, con);
                            classes::class_bonuses(c, m->tables);
                            c.rec[0x78] = 100;
                            c.rec[0x1A4] = 90;
                            c.rec[0x12C] = 40;
                            const int mask = sval("sit_classmask", k);
                            if (high) dice_high();
                            else dice_low();
                            create::train_classes(c, m->tables, m->facts, m->dice, mask, false);
                            dice_usual();
                            ++lv[k];
                            int inc = 0;
                            const int g = want_gain(lv, mask, con, high, held, &inc);
                            const int wmax = (100 + g) & 0xFF, wnow = (wmax - 10) & 0xFF;
                            ++n;
                            if (c.hp_max() != wmax || c.hp() != wnow || c.rec[0x12C] != ((40 + inc) & 0xFF)) {
                                ++bad;
                                snprintf(w, sizeof w, "class %d training %d to %d (class %d), Con %d, dice %s, fixes %d: hit points", cls, L,
                                         L + 1, k, con, high ? "high" : "low", fix);
                                if (bad <= 20)
                                    expect(P, w, false, "max %d now %d rolled %d, the original's %d %d %d", c.hp_max(), c.hp(), c.rec[0x12C], wmax,
                                           wnow, (40 + inc) & 0xFF);
                            }
                        }
                    }
        }
    rules::set_fixes(true);
    snprintf(w, sizeof w, "training's hit points: %d class / level / Constitution / dice cases", n);
    expect(P, w, bad == 0, "%d wrong", bad);
}

// ---- Create New Character (GAME.OVR 0x1D6BE): every race, class,
// alignment and sex with the dice all high and all low - experience,
// levels after silent training, age, stats, 18/xx, hit points, effects,
// money, the spell book
bool race_stops(int sr, int k, int L, int str, int in)
{
    // (training's limits, GAME.OVR 0x220E4: Secret's race numbers)
    switch (sr) {
    case 3: return (k == 2 && (L == 9 || (L == 8 && str == 17) || (L == 7 && str < 17))) || (k == 0 && L >= 8);
    case 1:
        return (k == 2 && (L == 7 || (L == 6 && str == 17) || (L == 5 && str < 17))) ||
               (k == 5 && (L == 11 || (L == 9 && in < 17) || (L == 10 && in == 17))) || (k == 0 && L >= 7);
    case 4: return (k == 2 && (L == 6 || (L == 5 && str < 18))) || (k == 0 && L >= 7);
    case 2:
        return (k == 0 && L == 5) || ((k == 2 || k == 4) && (L == 8 || (L == 7 && str == 17) || (L == 6 && str < 17))) ||
               (k == 5 && (L == 8 || (L == 7 && in == 17) || (L == 6 && in < 17)));
    case 5: return k == 2 && (L == 6 || (L == 5 && str == 17) || (L == 4 && str < 17));
    default: return false;
    }
}
int32_t xp_need(int k, int L)
{
    const auto* r = b_row("xp", k);
    return r && L >= 1 && L < static_cast<int>(r->size()) ? (*r)[L] : -1;
}
void creation()
{
    static party::Character c;
    char w[160];
    int ncase = 0, nbad = 0;
    for (int sr = 1; sr <= 6; ++sr) {
        const auto* rc = srow("sit_raceclasses", sr);
        const auto* lim = srow("sit_racelim", sr);
        const auto* br = srow("sit_agebr", sr);
        const auto* ages = srow("sit_raceage", sr);
        if (!rc || !lim || !br || !ages) {
            expect(P, "the race rows", false);
            return;
        }
        const int race = kSecretToCurseRace[sr];
        for (size_t ci = 1; ci < rc->size(); ++ci) {
            const int cls = (*rc)[ci];
            const auto* al = srow("sit_classalign", cls);
            const auto* mn = srow("sit_classmin", cls);
            for (size_t ai = 1; al && ai < al->size(); ++ai)
                for (int sex = 0; sex < 2; ++sex)
                    for (int high = 0; high < 2; ++high) {
                        if (high) dice_high();
                        else dice_low();
                        create::begin(c, m->tables, m->facts, m->dice, race, sex, cls, (*al)[ai]);
                        create::roll(c, m->tables, m->facts, m->dice);
                        dice_usual();
                        ++ncase;
                        char tag[64];
                        snprintf(tag, sizeof tag, "race %d class %d align %d sex %d dice %s", sr, cls, (*al)[ai], sex, high ? "high" : "low");
                        std::string why;
                        // Age (0x1E82F)
                        auto entry = [&](int e, bool most) {
                            const int base = (*ages)[1 + e * 3], cnt = (*ages)[2 + e * 3], sd = (*ages)[3 + e * 3];
                            return base + (most ? cnt * sd : high ? cnt * sd : cnt);
                        };
                        int age = 0;
                        if (cls <= 6) age = entry(cls, false);
                        else if (cls == 8 || cls == 9 || cls == 10 || cls == 11) age = entry(0, true);
                        else if (cls == 13 || cls == 15 || cls == 16) age = entry(5, true);
                        else if (cls == 14) age = entry(2, true);
                        if (c.age() != age) why += " age " + std::to_string(c.age()) + "/" + std::to_string(age);
                        // Stats (0x1EA17 on)
                        const auto* adj = srow("sit_raceadj", sr);
                        int want[6];
                        for (int i = 0; i < 6; ++i) {
                            int v = (high ? 19 : 4) + (adj ? (*adj)[1 + i] : 0);
                            auto past = [&](int b) { return age > (*br)[1 + b]; };
                            switch (i) {
                            case 0:
                                if (v > 0 && past(0)) v += 1;
                                if (v > 0 && past(1)) v -= 1;
                                if (v > 0 && past(2)) v -= 2;
                                if (v > 0 && past(3)) v -= 1;
                                break;
                            case 1:
                                if (past(1)) v += 1;
                                if (past(3)) v += 1;
                                break;
                            case 2:
                                if (!past(0)) v -= 1;
                                if (past(1)) v += 1;
                                if (past(2)) v += 1;
                                if (past(3)) v += 1;
                                break;
                            case 3:
                                if (past(2)) v -= 2;
                                if (past(3)) v -= 1;
                                break;
                            case 4:
                                if (past(1)) v -= 1;
                                if (past(2)) v -= 1;
                                if (past(3)) v -= 1;
                                break;
                            default: break;
                            }
                            const int lo = i == 0 ? (*lim)[1 + sex] : (*lim)[1 + 4 + i * 2];
                            const int hi = i == 0 ? (*lim)[3 + sex] : (*lim)[1 + 5 + i * 2];
                            if (v < lo) v = lo;
                            if (v > hi) v = hi;
                            if (mn && v < (*mn)[1 + i]) v = (*mn)[1 + i];
                            if (i == 2 && v < 13 && cls >= 8 && cls <= 12) v = 13;
                            want[i] = v;
                            if (c.stat(i) != v || c.stat_now(i) != v)
                                why += " stat" + std::to_string(i) + " " + std::to_string(c.stat(i)) + "/" + std::to_string(v);
                        }
                        const int bits = kMultiBits[cls];
                        int e = 0;
                        if (want[0] == 18 && (bits & 0x1C)) e = std::min(high ? 100 : 1, (*lim)[5 + sex]);
                        if (c.rec[0x1D] != e || c.rec[0x1C] != e) why += " 18/" + std::to_string(c.rec[0x1D]) + " vs " + std::to_string(e);
                        // Experience and silent training (0x2202A with [0x67F7] set)
                        int held = 0;
                        for (int k = 0; k < 7; ++k) held += (bits >> k) & 1;
                        const int32_t xp = held == 1 ? 200000 : (cls == 9 || cls == 15 || cls == 16) ? 66667 : 100000;
                        if (static_cast<int32_t>(c.exp()) != xp) why += " xp " + std::to_string(c.exp());
                        int lv[8] = {};
                        for (int k = 0; k < 7; ++k)
                            if ((bits >> k) & 1) lv[k] = 1;
                        int hpmax = 0, rolled = 0, inc = 0;
                        hpmax = want_gain(lv, 0xFF, want[4], high, held, &inc);
                        rolled = inc;
                        for (int round = 0; round < 30; ++round) {
                            int mask = 0;
                            for (int k = 0; k < 7; ++k) {
                                if (!lv[k] || race_stops(sr, k, lv[k], want[0], want[1])) continue;
                                const int32_t need = xp_need(k, lv[k]);
                                if (need > 0 && need <= xp) mask |= sval("sit_classmask", k);
                            }
                            if (!mask) break;
                            for (int k = 0; k < 7; ++k)
                                if (lv[k] && (sval("sit_classmask", k) & mask)) ++lv[k];
                            int hd = 0;
                            for (int k = 0; k < 7; ++k) hd = std::max(hd, lv[k]);
                            hpmax = (hpmax + want_gain(lv, mask, want[4], high, held, &inc)) & 0xFF;
                            rolled = (rolled + inc) & 0xFF;
                        }
                        for (int k = 0; k < 7; ++k)
                            if (c.level(k) != lv[k]) why += " level" + std::to_string(k) + " " + std::to_string(c.level(k)) + "/" + std::to_string(lv[k]);
                        if (c.hp_max() != hpmax || c.hp() != hpmax || c.rec[0x12C] != rolled)
                            why += " hp " + std::to_string(c.hp_max()) + "/" + std::to_string(c.hp()) + " rolled " + std::to_string(c.rec[0x12C]) +
                                   " vs " + std::to_string(hpmax) + " " + std::to_string(rolled);
                        // Effects: the race's (0x1DF40), a paladin's 0x08, a ranger's 0x69
                        std::vector<int> fx;
                        if (const auto* rf = srow("sit_racefx", sr)) fx.assign(rf->begin() + 1, rf->end());
                        if (bits & 0x08) fx.push_back(d->prof->create.prot_evil);
                        if (bits & 0x10) fx.push_back(d->prof->create.ranger_giant);
                        bool fok = static_cast<int>(fx.size()) == c.n_affects;
                        for (int t : fx) fok = fok && c.has_affect(static_cast<uint8_t>(t));
                        if (!fok) why += " effects (" + std::to_string(c.n_affects) + ")";
                        // Money: none, but a thief's 2d6 gold (0x1F589)
                        for (int i = 0; i < 7; ++i) {
                            const int wm = i == 3 && (bits & 0x40) ? (high ? 12 : 2) : 0;
                            if (c.money(i) != wm) why += " money" + std::to_string(i) + " " + std::to_string(c.money(i)) + "/" + std::to_string(wm);
                        }
                        // The book: a magic-user's four and silent training's lists; a
                        // cleric's 1-8 and every cleric spell of a level with slots
                        std::vector<int> book;
                        if (bits & 0x20) {
                            for (const auto& r : b_rows("sit_mufirst")) book.insert(book.end(), r.begin(), r.end());
                            for (const auto& r : b_rows("sit_musilent"))
                                if (r[0] <= lv[5]) book.insert(book.end(), r.begin() + 1, r.end());
                        }
                        if (bits & 0x01)
                            for (const auto& r : b_rows("sit_clericfirst")) book.insert(book.end(), r.begin(), r.end());
                        int sl[3][7] = {};
                        for (int k = 0; k < 7; ++k) {
                            if (!lv[k]) continue;
                            int one[3][7];
                            want_slots(k, lv[k], want[2], want[1], one);
                            for (int a = 0; a < 3; ++a)
                                for (int b2 = 0; b2 < 7; ++b2) sl[a][b2] += one[a][b2];
                        }
                        for (int sp = 1; sp <= 117; ++sp) {
                            const auto* r = srow("sit_spell", sp);
                            if (!r || (*r)[2] < 1 || (*r)[2] > 7) continue;
                            const int code = (*r)[1], l = (*r)[2] - 1;
                            if (((bits & 0x09) && code == 0 && sl[0][l] > 0) || ((bits & 0x10) && code == 1 && sl[1][l] > 0) ||
                                ((bits & 0x10) && code == 3 && sl[2][l] > 0))
                                book.push_back(sp);
                        }
                        for (int sp = 1; sp <= 117; ++sp) {
                            const bool wb = std::find(book.begin(), book.end(), sp) != book.end();
                            if (classes::in_book(c, sp) != wb) {
                                why += " book spell " + std::to_string(sp) + (wb ? " missing" : " extra");
                                break;
                            }
                        }
                        if (!why.empty()) {
                            ++nbad;
                            snprintf(w, sizeof w, "creation %s:", tag);
                            if (nbad <= 40 || getenv("ALLCREATE")) expect(P, w, false, "%s", why.c_str());
                        }
                    }
        }
    }
    snprintf(w, sizeof w, "Create New Character: %d race / class / alignment / sex / dice cases", ncase);
    expect(P, w, nbad == 0, "%d wrong", nbad);
}

// ---- The temple (its own overlay, GAME.OVR 0x5AFE; secret_facts_2.md 17):
// every service, on the GOG party's first member, the dice high and low
void temple_checks()
{
    if (!boot()) {
        expect(P, "boot for the temple", false);
        return;
    }
    party::Character& ch = pt->m[0];
    pt->selected = 0;
    open_heal(C);
    const uint8_t keep[party::kRecordSize] = {};
    (void)keep;
    static party::Character orig;
    orig = ch;
    char w[120];
    auto reset = [&](int hp) {
        ch = orig;
        ch.n_affects = 0;
        ch.rec[0x195] = party::Okay;
        ch.rec[0x1A4] = static_cast<uint8_t>(hp);
    };
    auto add = [&](uint8_t t) { spells::add_affect(ch, t, 0, 0xFF, false); };
    const int mx = orig.hp_max();
    // Cure Blindness: 0x21 gone
    reset(mx);
    add(0x21);
    secret_cure(rules::kCureBlindness, C);
    expect(P, "temple Cure Blindness: effect 0x21 gone", !ch.has_affect(0x21));
    // Cure Disease: 0x1F 0x22 0x2B 0x2C all gone (DS 0x1B3E)
    reset(mx);
    for (uint8_t t : {0x1F, 0x22, 0x2B, 0x2C}) add(t);
    secret_cure(rules::kCureDisease, C);
    expect(P, "temple Cure Disease: 0x1F 0x22 0x2B 0x2C gone",
           !ch.has_affect(0x1F) && !ch.has_affect(0x22) && !ch.has_affect(0x2B) && !ch.has_affect(0x2C));
    // The wound cures (0x5CFF): 1d8, 2d8+1, 3d8+3; Heal max - HP - 1d4 (a byte)
    const int cures[4] = {rules::kCureLight, rules::kCureSerious, rules::kCureCritical, rules::kHealCure};
    for (int ci = 0; ci < 4; ++ci)
        for (int high = 0; high < 2; ++high)
            for (int start : {1, mx - 2, mx}) {
                reset(start);
                if (high) dice_high();
                else dice_low();
                secret_cure(cures[ci], C);
                dice_usual();
                int amt;
                if (ci == 0) amt = high ? 8 : 1;
                else if (ci == 1) amt = high ? 17 : 3;
                else if (ci == 2) amt = high ? 27 : 6;
                else amt = (mx - start - (high ? 4 : 1)) & 0xFF;
                int want = start == mx ? mx : std::min(mx, start + amt);
                snprintf(w, sizeof w, "temple cure %d from %d/%d, dice %s", cures[ci], start, mx, high ? "high" : "low");
                expect(P, w, ch.hp() == want, "%d, the original's %d", ch.hp(), want);
            }
    // The dead, stoned and gone: no healing
    for (int h : {party::Dead, party::Stoned, party::Gone}) {
        reset(1);
        ch.rec[0x195] = static_cast<uint8_t>(h);
        secret_cure(rules::kCureCritical, C);
        snprintf(w, sizeof w, "temple Cure Critical on status %d: nothing", h);
        expect(P, w, ch.hp() == 1 && ch.health() == h, "HP %d status %d", ch.hp(), ch.health());
    }
    // Heal's cures: blindness, feeblemind 0x44, the diseases
    reset(mx - 10);
    for (uint8_t t : {0x21, 0x44, 0x22}) add(t);
    secret_cure(rules::kHealCure, C);
    expect(P, "temple Heal: blindness, feeblemind and disease gone", !ch.has_affect(0x21) && !ch.has_affect(0x44) && !ch.has_affect(0x22));
    // Neutralize Poison (0x6230): 0x37 there - HP 0 -> 1, 0x37 0x16 0x0F gone, okay
    reset(0);
    ch.rec[0x195] = party::Dying;
    for (uint8_t t : {0x37, 0x16, 0x0F}) add(t);
    secret_cure(rules::kNeutralizePoison, C);
    expect(P, "temple Neutralize Poison", ch.hp() == 1 && ch.health() == party::Okay && !ch.has_affect(0x37) && !ch.has_affect(0x16) &&
                                              !ch.has_affect(0x0F), "HP %d status %d", ch.hp(), ch.health());
    reset(5);
    secret_cure(rules::kNeutralizePoison, C);
    expect(P, "temple Neutralize Poison on the unpoisoned: nothing", ch.hp() == 5);
    // Raise Dead (0x5FC5): the dead only; elves never; Con - 1 (own, in use), 1 HP, max kept
    for (int elf = 0; elf < 2; ++elf) {
        reset(0);
        ch.rec[0x195] = party::Dead;
        if (elf) ch.rec[0x74] = 2;
        const int con = ch.rec[0x19];
        secret_cure(rules::kRaiseDead, C);
        if (elf)
            expect(P, "temple Raise Dead: an elf can't be raised", ch.health() == party::Dead);
        else
            expect(P, "temple Raise Dead: 1 HP, okay, Constitution - 1, maximum kept",
                   ch.health() == party::Okay && ch.hp() == 1 && ch.rec[0x19] == con - 1 && ch.rec[0x18] == con - 1 && ch.hp_max() == mx,
                   "status %d HP %d/%d Con %d", ch.health(), ch.hp(), ch.hp_max(), ch.rec[0x19]);
    }
    reset(5);
    secret_cure(rules::kRaiseDead, C);
    expect(P, "temple Raise Dead on the living: nothing", ch.health() == party::Okay && ch.rec[0x19] == orig.rec[0x19]);
    // Remove Curse (0x6344): effect 0x24 gone
    reset(mx);
    add(0x24);
    secret_cure(rules::kRemoveCurse, C);
    expect(P, "temple Remove Curse: Bestow Curse's 0x24 gone", !ch.has_affect(0x24));
    // Stone to Flesh (0x6429): status 7 -> 0, HP 1
    reset(20);
    ch.rec[0x195] = party::Stoned;
    secret_cure(rules::kStoneToFlesh, C);
    expect(P, "temple Stone to Flesh", ch.health() == party::Okay && ch.hp() == 1, "status %d HP %d", ch.health(), ch.hp());
    ch = orig;
    delete hw;
    hw = nullptr;
    screen = Screen::Game;
}

// ---- Op 0x41, the experience award (GAME.OVR 0x43FF; its scripts use it 26
// times): operand 1 not 0 - every member, 0 - the selected; operand 2 the
// experience, each member's share = it / the classes with a level; only
// those with record 0x1A7 (in-combat byte) set; "Congratulations the party
// gains experience!" / "Congratulations NAME gains experience!" (0x43AF,
// 0x43DB, 0x43EC) in the text area for the game's delay
ecl::Stop run_code(const std::vector<uint8_t>& code, uint16_t at)
{
    ecl::GameState& g = d->gs;
    memset(g.code, 0, sizeof g.code);
    memcpy(g.code, code.data(), code.size());
    g.code_len = static_cast<uint32_t>(code.size());
    vm->init_script(true);
    return vm->run(at);
}
void xp_award()
{
    if (!boot()) {
        expect(P, "boot for 0x41", false);
        return;
    }
    std::vector<uint8_t> code;
    for (int i = 0; i < 5; ++i) {
        code.push_back(0);
        code.push_back(2);
        code.push_back(0x14);
        code.push_back(0x80);
    }
    char w[120];
    for (int who = 0; who < 2; ++who) {
        std::vector<uint8_t> c = code;
        const uint8_t prog[] = {0x41, 0, static_cast<uint8_t>(who), 2, 0xE8, 0x03, 0x00};   // EXPERIENCE who, 1000; EXIT
        c.insert(c.end(), prog, prog + sizeof prog);
        uint32_t before[8];
        for (int i = 0; i < pt->count; ++i) before[i] = pt->m[i].exp();
        pt->m[3].rec[0x196] = 0;                // (one not counted)
        pt->selected = 2;
        ecl::Stop st = run_code(c, 0x8014);
        const bool printed = st == ecl::Stop::Waiting && vm->wait() == ecl::Wait::Print;
        const std::string txt = vm->text();
        if (printed) st = vm->resume();
        const bool paused = st == ecl::Stop::Waiting && vm->wait() == ecl::Wait::Pause;
        if (paused) vm->resume();
        bool ok = true;
        std::string got;
        for (int i = 0; i < pt->count; ++i) {
            int n = 0;
            for (int k = 0; k < 7; ++k) n += pt->m[i].level(k) > 0;
            const bool gets = (who || i == 2) && i != 3 && n > 0;
            const uint32_t want = before[i] + (gets ? 1000u / n : 0u);
            ok = ok && pt->m[i].exp() == want;
            got += " " + std::to_string(pt->m[i].exp() - before[i]);
        }
        snprintf(w, sizeof w, "op 0x41 %s 1000: each its share by its classes", who ? "everyone" : "the selected");
        expect(P, w, ok, "gains%s", got.c_str());
        char nm[20];
        pt->m[2].name(nm, sizeof nm);
        snprintf(w, sizeof w, "op 0x41 %s: its words, then the game's delay", who ? "everyone" : "the selected");
        const bool words = who ? txt.find("party gains experience") != std::string::npos
                               : txt.find("Congratulations") == 0 && txt.find(" gains experience") != std::string::npos;
        expect(P, w, printed && paused && words, "\"%s\"", txt.c_str());
        pt->m[3].rec[0x196] = 1;
    }
    // A real one: ECL1 block 34 at 0x99B5 - EXPERIENCE 1, 60000 (the party)
    if (boot()) {
        d->gs.game_area = 2;
        vm->set(0x7F12, 2);
        d->gs.script = 34;
        uint32_t before[8];
        for (int i = 0; i < pt->count; ++i) before[i] = pt->m[i].exp();
        if (host->load_script(34, d->gs.code, &d->gs.code_len) && vm->init_script()) {
            const ecl::Stop st = vm->run(0x99B5);
            bool ok = st == ecl::Stop::Waiting && vm->wait() == ecl::Wait::Print;
            for (int i = 0; i < pt->count; ++i) {
                int n = 0;
                for (int k = 0; k < 7; ++k) n += pt->m[i].level(k) > 0;
                ok = ok && pt->m[i].exp() == before[i] + 60000u / n;
            }
            expect(P, "ECL1 block 34's EXPERIENCE (0x99B5): 60000 shared by each one's classes", ok);
        } else {
            expect(P, "ECL1 block 34", false);
        }
    }
    // Its script words (the encounter's, DAMAGE's, 0x41's) read from GAME.OVR
    bool all = true;
    for (int i = 0; i < ecl::kScriptWords; ++i) all = all && d->script_word[i][0];
    expect(P, "Secret's script words: every one read (Both sides wait ... gains experience!)", all);
}

// ---- Camp: a day's rest heals 1 HP each (GAME.OVR 0x2BEB3: 288 steps of 5
// minutes), counted from the camp's start (0x1D158) across Rests
bool rest_for(int minutes)
{
    rest = RestRun{};
    rest.left = minutes;
    rest.running = true;
    magic::begin(rest.r);
    screen = Screen::Rest;
    for (int k = 0; k < 20000 && rest.running; ++k) {
        g_now += 2000;
        rest_tick(g_now, C);
    }
    return !rest.running && !rest.interrupted;
}
void camp_rest()
{
    if (!boot() || !load_magic()) {
        expect(P, "boot for the camp", false);
        return;
    }
    for (int i = 0; i < pt->count; ++i) pt->m[i].rec[0x1A4] = static_cast<uint8_t>(pt->m[i].hp_max() - 5);
    open_camp(C);
    const bool a = rest_for(12 * 60);
    int healed = 0;
    for (int i = 0; i < pt->count; ++i) healed += pt->m[i].hp() == pt->m[i].hp_max() - 4;
    expect(P, "camp: 12 hours' rest heals nothing", a && healed == 0, "%d healed", healed);
    const bool b = rest_for(12 * 60);
    healed = 0;
    for (int i = 0; i < pt->count; ++i) healed += pt->m[i].hp() == pt->m[i].hp_max() - 4;
    expect(P, "camp: another 12 hours in the same camp - a day: 1 HP each", b && healed == pt->count, "%d of %d healed", healed,
           pt->count);
    const bool c2 = rest_for(24 * 60);
    healed = 0;
    for (int i = 0; i < pt->count; ++i) healed += pt->m[i].hp() == pt->m[i].hp_max() - 3;
    expect(P, "camp: a further day: 1 HP more each", c2 && healed == pt->count, "%d of %d", healed, pt->count);
    screen = Screen::Camp;
}

// ---- Modify (GAME.OVR 0x1FDB4): the hit points' bounds - least 0x21B18,
// most 0x21BF1 (worked by hand from the routines)
void modify_bounds()
{
    static party::Character c;
    struct Case { int race, cls, lv[8], con, least, most; const char* what; };
    const Case cases[] = {
        {7, 0, {9, 0, 0, 0, 0, 0, 0, 0}, 16, 11, 90, "human cleric 9, Con 16"},
        {4, 13, {0, 0, 6, 0, 0, 7, 0, 0}, 17, 9, 60, "half-elf fighter 6 / magic-user 7, Con 17"},
        {7, 4, {0, 0, 0, 0, 9, 0, 0, 0}, 18, 14, 120, "human ranger 9, Con 18"},
        {7, 6, {0, 0, 0, 0, 0, 0, 10, 0}, 5, 9, 50, "human thief 10, Con 5"},
        {7, 2, {0, 0, 11, 0, 0, 0, 0, 0}, 16, 13, 92, "human fighter 11 (past its hit dice), Con 16"},
    };
    char w[120];
    for (const Case& k : cases) {
        bare(c, k.race, k.cls);
        levels(c, k.lv);
        set_stat(c, 4, k.con);
        const int lo = create::hp_least(c, m->tables, m->facts), hi = create::hp_most(c, m->tables, m->facts);
        snprintf(w, sizeof w, "Modify's hit points, %s: least / most", k.what);
        expect(P, w, lo == k.least && hi == k.most, "%d / %d, the original's %d / %d", lo, hi, k.least, k.most);
        c.rec[0x78] = 255;
        create::modify_hp(c, m->tables, m->facts, 1);
        const bool up = c.hp_max() == k.most;
        c.rec[0x78] = 0;
        create::modify_hp(c, m->tables, m->facts, -1);
        snprintf(w, sizeof w, "Modify's Add / Subtract held to them, %s", k.what);
        expect(P, w, up && c.hp_max() == k.least, "%d", c.hp_max());
    }
}

void all_situations()
{
    if (const char* e = getenv("ENDLESS")) {
        endless_probe(e);
        return;
    }
    b_oracle_load("secret_situations");
    race_effects();
    class_tables();
    hit_points();
    creation();
    temple_checks();
    xp_award();
    camp_rest();
    modify_bounds();
}

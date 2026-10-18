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

void all_situations()
{
    if (const char* e = getenv("ENDLESS")) {
        endless_probe(e);
        return;
    }
    b_oracle_load("secret_situations");
    race_effects();
    class_tables();
}

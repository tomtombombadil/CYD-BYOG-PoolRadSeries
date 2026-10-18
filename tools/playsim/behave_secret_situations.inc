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

void all_situations()
{
    if (const char* e = getenv("ENDLESS")) {
        endless_probe(e);
        return;
    }
    race_effects();
}

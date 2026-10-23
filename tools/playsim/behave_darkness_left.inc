// Behaviour checks: Pools of Darkness - the level drain, Energy Drain in a
// fight, the fixes switch's cases, spells cast in camp (included by
// behave_darkness.inc inside namespace bdark; ONLY= drain, fixes, camp).
// Expected values from the scratch oracle oracle_darkness_left.txt
// (dk_left_oracle.py: the program's own tables and the drain's facts L1 /
// L2 worked out there) - never from the engine's code.

namespace dkleft {

const char* D_ = "darkness drain";
int d_mode = 1;
int d_die(int n) { return d_mode == 1 ? n - 1 : 0; }

int32_t get32(const uint8_t* p) { return static_cast<int32_t>(p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24); }
void put32(uint8_t* p, int32_t v)
{
    for (int j = 0; j < 4; ++j) p[j] = static_cast<uint8_t>(static_cast<uint32_t>(v) >> (8 * j));
}

// Every oracle case: the record set as it says, the drain run with the dice
// all highest or all lowest, everything it says compared
void drain_cases()
{
    static party::Character c;
    char w[200];
    int n = 0, bad = 0;
    for (const auto& r : b_rows("drain")) {
        // id, n, high, con, hp, hpmax, rolled, exp, bexp, bhp, lv 7, bl 7 | lv 7, bl 7, exp, bexp, bhp, hpmax, hp, rolled, res
        if (r.size() < 1 + 9 + 14 + 14 + 7) continue;
        const int* v = r.data() + 1;
        c = party::Character{};
        c.rec[0x74] = 7;
        for (int i = 0; i < 6; ++i) set_stat(c, i, 12);
        set_stat(c, 4, v[2]);
        c.rec[0x1A4] = static_cast<uint8_t>(v[3]);
        c.rec[0x78] = static_cast<uint8_t>(v[4]);
        c.rec[0x12C] = static_cast<uint8_t>(v[5]);
        put32(c.rec + 0x127, v[6]);
        put32(c.ext + party::kExtBestXp, v[7]);
        c.ext[party::kExtBestHp] = static_cast<uint8_t>(v[8]);
        for (int k = 0; k < 7; ++k) {
            c.rec[0x109 + k] = static_cast<uint8_t>(v[9 + k]);
            c.ext[party::kExtBestLevels + k] = static_cast<uint8_t>(v[16 + k]);
        }
        d_mode = v[1] ? 1 : 2;
        create::g_die_hook = d_die;
        const create::DrainResult res = create::dk_drain(c, m->tables, m->facts, m->dice, v[0]);
        create::g_die_hook = nullptr;
        const int* e = v + 23;
        bool ok = true;
        for (int k = 0; k < 7; ++k)
            ok = ok && c.rec[0x109 + k] == e[k] && c.ext[party::kExtBestLevels + k] == e[7 + k];
        ok = ok && get32(c.rec + 0x127) == e[14] && get32(c.ext + party::kExtBestXp) == e[15] &&
             c.ext[party::kExtBestHp] == e[16] && c.rec[0x78] == e[17] && c.rec[0x1A4] == e[18] && c.rec[0x12C] == e[19] &&
             static_cast<int>(res) == e[20];
        ++n;
        if (!ok) {
            ++bad;
            if (bad <= 12) {
                snprintf(w, sizeof w, "drain case %d", r[0]);
                expect(D_, w, false,
                       "levels %d %d %d %d %d %d %d exp %d hp %d/%d rolled %d result %d; the original's levels %d %d %d %d %d %d "
                       "%d exp %d hp %d/%d rolled %d result %d",
                       c.rec[0x109], c.rec[0x10A], c.rec[0x10B], c.rec[0x10C], c.rec[0x10D], c.rec[0x10E], c.rec[0x10F],
                       get32(c.rec + 0x127), c.rec[0x1A4], c.rec[0x78], c.rec[0x12C], static_cast<int>(res), e[0], e[1], e[2],
                       e[3], e[4], e[5], e[6], e[14], e[18], e[17], e[19], e[20]);
            }
        }
    }
    snprintf(w, sizeof w, "the level drain (facts L1 / L2): %d cases", n);
    expect(D_, w, n > 0 && bad == 0, "%d wrong", bad);
}

// Energy Drain in a fight (R3: one level, no save) on a party member and on
// a monster - its words "gets drained", the level gone; and a member drained
// to nothing: "dies from drainning", gone (status 8)
void energy_drain()
{
    using dkfight::F;
    if (!dkfight::new_fight()) {
        expect(D_, "a fight for Energy Drain", false);
        return;
    }
    combat::Battle& b = play::fg->b;
    expect(D_, "the fight drains through Darkness's own routine", combat::g_dk_drain != nullptr);
    int mon = -1;
    for (int i = 0; i < b.n && mon < 0; ++i)
        if (b.f[i].member < 0) mon = i;
    dkfight::put(0, 0, 10, 10, 60);
    dkfight::put(1, 0, 11, 10, 40);
    dkfight::levels(0, 2, 9);                  // a 9th level fighter
    dkfight::levels(1, 5, 1);                  // a 1st level magic-user
    party::Character& ch0 = play::pt->m[F(0).member];
    party::Character& ch1 = play::pt->m[F(1).member];
    memset(ch0.ext + party::kExtBestLevels, 0, 7);
    memset(ch1.ext + party::kExtBestLevels, 0, 7);
    put32(ch0.rec + 0x127, 300000);
    combat::Event ev[8];
    int nev = 0;
    combat::drain(b, 0, 1, ev, 8, &nev, -1);
    const auto* xp = row("xp", 2);              // (E(L) = the experience to go from L - 1)
    const int32_t want_xp = xp ? (*xp)[7] + ((*xp)[8] - (*xp)[7]) / 2 : -1;
    expect(D_, "Energy Drain on a 9th level fighter: 8th level, experience halfway to 9th, best level 9 kept",
           ch0.rec[0x10B] == 8 && get32(ch0.rec + 0x127) == want_xp && ch0.ext[party::kExtBestLevels + 2] == 9,
           "level %d exp %d best %d (want 8, %d, 9)", ch0.rec[0x10B], get32(ch0.rec + 0x127), ch0.ext[party::kExtBestLevels + 2],
           want_xp);
    expect(D_, "Energy Drain: \"gets drained\" said, nothing more", nev == 1 && ev[0].ev == combat::Ev::DkDrained);
    nev = 0;
    combat::drain(b, 1, 1, ev, 8, &nev, -1);
    expect(D_, "a 1st level magic-user drained: \"dies from drainning\", gone",
           nev == 2 && ev[1].ev == combat::Ev::DkDrainDies && ch1.rec[0x195] == party::Gone && ch1.rec[0x10E] == 0,
           "%d events, status %d", nev, ch1.rec[0x195]);
    if (mon >= 0) {
        dkfight::put(mon, 1, 14, 10, 30);
        dkfight::levels(mon, 2, 6);
        nev = 0;
        combat::drain(b, mon, 1, ev, 8, &nev, -1);
        expect(D_, "Energy Drain on a 6th level monster: 5th level", F(mon).rec[0x10B] == 5 && nev >= 1, "level %d", static_cast<int>(F(mon).rec[0x10B]));
    }
}

// The originals' mistakes kept with Game Fixes off, mended with it on (Tom,
// 2026-10-23): dragon fear after the fight (L4), the bulette's leap (L5)
void fixes_checks()
{
    using dkfight::F;
    char w[160];
    for (bool fix : {false, true}) {
        // Dragon fear (0x78): kept after the fight in the original
        if (!dkfight::new_fight()) {
            expect(D_, "a fight for dragon fear", false);
            return;
        }
        rules::set_fixes(fix);
        const uint8_t feared = play::fg->b.fx->dk.feared;
        dkfight::add_aff(0, feared);
        party::Character& ch = play::pt->m[F(0).member];
        play::fight_cleanup_party(combat::Won);
        snprintf(w, sizeof w, "dragon fear after the fight, Game Fixes %s: %s", fix ? "on" : "off", fix ? "gone" : "kept");
        expect(D_, w, ch.has_affect(feared) == !fix);
        // The bulette (0x63): half-attacks 4 in its second slot; 1d4 = 1 -
        // the original's leap never takes (2 attacks), mended: 8 halves (4)
        dkfight::new_fight();
        rules::set_fixes(fix);
        combat::Battle& b = play::fg->b;
        int mon = -1;
        for (int i = 0; i < b.n && mon < 0; ++i)
            if (b.f[i].member < 0) mon = i;
        if (mon < 0) {
            expect(D_, "a monster for the bulette", false);
            continue;
        }
        dkfight::put(mon, 1, 14, 10, 60);
        F(mon).rec[0x11D] = 4;
        dkfight::add_aff(mon, b.fx->dk.rake);
        int got[2] = {};
        for (int mode : {2, 1}) {
            dkfight::dice(mode);                   // (2: every die 1 - the d4 too; 1: every die its highest)
            combat::Event ev[24];
            b.round = 2;                           // (an even round: half-attacks / 2 exactly)
            combat::start_round(b, play::rng, ev, 24);
            dkfight::dice_off();
            got[mode - 1] = F(mon).attacks[1];
        }
        snprintf(w, sizeof w, "the bulette's leap, Game Fixes %s: 1d4 = 1 %d claws, else 2", fix ? "on" : "off", fix ? 4 : 2);
        expect(D_, w, got[1] == (fix ? 4 : 2) && got[0] == 2, "1d4 = 1: %d, 1d4 = 4: %d", got[1], got[0]);
    }
    rules::set_fixes(true);
}


// ---- Spells cast in camp (facts L6): every spell the camp lets through
// (its row's targets byte above 0 - GAME.OVR 0x2AF7C) does something, its
// words are its handler's, its effect goes on whom the row says for the
// row's time (or the time routine's); Raise Dead, Resurrection (both ways),
// Restoration (the oracle's cases), the cures' words, Remove Curse's slip
// both ways, Protection from Fire
party::Character& mem(int i) { return play::pt->m[i]; }
void member(int i, int cls, int level, int hp = 60)
{
    party::Character& c = mem(i);
    for (int k = 0; k < 8; ++k) c.rec[0x109 + k] = c.rec[0x111 + k] = 0;
    if (cls >= 0) c.rec[0x109 + cls] = static_cast<uint8_t>(level);
    c.rec[0x74] = 7;
    c.ext[party::kExtRace] = 5;
    c.rec[0xE6] = 0;
    c.rec[0x1A4] = c.rec[0x78] = static_cast<uint8_t>(hp);
    c.rec[0x195] = party::Okay;
    c.rec[0x196] = 1;
    c.rec[0x197] = 0;
    c.n_affects = 0;
    for (int k = 0; k < 6; ++k) c.rec[0x10 + 2 * k] = c.rec[0x11 + 2 * k] = 12;
    c.rec[0x1C] = c.rec[0x1D] = 0;
    memset(c.ext + party::kExtBestLevels, 0, 7);
}
spells::Line cl[24];
int cn = 0;
int ccast(int caster, int target, int s)
{
    using namespace play;
    cn = 0;
    const spells::CampSpell* cs = camp_spell(s);
    if (!cs) return -1;
    cn = spells::cast(*pt, caster, target, *cs, mrules->tables, d->prof->cures, d->prof->magic.facts, rng, cl, 24, 0, 1);
    return cn;
}
int csaid(int who, spells::Said w)
{
    int n = 0;
    for (int k = 0; k < cn; ++k) n += cl[k].who == who && cl[k].what == w;
    return n;
}
int caff(int i, int type, int* minutes = nullptr)
{
    const party::Character& c = mem(i);
    for (int k = 0; k < c.n_affects; ++k)
        if (c.affects[k][0] == type) {
            if (minutes) *minutes = c.affects[k][1] | c.affects[k][2] << 8;
            return k;
        }
    return -1;
}
std::string word_text(uint32_t at)
{
    char t[64];
    play::ow(at, t, sizeof t);
    return t;
}
bool handler_says(int s, const std::string& t)
{
    for (const auto& r : b_rows("campword"))
        if (r.size() > 2 && r[0] == s) {
            std::string h;
            for (size_t k = 2; k < r.size(); ++k) h += static_cast<char>(r[k]);
            if (h == t) return true;
        }
    return false;
}
int fx(int k) { const auto& r = b_rows("campfx"); return r.empty() || k >= (int)r[0].size() ? 0 : r[0][k]; }
// The time the oracle's rules give: the time routine's rolled ones (dice highest), else row + per level
int want_time(int s, int t, int tl, int lv)
{
    for (const auto& r : b_rows("camptimed"))
        if (r[0] == s) return (r[1] * r[2] + r[3]) * r[4];
    return t + tl * lv;
}

void camp_checks()
{
    using namespace play;
    char w[200];
    if (!dkfight::dk_boot() || pt->count < 3 || !load_magic()) {
        expect(D_, "a party in camp to cast with", false);
        return;
    }
    // (a) every spell the camp lets through has its workings; (b) the words
    int none = 0, words_bad = 0, n = 0;
    std::string first;
    for (const auto& r : b_rows("camprow")) {
        const int sp = r[0], tg = r[3];
        if (tg == 0) continue;
        ++n;
        const spells::CampSpell* cs = camp_spell(sp);
        if (!cs || cs->does == spells::Does::NotYet) {
            ++none;
            if (first.empty()) first = "no workings: " + std::to_string(sp);
            continue;
        }
        for (uint32_t at : {cs->word, cs->word2, cs->word3, cs->word4}) {
            if (!at) continue;
            const std::string t = word_text(at);
            if (!handler_says(sp, t)) {
                ++words_bad;
                if (first.empty()) first = "spell " + std::to_string(sp) + " says \"" + t + "\"";
            }
        }
    }
    snprintf(w, sizeof w, "camp: the %d spells the camp lets through all do something, with their handlers' words", n);
    expect(D_, w, n > 50 && none == 0 && words_bad == 0, "%d without, %d words not the handler's; %s", none, words_bad, first.c_str());
    // (c) the plain effects: on whom the row says, the effect, its time
    int bad = 0;
    first.clear();
    for (const auto& r : b_rows("camprow")) {
        const int sp = r[0], cls = r[1], tg = r[3], eff = r[4], t = r[6], tl = r[7];
        const spells::CampSpell* cs = camp_spell(sp);
        if (tg == 0 || !cs || cs->does != spells::Does::Affect || !eff || cls == 1) continue;
        for (int i = 0; i < pt->count; ++i) member(i, classes::Fighter, 5);
        member(0, cls == 0 ? classes::Cleric : classes::MagicUser, 14);
        dkfight::dice(1);
        ccast(0, 1, sp);
        dkfight::dice_off();
        const int lv = cls == 3 ? 12 : 14;
        const int want = want_time(sp, t, tl, lv);
        for (int i = 0; i < pt->count; ++i) {
            const bool should = tg == 3 || (tg == 1 && i == 0) || (tg == 2 && i == 1);
            int got = -1;
            const bool has = caff(i, eff, &got) >= 0;
            if (has != should || (has && got != want)) {
                ++bad;
                if (first.empty())
                    first = "spell " + std::to_string(sp) + " member " + std::to_string(i) + " time " + std::to_string(got) +
                            " want " + std::to_string(should ? want : -1);
            }
        }
    }
    expect(D_, "camp: the effects' spells - on whom, the effect, its time (dice highest)", bad == 0, "%d wrong; %s", bad,
           first.c_str());

    // Raise Dead (GAME.OVR 0x2DF05): dead, Con 12 -> 11, okay with 1 HP
    member(0, classes::Cleric, 14);
    member(1, classes::Fighter, 8, 70);
    mem(1).rec[0x195] = party::Dead;
    mem(1).rec[0x196] = 0;
    mem(1).rec[0x1A4] = 0;
    spells::add_affect(mem(1), fx(3), 0, 0, false);
    ccast(0, 1, 0x4B);
    expect(D_, "camp: Raise Dead - okay, 1 HP, Con 11, the poison gone, \"is raised\"",
           mem(1).health() == party::Okay && mem(1).hp() == 1 && mem(1).rec[0x18] == 11 && caff(1, fx(3)) < 0 &&
               csaid(1, spells::Said::Raised) == 1,
           "status %d hp %d con %d", mem(1).health(), mem(1).hp(), mem(1).rec[0x18]);
    // Resurrection (0x2EF05): dead only, all hit points; Con kept - AD&D's -1 with Game Fixes
    for (bool fix : {false, true}) {
        rules::set_fixes(fix);
        member(1, classes::Fighter, 8, 70);
        mem(1).rec[0x195] = party::Dead;
        mem(1).rec[0x196] = 0;
        mem(1).rec[0x1A4] = 0;
        ccast(0, 1, 0x69);
        const int con = mem(1).rec[0x18];
        snprintf(w, sizeof w, "camp: Resurrection (Game Fixes %s) - okay with all hit points, Con %d", fix ? "on" : "off", fix ? 11 : 12);
        expect(D_, w, mem(1).health() == party::Okay && mem(1).hp() == mem(1).hp_max() && con == (fix ? 11 : 12) &&
                          csaid(1, spells::Said::Word) == 1,
               "status %d hp %d/%d con %d", mem(1).health(), mem(1).hp(), mem(1).hp_max(), con);
        member(1, classes::Fighter, 8, 70);
        mem(1).rec[0x195] = party::Unconscious;
        ccast(0, 1, 0x69);
        snprintf(w, sizeof w, "camp: Resurrection (Game Fixes %s) on one not dead: nothing", fix ? "on" : "off");
        expect(D_, w, mem(1).health() == party::Unconscious && cn == 0);
    }
    rules::set_fixes(true);
    // Remove Curse: the original looks for effect 0x2B; Game Fixes the curse 0x24
    for (bool fix : {false, true}) {
        rules::set_fixes(fix);
        member(1, classes::Fighter, 8);
        spells::add_affect(mem(1), static_cast<uint8_t>(fx(0)), 0, 0, false);
        spells::add_affect(mem(1), static_cast<uint8_t>(fx(1)), 0, 0, false);
        ccast(0, 1, 0x2B);
        const bool gone2b = caff(1, fx(0)) < 0, gone24 = caff(1, fx(1)) < 0;
        snprintf(w, sizeof w, "camp: Remove Curse (Game Fixes %s) takes away %s, \"is Cured\", \"is un-cursed\"", fix ? "on" : "off",
                 fix ? "the curse (0x24)" : "0x2B, not the curse");
        expect(D_, w, gone2b == !fix && gone24 == fix && csaid(1, spells::Said::Cured) == 1 && csaid(1, spells::Said::Uncursed) == 1);
    }
    rules::set_fixes(true);
    // Cure Disease: "is Cured" for each disease it takes, then "is cured"
    member(1, classes::Fighter, 8);
    for (uint8_t e : {0x22, 0x2B, 0x2C, 0x1F}) spells::add_affect(mem(1), e, 0, 0, false);
    ccast(0, 1, 0x27);
    expect(D_, "camp: Cure Disease - both diseases (and their course) gone, \"is Cured\" twice, then \"is cured\"",
           mem(1).n_affects == 0 && csaid(1, spells::Said::Cured) == 2 && csaid(1, spells::Said::Word) == 1 && cn == 3 &&
               cl[2].what == spells::Said::Word,
           "%d effects left, %d lines", mem(1).n_affects, cn);
    // Cure Blindness: "is Cured", "can see"
    member(1, classes::Fighter, 8);
    spells::add_affect(mem(1), 0x21, 0, 0, false);
    ccast(0, 1, 0x25);
    expect(D_, "camp: Cure Blindness - \"is Cured\", \"can see\"", mem(1).n_affects == 0 && cn == 2 &&
                                                                     cl[0].what == spells::Said::Cured && cl[1].what == spells::Said::CanSee);
    // Protection from Fire: a ranger its own effect for the caster's level, others Resist Fire's
    member(0, classes::Ranger, 12);
    member(1, classes::Fighter, 8);
    ccast(0, 0, 0x6D);
    const auto* pr = b_row("camprow", 0x6D);
    expect(D_, "camp: Protection from Fire on a ranger - its own effect", pr && caff(0, (*pr)[4]) >= 0 && caff(0, fx(2)) < 0);
    ccast(0, 1, 0x6D);
    expect(D_, "camp: Protection from Fire on a fighter - Resist Fire's effect", pr && caff(1, (*pr)[4]) < 0 && caff(1, fx(2)) >= 0);
    // Restoration: the oracle's cases (R11 worked out there)
    int rn = 0, rbad = 0;
    for (const auto& r : b_rows("restore")) {
        if (r.size() < 1 + 7 + 14 + 7 + 4) continue;
        const int* v = r.data() + 1;
        party::Character& c = mem(1);
        member(1, -1, 0);
        set_stat(c, 4, v[0]);
        c.rec[0x1A4] = static_cast<uint8_t>(v[1]);
        c.rec[0x78] = static_cast<uint8_t>(v[2]);
        c.ext[party::kExtBestHp] = static_cast<uint8_t>(v[3]);
        c.rec[0x12C] = static_cast<uint8_t>(v[4]);
        put32(c.rec + 0x127, v[5]);
        put32(c.ext + party::kExtBestXp, v[6]);
        for (int k = 0; k < 7; ++k) {
            c.rec[0x109 + k] = static_cast<uint8_t>(v[7 + k]);
            c.ext[party::kExtBestLevels + k] = static_cast<uint8_t>(v[14 + k]);
        }
        create::dk_restore(c, mrules->tables);
        const int* e = v + 21;
        bool ok = true;
        for (int k = 0; k < 7; ++k) ok = ok && c.rec[0x109 + k] == e[k];
        ok = ok && c.rec[0x1A4] == e[7] && c.rec[0x78] == e[8] && c.rec[0x12C] == e[9] && get32(c.rec + 0x127) == e[10];
        ++rn;
        if (!ok && ++rbad <= 8)
            expect(D_, "Restoration case", false, "case %d: hp %d/%d rolled %d exp %d; the original's %d/%d %d %d", r[0], c.rec[0x1A4],
                   c.rec[0x78], c.rec[0x12C], get32(c.rec + 0x127), e[7], e[8], e[9], e[10]);
    }
    snprintf(w, sizeof w, "camp: Restoration (R11): %d cases", rn);
    expect(D_, w, rn > 0 && rbad == 0, "%d wrong", rbad);
}


// ---- The menus' highlight (facts L8; darkness_alter_facts.md A1): the
// camp's, Magic's and View's own places, Rest's first word, a menu with no
// place of its own starting where the last one was left, the camp's "Lose
// it?" on Yes, the party menu's Drop through "Pick Character Select"
int word_at(char k)
{
    for (int i = 0; i < play::menu.count; ++i)
        if (text::key(play::menu, i) == k) return i;
    return -1;
}
void menu_checks()
{
    using namespace play;
    char w[160];
    if (!dkfight::dk_boot() || screen != Screen::Game) {
        expect(D_, "exploring, for the menus", false);
        return;
    }
    tap_word('E');                                  // Encamp
    if (screen != Screen::Camp) {
        expect(D_, "the camp, for the menus", false, "screen %d", (int)screen);
        return;
    }
    expect(D_, "menus: the camp opens on its first word", menu.selected == 0, "%d", menu.selected);
    const int magic_at = word_at('M'), view_at = word_at('V'), rest_at = word_at('R');
    tap_word('M');
    expect(D_, "menus: Magic opens on its first word (its own place starts there)", screen == Screen::Magic && menu.selected == 0,
           "screen %d, %d", (int)screen, menu.selected);
    tap_word('E');
    snprintf(w, sizeof w, "menus: back in camp, Magic lit (the camp's own place - word %d)", magic_at);
    expect(D_, w, screen == Screen::Camp && menu.selected == magic_at, "%d", menu.selected);
    if (rest_at >= 0) {
        tap_word('R');
        expect(D_, "menus: Rest opens on its first word", screen == Screen::Rest && menu.selected == 0, "%d", menu.selected);
        tap_word('E');
        expect(D_, "menus: back in camp, Rest lit", screen == Screen::Camp && menu.selected == rest_at, "%d", menu.selected);
    }
    tap_word('V');
    expect(D_, "menus: View opens on its first word", screen == Screen::View && menu.selected == 0, "%d", menu.selected);
    tap_word('E');
    expect(D_, "menus: back in camp, View lit", screen == Screen::Camp && menu.selected == view_at, "%d", menu.selected);
    // A menu with no place of its own (the save slots: GAME.OVR 0x131D2) starts at the last one's
    dk_place = 3;
    ask_save(C);
    expect(D_, "menus: the save slots start where the last menu was left (D)", menu.selected == 3, "%d", menu.selected);
    dk_place = 40;
    ask_save(C);
    expect(D_, "menus: past the menu's end - the first word", menu.selected == 0, "%d", menu.selected);
    // The camp's "Lose it?" lights Yes (GAME.OVR 0x2AFCB)
    ask_yes_no(C, Ask::LoseIt, "Lose it? ");
    expect(D_, "menus: \"Lose it?\" on Yes", text::key(menu, menu.selected) == 'Y', "%d", menu.selected);
    // The party menu's Drop: "Pick Character Select" first
    if (open_game()) {
        play::tap(24, (12 + pm_line('L')) * 8 + 2, C);
        if (play::load_where) tap_item(0);
        const char* p = strchr(play::pm_saves, 'A');
        if (p) tap_item(static_cast<int>(p - play::pm_saves));
        const int dl = pm_line('D');
        if (dl >= 0) {
            play::tap(24, (12 + dl) * 8 + 2, C);
            const bool pick = strncmp(menu.prompt, "Pick", 4) == 0 || strncmp(menu.prompt, "PICK", 4) == 0;
            expect(D_, "menus: the party menu's Drop asks \"Pick Character Select\" first", pick && word_at('S') >= 0 &&
                                                                                                 screen == Screen::PartyMenu,
                   "prompt \"%s\"", menu.prompt);
            play::tap(10, (4 + 1) * 8 + 2, C);       // the second member
            tap_word('S');
            char nm[20];
            pt->m[1].name(nm, sizeof nm);
            expect(D_, "menus: ... then \"Drop NAME forever?\" for the one picked", screen == Screen::YesNo && strstr(menu.prompt, nm),
                   "prompt \"%s\"", menu.prompt);
            tap_word('N');
        } else {
            expect(D_, "menus: the party menu's Drop", false, "no Drop line");
        }
    }
}

void checks(bool (*want)(const char*))
{
    if (want("drain")) {
        drain_cases();
        energy_drain();
    }
    if (want("fixes")) fixes_checks();
    if (want("camp")) camp_checks();
    if (want("menus")) menu_checks();
}

} // namespace dkleft

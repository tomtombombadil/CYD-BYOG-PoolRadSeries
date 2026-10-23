// Behaviour checks: Pools of Darkness - fights (included by
// behave_darkness.inc inside namespace bdark; ONLY=fights or a part's name:
// turn, words, spells, abilities, blows, xp, breaths). Expected values from
// the program's own tables and the fights' facts (the Project's
// claude/darkness_fight_facts.md and its rules part) through the scratch
// oracle oracle_darkness_fights.txt (dk_fight_oracle.py) - never from the
// engine's code. The rules each check follows are written beside it.

namespace dkfight {

const char* F_ = "darkness fights";
char w[200];

// ---- Dice: d20 set, the others highest (1) or lowest (2)
int f_d20 = 0, f_mode = 1;
int f_die(int n)
{
    if (n == 20 && f_d20) return f_d20 - 1;
    return f_mode == 1 ? n - 1 : 0;
}
void dice(int mode, int d20 = 0)
{
    f_mode = mode;
    f_d20 = d20;
    create::g_die_hook = f_die;
}
void dice_off() { create::g_die_hook = nullptr; }

// ---- The 30-monster fight (save A, 85BB), its field cleared to floor and
// its fighters taken off it; checks put plain creatures where they want
combat::Fighter& F(int i) { return play::fg->b.f[i]; }
// Save A loaded and Begin, the tour's questions answered with their first
// word (as the playsim's CHOICES=0) until the script rests
bool dk_boot()
{
    if (!open_game()) return printf("  (dk_boot: open)\n"), false;
    play::tap(24, (12 + pm_line('L')) * 8 + 2, C);
    if (play::load_where)               // ("Load from where?": Pools)
        play::tap(((int)strlen(play::menu.prompt) + play::menu.start[0]) * 8 + 2, text::kMenuRow * 8 + 2, C);
    const char* p = strchr(play::pm_saves, 'A');
    if (!p) return printf("  (dk_boot: no save A [%s])\n", play::pm_saves), false;
    tap_item(static_cast<int>(p - play::pm_saves));
    if (!play::pt->count) return printf("  (dk_boot: no party)\n"), false;
    play::tap(24, (12 + pm_line('B')) * 8 + 2, C);
    for (int it = 0; it < 20000; ++it) {
        g_now += 20;
        play::tick(g_now, C);
        if (play::screen != play::Screen::Game) return printf("  (dk_boot: screen %d)\n", (int)play::screen), false;
        if (!play::waiting) {
            if (play::then == play::Then::Idle) return true;
            continue;
        }
        if (it % 50) continue;
        if (play::vm->wait() == ecl::Wait::Menu) tap_item(0);
        else if (play::vm->wait() == ecl::Wait::Print && play::page_prompt) play::tap(10, 10, C);
        else if (play::vm->wait() == ecl::Wait::Key) play::tap(10, 10, C);
    }
    return true;
}
bool new_fight()
{
    dice_off();
    if (!dk_boot()) return printf("  (new_fight: boot)\n"), false;
    if (!start_fight()) return printf("  (new_fight: no fight, screen %d)\n", (int)play::screen), false;
    if (!play::load_magic() || !play::fg || play::fg->b.n < 12) return printf("  (new_fight: magic / fighters)\n"), false;
    combat::Battle& b = play::fg->b;
    const int floor = b.ground[b.f[0].y][b.f[0].x];
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = static_cast<uint8_t>(floor);
    for (int i = 0; i < b.n; ++i) b.f[i].size = 0;
    b.aim_x = b.aim_y = -1;
    b.turn_minus = 0;
    combat::occupancy(b);
    return true;
}
// A plain creature: team, HP, Hit Dice, saves 15, no effects, no undead
void put(int i, int team, int x, int y, int hp, int hd = 1)
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
    if (f.n_aff) *f.n_aff = 0;
    f.rec[0xE5] = static_cast<uint8_t>(hd);
    f.rec[0xE9] = 0;
    f.rec[0x19A] = f.rec[0x19B] = 50;
    f.flags2 = 0;
    f.magic_res = 0;
    f.fleeing = false;
    f.turned_undead = false;
    f.undead_turns = 0;
    combat::occupancy(play::fg->b);
}
void levels(int i, int cls, int level)
{
    combat::Fighter& f = F(i);
    for (int k = 0; k < 8; ++k) f.rec[0x109 + k] = f.rec[0x111 + k] = 0;
    if (cls >= 0) f.rec[0x109 + cls] = static_cast<uint8_t>(level);
    f.rec[0x74] = 7;
    f.rec[0xE6] = 0;
}

// ---- Turn Undead (R13): a reference from the facts and the oracle's table
// - the cleric's level or the paladin's - 2, 9-13 -> 9, 14+ -> 10, less the
// penalty; the highest undead type up to the limit; its value sets the count
// (D* 1d6 + 6, else 1d12 - the dice as set); the roll at least |value|:
// turned (> 0) or destroyed; too low: the limit - 1
struct TurnCase { int cl, pal, minus, roll, mode; std::vector<int> types; };
std::vector<int> turn_ref(const TurnCase& tc)
{
    const int lv = std::max(tc.cl, tc.pal - 2);
    int col = lv <= 8 ? lv : lv < 14 ? 9 : 10;
    col = tc.minus > col ? 0 : col - tc.minus;
    std::vector<int> out;
    std::vector<bool> gone(tc.types.size());
    int count = -1, extra = 6, limit = 13;
    for (;;) {
        int best = -1, type = 0;
        for (size_t k = 0; k < tc.types.size(); ++k)
            if (!gone[k] && tc.types[k] > type && tc.types[k] <= limit) {
                best = static_cast<int>(k);
                type = tc.types[k];
            }
        if (best < 0 || count == 0) break;
        const auto* row = b_row("turn", type);
        const int v = row ? (*row)[1 + col] : 99;
        if (count < 0) count = v < 0 ? (tc.mode == 1 ? 6 : 1) + 6 : (tc.mode == 1 ? 12 : 1);
        if (tc.roll < std::abs(v)) {
            if (--limit < 1) break;
            continue;
        }
        gone[best] = true;
        out.push_back(v > 0 ? best : best + 1000);
        if (extra > 0) --extra;
        if (--count == 0 && extra > 0 && v < 0) ++count;
    }
    return out;
}
void turn_checks()
{
    if (!new_fight()) {
        expect(F_, "a fight to turn in", false);
        return;
    }
    const TurnCase cases[] = {
        {5, 0, 0, 1, 1, {3}},              // automatic (1)
        {1, 0, 0, 9, 1, {1}},              // 10 needed: too low
        {1, 0, 0, 10, 1, {1}},             // just enough
        {7, 0, 0, 5, 1, {2, 6}},           // the highest type first, then the weaker (destroyed)
        {7, 0, 0, 3, 1, {2, 6}},           // 6 needs 4: too low - the limit down, 2 destroyed
        {0, 9, 0, 3, 1, {2, 6}},           // a paladin of 9 turns as a cleric of 7
        {7, 0, 2, 5, 1, {2, 6}},           // the area's penalty (block +0x0E) 2: as a 5th level cleric
        {12, 0, 0, 5, 2, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},   // D*: 1d6 + 6 (lowest: 7) and the extras
        {12, 0, 0, 5, 1, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}},   // D* (highest: 12)
        {14, 0, 0, 13, 1, {13, 9, 12}},    // 14+: the last column
        {3, 0, 3, 20, 1, {1}},             // a penalty to column 0: never
    };
    for (const TurnCase& tc : cases) {
        new_fight();
        const int C = 0;
        put(C, 0, 10, 10, 50);
        levels(C, -1, 0);
        F(C).rec[0x109] = static_cast<uint8_t>(tc.cl);
        F(C).rec[0x10C] = static_cast<uint8_t>(tc.pal);
        play::fg->b.turn_minus = tc.minus;
        const int first = play::fg->b.party_size;
        for (size_t k = 0; k < tc.types.size(); ++k) {
            const int m = first + static_cast<int>(k);
            put(m, 1, 12 + static_cast<int>(k % 5), 10 + static_cast<int>(k / 5), 30, 3);
            F(m).rec[0xE9] = static_cast<uint8_t>(tc.types[k]);
        }
        dice(tc.mode, tc.roll);
        int out[16];
        const int n = combat::turn_undead(play::fg->b, play::fg->t, C, play::rng, out, 16);
        dice_off();
        std::vector<int> got;
        for (int k = 0; k < n; ++k) got.push_back(out[k] >= 1000 ? out[k] - 1000 - first + 1000 : out[k] - first);
        const std::vector<int> want = turn_ref(tc);
        std::string gs, ws;
        for (int v : got) gs += " " + std::to_string(v);
        for (int v : want) ws += " " + std::to_string(v);
        snprintf(w, sizeof w, "Turn Undead: cleric %d paladin %d penalty %d, d20 %d, dice %s, %zu undead", tc.cl, tc.pal,
                 tc.minus, tc.roll, tc.mode == 1 ? "high" : "low", tc.types.size());
        expect(F_, w, got == want, "turned / destroyed (1000+):%s, the original's%s", gs.c_str(), ws.c_str());
    }
}

// ---- The new spells (R2-R10)
combat::SpellLine L[24];
int nl = 0;
int cast(int caster, int s, std::initializer_list<int> tg, int pw = 0)
{
    const auto& fl = play::d->prof->fight;
    const combat::FightSpell* fs = combat::fight_spell(fl.spells, fl.n_spells, s);
    if (!fs) return -1;
    int t[24], n = 0;
    for (int i : tg) t[n++] = i;
    nl = combat::cast(play::fg->b, play::mrules->tables, caster, s, *fs, t, n, play::rng, L, 24, pw);
    return nl;
}
int said(int who, combat::Did did)
{
    int n = 0;
    for (int k = 0; k < nl; ++k) n += L[k].who == who && L[k].did == did;
    return n;
}
int dmg(int who)
{
    int n = 0;
    for (int k = 0; k < nl; ++k)
        if (L[k].who == who && L[k].did == combat::Did::Damage) n += L[k].amount;
    return n;
}
int aff_time(int i, int type)
{
    const combat::Fighter& f = F(i);
    for (int k = 0; f.n_aff && k < *f.n_aff; ++k)
        if (f.aff[k][0] == type) return f.aff[k][1] | f.aff[k][2] << 8;
    return -1;
}
const std::vector<int>* fact(const char* key) { return b_row(key, b_rows(key).empty() ? 0 : b_rows(key)[0][0]); }
int fv(const char* key, int i)
{
    const auto& r = b_rows(key);
    return r.empty() || i >= static_cast<int>(r[0].size()) ? -999 : r[0][i];
}

void spell_checks()
{
    if (!new_fight()) {
        expect(F_, "a fight to cast in", false);
        return;
    }
#define BT (play::fg->b)
    const combat::Facts& fx = *BT.fx;
    const int C = 0, A = BT.party_size, B = A + 1, D = A + 2, E = A + 3;
    auto reset = [&]() {
        new_fight();
        put(C, 0, 10, 10, 60);
        levels(C, classes::MagicUser, 18);
        F(C).rec[0x10E] = 18;
        F(C).rec[0x109] = 18;
        BT.f[C].rec[0x199] = 20;                     // (the touch spells' blows: a good to-hit)
    };

    // Blade Barrier (R2): the knives 3 rounds a level; at the cast a save
    // against spells or 8d8 (no half); later no save
    for (int d20 : {20, 2}) {
        reset();
        put(A, 1, 12, 10, 100);
        const int lv = combat::power_of(F(C).rec, play::mrules->tables, 0x65, false);
        combat::lay_cloud(BT, play::fg->t, 12, 10, 3 * lv, false, true);
        dice(1, d20);
        cast(C, 0x65, {A});
        const int want = d20 >= 15 ? 0 : fv("bb", 0) * fv("bb", 1);
        snprintf(w, sizeof w, "Blade Barrier at the cast, d20 %d against a save of 15", d20);
        expect(F_, w, dmg(A) == want && (want == 0) == (said(A, combat::Did::Word) == 0), "damage %d, the original's %d", dmg(A), want);
        bool dn = false;
        dice(1, 20);
        const int cut = combat::knives_cut(BT, A, play::rng, &dn);
        snprintf(w, sizeof w, "Blade Barrier's round's end, d20 %d: no save", 20);
        expect(F_, w, cut == fv("bb", 0) * fv("bb", 1), "%d", cut);
        int rounds = -1;
        for (int k = 0; k < BT.n_clouds; ++k)
            if (BT.clouds[k].knives) rounds = BT.clouds[k].rounds;
        expect(F_, "Blade Barrier lasts 3 rounds a caster level", rounds == fv("bb", 2) * lv, "%d rounds, level %d", rounds, lv);
        dice_off();
    }

    // Destruction (R4): the unliving unaffected; else gone, no save
    for (int unliving : {0, 1}) {
        reset();
        put(A, 1, 11, 10, 100);
        F(A).flags2 = static_cast<uint8_t>(unliving ? 8 : 0);
        dice(1, 20);
        cast(C, 0x68, {A});
        dice_off();
        snprintf(w, sizeof w, "Destruction on a %s creature (a d20 of 20: no save)", unliving ? "unliving" : "living");
        const bool ok = unliving ? F(A).status() == party::Okay && said(A, combat::Did::Unaffected)
                                 : F(A).status() == party::Gone && said(A, combat::Did::Word);
        expect(F_, w, ok, "status %d", F(A).status());
    }

    // Mass Charm (R5): Hit Dice against 2 x level; persons, not large, of the
    // other side; a save at -2
    for (int d20 : {16, 17}) {
        reset();
        levels(C, classes::MagicUser, 10);
        put(A, 1, 12, 10, 40, 8);
        put(B, 1, 13, 10, 40, 8);
        put(D, 1, 14, 10, 40, 8);
        put(E, 1, 15, 10, 40, 2);
        for (int i : {A, B, D, E}) F(i).rec[0xDE] = 1;
        for (int i : {A, B, D}) F(i).flags2 = 2;
        dice(1, d20);
        cast(C, 0x78, {A, B, E, D});
        dice_off();
        const bool fails = d20 - 2 < 15;
        const bool ok = (F(A).team() == 0) == fails && (F(B).team() == 0) == fails && F(D).team() == 1 &&
                        said(E, combat::Did::Word2) == 1 && said(D, combat::Did::Word) == 0;
        snprintf(w, sizeof w, "Mass Charm, level 10 (20 Hit Dice), d20 %d: 8 + 8 charmed or saved, a non-person unaffected, "
                              "the third 8 past the count", d20);
        expect(F_, w, ok, "sides %d %d %d (lines %d: A word %d word2 %d unaffected %d; size %d, power %d)", F(A).team(), F(B).team(),
               F(D).team(), nl, said(A, combat::Did::Word), said(A, combat::Did::Word2), said(A, combat::Did::Unaffected),
               (int)F(A).rec[0xDE], combat::power_of(F(C).rec, play::mrules->tables, 0x78, false));
    }

    // Otto's (R6): a blow, then 1d4 + 1 rounds lost and 1d4 + 1 dancing
    for (int d20 : {20, 1}) {
        reset();
        put(A, 1, 11, 10, 40);
        dice(1, d20);
        cast(C, 0x79, {A});
        dice_off();
        const int want = d20 == 1 ? -1 : 4 + 1;
        snprintf(w, sizeof w, "Otto's Irresistible Dance, d20 %d", d20);
        expect(F_, w, aff_time(A, fx.fumbling) == want && aff_time(A, fx.dk.dancing) == want, "%d / %d, the original's %d",
               aff_time(A, fx.fumbling), aff_time(A, fx.dk.dancing), want);
    }

    // Power Word Blind (R7): HP max 100 at most in all; the time - the
    // original's the row's (0: for good), the rules fixes' 1d4 + 1 (x 10 under 51)
    for (bool fix : {false, true}) {
        reset();
        const bool was = rules::fixes();
        rules::set_fixes(fix);
        put(A, 1, 12, 10, 40);
        put(B, 1, 13, 10, 50);
        put(D, 1, 14, 10, 30);
        dice(1, 10);
        cast(C, 0x7B, {A, B, D});
        dice_off();
        rules::set_fixes(was);
        const auto* row = b_row("sprow", 0x7B);
        const int orig = row ? (*row)[5] + (*row)[6] * combat::power_of(F(C).rec, play::mrules->tables, 0x7B, false) : -2;
        const int want = fix ? 5 : orig;
        snprintf(w, sizeof w, "Power Word Blind on HP max 40, 50, 30 (rules fixes %s)", fix ? "on" : "off");
        expect(F_, w, aff_time(A, fx.blinded) == want && aff_time(B, fx.blinded) == want && aff_time(D, fx.blinded) == -1,
               "%d %d %d, the original's %d", aff_time(A, fx.blinded), aff_time(B, fx.blinded), aff_time(D, fx.blinded), want);
    }

    // Power Word Kill (R9): the original (rules fixes off) by HP max
    {
        const bool was = rules::fixes();
        rules::set_fixes(false);
        reset();
        rules::set_fixes(false);
        put(A, 1, 12, 10, 50);
        put(B, 1, 13, 10, 8);
        cast(C, 0x7D, {A, B});
        expect(F_, "Power Word Kill: the first of 11-60 HP max alone", F(A).status() == party::Dead && F(B).up(),
               "%d %d", F(A).status(), F(B).status());
        reset();
        rules::set_fixes(false);
        put(A, 1, 12, 10, 70);
        put(B, 1, 13, 10, 8);
        put(D, 1, 14, 10, 10);
        put(E, 1, 15, 10, 11);
        cast(C, 0x7D, {A, B, D, E});
        expect(F_, "Power Word Kill: the first over 60: all of 10 or less", F(A).up() && !F(B).up() && !F(D).up() && F(E).up(),
               "%d %d %d %d", F(A).status(), F(B).status(), F(D).status(), F(E).status());
        // HP max 70 with 50 left: by HP max the first is over 60
        reset();
        rules::set_fixes(false);
        put(A, 1, 12, 10, 70);
        F(A).rec[0x1A4] = 50;
        put(B, 1, 13, 10, 8);
        cast(C, 0x7D, {A, B});
        expect(F_, "Power Word Kill (fixes off): HP max 70, 50 left - the 8 slain, not it", F(A).up() && !F(B).up(),
               "%d %d", F(A).status(), F(B).status());
        rules::set_fixes(was);
    }
    // ... and AD&D's with the rules fixes (Tom, 2026-10-23): by hit points
    // now - one of 11-60 alone, else those of 10 or less up to 120 in all
    // (the oracle's pwk_fix: 10 60 10 120)
    {
        const bool was = rules::fixes();
        rules::set_fixes(true);
        reset();
        rules::set_fixes(true);
        put(A, 1, 12, 10, 70);
        F(A).rec[0x1A4] = 50;
        put(B, 1, 13, 10, 8);
        cast(C, 0x7D, {A, B});
        expect(F_, "Power Word Kill (fixes on): 50 hit points now - slain alone", !F(A).up() && F(B).up(), "%d %d",
               F(A).status(), F(B).status());
        reset();
        rules::set_fixes(true);
        const int lim = fv("pwk_fix", 3), each = fv("pwk_fix", 2);
        std::vector<int> tg;
        int want_dead = 0, total = 0;
        for (int k = 0; k < 13 && A + k < BT.n; ++k) {
            put(A + k, 1, 12 + k % 8, 8 + k / 8, k == 0 ? 5 : each);
            tg.push_back(A + k);
            const int h = k == 0 ? 5 : each;
            if (total + h <= lim) {
                total += h;
                ++want_dead;
            }
        }
        int t[24], n = 0;
        for (int i : tg) t[n++] = i;
        const auto& fl = play::d->prof->fight;
        nl = combat::cast(BT, play::mrules->tables, C, 0x7D, *combat::fight_spell(fl.spells, fl.n_spells, 0x7D), t, n, play::rng, L,
                          24, 0);
        int dead = 0;
        for (int i : tg) dead += !F(i).up();
        snprintf(w, sizeof w, "Power Word Kill (fixes on): %d of 5 and 10 hit points - %d slain (120 in all)", n, want_dead);
        expect(F_, w, dead == want_dead, "%d slain", dead);
        rules::set_fixes(was);
    }

    // Meteor Swarm (R8): four meteors on the diagonals of the square aimed
    // at; struck: 10d4 no save; the burst round it: a save for half
    {
        reset();
        put(A, 1, 19, 9, 200);                     // on a meteor's square (aim 20,10: NW)
        put(B, 1, 20, 10, 200);                    // the square aimed at: in all four bursts
        dice(1, 20);
        play::fg->b.aim_x = 20;
        play::fg->b.aim_y = 10;
        cast(C, 0x7C, {B});
        play::fg->b.aim_x = play::fg->b.aim_y = -1;
        dice_off();
        const int full = fv("meteor", 1) * fv("meteor", 2);
        bool spots = BT.n_meteors == 4;
        for (int k = 0; k < 4 && spots; ++k)
            spots = BT.meteor_x[k] == 20 + fv("meteor_dx", k) && BT.meteor_y[k] == 10 + fv("meteor_dy", k);
        expect(F_, "Meteor Swarm: four meteors on the square's diagonals", spots, "%d", BT.n_meteors);
        // (A: struck by the first meteor, full; then within 1 of none of the others' landings but...)
        expect(F_, "Meteor Swarm: struck (no save) / in a burst (saved: half)", dmg(B) == 4 * (full / 2) && dmg(A) >= full,
               "B %d (the original's %d), A %d", dmg(B), 4 * (full / 2), dmg(A));
    }

    // Monster Summoning (R10): the fight's reserve, 2 a cast, on the caster's side
    {
        new_fight();
        const MonsterLoad* m = play::ml;
        const int style = play::vm->get(0x3A) & 0xFF;
        const auto* rs = b_row("reserve", fv("reserve", 0));
        const int want_id = style <= 4 ? fv("reserve", style) : -1;
        const bool loaded = m && m->reserve_from >= 0 && m->n - m->reserve_from == 10 &&
                            m->g[m->reserve_group]->id == want_id;
        (void)rs;
        expect(F_, "the summoning reserve: 10 of the frame style's MON block, out of the fight", loaded, "style %d, block %d",
               style, m && m->reserve_group >= 0 ? m->g[m->reserve_group]->id : -1);
        // (the facts' plain ground: 0x17 in the 3D view's areas, else 0x45)
        const int plain = (play::vm->get(0x22) & 0xFF) ? 0x17 : 0x45;
        for (int y = 0; y < combat::kH; ++y)
            for (int x = 0; x < combat::kW; ++x) BT.ground[y][x] = static_cast<uint8_t>(plain);
        put(0, 0, 10, 10, 50);
        const int before = BT.n;
        play::summon(0, *combat::fight_spell(play::d->prof->fight.spells, play::d->prof->fight.n_spells, 0x7E));
        bool joined = BT.n == before + 2;
        for (int i = before; i < BT.n && joined; ++i)
            joined = BT.f[i].team() == F(0).team() && BT.f[i].up() && BT.f[i].hp() == BT.f[i].hp_max() && BT.f[i].size;
        expect(F_, "Monster Summoning: 2 gate in on the caster's side, whole", joined, "%d joined", BT.n - before);
        // (the squares: next to the list's first fighter, N, then the next
        // direction (DS 0x3EAC / 0x3EB5: N, NE, E ...) whose square the first
        // doesn't cover - a large one covers its size's squares east and north)
        int want2 = -1;
        if (BT.n == before + 2) {
            const int sz = BT.f[before].size;
            static const int ddx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, ddy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
            for (int k = 1; k < 8 && want2 < 0; ++k) {
                const int x = 10 + ddx[k], y = 10 + ddy[k];
                const bool covered = x >= 10 && x < 10 + sz && y <= 9 && y > 9 - sz;
                if (!covered) want2 = k;
            }
        }
        const bool where = want2 >= 0 && BT.f[before].x == 10 && BT.f[before].y == 9 &&
                           BT.f[before + 1].x == 10 + (want2 == 1 || want2 == 2 || want2 == 3) - (want2 >= 5) &&
                           BT.f[before + 1].y == 10 + (want2 >= 3 && want2 <= 5) - (want2 <= 1 || want2 == 7);
        expect(F_, "Monster Summoning: next to the first fighter, north first", where, "%d,%d %d,%d (size %d)",
               BT.f[before].x, BT.f[before].y, BT.f[before + 1].x, BT.f[before + 1].y, BT.f[before].size);
    }
    #undef BT
}

// ---- The monsters' abilities (R14) and Otto's dancing (R6)
// An effect put on a fighter (as the game's own list keeps it)
void add_aff(int i, int type, int data = 0)
{
    combat::Fighter& f = F(i);
    if (!f.n_aff || *f.n_aff >= f.max_aff) return;
    if (combat::Monster* m = f.rec.monster()) {
        if (!m->room_for_aff(*f.n_aff + 1)) return;
        f.aff = m->aff;
    }
    uint8_t* a = f.aff[(*f.n_aff)++];
    memset(a, 0, party::kAffectSize);
    a[0] = static_cast<uint8_t>(type);
    a[3] = static_cast<uint8_t>(data);
}
// A blow set up: THAC0-like value 40 against AC 50 (a d20 of 10 hits), one
// attack of 1d6, no bonuses
void blower(int i, int attacks = 1)
{
    combat::Fighter& f = F(i);
    f.rec[0x199] = 40;
    f.rec[0x19E] = 1;
    f.rec[0x1A0] = 6;
    f.rec[0x1A2] = 0;
    f.rec[0x11] = 10;
    f.rec[0x1C] = 0;
    f.attacks[0] = attacks;
    f.attacks[1] = 0;
}
int blow_damage(const combat::Attack& at)
{
    int n = 0;
    for (int k = 0; k < at.n; ++k) n += at.hits[k].hit ? at.hits[k].damage : 0;
    return n;
}
int events(const combat::Event* ev, int n, combat::Ev e, int who = -1)
{
    int c = 0;
    for (int k = 0; k < n; ++k) c += ev[k].ev == e && (who < 0 || ev[k].who == who);
    return c;
}

void ability_checks()
{
    if (!new_fight()) {
        expect(F_, "a fight for the abilities", false);
        return;
    }
#define BT (play::fg->b)
    const combat::Facts& fx = *BT.fx;
    const auto& k = fx.dk;
    const int C = 0, A = BT.party_size, B = A + 1;
    auto two = [&](int side_a) {
        new_fight();
        BT.to_hit_party = BT.to_hit_monsters = 0;
        put(C, 0, 10, 10, 200);
        put(A, 1, 11, 10, 200);
        (void)side_a;
    };

    // Otto's dancing (R6, ovr 0x1DB21): the dancer's AC 4 worse - a d20 4 lower hits
    for (int dancing : {0, 1}) {
        for (int d20 : {10 - fv("dance", 0), 10 - fv("dance", 0) - 1}) {
            two(0);
            blower(C);
            if (dancing) add_aff(A, k.dancing, 0);
            dice(1, d20);
            const combat::Attack at = combat::attack(BT, C, A, nullptr, play::rng, false);
            dice_off();
            const bool want = d20 + 40 + (dancing ? fv("dance", 0) : 0) >= 50;
            snprintf(w, sizeof w, "Otto's dancing: a %sdancer hit by a d20 of %d (needs 10%s)", dancing ? "" : "non-", d20,
                     dancing ? " less 4" : "");
            expect(F_, w, (at.n == 1 && at.hits[0].hit) == want, "hit %d, the original's %d", at.n ? at.hits[0].hit : -1, want);
        }
    }

    // Terrified (0x78, ovr 0x1DBDF): -2 to hit, -2 damage (not under 0)
    for (int d20 : {12, 11}) {
        two(0);
        blower(C);
        add_aff(C, k.feared, 0);
        dice(1, d20);
        const combat::Attack at = combat::attack(BT, C, A, nullptr, play::rng, false);
        dice_off();
        const bool hit = d20 + 40 + fv("feared", 0) >= 50;
        const int want = hit ? std::max(0, 6 + fv("feared", 1)) : 0;
        snprintf(w, sizeof w, "terrified: a d20 of %d (needs 12), 1d6 (high: 6) - 2", d20);
        expect(F_, w, (at.n == 1 && at.hits[0].hit) == hit && blow_damage(at) == want, "hit %d damage %d, the original's %d / %d",
               at.n ? at.hits[0].hit : -1, blow_damage(at), hit, want);
    }

    // Kalistes (0x74, ovr 0x1D924): blows 1-6 of an attack, each its weapon's
    // die (highest here) + 4 + Strength's (10: none)
    {
        two(0);
        blower(A, 7);
        add_aff(A, k.kalistes, 0);
        dice(1, 20);
        const combat::Attack at = combat::attack(BT, A, C, nullptr, play::rng, false);
        dice_off();
        std::string got, want;
        for (int j = 0; j < at.n; ++j) got += " " + std::to_string(at.hits[j].hit ? at.hits[j].damage : -1);
        for (int j = 0; j < 7; ++j)
            want += " " + std::to_string(j < 6 ? fv("kalistes", 2 * j) * fv("kalistes", 2 * j + 1) + fv("kalistes", 12) : 6);
        expect(F_, "Kalistes: seven blows - the six weapons by number (highest dice), then its own 1d6", got == want,
               "%s, the original's%s", got.c_str(), want.c_str());
    }

    // Vorpal (0x73, ovr 0x1D8C0): after attack 1's hit a d20 of 17+ beheads -
    // not one with flags 2 & 0x20
    for (int d20 : {fv("vorpal", 0), fv("vorpal", 0) - 1}) {
        for (int proof : {0, 1}) {
            two(0);
            blower(A);
            add_aff(A, k.vorpal, 0);
            F(C).flags2 = static_cast<uint8_t>(proof ? 0x20 : 0);
            dice(1, d20);
            const combat::Attack at = combat::attack(BT, A, C, nullptr, play::rng, false);
            dice_off();
            const bool want = d20 >= fv("vorpal", 0) && !proof;
            snprintf(w, sizeof w, "vorpal: a d20 of %d%s", d20, proof ? ", the target flags 2 & 0x20" : "");
            const int beh = events(at.ev, at.n_ev, combat::Ev::Beheaded, C);
            expect(F_, w, (beh == 1) == want && (F(C).status() == party::Dead) == want, "beheaded %d status %d", beh,
                   F(C).status());
        }
    }

    // Dragon fear (0x64, ovr 0x1CC4B; phase 8 at the set-up and 0x18 each
    // round): the other side's, a save against petrification at -4, else
    // terrified (once); the first round's start has both
    for (int d20 : {18, 19}) {
        two(0);
        put(B, 1, 12, 12, 200);                     // (its own side: never)
        add_aff(A, k.dragon_fear, 0);
        BT.round = 0;
        dice(1, d20);
        combat::Event ev[16];
        const int n = combat::start_round(BT, play::rng, ev, 16);
        dice_off();
        const bool fails = d20 + fv("dfear", 1) < 15;
        snprintf(w, sizeof w, "dragon fear at the first round's start: a d20 of %d against a save of 15 at -4", d20);
        expect(F_, w, events(ev, n, combat::Ev::Terrified, C) == (fails ? 1 : 0) && F(C).has(k.feared) == fails &&
               !F(B).has(k.feared) && !F(A).has(k.feared),
               "terrified %d, has %d", events(ev, n, combat::Ev::Terrified, C), F(C).has(k.feared));
    }

    // The paralysing gaze (0x72, ovr 0x1D6B4): a save against petrification at
    // +3, else paralysed 10 rounds; the gaze-proof (0x7E) always save
    for (int d20 : {11, 12}) {
        for (int proof : {0, 1}) {
            two(0);
            add_aff(A, k.para_gaze, 0);
            if (proof) add_aff(C, k.gaze_proof, 0);
            dice(1, d20);
            combat::Event ev[16];
            bool ends = false;
            const int n = combat::special(BT, play::fg->t, A, play::rng, ev, 16, &ends);
            dice_off();
            const bool fails = !proof && d20 + fv("pgaze", 1) < 15;
            snprintf(w, sizeof w, "paralysing gaze: a d20 of %d, save 15 at +3%s", d20, proof ? ", gaze-proof" : "");
            expect(F_, w, events(ev, n, combat::Ev::ParaGazes, A) == 1 && (aff_time(C, fx.mon.paralyzed) == fv("pgaze", 2)) == fails,
                   "gazes %d, paralysed for %d", events(ev, n, combat::Ev::ParaGazes, A), aff_time(C, fx.mon.paralyzed));
        }
    }

    // The vampire's gaze (0x7C, ovr 0x30898): its pick within 10, then Charm
    // Person on it (the caller casts) - the turn over
    {
        two(0);
        add_aff(A, k.vamp_gaze, 0);
        combat::Event ev[16];
        bool ends = false;
        const int n = combat::special(BT, play::fg->t, A, play::rng, ev, 16, &ends);
        int cast_at = -1, sp = -1;
        for (int j = 0; j < n; ++j)
            if (ev[j].ev == combat::Ev::Cast) {
                cast_at = ev[j].to;
                sp = ev[j].amount;
            }
        expect(F_, "the vampire's gaze: Charm Person (spell 0x0A) at its pick, the turn over",
               events(ev, n, combat::Ev::VampGazes, A) == 1 && cast_at == C && sp == 0x0A && ends, "at %d spell %d", cast_at, sp);
    }

    // The beholder (0x75, ovr 0x23275): the rays by distance - 2 disintegrate
    // (a save kind 3), 3 flesh to stone (kind 1: "is Stoned")
    for (int dist : {fv("beholder", 0), fv("beholder", 1)}) {
        two(0);
        put(A, 1, 10 + dist, 10, 200);
        add_aff(A, fx.mon.rays, 0);
        dice(1, 1);
        combat::Event ev[16];
        bool ends = false;
        const int n = combat::special(BT, play::fg->t, A, play::rng, ev, 16, &ends);
        dice_off();
        const bool dis = dist == fv("beholder", 0);
        snprintf(w, sizeof w, "the beholder at %d squares: %s (a d20 of 1)", dist, dis ? "disintegrated" : "stoned");
        const bool ok = dis ? events(ev, n, combat::Ev::RayDisintegrate, A) == 1 && F(C).status() == party::Gone
                            : events(ev, n, combat::Ev::RayStone, A) == 1 && events(ev, n, combat::Ev::RayStoned, C) == 1 &&
                                  F(C).status() == party::Stoned;
        expect(F_, w, ok, "status %d, %d events", F(C).status(), n);
    }
#undef BT
}

// ---- The blows' rules (the fights' facts F10) and the breaths (F9)
int dmg_events(const combat::Event* ev, int n, int who)
{
    int s = 0;
    for (int k = 0; k < n; ++k)
        if (ev[k].ev == combat::Ev::Damage && ev[k].who == who) s += ev[k].amount;
    return s;
}
void blow_checks()
{
    if (!new_fight()) {
        expect(F_, "a fight for the blows", false);
        return;
    }
#define BT (play::fg->b)
    const combat::Facts& fx = *BT.fx;
    const int A = BT.party_size, B = A + 1, C = 0;
    // A monster's bare blow against one needing a +N weapon: through from
    // (N - 1) x 2 + 5 Hit Dice (F10, plus_hd)
    struct Need { uint8_t type; int plus; };
    const Need needs[] = {{fx.mon.need_plus1, 1}, {fx.mon.need_plus2, 2}, {fx.mon.need_plus3, 3}};
    for (const Need& nd : needs) {
        const int from = (nd.plus - 1) * 2 + 5;
        for (int hd : {from - 1, from}) {
            new_fight();
            BT.to_hit_party = BT.to_hit_monsters = 0;
            put(A, 1, 11, 10, 200, hd);
            put(B, 0, 12, 10, 200);
            blower(A);
            F(A).rec[0x74] = 0;
            add_aff(B, nd.type, 0);
            dice(1, 20);
            const combat::Attack at = combat::attack(BT, A, B, nullptr, play::rng, false);
            dice_off();
            const int want = hd >= from ? 6 : 0;
            snprintf(w, sizeof w, "a bare blow of %d Hit Dice on one needing +%d", hd, nd.plus);
            expect(F_, w, blow_damage(at) == want, "%d, the original's %d", blow_damage(at), want);
        }
    }
    // Fire after a hit: 0x79 2d6 after either attack, 0x68 4d6 after attack 2
    // only; a save against spells for half
    struct Fire { uint8_t type; int slot, dice; };
    const Fire fires[] = {{fx.mon.fire2d6_hit, 0, 2}, {fx.mon.fire2d6_hit, 1, 2}, {fx.mon.fire4d6_2, 1, 4}, {fx.mon.fire4d6_2, 0, 0}};
    for (const Fire& fr : fires)
        for (int d20 : {10, 20}) {
            new_fight();
            BT.to_hit_party = BT.to_hit_monsters = 0;
            put(A, 1, 11, 10, 200);
            put(C, 0, 12, 10, 200);
            blower(A);
            if (fr.slot) {
                F(A).attacks[0] = 0;
                F(A).attacks[1] = 1;
                F(A).rec[0x19F] = 1;
                F(A).rec[0x1A1] = 6;
                F(A).rec[0x1A3] = 0;
            }
            add_aff(A, fr.type, 0);
            dice(1, d20);
            const combat::Attack at = combat::attack(BT, A, C, nullptr, play::rng, false);
            dice_off();
            const int full = fr.dice * 6, want = d20 >= 15 ? full / 2 : full;
            snprintf(w, sizeof w, "fire effect 0x%02X after attack %d, the save's d20 %d", fr.type, fr.slot + 1, d20);
            expect(F_, w, dmg_events(at.ev, at.n_ev, C) == want, "%d, the original's %d", dmg_events(at.ev, at.n_ev, C), want);
        }
    // Thorne's breath (0x53): 24d10 & 0xFF + 12 in the 3 x 9 cone, a save for half
    for (int high : {1, 2}) {
        new_fight();
        put(A, 1, 14, 10, 250);
        put(C, 0, 12, 10, 255);
        add_aff(A, fx.mon.big_fire, 3);
        BT.round = 0;
        dice(high, 1);
        combat::Event ev[16];
        bool ends = false;
        const int n = combat::special(BT, play::fg->t, A, play::rng, ev, 16, &ends);
        dice_off();
        const int want = ((high == 1 ? 240 : 24) & 0xFF) + 12;
        snprintf(w, sizeof w, "Thorne's breath, the dice %s, the save missed", high == 1 ? "high" : "low");
        expect(F_, w, events(ev, n, combat::Ev::BreathesFire, A) == 1 && dmg_events(ev, n, C) == std::min(want, 255) && ends,
               "%d, the original's %d", dmg_events(ev, n, C), want);
    }
    // The black dragon's acid (0x60): a line of 6, its HP max, a save for half
    for (int d20 : {1, 20}) {
        new_fight();
        put(A, 1, 14, 10, 80);
        put(C, 0, 12, 10, 200);
        add_aff(A, fx.mon.acid_breath, 3);
        BT.round = 0;
        dice(1, d20);
        combat::Event ev[16];
        bool ends = false;
        const int n = combat::special(BT, play::fg->t, A, play::rng, ev, 16, &ends);
        dice_off();
        const int want = d20 >= 15 ? 40 : 80;
        snprintf(w, sizeof w, "the black dragon's acid (HP max 80), the save's d20 %d", d20);
        expect(F_, w, events(ev, n, combat::Ev::BreathesAcid, A) == 1 && dmg_events(ev, n, C) == want, "%d, the original's %d",
               dmg_events(ev, n, C), want);
    }
    // The money's experience (F13): platinum x 2, gems x 250, jewellery x 2200
    {
        const int money[7] = {0, 0, 0, 0, 100, 3, 2};
        const int got = combat::money_exp(money, fx.plat_xp);
        expect(F_, "the treasure's experience: 100 platinum, 3 gems, 2 pieces of jewellery", got == 100 * 2 + 3 * 250 + 2 * 2200,
               "%d", got);
    }
#undef BT
}

void checks(bool (*want)(const char*))
{
    if (want("turn")) turn_checks();
    if (want("spells")) spell_checks();
    if (want("abilities")) ability_checks();
    if (want("blows")) blow_checks();
}

} // namespace dkfight

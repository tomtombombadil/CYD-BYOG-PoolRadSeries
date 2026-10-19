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

    // Power Word Kill (R9)
    {
        reset();
        put(A, 1, 12, 10, 50);
        put(B, 1, 13, 10, 8);
        cast(C, 0x7D, {A, B});
        expect(F_, "Power Word Kill: the first of 11-60 HP max alone", F(A).status() == party::Dead && F(B).up(),
               "%d %d", F(A).status(), F(B).status());
        reset();
        put(A, 1, 12, 10, 70);
        put(B, 1, 13, 10, 8);
        put(D, 1, 14, 10, 10);
        put(E, 1, 15, 10, 11);
        cast(C, 0x7D, {A, B, D, E});
        expect(F_, "Power Word Kill: the first over 60: all of 10 or less", F(A).up() && !F(B).up() && !F(D).up() && F(E).up(),
               "%d %d %d %d", F(A).status(), F(B).status(), F(D).status(), F(E).status());
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

void checks(bool (*want)(const char*))
{
    if (want("turn")) turn_checks();
    if (want("spells")) spell_checks();
}

} // namespace dkfight

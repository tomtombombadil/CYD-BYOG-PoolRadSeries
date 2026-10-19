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

void checks(bool (*want)(const char*))
{
    if (want("turn")) turn_checks();
}

} // namespace dkfight

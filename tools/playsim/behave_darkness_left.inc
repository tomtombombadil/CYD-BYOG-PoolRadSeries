// Behaviour checks: Pools of Darkness - the level drain, Energy Drain in a
// fight (included by behave_darkness.inc inside namespace bdark; ONLY=drain).
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
    expect(D_, "the fight drains through Darkness's own routine", b.dk_drain != nullptr);
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

void checks(bool (*want)(const char*))
{
    if (!want("drain")) return;
    drain_cases();
    energy_drain();
}

} // namespace dkleft

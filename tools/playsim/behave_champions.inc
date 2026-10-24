// Behaviour checks: Champions of Krynn (included by sweep.cpp with
// -DBEHAVE_CHAMPIONS; run with GAME=5 on its folder). Against the oracle
// ($ORACLE_DIR/oracle_champions.txt, made in scratch from the player's
// program: its race / class / alignment tables and experience rows, and the
// constants of its createPlayer, gods, Modify and steel code) - never
// against the engine's own code.
// ONLY= create, train, modify, moons, knights, acid
#include "behave_oracle.inc"

namespace bchamp {

using namespace play;
Making* m = nullptr;
const char* P = "champions";

const std::vector<int>* row(const char* key, int a) { return b_row(key, a); }

bool has_fx(const party::Character& c, int type)
{
    for (int i = 0; i < c.n_affects; ++i)
        if (c.affects[i][0] == type) return true;
    return false;
}

// The experience row of a class for this character (its god, order, robe)
int xp_row_of(const party::Character& c, int k)
{
    switch (k) {
    case 0: return c.ext[party::kExtGod] <= 4 ? 0 : 1;
    case 2: return 2;
    case 7: return c.ext[party::kExtKnight] == 2 ? 4 : c.ext[party::kExtKnight] == 3 ? 5 : 3;
    case 4: return 6;
    case 5: return c.ext[party::kExtRobe] == 1 ? 7 : 8;
    case 6: return 9;
    default: return -1;
    }
}

void creation()
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    const auto* al = row("alignlist", 0);
    if (b_rows("raceclasses").empty() || !al) {
        expect(P, "the oracle's creation rows", false);
        return;
    }
    // (the alignment list's row starts with its first value - the key's
    // second number; rebuild it whole)
    std::vector<int> aligns;
    for (const auto& r : b_rows("alignlist"))
        for (int v : r) aligns.push_back(v);
    for (const auto& rc : b_rows("raceclasses")) {
        const int race = rc[0];
        const bool dwarf = race == 3 || race == 4;
        for (size_t ci = 1; ci < rc.size(); ++ci) {
            const int cls = rc[ci];
            const auto* st = row("start", cls);
            const auto* cm = row("classmask", cls);
            if (!st || !cm) {
                ++nbad;
                continue;
            }
            const bool cleric = (*st)[2] > 0;
            std::vector<int> gods;
            if (!cleric) gods.push_back(0);
            else if (dwarf) gods.push_back(6);
            else for (int g = 1; g <= 7; ++g) gods.push_back(g);
            for (int god : gods) {
                const int gm = god ? (*row("godmask", god))[1] : 0xFF;
                const int mask = (*cm)[1] & gm;
                for (int ai = 0; ai < 6; ++ai) {
                    if (!(mask >> ai & 1)) continue;
                    const int a = aligns[ai];
                    for (int sex = 0; sex < 2; ++sex)
                        for (int high = 0; high < 2; ++high) {
                            if (high) dice_high();
                            else dice_low();
                            create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + race, sex, cls, a, god);
                            create::roll(c, m->tables, m->facts, m->dice);
                            dice_usual();
                            ++ncase;
                            std::string why;
                            int32_t xp = (*st)[1];
                            if (xp == 0 && cleric) xp = a % 3 == 0 ? 2000 : 1500;
                            if (static_cast<int32_t>(c.exp()) != xp) why += " xp " + std::to_string(c.exp());
                            for (int k = 0; k < 8; ++k)
                                if (c.level(k) != (*st)[2 + k])
                                    why += " class" + std::to_string(k) + " level " + std::to_string(c.level(k));
                            if (c.alignment() != a) why += " alignment";
                            if (c.ext[party::kExtGod] != god) why += " god";
                            if (c.ext[party::kExtRace] != race) why += " race";
                            const int robe = c.level(classes::MagicUser) ? (a % 3 == 0 ? 1 : 2) : 0;
                            if (c.ext[party::kExtRobe] != robe) why += " robe";
                            if (c.ext[party::kExtKnight] != (cls == 7 ? 1 : 0)) why += " order";
                            const auto* rf = row("racefx", race);
                            for (size_t fi = 1; rf && fi < rf->size(); ++fi)
                                if (!has_fx(c, (*rf)[fi])) why += " effect " + std::to_string((*rf)[fi]);
                            if ((cls == 4 || cls == 10) && !has_fx(c, 0x69)) why += " ranger effect";
                            if (c.hp_max() < 1) why += " no hit points";
                            // steel: the classes' dice all highest / lowest, 20% more
                            int s = 0;
                            for (int k = 0; k < 8; ++k) {
                                if (!c.level(k)) continue;
                                const auto* sd = row("steel", k);
                                if (!sd) continue;
                                const int v = high ? (*sd)[1] * (*sd)[2] : (*sd)[1];
                                s = k == 7 ? v : s + v;
                            }
                            s += s * 20 / 100;
                            if (c.money(3) != s) why += " steel " + std::to_string(c.money(3)) + "/" + std::to_string(s);
                            // a new character may be modified
                            if (!create::can_modify(c, false, false, false, false, true)) why += " not modifiable";
                            if (!why.empty()) {
                                if (nbad < 12) {
                                    char w[200];
                                    snprintf(w, sizeof w, "race %d class %d god %d align %d sex %d %s:%s", race, cls,
                                             god, a, sex, high ? "high" : "low", why.c_str());
                                    expect(P, w, false);
                                }
                                ++nbad;
                            }
                        }
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "creation: %d characters as the original makes them", ncase);
    expect(P, w, nbad == 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}

// Training: each class row's experience to each level - ready at it, not a
// point under (the oracle's rows: the program's class records)
void training()
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    struct Kind { int race, cls, god, align; } kinds[] = {
        {6, 0, 1, 0}, {6, 0, 5, 1}, {6, 2, 0, 0}, {6, 7, 0, 0}, {6, 4, 0, 0}, {6, 5, 0, 0}, {6, 5, 0, 1}, {6, 6, 0, 1},
    };
    for (const Kind& kd : kinds) {
        dice_usual();
        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + kd.race, 0, kd.cls, kd.align, kd.god);
        create::roll(c, m->tables, m->facts, m->dice);
        int k = 0;
        while (k < 8 && !c.level(k)) ++k;
        const int xr = xp_row_of(c, k);
        const auto* xrow = row("xp", xr);
        if (!xrow) {
            ++nbad;
            continue;
        }
        for (int lv = 1; lv <= 10; ++lv) {
            const int need = (*xrow)[lv];          // (the row: its number, then levels 1-11's need)
            if (need <= 0) break;
            for (int k2 = 0; k2 < 8; ++k2)
                if (c.level(k2)) c.rec[0x109 + k2] = static_cast<uint8_t>(lv);
            for (int under = 0; under < 2; ++under) {
                const uint32_t xp = static_cast<uint32_t>(need - under);
                for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(xp >> (8 * i));
                const bool ready = create::trainable(c, m->tables) != 0;
                ++ncase;
                if (ready == static_cast<bool>(under)) {
                    if (nbad < 12) {
                        char w[160];
                        snprintf(w, sizeof w, "class %d row %d level %d xp %u: %s", kd.cls, xr, lv, xp,
                                 ready ? "ready" : "not ready");
                        expect(P, w, false);
                    }
                    ++nbad;
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "training: %d levels ready at the original's experience", ncase);
    expect(P, w, nbad == 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}

// Modify: allowed at each starting figure, not a point either side
void modify()
{
    static party::Character c;
    dice_usual();
    create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + 6, 0, create::Fighter, 0, 0);
    create::roll(c, m->tables, m->facts, m->dice);
    int nbad = 0, ncase = 0;
    const auto& mx = b_rows("modxp");
    if (mx.empty()) {
        expect(P, "modify: the oracle's figures", false);
        return;
    }
    for (int v : mx[0])
        for (int d2 = -1; d2 <= 1; ++d2) {
            const uint32_t xp = static_cast<uint32_t>(v + d2);
            bool listed = false;
            for (int u : mx[0]) listed = listed || static_cast<uint32_t>(u) == xp;
            for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(xp >> (8 * i));
            ++ncase;
            if (create::can_modify(c, false, false, false, false, true) != listed) ++nbad;
        }
    char w[96];
    snprintf(w, sizeof w, "modify: %d experience figures allowed / refused as the original", ncase);
    expect(P, w, nbad == 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}

// The moons: a mage's extra spells by its robe's moon (phase 1: 1, full 2,
// others none; GAME.OVR 0x1B31A)
void moons()
{
    static party::Character c;
    int nbad = 0, ncase = 0;
    for (int a : {0, 1}) {
        dice_usual();
        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + 6, 0, create::MagicUser, a, 0);
        create::roll(c, m->tables, m->facts, m->dice);
        const int robe = c.ext[party::kExtRobe];
        for (int ph = 0; ph < 4; ++ph) {
            int p[3] = {0, 0, 0};
            if (robe >= 1 && robe <= 3) p[robe - 1] = ph;
            magic::set_moons(p[0], p[1], p[2]);
            const int want = ph == 1 ? 1 : ph == 2 ? 2 : 0;
            ++ncase;
            if (magic::moon_bonus(c, m->tables) != want) ++nbad;
        }
    }
    magic::set_moons(0, 0, 0);
    char w[96];
    snprintf(w, sizeof w, "moons: %d phases giving a mage its extra spells", ncase);
    expect(P, w, nbad == 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}

// The knights' petitions: each order's least level, experience and stats
// - may become at them, too inexperienced a level or a point under, not
// qualified a stat under
void knights()
{
    static party::Character c;
    int nbad = 0, ncase = 0;
    for (const auto& k : b_rows("knight")) {
        const int order = k[0], lv = k[1], xp = k[2];
        dice_usual();
        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + 6, 0, create::Monk, 0, 0);
        create::roll(c, m->tables, m->facts, m->dice);
        c.ext[party::kExtKnight] = static_cast<uint8_t>(order);
        auto set = [&](int level, uint32_t e, int low_stat) {
            c.rec[0x109 + 7] = static_cast<uint8_t>(level);
            for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(e >> (8 * i));
            for (int i = 0; i < 6; ++i) c.rec[0x10 + i * 2] = static_cast<uint8_t>(k[3 + i] - (i == low_stat ? 1 : 0));
        };
        const struct { int level; uint32_t xp; int low; int want; } cases[] = {
            {lv, static_cast<uint32_t>(xp), -1, 0}, {lv - 1, static_cast<uint32_t>(xp), -1, 1},
            {lv, static_cast<uint32_t>(xp - 1), -1, 1}, {lv, static_cast<uint32_t>(xp), 0, 2}, {lv, static_cast<uint32_t>(xp), 4, 2},
        };
        for (const auto& t : cases) {
            set(t.level, t.xp, t.low);
            ++ncase;
            const int got = create::knight_petition(c, m->tables, m->facts);
            if (got != t.want) {
                ++nbad;
                char w[120];
                snprintf(w, sizeof w, "knights: order %d level %d xp %u stat %d low: %d, want %d", order, t.level, t.xp,
                         t.low, got, t.want);
                expect(P, w, false);
            }
        }
    }
    // A knight's saves: the original's row 7 level 0 (Game Fixes off), its
    // own level's row with them on
    for (int fx = 0; fx < 2; ++fx) {
        rules::set_fixes(fx == 1);
        dice_usual();
        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + 6, 0, create::Monk, 0, 0);
        create::roll(c, m->tables, m->facts, m->dice);
        const auto& rr = b_rows(fx ? "knightrow1" : "knightsave");
        ++ncase;
        bool same = !rr.empty();
        for (int i = 0; same && i < 5; ++i) same = c.rec[0xDF + i] == rr[0][i];
        if (!same) {
            ++nbad;
            expect(P, fx ? "knights: saves with Game Fixes" : "knights: saves as the original", false);
        }
    }
    rules::set_fixes(true);
    char w[96];
    snprintf(w, sizeof w, "knights: %d petitions and saves as the original", ncase);
    expect(P, w, nbad == 0 && ncase > 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}

// The kapak's acid pool (GAME.OVR 0x12C1F / 0x34F7F; the oracle's "acid":
// ground, effect, its time's dice, the damage's dice from the program's
// code): a field of plain ground, the kapak's square laid, those with a
// square in it, the 1d6, the pool's time and the ground back after
void acid()
{
    const auto* r = row("acid", d->prof->fight.facts.acid_ground);
    if (!r || (*r)[2] != 1 || (*r)[4] != 1) {
        expect(P, "acid: the oracle's row", false, "ground %d", d->prof->fight.facts.acid_ground);
        return;
    }
    const int ground = (*r)[0], sides = (*r)[3], hsides = (*r)[5];
    expect(P, "acid: the kapak's effect", d->prof->fight.facts.mon.acid_pool > 0, "%d", d->prof->fight.facts.mon.acid_pool);
    static combat::Fighter fs[3];
    static combat::Battle b;
    static combat::Tables t;
    const int plain = 0x17;
    int nbad = 0, seen_r = 0, seen_h = 0, n = 0;
    for (int k = 0; k < 600; ++k) {
        b = combat::Battle{};
        b.fx = &d->prof->fight.facts;
        b.f = fs;
        b.cap = 3;
        b.n = 3;
        for (int y = 0; y < combat::kH; ++y)
            for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = plain;
        static uint8_t recs[3][party::kRecordSize];
        for (int i = 0; i < 3; ++i) {
            fs[i] = combat::Fighter{};
            memset(recs[i], 0, sizeof recs[i]);
            fs[i].rec = combat::Rec(recs[i]);
            fs[i].rec[0x196] = 1;
            fs[i].rec[0x195] = party::Okay;
            fs[i].rec[0x1A4] = fs[i].rec[0x78] = 60;
            fs[i].size = 1;
        }
        fs[0].x = 10, fs[0].y = 8;                 // (the kapak's square)
        fs[1].x = 10, fs[1].y = 8;                 // one that steps in
        fs[2].x = 9, fs[2].y = 7, fs[2].size = 4;  // a large one (2 x 2): its south-east square in it
        // (an earlier ground under it comes back: here a stinking cloud there first)
        const bool cloud = k % 2;
        if (cloud) combat::lay_cloud(b, t, 10, 8, 2);
        const bool laid = combat::lay_acid(b, t, 10, 8, rng);
        int rounds = -1;
        for (int c = 0; c < b.n_clouds; ++c)
            if (b.clouds[c].acid) rounds = b.clouds[c].rounds;
        ++n;
        if (!laid || b.ground[8][10] != ground || rounds < 1 || rounds > sides || !combat::on_acid(b, 1) ||
            !combat::on_acid(b, 2) || combat::on_acid(b, 0) != true) {
            ++nbad;
            continue;
        }
        seen_r |= 1 << rounds;
        bool dn = false;
        const int hp = fs[1].hp();
        const int h = combat::acid_burn(b, 1, rng, &dn);
        if (h < 1 || h > hsides || fs[1].hp() != hp - h) ++nbad;
        seen_h |= 1 << h;
        // the round ends: gone after its time - the plain ground back
        int went = -1;
        for (int round = 1; round <= sides + 2 && went < 0; ++round) {
            combat::clouds_round(b);
            if (b.acid_gone) went = round;
        }
        const bool acid_off = !combat::on_acid(b, 1);
        for (int round = 0; round < 4 && b.n_clouds; ++round) combat::clouds_round(b);    // (the cloud's time too)
        if (went != rounds || !acid_off || b.ground[8][10] != plain) ++nbad;
    }
    const int all_r = ((1 << (sides + 1)) - 2), all_h = ((1 << (hsides + 1)) - 2);
    char w[120];
    snprintf(w, sizeof w, "acid: a kapak's pool (ground %d) for 1d%d rounds, 1d%d on those in it, the ground back (%d pools)",
             ground, sides, hsides, n);
    expect(P, w, nbad == 0 && seen_r == all_r && seen_h == all_h, "%d wrong, rounds seen %x, damage seen %x", nbad, seen_r,
           seen_h);
}

bool open_game()
{
    play::close();
    const size_t sl = game_dir.rfind('/');
    pc::card_root() = game_dir.substr(0, sl);
    static std::string folder;
    folder = game_dir.substr(sl + 1);
    if (play::open(folder.c_str(), games::Game::ChampionsOfKrynn, C, nullptr)) return false;
    for (int guard = 0; play::screen == play::Screen::Title && guard < 40; ++guard) {
        if (play::title_run && play::title_run->prompt) {
            tap_word('P');
            continue;
        }
        play::tap(2, 2, C);
    }
    return play::screen == play::Screen::PartyMenu;
}

} // namespace bchamp

static void behave_champions()
{
    using namespace bchamp;
    if (!b_oracle_load("champions")) {
        expect(P, "oracle file", false, "no oracle_champions.txt");
        return;
    }
    if (!open_game()) {
        expect(P, "the game opened", false);
        return;
    }
    if (!play::load_rules(bchamp::m)) {
        expect(P, "rule tables", false);
        return;
    }
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("create")) creation();
    if (want("train")) training();
    if (want("modify")) modify();
    if (want("moons")) moons();
    if (want("knights")) knights();
    if (want("acid")) acid();
}

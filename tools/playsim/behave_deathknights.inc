// Behaviour checks: Death Knights of Krynn (included by sweep.cpp with
// -DBEHAVE_DEATHKNIGHTS; run with GAME=6 on its folder). Against the oracle
// ($ORACLE_DIR/oracle_deathknights.txt, made in scratch from the player's
// program: its race / class / alignment tables, experience rows and level
// limits, the knights' figures, and the constants of its createPlayer) -
// never against the engine's own code.
// ONLY= create, train, knights
#include "behave_oracle.inc"

namespace bdkk {

using namespace play;
Making* m = nullptr;
const char* P = "deathknights";

const std::vector<int>* row(const char* key, int a) { return b_row(key, a); }

// The classes a class code holds (the AD&D codes the games share: 0 cleric
// ... 7 knight, then the multi-classes) as record slots
std::vector<int> held(int cls)
{
    switch (cls) {
    case 8: return {0, 2};
    case 9: return {0, 2, 5};
    case 10: return {0, 4};
    case 11: return {0, 5};
    case 12: return {0, 6};
    case 13: return {2, 5};
    case 14: return {2, 6};
    case 15: return {2, 5, 6};
    case 16: return {5, 6};
    default: return {cls};
    }
}

// The experience row of a class for this character (its god, order, robe;
// ovl 0xCF +0x382C)
int xp_row_of(const party::Character& c, int k)
{
    switch (k) {
    case 0: return c.ext[party::kExtGod] <= 4 ? 0 : 1;
    case 2: return 2;
    case 3: return 10;
    case 7: return c.ext[party::kExtKnight] == 2 ? 4 : c.ext[party::kExtKnight] == 3 ? 5 : 3;
    case 4: return 6;
    case 5: return c.ext[party::kExtRobe] == 1 ? 7 : 8;
    case 6: return 9;
    default: return -1;
    }
}

// The level a class reaches with this experience: its row's need met, under
// the race's limit (training while the limit is above the level)
int level_for(int xrow, uint32_t xp, int cap)
{
    const auto* x = row("xp", xrow);
    if (!x) return -1;
    int lv = 1;
    for (int L = 2; L < 19 && L + 1 < static_cast<int>(x->size()); ++L) {
        const int need = (*x)[1 + L];
        if (need <= 0 || static_cast<uint32_t>(need) > xp || lv >= cap) break;
        lv = L;
    }
    return lv;
}

void creation()
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    std::vector<int> aligns;
    for (const auto& r : b_rows("alignlist"))
        for (int v : r) aligns.push_back(v);
    const auto* sx = row("startxp", 210003);
    const auto* st = b_rows("steel").empty() ? nullptr : &b_rows("steel")[0];
    const auto* mu = b_rows("muspells").empty() ? nullptr : &b_rows("muspells")[0];
    if (b_rows("raceclasses").empty() || aligns.size() != 6 || !sx || !st || !mu) {
        expect(P, "the oracle's creation rows", false);
        return;
    }
    for (const auto& rc : b_rows("raceclasses")) {
        const int race = rc[0];
        const bool dwarf = race == 3 || race == 4;
        const auto* cap = row("cap", race);
        for (size_t ci = 1; ci < rc.size(); ++ci) {
            const int cls = rc[ci];
            const auto* cm = row("classmask", cls);
            const std::vector<int> ks = held(cls);
            const bool cleric = ks[0] == 0;
            for (int ai = 0; ai < 6; ++ai) {
                if (!cm || !((*cm)[1] >> ai & 1)) continue;
                const int a = aligns[ai];
                // (a cleric's god: Paladine for the good, Sirrion for the
                // neutral, Reorx for the dwarves)
                const int god = !cleric ? 0 : dwarf ? 6 : a % 3 == 0 ? 1 : 5;
                for (int sex = 0; sex < 2; ++sex) {
                    dice_usual();
                    create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + race, sex, cls, a, god);
                    create::roll(c, m->tables, m->facts, m->dice);
                    ++ncase;
                    std::string why;
                    const uint32_t each = (210003u + ks.size() - 1) / ks.size();
                    if (c.exp() != each) why += " xp " + std::to_string(c.exp());
                    for (int k = 0; k < 8; ++k) {
                        bool h = false;
                        for (int q : ks) h = h || q == k;
                        int want = 0;
                        if (h) {
                            const int cp = cap ? (*cap)[1 + k] : 0;
                            want = level_for(xp_row_of(c, k), each, cp > 0 ? cp : 1);
                        }
                        if (c.level(k) != want)
                            why += " class" + std::to_string(k) + " level " + std::to_string(c.level(k)) + "/" +
                                   std::to_string(want);
                    }
                    if (c.alignment() != a) why += " alignment";
                    if (c.ext[party::kExtRace] != race) why += " race";
                    if (c.ext[party::kExtKnight] != (cls == 7 ? 1 : 0)) why += " order";
                    if (c.money(4) != (*st)[0]) why += " steel " + std::to_string(c.money(4));
                    if (c.level(classes::MagicUser) > 0) {
                        int n = 0;
                        for (int s = 1; s <= 114; ++s) n += classes::in_book(c, s) ? 1 : 0;
                        int want = (*mu)[0];
                        // (a cleric / magic-user's cleric spells counted apart: only magic-user ones here)
                        if (!cleric && n != want) {
                            why += " spells " + std::to_string(n) + ":";
                            for (int s = 1; s < 128; ++s) if (classes::in_book(c, s)) why += " " + std::to_string(s);
                        }
                    }
                    if (c.hp_max() < 1) why += " no hit points";
                    if (!why.empty()) {
                        if (nbad < 12) {
                            char w[240];
                            snprintf(w, sizeof w, "race %d class %d god %d align %d sex %d:%s", race, cls, god, a, sex,
                                     why.c_str());
                            expect(P, w, false);
                        }
                        ++nbad;
                    }
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "creation: %d characters as the original makes them", ncase);
    expect(P, w, nbad == 0, "%d wrong", nbad);
}

// Training: each class row's experience to each level - ready at it, not a
// point under; never at the race's limit
void training()
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    struct Kind { int race, cls, god, align; } kinds[] = {
        {6, 0, 1, 0}, {6, 0, 5, 1}, {6, 2, 0, 0}, {6, 7, 0, 0}, {6, 4, 0, 0}, {6, 5, 0, 0}, {6, 5, 0, 1}, {6, 6, 0, 1},
        {6, 3, 0, 0}, {5, 6, 0, 1}, {3, 2, 0, 0}, {2, 5, 0, 0},
    };
    for (const Kind& kd : kinds) {
        dice_usual();
        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + kd.race, 0, kd.cls, kd.align, kd.god);
        create::roll(c, m->tables, m->facts, m->dice);
        int k = 0;
        while (k < 8 && !c.level(k)) ++k;
        const int xr = xp_row_of(c, k);
        const auto* xrow = row("xp", xr);
        const auto* cap = row("cap", kd.race);
        if (!xrow || !cap || k >= 8) {
            ++nbad;
            continue;
        }
        const int limit = (*cap)[1 + k];
        for (int lv = 1; lv <= 17; ++lv) {
            const int need = (*xrow)[1 + lv + 1];      // (to reach lv + 1)
            if (need <= 0) break;
            c.rec[0x109 + k] = static_cast<uint8_t>(lv);
            for (int under = 0; under < 2; ++under) {
                const uint32_t xp = static_cast<uint32_t>(need - under);
                for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(xp >> (8 * i));
                const bool ready = create::trainable(c, m->tables) != 0;
                const bool want = !under && limit > lv;
                ++ncase;
                if (ready != want) {
                    if (nbad < 12) {
                        char w[160];
                        snprintf(w, sizeof w, "race %d class %d row %d level %d xp %u (limit %d): %s", kd.race, kd.cls, xr,
                                 lv, xp, limit, ready ? "ready" : "not ready");
                        expect(P, w, false);
                    }
                    ++nbad;
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "training: %d levels ready at the original's experience and limits", ncase);
    expect(P, w, nbad == 0, "%d wrong", nbad);
}

// The knights' petitions: each order's least level, experience and stats -
// may become at them, too inexperienced a level or a point under, not
// qualified a stat under; the change: the new order's row for the level
// held - kept at it, a level less a point under (ovl 0x61 +0x4DA1)
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
        // the change, at levels 5 and 11 (past 9th: 2 hit points), with the
        // new order's need for the level held and a point under
        const auto* nrow = row("xp", order == 1 ? 4 : 5);
        for (int level : {5, 11}) {
            if (!nrow) break;
            const int need = (*nrow)[1 + level];
            for (int under = 0; under < 2; ++under) {
                set(level, static_cast<uint32_t>(need - under), -1);
                c.ext[party::kExtKnight] = static_cast<uint8_t>(order);
                c.rec[0x12C] = 60;                  // (the rolled hit points)
                const int hpmax = c.hp_max();
                create::knight_change(c, m->tables);
                ++ncase;
                const int want_lv = under ? level - 1 : level;
                const bool ok = c.ext[party::kExtKnight] == order + 1 && c.level(7) == want_lv &&
                                (!under || level <= 9 || c.hp_max() == hpmax - 2);
                if (!ok) {
                    ++nbad;
                    char w[140];
                    snprintf(w, sizeof w, "knights: change order %d level %d xp %s: order %d level %d hp %d of %d", order,
                             level, under ? "under" : "at", c.ext[party::kExtKnight], c.level(7), c.hp_max(), hpmax);
                    expect(P, w, false);
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "knights: %d petitions and changes as the original", ncase);
    expect(P, w, nbad == 0 && ncase > 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}

bool open_game()
{
    play::close();
    const size_t sl = game_dir.rfind('/');
    pc::card_root() = game_dir.substr(0, sl);
    static std::string folder;
    folder = game_dir.substr(sl + 1);
    if (play::open(folder.c_str(), games::Game::DeathKnightsOfKrynn, C, nullptr)) return false;
    for (int guard = 0; play::screen == play::Screen::Title && guard < 40; ++guard) {
        if (play::title_run && play::title_run->prompt) {
            tap_word('P');
            continue;
        }
        play::tap(2, 2, C);
    }
    return play::screen == play::Screen::PartyMenu;
}

} // namespace bdkk

static void behave_deathknights()
{
    using namespace bdkk;
    if (!b_oracle_load("deathknights")) {
        expect(P, "oracle file", false, "no oracle_deathknights.txt");
        return;
    }
    if (!open_game()) {
        expect(P, "the game opened", false);
        return;
    }
    if (!play::load_rules(bdkk::m)) {
        expect(P, "rule tables", false);
        return;
    }
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("create")) creation();
    if (want("create")) b_krynn_stats_check(bdkk::m, P);
    if (want("train")) training();
    if (want("knights")) knights();
}

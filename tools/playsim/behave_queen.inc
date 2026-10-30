// Behaviour checks: The Dark Queen of Krynn (included by sweep.cpp with
// -DBEHAVE_QUEEN; run with GAME=7 on its folder). Against the oracle
// ($ORACLE_DIR/oracle_queen.txt, made in scratch from the player's DQK.EXE
// and MONCHA.GLB: its experience rows and steps, level limits, races'
// classes and alignments, the knights' figures, the spells' rows, the
// monsters' experience, the constants of its createPlayer) - never against
// the engine's own code.
// ONLY= create, train, knights, spells, monsters
#include "behave_oracle.inc"
#include "engine/hlib.h"

namespace bq {

using namespace play;
Making* m = nullptr;
const char* P = "queen";

// The engine's class slots (0 cleric ... 7 knight) in its order (cleric,
// knight, fighter, paladin, ranger, magic-user, thief) - the cap rows' places
int q_order(int k)
{
    static const int kAt[8] = {0, -1, 2, 3, 4, 5, 6, 1};
    return k >= 0 && k < 8 ? kAt[k] : -1;
}

// The classes a class code holds (the engine's codes: 7 the knight)
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

// Its experience row (resident 0xBB19): cleric by god 1-4 / 5-7, knight by
// order 1-3, fighter 2, paladin 10, ranger 6, magic-user White 7 else 8, thief 9
int xp_row_of(const party::Character& c, int k)
{
    switch (k) {
    case 0: return c.ext[party::kExtGod] >= 5 ? 1 : 0;
    case 7: return 2 + (c.ext[party::kExtKnight] ? c.ext[party::kExtKnight] : 1);
    case 2: return 2;
    case 3: return 10;
    case 4: return 6;
    case 5: return c.ext[party::kExtRobe] == 1 ? 7 : 8;
    case 6: return 9;
    default: return -1;
    }
}

// The experience for level L in a row (its routine ovl 0xB +0x5FDC: the
// table to 17th, then 17th's + a step a level; none past 40th)
int64_t need(int xrow, int L)
{
    const auto* x = b_row("xp", xrow);
    const auto* st = b_row("xpstep", xrow);
    if (!x || !st || L < 1) return -1;
    if (L > 40) return INT64_MAX;
    if (L < 18) return static_cast<uint32_t>((*x)[1 + L]);
    return static_cast<int64_t>(static_cast<uint32_t>((*x)[1 + 17])) + static_cast<int64_t>(L - 17) * (*st)[1];
}

// The level a class reaches by training with this experience under its limit
int level_for(int xrow, int64_t xp, int cap)
{
    int lv = 1;
    while (lv < cap) {
        const int64_t n = need(xrow, lv + 1);
        if (n <= 0 || n > xp) break;
        ++lv;
    }
    return lv;
}

int cap_of(int race, int k, int order)
{
    // (training's limits: a knight of the Rose 40, of the Sword 18; else
    // the race's row in its class order)
    if (k == 7 && order == 3) return 40;
    if (k == 7 && order == 2) return 18;
    const auto* cp = b_row("cap", race);
    const int at = q_order(k);
    return cp && at >= 0 ? (*cp)[1 + at] : 0;
}

void creation()
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    const auto* sx = b_rows("startxp").empty() ? nullptr : &b_rows("startxp")[0];
    const auto* st = b_rows("steel").empty() ? nullptr : &b_rows("steel")[0];
    if (b_rows("raceclasses").empty() || !sx || !st) {
        expect(P, "the oracle's creation rows", false);
        return;
    }
    for (const auto& rc : b_rows("raceclasses")) {
        const int race = rc[0];
        const bool dwarf = race == 3 || race == 4;
        for (size_t ci = 1; ci < rc.size(); ++ci) {
            const int qcls = rc[ci];
            const int cls = qcls == 1 ? 7 : qcls == 7 ? 1 : qcls;      // (its knight 1: the engine's 7)
            const auto* al = b_row("align", qcls);
            if (!al || al->size() < 2) continue;
            const std::vector<int> ks = held(cls);
            const bool cleric = ks[0] == 0;
            for (size_t ai = 1; ai < al->size(); ++ai) {
                const int a = (*al)[ai];
                if (a % 3 == 2) continue;                   // (no evil characters)
                const int god = !cleric ? 0 : dwarf ? 6 : a % 3 == 0 ? 1 : 5;
                dice_usual();
                create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + race, 0, cls, a, god);
                create::roll(c, m->tables, m->facts, m->dice);
                ++ncase;
                std::string why;
                const int64_t start = ks.size() >= 3 ? (*sx)[2] : ks.size() == 2 ? (*sx)[1] : (*sx)[0];
                // (training from 1st as far as the start's experience goes,
                // the experience then cut to the cap - queen_facts_4.md 6:
                // never more than the start)
                if (c.exp() > start) why += " xp " + std::to_string(c.exp());
                for (int k = 0; k < 8; ++k) {
                    bool h = false;
                    for (int q : ks) h = h || q == k;
                    int want = 0;
                    if (h) {
                        const int cp = cap_of(race, k, c.ext[party::kExtKnight]);
                        want = level_for(xp_row_of(c, k), start, cp > 0 ? cp : 1);
                    }
                    if (c.level(k) != want)
                        why += " class" + std::to_string(k) + " level " + std::to_string(c.level(k)) + "/" + std::to_string(want);
                }
                if (c.alignment() != a) why += " alignment";
                if (c.ext[party::kExtRace] != race) why += " race";
                if (c.ext[party::kExtKnight] != (cls == 7 ? 1 : 0)) why += " order";
                if (c.money(4) != (*st)[0]) why += " steel " + std::to_string(c.money(4));
                if (c.hp_max() < 1) why += " no hit points";
                if (!why.empty()) {
                    if (nbad < 12) {
                        char w[240];
                        snprintf(w, sizeof w, "race %d class %d god %d align %d:%s", race, cls, god, a, why.c_str());
                        expect(P, w, false);
                    }
                    ++nbad;
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "creation: %d characters at the original's levels", ncase);
    expect(P, w, nbad == 0 && ncase > 0, "%d wrong", nbad);
}

// Training: ready at each level's experience, not a point under; never at
// the race's limit
void training()
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    struct Kind { int race, cls, god, align; } kinds[] = {
        {6, 0, 1, 0}, {6, 0, 5, 1}, {6, 2, 0, 0}, {6, 7, 0, 0}, {6, 4, 0, 0}, {6, 5, 0, 0}, {6, 5, 0, 1},
        {6, 6, 0, 1}, {6, 3, 0, 0}, {5, 6, 0, 1}, {3, 2, 0, 0}, {2, 5, 0, 0}, {0, 3, 0, 0},
    };
    for (const Kind& kd : kinds) {
        dice_usual();
        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + kd.race, 0, kd.cls, kd.align, kd.god);
        create::roll(c, m->tables, m->facts, m->dice);
        int k = 0;
        while (k < 8 && !c.level(k)) ++k;
        if (k >= 8) {
            ++nbad;
            continue;
        }
        const int xr = xp_row_of(c, k), limit = cap_of(kd.race, k, c.ext[party::kExtKnight]);
        for (int lv = 1; lv <= 30; ++lv) {
            const int64_t n = need(xr, lv + 1);
            if (n <= 0 || n > 0x7FFFFFFF) break;
            c.rec[0x109 + k] = static_cast<uint8_t>(lv);
            for (int under = 0; under < 2; ++under) {
                const uint32_t xp = static_cast<uint32_t>(n - under);
                for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(xp >> (8 * i));
                const bool want = !under && lv < limit;
                const bool got = create::trainable(c, m->tables) != 0;
                ++ncase;
                if (got != want) {
                    ++nbad;
                    if (nbad < 12) {
                        char w[160];
                        snprintf(w, sizeof w, "train: race %d class %d level %d xp %u: %d, want %d", kd.race, kd.cls, lv, xp,
                                 got, want);
                        expect(P, w, false);
                    }
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "training: %d levels' experience as the original's rows", ncase);
    expect(P, w, nbad == 0 && ncase > 0, "%d wrong", nbad);
}

// Knight New Order (DQK 0x487F2 / 0x48710): the petition's three answers and
// the change's levels kept or lost
void knights()
{
    static party::Character c;
    int nbad = 0, ncase = 0;
    for (const auto& k : b_rows("knight")) {
        const int order = k[0], lv = k[1], xp = k[2];
        dice_usual();
        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + 6, 0, create::Monk, 0, 0);
        create::roll(c, m->tables, m->facts, m->dice);
        auto set = [&](int level, uint32_t e, int low_stat) {
            c.ext[party::kExtKnight] = static_cast<uint8_t>(order);
            c.rec[0x109 + 7] = static_cast<uint8_t>(level);
            for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(e >> (8 * i));
            for (int i = 0; i < 6; ++i) c.rec[0x10 + i * 2] = static_cast<uint8_t>(k[3 + i] - (i == low_stat ? 1 : 0));
        };
        const struct { int level; uint32_t xp; int low; int want; } cases[] = {
            {lv, static_cast<uint32_t>(xp), -1, 0},      {lv - 1, static_cast<uint32_t>(xp), -1, 1},
            {lv, static_cast<uint32_t>(xp - 1), -1, 1},  {lv, static_cast<uint32_t>(xp), 0, 2},
            {lv, static_cast<uint32_t>(xp), 4, 2},       {lv + 5, static_cast<uint32_t>(xp), 1, 2},
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
        // The change: levels come off while the NEW order's experience for
        // the level held is more than the character has (past 9th 2 hit
        // points a level)
        const int nrow = 3 + order;
        for (int level : {5, 9, 12, 15}) {
            for (const int64_t xpv : {need(nrow, level), need(nrow, level) - 1, need(nrow, level - 2) - 1}) {
                if (xpv <= 0) continue;
                set(level, static_cast<uint32_t>(xpv), -1);
                c.rec[0x12C] = 90;
                const int hpmax = c.hp_max();
                create::knight_change(c, m->tables);
                ++ncase;
                int want_lv = level;
                while (want_lv > 1 && need(nrow, want_lv) > xpv) --want_lv;
                int lost_fixed = 0;
                for (int l = level; l > want_lv; --l) lost_fixed += l > 9 ? 2 : 0;
                const bool hp_ok = level <= 9 || want_lv < 9 || c.hp_max() == hpmax - lost_fixed;
                if (c.ext[party::kExtKnight] != order + 1 || c.level(7) != want_lv || !hp_ok) {
                    ++nbad;
                    char w[160];
                    snprintf(w, sizeof w, "knights: change order %d level %d xp %lld: order %d level %d (want %d) hp %d of %d",
                             order, level, static_cast<long long>(xpv), c.ext[party::kExtKnight], c.level(7), want_lv,
                             c.hp_max(), hpmax);
                    expect(P, w, false);
                }
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "knights: %d petitions and changes as the original", ncase);
    expect(P, w, nbad == 0 && ncase > 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}

// The spells' rows: each spell's class and level as the program's table has them
void spell_rows()
{
    int ncase = 0, nbad = 0;
    for (const auto& s : b_rows("spell")) {
        const int n = s[0], lv = s[2];
        // (its classes by meaning, as the engine names them: 0 cleric, 1
        // druid, 2 magic-user, 3 a god's special spell - the engine's 4 -,
        // 4 an item's and 0xFF a monster's or reversed cleric spell - the
        // engine's "items' and monsters'" 3; queen_facts_4.md 8)
        const int cls = s[1] == 3 ? 4 : s[1] == 4 || s[1] == 0xFF ? 3 : s[1];
        if (n >= m->tables.lay.spell_count) continue;
        ++ncase;
        const int gc = m->tables.spell_class(n), gl = m->tables.spell_level(n);
        if (gc != cls || gl != lv) {
            ++nbad;
            if (nbad < 12) {
                char w[120];
                snprintf(w, sizeof w, "spells: %d class %d level %d, want %d / %d", n, gc, gl, cls, lv);
                expect(P, w, false);
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "spells: %d rows' class and level as the program's", ncase);
    expect(P, w, nbad == 0 && ncase > 0, "%d wrong", nbad);
}

// The monsters: each one's record read as the engine reads it - its
// experience (all of it from the record's 0x54: base 0x13C, nothing a hit
// point) and its flags
void monsters()
{
    int ncase = 0, nbad = 0;
    char path[300];
    snprintf(path, sizeof path, "%s/DISK2/MONCHA.GLB", game_dir.c_str());
    FILE* f = fopen(path, "rb");
    if (!f) {
        expect(P, "MONCHA.GLB", false);
        return;
    }
    std::vector<uint8_t> b;
    int ch;
    while ((ch = fgetc(f)) != EOF) b.push_back(static_cast<uint8_t>(ch));
    fclose(f);
    dax::MemorySource src(b.data(), b.size());
    hlib::Library lib;
    if (hlib::open(src, 0, static_cast<uint32_t>(b.size()), lib) != hlib::Status::Ok) {
        expect(P, "MONCHA.GLB opened", false);
        return;
    }
    static party::Character c;
    for (int i = lib.first_item(); i < lib.count; ++i) {
        uint32_t pos, len;
        if (!hlib::entry(src, lib, i, pos, len) || len < 3) continue;
        const int id = hlib::id_of(src, lib, i);
        const auto* o = b_row("monster", id);
        if (!o) continue;
        // (the entry: a big-endian size, then runs - c < 0x80: c + 1 bytes; else one byte 257 - c times)
        const uint32_t total = static_cast<uint32_t>(b[pos] << 8 | b[pos + 1]);
        std::vector<uint8_t> q;
        for (uint32_t at = pos + 2; at < pos + len && q.size() < total;) {
            const uint8_t cc = b[at++];
            if (cc < 0x80) {
                for (int k = 0; k <= cc && at < pos + len; ++k) q.push_back(b[at++]);
            } else if (at < pos + len) {
                const uint8_t v = b[at++];
                for (int k = 0; k < 257 - cc; ++k) q.push_back(v);
            }
        }
        if (q.size() < party::kQueenRecordSize) continue;
        party::from_queen(q.data(), c);
        ++ncase;
        const int base = c.rec[0x13C] | c.rec[0x13D] << 8;
        if (base != (*o)[1] || c.rec[0x13E] != 0 || c.rec[0x14B] != (*o)[2] || c.ext[party::kExtFlags2] != (*o)[3]) {
            ++nbad;
            if (nbad < 12) {
                char w[160];
                snprintf(w, sizeof w, "monsters: %d xp %d + %d a HP, flags %02X %02X - want %d, %02X %02X", id, base,
                         c.rec[0x13E], c.rec[0x14B], c.ext[party::kExtFlags2], (*o)[1], (*o)[2], (*o)[3]);
                expect(P, w, false);
            }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "monsters: %d records' experience and flags as the original's", ncase);
    expect(P, w, nbad == 0 && ncase > 0, "%d wrong", nbad);
}

bool open_game()
{
    play::close();
    const size_t sl = game_dir.rfind('/');
    pc::card_root() = game_dir.substr(0, sl);
    static std::string folder;
    folder = game_dir.substr(sl + 1);
    if (play::open(folder.c_str(), games::Game::DarkQueenOfKrynn, C, nullptr)) return false;
    for (int guard = 0; play::screen == play::Screen::Title && guard < 40; ++guard) {
        if (play::title_run && play::title_run->prompt) {
            tap_word('P');
            continue;
        }
        play::tap(2, 2, C);
    }
    return play::screen == play::Screen::PartyMenu;
}

} // namespace bq

static void behave_queen()
{
    using namespace bq;
    if (!b_oracle_load("queen")) {
        expect(P, "oracle file", false, "no oracle_queen.txt");
        return;
    }
    if (!open_game()) {
        expect(P, "the game opened", false);
        return;
    }
    if (!play::load_rules(bq::m)) {
        expect(P, "rule tables", false);
        return;
    }
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("create")) creation();
    if (want("train")) training();
    if (want("knights")) knights();
    if (want("spells")) spell_rows();
    if (want("monsters")) monsters();
}

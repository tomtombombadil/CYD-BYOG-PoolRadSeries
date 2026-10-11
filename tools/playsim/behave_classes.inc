// Behaviour checks: classes (included by sweep.cpp with -DBEHAVE_CLASSES).
// Every race and class Curse offers, made, levelled and played, against
// what the original does: the expected values come from the oracle file
// (coab's tables and the program's listing, AD&D), never from the engine.
// The rule tables the engine reads from the player's program are used
// only as the engine's own input.
#include "behave_oracle.inc"

namespace bcls {

using namespace play;
Making* m = nullptr;                            // the engine's rule tables (as the game loads them)
const char* P = "classes";

// ---- small helpers
void set_stat(party::Character& c, int i, int v)
{
    c.rec[0x10 + i * 2] = static_cast<uint8_t>(v);
    c.rec[0x11 + i * 2] = static_cast<uint8_t>(v);
}
void put32(uint8_t* r, int o, uint32_t v)
{
    for (int i = 0; i < 4; ++i) r[o + i] = static_cast<uint8_t>(v >> (8 * i));
}
// A bare character: race, class id, all stats `stat`, no levels
void bare(party::Character& c, int race, int cls, int stat = 12)
{
    c = party::Character{};
    c.rec[0x74] = static_cast<uint8_t>(race);
    c.rec[0x75] = static_cast<uint8_t>(cls);
    for (int i = 0; i < 6; ++i) set_stat(c, i, stat);
    c.rec[0x11C] = 2;
    c.rec[0x124] = 50;
    c.rec[0x125] = 1;
    c.rec[0xE4] = 12;
    c.rec[0x196] = 1;
}
void levels(party::Character& c, const int lv[8])
{
    int hd = 0;
    for (int k = 0; k < 8; ++k) {
        c.rec[0x109 + k] = static_cast<uint8_t>(lv[k]);
        if (lv[k] > hd) hd = lv[k];
    }
    c.rec[0xE5] = static_cast<uint8_t>(hd);
}
int oxp(int cls, int lvl)          // experience to go from lvl to lvl + 1 (oracle; -1 none)
{
    const auto* r = b_row("xp", cls);
    if (!r || lvl < 1 || lvl >= static_cast<int>(r->size())) return -1;
    return (*r)[lvl];
}
int othac0(int cls, int lvl)
{
    if (cls == 1 || cls == 7) cls = cls == 1 ? 0 : 0;      // (druid / monk: as the cleric's row at level 0)
    const auto* r = b_row("thac0", cls);
    return r && lvl + 1 < static_cast<int>(r->size()) ? (*r)[lvl + 1] : 40;
}
const std::vector<int>* osave(int cls, int lvl) { return b_row("save", cls, lvl); }

// The class levels the multi-class ids hold
int class_bits(int cls)
{
    static const uint8_t bits[17] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
                                     0x05, 0x25, 0x11, 0x21, 0x41, 0x24, 0x44, 0x64, 0x60};
    return cls >= 0 && cls < 17 ? bits[cls] : 0;
}

// The original's race level limits for training (listing ovr018:4DF6-4F6E)
bool train_limit(int race, int k, int lvl, int str, int in)
{
    switch (race) {
    case 1: return k == 2 && (lvl == 9 || (lvl == 8 && str == 17) || (lvl == 7 && str < 17));
    case 2:
        if (k == 2) return lvl == 7 || (lvl == 6 && str == 17) || (lvl == 5 && str < 17);
        if (k == 5) return lvl == 11 || (lvl == 9 && in < 17) || (lvl == 10 && in == 17);
        return false;
    case 3: return k == 2 && (lvl == 6 || (lvl == 5 && str < 18));
    case 4:
        if (k == 0) return lvl == 5;
        if (k == 2 || k == 4) return lvl == 8 || (lvl == 7 && str == 17) || (lvl == 6 && str < 17);
        if (k == 5) return lvl == 8 || (lvl == 7 && in == 17) || (lvl == 6 && in < 17);
        return false;
    case 5: return k == 2 && (lvl == 6 || (lvl == 5 && str == 17) || (lvl == 4 && str < 17));
    default: return false;
    }
}

// Constitution's hit points a die for the levels (get_con_hp_adj, oracle's con_hp)
int con_hp_of(int con, bool fighter_type)
{
    const auto& rows = b_rows("con_hp");
    int best = 0, extra = 0, at = -1;
    for (const auto& r : rows)
        if (r[0] <= con && r[0] > at) {
            at = r[0];
            best = r[1];
            extra = r[2];
        }
    return best + (fighter_type ? extra : 0);
}
int max_hd(int k) { const auto* r = b_row("hp", k); return r ? (*r)[3] : 99; }
int die_of(int k) { const auto* r = b_row("hp", k); return r ? (*r)[1] : 0; }
int count_of(int k) { const auto* r = b_row("hp", k); return r ? (*r)[2] : 1; }
int fixed_of(int k) { const auto* r = b_row("hp", k); return r ? (*r)[4] : 0; }
int con_adj(const int lv[8], int con, int cls_id)
{
    int adj = 0;
    for (int k = 0; k < 8; ++k) {
        if (lv[k] <= 0 || lv[k] >= max_hd(k)) continue;
        adj += con_hp_of(con, cls_id == 2 || cls_id == 3 || cls_id == 4) - (cls_id == 2 || cls_id == 3 || cls_id == 4 ? 0 : 0);
        if (k == 4 && lv[k] == 1) adj *= 2;
    }
    return adj;
}
// The hit points the original rolls for the classes in mask (sub_509E0) with every die `face` (0: its top)
int hp_dice(const int lv[8], int mask_classes, int face)
{
    int total = 0;
    for (int k = 0; k < 8; ++k) {
        if (lv[k] <= 0 || !(mask_classes & (1 << k))) continue;
        if (lv[k] < max_hd(k)) {
            const int n = lv[k] > 1 ? 1 : count_of(k);
            total += n * (face ? face : die_of(k));
        } else {
            total = fixed_of(k);             // (the original sets, it doesn't add)
        }
    }
    return total;
}

// ---- 1. The tables: THAC0, saves, spell slots, thief skills, attacks, by level
void tables()
{
    static party::Character c;
    const int single[6] = {0, 2, 3, 4, 5, 6};
    for (int k : single)
        for (int L = 1; L <= 12; ++L) {
            if (oxp(k, L - 1) < 0 && L > 1) continue;          // (past the class's last level)
            bare(c, 7, k);
            int lv[8] = {};
            lv[k] = L;
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            // THAC0: the best of every class's table at its level (0 for those not held)
            int want = 0;
            for (int j = 0; j < 7; ++j) want = std::max(want, othac0(j, j == k ? L : 0));
            char w[64];
            snprintf(w, sizeof w, "THAC0 class %d level %d", k, L);
            expect(P, w, c.rec[0x73] == want, "%d (shown %d), the original's %d (%d)", c.rec[0x73], 60 - c.rec[0x73], want,
                   60 - want);
            const auto* s = osave(k, L);
            if (s) {
                bool ok = true;
                for (int i = 0; i < 5; ++i) ok = ok && c.rec[0xDF + i] == (*s)[2 + i];
                snprintf(w, sizeof w, "saves class %d level %d", k, L);
                expect(P, w, ok, "%d %d %d %d %d, the original's %d %d %d %d %d", c.rec[0xDF], c.rec[0xE0], c.rec[0xE1],
                       c.rec[0xE2], c.rec[0xE3], (*s)[2], (*s)[3], (*s)[4], (*s)[5], (*s)[6]);
            }
            // Spell slots
            const char* key = k == 0 ? "cleric_slots" : k == 5 ? "mu_slots" : k == 3 ? "paladin_slots" : k == 4 ? "ranger_slots" : nullptr;
            const auto* sl = key ? b_row(key, L) : nullptr;
            if (sl) {
                int got[5];
                for (int i = 0; i < 5; ++i) {
                    if (k == 0 || k == 3) got[i] = c.rec[0x12D + i];
                    else if (k == 5) got[i] = c.rec[0x12D + 10 + i];
                    else got[i] = i < 3 ? c.rec[0x12D + 5 + i] : c.rec[0x12D + 10 + i - 3];
                }
                bool ok = true;
                for (int i = 0; i < 5; ++i) ok = ok && got[i] == (*sl)[1 + i];
                snprintf(w, sizeof w, "spell slots class %d level %d", k, L);
                expect(P, w, ok, "%d %d %d %d %d, the rules' %d %d %d %d %d", got[0], got[1], got[2], got[3], got[4], (*sl)[1],
                       (*sl)[2], (*sl)[3], (*sl)[4], (*sl)[5]);
            }
            // Attacks a round (half attacks): 3 for fighters / paladins from 7, rangers from 8
            const int att = ((k == 2 || k == 3) && L >= 7) || (k == 4 && L >= 8) ? 3 : 2;
            snprintf(w, sizeof w, "attacks class %d level %d", k, L);
            expect(P, w, c.rec[0x11C] == att, "%d half attacks, the original's %d", c.rec[0x11C], att);
            // Thief skills (human, Dexterity 14: no adjustments)
            if (k == 6) {
                set_stat(c, 3, 14);
                classes::class_bonuses(c, m->tables);
                const auto* tb = b_row("thief_base", L);
                bool ok = tb != nullptr;
                for (int i = 0; ok && i < 8; ++i) ok = c.rec[0xEA + i] == (*tb)[1 + i];
                snprintf(w, sizeof w, "thief skills level %d", L);
                expect(P, w, ok, "%d %d %d %d %d %d %d %d", c.rec[0xEA], c.rec[0xEB], c.rec[0xEC], c.rec[0xED], c.rec[0xEE],
                       c.rec[0xEF], c.rec[0xF0], c.rec[0xF1]);
            }
        }
    // Wisdom's extra cleric spells (cleric level 7: slots 3 3 2 1; level 5: 3 3 1)
    for (int L : {5, 7, 9})
        for (int wis = 12; wis <= 18; ++wis) {
            bare(c, 7, 0);
            set_stat(c, 2, wis);
            int lv[8] = {};
            lv[0] = L;
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            const auto* base = b_row("cleric_slots", L);
            const auto* bon = b_row("wis_bonus", wis);
            if (!base || !bon) continue;
            bool ok = true;
            int want[5];
            for (int i = 0; i < 5; ++i) {
                want[i] = (*base)[1 + i] + ((*base)[1 + i] > 0 ? (*bon)[1 + i] : 0);
                ok = ok && c.rec[0x12D + i] == want[i];
            }
            char w[64];
            snprintf(w, sizeof w, "Wisdom %d cleric level %d slots", wis, L);
            expect(P, w, ok, "%d %d %d %d %d, the rules' %d %d %d %d %d", c.rec[0x12D], c.rec[0x12E], c.rec[0x12F], c.rec[0x130],
                   c.rec[0x131], want[0], want[1], want[2], want[3], want[4]);
        }
    // Thief skills by race (level 1, Dexterity 14) and by Dexterity (level 5, human)
    for (const auto& rr : b_rows("thief_race")) {
        const int race = rr[0];
        for (int L : {1, 5}) {
            bare(c, race, 6);
            set_stat(c, 3, 14);
            int lv[8] = {};
            lv[6] = L;
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            const auto* tb = b_row("thief_base", L);
            bool ok = true;
            int want[8];
            for (int i = 0; i < 8; ++i) {
                const int adj = rr[1 + i], base = (*tb)[1 + i];
                want[i] = adj < 0 && base < -adj ? 0 : base + adj;      // (too little to take the minus: 0)
                ok = ok && c.rec[0xEA + i] == want[i];
            }
            char w[64];
            snprintf(w, sizeof w, "thief skills race %d level %d", race, L);
            expect(P, w, ok, "%d %d %d %d %d %d %d %d, the rules' %d %d %d %d %d %d %d %d", c.rec[0xEA], c.rec[0xEB], c.rec[0xEC],
                   c.rec[0xED], c.rec[0xEE], c.rec[0xEF], c.rec[0xF0], c.rec[0xF1], want[0], want[1], want[2], want[3], want[4],
                   want[5], want[6], want[7]);
        }
    }
    for (const auto& dr : b_rows("thief_dex")) {
        bare(c, 7, 6);
        set_stat(c, 3, dr[0]);
        int lv[8] = {};
        lv[6] = 5;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        const auto* tb = b_row("thief_base", 5);
        bool ok = true;
        int want[8];
        for (int i = 0; i < 8; ++i) {
            want[i] = (*tb)[1 + i] + (i < 5 ? dr[1 + i] : 0);
            ok = ok && c.rec[0xEA + i] == want[i];
        }
        char w[64];
        snprintf(w, sizeof w, "thief skills Dexterity %d", dr[0]);
        expect(P, w, ok, "%d %d %d %d %d, the rules' %d %d %d %d %d", c.rec[0xEA], c.rec[0xEB], c.rec[0xEC], c.rec[0xED],
               c.rec[0xEE], want[0], want[1], want[2], want[3], want[4]);
    }
    // Multi-classes: THAC0 the best, each save the best of the classes
    const int multi_lv[][3] = {{3, 4, 5}, {6, 5, 4}, {7, 8, 9}};
    for (int cls = 8; cls <= 16; ++cls)
        for (const auto& ml : multi_lv) {
            bare(c, 4, cls);
            int lv[8] = {}, n = 0;
            for (int k = 0; k < 8; ++k)
                if (class_bits(cls) & (1 << k)) lv[k] = ml[n++];
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            int want = 0, sv[5] = {20, 20, 20, 20, 20};
            for (int k = 0; k < 7; ++k) {
                want = std::max(want, othac0(k, lv[k]));
                if (lv[k] <= 0) continue;
                const auto* s = osave(k, lv[k]);
                for (int i = 0; s && i < 5; ++i) sv[i] = std::min(sv[i], (*s)[2 + i]);
            }
            bool sok = true;
            for (int i = 0; i < 5; ++i) sok = sok && c.rec[0xDF + i] == sv[i];
            char w[80];
            snprintf(w, sizeof w, "multi-class %d levels %d/%d/%d: THAC0 the best", cls, ml[0], ml[1], ml[2]);
            expect(P, w, c.rec[0x73] == want, "%d, the original's %d", c.rec[0x73], want);
            snprintf(w, sizeof w, "multi-class %d levels %d/%d/%d: saves the best", cls, ml[0], ml[1], ml[2]);
            expect(P, w, sok, "%d %d %d %d %d, the original's %d %d %d %d %d", c.rec[0xDF], c.rec[0xE0], c.rec[0xE1], c.rec[0xE2],
                   c.rec[0xE3], sv[0], sv[1], sv[2], sv[3], sv[4]);
        }
    // Saves: dwarves' and halflings' Constitution (added to the poison number, as the
    // original does - listing ovr026:09D7), Con 19+ for everyone
    struct PC { int race, con, add; };
    const PC pcs[] = {{1, 4, 1}, {1, 9, 2}, {1, 12, 3}, {5, 15, 4}, {5, 18, 5}, {1, 19, 1}, {7, 18, 0}, {7, 19, 1},
                      {7, 21, 2}, {2, 14, 0}, {3, 14, 0}};
    for (const PC& pc : pcs) {
        bare(c, pc.race, 2);
        set_stat(c, 4, pc.con);
        int lv[8] = {};
        lv[2] = 1;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        const auto* s = osave(2, 1);
        char w[64];
        snprintf(w, sizeof w, "poison save race %d Con %d", pc.race, pc.con);
        expect(P, w, s && c.rec[0xDF] == (*s)[2] + pc.add, "%d, the original's %d", c.rec[0xDF], s ? (*s)[2] + pc.add : -1);
    }
}

// ---- 2. Creation: every race, class, alignment
void creation()
{
    static party::Character c;
    for (const auto& rc : b_rows("race_classes")) {
        const int race = rc[0];
        int cl[32];
        const int nc = create::classes_for(m->tables, race, cl, 32);
        bool same = nc == static_cast<int>(rc.size()) - 1;
        for (int i = 0; same && i < nc; ++i) same = cl[i] == rc[1 + i];
        char w[80];
        snprintf(w, sizeof w, "race %d: the classes offered (in order)", race);
        expect(P, w, same, "%d classes", nc);
        const auto* lim = b_row("race_lim", race);
        for (int ci = 0; ci < nc; ++ci) {
            const int cls = cl[ci];
            int al[16];
            const int na = create::alignments_for(m->tables, cls, al, 16);
            const auto* oa = b_row("class_align", cls);
            bool asame = oa && na == static_cast<int>(oa->size()) - 1;
            for (int i = 0; asame && i < na; ++i) asame = al[i] == (*oa)[1 + i];
            if (race == 7 || race == 4) {      // (once a class)
                snprintf(w, sizeof w, "class %d: the alignments offered", cls);
                expect(P, w, asame, "%d", na);
            }
            const auto* xr = b_row("start_xp", cls);
            const auto* mn = b_row("class_min", cls);
            for (int ai = 0; ai < na; ++ai)
                for (int sex = 0; sex < 2; ++sex) {
                    dice_usual();
                    create::begin(c, m->tables, m->facts, m->dice, race, sex, cls, al[ai]);
                    create::roll(c, m->tables, m->facts, m->dice);
                    char tag[48];
                    snprintf(tag, sizeof tag, "race %d class %d align %d sex %d", race, cls, al[ai], sex);
                    // Experience as made
                    if (xr && c.exp() != static_cast<uint32_t>((*xr)[1])) {
                        snprintf(w, sizeof w, "%s: starting experience", tag);
                        expect(P, w, false, "%u, the original's %d", c.exp(), (*xr)[1]);
                    }
                    // Levels: what the experience buys (the race's limits)
                    bool lok = true;
                    for (int k = 0; k < 8; ++k) {
                        const bool has = class_bits(cls) & (1 << k);
                        int want = has ? 1 : 0;
                        while (has) {
                            const int need = oxp(k, want);
                            if (need <= 0 || static_cast<int>(c.exp()) < need ||
                                train_limit(race, k, want, c.rec[0x11], c.rec[0x13]))
                                break;
                            ++want;
                        }
                        if (c.level(k) != want) lok = false;
                    }
                    if (!lok) {
                        snprintf(w, sizeof w, "%s: levels after silent training", tag);
                        expect(P, w, false, "%d/%d/%d/%d/%d/%d/%d", c.level(0), c.level(2), c.level(3), c.level(4), c.level(5),
                               c.level(6), c.level(7));
                    }
                    // Stats within the race's / sex's limits and the class's minimums
                    bool sok = true;
                    for (int i = 0; i < 6; ++i) {
                        const int v = c.stat(i);
                        int lo, hi;
                        if (i == 0) {
                            lo = (*lim)[1 + sex];
                            hi = (*lim)[3 + sex];
                        } else {
                            lo = (*lim)[7 + (i - 1) * 2];
                            hi = (*lim)[8 + (i - 1) * 2];
                        }
                        int need = mn ? (*mn)[1 + i] : 0;
                        if (i == 2 && cls >= 8 && cls <= 12) need = std::max(need, 13);
                        if (v > hi || v < std::max(lo, need) || c.stat_now(i) != v) sok = false;
                    }
                    const bool strong = c.level(2) || c.level(3) || c.level(4);
                    const int e = c.rec[0x1D];
                    if (e && (c.stat(0) != 18 || !strong || e > (*lim)[5 + sex])) sok = false;
                    if (!sok) {
                        snprintf(w, sizeof w, "%s: stats within the limits", tag);
                        expect(P, w, false, "%d(%d) %d %d %d %d %d", c.stat(0), e, c.stat(1), c.stat(2), c.stat(3), c.stat(4), c.stat(5));
                    }
                    // Money: exactly 300 platinum
                    bool mok = true;
                    for (int i = 0; i < 7; ++i) mok = mok && c.money(i) == (i == 4 ? 300 : 0);
                    if (!mok) {
                        snprintf(w, sizeof w, "%s: 300 platinum", tag);
                        expect(P, w, false);
                    }
                    // The race's and class's effects
                    std::vector<int> fx;
                    if (const auto* rf = b_row("race_fx", race)) fx.assign(rf->begin() + 1, rf->end());
                    if (const auto* cf = b_row("class_fx", cls)) fx.insert(fx.end(), cf->begin() + 1, cf->end());
                    bool fok = static_cast<int>(fx.size()) == c.n_affects;
                    for (int t : fx) fok = fok && c.has_affect(static_cast<uint8_t>(t));
                    if (!fok) {
                        snprintf(w, sizeof w, "%s: the race's / class's effects", tag);
                        expect(P, w, false, "%d effects", c.n_affects);
                    }
                    // Spells: a magic-user's book (the four, then silent training's)
                    if (c.level(5)) {
                        std::vector<int> want;
                        if (const auto* f = b_row("mu_first", INT32_MIN)) (void)f;
                        for (const auto& r : b_rows("mu_first")) want.assign(r.begin(), r.end());
                        for (const auto& r : b_rows("mu_silent"))
                            if (r[0] <= c.level(5)) want.insert(want.end(), r.begin() + 1, r.end());
                        int n = 0;
                        bool bok = true;
                        for (int s = 1; s <= 100; ++s)
                            if (c.rec[0x79 + s - 1] && m->tables.spell_class(s) == 2) {
                                ++n;
                                if (std::find(want.begin(), want.end(), s) == want.end()) bok = false;
                            }
                        bok = bok && n == static_cast<int>(want.size());
                        if (!bok || (race == 7 && sex == 0 && ai == 0)) {
                            std::string got;
                            for (int s = 1; s <= 100; ++s)
                                if (c.rec[0x79 + s - 1] && m->tables.spell_class(s) == 2) got += " " + std::to_string(s);
                            snprintf(w, sizeof w, "%s: magic-user level %d spell book", tag, c.level(5));
                            expect(P, w, bok, "%s", got.c_str());
                        }
                    }
                    // A cleric's: every cleric spell of a level they have slots for, but
                    // Animate Dead (0x24) (sub_6A00F)
                    if (c.level(0)) {
                        bool bok = true;
                        for (int s = 1; s < m->tables.lay.spell_count && s <= 100; ++s) {
                            if (m->tables.spell_class(s) != 0) continue;
                            const int sl = m->tables.spell_level(s);
                            const bool want = s != 0x24 && sl >= 1 && sl <= 5 && c.rec[0x12D + sl - 1] > 0;
                            if ((c.rec[0x79 + s - 1] != 0) != want) bok = false;
                        }
                        if (!bok || (race == 7 && sex == 0 && ai == 0)) {
                            snprintf(w, sizeof w, "%s: cleric level %d spell book", tag, c.level(0));
                            expect(P, w, bok);
                        }
                    }
                    // Hit points: at least 1 a level held
                    if (c.hp_max() < 1 || c.hp() != c.hp_max()) {
                        snprintf(w, sizeof w, "%s: hit points", tag);
                        expect(P, w, false, "%d/%d", c.hp(), c.hp_max());
                    }
                }
        }
    }
    expect(P, "every race / class / alignment / sex made (the checks above report only failures)", true);
}

// ---- 3. Creation with the dice fixed: stats, age, exceptional strength, hit points
void fixed_dice()
{
    static party::Character c;
    for (const auto& rc : b_rows("race_classes")) {
        const int race = rc[0];
        const auto* lim = b_row("race_lim", race);
        const auto* br = b_row("age_bracket", race);
        for (size_t ci = 1; ci < rc.size(); ++ci) {
            const int cls = rc[ci];
            const auto* mn = b_row("class_min", cls);
            for (int high = 0; high < 2; ++high)
                for (int sex = 0; sex < 2; ++sex) {
                    if (high) dice_high();
                    else dice_low();
                    int al[16];
                    create::alignments_for(m->tables, cls, al, 16);
                    create::begin(c, m->tables, m->facts, m->dice, race, sex, cls, al[0]);
                    create::roll(c, m->tables, m->facts, m->dice);
                    dice_usual();
                    char tag[48], w[96];
                    snprintf(tag, sizeof tag, "race %d class %d sex %d dice %s", race, cls, sex, high ? "high" : "low");
                    // Age
                    int entry = cls <= 7 ? cls : (cls == 14 ? 2 : (cls == 13 || cls == 15 || cls == 16) ? 6 : 0);
                    const auto* ag = b_row("age", race, entry);
                    int want_age = -1;
                    if (ag) want_age = cls <= 7 ? (*ag)[2] + (*ag)[3] * (high ? (*ag)[4] : ((*ag)[4] ? 1 : 0)) : (*ag)[2] + (*ag)[3] * (*ag)[4];
                    snprintf(w, sizeof w, "%s: age", tag);
                    expect(P, w, c.age() == want_age, "%d, the original's %d", c.age(), want_age);
                    // Stats: 3d6+1 (all 19 / all 4), the age's effects, the race's / sex's limits, the class's minimums
                    int want[6];
                    for (int i = 0; i < 6; ++i) {
                        int v = high ? 19 : 4;
                        const auto* ae = b_row("age_effect", i);
                        for (int b = 0; b < 4 && br && ae; ++b)
                            if (v > 0 && (*br)[1 + b] < c.age()) v += (*ae)[1 + b];
                        int lo, hi;
                        if (i == 0) {
                            lo = (*lim)[1 + sex];
                            hi = (*lim)[3 + sex];
                        } else {
                            lo = (*lim)[7 + (i - 1) * 2];
                            hi = (*lim)[8 + (i - 1) * 2];
                        }
                        v = std::min(v, hi);
                        v = std::max(v, lo);
                        if (mn) v = std::max(v, (*mn)[1 + i]);
                        if (i == 2 && cls >= 8 && cls <= 12) v = std::max(v, 13);
                        want[i] = v;
                    }
                    bool ok = true;
                    for (int i = 0; i < 6; ++i) ok = ok && c.stat(i) == want[i];
                    snprintf(w, sizeof w, "%s: stats", tag);
                    expect(P, w, ok, "%d %d %d %d %d %d, the original's %d %d %d %d %d %d", c.stat(0), c.stat(1), c.stat(2), c.stat(3),
                           c.stat(4), c.stat(5), want[0], want[1], want[2], want[3], want[4], want[5]);
                    // Exceptional strength: fighters, paladins, rangers with 18 (d100, held to the race's / sex's)
                    const bool strong = class_bits(cls) & 0x1C;
                    const int e_want = want[0] == 18 && strong ? std::min(high ? 100 : 1, (*lim)[5 + sex]) : 0;
                    snprintf(w, sizeof w, "%s: exceptional strength", tag);
                    expect(P, w, c.rec[0x1D] == e_want, "18/%d, the original's %d", c.rec[0x1D], e_want);
                    // Hit points: the original's rolls (each die its top / 1), Constitution, the classes shared
                    int lv[8] = {};
                    int held = 0, mask = 0;
                    for (int k = 0; k < 8; ++k)
                        if (class_bits(cls) & (1 << k)) {
                            lv[k] = 1;
                            ++held;
                            mask |= 1 << k;
                        }
                    const int con = want[4];
                    int rolled = hp_dice(lv, mask, high ? 0 : 1);
                    int adj = con_adj(lv, con, cls);
                    int hp = adj < 0 ? (rolled > -adj + held ? (rolled + adj) / held : 1) : (rolled + adj) / held;
                    int rolled_kept = rolled / held;
                    // silent training, round by round
                    const uint32_t xp = c.exp();
                    for (int round = 0; round < 20; ++round) {
                        int tm = 0;
                        for (int k = 0; k < 8; ++k) {
                            if (!lv[k]) continue;
                            const int need = oxp(k, lv[k]);
                            if (need > 0 && static_cast<int>(xp) >= need && !train_limit(race, k, lv[k], want[0], want[1]))
                                tm |= 1 << k;
                        }
                        if (!tm) break;
                        for (int k = 0; k < 8; ++k)
                            if (tm & (1 << k)) ++lv[k];
                        const int gain = hp_dice(lv, tm, high ? 0 : 1);
                        int inc = gain / held;
                        if (!inc) inc = 1;
                        rolled_kept += inc;
                        int up = (gain + con_adj(lv, con, cls)) / held;
                        if (up < 1) up = 1;
                        hp += up;
                    }
                    snprintf(w, sizeof w, "%s: hit points", tag);
                    expect(P, w, c.hp_max() == hp && c.hp() == hp, "%d, the original's %d (levels %d/%d/%d/%d/%d/%d)", c.hp_max(), hp,
                           c.level(0), c.level(2), c.level(3), c.level(4), c.level(5), c.level(6));
                    snprintf(w, sizeof w, "%s: rolled hit points (record 0x12C)", tag);
                    expect(P, w, c.rec[0x12C] == rolled_kept, "%d, the original's %d", c.rec[0x12C], rolled_kept);
                }
        }
    }
}

// ---- 4. Levelling: experience needed, race limits, hit points, training
void levelling()
{
    static party::Character c;
    const int single[6] = {0, 2, 3, 4, 5, 6};
    // Experience: one short of a level isn't enough, the level's figure is; the last level trains no more
    for (int k : single)
        for (int L = 1; L <= 12; ++L) {
            bare(c, 7, k, 18);
            int lv[8] = {};
            lv[k] = L;
            levels(c, lv);
            const int need = oxp(k, L);
            const int bit = m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + k));
            char w[64];
            if (need <= 0) {
                put32(c.rec, 0x127, 3000000);
                snprintf(w, sizeof w, "class %d level %d: the last level", k, L);
                expect(P, w, !(create::trainable(c, m->tables) & bit), "trainable with 3,000,000");
                break;
            }
            put32(c.rec, 0x127, static_cast<uint32_t>(need - 1));
            const bool short_ok = !(create::trainable(c, m->tables) & bit);
            put32(c.rec, 0x127, static_cast<uint32_t>(need));
            const bool exact_ok = (create::trainable(c, m->tables) & bit) != 0;
            snprintf(w, sizeof w, "class %d level %d -> %d at %d experience", k, L, L + 1, need);
            expect(P, w, short_ok && exact_ok, "%s", !short_ok ? "trains one short" : !exact_ok ? "won't train at the figure" : "");
        }
    // The races' level limits (the original's rules: Strength / Intelligence)
    for (int race : {1, 2, 3, 4, 5})
        for (int k : {0, 2, 4, 5})
            for (int str : {16, 17, 18})
                for (int L = 3; L <= 11; ++L) {
                    bare(c, race, k, 12);
                    set_stat(c, 0, str);
                    set_stat(c, 1, str);           // (Intelligence the same: the magic-users' limits)
                    int lv[8] = {};
                    lv[k] = L;
                    levels(c, lv);
                    put32(c.rec, 0x127, 3000000);
                    const int bit = m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + k));
                    const bool want = oxp(k, L) > 0 && !train_limit(race, k, L, str, str);
                    const bool got = (create::trainable(c, m->tables) & bit) != 0;
                    if (got != want) {
                        char w[80];
                        snprintf(w, sizeof w, "race %d class %d level %d Str/Int %d: may train", race, k, L, str);
                        expect(P, w, false, "%s, the original %s", got ? "trains" : "doesn't", want ? "does" : "doesn't");
                    }
                }
    expect(P, "race level limits (failures listed above)", true);
    // Hit points a level: each class, every level, the dice at their top, Con 16 (fighters 18)
    for (int k : single)
        for (int L = 1; L <= 11; ++L) {
            if (oxp(k, L) <= 0) break;
            const int con = (k == 2 || k == 3 || k == 4) ? 18 : 16;
            bare(c, 7, k, 12);
            set_stat(c, 4, con);
            int lv[8] = {};
            lv[k] = L;
            levels(c, lv);
            c.rec[0x78] = 100;
            c.rec[0x1A4] = 90;
            c.rec[0x12C] = 80;
            dice_high();
            const int bit = m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + k));
            create::train_classes(c, m->tables, m->facts, m->dice, bit, false);
            dice_usual();
            lv[k] = L + 1;
            const int gain = hp_dice(lv, 1 << k, 0);
            int up = gain + con_adj(lv, con, k);
            if (up < 1) up = 1;
            char w[64];
            snprintf(w, sizeof w, "class %d level %d -> %d: hit points", k, L, L + 1);
            expect(P, w, c.hp_max() == 100 + up && c.hp() == 90 + up && c.rec[0x12C] == 80 + gain,
                   "+%d (rolled +%d), the original's +%d (+%d)", c.hp_max() - 100, c.rec[0x12C] - 80, up, gain);
        }
    // Multi-class: the hit points shared by the classes held (one class trained)
    {
        bare(c, 2, 13, 12);
        set_stat(c, 4, 16);
        int lv[8] = {};
        lv[2] = 3;
        lv[5] = 3;
        levels(c, lv);
        c.rec[0x78] = 40;
        c.rec[0x1A4] = 40;
        c.rec[0x12C] = 30;
        dice_high();
        create::train_classes(c, m->tables, m->facts, m->dice, m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + 2)), false);
        dice_usual();
        lv[2] = 4;
        const int gain = hp_dice(lv, 1 << 2, 0);
        const int up = (gain + con_adj(lv, 16, 13)) / 2;
        expect(P, "F/MU fighter 3 -> 4: hit points shared by 2", c.hp_max() == 40 + up && c.rec[0x12C] == 30 + gain / 2,
               "+%d (rolled +%d), the original's +%d (+%d)", c.hp_max() - 40, c.rec[0x12C] - 30, up, gain / 2);
    }
    // Training lost levels back (record 0xE7 levels, 0xE8 hit points): a share of
    // the lost hit points is no longer owed (listing ovr018:5502-552E)
    {
        bare(c, 7, 2, 12);
        int lv[8] = {};
        lv[2] = 4;
        levels(c, lv);
        c.rec[0xE7] = 2;
        c.rec[0xE8] = 15;
        create::train_classes(c, m->tables, m->facts, m->dice, m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + 2)), false);
        expect(P, "training with levels lost: one level and its hit points no longer owed", c.rec[0xE7] == 1 && c.rec[0xE8] == 8,
               "lost levels %d hit points %d, the original's 1 and 8", c.rec[0xE7], c.rec[0xE8]);
    }
    // Restoration (spell 0x38): a lost level back - hit points lost / levels, the
    // lowest class up, experience raised to that level's figure (cast_restore)
    {
        bare(c, 7, 13, 12);
        c.rec[0x74] = 4;
        int lv[8] = {};
        lv[2] = 5;
        lv[5] = 4;
        levels(c, lv);
        c.rec[0x78] = 30;
        c.rec[0x1A4] = 20;
        c.rec[0x12C] = 25;
        c.rec[0xE7] = 2;
        c.rec[0xE8] = 14;
        put32(c.rec, 0x127, 1000);
        create::restore(c, m->tables);
        const int need = oxp(5, 4);
        expect(P, "Restoration: hit points, the lowest class up, experience raised",
               c.hp_max() == 37 && c.hp() == 27 && c.rec[0x12C] == 32 && c.rec[0xE7] == 1 && c.rec[0xE8] == 7 && c.level(5) == 5 &&
                   c.level(2) == 5 && static_cast<int>(c.exp()) == need,
               "HP %d/%d rolled %d lost %d/%d levels F%d MU%d xp %u (the original's 27/37 32 1/7 F5 MU5 %d)", c.hp(), c.hp_max(),
               c.rec[0x12C], c.rec[0xE7], c.rec[0xE8], c.level(2), c.level(5), c.exp(), need);
        // The original's Restoration has its own race limits (listing ovr025:3138):
        // an elf magic-user at 9 with Int 16 is raised (training's limit doesn't apply)
        bare(c, 2, 5, 12);
        set_stat(c, 1, 16);
        int lv2[8] = {};
        lv2[5] = 9;
        levels(c, lv2);
        c.rec[0xE7] = 1;
        c.rec[0xE8] = 4;
        create::restore(c, m->tables);
        expect(P, "Restoration: an elf magic-user 9 (Int 16) raised to 10 - its own race limits", c.level(5) == 10,
               "level %d", c.level(5));
    }
}

// ---- 5. Training at a hall (the game's own screens): cost, one level at a
// time, the class needing the most, experience held to one under the level after
void training_hall()
{
    if (!boot()) {
        expect(P, "boot", false);
        return;
    }
    party::Character& ch = pt->m[0];               // the paladin of the sample party (level 5)
    vm->set(0x7EA8, 0xFF);                           // a hall that trains every class
    pt->selected = 0;
    screen = Screen::PartyMenu;
    // Not enough experience
    put32(ch.rec, 0x127, 20000);
    const int gold0 = rules::gold_worth(ch);
    train_character(C);
    expect(P, "training: not enough experience", ch.level(3) == 5 && rules::gold_worth(ch) == gold0, "level %d", ch.level(3));
    end_training();
    screen = Screen::PartyMenu;
    // Experience for two levels: one level, 1000 gold, experience held to one under the next
    const int need6 = oxp(3, 5), need7 = oxp(3, 6);
    put32(ch.rec, 0x127, static_cast<uint32_t>(need7 + 5000));
    for (int i = 0; i < 7; ++i) ch.rec[0xFB + i * 2] = ch.rec[0xFC + i * 2] = 0;
    ch.rec[0xFB + 6] = 0xD0;                         // 2000 gold
    ch.rec[0xFC + 6] = 0x07;
    const int worth = rules::gold_worth(ch);
    train_character(C);
    if (screen == Screen::YesNo) train_yes_no(Ask::Train, 'Y', C);
    char w[120];
    snprintf(w, sizeof w, "training: one level at a time (paladin 5 with %d experience)", need7 + 5000);
    expect(P, w, ch.level(3) == 6, "level %d", ch.level(3));
    expect(P, "training: costs 1000 gold", rules::gold_worth(ch) == worth - 1000, "%d -> %d", worth, rules::gold_worth(ch));
    expect(P, "training: experience held to one under the level after (listing ovr018:5240)",
           static_cast<int>(ch.exp()) == need7 - 1, "%u, the original's %d", ch.exp(), need7 - 1);
    (void)need6;
    end_training();
    screen = Screen::PartyMenu;
}

// ---- 6. Human Change (dual class)
void dual_class()
{
    static party::Character c;
    char w[120];
    // Requirements: the present class's stats with a minimum of 9+ at 15+, the new class's at 17+
    {
        bare(c, 7, 2, 15);
        int lv[8] = {};
        lv[2] = 7;
        levels(c, lv);
        int to[16];
        int n = create::change_classes(c, m->tables, to, 16);
        expect(P, "Human Change: fighter with every stat 15 qualifies for nothing", n == 0, "%d classes", n);
        for (int i = 0; i < 6; ++i) set_stat(c, i, 17);
        c.rec[0x11B] = 4;                            // true neutral: no paladin, no ranger
        n = create::change_classes(c, m->tables, to, 16);
        std::string got;
        for (int i = 0; i < n; ++i) got += " " + std::to_string(to[i]);
        expect(P, "Human Change: fighter (all 17, neutral) may become cleric, magic-user, thief", n == 3 && to[0] == 0 && to[1] == 5 && to[2] == 6,
               "%s", got.c_str());
        set_stat(c, 0, 14);
        n = create::change_classes(c, m->tables, to, 16);
        expect(P, "Human Change: fighter with Str 14 (own value) doesn't qualify", n == 0, "%d", n);
    }
    // The change: experience 0, level 1, the old level kept, hit dice count kept,
    // a magic-user's four spells (listing ovr026:1195-11B0)
    {
        bare(c, 7, 2, 17);
        int lv[8] = {};
        lv[2] = 7;
        levels(c, lv);
        c.rec[0x78] = 60;
        c.rec[0x1A4] = 60;
        c.rec[0x12C] = 50;
        put32(c.rec, 0x127, 80000);
        classes::class_bonuses(c, m->tables);
        create::change_class(c, m->tables, m->facts, 5);
        std::string book;
        for (int s = 1; s <= 100; ++s)
            if (c.rec[0x79 + s - 1]) book += " " + std::to_string(s);
        std::vector<int> want;
        for (const auto& r : b_rows("mu_change")) want = r;
        bool bok = true;
        int n = 0;
        for (int s = 1; s <= 100; ++s)
            if (c.rec[0x79 + s - 1]) {
                ++n;
                bok = bok && std::find(want.begin(), want.end(), s) != want.end();
            }
        expect(P, "Human Change to magic-user: the spells learned", bok && n == static_cast<int>(want.size()), "%s", book.c_str());
        expect(P, "Human Change: experience 0, magic-user 1, fighter 7 kept as former, hit dice kept",
               c.exp() == 0 && c.level(5) == 1 && c.level(2) == 0 && c.old_level(2) == 7 && c.rec[0xE6] == 7 && c.rec[0xE5] == 1 &&
                   c.cls() == 5 && c.rec[0x11C] == 2,
               "xp %u MU %d F %d old %d E5 %d E6 %d attacks %d", c.exp(), c.level(5), c.level(2), c.old_level(2), c.rec[0xE5],
               c.rec[0xE6], c.rec[0x11C]);
        // Suspended: THAC0 is the magic-user's until the new level passes 7
        int want_t = 0;
        for (int j = 0; j < 7; ++j) want_t = std::max(want_t, othac0(j, j == 5 ? 1 : 0));
        expect(P, "Human Change: the old class's THAC0 suspended", c.rec[0x73] == want_t, "%d, the original's %d", c.rec[0x73], want_t);
        // No hit points while the new level isn't past the old
        put32(c.rec, 0x127, 2600);
        const int hp = c.hp_max();
        dice_high();
        create::train_classes(c, m->tables, m->facts, m->dice, m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + 5)), false);
        dice_usual();
        expect(P, "Human Change: no hit points for levels up to the old one", c.hp_max() == hp && c.level(5) == 2, "HP %d -> %d level %d",
               hp, c.hp_max(), c.level(5));
        // Past it (magic-user 8): the fighter's THAC0 and 3 attacks; the saves stay the
        // magic-user's (the original's saves loop only looks at monk's old level - listing ovr026:0927)
        c.rec[0x109 + 5] = 8;
        c.rec[0xE5] = 8;
        classes::class_bonuses(c, m->tables);
        int want2 = 0;
        for (int j = 0; j < 7; ++j) want2 = std::max(want2, othac0(j, j == 5 ? 8 : j == 2 ? 7 : 0));
        expect(P, "Human Change: past the old level, the old THAC0 counts", c.rec[0x73] == want2, "%d, the original's %d", c.rec[0x73], want2);
        expect(P, "Human Change: past the old level (fighter 7), 3 half attacks", c.rec[0x11C] == 3, "%d", c.rec[0x11C]);
        const auto* s = osave(5, 8);
        bool sok = true;
        for (int i = 0; s && i < 5; ++i) sok = sok && c.rec[0xDF + i] == (*s)[2 + i];
        expect(P, "Human Change: saves are the new class's only (the original)", sok, "%d %d %d %d %d", c.rec[0xDF], c.rec[0xE0],
               c.rec[0xE1], c.rec[0xE2], c.rec[0xE3]);
        // A level past: hit points again
        put32(c.rec, 0x127, 140000);
        const int hp2 = c.hp_max();
        dice_high();
        create::train_classes(c, m->tables, m->facts, m->dice, m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + 5)), false);
        dice_usual();
        snprintf(w, sizeof w, "Human Change: past the old level, hit points again (MU 8 -> 9: 4 + Con)");
        expect(P, w, c.hp_max() > hp2, "HP %d -> %d", hp2, c.hp_max());
    }
}

// ---- 7. Stats in play: Strength, Dexterity, carrying
void stats_in_play()
{
    static party::Character c;
    for (const auto& sr : b_rows("str")) {
        bare(c, 7, 2, 12);
        set_stat(c, 0, sr[0]);
        c.rec[0x1C] = c.rec[0x1D] = static_cast<uint8_t>(sr[1]);
        int lv[8] = {};
        lv[2] = 1;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        c.rec[0x11E] = 1;                            // bare hands: 1d2
        c.rec[0x120] = 2;
        rules::recalc(c, *names, d->facts);
        const int hit = static_cast<int8_t>(c.rec[0x199]) - static_cast<int8_t>(c.rec[0x73]);
        char w[64];
        snprintf(w, sizeof w, "Strength %d/%02d: to hit, damage, weight allowance", sr[0], sr[1]);
        expect(P, w, hit == sr[2] && c.damage_bonus() == sr[3] && rules::max_encumbrance(c) == sr[4], "%+d %+d %d, the rules' %+d %+d %d", hit,
               c.damage_bonus(), rules::max_encumbrance(c), sr[2], sr[3], sr[4]);
    }
    for (const auto& dr : b_rows("dex")) {
        bare(c, 7, 2, 12);
        set_stat(c, 3, dr[0]);
        int lv[8] = {};
        lv[2] = 1;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        rules::recalc(c, *names, d->facts);
        char w[64];
        snprintf(w, sizeof w, "Dexterity %d: AC bonus and missile to hit", dr[0]);
        expect(P, w, c.rec[0x19A] - 50 == dr[1] && combat::dex_reaction(dr[0]) == dr[2], "AC %+d missile %+d, the rules' %+d %+d",
               c.rec[0x19A] - 50, combat::dex_reaction(dr[0]), dr[1], dr[2]);
    }
    // Carrying: coins weigh 1 each; past the allowance by 512 / 768 / 1024: 9 / 6 / 3
    for (const auto& mr : b_rows("move")) {
        bare(c, 7, 2, 12);
        int lv[8] = {};
        lv[2] = 1;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        const int coins = rules::max_encumbrance(c) + mr[0];
        c.rec[0xFB] = static_cast<uint8_t>(coins);
        c.rec[0xFC] = static_cast<uint8_t>(coins >> 8);
        rules::recalc(c, *names, d->facts);
        char w[64];
        snprintf(w, sizeof w, "carrying %d over the allowance: movement", mr[0]);
        expect(P, w, c.movement() == mr[1], "%d, the original's %d", c.movement(), mr[1]);
    }
    // Attack level (sweeps): a fighter's level, everyone else 1
    for (int k : {2, 3, 4, 0}) {
        bare(c, 7, k, 12);
        int lv[8] = {};
        lv[k] = 6;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        rules::recalc(c, *names, d->facts);
        char w[64];
        snprintf(w, sizeof w, "class %d level 6: attack level (sweeps)", k);
        expect(P, w, c.rec[0xDD] == (k == 2 ? 6 : 1), "%d", c.rec[0xDD]);
    }
}

// ---- 8. The GOG sample party (made by the original): the engine's class values
// for their levels and stats are the ones the original wrote
void sample_party()
{
    static party::Character c, raw;
    for (int i = 1; i <= 6; ++i) {
        char path[400];
        snprintf(path, sizeof path, "%s/SAVE/CHRDATA%d.SAV", game_dir.c_str(), i);
        FILE* f = fopen(path, "rb");
        if (!f) {
            expect(P, "sample party file", false, "%s", path);
            return;
        }
        raw = party::Character{};
        const size_t n = fread(raw.rec, 1, party::kRecordSize, f);
        fclose(f);
        if (n != party::kRecordSize) continue;
        c = raw;
        classes::class_bonuses(c, m->tables);
        rules::recalc(c, *names, d->facts);
        char nm[20], w[96];
        raw.name(nm, sizeof nm);
        const int fields[] = {0x73, 0xDF, 0xE0, 0xE1, 0xE2, 0xE3, 0x11C, 0x12B, 0xE5, 0x199, 0x19A, 0x1A5, 0xDD};
        std::string bad;
        // Thief skills: the original adds a local it never sets (outside the
        // gloves' case - listing ovr026:0BD5), so its skills may all be one
        // amount above the table's; the engine's are the table's (that amount 0)
        if (raw.level(6)) {
            const int k = raw.rec[0xEA] - c.rec[0xEA];
            bool same = k >= 0;
            for (int o = 0xEA; o <= 0xF1; ++o) same = same && raw.rec[o] - c.rec[o] == k;
            snprintf(w, sizeof w, "sample party %s: thief skills the table's (the original's own + %d)", nm, k);
            expect(P, w, same);
        }
        for (int o : fields)
            if (c.rec[o] != raw.rec[o]) {
                char t[40];
                snprintf(t, sizeof t, " %03X:%d(orig %d)", o, c.rec[o], raw.rec[o]);
                bad += t;
            }
        for (int o = 0x12D; o < 0x13C; ++o)
            if (c.rec[o] != raw.rec[o]) {
                char t[40];
                snprintf(t, sizeof t, " slot%03X:%d(orig %d)", o, c.rec[o], raw.rec[o]);
                bad += t;
            }
        for (int o = 0x79; o < 0x79 + 100; ++o)
            if (c.rec[o] != raw.rec[o]) {
                char t[40];
                snprintf(t, sizeof t, " book%d:%d(orig %d)", o - 0x78, c.rec[o], raw.rec[o]);
                bad += t;
            }
        snprintf(w, sizeof w, "sample party %s: class values as the original wrote them", nm);
        expect(P, w, bad.empty(), "%s", bad.c_str());
        // Remade: the same race / class with silent training: the same spell book
        if (raw.level(5)) {
            static party::Character mk2;
            dice_usual();
            create::begin(mk2, m->tables, m->facts, m->dice, raw.race(), raw.sex(), raw.cls(), raw.alignment());
            create::roll(mk2, m->tables, m->facts, m->dice);
            // (levels follow the rolled stats; compare when they match the sample's)
            if (mk2.level(5) == raw.level(5)) {
                bool same = memcmp(mk2.rec + 0x79, raw.rec + 0x79, 100) == 0;
                snprintf(w, sizeof w, "sample party %s remade: the silent-training spell book", nm);
                std::string got;
                for (int s = 1; s <= 100; ++s)
                    if (mk2.rec[0x79 + s - 1]) got += " " + std::to_string(s);
                expect(P, w, same, "%s", got.c_str());
            }
        }
    }
}

// ---- 9. Experience after a fight: shares, prime requisites, multi-classes
void experience()
{
    if (!boot() || !start_fight()) {
        expect(P, "a fight for the experience checks", false);
        return;
    }
    combat::Battle& b = fg->b;
    // Party: give them stats / classes that test each rule
    struct Case { int cls; int lv[8]; int stat_i, stat_v; const char* what; int num, den; };
    // (num / den: the share's multiplier - 11/10 prime requisite, 1/2, 1/3)
    const Case cases[6] = {
        {2, {0, 0, 3, 0, 0, 0, 0, 0}, 0, 16, "fighter Str 16: +10%", 11, 10},
        {5, {0, 0, 0, 0, 0, 3, 0, 0}, 1, 15, "magic-user Int 15: no bonus", 1, 1},
        {13, {0, 0, 3, 0, 0, 3, 0, 0}, 0, 18, "F/MU: half", 1, 2},
        {16, {0, 0, 0, 0, 0, 3, 3, 0}, 3, 18, "MU/T: half (listing ovr006:04ED)", 1, 2},
        {15, {0, 0, 3, 0, 0, 3, 3, 0}, 0, 18, "F/MU/T: a third", 1, 3},
        {4, {0, 0, 0, 0, 3, 0, 0, 0}, 0, 16, "ranger Str/Int/Wis 16: +10%", 11, 10},
    };
    const int n = std::min(b.party_size, 6);
    uint32_t before[8] = {};
    for (int i = 0; i < n; ++i) {
        uint8_t* r = b.f[i].rec;
        const Case& cs = cases[i];
        r[0x75] = static_cast<uint8_t>(cs.cls);
        for (int k = 0; k < 8; ++k) r[0x109 + k] = static_cast<uint8_t>(cs.lv[k]);
        for (int s = 0; s < 6; ++s) r[0x11 + s * 2] = 12;
        r[0x11 + cs.stat_i * 2] = static_cast<uint8_t>(cs.stat_v);
        if (cs.cls == 4) r[0x13] = r[0x15] = 16;
        before[i] = r[0x127] | r[0x128] << 8 | r[0x129] << 16 | static_cast<uint32_t>(r[0x12A]) << 24;
    }
    // One of the party down (out of the fight): the share is still the party's (listing ovr006:0337)
    b.f[n - 1].rec[0x196] = 0;
    const int total = 6000;
    combat::award(b, total);
    const int share = total / b.party_size;
    for (int i = 0; i < n; ++i) {
        const uint8_t* r = b.f[i].rec;
        const uint32_t now = r[0x127] | r[0x128] << 8 | r[0x129] << 16 | static_cast<uint32_t>(r[0x12A]) << 24;
        const Case& cs = cases[i];
        int want = i == n - 1 ? 0 : (cs.num == 11 ? share + share / 10 : share * cs.num / cs.den);
        char w[96];
        snprintf(w, sizeof w, "experience: %s%s", cs.what, i == n - 1 ? " (out of the fight: none)" : "");
        expect(P, w, static_cast<int>(now - before[i]) == want, "+%d, the original's +%d (share %d of %d)", static_cast<int>(now - before[i]),
               want, share, total);
    }
}

// ---- 10. Racial abilities in fights: Constitution saves, half-elves' resistance
void racial()
{
    if (!boot() || !start_fight()) {
        expect(P, "a fight for the racial checks", false);
        return;
    }
    combat::Battle& b = fg->b;
    combat::Fighter& f = b.f[0];
    // Dwarves, gnomes, halflings (effect 0x61): + by Con against spells and wands
    // (listing ovr013:1E42): Con 14-17 +4
    f.rec[0x19] = 14;
    f.rec[0xDF + 4] = 15;
    f.rec[0xDF + 2] = 15;
    f.rec[0xDF + 3] = 15;
    f.rec[0x186] = 0;
    const bool had = f.has(0x61);
    if (!had && f.n_aff && *f.n_aff < combat::kMonsterAffects) {
        uint8_t* a = f.aff[(*f.n_aff)++];
        memset(a, 0, party::kAffectSize);
        a[0] = 0x61;
        a[3] = 0xFF;
    }
    dice_faces({11});
    const bool spell = combat::saving_throw(f, 4, 0, rng);
    dice_faces({11});
    const bool wand = combat::saving_throw(f, 2, 0, rng);
    dice_faces({11});
    const bool breath = combat::saving_throw(f, 3, 0, rng);
    dice_usual();
    expect(P, "Con saving bonus (dwarf / gnome / halfling): spells +4 at Con 14 (11 + 4 >= 15)", spell, "not saved");
    expect(P, "Con saving bonus: rods / staves / wands +4", wand, "not saved");
    expect(P, "Con saving bonus: nothing against breath", !breath, "saved");
    auto give = [](combat::Fighter& x, uint8_t type) {
        if (!x.n_aff || x.has(type)) return;
        uint8_t* a = x.aff[(*x.n_aff)++];
        memset(a, 0, party::kAffectSize);
        a[0] = type;
        a[3] = 0xFF;
    };
    auto drop = [](combat::Fighter& x, uint8_t type) {
        for (int k = 0; x.n_aff && k < *x.n_aff; ++k)
            if (x.aff[k][0] == type) {
                memmove(x.aff[k], x.aff[k + 1], (*x.n_aff - k - 1) * party::kAffectSize);
                --*x.n_aff;
                --k;
            }
    };
    // Half-elves (0x7C): 30% against sleep and charm (listing ovr013:25D0)
    {
        combat::Fighter& h = b.f[1];
        give(h, 0x7C);
        combat::Harm hm;
        hm.level = 11;
        dice_faces({30});
        const bool r30 = combat::resists(b, 1, b.fx->mon.sleep, hm, rng);
        dice_faces({31});
        const bool r31 = combat::resists(b, 1, b.fx->mon.sleep, hm, rng);
        dice_usual();
        expect(P, "half-elf: resists sleep on 30 or under", r30 && !r31, "30: %d, 31: %d", r30, r31);
        drop(h, 0x7C);
    }
    // The attacks: a party member at a monster, a monster at a party member
    int mon = -1;
    for (int i = b.party_size; i < b.n; ++i)
        if (b.f[i].up() && b.f[i].size) {
            mon = i;
            break;
        }
    if (mon < 0) {
        expect(P, "a monster to fight", false);
        return;
    }
    combat::Fighter& pc = b.f[0];
    combat::Fighter& mo = b.f[mon];
    // The lowest d20 that hits (2-19), the target's hit points kept
    auto lowest_hit = [&](int a, int t) {
        for (int face = 2; face <= 19; ++face) {
            combat::Fighter& at = b.f[a];
            combat::Fighter& tg = b.f[t];
            const uint8_t hp = tg.rec[0x1A4], st = tg.rec[0x195], in = tg.rec[0x196];
            // (each try the first attack on it this round: never "from behind",
            // which the original's counting of attacks received would make it)
            tg.received = 0;
            tg.turns = 0;
            at.attacks[0] = 1;
            at.attacks[1] = 0;
            dice_faces({face, 1, 1, 1, 1, 1, 1});
            const combat::Attack r = combat::attack(b, a, t, names, rng);
            dice_usual();
            tg.rec[0x1A4] = hp;
            tg.rec[0x195] = st;
            tg.rec[0x196] = in;
            if (r.n && r.hits[0].hit) return face;
        }
        return 20;
    };
    {
        const uint8_t flags = mo.rec[0x14B];
        mo.rec[0x14B] &= static_cast<uint8_t>(~6);
        drop(pc, 0x1A);
        drop(pc, 0x12);
        const int plain = lowest_hit(0, mon);
        give(pc, 0x1A);
        mo.rec[0x14B] |= 4;
        const int orc = lowest_hit(0, mon);
        drop(pc, 0x1A);
        give(pc, 0x12);
        mo.rec[0x14B] = static_cast<uint8_t>((mo.rec[0x14B] & ~4) | 2);
        const int gfoe = lowest_hit(0, mon);
        drop(pc, 0x12);
        mo.rec[0x14B] = flags;
        expect(P, "dwarf (0x1A) against an orc (0x14B & 4): +1 to hit", plain > 2 && plain < 20 && orc == plain - 1, "lowest hit %d, %d",
               plain, orc);
        expect(P, "gnome (0x12) against its foe (0x14B & 2): +1 to hit", plain > 2 && plain < 20 && gfoe == plain - 1, "lowest hit %d, %d",
               plain, gfoe);
    }
    {
        const uint8_t type = mo.rec[0x11A], de = mo.rec[0xDE];
        drop(pc, 0x2F);
        drop(pc, 0x30);
        mo.rec[0xDE] = 2;
        mo.rec[0x11A] = 2;
        const int plain = lowest_hit(mon, 0);
        give(pc, 0x2F);
        const int giant = lowest_hit(mon, 0);
        mo.rec[0x11A] = 10;
        const int troll = lowest_hit(mon, 0);
        drop(pc, 0x2F);
        give(pc, 0x30);
        mo.rec[0x11A] = 1;
        const int kob = lowest_hit(mon, 0);
        drop(pc, 0x30);
        mo.rec[0x11A] = type;
        mo.rec[0xDE] = de;
        expect(P, "dwarf / gnome (0x2F) attacked by a giant / troll of size 2: -4", plain < 16 && giant == plain + 4 && troll == plain + 4,
               "lowest hit %d, giant %d, troll %d", plain, giant, troll);
        expect(P, "gnome (0x30) attacked by type 1 of size 2: -4", plain < 16 && kob == plain + 4, "lowest hit %d, %d", plain, kob);
    }
    {
        drop(pc, 0x86);
        pc.rec[0x10D] = 7;
        const uint8_t flags = mo.rec[0x14B], hp = mo.rec[0x1A4], st = mo.rec[0x195], in = mo.rec[0x196];
        mo.rec[0x14B] |= 8;
        mo.rec[0x1A4] = 200;
        auto dmg = [&]() {
            mo.received = 0;
            mo.turns = 0;
            pc.attacks[0] = 1;
            pc.attacks[1] = 0;
            dice_faces({20, 1, 1, 1, 1, 1, 1});
            const combat::Attack r = combat::attack(b, 0, mon, names, rng);
            dice_usual();
            mo.rec[0x1A4] = 200;
            mo.rec[0x195] = st;
            mo.rec[0x196] = in;
            return r.n && r.hits[0].hit ? r.hits[0].damage : -1;
        };
        const int plain = dmg();
        give(pc, 0x86);
        const int ranger = dmg();
        drop(pc, 0x86);
        mo.rec[0x14B] = flags;
        mo.rec[0x1A4] = hp;
        expect(P, "ranger (0x86) against a giant (0x14B & 8): + ranger level damage", plain >= 0 && ranger == plain + 7, "%d, %d", plain,
               ranger);
    }
}

// ---- 11. Turning undead: the table's column by cleric level (1-8 their own,
// 9-13 the ninth, past that the tenth - ovr014 turns_undead), a d20 at or
// over the entry turns (destroys when the entry is 0 or less)
void turning()
{
    if (!boot() || !start_fight()) {
        expect(P, "a fight for turning undead", false);
        return;
    }
    combat::Battle& b = fg->b;
    int mon = -1;
    for (int i = b.party_size; i < b.n; ++i)
        if (b.f[i].up() && b.f[i].size) {
            mon = i;
            break;
        }
    if (mon < 0) return;
    for (int i = b.party_size; i < b.n; ++i)
        if (i != mon) b.f[i].rec[0xE9] = 0;
    combat::Fighter& me = b.f[0];
    combat::Fighter& un = b.f[mon];
    const uint8_t keep[4] = {un.rec[0xE9], un.rec[0x195], un.rec[0x196], static_cast<uint8_t>(un.size)};
    int bad = 0, done = 0;
    std::string first;
    for (int L = 1; L <= 12; ++L)
        for (int type = 1; type <= 10; ++type) {
            const int col = L <= 8 ? L : L <= 13 ? 9 : 10;
            const int v = static_cast<int8_t>(fg->t.turn[type * 10 + col]);
            const int need = v < 0 ? -v : v;
            for (int face : {need, need - 1}) {
                if (face < 1 || face > 20) continue;
                memset(me.rec + 0x109, 0, 8);
                me.rec[0x109] = static_cast<uint8_t>(L);
                me.turned_undead = false;
                un.rec[0xE9] = static_cast<uint8_t>(type);
                un.rec[0x195] = keep[1];
                un.rec[0x196] = keep[2];
                un.size = keep[3];
                un.fleeing = false;
                combat::occupancy(b);
                int out[16];
                dice_faces({1, face});
                const int n = combat::turn_undead(b, fg->t, 0, rng, out, 16);
                dice_usual();
                const bool want = face >= need;
                const bool got = n == 1 && (out[0] == mon || out[0] == mon + 1000);
                const bool kind_ok = !got || (v > 0 ? out[0] == mon : out[0] == mon + 1000);
                ++done;
                if (got != want || !kind_ok) {
                    if (!bad++) {
                        char t[96];
                        snprintf(t, sizeof t, "level %d type %d entry %d d20 %d: %s", L, type, v, face, got ? "turned" : "not");
                        first = t;
                    }
                }
            }
        }
    un.rec[0xE9] = keep[0];
    un.rec[0x195] = keep[1];
    un.rec[0x196] = keep[2];
    un.size = keep[3];
    combat::occupancy(b);
    char w[96];
    snprintf(w, sizeof w, "turn undead: the column by cleric level, the d20 against it (%d tries)", done);
    expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());
}

} // namespace bcls

static void behave_classes()
{
    using namespace bcls;
    if (!b_oracle_load("classes")) {
        expect(P, "oracle file", false, "no oracle_classes.txt");
        return;
    }
    if (!boot()) {
        expect(P, "boot", false);
        return;
    }
    if (!play::load_rules(bcls::m)) {
        expect(P, "rule tables", false);
        return;
    }
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("tables")) tables();
    if (want("creation")) creation();
    if (want("fixed")) fixed_dice();
    if (want("level")) levelling();
    if (want("dual")) dual_class();
    if (want("stats")) stats_in_play();
    if (want("sample")) sample_party();
    if (want("hall")) training_hall();
    if (want("exp")) experience();
    if (want("racial")) racial();
    if (want("turn")) turning();
}

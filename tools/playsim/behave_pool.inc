// Behaviour checks: Pool of Radiance's characters (included by sweep.cpp
// with -DBEHAVE_POOL; run with GAME=1 on Pool's folder). What its program
// does - the class values, creation, training, Modify - against the oracle
// (Pool's own tables, read from the player's program by a scratch script)
// and the formulas of the Project's claude/poolrad_create_facts.md, never
// against the engine's own code.
#include "behave_oracle.inc"

namespace bpool {

using namespace play;
Making* m = nullptr;
const char* P = "pool";

void set_stat(party::Character& c, int i, int v)
{
    c.rec[0x10 + i * 2] = static_cast<uint8_t>(v);
    c.rec[0x11 + i * 2] = static_cast<uint8_t>(v);
}
void put32(uint8_t* r, int o, uint32_t v)
{
    for (int i = 0; i < 4; ++i) r[o + i] = static_cast<uint8_t>(v >> (8 * i));
}
void bare(party::Character& c, int race, int cls, int stat = 12)
{
    c = party::Character{};
    c.rec[0x74] = static_cast<uint8_t>(race);
    c.rec[0x75] = static_cast<uint8_t>(cls);
    for (int i = 0; i < 6; ++i) set_stat(c, i, stat);
    c.rec[0x11C] = 2;
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
int class_bits(int cls)
{
    static const uint8_t bits[17] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
                                     0x05, 0x25, 0x11, 0x21, 0x41, 0x24, 0x44, 0x64, 0x60};
    return cls >= 0 && cls < 17 ? bits[cls] : 0;
}
int held(int cls)
{
    int n = 0;
    for (int k = 0; k < 8; ++k) n += (class_bits(cls) >> k) & 1;
    return n;
}
int ocol(const char* key, int idx)            // a one-row table's column
{
    const auto& r = b_rows(key);
    return r.empty() || idx < 0 || idx >= static_cast<int>(r[0].size()) ? 0 : r[0][idx];
}
int oxp(int k, int lv)                         // to go from lv to lv + 1; -1 none
{
    const auto* r = b_row("xp", k);
    return r && lv >= 1 && lv + 1 < static_cast<int>(r->size()) ? (*r)[1 + lv] : -1;
}
int othac0(int k, int lv)
{
    const auto* r = b_row("thac0", k);
    if (lv > 10) lv = 10;
    return r ? (*r)[1 + lv] : 40;
}
const std::vector<int>* osave(int k, int lv) { return b_row("save", k, lv > 9 ? 9 : lv); }
// Slot totals (create_facts 2.1): level 1 one 1st level slot; 2-9 the table
void oslots(int k, int lv, int out[3])
{
    out[0] = out[1] = out[2] = 0;
    if (lv <= 0) return;
    if (lv == 1) {
        out[0] = 1;
        return;
    }
    const auto* r = b_row(k == 0 ? "cleric_slots" : "mu_slots", lv > 9 ? 9 : lv);
    for (int j = 0; r && j < 3; ++j) out[j] = (*r)[1 + j];
}
// 2.4: the cleric's Wisdom bonus, to levels already with slots
void wis_bonus(int wis, int s[3])
{
    if (wis >= 13 && s[0] > 0) ++s[0];
    if (wis >= 14 && s[0] > 0) ++s[0];
    if (wis >= 15 && s[1] > 0) ++s[1];
    if (wis >= 16 && s[1] > 0) ++s[1];
    if (wis >= 17 && s[2] > 0) ++s[2];
}
// 2.5: the Constitution adjustment - per class held, and a single-class
// fighter's +1 over 16, +1 more over 17
int ocon_adj(int con, int cls)
{
    const int per = ocol("con_hp", con);
    int a = per * held(cls);
    if (cls == 2) a += (con > 16) + (con > 17);
    return a;
}
int ohp(int R, int A, int n)                  // 1.12 / 2.6: the averaging with A
{
    if (A < 0) return R > -A + n ? (R + A) / n : 1;
    return (R + A) / n;
}
int ocount(int k) { return ocol("hp_count", k); }
int osides(int k) { return ocol("hp_sides", k); }

// ---- 1. The class values (ovr24 +0000): THAC0, saves, slots, attacks, thief skills
void class_values()
{
    static party::Character c;
    const int single[4] = {0, 2, 5, 6};
    for (int k : single)
        for (int L = 1; L <= 9; ++L) {
            if (L > 1 && oxp(k, L - 1) <= 0) break;          // (past the class's last level)
            bare(c, 7, k);
            int lv[8] = {};
            lv[k] = L;
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            char w[80];
            int want = 0;
            for (int j = 0; j < 8; ++j) want = std::max(want, othac0(j, j == k ? L : 0));
            snprintf(w, sizeof w, "THAC0 class %d level %d", k, L);
            expect(P, w, c.rec[0x73] == want, "%d, the original's %d", c.rec[0x73], want);
            const auto* s = osave(k, L);
            bool ok = s != nullptr;
            for (int i = 0; ok && i < 5; ++i) ok = c.rec[0xDF + i] == (*s)[2 + i];
            snprintf(w, sizeof w, "saves class %d level %d", k, L);
            expect(P, w, ok, "%d %d %d %d %d", c.rec[0xDF], c.rec[0xE0], c.rec[0xE1], c.rec[0xE2], c.rec[0xE3]);
            if (k == 0 || k == 5) {
                for (int wis = 9; wis <= 18; ++wis) {
                    set_stat(c, 2, wis);
                    classes::class_bonuses(c, m->tables);
                    int o[3];
                    oslots(k, L, o);
                    if (k == 0) wis_bonus(wis, o);
                    const int base = k == 0 ? 0x12D : 0x137;
                    const bool sok = c.rec[base] == o[0] && c.rec[base + 1] == o[1] && c.rec[base + 2] == o[2] &&
                                     c.rec[base + 3] == 0 && c.rec[base + 4] == 0;
                    if (!sok || wis == 18) {
                        snprintf(w, sizeof w, "spell slots class %d level %d Wis %d", k, L, wis);
                        expect(P, w, sok, "%d %d %d, the original's %d %d %d", c.rec[base], c.rec[base + 1], c.rec[base + 2],
                               o[0], o[1], o[2]);
                    }
                    // A cleric knows every cleric spell of a level with slots
                    if (k == 0) {
                        bool bok = true;
                        for (int sp = 1; sp < 0x44; ++sp) {
                            const auto* row = b_row("spell", sp);
                            if (!row) continue;
                            const int scl = (*row)[1], sl = (*row)[2];
                            const bool wantb = scl == 0 && sl >= 1 && sl <= 3 && o[sl - 1] > 0;
                            if ((c.rec[0x79 + sp - 1] != 0) != wantb) bok = false;
                        }
                        if (!bok || wis == 18) {
                            snprintf(w, sizeof w, "cleric level %d Wis %d: the spells known", L, wis);
                            expect(P, w, bok);
                        }
                    }
                }
            }
            // Attacks (half attacks): a fighter 3 above level 6, else 2
            const int att = k == 2 && L > 6 ? 3 : 2;
            snprintf(w, sizeof w, "attacks class %d level %d", k, L);
            expect(P, w, c.rec[0x11C] == att, "%d, the original's %d", c.rec[0x11C], att);
        }
    // A fighter's attacks back to 2 when the level is (e.g. drained) 6
    {
        bare(c, 7, 2);
        int lv[8] = {};
        lv[2] = 7;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        lv[2] = 6;
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        expect(P, "attacks: a fighter at level 6 after 7", c.rec[0x11C] == 2, "%d, the original's 2", c.rec[0x11C]);
    }
    // Thief skills (2.3): every race, Dex 3-19, levels 1-9
    int bad = 0, done = 0;
    std::string first;
    for (int race = 1; race <= 7; ++race)
        for (int dex = 3; dex <= 19; ++dex)
            for (int L = 1; L <= 9; ++L) {
                bare(c, race, 6);
                set_stat(c, 3, dex);
                int lv[8] = {};
                lv[6] = L;
                levels(c, lv);
                classes::class_bonuses(c, m->tables);
                const auto* tb = b_row("thief_base", L);
                const auto* tr = b_row("thief_race", race);
                const auto* td = b_row("thief_dex", dex);
                for (int i = 1; i <= 8; ++i) {
                    const int base = (*tb)[i], r = (*tr)[i];
                    int want = r < 0 && -r > base ? 0 : base + r;
                    if (i <= 5 && !(r < 0 && -r > base)) want += (*td)[i];
                    ++done;
                    if (c.rec[0xEA + i - 1] != static_cast<uint8_t>(want) && !bad++) {
                        char t[96];
                        snprintf(t, sizeof t, "race %d Dex %d level %d skill %d: %d, the original's %d", race, dex, L, i,
                                 c.rec[0xEA + i - 1], want);
                        first = t;
                    }
                }
            }
    char w[96];
    snprintf(w, sizeof w, "thief skills: base + race (none below 0) + Dex for 1-5 (%d)", done);
    expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());
    // Multi-classes: THAC0 the best, saves the best of the classes
    for (int cls = 8; cls <= 16; ++cls) {
        if (cls == 10) continue;                            // (C/R: not offered)
        for (int L = 1; L <= 6; ++L) {
            bare(c, 4, cls);
            int lv[8] = {};
            for (int k = 0; k < 8; ++k)
                if (class_bits(cls) & (1 << k)) lv[k] = std::max(1, L - k % 3);
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            int t0 = 0, sv[5] = {20, 20, 20, 20, 20};
            for (int k = 0; k < 8; ++k) {
                t0 = std::max(t0, othac0(k, lv[k]));
                if (!lv[k]) continue;
                const auto* s = osave(k, lv[k]);
                for (int i = 0; s && i < 5; ++i) sv[i] = std::min(sv[i], (*s)[2 + i]);
            }
            bool ok = c.rec[0x73] == t0;
            for (int i = 0; i < 5; ++i) ok = ok && c.rec[0xDF + i] == sv[i];
            snprintf(w, sizeof w, "class %d levels %d/%d/%d/%d: THAC0 and saves", cls, lv[0], lv[2], lv[5], lv[6]);
            if (!ok || L == 6)
                expect(P, w, ok, "THAC0 %d (%d), saves %d %d %d %d %d (%d %d %d %d %d)", c.rec[0x73], t0, c.rec[0xDF],
                       c.rec[0xE0], c.rec[0xE1], c.rec[0xE2], c.rec[0xE3], sv[0], sv[1], sv[2], sv[3], sv[4]);
        }
    }
}

// ---- 2. The Constitution adjustment (2.5)
void constitution()
{
    static party::Character c;
    int bad = 0, done = 0;
    std::string first;
    for (int cls : {0, 2, 5, 6, 8, 9, 11, 12, 13, 14, 15, 16})
        for (int con = 3; con <= 19; ++con) {
            bare(c, 1, cls);
            set_stat(c, 4, con);
            int lv[8] = {};
            for (int k = 0; k < 8; ++k)
                if (class_bits(cls) & (1 << k)) lv[k] = 1;
            levels(c, lv);
            const int got = create::con_hp_adj(c, m->tables), want = ocon_adj(con, cls);
            ++done;
            if (got != want && !bad++) {
                char t[96];
                snprintf(t, sizeof t, "class %d Con %d: %d, the original's %d", cls, con, got, want);
                first = t;
            }
        }
    char w[96];
    snprintf(w, sizeof w, "Constitution's hit point adjustment (%d)", done);
    expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());
}

// ---- 3. Creation: every race, class, alignment, sex - with the dice high and low
void creation()
{
    static party::Character c;
    int races[8];
    const int nr = create::races(races, 8);
    {
        std::string got;
        for (int i = 0; i < nr; ++i) got += " " + std::to_string(races[i]);
        expect(P, "the races offered: dwarf, elf, gnome, half-elf, halfling, human", got == " 1 2 3 4 5 7", "%s", got.c_str());
    }
    int bad_lim = 0, bad_hp = 0, bad_gold = 0, bad_age = 0, bad_fx = 0, bad_book = 0, bad_misc = 0, made = 0;
    std::string f_lim, f_hp, f_gold, f_age, f_fx, f_book, f_misc;
    auto note = [](int& n, std::string& first, const char* tag, const std::string& what) {
        if (!n++) first = std::string(tag) + ": " + what;
    };
    for (int ri = 0; ri < nr; ++ri) {
        const int race = races[ri];
        int cl[32];
        const int nc = create::classes_for(m->tables, race, cl, 32);
        const auto* rc = b_row("race_classes", race);
        bool same = rc && nc == static_cast<int>(rc->size()) - 1;
        for (int i = 0; same && i < nc; ++i) same = cl[i] == (*rc)[1 + i];
        char w[96];
        snprintf(w, sizeof w, "race %d: the classes offered (in order)", race);
        expect(P, w, same, "%d classes", nc);
        const auto* lim = b_row("limits", race);
        const auto* ages = b_row("ages", race);
        const auto* br = b_row("age_bracket", race);
        for (int ci = 0; ci < nc; ++ci) {
            const int cls = cl[ci];
            int al[16];
            const int na = create::alignments_for(m->tables, cls, al, 16);
            const auto* oa = b_row("class_align", cls);
            bool asame = oa && na == static_cast<int>(oa->size()) - 1;
            for (int i = 0; asame && i < na; ++i) asame = al[i] == (*oa)[1 + i];
            if (!asame || race == 4) {
                snprintf(w, sizeof w, "class %d: the alignments offered", cls);
                expect(P, w, asame, "%d", na);
            }
            const auto* mn = b_row("class_min", cls);
            for (int dice = 0; dice < 2; ++dice)
                for (int ai = 0; ai < na; ++ai)
                    for (int sex = 0; sex < 2; ++sex) {
                        if (dice) dice_low();
                        else dice_high();
                        create::begin(c, m->tables, m->facts, m->dice, race, sex, cls, al[ai]);
                        create::roll(c, m->tables, m->facts, m->dice);
                        dice_usual();
                        ++made;
                        char tag[64];
                        snprintf(tag, sizeof tag, "race %d class %d align %d sex %d dice %s", race, cls, al[ai], sex,
                                 dice ? "low" : "high");
                        char t[160];
                        // Levels 1, experience 0
                        bool lok = c.exp() == 0;
                        for (int k = 0; k < 8; ++k) lok = lok && c.level(k) == ((class_bits(cls) >> k) & 1);
                        if (!lok) {
                            snprintf(t, sizeof t, "exp %u levels %d/%d/%d/%d", c.exp(), c.level(0), c.level(2), c.level(5),
                                     c.level(6));
                            note(bad_misc, f_misc, tag, t);
                        }
                        // Age (1.1 step 6)
                        int want_age = 0;
                        if (cls <= 7) {
                            const int base = (*ages)[1 + cls * 3], n = (*ages)[2 + cls * 3], s = (*ages)[3 + cls * 3];
                            want_age = base + (dice ? n : n * s);
                        } else if (cls != 10) {
                            const int e = cls == 14 ? 2 : (cls == 13 || cls == 15 || cls == 16) ? 5 : 0;
                            want_age = (*ages)[1 + e * 3] + (*ages)[2 + e * 3] * (*ages)[3 + e * 3];
                        }
                        if (c.age() != want_age) {
                            snprintf(t, sizeof t, "age %d, the original's %d", c.age(), want_age);
                            note(bad_age, f_age, tag, t);
                        }
                        // Stats (step 8): best of six 3d6, the age's effects, the
                        // race's / sex's limits, the class's minimum, 18/xx
                        static const int8_t eff[6][4] = {{1, -1, -2, -1}, {0, 1, 0, 1}, {-1, 1, 1, 1},
                                                         {0, 0, -2, -1}, {0, -1, -1, -1}, {0, 0, 0, 0}};
                        bool sok = true;
                        int want_s[6];
                        for (int i = 0; i < 6; ++i) {
                            int v = dice ? 3 : 18;
                            for (int b = 0; b < 4; ++b)
                                if ((*br)[1 + b] < want_age) v += eff[i][b];
                            const int lo = i == 0 ? (*lim)[1 + sex] : (*lim)[5 + i * 2];
                            const int hi = i == 0 ? (*lim)[3 + sex] : (*lim)[6 + i * 2];
                            v = std::min(v, hi);
                            v = std::max(v, lo);
                            v = std::max(v, (*mn)[1 + i]);
                            want_s[i] = v;
                            if (c.stat(i) != v || c.stat_now(i) != v) sok = false;
                        }
                        int want_e = 0;
                        if (want_s[0] == 18 && c.level(2)) want_e = std::min(dice ? 1 : 100, (*lim)[5 + sex]);
                        if (c.stat(6) != want_e) sok = false;
                        if (!sok) {
                            snprintf(t, sizeof t, "%d(%d) %d %d %d %d %d, the original's %d(%d) %d %d %d %d %d", c.stat(0),
                                     c.stat(6), c.stat(1), c.stat(2), c.stat(3), c.stat(4), c.stat(5), want_s[0], want_e,
                                     want_s[1], want_s[2], want_s[3], want_s[4], want_s[5]);
                            note(bad_lim, f_lim, tag, t);
                        }
                        // Gold (step 11): per class dice into a byte, x10 / classes; nothing else
                        int sum = 0, n = 0;
                        for (int k = 0; k < 8; ++k)
                            if (class_bits(cls) & (1 << k)) {
                                const int cnt = ocol("money", 2 * k), sd = ocol("money", 2 * k + 1);
                                sum += dice ? cnt : cnt * sd;
                                ++n;
                            }
                        const int gold = (sum & 0xFF) * 10 / n;
                        bool gok = true;
                        for (int i = 0; i < 7; ++i) gok = gok && c.money(i) == (i == 3 ? gold : 0);
                        if (!gok) {
                            snprintf(t, sizeof t, "gold %d plat %d, the original's %d gold", c.money(3), c.money(4), gold);
                            note(bad_gold, f_gold, tag, t);
                        }
                        // Hit points (step 12)
                        int R = 0;
                        for (int k = 0; k < 8; ++k)
                            if (class_bits(cls) & (1 << k)) {
                                const int cnt = ocount(k), sd = osides(k);
                                int v = dice ? cnt : cnt * sd;
                                v = std::max(v, sd * 2 / 3);
                                R += v;
                            }
                        const int A = ocon_adj(want_s[4], cls);
                        const int hp = ohp(R, A, n);
                        if (c.hp_max() != hp || c.hp() != hp || c.rec[0x12C] != R / n) {
                            snprintf(t, sizeof t, "hp %d/%d rolled %d, the original's %d rolled %d (R %d A %d)", c.hp(),
                                     c.hp_max(), c.rec[0x12C], hp, R / n, R, A);
                            note(bad_hp, f_hp, tag, t);
                        }
                        // Effects (step 2)
                        const auto* fx = b_row("race_fx", race);
                        bool fok = fx && c.n_affects == static_cast<int>(fx->size()) - 1;
                        for (size_t i = 1; fx && i < fx->size(); ++i)
                            fok = fok && c.has_affect(static_cast<uint8_t>((*fx)[i]));
                        if (!fok) note(bad_fx, f_fx, tag, std::to_string(c.n_affects) + " effects");
                        // Spell book (step 10) and slots
                        std::vector<int> want;
                        if (c.level(0))
                            for (int sp = 1; sp < 0x44; ++sp) {
                                const auto* row = b_row("spell", sp);
                                if (row && (*row)[1] == 0 && (*row)[2] == 1) want.push_back(sp);
                            }
                        if (c.level(5))
                            for (const auto& r : b_rows("mu_first")) want.insert(want.end(), r.begin(), r.end());
                        std::sort(want.begin(), want.end());
                        std::vector<int> got;
                        for (int sp = 1; sp <= 100; ++sp)
                            if (c.rec[0x79 + sp - 1]) got.push_back(sp);
                        int sl[3] = {};
                        if (c.level(0)) {
                            sl[0] = 1;
                            wis_bonus(want_s[2], sl);
                        }
                        const bool slok = c.rec[0x12D] == sl[0] && c.rec[0x137] == (c.level(5) ? 1 : 0);
                        if (got != want || !slok) {
                            std::string g;
                            for (int s : got) g += " " + std::to_string(s);
                            snprintf(t, sizeof t, "book%s, slots %d / %d", g.c_str(), c.rec[0x12D], c.rec[0x137]);
                            note(bad_book, f_book, tag, t);
                        }
                        // Icon size, colours, portrait
                        const int size = race == 1 || race == 3 || race == 5 ? 1 : 2;
                        bool iok = c.rec[0x144] == size && c.rec[0x13F] == 1 && c.rec[0x140] == 1;
                        for (int i = 0; i < 6; ++i) {
                            const int col = ocol("icon_colours", i);
                            iok = iok && c.rec[0x145 + i] == ((col + 8) << 4) + col;
                        }
                        if (!iok) note(bad_misc, f_misc, tag, "icon size / colours / portrait");
                    }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "creation (%d made): stats, 18/xx", made);
    expect(P, w, !bad_lim, "%d wrong, first %s", bad_lim, f_lim.c_str());
    expect(P, "creation: hit points (2/3 at least, Con, averaged), rolled", !bad_hp, "%d wrong, first %s", bad_hp, f_hp.c_str());
    expect(P, "creation: gold only, the dice x10 / classes", !bad_gold, "%d wrong, first %s", bad_gold, f_gold.c_str());
    expect(P, "creation: age", !bad_age, "%d wrong, first %s", bad_age, f_age.c_str());
    expect(P, "creation: the race's effects", !bad_fx, "%d wrong, first %s", bad_fx, f_fx.c_str());
    expect(P, "creation: spell book and slots", !bad_book, "%d wrong, first %s", bad_book, f_book.c_str());
    expect(P, "creation: levels, experience 0, icon", !bad_misc, "%d wrong, first %s", bad_misc, f_misc.c_str());
}

// ---- 4. Training (section 3): who can, the race limits, the classes trained
// together, the experience kept, the hit points
bool olimited(int race, int k, int lv, int str)        // 2.7 (ovr17 +2A75: exact levels)
{
    switch (race) {
    case 1: return k == 2 && ((lv == 8 && str == 17) || (lv == 7 && str < 17));
    case 2: return k == 2 && (lv == 7 || (lv == 6 && str == 17) || (lv == 5 && str < 17));
    case 3: return k == 2 && (lv == 6 || (lv == 5 && str < 18));
    case 4:
        if (k == 0) return lv == 5;
        return k == 2 && (lv == 8 || (lv == 7 && str == 17) || (lv == 6 && str < 17));
    case 5: return k == 2 && (lv == 6 || (lv == 5 && str == 17) || (lv == 4 && str < 17));
    default: return false;
    }
}
void training()
{
    static party::Character c;
    int bad = 0, done = 0;
    std::string first;
    // Single classes: ready exactly when the experience is there and no limit
    for (int race : {1, 2, 3, 4, 5, 7})
        for (int k : {0, 2, 5, 6})
            for (int str : {16, 17, 18})
                for (int L = 1; L <= 9; ++L) {
                    bare(c, race, k);
                    set_stat(c, 0, str);
                    int lv[8] = {};
                    lv[k] = L;
                    levels(c, lv);
                    classes::class_bonuses(c, m->tables);
                    const int need = oxp(k, L);
                    put32(c.rec, 0x127, need > 0 ? static_cast<uint32_t>(need) : 9999999u);
                    const bool want = need > 0 && !olimited(race, k, L, str);
                    const bool got = create::trainable(c, m->tables) != 0;
                    ++done;
                    if (got != want && !bad++) {
                        char t[96];
                        snprintf(t, sizeof t, "race %d class %d Str %d level %d: %s", race, k, str, L, got ? "ready" : "not");
                        first = t;
                    }
                    if (need > 1) {          // one short: not ready
                        put32(c.rec, 0x127, static_cast<uint32_t>(need - 1));
                        ++done;
                        if (create::trainable(c, m->tables) && !bad++) first = "one point short, ready";
                    }
                }
    char w[120];
    snprintf(w, sizeof w, "training: ready with the experience, the race limits (%d)", done);
    expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());

    // Multi-classes: every ready class trains (one level each); the
    // experience kept: one under the next-but-one figure of a ready class
    // that has it (the most of them)
    for (int cls : {8, 9, 11, 12, 13, 14, 15, 16})
        for (int L = 1; L <= 4; ++L)
            for (int extra : {0, 1}) {
                bare(c, 4, cls, 14);
                set_stat(c, 0, 18);
                int lv[8] = {};
                for (int k = 0; k < 8; ++k)
                    if (class_bits(cls) & (1 << k)) lv[k] = L;
                levels(c, lv);
                classes::class_bonuses(c, m->tables);
                // Experience: enough for every class's next level (extra: and the one after)
                int32_t xp = 0;
                for (int k = 0; k < 8; ++k)
                    if (lv[k]) xp = std::max(xp, static_cast<int32_t>(oxp(k, extra ? L + 1 : L)));
                if (xp <= 0) continue;
                put32(c.rec, 0x127, static_cast<uint32_t>(xp));
                int want_mask = 0;
                int32_t keep = 0;
                for (int k = 0; k < 8; ++k) {
                    if (!lv[k] || olimited(4, k, L, 18)) continue;
                    const int need = oxp(k, L);
                    if (need <= 0 || need > xp) continue;
                    want_mask |= ocol("class_masks", k);
                    const int n2 = oxp(k, L + 1);
                    if (n2 > 0 && xp >= n2) keep = std::max(keep, static_cast<int32_t>(n2 - 1));
                }
                uint32_t after = 0;
                const int got = create::train_pick(c, m->tables, &after);
                const uint32_t want_after = keep > 0 ? static_cast<uint32_t>(keep) : static_cast<uint32_t>(xp);
                snprintf(w, sizeof w, "training class %d level %d%s: classes trained, experience kept", cls, L,
                         extra ? " (two levels' worth)" : "");
                expect(P, w, got == want_mask && after == want_after, "mask %d exp %u, the original's mask %d exp %u", got,
                       after, want_mask, want_after);
            }

    // Hit points on training (3.12): R over the trained classes, rolled +
    // max(1, R / n), max + max(1, (R + A) / n), the damage kept
    for (int cls : {0, 2, 5, 6, 8, 13, 15})
        for (int con : {3, 7, 15, 17, 18})
            for (int dice = 0; dice < 2; ++dice) {
                bare(c, 7, cls);
                set_stat(c, 4, con);
                int lv[8] = {};
                for (int k = 0; k < 8; ++k)
                    if (class_bits(cls) & (1 << k)) lv[k] = 2;
                levels(c, lv);
                classes::class_bonuses(c, m->tables);
                c.rec[0x78] = 30;
                c.rec[0x1A4] = 25;
                c.rec[0x12C] = 20;
                int mask = 0, R = 0, n = 0;
                for (int k = 0; k < 8; ++k)
                    if (lv[k]) {
                        ++n;
                        mask |= ocol("class_masks", k);
                        R += dice ? ocount(k) : ocount(k) * osides(k);
                    }
                if (dice) dice_low();
                else dice_high();
                create::train_classes(c, m->tables, m->facts, m->dice, mask, false);
                dice_usual();
                const int A = ocon_adj(con, cls);
                const int gain = std::max(1, (R + A) / n), roll = std::max(1, R / n);
                bool ok = c.hp_max() == 30 + gain && c.hp() == 25 + gain && c.rec[0x12C] == 20 + roll;
                for (int k = 0; k < 8; ++k) ok = ok && c.level(k) == (lv[k] ? 3 : 0);
                snprintf(w, sizeof w, "training hit points class %d Con %d dice %s", cls, con, dice ? "low" : "high");
                expect(P, w, ok, "max %d now %d rolled %d, the original's %d %d %d", c.hp_max(), c.hp(), c.rec[0x12C],
                       30 + gain, 25 + gain, 20 + roll);
            }
}

// ---- 5. Modify (section 5): the hit point bounds, the stat steps
void modify()
{
    static party::Character c;
    char w[120];
    int bad = 0, done = 0;
    std::string first;
    for (int cls : {0, 2, 5, 6, 8, 9, 11, 12, 13, 14, 15, 16})
        for (int con = 3; con <= 19; ++con) {
            bare(c, 4, cls);
            set_stat(c, 4, con);
            int lv[8] = {};
            for (int k = 0; k < 8; ++k)
                if (class_bits(cls) & (1 << k)) lv[k] = 1;
            levels(c, lv);
            const int A = ocon_adj(con, cls), n = held(cls);
            int S = 0, M = 0;
            for (int k = 0; k < 8; ++k)
                if (lv[k]) {
                    S += lv[k] + (k == 4 || k == 7 ? 1 : 0);
                    const int per[8] = {8, 8, 10, 10, 8, 4, 6, 4};
                    M += per[k] * (k == 4 || k == 7 ? lv[k] + 1 : lv[k]);
                }
            const int lo = ohp(S, A, n), hi = ohp(M, A, n);
            const int glo = create::hp_least(c, m->tables, m->facts), ghi = create::hp_most(c, m->tables, m->facts);
            ++done;
            if ((glo != lo || ghi != hi) && !bad++) {
                char t[120];
                snprintf(t, sizeof t, "class %d Con %d: %d-%d, the original's %d-%d", cls, con, glo, ghi, lo, hi);
                first = t;
            }
        }
    snprintf(w, sizeof w, "Modify: the hit point bounds (%d)", done);
    expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());

    // Only at experience 0
    bare(c, 7, 2);
    int lv1[8] = {0, 0, 1};
    levels(c, lv1);
    expect(P, "Modify: allowed at experience 0", create::can_modify(c, true));
    put32(c.rec, 0x127, 1);
    expect(P, "Modify: not after the first point", !create::can_modify(c, true));

    // The stat steps: up to the race's (and sex's) maximum, 18 into 18/xx
    // for fighters up to its maximum, down below the race's or class's
    // minimum -> the race's minimum
    bad = 0;
    done = 0;
    first.clear();
    for (int race : {1, 2, 3, 4, 5, 7})
        for (int cls : {0, 2, 5, 6, 13, 14})
            for (int sex = 0; sex < 2; ++sex)
                for (int i = 0; i < 6; ++i) {
                    const auto* lim = b_row("limits", race);
                    const auto* mn = b_row("class_min", cls);
                    const int lo = i == 0 ? (*lim)[1 + sex] : (*lim)[5 + i * 2];
                    const int hi = i == 0 ? (*lim)[3 + sex] : (*lim)[6 + i * 2];
                    const int cmin = (*mn)[1 + i];
                    const bool fighter = class_bits(cls) & 4;
                    const int emax = (*lim)[5 + sex];
                    for (int start = 3; start <= 19; ++start)
                        for (int dir : {1, -1}) {
                            bare(c, race, cls, 12);
                            c.rec[0x119] = static_cast<uint8_t>(sex);
                            int lv[8] = {};
                            for (int k = 0; k < 8; ++k)
                                if (class_bits(cls) & (1 << k)) lv[k] = 1;
                            levels(c, lv);
                            set_stat(c, i, start);
                            int e0 = 0;
                            if (i == 0 && start == 18 && fighter) {
                                e0 = std::min(50, emax);
                                set_stat(c, 6, e0);
                            }
                            create::modify_stat(c, m->tables, m->facts, i, dir);
                            int wv = start, we = e0;
                            if (dir > 0) {
                                if (i == 0 && start >= 18 && fighter && e0 < emax) {
                                    we = e0 + 1;
                                    wv = start;
                                } else {
                                    wv = std::min(start + 1, hi);
                                }
                            } else if (i == 0 && e0 > 0) {
                                we = e0 - 1;
                            } else {
                                wv = start - 1;
                                if (wv < lo || wv < cmin) wv = lo;
                            }
                            // (start values outside the limits: not a case the game meets)
                            if (start < lo || start > hi || start < cmin) continue;
                            ++done;
                            if ((c.stat(i) != wv || (i == 0 && c.stat(6) != we)) && !bad++) {
                                char t[160];
                                snprintf(t, sizeof t, "race %d class %d sex %d stat %d from %d(%d) %s: %d(%d), the original's %d(%d)",
                                         race, cls, sex, i, start, e0, dir > 0 ? "up" : "down", c.stat(i), c.stat(6), wv, we);
                                first = t;
                            }
                        }
                }
    snprintf(w, sizeof w, "Modify: the stat steps (%d)", done);
    expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());

    // Keep: the "modified" mark
    bare(c, 7, 2);
    levels(c, lv1);
    create::modify_done(c, m->tables, m->facts);
    expect(P, "Modify: Keep marks the character modified", c.rec[0xF8] == 1);
}

// ---- 6. Money: training's 1000 gp (the worth in gold, 3 step 2)
void money()
{
    const auto& cv = b_rows("coins");
    if (cv.empty()) return;
    int bad = 0, done = 0;
    std::string first;
    const int samples[][5] = {{0, 0, 0, 1000, 0}, {0, 0, 0, 999, 0}, {100, 0, 0, 999, 0}, {99, 0, 0, 999, 0},
                              {0, 10, 0, 999, 0}, {0, 0, 1, 999, 0}, {0, 0, 0, 0, 200}, {0, 0, 0, 0, 199},
                              {150, 5, 3, 0, 0},  {1999, 0, 0, 0, 0}, {0, 0, 2000, 0, 0}};
    for (const auto& s : samples) {
        int mny[7] = {s[0], s[1], s[2], s[3], s[4], 0, 0};
        long cu = 0;
        for (int i = 0; i < 5; ++i) cu += static_cast<long>(s[i]) * cv[0][i];
        const int want = static_cast<int>((cu + 100) / 200);
        const int got = rules::gold_worth(mny, true);
        ++done;
        if (got != want && !bad++) {
            char t[96];
            snprintf(t, sizeof t, "%d %d %d %d %d: %d, the original's %d", s[0], s[1], s[2], s[3], s[4], got, want);
            first = t;
        }
    }
    char w[96];
    snprintf(w, sizeof w, "money: the worth in gold (rounded, %d)", done);
    expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());
}


#ifdef BEHAVE_SPELLS
// ---- 7. Spells in fights (poolrad_combat_facts.md 3; built with
// -DBEHAVE_SPELLS too, for its fight helpers): damage, saves, the bolt's
// length, Hold Person, Dispel Magic, Mirror Image, magic resistance
int sp_affect(int s) { const auto* r = b_row("spell", s); return r ? (*r)[11] : 0; }
int sp_savekind(int s) { const auto* r = b_row("spell", s); return r ? (*r)[10] : 4; }
void spells_in_fights()
{
    using namespace play;
    struct D {
        int spell, cls, level, lo, hi;
        char save;                      // 'N' no save, 'H' half
    };
    auto mm = [](int L) { return (L + 1) / 2; };          // (Tom's rule: AD&D's)
    const D cases[] = {
        {0x04, 0, 3, 1, 8, 'N'},                  // Cause Light Wounds 1d8 (a touch)
        {0x09, 5, 1, 1, 1, 'N'},                  // Burning Hands: the level
        {0x09, 5, 6, 6, 6, 'N'},
        {0x0F, 5, 1, mm(1) * 2, mm(1) * 5, 'N'},  // Magic Missile: (L + 1) / 2 x (1d4 + 1)
        {0x0F, 5, 4, mm(4) * 2, mm(4) * 5, 'N'},
        {0x0F, 5, 6, mm(6) * 2, mm(6) * 5, 'N'},
        {0x14, 5, 5, 1 + 5, 8 + 5, 'N'},          // Shocking Grasp 1d8 + level (a touch)
        {0x2F, 5, 5, 5, 30, 'H'},                 // Fireball: level d6
        {0x2F, 5, 6, 6, 36, 'H'},
        {0x33, 5, 5, 5, 30, 'H'},                 // Lightning Bolt: level d6
        {0x40, 5, 5, 3, 42, 'H'},                 // the wand's fireball (Fireball's handler: (d3 x 2 + 1) d6)
        {0x41, 5, 5, 4, 10, 'N'},                 // the wand's missile 2d4 + 2
    };
    char w[120];
    for (const D& c : cases)
        for (int pass = 0; pass < 3; ++pass) {          // dice low, high (save failed); high, the save made
            if (!sp_new_fight() || !sp_pick()) {
                expect(P, "a fight", false);
                return;
            }
            sp_caster(sp_c, c.cls, c.level);
            sp_put(sp_c, 0, 10, 12);
            sp_put(sp_t[0], 1, 12, 12);
            if (c.spell == 0x04 || c.spell == 0x14) sp_put(sp_t[0], 1, 11, 12);
            fg->b.aim_x = F(sp_t[0]).x, fg->b.aim_y = F(sp_t[0]).y;
            if (pass == 2) {
                F(sp_t[0]).rec[0xDF + sp_savekind(c.spell)] = 2;
                sp_dice(1, 19, 100);
            } else {
                sp_dice(pass ? 1 : 2, 2, 100);
            }
            const int n = sp_cast(sp_c, c.spell, {sp_t[0]});
            const int got = 200 - F(sp_t[0]).hp();
            const int want = pass == 0 ? c.lo : pass == 1 ? c.hi : c.save == 'H' ? c.hi / 2 : c.hi;
            snprintf(w, sizeof w, "spell %02X level %d, %s: damage", c.spell, c.level,
                     pass == 0 ? "dice low" : pass == 1 ? "dice high" : c.save == 'H' ? "the save made (half)" : "no save");
            expect(P, w, n >= 0 && got == want, "%d, the original's %d", got, want);
        }
    // 0x3A (an item's): heals d4 + 8; the poisoned: slow poison taken off instead
    for (int pass = 0; pass < 2; ++pass) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, 0, 5);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 0, 11, 12, 100);
        F(sp_t[0]).rec[0x78] = 200;
        sp_dice(pass ? 1 : 2, 0, 0);
        sp_cast(sp_c, 0x3A, {sp_t[0]});
        snprintf(w, sizeof w, "spell 3A, dice %s: heals d4 + 8", pass ? "high" : "low");
        expect(P, w, F(sp_t[0]).hp() == 100 + (pass ? 12 : 9), "%d, the original's %d", F(sp_t[0]).hp() - 100, pass ? 12 : 9);
    }
    // Lightning Bolt: on from the square aimed at for 8 squares (the path's
    // length argument 8, Curse 7: the half steps it may take, 2 a square)
    if (!sp_new_fight() || !sp_pick()) return;
    sp_caster(sp_c, 5, 6);
    sp_put(sp_c, 0, 6, 12);
    sp_put(sp_t[0], 1, 9, 12);
    sp_put(sp_t[1], 1, 12, 12);
    sp_put(sp_t[2], 1, 17, 12);
    sp_put(sp_t[3], 1, 18, 12);
    sp_dice(1, 2, 100);
    fg->b.aim_x = 9, fg->b.aim_y = 12;
    sp_cast(sp_c, 0x33, {sp_t[0]});
    {
        const int d0 = 200 - F(sp_t[0]).hp(), d1 = 200 - F(sp_t[1]).hp(), d2 = 200 - F(sp_t[2]).hp(),
                  d3 = 200 - F(sp_t[3]).hp();
        expect(P, "Lightning Bolt: the square aimed at and 8 beyond (Curse 7)", d0 == 36 && d1 == 36 && d2 == 36,
               "%d %d %d", d0, d1, d2);
        expect(P, "Lightning Bolt: none 9 beyond", d3 == 0, "%d", d3);
    }
    // Hold Person: the save by the number of targets (1: cleric -2 / magic-user
    // -3, 2: -1, 3-4: 0); monsters of type > 1 or size > 1 unaffected
    for (int sp : {0x17, 0x31})
        for (int nt = 1; nt <= 4; ++nt) {
            const int bonus = nt == 1 ? (sp == 0x17 ? -2 : -3) : nt == 2 ? -1 : 0;
            for (int made = 0; made < 2; ++made) {
                if (!sp_new_fight() || !sp_pick()) return;
                sp_caster(sp_c, sp == 0x17 ? 0 : 5, 5);
                sp_put(sp_c, 0, 6, 12);
                for (int i = 0; i < nt; ++i) sp_put(sp_t[i], 1, 10 + i, 12);
                // save 15: made when d20 + bonus >= 15
                const int roll = 15 - bonus - (made ? 0 : 1);
                for (int i = 0; i < nt; ++i)
                    for (int k = 0; k < 5; ++k) F(sp_t[i]).rec[0xDF + k] = 15;
                sp_dice(1, roll, 100);
                switch (nt) {
                case 1: sp_cast(sp_c, sp, {sp_t[0]}); break;
                case 2: sp_cast(sp_c, sp, {sp_t[0], sp_t[1]}); break;
                case 3: sp_cast(sp_c, sp, {sp_t[0], sp_t[1], sp_t[2]}); break;
                default: sp_cast(sp_c, sp, {sp_t[0], sp_t[1], sp_t[2], sp_t[3]}); break;
                }
                int held = 0;
                for (int i = 0; i < nt; ++i) held += sp_aff(sp_t[i], sp_affect(sp)) >= 0;
                snprintf(w, sizeof w, "Hold Person %02X, %d target%s, d20 %d (save %+d): %s", sp, nt, nt > 1 ? "s" : "", roll,
                         bonus, made ? "saved" : "held");
                expect(P, w, held == (made ? 0 : nt), "%d held", held);
            }
        }
    for (int what = 0; what < 2; ++what) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, 0, 5);
        sp_put(sp_c, 0, 6, 12);
        sp_put(sp_t[0], 1, 10, 12);
        if (what) F(sp_t[0]).rec[0xDE] = 2;
        else F(sp_t[0]).rec[0x11A] = 2;
        sp_dice(1, 2, 100);
        sp_cast(sp_c, 0x17, {sp_t[0]});
        snprintf(w, sizeof w, "Hold Person: %s unaffected", what ? "size 2" : "monster type 2");
        expect(P, w, sp_aff(sp_t[0], sp_affect(0x17)) < 0);
    }
    // Mirror Image: d4 images, the data nothing else
    for (int pass = 0; pass < 2; ++pass) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, 5, 5);
        sp_put(sp_c, 0, 6, 12);
        sp_dice(pass ? 1 : 2, 0, 0);
        sp_cast(sp_c, 0x20, {sp_c});
        snprintf(w, sizeof w, "Mirror Image, dice %s: the effect's data = the images", pass ? "high" : "low");
        expect(P, w, sp_aff_data(sp_c, sp_affect(0x20)) == (pass ? 4 : 1), "%d", sp_aff_data(sp_c, sp_affect(0x20)));
    }
    // An image takes a spell aimed at one: d(images + 1) over 1, the images count down
    {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_caster(sp_c, 5, 5);
        sp_put(sp_c, 0, 6, 12);
        sp_put(sp_t[0], 1, 10, 12);
        sp_give(sp_t[0], sp_affect(0x20), 30, 1, false);
        sp_dice(1, 2, 100);
        sp_cast(sp_c, 0x0F, {sp_t[0]});
        expect(P, "Mirror Image (1 image): a magic missile takes the image, the effect gone",
               F(sp_t[0]).hp() == 200 && sp_aff(sp_t[0], sp_affect(0x20)) < 0, "HP %d, effect %d", F(sp_t[0]).hp(),
               sp_aff(sp_t[0], sp_affect(0x20)));
    }
    // Dispel Magic: every effect (data < 0xFF) of EACH target: E = data & 15,
    // chance 50 + 5 (L - E) / 50 - 2 (E - L) / 50; d100 <= chance ends it
    for (int sp : {0x29, 0x2E})
        for (int E : {2, 5, 9})
            for (int over = 0; over < 2; ++over) {
                if (!sp_new_fight() || !sp_pick()) return;
                const int L = 5;
                sp_caster(sp_c, sp == 0x29 ? 0 : 5, L);
                sp_put(sp_c, 0, 6, 12);
                sp_put(sp_t[0], 1, 10, 12);
                sp_put(sp_t[1], 1, 11, 12);
                const int chance = L > E ? 50 + 5 * (L - E) : L < E ? 50 - 2 * (E - L) : 50;
                sp_give(sp_t[0], 0x01, 30, E, false);
                sp_give(sp_t[1], 0x01, 30, E, false);
                sp_give(sp_t[1], 0x19, 30, 0xFF, false);
                sp_dice(1, 0, chance + over);
                fg->b.aim_x = 10, fg->b.aim_y = 12;
                sp_cast(sp_c, sp, {sp_t[0], sp_t[1]});
                const bool gone0 = sp_aff(sp_t[0], 0x01) < 0, gone1 = sp_aff(sp_t[1], 0x01) < 0;
                const bool kept = sp_aff(sp_t[1], 0x19) >= 0;
                snprintf(w, sizeof w, "Dispel Magic %02X level %d on effects of level %d, d100 %d (chance %d): %s", sp, L, E,
                         chance + over, chance, over ? "kept" : "ended, both targets");
                expect(P, w, gone0 == !over && gone1 == !over && kept, "first %s, second %s, data 0xFF %s",
                       gone0 ? "ended" : "kept", gone1 ? "ended" : "kept", kept ? "kept" : "ended");
            }
    // Magic resistance (4.1): base - 5 x (11 - caster level) as a byte, d100 <= it
    for (int type : {0x69, 0x6A})
        for (int L : {1, 4, 6, 8})
            for (int over = 0; over < 2; ++over) {
                const int base = type == 0x69 ? 50 : 15;
                const int v = (base - 5 * (11 - L)) & 0xFF;
                const int roll = std::min(100, std::max(1, v + over));
                const bool want = roll <= v;
                if (over && v >= 100) continue;
                if (!sp_new_fight() || !sp_pick()) return;
                sp_caster(sp_c, 5, L);
                sp_put(sp_c, 0, 6, 12);
                sp_put(sp_t[0], 1, 10, 12);
                sp_give(sp_t[0], type, 0, 0xFF, false);
                sp_dice(1, 2, roll);
                sp_cast(sp_c, 0x0F, {sp_t[0]});
                const bool res = F(sp_t[0]).hp() == 200;
                snprintf(w, sizeof w, "magic resistance %02X, caster level %d, d100 %d (%d): %s", type, L, roll, v,
                         want ? "resisted" : "hurt");
                expect(P, w, res == want, "%s", res ? "resisted" : "hurt");
            }
}

// ---- 8. Fights: the races' foes by name, backstabs, turning undead, level drain
void set_name(combat::Fighter& f, const char* nm)
{
    memset(f.rec, 0, 16);
    f.rec[0] = static_cast<uint8_t>(strlen(nm));
    memcpy(f.rec + 1, nm, strlen(nm));
}
// The lowest d20 (2-19) that hits; 21 none
int lowest_hit(int a, int c, int ac)
{
    for (int r = 2; r <= 19; ++r)
        if (sp_blow(a, c, r, ac, 0)) return r;      // (d20 + to-hit + effects >= the stored AC)
    return 21;
}
void fights()
{
    using namespace play;
    char w[140];
    // Dwarves +1 against orc-kind, gnomes against kobold-kind (type 1, size 1, by name)
    struct Foe {
        uint8_t effect;
        const char* name;
        int type, size, bonus;
        const char* what;
    };
    const Foe foes[] = {
        {0x1A, "ORC", 1, 1, 1, "a dwarf against an ORC: +1"},
        {0x1A, "HOBGOBLIN CHIEF", 1, 1, 1, "a dwarf against a HOBGOBLIN CHIEF: +1"},
        {0x1A, "KOBOLD", 1, 1, 0, "a dwarf against a KOBOLD: nothing"},
        {0x1A, "ORC", 1, 2, 0, "a dwarf against an ORC of size 2: nothing"},
        {0x1A, "ORC", 2, 1, 0, "a dwarf against an ORC of monster type 2: nothing"},
        {0x12, "KOBOLD LEADER", 1, 1, 1, "a gnome against a KOBOLD LEADER: +1"},
        {0x12, "GOBLIN", 1, 1, 1, "a gnome against a GOBLIN: +1"},
        {0x12, "ORC", 1, 1, 0, "a gnome against an ORC: nothing"},
    };
    for (const Foe& fo : foes) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 11, 12);
        set_name(F(sp_t[0]), fo.name);
        F(sp_t[0]).rec[0x11A] = static_cast<uint8_t>(fo.type);
        F(sp_t[0]).rec[0xDE] = static_cast<uint8_t>(fo.size);
        const int base = lowest_hit(sp_c, sp_t[0], 12);
        sp_give(sp_c, fo.effect, 0, 0xFF, false);
        const int with = lowest_hit(sp_c, sp_t[0], 12);
        snprintf(w, sizeof w, "races: %s to hit", fo.what);
        expect(P, w, base - with == fo.bonus, "lowest d20 %d, %d without", with, base);
    }
    // Dwarves and gnomes -4 to be hit by OGRE / TROLL / FIRE GIANT ... (type 1, size 2);
    // gnomes by BUGBEAR / GNOLL (type 1)
    struct Big {
        uint8_t effect;
        const char* name;
        int size, malus;
        const char* what;
    };
    const Big bigs[] = {
        {0x2F, "OGRE", 2, 4, "an OGRE (size 2) at a dwarf: -4"},
        {0x2F, "TROLL", 2, 4, "a TROLL (size 2) at a dwarf: -4"},
        {0x2F, "OGRE", 1, 0, "an OGRE of size 1: nothing"},
        {0x2F, "ORC", 2, 0, "an ORC of size 2: nothing"},
        {0x30, "BUGBEAR", 1, 4, "a BUGBEAR at a gnome: -4"},
        {0x30, "GNOLL", 2, 4, "a GNOLL at a gnome: -4"},
        {0x30, "ORC", 1, 0, "an ORC at a gnome: nothing"},
    };
    for (const Big& bg : bigs) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_put(sp_t[0], 1, 10, 12);                  // the attacker (a monster)
        sp_put(sp_c, 0, 11, 12);                     // the dwarf / gnome
        set_name(F(sp_t[0]), bg.name);
        F(sp_t[0]).rec[0x11A] = 1;
        F(sp_t[0]).rec[0xDE] = static_cast<uint8_t>(bg.size);
        F(sp_c).target = sp_t[0];                    // (the original compares the name of the one it fights)
        const int base = lowest_hit(sp_t[0], sp_c, 12);
        sp_give(sp_c, bg.effect, 0, 0xFF, false);
        const int with = lowest_hit(sp_t[0], sp_c, 12);
        snprintf(w, sizeof w, "races: %s to hit", bg.what);
        expect(P, w, with - base == bg.malus, "lowest d20 %d, %d without", with, base);
    }

    // Backstab (5.1): a thief straight behind; the damage x (thief level / 4 + 2)
    for (int L : {1, 3, 4, 7, 8, 9}) {
        if (!sp_new_fight() || !sp_pick()) return;
        combat::Fighter& th = F(sp_c);
        sp_caster(sp_c, 6, L);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 11, 12);
        combat::Fighter& tg = F(sp_t[0]);
        // (attacked once this round by one in front of it: the thief's blow, its
        // second, turns it about - so it faces away from the thief, as the original)
        tg.facing = (combat::direction(10, 12, 11, 12) + 4) & 7;
        tg.received = 1;
        for (int i = 0; i < th.n_items; ++i) th.items[i][0x34] = 0;     // nothing readied
        th.rec[0x199] = 100;
        th.attacks[0] = 1;
        th.attacks[1] = 0;
        th.rec[0x19E] = 1;
        th.rec[0x1A0] = 1;
        th.rec[0x1A2] = 0;
        sp_dice(1, 15, 100);
        const combat::Attack r = combat::attack(fg->b, sp_c, sp_t[0], names, rng);
        const int want = L / 4 + 2;
        snprintf(w, sizeof w, "backstab, thief level %d: damage x %d", L, want);
        expect(P, w, r.backstab && r.n > 0 && r.hits[0].hit && r.hits[0].damage == want, "backstab %d, damage %d",
               r.backstab, r.n ? r.hits[0].damage : -1);
    }
    // ... in no body armour but leather
    {
        if (!sp_new_fight() || !sp_pick()) return;
        combat::Fighter& th = F(sp_c);
        sp_caster(sp_c, 6, 4);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 11, 12);
        F(sp_t[0]).facing = combat::direction(10, 12, 11, 12);
        F(sp_t[0]).received = 2;
        int armour = -1;
        for (int i = 0; i < th.n_items; ++i) {
            th.items[i][0x34] = 0;
            if (armour < 0 && names->type(th.items[i][0x2E]).slot == items::kSlotArmour) armour = i;
        }
        if (armour < 0 && th.n_items > 0) armour = 0;
        if (armour >= 0) {
            th.items[armour][0x34] = 1;
            const uint8_t keep = th.items[armour][0x2E];
            th.items[armour][0x2E] = 0x32;                          // leather
            const bool leather = combat::can_backstab(fg->b, sp_c, sp_t[0], names);
            th.items[armour][0x2E] = 0x34;                          // chain mail
            const bool chain = names->type(0x34).slot == items::kSlotArmour && combat::can_backstab(fg->b, sp_c, sp_t[0], names);
            th.items[armour][0x2E] = keep;
            expect(P, "backstab: in leather armour, yes; in other body armour, no", leather && !chain, "leather %d, chain %d",
                   leather, chain);
        }
    }

    // Paralysing hits (4 / 4.1): the ghoul's 63 rounds (not elves), the
    // drider's d9 + 10 (a save at -2), the thri-kreen's 2d8 - effect 0x34
    // (effect groups 2 / 3: after a hit by attack 1 / attack 2 - 0x44 in
    // both, 0x43 and 0x45 after attack 2's only)
    struct Para {
        uint8_t effect;
        int race, rounds, slot;
        const char* what;
    };
    const Para paras[] = {
        {0x44, 7, 63, 1, "the ghoul's touch (attack 1): paralysed 63 rounds"},
        {0x44, 7, 63, 2, "the ghoul's touch (attack 2): paralysed 63 rounds"},
        {0x44, 2, -1, 1, "the ghoul's touch on an elf: nothing"},
        {0x45, 7, 19, 2, "the drider's (attack 2): d9 + 10 rounds (dice high 19)"},
        {0x45, 7, -1, 1, "the drider's attack 1: nothing"},
        {0x43, 7, 16, 2, "the thri-kreen's (attack 2): 2d8 rounds (dice high 16)"},
    };
    for (const Para& pa : paras) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_put(sp_t[0], 1, 10, 12);
        sp_put(sp_c, 0, 11, 12);
        F(sp_c).rec[0x74] = static_cast<uint8_t>(pa.race);
        for (int k = 0; k < 5; ++k) F(sp_c).rec[0xDF + k] = 21;       // (no save but a 20)
        sp_give(sp_t[0], pa.effect, 0, 0xFF, false);
        combat::Fighter& at = F(sp_t[0]);
        at.rec[0x199] = 0;
        at.attacks[0] = pa.slot == 1 ? 1 : 0;
        at.attacks[1] = pa.slot == 2 ? 1 : 0;
        at.rec[0x19E] = at.rec[0x19F] = 1;
        at.rec[0x1A0] = at.rec[0x1A1] = 1;
        at.rec[0x1A2] = at.rec[0x1A3] = 0;
        F(sp_c).rec[0x19A] = F(sp_c).rec[0x19B] = 12;
        fg->b.to_hit_party = fg->b.to_hit_monsters = 0;
        sp_dice(1, 19, 100);
        const combat::Attack r = combat::attack(fg->b, sp_t[0], sp_c, nullptr, rng);
        const bool hit = r.n > 0 && r.hits[0].hit;
        const int t = sp_aff_time(sp_c, 0x34);
        snprintf(w, sizeof w, "monsters: %s", pa.what);
        expect(P, w, hit && t == pa.rounds, "hit %d, paralysed %d", hit, t);
    }

    // Turning undead (5.1): the column by cleric level (1-8, 9-13 -> 9, then 10), d20 against it
    {
        if (!sp_new_fight() || !sp_pick()) return;
        combat::Battle& b = fg->b;
        sp_caster(sp_c, 0, 1);
        sp_put(sp_c, 0, 10, 12);
        sp_put(sp_t[0], 1, 12, 12, 50);
        combat::Fighter& me = F(sp_c);
        combat::Fighter& un = F(sp_t[0]);
        int bad = 0, done = 0;
        std::string first;
        for (int L = 1; L <= 12; ++L)
            for (int type = 1; type <= 10; ++type) {
                const auto* row = b_row("turn", type);
                const int col = L <= 8 ? L : L <= 13 ? 9 : 10;
                const int v = (*row)[1 + col];
                const int need = v < 0 ? -v : v;
                for (int face : {need, need - 1}) {
                    if (face < 1 || face > 20) continue;
                    memset(me.rec + 0x109, 0, 8);
                    me.rec[0x109] = static_cast<uint8_t>(L);
                    me.turned_undead = false;
                    un.rec[0xE9] = static_cast<uint8_t>(type);
                    un.rec[0x195] = party::Okay;
                    un.rec[0x196] = 1;
                    un.size = 1;
                    un.fleeing = false;
                    combat::occupancy(b);
                    int out[16];
                    dice_faces({1, face});
                    const int n = combat::turn_undead(b, fg->t, sp_c, rng, out, 16);
                    dice_usual();
                    const bool want = face >= need;
                    const bool got = n == 1 && (out[0] == sp_t[0] || out[0] == sp_t[0] + 1000);
                    const bool kind_ok = !got || (v > 0 ? out[0] == sp_t[0] : out[0] == sp_t[0] + 1000);
                    ++done;
                    if ((got != want || !kind_ok) && !bad++) {
                        char t[96];
                        snprintf(t, sizeof t, "level %d type %d entry %d d20 %d: %s", L, type, v, face, got ? "turned" : "not");
                        first = t;
                    }
                }
            }
        snprintf(w, sizeof w, "turn undead: Pool's table by cleric level, the d20 against it (%d tries)", done);
        expect(P, w, bad == 0, "%d wrong, first %s", bad, first.c_str());
    }

    // Level drain (4.1, ovr13+2122): HP max / the levels' sum off a level,
    // the highest class (the bigger experience figure on a tie) down one,
    // the experience that level needs
    struct Dr {
        int lv[8], hp, n;
        int want_lv[8], want_hp, cls_exp, lv_exp;
        const char* what;
    };
    const Dr drs[] = {
        {{0, 0, 5}, 40, 1, {0, 0, 4}, 32, 2, 4, "fighter 5, 40 HP, one level"},
        {{0, 0, 5}, 40, 2, {0, 0, 3}, 24, 2, 3, "fighter 5, 40 HP, two levels"},
        {{4, 0, 4}, 40, 1, {4, 0, 3}, 35, 0, 4, "cleric 4 / fighter 4, 40 HP: the fighter (its figure bigger)"},
        {{0, 0, 0, 0, 0, 3, 5}, 30, 1, {0, 0, 0, 0, 0, 3, 4}, 27, 6, 4, "magic-user 3 / thief 5, 30 HP: the thief"},
    };
    for (const Dr& dr : drs) {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_put(sp_c, 0, 10, 12);
        combat::Fighter& f = F(sp_c);
        int hd = 0;
        for (int k = 0; k < 8; ++k) {
            f.rec[0x109 + k] = static_cast<uint8_t>(dr.lv[k]);
            f.rec[0x111 + k] = 0;
            hd = std::max(hd, dr.lv[k]);
        }
        f.rec[0xE5] = static_cast<uint8_t>(hd);
        f.rec[0x78] = f.rec[0x1A4] = static_cast<uint8_t>(dr.hp);
        f.rec[0x12C] = static_cast<uint8_t>(dr.hp);
        put32(f.rec, 0x127, 200000);
        combat::Event ev[8];
        int ne = 0;
        combat::drain(fg->b, sp_c, dr.n, ev, 8, &ne);
        bool lok = true;
        for (int k = 0; k < 8; ++k) lok = lok && f.rec[0x109 + k] == dr.want_lv[k];
        const int want_exp = dr.lv_exp >= 2 ? oxp(dr.cls_exp, dr.lv_exp - 1) : 0;
        const uint32_t exp = f.rec[0x127] | f.rec[0x128] << 8 | f.rec[0x129] << 16 | static_cast<uint32_t>(f.rec[0x12A]) << 24;
        snprintf(w, sizeof w, "level drain: %s", dr.what);
        expect(P, w, lok && f.rec[0x78] == dr.want_hp && f.rec[0x1A4] == dr.want_hp && exp == static_cast<uint32_t>(want_exp) &&
                         f.rec[0xE5] == dr.lv_exp,
               "levels %d/%d/%d/%d, HP %d/%d, exp %u (the original's %d), HD %d", f.rec[0x109], f.rec[0x10B], f.rec[0x10E],
               f.rec[0x10F], f.rec[0x1A4], f.rec[0x78], exp, want_exp, f.rec[0xE5]);
    }
    // One of a single level: dead forever
    {
        if (!sp_new_fight() || !sp_pick()) return;
        sp_put(sp_c, 0, 10, 12);
        combat::Fighter& f = F(sp_c);
        for (int k = 0; k < 8; ++k) f.rec[0x109 + k] = 0;
        f.rec[0x10B] = 1;
        f.rec[0xE5] = 1;
        combat::Event ev[8];
        int ne = 0;
        combat::drain(fg->b, sp_c, 1, ev, 8, &ne);
        expect(P, "level drain: a level-1 character is dead forever", f.status() == party::Gone && f.rec[0x78] == 0,
               "status %d, HP max %d", f.status(), f.rec[0x78]);
    }
}
#endif

} // namespace bpool

static void behave_pool()
{
    using namespace bpool;
    b_oracle.clear();
    if (!b_oracle_load("pool")) {
        expect(P, "oracle file", false, "no oracle_pool.txt");
        return;
    }
    if (!boot()) {
        expect(P, "boot", false);
        return;
    }
    if (!play::load_rules(bpool::m)) {
        expect(P, "rule tables", false);
        return;
    }
    expect(P, "the Pool tables' shapes", m->tables.lay.pool);
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("values")) class_values();
    if (want("con")) constitution();
    if (want("creation")) creation();
    if (want("train")) training();
    if (want("modify")) modify();
    if (want("money")) money();
#ifdef BEHAVE_SPELLS
    if (want("spells")) spells_in_fights();
    if (want("fights")) fights();
#endif
}

// Behaviour checks: Pools of Darkness (included by sweep.cpp with
// -DBEHAVE_DARKNESS; run with GAME=4 on Darkness's folder). Create New
// Character, Modify's bounds and Keep, Human Change, the starting kit and
// the Constitution's hit point recalculation against the oracle (Darkness's
// tables read from the player's program and ITEM0.DAX by a scratch script,
// the constants of the Project's claude/darkness_create_facts.md and its
// rules worked out there) - never against the engine's own code.
// ONLY= create, kit, modify, conhp, change; the fights (behave_darkness_fights.inc):
// fights (all of them), turn ...; drain (behave_darkness_left.inc)
#include "behave_oracle.inc"

namespace bdark {

using namespace play;
Making* m = nullptr;
const char* P = "darkness";

// Darkness's race numbers (0 elf .. 5 human) as the record's (Curse's)
const int kToCurseRace[6] = {2, 4, 1, 3, 5, 7};
const int kMultiBits[17] = {0x01, 0, 0x04, 0x08, 0x10, 0x20, 0x40, 0, 0x05, 0x25, 0x11, 0x21, 0x41, 0x24, 0x44, 0x64, 0x60};

const std::vector<int>* row(const char* key, int a) { return b_row(key, a); }
const std::vector<int>* row2(const char* key, int a, int b) { return b_row(key, a, b); }
int val(const char* key, int idx)
{
    const auto& r = b_rows(key);
    return r.empty() || idx < 0 || idx >= static_cast<int>(r[0].size()) ? 0 : r[0][idx];
}
void set_stat(party::Character& c, int i, int v)
{
    c.rec[0x10 + i * 2] = static_cast<uint8_t>(v);
    c.rec[0x11 + i * 2] = static_cast<uint8_t>(v);
}
int32_t xp_need(int k, int L)
{
    const auto* r = row("xp", k);
    return r && L >= 1 && L < static_cast<int>(r->size()) ? (*r)[L] : -1;
}
int con_fig(int con, int k)
{
    const auto& r = b_rows("conhp");
    int v = r.empty() || con < 0 || con > 25 ? 0 : r[0][con];
    if (k == 2 || k == 3 || k == 4)
        v += con == 17 ? 1 : con == 18 ? 2 : con == 19 || con == 20 ? 3 : con >= 21 && con <= 23 ? 4 : con >= 24 && con <= 25 ? 5 : 0;
    return v;
}
// The hit point routine (facts 43.1) for the classes in `mask`
int want_gain(const int lv[8], int mask, int con, bool high, int held, int* inc)
{
    int rolled = 0, cs = 0;
    for (int k = 0; k < 7; ++k) {
        if (lv[k] <= 0 || !(val("classmask", k) & mask)) continue;
        if (lv[k] < val("lasthd", k)) {
            const int n = lv[k] > 1 ? 1 : val("hpcount", k);
            rolled += high ? n * val("hpsides", k) : n;
            cs += con_fig(con, k) * n;
        } else {
            rolled = k == 2 || k == 3 ? 3 : k == 4 || k == 0 || k == 6 ? 2 : 1;
        }
    }
    int i = (rolled & 0xFF) / held;
    if (i < 1) i = 1;
    *inc = i;
    int cp = rules::fixes() ? static_cast<int8_t>(cs & 0xFF) / held : (cs & 0xFF) / held;
    if (rules::fixes() && i + cp < 1) cp = 1 - i;
    return i + cp;
}
// Training's race limits (darkness_facts.md 36; Darkness race numbers)
bool race_stops(int dr, int k, int L, int str, int intel)
{
    if (L >= 40) return true;
    switch (dr) {
    case 2: return k == 2 && (L == 9 || (L == 8 && str == 17) || (L == 7 && str < 17));
    case 0:
        if (k == 2) return L == 7 || (L == 6 && str == 17) || (L == 5 && str < 17);
        if (k == 5) return L == 11 || (L == 10 && intel == 17) || (L == 9 && intel < 17);
        return false;
    case 3: return (k == 2 && (L == 6 || (L == 5 && str < 18))) || (k == 0 && L >= 7);
    case 1:
        if (k == 0) return L == 5;
        if (k == 2 || k == 4) return L == 8 || (L == 7 && str == 17) || (L == 6 && str < 17);
        if (k == 5) return L == 8 || (L == 7 && intel == 17) || (L == 6 && intel < 17);
        return false;
    case 4: return k == 2 && (L == 6 || (L == 5 && str == 17) || (L == 4 && str < 17));
    default: return false;
    }
}
// The magic-user's book (facts 41) at magic-user level L and Intelligence
std::vector<int> mu_book(int L, int intel)
{
    std::vector<int> book;
    const auto* sl = row("muslots", L);
    int slots[10] = {};
    for (int s = 1; sl && s <= 9; ++s) slots[s] = (*sl)[s];
    if (intel < 12) slots[6] = 0;
    if (intel < 14) slots[7] = 0;
    if (intel < 16) slots[8] = 0;
    if (intel < 18) slots[9] = 0;
    const int all_lv = val("mubook_all", 0);
    for (int n = 1; n <= 126; ++n) {
        const auto* r = row("spell", n);
        if (r && (*r)[1] == 2 && (*r)[2] <= all_lv) book.push_back(n);
    }
    for (const auto& r : b_rows("mubook"))
        if (slots[r[0]] > 0) book.insert(book.end(), r.begin() + 1, r.end());
    if (slots[val("mubook_every", 0)] > 0)
        for (int n = 1; n <= 126; ++n) {
            const auto* r = row("spell", n);
            if (r && (*r)[1] == 2) book.push_back(n);
        }
    return book;
}

// ---- Create New Character (facts 40): every race, class, alignment and sex,
// the dice all highest and all lowest, the rules fixes off (the original)
// and on (AD&D's race adjustments)
void creation()
{
    static party::Character c;
    char w[200];
    int ncase = 0, nbad = 0;
    const auto* sx = row("start_xp", 1500001);
    for (int fix = 0; fix < 2; ++fix)
        for (int dr = 0; dr < 6; ++dr) {
            const auto* rc = row("raceclasses", dr);
            const auto* lim = row("racelim", dr);
            const auto* br = row("agebr", dr);
            if (!rc || !lim || !br || !sx) {
                expect(P, "the oracle's race rows", false);
                return;
            }
            const int race = kToCurseRace[dr];
            for (size_t ci = 1; ci < rc->size(); ++ci) {
                const int cls = (*rc)[ci];
                const auto* al = row("classalign", cls);
                const auto* mn = row("classmin", cls);
                for (size_t ai = 1; al && ai < al->size(); ++ai)
                    for (int sex = 0; sex < 2; ++sex)
                        for (int high = 0; high < 2; ++high) {
                            rules::set_fixes(fix);
                            if (high) dice_high();
                            else dice_low();
                            create::begin(c, m->tables, m->facts, m->dice, race, sex, cls, (*al)[ai]);
                            create::roll(c, m->tables, m->facts, m->dice);
                            dice_usual();
                            ++ncase;
                            std::string why;
                            // The age (40.5)
                            auto entry = [&](int e, bool most) {
                                const auto* a = row2("age", dr, e);
                                if (!a) return 0;
                                const int base = (*a)[2], cnt = (*a)[3], sd = (*a)[4];
                                return base + (most || high ? cnt * sd : cnt);
                            };
                            int age = 0;
                            if (cls <= 6) age = entry(cls, false) + val("age_plus", 0);
                            else if (cls == 8 || cls == 9 || cls == 10 || cls == 11) age = entry(0, true);
                            else if (cls == 13 || cls == 15 || cls == 16) age = entry(5, true);
                            else if (cls == 14) age = entry(2, true);
                            if (c.age() != age) why += " age " + std::to_string(c.age()) + "/" + std::to_string(age);
                            // The stats (40.6)
                            const auto* adj = fix ? row("raceadj", dr) : nullptr;
                            int want[6];
                            for (int i = 0; i < 6; ++i) {
                                int v = (high ? 19 : 4) + (adj ? (*adj)[1 + i] : 0);
                                auto past = [&](int b) { return age > (*br)[1 + b]; };
                                switch (i) {
                                case 0:
                                    if (v > 0 && past(0)) v += 1;
                                    if (v > 0 && past(1)) v -= 1;
                                    if (v > 0 && past(2)) v -= 2;
                                    if (v > 0 && past(3)) v -= 1;
                                    break;
                                case 1:
                                    if (past(1)) v += 1;
                                    if (past(3)) v += 1;
                                    break;
                                case 2:
                                    if (!past(0)) v -= 1;
                                    if (past(1)) v += 1;
                                    if (past(2)) v += 1;
                                    if (past(3)) v += 1;
                                    break;
                                case 3:
                                    if (past(2)) v -= 2;
                                    if (past(3)) v -= 1;
                                    break;
                                case 4:
                                    if (past(1)) v -= 1;
                                    if (past(2)) v -= 1;
                                    if (past(3)) v -= 1;
                                    break;
                                default: break;
                                }
                                const int lo = i == 0 ? (*lim)[1 + sex] : (*lim)[1 + 4 + i * 2];
                                const int hi = i == 0 ? (*lim)[3 + sex] : (*lim)[1 + 5 + i * 2];
                                if (v < lo) v = lo;
                                if (v > hi) v = hi;
                                if (mn && v < (*mn)[1 + i]) v = (*mn)[1 + i];
                                if (i == 2 && v < 13 && cls >= 8 && cls <= 12) v = 13;
                                want[i] = v;
                                if (c.stat(i) != v || c.stat_now(i) != v)
                                    why += " stat" + std::to_string(i) + " " + std::to_string(c.stat(i)) + "/" + std::to_string(v);
                            }
                            const int bits = kMultiBits[cls];
                            int e = 0;
                            if (want[0] == 18 && (bits & 0x1C)) e = std::min(high ? 100 : 1, (*lim)[5 + sex]);
                            if (c.rec[0x1D] != e || c.rec[0x1C] != e) why += " 18/" + std::to_string(c.rec[0x1D]) + " vs " + std::to_string(e);
                            // Experience (40.4) and silent training (40.8)
                            int held = 0;
                            for (int k = 0; k < 7; ++k) held += (bits >> k) & 1;
                            const int32_t xp = held == 1 ? (*sx)[0] : (cls == 9 || cls == 15) ? (*sx)[2] : (*sx)[1];
                            if (static_cast<int32_t>(c.exp()) != xp) why += " xp " + std::to_string(c.exp());
                            int lv[8] = {};
                            for (int k = 0; k < 7; ++k)
                                if ((bits >> k) & 1) lv[k] = 1;
                            int inc = 0;
                            int hpmax = want_gain(lv, 0xFF, want[4], high, held, &inc);
                            int rolled = inc;
                            for (int round = 0; round < 60; ++round) {
                                int mask = 0;
                                for (int k = 0; k < 7; ++k) {
                                    if (!lv[k] || race_stops(dr, k, lv[k], want[0], want[1])) continue;
                                    const int32_t need = xp_need(k, lv[k]);
                                    if (need > 0 && need <= xp) mask |= val("classmask", k);
                                }
                                if (!mask) break;
                                for (int k = 0; k < 7; ++k)
                                    if (lv[k] && (val("classmask", k) & mask)) ++lv[k];
                                hpmax = (hpmax + want_gain(lv, mask, want[4], high, held, &inc)) & 0xFF;
                                rolled = (rolled + inc) & 0xFF;
                            }
                            for (int k = 0; k < 7; ++k)
                                if (c.level(k) != lv[k]) why += " level" + std::to_string(k) + " " + std::to_string(c.level(k)) + "/" + std::to_string(lv[k]);
                            if (c.hp_max() != hpmax || c.hp() != hpmax || c.rec[0x12C] != rolled)
                                why += " hp " + std::to_string(c.hp_max()) + "/" + std::to_string(c.hp()) + " rolled " + std::to_string(c.rec[0x12C]) +
                                       " vs " + std::to_string(hpmax) + " " + std::to_string(rolled);
                            // The effects, in the original's order (40.3, 40.4)
                            std::vector<int> fx;
                            if (const auto* rf = row("racefx", dr)) fx.assign(rf->begin() + 1, rf->end());
                            if (bits & 0x08) fx.push_back(0x08);
                            if (bits & 0x10) fx.push_back(0x69);
                            bool fok = static_cast<int>(fx.size()) == c.n_affects;
                            for (size_t i = 0; fok && i < fx.size(); ++i) fok = c.affects[i][0] == fx[i] && c.affects[i][3] == 0xFF;
                            if (!fok) why += " effects (" + std::to_string(c.n_affects) + ")";
                            if (c.rec[0x191] != ((bits & 0x08) ? 1 : 0)) why += " cures";
                            // Money: 200 platinum, nothing else (40.7)
                            for (int i = 0; i < 7; ++i) {
                                const int wm = i == 4 ? val("money_pp", 0) : 0;
                                if (c.money(i) != wm) why += " money" + std::to_string(i) + " " + std::to_string(c.money(i));
                            }
                            // The book: a cleric's 1st level spells; a magic-user's by its slots (41)
                            if (bits & 0x01)
                                for (int n = 1; n <= 126; ++n) {
                                    const auto* r = row("spell", n);
                                    if (r && (*r)[1] == 0 && (*r)[2] == 1 && !classes::in_book(c, n)) why += " cleric spell " + std::to_string(n) + " missing";
                                }
                            {
                                std::vector<int> mb = (bits & 0x20) ? mu_book(lv[5], want[1]) : std::vector<int>{};
                                // (a ranger knows the magic-user spells of the levels its
                                // record gives it slots of - columns 5 / 6: the GOG ranger's book)
                                if (const auto* rs = (bits & 0x10) ? row("rgslots", lv[4]) : nullptr)
                                    for (int n = 1; n <= 126; ++n) {
                                        const auto* r = row("spell", n);
                                        if (r && (*r)[1] == 2 && (((*r)[2] == 1 && (*rs)[5] > 0) || ((*r)[2] == 2 && (*rs)[6] > 0)))
                                            mb.push_back(n);
                                    }
                                for (int n = 1; n <= 126; ++n) {
                                    const auto* r = row("spell", n);
                                    if (!r || (*r)[1] != 2) continue;
                                    const bool wb = std::find(mb.begin(), mb.end(), n) != mb.end();
                                    if (classes::in_book(c, n) != wb) {
                                        why += " MU spell " + std::to_string(n) + (wb ? " missing" : " extra");
                                        break;
                                    }
                                }
                            }
                            // The first icon (40.11), 0x171, the flags (40.12)
                            const auto* ih = row("icon_head", 5);
                            const auto* ib = row("icon_body", 23);
                            const int size = (*row("racesize", 2))[dr];
                            const int head = dr == 4 ? (*ih)[4] : sex ? (size == 2 ? (*ih)[1] : (*ih)[3]) : (size == 2 ? (*ih)[0] : (*ih)[2]);
                            const int body = (bits & 0x01) ? (*ib)[0] : (bits & 0x10) ? (*ib)[1] : (bits & 0x0C) ? (*ib)[2] : (bits & 0x20) ? (*ib)[3] : (*ib)[4];
                            if (c.rec[0x141] != head || c.rec[0x142] != body || c.rec[0x144] != size)
                                why += " icon " + std::to_string(c.rec[0x141]) + "/" + std::to_string(c.rec[0x142]) + "/" + std::to_string(c.rec[0x144]);
                            if (c.rec[0x126] != 0 || c.rec[0x14B] != 2 || c.ext[party::kExtFlags2] != 2) why += " 0x171 / flags";
                            if (!why.empty()) {
                                ++nbad;
                                snprintf(w, sizeof w, "creation fixes %d race %d class %d align %d sex %d dice %s:", fix, dr, cls, (*al)[ai], sex,
                                         high ? "high" : "low");
                                if (nbad <= 40 || getenv("ALLCREATE")) expect(P, w, false, "%s", why.c_str());
                            }
                        }
            }
        }
    rules::set_fixes(true);
    snprintf(w, sizeof w, "Create New Character: %d race / class / alignment / sex / dice / fixes cases", ncase);
    expect(P, w, nbad == 0, "%d wrong", nbad);
}

// ---- The starting kit (facts 42): each kind's items from ITEM0, in the
// original's order (the last given first), identified, 50 of the bundles,
// readied but the 4th
void kit()
{
    static party::Character c;
    const int kinds[4][2] = {{7, 2}, {7, 6}, {7, 0}, {7, 5}};     // (a fighter, thief, cleric, magic-user)
    for (int k = 1; k <= 4; ++k) {
        c = party::Character{};
        c.rec[0x74] = static_cast<uint8_t>(kinds[k - 1][0]);
        c.rec[0x109 + kinds[k - 1][1]] = 1;
        char w[80];
        snprintf(w, sizeof w, "kit kind %d", k);
        expect(P, w, create::dk_kit_kind(c) == k, "kind %d", create::dk_kit_kind(c));
        const auto* ids = row("kit", k);
        uint8_t id6[6] = {}, recs[6][17];
        memset(recs, 0xFF, sizeof recs);
        for (int j = 1; j <= 6; ++j) {
            id6[j - 1] = static_cast<uint8_t>((*ids)[j]);
            if (const auto* it = row2("kititem", k, j))
                for (int b = 0; b < 17; ++b) recs[j - 1][b] = static_cast<uint8_t>((*it)[2 + b]);
        }
        create::dk_kit(c, k, id6, recs);
        std::vector<int> js;
        for (int j = 1; j <= 6; ++j)
            if (id6[j - 1] && recs[j - 1][0] != 0xFF) js.push_back(j);
        bool ok = c.n_items == static_cast<int>(js.size());
        for (size_t i = 0; ok && i < js.size(); ++i) {
            const int j = js[js.size() - 1 - i];
            const uint8_t* it = c.items[i];
            uint8_t want[17];
            memcpy(want, recs[j - 1], 17);
            want[7] &= 0xF8;
            if ((j == 5 && k == 1) || (j == 4 && k == 4)) want[11] = 50;
            if (j != 4) want[6] = 1;
            ok = memcmp(it + 0x2E, want, 17) == 0;
            for (int b = 0; ok && b < 0x2E; ++b) ok = it[b] == 0;
        }
        snprintf(w, sizeof w, "kit kind %d: %d items as the original's", k, static_cast<int>(js.size()));
        expect(P, w, ok, "%d items", c.n_items);
    }
}

// ---- Modify (facts 43.3-43.5, 44)
void modify()
{
    static party::Character c;
    int n = 0, bad = 0;
    char w[160];
    for (const auto& r : b_rows("bounds")) {
        int lv[8] = {};
        int bits = 0;
        for (int k = 0; k < 7; ++k) {
            lv[k] = r[k];
            if (lv[k]) bits |= 1 << k;
        }
        const int con = r[7];
        c = party::Character{};
        c.rec[0x74] = 7;
        int cls = 0;
        for (int i = 0; i < 17; ++i)
            if (kMultiBits[i] == bits) cls = i;
        c.rec[0x75] = static_cast<uint8_t>(cls);
        int hd = 0;
        for (int k = 0; k < 8; ++k) {
            c.rec[0x109 + k] = static_cast<uint8_t>(lv[k]);
            hd = std::max(hd, lv[k]);
        }
        c.rec[0xE5] = static_cast<uint8_t>(hd);
        for (int i = 0; i < 6; ++i) set_stat(c, i, 12);
        set_stat(c, 4, con);
        const int lo = create::hp_least(c, m->tables, m->facts), hi = create::hp_most(c, m->tables, m->facts);
        ++n;
        if (lo != r[8] || hi != r[9]) {
            ++bad;
            snprintf(w, sizeof w, "Modify bounds levels %d %d %d %d %d %d %d Con %d", lv[0], lv[1], lv[2], lv[3], lv[4], lv[5], lv[6], con);
            if (bad <= 20) expect(P, w, false, "%d..%d, the original's %d..%d", lo, hi, r[8], r[9]);
        }
    }
    snprintf(w, sizeof w, "Modify's hit point bounds: %d cases", n);
    expect(P, w, bad == 0, "%d wrong", bad);
    // Who may be modified: exactly the starting experience
    for (uint32_t xp : {1500001u, 750000u, 500000u, 1500000u, 0u, 25000u, 200000u, 750001u}) {
        c = party::Character{};
        for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(xp >> (8 * i));
        c.rec[0xE6] = 9;                                    // (a former class doesn't matter)
        const bool want = xp == 1500001u || xp == 750000u || xp == 500000u;
        snprintf(w, sizeof w, "Modify allowed at %u experience: %s", xp, want ? "yes" : "no");
        expect(P, w, create::can_modify(c, false, true, true) == want);
    }
    // Keep: the rolled hit points = the maximum less the Constitution's part
    // (43.5); a magic-user's book by its slots again
    for (int con : {3, 9, 15, 16, 17, 18}) {
        c = party::Character{};
        c.rec[0x74] = 7;
        c.rec[0x75] = 13;                                   // fighter / magic-user
        c.rec[0x109 + 2] = 11;
        c.rec[0x109 + 5] = 13;
        c.rec[0xE5] = 13;
        for (int i = 0; i < 6; ++i) set_stat(c, i, 16);
        set_stat(c, 4, con);
        c.rec[0x78] = 90;
        c.rec[0x1A4] = 90;
        create::modify_done(c, m->tables, m->facts);
        // fighter 11 >= its last die 10: 9 dice; magic-user 13 >= 12: 11 dice
        const int part = (((con_fig(con, 2) * 9) + (con_fig(con, 5) * 11)) & 0xFF) / 2;
        snprintf(w, sizeof w, "Modify Keep, fighter 11 / magic-user 13, Con %d: rolled hit points", con);
        expect(P, w, c.rec[0x12C] == ((90 - part) & 0xFF), "%d, the original's %d", c.rec[0x12C], (90 - part) & 0xFF);
        const std::vector<int> mb = mu_book(13, 16);
        bool ok = true;
        for (int s = 1; s <= 126 && ok; ++s) {
            const auto* r = row("spell", s);
            if (r && (*r)[1] == 2) ok = classes::in_book(c, s) == (std::find(mb.begin(), mb.end(), s) != mb.end());
        }
        snprintf(w, sizeof w, "Modify Keep, Con %d: the magic-user's book by its slots", con);
        expect(P, w, ok);
    }
}

// ---- The Constitution's hit point recalculation (facts 43.2; the Secret
// converter's)
void conhp()
{
    static party::Character c;
    int n = 0, bad = 0;
    char w[160];
    for (const auto& r : b_rows("conhpcase")) {
        c = party::Character{};
        c.rec[0x74] = r[15] ? 7 : 1;
        for (int k = 0; k < 7; ++k) {
            c.rec[0x109 + k] = static_cast<uint8_t>(r[k]);
            c.rec[0x111 + k] = static_cast<uint8_t>(r[7 + k]);
        }
        c.rec[0xE6] = static_cast<uint8_t>(r[14]);
        set_stat(c, 4, r[16]);
        c.rec[0x12C] = static_cast<uint8_t>(r[17]);
        const int got = create::dk_con_hp(c, m->tables);
        ++n;
        if (got != r[18]) {
            ++bad;
            if (bad <= 20) expect(P, "Constitution recalculation case", false, "%d, the original's %d (Con %d)", got, r[18], r[16]);
        }
    }
    snprintf(w, sizeof w, "the Constitution's hit point recalculation: %d cases", n);
    expect(P, w, bad == 0, "%d wrong", bad);
}

// ---- Human Change (facts 45)
void change()
{
    static party::Character c;
    char w[160];
    const auto* hc = row("raceclasses", 5);
    int n = 0, bad = 0;
    for (size_t pi = 1; pi < hc->size(); ++pi) {
        const int now = (*hc)[pi];
        for (int st = 14; st <= 18; ++st)
            for (int al = 0; al < 9; al += 4) {
                c = party::Character{};
                c.rec[0x74] = 7;
                c.rec[0x75] = static_cast<uint8_t>(now);
                c.rec[0x109 + now] = 9;
                c.rec[0xE5] = 9;
                c.rec[0x11B] = static_cast<uint8_t>(al);
                for (int i = 0; i < 6; ++i) set_stat(c, i, st);
                set_stat(c, 1, 15);                         // (Intelligence 15: some classes need 17)
                std::vector<int> want;
                const auto* mnow = row("classmin", now);
                bool keep = true;
                for (int i = 0; i < 6; ++i)
                    if ((*mnow)[1 + i] >= 9 && c.stat(i) <= 14) keep = false;
                for (size_t ci = 1; keep && ci < hc->size(); ++ci) {
                    const int k = (*hc)[ci];
                    if (k == now) continue;
                    const auto* mk2 = row("classmin", k);
                    bool ok = true;
                    for (int i = 0; i < 6; ++i)
                        if ((*mk2)[1 + i] >= 9 && c.stat(i) <= 16) ok = false;
                    const auto* ca = row("classalign", k);
                    bool alok = false;
                    for (size_t a = 1; ca && a < ca->size(); ++a) alok = alok || (*ca)[a] == al;
                    if (ok && alok) want.push_back(k);
                }
                int got[17];
                const int ng = create::change_classes(c, m->tables, got, 17);
                ++n;
                bool ok = ng == static_cast<int>(want.size());
                for (int i = 0; ok && i < ng; ++i) ok = got[i] == want[i];
                if (!ok) {
                    ++bad;
                    snprintf(w, sizeof w, "Human Change from class %d, stats %d, alignment %d: the choices", now, st, al);
                    if (bad <= 20) expect(P, w, false, "%d choices, the original's %d", ng, static_cast<int>(want.size()));
                }
            }
    }
    snprintf(w, sizeof w, "Human Change's choices: %d cases", n);
    expect(P, w, bad == 0, "%d wrong", bad);
    // The change itself: a ranger 9 (its 0x69) to a magic-user, a magic-user to a paladin
    for (int t = 0; t < 2; ++t) {
        const int from = t ? 5 : 4, to = t ? 3 : 5;
        c = party::Character{};
        c.rec[0x74] = 7;
        c.rec[0x75] = static_cast<uint8_t>(from);
        c.rec[0x109 + from] = 9;
        c.rec[0xE5] = 9;
        for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = 0x11;
        for (int i = 0; i < 6; ++i) set_stat(c, i, 18);
        if (from == 4) spells::add_affect(c, 0x69, 0, 0xFF, false);
        spells::add_affect(c, 0x21, 0, 0, false);
        memset(c.ext + party::kExtMemorized, 7, 57);
        memset(c.ext + party::kExtBestLevels, 9, 7);
        c.ext[party::kExtMage89] = 3;
        create::change_class(c, m->tables, m->facts, to);
        bool ok = c.exp() == 0 && c.level(from) == 0 && c.old_level(from) == 9 && c.level(to) == 1 && c.rec[0xE6] == 9 &&
                  c.rec[0xE5] == 1 && c.cls() == to && c.rec[0x11C] == 2;
        snprintf(w, sizeof w, "Human Change %d -> %d: experience, levels, former level", from, to);
        expect(P, w, ok);
        bool ext0 = true;
        for (int i = 0; i < 57; ++i) ext0 = ext0 && !c.ext[party::kExtMemorized + i];
        for (int i = 0; i < 7; ++i) ext0 = ext0 && !c.ext[party::kExtBestLevels + i];
        ext0 = ext0 && !c.ext[party::kExtMage89];
        snprintf(w, sizeof w, "Human Change %d -> %d: memorized places, best levels, slots cleared", from, to);
        expect(P, w, ext0);
        bool fx = c.has_affect(0x21) && !c.has_affect(0x69) == (to != 4) && c.has_affect(0x08) == (to == 3);
        snprintf(w, sizeof w, "Human Change %d -> %d: the paladin's / ranger's effects", from, to);
        expect(P, w, fx, "%d effects", c.n_affects);
        if (to == 5) {
            bool b = true;
            for (const auto& r : b_rows("mu_change"))
                for (int s : r) b = b && classes::in_book(c, s);
            expect(P, "Human Change to magic-user: 11 12 18 21 in the book", b);
        }
    }
}

// The game opened and its title passed (the party menu: no party needed)
bool open_game()
{
    play::close();
    const size_t sl = game_dir.rfind('/');
    pc::card_root() = game_dir.substr(0, sl);
    static std::string folder;
    folder = game_dir.substr(sl + 1);
    if (play::open(folder.c_str(), games::Game(4), C, nullptr)) return false;
    for (int guard = 0; play::screen == play::Screen::Title && guard < 40; ++guard) {
        if (play::title_run && play::title_run->prompt) {
            tap_word('P');
            continue;
        }
        play::tap(2, 2, C);
    }
    return play::screen == play::Screen::PartyMenu;
}

#include "behave_darkness_fights.inc"
#include "behave_darkness_left.inc"

bool want_part(const char* s)
{
    const char* only = getenv("ONLY");
    return !only || strstr(only, s) || (strstr(only, "fights") && strcmp(s, "create") && strcmp(s, "kit") &&
                                        strcmp(s, "modify") && strcmp(s, "conhp") && strcmp(s, "change") &&
                                        strcmp(s, "drain") && strcmp(s, "camp") && strcmp(s, "menus"));
}

} // namespace bdark

static void behave_darkness()
{
    using namespace bdark;
    b_oracle.clear();
    if (!b_oracle_load("darkness")) {
        expect(P, "oracle file", false, "no oracle_darkness.txt");
        return;
    }
    if (!b_oracle_load("darkness_fights")) expect(P, "oracle file", false, "no oracle_darkness_fights.txt");
    if (!b_oracle_load("darkness_left")) expect(P, "oracle file", false, "no oracle_darkness_left.txt");
    if (!open_game()) {
        expect(P, "the game opened", false);
        return;
    }
    if (!play::load_rules(bdark::m)) {
        expect(P, "rule tables", false);
        return;
    }
    expect(P, "the Darkness tables' shapes", m->tables.lay.dk);
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("create")) creation();
    if (want("kit")) kit();
    if (want("modify")) modify();
    if (want("conhp")) conhp();
    if (want("change")) change();
    dkfight::checks(want_part);
    dkleft::checks(want_part);
}

// Behaviour checks: Gateway to the Savage Frontier (included by sweep.cpp
// with -DBEHAVE_GATEWAY; run with GAME=8 on Gateway's folder). Create New
// Character - every race, class, alignment and sex with the dice all
// highest and all lowest - against the oracle: the experience a new
// character starts with (3,000 shared evenly by its classes, GAME.OVR
// 0x2650E +0xF63), its platinum, the levels that experience buys (the
// program's class records), the race's classes and the class's
// alignments (the program's tables, read by a scratch script into
// $ORACLE_DIR/oracle_gateway.txt) - never against the engine's own code.
// ONLY= create, menus
#include "behave_oracle.inc"

namespace bgate {

using namespace play;
Making* m = nullptr;
const char* P = "gateway";

// A class code's classes (bit k: class k - Curse's codes 0-16)
const int kBits[17] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x05, 0x25, 0x11, 0x21, 0x41, 0x24, 0x44, 0x64, 0x60};

const std::vector<int>* row(const char* key, int a) { return b_row(key, a); }
int val(const char* key)
{
    const auto& r = b_rows(key);
    return r.empty() || r[0].empty() ? 0 : r[0][0];
}

// The level experience buys in class k (the oracle's need for levels 2-12)
int level_for(int k, int32_t xp)
{
    const auto* r = row("xp", k);
    int lv = 1;
    // (the row: class, then levels 0 and 1's place, then levels 2-12's need)
    for (int L = 2; r && L <= 12 && L + 1 < static_cast<int>(r->size()); ++L)
        if ((*r)[L + 1] > 0 && xp >= (*r)[L + 1]) lv = L;
    return lv;
}

void creation()
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    const int start = val("start_xp"), plat = val("platinum");
    if (!start || b_rows("raceclasses").empty()) {
        expect(P, "the oracle's creation rows", false);
        return;
    }
    for (const auto& rc : b_rows("raceclasses")) {
        const int race = rc[0];
        for (size_t ci = 1; ci < rc.size(); ++ci) {
            const int cls = rc[ci];
            const auto* al = row("classalign", cls);
            for (size_t ai = 1; al && ai < al->size(); ++ai)
                for (int sex = 0; sex < 2; ++sex)
                    for (int high = 0; high < 2; ++high) {
                        if (high) dice_high();
                        else dice_low();
                        create::begin(c, m->tables, m->facts, m->dice, race, sex, cls, (*al)[ai]);
                        create::roll(c, m->tables, m->facts, m->dice);
                        dice_usual();
                        ++ncase;
                        std::string why;
                        const int bits = kBits[cls];
                        int held = 0;
                        for (int k = 0; k < 8; ++k) held += (bits >> k) & 1;
                        const int32_t xp = start / held;
                        if (static_cast<int32_t>(c.exp()) != xp) why += " xp " + std::to_string(c.exp());
                        if (c.money(4) != plat) why += " platinum " + std::to_string(c.money(4));
                        for (int k = 0; k < 8; ++k) {
                            const int want = (bits >> k) & 1 ? level_for(k, xp) : 0;
                            if (c.level(k) != want)
                                why += " class" + std::to_string(k) + " level " + std::to_string(c.level(k)) + "/" +
                                       std::to_string(want);
                        }
                        if (c.hp_max() < 1) why += " no hit points";
                        if (c.alignment() != (*al)[ai]) why += " alignment";
                        if (!why.empty()) {
                            if (nbad < 12) {
                                char w[160];
                                snprintf(w, sizeof w, "race %d class %d align %d sex %d %s:%s", race, cls, (*al)[ai],
                                         sex, high ? "high" : "low", why.c_str());
                                expect(P, w, false);
                            }
                            ++nbad;
                        }
                    }
        }
    }
    char w[96];
    snprintf(w, sizeof w, "creation: %d characters as the original makes them", ncase);
    expect(P, w, nbad == 0, nbad ? (std::to_string(nbad) + " wrong").c_str() : "");
}

// ---- The shop's, temple's and treasure's menus: the words they offer for
// each state of the counter and the party (the oracle's keys and what each
// word needs - the original's overlays)
std::string keys_of(const text::Menu& mn)
{
    std::string k;
    for (int i = 0; i < mn.count; ++i) k += text::key(mn, i);
    return k;
}

void menus()
{
    if (!pt->count) {
        expect(P, "menus: a party", false);
        return;
    }
    static uint8_t keep_money[party::kMaxParty][14];
    for (int i = 0; i < pt->count; ++i) memcpy(keep_money[i], pt->m[i].rec + 0xFB, 14);
    const int keep_items = pt->sel()->n_items;
    int ncase = 0, nbad = 0;
    for (const char* kind : {"shop", "temple", "treasure"}) {
        const std::string key = std::string(kind) + "word";
        for (int items = 0; items < 2; ++items)
            for (int coins = 0; coins < 2; ++coins)
                for (int ground_items = 0; ground_items < 2; ++ground_items)
                    for (int party_coins = 0; party_coins < 2; ++party_coins) {
                        for (int i = 0; i < pt->count; ++i) memset(pt->m[i].rec + 0xFB, 0, 14);
                        if (party_coins) pt->m[0].rec[0xFB + 3 * 2] = 1;
                        pt->sel()->n_items = items ? 1 : 0;
                        ground->clear();
                        if (coins) ground->money[3] = 5;
                        if (ground_items) ground->n = 1;
                        std::string want;
                        for (const auto& r : b_rows(key.c_str())) {
                            const int need = r[1];
                            const bool on = need == 0 || (need == 1 && items) || (need == 2 && coins) ||
                                            (need == 3 && party_coins) || (need == 4 && (coins || ground_items));
                            if (on) want += static_cast<char>(r[0]);
                        }
                        if (!strcmp(kind, "treasure")) {
                            temple = false;
                            open_treasure(C);
                        } else {
                            treasure_off();
                            temple = !strcmp(kind, "temple");
                            screen = Screen::Shop;
                            shop_menu(C);
                        }
                        const std::string got = keys_of(menu);
                        ++ncase;
                        if (got != want) {
                            if (nbad < 8) {
                                char w[160];
                                snprintf(w, sizeof w, "%s items %d coins %d ground items %d party coins %d: %s / %s",
                                         kind, items, coins, ground_items, party_coins, got.c_str(), want.c_str());
                                expect(P, w, false);
                            }
                            ++nbad;
                        }
                    }
    }
    treasure_off();
    temple = false;
    ground->clear();
    pt->sel()->n_items = keep_items;
    for (int i = 0; i < pt->count; ++i) memcpy(pt->m[i].rec + 0xFB, keep_money[i], 14);
    screen = Screen::PartyMenu;
    char w[96];
    snprintf(w, sizeof w, "menus: %d shop / temple / treasure states as the original offers them", ncase);
    expect(P, w, nbad == 0, nbad ? (std::to_string(nbad) + " wrong").c_str() : "");
}

bool open_game()
{
    play::close();
    const size_t sl = game_dir.rfind('/');
    pc::card_root() = game_dir.substr(0, sl);
    static std::string folder;
    folder = game_dir.substr(sl + 1);
    if (play::open(folder.c_str(), games::Game::GatewayToTheSavageFrontier, C, nullptr)) return false;
    for (int guard = 0; play::screen == play::Screen::Title && guard < 40; ++guard) {
        if (play::title_run && play::title_run->prompt) {
            tap_word('P');
            continue;
        }
        play::tap(2, 2, C);
    }
    return play::screen == play::Screen::PartyMenu;
}

} // namespace bgate

static void behave_gateway()
{
    using namespace bgate;
    if (!b_oracle_load("gateway")) {
        expect(P, "oracle file", false, "no oracle_gateway.txt");
        return;
    }
    if (!open_game()) {
        expect(P, "the game opened", false);
        return;
    }
    if (!play::load_rules(bgate::m)) {
        expect(P, "rule tables", false);
        return;
    }
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("create")) creation();
    if (want("menus")) {
        // (a party: the folder's save A - the run's copy has the test saves)
        play::find_saves();
        if (!play::load_game('A')) expect(P, "menus: save A loaded", false);
        else menus();
    }
}

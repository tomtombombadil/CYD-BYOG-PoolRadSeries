// The behaviour checks' oracle (included by behave_classes.inc and
// behave_saves.inc): expected values that come from the original game
// (coab's facts, the program's listing) and the AD&D rules, kept OUTSIDE
// the repo in a scratch file - $ORACLE_DIR/oracle_<part>.txt (default
// /tmp/claude-0/bt), one row a line: "<key> <int> <int> ...", '#' comments.
#ifndef BEHAVE_ORACLE_INC
#define BEHAVE_ORACLE_INC
#include <algorithm>
#include <map>
#include <sstream>
#include <fstream>

static std::map<std::string, std::vector<std::vector<int>>> b_oracle;

static bool b_oracle_load(const char* part)
{
    const char* dir = getenv("ORACLE_DIR");
    std::string path = std::string(dir ? dir : "/tmp/claude-0/bt") + "/oracle_" + part + ".txt";
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t h = line.find('#');
        if (h != std::string::npos) line.erase(h);
        std::istringstream s(line);
        std::string key;
        if (!(s >> key)) continue;
        std::vector<int> row;
        int v;
        while (s >> v) row.push_back(v);
        b_oracle[key].push_back(row);
    }
    return !b_oracle.empty();
}

// The rows of a key
static const std::vector<std::vector<int>>& b_rows(const char* key)
{
    static const std::vector<std::vector<int>> none;
    auto it = b_oracle.find(key);
    return it == b_oracle.end() ? none : it->second;
}
// The first row of `key` starting with a (and b, when given)
static const std::vector<int>* b_row(const char* key, int a, int b = INT32_MIN)
{
    for (const auto& r : b_rows(key))
        if (!r.empty() && r[0] == a && (b == INT32_MIN || (r.size() > 1 && r[1] == b))) return &r;
    return nullptr;
}

// The Krynn games' creation roll (oracle rows statlim, agebr, classmin,
// ageeff, raceadj, adjtested, wis13 - the program's tables and its roll
// code's constants): the six stats a character rolled with every 3d6 at
// `raw` - 1 (each roll 3d6 + 1) should have; the race the original's code
// adjusts by is `adjtested`, the race itself with Game Fixes. "" when they
// match, else what differs (-1 when the oracle lacks a row)
[[maybe_unused]] static std::string b_krynn_stats(const uint8_t* rec, int race, int sex, int cls, int age, int raw,
                                                  bool fixes)
{
    const auto* lim = b_row("statlim", race);
    const auto* br = b_row("agebr", race);
    const auto* mn = b_row("classmin", cls);
    const auto& ats = b_rows("adjtested");
    const int tested = ats.empty() || ats[0].empty() ? -1 : ats[0][0];
    const auto* ad = b_row("raceadj", fixes ? race : tested);
    const auto& w13 = b_rows("wis13");
    if (!lim || !br || !mn || !ad || w13.empty() || b_rows("ageeff").size() != 6) return " -1 no oracle rows";
    std::string why;
    for (int i = 0; i < 6; ++i) {
        const auto* ef = b_row("ageeff", i);
        int v = raw + (*ad)[1 + i];
        for (int b = 0; b < 4; ++b)
            if ((*br)[1 + b] < age) v += (*ef)[1 + b];
        if ((*br)[1] >= age) v += (*ef)[5];        // (the young's)
        const int lo = i == 0 ? (*lim)[1 + sex] : (*lim)[1 + 4 + i * 2];
        const int hi = i == 0 ? (*lim)[1 + 2 + sex] : (*lim)[1 + 5 + i * 2];
        if (v < lo) v = lo;
        if (v > hi) v = hi;
        if (v < (*mn)[1 + i]) v = (*mn)[1 + i];
        if (i == 2 && v < 13 && std::find(w13[0].begin(), w13[0].end(), cls) != w13[0].end()) v = 13;
        const int got = rec[0x11 + i * 2];
        if (got != v) why += " stat" + std::to_string(i) + " " + std::to_string(got) + "/" + std::to_string(v);
    }
    return why;
}

#if defined(BEHAVE_CHAMPIONS) || defined(BEHAVE_DEATHKNIGHTS)
// Every race / class (its first alignment and god), both sexes, the dice
// all lowest / a d6's 3 / all highest, Game Fixes off (the original: no race
// adjusts the rolls) and on (the races' own)
static void b_krynn_stats_check(play::Making* m, const char* P)
{
    static party::Character c;
    int ncase = 0, nbad = 0;
    std::vector<int> aligns;
    for (const auto& r : b_rows("alignlist"))
        for (int v : r) aligns.push_back(v);
    for (const auto& rc : b_rows("raceclasses")) {
        const int race = rc[0];
        const bool dwarf = race == 3 || race == 4;
        for (size_t ci = 1; ci < rc.size(); ++ci) {
            const int cls = rc[ci];
            const auto* cm = b_row("classmask", cls);
            int a = -1;
            for (int ai = 0; ai < 6 && a < 0 && cm && ai < static_cast<int>(aligns.size()); ++ai)
                if ((*cm)[1] >> ai & 1) a = aligns[ai];
            if (a < 0) {
                ++nbad;
                continue;
            }
            for (int fx = 0; fx < 2; ++fx)
                for (int sex = 0; sex < 2; ++sex)
                    for (int dm = 0; dm < 3; ++dm) {
                        rules::set_fixes(fx == 1);
                        if (dm == 0) dice_low();
                        else if (dm == 1) dice_mid();
                        else dice_high();
                        // (a cleric's god - any the race may have; the others none)
                        create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + race, sex, cls, a, 0);
                        if (c.level(0))
                            create::begin(c, m->tables, m->facts, m->dice, classes::kKrynnRace + race, sex, cls, a,
                                          dwarf ? 6 : 1);
                        create::roll(c, m->tables, m->facts, m->dice);
                        dice_usual();
                        ++ncase;
                        const std::string why = b_krynn_stats(c.rec, race, sex, cls, c.age(), dm == 0 ? 4 : dm == 1 ? 10 : 19,
                                                              fx == 1);
                        if (!why.empty()) {
                            if (nbad < 12) {
                                char w[200];
                                snprintf(w, sizeof w, "stats: race %d class %d sex %d dice %s fixes %s age %d:%s", race,
                                         cls, sex, dm == 0 ? "low" : dm == 1 ? "mid" : "high", fx ? "on" : "off", c.age(),
                                         why.c_str());
                                expect(P, w, false);
                            }
                            ++nbad;
                        }
                    }
        }
    }
    rules::set_fixes(true);
    char w[120];
    snprintf(w, sizeof w, "stats: %d rolls as the original (no race adjustment; the races' own with Game Fixes)", ncase);
    expect(P, w, nbad == 0 && ncase > 0, nbad ? "%d wrong" : "%s", nbad ? std::to_string(nbad).c_str() : "");
}
#endif
#endif

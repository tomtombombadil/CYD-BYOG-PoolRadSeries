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
#endif

#include "games.h"

#include <cctype>
#include <cstring>

namespace games {

namespace {

// Folder name upper-cased with everything but letters and digits removed,
// so "Pool of Radiance", "POOL_RAD" and "poolrad" all read POOLOFRADIANCE /
// POOLRAD.
void squash(const char* in, char* out, size_t cap)
{
    size_t o = 0;
    for (; *in && o + 1 < cap; ++in) {
        const unsigned char c = static_cast<unsigned char>(*in);
        if (isalnum(c)) out[o++] = static_cast<char>(toupper(c));
    }
    out[o] = 0;
}

bool has(const char* s, const char* word) { return strstr(s, word) != nullptr; }

} // namespace

const char* title(Game g)
{
    switch (g) {
    case Game::PoolOfRadiance:          return "Pool of Radiance";
    case Game::CurseOfTheAzureBonds:    return "Curse of the Azure Bonds";
    case Game::SecretOfTheSilverBlades: return "Secret of the Silver Blades";
    case Game::PoolsOfDarkness:         return "Pools of Darkness";
    case Game::Unknown:                 break;
    }
    return "Unknown Gold Box Game";
}

const char* short_title(Game g)
{
    switch (g) {
    case Game::PoolOfRadiance:          return "Pool of Radiance";
    case Game::CurseOfTheAzureBonds:    return "Azure Bonds";
    case Game::SecretOfTheSilverBlades: return "Silver Blades";
    case Game::PoolsOfDarkness:         return "Pools of Darkness";
    case Game::Unknown:                 break;
    }
    return "Unknown Game";
}

int number(Game g) { return static_cast<int>(g); }

Game from_folder_name(const char* name)
{
    char s[64];
    squash(name, s, sizeof s);
    // Darkness first: "POOLS OF DARKNESS" / "POOLDARK" also contain POOL.
    if (has(s, "DARK")) return Game::PoolsOfDarkness;
    if (has(s, "SILVER") || has(s, "BLADE") || has(s, "SOTSB")) return Game::SecretOfTheSilverBlades;
    if (has(s, "CURSE") || has(s, "AZURE") || has(s, "COTAB") || has(s, "BONDS")) return Game::CurseOfTheAzureBonds;
    if (has(s, "RADIANCE") || has(s, "POOLRAD") || has(s, "POR")) return Game::PoolOfRadiance;
    return Game::Unknown;
}

} // namespace games

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

struct Info {
    const char* title;
    const char* short_title;
    const char* folder;
    Series      series;
    int         order;
};

const Info& info(Game g)
{
    static const Info kInfo[kGameCount] = {
        {"Unknown Gold Box Game", "Unknown Game", "", Series::Unknown, 99},
        {"Pool of Radiance", "Pool of Radiance", "Pool of Radiance", Series::PoolOfRadiance, 1},
        {"Curse of the Azure Bonds", "Azure Bonds", "Curse of the Azure Bonds", Series::PoolOfRadiance, 2},
        {"Secret of the Silver Blades", "Silver Blades", "Secret of the Silver Blades", Series::PoolOfRadiance, 3},
        {"Pools of Darkness", "Pools of Darkness", "Pools of Darkness", Series::PoolOfRadiance, 4},
        {"Champions of Krynn", "Champions of Krynn", "Champions of Krynn", Series::Krynn, 5},
        {"Death Knights of Krynn", "Death Knights", "Death Knights of Krynn", Series::Krynn, 6},
        {"The Dark Queen of Krynn", "Dark Queen", "The Dark Queen of Krynn", Series::Krynn, 7},
        {"Gateway to the Savage Frontier", "Gateway", "Gateway to the Savage Frontier", Series::SavageFrontier, 8},
        {"Treasures of the Savage Frontier", "Treasures", "Treasures of the Savage Frontier", Series::SavageFrontier, 9},
        {"Unlimited Adventures", "Unlimited Adv.", "Unlimited Adventures", Series::Unlimited, 10},
    };
    const int i = static_cast<int>(g);
    return kInfo[i >= 0 && i < kGameCount ? i : 0];
}

} // namespace

const char* title(Game g) { return info(g).title; }
const char* short_title(Game g) { return info(g).short_title; }
const char* folder_hint(Game g) { return info(g).folder; }
Series series(Game g) { return info(g).series; }
bool main_series(Game g) { return series(g) == Series::PoolOfRadiance; }
int list_order(Game g) { return info(g).order; }
int number(Game g) { return list_order(g); }

Game from_folder_name(const char* name)
{
    char s[64];
    squash(name, s, sizeof s);
    // Order matters: "THE DARK QUEEN OF KRYNN" contains DARK (Pools of
    // Darkness), "POOLS OF DARKNESS" contains POOL, and both Savage
    // Frontier games contain SAVAGE.
    if (has(s, "UNLIMIT") || has(s, "FRUA")) return Game::UnlimitedAdventures;
    if (has(s, "QUEEN") || has(s, "DQK")) return Game::DarkQueenOfKrynn;
    if (has(s, "DEATH") || has(s, "KNIGHT") || has(s, "DKK")) return Game::DeathKnightsOfKrynn;
    if (has(s, "CHAMPION") || has(s, "KRYNN")) return Game::ChampionsOfKrynn;
    if (has(s, "TREASURE")) return Game::TreasuresOfTheSavageFrontier;
    if (has(s, "GATEWAY") || has(s, "SAVAGE")) return Game::GatewayToTheSavageFrontier;
    if (has(s, "DARK")) return Game::PoolsOfDarkness;
    if (has(s, "SILVER") || has(s, "BLADE") || has(s, "SECRET") || has(s, "SOTSB")) return Game::SecretOfTheSilverBlades;
    if (has(s, "CURSE") || has(s, "AZURE") || has(s, "COTAB") || has(s, "BONDS")) return Game::CurseOfTheAzureBonds;
    if (has(s, "RADIANCE") || has(s, "POOLRAD") || has(s, "POR")) return Game::PoolOfRadiance;
    return Game::Unknown;
}

} // namespace games

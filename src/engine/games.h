// The Gold Box games this engine knows, and how a folder on the SD card is
// recognised as one of them.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// The player copies each game's folder (the original DOS files, from their
// own copy) into /GOLDBOX/ on the card. Any folder name works; the game is
// recognised from words in the name: Tom's short names (POOLRAD, CURSE,
// SECRET, DARKNESS, CHAMPIONS, DEATH, QUEEN, GATEWAY, TREASURE, UNLIMIT),
// the full titles (as GOG names its install folders), or words like AZURE,
// SILVER, KRYNN, SAVAGE... A folder with game files but no matching name
// still shows up, as "Unknown Gold Box game".
//
// The engine is for the four Pool of Radiance games first; the others
// (Krynn, Savage Frontier, Unlimited Adventures) are recognised so their
// files can be looked at, and come later (SPEC section 9).
#pragma once

#include <cstdint>

namespace games {

// Values are fixed (they may end up in settings files): append only.
enum class Game : uint8_t {
    Unknown,
    PoolOfRadiance,
    CurseOfTheAzureBonds,
    SecretOfTheSilverBlades,
    PoolsOfDarkness,
    ChampionsOfKrynn,
    DeathKnightsOfKrynn,
    DarkQueenOfKrynn,
    GatewayToTheSavageFrontier,
    TreasuresOfTheSavageFrontier,
    UnlimitedAdventures,
};
constexpr int kGameCount = 11;   // including Unknown

enum class Series : uint8_t { Unknown, PoolOfRadiance, Krynn, SavageFrontier, Unlimited };

constexpr const char* kRootDir = "/GOLDBOX";

const char* title(Game g);
const char* short_title(Game g);      // fits a narrow key
// The folder name the README suggests (POOLRAD, CURSE, ...); "" for Unknown.
const char* folder_hint(Game g);
Series series(Game g);
// One of the four games this engine is being built for.
bool main_series(Game g);
// Order in the library: the Pool of Radiance series 1-4, then Krynn 5-7,
// Savage Frontier 8-9, Unlimited Adventures 10; Unknown 99.
int list_order(Game g);
// Kept for older callers: release order 1-4 within the main series, else
// list_order().
int number(Game g);

// Recognises a game from its folder name (case-insensitive).
Game from_folder_name(const char* name);

} // namespace games

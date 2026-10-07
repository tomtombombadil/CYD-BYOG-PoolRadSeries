// The four games this engine is for, and how a folder on the SD card is
// recognised as one of them.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// The player copies each game's folder (the original DOS files, from their
// own copy) into /GOLDBOX/ on the card. Any folder name works; the game is
// recognised from words in the name (POOLRAD, "Pool of Radiance", CURSE,
// AZURE, SILVER, BLADES, DARK...). A folder with DAX files but no matching
// name still shows up, as "Unknown Gold Box game".
#pragma once

#include <cstdint>

namespace games {

enum class Game : uint8_t { Unknown, PoolOfRadiance, CurseOfTheAzureBonds, SecretOfTheSilverBlades, PoolsOfDarkness };

constexpr const char* kRootDir = "/GOLDBOX";

const char* title(Game g);
const char* short_title(Game g);      // fits a narrow list column
// Release order 1-4, 0 for Unknown.
int number(Game g);

// Recognises a game from its folder name (case-insensitive).
Game from_folder_name(const char* name);

} // namespace games

// The engine's own settings, saved in LittleFS (/settings.bin).
#pragma once

#include <cstdint>

// Game sound (Tom, 2026-10-09): Tandy unless the player picks the PC speaker or Off
enum SoundMode : uint8_t { kSoundUnset = 0, kSoundTandy = 1, kSoundPc = 2, kSoundOff = 3 };
constexpr uint8_t kDefaultVolume = 230, kMinVolume = 10;

struct Settings {
    uint8_t brightness = 200;     // backlight 0-255 (floor kMinBrightness)
    uint8_t scale_15x  = 0;       // 480x320 panels: show the game 1.5x (else 1:1)
    uint8_t flipped    = 0;       // screen turned 180 degrees
    uint8_t sound      = kSoundTandy;     // Tandy (the default; 0 in older files = Tandy too), PC speaker, Off
    uint8_t volume     = kDefaultVolume;  // game sound 0-255 (0 in older files: kDefaultVolume)
    uint8_t large_icons = 0;      // 320x240: the icon editor's big preview (Tom, v0.59.0; 480x320 always;
                                  // CYD_BIG_ICON_PREVIEW in app/features.h)
    uint8_t comforts_off = 0;     // the engine comforts the player switched off (kOff*; 0 = all on,
                                  // as older files read; Tom, 2026-10-11)
    uint8_t game_font = 0;        // the game's text: kFontGame (the default - Tom, 2026-10-11), kFontClear
                                  // (the engine's Clear font), kFontDarkness (the player's Pools of Darkness one)
};

// comforts_off bits (app/features.h)
// game_font (CYD_CLEAR_FONT in app/features.h)
constexpr uint8_t kFontGame = 0, kFontClear = 1, kFontDarkness = 2;
constexpr uint8_t kOffGallery = 1, kOffBigIcon = 2, kOffDemoTap = 4, kOffJournal = 8, kOffMoreGuys = 16;


constexpr uint8_t kMinBrightness = 20;

Settings settings_load();
void settings_save(const Settings& s);

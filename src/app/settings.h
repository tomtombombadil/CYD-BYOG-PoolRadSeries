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
};


constexpr uint8_t kMinBrightness = 20;

Settings settings_load();
void settings_save(const Settings& s);

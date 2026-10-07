// The engine's own settings, saved in LittleFS (/settings.bin).
#pragma once

#include <cstdint>

struct Settings {
    uint8_t brightness = 200;     // backlight 0-255 (floor kMinBrightness)
    uint8_t scale_15x  = 0;       // 480x320 panels: show the game 1.5x (else 1:1)
    uint8_t flipped    = 0;       // screen turned 180 degrees
};

constexpr uint8_t kMinBrightness = 20;

Settings settings_load();
void settings_save(const Settings& s);

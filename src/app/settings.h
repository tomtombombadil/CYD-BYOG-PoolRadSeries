// The engine's own settings, saved in LittleFS (/settings.bin).
#pragma once

#include <cstdint>

// Game sound (Tom, 2026-10-09): Tandy unless the player picks another. Off
// (3) only in older files: since 2026-10-24 Mute does it (read as Tandy,
// muted). The later games' own effects (Tom, 2026-10-24): Darkness /
// Gateway / Treasures = that game's ZOUNDS.DAX recordings for an older
// game's effects (a choice only with that game on the card; in those
// games themselves the only mode, their own)
enum SoundMode : uint8_t {
    kSoundUnset = 0, kSoundTandy = 1, kSoundPc = 2, kSoundOff = 3,
    kSoundDarkness = 4, kSoundGateway = 5, kSoundTreasures = 6,
};
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
                                  // (the engine's Clear font), kFontClearCaps (its capitals only), kFontDarkness (the player's Pools of Darkness one)
    uint8_t volume_muted = 0;     // muted (Tom, 2026-10-24): the Mute key (volume keeps its level), or
                                  // Volume's - down to 0% (volume then keeps 5%'s)
};

// The Volume key (Tom, 2026-10-24): 0-100% in 5% steps, kept in the 0-255
// volume byte - a step's byte is the nearest to its percentage, and the
// percentage shown the nearest step to the byte, so - then + comes back to
// the same byte (no drift). 0% = muted (volume_muted; the byte stays 5%'s;
// the Mute key leaves the byte as it was, so un-muting comes back to it).
inline int settings_volume_pct(const Settings& s)
{
    if (s.volume_muted) return 0;
    const int k = (s.volume * 20 + 127) / 255;          // nearest 5% step
    return (k < 1 ? 1 : k > 20 ? 20 : k) * 5;
}
inline void settings_set_volume_pct(Settings& s, int pct)
{
    if (pct > 100) pct = 100;
    const int k = pct <= 0 ? 1 : (pct + 2) / 5;
    s.volume = static_cast<uint8_t>((k * 255 + 10) / 20);
    s.volume_muted = pct <= 0;
}
// What the sound plays at (0 when muted)
inline uint8_t settings_sound_volume(const Settings& s) { return s.volume_muted ? 0 : s.volume; }

// comforts_off bits (app/features.h)
// game_font (CYD_CLEAR_FONT in app/features.h)
constexpr uint8_t kFontGame = 0, kFontClear = 1, kFontDarkness = 2, kFontClearCaps = 3;   // (the stored values)
constexpr uint8_t kOffGallery = 1, kOffBigIcon = 2, kOffDemoTap = 4, kOffJournal = 8, kOffMoreGuys = 16, kOffPortrait = 32,
                  kOffItemFixes = 64;


constexpr uint8_t kMinBrightness = 20;

Settings settings_load();
void settings_save(const Settings& s);

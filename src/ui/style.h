// Colours and sizes for the engine's own screens (library, viewer,
// settings). The games' screens use the games' own palettes.
//
// Dark theme in the CYD-Classic-Games style: black background, night-sky
// navy keys, cream text, gold accents. RGB565.
#pragma once

#include <cstdint>

namespace style {

constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t kBackground = rgb(0, 0, 0);
constexpr uint16_t kKey        = rgb(24, 38, 82);      // navy
constexpr uint16_t kKeyEdge    = rgb(70, 92, 150);
constexpr uint16_t kKeyDim     = rgb(40, 40, 48);      // disabled
constexpr uint16_t kKeyLit     = rgb(150, 112, 30);    // selected / on
constexpr uint16_t kText       = rgb(240, 230, 200);   // cream
constexpr uint16_t kTextMuted  = rgb(150, 145, 130);
constexpr uint16_t kGold       = rgb(232, 184, 64);
constexpr uint16_t kWarn       = rgb(220, 90, 70);
constexpr uint16_t kHeader     = rgb(16, 24, 56);

} // namespace style

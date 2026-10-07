#include "settings.h"

#include <Arduino.h>
#include <LittleFS.h>

#include "hal/storage.h"

namespace {

constexpr const char* kPath  = "/settings.bin";
constexpr uint32_t    kMagic = 0x53455431;   // "SET1"

struct File1 {
    uint32_t magic;
    Settings s;
    uint8_t  reserved[13];
};

} // namespace

Settings settings_load()
{
    Settings s;
    if (!storage_begin() || !LittleFS.exists(kPath)) return s;
    File f = LittleFS.open(kPath, "r");
    File1 d{};
    if (f && f.read(reinterpret_cast<uint8_t*>(&d), sizeof d) == sizeof d && d.magic == kMagic) s = d.s;
    f.close();
    if (s.brightness < kMinBrightness) s.brightness = kMinBrightness;
    return s;
}

void settings_save(const Settings& s)
{
    if (!storage_begin()) return;
    File1 d{};
    d.magic = kMagic;
    d.s = s;
    File f = LittleFS.open(kPath, "w");
    if (!f) return;
    f.write(reinterpret_cast<const uint8_t*>(&d), sizeof d);
    f.close();
}

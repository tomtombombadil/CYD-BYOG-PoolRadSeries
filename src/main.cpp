// CYD BYOG Pool of Radiance Series Engine - entry point.
//
// Milestone 1 (this build): the Gold Box Library / asset viewer. It finds
// the player's own game files on the SD card and shows what is in them,
// so the file-format code is proven on real files and boards before the
// game engine is built on top.
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "app/settings.h"
#include "boards/board_select.h"
#include "hal/panel_prefs.h"
#include "hal/storage.h"
#include "hal/touch_cal.h"
#include "ui/frame.h"
#include "ui/ui.h"
#include "ui/viewer.h"

#ifndef BYOG_VERSION
#define BYOG_VERSION "v?"      // tools/version.py sets it from the VERSION file
#endif
#ifndef BYOG_BUILD
#define BYOG_BUILD ""          // git commit
#endif

namespace {

LGFX     gfx;
Settings cfg;

// Landscape: the games are 320x200 screens. 1 = USB on the right for the
// boards Tom has; Rotate 180 in Settings turns it round.
constexpr uint8_t kRotation = 1;

// RGB LED off and audio amplifier off until something needs them (floating
// pins can light the LED or make the speaker hiss).
void quiet_peripherals()
{
#ifdef BOARD_PIN_LED_R
    const uint8_t off = BOARD_LED_ACTIVE_LOW ? HIGH : LOW;
    const uint8_t leds[3] = {BOARD_PIN_LED_R, BOARD_PIN_LED_G, BOARD_PIN_LED_B};
    for (uint8_t p : leds) { pinMode(p, OUTPUT); digitalWrite(p, off); }
#endif
#ifdef BOARD_PIN_AUDIO_EN
    pinMode(BOARD_PIN_AUDIO_EN, OUTPUT);
    digitalWrite(BOARD_PIN_AUDIO_EN, HIGH);   // high = amplifier off
#endif
}

void apply_rotation(bool flipped)
{
    gfx.setRotation(flipped ? (kRotation + 2) & 3 : kRotation);
}

void recalibrate()
{
    touch_cal_run(gfx);
}

} // namespace

// loop() keeps Arduino's 8 KB of stack: the deep PDF / JPEG work runs on a
// stack of its own while it runs (hal/bigstack.h; v0.19.1-v0.21.1 gave
// loop() 16 KB all the time)

void setup()
{
    Serial.begin(115200);
    delay(50);
    Serial.println("\nCYD BYOG Pool of Radiance Series " BYOG_VERSION " (" BYOG_BUILD ") - " BOARD_NAME);
    quiet_peripherals();

    if (!gfx.init()) {
        Serial.println("Display init failed - halting");
        while (true) delay(1000);
    }
    storage_begin();
    cfg = settings_load();
    apply_rotation(cfg.flipped);
    gfx.setBrightness(cfg.brightness);
    panel_prefs_begin(gfx);
    touch_cal_begin(gfx);      // BOOT held at power-up = calibrate again

    ui::begin(gfx);
    if (!frame::begin()) {
        gfx.fillScreen(TFT_BLACK);
        gfx.setTextColor(TFT_WHITE);
        gfx.drawString("Not enough memory for the game screen", 4, 4);
        while (true) delay(1000);
    }
    Serial.printf("[mem] free %u, largest block %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

    viewer::Env env{BYOG_VERSION, BYOG_BUILD, recalibrate, apply_rotation};
    viewer::begin(env, cfg);
}

void loop()
{
    viewer::tick();
    delay(2);
}

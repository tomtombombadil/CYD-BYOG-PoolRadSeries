// XPT2046 resistive touch read by bit-banging its four pins (software SPI).
//
// Why: on the 2.8" ESP32-2432S028 the touch chip and the SD slot are wired
// to different pins but both are meant for the ESP32's VSPI controller.
// LovyanGFX's XPT2046 driver needs a hardware SPI controller, so with it the
// SD card can't be used - and this engine needs the SD card for the player's
// game files. Reading touch in software leaves VSPI to the SD card.
//
// Plugs into LovyanGFX as an lgfx::ITouch, so getTouch(), calibrateTouch()
// and rotation keep working. Same sampling idea as LovyanGFX's driver: 7
// readings, median of each axis, reject light touches.
//
// Do not include directly - the board file includes it.
#pragma once

#define LGFX_USE_V1
#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <algorithm>

class Touch_XPT2046_Soft : public lgfx::ITouch
{
public:
    // Raw pressure (z1 + 4095 - z2) needed to count as a touch.
    static constexpr int kMinPressure = 400;

    Touch_XPT2046_Soft()
    {
        _cfg.x_min = 300;
        _cfg.x_max = 3900;
        _cfg.y_min = 200;
        _cfg.y_max = 3750;
        _cfg.bus_shared = false;
    }

    bool init() override
    {
        pinMode(_cfg.pin_cs, OUTPUT);
        digitalWrite(_cfg.pin_cs, HIGH);
        pinMode(_cfg.pin_sclk, OUTPUT);
        digitalWrite(_cfg.pin_sclk, LOW);
        pinMode(_cfg.pin_mosi, OUTPUT);
        digitalWrite(_cfg.pin_mosi, LOW);
        pinMode(_cfg.pin_miso, INPUT);
        if (_cfg.pin_int >= 0) pinMode(_cfg.pin_int, INPUT);
        _inited = true;
        return true;
    }

    void wakeup() override {}
    void sleep() override {}

    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override
    {
        if (!_inited || count == 0) return 0;
        tp->size = 0;
        // PENIRQ is low while the panel is pressed: skip the SPI work otherwise
        if (_cfg.pin_int >= 0 && digitalRead(_cfg.pin_int)) return 0;

        constexpr int kSamples = 7;
        uint16_t xs[kSamples], ys[kSamples];
        int nx = 0, ny = 0, pressed = 0;
        digitalWrite(_cfg.pin_cs, LOW);
        for (int i = 0; i < kSamples; ++i) {
            // Control byte: start, channel, 12-bit, differential, PD = 01
            // (ADC on between conversions)
            const int z1 = xfer(0xB1);
            const int z2 = xfer(0xC1);
            const int x  = xfer(0xD1);
            const int y  = xfer(0x91);
            if (z1 + 4095 - z2 >= kMinPressure) ++pressed;
            if (x > 128 && x <= 3968) xs[nx++] = x;
            if (y > 128 && y <= 3968) ys[ny++] = y;
        }
        xfer(0x90);   // PD = 00: power down between readings, PENIRQ enabled
        digitalWrite(_cfg.pin_cs, HIGH);

        if (pressed < 4 || nx < 3 || ny < 3) return 0;
        std::sort(xs, xs + nx);
        std::sort(ys, ys + ny);
        tp->x = xs[nx >> 1];
        tp->y = ys[ny >> 1];
        tp->size = 1;
        return 1;
    }

private:
    // Sends a control byte and reads the 12-bit result that follows.
    int xfer(uint8_t cmd)
    {
        for (int bit = 7; bit >= 0; --bit) {
            digitalWrite(_cfg.pin_mosi, (cmd >> bit) & 1);
            delayMicroseconds(1);
            digitalWrite(_cfg.pin_sclk, HIGH);
            delayMicroseconds(1);
            digitalWrite(_cfg.pin_sclk, LOW);
        }
        digitalWrite(_cfg.pin_mosi, LOW);
        uint16_t v = 0;
        for (int bit = 0; bit < 16; ++bit) {
            digitalWrite(_cfg.pin_sclk, HIGH);
            delayMicroseconds(1);
            v = static_cast<uint16_t>((v << 1) | (digitalRead(_cfg.pin_miso) & 1));
            digitalWrite(_cfg.pin_sclk, LOW);
            delayMicroseconds(1);
        }
        return (v >> 3) & 0x0FFF;
    }
};

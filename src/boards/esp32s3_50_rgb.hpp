// 5.0" 800x480 IPS capacitive display board with an ESP32-S3 (Tom ordered
// two, 2026-10-30: a Freenove FNK0115Q and a Hosyond 5" 800x480 ESP32-S3
// IPS, thought to be the same board): ESP32-S3 N16R8 - 16 MB flash, 8 MB
// octal PSRAM; a CH340C USB-serial chip (the serial log on UART0, not the
// S3's own USB - its pins 19 / 20 are the touch's I2C here).
//
// Pin reference: the maker's own sketches and schematic (facts only; its
// code is CC BY-NC-SA and isn't copied). The same layout as LovyanGFX's own
// Sunton ESP32-8048S050 setup (lgfx_user/, BSD) - pins agree:
//   Display: 16-bit RGB parallel ("RGB565" bus, no controller to set up):
//            DE 40, VSYNC 41, HSYNC 39, PCLK 42;
//            R0-R4 45 48 47 21 14, G0-G5 5 6 7 15 16 4, B0-B4 8 3 46 9 1;
//            13 MHz pixel clock, HSYNC / VSYNC polarity 0, front porch 4,
//            pulse 4, back porch 8; data out on the clock's falling edge
//   Backlight: GPIO 2 into the LED driver's EN/PWM pin (high = on)
//   Touch GT911: I2C SDA 19, SCL 20, RST 38, INT 18 - reports 800x480
//            (raw x, y = the panel's own pixels, landscape)
//   SD card: SPI on a bus of its own - CS 10, SCLK 12, MISO 13, MOSI 11
//   Sound: NS4168 I2S amplifier (no codec, no enable pin - its CTRL pin is
//          set by resistors) - BCLK 0, LRCLK 18, DATA 17; speaker socket
//   BOOT button 0 (also the I2S BCLK - read only at power-up)
//   INT 18 is ALSO the amplifier's LRCLK (both 0R links fitted): the touch
//   is polled without INT (as the 4.0" S3's FT6336), and its reset -
//   where INT picks the chip's I2C address - is done here before any sound.
//
// The screen: the engine's 320x240 layout drawn at 2x, centred on the
// 800x480 panel as 640x480 with black bars left and right (Tom, 2026-10-30).
// The rest of the code sees a 320x240 display (layout from the panel size,
// as on the 2.8" boards): Panel_Double below is a 320x240 frame buffer
// panel (LovyanGFX's Panel_FrameBufferBase does all the drawing, rotation
// and reading back into it, in PSRAM) whose changed rectangle is copied
// doubled into the RGB panel's 800x480 frame buffer when a draw ends
// (endWrite -> display()). Touch maps back: x = (raw - 80) / 2, y = raw / 2;
// touches in the bars are ignored.
//
// Do not include directly - include boards/board_select.h.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>

#define BOARD_TOUCH_RESISTIVE   0    // capacitive: factory-calibrated
#define BOARD_PIN_BOOT_BTN      0
#define BOARD_SD_USABLE         1    // SPI on a bus of its own (FSPI's own pins)
#define BOARD_SD_SPI_PORT       FSPI // (the ESP32-S3 has no VSPI)
#define BOARD_PIN_SD_CS         10
#define BOARD_PIN_SD_SCK        12
#define BOARD_PIN_SD_MISO       13
#define BOARD_PIN_SD_MOSI       11
#define BOARD_AUDIO_I2S         1    // a plain I2S amplifier (hal/audio.cpp)
#define BOARD_PIN_I2S_BCLK      0
#define BOARD_PIN_I2S_LRCK      18
#define BOARD_PIN_I2S_DOUT      17
#define BOARD_I2C_PORT          0    // the touch's
#define BOARD_PIN_I2C_SDA       19
#define BOARD_PIN_I2C_SCL       20

namespace board5 {

constexpr int kPanelW = 800, kPanelH = 480;   // the RGB panel
constexpr int kW = 320, kH = 240;             // what the engine sees
constexpr int kBarX = (kPanelW - kW * 2) / 2; // 80: the black bar's width

// ---- The engine's 320x240 screen, shown doubled on the RGB panel ----------
class Panel_Double : public lgfx::Panel_FrameBufferBase
{
public:
    lgfx::Panel_RGB phys;          // the real 800x480 panel (its own frame buffer)

    Panel_Double()
    {
        _write_depth = lgfx::color_depth_t::rgb565_2Byte;
        _read_depth  = lgfx::color_depth_t::rgb565_2Byte;
    }

    lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override { return _write_depth; }

    bool init(bool use_reset) override
    {
        if (!_lines_buffer) {
            // The 320x240 picture, a line a pointer (Panel_FrameBufferBase's
            // way): 153.6 KB in PSRAM
            uint8_t* px = static_cast<uint8_t*>(heap_caps_calloc(kW * kH, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            uint8_t** lines = static_cast<uint8_t**>(heap_caps_malloc(kH * sizeof(uint8_t*), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            if (!px || !lines) {
                heap_caps_free(px);
                heap_caps_free(lines);
                return false;
            }
            for (int y = 0; y < kH; ++y) lines[y] = px + y * kW * 2;
            _lines_buffer = lines;
        }
        if (!fb_) {
#if defined(CYD_QEMU)
            // (the emulator build: QEMU's S3 has no LCD_CAM - the panel's
            // frame buffer is a plain PSRAM block, checked by qemu_check)
            fb_ = static_cast<uint8_t*>(heap_caps_malloc(kPanelW * kPanelH * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
#else
            if (!phys.init(false)) return false;        // (starts the RGB bus)
            fb_ = phys.getBus()->getDMABuffer(0);
#endif
            if (!fb_) return false;
            memset(fb_, 0, kPanelW * kPanelH * 2);       // black, bars included
            esp_cache_msync(fb_, kPanelW * kPanelH * 2, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
        }
        if (!Panel_FrameBufferBase::init(use_reset)) return false;
        _auto_display = true;      // every draw's endWrite shows what it changed
        return true;
    }

    // Swap Red/Blue (Settings) changes cfg.rgb_order and sets the rotation
    // again; Invert Colors calls setInvert. Done here, as the pixels are
    // copied (an RGB panel has no controller to tell)
    void setRotation(uint_fast8_t r) override
    {
        Panel_FrameBufferBase::setRotation(r);
        restyle();
    }
    void setInvert(bool invert) override
    {
        _invert = invert;
        restyle();
    }

    void display(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w, uint_fast16_t h) override
    {
        if (w && h) {      // (a rectangle in the screen's rotation -> the buffer's)
            const uint_fast8_t r = _internal_rotation;
            if (r) {
                if ((1u << r) & 0b10010110) y = _height - (y + h);
                if (r & 2) x = _width - (x + w);
                if (r & 1) { std::swap(x, y); std::swap(w, h); }
            }
            grow(x, y, x + w - 1, y + h - 1);
        }
        if (_range_mod.empty()) return;
        if (fb_ && _lines_buffer) copy_doubled(_range_mod.left, _range_mod.top, _range_mod.right, _range_mod.bottom);
        _range_mod.top = INT16_MAX;
        _range_mod.left = INT16_MAX;
        _range_mod.right = 0;
        _range_mod.bottom = 0;
    }

    // The slowest copy so far and the whole screen's, in microseconds
    // (printed on the serial line the first time the whole screen goes)
    uint32_t worst_us = 0, full_us = 0;

private:
    uint8_t* fb_ = nullptr;
    bool     shown_invert_ = false, shown_swap_ = false;

    void grow(int l, int t, int r, int b)
    {
        _range_mod.left   = std::min<int_fast16_t>(_range_mod.left, l);
        _range_mod.right  = std::max<int_fast16_t>(_range_mod.right, r);
        _range_mod.top    = std::min<int_fast16_t>(_range_mod.top, t);
        _range_mod.bottom = std::max<int_fast16_t>(_range_mod.bottom, b);
    }

    // The colour settings changed: the whole screen again with them
    void restyle()
    {
        const bool inv = _invert ^ _cfg.invert, sw = _cfg.rgb_order;
        if (inv == shown_invert_ && sw == shown_swap_) return;
        shown_invert_ = inv;
        shown_swap_ = sw;
        if (!fb_ || !_lines_buffer) return;
        grow(0, 0, kW - 1, kH - 1);
        display(0, 0, 0, 0);
    }

    // Buffer rectangle l..r, t..b (inclusive) -> the panel, each pixel 2 x 2.
    // The pixels are LovyanGFX's byte-swapped RGB565 in both buffers: a
    // pixel doubled is one 32-bit word, written to both rows
    void copy_doubled(int l, int t, int r, int b)
    {
        if (l < 0) l = 0;
        if (t < 0) t = 0;
        if (r > kW - 1) r = kW - 1;
        if (b > kH - 1) b = kH - 1;
        if (l > r || t > b) return;
        const int64_t t0 = esp_timer_get_time();
        constexpr int kStride = kPanelW * 2;               // bytes a panel row
        const int n = r - l + 1;
        const uint32_t inv = shown_invert_ ? 0xFFFFFFFFu : 0;
        for (int y = t; y <= b; ++y) {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(_lines_buffer[y]) + l;
            uint32_t* d0 = reinterpret_cast<uint32_t*>(fb_ + (2 * y) * kStride + (kBarX + 2 * l) * 2);
            uint32_t* d1 = reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(d0) + kStride);
            if (!shown_swap_) {
                for (int i = 0; i < n; ++i) {
                    const uint32_t v = (s[i] * 0x00010001u) ^ inv;
                    d0[i] = v;
                    d1[i] = v;
                }
            } else {
                for (int i = 0; i < n; ++i) {
                    const uint32_t p = static_cast<uint16_t>((s[i] >> 8) | (s[i] << 8));    // RGB565
                    const uint32_t q = ((p & 0x1F) << 11) | (p & 0x07E0) | (p >> 11);      // red <-> blue
                    const uint32_t v = (((q >> 8) | (q << 8)) & 0xFFFF) * 0x00010001u ^ inv;
                    d0[i] = v;
                    d1[i] = v;
                }
            }
        }
        // The RGB peripheral reads the PSRAM itself: the changed lines out
        // of the cache
        uint8_t* first = fb_ + (2 * t) * kStride + (kBarX + 2 * l) * 2;
        uint8_t* end = fb_ + (2 * b + 1) * kStride + (kBarX + 2 * r + 2) * 2;
        esp_cache_msync(first, end - first,
                        ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        const uint32_t us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        if (us > worst_us) worst_us = us;
        if (n == kW && t == 0 && b == kH - 1) {
            if (!full_us) printf("[lcd] whole screen doubled to the 800x480 panel: %u us\n", static_cast<unsigned>(us));
            full_us = us;
        }
#if defined(CYD_QEMU)
        qemu_check(l, t, r, b);
#endif
    }

#if defined(CYD_QEMU)
    // The emulator build: every copy checked against the 320x240 buffer
    // (each pixel 2 x 2 where it belongs, the bars black); the totals
    // printed now and then
    uint32_t q_copies_ = 0, q_bad_ = 0;
    void qemu_check(int l, int t, int r, int b)
    {
        constexpr int kStride = kPanelW;              // (in pixels)
        const uint16_t* fb = reinterpret_cast<const uint16_t*>(fb_);
        for (int y = t; y <= b; ++y) {
            const uint16_t* s = reinterpret_cast<const uint16_t*>(_lines_buffer[y]);
            for (int x = l; x <= r; ++x) {
                uint16_t want = s[x];
                if (shown_swap_) {
                    const uint16_t p = static_cast<uint16_t>((want >> 8) | (want << 8));
                    const uint16_t q = static_cast<uint16_t>(((p & 0x1F) << 11) | (p & 0x07E0) | (p >> 11));
                    want = static_cast<uint16_t>((q >> 8) | (q << 8));
                }
                if (shown_invert_) want = static_cast<uint16_t>(~want);
                const int px = kBarX + 2 * x, py = 2 * y;
                const uint16_t* d = fb + py * kStride + px;
                if (d[0] != want || d[1] != want || d[kStride] != want || d[kStride + 1] != want) ++q_bad_;
            }
            for (int x = 0; x < kBarX; ++x)
                if (fb[2 * y * kStride + x] || fb[2 * y * kStride + kPanelW - 1 - x]) ++q_bad_;
        }
        if (++q_copies_ % 50 == 0 || (r - l == kW - 1 && b - t == kH - 1))
            printf("[qemu] 2x screen: %u copies, %u bad pixels (rotation %u, last %d,%d-%d,%d)\n",
                   static_cast<unsigned>(q_copies_), static_cast<unsigned>(q_bad_),
                   static_cast<unsigned>(_rotation), l, t, r, b);
    }
#endif
};

// ---- GT911 touch: reset with the address picked, bars dropped -------------
class Touch_GT911_Bars : public lgfx::Touch_GT911
{
public:
    static constexpr int kPinRst = 38, kPinInt = 18;

    bool init() override
    {
        if (_inited) return true;
        // The GT911's reset: INT's level as RST rises picks its I2C address
        // (low = 0x5D). INT is the amplifier's LRCLK too - the sound isn't
        // running yet at this point (gfx.init in setup). INT is let go
        // (input) afterwards; it is never read.
        lgfx::pinMode(kPinInt, lgfx::pin_mode_t::output);
        lgfx::gpio_lo(kPinInt);
        lgfx::pinMode(kPinRst, lgfx::pin_mode_t::output);
        lgfx::gpio_lo(kPinRst);
        lgfx::delay(11);
        lgfx::gpio_hi(kPinRst);
        lgfx::delay(6);                       // INT held >= 5 ms after RST
        lgfx::delay(50);                      // (INT low 50 ms: the chip's own start-up sync)
        lgfx::pinMode(kPinInt, lgfx::pin_mode_t::input);
        lgfx::delay(50);
        if (!Touch_GT911::init()) return false;   // (it tries 0x5D and 0x14 in turn)
        // The chip's own output size (its config, 0x8048..0x804B) - the maker
        // sets 800 x 480; scaled to that if it says otherwise
        const uint8_t reg[2] = {0x80, 0x48};
        uint8_t res[4] = {0, 0, 0, 0};
        if (lgfx::i2c::transactionWriteRead(_cfg.i2c_port, _cfg.i2c_addr, reg, 2, res, 4, _cfg.freq).has_value()) {
            const int rx = res[0] | (res[1] << 8), ry = res[2] | (res[3] << 8);
            if (rx >= 100 && rx <= 4096 && ry >= 100 && ry <= 4096) {
                res_x_ = rx;
                res_y_ = ry;
            }
        }
        printf("[touch] GT911 at 0x%02X, %d x %d\n", static_cast<unsigned>(_cfg.i2c_addr), res_x_, res_y_);
        return true;
    }

    uint_fast8_t getTouchRaw(lgfx::touch_point_t* tp, uint_fast8_t count) override
    {
        const uint_fast8_t got = Touch_GT911::getTouchRaw(tp, count);
        uint_fast8_t n = 0;
        for (uint_fast8_t i = 0; i < got; ++i) {
            lgfx::touch_point_t p = tp[i];
            if (res_x_ != kPanelW) p.x = static_cast<int16_t>(p.x * kPanelW / res_x_);
            if (res_y_ != kPanelH) p.y = static_cast<int16_t>(p.y * kPanelH / res_y_);
            if (p.x < kBarX || p.x >= kPanelW - kBarX) continue;      // (in a black bar)
            tp[n++] = p;
        }
        return n;
    }

private:
    int res_x_ = kPanelW, res_y_ = kPanelH;
};

} // namespace board5

class LGFX_S3_50_Rgb : public lgfx::LGFX_Device
{
    board5::Panel_Double     _panel;
    lgfx::Bus_RGB            _bus;
    lgfx::Light_PWM          _light;
    board5::Touch_GT911_Bars _touch;

public:
    LGFX_S3_50_Rgb()
    {
        {   // The real panel: 800x480 landscape, its frame buffer in PSRAM
            auto cfg = _panel.phys.config();
            cfg.memory_width  = board5::kPanelW;
            cfg.memory_height = board5::kPanelH;
            cfg.panel_width   = board5::kPanelW;
            cfg.panel_height  = board5::kPanelH;
            cfg.offset_x      = 0;
            cfg.offset_y      = 0;
            _panel.phys.config(cfg);
        }
        {
            auto cfg = _bus.config();
            cfg.panel   = &_panel.phys;
            cfg.pin_d0  = 8;     // B0
            cfg.pin_d1  = 3;     // B1
            cfg.pin_d2  = 46;    // B2
            cfg.pin_d3  = 9;     // B3
            cfg.pin_d4  = 1;     // B4
            cfg.pin_d5  = 5;     // G0
            cfg.pin_d6  = 6;     // G1
            cfg.pin_d7  = 7;     // G2
            cfg.pin_d8  = 15;    // G3
            cfg.pin_d9  = 16;    // G4
            cfg.pin_d10 = 4;     // G5
            cfg.pin_d11 = 45;    // R0
            cfg.pin_d12 = 48;    // R1
            cfg.pin_d13 = 47;    // R2
            cfg.pin_d14 = 21;    // R3
            cfg.pin_d15 = 14;    // R4
            cfg.pin_henable = 40;
            cfg.pin_vsync   = 41;
            cfg.pin_hsync   = 39;
            cfg.pin_pclk    = 42;
            cfg.freq_write  = 13000000;          // the maker's (~32 frames a second)
            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 4;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch  = 8;
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 4;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch  = 8;
            cfg.pclk_active_neg   = 1;
            cfg.pclk_idle_high    = 1;           // (LovyanGFX: the clock's out edge - the maker's "active negative")
            cfg.de_idle_high      = 0;
            _bus.config(cfg);
            _panel.phys.setBus(&_bus);
        }
        {   // What the engine sees: 320x240 landscape. The engine's rotation 1
            // (its landscape) is this buffer as it is; 3 (Rotate 180) turned
            auto cfg = _panel.config();
            cfg.memory_width    = board5::kW;
            cfg.memory_height   = board5::kH;
            cfg.panel_width     = board5::kW;
            cfg.panel_height    = board5::kH;
            cfg.offset_x        = 0;
            cfg.offset_y        = 0;
            cfg.offset_rotation = 3;
            cfg.readable        = true;
            cfg.invert          = false;
            cfg.rgb_order       = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 2;
            cfg.invert      = false;
            cfg.freq        = 44100;             // (LovyanGFX's for this layout)
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {   // Raw 80..719 x 0..479 (the 640x480 picture) -> 0..319 x 0..239
            auto cfg = _touch.config();
            cfg.x_min           = board5::kBarX;
            cfg.x_max           = board5::kPanelW - board5::kBarX - 1;
            cfg.y_min           = 0;
            cfg.y_max           = board5::kPanelH - 1;
            cfg.pin_int         = -1;    // (INT is the amplifier's LRCLK: polled instead)
            cfg.pin_rst         = -1;    // (the reset is Touch_GT911_Bars::init's)
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port        = BOARD_I2C_PORT;
            cfg.i2c_addr        = 0x5D;  // (INT low at reset)
            cfg.pin_sda         = BOARD_PIN_I2C_SDA;
            cfg.pin_scl         = BOARD_PIN_I2C_SCL;
            cfg.freq            = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

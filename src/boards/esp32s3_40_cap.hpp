// 4.0" ST7796 480x320 capacitive display board with an ESP32-S3 (Tom's,
// 2026-10-12): ESP32-S3R8 - dual core 240 MHz, 512 KB SRAM, 8 MB octal
// PSRAM in the chip; 16 MB flash. Sold as part of a family with 2.8" /
// 3.5" / 4.0" models (the 4.0" ST7796 one is this board; the others have
// other pins and panels).
//
// Pin reference: the maker's own tutorial setups and schematic (facts only;
// its code is CC BY-NC-SA and isn't copied):
//   Display ST7796 on SPI2 (FSPI's own pins): MOSI 11, SCLK 12, CS 10,
//                    DC 46, RST = EN, no MISO; inversion on; 80 MHz
//   Backlight      : GPIO 45 (high = on)
//   Touch FT6336   : I2C SDA 16, SCL 15, RST 18, INT 17 (address 0x38)
//   SD card SD/MMC 4-bit: CLK 38, CMD 40, D0 39, D1 41, D2 48, D3 47
//   Sound: ES8311 codec on I2S (MCLK 4, BCLK 5, LRCK 7, DOUT 8, DIN 6),
//          set up over the touch's I2C (address 0x18); FM8002E amplifier,
//          enable GPIO 1 (low = on); speaker socket
//   WS2812 RGB LED 42, BOOT button 0
//
// Do not include directly - include boards/board_select.h.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define BOARD_TOUCH_RESISTIVE   0    // capacitive: factory-calibrated
#define BOARD_PIN_BOOT_BTN      0
#define BOARD_SD_USABLE         1
#define BOARD_SD_MMC            1    // 4-bit SD/MMC, a bus of its own
#define BOARD_PIN_SD_CLK        38
#define BOARD_PIN_SD_CMD        40
#define BOARD_PIN_SD_D0         39
#define BOARD_PIN_SD_D1         41
#define BOARD_PIN_SD_D2         48
#define BOARD_PIN_SD_D3         47
#define BOARD_AUDIO_ES8311      1    // sound through the codec (hal/es8311.*)
#define BOARD_PIN_I2S_MCLK      4
#define BOARD_PIN_I2S_BCLK      5
#define BOARD_PIN_I2S_LRCK      7
#define BOARD_PIN_I2S_DOUT      8
#define BOARD_I2C_PORT          0    // shared by the touch and the codec
#define BOARD_PIN_I2C_SDA       16
#define BOARD_PIN_I2C_SCL       15
#define BOARD_PIN_AUDIO_EN      1    // low = amplifier on
#define BOARD_PIN_TOUCH_RST     18   // reset at power-up by main.cpp, as the maker does

class LGFX_S3_40_Cap : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7796  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_FT5x06  _touch;

public:
    LGFX_S3_40_Cap()
    {
        {   // SPI2 on its own (IO MUX) pins: the display only
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = 80000000;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 12;
            cfg.pin_mosi    = 11;
            cfg.pin_miso    = -1;
            cfg.pin_dc      = 46;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // Panel: native portrait 320x480
            auto cfg = _panel.config();
            cfg.pin_cs           = 10;
            cfg.pin_rst          = -1;   // tied to EN
            cfg.pin_busy         = -1;
            cfg.panel_width      = 320;
            cfg.panel_height     = 480;
            cfg.memory_width     = 320;
            cfg.memory_height    = 480;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = false;  // no MISO
            cfg.invert           = true;
            cfg.rgb_order        = false;  // false = BGR in LovyanGFX
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = false;
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 45;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 7;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {   // Capacitive touch: reports the panel's own portrait pixels.
            // v0.103.4 (Tom: touches had to be heavy and missed): polled
            // over I2C every time, as the maker's library does - with its
            // INT pin given, LovyanGFX reads only while INT happens to be
            // low and reports no touch otherwise; and the reset is
            // main.cpp's (10 ms low, then time to start up - LovyanGFX
            // holds it low 1 ms and talks to the chip at once)
            auto cfg = _touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = 319;
            cfg.y_min           = 0;
            cfg.y_max           = 479;
            cfg.pin_int         = -1;    // (INT is GPIO 17: not used)
            cfg.pin_rst         = -1;    // (RST is GPIO 18: main.cpp)
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port        = BOARD_I2C_PORT;
            cfg.i2c_addr        = 0x38;
            cfg.pin_sda         = BOARD_PIN_I2C_SDA;
            cfg.pin_scl         = BOARD_PIN_I2C_SCL;
            cfg.freq            = 400000;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

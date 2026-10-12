// 2.8" ST7789 320x240 resistive display board with an ESP32-C5 (Tom has
// two, 2026-10-12): ESP32-C5-WROOM-1-N16R8 - one RISC-V core at 240 MHz,
// 384 KB SRAM, 8 MB PSRAM, 16 MB flash. The classic 2.8" board's layout
// with the newer chip.
//
// Pin reference: the maker's README and its PlatformIO / TFT_eSPI setups:
//   One SPI bus for everything (SPI2, the C5's only one): SCK 6, MISO 2,
//   MOSI 7 - display CS 23, DC 24, RST = EN; touch XPT2046 CS 1 (no IRQ);
//   SD card CS 10
//   Backlight GPIO 25 (high = on)
//   Sound: SC8002 amplifier, its input from GPIO 26 through a filter -
//          no DAC on the C5: I2S PDM output on GPIO 26 does its work
//   WS2812 RGB LED 27, BOOT button 28
//
// Do not include directly - include boards/board_select.h.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#define BOARD_TOUCH_RESISTIVE   1
#define BOARD_PIN_BOOT_BTN      28
#define BOARD_SD_USABLE         1
#define BOARD_SD_SHARED_SPI     1    // on the display's bus (Arduino's SPI)
#define BOARD_PIN_SD_CS         10
#define BOARD_PIN_SD_SCK        6
#define BOARD_PIN_SD_MISO       2
#define BOARD_PIN_SD_MOSI       7
#define BOARD_AUDIO_PDM         1    // sound as PDM into the amplifier's filter
#define BOARD_PIN_SPEAKER       26

class LGFX_C5_28_Res : public lgfx::LGFX_Device
{
    lgfx::Panel_ST7789  _panel;
    lgfx::Bus_SPI       _bus;
    lgfx::Light_PWM     _light;
    lgfx::Touch_XPT2046 _touch;

public:
    LGFX_C5_28_Res()
    {
        {   // SPI2, shared by the display, the touch and the SD card
            auto cfg = _bus.config();
            cfg.spi_host    = SPI2_HOST;
            cfg.spi_mode    = 0;
            cfg.freq_write  = CYD_SPI_WRITE_HZ;
            cfg.freq_read   = 16000000;
            cfg.spi_3wire   = false;
            cfg.use_lock    = true;
            cfg.dma_channel = SPI_DMA_CH_AUTO;
            cfg.pin_sclk    = 6;
            cfg.pin_mosi    = 7;
            cfg.pin_miso    = 2;
            cfg.pin_dc      = 24;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {   // Panel: native portrait 240x320
            auto cfg = _panel.config();
            cfg.pin_cs           = 23;
            cfg.pin_rst          = -1;   // tied to EN
            cfg.pin_busy         = -1;
            cfg.panel_width      = 240;
            cfg.panel_height     = 320;
            cfg.memory_width     = 240;
            cfg.memory_height    = 320;
            cfg.offset_x         = 0;
            cfg.offset_y         = 0;
            cfg.offset_rotation  = 0;
            cfg.readable         = true;
            cfg.invert           = false;  // the maker's setup: inversion off
            cfg.rgb_order        = false;  // false = BGR in LovyanGFX
            cfg.dlen_16bit       = false;
            cfg.bus_shared       = true;   // touch and SD are on this bus too
            _panel.config(cfg);
        }
        {
            auto cfg = _light.config();
            cfg.pin_bl      = 25;
            cfg.invert      = false;
            cfg.freq        = 12000;
            cfg.pwm_channel = 0;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {   // Touch on the display's bus (no IRQ line: polled). Raw limits
            // are a starting point; the saved calibration replaces them.
            auto cfg = _touch.config();
            cfg.x_min           = 185;
            cfg.x_max           = 3700;
            cfg.y_min           = 250;
            cfg.y_max           = 3800;
            cfg.pin_int         = -1;
            cfg.bus_shared      = true;
            cfg.offset_rotation = 0;
            cfg.spi_host        = SPI2_HOST;
            cfg.freq            = 1000000;
            cfg.pin_sclk        = 6;
            cfg.pin_mosi        = 7;
            cfg.pin_miso        = 2;
            cfg.pin_cs          = 1;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

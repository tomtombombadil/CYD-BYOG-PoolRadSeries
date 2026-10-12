#include "sdcard.h"

#include <Arduino.h>
#include "boards/board_select.h"

#if defined(CYD_QEMU)

// The emulator build: SD/MMC (hal/qemu.h)
#include "hal/qemu.h"

bool sd_begin() { return qemu::sd_begin(); }
fs::FS& sd_fs() { return qemu::sd_fs(); }
void sd_lost() { qemu::sd_end(); }

#elif BOARD_SD_USABLE && defined(BOARD_SD_MMC)

// SD/MMC, 4 data lines on a bus of their own (the ESP32-S3 board)
#include <SD_MMC.h>

namespace {
bool mounted = false;
bool pins_set = false;
uint32_t failed_at = 0;     // (a try without a card takes ~2 s: not again for 5 s)
}

bool sd_begin()
{
    if (mounted) return true;
    if (failed_at && millis() - failed_at < 5000) return false;
    if (!pins_set) {
        pins_set = SD_MMC.setPins(BOARD_PIN_SD_CLK, BOARD_PIN_SD_CMD, BOARD_PIN_SD_D0, BOARD_PIN_SD_D1,
                                  BOARD_PIN_SD_D2, BOARD_PIN_SD_D3);
        if (!pins_set) Serial.println("[sd] the SD/MMC pins weren't taken");
    }
    // 3 files open at most, as on the SPI boards (each ~4 KB of RAM)
    mounted = SD_MMC.begin("/sd", false, false, SDMMC_FREQ_DEFAULT, 3);
    if (mounted) {
        Serial.printf("[sd] card mounted (SD/MMC 4-bit), %llu MB\n", (unsigned long long)(SD_MMC.cardSize() >> 20));
    }
    failed_at = mounted ? 0 : (millis() | 1);
    return mounted;
}

fs::FS& sd_fs() { return SD_MMC; }

void sd_lost()
{
    if (!mounted) return;
    SD_MMC.end();
    mounted = false;
    Serial.println("[sd] card unavailable");
}

#elif BOARD_SD_USABLE

#include <SD.h>
#include <SPI.h>

namespace {
#if defined(BOARD_SD_SHARED_SPI)
// On the display's bus: Arduino's SPI, which LovyanGFX started with these
// pins (the bus lock keeps the display, touch and card apart)
SPIClass& sd_spi = SPI;
#else
SPIClass sd_spi(VSPI);
#endif
bool     mounted = false;
bool     spi_started = false;
}

bool sd_begin()
{
    if (mounted) return true;
    if (!spi_started) {
        sd_spi.begin(BOARD_PIN_SD_SCK, BOARD_PIN_SD_MISO, BOARD_PIN_SD_MOSI, BOARD_PIN_SD_CS);
        spi_started = true;
    }
    // 3 files open at most (the default 5; 4 until v0.85.7): each slot
    // costs ~4 KB of RAM (a 4096-byte sector buffer) from the start; the
    // engine never has more than 3 open (scan log + a source + an output;
    // event picture + book + a DAX file) - the playsim's card has 3 too
    mounted = SD.begin(BOARD_PIN_SD_CS, sd_spi, 20000000, "/sd", 3);
    if (mounted) {
        Serial.printf("[sd] card mounted, %llu MB\n", (unsigned long long)(SD.cardSize() >> 20));
    }
    return mounted;
}

fs::FS& sd_fs() { return SD; }

void sd_lost()
{
    if (!mounted) return;
    SD.end();
    mounted = false;
    Serial.println("[sd] card unavailable");
}

#else  // no usable SD slot on this board

#include <LittleFS.h>

bool sd_begin() { return false; }
fs::FS& sd_fs() { return LittleFS; }   // never used: sd_begin() is false
void sd_lost() {}

#endif

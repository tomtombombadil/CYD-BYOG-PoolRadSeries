#include "es8311.h"

#include <Arduino.h>

#include "boards/board_select.h"   // (LovyanGFX: its I2C, shared with the touch)

#if defined(BOARD_AUDIO_ES8311)

namespace es8311 {
namespace {

constexpr int kAddr = 0x18;        // CE pin low
int port = 0;

bool put(uint8_t reg, uint8_t v)
{
    return lgfx::i2c::writeRegister8(port, kAddr, reg, v, 0, 400000).has_value();
}

} // namespace

bool begin(int i2c_port, int pin_sda, int pin_scl, uint32_t rate)
{
    (void)rate;    // (MCLK = 256 x rate: the dividers below are the same at every rate)
    port = i2c_port;
    lgfx::i2c::init(port, pin_sda, pin_scl);
    auto id1 = lgfx::i2c::readRegister8(port, kAddr, 0xFD, 400000);
    auto id2 = lgfx::i2c::readRegister8(port, kAddr, 0xFE, 400000);
    if (!id1.has_value() || !id2.has_value()) {
        Serial.println("[audio] the ES8311 codec didn't answer");
        return false;
    }
    Serial.printf("[audio] ES8311 codec, chip id %02X %02X\n", id1.value(), id2.value());

    bool ok = put(0x00, 0x1F);     // reset
    delay(20);
    ok = ok && put(0x00, 0x00) && put(0x00, 0x80);    // power on, slave mode
    // Clocks: all on, MCLK from its pin; MCLK / LRCK = 256, single speed:
    // pre-divider 1, multiplier 1, ADC / DAC dividers 1, over-sampling 16,
    // BCLK = MCLK / 4, LRCK = MCLK / 256
    ok = ok && put(0x01, 0x3F) && put(0x02, 0x00) && put(0x03, 0x10) && put(0x04, 0x10) && put(0x05, 0x00) &&
         put(0x06, 0x03) && put(0x07, 0x00) && put(0x08, 0xFF);
    // Serial port: I2S, 16-bit in and out
    ok = ok && put(0x09, 0x0C) && put(0x0A, 0x0C);
    // Analog power up; DAC on; output to the line / headphone driver; the
    // ADC's DC cancel; the DAC's equaliser bypassed
    ok = ok && put(0x0D, 0x01) && put(0x0E, 0x02) && put(0x12, 0x00) && put(0x13, 0x10) && put(0x1C, 0x6A) &&
         put(0x37, 0x08);
    ok = ok && put(0x32, 0xBF);    // DAC volume 0 dB
    if (!ok) Serial.println("[audio] the ES8311 codec's set-up didn't finish");
    return ok;
}

void volume(uint8_t reg) { put(0x32, reg); }

} // namespace es8311

#endif // BOARD_AUDIO_ES8311

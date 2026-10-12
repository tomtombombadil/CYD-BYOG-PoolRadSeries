// The ES8311 audio codec (the ESP32-S3 board's sound): set up over I2C for
// 16-bit I2S samples in, its DAC out to the amplifier. Our own code; the
// register settings follow the chip's datasheet and Espressif's es8311
// driver (Apache-2.0) as facts - see THIRD_PARTY_NOTICES.md.
#pragma once

#include <cstdint>

namespace es8311 {

// The codec as an I2S slave, MCLK = 256 x rate from the MCLK pin, 16-bit
// samples. The I2S clocks should run before this (the codec's clocks come
// from them). False when the chip didn't answer.
bool begin(int i2c_port, int pin_sda, int pin_scl, uint32_t rate);

// 0 dB = 0xBF (each step 0.5 dB)
void volume(uint8_t reg);

} // namespace es8311

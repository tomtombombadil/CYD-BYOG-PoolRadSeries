// The game's sound out of the board's speaker socket: 8-bit samples to the
// DAC (GPIO 26) through the board's little amplifier, fed by DMA from a task
// of its own that runs only while something sounds (Tom: every KB for the
// games - ~7 KB while it plays, nothing otherwise) - except during a fight,
// when audio_hold keeps it (asleep, the DAC off) between sounds so Hit and
// Miss always play (Tom, 2026-10-12: "Prioritize the hit and miss sounds").
// Boards with an amplifier enable pin (the ESP32-32E ones: GPIO 4, low =
// on) switch it on only while something sounds. The ESP32-S3 board (no
// DAC) sends the samples as I2S to its ES8311 codec; the ESP32-C5 board
// (no DAC) as I2S PDM on GPIO 26 into its amplifier's filter (v0.88.0).
#pragma once

#include <cstdint>

constexpr int kAudioHz = 22050;

// Fills buf with n samples (silence = 128); false when there's nothing more
using AudioFill = bool (*)(uint8_t* buf, int n, void* ctx);

// Starts the output with this filler if it isn't running (else nothing: the
// running filler goes on - it picks up new sounds itself)
void audio_play(AudioFill fill, void* ctx);
// Stops it and waits until the task has ended (call before the filler's
// data goes away)
void audio_stop();
bool audio_running();
// on: the output's task and DAC made now and kept between sounds until the
// hold ends (a fight) - false from audio_held() when there was no memory
void audio_hold(bool on, AudioFill fill, void* ctx);
bool audio_held();

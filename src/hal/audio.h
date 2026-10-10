// The game's sound out of the board's speaker socket: 8-bit samples to the
// DAC (GPIO 26) through the board's little amplifier, fed by DMA from a task
// of its own that runs only while something sounds (Tom: every KB for the
// games - ~5 KB while it plays, nothing otherwise). Boards with an amplifier
// enable pin (the ESP32-32E ones: GPIO 4, low = on) switch it on only then.
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

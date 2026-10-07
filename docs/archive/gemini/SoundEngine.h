#ifndef SOUND_ENGINE_H
#define SOUND_ENGINE_H

#include <Arduino.h>

enum SoundEffect {
  SND_STEP,
  SND_BUMP,
  SND_CLICK,
  SND_ATTACK,
  SND_HIT,
  SND_MISS,
  SND_SPELL,
  SND_VICTORY,
  SND_REST
};

class SoundEngine {
private:
  int pin;
  int pwmChannel;

public:
  SoundEngine(int audioPin = 26, int channel = 0) : pin(audioPin), pwmChannel(channel) {}

  void init() {
    ledcSetup(pwmChannel, 2000, 8); // 2 kHz, 8-bit resolution
    ledcAttachPin(pin, pwmChannel);
  }

  void play(SoundEffect fx) {
    switch(fx) {
      case SND_STEP: tone(150, 20); break;
      case SND_BUMP: tone(80, 50); break;
      case SND_CLICK: tone(1000, 10); break;
      case SND_ATTACK: tone(400, 30); break;
      case SND_HIT: tone(200, 60); break;
      case SND_MISS: tone(800, 40); break;
      case SND_SPELL: tone(1200, 100); break;
      case SND_VICTORY: tone(600, 200); delay(200); tone(800, 300); break;
      case SND_REST: tone(300, 150); delay(150); tone(400, 200); break;
    }
  }

private:
  void tone(uint32_t freq, uint32_t durationMs) {
    ledcWriteTone(pwmChannel, freq);
    delay(durationMs);
    ledcWriteTone(pwmChannel, 0); // Stop sound
  }
};

#endif // SOUND_ENGINE_H
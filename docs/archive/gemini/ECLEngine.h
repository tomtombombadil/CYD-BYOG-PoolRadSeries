#ifndef ECL_ENGINE_H
#define ECL_ENGINE_H

#include <Arduino.h>
#include <LovyanGFX.hpp>

class ECLEngine {
private:
  const uint8_t* currentScript;
  int pc; // Program Counter
  bool active;
  char displayBuffer[128];

public:
  bool startCombatFlag; // Flag to trigger combat state in main

  ECLEngine() : active(false), startCombatFlag(false) {}

  void executeScript(const uint8_t* scriptData) {
    currentScript = scriptData;
    pc = 0;
    active = true;
    startCombatFlag = false;
    memset(displayBuffer, 0, sizeof(displayBuffer));
    step();
  }

  bool isActive() const { return active; }

  void step() {
    if (!active) return;
    uint8_t opcode = currentScript[pc++];
    
    switch (opcode) {
      case 0x00: // END
        active = false;
        break;
      case 0x01: // PRINT
        {
          int i = 0;
          while (currentScript[pc] != 0x00 && i < 127) {
            displayBuffer[i++] = currentScript[pc++];
          }
          displayBuffer[i] = '\0';
          pc++; // Skip string terminator
        }
        break;
      case 0x02: // START ENCOUNTER
        active = false;
        startCombatFlag = true;
        break;
    }
  }

  void handleTouch(int tx, int ty) {
    if (active) step();
  }

  void render(LGFX_Sprite* spr) {
    spr->fillRect(0, 0, 220, 200, TFT_BLACK);
    spr->drawRect(5, 5, 210, 190, TFT_BLUE);
    spr->setTextColor(TFT_WHITE, TFT_BLACK);
    spr->setTextDatum(TL_DATUM);
    spr->drawString(displayBuffer, 15, 20);
    
    // Blinking cursor
    if ((millis() / 500) % 2 == 0) {
      spr->drawString(">> Tap to continue >>", 40, 170);
    }
    spr->pushSprite(0, 0);
  }
};

#endif // ECL_ENGINE_H
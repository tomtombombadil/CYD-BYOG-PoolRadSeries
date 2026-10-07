#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "SoundEngine.h"
#include "Character.h"
#include "MapEngine.h"
#include "Viewport.h"
#include "ECLEngine.h"
#include "CombatEngine.h"
#include "CampMenu.h"
#include "Minimap.h"

// --- Display Configuration ---
// Note: Substitute with your specific board's LovyanGFX configuration
class LGFX : public lgfx::LGFX_SPI {
  // Config parameters omitted for space - insert your standard Lovyan config here
};
LGFX tft;
LGFX_Sprite sprViewport(&tft);

// --- Subsystems ---
SoundEngine sound(26); 
MapEngine mapEngine;
Viewport viewport;
ECLEngine eclEngine;
CombatEngine combatEngine;
CampMenu campMenu;
Minimap minimap;

// --- Global State ---
Character party[6];
int partySize = 4;
bool inCampMode = false;
bool inCombatMode = false;
bool inMapMode = false;

const uint8_t demoEncounterScript[] = {
  0x01, 'G', 'o', 'b', 'l', 'i', 'n', 's', ' ', 'a', 't', 't', 'a', 'c', 'k', '!', 0x00,
  0x02, // Trigger combat
  0x00
};

// --- Prototypes ---
void updateDisplay();
void renderDashboard();
void renderControlDeck();

void setup() {
  Serial.begin(115200);
  
  tft.init();
  tft.setRotation(1);
  sprViewport.createSprite(320, 200);
  sound.init();

  party[0] = {"Fighter", 15, 15, 18, 12, 14, 10, 10, 12, 4, 1, 100, true};
  party[1] = {"Cleric",  12, 12, 14, 10, 12, 10, 16, 14, 5, 1, 50, true};
  party[2] = {"Mage",     6,  6,  8, 14, 10, 18, 12, 10, 9, 1, 20, true};
  party[3] = {"Thief",    8,  8, 12, 18, 10, 12,  8, 14, 7, 1, 75, true};

  minimap.markExplored(mapEngine.getPlayerX(), mapEngine.getPlayerY());
  updateDisplay();
}

void loop() {
  int32_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {
    
    // Viewport Touch Routing (Top 200px)
    if (ty < 200) {
      if (eclEngine.isActive()) {
        eclEngine.handleTouch(tx, ty);
        if (!eclEngine.isActive() && eclEngine.startCombatFlag) {
          eclEngine.startCombatFlag = false;
          inCombatMode = true;
          combatEngine.initCombat(party, partySize, 3);
        }
        updateDisplay();
      } 
      else if (inCombatMode) {
        combatEngine.handleTouch(tx, ty, party, sound);
        if (combatEngine.isCombatOver()) inCombatMode = false;
        updateDisplay();
      }
      else if (inCampMode) {
        int px = mapEngine.getPlayerX(), py = mapEngine.getPlayerY(), pdir = mapEngine.getPlayerDir();
        campMenu.handleTouch(tx, ty, party, partySize, px, py, pdir, sound, inCampMode);
        updateDisplay();
      }
    } 
    // Control Deck Touch Routing (Bottom 40px)
    else {
      if (eclEngine.isActive()) return;

      if (inCombatMode) {
        combatEngine.handleTouch(tx, ty, party, sound);
        if (combatEngine.isCombatOver()) inCombatMode = false;
        updateDisplay();
      }
      else if (!inCampMode && !inMapMode) {
        if (tx >= 100 && tx <= 160 && ty >= 205 && ty <= 235) { // FORWARD
          if (mapEngine.moveForward()) {
            sound.play(SND_STEP);
            minimap.markExplored(mapEngine.getPlayerX(), mapEngine.getPlayerY());
            if (random(0, 100) < 15) eclEngine.executeScript(demoEncounterScript);
            updateDisplay();
          } else { sound.play(SND_BUMP); }
        }
        else if (tx >= 20 && tx <= 80 && ty >= 205 && ty <= 235) { // LEFT
          mapEngine.turnLeft(); updateDisplay();
        }
        else if (tx >= 180 && tx <= 240 && ty >= 205 && ty <= 235) { // RIGHT
          mapEngine.turnRight(); updateDisplay();
        }
        else if (tx >= 260 && tx <= 310 && ty >= 205 && ty <= 220) { // CAMP
          inCampMode = true; sound.play(SND_CLICK); updateDisplay();
        }
        else if (tx >= 260 && tx <= 310 && ty >= 220 && ty <= 235) { // MAP
          inMapMode = true; sound.play(SND_CLICK); updateDisplay();
        }
      } 
      else if (inMapMode) {
        // Tapping the bottom deck while in Map Mode exits Map Mode
        inMapMode = false; sound.play(SND_CLICK); updateDisplay();
      }
    }
    delay(150); // Debounce
  }
}

// --- Rendering Loop Dispatcher ---
void updateDisplay() {
  if (eclEngine.isActive()) {
    eclEngine.render(&sprViewport);
  } else if (inMapMode) {
    minimap.render(&sprViewport, mapEngine, mapEngine.getPlayerX(), mapEngine.getPlayerY(), mapEngine.getPlayerDir());
  } else if (inCombatMode) {
    combatEngine.render(&sprViewport, party, partySize);
  } else if (inCampMode) {
    campMenu.render(&sprViewport, party, partySize);
  } else {
    viewport.render(&sprViewport, mapEngine);
  }

  // Dashboard and Deck only shown when exploring
  if (!inCombatMode && !inCampMode && !inMapMode && !eclEngine.isActive()) {
    renderDashboard();
    renderControlDeck();
  }
}

void renderDashboard() {
  // Draws Party Stats to the right of the 220px viewport
  tft.fillRect(220, 0, 100, 200, TFT_NAVY);
  tft.drawRect(220, 0, 100, 200, TFT_WHITE);
  
  tft.setTextDatum(TL_DATUM);
  for (int i = 0; i < partySize; i++) {
    int yOffset = 10 + (i * 45);
    
    // Name (Red if dead, White if alive)
    tft.setTextColor(party[i].alive ? TFT_WHITE : TFT_RED, TFT_NAVY);
    tft.drawString(party[i].name, 225, yOffset);
    
    // HP / Max HP Bar
    tft.setTextColor(TFT_LIGHTGREY, TFT_NAVY);
    char hpBuf[16];
    snprintf(hpBuf, sizeof(hpBuf), "HP %d/%d", party[i].hp, party[i].maxHp);
    tft.drawString(hpBuf, 225, yOffset + 15);
    
    // AC and Level
    char statBuf[16];
    snprintf(statBuf, sizeof(statBuf), "AC %d Lv %d", party[i].ac, party[i].level);
    tft.drawString(statBuf, 225, yOffset + 30);
  }
}

void renderControlDeck() {
  tft.fillRect(0, 200, 320, 40, TFT_BLACK);
  tft.drawRect(0, 200, 320, 40, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_BLUE);
  tft.setTextDatum(MC_DATUM);

  tft.fillRoundRect(20, 205, 60, 30, 4, TFT_BLUE);
  tft.drawString("LEFT", 50, 220);

  tft.fillRoundRect(100, 205, 60, 30, 4, TFT_BLUE);
  tft.drawString("FORWARD", 130, 220);

  tft.fillRoundRect(180, 205, 60, 30, 4, TFT_BLUE);
  tft.drawString("RIGHT", 210, 220);

  tft.fillRoundRect(260, 202, 50, 16, 2, TFT_MAROON);
  tft.drawString("CAMP", 285, 210);

  tft.fillRoundRect(260, 220, 50, 16, 2, TFT_DARKGREEN);
  tft.drawString("MAP", 285, 228);
}
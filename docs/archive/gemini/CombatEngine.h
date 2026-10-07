#ifndef COMBAT_ENGINE_H
#define COMBAT_ENGINE_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "Character.h"
#include "SoundEngine.h"
#include "SpellEngine.h"

struct Enemy {
  char name[12];
  int hp;
  int ac;
  int thac0;
  int x, y;
  bool alive;
};

struct TurnRecord {
  bool isEnemy;
  int index;
  int dex;
};

class CombatEngine {
private:
  Enemy enemies[6];
  int numEnemies;
  int partyX[6], partyY[6];
  
  TurnRecord turnOrder[12];
  int numCombatants;
  int currentTurnIndex;

  int cursorX, cursorY;
  char combatLog[64];
  
  SpellEngine spellEngine;
  bool inSpellMenu;

public:
  CombatEngine() : inSpellMenu(false) {}

  void initCombat(Character party[], int numParty, int enemiesToSpawn) {
    numEnemies = enemiesToSpawn;
    numCombatants = 0;

    // Place Party at bottom of 11x11 grid
    for (int i = 0; i < numParty; i++) {
      if (party[i].alive) {
        partyX[i] = 4 + i;
        partyY[i] = 9;
        turnOrder[numCombatants++] = {false, i, party[i].dex};
      }
    }

    // Place Enemies at top of grid
    for (int i = 0; i < numEnemies; i++) {
      snprintf(enemies[i].name, sizeof(enemies[i].name), "Goblin %d", i + 1);
      enemies[i].hp = 5;
      enemies[i].ac = 6;
      enemies[i].thac0 = 19;
      enemies[i].x = 3 + (i * 2);
      enemies[i].y = 2;
      enemies[i].alive = true;
      turnOrder[numCombatants++] = {true, i, 10 + random(0, 4)}; // Rand Dex
    }

    sortInitiative();
    currentTurnIndex = 0;
    cursorX = 5; cursorY = 5;
    inSpellMenu = false;
    snprintf(combatLog, sizeof(combatLog), "Combat Begins!");
  }

  bool isCombatOver() {
    bool enemyAlive = false;
    for (int i = 0; i < numEnemies; i++) if (enemies[i].alive) enemyAlive = true;
    return !enemyAlive;
  }

  bool isPlayerTurn() const {
    if (currentTurnIndex >= numCombatants) return false;
    return !turnOrder[currentTurnIndex].isEnemy;
  }

  void nextTurn() {
    do {
      currentTurnIndex++;
      if (currentTurnIndex >= numCombatants) {
        currentTurnIndex = 0; // New Round
        sortInitiative();
      }
    } while (!isCombatantAlive(turnOrder[currentTurnIndex]));
  }

  void handleTouch(int tx, int ty, Character party[], SoundEngine& sound) {
    // Top Viewport Touches
    if (ty < 200) {
      if (spellEngine.isTargeting()) {
        int gx = (tx - 67) / 17;
        int gy = (ty - 6) / 17;
        if (gx >= 0 && gx < 11 && gy >= 0 && gy < 11) {
          cursorX = gx; cursorY = gy;
          sound.play(SND_CLICK);
        }
      } else if (inSpellMenu) {
        if (ty >= 50 && ty <= 170) {
          int spellIdx = (ty - 50) / 30;
          if (spellIdx >= 0 && spellIdx < spellEngine.getSpellCount()) {
            spellEngine.selectSpell(spellIdx);
            spellEngine.setTargeting(true);
            inSpellMenu = false;
            sound.play(SND_CLICK);
          }
        }
      } else if (isPlayerTurn()) {
        int gx = (tx - 67) / 17;
        int gy = (ty - 6) / 17;
        if (gx >= 0 && gx < 11 && gy >= 0 && gy < 11) {
          cursorX = gx; cursorY = gy;
          sound.play(SND_CLICK);
        }
      }
    } 
    // Bottom Deck Touches
    else {
      if (isPlayerTurn() && !inSpellMenu) {
        // MOVE / ATTACK Button
        if (tx >= 20 && tx <= 100 && ty >= 205 && ty <= 235) {
          if (!spellEngine.isTargeting()) executePlayerAction(party, sound);
        }
        // CAST SPELL Button
        else if (tx >= 110 && tx <= 190 && ty >= 205 && ty <= 235) {
          if (spellEngine.isTargeting()) {
            executeSpellAtCursor(party, sound);
          } else {
            inSpellMenu = true;
            sound.play(SND_CLICK);
          }
        }
        // END TURN Button
        else if (tx >= 200 && tx <= 280 && ty >= 205 && ty <= 235) {
          if (spellEngine.isTargeting()) {
            spellEngine.setTargeting(false); // Cancel spell
            sound.play(SND_CLICK);
          } else {
            nextTurn();
          }
        }
      }
    }
  }

  void render(LGFX_Sprite* spr, Character party[], int numParty) {
    if (inSpellMenu) {
      spellEngine.renderSpellMenu(spr);
      spr->pushSprite(0, 0);
      return;
    }

    spr->fillScreen(TFT_BLACK);
    
    // Draw 11x11 Grid (Starts at x:67, y:6 to center it)
    spr->drawRect(65, 4, 11 * 17 + 4, 11 * 17 + 4, TFT_DARKGREY);
    for (int x = 0; x < 11; x++) {
      for (int y = 0; y < 11; y++) {
        spr->drawRect(67 + (x * 17), 6 + (y * 17), 17, 17, TFT_NAVY);
      }
    }

    // Draw Party
    for (int i = 0; i < numParty; i++) {
      if (party[i].alive) {
        spr->fillCircle(67 + (partyX[i] * 17) + 8, 6 + (partyY[i] * 17) + 8, 6, TFT_CYAN);
      }
    }

    // Draw Enemies
    for (int i = 0; i < numEnemies; i++) {
      if (enemies[i].alive) {
        spr->fillCircle(67 + (enemies[i].x * 17) + 8, 6 + (enemies[i].y * 17) + 8, 6, TFT_RED);
      }
    }

    // Draw Cursor & Overlays
    if (spellEngine.isTargeting()) {
      const Spell& spell = spellEngine.getSpell(spellEngine.getSelectedSpellIndex());
      renderSpellTargetOverlay(spr, cursorX, cursorY, spell.aoeRadius);
    } else {
      spr->drawRect(67 + (cursorX * 17), 6 + (cursorY * 17), 17, 17, TFT_YELLOW);
    }

    // Draw Log
    spr->fillRect(0, 185, 320, 15, TFT_MAROON);
    spr->setTextColor(TFT_WHITE, TFT_MAROON);
    spr->setTextDatum(TL_DATUM);
    spr->drawString(combatLog, 5, 186);

    spr->pushSprite(0, 0);
  }

private:
  void executePlayerAction(Character party[], SoundEngine& sound) {
    TurnRecord current = turnOrder[currentTurnIndex];
    int pIdx = current.index;
    
    // Check if Enemy at cursor
    for (int i = 0; i < numEnemies; i++) {
      if (enemies[i].alive && enemies[i].x == cursorX && enemies[i].y == cursorY) {
        int dist = abs(partyX[pIdx] - cursorX) + abs(partyY[pIdx] - cursorY);
        if (dist <= 1) { // Melee range
          sound.play(SND_ATTACK);
          int roll = random(1, 21);
          int thac0 = 20 - (party[pIdx].level / 2); 
          if (roll >= (thac0 - enemies[i].ac)) {
            int dmg = random(1, 9); // 1d8 Longsword
            enemies[i].hp -= dmg;
            sound.play(SND_HIT);
            if (enemies[i].hp <= 0) {
              enemies[i].alive = false;
              snprintf(combatLog, sizeof(combatLog), "%s slays %s!", party[pIdx].name, enemies[i].name);
            } else {
              snprintf(combatLog, sizeof(combatLog), "%s hits for %d!", party[pIdx].name, dmg);
            }
          } else {
            sound.play(SND_MISS);
            snprintf(combatLog, sizeof(combatLog), "%s misses.", party[pIdx].name);
          }
          nextTurn();
          return;
        }
      }
    }

    // Move to Cursor if empty
    bool occupied = false;
    for(int i=0; i<6; i++) { if (partyX[i] == cursorX && partyY[i] == cursorY) occupied = true; }
    for(int i=0; i<numEnemies; i++) { if (enemies[i].alive && enemies[i].x == cursorX && enemies[i].y == cursorY) occupied = true; }
    
    if (!occupied) {
      partyX[pIdx] = cursorX;
      partyY[pIdx] = cursorY;
      sound.play(SND_STEP);
    }
  }

  void executeSpellAtCursor(Character party[], SoundEngine& sound) {
    if (turnOrder[currentTurnIndex].isEnemy) return;

    const Spell& spell = spellEngine.getSpell(spellEngine.getSelectedSpellIndex());

    if (spell.id == SPELL_MAGIC_MISSILE) {
      for (int i = 0; i < numEnemies; i++) {
        if (enemies[i].alive && enemies[i].x == cursorX && enemies[i].y == cursorY) {
          int dmg = spellEngine.castMagicMissile(sound);
          enemies[i].hp -= dmg;
          if (enemies[i].hp <= 0) { enemies[i].hp = 0; enemies[i].alive = false; snprintf(combatLog, sizeof(combatLog), "MM slays target!"); }
          else { snprintf(combatLog, sizeof(combatLog), "MM hits for %d!", dmg); }
          break;
        }
      }
    } else if (spell.id == SPELL_FIREBALL) {
      int dmg = spellEngine.castFireball(sound);
      int hits = 0;
      for (int i = 0; i < numEnemies; i++) {
        if (enemies[i].alive && abs(enemies[i].x - cursorX) <= 1 && abs(enemies[i].y - cursorY) <= 1) {
          enemies[i].hp -= dmg; hits++;
          if (enemies[i].hp <= 0) { enemies[i].hp = 0; enemies[i].alive = false; }
        }
      }
      snprintf(combatLog, sizeof(combatLog), "Fireball hits %d for %d dmg!", hits, dmg);
    } else if (spell.id == SPELL_CURE_LIGHT) {
      for (int i = 0; i < 6; i++) {
        if (party[i].alive && partyX[i] == cursorX && partyY[i] == cursorY) {
          int healed = spellEngine.castHealing(party[i], sound);
          snprintf(combatLog, sizeof(combatLog), "Healed %d HP!", healed);
          break;
        }
      }
    }

    spellEngine.setTargeting(false);
    nextTurn();
  }

  void renderSpellTargetOverlay(LGFX_Sprite* spr, int tx, int ty, int radius) {
    for (int dx = -radius; dx <= radius; dx++) {
      for (int dy = -radius; dy <= radius; dy++) {
        int gx = tx + dx, gy = ty + dy;
        if (gx >= 0 && gx < 11 && gy >= 0 && gy < 11) {
          int px = 67 + (gx * 17), py = 6 + (gy * 17);
          spr->drawRect(px, py, 17, 17, TFT_MAGENTA);
        }
      }
    }
  }

  bool isCombatantAlive(const TurnRecord& rec) {
    return rec.isEnemy ? enemies[rec.index].alive : true;
  }

  void sortInitiative() {
    for (int i = 0; i < numCombatants - 1; i++) {
      for (int j = 0; j < numCombatants - i - 1; j++) {
        if (turnOrder[j].dex < turnOrder[j+1].dex) {
          TurnRecord temp = turnOrder[j];
          turnOrder[j] = turnOrder[j+1];
          turnOrder[j+1] = temp;
        }
      }
    }
  }
};

#endif // COMBAT_ENGINE_H
#ifndef SPELL_ENGINE_H
#define SPELL_ENGINE_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "Character.h"
#include "SoundEngine.h"

enum SpellType {
  SPELL_MAGIC_MISSILE, 
  SPELL_CURE_LIGHT,    
  SPELL_SLEEP,         
  SPELL_FIREBALL       
};

enum SpellTargetType {
  TARGET_SINGLE_ENEMY,
  TARGET_SINGLE_ALLY,
  TARGET_AOE_GRID
};

struct Spell {
  uint8_t id;
  char name[18];
  uint8_t level;
  SpellTargetType targetType;
  uint8_t range; 
  uint8_t aoeRadius; 
};

class SpellEngine {
private:
  Spell spellDb[4];
  int selectedSpellIdx;
  bool targetModeActive;

public:
  SpellEngine() : selectedSpellIdx(-1), targetModeActive(false) {
    spellDb[0] = { SPELL_MAGIC_MISSILE, "Magic Missile", 1, TARGET_SINGLE_ENEMY, 6, 0 };
    spellDb[1] = { SPELL_CURE_LIGHT,    "Cure Lt Wounds",1, TARGET_SINGLE_ALLY,  1, 0 };
    spellDb[2] = { SPELL_SLEEP,         "Sleep",         1, TARGET_AOE_GRID,     5, 1 };
    spellDb[3] = { SPELL_FIREBALL,      "Fireball",      3, TARGET_AOE_GRID,     7, 1 };
  }

  const Spell& getSpell(int index) const { return spellDb[index]; }
  int getSpellCount() const { return 4; }

  bool isTargeting() const { return targetModeActive; }
  void setTargeting(bool active) { targetModeActive = active; }

  int getSelectedSpellIndex() const { return selectedSpellIdx; }
  void selectSpell(int index) { selectedSpellIdx = index; }

  int castHealing(Character& target, SoundEngine& sound) {
    sound.play(SND_SPELL);
    int healAmount = random(1, 9); // 1d8
    target.hp += healAmount;
    if (target.hp > target.maxHp) target.hp = target.maxHp;
    return healAmount;
  }

  int castMagicMissile(SoundEngine& sound) {
    sound.play(SND_SPELL);
    return random(1, 5) + 1; // 1d4 + 1
  }

  int castFireball(SoundEngine& sound) {
    sound.play(SND_SPELL);
    int totalDmg = 0;
    for (int i = 0; i < 3; i++) totalDmg += random(1, 7); // 3d6
    return totalDmg;
  }

  void renderSpellMenu(LGFX_Sprite* spr) {
    spr->fillScreen(TFT_BLACK);
    spr->drawRect(10, 10, 300, 180, TFT_PURPLE);
    spr->drawRect(12, 12, 296, 176, TFT_NAVY);

    spr->setTextColor(TFT_GOLD, TFT_NAVY);
    spr->setTextDatum(TC_DATUM);
    spr->drawString("SELECT SPELL TO CAST", 160, 20);

    spr->setTextColor(TFT_WHITE, TFT_BLACK);
    spr->setTextDatum(TL_DATUM);

    for (int i = 0; i < getSpellCount(); i++) {
      int yPos = 50 + (i * 30);
      spr->fillRoundRect(30, yPos, 260, 24, 4, TFT_DARKGREY);
      spr->setCursor(40, yPos + 4);
      spr->printf("Lvl %d: %s", spellDb[i].level, spellDb[i].name);
    }
  }
};

#endif // SPELL_ENGINE_H
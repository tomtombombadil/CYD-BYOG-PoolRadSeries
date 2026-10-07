#ifndef CAMP_MENU_H
#define CAMP_MENU_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "Character.h"
#include "SoundEngine.h"
#include "SaveManager.h"

class CampMenu {
private:
  SaveManager saveManager;

public:
  CampMenu() {}

  void render(LGFX_Sprite* spr, Character party[], int partySize) {
    spr->fillScreen(TFT_BLACK);
    spr->drawRect(5, 5, 310, 190, TFT_MAROON);
    spr->drawRect(7, 7, 306, 186, TFT_GOLD);

    spr->setTextColor(TFT_GOLD, TFT_BLACK);
    spr->setTextDatum(TC_DATUM);
    spr->drawString("-- ENCAMPMENT --", 160, 15);

    spr->setTextColor(TFT_WHITE);
    spr->setTextDatum(MC_DATUM);

    spr->fillRoundRect(30, 45, 120, 35, 4, TFT_NAVY);
    spr->drawString("REST & HEAL", 90, 62);

    spr->fillRoundRect(170, 45, 120, 35, 4, TFT_DARKGREEN);
    spr->drawString("SAVE GAME", 230, 62);

    uint16_t loadColor = saveManager.hasSaveData() ? TFT_PURPLE : TFT_DARKGREY;
    spr->fillRoundRect(30, 95, 120, 35, 4, loadColor);
    spr->drawString("LOAD GAME", 90, 112);

    spr->fillRoundRect(170, 95, 120, 35, 4, TFT_MAROON);
    spr->drawString("EXIT CAMP", 230, 112);

    spr->setTextDatum(TL_DATUM);
    spr->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    for (int i = 0; i < partySize; i++) {
      spr->setCursor(20, 145 + (i * 12));
      spr->printf("%s: %d/%d HP | Gold: %d", party[i].name, party[i].hp, party[i].maxHp, party[i].gold);
    }
    spr->pushSprite(0, 0);
  }

  void handleTouch(int vx, int vy, Character party[], int partySize, int &px, int &py, int &pdir, SoundEngine& sound, bool &inCampMode) {
    if (vx >= 30 && vx <= 150 && vy >= 45 && vy <= 80) { // REST
      for (int i = 0; i < partySize; i++) party[i].hp = party[i].maxHp;
      sound.play(SND_REST);
    }
    else if (vx >= 170 && vx <= 290 && vy >= 45 && vy <= 80) { // SAVE
      if (saveManager.saveGame(px, py, pdir, party, partySize)) sound.play(SND_VICTORY);
    }
    else if (vx >= 30 && vx <= 150 && vy >= 95 && vy <= 130) { // LOAD
      if (saveManager.loadGame(px, py, pdir, party, partySize)) sound.play(SND_SPELL);
    }
    else if (vx >= 170 && vx <= 290 && vy >= 95 && vy <= 130) { // EXIT
      inCampMode = false;
      sound.play(SND_STEP);
    }
  }
};

#endif // CAMP_MENU_H
#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "Character.h"

class SaveManager {
private:
  Preferences prefs;
  const char* NAMESPACE = "goldbox_save";

public:
  SaveManager() {}

  bool hasSaveData() {
    prefs.begin(NAMESPACE, true); 
    bool exists = prefs.getBool("valid", false);
    prefs.end();
    return exists;
  }

  bool saveGame(int pX, int pY, int pDir, Character party[], int partySize) {
    if (!prefs.begin(NAMESPACE, false)) return false; 

    prefs.clear();
    prefs.putInt("pX", pX);
    prefs.putInt("pY", pY);
    prefs.putInt("pDir", pDir);
    prefs.putInt("pSize", partySize);

    for (int i = 0; i < partySize; i++) {
      char prefix[8]; snprintf(prefix, sizeof(prefix), "c%d_", i);
      char key[16];
      snprintf(key, sizeof(key), "%sname", prefix); prefs.putString(key, party[i].name);
      snprintf(key, sizeof(key), "%shp", prefix);   prefs.putInt(key, party[i].hp);
      snprintf(key, sizeof(key), "%smhp", prefix);  prefs.putInt(key, party[i].maxHp);
      snprintf(key, sizeof(key), "%sgld", prefix);  prefs.putInt(key, party[i].gold);
      // Simplify save for brevity, add full stats as needed
    }

    prefs.putBool("valid", true);
    prefs.end();
    return true;
  }

  bool loadGame(int &pX, int &pY, int &pDir, Character party[], int &partySize) {
    if (!hasSaveData()) return false;
    if (!prefs.begin(NAMESPACE, true)) return false;

    pX = prefs.getInt("pX", 1);
    pY = prefs.getInt("pY", 1);
    pDir = prefs.getInt("pDir", 0);
    partySize = prefs.getInt("pSize", 1);

    for (int i = 0; i < partySize; i++) {
      char prefix[8]; snprintf(prefix, sizeof(prefix), "c%d_", i);
      char key[16];
      
      snprintf(key, sizeof(key), "%sname", prefix);
      String nameStr = prefs.getString(key, "Hero");
      strncpy(party[i].name, nameStr.c_str(), sizeof(party[i].name));

      snprintf(key, sizeof(key), "%shp", prefix);   party[i].hp = prefs.getInt(key, 10);
      snprintf(key, sizeof(key), "%smhp", prefix);  party[i].maxHp = prefs.getInt(key, 10);
      snprintf(key, sizeof(key), "%sgld", prefix);  party[i].gold = prefs.getInt(key, 0);
    }

    prefs.end();
    return true;
  }
};

#endif // SAVE_MANAGER_H
#ifndef CHARACTER_H
#define CHARACTER_H

#include <Arduino.h>

struct Character {
  char name[12];
  int hp;
  int maxHp;
  int str, dex, con, intel, wis, cha;
  int ac; // Armor Class (Lower is better, AD&D 1e style)
  int level;
  int gold;
  bool alive;
};

#endif // CHARACTER_H
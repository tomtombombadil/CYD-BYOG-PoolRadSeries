#ifndef MINIMAP_H
#define MINIMAP_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "MapEngine.h"

class Minimap {
private:
  bool visited[16][16];

public:
  Minimap() { clearExploration(); }

  void clearExploration() { memset(visited, 0, sizeof(visited)); }

  void markExplored(int x, int y) {
    if (x >= 0 && x < 16 && y >= 0 && y < 16) visited[x][y] = true;
  }

  void render(LGFX_Sprite* spr, const MapEngine& map, int px, int py, int pDir) {
    spr->fillScreen(TFT_BLACK);
    spr->drawRect(0, 0, 320, 200, TFT_GOLD);

    const int CELL_SIZE = 10;
    const int START_X = 80;
    const int START_Y = 20;

    spr->setTextColor(TFT_GOLD, TFT_BLACK);
    spr->setTextDatum(TC_DATUM);
    spr->drawString("-- DUNGEON AUTO-MAP --", 160, 5);

    for (int y = 0; y < 16; y++) {
      for (int x = 0; x < 16; x++) {
        if (!visited[x][y]) continue;

        int dx = START_X + (x * CELL_SIZE);
        int dy = START_Y + (y * CELL_SIZE);
        spr->fillRect(dx, dy, CELL_SIZE - 1, CELL_SIZE - 1, TFT_DARKGREY);

        if (map.isWall(x, y - 1)) spr->drawLine(dx, dy, dx + CELL_SIZE, dy, TFT_WHITE); 
        if (map.isWall(x, y + 1)) spr->drawLine(dx, dy + CELL_SIZE, dx + CELL_SIZE, dy + CELL_SIZE, TFT_WHITE);
        if (map.isWall(x - 1, y)) spr->drawLine(dx, dy, dx, dy + CELL_SIZE, TFT_WHITE); 
        if (map.isWall(x + 1, y)) spr->drawLine(dx + CELL_SIZE, dy, dx + CELL_SIZE, dy + CELL_SIZE, TFT_WHITE); 
      }
    }

    int cx = START_X + (px * CELL_SIZE) + (CELL_SIZE / 2);
    int cy = START_Y + (py * CELL_SIZE) + (CELL_SIZE / 2);
    
    if (pDir == 0) spr->fillTriangle(cx, cy - 4, cx - 3, cy + 3, cx + 3, cy + 3, TFT_RED);
    else if (pDir == 1) spr->fillTriangle(cx + 4, cy, cx - 3, cy - 3, cx - 3, cy + 3, TFT_RED);
    else if (pDir == 2) spr->fillTriangle(cx, cy + 4, cx - 3, cy - 3, cx + 3, cy - 3, TFT_RED);
    else if (pDir == 3) spr->fillTriangle(cx - 4, cy, cx + 3, cy - 3, cx + 3, cy + 3, TFT_RED);

    spr->pushSprite(0, 0);
  }
};

#endif // MINIMAP_H
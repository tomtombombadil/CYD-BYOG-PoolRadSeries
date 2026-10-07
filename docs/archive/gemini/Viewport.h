#ifndef VIEWPORT_H
#define VIEWPORT_H

#include <Arduino.h>
#include <LovyanGFX.hpp>
#include "MapEngine.h"

class Viewport {
public:
  Viewport() {}

  void render(LGFX_Sprite* spr, const MapEngine& map) {
    spr->fillRect(0, 0, 220, 200, TFT_BLACK);

    int px = map.getPlayerX();
    int py = map.getPlayerY();
    int dir = map.getPlayerDir();

    for (int depth = 3; depth >= 0; depth--) {
      int checkX = px, checkY = py;
      
      // Calculate target tile based on facing direction and depth
      if (dir == 0) checkY -= depth;
      else if (dir == 1) checkX += depth;
      else if (dir == 2) checkY += depth;
      else if (dir == 3) checkX -= depth;

      // Check walls straight ahead, and immediate left/right sides
      bool wallAhead = map.isWall(checkX, checkY);
      
      int leftX = checkX, leftY = checkY;
      if (dir == 0) leftX--; else if (dir == 1) leftY--; else if (dir == 2) leftX++; else if (dir == 3) leftY++;
      bool wallLeft = map.isWall(leftX, leftY);

      int rightX = checkX, rightY = checkY;
      if (dir == 0) rightX++; else if (dir == 1) rightY++; else if (dir == 2) rightX--; else if (dir == 3) rightY--;
      bool wallRight = map.isWall(rightX, rightY);

      drawSegment(spr, depth, wallAhead, wallLeft, wallRight);
    }
    
    // Draw viewport border
    spr->drawRect(0, 0, 220, 200, TFT_WHITE);
    spr->pushSprite(0, 0); // Assuming viewport goes in top-left
  }

private:
  void drawSegment(LGFX_Sprite* spr, int depth, bool wallAhead, bool wallLeft, bool wallRight) {
    // Simplified perspective coordinates for a 220x200 window
    int rects[4][4] = {
      {0,   0,   220, 200}, // Depth 0 (Immediate)
      {30,  20,  160, 160}, // Depth 1
      {60,  40,  100, 120}, // Depth 2
      {90,  60,  40,  80}   // Depth 3
    };

    if (depth >= 4) return;

    int x = rects[depth][0], y = rects[depth][1], w = rects[depth][2], h = rects[depth][3];

    // Floor and Ceiling lines
    if (depth > 0) {
      int px = rects[depth-1][0], py = rects[depth-1][1], pw = rects[depth-1][2], ph = rects[depth-1][3];
      spr->drawLine(px, py, x, y, TFT_DARKGREY);
      spr->drawLine(px + pw, py, x + w, y, TFT_DARKGREY);
      spr->drawLine(px, py + ph, x, y + h, TFT_DARKGREY);
      spr->drawLine(px + pw, py + ph, x + w, y + h, TFT_DARKGREY);
    }

    if (wallAhead) spr->drawRect(x, y, w, h, TFT_LIGHTGREY);
    if (wallLeft && depth > 0) spr->drawLine(rects[depth-1][0], rects[depth-1][1], x, y, TFT_WHITE);
    if (wallRight && depth > 0) spr->drawLine(rects[depth-1][0] + rects[depth-1][2], rects[depth-1][1], x + w, y, TFT_WHITE);
  }
};

#endif // VIEWPORT_H
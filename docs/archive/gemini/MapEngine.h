#ifndef MAP_ENGINE_H
#define MAP_ENGINE_H

#include <Arduino.h>

class MapEngine {
private:
  uint8_t mapData[16][16];
  int playerX, playerY;
  int playerDir; // 0=N, 1=E, 2=S, 3=W

public:
  MapEngine() : playerX(1), playerY(1), playerDir(1) {
    memset(mapData, 0, sizeof(mapData));
    // Build a simple room and corridor
    for (int i = 0; i < 16; i++) { mapData[0][i] = 1; mapData[15][i] = 1; mapData[i][0] = 1; mapData[i][15] = 1; }
    mapData[2][2] = 1; mapData[2][3] = 1; mapData[3][2] = 1;
  }

  bool isWall(int x, int y) const {
    if (x < 0 || x >= 16 || y < 0 || y >= 16) return true;
    return mapData[x][y] > 0;
  }

  int getPlayerX() const { return playerX; }
  int getPlayerY() const { return playerY; }
  int getPlayerDir() const { return playerDir; }

  bool moveForward() {
    int nx = playerX, ny = playerY;
    if (playerDir == 0) ny--; else if (playerDir == 1) nx++;
    else if (playerDir == 2) ny++; else if (playerDir == 3) nx--;
    if (!isWall(nx, ny)) { playerX = nx; playerY = ny; return true; }
    return false;
  }

  void turnLeft() { playerDir = (playerDir + 3) % 4; }
  void turnRight() { playerDir = (playerDir + 1) % 4; }
};

#endif // MAP_ENGINE_H
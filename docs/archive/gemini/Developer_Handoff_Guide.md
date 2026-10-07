CYD Gold Box RPG Engine: Technical Developer Guide and Claude.ai Handoff Specification  
1\. Executive Overview & Project Vision  
The "Bring Your Own Game" (BYOG) Gold Box RPG Quadlogy engine provides a high-performance native C++ runtime environment designed to execute classic late-1980s and early-1990s Strategic Simulations, Inc. (SSI) turn-based tactical RPGs on the Cheap Yellow Display (CYD / ESP32) micro-console family. The targeted quadlogy comprises:

1. *Pool of Radiance* (1988)  
2. *Curse of the Azure Bonds* (1989)  
3. *Secret of the Silver Blades* (1990)  
4. *Pools of Darkness* (1991)

Hardware & Processing Alignment  
Original MS-DOS, Apple II, and Commodore 64 releases executed on 4.77 MHz to 10 MHz Intel 8086/8088 processors or 1 MHz MOS 6510 CPUs. The CYD hardware is driven by an ESP32 Xtensa LX6 dual-core processor running at a clock frequency of 240 MHz.  
Because turn-based game logic and frame rendering consume only a fraction of a single Xtensa core's capacity, the ESP32 provides massive compute headroom. Rendering loops utilize 8-bit indexed color palettes mapped directly to hardware lookup tables. This eliminates the need for expensive 32-bit floating-point transformations, allowing graphics decompression and spatial rendering routines to execute via single-cycle integer lookups.  
\+-------------------------------------------------------------------------+  
|                         PROCESSING CAPACITY                             |  
|                                                                         |  
|  1980s PC (4.77–10 MHz 8086\)  \[==\]                                      |  
|  CYD ESP32 (240 MHz LX6)      \[=======================================\] |  
\+-------------------------------------------------------------------------+

Storage Efficiency & Peak SRAM Allocation

* **SD Card Asset Storage:** The raw asset archives (`.DAX`, `.GEO`, `.TLB`, `.ECL`, `.CHA`, `.SAV`, `JOURNAL.TXT`) across all four games require approximately 12.0 MB total storage. This footprint consumes under 1% of a standard microSD card, allowing all four full games to reside on a single card simultaneously.  
* **Onboard SPI Flash Memory:** The compiled C++ engine executable (incorporating LovyanGFX graphics drivers, SPI bus handlers, and core engine submodules) yields a firmware binary footprint between 1.2 MB and 1.8 MB. This fits comfortably within the 4 MB onboard SPI Flash of standard ESP32 development modules without requiring complex multi-app Over-The-Air (OTA) flash partitioning.  
* **Peak Execution SRAM Footprint Analysis:** The ESP32 provides 520 KB of total internal SRAM, yielding approximately 300 KB of usable heap memory. During peak execution (such as active 2D tactical combat with live script evaluation), the engine allocates memory across four primary buffers:

\\begin{aligned} \\text{LovyanGFX Off-Screen Canvas Sprites} &\\approx 153.6\\text{ KB} \\ \\text{Active } \\texttt{.DAX} \\text{ Tile Decompression Buffers} &\\approx 32.0\\text{ KB} \- 64.0\\text{ KB} \\ \\text{Map Grid Structs & Event Tables} &\\approx 4.0\\text{ KB} \\ \\text{ECL String Buffers & Character Records} &\\approx 8.0\\text{ KB} \\ \\hline \\mathbf{Total\\ Combined\\ Engine\\ Allocation} &\\approx \\mathbf{197.6\\text{ KB}} \- \\mathbf{229.6\\text{ KB}} \\end{aligned}  
This leaves a safety margin of $\sim 70.4KB$ to $102.4KB$ of contiguous internal SRAM for dynamic runtime allocations, ensuring complete stability without mandating external PSRAM modules.  
Hardware Specification Comparison

| Feature / Spec | Original MS-DOS Hardware Target | Target CYD Hardware (ESP32) |
| ----- | ----- | ----- |
| **CPU Clock Speed** | 4.77 MHz – 10 MHz (Intel 8086/8088) | 240 MHz Dual-Core Xtensa LX6 |
| **Display Native Resolution** | 320×200 pixels | 320×240 (2.8" CYD) or 480×320 (3.5" CYD) |
| **Palette Depth** | 16-color EGA/CGA/Tandy (256-color VGA for *POD*) | 16-bit RGB565 LCD Panel (via 8-bit indexed buffers) |
| **Framebuffer RAM Allocation** | 32 KB (4-bit indexed) / 64 KB (8-bit indexed) | \~153.6 KB (Split 8-bit indexed canvas sprites) |
| **Firmware / Binary Footprint** | \~1.5 MB – 4.5 MB per disk set (\~12.0 MB total) | \~1.2 MB – 1.8 MB compiled SPI Flash footprint |

---

2\. Target Hardware Specifications & System Constraints  
The engine targets two main board variants in the Cheap Yellow Display family: the standard ESP32-2432S028R (2.8" display) and the expanded ESP32-3248S035 (3.5" display).  
SPI Bus & Peripheral Pinout Assignment  
The ESP32 manages high-speed display transactions, resistive touchscreen inputs, SD card file streaming, and software PWM audio generation across dedicated hardware SPI buses and timers:

| Bus / Peripheral | Signal Line | ESP32 GPIO Pin | Driver / Parameters |
| ----- | ----- | ----- | ----- |
| **Display SPI (HSPI)** | SCLK | GPIO 14 | Clock speed: 40 MHz |
|  | MOSI | GPIO 13 | Master Out Slave In |
|  | MISO | GPIO 12 | Master In Slave Out |
|  | DC | GPIO 2 | Data / Command Control Line |
|  | CS | GPIO 15 | Display Chip Select |
|  | Backlight | GPIO 21 | Display Backlight Control (PWM/Digital) |
|  | Panel Drivers | — | ILI9341 / ST7796 / ILI9488 |
| **Touch Controller (VSPI)** | CS | GPIO 33 | XPT2046 Touch Chip Select |
|  | SCLK | GPIO 25 | Shared SPI Clock |
|  | MOSI | GPIO 32 | Master Out Slave In |
|  | MISO | GPIO 39 | Master In Slave Out (Input Only Pin) |
|  | Calibration | — | X Bounds: 300–3800, Y Bounds: 300–3800 |
| **MicroSD Card (VSPI)** | CS | GPIO 5 | SD Card Chip Select |
|  | SCLK | GPIO 18 | Dedicated Bus Clock (20 MHz) |
|  | MISO | GPIO 19 | Master In Slave Out |
|  | MOSI | GPIO 23 | Master Out Slave In |
| **Audio Output** | Speaker Out | GPIO 26 | Driven by ESP32 LEDC PWM Timer Channel 0 |

              \+----------------------------------+  
               |     ESP32 DUAL-CORE PROCESSOR    |  
               \+----------------+-----------------+  
                                |  
        \+-----------------------+-----------------------+  
        | HSPI BUS (40 MHz)                             | VSPI BUS (20 MHz)  
        v                                               v  
\+---------------+-------+                      \+----------------+-------+  
| ILI9341 / ST7796 LCD  |                      |  XPT2046 Touch | SD    |  
| (Display DMA Channel) |                      |  Controller    | Card  |  
\+-----------------------+                      \+----------------+-------+

Double-Bus Architecture & Bus Contention Isolation  
The display operates on **HSPI** with hardware Direct Memory Access (DMA) running at 40 MHz. The microSD card reader and XPT2046 touch controller share **VSPI** running at 20 MHz. Physical isolation of display DMA transactions from SD card asset streaming eliminates bus contention, preventing frame tearing, screen flicker, and audio stutter when streaming `.DAX` textures or `.GEO` map data during fast movement.  
---

3\. MicroSD File System Architecture & Data Asset Layout  
All original game assets must be extracted from standard PC disk images and placed within a unified `/GOLDBOX/` root directory on a FAT32-formatted microSD card.  
Directory Layout  
/  
└── GOLDBOX/  
    ├── POOLRAD/             \<-- Pool of Radiance (1988)  
    │   ├── START.DAX  
    │   ├── WALL.DAX  
    │   ├── CHRDATA.DAX  
    │   ├── JOURNAL.TXT  
    │   └── ...  
    ├── CURSE/               \<-- Curse of the Azure Bonds (1989)  
    │   ├── START.DAX  
    │   ├── WALLS.DAX  
    │   ├── CHRDATA.DAX  
    │   └── ...  
    ├── SILVER/              \<-- Secret of the Silver Blades (1990)  
    │   ├── START.DAX  
    │   ├── WALLS.DAX  
    │   └── ...  
    └── POOLDARK/            \<-- Pools of Darkness (1991)  
        ├── START.DAX  
        ├── WALLS.DAX  
        └── ...

Data Asset Extensions & Technical Definitions

| Extension | Technical Definition | Data Structure Type | Operational Role in Engine |
| ----- | ----- | ----- | ----- |
| **.DAX** | Graphics & Sprite Archive | Run-Length Encoded (RLE) Bitmaps (4-bit EGA nibbles / 8-bit VGA bytes) | Stores wall textures, UI frame elements, title screens, monster sprites, and combat icons. |
| **.GEO** | Geometry Map File | Binary array of 16×16 grid cells (6 bytes/cell) | Defines dungeon wall direction bitmasks (N, E, S, W), door flags, and ECL event triggers. |
| **.TLB** | Tile Library / Combat Layout | Graphic tile sheets and grid layouts | Stores ground textures and map layouts for 2D tactical combat grids. |
| **.ECL** | Event Control Language | Compiled Bytecode Scripting Language | Drives story scripts, dialogue branching, quest state flags, shops, and traps. |
| **.CHA** | Character Record | Binary Struct (Attributes, Level, Stats) | Stores individual party member ability scores, HP, THAC0, inventory arrays, and spell slots. |
| **.SAV** | Engine Save State | Serialized Binary Game Record | Retains world state, map coordinates (`pX`, `pY`, `pDir`), inventory, and party flags. |
| **JOURNAL.TXT** | Adventurers' Journal Text | Plain ASCII Text with tagged entries | Contains story and copy-protection text read dynamically by the in-engine journal reader. |

---

4\. Display & Touch Interface Architectures (Strategy A vs. Strategy B)  
The engine supports two screen layout strategies tailored to different display dimensions.  
Strategy A: 320×240 Display Layout (2.8" CYD)  
Strategy A places the native 320×200 pixel game viewport at the top of the display (`(0,0)` to `(319,199)`). The remaining 320×40 pixel strip at the bottom (`(0,200)` to `(319,239)`) hosts dynamic softkeys.

* **Touch Grid Geometry:** 2 rows × 8 columns, producing 16 touch targets measuring **40×20 pixels** each.  
* **Contextual Softkey Mapping:** Automatically toggles key maps between Exploration Mode, Tactical Combat Mode, and Inventory/Menu Mode.

(0,0)                                                               (319,0)  
\+-------------------------------------------------------------------+  
|                                                                   |  
|                       NATIVE GAME VIEWPORT                        |  
|                            (320x200)                              |  
|                                                                   |  
\+-------------------------------------------------------------------+ (0,200)  
|  NW  |  N   |  NE  | ATTACK | CAST  | MOVE  | GUARD |  ESC/CANCEL | Row 1 (20px)  
\+------+------+------+--------+-------+-------+-------+-------------+  
|  SW  |  S   |  SE  |  AIM   | USE   | DELAY | OPT\*  | ENTER/CONF  | Row 2 (20px)  
\+-------------------------------------------------------------------+ (319,239)

Strategy B: 480×320 Display Layout (3.5" CYD \- Recommended)  
Strategy B leverages the higher-resolution 480×320 display to deliver a 1:1 pixel-perfect native viewport alongside a companion HUD and an expanded control deck.  
(0,0)                 (319,0) (320,0)                        (479,0)  
\+-----------------------+-----+----------------------------------+  
|                       |     | COMPANION DASHBOARD (160x320)    |  
|                       |     \+----------------------------------+  
|                       |     | LIVE MINIMAP (16x16 Grid)        |  
|   UNSCALED VIEWPORT   |     | \[N\]  POS: X:08 Y:12  DIR: EAST   |  
|      (320 x 200\)      |     \+----------------------------------+  
|                       |     | PARTY STATUS & QUICK HP BARS     |  
| \- 3D Maze View        |     | 1\. Paladin   \[35/35\] \[==========\]|  
| \- Tactical Combat     |     | 2\. Fighter   \[28/28\] \[==========\]|  
| \- Original UI Menus   |     | 3\. Cleric    \[18/22\] \[========  \]|  
|                       |     | 4\. Mage      \[12/12\] \[==========\]|  
|                       |     \+----------------------------------+  
|                       |     | ACTIVE BUFFS: BLESS, HASTE       |  
(0,199)                 |     \+----------------------------------+  
(0,200)                 |     | QoL TOUCH BUTTONS                |  
\+-----------------------+     | \[ AUTO-FIX \]       \[ JOURNAL \]   |  
| TOUCH CONTROL DECK    |     |                                  |  
|      (320 x 120\)      |     |                                  |  
\+-----------------------+-----+----------------------------------+ (479,319)  
(0,319)               (319,319)

Coordinate Bounds & Subsystem Mapping

1. **Unscaled Viewport ((0,0) to (319,199) \[320×200\]):** Native game screen rendering 3D dungeon walls, story screens, and combat grids without graphical scaling artifacts.  
2. **Companion Dashboard ((320,0) to (479,319) \[160×320\]):** Dedicated Gold Box Companion HUD featuring:  
   * Real-time 16×16 live minimap with directional player arrow (▲ ► ▼ ◄).  
   * Real-time party HP status bars (color-coded Green $\rightarrow$ Yellow $\rightarrow$ Red).  
   * Active spell indicators (`BLESS`, `HASTE`, `POISON`).  
   * One-tap QoL buttons (`AUTO-FIX`, `JOURNAL`).  
3. **Bottom Control Deck ((0,200) to (319,319) \[320×120\]):** Expanded touch input deck.

Touch Hitbox Math & Grid Resolution  
Touch inputs registering within $Y\geq 200$ are translated into local deck coordinates:

$DeckX=TouchX,DeckY=TouchY-200$

* **2×5 Pad Grid (64×60px Targets):** Used for primary combat and 3D navigation modes:

\$\$\\text{Column} \= \\lfloor \\text{DeckX} / 64 \\rfloor, \\quad \\text{Row} \= \\lfloor \\text{DeckY} / 60 \\rfloor, \\quad \\text{Index} \= (\\text{Row} \\times 5\) \+ \\text{Column}\$\$

* **3×6 Pad Grid (53×40px Targets):** Used for fine-grained list navigation and inventory management:

\$\$\\text{Column} \= \\min(5, \\lfloor \\text{DeckX} / 53 \\rfloor), \\quad \\text{Row} \= \\lfloor \\text{DeckY} / 40 \\rfloor, \\quad \\text{Index} \= (\\text{Row} \\times 6\) \+ \\text{Column}\$\$  
---

5\. Architectural Breakdown of Completed C++ Submodules & Master State Loop  
5.1 Submodule Deep Dives  
1\. `Character.h` (Character Record Parser)

* **Design & Data Structure:** Parses binary `.CHA` character records into C++ memory layout structs.  
* **Memory Structure:**

// File: src/Character.h  
\#ifndef CHARACTER\_H  
\#define CHARACTER\_H

\#include \<Arduino.h\>

struct CharacterStats {  
  uint8\_t str;  
  uint8\_t strExt; // Percentile strength (18/01 to 18/00 \= 1 to 100\)  
  uint8\_t intel;  
  uint8\_t wis;  
  uint8\_t dex;  
  uint8\_t con;  
  uint8\_t cha;  
};

class Character {  
public:  
  char name\[16\];  
  CharacterStats stats;  
  uint8\_t race;  
  uint8\_t charClass;  
  uint8\_t level;  
  int16\_t currentHP;  
  int16\_t maxHP;  
  int8\_t  thac0;  
  int8\_t  armorClass;  
  uint32\_t experiencePoints;  
  uint32\_t goldPieces;  
    
  bool loadFromBuffer(const uint8\_t\* buf) {  
    memcpy(name, buf, 15);  
    name\[15\] \= '\\0';  
    stats.str    \= buf\[15\]; stats.strExt \= buf\[16\];  
    stats.intel  \= buf\[17\]; stats.wis    \= buf\[18\];  
    stats.dex    \= buf\[19\]; stats.con    \= buf\[20\]; stats.cha \= buf\[21\];  
    race         \= buf\[22\]; charClass    \= buf\[23\]; level     \= buf\[24\];  
    currentHP    \= buf\[25\] | (buf\[26\] \<\< 8);  
    maxHP        \= buf\[27\] | (buf\[28\] \<\< 8);  
    thac0        \= (int8\_t)buf\[29\];  
    armorClass   \= (int8\_t)buf\[30\];  
    experiencePoints \= buf\[31\] | (buf\[32\] \<\< 8\) | (buf\[33\] \<\< 16\) | (buf\[34\] \<\< 24);  
    goldPieces       \= buf\[35\] | (buf\[36\] \<\< 8\) | (buf\[37\] \<\< 16\) | (buf\[38\] \<\< 24);  
    return true;  
  }  
};  
\#endif

2\. `SoundEngine.h` (Software PWM Audio Synthesizer)

* **Design & Data Structure:** Generates square-wave synthesized audio on GPIO 26 using ESP32 LEDC PWM channel 0\.  
* **Operational Logic:** Attenuates output power by scaling duty cycle from $0$ to $128$ (\$50\\%\$ duty cycle peak power) using $duty=(volume\times 128)/100$.  
* **Exact Frequency Profiles:**  
  * `SND_STEP`: 150 Hz square wave for 20 ms.  
  * `SND_HIT`: Frequency sweep from 800 Hz down to 150 Hz in steps of \-50 Hz with a 5 ms step delay.  
  * `SND_MISS`: Frequency sweep from 300 Hz up to 700 Hz in steps of \+40 Hz with a 4 ms step delay.  
  * `SND_SPELL`: Arpeggio sequence of C5 (523 Hz), E5 (659 Hz), G5 (784 Hz), C6 (1046 Hz), 30 ms per tone.

// File: src/SoundEngine.h  
\#ifndef SOUND\_ENGINE\_H  
\#define SOUND\_ENGINE\_H

\#include \<Arduino.h\>

\#define SPEAKER\_PIN 26  
\#define LEDC\_CHANNEL 0

enum SoundEffect { SND\_CLICK, SND\_STEP, SND\_HIT, SND\_MISS, SND\_SPELL, SND\_REST, SND\_VICTORY };

class SoundEngine {  
private:  
  uint8\_t volume; // 0 to 100%  
  bool muted;

public:  
  SoundEngine() : volume(75), muted(false) {}

  void init() {  
    pinMode(SPEAKER\_PIN, OUTPUT);  
    ledcSetup(LEDC\_CHANNEL, 2000, 8);  
    ledcAttachPin(SPEAKER\_PIN, LEDC\_CHANNEL);  
    stop();  
  }

  void stop() { ledcWrite(LEDC\_CHANNEL, 0); }

  void playToneRaw(unsigned int freq) {  
    if (muted || volume \== 0 || freq \== 0\) { stop(); return; }  
    ledcWriteTone(LEDC\_CHANNEL, freq);  
    uint32\_t duty \= (volume \* 128\) / 100;  
    ledcWrite(LEDC\_CHANNEL, duty);  
  }

  void playTone(unsigned int freq, unsigned long duration) {  
    playToneRaw(freq);  
    delay(duration);  
    stop();  
  }

  void play(SoundEffect effect) {  
    if (muted || volume \== 0\) return;  
    switch (effect) {  
      case SND\_CLICK: playTone(1200, 15); break;  
      case SND\_STEP:  playTone(150, 20); break;  
      case SND\_HIT:  
        for (int f \= 800; f \>= 150; f \-= 50\) { playToneRaw(f); delay(5); }  
        stop(); break;  
      case SND\_MISS:  
        for (int f \= 300; f \<= 700; f \+= 40\) { playToneRaw(f); delay(4); }  
        stop(); break;  
      case SND\_SPELL:  
        for (int i \= 0; i \< 3; i++) {  
          playTone(523, 30); playTone(659, 30); playTone(784, 30); playTone(1046, 30);  
        }  
        break;  
      case SND\_REST:    playTone(440, 80); playTone(330, 80); playTone(220, 120); break;  
      case SND\_VICTORY: playTone(523, 100); playTone(659, 250); playTone(784, 350); break;  
    }  
  }  
};  
\#endif

3\. `MapEngine.h` (16×16 Dungeon Grid Engine)

* **Design & Struct Definitions:** Manages a 16×16 array of 6-byte `MapCell` structures loaded directly from binary `.GEO` map archives.

// File: src/MapEngine.h  
\#ifndef MAP\_ENGINE\_H  
\#define MAP\_ENGINE\_H

\#include \<Arduino.h\>

enum Direction { DIR\_NORTH \= 0, DIR\_EAST \= 1, DIR\_SOUTH \= 2, DIR\_WEST \= 3 };

struct MapCell {  
  uint8\_t wallNorth; // 0 \= Clear, 1+ \= Wall Texture ID  
  uint8\_t wallEast;  
  uint8\_t wallSouth;  
  uint8\_t wallWest;  
  uint8\_t doorFlags; // Bit 0: Door Present, Bit 1: Open, Bit 2: Locked  
  uint8\_t eventID;   // ECL Script Trigger ID  
};

class MapEngine {  
public:  
  MapCell grid\[16\]\[16\];  
  uint8\_t partyX;  
  uint8\_t partyY;  
  Direction partyDir;

  uint8\_t getWallInDirection(uint8\_t x, uint8\_t y, Direction dir) {  
    if (x \> 15 || y \> 15\) return 1; // Boundary wall  
    switch (dir) {  
      case DIR\_NORTH: return grid\[x\]\[y\].wallNorth;  
      case DIR\_EAST:  return grid\[x\]\[y\].wallEast;  
      case DIR\_SOUTH: return grid\[x\]\[y\].wallSouth;  
      case DIR\_WEST:  return grid\[x\]\[y\].wallWest;  
    }  
    return 0;  
  }  
};  
\#endif

4\. `Viewport3D.h` / `Graphics.h` (EGA/VGA Unpacker & 3D Painter's Algorithm)

* **Design & Bitwise Mechanics:**  
  * **4-bit EGA Decoding (POOLRAD, CURSE, SILVER):** Two pixels are packed per byte. The high nibble `(pixelByte >> 4) & 0x0F` and low nibble `pixelByte & 0x0F` index into a 16-color RGB565 table (`egaPalette[16]`).  
  * **8-bit VGA Decoding (POOLDARK):** Bytes map 1-to-1 to a 256-color lookup array (`uint16_t vgaPalette[256]`), consuming a 512-byte table in SRAM.  
* **Rendering Pipeline:** Renders 3D perspective back-to-front using the **Painter's Algorithm** (distances 3 down to 0), overlaying side walls and center corridors.

      \[ Distance 3 \]     (Rendered First)  
       \[ Distance 2 \]  
       \[ Distance 1 \]  
       \[ Distance 0 \]     (Rendered Last / Overlaid)

// File: src/Graphics.h  
\#ifndef GRAPHICS\_H  
\#define GRAPHICS\_H

\#include \<Arduino.h\>  
\#include \<LovyanGFX.hpp\>

const uint16\_t egaPalette\[16\] \= {  
  0x0000, 0x0015, 0x0540, 0x0555, 0xA800, 0xA815, 0xAAA0, 0xAD55,  
  0x52AA, 0x52BF, 0x57EA, 0x57FF, 0xF800, 0xF815, 0xFFE0, 0xFFFF  
};

class GraphicsEngine {  
public:  
  void unpackEgaFrame(const uint8\_t\* rawData, LGFX\_Sprite\* sprite, int x, int y, int w, int h) {  
    sprite-\>setPalette(egaPalette, 16);  
    int idx \= 0;  
    for (int py \= 0; py \< h; py++) {  
      for (int px \= 0; px \< w; px \+= 2\) {  
        uint8\_t b \= rawData\[idx++\];  
        uint8\_t left  \= (b \>\> 4\) & 0x0F;  
        uint8\_t right \= b & 0x0F;  
        if (left \!= 0\)  sprite-\>drawPixel(x \+ px,     y \+ py, left);  
        if (right \!= 0\) sprite-\>drawPixel(x \+ px \+ 1, y \+ py, right);  
      }  
    }  
  }  
};  
\#endif

5\. `EclEngine.h` (Event Control Language VM)

* **Design & VM Pipeline:** Virtual machine executing compiled SSI ECL bytecode. Manages a Program Counter (`pc`), text buffers, and conditional input states (`waitingForInput`, `lastInputResult`).  
* **Core Opcode Table:**  
  * `0x00` (`ECL_END`): Halts script execution.  
  * `0x01` (`ECL_PRINT_TEXT`): Reads null-terminated string, applies word-wrapping to 320×200 viewport, awaits input.  
  * `0x02` (`ECL_PROMPT_YES_NO`): Displays modal YES/NO touch targets.  
  * `0x03` (`ECL_JUMP_IF_FALSE`): Branches program counter (`pc += offset`) if `lastInputResult` is false.  
  * `0x04` (`ECL_GIVE_ITEM`): Appends item ID to party inventory array.

6\. `SpellEngine.h` (AD\&D Magic Rules)

* **Design & Data Structure:** Database of AD\&D 1st Edition spells (`Magic Missile`, `Cure Light Wounds`, `Sleep`, `Fireball`). Handles targeting types (`TARGET_SINGLE_ENEMY`, `TARGET_SINGLE_ALLY`, `TARGET_AOE_GRID`), spell slots, range limits, and damage/healing formulas ($1d4+1$ for *Magic Missile*, $1d8$ for *Cure Light*, $3d6$ for *Fireball*).

7\. `CombatEngine.h` (Tactical Grid Engine & Ruleset Math)

* **Design & Struct Definitions:** Renders top-down 11×11 cell combat grids (17×17px cells). Maintains turn queues sorted by AD\&D 1e rules where initiative is sorted in **ascending order** (lower rolls act earlier in turn order).

// File: src/CombatEngine.h  
\#ifndef COMBAT\_ENGINE\_H  
\#define COMBAT\_ENGINE\_H

\#include \<Arduino.h\>

struct Enemy {  
  char name\[16\];  
  int16\_t hp;  
  int16\_t maxHp;  
  int8\_t  ac;  
  int8\_t  thac0;  
  uint8\_t x;  
  uint8\_t y;  
  bool    alive;  
};

struct TurnRecord {  
  bool    isEnemy;  
  uint8\_t index;  
  uint8\_t roll; // 1d10 \+ Dex mod. Ascending sort order\!  
};

class CombatEngine {  
public:  
  Enemy enemies\[6\];  
  TurnRecord turnOrder\[12\];  
  uint8\_t totalCombatants;

  uint8\_t calculateChebyshevDistance(uint8\_t x1, uint8\_t y1, uint8\_t x2, uint8\_t y2) {  
    return max(abs((int)x1 \- (int)x2), abs((int)y1 \- (int)y2));  
  }

  bool resolveAttack(int8\_t thac0, int8\_t targetAC, int8\_t& damageOut) {  
    int roll \= random(1, 21); // d20  
    if (roll \== 20 || (roll \+ targetAC \>= thac0)) {  
      damageOut \= random(1, 9); // 1d8 weapon damage  
      return true;  
    }  
    damageOut \= 0;  
    return false;  
  }  
};  
\#endif

8\. `SaveManager.h` (NVS Persistence)

* **Design & Logic:** Interfaces with ESP32 Non-Volatile Storage (NVS) via `Preferences.h` under the `goldbox_save` namespace. Serializes party counts, location coordinates (`pX`, `pY`, `pDir`), stats, levels, gold, and HP into persistent keys (`c0_hp`, `c0_str`).

9\. `CampMenu.h` (Encampment State Machine)

* **Design & Logic:** State machine for `CAMP_MAIN`, `CAMP_VIEW_CHARACTER`, `CAMP_INVENTORY`, and `CAMP_REST`. Handles equipment changes, updates dynamic AC and THAC0 calculations via `InventoryManager`, and provides one-tap rest/healing functions.

10\. `Minimap.h` (HUD Auto-Map Module)

* **Design & Tile Math:** Renders a 16×16 cell dungeon grid at $8\times 8$ pixels per tile, producing a $128\times 128$ pixel box embedded within the `sprDashboard` canvas at offsets `(16, 60)`. Plots visited walls, open doors, and the directional party vector arrow (▲ ► ▼ ◄).

---

5.2 Master Loop State Machine & LovyanGFX Multi-Sprite Double Buffering (`main.cpp`)  
To prevent flickering and screen tearing during frame updates, the engine allocates three off-screen 8-bit indexed canvas sprites in SRAM:

1. `sprViewport` (320×200 pixels, 8-bit depth \= **64.0 KB RAM**)  
2. `sprDashboard` (160×320 pixels, 8-bit depth \= **51.2 KB RAM**)  
3. `sprControlDeck` (320×120 pixels, 8-bit depth \= **38.4 KB RAM**)

**Total Framebuffer SRAM Allocation:** Exactly 153.6 KB.  
      RAM CANVASES (153.6 KB Total)                PHYSICAL LCD PANEL  
\+-------------------------+------------------+     \+-------------------+  
| sprViewport (64.0 KB)   | sprDashboard     |     | (0,0)             |  
| 320x200 Indexed Canvas  | (51.2 KB)        |     |  Viewport  | Dash |  
\+-------------------------+ 160x320 Canvas   | \--\> |------------| board|  
| sprControlDeck (38.4 KB)|                  |     |  Control   |      |  
| 320x120 Indexed Canvas  |                  |     |  Deck      |      |  
\+-------------------------+------------------+     \+-------------------+ (480,320)

Master Execution Pipeline (main.cpp)  
// File: src/main.cpp  
\#include \<Arduino.h\>  
\#define LGFX\_USE\_V1  
\#include \<LovyanGFX.hpp\>

\#include "MapEngine.h"  
\#include "Viewport3D.h"  
\#include "EclEngine.h"  
\#include "CombatEngine.h"  
\#include "CampMenu.h"

// Instantiate LovyanGFX Device & Sprites  
lgfx::LGFX lcd;  
LGFX\_Sprite sprViewport(\&lcd);  
LGFX\_Sprite sprDashboard(\&lcd);  
LGFX\_Sprite sprControlDeck(\&lcd);

EclEngine    eclEngine;  
CombatEngine combatEngine;  
CampMenu     campMenu;  
MapEngine    mapEngine;  
Viewport3D   viewport3D;

bool inCombatMode \= false;  
bool inCampMode   \= false;

void renderFrame() {  
  // 1\. Evaluate Engine Master State & render into off-screen RAM viewport sprite  
  if (eclEngine.isActive()) {  
    eclEngine.render(\&sprViewport);  
  } else if (inCombatMode) {  
    combatEngine.render(\&sprViewport);  
  } else if (inCampMode) {  
    campMenu.render(\&sprViewport);  
  } else {  
    viewport3D.render(\&sprViewport, mapEngine);  
  }

  // 2\. Render Companion Sidebar & Control Deck Sprites  
  renderDashboardSprite(\&sprDashboard, mapEngine);  
  renderControlDeckSprite(\&sprControlDeck, inCombatMode, inCampMode);

  // 3\. Push complete off-screen buffers to physical display coordinates  
  sprViewport.pushSprite(0, 0);          // Top-Left (0,0) \-\> (319,199)  
  sprControlDeck.pushSprite(0, 200);     // Bottom-Left (0,200) \-\> (319,319)  
  sprDashboard.pushSprite(320, 0);       // Right Column (320,0) \-\> (479,319)  
}

---

6\. Integrated Quality of Life (Gold Box Companion) Features  
The engine natively integrates classic "Gold Box Companion" utilities directly into the C++ runtime, eliminating the need for external memory-hacking tools.  
1\. In-Engine Adventurers' Journal Reader  
When an ECL script triggers a journal lookup, the engine reads `JOURNAL.TXT` directly off the microSD card (`/GOLDBOX/POOLRAD/JOURNAL.TXT`), seeks the corresponding entry tag (`[ENTRY XX]`), and opens a formatted, scrollable modal overlay over the 320×200 viewport.  
// File: src/JournalReader.cpp  
\#include \<SD.h\>  
\#include \<LovyanGFX.hpp\>

void displayJournalEntry(int entryNum, LGFX\_Sprite\* viewport) {  
  File journalFile \= SD.open("/GOLDBOX/POOLRAD/JOURNAL.TXT", FILE\_READ);  
  if (\!journalFile) return;

  String tag \= "\[ENTRY " \+ String(entryNum) \+ "\]";  
  if (journalFile.find((char\*)tag.c\_str())) {  
    String entryText \= journalFile.readStringUntil('\[');  
    viewport-\>fillRect(10, 10, 300, 180, 0x000F); // Navy background  
    viewport-\>drawRect(10, 10, 300, 180, 0xFFE0); // Gold border  
    viewport-\>setTextColor(0xFFFF);  
    viewport-\>setCursor(18, 18);  
    viewport-\>printf("--- JOURNAL ENTRY \#%d \---\\n\\n%s", entryNum, entryText.c\_str());  
  }  
  journalFile.close();  
}

2\. One-Tap "AUTO-FIX" Routine  
Tapping **AUTO-FIX** on the Companion HUD triggers an automated loop that calculates party HP deficits, executes available Cleric healing spells (`Cure Light Wounds`), advances world calendar time in 8-hour blocks, and restores the party to 100% HP.  
3\. Auto-Map & HUD Indicators  
Renders a live 16×16 minimap on the right sidebar alongside real-time status pills for active party buffs (`BLESS`, `HASTE`) and debuffs (`POISON`, `DRAIN`).  
4\. Auto-Memorize & Auto-Identify / Ammo Restock

* **Auto-Memorize:** Stores a `favoriteSpells` bitmask per caster, automatically refilling spell slots during rest cycles.  
* **Auto-Identify:** Launcher configuration flag that displays true item names (e.g., *Long Sword \+1*) upon looting.  
* **Auto-Ammo:** Automatically restocks quivers from party inventory reserves during combat.

---

7\. Actionable Development Roadmap for Claude.ai Completion  
To bring the engine from its functional architecture (\~15% complete) to full gameplay parity across all four titles, Claude.ai must execute the following step-by-step implementation plan.  
Task Checklist for Claude.ai

* \[ \] **1\. Authentic .DAX RLE Decoder Expansion & 256-Color VGA Support**  
  * Expand `Graphics.h` to fully parse multi-frame `.DAX` archive headers.  
  * **Header Specification:** Bytes 0–1 store total block length (16-bit little-endian `uint16_t`); Bytes 2–3 store frame dimensions and flags.  
  * **RLE Control Byte Protocol:** If control byte $<0x80$, read next $(controlByte+1)$ as raw uncompressed bytes. If control byte $\geq 0x80$, repeat the following byte $((0x100-controlByte)+1)$ times using two's complement math.  
  * Implement 8-bit direct VGA palette decoding (256-color palette, 512-byte lookup table) for *Pools of Darkness*.  
* \[ \] **2\. Full ECL Bytecode VM Parser**  
  * Expand `EclEngine.h` from basic opcodes to support the full SSI instruction set according to the opcode table below:

| Opcode Hex | Symbol | Execution & State Behavior |
| ----- | ----- | ----- |
| `0x00` | `ECL_END` | Halts script execution, restores viewport to exploration renderer. |
| `0x01` | `ECL_PRINT_TEXT` | Reads null-terminated string, applies word-wrapping to 320×200, awaits input. |
| `0x02` | `ECL_PROMPT_YES_NO` | Displays modal YES/NO touch prompt, sets `lastInputResult`. |
| `0x03` | `ECL_JUMP_IF_FALSE` | Reads offset byte; if `lastInputResult == false`, `pc += offset`. |
| `0x04` | `ECL_GIVE_ITEM` | Reads 16-bit Item ID; appends item to active party inventory. |

* \[ \] **3\. Inventory Subsystem & Equipment Management**  
  * Implement complete item slotting (head, torso, main hand, off hand, rings).  
  * Add weight calculation routines for encumbrance and movement speed drops.  
  * Construct shop UI for buying, selling, and identifying items.  
* \[ \] **4\. AD\&D 1e Progression & Class Rulesets**  
  * Implement level-up experience tables for all races and classes (Cleric, Fighter, Paladin, Ranger, Mage, Thief).  
  * Add multi-class experience splitting and saving throw matrices.  
  * Implement full spell progression tables for Mage and Cleric spells (Levels 1 through 9).  
* \[ \] **5\. Multi-Game Save Transfer Utility**  
  * Build a character parser capable of reading standard `.SAV` / `.CHA` files.  
  * Enable party export and import across titles (`POOLRAD` $\rightarrow$ `CURSE` $\rightarrow$ `SILVER` $\rightarrow$ `POOLDARK`).  
* \[ \] **6\. PlatformIO Build & Deployment Infrastructure**  
  * Configure and verify the VS Code PlatformIO environment.

Reference `platformio.ini` Configuration  
; File: platformio.ini  
\[env:esp32dev\]  
platform \= espressif32  
board \= esp32dev  
framework \= arduino  
monitor\_speed \= 115200  
board\_build.flash\_mode \= qio  
board\_build.f\_flash \= 80000000L

; Automatic dependency resolution for display drivers  
lib\_deps \=   
    lovyan03/LovyanGFX@^1.1.12

Compilation & Flashing Procedures

1. Open VS Code with the PlatformIO IDE extension installed.  
2. Open the project root directory containing `platformio.ini` and `src/main.cpp`.  
3. Connect the CYD board via USB.  
4. Click **Build** (Checkmark icon) on the PlatformIO status bar to compile the firmware.  
5. Click **Upload** (Right Arrow icon) to flash the compiled binary to the ESP32 SPI Flash.  
6. Click **Serial Monitor** (Plug icon) to verify system boot logs at 115200 baud.


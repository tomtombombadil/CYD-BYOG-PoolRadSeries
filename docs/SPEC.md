# CYD BYOG Pool of Radiance Series Engine - Specification

Play SSI's four "Pool of Radiance series" Gold Box games on ESP32 Cheap Yellow
Display boards, from the player's own copy of the games:

1. Pool of Radiance (1988)
2. Curse of the Azure Bonds (1989)
3. Secret of the Silver Blades (1990)
4. Pools of Darkness (1991)

## 1. Bring your own game

- The firmware contains **no** game data, pictures, text or code from SSI.
- The player copies the original **DOS** files of each game from their own copy
  (e.g. the GOG release) into a folder under `/GOLDBOX/` on a FAT32 microSD card.
- Folder names are free; the game is recognised from words in the name
  (`POOLRAD`, `Pool of Radiance`, `CURSE`, `AZURE`, `SILVER`, `BLADES`, `DARK...`) -
  `src/engine/games.*`. A folder with DAX files and no known name still shows up.
- The engine reads the files where they are; nothing is converted or copied off
  the card.

## 2. How the games run: reimplementation, not emulation

The DOS games need an 8086 PC with 640 KB of memory. A CYD has ~300 KB of RAM in
all and no PSRAM, so emulating DOS (as DOSBox does) is out. The engine is a
**reimplementation**: it reads the games' data files (pictures, maps, scripts,
monsters, items) and does what the original program did with them - like ScummVM
does for adventure games. The script language (ECL) carries most of each game's
story and logic, so one engine with per-game differences can run all four.

Reference for how the original program behaves: the C# reimplementation of
Curse of the Azure Bonds, [coab](https://github.com/simeonpilgrim/coab), and
published format notes. coab has **no license**: read it to learn formats and
rules, never copy its code (see CLAUDE.md, Licensing).

## 3. Hardware

Boards (from CYD-Classic-Games; landscape for this engine):

| Board | Panel | Landscape size | SD | Touch |
|---|---|---|---|---|
| 2.8" ESP32-2432S028, ILI9341 or ST7789 | 240x320 | 320x240 | VSPI 18/19/23/5 | XPT2046, **software SPI** on 25/32/39/33 (IRQ 36) |
| 3.2" ESP32-32E, ST7789 | 240x320 | 320x240 | VSPI | XPT2046 on the display's HSPI bus |
| 3.5" ESP32-32E, ST7796 | 320x480 | 480x320 | VSPI | XPT2046 on HSPI |
| 4.0" ESP32-32E, ST7796 | 320x480 | 480x320 | VSPI | XPT2046 on HSPI |

- The 2.8" board's touch chip and SD slot both need the VSPI controller, and
  LovyanGFX's touch driver only does hardware SPI. So the 2.8" reads touch by
  bit-banging (`src/boards/touch_xpt2046_soft.hpp`, an `lgfx::ITouch`, so
  calibration and rotation still work) and the SD card gets VSPI.
- Memory budget: one **64,000-byte canvas** (320x200 palette indexes, like the
  EGA/VGA screen) plus a 7.5 KB band buffer for converting it to panel colours
  (`src/ui/frame.*`). DAX blocks are **streamed** while decoding (`dax::RleReader`)
  so a picture never needs its whole block in RAM.
- Storage: huge_app partition table (3 MB app, ~900 KB LittleFS for settings,
  touch calibration and later saves). Game files only on SD.

## 4. Screen layout

The games draw a 320x200 screen. The canvas is presented:

- **320x240 panels**: 1:1 at the top; the 40 rows below are free for controls.
- **480x320 panels**: either **1.5x** (480x300, fills the panel, 20 rows left)
  or **1:1** centred with room for a companion panel and bigger controls.
  Settings -> "Game Screen: 1.5x / 1:1" switches it today so Tom can compare on
  the 3.5" / 4.0" (open decision 1).

## 5. File formats (status)

| What | Where | Status |
|---|---|---|
| DAX archive: u16 index size, 9-byte entries (id, u32 offset, u16 unpacked, u16 packed), RLE blocks (control `c >= 0`: copy `c+1` bytes; `c < 0`: repeat next byte `-c` times) | `src/engine/dax.*` | Done, host-tested. Confirm on real files (M1). |
| EGA picture block: u16 height, u16 width in 8-px columns, u16 x, u16 y, u8 frames, 8 unknown bytes, then nibble-packed pixels (high first) | `src/engine/picture.*` | Done for Curse of the Azure Bonds' layout. Check PoR and Silver Blades on real files (M1). |
| VGA pictures (Pools of Darkness) | - | To do: different format, 256 colours. |
| GEO map block: 2-byte header + four 256-byte planes (wall types as nibbles, a byte per cell, 2 bits per direction) | - | M3. |
| WALLDEF (5 x 156-byte wall sets) + 8X8D tile sets | - | M3. |
| ECL script: 65 opcodes 0x00-0x40, packed text | - | M4. |
| Characters, items, monsters, saves | - | M5-M6. Differ per game. |

## 6. Milestones

1. **Library and asset viewer** (v0.1.0, this build). Finds the games on the
   card, lists DAX files and blocks, draws EGA pictures (all frames), hex-dumps
   the rest. Proves: SD on every board (incl. the 2.8" with software touch),
   the DAX and picture decoders on Tom's real files, 1:1 vs 1.5x on 480x320.
2. **The game's look**: its font, screen frame and title screens; text windows.
3. **3D view and walking**: GEO + WALLDEF + 8X8D drawing, turning and stepping by
   touch, auto-map.
4. **ECL script engine**: events, text, menus, pictures, area changes.
5. **Party**: characters (create / load), inventory, camp, shops.
6. **Combat.**
7. **All four games**: the per-game differences (PoD's VGA graphics), party
   transfer between games.
8. **Sound and companion features** (auto-map panel, journal entry lookup,
   rest-until-healed, re-memorise spells - as options).

## 7. Open decisions (Tom's)

1. **480x320 layout**: game 1.5x full screen, or 1:1 with a companion panel
   (auto-map + party HP) and bigger controls? Try both in the viewer.
2. **First game**: Pool of Radiance (the series' start), or Curse of the Azure
   Bonds first (its engine is the best documented, through coab) and then back
   to PoR?
3. **Controls**: tap the game's own menu words on screen (they are small 8x8
   text at 1:1), on-screen keys in the free rows, or both.
4. **Journal**: the games say "read journal entry N". Show just the number (the
   player reads their own journal / PDF), or optionally read entries from a
   text file the player puts on the card themselves?
5. **Copy protection** (code wheel / journal word checks): skip it, since the
   player must already own the game files?

## 8. Engine UI rules (carried over from CYD-Classic-Games)

- Big targets, stylus taps (Tom uses a DS Lite stylus, firm presses). A tap acts
  on release, at the point where the stylus came down. No drags or swipes.
- No "are you sure" confirmations. Title Case on keys and headings.
- Layout always from the panel size, never fixed for one board.
- Colours of the engine's own screens from `src/ui/style.h` (dark: black, navy
  keys, cream text, gold accents).

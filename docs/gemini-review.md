# Review of the Gemini starting files (2026-10-06)

Gemini wrote 11 source files (`main.cpp` and 10 headers) and a "Developer & Handoff
Guide" for this project. All of them are kept unchanged in `docs/archive/gemini/`
for reference. **None of them is built into the firmware.** This page explains why,
and what was kept as ideas.

## Verdict

The files are a mock-up of a generic dungeon crawler, not a starting point for
running the Gold Box games. The original game files are never read: the map is
hard-coded, the walls are wire-frame rectangles, the "goblins" and spells are
invented, and the script engine has 3 made-up opcodes. The guide's description
of the file formats is mostly wrong too (checked against a working
reimplementation of Curse of the Azure Bonds), and several hardware claims
don't hold for the boards Tom has. Building on it would mean rewriting every
file anyway, so the project starts from Tom's proven CYD-Classic-Games board
code and a format layer checked against real data. The guide's "~15 %
complete" really means 0 % of the engine.

## The code, file by file

| File | What it does | Problem |
|---|---|---|
| `main.cpp` | Mode switch: explore / script / combat / camp / map | The `LGFX` display class is empty (it says "insert your config here"), so it can't run. Every coordinate is hard-coded for one 320x240 layout. A 15 % random encounter fires a fake script. |
| `MapEngine.h` | 16x16 grid where a cell *is* a wall | Gold Box walls sit on the cell *edges* (one per direction, with doors), from GEO blocks. Wrong model. |
| `Viewport.h` | 4 nested rectangles as a 3D view | The game draws its walls from 8x8 tile sets (`8X8Dn.DAX`) placed by wall-definition tables (`WALLDEFn.DAX`) into an 88x88 window. Nothing of that. |
| `ECLEngine.h` | 3 opcodes: END, PRINT, START COMBAT | Real ECL has 65 opcodes (0x00-0x40) - see below - plus game variables and packed text. Its 0x02 is GOSUB. |
| `CombatEngine.h` | 11x11 grid, 3 hard-coded goblins | Dead party members keep taking turns (`isCombatantAlive` returns true for every party member); initiative sorts the opposite way from the guide; THAC0 and damage are invented. Monsters come from `MONnCHA.DAX` in the real games. |
| `SpellEngine.h` | 4 spells with made-up numbers | Spells are the games' own rules and data. |
| `Character.h` | Generic stats struct | Each game has its own character record format. |
| `SaveManager.h` | Saves HP and gold to NVS (Preferences) | NVS is a ~20 KB partition, too small for a real save; saves belong on SD / LittleFS in a defined format. |
| `CampMenu.h` | Rest = instant full heal | Placeholder. |
| `SoundEngine.h` | Beeps | Blocks the whole program with `delay()` while a sound plays, and uses `ledcSetup`/`ledcAttachPin`, which Arduino-ESP32 3.x removed. CYD-Classic-Games has a timer-driven speaker driver to reuse instead. |
| `MiniMap.h` | Auto-map of visited cells | Good idea, wrong map model. |

## The guide's claims, checked

| Claim | Fact |
|---|---|
| Files `START.DAX`, `WALL.DAX`, `CHRDATA.DAX`, `.GEO`, `.TLB`, `.CHA`, `JOURNAL.TXT` | Not how the games are laid out. The DOS games use numbered DAX archives per area: `GEOn.DAX` (maps), `ECLn.DAX` (scripts), `WALLDEFn.DAX` + `8X8Dn.DAX` (3D walls), `MONnCHA/SPC/ITM.DAX` (monsters), `ITEMn.DAX`, and picture archives. There is no `.TLB` or journal file. |
| DAX header: "bytes 0-1 block length, 2-3 frame size" | The file starts with the index size (u16), then 9-byte entries: id (u8), offset (u32), unpacked size (u16), packed size (u16). Implemented in `src/engine/dax.*`. |
| RLE repeat count `(0x100 - c) + 1` | Off by one: a negative control byte `c` repeats the next byte `-c` times. |
| GEO = 16x16 cells of 6 bytes | A GEO block is 0x402 bytes: a 2-byte header, then four 256-byte planes - wall types as nibbles (two directions per byte, two planes), a byte per cell, and 2 bits per direction (most likely doors - to be confirmed). |
| ECL opcodes 0x02 YES/NO, 0x03 JUMP IF FALSE, 0x04 GIVE ITEM | 0x00 EXIT, 0x01 GOTO, 0x02 GOSUB, 0x03 COMPARE, 0x04-0x07 ADD/SUBTRACT/DIVIDE/MULTIPLY, 0x08 RANDOM, 0x09 SAVE, ... 0x11 PRINT, ... 0x24 COMBAT, ... 0x40 DESTROY ITEMS (65 in all). |
| In-engine reader for `JOURNAL.TXT` | The Adventurer's Journal was a printed booklet (and partly copy protection); the game only says "read entry 23". Shipping its text would copy SSI's work. Options are in SPEC.md. |
| 2.8" board: touch and SD share VSPI at 20 MHz, "isolation eliminates contention" | On the ESP32-2432S028 touch and SD sit on *different pins* that both need the one VSPI controller, and LovyanGFX's touch driver needs hardware SPI - so the SD card simply didn't work (CYD-Classic-Games has `BOARD_SD_USABLE 0` there). This engine needs the card, so the 2.8" now reads touch in software (`src/boards/touch_xpt2046_soft.hpp`). |
| 3.5" board = ESP32-3248S035 | Tom's 3.5" and 4.0" boards are "ESP32-32E" display boards with a different pinout (backlight 27, touch on the display bus). Using the 3248S035 pins would not work. |
| ~300 KB usable heap; three 8-bit sprites = 153.6 KB | In practice ~140-200 KB is free with the largest single block around 75-110 KB. This engine uses one 64,000-byte canvas (320x200 palette indexes) and converts it to panel colours a band of rows at a time (`src/ui/frame.*`). |
| EGA palette table | Light red, light magenta and yellow are wrong (e.g. light red is 0xFAAA, not 0xF800). `src/engine/picture.cpp` has the standard values. |
| "~15 % complete" | None of the real formats or rules was implemented. |

The research behind the corrections: the C# reimplementation of Curse of the
Azure Bonds ([coab](https://github.com/simeonpilgrim/coab), Simeon Pilgrim) was
read to learn the formats. It has no license, so **none of its code is copied**;
only the facts about the file formats are used, written in our own code.

## Kept as ideas

- The mode split (explore, scripted event, combat, camp, auto-map) - it matches
  how the original games are organised.
- A companion panel on 480x320 boards: auto-map, party hit points, active
  spells. Worth trying against showing the game screen 1.5x (SPEC.md, open
  decision 1).
- Quality-of-life helpers (auto-map, journal entry lookup, rest-until-healed,
  re-memorise spells) - as options, since some change the game's balance.
- Decoding pictures by streaming the RLE data straight into the screen through
  a palette table.

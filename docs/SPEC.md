# CYD BYOG Pool of Radiance Series Engine - Specification

Play SSI's four "Pool of Radiance series" Gold Box games on ESP32 Cheap Yellow
Display boards, from the player's own copy of the games:

1. Pool of Radiance (1988)
2. Curse of the Azure Bonds (1989)
3. Secret of the Silver Blades (1990)
4. Pools of Darkness (1991)

## 1. Bring your own game

Names used throughout (Tom, 2026-10-06): **PoolRad**, **Curse**, **Secret**,
**Darkness**. The target files are the **GOG releases** (the DOS game files
inside the GOG install, and the PDFs that come with them).

- The firmware contains **no** game data, pictures, text or code from SSI.
- The player copies the original **DOS** files of each game from their own copy
  (the GOG release) into a folder under `/GOLDBOX/` on a FAT32 microSD card -
  suggested names `POOLRAD`, `CURSE`, `SECRET`, `DARKNESS`. A whole GOG
  install folder copied as it is also works: if the folder has no DAX files,
  the engine looks one folder down.
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
- **480x320 panels** (Tom, 2026-10-06): **1:1, never scaled**, with a
  **Gold Box Companion panel** beside it and controls below. The 160x320 strip
  on the right holds the auto-map, party hit points and active spells; the
  320x120 area under the game screen holds the controls. The viewer's
  Settings -> "Game Screen: 1.5x" stays for now as a viewer-only experiment;
  the game screens won't use it.

### Controls (Tom, 2026-10-06; mockup `docs/controls-proposal.png`)

- **Tap what the game shows.** A menu word on the game screen ("AREA CAST
  VIEW ENCAMP ...") = pressing its key; a line in a list = choosing it (and
  Enter); a square on the combat map = stepping / aiming toward it; "press
  any key" prompts = tap the game screen.
- **Keys only for what isn't on screen**, changing with what the game does:
  - 320x240: one row of 8 square keys (~37x34) in the 40 rows under the game.
    Exploring: Turn Left, Side-step Left, Forward, Side-step Right, Turn
    Right, Turn Around, Esc, and a Companion key (auto-map, journal, party
    on a full-screen page). Combat: the 8 direction arrows.
  - 480x320: the 320x120 area under the game holds a 3x3 pad laid out like a
    numeric keypad (Tom): exploring 7 = Turn Left, 8 = Forward, 9 = Turn
    Right, 4 = Side-step Left, 6 = Side-step Right, 2 = Turn Around; combat:
    all 8 directions. Beside it Enter / Esc / Keys. The companion panel has
    Journal and Map.
- **Side-steps** (Tom remembers them from the games). coab's Curse code only
  has forward, turn left / right and turn around on the arrow keys (numpad
  4 / 6 send the same keys as the arrows), so for Curse at least the engine
  adds them: one square left or right of the facing, without turning, with
  the same wall / door / event checks as a normal step into that square (as
  if the party turned, stepped and turned back). If a game has its own
  side-step, use its behaviour.
- Typed text (names, numbers): an on-screen keyboard until it's done.

## 5. File formats (status)

Checked against Tom's GOG files of all four games on 2026-10-06 with
`tools/dax_tool/dax_inspect` (every DAX file, every block). The games' files
stay in the session's scratch space, never in the repo.

| What | Where | Status |
|---|---|---|
| DAX archive: u16 index size, 9-byte entries (id, u32 offset, u16 unpacked, u16 packed), RLE blocks (control `c >= 0`: copy `c+1` bytes; `c < 0`: repeat next byte `-c` times) | `src/engine/dax.*` | **Confirmed**: every DAX file of all four games reads (3,661 blocks). |
| EGA picture: u16 height, u16 width in 8-px columns, u16 x, u16 y, u8 frames, 8 unknown bytes, nibble-packed pixels (high first); the frames fill the block exactly | `src/engine/picture.*` | **Confirmed** on PoolRad (600), Curse (477), Secret (353): TITLE, BIGPIC, HEAD, BODY, CBODY, CHEAD, COMSPR, CPIC, SKY, 8X8D tile sets (70 tiles each), DUNGCOM / WILDCOM / RANDCOM, TILES. |
| EGA animation (PICn, FINALn, SPRITn): u8 frames, then per frame u32 delay, u16 height, u16 width cols, u16 x, u16 y, u8, 8 unknown bytes, packed pixels; PIC / FINAL frames after the first XORed with the first | `src/engine/picture.*` | **Confirmed** on PoolRad (183), Curse (102), Secret (72); frames checked by eye. SPRIT blocks = 3 frames: near / middle / far. |
| VGA picture (Darkness): u8 height, u8 width cols, u16 x, u16 y, u8 frames, u8, u8 first palette index, u8 count-1, palette (6-bit RGB), one EGA nibble per entry, 4 bytes, then 1 byte a pixel | `src/engine/picture.*` | **Confirmed** on 479 Darkness blocks: TITLE, BIGPIC, BACK, SKYGRND, CBODY, CHEAD, COMSPR, CPIC, DUNGCOM, WILDCOM, BORDERS, CURSOR. |
| VGA animations (Darkness PIC1, SPRIT1) and 8X8D1 tile sets | - | To do: header starts u16 height, u16 width cols, u16 x, u16 y, then a different layout. Darkness is last in the build order. |
| Font: 8X8D1.DAX block 201 (1416 bytes in all four games) | - | M2. |
| GEO map block (1026 bytes): 2-byte header + four 256-byte planes (wall types as nibbles, a byte per cell, 2 bits per direction) | - | M3. |
| WALLDEF (780 bytes a wall set) + 8X8D tile sets | - | M3. |
| ECL script: 65 opcodes 0x00-0x40, packed text | - | M4. |
| Monsters (MONnCHA: 285 B PoolRad, 422 B Curse, 439 B Secret, 510 B Darkness), items, characters, saves | - | M5-M6. Record sizes differ per game - per-game profiles. |

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

Order of the games (Tom, 2026-10-06): **Curse of the Azure Bonds first** - its
engine is the best documented (coab), and most of that work carries over -
then **Pool of Radiance**, then Secret of the Silver Blades and Pools of
Darkness. Milestones 2-6 are built against Azure Bonds' files.
8. **Sound and companion features** (auto-map panel, journal entry lookup,
   rest-until-healed, re-memorise spells - as options).

## 7. The journal (Tom, 2026-10-06)

The games tell the player to "read journal entry N" from the printed
Adventurer's Journal. The GOG releases include the journals as PDFs.

What the GOG Curse journal PDF is (checked 2026-10-06; 17 pages, scanned
two-page spreads at 300 dpi, SHA-256 d4712a05...f2f3):
- Its text layer is Acrobat OCR, and the entries are set in a calligraphic
  font: the OCR text is garbled ("6razecl {i!(g, fire") - **unusable**.
- Several entries are **pictures and maps** (Entry 4 is a sewer map, 8 a
  drawing, 9 a symbol), which text would lose anyway.
- Entries run in columns and continue into the next column or page.

So the journal is shown as **pictures of the entries**, not text:
- A **journal converter** run by the player on their own computer opens the
  journal PDF from *their* GOG copy, cuts each entry out of the page images
  using a table of rectangles for that exact PDF (page + position per piece
  - coordinates only, no content; the PDF is recognised by its SHA-256),
  stacks the pieces of an entry into one image, reduces it to 16 colours at
  the panel widths (310 and 470 px wide) and writes the result next to that
  game's files on the SD card (e.g. `/GOLDBOX/CURSE/JOURNAL.BIN`).
- Planned as a page on the flasher site that runs entirely in the browser
  (pdf.js; nothing is uploaded), so players need no install. A mockup at
  320x240 reads well (script text ~9 px x-height).
- The engine shows entry N full-screen with Prev Page / Back / Next Page
  keys when the game asks for it, and from the Journal key. Without the file
  it shows the entry number.
- The repo, the firmware and the flasher site never contain journal text or
  images; tests use made-up entries; the rectangle tables are coordinates
  only. Same rule as the DAX files.
- Each game's journal gets its own table once its GOG PDF has been looked at.

## 8. Copy protection (Tom, 2026-10-06)

Skipped. The player supplies their own game files, so the journal word
checks / code wheel questions are never asked; when the game's code reaches
one, the engine carries on as if it was answered correctly.

## 9. Other Gold Box games (later)

Same engine family, same bring-your-own-game rules: the Krynn games
(Champions, Death Knights, Dark Queen), the Savage Frontier games (Gateway,
Treasures) and Forgotten Realms Unlimited Adventures (FRUA - playing its
user-made designs). The engine keeps everything game-specific behind a
per-game profile from the start, so they can be added to this repo after the
four Pool of Radiance series games. Not started.

First look at Tom's GOG files with `dax_inspect` (2026-10-07):

| Game | Containers | Pictures |
|---|---|---|
| Champions of Krynn | DAX, all blocks read | EGA pictures + animations, all parse (523) |
| Death Knights of Krynn | DAX, all blocks read | EGA pictures + animations, all parse (415) |
| Treasures of the Savage Frontier | DAX, all blocks read | VGA pictures like Darkness (460) |
| Dark Queen of Krynn | **"HLIB" containers** (`.TLB` / `.GLB`, files split over DISK1-3 folders), plus GAME.FON | not looked at yet - a newer engine generation |
| Gateway, FRUA | not checked yet (zips too big to fetch) | |

## 10. Engine UI rules (carried over from CYD-Classic-Games)

- Big targets, stylus taps (Tom uses a DS Lite stylus, firm presses). A tap acts
  on release, at the point where the stylus came down. No drags or swipes.
- No "are you sure" confirmations. Title Case on keys and headings.
- Layout always from the panel size, never fixed for one board.
- Colours of the engine's own screens from `src/ui/style.h` (dark: black, navy
  keys, cream text, gold accents).

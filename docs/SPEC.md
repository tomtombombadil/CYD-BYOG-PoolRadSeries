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
| Font: block 201 of an 8X8D file (8X8D1 PoolRad / Curse, 8X8D5 Secret, 8X8D0 Darkness): 177 glyphs x 8 bytes, bit 7 = left pixel; glyph = upper-case ASCII mod 64, 64+ = symbols and frame bits. PoolRad, Curse, Secret identical; Darkness differs | `src/engine/font.*` | **v0.2.0**, checked on all four games' files; the viewer shows it as a glyph sheet + sample text. |
| Game program (START.EXE; Darkness GAME.EXE): Microsoft **EXEPACK** - MZ header, packed program, at CS:0 an 18-byte header (real IP, CS, mem start, size, SP, SS, dest len in paragraphs, skip len, "RB"); unpacked backwards from the end: command byte, length (high byte, low byte), 0xB0 fill (value byte) / 0xB2 copy, bit 0 = last; the bytes below the last output byte stay as they are | `src/engine/exepack.*` | **v0.3.0**: unpacks every Gold Box program Tom has (Curse, PoolRad, Secret, Darkness, Champions, Death Knights, Gateway, Treasures) identically to a reference unpacker. `read()` unpacks just a byte range straight from the file - no 60 KB buffer. |
| Per-release program facts: file size + unpacked size identify the release; data segment base + table addresses | `src/engine/profile.*` | Curse GOG (`START.EXE` 57,789 bytes, 62,432 unpacked, DS at 0x48D0). Other games: their frame tables aren't where Curse keeps them (and don't match Curse's) - find them when each game's turn comes. |
| Screen frame: tiles = 8X8D1 block 202 (a picture block 8 high, 1 column, 40 frames; colour 13 not drawn); frame tile = 30 + table value, 3D-view frame tile = 20 + value. Tables (one byte a cell) in the program's data segment: top, bar, bottom (40), left, right (24), the explore screen's split column (17) and 3D-view frame (4 x 15), combat's three columns (23) | `src/engine/layout.*` | **v0.3.0** (Curse). Screens: outer border (rows 0-23, row 24 = menu line), + bar at row 16 (text screen), exploring (3D view cells 3-13, party right of column 16, text rows 17-22), combat (columns 0 / 22 / 39). The viewer's **Screen Test** draws them from Tom's own files. |
| Strings: Pascal strings (length byte + text) in the program's data (START.EXE, unpacked) and in GAME.OVR, a Turbo Pascal overlay file ("FBOV") whose procedures keep their string constants in their code segment | `src/engine/text.*` | **v0.4.0**: Curse's "Press any key to continue" read from START.EXE. Every string the engine will show comes from the player's files the same way (profile offsets). |
| Fixed screens printed by code (Curse credits): a run of 32-byte calls `B0 col 50 B0 row 50 B0 fg 50 B0 bg 50 8D 7E xx 16 57 BF <string offset> 0E 57 9A .... 9A ....` | `src/engine/printcalls.*` | **v0.4.0**: the 34 credit lines (text, row, column, colours) read from Tom's GAME.OVR; profile = where the run starts + its code segment base. |
| Title sequence (Curse): TITLE.DAX 1 at (0,0) 5 s; 2 at (0,0) + 3 at row 11 col 6, 10 s; 4 at row 11 col 0, 10 s; credits (outer frame + bars at rows 3 and 8), 10 s; a key skips each | `src/engine/profile.*` (steps), `src/ui/look.cpp` | **v0.4.0**, in the Screen Test. |
| Text window: printed a character at a time (game speed 4 = 12 ms) into a cell rectangle (text area rows 17-22 x cols 1-38; rows 21-22; combat panel rows 1-21 x cols 23-38); a word = punctuation + letters + punctuation + one space, wrapped whole if it (with its space) doesn't fit, longer-than-a-line words broken; leading spaces dropped after a wrap; area full = "Press any key to continue" (colour 13, row 24), clear, go on | `src/engine/text.*` | **v0.4.0** (behaviour from coab, own code). Touch: a tap = the key; a tap while printing finishes the page. |
| Combat screen: its frame ends at row 22; row 23 = a status line (range, "Spell: ...", "Item: ..."), row 24 = the menu as on every screen | `src/engine/layout.*` | Checked (coab). |
| Menu line (row 24): choices start at capitals / digits; prompt colour 13, key letters 15, the rest 10, the chosen word reversed (15 background) | `src/engine/text.*` | **v0.4.0**. Touch: tapping a word moves the highlight to it, shows it for 250 ms, then acts (its trailing space counts, so no gaps between targets; the tap area starts a row higher, at row 23 - Tom, v0.5.1). The chosen word stays chosen next time, as in the games. |
| Game icons: GOG's goggame-<id>.ico (ICONDIR) or goggame-<id>.dll (PE resources RT_GROUP_ICON 14 -> RT_ICON 3), copied by the player into the game's folder; images are DIBs (1/4/8/24/32 bit + mask) or PNG (GOG's 256 x 256, RGBA). The biggest is drawn, scaled down (alpha-weighted averaging) to 128 px on 320x240, 192 px on 480x320 | `src/engine/icon.*`, `png.*`, `inflate.*` (streaming, 32 KB window - based on CYD-Classic-Games' inflate) | **v0.6.0**: checked on the real goggame .ico / .dll of six games. START.EXE (DOS) has no icon; Support.ico is GOG's generic one, ignored. |
| GEO map block (1026 bytes): 2 bytes, then four 256-byte planes (a byte per square, x + y * 16): N / E wall types (nibbles), S / W wall types, a flags byte (>= 0x80 = under a roof: indoor sky colour), door states (2 bits per side: 0 solid, 1 open, 2 locked, 3 barred); wraps at the edges | `src/engine/geo.*` | **v0.7.0** (Curse). |
| WALLDEFn: n x 780 bytes = wall sets (5 pieces x 156 tile numbers in 10 groups: far front / sides, middle front / sides, near front / sides, far corner); a 2- or 3-part block fills the next sets too and its tiles are 8X8Dn blocks id*10+1.. ; tile numbers 1-45 common (8X8D1 #203), 46-115 / 116-185 / 186-255 sets 1-3 (a set's own numbers stored as set 1's, moved up 70 / 140), 256+ frame tiles | `src/engine/view3d.*` | **v0.7.0** (Curse): drawn far to near like the games; sky (area colour; indoor black / outdoor light blue until area data is read), black line, ground (8), SKY #252 horizon; AREA map from frame tiles 260-275 + the party arrow 256-259. |
| ECL scripts: 2 header bytes; 5 entry points; opcode + operands (code, low, [high] / packed string); per-game opcode table (Curse's in the profile) | `src/engine/ecl.*` | **v0.7.0**: decoding + following jumps from the entry points; finds each area script's first LOAD FILES (GEO block) and LOAD PIECES (wall sets) - all 18 Curse 3D maps. PoolRad's scripts use other opcodes (its profile will need its own table). Running scripts = M4. |
| ECL script: 65 opcodes 0x00-0x40, packed text | - | M4. |
| Monsters (MONnCHA: 285 B PoolRad, 422 B Curse, 439 B Secret, 510 B Darkness), items, characters, saves | - | M5-M6. Record sizes differ per game - per-game profiles. |

## 6. Milestones

1. **Library and asset viewer** (v0.1.0, this build). Finds the games on the
   card, lists DAX files and blocks, draws EGA pictures (all frames), hex-dumps
   the rest. Proves: SD on every board (incl. the 2.8" with software touch),
   the DAX and picture decoders on Tom's real files, 1:1 vs 1.5x on 480x320.
2. **The game's look**: its font (v0.2.0), screen frame (v0.3.0, Curse - the
   viewer's Screen Test), title sequence + credits, text windows and the menu
   line (v0.4.0). Done for Curse; the other games get theirs on their turn.
3. **3D view and walking**: GEO + WALLDEF + 8X8D drawing, turning and stepping by
   touch, auto-map. v0.7.0: the viewer's **Walk Test** (Curse) walks every 3D
   map the area scripts load, with the exploring keys, the AREA view and (on
   480x320) the Companion map; walls and doors only, no events yet.
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

What the GOG Curse journal PDFs are (checked 2026-10-06 / 07):
- There are **two different Curse journal PDFs**, both scans of the printed
  journal as two-page spreads:
  - the copy **in the game's install folder** ("Adventurers Journal.pdf",
    6.8 MB, 150 dpi, SHA-256 7c918ead661b...) - every GOG player has this one;
  - a 300 dpi copy (12 MB, Acrobat "Paper Capture" OCR, SHA-256
    d4712a050919...), which Tom attached first - probably GOG's separate
    extras download.
  The converter supports only the install-folder copy (Tom, 2026-10-07:
  stick to the one in the Project files); each rectangle table is keyed by
  the PDF's SHA-256, so another version would need its own table.
- All nine journals in Tom's Project (four PRS games + Krynn, Savage
  Frontier) are 150 dpi scans except Treasures (lower).
- The text layers are OCR and unusable for the calligraphic entry font
  ("6razecl {i!(g, fire"); the Curse install copy has none at all.
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
  game's files on the SD card (e.g. `/GOLDBOX/CURSE/JOURNAL.BIN`). At 150
  dpi a text column is ~340 px wide: about 1:1 on 320-wide panels, scaled
  up ~1.4x on 480-wide ones.
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

Recognised on the card from v0.3.0 (folder names Tom uses: CHAMPIONS, DEATH,
QUEEN, GATEWAY, TREASURE, UNLIMIT, or the full titles; also KRYNN, SAVAGE,
FRUA): the DAX games open in the viewer like the main four; Dark Queen and
Unlimited Adventures are listed as "newer format" (their .TLB / .GLB files,
looked for up to two folders down, can't be read yet).

First look at Tom's GOG files with `dax_inspect` (2026-10-07):

| Game | Containers | Pictures |
|---|---|---|
| Champions of Krynn | DAX, all blocks read | EGA pictures + animations, all parse (523) |
| Death Knights of Krynn | DAX, all blocks read | EGA pictures + animations, all parse (415) |
| Treasures of the Savage Frontier | DAX, all blocks read | VGA pictures like Darkness (460) |
| Dark Queen of Krynn | **"HLIB" containers** (`.TLB` / `.GLB`, files split over DISK1-3 folders), plus GAME.FON | not looked at yet - a newer engine generation |
| Gateway to the Savage Frontier | DAX, all blocks read | EGA pictures parse (238); only 1 animation - its PIC files differ, to check |
| Unlimited Adventures (FRUA) | "HLIB" `.TLB` / `.GLB` like Dark Queen, plus PCX / LBM pictures | not looked at yet |

## 10. Engine UI rules (carried over from CYD-Classic-Games)

- Big targets, stylus taps (Tom uses a DS Lite stylus, firm presses). A tap acts
  on release, at the point where the stylus came down. No drags or swipes.
- No "are you sure" confirmations. Title Case on keys and headings.
- Layout always from the panel size, never fixed for one board.
- Colours of the engine's own screens from `src/ui/style.h` (dark: black, navy
  keys, cream text, gold accents).

## 11. Other platforms (Tom asked, 2026-10-07)

The engine is written so it can be moved off the CYD later (a Raspberry Pi,
a PC, PortMaster handhelds):

- `src/engine/` is plain C++17 with no Arduino, display or touch code (CI
  builds and tests it on Linux). Files come in through `dax::ByteSource`
  (any file API), pictures go out as one 320x200 canvas of palette indexes.
- Everything board-specific is in `src/boards/`, `src/hal/`, `src/ui/` and
  `src/app/` (Arduino SD, LovyanGFX, touch).
- Rule from here on: game logic takes **key events** like the original
  (letters, arrows / keypad, Enter, Esc); taps on menu words, list lines and
  combat squares are turned into those keys by the CYD front end. A gamepad
  (PortMaster) or keyboard maps onto the same keys.
- A port = a new front end: open files (`ByteSource` over stdio), show the
  canvas (e.g. an SDL2 texture, scaled), turn input into keys, timing, sound.
  Tom (2026-10-07): PortMaster versions are a **stretch goal** - keep the
  engine easy to port; an SDL2 front end is held in reserve (a PC test build
  isn't wanted for its own sake).

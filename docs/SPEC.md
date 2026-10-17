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
- The player copies each game's whole GOG install folder, as it is, into
  `/GOLDBOX/` on a FAT32 microSD card: `/GOLDBOX/Curse of the Azure Bonds`
  and so on (Tom, 2026-10-09: what nearly everyone will do - the docs use
  the GOG names, not short DOS names). If the folder has no DAX files, the
  engine looks one folder down, then two (the Steam / SNEG releases:
  `/GOLDBOX/CURSE/GAME/CURSE`, the journal in `CURSE/Documentation/`).
  Their CURSE.CFG names the save folder `C:\CURSE\SAVE\` (DOSBox mounts
  `GAME` as C:), i.e. the game folder's own `SAVE`; since v0.71.1 the
  engine takes off the game folder's own name when the path as written
  isn't there (v0.70.0 looked for `GAME/CURSE/CURSE/SAVE`, so saved games
  weren't found and saving a character failed: "Couldn't write
  NAME.GUY"). A save folder missing from a copy is made when first
  written to. SNEG Pool of Radiance (POOL.CFG: `C:\POOLRAD\` then
  `C:\POOLRAD\SAVE\`) keeps its characters in `GAME/POOLRAD/SAVE`: Curse's
  Add Character -> Pool looks there when it exists (v0.71.2). (When Pool of
  Radiance itself is played: its save folder is the CFG's 4th line, not
  the first path line.)
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
  Settings -> "Asset Viewer: 1.5x" (v0.41.0; was "Game Screen", which Tom
  took for a play option that did nothing) scales only the asset viewer's
  pictures; the game screens stay 1:1. A scaled play screen (480x300, no
  Companion) is not planned unless Tom asks.

### Controls (Tom, 2026-10-06; mockup `docs/controls-proposal.png`)

- **Tap what the game shows.** A menu word on the game screen ("AREA CAST
  VIEW ENCAMP ...") = pressing its key; a line in a list = choosing it (and
  Enter); a square on the combat map = stepping / aiming toward it; "press
  any key" prompts = tap the game screen.
- **Keys only for what isn't on screen**, changing with what the game does:
  - 320x240 (Walk Test): one row of 8 square keys (~37x34) under the game.
    Exploring: Side-step Left, Turn Left, Forward, Turn Right, Side-step
    Right, Turn Around (Tom, 2026-10-07: side-steps outside the turns), Esc,
    and a Companion key (auto-map, journal, party on a full-screen page).
    Combat: the 8 direction arrows.
  - 480x320 (Tom, 2026-10-10, v0.50.0): the 320x120 area under the game
    holds two pads. The **movement pad**, 3 x 2 like a numeric keypad's top
    rows: Turn Left, Forward, Turn Right / Side-step Left, Turn Around,
    Side-step Right (Turn Around moved up between the side-steps). The
    **cursor pad**: Up, Left, Select, Right, Down (small words - v0.51.0,
    Tom: the movement keys keep their arrows, the cursor keys are told
    apart by small text) for the highlighted thing on the game screen -
    many of the games' menus are small to hit with a stylus. Up / Down move
    a list's highlight (the party menu's lines - highlighted once a cursor
    key is used -, the game's list menus, items, goods, spells, the party
    for a WHO pick or a trade, Modify's items; v0.52.0, Tom: the party
    list beside a menu line - exploring, camp, shop, magic, a spell's
    target, Alter -, as a tap on the line would); Left / Right move the menu
    line's (Up / Down too when there's no list; on the party menu, with no
    menu line, they pick the character; in Modify Character they change the
    value). Select = a tap on the highlighted thing (it lights up first, as
    taps do): the menu line's word, the list menu's line, the party menu's
    line, "press a key". Under the Companion map, stacked: **Game** (was
    "Menu": the engine's Menu, renamed so it isn't confused with the cursor
    keys), **Look** (v0.86.0, Tom: no **Esc** in the game - a stray tap
    left it; Exit Game is in the Game menu only, as on 320x240; Game and
    Look share the height). The map is tighter: no "Map" heading, the
    area / script / map and position lines small at the top (for
    troubleshooting), one legend line under it. The Walk Test has the
    movement pad and Area / Next Map / Esc, no cursor pad.
  - 320x240 Play Test (Tom, 2026-10-10, v0.51.0: "automatic plus a
    switch"): one row of 9 keys (~31 px) that changes with the game. While
    the party can move (exploring a 3D area, or a fighter's Move / manual
    Aim in combat) it shows the **movement row**: the six movement arrows
    (Side-step Left, Turn Left, Forward, Turn Right, Side-step Right, Turn
    Around), **Menu Keys**, **Game**, **Map**. Otherwise (menus, lists,
    text, the party menu) the **cursor row** (order: Tom, v0.52.0): Up,
    Down, Select (two keys wide), Left, Right in small text, **Move Keys**,
    Game, Map (v0.66.1, Tom: every key's text small, Game and Map too;
    Select gives 8 px so the four direction keys are each 2 px wider and
    "Right" fits). Menu Keys / Move Keys swap the rows by hand; the swap lasts
    until the game changes between moving and not. **Esc** moved into the
    Game menu (v0.52.0, Tom): a key at the right of its tab bar - back to
    the game, then Esc (at the game's top level that leaves the Play Test,
    as the key did). Its slot went to **Map** (Claude's pick, Tom to
    confirm; SPEC's old plan of a Companion key): the Companion's map
    (walls, gold doors, red locked doors, the party's arrow, the area /
    script / map lines) drawn over the game screen until the next tap; the
    game waits meanwhile. Look is on the game's menu line there. The Walk
    Test keeps its row of 8 (no cursor keys).
- **The Game key** (Tom, 2026-10-09; v0.16.0; called Menu until v0.50.0):
  the Play Test's key where the Walk Test has Area (320x240 row: 8th key;
  480x320: the first key under the map) opens the engine's own screen,
  tabbed along the top:
  **Journal** (the journal entries and tavern tales the game has mentioned
  so far, oldest first, the latest in gold - tap one to read it; kept for
  the session until saved games keep it) and **Journal PDF** (the book
  view). More tabs as the engine grows (party, settings ...). Back to Game
  at the bottom. Area itself is the first word of the game's exploring
  menu; a tap on the 3D view also switches between the 3D and Area views.
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
| Title sequence (Curse): TITLE.DAX 1 at (0,0) 5 s; 2 at (0,0) + 3 at row 11 col 6, 10 s; 4 at row 11 col 0, 10 s; credits (outer frame + bars at rows 3 and 8), 10 s; a key skips each | `src/engine/profile.*` (steps), `src/game/look.cpp`, `src/game/play.cpp` | **v0.4.0**, in the Screen Test. **v0.53.0** (Tom: "like starting the game for real"): the Play Test opens with it (sound 0x0D with picture 4; a tap or key skips each wait), then a clear screen with the version line and its menu from the program ("Curse of the Azure Bonds v1.3 " Pascal string at image 0x06, "Play Demo" at 0x25); Play opens the party menu. The demo: v0.54.0 (roadmap 6). |
| Text window: printed a character at a time (game speed 4 = 12 ms) into a cell rectangle (text area rows 17-22 x cols 1-38; rows 21-22; combat panel rows 1-21 x cols 23-38); a word = punctuation + letters + punctuation + one space, wrapped whole if it (with its space) doesn't fit, longer-than-a-line words broken; leading spaces dropped after a wrap; area full = "Press any key to continue" (colour 13, row 24), clear, go on | `src/engine/text.*` | **v0.4.0** (behaviour from coab, own code). Touch: a tap = the key; a tap while printing finishes the page. |
| Combat screen: its frame ends at row 22; row 23 = a status line (range, "Spell: ...", "Item: ..."), row 24 = the menu as on every screen | `src/engine/layout.*` | Checked (coab). |
| Menu line (row 24): choices start at capitals / digits; prompt colour 13, key letters 15, the rest 10, the chosen word reversed (15 background) | `src/engine/text.*` | **v0.4.0**. Touch: tapping a word moves the highlight to it, shows it for 250 ms, then acts (its trailing space counts, so no gaps between targets; the tap area starts a row higher, at row 23 - Tom, v0.5.1). The chosen word stays chosen next time, as in the games. |
| Game icons: GOG's goggame-<id>.ico (ICONDIR) or goggame-<id>.dll (PE resources RT_GROUP_ICON 14 -> RT_ICON 3), copied by the player into the game's folder; images are DIBs (1/4/8/24/32 bit + mask) or PNG (GOG's 256 x 256, RGBA). The biggest is drawn, scaled down (alpha-weighted averaging) as big as the library page allows (~154 px on 320x240, ~194 px on 480x320; v0.7.1) | `src/engine/icon.*`, `png.*`, `inflate.*` (streaming, 32 KB window - based on CYD-Classic-Games' inflate) | **v0.6.0**: checked on the real goggame .ico / .dll of six games. START.EXE (DOS) has no icon; Support.ico is GOG's generic one, ignored. |
| GEO map block (1026 bytes): 2 bytes, then four 256-byte planes (a byte per square, x + y * 16): N / E wall types (nibbles), S / W wall types, a flags byte (>= 0x80 = under a roof: indoor sky colour), door states (2 bits per side: 0 solid, 1 open, 2 locked, 3 barred); wraps at the edges | `src/engine/geo.*` | **v0.7.0** (Curse). |
| WALLDEFn: n x 780 bytes = wall sets (5 pieces x 156 tile numbers in 10 groups: far front / sides, middle front / sides, near front / sides, far corner); a 2- or 3-part block fills the next sets too, its tiles are 8X8Dn blocks id*10+1.. (block 0 counting as 10: 101.. - Pool's loader, v0.104.2), and its later parts already number their tiles after the first's (every part moves by the first set's amount - v0.7.2 fix: shifting each part by its own set garbled Curse's area 5 / 6 walls); tile numbers 1-45 common (8X8D1 #203), 46-115 / 116-185 / 186-255 sets 1-3 (a set's own numbers stored as set 1's, moved up 70 / 140), 256+ frame tiles | `src/engine/view3d.*` | **v0.7.0** (Curse): drawn far to near like the games; sky (area colour; indoor black / outdoor light blue until area data is read), black line, ground (8), SKY #252 horizon; AREA map from frame tiles 260-275 + the party arrow 256-259. |
| ECL scripts: 2 header bytes; 5 entry points; opcode + operands (code, low, [high] / packed string); per-game opcode table (Curse's in the profile) | `src/engine/ecl.*` | **v0.7.0**: decoding + following jumps from the entry points; finds each area script's first LOAD FILES (GEO block) and LOAD PIECES (wall sets). Several scripts can load one GEO block with different sets (Curse area 2 GEO 1: script 1 loads set 3 = block 3, a set the walking scripts 2-4 don't use); the Walk Test keeps one entry per map with the sets most of its scripts load (tie: the later script) - right until scripts run (M4). PoolRad's scripts use other opcodes (its profile will need its own table). Running scripts = M4. |
| ECL script machine: 65 opcodes 0x00-0x40, packed text (3 bytes = four 6-bit characters). Memory: 0x4B00-0x4EFF area words (0x4BC5 map block, 0x4BC6-0x4BCC clock slots counting 10 / 10 / 6 / 24 / 30 / 12 / 256 - slot 1 minutes, 2 ten minutes, 3 hours; 0x4BE6 in a 3D area, 0x4BF0 / 1 last square, 0x4BF2 last script, 0x4BFB area view blocked, 0x4BFC game speed, 0x4BFD / E outdoor / indoor sky colour (index into the program's sky table, DS 0x6D9A), 0x4C00-0x4C20 the script's own variables), 0x7A00-0x7BFF a table, 0x7C00-0x7FFF game / character fields (0x7EC9 >= 0xFF: no move this step, 0x7ECA search flags, 0x7EE1 head picture, 0x7F12 area, 0x7F22 / 24 / 26 > 0x80 load wall set 1 / 2 / 3), 0x8000+ the script's bytes, 0xC04B-0xC04F party x, y, facing, wall ahead, roof flags. Compare flags in the order ==, !=, <, >, <=, >= (op1 vs op2). A step: the area script's entry 0 runs on the square the party is on, then the party moves (unless 0x7EC9 says not; a minute, ten when searching), then entry 1; NEWECL = the new script's entry 4 (first), then 0, then 1; a new game = ECL<start area> block <start script> (Curse: ECL2 #1) | `src/engine/ecl_vm.*`, `src/game/play.*` | **v0.8.0** (Curse): the viewer's **Play Test** runs a new game from the opening on Tom's files: text (word wrap, pages), menus on the menu line (tap a word), vertical list menus (tap a line), pictures (PIC / BIGPIC / HEAD + BODY), map + wall loading, the clock, NEWECL. Everything that needs a party (combat, treasure, checks, NPCs) is logged and passed over; v0.9.0: INPUT NUMBER / STRING typed on the menu line with an on-screen keyboard (320x240: letters over the top of the game screen, Del / Space / Enter under it; 480x320: letters under the game screen, Del / Space / Enter in the Companion strip); event pictures (PIC<area>) animate while the game waits at a menu (frame delay x 0.1 s) and step a frame on CALL 6803 (+ the game's delay); the wilderness map (BIGPIC 0x79) gets the blinking square at the party's place (columns / rows read from the program: Curse DS 0x6D5A / 0x6D7A, 32 places, place number = script word 0x4CA1; not after event picture 0x50 was shown); area changes (SAVE area -> 0x7F12, NEWECL) load the new area's files, and leaving a big picture for a 3D area brings the exploring screen back. CALL addresses: 2E10 redraw, C01E step forward, 6803 picture frame. v0.9.2 (Tom's v0.8.0 report): CALL 2E10 draws the 3D view again (the event picture goes; CLEAR BOX keeps it); locked / barred doors stop the party with "Locked." on the menu line (v0.60.0, facts: the Project's claude/door_facts.md: "Locked. Bash Pick Knock Exit" - Bash while allowed, every member in party order by the Strength in use until one breaks it (the original's table; a not-pickable door is too strong below 18/91 and takes Bash away until a step); Pick with a thief whose level counts, each okay member's d100 against Open Locks, once until a step; Knock when someone has it memorized, which uses it up and lets the party through without opening the door; nothing but Exit to offer: no menu. Bash / Pick open both sides in the map in memory; the area reloaded locks them again. Success: a step (sound, a minute); either way the view is redrawn and the arrival script runs); encounters: SETUP MONSTER (sprite, max distance, picture) / APPROACH / ENCOUNTER MENU show the monsters' SPRIT<area> sprite in the 3D view (frame = distance 0-2 = open squares ahead up to the max; frame x / y + 3 cells; colour 0 see-through, 13 drawn black), then at distance 0 their picture (PIC, or HEAD + BODY when 0x7EE1 names a head) after the game's delay - not while the encounter menu is up; the encounter menu's texts by distance, Combat / Wait / Flee / Advance (Parlay when close or outdoors) and the script's result table (0 monsters flee, 1 combat, 2 party flees, 3 parlay; the party's speed is taken as 12 until there is a party). An instruction budget (50,000 a run) stops a script that never ends. v0.37.0 (facts in the Project's claude/ecl_stub_facts.md): **the scripts and the party** - the selected character's fields are read and now WRITTEN at 0x7C00-0x7FFF (the word is always stored; then money 0x7CBB copper / BD silver / BF electrum / C1 gold / C3 platinum, 0x7CB8 the control byte (over 0xB2: - 0x32), 0x7CF7 / F9, spell places 0x7C20-0x7C70 = record 0x1F-0x6F, 0x7D00 >= 0x80 out of action (0x87 stoned), 0x7D0C the side (0 / 0x80 the computer's / 0x81 the enemy's), 0x7CC9 the magic-user level + a human's old one past it); the string at 0x7C00 = the selected character's name (PRINT / SAVE of a string). **LOAD CHARACTER** n: member n & 0x7F selected (none: 0x7D00 reads 0 once); EXIT / PROGRAM put back the selection the run began with; + 0x80 after the script wrote 0 to 0x7C00 and 0x7D00: the member leaves the party. **WHO** "<prompt> Select" over the party list (a tap on a line, Select). **ROB** who (0 the selected, else all), % of the money (all seven kinds), item chance (heavy items cut it: over 255 - 90, over 24 - 50, and it stays cut). **DAMAGE** f, dice, sides, bonus, t: rolled once; f & 0x80: saves (bonus f & 0x1F, type t & 7; f & 0x40 everyone - 0x20 no save -, else t & 0x80 the selected - type t & 7 - 1, 0 no save -, else a random member; f & 0x10 hurts even when saved; no half damage), else f attacks on random members (d20 + t > the AC byte; 1 misses, 20 hits; damage rolled again each); dead members skipped; HP 0 unconscious, below dying, 10 below dead; "  NAME is hit FOR N points of Damage." / "  NAME dies. " a line each, then "press <enter>"; everyone down: "The entire party is killed!" and the party menu. **ADD NPC** id, morale: MON<area> block id (record, effects, items) joins at the end of the party (8 at most), selected, control byte 0x80 | morale / 2, its fight icon CPIC<area> block id. **DUMP**: the selected member leaves. **DESTROY ITEMS** type: everyone's items of that type. **FIND ITEM** (anyone carries the type) / **FIND SPECIAL** (the selected has the effect): == / !=. **SPELL** id: its place in the first member's memorized list holding it and the member (none: 0xFF, the last member). **PARTYSTRENGTH** (HP + 5 x AC over 0 + 5 x to-hit + 8 x magic-user + 4 x cleric levels) / 10 a member, a byte sum. **PARTY SURPRISE**: a ranger (class 4 / 10) in the party. **CHECKPARTY** as the game does it: 0 - anyone with effect op2; 0x80A4-0x80AB a thief skill, 0x809E movement: lowest, highest, average; anything else (Curse's one use) writes nothing. 0x7F3E follows the party's size. The encounter menu's Flee / the monsters' flight use the party's slowest / fastest movement. CALL B200 (a sound) passed over quietly. The machine's own words (encounter notes, DAMAGE's lines) come from GAME.OVR (profile script_words). v0.38.0: **PROGRAM 9** the camp (the script ends when the party breaks camp), **PROGRAM 3** the party killed (the party menu); v0.65.0: **PROGRAM 8** the game won (claude/curse_finish_facts.md 1: Curse's one use is after the last fight, ECL6 block 67): six pages of the end texts (GAME.OVR) in the text window, "Press any key to continue." after each but the 4th; PIC animations 0x4A and 0x4B played once with pages 2 and 3, PIC 0x4D drawn faded (each colour by the program's table, DS 0x0AAA) and held (10 - speed) x 2 x 0.25 s, then head and body 0x41 for page 5, BIGPIC 0x7A with fireworks (an approximation of the original's: rockets from (65, 65), trails, bursts of 3 x 40 sparks) until a key; then every member healed and up, the training mask 0xFF (everyone may train, free while the engine runs - not saved, as in the original), area word 0x3FA (vm 0x4CFD) 0xFF so Begin does nothing, and the party menu. Other PROGRAM values do nothing. **TREASURE 0x80 + n: n random items** (facts in the Project's claude/random_treasure_facts.md; `engine/treasure.*`): d100 1-60 a weapon or armour by a second d100 (1-47 / 50-59 that type - the heavy crossbow a shield -, 48-49 a shield, 60-90 a sword by d10: long 1-4, broad 5-7, bastard, short, two-handed; 91-94 arrows, 95-97 a ring of protection, 98-100 bracers), 61-85 a magic-user scroll, 86-92 a clerics', 93-98 d15: 1-9 a potion (d8 1-5 healing, 6-8 extra healing), 10 giant strength, 11-15 the wand of magic missiles, 99-100 a shield. Weapons and armour +1 (d20 1-14) / +2, never cursed, words 1-2 hidden until identified ("Long Sword" -> "Long Sword +1"; armour "Chain Mail +2"), weights and values per plus by type (profile), arrows / quarrels 10, darts 5; bracers AC 6 / AC 4; a javelin 1 in 5 of lightning. Scrolls: d3 spells, each a d5 band and a die (profile bands), 300 x band gold each. Potions, the wand and the javelin of lightning: the program's rows of ready-made items (DS 0x0830, 7 x 8 words: words, weight, value, effects), plus 1. |
| Characters (Curse): the game's own 422-byte record (.SAV in a saved game, .GUY), kept as read so saving writes it back unchanged: name (Pascal, 15), stats (current / full, Str Int Wis Dex Con Cha Str00) at 0x10, THAC0 0x73, race 0x74, class 0x75, age 0x76, HP max 0x78, saving throws 0xDF, hit dice 0xE5, thief skills 0xEA, control 0xF7 (>= 0x80 NPC), money 0xFB (7 x u16: copper, silver, electrum, gold, platinum, gems, jewellery), levels 0x109 (8 classes) and before a class change 0x111, sex 0x119, alignment 0x11B, experience 0x127, health 0x195, in the fights 0x196, side 0x197, AC 0x19A (shown 60 - it), HP 0x1A4, movement 0x1A5; items .SWG (63 bytes each, 16 max), effects .FX (9 bytes each) | `src/engine/party.*` | **v0.14.0**: read and checked on the GOG release's sample party (`SAVE/CHRDATA1-6.SAV`: Mathew, Mark, Travis, Ledera, Shara, Philippe). The scripts read the selected character at 0x7C00 + 0x15 (Int), 0x18 (Con), 0x72 race, 0x73 class, 0x9B, 0xA0, 0xA5-0xAC thief skills, 0xB8 control, 0xBB-0xC3 money, 0xC9 magic-user level, 0xD6 sex, 0xD8 alignment, 0xE4, 0xF7, 0xF9, 0x100 (1 in the fights, 0x80 not), 0x11B movement, 0x2B1 / 0x2B4 its place in the party, 0x2CF a charisma percentage (coab's list); party size = 0x7F3E. |
| Saved games (Curse): `SAVGAMA.DAT`-`SAVGAMJ.DAT` (13,149 bytes) in the save folder (`CURSE.CFG`'s path line, `C:\SAVE\` = the game folder's `SAVE`): game area; script memory 0x4B00-0x4EFF (0x800), 0x7C00-0x7FFF (0x800), 0x7A00-0x7BFF (0x400); the script (0x1E00); x, y, facing, wall ahead, roof; last / current game state; 3 x (WALLDEF block, set); party count; 8 x 41-byte character file names (`CHRDATA1`; + .SAV / .SWG / .FX) | `src/engine/savegame.*` | **v0.14.0**: GOG's `SAVE/SAVGAMA.DAT` is a new game in area 2 at 7,13 with the sample party (no script run yet: 0x4BF2 = 0). **v0.18.0 saving**: Save Current Game (party menu) and Camp -> Save: "Save Which Game: A B C D E F G H I J", "Saving...Please Wait"; the file as above (state 0 party menu / 2 camp, last state 4 in a 3D area / 3 outdoors; the wall sets as loaded, a multi-part block's later sets -1), then each character as CHRDAT<slot><n>.SAV (the record) / .SWG (items; removed when none) / .FX (effects; removed when none). A read-then-write of GOG's SAVGAMA.DAT gives the same bytes but for the unused ends of the name slots. The engine keeps the journal entries met beside it: `_CYD/<folder>/SAVGAM<slot>.JNL` (kind, number byte pairs). BEGIN marks the script as run (0x4BF2) after its first run, so a game saved after that resumes where it was. |
| Camp (Curse; the exploring menu's Encamp): "The party makes camp..." (1, 18, colour 10), "Camp: Save View Magic Rest Alter Fix Exit" (the words in START.EXE image 0xB00A, "Camp:" / the line in GAME.OVR 0x1B382 / 0x1B34E) | `src/game/play.*` | **v0.18.0**: Save, View, Exit (a tap on a character selects them). v0.28.0: **Magic** ("Cast Memorize Scribe Display Rest Exit", START.EXE 0xB033) - **Memorize**: "NAME's" + "Spells in Grimoire" (row 1, at the name's length + 4), the spells they know and can use (clerics Wis 9+, druid spells for rangers past 6th level, magic-users Int 9+) by level under "1st Level" headings (START.EXE 0xB480, colour 13 - the lists' heading colour, v0.29.0) from row 5 to 15, "NAME can memorize:" (row 19) and a row a kind ("    Cleric Spells:" with the spells still to choose at columns 20, 23 ...), "Choose Spell: Memorize Next Prev Exit"; leaving with spells chosen: "Spells to Memorize" and "Memorize these spells? Yes No" (No forgets them; already memorizing: "Memorize These Spells?"); "NAME is in no condition to memorize spells", "NAME cannot memorize any spells". The list in the record at 0x1E (84 spell numbers, + 0x80 while being memorized), hours before the first spell at 0x72 (engine/magic). **Rest** (camp and magic menu): "Rest Time:" (row 17) DD:HH:MM (the unit being set in colour 15), "Rest Days Hours Mins Add Subtract Exit"; the time the spells need (4 hours' start, 6 for spells past 2nd level, + 15 minutes a spell level, the longest in the party). Resting, five minutes a step: the clock, effects running out, a day heals everyone 1 HP ("The Whole Party Is Healed"), spells memorized one after another once the start has passed (3 steps a level; "NAME has memorized SPELL"), an encounter check every 0x7ED2 steps at 0x7ED3 % ("Your repose is suddenly interrupted!": the camp ends and the area's camp-interrupted script, entry 3, runs); a tap asks "Stop Resting? Yes No". Leaving the camp forgets spells not yet memorized. v0.29.0: **Cast** (the magic menu, and the exploring menu's Cast for a character who is okay): "NAME's Spells in Memory" (rows 5-22, the chosen line highlighted), "Choose Spell: Cast Next Prev Exit"; "NAME is in no condition to cast any spells", "NAME has no spells memorized" (and back). A spell for fights (the spell table's targets byte 0): its name (row 19), "can't be cast here..." (row 20), "Lose it? Yes No". Else "NAME casts" / the spell (rows 19-20) for the game's delay (speed x 100 ms; a tap moves on); a spell for one member: "Cast Spell on whom Select Exit" over the camp screen (tap a member; Exit keeps the spell); the spell leaves the caster's memory and what it did is said a line at a time ("NAME is Blessed", "NAME is fully healed" / "partially healed", "is Cured", "can see", "is unpoisoned", "is raised", "is un-cursed", "has an item un-cursed", "is unaffected"), then the list again. The spell table (16 bytes a spell in START.EXE's data segment: class, level, range, duration fixed + per level, targets 1 self / 2 a member / 4 the party, effect, when) gives the effect and how long it lasts (minutes: fixed + per level x the caster's level for the spell's kind; non-casters 6); what each spell does outside combat is the profile's camp table (engine/spells): effects (Bless, Prayer (side x 16 + level), Mirror Image (1d4 x 16 + level), Haste (the first `level` members; a slowed one is cured instead), protections, resistances, Shield, Invisibility, Detect ..., Find Traps, Read Magic, Minor Globe; the effect's data = the caster's level; a running one of the same kind gives way), cures of wounds (1d8, 2d8+1, 3d8+3; outside a fight the dying come round and the unconscious wake), Cure Blindness, Cure Disease (disease 0x22; weakness 0x2B with 0x2C and 0x1F; 0x32 with 0x39), Slow Poison (the poisoned: 1 HP at least, effect 0x16 with data 0xFF, the poison's harm 0x0F held 10 minutes), Neutralize Poison, Remove Curse (the curse effect, else the first cursed item comes off), Raise Dead (the dead or animated, Constitution left, not elves: 1 HP, a point of Constitution). Not in the engine yet (the spell stays in memory): Enlarge, Friends, Strength, Spiritual Hammer, Dispel Magic, Restoration, Fire Shield - they change ability scores, make weapons or need choices that come with combat. **Display**: each member's name (colour 11), their effects (colour 10: effects named after the first spell 1-56 that gives them, and the named ones in GAME.OVR - "Poisoned", "Regenerating" ...), " <No Spell Effects>", a page of rows 4-22, "Next Prev Exit". Effects run out as game time passes (walking 1 minute a step, searching 10, scripts' CLOCK, resting). v0.30.0: **Alter** ("Alter: Order Drop Speed Icon Pics Exit", START.EXE 0xB05C; words in GAME.OVR): Order - "Party Order: Select Exit" (tap a member), Select: "NAME has been selected" and "Party Order: Place Exit" - a tap on another line moves them there (the games: arrow keys), Place puts them down; Drop - "NAME will be gone", "Drop from party? Yes No": "NAME bids you farewell" (or "is dumped in a ditch" when they can't fight; gone, not saved), No: "Breathes A sigh of relief"; the last member: "quit TO DOS: Yes No" (Yes leaves the Play Test); Speed - "Game Speed = N (0=fastest 9=slowest)" (row 18), "Game Speed: Faster Slower Exit" (the area word 0x4BFC, saved with the game); Icon (combat icons) and Pics: to come. **Fix**: nothing lost, nothing happens; else the healers' (okay members') cure spells in memory and the cures they would memorize again (a day's slots of each cure's level) are rolled, a rest of 4 hours (6 for cures past 2nd level) + 15 minutes a spell level (the longest healer; shorter when the party lost less than the healers can heal - 27 a 1st-level healer, 34 / 78 past 2nd / at 5th level - divided by that ratio), then the rolled healing shared out in party order. Resting as Rest does (encounters, "Stop Resting?"); stopped or interrupted: no healing. v0.31.0: **Scribe** ("NAME is in no condition to scribe any scrolls"): "NAME's Spells on Scrolls" (rows 5-22; the scrolls they can read - identified, or Read Magic (effect 0x10) on them, or a cleric with a clerics' scroll, which makes them known), "Choose Spell: Scribe Next Prev Exit"; "You already know that spell", "You are already scibing that spell" (the game's spelling), "You can not scribe that spell." (no slots of its kind and level) on the menu line for the game's delay; leaving with spells marked: "Spells to Scribe" and "Scribe these spells? Yes No" (No forgets them; already scribing when it opens: "Scribe These Spells? Yes No" first, No forgets them and shows the scrolls); none: "NAME has no copyable scrolls". Scrolls are items of slots 11-13 (ITEMS file; 12 a clerics'), their spells in the item's effect bytes 0x3C-0x3E (+ 0x80 being scribed), the second name word counting them ("With 1 Spell" 0xD2 ...). The rest time counts the spells being scribed like those being memorized; resting scribes first ("NAME has scribed SPELL"): the spell into the spell book, off the scroll (its word one less; used up below "With 1 Spell": the scroll is gone). Leaving the camp forgets scribing not yet done. Still to come: magic resistance and the spells' workings in fights (M6). The games ask "Quit TO DOS" after a camp save; the Play Test doesn't (its Esc key leaves). |
| Combat (Curse): LOAD MONSTER (MON<area>CHA.DAX block = the monster's 0x1A6-byte record, the same layout as a character's; SPC = its 9-byte effects, ITM = its 63-byte items; copies of it - 0 = 1 - up to 63 monsters; CPIC<area> block = its icon, + 0x80 the attack frame), SURPRISE (two d6 against d + 2 - a and b + 2 - c; the fight's surprise word 0x7ECB: 2 our side, 4 the enemies), COMBAT with monsters. The battlefield: 50 x 25 squares of 24 x 24 pixels, a ground value each (the program's ground table, DS 0x26A4: move cost, eye height, obstacle height, picture); indoors drawn from the 3D map 6 squares east-west and 2 north-south of the party, slanted (a map square (dx, dy) = a block at (21 + 6dx + 5dy, 10 + 5dy): floor, wall strips, doors, corners by the sides' codes - 0 open, 1 wall, 3 door; tables and chairs in rooms), outdoors a random field by the place's terrain flags (DS 0x0354: a stream, trees and logs, ponds, rocks, grass); pictures DUNGCOM / WILDCOM block 1 and RANDCOM (table, chair, clouds, a body). Placement: the party at its square facing the enemies, the monsters the encounter's distance ahead (no further than the party sees) facing back; each side fills ranks of an 11 x 6 grid (DS 0x0310 shapes, 0x0300 / 0x0308 rank centres, 0x02EC formation facings, 0x02DC fallbacks behind and to the sides), half the side in the front rank; no room: a monster is out of the fight; a fallen party member leaves a body. The screen: the combat frame, a 7 x 7 view (pixel 8, 8), the view following the party's fighters (radius 2); the one whose turn it is on a grey box (COMSPR 0x19); party icons composed from CHEAD (head) over CBODY (body; + 0x40 normal size, + 0x80 attack), recoloured by the record's six colour bytes (0x145: defaults 1 2 3 4 6 7 -> low nibble, + 8 -> high nibble); monsters' CPIC icons (24 x 24 to 48 x 48); facing west-ish (4-7) mirrored; the right panel: name (11 ours, 14 the enemy, 12 down), "Hitpoints", "AC", the readied weapon, the status word when down; messages from row 10 ("NAME / Attacks / TARGET / Hitting for N points of damage" or "and Misses", "goes down" / "and is Dying" / "is killed"), a page for the game's delay x 2 (a tap moves on); colours 0 and 8 swapped while fighting. Rounds: initiative d6 + Dex reaction (min 1; the surprised side -6, round 1 only; outside 0-20: 0), the highest acts first (ties: d100); movement points = movement x 2 (a straight step costs the ground's move cost x 2, a diagonal x 3); attacks a round from half attacks (record 0x11C / 0x11D: 3 = 1, 2, 1, 2 ...). The player's menu "Move View Aim Use Cast Turn Quick Done" (Move while points are left, Use with items, Cast with spells in memory and not hit this round, Turn for clerics): Move - "Move/Attack, Move Left = N", tap a square next to the fighter (or the pad: 8 N, 7 NW, 9 NE, 4 W, 6 E, 2 S) to step, into an enemy to attack, an ally asks "Attack Ally:" (Yes: NPCs on the party's side turn on it), the field's edge asks "Flee:" (faster than every enemy: "Got Away", even: d2, else "Escape is blocked"), "can't go there"; the menu line ends the move, Esc undoes it; leaving an enemy's side gives it its attacks (from behind), stepping next to a guard too. Aim - the fighters by distance, "Aim: Next Prev Manual Target Center Exit" ("Range = N" on row 23; Target when in reach: the weapon's range - 1, at least 1 - and in sight; a tap on someone picks them; Center puts the one aimed at - or Manual's square, empty or not - in the view's middle, and Manual's square keeps the view with it: v0.104.2), missiles use the readied arrows / quarrels or the thrown weapon. Done - "Guard Delay Quit Bandage Speed Exit" (Guard: the first enemy to step next to them is attacked; Delay: last this round; Bandage: the first dying party member stops bleeding; Speed: "GameSpeed (N) : Slower Faster Exit"). Quick: the computer plays them. Attacks: d20 (1 misses, 20 hits) + the to-hit value (0x199) + the side's bonus (0x7F70 / 0x7F71) + Bless 1 / Curse -1 >= the target's AC (0x19A; from behind 0x19B); damage: the slot's dice (slot 2 then 1; the weapon's large dice against large targets) + bonus; the target turns to face the attacker. Damage: HP 0 unconscious, -1 to -9 dying (bleeding a point a round, dead past 9), -10 dead (HP kept 0). The computer: keeps its target while it's in sight, else a random one in sight, else the nearest; attacks when next to an enemy, else steps toward it (straight, or up to 2 directions to either side, never further away), else guards. A round's end: a minute of game time, bleeding, "Your Teammate is Dying"; over when a side is gone or 15 rounds pass without an attack. The end: won / fled (any running and none standing) / lost (no one standing): result 0x7EC7 = 0 / 0x81 / 0x80 for the script, 0x7EC8 = the enemies out; experience = each beaten monster's 0x13E x its rolled HP (0x12C) + 0x13C, + the treasure's worth (gold worth + 250 a gem + 2200 a jewel, + 400 x plus a magic item) shared by those standing (+10% for a prime requisite over 15; multi-classes divided); the effects that end with a fight go (profile fight.ends); the running come back, the dying fall unconscious, the unconscious with HP stand; a flight leaves the fallen behind; a lost fight: "The monsters rejoice for the party has been destroyed" (the party is gone: the party menu). "The party has won." / "The party has fled." / "Each character receives N" / "experience points." / "press <enter>/<return> to continue". The treasure (the beaten monsters' coins and items, and what TREASURE set out; COMBAT with no monsters: "The party has found Treasure!"): "View Take Pool Share Exit" over the exploring screen ("View Take Pool Exit" with items only, "View Pool Exit" with nothing), Take: "Take: Money Items Exit", coins as in shops, items "Items: " ... "Take" (newest first; too heavy: "Overloaded"); Exit with treasure left: "There is still treasure left.  Do you want to go back and claim your treasure?" Yes No (what's left is lost). | `engine/combat.*`, `src/game/play_fight.inc` (part of play.cpp) | **v0.32.0** (rules learned from coab - facts in the Project's claude/combat_setup_facts.md and combat_rules_facts.md - checked against the program's tables). v0.33.0: **Cast** in fights - "NAME's Spells in Memory" (the list screen), a camp spell: "Camp Only Spell"; a casting time (spell table byte 12 / 3 > 0): "Begins Casting" - it goes off when their turn comes round again this round (their delay = that, or 1), lost if they're hurt first ("lost a spell"); then the targets by the spell's aim (byte 6: 0 themselves, 1-4 that many picked with Aim's Target - "Already been targeted" -, 8-14 an area of radius & 7 round a fighter or a square picked with Manual; 5 and 15 one), reach = fixed + per level x the caster's level (touch: 1), "Casts a Spell" with "Spell: NAME" on row 23, the spell out of memory, and what it did a page each: effects ("is Blessed" - Bless only on the caster's side, Curse only on the other -, protections, resistances, Shield, Invisibility, Mirror Image, Prayer - its side +1 to hit, the other -1 -, Haste - twice the moves and attacks, Slow half), wounds cured ("is fully healed" / "is partially healed"; the dying fall unconscious), damage ("takes N points of damage from Fire"; Magic Missile (level + 1) / 2 missiles of 1d4 + 1, Burning Hands the level, Shocking Grasp 1d8 + level, Fireball level d6, Flame Strike 6d8, Cause Wounds; touch spells must hit; a save as the table says - byte 8: 1 none, 2 half; saves d20 (1 fails, 20 saves) + the record's save bonus 0x186 >= the record's save 0xDF + type), Sleep (4d4 by Hit Dice - 1 a die up to 1, 2, 4, 6, then 10 for monsters / 20 - "falls asleep"), Hold Person (a save at -2 / -3 for one, -1 for two, 0 for more; "is held", "is Unaffected"). The asleep, paralysed, held and helpless (fight facts) don't act, and any blow kills: "NAME slays helpless TARGET with one cruel blow" (to -5). Protection from Evil / Good: evil / good attackers -2. Attacking makes the invisible seen; the computer doesn't pick invisible targets. Everyone's effects run a minute each round. **Turn** (clerics, once a fight): "NAME turns undead...", one d20, d12 undead at most, the weakest in sight first (record 0xE9), the program's table (DS 0x0369, [type x 10 + column]; columns: levels 1-8, 9-13, 14+): d20 >= |value| - positive "is turned" (it flees for the field's edge), else "Is destroyed" (and a few more may go); none: "Nothing Happens...". Fleeing monsters run off the field ("Got Away" / "Escape is blocked"). v0.34.0: **the computer's spells** (monsters and NPCs with spells in memory, once a turn before moving): d7 tries from priority 7 down, three random spells from memory each, a spell taken when its priority (spell table byte 13) is at least the try's; a cure only on themselves below half their HP; effects for themselves or their side only when they don't have them yet; against the enemy: a random visible one in reach and sight (bolts from it on, areas round it, one to four as the aim says) - an area with one of their own side in it only when that one would pass a save (+8 the party's side, -2 the monsters'); a casting time: "Begins Casting" and the stored targets when their turn comes back. **Lightning Bolt**: level d6 (save half) to everyone on a line of 7 squares from the target away from the caster (a wall stops it). **Stinking Cloud**: everyone round the square (radius 1): a save, or "chokes and gags from nausea" (helpless d4 + 1 rounds); saved: "starts to cough". Hold Monster, Slow ("is Slowed", the other side), Ray of Enfeeblement, Cause Blindness, Cause Disease (their effects). **Morale** (monsters and NPCs, once a turn): morale = (the control byte 0xF7 & 0x7F) x 2 (over 102: 0), Bless +5 / Curse -5; it breaks when the HP lost (%) is more, and their side's health (% of HP left, + the same) is under 100 - the script's morale word (0x7EC6) or they're on the party's side: "flees in panic" when no enemy is faster, else "Surrenders" with Int (0x13) over 5 (out of the fight, beaten). v0.35.0: **Use** in fights - the items screen ("Ready Use Drop Halve Join Exit"; Exit back to the fight menu; Ready / Drop / Halve / Join don't end the turn), Use as outside fights (below) but the spell goes off at once (no casting time), "NAME uses an item" with "Item:" + its name on row 23, aimed like a spell (a spell against enemies starts at the nearest enemy), and the turn is over; Aim's Exit with nothing picked: "Abort Spell? Yes No" (Yes: "Spell Aborted", the use is gone and the turn over; No: aim again); a scroll the user can't read: "oops!" (the spell gone, the turn over). **The computer's items** (monsters, NPCs, Quick members; before their spells): d7 rounds from priority 7 down, their readied items that cast a spell (not scrolls) in order, judged by the spell table's line 0x17 back for spells past 0x38 (the games' way), the first with a use as for spells; a monster's item is its group's (a use off the shared pile). The items' own spells (the spell table's monster spells, 0x39-0x63): speed 0x39 (Haste, "is Speedy", 5d4 rounds), a lightning stroke 0x3C (1d6 + 20 along a line), paralysis 0x3D ("is paralyzed", 5d4 rounds), healing 0x3E / 0x63 (2d4 + 2, "is Healed"), the party invisible 0x3F (2d10 x 10 rounds), a fireball 0x40 (3, 5 or 7 d6), the wand's missile 0x41 (2d4 + 2); giant strength 0x3B, the protections 0x5F-0x61 and defoliation 0x62: v0.67.0. v0.36.0: **missiles for the computer** (monsters, NPCs, Quick members): not next to an enemy, with a readied missile weapon it can use now - a bow / crossbow (ITEMS flags 1 / 0x80) with its arrows / quarrels readied, a thrown weapon (flag 0x10: it goes with the throw), another with a range (a sling: nothing goes) - it shoots at its target when in reach (the range - 1) and sight, else at a random enemy there; one arrow / quarrel of the pile goes (a monster's: its group's pile). The player's Aim uses the same rule (no arrows readied: a bow reaches 1). v0.39.0: **backstabs** - a thief (record 0x10F) with no weapon or a club, dagger, broad / long / short sword, drow long sword (profile), straight behind a man-sized target that has already had an attack this round: the rear AC 4 worse, damage x ((thief level - 1) / 4 + 2), "-Backstabs-"; **sweeps** - a fighter (record 0x10B = their sweep level; once a round) with fewer attacks left than that, attacking an enemy under 1 Hit Die next to them while more such enemies are next to them than attacks left: "sweeps", one attack on each (the target first, up to the level), the turn over; **free attacks** only from an enemy that isn't held, sees the one stepping away, and hasn't acted yet this round, or hasn't been attacked, or has them in its front (facing +- 2). v0.41.0: **pictures in flight** (COMSPR: 0-2 an arrow up / slanted / across, 3 an axe, 4 a flask, 5 a spell, 6 lightning, 7 a rock, 8 a sling stone, 9 sparkles, 10 a burst, 11 the skull, 0x19 the box): a picture steps 8 pixels (a third of a square) at a time from the attacker toward the target along a straight line, stopping a step short (none next door), drawn over the field and the field put back each step; shots by the flying item's type (profile): pointed (darts, javelins, quarrels, spears, arrows) - one picture by direction (up / slanted / across, the attack picture the other way, mirrored to the west), 10 ms a step, the whistle (0x0C); spinning (hand axes, clubs, hammers - v0.78.0: type 20, as Curse's code tests; it was glaives) - the 4-picture cycle (ready, ready mirrored, attack mirrored, attack), 50 ms, sound 9; flasks - the same, 50 ms, sound 6; slings - the stone's 2 pictures, 10 ms, sound 6 (a sling readied wins over what it throws); the rest a rock, 20 ms, sound 9; a hit's blow (7) after the flight. A spell flies from the caster to the square aimed at (an area's centre) or its first target: picture 5 (lightning 6), the 4-picture cycle, 30 ms (lightning 50), with the spell's sound; a spell aimed at several picked one by one flies on to each (sound 2). **Magic hits**: damage from a spell - the burst (10) on the fighter's square, 4 pictures 70 ms each, sound 3; an effect taking hold ("is Blessed", "falls asleep", healed, "is turned") - sparkles (9), the game speed + 1 times round, sound 4. **The fallen** ("goes down", "is killed", destroyed undead): sound 5, their squares blink skull / box 9 times 10 ms apart, the skull staying until the page ends. A tap cuts the pictures short. **The computer's weapon** (Quick members and NPCs in the party; monsters keep theirs): once a turn after its spells, each weapon its classes can use rated dice x sides + 8 x plus + 2 x the type's damage bonus (> 0) + 2 x (attacks - 1) for a launcher + 3 one-handed (0 cursed, or past 3 hands); the best missile weapon when it rates over half the best melee one (which must beat the bare hands' dice), has what it shoots (readied arrows / quarrels; a sling or a thrown weapon: itself) and can be used now (thrown, or no enemy next to them), else the best melee weapon (none: bare hands); a cursed weapon stays; two hands put the shield away, free hands take the best shield (plus + 1); a bow held with an enemy next to them: another weapon and the turn is over. Fixed: party members whose head and body pictures are 0 (the GOG sample party) fought without icons (taken for NPCs). v0.43.0: **clouds that stay** (Stinking Cloud; coab's facts): the square aimed at and those east, south-east and south of it - each that can be entered - become cloud (ground 0x1E, the green cloud picture) for the caster's level in rounds, "Creates a noxious cloud" (GAME.OVR 0x30788); those in it, and anyone stepping in later, breathe it: a save against poison or "chokes and gags from nausea" (helpless d4 + 1 rounds; their turn ends), else "starts to cough"; the computer steps into one only after passing a test save; a cloud laid over another remembers the ground under both; when it runs out (a round's end): "The air clears a little..." (0x10AE9) on the menu line, the ground back (a fallen party member's body where one lies). v0.49.0: the coughing (those who saved: effect 0x1E for the round) - at their turn "is coughing" (GAME.OVR 0x106C3), no items and no spells that round (its AC 2 worse: v0.65.0). v0.44.0: **Charm Person** (coab's facts): a person only - humanoid (record 0x11A at most 1) and not large (0xDE at most 1), else "is unaffected" (GAME.OVR 0x2FECA); a save as the spell table says; "is charmed" (0x2FED8): the charm effect (0x0B; its data the caster's side << 7, their own << 6, 0x20, the level) for the table's time, they join the caster's side run by the computer (a charmed party member fights for the enemy) and drop their target; when it runs out they go back to their own side; party members are back on the party's side after the fight whatever happens. v0.46.0: **cones** (coab's facts; `combat::cone`): the line from the caster through the square aimed at, its steps repeated on to the cone's length (straight steps 2 half-squares, slanted 3), stopped by the field's edge and before a wall; everyone but the caster on the line to its end and - with more rays - on the lines to the end moved a square to the right (2 rays) and the left (3) of the line's last step, friends included. **Fear** (3 rays, 6 squares): a save against spells or "runs in terror" (GAME.OVR 0x32A7A; effect 0x8E for the table's time): they flee for the field's edge, a party member run by the computer meanwhile; when it's over they stop fleeing (and come back under the player). **Cone of Cold** (2 rays, (level + 1) / 2 squares): level d4 + level, a save as the table says ("from Acid": coab's damage type). v0.47.0: **Cloudkill** (coab's facts): the square aimed at and the 8 round it become poisonous cloud (ground 0x1C, the blue cloud picture) for the caster's level in rounds, "Creates a poisonous cloud" (GAME.OVR 0x32F30); those in it when it's laid, anyone stepping in, and everyone standing in it at each round's end: Hit Dice 0-4 die, 5 unless they save against poison at -4, 6 unless they save, 7 and up unaffected - "is Poisoned" (0x35E53), "is killed" (the skull); the computer never steps in below 7 Hit Dice; it clears like the stinking cloud. v0.48.0: **Charm Monsters**: as Charm Person but any creature, the spell table's number of targets (each picked; the spell flies on to each). v0.61.0: the monsters' special abilities (roadmap 6; the Project's claude/monster_fx_facts.md). **Magic-users stay back**: a computer-run fighter with magic-user levels and no armour readied doesn't step toward its enemies (it guards) - spells, items and shots as before. v0.65.0: the bolt's flight along its path and the coughing's AC. v0.66.0: **Detect** on the treasure screen (claude/curse_finish_facts.md 4): " Detect Exit" when items lie there and the one selected (in the fight's end, up) has Detect Magic memorized (spells 5, 0x0B, 0x4D - the first in the list's order); D casts it as in camp (out of memory, effect 5 on the caster for the spell table's time, "NAME casts" / the spell / "NAME is affected"); while anyone in the party has effect 5, item names everywhere get "* " first for magic (plus or save plus above 0, signed) and cursed items - nothing is identified. v0.67.0 (curse_finish_facts.md 10): **the items' own spells** everywhere - the Potion of Speed (0x39: a slowed one cured, else "is Speedy", 5d4), the Potion of Giant Strength (0x3B: effect 0x92 for d4 x 10 + 40, Strength 21 and "is stronger" when more than their own), the protection scrolls and the Potion of Invisibility (0x5F-0x61: the table's effect, nothing said), the Dust of Disappearance in camp too (0x3F, (d10 + 10) x 10), the Wand of Defoliation (0x62: a cone of 3 squares, 6d6 - plants all of it, the rest as when saved); the original's rolled times in camp (cause disease, speed, paralysis, giant strength, the party invisible, Neutralize Poison 1440); **Fire Shield in camp** ("flame type: Hot Cold", Esc: "Abort spell? Yes No" - Yes: the spell is gone) as in fights. v0.68.0 (a code review against the facts): **Mirror Image** works as the original's - only against spells at picked creatures (never weapons or areas): d(images + 1) over 1 takes an image ("lost an image"; the effect's whole data byte counts down); **Shield** AC 3 at worst, saves +1, Magic Missile stopped; **Prayer** +1 / -1 on saves; leaving a spell's aim (memorized or an item's) asks **"Abort Spell? Yes No"** (Yes: the spell is gone, the turn over); a spell begun last round is gone at the next round's start; **"Continue Battle: Yes No"** when no enemies are left and more than one stands (the party's turns go on, to bandage and heal); Guard, Bandage and Flee end the turn; a bow or crossbow can't strike ("Not with that weapon"), shoots only with ammunition and no enemy next to them; thrown weapons used up (daggers, spears, darts) land on the treasure (no experience for them); a tap during a Quick member's turn takes the party back (the original's Space); the pad moves Manual aim's square; confusion after the snakes and silence (the original's order); Sticks to Snakes counts at once; Dispel Magic on the animated dead makes them collapse, on a hold frees its victim; charmed / feared again keeps the first one's side / Quick; party members are on the party's side after every fight; invisible to animals hides from an animal's targeting; no enemy that can reach the fleer: away; items' effects can't be dispelled (data 0xFF), Remove Curse takes a cursed item's effect off too; a human past the old class: its THAC0, attacks and thief skills count; the Girdle of the Dwarves gives the dwarves' poison save bonus; AND / OR set the flags as the original (0 against the result). v0.68.1 (the game test, `tools/playsim/sweep.cpp`): ON GOTO / ON GOSUB tables longer than 22 entries jump right (the script ran off its end in two of the wilderness blocks); Exit on the treasure's item list goes back to the treasure (it opened a character sheet that couldn't be left after a fight); "Abort Spell?" No goes back to aiming (the menu came back but taps did nothing). v0.69.0 (the behaviour tests - `sweep.cpp`'s `behave` mode, about 6,400 checks of what each class, item, spell, monster ability, script instruction and save field does, against the original's listing and coab's facts; the facts in the Project's claude/behaviour_facts.md): **classes** - a magic-user / thief starts with 8,333 experience; a half-elf magic-user's limit by Intelligence; level 3 magic-users learn Charm Person, Human Change to magic-user gives four spells (Enlarge too); training makes up lost levels, keeps one under the level after next's figure and picks the class the original's way; a ranger past 8 gets a spell at any training; Restoration's own race limits; experience shared by those still in the fight (not the animated dead); the races' and rangers' effects in fights (Constitution save bonus, half-elves' 30% against sleep and charm, dwarves +1 against orcs, gnomes against their foes, giants -4 against dwarves and gnomes, rangers' level against giants); **items** - missile weapons' rate of fire (bows 2, darts 3, no more than the pile) and a missile a shot; thrown weapons that also strike land on the field (darts, javelins, flasks don't); the range penalty (AC +2 past a third, +5 past two thirds); Flame Tongue, Frost Brand and the Dragon Slayer's bonuses; the Cloak of Displacement (the first attack misses); the Ring of Invisibility (invisible at the start and after each round); large is size > 0x80; the Gloves of Thievery / Gauntlets of Dexterity thief skills; no free attack from the Dragon Slayer's or the Robe of Vermin's wearer; the Robe's miscast (a d2 of 1: nothing cast, the spell kept); the Berserker sword (computer-run, attacks the nearest other creature, friend or foe); **spells** - a dual-class human's old levels count for casting; Slow at most the caster's level of them, and it cures Haste ("is Cured"); Bless not on those next to an enemy; Fireball radius 2 outdoors; Sticks to Snakes' rounds; Ray of Enfeeblement (a quarter less damage); the druids' Detect Magic in fights; poison kills leave the poisoned effect (Slow / Neutralize Poison can bring them back); Prayer's 6 squares; Cause Disease's course (Strength -1 an hour, 1 HP every 10 minutes until cured); Haste ages a year once ("ages"); **combat** - monsters' values worked out at the fight's start as the original's (armed humanoids' damage, sane to-hit values); monsters ready their weapons (archers shoot); saves: a natural 1 fails even when immune, the byte sum compared unsigned; backstab's facing (the target turns after the attack is counted); Turn Undead's extra only for D*; sweeps by attack level, once a round; free and guard attacks with the attacks left; money's experience coin by coin; **events** - the surprise word (1 the party surprised, 2 the monsters; cleared at the fight's start; SURPRISE writes nowhere the scripts see); exits at the map's edge (0x7ED5); Look's search (0x7ECA = 1); "don't move" cleared before each key; fleeing by the slowest; rest encounters' step count kept; RETURN with nothing to return to; scripts reach the loaded monsters (the Thieves' Guild hideout's four allies). Saved games: every field the original keeps comes back (no changes needed). |
| Sound (Curse; facts in the Project's claude/sound_facts.md): the games' own driver in START.EXE (segment 0x699, image 0x6990): INT 8 reprogrammed to 236.69 Hz (5041 PIT clocks); each tick a byte-code player runs four voices for the PC speaker and four for the Tandy 1000's sound chip (SN76489-type at port C0h: three square-wave tones, a noise channel, 16 volume steps of 2 dB). A voice: wait, byte-code pointer, frequency, slide, output, volume, volume step, a modulation table (a sine; 512 random bytes - the speaker's "noise"), phase, step, depth, wrap, two loop counters; each tick volume += step, frequency += slide, output = frequency + table[phase] x depth / 256, then the byte code when the wait runs out (FF set a field - field 0: wait, 0 = the end; FE loop; FC / FB call / return; FD select a voice; FA clear; notes unused by Curse). PC speaker: the first voice sounding, a square wave at 1193182 / output Hz, on or off; Tandy: tone N = output >> 6 (3579545 / 32N Hz), attenuation (0xFFFF - volume) >> 12, the noise control rewritten every tick (its shift register starts over, as PC emulators of the chip run it). Per sound (the games' numbers, 2-0x0D) a start for each voice: PC table seg:04A2, Tandy seg:054A, 21 entries; a new sound replaces the one playing. CURSE.CFG's second line chose the device (T / P / else silent); GOG's set-up gives P and no Tandy chip. Where they play: 2 a spell (in a fight), 3 / 4 magic hits, 5 a death, 6 a sling / oil flask, 7 each hit, 8 Lightning Bolt, 9 all missed, 0x0A a step / a turn (in the 3D view and in fights; CALL B200), 0x0B Fireball (CALL B200 when the script set 0x03DE to 10), 0x0C a missile's flight, 0x0D the title. | `engine/sound.*`, `hal/audio.*` | **v0.40.0**: our own sequencer plays the player's byte code (read at the start of the Play Test: seg:04A2-12BA, 3.6 KB) and a synth makes 22,050 samples a second for the DAC (GPIO 26, DMA, the amplifier on only while it sounds; a ramp to and from the middle voltage against clicks; ~5 KB of RAM while it plays). Device and volume: Settings (Tandy the default). In fights each message page plays its sound as it shows (a hit, a miss, a missile's whistle on a shot's first page, a death, the spell's sound with its first result page); steps and turns in the 3D view and in fights; DAMAGE's "dies". v0.41.0: 3 / 4 with the magic-hit pictures, 5 for anyone falling (not only the dead), a shot's sound by what flies (0x0C, 6, 9). Not yet: the title (the Screen Test), cutting a sound short when the game loads a file (the originals do). |
| Party menu (the games' first screen): the outer frame, the party list (below), the menu a line each from row 12 (first letter colour 15 at column 2, the rest colour 10), "Choose a function " (colour 13) on the menu line; entries: a table of 12 x (string[40] + "on" byte) in START.EXE (Curse image 0xB133); Create / Add / Exit on from the start, Drop, Modify, View, Remove, Save, BEGIN with a party, Load without one, Train where the scripts offer training (0x7EA8 != 0), Human Change with training and a character who can change. Party list ("Name" at column 1 / 17, "AC  HP" at 33, row 2; a character a row from row 4; the selected name colour 15, others 11 (12 out of the fights, 14 the other side); AC right-aligned at 34 (a "-" before negative ones), HP at 38, colour 14 when below the most, else 10), strings in GAME.OVR (Curse 0x37E31 / 0x37E36; prompt 0x20111, "Load Which Game: " 0x1EE80) | `src/game/play.*`, `engine/profile.*` | **v0.14.0** (checked against the program's code). Taps: a menu line picks it, a character selects them (also on the exploring screen). Load Saved Game -> "Load Which Game: A B ..." (the slots found); BEGIN Adventuring -> the saved game's script again (its first run) or, when none ran yet, the area's start script, as the games do. BEGIN needs a party (v0.20.0; before that the Play Test began without one). (Save: v0.18.0, Create: v0.20.0, Modify: v0.42.0, Human Change: v0.45.0.) v0.27.0: **PROGRAM 0** (a script's party menu - the training halls) opens this menu in the game; BEGIN Adventuring goes back to the game screen and on with the script. **Train Character** (when 0x7EA8, the classes the hall trains, is set): "we only train conscious people" / "Training costs 1000 gp." / "We don't train that class here" / "Not Enough Experience" (on the menu line, the game's delay); else "NAME will become:" (row 4, column 4, the name in the party list's colour, the rest colour 10), "    a level 6 Paladin" (rows 5.., column 6; "and a level ..." after the first), "Do you wish to train? Yes No": "Congratulations...", 1000 gp paid, ONE class a session (of those with the experience and the hall's training: the one whose next level needs the most), its hit points rolled as in creation (engine/create: trainable, train_classes). v0.31.0: a magic-user's new level (or a ranger past 8th) brings a new spell: "NAME's Spells to Choose" (the spells of levels they have slots for, that they can use and don't know; rows 5-22), "Choose Spell: Learn Next Prev" (no Exit: one must be chosen), then "Congratulations...". After a message the party menu's own prompt comes back (it showed the last question before v0.31.0). Words: GAME.OVR 0x24C2E-0x24CDB. v0.42.0: **Modify Character** (rules learned from coab; `engine/create` can_modify / modify_stat / modify_hp / modify_done): only a character as made - experience 0, 8333, 12500 or 25000 and no former class - else "NAME can't be modified." (GAME.OVR 0x227F1); View Character's screen with the item being changed in colour 13 (a stat's value, the hit points, the name) and "Modify: Keep Exit" (0x22804 / 0x2280D). A tap on a stat, the hit points or the name picks it (the name again: the keyboard, "Character name:"); the keys' left / right arrows and turns take it down / up (the games' arrow keys), forward / turn around move to the item above / below. Stats: within the race's and sex's limits (the program's table), down no lower than the class's minimum (multi-class clerics' Wisdom 13); a fighter, ranger or paladin at Strength 18 goes on into 18/01 .. 18/00 (the race's and sex's top) and back down through it first. Hit points: from a point a hit die (with the Constitution adjustment) to every die at its highest (the class's dice, each class's Constitution bonus - fighters' extra from 17), averaged over the classes; Constitution keeps them in those bounds. Keep: the class values follow the stats (spell slots, thief skills, saves) and the rolled hit points (record 0x12C) are what's left after the Constitution bonus; Exit / Esc puts the character back as it was. v0.45.0: **Human Change** (coab's facts; `engine/create` can_change / change_classes / change_class): on the menu where training is offered, for a human with no former class; "Pick New Class" (GAME.OVR 0x3BB0D) over the classes they qualify for - the race's, not the present one, every stat the present class needs (a minimum of 9 or more) at 15 or more, every stat the new class needs at 17 or more, their alignment allowed - "Select ... Exit" (0x3BB2F); none: "NAME doesn't qualify." (0x3BB1C). Then experience 0, the present class's level kept as the former one, level 1 in the new class, the hit dice count kept (record 0xE6) and reset to 1, 2 half-attacks, spell counts and the memorized list cleared (a cleric: one first-level spell; a magic-user: detect magic, read magic, sleep - profile), the class values recomputed, items the new class can't use (not cursed) put away; "NAME is now a 1st level CLASS." (0x3BB36 / 0x3BB4B). |
| Add / Remove / Drop (Curse): Add: "Add from where? Curse Pool Hillsfar Exit"; Curse lists the save folder's .GUY files (422 bytes, not NPCs, not in the party) by name, "Add a character: Add Next Prev Exit" from row 2, "* " before those added; rules: 6 player characters (8 in all), "paladins do not join with evil scum", "too many rangers in party" (3), "NAME will tolerate no evil!"; ends when the party is full. Remove: saves the character as NAME.GUY (the name without spaces / punctuation, 8 letters) + .SWG / .FX, "Overwrite NAME? Yes No" when one exists; an NPC is dropped instead. Drop: "Drop NAME forever? " then "Are you sure? " (Yes No, No first): their files go, "You dump NAME out back." (or "NAME bids you farewell." when in the fights); No: "NAME breathes a sigh of relief." (words GAME.OVR 0x23595-0x23617, 0x1C506, 0x2255C-0x225A5, "Yes No" 0x32BF0) | `src/game/play.*` | **v0.19.0**. Overwrite -> No: "New file name: " (v0.66.0, curse_finish_facts.md 6: up to 8, upper case, asked again while empty, Esc ends it as Enter does, the name typed not cleaned; that one there too: "Overwrite" again - no way back, as the original); Pool (Pool of Radiance characters): v0.56.0; Hillsfar: not supported (Tom). |
| View Character (Curse; from the party menu and the exploring menu's View): the outer frame; name (1, 1, the party list's colours; "(NPC)" after it); sex, race, "Age n" (row 3, colour 15, a space between); alignment (row 4), class (row 5); STR ... CHA (rows 7-12, colour 10) with the full value at column 5 (6 below 10) and "(nn)" / "(00)" for exceptional strength at column 7; coins from jewellery down to copper, those held, from row 7 (name right-aligned to column 19, amount at 21); "Level" (1, 15) with the levels joined by "/" at 7 (a former class only below the current level), "Exp n" at 17; row 17 "AC" (value at 4), "THAC0" (9; 60 - to-hit bonus at 15), "Encumbrance" (22; at 34); row 18 "HP" (at 4, yellow when hurt), "Damage" (8; dice "1d2+6" at 15), "Movement" (25; doubled when slowed, halved when hasted, at 34); "Status" (1, 22) and the health word at 8; menu line: what the character can do. Names: tables in START.EXE (Curse image 0xB898 classes, 27-byte slots x 18; 0xBA7E races 10 x 8; 0xBACE alignments 17 x 9; 0xBB67 sexes 7 x 2; 0xBB75 coins 11 x 7; 0xBBC2 health 13 x 9); words in GAME.OVR (0x27094 "(NPC)" ... 0x276E8 "Movement", 0x27BA8 "Exit") | `src/game/play.*`, `engine/party.*`, `engine/profile.*` | **v0.15.0** (layout read from the program's code). Weapon / armour lines (rows 20-21) need item names, and the menu offers only Exit until Items, Spells, Trade, Drop, Heal and Cure come. |
| Items (Curse): 63-byte records (characters' .SWG, ITEMn.DAX blocks = treasure / shop goods, MONnITM.DAX): type 0x2E, name words 0x2F-0x31 (word 3 first: "Long Sword", "+1", "Frost Brand"; 0 = none; hidden-word bits 0x35: 4 hides word 1, 2 word 2, 1 word 3 until identified), plus 0x32, plus vs saves 0x33, readied 0x34, cursed 0x36, weight 0x37, count 0x39 (printed first: "10 Arrows"), value 0x3A (gold), effects 0x3C-0x3E. Name words: 255 x 21-byte slots in START.EXE (Curse image 0xBC37 = word 1). An "s" goes on one word when there are 2+ (the games' rules; missiles take it on their name unless word 3 is 0x87 / 0xB1). ITEMS file: 2 bytes + 128 x 16 bytes per type: slot (0 weapon, 1 shield, 2 armour, 9 rings ...), hands, damage vs large, attacks, armour value (0x80 + AC points, absolute: plate 57 = AC 3; shield 0x81 = +1), damage vs man-sized, range, classes (& the character's class bits 0x12B), flags (1 arrows, 2 missile, 4 melee, 0x80 quarrels) | `src/engine/items.*` | **v0.17.0**: names checked against all of Curse's ITEM / MONnITM blocks ("Long Sword +3 Frost Brand", "Studded Leather Armor", "10 Arrows"). |
| Create New Character (Curse): "Pick Race" (dwarf, elf, gnome, half-elf, halfling, human - no half-orcs), "Pick Gender", "Pick Class" (the race's list, DS 0x3FFA: 14 bytes a race, count + classes), "Pick Alignment" (the class's list, DS 0x41DA: 10 bytes a class), each a list with "Select Exit" (heading colour 13); then the character screen and "Reroll stats? Yes No", "Character name: " (15 letters), "Save NAME? Yes No" -> NAME.GUY / .SWG / .FX in the save folder (Add Character to Party brings them in). The rules: defaults (base AC 50, THAC0 40, icon colours from DS 0x3EC3), the race's effects (con save, dwarf vs orcs / giants, gnome, elf sleep resistance, half-elf; paladin protection from evil, ranger vs giants), experience 25000 / 12500 / 8333 (one / two / three classes), age (DS 0x404E: 7 x (base, dice, sides) a race; multi-classes the dice's top), stats best of six 3d6+1, the age's effects (from the program's code: Str +1 -1 -2 -1, Int 0 +1 0 +1, Wis -1 +1 +1 +1, Dex 0 0 -2 -1, Con 0 -1 -1 -1 for each bracket past, DS 0x4124), the race / sex limits (DS 0x3F88), class minimums (DS 0x4174), Wis 13 for multi-class clerics, 18/xx strength for fighters, paladins, rangers; 300 platinum; hit points (better of two rolls, dice DS 0x822 / count 0x81A, the constitution adjustment, divided by the classes); first spells (clerics all of level 1, magic-users four); then training to the level the experience buys (DS 0x429B: 99 bytes a class - experience for levels 2-12, spell slots gained; race level limits). Class rules (THAC0 DS 0x3E3A, saves 0x45BE, thief skills 0x3EC0 / 0x3F20 / 0x3F33, spell slots, class flags 0x3EA2) checked against GOG's sample party | `engine/create.*`, `engine/classes.*`, `src/game/play.*` | **v0.20.0**. To come: the combat icon editor (with combat), Modify Character, Train, Human Change (they use the same rules). |
| Rules (Curse): what the games keep up to date - encumbrance (items x count + coins), hands in use, AC (dex bonus + shield + rings / protection + the best of base AC and armour; magic armour drops ring bonuses), movement (armour over 150 / 399 weight: 9 / 6, +3 when 9 or less; carrying over the strength allowance + 0x200 / 0x300 / 0x400: 9 / 6 / 3), to-hit (THAC0 field; strength with melee weapons or none, dexterity with missiles, the weapon's plus, arrows' / quarrels' plus, +1 for elves with bows, short and long swords), damage (the weapon's man-sized dice, its bonus, strength, plus), attack level; strength groups 18/01-18/00; money: copper 1, silver 10, electrum 100, gold 200, platinum 1000 copper (gems / jewellery not counted); paying makes change as the games do; can't carry: 16 items or weight over the allowance + 1500 | `src/engine/rules.*` | **v0.17.0** (coab's rules, own code): recalculating the GOG sample party gives exactly their stored values. |
| The shop (Curse): a script sets 0x7F6C = 1 (0x7EE2 = a temple), the price factor 0x7F6D (0x10 normal, 1 = 1/16 ... 0x80 = x8), CLEARMONSTERS, TREASURE (coins + an ITEMn block of goods), COMBAT with no monsters loaded -> the shop: the picture stays, the party list, "Buy View Pool Appraise Exit" (+ Take, Share with coins on the counter). Buy: the goods last first, "Items: " + name (21) + price (9) from row 1, the chosen line highlighted; "Buy Next Prev Exit"; the selected character pays (the pool when they can't), "Not enough Money." / "Overloaded" on the menu line for the game's delay. A character's items (View -> Items): "NAME's Items", a bar at row 2, "Ready Item", " Yes  " / " No   " + name from row 5; Ready checks the class, the slot ("already using ..."), the hands ("Your hands are full!") and curses ("It's Cursed") | `src/game/play.*` | **v0.17.0**: Buy, View, Items -> Ready, Exit (tap a line to choose it, then the menu word). v0.19.0: Pool (the player characters' coins on the counter) and Share (equal shares by coin kind from jewellery down, one more of the rest each, then whoever can still carry, 1500 + strength allowance; the rest stays); Exit with coins on the counter shares them out (the games' shopkeeper asks instead - to come). v0.25.0: the items menu as the games build it - Ready, Use (exploring / camp; magic items come with spells: "not in the engine yet"), Trade (player characters, not in shops: "Trade with Whom?" over the party list, Select Exit; too heavy / 16 items -> "Overloaded"), Drop ("Your X will be gone forever", "Drop It? Yes No"), Halve (fewer than 16 items; n -> n - n/2 and n/2, "Can't halve that"), Join (the same item's piles, 255 a pile), in shops Sell (half the value, piles of arrows / quarrels each, other piles count / 20; "I'll give you N gold pieces for your X", "Is It a Deal?", paid as N/5 platinum + the rest gold, "Overloaded.  Money will be put in pool." puts the platinum on the counter) and Id (200 gold from the character or the counter, "Not Enough Money"; "It looks like some sort of X" / "I can't tell anything new about your X"). Readied items: "Must be unreadied" (Trade / Drop / Sell), Use needs "Must be Readied". Words: GAME.OVR (profile item_words). v0.26.0: **Take** ("Select type of coin " over the kinds on the counter from row 2, "Select" (+ Exit, a convenience for touch); "How much X will you take? " typed on the keyboard; too heavy -> "Overloaded"); **Appraise** (NAME, "You have a fine collection of:" row 7, "3 Gems" / "1 piece of Jewelry" rows 9-10, "Appraise :   Gems  Jewelry Exit"; a gem's value by d100: 10 / 50 / 100 / 500 / 1000 / 5000 gp, a jewel's by d100 then a random amount; "The Gem is Valued at N gp." row 12, "You can : Sell Keep" (Sell alone when it can't be carried): Keep makes it an item (type 70, name word 0x65 gem / 0xD6 jewel), Sell gives N/5 platinum, too heavy -> the rest on the counter, "Overloaded.  Money will be put in Pool."; none -> "No Gems or Jewelry"); **leaving coins on the counter**: the shopkeeper's (priest's) words in the text area and Yes No: Yes stays, No leaves them behind (replaces v0.19.0's sharing out). **The temple** (a script sets 0x7EE2 = 1 then COMBAT with no monsters): the shop screen with "Heal View Pool Appraise Exit" (+ Take, Share); Heal: "NAME, how can we help you?" (row 1, colour 15), the ten cures from row 4 column 2, "Heal Exit"; a cure that does nothing for them: "NAME is not blind." etc. and "cast cure anyway: Yes No"; "<cure> will only cost N gold pieces." (rows 21-22, colour 10), "pay for cure Yes No" - the character pays, else the counter, else "Not enough money."; "NAME is cured." Cures (prices from the game's code): Cure Blindness 1000, Cure Disease 1000, Cure Light Wounds 100 (1d8), Cure Serious Wounds 350 (2d8+1), Cure Critical Wounds 600 (3d8+3), Heal 5000 (all but 1d4, and blindness, diseases, feeblemind), Neutralize Poison 1000, Raise Dead 5500 (1 HP; the Constitution / HP changes: to check), Remove Curse 3500 (the curse effect, else a cursed item comes off), Stone to Flesh 2000. The words are read from GAME.OVR when shown (profile shop_words). v0.35.0: **Use** (exploring, camp, fights - facts in the Project's claude/item_use_facts.md): not readied: "Must be Readied"; an item that casts a spell (not a scroll, the spell in its effect byte 0x3D & 0x7F, 0x3E under 0x80 - 0x80 and up works while readied instead; 0x3C its charges, 0: it never runs out): "NAME uses an item" and its name (rows 21-22) for the game's delay, then as the spell is cast ("Cast Spell on whom Select Exit", what it did), at the item's level (6; the items' own monster spells: the user's); a use goes off the item (one of a stack, else a charge; the last: gone). A scroll: "NAME's Spells on Scroll" (only one they can read - identified, Read Magic, a cleric with a clerics' scroll), "Choose Spell: Cast Next Prev Exit"; clerics and magic-users read either kind, thieves of 10th level 3 times in 4, else "NAME oops!"; the spell goes off the scroll (used up below "With 1 Spell"). A spell for fights: "That Item is a combat-only item...", "Use it? Yes No" (Yes: the use goes for nothing). Anything else: nothing. v0.39.0: items that work while readied (effect byte 0x3E 0x80 and up): 0x80 - the effect in 0x3D is theirs while it's readied (no time limit; gone when put away, robbed or destroyed); 0x84 - keyed to an alignment (0x3D & 0x0F): the wrong one takes 0x3D >> 4 damage and it won't stay readied. v0.66.0 (curse_finish_facts.md 5): the **Ring of Wizardry** (magic-user levels 1-3 doubled while readied - each ring; put away: the slots without it and the magic-user spells past them forgotten, the later first; class values recomputed double them again), the **thief-skill items** (Gauntlets of Dexterity: below 4th level the skills of 4th, else +10; Gloves of Thievery: skill 1 of 5th / skill 2 of 7th level, else +5 - the +5 carried on to the skills after). The stat items (girdles, ioun stones, gauntlets, the cursed ones) were already as the original. v0.67.0 (curse_finish_facts.md 10): a **change of Constitution in use** (the Girdle of the Dwarves, a Constitution ioun stone) rebuilds the hit points as the original - the rolled ones (0x12C) plus Constitution's per class and level (fighters, paladins, rangers 15-19 (Con - 14) a level, 20 five, 21-23 six, 24-25 seven; the rest 15 one, 16+ two; old levels too; a ranger who hasn't changed class one level more), shared by the classes; the current hit points move by as much; at 20 and up an effect heals 1 hit point each time 60 minutes (rounds in a fight) run out ("is fully healed" / "is partially healed"). |
| Monsters (MONnCHA: 285 B PoolRad, 422 B Curse, 439 B Secret, 510 B Darkness) | - | M6. Record sizes differ per game - per-game profiles. |

## 5b. The card scan (Tom, 2026-10-09)

- The card is scanned once: at the first boot with a card that has no
  saved library, and when the player taps Rescan Card. Boot otherwise loads
  `/GOLDBOX/_CYD/LIBRARY.BIN` (the list of games; its record size is
  checked, so a firmware with another layout scans again). The board never
  looks for changes on its own.
- Everything made from the player's files goes in `/GOLDBOX/_CYD/`:
  `SCAN.TXT` (the scan's list, readable on a PC: games, icons and journals
  found, what was made, problems), `LIBRARY.BIN`, and per game
  `_CYD/<folder>/`: `ICON<px>.BIN` (the GOG icon decoded once at the
  library's icon size: "ICN1", u16 w, u16 h, RGBA rows; a board with
  another screen size makes its own size when first shown), the journal
  entries (v0.12.0).
- The scan screen: header "Scanning Your Card", a scrolling list of what
  it does ("Searching for Gold Box games...", "Looking in CURSE..." ->
  "Found Curse of the Azure Bonds", "Found the Curse game icon", "Found the
  Curse of the Azure Bonds journal", "Prepared the Curse icon", ...), then
  "Done." - since v0.21.0 (Tom) the whole log then stays up ("Card Scan",
  scrolling, see 5c) until Continue, then the library.
- Journal PDFs: a .pdf with "journal" in its name in the game's folder (or
  the folder holding its game files).
- v0.19.1 (Tom: v0.19.0 restarted as the journals' processing began): the
  library is saved BEFORE the journals are made, so a restart there can't
  start a scan on every boot; a journal being read is marked by
  `_CYD/<folder>/JOURNAL.TRY` - a scan finding one left behind skips that
  journal (and says so), the next Rescan Card tries again; loop() runs
  with 16 KB of stack (the PDF / JPEG code runs deep under the viewer);
  the "Processing" line shows the free memory; a restart by a crash,
  watchdog or brownout is noted in `_CYD/RESTART.TXT`.

## 5c. Settings and logs (Tom, 2026-10-09)

- **One Settings screen** (Tom, 2026-10-12, v0.91.0: "the chooser's
  settings and the game settings ... should be the same (except the game
  specific settings). A player playing a game should be able to adjust
  the brightness and volume, etc, as well as adjust the game tweaks"):
  Home's Settings, a game's page's Settings and the Game menu's
  **Settings** tab (was Options) are one screen with the same keys, 2 x 4
  a page with arrows bottom right: Brightness, Volume, Game Sound, Invert
  Colors, Swap Red/Blue, Rotate 180, Recalibrate Touch (resistive
  screens), Asset Viewer 1:1 / 1.5x (480x320, not in the game - the game
  is always 1:1), Logs (in the game: the Logs tab's screen), then the
  engine comforts (all games), then the game's own keys where a game is
  in hand - Item Fixes for a game with fixes (Pool) - and the version,
  board and memory lines. Every switch has its "?". Bottom: As Original /
  All On (the comforts), and Dev Tools (a game's page) or Back to Game (in
  the game). The keys before v0.91.0, kept below for their history:
- Settings keys (2 columns; 3 rows on 320x240, 4 on 480x320; more than fit
  go on further pages, arrow keys bottom right): Brightness (a slider in
  one key's space, "Brightness nn%" in small text above it; tap a point or
  drag), Logs, Sound (v0.40.0: "Sound: Tandy" - the default -, "Sound: PC
  Speaker", "Sound: Off", a tap goes round them), Volume (a slider like
  Brightness), Invert Colors, Swap Red/Blue (red, green and blue blocks
  named in them, to see whether a swap is needed), Rotate 180, Recalibrate
  Touch, Asset Viewer 1.5x (480x320). Sound is Tandy from the start,
  speaker or not (Tom, v0.40.1: v0.40.0's first-start question "Is a
  speaker plugged in?" hung the 3.2" board and is gone). The bottom line: version, board,
  free memory, largest block, PSRAM ("no PSRAM" on all five boards).
- Logs: `_CYD/SCAN.TXT` (Card Scan), `_CYD/RESTART.TXT` (Restarts; since
  v0.103.1 a crash's line names where it happened - the task, the program
  counter and the backtrace from the core dump, for decoding with the
  build's .elf),
  `_CYD/ERRORS.TXT` (Errors: every error the Play Test showed, with the
  time since power-on) in a scrolling box: a tap in its top half goes up a
  page, bottom half down a page, dragging scrolls; long lines wrap; the
  last 32 KB of a longer file. They come off the board on the card, or
  by Send Logs (below).
- WiFi and emailing the logs were in v0.21.0 and taken out in v0.22.0
  (Tom): WiFi took ~23 KB of RAM for good, and the games need every KB.

**The memory log** (Tom, v0.85.7): Logs has a fourth tab, **Memory**
(`_CYD/MEMORY.TXT`; the session before as MEMORY.OLD). One line a moment
worth knowing - start-up, every screen change, each area's script, a
fight's start and end, saves and loads, every "not enough memory", a line
a minute while playing, and "LOW" when the largest free block drops under
24 KB: "[h:mm:ss] what: free N KB, largest block N KB, lowest N KB"
(largest block = the biggest single piece a load can get; lowest = the
least free memory since the board started).
v0.86.1 (Tom's 4.0" log: a fight left 24 KB, then 8 and 1 KB mid-fight;
sounds, spell names and pictures failed): a fight takes ~12 KB less (the
fighters as many as it has, the message pages' lines in one pool), its
magic tables go when it ends, and when a load would fail the engine first
drops what it can read again (the sound playing; while exploring, the
healing words and magic tables) - "made room for X: dropped Y" in the
log; while exploring with the largest block under 32 KB it does that by
itself every 5 s. A sound in a fight with under 10 KB in one piece is left
out ("a sound left out: memory low"); a sound or spell names that can't
get memory say so in the log instead of failing quietly.
v0.87.3 (Tom): a fight keeps room for 8 more fighters to join (summons,
allies arriving in stages - none in Curse or Pool, likely later); a fight
holds the sound output from start to end so Hit and Miss always play, and
outside fights a sound left out for memory is never Hit or Miss.
v0.87.0: the game screen's canvas is 4 bits a pixel (EGA's 16 colours) -
32 KB instead of 64 KB, the biggest single block the engine has; only the
Resource Test's 256-colour pictures switch it to a byte a pixel for as
long as they show. Checked: the playsim's snapshots of the title, demo,
walking, a fight with its pictures in flight, View, the icon editor, the
camp, Curse's ending and Pool's title / walking / wilderness are byte for
byte the same as with the 8-bit canvas.

**Send Logs** (Tom, 2026-10-12, v0.86.0: "a quick and easy way to send
logs", as in CYD-Classic-Games): a key at the Logs screen's header right.
It packs the newest lines of Errors, Memory, the session before's Memory,
Restarts and Card Scan (shares 25 / 25 / 20 / 10 / 20 %, what one doesn't
need goes to the next) under a header (board, firmware, memory now) into
one QR code: `engine/log_pack.*` (from CYD-Classic-Games: raw DEFLATE,
fixed Huffman, 4 KB window, then base43 - QR's alphanumeric set minus
space and %), the code = byte segment
`https://tomtombombadil.github.io/CYD-BYOG-PoolRadSeries/l/#` +
alphanumeric segment, ECC L, Nayuki's qrcodegen
(`engine/third_party/qrcodegen.*`, MIT). The code fills the screen's
height at the left (modules 2 px at least, a 3-module white edge: version
24 on 320x240 - about 5 KB of log text -, 34 on 480x320 - about 11 KB);
the most text that fits is found by trying (all of it, else a search).
Beside it: "Point your phone's camera at the QR code to get a link that
sends the logs to the developer." (Tom's words from Classic Games), how
much went in, Back. The page `web/l/index.html` unpacks it in the browser
(DecompressionStream "deflate-raw") and offers Email the Logs (mailto
cyd.classic.games.logs@gmail.com, subject from the Board / Firmware
lines), Copy, Download logs.txt, with a "what were you doing" box. Its
buffers (about 25 KB + DEFLATE's 16 KB) are on the heap only while it
packs; the code itself (~1.6 / 2.9 KB) while it's shown. A Send Logs line
goes in the memory log.

**Logs in the game** (Tom, 2026-10-12, v0.86.0): the Game menu has a
fifth tab, **Logs** (the tabs now in the small text), opening the Logs
screen (Errors first) with Send Logs; its Back returns to the Game menu.
While it's open the game screen waits on the card (`frame::park`, as for
the journal book), so the logs and the QR code get its memory. A log that
can't get the memory says "Not enough memory to show this log."

## 5d. Boards and games (Tom, 2026-10-12)

- **Classic CYD boards** (ESP32, no PSRAM - the five boards now): the
  original series complete and playable - Pool, Curse, Secret, Darkness.
  Darkness (and Treasures, later) in **their own EGA mode**: their setup
  offers EGA / Tandy / VGA, and the original in EGA shows the same VGA
  pictures in 16 dithered colours (checked in DOSBox) - the engine does
  what that mode does. Unlimited Adventures: not on these. Dark Queen
  (Tom, 2026-10-12): to be tried in its own EGA mode (an install choice).
- **ESP32-S3 / ESP32-C5 boards with PSRAM (v0.88.0, untested until Tom
  runs them)**: the 4.0" ST7796 480x320 capacitive S3 board
  (`cyd40s3_st7796_cap`: ESP32-S3R8, 8 MB PSRAM; FT6336 touch - no
  calibration, Settings has no Recalibrate Touch; SD/MMC 4-bit card;
  sound by I2S to its ES8311 codec + amplifier, enable GPIO 1) and the
  2.8" ST7789 resistive C5 board (`cyd28c5_st7789_res`: ESP32-C5, 8 MB
  PSRAM; display, touch and card share one SPI bus; sound as I2S PDM on
  GPIO 26 into the amplifier's filter - the C5 has no DAC). Blocks over
  4 KB go to PSRAM by themselves (Arduino's malloc); later the VGA games
  run there in 256 colours (the 8-bit canvas). Flasher entries carry the
  chip (ESP Web Tools' chipFamily).
- Board facts: the Project's claude/board_facts.md; the games' needs:
  claude/other_games_facts.md.

## 6. Milestones

1. **Library and asset viewer** (v0.1.0, this build). Finds the games on the
   card, lists DAX files and blocks, draws EGA pictures (all frames), hex-dumps
   the rest. Proves: SD on every board (incl. the 2.8" with software touch),
   the DAX and picture decoders on Tom's real files, 1:1 vs 1.5x on 480x320.
   Box art (Tom, 2026-10-12, v0.102.0): with the Steam / SNEG Launcher
   folder on the card (/GOLDBOX/Launcher), a game's page shows its box art
   (`Launcher/Sprites/Backgrounds/Back-NNN.jpg`) filling the card, the
   name centred over it; made once by the scan at the page's size, as the
   icons (`engine/jpeg_thumb.*`: the progressive JPEGs read at 1/4 size from
   their DC and first luma AC scans - a block's four 4 x 4 quarters come
   from coefficients 0, 1, 2 and 4; the chroma from its DC, blended; then
   scaled to cover the card). v0.103.0 (Tom's pick from mockups): the art
   full width, top-justified, under a header with Rescan / Settings (36 px
   on 320x240, 40 on 480x320); the page's left / right thirds page through
   the games round, the middle opens one; faint triangles at the sides.
   v0.103.1 (Tom, S3: the triangles came only with the picture): the
   triangles are drawn first; a picture file that can't be saved or read
   is noted in the Memory log. v0.103.2 (Tom: "They're to be made and
   saved to the SD card for near-instant access"): the page never makes a
   picture - the scan makes them, and a start whose card lacks this
   screen's (an older scan, a board of another size) makes the missing
   ones first ("Preparing Pictures", added to the scan log; a failure
   stays up as the scan log); a page without its file shows the name.
   The chooser (a game tapped in the library; Tom, 2026-10-10, v0.53.0):
   four big keys filling the screen under the title bar - **Resource
   Test** (the game's DAX files a page at a time, then their blocks: what
   the chooser used to show), **Screen Test**, **Walk Test**, **Play
   Test**, each with a small line under its name. A game the engine can't
   play yet shows a note and Resource Test only; .TLB / .GLB games the note.
   v0.72.0 (Tom, 2026-10-11: Curse playable): the game's page is two big
   keys, **Play** (the former Play Test) and **Settings**. Settings (one
   set for all games, saved with the engine's settings) holds the engine
   comforts' switches - Game Font (v0.73.0; Original / Clear / Darkness since v0.74.0, Clear Caps
   since v0.84.0), Icon Gallery, Large Icon, Journal Pop-Up, Tap to Stop Demo (v0.84.0; was Demo Tap
   Stops), More Characters, Icon Portrait (v0.84.0), Item Fixes (v0.90.0) - and Game Sound (Tandy /
   PC Speaker / Off; more than 8 keys: pages, arrows bottom right since v0.90.0), each
   key lit when on, with a circled "?" at its right (Tom, v0.84.0) that opens what the option does
   (the next tap closes it; Game Font's shows a line in each font, the player's own fonts read from
   the card); below them **As Original** (every comfort off; then the
   key reads All On) and **Dev Tools** (Resource Test, Screen Test, Walk
   Test; their Back returns there). The in-game Game menu's Options tab
   shows the same switches. v0.91.0 (Tom, 2026-10-12): **one Settings
   screen** everywhere - section 5c. A game the engine can't play yet: its note
   and Settings (its files under Dev Tools).
2. **The game's look**: its font (v0.2.0), screen frame (v0.3.0, Curse - the
   viewer's Screen Test), title sequence + credits, text windows and the menu
   line (v0.4.0). Done for Curse; the other games get theirs on their turn.
3. **3D view and walking**: GEO + WALLDEF + 8X8D drawing, turning and stepping by
   touch, auto-map. v0.7.0: the viewer's **Walk Test** (Curse) walks every 3D
   map the area scripts load, with the exploring keys, the AREA view and (on
   480x320) the Companion map; walls and doors only, no events yet; locked doors let the party through
   with a note (Tom: for testing); tapping a square in the AREA view or on
   the Companion map moves the party there (v0.7.3, Tom: to reach parts of a
   map cut off by walls).
4. **ECL script engine**: events, text, menus, pictures, area changes.
   v0.8.0: the viewer's **Play Test** (Curse) - a new game run by the game's
   own scripts from the opening: events as the party walks, text, menus,
   pictures, the clock. v0.9.0: the on-screen keyboard, animated event
   pictures, the wilderness map and travel between areas. Still to come:
   PoolRad's own opcode table; the rest needs a party (M5).
5. **Party**: characters (create / load), inventory, camp, shops.
   v0.14.0: the Play Test opens at the games' party menu (v0.53.0: after
   the title sequence, as the games do); Load Saved Game
   reads a saved game and its party (the GOG release's sample party), the
   party list on the menu and the exploring screen, the scripts see the
   selected character. v0.15.0: View Character. v0.17.0: items, the rules
   that follow them (AC, THAC0, damage, movement), the shop (buy, ready).
   v0.18.0: saving (Save Current Game, Camp -> Save) and loading back.
   v0.19.0: Add / Remove / Drop, Pool / Share in shops. v0.20.0: Create
   New Character (BEGIN now needs a party). v0.21.0: the save folder's
   paths fixed on the board (saves, characters), errors stay until tapped.
   v0.21.1: memory for the Play Test again (WiFi's static RAM had taken
   the room; the asset viewer's tables now live on the heap only on its
   screens, the Play Test's state is in smaller blocks). v0.22.0: WiFi /
   email out again, loop() back to 8 KB of stack (deep decoding on a stack
   of its own while it runs), 4 SD file slots: ~27 KB more free memory
   than v0.20.0. v0.25.0: the items menu (Use, Trade, Drop, Halve, Join,
   Sell / Id in shops). v0.26.0: the temple, Take, Appraise. v0.27.0:
   training halls. v0.28.0: camp Magic - Memorize - and Rest. v0.29.0:
   Cast (camp and exploring) and Display; effects run out with game time.
   v0.30.0: Alter (Order, Drop, Speed) and Fix. (v0.60.1: Encamp runs the
   area's before-camp script - entry 2 - first, as the games do.) v0.31.0: Scribe, and
   training's new magic-user spell. v0.37.0: the scripts and the party
   (LOAD CHARACTER, WHO, ROB, DAMAGE, ADD NPC, DUMP, DESTROY ITEMS, FIND
   ITEM / SPECIAL, SPELL, PARTYSTRENGTH, CHECKPARTY; character fields
   written). v0.38.0: random treasure items, PROGRAM 9 (camp) / 3.
   v0.39.0: items that work while readied (effects, alignment-keyed).
   v0.42.0: Modify Character. v0.45.0: Human Change. v0.54.0 (facts: the
   Project's claude/alter_icon_facts.md): Alter's **Icon** - the combat icon
   editor (also after the name in Create New Character): old / new icons
   ready and action on COMSPR 25, Parts (Head / Weapon) / 1st-color /
   2nd-color (Weapon Body Hair-or-Face Shield Arm Leg) / Size, Next Prev
   Keep Exit, "Is this icon ok?"; icons now merge head and body as the
   program does (where both have a colour, the two OR'd); **Pics** - Pics on
   / off and Animation on / off (area word 0x4BFF; Animation off: event
   pictures show their first frame only; Pics itself changes nothing in
   Curse). The **demo** (claude/demo_facts.md): Demo on the version line
   (or 30 s untouched; 10 s after a demo) runs ECL1 block 0x52 - area 1,
   speed 9, no party menu, the three NPCs it adds fight the dragons
   computer-run, key waits pass at once, no experience / treasure; an
   all-NPC party wins while one stands; PROGRAM 3 ends it, the title
   again. v0.64.0 (Tom): a tap or key stops it and the title comes back
   (the original's can't be stopped; switch `CYD_DEMO_TAP_STOPS`, section
   12). v0.56.0 (facts: claude/import_facts.md): Add Character ->
   **Pool** - Pool of Radiance characters (*.CHA, then *.SAV of exactly 285
   bytes, player characters) from Curse's save folder (as the original)
   AND from the player's own Pool of Radiance folder on the card (the
   engine's convenience: nothing to copy; Tom to confirm); a saved game's
   shows "NAME from saved game A". Converted as the original does: the
   fields Curse reads, stats held to the race / sex limits, Animate Dead
   out of the spell book, exactly 300 platinum, no items (Curse never
   reads Pool's .ITM), a NAME.CHA's racial effects from NAME.SPC (a saved
   game's are lost, as in the original), class values recomputed; levels
   and experience kept. The party's rules as for Curse characters, and the
   original's duplicate rule (same name and "mod id" 0x126). Hillsfar:
   not supported (Tom, 2026-10-10: not one of the engine's games) - the
   menu word stays, "Hillsfar characters can't be added." (v0.63.1). v0.58.0 (Tom - beyond the original, which lists only
   .GUY files): Add Character -> Curse also lists the members of saved
   games (CHRDAT<letter><n>.SAV, their .SWG items and .FX effects), shown
   as "NAME from saved game A" - so GOG's sample party can be added one by
   one; .GUY characters first by name, then game A, B ... in party order
   (48 lines at most). v0.59.0 (Tom): the icon editor's **big preview**
   (section 11's CYD_BIG_ICON_PREVIEW) and the Game menu's **Options** tab
   (320x240: Large Icons On / Off; its tabs read Journal, PDF, Sounds,
   Options). v0.57.0: **the stats as they stand** (`rules::stats`, run
   with every recalculation): from each stat's own value (0x10 + 2i; for
   18/xx 0x1D) to the one in use (0x11 + 2i; 0x1C) by readied items that
   work on stats (0x80 + code in the third effect byte: 5 giant strength
   18/00-24, 8 a stat + 1 under 18, 2 dexterity, 6 / 10 / 12 / 13 others) and
   effects (Strength, the giant strength potion, Enlarge, Friends - the
   original adds 2d4 to Charisma -, Feeblemind 3); camp spells **Enlarge**
   (18 to 22 by level), **Reduce**, **Friends**, **Strength** (d4 / d6 / d8 by
   class; fighters past 18 get 18/xx).
6. **Combat.** v0.32.0: monsters, the battlefield (indoors from the 3D map,
   outdoors random), placement, the combat screen and icons, rounds and
   initiative, the player's Move / View / Aim / Quick / Done, attacks and
   damage, dying and bleeding, the computer's turns, the end (experience,
   the result for the script, the treasure). v0.33.0: Cast and Turn in
   fights. v0.34.0: the computer's spells, morale and surrender, lightning
   bolts, stinking clouds. v0.35.0: Use (magic items and scrolls) in and
   out of fights, the computer's items. v0.36.0: the computer shoots and
   throws. v0.39.0: backstabs, sweeps, the free attacks' facing rule.
   v0.41.0: missiles and spells in flight, magic hits, the fallen's skull,
   the computer's weapon choice. v0.43.0: clouds that stay on the field.
   v0.44.0: Charm Person. v0.46.0: cones (Fear, Cone of Cold).
   v0.47.0: Cloudkill. v0.48.0: Charm Monsters, unarmoured magic-users
   staying back. v0.49.0: coughing in stinking clouds. v0.55.0 (facts:
   the Project's claude/spell_facts.md, checked against the original code
   where coab differs): Silence 15' (no spells or items for the silenced and
   those next to them, "is silenced" at their turn), Snake Charm, Cure
   Blindness, Bestow Curse (a touch; attacks and saves -4), Blink (attacks
   miss once it has acted), Poison, Sticks to Snakes (turns lost while the
   snakes outnumber its attacks), Slay Living, Entangle (outdoors; no
   moving, 24 rounds), Faerie Fire (the original's +2 to the stored AC),
   Invisibility to Animals (an animal's attacks -4), Fumble (the
   original's two saves), Ice Storm, Feeblemind; blinded fighters (attacks
   -4, easier to hit, saves -4); Cone of Cold is cold + magic (coab: acid);
   the hold spells take persons only (but Hold Monsters); Charm Monsters
   as the original (the last picked, non-persons and large ones
   unaffected). The group-by-size aim (Faerie Fire, Charm Monsters)
   is a radius of 1 for now (one by one since v0.65.0).
   v0.61.0 (facts: the Project's claude/monster_fx_facts.md, checked
   against the original code; `combat::MonFx` in the profile): **the
   monsters' special abilities.** After a hit: poison bites (a save or
   killed), the thri-kreen's paralysing bite (2d8 rounds), the dracolich's
   paralysing touch (the fight) and chill (2d8 cold + magic), Tyranthraxus'
   fire (2d10), the ankheg's acid bite (1d4), engulfing (both swings: held
   fast, suffocating after 2d4 turns; let go at the mound's next turn, as
   the original does), the owl bear's hug (a natural 18+: held fast); the
   salamander's heat (+1d6). Defences against blows: only magic weapons
   (the rakshasa: +1-2 half), +0 weapons useless unless a 4+ Hit Dice
   monster swings them, half damage, piercing weapons 1 (the vegepygmies;
   a bow's arrows count as not piercing, quarrels do), the blessed quarrel
   slays the rakshasa, missiles from a +0 weapon dodged / 60% dodged
   ("Avoids it"). The damage routine (spells, breath, specials): fire /
   cold / electricity immunity, lightning healing the shambling mound 8,
   fire / cold a save for none (else half), the efreet's -1 a die, resist
   fire / cold half and +3 on saves; magic resistance 50% / 15% by the
   caster's level, no magic at all, the minor globe (spells below level 4),
   sleep / charm / paralysis immunities, the elves' 90% against sleep and
   charm; poison / paralysis immunity (every such save made). The troll
   regenerates (3 a round, 3 rounds after it was hurt) and gets up again
   3d6 rounds after falling to anything but fire or acid ("stands up and
   grins"). Invisible from the start (Alias, Dracandros, Dragonbait: -4 to
   hit them, the computer can't pick them, attacking or casting shows
   them; seeing the invisible ignores it); protection from evil / good is
   +2 on saves against an evil / good one acting (the original's -2 to hit
   is lost before the roll - removed here too), Dragonbait's covers those
   next to it. The computer's special actions at the start of its step:
   the black dragon's acid breath (3 a fight, a line of 6, not over a
   friend; its maximum HP, a save for half), the dracolich's fire breath
   (3 lines of 9, friends or not), the hell hound's fire (7), Tyranthraxus'
   thrown lightning (the first 4 rounds: 16d6 to the one aimed at, a
   second 16d6 down the bolt), the giant slug's and the ankheg's acid
   spit, the hooded medusa's stone gaze (a readied mirror sends it back),
   the beholder's rays (disintegrate 2 squares, stone 3, death 4, wounds
   5) then Fear, Slow, Sleep. The held lose their turns at once. The
   original's quirks kept: the dracolich's paralysing gaze never fires,
   the owl bear never squeezes, Mogion's cloak does nothing. To come: the
   lightning's bounces off walls - done in v0.65.0.
   v0.65.0 (claude/curse_finish_facts.md): **Lightning Bolt as the
   original** - one roll for the one on the square aimed at and all on
   the path, each saving for half (electricity + magic); 7 squares
   whatever the level (the item's stroke 3, Tyranthraxus 10 with a second
   roll for the path); the bolt stops on each creature, ends at the field's
   edge, bounces off walls indoors only (back toward the caster; within 4
   squares of the caster the bounce costs 8 more, not for Tyranthraxus),
   the original's byte sums after a bounce (odd return lengths), anyone hit
   again on the way back - the caster too; its picture along each segment
   (`combat::bolt_path`, checked against a model of the original on 400
   random fields). **Coughing**: attacks on a coughing fighter use its rear
   AC 2 worse (worse than 10 becomes 10; a blind one's -4 lost - the
   original's). **Faerie Fire / Charm Monsters** pick one by one (no
   budget shown, as the original): Faerie Fire's budget the caster's level
   by size (1, 2 / 3 -> 2, 4), Charm Monsters 2d4 by Hit Dice (1, 2, 4, 8);
   picking ends once 2+ pass the budget; Exit / Esc casts with those picked
   (none: the spell is used up all the same); the computer picks at random,
   repeats lowering the budget.
   v0.62.0 (spell_facts.md 2): **Enlarge** in fights (Strength by level,
   only when more: "is stronger"), **Reduce** (a failed save takes Enlarge
   away; nothing said otherwise), **Remove Curse** in fights (Bestow Curse
   "is Cured" / "is un-cursed", else the first cursed item comes off: "has
   an item un-cursed"), **Confusion** (the first 2d8 in 3 squares, a save
   or "is confused" for 2 + level rounds; each turn d100: 1-10 "runs away"
   (afraid 10 rounds), 11-60 "is confused" (the turn lost), 61-80 "goes
   berserk" (a round on the side against the nearest creature), 81-100 "is
   enraged"; then a save at -2 ends it), **Dispel Evil** (the evil's
   attacks on the caster -7; its first slot's hits: "is dispelled" (gone)
   or "resists dispel evil"), **Fire Shield** ("flame type: Hot Cold",
   Esc: "Abort spell? Yes No"; the computer: d10 over 5 hot; hot "is
   protected", +2 on saves against cold, fire doubled when not saved; cold
   the other way round; both zap: one hitting it from next to it "gets
   zapped" for twice the blow, magic), **Dimension Door** (an empty
   square within 3 x level; a hug or engulfing lets go: "teleports"),
   **Dispel Magic** (the first one in the area: each effect but data 0xFF
   50% +5 a level the caster is above / -2 a level below it, a pass for
   each one in the area - the original's; "is affected"; the clouds next
   to the square aimed at likewise against their caster's level, a failed
   one resists from then on), the magic-users' Bestow Curse (no effect, as
   in the original).
   v0.63.0: **Slow Poison** in fights (the poisoned get back up with 1 HP:
   "is affected", "gets back up"; a hit point every 10 minutes, down to 1;
   when it runs out still poisoned: "dies from poison" - in fights and out
   of them, `rules::poison_clock`), **Spiritual Hammer** (fights and camp:
   the hammer - type 20, words "Hammer" "Spiritual", +1 - in hand while
   the effect lasts, back each round if dropped, "Gains an item"; gone when
   it ends), **Animate Dead** (the computer's, from items / monsters: up to
   its level of the dead - characters, not monsters - back up on its side,
   undead, no spells, movement 6: "gets back up" / "stands up and grins",
   "is animated"; after the fight they collapse, dead, on their own side
   again), **Restoration** (fights and camp: a lost level back - the hit
   points lost with the levels shared by them, the lowest class that may
   rise up one: "is restored"), **Dispel Magic** in camp.
7. **All four games**: the per-game differences (PoD's VGA graphics), party
   transfer between games.

Order of the games (Tom, 2026-10-06): **Curse of the Azure Bonds first** - its
engine is the best documented (coab), and most of that work carries over -
then **Pool of Radiance**, then Secret of the Silver Blades and Pools of
Darkness. Milestones 2-6 are built against Azure Bonds' files.
8. **Sound and companion features** (auto-map panel, journal entry lookup,
   rest-until-healed, re-memorise spells - as options). v0.40.0: the
   games' sound effects (Curse), Tandy 1000 (default) or PC speaker; the
   Engine Menu's Sounds tab plays each to compare (with Tandy / PC
   Speaker keys and a Volume slider). v0.40.1: no first-start speaker
   question (Tandy from the start), about 2.5x louder (Tandy 5x). v0.40.2 (Tom: Tandy barely
   audible at 100%): the speaker's square wave at the DAC's full swing; the
   chip's voices at full scale with 1.2 dB volume steps (not 2 dB: fades stay
   audible), 2x gain into a soft limiter - Tandy now about as loud as the
   speaker (rms ~85-120 of 127; v0.40.1 ~40-60). Louder than this needs a
   bigger speaker or amplifier. Later games' AdLib /
   Sound Blaster: when we get there.

## 6b. Pool of Radiance (Tom, 2026-10-11: started after Curse)

The second game (build order: Curse, Pool, Silver Blades, Darkness). Facts
from the player's own files: the Project's `claude/poolrad_facts.md`
(program, DAX files, saves, the character record, scripts) and
`claude/poolrad_screen_facts.md` (how its program draws its screens).
The Steam / SNEG game files are the same bytes as GOG's (POOL.CFG aside),
so one profile covers both.

How it fits the engine: one more `profile::Profile` (START.EXE 65,231 /
68,592 bytes), and `profile.variant` for what Pool's older program does
otherwise (all zero = Curse):
- **Frames**: no frame tables - boxes drawn by code: block 202's tile 20
  at every corner and junction, 21 down the sides, 22 along the tops and
  bottoms (`layout`'s box mode; the same screens' rows and columns).
- **Credits**: 29-byte print calls (no background push; `printcalls`).
- **Title**: picture 1 (5 s), picture 2 (5 s) with lightning on colour 1
  now and then (`engine/lightning`, the viewer flashes the palette), the
  credits (10 s). No version line, no demo.
- **3D view**: no SKY.DAX - four flat bands (sky / ceiling colour through
  the DS 0x0C26 table, a white line, a light grey band, a brown floor;
  `view3d::Bands`).
- **Scripts**: Curse's opcodes 0x00-0x3D (ECL CLOCK 1 operand), loaded at
  0x9900 with Pool's own memory map; `ecl::vm_address` moves every address
  to Curse's map as instructions are decoded, so the script machine and
  the front end keep one map.
- `variant.unfinished`: while Pool is being built the game page has no
  Play key; Dev Tools has the tests **and the Play Test** (as far as it
  goes).

Steps (each pushed when it works; Curse stays as it is):
1. v0.75.0: the profile, frames, title (with the lightning), credits, the
   Screen Test and the Walk Test (21 of Pool's maps found from the
   scripts; the flat bands), script decoding with Pool's map.
2. v0.76.0: the party menu (11 entries), saved games (13,137 bytes: no
   wall-set block; after a load the walls come from the script's own LOAD
   FILES / LOAD PIECES), POOL.CFG's last folder line (GOG: the game's own
   folder), the 285-byte character record (`party::from_pool` / `to_pool`:
   moved into Curse's layout and back, every field Pool has - a load and
   save gives back the same bytes but the record's in-memory pointers),
   NAME.CHA / .ITM / .SPC, Add Character straight to the list (no "Add
   from where?"), Remove without an Overwrite question, View Character,
   items; BEGIN goes into the saved game's area. Words Pool keeps in its
   program (not GAME.OVR) are marked `text::kInProgram` in the profile
   (`text::Words` reads both).
3. Scripts in play (v0.76.0, most of it): a new game starts in ECL3
   block 0; Pool's ECL CLOCK (one operand: the clock's slot 1, as its
   program's handler); CALL 0x2C90 (the view redrawn) and 0xBA03 (a
   sound), other CALL addresses nothing (as Pool's handler; Curse's log
   them); the view colours scripts set (0xC059 floor, 0xC05F line / band:
   `view3d::pool_bands`, back to the defaults as each area starts); the
   character fields Pool's program gives scripts (Int, race, class, the
   magic-user's level, sex and 0xE4 are plain script memory there, as in
   the original - so its race checks in Podol Plaza and the Temple of Bane
   never match; v0.85.1 (Tom): race is given to Pool's scripts after all,
   so those checks work - SPEC 12); monsters' 285-byte
   records. The sweep (`sweep.cpp events`, GAME=1) walks every square of
   all 30 area scripts with no problems. Fights: step 5.
4. v0.77.0: Create New Character as Pool makes one (create_facts.md):
   its races / classes (no half-orcs; humans C, F, MU, T), the best of six
   3d6 (no +1), HP (one roll, at level 1 at least 2/3 of the die), gold
   only (the classes' money dice x 10, shared), no starting experience and
   no silent training, a cleric's every 1st-level spell, a magic-user's
   Detect Magic / Read Magic / Shield / Sleep, dwarves' and halflings'
   poison effect; "Keep this character? Yes No" (No rolls again), the name,
   the **portrait picker** ("Head Body Keep": heads 1-14, bodies 1-12 from
   HEAD3 / BODY3 by the program's tables), the icon editor, saved as
   NAME.CHA (no question) and listed in CHARLIST.TXT. Pool's rule tables
   have their own shapes (`classes::Layout pool`: THAC0 11 levels, saves
   class x 45, 56-byte class records whose spell slots are the TOTALS
   for levels 2-9, race classes 12 bytes, the spell table's magic-user
   class 1, no hit dice limit, flags / masks at DS 0x5D6 / 0x5DE); the
   GOG sample party's THAC0, saves, slots, thief skills and class flags
   come out exactly as the game stored them. Training: Pool's level caps
   (cleric 6, fighter 8, magic-user 6, thief 9 by its experience table),
   no magic-user race limits. Rest: Pool's keys (Rest daYs Hours Mins
   Inc Dec Exit). Not copied (quirks read from the code, SPEC 12 if ever
   wanted): the new thief's Dex-0 skills in the .CHA until joining, a
   magic-user with no spell left to choose at training getting 1 HP.
   View Character's portrait (v0.84.0): HEAD3 / BODY3 in Pool's box top
   right (cols 28-38, rows 1-11) when viewed from the party menu, as the
   original (its View: DS 0x49F5 portraits on and 0x5376 = 3); with the
   Icon Portrait comfort wherever View is (section 11).
   v0.85.0: **Alter -> Pics** is Pool's "Monsters on/off  Portraits on/off
   Exit" (its ovr16 +1D02; the area word 0x4BFF as Curse's Pics /
   Animation, saved with the game: bit 1 the monsters' event pictures while
   exploring the 3D areas - off: the view stays -, bit 0 the portrait on
   View Character). **Modify** as Pool's program: experience 0 only; a
   stat taken below the race's or the class's minimum goes to the race's
   minimum; no multi-class cleric Wisdom 13; Keep marks the record
   "modified" (its 0x85).
5. Combat (v0.78.0, started; the Project's claude/poolrad_combat_facts.md):
   Curse's fight code with Pool's tables (the ground table at DS 0x2744 -
   Pool's own outdoor pictures and costs -, placement, turn undead at
   0x0447), its effect numbers (the same number often means something
   else in Pool: those Curse rules are off for it), its spells (Magic
   Missile: (level + 1) / 2 missiles of 1d4 + 1, always hitting - the AD&D
   rule, Tom 2026-10-11 (v0.84.0; v0.78.0 had level / 2 as its code was
   read); Lightning Bolt 8 squares; Heal and the later spells not in Pool),
   camp spells, the temple's cures (Curse's prices, no Heal), gem and
   jewel words, no end-of-fight effect list (its program has none - effects
   run out by their own time). Fights in the three wilderness areas (25-27)
   are outdoors on a field made Pool's way: the place under the party on
   the program's 44-wide overland map (DS 0x3648) gives the field's flags
   (its code's compare chain, `combat::pool_terrain`), and its own
   generator lays the stream (always with a crossing), the trees (with
   their tops) and the scatter (`combat::build_outdoors_pool`); every other
   area fights on its 3D map. Fixed for Curse while comparing (its own
   program's lists): Cure Disease takes effect 0x32 (not 0x20), the fight's
   end takes 0x89 and 0x8B away (not 0x4D), hammers spin in flight (not
   glaives). v0.79.0: **Pool's own monster specials** (traced in its
   program: poolrad_combat_facts.md 4 and 4.1): level drain (wight 1,
   spectre and vampire 2 - "is drained a level / N levels": its share of
   the hit points, the highest class level off (its experience the most on
   a tie), the experience the new level needs, THAC0 / saves / slots /
   attacks again; drained at 1st level or animated: "is dead forever"); the
   mummy's touch ("is Diseased" - the rot: each 30 days "Rots...", Charisma
   - 2, after d6 months "Dies rots away"; Cure Disease ends it), its fear
   (once a fight, paralysed d4 rounds; humans +2, a side of 6+ +1) and a
   flask of oil (3d8; fire and magic a point more a die); the ghoul's
   paralysis (63 rounds, not elves), the drider's (d9 + 10, -2); the
   stirge (attached: "Sucks some Blood" 1d4 at its turns until full), the
   mantis ("is held fast", a 3d4 bite at its turns; Strength 19+ breaks
   free), the tiger ("Rakes..." twice 2d4 when both claws hit), the wight
   (only magic or silver hurts it) and the wraith (+0 silver half, other
   +0 nothing), the vampire's charming gaze and its escape at 0 hit points
   ("Turns gaseous and escapes": fled, no experience), Tyranthraxus's
   lightning breath (3 a fight, its maximum hit points along 10 squares,
   after the first round half its turns) and dragon fear (every round:
   Hit Dice 0-3 paralysed for the fight, 4-5 cursed), poison at +4 / +2,
   holders get no free attacks, magic resistance Pool's way (the caster's
   level counts against it). Guesses (noted in the facts): the mummy fear's
   "side of 6" is those standing; the lightning breath aims as the
   computer's Lightning Bolt. v0.80.0: **the races' fight bonuses by
   name** (Pool's code, not Curse's class bits): dwarves +1 to hit a man-
   sized humanoid named ORC, ORC LEADER, GOBLIN, GOBLIN LEADER, HOBGOBLIN,
   HOBGOBLIN CHIEF, GAGOOL or MACE, gnomes one named KOBOLD, KOBOLD
   LEADER, GOBLIN or GOBLIN LEADER; dwarves and gnomes are hit at -4 by a
   size-2 humanoid when the one they fight is named OGRE, OGRE LEADER,
   TROLL, FIRE GIANT or HILL GIANT, gnomes by any BUGBEAR or GNOLL (the
   names read from the player's program). v0.81.0: **random treasure**
   (TREASURE 0x80 + n) as Pool's own code makes it: Curse's weapons,
   armour and scrolls (3 spell bands of its own), a 20 on the plus's d20
   makes +3 (bracers AC 2); 93-98 a d16 of its kinds (potions, rings,
   necklaces, wands ...), 99-100 type 0x45; each kind's ready-made item
   from its program's 19 rows (some by a die); its quirks kept - a weapon
   roll of 48 / 49 repeats the item before, and the javelin of lightning
   reads its words across two rows (a nameless "of Elvenkind" javelin).
   v0.82.0: **the ending** (PROGRAM 8, its ovr19): the dragon's words in
   the text box (three pages: 3, 6 and 4 lines, read from GAME.OVR),
   FINAL<area>.DAX's pictures in the left box (the dragon, then the
   party's figures by its size) and its animations, each frame (speed + 3)
   x its delay in hundredths of a second, played 5 x (10 - speed) / 2
   times round (the last once), no key; then a pause, the screen back and
   **the script goes on** (Pool's PROGRAM 8 doesn't end it, and its
   PROGRAM 3 does nothing - its handler knows 0, 8 and 9 only).
6. Tests (v0.85.0): the sweep's events (every block, every square),
   monsters (every MON file fought), saveload (Pool's area 3 entry sets
   0x4C05 / 0x7F79 / 0x7F7D itself: the test uses another word) and
   monkey runs in areas 1, 2, 4, 6, 8, 25 with fight checks (each party
   fighter can walk to an enemy; the fighter whose menu it is is in view;
   the card's 4 file slots emulated in the playsim). **Pool plays** from
   v0.85.0: its game page has Play (`variant.unfinished` off). Tom's first
   fight (v0.84.0, a 3.2") showed two party members and moves that seemed
   to do nothing - not reproduced (all six placed, Move / Aim work); the
   fixes it led to: the view comes back to the fighter whose menu it is
   (after Aim / View looked elsewhere it stayed there, the fighter off the
   screen), and the party menu's words no longer open five files at once
   (two word sources alive together: on the board the fifth failed).
   v0.85.2: **behaviour checks** (sweep.cpp's `behave pool`, built with
   -DBEHAVE_POOL -DBEHAVE_SPELLS, GAME=1; expectations from Pool's own
   tables - a scratch oracle - and the facts docs' formulas): class values,
   Constitution, creation (every race / class / alignment / sex, dice high
   and low), training, Modify, money, spells in fights, the races' foes,
   backstabs, paralysing hits, turning, level drain - 400 checks. What
   they found and fixed (all Pool only): **every ready class trains** at
   once (Curse's one-class pick isn't Pool's); Constitution's hit points
   +1 over 16 / +1 more over 17 for a single-class fighter only (Curse's
   17-25 scale); Modify's most hit points every die high plus the
   adjustment once; Modify's Strength 17 -> 18 first, then a fighter's
   18/01 (not straight to 18/01) and Keep leaves the rolled hit points as
   they are; a fighter's attacks set by its level (back to 2 when drained
   to 6th); training's 1000 gp counts half a gold piece up; Mirror Image's
   data is the images alone (Curse: images << 4 + level) and an image
   takes a spell as that count says; Dispel Magic works on every target
   in its area once (Curse: the first one, as many passes as targets);
   Friends' Charisma at most 25; a backstab is x (thief level / 4 + 2)
   and only in leather or no body armour. **Phlan's walls** (Tom, a 4.0"
   new game: the 3D view empty after the tour): Pool's LOAD PIECES 0x7F
   loads block 0 into set 1 (WALLDEF3 block 0, three parts - area 3's
   scripts all use it); the engine took 0x7F as "no walls" (Curse's).
   v0.104.2 (Tom: "made up of tiny icons ... artifacts sticking out"):
   that block's tiles are 8X8D3 101-103 - Pool's loader takes a
   multi-part block 0 as 10 for the tile blocks (block x 10 + part); the
   engine read blocks 1-3 (other areas' tiles) - checked against the
   original in DOSBox (the gateway, the Temple of Tyr).
   v0.85.3 (Tom, 3.2"): **the Map stays up while walking in Pool** - its
   step scripts clear the text with an empty PRINTCLEAR and finish a tick
   later, which closed the map at once; now the map waits for them
   (`play::walk_pending`) and closes only for something to read or answer.
7. v0.83.0: **the wilderness** (areas 25-27 while their area word 0x4BE6 is
   0; the Project's claude/poolrad_overland_facts.md, `engine/overland.*`):
   the program's 44 x 36 map of icon numbers (the three areas windows on
   it, side by side), 5 x 5 of SQRPACI.DAX's 24 x 24 icons filling the left
   box around the party, the rider (ICON.DAX, a gallop picture each
   redraw, mirrored facing south or west) on its square; special squares
   showing other icons as the area words say (camps gone, the pyramid and
   the town changed); the land healed after the pyramid (the river and
   dead land's colours, the river passable). The arrow keys face north /
   east / south / west and step: the scripts' step entry first (the map's
   edges and the crossings to the next area are theirs), then the square
   unless it's sea, coast or the lake ("The river's stench drives you
   back." until the land is healed), **12 hours a step**, then the
   arrival entry (places, random encounters) even when the step was
   blocked. Menu "Cast View Encamp Search Look" (no Area); the status line
   gives the map's column; pictures put the 3D view's frame over the map;
   fights outdoors as step 5. Mountains don't stop the party (checked in
   DOSBox, 2026-10-12: the original walks into them, 12 hours a step).
8. v0.89.0: **items and the stats they change** (poolrad_combat_facts.md
   2.2 / 3.2): what a readied item's code (byte 0x3E 0x80 + n) does is
   the game's own table (`items::passive`: Curse's codes 0-13, Pool's
   0x80-0x82 / 0x85 / 0x86 / 0x88 / 0x8A / 0x8B the effect in 0x3D with
   data 12, 0x83 Ogre Power - Strength 18/00 through effect 0x26, "is
   stronger" -, 0x84 keyed to an alignment, 0x87 the boulder - "Must have
   Giant Strength" below 19 -, 0x89 the Spiritual Hammer); before, Pool's
   Ring of Fire Resistance doubled spell slots, its Cloak of Elvenkind
   counted as a Girdle of the Dwarves and the Cloak of Displacement did
   nothing. **Item spells**: Pool keeps those over 0x38 0x17 higher (the
   Potion of Healing's 0x55 is spell 0x3E) and takes it off as it uses
   them (ovr20+1B28) - Pool's potions, wands, the javelin and the necklace
   cast their spells (before: nothing). Enlarge / Strength / the giant
   strength potion give their Strength through effects 0x0C / 0x26 (the
   most of them counts; Enlarge and Strength put the effect on either
   way, "is stronger" when it's more; Strength: d4 a magic-user, d6 a
   cleric or thief, d8 a fighter, over 18 a fighter's 18/xx + 10 a point);
   the potion lasts (d4 x 10) + 40 minutes; Keoghtum's Ointment (0x3A)
   takes the poison and Slow Poison away from the poisoned (nothing said)
   and heals d4 + 8 otherwise; the **Manual of Bodily Health** (0x43): "is
   Reading" for a day, "starts to train" for 30 days, "is hardier" -
   Constitution + 1 (20: an hour's healing, else the original's hit
   points); the Spiritual Hammer's item (code 0x89, "Spiritual Hammer"
   +1). Pool's files keep Strength and Charisma as the original's (SPEC 12).
   v0.89.1 (every item of both games checked, sweep's `behave items` and
   `behave itemtest`): Pool's **Two-Handed Sword +1 +3 vs Undead** (its
   effect 0x03: +2 to hit and damage against monster type 4) and the
   **Cloak of Elvenkind** (effect 0x48, the same handler in both games:
   95 in 100 invisible at a fight's start and after each round, as the
   ring) now work; before, the sword was a plain +1 and the cloak did
   nothing. An item with no charges never runs out (as the originals).
   v0.90.0 (Tom): Pool's "Long Sword +2 Flame Tongue" is, in the original,
   a +2 long sword whose Use casts Find Traps (its record has 0x3D 0x16
   and no effect code, where Curse's same sword has the effect 0x06 with
   0x80) - no fire, so a troll it fells gets up again. The **Item Fixes**
   comfort (section 11, on unless switched off) makes it the AD&D sword,
   as Curse's: Long Sword +1 Flame Tongue. Also fixed in both games: the
   Flame Tongue's blows are fire and magic (kind 9, its handler's), so a
   troll it fells stays down and Pool's mummy takes a point more a die -
   the engine had left the kind out.

## 6c. Secret of the Silver Blades (Tom, 2026-10-12: started)

Facts in the Project's claude/secret_facts.md (from the player's files,
the program and the original in DOSBox; nothing from the game in the
repo). Built a chunk a commit, Play off the game page until it plays
(`variant.unfinished`); the Play Test under Dev Tools says it isn't ready
until the party menu is known (`party.items`).

- **v0.92.0 - scripts, screens, maps.** The script interpreter (GAME.OVR
  overlay 4) reads the same operand counts as Curse's but 0x3C (none) and
  the new 0x41 (2): following every block from its entry points, every
  instruction of all 22 blocks decodes. The frame is drawn by code, not
  tables: bars of block 202's tiles 52-59 (`variant.bars`; `layout`'s
  bars mode), so `layout::Tiles` keeps tiles 0-19 and a window of 20
  (`variant.tile_window` 40) in its 40 slots - no more memory. The font is
  8X8D5.DAX's block 201 (`Profile::font_file`). The title (5 TITLE.DAX
  steps), the credits (32-byte print calls in GAME.OVR at 0x379) and the
  version line as the original shows them; the sky colours at DS 0x0AA8.
  Screen Test and Walk Test work (16 maps).
- **v0.93.0 - characters and saves.** Secret's 439-byte record is moved
  into Curse's layout field by field (`party::from_secret` / `to_secret`,
  `Format::Secret`; checked byte for byte on the GOG party - only the
  in-memory pointers differ); what Curse's layout has no place for (spells
  101-117 of the book, the 6th / 7th level slots, flags 2, magic
  resistance ...) is kept in `Character::ext` (34 bytes) and written back.
  Its saves are Curse's without the script's bytes (5,469;
  `savegame::read / write` with `script` false); characters' items in
  .STF (67-byte records: Curse's 63 + 4 zero), effects .SFX, characters
  saved alone .WHO (`variant.secret_files`). v0.93.1: its races are
  numbered otherwise (0 tribble, elf, half-elf, dwarf, gnome, halfling,
  human, 7 monster) - the record gets Curse's number
  (`party::race_from_secret` / `race_to_secret`), Secret's own kept in
  ext so a tribble comes back as one.
- **v0.94.0 - the party menu, Load, Begin.** Its 13-entry party menu and
  words (Initialize Mouse/Joystick left out - section 12), the party list,
  Load Saved Game, View's name tables (its races put in Curse's order),
  Begin: a save with no script running starts ECL1 block 16 - Priam's
  house at 3,3 facing south, as the original. Its script blocks are
  numbered across ECL1-3 (NEW ECL 33 after 0x7F12 = 2 is ECL1's 33;
  `OpSet::area_by_block` - v0.95.0 has the whole picture). The Play Test opens (Dev Tools); what isn't
  known yet says so ("Spells and rules aren't in the engine for this game
  yet", "Fights aren't ...").
- **v0.95.0 - the rule tables.** Secret's class / spell tables read from
  its program in their own shapes (`classes::Layout::secret`: 7 classes,
  levels to 18, 119 spells whose rows start with a byte of their own,
  7 spell levels, race tables in its race order); `classes::Tables` holds
  as many bytes as the game's tables are (on the heap). Checked: the GOG
  party's THAC0, saves, thief skills, spell slots and books come out of
  the tables as their files have them (but a dwarf's saves - his
  Constitution effect, still to work out). Spell slots of the 6th / 7th
  levels go to `Character::ext`; spells 101-117 to its book there.
  Its areas, corrected: 0x7F12 IS the area (1-6; its interpreter's
  set-area, as Curse's) and the areas share the numbered files two by two
  (file (area + 1) / 2); block numbers are unique across a family's files,
  so a block its area's file hasn't is found in the others
  (`play::open_area_dax`); monsters and icons are MON1* / CPIC1 for every
  area (`area_fmt`); LOAD PIECES 127 = no set.
- **v0.96.0 - the scripts' view of a character.** Secret's interpreter
  has its own field switch (`party::script_value_secret` /
  `script_set_secret`, `OpSet::secret_fields`): Curse's codes on Secret's
  fields, the race in its own numbers, new ones (Charisma, HP max and now,
  gems, each class's level), the status in its numbers (0x100), the 75
  memorized spells; no 0xE4.
- **v0.97.0 - View Character** in Secret's own layout (bars at rows 0,
  8, 16, 20, 23 and a divider at column 19; name / status, sex and age,
  Hit Points, alignment, race, class, Experience:, level, the stats,
  Armor Class / THAC0 / Damage, Encumbrance / Movement; its own words) -
  pixel for pixel the original's on the GOG party (DOSBox). Its menu: Items
  (with items) and Exit; the paladin's Heal / Cure and the right middle
  panel (empty for a party without items - what fills it to be seen) not
  yet.
- **v0.98.0 - item names.** Its 124 name words (21 bytes each) and ITEMS
  type file are read; its item types are its words' numbers (arrows 30,
  quarrels 12, darts 5; the elves' bows and swords 22-25, 18, 19). An
  item's name is the one its record keeps (`items::Names::stored_names`:
  "Two-Handed Sword of Giant Slaying" isn't made of its words). Not yet:
  how it renames an item it identifies or splits. (v0.115.1: wrong - its
  names are made from the words, as Curse's.)
- **v0.99.0 - fights.** Its battlefield tables are Curse's at its own
  places (DS 0x1A9E ground, 0x350-0x384 the placing tables; no terrain
  table); its monsters' 439-byte records are moved into Curse's layout
  as the party's (an NPC who joins keeps his extension); a monster's
  items have no name of their own in MON1ITM (left-over bytes), so the
  name is made from its words. The effects are its own numbers (GBC's
  lists and its own View names, its MON1SPC): what Curse's numbers mean
  there is often something else - those are Secret's number for the same
  thing (deadly poison 0x40 / -2 0x56, paralysis on a hit 0x58, immune to
  nonmagical weapons 0x67, to fire 0x5D, cold 0x62, electricity 0x06,
  the iron golem's spell immunity 0x4F as Curse's immunity to spells,
  the gazes 0x3A / 0x3B, breath 0x5A / 0x53 / 0x50, frightened 0x6F,
  berserk 0x6B, its fire shield 0x70) or nothing; 0x51 and 0x60 (unnamed
  in Secret, used by its monsters) taken as Curse's (half damage, the
  hug). The effects a fight's end takes away: its DS 0x0A9A list. Words:
  Curse's where Secret's GAME.OVR has the same text (most of the menu,
  hits, results, treasure); the spells' results are worded otherwise
  there ("unaffected", "flees in terror") and say nothing yet. Turn
  Undead: its table isn't found yet - no Turn on the fight menu
  (`Tables::turn_known`; section 12). Its new monster abilities
  (petrifying touch 0x41, the pyrohydra's breath 0x43, the confusion
  gaze 0x46, +4d6 fire 0x68, +3 weapons to hit 0x3C, half damage from
  piercing / cutting 0x42, immune to fear 0x5C) not yet. CALL 2DCB, after
  the scripts move the party (0xC04B-D), is its 3D view redrawn (Curse's
  CALL 2E10).
- **v0.100.0 - the start, the walls, treasure and camp.** A new game
  begins with ECL2's block 17 (the shared one), as the original: the four
  opening texts over BIGPIC 119, then 0x7F12 = 1 and NEW ECL 16 (Priam's
  house). Secret's big pictures are PICTURE 112 and up
  (`variant.bigpic_from`; Curse 120). LOAD PIECES 127 loads nothing and
  leaves the set as it is (v0.95.0 cleared it: block 21's three parts in
  sets 1-3 lost set 3, and Priam's house had no walls). The treasure
  screen shows its picture (PIC 60, `variant.treasure_pic`) and "Each
  character receives ... / The party has found treasure!" in its order
  (the program's two words). The camp shows its picture (PIC 59,
  `variant.camp_pic`), "The party makes camp..." and " CAMPING" after the
  time on the position line (both games' programs print it: Curse's camp
  says it now too). All checked against the original in DOSBox.
- **v0.101.0 - camp magic.** Its menu, the level names ("1st".."7th" +
  " Level", `magic.level_word`), 119 spell names (35 bytes each), its
  words; Memorize / Cast / Scribe / Display / Rest as Curse's, with 7
  spell levels: slots 6 and 7 and spells 101-117 kept in the record's
  extension (`classes::slots`, `in_book`, `to_book`; magic.cpp works to
  the tables' `slot_levels()`); no "NAME can memorize:" line (the kinds'
  counts from row 18); the confirmation "Memorize" / "Scribe" + " these
  spells?" (`magic.these_spells`); Display names an effect by Secret's
  own table, an entry an effect number (`magic.effect_names`; no name:
  left out, as the original). Checked against DOSBox (Dominic's
  grimoire, the counts, Display). The items' list words (Next / Prev /
  Exit, "'s" ...) mapped too. Not yet: the spells' result words in camp
  ("is Blessed" - Secret's are worded otherwise), Heal (6th level) and
  the new spells' camp effects.
- **v0.105.0 - the spells' words.** Secret's spell routines (its
  handler table: DS 0x8A8E + 4 x spell, set at run time;
  secret_facts_2.md 14) say "is " + a word: mostly the effect's own name from its effects
  list ("is Blessed", "is Held", "is Hastened", "falls Asleep"), else
  "Healed" / "Affected" / "Unaffected" / "Protected" or a word in the
  routine's code ("is invisible", "is un-cursed"). The profile's joined
  words (`Profile::joined`, `text::kJoined`) put the two together; both
  its fight and camp tables say them. Its renumbered spells: 36 is Heal
  (Curse: Animate Dead) - disease, blindness, feeblemind cured, then the
  most hit points less 1d4 (`FullHeal`); 56 Harm (Curse: Restoration) -
  diseased, hit points down to 1d4 (`HarmTouch`); 63 the Elixir of Youth
  (Curse: the party invisible) - 1d4 + 1 years younger, at least 18
  (`Youth`); 96 the druids' Charm Person or Mammal (as Charm Person: the
  engine doesn't tell mammals apart); 98 the druids' Cure Light Wounds;
  90 Barkskin runs Heal's routine in the original (and gives its effect)
  - kept so (section 12). Its item words (Use ... Id, Select) mapped by
  their text; its cures' effect numbers (blindness 0x21, diseases, 0x44
  feeblemind ...). Cure Blindness says "can see" alone, as its routine.
- **v0.106.0 - its monsters.** Its effect handlers (DS 0x87EA + 4 x
  (effect - 1); secret_facts_2.md 15) give its new abilities
  (`MonFx` Secret fields): the iron golem only hurt by +3 weapons, the
  cockatrice's stoning touch, the remorhaz swallowing on a natural 20, the
  pyrohydra's fire (8, every turn), the umber hulk's confusing gaze, the
  white dragons' cold breath (3 lines of 7) - its red dragons' fire is
  0x68, not 0x53 -, the giant slug's spit by its rule (0x51 - not Curse's
  half damage), the dreadlord's immunities (fear; charm and sleep; poison
  and paralysis). New words "breathes cold" / "breathes Fire" (joined),
  "is swallowed whole", "stares blankly". The original has no Turn Undead
  (its fight menu: Move View Aim Use Cast Quick Done) and no "The party
  has lost" - the engine already matched.
- **v0.115.1 - Secret's item names are made from their words** when
  shown, as every game's (DOSBox, secret_facts_2.md 25): its records' own
  names are left-overs of a list line (the original writes " No   Mace +1 "
  there), so "Two-Handed Sword of Giant Slaying" shows as "Two-Handed
  Sword of vs Giants" and "6 Arrow +1" as "6 Arrows +1"; identified,
  halved and joined items are named by Curse's rules (the hidden words,
  0x35, as Curse's).
- **v0.115.0 - Secret's vault** (secret_facts_2.md 24): the bank's
  Deposit / Withdraw (its script sets the area word 0x4CFC, then COMBAT)
  open the vault - the treasure's screen under the script's picture,
  "View Take Pool Exit" (Take only with something there): Take its coins
  and items, Pool the party's coins into it, View with **Deposit** in
  Drop's place (coins: "How much X will you deposit? "; items: at once,
  put away first; the items menu "Ready Trade Deposit Halve Join Exit"),
  leaving writes `VAULT.DAT` in the save folder as the original's (7 coin
  counts of 4 bytes, then 67-byte items in the list's order) - one vault
  for all saved games, as the original's. Checked in DOSBox with a save
  made by the engine at the vault's door.
- **v0.114.0 - Secret's Alter** (secret_facts_2.md 23): "Order Drop Speed
  Icon Level Exit" with no prompt, its own words (and the icon editor's
  menus - they were empty for Secret); **Level** - "Novice Squire Veteran
  Adept Champion" (area word 0x4CF4, Veteran at the start): the other
  side's monsters come into a fight with hit points x (level + 2) / 4.
- **v0.113.0 - Secret's exploring menu**: "Move Area Cast View Encamp
  Search Look" (GAME.OVR 0x198E3); Move switches the menu line to "Exit"
  as the original's move mode, a script or Exit ends it (SPEC 12: the
  movement keys move either way).
- **v0.112.0 - Secret's sound** (secret_facts_2.md 22): its START.EXE
  keeps Curse's sound driver (segment 0x7E4) with 13 sounds - Curse's
  byte code but the magic missile's - so Tandy and the PC speaker play as
  in Curse; its Tandy byte code lies past the driver's own code, so the
  engine reads two parts of the segment (`sound::Layout` lo2 / hi2; ~4 KB
  in all). SOUND.DAX (a second driver - AdLib, Innovation, Covox, Tandy,
  speaker - and a "GLBR" parts file) isn't used.
- **v0.111.0 - Secret's Create New Character and Modify** (secret_facts_2.md
  21; checked against a magic-user made in DOSBox - the same record but
  the rolls): its words ("Pick Race" ...), its race order (Elf, Half-Elf,
  Dwarf, Gnome, Halfling, Human), each roll with its races' adjustments
  (dwarves Con +1 Cha -1, elves Dex +1 Con -1, halflings Str -1 Dex +1),
  200,000 experience (two classes 100,000; three, and magic-user /
  thief, 66,667) trained silently to its level (a magic-user to 9th), no
  money, its races' effects (gnomes without the giants' one), a new
  magic-user's Detect Magic / Read Magic / Shield / Sleep and the lists
  its silent training teaches at magic-user levels 6-9, the default icon
  colours. Modify: its "Add Subtract Keep Exit" (Add / Subtract = the
  arrows), the stats at rows 9-14 and the hit points at row 3 as its View,
  a character with its own starting experience. Human Change's new
  magic-user spells as Curse's.
- **v0.110.0 - Secret's Add Character: "Add from where? Secret Curse
  Exit"** (secret_facts_2.md 20): Secret its own characters (.WHO and the
  saved games' members); Curse - the .GUY files in Secret's save folder (as
  the original) and, with Add Characters (more characters), Curse's own
  save folder on the card (its .GUY files and saved games' members). A
  Curse character comes as Secret takes one in: its record, but no items,
  money or effects, no spells in memory, the spell book without the spells
  Secret numbers otherwise (36 56 63 90 96 98 100); a paladin gets one
  cure of disease and Protection from Evil, a ranger the giant-class bonus
  (also made with Secret's new paladins / rangers now). The earlier
  game's folder is the selected game's: Curse - Pool, Secret - Curse.
- **v0.109.0 - Secret's spells past 100** (secret_facts_2.md 19): Death
  Spell (2 x 4d20 points spent by Hit Dice - 0 1, 1 2, 2-3 8, 4-5 16, 6 16
  or 20 over 51 HP, 7-8 20, more never; no save) "drops dead";
  Disintegrate (magic resistance, a save or dead) "disintegrates"; Globe
  of Invulnerability "is Protected"; Stone to Flesh (fights and camp: the
  stoned back up with 1 HP, "is no longer a statue" / "is not stoned.");
  Flesh to Stone (a save or stoned) "is turned to stone"; Delayed Blast
  Fireball (level d6 + level, fire and magic, the fireball's area);
  Mass Invisibility (the caster's side, "is Invisible (spell)"; camp too);
  Power Word Stun (no save: 1-30 HP 4d4 rounds, 31-60 2d4, 61-90 1d4 of
  effect 0x6A - its turns lost) "is STUNNED". 101-109 have no workings
  (the original's empty routine). Its monsters keep their record's magic
  resistance (0x1A5: the drider 15, the lizard king 20 - by the caster's
  level as Curse's) and flags 2 (0x15E: 8 = the iron golem, dreadlord,
  otyughs - Slay Living and the Death Spell leave them be).
- **v0.108.1 - Secret's training hall** (secret_facts_2.md 18): free (its
  program has no price and no "Training costs" word); the hit points a
  level from its own tables (DS 0x9B0 the count at level 1, 0x9B7 the
  sides - 7 classes; the better of two rolls, as Curse); its demihuman
  limits also stop clerics (dwarves at 8th level, elves and gnomes at
  7th). The temple's list keeps the shop's bar at row 16, as the original.
- **v0.108.0 - Secret's temple, shops' words, Appraise** (secret_facts_2.md
  17): the temple's menus ("Heal View Pool Appraise Exit", "Heal Exit",
  "NAME, how can we help you?") and the cures' names (START.EXE's data);
  **no prices** - a cure is done at once and says what it did (its own
  overlay, GAME.OVR 0x5AFE): Cure Blindness "can see!" / "is not blind";
  Cure Disease (0x1F 0x22 0x2B 0x2C) "is cured" / "is not Diseased"; the
  wound cures 1d8, 2d8+1, 3d8+3 and Heal (to all but 1d4 - a byte, so near
  full it heals all; and blindness, feeblemind, the diseases: "is cured",
  then "is fully healed") - "is healed", "has no need of healing", the
  dead "must be raised from dead", the stoned "can only be healed by a
  stonemason", the gone "is not here"; Neutralize Poison (0 HP -> 1, okay
  again) "is no longer poisoned" / "is not poisoned"; Raise Dead (not
  elves: "cannot be raised"; a point of Constitution, 1 HP) "is
  ressurected" / "is not dead yet"; Remove Curse "rejoices.  The curse
  has been lifted!" / "is not cursed"; Stone to Flesh "is turned to
  flesh" / "is not petrified". The priest's / shopkeeper's coins words,
  Take's words (its " will you take? " has its own space), Appraise's
  words and values - Curse's halved (gems 5 / 25 / 50 / 250 / 500 /
  2500; jewels' ranges and bases halved: `variant.appraise_div`), a kept
  gem / jewel item type 47, words 122 / 123; the training hall's words
  (its training itself: still to check).
- **v0.107.0 - View Character's menu** (all three games; coab's ovr020
  rules, each game's own words - `profile::view_facts`): Spells (the
  memorized ones, only looked at), Trade (coins to another member: "Trade
  to?", "Select type of coin ", "How much X will you trade? " - too heavy:
  "Overloaded"), Drop (coins; at a treasure or in a shop onto the
  counter), a paladin's Heal (twice the paladin level, once a day: its
  effect - "NAME feels better" / "is unaffected") and Cure (the diseases,
  "is not diseased" + "cure anyway: Yes No"; the week's cures in the
  record's 0x191 - Secret's 0x6D, mapped there -, given back when its
  effect ends: `magic::tick_affects`); Pool has no paladin's powers. In a
  fight View keeps Items and Exit. Secret's View shows its money panel
  (copper first, rows 9-15) and the readied weapon / armour at rows 21-22,
  as the original (secret_facts_2.md 16). Not yet: Secret's Deposit (its
  vault).
- Not yet: the items and
  their words (Secret's item word table differs), Modify ("Add Subtract
  Keep Exit"), Add Character's "Secret Curse" (importing Curse's
  characters), the camp (spell names, 6th / 7th level spells), the
  scripts' view of a
  character (Secret's own offsets and race numbers), the spells' words
  and its new monster abilities in fights, sound (SOUND.DAX), the sky and
  approaching monsters (SPRIT1).

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
  Only the install-folder copy is supported (Tom, 2026-10-07: stick to the
  one in the Project files); another edition needs its own table.
- All nine journals in Tom's Project (four PRS games + Krynn, Savage
  Frontier) are 150 dpi scans except Treasures (lower).
- The text layers are OCR and unusable for the calligraphic entry font
  ("6razecl {i!(g, fire"); the Curse install copy has none at all.
- Several entries are **pictures and maps** (Entry 4 is a sewer map, 8 a
  drawing, 9 a symbol), which text would lose anyway.
- Entries run in columns and continue into the next column or page.

So the journal is shown as **pictures of the entries**, not text, cut out
of the player's own PDF with a table of rectangles for that exact PDF
(coordinates only). The repo, the firmware and the flasher site never
contain journal text or images; tests use made-up pictures. Each game's
journal gets its own table once its GOG PDF has been looked at.
v0.85.0: **Pool of Radiance's** GOG journal (the copy in the game folder,
6,325,424 bytes, 21 spreads 1328 x 1003 at 150 dpi): its headings are
italic text, not Curse's blue boxes, so `make_table.py poolrad` finds
"Journal Entry N:" and the tavern page's "Tale N:" with tesseract (on the
player's PDF, in scratch space); 58 entries and 23 tavern tales, each
checked by eye (entry 37 is the atlas across a whole spread; the "legend
for all maps" box belongs to none). Pool's scripts also say "TALK ENTRY 55
IN YOUR JOURNAL" (the number first): the mention finder knows that too.

v0.10.0 did this in a browser page (pdf.js) on the player's PC; Tom
(2026-10-09) wants no PC step: since v0.12.0 the board does it.

How it works (v0.12.0, Curse):
- The card scan finds a .pdf with "journal" in its name in the game's
  folder, reads its cross-reference table and page tree (`engine/pdf.*`,
  classic xref only) and recognises the edition by file size + the
  trailer's /ID (`journal::find_table`). Curse install-folder PDF: 6779800
  bytes, /ID 36ce730a4edce39469cb5215e05df63d.
- `engine/journal_tables.cpp` (made by `tools/journal/make_table.py`, which
  also keeps `tools/journal/tables.json`) lists each entry's pieces: PDF
  page, x, y, w, h in pixels of the page's picture (150 dpi). Curse:
  entries 1-59 (1 on PDF page 2; 2-59 on pages 6-13; 59 comes before 58),
  Tavern Tales 1-62; 138 pieces. Checked by eye against the rendered
  entries (renders stay in scratch).
- Each page is one baseline JPEG (4:2:0, 16 x 16 MCUs); `engine/jpeg.*`
  wraps ChaN's TJpgDec (`third_party/`), decoding only the MCUs the pieces
  need and stopping below the last one. Pixels are stored as one of 256
  colours (a 6 x 6 x 6 cube + 40 greys); paper and show-through (lum > 185,
  not blue; blue: lum > 225) become white.
- `_CYD/<folder>/JOURNAL.DAT` (format in `engine/journal.h`, "GBJ2" written
  last; Curse ~15 MB) keeps every piece at the scan's resolution; a later
  scan keeps a file made from the same PDF. The scan log lists a PDF it
  doesn't know with its size and /ID, so a table can be added.
- The engine watches the printed text for "JOURNAL ENTRY n" / "JOURNAL AS
  ENTRY n" / "TAVERN TALE n" (numbers may come in the next PRINT) and, when
  the game next waits for a key, the screen stays to be read (Tom,
  2026-10-10, v0.50.0: it used to jump straight to the entry); the player's
  next tap (or key) shows the entry instead of going on, and after Back to
  Game the game still waits for its tap. The entry: scaled (bilinear) to the
  screen's width, on white, header "Journal Entry 31  1/2", Prev Page /
  Back to Game / Next Page. Without JOURNAL.DAT: "Read Journal Entry 31 in
  your Adventurer's Journal." (as the original).
- v0.13.0: **Zoom** on the entry screen (Prev Page | Zoom | Back to Game |
  Next Page) shows the scan's own size (the maps); wider than the screen,
  a tap on the left / right third moves the view (no dragging). **The book
  view** (`ui/pdfview.*`), the fallback for an unknown edition or an
  unprepared journal: "Open Journal PDF" on the entry screen pages through
  the PDF - whole pages (decoded at 1/2-1/8 with TJpgDec's scaling, fitted),
  Zoom or a tap on a spot = the scan's size there, taps on the edges move
  it; only the MCUs in view are decoded, stopping below the view.
- v0.16.0: the Menu key's Journal tab lists the entries received so far
  (section 4); its Journal PDF tab is the book view.
- v0.23.0 (Tom): both screens' keys are Back | Zoom | Prev Page | Next
  Page. Zoom cycles three levels, the key naming the one shown: entries
  Width (the start) -> Full Size (the scan's pixels) -> Whole (the entry
  fits the screen); the book Whole Page (the start) -> Page Width -> Full
  Size; the middle of the view stays in view. Dragging the picture moves
  it (applied when the stylus lifts - redrawing a scan as it moves is too
  slow); taps on the edges still move it. The book draws each decoded
  block straight to the panel (a few panel rows gathered when scaled
  down) - the old 23 KB decode band didn't fit beside the Play Test, which
  made Zoom say "This page has no scanned picture." Errors now say what
  went wrong (no picture / not readable / not enough memory, with the
  numbers). v0.52.0 (Tom: "no scanned picture" again, opened from the
  Game menu): the Play Test had grown, the two ~8 KB blocks reading a
  page's objects weren't there, and that failure was still reported as
  "no picture" (now it says not enough memory). While the book is open
  the game screen's canvas (64,000 bytes) waits on the card
  (`/GOLDBOX/_CYD/CANVAS.TMP`, `frame::park` / `unpark`) and comes back
  when the book closes; the journal PDFs of all four games read on the
  PC page by page (17 / 21 / 33 / 33 pages, all with a picture).
- v0.64.0 (Tom): no title bar; one slim key row, Back | Zoom | Prev |
  page numbers | Next. v0.66.1 (Tom): the page numbers are just "2 of 3"
  (the entry's number is in its picture; no "Page").
- v0.70.0 (Tom, 2026-10-10: the Steam / SNEG releases): their journal PDFs
  are typeset (InDesign, cross-reference and object streams, thousands of
  objects) - far too heavy to take apart on the board, and nothing the
  page-picture tables can use. But the SNEG folders carry the **Gold Box
  Companion** (`<folder>/GBC/Games/<nn. Game>/`), whose `Game.dat` holds
  every journal entry as plain text (`<journal N>` ... `</journal>`), and
  `JE_NNN.jpg` the entries that are maps or pictures (their text is just
  "(map)" or similar). So when the scan finds a GBC folder with Game.dat,
  JOURNAL.DAT is made from those instead of the PDF (id `gbc:<Game.dat
  size>`): a **text piece** per entry (format version 2: width 0, height =
  the text's length; lines joined into paragraphs, blank lines between
  them), or the entry's JPEG decoded whole when it's a picture entry.
  Text is laid out by the viewer at the screen's width, black on white;
  Zoom cycles Small / Medium / Large text. Curse's GBC: 59 entries, 9 of
  them pictures. The SNEG game files themselves are the GOG ones byte for
  byte (Curse checked), so they share the GOG profile. The book view (the
  Journal PDF tab) can't show a typeset PDF; on a GBC game it says so.
  Only Curse's Game.dat has been looked at; the other games' are assumed
  to be the same format (the scan says when one isn't).
  v0.71.2 (SNEG Pool of Radiance looked at: same format, 58 entries, 12
  pictures): some entries are text, the picture's note ("(picture)"),
  then more text - those become up to 3 pieces (text, the JPEG, text) and
  the viewer shows text and pictures in one scroll (text at the font Zoom
  picks: Whole small, Width medium, Full Size large). The journal's id
  became `gbc2:<size>`, so a Rescan Card rebuilds it.
  Art (Tom asked, 2026-10-11): neither release has Pool of Radiance's own
  1988 cover - the SNEG `game.ico` is the same picture as GOG's
  `goggame-*.ico` (the later Ruins of Myth Drannor art), `gog.ico` /
  `Support.ico` are GOG's generic ones, GBC.exe has only a 32 px app icon,
  and the Companion folder holds only parts, fonts, the map and the
  journal pictures. The game's own title screen (TITLE.DAX: the SSI / AD&D
  page and the "Pool of Radiance" title page) is the only 1988 art there.

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
  on release, at the point where the stylus came down. No drags or swipes,
  except scrolling the logs and the brightness slider (Tom, 2026-10-09) -
  and those work by taps too.
- Errors stay on screen until tapped (Tom, 2026-10-09). The games' own
  timed messages keep the games' timing.
- Tap highlight (Tom, 2026-10-09; v0.24.0): resistive screens are finicky
  and the engine can be slow to answer, so what a tap acts on lights up
  before anything else happens: an engine key gets a bright ring (cream
  edge, gold inside); on the game screen a menu line word, a list line or
  a party menu line has its letters drawn in the highlight colour (15). A
  key or word already lit blinks off (70 ms) and on. Afterwards it is put
  back as it was unless the screen was redrawn there. Since v0.104.0 (Tom,
  2026-10-17) it lights up as the stylus TOUCHES (ui::poll_press), not
  as it lifts; the tap still acts on the lift, at the press point; a press
  that ends without a tap (a drag) puts it back.
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
- **The platform layer** (Tom, 2026-10-12, v0.91.1: "you have to be coding
  with a hardware abstraction layer"): `src/platform/*.h` is all the game
  code may ask of the machine - `platform.h` (milliseconds, the log, the
  heap figures), `file.h` (the card's files: `plat::File`, `plat::open /
  exists / remove / mkdir / card_ready`, `plat::FileSource`), `audio.h`
  (sound out: a filler of 8-bit samples) and `memlog.h` (the memory log).
  Implementations: `src/hal/platform_esp32.cpp`, `hal/audio.cpp`,
  `hal/memlog.cpp` (the boards: Arduino, the SD library, heap_caps, the
  DAC / I2S) and `tools/host/` (a PC: stdio files under a folder that
  stands for /GOLDBOX, the test driver's clock, stdout - the playsim and
  the sweeps run on it).
- **The game code** `src/game/` (play: the party menu, exploring, camp,
  shops, fights - `play.cpp`, `play_fight.inc`; the Screen / Walk Tests:
  `look.cpp`, `walk.cpp`) uses only the engine and the platform layer: no
  Arduino, board, display or touch headers. It draws into the canvas and
  takes keys / taps through its own API (`game/play.h`). CI compiles it
  with nothing but `-Isrc` and runs `tools/host_tests/test_platform.cpp`.
- **The CYD front end** - `src/ui/` (screens, keys, touch, the canvas on
  the panel: LovyanGFX), `src/app/` (the library scan, settings),
  `src/boards/`, `src/hal/` - is what a port replaces.
- Rule from here on: game logic takes **key events** like the original
  (letters, arrows / keypad, Enter, Esc); taps on menu words, list lines and
  combat squares are turned into those keys by the CYD front end. A gamepad
  (PortMaster) or keyboard maps onto the same keys.
- A port = the platform layer (`tools/host` is a working example for a
  PC) + a front end: show the canvas (e.g. an SDL2 texture, scaled), turn
  input into the game's keys / taps (`game/play.h`), the engine's own
  screens (settings, journal, library). Still CYD-only (TODO): those
  engine screens are drawn with LovyanGFX in `ui/viewer.cpp`.
  Tom (2026-10-07): PortMaster versions are a **stretch goal** - keep the
  engine easy to port; an SDL2 front end is held in reserve (a PC test build
  isn't wanted for its own sake).
- **Engine comforts** (things the originals didn't do, added for the small
  CYD screens) each have a switch in `src/app/features.h`, so a fork for
  bigger screens can drop them with a build flag (Tom, 2026-10-10):
  - `CYD_BIG_ICON_PREVIEW` (v0.59.0): the icon editor's big preview of the
    new combat icon - 480x320 always, in the Companion strip; 320x240 with
    Settings -> Large Icon, in the editor's empty right side;
    a tap flips ready / action.
  - `CYD_DEMO_TAP_STOPS` (v0.64.0, Tom): a tap or key stops the title's
    Demo (the title comes back); the original's demo can't be stopped.
  - `CYD_CLEAR_FONT` (v0.73.0, Tom: the Gold Box Companion offers other
    fonts; easier reading matters on these screens): the engine's own
    **Clear** font - upright, bold (2-pixel stems) capitals, digits and
    punctuation, plus real lower case - in place of the game's narrow
    slanted capitals (glyphs 1-26 and 32-63; lower case gets glyphs of its
    own, so the program's mixed-case words - menus, character sheets -
    print as written; the scripts' text is stored in capitals and stays
    so). The game's symbols and frame pieces (glyphs 0, 27-31, 64-176) are
    kept. Drawn for the engine, glyph by glyph, in
    `tools/fonts/clear8.txt` (`tools/fonts/make_font.py` makes
    `engine/font_clear.cpp`); the Companion's Font_01-03.bmp were only
    looked at (they are the game's own font, lightly retouched, capitals
    only). v0.73.1 (Tom): OFF unless the player picks it (settings.h
    `clear_font`; the other comforts are on by default), and switching it
    mid-game (Game menu -> Options) changes the text already on the screen
    too: every 8 x 8 cell holding exactly an old-font text glyph (one
    colour on black, or black on one colour for the menu's choice) is
    redrawn in the new font, then the 3D view, the party list and the menu
    line are drawn again (`font::set_clear` with a canvas).
    v0.74.0 (Tom): a third choice, the player's own **Pools of Darkness**
    font (block 201 of 8X8D0.DAX in their Pools of Darkness folder on the
    card; its glyphs 1-26 and 32-63, capitals only) - the key is **Game
    Font**, cycling Original / Clear / Darkness (Darkness only offered
    when that folder was found; settings.h `game_font` 0 / 1 / 2, older
    files' Clear = 1). `font::set_letters` swaps any of the three, mid-game
    too (the same redraw; checked pixel for pixel against a fresh start).
    v0.84.0 (Tom): a fourth, **Clear Caps** - the Clear font's capitals,
    digits and punctuation without its lower case, all capitals as the
    games print (`font::kClearCapsLetters`, settings.h `game_font` 3);
    the key cycles Original / Clear / Clear Caps / Darkness.
  - `CYD_ICON_PORTRAIT` (v0.84.0, Tom: the games have no character
    portraits, but View Character leaves room for one): the character's
    combat icon at three times its size (72 x 72) in an 88 x 88 box where
    Pool draws its portraits - Curse: cols 27-37, rows 2-12, framed with
    the 3D view's own frame tiles (`layout::picture_frame`); Pool: its own
    box, top right. Where the game has a portrait (Pool: every character,
    NPCs too) the portrait comes first and a tap on it swaps to the icon;
    a tap on the icon flips ready / action (with a portrait: action ->
    the portrait again). An NPC from the monsters' files shows its CPIC
    picture, as in fights. Off: as the originals (Pool's portrait from the
    party menu only). Settings key **Icon Portrait**.
  - `CYD_ITEM_FIXES` (v0.90.0, Tom: "an optional fix, making the Flame
    Tongue match the AD&D description"): items the originals got wrong,
    made as AD&D describes them. Pool's Flame Tongue (type 36, name word
    "Flame Tongue", +2, 0x3D 0x16, no code) becomes a Long Sword +1 Flame
    Tongue - Curse's record: +1 (its "+1" name word), effect 0x06 with
    code 0x80 - so +2 against regenerating creatures (type 10, trolls), +3
    against cold-using / avian ones (types 9 / 12), +4 against undead
    (type 4), its blows fire (the AD&D 1e sword's figures). Made as
    characters are read (`play::character_read`), items are taken
    (`rules::add_item`) and the switch turns mid-game; the files keep the
    original's bytes (`write_character_files` writes a copy put back, its
    effect gone). Settings key **Item Fixes** (on by default; As Original
    turns it off).
  - `CYD_ICON_GALLERY` (v0.71.0, Tom: making icons a value at a time is
    "one of the worst parts of the game"): the icon editor (Alter -> Icon,
    Create New Character) opens on a gallery - pages Icons (32 ready-made
    icons), Heads (all 14), Weapons (all 32 bodies) and Colors (24
    schemes), each cell the character's icon with that choice in place.
    v0.71.1 (Tom, on a 2.8": too small to pick from): the icons are shown
    at twice their size, 4 x 3 to a page, with big Prev / Next boxes and
    "1 of 3" to their right (Up / Down step on past a page's end too). A tap (or Up / Down) puts it on the character at once (the
    480x320 big preview follows; on 320x240 the gallery fills the screen,
    so its big preview waits for the editor). Edit opens the original
    editor for fine work, whose Exit comes back to the gallery; Done (or
    Esc) asks the original's "Is this icon ok?" over the old / new pairs,
    No back to the gallery. The pictures are the player's CHEAD / CBODY
    parts at the character's size; the schemes and combinations are the
    engine's own numbers (`engine/icon_looks.*`, host-tested). The SNEG
    Gold Box Companion has no ready-made character icons (its Head.bmp /
    Body.bmp are the same parts; its other pictures are the NPCs), so
    there was nothing of the player's to take them from.
    v0.74.0 (Tom): the GOG release of the Companion does have combat icons
    (GBC/DAX-Icons-EGA: 24 x 24 BMPs in pairs, odd = ready, even =
    action, dark grey behind). With the player's copy of the Companion's
    GBC folder on the card as **/GOLDBOX/GBC**, its pairs follow the
    ready-made icons as more gallery pages ("Gold Box Companion Icons").
    The games' character records hold only head / body / colours, so a
    pick is the engine's own note, by name, in `_CYD/<folder>/ICONS.TXT`
    ("NAME=n"); fights draw the picked pair instead of the head / body
    icon (dark grey see-through, black kept black). Tapping another gallery
    icon or Edit drops the pick. Never shipped: the Companion is its
    author's work, read only from the card (Tom, `engine/gbc_icons.*`,
    host-tested with synthetic BMPs).

## 12. Where the engine differs from the original games (Tom, 2026-10-10: keep this list up to date)

Everything here is on purpose; anything else that plays differently from
the originals is a bug. Engine comforts with a `features.h` switch are
marked (switch); since v0.72.0 the player can also turn each one off in
the game's Settings (As Original: all of them).

- **Secret of the Silver Blades** (v0.94.0): its party menu's Initialize
  Mouse/Joystick isn't shown (nothing to set up: taps are the mouse);
  when BLADES.CFG's save folder (C:\SAVE\) isn't on the card, the saves
  are the game folder's own (where GOG ships its sample party).
  v0.108.0: its temple's Raise Dead takes a point of the character's
  own Constitution and the hit points follow it as the girdle's change
  does (the original lowers the Constitution in use and takes its own
  smaller sum off the maximum - which the next recalculation may undo:
  not checked in DOSBox); Heal's two lines show together (rows 21-22),
  the original one after the other. v0.109.0: its Delayed Blast
  Fireball goes off at once (the original asks "Blast Delay in segments
  (1-50)?" and can leave the blast on its square for a few rounds).
- **Controls and screens**: taps on menu words, list lines and combat
  squares; on-screen keys (movement pad, cursor keys - Up / Down / Left /
  Right / Select - Game, Look, and on 320x240 Map: the area map over
  the game screen until the next tap; Tom confirmed Map 2026-10-10); a
  fight's own keys while a fighter moves or aims by hand (the 8 directions
  and Select); the 480x320 Companion strip (map - black, walls 1 px, doors
  a 3 px band a pixel short of each end so the wall line shows there,
  secret doors an S on the wall line, the party a bright green arrow -
  Tom, v0.84.0); while the 320x240 Map is up the movement keys move the
  party and the map follows (Tom, v0.84.0; a step that starts a message,
  menu or fight brings the game screen back); the Game menu (Journal, Journal PDF, Sounds, Options, Logs, Exit Game - no Esc key in the game, Tom 2026-10-12);
  a tap on the 3D view toggles 3D / Area; in Pool's wilderness the
  movement pad's Forward / Side-step Left / Side-step Right / Turn Around
  are its arrow keys (north / west / east / south; the turn keys do
  nothing, as there's no turning there) and a tap on the square north,
  east, south or west of the rider steps there.
- **Secret doors on the map** (v0.84.0): the GEO data has no secret-door
  mark (nor the Gold Box Companion's docs), so the engine takes a
  passable side whose wall type is drawn as the area's ordinary wall - a
  type at least four times as often solid as open on that map - to be a
  secret door (the 3D view shows a plain wall there). Most maps have a
  handful; it's a guess from the data, not the games' own notion.
- **Flasks of oil burn** (Tom, 2026-10-12: "the flasks of oil should do
  fire damage"): the originals make a thrown flask a plain 2d6 blow
  (ITEMS row 86); here its blow counts as fire, so a troll it brings down
  stays down (as one killed by a fire spell) - v0.87.2. Two throws a
  round and no splash, as the originals (Tom: no splash in either game).
- **Magic Missile** (Tom, 2026-10-11): (level + 1) / 2 missiles of 1d4 + 1,
  always hitting - the AD&D rule, Curse's code too; Pool's code was read as
  level / 2 (nothing at first level). All on one target, as both games.
- **View Character's icon portrait** (switch `CYD_ICON_PORTRAIT`).
- **Item Fixes** (switch `CYD_ITEM_FIXES`, Tom, v0.90.0): Pool's Flame
  Tongue is the AD&D sword (Long Sword +1 Flame Tongue, as Curse's); the
  original's is a plain +2 sword whose Use casts Find Traps.
- **Pool's race checks** (Tom, v0.85.1): Pool's program never gives its
  scripts the character's race (they read a memory word nothing sets: 0),
  so two checks never matched. The engine gives the race, so they work as
  written: in Podol Plaza (ECL1 block 18) a passing patrol stops the party
  with a chance of 5% + 5% for each human in it (the original: always
  5%); in the Temple of Bane (ECL1 block 24) each member gets a chance to
  find its trap doors - 1 in 5 while searching, +1 in 5 for an elf or a
  half-elf (the original: elves and half-elves no better, and nobody finds
  them without searching).
- **Saving a game** (Tom, v0.85.5): "Saving to slot A..." on the menu
  line while the card is written (the originals: "Saving...Please Wait").
- **Pool's training, a magic-user with no spell left to learn**: the
  original writes the "learnt" mark to spell 0's place, which is the
  record's maximum hit points (they become 1 before the level's gain); the
  engine just learns nothing.
- **Pool's healed land** (after the pyramid): the wilderness icons show
  their healed colours at once; the original recolours them a few pixels
  at a time while panning the map from the city to the pyramid.
- **The journal**: the entries are pictures the board makes from the
  player's own journal PDF (Steam / SNEG: text and pictures from its Gold
  Box Companion files) and shows when the game mentions one (the next tap
  opens it; switch `CYD_JOURNAL_POPUP`); the whole PDF as a book. The originals only print "read entry
  N".
- **Copy protection**: never asked.
- **The demo**: a tap or key stops it (switch `CYD_DEMO_TAP_STOPS`).
- **Add Character**: Curse also lists the members of saved games ("NAME
  from saved game A"); Pool lists the Pool of Radiance characters straight
  from the player's Pool of Radiance folder on the card too (the original
  needs them copied into Curse's save folder) - **the standard for every
  game's import from the game before it** (Tom, 2026-10-10); Hillsfar
  characters can't be added (not one of the engine's games) (switch
  `CYD_MORE_CHARACTERS`).
- **The icon editor's big preview** (switch `CYD_BIG_ICON_PREVIEW`).
- **The game font** (switch `CYD_CLEAR_FONT`): the engine's own Clear font
  (upright, bold letters with lower case), Clear Caps (its capitals only)
  or the player's Pools of Darkness font instead of the game's own;
  changeable mid-game.
- **The icon gallery** (switch `CYD_ICON_GALLERY`): the icon editor opens
  on ready-made icons, heads, weapons and colour schemes to tap; the
  original's Parts / colours / Size menus are behind Edit, and their Exit
  goes back to the gallery instead of straight to "Is this icon ok?".
  With the player's Gold Box Companion on the card its combat icons can be
  picked too (kept by name in the engine's ICONS.TXT, not in the save).
- **Sound**: Tandy (default), PC speaker or off, with a volume slider in
  the engine's Settings.
- **Saving**: the GOG files' save folder on the card, as the game.
- **Monsters of one kind share one item list** (a group's): one archer's
  arrows, a wand's charges are the group's (the original gives each copy
  its own - RAM).
- **The Strength spell's 18/xx** is worked out from what the items left;
  the original adds to the 18/xx in use, so it grows a little with every
  recalculation (the engine recalculates far more often).
- **The giant strength potion on someone already stronger than 21**: the
  original's effect then holds an unset byte (anything); the engine gives
  their own Strength (no change).
- **Thief skills**: the original adds to every skill a value it never sets
  (whatever was on the stack - the GOG sample party's thief has +7 on all
  eight); the engine adds nothing (the table's skills).
- **Pool's Strength effects** (v0.89.0): the original keeps the Strength in
  the record and, in the effect in force, the Strength it replaced (the
  others wait with their own); the engine keeps the character's own
  Strength and what each effect gives, and the Strength in use is the
  most of them - so when the effect in force ends while a weaker one is
  still running, the original drops to the weaker one (even below their
  own) and the engine keeps their own. Its files are written the
  original's way (the same for Friends' Charisma). Putting Gauntlets of
  Ogre Power away takes off a strength effect giving 18/00 (the
  gauntlets' own first); the original takes the first strength effect
  when the Strength in use is 18/00 (it can be a spell's). Dispel Magic
  on these effects reads the engine's data (the original's holds the old
  Strength).
- **Secret's vault** (v0.115.0) keeps up to 64 items (the treasure's
  list); the original's holds as many as its memory takes. A vault item
  of type 0x49 (none in the game) is followed in the file by as many
  records as its count: the engine reads past them and doesn't keep them.
- **Secret's Move** (v0.113.0): its exploring menu starts with Move, and
  only in that mode (the menu line just "Exit") do the original's arrow
  keys move the party; the engine shows the same menu and mode, but its
  movement keys move the party in either (the cursor keys work the menu).
- Smaller things kept from the originals although they look like bugs are
  NOT differences: they're listed as quirks in the facts docs.

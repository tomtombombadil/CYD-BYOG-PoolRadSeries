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
  engine looks one folder down.
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
    keys), **Look**, **Esc**. The map is tighter: no "Map" heading, the
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
    Game, Map. Menu Keys / Move Keys swap the rows by hand; the swap lasts
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
| Title sequence (Curse): TITLE.DAX 1 at (0,0) 5 s; 2 at (0,0) + 3 at row 11 col 6, 10 s; 4 at row 11 col 0, 10 s; credits (outer frame + bars at rows 3 and 8), 10 s; a key skips each | `src/engine/profile.*` (steps), `src/ui/look.cpp`, `src/ui/play.cpp` | **v0.4.0**, in the Screen Test. **v0.53.0** (Tom: "like starting the game for real"): the Play Test opens with it (sound 0x0D with picture 4; a tap or key skips each wait), then a clear screen with the version line and its menu from the program ("Curse of the Azure Bonds v1.3 " Pascal string at image 0x06, "Play Demo" at 0x25); Play opens the party menu. The demo (area 1, ECL1 block 0x52 with no party, game speed 9; after 30 s the original starts it by itself) is still to come - Demo says so, and the version line waits. |
| Text window: printed a character at a time (game speed 4 = 12 ms) into a cell rectangle (text area rows 17-22 x cols 1-38; rows 21-22; combat panel rows 1-21 x cols 23-38); a word = punctuation + letters + punctuation + one space, wrapped whole if it (with its space) doesn't fit, longer-than-a-line words broken; leading spaces dropped after a wrap; area full = "Press any key to continue" (colour 13, row 24), clear, go on | `src/engine/text.*` | **v0.4.0** (behaviour from coab, own code). Touch: a tap = the key; a tap while printing finishes the page. |
| Combat screen: its frame ends at row 22; row 23 = a status line (range, "Spell: ...", "Item: ..."), row 24 = the menu as on every screen | `src/engine/layout.*` | Checked (coab). |
| Menu line (row 24): choices start at capitals / digits; prompt colour 13, key letters 15, the rest 10, the chosen word reversed (15 background) | `src/engine/text.*` | **v0.4.0**. Touch: tapping a word moves the highlight to it, shows it for 250 ms, then acts (its trailing space counts, so no gaps between targets; the tap area starts a row higher, at row 23 - Tom, v0.5.1). The chosen word stays chosen next time, as in the games. |
| Game icons: GOG's goggame-<id>.ico (ICONDIR) or goggame-<id>.dll (PE resources RT_GROUP_ICON 14 -> RT_ICON 3), copied by the player into the game's folder; images are DIBs (1/4/8/24/32 bit + mask) or PNG (GOG's 256 x 256, RGBA). The biggest is drawn, scaled down (alpha-weighted averaging) as big as the library page allows (~154 px on 320x240, ~194 px on 480x320; v0.7.1) | `src/engine/icon.*`, `png.*`, `inflate.*` (streaming, 32 KB window - based on CYD-Classic-Games' inflate) | **v0.6.0**: checked on the real goggame .ico / .dll of six games. START.EXE (DOS) has no icon; Support.ico is GOG's generic one, ignored. |
| GEO map block (1026 bytes): 2 bytes, then four 256-byte planes (a byte per square, x + y * 16): N / E wall types (nibbles), S / W wall types, a flags byte (>= 0x80 = under a roof: indoor sky colour), door states (2 bits per side: 0 solid, 1 open, 2 locked, 3 barred); wraps at the edges | `src/engine/geo.*` | **v0.7.0** (Curse). |
| WALLDEFn: n x 780 bytes = wall sets (5 pieces x 156 tile numbers in 10 groups: far front / sides, middle front / sides, near front / sides, far corner); a 2- or 3-part block fills the next sets too, its tiles are 8X8Dn blocks id*10+1.., and its later parts already number their tiles after the first's (every part moves by the first set's amount - v0.7.2 fix: shifting each part by its own set garbled Curse's area 5 / 6 walls); tile numbers 1-45 common (8X8D1 #203), 46-115 / 116-185 / 186-255 sets 1-3 (a set's own numbers stored as set 1's, moved up 70 / 140), 256+ frame tiles | `src/engine/view3d.*` | **v0.7.0** (Curse): drawn far to near like the games; sky (area colour; indoor black / outdoor light blue until area data is read), black line, ground (8), SKY #252 horizon; AREA map from frame tiles 260-275 + the party arrow 256-259. |
| ECL scripts: 2 header bytes; 5 entry points; opcode + operands (code, low, [high] / packed string); per-game opcode table (Curse's in the profile) | `src/engine/ecl.*` | **v0.7.0**: decoding + following jumps from the entry points; finds each area script's first LOAD FILES (GEO block) and LOAD PIECES (wall sets). Several scripts can load one GEO block with different sets (Curse area 2 GEO 1: script 1 loads set 3 = block 3, a set the walking scripts 2-4 don't use); the Walk Test keeps one entry per map with the sets most of its scripts load (tie: the later script) - right until scripts run (M4). PoolRad's scripts use other opcodes (its profile will need its own table). Running scripts = M4. |
| ECL script machine: 65 opcodes 0x00-0x40, packed text (3 bytes = four 6-bit characters). Memory: 0x4B00-0x4EFF area words (0x4BC5 map block, 0x4BC6-0x4BCC clock slots counting 10 / 10 / 6 / 24 / 30 / 12 / 256 - slot 1 minutes, 2 ten minutes, 3 hours; 0x4BE6 in a 3D area, 0x4BF0 / 1 last square, 0x4BF2 last script, 0x4BFB area view blocked, 0x4BFC game speed, 0x4BFD / E outdoor / indoor sky colour (index into the program's sky table, DS 0x6D9A), 0x4C00-0x4C20 the script's own variables), 0x7A00-0x7BFF a table, 0x7C00-0x7FFF game / character fields (0x7EC9 >= 0xFF: no move this step, 0x7ECA search flags, 0x7EE1 head picture, 0x7F12 area, 0x7F22 / 24 / 26 > 0x80 load wall set 1 / 2 / 3), 0x8000+ the script's bytes, 0xC04B-0xC04F party x, y, facing, wall ahead, roof flags. Compare flags in the order ==, !=, <, >, <=, >= (op1 vs op2). A step: the area script's entry 0 runs on the square the party is on, then the party moves (unless 0x7EC9 says not; a minute, ten when searching), then entry 1; NEWECL = the new script's entry 4 (first), then 0, then 1; a new game = ECL<start area> block <start script> (Curse: ECL2 #1) | `src/engine/ecl_vm.*`, `src/ui/play.*` | **v0.8.0** (Curse): the viewer's **Play Test** runs a new game from the opening on Tom's files: text (word wrap, pages), menus on the menu line (tap a word), vertical list menus (tap a line), pictures (PIC / BIGPIC / HEAD + BODY), map + wall loading, the clock, NEWECL. Everything that needs a party (combat, treasure, checks, NPCs) is logged and passed over; v0.9.0: INPUT NUMBER / STRING typed on the menu line with an on-screen keyboard (320x240: letters over the top of the game screen, Del / Space / Enter under it; 480x320: letters under the game screen, Del / Space / Enter in the Companion strip); event pictures (PIC<area>) animate while the game waits at a menu (frame delay x 0.1 s) and step a frame on CALL 6803 (+ the game's delay); the wilderness map (BIGPIC 0x79) gets the blinking square at the party's place (columns / rows read from the program: Curse DS 0x6D5A / 0x6D7A, 32 places, place number = script word 0x4CA1; not after event picture 0x50 was shown); area changes (SAVE area -> 0x7F12, NEWECL) load the new area's files, and leaving a big picture for a 3D area brings the exploring screen back. CALL addresses: 2E10 redraw, C01E step forward, 6803 picture frame. v0.9.2 (Tom's v0.8.0 report): CALL 2E10 draws the 3D view again (the event picture goes; CLEAR BOX keeps it); locked / barred doors stop the party with "Locked." on the menu line (v0.60.0, facts: the Project's claude/door_facts.md: "Locked. Bash Pick Knock Exit" - Bash while allowed, every member in party order by the Strength in use until one breaks it (the original's table; a not-pickable door is too strong below 18/91 and takes Bash away until a step); Pick with a thief whose level counts, each okay member's d100 against Open Locks, once until a step; Knock when someone has it memorized, which uses it up and lets the party through without opening the door; nothing but Exit to offer: no menu. Bash / Pick open both sides in the map in memory; the area reloaded locks them again. Success: a step (sound, a minute); either way the view is redrawn and the arrival script runs); encounters: SETUP MONSTER (sprite, max distance, picture) / APPROACH / ENCOUNTER MENU show the monsters' SPRIT<area> sprite in the 3D view (frame = distance 0-2 = open squares ahead up to the max; frame x / y + 3 cells; colour 0 see-through, 13 drawn black), then at distance 0 their picture (PIC, or HEAD + BODY when 0x7EE1 names a head) after the game's delay - not while the encounter menu is up; the encounter menu's texts by distance, Combat / Wait / Flee / Advance (Parlay when close or outdoors) and the script's result table (0 monsters flee, 1 combat, 2 party flees, 3 parlay; the party's speed is taken as 12 until there is a party). An instruction budget (50,000 a run) stops a script that never ends. v0.37.0 (facts in the Project's claude/ecl_stub_facts.md): **the scripts and the party** - the selected character's fields are read and now WRITTEN at 0x7C00-0x7FFF (the word is always stored; then money 0x7CBB copper / BD silver / BF electrum / C1 gold / C3 platinum, 0x7CB8 the control byte (over 0xB2: - 0x32), 0x7CF7 / F9, spell places 0x7C20-0x7C70 = record 0x1F-0x6F, 0x7D00 >= 0x80 out of action (0x87 stoned), 0x7D0C the side (0 / 0x80 the computer's / 0x81 the enemy's), 0x7CC9 the magic-user level + a human's old one past it); the string at 0x7C00 = the selected character's name (PRINT / SAVE of a string). **LOAD CHARACTER** n: member n & 0x7F selected (none: 0x7D00 reads 0 once); EXIT / PROGRAM put back the selection the run began with; + 0x80 after the script wrote 0 to 0x7C00 and 0x7D00: the member leaves the party. **WHO** "<prompt> Select" over the party list (a tap on a line, Select). **ROB** who (0 the selected, else all), % of the money (all seven kinds), item chance (heavy items cut it: over 255 - 90, over 24 - 50, and it stays cut). **DAMAGE** f, dice, sides, bonus, t: rolled once; f & 0x80: saves (bonus f & 0x1F, type t & 7; f & 0x40 everyone - 0x20 no save -, else t & 0x80 the selected - type t & 7 - 1, 0 no save -, else a random member; f & 0x10 hurts even when saved; no half damage), else f attacks on random members (d20 + t > the AC byte; 1 misses, 20 hits; damage rolled again each); dead members skipped; HP 0 unconscious, below dying, 10 below dead; "  NAME is hit FOR N points of Damage." / "  NAME dies. " a line each, then "press <enter>"; everyone down: "The entire party is killed!" and the party menu. **ADD NPC** id, morale: MON<area> block id (record, effects, items) joins at the end of the party (8 at most), selected, control byte 0x80 | morale / 2, its fight icon CPIC<area> block id. **DUMP**: the selected member leaves. **DESTROY ITEMS** type: everyone's items of that type. **FIND ITEM** (anyone carries the type) / **FIND SPECIAL** (the selected has the effect): == / !=. **SPELL** id: its place in the first member's memorized list holding it and the member (none: 0xFF, the last member). **PARTYSTRENGTH** (HP + 5 x AC over 0 + 5 x to-hit + 8 x magic-user + 4 x cleric levels) / 10 a member, a byte sum. **PARTY SURPRISE**: a ranger (class 4 / 10) in the party. **CHECKPARTY** as the game does it: 0 - anyone with effect op2; 0x80A4-0x80AB a thief skill, 0x809E movement: lowest, highest, average; anything else (Curse's one use) writes nothing. 0x7F3E follows the party's size. The encounter menu's Flee / the monsters' flight use the party's slowest / fastest movement. CALL B200 (a sound) passed over quietly. The machine's own words (encounter notes, DAMAGE's lines) come from GAME.OVR (profile script_words). v0.38.0: **PROGRAM 9** the camp (the script ends when the party breaks camp), **PROGRAM 3** the party killed (the party menu); PROGRAM 8 (the game won) to come. **TREASURE 0x80 + n: n random items** (facts in the Project's claude/random_treasure_facts.md; `engine/treasure.*`): d100 1-60 a weapon or armour by a second d100 (1-47 / 50-59 that type - the heavy crossbow a shield -, 48-49 a shield, 60-90 a sword by d10: long 1-4, broad 5-7, bastard, short, two-handed; 91-94 arrows, 95-97 a ring of protection, 98-100 bracers), 61-85 a magic-user scroll, 86-92 a clerics', 93-98 d15: 1-9 a potion (d8 1-5 healing, 6-8 extra healing), 10 giant strength, 11-15 the wand of magic missiles, 99-100 a shield. Weapons and armour +1 (d20 1-14) / +2, never cursed, words 1-2 hidden until identified ("Long Sword" -> "Long Sword +1"; armour "Chain Mail +2"), weights and values per plus by type (profile), arrows / quarrels 10, darts 5; bracers AC 6 / AC 4; a javelin 1 in 5 of lightning. Scrolls: d3 spells, each a d5 band and a die (profile bands), 300 x band gold each. Potions, the wand and the javelin of lightning: the program's rows of ready-made items (DS 0x0830, 7 x 8 words: words, weight, value, effects), plus 1. |
| Characters (Curse): the game's own 422-byte record (.SAV in a saved game, .GUY), kept as read so saving writes it back unchanged: name (Pascal, 15), stats (current / full, Str Int Wis Dex Con Cha Str00) at 0x10, THAC0 0x73, race 0x74, class 0x75, age 0x76, HP max 0x78, saving throws 0xDF, hit dice 0xE5, thief skills 0xEA, control 0xF7 (>= 0x80 NPC), money 0xFB (7 x u16: copper, silver, electrum, gold, platinum, gems, jewellery), levels 0x109 (8 classes) and before a class change 0x111, sex 0x119, alignment 0x11B, experience 0x127, health 0x195, in the fights 0x196, side 0x197, AC 0x19A (shown 60 - it), HP 0x1A4, movement 0x1A5; items .SWG (63 bytes each, 16 max), effects .FX (9 bytes each) | `src/engine/party.*` | **v0.14.0**: read and checked on the GOG release's sample party (`SAVE/CHRDATA1-6.SAV`: Mathew, Mark, Travis, Ledera, Shara, Philippe). The scripts read the selected character at 0x7C00 + 0x15 (Int), 0x18 (Con), 0x72 race, 0x73 class, 0x9B, 0xA0, 0xA5-0xAC thief skills, 0xB8 control, 0xBB-0xC3 money, 0xC9 magic-user level, 0xD6 sex, 0xD8 alignment, 0xE4, 0xF7, 0xF9, 0x100 (1 in the fights, 0x80 not), 0x11B movement, 0x2B1 / 0x2B4 its place in the party, 0x2CF a charisma percentage (coab's list); party size = 0x7F3E. |
| Saved games (Curse): `SAVGAMA.DAT`-`SAVGAMJ.DAT` (13,149 bytes) in the save folder (`CURSE.CFG`'s path line, `C:\SAVE\` = the game folder's `SAVE`): game area; script memory 0x4B00-0x4EFF (0x800), 0x7C00-0x7FFF (0x800), 0x7A00-0x7BFF (0x400); the script (0x1E00); x, y, facing, wall ahead, roof; last / current game state; 3 x (WALLDEF block, set); party count; 8 x 41-byte character file names (`CHRDATA1`; + .SAV / .SWG / .FX) | `src/engine/savegame.*` | **v0.14.0**: GOG's `SAVE/SAVGAMA.DAT` is a new game in area 2 at 7,13 with the sample party (no script run yet: 0x4BF2 = 0). **v0.18.0 saving**: Save Current Game (party menu) and Camp -> Save: "Save Which Game: A B C D E F G H I J", "Saving...Please Wait"; the file as above (state 0 party menu / 2 camp, last state 4 in a 3D area / 3 outdoors; the wall sets as loaded, a multi-part block's later sets -1), then each character as CHRDAT<slot><n>.SAV (the record) / .SWG (items; removed when none) / .FX (effects; removed when none). A read-then-write of GOG's SAVGAMA.DAT gives the same bytes but for the unused ends of the name slots. The engine keeps the journal entries met beside it: `_CYD/<folder>/SAVGAM<slot>.JNL` (kind, number byte pairs). BEGIN marks the script as run (0x4BF2) after its first run, so a game saved after that resumes where it was. |
| Camp (Curse; the exploring menu's Encamp): "The party makes camp..." (1, 18, colour 10), "Camp: Save View Magic Rest Alter Fix Exit" (the words in START.EXE image 0xB00A, "Camp:" / the line in GAME.OVR 0x1B382 / 0x1B34E) | `src/ui/play.*` | **v0.18.0**: Save, View, Exit (a tap on a character selects them). v0.28.0: **Magic** ("Cast Memorize Scribe Display Rest Exit", START.EXE 0xB033) - **Memorize**: "NAME's" + "Spells in Grimoire" (row 1, at the name's length + 4), the spells they know and can use (clerics Wis 9+, druid spells for rangers past 6th level, magic-users Int 9+) by level under "1st Level" headings (START.EXE 0xB480, colour 13 - the lists' heading colour, v0.29.0) from row 5 to 15, "NAME can memorize:" (row 19) and a row a kind ("    Cleric Spells:" with the spells still to choose at columns 20, 23 ...), "Choose Spell: Memorize Next Prev Exit"; leaving with spells chosen: "Spells to Memorize" and "Memorize these spells? Yes No" (No forgets them; already memorizing: "Memorize These Spells?"); "NAME is in no condition to memorize spells", "NAME cannot memorize any spells". The list in the record at 0x1E (84 spell numbers, + 0x80 while being memorized), hours before the first spell at 0x72 (engine/magic). **Rest** (camp and magic menu): "Rest Time:" (row 17) DD:HH:MM (the unit being set in colour 15), "Rest Days Hours Mins Add Subtract Exit"; the time the spells need (4 hours' start, 6 for spells past 2nd level, + 15 minutes a spell level, the longest in the party). Resting, five minutes a step: the clock, effects running out, a day heals everyone 1 HP ("The Whole Party Is Healed"), spells memorized one after another once the start has passed (3 steps a level; "NAME has memorized SPELL"), an encounter check every 0x7ED2 steps at 0x7ED3 % ("Your repose is suddenly interrupted!": the camp ends and the area's camp-interrupted script, entry 3, runs); a tap asks "Stop Resting? Yes No". Leaving the camp forgets spells not yet memorized. v0.29.0: **Cast** (the magic menu, and the exploring menu's Cast for a character who is okay): "NAME's Spells in Memory" (rows 5-22, the chosen line highlighted), "Choose Spell: Cast Next Prev Exit"; "NAME is in no condition to cast any spells", "NAME has no spells memorized" (and back). A spell for fights (the spell table's targets byte 0): its name (row 19), "can't be cast here..." (row 20), "Lose it? Yes No". Else "NAME casts" / the spell (rows 19-20) for the game's delay (speed x 100 ms; a tap moves on); a spell for one member: "Cast Spell on whom Select Exit" over the camp screen (tap a member; Exit keeps the spell); the spell leaves the caster's memory and what it did is said a line at a time ("NAME is Blessed", "NAME is fully healed" / "partially healed", "is Cured", "can see", "is unpoisoned", "is raised", "is un-cursed", "has an item un-cursed", "is unaffected"), then the list again. The spell table (16 bytes a spell in START.EXE's data segment: class, level, range, duration fixed + per level, targets 1 self / 2 a member / 4 the party, effect, when) gives the effect and how long it lasts (minutes: fixed + per level x the caster's level for the spell's kind; non-casters 6); what each spell does outside combat is the profile's camp table (engine/spells): effects (Bless, Prayer (side x 16 + level), Mirror Image (1d4 x 16 + level), Haste (the first `level` members; a slowed one is cured instead), protections, resistances, Shield, Invisibility, Detect ..., Find Traps, Read Magic, Minor Globe; the effect's data = the caster's level; a running one of the same kind gives way), cures of wounds (1d8, 2d8+1, 3d8+3; outside a fight the dying come round and the unconscious wake), Cure Blindness, Cure Disease (disease 0x22; weakness 0x2B with 0x2C and 0x1F; 0x32 with 0x39), Slow Poison (the poisoned: 1 HP at least, effect 0x16 with data 0xFF, the poison's harm 0x0F held 10 minutes), Neutralize Poison, Remove Curse (the curse effect, else the first cursed item comes off), Raise Dead (the dead or animated, Constitution left, not elves: 1 HP, a point of Constitution). Not in the engine yet (the spell stays in memory): Enlarge, Friends, Strength, Spiritual Hammer, Dispel Magic, Restoration, Fire Shield - they change ability scores, make weapons or need choices that come with combat. **Display**: each member's name (colour 11), their effects (colour 10: effects named after the first spell 1-56 that gives them, and the named ones in GAME.OVR - "Poisoned", "Regenerating" ...), " <No Spell Effects>", a page of rows 4-22, "Next Prev Exit". Effects run out as game time passes (walking 1 minute a step, searching 10, scripts' CLOCK, resting). v0.30.0: **Alter** ("Alter: Order Drop Speed Icon Pics Exit", START.EXE 0xB05C; words in GAME.OVR): Order - "Party Order: Select Exit" (tap a member), Select: "NAME has been selected" and "Party Order: Place Exit" - a tap on another line moves them there (the games: arrow keys), Place puts them down; Drop - "NAME will be gone", "Drop from party? Yes No": "NAME bids you farewell" (or "is dumped in a ditch" when they can't fight; gone, not saved), No: "Breathes A sigh of relief"; the last member: "quit TO DOS: Yes No" (Yes leaves the Play Test); Speed - "Game Speed = N (0=fastest 9=slowest)" (row 18), "Game Speed: Faster Slower Exit" (the area word 0x4BFC, saved with the game); Icon (combat icons) and Pics: to come. **Fix**: nothing lost, nothing happens; else the healers' (okay members') cure spells in memory and the cures they would memorize again (a day's slots of each cure's level) are rolled, a rest of 4 hours (6 for cures past 2nd level) + 15 minutes a spell level (the longest healer; shorter when the party lost less than the healers can heal - 27 a 1st-level healer, 34 / 78 past 2nd / at 5th level - divided by that ratio), then the rolled healing shared out in party order. Resting as Rest does (encounters, "Stop Resting?"); stopped or interrupted: no healing. v0.31.0: **Scribe** ("NAME is in no condition to scribe any scrolls"): "NAME's Spells on Scrolls" (rows 5-22; the scrolls they can read - identified, or Read Magic (effect 0x10) on them, or a cleric with a clerics' scroll, which makes them known), "Choose Spell: Scribe Next Prev Exit"; "You already know that spell", "You are already scibing that spell" (the game's spelling), "You can not scribe that spell." (no slots of its kind and level) on the menu line for the game's delay; leaving with spells marked: "Spells to Scribe" and "Scribe these spells? Yes No" (No forgets them; already scribing when it opens: "Scribe These Spells? Yes No" first, No forgets them and shows the scrolls); none: "NAME has no copyable scrolls". Scrolls are items of slots 11-13 (ITEMS file; 12 a clerics'), their spells in the item's effect bytes 0x3C-0x3E (+ 0x80 being scribed), the second name word counting them ("With 1 Spell" 0xD2 ...). The rest time counts the spells being scribed like those being memorized; resting scribes first ("NAME has scribed SPELL"): the spell into the spell book, off the scroll (its word one less; used up below "With 1 Spell": the scroll is gone). Leaving the camp forgets scribing not yet done. Still to come: magic resistance and the spells' workings in fights (M6). The games ask "Quit TO DOS" after a camp save; the Play Test doesn't (its Esc key leaves). |
| Combat (Curse): LOAD MONSTER (MON<area>CHA.DAX block = the monster's 0x1A6-byte record, the same layout as a character's; SPC = its 9-byte effects, ITM = its 63-byte items; copies of it - 0 = 1 - up to 63 monsters; CPIC<area> block = its icon, + 0x80 the attack frame), SURPRISE (two d6 against d + 2 - a and b + 2 - c; the fight's surprise word 0x7ECB: 2 our side, 4 the enemies), COMBAT with monsters. The battlefield: 50 x 25 squares of 24 x 24 pixels, a ground value each (the program's ground table, DS 0x26A4: move cost, eye height, obstacle height, picture); indoors drawn from the 3D map 6 squares east-west and 2 north-south of the party, slanted (a map square (dx, dy) = a block at (21 + 6dx + 5dy, 10 + 5dy): floor, wall strips, doors, corners by the sides' codes - 0 open, 1 wall, 3 door; tables and chairs in rooms), outdoors a random field by the place's terrain flags (DS 0x0354: a stream, trees and logs, ponds, rocks, grass); pictures DUNGCOM / WILDCOM block 1 and RANDCOM (table, chair, clouds, a body). Placement: the party at its square facing the enemies, the monsters the encounter's distance ahead (no further than the party sees) facing back; each side fills ranks of an 11 x 6 grid (DS 0x0310 shapes, 0x0300 / 0x0308 rank centres, 0x02EC formation facings, 0x02DC fallbacks behind and to the sides), half the side in the front rank; no room: a monster is out of the fight; a fallen party member leaves a body. The screen: the combat frame, a 7 x 7 view (pixel 8, 8), the view following the party's fighters (radius 2); the one whose turn it is on a grey box (COMSPR 0x19); party icons composed from CHEAD (head) over CBODY (body; + 0x40 normal size, + 0x80 attack), recoloured by the record's six colour bytes (0x145: defaults 1 2 3 4 6 7 -> low nibble, + 8 -> high nibble); monsters' CPIC icons (24 x 24 to 48 x 48); facing west-ish (4-7) mirrored; the right panel: name (11 ours, 14 the enemy, 12 down), "Hitpoints", "AC", the readied weapon, the status word when down; messages from row 10 ("NAME / Attacks / TARGET / Hitting for N points of damage" or "and Misses", "goes down" / "and is Dying" / "is killed"), a page for the game's delay x 2 (a tap moves on); colours 0 and 8 swapped while fighting. Rounds: initiative d6 + Dex reaction (min 1; the surprised side -6, round 1 only; outside 0-20: 0), the highest acts first (ties: d100); movement points = movement x 2 (a straight step costs the ground's move cost x 2, a diagonal x 3); attacks a round from half attacks (record 0x11C / 0x11D: 3 = 1, 2, 1, 2 ...). The player's menu "Move View Aim Use Cast Turn Quick Done" (Move while points are left, Use with items, Cast with spells in memory and not hit this round, Turn for clerics): Move - "Move/Attack, Move Left = N", tap a square next to the fighter (or the pad: 8 N, 7 NW, 9 NE, 4 W, 6 E, 2 S) to step, into an enemy to attack, an ally asks "Attack Ally:" (Yes: NPCs on the party's side turn on it), the field's edge asks "Flee:" (faster than every enemy: "Got Away", even: d2, else "Escape is blocked"), "can't go there"; the menu line ends the move, Esc undoes it; leaving an enemy's side gives it its attacks (from behind), stepping next to a guard too. Aim - the fighters by distance, "Aim: Next Prev Manual Target Center Exit" ("Range = N" on row 23; Target when in reach: the weapon's range - 1, at least 1 - and in sight; a tap on someone picks them), missiles use the readied arrows / quarrels or the thrown weapon. Done - "Guard Delay Quit Bandage Speed Exit" (Guard: the first enemy to step next to them is attacked; Delay: last this round; Bandage: the first dying party member stops bleeding; Speed: "GameSpeed (N) : Slower Faster Exit"). Quick: the computer plays them. Attacks: d20 (1 misses, 20 hits) + the to-hit value (0x199) + the side's bonus (0x7F70 / 0x7F71) + Bless 1 / Curse -1 >= the target's AC (0x19A; from behind 0x19B); damage: the slot's dice (slot 2 then 1; the weapon's large dice against large targets) + bonus; the target turns to face the attacker. Damage: HP 0 unconscious, -1 to -9 dying (bleeding a point a round, dead past 9), -10 dead (HP kept 0). The computer: keeps its target while it's in sight, else a random one in sight, else the nearest; attacks when next to an enemy, else steps toward it (straight, or up to 2 directions to either side, never further away), else guards. A round's end: a minute of game time, bleeding, "Your Teammate is Dying"; over when a side is gone or 15 rounds pass without an attack. The end: won / fled (any running and none standing) / lost (no one standing): result 0x7EC7 = 0 / 0x81 / 0x80 for the script, 0x7EC8 = the enemies out; experience = each beaten monster's 0x13E x its rolled HP (0x12C) + 0x13C, + the treasure's worth (gold worth + 250 a gem + 2200 a jewel, + 400 x plus a magic item) shared by those standing (+10% for a prime requisite over 15; multi-classes divided); the effects that end with a fight go (profile fight.ends); the running come back, the dying fall unconscious, the unconscious with HP stand; a flight leaves the fallen behind; a lost fight: "The monsters rejoice for the party has been destroyed" (the party is gone: the party menu). "The party has won." / "The party has fled." / "Each character receives N" / "experience points." / "press <enter>/<return> to continue". The treasure (the beaten monsters' coins and items, and what TREASURE set out; COMBAT with no monsters: "The party has found Treasure!"): "View Take Pool Share Exit" over the exploring screen ("View Take Pool Exit" with items only, "View Pool Exit" with nothing), Take: "Take: Money Items Exit", coins as in shops, items "Items: " ... "Take" (newest first; too heavy: "Overloaded"); Exit with treasure left: "There is still treasure left.  Do you want to go back and claim your treasure?" Yes No (what's left is lost). | `engine/combat.*`, `src/ui/play_fight.inc` (part of play.cpp) | **v0.32.0** (rules learned from coab - facts in the Project's claude/combat_setup_facts.md and combat_rules_facts.md - checked against the program's tables). v0.33.0: **Cast** in fights - "NAME's Spells in Memory" (the list screen), a camp spell: "Camp Only Spell"; a casting time (spell table byte 12 / 3 > 0): "Begins Casting" - it goes off when their turn comes round again this round (their delay = that, or 1), lost if they're hurt first ("lost a spell"); then the targets by the spell's aim (byte 6: 0 themselves, 1-4 that many picked with Aim's Target - "Already been targeted" -, 8-14 an area of radius & 7 round a fighter or a square picked with Manual; 5 and 15 one), reach = fixed + per level x the caster's level (touch: 1), "Casts a Spell" with "Spell: NAME" on row 23, the spell out of memory, and what it did a page each: effects ("is Blessed" - Bless only on the caster's side, Curse only on the other -, protections, resistances, Shield, Invisibility, Mirror Image, Prayer - its side +1 to hit, the other -1 -, Haste - twice the moves and attacks, Slow half), wounds cured ("is fully healed" / "is partially healed"; the dying fall unconscious), damage ("takes N points of damage from Fire"; Magic Missile (level + 1) / 2 missiles of 1d4 + 1, Burning Hands the level, Shocking Grasp 1d8 + level, Fireball level d6, Flame Strike 6d8, Cause Wounds; touch spells must hit; a save as the table says - byte 8: 1 none, 2 half; saves d20 (1 fails, 20 saves) + the record's save bonus 0x186 >= the record's save 0xDF + type), Sleep (4d4 by Hit Dice - 1 a die up to 1, 2, 4, 6, then 10 for monsters / 20 - "falls asleep"), Hold Person (a save at -2 / -3 for one, -1 for two, 0 for more; "is held", "is Unaffected"). The asleep, paralysed, held and helpless (fight facts) don't act, and any blow kills: "NAME slays helpless TARGET with one cruel blow" (to -5). Protection from Evil / Good: evil / good attackers -2. Attacking makes the invisible seen; the computer doesn't pick invisible targets. Everyone's effects run a minute each round. **Turn** (clerics, once a fight): "NAME turns undead...", one d20, d12 undead at most, the weakest in sight first (record 0xE9), the program's table (DS 0x0369, [type x 10 + column]; columns: levels 1-8, 9-13, 14+): d20 >= |value| - positive "is turned" (it flees for the field's edge), else "Is destroyed" (and a few more may go); none: "Nothing Happens...". Fleeing monsters run off the field ("Got Away" / "Escape is blocked"). v0.34.0: **the computer's spells** (monsters and NPCs with spells in memory, once a turn before moving): d7 tries from priority 7 down, three random spells from memory each, a spell taken when its priority (spell table byte 13) is at least the try's; a cure only on themselves below half their HP; effects for themselves or their side only when they don't have them yet; against the enemy: a random visible one in reach and sight (bolts from it on, areas round it, one to four as the aim says) - an area with one of their own side in it only when that one would pass a save (+8 the party's side, -2 the monsters'); a casting time: "Begins Casting" and the stored targets when their turn comes back. **Lightning Bolt**: level d6 (save half) to everyone on a line of 7 squares from the target away from the caster (a wall stops it). **Stinking Cloud**: everyone round the square (radius 1): a save, or "chokes and gags from nausea" (helpless d4 + 1 rounds); saved: "starts to cough". Hold Monster, Slow ("is Slowed", the other side), Ray of Enfeeblement, Cause Blindness, Cause Disease (their effects). **Morale** (monsters and NPCs, once a turn): morale = (the control byte 0xF7 & 0x7F) x 2 (over 102: 0), Bless +5 / Curse -5; it breaks when the HP lost (%) is more, and their side's health (% of HP left, + the same) is under 100 - the script's morale word (0x7EC6) or they're on the party's side: "flees in panic" when no enemy is faster, else "Surrenders" with Int (0x13) over 5 (out of the fight, beaten). v0.35.0: **Use** in fights - the items screen ("Ready Use Drop Halve Join Exit"; Exit back to the fight menu; Ready / Drop / Halve / Join don't end the turn), Use as outside fights (below) but the spell goes off at once (no casting time), "NAME uses an item" with "Item:" + its name on row 23, aimed like a spell (a spell against enemies starts at the nearest enemy), and the turn is over; Aim's Exit with nothing picked: "Abort Spell? Yes No" (Yes: "Spell Aborted", the use is gone and the turn over; No: aim again); a scroll the user can't read: "oops!" (the spell gone, the turn over). **The computer's items** (monsters, NPCs, Quick members; before their spells): d7 rounds from priority 7 down, their readied items that cast a spell (not scrolls) in order, judged by the spell table's line 0x17 back for spells past 0x38 (the games' way), the first with a use as for spells; a monster's item is its group's (a use off the shared pile). The items' own spells (the spell table's monster spells, 0x39-0x63): speed 0x39 (Haste, "is Speedy", 5d4 rounds), a lightning stroke 0x3C (1d6 + 20 along a line), paralysis 0x3D ("is paralyzed", 5d4 rounds), healing 0x3E / 0x63 (2d4 + 2, "is Healed"), the party invisible 0x3F (2d10 x 10 rounds), a fireball 0x40 (3, 5 or 7 d6), the wand's missile 0x41 (2d4 + 2); giant strength 0x3B, the protections 0x5F-0x61 and defoliation 0x62 not yet. v0.36.0: **missiles for the computer** (monsters, NPCs, Quick members): not next to an enemy, with a readied missile weapon it can use now - a bow / crossbow (ITEMS flags 1 / 0x80) with its arrows / quarrels readied, a thrown weapon (flag 0x10: it goes with the throw), another with a range (a sling: nothing goes) - it shoots at its target when in reach (the range - 1) and sight, else at a random enemy there; one arrow / quarrel of the pile goes (a monster's: its group's pile). The player's Aim uses the same rule (no arrows readied: a bow reaches 1). v0.39.0: **backstabs** - a thief (record 0x10F) with no weapon or a club, dagger, broad / long / short sword, drow long sword (profile), straight behind a man-sized target that has already had an attack this round: the rear AC 4 worse, damage x ((thief level - 1) / 4 + 2), "-Backstabs-"; **sweeps** - a fighter (record 0x10B = their sweep level; once a round) with fewer attacks left than that, attacking an enemy under 1 Hit Die next to them while more such enemies are next to them than attacks left: "sweeps", one attack on each (the target first, up to the level), the turn over; **free attacks** only from an enemy that isn't held, sees the one stepping away, and hasn't acted yet this round, or hasn't been attacked, or has them in its front (facing +- 2). v0.41.0: **pictures in flight** (COMSPR: 0-2 an arrow up / slanted / across, 3 an axe, 4 a flask, 5 a spell, 6 lightning, 7 a rock, 8 a sling stone, 9 sparkles, 10 a burst, 11 the skull, 0x19 the box): a picture steps 8 pixels (a third of a square) at a time from the attacker toward the target along a straight line, stopping a step short (none next door), drawn over the field and the field put back each step; shots by the flying item's type (profile): pointed (darts, javelins, quarrels, spears, arrows) - one picture by direction (up / slanted / across, the attack picture the other way, mirrored to the west), 10 ms a step, the whistle (0x0C); spinning (hand axes, clubs, glaives) - the 4-picture cycle (ready, ready mirrored, attack mirrored, attack), 50 ms, sound 9; flasks - the same, 50 ms, sound 6; slings - the stone's 2 pictures, 10 ms, sound 6 (a sling readied wins over what it throws); the rest a rock, 20 ms, sound 9; a hit's blow (7) after the flight. A spell flies from the caster to the square aimed at (an area's centre) or its first target: picture 5 (lightning 6), the 4-picture cycle, 30 ms (lightning 50), with the spell's sound; a spell aimed at several picked one by one flies on to each (sound 2). **Magic hits**: damage from a spell - the burst (10) on the fighter's square, 4 pictures 70 ms each, sound 3; an effect taking hold ("is Blessed", "falls asleep", healed, "is turned") - sparkles (9), the game speed + 1 times round, sound 4. **The fallen** ("goes down", "is killed", destroyed undead): sound 5, their squares blink skull / box 9 times 10 ms apart, the skull staying until the page ends. A tap cuts the pictures short. **The computer's weapon** (Quick members and NPCs in the party; monsters keep theirs): once a turn after its spells, each weapon its classes can use rated dice x sides + 8 x plus + 2 x the type's damage bonus (> 0) + 2 x (attacks - 1) for a launcher + 3 one-handed (0 cursed, or past 3 hands); the best missile weapon when it rates over half the best melee one (which must beat the bare hands' dice), has what it shoots (readied arrows / quarrels; a sling or a thrown weapon: itself) and can be used now (thrown, or no enemy next to them), else the best melee weapon (none: bare hands); a cursed weapon stays; two hands put the shield away, free hands take the best shield (plus + 1); a bow held with an enemy next to them: another weapon and the turn is over. Fixed: party members whose head and body pictures are 0 (the GOG sample party) fought without icons (taken for NPCs). v0.43.0: **clouds that stay** (Stinking Cloud; coab's facts): the square aimed at and those east, south-east and south of it - each that can be entered - become cloud (ground 0x1E, the green cloud picture) for the caster's level in rounds, "Creates a noxious cloud" (GAME.OVR 0x30788); those in it, and anyone stepping in later, breathe it: a save against poison or "chokes and gags from nausea" (helpless d4 + 1 rounds; their turn ends), else "starts to cough"; the computer steps into one only after passing a test save; a cloud laid over another remembers the ground under both; when it runs out (a round's end): "The air clears a little..." (0x10AE9) on the menu line, the ground back (a fallen party member's body where one lies). v0.49.0: the coughing (those who saved: effect 0x1E for the round) - at their turn "is coughing" (GAME.OVR 0x106C3), no items and no spells that round (its AC 2 worse: to come). v0.44.0: **Charm Person** (coab's facts): a person only - humanoid (record 0x11A at most 1) and not large (0xDE at most 1), else "is unaffected" (GAME.OVR 0x2FECA); a save as the spell table says; "is charmed" (0x2FED8): the charm effect (0x0B; its data the caster's side << 7, their own << 6, 0x20, the level) for the table's time, they join the caster's side run by the computer (a charmed party member fights for the enemy) and drop their target; when it runs out they go back to their own side; party members are back on the party's side after the fight whatever happens. v0.46.0: **cones** (coab's facts; `combat::cone`): the line from the caster through the square aimed at, its steps repeated on to the cone's length (straight steps 2 half-squares, slanted 3), stopped by the field's edge and before a wall; everyone but the caster on the line to its end and - with more rays - on the lines to the end moved a square to the right (2 rays) and the left (3) of the line's last step, friends included. **Fear** (3 rays, 6 squares): a save against spells or "runs in terror" (GAME.OVR 0x32A7A; effect 0x8E for the table's time): they flee for the field's edge, a party member run by the computer meanwhile; when it's over they stop fleeing (and come back under the player). **Cone of Cold** (2 rays, (level + 1) / 2 squares): level d4 + level, a save as the table says ("from Acid": coab's damage type). v0.47.0: **Cloudkill** (coab's facts): the square aimed at and the 8 round it become poisonous cloud (ground 0x1C, the blue cloud picture) for the caster's level in rounds, "Creates a poisonous cloud" (GAME.OVR 0x32F30); those in it when it's laid, anyone stepping in, and everyone standing in it at each round's end: Hit Dice 0-4 die, 5 unless they save against poison at -4, 6 unless they save, 7 and up unaffected - "is Poisoned" (0x35E53), "is killed" (the skull); the computer never steps in below 7 Hit Dice; it clears like the stinking cloud. v0.48.0: **Charm Monsters**: as Charm Person but any creature, the spell table's number of targets (each picked; the spell flies on to each). v0.61.0: the monsters' special abilities (roadmap 6; the Project's claude/monster_fx_facts.md). **Magic-users stay back**: a computer-run fighter with magic-user levels and no armour readied doesn't step toward its enemies (it guards) - spells, items and shots as before. Still to come: Detect on the treasure screen, a bolt's flight along each of its squares, the coughing's AC. |
| Sound (Curse; facts in the Project's claude/sound_facts.md): the games' own driver in START.EXE (segment 0x699, image 0x6990): INT 8 reprogrammed to 236.69 Hz (5041 PIT clocks); each tick a byte-code player runs four voices for the PC speaker and four for the Tandy 1000's sound chip (SN76489-type at port C0h: three square-wave tones, a noise channel, 16 volume steps of 2 dB). A voice: wait, byte-code pointer, frequency, slide, output, volume, volume step, a modulation table (a sine; 512 random bytes - the speaker's "noise"), phase, step, depth, wrap, two loop counters; each tick volume += step, frequency += slide, output = frequency + table[phase] x depth / 256, then the byte code when the wait runs out (FF set a field - field 0: wait, 0 = the end; FE loop; FC / FB call / return; FD select a voice; FA clear; notes unused by Curse). PC speaker: the first voice sounding, a square wave at 1193182 / output Hz, on or off; Tandy: tone N = output >> 6 (3579545 / 32N Hz), attenuation (0xFFFF - volume) >> 12, the noise control rewritten every tick (its shift register starts over, as PC emulators of the chip run it). Per sound (the games' numbers, 2-0x0D) a start for each voice: PC table seg:04A2, Tandy seg:054A, 21 entries; a new sound replaces the one playing. CURSE.CFG's second line chose the device (T / P / else silent); GOG's set-up gives P and no Tandy chip. Where they play: 2 a spell (in a fight), 3 / 4 magic hits, 5 a death, 6 a sling / oil flask, 7 each hit, 8 Lightning Bolt, 9 all missed, 0x0A a step / a turn (in the 3D view and in fights; CALL B200), 0x0B Fireball (CALL B200 when the script set 0x03DE to 10), 0x0C a missile's flight, 0x0D the title. | `engine/sound.*`, `hal/audio.*` | **v0.40.0**: our own sequencer plays the player's byte code (read at the start of the Play Test: seg:04A2-12BA, 3.6 KB) and a synth makes 22,050 samples a second for the DAC (GPIO 26, DMA, the amplifier on only while it sounds; a ramp to and from the middle voltage against clicks; ~5 KB of RAM while it plays). Device and volume: Settings (Tandy the default). In fights each message page plays its sound as it shows (a hit, a miss, a missile's whistle on a shot's first page, a death, the spell's sound with its first result page); steps and turns in the 3D view and in fights; DAMAGE's "dies". v0.41.0: 3 / 4 with the magic-hit pictures, 5 for anyone falling (not only the dead), a shot's sound by what flies (0x0C, 6, 9). Not yet: the title (the Screen Test), cutting a sound short when the game loads a file (the originals do). |
| Party menu (the games' first screen): the outer frame, the party list (below), the menu a line each from row 12 (first letter colour 15 at column 2, the rest colour 10), "Choose a function " (colour 13) on the menu line; entries: a table of 12 x (string[40] + "on" byte) in START.EXE (Curse image 0xB133); Create / Add / Exit on from the start, Drop, Modify, View, Remove, Save, BEGIN with a party, Load without one, Train where the scripts offer training (0x7EA8 != 0), Human Change with training and a character who can change. Party list ("Name" at column 1 / 17, "AC  HP" at 33, row 2; a character a row from row 4; the selected name colour 15, others 11 (12 out of the fights, 14 the other side); AC right-aligned at 34 (a "-" before negative ones), HP at 38, colour 14 when below the most, else 10), strings in GAME.OVR (Curse 0x37E31 / 0x37E36; prompt 0x20111, "Load Which Game: " 0x1EE80) | `src/ui/play.*`, `engine/profile.*` | **v0.14.0** (checked against the program's code). Taps: a menu line picks it, a character selects them (also on the exploring screen). Load Saved Game -> "Load Which Game: A B ..." (the slots found); BEGIN Adventuring -> the saved game's script again (its first run) or, when none ran yet, the area's start script, as the games do. BEGIN needs a party (v0.20.0; before that the Play Test began without one). (Save: v0.18.0, Create: v0.20.0, Modify: v0.42.0, Human Change: v0.45.0.) v0.27.0: **PROGRAM 0** (a script's party menu - the training halls) opens this menu in the game; BEGIN Adventuring goes back to the game screen and on with the script. **Train Character** (when 0x7EA8, the classes the hall trains, is set): "we only train conscious people" / "Training costs 1000 gp." / "We don't train that class here" / "Not Enough Experience" (on the menu line, the game's delay); else "NAME will become:" (row 4, column 4, the name in the party list's colour, the rest colour 10), "    a level 6 Paladin" (rows 5.., column 6; "and a level ..." after the first), "Do you wish to train? Yes No": "Congratulations...", 1000 gp paid, ONE class a session (of those with the experience and the hall's training: the one whose next level needs the most), its hit points rolled as in creation (engine/create: trainable, train_classes). v0.31.0: a magic-user's new level (or a ranger past 8th) brings a new spell: "NAME's Spells to Choose" (the spells of levels they have slots for, that they can use and don't know; rows 5-22), "Choose Spell: Learn Next Prev" (no Exit: one must be chosen), then "Congratulations...". After a message the party menu's own prompt comes back (it showed the last question before v0.31.0). Words: GAME.OVR 0x24C2E-0x24CDB. v0.42.0: **Modify Character** (rules learned from coab; `engine/create` can_modify / modify_stat / modify_hp / modify_done): only a character as made - experience 0, 8333, 12500 or 25000 and no former class - else "NAME can't be modified." (GAME.OVR 0x227F1); View Character's screen with the item being changed in colour 13 (a stat's value, the hit points, the name) and "Modify: Keep Exit" (0x22804 / 0x2280D). A tap on a stat, the hit points or the name picks it (the name again: the keyboard, "Character name:"); the keys' left / right arrows and turns take it down / up (the games' arrow keys), forward / turn around move to the item above / below. Stats: within the race's and sex's limits (the program's table), down no lower than the class's minimum (multi-class clerics' Wisdom 13); a fighter, ranger or paladin at Strength 18 goes on into 18/01 .. 18/00 (the race's and sex's top) and back down through it first. Hit points: from a point a hit die (with the Constitution adjustment) to every die at its highest (the class's dice, each class's Constitution bonus - fighters' extra from 17), averaged over the classes; Constitution keeps them in those bounds. Keep: the class values follow the stats (spell slots, thief skills, saves) and the rolled hit points (record 0x12C) are what's left after the Constitution bonus; Exit / Esc puts the character back as it was. v0.45.0: **Human Change** (coab's facts; `engine/create` can_change / change_classes / change_class): on the menu where training is offered, for a human with no former class; "Pick New Class" (GAME.OVR 0x3BB0D) over the classes they qualify for - the race's, not the present one, every stat the present class needs (a minimum of 9 or more) at 15 or more, every stat the new class needs at 17 or more, their alignment allowed - "Select ... Exit" (0x3BB2F); none: "NAME doesn't qualify." (0x3BB1C). Then experience 0, the present class's level kept as the former one, level 1 in the new class, the hit dice count kept (record 0xE6) and reset to 1, 2 half-attacks, spell counts and the memorized list cleared (a cleric: one first-level spell; a magic-user: detect magic, read magic, sleep - profile), the class values recomputed, items the new class can't use (not cursed) put away; "NAME is now a 1st level CLASS." (0x3BB36 / 0x3BB4B). |
| Add / Remove / Drop (Curse): Add: "Add from where? Curse Pool Hillsfar Exit"; Curse lists the save folder's .GUY files (422 bytes, not NPCs, not in the party) by name, "Add a character: Add Next Prev Exit" from row 2, "* " before those added; rules: 6 player characters (8 in all), "paladins do not join with evil scum", "too many rangers in party" (3), "NAME will tolerate no evil!"; ends when the party is full. Remove: saves the character as NAME.GUY (the name without spaces / punctuation, 8 letters) + .SWG / .FX, "Overwrite NAME? Yes No" when one exists; an NPC is dropped instead. Drop: "Drop NAME forever? " then "Are you sure? " (Yes No, No first): their files go, "You dump NAME out back." (or "NAME bids you farewell." when in the fights); No: "NAME breathes a sigh of relief." (words GAME.OVR 0x23595-0x23617, 0x1C506, 0x2255C-0x225A5, "Yes No" 0x32BF0) | `src/ui/play.*` | **v0.19.0**. Overwrite -> No cancels the Remove (the games ask for another file name - to come); Pool (Pool of Radiance characters) and Hillsfar: to come. |
| View Character (Curse; from the party menu and the exploring menu's View): the outer frame; name (1, 1, the party list's colours; "(NPC)" after it); sex, race, "Age n" (row 3, colour 15, a space between); alignment (row 4), class (row 5); STR ... CHA (rows 7-12, colour 10) with the full value at column 5 (6 below 10) and "(nn)" / "(00)" for exceptional strength at column 7; coins from jewellery down to copper, those held, from row 7 (name right-aligned to column 19, amount at 21); "Level" (1, 15) with the levels joined by "/" at 7 (a former class only below the current level), "Exp n" at 17; row 17 "AC" (value at 4), "THAC0" (9; 60 - to-hit bonus at 15), "Encumbrance" (22; at 34); row 18 "HP" (at 4, yellow when hurt), "Damage" (8; dice "1d2+6" at 15), "Movement" (25; doubled when slowed, halved when hasted, at 34); "Status" (1, 22) and the health word at 8; menu line: what the character can do. Names: tables in START.EXE (Curse image 0xB898 classes, 27-byte slots x 18; 0xBA7E races 10 x 8; 0xBACE alignments 17 x 9; 0xBB67 sexes 7 x 2; 0xBB75 coins 11 x 7; 0xBBC2 health 13 x 9); words in GAME.OVR (0x27094 "(NPC)" ... 0x276E8 "Movement", 0x27BA8 "Exit") | `src/ui/play.*`, `engine/party.*`, `engine/profile.*` | **v0.15.0** (layout read from the program's code). Weapon / armour lines (rows 20-21) need item names, and the menu offers only Exit until Items, Spells, Trade, Drop, Heal and Cure come. |
| Items (Curse): 63-byte records (characters' .SWG, ITEMn.DAX blocks = treasure / shop goods, MONnITM.DAX): type 0x2E, name words 0x2F-0x31 (word 3 first: "Long Sword", "+1", "Frost Brand"; 0 = none; hidden-word bits 0x35: 4 hides word 1, 2 word 2, 1 word 3 until identified), plus 0x32, plus vs saves 0x33, readied 0x34, cursed 0x36, weight 0x37, count 0x39 (printed first: "10 Arrows"), value 0x3A (gold), effects 0x3C-0x3E. Name words: 255 x 21-byte slots in START.EXE (Curse image 0xBC37 = word 1). An "s" goes on one word when there are 2+ (the games' rules; missiles take it on their name unless word 3 is 0x87 / 0xB1). ITEMS file: 2 bytes + 128 x 16 bytes per type: slot (0 weapon, 1 shield, 2 armour, 9 rings ...), hands, damage vs large, attacks, armour value (0x80 + AC points, absolute: plate 57 = AC 3; shield 0x81 = +1), damage vs man-sized, range, classes (& the character's class bits 0x12B), flags (1 arrows, 2 missile, 4 melee, 0x80 quarrels) | `src/engine/items.*` | **v0.17.0**: names checked against all of Curse's ITEM / MONnITM blocks ("Long Sword +3 Frost Brand", "Studded Leather Armor", "10 Arrows"). |
| Create New Character (Curse): "Pick Race" (dwarf, elf, gnome, half-elf, halfling, human - no half-orcs), "Pick Gender", "Pick Class" (the race's list, DS 0x3FFA: 14 bytes a race, count + classes), "Pick Alignment" (the class's list, DS 0x41DA: 10 bytes a class), each a list with "Select Exit" (heading colour 13); then the character screen and "Reroll stats? Yes No", "Character name: " (15 letters), "Save NAME? Yes No" -> NAME.GUY / .SWG / .FX in the save folder (Add Character to Party brings them in). The rules: defaults (base AC 50, THAC0 40, icon colours from DS 0x3EC3), the race's effects (con save, dwarf vs orcs / giants, gnome, elf sleep resistance, half-elf; paladin protection from evil, ranger vs giants), experience 25000 / 12500 / 8333 (one / two / three classes), age (DS 0x404E: 7 x (base, dice, sides) a race; multi-classes the dice's top), stats best of six 3d6+1, the age's effects (from the program's code: Str +1 -1 -2 -1, Int 0 +1 0 +1, Wis -1 +1 +1 +1, Dex 0 0 -2 -1, Con 0 -1 -1 -1 for each bracket past, DS 0x4124), the race / sex limits (DS 0x3F88), class minimums (DS 0x4174), Wis 13 for multi-class clerics, 18/xx strength for fighters, paladins, rangers; 300 platinum; hit points (better of two rolls, dice DS 0x822 / count 0x81A, the constitution adjustment, divided by the classes); first spells (clerics all of level 1, magic-users four); then training to the level the experience buys (DS 0x429B: 99 bytes a class - experience for levels 2-12, spell slots gained; race level limits). Class rules (THAC0 DS 0x3E3A, saves 0x45BE, thief skills 0x3EC0 / 0x3F20 / 0x3F33, spell slots, class flags 0x3EA2) checked against GOG's sample party | `engine/create.*`, `engine/classes.*`, `src/ui/play.*` | **v0.20.0**. To come: the combat icon editor (with combat), Modify Character, Train, Human Change (they use the same rules). |
| Rules (Curse): what the games keep up to date - encumbrance (items x count + coins), hands in use, AC (dex bonus + shield + rings / protection + the best of base AC and armour; magic armour drops ring bonuses), movement (armour over 150 / 399 weight: 9 / 6, +3 when 9 or less; carrying over the strength allowance + 0x200 / 0x300 / 0x400: 9 / 6 / 3), to-hit (THAC0 field; strength with melee weapons or none, dexterity with missiles, the weapon's plus, arrows' / quarrels' plus, +1 for elves with bows, short and long swords), damage (the weapon's man-sized dice, its bonus, strength, plus), attack level; strength groups 18/01-18/00; money: copper 1, silver 10, electrum 100, gold 200, platinum 1000 copper (gems / jewellery not counted); paying makes change as the games do; can't carry: 16 items or weight over the allowance + 1500 | `src/engine/rules.*` | **v0.17.0** (coab's rules, own code): recalculating the GOG sample party gives exactly their stored values. |
| The shop (Curse): a script sets 0x7F6C = 1 (0x7EE2 = a temple), the price factor 0x7F6D (0x10 normal, 1 = 1/16 ... 0x80 = x8), CLEARMONSTERS, TREASURE (coins + an ITEMn block of goods), COMBAT with no monsters loaded -> the shop: the picture stays, the party list, "Buy View Pool Appraise Exit" (+ Take, Share with coins on the counter). Buy: the goods last first, "Items: " + name (21) + price (9) from row 1, the chosen line highlighted; "Buy Next Prev Exit"; the selected character pays (the pool when they can't), "Not enough Money." / "Overloaded" on the menu line for the game's delay. A character's items (View -> Items): "NAME's Items", a bar at row 2, "Ready Item", " Yes  " / " No   " + name from row 5; Ready checks the class, the slot ("already using ..."), the hands ("Your hands are full!") and curses ("It's Cursed") | `src/ui/play.*` | **v0.17.0**: Buy, View, Items -> Ready, Exit (tap a line to choose it, then the menu word). v0.19.0: Pool (the player characters' coins on the counter) and Share (equal shares by coin kind from jewellery down, one more of the rest each, then whoever can still carry, 1500 + strength allowance; the rest stays); Exit with coins on the counter shares them out (the games' shopkeeper asks instead - to come). v0.25.0: the items menu as the games build it - Ready, Use (exploring / camp; magic items come with spells: "not in the engine yet"), Trade (player characters, not in shops: "Trade with Whom?" over the party list, Select Exit; too heavy / 16 items -> "Overloaded"), Drop ("Your X will be gone forever", "Drop It? Yes No"), Halve (fewer than 16 items; n -> n - n/2 and n/2, "Can't halve that"), Join (the same item's piles, 255 a pile), in shops Sell (half the value, piles of arrows / quarrels each, other piles count / 20; "I'll give you N gold pieces for your X", "Is It a Deal?", paid as N/5 platinum + the rest gold, "Overloaded.  Money will be put in pool." puts the platinum on the counter) and Id (200 gold from the character or the counter, "Not Enough Money"; "It looks like some sort of X" / "I can't tell anything new about your X"). Readied items: "Must be unreadied" (Trade / Drop / Sell), Use needs "Must be Readied". Words: GAME.OVR (profile item_words). v0.26.0: **Take** ("Select type of coin " over the kinds on the counter from row 2, "Select" (+ Exit, a convenience for touch); "How much X will you take? " typed on the keyboard; too heavy -> "Overloaded"); **Appraise** (NAME, "You have a fine collection of:" row 7, "3 Gems" / "1 piece of Jewelry" rows 9-10, "Appraise :   Gems  Jewelry Exit"; a gem's value by d100: 10 / 50 / 100 / 500 / 1000 / 5000 gp, a jewel's by d100 then a random amount; "The Gem is Valued at N gp." row 12, "You can : Sell Keep" (Sell alone when it can't be carried): Keep makes it an item (type 70, name word 0x65 gem / 0xD6 jewel), Sell gives N/5 platinum, too heavy -> the rest on the counter, "Overloaded.  Money will be put in Pool."; none -> "No Gems or Jewelry"); **leaving coins on the counter**: the shopkeeper's (priest's) words in the text area and Yes No: Yes stays, No leaves them behind (replaces v0.19.0's sharing out). **The temple** (a script sets 0x7EE2 = 1 then COMBAT with no monsters): the shop screen with "Heal View Pool Appraise Exit" (+ Take, Share); Heal: "NAME, how can we help you?" (row 1, colour 15), the ten cures from row 4 column 2, "Heal Exit"; a cure that does nothing for them: "NAME is not blind." etc. and "cast cure anyway: Yes No"; "<cure> will only cost N gold pieces." (rows 21-22, colour 10), "pay for cure Yes No" - the character pays, else the counter, else "Not enough money."; "NAME is cured." Cures (prices from the game's code): Cure Blindness 1000, Cure Disease 1000, Cure Light Wounds 100 (1d8), Cure Serious Wounds 350 (2d8+1), Cure Critical Wounds 600 (3d8+3), Heal 5000 (all but 1d4, and blindness, diseases, feeblemind), Neutralize Poison 1000, Raise Dead 5500 (1 HP; the Constitution / HP changes: to check), Remove Curse 3500 (the curse effect, else a cursed item comes off), Stone to Flesh 2000. The words are read from GAME.OVR when shown (profile shop_words). v0.35.0: **Use** (exploring, camp, fights - facts in the Project's claude/item_use_facts.md): not readied: "Must be Readied"; an item that casts a spell (not a scroll, the spell in its effect byte 0x3D & 0x7F, 0x3E under 0x80 - 0x80 and up works while readied instead; 0x3C its charges, 0: it never runs out): "NAME uses an item" and its name (rows 21-22) for the game's delay, then as the spell is cast ("Cast Spell on whom Select Exit", what it did), at the item's level (6; the items' own monster spells: the user's); a use goes off the item (one of a stack, else a charge; the last: gone). A scroll: "NAME's Spells on Scroll" (only one they can read - identified, Read Magic, a cleric with a clerics' scroll), "Choose Spell: Cast Next Prev Exit"; clerics and magic-users read either kind, thieves of 10th level 3 times in 4, else "NAME oops!"; the spell goes off the scroll (used up below "With 1 Spell"). A spell for fights: "That Item is a combat-only item...", "Use it? Yes No" (Yes: the use goes for nothing). Anything else: nothing. v0.39.0: items that work while readied (effect byte 0x3E 0x80 and up): 0x80 - the effect in 0x3D is theirs while it's readied (no time limit; gone when put away, robbed or destroyed); 0x84 - keyed to an alignment (0x3D & 0x0F): the wrong one takes 0x3D >> 4 damage and it won't stay readied. Still to come: those that change ability scores (gauntlets, girdles, ioun stones ...) and the ring of wizardry. |
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
- Logs: `_CYD/SCAN.TXT` (Card Scan), `_CYD/RESTART.TXT` (Restarts),
  `_CYD/ERRORS.TXT` (Errors: every error the Play Test showed, with the
  time since power-on) in a scrolling box: a tap in its top half goes up a
  page, bottom half down a page, dragging scrolls; long lines wrap; the
  last 32 KB of a longer file. They come off the board on the card.
- WiFi and emailing the logs were in v0.21.0 and taken out in v0.22.0
  (Tom): WiFi took ~23 KB of RAM for good, and the games need every KB.

## 6. Milestones

1. **Library and asset viewer** (v0.1.0, this build). Finds the games on the
   card, lists DAX files and blocks, draws EGA pictures (all frames), hex-dumps
   the rest. Proves: SD on every board (incl. the 2.8" with software touch),
   the DAX and picture decoders on Tom's real files, 1:1 vs 1.5x on 480x320.
   The chooser (a game tapped in the library; Tom, 2026-10-10, v0.53.0):
   four big keys filling the screen under the title bar - **Resource
   Test** (the game's DAX files a page at a time, then their blocks: what
   the chooser used to show), **Screen Test**, **Walk Test**, **Play
   Test**, each with a small line under its name. A game the engine can't
   play yet shows a note and Resource Test only; .TLB / .GLB games the note.
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
   again. As in the original, a tap doesn't stop it (Esc leaves the Play
   Test). v0.56.0 (facts: claude/import_facts.md): Add Character ->
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
   still to come. v0.58.0 (Tom - beyond the original, which lists only
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
   unaffected). Still to come: Enlarge / Reduce in fights, Spiritual Hammer, Animate Dead, Dispel Magic,
   Restoration, Dispel Evil, Confusion, Dimension Door, Fire Shield, Slow
   Poison in fights; the group-by-size aim (Faerie Fire, Charm Monsters)
   is a radius of 1 for now.
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
   lightning's bounces off walls.
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
  back as it was unless the screen was redrawn there.
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
- **Engine comforts** (things the originals didn't do, added for the small
  CYD screens) each have a switch in `src/app/features.h`, so a fork for
  bigger screens can drop them with a build flag (Tom, 2026-10-10):
  - `CYD_BIG_ICON_PREVIEW` (v0.59.0): the icon editor's big preview of the
    new combat icon - 480x320 always, in the Companion strip; 320x240 with
    Game menu -> Options -> Large Icons, in the editor's empty right side;
    a tap flips ready / action.

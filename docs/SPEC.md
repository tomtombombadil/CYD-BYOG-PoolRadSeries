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
  Settings -> "Game Screen: 1.5x" stays for now as a viewer-only experiment;
  the game screens won't use it.

### Controls (Tom, 2026-10-06; mockup `docs/controls-proposal.png`)

- **Tap what the game shows.** A menu word on the game screen ("AREA CAST
  VIEW ENCAMP ...") = pressing its key; a line in a list = choosing it (and
  Enter); a square on the combat map = stepping / aiming toward it; "press
  any key" prompts = tap the game screen.
- **Keys only for what isn't on screen**, changing with what the game does:
  - 320x240: one row of 8 square keys (~37x34) in the 40 rows under the game.
    Exploring: Side-step Left, Turn Left, Forward, Turn Right, Side-step
    Right, Turn Around (Tom, 2026-10-07: side-steps outside the turns), Esc,
    and a Companion key (auto-map, journal, party on a full-screen page).
    Combat: the 8 direction arrows.
  - 480x320: the 320x120 area under the game holds a 3x3 pad laid out like a
    numeric keypad (Tom): exploring 7 = Turn Left, 8 = Forward, 9 = Turn
    Right, 4 = Side-step Left, 6 = Side-step Right, 2 = Turn Around; combat:
    all 8 directions. Beside it Enter / Esc / Keys. The companion panel has
    Journal and Map.
- **The Menu key** (Tom, 2026-10-09; v0.16.0): the Play Test's key where
  the Walk Test has Area (320x240 row: 7th key; 480x320: the first key
  beside the pad) opens the engine's own screen, tabbed along the top:
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
| Title sequence (Curse): TITLE.DAX 1 at (0,0) 5 s; 2 at (0,0) + 3 at row 11 col 6, 10 s; 4 at row 11 col 0, 10 s; credits (outer frame + bars at rows 3 and 8), 10 s; a key skips each | `src/engine/profile.*` (steps), `src/ui/look.cpp` | **v0.4.0**, in the Screen Test. |
| Text window: printed a character at a time (game speed 4 = 12 ms) into a cell rectangle (text area rows 17-22 x cols 1-38; rows 21-22; combat panel rows 1-21 x cols 23-38); a word = punctuation + letters + punctuation + one space, wrapped whole if it (with its space) doesn't fit, longer-than-a-line words broken; leading spaces dropped after a wrap; area full = "Press any key to continue" (colour 13, row 24), clear, go on | `src/engine/text.*` | **v0.4.0** (behaviour from coab, own code). Touch: a tap = the key; a tap while printing finishes the page. |
| Combat screen: its frame ends at row 22; row 23 = a status line (range, "Spell: ...", "Item: ..."), row 24 = the menu as on every screen | `src/engine/layout.*` | Checked (coab). |
| Menu line (row 24): choices start at capitals / digits; prompt colour 13, key letters 15, the rest 10, the chosen word reversed (15 background) | `src/engine/text.*` | **v0.4.0**. Touch: tapping a word moves the highlight to it, shows it for 250 ms, then acts (its trailing space counts, so no gaps between targets; the tap area starts a row higher, at row 23 - Tom, v0.5.1). The chosen word stays chosen next time, as in the games. |
| Game icons: GOG's goggame-<id>.ico (ICONDIR) or goggame-<id>.dll (PE resources RT_GROUP_ICON 14 -> RT_ICON 3), copied by the player into the game's folder; images are DIBs (1/4/8/24/32 bit + mask) or PNG (GOG's 256 x 256, RGBA). The biggest is drawn, scaled down (alpha-weighted averaging) as big as the library page allows (~154 px on 320x240, ~194 px on 480x320; v0.7.1) | `src/engine/icon.*`, `png.*`, `inflate.*` (streaming, 32 KB window - based on CYD-Classic-Games' inflate) | **v0.6.0**: checked on the real goggame .ico / .dll of six games. START.EXE (DOS) has no icon; Support.ico is GOG's generic one, ignored. |
| GEO map block (1026 bytes): 2 bytes, then four 256-byte planes (a byte per square, x + y * 16): N / E wall types (nibbles), S / W wall types, a flags byte (>= 0x80 = under a roof: indoor sky colour), door states (2 bits per side: 0 solid, 1 open, 2 locked, 3 barred); wraps at the edges | `src/engine/geo.*` | **v0.7.0** (Curse). |
| WALLDEFn: n x 780 bytes = wall sets (5 pieces x 156 tile numbers in 10 groups: far front / sides, middle front / sides, near front / sides, far corner); a 2- or 3-part block fills the next sets too, its tiles are 8X8Dn blocks id*10+1.., and its later parts already number their tiles after the first's (every part moves by the first set's amount - v0.7.2 fix: shifting each part by its own set garbled Curse's area 5 / 6 walls); tile numbers 1-45 common (8X8D1 #203), 46-115 / 116-185 / 186-255 sets 1-3 (a set's own numbers stored as set 1's, moved up 70 / 140), 256+ frame tiles | `src/engine/view3d.*` | **v0.7.0** (Curse): drawn far to near like the games; sky (area colour; indoor black / outdoor light blue until area data is read), black line, ground (8), SKY #252 horizon; AREA map from frame tiles 260-275 + the party arrow 256-259. |
| ECL scripts: 2 header bytes; 5 entry points; opcode + operands (code, low, [high] / packed string); per-game opcode table (Curse's in the profile) | `src/engine/ecl.*` | **v0.7.0**: decoding + following jumps from the entry points; finds each area script's first LOAD FILES (GEO block) and LOAD PIECES (wall sets). Several scripts can load one GEO block with different sets (Curse area 2 GEO 1: script 1 loads set 3 = block 3, a set the walking scripts 2-4 don't use); the Walk Test keeps one entry per map with the sets most of its scripts load (tie: the later script) - right until scripts run (M4). PoolRad's scripts use other opcodes (its profile will need its own table). Running scripts = M4. |
| ECL script machine: 65 opcodes 0x00-0x40, packed text (3 bytes = four 6-bit characters). Memory: 0x4B00-0x4EFF area words (0x4BC5 map block, 0x4BC6-0x4BCC clock slots counting 10 / 10 / 6 / 24 / 30 / 12 / 256 - slot 1 minutes, 2 ten minutes, 3 hours; 0x4BE6 in a 3D area, 0x4BF0 / 1 last square, 0x4BF2 last script, 0x4BFB area view blocked, 0x4BFC game speed, 0x4BFD / E outdoor / indoor sky colour (index into the program's sky table, DS 0x6D9A), 0x4C00-0x4C20 the script's own variables), 0x7A00-0x7BFF a table, 0x7C00-0x7FFF game / character fields (0x7EC9 >= 0xFF: no move this step, 0x7ECA search flags, 0x7EE1 head picture, 0x7F12 area, 0x7F22 / 24 / 26 > 0x80 load wall set 1 / 2 / 3), 0x8000+ the script's bytes, 0xC04B-0xC04F party x, y, facing, wall ahead, roof flags. Compare flags in the order ==, !=, <, >, <=, >= (op1 vs op2). A step: the area script's entry 0 runs on the square the party is on, then the party moves (unless 0x7EC9 says not; a minute, ten when searching), then entry 1; NEWECL = the new script's entry 4 (first), then 0, then 1; a new game = ECL<start area> block <start script> (Curse: ECL2 #1) | `src/engine/ecl_vm.*`, `src/ui/play.*` | **v0.8.0** (Curse): the viewer's **Play Test** runs a new game from the opening on Tom's files: text (word wrap, pages), menus on the menu line (tap a word), vertical list menus (tap a line), pictures (PIC / BIGPIC / HEAD + BODY), map + wall loading, the clock, NEWECL. Everything that needs a party (combat, treasure, checks, NPCs) is logged and passed over; v0.9.0: INPUT NUMBER / STRING typed on the menu line with an on-screen keyboard (320x240: letters over the top of the game screen, Del / Space / Enter under it; 480x320: letters under the game screen, Del / Space / Enter in the Companion strip); event pictures (PIC<area>) animate while the game waits at a menu (frame delay x 0.1 s) and step a frame on CALL 6803 (+ the game's delay); the wilderness map (BIGPIC 0x79) gets the blinking square at the party's place (columns / rows read from the program: Curse DS 0x6D5A / 0x6D7A, 32 places, place number = script word 0x4CA1; not after event picture 0x50 was shown); area changes (SAVE area -> 0x7F12, NEWECL) load the new area's files, and leaving a big picture for a 3D area brings the exploring screen back. CALL addresses: 2E10 redraw, C01E step forward, 6803 picture frame. v0.9.2 (Tom's v0.8.0 report): CALL 2E10 draws the 3D view again (the event picture goes; CLEAR BOX keeps it); locked / barred doors stop the party with "Locked." on the menu line (Bash / Pick / Knock need a party); encounters: SETUP MONSTER (sprite, max distance, picture) / APPROACH / ENCOUNTER MENU show the monsters' SPRIT<area> sprite in the 3D view (frame = distance 0-2 = open squares ahead up to the max; frame x / y + 3 cells; colour 0 see-through, 13 drawn black), then at distance 0 their picture (PIC, or HEAD + BODY when 0x7EE1 names a head) after the game's delay - not while the encounter menu is up; the encounter menu's texts by distance, Combat / Wait / Flee / Advance (Parlay when close or outdoors) and the script's result table (0 monsters flee, 1 combat, 2 party flees, 3 parlay; the party's speed is taken as 12 until there is a party). An instruction budget (50,000 a run) stops a script that never ends. |
| Characters (Curse): the game's own 422-byte record (.SAV in a saved game, .GUY), kept as read so saving writes it back unchanged: name (Pascal, 15), stats (current / full, Str Int Wis Dex Con Cha Str00) at 0x10, THAC0 0x73, race 0x74, class 0x75, age 0x76, HP max 0x78, saving throws 0xDF, hit dice 0xE5, thief skills 0xEA, control 0xF7 (>= 0x80 NPC), money 0xFB (7 x u16: copper, silver, electrum, gold, platinum, gems, jewellery), levels 0x109 (8 classes) and before a class change 0x111, sex 0x119, alignment 0x11B, experience 0x127, health 0x195, in the fights 0x196, side 0x197, AC 0x19A (shown 60 - it), HP 0x1A4, movement 0x1A5; items .SWG (63 bytes each, 16 max), effects .FX (9 bytes each) | `src/engine/party.*` | **v0.14.0**: read and checked on the GOG release's sample party (`SAVE/CHRDATA1-6.SAV`: Mathew, Mark, Travis, Ledera, Shara, Philippe). The scripts read the selected character at 0x7C00 + 0x15 (Int), 0x18 (Con), 0x72 race, 0x73 class, 0x9B, 0xA0, 0xA5-0xAC thief skills, 0xB8 control, 0xBB-0xC3 money, 0xC9 magic-user level, 0xD6 sex, 0xD8 alignment, 0xE4, 0xF7, 0xF9, 0x100 (1 in the fights, 0x80 not), 0x11B movement, 0x2B1 / 0x2B4 its place in the party, 0x2CF a charisma percentage (coab's list); party size = 0x7F3E. |
| Saved games (Curse): `SAVGAMA.DAT`-`SAVGAMJ.DAT` (13,149 bytes) in the save folder (`CURSE.CFG`'s path line, `C:\SAVE\` = the game folder's `SAVE`): game area; script memory 0x4B00-0x4EFF (0x800), 0x7C00-0x7FFF (0x800), 0x7A00-0x7BFF (0x400); the script (0x1E00); x, y, facing, wall ahead, roof; last / current game state; 3 x (WALLDEF block, set); party count; 8 x 41-byte character file names (`CHRDATA1`; + .SAV / .SWG / .FX) | `src/engine/savegame.*` | **v0.14.0**: GOG's `SAVE/SAVGAMA.DAT` is a new game in area 2 at 7,13 with the sample party (no script run yet: 0x4BF2 = 0). **v0.18.0 saving**: Save Current Game (party menu) and Camp -> Save: "Save Which Game: A B C D E F G H I J", "Saving...Please Wait"; the file as above (state 0 party menu / 2 camp, last state 4 in a 3D area / 3 outdoors; the wall sets as loaded, a multi-part block's later sets -1), then each character as CHRDAT<slot><n>.SAV (the record) / .SWG (items; removed when none) / .FX (effects; removed when none). A read-then-write of GOG's SAVGAMA.DAT gives the same bytes but for the unused ends of the name slots. The engine keeps the journal entries met beside it: `_CYD/<folder>/SAVGAM<slot>.JNL` (kind, number byte pairs). BEGIN marks the script as run (0x4BF2) after its first run, so a game saved after that resumes where it was. |
| Camp (Curse; the exploring menu's Encamp): "The party makes camp..." (1, 18, colour 10), "Camp: Save View Magic Rest Alter Fix Exit" (the words in START.EXE image 0xB00A, "Camp:" / the line in GAME.OVR 0x1B382 / 0x1B34E) | `src/ui/play.*` | **v0.18.0**: Save, View, Exit (a tap on a character selects them). v0.28.0: **Magic** ("Cast Memorize Scribe Display Rest Exit", START.EXE 0xB033) - **Memorize**: "NAME's" + "Spells in Grimoire" (row 1, at the name's length + 4), the spells they know and can use (clerics Wis 9+, druid spells for rangers past 6th level, magic-users Int 9+) by level under "1st Level" headings (START.EXE 0xB480, colour 13 - the lists' heading colour, v0.29.0) from row 5 to 15, "NAME can memorize:" (row 19) and a row a kind ("    Cleric Spells:" with the spells still to choose at columns 20, 23 ...), "Choose Spell: Memorize Next Prev Exit"; leaving with spells chosen: "Spells to Memorize" and "Memorize these spells? Yes No" (No forgets them; already memorizing: "Memorize These Spells?"); "NAME is in no condition to memorize spells", "NAME cannot memorize any spells". The list in the record at 0x1E (84 spell numbers, + 0x80 while being memorized), hours before the first spell at 0x72 (engine/magic). **Rest** (camp and magic menu): "Rest Time:" (row 17) DD:HH:MM (the unit being set in colour 15), "Rest Days Hours Mins Add Subtract Exit"; the time the spells need (4 hours' start, 6 for spells past 2nd level, + 15 minutes a spell level, the longest in the party). Resting, five minutes a step: the clock, effects running out, a day heals everyone 1 HP ("The Whole Party Is Healed"), spells memorized one after another once the start has passed (3 steps a level; "NAME has memorized SPELL"), an encounter check every 0x7ED2 steps at 0x7ED3 % ("Your repose is suddenly interrupted!": the camp ends and the area's camp-interrupted script, entry 3, runs); a tap asks "Stop Resting? Yes No". Leaving the camp forgets spells not yet memorized. v0.29.0: **Cast** (the magic menu, and the exploring menu's Cast for a character who is okay): "NAME's Spells in Memory" (rows 5-22, the chosen line highlighted), "Choose Spell: Cast Next Prev Exit"; "NAME is in no condition to cast any spells", "NAME has no spells memorized" (and back). A spell for fights (the spell table's targets byte 0): its name (row 19), "can't be cast here..." (row 20), "Lose it? Yes No". Else "NAME casts" / the spell (rows 19-20) for the game's delay (speed x 100 ms; a tap moves on); a spell for one member: "Cast Spell on whom Select Exit" over the camp screen (tap a member; Exit keeps the spell); the spell leaves the caster's memory and what it did is said a line at a time ("NAME is Blessed", "NAME is fully healed" / "partially healed", "is Cured", "can see", "is unpoisoned", "is raised", "is un-cursed", "has an item un-cursed", "is unaffected"), then the list again. The spell table (16 bytes a spell in START.EXE's data segment: class, level, range, duration fixed + per level, targets 1 self / 2 a member / 4 the party, effect, when) gives the effect and how long it lasts (minutes: fixed + per level x the caster's level for the spell's kind; non-casters 6); what each spell does outside combat is the profile's camp table (engine/spells): effects (Bless, Prayer (side x 16 + level), Mirror Image (1d4 x 16 + level), Haste (the first `level` members; a slowed one is cured instead), protections, resistances, Shield, Invisibility, Detect ..., Find Traps, Read Magic, Minor Globe; the effect's data = the caster's level; a running one of the same kind gives way), cures of wounds (1d8, 2d8+1, 3d8+3; outside a fight the dying come round and the unconscious wake), Cure Blindness, Cure Disease (disease 0x22; weakness 0x2B with 0x2C and 0x1F; 0x32 with 0x39), Slow Poison (the poisoned: 1 HP at least, effect 0x16 with data 0xFF, the poison's harm 0x0F held 10 minutes), Neutralize Poison, Remove Curse (the curse effect, else the first cursed item comes off), Raise Dead (the dead or animated, Constitution left, not elves: 1 HP, a point of Constitution). Not in the engine yet (the spell stays in memory): Enlarge, Friends, Strength, Spiritual Hammer, Dispel Magic, Restoration, Fire Shield - they change ability scores, make weapons or need choices that come with combat. **Display**: each member's name (colour 11), their effects (colour 10: effects named after the first spell 1-56 that gives them, and the named ones in GAME.OVR - "Poisoned", "Regenerating" ...), " <No Spell Effects>", a page of rows 4-22, "Next Prev Exit". Effects run out as game time passes (walking 1 minute a step, searching 10, scripts' CLOCK, resting). v0.30.0: **Alter** ("Alter: Order Drop Speed Icon Pics Exit", START.EXE 0xB05C; words in GAME.OVR): Order - "Party Order: Select Exit" (tap a member), Select: "NAME has been selected" and "Party Order: Place Exit" - a tap on another line moves them there (the games: arrow keys), Place puts them down; Drop - "NAME will be gone", "Drop from party? Yes No": "NAME bids you farewell" (or "is dumped in a ditch" when they can't fight; gone, not saved), No: "Breathes A sigh of relief"; the last member: "quit TO DOS: Yes No" (Yes leaves the Play Test); Speed - "Game Speed = N (0=fastest 9=slowest)" (row 18), "Game Speed: Faster Slower Exit" (the area word 0x4BFC, saved with the game); Icon (combat icons) and Pics: to come. **Fix**: nothing lost, nothing happens; else the healers' (okay members') cure spells in memory and the cures they would memorize again (a day's slots of each cure's level) are rolled, a rest of 4 hours (6 for cures past 2nd level) + 15 minutes a spell level (the longest healer; shorter when the party lost less than the healers can heal - 27 a 1st-level healer, 34 / 78 past 2nd / at 5th level - divided by that ratio), then the rolled healing shared out in party order. Resting as Rest does (encounters, "Stop Resting?"); stopped or interrupted: no healing. Still to come: Scribe; magic resistance and the spells' workings in fights (M6). The games ask "Quit TO DOS" after a camp save; the Play Test doesn't (its Esc key leaves). |
| Party menu (the games' first screen): the outer frame, the party list (below), the menu a line each from row 12 (first letter colour 15 at column 2, the rest colour 10), "Choose a function " (colour 13) on the menu line; entries: a table of 12 x (string[40] + "on" byte) in START.EXE (Curse image 0xB133); Create / Add / Exit on from the start, Drop, Modify, View, Remove, Save, BEGIN with a party, Load without one, Train where the scripts offer training (0x7EA8 != 0), Human Change with training and a character who can change. Party list ("Name" at column 1 / 17, "AC  HP" at 33, row 2; a character a row from row 4; the selected name colour 15, others 11 (12 out of the fights, 14 the other side); AC right-aligned at 34 (a "-" before negative ones), HP at 38, colour 14 when below the most, else 10), strings in GAME.OVR (Curse 0x37E31 / 0x37E36; prompt 0x20111, "Load Which Game: " 0x1EE80) | `src/ui/play.*`, `engine/profile.*` | **v0.14.0** (checked against the program's code). Taps: a menu line picks it, a character selects them (also on the exploring screen). Load Saved Game -> "Load Which Game: A B ..." (the slots found); BEGIN Adventuring -> the saved game's script again (its first run) or, when none ran yet, the area's start script, as the games do. BEGIN needs a party (v0.20.0; before that the Play Test began without one). Still "not in the engine yet": Modify, Human Change (Save: v0.18.0, Create: v0.20.0). v0.27.0: **PROGRAM 0** (a script's party menu - the training halls) opens this menu in the game; BEGIN Adventuring goes back to the game screen and on with the script. **Train Character** (when 0x7EA8, the classes the hall trains, is set): "we only train conscious people" / "Training costs 1000 gp." / "We don't train that class here" / "Not Enough Experience" (on the menu line, the game's delay); else "NAME will become:" (row 4, column 4, the name in the party list's colour, the rest colour 10), "    a level 6 Paladin" (rows 5.., column 6; "and a level ..." after the first), "Do you wish to train? Yes No": "Congratulations...", 1000 gp paid, ONE class a session (of those with the experience and the hall's training: the one whose next level needs the most), its hit points rolled as in creation (engine/create: trainable, train_classes). A magic-user's new spell to learn: with the spells (logged). Words: GAME.OVR 0x24C2E-0x24CDB. |
| Add / Remove / Drop (Curse): Add: "Add from where? Curse Pool Hillsfar Exit"; Curse lists the save folder's .GUY files (422 bytes, not NPCs, not in the party) by name, "Add a character: Add Next Prev Exit" from row 2, "* " before those added; rules: 6 player characters (8 in all), "paladins do not join with evil scum", "too many rangers in party" (3), "NAME will tolerate no evil!"; ends when the party is full. Remove: saves the character as NAME.GUY (the name without spaces / punctuation, 8 letters) + .SWG / .FX, "Overwrite NAME? Yes No" when one exists; an NPC is dropped instead. Drop: "Drop NAME forever? " then "Are you sure? " (Yes No, No first): their files go, "You dump NAME out back." (or "NAME bids you farewell." when in the fights); No: "NAME breathes a sigh of relief." (words GAME.OVR 0x23595-0x23617, 0x1C506, 0x2255C-0x225A5, "Yes No" 0x32BF0) | `src/ui/play.*` | **v0.19.0**. Overwrite -> No cancels the Remove (the games ask for another file name - to come); Pool (Pool of Radiance characters) and Hillsfar: to come. |
| View Character (Curse; from the party menu and the exploring menu's View): the outer frame; name (1, 1, the party list's colours; "(NPC)" after it); sex, race, "Age n" (row 3, colour 15, a space between); alignment (row 4), class (row 5); STR ... CHA (rows 7-12, colour 10) with the full value at column 5 (6 below 10) and "(nn)" / "(00)" for exceptional strength at column 7; coins from jewellery down to copper, those held, from row 7 (name right-aligned to column 19, amount at 21); "Level" (1, 15) with the levels joined by "/" at 7 (a former class only below the current level), "Exp n" at 17; row 17 "AC" (value at 4), "THAC0" (9; 60 - to-hit bonus at 15), "Encumbrance" (22; at 34); row 18 "HP" (at 4, yellow when hurt), "Damage" (8; dice "1d2+6" at 15), "Movement" (25; doubled when slowed, halved when hasted, at 34); "Status" (1, 22) and the health word at 8; menu line: what the character can do. Names: tables in START.EXE (Curse image 0xB898 classes, 27-byte slots x 18; 0xBA7E races 10 x 8; 0xBACE alignments 17 x 9; 0xBB67 sexes 7 x 2; 0xBB75 coins 11 x 7; 0xBBC2 health 13 x 9); words in GAME.OVR (0x27094 "(NPC)" ... 0x276E8 "Movement", 0x27BA8 "Exit") | `src/ui/play.*`, `engine/party.*`, `engine/profile.*` | **v0.15.0** (layout read from the program's code). Weapon / armour lines (rows 20-21) need item names, and the menu offers only Exit until Items, Spells, Trade, Drop, Heal and Cure come. |
| Items (Curse): 63-byte records (characters' .SWG, ITEMn.DAX blocks = treasure / shop goods, MONnITM.DAX): type 0x2E, name words 0x2F-0x31 (word 3 first: "Long Sword", "+1", "Frost Brand"; 0 = none; hidden-word bits 0x35: 4 hides word 1, 2 word 2, 1 word 3 until identified), plus 0x32, plus vs saves 0x33, readied 0x34, cursed 0x36, weight 0x37, count 0x39 (printed first: "10 Arrows"), value 0x3A (gold), effects 0x3C-0x3E. Name words: 255 x 21-byte slots in START.EXE (Curse image 0xBC37 = word 1). An "s" goes on one word when there are 2+ (the games' rules; missiles take it on their name unless word 3 is 0x87 / 0xB1). ITEMS file: 2 bytes + 128 x 16 bytes per type: slot (0 weapon, 1 shield, 2 armour, 9 rings ...), hands, damage vs large, attacks, armour value (0x80 + AC points, absolute: plate 57 = AC 3; shield 0x81 = +1), damage vs man-sized, range, classes (& the character's class bits 0x12B), flags (1 arrows, 2 missile, 4 melee, 0x80 quarrels) | `src/engine/items.*` | **v0.17.0**: names checked against all of Curse's ITEM / MONnITM blocks ("Long Sword +3 Frost Brand", "Studded Leather Armor", "10 Arrows"). |
| Create New Character (Curse): "Pick Race" (dwarf, elf, gnome, half-elf, halfling, human - no half-orcs), "Pick Gender", "Pick Class" (the race's list, DS 0x3FFA: 14 bytes a race, count + classes), "Pick Alignment" (the class's list, DS 0x41DA: 10 bytes a class), each a list with "Select Exit" (heading colour 13); then the character screen and "Reroll stats? Yes No", "Character name: " (15 letters), "Save NAME? Yes No" -> NAME.GUY / .SWG / .FX in the save folder (Add Character to Party brings them in). The rules: defaults (base AC 50, THAC0 40, icon colours from DS 0x3EC3), the race's effects (con save, dwarf vs orcs / giants, gnome, elf sleep resistance, half-elf; paladin protection from evil, ranger vs giants), experience 25000 / 12500 / 8333 (one / two / three classes), age (DS 0x404E: 7 x (base, dice, sides) a race; multi-classes the dice's top), stats best of six 3d6+1, the age's effects (from the program's code: Str +1 -1 -2 -1, Int 0 +1 0 +1, Wis -1 +1 +1 +1, Dex 0 0 -2 -1, Con 0 -1 -1 -1 for each bracket past, DS 0x4124), the race / sex limits (DS 0x3F88), class minimums (DS 0x4174), Wis 13 for multi-class clerics, 18/xx strength for fighters, paladins, rangers; 300 platinum; hit points (better of two rolls, dice DS 0x822 / count 0x81A, the constitution adjustment, divided by the classes); first spells (clerics all of level 1, magic-users four); then training to the level the experience buys (DS 0x429B: 99 bytes a class - experience for levels 2-12, spell slots gained; race level limits). Class rules (THAC0 DS 0x3E3A, saves 0x45BE, thief skills 0x3EC0 / 0x3F20 / 0x3F33, spell slots, class flags 0x3EA2) checked against GOG's sample party | `engine/create.*`, `engine/classes.*`, `src/ui/play.*` | **v0.20.0**. To come: the combat icon editor (with combat), Modify Character, Train, Human Change (they use the same rules). |
| Rules (Curse): what the games keep up to date - encumbrance (items x count + coins), hands in use, AC (dex bonus + shield + rings / protection + the best of base AC and armour; magic armour drops ring bonuses), movement (armour over 150 / 399 weight: 9 / 6, +3 when 9 or less; carrying over the strength allowance + 0x200 / 0x300 / 0x400: 9 / 6 / 3), to-hit (THAC0 field; strength with melee weapons or none, dexterity with missiles, the weapon's plus, arrows' / quarrels' plus, +1 for elves with bows, short and long swords), damage (the weapon's man-sized dice, its bonus, strength, plus), attack level; strength groups 18/01-18/00; money: copper 1, silver 10, electrum 100, gold 200, platinum 1000 copper (gems / jewellery not counted); paying makes change as the games do; can't carry: 16 items or weight over the allowance + 1500 | `src/engine/rules.*` | **v0.17.0** (coab's rules, own code): recalculating the GOG sample party gives exactly their stored values. |
| The shop (Curse): a script sets 0x7F6C = 1 (0x7EE2 = a temple), the price factor 0x7F6D (0x10 normal, 1 = 1/16 ... 0x80 = x8), CLEARMONSTERS, TREASURE (coins + an ITEMn block of goods), COMBAT with no monsters loaded -> the shop: the picture stays, the party list, "Buy View Pool Appraise Exit" (+ Take, Share with coins on the counter). Buy: the goods last first, "Items: " + name (21) + price (9) from row 1, the chosen line highlighted; "Buy Next Prev Exit"; the selected character pays (the pool when they can't), "Not enough Money." / "Overloaded" on the menu line for the game's delay. A character's items (View -> Items): "NAME's Items", a bar at row 2, "Ready Item", " Yes  " / " No   " + name from row 5; Ready checks the class, the slot ("already using ..."), the hands ("Your hands are full!") and curses ("It's Cursed") | `src/ui/play.*` | **v0.17.0**: Buy, View, Items -> Ready, Exit (tap a line to choose it, then the menu word). v0.19.0: Pool (the player characters' coins on the counter) and Share (equal shares by coin kind from jewellery down, one more of the rest each, then whoever can still carry, 1500 + strength allowance; the rest stays); Exit with coins on the counter shares them out (the games' shopkeeper asks instead - to come). v0.25.0: the items menu as the games build it - Ready, Use (exploring / camp; magic items come with spells: "not in the engine yet"), Trade (player characters, not in shops: "Trade with Whom?" over the party list, Select Exit; too heavy / 16 items -> "Overloaded"), Drop ("Your X will be gone forever", "Drop It? Yes No"), Halve (fewer than 16 items; n -> n - n/2 and n/2, "Can't halve that"), Join (the same item's piles, 255 a pile), in shops Sell (half the value, piles of arrows / quarrels each, other piles count / 20; "I'll give you N gold pieces for your X", "Is It a Deal?", paid as N/5 platinum + the rest gold, "Overloaded.  Money will be put in pool." puts the platinum on the counter) and Id (200 gold from the character or the counter, "Not Enough Money"; "It looks like some sort of X" / "I can't tell anything new about your X"). Readied items: "Must be unreadied" (Trade / Drop / Sell), Use needs "Must be Readied". Words: GAME.OVR (profile item_words). v0.26.0: **Take** ("Select type of coin " over the kinds on the counter from row 2, "Select" (+ Exit, a convenience for touch); "How much X will you take? " typed on the keyboard; too heavy -> "Overloaded"); **Appraise** (NAME, "You have a fine collection of:" row 7, "3 Gems" / "1 piece of Jewelry" rows 9-10, "Appraise :   Gems  Jewelry Exit"; a gem's value by d100: 10 / 50 / 100 / 500 / 1000 / 5000 gp, a jewel's by d100 then a random amount; "The Gem is Valued at N gp." row 12, "You can : Sell Keep" (Sell alone when it can't be carried): Keep makes it an item (type 70, name word 0x65 gem / 0xD6 jewel), Sell gives N/5 platinum, too heavy -> the rest on the counter, "Overloaded.  Money will be put in Pool."; none -> "No Gems or Jewelry"); **leaving coins on the counter**: the shopkeeper's (priest's) words in the text area and Yes No: Yes stays, No leaves them behind (replaces v0.19.0's sharing out). **The temple** (a script sets 0x7EE2 = 1 then COMBAT with no monsters): the shop screen with "Heal View Pool Appraise Exit" (+ Take, Share); Heal: "NAME, how can we help you?" (row 1, colour 15), the ten cures from row 4 column 2, "Heal Exit"; a cure that does nothing for them: "NAME is not blind." etc. and "cast cure anyway: Yes No"; "<cure> will only cost N gold pieces." (rows 21-22, colour 10), "pay for cure Yes No" - the character pays, else the counter, else "Not enough money."; "NAME is cured." Cures (prices from the game's code): Cure Blindness 1000, Cure Disease 1000, Cure Light Wounds 100 (1d8), Cure Serious Wounds 350 (2d8+1), Cure Critical Wounds 600 (3d8+3), Heal 5000 (all but 1d4, and blindness, diseases, feeblemind), Neutralize Poison 1000, Raise Dead 5500 (1 HP; the Constitution / HP changes: to check), Remove Curse 3500 (the curse effect, else a cursed item comes off), Stone to Flesh 2000. The words are read from GAME.OVR when shown (profile shop_words). To come: magic items' effects, treasure after fights. |
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
  drag), Logs, Invert Colors, Swap Red/Blue (red, green and blue blocks
  named in them, to see whether a swap is needed), Rotate 180, Recalibrate
  Touch, Game Screen 1.5x (480x320). The bottom line: version, board,
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
   v0.14.0: the Play Test opens at the games' party menu; Load Saved Game
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
   v0.30.0: Alter (Order, Drop, Speed) and Fix.
   Next: Scribe, treasure after fights, magic items.
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
  the game next waits for a key, shows the entry: scaled (bilinear) to the
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
  numbers).

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

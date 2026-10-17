# CLAUDE.md - working rules for this repo

CYD BYOG Pool of Radiance Series Engine: runs SSI's Gold Box games (Pool of
Radiance, Curse of the Azure Bonds, Secret of the Silver Blades, Pools of
Darkness) on ESP32 "Cheap Yellow Display" boards from the player's own DOS game
files on SD ("bring your own game"). Design and roadmap: `docs/SPEC.md`.

Repo: `tomtombombadil/CYD-BYOG-PoolRadSeries`. Web flasher:
https://tomtombombadil.github.io/CYD-BYOG-PoolRadSeries/
Reference repo: `tomtombombadil/CYD-Classic-Games` (Tom's, MIT) - board files,
HAL, CI, flasher and UI rules come from there; copy from it freely.

## The developer
- Tom works on **Windows** with VS Code + PlatformIO. Never give Linux/bash
  commands in instructions for him; use the PlatformIO GUI or PowerShell.
- If a Python tool is ever needed on his side: always a venv, and
  `python -m pip install`, never bare `pip install`.
- He does not want to repeat instructions. Anything decided goes in this file,
  `docs/SPEC.md`, or `THIRD_PARTY_NOTICES.md`.
- **The task list** (Tom, 2026-10-12: "start keeping a list of tasks we
  need to do - both you and me"): `docs/TODO.md` - Tom's and Claude's open
  items. Add anything either of us must do (questions for Tom, checks
  Claude wants, follow-ups) the moment it comes up; tick it off with the
  version that did it; sync it to the Project (claude/TODO.md) with every
  push, like SPEC. Never ask Tom the same thing twice: it's on the list.
- **Game order** (Tom, 2026-10-17): the original series first -
  Secret, then Pools of Darkness - then the Savage Frontier (Gateway,
  Treasures), the Krynn games last (Dark Queen's TLB / GLB format is the
  only new storage; Darkness and Treasures are DAX with VGA pictures).
- The other games (survey 2026-10-12): the Project's
  claude/other_games_facts.md - 16-colour: Pool, Curse, Secret,
  Champions, Death Knights, Gateway; 256-colour VGA: Treasures, Darkness,
  Dark Queen, Unlimited Adventures (the last two a newer TLB / GLB
  engine).
- **Boards and games (Tom, 2026-10-12)**: the original series - Pool,
  Curse, Secret, Darkness - must be complete and playable on a basic
  (classic ESP32, no PSRAM) CYD. Darkness (and Treasures) run there in
  **their own EGA mode**: their setup offers EGA / Tandy / VGA, and in
  EGA the original turns the same VGA pictures into 16 dithered colours
  (checked in DOSBox: Darkness with POOL4.CFG's graphics line E) - the
  engine does what the original's EGA mode does (facts from the player's
  EGA.DRV / program, never copied). Unlimited Adventures: NOT on the
  classic boards. **Dark Queen** (Tom, 2026-10-12: "not completely wipe
  Dark Queen off the basic CYD builds just yet"): it has an EGA choice
  at install - try it there in its EGA mode when we get to it. **ESP32-S3
  and ESP32-C5 boards with PSRAM are added (v0.88.0)**: the 4.0" ST7796
  480x320 capacitive S3 board (env `cyd40s3_st7796_cap`; Tom's arrives the
  week of 2026-10-12 - he uses a capacitive stylus on it) and the 2.8"
  ST7789 resistive C5 board (`cyd28c5_st7789_res`; Tom has two). There
  the VGA games run in 256 colours. Board facts: the Project's
  claude/board_facts.md; SPEC 5d.
- **The repo can change under you** (Tom, 2026-10-12: he edits README.md
  on GitHub): `git fetch` and merge origin/main before every push, and
  never push over his edits (no force pushes; check README.md's changes
  before touching it).
- Tom plays with a Nintendo DS Lite stylus with firm presses - NOT a finger.
- Refer to boards by size + display driver + touch type, never vendor codes.

## Gemini's starting files (reviewed 2026-10-06)
- Kept unchanged in `docs/archive/gemini/`, NOT built. Review and reasons:
  `docs/gemini-review.md`. Don't revive that code; its formats are wrong.

## Decisions (Tom, 2026-10-06)
- Game names: PoolRad, Curse, Secret, Darkness. Target = the GOG releases
  (their DOS files and their journal PDFs).
- Controls: tap what the game shows (menu words, list lines, combat squares);
  keys only for what isn't on screen - one row of 8 under the game on
  320x240, a 3x3 pad + keys under it on 480x320 (SPEC section 4). Exploring
  keys include Side-step Left / Right (same checks as a step into that
  square). Pad = numeric keypad: 7 / 9 turn, 8 forward, 4 / 6 side-step,
  2 turn around (Tom). 320x240 row (Tom, 2026-10-07): Side-step Left,
  Turn Left, Forward, Turn Right, Side-step Right, Turn Around, Area, Esc.
- Play Test follows the real games, not test shortcuts: locked doors stop
  the party ("Locked." - Bash / Pick / Knock since v0.60.0, door_facts.md), the 3D
  view comes back when a script redraws it (CALL 2E10) or the party moves.
- Copy protection: skipped - never asked, treated as answered.
- Later: Krynn, Savage Frontier and FRUA in this repo, after the four games.
  Keep game-specific code behind a per-game profile so they fit.
- 480x320: the game screen stays 1:1 (never scaled for play) with a Gold Box
  Companion panel (auto-map, party HP, spells) to its right; controls below.
- Build order: Curse of the Azure Bonds first, then Pool of Radiance, Silver
  Blades, Pools of Darkness.
- Journal (Tom, 2026-10-09; replaces the browser converter of v0.10.0,
  which is removed): the BOARD makes the entry pictures from the player's
  own GOG journal PDF during the card scan, using per-PDF rectangle tables
  in the firmware (page numbers and positions only). Entries are kept at
  the scan's resolution and scaled as they are shown (both screen sizes,
  zoom later). The game shows entry N when it mentions it. Fallback (later):
  a PDF viewer to page through / zoom the book, for other editions or an
  unknown PDF. Copyright rule: nothing from the journal (text, pictures) is
  ever distributed - files made on the player's device from their copy are
  fine (SPEC section 7). Built v0.12.0: `engine/pdf.*` (page pictures),
  `engine/jpeg.*` + `third_party/tjpgd.*` (TJpgDec), `engine/journal.*`
  (JOURNAL.DAT, mention finder), `ui/pdfview.*` (the book view fallback,
  v0.13.0), `journal_tables.cpp` from
  `tools/journal/make_table.py` (run on the player's PDF in scratch space;
  check every entry by eye; renders stay in scratch).
  SNEG (Tom, 2026-10-10, v0.70.0): its PDFs are typeset text the board
  can't take apart, so the journal comes from the Gold Box Companion's
  `GBC/Games/<nn. Game>/Game.dat` (`<journal N>` text) + `JE_NNN.jpg`
  (`journal::make_gbc`); text entries are laid out by the viewer (Zoom =
  Small / Medium / Large text). No book view for those PDFs.
- Settings / logs (Tom, 2026-10-09; SPEC 5c): the card scan ends at its
  scrolling log until Continue; Settings has a brightness slider, Swap
  Red/Blue with red / green / blue blocks, a Logs key (Card Scan, Restarts,
  Errors), pages with arrows bottom right when keys don't fit. Logs come
  off the board on the card (`/GOLDBOX/_CYD/*.TXT`) or by **Send Logs**
  (Tom, 2026-10-12, v0.86.0, as CYD-Classic-Games: a QR code a phone opens
  as `web/l/index.html`, which emails them to
  cyd.classic.games.logs@gmail.com; `ui/sendlog.*`, `engine/log_pack.*`,
  Nayuki qrcodegen in `engine/third_party/`; SPEC 5c). The Game menu has
  a Logs tab (the canvas parks to the card while it's open). NO WiFi / email (Tom,
  v0.22.0: tried in v0.21.0, ~23 KB of RAM for good - every KB goes to the
  games). Don't link WiFi, BT or other networking libraries.
- Engine Menu (Tom, 2026-10-09): the Play Test's Area key became a Menu
  key (both screen sizes) opening the engine's own tabbed screen: Journal
  (entries met so far) and Journal PDF (the book) now; further engine
  features get tabs there. Area stays on the game's menu line, and a tap
  on the 3D view toggles 3D / Area. The key is labelled **Game** (Tom,
  2026-10-10) so it isn't confused with the cursor keys.
- Journal entries (Tom, 2026-10-10): when the game mentions one, its screen
  stays up to be read; the next tap (or key) opens the entry, and after the
  viewer closes the game still waits for its tap. Never jump straight in.
- Cursor keys (Tom, 2026-10-10; SPEC section 4): the games' menus are small
  for a stylus, so 480x320 has a cursor pad (up / left / Select / right /
  down) beside a 3 x 2 movement pad (Turn Around between the side-steps),
  and Game / Look under the Companion map - NO Esc key in the Play Test on
  either size (Tom, 2026-10-12: a stray tap left the game; Exit Game is
  in the Game menu only) - (no big "Map" heading; the
  area / script / map lines small). Up / Down: a list's highlight (Left /
  Right on the menu line; Up / Down too when there's no list; the party
  list beside a menu line is a list - `play::party_nav`, Tom); Select = a
  tap on the highlighted thing (`play::nav`, `play::nav_point`). New screens
  with lists or menus must take part. A turned page (Next / Prev): Up /
  Down start on the page shown - Down its first line, Up its last (Tom,
  v0.85.4: the spell lists jumped back to the first page). 320x240 (Tom, v0.51.0): automatic
  plus a switch - one row of 9: movement arrows + Menu Keys / Game / Esc
  while the party can move (`play::walking()`), else Left / Up / Select /
  Down / Right + Move Keys / Game / Esc; the switch lasts until walking
  changes. Cursor keys are small text (`ui::key_small`) on both sizes;
  movement keys keep their drawn arrows (never "<<" / "^" characters).
  v0.52.0 (Tom): cursor row order Up, Down, Select (2 wide), Left, Right,
  Move Keys, Game, then Map; Esc went into the Game menu (tab bar's right,
  320x240 only); Map (map over the game screen until the next tap) is
  Claude's pick for the freed slot, confirmed by Tom (2026-10-10).
  v0.66.1 (Tom): every key in the 320x240 row uses the small text (Game,
  Map, Esc too); Select gives 8 px so Up / Down / Left / Right are each
  2 px wider ("Right" fits) - widths change in multiples of 4, and
  Select's text must still fit.
  Tom, 2026-10-10: Map then Game (Game last on the right); the Game menu's
  Esc became **Exit Game** (out of the Play Test; both sizes). A fight's
  own keys while a fighter moves or aims by hand (`play::fight_pad`): the
  8 directions + Select (Select = end the move / Target) - 320x240 one row
  in keypad order 7 8 9 4 Select 6 1 2 3 + Game (arrows 23 px, Select and
  Game wide enough for their words), 480x320 a 3 x 3 pad in the movement
  pad's place. The map (Companion / Map key): black behind, walls 1 px,
  doors 3 px - v0.84.0 (Tom): a pixel short at each end so the white wall
  line shows there; the party arrow bright green (`style::kParty`);
  secret doors an S on the wall line (no mark in the data: a passable
  side of a wall type mostly solid on that map - SPEC 12); the movement
  keys move the party while the 320x240 Map is up (the map follows).
- Add Character -> Pool (v0.56.0): lists Pool of Radiance characters from
  Curse's save folder (the original) AND the player's Pool of Radiance
  folder on the card (confirmed by Tom, 2026-10-10). The demo stops on
  a tap since v0.64.0 (Tom; the original's can't be stopped).
  Add Character -> Curse (Tom, v0.58.0): also the members of saved games
  ("NAME from saved game A"), beyond the original's .GUY-only list.
  Add Character -> Hillsfar: NOT supported (Tom, 2026-10-10 - Hillsfar
  isn't one of the engine's games); the menu word stays, with a message.
- Tom, 2026-10-10: the Map key (320x240) stays; the demo stops on a tap
  (`CYD_DEMO_TAP_STOPS`); importing characters reads the earlier game's
  own folder on the card directly - the standard for every game (Secret
  from Curse, Darkness from Secret). Every difference from the originals
  goes in SPEC section 12 (keep it up to date).
- Journal viewers (Tom, 2026-10-10, v0.64.0): no title bar; one slim key
  row (about 57% of a normal key) Back | Zoom | Prev | page numbers |
  Next. The page numbers are just "2 of 3" (Tom, v0.66.1: no "Entry N" -
  it's in the picture - and no "Page").
- Stats (v0.57.0): bytes 0x10 + 2i = a stat's own value, 0x11 + 2i = the
  one in use (18/xx the other way round: 0x1D own, 0x1C in use);
  `rules::stats` works the in-use ones out on every recalculation - never
  write the in-use bytes as if they were the character's own.
- Magic Missile (Tom, 2026-10-11): (caster level + 1) / 2 missiles of
  1d4 + 1, always hitting - the AD&D rule, every game (Pool's code was read
  as level / 2; not followed). All on one target as the originals (Tom,
  2026-10-11: keep it; separate targets maybe later, as an option).
- Settings help (Tom, v0.84.0): every switch on the Settings screen has
  a circled "?" at its right (`ui::key_help`) opening
  what it does (`comfort_help_text`; Game Font's shows each font); a new
  switch needs its help text too. Key renamed: "Tap to Stop Demo".
- Engine comforts (Tom, 2026-10-10): anything the original games didn't do,
  added for the small screens, goes behind a switch in
  `src/app/features.h` (documented there and in SPEC section 11) so forks
  for bigger screens can turn it off. First: `CYD_BIG_ICON_PREVIEW`, the
  icon editor's big preview (480x320 always; 320x240 via Settings ->
  Large Icon).
  `CYD_ICON_GALLERY` (Tom, 2026-10-11, v0.71.0): the icon editor opens on
  a gallery of ready-made icons / heads / weapons / colour schemes (the
  player's own parts; the combinations are `engine/icon_looks.*`, ours);
  Edit = the original editor (its Exit returns to the gallery), Done =
  "Is this icon ok?". v0.71.1 (Tom): icons at twice their size, 4 x 3 a
  page, Prev / Next boxes beside them.
- Journal book memory (v0.52.0): while the book is open the game canvas
  waits on the card (`frame::park` / `unpark`, `_CYD/CANVAS.TMP`); a
  memory failure must never be reported as "no scanned picture".
- Card scan (Tom, 2026-10-09): scan ONCE; boot reads the scan log and
  loads the library from it. Rescan Card is the only rescan - never
  check the card for changes on its own. Everything the board makes from
  the player's files goes in `/GOLDBOX/_CYD/` (per game a sub-folder):
  the scan log (also readable text: games, PDFs, icons found, problems),
  icons decoded once at the screen's size, the journal entries.
  **The library's pictures (icons, box art) are made by the scan and
  saved; the library page only draws those files - never decoding or
  resizing on the fly** (Tom, 2026-10-16). A start whose card lacks this
  screen's pictures (scanned by an older version, or in a board of
  another size) runs the scan's picture step for the missing ones first
  ("Preparing Pictures", lines added to SCAN.TXT; `make_missing_pictures`,
  `picture_ready`); a page without its file shows the name without it.
  The scan shows a scrolling list of what it does ("Searching for Gold Box
  games...", "Found Curse of the Azure Bonds", "Processing Pools of
  Darkness Adventurer's Journal...") - it can take minutes.

## Bring your own game - hard rules
- The repo and firmware contain nothing from the games: no DAX files, no
  extracted pictures, text, journal entries or code. Tests build their own
  synthetic files. Screenshots of real game graphics stay out of the repo.
- Game files are read from `/GOLDBOX/<folder>/` on the SD card, never copied
  into flash.
- Tom's GOG game files are in this Claude Project's files: one zip per game
  (game files + GOG's goggame-* files incl. the icon .ico/.dll, no PDFs -
  2026-10-07) plus each journal as
  "<SHORT> - ... Journal.pdf". Fetch zips with the Projects tool
  (`project_read` -> local file), unzip to the session's scratch space (e.g.
  /tmp/claude-0/games/<game>), check with `tools/dax_tool/dax_inspect`.
  project_read refuses files over ~20 MB, and gives only the TEXT of a PDF
  (Curse's comes back empty) - making a journal table needs the PDF's page
  images, so journals must be uploaded inside zips to get their bytes.
- Tom may attach his own game files to a session for testing. Keep them in
  the session's scratch space only (never under the repo; .gitignore blocks
  *.DAX / *.pdf as a backstop), use them to check decoders, and never
  commit their bytes, pictures or text - tests stay synthetic.

## Licensing
MIT. Only copy code from MIT/BSD/Apache/zlib/public-domain sources and keep
their headers; update `THIRD_PARTY_NOTICES.md` when anything is brought in.
- **coab** (C# Curse of the Azure Bonds reimplementation, Simeon Pilgrim) has
  NO license: reading it to learn formats and rules is fine, copying or
  translating its code is not. Write our own code from the facts.
- GPL projects are reference only.

## Architecture
- One PlatformIO env per board, each sets exactly one `CYD_BOARD_*` flag;
  `src/boards/board_select.h` is the only file that tests them. Env options
  `custom_firmware_name` (`TTB-CYD-PRS_<size>in_<DRIVER>_<touch>`, plus
  `_ESP32-S3` / `_ESP32-C5` for the newer chips),
  `custom_board_title`, `custom_board_hint`, `custom_board_tested`,
  `custom_chip_family` (the flasher's chip, ESP32 when not given) drive CI,
  releases and the flasher - platformio.ini is the single source of truth.
- Newer chips (v0.88.0): the card by SD/MMC 4-bit (S3) or on the display's
  SPI bus (C5, Arduino's `SPI`); sound by I2S to the S3's ES8311 codec
  (`hal/es8311.*`, set up over the touch's I2C through LovyanGFX's i2c)
  or I2S PDM on the C5's GPIO 26 - `hal/audio.cpp` has the three outputs
  (BOARD_AUDIO_DAC / _ES8311 / _PDM). PSRAM: Arduino's malloc puts blocks
  over 4 KB there by itself; the memory figures then include it. The
  display object is made in setup() (the S3 panics on floating point in
  static constructors). The C5's card shares the display's SPI bus: never
  read the card inside a startWrite / endWrite - drawing that reads the
  card as it goes uses `ui::card_draw_begin / card_draw_end` (Tom's C5s
  hung at Home's first icon, v0.94.1). Their serial log is on native USB (CDC on boot).
  S3 rules (v0.103.1, Tom's S3 crashed at Play): the codec is set up
  from the main task only (`audio.cpp` codec_setup - its I2C is the
  touch's, and LovyanGFX's i2c isn't safe from two tasks; the sound
  task never touches I2C; I2S boards' sound task has 5 KB of stack);
  the card's SD/MMC DMA never gets a PSRAM buffer - `plat::File` reads /
  writes into PSRAM go through a 2 KB internal bounce
  (`platform_esp32.cpp`), the engine's own fs::File reads use internal
  buffers (`heap_caps_malloc(..., MALLOC_CAP_INTERNAL)`).
  S3 touch (v0.103.4, Tom: heavy taps needed, inaccurate): the FT6336 is
  polled over I2C every time (`pin_int = -1` - with INT given, LovyanGFX
  reports no touch whenever INT isn't low at that moment) and reset by
  main.cpp as the maker does (`BOARD_PIN_TOUCH_RST`: 10 ms low, then
  ~300 ms to start - LovyanGFX's own reset is 1 ms). v0.103.5 (Tom:
  "improved, but still pretty bad"): a capacitive panel is read by a
  task of its own every 10 ms (ui.cpp `cap::sampler`, core 0) that
  queues Down / Move / Up - the loop only looked between its work, so
  quick light taps came and went unseen - and poll_tap plays the queue
  (press point, drags) as before; the I2C it shares with the codec is
  guarded by `hal/i2c_lock.*` (codec set-up, touch calibration hold it).
  Recalibrate Touch is on every board's Settings now; a capacitive
  board uses a calibration only when the player made one. v0.103.6
  (Tom: "delayed"): the FT6336 kept in its active mode (register 0x86 =
  0, written by the touch task until it takes) - by itself it drops to a
  25-scans-a-second monitor mode, I2C closed, after a moment untouched.
  S3 sound (v0.103.6, Tom: the step sound silent, Hit cut short, Pool
  seemingly silent): the FM8002E amplifier takes 100 ms to wake (its
  datasheet), so the codec's output, its clocks and the amplifier stay
  on from the first sound (`kKeep`; `audio_prepare()` at Play's start
  wakes them before the first sound; the I2S sends silence by itself).
  The ESP32-32E boards (3.2" / 3.5" / 4.0", amplifier enable GPIO 4) the
  same (v0.104.1, Tom's 3.5": Step and Hit "a microsecond", the long
  sounds fine): their amplifier also takes ~100 ms to wake, so it is
  switched on by `audio_prepare()` at Play's start and stays on (the DAC
  pin held at 0 V between sounds, not floating); only the DAC and the
  sound task come and go with the sounds.
  The display's set-up matches the maker's (TFT_eSPI's ST7796 sequence:
  same gamma, VCOM, power) - for colour questions look elsewhere.
- **Crash lines** (v0.103.1): a panic leaves a core dump in the flash's
  coredump partition; the next boot puts its summary in the Restarts log
  (`hal/crashinfo.*`: task, PC, cause, backtrace - RISC-V: ra, sp and
  the stack's code addresses) and erases it. Decode with the CI artifact
  `elf-<env>` of that build (the version line's hash) and
  `xtensa-esp32s3-elf-addr2line -pfiaC -e <elf> <addresses>` (the
  toolchain's xtensa-esp32- / riscv32-esp- for the other chips).
- `custom_board_tested = no` on a board until Tom has run THIS firmware on
  it. Tested: the five classic boards (Tom, 2026-10-09, v0.20.0); the 2.8"
  ST7789 resistive C5 (Tom, 2026-10-12, v0.94.1: both of his - the yellow
  and the "colorful" one - Home, scan and a Secret Play Test); the 4.0"
  ST7796 capacitive S3 (Tom, 2026-10-16, v0.103.3: Pool, Curse, Secret
  play); a new board starts at `no`.
- 2.8" ESP32-2432S028: touch is bit-banged (`touch_xpt2046_soft.hpp`, an
  `lgfx::ITouch`) so the SD card can have VSPI. Keep it that way - the engine
  needs the card.
- Landscape (rotation 1; Rotate 180 = 3). Layout always from the panel size.
- No LVGL: the games draw their own screens. `src/engine/` is plain C++ (no
  Arduino), host-tested in `tools/host_tests/`. `src/ui/frame.*` holds the one
  320x200 canvas (palette indexes) and presents it 1:1 or 1.5x (480x320).
  Since v0.87.0 the canvas is **4 bits a pixel** (32,000 bytes; Tom
  2026-10-12 "focus on memory": EGA's 16 colours) - pixels only through
  `pic::Canvas::get / set / get_row / put_row / fill` (never `px[...]`);
  `frame::set_depth(8)` only for the Resource Test's 256-colour VGA
  pictures (back to 4 on any other screen). The playsim runs 4-bit too
  (CANVAS8=1: a byte a pixel - its snapshots must be identical).
  **The VGA games need the 8-bit canvas back** (Tom, 2026-10-12): Pools of
  Darkness (checked: its GOG PIC / BIGPIC / CPIC blocks are 256-colour
  VGA pictures, 479 of them), The Dark Queen of Krynn, Unlimited
  Adventures (Treasures of the Savage Frontier: check its files when we
  get there). The depth will be the game profile's choice
  (`frame::set_depth` already does it); for those games the memory
  budget is 32 KB tighter - plan their memory with that.
  `src/ui/ui.*` = keys, header, touch filter for the engine's own screens.
- Facts that live in a game's program (START.EXE / GAME.EXE, EXEPACK-packed;
  e.g. the screen frame's tile tables) are read from the player's copy at
  run time (`engine/exepack.*` unpacks just the bytes asked for), found via a
  per-release entry in `engine/profile.*` (file size + unpacked size, data
  segment base, table addresses). Never copy those tables into the repo.
- Game folders (Tom, 2026-10-09): the GOG install folders copied as they
  are - `/GOLDBOX/Curse of the Azure Bonds` etc. Docs, screens and examples
  use those names, not short DOS names (the scanner still recognises short
  names like CURSE: `engine/games.*`). Dark Queen / Unlimited Adventures are
  .TLB/.GLB "newer format" games, listed, not readable. The Steam / SNEG
  releases (Tom, 2026-10-10) are found too: each game's SNEG folder copied
  into /GOLDBOX as it is (`CURSE`, `POOLRAD`, `SECRET`, `Pools of Darkness`
  ...), the DAX files two down (`<folder>/GAME/<SHORT>`), the journal in
  `<folder>/Documentation/`, the icon `game.ico`; folder listings in the
  Project ("Steam SNEG Directory Listing*.txt"). Their .CFG save path
  includes the game folder's own name (`C:\CURSE\SAVE\` = `GAME/CURSE/SAVE`;
  `savegame::drop_own_folder`, v0.71.1).
- Chooser (Tom, 2026-10-10, v0.53.0): a game's page is four big keys -
  Resource Test (the DAX file / block viewer, behind it now), Screen Test,
  Walk Test, Play Test. The Play Test starts with the title sequence and
  the version line's Play / Demo, then the party menu (as the games do).
  v0.72.0 (Tom, 2026-10-11): the page is **Play** + **Settings** only;
  Settings = the engine comforts' on/off switches + Game Sound, one set
  for ALL games (settings.h `comforts_off`; per-game only if a need shows
  up), As Original / All On, and **Dev Tools** at the bottom (the three
  tests). New comforts get a features.h switch AND a Settings key.
  **One Settings screen everywhere** (Tom, 2026-10-12, v0.91.0: "the
  chooser's settings and the game settings ... should be the same (except
  the game specific settings)"): Home, a game's page and the in-game Game
  menu's **Settings** tab (was Options) show the same keys - the board's
  (Brightness, Volume, Game Sound, Invert, Swap Red/Blue, Rotate 180,
  Recalibrate Touch, Asset Viewer 1.5x on 480x320 outside the game,
  Logs, the version / board / memory lines) then the comforts; a game's
  own keys (Item Fixes, for a game with fixes) only with a game in hand;
  bottom: As Original, + Dev Tools (game page) or Back to Game (in game);
  2 x 4 a page, arrows bottom right (viewer.cpp `set_entries`, SetFrom).
- Clear font (Tom, 2026-10-11, v0.73.0): the engine's OWN 8x8 font
  (`tools/fonts/clear8.txt` -> `make_font.py` -> `engine/font_clear.cpp`),
  bold upright capitals + real lower case, a comfort (`CYD_CLEAR_FONT`,
  Settings key) - OFF by default (Tom: the original font stays the
  default), switchable mid-game from the Game menu's Settings tab (the
  text on screen changes with it). Third-party fonts (the Gold Box Companion's
  have no license and are the game's own glyphs retouched) are only looked
  at, never copied; edit the .txt art and regenerate. v0.74.0 (Tom): the
  key is **Game Font** - Original / Clear / Darkness (the player's own
  Pools of Darkness 8X8D0.DAX, only when that game is on the card);
  v0.84.0 (Tom) adds **Clear Caps** (Clear's capitals, no lower case).
- Item Fixes (Tom, 2026-10-12, v0.90.0; `CYD_ITEM_FIXES`): items the
  originals got wrong made as AD&D describes them, behind a switch (on by
  default) - Pool's Flame Tongue becomes Curse's / AD&D's sword
  (`items::fix_item`, the facts in `profile.items.flame`); the save files
  always keep the original's bytes. A new fix of an original mistake goes
  here too (Tom wants the originals' behaviour available).
  The Settings switches page past 8 (arrows bottom right).
- Icon Portrait (Tom, v0.84.0; `CYD_ICON_PORTRAIT`): View Character shows
  the combat icon x3 in a game-tile frame where Pool draws portraits
  (Curse: the 3D view's frame tiles, cols 26-38 rows 1-13); where the game
  has a portrait (Pool) it comes first, a tap swaps; Pool's own portrait
  on View is the original's (from the party menu).
- Gold Box Companion files (Tom, 2026-10-11): its author's work - read
  from the player's copy on the card, NEVER shipped or copied into the
  repo (no license). Its combat icons: `/GOLDBOX/GBC/DAX-Icons-EGA`
  (the GOG Companion's GBC folder copied to /GOLDBOX/GBC) as more icon
  gallery pages, picks by name in `_CYD/<folder>/ICONS.TXT`
  (`engine/gbc_icons.*`). Its Resources docs (formats, item lists,
  monster manuals) may be read for facts, like coab.
- Library (Tom, 2026-10-07): title bar shows the firmware version (to see
  which build is flashed); ONE game a page, no key look, the whole space
  between header and bottom keys - its GOG icon as big as fits (scaled from
  the 256 px PNG: ~154 px on 320x240, ~194 px on 480x320), the title and
  info lines wrapped inside the space beside it; < > page through games;
  (v0.103.0: Rescan / Settings in the header, below); the scan shows its
  scrolling list (Card scan above). Icons come from the
  player's goggame-<id>.ico / .dll on the card (`engine/icon.*`), never from
  the repo.
  **Box art** (Tom, 2026-10-12, v0.102.0): the Steam / SNEG collection's
  `Launcher` folder copied into /GOLDBOX (beside the game folders; not
  listed as a game) has each game's box art in
  `Launcher/Sprites/Backgrounds/Back-NNN.jpg` (`games::box_art_file`, Tom's
  list). When it's there the library page shows it in place of the icon,
  filling the whole card (centred, the overflow cut off) with the game's
  name centred over it (outlined). Same rules as the icons: the scan makes
  it once at the page's size (`_CYD/<folder>/BOX<w>x<h>.BIN`, RGB565 rows),
  the page draws that file; a board of another size makes its own at
  the start (above) - never on the page. The JPEGs are 2016 px progressive: `engine/jpeg_thumb.*`
  reads them at 1/4 size from their first scans only (our own decoder),
  `jthumb::Fit` scales.
  **The page since v0.103.0** (Tom, 2026-10-12, from mockups): the art the
  full screen width, top-justified (`ART<w>x<h>.BIN`), under Home's own
  header - 36 px on 320x240 so the art (320 x 204) fills the rest, 40 on
  480x320 (the art's bottom cut) - with **Rescan** (was "Rescan Card")
  and **Settings** as small keys right-justified in it; no bottom keys.
  Invisible tap zones: left third = the game before, right third = the
  next one, ROUND (Pool's left goes to the last game, the last's right to
  the first), middle third = open the game. Faint hollow triangles at
  the sides as the clue (`hints_blend`: a 1 px cream line over a fainter
  dark one, blended into the art's rows) - not arrows, not keys; the
  attention goes to the art and the name. Icon pages (no box art) keep
  the icon + lines inside the same strips, triangles on black.
- Play Test (`src/game/play.*`, v0.8.0): the script machine is
  `engine/ecl_vm.*` (plain C++, host-tested with synthetic scripts); the
  front end only shows what it asks for (Wait: Print, Menu, ListMenu,
  Number, String, Pause) and answers. Things that need a party are logged
  and passed over until M5/M6 - never faked. Since v0.14.0 it opens at the
  games' party menu (`engine/party.*`, `engine/savegame.*`): Load Saved
  Game reads the player's saves (GOG ships `SAVE/SAVGAMA.DAT` with a sample
  party); since v0.20.0 Create New Character (`engine/create.*`,
  `engine/classes.*`: rule tables read from the program) and BEGIN needs a
  party, as in the games. While text or a one-choice
  menu waits, any key goes on (like "press a key").
  The game's questions (INPUT NUMBER / STRING) use an on-screen keyboard
  in the viewer (`play::input()` / `play::input_key()`); event pictures
  animate only while a menu waits (as in the games).
- Combat (v0.32.0): `engine/combat.*` (battlefield, placement, rounds,
  attacks, the computer's turns, experience; host-tested) and
  `src/game/play_fight.inc` (the fight's screen and menus, the treasure step -
  included by play.cpp inside its anonymous namespace, not compiled on its
  own). Facts from coab: the Project's claude/combat_setup_facts.md and
  claude/combat_rules_facts.md, claude/combat_fx_facts.md (pictures in
  flight, magic hits, the computer's weapon; v0.41.0),
  claude/monster_fx_facts.md (monster special abilities, `combat::MonFx`;
  v0.61.0), claude/curse_finish_facts.md (the game won, the bolt's path,
  coughing, Detect, stat items, file names, aim-5 picking; v0.65.0) - read
  them before changing combat. The
  playsim runs a fight with `RUNAT=<addr> FIGHT=1` (Quick for everyone;
  `FINDMON=1` lists the loaded script's LOAD MONSTER addresses - the GOG
  save A: 8AC2, 947B; `MONSWAP=area:id[:copies]` fights another monster
  there, e.g. 1:53 the black dragon, 4:40 the beholder; `FORCESPELL` /
  `FORCEWEAPON` / `FXSHOTS` to try spells, shots and their pictures).
- `tools/playsim/` (Claude's side, Linux): runs the Play Test on the PC
  against the game files in scratch space (the PC platform layer,
  `tools/host`); use it
  to check script behaviour and take canvas snapshots before pushing.
  Its card is like the board's: `/GOLDBOX/...` paths map to the game
  folder's parent, and `library::path_of` adds `/GOLDBOX` as on the board
  (v0.21.0: a doubled `/GOLDBOX` in the save paths went unseen before).
  Snapshots of real game screens stay in scratch, never in the repo.
  v0.85.0: the shim's card has the board's 4 file slots (a fifth open
  fails, "[fs] no file slot left" on stderr); LCD=1 snapshots show only
  the rows the game marked changed (what the board's screen gets);
  FIGHTCHECK, FIGHTOPS pad steps (>d, ., !x,y), TELE0, PEEK, DISASM.
  `tools/playsim/sweep.cpp` (v0.68.1) is the game test: modes spells,
  campspells, monsters, items, classes, events (every script block,
  every square), saveload (round trip) and monkey (random taps / keys /
  moves with invariant checks; MAREA / MBLOCK start it in a block). Build
  it with -fsanitize=address,undefined; give each run its own copy of the
  game folder (runs save over the slots). Mode `testchars` (2026-10-12,
  Tom: characters to test items with) writes ITEMTEST1, 2 ... (files
  ITEMTST<n>) with every magic item of the game, each with a class that
  may use it; Pool's save-only items come from a scratch file (EXTRA=),
  never from the repo; the characters go to Tom as a zip, never committed.
- **DOSBox here** (2026-10-12): the ORIGINAL games run on Claude's side -
  `apt-get install dosbox xdotool x11-apps imagemagick`, a virtual display
  (`Xvfb :5`), `DISPLAY=:5 SDL_AUDIODRIVER=dummy dosbox -conf <conf>`
  (autoexec: mount the game folder copy, start), keys with `xdotool`,
  screenshots with `xwd -root | convert` - for questions only the
  original can answer (what it shows / does somewhere). The DOS files
  from the Project's zips are all it needs (GOG's Linux builds are the
  same DOS games in DOSBox). Screenshots stay in scratch. Don't kill
  processes with `pkill -f` patterns that match the shell's own command.
- `tools/qemu/` (Tom, 2026-10-11: "if worth it"): the REAL firmware in
  Espressif's QEMU (qemu-system-xtensa, esp-develop-9.2.2; downloads from
  GitHub work here) - `pio run -c tools/qemu/qemu.ini -e qemu28` (the 2.8"
  ILI9341 build + -D CYD_QEMU: card over SD/MMC since QEMU has no
  SD-over-SPI, no touch calibration, taps from the card's
  /GOLDBOX/_QEMU/TAPS.TXT, heap printed), `run_qemu.py` makes the card
  image and runs it. For the board's own limits (heap, largest block,
  stack) the playsim can't show. No screen output (taps need known
  coordinates). Board builds stay byte-for-byte (`#if CYD_QEMU` only;
  the SD_MMC library lives in tools/qemu/lib so it never links there).
  The emulator build has no sound, no LittleFS (QEMU panics on internal
  flash writes) and prints every key's centre ("[qemu] key 'Play' at
  81,134") and each screen with its heap; taps are counted in polls
  (QEMU's polls are slow). It panics (cache error) now and then at boot
  - run_qemu.py retries boots; since 2026-10-12 the Play Test opens in it
  (Pool: Home, the game's page, Play - free 96.9 KB, largest block 80 KB
  at the title; the same before and after the platform layer).
  **The S3 in QEMU** (v0.103.1): `-e qemus3` (the S3 board's firmware,
  -D CYD_QEMU_FLASHCARD) and `run_qemu.py --s3`: QEMU's ESP32-S3 with
  8 MB octal PSRAM, the card a READ-ONLY FAT image in the flash's
  "card" partition (`tools/qemu/qemu_s3.csv`; QEMU's S3 has no SD/MMC,
  I2C or I2S - no touch, codec or saving there), DIO flash (QEMU's S3
  reads QIO flash 2 bytes late outside the cache). TAPS.TXT's `panic`
  crashes on purpose (the crash line's check). Pool's Play runs there.
- Game testing (Tom, 2026-10-10): "test the game" means BEHAVIOUR, not
  just crashes - every spell, item, monster ability, class / level,
  encounter and save field must do what the original does (a +1 long
  sword is +1 to hit AND +1 damage; Fireball does its dice, area and
  save; encounters fire where and when the script says; levels give the
  right HP, THAC0, saves, spells). Expected values come from the rules
  (AD&D tables the games use), the original program's own tables and the
  coab facts - never from our own code. The expectations live in the
  Project (claude/behaviour_facts.md), the checks in `sweep.cpp`'s
  `behave` mode; results reports stay out of the repo.
- Pool of Radiance (Tom, 2026-10-11: implement it next; SPEC 6b): facts
  in the Project's claude/poolrad_facts.md + claude/poolrad_screen_facts.md
  (also GBC/Resources docs - facts only). Differences from Curse go in
  `profile.variant` (zero = Curse); Pool's script addresses are moved to
  Curse's map by `ecl::vm_address` at decode time. `variant.unfinished`
  kept Play off the game page until Pool played: off since v0.85.0
  (Tom: playable 2026-10-11) - the next game starts with it on. Its GOG
  files: scratch /tmp/claude-0/games2/poolrad/...; the playsim takes
  GAME=1 for Pool. Journal table: `make_table.py poolrad` (tesseract
  finds its text headings). Pool's race checks (Podol Plaza patrols, the
  Temple of Bane's trap doors) are corrected, not copied (Tom, v0.85.1:
  its scripts get the race; SPEC 12). Pool's behaviour checks: sweep's
  `behave pool` (-DBEHAVE_POOL -DBEHAVE_SPELLS, GAME=1; ONLY= values, con,
  creation, train, modify, money, spells, fights), oracle
  /tmp/claude-0/bt/oracle_pool.txt from scratch `pool_oracle.py` (Pool's
  own tables + the facts' constants; never in the repo). The playsim's
  NEWGAME=1 starts a new game (the tour) with save A's party.
  Pool expectations: the Project's claude/poolrad_behaviour_facts.md.
  Pool's items and stat effects (item codes, item spells 0x17 higher,
  Strength in the record, the Manual): claude/poolrad_items_facts.md
  (v0.89.0); `items::passive` is each game's code table. Item tests
  (v0.89.1): sweep's `behave items` with ITEMS_BIN = every item of the
  game (testchars mode's ALLITEMS=1 ITEMS_OUT=...; both games must stay
  at 0 failures), and `behave itemtest` - the ITEMTEST characters read by
  Add Character from the game folder (ITEMTEST_REPORT=file: a line an
  item, for Tom).
- Secret of the Silver Blades (Tom, 2026-10-12: implementing now, in
  discrete chunks a commit each - his usage is limited, nothing may be
  lost): facts and the plan in the Project's claude/secret_facts.md
  (sections 1-13) and claude/secret_facts_2.md (14 on), SPEC
  6c. Its GOG files: scratch /tmp/claude-0/games3/secret/...; game number
  3 (lwsim / playsim GAME=3). Its frame is code-drawn bars
  (`variant.bars`, tiles 52-59 through `variant.tile_window`), font in
  8X8D5 (`font_file`), scripts = Curse's opcodes but 0x3C / 0x41, block
  numbers unique across a family's files, areas 1-6 sharing them two by
  two - file (area + 1) / 2 (`area_by_block`, `play::open_area_dax`,
  `area_fmt`). Rule tables: `classes::Layout::secret` shapes.
  Records: `party::from_secret` (+ `Character::ext`, races renumbered).
  The playsim's Secret run: `GAME=3 SAVE=A sim <copy of its folder>`.
  Its behaviour checks: sweep's `behave secret` (-DCYD_TEST_HOOKS
  -DBEHAVE_SECRET, GAME=3; ONLY= values, train, level, vault, spells), oracle
  /tmp/claude-0/bt/oracle_secret.txt from scratch `secret_oracle.py`
  (Secret's own tables + the facts' constants; never in the repo). The
  playsim's SEED=n (the game's dice), FIGHTTYPE (typed when a fight asks
  a number), SAVECHECK (stored saves against the tables), GROUNDS (the
  field's ground values). Scratch tools:
  /tmp/claude-0/secret (ops.py: the interpreter's operand counts,
  ecltrace: every block followed from its entry points, py/match.py:
  DOSBox screenshots matched against tiles, lw/lwsim.cpp: the Screen /
  Walk Tests on the PC). DOSBox: `usescancodes=false` makes Return and
  letters work.
- PortMaster ports are a stretch goal (Tom): keep everything portable.
- Game screens (Walk Test, Play Test now, the games later): canvas 1:1 at the top left
  (`frame::set_left`), controls below it, on 480x320 the Companion strip on
  the right (SPEC section 4). Which map / walls an area uses comes from its
  ECL script (`ecl::find_map_load`), never from tables in the repo.
- Portability (SPEC section 11): game logic takes key events like the
  original (letters, arrows / keypad, Enter, Esc); the CYD front end turns
  taps into keys. Nothing platform-specific in `src/engine/`.
  **The platform layer** (Tom, 2026-10-12, v0.91.1: "Separating the game
  logic from the hardware its running on is an important part"):
  `src/game/` (play.cpp, play_fight.inc, look, walk) and `src/engine/`
  use ONLY `src/platform/*.h` for the machine - `plat::now_ms / log /
  free_heap / largest_block`, `plat::File / open / exists / remove /
  mkdir / card_ready / FileSource`, `audio_*` (platform/audio.h),
  `memlog::` (platform/memlog.h). Never Arduino.h, Serial, millis,
  fs::File, sd_fs(), heap_caps or board headers there (CI compiles
  src/game with only -Isrc). Boards: `hal/platform_esp32.cpp` (+ audio,
  memlog); PC: `tools/host/` (the playsim / sweep build with
  `-Itools/host -Isrc ... tools/host/*.cpp`; `pc::card_root()` is the
  folder standing for /GOLDBOX; the driver owns `g_now`).
  `tools/host_tests/test_platform.cpp` tests the PC side.
- Stream DAX blocks (`dax::RleReader`) instead of loading whole blocks where
  possible; RAM is tight (largest free block ~75-110 KB). Static RAM counts
  too: the heap block right after .bss shrinks with every static byte
  (v0.21.0's WiFi link took ~23 KB static and the 4.0" Play Test ran out).
  So: big tables only on the heap while their screen is open (the asset
  viewer's `Assets`), the Play Test's state in pieces (Data, World,
  GameState, Party each a block of its own), and check `pio run` RAM
  (DRAM .bss) before / after any change that links a new library.
  The boards have NO PSRAM (GPIO 16 / 17, the original ESP32's PSRAM pins,
  drive the RGB LED; Settings shows "no PSRAM"). loop() has Arduino's 8 KB
  stack on the classic boards, 16 KB on the S3 / C5 (main.cpp; v0.103.3:
  Play's start left under 200 bytes of the 8 KB and Tom's S3, whose
  SD/MMC driver goes deeper, crashed there - PC 0xfffffffe, backtrace
  corrupted). **Stack is watched like the heap**: every memory-log line
  ends "stack left N" (loop()'s, never used so far; QEMU's mem lines too);
  under ~1 KB is a crash waiting. Keep 0.5 KB locals (a `plat::FileSource`
  is 512 bytes of buffer) out of deep chains - split them into
  `__attribute__((noinline))` steps as play::open's (`open_program`,
  `open_tiles`, `open_sky`, `open_script_words`, `load_party_text`); check
  frames with `PLATFORMIO_BUILD_FLAGS=-fstack-usage` (.su files). QEMU
  (v0.103.3): classic 2.1 KB left at Play, 1.5 KB at a fight's start;
  deep work (PNG / PDF / JPEG: the scan's icons and journals, the
  journal book, Home's icon decode) runs via `hal/bigstack.h` on a 16 KB
  stack of its own only while it runs. The SD card mounts with 3 file
  slots (~4 KB of RAM each; 4 until v0.85.7): never more than 3 files
  open at once (the playsim's card has 3 too - SLOTS=n to try others).
  v0.22.0: static RAM 48 KB (v0.20.0 63 KB, v0.21.0 90 KB).
- **Memory is the scarcest thing (Tom, 2026-10-11).** The budget on a
  320x240 board: ~137 KB free at Home with the 64 KB canvas, ~169 KB
  since v0.87.0's 32 KB one (QEMU: 173 KB, largest block 110 KB); the Play
  Test keeps ~82 KB (Data 21.6, Party 13.8, World 13.5, GameState 12.8,
  Names 5.4, Ground 4, the VM, sound 3.6); a fight adds Fight ~10.5 KB +
  Battle 14.6 KB + its monsters' records. Tom's 3.2" ran out at a fight's
  start (free 12 KB). Rules: (1) **the memory log** - `hal/memlog.*`
  writes `_CYD/MEMORY.TXT` (Settings - Logs - Memory; the last session's
  as MEMORY.OLD): screen changes, areas, fights, saves / loads, every
  "not enough memory", a line a minute while playing, LOW when the
  largest block drops under 24 KB - new heavy features note themselves
  there; (2) what a screen doesn't use waits on the card while a bigger
  one runs (`frame::park` the canvas for the book; the 3D view's walls
  park to `_CYD/WORLD.TMP` during a fight and the field's tiles use their
  memory - `park_world`); (3) no fixed-size word tables (the fight's words
  are packed: `wbuf` / `woff`); (4) before / after any change that adds a
  long-lived block, measure: the playsim's `SIZES=1` (the big blocks'
  sizes), sweep's `memloop` (one session, fight after fight: the heap in
  use stays flat) and `HEAPLOG=1` on a monkey run; (5) **the heap by
  call site**: link `tools/playsim/heapprof.cpp` into the playsim
  (`-fno-omit-frame-pointer ... -Wl,--wrap=malloc,--wrap=free,--wrap=calloc,--wrap=realloc -ldl`)
  and run with HEAPPROF=1 - each fight's start, every third round and the
  highest point, biggest first (addr2line the sites); (6) **recovery**
  (Tom, 2026-10-12: "proactive methods ... ways to recover"):
  `play::make_room(kb, what)` before a big load drops what reads again -
  the sound playing, then (only while exploring) the healing words and
  the magic tables - each drop a memory-log line; `tidy_memory()` does it
  every 5 s while exploring when the largest block is under 32 KB; a
  fight ends with the magic tables dropped; a sound in a fight is left
  out (logged) when the largest block is under 10 KB. The playsim's
  LOWMEM=kb pretends memory is that low (every drop path runs). Tom's
  4.0" log (v0.85.7): Play 78 KB, a fight's start left 24 KB, mid-fight
  8 then 1 KB; v0.86.1 cut a fight ~12 KB (fighters as many as the fight
  has, not 71; the pages' lines in one 2 KB pool). (7) **the budget**:
  the playsim's fights `SAVE=A RUNAT=8AC2 FIGHT=1` (Curse) and `GAME=1
  SAVE=A RUNAT=85F9 FIGHT=1` (Pool) with heapprof linked and BUDGET=150
  must stay within 150 KB at their highest point (the 32 KB canvas
  included; v0.87.1: 145 / 143 KB, was 195 at v0.85.7) - raise it only
  with a reason. Also since v0.87.1: the picture animation's DAX index
  (3 KB) only while a picture animates; the magic tables load alone
  (`MagicRules`, 4.7 KB) instead of in character making's 6.9 KB block.
  (8) **careful cuts** (Tom, 2026-10-12): a reduction must not take away
  what a later game needs - the fighters are the fight's own plus
  `combat::kSpareFighters` (8) for any that join mid-fight (summons,
  allies in stages - Curse / Pool have none: coab's LOAD MONSTERs all come
  before the fight), taken with `combat::add_fighter` (nullptr: full).
  (9) **sound** (Tom: "Prioritize the hit and miss sounds"): the sounds
  are synthesised from the player's own driver byte code (3.6 KB - nothing
  to compress); what costs is the output (task + DAC ~7 KB). A fight holds
  it from start to end (`audio_hold`, `play::sound_hold`; the memory log
  says if it couldn't) and make_room never drops a held output; outside
  that, a sound may be left out when memory is low, but never Hit or Miss.
- Toolchain: pioarduino platform 55.03.312-1 (Arduino-ESP32 3.3.x), LovyanGFX
  1.2.x, huge_app.csv (no OTA - the web flasher is the update path).
- Claude's local builds: PlatformIO in a venv; the proxy CA must be appended to
  the certifi bundles under the venv and `~/.platformio/penv` or downloads fail.

## UI rules (Tom's, from CYD-Classic-Games)
- Big targets; taps act on release at the PRESS point; no drag / swipe
  except as below.
- No "are you sure" confirmations. Title Case for keys and headings; status
  and help lines in sentence case.
- Long-press only with Tom's OK, never the only way to do something.
- Dragging (Tom, 2026-10-09) only where something scrolls, slides or pans
  (the logs, the brightness slider, the journal entry and book pictures -
  those move when the stylus lifts) and never the only way: taps do it too.
- Picture viewers' keys (Tom, 2026-10-09): Back | Zoom | Prev Page | Next
  Page; Zoom cycles levels (whole / width / full size), the key names the
  level shown.
- Tap highlight (Tom, 2026-10-09): what a tap acts on lights up FIRST,
  before any slow work - AS THE STYLUS TOUCHES (Tom, 2026-10-17,
  v0.104.0: `ui::poll_press` then `ui::poll_tap`; the tap still acts on
  release at the press point; a drag puts it back) - engine keys get a bright ring (`ui::tap_flash`,
  every key drawn with ui::key / key2 / key_arrow is known), the game
  screen's menu words, list lines and party menu lines turn to the
  highlight colour (`play::tap_highlight`); one already lit blinks off and
  on. New tappable things must take part.
- Slow work inside a tap puts its message up first (`play::present_now`;
  Tom, v0.85.5: saving shows "Saving to slot A...").
- Errors stay on screen until tapped (Tom, 2026-10-09) - never timed. The
  games' own timed messages ("Not enough Money.") keep the games' timing.
- Sounds: go easy - no sound on plain key presses.
- Game sound (Tom, 2026-10-09): the games' own sound effects, both ways
  the originals offered - **Tandy 1000** (the 3-voice + noise chip; the
  DEFAULT, era fans agree it's far better) and the **PC speaker** - plus
  Off, in Settings with a volume slider. Tandy whether or not a speaker is
  connected (Tom, v0.40.1: no first-start question - it hung the 3.2"; the
  player changes it in Settings). Later games'
  AdLib / Sound Blaster: when we get to them. Curse's sound driver and its
  data live in START.EXE (facts: the Project's claude/sound_facts.md); our
  own sequencer + synth (`engine/sound.*`) plays the player's byte code, read
  at run time; output on the DAC (GPIO 26, `hal/audio.*`).

## Versions and releases
- Semantic versioning from `VERSION` (started 0.1.0, 2026-10-06). Bump it in
  every push to main that changes the firmware (fix = Z, feature = Y).
- Every push to main: CI runs host tests, builds every board, redeploys the
  web flasher. Tom tests by flashing from there. The flasher's manifest
  and firmware URLs carry `?v=<version>` (2026-10-16: GitHub Pages lets a
  browser keep a file 10 minutes, and a board flashed again within that
  got the previous build while the page showed the new version).
- **Don't publish a release unless Tom asks for one.** Releases: Actions ->
  Build -> Run workflow with `release_tag` = `v` + VERSION (API
  workflow_dispatch; Claude can't push tags).

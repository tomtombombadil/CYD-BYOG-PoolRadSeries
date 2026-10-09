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
  the party ("Locked." - Bash / Pick / Knock come with the party), the 3D
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
- Settings / logs (Tom, 2026-10-09; SPEC 5c): the card scan ends at its
  scrolling log until Continue; Settings has a brightness slider, Swap
  Red/Blue with red / green / blue blocks, a Logs key (Card Scan, Restarts,
  Errors), pages with arrows bottom right when keys don't fit. Logs come
  off the board on the card (`/GOLDBOX/_CYD/*.TXT`). NO WiFi / email (Tom,
  v0.22.0: tried in v0.21.0, ~23 KB of RAM for good - every KB goes to the
  games). Don't link WiFi, BT or other networking libraries.
- Engine Menu (Tom, 2026-10-09): the Play Test's Area key became a Menu
  key (both screen sizes) opening the engine's own tabbed screen: Journal
  (entries met so far) and Journal PDF (the book) now; further engine
  features get tabs there. Area stays on the game's menu line, and a tap
  on the 3D view toggles 3D / Area.
- Card scan (Tom, 2026-10-09): scan ONCE; boot reads the scan log and
  loads the library from it. Rescan Card is the only rescan - never
  check the card for changes on its own. Everything the board makes from
  the player's files goes in `/GOLDBOX/_CYD/` (per game a sub-folder):
  the scan log (also readable text: games, PDFs, icons found, problems),
  icons decoded once at the screen's size (a card in a board of another
  size gets its own icon files when first shown), the journal entries.
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
  `custom_firmware_name` (`TTB-CYD-PRS_<size>in_<DRIVER>_<touch>`),
  `custom_board_title`, `custom_board_hint`, `custom_board_tested` drive CI,
  releases and the flasher - platformio.ini is the single source of truth.
- `custom_board_tested = no` on a board until Tom has run THIS firmware on
  it. Tested: all five boards (Tom, 2026-10-09, v0.20.0); a new board starts
  at `no`.
- 2.8" ESP32-2432S028: touch is bit-banged (`touch_xpt2046_soft.hpp`, an
  `lgfx::ITouch`) so the SD card can have VSPI. Keep it that way - the engine
  needs the card.
- Landscape (rotation 1; Rotate 180 = 3). Layout always from the panel size.
- No LVGL: the games draw their own screens. `src/engine/` is plain C++ (no
  Arduino), host-tested in `tools/host_tests/`. `src/ui/frame.*` holds the one
  320x200 canvas (palette indexes) and presents it 1:1 or 1.5x (480x320).
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
  .TLB/.GLB "newer format" games, listed, not readable.
- Library (Tom, 2026-10-07): title bar shows the firmware version (to see
  which build is flashed); ONE game a page, no key look, the whole space
  between header and bottom keys - its GOG icon as big as fits (scaled from
  the 256 px PNG: ~154 px on 320x240, ~194 px on 480x320), the title and
  info lines wrapped inside the space beside it; < > page through games;
  "Rescan / Card" on two lines; the scan shows its scrolling list (Card
  scan above). Icons come from the
  player's goggame-<id>.ico / .dll on the card (`engine/icon.*`), never from
  the repo.
- Play Test (`src/ui/play.*`, v0.8.0): the script machine is
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
- `tools/playsim/` (Claude's side, Linux): runs the Play Test on the PC
  against the game files in scratch space (shims for Arduino / SD); use it
  to check script behaviour and take canvas snapshots before pushing.
  Its card is like the board's: `/GOLDBOX/...` paths map to the game
  folder's parent, and `library::path_of` adds `/GOLDBOX` as on the board
  (v0.21.0: a doubled `/GOLDBOX` in the save paths went unseen before).
  Snapshots of real game screens stay in scratch, never in the repo.
- PortMaster ports are a stretch goal (Tom): keep everything portable.
- Game screens (Walk Test, Play Test now, the games later): canvas 1:1 at the top left
  (`frame::set_left`), controls below it, on 480x320 the Companion strip on
  the right (SPEC section 4). Which map / walls an area uses comes from its
  ECL script (`ecl::find_map_load`), never from tables in the repo.
- Portability (SPEC section 11): game logic takes key events like the
  original (letters, arrows / keypad, Enter, Esc); the CYD front end turns
  taps into keys. Nothing platform-specific in `src/engine/`.
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
  stack; deep work (PNG / PDF / JPEG: the scan's icons and journals, the
  journal book, Home's icon decode) runs via `hal/bigstack.h` on a 16 KB
  stack of its own only while it runs. The SD card mounts with 4 file
  slots (~4 KB of RAM each): never more than 3 files open at once.
  v0.22.0: static RAM 48 KB (v0.20.0 63 KB, v0.21.0 90 KB).
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
- Errors stay on screen until tapped (Tom, 2026-10-09) - never timed. The
  games' own timed messages ("Not enough Money.") keep the games' timing.
- Sounds: go easy - no sound on plain key presses.

## Versions and releases
- Semantic versioning from `VERSION` (started 0.1.0, 2026-10-06). Bump it in
  every push to main that changes the firmware (fix = Z, feature = Y).
- Every push to main: CI runs host tests, builds every board, redeploys the
  web flasher. Tom tests by flashing from there.
- **Don't publish a release unless Tom asks for one.** Releases: Actions ->
  Build -> Run workflow with `release_tag` = `v` + VERSION (API
  workflow_dispatch; Claude can't push tags).

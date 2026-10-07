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
  keys include Side-step Left / Right (an engine addition; same checks as a
  step into that square).
- Copy protection: skipped - never asked, treated as answered.
- Later: Krynn, Savage Frontier and FRUA in this repo, after the four games.
  Keep game-specific code behind a per-game profile so they fit.
- 480x320: the game screen stays 1:1 (never scaled for play) with a Gold Box
  Companion panel (auto-map, party HP, spells) to its right; controls below.
- Build order: Curse of the Azure Bonds first, then Pool of Radiance, Silver
  Blades, Pools of Darkness.
- Journal: a converter (browser page, pdf.js, local only) cuts each entry
  out of the player's own GOG journal PDF as a picture, using per-PDF
  rectangle tables, and writes them to their SD card; the engine shows entry
  N. The GOG PDFs are scans whose OCR text is garbled, and entries include
  maps - so pictures, not text. No journal text or images ever in the repo,
  firmware or site (SPEC section 7).

## Bring your own game - hard rules
- The repo and firmware contain nothing from the games: no DAX files, no
  extracted pictures, text, journal entries or code. Tests build their own
  synthetic files. Screenshots of real game graphics stay out of the repo.
- Game files are read from `/GOLDBOX/<folder>/` on the SD card, never copied
  into flash.

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
- `custom_board_tested = no` on every board until Tom has run THIS firmware on
  it (all five as of v0.1.0).
- 2.8" ESP32-2432S028: touch is bit-banged (`touch_xpt2046_soft.hpp`, an
  `lgfx::ITouch`) so the SD card can have VSPI. Keep it that way - the engine
  needs the card.
- Landscape (rotation 1; Rotate 180 = 3). Layout always from the panel size.
- No LVGL: the games draw their own screens. `src/engine/` is plain C++ (no
  Arduino), host-tested in `tools/host_tests/`. `src/ui/frame.*` holds the one
  320x200 canvas (palette indexes) and presents it 1:1 or 1.5x (480x320).
  `src/ui/ui.*` = keys, header, touch filter for the engine's own screens.
- Stream DAX blocks (`dax::RleReader`) instead of loading whole blocks where
  possible; RAM is tight (largest free block ~75-110 KB).
- Toolchain: pioarduino platform 55.03.312-1 (Arduino-ESP32 3.3.x), LovyanGFX
  1.2.x, huge_app.csv (no OTA - the web flasher is the update path).
- Claude's local builds: PlatformIO in a venv; the proxy CA must be appended to
  the certifi bundles under the venv and `~/.platformio/penv` or downloads fail.

## UI rules (Tom's, from CYD-Classic-Games)
- Big targets; taps act on release at the PRESS point; never drag/swipe.
- No "are you sure" confirmations. Title Case for keys and headings; status
  and help lines in sentence case.
- Long-press only with Tom's OK, never the only way to do something.
- Sounds: go easy - no sound on plain key presses.

## Versions and releases
- Semantic versioning from `VERSION` (started 0.1.0, 2026-10-06). Bump it in
  every push to main that changes the firmware (fix = Z, feature = Y).
- Every push to main: CI runs host tests, builds every board, redeploys the
  web flasher. Tom tests by flashing from there.
- **Don't publish a release unless Tom asks for one.** Releases: Actions ->
  Build -> Run workflow with `release_tag` = `v` + VERSION (API
  workflow_dispatch; Claude can't push tags).

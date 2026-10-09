# Third-party notices

## Libraries (linked, not copied)
- **LovyanGFX** - FreeBSD license, https://github.com/lovyan03/LovyanGFX.
  `src/boards/touch_xpt2046_soft.hpp` follows the sampling approach of its
  XPT2046 driver (7 readings, median per axis) in our own code.
- **Arduino-ESP32 / ESP-IDF** (via the pioarduino PlatformIO platform) -
  LGPL-2.1 / Apache-2.0.

## Copied from Tom's own projects (MIT)
- From CYD-Classic-Games: `src/games/common/inflate.*` (raw DEFLATE
  decoder) is the base of `src/engine/inflate.*`, changed to stream through
  a 32 KB window.
- From CYD-Classic-Games: `src/boards/*` (board files), `src/hal/storage.*`,
  `sdcard.*`, `touch_cal.*`, `panel_prefs.*`, `tools/version.py`,
  `tools/make_site.py`, `web/index.html`, `.github/workflows/build.yml` (adapted).

## Loaded by the web pages (not copied into the repo)
- **pdf.js** 3.11.174 (Mozilla, Apache License 2.0), https://mozilla.github.io/pdf.js/ -
  the Journal Converter (`web/journal/`) loads it from cdnjs to read the
  player's own journal PDF in their browser.
- **ESP Web Tools** (Apache License 2.0) - the installer page loads it from unpkg.

## Used for reference only (nothing copied)
- **coab** - Curse of the Azure Bonds reimplementation in C# by Simeon Pilgrim,
  https://github.com/simeonpilgrim/coab. No license file, so all rights
  reserved: used only to learn the DAX, picture, GEO and ECL formats.

## Game data
None. The player supplies their own original game files on the SD card.
The journal tables (`web/journal/tables.json`) hold only page numbers and
positions inside the player's own journal PDF - no text or pictures.

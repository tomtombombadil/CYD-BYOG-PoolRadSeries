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
  a 32 KB window. `src/ui/log_pack.*` (Send Log's packer) is
  `src/engine/log_pack.*` here, only its URL changed, and `web/l/index.html`
  is adapted from its page of the same name.
- From CYD-Classic-Games: `src/boards/*` (board files), `src/hal/storage.*`,
  `sdcard.*`, `touch_cal.*`, `panel_prefs.*`, `tools/version.py`,
  `tools/make_site.py`, `web/index.html`, `.github/workflows/build.yml` (adapted).

## Included third-party code
- **TJpgDec R0.03** (Tiny JPEG Decompressor), (C) ChaN 2021,
  http://elm-chan.org/fsw/tjpgd/ - `src/engine/third_party/tjpgd.*`, from
  Bodmer's TJpg_Decoder copy with his byte-swap change removed. License:
  "No restriction on use. You can use, modify and redistribute it for
  personal, non-profit or commercial products UNDER YOUR RESPONSIBILITY.
  Redistributions of source code must retain the above copyright notice."
  Our changes are marked "CYD BYOG" (an MCU skip callback, C++ casts).

- **QR Code generator library (C)**, Copyright (c) Project Nayuki, MIT
  License, https://www.nayuki.io/page/qr-code-generator-library -
  `src/engine/third_party/qrcodegen.*`, unchanged (from
  github.com/nayuki/QR-Code-generator, commit 3c6d0b3). Send Logs' QR code.

## Loaded by the web pages (not copied into the repo)
- **ESP Web Tools** (Apache License 2.0) - the installer page loads it from unpkg.

## Used for reference only (nothing copied)
- **coab** - Curse of the Azure Bonds reimplementation in C# by Simeon Pilgrim,
  https://github.com/simeonpilgrim/coab. No license file, so all rights
  reserved: used only to learn the DAX, picture, GEO and ECL formats.

## Game data
None. The player supplies their own original game files on the SD card.
The journal tables (`src/engine/journal_tables.cpp`, `tools/journal/tables.json`)
hold only page numbers and positions inside the player's own journal PDF -
no text or pictures.

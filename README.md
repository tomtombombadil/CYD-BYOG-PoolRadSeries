# CYD BYOG Pool of Radiance Series Engine

Play SSI's Gold Box games - **Pool of Radiance**, **Curse of the Azure Bonds**,
**Secret of the Silver Blades** and **Pools of Darkness** - on an ESP32 "Cheap
Yellow Display", from your own copy of the games.

**Bring your own game:** this is an engine only. Nothing from the original games
is included; you copy the original DOS game files to a microSD card and the board
reads them from there.

> **Early development.** A library and asset viewer that finds your games on
> the card and shows the pictures inside their files, the games' own font and
> (Curse of the Azure Bonds) its title sequence, screen frame and text windows. The games aren't
> playable yet. Roadmap: [docs/SPEC.md](docs/SPEC.md).

## Install

Easiest: **https://tomtombombadil.github.io/CYD-BYOG-PoolRadSeries/** in Chrome
or Edge on a computer - pick your board, click Install.

| Board (what's printed on the back) | Firmware |
|---|---|
| 2.8" ESP32-2432S028, single micro-USB (ILI9341) | `TTB-CYD-PRS_2.8in_ILI9341_Resistive` |
| 2.8" ESP32-2432S028, micro-USB + USB-C (ST7789) | `TTB-CYD-PRS_2.8in_ST7789_Resistive` |
| 3.2" ESP32-32E, 240x320 | `TTB-CYD-PRS_3.2in_ST7789_Resistive` |
| 3.5" ESP32-32E, 320x480 | `TTB-CYD-PRS_3.5in_ST7796_Resistive` |
| 4.0" ESP32-32E, 320x480 | `TTB-CYD-PRS_4.0in_ST7796_Resistive` |

Tested with this firmware so far: the 4.0".

## Your games on the SD card

1. Format a microSD card as FAT32.
2. Make a folder `GOLDBOX` at the top of the card.
3. Copy each game's folder from your own install into it, e.g.
   `GOLDBOX/POOLRAD`, `GOLDBOX/CURSE`, `GOLDBOX/SECRET`, `GOLDBOX/DARKNESS`.
   Any folder name containing the game's name works.

**Game icons (optional):** the DOS games have no icons of their own; GOG's
install folder holds each game's icon in a `goggame-<number>.ico` or
`goggame-<number>.dll` file. Copy that file into the game's folder on the card (for Pool of Radiance it
sits one folder above the game files)
and the library shows the icon next to the game's name. (GOG's `Support.ico`
is the same generic GOG icon for every game, so it is ignored.)

The other Gold Box games are recognised too (folders `CHAMPIONS`, `DEATH`,
`QUEEN`, `GATEWAY`, `TREASURE`, `UNLIMIT`, or their full names), so their files
can be looked at; they're planned after the four. The Dark Queen of Krynn and
Unlimited Adventures use a newer file format the viewer can't read yet.

## Using the asset viewer

The games aren't playable yet; this build shows what is inside their files.

1. **Library** (the firmware version is in its title bar): one game a page,
   with its icon if you copied it; `<` / `>` go through the games. Tap the
   game to open it.
2. **Files**: the game's DAX archives. Tap one. For Curse of the Azure Bonds
   the middle key, **Screen Test**, shows the game's own look, drawn from your
   files (`START.EXE`, `GAME.OVR`, `TITLE.DAX`, `8X8D1.DAX`): the title
   sequence and credits (tap = next picture), a text window printing and the
   menu line (tap to go on; tap a menu word to choose it), the screen frames
   and their tiles. `< Prev` / `Next >` step through the pages.
3. **Blocks**: each piece inside the file. **Gold keys are pictures** (size and
   frame count shown); navy keys are other data. Tap one.
4. **Picture**: `< Prev` / `Next >` step through the frames, then the next
   block. Tapping the left / right third of the picture does the same; the
   middle shows the block's details. `Back` returns to the list.
   Other data shows as a hex dump.

Good files to try in Curse: `TITLE.DAX` (title screens, blocks 1-4),
`8X8D1.DAX` block #201 (the game's own font, from v0.2.0),
`PICn.DAX` / `BIGPICn.DAX` (event pictures), `HEAD*.DAX` / `BODY*.DAX`
(portraits), `SKY.DAX`, `COMSPR.DAX` (combat figures). Pools of Darkness's
256-colour pictures show too (from v0.1.3), except its animations (`PIC1`,
`SPRIT1`) and wall tiles (`8X8D1`), which are still to be worked out.

## Building

VS Code + PlatformIO: open the folder, pick the board's env in the status bar,
Build / Upload. Host tests of the file-format code run in CI
(`tools/host_tests/`).

## Credits

- Board support, CI and web flasher from
  [CYD-Classic-Games](https://github.com/tomtombombadil/CYD-Classic-Games).
- File formats learned from the [coab](https://github.com/simeonpilgrim/coab)
  reimplementation of Curse of the Azure Bonds (no code copied).
- Graphics: [LovyanGFX](https://github.com/lovyan03/LovyanGFX) (FreeBSD license).

Pool of Radiance, Curse of the Azure Bonds, Secret of the Silver Blades and
Pools of Darkness are the property of their owners. This project is not
affiliated with them and includes none of their files.

MIT license.

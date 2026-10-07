# CYD BYOG Pool of Radiance Series Engine

Play SSI's Gold Box games - **Pool of Radiance**, **Curse of the Azure Bonds**,
**Secret of the Silver Blades** and **Pools of Darkness** - on an ESP32 "Cheap
Yellow Display", from your own copy of the games.

**Bring your own game:** this is an engine only. Nothing from the original games
is included; you copy the original DOS game files to a microSD card and the board
reads them from there.

> **Early development.** v0.1.0 is milestone 1: a library and asset viewer that
> finds your games on the card and shows the pictures inside their files. The
> games aren't playable yet. Roadmap: [docs/SPEC.md](docs/SPEC.md).

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

None of these has been tested with this firmware yet.

## Your games on the SD card

1. Format a microSD card as FAT32.
2. Make a folder `GOLDBOX` at the top of the card.
3. Copy each game's folder from your own install into it, e.g.
   `GOLDBOX/POOLRAD`, `GOLDBOX/CURSE`, `GOLDBOX/SILVER`, `GOLDBOX/DARKNESS`.
   Any folder name containing the game's name works.

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

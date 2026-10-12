# Gold Box Game Engine for Cheap Yellow Displays
**I really need to change the name of this repo to "CYD-GoldBoxGameEngine"

Play SSI's Gold Box games - **Pool of Radiance**, **Curse of the Azure Bonds**, **Secret of the Silver Blades** and **Pools of Darkness** (and maybe MORE!) on an ESP32 "Cheap Yellow Display". Your own legitimate copy of each game is required!

**Bring your own game:** this is a game engine only. Nothing from the original games is included; you copy the original DOS game files to a microSD card and the board reads them and runs the game from them.

> **Early development.** This repo is in early development and testing stages.
> Currently Curse and PoolRad are working and being tested.
> Roadmap:
> [docs/SPEC.md](docs/SPEC.md).

A stretch goal of this repo is to include all of the Gold Box games that were based on the same Pool of Radiance game engine. That includes:
 - Pool of Radiance (currently runs, but is in testing)
 - Curse of the Azure Bonds (currently runs, but is in testing)
 - Secret of the Silver Blades
 - Pools of Darkness
 - Champions of Krynn
 - Death Knights of Krynn
 - The Dark Queen of Krynn
 - Gateway to the Savage Frontier
 - Treasures of the Savage Frontier

Unlimited Adventures is not on the menu because of the very tight memory constraints of the original ESP32 (most base CYD models). The ESP32-S3 and ESP32-C5 models might be able to support it. As it is: VGA modes are not supported on the base CYD models. The original ESP32 just doesn't have enough available SRAM for VGA. Frankly it's a miracle we have these games working with only about 320KB of sram available.

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

All five boards are tested with this firmware.

**Testing and development for ESP32-S3 and NM-CYD-C5 boards is under way!**

## Your games on the SD card

1. Format a microSD card as FAT32.
2. Make a folder `GOLDBOX` at the root of the card.
3. Copy each game's whole GOG install folder into it, as it is: `GOLDBOX/Pool of Radiance`, `GOLDBOX/Curse of the Azure Bonds`, `GOLDBOX/Secret of the Silver Blades`, `GOLDBOX/Pools of Darkness`. (Any folder name containing the game's name works.) NOTE: It doesn't HAVE to be the GOG version of the game, but that is what we are using in development and testing. The Steam versions of the game should work just fine as well. Also, other Gold Box games are planned, so you may as well copy over Champions of Krynn, Death Knights of Krynn, The Dark Queen of Krynn, Gateway to the Savage Frontier, and Treasures of the Savage Frontier game folders as well. The games are relatively small (less than 400MB all totaled) so you don't even need a big SD card.
4. Remove the SD card and put it in your CYD and apply power.
5. When the CYD boots, it will scan the SD card for the games, letting you know what it finds. It will also process the Adventurer's Journal PDFs from the games so the game can show the journal entries as they are referenced in-game. It will also pull the game icon art from the GOG .dll files and display it in the game chooser interface on the CYD. Without those GOG files, the games would just show by title (no art).
6. Once your SD card is scanned and processed, you'll be in the game chooser where you navigate left and right to select which Gold Box game you want to play.


## Settings and logs

**Settings** (from the library): a brightness slider, color fixes (Swap
Red/Blue shows red, green and blue blocks - if they don't match their
names, tap it), Rotate 180, touch calibration and **Logs**. The card scan
ends on its log; read it (tap the top / bottom half of the box, or drag),
then tap Continue. **Logs** shows the scan log, restarts and errors; to
send them, take the card to a PC - they're text files in `GOLDBOX/_CYD`.

## The Adventurer's Journal

The original games referred to numbered entries in a printed journal. The GOG and Steam releases of these games put those printed docs into PDFs, including the Adventurer's Journal for each game. When you copy the entire GOG or Steam game's folder to the GOLBOX\ folder of your SD card, that includes the PDF docs. The SD card scan that runs on the CYD digests each journal PDF and pulls each entry out so that it can be shown in-game at the right time. The firmware knows only where each entry sits on the pages of the PDF - no journal text or pictures are in the firmware or this repository.

## BUILDING

VS Code + PlatformIO: open the folder, pick the board's env in the status bar,
Build / Upload. Host tests of the file-format code run in CI
(`tools/host_tests/`).

## CREDITS

- Board support, CI and web flasher from
  [CYD-Classic-Games](https://github.com/tomtombombadil/CYD-Classic-Games).
- File formats learned from the [coab](https://github.com/simeonpilgrim/coab)
  reimplementation of Curse of the Azure Bonds (no code copied).
- Graphics: [LovyanGFX](https://github.com/lovyan03/LovyanGFX) (FreeBSD license).

Pool of Radiance, Curse of the Azure Bonds, Secret of the Silver Blades, Pools of Darkness, Champions of Krynn, Death Knights of Krynn, The Dark Queen of Krynn, Gateway to the Savage Frontier, and Treasures of the Savage Frontier are the property of their owners. This project is not affiliated with them and includes none of their files or copyrighted material.

## LICENSE
MIT license.

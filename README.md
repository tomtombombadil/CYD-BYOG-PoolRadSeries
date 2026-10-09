# CYD BYOG Pool of Radiance Series Engine

Play SSI's Gold Box games - **Pool of Radiance**, **Curse of the Azure Bonds**,
**Secret of the Silver Blades** and **Pools of Darkness** - on an ESP32 "Cheap
Yellow Display", from your own copy of the games.

**Bring your own game:** this is an engine only. Nothing from the original games
is included; you copy the original DOS game files to a microSD card and the board
reads them from there.

> **Early development.** The viewer's Play Test runs Curse of the Azure
> Bonds from its own scripts: the party menu (load the sample party GOG
> includes, or make your own characters and add them), walking, events,
> text, pictures, shops, the temple, training, camp (memorizing, resting
> and casting spells), fights (first steps: moving and attacking; spells
> and items in fights to come), saving, the journal. Roadmap:
> [docs/SPEC.md](docs/SPEC.md).

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

## Your games on the SD card

1. Format a microSD card as FAT32.
2. Make a folder `GOLDBOX` at the top of the card.
3. Copy each game's whole GOG install folder into it, as it is:
   `GOLDBOX/Pool of Radiance`, `GOLDBOX/Curse of the Azure Bonds`,
   `GOLDBOX/Secret of the Silver Blades`, `GOLDBOX/Pools of Darkness`.
   (Any folder name containing the game's name works.)

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
   game to open it. The card is scanned once - the first time the board
   starts with it - and a list shows what is found. What the board makes
   from your files (icons at the screen's size, the scan log `SCAN.TXT`
   you can read on a PC) goes in `GOLDBOX/_CYD`. After you add or change
   games, tap **Rescan Card**.
2. **Files**: the game's DAX archives. Tap one. For Curse of the Azure Bonds
   the middle key, **Screen Test**, shows the game's own look, drawn from your
   files (`START.EXE`, `GAME.OVR`, `TITLE.DAX`, `8X8D1.DAX`): the title
   sequence and credits (tap = next picture), a text window printing and the
   menu line (tap to go on; tap a menu word to choose it), the screen frames
   and their tiles. `< Prev` / `Next >` step through the pages.
   **Walk Test** walks the game's 3D areas (every map the game's area
   scripts load, with their own walls): the arrow keys turn, step forward,
   side-step and turn around; **Area** shows the game's area map; tap the
   party panel (or **Next** on the menu line) for the next map. To get
   past walls: in the **Area** view tap a square to move there (the map
   window follows you, so far squares take a few taps); on 480x320 tap a
   square on the map beside the game screen. On 480x320
   the whole map is drawn beside the game screen. No events yet - only
   walls and doors (for testing, locked doors let you through and say so).
   **Play Test** starts a new game of Curse run by the game's own scripts:
   the opening, then events as you walk - text (tap, or any key, to go
   on), menus (tap a word on the menu line, or a line of a list), pictures
   and the clock. The game's own menu line works too: **Area**, **Search**
   (on / off), **Look**. Event pictures animate while the game waits for
   you; when the game asks for a word or a number, a keyboard comes up (the
   typing shows on the menu line; Enter answers). Travel works too: the
   wilderness map, with a blinking square where the party is, and the areas
   you go into. The game starts at its party menu: **Load Saved Game**
   (GOG's sample party is saved game A) or **Create New Character**, then
   **Add Character to Party**, then **BEGIN Adventuring**. Combat isn't in
   yet, so fights are skipped; when the game says "record it in
   journal entry 31" the entry comes up on screen (from the journal PDF in
   the game's folder - see below); on 480x320 **Look** is also a
   key beside the pad and the map shows beside the game screen.
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

## Settings and logs

**Settings** (from the library): a brightness slider, colour fixes (Swap
Red/Blue shows red, green and blue blocks - if they don't match their
names, tap it), Rotate 180, touch calibration and **Logs**. The card scan
ends on its log; read it (tap the top / bottom half of the box, or drag),
then tap Continue. **Logs** shows the scan log, restarts and errors; to
send them, take the card to a PC - they're text files in `GOLDBOX/_CYD`.

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

## The Adventurer's Journal

The games refer to numbered entries in the printed journal. Copy the
journal PDF from your GOG game (e.g. `Adventurers Journal.pdf`) into the
game's folder on the card. The card scan cuts every entry out of your PDF
(Curse of the Azure Bonds: about 15 MB in `GOLDBOX/_CYD/Curse of the Azure Bonds/JOURNAL.DAT`,
a few minutes, once) and the board shows an entry when the game mentions it.
The firmware knows only where each entry sits on the pages of the GOG PDF -
no journal text or pictures are in the firmware or this repository. A PDF it
doesn't know is reported in the scan log (`GOLDBOX/_CYD/SCAN.TXT`).

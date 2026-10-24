# Gold Box Game Engine for Cheap Yellow Displays
**I really need to change the name of this repo to "CYD-GoldBoxGameEngine"**

Play SSI's Gold Box games on an ESP32 "Cheap Yellow Display". Your own legitimate copy of each game is required!
- **Pool of Radiance**
- **Curse of the Azure Bonds**
- **Secret of the Silver Blades**
- **Pools of Darkness**
- **Gateway to the Savage Frontier**
- **Treasures of the Savage Frontier**
- **Champions of Krynn**
- **Death Knights of Krynn**
- **The Dark Queen of Krynn**

**Bring Your Own Game (BYOG):** this is a game engine only. Nothing from the original games is included; you copy the original DOS game files to a microSD card and the board reads them and runs the game from them. The experience is best from the Steam collections of the games, but also works with the GOG versions of the games.

Unlimited Adventures is not on the menu because of the very tight memory constraints of the original ESP32 (most base CYD models). The ESP32-S3 and ESP32-C5 models might be able to support it. As it is: VGA modes are not supported on the base ESP32 CYD models. The original ESP32 just doesn't have enough available SRAM for the game and VGA. Frankly it's a miracle we have these games working with only about 320KB of sram available.

## Install

Easiest method: Use the web flasher!
**https://tomtombombadil.github.io/CYD-BYOG-PoolRadSeries/** in Chrome or Edge on a computer - pick your board, click Install.

| Board (what's printed on the back) | Firmware |
|---|---|
| 2.8" ESP32-2432S028, single micro-USB (ILI9341) | `TTB-CYD-PRS_2.8in_ILI9341_Resistive` |
| 2.8" ESP32-2432S028, micro-USB + USB-C (ST7789) | `TTB-CYD-PRS_2.8in_ST7789_Resistive` |
| 3.2" ESP32-32E, 240x320 | `TTB-CYD-PRS_3.2in_ST7789_Resistive` |
| 3.5" ESP32-32E, 320x480 | `TTB-CYD-PRS_3.5in_ST7796_Resistive` |
| 4.0" ESP32-32E, 320x480 | `TTB-CYD-PRS_4.0in_ST7796_Resistive` |
| 4.0" ESP32-S3, 480x320 capacitive touch (FNK0104, ST7796 model) | `TTB-CYD-PRS_4.0in_ST7796_Capacitive_ESP32-S3` |
| 2.8" ESP32-C5, two USB-C (NM-CYD-C5) | `TTB-CYD-PRS_2.8in_ST7789_Resistive_ESP32-C5` |

These five ESP32 boards, the ESP32-C5 board and the ESP32-S3 board are tested with this firmware. The base ESP32 CYD boards I tested with were all purchased through Amazon and are 'Freenove' brand. Links below.

## Prepare Your SD Card

1. Format a microSD card as FAT32.
2. Make a folder **`GOLDBOX`** at the root of the card. Then choose below, the Steam versions of the games, or the GOG versions. You can even mix and match. It doesn't really matter where you pull the game folders from as long as you put them in the SD card's GOLDBOX folder with the proper folder names. (See below.)
   (If you have trouble formatting a larger SD card as FAT32, use the SD Association's official formatter from https://www.sdcard.org/downloads/formatter/ )

## Copy the Games to Your SD Card (STEAM VERSION)

3. Go to the folder on your PC where Steam stores your games (typically **C:\Programs Files (x86)\Steam\steamapps\common**)
4. There you will see the folders where your Steam games are stored. The Gold Box games come in various Collections. Look for "Forgotten Realms The Archives - Collection Two" and open that folder.
5. Inside that folder you will find a **games** folder and a **Launcher** folder. Copy the Launcher folder to your SD card's GOLDBOX folder.
6. Go into the games folder and copy each game's folder to the GOLDBOX folder on your SD card. (You do NOT need to copy the Hillsfar folder or the Unlimited Adventures folder, but it won't hurt anything if you do.)
7. Your SD card should now have these folders:
        GOLDBOX\CURSE
        GOLDBOX\Gateway to the Savage Frontier
        GOLDBOX\POOLRAD
        GOLDBOX\Pools of Darkness
        GOLDBOX\SECRET
        GOLDBOX\Treasures of the Savage Frontier
9. Go back twice, up to the steamapps\common folder and look for "Dungeons & Dragons Krynn Series". Inside that folder you will find another set of games and Launcher folders. You do not need to copy the Launcher folder again if you already did so from the "Forgotten Realms Archive - Collection Two" folder. Go into the "Dungeons & Dragons Krynn Series" games folder.
10. Copy each of the game folders to your SD card's GOLDBOX folder. Your SD card should now have these folders:
        GOLDBOX\Champions of Krynn
        GOLDBOX\Death Knights of Krynn
        GOLDBOX\The Dark Queen of Krynn
12. Copying the whole game folders from Steam will ensure that you have the journal PDF files, logo, and graphics files, as well as the necessary game resource files. This will include any savegame and character files you may have from playing these games on your PC.
13. All said and done, these folders should take up approximately 400 MB of your SD card. So you don't need a huge SD card. The CYD's only support up to 32 GB cards, so there's plenty of room.
14. Eject the SD card from your PC (so it doesn't get corrupted) and insert it into your CYD. After you flash this repo to your CYD, the CYD will scan the SD card for the games and do some file processing. This will take a few minutes, but only needs to be done once. If you add or remove any games from the SD card, you will need to run the scan again with the Rescan button on the CYD.

## Copy the Games to Your SD Card (GOG VERSION)

3. Go to the folder on your PC where GOG stores your games (typically **C:\GOG Games\**)
4. There you will see the folders where your GOG games are stored.
5. Go into the games folder and copy each game's folder to the GOLDBOX folder on your SD card. (You do NOT need to copy the Hillsfar folder or the Unlimited Adventures folder, but it won't hurt anything if you do.)
6. Your SD card should now have these folders:
        GOLDBOX\Champions of Krynn
        GOLDBOX\Curse of the Azure Bonds
        GOLDBOX\Death Knights of Krynn
        GOLDBOX\Gateway to the Savage Frontier
        GOLDBOX\Pool of Radiance
        GOLDBOX\Pools of Darkness
        GOLDBOX\Secret of the Silver Blades
        GOLDBOX\The Dark Queen of Krynn
        GOLDBOX\Treasures of the Savage Frontier
7. Copying the whole game folders from GOG will ensure that you have the journal PDF files, logo, and graphics files, as well as the necessary game resource files. This will include any savegame and character files you may have from playing these games on your PC.
8. All said and done, these folders should take up approximately 400 MB of your SD card. So you don't need a huge SD card. The CYD's only support up to 32 GB cards, so there's plenty of room.
9. Eject the SD card from your PC (so it doesn't get corrupted) and insert it into your CYD. After you flash this repo to your CYD, the CYD will scan the SD card for the games and do some file processing. This will take a few minutes, but only needs to be done once. If you add or remove any games from the SD card, you will need to run the scan again with the Rescan button on the CYD.

## Settings and logs

**Settings** (from the library): a brightness slider, color fixes (Swap
Red/Blue shows red, green and blue blocks - if they don't match their
names, tap it), Rotate 180, touch calibration and **Logs**. The card scan
ends on its log; read it (tap the top / bottom half of the box, or drag),
then tap Continue. **Logs** shows the scan log, restarts, errors and memory; to
send them, tap **Send Logs** and scan its QR code with a phone (the page
it opens emails them), or take the card to a PC - they're text files in
`GOLDBOX/_CYD`.

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

**Pool of Radiance, Curse of the Azure Bonds, Secret of the Silver Blades, Pools of Darkness, Champions of Krynn, Death Knights of Krynn, The Dark Queen of Krynn, Gateway to the Savage Frontier, and Treasures of the Savage Frontier are the property of their owners. This project is not affiliated with them and includes none of their files or copyrighted material.**

## LICENSE
MIT license.

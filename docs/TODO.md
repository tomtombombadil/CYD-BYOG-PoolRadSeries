# Open tasks (Tom and Claude)

Tom, 2026-10-12: "start keeping a list of tasks we need to do - both you
and me." Kept up to date with every push (CLAUDE.md); finished items go
to the bottom with their version, then drop off after a while. Synced to
the Project as claude/TODO.md.

## Tom

- [ ] Flash the latest build and play a fight-heavy session on the 4.0"
      and the 3.2"; then Send Logs. What to look for: room left in fights
      (MEMORY.TXT), Hit / Miss sounds all through a fight, whether drawing
      feels slower with the 4-bit screen (v0.87.0).
- [ ] When the 4.0" S3 board arrives: flash "4.0" ST7796 Capacitive
      (ESP32-S3)" from the web flasher (there since v0.88.0) and report
      what works - screen (colours: Settings has Invert / Swap Red/Blue),
      touch with the capacitive stylus, card, sound (Send Logs after).
- [ ] Send Logs goes to cyd.classic.games.logs@gmail.com (the Classic
      Games address) - keep it, or name another (one line in
      web/l/index.html).
- [ ] Try the items with the ITEMTEST characters (sent 2026-10-12 as
      ITEMTEST_characters.zip: Curse's six in its SAVE folder, Pool's five
      beside its SAVGAMA.DAT; what each carries in ITEMTEST.txt) and
      report what doesn't work as in the originals.
- [ ] Keep play testing Pool of Radiance and Curse; report anything that
      plays differently from the originals. New in v0.89.0 for Pool:
      potions / wands / the Manual, Gauntlets of Ogre Power, the cloaks
      and rings - worth a look.

## Claude

- [ ] **Engine screens for ports**: the platform layer is done
      (v0.91.1: src/game + src/engine use only src/platform/*.h); still
      CYD-only are the engine's own screens (Settings, journal viewer,
      library, logs - `ui/viewer.cpp` etc. draw with LovyanGFX). For a
      PortMaster port: draw them through ui:: primitives a port supplies.
- [ ] Memory, next cuts (measure each with heapprof): the party's unused
      member slots (1.7 KB each), Data's words read on demand, the spell
      names' 4 KB read buffer in pieces.
- [ ] The plain C5's scan: "The Unlimited Adventures journal couldn't be
      processed (card full, or not enough memory)" (the Companion's) - look
      at why (8 MB free there, so the card or the file).
- [ ] S3 / C5 follow-ups once Tom has run them: fix what his reports show
      (colours, touch direction, sound level - the codec's volume is
      0 dB); then the 8-bit canvas for the VGA games there.
- [ ] Dark Queen on the classic boards: try it in its own EGA mode (Tom,
      2026-10-12) when we get to the Krynn games.
- [ ] Darkness's EGA mode: how the original's EGA.DRV turns VGA pictures
      into 16 colours (facts from the player's file), so Darkness plays on
      the classic boards as its own EGA mode shows it.
- [ ] QEMU: the cache-error panic inside play::open (parked).
- [ ] **Secret of the Silver Blades** (started 2026-10-12; facts and the
      work plan in the Project's claude/secret_facts.md; SPEC 6c). Done:
      program layout, words, scripts, frame, font, title, Screen / Walk
      Test (v0.92.0); the 439-byte record and the saves (v0.93.0); party
      menu, Load, Begin into Priam's house (v0.94.0); the rule tables,
      areas (v0.95.0); the scripts' view of a character (v0.96.0); View
      Character (v0.97.0); item names (v0.98.0); fights (v0.99.0: tables,
      monsters, effects, most words). Next, a chunk a commit:
      the camp spells' result words (Bless's "is Blessed" ... aren't in
      Secret's files: see in DOSBox how it words them), Heal and the
      spells past 100 in camp; Alter ("Order Drop Speed Icon Level
      Exit" - its Level); how Secret renames an item it
      identifies / halves / joins; the exploring menu's "Move" (the
      original's first word); fights still to do: the turn undead table
      (not found: Curse's code reads its DS 0x369 at ovr 0x14684, Secret's
      routine not found), the spells' result words ("is Blessed" isn't in
      Secret's files - how it words them, in DOSBox), "The party has
      lost" / "has found treasure!" / "Turn " (not found as Pascal text),
      its new monster abilities (0x41 petrify, 0x43 pyrohydra breath, 0x46
      confusion gaze, 0x68 +4d6 fire, 0x3C +3 weapons, 0x42 half from
      pierce / cut, 0x5C no fear; what 0x51, 0x60, 0x63 are - taken as
      Curse's), Secret's monster exp rule; View's right panel (with items) and a paladin's Heal / Cure;
      items and their words (its own word table), shops; Modify
      ("Add Subtract Keep Exit"); Add from Curse; camp (spell names; 6th /
      7th level spells and 101-117 in magic); a dwarf's saves (the
      Constitution effect 0x61 kept in the record's saves?); the fight
      frame (not compared with DOSBox yet); sound, sky, approaching monsters; the version line's
      two-line look; behaviour tests.
- [ ] Tom: try the new library page (v0.103.0) on a board - the art,
      the header keys, the thirds and the triangles.
- [ ] Tom: when you have a moment, Secret's Screen Test and Walk Test
      (Settings - Dev Tools on its page) on a board - the frame, title,
      credits and maps should look like the original's.

## Done

- v0.103.0: the library page as Tom picked it - full-width art,
  top-justified, Rescan / Settings in the header, tap thirds (round),
  faint triangles.

- v0.102.0: the SNEG Launcher's box art on the library pages (Tom), the
  game's name centred over it.

- v0.101.0: Secret's camp magic - 7 spell levels, spells 101-117,
  its names, words, counts and Display as the original's.

- v0.100.0: Secret's start (the opening texts, block 17), the walls of
  Priam's house (LOAD PIECES 127 kept the set), the treasure and camp
  screens as the original's; " CAMPING" on the position line (Curse too).

- v0.99.0: Secret's fights - its tables, monsters (439-byte records),
  their effects in its own numbers, most words; Turn left out until its
  table is found; CALL 2DCB redraws the 3D view.

- 2026-10-12 (Tom): the C5 boards work with v0.94.1 (Home, the scan, a
  Secret Play Test) - marked tested.

- v0.98.0: Secret's item names (the records' own) and its word table /
  types.

- v0.97.0: Secret's View Character in its own layout, pixel for pixel
  the original's (DOSBox, the GOG party).

- v0.96.0: Secret's scripts read and write a character through its own
  field switch (race in its numbers, HP, levels, gems, status).

- v0.95.0: Secret's rule tables in its own shapes (levels to 18, 7 spell
  levels, 119 spells, its race order) - the GOG party's values come out
  of them; a ranger learns only the druid spells of levels he has slots
  for (Curse: the same, its druid spells are all 1st level).

- v0.94.1: the C5 hung at Home drawing the first icon (Tom's logs, both
  C5 boards): the drawing held the SPI bus the card shares there. Card
  reads while drawing (icons, journal pictures, the PDF book) no longer
  sit inside one display transaction on that board (`ui::card_draw_begin`).

- v0.94.0: Secret's party menu, Load Saved Game and Begin (Priam's house
  at 3,3 S); script blocks found across ECL1-3; the Play Test opens under
  Dev Tools.

- v0.93.1: Secret's race numbers (human 6, Curse's 7) turned into Curse's
  when read, back when written.

- v0.93.0: Secret's characters and saves - its 439-byte record in and out
  of the engine's (Curse's) layout with an extension for what has no
  place there; saves without the script's bytes; .STF items (67 bytes),
  .SFX effects, .WHO characters.

- v0.92.0: Secret of the Silver Blades - its scripts decode (Curse's
  opcodes but 0x3C / 0x41), its frame bars (tiles 52-59 through a tile
  window), font (8X8D5), title, credits; Screen Test and Walk Test work.

- v0.91.1: the platform layer - the game code (src/game: play, fights,
  Screen / Walk Tests) and the engine use only src/platform/*.h (time,
  log, memory, files, sound, memory log); boards: hal/platform_esp32.cpp;
  PC: tools/host (the playsim and sweeps run on it; CI builds the game
  code with no board headers and tests the PC side).
  Found by its tests and fixed: a script's treasure with no fight (COMBAT
  with no monsters - Curse ECL1 block 80) crashed since v0.86.1 (the
  fight's fighter blocks weren't made for the party).
- v0.91.0: one Settings screen everywhere (Home, the game's page, the
  Game menu's Settings tab): board keys + comforts + the game's own.
- v0.90.0: Tom's questions on the Cloak of Elvenkind (95 in 100 is the
  originals' own code - answered) and the Flame Tongue: the Item Fixes
  switch (Pool's Flame Tongue as AD&D, files kept as the original's), the
  Flame Tongue's blows fire in both games; Settings switches in pages.
- v0.89.1: every item of both games tested (equip, the class / alignment
  / Strength rules, to hit, damage, AC, saves, stats, effects, use, fights;
  Curse 4069 checks, Pool 2738) plus the ITEMTEST characters themselves
  through Add Character (Curse 204, Pool 176 - each item readied alone,
  each usable one used, a fight with them all): fixed Pool's sword +3 vs
  undead and the Cloak of Elvenkind (both did nothing extra).
- 2026-10-12: Secret's files checked with the engine's readers: every DAX
  block decodes, 16 colours; the differences listed in the Project's
  claude/secret_facts.md.
- v0.89.0: Pool's items - its own item codes (generic effects, Ogre Power,
  the boulder, keyed swords), its item spells (0x17 higher: potions,
  wands, the javelin, the necklace now work), Enlarge / Strength / giant
  strength, Keoghtum's Ointment, the Manual of Bodily Health, the
  Spiritual Hammer's item, Strength / Charisma in Pool's files. The
  magic-user's spell pick at training was already compulsory (no Exit).
- 2026-10-12: README.md's board table has the S3 and C5 rows (after Tom's
  README edits); its Logs paragraph mentions Send Logs.
- v0.88.0: ESP32-S3 4.0" capacitive and ESP32-C5 2.8" resistive builds on
  the flasher (untested); Tom decided the C5 boards get a build.

- 2026-10-12: Tom decided the boards / games question (SPEC 5d); boards'
  facts in the Project (board_facts.md); the VGA games' own EGA modes
  checked (Darkness in DOSBox).
- 2026-10-12: DOSBox - the mountains in Pool's wilderness don't stop the
  party in the original (12 hours a step, as in the engine, which already
  matched): poolrad_overland_facts.md 7.4.
- 2026-10-12: the other games surveyed (Project: other_games_facts.md).
- v0.87.3: room for fighters joining mid-fight; Hit / Miss always sound.
- v0.87.2: flasks of oil burn.
- v0.87.0-v0.87.1: 4-bit screen (32 KB), lighter fights, heap budget.
- v0.86.0-v0.86.1: Send Logs, Logs in the Game menu, no Esc, memory recovery.

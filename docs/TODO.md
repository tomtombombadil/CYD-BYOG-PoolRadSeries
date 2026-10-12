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
- [ ] Flash a C5 board with "2.8" ST7789 Resistive (ESP32-C5)" (v0.88.0)
      and report the same (it asks for touch calibration at the first
      start).
- [ ] Send Logs goes to cyd.classic.games.logs@gmail.com (the Classic
      Games address) - keep it, or name another (one line in
      web/l/index.html).
- [ ] Keep play testing Pool of Radiance and Curse; report anything that
      plays differently from the originals. New in v0.89.0 for Pool:
      potions / wands / the Manual, Gauntlets of Ogre Power, the cloaks
      and rings - worth a look.

## Claude

- [ ] Memory, next cuts (measure each with heapprof): the party's unused
      member slots (1.7 KB each), Data's words read on demand, the spell
      names' 4 KB read buffer in pieces.
- [ ] S3 / C5 follow-ups once Tom has run them: fix what his reports show
      (colours, touch direction, sound level - the codec's volume is
      0 dB); then the 8-bit canvas for the VGA games there.
- [ ] Dark Queen on the classic boards: try it in its own EGA mode (Tom,
      2026-10-12) when we get to the Krynn games.
- [ ] Darkness's EGA mode: how the original's EGA.DRV turns VGA pictures
      into 16 colours (facts from the player's file), so Darkness plays on
      the classic boards as its own EGA mode shows it.
- [ ] QEMU: the cache-error panic inside play::open (parked).
- [ ] Before Secret: its files checked against Curse's formats (EGA,
      same DAX formats; sizes like Curse's - see the survey).

## Done

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

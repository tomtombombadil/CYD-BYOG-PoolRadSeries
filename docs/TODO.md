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
- [ ] When the 4.0" S3 board arrives: flash the S3 build from the web
      flasher (once it's there) and report what works - screen, touch with
      the capacitive stylus, card, sound.
- [ ] Say whether the 2.8" C5 boards should get a build too (they could:
      8 MB PSRAM, resistive touch, a speaker amplifier).
- [ ] Send Logs goes to cyd.classic.games.logs@gmail.com (the Classic
      Games address) - keep it, or name another (one line in
      web/l/index.html).
- [ ] Keep play testing Pool of Radiance and Curse; report anything that
      plays differently from the originals.

## Claude

- [ ] Pool items: the 0x80+ item effect codes (generic 0x80-0x82, 0x85,
      0x86, 0x88, 0x8A, 0x8B; Gauntlets of Ogre Power 0x83 = Str 18/00;
      0x87 the Giant Strength check) from GBC's Pool item list as facts;
      the Manual of Bodily Health (0x43); Pool's Strength values for
      Enlarge / Strength; the 0x3A poisoned case; the magic-user's spell
      pick at training made compulsory.
- [ ] Memory, next cuts (measure each with heapprof): the party's unused
      member slots (1.7 KB each), Data's words read on demand, the spell
      names' 4 KB read buffer in pieces.
- [ ] S3 support (4.0" ST7796 480x320 capacitive, ESP32-S3R8): board
      files and PlatformIO env, FT6336 touch, SDMMC 4-bit card, ES8311
      codec + amplifier (I2S) for sound, the big blocks in PSRAM, the
      canvas 8-bit where a game wants it; flasher entry (untested until
      Tom runs it). Before the board arrives.
- [ ] Darkness's EGA mode: how the original's EGA.DRV turns VGA pictures
      into 16 colours (facts from the player's file), so Darkness plays on
      the classic boards as its own EGA mode shows it.
- [ ] QEMU: the cache-error panic inside play::open (parked).
- [ ] Before Secret: its files checked against Curse's formats (EGA,
      same DAX formats; sizes like Curse's - see the survey).

## Done

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

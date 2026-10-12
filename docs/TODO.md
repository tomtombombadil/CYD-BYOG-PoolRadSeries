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
- [ ] Decide the board question for the VGA games (Gateway, Treasures,
      Darkness, Dark Queen, Unlimited Adventures): stay on the current
      ESP32 boards (EGA games only, or VGA shown in 16 colours), or add
      ESP32-S3 boards with PSRAM. Claude's survey: the Project's
      claude/other_games_facts.md.
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
- [ ] QEMU: the cache-error panic inside play::open (parked).
- [ ] Before Secret: its files checked against Curse's formats (EGA,
      same DAX formats; sizes like Curse's - see the survey).

## Done

- 2026-10-12: DOSBox - the mountains in Pool's wilderness don't stop the
  party in the original (12 hours a step, as in the engine, which already
  matched): poolrad_overland_facts.md 7.4.
- 2026-10-12: the other games surveyed (Project: other_games_facts.md).
- v0.87.3: room for fighters joining mid-fight; Hit / Miss always sound.
- v0.87.2: flasks of oil burn.
- v0.87.0-v0.87.1: 4-bit screen (32 KB), lighter fights, heap budget.
- v0.86.0-v0.86.1: Send Logs, Logs in the Game menu, no Esc, memory recovery.

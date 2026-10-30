# Open tasks (Tom and Claude)

Tom, 2026-10-12: "start keeping a list of tasks we need to do - both you
and me." Kept up to date with every push (CLAUDE.md); finished items go
to the bottom with their version, then drop off after a while. Synced to
the Project as claude/TODO.md.

## Tom

- [ ] The 5.0" boards (`TTB-CYD-GBGE_5.0in_Capacitive_ESP32-S3`, the
      FNK0115Q first; then the Hosyond - its back / box against the
      Freenove's): (1) the picture: 640x480 in the middle, black bars at
      the sides, colours right (red / green / blue blocks in Settings -
      if red and blue are swapped, Swap Red/Blue fixes it; say so and
      the board's default changes), no flicker or shifting (also while
      saving and in a fight); (2) touch: taps land where the stylus is,
      in both Rotate 180 settings, light taps seen, nothing from the
      bars; (3) the brightness slider dims the backlight (or only on /
      off?); (4) sound: Hit / Miss and the step sound, whole, and touch
      still fine while sounds play (the touch's INT pin is also the
      amplifier's LRCLK); (5) the serial line (115200, the CH340's port)
      shows "[lcd] whole screen doubled ... us" and "[touch] GT911 at
      0x.., 800 x 480" - send those two lines; (6) Send Logs after a
      fight.
- [ ] Flash the latest build and play a fight-heavy session on the 4.0"
      and the 3.2"; then Send Logs. What to look for: room left in fights
      (MEMORY.TXT), Hit / Miss sounds all through a fight, whether drawing
      feels slower with the 4-bit screen (v0.87.0).
- [ ] The 4.0" S3: does sound play (the codec, v0.103.1)? (Send Logs
      after a fight.)
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
      2026-10-12) when we get to the Krynn games. Decided (2026-10-24,
      branch darkqueen, queen_facts.md): its EGA install's pictures are
      the VGA ones through each picture's own EGA table, pixel for pixel
      (not dithered by the installer) - the engine reads the GOG VGA files
      and draws them in 16 colours itself (no EGA install, no scan step).
      Open: the 16 colours' RGB on the EGA screen for pictures whose
      colours start at 0 (TITLE's: their own 16?) - check in DOSBox.
- [ ] **Dark Queen of Krynn** phase 3+ (Claude; facts queen_facts.md +
      queen_facts_2.md, its section 12 the plan). Done in phase 2 (branch
      darkqueen2): DQK.EXE as a plain MZ (data segment file 0x63A20), the
      profile (being built), maps of any size, the 3D view / area map /
      frame from its files, GAME.FON's text, ECL.GLB scripts in the VM,
      QSV saves and its characters, Load from Death Knights - the Play
      Test (Dev Tools) walks the forest after Palanthas. Next: the
      menus as its buttons, the title, picture animation, the far band of
      the 3D view, items / effects / spells, View, fights, sound, 256
      colours on the PSRAM boards. Undecoded: picture kinds 0x03 and
      0x19, SOUNDS.GLB's DIG4, colour cycles.
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
      monsters, effects, most words). v0.108.0: its temple (free, its
      own words), the shops' Take / Appraise / coins words, Appraise's
      halved values. v0.107.0: View's menu (all games) and
      Secret's View panel. v0.106.0: its monsters' abilities
      (no Turn in the original). v0.105.0: the spells' words (fight
      and camp), Heal / Harm / the Elixir of Youth / the druids' spells,
      item words. v0.113.0: the exploring menu's Move. v0.114.0: Alter and its Level. v0.115.0: the vault. v0.115.1: item names from their words. v0.115.2: View's coin choice on its panel. v0.116.0: Delayed Blast Fireball's delay. v0.116.1: the version screen. v0.116.2: the fight's colours (compared with DOSBox). v0.116.3: the remorhaz's swallow. Next, a chunk a commit:
      a dwarf's saves: the original's training recomputes the saves from
      the tables (Epona, DOSBox) and effect 0x61 adds the Constitution
      bonus at the roll (as the engine), yet the GOG save's Malachite (a
      dwarf F/T) kept his lower saves through a training in DOSBox -
      unexplained (secret_facts_2.md 28); approaching monsters (SPRIT1:
      sprites drawn as Curse's, not compared with DOSBox; the sky checked
      at the vault's door, v0.116.1); more behaviour tests (sweep's
      `behave secret` has THAC0, saves, training's hit points, the Level,
      the vault, the spells past 100, the cockatrice / remorhaz / iron
      golem - 239 checks, 0 failed, 2026-10-17; more of its monsters'
      abilities to add). v0.117.0: playable (Tom, 2026-10-18: Play on
      its game page). Done: the full checks - monsters (v0.120.1),
      spells (v0.121.1), items and situations (v0.124.0): 5,412 checks,
      0 failed; the ITEMTEST characters sent to Tom (2026-10-18).
      Still open: scroll bundles (Join on scrolls - not in the engine);
      Buy / Id prices, Share / Take / Pool, Look, the training hall's
      screens, the whole party dying, importing Pool (.WHO) characters -
      not checked yet; the unsure points in secret_facts_2.md 29.items.3
      / 29.situations.0b.
- [ ] **Pools of Darkness** (started 2026-10-17; facts: the Project's
      claude/darkness_facts.md; SPEC 6d). Done: the survey (program and
      overlay layout, files, saves, the scripts' operand counts);
      v0.118.0 the profile (Play off), scripts' counts, the title in its
      EGA colours. v0.119.0-v0.122.0: record, saves, party menu,
      Load, the 3D view and frame, the scripts' memory, Begin into
      Phlan. v0.123.0: event pictures (PIC1 animations, BIGPIC).
      v0.125.0: exploring menu, camp, View, item names. v0.126.0: the
      items screen. v0.127.0: the rule tables. v0.128.0: magic (camp
      screens). v0.129.0: sounds (ZOUNDS samples, SOUND op; Tandy = the
      SL / TL DAC, PC speaker as IBMSND.COM). v0.130.0: training (not
      compared with DOSBox: the hall's screen). v0.131.0: Secret's
      characters (Load / Add; Malachite's HP: done with creation).
      v0.132.0: approaching monsters (SPRIT1; compare with DOSBox when
      one is met).
      v0.133.0: the overland (matches DOSBox).
      v0.135.0: shops, temple, treasure, vault (open: the treasure-only
      result screen, Restoration's cheapest class, Remove Curse's words,
      Vault Items' way back, Raise Dead's stats recalculation).
      v0.136.2: the icon gallery shows Darkness's parts (VGA CHEAD /
      CBODY with their see-through plane). v0.137.0: camp Alter (Order,
      Drop, Speed, Level) and Darkness's own icon editor (camp, creation,
      the gallery's Edit) - matches DOSBox. Open: the party menu's Drop
      Character should ask "Pick Character Select" first (the same
      routine in the original); Darkness keeps the menu highlight's place
      in every menu - the engine does it in Alter / the editor only.
      v0.141.0: PLAYABLE (Play on its page; Tom, 2026-10-23: "finish
      Pools of Darkness") - its own level drain, camp spells, the gazes'
      pictures, Kalistes' weapon, the beholder's pick, menu highlights
      everywhere, the party menu's Drop pick, the overland fight's field
      (its own outdoor builder), Game Fixes: Power Word Kill, dragon fear,
      the bulette's leap, Remove Curse's slip, Resurrection's Con. Open:
      Protection from Fire in fights a plain effect; Secret's
      camp picture while asking whom (not checked).
      v0.136.0: fights. v0.138.0: the missing rules - Turn Undead's
      table, the 6th-9th level spells (Monster Summoning from a reserve
      of 10), dragon fear, Otto's, Kalistes' weapons, vorpal, blessed
      bolts, the gazes, beholders' rays, the burst sound, the Aim / Speed
      words (as DOSBox); monster records shared per group (Darkness's
      budget fight 145 KB). Open: the arena's third south ogre (31,19)
      against the original's (33,18) - its rank widths (DS 0xA7CE / 0xA7CF)
      are left over from an earlier count (facts R17); Darkness's own
      level drain (the engine uses Pool's with Darkness's words); the
      gazes' pictures (COMSPR 0x2D / 0x32); Kalistes' weapon on row 23;
      the beholder's ray target (the original's picker, ovr 0x23130); an
      Energy Drain check.
- [ ] Darkness 256 colours on the S3 / C5 (merged, branch darkness-vga):
      matches DOSBox's VGA mode. Open: the overland arena fight (jumper ->
      Arena -> Overland -> Prime, river No, monster 10 x 1) gives an
      all-river field with no monster and an empty frame after (EGA
      too; DOSBox: grass, the monster, the map back); the VGA fight's 40
      field tiles (23 KB) in internal RAM - one block would go to PSRAM;
      a Settings switch for EGA on the S3 / C5 (Claude: not now - Tom's
      rule is 256 colours there).
- [x] Tom's tests 2026-10-24 (v0.145.1 / v0.146.0): the C5's sound line
      stays at the middle (v0.145.1 - Tom to listen again); Gateway's
      ready-made characters (the SNEG copy's 12 .GUY; GAME.CFG's first
      folder line; no saved game ships with either release), Modify on new
      characters, the icon gallery's first page, Sling / Magic Hit sounds
      (the original plays the Miss sample - Game Fixes: the magic and
      missile sounds), the journal's Introduction (Gateway and Treasures,
      entry 'I' 1 - Rescan needed); a Loading screen on Play; one menu
      highlight rule for every game (white box, first word, the place kept
      after Select - SPEC 12); Treasures' Valhala Inn (a script CALL's
      operand - Darkness's Rolf tour fixed by it too). Tandy / PC Speaker
      in the recorded-sample games: only grain differs (told Tom).
- [x] Tom (2026-10-24): Gateway's Modify drew the changing number on
      Curse's rows - Gateway's View layout now (v0.146.1).
- [x] Slow game starts (v0.148.0): each program unpacked once into
      _CYD/<folder>/PROGRAM.BIN (by the scan, or the first Play on a card
      scanned before); QEMU: Play to title 6-35 s -> about 1 s.
- [ ] Tom: time a game's start on a board after Rescan (QEMU says ~1 s).
- [ ] Tom: Gateway test characters (gateway_itemtest.zip, 2026-10-24):
      into Gateway's SAVE folder, then Add Character -> ITEMTST1-3.
- [ ] Champions of Krynn (v0.147.0, SPEC 6g): Play on, in testing - plays
      end to end in the playsim; moons, gods, knights (petitions, Change
      Classes), kender Yell, draconian deaths (bozak, baaz), creation,
      Modify, the overland; matches DOSBox (title, party menu, View, the
      start); behave champions; budget fight 129.8 KB. Game Fixes: thief
      skills by race (the original reads Curse's race rows), a knight's
      saves (the original gives every knight row 7's level-0 saves).
      v0.149.0: its GOG journal table (88 entries, 57 tavern tales - the
      PDF reader now takes cross-reference / object streams; Rescan
      needed), the kapak's acid pool, "WHERE DO YOU WISH TO GO?" kept in
      shops / temple, its sound effects (the SSI driver in START.EXE, as
      Secret's). Open: the title music (SOUND.DAX / MIDI.DAX, not played);
      an in-game effect not yet compared with DOSBox.
- [x] v0.151.0: the journal viewer (Column / Whole, Turn for maps,
      pieces at their own widths - Treasures' introduction; the book a
      column at a time; grey print to black, paper threshold 232, cubic
      scaling; GOG map entries from the Companion's JE pictures when
      there; a Rescan remakes JOURNAL.DAT) and the party menu box
      (v0.150.0) from the list below. Tom: check Treasures' map J1.
- [ ] Death Knights (Play on, v0.153.0): the GOG journal table (v0.154.1:
      65 entries, 13 not in the book, maps 37 / 44); the playsim crashed
      started at JOURNAL instructions in areas 3 / 5 (RUNAT into blocks
      67 / 82 / 96 / 97) - check it isn't the engine; events block 16 (the intro) - a longer run or the sweep not
      replaying its fight per square; open: DK temple healing assumed free,
      effect 0x44 (feeblemind and drain?), Sivak morale / fleeing. Game
      Fixes: DK thief skills by race (each race its own row).
- [x] v0.154.2: the Krynn games' race adjustments at creation - the
      originals never apply them (the code tests a loop counter left at
      Human; DOSBox: Champions' kender Dex unchanged): Game Fixes on only;
      the young-Wisdom -1 (Secret's rule) now in Champions / Death Knights.
- [ ] Dark Queen (Play still off - Dev Tools; branch darkqueen4,
      queen_facts_4.md 9-12): in since v0.157.0 - the fights' two rows of
      buttons / status / Aim / Move's Undo Done, names' colours, flags /
      flags 2, monster xp (record 0x54 - checked), sound (SOUNDS.GLB), 256
      colours on S3 / C5, animation (0.5 s), Inv, Modify, `behave queen`
      (creation, train, knights, spells, monsters). Str00 test 50 confirmed.
      Plays from the start through Caergoth, the dragons, the Silver Shark
      and the cave's draconians. Left: the Taladas overland (ECL 5, map 2 -
      a step there does nothing), its own icon editor (Alter Icon, the
      creation's icon), Modify without the items' figures, the camp's cast
      message place. Then Play on.
- [x] 5.0" 800x480 IPS capacitive S3 boards (Tom: a Hosyond and a
      Freenove FNK0115Q, 2026-10-30): board file built (branch board5in,
      env `cyd50s3_cap`, `src/boards/esp32s3_50_rgb.hpp`; untested -
      Tom's checks under Tom). RGB panel (LovyanGFX Bus_RGB, 13 MHz, frame
      buffer in PSRAM) with the 320x240 layout at 2x, centred (640x480,
      black bars); GT911 polled without INT; SD over SPI (FSPI); NS4168
      by I2S (`BOARD_AUDIO_I2S`); serial on UART0 (CH340C). QEMU:
      `qemus3_50` (2x copies checked: 0 bad pixels through Home, Settings,
      Rotate 180, Swap Red/Blue, Invert, Pool's Play). Facts:
      board_5inch_facts_2.md (scratch -> Project).
- [ ] Secret J45's piece takes in the "Journal Entry 47" heading (the
      map overlaps it; a rectangle can't separate them).
- [ ] Check: pdfview's State (FileSource cache, >4 KB) lands in PSRAM on
      the S3 - its card reads must go through the internal bounce.
- [ ] Tom's tests 2026-10-24 (12:14): Treasures' party menu shows no
      highlighted character (View then shows whom?); the C5's sound still
      distorted, harsh, like static (v0.145.1's fix didn't cure it); the
      Treasures introduction entry in two pictures - the full-width first
      paragraph, then the rest ("Dear Friends...") at one column's width;
      the journal viewer for ALL games: scans are whole facing pages - cut
      into columns (two a page), default one column wide, Whole for maps,
      no "Full Size", a 90-degree turn for map entries filling the screen;
      Treasures' first map illegible - clean it up (Tom: replace its text
      with readable font text - not allowed: the journal's text can't be
      shipped; cleaned-up picture / the Companion's own pictures instead);
      Settings' Volume as one key: tap left -, right +, 5% steps, the small
      "Volume" label, the percentage between - and +, a mute symbol at 0.
- [x] v0.149.1 (Tom, 2026-10-24 12:41: C5 sound fixed in every playable
      game): the C5's Curse / Secret sounds were also distorted - the
      output (PDM up-sampling / quarter scale).
- [ ] Tom (2026-10-24 12:57): review in testing - the exploring / camp
      panels keep the originals' white name for the selected member (the
      party menu has the white box since v0.150.0); box them too?
- [ ] Tom (2026-10-24, 12:21; 12:34 Settings: one Game Sound key naming
      the mode, tap to change - Tandy / PC Speaker / Darkness / Gateway /
      Treasures by what's on the card; in those three games just the game;
      Volume -/+ wider, half-width Mute toggle - done v0.150.0; the game
      modes play Tandy until the recordings are wired). Tom's idea:
      the later games' better sounds on the early games - Claude's plan: a
      Game Sound choice "Recorded" that plays the player's own Pools of
      Darkness (or Treasures / Gateway) ZOUNDS.DAX samples for Curse, Pool,
      Secret and Champions' effects, when one of those games is on the card
      (effect numbers already mapped as Darkness's; the player's files read
      at run time, nothing shipped). Built (branch rec-sound): the three
      modes play the recordings (`Clips::lent`, recorded_sound_facts.md).
      Tom to listen: Curse / Pool / Secret / Champions in each mode - the
      step (Darkness's pad 0.2 s, Gateway's / Treasures' a 16 ms tick),
      hit, miss, sling (the arrow), magic hit (the stars' sound), death;
      the title and anything else stays Tandy.
- [ ] Krynn (survey 2026-10-24, the Project's claude/krynn_facts.md):
      Champions = Secret's engine (port started, branch champions); Death
      Knights = Darkness's engine at 4 bits (LOAD FILES / PIECES 3
      operands, a 216 + 220-byte record); Dark Queen = a new C program
      with HLIB .TLB / .GLB libraries, only the scripts (Darkness's ECL)
      shared - a near-new port. Dark Queen on classic boards: chosen
      2026-10-24 (above: the VGA files drawn in 16 colours through their
      own EGA tables). Also: the Krynn games' sound is an EA driver
      (SOUND.DAX / MIDI.DAX), not decoded yet.
- [ ] Gateway to the Savage Frontier (v0.144.0, SPEC 6e): PLAYABLE (Play
      on) - matches DOSBox through Yartar, the wilderness, the vault, shops,
      fights; its GOG journal table (v0.142.0). behave gateway is light
      (creation 1084 cases, 48 menu states) - Claude: a deeper behaviour
      pass (spells, monsters, items, as Curse's sweeps). Open: music
      (MUSIC.DAX, sounds 0x80 up - no game plays music yet); the
      wilderness start selects ARNOLD where the original has MERL.
- [ ] Treasures of the Savage Frontier: PLAYABLE since v0.145.0 (Play
      on) - the 256-colour parchment, the clock's years and old age, the
      journal (83 entries; the PDF reader takes several pictures a page),
      the camp picture, LUCK texts as DOSBox; late-game screens checked
      in DOSBox from edited saves. Game Fixes: everyone ages a year each
      game year (the originals - Darkness too - age no one until year 100).
      Open: the VGA colour cycling of PIC 130's crown sparkles (no cycle
      facts yet); the post-fight old-age texts in DOSBox.
- [x] Journal tables (v0.142.0): Secret 72 entries, Darkness 99 (its
      book has no entry 13), Gateway 62 - checked by eye; Darkness's
      mentions now offered at a key wait too. Small leftovers: neighbouring
      text at the edges of four maps (Secret 45, Darkness 36 / 38, Gateway
      7). Open: Treasures' GOG PDF (72 dpi, several pictures a page) needs
      the PDF reader to take more than one image a page first.
- [x] Darkness's fight memory (v0.136.1): the 30-monster test fight 169
      -> 150.6 KB (Curse 146 -> 132, Pool 147 -> 133): the script's code
      parked during fights and its memory the fight's arena, the monster
      groups made as needed, the spell names packed, smaller fighters,
      Darkness's background picture freed. Only 3 KB spare: next cut the
      monster records (~540 bytes each, up to 48) - a group's copies
      sharing one, or growable effects (108 bytes each).
- [ ] Darkness's sounds, with its fights: the area spells' burst plays
      effect 11 again as their picture shows (spells 0x22, 0x2F, 0x40,
      0x57, 0x5B, 0x73 - GAME.OVR 0x29D34 +0x1451; and an item's burst,
      +0x5FB8); a ranged attack's one sound: 12 for the item types 4, 5,
      7, 0x0C, 0x1E, 0x42, 0x4A, else 9 (+0x2800 of GAME.OVR 0x1E642 -
      check Darkness's shot kinds give that). darkness_sound_facts.md 4.
- [x] Tom (2026-10-23): yes - Darkness's race adjustments and the
      cleric's experience bonus from Wisdom 16 behind Game Fixes.
- [x] Tom (2026-10-23), Darkness's fights: Power Word Blind's worked-out
      time behind Game Fixes - yes; Energy Drain stays one level (no
      switch); Power Word Kill and Resurrection as AD&D behind Game Fixes;
      dragon fear ends with the fight behind Game Fixes. (Claude: the
      last three on branch darkness-leftovers.)
- [x] Tom (2026-10-23): the bulette's leap - the original's way (never
      happens) in the game, the working leap behind Game Fixes; the same
      answer for every future question of this kind (CLAUDE.md).
- [x] Darkness's sounds (Tom, 2026-10-23): Claude's recommendation -
      two choices, Tandy = the recordings as they are (the SL / TL DAC,
      the GOG Sound Blaster), and the PC speaker; no EX / TX chip choice
      (it differs only by a little grain). The game goes on while a
      sound plays (Tom OK, 2026-10-19).
- [ ] Tom: flash v0.104.0 on the 4.0" S3: the touch (keys now light as
      the stylus touches; light taps after a pause, finger and stylus;
      small keys) and the sounds (Settings' sound test - Step, Hit; Pool's
      steps and fights). Also on a resistive board: keys light on touch
      there too. Sounds: Step and the others work now (Tom, 2026-10-16) -
      still to say: any hum or hiss between sounds on the 3.5" now that
      its amplifier stays on? If taps still land off
      the mark, Settings - Recalibrate Touch (new on this board: tap
      each arrow tip) and say whether it helped. Then the yellow tint:
      the display's set-up is the same as the maker's (same gamma and
      voltages), so please try Settings - Brightness all the way up, and
      say which firmware looked right (Freenove's demos?) - the tint may
      be the backlight at 78% (our default) or the engine's own colours.
- [ ] Claude: decode any crash line in Tom's S3 logs (CI's
      elf-cyd40s3_st7796_cap for that build, xtensa-esp32s3-elf-addr2line).
- [ ] Claude: watch "stack left" in Tom's classic-board memory logs (QEMU:
      1.5 KB at a fight's start); slim the deepest frames if it drops
      under ~1 KB (fight_load_monster 1.2 KB, draw_overland 1.4 KB,
      load_party_text 1.3 KB, save_game 1.1 KB).
- [ ] Tom: v0.104.2 - Pool's walls round Phlan's gateway / the
      temples, and Center in a fight (Aim, Manual, an empty square).
- [ ] Tom: Secret test characters (2026-10-18, secret_itemtest.zip):
      its files into Secret's folder on the card beside SAVGAMA.DAT
      (README inside), then Add Character -> Secret -> ITEMTEST1 ... 6
      (every magic item of the game between them); say what looks wrong.
- [x] Tom (2026-10-23): the switch is **Game Fixes** (was Game Fixes) -
      the originals' behaviour in the game, the engine's fixes behind it
      (v0.139.0).
- [ ] Claude: Secret's Barkskin - the original runs Heal's routine for it
      (the AC effect AND nearly full hit points). Tom's rule (2026-10-23:
      "Keep original game behaviors in the game and keep our fixes under
      the ... toggle"): fix it (AC only) behind Game Fixes.
- [ ] Tom: try the new library page (v0.103.0) on a board - the art,
      the header keys, the thirds and the triangles.
- [ ] Tom: when you have a moment, Secret's Screen Test and Walk Test
      (Settings - Dev Tools on its page) on a board - the frame, title,
      credits and maps should look like the original's.

## Done

- v0.116.4: Secret's temple Raise Dead checked in DOSBox (Con - 1, own
  and in use; the maximum HP kept) and made so.
- v0.112.0: Secret's sound effects (Tandy / PC speaker) from its program's
  driver, as Curse's.

- v0.111.0: Secret's Create New Character (its words, races, rolls, 200,000
  experience, silent training's spells) and Modify (Add / Subtract).

- v0.110.0: Secret's Add Character - its own characters and Curse's
  (without items or money, as the original takes them in).

- v0.109.0: Secret's spells past 100 - Death Spell, Disintegrate, Globe,
  Stone to Flesh, Flesh to Stone, Delayed Blast Fireball, Mass
  Invisibility, Power Word Stun; its monsters' magic resistance.

- v0.108.1: Secret's training - free, its own hit dice, demihuman
  clerics' limits; the temple's list with the shop's bar.

- v0.108.0: Secret's temple - free, each cure at once with its own words
  ("can see!", "is ressurected" ...); its shops' Take / Appraise words,
  Appraise's values (Curse's halved), the priest's / shopkeeper's coins
  words.

- v0.107.0: View Character's menu in every game - Spells, Trade, Drop,
  the paladin's Heal and Cure (they weren't in the engine); Secret's View
  shows its money and the readied weapon / armour.

- v0.106.0: Secret's monsters - iron golem, cockatrice, remorhaz,
  pyrohydra, umber hulk, the dragons' breaths, the giant slug's spit, the
  dreadlord's immunities.

- v0.105.0: Secret's spells say its own words ("is Blessed", "falls
  Asleep", "is Hastened" ...) in fights and camp; its Heal, Harm, Elixir
  of Youth and the druids' Charm Person or Mammal / Cure Light Wounds;
  its item words (Use ... Select).

- v0.104.2: Pool's area 3 walls (the "unsettled areas" round Phlan,
  the Temple of Tyr) from their own tiles - they were drawn from other
  areas' tiles since v0.85.2 (Tom: "made up of tiny icons"); Center in
  fights centres the view on what's aimed at, Manual's empty squares too,
  and the Manual square keeps the view with it (Tom).

- v0.104.1: the ESP32-32E boards' amplifier stays on from Play's start
  (its ~100 ms wake-up ate Step and Hit on Tom's 3.5").

- v0.104.0: keys and game-screen words light up as the stylus touches,
  not as it lifts (Tom); the tap still acts on the lift.

- v0.103.6: the S3's touch kept in its active mode; its sound output and
  amplifier stay awake (the amplifier's 100 ms wake-up ate short sounds);
  every board's amplifier off only after the sound's end has played.

- v0.103.5: the S3's touch read by a task of its own every 10 ms (quick
  light taps were missed while the loop worked); Recalibrate Touch on
  the S3 too.

- v0.103.4: the S3's touch polled every time (its INT pin no longer
  gates it) and reset as the maker does; a capacitive press starts on
  its first reading; the S3 marked tested (Tom: Pool, Curse, Secret
  play on it).

- v0.103.3: the crash at Play on the S3 (and Secret's): loop()'s stack
  ran out - play::open split into steps (the classic boards had under
  200 bytes to spare there too; now 2.1 KB), 16 KB of loop stack on the
  S3 / C5; the memory log shows the stack left.

- v0.103.2: the library's pictures only from the files the scan made
  (Tom) - a start without them for this screen makes the missing ones
  first; the page never decodes.

- v0.103.1: S3 fixes from Tom's first run - the codec set up from the
  main task, the card's DMA kept out of the PSRAM, crash lines in the
  Restarts log; the library page's triangles come first; the picture
  files' problems noted in the Memory log; the S3 in QEMU (`qemus3`).

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
  originals' own code - answered) and the Flame Tongue: the Game Fixes
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

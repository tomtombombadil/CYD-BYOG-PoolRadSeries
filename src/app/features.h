// Engine comforts beyond the original games - each one a switch, so a port
// to another platform (bigger screens, PortMaster ...) can drop it with a
// build flag, e.g. -DCYD_BIG_ICON_PREVIEW=0 in its build_flags.
#pragma once

// The icon editor's big preview (Tom, 2026-10-10, v0.59.0): the combat
// icons are 24 x 24 pixels, too small to judge on these 2.8" - 4.0"
// screens, so the editor (Alter -> Icon, Create New Character) also shows
// the NEW icon as large as fits - on 480x320 always, in the Companion
// strip; on 320x240 when Settings' Large Icon is on, in
// the editor's empty right side. A tap on it flips ready / action.
// Where: viewer.cpp (big_icon_* / draw_big_icon, the Settings key),
// play.cpp (play::icon_preview), settings.h (large_icons).
// Turn it off for screens where the game shows at a readable size.
#ifndef CYD_BIG_ICON_PREVIEW
#define CYD_BIG_ICON_PREVIEW 1
#endif

// The demo stops on a tap (Tom, 2026-10-10, v0.64.0): the title's Demo
// runs the game's own demo; in the original a key doesn't stop it (it runs
// until the demo party wins or falls). Here any tap or key ends it and the
// title comes back. 0: as the original (input is ignored while it runs).
// Where: play.cpp (demo_input / stop_demo).
#ifndef CYD_DEMO_TAP_STOPS
#define CYD_DEMO_TAP_STOPS 1
#endif

// The icon gallery (Tom, 2026-10-11, v0.71.0): the icon editor (Alter ->
// Icon, Create New Character) opens on pages of ready-made icons to tap -
// whole icons, then every head, every body / weapon and colour schemes,
// each shown on the character - instead of stepping through values one at
// a time with Next / Prev. Edit goes on to the original editor (its Exit
// comes back here); Done asks the original's "Is this icon ok?" (No: back
// here). The pictures are the player's own CHEAD / CBODY parts; the schemes
// and combinations are the engine's (engine/icon_looks.*).
// v0.74.0 (Tom): with the player's Gold Box Companion on the card
// (/GOLDBOX/GBC, its GOG release's DAX-Icons-EGA), its icon pairs follow as
// more pages; a pick is kept by the character's name (_CYD/<folder>/
// ICONS.TXT) and drawn in fights instead of the head / body icon. Never
// shipped: the Companion is its author's work (engine/gbc_icons.*).
// Where: play.cpp (gal_*), viewer.cpp (no 320x240 big preview over it).
// 0: the original editor only.
#ifndef CYD_ICON_GALLERY
#define CYD_ICON_GALLERY 1
#endif

// More characters to add (Tom, v0.56.0 / v0.58.0): Add Character also lists
// the members of saved games ("NAME from saved game A") and, for Pool of
// Radiance, the player's Pool of Radiance folder on the card; the original
// lists only its save folder's files. Where: play.cpp (find_guys).
#ifndef CYD_MORE_CHARACTERS
#define CYD_MORE_CHARACTERS 1
#endif

// Journal entries shown (Tom, 2026-10-10): when the game says "read entry
// N", the next tap opens the entry from the player's own journal (the
// original only prints the number). Where: viewer.cpp (play_journal).
#ifndef CYD_JOURNAL_POPUP
#define CYD_JOURNAL_POPUP 1
#endif

// The Clear font (Tom, 2026-10-11, v0.73.0): the engine's own upright, bold
// letters with real lower case (tools/fonts/clear8.txt, drawn for the
// engine) in place of the game's narrow slanted capitals - much easier to
// read on 2.8" - 4.0" screens. The game's symbols and frame pieces stay.
// Off unless the player picks it (Tom); switchable mid-game (Game menu ->
// Options), the text on screen changing with it.
// v0.74.0 (Tom): the switch is the Game Font key - Original / Clear /
// Darkness, the last the player's own Pools of Darkness font (8X8D0.DAX in
// their Pools of Darkness folder; offered only when it's on the card).
// Where: engine/font.* (set_letters), play.cpp (font_wanted), viewer.cpp,
// settings.h (game_font).
#ifndef CYD_CLEAR_FONT
#define CYD_CLEAR_FONT 1
#endif

// v0.84.0 (Tom): Clear Caps, a fourth choice - the Clear font's capitals,
// digits and punctuation without its lower case (all capitals, as the
// games print).

// The icon portrait (Tom, 2026-10-11, v0.84.0): the games have no
// character portraits, but View Character leaves room for one (Pool of
// Radiance draws its head / body portrait there, top right). The engine
// shows the character's combat icon three times its size in that space,
// inside a frame of the game's own tiles (the 3D view's frame; Pool's
// box). Where the game has a portrait (Pool: every character, NPCs too)
// the portrait comes first and a tap on it swaps to the icon and back;
// otherwise a tap flips the icon's ready / action pose. 0: as the
// originals (Pool's portrait only; nothing in Curse's space).
// Where: play.cpp (view_box), engine/layout.* (picture_frame).
#ifndef CYD_ICON_PORTRAIT
#define CYD_ICON_PORTRAIT 1
#endif

// The originals' item mistakes fixed (Tom, 2026-10-12, v0.90.0): Pool of
// Radiance's "Long Sword +2 Flame Tongue" has no flame tongue in its record
// (0x3D 0x16 - its Use casts Find Traps - and no effect code), so it is a
// plain +2 sword whose blows aren't fire: a troll it fells gets back up.
// Fixed it is the AD&D Flame Tongue, as Curse's: a Long Sword +1 Flame
// Tongue, +2 against regenerating creatures, +3 against cold-using / avian
// ones, +4 against undead, its blows fire. The files keep the original's
// bytes (the fix is made as characters are read, items taken, the switch
// turned). 0: as the original. Where: engine/items.* (fix_item),
// rules.cpp (add_item, fix_items), play.cpp (character_read, set_options,
// write_character_files), profile.cpp (Pool's items.flame).
// The Settings key is "Game Fixes" (Tom, 2026-10-23; "Item Fixes" until
// v0.139.0): every fix of an original game's mistake - items and rules -
// with the originals' behaviour when off.
#ifndef CYD_ITEM_FIXES
#define CYD_ITEM_FIXES 1
#endif

// The menu highlight rule (Tom, 2026-10-24: "My preferences about the
// highlight should be on ALL games"): one rule for every game's menu line -
// (1) the highlight is a white box round the whole word (Treasures draws
// white letters, its words' first letters white anyway, so one-letter
// choices couldn't be told apart; its lists too); (2) a menu starts on its
// first word (Darkness / Treasures carried the last menu's place into the
// next - past a short menu's end that was its last word); questions keep
// the word their game starts them on (No; Darkness's / Gateway's Yes);
// (3) after a pick the highlight stays there: the same menu or question
// shown again (a reroll, a menu back after the one it opened) starts on
// the word picked last. Built in (no Settings key: it changes no play).
// 0: as the originals. Where: play.cpp (hl_apply, hl_remember, hl_plain,
// hl_inherit).
#ifndef CYD_MENU_HIGHLIGHT
#define CYD_MENU_HIGHLIGHT 1
#endif

// Each comfort compiled in can also be switched off by the player: the
// game's Settings page (Tom, 2026-10-11, v0.72.0; one set for all games,
// settings.h comforts_off), the one Settings screen (Home, a game's page, the
// in-game Game menu's Settings tab - v0.91.0).

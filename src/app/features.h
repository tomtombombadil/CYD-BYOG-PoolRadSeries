// Engine comforts beyond the original games - each one a switch, so a port
// to another platform (bigger screens, PortMaster ...) can drop it with a
// build flag, e.g. -DCYD_BIG_ICON_PREVIEW=0 in its build_flags.
#pragma once

// The icon editor's big preview (Tom, 2026-10-10, v0.59.0): the combat
// icons are 24 x 24 pixels, too small to judge on these 2.8" - 4.0"
// screens, so the editor (Alter -> Icon, Create New Character) also shows
// the NEW icon as large as fits - on 480x320 always, in the Companion
// strip; on 320x240 when the Game menu's Options -> Large Icons is on, in
// the editor's empty right side. A tap on it flips ready / action.
// Where: viewer.cpp (big_icon_* / draw_big_icon, the Options tab's key),
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
// Where: play.cpp (gal_*), viewer.cpp (no 320x240 big preview over it).
// 0: the original editor only.
#ifndef CYD_ICON_GALLERY
#define CYD_ICON_GALLERY 1
#endif

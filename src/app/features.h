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

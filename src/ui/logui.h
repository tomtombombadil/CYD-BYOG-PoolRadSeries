// The engine's logs (Tom, 2026-10-09): the card scan's log, the restarts
// noted and the errors shown, in a scrolling box (ui/logview). Also shown
// when a card scan ends, with Continue. To get them off the board: Send
// Logs (header, right) shows them as a QR code a phone opens as a page
// that emails them (ui/sendlog, v0.86.0), or take the card to a PC:
// they're text files in /GOLDBOX/_CYD/.
// (WiFi and emailing the logs were tried in v0.21.0 and taken out in
// v0.22.0 - Tom: the games need the memory.)
#pragma once

#include "ui.h"

namespace logui {

// after_scan: the card scan's log, with Continue (no other logs);
// fallback: the text shown when the log isn't on the card (no card)
void open(bool after_scan, const char* fallback = nullptr);
// From the Game menu's Logs tab (Tom, 2026-10-12): leaving goes back there
void open_from_game();
bool from_game();

void draw();
// False when the screen is left (back to Settings, or on from the scan)
bool tap(const ui::Tap& t);
void tick();
bool from_scan();          // opened by the card scan (leaving goes on to the library)

} // namespace logui

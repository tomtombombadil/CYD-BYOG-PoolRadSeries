// The engine's log and network screens (Tom, 2026-10-09):
//   Logs        - the card scan's log and the restarts noted, scrolling
//                 (also shown when a card scan ends, with Continue);
//   Email Logs  - sends them to the engine's log address from the
//                 player's own mail account;
//   WiFi        - choose a network and type its password.
// The viewer hands these screens their taps and draws while they're open.
#pragma once

#include "ui.h"

namespace netui {

void begin(const char* version, const char* build);

// after_scan: the card scan's log, with Continue (no other logs, no mail);
// fallback: the text shown when the log isn't on the card (no card)
void open_logs(bool after_scan, const char* fallback = nullptr);
void open_wifi();

void draw();
// False when the screens are left (back to Settings, or on from the scan)
bool tap(const ui::Tap& t);
void tick();
bool from_scan();          // opened by the card scan (leaving goes on to the library)

} // namespace netui

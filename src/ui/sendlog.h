// Send Logs (Tom, 2026-10-12: "a quick and easy way to send logs", as in
// CYD-Classic-Games): Settings -> Logs -> Send Logs packs the newest lines
// of the logs (Errors, Memory, the last session's Memory, Restarts, Card
// Scan) with a short header (board, firmware, memory now) into one QR
// code. A phone camera opens it as a page (web/l/index.html) that shows
// the text and emails it to the developer. engine/log_pack.* packs it;
// engine/third_party/qrcodegen.* (Nayuki, MIT) draws the code. Everything
// it needs is on the heap only while its screen is up.
#pragma once

#include "ui.h"

namespace sendlog {

// The firmware's version and build, for the header (at start-up)
void set_build(const char* version, const char* build);

// Packs the logs and shows the code (a message first: it takes a moment)
void open();
void close();
bool is_open();
void draw();
// False when Back was tapped (the screen closes itself)
bool tap(const ui::Tap& t);

} // namespace sendlog

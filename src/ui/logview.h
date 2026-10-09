// A text file shown in a scrolling box (Tom, 2026-10-09): the card scan's
// list once it's done, the logs in Settings. A tap in the top half of the
// box goes up a page, in the bottom half down a page; dragging scrolls it.
// Long lines wrap. Files longer than kMaxBytes show their end.
#pragma once

#include <cstddef>

#include "ui.h"

namespace logview {

constexpr size_t kMaxBytes = 32 * 1024;

// Loads a file from the SD card; false when it isn't there (the box then
// says so). at_end: start scrolled to the end.
bool open_file(const char* path, bool at_end);
// Shows text from memory instead
void open_text(const char* text, bool at_end);
void close();

// Where the box goes (call before draw)
void set_area(const ui::Rect& r);
void draw();
// A tap in the box: true if it was one (it scrolled)
bool tap(const ui::Tap& t);
// Call every loop: follows drags (the screen allows them: ui::allow_drag)
void tick();

} // namespace logview

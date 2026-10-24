// The journal PDF as a book (Tom, 2026-10-09: the fallback for a PDF the
// engine has no table for, or when its entries aren't prepared). Each page
// is a scanned picture (JPEG) - mostly two facing book pages of two
// columns each - shown a column at a time (Tom, 2026-10-24: "divide them
// into columns, two per page, just like the text. The default view would
// be one column wide"), the column's width across the screen, or the
// whole page. The columns are found on the board, by the white gutters
// between them (journal::find_columns).
#pragma once

#include "ui.h"

namespace pdfview {

// Opens the PDF (path on the SD card). False if it can't be read as a book
// of scanned pages.
bool open(const char* path);
void close();
bool is_open();

int pages();

// Column: one column across the screen (scrolling down it); Whole: the
// whole page
enum class Fit : unsigned char { Column, Whole };

enum class Result : unsigned char { Ok, NoPicture, NoMemory, BadData };

constexpr int kLastColumn = -1;            // View::col: the page's last column
constexpr int kBottom = 1 << 30;           // View::vy: the column's bottom

struct View {
    // in: which column and how far down it (shown pixels); at_x / at_y
    // (scan pixels, at_x >= 0): the column there instead, with that spot
    // in the middle (a tap on the whole page)
    int col = 0, vy = 0;
    int at_x = -1, at_y = -1;
    // out: the page's columns, the column's shown height, where the
    // picture landed on the panel, the page's size (scan pixels)
    int cols = 0, shown_h = 0;
    ui::Rect fit{0, 0, 0, 0};
    int page_w = 0, page_h = 0;
};

// Draws page `page` (from 1) into r. Column: finds the page's columns
// first (once a page) and keeps v.col / v.vy in range. Deep: run it on a
// big stack. No big buffers: rows go to the panel as they're made.
Result draw(int page, const ui::Rect& r, Fit fit, View& v);

} // namespace pdfview

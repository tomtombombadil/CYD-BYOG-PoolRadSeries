// The journal PDF as a book (Tom, 2026-10-09: the fallback for a PDF the
// engine has no table for, or when its entries aren't prepared): each page
// is a scanned picture (JPEG) - shown whole, or at the scan's size with the
// view moved round it.
#pragma once

#include "ui.h"

namespace pdfview {

// Opens the PDF (path on the SD card). False if it can't be read as a book
// of scanned pages.
bool open(const char* path);
void close();
bool is_open();

int pages();
// The page picture's size (scan pixels). False if the page has none.
bool page_size(int page, int* w, int* h);

// Draws page `page` (from 1) into r: zoom false = the whole page fitted in;
// true = the scan's size, r's top left showing scan pixel (vx, vy).
// fit_rect (optional) gets where the whole page landed (for taps).
bool draw(int page, const ui::Rect& r, bool zoom, int vx, int vy, ui::Rect* fit_rect = nullptr);

} // namespace pdfview

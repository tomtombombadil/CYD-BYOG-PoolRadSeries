// The journal PDF as a book (Tom, 2026-10-09: the fallback for a PDF the
// engine has no table for, or when its entries aren't prepared): each page
// is a scanned picture (JPEG), shown at one of three sizes - the whole
// page, the page's width, or the scan's own pixels - with the view moved
// round it.
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

// How big a page is shown (v0.23.0, Tom: zoom levels the Zoom key cycles)
enum class Fit : unsigned char { Page, Width, Full };

// The page's size on the panel at that fit, for a view r (panel pixels).
bool shown_size(int page, const ui::Rect& r, Fit fit, int* w, int* h);

enum class Result : unsigned char { Ok, NoPicture, NoMemory, BadData };

// Draws page `page` (from 1) into r at `fit`, the view's top left at
// (*vx, *vy) in shown pixels (kept on the page; a page smaller than r is
// centred). fit_rect (optional) gets where the page landed on the panel.
// No big buffers: each decoded block goes straight to the panel.
Result draw(int page, const ui::Rect& r, Fit fit, int* vx, int* vy, ui::Rect* fit_rect = nullptr);

} // namespace pdfview

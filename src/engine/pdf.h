// Just enough PDF to find the page pictures of a scanned book (the GOG
// journal PDFs: every page is one JPEG): the cross-reference table, the
// page tree, each page's image XObject and where its JPEG data is.
// Classic xref tables only (PDF 1.4 and the like); /Prev sections are
// followed. Not a general PDF reader: no content streams, no fonts.
//
// Plain C++, host-tested in tools/host_tests/test_dax.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace pdf {

constexpr int kMaxObjects = 1024;
constexpr int kMaxPages = 128;

struct Doc {
    uint32_t offset[kMaxObjects] = {};     // 0 = not in use
    int      objects = 0;
    int      root = 0;                     // the catalog's object number
    char     id[33] = {};                  // the trailer's first /ID string, hex (lower case)
};

struct Image {
    int      width = 0, height = 0;
    bool     jpeg = false;                 // /DCTDecode
    uint32_t data_at = 0, data_len = 0;
};

// Reads the cross-reference table and trailer. False if it isn't a PDF this
// can read.
bool open(dax::ByteSource& src, Doc& doc);

// The page objects in reading order; returns how many (at most max).
int pages(dax::ByteSource& src, const Doc& doc, int* page_obj, int max);

// The first image the page uses (its /Resources /XObject).
bool page_image(dax::ByteSource& src, const Doc& doc, int page_obj, Image& out);

} // namespace pdf

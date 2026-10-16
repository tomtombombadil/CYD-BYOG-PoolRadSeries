// Small pictures from big JPEGs, without decoding them whole (Tom,
// 2026-10-12: the Steam / SNEG Launcher's box art, 2016 x 1284 progressive
// JPEGs, shown on the library's pages). A JPEG's 8x8 blocks hold their
// average (the DC coefficient) and their coarse shape (the first AC
// coefficients): the four 4x4 quarters of a block come straight out of
// coefficients 0, 1, 2 and 4, so the picture at 1/4 of its size needs only
// those - in a progressive JPEG its first scans (the DC scan and the luma's
// first AC scan, read side by side from the one file), in a baseline JPEG
// the low coefficients of each block (the rest skipped). The chroma comes
// from its DC, blended between blocks. Then `Fit` scales the rows to fill a
// rectangle (centred, the overflow cut off).
//
// Our own code from the JPEG standard (ITU-T T.81). 8-bit Huffman JPEGs,
// grey or Y Cb Cr with the chroma at most halved (H, V 1 or 2 for the luma,
// 1 for the chroma); anything else is refused. Memory: a few KB plus Fit's
// rows. Plain C++, host-tested in tools/host_tests/test_dax.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace jthumb {

struct Info {
    int  width = 0, height = 0;     // the JPEG's
    int  out_w = 0, out_h = 0;      // the picture decode() gives: a quarter (rounded up)
    bool progressive = false;
    bool shape = false;             // the blocks' quarters from AC coefficients (false: flat, DC only)
};

// row(y, rgb, w, ctx): row y of the quarter-size picture, RGB888; false = stop
using RowFn = bool (*)(int y, const uint8_t* rgb, int w, void* ctx);

// Only the size (and whether it's a kind decode() reads).
bool probe(dax::ByteSource& src, Info& info);
// The picture at 1/4 of its size, a row at a time, top to bottom. False on
// data it can't read or no memory (a stop asked for is not a failure).
bool decode(dax::ByteSource& src, RowFn row, void* ctx, Info* info = nullptr);

// Scales a picture coming a row at a time (src_w x src_h) to fill tw x th:
// as big as it must be to cover it, centred (or its top kept), what's over
// cut off. Rows out:
// out(y, rgb, tw, ctx) top to bottom. A triangle filter (bilinear going
// up, averaging going down).
class Fit {
public:
    using OutFn = bool (*)(int y, const uint8_t* rgb, int w, void* ctx);
    Fit() = default;
    ~Fit();
    Fit(const Fit&) = delete;
    Fit& operator=(const Fit&) = delete;
    // top: the picture's top kept (what's over cut off the bottom), else centred
    bool begin(int src_w, int src_h, int tw, int th, OutFn out, void* ctx, bool top = false);
    bool add(const uint8_t* rgb);           // the next source row
    bool finish();                          // rows still open (the bottom edge)

private:
    bool emit(int slot);
    int  src_w_ = 0, src_h_ = 0, tw_ = 0, th_ = 0;
    float scale_ = 1, ox_ = 0, oy_ = 0, radius_ = 1;
    int  next_src_ = 0, next_out_ = 0;
    OutFn out_ = nullptr;
    void* ctx_ = nullptr;
    int      slots_ = 0;                    // output rows open at once
    int32_t* acc_ = nullptr;                // slots x tw x 3 (weights x 256)
    int32_t* wsum_ = nullptr;               // slots
    int*     slot_row_ = nullptr;           // the output row in each slot (-1: none)
    uint8_t* hrow_ = nullptr;               // a source row scaled across (tw x 3)
    uint8_t* line_ = nullptr;               // an output row
};

} // namespace jthumb

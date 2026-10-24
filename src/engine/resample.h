// Scaling scanned pictures for the small screens (the journal entries and
// the journal book, Tom 2026-10-24: "the zoom settings ... are not
// useful"): each shown pixel takes up to kTaps source pixels a direction -
// Catmull-Rom (bicubic) when enlarging, so the 72 dpi scans (Treasures)
// stay as sharp as they can, the area each shown pixel covers when
// shrinking. A turned picture (Turn: a map's long side along the screen)
// swaps the directions; the callers do that.
//
// Plain C++, host-tested in tools/host_tests/test_dax.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

namespace resample {

constexpr int kTaps = 4;
constexpr int kOne = 256;                 // the weights add up to this

// Source pixels first, first + step ... (kTaps of them; clamp each to the
// source: tap_index) with their weights (a cubic's may be below 0). step
// is 1 but when a shown pixel covers more than kTaps source pixels (the
// samples then spread over it).
struct Tap {
    int16_t first = 0;
    int16_t step = 1;
    int16_t w[kTaps] = {kOne, 0, 0, 0};
};

// Shown pixel i of a source n pixels long shown at k (shown pixels per
// source pixel x 65536)
Tap tap(int i, int64_t k, int n);

// The shown length of n source pixels at k (at least 1)
int shown(int n, int64_t k);

// k that makes n source pixels `want` shown pixels long, at most `cap`
// times enlarged (x 65536)
int64_t fit(int n, int want, int64_t cap = 4 * 65536);

inline int tap_index(const Tap& t, int j, int n)
{
    const int x = t.first + j * t.step;
    return x < 0 ? 0 : x >= n ? n - 1 : x;
}

inline uint8_t clamp8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }

// A palette picture (a journal entry's piece: w x h indexes, row by row)
struct RowSource {
    virtual ~RowSource() = default;
    // Row y's w indexes into out; false: couldn't be read (drawn as paper)
    virtual bool row(int y, uint8_t* out) = 0;
};

// Shows a palette picture at k, upright or turned (Turn: a quarter turn
// to the left - the picture's top along the screen's left edge, its long
// side along the screen's), reading only the rows it needs and keeping
// the last few. shown_w / shown_h are the picture's size on the screen.
class Scaler {
public:
    static constexpr int kRows = kTaps + 1;           // rows kept
    static constexpr int kMaxShown = 480;             // the tap table's length (a line or a column)
    // Memory it needs for a picture w pixels wide (the rows kept)
    static size_t cache_bytes(int w) { return static_cast<size_t>(w) * kRows; }

    // cache: cache_bytes(w); pal: 256 RGB triples
    void begin(int w, int h, int64_t k, bool turn, uint8_t* cache, RowSource* src, const uint8_t (*pal)[3]);
    int shown_w() const { return sw_; }
    int shown_h() const { return sh_; }

    // Upright: shown pixels [x0, x1) of shown line y, RGB (3 bytes each)
    void line(int y, int x0, int x1, uint8_t* rgb);
    // Turned: shown column x's pixels [y0, y1) (top down), RGB
    void column(int x, int y0, int y1, uint8_t* rgb);

private:
    const uint8_t* fetch(int y);
    void across(int a0, int a1);                      // the tap table for shown pixels [a0, a1) across
    void mix(const Tap& down, const Tap& across_tap, const uint8_t* const* rows, uint8_t* rgb, bool mirror);

    int w_ = 0, h_ = 0, sw_ = 0, sh_ = 0;
    int64_t k_ = 65536;
    bool turn_ = false;
    uint8_t* cache_ = nullptr;
    RowSource* src_ = nullptr;
    const uint8_t (*pal_)[3] = nullptr;
    int row_y_[kRows] = {};
    unsigned row_age_[kRows] = {};
    unsigned clock_ = 0;
    Tap tab_[kMaxShown];
    int tab0_ = 0, tab1_ = 0;                         // the shown pixels the table holds
};

} // namespace resample

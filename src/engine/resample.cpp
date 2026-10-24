#include "resample.h"

#include <cstring>

namespace resample {

namespace {

int64_t floor_div(int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Makes the weights add up to kOne (rounding), the change on the biggest
void settle(Tap& t)
{
    int sum = 0, big = 0;
    for (int j = 0; j < kTaps; ++j) {
        sum += t.w[j];
        if (t.w[j] > t.w[big]) big = j;
    }
    t.w[big] = static_cast<int16_t>(t.w[big] + kOne - sum);
}

} // namespace

Tap tap(int i, int64_t k, int n)
{
    Tap t;
    if (n <= 1 || k <= 0) {
        t.first = 0;
        return t;
    }
    if (k >= 65536) {
        // Enlarging: Catmull-Rom round the source position of the shown
        // pixel's centre, s = (i + 0.5) / k - 0.5 (in 1/256 pixels)
        const int64_t s = floor_div((2 * static_cast<int64_t>(i) + 1) * 65536 * 128, k) - 128;
        const int64_t x0 = floor_div(s, 256);
        const float f = static_cast<float>(s - x0 * 256) / 256.0f;
        const float f2 = f * f, f3 = f2 * f;
        const float w[kTaps] = {(-f3 + 2 * f2 - f) / 2, (3 * f3 - 5 * f2 + 2) / 2, (-3 * f3 + 4 * f2 + f) / 2,
                                (f3 - f2) / 2};
        t.first = static_cast<int16_t>(x0 - 1);
        for (int j = 0; j < kTaps; ++j) {
            const float v = w[j] * kOne;
            t.w[j] = static_cast<int16_t>(v >= 0 ? v + 0.5f : v - 0.5f);
        }
        settle(t);
        return t;
    }
    // Shrinking: the source span [a, b) the shown pixel covers (1/256 pixels)
    const int64_t a = static_cast<int64_t>(i) * 65536 * 256 / k;
    int64_t b = (static_cast<int64_t>(i) + 1) * 65536 * 256 / k;
    if (b > static_cast<int64_t>(n) * 256) b = static_cast<int64_t>(n) * 256;
    const int64_t span = b - a;
    if (span <= 0) {
        t.first = static_cast<int16_t>(a / 256);
        return t;
    }
    if (span <= (kTaps - 1) * 256) {
        // Each source pixel by how much of the span it covers
        const int64_t x0 = a / 256;
        t.first = static_cast<int16_t>(x0);
        for (int j = 0; j < kTaps; ++j) {
            const int64_t p0 = (x0 + j) * 256, p1 = p0 + 256;
            const int64_t lo = p0 > a ? p0 : a, hi = p1 < b ? p1 : b;
            t.w[j] = static_cast<int16_t>(hi > lo ? (hi - lo) * kOne / span : 0);
        }
        settle(t);
        return t;
    }
    // Wider: kTaps samples spread evenly over it
    const int64_t step = span / kTaps;
    t.first = static_cast<int16_t>((a + step / 2) / 256);
    int st = static_cast<int>((step + 128) / 256);
    t.step = static_cast<int16_t>(st < 1 ? 1 : st);
    for (int j = 0; j < kTaps; ++j) t.w[j] = kOne / kTaps;
    return t;
}

int shown(int n, int64_t k)
{
    const int64_t s = static_cast<int64_t>(n) * k / 65536;
    return s < 1 ? 1 : static_cast<int>(s);
}

int64_t fit(int n, int want, int64_t cap)
{
    if (n < 1 || want < 1) return 65536;
    int64_t k = (static_cast<int64_t>(want) * 65536 + n - 1) / n;      // (rounded up: exactly `want` shown)
    if (k > cap) k = cap;
    return k < 1 ? 1 : k;
}

} // namespace resample

namespace resample {

void Scaler::begin(int w, int h, int64_t k, bool turn, uint8_t* cache, RowSource* src, const uint8_t (*pal)[3])
{
    w_ = w;
    h_ = h;
    k_ = k;
    turn_ = turn;
    cache_ = cache;
    src_ = src;
    pal_ = pal;
    sw_ = shown(turn ? h : w, k);
    sh_ = shown(turn ? w : h, k);
    for (int i = 0; i < kRows; ++i) {
        row_y_[i] = -1;
        row_age_[i] = 0;
    }
    clock_ = 0;
    tab0_ = tab1_ = 0;
}

const uint8_t* Scaler::fetch(int y)
{
    int slot = 0;
    for (int i = 0; i < kRows; ++i) {
        if (row_y_[i] == y) {
            row_age_[i] = ++clock_;
            return cache_ + static_cast<size_t>(i) * w_;
        }
        if (row_age_[i] < row_age_[slot]) slot = i;
    }
    uint8_t* r = cache_ + static_cast<size_t>(slot) * w_;
    if (!src_ || !src_->row(y, r)) memset(r, 255, static_cast<size_t>(w_));     // (index 255: white)
    row_y_[slot] = y;
    row_age_[slot] = ++clock_;
    return r;
}

void Scaler::across(int a0, int a1)
{
    if (a0 >= tab0_ && a1 <= tab1_) return;
    int n = a1 - a0;
    if (n > kMaxShown) n = kMaxShown;
    for (int i = 0; i < n; ++i) tab_[i] = tap(a0 + i, k_, w_);
    tab0_ = a0;
    tab1_ = a0 + n;
}

void Scaler::mix(const Tap& down, const Tap& t, const uint8_t* const* rows, uint8_t* rgb, bool mirror)
{
    int64_t acc[3] = {0, 0, 0};
    for (int j = 0; j < kTaps; ++j) {
        if (!down.w[j] || !rows[j]) continue;
        int a[3] = {0, 0, 0};
        for (int i = 0; i < kTaps; ++i) {
            if (!t.w[i]) continue;
            int sx = tap_index(t, i, w_);
            if (mirror) sx = w_ - 1 - sx;
            const uint8_t* c = pal_[rows[j][sx]];
            a[0] += t.w[i] * c[0];
            a[1] += t.w[i] * c[1];
            a[2] += t.w[i] * c[2];
        }
        for (int c = 0; c < 3; ++c) acc[c] += static_cast<int64_t>(down.w[j]) * a[c];
    }
    for (int c = 0; c < 3; ++c) rgb[c] = clamp8(static_cast<int>((acc[c] + 32768) >> 16));
}

void Scaler::line(int y, int x0, int x1, uint8_t* rgb)
{
    if (x1 > x0 + kMaxShown) x1 = x0 + kMaxShown;
    const Tap down = tap(y, k_, h_);
    const uint8_t* rows[kTaps] = {};
    for (int j = 0; j < kTaps; ++j)
        if (down.w[j]) rows[j] = fetch(tap_index(down, j, h_));
    across(x0, x1);
    for (int x = x0; x < x1; ++x, rgb += 3) mix(down, tab_[x - tab0_], rows, rgb, false);
}

void Scaler::column(int x, int y0, int y1, uint8_t* rgb)
{
    if (y1 > y0 + kMaxShown) y1 = y0 + kMaxShown;
    // Shown column x = source row; shown row y = source column w - 1 - y
    const Tap down = tap(x, k_, h_);
    const uint8_t* rows[kTaps] = {};
    for (int j = 0; j < kTaps; ++j)
        if (down.w[j]) rows[j] = fetch(tap_index(down, j, h_));
    across(y0, y1);
    for (int y = y0; y < y1; ++y, rgb += 3) mix(down, tab_[y - tab0_], rows, rgb, true);
}

} // namespace resample

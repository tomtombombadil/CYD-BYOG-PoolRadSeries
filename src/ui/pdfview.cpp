#include "pdfview.h"

#include <Arduino.h>
#include <cstring>
#include <new>

#include "app/library.h"
#include "engine/journal.h"
#include "engine/jpeg.h"
#include "engine/pdf.h"
#include "engine/resample.h"
#include "hal/sdcard.h"

namespace pdfview {

namespace {

constexpr int kMaxCols = 8;

struct State {
    fs::File  file;
    library::FileSource* src = nullptr;
    alignas(library::FileSource) uint8_t src_mem[sizeof(library::FileSource)];
    pdf::Doc  doc;
    int       page_obj[pdf::kMaxPages];
    int       n = 0;
    // The book's pages: a PDF page and its picture (page_image's index) -
    // one a PDF page, or (the Treasures journal: a spread a PDF page, a
    // picture a book page) each big picture, left to right
    uint8_t   bp_page[pdf::kMaxPages * 2];
    uint8_t   bp_img[pdf::kMaxPages * 2];
    int       nbp = 0;
    // The last page whose columns were found (scan pixels)
    int       col_page = 0, ncols = 0;
    int       cx0[kMaxCols], cx1[kMaxCols];
};
State* st = nullptr;

// ---- the whole page: decoded at 1 / (1 << s), each panel pixel the
// nearest decoded pixel ---------------------------------------------------

// kd: panel pixels per decoded pixel x 65536, at most 1:1; sw x sh: the
// page's size on the panel
struct Scale {
    int s = 0;
    int64_t kd = 65536;
    int sw = 0, sh = 0;
};

Scale whole_scale(int w, int h, const ui::Rect& r)
{
    Scale c;
    if (w < 1 || h < 1) return c;
    const int64_t kx = static_cast<int64_t>(r.w) * 65536 / w, ky = static_cast<int64_t>(r.h) * 65536 / h;
    int64_t k = kx < ky ? kx : ky;
    if (k > 65536) k = 65536;            // never bigger than the scan
    if (k < 1) k = 1;
    c.s = 0;
    while (c.s < 3 && (k << (c.s + 1)) <= 65536) ++c.s;   // decode as small as still fills it
    c.kd = k << c.s;
    c.sw = static_cast<int>((static_cast<int64_t>(w >> c.s) * c.kd) / 65536);
    c.sh = static_cast<int>((static_cast<int64_t>(h >> c.s) * c.kd) / 65536);
    if (c.sw < 1) c.sw = 1;
    if (c.sh < 1) c.sh = 1;
    return c;
}

struct Whole {
    Scale c;
    int ox = 0, oy = 0, ow = 0, oh = 0;    // where the page goes on the panel
    lgfx::rgb888_t* band = nullptr;        // band_rows x ow, or nullptr
    int band_rows = 0;
    int64_t band_y0 = -1, band_y1 = -1;    // shown rows the band holds now
    lgfx::rgb888_t line[480];
};

int64_t first_shown(const Whole& d, int64_t dec) { return (dec * d.c.kd + 65535) / 65536; }
int dec_of(const Whole& d, int64_t shown) { return static_cast<int>(shown * 65536 / d.c.kd); }

void whole_flush(Whole& d)
{
    if (!d.band || d.band_y0 < 0) return;
    for (int64_t Y = d.band_y0; Y < d.band_y1; ++Y) {
        if (Y >= d.oh) continue;
        ui::gfx().pushImage(d.ox, d.oy + static_cast<int>(Y), d.ow, 1, d.band + static_cast<size_t>(Y - d.band_y0) * d.ow);
    }
    d.band_y0 = d.band_y1 = -1;
}

bool whole_block(int x, int y, int w, int h, const uint8_t* rgb, void* ctx)
{
    Whole& d = *static_cast<Whole*>(ctx);
    if (d.band) {
        const int64_t y0 = first_shown(d, y), y1 = first_shown(d, y + h);
        if (y0 != d.band_y0) {
            whole_flush(d);
            d.band_y0 = y0;
            d.band_y1 = y1 - y0 <= d.band_rows ? y1 : y0 + d.band_rows;
        }
    }
    int64_t X0 = first_shown(d, x), X1 = first_shown(d, x + w);
    int64_t Y0 = first_shown(d, y), Y1 = first_shown(d, y + h);
    if (X1 > d.ow) X1 = d.ow;
    if (Y1 > d.oh) Y1 = d.oh;
    if (X0 >= X1 || Y0 >= Y1) return true;
    for (int64_t Y = Y0; Y < Y1; ++Y) {
        int ry = dec_of(d, Y) - y;
        if (ry >= h) ry = h - 1;
        const uint8_t* row = rgb + static_cast<size_t>(ry) * w * 3;
        int n = 0;
        lgfx::rgb888_t* out = d.line;
        if (d.band) {
            if (Y < d.band_y0 || Y >= d.band_y1) continue;
            out = d.band + static_cast<size_t>(Y - d.band_y0) * d.ow + X0;
        }
        for (int64_t X = X0; X < X1 && n < 480; ++X, ++n) {
            int rx = dec_of(d, X) - x;
            if (rx >= w) rx = w - 1;
            const uint8_t* p = row + rx * 3;
            out[n] = lgfx::rgb888_t(p[0], p[1], p[2]);
        }
        if (!d.band) ui::gfx().pushImage(d.ox + static_cast<int>(X0), d.oy + static_cast<int>(Y), n, 1, d.line);
    }
    return true;
}

// ---- finding the columns: the page decoded small, how much print each
// of its columns has --------------------------------------------------------

struct Profile {
    int s = 0, w = 0, h = 0;               // decoded at 1 / (1 << s); its size
    int top = 0, band_h = 1;               // the strips (the top and bottom 4% left out: ornaments, page numbers)
    uint16_t ink[journal::kColumnBands][424];
    uint8_t clear[424];
};

bool profile_block(int x, int y, int w, int h, const uint8_t* rgb, void* ctx)
{
    Profile& p = *static_cast<Profile*>(ctx);
    for (int r = 0; r < h; ++r) {
        const int band = (y + r - p.top) / p.band_h;
        if (y + r < p.top || band >= journal::kColumnBands) continue;
        uint16_t* ink = p.ink[band];
        const uint8_t* s = rgb + static_cast<size_t>(r) * w * 3;
        for (int c = 0; c < w; ++c, s += 3) {
            const int px = x + c;
            if (px >= p.w || px >= 424) break;
            const int lum = (s[0] + s[1] + s[2]) / 3;
            if (lum < 240 && ink[px] < 60000) ink[px] = static_cast<uint16_t>(ink[px] + (240 - lum));
        }
    }
    return true;
}

// The page's columns into st (once a page)
Result find_columns(int page, const pdf::Image& img, uint8_t* pool, const jpeg::Info& info)
{
    if (st->col_page == page) return Result::Ok;
    Profile* p = new (std::nothrow) Profile;
    if (!p) return Result::NoMemory;
    memset(p->ink, 0, sizeof p->ink);
    p->s = 0;
    while (p->s < 3 && (info.width >> p->s) > 420) ++p->s;
    p->w = info.width >> p->s;
    p->h = info.height >> p->s;
    p->top = p->h / 25;
    p->band_h = (p->h - 2 * p->top) / journal::kColumnBands;
    if (p->band_h < 1) p->band_h = 1;
    Result res = Result::Ok;
    if (!jpeg::decode(*st->src, img.data_at, img.data_len, pool, nullptr, profile_block, p, nullptr, p->s))
        res = Result::BadData;
    if (res == Result::Ok) {
        const int n = p->w < 424 ? p->w : 424;
        uint8_t* clear = p->clear;
        journal::clear_strips(&p->ink[0][0], 424, n, journal::kColumnBands, p->band_h, clear);
        int x0[kMaxCols], x1[kMaxCols];
        const int nc = journal::find_columns(clear, n, journal::kColumnBands, x0, x1, kMaxCols);
        const int n_ = nc;
        st->ncols = n_ > 0 ? n_ : 1;
        for (int i = 0; i < st->ncols; ++i) {
            st->cx0[i] = n_ > 0 ? x0[i] << p->s : 0;
            st->cx1[i] = n_ > 0 ? x1[i] << p->s : info.width;
            if (st->cx1[i] > info.width || (i + 1 == n_ && info.width - st->cx1[i] < (1 << p->s))) st->cx1[i] = info.width;
        }
        st->col_page = page;
    }
    delete p;
    return res;
}

// ---- one column: decoded at 1 / (1 << s) (no smaller than the screen's
// width), its rows kept a few at a time and scaled (resample::) as they
// come ----------------------------------------------------------------------

struct Column {
    int s = 0;
    int dx0 = 0, dw = 0, dh = 0;           // the column in decoded pixels
    int64_t k = 65536;                     // shown pixels per decoded pixel
    int vy = 0, ox = 0, oy = 0, ow = 0, oh = 0;
    int first_row = 0, last_row = 0;       // the decoded rows the view needs
    static constexpr int kRing = 24;       // rows kept (an MCU row's and the taps' before it)
    uint8_t* ring = nullptr;               // kRing x dw palette indexes (journal::index_of)
    int row_at[kRing];
    int done = 0;                          // rows complete (all before it)
    int cur_y = -1;                        // the MCU row being decoded
    int next = 0;                          // the next shown row to draw
    resample::Tap tx[480];
    uint8_t pal[256][3];
    lgfx::rgb888_t line[480];
    bool ok = true;
};

// Shown rows whose source rows are all there (all of them: limit huge)
void column_emit(Column& c, int limit)
{
    while (c.next < c.oh) {
        const resample::Tap ty = resample::tap(c.vy + c.next, c.k, c.dh);
        const uint8_t* rows[resample::kTaps] = {};
        bool ready = true;
        for (int j = 0; j < resample::kTaps && ready; ++j) {
            if (!ty.w[j]) continue;
            const int y = resample::tap_index(ty, j, c.dh);
            if (y >= limit) {
                ready = false;
                break;
            }
            const int slot = y % Column::kRing;
            rows[j] = c.row_at[slot] == y ? c.ring + static_cast<size_t>(slot) * c.dw : nullptr;
        }
        if (!ready) return;
        for (int x = 0; x < c.ow; ++x) {
            const resample::Tap& t = c.tx[x];
            int64_t acc[3] = {0, 0, 0};
            for (int j = 0; j < resample::kTaps; ++j) {
                if (!ty.w[j]) continue;
                int a[3] = {0, 0, 0};
                for (int i = 0; i < resample::kTaps; ++i) {
                    if (!t.w[i]) continue;
                    const uint8_t* p = c.pal[rows[j] ? rows[j][resample::tap_index(t, i, c.dw)] : journal::kPaper];
                    a[0] += t.w[i] * p[0];
                    a[1] += t.w[i] * p[1];
                    a[2] += t.w[i] * p[2];
                }
                for (int q = 0; q < 3; ++q) acc[q] += static_cast<int64_t>(ty.w[j]) * a[q];
            }
            c.line[x] = lgfx::rgb888_t(resample::clamp8(static_cast<int>((acc[0] + 32768) >> 16)),
                                       resample::clamp8(static_cast<int>((acc[1] + 32768) >> 16)),
                                       resample::clamp8(static_cast<int>((acc[2] + 32768) >> 16)));
        }
        ui::gfx().pushImage(c.ox, c.oy + c.next, c.ow, 1, c.line);
        ++c.next;
    }
}

int column_want(int x, int y, int mw, int mh, void* ctx)
{
    Column& c = *static_cast<Column*>(ctx);
    if ((y >> c.s) > c.last_row) return -1;
    if (((y + mh) >> c.s) < c.first_row) return 0;          // (above the view: not needed)
    const int fx0 = c.dx0 << c.s, fx1 = (c.dx0 + c.dw) << c.s;
    return x < fx1 && x + mw > fx0 ? 1 : 0;
}

bool column_block(int x, int y, int w, int h, const uint8_t* rgb, void* ctx)
{
    Column& c = *static_cast<Column*>(ctx);
    if (y != c.cur_y) {
        // A new MCU row: the ones before it are complete
        if (c.cur_y >= 0) {
            c.done = y;
            column_emit(c, c.done);
        }
        c.cur_y = y;
    }
    for (int r = 0; r < h; ++r) {
        const int ry = y + r;
        const int slot = ry % Column::kRing;
        uint8_t* out = c.ring + static_cast<size_t>(slot) * c.dw;
        c.row_at[slot] = ry;
        const uint8_t* s = rgb + static_cast<size_t>(r) * w * 3;
        for (int q = 0; q < w; ++q, s += 3) {
            const int px = x + q - c.dx0;
            if (px < 0 || px >= c.dw) continue;
            out[px] = journal::index_of(s[0], s[1], s[2]);
        }
    }
    return c.ok;
}

Result draw_column(int page, const pdf::Image& img, uint8_t* pool, const jpeg::Info& info, const ui::Rect& r, View& v)
{
    Result res = find_columns(page, img, pool, info);
    if (res != Result::Ok) return res;
    v.cols = st->ncols;
    if (v.at_x >= 0) {
        // The column at that spot (or the nearest)
        int best = 0, dist = 1 << 30;
        for (int i = 0; i < st->ncols; ++i) {
            const int d = v.at_x < st->cx0[i] ? st->cx0[i] - v.at_x : v.at_x >= st->cx1[i] ? v.at_x - st->cx1[i] + 1 : 0;
            if (d < dist) {
                dist = d;
                best = i;
            }
        }
        v.col = best;
    }
    if (v.col < 0 || v.col >= st->ncols) v.col = st->ncols - 1;      // (kLastColumn, or past the last)
    Column* c = new (std::nothrow) Column;
    if (!c) return Result::NoMemory;
    const int x0 = st->cx0[v.col], x1 = st->cx1[v.col];
    c->s = 0;
    while (c->s < 3 && ((x1 - x0) >> (c->s + 1)) >= r.w) ++c->s;
    c->dx0 = x0 >> c->s;
    c->dw = (x1 >> c->s) - c->dx0;
    c->dh = info.height >> c->s;
    if (c->dw < 1) c->dw = 1;
    if (c->dh < 1) c->dh = 1;
    c->k = resample::fit(c->dw, r.w, 3 * 65536);
    c->ow = resample::shown(c->dw, c->k);
    if (c->ow > r.w) c->ow = r.w;
    const int sh = resample::shown(c->dh, c->k);
    v.shown_h = sh;
    if (v.at_x >= 0) v.vy = static_cast<int>(static_cast<int64_t>(v.at_y >> c->s) * c->k / 65536) - r.h / 2;
    v.at_x = v.at_y = -1;
    const int maxy = sh > r.h ? sh - r.h : 0;
    if (v.vy > maxy) v.vy = maxy;
    if (v.vy < 0) v.vy = 0;
    c->vy = v.vy;
    c->oh = sh < r.h ? sh : r.h;
    c->ox = r.x + (r.w - c->ow) / 2;
    c->oy = r.y;
    v.fit = ui::Rect{c->ox, c->oy, c->ow, c->oh};
    // The rows the view needs
    const resample::Tap first = resample::tap(c->vy, c->k, c->dh), last = resample::tap(c->vy + c->oh - 1, c->k, c->dh);
    c->first_row = c->dh;
    c->last_row = 0;
    for (int j = 0; j < resample::kTaps; ++j) {
        const int y = resample::tap_index(last, j, c->dh), y0 = resample::tap_index(first, j, c->dh);
        if (last.w[j] && y > c->last_row) c->last_row = y;
        if (first.w[j] && y0 < c->first_row) c->first_row = y0;
    }
    c->first_row -= 2;                     // (an MCU row's start at the scale may round)
    for (int x = 0; x < c->ow; ++x) c->tx[x] = resample::tap(x, c->k, c->dw);
    for (int i = 0; i < 256; ++i) journal::palette_rgb(i, &c->pal[i][0], &c->pal[i][1], &c->pal[i][2]);
    for (int i = 0; i < Column::kRing; ++i) c->row_at[i] = -1;
    const int mh = info.mcu_h >> c->s > 0 ? info.mcu_h >> c->s : 1;
    c->ring = mh + resample::kTaps + 2 <= Column::kRing
                  ? static_cast<uint8_t*>(malloc(static_cast<size_t>(Column::kRing) * c->dw))
                  : nullptr;
    if (!c->ring) res = Result::NoMemory;
    if (res == Result::Ok) {
        // Paper below the picture when the column is shorter than the view
        if (c->oh < r.h) ui::gfx().fillRect(r.x, r.y + c->oh, r.w, r.h - c->oh, TFT_WHITE);
        if (c->ox > r.x) ui::gfx().fillRect(r.x, r.y, c->ox - r.x, c->oh, TFT_WHITE);
        if (c->ox + c->ow < r.x + r.w) ui::gfx().fillRect(c->ox + c->ow, r.y, r.x + r.w - c->ox - c->ow, c->oh, TFT_WHITE);
        ui::card_draw_begin();
        if (!jpeg::decode(*st->src, img.data_at, img.data_len, pool, column_want, column_block, c, nullptr, c->s))
            res = Result::BadData;
        else
            column_emit(*c, 1 << 30);
        ui::card_draw_end();
    }
    free(c->ring);
    delete c;
    return res;
}

} // namespace

bool open(const char* path)
{
    close();
    if (!sd_begin()) return false;
    st = new (std::nothrow) State;
    if (!st) return false;
    st->file = sd_fs().open(path, "r");
    if (!st->file) {
        close();
        return false;
    }
    st->src = new (st->src_mem) library::FileSource(st->file);
    if (!pdf::open(*st->src, st->doc)) {
        close();
        return false;
    }
    st->n = pdf::pages(*st->src, st->doc, st->page_obj, pdf::kMaxPages);
    if (st->n == 0) {
        close();
        return false;
    }
    // Several pictures on the first page: the book's pages are the big ones
    pdf::Image first;
    pdf::page_image(*st->src, st->doc, st->page_obj[0], first);
    for (int p = 0; p < st->n; ++p) {
        if (first.images <= 1) {
            st->bp_page[st->nbp] = static_cast<uint8_t>(p);
            st->bp_img[st->nbp++] = 0;
            continue;
        }
        pdf::Picture pics[pdf::kMaxPictures];
        const int k = pdf::page_pictures(*st->src, st->doc, st->page_obj[p], pics, pdf::kMaxPictures);
        int tall = 0;
        for (int i = 0; i < k; ++i) tall = pics[i].height > tall ? pics[i].height : tall;
        for (int i = 0; i < k && st->nbp < pdf::kMaxPages * 2; ++i)
            if (pics[i].height * 10 >= tall * 6) {
                st->bp_page[st->nbp] = static_cast<uint8_t>(p);
                st->bp_img[st->nbp++] = static_cast<uint8_t>(pics[i].index);
            }
    }
    return st->nbp > 0;
}

void close()
{
    if (!st) return;
    if (st->src) st->src->~FileSource();
    if (st->file) st->file.close();
    delete st;
    st = nullptr;
}

bool is_open() { return st != nullptr; }
int pages() { return st ? st->nbp : 0; }

Result draw(int page, const ui::Rect& r, Fit fit, View& v)
{
    if (!st || page < 1 || page > st->nbp) return Result::NoPicture;
    pdf::Image img;
    if (!pdf::page_image(*st->src, st->doc, st->page_obj[st->bp_page[page - 1]], img, st->bp_img[page - 1]) || !img.jpeg)
        return img.no_memory ? Result::NoMemory : Result::NoPicture;
    uint8_t* pool = static_cast<uint8_t*>(malloc(jpeg::kPoolSize));
    if (!pool) return Result::NoMemory;
    jpeg::Info info;
    Result res = jpeg::probe(*st->src, img.data_at, img.data_len, pool, info) ? Result::Ok : Result::BadData;
    if (res == Result::Ok) {
        v.page_w = info.width;
        v.page_h = info.height;
        if (fit == Fit::Column) {
            res = draw_column(page, img, pool, info, r, v);
        } else {
            Whole* d = new (std::nothrow) Whole;
            if (!d) res = Result::NoMemory;
            if (res == Result::Ok) {
                d->c = whole_scale(info.width, info.height, r);
                d->ow = d->c.sw < r.w ? d->c.sw : r.w;
                d->oh = d->c.sh < r.h ? d->c.sh : r.h;
                d->ox = r.x + (r.w - d->ow) / 2;
                d->oy = r.y + (r.h - d->oh) / 2;
                v.fit = ui::Rect{d->ox, d->oy, d->ow, d->oh};
                v.shown_h = d->oh;
                v.cols = st->col_page == page ? st->ncols : 0;
                // The band: the shown rows one MCU row makes (small when scaled down)
                d->band_rows = static_cast<int>(((info.mcu_h >> d->c.s) * d->c.kd + 65535) / 65536) + 1;
                d->band = static_cast<lgfx::rgb888_t*>(malloc(sizeof(lgfx::rgb888_t) * d->band_rows * d->ow));
                ui::card_draw_begin();
                if (!jpeg::decode(*st->src, img.data_at, img.data_len, pool, nullptr, whole_block, d, nullptr, d->c.s))
                    res = Result::BadData;
                whole_flush(*d);
                ui::card_draw_end();
                free(d->band);
            }
            delete d;
        }
    }
    free(pool);
    return res;
}

} // namespace pdfview

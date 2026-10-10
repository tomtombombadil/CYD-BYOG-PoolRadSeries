#include "pdfview.h"

#include <Arduino.h>
#include <cstring>
#include <new>

#include "app/library.h"
#include "engine/jpeg.h"
#include "engine/pdf.h"
#include "hal/sdcard.h"

namespace pdfview {

namespace {

struct State {
    fs::File  file;
    library::FileSource* src = nullptr;
    alignas(library::FileSource) uint8_t src_mem[sizeof(library::FileSource)];
    pdf::Doc  doc;
    int       page_obj[pdf::kMaxPages];
    int       n = 0;
    int       size_page = 0, size_w = 0, size_h = 0;  // the last page drawn's size (no PDF work on loop()'s stack)
};
State* st = nullptr;

// The scale for a fit: the JPEG decoded at 1 / (1 << s), then each panel
// pixel takes the nearest decoded pixel (kd: panel pixels per decoded
// pixel x 65536, at most 1:1); sw x sh: the page's size on the panel
struct Scale {
    int s = 0;
    int64_t kd = 65536;
    int sw = 0, sh = 0;
};

Scale fit_scale(int w, int h, const ui::Rect& r, Fit fit)
{
    Scale c;
    if (w < 1 || h < 1) return c;
    // k: panel pixels per scan pixel x 65536
    int64_t k = 65536;
    if (fit == Fit::Width) {
        k = static_cast<int64_t>(r.w) * 65536 / w;
    } else if (fit == Fit::Page) {
        const int64_t kx = static_cast<int64_t>(r.w) * 65536 / w, ky = static_cast<int64_t>(r.h) * 65536 / h;
        k = kx < ky ? kx : ky;
    }
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

// One drawing. The panel rows an MCU row makes are gathered in a small
// band (a few rows when scaled down) and pushed whole; when there's no
// memory for it (full size: 16 rows), each block goes straight to the
// panel (v0.23.0 - the old 23 KB decode band didn't fit beside the Play
// Test)
struct Draw {
    Scale c;
    int vx = 0, vy = 0;            // the view's top left, in shown pixels
    int ox = 0, oy = 0, ow = 0, oh = 0;    // where the page goes on the panel
    lgfx::rgb888_t* band = nullptr;        // band_rows x ow, or nullptr
    int band_rows = 0;
    int64_t band_y0 = -1, band_y1 = -1;    // shown rows the band holds now
    lgfx::rgb888_t line[480];
};

// Decoded pixel <-> shown pixel
int64_t first_shown(const Draw& d, int64_t dec) { return (dec * d.c.kd + 65535) / 65536; }
int dec_of(const Draw& d, int64_t shown) { return static_cast<int>(shown * 65536 / d.c.kd); }

int want(int x, int y, int mw, int mh, void* ctx)
{
    Draw& d = *static_cast<Draw*>(ctx);
    const int s = d.c.s;
    const int fx0 = dec_of(d, d.vx) << s, fy0 = dec_of(d, d.vy) << s;
    const int fx1 = (dec_of(d, d.vx + d.ow) + 1) << s, fy1 = (dec_of(d, d.vy + d.oh) + 1) << s;
    if (y >= fy1) return -1;
    return (x < fx1 && x + mw > fx0 && y < fy1 && y + mh > fy0) ? 1 : 0;
}

// The band's rows to the panel
void flush(Draw& d)
{
    if (!d.band || d.band_y0 < 0) return;
    for (int64_t Y = d.band_y0; Y < d.band_y1; ++Y) {
        if (Y < d.vy || Y >= d.vy + d.oh) continue;
        ui::gfx().pushImage(d.ox, d.oy + static_cast<int>(Y - d.vy), d.ow, 1,
                            d.band + static_cast<size_t>(Y - d.band_y0) * d.ow);
    }
    d.band_y0 = d.band_y1 = -1;
}

bool block(int x, int y, int w, int h, const uint8_t* rgb, void* ctx)
{
    Draw& d = *static_cast<Draw*>(ctx);
    if (d.band) {
        const int64_t y0 = first_shown(d, y), y1 = first_shown(d, y + h);
        if (y0 != d.band_y0) {
            flush(d);
            d.band_y0 = y0;
            d.band_y1 = y1 - y0 <= d.band_rows ? y1 : y0 + d.band_rows;
        }
    }
    // The shown pixels this block covers, inside the view
    int64_t X0 = first_shown(d, x), X1 = first_shown(d, x + w);
    int64_t Y0 = first_shown(d, y), Y1 = first_shown(d, y + h);
    if (X0 < d.vx) X0 = d.vx;
    if (X1 > d.vx + d.ow) X1 = d.vx + d.ow;
    if (Y0 < d.vy) Y0 = d.vy;
    if (Y1 > d.vy + d.oh) Y1 = d.vy + d.oh;
    if (X0 >= X1 || Y0 >= Y1) return true;
    for (int64_t Y = Y0; Y < Y1; ++Y) {
        int ry = dec_of(d, Y) - y;
        if (ry >= h) ry = h - 1;
        const uint8_t* row = rgb + static_cast<size_t>(ry) * w * 3;
        int n = 0;
        lgfx::rgb888_t* out = d.line;
        if (d.band) {
            if (Y < d.band_y0 || Y >= d.band_y1) continue;
            out = d.band + static_cast<size_t>(Y - d.band_y0) * d.ow + (X0 - d.vx);
        }
        for (int64_t X = X0; X < X1 && n < 480; ++X, ++n) {
            int rx = dec_of(d, X) - x;
            if (rx >= w) rx = w - 1;
            const uint8_t* p = row + rx * 3;
            out[n] = lgfx::rgb888_t(p[0], p[1], p[2]);
        }
        if (!d.band)
            ui::gfx().pushImage(d.ox + static_cast<int>(X0 - d.vx), d.oy + static_cast<int>(Y - d.vy), n, 1, d.line);
    }
    return true;
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
    return true;
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
int pages() { return st ? st->n : 0; }

bool page_size(int page, int* w, int* h)
{
    if (!st || page < 1 || page > st->n) return false;
    if (page == st->size_page) {
        *w = st->size_w;
        *h = st->size_h;
        return true;
    }
    pdf::Image img;
    if (!pdf::page_image(*st->src, st->doc, st->page_obj[page - 1], img) || !img.jpeg) return false;
    *w = img.width;
    *h = img.height;
    return true;
}

bool shown_size(int page, const ui::Rect& r, Fit fit, int* w, int* h)
{
    int pw, ph;
    if (!page_size(page, &pw, &ph)) return false;
    const Scale c = fit_scale(pw, ph, r, fit);
    *w = c.sw;
    *h = c.sh;
    return true;
}

Result draw(int page, const ui::Rect& r, Fit fit, int* vx, int* vy, ui::Rect* fit_rect)
{
    if (!st || page < 1 || page > st->n) return Result::NoPicture;
    pdf::Image img;
    if (!pdf::page_image(*st->src, st->doc, st->page_obj[page - 1], img) || !img.jpeg)
        return img.no_memory ? Result::NoMemory : Result::NoPicture;
    uint8_t* pool = static_cast<uint8_t*>(malloc(jpeg::kPoolSize));
    Draw* d = new (std::nothrow) Draw;
    Result res = pool && d ? Result::Ok : Result::NoMemory;
    jpeg::Info info;
    if (res == Result::Ok && !jpeg::probe(*st->src, img.data_at, img.data_len, pool, info)) res = Result::BadData;
    if (res == Result::Ok) {
        st->size_page = page;
        st->size_w = info.width;
        st->size_h = info.height;
        d->c = fit_scale(info.width, info.height, r, fit);
        // The view on the page; a page smaller than the view is centred
        const int maxx = d->c.sw > r.w ? d->c.sw - r.w : 0, maxy = d->c.sh > r.h ? d->c.sh - r.h : 0;
        if (*vx > maxx) *vx = maxx;
        if (*vy > maxy) *vy = maxy;
        if (*vx < 0) *vx = 0;
        if (*vy < 0) *vy = 0;
        d->vx = *vx;
        d->vy = *vy;
        d->ow = d->c.sw < r.w ? d->c.sw : r.w;
        d->oh = d->c.sh < r.h ? d->c.sh : r.h;
        d->ox = r.x + (r.w - d->ow) / 2;
        d->oy = r.y + (r.h - d->oh) / 2;
        if (fit_rect) *fit_rect = ui::Rect{d->ox, d->oy, d->ow, d->oh};
        // The band: the shown rows one MCU row makes (small when scaled down)
        d->band_rows = static_cast<int>(((info.mcu_h >> d->c.s) * d->c.kd + 65535) / 65536) + 1;
        d->band = static_cast<lgfx::rgb888_t*>(malloc(sizeof(lgfx::rgb888_t) * d->band_rows * d->ow));
        ui::gfx().startWrite();
        if (!jpeg::decode(*st->src, img.data_at, img.data_len, pool, want, block, d, nullptr, d->c.s))
            res = Result::BadData;
        flush(*d);
        ui::gfx().endWrite();
        free(d->band);
    }
    delete d;
    free(pool);
    return res;
}

} // namespace pdfview

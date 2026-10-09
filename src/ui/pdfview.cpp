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
};
State* st = nullptr;

// One drawing: the part of the (scaled) page picture in view, a band of
// MCU rows at a time, put on the panel scaled by k (nearest pixel)
struct Draw {
    int scale = 0;                 // decode 1 / (1 << scale)
    int vx = 0, vy = 0, vw = 0, vh = 0;    // view, in scaled picture pixels
    int ox = 0, oy = 0, ow = 0, oh = 0;    // where it goes on the panel
    int k256 = 256;                // panel pixels per picture pixel x 256
    int band_y = -1, band_h = 16;
    uint8_t* band = nullptr;       // band_h rows of vw (RGB888)
    lgfx::rgb888_t line[480];
};

void emit(Draw& d)
{
    if (d.band_y < 0) return;
    // Panel rows whose picture row is in this band
    for (int j = 0; j < d.oh; ++j) {
        const int sy = d.vy + j * 256 / d.k256;
        if (sy < d.band_y) continue;
        if (sy >= d.band_y + d.band_h) break;
        const uint8_t* row = d.band + static_cast<size_t>(sy - d.band_y) * d.vw * 3;
        for (int i = 0; i < d.ow && i < 480; ++i) {
            int sx = i * 256 / d.k256;
            if (sx >= d.vw) sx = d.vw - 1;
            d.line[i] = lgfx::rgb888_t(row[sx * 3], row[sx * 3 + 1], row[sx * 3 + 2]);
        }
        ui::gfx().pushImage(d.ox, d.oy + j, d.ow, 1, d.line);
    }
}

int want(int x, int y, int mw, int mh, void* ctx)
{
    Draw& d = *static_cast<Draw*>(ctx);
    const int s = d.scale;
    const int fx0 = d.vx << s, fy0 = d.vy << s, fx1 = (d.vx + d.vw) << s, fy1 = (d.vy + d.vh) << s;
    if (y >= fy1) return -1;
    return (x < fx1 && x + mw > fx0 && y < fy1 && y + mh > fy0) ? 1 : 0;
}

bool block(int x, int y, int w, int h, const uint8_t* rgb, void* ctx)
{
    Draw& d = *static_cast<Draw*>(ctx);
    if (y != d.band_y) {
        emit(d);
        d.band_y = y;
    }
    for (int r = 0; r < h && r < d.band_h; ++r) {
        const uint8_t* s = rgb + static_cast<size_t>(r) * w * 3;
        uint8_t* out = d.band + static_cast<size_t>(r) * d.vw * 3;
        for (int c = 0; c < w; ++c, s += 3) {
            const int px = x + c - d.vx;
            if (px < 0 || px >= d.vw) continue;
            memcpy(out + px * 3, s, 3);
        }
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
    pdf::Image img;
    if (!pdf::page_image(*st->src, st->doc, st->page_obj[page - 1], img) || !img.jpeg) return false;
    *w = img.width;
    *h = img.height;
    return true;
}

bool draw(int page, const ui::Rect& r, bool zoom, int vx, int vy, ui::Rect* fit_rect)
{
    if (!st || page < 1 || page > st->n) return false;
    pdf::Image img;
    if (!pdf::page_image(*st->src, st->doc, st->page_obj[page - 1], img) || !img.jpeg) return false;
    uint8_t* pool = static_cast<uint8_t*>(malloc(jpeg::kPoolSize));
    Draw* d = new (std::nothrow) Draw;
    bool ok = pool && d;
    jpeg::Info info;
    ok = ok && jpeg::probe(*st->src, img.data_at, img.data_len, pool, info);
    if (ok) {
        if (zoom) {
            d->scale = 0;
            d->vw = r.w < info.width ? r.w : info.width;
            d->vh = r.h < info.height ? r.h : info.height;
            d->vx = vx < 0 ? 0 : vx > info.width - d->vw ? info.width - d->vw : vx;
            d->vy = vy < 0 ? 0 : vy > info.height - d->vh ? info.height - d->vh : vy;
            d->k256 = 256;
        } else {
            // The biggest of 1/2, 1/4, 1/8 that fits, then stretched to fill
            d->scale = 3;
            for (int s = 1; s <= 3; ++s)
                if ((info.width >> s) <= r.w && (info.height >> s) <= r.h) {
                    d->scale = s;
                    break;
                }
            d->vx = d->vy = 0;
            d->vw = info.width >> d->scale;
            d->vh = info.height >> d->scale;
            const int kx = r.w * 256 / d->vw, ky = r.h * 256 / d->vh;
            d->k256 = kx < ky ? kx : ky;
        }
        d->ow = d->vw * d->k256 / 256;
        d->oh = d->vh * d->k256 / 256;
        if (d->ow > r.w) d->ow = r.w;
        if (d->oh > r.h) d->oh = r.h;
        d->ox = r.x + (r.w - d->ow) / 2;
        d->oy = r.y + (r.h - d->oh) / 2;
        if (fit_rect) *fit_rect = ui::Rect{d->ox, d->oy, d->ow, d->oh};
        d->band_h = info.mcu_h >> d->scale;
        if (d->band_h < 1) d->band_h = 1;
        d->band = static_cast<uint8_t*>(malloc(static_cast<size_t>(d->vw) * d->band_h * 3));
        ok = d->band != nullptr;
        if (ok) {
            ui::gfx().startWrite();
            ok = jpeg::decode(*st->src, img.data_at, img.data_len, pool, want, block, d, nullptr, d->scale);
            if (ok) emit(*d);
            ui::gfx().endWrite();
        }
        free(d->band);
    }
    delete d;
    free(pool);
    return ok;
}

} // namespace pdfview

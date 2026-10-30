#include "hlpic.h"

#include <new>

namespace hlpic {

void Colours::reset()
{
    for (int i = 0; i < 256; ++i) ega[i] = static_cast<uint8_t>(i & 15);
}

bool load_colours(hlib::ByteSource& src, uint32_t pos, uint32_t len, Colours& c, pic::VgaPalette* pal)
{
    hlib::Colours h;
    if (!hlib::read_colours(src, pos, len, h)) return false;
    if (h.first + h.count > 256) return false;
    if (h.vga) hlib::ega_table(src, h, c.ega + h.first, 256 - h.first);
    if (pal) {
        const int n = h.vga ? h.count : 16;
        if (pal->count > 0) {
            // (a load already in it: the range takes both)
            const int a = pal->first < h.first ? pal->first : h.first;
            const int b = pal->first + pal->count > h.first + n ? pal->first + pal->count : h.first + n;
            pal->first = static_cast<uint16_t>(a);
            pal->count = static_cast<uint16_t>(b - a);
        } else {
            pal->first = h.first;
            pal->count = static_cast<uint16_t>(n);
        }
        for (int i = 0; i < n && h.first + i < 256; ++i) {
            uint8_t rgb[3] = {};
            hlib::colour_rgb(src, h, i, rgb);     // (8 bits a channel)
            pal->rgb[h.first + i] = pic::Rgb{rgb[0], rgb[1], rgb[2]};
        }
    }
    return true;
}

namespace {

// The decoder and its row buffers, on the heap while a part is drawn
struct Work {
    hlib::Pixels px;
    uint8_t row[hlib::kMaxWidth];
    uint8_t seen[hlib::kMaxWidth];
    Work(hlib::ByteSource& s, const hlib::Part& p) : px(s, p) {}
};

} // namespace

bool draw(hlib::ByteSource& src, const hlib::Part& p, const Colours& col, pic::Canvas& c, int x, int y,
          const Clip& clip)
{
    if (!p.drawable() || p.width() > hlib::kMaxWidth) return false;
    Work* w = new (std::nothrow) Work(src, p);
    if (!w) return false;
    bool ok = w->px.ok();
    const int pw = p.width();
    const int cx0 = clip.x0 < 0 ? 0 : clip.x0, cy0 = clip.y0 < 0 ? 0 : clip.y0;
    const int cx1 = clip.x1 > c.w ? c.w : clip.x1, cy1 = clip.y1 > c.h ? c.h : clip.y1;
    const bool vga = c.vga && c.bpp == 8 && !p.ega();
    for (int r = 0; ok && r < p.height; ++r) {
        if (!w->px.row(w->row, w->seen)) {
            ok = false;
            break;
        }
        const int yy = y + r;
        if (yy < cy0) continue;
        if (yy >= cy1) break;                 // (rows below the clip: no need to read them)
        for (int k = 0; k < pw; ++k) {
            const int xx = x + k;
            if (xx < cx0 || xx >= cx1 || !w->seen[k]) continue;
            const uint8_t v = w->row[k];
            c.set(xx, yy, p.ega() ? static_cast<uint8_t>(v & 15) : vga ? v : col.ega[v]);
        }
    }
    delete w;
    return ok;
}

bool draw_picture_part(hlib::ByteSource& src, uint32_t pos, uint32_t len, int part, Colours& col,
                       pic::VgaPalette* pal, pic::Canvas& c, int anchor_x, int anchor_y, const Clip& clip)
{
    hlib::Library lib;
    if (hlib::open(src, pos, len, lib) != hlib::Status::Ok) return false;
    uint32_t pp, pl;
    if (hlib::entry(src, lib, 0, pp, pl)) load_colours(src, pp, pl, col, pal);
    hlib::Part p;
    if (!hlib::entry(src, lib, part, pp, pl) || !hlib::read_part(src, pp, pl, p)) return false;
    return draw(src, p, col, c, place_x(p, anchor_x), place_y(p, anchor_y), clip);
}

} // namespace hlpic

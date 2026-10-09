#include "ui.h"

#include <cstring>

#include <Arduino.h>
#include <cstdlib>

namespace ui {

namespace {

LGFX* g = nullptr;

// Touch filter state
bool     down = false;          // a press is in progress
int      agree = 0;             // readings in a row that agreed
int      empty = 0;             // empty readings in a row
int      last_x = 0, last_y = 0;
int      press_x = 0, press_y = 0;
uint32_t next_poll = 0;

// Dragging (screens that scroll or have a slider)
bool     drag_ok = false;       // this screen takes drags
bool     dragging = false;      // the press moved: a drag, its release no tap
int      cur_x = 0, cur_y = 0;  // where the press is now (readings that agreed)
int      moved_x = 0, moved_y = 0;   // drag movement not yet collected

constexpr int kAgreePx = 8;
constexpr uint32_t kPollMs = 15;

const lgfx::IFont* font_of(Font f)
{
    switch (f) {
    case Font::Small:
        if (large()) return &fonts::DejaVu12;
        return &fonts::Font0;
    case Font::Mono:   return &fonts::Font0;
    case Font::Large:  return large() ? &fonts::DejaVu24 : &fonts::DejaVu18;
    case Font::Normal: break;
    }
    return large() ? &fonts::DejaVu18 : &fonts::DejaVu12;
}

void use_font(Font f)
{
    g->setFont(font_of(f));
    g->setTextSize(1);
}

} // namespace

void begin(LGFX& gfx) { g = &gfx; }
LGFX& gfx() { return *g; }

int width() { return g->width(); }
int height() { return g->height(); }
bool large() { return g->width() >= 480; }

int header_h() { return large() ? 40 : 28; }
int key_h() { return large() ? 56 : 38; }
int gap() { return large() ? 6 : 4; }

bool poll_tap(Tap& out)
{
    const uint32_t now = millis();
    if (static_cast<int32_t>(now - next_poll) < 0) return false;
    next_poll = now + kPollMs;

    int32_t x, y;
    const bool touched = g->getTouch(&x, &y);
    if (touched) {
        empty = 0;
        if (std::abs(x - last_x) <= kAgreePx && std::abs(y - last_y) <= kAgreePx) {
            ++agree;
        } else {
            agree = 0;
        }
        last_x = x;
        last_y = y;
        if (!down && agree >= 1) {
            down = true;
            press_x = cur_x = x;
            press_y = cur_y = y;
            dragging = false;
        } else if (down && agree >= 1) {
            const int px = large() ? 14 : 10;      // a press that moves this far is a drag
            if (drag_ok && !dragging && (std::abs(x - press_x) > px || std::abs(y - press_y) > px)) dragging = true;
            if (dragging) {
                moved_x += x - cur_x;
                moved_y += y - cur_y;
            }
            cur_x = x;
            cur_y = y;
        }
        return false;
    }
    agree = 0;
    last_x = last_y = -100;
    if (down && ++empty >= 2) {
        down = false;
        empty = 0;
        if (dragging) {
            dragging = false;
            return false;
        }
        out.x = press_x;
        out.y = press_y;
        return true;
    }
    return false;
}

bool pressed() { return down; }

void allow_drag(bool on)
{
    drag_ok = on;
    dragging = false;
    moved_x = moved_y = 0;
}

bool drag(int& dx, int& dy)
{
    dx = moved_x;
    dy = moved_y;
    moved_x = moved_y = 0;
    return dragging;
}

bool touch_point(int& x, int& y)
{
    x = cur_x;
    y = cur_y;
    return down;
}

void clear() { g->fillScreen(style::kBackground); }

Rect back_rect() { return {0, 0, header_h() * 3 / 2, header_h()}; }

void header(const char* title, bool back)
{
    const int h = header_h();
    g->fillRect(0, 0, width(), h, style::kHeader);
    g->drawFastHLine(0, h - 1, width(), style::kKeyEdge);
    int tx = gap() * 2;
    if (back) {
        const Rect r = back_rect();
        // A plain chevron, big enough for a stylus
        const int cx = r.x + r.w / 2, cy = r.h / 2, s = h / 4;
        for (int t = -1; t <= 1; ++t) {
            g->drawLine(cx + s / 2 + t, cy - s, cx - s / 2 + t, cy, style::kGold);
            g->drawLine(cx - s / 2 + t, cy, cx + s / 2 + t, cy + s, style::kGold);
        }
        tx = r.w;
    }
    use_font(Font::Normal);
    g->setTextColor(style::kText);
    g->setTextDatum(textdatum_t::middle_left);
    g->drawString(title, tx, h / 2);
}

void key(const Rect& r, const char* label, KeyStyle s)
{
    const uint16_t fill = s == KeyStyle::Lit ? style::kKeyLit : s == KeyStyle::Dim ? style::kKeyDim : style::kKey;
    const int rad = large() ? 6 : 4;
    g->fillRoundRect(r.x, r.y, r.w, r.h, rad, fill);
    g->drawRoundRect(r.x, r.y, r.w, r.h, rad, s == KeyStyle::Lit ? style::kGold : style::kKeyEdge);
    const uint16_t col = s == KeyStyle::Dim ? style::kTextMuted : style::kText;
    const char* nl = strchr(label, '\n');
    if (!nl) {
        text_center(r, label, col);
        return;
    }
    // Two lines, close together
    char first[48];
    const size_t n = static_cast<size_t>(nl - label) < sizeof first - 1 ? static_cast<size_t>(nl - label) : sizeof first - 1;
    memcpy(first, label, n);
    first[n] = 0;
    use_font(Font::Normal);
    const int lh = g->fontHeight() - (large() ? 3 : 2);
    const int top = r.y + (r.h - lh * 2) / 2;
    g->setTextColor(col);
    g->setTextDatum(textdatum_t::top_center);
    g->drawString(first, r.x + r.w / 2, top);
    g->drawString(nl + 1, r.x + r.w / 2, top + lh);
}

void key_arrow(const Rect& r, Arrow a, KeyStyle s)
{
    key(r, "", s);
    const uint16_t col = s == KeyStyle::Dim ? style::kTextMuted : style::kGold;
    const int cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    int sz = (r.w < r.h ? r.w : r.h) * 3 / 10;
    if (sz < 6) sz = 6;
    const int t = sz / 3 < 3 ? 3 : sz / 3;          // line thickness
    const int hh = sz * 2 / 3;                       // arrow head half size
    switch (a) {
    case Arrow::Forward:
        g->fillTriangle(cx, cy - sz, cx - hh, cy - sz + hh, cx + hh, cy - sz + hh, col);
        g->fillRect(cx - t / 2, cy - sz + hh, t, sz * 2 - hh, col);
        break;
    case Arrow::Left:
        g->fillTriangle(cx - sz, cy, cx - sz + hh, cy - hh, cx - sz + hh, cy + hh, col);
        g->fillRect(cx - sz + hh, cy - t / 2, sz * 2 - hh, t, col);
        break;
    case Arrow::Right:
        g->fillTriangle(cx + sz, cy, cx + sz - hh, cy - hh, cx + sz - hh, cy + hh, col);
        g->fillRect(cx - sz, cy - t / 2, sz * 2 - hh, t, col);
        break;
    case Arrow::TurnLeft:
    case Arrow::TurnRight: {
        // Up from the bottom, then across with the head at the end
        const int dir = a == Arrow::TurnLeft ? -1 : 1;
        const int sx = cx - dir * sz / 2;            // the upright stroke
        const int ty = cy - sz / 3;                  // the cross stroke
        g->fillRect(sx - t / 2, ty - t / 2, t, cy + sz - ty + t / 2, col);
        const int ex = cx + dir * (sz - hh);
        g->fillRect(dir < 0 ? ex : sx - t / 2, ty - t / 2, dir < 0 ? sx + t / 2 - ex : ex - sx + t / 2, t, col);
        g->fillTriangle(cx + dir * sz, ty, ex, ty - hh, ex, ty + hh, col);
        break;
    }
    case Arrow::TurnAround: {
        // Up the right, across the top, down the left with the head
        const int rx = cx + sz / 2, lx = cx - sz / 2, top = cy - sz * 2 / 3;
        g->fillRect(rx - t / 2, top, t, cy + sz - top, col);
        g->fillRect(lx - t / 2, top, rx - lx + t, t, col);
        g->fillRect(lx - t / 2, top, t, cy + sz - hh - top, col);
        g->fillTriangle(lx, cy + sz, lx - hh, cy + sz - hh, lx + hh, cy + sz - hh, col);
        break;
    }
    }
}

uint16_t key_fill(KeyStyle s)
{
    return s == KeyStyle::Lit ? style::kKeyLit : s == KeyStyle::Dim ? style::kKeyDim : style::kKey;
}

void key2(const Rect& r, const char* label, const char* sub, KeyStyle s, int left_inset)
{
    const uint16_t fill = key_fill(s);
    const int rad = large() ? 6 : 4;
    g->fillRoundRect(r.x, r.y, r.w, r.h, rad, fill);
    g->drawRoundRect(r.x, r.y, r.w, r.h, rad, s == KeyStyle::Lit ? style::kGold : style::kKeyEdge);
    const int lh = line_h(Font::Normal), sh = line_h(Font::Small);
    const int top = r.y + (r.h - lh - sh - 2) / 2;
    use_font(Font::Normal);
    g->setTextColor(s == KeyStyle::Dim ? style::kTextMuted : style::kText);
    g->setTextDatum(textdatum_t::top_center);
    const int cx = r.x + left_inset + (r.w - left_inset) / 2;
    g->drawString(label, cx, top);
    use_font(Font::Small);
    g->setTextColor(s == KeyStyle::Lit ? style::kText : style::kTextMuted);
    g->drawString(sub, cx, top + lh + 2);
}

void text(int x, int y, const char* s, uint16_t color, Font f)
{
    use_font(f);
    g->setTextColor(color);
    g->setTextDatum(textdatum_t::top_left);
    g->drawString(s, x, y);
}

void text_center(const Rect& r, const char* s, uint16_t color, Font f)
{
    use_font(f);
    g->setTextColor(color);
    g->setTextDatum(textdatum_t::middle_center);
    g->drawString(s, r.x + r.w / 2, r.y + r.h / 2);
}

int text_width(const char* s, Font f)
{
    use_font(f);
    return g->textWidth(s);
}

int line_h(Font f)
{
    use_font(f);
    return g->fontHeight();
}

Rect bottom_key(int i, int n)
{
    const int gp = gap();
    const int w = (width() - gp * (n + 1)) / n;
    const int h = key_h();
    return {gp + i * (w + gp), height() - h - gp, w, h};
}

Rect grid_cell(int i, int cols, int rows, bool leave_bottom_row)
{
    const int gp = gap();
    const int top = header_h() + gp;
    const int bottom = leave_bottom_row ? height() - key_h() - gp * 2 : height() - gp;
    const int w = (width() - gp * (cols + 1)) / cols;
    const int h = (bottom - top - gp * (rows - 1)) / rows;
    const int c = i % cols, r = i / cols;
    return {gp + c * (w + gp), top + r * (h + gp), w, h};
}

} // namespace ui

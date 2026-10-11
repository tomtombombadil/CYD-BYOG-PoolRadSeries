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

// The keys on screen (for the tap highlight): rect and style, since the
// last clear(); 255 = the header's back key
struct KeyRec {
    Rect     r;
    uint8_t  style;
    uint32_t gen;               // key_gen when it was last drawn
};
constexpr int kMaxKeys = 56;              // (the keyboard: 40 letters + Del, Space, Enter, and the row)
KeyRec   keys[kMaxKeys];
int      n_keys = 0;
uint32_t key_gen = 0;           // counts key drawing (a key redrawn since a flash?)
uint32_t clear_gen = 0;         // key_gen at the last clear()

void note_key(const Rect& r, uint8_t style)
{
    ++key_gen;
    for (int i = 0; i < n_keys; ++i)
        if (keys[i].r.x == r.x && keys[i].r.y == r.y && keys[i].r.w == r.w && keys[i].r.h == r.h) {
            keys[i].style = style;
            keys[i].gen = key_gen;
            return;
        }
    if (n_keys < kMaxKeys) keys[n_keys++] = {r, style, key_gen};
}

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

void clear()
{
    g->fillScreen(style::kBackground);
    n_keys = 0;
    clear_gen = ++key_gen;
}

void clear_area(const Rect& r)
{
    g->fillRect(r.x, r.y, r.w, r.h, style::kBackground);
    int n = 0;
    for (int i = 0; i < n_keys; ++i) {
        const Rect& k = keys[i].r;
        const bool inside = k.x >= r.x && k.y >= r.y && k.x + k.w <= r.x + r.w && k.y + k.h <= r.y + r.h;
        if (!inside) keys[n++] = keys[i];
    }
    n_keys = n;
    ++key_gen;
}

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
        note_key(r, 255);
    }
    use_font(Font::Normal);
    g->setTextColor(style::kText);
    g->setTextDatum(textdatum_t::middle_left);
    g->drawString(title, tx, h / 2);
}

void key(const Rect& r, const char* label, KeyStyle s)
{
    note_key(r, static_cast<uint8_t>(s));
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

void key_small(const Rect& r, const char* label, KeyStyle s)
{
    key(r, "", s);
    const uint16_t col = s == KeyStyle::Dim ? style::kTextMuted : style::kText;
    const char* nl = strchr(label, '\n');
    if (!nl) {
        text_center(r, label, col, Font::Small);
        return;
    }
    // Two small lines (a narrow key)
    char first[24];
    const size_t n = static_cast<size_t>(nl - label) < sizeof first - 1 ? static_cast<size_t>(nl - label) : sizeof first - 1;
    memcpy(first, label, n);
    first[n] = 0;
    use_font(Font::Small);
    const int lh = g->fontHeight() + 2;
    const int top = r.y + (r.h - lh * 2 + 2) / 2;
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

void key_compass(const Rect& r, int dir, KeyStyle s)
{
    key(r, "", s);
    const uint16_t col = s == KeyStyle::Dim ? style::kTextMuted : style::kGold;
    const float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f;
    float sz = (r.w < r.h ? r.w : r.h) * 0.32f;
    if (sz < 6) sz = 6;
    const float t = sz / 3 < 3 ? 3 : sz / 3;        // line thickness
    const float hh = sz * 2 / 3;                     // arrow head half size
    static const float kX[8] = {0, 0.7071f, 1, 0.7071f, 0, -0.7071f, -1, -0.7071f};
    static const float kY[8] = {-1, -0.7071f, 0, 0.7071f, 1, 0.7071f, 0, -0.7071f};
    const float ux = kX[dir & 7], uy = kY[dir & 7], px = -uy, py = ux;
    const float tipx = cx + ux * sz, tipy = cy + uy * sz;
    const float bx = tipx - ux * hh * 1.3f, by = tipy - uy * hh * 1.3f;   // the head's base
    const float tx = cx - ux * sz, ty = cy - uy * sz;                     // the tail
    auto P = [](float v) { return static_cast<int32_t>(v + (v < 0 ? -0.5f : 0.5f)); };
    const float h = t / 2;
    g->fillTriangle(P(tx + px * h), P(ty + py * h), P(tx - px * h), P(ty - py * h), P(bx + px * h), P(by + py * h), col);
    g->fillTriangle(P(tx - px * h), P(ty - py * h), P(bx - px * h), P(by - py * h), P(bx + px * h), P(by + py * h), col);
    g->fillTriangle(P(tipx), P(tipy), P(bx + px * hh), P(by + py * hh), P(bx - px * hh), P(by - py * hh), col);
}

uint16_t key_fill(KeyStyle s)
{
    return s == KeyStyle::Lit ? style::kKeyLit : s == KeyStyle::Dim ? style::kKeyDim : style::kKey;
}

void key2(const Rect& r, const char* label, const char* sub, KeyStyle s, int left_inset)
{
    note_key(r, static_cast<uint8_t>(s));
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

void key_big(const Rect& r, const char* label, const char* sub, KeyStyle s)
{
    note_key(r, static_cast<uint8_t>(s));
    const uint16_t fill = key_fill(s);
    const int rad = large() ? 8 : 6;
    g->fillRoundRect(r.x, r.y, r.w, r.h, rad, fill);
    g->drawRoundRect(r.x, r.y, r.w, r.h, rad, s == KeyStyle::Lit ? style::kGold : style::kKeyEdge);
    const int lh = line_h(Font::Large), sh = line_h(Font::Small);
    const int top = r.y + (r.h - lh - sh - 4) / 2;
    use_font(Font::Large);
    g->setTextColor(s == KeyStyle::Dim ? style::kTextMuted : style::kGold);
    g->setTextDatum(textdatum_t::top_center);
    g->drawString(label, r.x + r.w / 2, top);
    use_font(Font::Small);
    g->setTextColor(s == KeyStyle::Lit ? style::kText : style::kTextMuted);
    g->drawString(sub, r.x + r.w / 2, top + lh + 4);
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

// ---- tap highlight ------------------------------------------------------------
// (Tom, 2026-10-09: resistive screens are finicky and the engine can be
// slow to answer, so a tapped key lights up FIRST; one already lit blinks
// off and on)

namespace {

uint32_t flash_gen = 0;
int      flash_key = -1;
Rect     flash_rect{};

void ring(const Rect& r, uint16_t outer, uint16_t inner, int width)
{
    const int rad = large() ? 6 : 4;
    g->drawRoundRect(r.x, r.y, r.w, r.h, rad, outer);
    for (int i = 1; i < width; ++i) g->drawRoundRect(r.x + i, r.y + i, r.w - i * 2, r.h - i * 2, rad, inner);
}

uint16_t fill_of(uint8_t st)
{
    return st == 255 ? style::kHeader : key_fill(static_cast<KeyStyle>(st));
}

} // namespace

int tap_flash(const Tap& t)
{
    flash_key = -1;
    for (int i = n_keys - 1; i >= 0; --i) {
        const KeyRec& k = keys[i];
        if (!k.r.contains(t.x, t.y) || k.style == static_cast<uint8_t>(KeyStyle::Dim)) continue;
        const int w = large() ? 4 : 3;
        g->startWrite();
        if (k.style == static_cast<uint8_t>(KeyStyle::Lit)) {
            // already lit: off, then on again
            ring(k.r, fill_of(k.style), fill_of(k.style), w);
            g->endWrite();
            delay(70);
            g->startWrite();
        }
        ring(k.r, style::kText, style::kGold, w);
        g->endWrite();
        flash_key = i;
        flash_rect = k.r;
        flash_gen = key_gen;
        return i;
    }
    return -1;
}

void tap_unflash()
{
    // Put the key back as it was - unless the screen was cleared or that
    // key drawn again since (v0.53.0: other keys drawn meanwhile, e.g. the
    // Companion's keys after a step, used to leave the ring on for good)
    int at = -1;
    if (flash_key >= 0 && clear_gen <= flash_gen)
        for (int i = 0; i < n_keys; ++i)
            if (keys[i].r.x == flash_rect.x && keys[i].r.y == flash_rect.y && keys[i].r.w == flash_rect.w &&
                keys[i].r.h == flash_rect.h)
                at = i;
    if (at < 0 || keys[at].gen > flash_gen) {
        flash_key = -1;
        return;
    }
    const KeyRec& k = keys[at];
    const uint16_t edge = k.style == 255 ? style::kHeader
                        : k.style == static_cast<uint8_t>(KeyStyle::Lit) ? style::kGold : style::kKeyEdge;
    g->startWrite();
    ring(k.r, edge, fill_of(k.style), large() ? 4 : 3);
    g->endWrite();
    flash_key = -1;
}

} // namespace ui

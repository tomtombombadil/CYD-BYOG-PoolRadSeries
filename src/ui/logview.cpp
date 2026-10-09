#include "logview.h"

#include <cstdlib>
#include <cstring>

#include "hal/sdcard.h"

namespace logview {

namespace {

struct Line {
    uint32_t at;
    uint16_t n;
};

char*    text_ = nullptr;      // the file's text
size_t   size_ = 0;
Line*    lines_ = nullptr;
int      n_lines = 0, cap_lines = 0;
int      top_ = 0;             // the first line shown
bool     at_end_ = false;
int      drag_px = 0;          // drag movement not yet turned into lines
ui::Rect area_{};
bool     wrapped = false;      // lines_ made for area_'s width

int lh() { return ui::line_h(ui::Font::Small) + 2; }
int rows() { return area_.h > 0 ? (area_.h - 4) / lh() : 1; }
int max_top() { return n_lines > rows() ? n_lines - rows() : 0; }

bool add_line(uint32_t at, size_t n)
{
    if (n_lines == cap_lines) {
        const int cap = cap_lines ? cap_lines * 2 : 256;
        Line* l = static_cast<Line*>(realloc(lines_, sizeof(Line) * cap));
        if (!l) return false;
        lines_ = l;
        cap_lines = cap;
    }
    lines_[n_lines++] = {at, static_cast<uint16_t>(n)};
    return true;
}

// Splits the text into screen lines that fit the box (at spaces when it can)
void wrap()
{
    n_lines = 0;
    wrapped = true;
    if (!text_) return;
    int w[96];
    for (int c = 32; c < 128; ++c) {
        const char s[2] = {static_cast<char>(c), 0};
        w[c - 32] = ui::text_width(s, ui::Font::Small);
    }
    const int room = area_.w - 14;          // the scroll bar's strip at the right
    size_t i = 0;
    while (i < size_) {
        size_t start = i, last_space = 0;
        int x = 0;
        while (i < size_ && text_[i] != '\n') {
            const unsigned char c = static_cast<unsigned char>(text_[i]);
            const int cw = c >= 32 && c < 128 ? w[c - 32] : w[0];
            if (x + cw > room && i > start) break;
            if (c == ' ') last_space = i;
            x += cw;
            ++i;
        }
        if (i < size_ && text_[i] != '\n' && last_space > start) i = last_space + 1;   // break at the space
        size_t n = i - start;
        while (n > 0 && text_[start + n - 1] == ' ') --n;
        if (n > 300) n = 300;
        if (!add_line(static_cast<uint32_t>(start), n)) break;
        if (i < size_ && text_[i] == '\n') ++i;
    }
    top_ = at_end_ ? max_top() : 0;
}

void clean()
{
    for (size_t i = 0; i < size_; ++i) {
        const unsigned char c = static_cast<unsigned char>(text_[i]);
        if (c == '\t') text_[i] = ' ';
        else if (c == '\r') text_[i] = ' ';
        else if (c != '\n' && (c < 32 || c >= 127)) text_[i] = '?';
    }
}

} // namespace

void close()
{
    free(text_);
    free(lines_);
    text_ = nullptr;
    lines_ = nullptr;
    size_ = 0;
    n_lines = cap_lines = 0;
    top_ = 0;
    wrapped = false;
}

void open_text(const char* text, bool at_end)
{
    close();
    at_end_ = at_end;
    const size_t n = strlen(text);
    text_ = static_cast<char*>(malloc(n + 1));
    if (!text_) return;
    memcpy(text_, text, n + 1);
    size_ = n;
    clean();
}

bool open_file(const char* path, bool at_end)
{
    close();
    at_end_ = at_end;
    fs::File f = sd_fs().open(path, "r");
    if (!f) return false;
    size_t n = f.size();
    size_t skip = 0;
    static const char kCut[] = "(earlier lines left out)\n";
    if (n > kMaxBytes) {
        skip = n - kMaxBytes;
        n = kMaxBytes;
    }
    const size_t pre = skip ? sizeof kCut - 1 : 0;
    text_ = static_cast<char*>(malloc(n + pre + 1));
    if (!text_) {
        f.close();
        return false;
    }
    f.seek(skip);
    size_t got = f.read(reinterpret_cast<uint8_t*>(text_ + pre), n);
    f.close();
    if (skip) {
        // from the next whole line on
        size_t k = 0;
        while (k < got && text_[pre + k] != '\n') ++k;
        if (k < got) ++k;
        memmove(text_ + pre, text_ + pre + k, got - k);
        got -= k;
        memcpy(text_, kCut, pre);
    }
    size_ = pre + got;
    text_[size_] = 0;
    clean();
    return true;
}

void set_area(const ui::Rect& r)
{
    if (r.w != area_.w || r.h != area_.h) wrapped = false;
    area_ = r;
}

void draw()
{
    if (!wrapped) wrap();
    LGFX& g = ui::gfx();
    g.fillRect(area_.x, area_.y, area_.w, area_.h, style::kBackground);
    g.drawRect(area_.x, area_.y, area_.w, area_.h, style::kKeyEdge);
    if (!text_ || n_lines == 0) {
        ui::text(area_.x + 6, area_.y + 4, text_ ? "(empty)" : "(not on the card)", style::kTextMuted, ui::Font::Small);
        return;
    }
    if (top_ > max_top()) top_ = max_top();
    if (top_ < 0) top_ = 0;
    char buf[304];
    const int r = rows();
    for (int i = 0; i < r && top_ + i < n_lines; ++i) {
        const Line& l = lines_[top_ + i];
        memcpy(buf, text_ + l.at, l.n);
        buf[l.n] = 0;
        ui::text(area_.x + 5, area_.y + 3 + i * lh(), buf, style::kText, ui::Font::Small);
    }
    // The scroll bar: where the shown lines are in the whole
    if (n_lines > r) {
        const int bh = area_.h - 6;
        int th = bh * r / n_lines;
        if (th < 10) th = 10;
        const int ty = area_.y + 3 + (bh - th) * top_ / max_top();
        g.fillRect(area_.x + area_.w - 7, area_.y + 3, 4, bh, style::kKeyDim);
        g.fillRect(area_.x + area_.w - 7, ty, 4, th, style::kGold);
    }
}

bool tap(const ui::Tap& t)
{
    if (!area_.contains(t.x, t.y)) return false;
    const int page = rows() > 1 ? rows() - 1 : 1;
    const int before = top_;
    top_ += t.y < area_.y + area_.h / 2 ? -page : page;
    if (top_ > max_top()) top_ = max_top();
    if (top_ < 0) top_ = 0;
    if (top_ != before) draw();
    return true;
}

void tick()
{
    int dx, dy;
    if (!ui::drag(dx, dy)) {
        drag_px = 0;
        return;
    }
    drag_px += dy;
    const int step = lh();
    int move = 0;
    while (drag_px >= step) {         // dragging down shows earlier lines
        drag_px -= step;
        --move;
    }
    while (drag_px <= -step) {
        drag_px += step;
        ++move;
    }
    if (!move) return;
    const int before = top_;
    top_ += move;
    if (top_ > max_top()) top_ = max_top();
    if (top_ < 0) top_ = 0;
    if (top_ != before) draw();
}

} // namespace logview

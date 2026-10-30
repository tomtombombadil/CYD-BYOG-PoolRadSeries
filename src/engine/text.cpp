#include "text.h"

#include <cctype>
#include <cstring>

namespace text {

namespace {

bool printable(const char* s, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char>(s[i]) < 0x20 || static_cast<unsigned char>(s[i]) > 0x7E) return false;
    return true;
}

bool punct(char c) { return c && strchr("!,-.:;?", c) != nullptr; }


bool is_key(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

// The word starting at pos: punctuation, letters, punctuation, one space
int hex_digit(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
// A colour code at s[pos] (Writer::codes): "%" and a hex digit
bool code_at(const char* s, int len, int pos) { return pos + 1 < len && s[pos] == '%' && hex_digit(s[pos + 1]) >= 0; }

int unit_end(const char* s, int len, int pos)
{
    int e = pos;
    while (e < len && punct(s[e])) ++e;
    while (e < len && !punct(s[e]) && s[e] != ' ') ++e;
    while (e < len && punct(s[e])) ++e;
    if (e < len && s[e] == ' ') ++e;
    return e > pos ? e : pos + 1;
}

} // namespace

// ---- Strings ---------------------------------------------------------------

namespace {

// A C string at pos (text::kCString): up to cap - 1 printable characters
// before its NUL - false if it has none, no NUL in reach, or others
bool read_c(dax::ByteSource& src, uint32_t pos, char* out, size_t cap, bool word1 = false)
{
    out[0] = 0;
    uint8_t buf[kMaxString];
    const size_t want = cap < sizeof buf ? cap : sizeof buf;
    const size_t got = src.read_at(pos, buf, want);
    size_t n = 0;
    while (n < got && buf[n] && !(word1 && buf[n] == ' ')) ++n;     // (kWord1: a space ends it too)
    if (n == 0 || n == got) return false;
    // (a tab ends the word: Dark Queen's "THAC0\t " lays out a column)
    for (size_t k = 0; k < n; ++k)
        if (buf[k] == '\t') n = k;
    if (n == 0) return false;
    memcpy(out, buf, n);
    out[n] = 0;
    if (!printable(out, n)) {
        out[0] = 0;
        return false;
    }
    return true;
}

} // namespace

bool read_pascal(dax::ByteSource& src, uint32_t pos, char* out, size_t cap)
{
    if (cap == 0) return false;
    out[0] = 0;
    if (pos & kCString) return read_c(src, pos & ~(kCString | kWord1), out, cap, (pos & kWord1) != 0);
    uint8_t n;
    if (src.read_at(pos, &n, 1) != 1 || n == 0 || static_cast<size_t>(n) + 1 > cap) return false;
    if (src.read_at(pos + 1, reinterpret_cast<uint8_t*>(out), n) != n) return false;
    out[n] = 0;
    if (!printable(out, n)) {
        out[0] = 0;
        return false;
    }
    return true;
}

size_t Words::read_at(uint32_t pos, uint8_t* buf, size_t n)
{
    if (!(pos & kInProgram)) return ovr_.read_at(pos, buf, n);
    pos &= ~kInProgram;
    if (!exe_ || !info_ || pos >= info_->image_size) return 0;
    if (pos < win_at_ || pos + n > win_at_ + win_len_) {
        uint32_t want = info_->image_size - pos;
        if (want > sizeof win_) want = sizeof win_;
        win_len_ = 0;
        if (exepack::read(*exe_, *info_, pos, win_, want) != exepack::Status::Ok) return 0;
        win_at_ = pos;
        win_len_ = want;
    }
    const uint32_t have = win_at_ + win_len_ - pos;
    if (n > have) n = have;
    memcpy(buf, win_ + (pos - win_at_), n);
    return n;
}

bool read_pascal(dax::ByteSource& exe, const exepack::Info& info, uint32_t pos, char* out, size_t cap)
{
    if (cap == 0) return false;
    out[0] = 0;
    pos &= ~kInProgram;                 // (Gateway's version line is at image 0: marked kInProgram so it isn't "none")
    if (pos & kCString) {
        // (a plain program: the image is the file - exepack::read reads it as it is)
        const bool word1 = (pos & kWord1) != 0;
        pos &= ~(kCString | kWord1);
        if (pos >= info.image_size) return false;
        uint8_t buf[kMaxString];
        uint32_t want = info.image_size - pos;
        if (want > sizeof buf) want = sizeof buf;
        if (exepack::read(exe, info, pos, buf, want) != exepack::Status::Ok) return false;
        dax::MemorySource m(buf, want);
        return read_c(m, 0, out, cap, word1);
    }
    if (pos >= info.image_size) return false;
    uint8_t buf[kMaxString];
    uint32_t want = info.image_size - pos;
    if (want > sizeof buf) want = sizeof buf;
    if (exepack::read(exe, info, pos, buf, want) != exepack::Status::Ok) return false;
    const uint8_t n = buf[0];
    if (n == 0 || n + 1u > want || static_cast<size_t>(n) + 1 > cap) return false;
    memcpy(out, buf + 1, n);
    out[n] = 0;
    if (!printable(out, n)) {
        out[0] = 0;
        return false;
    }
    return true;
}

// ---- Text windows ----------------------------------------------------------

void clear(pic::Canvas& c, const Region& r)
{
    c.fill(r.x0 * 8, r.y0 * 8, (r.x1 - r.x0 + 1) * 8, (r.y1 - r.y0 + 1) * 8, 0);
}

void begin(Writer& w, pic::Canvas& c, const char* s, const Region& r, uint8_t fg, bool clear_area)
{
    w.r = r;
    w.fg = fg;
    w.codes = false;
    w.s = s;
    w.len = s ? static_cast<int>(strlen(s)) : 0;
    w.pos = 0;
    w.unit_end = 0;
    if (clear_area) {
        clear(c, r);
        w.col = r.x0;
        w.row = r.y0;
    } else if (w.col < r.x0 || w.col > r.x1 || w.row < r.y0 || w.row > r.last_row()) {
        w.col = r.x0;
        w.row = r.y0;
    }
    w.state = w.len > 0 ? State::Writing : State::Done;
}

namespace {
bool g_hang = false;
}
void set_hanging_space(bool on) { g_hang = on; }

State step(Writer& w, pic::Canvas& c, const font::Font& f, int n)
{
    if (w.state != State::Writing) return w.state;
    const Region& r = w.r;
    while (n != 0) {
        if (w.pos >= w.len) {
            if (w.col > r.x1) {
                w.col = r.x0;
                ++w.row;
            }
            return w.state = State::Done;
        }
        if (w.codes && code_at(w.s, w.len, w.pos)) {
            w.fg = static_cast<uint8_t>(hex_digit(w.s[w.pos + 1]));
            w.pos += 2;
            continue;
        }
        if (w.pos >= w.unit_end) {
            const int e = unit_end(w.s, w.len, w.pos);
            int width = e - w.pos;
            for (int k = w.pos; w.codes && k + 1 < e; ++k)
                if (code_at(w.s, w.len, k)) {
                    width -= 2;
                    ++k;
                }
            // (Treasures: a word's trailing space may run off the line - DOSBox;
            // Gateway too: a word fits when its letters do, set_hanging_space)
            if ((w.codes || g_hang) && width > 1 && w.s[e - 1] == ' ') --width;
            if (w.col + width - 1 > r.x1) {
                if (w.col == r.x0) {
                    // Longer than a line: as much as fits, the rest below
                    w.unit_end = w.pos + (r.x1 - r.x0 + 1);
                } else {
                    w.col = r.x0;
                    ++w.row;
                    while (w.pos < w.len && w.s[w.pos] == ' ') ++w.pos;
                    if (w.pos >= w.len) continue;
                    if (w.row > r.last_row()) return w.state = State::PageFull;
                    continue;
                }
            } else {
                w.unit_end = e;
            }
        }
        if (w.col <= r.x1 || w.s[w.pos] != ' ')
            font::draw_glyph(c, f, font::glyph_of(f, w.s[w.pos]), w.col * 8, r.line_y(w.row), w.fg, 0);
        ++w.pos;
        ++w.col;
        if (n > 0) --n;
    }
    return w.state;
}

void next_page(Writer& w, pic::Canvas& c)
{
    if (w.state != State::PageFull) return;
    clear(c, w.r);
    w.col = w.r.x0;
    w.row = w.r.y0;
    w.state = State::Writing;
}

// ---- Menu line -------------------------------------------------------------

void build(Menu& m, const char* prompt, const char* s)
{
    m = Menu{};
    strncpy(m.prompt, prompt ? prompt : "", sizeof m.prompt - 1);
    if (s && strchr(s, '~')) {
        // Gateway's menus mark each choice's key with '~' ("~Yes ~No",
        // "~Press Enter to continue."): a choice runs from its mark to the
        // next; only the marked letter is lit
        m.marked = true;
        int o = 0;
        for (const char* q = s; *q && o < static_cast<int>(sizeof m.s) - 1; ++q) {
            if (*q == '~') {
                if (m.count >= kMaxItems) break;
                if (m.count > 0) {
                    int e = o - 1;
                    while (e > m.start[m.count - 1] && m.s[e] == ' ') --e;
                    m.end[m.count - 1] = static_cast<int8_t>(e);
                }
                m.start[m.count++] = static_cast<int8_t>(o);
                continue;
            }
            m.s[o++] = *q;
        }
        m.s[o] = 0;
        if (m.count > 0) {
            int e = o - 1;
            while (e > m.start[m.count - 1] && m.s[e] == ' ') --e;
            m.end[m.count - 1] = static_cast<int8_t>(e);
        }
        return;
    }
    strncpy(m.s, s ? s : "", sizeof m.s - 1);
    const int len = static_cast<int>(strlen(m.s));
    for (int i = 0; i < len && m.count < kMaxItems; ++i) {
        if (!is_key(m.s[i])) continue;
        if (m.count > 0) {
            int e = i - 1;
            while (e > m.start[m.count - 1] && (m.s[e] == ' ' || m.s[e] == kRowBreak)) --e;
            m.end[m.count - 1] = static_cast<int8_t>(e);
        }
        m.start[m.count++] = static_cast<int8_t>(i);
    }
    if (m.count > 0) {
        int e = len - 1;
        while (e > m.start[m.count - 1] && m.s[e] == ' ') --e;
        m.end[m.count - 1] = static_cast<int8_t>(e);
    }
}

bool two_rows(const Menu& m) { return strchr(m.s, kRowBreak) != nullptr; }

namespace {
int g_origin = 0;
// Real black (a fill with 0 would give Dark Queen's paper)
void hline(pic::Canvas& c, int x0, int x1, int y, uint8_t v)
{
    if (y < 0 || y >= c.h) return;
    for (int x = x0 < 0 ? 0 : x0; x < x1 && x < c.w; ++x) c.set(x, y, v);
}
void vline(pic::Canvas& c, int x, int y0, int y1, uint8_t v)
{
    if (x < 0 || x >= c.w) return;
    for (int y = y0 < 0 ? 0 : y0; y < y1 && y < c.h; ++y) c.set(x, y, v);
}
void solid(pic::Canvas& c, int x0, int y0, int x1, int y1, uint8_t v)
{
    for (int y = y0; y < y1; ++y) hline(c, x0, x1, y, v);
}
// The button rows: a button's top 1 pixel above its text, 11 high
constexpr int kAbove = 1, kHigh = 11;

void draw_buttons(pic::Canvas& c, const font::Font& f, const Menu& m, const MenuColors& col)
{
    const int c0 = line_col(m), plen = static_cast<int>(strlen(m.prompt));
    int y = col.y_text;
    auto text_at = [&](const char* s, int n, int at, uint8_t fg, uint8_t bg, bool key_white) {
        for (int i = 0; i < n && at + i < font::kCols; ++i) {
            if (s[i] == ' ' && bg == 0xFF) continue;
            const bool white = key_white && i == 0;
            font::draw_glyph(c, f, font::glyph_of(f, s[i]), (at + i) * 8, y, white ? col.highlight : fg, bg);
        }
    };
    // A prompt part: black letters in a black-edged box
    auto box = [&](const char* s, int at) {
        int n = static_cast<int>(strlen(s));
        while (n > 0 && s[n - 1] == ' ') --n;
        if (n <= 0) return;
        const int x0 = at * 8, x1 = (at + n) * 8;
        solid(c, x0, y, x1, y + 9, col.b_box);
        hline(c, x0 - 1, x1 + 1, y - 1, 0);
        hline(c, x0 - 1, x1 + 1, y + 9, 0);
        vline(c, x0 - 1, y - 1, y + 10, 0);
        vline(c, x1, y - 1, y + 10, 0);
        text_at(s, n, at, col.b_box_ink, col.b_box, false);
    };
    const bool two = two_rows(m);
    if (plen && !two) box(m.prompt, c0);
    const int sel = m.selected >= 0 && m.selected < m.count ? m.selected : -1;
    for (int k = 0; k < m.count; ++k) {
        int x0, y0, x1, y1;
        button_box(m, k, &x0, &y0, &x1, &y1, col);
        bevel(c, x0, y0, x1, y1, col.b_light, col.b_fill, false);
        const int n = m.end[k] - m.start[k] + 1;
        int at = c0 + plen + m.start[k];
        if (two) {
            int row;
            item_cell(m, k, &at, &row);
            y = two_row_y(col, row);
        }
        if (k == sel) text_at(m.s + m.start[k], n, at, 0, col.highlight, false);     // (the engine's white box)
        else text_at(m.s + m.start[k], n, at, m.lit ? col.highlight : col.normal, col.b_fill, true);
    }
    if (m.tail[0] && m.count) box(m.tail, c0 + plen + m.end[m.count - 1] + 1);
}
} // namespace

void set_menu_origin(int col) { g_origin = col; }
int  menu_origin() { return g_origin; }
int  line_col(const Menu& m) { return m.col0 >= 0 ? m.col0 : g_origin; }
int  word_col(const Menu& m, int k)
{
    return line_col(m) + static_cast<int>(strlen(m.prompt)) + (k >= 0 && k < m.count ? m.start[k] : 0);
}

void button_box(const Menu& m, int k, int* x0, int* y0, int* x1, int* y1, const MenuColors& col)
{
    int a = word_col(m, k), y = col.y_text;
    if (two_rows(m)) {
        int row;
        item_cell(m, k, &a, &row);
        y = two_row_y(col, row);
    }
    const int b = a + (k >= 0 && k < m.count ? m.end[k] - m.start[k] : 0);
    *x0 = a * 8 - 4;
    *x1 = (b + 1) * 8 + 4;
    *y0 = y - kAbove;
    *y1 = *y0 + kHigh;
}

int two_row_y(const MenuColors& col, int row) { return row == kMenuRow - 1 ? col.y_text - 10 : col.y_text + 2; }

void bevel(pic::Canvas& c, int x0, int y0, int x1, int y1, uint8_t light, uint8_t fill, bool sunken)
{
    solid(c, x0 + 1, y0 + 1, x1 - 1, y1 - 1, fill);
    const uint8_t tl = sunken ? 0 : light, br = sunken ? light : 0;
    hline(c, x0, x1 - 1, y0, tl);
    vline(c, x0, y0, y1 - 1, tl);
    hline(c, x0 + 1, x1, y1 - 1, br);
    vline(c, x1 - 1, y0 + 1, y1, br);
}

void draw(pic::Canvas& c, const font::Font& f, const Menu& m, const MenuColors& col)
{
    if (col.buttons) {
        draw_buttons(c, f, m, col);
        return;
    }
    const bool two = two_rows(m);
    if (two) {
        // Two rows: row 23's choices, then row 24's (no prompt)
        c.fill(0, (kMenuRow - 1) * 8, pic::kScreenW, 16, 0);
        const int sel = m.selected >= 0 && m.selected < m.count ? m.selected : -1;
        int x = line_col(m), row = kMenuRow - 1;
        for (int i = 0; m.s[i]; ++i) {
            if (m.s[i] == kRowBreak) {
                row = kMenuRow;
                x = line_col(m);
                continue;
            }
            if (x >= font::kCols) continue;
            const int g = font::glyph_of(f, m.s[i]);
            if (sel >= 0 && i >= m.start[sel] && i <= m.end[sel])
                font::draw_glyph(c, f, g, x * 8, row * 8, col.plain_sel ? col.highlight : 0, col.plain_sel ? 0 : col.highlight);
            else
                font::draw_glyph(c, f, g, x * 8, row * 8, is_key(m.s[i]) ? col.highlight : col.normal, 0);
            ++x;
        }
        return;
    }
    c.fill(0, kMenuRow * 8, pic::kScreenW, 8, 0);
    int x = line_col(m);
    for (const char* p = m.prompt; *p && x < font::kCols; ++p, ++x)
        font::draw_glyph(c, f, font::glyph_of(f, *p), x * 8, kMenuRow * 8, m.prompt_fg ? m.prompt_fg : col.prompt, 0);
    const int sel = m.selected >= 0 && m.selected < m.count ? m.selected : -1;
    for (int i = 0; m.s[i] && x < font::kCols; ++i, ++x) {
        const int g = font::glyph_of(f, m.s[i]);
        if (sel >= 0 && i >= m.start[sel] && i <= m.end[sel]) {
            font::draw_glyph(c, f, g, x * 8, kMenuRow * 8, col.plain_sel ? col.highlight : 0, col.plain_sel ? 0 : col.highlight);
        } else {
            bool lit = is_key(m.s[i]);
            if (m.marked) {
                lit = false;
                for (int k = 0; k < m.count; ++k)
                    if (m.start[k] == i) lit = true;
            }
            font::draw_glyph(c, f, g, x * 8, kMenuRow * 8, lit ? col.highlight : col.normal, 0);
        }
    }
}

int hit_row(const Menu& m, int col, int row)
{
    const char* br = strchr(m.s, kRowBreak);
    if (!br) return row == kMenuRow ? hit(m, col) : -1;
    const int split = static_cast<int>(br - m.s);
    // The row's own columns: row 23's from 0, row 24's from just after the break
    const int at = col - line_col(m);
    const int rel = row == kMenuRow ? at + split + 1 : at;
    int first = -1, last = -1;
    for (int k = 0; k < m.count; ++k) {
        const bool second = m.start[k] > split;
        if (second != (row == kMenuRow)) continue;
        if (first < 0) first = k;
        last = k;
    }
    for (int k = first; k >= 0 && k <= last; ++k) {
        const int hi = k < last ? m.start[k + 1] - 1 : (row == kMenuRow ? m.end[k] : split - 1);
        if (rel >= m.start[k] && rel <= hi) return k;
    }
    return -1;
}

void item_cell(const Menu& m, int k, int* col, int* row)
{
    const char* br = strchr(m.s, kRowBreak);
    const int at = k >= 0 && k < m.count ? m.start[k] : 0;
    if (br && at < br - m.s) {
        *col = line_col(m) + at;
        *row = kMenuRow - 1;
    } else if (br) {
        *col = line_col(m) + at - static_cast<int>(br - m.s) - 1;
        *row = kMenuRow;
    } else {
        *col = line_col(m) + static_cast<int>(strlen(m.prompt)) + at;
        *row = kMenuRow;
    }
}

int hit(const Menu& m, int col)
{
    const int rel = col - line_col(m) - static_cast<int>(strlen(m.prompt));
    for (int k = 0; k < m.count; ++k) {
        const int hi = k + 1 < m.count ? m.start[k + 1] - 1 : m.end[k];
        if (rel >= m.start[k] && rel <= hi) return k;
    }
    return -1;
}

char key(const Menu& m, int item)
{
    return item >= 0 && item < m.count ? m.s[m.start[item]] : 0;
}

} // namespace text

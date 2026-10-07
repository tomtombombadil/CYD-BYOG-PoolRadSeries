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

bool read_pascal(dax::ByteSource& src, uint32_t pos, char* out, size_t cap)
{
    if (cap == 0) return false;
    out[0] = 0;
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

bool read_pascal(dax::ByteSource& exe, const exepack::Info& info, uint32_t pos, char* out, size_t cap)
{
    if (cap == 0) return false;
    out[0] = 0;
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
    w.s = s;
    w.len = s ? static_cast<int>(strlen(s)) : 0;
    w.pos = 0;
    w.unit_end = 0;
    if (clear_area) {
        clear(c, r);
        w.col = r.x0;
        w.row = r.y0;
    } else if (w.col < r.x0 || w.col > r.x1 || w.row < r.y0 || w.row > r.y1) {
        w.col = r.x0;
        w.row = r.y0;
    }
    w.state = w.len > 0 ? State::Writing : State::Done;
}

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
        if (w.pos >= w.unit_end) {
            const int e = unit_end(w.s, w.len, w.pos);
            if (w.col + (e - w.pos) - 1 > r.x1) {
                if (w.col == r.x0) {
                    // Longer than a line: as much as fits, the rest below
                    w.unit_end = w.pos + (r.x1 - r.x0 + 1);
                } else {
                    w.col = r.x0;
                    ++w.row;
                    while (w.pos < w.len && w.s[w.pos] == ' ') ++w.pos;
                    if (w.pos >= w.len) continue;
                    if (w.row > r.y1) return w.state = State::PageFull;
                    continue;
                }
            } else {
                w.unit_end = e;
            }
        }
        font::draw_glyph(c, f, font::glyph_of(w.s[w.pos]), w.col * 8, w.row * 8, w.fg, 0);
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
    strncpy(m.s, s ? s : "", sizeof m.s - 1);
    const int len = static_cast<int>(strlen(m.s));
    for (int i = 0; i < len && m.count < kMaxItems; ++i) {
        if (!is_key(m.s[i])) continue;
        if (m.count > 0) {
            int e = i - 1;
            while (e > m.start[m.count - 1] && m.s[e] == ' ') --e;
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

void draw(pic::Canvas& c, const font::Font& f, const Menu& m, const MenuColors& col)
{
    c.fill(0, kMenuRow * 8, pic::kScreenW, 8, 0);
    int x = 0;
    for (const char* p = m.prompt; *p && x < font::kCols; ++p, ++x)
        font::draw_glyph(c, f, font::glyph_of(*p), x * 8, kMenuRow * 8, col.prompt, 0);
    const int sel = m.selected >= 0 && m.selected < m.count ? m.selected : -1;
    for (int i = 0; m.s[i] && x < font::kCols; ++i, ++x) {
        const int g = font::glyph_of(m.s[i]);
        if (sel >= 0 && i >= m.start[sel] && i <= m.end[sel])
            font::draw_glyph(c, f, g, x * 8, kMenuRow * 8, 0, col.highlight);
        else
            font::draw_glyph(c, f, g, x * 8, kMenuRow * 8, is_key(m.s[i]) ? col.highlight : col.normal, 0);
    }
}

int hit(const Menu& m, int col)
{
    const int rel = col - static_cast<int>(strlen(m.prompt));
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

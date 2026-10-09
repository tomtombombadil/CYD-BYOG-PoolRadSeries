#include "keyboard.h"

#include <cstdio>
#include <cstring>

namespace keyboard {

namespace {

char   title_[48];
char   prompt_[48];
char   text_[kMax + 1];
size_t max_ = kMax;
bool   caps = false;
bool   symbols = false;

const char* const kLower[4] = {"1234567890", "qwertyuiop", "asdfghjkl@", "zxcvbnm._-"};
const char* const kUpper[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL@", "ZXCVBNM._-"};
const char* const kSymbols[4] = {"!\"#$%&'()*", "+,-./:;<=>", "?@[\\]^_`{|", "}~"};

enum Special { kShift, kSym, kSpace, kDelete, kDone, kSpecials };
const char* const kSpecialLabel[kSpecials] = {"Shift", "#+=", "Space", "Delete", "Done"};

const char* const* rows() { return symbols ? kSymbols : caps ? kUpper : kLower; }

int field_top() { return ui::header_h() + ui::gap(); }
int field_h() { return ui::line_h(ui::Font::Normal) + ui::gap() * 2; }

// The key area: 5 rows from under the text field to the bottom
int keys_top() { return field_top() + field_h() + ui::gap(); }
int row_h() { return (ui::height() - ui::gap() - keys_top()) / 5; }
int col_w() { return (ui::width() - ui::gap()) / 10; }

ui::Rect char_key(int row, int col)
{
    const int g = ui::gap() / 2 + 1;
    return {ui::gap() / 2 + col * col_w() + g / 2, keys_top() + row * row_h() + g / 2, col_w() - g, row_h() - g};
}

ui::Rect special_key(int i)      // two columns each
{
    const int g = ui::gap() / 2 + 1;
    return {ui::gap() / 2 + i * 2 * col_w() + g / 2, keys_top() + 4 * row_h() + g / 2, col_w() * 2 - g, row_h() - g};
}

void draw_field()
{
    LGFX& g = ui::gfx();
    const ui::Rect r{ui::gap(), field_top(), ui::width() - ui::gap() * 2, field_h()};
    g.fillRect(r.x, r.y, r.w, r.h, style::kBackground);
    g.drawRect(r.x, r.y, r.w, r.h, style::kKeyEdge);
    const int pw = ui::text_width(prompt_, ui::Font::Normal);
    const int x = r.x + ui::gap();
    const int y = r.y + ui::gap();
    ui::text(x, y, prompt_, style::kTextMuted, ui::Font::Normal);
    // The end of the text when it's too long to show whole
    const int room = r.w - ui::gap() * 3 - pw - 4;
    const char* s = text_;
    while (*s && ui::text_width(s, ui::Font::Normal) > room) ++s;
    ui::text(x + pw, y, s, style::kText, ui::Font::Normal);
    const int cx = x + pw + ui::text_width(s, ui::Font::Normal) + 1;
    g.fillRect(cx, y, 2, ui::line_h(ui::Font::Normal), style::kGold);
}

void draw_keys()
{
    LGFX& g = ui::gfx();
    g.fillRect(0, keys_top(), ui::width(), ui::height() - keys_top(), style::kBackground);
    const char* const* rw = rows();
    for (int row = 0; row < 4; ++row)
        for (int col = 0; rw[row][col]; ++col) {
            const char label[2] = {rw[row][col], 0};
            ui::key(char_key(row, col), label);
        }
    for (int i = 0; i < kSpecials; ++i) {
        const char* label = i == kSym && symbols ? "abc" : kSpecialLabel[i];
        const bool lit = (i == kShift && caps && !symbols) || i == kDone;
        ui::key(special_key(i), label, lit ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    }
}

void type(char c)
{
    const size_t n = strlen(text_);
    if (n >= max_) return;
    text_[n] = c;
    text_[n + 1] = 0;
}

} // namespace

void open(const char* title, const char* prompt, const char* initial, size_t max_len)
{
    snprintf(title_, sizeof title_, "%s", title);
    snprintf(prompt_, sizeof prompt_, "%s", prompt);
    max_ = max_len < kMax ? max_len : kMax;
    snprintf(text_, max_ + 1, "%s", initial ? initial : "");
    caps = false;
    symbols = false;
}

void draw()
{
    ui::clear();
    ui::header(title_, true);
    draw_field();
    draw_keys();
}

const char* text() { return text_; }

Result tap(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) return Result::Cancel;
    const char* const* rw = rows();
    for (int row = 0; row < 4; ++row)
        for (int col = 0; rw[row][col]; ++col)
            if (char_key(row, col).contains(t.x, t.y)) {
                type(rw[row][col]);
                draw_field();
                return Result::Typing;
            }
    for (int i = 0; i < kSpecials; ++i) {
        if (!special_key(i).contains(t.x, t.y)) continue;
        switch (i) {
        case kShift:
            if (symbols) symbols = false;
            else caps = !caps;
            draw_keys();
            break;
        case kSym:
            symbols = !symbols;
            draw_keys();
            break;
        case kSpace:
            type(' ');
            draw_field();
            break;
        case kDelete: {
            const size_t n = strlen(text_);
            if (n) text_[n - 1] = 0;
            draw_field();
            break;
        }
        case kDone:
            return Result::Done;
        }
        return Result::Typing;
    }
    return Result::Typing;
}

} // namespace keyboard

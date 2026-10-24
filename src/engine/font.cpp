#include "font.h"

#include <cstdlib>
#include <cstring>

#include <cctype>

namespace font {

bool load(dax::ByteSource& src, const dax::Index& idx, Font& out)
{
    out.loaded = false;
    const dax::Entry* e = idx.find(kBlockId);
    if (!e || e->raw_size != kBlockBytes) return false;
    if (dax::load_block(src, idx, *e, &out.glyph[0][0]) != kBlockBytes) return false;
    out.loaded = true;
    return true;
}

int glyph_of(char c)
{
    return toupper(static_cast<unsigned char>(c)) % 64;
}

int glyph_of(const Font& f, char c)
{
    if (f.clear && c >= 'a' && c <= 'z') return kGlyphs + (c - 'a');
    return glyph_of(c);
}

namespace {

// The text glyphs a font prints with: index (1-26, 32-63, then lower case)
// and bits
int text_glyphs(const Font& f, uint8_t (*bits)[8], int* index)
{
    int n = 0;
    for (int g = 1; g < 64; ++g) {
        if (g >= 27 && g < 32) continue;
        memcpy(bits[n], f.glyph[g], 8);
        index[n++] = g;
    }
    if (f.clear)
        for (int i = 0; i < 26; ++i) {
            memcpy(bits[n], kClearLower[i], 8);
            index[n++] = kGlyphs + i;
        }
    return n;
}

void redraw_text(pic::Canvas& c, const uint8_t (*old_bits)[8], const int* old_index, int n, const Font& f)
{
    for (int cy = 0; cy + 8 <= c.h; cy += 8)
        for (int cx = 0; cx + 8 <= c.w; cx += 8) {
            // The cell's colours: black and one other
            int other = -1;
            bool ok = true;
            for (int y = 0; y < 8 && ok; ++y) {
                uint8_t row[8];
                c.get_row(cx, cy + y, 8, row);
                for (int x = 0; x < 8 && ok; ++x) {
                    if (row[x] == 0) continue;
                    if (other < 0) other = row[x];
                    else if (row[x] != other) ok = false;
                }
            }
            if (!ok || other < 0) continue;            // more colours, or all black (a space)
            for (int pass = 0; pass < 2; ++pass) {
                // pass 0: ink = the colour on black; pass 1: ink = black on the colour
                uint8_t pat[8];
                for (int y = 0; y < 8; ++y) {
                    uint8_t row[8];
                    c.get_row(cx, cy + y, 8, row);
                    uint8_t b = 0;
                    for (int x = 0; x < 8; ++x)
                        if ((row[x] != 0) == (pass == 0)) b |= static_cast<uint8_t>(0x80 >> x);
                    pat[y] = b;
                }
                int hit = -1;
                for (int k = 0; k < n && hit < 0; ++k)
                    if (memcmp(pat, old_bits[k], 8) == 0) hit = k;
                if (hit < 0) continue;
                int g = old_index[hit];
                if (g >= kGlyphs && !f.clear) g = g - kGlyphs + 1;          // lower case -> the capital
                if (pass == 0) draw_glyph(c, f, g, cx, cy, static_cast<uint8_t>(other), 0);
                else draw_glyph(c, f, g, cx, cy, 0, other);
                break;
            }
        }
}

} // namespace

void set_letters(Font& f, int letters, const Font* alt, pic::Canvas* redraw)
{
    if (letters == kAltLetters && (!alt || !alt->loaded)) letters = kGameLetters;
    if (letters == f.letters) return;
    // (the old font's text glyphs, while it's redrawn; on the heap - static
    // RAM is dear on the boards)
    struct Old {
        uint8_t bits[26 + 32 + 26][8];
        int index[26 + 32 + 26];
    };
    Old* old = redraw ? static_cast<Old*>(malloc(sizeof(Old))) : nullptr;
    const int n = old ? text_glyphs(f, old->bits, old->index) : 0;
    // The game's own letters back first
    if (f.letters != kGameLetters) {
        memcpy(f.glyph[1], f.saved, 26 * 8);
        memcpy(f.glyph[32], f.saved[26], 32 * 8);
    }
    if (letters != kGameLetters) {
        memcpy(f.saved, f.glyph[1], 26 * 8);
        memcpy(f.saved[26], f.glyph[32], 32 * 8);
        if (letters == kClearLetters || letters == kClearCapsLetters) {
            memcpy(f.glyph[1], kClearUpper, 26 * 8);
            memcpy(f.glyph[32], kClearText, 32 * 8);
        } else {
            memcpy(f.glyph[1], alt->glyph[1], 26 * 8);
            memcpy(f.glyph[32], alt->glyph[32], 32 * 8);
        }
    }
    f.letters = static_cast<uint8_t>(letters);
    f.clear = letters == kClearLetters;
    if (old) redraw_text(*redraw, old->bits, old->index, n, f);
    free(old);
}

void set_clear(Font& f, bool on, pic::Canvas* redraw) { set_letters(f, on ? kClearLetters : kGameLetters, nullptr, redraw); }

void draw_glyph(pic::Canvas& c, const Font& f, int g, int x, int y, uint8_t fg, int bg)
{
    const bool lower = f.clear && g >= kGlyphs && g < kGlyphs + 26;
    if (g < 0 || (g >= kGlyphs && !lower)) return;
    const bool shadowed = bg == 0 && c.bg && c.bpp == 8;
    const bool own_shade = shadowed && f.shade && !lower && g < 64 && f.letters == kGameLetters;
    uint8_t above = 0;
    for (int row = 0; row < 8; ++row) {
        const int py = y + row;
        const uint8_t bits = lower ? kClearLower[g - kGlyphs][row] : f.glyph[g][row];
        const uint8_t shade = own_shade ? f.shade[g][row]
                                        : static_cast<uint8_t>(((bits >> 1) | (above >> 1) | above) & ~bits);
        above = bits;
        if (py < 0 || py >= c.h) continue;
        for (int col = 0; col < 8; ++col) {
            const int px = x + col;
            if (px < 0 || px >= c.w) continue;
            if (bits & (0x80 >> col)) c.set(px, py, fg);
            else if (shadowed) c.set(px, py, (shade & (0x80 >> col)) || py >= c.bg_rows ? 0 : c.bg[py * c.w + px]);
            else if (bg >= 0) c.set(px, py, static_cast<uint8_t>(bg));
        }
    }
}

int draw_text(pic::Canvas& c, const Font& f, const char* s, int col, int row, uint8_t fg, int bg)
{
    for (; *s && col < kCols; ++s, ++col) draw_glyph(c, f, glyph_of(f, *s), col * 8, row * 8, fg, bg);
    return col;
}

} // namespace font

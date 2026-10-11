#include "font.h"

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

void set_clear(Font& f, bool on)
{
    if (on == f.clear) return;
    if (on) {
        memcpy(f.saved, f.glyph[1], 26 * 8);
        memcpy(f.saved[26], f.glyph[32], 32 * 8);
        memcpy(f.glyph[1], kClearUpper, 26 * 8);
        memcpy(f.glyph[32], kClearText, 32 * 8);
    } else {
        memcpy(f.glyph[1], f.saved, 26 * 8);
        memcpy(f.glyph[32], f.saved[26], 32 * 8);
    }
    f.clear = on;
}

void draw_glyph(pic::Canvas& c, const Font& f, int g, int x, int y, uint8_t fg, int bg)
{
    const bool lower = f.clear && g >= kGlyphs && g < kGlyphs + 26;
    if (g < 0 || (g >= kGlyphs && !lower)) return;
    for (int row = 0; row < 8; ++row) {
        const int py = y + row;
        if (py < 0 || py >= c.h) continue;
        const uint8_t bits = lower ? kClearLower[g - kGlyphs][row] : f.glyph[g][row];
        uint8_t* line = c.px + py * c.w;
        for (int col = 0; col < 8; ++col) {
            const int px = x + col;
            if (px < 0 || px >= c.w) continue;
            if (bits & (0x80 >> col)) line[px] = fg;
            else if (bg >= 0) line[px] = static_cast<uint8_t>(bg);
        }
    }
}

int draw_text(pic::Canvas& c, const Font& f, const char* s, int col, int row, uint8_t fg, int bg)
{
    for (; *s && col < kCols; ++s, ++col) draw_glyph(c, f, glyph_of(f, *s), col * 8, row * 8, fg, bg);
    return col;
}

} // namespace font

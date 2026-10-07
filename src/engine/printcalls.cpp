#include "printcalls.h"

#include <cstring>

#include "text.h"

namespace printcalls {

namespace {

bool matches(const uint8_t* b)
{
    for (int i = 0; i < 4; ++i)
        if (b[i * 3] != 0xB0 || b[i * 3 + 2] != 0x50) return false;
    return b[12] == 0x8D && b[13] == 0x7E && b[15] == 0x16 && b[16] == 0x57 && b[17] == 0xBF && b[20] == 0x0E &&
           b[21] == 0x57 && b[22] == 0x9A && b[27] == 0x9A;
}

} // namespace

int read(dax::ByteSource& ovr, uint32_t at, uint32_t seg_base, Line* out, int max)
{
    int n = 0;
    for (; n < max; ++n, at += kBlock) {
        uint8_t b[kBlock];
        if (ovr.read_at(at, b, kBlock) != kBlock || !matches(b)) break;
        Line& l = out[n];
        l.col = b[1];
        l.row = b[4];
        l.fg = b[7];
        l.bg = b[10];
        const uint32_t str = seg_base + (b[18] | b[19] << 8);
        char s[text::kMaxString];
        if (!text::read_pascal(ovr, str, s, sizeof s)) return 0;
        strncpy(l.s, s, sizeof l.s - 1);
        l.s[sizeof l.s - 1] = 0;
        if (l.row > 24 || l.col > 39 || l.fg > 15 || l.bg > 15) return 0;
    }
    return n;
}

} // namespace printcalls

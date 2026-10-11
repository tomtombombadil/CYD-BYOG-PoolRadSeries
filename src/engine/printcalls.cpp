#include "printcalls.h"

#include <cstring>

#include "text.h"

namespace printcalls {

namespace {

bool matches(const uint8_t* b, int pushes)
{
    for (int i = 0; i < pushes; ++i)
        if (b[i * 3] != 0xB0 || b[i * 3 + 2] != 0x50) return false;
    b += pushes * 3;
    return b[0] == 0x8D && b[1] == 0x7E && b[3] == 0x16 && b[4] == 0x57 && b[5] == 0xBF && b[8] == 0x0E &&
           b[9] == 0x57 && b[10] == 0x9A && b[15] == 0x9A;
}

} // namespace

int read(dax::ByteSource& ovr, uint32_t at, uint32_t seg_base, Line* out, int max, int stride)
{
    if (stride != kBlock && stride != kBlockNoBack) return 0;
    const int pushes = stride == kBlock ? 4 : 3;
    int n = 0;
    for (; n < max; ++n, at += static_cast<uint32_t>(stride)) {
        uint8_t b[kBlock];
        if (ovr.read_at(at, b, static_cast<uint32_t>(stride)) != static_cast<uint32_t>(stride) || !matches(b, pushes)) break;
        Line& l = out[n];
        l.col = b[1];
        l.row = b[4];
        l.fg = b[7];
        l.bg = pushes == 4 ? b[10] : 0;          // (Pool's print: the paper black)
        const uint8_t* m = b + pushes * 3;
        const uint32_t str = seg_base + (m[6] | m[7] << 8);
        char s[text::kMaxString];
        if (!text::read_pascal(ovr, str, s, sizeof s)) return 0;
        strncpy(l.s, s, sizeof l.s - 1);
        l.s[sizeof l.s - 1] = 0;
        if (l.row > 24 || l.col > 39 || l.fg > 15 || l.bg > 15) return 0;
    }
    return n;
}

} // namespace printcalls

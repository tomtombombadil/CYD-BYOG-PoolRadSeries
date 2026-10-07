#include "inflate.h"

#include <cstring>

namespace inflate {

namespace {

struct Bits {
    Input&   in;
    uint32_t bits = 0;
    int      count = 0;
    bool     bad = false;

    explicit Bits(Input& i) : in(i) {}

    int bit()
    {
        if (count == 0) {
            const int b = in.byte();
            if (b < 0) { bad = true; return 0; }
            bits = static_cast<uint32_t>(b);
            count = 8;
        }
        const int b = static_cast<int>(bits & 1);
        bits >>= 1;
        --count;
        return b;
    }
    uint32_t take(int n)                   // n bits, least significant first
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<uint32_t>(bit()) << i;
        return v;
    }
    int byte_aligned()                     // drop the rest of the byte, read one
    {
        count = 0;
        const int b = in.byte();
        if (b < 0) bad = true;
        return b;
    }
};

// A canonical Huffman code: how many codes of each length, and the symbols
// in code order
struct Huffman {
    uint16_t count[16];
    uint16_t symbol[320];
};

bool build(Huffman& h, const uint8_t* lengths, int n)
{
    memset(h.count, 0, sizeof h.count);
    for (int i = 0; i < n; ++i) ++h.count[lengths[i]];
    if (h.count[0] == n) return true;      // no codes (allowed for an empty distance tree)
    int left = 1;                          // over-subscribed codes are bad data
    for (int len = 1; len < 16; ++len) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return false;
    }
    uint16_t offs[16];
    offs[1] = 0;
    for (int len = 1; len < 15; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + h.count[len]);
    for (int i = 0; i < n; ++i)
        if (lengths[i]) h.symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
    return true;
}

int decode(Bits& r, const Huffman& h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; ++len) {
        code |= r.bit();
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
        if (r.bad) return -1;
    }
    return -1;
}

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t  kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                8193, 12289, 16385, 24577};
const uint8_t  kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// The output side: every byte goes into the window and on to out
struct Sink {
    Output&  out;
    uint8_t* win;
    uint32_t pos = 0;          // bytes written so far
    bool     stop = false;

    bool put(uint8_t b)
    {
        win[pos & (kWindow - 1)] = b;
        ++pos;
        if (!out.put(b)) stop = true;
        return !stop;
    }
    bool copy(uint32_t dist, uint32_t len)
    {
        if (dist == 0 || dist > pos || dist > kWindow) return false;
        for (uint32_t k = 0; k < len; ++k)
            if (!put(win[(pos - dist) & (kWindow - 1)])) return false;
        return true;
    }
};

bool codes(Bits& r, const Huffman& lit, const Huffman& dist, Sink& s)
{
    for (;;) {
        const int sym = decode(r, lit);
        if (sym < 0 || r.bad) return false;
        if (sym < 256) {
            if (!s.put(static_cast<uint8_t>(sym))) return false;
        } else if (sym == 256) {
            return true;
        } else {
            const int li = sym - 257;
            if (li >= 29) return false;
            const uint32_t len = kLenBase[li] + r.take(kLenExtra[li]);
            const int ds = decode(r, dist);
            if (ds < 0 || ds >= 30 || r.bad) return false;
            const uint32_t d = kDistBase[ds] + r.take(kDistExtra[ds]);
            if (!s.copy(d, len)) return false;
        }
    }
}

} // namespace

bool raw(Input& in, Output& out, uint8_t* window)
{
    Bits r(in);
    Sink s{out, window};
    static const uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    Huffman lit, dist, lens;               // ~2 KB of stack
    bool last = false;
    while (!last) {
        last = r.bit() != 0;
        const uint32_t type = r.take(2);
        if (r.bad) return false;
        if (type == 0) {                   // stored
            const int a = r.byte_aligned(), b = r.in.byte(), c = r.in.byte(), d = r.in.byte();
            if (a < 0 || b < 0 || c < 0 || d < 0) return false;
            const uint32_t len = static_cast<uint32_t>(a | b << 8), nlen = static_cast<uint32_t>(c | d << 8);
            if (len != (~nlen & 0xFFFF)) return false;
            for (uint32_t k = 0; k < len; ++k) {
                const int v = r.in.byte();
                if (v < 0 || !s.put(static_cast<uint8_t>(v))) return false;
            }
        } else if (type == 1) {            // fixed codes
            uint8_t l[320];
            int i = 0;
            for (; i < 144; ++i) l[i] = 8;
            for (; i < 256; ++i) l[i] = 9;
            for (; i < 280; ++i) l[i] = 7;
            for (; i < 288; ++i) l[i] = 8;
            build(lit, l, 288);
            for (i = 0; i < 30; ++i) l[i] = 5;
            build(dist, l, 30);
            if (!codes(r, lit, dist, s)) return false;
        } else if (type == 2) {            // dynamic codes
            const int nlen = static_cast<int>(r.take(5)) + 257, ndist = static_cast<int>(r.take(5)) + 1,
                      ncode = static_cast<int>(r.take(4)) + 4;
            if (nlen > 286 || ndist > 30 || r.bad) return false;
            uint8_t l[320] = {};
            for (int i = 0; i < ncode; ++i) l[kOrder[i]] = static_cast<uint8_t>(r.take(3));
            if (!build(lens, l, 19)) return false;
            int i = 0;
            memset(l, 0, sizeof l);
            while (i < nlen + ndist) {
                const int sym = decode(r, lens);
                if (sym < 0 || r.bad) return false;
                if (sym < 16) { l[i++] = static_cast<uint8_t>(sym); continue; }
                uint8_t v = 0;
                int rep;
                if (sym == 16) {
                    if (i == 0) return false;
                    v = l[i - 1];
                    rep = 3 + static_cast<int>(r.take(2));
                } else if (sym == 17) {
                    rep = 3 + static_cast<int>(r.take(3));
                } else {
                    rep = 11 + static_cast<int>(r.take(7));
                }
                if (i + rep > nlen + ndist) return false;
                while (rep--) l[i++] = v;
            }
            if (l[256] == 0) return false; // no end-of-block code
            if (!build(lit, l, nlen) || !build(dist, l + nlen, ndist)) return false;
            if (!codes(r, lit, dist, s)) return false;
        } else {
            return false;
        }
    }
    return true;
}

} // namespace inflate

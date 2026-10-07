#include "ecl.h"

#include <cstring>
#include <new>

namespace ecl {

namespace {

constexpr uint16_t kBase = 0x8000;   // VM address of the code's first byte

// Reads n operands from p; *count grows, ops beyond kMaxOps are dropped
bool operands(const uint8_t* c, uint32_t len, uint32_t& p, int n, Insn& in)
{
    for (int i = 0; i < n; ++i) {
        if (p + 2 > len) return false;
        Operand o;
        o.code = c[p];
        o.low = c[p + 1];
        p += 2;
        if (o.code == 1 || o.code == 2 || o.code == 3 || o.code == 0x81) {
            if (p + 1 > len) return false;
            o.high = c[p++];
        } else if (o.code == 0x80) {
            if (p + o.low > len) return false;
            p += o.low;
        }
        if (in.count < kMaxOps) in.ops[in.count] = o;
        ++in.count;
    }
    return true;
}

uint32_t target(const Operand& o, uint32_t len)
{
    if (!(o.code == 1 || o.code == 2)) return 0xFFFFFFFFu;
    const uint16_t w = o.word();
    if (w < kBase) return 0xFFFFFFFFu;
    const uint32_t off = static_cast<uint32_t>(w - kBase);
    return off < len ? off : 0xFFFFFFFFu;
}

} // namespace

bool decode(const uint8_t* c, uint32_t len, uint32_t at, const OpSet& set, Insn& out)
{
    out = Insn{};
    if (at >= len) return false;
    out.op = c[at];
    out.at = at;
    const int8_t size = set.sizes[out.op];
    if (size == kUnknown) return false;
    uint32_t p = at + 1;
    if (size >= 0) {
        if (!operands(c, len, p, size, out)) return false;
    } else {
        const int first = size == kCount3 ? 3 : 2;
        if (!operands(c, len, p, first, out)) return false;
        const Operand& n = out.ops[first - 1];
        if (!n.immediate()) return false;
        if (!operands(c, len, p, n.low, out)) return false;
    }
    out.next = p;
    return true;
}

bool entries(const uint8_t* c, uint32_t len, const OpSet&, uint32_t out[5])
{
    uint32_t p = 0;
    for (int i = 0; i < 5; ++i) {
        Insn in;
        p += 1;                       // the filler byte
        if (!operands(c, len, p, 1, in)) return false;
        out[i] = target(in.ops[0], len);
    }
    return true;
}

MapLoad find_map_load(const uint8_t* block, uint32_t len, const OpSet& set)
{
    MapLoad m;
    if (len < 3) return m;
    const uint8_t* c = block + 2;
    const uint32_t n = len - 2;
    uint32_t start[5];
    if (!entries(c, n, set, start)) return m;

    // Follow the script from its entry points: every instruction once
    uint8_t* seen = new (std::nothrow) uint8_t[(n + 7) / 8];
    uint32_t* todo = new (std::nothrow) uint32_t[256];
    if (!seen || !todo) {
        delete[] seen;
        delete[] todo;
        return m;
    }
    memset(seen, 0, (n + 7) / 8);
    int top = 0;
    auto push = [&](uint32_t t) {
        if (t < n && top < 256) todo[top++] = t;
    };
    for (uint32_t s : start) push(s);
    // The first in the block wins (scripts set the map up at their start)
    uint32_t geo_at = 0xFFFFFFFFu, walls_at = 0xFFFFFFFFu;
    auto value = [](const Operand& o) -> uint8_t {
        return (o.low == 0x7F || o.low == 0xFF) ? kNone : o.low;
    };

    while (top > 0) {
        uint32_t p = todo[--top];
        while (p < n && !(seen[p >> 3] & (1 << (p & 7)))) {
            seen[p >> 3] |= static_cast<uint8_t>(1 << (p & 7));
            Insn in;
            if (!decode(c, n, p, set, in)) break;
            if (in.op == set.load_files && p < geo_at && in.count >= 1 && in.ops[0].immediate()) {
                m.geo = value(in.ops[0]);
                geo_at = p;
            }
            if (in.op == set.load_pieces && p < walls_at && in.count >= 3 && in.ops[0].immediate() &&
                in.ops[1].immediate() && in.ops[2].immediate()) {
                for (int k = 0; k < 3; ++k) m.walls[k] = value(in.ops[k]);
                walls_at = p;
            }
            if (in.op == set.go_to || in.op == set.go_sub) {
                if (in.count >= 1) push(target(in.ops[0], n));
            } else if (in.op == set.on_goto || in.op == set.on_gosub) {
                for (int k = 2; k < in.count && k < kMaxOps; ++k) push(target(in.ops[k], n));
            } else if (in.op >= set.if_first && in.op <= set.if_last) {
                Insn next;            // the instruction it may skip: go on after it too
                if (decode(c, n, in.next, set, next)) push(next.next);
            }
            if (in.op == set.exit || in.op == set.go_to || in.op == set.ret || in.op == set.new_ecl) break;
            p = in.next;
        }
    }
    delete[] seen;
    delete[] todo;
    return m;
}

} // namespace ecl

#include "pdf.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace pdf {

namespace {

constexpr size_t kObjBuf = 4096;
constexpr int kNodes = 256;

enum class T : uint8_t { None, Null, Bool, Num, Name, Str, Ref, Array, Dict, Keyword };

struct Node {
    T        t = T::None;
    int32_t  v = 0;            // number, ref object number, bool
    uint16_t s = 0, n = 0;     // span in the buffer (names, strings, keywords)
    int16_t  first = -1;       // arrays and dicts: first child (dicts: key, value, key, ...)
    int16_t  next = -1;        // next sibling
};

// One object (or the trailer) read into a buffer and parsed
struct Obj {
    char     buf[kObjBuf];
    size_t   len = 0;
    uint32_t file_at = 0;      // where buf starts in the file
    Node     node[kNodes];
    int      nn = 0;
    int      root = -1;        // the object's value
    uint32_t stream_at = 0;    // data after "stream" (0 = none)
};

bool is_ws(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0; }
bool is_delim(char c)
{
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
           c == '%';
}

struct Parser {
    Obj& o;
    size_t p = 0;

    void skip()
    {
        while (p < o.len) {
            if (is_ws(o.buf[p])) {
                ++p;
            } else if (o.buf[p] == '%') {
                while (p < o.len && o.buf[p] != '\n' && o.buf[p] != '\r') ++p;
            } else {
                break;
            }
        }
    }
    int add(T t)
    {
        if (o.nn >= kNodes) return -1;
        o.node[o.nn] = Node{};
        o.node[o.nn].t = t;
        return o.nn++;
    }
    bool number_at(size_t q, int32_t* v, size_t* end) const
    {
        size_t i = q;
        bool neg = false;
        if (i < o.len && (o.buf[i] == '+' || o.buf[i] == '-')) neg = o.buf[i++] == '-';
        const size_t d0 = i;
        int64_t x = 0;
        while (i < o.len && o.buf[i] >= '0' && o.buf[i] <= '9') x = x * 10 + (o.buf[i++] - '0');
        bool frac = false;
        if (i < o.len && o.buf[i] == '.') {
            frac = true;
            ++i;
            while (i < o.len && o.buf[i] >= '0' && o.buf[i] <= '9') ++i;
        }
        if (i == d0 && !frac) return false;
        if (i < o.len && !is_ws(o.buf[i]) && !is_delim(o.buf[i])) return false;
        *v = static_cast<int32_t>(neg ? -x : x);
        *end = i;
        return true;
    }
    int value(int depth = 0)
    {
        skip();
        if (p >= o.len || depth > 16) return -1;
        const char c = o.buf[p];
        if (c == '<' && p + 1 < o.len && o.buf[p + 1] == '<') {
            p += 2;
            const int d = add(T::Dict);
            if (d < 0) return -1;
            int last = -1;
            for (;;) {
                skip();
                if (p + 1 < o.len && o.buf[p] == '>' && o.buf[p + 1] == '>') {
                    p += 2;
                    return d;
                }
                const int k = value(depth + 1);
                if (k < 0) return -1;
                const int v = value(depth + 1);
                if (v < 0) return -1;
                if (last < 0) o.node[d].first = static_cast<int16_t>(k);
                else o.node[last].next = static_cast<int16_t>(k);
                o.node[k].next = static_cast<int16_t>(v);
                last = v;
            }
        }
        if (c == '[') {
            ++p;
            const int a = add(T::Array);
            if (a < 0) return -1;
            int last = -1, count = 0;
            for (;;) {
                skip();
                if (p < o.len && o.buf[p] == ']') {
                    ++p;
                    o.node[a].v = count;
                    return a;
                }
                const int v = value(depth + 1);
                if (v < 0) return -1;
                if (last < 0) o.node[a].first = static_cast<int16_t>(v);
                else o.node[last].next = static_cast<int16_t>(v);
                last = v;
                ++count;
            }
        }
        if (c == '/') {
            const int nm = add(T::Name);
            if (nm < 0) return -1;
            const size_t s = ++p;
            while (p < o.len && !is_ws(o.buf[p]) && !is_delim(o.buf[p])) ++p;
            o.node[nm].s = static_cast<uint16_t>(s);
            o.node[nm].n = static_cast<uint16_t>(p - s);
            return nm;
        }
        if (c == '(') {
            const int st = add(T::Str);
            if (st < 0) return -1;
            const size_t s = ++p;
            int nest = 1;
            while (p < o.len && nest > 0) {
                if (o.buf[p] == '\\') ++p;
                else if (o.buf[p] == '(') ++nest;
                else if (o.buf[p] == ')') --nest;
                ++p;
            }
            o.node[st].s = static_cast<uint16_t>(s);
            o.node[st].n = static_cast<uint16_t>(p - 1 - s);
            return st;
        }
        if (c == '<') {
            const int st = add(T::Str);
            if (st < 0) return -1;
            const size_t s = ++p;
            while (p < o.len && o.buf[p] != '>') ++p;
            o.node[st].s = static_cast<uint16_t>(s);
            o.node[st].n = static_cast<uint16_t>(p - s);
            o.node[st].v = 1;                       // hex
            if (p < o.len) ++p;
            return st;
        }
        int32_t num = 0;
        size_t end = 0;
        if (number_at(p, &num, &end)) {
            // "12 0 R" is a reference
            size_t q = end;
            while (q < o.len && is_ws(o.buf[q])) ++q;
            int32_t gen = 0;
            size_t end2 = 0;
            if (number_at(q, &gen, &end2)) {
                size_t r = end2;
                while (r < o.len && is_ws(o.buf[r])) ++r;
                if (r < o.len && o.buf[r] == 'R' && (r + 1 >= o.len || is_ws(o.buf[r + 1]) || is_delim(o.buf[r + 1]))) {
                    const int rf = add(T::Ref);
                    if (rf < 0) return -1;
                    o.node[rf].v = num;
                    p = r + 1;
                    return rf;
                }
            }
            const int n = add(T::Num);
            if (n < 0) return -1;
            o.node[n].v = num;
            p = end;
            return n;
        }
        // A keyword: true, false, null, ...
        const int k = add(T::Keyword);
        if (k < 0) return -1;
        const size_t s = p;
        while (p < o.len && !is_ws(o.buf[p]) && !is_delim(o.buf[p])) ++p;
        if (p == s) return -1;
        o.node[k].s = static_cast<uint16_t>(s);
        o.node[k].n = static_cast<uint16_t>(p - s);
        return k;
    }
    bool keyword(const char* w)
    {
        skip();
        const size_t n = strlen(w);
        if (p + n > o.len || memcmp(o.buf + p, w, n) != 0) return false;
        p += n;
        return true;
    }
};

bool name_is(const Obj& o, int node, const char* s)
{
    if (node < 0 || (o.node[node].t != T::Name && o.node[node].t != T::Keyword)) return false;
    const size_t n = strlen(s);
    return o.node[node].n == n && memcmp(o.buf + o.node[node].s, s, n) == 0;
}

int get(const Obj& o, int dict, const char* key)
{
    if (dict < 0 || o.node[dict].t != T::Dict) return -1;
    for (int k = o.node[dict].first; k >= 0;) {
        const int v = o.node[k].next;
        if (v < 0) return -1;
        if (name_is(o, k, key)) return v;
        k = o.node[v].next;
    }
    return -1;
}

bool fill(dax::ByteSource& src, uint32_t at, Obj& o)
{
    o.file_at = at;
    o.len = src.read_at(at, reinterpret_cast<uint8_t*>(o.buf), kObjBuf);
    o.nn = 0;
    o.root = -1;
    o.stream_at = 0;
    return o.len > 0;
}

// Object `num` ("num gen obj <value> [stream ...]")
bool load(dax::ByteSource& src, const Doc& doc, int num, Obj& o)
{
    if (num <= 0 || num >= kMaxObjects || !doc.offset[num] || !fill(src, doc.offset[num], o)) return false;
    Parser ps{o};
    int32_t n = 0, g = 0;
    size_t end = 0;
    ps.skip();
    if (!ps.number_at(ps.p, &n, &end) || n != num) return false;
    ps.p = end;
    ps.skip();
    if (!ps.number_at(ps.p, &g, &end)) return false;
    ps.p = end;
    if (!ps.keyword("obj")) return false;
    o.root = ps.value();
    if (o.root < 0) return false;
    if (ps.keyword("stream")) {
        if (ps.p < o.len && o.buf[ps.p] == '\r') ++ps.p;
        if (ps.p < o.len && o.buf[ps.p] == '\n') ++ps.p;
        o.stream_at = o.file_at + static_cast<uint32_t>(ps.p);
    }
    return true;
}

// A value that may be a reference: the object it points to, loaded into
// `tmp`; returns (obj, node) to use
bool deref(dax::ByteSource& src, const Doc& doc, const Obj& o, int node, Obj& tmp, const Obj** ro, int* rn)
{
    if (node < 0) return false;
    if (o.node[node].t == T::Ref) {
        if (!load(src, doc, o.node[node].v, tmp)) return false;
        *ro = &tmp;
        *rn = tmp.root;
        return true;
    }
    *ro = &o;
    *rn = node;
    return true;
}

// A small buffered reader for the xref table
struct Reader {
    dax::ByteSource& src;
    uint32_t pos;
    uint8_t buf[256];
    uint32_t at = 0, n = 0;
    int peek()
    {
        if (pos < at || pos >= at + n) {
            at = pos;
            n = static_cast<uint32_t>(src.read_at(pos, buf, sizeof buf));
            if (n == 0) return -1;
        }
        return buf[pos - at];
    }
    void ws()
    {
        for (int c = peek(); c >= 0 && is_ws(static_cast<char>(c)); c = peek()) ++pos;
    }
    bool word(char* out, size_t cap)
    {
        ws();
        size_t k = 0;
        for (int c = peek(); c >= 0 && !is_ws(static_cast<char>(c)) && k + 1 < cap; c = peek()) {
            out[k++] = static_cast<char>(c);
            ++pos;
        }
        out[k] = 0;
        return k > 0;
    }
};

bool read_xref(dax::ByteSource& src, uint32_t at, Doc& doc, Obj& o, uint32_t* prev)
{
    *prev = 0;
    Reader r{src, at};
    char w[24];
    if (!r.word(w, sizeof w) || strcmp(w, "xref") != 0) return false;   // xref streams: not supported
    for (;;) {
        const uint32_t mark = r.pos;
        if (!r.word(w, sizeof w)) return false;
        if (strcmp(w, "trailer") == 0) {
            r.pos = mark;
            break;
        }
        const long start = strtol(w, nullptr, 10);
        if (!r.word(w, sizeof w)) return false;
        const long count = strtol(w, nullptr, 10);
        if (start < 0 || count < 0 || count > 100000) return false;
        for (long i = 0; i < count; ++i) {
            char off[24], gen[24], kind[8];
            if (!r.word(off, sizeof off) || !r.word(gen, sizeof gen) || !r.word(kind, sizeof kind)) return false;
            const long num = start + i;
            if (num < kMaxObjects && kind[0] == 'n' && doc.offset[num] == 0) {
                doc.offset[num] = static_cast<uint32_t>(strtoul(off, nullptr, 10));
                if (num + 1 > doc.objects) doc.objects = static_cast<int>(num + 1);
            }
        }
    }
    // The trailer dictionary
    if (!fill(src, r.pos, o)) return false;
    Parser ps{o};
    if (!ps.keyword("trailer")) return false;
    const int t = ps.value();
    if (t < 0 || o.node[t].t != T::Dict) return false;
    const int root = get(o, t, "Root");
    if (!doc.root && root >= 0 && o.node[root].t == T::Ref) doc.root = o.node[root].v;
    const int id = get(o, t, "ID");
    if (!doc.id[0] && id >= 0 && o.node[id].t == T::Array && o.node[id].first >= 0) {
        const Node& s = o.node[o.node[id].first];
        if (s.t == T::Str && s.v == 1) {
            size_t k = 0;
            for (size_t i = 0; i < s.n && k + 1 < sizeof doc.id; ++i) {
                char c = o.buf[s.s + i];
                if (c >= 'A' && c <= 'F') c = static_cast<char>(c + 32);
                if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) doc.id[k++] = c;
            }
            doc.id[k] = 0;
        }
    }
    const int pv = get(o, t, "Prev");
    if (pv >= 0 && o.node[pv].t == T::Num) *prev = static_cast<uint32_t>(o.node[pv].v);
    return true;
}

} // namespace

bool open(dax::ByteSource& src, Doc& doc)
{
    doc = Doc{};
    const uint32_t size = src.size();
    uint8_t head[8];
    if (src.read_at(0, head, 5) != 5 || memcmp(head, "%PDF-", 5) != 0) return false;
    // "startxref <n>" near the end
    char tail[1024];
    const uint32_t from = size > sizeof tail ? size - static_cast<uint32_t>(sizeof tail) : 0;
    const size_t n = src.read_at(from, reinterpret_cast<uint8_t*>(tail), size - from);
    long xref = -1;
    for (size_t i = n >= 9 ? n - 9 : 0; i-- > 0;) {
        if (memcmp(tail + i, "startxref", 9) == 0) {
            xref = strtol(tail + i + 9, nullptr, 10);
            break;
        }
    }
    if (xref <= 0 || static_cast<uint32_t>(xref) >= size) return false;
    Obj* o = new (std::nothrow) Obj;
    if (!o) return false;
    uint32_t at = static_cast<uint32_t>(xref), prev = 0;
    bool ok = false;
    for (int k = 0; k < 16 && at; ++k) {
        if (!read_xref(src, at, doc, *o, &prev)) break;
        ok = true;
        at = prev;
    }
    delete o;
    return ok && doc.root > 0;
}

namespace {

void visit(dax::ByteSource& src, const Doc& doc, int num, Obj& o, int* out, int max, int* n, int depth)
{
    if (depth > 12 || *n >= max || !load(src, doc, num, o)) return;
    const int type = get(o, o.root, "Type");
    if (name_is(o, type, "Page")) {
        out[(*n)++] = num;
        return;
    }
    const int kids = get(o, o.root, "Kids");
    if (kids < 0 || o.node[kids].t != T::Array) return;
    int list[64];
    int count = 0;
    for (int k = o.node[kids].first; k >= 0 && count < 64; k = o.node[k].next)
        if (o.node[k].t == T::Ref) list[count++] = o.node[k].v;
    for (int i = 0; i < count; ++i) visit(src, doc, list[i], o, out, max, n, depth + 1);
}

} // namespace

int pages(dax::ByteSource& src, const Doc& doc, int* page_obj, int max)
{
    Obj* o = new (std::nothrow) Obj;
    if (!o) return 0;
    int n = 0;
    if (load(src, doc, doc.root, *o)) {
        const int p = get(*o, o->root, "Pages");
        if (p >= 0 && o->node[p].t == T::Ref) visit(src, doc, o->node[p].v, *o, page_obj, max, &n, 0);
    }
    delete o;
    return n;
}

bool page_image(dax::ByteSource& src, const Doc& doc, int page_obj, Image& out)
{
    Obj* a = new (std::nothrow) Obj;
    Obj* b = new (std::nothrow) Obj;
    bool ok = false;
    out.no_memory = !a || !b;
    if (a && b && load(src, doc, page_obj, *a)) {
        // /Resources here or inherited from a parent
        int res = get(*a, a->root, "Resources");
        for (int up = 0; res < 0 && up < 8; ++up) {
            const int parent = get(*a, a->root, "Parent");
            if (parent < 0 || a->node[parent].t != T::Ref || !load(src, doc, a->node[parent].v, *a)) break;
            res = get(*a, a->root, "Resources");
        }
        const Obj* ro = nullptr;
        int rn = -1;
        if (res >= 0 && deref(src, doc, *a, res, *b, &ro, &rn)) {
            // b may now hold the resources; copy what's needed before reusing
            int xo = get(*ro, rn, "XObject");
            Obj* holder = (ro == b) ? b : a;
            const Obj* xo_o = nullptr;
            int xo_n = -1;
            Obj* spare = (holder == a) ? b : a;
            if (xo >= 0 && deref(src, doc, *holder, xo, *spare, &xo_o, &xo_n) && xo_n >= 0 &&
                xo_o->node[xo_n].t == T::Dict) {
                const int first_val = xo_o->node[xo_n].first >= 0 ? xo_o->node[xo_o->node[xo_n].first].next : -1;
                if (first_val >= 0 && xo_o->node[first_val].t == T::Ref) {
                    const int img = xo_o->node[first_val].v;
                    if (load(src, doc, img, *a) && a->stream_at) {
                        const int st = get(*a, a->root, "Subtype");
                        const int w = get(*a, a->root, "Width");
                        const int h = get(*a, a->root, "Height");
                        const int f = get(*a, a->root, "Filter");
                        int len = get(*a, a->root, "Length");
                        int32_t length = -1;
                        if (len >= 0 && a->node[len].t == T::Num) length = a->node[len].v;
                        bool dct = name_is(*a, f, "DCTDecode");
                        if (f >= 0 && a->node[f].t == T::Array)
                            for (int k = a->node[f].first; k >= 0; k = a->node[k].next) dct = dct || name_is(*a, k, "DCTDecode");
                        out.width = w >= 0 ? a->node[w].v : 0;
                        out.height = h >= 0 ? a->node[h].v : 0;
                        out.jpeg = dct;
                        out.data_at = a->stream_at;
                        if (len >= 0 && a->node[len].t == T::Ref && load(src, doc, a->node[len].v, *b) &&
                            b->node[b->root].t == T::Num)
                            length = b->node[b->root].v;
                        ok = name_is(*a, st, "Image") && length > 0 && out.data_at + static_cast<uint32_t>(length) <= src.size();
                        out.data_len = ok ? static_cast<uint32_t>(length) : 0;
                    }
                }
            }
        }
    }
    delete a;
    delete b;
    return ok;
}

} // namespace pdf

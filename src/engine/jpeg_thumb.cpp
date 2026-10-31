#include "jpeg_thumb.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

namespace jthumb {

namespace {

constexpr int kMaxComps = 3;

// A Huffman table, ready for decoding (T.81 F.2.2.3)
struct Huff {
    uint8_t val[256];
    int32_t maxcode[18];
    int32_t valptr[17];
    int32_t mincode[17];
    bool    ok = false;
};

bool build_huff(Huff& h, const uint8_t bits[16], const uint8_t* vals, int nvals)
{
    memset(h.val, 0, sizeof h.val);
    h.ok = false;
    if (nvals < 0 || nvals > 256) return false;          // (a damaged table: more values than a table holds)
    memcpy(h.val, vals, static_cast<size_t>(nvals));
    int code = 0, k = 0;
    for (int l = 1; l <= 16; ++l) {
        const int n = bits[l - 1];
        if (n) {
            h.valptr[l] = k;
            h.mincode[l] = code;
            code += n;
            k += n;
            h.maxcode[l] = code - 1;
        } else {
            h.maxcode[l] = -1;
        }
        code <<= 1;
    }
    h.maxcode[17] = 0x7FFFFFFF;
    h.ok = k == nvals && k <= 256;
    return h.ok;
}

// The entropy-coded data of a scan: bits, byte stuffing, restart markers
struct Bits {
    dax::ByteSource* src = nullptr;
    uint32_t pos = 0, end = 0;          // the next byte to read
    uint8_t  buf[512];
    uint32_t buf_at = 0, buf_len = 0;
    uint32_t acc = 0;
    int      n = 0;                     // bits in acc
    bool     marker = false;            // a marker reached: zeros from here
    bool     bad = false;

    void start(dax::ByteSource& s, uint32_t at)
    {
        src = &s;
        pos = at;
        end = s.size();
        buf_at = buf_len = 0;
        acc = 0;
        n = 0;
        marker = false;
    }
    int byte_at(uint32_t p)
    {
        if (p >= end) return -1;
        if (p < buf_at || p >= buf_at + buf_len) {
            buf_at = p;
            buf_len = static_cast<uint32_t>(src->read_at(p, buf, sizeof buf));
            if (buf_len == 0) return -1;
        }
        return buf[p - buf_at];
    }
    void fill()
    {
        while (n <= 24) {
            int b = 0;
            if (!marker) {
                b = byte_at(pos);
                if (b < 0) {
                    marker = true;
                    b = 0;
                } else if (b == 0xFF) {
                    const int nx = byte_at(pos + 1);
                    if (nx == 0) {
                        pos += 2;
                    } else {
                        marker = true;      // (pos stays on the marker)
                        b = 0;
                    }
                } else {
                    ++pos;
                }
            }
            acc |= static_cast<uint32_t>(b) << (24 - n);
            n += 8;
        }
    }
    int bit()
    {
        if (n == 0) fill();
        const int v = static_cast<int>(acc >> 31);
        acc <<= 1;
        --n;
        return v;
    }
    int bits(int k)
    {
        if (k == 0) return 0;
        if (n < k) fill();
        const int v = static_cast<int>(acc >> (32 - k));
        acc <<= k;
        n -= k;
        return v;
    }
    int decode(const Huff& h)
    {
        int code = 0;
        for (int l = 1; l <= 16; ++l) {
            code = (code << 1) | bit();
            if (code <= h.maxcode[l]) {
                const int i = h.valptr[l] + code - h.mincode[l];
                return i >= 0 && i < 256 ? h.val[i] : 0;
            }
        }
        bad = true;
        return 0;
    }
    int extend(int s)
    {
        const int v = bits(s);
        return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
    }
    // A restart marker: the bits left go, the marker is passed
    void restart()
    {
        acc = 0;
        n = 0;
        // (find FF D0-D7 from here)
        for (int guard = 0; guard < 4096; ++guard) {
            const int b = byte_at(pos);
            if (b < 0) break;
            if (b == 0xFF) {
                const int nx = byte_at(pos + 1);
                if (nx >= 0xD0 && nx <= 0xD7) {
                    pos += 2;
                    break;
                }
                if (nx != 0 && nx != 0xFF) break;      // another marker: stop there
            }
            ++pos;
        }
        marker = false;
    }
};

struct Comp {
    int id = 0, h = 1, v = 1, tq = 0;
    int td = 0, ta = 0;                 // the DC scan's / baseline's tables
};

// A scan found in the file: its data's start and what it covers
struct Scan {
    uint32_t at = 0;
    int      ncomp = 0;
    int      comp[kMaxComps] = {};      // indexes into comps
    int      ss = 0, se = 0, ah = 0, al = 0;
    int      restart = 0;
    bool     found = false;
};

struct Dec {
    int      width = 0, height = 0, ncomp = 0;
    bool     progressive = false, baseline = false;
    Comp     comps[kMaxComps];
    uint16_t q[4][64] = {};             // zig-zag order
    uint16_t qs[kMaxComps][5] = {};     // each component's (zig-zag 0, 1, 2, 4) at its scan
    Huff     dc[4], ac[4];              // as defined (they change between scans)
    Huff     scan_dc[kMaxComps];        // the tables the scans use, kept
    Huff     scan_ac[kMaxComps];
    Huff     luma_ac;
    Scan     main, luma;                // the DC (or baseline) scan, the luma's first AC scan
    int      restart = 0;
};

uint16_t be16(const uint8_t* p) { return static_cast<uint16_t>(p[0] << 8 | p[1]); }

const uint8_t kZz[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                         41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                         30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// The headers, the scans the picture needs (their tables kept as they were)
bool parse(dax::ByteSource& src, Dec& d, bool headers_only)
{
    uint8_t m[4];
    if (src.read_at(0, m, 2) != 2 || m[0] != 0xFF || m[1] != 0xD8) return false;
    uint32_t pos = 2;
    const uint32_t size = src.size();
    uint8_t* seg = static_cast<uint8_t*>(malloc(1024));
    if (!seg) return false;
    bool sof = false, ok = true;
    while (ok && pos + 4 <= size) {
        if (src.read_at(pos, m, 2) != 2) break;
        if (m[0] != 0xFF) {
            ++pos;                          // (junk: look on)
            continue;
        }
        if (m[1] == 0xFF) {
            ++pos;
            continue;
        }
        const uint8_t mk = m[1];
        if (mk == 0xD8 || mk == 0x01 || (mk >= 0xD0 && mk <= 0xD7)) {
            pos += 2;
            continue;
        }
        if (mk == 0xD9) break;
        if (src.read_at(pos + 2, m, 2) != 2) break;
        const uint32_t len = be16(m);
        if (len < 2) {
            ok = false;
            break;
        }
        const uint32_t body = pos + 4, blen = len - 2;
        const uint32_t next = pos + 2 + len;
        if ((mk == 0xC4 || mk == 0xDB || (mk >= 0xC0 && mk <= 0xCF) || mk == 0xDA || mk == 0xDD) && blen > 1024) {
            ok = false;
            break;
        }
        if (mk == 0xC4 || mk == 0xDB || (mk >= 0xC0 && mk <= 0xCF) || mk == 0xDA || mk == 0xDD)
            if (src.read_at(body, seg, blen) != blen) {
                ok = false;
                break;
            }
        if (mk == 0xDB) {
            for (uint32_t p = 0; p < blen;) {
                const int pq = seg[p] >> 4, tq = seg[p] & 3;
                ++p;
                if (p + (pq ? 128u : 64u) > blen) {          // (a damaged table: shorter than its segment says)
                    ok = false;
                    break;
                }
                for (int k = 0; k < 64; ++k) {
                    d.q[tq][k] = pq ? be16(seg + p + 2 * k) : seg[p + k];
                }
                p += pq ? 128 : 64;
            }
        } else if (mk == 0xC4) {
            for (uint32_t p = 0; p + 17 <= blen;) {
                const int tc = seg[p] >> 4, th = seg[p] & 3;
                const uint8_t* bits = seg + p + 1;
                int nv = 0;
                for (int i = 0; i < 16; ++i) nv += bits[i];
                if (p + 17 + nv > blen) {
                    ok = false;
                    break;
                }
                if (!build_huff(tc ? d.ac[th] : d.dc[th], bits, seg + p + 17, nv)) ok = false;
                p += 17 + static_cast<uint32_t>(nv);
            }
        } else if (mk == 0xDD) {
            if (blen < 2) {
                ok = false;
                break;
            }
            d.restart = be16(seg);
        } else if (mk >= 0xC0 && mk <= 0xCF && mk != 0xC4 && mk != 0xC8 && mk != 0xCC) {
            // A frame: baseline / extended Huffman (C0, C1) or progressive (C2); the rest refused
            if (!(mk == 0xC0 || mk == 0xC1 || mk == 0xC2) || blen < 6 || seg[0] != 8) {
                ok = false;
                break;
            }
            d.progressive = mk == 0xC2;
            d.baseline = !d.progressive;
            d.height = be16(seg + 1);
            d.width = be16(seg + 3);
            d.ncomp = seg[5];
            if ((d.ncomp != 1 && d.ncomp != 3) || d.width <= 0 || d.height <= 0 ||
                blen < 6 + 3 * static_cast<uint32_t>(d.ncomp)) {
                ok = false;
                break;
            }
            for (int c = 0; c < d.ncomp; ++c) {
                d.comps[c].id = seg[6 + c * 3];
                d.comps[c].h = seg[7 + c * 3] >> 4;
                d.comps[c].v = seg[7 + c * 3] & 15;
                d.comps[c].tq = seg[8 + c * 3] & 3;
            }
            // (the luma 1 or 2 each way, the chroma 1)
            if (d.comps[0].h < 1 || d.comps[0].h > 2 || d.comps[0].v < 1 || d.comps[0].v > 2) ok = false;
            for (int c = 1; c < d.ncomp; ++c)
                if (d.comps[c].h != 1 || d.comps[c].v != 1) ok = false;
            sof = true;
            if (headers_only) break;
        } else if (mk == 0xDA) {
            if (!sof) {
                ok = false;
                break;
            }
            Scan s;
            s.at = next;
            s.ncomp = blen ? seg[0] : 0;
            if (s.ncomp < 1 || s.ncomp > kMaxComps || blen < 4 + 2 * static_cast<uint32_t>(s.ncomp)) {
                ok = false;
                break;
            }
            int td[kMaxComps] = {}, ta[kMaxComps] = {};
            for (int i = 0; i < s.ncomp; ++i) {
                const int id = seg[1 + i * 2];
                s.comp[i] = -1;
                for (int c = 0; c < d.ncomp; ++c)
                    if (d.comps[c].id == id) s.comp[i] = c;
                if (s.comp[i] < 0) ok = false;
                td[i] = seg[2 + i * 2] >> 4 & 3;
                ta[i] = seg[2 + i * 2] & 3;
            }
            if (!ok) break;
            const uint8_t* p = seg + 1 + s.ncomp * 2;
            s.ss = p[0];
            s.se = p[1];
            s.ah = p[2] >> 4;
            s.al = p[2] & 15;
            s.restart = d.restart;
            s.found = true;
            const bool all = s.ncomp == d.ncomp;
            if (d.baseline && !d.main.found && all) {
                d.main = s;
                for (int i = 0; i < s.ncomp; ++i) {
                    d.scan_dc[s.comp[i]] = d.dc[td[i]];
                    d.scan_ac[s.comp[i]] = d.ac[ta[i]];
                }
            } else if (d.progressive && !d.main.found && all && s.ss == 0 && s.ah == 0) {
                d.main = s;
                for (int i = 0; i < s.ncomp; ++i) d.scan_dc[s.comp[i]] = d.dc[td[i]];
            } else if (d.progressive && !d.luma.found && s.ncomp == 1 && s.comp[0] == 0 && s.ss == 1 && s.se >= 1 &&
                       s.ah == 0) {
                d.luma = s;
                d.luma_ac = d.ac[ta[0]];
            }
            for (int i = 0; i < s.ncomp; ++i) {
                const int c = s.comp[i];
                uint16_t* qs = d.qs[c];
                const uint16_t* q = d.q[d.comps[c].tq];
                if (qs[0] == 0) {
                    qs[0] = q[0];
                    qs[1] = q[1];
                    qs[2] = q[2];
                    qs[3] = q[4];
                }
            }
            if (d.baseline) break;          // (one scan: all there is)
            if (d.main.found && d.luma.found) break;
            // Over the scan's data to the next marker
            uint32_t at = next;
            bool done = false;
            uint8_t chunk[256];
            while (!done && at < size) {
                const size_t got = src.read_at(at, chunk, sizeof chunk);
                if (got < 2) {
                    at = size;
                    break;
                }
                for (size_t i = 0; i + 1 < got; ++i)
                    if (chunk[i] == 0xFF && chunk[i + 1] != 0 && !(chunk[i + 1] >= 0xD0 && chunk[i + 1] <= 0xD7) &&
                        chunk[i + 1] != 0xFF) {
                        at += static_cast<uint32_t>(i);
                        done = true;
                        break;
                    }
                if (!done) at += static_cast<uint32_t>(got - 1);
            }
            pos = at;
            continue;
        }
        pos = next;
    }
    free(seg);
    if (!sof) return false;
    if (headers_only) return ok;
    return ok && d.main.found;
}

// Y Cb Cr -> RGB
inline uint8_t clamp8(int v) { return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }
inline void ycc(int y, int cb, int cr, uint8_t* o)
{
    cb -= 128;
    cr -= 128;
    o[0] = clamp8(y + ((91881 * cr) >> 16));
    o[1] = clamp8(y - ((22554 * cb + 46802 * cr) >> 16));
    o[2] = clamp8(y + ((116130 * cb) >> 16));
}

} // namespace

bool probe(dax::ByteSource& src, Info& info)
{
    Dec* d = new (std::nothrow) Dec;
    if (!d) return false;
    const bool ok = parse(src, *d, true);
    info.width = d->width;
    info.height = d->height;
    info.out_w = (d->width + 3) / 4;
    info.out_h = (d->height + 3) / 4;
    info.progressive = d->progressive;
    delete d;
    return ok;
}

bool decode(dax::ByteSource& src, RowFn row, void* ctx, Info* info)
{
    Dec* d = new (std::nothrow) Dec;
    if (!d) return false;
    bool ok = parse(src, *d, false);
    const int H = d->comps[0].h, V = d->comps[0].v;
    const int mcux = (d->width + 8 * H - 1) / (8 * H), mcuy = (d->height + 8 * V - 1) / (8 * V);
    const int bwp = mcux * H;                           // luma blocks a row (padded to the MCUs)
    const int bw = (d->width + 7) / 8, bh = (d->height + 7) / 8;
    const int qw = bwp * 2;                             // quarter pixels a row (padded)
    const int out_w = (d->width + 3) / 4, out_h = (d->height + 3) / 4;
    const bool shape = d->baseline || d->luma.found;
    if (info) {
        info->width = d->width;
        info->height = d->height;
        info->out_w = out_w;
        info->out_h = out_h;
        info->progressive = d->progressive;
        info->shape = shape;
    }
    // Buffers: the luma's quarter rows (this MCU row, the last), its
    // blocks' coefficients (V block rows), the chroma's DC (three MCU rows)
    const size_t ysz = static_cast<size_t>(2 * V) * qw;
    uint8_t* yq[2] = {nullptr, nullptr};
    int16_t* co = nullptr;                              // [V][bwp][4]: DC, zz 1, 2, 4
    uint8_t* ch = nullptr;                              // [3 rows][2][mcux]
    uint8_t* line = nullptr;
    Bits* b1 = new (std::nothrow) Bits;
    Bits* b2 = shape && d->progressive ? new (std::nothrow) Bits : nullptr;
    if (ok) {
        yq[0] = static_cast<uint8_t*>(malloc(ysz));
        yq[1] = static_cast<uint8_t*>(malloc(ysz));
        co = static_cast<int16_t*>(malloc(static_cast<size_t>(V) * bwp * 4 * sizeof(int16_t)));
        ch = static_cast<uint8_t*>(malloc(static_cast<size_t>(3) * 2 * mcux));
        line = static_cast<uint8_t*>(malloc(static_cast<size_t>(out_w) * 3));
        ok = yq[0] && yq[1] && co && ch && line && b1 && (!shape || !d->progressive || b2);
    }
    if (ok) {
        b1->start(src, d->main.at);
        if (b2) b2->start(src, d->luma.at);
        int pred[kMaxComps] = {};
        int eobrun = 0;
        int mcus_left = d->main.restart, blocks_left = d->luma.restart;
        const int al1 = d->main.al, al2 = d->luma.al;
        const int se2 = d->luma.se;
        // Quarter values from a block's coefficients (dequantized): F00 / 8
        // + 128 and the quarters' differences (T.81 A.3.3's IDCT averaged
        // over 4 x 4: a = the mean of cos((2x + 1)pi / 16), x = 0-3)
        const float a = 0.6407288f;
        const float ka = a / (4.0f * 1.4142136f), kb = a * a / 4.0f;
        const uint16_t* qy = d->qs[0];
        int emitted = 0;
        auto emit = [&](int r) -> bool {
            // MCU row r's quarter rows (its luma in yq[r & 1], the chroma rows r-1 .. r+1 kept)
            const uint8_t* ys = yq[r & 1];
            for (int yy = 0; yy < 2 * V; ++yy) {
                const int y = r * 2 * V + yy;
                if (y >= out_h) return true;
                // the chroma rows around this one: sample positions in MCU rows
                const float fy = (yy + 0.5f) / (2.0f * V) - 0.5f;      // -0.5 .. 0.5 about row r
                const int r0 = fy < 0 ? r - 1 : r;
                const float wy = fy < 0 ? fy + 1.0f : fy;              // weight of r0 + 1
                const int ra = r0 < 0 ? 0 : r0, rb = r0 + 1 > mcuy - 1 ? mcuy - 1 : r0 + 1;
                for (int x = 0; x < out_w; ++x) {
                    const int lum = ys[yy * qw + x];
                    uint8_t* o = line + x * 3;
                    if (d->ncomp == 1) {
                        o[0] = o[1] = o[2] = static_cast<uint8_t>(lum);
                        continue;
                    }
                    const float fx = (x + 0.5f) / (2.0f * H) - 0.5f;
                    int c0 = fx < 0 ? 0 : static_cast<int>(fx);
                    float wx = fx < 0 ? 0.0f : fx - c0;
                    int c1 = c0 + 1 < mcux ? c0 + 1 : mcux - 1;
                    int cc[2];
                    for (int k = 0; k < 2; ++k) {
                        const uint8_t* A = ch + ((ra % 3) * 2 + k) * mcux;
                        const uint8_t* B = ch + ((rb % 3) * 2 + k) * mcux;
                        const float top = A[c0] + (A[c1] - A[c0]) * wx;
                        const float bot = B[c0] + (B[c1] - B[c0]) * wx;
                        cc[k] = static_cast<int>(top + (bot - top) * wy + 0.5f);
                    }
                    ycc(lum, cc[0], cc[1], o);
                }
                if (!row(y, line, out_w, ctx)) return false;
            }
            return true;
        };
        for (int my = 0; ok && my < mcuy; ++my) {
            memset(co, 0, static_cast<size_t>(V) * bwp * 4 * sizeof(int16_t));
            for (int mx = 0; ok && mx < mcux; ++mx) {
                if (d->main.restart && mcus_left == 0) {
                    b1->restart();
                    for (int& p : pred) p = 0;
                    mcus_left = d->main.restart;
                }
                for (int ci = 0; ci < d->main.ncomp; ++ci) {
                    const int c = d->main.comp[ci];
                    const Comp& cp = d->comps[c];
                    for (int by = 0; by < cp.v; ++by)
                        for (int bx = 0; bx < cp.h; ++bx) {
                            const int s = b1->decode(d->scan_dc[c]);
                            pred[c] += s ? b1->extend(s) : 0;
                            const int dcv = pred[c] * (1 << al1);
                            int16_t lo[3] = {0, 0, 0};
                            if (d->baseline) {
                                for (int k = 1; k < 64;) {
                                    const int rs = b1->decode(d->scan_ac[c]);
                                    const int rr = rs >> 4, ss = rs & 15;
                                    if (ss == 0) {
                                        if (rr == 15) {
                                            k += 16;
                                            continue;
                                        }
                                        break;
                                    }
                                    k += rr;
                                    const int v = b1->extend(ss);
                                    if (k == 1) lo[0] = static_cast<int16_t>(v);
                                    else if (k == 2) lo[1] = static_cast<int16_t>(v);
                                    else if (k == 4) lo[2] = static_cast<int16_t>(v);
                                    ++k;
                                }
                            }
                            if (c == 0) {
                                int16_t* e = co + (static_cast<size_t>(by) * bwp + mx * H + bx) * 4;
                                e[0] = static_cast<int16_t>(dcv);
                                if (d->baseline) {
                                    e[1] = lo[0];
                                    e[2] = lo[1];
                                    e[3] = lo[2];
                                }
                            } else {
                                const int v = dcv * d->qs[c][0] / 8 + 128;
                                ch[((my % 3) * 2 + (c - 1)) * mcux + mx] = clamp8(v);
                            }
                        }
                }
                if (b1->bad) ok = false;
                if (d->main.restart) --mcus_left;
            }
            // The luma's first AC scan, for these block rows (raster order)
            if (ok && b2) {
                for (int by = 0; by < V; ++by) {
                    const int gy = my * V + by;
                    if (gy >= bh) break;
                    for (int bx = 0; bx < bw; ++bx) {
                        if (d->luma.restart && blocks_left == 0) {
                            b2->restart();
                            eobrun = 0;
                            blocks_left = d->luma.restart;
                        }
                        if (d->luma.restart) --blocks_left;
                        if (eobrun > 0) {
                            --eobrun;
                            continue;
                        }
                        int16_t* e = co + (static_cast<size_t>(by) * bwp + bx) * 4;
                        for (int k = 1; k <= se2;) {
                            const int rs = b2->decode(d->luma_ac);
                            const int rr = rs >> 4, ss = rs & 15;
                            if (ss == 0) {
                                if (rr < 15) {
                                    eobrun = (1 << rr) - 1;
                                    if (rr) eobrun += b2->bits(rr);
                                    break;
                                }
                                k += 16;
                                continue;
                            }
                            k += rr;
                            const int v = b2->extend(ss) * (1 << al2);
                            if (k == 1) e[1] = static_cast<int16_t>(v);
                            else if (k == 2) e[2] = static_cast<int16_t>(v);
                            else if (k == 4) e[3] = static_cast<int16_t>(v);
                            ++k;
                        }
                    }
                    if (b2->bad) ok = false;
                }
            }
            if (!ok) break;
            // The quarters
            uint8_t* ys = yq[my & 1];
            for (int by = 0; by < V; ++by)
                for (int bx = 0; bx < bwp; ++bx) {
                    const int16_t* e = co + (static_cast<size_t>(by) * bwp + bx) * 4;
                    const float base = e[0] * qy[0] / 8.0f + 128.0f;
                    const float hx = ka * e[1] * qy[1], hy = ka * e[2] * qy[2], hxy = kb * e[3] * qy[3];
                    uint8_t* p = ys + (by * 2) * qw + bx * 2;
                    p[0] = clamp8(static_cast<int>(base + hx + hy + hxy + 0.5f));
                    p[1] = clamp8(static_cast<int>(base - hx + hy - hxy + 0.5f));
                    p[qw] = clamp8(static_cast<int>(base + hx - hy - hxy + 0.5f));
                    p[qw + 1] = clamp8(static_cast<int>(base - hx - hy + hxy + 0.5f));
                }
            // The MCU row before this one can go now (its chroma neighbours are here)
            if (my > 0) {
                if (!emit(my - 1)) break;
                emitted = my;
            }
        }
        if (ok && emitted < mcuy && mcuy > 0) emit(mcuy - 1);
    }
    free(yq[0]);
    free(yq[1]);
    free(co);
    free(ch);
    free(line);
    delete b1;
    delete b2;
    delete d;
    return ok;
}

// ---- Fit -------------------------------------------------------------------

Fit::~Fit()
{
    free(acc_);
    free(wsum_);
    free(hrow_);
    free(line_);
    free(slot_row_);
}

bool Fit::begin(int src_w, int src_h, int tw, int th, OutFn out, void* ctx, bool top)
{
    if (src_w <= 0 || src_h <= 0 || tw <= 0 || th <= 0) return false;
    src_w_ = src_w;
    src_h_ = src_h;
    tw_ = tw;
    th_ = th;
    out_ = out;
    ctx_ = ctx;
    const float sx = static_cast<float>(tw) / src_w, sy = static_cast<float>(th) / src_h;
    scale_ = sx > sy ? sx : sy;
    ox_ = (src_w * scale_ - tw) / 2.0f;
    oy_ = top ? 0.0f : (src_h * scale_ - th) / 2.0f;
    radius_ = scale_ < 1.0f ? 1.0f / scale_ : 1.0f;
    slots_ = static_cast<int>(std::ceil(2.0f * radius_ * scale_)) + 2;
    next_src_ = next_out_ = 0;
    free(acc_);
    free(wsum_);
    free(hrow_);
    free(line_);
    free(slot_row_);
    acc_ = static_cast<int32_t*>(malloc(static_cast<size_t>(slots_) * tw * 3 * sizeof(int32_t)));
    wsum_ = static_cast<int32_t*>(malloc(static_cast<size_t>(slots_) * sizeof(int32_t)));
    slot_row_ = static_cast<int*>(malloc(static_cast<size_t>(slots_) * sizeof(int)));
    hrow_ = static_cast<uint8_t*>(malloc(static_cast<size_t>(tw) * 3));
    line_ = static_cast<uint8_t*>(malloc(static_cast<size_t>(tw) * 3));
    if (!acc_ || !wsum_ || !hrow_ || !line_ || !slot_row_) return false;
    for (int i = 0; i < slots_; ++i) slot_row_[i] = -1;
    return true;
}

namespace {
inline int tri(float dist, float radius)
{
    const float w = 1.0f - (dist < 0 ? -dist : dist) / radius;
    return w > 0 ? static_cast<int>(w * 256.0f + 0.5f) : 0;
}
} // namespace

bool Fit::emit(int slot)
{
    const int32_t* a = acc_ + static_cast<size_t>(slot) * tw_ * 3;
    const int32_t ws = wsum_[slot] > 0 ? wsum_[slot] : 1;
    for (int i = 0; i < tw_ * 3; ++i) line_[i] = static_cast<uint8_t>((a[i] + ws / 2) / ws);
    const int y = slot_row_[slot];
    slot_row_[slot] = -1;
    return out_(y, line_, tw_, ctx_);
}

bool Fit::add(const uint8_t* rgb)
{
    if (!acc_) return false;
    const int i = next_src_++;
    // Across
    for (int x = 0; x < tw_; ++x) {
        const float u = (x + ox_ + 0.5f) / scale_ - 0.5f;
        int lo = static_cast<int>(std::floor(u - radius_)) + 1, hi = static_cast<int>(std::ceil(u + radius_)) - 1;
        int32_t s[3] = {0, 0, 0}, ws = 0;
        for (int k = lo; k <= hi; ++k) {
            const int w = tri(k - u, radius_);
            if (!w) continue;
            const int kk = k < 0 ? 0 : k >= src_w_ ? src_w_ - 1 : k;
            const uint8_t* p = rgb + kk * 3;
            s[0] += w * p[0];
            s[1] += w * p[1];
            s[2] += w * p[2];
            ws += w;
        }
        if (!ws) ws = 1;
        uint8_t* o = hrow_ + x * 3;
        o[0] = static_cast<uint8_t>((s[0] + ws / 2) / ws);
        o[1] = static_cast<uint8_t>((s[1] + ws / 2) / ws);
        o[2] = static_cast<uint8_t>((s[2] + ws / 2) / ws);
    }
    // Down: into the output rows this source row reaches (the edges: the first / last row stands in)
    for (int y = next_out_; y < th_; ++y) {
        const float v = (y + oy_ + 0.5f) / scale_ - 0.5f;
        if (v - radius_ >= i && i != 0) break;
        float dist = i - v;
        if (i == 0 && v < 0) dist = 0;                          // (above the top: the top row)
        if (i == src_h_ - 1 && v > i) dist = 0;                 // (below the bottom: the bottom row)
        const int w = tri(dist, radius_);
        if (!w) {
            if (i < v) break;
            continue;
        }
        const int slot = y % slots_;
        if (slot_row_[slot] != y) {
            if (slot_row_[slot] >= 0 && !emit(slot)) return false;
            slot_row_[slot] = y;
            memset(acc_ + static_cast<size_t>(slot) * tw_ * 3, 0, static_cast<size_t>(tw_) * 3 * sizeof(int32_t));
            wsum_[slot] = 0;
        }
        int32_t* a = acc_ + static_cast<size_t>(slot) * tw_ * 3;
        for (int k = 0; k < tw_ * 3; ++k) a[k] += w * hrow_[k];
        wsum_[slot] += w;
    }
    // Rows done: every source row they take has come
    while (next_out_ < th_) {
        const float v = (next_out_ + oy_ + 0.5f) / scale_ - 0.5f;
        if (static_cast<float>(i) < v + radius_ - 1.0f && i < src_h_ - 1) break;
        const int slot = next_out_ % slots_;
        if (slot_row_[slot] == next_out_ && !emit(slot)) return false;
        ++next_out_;
    }
    return true;
}

bool Fit::finish()
{
    while (next_out_ < th_) {
        const int slot = next_out_ % slots_;
        if (slot_row_[slot] == next_out_ && !emit(slot)) return false;
        ++next_out_;
    }
    return true;
}

} // namespace jthumb

#include "geo.h"

#include <cstdlib>

namespace geo {

namespace {

const int8_t kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int8_t kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

// The square's byte in each plane; -1: off a map that doesn't wrap
int at(const Map& m, int x, int y)
{
    if (m.wraps) {
        x %= m.w;
        y %= m.h;
        if (x < 0) x += m.w;
        if (y < 0) y += m.h;
    } else if (!m.on(x, y)) {
        return -1;
    }
    return x + y * m.w;
}

} // namespace

int dx(int dir) { return kDx[dir & 7]; }
int dy(int dir) { return kDy[dir & 7]; }

const char* dir_name(int dir)
{
    static const char* const kNames[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return kNames[dir & 7];
}

void release(Map& m)
{
    free(m.big);
    m.big = nullptr;
    m.w = m.h = kSize;
    m.wraps = true;
    m.loaded = false;
}

bool load(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Map& out)
{
    release(out);
    const dax::Entry* e = idx.find(block);
    if (!e || e->raw_size != kBlockBytes) return false;
    dax::RleReader r(src, idx, *e);
    if (r.skip(2) != 2 || r.read(&out.plane[0][0], sizeof out.plane) != sizeof out.plane) return false;
    out.loaded = true;
    return true;
}

bool load_hlib(dax::ByteSource& src, uint32_t pos, uint32_t len, Map& out)
{
    release(out);
    uint8_t h[kHlibHeader];
    if (len < kHlibHeader || src.read_at(pos, h, sizeof h) != sizeof h) return false;
    const int w = h[0], ht = h[1], n = w * ht;
    if (!w || !ht || n > kMaxSquares || len < static_cast<uint32_t>(kHlibHeader + 4 * n)) return false;
    if (n > 256) {
        out.big = static_cast<uint8_t*>(malloc(static_cast<size_t>(4 * n)));
        if (!out.big) return false;
    }
    out.w = static_cast<uint8_t>(w);
    out.h = static_cast<uint8_t>(ht);
    out.wraps = false;
    for (int k = 0; k < 6; ++k) out.head[k] = h[2 + k];
    // (a map of up to 256 squares: each plane in plane[k], x + y * w)
    for (int k = 0; k < 4; ++k)
        if (src.read_at(pos + kHlibHeader + static_cast<uint32_t>(k * n), out.pl(k), static_cast<size_t>(n)) !=
            static_cast<size_t>(n)) {
            release(out);
            return false;
        }
    out.loaded = true;
    return true;
}

int wall(const Map& m, int x, int y, int dir)
{
    const int i = at(m, x, y);
    if (i < 0) return 0;
    switch (dir & 7) {
    case 0: return m.pl(0)[i] >> 4;
    case 2: return m.pl(0)[i] & 15;
    case 4: return m.pl(1)[i] >> 4;
    case 6: return m.pl(1)[i] & 15;
    }
    return 0;
}

int door(const Map& m, int x, int y, int dir)
{
    const int i = at(m, x, y);
    if (i < 0) return 0;
    const uint8_t b = m.pl(3)[i];
    switch (dir & 7) {
    case 0: return b & 3;
    case 2: return (b >> 2) & 3;
    case 4: return (b >> 4) & 3;
    case 6: return (b >> 6) & 3;
    }
    return 0;
}

uint8_t flags(const Map& m, int x, int y)
{
    const int i = at(m, x, y);
    return i < 0 ? 0 : m.pl(2)[i];
}

int passage(const Map& m, int x, int y, int dir)
{
    if (wall(m, x, y, dir) == 0) return 1;
    return door(m, x, y, dir);
}

bool step(const Map& m, int& x, int& y, int dir)
{
    int nx = x + dx(dir), ny = y + dy(dir);
    if (m.wraps) {
        nx = ((nx % m.w) + m.w) % m.w;
        ny = ((ny % m.h) + m.h) % m.h;
    } else if (!m.on(nx, ny)) {
        return false;
    }
    x = nx;
    y = ny;
    return true;
}

namespace {
void set_open(Map& m, int x, int y, int dir)
{
    if (!m.on(x, y)) return;
    const int shift = (dir & 7) / 2 * 2;
    uint8_t& b = m.pl(3)[x + y * m.w];
    b = static_cast<uint8_t>((b & ~(3 << shift)) | (1 << shift));
}
} // namespace

void unlock(Map& m, int x, int y, int dir)
{
    set_open(m, x, y, dir);
    set_open(m, x + dx(dir), y + dy(dir), (dir + 4) & 7);
}

} // namespace geo

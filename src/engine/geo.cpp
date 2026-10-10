#include "geo.h"

namespace geo {

namespace {

const int8_t kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1};
const int8_t kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

int at(int x, int y) { return (x & 15) + (y & 15) * kSize; }

} // namespace

int dx(int dir) { return kDx[dir & 7]; }
int dy(int dir) { return kDy[dir & 7]; }

const char* dir_name(int dir)
{
    static const char* const kNames[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return kNames[dir & 7];
}

bool load(dax::ByteSource& src, const dax::Index& idx, uint8_t block, Map& out)
{
    out.loaded = false;
    const dax::Entry* e = idx.find(block);
    if (!e || e->raw_size != kBlockBytes) return false;
    dax::RleReader r(src, idx, *e);
    if (r.skip(2) != 2 || r.read(&out.plane[0][0], sizeof out.plane) != sizeof out.plane) return false;
    out.loaded = true;
    return true;
}

int wall(const Map& m, int x, int y, int dir)
{
    const int i = at(x, y);
    switch (dir & 7) {
    case 0: return m.plane[0][i] >> 4;
    case 2: return m.plane[0][i] & 15;
    case 4: return m.plane[1][i] >> 4;
    case 6: return m.plane[1][i] & 15;
    }
    return 0;
}

int door(const Map& m, int x, int y, int dir)
{
    const int i = at(x, y);
    switch (dir & 7) {
    case 0: return m.plane[3][i] & 3;
    case 2: return (m.plane[3][i] >> 2) & 3;
    case 4: return (m.plane[3][i] >> 4) & 3;
    case 6: return (m.plane[3][i] >> 6) & 3;
    }
    return 0;
}

uint8_t flags(const Map& m, int x, int y) { return m.plane[2][at(x, y)]; }

int passage(const Map& m, int x, int y, int dir)
{
    if (wall(m, x, y, dir) == 0) return 1;
    return door(m, x, y, dir);
}

namespace {
void set_open(Map& m, int x, int y, int dir)
{
    if (x < 0 || x > 15 || y < 0 || y > 15) return;
    const int shift = (dir & 7) / 2 * 2;
    uint8_t& b = m.plane[3][x + y * 16];
    b = static_cast<uint8_t>((b & ~(3 << shift)) | (1 << shift));
}
} // namespace

void unlock(Map& m, int x, int y, int dir)
{
    set_open(m, x, y, dir);
    set_open(m, x + dx(dir), y + dy(dir), (dir + 4) & 7);
}

} // namespace geo

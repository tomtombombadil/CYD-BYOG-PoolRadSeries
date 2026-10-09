#include "jpeg.h"

#include <cstdint>
#include <cstring>

namespace tjpgd {
#include "third_party/tjpgd.inc"
}

namespace jpeg {

namespace {

struct Ctx {
    dax::ByteSource* src;
    uint32_t pos, end;
    WantFn want;
    BlockFn block;
    void* user;
    bool stopped;
};

size_t in_fn(tjpgd::JDEC* jd, uint8_t* buf, size_t n)
{
    Ctx& c = *static_cast<Ctx*>(jd->device);
    if (c.pos + n > c.end) n = c.end - c.pos;
    if (!buf) {                      // skip
        c.pos += static_cast<uint32_t>(n);
        return n;
    }
    const size_t got = c.src->read_at(c.pos, buf, n);
    c.pos += static_cast<uint32_t>(got);
    return got;
}

int want_fn(tjpgd::JDEC* jd, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    Ctx& c = *static_cast<Ctx*>(jd->device);
    const int r = c.want(static_cast<int>(x), static_cast<int>(y), static_cast<int>(w), static_cast<int>(h), c.user);
    if (r < 0) c.stopped = true;
    return r;
}

int out_fn(tjpgd::JDEC* jd, void* bitmap, tjpgd::JRECT* r)
{
    Ctx& c = *static_cast<Ctx*>(jd->device);
    if (!c.block) return 1;
    const bool go = c.block(r->left, r->top, r->right - r->left + 1, r->bottom - r->top + 1,
                            static_cast<const uint8_t*>(bitmap), c.user);
    if (!go) c.stopped = true;
    return go ? 1 : 0;
}

bool prepare(tjpgd::JDEC& jd, Ctx& c, void* pool, Info* info)
{
    jd.want = nullptr;
    if (tjpgd::jd_prepare(&jd, in_fn, pool, kPoolSize, &c) != tjpgd::JDR_OK) return false;
    jd.want = c.want ? want_fn : nullptr;
    jd.skip = 0;
    if (info) {
        info->width = jd.width;
        info->height = jd.height;
        info->mcu_w = jd.msx * 8;
        info->mcu_h = jd.msy * 8;
    }
    return true;
}

} // namespace

bool decode(dax::ByteSource& src, uint32_t at, uint32_t len, void* pool, WantFn want, BlockFn block, void* ctx,
            Info* info, int scale)
{
    Ctx c{&src, at, at + len, want, block, ctx, false};
    tjpgd::JDEC jd;
    if (scale < 0 || scale > 3 || !prepare(jd, c, pool, info)) return false;
    const tjpgd::JRESULT r = tjpgd::jd_decomp(&jd, out_fn, static_cast<uint8_t>(scale));
    return r == tjpgd::JDR_OK || (r == tjpgd::JDR_INTR && c.stopped);
}

bool probe(dax::ByteSource& src, uint32_t at, uint32_t len, void* pool, Info& info)
{
    Ctx c{&src, at, at + len, nullptr, nullptr, nullptr, false};
    tjpgd::JDEC jd;
    return prepare(jd, c, pool, &info);
}

} // namespace jpeg

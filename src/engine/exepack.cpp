#include "exepack.h"

#include <cstring>

namespace exepack {

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

// Reads the load module walking downwards: a 256-byte window that ends at
// the byte asked for, so a backwards walk reloads once every 256 bytes.
class Backwards {
public:
    Backwards(dax::ByteSource& s, uint32_t base) : s_(s), base_(base) {}
    bool get(uint32_t p, uint8_t& v)
    {
        if (p < lo_ || p >= hi_) {
            const uint32_t end = p + 1;
            const uint32_t start = end > sizeof buf_ ? end - sizeof buf_ : 0;
            const size_t want = end - start;
            if (s_.read_at(base_ + start, buf_, want) != want) return false;
            lo_ = start;
            hi_ = end;
        }
        v = buf_[p - lo_];
        return true;
    }
private:
    dax::ByteSource& s_;
    uint32_t base_;
    uint8_t  buf_[256];
    uint32_t lo_ = 1, hi_ = 0;     // empty
};

} // namespace

const char* status_text(Status s)
{
    switch (s) {
    case Status::Ok:        return "ok";
    case Status::NotExe:    return "not a DOS program";
    case Status::NotPacked: return "not packed with EXEPACK";
    case Status::BadData:   return "packed data is damaged";
    case Status::ReadError: return "read error";
    }
    return "?";
}

Status parse(dax::ByteSource& exe, Info& out)
{
    out = Info{};
    uint8_t mz[0x1C];
    if (exe.size() < sizeof mz) return Status::NotExe;
    if (exe.read_at(0, mz, sizeof mz) != sizeof mz) return Status::ReadError;
    if (!((mz[0] == 'M' && mz[1] == 'Z') || (mz[0] == 'Z' && mz[1] == 'M'))) return Status::NotExe;
    const uint32_t load_start = static_cast<uint32_t>(u16(mz + 8)) * 16;
    const uint16_t cs = u16(mz + 0x16);
    const uint32_t hdr_at = load_start + static_cast<uint32_t>(cs) * 16;
    uint8_t h[18];
    if (hdr_at + sizeof h > exe.size()) return Status::NotPacked;
    if (exe.read_at(hdr_at, h, sizeof h) != sizeof h) return Status::ReadError;
    uint16_t skip = 1;
    if (h[16] == 'R' && h[17] == 'B') skip = u16(h + 14);
    else if (!(h[14] == 'R' && h[15] == 'B')) return Status::NotPacked;
    if (skip == 0) return Status::BadData;

    const uint32_t skip_bytes = static_cast<uint32_t>(skip - 1) * 16;
    const uint32_t packed_area = static_cast<uint32_t>(cs) * 16;
    if (skip_bytes > packed_area) return Status::BadData;
    out.load_start = load_start;
    out.packed_end = packed_area - skip_bytes;
    out.image_size = static_cast<uint32_t>(u16(h + 12)) * 16;
    out.ip = u16(h + 0);
    out.cs = u16(h + 2);
    out.sp = u16(h + 8);
    out.ss = u16(h + 10);
    if (out.image_size < out.packed_end) return Status::BadData;
    return Status::Ok;
}

Status read(dax::ByteSource& exe, const Info& info, uint32_t pos, uint8_t* buf, size_t n)
{
    if (n == 0) return Status::Ok;
    if (pos > info.image_size || n > info.image_size - pos) return Status::BadData;
    const uint32_t want_end = pos + static_cast<uint32_t>(n);
    Backwards in(exe, info.load_start);
    uint8_t b;

    uint32_t src = info.packed_end;
    for (;;) {
        if (src == 0) return Status::BadData;
        if (!in.get(src - 1, b)) return Status::ReadError;
        if (b != 0xFF) break;
        --src;
    }
    uint32_t dst = info.image_size;

    for (;;) {
        if (src < 3) return Status::BadData;
        uint8_t cmd, hi, lo;
        if (!in.get(src - 1, cmd) || !in.get(src - 2, hi) || !in.get(src - 3, lo)) return Status::ReadError;
        src -= 3;
        const uint32_t len = static_cast<uint32_t>(hi) << 8 | lo;
        if (len > dst) return Status::BadData;
        // The output run is [dst - len, dst); the part the caller wants:
        const uint32_t a = dst - len > pos ? dst - len : pos;
        const uint32_t z = dst < want_end ? dst : want_end;
        if ((cmd & 0xFE) == 0xB0) {
            if (src < 1) return Status::BadData;
            uint8_t v;
            if (!in.get(src - 1, v)) return Status::ReadError;
            --src;
            if (a < z) memset(buf + (a - pos), v, z - a);
        } else if ((cmd & 0xFE) == 0xB2) {
            if (len > src) return Status::BadData;
            // Output byte k comes from packed byte src - dst + k; walk down
            // so the window keeps moving one way.
            for (uint32_t k = z; k > a; --k) {
                if (!in.get(src - dst + k - 1, b)) return Status::ReadError;
                buf[k - 1 - pos] = b;
            }
            src -= len;
        } else {
            return Status::BadData;
        }
        dst -= len;
        if (src > dst) return Status::BadData;     // would overwrite unread data
        if (cmd & 1) break;
    }

    // Below the last output byte the program was never packed
    for (uint32_t k = want_end < dst ? want_end : dst; k > pos; --k) {
        if (!in.get(k - 1, b)) return Status::ReadError;
        buf[k - 1 - pos] = b;
    }
    return Status::Ok;
}

} // namespace exepack

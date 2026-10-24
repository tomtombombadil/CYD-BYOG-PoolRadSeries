#include "exepack.h"

#include <cstring>

namespace exepack {

namespace {

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t u32(const uint8_t* p) { return static_cast<uint32_t>(u16(p)) | static_cast<uint32_t>(u16(p + 2)) << 16; }
void put16(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
}
void put32(uint8_t* p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}
constexpr uint8_t kCopyMagic[8] = {'C', 'Y', 'D', 'X', 'P', 'K', '1', 0};

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
    uint8_t mz[kCopyHeader];
    if (exe.size() < 0x1C) return Status::NotExe;
    if (exe.read_at(0, mz, 0x1C) != 0x1C) return Status::ReadError;
    if (memcmp(mz, kCopyMagic, 8) == 0) {
        // The unpacked copy (write_copy)
        if (exe.size() < kCopyHeader || exe.read_at(0, mz, kCopyHeader) != kCopyHeader) return Status::BadData;
        out.file_size = u32(mz + 8);
        out.image_size = u32(mz + 16);
        out.ip = u16(mz + 20);
        out.cs = u16(mz + 22);
        out.sp = u16(mz + 24);
        out.ss = u16(mz + 26);
        out.copy_at = kCopyHeader;
        if (exe.size() != kCopyHeader + out.image_size) return Status::BadData;
        return Status::Ok;
    }
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
    out.file_size = exe.size();
    if (out.image_size < out.packed_end) return Status::BadData;
    return Status::Ok;
}

Status read(dax::ByteSource& exe, const Info& info, uint32_t pos, uint8_t* buf, size_t n)
{
    if (n == 0) return Status::Ok;
    if (pos > info.image_size || n > info.image_size - pos) return Status::BadData;
    const uint32_t want_end = pos + static_cast<uint32_t>(n);
    if (info.copy_at) {
        // The copy: top byte first, so the piece is one read, turned round
        if (exe.read_at(info.copy_at + info.image_size - want_end, buf, n) != n) return Status::ReadError;
        for (size_t i = 0, j = n - 1; i < j; ++i, --j) {
            const uint8_t t = buf[i];
            buf[i] = buf[j];
            buf[j] = t;
        }
        return Status::Ok;
    }
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

uint32_t checksum(dax::ByteSource& exe)
{
    uint32_t h = 2166136261u;
    uint8_t buf[256];
    const uint32_t size = exe.size();
    for (uint32_t at = 0; at < size;) {
        const size_t want = size - at < sizeof buf ? size - at : sizeof buf;
        if (exe.read_at(at, buf, want) != want) return 0;
        for (size_t i = 0; i < want; ++i) h = (h ^ buf[i]) * 16777619u;
        at += static_cast<uint32_t>(want);
    }
    return h;
}

namespace {

// The copy's bytes out in order, 256 at a time
class Out {
public:
    explicit Out(Sink& s) : s_(s) {}
    bool put(uint8_t v)
    {
        buf_[n_++] = v;
        return n_ < sizeof buf_ || flush();
    }
    bool flush()
    {
        const size_t n = n_;
        n_ = 0;
        return n == 0 || s_.put(buf_, n);
    }
private:
    Sink&   s_;
    uint8_t buf_[256];
    size_t  n_ = 0;
};

} // namespace

Status write_copy(dax::ByteSource& exe, const Info& info, uint32_t sum, Sink& sink)
{
    if (info.copy_at) return Status::BadData;
    uint8_t h[kCopyHeader] = {};
    memcpy(h, kCopyMagic, sizeof kCopyMagic);
    put32(h + 8, info.file_size);
    put32(h + 12, sum);
    put32(h + 16, info.image_size);
    put16(h + 20, info.ip);
    put16(h + 22, info.cs);
    put16(h + 24, info.sp);
    put16(h + 26, info.ss);
    if (!sink.put(h, sizeof h)) return Status::ReadError;

    // The unpacker's walk as read() makes it, every output byte from the
    // top down
    Backwards in(exe, info.load_start);
    Out out(sink);
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
        if ((cmd & 0xFE) == 0xB0) {
            if (src < 1) return Status::BadData;
            uint8_t v;
            if (!in.get(src - 1, v)) return Status::ReadError;
            --src;
            for (uint32_t k = 0; k < len; ++k)
                if (!out.put(v)) return Status::ReadError;
        } else if ((cmd & 0xFE) == 0xB2) {
            if (len > src) return Status::BadData;
            for (uint32_t k = dst; k > dst - len; --k) {
                if (!in.get(src - dst + k - 1, b)) return Status::ReadError;
                if (!out.put(b)) return Status::ReadError;
            }
            src -= len;
        } else {
            return Status::BadData;
        }
        dst -= len;
        if (src > dst) return Status::BadData;
        if (cmd & 1) break;
    }
    for (uint32_t k = dst; k > 0; --k) {
        if (!in.get(k - 1, b)) return Status::ReadError;
        if (!out.put(b)) return Status::ReadError;
    }
    return out.flush() ? Status::Ok : Status::ReadError;
}

bool copy_matches(dax::ByteSource& copy, uint32_t file_size, uint32_t sum)
{
    uint8_t h[kCopyHeader];
    if (copy.size() < kCopyHeader || copy.read_at(0, h, sizeof h) != sizeof h) return false;
    return memcmp(h, kCopyMagic, sizeof kCopyMagic) == 0 && u32(h + 8) == file_size && u32(h + 12) == sum &&
           copy.size() == kCopyHeader + u32(h + 16);
}

} // namespace exepack

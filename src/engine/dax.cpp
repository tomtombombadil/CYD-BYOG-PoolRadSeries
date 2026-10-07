#include "dax.h"

#include <cstring>

namespace dax {

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

size_t MemorySource::read_at(uint32_t pos, uint8_t* buf, size_t n)
{
    if (pos >= size_) return 0;
    if (n > size_ - pos) n = size_ - pos;
    memcpy(buf, data_ + pos, n);
    return n;
}

const char* status_text(Status s)
{
    switch (s) {
    case Status::Ok:           return "OK";
    case Status::ReadError:    return "Read error";
    case Status::BadIndex:     return "Not a DAX file (bad index)";
    case Status::BlockOutside: return "Block data past end of file";
    }
    return "?";
}

const Entry* Index::find(uint8_t id) const
{
    for (int i = 0; i < count; ++i)
        if (entries[i].id == id) return &entries[i];
    return nullptr;
}

Status read_index(ByteSource& src, Index& out)
{
    out.count = 0;
    out.data_start = 0;
    uint8_t hdr[2];
    if (src.read_at(0, hdr, 2) != 2) return Status::ReadError;
    const uint32_t index_bytes = le16(hdr);
    if (index_bytes == 0 || index_bytes % 9 != 0) return Status::BadIndex;
    const uint32_t n = index_bytes / 9;
    if (n > static_cast<uint32_t>(kMaxEntries) || 2 + index_bytes > src.size()) return Status::BadIndex;
    out.data_start = 2 + index_bytes;

    uint8_t rec[9];
    for (uint32_t i = 0; i < n; ++i) {
        if (src.read_at(2 + i * 9, rec, 9) != 9) return Status::ReadError;
        Entry& e = out.entries[i];
        e.id = rec[0];
        e.offset = le32(rec + 1);
        e.raw_size = le16(rec + 5);
        e.comp_size = le16(rec + 7);
        const uint64_t end = static_cast<uint64_t>(out.data_start) + e.offset + e.comp_size;
        if (end > src.size()) return Status::BlockOutside;
    }
    out.count = static_cast<int>(n);
    return Status::Ok;
}

RleReader::RleReader(ByteSource& src, const Index& index, const Entry& e)
    : src_(src), in_pos_(index.data_start + e.offset), in_left_(e.comp_size), raw_size_(e.raw_size)
{
}

int RleReader::in_byte()
{
    if (buf_at_ == buf_len_) {
        if (in_left_ == 0) return -1;
        const size_t want = in_left_ < kChunk ? in_left_ : kChunk;
        const size_t got = src_.read_at(in_pos_, buf_, want);
        if (got != want) {
            error_ = true;
            in_left_ = 0;
            if (got == 0) return -1;
        } else {
            in_left_ -= static_cast<uint32_t>(got);
        }
        in_pos_ += static_cast<uint32_t>(got);
        buf_len_ = static_cast<uint16_t>(got);
        buf_at_ = 0;
    }
    return buf_[buf_at_++];
}

int RleReader::next()
{
    if (produced_ >= raw_size_) return -1;
    if (run_ == 0) {
        const int c = in_byte();
        if (c < 0) return -1;
        const int8_t ctl = static_cast<int8_t>(c);
        if (ctl >= 0) {
            run_ = ctl + 1;
            repeat_ = -1;
        } else {
            // -128 (0x80) is taken as a run of 128; SSI's own files are not
            // known to use it.
            run_ = -static_cast<int>(ctl);
            repeat_ = in_byte();
            if (repeat_ < 0) { run_ = 0; return -1; }
        }
    }
    int b;
    if (repeat_ >= 0) {
        b = repeat_;
    } else {
        b = in_byte();
        if (b < 0) { run_ = 0; return -1; }
    }
    --run_;
    ++produced_;
    return b;
}

size_t RleReader::read(uint8_t* buf, size_t n)
{
    size_t i = 0;
    for (; i < n; ++i) {
        const int b = next();
        if (b < 0) break;
        buf[i] = static_cast<uint8_t>(b);
    }
    return i;
}

size_t RleReader::skip(size_t n)
{
    size_t i = 0;
    for (; i < n; ++i)
        if (next() < 0) break;
    return i;
}

uint32_t load_block(ByteSource& src, const Index& index, const Entry& e, uint8_t* out)
{
    RleReader r(src, index, e);
    return static_cast<uint32_t>(r.read(out, e.raw_size));
}

size_t compress(const uint8_t* in, size_t n, uint8_t* out)
{
    size_t o = 0, i = 0;
    while (i < n) {
        // A repeat of 3+ is worth a run (2 bytes).
        size_t rep = 1;
        while (i + rep < n && rep < 127 && in[i + rep] == in[i]) ++rep;
        if (rep >= 3) {
            out[o++] = static_cast<uint8_t>(-static_cast<int>(rep));
            out[o++] = in[i];
            i += rep;
            continue;
        }
        // Literal run until the next repeat of 3+ (max 128 bytes).
        size_t lit = 0;
        while (i + lit < n && lit < 128) {
            const size_t j = i + lit;
            if (j + 2 < n && in[j] == in[j + 1] && in[j] == in[j + 2]) break;
            ++lit;
        }
        out[o++] = static_cast<uint8_t>(lit - 1);
        memcpy(out + o, in + i, lit);
        o += lit;
        i += lit;
    }
    return o;
}

} // namespace dax

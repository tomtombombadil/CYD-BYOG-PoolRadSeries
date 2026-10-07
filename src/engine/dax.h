// DAX archives: the container every SSI Gold Box game (DOS) keeps its data in
// - pictures, wall tiles, maps (GEO), event scripts (ECL), monsters, items.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// File layout (all little-endian):
//   u16  index_bytes        size of the index that follows
//   index_bytes / 9 entries:
//     u8   id               block number, unique within the file
//     u32  offset           from the end of the index
//     u16  raw_size         size after decompression
//     u16  comp_size        size stored in the file
//   block data
//
// Blocks are run-length compressed. A control byte c (signed):
//   c >= 0  copy the next c + 1 bytes as they are
//   c <  0  repeat the next byte -c times
// Decompression stops at comp_size input bytes or raw_size output bytes,
// whichever comes first; output past raw_size is never written.
//
// Reading goes through ByteSource so the same code works on an SD card file
// on the board and on a memory buffer in the tests. Nothing here allocates:
// a whole block can be decoded into a caller's buffer, or streamed byte by
// byte (RleReader) so a picture can be drawn without holding the block.
#pragma once

#include <cstddef>
#include <cstdint>

namespace dax {

// Random-access input. read_at returns the number of bytes read (short at EOF).
class ByteSource {
public:
    virtual ~ByteSource() = default;
    virtual size_t read_at(uint32_t pos, uint8_t* buf, size_t n) = 0;
    virtual uint32_t size() const = 0;
};

// A ByteSource over memory (tests, and blocks already loaded).
class MemorySource : public ByteSource {
public:
    MemorySource(const uint8_t* data, uint32_t size) : data_(data), size_(size) {}
    size_t read_at(uint32_t pos, uint8_t* buf, size_t n) override;
    uint32_t size() const override { return size_; }
private:
    const uint8_t* data_;
    uint32_t size_;
};

struct Entry {
    uint8_t  id;
    uint32_t offset;     // from data_start
    uint16_t raw_size;
    uint16_t comp_size;
};

constexpr int kMaxEntries = 256;   // ids are bytes, so a file can't hold more

enum class Status : uint8_t {
    Ok,
    ReadError,      // the source returned fewer bytes than the header promised
    BadIndex,       // index size not a multiple of 9, or larger than the file
    BlockOutside,   // a block's data runs past the end of the file
};

const char* status_text(Status s);

// The parsed index of one file.
struct Index {
    Entry    entries[kMaxEntries];
    int      count = 0;
    uint32_t data_start = 0;   // file offset where block data begins

    const Entry* find(uint8_t id) const;
};

// Reads and checks the index. Every entry is checked to lie inside the file.
Status read_index(ByteSource& src, Index& out);

// Streams one block's decompressed bytes. Buffered: reads the source in
// chunks of kChunk bytes.
class RleReader {
public:
    static constexpr size_t kChunk = 256;

    RleReader(ByteSource& src, const Index& index, const Entry& e);

    // Next decompressed byte, or -1 at the end of the block (raw_size bytes
    // given, or input exhausted, or a read error - see error()).
    int next();
    // Up to n bytes into buf; returns how many.
    size_t read(uint8_t* buf, size_t n);
    // Skip n output bytes; returns how many were skipped.
    size_t skip(size_t n);

    uint32_t produced() const { return produced_; }
    bool error() const { return error_; }

private:
    int in_byte();      // next compressed byte or -1

    ByteSource& src_;
    uint32_t in_pos_;          // absolute file position of the next unread chunk
    uint32_t in_left_;         // compressed bytes not yet fetched into buf_
    uint8_t  buf_[kChunk];
    uint16_t buf_len_ = 0, buf_at_ = 0;
    uint32_t raw_size_;
    uint32_t produced_ = 0;
    int      run_ = 0;          // bytes left in the current run
    int      repeat_ = -1;      // byte being repeated, or -1 for a literal run
    bool     error_ = false;
};

// Decodes a whole block into out (at least e.raw_size bytes). Returns the
// bytes produced; less than raw_size means the block's data was short.
uint32_t load_block(ByteSource& src, const Index& index, const Entry& e, uint8_t* out);

// Compresses with the same scheme (used by the tests to make DAX files, and
// by future tools). Returns the compressed size; out needs at most
// n + n / 128 + 2 bytes.
size_t compress(const uint8_t* in, size_t n, uint8_t* out);

} // namespace dax

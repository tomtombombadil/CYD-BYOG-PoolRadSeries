// The card's files, for the game code (platform/platform.h). Paths are the
// board's: "/GOLDBOX/<game folder>/<file>" (games::kRootDir) - a platform
// maps them onto its own storage. A File is a handle shared by its copies
// (the last copy gone closes it; close() closes it for all), as Arduino's.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

#include "engine/dax.h"

namespace plat {

// What a platform's file is (its own class behind this)
class FileImpl {
public:
    virtual ~FileImpl() = default;
    virtual bool        ok() const = 0;
    virtual size_t      size() = 0;
    virtual bool        seek(uint32_t pos) = 0;
    virtual int         read(uint8_t* buf, size_t n) = 0;
    virtual size_t      write(const uint8_t* buf, size_t n) = 0;
    virtual void        close() = 0;
    virtual bool        is_dir() const = 0;
    virtual const char* name() const = 0;                       // a folder's entry: its own name
    virtual std::shared_ptr<FileImpl> next() = 0;               // a folder: its next entry (nullptr: no more)
};

class File {
public:
    File() = default;
    explicit File(std::shared_ptr<FileImpl> p) : p_(std::move(p)) {}
    explicit operator bool() const { return p_ && p_->ok(); }
    size_t size() const { return p_ ? p_->size() : 0; }
    bool seek(uint32_t pos) { return p_ && p_->seek(pos); }
    int read(uint8_t* buf, size_t n) { return p_ ? p_->read(buf, n) : 0; }
    size_t write(const uint8_t* buf, size_t n) { return p_ ? p_->write(buf, n) : 0; }
    void close()
    {
        if (p_) p_->close();
        p_.reset();
    }
    bool isDirectory() const { return p_ && p_->is_dir(); }
    const char* name() const { return p_ ? p_->name() : ""; }
    // A folder's next entry (an invalid File: no more)
    File openNextFile() { return p_ ? File(p_->next()) : File(); }

private:
    std::shared_ptr<FileImpl> p_;
};

// The card ready (mounted; tried again later when it wasn't). False: no card.
bool card_ready();
// mode "r" (a folder too), "w" (made / emptied), "a" (added to)
File open(const char* path, const char* mode = "r");
bool exists(const char* path);
bool remove(const char* path);
bool mkdir(const char* path);

// A DAX source over an open file, with a 512-byte read-ahead buffer
class FileSource : public dax::ByteSource {
public:
    explicit FileSource(File f) : f_(f), size_(static_cast<uint32_t>(f.size())) {}
    size_t read_at(uint32_t pos, uint8_t* buf, size_t n) override
    {
        size_t done = 0;
        while (done < n) {
            const uint32_t p = pos + static_cast<uint32_t>(done);
            if (p >= size_) break;
            if (p < cache_pos_ || p >= cache_pos_ + cache_len_) {
                if (!f_.seek(p)) break;
                const int got = f_.read(cache_, sizeof cache_);
                if (got <= 0) break;
                cache_pos_ = p;
                cache_len_ = static_cast<uint32_t>(got);
            }
            const uint32_t off = p - cache_pos_;
            uint32_t take = cache_len_ - off;
            if (take > n - done) take = static_cast<uint32_t>(n - done);
            memcpy(buf + done, cache_ + off, take);
            done += take;
        }
        return done;
    }
    uint32_t size() const override { return size_; }

private:
    File     f_;
    uint32_t size_;
    uint8_t  cache_[512];
    uint32_t cache_pos_ = 0;
    uint32_t cache_len_ = 0;
};

} // namespace plat

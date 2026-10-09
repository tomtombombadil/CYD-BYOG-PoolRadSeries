#pragma once
#include <FS.h>
#include <cstdint>
#include "engine/dax.h"
#include "engine/games.h"
namespace library {
inline void path_of(const char* d, const char* f, char* out, size_t cap) { snprintf(out, cap, "%s/%s/%s", games::kRootDir, d, f); }   // as on the board
class FileSource : public dax::ByteSource {
public:
    explicit FileSource(fs::File f) : f_(f), size_((uint32_t)f.size()) {}
    size_t read_at(uint32_t pos, uint8_t* buf, size_t n) override {
        if (pos >= size_) return 0;
        if (pos + n > size_) n = size_ - pos;
        f_.seek(pos); return (size_t)f_.read(buf, n);
    }
    uint32_t size() const override { return size_; }
private:
    fs::File f_; uint32_t size_;
};
}

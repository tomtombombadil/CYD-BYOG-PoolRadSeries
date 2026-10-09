#pragma once
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <memory>
namespace fs {
class File {
public:
    File() = default;
    explicit File(FILE* f) : f_(f, [](FILE* p) { fclose(p); }) {}
    explicit operator bool() const { return f_ != nullptr; }
    size_t size() const { long c = ftell(f_.get()); fseek(f_.get(), 0, SEEK_END); long s = ftell(f_.get()); fseek(f_.get(), c, SEEK_SET); return (size_t)s; }
    bool seek(uint32_t p) { return fseek(f_.get(), p, SEEK_SET) == 0; }
    int read(uint8_t* b, size_t n) { return (int)fread(b, 1, n, f_.get()); }
    void close() { f_.reset(); }
private:
    std::shared_ptr<FILE> f_;
};
class FS {
public:
    File open(const char* path, const char*) { FILE* f = fopen(path, "rb"); return f ? File(f) : File(); }
};
}

#pragma once
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <dirent.h>
#include <sys/stat.h>
namespace fs {
class File {
public:
    File() = default;
    explicit File(FILE* f) : f_(f, [](FILE* p) { fclose(p); }) {}
    File(DIR* d, const std::string& path) : d_(d, [](DIR* p) { closedir(p); }), path_(path) {}
    explicit operator bool() const { return f_ != nullptr || d_ != nullptr; }
    size_t size() const { long c = ftell(f_.get()); fseek(f_.get(), 0, SEEK_END); long s = ftell(f_.get()); fseek(f_.get(), c, SEEK_SET); return (size_t)s; }
    bool seek(uint32_t p) { return fseek(f_.get(), p, SEEK_SET) == 0; }
    int read(uint8_t* b, size_t n) { return (int)fread(b, 1, n, f_.get()); }
    size_t write(const uint8_t* b, size_t n) { return fwrite(b, 1, n, f_.get()); }
    void close() { f_.reset(); d_.reset(); }
    bool isDirectory() const { return d_ != nullptr; }
    const char* name() const { return name_.c_str(); }
    File openNextFile()
    {
        if (!d_) return File();
        while (dirent* e = readdir(d_.get())) {
            if (e->d_name[0] == '.') continue;
            std::string p = path_ + "/" + e->d_name;
            struct stat st;
            if (stat(p.c_str(), &st) != 0) continue;
            File r;
            if (S_ISDIR(st.st_mode)) r = File(opendir(p.c_str()), p);
            else r = File(fopen(p.c_str(), "rb"));
            r.name_ = e->d_name;
            return r;
        }
        return File();
    }
private:
    std::shared_ptr<FILE> f_;
    std::shared_ptr<DIR> d_;
    std::string path_, name_;
};
class FS {
public:
    File open(const char* path, const char* mode = "r")
    {
        struct stat st;
        if ((!mode || mode[0] != 'w') && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return File(opendir(path), path);
        FILE* f = fopen(path, mode && mode[0] == 'w' ? "wb" : "rb");
        return f ? File(f) : File();
    }
    bool remove(const char* path) { return ::remove(path) == 0; }
    bool exists(const char* path) { struct stat st; return stat(path, &st) == 0; }
    bool mkdir(const char*) { return true; }
};
}

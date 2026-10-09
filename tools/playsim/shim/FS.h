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
// The card: paths under /GOLDBOX are files under this host folder (as on
// the board, so path mistakes show up here too)
inline std::string& sim_root() { static std::string r; return r; }
inline std::string host_path(const char* p)
{
    if (!sim_root().empty() && strncmp(p, "/GOLDBOX", 8) == 0 && (p[8] == '/' || p[8] == 0)) return sim_root() + (p + 8);
    return p;
}
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
    File open(const char* card_path, const char* mode = "r")
    {
        const std::string hp = host_path(card_path);
        const char* path = hp.c_str();
        struct stat st;
        if ((!mode || mode[0] != 'w') && stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return File(opendir(path), path);
        FILE* f = fopen(path, mode && mode[0] == 'w' ? "wb" : mode && mode[0] == 'a' ? "ab" : "rb");
        return f ? File(f) : File();
    }
    bool remove(const char* path) { return ::remove(host_path(path).c_str()) == 0; }
    bool exists(const char* path) { struct stat st; return stat(host_path(path).c_str(), &st) == 0; }
    bool mkdir(const char* path) { ::mkdir(host_path(path).c_str(), 0755); return exists(path); }
};
}

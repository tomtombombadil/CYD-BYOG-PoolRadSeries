// The platform layer on a PC (tools/host/host.h)
#include "host.h"

#include "platform/file.h"
#include "platform/platform.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace pc {

std::string& card_root()
{
    static std::string r;
    return r;
}

std::string host_path(const char* p)
{
    if (!card_root().empty() && strncmp(p, "/GOLDBOX", 8) == 0 && (p[8] == '/' || p[8] == 0))
        return card_root() + (p + 8);
    return p;
}

namespace {
int g_open = 0;
std::string g_names[64];        // (the open files' card paths, for the "no slot" message)
} // namespace

int open_files() { return g_open; }

} // namespace pc

namespace plat {

uint32_t now_ms() { return g_now; }

void log(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
}

// (the PC's heap isn't the board's: unknown)
size_t free_heap() { return 0; }
size_t largest_block() { return 0; }
// (VGA=1: as a board with PSRAM - the VGA games in 256 colours)
bool vga_screen() { return getenv("VGA") != nullptr; }

namespace {

class HostFile : public FileImpl {
public:
    HostFile(FILE* f, const std::string& card_path) : f_(f)
    {
        ++pc::g_open;
        for (auto& n : pc::g_names)
            if (n.empty()) {
                n = card_path;
                slot_ = &n;
                break;
            }
    }
    HostFile(DIR* d, const std::string& path) : d_(d), path_(path) {}
    ~HostFile() override { close(); }
    bool ok() const override { return f_ || d_; }
    size_t size() override
    {
        if (!f_) return 0;
        const long c = ftell(f_);
        fseek(f_, 0, SEEK_END);
        const long s = ftell(f_);
        fseek(f_, c, SEEK_SET);
        return static_cast<size_t>(s);
    }
    bool seek(uint32_t p) override { return f_ && fseek(f_, p, SEEK_SET) == 0; }
    int read(uint8_t* b, size_t n) override { return f_ ? static_cast<int>(fread(b, 1, n, f_)) : 0; }
    size_t write(const uint8_t* b, size_t n) override { return f_ ? fwrite(b, 1, n, f_) : 0; }
    void close() override
    {
        if (f_) {
            fclose(f_);
            f_ = nullptr;
            --pc::g_open;
            if (slot_) slot_->clear();
            slot_ = nullptr;
        }
        if (d_) {
            closedir(d_);
            d_ = nullptr;
        }
    }
    bool is_dir() const override { return d_ != nullptr; }
    const char* name() const override { return name_.c_str(); }
    std::shared_ptr<FileImpl> next() override
    {
        if (!d_) return nullptr;
        while (dirent* e = readdir(d_)) {
            if (e->d_name[0] == '.') continue;
            const std::string p = path_ + "/" + e->d_name;
            struct stat st;
            if (stat(p.c_str(), &st) != 0) continue;
            std::shared_ptr<HostFile> r;
            if (S_ISDIR(st.st_mode)) r = std::make_shared<HostFile>(opendir(p.c_str()), p);
            else r = std::make_shared<HostFile>(fopen(p.c_str(), "rb"), p);
            r->name_ = e->d_name;
            return r;
        }
        return nullptr;
    }

private:
    FILE*        f_ = nullptr;
    DIR*         d_ = nullptr;
    std::string  path_, name_;
    std::string* slot_ = nullptr;
};

} // namespace

bool card_ready() { return true; }

File open(const char* card_path, const char* mode)
{
    const std::string hp = pc::host_path(card_path);
    const char* path = hp.c_str();
    struct stat st;
    if ((!mode || mode[0] != 'w') && stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return File(std::make_shared<HostFile>(opendir(path), path));
    static const int slots = getenv("SLOTS") ? atoi(getenv("SLOTS")) : 3;    // (the board's: hal/sdcard.cpp)
    if (pc::g_open >= slots) {
        fprintf(stderr, "[fs] no file slot left for %s (%d open:", card_path, pc::g_open);
        for (const auto& n : pc::g_names)
            if (!n.empty()) fprintf(stderr, " %s", n.c_str());
        fprintf(stderr, ")\n");
        if (getenv("FSABORT")) abort();
        return File();
    }
    FILE* f = fopen(path, mode && mode[0] == 'w' ? "wb" : mode && mode[0] == 'a' ? "ab" : "rb");
    if (!f) return File();
    return File(std::make_shared<HostFile>(f, card_path));
}

bool exists(const char* path)
{
    struct stat st;
    return stat(pc::host_path(path).c_str(), &st) == 0;
}
bool remove(const char* path) { return ::remove(pc::host_path(path).c_str()) == 0; }
bool rename(const char* from, const char* to)
{
    // (as the card: the old file goes first - FatFs won't rename onto a name)
    if (!exists(from)) return false;
    if (exists(to) && !remove(to)) return false;
    return ::rename(pc::host_path(from).c_str(), pc::host_path(to).c_str()) == 0;
}
bool mkdir(const char* path)
{
    ::mkdir(pc::host_path(path).c_str(), 0755);
    return exists(path);
}

} // namespace plat

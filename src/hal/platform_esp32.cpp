// The platform layer (platform/platform.h, platform/file.h) on the CYD
// boards: Arduino's clock and serial port, heap_caps, the SD library's
// files (hal/sdcard.*).
#include "platform/platform.h"
#include "platform/file.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdarg>
#include <cstdio>

#include "hal/sdcard.h"

namespace plat {

uint32_t now_ms() { return millis(); }

void log(const char* fmt, ...)
{
    char buf[256];
    va_list a;
    va_start(a, fmt);
    const int n = vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    if (n < 0) return;
    if (static_cast<size_t>(n) < sizeof buf) {
        Serial.print(buf);
        return;
    }
    // (longer than the buffer: once more into one big enough)
    char* big = static_cast<char*>(malloc(static_cast<size_t>(n) + 1));
    if (!big) {
        Serial.print(buf);
        return;
    }
    va_start(a, fmt);
    vsnprintf(big, static_cast<size_t>(n) + 1, fmt, a);
    va_end(a);
    Serial.print(big);
    free(big);
}

size_t free_heap() { return heap_caps_get_free_size(MALLOC_CAP_8BIT); }
size_t largest_block() { return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT); }

namespace {

// A file of the SD library
class SdFile : public FileImpl {
public:
    explicit SdFile(fs::File f) : f_(f) {}
    bool ok() const override { return static_cast<bool>(const_cast<fs::File&>(f_)); }
    size_t size() override { return f_.size(); }
    bool seek(uint32_t pos) override { return f_.seek(pos); }
    int read(uint8_t* buf, size_t n) override { return static_cast<int>(f_.read(buf, n)); }
    size_t write(const uint8_t* buf, size_t n) override { return f_.write(buf, n); }
    void close() override { f_.close(); }
    bool is_dir() const override { return const_cast<fs::File&>(f_).isDirectory(); }
    const char* name() const override { return const_cast<fs::File&>(f_).name(); }
    std::shared_ptr<FileImpl> next() override
    {
        fs::File n = f_.openNextFile();
        if (!n) return nullptr;
        return std::make_shared<SdFile>(n);
    }

private:
    fs::File f_;
};

} // namespace

bool card_ready() { return sd_begin(); }

// (as before the platform layer: the card mounted by card_ready() first)
File open(const char* path, const char* mode)
{
    fs::File f = sd_fs().open(path, mode);
    if (!f) return File();
    return File(std::make_shared<SdFile>(f));
}

bool exists(const char* path) { return sd_fs().exists(path); }
bool remove(const char* path) { return sd_fs().remove(path); }
bool mkdir(const char* path) { return sd_fs().mkdir(path); }

} // namespace plat

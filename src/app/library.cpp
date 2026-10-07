#include "library.h"

#include <Arduino.h>
#include <cstring>
#include <strings.h>

#include "hal/sdcard.h"

namespace library {

namespace {

bool is_dax(const char* name)
{
    const size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".DAX") == 0;
}

// Last path component (Arduino-ESP32 3.x File::name() is already that, but
// older cores gave the full path)
const char* base_name(const char* path)
{
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

int count_dax(fs::File& dir)
{
    int n = 0;
    for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (!f.isDirectory() && is_dax(base_name(f.name()))) ++n;
        f.close();
    }
    return n;
}

} // namespace

ScanResult scan(GameDir* out, int max, int* n)
{
    *n = 0;
    if (!sd_begin()) return ScanResult::NoCard;
    fs::FS& fs = sd_fs();
    fs::File root = fs.open(games::kRootDir);
    if (!root || !root.isDirectory()) return ScanResult::NoRootFolder;

    for (fs::File d = root.openNextFile(); d && *n < max; d = root.openNextFile()) {
        if (d.isDirectory()) {
            const int dax = count_dax(d);
            if (dax > 0) {
                GameDir& g = out[(*n)++];
                strlcpy(g.folder, base_name(d.name()), sizeof g.folder);
                g.game = games::from_folder_name(g.folder);
                g.dax_files = dax;
            }
        }
        d.close();
    }
    root.close();

    // Insertion sort: game number (Unknown last), then folder name
    auto key = [](const GameDir& g) { return g.game == games::Game::Unknown ? 99 : games::number(g.game); };
    for (int i = 1; i < *n; ++i) {
        GameDir t = out[i];
        int j = i - 1;
        while (j >= 0 && (key(out[j]) > key(t) || (key(out[j]) == key(t) && strcasecmp(out[j].folder, t.folder) > 0))) {
            out[j + 1] = out[j];
            --j;
        }
        out[j + 1] = t;
    }
    return ScanResult::Ok;
}

int list_dax(const char* folder, char (*names)[kNameLen], int max)
{
    if (!sd_begin()) return 0;
    char path[96];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, folder);
    fs::File dir = sd_fs().open(path);
    if (!dir || !dir.isDirectory()) return 0;
    int n = 0;
    for (fs::File f = dir.openNextFile(); f && n < max; f = dir.openNextFile()) {
        const char* nm = base_name(f.name());
        if (!f.isDirectory() && is_dax(nm) && strlen(nm) < kNameLen) strlcpy(names[n++], nm, kNameLen);
        f.close();
    }
    dir.close();
    for (int i = 1; i < n; ++i) {
        char t[kNameLen];
        strlcpy(t, names[i], kNameLen);
        int j = i - 1;
        while (j >= 0 && strcasecmp(names[j], t) > 0) {
            strlcpy(names[j + 1], names[j], kNameLen);
            --j;
        }
        strlcpy(names[j + 1], t, kNameLen);
    }
    return n;
}

void path_of(const char* folder, const char* file, char* out, size_t cap)
{
    snprintf(out, cap, "%s/%s/%s", games::kRootDir, folder, file);
}

FileSource::FileSource(fs::File f) : f_(f), size_(static_cast<uint32_t>(f.size())) {}

size_t FileSource::read_at(uint32_t pos, uint8_t* buf, size_t n)
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

} // namespace library

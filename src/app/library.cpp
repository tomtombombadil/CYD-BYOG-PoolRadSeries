#include "library.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <strings.h>

#include "hal/sdcard.h"

namespace library {

namespace {

bool ends_with(const char* name, const char* ext)
{
    const size_t n = strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ext) == 0;
}

bool is_dax(const char* name) { return ends_with(name, ".DAX"); }
bool is_hlib(const char* name) { return ends_with(name, ".TLB") || ends_with(name, ".GLB"); }

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

// How good a file is as the game's icon: 3 goggame-*.ico, 2 goggame-*.dll,
// 1 another .ico, 0 none
int icon_rank(const char* name)
{
    const bool gog = strncasecmp(name, "goggame-", 8) == 0;
    if (ends_with(name, ".ICO")) {
        if (gog) return 3;
        return strcasecmp(name, "Support.ico") == 0 ? 0 : 1;
    }
    if (gog && ends_with(name, ".DLL")) return 2;
    return 0;
}

// The best icon file in /GOLDBOX/<rel_dir> (as rel_dir/<name> into out).
// Opens the folder afresh rather than rewinding a listing already read.
void find_icon(fs::FS& fs, const char* rel_dir, char* out, size_t cap, int& best)
{
    char path[160];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, rel_dir);
    fs::File dir = fs.open(path);
    if (!dir || !dir.isDirectory()) return;
    for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const char* nm = base_name(f.name());
        const int r = f.isDirectory() ? 0 : icon_rank(nm);
        if (r > best) {
            best = r;
            snprintf(out, cap, "%s/%s", rel_dir, nm);
        }
        f.close();
    }
    dir.close();
}

bool contains_nocase(const char* s, const char* word)
{
    const size_t n = strlen(word);
    for (; *s; ++s)
        if (strncasecmp(s, word, n) == 0) return true;
    return false;
}

// The journal PDF in /GOLDBOX/<rel_dir>: a .pdf with "journal" in its name
void find_journal(fs::FS& fs, const char* rel_dir, GameDir& g)
{
    char path[160];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, rel_dir);
    fs::File dir = fs.open(path);
    if (!dir || !dir.isDirectory()) return;
    for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        const char* nm = base_name(f.name());
        if (!g.journal[0] && !f.isDirectory() && ends_with(nm, ".PDF") && contains_nocase(nm, "journal")) {
            snprintf(g.journal, sizeof g.journal, "%s/%s", rel_dir, nm);
            g.journal_size = static_cast<uint32_t>(f.size());
        }
        f.close();
    }
    dir.close();
}

void say(Progress p, void* ctx, bool replace, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
void say(Progress p, void* ctx, bool replace, const char* fmt, ...)
{
    char line[120];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    Serial.printf("[scan] %s\n", line);
    if (p) p(line, replace, ctx);
}

// .TLB / .GLB files in dir and up to `depth` folders below it
int count_hlib(fs::File& dir, int depth)
{
    int n = 0;
    for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) {
            if (depth > 0) n += count_hlib(f, depth - 1);
        } else if (is_hlib(base_name(f.name()))) {
            ++n;
        }
        f.close();
    }
    return n;
}

// The first folder exactly `depth` levels below /GOLDBOX/<rel> (0: rel
// itself) holding .DAX files: its path relative to /GOLDBOX into out, and
// how many. The subfolders' names are read first and the listing closed
// before going down (the card has 4 file slots).
int dax_at_depth(fs::FS& fs, const char* rel, int depth, char* out, size_t cap)
{
    char path[160];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, rel);
    fs::File dir = fs.open(path);
    if (!dir || !dir.isDirectory()) return 0;
    if (depth == 0) {
        const int n = count_dax(dir);
        dir.close();
        if (n > 0) strlcpy(out, rel, cap);
        return n;
    }
    char names[16][40];
    int k = 0;
    for (fs::File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory() && k < 16) strlcpy(names[k++], base_name(f.name()), sizeof names[0]);
        f.close();
    }
    dir.close();
    for (int i = 0; i < k; ++i) {
        char sub[96];
        if (snprintf(sub, sizeof sub, "%s/%s", rel, names[i]) >= static_cast<int>(sizeof sub)) continue;
        const int n = dax_at_depth(fs, sub, depth - 1, out, cap);
        if (n > 0) return n;
    }
    return 0;
}

} // namespace

ScanResult scan(GameDir* out, int max, int* n, Progress progress, void* ctx)
{
    *n = 0;
    say(progress, ctx, false, "Searching for Gold Box games...");
    if (!sd_begin()) {
        say(progress, ctx, false, "No SD card found.");
        return ScanResult::NoCard;
    }
    fs::FS& fs = sd_fs();
    fs::File root = fs.open(games::kRootDir);
    if (!root || !root.isDirectory()) {
        say(progress, ctx, false, "No GOLDBOX folder on the card.");
        return ScanResult::NoRootFolder;
    }

    for (fs::File d = root.openNextFile(); d && *n < max; d = root.openNextFile()) {
        if (d.isDirectory() && strcasecmp(base_name(d.name()), kCacheDir) != 0) {
            say(progress, ctx, false, "Looking in %s...", base_name(d.name()));
            GameDir& g = out[*n];
            strlcpy(g.folder, base_name(d.name()), sizeof g.folder);
            strlcpy(g.data_dir, g.folder, sizeof g.data_dir);
            int dax = count_dax(d);
            // A whole install copied as it is: the game files may sit one
            // folder down (GOG: next to DOSBox's own folder) or two (the
            // Steam / SNEG releases: <folder>/GAME/<SHORT> - Tom, 2026-10-10);
            // the shallowest folder with them wins
            for (int depth = 1; dax == 0 && depth <= 2; ++depth)
                dax = dax_at_depth(fs, g.folder, depth, g.data_dir, sizeof g.data_dir);
            g.format = Format::Dax;
            if (dax == 0) {
                char path[96];
                snprintf(path, sizeof path, "%s/%s", games::kRootDir, g.folder);
                fs::File again = fs.open(path);
                if (again && again.isDirectory()) dax = count_hlib(again, 3);   // (SNEG: GAME/<SHORT>/DISK1-3)
                if (again) again.close();
                g.format = Format::Hlib;
            }
            if (dax > 0) {
                g.game = games::from_folder_name(g.folder);
                if (g.game == games::Game::Unknown && strcmp(g.data_dir, g.folder) != 0)
                    g.game = games::from_folder_name(base_name(g.data_dir));    // (.../GAME/CURSE)
                g.dax_files = dax;
                g.icon[0] = 0;
                g.journal[0] = 0;
                g.journal_size = 0;
                int best = 0;
                find_icon(fs, g.folder, g.icon, sizeof g.icon, best);
                if (strcmp(g.data_dir, g.folder) != 0) find_icon(fs, g.data_dir, g.icon, sizeof g.icon, best);
                // The Gold Box Companion (Steam / SNEG): GBC/Games/<nn. Title>/Game.dat
                g.gbc[0] = 0;
                {
                    char path[160];
                    snprintf(path, sizeof path, "%s/%s/GBC/Games", games::kRootDir, g.folder);
                    fs::File gd = fs.open(path);
                    char sub[48] = {};
                    for (fs::File f = gd ? gd.openNextFile() : fs::File(); f && !sub[0]; f = gd.openNextFile()) {
                        if (f.isDirectory()) strlcpy(sub, base_name(f.name()), sizeof sub);
                        f.close();
                    }
                    if (gd) gd.close();
                    if (sub[0]) {
                        snprintf(path, sizeof path, "%s/%s/GBC/Games/%s/Game.dat", games::kRootDir, g.folder, sub);
                        if (fs.exists(path)) strlcpy(g.gbc, sub, sizeof g.gbc);
                    }
                }
                find_journal(fs, g.folder, g);
                if (strcmp(g.data_dir, g.folder) != 0) find_journal(fs, g.data_dir, g);
                {
                    // The Steam / SNEG releases keep it in Documentation/
                    char doc[96];
                    snprintf(doc, sizeof doc, "%s/Documentation", g.folder);
                    if (!g.journal[0]) find_journal(fs, doc, g);
                }
                if (g.game == games::Game::Unknown)
                    say(progress, ctx, true, "Found game files in %s (a game this engine doesn't know)", g.folder);
                else
                    say(progress, ctx, true, "Found %s", games::title(g.game));
                if (g.format == Format::Hlib) say(progress, ctx, false, "  (newer format - not readable yet)");
                if (g.icon[0]) say(progress, ctx, false, "Found the %s game icon", games::short_title(g.game));
                if (g.journal[0]) say(progress, ctx, false, "Found the %s journal", games::title(g.game));
                if (g.gbc[0]) say(progress, ctx, false, "Found the Gold Box Companion's %s journal", games::short_title(g.game));
                ++*n;
            } else {
                say(progress, ctx, true, "No game files in %s", g.folder);
            }
        }
        d.close();
    }
    root.close();
    say(progress, ctx, false, "Found %d game%s.", *n, *n == 1 ? "" : "s");

    // Insertion sort: list order (Unknown last), then folder name
    auto key = [](const GameDir& g) { return games::list_order(g.game); };
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

void cache_path(const char* file, char* out, size_t cap)
{
    snprintf(out, cap, "%s/%s/%s", games::kRootDir, kCacheDir, file);
}

void cache_path(const GameDir& g, const char* file, char* out, size_t cap)
{
    snprintf(out, cap, "%s/%s/%s/%s", games::kRootDir, kCacheDir, g.folder, file);
}

bool make_cache_dirs(const GameDir& g)
{
    fs::FS& fs = sd_fs();
    char path[160];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, kCacheDir);
    if (!fs.exists(path) && !fs.mkdir(path)) return false;
    snprintf(path, sizeof path, "%s/%s/%s", games::kRootDir, kCacheDir, g.folder);
    return fs.exists(path) || fs.mkdir(path);
}

// LIBRARY.BIN: "GBL" + format version, the struct size, the count, then the
// GameDir records as they are in memory (only this firmware reads them; a
// new layout changes the version or size, and the board scans again)
namespace {
constexpr char kLibMagic[4] = {'G', 'B', 'L', '1'};
}

bool save_library(const GameDir* games, int n)
{
    if (!sd_begin()) return false;
    char path[160];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, kCacheDir);
    if (!sd_fs().exists(path) && !sd_fs().mkdir(path)) return false;
    cache_path("LIBRARY.BIN", path, sizeof path);
    fs::File f = sd_fs().open(path, "w");
    if (!f) return false;
    const uint32_t hdr[2] = {static_cast<uint32_t>(sizeof(GameDir)), static_cast<uint32_t>(n)};
    bool ok = f.write(reinterpret_cast<const uint8_t*>(kLibMagic), 4) == 4 &&
              f.write(reinterpret_cast<const uint8_t*>(hdr), sizeof hdr) == sizeof hdr &&
              f.write(reinterpret_cast<const uint8_t*>(games), sizeof(GameDir) * n) == sizeof(GameDir) * n;
    f.close();
    return ok;
}

bool load_library(GameDir* out, int max, int* n)
{
    *n = 0;
    if (!sd_begin()) return false;
    char path[160];
    cache_path("LIBRARY.BIN", path, sizeof path);
    fs::File f = sd_fs().open(path, "r");
    if (!f) return false;
    char magic[4];
    uint32_t hdr[2];
    bool ok = f.read(reinterpret_cast<uint8_t*>(magic), 4) == 4 && memcmp(magic, kLibMagic, 4) == 0 &&
              f.read(reinterpret_cast<uint8_t*>(hdr), sizeof hdr) == sizeof hdr && hdr[0] == sizeof(GameDir) &&
              hdr[1] <= static_cast<uint32_t>(max) &&
              f.read(reinterpret_cast<uint8_t*>(out), sizeof(GameDir) * hdr[1]) == sizeof(GameDir) * hdr[1];
    f.close();
    if (ok) *n = static_cast<int>(hdr[1]);
    return ok;
}

int list_dax(const char* data_dir, char (*names)[kNameLen], int max)
{
    if (!sd_begin()) return 0;
    char path[128];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, data_dir);
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

bool gbc_path(const GameDir& g, const char* file, char* out, size_t cap)
{
    if (!g.gbc[0]) return false;
    snprintf(out, cap, "%s/%s/GBC/Games/%s/%s", games::kRootDir, g.folder, g.gbc, file);
    return true;
}

void path_of(const char* data_dir, const char* file, char* out, size_t cap)
{
    snprintf(out, cap, "%s/%s/%s", games::kRootDir, data_dir, file);
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

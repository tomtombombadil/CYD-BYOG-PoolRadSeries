#include "look.h"

#include <Arduino.h>
#include <cstdio>
#include <new>

#include "app/library.h"
#include "engine/exepack.h"
#include "engine/font.h"
#include "engine/layout.h"
#include "engine/profile.h"
#include "hal/sdcard.h"

namespace look {

namespace {

struct Data {
    layout::Tiles  tiles;
    layout::Tables tables;
    font::Font     font;
};

Data* d = nullptr;
char  msg[160];
char  src_line[64];

enum Page { kExplore, kText, kOuter, kCombat, kTiles, kPages };

// Opens /GOLDBOX/<dir>/<name>; false if missing
bool open_file(const char* dir, const char* name, fs::File& f)
{
    char path[160];
    library::path_of(dir, name, path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

void text(pic::Canvas& c, const char* s, int col, int row, uint8_t fg = 15)
{
    font::draw_text(c, d->font, s, col, row, fg, -1);
}

} // namespace

bool available(games::Game g) { return profile::program_name(g) != nullptr; }

const char* open(const char* data_dir, games::Game g)
{
    close();
    const char* prog = profile::program_name(g);
    if (!prog) {
        snprintf(msg, sizeof msg, "No screen test for %s yet.", games::title(g));
        return msg;
    }
    if (!sd_begin()) return "No SD card found.";

    d = new (std::nothrow) Data;
    if (!d) return "Not enough memory.";

    // The frame layout, from the game's own program
    fs::File f;
    if (!open_file(data_dir, prog, f)) {
        close();
        snprintf(msg, sizeof msg, "%s is missing from the game folder.", prog);
        return msg;
    }
    {
        library::FileSource fsrc(f);
        exepack::Info info;
        const exepack::Status st = exepack::parse(fsrc, info);
        if (st != exepack::Status::Ok) {
            f.close();
            close();
            snprintf(msg, sizeof msg, "%s: %s.", prog, exepack::status_text(st));
            return msg;
        }
        const profile::Profile* p = profile::find(g, fsrc.size(), info.image_size);
        if (!p) {
            const uint32_t sz = fsrc.size();
            f.close();
            close();
            snprintf(msg, sizeof msg, "This %s (%lu bytes) is a release the engine doesn't know yet.", prog,
                     (unsigned long)sz);
            return msg;
        }
        const uint32_t t0 = millis();
        const layout::Status ls = layout::load_tables(fsrc, info, *p, d->tables);
        Serial.printf("[look] %s frame tables: %s (%lu ms)\n", prog, layout::status_text(ls),
                      (unsigned long)(millis() - t0));
        if (ls != layout::Status::Ok) {
            f.close();
            close();
            snprintf(msg, sizeof msg, "%s: %s.", prog, layout::status_text(ls));
            return msg;
        }
        snprintf(src_line, sizeof src_line, "%s, %s release", prog, p->release);
        f.close();

        // Tiles and font
        if (!open_file(data_dir, p->tiles_file, f)) {
            close();
            snprintf(msg, sizeof msg, "%s is missing from the game folder.", p->tiles_file);
            return msg;
        }
        library::FileSource tsrc(f);
        static dax::Index idx;      // 2.3 KB: static so the stack stays small
        const bool ok = dax::read_index(tsrc, idx) == dax::Status::Ok &&
                        layout::load_tiles(tsrc, idx, p->tiles_block, d->tiles) && font::load(tsrc, idx, d->font);
        f.close();
        if (!ok) {
            close();
            snprintf(msg, sizeof msg, "%s: the frame tiles or font didn't load.", p->tiles_file);
            return msg;
        }
    }
    return nullptr;
}

void close()
{
    delete d;
    d = nullptr;
}

int pages() { return kPages; }

const char* page_name(int page)
{
    switch (page) {
    case kExplore: return "Exploring: 3D view, party, text";
    case kText:    return "Text screen";
    case kOuter:   return "Outer frame and font";
    case kCombat:  return "Combat";
    case kTiles:   return "Frame tiles";
    }
    return "";
}

const char* source() { return src_line; }

void draw(int page, pic::Canvas& c)
{
    c.clear(0);
    if (!d) return;
    const layout::Tables& tb = d->tables;
    const layout::Tiles& t = d->tiles;
    switch (page) {
    case kExplore:
        layout::explore(c, tb, t);
        text(c, "3D VIEW", 5, 8, 7);
        text(c, "PARTY", 18, 2);
        text(c, "TEXT AREA", 2, 18);
        text(c, "MENU LINE", 0, 24, 14);
        break;
    case kText:
        layout::outer(c, tb, t);
        layout::bar(c, tb, t, 16);
        text(c, "MAP / PICTURE AREA", 2, 7, 7);
        text(c, "TEXT AREA", 2, 18);
        text(c, "MENU LINE", 0, 24, 14);
        break;
    case kOuter:
        layout::outer(c, tb, t);
        text(c, "THE QUICK BROWN FOX JUMPS", 2, 3);
        text(c, "OVER THE LAZY DOG.", 2, 4);
        text(c, "0123456789 !?:,'\"-", 2, 6, 11);
        for (int g = 64; g < font::kGlyphs; ++g) {
            const int i = g - 64;
            font::draw_glyph(c, d->font, g, (2 + i % 36) * 8, (9 + i / 36) * 8, 14, -1);
        }
        text(c, "SYMBOLS 64-176 ABOVE", 2, 14, 7);
        text(c, "MENU LINE", 0, 24, 14);
        break;
    case kCombat:
        layout::combat(c, tb, t);
        text(c, "BATTLEFIELD", 6, 10, 7);
        text(c, "COMBAT", 26, 2);
        text(c, "MENU LINE", 0, 24, 14);
        break;
    case kTiles:
        c.clear(1);
        for (int n = 0; n < layout::kTiles; ++n) {
            const int col = 2 + (n % 10) * 3, row = 2 + (n / 10) * 4;
            layout::tile(c, t, n, col, row);
            char num[4];
            snprintf(num, sizeof num, "%d", n);
            text(c, num, col, row + 1, 7);
        }
        text(c, "TILES 0-39 (8X8D1 #202)", 2, 20);
        text(c, "FRAME = 30-39, 3D VIEW = 20-29", 2, 21, 7);
        break;
    }
}

} // namespace look

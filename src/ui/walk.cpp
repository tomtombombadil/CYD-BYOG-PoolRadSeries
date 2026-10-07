#include "walk.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/library.h"
#include "engine/ecl.h"
#include "engine/exepack.h"
#include "engine/font.h"
#include "engine/layout.h"
#include "engine/profile.h"
#include "engine/text.h"
#include "engine/view3d.h"
#include "hal/sdcard.h"

namespace walk {

namespace {

constexpr int kMaxMaps = 64;

struct MapRef {
    uint8_t area, script;
    ecl::MapLoad load;
};

struct Data {
    layout::Tiles  frame_tiles;
    layout::Tables tables;
    font::Font     font;
    view3d::World  world;
    geo::Map       map;
    MapRef         maps[kMaxMaps];
    int            n_maps = 0;
    char           data_dir[96] = {};
    const profile::Profile* prof = nullptr;
    dax::Index idx;          // reused for each file opened
};

Data* d = nullptr;
char  msg[160];

int  cur = 0;                 // map shown
int  px = 0, py = 0, pdir = 0;
bool area_view = false;
const char* note = "";        // a word in the text area ("Blocked.")
text::Menu menu;

bool open_file(const char* name, fs::File& f)
{
    char path[160];
    library::path_of(d->data_dir, name, path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

// Opens <name> and reads its index into d->idx
bool open_dax(const char* name, fs::File& f)
{
    if (!open_file(name, f)) return false;
    library::FileSource src(f);
    if (dax::read_index(src, d->idx) != dax::Status::Ok) {
        f.close();
        return false;
    }
    return true;
}

void area_file(char* out, size_t cap, const char* stem, int area) { snprintf(out, cap, "%s%d.DAX", stem, area); }

// Every map the area scripts load, with their wall sets
void find_maps()
{
    d->n_maps = 0;
    for (int a = d->prof->first_area; a <= d->prof->last_area && d->n_maps < kMaxMaps; ++a) {
        char name[24];
        area_file(name, sizeof name, "ECL", a);
        fs::File f;
        if (!open_dax(name, f)) continue;
        library::FileSource src(f);
        for (int e = 0; e < d->idx.count && d->n_maps < kMaxMaps; ++e) {
            const dax::Entry& en = d->idx.entries[e];
            uint8_t* blk = static_cast<uint8_t*>(malloc(en.raw_size));
            if (!blk) break;
            if (dax::load_block(src, d->idx, en, blk) == en.raw_size) {
                const ecl::MapLoad ml = ecl::find_map_load(blk, en.raw_size, *d->prof->ecl_ops);
                if (ml.has_geo()) d->maps[d->n_maps++] = MapRef{static_cast<uint8_t>(a), en.id, ml};
            }
            free(blk);
        }
        f.close();
    }
    Serial.printf("[walk] %d maps found in the area scripts\n", d->n_maps);
}

// A square near the middle with a way out; faces that way
void pick_start()
{
    int best = 1 << 30;
    px = py = 8;
    pdir = 0;
    for (int y = 0; y < geo::kSize; ++y)
        for (int x = 0; x < geo::kSize; ++x)
            for (int dir = 0; dir < 8; dir += 2) {
                if (geo::passage(d->map, x, y, dir) != 1) continue;
                const int walls = (geo::wall(d->map, x, y, 0) != 0) + (geo::wall(d->map, x, y, 2) != 0) +
                                  (geo::wall(d->map, x, y, 4) != 0) + (geo::wall(d->map, x, y, 6) != 0);
                if (walls == 0) continue;     // open ground: little to see
                const int score = (x - 8) * (x - 8) + (y - 8) * (y - 8);
                if (score < best) {
                    best = score;
                    px = x;
                    py = y;
                    pdir = dir;
                }
            }
}

bool select_map(int i)
{
    if (i < 0 || i >= d->n_maps) return false;
    cur = i;
    const MapRef& r = d->maps[i];
    view3d::World& w = d->world;
    for (auto& ws : w.walls) ws.loaded = false;
    for (auto& ts : w.sets) ts.count = 0;
    d->map.loaded = false;

    char name[24];
    fs::File f;
    area_file(name, sizeof name, "GEO", r.area);
    if (open_dax(name, f)) {
        library::FileSource src(f);
        geo::load(src, d->idx, r.load.geo, d->map);
        f.close();
    }
    // Wall sets: a WALLDEF block of 2 or 3 parts fills the sets after its
    // own too, and the script's numbers for those are then not loaded
    int parts[3] = {0, 0, 0};
    bool filled[3] = {false, false, false};
    area_file(name, sizeof name, "WALLDEF", r.area);
    if (open_dax(name, f)) {
        library::FileSource src(f);
        for (int s = 0; s < 3; ++s) {
            if (r.load.walls[s] == ecl::kNone || filled[s]) continue;
            int n = 0;
            if (view3d::load_walls(src, d->idx, s + 1, r.load.walls[s], w, &n)) {
                parts[s] = n;
                for (int k = 0; k < n; ++k) filled[s + k] = true;
            }
        }
        f.close();
    }
    area_file(name, sizeof name, "8X8D", r.area);
    if (open_dax(name, f)) {
        library::FileSource src(f);
        for (int s = 0; s < 3; ++s)
            for (int k = 0; k < parts[s]; ++k)
                view3d::load_tiles(src, d->idx, static_cast<uint8_t>(view3d::tiles_block(r.load.walls[s], parts[s], k)),
                                   w.sets[s + k]);
        f.close();
    }
    Serial.printf("[walk] map %d: area %d script %d GEO %d walls %d %d %d, geo %s\n", i, r.area, r.script, r.load.geo,
                  r.load.walls[0], r.load.walls[1], r.load.walls[2], d->map.loaded ? "ok" : "missing");
    pick_start();
    note = d->map.loaded ? "" : "This map's GEO block is missing.";
    return true;
}

void put(pic::Canvas& c, const char* s, int col, int row, uint8_t fg = 15)
{
    font::draw_text(c, d->font, s, col, row, fg, -1);
}

bool move(int dir)
{
    const int p = geo::passage(d->map, px, py, dir);
    if (p == 1) {
        px = (px + geo::dx(dir)) & 15;
        py = (py + geo::dy(dir)) & 15;
        note = "";
    } else {
        note = p == 0 ? "Blocked." : "Locked.";
    }
    return true;
}

} // namespace

bool available(games::Game g)
{
    // Needs a known release (for its script opcodes)
    return profile::program_name(g) != nullptr;
}

const char* open(const char* data_dir, games::Game g)
{
    close();
    const char* prog = profile::program_name(g);
    if (!prog) {
        snprintf(msg, sizeof msg, "No walk test for %s yet.", games::title(g));
        return msg;
    }
    if (!sd_begin()) return "No SD card found.";
    d = new (std::nothrow) Data;
    if (!d) return "Not enough memory.";
    strncpy(d->data_dir, data_dir, sizeof d->data_dir - 1);

    fs::File f;
    if (!open_file(prog, f)) {
        close();
        snprintf(msg, sizeof msg, "%s is missing from the game folder.", prog);
        return msg;
    }
    {
        library::FileSource src(f);
        exepack::Info info;
        const profile::Profile* p = nullptr;
        if (exepack::parse(src, info) == exepack::Status::Ok) p = profile::find(g, src.size(), info.image_size);
        if (p && layout::load_tables(src, info, *p, d->tables) != layout::Status::Ok) p = nullptr;
        f.close();
        if (!p || !p->ecl_ops) {
            close();
            snprintf(msg, sizeof msg, "This %s is a release the engine doesn't know yet.", prog);
            return msg;
        }
        d->prof = p;
    }
    // Frame tiles, font and the common wall tiles: 8X8D1
    bool ok = open_dax(d->prof->tiles_file, f);
    if (ok) {
        library::FileSource src(f);
        ok = layout::load_tiles(src, d->idx, d->prof->tiles_block, d->frame_tiles) && font::load(src, d->idx, d->font) &&
             view3d::load_tiles(src, d->idx, d->prof->common_tiles_block, d->world.common);
        f.close();
    }
    if (!ok) {
        snprintf(msg, sizeof msg, "%s: the tiles or font didn't load.", d->prof->tiles_file);
        close();
        return msg;
    }
    d->world.frame = &d->frame_tiles;
    if (open_dax(d->prof->sky_file, f)) {
        library::FileSource src(f);
        view3d::load_horizon(src, d->idx, d->prof->horizon_block, d->world);
        f.close();
    }
    find_maps();
    if (d->n_maps == 0) {
        close();
        return "No 3D maps found in the area scripts (ECL files).";
    }
    area_view = false;
    text::build(menu, "", "Area Next Prev");
    menu.selected = -1;
    select_map(0);
    return nullptr;
}

void close()
{
    delete d;
    d = nullptr;
}

void draw(pic::Canvas& c)
{
    c.clear(0);
    if (!d) return;
    layout::explore(c, d->tables, d->frame_tiles);
    if (d->map.loaded) {
        if (area_view) {
            view3d::draw_area_map(c, d->world, d->map, px, py, pdir);
        } else {
            // Under a roof: the indoor sky (black); outside: light blue
            const uint8_t sky = geo::flags(d->map, px, py) >= 0x80 ? 0 : 11;
            view3d::draw(c, d->world, d->map, px, py, pdir, sky);
        }
    }
    const MapRef& r = d->maps[cur];
    char line[40];
    snprintf(line, sizeof line, "MAP %d OF %d", cur + 1, d->n_maps);
    put(c, line, 18, 2);
    snprintf(line, sizeof line, "AREA %d, GEO %d", r.area, r.load.geo);
    put(c, line, 18, 4, 7);
    snprintf(line, sizeof line, "WALLS %d %d %d", r.load.walls[0] == ecl::kNone ? 0 : r.load.walls[0],
             r.load.walls[1] == ecl::kNone ? 0 : r.load.walls[1], r.load.walls[2] == ecl::kNone ? 0 : r.load.walls[2]);
    put(c, line, 18, 5, 7);
    put(c, "TAP HERE FOR", 18, 9, 8);
    put(c, "THE NEXT MAP", 18, 10, 8);
    snprintf(line, sizeof line, "%d,%d %s", px, py, geo::dir_name(pdir));
    put(c, line, 17, 15, 10);
    if (note[0]) put(c, note, 1, 17, 15);
    text::draw(c, d->font, menu);
}

bool act(Act a, pic::Canvas& c)
{
    if (!d) return false;
    switch (a) {
    case Act::TurnLeft:   pdir = (pdir + 6) & 7; note = ""; break;
    case Act::TurnRight:  pdir = (pdir + 2) & 7; note = ""; break;
    case Act::TurnAround: pdir = (pdir + 4) & 7; note = ""; break;
    case Act::Forward:    move(pdir); break;
    case Act::StepLeft:   move((pdir + 6) & 7); break;      // Tom: the same checks as a step that way
    case Act::StepRight:  move((pdir + 2) & 7); break;
    case Act::Area:       area_view = !area_view; break;
    case Act::NextMap:    select_map((cur + 1) % d->n_maps); break;
    case Act::PrevMap:    select_map((cur + d->n_maps - 1) % d->n_maps); break;
    }
    draw(c);
    return true;
}

bool tap(int x, int y, pic::Canvas& c)
{
    if (!d) return false;
    if (y >= text::kMenuTapTop) {
        const int item = text::hit(menu, x / 8);
        switch (text::key(menu, item)) {
        case 'A': return act(Act::Area, c);
        case 'N': return act(Act::NextMap, c);
        case 'P': return act(Act::PrevMap, c);
        default:  return false;
        }
    }
    if (x >= 17 * 8 && y < 15 * 8) return act(Act::NextMap, c);     // the party panel
    return false;
}

const geo::Map* map() { return d && d->map.loaded ? &d->map : nullptr; }
int pos_x() { return px; }
int pos_y() { return py; }
int dir() { return pdir; }

void describe(char* line1, char* line2, int cap)
{
    if (!d) {
        line1[0] = line2[0] = 0;
        return;
    }
    const MapRef& r = d->maps[cur];
    snprintf(line1, cap, "Map %d of %d", cur + 1, d->n_maps);
    snprintf(line2, cap, "Area %d, GEO %d (script %d)", r.area, r.load.geo, r.script);
}

} // namespace walk

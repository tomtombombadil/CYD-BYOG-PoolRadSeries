#include "walk.h"

#include "platform/platform.h"

#include <cstdlib>
#include <initializer_list>
#include <cstdio>
#include <cstring>
#include <new>

#include "platform/file.h"
#include "engine/ecl.h"
#include "engine/exepack.h"
#include "engine/font.h"
#include "engine/hlib.h"
#include "engine/hlpic.h"
#include "engine/layout.h"
#include "engine/profile.h"
#include "engine/text.h"
#include "engine/view3d.h"
#include "engine/qview.h"

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
    // The Dark Queen of Krynn (HLIB files - variant.hlib): its colours as
    // the view loads them (the 16-colour table)
    bool           hl = false;
    hlpic::Colours col;
};

Data* d = nullptr;
char  msg[160];

int  cur = 0;                 // map shown
int  px = 0, py = 0, pdir = 0;
bool area_view = false;
const char* note = "";        // a word in the text area ("Blocked.")
text::Menu menu;

bool open_file(const char* name, plat::File& f)
{
    char path[160];
    games::path_of(d->data_dir, name, path, sizeof path);
    f = plat::open(path, "r");
    return static_cast<bool>(f);
}

// An HLIB game's file (Dark Queen): in the game folder's DISK1-3 (or itself)
bool open_hl(const char* name, plat::File& f)
{
    char rel[40];
    for (int disk = 1; disk <= 4; ++disk) {
        if (disk <= 3) snprintf(rel, sizeof rel, "DISK%d/%s", disk, name);
        else snprintf(rel, sizeof rel, "%s", name);
        if (open_file(rel, f)) return true;
    }
    return false;
}

// Opens <name> and reads its index into d->idx
bool open_dax(const char* name, plat::File& f)
{
    if (!open_file(name, f)) return false;
    plat::FileSource src(f);
    if (dax::read_index(src, d->idx) != dax::Status::Ok) {
        f.close();
        return false;
    }
    return true;
}

// The font from a file of its own (Secret: 8X8D5.DAX; profile font_file)
bool load_font_file(const char* name, dax::Index& idx, font::Font& out)
{
    plat::File f;
    if (!open_file(name, f)) return false;
    plat::FileSource src(f);
    const bool ok = dax::read_index(src, idx) == dax::Status::Ok && font::load(src, idx, out);
    f.close();
    return ok;
}

void area_file(char* out, size_t cap, const char* stem, int area) { snprintf(out, cap, "%s%d.DAX", stem, area); }

// The family's file holding `block`: Gateway's without a number first, then
// the area's (gateway_facts.md 8); the area's for the others
bool open_family(const char* stem, int area, int block, plat::File& f)
{
    char name[24];
    if (d->prof->variant.common_files) {
        snprintf(name, sizeof name, "%s.DAX", stem);
        if (open_dax(name, f)) {
            if (d->idx.find(static_cast<uint8_t>(block))) return true;
            f.close();
        }
    }
    area_file(name, sizeof name, stem, area);
    return open_dax(name, f);
}

// Dark Queen: every map ECL.GLB's scripts load (LOAD FILES: the GEO.GLB
// id and the look - BACK's id - in walls[0])
void find_hl_maps()
{
    d->n_maps = 0;
    plat::File f;
    if (!open_hl("ECL.GLB", f)) return;
    plat::FileSource src(f);
    hlib::Library lib;
    if (hlib::open(src, 0, src.size(), lib) == hlib::Status::Ok)
        for (int i = lib.first_item(); i < lib.count && d->n_maps < kMaxMaps; ++i) {
            uint32_t pos, len;
            if (!hlib::entry(src, lib, i, pos, len) || !len) continue;
            uint8_t* blk = static_cast<uint8_t*>(malloc(len));
            if (!blk) break;
            hlib::EntryReader r(src, pos, len);
            if (r.read(blk, len) == len) {
                const ecl::MapLoad ml = ecl::find_map_load(blk, len, *d->prof->ecl_ops);
                bool dup = false;
                for (int k = 0; k < d->n_maps; ++k) dup = dup || d->maps[k].load.geo == ml.geo;
                const int id = hlib::id_of(src, lib, i);
                if (ml.has_geo() && !dup) d->maps[d->n_maps++] = MapRef{0, static_cast<uint8_t>(id), ml};
            }
            free(blk);
        }
    f.close();
    plat::log("[walk] ECL.GLB: %d maps\n", d->n_maps);
}

// Every map the area scripts load, with their wall sets
void find_maps()
{
    if (d->hl) {
        find_hl_maps();
        return;
    }
    d->n_maps = 0;
    for (int a = d->prof->first_area; a <= d->prof->last_area && d->n_maps < kMaxMaps; ++a) {
        char name[24];
        area_file(name, sizeof name, "ECL", a);
        plat::File f;
        if (!open_dax(name, f)) continue;
        plat::FileSource src(f);
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
    // One entry per map: several scripts may load the same GEO block with
    // different wall sets (Curse's area 2 map 1: an opening script loads a
    // set the walking scripts don't use). Keep the sets most of its scripts
    // load; on a tie, the later script's.
    int out = 0;
    for (int i = 0; i < d->n_maps; ++i) {
        const MapRef& r = d->maps[i];
        bool dup = false;
        for (int k = 0; k < out; ++k)
            if (d->maps[k].area == r.area && d->maps[k].load.geo == r.load.geo) dup = true;
        if (dup) continue;
        auto same_walls = [](const ecl::MapLoad& a, const ecl::MapLoad& b) {
            return memcmp(a.walls, b.walls, sizeof a.walls) == 0;
        };
        int best = i, best_n = 0;
        for (int j = i; j < d->n_maps; ++j) {
            const MapRef& c = d->maps[j];
            if (c.area != r.area || c.load.geo != r.load.geo) continue;
            int n = 0;
            for (int k = i; k < d->n_maps; ++k)
                if (d->maps[k].area == r.area && d->maps[k].load.geo == r.load.geo && same_walls(d->maps[k].load, c.load)) ++n;
            if (n >= best_n) {
                best_n = n;
                best = j;
            }
        }
        d->maps[out++] = d->maps[best];
    }
    plat::log("[walk] %d scripts load a 3D map: %d maps\n", d->n_maps, out);
    d->n_maps = out;
}

// A square near the middle with a way out; faces that way
void pick_start()
{
    int best = 1 << 30;
    const int cx = d->map.w / 2, cy = d->map.h / 2;
    px = cx;
    py = cy;
    pdir = 0;
    for (int y = 0; y < d->map.h; ++y)
        for (int x = 0; x < d->map.w; ++x)
            for (int dir = 0; dir < 8; dir += 2) {
                if (geo::passage(d->map, x, y, dir) != 1) continue;
                const int walls = (geo::wall(d->map, x, y, 0) != 0) + (geo::wall(d->map, x, y, 2) != 0) +
                                  (geo::wall(d->map, x, y, 4) != 0) + (geo::wall(d->map, x, y, 6) != 0);
                if (walls == 0) continue;     // open ground: little to see
                const int score = (x - cx) * (x - cx) + (y - cy) * (y - cy);
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
    for (auto& ts : w.sets) {
        ts.count = 0;
        view3d::free_extra(ts);
    }
    w.per_set = d->prof->variant.per_set_walls;
    d->map.loaded = false;

    char name[24];
    plat::File f;
    if (d->hl) {
        // Dark Queen: GEO.GLB's map (its wall sets in its header), the look
        // its backdrop
        if (open_hl("GEO.GLB", f)) {
            plat::FileSource src(f);
            uint32_t pos, len;
            if (hlib::find_entry(src, r.load.geo, pos, len)) geo::load_hlib(src, pos, len, d->map);
            f.close();
        }
        plat::log("[walk] map %d: script %d GEO %d look %d, %dx%d %s\n", i, r.script, r.load.geo, r.load.walls[0],
                  d->map.w, d->map.h, d->map.loaded ? "ok" : "missing");
        pick_start();
        note = d->map.loaded ? "" : "This map is missing from GEO.GLB.";
        return true;
    }
    area_file(name, sizeof name, "GEO", r.area);
    if (open_dax(name, f)) {
        plat::FileSource src(f);
        geo::load(src, d->idx, r.load.geo, d->map);
        f.close();
    }
    if (w.dk) {
        // Darkness: the look - WALLDEF1's three parts and 8X8D1's tiles of
        // the same number (darkness_facts.md 7.3)
        const uint8_t look = r.load.walls[0];
        if (look != ecl::kNone && open_dax("WALLDEF1.DAX", f)) {
            plat::FileSource src(f);
            view3d::load_walls(src, d->idx, 1, look, w, nullptr);
            f.close();
        }
        if (look != ecl::kNone && open_dax("8X8D1.DAX", f)) {
            plat::FileSource src(f);
            view3d::load_vga_tiles(src, d->idx, look, w);
            f.close();
        }
        plat::log("[walk] map %d: script %d GEO %d look %d, geo %s\n", i, r.script, r.load.geo, look,
                  d->map.loaded ? "ok" : "missing");
        pick_start();
        note = d->map.loaded ? "" : "This map's GEO block is missing.";
        return true;
    }
    // Wall sets: a WALLDEF block of 2 or 3 parts fills the sets after its
    // own too, and the script's numbers for those are then not loaded
    int parts[3] = {0, 0, 0};
    bool filled[3] = {false, false, false};
    for (int s = 0; s < 3; ++s) {
        if (r.load.walls[s] == ecl::kNone || filled[s]) continue;
        if (!open_family("WALLDEF", r.area, r.load.walls[s], f)) continue;
        plat::FileSource src(f);
        int n = 0;
        if (view3d::load_walls(src, d->idx, s + 1, r.load.walls[s], w, &n)) {
            parts[s] = n;
            for (int k = 0; k < n; ++k) filled[s + k] = true;
        }
        f.close();
    }
    for (int s = 0; s < 3; ++s)
        for (int k = 0; k < parts[s]; ++k) {
            const int tb = view3d::tiles_block(r.load.walls[s], parts[s], k);
            if (!open_family("8X8D", r.area, tb, f)) continue;
            plat::FileSource src(f);
            view3d::load_tiles(src, d->idx, static_cast<uint8_t>(tb), w.sets[s + k], w.per_set);
            f.close();
        }
    plat::log("[walk] map %d: area %d script %d GEO %d walls %d %d %d, geo %s\n", i, r.area, r.script, r.load.geo,
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
    if (p == 0) {
        note = "Blocked.";
    } else if (!geo::step(d->map, px, py, dir)) {
        note = "The edge of the map.";
    } else {
        // Tom: for testing, locked doors let the party through and say so
        note = p == 1 ? "" : "Locked - passed for testing.";
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
    if (!plat::card_ready()) return "No SD card found.";
    d = new (std::nothrow) Data;
    if (!d) return "Not enough memory.";
    strncpy(d->data_dir, data_dir, sizeof d->data_dir - 1);

    plat::File f;
    if (!open_file(prog, f)) {
        close();
        snprintf(msg, sizeof msg, "%s is missing from the game folder.", prog);
        return msg;
    }
    {
        plat::FileSource src(f);
        exepack::Info info;
        const profile::Profile* p = nullptr;
        if (exepack::parse(src, info) == exepack::Status::Ok) p = profile::find(g, info.file_size, info.image_size);
        if (p && !p->variant.hlib && layout::load_tables(src, info, *p, d->tables) != layout::Status::Ok) p = nullptr;
        f.close();
        if (!p || !p->ecl_ops) {
            close();
            snprintf(msg, sizeof msg, "This %s is a release the engine doesn't know yet.", prog);
            return msg;
        }
        d->prof = p;
    }
    if (d->prof->variant.hlib) {
        // Dark Queen: its screen from FRAME / GEN.TLB, the view from the
        // wall sets and BACK; the engine's own letters for the test's lines
        d->hl = true;
        d->col.reset();
        memset(&d->font, 0, sizeof d->font);
        font::set_letters(d->font, font::kClearLetters, nullptr);
        d->font.loaded = true;
        find_maps();
        if (d->n_maps == 0) {
            close();
            return "No 3D maps found in the scripts (ECL.GLB).";
        }
        area_view = false;
        text::build(menu, "", "Area Next Prev");
        menu.selected = -1;
        select_map(0);
        return nullptr;
    }
    // Frame tiles, font and the common wall tiles: 8X8D1 (Darkness: the
    // area map's tiles and the font in 8X8D0, the frame BORDERS' gold
    // chain, every wall tile in the area's own 8X8D1 block)
    bool ok = open_dax(d->prof->tiles_file, f);
    if (ok && d->prof->variant.vga) {
        plat::FileSource src(f);
        ok = layout::load_vga_tiles(src, d->idx, d->prof->tiles_block, d->frame_tiles, 0, 20, true) &&
             font::load(src, d->idx, d->font);
        f.close();
        if (ok) ok = open_dax("BORDERS.DAX", f);
        if (ok) {
            plat::FileSource bsrc(f);
            ok = layout::load_vga_tiles(bsrc, d->idx, 0, d->frame_tiles, 20, 11, true);
            f.close();
        }
        d->world.dk = true;
    } else if (ok) {
        plat::FileSource src(f);
        ok = layout::load_tiles(src, d->idx, d->prof->tiles_block, d->frame_tiles, d->prof->variant.tile_window,
                                d->prof->variant.dkk ? 25 : 20) &&
             (d->prof->font_file || font::load(src, d->idx, d->font)) &&
             view3d::load_tiles(src, d->idx, d->prof->common_tiles_block, d->world.common);
        f.close();
        if (ok && d->prof->font_file) ok = load_font_file(d->prof->font_file, d->idx, d->font);
    }
    if (!ok) {
        snprintf(msg, sizeof msg, "%s: the tiles or font didn't load.", d->prof->tiles_file);
        close();
        return msg;
    }
    d->world.frame = &d->frame_tiles;
    if (d->prof->sky_file && open_dax(d->prof->sky_file, f)) {
        plat::FileSource src(f);
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
    if (d) {
        geo::release(d->map);
        view3d::back_free(d->world);
        for (view3d::TileSet& ts : d->world.sets) view3d::free_extra(ts);
    }
    delete d;
    d = nullptr;
}

// Dark Queen's screen: its frame, the view or the area map, the test's lines
void draw_hl(pic::Canvas& c)
{
    plat::File fa, fb, fc;
    {
        const bool a = open_hl("FRAME.TLB", fa), b = open_hl("GEN.TLB", fb);
        plat::FileSource sa(fa), sb(fb);
        const int moons[3] = {-1, -1, -1};
        qview::draw_frame(c, a ? &sa : nullptr, b ? &sb : nullptr, pdir, moons, d->col);
        if (a) fa.close();
        if (b) fb.close();
    }
    if (d->map.loaded && area_view) {
        if (open_hl("TOPVIEW.TLB", fa)) {
            plat::FileSource sa(fa);
            qview::Libs libs;
            libs.topview = &sa;
            qview::draw_area_map(c, libs, d->map, px, py, pdir, d->col);
            fa.close();
        }
    } else if (d->map.loaded) {
        // (three files at once: the card's slots)
        const bool a = open_hl("8X8DB.TLB", fa), b = open_hl("8X8DC.TLB", fb), k = open_hl("BACK.TLB", fc);
        plat::FileSource sa(fa), sb(fb), sc(fc);
        qview::Libs libs;
        libs.walls[0] = a ? &sa : nullptr;
        libs.walls[1] = b ? &sb : nullptr;
        libs.back = k ? &sc : nullptr;
        const qview::Look look = qview::look_of(d->map, d->maps[cur].load.walls[0] == ecl::kNone ? 0 : d->maps[cur].load.walls[0]);
        qview::draw(c, libs, d->map, px, py, pdir, look, d->col);
        if (a) fa.close();
        if (b) fb.close();
        if (k) fc.close();
    }
    const MapRef& r = d->maps[cur];
    char line[48];
    snprintf(line, sizeof line, "Map %d of %d: GEO %d", cur + 1, d->n_maps, r.load.geo);
    put(c, line, 18, 2);
    snprintf(line, sizeof line, "%dx%d, look %d", d->map.w, d->map.h, r.load.walls[0] == ecl::kNone ? 0 : r.load.walls[0]);
    put(c, line, 18, 3, 7);
    snprintf(line, sizeof line, "Sets %d %d %d", d->map.head[0], d->map.head[1], d->map.head[2]);
    put(c, line, 18, 4, 7);
    snprintf(line, sizeof line, "Script %d", r.script);
    put(c, line, 18, 5, 7);
    put(c, "Tap here for", 18, 8, 15);
    put(c, "the next map", 18, 9, 15);
    snprintf(line, sizeof line, "%d,%d %s", px, py, geo::dir_name(pdir));
    put(c, line, 27, 14, 15);
    if (note[0]) put(c, note, 1, 18, 15);
    text::draw(c, d->font, menu);
}

void draw(pic::Canvas& c)
{
    c.clear(0);
    if (!d) return;
    if (d->hl) {
        draw_hl(c);
        return;
    }
    layout::explore(c, d->tables, d->frame_tiles);
    if (d->map.loaded) {
        if (area_view) {
            view3d::draw_area_map(c, d->world, d->map, px, py, pdir);
        } else {
            // Under a roof: the indoor sky (black); outside: light blue
            const uint8_t sky = geo::flags(d->map, px, py) >= 0x80 ? 0 : 11;
            if (d->world.dk) {
                // Darkness: indoors BACK1's picture of the look, outdoors
                // SKYGRND's day (darkness_facts.md 7.3)
                const bool in = sky == 0;
                const uint8_t block = in ? d->maps[cur].load.walls[0] : 1;
                plat::File f;
                if (block != ecl::kNone && open_dax(in ? "BACK1.DAX" : "SKYGRND.DAX", f)) {
                    plat::FileSource src(f);
                    view3d::load_back(src, d->idx, block, static_cast<uint8_t>((in ? 0x10 : 0x20) | block), d->world);
                    f.close();
                }
            }
            const auto& v = d->prof->variant;
            if (v.flat_view) view3d::draw(c, d->world, d->map, px, py, pdir, view3d::Bands{sky, v.view_line, v.view_band, v.view_floor});
            else view3d::draw(c, d->world, d->map, px, py, pdir, sky);
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
    // In the AREA view a tap on a square moves the party there (Tom: a way
    // past walls for testing). The window follows the party, so far
    // squares take a few taps.
    const int col = x / 8 - view3d::kCell0, row = y / 8 - view3d::kCell0;
    if (area_view && col >= 0 && col < view3d::kCells && row >= 0 && row < view3d::kCells) {
        auto clamp = [](int v, int side) {
            const int top = side > view3d::kCells ? side - view3d::kCells : 0;
            return v < 0 ? 0 : v > top ? top : v;
        };
        teleport(col + clamp(px - 5, d->map.w), row + clamp(py - 5, d->map.h));
        draw(c);
        return true;
    }
    return false;
}

void teleport(int x, int y)
{
    if (!d || !d->map.on(x, y)) return;
    px = x;
    py = y;
    note = "Moved here. Area = the 3D view.";
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

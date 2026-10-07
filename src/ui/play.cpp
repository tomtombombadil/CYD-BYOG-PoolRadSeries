#include "play.h"

#include <Arduino.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/library.h"
#include "engine/ecl.h"
#include "engine/ecl_vm.h"
#include "engine/exepack.h"
#include "engine/font.h"
#include "engine/layout.h"
#include "engine/profile.h"
#include "engine/text.h"
#include "engine/view3d.h"
#include "hal/sdcard.h"

namespace play {

namespace {

// What to do when a script run ends
enum class Then : uint8_t {
    Idle,          // back to the player
    Move,          // the pre-move script ran: move, then the arrival script
    Arrive,        // the arrival script ran
    NewFirst,      // a new script's first run ended: its step script next
    NewStep,       // ... then its arrival script
    Look,          // the search script ran for Look
};

struct Host;

struct Data {
    layout::Tiles  frame_tiles;
    layout::Tables tables;
    font::Font     font;
    view3d::World  world;
    geo::Map       map;
    ecl::GameState gs;
    dax::Index     idx;
    uint8_t        sky[16] = {};
    char           press_key[text::kMaxString] = {};
    char           data_dir[96] = {};
    const profile::Profile* prof = nullptr;
};

Data* d = nullptr;
ecl::Vm* vm = nullptr;
Host* host = nullptr;
alignas(ecl::Vm) uint8_t vm_mem[sizeof(ecl::Vm)];
char msg[160];
pic::Canvas* cv = nullptr;

bool area_view = false;
bool pic_shown = false;       // a script picture covers the 3D view
Then then = Then::Idle;
bool waiting = false;         // the script waits for the player
int  idle_cycles = 0;         // script cycles in a row with nothing shown (outdoors)
int  dirty0 = 0, dirty1 = 0;

// Text window and menu line
text::Writer w;
text::Menu   menu;
bool         page_prompt = false;   // "press any key" shown
uint32_t     t_last = 0;
bool         t_started = false;
uint32_t     pause_until = 0;
int          list_row0 = 0;         // first row of a list menu's items
bool         list_wait = false;     // the prompt printed; the list is up
char         menu_text[160];

constexpr int kCharMs = 12;

void dirty(int y0, int y1)
{
    if (dirty1 == 0) {
        dirty0 = y0;
        dirty1 = y1;
    } else {
        if (y0 < dirty0) dirty0 = y0;
        if (y1 > dirty1) dirty1 = y1;
    }
}
void dirty_rows(int r0, int r1) { dirty(r0 * 8, (r1 + 1) * 8); }

// ---- files --------------------------------------------------------------------

bool open_file(const char* name, fs::File& f)
{
    char path[160];
    library::path_of(d->data_dir, name, path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

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

void area_file(char* out, size_t cap, const char* stem) { snprintf(out, cap, "%s%d.DAX", stem, d->gs.game_area); }

// ---- drawing ------------------------------------------------------------------

void put(pic::Canvas& c, const char* s, int col, int row, uint8_t fg)
{
    font::draw_text(c, d->font, s, col, row, fg, 0);
}

uint8_t sky_colour()
{
    const ecl::GameState& g = d->gs;
    const bool roof = geo::flags(d->map, g.x, g.y) >= 0x80;
    const uint16_t idx = vm->get(roof ? 0x4BFE : 0x4BFD) & 15;
    return d->sky[idx];
}

void draw_view(pic::Canvas& c)
{
    if (pic_shown) return;
    if (!d->map.loaded) {
        c.fill(24, 24, 88, 88, 0);
    } else if (area_view && (vm->get(0x4BFB) == 0)) {
        view3d::draw_area_map(c, d->world, d->map, d->gs.x, d->gs.y, d->gs.dir);
    } else {
        view3d::draw(c, d->world, d->map, d->gs.x, d->gs.y, d->gs.dir, sky_colour());
    }
    dirty_rows(3, 13);
}

void draw_position(pic::Canvas& c)
{
    const ecl::GameState& g = d->gs;
    c.fill(17 * 8, 15 * 8, 22 * 8, 8, 0);
    char line[40];
    const int hours = vm->get(0x4BC9), tens = vm->get(0x4BC8), ones = vm->get(0x4BC7);
    if (vm->get(0x4BFB) == 0)
        snprintf(line, sizeof line, "%d,%d %s %02d:%d%d%s", g.x, g.y, geo::dir_name(g.dir), hours, tens, ones,
                 (vm->get(0x7ECA) & 1) ? " SEARCH" : "");
    else
        snprintf(line, sizeof line, "%s %02d:%d%d", geo::dir_name(g.dir), hours, tens, ones);
    put(c, line, 17, 15, 10);
    dirty_rows(15, 15);
}

void draw_panel(pic::Canvas& c)
{
    c.fill(17 * 8, 8, 22 * 8, 14 * 8, 0);
    put(c, "NO PARTY YET", 18, 2, 8);
    draw_position(c);
}

void draw_frame(pic::Canvas& c)
{
    c.clear(0);
    layout::explore(c, d->tables, d->frame_tiles);
    dirty(0, pic::kScreenH);
}

void show_menu_line(pic::Canvas& c)
{
    text::draw(c, d->font, menu);
    dirty_rows(text::kMenuRow, text::kMenuRow);
}

void clear_menu_line(pic::Canvas& c)
{
    c.fill(0, text::kMenuRow * 8, pic::kScreenW, 8, 0);
    dirty_rows(text::kMenuRow, text::kMenuRow);
}

// The game's own menu while exploring
void idle_menu(pic::Canvas& c)
{
    if (vm->get(0x4BE6)) {
        text::build(menu, "", "Area Cast View Encamp Search Look");
        menu.selected = 0;
        show_menu_line(c);
    } else {
        clear_menu_line(c);
    }
}

// Script pictures (PICTURE): event pictures in the view, the big ones over
// the top of the screen, a head and body for people
bool draw_block(pic::Canvas& c, const char* stem, int block, int x, int y, bool anim)
{
    char name[24];
    area_file(name, sizeof name, stem);
    fs::File f;
    if (!open_dax(name, f)) return false;
    library::FileSource src(f);
    bool ok = false;
    const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
    if (e) {
        dax::RleReader r(src, d->idx, *e);
        if (anim) {
            static pic::Anim a;
            if (pic::parse_anim(r, e->raw_size, a)) ok = pic::draw_anim(src, d->idx, *e, a, 0, true, c, x, y);
        } else {
            uint8_t hdr[pic::kHeaderSize];
            pic::Header h;
            ok = r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, h) &&
                 pic::draw(r, h, 0, c, x, y);
        }
    }
    f.close();
    return ok;
}

// ---- the script host ------------------------------------------------------------

struct Host : ecl::Host {
    bool load_script(int block, uint8_t* code, uint32_t* len) override
    {
        char name[24];
        area_file(name, sizeof name, "ECL");
        fs::File f;
        if (!open_dax(name, f)) return false;
        library::FileSource src(f);
        bool ok = false;
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
        if (e && e->raw_size >= 2 && static_cast<uint32_t>(e->raw_size) - 2 <= ecl::kCodeSize) {
            uint8_t* tmp = static_cast<uint8_t*>(malloc(e->raw_size));
            if (tmp && dax::load_block(src, d->idx, *e, tmp) == e->raw_size) {
                memcpy(code, tmp + 2, e->raw_size - 2);
                *len = e->raw_size - 2;
                ok = true;
            }
            free(tmp);
        }
        f.close();
        Serial.printf("[play] script %d of %s: %s\n", block, name, ok ? "loaded" : "missing");
        return ok;
    }
    void load_map(int geo_block) override
    {
        char name[24];
        area_file(name, sizeof name, "GEO");
        fs::File f;
        d->map.loaded = false;
        if (open_dax(name, f)) {
            library::FileSource src(f);
            geo::load(src, d->idx, static_cast<uint8_t>(geo_block), d->map);
            f.close();
        }
        Serial.printf("[play] map %s #%d: %s\n", name, geo_block, d->map.loaded ? "ok" : "missing");
    }
    void load_walls(int set, int block) override
    {
        view3d::World& wd = d->world;
        if (set < 1 || set > 3) return;
        if (block < 0) {
            wd.walls[set - 1].loaded = false;
            wd.sets[set - 1].count = 0;
            return;
        }
        char name[24];
        int n = 0;
        fs::File f;
        area_file(name, sizeof name, "WALLDEF");
        if (open_dax(name, f)) {
            library::FileSource src(f);
            if (!view3d::load_walls(src, d->idx, set, static_cast<uint8_t>(block), wd, &n)) n = 0;
            f.close();
        }
        if (n == 0) {
            Serial.printf("[play] wall set %d: block %d not loaded\n", set, block);
            return;
        }
        area_file(name, sizeof name, "8X8D");
        if (open_dax(name, f)) {
            library::FileSource src(f);
            for (int k = 0; k < n; ++k)
                view3d::load_tiles(src, d->idx, static_cast<uint8_t>(view3d::tiles_block(static_cast<uint8_t>(block), n, k)),
                                   wd.sets[set - 1 + k]);
            f.close();
        }
    }
    void picture(int id, int head) override
    {
        pic::Canvas& c = *cv;
        if (id == 0xFF) {
            if (pic_shown) {
                pic_shown = false;
                draw_view(c);
            }
            return;
        }
        if (head == 0xFF && id >= 0x78) {
            // Big picture: the frame with a bar at row 16, the picture inside
            layout::outer(c, d->tables, d->frame_tiles);
            layout::bar(c, d->tables, d->frame_tiles, 16);
            draw_block(c, "BIGPIC", id, 8, 8, false);
            dirty(0, 17 * 8);
        } else if (head == 0xFF) {
            c.fill(24, 24, 88, 88, 0);
            draw_block(c, "PIC", id, 24, 24, true);
            dirty_rows(3, 13);
        } else {
            c.fill(24, 24, 88, 88, 0);
            draw_block(c, "HEAD", head, 24, 24, false);
            draw_block(c, "BODY", id, 24, 64, false);
            dirty_rows(3, 13);
        }
        pic_shown = true;
    }
    void redraw() override
    {
        d->gs.roof = geo::flags(d->map, d->gs.x, d->gs.y);
        draw_view(*cv);
        draw_position(*cv);
    }
    void log(const char* what) override { Serial.printf("[ecl %d:%04X] %s\n", d->gs.script, vm->pc() + 0x8000, what); }
};

// ---- running scripts -------------------------------------------------------------

void handle(ecl::Stop r);

void begin_wait(pic::Canvas& c)
{
    waiting = true;
    switch (vm->wait()) {
    case ecl::Wait::Print:
        if (strcmp(vm->text(), "\n") == 0) {          // PRINT RETURN
            w.col = w.r.x0;
            ++w.row;
            waiting = false;
            handle(vm->resume());
            return;
        }
        text::begin(w, c, vm->text(), text::kTextArea, 10, vm->clear());
        if (vm->clear()) dirty_rows(17, 22);
        t_started = false;
        page_prompt = false;
        break;
    case ecl::Wait::Menu: {
        // "~Yes ~No": each choice's first letter is its key; the rest shows
        // in the normal colour (lower case prints the same in this font)
        size_t o = 0;
        for (int i = 0; i < vm->items() && o + 2 < sizeof menu_text; ++i) {
            const char* it = vm->item(i);
            if (i) menu_text[o++] = ' ';
            for (int k = 0; it[k] && o + 1 < sizeof menu_text; ++k) {
                char ch = it[k];
                if (k == 0) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
                else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
                menu_text[o++] = ch;
            }
        }
        menu_text[o] = 0;
        text::build(menu, vm->prompt(), menu_text);
        if (menu.count != vm->items()) {
            // Words that don't start with a letter: fall back to one target a word
            menu.count = 0;
        }
        menu.selected = 0;
        show_menu_line(c);
        break;
    }
    case ecl::Wait::ListMenu:
        text::begin(w, c, vm->prompt(), text::kTextArea, 10, true);
        dirty_rows(17, 22);
        t_started = false;
        page_prompt = false;
        list_wait = false;
        break;
    case ecl::Wait::Number:
        Serial.println("[play] INPUT NUMBER: no keyboard yet, answered 0");
        waiting = false;
        handle(vm->answer(0));
        return;
    case ecl::Wait::String:
        Serial.println("[play] INPUT STRING: no keyboard yet, answered blank");
        waiting = false;
        handle(vm->answer_string(""));
        return;
    case ecl::Wait::Pause:
        pause_until = millis() + vm->pause_ms();
        break;
    case ecl::Wait::None:
        waiting = false;
        break;
    }
}

void run_entry(int i, Then next)
{
    then = next;
    handle(vm->run(vm->entry(i)));
}

void after_move_redraw()
{
    pic_shown = false;
    d->gs.roof = geo::flags(d->map, d->gs.x, d->gs.y);
    d->gs.wall_ahead = static_cast<uint8_t>(geo::wall(d->map, d->gs.x, d->gs.y, d->gs.dir));
    draw_view(*cv);
    draw_panel(*cv);
}

// The step the player asked for (dir = the way the party goes)
int step_dir = 0;

void do_move()
{
    ecl::GameState& g = d->gs;
    vm->set(0x4BF0, static_cast<uint16_t>(g.x));
    vm->set(0x4BF1, static_cast<uint16_t>(g.y));
    if (vm->get(0x7EC9) < 0xFF && d->map.loaded) {
        const int p = geo::passage(d->map, g.x, g.y, step_dir);
        if (p >= 1) {      // locked doors let the party through for now (Tom: testing)
            g.x = (g.x + geo::dx(step_dir)) & 15;
            g.y = (g.y + geo::dy(step_dir)) & 15;
            // A minute a step, ten when searching (clock slot 1 / 2)
            vm->advance_clock((vm->get(0x7ECA) & 1) ? 2 : 1, 1);
        }
    }
    vm->set(0x7EC9, 0);
    after_move_redraw();
}

void handle(ecl::Stop r)
{
    pic::Canvas& c = *cv;
    if (r == ecl::Stop::Waiting) {
        idle_cycles = 0;
        begin_wait(c);
        return;
    }
    waiting = false;
    w.col = w.r.x0;                      // EXIT puts the text cursor back
    w.row = w.r.y0;
    if (r == ecl::Stop::NewScript) {
        // A new script block: its first run, then its step and arrival runs
        d->gs.moved = false;
        run_entry(4, Then::NewFirst);
        return;
    }
    if (d->gs.moved) {
        d->gs.moved = false;
        after_move_redraw();
    }
    switch (then) {
    case Then::Move:
        do_move();
        run_entry(1, Then::Arrive);
        return;
    case Then::NewFirst:
        vm->set(0x4BF2, d->gs.script);
        after_move_redraw();
        run_entry(0, Then::NewStep);
        return;
    case Then::NewStep:
        run_entry(1, Then::Arrive);
        return;
    case Then::Look:
        vm->set(0x7ECA, vm->get(0x7ECA) & 1);
        break;
    case Then::Arrive:
    case Then::Idle:
        break;
    }
    then = Then::Idle;
    if (!vm->get(0x4BE6)) {
        // Outdoors the scripts run the show: go round again, unless nothing
        // happened (a script with nothing to do would spin forever)
        if (++idle_cycles < 3) {
            run_entry(0, Then::Move);
            return;
        }
        Serial.println("[play] outdoor scripts idle; waiting for the player");
    }
    idle_menu(c);
}

void step(int dir_of_step)
{
    if (waiting || then != Then::Idle) return;
    step_dir = dir_of_step;
    clear_menu_line(*cv);
    text::clear(*cv, text::kTextArea);
    dirty_rows(17, 22);
    // The step script runs before the move, on the square the party is on
    run_entry(0, Then::Move);
}

void finish_print_wait()
{
    // A list menu prints its prompt first, then shows the choices
    if (vm->wait() == ecl::Wait::ListMenu) {
        list_row0 = w.row + (w.col > w.r.x0 ? 1 : 0);
        for (int i = 0; i < vm->items(); ++i) {
            const int row = list_row0 + i;
            if (row > text::kTextArea.y1) break;
            put(*cv, vm->item(i), 1, row, i == 0 ? 0 : 10);
            if (i == 0) {
                // the first choice starts highlighted, as in the games
                cv->fill(8, row * 8, static_cast<int>(strlen(vm->item(i))) * 8, 8, 15);
                font::draw_text(*cv, d->font, vm->item(i), 1, row, 0, -1);
            }
        }
        dirty_rows(17, 22);
        list_wait = true;
        return;
    }
    waiting = false;
    handle(vm->resume());
}

} // namespace

bool available(games::Game g) { return profile::program_name(g) != nullptr; }

const char* open(const char* data_dir, games::Game g, pic::Canvas& c)
{
    close();
    cv = &c;
    const char* prog = profile::program_name(g);
    if (!prog) {
        snprintf(msg, sizeof msg, "No play test for %s yet.", games::title(g));
        return msg;
    }
    if (!sd_begin()) return "No SD card found.";
    d = new (std::nothrow) Data;
    if (!d) return "Not enough memory.";
    strncpy(d->data_dir, data_dir, sizeof d->data_dir - 1);

    fs::File f;
    if (!open_file(prog, f)) {
        snprintf(msg, sizeof msg, "%s is missing from the game folder.", prog);
        close();
        return msg;
    }
    {
        library::FileSource src(f);
        exepack::Info info;
        const profile::Profile* p = nullptr;
        if (exepack::parse(src, info) == exepack::Status::Ok) p = profile::find(g, src.size(), info.image_size);
        if (p && layout::load_tables(src, info, *p, d->tables) != layout::Status::Ok) p = nullptr;
        if (p) {
            exepack::read(src, info, p->data_base + p->sky_colours, d->sky, sizeof d->sky);
            if (!text::read_pascal(src, info, p->press_any_key, d->press_key, sizeof d->press_key))
                strcpy(d->press_key, "Tap to go on");
        }
        f.close();
        if (!p || !p->ecl_ops) {
            snprintf(msg, sizeof msg, "This %s is a release the engine doesn't know yet.", prog);
            close();
            return msg;
        }
        d->prof = p;
    }
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

    // A new game: the start script, as the games begin one
    host = new (std::nothrow) Host;
    if (!host) {
        close();
        return "Not enough memory.";
    }
    vm = new (vm_mem) ecl::Vm(d->gs, *host, *d->prof->ecl_ops);
    ecl::GameState& gs = d->gs;
    gs.game_area = d->prof->start_area;
    gs.script = d->prof->start_script;
    gs.x = 7;
    gs.y = 13;
    gs.dir = 0;
    vm->set(0x7F12, gs.game_area);
    if (!host->load_script(gs.script, gs.code, &gs.code_len) || !vm->init_script()) {
        snprintf(msg, sizeof msg, "The start script (ECL%d block %d) didn't load.", gs.game_area, gs.script);
        close();
        return msg;
    }
    area_view = false;
    pic_shown = false;
    waiting = false;
    idle_cycles = 0;
    w = text::Writer{};
    draw_frame(c);
    draw_view(c);
    draw_panel(c);
    run_entry(4, Then::Idle);
    return nullptr;
}

void close()
{
    if (vm) {
        vm->~Vm();
        vm = nullptr;
    }
    delete host;
    host = nullptr;
    delete d;
    d = nullptr;
}

void draw(pic::Canvas& c)
{
    if (!d) return;
    cv = &c;
    // Only the frame and panel: the text and pictures stay as the scripts
    // left them (the canvas keeps them)
    dirty(0, pic::kScreenH);
}

bool act(Act a, pic::Canvas& c)
{
    if (!d) return false;
    cv = &c;
    if (waiting) {
        // Any key goes on, like the games' "press a key": the rest of the
        // page, the next page, a one-choice menu
        tap(0, 0, c);
        return true;
    }
    if (then != Then::Idle) return false;
    ecl::GameState& g = d->gs;
    switch (a) {
    case Act::TurnLeft:   g.dir = (g.dir + 6) & 7; break;
    case Act::TurnRight:  g.dir = (g.dir + 2) & 7; break;
    case Act::TurnAround: g.dir = (g.dir + 4) & 7; break;
    case Act::Forward:    step(g.dir); return true;
    case Act::StepLeft:   step((g.dir + 6) & 7); return true;     // Tom: the same checks as a step that way
    case Act::StepRight:  step((g.dir + 2) & 7); return true;
    case Act::Area:
        if (vm->get(0x4BFB) == 0) area_view = !area_view;
        pic_shown = false;
        break;
    case Act::Look: {
        // Search this square (+10 minutes); the search script runs
        vm->set(0x7ECA, static_cast<uint16_t>((vm->get(0x7ECA) & 1) | 2));
        vm->advance_clock(2, 1);
        clear_menu_line(c);
        run_entry(1, Then::Look);
        return true;
    }
    }
    pic_shown = false;
    g.wall_ahead = static_cast<uint8_t>(geo::wall(d->map, g.x, g.y, g.dir));
    draw_view(c);
    draw_position(c);
    return true;
}

void tap(int x, int y, pic::Canvas& c)
{
    if (!d) return;
    cv = &c;
    const int row = y / 8, col = x / 8;
    if (waiting) {
        switch (vm->wait()) {
        case ecl::Wait::Print:
        case ecl::Wait::ListMenu:
            if (page_prompt) {
                clear_menu_line(c);
                text::next_page(w, c);
                dirty_rows(17, 22);
                page_prompt = false;
                t_last = millis();
            } else if (vm->wait() == ecl::Wait::ListMenu && list_wait) {
                const int i = row - list_row0;
                if (row >= text::kTextArea.y0 && row <= text::kTextArea.y1 && i >= 0 && i < vm->items()) {
                    list_wait = false;
                    waiting = false;
                    text::clear(c, text::kTextArea);
                    dirty_rows(17, 22);
                    handle(vm->answer(i));
                }
            } else if (w.state == text::State::Writing) {
                text::step(w, c, d->font, -1);      // a tap prints the rest of the page
                dirty_rows(17, 22);
            }
            break;
        case ecl::Wait::Menu: {
            int item = -1;
            if (vm->items() == 1) item = 0;                         // "press a key": anywhere
            else if (y >= text::kMenuTapTop) item = text::hit(menu, col);
            if (item < 0 || item >= vm->items()) break;
            menu.selected = item;
            show_menu_line(c);
            clear_menu_line(c);
            waiting = false;
            handle(vm->answer(item));
            break;
        }
        default:
            break;
        }
        return;
    }
    if (then != Then::Idle) return;
    // The exploring menu: Area Cast View Encamp Search Look
    if (y >= text::kMenuTapTop && vm->get(0x4BE6)) {
        switch (text::key(menu, text::hit(menu, col))) {
        case 'A': act(Act::Area, c); break;
        case 'L': act(Act::Look, c); break;
        case 'S':
            vm->set(0x7ECA, static_cast<uint16_t>(vm->get(0x7ECA) ^ 1));
            draw_position(c);
            break;
        case 'C':
        case 'V':
        case 'E':
            text::begin(w, c, "Not in the engine yet: it needs a party.", text::kTextArea, 10, true);
            text::step(w, c, d->font, -1);
            dirty_rows(17, 22);
            break;
        default: break;
        }
    }
}

void tick(uint32_t now, pic::Canvas& c)
{
    if (!d || !waiting) return;
    cv = &c;
    const ecl::Wait wt = vm->wait();
    if (wt == ecl::Wait::Pause) {
        if (static_cast<int32_t>(now - pause_until) >= 0) {
            waiting = false;
            handle(vm->resume());
        }
        return;
    }
    if (wt != ecl::Wait::Print && wt != ecl::Wait::ListMenu) return;
    if (list_wait || page_prompt) return;
    if (w.state == text::State::Writing) {
        if (!t_started) {
            t_last = now;
            t_started = true;
        }
        const int n = static_cast<int>((now - t_last) / kCharMs);
        if (n > 0) {
            t_last += static_cast<uint32_t>(n) * kCharMs;
            text::step(w, c, d->font, n);
            dirty_rows(17, 22);
        }
    }
    if (w.state == text::State::PageFull && !page_prompt) {
        clear_menu_line(c);
        put(c, d->press_key, 0, text::kMenuRow, 13);
        page_prompt = true;
    } else if (w.state == text::State::Done) {
        finish_print_wait();
    }
}

void take_dirty(int& y0, int& y1)
{
    y0 = dirty0;
    y1 = dirty1;
    dirty0 = dirty1 = 0;
}

const geo::Map* map() { return d && d->map.loaded ? &d->map : nullptr; }
int pos_x() { return d ? d->gs.x : 0; }
int pos_y() { return d ? d->gs.y : 0; }
int dir() { return d ? d->gs.dir : 0; }

void describe(char* line1, char* line2, int cap)
{
    if (!d) {
        line1[0] = line2[0] = 0;
        return;
    }
    snprintf(line1, cap, "Area %d, script %d", d->gs.game_area, d->gs.script);
    snprintf(line2, cap, "Map %d", vm->get(0x4BC5));
}

} // namespace play

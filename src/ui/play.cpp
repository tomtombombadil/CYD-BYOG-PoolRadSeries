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
#include "engine/journal.h"
#include "engine/layout.h"
#include "engine/party.h"
#include "engine/profile.h"
#include "engine/savegame.h"
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
    Door,          // a locked door stopped the party: "Locked." waits for a key
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

    // The event picture (a PIC animation): its file stays open while shown
    pic::Anim      anim;
    fs::File       anim_file;
    dax::Index     anim_idx;

    // A monster's sprite in the 3D view (SPRITn)
    pic::Anim      sprite;

    // The wilderness map's places (from the program)
    uint8_t        city_x[32] = {}, city_y[32] = {};
    int            cities = 0;

    // The party menu's words (from the program and GAME.OVR)
    static constexpr int kItems = 12;
    char           item[kItems][41] = {};
    bool           item_on[kItems] = {};   // the program's own starting flags
    int            items = 0;
    char           choose[41] = {}, load_which[41] = {}, name_head[8] = {}, ac_hp_head[12] = {};
    char           save_dir[128] = {};      // the game's save folder (data_dir/SAVE)
    savegame::Header save;                 // the saved game loaded
    bool           loaded = false;

    // View Character's words (from the program and GAME.OVR)
    char           cls[18][27] = {}, race[8][10] = {}, alignment[9][17] = {}, sex[2][7] = {}, money[7][11] = {},
                   health[9][13] = {};
    char           v_npc[8] = {}, v_age[8] = {}, v_stat[6][8] = {}, v_level[8] = {}, v_exp[8] = {}, v_status[8] = {},
                   v_ac[8] = {}, v_hp[8] = {}, v_thac0[12] = {}, v_damage[20] = {}, v_enc[24] = {}, v_move[16] = {},
                   v_exit[8] = {};
};

Data* d = nullptr;
party::Party* pt = nullptr;
ecl::Vm* vm = nullptr;
Host* host = nullptr;
alignas(ecl::Vm) uint8_t vm_mem[sizeof(ecl::Vm)];
char msg[160];
pic::Canvas* cv = nullptr;

// Which screen: the party menu (the games' first screen), its "Load Which
// Game" question, or the game itself
enum class Screen : uint8_t { Game, PartyMenu, LoadWhich, View };
Screen screen = Screen::Game;
Screen view_from = Screen::Game;  // where View Character goes back to
int  pm_item[Data::kItems];   // the menu item on each list line
int  pm_lines = 0;
char pm_saves[12];            // save slots found ("AB")
bool exit_wanted = false;     // Exit to DOS: back to the viewer

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

// Event picture animation
int          anim_block = -1;       // the PIC block shown (-1: none)
int          anim_frame = 0;
uint32_t     anim_at = 0;           // when the frame was drawn
int          bigpic = -1;           // the big picture shown
int          last_pic = -1;         // the last event picture loaded
int          head_shown = -1, body_shown = -1;   // a person's head and body shown

// The wilderness map's blinking square
bool         cursor_on = false;
int          cursor_px = 0, cursor_py = 0;
uint8_t      cursor_under[64];
uint32_t     cursor_at = 0;

// Journal entries the text mentions ("record it in journal entry 31"):
// shown once the game waits for a key
char         jtext[200];            // the latest printed text
char         journal_kind = 0;      // mentioned, not yet shown
int          journal_num = 0;
bool         journal_due = false;   // the viewer should show it now

void heard(const char* t)
{
    size_t n = strlen(jtext), add = strlen(t);
    if (add >= sizeof jtext) {
        t += add - (sizeof jtext - 1);
        add = sizeof jtext - 1;
    }
    if (n + add >= sizeof jtext) {          // keep the end
        const size_t drop = n + add - (sizeof jtext - 1);
        memmove(jtext, jtext + drop, n - drop + 1);
        n -= drop;
    }
    memcpy(jtext + n, t, add + 1);
    int num = 0;
    const char k = journal::find_mention(jtext, &num);
    if (k) {
        journal_kind = k;
        journal_num = num;
        jtext[0] = 0;
    }
}

void journal_ready()
{
    if (journal_kind) journal_due = true;
}

// Typing (INPUT NUMBER / STRING) on the menu line
Input        input_mode = Input::None;
char         input_buf[ecl::kMaxInput + 1];
int          input_len = 0;

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

void draw_frame(pic::Canvas& c);
void draw_panel(pic::Canvas& c);
bool bigpic_shown() { return bigpic >= 0; }

void draw_view(pic::Canvas& c)
{
    if (bigpic >= 0) {
        // A big picture covers the screen: outdoors it stays (the wilderness
        // map); in a 3D area the exploring screen comes back
        if (!vm->get(0x4BE6)) return;
        bigpic = -1;
        pic_shown = false;
        draw_frame(c);
        draw_panel(c);
    }
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

// The party list: "Name" and "AC  HP" on row 2, a character a row from
// row 4 - the selected one's name white, the others cyan (red: out of the
// fights, yellow: on the other side); AC and HP right-aligned at columns
// 34 and 38, HP yellow when below the most (as the games print it)
void draw_party(pic::Canvas& c, int col)
{
    put(c, d->name_head, col, 2, 15);
    put(c, d->ac_hp_head, 33, 2, 15);
    int row = 4;
    for (int i = 0; i < pt->count; ++i, ++row) {
        const party::Character& ch = pt->m[i];
        c.fill(col * 8, row * 8, (39 - col) * 8, 8, 0);
        char t[24];
        ch.name(t, sizeof t);
        const uint8_t fg = i == pt->selected ? 15 : !ch.in_combat() ? 12 : ch.enemy() ? 14 : 11;
        put(c, t, col, row, fg);
        const int ac = ch.ac_raw();
        const int aw = ac <= 0x32 ? 1 : ac <= 0x3C ? 2 : ac <= 0x45 ? 1 : 0;
        snprintf(t, sizeof t, "%s%d", ac > 60 ? "-" : "", ac > 60 ? ac - 60 : 60 - ac);
        put(c, t, 0x20 + aw, row, 10);
        const int hp = ch.hp();
        snprintf(t, sizeof t, "%d", hp);
        put(c, t, 0x24 + (hp <= 9 ? 2 : hp <= 99 ? 1 : 0), row, hp < ch.hp_max() ? 14 : 10);
    }
    c.fill(col * 8, row * 8, (39 - col) * 8, 8, 0);
    dirty_rows(2, row);
}

void draw_panel(pic::Canvas& c)
{
    c.fill(17 * 8, 8, 22 * 8, 14 * 8, 0);
    if (pt->count) draw_party(c, 17);
    else put(c, "NO PARTY YET", 18, 2, 8);
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
bool draw_block(pic::Canvas& c, const char* stem, int block, int x, int y)
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
        uint8_t hdr[pic::kHeaderSize];
        pic::Header h;
        ok = r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, h) &&
             pic::draw(r, h, 0, c, x, y);
    }
    f.close();
    return ok;
}

void anim_stop()
{
    anim_block = -1;
    if (d->anim_file) d->anim_file.close();
}

void anim_draw(int frame)
{
    if (anim_block < 0) return;
    library::FileSource src(d->anim_file);
    const dax::Entry* e = d->anim_idx.find(static_cast<uint8_t>(anim_block));
    if (e) pic::draw_anim(src, d->anim_idx, *e, d->anim, frame, true, *cv, 24, 24);
    dirty_rows(3, 13);
}

// An event picture: PIC<area> block id, drawn in the view; it animates
// while the game waits at a menu
bool anim_start(int id)
{
    anim_stop();
    char name[24];
    area_file(name, sizeof name, "PIC");
    if (!open_file(name, d->anim_file)) return false;
    library::FileSource src(d->anim_file);
    const dax::Entry* e = nullptr;
    if (dax::read_index(src, d->anim_idx) == dax::Status::Ok) e = d->anim_idx.find(static_cast<uint8_t>(id));
    if (e) {
        dax::RleReader r(src, d->anim_idx, *e);
        if (pic::parse_anim(r, e->raw_size, d->anim) && d->anim.frames > 0) {
            anim_block = id;
            anim_frame = 0;
            anim_at = millis();
            last_pic = id;
            anim_draw(0);
            return true;
        }
    }
    d->anim_file.close();
    return false;
}

// The wilderness map's square: shown / hidden in turn while waiting
bool cursor_wanted()
{
    const profile::Profile& p = *d->prof;
    return p.wild.bigpic && bigpic == p.wild.bigpic && last_pic != p.wild.hide_pic && d->cities > 0 &&
           vm->get(p.wild.city_var) < d->cities;
}

void cursor_hide()
{
    if (!cursor_on) return;
    for (int r = 0; r < 8; ++r) memcpy(cv->px + (cursor_py + r) * cv->w + cursor_px, cursor_under + r * 8, 8);
    cursor_on = false;
    dirty(cursor_py, cursor_py + 8);
}

void cursor_show()
{
    if (cursor_on) return;
    const int city = vm->get(d->prof->wild.city_var);
    cursor_px = d->city_x[city] * 8;
    cursor_py = d->city_y[city] * 8;
    if (cursor_px < 0 || cursor_px + 8 > cv->w || cursor_py + 8 > cv->h) return;
    for (int r = 0; r < 8; ++r) memcpy(cursor_under + r * 8, cv->px + (cursor_py + r) * cv->w + cursor_px, 8);
    cv->fill(cursor_px, cursor_py, 8, 8, 15);
    cursor_on = true;
    dirty(cursor_py, cursor_py + 8);
}

// ---- the party menu -----------------------------------------------------------
//
// The games' first screen: the party list, then the menu (Create New
// Character ... BEGIN Adventuring, Exit to DOS) a line each from row 12,
// its first letter white; "Choose a function" on the menu line. Which
// entries are on follows the party, as in the games. A tap on a line
// picks it; a tap on a character selects them.

char pm_key(int i) { return static_cast<char>(toupper(static_cast<unsigned char>(d->item[i][0]))); }

void pm_flags(bool on[Data::kItems])
{
    for (int i = 0; i < d->items; ++i) {
        on[i] = d->item_on[i];
        switch (pm_key(i)) {
        case 'D': case 'M': case 'V': case 'R': case 'S':
            on[i] = pt->count > 0;
            break;
        case 'T':                               // training: only where the game offers it
            on[i] = pt->count > 0 && (vm->get(0x7EA8) & 0xFF) != 0;
            break;
        case 'H':                               // class changes: with training (rules to come)
            on[i] = false;
            break;
        case 'L':
            on[i] = pt->count == 0;
            break;
        case 'B':
            // The games need a party to begin; until characters can be
            // made, the Play Test lets a new game begin without one
            on[i] = true;
            break;
        default:
            break;
        }
    }
}

void pm_prompt(pic::Canvas& c, const char* note = nullptr)
{
    clear_menu_line(c);
    put(c, note ? note : d->choose, 0, text::kMenuRow, 13);
}

void draw_party_menu(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    if (pt->count) draw_party(c, 1);
    bool on[Data::kItems];
    pm_flags(on);
    pm_lines = 0;
    for (int i = 0; i < d->items; ++i) {
        if (!on[i]) continue;
        const int row = 12 + pm_lines;
        char first[2] = {d->item[i][0], 0};
        put(c, first, 2, row, 15);
        put(c, d->item[i] + 1, 3, row, 10);
        pm_item[pm_lines++] = i;
    }
    pm_prompt(c);
    dirty(0, pic::kScreenH);
}

void find_saves()
{
    int n = 0;
    for (char s = savegame::kFirst; s <= savegame::kLast; ++s) {
        char name[24], path[160];
        savegame::file_name(s, name, sizeof name);
        library::path_of(d->save_dir, name, path, sizeof path);
        fs::File f = sd_fs().open(path, "r");
        if (f) {
            pm_saves[n++] = s;
            f.close();
        }
    }
    pm_saves[n] = 0;
}

bool open_save_file(const char* name, fs::File& f)
{
    char path[160];
    library::path_of(d->save_dir, name, path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

// Loads saved game `slot`: the game's memory, position and script, and
// the party from their files (.SAV, with .SWG items and .FX effects)
bool load_game(char slot)
{
    char name[24];
    savegame::file_name(slot, name, sizeof name);
    fs::File f;
    if (!open_save_file(name, f)) return false;
    bool ok;
    {
        library::FileSource src(f);
        ok = savegame::read(src, d->gs, d->save);
    }
    f.close();
    if (!ok) {
        Serial.printf("[play] %s isn't a saved game\n", name);
        return false;
    }
    pt->clear();
    for (int i = 0; i < d->save.count; ++i) {
        party::Character& ch = pt->m[pt->count];
        char fn[48];
        snprintf(fn, sizeof fn, "%s.SAV", d->save.names[i]);
        if (!open_save_file(fn, f)) {
            Serial.printf("[play] %s missing\n", fn);
            continue;
        }
        {
            library::FileSource src(f);
            ok = party::read_record(src, ch);
        }
        f.close();
        if (!ok) continue;
        snprintf(fn, sizeof fn, "%s.SWG", d->save.names[i]);
        if (open_save_file(fn, f)) {
            library::FileSource src(f);
            party::read_items(src, ch);
            f.close();
        }
        snprintf(fn, sizeof fn, "%s.FX", d->save.names[i]);
        if (open_save_file(fn, f)) {
            library::FileSource src(f);
            party::read_affects(src, ch);
            f.close();
        }
        ++pt->count;
    }
    vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
    vm->set(0x7F12, d->gs.game_area);
    d->loaded = true;
    Serial.printf("[play] loaded %s: area %d, %d,%d, script %d, %d characters\n", name, d->gs.game_area, d->gs.x,
                  d->gs.y, vm->get(0x4BF2), pt->count);
    return true;
}

void begin_adventuring();

// ---- View Character ------------------------------------------------------------
//
// The selected character as the games show them: name (row 1), sex, race
// and age (row 3), alignment, class, the six stats from row 7, coins from
// row 7 (names right-aligned to column 19), levels and experience (row
// 15), AC / HP / THAC0 / damage / encumbrance / movement (rows 17-18),
// health (row 22). Weapon and armour (rows 20-21) need the item names
// (to come); the menu line offers what the engine can do (Exit).

const char* name_of(const char* table, int stride, int count, int i)
{
    return i >= 0 && i < count ? table + i * stride : "";
}

void draw_character(pic::Canvas& c)
{
    const party::Character* ch = pt->sel();
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    dirty(0, pic::kScreenH);
    if (!ch) return;
    char t[48];
    ch->name(t, sizeof t);
    put(c, t, 1, 1, !ch->in_combat() ? 12 : ch->enemy() ? 14 : 11);
    if (ch->npc()) put(c, d->v_npc, static_cast<int>(strlen(t)) + 3, 1, 10);
    int col = 1;
    const char* sx = name_of(d->sex[0], 7, 2, ch->sex());
    put(c, sx, col, 3, 15);
    col += static_cast<int>(strlen(sx)) + 1;
    const char* rc = name_of(d->race[0], 10, 8, ch->race());
    put(c, rc, col, 3, 15);
    col += static_cast<int>(strlen(rc)) + 1;
    snprintf(t, sizeof t, "%s%d", d->v_age, ch->age());
    put(c, t, col, 3, 15);
    put(c, name_of(d->alignment[0], 17, 9, ch->alignment()), 1, 4, 15);
    put(c, name_of(d->cls[0], 27, 18, ch->cls()), 1, 5, 15);
    for (int i = 0; i < 6; ++i) {
        put(c, d->v_stat[i], 1, 7 + i, 10);
        const int v = ch->stat(i);
        snprintf(t, sizeof t, "%d", v);
        put(c, t, v < 10 ? 6 : 5, 7 + i, 10);
        if (i == 0 && v == 18 && ch->str00() > 0) {
            const int e = ch->str00();
            if (e == 100) snprintf(t, sizeof t, "(00)");
            else snprintf(t, sizeof t, "(%02d)", e);
            put(c, t, 7, 7, 10);
        }
    }
    int row = 7;
    for (int coin = 6; coin >= 0; --coin) {
        const int n = ch->money(coin);
        if (n <= 0) continue;
        const char* nm = name_of(d->money[0], 11, 7, coin);
        put(c, nm, 20 - static_cast<int>(strlen(nm)), row, 10);
        snprintf(t, sizeof t, "%d", n);
        put(c, t, 21, row, 10);
        ++row;
    }
    put(c, d->v_level, 1, 15, 15);
    int top = 0;
    for (int k = 0; k < 8; ++k)
        if (ch->level(k) > top) top = ch->level(k);
    size_t o = 0;
    t[0] = 0;
    for (int k = 0; k < 8; ++k) {
        const int lv = ch->level(k), old = ch->old_level(k);
        if (lv > 0 || (old > 0 && old < top)) {
            o += static_cast<size_t>(snprintf(t + o, sizeof t - o, "%s%d", o ? "/" : "", lv + old));
            if (o >= sizeof t) break;
        }
    }
    put(c, t, 7, 15, 15);
    snprintf(t, sizeof t, "%s%lu", d->v_exp, static_cast<unsigned long>(ch->exp()));
    put(c, t, 17, 15, 15);
    // AC, HP, THAC0, damage, encumbrance, movement
    put(c, d->v_ac, 1, 17, 15);
    const int ac = ch->ac_raw();
    snprintf(t, sizeof t, "%s%d", ac > 60 ? "-" : "", ac > 60 ? ac - 60 : 60 - ac);
    put(c, t, 4, 17, 10);
    put(c, d->v_hp, 1, 18, 15);
    snprintf(t, sizeof t, "%d", ch->hp());
    put(c, t, 4, 18, ch->hp() < ch->hp_max() ? 14 : 10);
    put(c, d->v_thac0, 9, 17, 15);
    snprintf(t, sizeof t, "%d", ch->thac0());
    put(c, t, 15, 17, 10);
    put(c, d->v_damage, 8, 18, 15);
    const int b = ch->damage_bonus();
    if (b) snprintf(t, sizeof t, "%dd%d%s%d", ch->dice(), ch->dice_sides(), b > 0 ? "+" : "-", b > 0 ? b : -b);
    else snprintf(t, sizeof t, "%dd%d", ch->dice(), ch->dice_sides());
    put(c, t, 15, 18, 10);
    put(c, d->v_enc, 22, 17, 15);
    snprintf(t, sizeof t, "%d", ch->encumbrance());
    put(c, t, 34, 17, 10);
    int mv = ch->movement();
    if (ch->has_affect(0x2A)) mv *= 2;          // slow
    if (ch->has_affect(0x27)) mv /= 2;          // haste
    put(c, d->v_move, 25, 18, 15);
    snprintf(t, sizeof t, "%d", mv);
    put(c, t, 34, 18, 10);
    put(c, d->v_status, 1, 22, 15);
    put(c, name_of(d->health[0], 13, 9, ch->health()), 8, 22, 10);
    text::build(menu, "", d->v_exit);
    menu.selected = 0;
    show_menu_line(c);
}

void view_character(pic::Canvas& c)
{
    if (!pt->sel()) return;
    view_from = screen;
    screen = Screen::View;
    draw_character(c);
}

void draw_party_menu(pic::Canvas& c);
void back_from_view(pic::Canvas& c);

void pm_choose(int i, pic::Canvas& c)
{
    switch (pm_key(i)) {
    case 'L':
        find_saves();
        if (!pm_saves[0]) break;
        screen = Screen::LoadWhich;
        {
            char keys[24];
            size_t o = 0;
            for (int k = 0; pm_saves[k]; ++k) {
                if (k) keys[o++] = ' ';
                keys[o++] = pm_saves[k];
            }
            keys[o] = 0;
            text::build(menu, d->load_which, keys);
            menu.selected = 0;
            show_menu_line(c);
        }
        return;
    case 'B':
        begin_adventuring();
        return;
    case 'E':
        exit_wanted = true;
        return;
    case 'V':
        view_character(c);
        return;
    default:
        pm_prompt(c, "Not in the engine yet.");
        dirty_rows(text::kMenuRow, text::kMenuRow);
        return;
    }
}

void pm_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (screen == Screen::View) {
        // Exit (the only choice yet): a tap on it, or anywhere
        back_from_view(c);
        return;
    }
    if (screen == Screen::LoadWhich) {
        if (y < text::kMenuTapTop) return;
        const int k = text::hit(menu, col);
        if (k < 0) return;
        menu.selected = k;
        show_menu_line(c);
        load_game(text::key(menu, k));
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return;
    }
    if (row >= 4 && row < 4 + pt->count && col >= 1) {
        pt->selected = row - 4;
        draw_party_menu(c);
        return;
    }
    const int line = row - 12;
    if (line >= 0 && line < pm_lines && y < text::kMenuTapTop) {
        pm_prompt(c);
        pm_choose(pm_item[line], c);
    }
}

// Reads the menu's words from the program (and GAME.OVR) and the save
// folder from the game's configuration file
void load_party_text(dax::ByteSource& exe, const exepack::Info& info)
{
    const auto& pp = d->prof->party;
    d->items = 0;
    if (pp.items && pp.count <= Data::kItems && pp.stride >= 41) {
        uint8_t* buf = static_cast<uint8_t*>(malloc(static_cast<size_t>(pp.count) * pp.stride));
        if (buf && exepack::read(exe, info, pp.items, buf, static_cast<size_t>(pp.count) * pp.stride) ==
                       exepack::Status::Ok) {
            for (int i = 0; i < pp.count; ++i) {
                const uint8_t* e = buf + i * pp.stride;
                size_t n = e[0];
                if (n == 0 || n > 40) continue;
                memcpy(d->item[d->items], e + 1, n);
                d->item[d->items][n] = 0;
                d->item_on[d->items] = e[41] != 0;
                ++d->items;
            }
        }
        free(buf);
    }
    // View Character's name tables: one read of the range holding them
    const auto& pv = d->prof->view;
    const profile::NameTable* tabs[6] = {&pv.cls, &pv.race, &pv.alignment, &pv.sex, &pv.money, &pv.health};
    char* outs[6] = {d->cls[0], d->race[0], d->alignment[0], d->sex[0], d->money[0], d->health[0]};
    const int caps[6][2] = {{27, 18}, {10, 8}, {17, 9}, {7, 2}, {11, 7}, {13, 9}};
    uint32_t lo = 0xFFFFFFFFu, hi = 0;
    for (const profile::NameTable* nt : tabs) {
        if (!nt->at) continue;
        if (nt->at < lo) lo = nt->at;
        if (nt->at + nt->stride * nt->count > hi) hi = nt->at + nt->stride * nt->count;
    }
    if (hi > lo && hi - lo <= 2048) {
        uint8_t* buf = static_cast<uint8_t*>(malloc(hi - lo));
        if (buf && exepack::read(exe, info, lo, buf, hi - lo) == exepack::Status::Ok) {
            for (int t = 0; t < 6; ++t) {
                const profile::NameTable& nt = *tabs[t];
                if (!nt.at || nt.stride > caps[t][0] || nt.count > caps[t][1]) continue;
                for (int i = 0; i < nt.count; ++i) {
                    const uint8_t* e = buf + (nt.at - lo) + i * nt.stride;
                    size_t n = e[0];
                    if (n >= nt.stride) n = nt.stride - 1;
                    char* out = outs[t] + i * caps[t][0];
                    memcpy(out, e + 1, n);
                    out[n] = 0;
                }
            }
        }
        free(buf);
    }
    fs::File f;
    if (open_file(d->prof->overlay, f)) {
        library::FileSource src(f);
        struct { uint32_t at; char* out; size_t cap; } words[] = {
            {pv.npc, d->v_npc, sizeof d->v_npc}, {pv.age, d->v_age, sizeof d->v_age},
            {pv.level, d->v_level, sizeof d->v_level}, {pv.exp, d->v_exp, sizeof d->v_exp},
            {pv.status, d->v_status, sizeof d->v_status}, {pv.ac, d->v_ac, sizeof d->v_ac},
            {pv.hp, d->v_hp, sizeof d->v_hp}, {pv.thac0, d->v_thac0, sizeof d->v_thac0},
            {pv.damage, d->v_damage, sizeof d->v_damage}, {pv.encumbrance, d->v_enc, sizeof d->v_enc},
            {pv.movement, d->v_move, sizeof d->v_move}, {pv.exit, d->v_exit, sizeof d->v_exit},
        };
        for (auto& wd : words)
            if (wd.at) text::read_pascal(src, wd.at, wd.out, wd.cap);
        for (int i = 0; i < 6 && pv.stats; ++i)
            text::read_pascal(src, pv.stats + static_cast<uint32_t>(i) * pv.stats_stride, d->v_stat[i], sizeof d->v_stat[i]);
        text::read_pascal(src, pp.choose, d->choose, sizeof d->choose);
        text::read_pascal(src, pp.load_which, d->load_which, sizeof d->load_which);
        text::read_pascal(src, pp.name, d->name_head, sizeof d->name_head);
        text::read_pascal(src, pp.ac_hp, d->ac_hp_head, sizeof d->ac_hp_head);
        f.close();
    }
    if (!d->choose[0]) strcpy(d->choose, "Choose a function ");
    if (!d->load_which[0]) strcpy(d->load_which, "Load Which Game: ");
    if (!d->name_head[0]) strcpy(d->name_head, "Name");
    if (!d->ac_hp_head[0]) strcpy(d->ac_hp_head, "AC  HP");
    if (!d->v_exit[0]) strcpy(d->v_exit, "Exit");
    char sub[64] = "SAVE";
    if (pp.cfg && open_file(pp.cfg, f)) {
        char cfg[256];
        const int n = f.read(reinterpret_cast<uint8_t*>(cfg), sizeof cfg);
        f.close();
        if (n > 0) {
            char got[64];
            savegame::save_dir(cfg, static_cast<size_t>(n), got, sizeof got);
            if (got[0]) strcpy(sub, got);
        }
    }
    library::path_of(d->data_dir, sub, d->save_dir, sizeof d->save_dir);
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
        cursor_hide();
        if (id == 0xFF) {
            anim_stop();
            bigpic = -1;
            if (pic_shown) {
                pic_shown = false;
                draw_view(c);
            }
            return;
        }
        if (head == 0xFF && id >= 0x78) {
            anim_stop();
            bigpic = id;
            // Big picture: the frame with a bar at row 16, the picture inside
            layout::outer(c, d->tables, d->frame_tiles);
            layout::bar(c, d->tables, d->frame_tiles, 16);
            draw_block(c, "BIGPIC", id, 8, 8);
            dirty(0, 17 * 8);
        } else if (head == 0xFF) {
            c.fill(24, 24, 88, 88, 0);
            anim_start(id);
            dirty_rows(3, 13);
        } else {
            anim_stop();
            c.fill(24, 24, 88, 88, 0);
            draw_block(c, "HEAD", head, 24, 24);
            draw_block(c, "BODY", id, 24, 64);
            head_shown = head;
            body_shown = id;
            dirty_rows(3, 13);
        }
        pic_shown = true;
    }
    void anim_step() override
    {
        if (anim_block < 0) return;
        anim_draw(anim_frame);
        anim_frame = (anim_frame + 1) % d->anim.frames;
        anim_at = millis();
    }
    void redraw() override
    {
        // The 3D view comes back over any picture (CALL 2E10)
        cursor_hide();
        anim_stop();
        pic_shown = false;
        head_shown = body_shown = -1;
        d->gs.roof = geo::flags(d->map, d->gs.x, d->gs.y);
        draw_view(*cv);
        draw_position(*cv);
    }
    void clear_box() override
    {
        // The exploring frame, party panel and position again, the text
        // window cleared; an event picture stays
        pic::Canvas& c = *cv;
        cursor_hide();
        bigpic = -1;
        draw_frame(c);
        draw_panel(c);
        if (anim_block >= 0) {
            anim_draw(0);
        } else if (pic_shown && head_shown >= 0) {
            draw_block(c, "HEAD", head_shown, 24, 24);
            draw_block(c, "BODY", body_shown, 24, 64);
        } else {
            pic_shown = false;
            draw_view(c);
        }
    }
    int wall_type(int x, int y, int dir) override { return d->map.loaded ? geo::wall(d->map, x, y, dir) : 1; }
    void sprite(int id, int distance) override
    {
        // The 3D view, the monsters' sprite on it: frame = how far away,
        // placed by the frame's own position (8-px cells from the view's
        // corner); colour 0 is see-through, 13 is drawn black
        pic::Canvas& c = *cv;
        anim_stop();
        pic_shown = false;
        head_shown = body_shown = -1;
        area_view = false;
        draw_view(c);
        char name[24];
        area_file(name, sizeof name, "SPRIT");
        fs::File f;
        if (!open_dax(name, f)) return;
        library::FileSource src(f);
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(id));
        if (e) {
            dax::RleReader r(src, d->idx, *e);
            if (pic::parse_anim(r, e->raw_size, d->sprite) && distance >= 0 && distance < d->sprite.frames) {
                const pic::Header& h = d->sprite.frame[distance];
                pic::draw_anim(src, d->idx, *e, d->sprite, distance, false, c, (h.x_cell + 3) * 8, (h.y_cell + 3) * 8, 0,
                               13, 0);
            }
        }
        f.close();
        dirty_rows(3, 13);
    }
    void log(const char* what) override { Serial.printf("[ecl %d:%04X] %s\n", d->gs.script, vm->pc() + 0x8000, what); }
};

// ---- running scripts -------------------------------------------------------------

void handle(ecl::Stop r);

void draw_input(pic::Canvas& c)
{
    clear_menu_line(c);
    put(c, input_buf, 0, text::kMenuRow, 10);
    if (input_len < 40) c.fill(input_len * 8, text::kMenuRow * 8, 8, 8, 15);     // the cursor
}

void begin_wait(pic::Canvas& c)
{
    waiting = true;
    switch (vm->wait()) {
    case ecl::Wait::Print:
        heard(vm->text());
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
        journal_ready();
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
        heard(vm->prompt());
        text::begin(w, c, vm->prompt(), text::kTextArea, 10, true);
        dirty_rows(17, 22);
        t_started = false;
        page_prompt = false;
        list_wait = false;
        break;
    case ecl::Wait::Number:
    case ecl::Wait::String:
        // Typed on the menu line, as in the games (the front end shows a
        // keyboard)
        input_mode = vm->wait() == ecl::Wait::Number ? Input::Number : Input::Text;
        input_len = 0;
        input_buf[0] = 0;
        draw_input(c);
        break;
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
    // After every step the games draw the 3D view again: pictures go
    cursor_hide();
    anim_stop();
    pic_shown = false;
    head_shown = body_shown = -1;
    d->gs.roof = geo::flags(d->map, d->gs.x, d->gs.y);
    d->gs.wall_ahead = static_cast<uint8_t>(geo::wall(d->map, d->gs.x, d->gs.y, d->gs.dir));
    draw_view(*cv);
    draw_panel(*cv);
}

// The step the player asked for (dir = the way the party goes)
int step_dir = 0;

// Moves the party (after the step script). False when a locked door
// stopped it (the caller asks for a key first)
bool do_move()
{
    ecl::GameState& g = d->gs;
    vm->set(0x4BF0, static_cast<uint16_t>(g.x));
    vm->set(0x4BF1, static_cast<uint16_t>(g.y));
    bool locked = false;
    if (vm->get(0x7EC9) < 0xFF && d->map.loaded) {
        const int p = geo::passage(d->map, g.x, g.y, step_dir);
        locked = p >= 2;        // locked or barred: the party would need to bash or pick it
        if (p == 1) {
            g.x = (g.x + geo::dx(step_dir)) & 15;
            g.y = (g.y + geo::dy(step_dir)) & 15;
            // A minute a step, ten when searching (clock slot 1 / 2)
            vm->advance_clock((vm->get(0x7ECA) & 1) ? 2 : 1, 1);
        }
    }
    vm->set(0x7EC9, 0);
    if (locked) return false;
    after_move_redraw();
    return true;
}

// "Locked." on the menu line. Bash / Pick / Knock need characters, so for
// now the only answer is Exit
void door_prompt(pic::Canvas& c)
{
    text::build(menu, "Locked. ", "Exit");
    menu.selected = 0;
    show_menu_line(c);
}

void door_done()
{
    clear_menu_line(*cv);
    after_move_redraw();
    run_entry(1, Then::Arrive);
}

void handle(ecl::Stop r)
{
    pic::Canvas& c = *cv;
    cursor_hide();
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
        if (!do_move()) {
            then = Then::Door;
            door_prompt(c);
            return;
        }
        run_entry(1, Then::Arrive);
        return;
    case Then::Door:
        break;
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
    journal_ready();
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

// Back from View Character: the party menu, or the exploring screen drawn
// again (the 3D view, the party, an empty text window)
void back_from_view(pic::Canvas& c)
{
    screen = view_from;
    if (screen != Screen::Game) {
        draw_party_menu(c);
        return;
    }
    anim_stop();
    bigpic = -1;
    pic_shown = false;
    head_shown = body_shown = -1;
    draw_frame(c);
    draw_view(c);
    draw_panel(c);
    idle_menu(c);
}

// BEGIN Adventuring: the saved game's script again (its first run), or a
// new game's start script (as the games do when no script ran yet)
void begin_adventuring()
{
    pic::Canvas& c = *cv;
    ecl::GameState& gs = d->gs;
    const int last = vm->get(0x4BF2) & 0xFF;
    const bool resume = d->loaded && last != 0;
    const bool dungeon = !d->loaded || vm->get(0x4BE6) != 0;
    gs.script = static_cast<uint8_t>(resume ? last : d->prof->start_script);
    screen = Screen::Game;
    if (!host->load_script(gs.script, gs.code, &gs.code_len) || !vm->init_script(resume)) {
        snprintf(msg, sizeof msg, "Script %d of ECL%d.DAX didn't load.", gs.script, gs.game_area);
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        pm_prompt(c, msg);
        return;
    }
    draw_frame(c);
    if (resume) {
        if (dungeon) {
            if (d->save.wall_block[0] > 0) host->load_map(vm->get(0x4BC5) & 0xFF);
            for (int i = 0; i < 3; ++i)
                if (d->save.wall_block[i] > 0) host->load_walls(d->save.wall_set[i], d->save.wall_block[i]);
        } else {
            host->picture(0x79, 0xFF);
        }
    }
    d->gs.roof = geo::flags(d->map, gs.x, gs.y);
    draw_view(c);
    draw_panel(c);
    run_entry(4, Then::Idle);
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
            if (p->wild.bigpic && p->wild.count <= sizeof d->city_x &&
                exepack::read(src, info, p->data_base + p->wild.xs, d->city_x, p->wild.count) == exepack::Status::Ok &&
                exepack::read(src, info, p->data_base + p->wild.ys, d->city_y, p->wild.count) == exepack::Status::Ok)
                d->cities = p->wild.count;
            if (!text::read_pascal(src, info, p->press_any_key, d->press_key, sizeof d->press_key))
                strcpy(d->press_key, "Tap to go on");
            d->prof = p;
            load_party_text(src, info);
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

    // The party menu first, as the games begin
    host = new (std::nothrow) Host;
    pt = new (std::nothrow) party::Party;
    if (!host || !pt) {
        close();
        return "Not enough memory.";
    }
    vm = new (vm_mem) ecl::Vm(d->gs, *host, *d->prof->ecl_ops);
    vm->set_party(pt);
    ecl::GameState& gs = d->gs;
    gs.game_area = d->prof->start_area;
    gs.x = 7;
    gs.y = 13;
    gs.dir = 0;
    vm->set(0x7F12, gs.game_area);
    area_view = false;
    pic_shown = false;
    waiting = false;
    then = Then::Idle;
    anim_block = bigpic = last_pic = -1;
    jtext[0] = 0;
    journal_kind = 0;
    journal_due = false;
    cursor_on = false;
    input_mode = Input::None;
    idle_cycles = 0;
    exit_wanted = false;
    w = text::Writer{};
    screen = Screen::PartyMenu;
    draw_party_menu(c);
    return nullptr;
}

void close()
{
    if (d) anim_stop();
    input_mode = Input::None;
    if (vm) {
        vm->~Vm();
        vm = nullptr;
    }
    delete host;
    host = nullptr;
    delete pt;
    pt = nullptr;
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
    if (screen != Screen::Game) return false;
    if (waiting) {
        // Any key goes on, like the games' "press a key": the rest of the
        // page, the next page, a one-choice menu
        tap(0, 0, c);
        return true;
    }
    if (then == Then::Door) {
        door_done();
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
    if (screen != Screen::Game) {
        pm_tap(x, y, c);
        return;
    }
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
    if (then == Then::Door) {
        door_done();
        return;
    }
    if (then != Then::Idle) return;
    // A tap on a character in the party list selects them
    if (col >= 17 && row >= 4 && row < 4 + pt->count && !bigpic_shown()) {
        pt->selected = row - 4;
        draw_party(c, 17);
        return;
    }
    // The exploring menu: Area Cast View Encamp Search Look
    if (y >= text::kMenuTapTop && vm->get(0x4BE6)) {
        switch (text::key(menu, text::hit(menu, col))) {
        case 'A': act(Act::Area, c); break;
        case 'L': act(Act::Look, c); break;
        case 'S':
            vm->set(0x7ECA, static_cast<uint16_t>(vm->get(0x7ECA) ^ 1));
            draw_position(c);
            break;
        case 'V':
            view_character(c);
            break;
        case 'C':
        case 'E':
            text::begin(w, c, "Not in the engine yet.", text::kTextArea, 10, true);
            text::step(w, c, d->font, -1);
            dirty_rows(17, 22);
            break;
        default: break;
        }
    }
}

void tick(uint32_t now, pic::Canvas& c)
{
    if (!d || !waiting || screen != Screen::Game) return;
    cv = &c;
    const ecl::Wait wt = vm->wait();
    // While the game waits for the player: the event picture animates (at
    // a menu, as in the games) and the wilderness map's square blinks
    const bool asking = wt == ecl::Wait::Menu || (wt == ecl::Wait::ListMenu && list_wait) ||
                        wt == ecl::Wait::Number || wt == ecl::Wait::String;
    if (wt == ecl::Wait::Menu && anim_block >= 0 && d->anim.frames > 1) {
        const uint32_t delay = d->anim.delay[anim_frame] ? d->anim.delay[anim_frame] * 100 : 100;
        if (now - anim_at >= delay) {
            anim_frame = (anim_frame + 1) % d->anim.frames;
            anim_draw(anim_frame);
            anim_at = now;
        }
    }
    if (asking && cursor_wanted() && now - cursor_at >= (cursor_on ? 400u : 300u)) {
        if (cursor_on) cursor_hide();
        else cursor_show();
        cursor_at = now;
    }
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

Input input() { return d ? input_mode : Input::None; }

bool back(pic::Canvas& c)
{
    if (!d) return false;
    cv = &c;
    if (screen == Screen::LoadWhich) {
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return true;
    }
    if (screen == Screen::View) {
        back_from_view(c);
        return true;
    }
    return false;
}

bool exit_requested()
{
    const bool e = exit_wanted;
    exit_wanted = false;
    return e;
}

bool journal_request(char* kind, int* number)
{
    if (!d || !journal_due) return false;
    *kind = journal_kind;
    *number = journal_num;
    journal_due = false;
    journal_kind = 0;
    return true;
}

void input_key(char k, pic::Canvas& c)
{
    if (!d || input_mode == Input::None) return;
    cv = &c;
    if (k == '\n') {
        const bool number = input_mode == Input::Number;
        input_mode = Input::None;
        clear_menu_line(c);
        waiting = false;
        if (number) {
            long v = atol(input_buf);
            if (v > 65535) v = 65535;
            handle(vm->answer(static_cast<int>(v)));
        } else {
            handle(vm->answer_string(input_buf));
        }
        return;
    }
    if (k == '\b') {
        if (input_len > 0) input_buf[--input_len] = 0;
    } else {
        if (k >= 'a' && k <= 'z') k = static_cast<char>(k - 32);
        const bool ok = input_mode == Input::Number ? (k >= '0' && k <= '9' && input_len < 5)
                                                    : (k >= ' ' && k <= 'Z' && input_len < ecl::kMaxInput);
        if (!ok) return;
        input_buf[input_len++] = k;
        input_buf[input_len] = 0;
    }
    draw_input(c);
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
    if (screen != Screen::Game) {
        snprintf(line1, cap, screen == Screen::View ? "View Character" : "Party menu");
        snprintf(line2, cap, "%d character%s", pt->count, pt->count == 1 ? "" : "s");
        return;
    }
    snprintf(line1, cap, "Area %d, script %d", d->gs.game_area, d->gs.script);
    snprintf(line2, cap, "Map %d", vm->get(0x4BC5));
}

} // namespace play

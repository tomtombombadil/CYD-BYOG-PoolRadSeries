#include "play.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/features.h"
#include "app/library.h"
#include "engine/ecl.h"
#include "engine/ecl_vm.h"
#include "engine/exepack.h"
#include "engine/font.h"
#include "engine/journal.h"
#include "engine/classes.h"
#include "engine/combat.h"
#include "engine/create.h"
#include "engine/items.h"
#include "engine/magic.h"
#include "engine/layout.h"
#include "engine/party.h"
#include "engine/printcalls.h"
#include "engine/profile.h"
#include "engine/rules.h"
#include "engine/savegame.h"
#include "engine/sound.h"
#include "engine/text.h"
#include "engine/view3d.h"
#include "hal/audio.h"
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
    Begun,         // BEGIN's first run of the script ended: it's the last script run now
    Camp,          // Encamp: the area's before-camp script ran (entry 2) - now the camp
};

struct Host;

// The Play Test's state. The two biggest parts (the 3D view's tiles,
// ~13 KB, and the script memory, ~13 KB) are blocks of their own: the
// heap is in pieces and no one piece has room for all of it (v0.21.1)
struct Data {
    Data(view3d::World& w, ecl::GameState& g) : world(w), gs(g) {}
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    layout::Tiles  frame_tiles;
    layout::Tables tables;
    font::Font     font;
    view3d::World& world;
    geo::Map       map;
    ecl::GameState& gs;
    dax::Index     idx;
    uint8_t        sky[16] = {};
    char           press_key[text::kMaxString] = {};
    char           script_word[ecl::kScriptWords][32] = {};
    const char*    script_words[ecl::kScriptWords] = {};
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
    char           cache_dir[128] = {};     // _CYD/<folder>: what the engine keeps beside a save
    int16_t        wall_block[3] = {-1, -1, -1}, wall_set[3] = {-1, -1, -1};   // the wall sets loaded (saves)
    // Add / Remove / Drop words (profile party.add_from ... yes_no, in order)
    enum Roster { kAddFrom, kAddSources, kAddPrompt, kAdd, kAdded, kPaladinEvil, kRangers, kNoEvil, kOverwrite,
                  kQmark, kDrop, kForever, kSure, kDump, kOutBack, kFarewell, kRelief, kYesNo,
                  kCantModify, kModify, kKeepExit, kFromSaved, kNewFile, kRosterWords };
    char           roster[kRosterWords][40] = {};
    // Characters that can be added (.GUY files in the save folder)
    static constexpr int kMaxGuys = 48;
    char           guy_file[kMaxGuys][13] = {};
    char           guy_name[kMaxGuys][16] = {};
    bool           guy_added[kMaxGuys] = {};
    // Add from Pool (import_facts.md): a Pool of Radiance record (285
    // bytes) - in the save folder (as the original) or in the player's
    // Pool of Radiance folder on the card (the engine's convenience)
    uint8_t        guy_src[kMaxGuys] = {};      // 0 the save folder's .GUY, 1 a Pool file there, 2 one in Pool's folder
    char           guy_letter[kMaxGuys] = {};   // a Pool saved game's letter (CHRDAT<letter>n.SAV)
    char           guy_from = 'C';              // the list's source: C(urse) / P(ool)
    int            guys = 0;
    char           w_magic[44] = {}, w_level[5][12] = {};    // the magic menu, "1st Level" ...
    char           w_alter[36] = {}, w_select[14] = {}, w_place[14] = {};   // Alter's menus
    char           w_save_which[24] = {}, w_slots[24] = {}, w_saving[24] = {}, w_camp_menu[40] = {},
                   w_camp[8] = {}, w_makes_camp[28] = {};

    // View Character's words (from the program and GAME.OVR)
    char           cls[18][27] = {}, race[8][10] = {}, alignment[9][17] = {}, sex[2][7] = {}, money[7][11] = {},
                   health[9][13] = {};
    // Items: shop and item list words (GAME.OVR)
    rules::ItemFacts facts{};
    char           w_items[12] = {}, w_buy[8] = {}, w_next[8] = {}, w_prev[8] = {}, w_exit[8] = {},
                   w_shop[48] = {}, w_shop_money[48] = {}, w_no_money[24] = {}, w_over[16] = {},
                   w_title[8] = {}, w_heading[16] = {}, w_ready[8] = {}, w_yes[8] = {}, w_no[8] = {},
                   w_cursed[16] = {}, w_wrong[16] = {}, w_already[20] = {}, w_hands[24] = {}, w_s[4] = {},
                   w_weapon[8] = {}, w_armour[8] = {};
    char           iw[profile::kItemWords][44] = {};   // the items menu's words (profile item_words)
    char           v_npc[8] = {}, v_age[8] = {}, v_stat[6][8] = {}, v_level[8] = {}, v_exp[8] = {}, v_status[8] = {},
                   v_ac[8] = {}, v_hp[8] = {}, v_thac0[12] = {}, v_damage[20] = {}, v_enc[24] = {}, v_move[16] = {},
                   v_exit[8] = {};
};

Data* d = nullptr;
party::Party* pt = nullptr;
items::Names* names = nullptr;     // item name words and types
items::Ground* ground = nullptr;   // treasure / a shop's goods (own block: RAM is tight)
const char* rw(int i) { return d->roster[i]; }
ecl::Vm* vm = nullptr;
Host* host = nullptr;
alignas(ecl::Vm) uint8_t vm_mem[sizeof(ecl::Vm)];
char msg[160];
pic::Canvas* cv = nullptr;

// Which screen: the party menu (the games' first screen), its "Load Which
// Game" question, or the game itself
enum class Screen : uint8_t { Game, PartyMenu, LoadWhich, View, Items, Shop, ShopBuy, Camp, SaveWhich, AddFrom,
                               AddList, YesNo, CreatePick, CreateName, TradeWho, Heal, Take, Appraise, Magic,
                               SpellList, Rest, Cast, Effects, Alter, Fight, Loot, Modify, Title, Icon, Won };
Screen screen = Screen::Game;
Screen view_from = Screen::Game;  // where View Character goes back to
Screen save_from = Screen::PartyMenu;   // where Save Which Game goes back to

// A list to pick from, as the games draw them (shop goods, a character's
// items): lines in a cell area, the chosen one highlighted (colour 15
// behind), the menu line "<what> Next Prev Exit"
struct PickList {
    int row0 = 1, row1 = 22, col0 = 1;
    int n = 0, index = 0, top = 0;
    int rows() const { return row1 - row0 + 1; }
} plist;

// A message on the menu line ("Not enough Money."), then the menu again
uint32_t note_until = 0;
bool     note_held = false;      // an error: stays until a tap (Tom, 2026-10-09)
bool     resume_after_note = false;  // the script goes on once that error is tapped away
int last_pic_id = -1, last_pic_head = 0xFF;   // the script's picture (shops come back to it)
int  pm_item[Data::kItems];   // the menu item on each list line
int  pm_lines = 0;
int  pm_sel = 0;              // the party menu line the cursor keys highlight
bool keys_used = false;       // a cursor key was used: highlights where the games had none (the party menu)
char pm_saves[12];            // save slots found ("AB")
bool exit_wanted = false;     // Exit to DOS: back to the viewer

bool area_view = false;
bool pic_shown = false;       // a script picture covers the 3D view
Then then = Then::Idle;
bool waiting = false;         // the script waits for the player
bool in_game_menu = false;          // the party menu a script opened (PROGRAM 0)

// Combat (play_fight.inc)
// ---- Sound: the game's own effects (engine/sound), Tandy or PC speaker
sound::Player* snd = nullptr;
uint8_t snd_mode = 1, snd_volume = 230;     // (Settings: 1 Tandy, 2 PC speaker, 3 Off)

bool snd_fill(uint8_t* buf, int n, void* ctx)
{
    return static_cast<sound::Player*>(ctx)->render(buf, n, kAudioHz, snd_volume);
}

void sfx(int id)
{
    if (!snd || !snd->ready() || (snd_mode != 1 && snd_mode != 2)) return;
    snd->start(id, snd_mode == 2 ? sound::Device::PcSpeaker : sound::Device::Tandy);
    audio_play(snd_fill, snd);
}

void fight_load_monster(int id, int copies, int icon);
bool npc_join(int id);
bool party_dead = false;      // DAMAGE killed everyone: the party menu after the script
// The demo (demo_facts.md): area 1, ECL1 block 0x52's first run, speed 9,
// the party three NPCs the script adds; key waits pass at once; no
// experience or treasure after its fight; PROGRAM 3 ends it - the title
// again, then the version line with a 10 s timeout
bool in_demo = false;
bool game_won = false;                  // PROGRAM 8 ran: training is free (not saved, as in the original)
void won_key(pic::Canvas& c);
bool auto_tap = false;                  // (the demo's own "key presses", not the player's)
void end_demo(pic::Canvas& c);
void fight_clear_monsters();
void fight_start(pic::Canvas& c);
void fight_treasure_only(pic::Canvas& c);
void fight_tick(uint32_t now, pic::Canvas& c);
void fight_tap(int x, int y, pic::Canvas& c);
bool fight_back(pic::Canvas& c);
bool fight_act(Act a, pic::Canvas& c);
void fight_after_view(pic::Canvas& c);
void fight_spell_chosen(int spell, pic::Canvas& c);
void fight_use(int item, int spell, bool scroll, pic::Canvas& c);
bool in_fight();
bool items_direct = false;    // the items screen opened by a fight's Use (Exit: back to the fight)

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
int          list_sel = 0;          // the highlighted one (the cursor keys)
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
bool         journal_due = false;   // the game waits for the player: the next tap shows it
// Every entry mentioned so far, in order (the Menu's Journal list); kept
// for this session until saving games keeps it with the game
constexpr int kMaxSeen = 192;
struct Seen { char kind; uint8_t num; };
Seen         seen[kMaxSeen];
int          n_seen = 0;

void note_seen(char k, int num)
{
    if (num <= 0 || num > 255) return;
    for (int i = 0; i < n_seen; ++i)
        if (seen[i].kind == k && seen[i].num == num) return;
    if (n_seen < kMaxSeen) seen[n_seen++] = Seen{k, static_cast<uint8_t>(num)};
}

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
        note_seen(k, num);
    }
}

void journal_ready()
{
    if (journal_kind) journal_due = true;
}

// Typing (INPUT NUMBER / STRING) on the menu line
Input        input_mode = Input::None;
bool         input_engine = false;   // the engine asks (a new character's name, coins to take), not a script
enum class EngineAsk : uint8_t { Name, Coins, ModName, FileName };
char new_base[12] = {};             // Remove: the file name typed instead (Overwrite -> No)
bool name_for_new = false;          // ... for a new character (Create's save), not Remove
EngineAsk    engine_ask = EngineAsk::Name;
const char*  input_prompt = "";
int          input_max = ecl::kMaxInput;
char         input_buf[ecl::kMaxInput + 1];
int          input_len = 0;

constexpr int kCharMs = 12;

// The tap highlight's place on the canvas (rows y0..y1), and whether
// anything was drawn there since (then it isn't put back)
int  fb_y0 = -1, fb_y1 = -1;
bool fb_touched = false;

void dirty(int y0, int y1)
{
    if (fb_y0 >= 0 && y0 < fb_y1 && y1 > fb_y0) fb_touched = true;
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
    c.fill(col * 8, 2 * 8, (39 - col) * 8, 8, 0);      // (a "no party yet" line goes)
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

bool menu_on = false;            // the menu line shows `menu` (for the tap highlight)

void show_menu_line(pic::Canvas& c)
{
    menu_on = true;
    text::draw(c, d->font, menu);
    dirty_rows(text::kMenuRow, text::kMenuRow);
}

void clear_menu_line(pic::Canvas& c)
{
    menu_on = false;
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

bool animation_on();

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
            if (!animation_on()) anim_stop();       // Alter Pics: Animation off - the first frame only
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
        case 'H':                               // Human Change: with training, a human with no former class
            on[i] = pt->sel() && (vm->get(0x7EA8) & 0xFF) != 0 && create::can_change(*pt->sel());
            break;
        case 'L':
            on[i] = pt->count == 0;
            break;
        case 'B':                               // the games need a party to begin
            on[i] = pt->count > 0;
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
    if (pm_sel >= pm_lines) pm_sel = pm_lines ? pm_lines - 1 : 0;
    if (keys_used && pm_lines) {
        // the cursor keys' line, highlighted as the games' lists are
        const char* it = d->item[pm_item[pm_sel]];
        c.fill(2 * 8, (12 + pm_sel) * 8, static_cast<int>(strlen(it)) * 8, 8, 15);
        font::draw_text(c, d->font, it, 2, 12 + pm_sel, 0, -1);
    }
    pm_prompt(c);
    dirty(0, pic::kScreenH);
}

// A file in the save folder (save_dir is the folder's full path on the card)
void save_path(const char* name, char* out, size_t cap) { snprintf(out, cap, "%s/%s", d->save_dir, name); }

void find_saves()
{
    int n = 0;
    for (char s = savegame::kFirst; s <= savegame::kLast; ++s) {
        char name[24], path[160];
        savegame::file_name(s, name, sizeof name);
        save_path(name, path, sizeof path);
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
    save_path(name, path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

void load_journal_list(char slot);

// Loads saved game `slot`: the game's memory, position and script, and
// the party from their files (.SAV, with .SWG items and .FX effects)
const char* load_problem = nullptr;     // what went wrong with the last load (shown, stays until tapped)

bool load_game(char slot)
{
    load_problem = nullptr;
    char name[24];
    savegame::file_name(slot, name, sizeof name);
    fs::File f;
    if (!open_save_file(name, f)) {
        load_problem = "That saved game couldn't be opened.";
        return false;
    }
    bool ok;
    {
        library::FileSource src(f);
        ok = savegame::read(src, d->gs, d->save);
    }
    f.close();
    // Whatever was going on is over now (a script's party menu, a wait)
    in_game_menu = false;
    waiting = false;
    then = Then::Idle;
    if (!ok) {
        // (read part-way: what was in memory isn't a game any more)
        Serial.printf("[play] %s isn't a saved game\n", name);
        load_problem = "That saved game couldn't be read (damaged or short).";
        d->loaded = false;
        pt->clear();
        return false;
    }
    pt->clear();
    for (int i = 0; i < d->save.count; ++i) {
        party::Character& ch = pt->m[pt->count];
        char fn[48];
        snprintf(fn, sizeof fn, "%s.SAV", d->save.names[i]);
        if (!open_save_file(fn, f)) {
            Serial.printf("[play] %s missing\n", fn);
            load_problem = "A character of that saved game is missing from the save folder.";
            continue;
        }
        {
            library::FileSource src(f);
            ok = party::read_record(src, ch);
        }
        f.close();
        if (!ok) {
            load_problem = "A character of that saved game couldn't be read.";
            continue;
        }
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
    for (int i = 0; i < 3; ++i) {
        d->wall_block[i] = d->save.wall_block[i];
        d->wall_set[i] = d->save.wall_set[i];
    }
    load_journal_list(slot);
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
// weapon and armour (rows 20-21), health (row 22); the menu line offers
// what the engine can do (Items, Exit).

const char* name_of(const char* table, int stride, int count, int i)
{
    return i >= 0 && i < count ? table + i * stride : "";
}

const party::Character* new_char = nullptr;   // the character being made (Create New Character)

void draw_character(pic::Canvas& c)
{
    const party::Character* ch = new_char ? new_char : pt->sel();
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
    // The readied weapon and armour
    const int wi = items::readied_in(ch->items, ch->n_items, *names, items::kSlotWeapon);
    const int ai = items::readied_in(ch->items, ch->n_items, *names, items::kSlotArmour);
    if (wi >= 0) {
        put(c, d->w_weapon, 1, 20, 15);
        names->name(items::Item{ch->items[wi]}, t, sizeof t);
        put(c, t, 8, 20, 10);
    }
    if (ai >= 0) {
        put(c, d->w_armour, 2, 21, 15);
        names->name(items::Item{ch->items[ai]}, t, sizeof t);
        put(c, t, 8, 21, 10);
    }
    put(c, d->v_status, 1, 22, 15);
    put(c, name_of(d->health[0], 13, 9, ch->health()), 8, 22, 10);
    // What the character can do: Items (when they have some), Exit
    char keys[40];
    snprintf(keys, sizeof keys, "%s%s%s", ch->n_items ? d->w_title : "", ch->n_items ? " " : "", d->v_exit);
    text::build(menu, "", keys);
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

// ---- Picking from a list, items, the shop ---------------------------------------

void back_from_view(pic::Canvas& c);
void host_picture(int id, int head);
void handle(ecl::Stop r);
void draw_items(pic::Canvas& c);
void draw_buy(pic::Canvas& c);
void draw_shop(pic::Canvas& c);

// The menu line again after a message
bool fight_moving();
void fight_move_prompt(pic::Canvas& c);

void redraw_menu(pic::Canvas& c)
{
    note_until = 0;
    note_held = false;
    if (screen == Screen::PartyMenu) pm_prompt(c);      // the party menu's own prompt
    else if (fight_moving()) fight_move_prompt(c);      // a fight's Move: its own prompt
    else show_menu_line(c);
    if (resume_after_note) {
        resume_after_note = false;
        waiting = false;
        handle(vm->resume());
    }
}

// A message on the menu line for the game's delay (as the games do)
void note(pic::Canvas& c, const char* t)
{
    clear_menu_line(c);
    put(c, t, 0, text::kMenuRow, 10);
    int speed = vm->get(0x4BFC) & 0xFF;
    if (speed == 0) speed = 4;
    note_until = millis() + static_cast<uint32_t>(speed) * 300;
    if (!note_until) note_until = 1;
}

// An error (or "not in the engine yet") on the menu line: it stays until
// the player taps (Tom, 2026-10-09: errors mustn't vanish before they're
// read). Also noted in /GOLDBOX/_CYD/ERRORS.TXT (Settings - Logs).
void error(pic::Canvas& c, const char* t)
{
    clear_menu_line(c);
    put(c, t, 0, text::kMenuRow, 12);
    note_until = 1;
    note_held = true;
    Serial.printf("[play] error: %s\n", t);
    char path[96];
    snprintf(path, sizeof path, "%s/_CYD/ERRORS.TXT", games::kRootDir);
    fs::File f = sd_fs().open(path, "a");
    if (f) {
        const uint32_t s = millis() / 1000;
        char line[128];
        const int n = snprintf(line, sizeof line, "[%u:%02u:%02u] %s\n", (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
                               (unsigned)(s % 60), t);
        f.write(reinterpret_cast<const uint8_t*>(line), n > 0 && n < (int)sizeof line ? n : 0);
        f.close();
    }
}

void pick_line(int i, char* out, size_t cap);     // Create New Character's lists
void draw_input(pic::Canvas& c);

// The shop's goods are listed last first (as the games list them)
const uint8_t* goods(int i) { return ground->item[ground->n - 1 - i]; }

// Combat and the treasure after it (play_fight.inc)
bool treasure_on();
const char* tw(int i);

// One line of the list being shown
void heal_line(int i, char* out, size_t cap);
void take_line(int i, char* out, size_t cap);

void list_line(int i, char* out, size_t cap)
{
    if (screen == Screen::Heal) {
        heal_line(i, out, cap);
        return;
    }
    if (screen == Screen::Take) {
        take_line(i, out, cap);
        return;
    }
    if (screen == Screen::AddList) {
        if (d->guy_letter[i])                   // a Pool saved game's: "NAME           from saved game A"
            snprintf(out, cap, "%s%-15s%s%c", d->guy_added[i] ? rw(Data::kAdded) : "", d->guy_name[i], rw(Data::kFromSaved),
                     d->guy_letter[i]);
        else
            snprintf(out, cap, "%s%s", d->guy_added[i] ? rw(Data::kAdded) : "", d->guy_name[i]);
        return;
    }
    if (screen == Screen::CreatePick) {
        pick_line(i, out, cap);
        return;
    }
    if (screen == Screen::Loot) {
        names->name(items::Item{goods(i)}, out, cap);
        return;
    }
    if (screen == Screen::ShopBuy) {
        char nm[48];
        const uint8_t* it = goods(i);
        names->name(items::Item{it}, nm, sizeof nm);
        snprintf(out, cap, "%-21s%9d", nm, rules::price(it, vm->get(0x7F6D) & 0xFF));
    } else {
        const party::Character* ch = pt->sel();
        char nm[48];
        names->name(items::Item{ch->items[i]}, nm, sizeof nm);
        snprintf(out, cap, "%s%s", items::Item{ch->items[i]}.readied() ? d->w_yes : d->w_no, nm);
    }
}

void draw_list(pic::Canvas& c, const char* prompt, const char* what)
{
    PickList& l = plist;
    if (l.index >= l.n) l.index = l.n ? l.n - 1 : 0;
    if (l.index < l.top) l.top = l.index;
    if (l.index >= l.top + l.rows()) l.top = l.index - l.rows() + 1;
    c.fill(l.col0 * 8, l.row0 * 8, (39 - l.col0) * 8, l.rows() * 8, 0);
    for (int r = 0; r < l.rows() && l.top + r < l.n; ++r) {
        char line[64];
        list_line(l.top + r, line, sizeof line);
        // trailing spaces aren't highlighted (as the games trim the line)
        size_t n = strlen(line);
        while (n && line[n - 1] == ' ') line[--n] = 0;
        const int row = l.row0 + r;
        if (l.top + r == l.index) {
            int lead = 0;
            while (line[lead] == ' ') ++lead;
            c.fill((l.col0 + lead) * 8, row * 8, static_cast<int>(n - lead) * 8, 8, 15);
            font::draw_text(c, d->font, line + lead, l.col0 + lead, row, 0, -1);
        } else {
            put(c, line, l.col0, row, screen == Screen::CreatePick && l.top + r == 0 ? 13 : 10);
        }
    }
    char keys[60];
    snprintf(keys, sizeof keys, "%s%s%s%s", what, l.top + l.rows() < l.n ? d->w_next : "", l.top > 0 ? d->w_prev : "",
             d->w_exit[0] == ' ' ? d->w_exit : " Exit");
    text::build(menu, prompt, keys);
    menu.selected = 0;
    show_menu_line(c);
    dirty(0, pic::kScreenH);
}

// ---- A character's items --------------------------------------------------------
// "MATHEW's Items" (row 1), a bar at row 2, "Ready Item" (row 3), the items
// from row 5 as " Yes  Long Sword" / " No   Plate Mail"; menu: Ready, Use
// (exploring / camp), Trade (player characters), Drop, Halve (fewer than
// 16 items), Join, in a shop Sell (player characters) and Id; Exit.

enum class Ask : uint8_t { None, Overwrite, Drop, DropSure, Reroll, SaveNew, OverwriteNew, DropItem, SellDeal, IdDeal,
                          LeaveCoins, CureAnyway, PayCure, Train, MemorizeThese, StopRest, LoseIt, AlterDrop, QuitDos,
                          ScribeThese, ScribeThese2, UseIt };
void ask_yes_no(pic::Canvas& c, Ask what, const char* prompt);

bool shop_yes_no(Ask what, char k, pic::Canvas& c);
bool train_yes_no(Ask what, char k, pic::Canvas& c);
bool magic_yes_no(Ask what, char k, pic::Canvas& c);
bool alter_yes_no(Ask what, char k, pic::Canvas& c);
void open_magic(pic::Canvas& c);
void open_rest(pic::Canvas& c, bool from_magic);
void magic_tap(int x, int y, pic::Canvas& c);
void spells_tap(int x, int y, pic::Canvas& c);
void use_item(int i, pic::Canvas& c);
void reading_tap(char k, pic::Canvas& c);
void rest_tap(int x, int y, pic::Canvas& c);
void rest_tick(uint32_t now, pic::Canvas& c);
void effects_clock(int m, pic::Canvas& c);
void tick_screens(uint32_t now, pic::Canvas& c);
void run_entry(int i, Then next);

const char* iw(int i) { return d->iw[i]; }
int  item_at = -1;                 // the item an offer / question is about
int  trade_from = -1;              // Trade: whose item (pt->selected picks who gets it)
bool in_shop_items() { return view_from == Screen::Shop; }

// The menu for the items screen (as the games build it)
// Use is offered to one who's up and about where the area allows it
// (item_use_facts.md 2: record 0x196, the area word 0x4BE5)
bool can_use_items(const party::Character& ch) { return ch.in_combat() && (vm->get(0x4BE5) & 0xFF) == 0; }

void items_keys(char* out, size_t cap)
{
    const party::Character* ch = pt->sel();
    const bool exploring = view_from == Screen::Game || view_from == Screen::Camp || view_from == Screen::Fight;
    snprintf(out, cap, "%s%s%s%s%s%s%s%s", d->w_ready, exploring && can_use_items(*ch) ? iw(profile::kUse) : "",
             !ch->npc() && !in_shop_items() && view_from != Screen::Fight ? iw(profile::kTrade) : "", iw(profile::kDrop),
             ch->n_items < party::kMaxItems ? iw(profile::kHalve) : "", iw(profile::kJoin),
             in_shop_items() && !ch->npc() ? iw(profile::kSell) : "", in_shop_items() ? iw(profile::kId) : "");
}

// What the game says about an item, in the text rows under the list
// (rows 21-22, colour 14), wrapped
void say_item(pic::Canvas& c, const char* t, uint8_t colour = 14)
{
    c.fill(8, 21 * 8, 38 * 8, 16, 0);
    int row = 21;
    const char* s = t;
    while (*s && row <= 22) {
        int n = static_cast<int>(strlen(s));
        if (n > 38) {
            n = 38;
            while (n > 0 && s[n] != ' ') --n;
            if (n == 0) n = 38;
        }
        char line[40];
        memcpy(line, s, n);
        line[n] = 0;
        put(c, line, 1, row++, colour);
        s += n;
        while (*s == ' ') ++s;
    }
    dirty_rows(21, 22);
}

void item_name(int i, char* out, size_t cap) { names->name(items::Item{pt->sel()->items[i]}, out, cap); }

void draw_items(pic::Canvas& c)
{
    const party::Character* ch = pt->sel();
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    layout::bar(c, d->tables, d->frame_tiles, 2);
    char t[40];
    ch->name(t, sizeof t);
    strncat(t, d->w_s, sizeof t - strlen(t) - 1);
    put(c, t, 1, 1, !ch->in_combat() ? 12 : ch->enemy() ? 14 : 11);
    char nm[20];
    ch->name(nm, sizeof nm);
    put(c, d->w_title, static_cast<int>(strlen(nm)) + 4, 1, 10);
    put(c, d->w_heading, 1, 3, 15);
    plist.row0 = 5;
    plist.row1 = 22;
    plist.col0 = 1;
    plist.n = ch->n_items;
    char keys[48];
    items_keys(keys, sizeof keys);
    draw_list(c, "", keys);
}

void open_items(pic::Canvas& c)
{
    if (!pt->sel() || !pt->sel()->n_items) return;
    screen = Screen::Items;
    plist = PickList{};
    draw_items(c);
}

// Ready / unready an item, with the games' checks
void item_class_values(party::Character& ch, int i);

void ready_item(int i, pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    uint8_t* r = ch.items[i];
    const items::Item it{r};
    char t[64];
    if (it.readied()) {
        if (it.cursed()) {
            note(c, d->w_cursed);
            return;
        }
        r[0x34] = 0;
        rules::worn(ch, i, false);
    } else {
        const items::TypeInfo& ti = names->type(it.type());
        int other = -1;
        // What's already in the hands / that slot
        if (ti.slot <= 8) other = items::readied_in(ch.items, ch.n_items, *names, ti.slot);
        if (ti.slot == 9) {
            int k = 0;
            for (int j = 0; j < ch.n_items; ++j)
                if (items::Item{ch.items[j]}.readied() && names->type(items::Item{ch.items[j]}.type()).slot == 9) {
                    ++k;
                    other = j;
                }
            if (k < 2) other = -1;
        }
        if (it.type() == d->facts.arrow || it.type() == d->facts.quarrel)
            for (int j = 0; j < ch.n_items; ++j)
                if (j != i && items::Item{ch.items[j]}.readied() && items::Item{ch.items[j]}.type() == it.type()) other = j;
        if ((ch.rec[0x12B] & ti.classes) == 0) {
            note(c, d->w_wrong);
            return;
        }
        if (other >= 0) {
            char nm[48];
            names->name(items::Item{ch.items[other]}, nm, sizeof nm);
            snprintf(t, sizeof t, "%s%s", d->w_already, nm);
            note(c, t);
            return;
        }
        if (ch.rec[0x185] + ti.hands > 2) {
            note(c, d->w_hands);
            return;
        }
        r[0x34] = 1;
        rules::worn(ch, i, true);                   // (an item keyed to another alignment hurts and won't stay)
    }
    rules::recalc(ch, *names, d->facts);
    item_class_values(ch, i);
    draw_items(c);
}

// Drop / Trade / Sell need the item put away first (and not readied)
bool can_part_with(int i, pic::Canvas& c)
{
    if (items::Item{pt->sel()->items[i]}.readied()) {
        note(c, iw(profile::kMustUnready));
        return false;
    }
    return true;
}

void drop_item(int i, pic::Canvas& c)
{
    if (!can_part_with(i, c)) return;
    char nm[48], t[96];
    item_name(i, nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s %s", iw(profile::kYour), nm, iw(profile::kGoneForever));
    say_item(c, t);
    item_at = i;
    ask_yes_no(c, Ask::DropItem, iw(profile::kDropIt));
}

void draw_trade(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    draw_party(c, 1);
    char keys[24];
    snprintf(keys, sizeof keys, "%s%s", iw(profile::kSelect), d->w_exit);
    char prompt[44];
    snprintf(prompt, sizeof prompt, "%s ", iw(profile::kTradeWhom));
    text::build(menu, prompt, keys);
    menu.selected = 0;
    show_menu_line(c);
    dirty(0, pic::kScreenH);
}

void trade_item(int i, pic::Canvas& c)
{
    if (!can_part_with(i, c)) return;
    item_at = i;
    trade_from = pt->selected;
    screen = Screen::TradeWho;
    draw_trade(c);
}

// Back to the items of the one who traded (or their sheet when none are left)
void back_to_items(pic::Canvas& c)
{
    if (trade_from >= 0) pt->selected = trade_from;
    trade_from = -1;
    if (pt->sel()->n_items) {
        screen = Screen::Items;
        draw_items(c);
    } else {
        screen = Screen::View;
        draw_character(c);
    }
}

void trade_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (y < text::kMenuTapTop) {
        if (row >= 4 && row < 4 + pt->count && col >= 1) {
            pt->selected = row - 4;
            draw_party(c, 1);
        }
        return;
    }
    const char k = text::key(menu, text::hit(menu, col));
    if (k == 'E') {
        back_to_items(c);
        return;
    }
    if (k != 'S') return;
    const int to = pt->selected;
    if (to == trade_from || trade_from < 0) {
        back_to_items(c);
        return;
    }
    party::Character& from = pt->m[trade_from];
    party::Character& who = pt->m[to];
    uint8_t it[items::kRecordSize];
    memcpy(it, from.items[item_at], sizeof it);
    if (rules::too_heavy(who, it, *names, d->facts)) {
        note(c, d->w_over);
        return;
    }
    rules::add_item(who, it);
    rules::remove_item(from, item_at);
    rules::recalc(who, *names, d->facts);
    rules::recalc(from, *names, d->facts);
    back_to_items(c);
}

void halve_item(int i, pic::Canvas& c)
{
    if (!rules::halve(*pt->sel(), i)) {
        note(c, iw(profile::kCantHalve));
        return;
    }
    rules::recalc(*pt->sel(), *names, d->facts);
    draw_items(c);
}

void join_item(int i, pic::Canvas& c)
{
    rules::join(*pt->sel(), i);
    rules::recalc(*pt->sel(), *names, d->facts);
    draw_items(c);
}

// The shop's offer for an item
void sell_item(int i, pic::Canvas& c)
{
    if (!can_part_with(i, c)) return;
    char nm[48], t[120];
    item_name(i, nm, sizeof nm);
    snprintf(t, sizeof t, "%s%d%s%s", iw(profile::kGiveYou), rules::sell_value(pt->sel()->items[i], d->facts),
             iw(profile::kGoldFor), nm);
    say_item(c, t);
    item_at = i;
    ask_yes_no(c, Ask::SellDeal, iw(profile::kDeal));
}

void identify_item(int i, pic::Canvas& c)
{
    char nm[48], t[120];
    item_name(i, nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s", iw(profile::kIdentify), nm);
    say_item(c, t);
    item_at = i;
    ask_yes_no(c, Ask::IdDeal, iw(profile::kDeal));
}

void add_coins(party::Character& ch, int kind, int n)
{
    const int v = ch.money(kind) + n;
    ch.rec[0xFB + kind * 2] = static_cast<uint8_t>(v);
    ch.rec[0xFC + kind * 2] = static_cast<uint8_t>(v >> 8);
}

// The answers to Drop It? / Is It a Deal?
bool items_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what != Ask::DropItem && what != Ask::SellDeal && what != Ask::IdDeal) return false;
    party::Character& ch = *pt->sel();
    screen = Screen::Items;
    const int i = item_at;
    item_at = -1;
    if (k != 'Y' || i < 0 || i >= ch.n_items) {
        back_to_items(c);
        return true;
    }
    char nm[48], t[120];
    item_name(i, nm, sizeof nm);
    if (what == Ask::DropItem) {
        rules::remove_item(ch, i);
        rules::recalc(ch, *names, d->facts);
        back_to_items(c);
        return true;
    }
    if (what == Ask::SellDeal) {
        const int gold = rules::sell_value(ch.items[i], d->facts);
        rules::remove_item(ch, i);
        const int plat = gold / 5;
        add_coins(ch, 4, plat);
        add_coins(ch, 3, gold % 5);
        rules::recalc(ch, *names, d->facts);
        bool over = false;
        if (ch.encumbrance() > rules::max_load(ch)) {
            add_coins(ch, 4, -plat);              // the platinum goes on the counter
            ground->money[4] += plat;
            rules::recalc(ch, *names, d->facts);
            over = true;
        }
        back_to_items(c);
        note(c, over ? iw(profile::kOverloadPool) : iw(profile::kSold));
        return true;
    }
    // Id: 200 gold, from the character or the counter
    if (rules::gold_worth(ch) >= 200) {
        rules::pay(ch, 200);
    } else if (rules::gold_worth(ground->money) >= 200) {
        rules::pay(ground->money, 200);
    } else {
        back_to_items(c);
        note(c, iw(profile::kNoMoney));
        return true;
    }
    if (ch.items[i][0x35] == 0) {
        snprintf(t, sizeof t, "%s%s", iw(profile::kNothingNew), nm);
    } else {
        ch.items[i][0x35] = 0;
        item_name(i, nm, sizeof nm);
        snprintf(t, sizeof t, "%s%s", iw(profile::kSortOf), nm);
    }
    rules::recalc(ch, *names, d->facts);
    back_to_items(c);
    say_item(c, t);
    return true;
}

// ---- Saving, camp ------------------------------------------------------------------
// Save Current Game (the party menu) and the camp's Save: "Save Which Game:
// A B C D E F G H I J"; SAVGAMx.DAT and each character's CHRDATxn.SAV /
// .SWG / .FX in the game's save folder, as the games write them; the
// journal entries met so far beside it in _CYD/<folder>/SAVGAMx.JNL (the
// engine's own). Camp (the exploring menu's Encamp): "The party makes
// camp...", "Camp: Save View Magic Rest Alter Fix Exit".

struct FileSink : savegame::Sink {
    fs::File& f;
    explicit FileSink(fs::File& file) : f(file) {}
    bool put(const uint8_t* p, size_t n) override { return f.write(p, n) == n; }
};

bool write_file(const char* dir, const char* name, const uint8_t* p, size_t n)
{
    char path[200];
    snprintf(path, sizeof path, "%s/%s", dir, name);      // dir: a full card path
    if (!n) {
        if (sd_fs().exists(path)) sd_fs().remove(path);
        return true;
    }
    fs::File f = sd_fs().open(path, "w");
    if (!f) return false;
    const bool ok = f.write(p, n) == n;
    f.close();
    return ok;
}

bool save_game(char slot)
{
    savegame::Header h;
    h.game_area = d->gs.game_area;
    const bool dungeon = vm->get(0x4BE6) != 0;
    h.last_state = static_cast<uint8_t>(dungeon ? 4 : 3);
    h.state = static_cast<uint8_t>(save_from == Screen::Camp ? 2 : 0);
    for (int i = 0; i < 3; ++i) {
        h.wall_block[i] = d->wall_block[i];
        h.wall_set[i] = d->wall_set[i];
    }
    h.count = pt->count;
    for (int i = 0; i < pt->count; ++i) savegame::char_file(slot, i + 1, h.names[i], sizeof h.names[i]);
    vm->set(0x7F12, d->gs.game_area);
    vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
    char name[24], path[200];
    savegame::file_name(slot, name, sizeof name);
    save_path(name, path, sizeof path);
    fs::File f = sd_fs().open(path, "w");
    if (!f) return false;
    bool ok;
    {
        FileSink sink(f);
        ok = savegame::write(sink, d->gs, h);
    }
    f.close();
    for (int i = 0; ok && i < pt->count; ++i) {
        const party::Character& ch = pt->m[i];
        char fn[48];
        snprintf(fn, sizeof fn, "%s.SAV", h.names[i]);
        ok = write_file(d->save_dir, fn, ch.rec, party::kRecordSize);
        snprintf(fn, sizeof fn, "%s.SWG", h.names[i]);
        ok = ok && write_file(d->save_dir, fn, ch.items[0], static_cast<size_t>(ch.n_items) * party::kItemSize);
        snprintf(fn, sizeof fn, "%s.FX", h.names[i]);
        ok = ok && write_file(d->save_dir, fn, ch.affects[0], static_cast<size_t>(ch.n_affects) * party::kAffectSize);
    }
    // The journal entries met (the engine's own, beside the save)
    if (ok && d->cache_dir[0]) {
        uint8_t buf[kMaxSeen * 2];
        for (int i = 0; i < n_seen; ++i) {
            buf[i * 2] = static_cast<uint8_t>(seen[i].kind);
            buf[i * 2 + 1] = seen[i].num;
        }
        snprintf(name, sizeof name, "SAVGAM%c.JNL", slot);
        write_file(d->cache_dir, name, buf, static_cast<size_t>(n_seen) * 2);
    }
    Serial.printf("[play] saved game %c: %s\n", slot, ok ? "ok" : "FAILED");
    return ok;
}

void load_journal_list(char slot)
{
    n_seen = 0;
    if (!d->cache_dir[0]) return;
    char name[24], path[200];
    snprintf(name, sizeof name, "SAVGAM%c.JNL", slot);
    snprintf(path, sizeof path, "%s/%s", d->cache_dir, name);   // cache_dir: a full card path
    fs::File f = sd_fs().open(path, "r");
    if (!f) return;
    uint8_t b[2];
    while (n_seen < kMaxSeen && f.read(b, 2) == 2) note_seen(static_cast<char>(b[0]), b[1]);
    f.close();
}

void draw_party_menu(pic::Canvas& c);
void draw_camp(pic::Canvas& c);

void ask_save(pic::Canvas& c)
{
    save_from = screen;
    screen = Screen::SaveWhich;
    text::build(menu, d->w_save_which, d->w_slots);
    menu.selected = 0;
    show_menu_line(c);
}

void back_from_save(pic::Canvas& c)
{
    screen = save_from;
    if (screen == Screen::Camp) draw_camp(c);
    else draw_party_menu(c);
}

void save_tap(int x, int y, pic::Canvas& c)
{
    if (y < text::kMenuTapTop) return;
    const int k = text::hit(menu, x / 8);
    if (k < 0) return;
    menu.selected = k;
    clear_menu_line(c);
    put(c, d->w_saving, 0, text::kMenuRow, 10);
    dirty_rows(text::kMenuRow, text::kMenuRow);
    const char slot = text::key(menu, k);
    const bool ok = save_game(slot);
    back_from_save(c);
    if (!ok) {
        char t[48];
        snprintf(t, sizeof t, "Couldn't save game %c on the card.", slot);
        error(c, t);
    }
}

void draw_camp(pic::Canvas& c)
{
    // The exploring screen; the party's camp in the text window
    anim_stop();
    pic_shown = false;
    head_shown = body_shown = -1;
    if (bigpic < 0) {
        draw_frame(c);
        draw_view(c);
        draw_panel(c);
    }
    text::clear(c, text::kTextArea);
    put(c, d->w_makes_camp, 1, 18, 10);
    dirty_rows(17, 22);
    text::build(menu, d->w_camp, d->w_camp_menu);
    menu.selected = 0;
    show_menu_line(c);
}

bool camp_from_script = false;     // PROGRAM 9: breaking camp lets the script go on

void open_camp(pic::Canvas& c)
{
    screen = Screen::Camp;
    draw_camp(c);
}

void end_magic();
magic::Scrolls scroll_facts();
void open_alter(pic::Canvas& c);
void fix_party(pic::Canvas& c);

void leave_camp(pic::Canvas& c)
{
    // Spells not yet memorized are forgotten when the camp breaks (the games)
    for (int i = 0; i < pt->count; ++i) {
        magic::cancel(pt->m[i]);
        magic::cancel_scribes(pt->m[i], scroll_facts());
    }
    end_magic();
    screen = Screen::Game;
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    draw_position(c);
    if (camp_from_script) {
        camp_from_script = false;
        waiting = false;
        handle(vm->resume());
        return;
    }
    idle_menu(c);
}

void camp_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y >= text::kMenuTapTop) {
        switch (text::key(menu, text::hit(menu, col))) {
        case 'S': ask_save(c); break;
        case 'V': view_character(c); break;
        case 'M': open_magic(c); break;
        case 'R': open_rest(c, false); break;
        case 'A': open_alter(c); break;
        case 'F': fix_party(c); break;
        case 'E': leave_camp(c); break;
        case 0: break;
        default: error(c, "Not in the engine yet."); break;
        }
        return;
    }
    if (col >= 17 && row >= 4 && row < 4 + pt->count && bigpic < 0) {
        pt->selected = row - 4;
        draw_party(c, 17);
    }
}


// ---- Add, Remove, Drop -----------------------------------------------------------
// Add Character to Party: "Add from where? Curse Pool Hillsfar Exit"; Curse
// lists the characters saved in the save folder (.GUY files: a removed
// character), "Add a character: Add Next Prev Exit", "* " before those
// added; the party's rules (6 player characters, 8 in all, rangers,
// paladins and evil). Remove Character from Party saves them as
// NAME.GUY (+ .SWG / .FX; "Overwrite NAME? Yes No" when there is one).
// Drop Character: "Drop NAME forever? ", "Are you sure? ", their files go.

Ask ask = Ask::None;

// The file name the games give a character: the name without spaces and
// punctuation, 8 letters at most
void guy_base(const party::Character& ch, char* out, size_t cap)
{
    char nm[20];
    ch.name(nm, sizeof nm);
    size_t o = 0;
    for (const char* p = nm; *p && o + 1 < cap && o < 8; ++p)
        if (!strchr(" .*,?/\\:;|", *p)) out[o++] = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
    out[o] = 0;
}

bool write_character(const char* base, const party::Character& ch)
{
    char fn[24];
    snprintf(fn, sizeof fn, "%s.GUY", base);
    bool ok = write_file(d->save_dir, fn, ch.rec, party::kRecordSize);
    snprintf(fn, sizeof fn, "%s.SWG", base);
    ok = ok && write_file(d->save_dir, fn, ch.items[0], static_cast<size_t>(ch.n_items) * party::kItemSize);
    snprintf(fn, sizeof fn, "%s.FX", base);
    ok = ok && write_file(d->save_dir, fn, ch.affects[0], static_cast<size_t>(ch.n_affects) * party::kAffectSize);
    return ok;
}

void delete_character(const char* base)
{
    for (const char* ext : {".GUY", ".SWG", ".FX"}) {
        char fn[24];
        snprintf(fn, sizeof fn, "%s%s", base, ext);
        write_file(d->save_dir, fn, nullptr, 0);
    }
}

void leave_party(int i)
{
    if (i < 0 || i >= pt->count) return;
    for (int k = i; k + 1 < pt->count; ++k) pt->m[k] = pt->m[k + 1];
    --pt->count;
    pt->selected = i > 0 ? i - 1 : 0;
    vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
}

void ask_yes_no(pic::Canvas& c, Ask what, const char* prompt)
{
    ask = what;
    screen = Screen::YesNo;
    text::build(menu, prompt, rw(Data::kYesNo));
    menu.selected = 1;          // the games start on No
    show_menu_line(c);
}

void remove_character(pic::Canvas& c, bool overwrite_ok)
{
    party::Character* ch = pt->sel();
    if (!ch) return;
    if (ch->npc()) {
        // An NPC doesn't go home: they leave for good
        char t[60], nm[20];
        ch->name(nm, sizeof nm);
        snprintf(t, sizeof t, "%s%s%s", rw(Data::kDrop), nm, rw(Data::kForever));
        ask_yes_no(c, Ask::Drop, t);
        return;
    }
    char base[12], fn[24], path[200];
    guy_base(*ch, base, sizeof base);
    if (new_base[0]) snprintf(base, sizeof base, "%s", new_base);       // the name typed (not cleaned, as the original)
    snprintf(fn, sizeof fn, "%s.GUY", base);
    save_path(fn, path, sizeof path);
    if (!overwrite_ok && sd_fs().exists(path)) {
        char t[60];
        snprintf(t, sizeof t, "%s%s%s", rw(Data::kOverwrite), base, rw(Data::kQmark));
        ask_yes_no(c, Ask::Overwrite, t);
        return;
    }
    new_base[0] = 0;
    if (!write_character(base, *ch)) {
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        char t[48];
        snprintf(t, sizeof t, "Couldn't write %s.GUY to the card.", base);
        error(c, t);
        return;
    }
    leave_party(pt->selected);
    screen = Screen::PartyMenu;
    draw_party_menu(c);
}

bool create_yes_no(Ask what, char k, pic::Canvas& c);

void yes_no_key(char k, pic::Canvas& c);

void yes_no_tap(int x, int y, pic::Canvas& c)
{
    if (y < text::kMenuTapTop) return;
    yes_no_key(text::key(menu, text::hit(menu, x / 8)), c);
}

void yes_no_key(char k, pic::Canvas& c)
{
    if (k != 'Y' && k != 'N') return;
    const Ask what = ask;
    ask = Ask::None;
    if (create_yes_no(what, k, c)) return;
    if (items_yes_no(what, k, c)) return;
    if (shop_yes_no(what, k, c)) return;
    if (train_yes_no(what, k, c)) return;
    if (magic_yes_no(what, k, c)) return;
    if (alter_yes_no(what, k, c)) return;
    party::Character* ch = pt->sel();
    char nm[20] = {}, t[64];
    if (ch) ch->name(nm, sizeof nm);
    screen = Screen::PartyMenu;
    if (what == Ask::Overwrite) {
        if (k == 'Y') {
            remove_character(c, true);
        } else {
            // "New file name: " - up to 8, upper case; asked again while empty (no way back, as the original)
            draw_party_menu(c);
            name_for_new = false;
            input_engine = true;
            engine_ask = EngineAsk::FileName;
            input_prompt = rw(Data::kNewFile);
            input_max = 8;
            input_mode = Input::Text;
            input_len = 0;
            input_buf[0] = 0;
            draw_input(c);
        }
        return;
    }
    if (what == Ask::Drop && k == 'Y') {
        ask_yes_no(c, Ask::DropSure, rw(Data::kSure));
        return;
    }
    draw_party_menu(c);
    if (!ch) return;
    if (what == Ask::DropSure && k == 'Y') {
        if (!ch->in_combat()) snprintf(t, sizeof t, "%s%s%s", rw(Data::kDump), nm, rw(Data::kOutBack));
        else snprintf(t, sizeof t, "%s%s", nm, rw(Data::kFarewell));
        char base[12];
        guy_base(*ch, base, sizeof base);
        delete_character(base);
        leave_party(pt->selected);
        draw_party_menu(c);
    } else {
        snprintf(t, sizeof t, "%s%s", nm, rw(Data::kRelief));
    }
    note(c, t);
}

// The .GUY files in the save folder not already in the party
char pool_dir[128] = {};        // the player's Pool of Radiance folder on the card ("" if none)

bool in_party(const char* name)
{
    for (int i = 0; i < pt->count; ++i) {
        char pn[20];
        pt->m[i].name(pn, sizeof pn);
        if (strcmp(pn, name) == 0) return true;
    }
    return false;
}

// Pool of Radiance characters (import_facts.md 2): *.CHA, then *.SAV, of
// exactly a Pool record's size, player characters, not in the party - in
// the save folder (src 1) or Pool's own folder (src 2)
void find_pool_guys(const char* where, uint8_t src)
{
    if (!where[0]) return;
    for (int pass = 0; pass < 2; ++pass) {
        fs::File dir = sd_fs().open(where, "r");
        if (!dir || !dir.isDirectory()) return;
        for (fs::File f = dir.openNextFile(); f && d->guys < Data::kMaxGuys; f = dir.openNextFile()) {
            const char* nm = f.name();
            const char* slash = strrchr(nm, '/');
            if (slash) nm = slash + 1;
            const size_t n = strlen(nm);
            if (f.isDirectory() || n < 5 || n > 12 || strcasecmp(nm + n - 4, pass ? ".SAV" : ".CHA") != 0 ||
                f.size() != create::kPoolRecordSize)
                continue;
            uint8_t rec[create::kPoolRecordSize];
            if (f.read(rec, sizeof rec) != sizeof rec || !create::pool_is_pc(rec)) continue;
            char name[16];
            create::pool_name(rec, name, sizeof name);
            if (in_party(name)) continue;
            strncpy(d->guy_file[d->guys], nm, sizeof d->guy_file[0] - 1);
            strcpy(d->guy_name[d->guys], name);
            d->guy_added[d->guys] = false;
            d->guy_src[d->guys] = src;
            d->guy_letter[d->guys] = pass && n == 12 ? static_cast<char>(toupper(nm[6])) : 0;
            ++d->guys;
        }
        dir.close();
    }
}

void find_guys(char from = 'C')
{
    d->guys = 0;
    d->guy_from = from;
    if (from == 'P') {
        find_pool_guys(d->save_dir, 1);
        if (pool_dir[0] && strcasecmp(pool_dir, d->save_dir) != 0) find_pool_guys(pool_dir, 2);
        return;
    }
    fs::File dir = sd_fs().open(d->save_dir, "r");
    if (!dir || !dir.isDirectory()) return;
    for (fs::File f = dir.openNextFile(); f && d->guys < Data::kMaxGuys; f = dir.openNextFile()) {
        const char* nm = f.name();
        const char* slash = strrchr(nm, '/');
        if (slash) nm = slash + 1;
        const size_t n = strlen(nm);
        // .GUY files, and (Tom, v0.58.0 - the original lists only those)
        // the members of saved games, CHRDAT<letter><n>.SAV
        const bool guy = n >= 5 && n <= 12 && strcasecmp(nm + n - 4, ".GUY") == 0;
        const bool member = n == 12 && strncasecmp(nm, "CHRDAT", 6) == 0 && strcasecmp(nm + 8, ".SAV") == 0;
        if (f.isDirectory() || (!guy && !member) || f.size() != party::kRecordSize) continue;
        uint8_t rec[0x100];
        if (f.read(rec, 0xF8) != 0xF8 || rec[0xF7] > 0x7F) continue;     // NPCs aren't listed
        char name[16];
        size_t len = rec[0] > 15 ? 15 : rec[0];
        memcpy(name, rec + 1, len);
        name[len] = 0;
        bool in_party = false;
        for (int i = 0; i < pt->count; ++i) {
            char pn[20];
            pt->m[i].name(pn, sizeof pn);
            if (strcmp(pn, name) == 0) in_party = true;
        }
        if (in_party) continue;
        strncpy(d->guy_file[d->guys], nm, sizeof d->guy_file[0] - 1);
        strcpy(d->guy_name[d->guys], name);
        d->guy_added[d->guys] = false;
        d->guy_src[d->guys] = 0;
        d->guy_letter[d->guys] = member ? static_cast<char>(toupper(nm[6])) : 0;
        ++d->guys;
    }
    dir.close();
    // In order: the .GUY characters by name, then each saved game's members
    // (game A first, in party order)
    auto before = [](int a, int b) {
        if ((d->guy_letter[a] != 0) != (d->guy_letter[b] != 0)) return d->guy_letter[a] == 0;
        return d->guy_letter[a] ? strcasecmp(d->guy_file[a], d->guy_file[b]) < 0
                                : strcmp(d->guy_name[a], d->guy_name[b]) < 0;
    };
    for (int i = 1; i < d->guys; ++i)
        for (int j = i; j > 0 && before(j, j - 1); --j) {
            char f[13], nm2[16];
            memcpy(f, d->guy_file[j], 13); memcpy(d->guy_file[j], d->guy_file[j - 1], 13); memcpy(d->guy_file[j - 1], f, 13);
            memcpy(nm2, d->guy_name[j], 16); memcpy(d->guy_name[j], d->guy_name[j - 1], 16); memcpy(d->guy_name[j - 1], nm2, 16);
            const char l = d->guy_letter[j]; d->guy_letter[j] = d->guy_letter[j - 1]; d->guy_letter[j - 1] = l;
        }
}

bool load_pool_guy(int i, party::Character& ch);

void draw_add_list(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    plist.row0 = 2;
    plist.row1 = 22;
    plist.col0 = 1;
    plist.n = d->guys;
    char what[12];
    strncpy(what, rw(Data::kAdd), sizeof what - 1);
    what[sizeof what - 1] = 0;
    for (size_t n = strlen(what); n && what[n - 1] == ' ';) what[--n] = 0;
    draw_list(c, rw(Data::kAddPrompt), what);
}

// Adds the chosen character, with the games' rules
void add_checked(int i, party::Character& ch, pic::Canvas& c);

void add_character(int i, pic::Canvas& c)
{
    if (i < 0 || i >= d->guys || d->guy_added[i] || pt->count >= party::kMaxParty) return;
    party::Character& ch = pt->m[pt->count];
    if (d->guy_src[i] != 0) {
        if (!load_pool_guy(i, ch)) return;
        add_checked(i, ch, c);
        return;
    }
    char base[16];
    strncpy(base, d->guy_file[i], sizeof base - 1);
    base[sizeof base - 1] = 0;
    char* dot = strrchr(base, '.');
    if (dot) *dot = 0;
    fs::File f;
    if (!open_save_file(d->guy_file[i], f)) return;
    bool ok;
    {
        library::FileSource src(f);
        ok = party::read_record(src, ch);
    }
    f.close();
    if (!ok) return;
    char fn[24];
    snprintf(fn, sizeof fn, "%s.SWG", base);
    if (open_save_file(fn, f)) {
        library::FileSource src(f);
        party::read_items(src, ch);
        f.close();
    }
    snprintf(fn, sizeof fn, "%s.FX", base);
    if (open_save_file(fn, f)) {
        library::FileSource src(f);
        party::read_affects(src, ch);
        f.close();
    }
    add_checked(i, ch, c);
}

// The party's rules for a character read into the next party slot
void add_checked(int i, party::Character& ch, pic::Canvas& c)
{
    // Already in the party (the same name and the same "mod id", 0x126): no
    char nm[20];
    ch.name(nm, sizeof nm);
    for (int k = 0; k < pt->count; ++k) {
        char pn[20];
        pt->m[k].name(pn, sizeof pn);
        if (strcmp(pn, nm) == 0 && pt->m[k].rec[0x126] == ch.rec[0x126]) return;
    }
    // The party's rules
    int pcs = 0, rangers = 0;
    bool evil = false, paladin = false;
    char paladin_name[20] = {};
    for (int k = 0; k < pt->count; ++k) {
        const party::Character& m = pt->m[k];
        if (!m.npc()) ++pcs;
        if (m.level(4) > 0) ++rangers;
        if ((m.alignment() + 1) % 3 == 0) evil = true;
        if (m.level(3) > 0) {
            paladin = true;
            m.name(paladin_name, sizeof paladin_name);
        }
    }
    const bool is_evil = (ch.alignment() + 1) % 3 == 0;
    char t[64];
    if (ch.level(3) > 0 && evil) {
        note(c, rw(Data::kPaladinEvil));
        return;
    }
    if (ch.level(4) > 0 && rangers > 2) {
        note(c, rw(Data::kRangers));
        return;
    }
    if (is_evil && paladin) {
        snprintf(t, sizeof t, "%s%s", paladin_name, rw(Data::kNoEvil));
        note(c, t);
        return;
    }
    if ((!ch.npc() && pcs >= 6) || pt->count >= party::kMaxParty) return;
    rules::recalc(ch, *names, d->facts);
    ++pt->count;
    vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
    d->guy_added[i] = true;
    if (pcs + 1 >= 6 || pt->count >= party::kMaxParty) {
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return;
    }
    draw_add_list(c);
}

void add_from_tap(int x, int y, pic::Canvas& c)
{
    if (note_until) {                   // (an error: the tap puts it away first)
        redraw_menu(c);
        return;
    }
    if (y < text::kMenuTapTop) return;
    switch (text::key(menu, text::hit(menu, x / 8))) {
    case 'C':
        find_guys();
        if (!d->guys) {
            screen = Screen::PartyMenu;
            draw_party_menu(c);
            return;
        }
        screen = Screen::AddList;
        plist = PickList{};
        draw_add_list(c);
        return;
    case 'E':
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return;
    case 'P':
        find_guys('P');
        if (!d->guys) {
            screen = Screen::PartyMenu;
            draw_party_menu(c);
            return;
        }
        screen = Screen::AddList;
        plist = PickList{};
        draw_add_list(c);
        return;
    case 'H':
        // (Tom, 2026-10-10: Hillsfar isn't one of the engine's games - no import)
        error(c, "Hillsfar characters can't be added.");
        return;
    default:
        return;
    }
}


// ---- Create New Character ---------------------------------------------------------
// As the games make one: "Pick Race", "Pick Gender", "Pick Class", "Pick
// Alignment" (a list from row 2, the heading in the prompt colour; "Select
// Next Prev Exit"); the stats rolled and the character shown - "Reroll
// stats? Yes No"; "Character name: "; "Save NAME? Yes No" - saved as
// NAME.GUY in the save folder (Add Character to Party then brings them
// in). The rules and tables: engine/create, engine/classes, from the
// player's program. (The combat icon editor comes with combat.)

struct Making {
    classes::Tables tables;
    create::Facts   facts{};
    create::Dice    dice;
    party::Character ch;
    int  stage = 0;                 // 0 race, 1 gender, 2 class, 3 alignment
    int  opt[17] = {};
    int  n_opt = 0;
    int  race = 0, sex = 0, cls = 0;
    char words[9][24] = {};         // Pick Race ... "? " (profile create.pick_race ...)
    // Human Change (stage 4): "Pick New Class", " doesn't qualify.", "Select", " is now a 1st level ", "."
    char change[5][24] = {};
};
Making* mk = nullptr;

void end_create(pic::Canvas& c)
{
    new_char = nullptr;
    delete mk;
    mk = nullptr;
    input_engine = false;
    input_mode = Input::None;
    screen = Screen::PartyMenu;
    draw_party_menu(c);
}

// The rule tables and facts, read from the program for the occasion
bool load_rules(Making*& m)
{
    const auto& pc = d->prof->create;
    if (!pc.ds_image) return false;
    m = new (std::nothrow) Making;
    if (!m) return false;
    m->dice = create::Dice(static_cast<uint32_t>(millis()) * 2654435761u + 1);
    fs::File f;
    if (!open_file(d->prof->program, f)) return false;
    bool ok = false;
    {
        library::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok) {
            const size_t n = pc.tables.hi - pc.tables.lo;
            uint8_t* buf = static_cast<uint8_t*>(malloc(n));
            ok = buf && exepack::read(src, info, pc.ds_image + pc.tables.lo, buf, n) == exepack::Status::Ok &&
                 m->tables.set(pc.tables, buf, n);
            free(buf);
            uint8_t hp[16];
            ok = ok && exepack::read(src, info, pc.ds_image + pc.hp_count, hp, 8) == exepack::Status::Ok &&
                 exepack::read(src, info, pc.ds_image + pc.hp_dice, hp + 8, 8) == exepack::Status::Ok &&
                 exepack::read(src, info, pc.ds_image + pc.icon_colours, m->facts.icon_colours, 6) ==
                     exepack::Status::Ok;
            memcpy(m->facts.hp_count, hp, 8);
            memcpy(m->facts.hp_dice, hp + 8, 8);
        }
    }
    f.close();
    create::Facts& fa = m->facts;
    fa.con_save = pc.con_save;
    fa.dwarf_orc = pc.dwarf_orc;
    fa.giants = pc.giants;
    fa.gnome_giant = pc.gnome_giant;
    fa.gnome_extra = pc.gnome_extra;
    fa.elf_sleep = pc.elf_sleep;
    fa.halfelf = pc.halfelf;
    fa.prot_evil = pc.prot_evil;
    fa.ranger_giant = pc.ranger_giant;
    memcpy(fa.mu_first, pc.mu_first, 4);
    fa.mu_level2 = pc.mu_level2;
    memcpy(fa.mu_level3, pc.mu_level3, 2);
    fa.mu_level4 = pc.mu_level4;
    fa.mu_level5 = pc.mu_level5;
    memcpy(fa.mu_change, pc.mu_change, 3);
    const uint32_t at[9] = {pc.pick_race, pc.pick_gender, pc.pick_class, pc.pick_alignment, pc.select,
                            pc.reroll,    pc.char_name,   pc.save_q,     pc.qmark};
    const uint32_t ch_at[5] = {pc.pick_new, pc.no_qualify, pc.change_select, pc.now_first, pc.dot};
    if (open_file(d->prof->overlay, f)) {
        library::FileSource src(f);
        for (int i = 0; i < 9; ++i)
            if (at[i]) text::read_pascal(src, at[i], m->words[i], sizeof m->words[i]);
        for (int i = 0; i < 5; ++i)
            if (ch_at[i]) text::read_pascal(src, ch_at[i], m->change[i], sizeof m->change[i]);
        f.close();
    }
    return ok;
}

bool load_making() { return load_rules(mk); }

// A Pool character into ch (false: unreadable, or no rule tables)
bool load_pool_guy(int i, party::Character& ch)
{
    char path[200];
    const char* where = d->guy_src[i] == 2 ? pool_dir : d->save_dir;
    snprintf(path, sizeof path, "%s/%s", where, d->guy_file[i]);
    fs::File f = sd_fs().open(path, "r");
    if (!f) return false;
    uint8_t rec[create::kPoolRecordSize];
    const bool ok = f.read(rec, sizeof rec) == sizeof rec;
    f.close();
    if (!ok) return false;
    Making* tb = nullptr;
    if (!load_rules(tb)) {
        delete tb;
        return false;
    }
    create::from_pool(rec, ch, tb->tables);
    delete tb;
    // A NAME.CHA's NAME.SPC: the racial effects only (a saved game's
    // CHRDAT??.SPC isn't found by the name - as the original)
    char base[16];
    strncpy(base, d->guy_file[i], sizeof base - 1);
    base[sizeof base - 1] = 0;
    char* dot = strrchr(base, '.');
    if (dot && strcasecmp(dot, ".CHA") == 0) {
        *dot = 0;
        snprintf(path, sizeof path, "%s/%s.SPC", where, base);
        fs::File s = sd_fs().open(path, "r");
        if (s) {
            uint8_t a[party::kAffectSize];
            while (ch.n_affects < party::kMaxAffects && s.read(a, sizeof a) == sizeof a)
                if (create::pool_effect_kept(a[0])) memcpy(ch.affects[ch.n_affects++], a, sizeof a);
            s.close();
        }
    }
    return true;
}

const char* option_name(int i)
{
    const int v = mk->opt[i];
    switch (mk->stage) {
    case 0: return name_of(d->race[0], 10, 8, v);
    case 1: return name_of(d->sex[0], 7, 2, v);
    case 2:
    case 4: return name_of(d->cls[0], 27, 18, v);
    default: return name_of(d->alignment[0], 17, 9, v);
    }
}

void pick_line(int i, char* out, size_t cap)
{
    if (i == 0) snprintf(out, cap, "%s", mk->stage == 4 ? mk->change[0] : mk->words[mk->stage]);
    else snprintf(out, cap, "  %s", option_name(i - 1));
}

void draw_pick(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    plist.row0 = 2;
    plist.row1 = 22;
    plist.col0 = 1;
    plist.n = mk->n_opt + 1;                // the heading, then the choices
    if (plist.index < 1) plist.index = 1;
    draw_list(c, "", mk->stage == 4 ? mk->change[2] : mk->words[4]);
}

// ---- Human Change (the party menu, where training is offered): a human
// with no former class takes up another (engine/create: change_classes,
// change_class): "Pick New Class" and the classes they qualify for
// ("Select ... Exit"), else "NAME doesn't qualify."; then "NAME is now a
// 1st level CLASS." and items the new class can't use are put away.
void start_change(pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch) return;
    if (!load_making()) {
        delete mk;
        mk = nullptr;
        pm_prompt(c, "Not in the engine yet.");
        return;
    }
    mk->stage = 4;
    mk->n_opt = create::change_classes(*ch, mk->tables, mk->opt, 17);
    if (!mk->n_opt) {
        char t[60], nm[20];
        ch->name(nm, sizeof nm);
        snprintf(t, sizeof t, "%s%s", nm, mk->change[1]);
        delete mk;
        mk = nullptr;
        pm_prompt(c, t);
        dirty_rows(text::kMenuRow, text::kMenuRow);
        return;
    }
    screen = Screen::CreatePick;
    plist = PickList{};
    plist.index = 1;
    draw_pick(c);
}

void change_picked(int cls, pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch || !mk) return;
    create::change_class(*ch, mk->tables, mk->facts, cls);
    for (int k = 0; k < ch->n_items; ++k) {
        uint8_t* it = ch->items[k];
        if (it[0x34] && !it[0x36] && !(ch->rec[0x12B] & names->type(it[0x2E]).classes)) {
            it[0x34] = 0;
            rules::worn(*ch, k, false);
        }
    }
    rules::recalc(*ch, *names, d->facts);
    char t[80], nm[20];
    ch->name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s%s%s", nm, mk->change[3], name_of(d->cls[0], 27, 18, cls), mk->change[4]);
    end_create(c);
    pm_prompt(c, t);
    dirty_rows(text::kMenuRow, text::kMenuRow);
}

void pick_stage(int stage, pic::Canvas& c)
{
    mk->stage = stage;
    switch (stage) {
    case 0: mk->n_opt = create::races(mk->opt, 17); break;
    case 1:
        mk->opt[0] = 0;
        mk->opt[1] = 1;
        mk->n_opt = 2;
        break;
    case 2: mk->n_opt = create::classes_for(mk->tables, mk->race, mk->opt, 17); break;
    default: mk->n_opt = create::alignments_for(mk->tables, mk->cls, mk->opt, 17); break;
    }
    screen = Screen::CreatePick;
    plist = PickList{};
    plist.index = stage == 1 ? 1 : 1;
    draw_pick(c);
}

void start_create(pic::Canvas& c)
{
    if (!load_making()) {
        delete mk;
        mk = nullptr;
        pm_prompt(c, "Not in the engine yet.");
        return;
    }
    pick_stage(0, c);
}

void show_new_character(pic::Canvas& c)
{
    new_char = &mk->ch;
    view_from = Screen::PartyMenu;
    draw_character(c);
    clear_menu_line(c);
}

void picked(int i, pic::Canvas& c)
{
    if (i < 1 || i > mk->n_opt) return;
    const int v = mk->opt[i - 1];
    switch (mk->stage) {
    case 0: mk->race = v; pick_stage(1, c); return;
    case 1: mk->sex = v; pick_stage(2, c); return;
    case 2: mk->cls = v; pick_stage(3, c); return;
    case 4: change_picked(v, c); return;
    default:
        create::begin(mk->ch, mk->tables, mk->facts, mk->dice, mk->race, mk->sex, mk->cls, v);
        create::roll(mk->ch, mk->tables, mk->facts, mk->dice);
        rules::recalc(mk->ch, *names, d->facts);
        show_new_character(c);
        ask_yes_no(c, Ask::Reroll, mk->words[5]);
        return;
    }
}

void start_icon(pic::Canvas& c, Screen from);
void create_after_icon(pic::Canvas& c);

// The name, then the icon editor (as the games do), then "save NAME?"
void create_named(const char* name, pic::Canvas& c)
{
    if (!mk) return;
    create::set_name(mk->ch, name);
    start_icon(c, Screen::CreateName);
    if (screen != Screen::Icon) create_after_icon(c);      // (no memory for the editor)
}

void create_after_icon(pic::Canvas& c)
{
    if (!mk) return;
    screen = Screen::CreateName;
    show_new_character(c);
    char t[72], nm[20];
    mk->ch.name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s%s", mk->words[7], nm, mk->words[8]);
    ask_yes_no(c, Ask::SaveNew, t);
}

void ask_name(pic::Canvas& c)
{
    show_new_character(c);
    screen = Screen::CreateName;
    input_engine = true;
    engine_ask = EngineAsk::Name;
    input_prompt = mk->words[6];
    input_max = party::kNameMax;
    input_mode = Input::Text;
    input_len = 0;
    input_buf[0] = 0;
    draw_input(c);
}

// Saves the new character as NAME.GUY (asking first if there is one)
void save_new(pic::Canvas& c, bool overwrite_ok)
{
    char base[12], fn[24], path[200];
    guy_base(mk->ch, base, sizeof base);
    if (new_base[0]) snprintf(base, sizeof base, "%s", new_base);       // the name typed (Overwrite -> No)
    snprintf(fn, sizeof fn, "%s.GUY", base);
    save_path(fn, path, sizeof path);
    if (!overwrite_ok && sd_fs().exists(path)) {
        char t[60];
        snprintf(t, sizeof t, "%s%s%s", rw(Data::kOverwrite), base, rw(Data::kQmark));
        ask_yes_no(c, Ask::OverwriteNew, t);
        return;
    }
    new_base[0] = 0;
    const bool ok = write_character(base, mk->ch);
    end_create(c);
    if (!ok) {
        char t[48];
        snprintf(t, sizeof t, "Couldn't write %s.GUY to the card.", base);
        error(c, t);
    }
}


bool create_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what == Ask::Reroll) {
        if (k == 'Y') {
            create::roll(mk->ch, mk->tables, mk->facts, mk->dice);
            rules::recalc(mk->ch, *names, d->facts);
            show_new_character(c);
            ask_yes_no(c, Ask::Reroll, mk->words[5]);
        } else {
            ask_name(c);
        }
        return true;
    }
    if (what == Ask::SaveNew) {
        if (k == 'Y') save_new(c, false);
        else end_create(c);
        return true;
    }
    if (what == Ask::OverwriteNew) {
        if (k == 'Y') {
            save_new(c, true);
        } else {
            // "New file name: " as for Remove (the same routine in the original)
            name_for_new = true;
            input_engine = true;
            engine_ask = EngineAsk::FileName;
            input_prompt = rw(Data::kNewFile);
            input_max = 8;
            input_mode = Input::Text;
            input_len = 0;
            input_buf[0] = 0;
            draw_input(c);
        }
        return true;
    }
    return false;
}

// ---- The shop -------------------------------------------------------------------
// A script sets the shop flag, sets out the goods (TREASURE) and starts a
// "fight" (COMBAT): the shop's menu "Buy View Pool Appraise Exit" (Take
// and Share when coins lie on the counter) under the shop's picture and
// the party list. Buy lists the goods ("Items: ", name and price); the
// selected character pays (from the pool when they can't), if they can
// carry it. Pool puts everyone's coins on the counter, Share shares them
// out. (The games' shopkeeper reminds the party of coins left behind.)

bool temple = false;               // the shop screen is the temple's (Heal instead of Buy)
create::Dice rng;                   // the temple's dice, appraising

// One of the shop's / temple's words, read from GAME.OVR when needed (the
// long ones would take RAM all the time)
void sw(int i, char* out, size_t cap)
{
    out[0] = 0;
    const uint32_t at = d->prof->shop_words[i];
    fs::File f;
    if (!at || !open_file(d->prof->overlay, f)) return;
    library::FileSource src(f);
    text::read_pascal(src, at, out, cap);
    f.close();
}

void treasure_menu(pic::Canvas& c);

void shop_menu(pic::Canvas& c)
{
    if (treasure_on()) {
        treasure_menu(c);
        return;
    }
    char words[48];
    if (temple) sw(ground->any_money() ? profile::kHealMenuMoney : profile::kHealMenu, words, sizeof words);
    else snprintf(words, sizeof words, "%s", ground->any_money() ? d->w_shop_money : d->w_shop);
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

void draw_shop(pic::Canvas& c)
{
    // The exploring screen with the shop's picture, as the script left it
    anim_stop();
    bigpic = -1;
    pic_shown = false;
    head_shown = body_shown = -1;
    draw_frame(c);
    if (last_pic_id >= 0 && !treasure_on()) host_picture(last_pic_id, last_pic_head);
    else draw_view(c);
    draw_panel(c);
    shop_menu(c);
}

void open_shop(pic::Canvas& c)
{
    screen = Screen::Shop;
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    if (pt->count) draw_party(c, 17);
    shop_menu(c);
}

// Coins left on the counter: the shopkeeper (the priest) calls the party
// back - "Do you want to go back and get your Money?" Yes stays, No goes
// (the coins stay behind)
void leave_shop(pic::Canvas& c, bool asked = false);

void ask_leave(pic::Canvas& c)
{
    char says[96], back[64];
    if (treasure_on()) {
        snprintf(says, sizeof says, "%s", tw(profile::kTreasureLeft));
        snprintf(back, sizeof back, "%s", tw(profile::kClaimTreasure));
    } else {
        sw(temple ? profile::kPriestSays : profile::kShopSays, says, sizeof says);
        sw(temple ? profile::kPriestRetrieve : profile::kShopRetrieve, back, sizeof back);
    }
    text::clear(c, text::kTextArea);
    text::begin(w, c, says, text::kTextArea, 10, true);
    text::step(w, c, d->font, -1);
    text::begin(w, c, back, text::kTextArea, temple ? 10 : 15, false);
    text::step(w, c, d->font, -1);
    dirty_rows(17, 22);
    ask_yes_no(c, Ask::LeaveCoins, "");
}

void treasure_off();

void leave_shop(pic::Canvas& c, bool asked)
{
    if (!asked && (ground->any_money() || (treasure_on() && ground->n))) {
        ask_leave(c);
        return;
    }
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    temple = false;
    if (treasure_on()) {
        treasure_off();
        ground->clear();            // what's left behind is lost
    }
    screen = Screen::Game;
    clear_menu_line(c);
    waiting = false;
    handle(vm->resume());
}

void draw_buy(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    plist.row0 = 1;
    plist.row1 = 22;
    plist.col0 = 1;
    plist.n = ground->n;
    draw_list(c, d->w_items, d->w_buy);
}

void buy(int i, pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch || i < 0 || i >= ground->n) return;
    const uint8_t* it = goods(i);
    const int cost = rules::price(it, vm->get(0x7F6D) & 0xFF);
    const bool own = cost <= rules::gold_worth(*ch);
    if (!own && cost > rules::gold_worth(ground->money)) {
        note(c, d->w_no_money);
        return;
    }
    if (rules::too_heavy(*ch, it, *names, d->facts)) {
        note(c, d->w_over);
        return;
    }
    rules::add_item(*ch, it);
    if (own) rules::pay(*ch, cost);
    else rules::pay(ground->money, cost);
    rules::recalc(*ch, *names, d->facts);
    char nm[48];
    names->name(items::Item{it}, nm, sizeof nm);
    Serial.printf("[play] bought %s for %d\n", nm, cost);
    draw_buy(c);
}

// ---- The temple's Heal ----------------------------------------------------------
// "NAME, how can we help you?" (row 1, colour 15) over the ten cures (from
// row 4, column 2), "Heal Exit". A cure that does nothing for them asks
// "cast cure anyway: "; then "<cure> will only cost N gold pieces.", "pay
// for cure " - the character pays, or the counter; "NAME is cured."

struct HealWords {
    char name[10][24];
    char help[28];
    char heal[8];                   // "Heal" (from "Heal Exit")
};
HealWords* hw = nullptr;
int cure_at = -1;

void heal_line(int i, char* out, size_t cap) { snprintf(out, cap, "%s", hw && i < 10 ? hw->name[i] : ""); }

void draw_heal(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    char t[48], nm[20];
    pt->sel()->name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s", nm, hw->help);
    put(c, t, 1, 1, 15);
    plist.row0 = 4;
    plist.row1 = 13;
    plist.col0 = 2;
    plist.n = 10;
    draw_list(c, "", hw->heal);
}

void open_heal(pic::Canvas& c)
{
    if (!pt->sel()) return;
    if (!hw) hw = new (std::nothrow) HealWords;
    if (!hw) {
        error(c, "Not enough memory.");
        return;
    }
    for (int i = 0; i < 10; ++i) sw(profile::kCureName + i, hw->name[i], sizeof hw->name[i]);
    sw(profile::kHelpYou, hw->help, sizeof hw->help);
    char he[16];
    sw(profile::kHealExit, he, sizeof he);
    char* sp = strchr(he, ' ');
    if (sp) *sp = 0;
    snprintf(hw->heal, sizeof hw->heal, "%s", he);
    screen = Screen::Heal;
    plist = PickList{};
    draw_heal(c);
}

void leave_heal(pic::Canvas& c)
{
    delete hw;
    hw = nullptr;
    screen = Screen::Shop;
    draw_shop(c);
}

// The price, then "pay for cure"
void offer_cure(pic::Canvas& c)
{
    char t[96], a[24], b[24];
    sw(profile::kOnlyCost, a, sizeof a);
    sw(profile::kGoldPieces, b, sizeof b);
    snprintf(t, sizeof t, "%s%s%d%s", hw->name[cure_at], a, d->prof->cures.cost[cure_at], b);
    say_item(c, t, 10);
    sw(profile::kPayFor, a, sizeof a);
    ask_yes_no(c, Ask::PayCure, a);
}

void heal_pick(int i, pic::Canvas& c)
{
    cure_at = i;
    const rules::Cure cure = static_cast<rules::Cure>(i);
    if (!rules::needs_cure(*pt->sel(), cure, d->prof->cures)) {
        static const int kNot[rules::kCures] = {profile::kNotBlind, profile::kNotDiseased, -1, -1, -1, -1,
                                                profile::kNotPoisoned, profile::kNotDead, profile::kNotCursed,
                                                profile::kNotStoned};
        if (kNot[i] >= 0) {
            char t[64], nm[20], why[24], q[24];
            pt->sel()->name(nm, sizeof nm);
            sw(kNot[i], why, sizeof why);
            snprintf(t, sizeof t, "%s %s", nm, why);
            say_item(c, t, 10);
            sw(profile::kCastAnyway, q, sizeof q);
            ask_yes_no(c, Ask::CureAnyway, q);
            return;
        }
    }
    offer_cure(c);
}

// ---- Take: coins from the counter --------------------------------------------------
// "Select type of coin " over the kinds on the counter (from row 2, column
// 2: "Platinum 25"), "Select" (and Exit); "How much X will you take? "
// typed (on the keyboard); too heavy -> "Overloaded".

int  take_kind[7];
int  take_n = 0;
int  take_coin = -1;
char take_prompt[48];

void list_coins()
{
    take_n = 0;
    for (int k = 6; k >= 0; --k)
        if (ground->money[k] > 0) take_kind[take_n++] = k;
}

void take_line(int i, char* out, size_t cap)
{
    if (i < 0 || i >= take_n) {
        out[0] = 0;
        return;
    }
    const int k = take_kind[i];
    snprintf(out, cap, "%s %d", d->money[k], ground->money[k]);
}

void draw_take(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    plist.row0 = 2;
    plist.row1 = 9;
    plist.col0 = 2;
    plist.n = take_n;
    char prompt[28], sel[12];
    sw(profile::kCoinType, prompt, sizeof prompt);
    sw(profile::kSelectWord, sel, sizeof sel);
    draw_list(c, prompt, sel);
}

void open_take(pic::Canvas& c)
{
    list_coins();
    if (!take_n) return;
    screen = Screen::Take;
    plist = PickList{};
    draw_take(c);
}

void ask_take(int i, pic::Canvas& c)
{
    if (i < 0 || i >= take_n) return;
    take_coin = take_kind[i];
    char a[16], b[20];
    sw(profile::kHowMuch, a, sizeof a);
    sw(profile::kWillTake, b, sizeof b);
    snprintf(take_prompt, sizeof take_prompt, "%s%s %s", a, d->money[take_coin], b);
    input_engine = true;
    engine_ask = EngineAsk::Coins;
    input_prompt = take_prompt;
    input_max = 5;
    input_len = 0;
    input_buf[0] = 0;
    input_mode = Input::Number;
    draw_input(c);
}

void take_coins(int n, pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    rules::recalc(ch, *names, d->facts);
    if (take_coin >= 0 && n > 0) {
        if (n > ground->money[take_coin]) n = ground->money[take_coin];     // (no more than lies there)
        if (ch.encumbrance() + n > rules::max_load(ch)) {
            draw_take(c);
            note(c, d->w_over);
            return;
        }
        if (n > ground->money[take_coin]) n = ground->money[take_coin];
        ground->money[take_coin] -= n;
        add_coins(ch, take_coin, n);
        rules::recalc(ch, *names, d->facts);
    }
    list_coins();
    if (!take_n) {
        screen = Screen::Shop;
        draw_shop(c);
        return;
    }
    draw_take(c);
}

// ---- Appraise: gems and jewellery --------------------------------------------------
// NAME (1, 1); "You have a fine collection of:" (row 7, colour 15), "3 Gems"
// (row 9), "1 piece of Jewelry" (row 10); "Appraise :   Gems Jewelry Exit".
// One appraised: "The Gem is Valued at N gp." (row 12), "You can : Sell
// Keep" (Sell alone when they can't carry it): Keep makes it an item,
// Sell gives N / 5 platinum (too heavy: the rest on the counter).

int  appraised = 0;                 // the value just found (0: none)
bool appraised_jewel = false;

void draw_appraise(pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    char nm[20], t[48], w1[24];
    ch.name(nm, sizeof nm);
    put(c, nm, 1, 1, 15);
    sw(profile::kCollection, t, sizeof t);
    put(c, t, 1, 7, 15);
    const int gems = ch.money(5), jewels = ch.money(6);
    if (gems) {
        sw(gems == 1 ? profile::kGemWord : profile::kGemsWord, w1, sizeof w1);
        snprintf(t, sizeof t, "%d%s", gems, w1);
        put(c, t, 1, 9, 15);
    }
    if (jewels) {
        sw(jewels == 1 ? profile::kJewelWord : profile::kJewelsWord, w1, sizeof w1);
        snprintf(t, sizeof t, "%d%s", jewels, w1);
        put(c, t, 1, 10, 15);
    }
    char keys[40], prompt[16], g[12], j[12], e[8];
    sw(profile::kAppraisePrompt, prompt, sizeof prompt);
    sw(profile::kGemsKey, g, sizeof g);
    sw(profile::kJewelryKey, j, sizeof j);
    sw(profile::kExitKey, e, sizeof e);
    snprintf(keys, sizeof keys, "%s%s%s", gems ? g : "", jewels ? j : "", e);
    if (appraised) {
        sw(appraised_jewel ? profile::kJewelValued : profile::kGemValued, t, sizeof t);
        char gp[8], line[48];
        sw(profile::kGp, gp, sizeof gp);
        snprintf(line, sizeof line, "%s%d%s", t, appraised, gp);
        put(c, line, 1, 12, 15);
        const bool must_sell = ch.encumbrance() + 1 > rules::max_load(ch) || ch.n_items >= party::kMaxItems;
        sw(profile::kYouCan, prompt, sizeof prompt);
        sw(must_sell ? profile::kSellKey : profile::kSellKeep, keys, sizeof keys);
    }
    text::build(menu, prompt, keys);
    menu.selected = 0;
    show_menu_line(c);
    dirty(0, pic::kScreenH);
}

void open_appraise(pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch) return;
    if (!ch->money(5) && !ch->money(6)) {
        char t[28];
        sw(profile::kNoGems, t, sizeof t);
        note(c, t);
        return;
    }
    appraised = 0;
    screen = Screen::Appraise;
    draw_appraise(c);
}

void appraise_key(char k, pic::Canvas& c);

void appraise_tap(int x, int y, pic::Canvas& c)
{
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y < text::kMenuTapTop) return;
    appraise_key(text::key(menu, text::hit(menu, x / 8)), c);
}

void appraise_key(char k, pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    if (appraised) {
        const bool must_sell = ch.encumbrance() + 1 > rules::max_load(ch) || ch.n_items >= party::kMaxItems;
        if (k == 'K' && !must_sell) {
            uint8_t it[items::kRecordSize] = {};
            it[0x2E] = d->prof->gem_type;
            it[0x31] = appraised_jewel ? d->prof->jewel_word : d->prof->gem_word;
            it[0x37] = 1;                             // weight
            it[0x3A] = static_cast<uint8_t>(appraised);
            it[0x3B] = static_cast<uint8_t>(appraised >> 8);
            rules::add_item(ch, it);
        } else if (k == 'S' || k == 'K') {
            const int plat = appraised / 5;
            const int room = rules::max_load(ch) - ch.encumbrance();
            if (plat > room) {
                add_coins(ch, 4, room > 0 ? room : 0);
                ground->money[4] += plat - (room > 0 ? room : 0);
                char t[44];
                sw(profile::kOverPool, t, sizeof t);
                note(c, t);
            } else {
                add_coins(ch, 4, plat);
            }
        } else {
            return;
        }
        appraised = 0;
        rules::recalc(ch, *names, d->facts);
        if (!ch.money(5) && !ch.money(6)) {
            screen = Screen::Shop;
            draw_shop(c);
            return;
        }
        draw_appraise(c);
        return;
    }
    if (k == 'E') {
        screen = Screen::Shop;
        draw_shop(c);
        return;
    }
    if ((k == 'G' && ch.money(5) > 0) || (k == 'J' && ch.money(6) > 0)) {
        appraised_jewel = k == 'J';
        add_coins(ch, appraised_jewel ? 6 : 5, -1);
        const int r = rng.roll(100, 1);
        appraised = appraised_jewel ? rules::jewel_value(r, rng) : rules::gem_value(r);
        if (appraised <= 0) appraised = 1;
        rules::recalc(ch, *names, d->facts);
        draw_appraise(c);
    }
}

// The answers to "Do you want to go back...", "cast cure anyway", "pay for cure"
bool shop_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what == Ask::LeaveCoins) {
        screen = Screen::Shop;
        if (k == 'Y') {
            text::clear(c, text::kTextArea);
            dirty_rows(17, 22);
            if (pt->count) draw_party(c, 17);
            shop_menu(c);
        } else {
            leave_shop(c, true);
        }
        return true;
    }
    if (what != Ask::CureAnyway && what != Ask::PayCure) return false;
    screen = Screen::Heal;
    if (!hw || cure_at < 0) {
        leave_heal(c);
        return true;
    }
    if (k != 'Y') {
        draw_heal(c);
        return true;
    }
    if (what == Ask::CureAnyway) {
        draw_heal(c);
        offer_cure(c);
        return true;
    }
    party::Character& ch = *pt->sel();
    const int cost = d->prof->cures.cost[cure_at];
    bool paid = false;
    if (cost <= rules::gold_worth(ch)) {
        rules::pay(ch, cost);
        paid = true;
    } else if (cost <= rules::gold_worth(ground->money)) {
        rules::pay(ground->money, cost);
        paid = true;
    }
    draw_heal(c);
    if (!paid) {
        char t[24];
        sw(profile::kNotEnough, t, sizeof t);
        note(c, t);
        return true;
    }
    rules::apply_cure(ch, static_cast<rules::Cure>(cure_at), d->prof->cures, rng);
    rules::recalc(ch, *names, d->facts);
    if (pt->count) {}
    char t[48], nm[20], cured[16];
    ch.name(nm, sizeof nm);
    sw(profile::kCured, cured, sizeof cured);
    snprintf(t, sizeof t, "%s %s", nm, cured);
    say_item(c, t, 10);
    return true;
}

void treasure_take(pic::Canvas& c);
void treasure_detect(pic::Canvas& c);
void open_loot(pic::Canvas& c);

void shop_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y >= text::kMenuTapTop) {
        switch (text::key(menu, text::hit(menu, col))) {
        case 'B':
            screen = Screen::ShopBuy;
            plist = PickList{};
            draw_buy(c);
            break;
        case 'V':
            view_character(c);
            break;
        case 'P':                       // Pool: everyone's coins on the counter
            rules::pool(*pt, ground->money);
            shop_menu(c);
            break;
        case 'T':                       // Take: coins from the counter (the treasure: coins, items)
            if (treasure_on()) treasure_take(c);
            else open_take(c);
            break;
        case 'M':                       // the treasure's "Take: Money Items Exit"
            if (treasure_on()) open_take(c);
            break;
        case 'I':
            if (treasure_on()) open_loot(c);
            break;
        case 'D':                       // the treasure's Detect (Detect Magic memorized)
            if (treasure_on()) treasure_detect(c);
            break;
        case 'A':                       // Appraise: gems and jewellery
            open_appraise(c);
            break;
        case 'H':                       // the temple's Heal
            if (temple) open_heal(c);
            break;
        case 'S':                       // Share: the coins on the counter shared out
            rules::share(*pt, ground->money);
            shop_menu(c);
            break;
        case 'E':
            leave_shop(c);
            break;
        case 0:
            break;
        default:
            error(c, "Not in the engine yet.");
            break;
        }
        return;
    }
    // A character in the party list: they're the one buying
    if (col >= 17 && row >= 4 && row < 4 + pt->count) {
        pt->selected = row - 4;
        draw_party(c, 17);
    }
}

void take_loot(int i, pic::Canvas& c);
void draw_loot(pic::Canvas& c);

// Taps on a list: a line chooses it; the menu line acts on it
void list_tap(int x, int y, pic::Canvas& c)
{
    PickList& l = plist;
    const int row = y / 8, col = x / 8;
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y < text::kMenuTapTop) {
        const int i = l.top + row - l.row0;
        if (row >= l.row0 && row <= l.row1 && i < l.n) {
            if (screen != Screen::CreatePick) l.index = i;     // (Create's line 0 is its heading)
            if (screen == Screen::CreatePick) {
                if (i >= 1) {
                    l.index = i;
                    draw_pick(c);
                }
                return;
            }
            if (screen == Screen::ShopBuy) draw_buy(c);
            else if (screen == Screen::Loot) draw_loot(c);
            else if (screen == Screen::AddList) draw_add_list(c);
            else if (screen == Screen::Heal) draw_heal(c);
            else if (screen == Screen::Take) draw_take(c);
            else draw_items(c);
        }
        return;
    }
    const char k = text::key(menu, text::hit(menu, col));
    if (k == 'N' && l.top + l.rows() < l.n) {
        l.top += l.rows();
        l.index = l.top;
    } else if (k == 'P' && l.top > 0) {
        l.top = l.top > l.rows() ? l.top - l.rows() : 0;
        l.index = l.top;
    } else if (k == 'E' && screen == Screen::CreatePick) {
        end_create(c);
        return;
    } else if (screen == Screen::Heal && (k == 'H' || k == 'E')) {
        if (k == 'H') heal_pick(l.index, c);
        else leave_heal(c);
        return;
    } else if (screen == Screen::Take && (k == 'S' || k == 'E')) {
        if (k == 'S') {
            ask_take(l.index, c);
        } else {
            screen = Screen::Shop;
            draw_shop(c);
        }
        return;
    } else if (k == 'S' && screen == Screen::CreatePick) {
        picked(l.index, c);
        return;
    } else if (k == 'E' && screen != Screen::Loot) {     // (the treasure's Exit: below)
        if (screen == Screen::AddList) {
            screen = Screen::PartyMenu;
            draw_party_menu(c);
        } else if (screen == Screen::ShopBuy) {
            screen = Screen::Shop;
            draw_shop(c);
        } else if (items_direct) {
            items_direct = false;
            fight_after_view(c);
        } else {
            screen = Screen::View;
            draw_character(c);
        }
        return;
    } else if (k == 'B' && screen == Screen::ShopBuy) {
        buy(l.index, c);
        return;
    } else if (screen == Screen::Loot && (k == 'T' || k == 'E')) {
        if (k == 'T') take_loot(l.index, c);
        else {
            screen = Screen::Shop;
            draw_shop(c);
        }
        return;
    } else if (screen == Screen::Items && l.index < pt->sel()->n_items &&
               (k == 'R' || k == 'U' || k == 'T' || k == 'D' || k == 'H' || k == 'J' || k == 'S' || k == 'I')) {
        switch (k) {
        case 'R': ready_item(l.index, c); break;
        case 'U': use_item(l.index, c); break;
        case 'T': trade_item(l.index, c); break;
        case 'D': drop_item(l.index, c); break;
        case 'H': halve_item(l.index, c); break;
        case 'J': join_item(l.index, c); break;
        case 'S': sell_item(l.index, c); break;
        case 'I': identify_item(l.index, c); break;
        }
        return;
    } else if (k == 'A' && screen == Screen::AddList) {
        add_character(l.index, c);
        return;
    } else {
        return;
    }
    if (screen == Screen::ShopBuy) draw_buy(c);
    else if (screen == Screen::Loot) draw_loot(c);
    else if (screen == Screen::AddList) draw_add_list(c);
    else if (screen == Screen::CreatePick) draw_pick(c);
    else if (screen == Screen::Heal) draw_heal(c);
    else if (screen == Screen::Take) draw_take(c);
    else draw_items(c);
}

void draw_party_menu(pic::Canvas& c);
void back_from_view(pic::Canvas& c);

// ---- Magic (the camp's): Memorize, Rest (Cast and Display below) --------------------
// The magic menu "Cast Memorize Scribe Display Rest Exit" under the camp
// screen. Memorize: "NAME's Spells in Grimoire" (row 1), the spells they
// know by level ("1st Level" headings) from row 5 to 15, "NAME can
// memorize:" with a row of counts a kind (rows 19-22), "Choose Spell:
// Memorize Exit"; at the end "NAME's Spells to Memorize" and "Memorize
// these spells? Yes No" (No forgets them). Rest: "Rest Time: 00:04:15"
// (row 17; the unit being set in colour 15), "Rest Days Hours Mins Add
// Subtract Exit" - the time the spells need, changed in days, hours or 5
// minutes; resting runs five minutes a step: a day heals everyone a point
// ("The Whole Party Is Healed"), "NAME has memorized SPELL", an encounter
// can break in ("Your repose is suddenly interrupted!": the camp ends and
// the area's camp-interrupted script runs); a tap asks "Stop Resting?".


Making* mrules = nullptr;           // the rule tables while camping
struct SpellNames {
    char name[101][24];
};
SpellNames* spn = nullptr;

void mw(int i, char* out, size_t cap)
{
    out[0] = 0;
    const uint32_t at = d->prof->magic.words[i];
    fs::File f;
    if (!at || !open_file(d->prof->overlay, f)) return;
    library::FileSource src(f);
    text::read_pascal(src, at, out, cap);
    f.close();
}

void end_effects();

void end_magic()
{
    end_effects();
    delete mrules;
    mrules = nullptr;
    delete spn;
    spn = nullptr;
}

bool load_magic();

// Items that change class values as they're readied / put away
// (curse_finish_facts.md 5): the Ring of Wizardry's spell slots, the
// thief-skill items
void item_class_values(party::Character& ch, int i)
{
    if (i < 0 || i >= ch.n_items || ch.items[i][0x3E] < 0x80) return;
    const int code = ch.items[i][0x3E] & 0x7F;
    if (code != 1 && code != 2 && code != 11) return;
    const bool had = mrules != nullptr;
    if (!load_magic()) return;
    if (code == 1) classes::wizardry(ch, mrules->tables, ch.items[i][0x34] != 0);
    else if (ch.level(classes::Thief) > 0) classes::thief_skills(ch, mrules->tables);
    if (!had) end_magic();              // (loaded for this: the tables don't stay)
}

// The rule tables and spell names (from the program), for the camp
bool load_magic()
{
    if (!mrules && !load_rules(mrules)) {
        end_magic();
        return false;
    }
    if (spn) return true;
    spn = new (std::nothrow) SpellNames;
    if (!spn) return false;
    memset(spn, 0, sizeof *spn);
    const profile::NameTable& nt = d->prof->magic.names;
    fs::File f;
    if (!nt.at || !open_file(d->prof->program, f)) return true;
    library::FileSource src(f);
    exepack::Info info;
    if (exepack::parse(src, info) == exepack::Status::Ok) {
        const size_t n = static_cast<size_t>(nt.stride) * nt.count;
        uint8_t* buf = static_cast<uint8_t*>(malloc(n));
        if (buf && exepack::read(src, info, nt.at, buf, n) == exepack::Status::Ok)
            for (int i = 0; i < nt.count && i < 101; ++i) {
                const uint8_t* e = buf + static_cast<size_t>(i) * nt.stride;
                const int len = e[0] < 23 ? e[0] : 23;
                memcpy(spn->name[i], e + 1, len);
                spn->name[i][len] = 0;
            }
        free(buf);
    }
    f.close();
    return true;
}

const char* spell_name(int s) { return spn && s >= 0 && s <= 100 ? spn->name[s] : ""; }

void draw_magic_menu(pic::Canvas& c)
{
    text::build(menu, "", d->w_magic);
    menu.selected = 0;
    show_menu_line(c);
}

void open_magic(pic::Canvas& c)
{
    if (!load_magic()) {
        error(c, "Not enough memory.");
        return;
    }
    screen = Screen::Magic;
    draw_magic_menu(c);
}

void back_to_magic(pic::Canvas& c)
{
    screen = Screen::Magic;
    draw_camp(c);
    draw_magic_menu(c);
}

// A character's words on the text rows: "NAME text" (rows 19-20, colour 10)
void say_status(pic::Canvas& c, const char* what, int row = 19, uint8_t colour = 10)
{
    char nm[20], t[64];
    pt->sel()->name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s %s", nm, what);
    c.fill(8, row * 8, 38 * 8, 8, 0);
    put(c, t, 1, row, colour);
    dirty_rows(row, row);
}

void cw(int i, char* out, size_t cap);
void cast_done(pic::Canvas& c);
void open_cast_exploring(pic::Canvas& c);
void choose_spell(int spell, pic::Canvas& c);
void open_cast(pic::Canvas& c, bool exploring = false);
void open_effects(pic::Canvas& c);

// ---- the spell list
struct SpellLines {
    uint8_t id[60];                 // 0: a level heading (level in lvl)
    uint8_t lvl[60];
    int     n = 0;
    int     top = 0, sel = -1;
    bool    learning = false;       // "to Memorize" (else the grimoire)
    bool    casting = false;        // "in Memory" (Cast)
    bool    scrolls = false;        // scrolls' spells: "on Scrolls" (Scribe) / "to Scribe" (with learning)
    bool    choosing = false;       // training's new spell: "to Choose", Learn (no Exit)
    bool    fighting = false;       // "in Memory" in a fight: the chosen one is cast there
    int     reading = -1;           // a scroll's spells (the item): "on Scroll", Cast reads one (Use)
} sl;

// The scrolls' facts for the magic rules
magic::Scrolls scroll_facts()
{
    magic::Scrolls sc;
    sc.names = names;
    sc.one_spell = d->prof->magic.scroll_one_spell;
    sc.read_magic = d->prof->magic.read_magic;
    return sc;
}

void build_lines(const uint8_t* ids, int n)
{
    sl.n = 0;
    int last = 0;
    const classes::Tables& t = mrules->tables;
    for (int i = 0; i < n && sl.n < 59; ++i) {
        const int lv = t.spell_level(ids[i]);
        if (lv != last) {
            sl.id[sl.n] = 0;
            sl.lvl[sl.n++] = static_cast<uint8_t>(lv);
            last = lv;
        }
        sl.id[sl.n] = ids[i];
        sl.lvl[sl.n++] = static_cast<uint8_t>(lv);
    }
    sl.top = 0;
    sl.sel = -1;
    sl.reading = -1;
    for (int i = 0; i < sl.n; ++i)
        if (sl.id[i]) {
            sl.sel = i;
            break;
        }
}

int list_rows()
{
    return sl.learning || sl.casting || sl.scrolls || sl.choosing || sl.reading >= 0 ? 22 - 5 + 1 : 15 - 5 + 1;
}

// "NAME can memorize:" and the counts a kind (cleric, druid, magic-user)
void draw_counts(pic::Canvas& c)
{
    const party::Character& ch = *pt->sel();
    const classes::Tables& t = mrules->tables;
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    char w1[24];
    mw(profile::kCanMemorize, w1, sizeof w1);
    say_status(c, w1, 19);
    int row = 20;
    static const int kKind[3] = {profile::kClericSpells, profile::kDruidSpells, profile::kMuSpells};
    for (int k = 0; k < 3; ++k) {
        bool any = false;
        for (int lv = 1; lv <= 5; ++lv)
            if (ch.rec[0x12D + k * 5 + lv - 1]) any = true;
        if (!any || row > 22) continue;
        mw(kKind[k], w1, sizeof w1);
        put(c, w1, 1, row, 10);
        for (int lv = 1; lv <= 5; ++lv) {
            if (!ch.rec[0x12D + k * 5 + lv - 1]) continue;
            char n[4];
            snprintf(n, sizeof n, "%d", magic::room(ch, t, k, lv));
            put(c, n, 20 + (lv - 1) * 3, row, 10);
        }
        ++row;
    }
    dirty_rows(17, 22);
}

void draw_spells(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    char nm[20], t[48], a[12], b[16];
    pt->sel()->name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s", nm, d->w_s);
    put(c, t, 1, 1, pt->sel()->npc() ? 10 : 11);
    mw(profile::kSpellsWord, a, sizeof a);
    if (sl.choosing) cw(profile::kToChoose, b, sizeof b);
    else if (sl.reading >= 0) cw(profile::kOnScroll, b, sizeof b);
    else if (sl.scrolls) cw(sl.learning ? profile::kToScribe : profile::kOnScrolls, b, sizeof b);
    else mw(sl.learning ? profile::kToMemorize : sl.casting ? profile::kInMemory : profile::kInGrimoire, b, sizeof b);
    snprintf(t, sizeof t, "%s%s", a, b);
    put(c, t, static_cast<int>(strlen(nm)) + 4, 1, 10);
    const int rows = list_rows();
    for (int r = 0; r < rows && sl.top + r < sl.n; ++r) {
        const int i = sl.top + r;
        const int row = 5 + r;
        if (!sl.id[i]) {
            put(c, d->w_level[sl.lvl[i] - 1], 1, row, 13);       // headings in the prompt colour
            continue;
        }
        char line[32];
        snprintf(line, sizeof line, "  %s", spell_name(sl.id[i]));
        if (i == sl.sel && !sl.learning) {
            c.fill(3 * 8, row * 8, static_cast<int>(strlen(line) - 2) * 8, 8, 15);
            font::draw_text(c, d->font, line + 2, 3, row, 0, -1);
        } else {
            put(c, line, 1, row, 10);
        }
    }
    if (!sl.learning && !sl.casting && !sl.scrolls && !sl.choosing && sl.reading < 0) draw_counts(c);
    // The menu
    char keys[40], prompt[20], k1[12];
    keys[0] = 0;
    prompt[0] = 0;
    if (!sl.learning) {
        mw(profile::kChooseSpell, prompt, sizeof prompt);
        if (sl.casting || sl.reading >= 0) cw(profile::kCastKey, k1, sizeof k1);
        else if (sl.scrolls) cw(profile::kScribeKey, k1, sizeof k1);
        else if (sl.choosing) cw(profile::kLearnKey, k1, sizeof k1);
        else mw(profile::kMemorizeKey, k1, sizeof k1);
        snprintf(keys, sizeof keys, "%s%s%s%s", k1, sl.top + rows < sl.n ? d->w_next : "", sl.top > 0 ? d->w_prev : "",
                 sl.choosing ? "" : d->w_exit);
        text::build(menu, prompt, keys);
        menu.selected = 0;
        show_menu_line(c);
    }
    dirty(0, pic::kScreenH);
}

// "Spells to Memorize" and the question
void confirm_memorize(pic::Canvas& c, int word)
{
    uint8_t ids[84];
    const int n = magic::in_memory(*pt->sel(), mrules->tables, true, ids, 84);
    sl.learning = true;
    sl.casting = false;
    sl.scrolls = false;
    sl.choosing = false;
    sl.fighting = false;
    build_lines(ids, n);
    screen = Screen::SpellList;
    draw_spells(c);
    char q[28];
    mw(word, q, sizeof q);
    ask_yes_no(c, Ask::MemorizeThese, q);
}

void show_grimoire(pic::Canvas& c)
{
    uint8_t ids[100];
    const int n = magic::known(*pt->sel(), mrules->tables, ids, 100);
    sl.learning = false;
    sl.casting = false;
    sl.scrolls = false;
    sl.choosing = false;
    sl.fighting = false;
    build_lines(ids, n);
    screen = Screen::SpellList;
    draw_spells(c);
}

// ---- Scribe: "NAME's Spells on Scrolls" (the scrolls they can read; rows
// 5-22), "Choose Spell: Scribe Exit"; a spell known already, being scribed
// already or one they can't learn: "You already know that spell", "You
// are already scibing that spell", "You can not scribe that spell." on
// the menu line; leaving: "Spells to Scribe" and "Scribe these spells? Yes
// No" (No forgets them; already scribing when it opens: "Scribe These
// Spells?" first). The rest scribes them before memorizing ("NAME has
// scribed SPELL"): into the spell book, off the scroll (used up: gone).
void show_scrolls(pic::Canvas& c);

void confirm_scribe(pic::Canvas& c, int word, Ask what)
{
    uint8_t ids[48];
    const int n = magic::scroll_spells(*pt->sel(), mrules->tables, scroll_facts(), true, ids, 48);
    sl.learning = true;
    sl.casting = false;
    sl.scrolls = true;
    sl.choosing = false;
    sl.fighting = false;
    build_lines(ids, n);
    screen = Screen::SpellList;
    draw_spells(c);
    char q[28];
    cw(word, q, sizeof q);
    ask_yes_no(c, what, q);
}

void show_scrolls(pic::Canvas& c)
{
    uint8_t ids[48];
    const int n = magic::scroll_spells(*pt->sel(), mrules->tables, scroll_facts(), false, ids, 48);
    if (n == 0) {
        back_to_magic(c);
        char t[32];
        cw(profile::kNoCopyable, t, sizeof t);
        say_status(c, t);
        return;
    }
    const int keep = sl.scrolls && !sl.learning && sl.sel >= 0 ? sl.id[sl.sel] : 0;
    sl.learning = false;
    sl.casting = false;
    sl.scrolls = true;
    sl.choosing = false;
    sl.fighting = false;
    build_lines(ids, n);
    for (int i = 0; keep && i < sl.n; ++i)
        if (sl.id[i] == keep) {
            sl.sel = i;
            while (sl.sel >= sl.top + list_rows()) sl.top += list_rows();
            break;
        }
    screen = Screen::SpellList;
    draw_spells(c);
}

void open_scribe(pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    if (ch.health() == party::Animated || !ch.in_combat()) {
        char a[28], b[24], t[52];
        mw(profile::kNoCondition, a, sizeof a);
        cw(profile::kScribeAny, b, sizeof b);
        snprintf(t, sizeof t, "%s%s", a, b);
        say_status(c, t);
        return;
    }
    sl.scrolls = false;
    sl.choosing = false;
    sl.fighting = false;
    if (magic::scribing(ch, scroll_facts())) {
        confirm_scribe(c, profile::kScribeThese, Ask::ScribeThese);
        return;
    }
    show_scrolls(c);
}

// ---- Training's new spell: "NAME's Spells to Choose" (rows 5-22),
// "Choose Spell: Learn" (no Exit: one must be chosen), then
// "Congratulations..."
void train_note(int word, pic::Canvas& c);

bool open_learn(pic::Canvas& c)
{
    if (!load_magic()) {
        end_magic();
        return false;
    }
    uint8_t ids[60];
    const int n = magic::learnable(*pt->sel(), mrules->tables, ids, 60);
    if (n == 0) {
        end_magic();
        return false;
    }
    sl.learning = sl.casting = sl.scrolls = false;
    sl.choosing = true;
    sl.fighting = false;
    build_lines(ids, n);
    screen = Screen::SpellList;
    draw_spells(c);
    return true;
}

void learn_tap(char k, pic::Canvas& c)
{
    if (k == 'L' && sl.sel >= 0) {
        magic::learn(*pt->sel(), sl.id[sl.sel]);
        sl.choosing = false;
    sl.fighting = false;
        end_magic();
        screen = Screen::PartyMenu;
        train_note(profile::kCongrats, c);
        return;
    }
    if (k == 'N' && sl.top + list_rows() < sl.n) sl.top += list_rows();
    else if (k == 'P' && sl.top > 0) sl.top = sl.top > list_rows() ? sl.top - list_rows() : 0;
    else return;
    draw_spells(c);
}

void scribe_tap(char k, pic::Canvas& c)
{
    if (k == 'S' && sl.sel >= 0) {
        char t[40];
        switch (magic::scribe(*pt->sel(), mrules->tables, scroll_facts(), sl.id[sl.sel])) {
        case magic::Scribe::Ok: return;
        case magic::Scribe::Known: cw(profile::kAlreadyKnow, t, sizeof t); break;
        case magic::Scribe::Already: cw(profile::kAlreadyScribing, t, sizeof t); break;
        case magic::Scribe::Cannot: cw(profile::kCannotScribe, t, sizeof t); break;
        }
        note(c, t);
        return;
    }
    if (k == 'E') {
        if (magic::scribing(*pt->sel(), scroll_facts())) confirm_scribe(c, profile::kScribeThese2, Ask::ScribeThese2);
        else back_to_magic(c);
        return;
    }
    if (k == 'N' && sl.top + list_rows() < sl.n) sl.top += list_rows();
    else if (k == 'P' && sl.top > 0) sl.top = sl.top > list_rows() ? sl.top - list_rows() : 0;
    else return;
    draw_spells(c);
}

void memorize(pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    if (ch.health() == party::Animated || !ch.in_combat()) {
        char a[28], b[20], t[48];
        mw(profile::kNoCondition, a, sizeof a);
        mw(profile::kMemorizeSpells, b, sizeof b);
        snprintf(t, sizeof t, "%s%s", a, b);
        say_status(c, t);
        return;
    }
    if (magic::memorizing(ch)) {
        confirm_memorize(c, profile::kMemorizeThese);
        return;
    }
    if (!magic::any_room(ch, mrules->tables)) {
        char t[32];
        mw(profile::kCannotMemorize, t, sizeof t);
        say_status(c, t);
        return;
    }
    show_grimoire(c);
}

void spells_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8;
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y < text::kMenuTapTop) {
        const int i = sl.top + row - 5;
        if (row >= 5 && row < 5 + list_rows() && i < sl.n && sl.id[i]) {
            sl.sel = i;
            draw_spells(c);
        }
        return;
    }
    const char k = text::key(menu, text::hit(menu, x / 8));
    if (sl.reading >= 0) {
        reading_tap(k, c);
        return;
    }
    if (sl.fighting) {
        if (k == 'C' && sl.sel >= 0) fight_spell_chosen(sl.id[sl.sel], c);
        else if (k == 'E') fight_spell_chosen(0, c);
        else if (k == 'N' && sl.top + list_rows() < sl.n) sl.top += list_rows();
        else if (k == 'P' && sl.top > 0) sl.top = sl.top > list_rows() ? sl.top - list_rows() : 0;
        else return;
        if (screen == Screen::SpellList) draw_spells(c);
        return;
    }
    if (sl.choosing) {
        learn_tap(k, c);
        return;
    }
    if (sl.scrolls) {
        scribe_tap(k, c);
        return;
    }
    if (sl.casting) {
        if (k == 'C' && sl.sel >= 0) choose_spell(sl.id[sl.sel], c);
        else if (k == 'E') cast_done(c);
        else if (k == 'N' && sl.top + list_rows() < sl.n) sl.top += list_rows();
        else if (k == 'P' && sl.top > 0) sl.top = sl.top > list_rows() ? sl.top - list_rows() : 0;
        else return;
        if (screen == Screen::SpellList && k != 'C') draw_spells(c);
        return;
    }
    if (k == 'M' && sl.sel >= 0) {
        magic::add(*pt->sel(), mrules->tables, sl.id[sl.sel]);
        draw_counts(c);
        draw_spells(c);
    } else if (k == 'N' && sl.top + list_rows() < sl.n) {
        sl.top += list_rows();
        draw_spells(c);
    } else if (k == 'P' && sl.top > 0) {
        sl.top = sl.top > list_rows() ? sl.top - list_rows() : 0;
        draw_spells(c);
    } else if (k == 'E') {
        if (magic::memorizing(*pt->sel())) confirm_memorize(c, profile::kMemorizeThese2);
        else back_to_magic(c);
    }
}

// ---- Rest
struct RestRun {
    int  left = 0;                  // minutes still to rest
    int  unit = 2;                  // being set: 2 minutes, 3 hours, 4 days
    bool running = false, from_magic = false, interrupted = false;
    bool fix = false;               // Fix's rest: `fix_heal` shared out at its end
    int  fix_heal = 0;
    uint32_t pause_until = 0;       // a message stays a moment
    int  shown = 0;                 // steps since the time was shown
    magic::Rest r;
} rest;
// Rest steps since the last encounter check: from rest to rest, area to
// area - the original's counter starts at 0 only when the program starts
// (coab's facts: ovr021 resting, rest_incounter_count)
int rest_encounter_steps = 0;

void draw_rest_time(pic::Canvas& c)
{
    char w1[16], t[12];
    c.fill(8, 17 * 8, 38 * 8, 8, 0);
    mw(profile::kRestTime, w1, sizeof w1);
    put(c, w1, 1, 17, 10);
    const int days = rest.left / 1440, hours = rest.left / 60 % 24, mins = rest.left % 60;
    const int v[3] = {days, hours, mins};
    for (int i = 0; i < 3; ++i) {
        snprintf(t, sizeof t, "%02d", v[i]);
        const int unit = 4 - i;
        put(c, t, 12 + i * 3, 17, !rest.running && unit == rest.unit ? 15 : 10);
        if (i < 2) put(c, ":", 14 + i * 3, 17, 10);
    }
    dirty_rows(17, 17);
}

void rest_menu(pic::Canvas& c)
{
    char words[48];
    mw(profile::kRestMenu, words, sizeof words);
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

void open_rest(pic::Canvas& c, bool from_magic)
{
    if (!load_magic()) {
        error(c, "Not enough memory.");
        return;
    }
    int most = 0;
    for (int i = 0; i < pt->count; ++i) {
        const int m = magic::rest_minutes(pt->m[i], mrules->tables, scroll_facts());
        if (m > most) most = m;
    }
    rest = RestRun{};
    rest.left = most;
    rest.from_magic = from_magic;
    screen = Screen::Rest;
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    draw_rest_time(c);
    rest_menu(c);
}

void end_rest(pic::Canvas& c)
{
    rest.running = false;
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    if (rest.interrupted) {
        // The camp breaks up; the area's camp-interrupted script runs
        rest.interrupted = false;
        camp_from_script = false;       // (the camp-interrupted script takes over: the camp's script isn't resumed too)
        leave_camp(c);
        run_entry(3, Then::Idle);
        return;
    }
    if (rest.from_magic) back_to_magic(c);
    else {
        screen = Screen::Camp;
        draw_camp(c);
    }
}

void rest_tap(int x, int y, pic::Canvas& c)
{
    if (rest.running) {
        // A tap asks to stop (the games: a key)
        char q[20];
        mw(profile::kStopResting, q, sizeof q);
        ask_yes_no(c, Ask::StopRest, q);
        return;
    }
    if (y < text::kMenuTapTop) return;
    const char k = text::key(menu, text::hit(menu, x / 8));
    switch (k) {
    case 'R':
        if (rest.left <= 0) break;
        rest.running = true;
        magic::begin(rest.r);
        clear_menu_line(c);
        draw_rest_time(c);
        break;
    case 'D': rest.unit = 4; break;
    case 'H': rest.unit = 3; break;
    case 'M': rest.unit = 2; break;
    case 'A': rest.left += rest.unit == 4 ? 1440 : rest.unit == 3 ? 60 : 5; break;
    case 'S':
        rest.left -= rest.unit == 4 ? 1440 : rest.unit == 3 ? 60 : 5;
        if (rest.left < 0) rest.left = 0;
        break;
    case 'E': end_rest(c); return;
    default: return;
    }
    if (rest.left > 99 * 1440) rest.left = 99 * 1440;
    if (screen == Screen::Rest) draw_rest_time(c);
}

// Resting: a few steps each tick, the messages as they come
void rest_tick(uint32_t now, pic::Canvas& c)
{
    if (!rest.running || screen != Screen::Rest) return;
    if (rest.pause_until && static_cast<int32_t>(now - rest.pause_until) < 0) return;
    if (rest.pause_until) {
        rest.pause_until = 0;
        c.fill(8, 18 * 8, 38 * 8, 4 * 8, 0);
        dirty_rows(18, 21);
        if (rest.interrupted) {
            end_rest(c);
            return;
        }
    }
    int speed = vm->get(0x4BFC) & 0xFF;
    if (speed == 0) speed = 4;
    const uint32_t delay = static_cast<uint32_t>(speed) * 300;
    for (int k = 0; k < 6 && rest.left > 0; ++k) {
        rest.left -= 5;
        if (rest.left < 0) rest.left = 0;
        vm->advance_clock(1, 5);
        vm->take_minutes();                 // (the rest's own clock runs the effects)
        const magic::Step st = magic::step(rest.r, *pt, mrules->tables, scroll_facts(), false);
        effects_clock(5, c);
        bool said = false;
        if (st.healed) {
            char t[32];
            mw(profile::kHealedAll, t, sizeof t);
            put(c, t, 1, 19, 10);
            draw_party(c, 17);
            said = true;
        }
        for (int i = 0; i < pt->count && !said; ++i)
            if (st.scribed[i]) {
                char nm[20], w1[20], t[64];
                pt->m[i].name(nm, sizeof nm);
                cw(profile::kHasScribed, w1, sizeof w1);
                snprintf(t, sizeof t, "%s %s %s", nm, w1, spell_name(st.scribed[i]));
                put(c, t, 1, 19, 10);
                said = true;
            }
        for (int i = 0; i < pt->count && !said; ++i)
            if (st.learnt[i]) {
                char nm[20], w1[20], t[64];
                pt->m[i].name(nm, sizeof nm);
                mw(profile::kHasMemorized, w1, sizeof w1);
                snprintf(t, sizeof t, "%s %s %s", nm, w1, spell_name(st.learnt[i]));
                put(c, t, 1, 19, 10);
                said = true;
            }
        // An encounter can break in
        const int period = vm->get(d->prof->magic.rest_period), chance = vm->get(d->prof->magic.rest_chance);
        if (period > 0 && ++rest_encounter_steps >= period) {
            rest_encounter_steps = 0;
            if (rng.roll(100, 1) <= chance) {
                char t[40];
                mw(profile::kInterrupted, t, sizeof t);
                c.fill(8, 19 * 8, 38 * 8, 8, 0);
                put(c, t, 1, 19, 15);
                rest.interrupted = true;
                said = true;
            }
        }
        if (said) {
            dirty_rows(17, 22);
            draw_rest_time(c);
            rest.pause_until = now + delay;
            if (!rest.pause_until) rest.pause_until = 1;
            return;
        }
    }
    draw_rest_time(c);
    draw_position(c);
    if (rest.left <= 0) {
        rest.running = false;
        if (rest.fix) {
            spells::fix_heal(*pt, rest.fix_heal);
            draw_party(c, 17);
        }
        end_rest(c);
    }
}

// Fix (the camp's): the healers' rest, then their healing shared out (the
// time as the games work it out; resting as Rest does - an encounter or
// stopping early: no healing)
void fix_party(pic::Canvas& c)
{
    if (spells::hp_lost(*pt) <= 0) return;
    if (!load_magic()) {
        error(c, "Not enough memory.");
        return;
    }
    const auto& m = d->prof->magic;
    const spells::FixPlan f = spells::fix_plan(*pt, mrules->tables, m.camp, m.n_camp, rng);
    if (f.minutes <= 0) return;
    rest = RestRun{};
    rest.left = f.minutes;
    rest.fix = true;
    rest.fix_heal = f.heal;
    screen = Screen::Rest;
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    rest.running = true;
    magic::begin(rest.r);
    clear_menu_line(c);
    draw_rest_time(c);
}

void show_memory(pic::Canvas& c);
struct CastRun;
void lose_spell(bool yes, pic::Canvas& c);
bool use_it_answer(char k, pic::Canvas& c);

bool magic_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what == Ask::LoseIt) {
        lose_spell(k == 'Y', c);
        return true;
    }
    if (what == Ask::UseIt) return use_it_answer(k, c);
    if (what == Ask::ScribeThese || what == Ask::ScribeThese2) {
        if (k != 'Y') magic::cancel_scribes(*pt->sel(), scroll_facts());
        if (k != 'Y' && what == Ask::ScribeThese) show_scrolls(c);
        else back_to_magic(c);
        return true;
    }
    if (what == Ask::MemorizeThese) {
        if (k != 'Y') magic::cancel(*pt->sel());
        back_to_magic(c);
        return true;
    }
    if (what == Ask::StopRest) {
        screen = Screen::Rest;
        if (k == 'Y') {
            rest.running = false;
            end_rest(c);
        } else {
            clear_menu_line(c);
        }
        return true;
    }
    return false;
}

void magic_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y >= text::kMenuTapTop) {
        switch (text::key(menu, text::hit(menu, col))) {
        case 'C': open_cast(c); break;
        case 'M': memorize(c); break;
        case 'S': open_scribe(c); break;
        case 'D': open_effects(c); break;
        case 'R': open_rest(c, true); break;
        case 'E':
            screen = Screen::Camp;
            draw_camp(c);
            break;
        case 0: break;
        default: error(c, "Not in the engine yet."); break;
        }
        return;
    }
    if (col >= 17 && row >= 4 && row < 4 + pt->count && bigpic < 0) {
        pt->selected = row - 4;
        draw_party(c, 17);
    }
}

// ---- Cast and Display (the camp's) ------------------------------------------------
// Cast: "NAME's Spells in Memory" (rows 5-22), "Choose Spell: Cast Exit".
// A spell for fights: its name (row 19), "can't be cast here..." (row 20),
// "Lose it? Yes No". Else "NAME casts" / the spell's name for a moment; a
// spell for one member asks "Cast Spell on whom Select Exit" over the camp
// screen (the party list); the spell leaves the caster's memory and what
// it did is said a line at a time ("NAME is Blessed", "NAME is fully
// healed"), then the list again ("has no spells memorized" when it's
// empty). Display: each member's name and their spell effects ("
// <No Spell Effects>"), a page of rows 4-22 at a time.

void cw(int i, char* out, size_t cap)
{
    out[0] = 0;
    const uint32_t at = d->prof->magic.cast_words[i];
    fs::File f;
    if (!at || !open_file(d->prof->overlay, f)) return;
    library::FileSource src(f);
    text::read_pascal(src, at, out, cap);
    f.close();
}

// A GAME.OVR word by its offset
void ow(uint32_t at, char* out, size_t cap)
{
    out[0] = 0;
    fs::File f;
    if (!at || !open_file(d->prof->overlay, f)) return;
    library::FileSource src(f);
    text::read_pascal(src, at, out, cap);
    f.close();
}

uint32_t game_delay_ms()
{
    int speed = vm->get(0x4BFC) & 0xFF;
    if (speed == 0) speed = 4;
    return static_cast<uint32_t>(speed) * 100;      // the games' delay: speed x 100 ms
}

struct CastRun {
    enum Stage : uint8_t { None, Casts, Whom, Saying, Flame, FlameAbort } stage = None;
    int  spell = 0, caster = 0, target = 0;
    int  flame = 0;                 // Fire Shield: 1 hot, 2 cold
    const spells::CampSpell* cs = nullptr;
    spells::Line lines[24];
    int  n = 0, at = 0;
    uint32_t until = 0;
    bool on_camp = false;           // the camp screen is shown (Whom)
    bool exploring = false;         // cast from the exploring menu (not the camp's)
    int  item = -1;                 // Use: the item it comes from (its charge goes, not the memory)
    bool scroll = false;            // ... a scroll (the spell goes off it)
} cr;
void item_cast_done(pic::Canvas& c);

// The list closed: back to the magic menu, or to exploring
void cast_done(pic::Canvas& c)
{
    if (!cr.exploring) {
        back_to_magic(c);
        return;
    }
    cr.exploring = false;
    end_magic();
    screen = Screen::Game;
    anim_stop();
    bigpic = -1;
    pic_shown = false;
    head_shown = body_shown = -1;
    draw_frame(c);
    draw_view(c);
    draw_panel(c);
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    idle_menu(c);
}

// The exploring menu's Cast (the same spells, then back to exploring)
void open_cast_exploring(pic::Canvas& c)
{
    if (!pt->sel() || pt->sel()->health() != party::Okay) return;
    if (!load_magic()) {
        end_magic();
        error(c, "Not enough memory.");
        return;
    }
    open_cast(c, true);
    if (screen != Screen::SpellList) end_magic();       // (no list: nothing left open)
}

const spells::CampSpell* camp_spell(int s)
{
    const auto& m = d->prof->magic;
    for (int i = 0; i < m.n_camp; ++i)
        if (m.camp[i].spell == s) return &m.camp[i];
    return nullptr;
}

// Clears the text rows under the list / camp view and says "NAME text"
void say_line(pic::Canvas& c, int who, const char* text)
{
    char nm[20], t[64];
    pt->m[who].name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s %s", nm, text);
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    put(c, t, 1, 19, 10);
    dirty_rows(17, 22);
}

// The spells in memory (again after a spell); none: back to the menu
void show_memory(pic::Canvas& c)
{
    if (cr.item >= 0) {
        item_cast_done(c);
        return;
    }
    cr.stage = CastRun::None;
    if (cr.caster >= 0 && cr.caster < pt->count) pt->selected = cr.caster;
    uint8_t ids[84];
    const int n = magic::in_memory(*pt->sel(), mrules->tables, false, ids, 84);
    if (n == 0) {
        cast_done(c);
        char t[32];
        cw(profile::kNoSpells, t, sizeof t);
        say_status(c, t);
        return;
    }
    const int keep = sl.casting && sl.sel >= 0 ? sl.id[sl.sel] : 0;
    sl.learning = false;
    sl.casting = true;
    sl.scrolls = false;
    sl.choosing = false;
    sl.fighting = false;
    build_lines(ids, n);
    for (int i = 0; keep && i < sl.n; ++i)
        if (sl.id[i] == keep) {
            sl.sel = i;
            while (sl.sel >= sl.top + list_rows()) sl.top += list_rows();
            break;
        }
    screen = Screen::SpellList;
    draw_spells(c);
}

void open_cast(pic::Canvas& c, bool exploring)
{
    party::Character& ch = *pt->sel();
    if (!spells::can_cast(ch)) {
        char a[28], b[20], t[48];
        mw(profile::kNoCondition, a, sizeof a);
        cw(profile::kCastAny, b, sizeof b);
        snprintf(t, sizeof t, "%s%s", a, b);
        say_status(c, t);
        return;
    }
    cr = CastRun{};
    cr.caster = pt->selected;
    cr.exploring = exploring;           // (none in memory: back to exploring, not the camp)
    sl.casting = false;
    show_memory(c);
}

void draw_whom(pic::Canvas& c)
{
    // The camp screen with the party list; with a big picture up, the list
    // on its own
    if (bigpic < 0) {
        cr.on_camp = true;
        draw_frame(c);
        draw_view(c);
        draw_panel(c);
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
    } else {
        cr.on_camp = false;
        c.clear(0);
        layout::outer(c, d->tables, d->frame_tiles);
        draw_party(c, 1);
        dirty(0, pic::kScreenH);
    }
    char prompt[28], keys[24];
    cw(profile::kCastOnWhom, prompt, sizeof prompt - 1);
    strcat(prompt, " ");
    snprintf(keys, sizeof keys, "%s%s", iw(profile::kSelect), d->w_exit);
    text::build(menu, prompt, keys);
    menu.selected = 0;
    show_menu_line(c);
}

void next_line(pic::Canvas& c);

void do_cast(pic::Canvas& c)
{
    party::Character& me = pt->m[cr.caster];
    int pw = 0;
    if (cr.item < 0) magic::remove(me, cr.spell);
    else pw = spells::item_power(me, mrules->tables, cr.spell);
    cr.n = spells::cast(*pt, cr.caster, cr.target, *cr.cs, mrules->tables, d->prof->cures, d->prof->magic.facts, rng,
                        cr.lines, 24, pw, cr.flame);
    if (cr.item >= 0) {
        // A use of the item (a scroll: the spell goes off it)
        if (cr.scroll) magic::scroll_used(me, scroll_facts(), cr.item, cr.spell);
        else magic::used(me, cr.item);
    }
    // (Strength, Enlarge, Friends: the stats as they stand now; Spiritual Hammer's hammer)
    for (int k = 0; k < pt->count; ++k) {
        rules::keep_hammer(pt->m[k], *names, d->facts);
        rules::recalc(pt->m[k], *names, d->facts);
    }
    cr.at = 0;
    pt->selected = cr.caster;
    if (cr.on_camp) draw_party(c, 17);
    cr.stage = CastRun::Saying;
    next_line(c);
}

void next_line(pic::Canvas& c)
{
    if (cr.at >= cr.n) {
        show_memory(c);
        return;
    }
    const spells::Line& l = cr.lines[cr.at++];
    char t[40];
    switch (l.what) {
    case spells::Said::Word: ow(cr.cs->word, t, sizeof t); break;
    case spells::Said::Unaffected: cw(profile::kIsUnaffected, t, sizeof t); break;
    case spells::Said::Fully: cw(profile::kFullyHealed, t, sizeof t); break;
    case spells::Said::Partly: cw(profile::kPartlyHealed, t, sizeof t); break;
    case spells::Said::Cured: cw(profile::kIsCured, t, sizeof t); break;
    case spells::Said::CanSee: cw(profile::kCanSee, t, sizeof t); break;
    case spells::Said::Unpoisoned: cw(profile::kUnpoisoned, t, sizeof t); break;
    case spells::Said::Raised: cw(profile::kRaised, t, sizeof t); break;
    case spells::Said::Uncursed: cw(profile::kUncursed, t, sizeof t); break;
    case spells::Said::ItemUncursed: cw(profile::kItemUncursed, t, sizeof t); break;
    }
    say_line(c, l.who, t);
    if (cr.on_camp) draw_party(c, 17);
    cr.until = millis() + game_delay_ms();
    if (!cr.until) cr.until = 1;
}

void choose_spell(int spell, pic::Canvas& c)
{
    const spells::Entry e = spells::entry(mrules->tables, spell);
    cr.spell = spell;
    cr.cs = camp_spell(spell);
    cr.on_camp = false;
    if (e.targets == spells::kCombat) {
        // A spell for fights: "can't be cast here... Lose it?"
        char t[32];
        c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
        put(c, spell_name(spell), 1, 19, 10);
        cw(profile::kCantCastHere, t, sizeof t);
        put(c, t, 1, 20, 10);
        dirty_rows(17, 22);
        cw(profile::kLoseIt, t, sizeof t);
        ask_yes_no(c, Ask::LoseIt, t);
        return;
    }
    if (!cr.cs || cr.cs->does == spells::Does::NotYet) {
        error(c, "Not in the engine yet.");          // the spell stays in memory
        return;
    }
    // "NAME casts" / the spell, for a moment
    char nm[20], w1[16], t[48];
    pt->m[cr.caster].name(nm, sizeof nm);
    cw(profile::kCasts, w1, sizeof w1);
    snprintf(t, sizeof t, "%s %s", nm, w1);
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    put(c, t, 1, 19, 10);
    put(c, spell_name(spell), 1, 20, 10);
    dirty_rows(17, 22);
    clear_menu_line(c);
    screen = Screen::Cast;
    cr.stage = CastRun::Casts;
    cr.until = millis() + game_delay_ms();
    if (!cr.until) cr.until = 1;
}

// Fire Shield's flame: "flame type: Hot Cold"; leaving it: "Abort spell? Yes No"
// (as in fights)
void flame_ask(pic::Canvas& c, bool abort)
{
    char a[24], b[24];
    const auto& w = d->prof->fight.words;
    ow(w[abort ? profile::kAbortSpellQ : profile::kFlameType], a, sizeof a);
    ow(w[abort ? profile::kYesNoF : profile::kHotCold], b, sizeof b);
    cr.stage = abort ? CastRun::FlameAbort : CastRun::Flame;
    text::build(menu, a, b);
    menu.selected = abort ? 1 : 0;
    show_menu_line(c);
}

// After "casts": who it's for
void cast_onwards(pic::Canvas& c)
{
    if (cr.cs && cr.cs->does == spells::Does::FireShield && !cr.flame) {
        flame_ask(c, false);
        return;
    }
    const spells::Entry e = spells::entry(mrules->tables, cr.spell);
    if (e.targets == spells::kMember) {
        cr.stage = CastRun::Whom;
        cr.target = cr.caster;
        pt->selected = cr.target;
        draw_whom(c);
        return;
    }
    cr.target = cr.caster;
    do_cast(c);
}

void cast_tick(uint32_t now, pic::Canvas& c)
{
    if (screen != Screen::Cast || !cr.until || static_cast<int32_t>(now - cr.until) < 0) return;
    cr.until = 0;
    if (cr.stage == CastRun::Casts) cast_onwards(c);
    else if (cr.stage == CastRun::Saying) next_line(c);
}

void cast_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (cr.stage == CastRun::Flame || cr.stage == CastRun::FlameAbort) {
        if (y < text::kMenuTapTop) return;
        const char k = text::key(menu, text::hit(menu, col));
        if (cr.stage == CastRun::Flame && (k == 'H' || k == 'C')) {
            cr.flame = k == 'H' ? 1 : 2;
            clear_menu_line(c);
            cast_onwards(c);
        } else if (cr.stage == CastRun::FlameAbort && k == 'N') {
            flame_ask(c, false);
        } else if (cr.stage == CastRun::FlameAbort && k == 'Y') {
            // The spell is gone (an item's use too), nothing else
            party::Character& me = pt->m[cr.caster];
            if (cr.item < 0) magic::remove(me, cr.spell);
            else if (cr.scroll) magic::scroll_used(me, scroll_facts(), cr.item, cr.spell);
            else magic::used(me, cr.item);
            rules::recalc(me, *names, d->facts);
            clear_menu_line(c);
            show_memory(c);
        }
        return;
    }
    if (cr.stage != CastRun::Whom) {
        // A tap moves the message on
        if (cr.until) {
            cr.until = 0;
            if (cr.stage == CastRun::Casts) cast_onwards(c);
            else if (cr.stage == CastRun::Saying) next_line(c);
        }
        return;
    }
    if (y >= text::kMenuTapTop) {
        const char k = text::key(menu, text::hit(menu, col));
        if (k == 'S') {
            cr.target = pt->selected;
            do_cast(c);
        } else if (k == 'E') {
            show_memory(c);         // not cast: the spell stays
        }
        return;
    }
    const int col0 = cr.on_camp ? 17 : 1;
    if (col >= col0 && row >= 4 && row < 4 + pt->count) {
        pt->selected = row - 4;
        draw_party(c, col0);
    }
}

// ---- Use (the items screen; exploring and in camp; fights: fight_use)
// A readied item that casts a spell: "NAME uses an item" and its name
// (rows 21-22) for the game's delay, then as a spell is cast ("Cast Spell
// on whom", what it did), at the item's own level (6); a use goes off the
// item. A scroll: "NAME's Spells on Scroll" (only one they can read),
// "Choose Spell: Cast Exit"; clerics and magic-users read either kind,
// thieves of 10th level 3 times in 4 ("oops!"). A spell for fights: "That
// Item" / "is a combat-only item...", "Use it? Yes No" (Yes: the use goes
// for nothing). Not readied: "Must be Readied"; anything else: nothing.

void item_cast_done(pic::Canvas& c)
{
    if (cr.caster >= 0 && cr.caster < pt->count) pt->selected = cr.caster;
    cr = CastRun{};
    if (view_from == Screen::Game) end_magic();     // (exploring: loaded for this)
    screen = Screen::Items;
    back_to_items(c);
}

void use_spell(int i, int sp, bool scroll, pic::Canvas& c)
{
    if (in_fight()) {
        fight_use(i, sp, scroll, c);
        return;
    }
    party::Character& ch = *pt->sel();
    char nm[20], t[64];
    ch.name(nm, sizeof nm);
    cr = CastRun{};
    cr.caster = pt->selected;
    cr.item = i;
    cr.scroll = scroll;
    cr.spell = sp;
    cr.cs = camp_spell(sp);
    if (screen != Screen::Items) {
        screen = Screen::Items;
        draw_items(c);
    }
    if (scroll && !magic::reads_scroll(ch, rng.roll(100, 1))) {
        char w1[12];
        cw(profile::kOops, w1, sizeof w1);
        snprintf(t, sizeof t, "%s %s", nm, w1);
        say_item(c, t);
        cr = CastRun{};
        if (view_from == Screen::Game) end_magic();     // (loaded for the use)
        return;
    }
    const spells::Entry e = spells::entry(mrules->tables, sp);
    if (e.targets == spells::kCombat) {
        char a[16], b[32], q[16];
        cw(profile::kThatItem, a, sizeof a);
        cw(profile::kCombatOnly, b, sizeof b);
        snprintf(t, sizeof t, "%s %s", a, b);
        say_item(c, t);
        cw(profile::kUseIt, q, sizeof q);
        ask_yes_no(c, Ask::UseIt, q);
        return;
    }
    if (!cr.cs || cr.cs->does == spells::Does::NotYet) {
        cr = CastRun{};
        if (view_from == Screen::Game) end_magic();     // (loaded for the use)
        error(c, "Not in the engine yet.");          // nothing used
        return;
    }
    screen = Screen::Cast;
    clear_menu_line(c);
    if (scroll) {
        cast_onwards(c);
        return;
    }
    // "NAME uses an item" and the item's name, for a moment
    char w1[16], it[48];
    cw(profile::kUsesItem, w1, sizeof w1);
    snprintf(t, sizeof t, "%s %s", nm, w1);
    names->name(items::Item{ch.items[i]}, it, sizeof it);
    c.fill(8, 21 * 8, 38 * 8, 16, 0);
    put(c, t, 1, 21, 10);
    put(c, it, 1, 22, 10);
    dirty_rows(21, 22);
    cr.stage = CastRun::Casts;
    cr.until = millis() + game_delay_ms();
    if (!cr.until) cr.until = 1;
}

// Use it? (a spell for fights from an item, outside one)
bool use_it_answer(char k, pic::Canvas& c)
{
    if (k == 'Y' && cr.item >= 0) {
        party::Character& me = pt->m[cr.caster];
        if (cr.scroll) magic::scroll_used(me, scroll_facts(), cr.item, cr.spell);
        else magic::used(me, cr.item);
        rules::recalc(me, *names, d->facts);
    }
    item_cast_done(c);
    return true;
}

void use_item(int i, pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    if (i < 0 || i >= ch.n_items) return;
    const uint8_t* it = ch.items[i];
    if (!can_use_items(ch)) return;
    if (!items::Item{it}.readied()) {
        note(c, iw(profile::kMustReady));
        return;
    }
    const magic::Scrolls sc = scroll_facts();
    const bool scroll = magic::is_scroll(sc, it);
    if (!scroll && !magic::usable(sc, it)) return;
    if (!load_magic()) {
        end_magic();
        error(c, "Not enough memory.");
        return;
    }
    if (!scroll) {
        use_spell(i, magic::item_spell(it), false, c);
        return;
    }
    uint8_t ids[3];
    const int n = magic::scroll_list(ch, mrules->tables, sc, i, ids, 3);
    if (!n) {                                       // (can't read it: nothing)
        if (!in_fight() && view_from == Screen::Game) end_magic();
        return;
    }
    sl.learning = sl.casting = sl.scrolls = sl.choosing = sl.fighting = false;
    build_lines(ids, n);
    sl.reading = i;
    screen = Screen::SpellList;
    draw_spells(c);
}

void reading_tap(char k, pic::Canvas& c)
{
    if (k == 'C' && sl.sel >= 0) {
        const int item = sl.reading, sp = sl.id[sl.sel];
        sl.reading = -1;
        use_spell(item, sp, true, c);
        return;
    }
    if (k == 'E') {
        sl.reading = -1;
        if (!in_fight() && view_from == Screen::Game) end_magic();
        screen = Screen::Items;
        back_to_items(c);
        return;
    }
    if (k == 'N' && sl.top + list_rows() < sl.n) sl.top += list_rows();
    else if (k == 'P' && sl.top > 0) sl.top = sl.top > list_rows() ? sl.top - list_rows() : 0;
    else return;
    draw_spells(c);
}

// ---- Display: the spell effects
struct EffectLines {
    char    text[80][32];
    uint8_t colour[80];
    int     n = 0, top = 0;
};
EffectLines* el = nullptr;

void end_effects()
{
    delete el;
    el = nullptr;
}

// An effect's name ("" if the list leaves it out)
void effect_name(int type, char* out, size_t cap)
{
    out[0] = 0;
    const auto& m = d->prof->magic;
    for (int i = 0; i < m.n_spell_named; ++i)
        if (m.spell_named[i] == type) {
            for (int s = 1; s <= 0x38; ++s)
                if (spells::entry(mrules->tables, s).affect == type) {
                    snprintf(out, cap, "%s", spell_name(s));
                    return;
                }
            return;
        }
    for (int i = 0; i < m.n_named; ++i)
        if (m.named[i].type == type) {
            ow(m.named[i].at, out, cap);
            return;
        }
}

void draw_effects(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    const int rows = 22 - 4 + 1;
    for (int r = 0; r < rows && el->top + r < el->n; ++r)
        put(c, el->text[el->top + r], 1, 4 + r, el->colour[el->top + r]);
    char keys[32];
    snprintf(keys, sizeof keys, "%s%s%s", el->top + rows < el->n ? d->w_next : "", el->top > 0 ? d->w_prev : "",
             d->w_exit);
    const char* k = keys;
    while (*k == ' ') ++k;
    text::build(menu, "", k);
    menu.selected = 0;
    show_menu_line(c);
    dirty(0, pic::kScreenH);
}

void open_effects(pic::Canvas& c)
{
    if (!el) el = new (std::nothrow) EffectLines;
    if (!el) {
        error(c, "Not enough memory.");
        return;
    }
    el->n = el->top = 0;
    auto add = [&](const char* t, uint8_t colour) {
        if (el->n >= 80) return;
        snprintf(el->text[el->n], sizeof el->text[0], "%s", t);
        el->colour[el->n++] = colour;
    };
    add("", 10);
    char none[24];
    cw(profile::kNoEffects, none, sizeof none);
    for (int i = 0; i < pt->count; ++i) {
        const party::Character& ch = pt->m[i];
        char nm[20];
        ch.name(nm, sizeof nm);
        add(nm, 11);
        bool any = false;
        for (int k = 0; k < ch.n_affects; ++k) {
            char name[30], t[32];
            effect_name(ch.affects[k][0], name, sizeof name);
            if (!name[0]) continue;
            snprintf(t, sizeof t, " %s", name);
            add(t, 10);
            any = true;
        }
        if (!any) add(none, 10);
        add(" ", 10);
    }
    screen = Screen::Effects;
    draw_effects(c);
}

void lose_spell(bool yes, pic::Canvas& c)
{
    if (yes) magic::remove(pt->m[cr.caster], cr.spell);
    show_memory(c);
}

void effects_tap(int x, int y, pic::Canvas& c)
{
    if (y < text::kMenuTapTop) return;
    const int rows = 22 - 4 + 1;
    switch (text::key(menu, text::hit(menu, x / 8))) {
    case 'N':
        if (el->top + rows < el->n) el->top += rows;
        break;
    case 'P': el->top = el->top > rows ? el->top - rows : 0; break;
    case 'E':
        end_effects();
        back_to_magic(c);
        return;
    default: return;
    }
    draw_effects(c);
}

// ---- Alter (the camp's) ----------------------------------------------------------
// "Alter: Order Drop Speed Icon Pics Exit" under the camp screen. Order:
// "Party Order: Select Exit" (tap a member), Select -> "NAME has been
// selected" and "Party Order: Place Exit": a tap on another line moves them
// there (the games: the arrow keys), Place puts them down. Drop: "NAME will
// be gone", "Drop from party? Yes No" -> "NAME bids you farewell" (or "is
// dumped in a ditch" when they can't stand), No -> "Breathes A sigh of
// relief"; the last one: "quit TO DOS: Yes No" (leaves the Play Test).
// Speed: "Game Speed = 4 (0=fastest 9=slowest)" (row 18), "Game Speed:
// Faster Slower Exit" (the area word 0x4BFC). Pics: on / off, Animation on
// / off. Icon: the combat icon editor (with Create New Character too).

enum class AlterMode : uint8_t { Menu, Select, Place, Speed, Pics };
AlterMode alter_mode = AlterMode::Menu;

void aw(int i, char* out, size_t cap) { ow(d->prof->alter.words[i], out, cap); }

void alter_menu(pic::Canvas& c)
{
    char prompt[12];
    aw(profile::kAlterPrompt, prompt, sizeof prompt);
    alter_mode = AlterMode::Menu;
    text::build(menu, prompt, d->w_alter);
    menu.selected = 0;
    show_menu_line(c);
}

void open_alter(pic::Canvas& c)
{
    screen = Screen::Alter;
    alter_menu(c);
}

void start_icon(pic::Canvas& c, Screen from);

// Pics (alter_icon_facts.md 2): the area word 0x4BFF = Pics x 2 +
// Animation, both on at the start; the menu names the current state ("Pics
// on  Animation on  Exit"; Animation isn't offered while Pics is off).
// Animation off: event pictures show their first frame only. Pics itself
// changes nothing else in Curse.
bool pics_on() { return !vm || (vm->get(0x4BFF) & 2); }
bool animation_on() { return !vm || (vm->get(0x4BFF) & 1); }

void pics_menu(pic::Canvas& c)
{
    alter_mode = AlterMode::Pics;
    char words[48], a[20];
    words[0] = 0;
    aw(pics_on() ? profile::kPicsOn : profile::kPicsOff, a, sizeof a);
    strlcat(words, a, sizeof words);
    if (pics_on()) {
        aw(animation_on() ? profile::kAnimOn : profile::kAnimOff, a, sizeof a);
        strlcat(words, a, sizeof words);
    }
    aw(profile::kPicsExit, a, sizeof a);
    strlcat(words, a, sizeof words);
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

void order_menu(pic::Canvas& c, bool place)
{
    char prompt[16];
    aw(profile::kPartyOrder, prompt, sizeof prompt);
    alter_mode = place ? AlterMode::Place : AlterMode::Select;
    text::build(menu, prompt, place ? d->w_place : d->w_select);
    menu.selected = 0;
    show_menu_line(c);
}

void draw_speed(pic::Canvas& c)
{
    const int speed = vm->get(0x4BFC) & 0xFF;
    char a[16], b[26], t[48], words[32], prompt[14], w1[10];
    aw(profile::kSpeedIs, a, sizeof a);
    aw(profile::kSpeedRange, b, sizeof b);
    snprintf(t, sizeof t, "%s%d%s", a, speed, b);
    c.fill(8, 18 * 8, 38 * 8, 8, 0);
    put(c, t, 1, 18, 10);
    dirty_rows(18, 18);
    words[0] = 0;
    if (speed > 0) {
        aw(profile::kFaster, w1, sizeof w1);
        strcat(words, w1);
    }
    if (speed < 9) {
        aw(profile::kSlower, w1, sizeof w1);
        strcat(words, w1);
    }
    aw(profile::kSpeedExit, w1, sizeof w1);
    strcat(words, w1);
    const char* k = words;
    while (*k == ' ') ++k;
    aw(profile::kSpeedPrompt, prompt, sizeof prompt);
    alter_mode = AlterMode::Speed;
    text::build(menu, prompt, k);
    menu.selected = 0;
    show_menu_line(c);
}

// Moves party member `from` to place `to` (the others close up)
void move_member(int from, int to)
{
    if (from == to || from < 0 || to < 0 || from >= pt->count || to >= pt->count) return;
    party::Character* t = new (std::nothrow) party::Character(pt->m[from]);
    if (!t) return;
    if (from < to)
        for (int k = from; k < to; ++k) pt->m[k] = pt->m[k + 1];
    else
        for (int k = from; k > to; --k) pt->m[k] = pt->m[k - 1];
    pt->m[to] = *t;
    delete t;
    pt->selected = to;
}

void alter_drop(pic::Canvas& c)
{
    char t[24];
    if (pt->count <= 1) {
        aw(profile::kQuitToDos, t, sizeof t);
        ask_yes_no(c, Ask::QuitDos, t);
        return;
    }
    aw(profile::kWillBeGone, t, sizeof t);
    say_status(c, t);
    aw(profile::kDropFromParty, t, sizeof t);
    ask_yes_no(c, Ask::AlterDrop, t);
}

bool alter_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what != Ask::AlterDrop && what != Ask::QuitDos) return false;
    screen = Screen::Alter;
    if (what == Ask::QuitDos) {
        if (k == 'Y') exit_wanted = true;
        alter_menu(c);
        return true;
    }
    char t[32];
    if (k == 'Y') {
        aw(pt->sel()->in_combat() ? profile::kBidsFarewell : profile::kDumped, t, sizeof t);
        say_status(c, t);
        leave_party(pt->selected);
        draw_panel(c);
    } else {
        aw(profile::kRelief, t, sizeof t);
        say_status(c, t);
    }
    alter_menu(c);
    return true;
}

void alter_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y < text::kMenuTapTop) {
        if (col < 17 || row < 4 || row >= 4 + pt->count || bigpic >= 0) return;
        if (alter_mode == AlterMode::Place) move_member(pt->selected, row - 4);
        else if (alter_mode != AlterMode::Speed) pt->selected = row - 4;
        draw_party(c, 17);
        return;
    }
    const char k = text::key(menu, text::hit(menu, col));
    if (!k) return;
    switch (alter_mode) {
    case AlterMode::Menu:
        switch (k) {
        case 'O': order_menu(c, false); break;
        case 'D': alter_drop(c); break;
        case 'S': draw_speed(c); break;
        case 'P': pics_menu(c); break;
        case 'I': start_icon(c, Screen::Alter); break;
        case 'E':
            screen = Screen::Camp;
            draw_camp(c);
            break;
        default: break;
        }
        return;
    case AlterMode::Pics: {
        const uint16_t v = vm->get(0x4BFF);
        if (k == 'P') vm->set(0x4BFF, static_cast<uint16_t>(v ^ 2));
        else if (k == 'A') vm->set(0x4BFF, static_cast<uint16_t>(v ^ 1));
        else if (k == 'E') {
            alter_menu(c);
            return;
        }
        pics_menu(c);
        return;
    }
    case AlterMode::Select:
        if (k == 'S') {
            char t[24];
            aw(profile::kHasBeenSelected, t, sizeof t);
            say_status(c, t);
            order_menu(c, true);
        } else if (k == 'E') {
            alter_menu(c);
        }
        return;
    case AlterMode::Place:
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
        if (k == 'P') order_menu(c, false);
        else if (k == 'E') alter_menu(c);
        return;
    case AlterMode::Speed: {
        int speed = vm->get(0x4BFC) & 0xFF;
        const uint16_t high = static_cast<uint16_t>(vm->get(0x4BFC) & 0xFF00);
        if (k == 'F' && speed > 0) --speed;
        else if (k == 'S' && speed < 9) ++speed;
        else if (k == 'E') {
            text::clear(c, text::kTextArea);
            dirty_rows(17, 22);
            alter_menu(c);
            return;
        }
        vm->set(0x4BFC, static_cast<uint16_t>(high | speed));
        draw_speed(c);
        return;
    }
    }
}

// ---- Train Character (a training hall: a script sets 0x7EA8, the classes it
// trains) ---------------------------------------------------------------------
// The games' checks ("we only train conscious people", "Training costs
// 1000 gp.", "We don't train that class here", "Not Enough Experience"),
// then "NAME will become:" (row 4, column 4) / "    a level 6 Fighter"
// (row 5, column 6) and "Do you wish to train? Yes No": "Congratulations...",
// 1000 gp, one level (one class: the one needing the most experience),
// hit points. (A magic-user's new spell: with the spells.)

Making* trainer = nullptr;          // the rule tables while training
int     train_mask = 0;

void end_training()
{
    delete trainer;
    trainer = nullptr;
    train_mask = 0;
}

void train_note(int word, pic::Canvas& c)
{
    char t[48];
    sw(word, t, sizeof t);
    draw_party_menu(c);
    note(c, t);
}

void train_character(pic::Canvas& c)
{
    party::Character* chp = pt->sel();
    if (!chp) return;
    party::Character& ch = *chp;
    if (ch.health() != party::Okay) {
        train_note(profile::kTrainConscious, c);
        return;
    }
    if (!game_won && rules::gold_worth(ch) < 1000) {      // (free once the game is won)
        train_note(profile::kTrainCost, c);
        return;
    }
    if (!trainer && !load_rules(trainer)) {
        end_training();
        error(c, "Not in the engine yet.");
        return;
    }
    const classes::Tables& t = trainer->tables;
    const int here = vm->get(0x7EA8) & 0xFF;
    int has = 0;
    for (int k = 0; k < classes::kClasses; ++k)
        if (ch.level(k) > 0) has |= t.u8(static_cast<uint16_t>(t.lay.class_masks + k));
    // The class that needs the most experience of those ready (one a time)
    const int ready = create::trainable(ch, t);
    int best = -1;
    int32_t most = 0;
    for (int k = 0; k < classes::kClasses; ++k)
        if (ready & t.u8(static_cast<uint16_t>(t.lay.class_masks + k))) {
            const int32_t need = t.exp_needed(k, ch.level(k));
            if (need > most) {
                most = need;
                best = k;
            }
        }
    if (!(has & here)) {
        end_training();
        train_note(profile::kTrainClass, c);
        return;
    }
    train_mask = best >= 0 ? t.u8(static_cast<uint16_t>(t.lay.class_masks + best)) & here : 0;
    if (!train_mask) {
        end_training();
        train_note(profile::kTrainExp, c);
        return;
    }
    // "NAME will become:" and the new levels
    c.fill(8, 8, 38 * 8, 22 * 8, 0);
    char nm[20], w1[24], line[48];
    ch.name(nm, sizeof nm);
    put(c, nm, 4, 4, ch.npc() ? 10 : 11);
    sw(profile::kWillBecome, w1, sizeof w1);
    put(c, w1, 4 + static_cast<int>(strlen(nm)), 4, 10);
    int row = 5;
    for (int k = 0; k < classes::kClasses; ++k) {
        if (ch.level(k) <= 0 || !(t.u8(static_cast<uint16_t>(t.lay.class_masks + k)) & train_mask)) continue;
        sw(row == 5 ? profile::kALevel : profile::kAndALevel, w1, sizeof w1);
        snprintf(line, sizeof line, "%s%d %s", w1, ch.level(k) + 1, name_of(d->cls[0], 27, 18, k));
        put(c, line, 6, row++, 10);
    }
    dirty(0, pic::kScreenH);
    sw(profile::kWishTrain, w1, sizeof w1);
    ask_yes_no(c, Ask::Train, w1);
}

bool open_learn(pic::Canvas& c);

bool train_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what != Ask::Train) return false;
    screen = Screen::PartyMenu;
    party::Character* ch = pt->sel();
    if (k == 'Y' && ch && trainer && train_mask) {
        if (!game_won) rules::pay(*ch, 1000);
        const int mu = ch->level(classes::MagicUser), ra = ch->level(classes::Ranger);
        create::train_classes(*ch, trainer->tables, trainer->facts, trainer->dice, train_mask, false);
        rules::recalc(*ch, *names, d->facts);
        end_training();
        // A magic-user's new level (or a ranger's past 8th): a new spell
        if ((ch->level(classes::MagicUser) > mu || (ch->level(classes::Ranger) > ra && ch->level(classes::Ranger) > 8)) &&
            open_learn(c))
            return true;
        train_note(profile::kCongrats, c);
        return true;
    }
    end_training();
    draw_party_menu(c);
    return true;
}

// ---- Modify Character (the party menu) --------------------------------------------
// A character as made (no adventures yet) can have their stats, hit points
// and name changed within the rules (engine/create: modify_*): View
// Character's screen, the item being changed in colour 13 - a stat's
// value, the hit points, the name -, "Modify: Keep Exit". A tap on a stat,
// the hit points or the name picks it (the name again: the keyboard); the
// keys' left / right arrows (and turns) take it down / up, forward / back
// move to the item above / below (the games' arrow keys); Exit (or Esc)
// puts everything back, Keep keeps it. Others: "NAME can't be modified."

Making* modder = nullptr;               // the rule tables, and the character as it was (ch)
int mod_item = 0;                       // 0-5 the stats, 6 the hit points, 7 the name

void draw_modify(pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch) return;
    draw_character(c);
    char t[24];
    if (mod_item < 6) {
        const int v = ch->stat(mod_item);
        c.fill(5 * 8, (7 + mod_item) * 8, 6 * 8, 8, 0);
        snprintf(t, sizeof t, "%d", v);
        put(c, t, v < 10 ? 6 : 5, 7 + mod_item, 13);
        if (mod_item == 0 && v == 18 && ch->str00() > 0) {
            const int e = ch->str00();
            if (e == 100) snprintf(t, sizeof t, "(00)");
            else snprintf(t, sizeof t, "(%02d)", e);
            put(c, t, 7, 7, 13);
        }
    } else if (mod_item == 6) {
        c.fill(4 * 8, 18 * 8, 3 * 8, 8, 0);
        snprintf(t, sizeof t, "%d", ch->hp());
        put(c, t, 4, 18, 13);
    } else {
        ch->name(t, sizeof t);
        put(c, t, 1, 1, 13);
    }
    text::build(menu, rw(Data::kModify), rw(Data::kKeepExit));
    menu.selected = 0;
    show_menu_line(c);
}

void end_modify(pic::Canvas& c, bool keep)
{
    party::Character* ch = pt->sel();
    if (ch && modder) {
        if (keep) create::modify_done(*ch, modder->tables, modder->facts);
        else *ch = modder->ch;
        rules::recalc(*ch, *names, d->facts);
    }
    delete modder;
    modder = nullptr;
    input_mode = Input::None;
    input_engine = false;
    engine_ask = EngineAsk::Name;
    screen = Screen::PartyMenu;
    draw_party_menu(c);
}

void start_modify(pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch) return;
    if (!create::can_modify(*ch)) {
        char t[60], nm[20];
        ch->name(nm, sizeof nm);
        snprintf(t, sizeof t, "%s%s", nm, rw(Data::kCantModify));
        pm_prompt(c, t);
        dirty_rows(text::kMenuRow, text::kMenuRow);
        return;
    }
    if (!load_rules(modder)) {
        delete modder;
        modder = nullptr;
        pm_prompt(c, "Not in the engine yet.");
        return;
    }
    modder->ch = *ch;
    mod_item = 0;
    screen = Screen::Modify;
    draw_modify(c);
}

// A step down (dir < 0) or up of the item being changed
void modify_step(int dir, pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch || !modder || input_mode != Input::None) return;
    if (mod_item < 6) create::modify_stat(*ch, modder->tables, modder->facts, mod_item, dir);
    else if (mod_item == 6) create::modify_hp(*ch, modder->tables, modder->facts, dir);
    rules::recalc(*ch, *names, d->facts);
    draw_modify(c);
}

void modify_name(pic::Canvas& c)
{
    input_engine = true;
    engine_ask = EngineAsk::ModName;
    input_prompt = modder->words[6];
    input_max = party::kNameMax;
    input_mode = Input::Text;
    input_len = 0;
    input_buf[0] = 0;
    draw_input(c);
}

// The item a tap at (row, col) picks (-1: none)
int modify_item_at(int row, int col)
{
    if (row >= 7 && row <= 12 && col >= 1 && col <= 10) return row - 7;
    if (row == 18 && col >= 1 && col <= 6) return 6;
    if (row == 1 && col >= 1 && col <= 16) return 7;
    return -1;
}

void modify_tap(int x, int y, pic::Canvas& c)
{
    if (!modder || input_mode != Input::None) return;
    const int row = y / 8, col = x / 8;
    if (y >= text::kMenuTapTop) {
        switch (text::key(menu, text::hit(menu, col))) {
        case 'K': end_modify(c, true); break;
        case 'E': end_modify(c, false); break;
        default: break;
        }
        return;
    }
    const int i = modify_item_at(row, col);
    if (i < 0) return;
    if (i == 7 && mod_item == 7) {
        modify_name(c);
        return;
    }
    mod_item = i;
    draw_modify(c);
}

bool modify_act(Act a, pic::Canvas& c)
{
    if (!modder || input_mode != Input::None) return false;
    switch (a) {
    case Act::StepLeft:
    case Act::TurnLeft:   modify_step(-1, c); return true;
    case Act::StepRight:
    case Act::TurnRight:  modify_step(1, c); return true;
    case Act::Forward:    mod_item = (mod_item + 7) % 8; break;
    case Act::TurnAround: mod_item = (mod_item + 1) % 8; break;
    default: return false;
    }
    draw_modify(c);
    return true;
}

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
    case 'S':
        ask_save(c);
        return;
    case 'C':
        start_create(c);
        return;
    case 'A':
        screen = Screen::AddFrom;
        text::build(menu, rw(Data::kAddFrom), rw(Data::kAddSources));
        menu.selected = 0;
        show_menu_line(c);
        return;
    case 'R':
        remove_character(c, false);
        return;
    case 'D':
        if (pt->sel()) {
            char t[60], nm[20];
            pt->sel()->name(nm, sizeof nm);
            snprintf(t, sizeof t, "%s%s%s", rw(Data::kDrop), nm, rw(Data::kForever));
            ask_yes_no(c, Ask::Drop, t);
        }
        return;
    case 'T':
        train_character(c);
        return;
    case 'M':
        start_modify(c);
        return;
    case 'H':
        start_change(c);
        return;
    default:
        pm_prompt(c, "Not in the engine yet.");
        dirty_rows(text::kMenuRow, text::kMenuRow);
        return;
    }
}

void title_tap(int x, int y, pic::Canvas& c);
void icon_tap(int x, int y, pic::Canvas& c);
void icon_back(pic::Canvas& c);

void pm_tap(int x, int y, pic::Canvas& c)
{
    const int row = y / 8, col = x / 8;
    if (screen == Screen::Title) {
        title_tap(x, y, c);
        return;
    }
    if (screen == Screen::Won) {
        won_key(c);
        return;
    }
    if (screen == Screen::Icon) {
        icon_tap(x, y, c);
        return;
    }
    if (screen == Screen::Modify) {
        modify_tap(x, y, c);
        return;
    }
    if (screen == Screen::View) {
        // Items on the menu line opens the list; anything else is Exit
        if (y >= text::kMenuTapTop && text::key(menu, text::hit(menu, col)) == 'I') open_items(c);
        else back_from_view(c);
        return;
    }
    if (screen == Screen::CreatePick) {
        list_tap(x, y, c);
        return;
    }
    if (screen == Screen::CreateName) return;      // the keyboard types the name
    if (screen == Screen::TradeWho) {
        trade_tap(x, y, c);
        return;
    }
    if (screen == Screen::Items || screen == Screen::ShopBuy || screen == Screen::AddList || screen == Screen::Heal ||
        screen == Screen::Take) {
        list_tap(x, y, c);
        return;
    }
    if (screen == Screen::Appraise) {
        appraise_tap(x, y, c);
        return;
    }
    if (screen == Screen::Magic) {
        magic_tap(x, y, c);
        return;
    }
    if (screen == Screen::SpellList) {
        spells_tap(x, y, c);
        return;
    }
    if (screen == Screen::Cast) {
        cast_tap(x, y, c);
        return;
    }
    if (screen == Screen::Effects) {
        effects_tap(x, y, c);
        return;
    }
    if (screen == Screen::Alter) {
        alter_tap(x, y, c);
        return;
    }
    if (screen == Screen::Fight) {
        fight_tap(x, y, c);
        return;
    }
    if (screen == Screen::Loot) {
        list_tap(x, y, c);
        return;
    }
    if (screen == Screen::Rest) {
        rest_tap(x, y, c);
        return;
    }
    if (screen == Screen::AddFrom) {
        add_from_tap(x, y, c);
        return;
    }
    if (screen == Screen::YesNo) {
        yes_no_tap(x, y, c);
        return;
    }
    if (note_until && screen == Screen::PartyMenu) {
        const bool held = note_held;
        redraw_menu(c);
        if (held) return;                   // the tap only puts the error away
    }
    if (screen == Screen::Shop) {
        shop_tap(x, y, c);
        return;
    }
    if (screen == Screen::Camp) {
        camp_tap(x, y, c);
        return;
    }
    if (screen == Screen::SaveWhich) {
        save_tap(x, y, c);
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
        if (load_problem) error(c, load_problem);
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
    // Item names: the words in the program, the types in ITEMS
    const auto& pi = d->prof->items;
    if (!names) names = new (std::nothrow) items::Names;
    if (names && pi.words.at) {
        const size_t n = static_cast<size_t>(pi.words.stride) * pi.words.count;
        uint8_t* buf = static_cast<uint8_t*>(malloc(n));
        if (buf && exepack::read(exe, info, pi.words.at, buf, n) == exepack::Status::Ok)
            names->set_words(buf, pi.words.count, pi.words.stride);
        free(buf);
        names->set_plural(items::Names::Plural{pi.arrow, pi.quarrel, pi.dart, pi.flask, pi.keep1, pi.keep2});
        d->facts.arrow = pi.arrow;
        d->facts.quarrel = pi.quarrel;
        memcpy(d->facts.elf_bonus, pi.elf_bonus, sizeof d->facts.elf_bonus);
        d->facts.strength_fx = pi.stat_fx[0];
        d->facts.giant_fx = pi.stat_fx[1];
        d->facts.enlarge_fx = pi.stat_fx[2];
        d->facts.friends_fx = pi.stat_fx[3];
        d->facts.feeble_fx = pi.stat_fx[4];
        d->facts.con_regen_fx = pi.stat_fx[5];
        {
            const auto& pc = d->prof->create;
            if (pc.ds_image && pc.tables.max_hit_dice &&
                exepack::read(exe, info, pc.ds_image + pc.tables.max_hit_dice, d->facts.max_hd, 8) !=
                    exepack::Status::Ok)
                memset(d->facts.max_hd, 0, sizeof d->facts.max_hd);
        }
        d->facts.hammer_fx = pi.hammer[0];
        d->facts.hammer_type = pi.hammer[1];
        d->facts.hammer_word = pi.hammer[2];
        d->facts.hammer_word2 = pi.hammer[3];
        fs::File tf;
        if (pi.types_file && open_file(pi.types_file, tf)) {
            library::FileSource src(tf);
            names->read_types(src);
            tf.close();
        }
    }
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
            {pi.buy_items, d->w_items, sizeof d->w_items}, {pi.buy, d->w_buy, sizeof d->w_buy},
            {pi.list_next, d->w_next, sizeof d->w_next}, {pi.list_prev, d->w_prev, sizeof d->w_prev},
            {pi.list_exit, d->w_exit, sizeof d->w_exit}, {pi.shop_menu, d->w_shop, sizeof d->w_shop},
            {pi.shop_menu_money, d->w_shop_money, sizeof d->w_shop_money},
            {pi.no_money, d->w_no_money, sizeof d->w_no_money}, {pi.overloaded, d->w_over, sizeof d->w_over},
            {pi.title, d->w_title, sizeof d->w_title}, {pi.heading, d->w_heading, sizeof d->w_heading},
            {pi.ready, d->w_ready, sizeof d->w_ready}, {pi.yes, d->w_yes, sizeof d->w_yes},
            {pi.no, d->w_no, sizeof d->w_no}, {pi.cursed, d->w_cursed, sizeof d->w_cursed},
            {pi.wrong_class, d->w_wrong, sizeof d->w_wrong}, {pi.already, d->w_already, sizeof d->w_already},
            {pi.hands_full, d->w_hands, sizeof d->w_hands}, {pi.plural_s, d->w_s, sizeof d->w_s},
            {pi.weapon, d->w_weapon, sizeof d->w_weapon}, {pi.armour, d->w_armour, sizeof d->w_armour},
            {pp.save_which, d->w_save_which, sizeof d->w_save_which}, {pp.slots, d->w_slots, sizeof d->w_slots},
            {pp.saving, d->w_saving, sizeof d->w_saving}, {pp.camp, d->w_camp, sizeof d->w_camp},
            {pp.makes_camp, d->w_makes_camp, sizeof d->w_makes_camp},
        };
        for (auto& wd : words)
            if (wd.at) text::read_pascal(src, wd.at, wd.out, wd.cap);
        for (int i = 0; i < profile::kItemWords; ++i)
            if (d->prof->item_words[i]) text::read_pascal(src, d->prof->item_words[i], d->iw[i], sizeof d->iw[i]);
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
    if (pp.camp_menu) text::read_pascal(exe, info, pp.camp_menu, d->w_camp_menu, sizeof d->w_camp_menu);
    const auto& pm = d->prof->magic;
    if (pm.menu) text::read_pascal(exe, info, pm.menu, d->w_magic, sizeof d->w_magic);
    const auto& pa = d->prof->alter;
    if (pa.menu) text::read_pascal(exe, info, pa.menu, d->w_alter, sizeof d->w_alter);
    if (pa.select) text::read_pascal(exe, info, pa.select, d->w_select, sizeof d->w_select);
    if (pa.place) text::read_pascal(exe, info, pa.place, d->w_place, sizeof d->w_place);
    for (int i = 0; i < 5 && pm.levels.at; ++i)
        text::read_pascal(exe, info, pm.levels.at + static_cast<uint32_t>(i) * pm.levels.stride, d->w_level[i],
                          sizeof d->w_level[i]);
    {
        const uint32_t at[Data::kRosterWords] = {pp.add_from, pp.add_sources, pp.add_prompt, pp.add, pp.added,
                                                 pp.paladin_evil, pp.rangers, pp.no_evil, pp.overwrite, pp.qmark,
                                                 pp.drop, pp.forever, pp.sure, pp.dump, pp.out_back, pp.farewell,
                                                 pp.relief, pp.yes_no, pp.cant_modify, pp.modify, pp.keep_exit,
                                                 pp.from_saved, pp.new_file};
        fs::File of;
        if (open_file(d->prof->overlay, of)) {
            library::FileSource src(of);
            for (int i = 0; i < Data::kRosterWords; ++i)
                if (at[i]) text::read_pascal(src, at[i], d->roster[i], sizeof d->roster[i]);
            of.close();
        }
        if (!d->roster[Data::kYesNo][0]) strcpy(d->roster[Data::kYesNo], "Yes No");
        if (!d->roster[Data::kModify][0]) strcpy(d->roster[Data::kModify], "Modify: ");
        if (!d->roster[Data::kKeepExit][0]) strcpy(d->roster[Data::kKeepExit], "Keep Exit");
        if (!d->roster[Data::kCantModify][0]) strcpy(d->roster[Data::kCantModify], " can't be modified.");
    }
    if (!d->w_slots[0]) strcpy(d->w_slots, "A B C D E F G H I J");
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
        d->wall_block[set - 1] = static_cast<int16_t>(block < 0 ? -1 : block);
        d->wall_set[set - 1] = static_cast<int16_t>(block < 0 ? -1 : set);
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
        // The later parts of a multi-part block fill the next sets
        for (int k = 1; k < n && set - 1 + k < 3; ++k) d->wall_block[set - 1 + k] = d->wall_set[set - 1 + k] = -1;
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
        last_pic_id = id == 0xFF ? -1 : id;
        last_pic_head = head;
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
    void load_items(int block, items::Ground& g) override
    {
        char name[24];
        area_file(name, sizeof name, "ITEM");
        fs::File f;
        if (!open_dax(name, f)) return;
        library::FileSource src(f);
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
        if (e) {
            dax::RleReader r(src, d->idx, *e);
            while (g.n < items::kMaxGround && r.read(g.item[g.n], items::kRecordSize) == items::kRecordSize) ++g.n;
        }
        f.close();
        Serial.printf("[play] %s #%d: %d items\n", name, block, g.n);
    }
    void random_items(int n, items::Ground& g) override
    {
        // The program's rows of ready-made items (potions, the wand ...), then the dice
        const profile::Profile* p = d->prof;
        if (!p->random_items || !p->random_rows) return;
        treasure::Rows rows{};
        fs::File f;
        bool ok = false;
        if (open_file(p->program, f)) {
            library::FileSource src(f);
            exepack::Info info;
            uint8_t raw[sizeof rows.r];
            ok = exepack::parse(src, info) == exepack::Status::Ok &&
                 exepack::read(src, info, p->create.ds_image + p->random_rows, raw, sizeof raw) == exepack::Status::Ok;
            for (int r = 0; ok && r < 7; ++r)
                for (int k = 0; k < 8; ++k)
                    rows.r[r][k] = static_cast<uint16_t>(raw[r * 16 + k * 2] | raw[r * 16 + k * 2 + 1] << 8);
            f.close();
        }
        if (!ok) {
            log("TREASURE: the program's item rows didn't read");
            return;
        }
        for (int k = 0; k < n && g.n < items::kMaxGround; ++k) treasure::make(g.item[g.n++], *p->random_items, rows, rng);
        Serial.printf("[play] TREASURE: %d random items (%d on the ground)\n", n, g.n);
    }
    void load_monster(int id, int copies, int icon) override { fight_load_monster(id, copies, icon); }
    void clear_monsters() override { fight_clear_monsters(); }
    bool add_npc(int id) override { return npc_join(id); }
    void party_changed() override
    {
        for (int i = 0; i < pt->count; ++i) rules::recalc(pt->m[i], *names, d->facts);
        if (cv && screen == Screen::Game && !bigpic_shown()) draw_party(*cv, 17);
    }
    void party_killed() override { party_dead = true; }
    void sound(int id) override { sfx(id); }
    void log(const char* what) override { Serial.printf("[ecl %d:%04X] %s\n", d->gs.script, vm->pc() + 0x8000, what); }
};

// ---- running scripts -------------------------------------------------------------

void host_picture(int id, int head) { host->picture(id, head); }

void draw_input(pic::Canvas& c)
{
    clear_menu_line(c);
    const int x = input_engine ? static_cast<int>(strlen(input_prompt)) : 0;
    if (input_engine) put(c, input_prompt, 0, text::kMenuRow, 13);
    put(c, input_buf, x, text::kMenuRow, input_engine ? 13 : 10);
    if (x + input_len < 40) c.fill((x + input_len) * 8, text::kMenuRow * 8, 8, 8, 15);     // the cursor
}

void start_won(pic::Canvas& c);

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
        int at[text::kMaxItems + 1] = {}, until[text::kMaxItems + 1] = {};
        for (int i = 0; i < vm->items() && o + 2 < sizeof menu_text; ++i) {
            const char* it = vm->item(i);
            if (i) menu_text[o++] = ' ';
            if (i <= text::kMaxItems) at[i] = static_cast<int>(o);
            for (int k = 0; it[k] && o + 1 < sizeof menu_text; ++k) {
                char ch = it[k];
                if (k == 0) ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
                else if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
                menu_text[o++] = ch;
            }
            if (i <= text::kMaxItems) until[i] = static_cast<int>(o) - 1;
        }
        menu_text[o] = 0;
        text::build(menu, vm->prompt(), menu_text);
        if (menu.count != vm->items()) {
            // Words that don't split by their capitals (a digit inside, too
            // long a line): one target a choice, as far as the line shows
            const int shown = static_cast<int>(sizeof menu.s) - 1;
            menu.count = 0;
            for (int i = 0; i < vm->items() && i < text::kMaxItems && at[i] < shown; ++i) {
                menu.start[i] = static_cast<int8_t>(at[i]);
                menu.end[i] = static_cast<int8_t>(until[i] < shown ? until[i] : shown - 1);
                menu.count = i + 1;
            }
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
        input_max = ecl::kMaxInput;         // (the engine's own questions set it shorter)
        input_engine = false;
        input_prompt = "";
        input_len = 0;
        input_buf[0] = 0;
        draw_input(c);
        break;
    case ecl::Wait::Pause:
        pause_until = millis() + vm->pause_ms();
        break;
    case ecl::Wait::Who: {
        // "<prompt> Select" over the party list (a tap on a line picks them)
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
        char prompt[48];
        snprintf(prompt, sizeof prompt, "%s ", vm->prompt());
        text::build(menu, prompt, iw(profile::kSelect));
        menu.selected = 0;
        show_menu_line(c);
        if (!bigpic_shown()) draw_party(c, 17);
        break;
    }
    case ecl::Wait::Camp:               // PROGRAM 9: the camp
        camp_from_script = true;
        open_camp(c);
        break;
    case ecl::Wait::Won:                // PROGRAM 8: the game won
        start_won(c);
        break;
    case ecl::Wait::Key:                // "press <enter>/<return> to continue"
        clear_menu_line(c);
        put(c, d->press_key, 0, text::kMenuRow, 15);
        break;
    case ecl::Wait::Shop:
        temple = false;
        open_shop(c);
        break;
    case ecl::Wait::Temple:
        temple = true;
        open_shop(c);
        break;
    case ecl::Wait::Combat:
        fight_start(c);
        break;
    case ecl::Wait::Treasure:
        fight_treasure_only(c);
        break;
    case ecl::Wait::PartyMenu:          // PROGRAM 0: the party menu, BEGIN goes on
        in_game_menu = true;
        anim_stop();
        screen = Screen::PartyMenu;
        draw_party_menu(c);
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
// Look: search mode before it (put back after the search script)
uint16_t look_search = 0;
bool look_pending = false;
// Locked doors (door_facts.md): what the "Locked." menu may offer, all
// back on with each step the party takes
bool can_bash = true, can_pick = true, can_knock = true;

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
            can_bash = can_pick = can_knock = true;
            // A minute a step, ten when searching (clock slot 1 / 2)
            vm->advance_clock((vm->get(0x7ECA) & 1) ? 2 : 1, 1);
        }
    }
    vm->set(0x7EC9, 0);
    if (locked) return false;
    after_move_redraw();
    return true;
}

void door_done()
{
    clear_menu_line(*cv);
    after_move_redraw();
    run_entry(1, Then::Arrive);
}

// "Locked. Bash Pick Knock Exit" on the menu line (door_facts.md): Bash
// while it's allowed, Pick with a thief, Knock with the spell memorized;
// nothing to offer but Exit: the party just stays
void door_prompt(pic::Canvas& c)
{
    const auto& dw = d->prof->door;
    char words[48] = {}, w[12];
    if (can_bash) {
        ow(dw.bash, w, sizeof w);
        strlcat(words, w, sizeof words);
    }
    if (can_pick && rules::has_thief(*pt)) {
        ow(dw.pick, w, sizeof w);
        strlcat(words, w, sizeof words);
    }
    if (can_knock && dw.knock_spell && rules::knock_member(*pt, dw.knock_spell) >= 0) {
        ow(dw.knock, w, sizeof w);
        strlcat(words, w, sizeof words);
    }
    if (!words[0]) {
        door_done();
        return;
    }
    ow(dw.exit, w, sizeof w);
    strlcat(words, w, sizeof words);
    char prompt[12];
    ow(dw.locked, prompt, sizeof prompt);
    text::build(menu, prompt, words);
    menu.selected = 0;
    show_menu_line(c);
}

// The choice: Bash / Pick open the door (both sides) and the party steps
// through; Knock (a member's spell used up) lets them through without
// opening it; failing or Exit, they stay. Either way the view is drawn
// again and the arrival script runs.
void door_choice(char k)
{
    ecl::GameState& g = d->gs;
    const int state = geo::passage(d->map, g.x, g.y, step_dir);
    bool through = false;
    if (k == 'B') {
        bool gone = false;
        through = rules::bash_door(*pt, state, rng, &gone);
        if (gone) can_bash = false;
        if (through) geo::unlock(d->map, g.x, g.y, step_dir);
    } else if (k == 'P') {
        if (state == 2) through = rules::pick_lock(*pt, rng);
        can_pick = false;
        if (through) geo::unlock(d->map, g.x, g.y, step_dir);
    } else if (k == 'K') {
        const int m = rules::knock_member(*pt, d->prof->door.knock_spell);
        if (m >= 0) {
            for (int i = 0; i < 84; ++i)
                if (pt->m[m].rec[0x1E + i] == d->prof->door.knock_spell) {
                    pt->m[m].rec[0x1E + i] = 0;
                    break;
                }
            through = true;
        }
    }
    if (through) {
        sfx(sound::kStep);
        g.x = (g.x + geo::dx(step_dir)) & 15;
        g.y = (g.y + geo::dy(step_dir)) & 15;
        can_bash = can_pick = can_knock = true;
        vm->advance_clock((vm->get(0x7ECA) & 1) ? 2 : 1, 1);
        if (vm->get(0x4BE6)) sfx(sound::kStep);
    }
    door_done();
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
    if (party_dead && in_demo) {
        party_dead = false;
        end_demo(c);
        return;
    }
    if (party_dead) {
        // DAMAGE killed everyone: the party menu
        party_dead = false;
        while (pt->count) leave_party(pt->count - 1);
        anim_stop();
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return;
    }
    if (r == ecl::Stop::NewScript) {
        // A new script block: its first run, then its step and arrival runs
        // (no map edge tried in it: coab's facts, sub_29677)
        d->gs.moved = false;
        vm->set(0x7ED5, 0);
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
        if (vm->get(0x4BE6)) sfx(sound::kStep);         // a step in the 3D view
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
        break;
    case Then::Camp:
        then = Then::Idle;
        open_camp(c);
        return;
    case Then::Begun:
        if (in_demo) {                      // the demo is its first run only
            end_demo(c);
            return;
        }
        vm->set(0x4BF2, d->gs.script);
        break;
    case Then::Arrive:
    case Then::Idle:
        break;
    }
    then = Then::Idle;
    if (look_pending) {
        // Look's search script (and any block it led to) done: search mode as before
        look_pending = false;
        vm->set(0x7ECA, look_search);
    }
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
    // A step through a side that isn't solid, off the map's edge: 0x7ED5 = 1
    // for the step script (the scripts take the party to the next area);
    // else 0 (coab's facts: TryStepForward)
    {
        const ecl::GameState& g = d->gs;
        const int nx = g.x + geo::dx(dir_of_step), ny = g.y + geo::dy(dir_of_step);
        const bool off = nx < 0 || nx > 15 || ny < 0 || ny > 15;
        vm->set(0x7ED5, d->map.loaded && off && geo::passage(d->map, g.x, g.y, dir_of_step) != 0 ? 1 : 0);
    }
    // The step script runs before the move, on the square the party is on
    run_entry(0, Then::Move);
}

// A list menu's choices, the highlighted one (list_sel) black on white
void draw_list_menu(pic::Canvas& c)
{
    for (int i = 0; i < vm->items(); ++i) {
        const int row = list_row0 + i;
        if (row > text::kTextArea.y1) break;
        c.fill(8, row * 8, 38 * 8, 8, 0);
        if (i == list_sel) {
            c.fill(8, row * 8, static_cast<int>(strlen(vm->item(i))) * 8, 8, 15);
            font::draw_text(c, d->font, vm->item(i), 1, row, 0, -1);
        } else {
            put(c, vm->item(i), 1, row, 10);
        }
    }
    dirty_rows(17, 22);
}

void finish_print_wait()
{
    // A list menu prints its prompt first, then shows the choices
    if (vm->wait() == ecl::Wait::ListMenu) {
        list_row0 = w.row + (w.col > w.r.x0 ? 1 : 0);
        list_sel = 0;                   // the first choice starts highlighted, as in the games
        draw_list_menu(*cv);
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
    if (view_from == Screen::Fight && in_fight()) {
        fight_after_view(c);
        return;
    }
    if (view_from == Screen::Fight) view_from = Screen::Game;  // (the fight is over: never stuck here)
    screen = view_from;
    if (screen == Screen::Shop) {
        draw_shop(c);
        return;
    }
    if (screen == Screen::Camp) {
        draw_camp(c);
        return;
    }
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
    // Won: Begin does nothing any more (area word 0x3FA, as the original)
    if (!in_demo && d->prof->won.begin_word && (vm->get(d->prof->won.begin_word) & 0xFF) != 0) return;
    if (in_game_menu) {
        // PROGRAM 0's party menu: back to the game where the script was
        in_game_menu = false;
        screen = Screen::Game;
        draw_frame(c);
        draw_view(c);
        draw_panel(c);
        waiting = false;
        handle(vm->resume());
        return;
    }
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
    run_entry(4, Then::Begun);
}

#include "play_fight.inc"

// ---- the icon editor (alter_icon_facts.md 1): Alter -> Icon, and Create
// New Character after the name. The outer frame, the combat colours (0 / 8
// swapped), the icon as it was ("old") and as it is now ("new"), each ready
// and action on COMSPR block 25's grey squares; the menus Parts / 1st-color
// / 2nd-color / Size / Exit -> Head Weapon / the colour's part / the other
// size -> Next Prev Keep Exit. Keep makes a value the kept one, Exit (Esc)
// puts the kept one back; Exit at the top keeps only what was kept and asks
// "Is this icon ok?" - No: another pass.
struct IconEdit {
    party::Character* ch = nullptr;
    Screen   from = Screen::Alter;
    uint8_t  keep[9] = {};              // head, body, size, the six colour bytes
    int      level = 1;                 // 1 Parts, 2 Head / Weapon, 3 colour part, 4 size, 5 cycle, 6 ok?
    int      part = 0;                  // level 5: 0 head, 1 body, 2 a colour
    int      slot = 0;                  // the colour slot (0-5)
    bool     second = false;            // 2nd-color
    Pic4     old_ic, new_ic, base[2];
    char     menus[5][42] = {};
    char     words[profile::kIconWords][20] = {};
};
IconEdit* ie = nullptr;

void end_icon_art();

void icon_take(uint8_t* to, const uint8_t* rec)
{
    to[0] = rec[0x141];
    to[1] = rec[0x142];
    to[2] = rec[0x144];
    memcpy(to + 3, rec + 0x145, 6);
}
void icon_put(uint8_t* rec, const uint8_t* from)
{
    rec[0x141] = from[0];
    rec[0x142] = from[1];
    rec[0x144] = from[2];
    memcpy(rec + 0x145, from + 3, 6);
}

void icon_pair(pic::Canvas& c, const Pic4& ic, int y)
{
    c.fill(32, y, 96, kSq, 0);
    blit4(c, ie->base[0].px[0], ie->base[0].w, ie->base[0].h, 32, y, false, true, 0, 0, pic::kScreenW, pic::kScreenH);
    blit4(c, ie->base[1].px[0], ie->base[1].w, ie->base[1].h, 104, y, false, true, 0, 0, pic::kScreenW, pic::kScreenH);
    blit4(c, ic.px[0], ic.w, ic.h, 32, y, false, true, 0, 0, pic::kScreenW, pic::kScreenH);
    blit4(c, ic.px[1], ic.w, ic.h, 104, y, false, true, 0, 0, pic::kScreenW, pic::kScreenH);
    dirty(y, y + kSq);
}

void icon_new(pic::Canvas& c)
{
    build_icon(ie->ch->rec, ie->new_ic);
    icon_pair(c, ie->new_ic, 104);
}

void icon_menu(pic::Canvas& c)
{
    char t[48];
    switch (ie->level) {
    case 1: text::build(menu, "", ie->menus[0]); break;
    case 2: text::build(menu, "", ie->menus[1]); break;
    case 3: {
        strlcpy(t, ie->menus[2], sizeof t);
        const char* hf = ie->words[ie->second ? profile::kIconFace : profile::kIconHair];
        char* x = strstr(t, "xxxx");
        if (x && strlen(hf) == 4) memcpy(x, hf, 4);
        text::build(menu, "", t);
        break;
    }
    case 4:
        snprintf(t, sizeof t, "%s%s", ie->words[ie->ch->rec[0x144] == 2 ? profile::kIconSmall : profile::kIconLarge],
                 ie->menus[3]);
        text::build(menu, "", t);
        break;
    case 5: text::build(menu, "", ie->menus[4]); break;
    default: text::build(menu, ie->words[profile::kIconOk], rw(Data::kYesNo)); break;
    }
    menu.selected = ie->level == 6 ? 1 : 0;          // the question starts on No, as the games do
    show_menu_line(c);
}

// A pass: the kept values from the record, the old pair, the new pair
void icon_pass(pic::Canvas& c)
{
    icon_take(ie->keep, ie->ch->rec);
    build_icon(ie->ch->rec, ie->old_ic);
    icon_pair(c, ie->old_ic, 56);
    icon_new(c);
    ie->level = 1;
    icon_menu(c);
}

void start_icon(pic::Canvas& c, Screen from)
{
    party::Character* ch = from == Screen::CreateName ? (mk ? &mk->ch : nullptr) : pt->sel();
    if (!ch) return;
    end_icon_art();
    ie = new (std::nothrow) IconEdit;
    if (!ie) {
        error(c, "Not enough memory for the icon editor.");
        return;
    }
    ie->ch = ch;
    ie->from = from;
    const auto& pa = d->prof->alter;
    fs::File f;
    if (open_file(d->prof->program, f)) {
        library::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok)
            for (int i = 0; i < 5; ++i)
                if (pa.icon_menu[i]) text::read_pascal(src, info, pa.icon_menu[i], ie->menus[i], sizeof ie->menus[i]);
        f.close();
    }
    for (int i = 0; i < profile::kIconWords; ++i) ow(pa.icon_words[i], ie->words[i], sizeof ie->words[i]);
    for (int fr = 0; fr < 2; ++fr) {
        int w = 0, h = 0;
        ie->base[fr].px[0] = load_pic4("COMSPR.DAX", 25 + fr * 0x80, 0, &w, &h);
        ie->base[fr].w = static_cast<uint8_t>(w);
        ie->base[fr].h = static_cast<uint8_t>(h);
    }
    screen = Screen::Icon;
    anim_stop();
    pic_shown = false;
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    put(c, ie->words[profile::kIconOld], 8, 6, 15);
    put(c, ie->words[profile::kIconReadyAction], 3, 10, 15);
    put(c, ie->words[profile::kIconNew], 8, 12, 15);
    put(c, ie->words[profile::kIconReadyAction], 3, 16, 15);
    dirty(0, pic::kScreenH);
    palette_fight(true);
    icon_pass(c);
}

void end_icon_art()
{
    if (!ie) return;
    free_pic(ie->old_ic);
    free_pic(ie->new_ic);
    free_pic(ie->base[0]);
    free_pic(ie->base[1]);
    delete ie;
    ie = nullptr;
}

void create_after_icon(pic::Canvas& c);

void end_icon(pic::Canvas& c)
{
    const Screen from = ie->from;
    end_icon_art();
    palette_fight(false);
    if (from == Screen::CreateName) {
        create_after_icon(c);
        return;
    }
    draw_camp(c);
    screen = Screen::Alter;
    alter_menu(c);
}

void icon_key(char k, pic::Canvas& c)
{
    uint8_t* rec = ie->ch->rec;
    switch (ie->level) {
    case 1:
        if (k == 'P') ie->level = 2;
        else if (k == '1' || k == '2') {
            ie->second = k == '2';
            ie->level = 3;
        } else if (k == 'S') ie->level = 4;
        else if (k == 'E') {
            icon_put(rec, ie->keep);            // only what was kept
            icon_new(c);
            ie->level = 6;
        } else return;
        icon_menu(c);
        return;
    case 2:
        if (k == 'H' || k == 'W') {
            ie->part = k == 'H' ? 0 : 1;
            ie->level = 5;
        } else if (k == 'E') ie->level = 1;
        else return;
        icon_menu(c);
        return;
    case 3: {
        // Weapon Body Hair / Face Shield Arm Leg: colour slots 5 0 3 4 1 2
        const int slot = k == 'W' ? 5 : k == 'B' ? 0 : (k == 'H' || k == 'F') ? 3 : k == 'S' ? 4 : k == 'A' ? 1 : k == 'L' ? 2 : -1;
        if (slot >= 0) {
            ie->part = 2;
            ie->slot = slot;
            ie->level = 5;
        } else if (k == 'E') ie->level = 1;
        else return;
        icon_menu(c);
        return;
    }
    case 4:
        if (k == 'L' || k == 'S') {
            rec[0x144] = k == 'L' ? 2 : 1;
            icon_new(c);
        } else if (k == 'K') {
            ie->keep[2] = rec[0x144];
            ie->level = 1;
        } else if (k == 'E') {
            rec[0x144] = ie->keep[2];
            icon_new(c);
            ie->level = 1;
        } else return;
        icon_menu(c);
        return;
    case 5: {
        const int back = ie->part == 2 ? 3 : 2;
        if (k == 'N' || k == 'P') {
            const int step = k == 'N' ? 1 : -1;
            if (ie->part == 0) rec[0x141] = static_cast<uint8_t>((rec[0x141] + 14 + step) % 14);
            else if (ie->part == 1) rec[0x142] = static_cast<uint8_t>((rec[0x142] + 32 + step) % 32);
            else {
                uint8_t& b = rec[0x145 + ie->slot];
                if (ie->second) b = static_cast<uint8_t>((b & 0x0F) | ((((b >> 4) + 16 + step) & 15) << 4));
                else b = static_cast<uint8_t>((b & 0xF0) | (((b & 15) + 16 + step) & 15));
            }
            icon_new(c);
            return;
        }
        if (k == 'K') {
            if (ie->part == 0) ie->keep[0] = rec[0x141];
            else if (ie->part == 1) ie->keep[1] = rec[0x142];
            else memcpy(ie->keep + 3, rec + 0x145, 6);
        } else if (k == 'E') {
            if (ie->part == 0) rec[0x141] = ie->keep[0];
            else if (ie->part == 1) rec[0x142] = ie->keep[1];
            else memcpy(rec + 0x145, ie->keep + 3, 6);
            icon_new(c);
        } else return;
        ie->level = back;
        icon_menu(c);
        return;
    }
    default:
        if (k == 'Y') end_icon(c);
        else if (k == 'N') icon_pass(c);
        return;
    }
}

void icon_tap(int x, int y, pic::Canvas& c)
{
    if (!ie || y < text::kMenuTapTop) return;
    const int k = text::hit(menu, x / 8);
    if (k < 0) return;
    menu.selected = k;
    show_menu_line(c);
    icon_key(text::key(menu, k), c);
}

// Esc: as Exit at the level shown (No at the question)
void icon_back(pic::Canvas& c) { icon_key(ie->level == 6 ? 'N' : 'E', c); }

// The sound driver's tables and byte code, from the player's program
void load_sound()
{
    const sound::Layout& l = d->prof->sound;
    if (!l.hi || l.hi <= l.lo) return;
    const size_t n = l.hi - l.lo;
    uint8_t* buf = static_cast<uint8_t*>(malloc(n));
    snd = new (std::nothrow) sound::Player;
    fs::File f;
    bool ok = false;
    if (buf && snd && open_file(d->prof->program, f)) {
        library::FileSource src(f);
        exepack::Info info;
        ok = exepack::parse(src, info) == exepack::Status::Ok &&
             exepack::read(src, info, l.segment_image + l.lo, buf, n) == exepack::Status::Ok && snd->set_data(l, buf, n);
        f.close();
    }
    free(buf);
    if (!ok) {
        Serial.println("[play] the sound driver's data didn't read: no sound");
        delete snd;
        snd = nullptr;
    }
}

// ---- the title sequence (Tom, 2026-10-10: "like starting the game for real")
// As the game begins: the title pictures (TITLE.DAX) and the credits
// (GAME.OVR's print calls), each for its time or until a tap; then the
// version line with Play / Demo on a clear screen; Play opens the party
// menu. Only while it runs: the credits' lines (~2 KB).
constexpr int kTitleCredits = 48;
struct TitleRun {
    printcalls::Line credits[kTitleCredits];
    int      n_credits = 0;
    int      step = 0;
    uint32_t since = 0;
    bool     prompt = false;
    uint32_t timeout_ms = 30000;        // the version line picks Demo after this (10 s after a demo)
    char     version[48] = {};
    char     words[24] = {};
};
TitleRun* title_run = nullptr;

bool title_picture(pic::Canvas& c, int block, int row, int col)
{
    fs::File f;
    if (!open_dax(d->prof->title_file, f)) return false;
    library::FileSource src(f);
    bool ok = false;
    const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
    if (e) {
        dax::RleReader r(src, d->idx, *e);
        uint8_t hdr[pic::kHeaderSize];
        pic::Header h;
        ok = r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, h) &&
             pic::draw(r, h, 0, c, col * 8, row * 8);
    }
    f.close();
    return ok;
}

// Draws steps from `from` on until one that waits; that one is current
void title_prompt(pic::Canvas& c);

void title_show(int from, pic::Canvas& c)
{
    const profile::Profile& p = *d->prof;
    bool shown = false;
    for (int s = from; s < p.title_steps; ++s) {
        const profile::TitleStep& st = p.title[s];
        if (st.block == 0 && title_run->n_credits == 0) continue;      // no GAME.OVR: no credits
        shown = true;
        if (st.clear) c.clear(0);
        if (st.block == 0) {
            layout::outer(c, d->tables, d->frame_tiles);
            for (uint8_t bar : p.credits_bars) layout::bar(c, d->tables, d->frame_tiles, bar);
            for (int i = 0; i < title_run->n_credits; ++i) {
                const printcalls::Line& l = title_run->credits[i];
                font::draw_text(c, d->font, l.s, l.col, l.row, l.fg, l.bg);
            }
        } else {
            title_picture(c, st.block, st.row, st.col);
        }
        if (st.sound) sfx(st.sound);
        title_run->step = s;
        if (st.wait_ms > 0) break;
    }
    title_run->since = millis();
    dirty_rows(0, 24);
    if (!shown) title_prompt(c);        // (nothing left to show: the version line's Play / Demo)
}

void end_title(pic::Canvas& c)
{
    delete title_run;
    title_run = nullptr;
    c.clear(0);
    menu_on = false;
    screen = Screen::PartyMenu;
    draw_party_menu(c);
}

// After the pictures: the version line and its menu (Play / Demo)
void title_prompt(pic::Canvas& c)
{
    title_run->prompt = true;
    c.clear(0);
    dirty_rows(0, 24);
    if (!title_run->words[0]) {
        end_title(c);
        return;
    }
    text::build(menu, title_run->version, title_run->words);
    menu.selected = 0;
    show_menu_line(c);
    title_run->since = millis();
}

void start_demo(pic::Canvas& c);

void title_next(pic::Canvas& c)
{
    if (title_run->step + 1 < d->prof->title_steps) title_show(title_run->step + 1, c);
    else title_prompt(c);
}

void title_tick(uint32_t now, pic::Canvas& c)
{
    if (!title_run) return;
    if (title_run->prompt) {
        if (title_run->words[0] && now - title_run->since >= title_run->timeout_ms) start_demo(c);
        return;
    }
    if (now - title_run->since >= d->prof->title[title_run->step].wait_ms) title_next(c);
}

void title_tap(int x, int y, pic::Canvas& c)
{
    if (!title_run->prompt) {
        title_next(c);                      // a tap skips the wait
        return;
    }
    if (note_until) {
        redraw_menu(c);
        return;
    }
    if (y < text::kMenuTapTop) return;
    const int k = text::hit(menu, x / 8);
    if (k < 0) return;
    menu.selected = k;
    show_menu_line(c);
    if (text::key(menu, k) == 'D') start_demo(c);
    else end_title(c);
}

// ---- The game won (PROGRAM 8; the Project's claude/curse_finish_facts.md 1)
// Six pages of the end texts in the text window ("Press any key to
// continue." after each but the 4th), the pictures between them (two
// animations played once, a picture drawn faded, a head and body, the big
// picture with fireworks until a key), then the party healed, training
// for everyone and free, Begin stopped, and the party menu.
struct WonRun {
    enum Stage : uint8_t { Text, Anim, Key, Hold, Fireworks } stage = Text;
    int      page = 0;
    uint32_t at = 0;
    uint8_t  fade[16] = {};
    char     text[320] = {};            // the page's text (the writer reads it while it prints)
    // The fireworks (an approximation of the original's; facts 1.4)
    bool     rocket = false, burst = false, stop = false;
    int      rx = 0, ry = 0, rvx = 0, rvy = 0, steps = 0;
    uint32_t next_rocket = 0, step_at = 0;
    struct Spark { int16_t x, y, vx, vy; uint8_t col, under; bool drawn; };
    Spark    spark[120];
    struct Px { int16_t x, y; uint8_t under; };
    Px       trail[64];
    int      n_trail = 0;
};
WonRun* won = nullptr;

void won_page(int page, pic::Canvas& c);

// The page's text, all at once (a key goes on when the window fills)
void won_text(int page, pic::Canvas& c)
{
    const auto& wf = d->prof->won;
    int first = 0;
    for (int p = 0; p < page; ++p) first += wf.page_lines[p];
    char* buf = won->text;
    const size_t cap = sizeof won->text;
    size_t o = 0;
    fs::File f;
    if (open_file(d->prof->overlay, f)) {
        library::FileSource src(f);
        for (int k = 0; k < wf.page_lines[page] && first + k < 24; ++k) {
            char line[text::kMaxString];
            if (!text::read_pascal(src, wf.text[first + k], line, sizeof line)) continue;
            for (const char* q = line; *q && o + 1 < cap; ++q) buf[o++] = *q;
        }
        f.close();
    }
    buf[o] = 0;
    text::begin(w, c, buf, text::kTextArea, 10, true);
    text::step(w, c, d->font, -1);
    dirty_rows(17, 22);
}

void won_prompt(pic::Canvas& c)
{
    won->stage = WonRun::Key;
    clear_menu_line(c);
    put(c, d->press_key, 0, text::kMenuRow, 13);
}

// After the text (and its window full): the page's picture, or the key
void won_after_text(pic::Canvas& c)
{
    const auto& wf = d->prof->won;
    if (w.state == text::State::PageFull) {         // the window filled: a key first
        won_prompt(c);
        return;
    }
    switch (won->page) {
    case 1:
    case 2:
        // An animation played once (on black, at the event picture's place)
        c.fill(24, 24, 88, 88, 0);
        if (anim_start(wf.anim[won->page - 1]) && anim_block >= 0 && d->anim.frames > 1) {
            won->stage = WonRun::Anim;
            won->at = millis();
            return;
        }
        anim_stop();
        won_prompt(c);
        return;
    case 3: {
        // The picture faded (each colour by the program's table), held
        // (10 - game speed) x 2 draws' time, then the head and body
        c.fill(24, 24, 88, 88, 0);
        if (anim_start(wf.fade_pic)) {
            anim_stop();
            for (int y = 24; y < 112; ++y)
                for (int x = 24; x < 112; ++x) c.px[y * c.w + x] = won->fade[c.px[y * c.w + x] & 15];
            dirty_rows(3, 13);
        }
        int speed = vm->get(0x4BFC) & 0xFF;
        if (speed > 9) speed = 9;
        won->stage = WonRun::Hold;
        won->at = millis() + static_cast<uint32_t>((10 - speed) * 2) * 250;
        return;
    }
    case 5:
        won->stage = WonRun::Fireworks;
        won->next_rocket = millis() + 300;
        return;
    default:
        won_prompt(c);
        return;
    }
}

void won_page(int page, pic::Canvas& c)
{
    const auto& wf = d->prof->won;
    won->page = page;
    won->stage = WonRun::Text;
    if (page == 4) {
        // The head and body (no key before them)
        c.fill(24, 24, 88, 88, 0);
        draw_block(c, "HEAD", wf.head, 24, 24);
        draw_block(c, "BODY", wf.body, 24, 64);
        dirty_rows(3, 13);
    } else if (page == 5) {
        anim_stop();
        layout::outer(c, d->tables, d->frame_tiles);
        layout::bar(c, d->tables, d->frame_tiles, 16);
        draw_block(c, "BIGPIC", wf.bigpic, 8, 8);
        dirty(0, 17 * 8);
    }
    won_text(page, c);
    won_after_text(c);
}

// Everyone healed and back up, training for everyone (free), Begin
// stopped; the party menu
void won_finish(pic::Canvas& c)
{
    const auto& wf = d->prof->won;
    delete won;
    won = nullptr;
    anim_stop();
    for (int i = 0; i < pt->count; ++i) {
        uint8_t* r = pt->m[i].rec;
        r[0x1A4] = r[0x78];
        r[0x195] = party::Okay;
        r[0x196] = 1;
    }
    vm->set(wf.train_word, 0xFF);
    vm->set(wf.begin_word, static_cast<uint16_t>((vm->get(wf.begin_word) & 0xFF00) | 0xFF));
    game_won = true;
    waiting = false;
    clear_menu_line(c);
    c.clear(0);
    screen = Screen::PartyMenu;
    draw_party_menu(c);
}

void start_won(pic::Canvas& c)
{
    anim_stop();
    if (!won) won = new (std::nothrow) WonRun;
    if (!won) {
        won_finish(c);
        return;
    }
    // The fade's table (the program's data)
    for (int k = 0; k < 16; ++k) won->fade[k] = static_cast<uint8_t>(k);
    fs::File f;
    if (open_file(d->prof->program, f)) {
        library::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok)
            exepack::read(src, info, d->prof->data_base + d->prof->won.fade_table, won->fade, sizeof won->fade);
        f.close();
    }
    screen = Screen::Won;
    won_page(0, c);
}

// A key or tap during the ending
void won_key(pic::Canvas& c)
{
    if (!won) return;
    switch (won->stage) {
    case WonRun::Key:
        clear_menu_line(c);
        if (w.state == text::State::PageFull) {
            text::next_page(w, c);
            text::step(w, c, d->font, -1);
            dirty_rows(17, 22);
            won_after_text(c);
            return;
        }
        if (won->page + 1 < 6) won_page(won->page + 1, c);
        else won_finish(c);
        return;
    case WonRun::Fireworks:
        won->stop = true;               // (the original ends after the rocket in the air)
        return;
    default:
        return;                         // (the pictures play out)
    }
}

// The fireworks: a pixel drawn over the big picture, what was under kept
void fw_put(pic::Canvas& c, int x, int y, uint8_t col, uint8_t* under)
{
    *under = c.px[y * c.w + x];
    c.px[y * c.w + x] = col;
}
bool fw_on(int x, int y) { return x >= 8 && x < 312 && y >= 9 && y <= 64; }

void fireworks_step(pic::Canvas& c, uint32_t now)
{
    WonRun& r = *won;
    if (!r.rocket) {
        if (r.stop) {
            won_finish(c);
            return;
        }
        if (static_cast<int32_t>(now - r.next_rocket) < 0) return;
        r.rocket = true;
        r.burst = false;
        r.rx = 65 * 32;
        r.ry = 65 * 32;
        r.rvx = 34 + rng.roll(20, 1);
        r.rvy = -(49 + rng.roll(5, 1));
        r.steps = 0;
        r.n_trail = 0;
        r.step_at = now;
    }
    if (static_cast<int32_t>(now - r.step_at) < 15) return;
    r.step_at = now;
    if (!r.burst) {
        // Up, a trail of colours 8-14 behind
        r.rx += r.rvx;
        r.ry += r.rvy;
        ++r.rvy;
        const int x = r.rx >> 5, y = r.ry >> 5;
        if (fw_on(x, y) && r.n_trail < 64) {
            WonRun::Px& p = r.trail[r.n_trail++];
            p.x = static_cast<int16_t>(x);
            p.y = static_cast<int16_t>(y);
            fw_put(c, x, y, static_cast<uint8_t>(7 + rng.roll(7, 1)), &p.under);
        }
        if (++r.steps >= 60) {
            // The burst: 3 groups of 40 sparks, 1 or 2 of them coloured 2-6, the rest 1
            const int coloured = rng.roll(2, 1);
            for (int g = 0; g < 3; ++g) {
                const uint8_t col = g < coloured ? static_cast<uint8_t>(1 + rng.roll(5, 1)) : 1;
                for (int k = 0; k < 40; ++k) {
                    WonRun::Spark& s = r.spark[g * 40 + k];
                    s.x = static_cast<int16_t>(r.rx);
                    s.y = static_cast<int16_t>(r.ry);
                    s.vx = static_cast<int16_t>(rng.roll(65, 1) - 33);
                    s.vy = static_cast<int16_t>(rng.roll(65, 1) - 37);
                    s.col = col;
                    s.drawn = false;
                }
            }
            r.burst = true;
            r.steps = 0;
        }
    } else {
        // The sparks: off their old pixels (last drawn first), on to the new
        for (int k = 119; k >= 0; --k) {
            WonRun::Spark& s = r.spark[k];
            if (s.drawn) c.px[(s.y >> 5) * c.w + (s.x >> 5)] = s.under;
            s.drawn = false;
        }
        if (++r.steps < 40) {
            for (int k = 0; k < 120; ++k) {
                WonRun::Spark& s = r.spark[k];
                s.x = static_cast<int16_t>(s.x + s.vx);
                s.y = static_cast<int16_t>(s.y + s.vy);
                ++s.vy;
                const int x = s.x >> 5, y = s.y >> 5;
                if (!fw_on(x, y)) continue;
                fw_put(c, x, y, s.col, &s.under);
                s.drawn = true;
            }
        } else {
            // Over: the trail goes too
            for (int k = r.n_trail - 1; k >= 0; --k) c.px[r.trail[k].y * c.w + r.trail[k].x] = r.trail[k].under;
            r.n_trail = 0;
            r.rocket = false;
            r.next_rocket = now + 200 + static_cast<uint32_t>(rng.roll(1000, 1));
        }
    }
    dirty(8, 66);
}

void won_tick(uint32_t now, pic::Canvas& c)
{
    if (!won) return;
    switch (won->stage) {
    case WonRun::Anim: {
        const uint32_t delay = d->anim.delay[anim_frame] ? d->anim.delay[anim_frame] * 100u : 100u;
        if (now - won->at < delay) return;
        won->at = now;
        if (anim_frame + 1 >= d->anim.frames) {     // once round
            anim_stop();
            won_prompt(c);
            return;
        }
        anim_draw(++anim_frame);
        return;
    }
    case WonRun::Hold:
        if (static_cast<int32_t>(now - won->at) >= 0) won_page(4, c);
        return;
    case WonRun::Fireworks:
        fireworks_step(c, now);
        return;
    default:
        return;
    }
}

// The demo: the party menu skipped, area 1, speed 9, ECL1 block 0x52's
// first-run entry
void start_demo(pic::Canvas& c)
{
    delete title_run;
    title_run = nullptr;
    menu_on = false;
    in_demo = true;
    while (pt->count) leave_party(pt->count - 1);
    ecl::GameState& gs = d->gs;
    gs.game_area = 1;
    vm->set(0x7F12, 1);
    vm->set(0x4BFC, 9);
    vm->set(0x4BE6, 1);
    gs.x = 7;
    gs.y = 13;
    gs.dir = 2;
    gs.script = 0x52;
    d->loaded = false;
    c.clear(0);
    screen = Screen::Game;
    if (!host->load_script(gs.script, gs.code, &gs.code_len) || !vm->init_script(false)) {
        Serial.println("[play] the demo's script didn't load");
        end_demo(c);
        return;
    }
    draw_frame(c);
    draw_panel(c);
    run_entry(4, Then::Begun);
}

bool start_title(pic::Canvas& c, uint32_t timeout_ms);
void end_demo(pic::Canvas& c);

// The player's tap or key while the demo runs: it stops the demo (an engine
// comfort, features.h CYD_DEMO_TAP_STOPS; the original ignores it). True: taken.
bool demo_input(pic::Canvas& c)
{
    if (!in_demo || auto_tap) return false;
#if CYD_DEMO_TAP_STOPS
    if (fg) {
        // A fight in the demo: gone, as at the demo's own end
        end_fight_art();
        fight_clear_monsters();
        palette_fight(false);
        ground->clear();
        delete fg;
        fg = nullptr;
    }
    end_demo(c);
#else
    (void)c;
#endif
    return true;
}

// After the demo: the party goes, the game's state as at the start, the
// title again with a 10 s version line
void end_demo(pic::Canvas& c)
{
    in_demo = false;
    while (pt->count) leave_party(pt->count - 1);
    anim_stop();
    waiting = false;
    then = Then::Idle;
    ecl::GameState& gs = d->gs;
    gs.game_area = d->prof->start_area;
    vm->set(0x7F12, gs.game_area);
    vm->set(0x4BFC, 4);
    vm->set(0x7F70, 0);
    vm->set(0x7F71, 0);
    vm->set(0x7ECB, 0);
    gs.x = 7;
    gs.y = 13;
    gs.dir = 0;
    area_view = false;
    pic_shown = false;
    anim_block = bigpic = last_pic = -1;
    if (!start_title(c, 10000)) {
        screen = Screen::PartyMenu;
        draw_party_menu(c);
    }
}

// Starts the title (false: this game has none - straight to the party menu)
bool start_title(pic::Canvas& c, uint32_t timeout_ms)
{
    const profile::Profile& p = *d->prof;
    if (!p.title_file || p.title_steps <= 0) return false;
    title_run = new (std::nothrow) TitleRun;
    if (!title_run) return false;
    title_run->timeout_ms = timeout_ms;
    fs::File f;
    if ((p.title_version || p.title_menu) && open_file(p.program, f)) {
        library::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok) {
            if (p.title_version) text::read_pascal(src, info, p.title_version, title_run->version, sizeof title_run->version);
            if (p.title_menu) text::read_pascal(src, info, p.title_menu, title_run->words, sizeof title_run->words);
        }
        f.close();
    }
    if (open_file(p.overlay, f)) {
        library::FileSource osrc(f);
        if (osrc.size() == p.overlay_size)
            title_run->n_credits = printcalls::read(osrc, p.credits_at, p.credits_base, title_run->credits, kTitleCredits);
        f.close();
    }
    screen = Screen::Title;
    c.clear(0);
    title_show(0, c);
    return true;
}

} // namespace

bool available(games::Game g) { return profile::program_name(g) != nullptr; }

bool icon_editing() { return d && screen == Screen::Icon && ie; }

bool icon_preview(bool action, uint8_t* out)
{
    if (!icon_editing()) return false;
    const Pic4& ic = ie->new_ic;
    const uint8_t* p = ic.px[action ? 1 : 0];
    if (!p || ic.w != kSq || ic.h != kSq) return false;
    for (int y = 0; y < kSq; ++y)
        for (int x = 0; x < kSq; ++x) out[y * kSq + x] = static_cast<uint8_t>(nib(p, kSq, x, y));
    return true;
}

void set_pool_dir(const char* dir)
{
    strncpy(pool_dir, dir ? dir : "", sizeof pool_dir - 1);
    pool_dir[sizeof pool_dir - 1] = 0;
}

// "Not enough memory" with the numbers (for Tom's reports)
const char* no_memory(const char* what)
{
    snprintf(msg, sizeof msg, "Not enough memory for %s (free %u KB, largest block %u KB).", what,
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    Serial.printf("[play] %s\n", msg);
    return msg;
}

void set_sound(uint8_t mode, uint8_t volume)
{
    snd_mode = mode;
    snd_volume = volume;
    if (mode != 1 && mode != 2) audio_stop();
}

uint8_t sound_mode() { return snd_mode; }

void sound_test(int id) { sfx(id); }

const char* open(const char* data_dir, games::Game g, pic::Canvas& c, const char* cache_dir)
{
    close();
    cv = &c;
    const char* prog = profile::program_name(g);
    if (!prog) {
        snprintf(msg, sizeof msg, "No play test for %s yet.", games::title(g));
        return msg;
    }
    if (!sd_begin()) return "No SD card found.";
    {
        view3d::World* w = new (std::nothrow) view3d::World;
        ecl::GameState* g = new (std::nothrow) ecl::GameState;
        d = w && g ? new (std::nothrow) Data(*w, *g) : nullptr;
        if (!d) {
            delete w;
            delete g;
            return no_memory("the game state");
        }
    }
    strncpy(d->data_dir, data_dir, sizeof d->data_dir - 1);
    if (cache_dir) strncpy(d->cache_dir, cache_dir, sizeof d->cache_dir - 1);

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
    ground = new (std::nothrow) items::Ground;
    if (!host || !pt || !names || !ground) {
        close();
        return no_memory("the party");
    }
    vm = new (vm_mem) ecl::Vm(d->gs, *host, *d->prof->ecl_ops);
    vm->set_party(pt);
    vm->set_ground(ground);
    vm->set_move_affects(d->prof->fight.facts.haste, d->prof->fight.facts.slow);
    if (open_file(d->prof->overlay, f)) {
        // The script machine's own words
        library::FileSource src(f);
        for (int i = 0; i < ecl::kScriptWords; ++i) {
            if (d->prof->script_words[i])
                text::read_pascal(src, d->prof->script_words[i], d->script_word[i], sizeof d->script_word[i]);
            d->script_words[i] = d->script_word[i];
        }
        f.close();
    }
    vm->set_words(d->script_words);
    vm->set(0x4BFF, 3);                     // Pics and Animation on, as the game starts (a save says its own)
    load_sound();
    note_until = 0;
    last_pic_id = -1;
    ecl::GameState& gs = d->gs;
    gs.game_area = d->prof->start_area;
    gs.x = 7;
    gs.y = 13;
    gs.dir = 0;
    vm->set(0x7F12, gs.game_area);
    area_view = false;
    pic_shown = false;
    look_pending = false;
    rest_encounter_steps = 0;
    waiting = false;
    then = Then::Idle;
    anim_block = bigpic = last_pic = -1;
    jtext[0] = 0;
    journal_kind = 0;
    journal_due = false;
    n_seen = 0;
    cursor_on = false;
    input_mode = Input::None;
    idle_cycles = 0;
    exit_wanted = false;
    w = text::Writer{};
    // The title sequence first, as the games begin; then the party menu
    if (!start_title(c, 30000)) {
        screen = Screen::PartyMenu;
        draw_party_menu(c);
    }
    return nullptr;
}

void close()
{
    audio_stop();
    delete snd;
    snd = nullptr;
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
    delete names;
    names = nullptr;
    delete ground;
    ground = nullptr;
    delete mk;
    mk = nullptr;
    delete title_run;
    title_run = nullptr;
    end_icon_art();
    new_char = nullptr;
    input_engine = false;
    // Whatever was open goes (the heap is the viewer's again), and the
    // next Play Test starts clean
    if (fg) {
        end_fight_art();
        delete fg;
        fg = nullptr;
    }
    fight_clear_monsters();
    end_magic();
    delete hw;
    hw = nullptr;
    delete trainer;
    trainer = nullptr;
    delete modder;
    modder = nullptr;
    delete won;
    won = nullptr;
    in_game_menu = camp_from_script = game_won = items_direct = false;
    waiting = resume_after_note = note_held = false;
    note_until = 0;
    then = Then::Idle;
    new_base[0] = 0;
    if (d) {
        view3d::World* w = &d->world;
        ecl::GameState* g = &d->gs;
        delete d;
        delete w;
        delete g;
    }
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
    if (demo_input(c)) return true;
    if (screen == Screen::Fight) return fight_act(a, c);
    if (screen == Screen::Modify) return modify_act(a, c);
    if (screen == Screen::Title && title_run && !title_run->prompt) {
        title_next(c);                      // any key skips the wait, as in the games
        return true;
    }
    if (screen == Screen::Won) {
        won_key(c);
        return true;
    }
    if (screen != Screen::Game) return false;
    if (waiting) {
        // Any key goes on, like the games' "press a key": the rest of the
        // page, the next page, a one-choice menu
        tap(0, 0, c);
        return true;
    }
    if (then == Then::Door) {
        door_choice('E');                   // a key at the door: as Exit
        return true;
    }
    if (then != Then::Idle) return false;
    ecl::GameState& g = d->gs;
    switch (a) {
    case Act::TurnLeft:   g.dir = (g.dir + 6) & 7; sfx(sound::kStep); break;
    case Act::TurnRight:  g.dir = (g.dir + 2) & 7; sfx(sound::kStep); break;
    case Act::TurnAround: g.dir = (g.dir + 4) & 7; break;
    case Act::Forward:    step(g.dir); return true;
    case Act::StepLeft:   step((g.dir + 6) & 7); return true;     // Tom: the same checks as a step that way
    case Act::StepRight:  step((g.dir + 2) & 7); return true;
    case Act::Area:
        if (vm->get(0x4BFB) == 0) area_view = !area_view;
        pic_shown = false;
        break;
    case Act::Look: {
        // Search this square (+10 minutes); the search script runs with the
        // search word 1 - the scripts look for 1 - and search mode is put
        // back as it was after (coab's facts: sub_29758's Look)
        look_search = vm->get(0x7ECA) & 1;
        look_pending = true;
        vm->set(0x7ECA, 1);
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

// ---- the cursor keys (Tom, 2026-10-10) ---------------------------------------------
// The games' arrow keys and Enter, for the highlighted thing on screen:
// Up / Down a list's highlight, Left / Right the menu line's (Up / Down
// too when there's no list); Select is a tap on the highlighted thing
// (nav_point), so it does exactly what a tap does.

bool plist_screen()
{
    return screen == Screen::Items || screen == Screen::ShopBuy || screen == Screen::AddList ||
           screen == Screen::CreatePick || screen == Screen::Heal || screen == Screen::Take || screen == Screen::Loot;
}

void plist_redraw(pic::Canvas& c)
{
    if (screen == Screen::CreatePick) draw_pick(c);
    else if (screen == Screen::ShopBuy) draw_buy(c);
    else if (screen == Screen::Loot) draw_loot(c);
    else if (screen == Screen::AddList) draw_add_list(c);
    else if (screen == Screen::Heal) draw_heal(c);
    else if (screen == Screen::Take) draw_take(c);
    else draw_items(c);
}

// The menu line's choice kept across a redraw that builds it again
void keep_menu_choice(int sel, pic::Canvas& c)
{
    if (menu_on && sel > 0 && sel < menu.count && menu.selected != sel) {
        menu.selected = sel;
        show_menu_line(c);
    }
}

// Up / Down on a list: true when there is one here
bool nav_list(int step, pic::Canvas& c)
{
    const int keep = menu.selected;
    if (screen == Screen::Game) {
        if (!waiting) return false;
        if (vm->wait() == ecl::Wait::ListMenu && list_wait) {
            int n = vm->items();
            if (list_row0 + n - 1 > text::kTextArea.y1) n = text::kTextArea.y1 - list_row0 + 1;
            if (n <= 0) return false;
            list_sel = (list_sel + step + n) % n;
            draw_list_menu(c);
            return true;
        }
        if (vm->wait() == ecl::Wait::Who && pt->count && !bigpic_shown()) {
            pt->selected = (pt->selected + step + pt->count) % pt->count;
            draw_party(c, 17);
            return true;
        }
        return false;
    }
    if (screen == Screen::PartyMenu) {
        if (!pm_lines) return false;
        pm_sel = (pm_sel + step + pm_lines) % pm_lines;
        draw_party_menu(c);
        return true;
    }
    if (screen == Screen::TradeWho && pt->count) {
        pt->selected = (pt->selected + step + pt->count) % pt->count;
        draw_party(c, 1);
        return true;
    }
    if (screen == Screen::Modify && modder) {
        mod_item = (mod_item + step + 8) % 8;
        draw_modify(c);
        return true;
    }
    if (plist_screen()) {
        PickList& l = plist;
        const int lo = screen == Screen::CreatePick ? 1 : 0;
        if (l.n <= lo) return false;
        int i = l.index + step;
        if (i < lo) i = l.n - 1;
        if (i >= l.n) i = lo;
        l.index = i;
        plist_redraw(c);
        keep_menu_choice(keep, c);
        return true;
    }
    if (screen == Screen::SpellList && !sl.learning && sl.sel >= 0) {
        int i = sl.sel;
        for (int k = 0; k < sl.n; ++k) {
            i = (i + step + sl.n) % sl.n;
            if (sl.id[i]) break;
        }
        sl.sel = i;
        const int rows = list_rows();
        while (sl.sel < sl.top) sl.top = sl.top >= rows ? sl.top - rows : 0;
        while (sl.sel >= sl.top + rows) sl.top += rows;
        draw_spells(c);
        keep_menu_choice(keep, c);
        return true;
    }
    return false;
}

// Screens with the party list beside a menu line (exploring, camp, shop,
// magic, a spell's target, Alter): Up / Down move the party's highlight,
// as a tap on the next line would; Left / Right stay on the menu line
// (Tom, v0.52.0)
bool party_nav(int step, pic::Canvas& c)
{
    if (!pt || pt->count < 2) return false;
    int col = -1;
    switch (screen) {
    case Screen::Game:
        if (!waiting && then == Then::Idle && !bigpic_shown()) col = 17;
        break;
    case Screen::Camp:
    case Screen::Magic:
        if (bigpic < 0) col = 17;
        break;
    case Screen::Shop: col = 17; break;
    case Screen::Cast: col = cr.on_camp ? 17 : 1; break;
    case Screen::Alter:
        if (alter_mode != AlterMode::Speed && bigpic < 0) col = 17;   // Place: moves them a line
        break;
    default: break;
    }
    if (col < 0) return false;
    const int to = (pt->selected + step + pt->count) % pt->count;
    tap(col * 8 + 4, (4 + to) * 8 + 2, c);
    return true;
}

bool nav(Nav n, pic::Canvas& c)
{
    if (!d || input_mode != Input::None) return false;
    cv = &c;
    if (demo_input(c)) return true;
    const bool first = !keys_used;
    if (screen == Screen::Won) {
        won_key(c);
        return true;
    }
    keys_used = true;
    if (first && screen == Screen::PartyMenu) {
        draw_party_menu(c);                 // the highlight shows from the first key on
        if (n == Nav::Up || n == Nav::Down) return true;
    }
    if (note_until && !note_held) redraw_menu(c);
    const int step = n == Nav::Up || n == Nav::Left ? -1 : 1;
    // Modify Character: left / right change the value (the games' arrows)
    if (screen == Screen::Modify && (n == Nav::Left || n == Nav::Right)) {
        modify_step(step, c);
        return true;
    }
    if ((n == Nav::Up || n == Nav::Down) && (nav_list(step, c) || party_nav(step, c))) return true;
    // The party menu has no menu line: left / right pick the character
    if (screen == Screen::PartyMenu && (n == Nav::Left || n == Nav::Right)) {
        if (pt->count < 2) return false;
        pt->selected = (pt->selected + step + pt->count) % pt->count;
        draw_party_menu(c);
        return true;
    }
    if (menu_on && menu.count > 1) {
        menu.selected = ((menu.selected < 0 ? 0 : menu.selected) + step + menu.count) % menu.count;
        show_menu_line(c);
        return true;
    }
    return false;
}

bool walking()
{
    if (!d || input_mode != Input::None) return false;
    if (screen == Screen::Fight) return fg && (fg->st == FSt::Move || (fg->st == FSt::Aim && fg->manual));
    return screen == Screen::Game && !waiting && then == Then::Idle && vm->get(0x4BE6) != 0;
}

bool nav_point(int* x, int* y)
{
    if (!d || input_mode != Input::None) return false;
    keys_used = true;
    auto at_word = [&]() {
        const int sel = menu.selected >= 0 && menu.selected < menu.count ? menu.selected : 0;
        *x = (static_cast<int>(strlen(menu.prompt)) + menu.start[sel]) * 8 + 2;
        *y = text::kMenuRow * 8 + 2;
        return true;
    };
    auto anywhere = [&]() {                 // "press a key": a tap on the text
        *x = 8 * 2;
        *y = text::kTextArea.y0 * 8 + 2;
        return true;
    };
    if (note_until) {
        *x = 2;
        *y = text::kMenuRow * 8 + 2;
        return true;
    }
    if (screen == Screen::Game) {
        if (waiting) {
            switch (vm->wait()) {
            case ecl::Wait::ListMenu:
                if (!list_wait) return anywhere();
                *x = 8 + 2;
                *y = (list_row0 + list_sel) * 8 + 2;
                return true;
            case ecl::Wait::Menu:
                if (vm->items() == 1) return anywhere();
                return menu_on && menu.count ? at_word() : anywhere();
            case ecl::Wait::Who:
                return menu_on && menu.count ? at_word() : false;
            default:
                return anywhere();
            }
        }
        return menu_on && menu.count ? at_word() : false;
    }
    if (screen == Screen::PartyMenu) {
        if (!pm_lines) return false;
        *x = 3 * 8 + 2;
        *y = (12 + pm_sel) * 8 + 2;
        return true;
    }
    if (menu_on && menu.count) return at_word();
    *x = 2;
    *y = text::kMenuRow * 8 + 2;            // a tap anywhere (a page, "press a key")
    return true;
}

// ---- tap highlight (Tom, 2026-10-09) ---------------------------------------------
// What a tap will act on - a menu line word, a list line, a party menu
// line - lights up (its letters in the highlight colour) before the tap is
// acted on; one already lit blinks off and on. Put back afterwards unless
// something was drawn there meanwhile.

namespace {

struct Flash {
    int x0 = 0, y0 = 0, w = 0, h = 0;
    uint8_t* orig = nullptr;
};
Flash fl;

// The cells (row, first and last column) a tap at canvas (x, y) acts on
bool tap_target(int x, int y, int* row, int* c0, int* c1)
{
    const int r = y / 8, col = x / 8;
    if (note_until) return false;                   // the tap puts a message away
    auto menu_word = [&](int item) {
        if (item < 0 || item >= menu.count) return false;
        const int p = static_cast<int>(strlen(menu.prompt));
        *row = text::kMenuRow;
        *c0 = p + menu.start[item];
        *c1 = p + menu.end[item];
        return true;
    };
    // "press a key" (one choice): a tap anywhere is it
    if (menu_on && screen == Screen::Game && waiting && vm->wait() == ecl::Wait::Menu && vm->items() == 1)
        return menu_word(0);
    if (menu_on && y >= text::kMenuTapTop) return menu_word(text::hit(menu, col));
    if (screen == Screen::Modify) {
        const int i = modify_item_at(r, col);
        if (i < 0 || input_mode != Input::None) return false;
        *row = r;
        *c0 = 1;
        *c1 = i < 6 ? 10 : i == 6 ? 6 : 16;
        return true;
    }
    if (screen == Screen::Game) {
        if (waiting && vm->wait() == ecl::Wait::ListMenu && list_wait) {
            const int i = r - list_row0;
            if (r < text::kTextArea.y0 || r > text::kTextArea.y1 || i < 0 || i >= vm->items()) return false;
            *row = r;
            *c0 = text::kTextArea.x0;
            *c1 = text::kTextArea.x1;
            return true;
        }
        // The party beside the view (exploring, or WHO): a tap picks one
        if ((!waiting || vm->wait() == ecl::Wait::Who) && col >= 17 && r >= 4 && r < 4 + pt->count &&
            !bigpic_shown()) {
            *row = r;
            *c0 = 17;
            *c1 = 38;
            return true;
        }
        return false;
    }
    if (screen == Screen::PartyMenu && r >= 12 && r < 12 + pm_lines) {
        *row = r;
        *c0 = 1;
        *c1 = 38;
        return true;
    }
    // The party menu's characters, Trade's party (a tap picks one)
    if ((screen == Screen::PartyMenu || screen == Screen::TradeWho) && col >= 1 && r >= 4 && r < 4 + pt->count) {
        *row = r;
        *c0 = 1;
        *c1 = 38;
        return true;
    }
    if ((screen == Screen::Items || screen == Screen::ShopBuy || screen == Screen::AddList ||
         screen == Screen::CreatePick || screen == Screen::Heal || screen == Screen::Take || screen == Screen::Loot) &&
        r >= plist.row0 && r <= plist.row1 && plist.top + r - plist.row0 < plist.n) {
        *row = r;
        *c0 = plist.col0;
        *c1 = 38;
        return true;
    }
    // The spell lists' spells
    if (screen == Screen::SpellList && !sl.learning && r >= 5 && r < 5 + list_rows() && sl.top + r - 5 < sl.n &&
        sl.id[sl.top + r - 5]) {
        *row = r;
        *c0 = 3;
        *c1 = 38;
        return true;
    }
    // The party list on the camp screens (and "Cast Spell on whom")
    const bool whom = screen == Screen::Cast && cr.stage == CastRun::Whom;
    const int pc0 = whom && !cr.on_camp ? 1 : 17;
    if ((screen == Screen::Camp || screen == Screen::Magic ||
         (screen == Screen::Alter && alter_mode != AlterMode::Speed) || whom) && (bigpic < 0 || (whom && !cr.on_camp)) &&
        col >= pc0 && r >= 4 && r < 4 + pt->count) {
        *row = r;
        *c0 = pc0;
        *c1 = 38;
        return true;
    }
    return false;
}

void ink(pic::Canvas& c, uint8_t colour)
{
    for (int j = 0; j < fl.h; ++j) {
        uint8_t* p = c.px + static_cast<size_t>(fl.y0 + j) * c.w + fl.x0;
        const uint8_t* o = fl.orig + static_cast<size_t>(j) * fl.w;
        for (int i = 0; i < fl.w; ++i) p[i] = o[i] ? colour : 0;
    }
}

} // namespace

bool tap_highlight(int x, int y, pic::Canvas& c, int* y0, int* y1)
{
    tap_highlight_end(c);
    if (!d) return false;
    int row, c0, c1;
    if (!tap_target(x, y, &row, &c0, &c1)) return false;
    if (c0 < 0) c0 = 0;
    if (c1 > 39) c1 = 39;
    if (c1 < c0) return false;
    fl.x0 = c0 * 8;
    fl.y0 = row * 8;
    fl.w = (c1 - c0 + 1) * 8;
    fl.h = 8;
    fl.orig = static_cast<uint8_t*>(malloc(static_cast<size_t>(fl.w) * fl.h));
    if (!fl.orig) return false;
    bool lit = true, any = false;
    for (int j = 0; j < fl.h; ++j) {
        const uint8_t* p = c.px + static_cast<size_t>(fl.y0 + j) * c.w + fl.x0;
        memcpy(fl.orig + static_cast<size_t>(j) * fl.w, p, fl.w);
        for (int i = 0; i < fl.w; ++i)
            if (p[i]) {
                any = true;
                if (p[i] != 15) lit = false;
            }
    }
    if (!any) {
        free(fl.orig);
        fl.orig = nullptr;
        return false;
    }
    *y0 = fl.y0;
    *y1 = fl.y0 + fl.h;
    if (lit) {
        // already lit: off, then on again (the viewer shows each step)
        ink(c, 0);
        return true;
    }
    ink(c, 15);
    fb_y0 = fl.y0;
    fb_y1 = fl.y0 + fl.h;
    fb_touched = false;
    return true;
}

bool tap_highlight_blink(pic::Canvas& c)
{
    if (!fl.orig || fb_y0 >= 0) return false;      // nothing blinking
    ink(c, 15);
    fb_y0 = fl.y0;
    fb_y1 = fl.y0 + fl.h;
    fb_touched = false;
    return true;
}

void tap_highlight_end(pic::Canvas& c)
{
    if (!fl.orig) return;
    if (fb_y0 >= 0 && !fb_touched) {
        for (int j = 0; j < fl.h; ++j)
            memcpy(c.px + static_cast<size_t>(fl.y0 + j) * c.w + fl.x0, fl.orig + static_cast<size_t>(j) * fl.w, fl.w);
        const int a = fl.y0, b = fl.y0 + fl.h;
        fb_y0 = fb_y1 = -1;
        dirty(a, b);
    }
    fb_y0 = fb_y1 = -1;
    free(fl.orig);
    fl.orig = nullptr;
}

void tap(int x, int y, pic::Canvas& c)
{
    if (!d) return;
    cv = &c;
    if (demo_input(c)) return;
    if (screen != Screen::Game) {
        pm_tap(x, y, c);
        return;
    }
    if (note_until && note_held) {      // an error: the tap puts it away first
        redraw_menu(c);
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
        case ecl::Wait::Who:
            if (col >= 17 && row >= 4 && row < 4 + pt->count && !bigpic_shown()) {
                pt->selected = row - 4;
                draw_party(c, 17);
            } else if (y >= text::kMenuTapTop && text::key(menu, text::hit(menu, col)) == 'S') {
                clear_menu_line(c);
                waiting = false;
                handle(vm->answer(pt->selected));
            }
            break;
        case ecl::Wait::Key:
            clear_menu_line(c);
            waiting = false;
            handle(vm->resume());
            break;
        default:
            break;
        }
        return;
    }
    if (then == Then::Door) {
        if (y >= text::kMenuTapTop) {
            const int k = text::hit(menu, x / 8);
            if (k >= 0) {
                menu.selected = k;
                show_menu_line(c);
                door_choice(text::key(menu, k));
            }
        }
        return;
    }
    if (then != Then::Idle) return;
    // A tap on the 3D view switches between it and the Area view (the
    // menu line's Area does the same)
    if (x >= 24 && x < 112 && y >= 24 && y < 112 && vm->get(0x4BE6) && !pic_shown && !bigpic_shown()) {
        act(Act::Area, c);
        return;
    }
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
        case 'E':
            // As the games encamp: the area's before-camp script first
            // (entry 2), then the camp; an interrupted rest runs entry 3
            run_entry(2, Then::Camp);
            break;
        case 'C': open_cast_exploring(c); break;
        default: break;
        }
    }
}

void tick(uint32_t now, pic::Canvas& c)
{
    if (!d) return;
    // Effects run out as game time passes (walking, searching, scripts)
    if (vm && pt) {
        // Detect Magic on anyone: magic and cursed items' names get "* "
        if (names && d->prof->items.detect[0]) {
            bool on = false;
            for (int i = 0; i < pt->count && !on; ++i) on = pt->m[i].has_affect(d->prof->items.detect[0]);
            names->detect = on;
        }
        effects_clock(vm->take_minutes(), c);
    }
    if (note_until && !note_held && static_cast<int32_t>(now - note_until) >= 0) {
        cv = &c;
        redraw_menu(c);
    }
    tick_screens(now, c);
}

namespace {

// Effects run out as `m` minutes pass (exploring, resting): the repeating
// ones first (poison, Constitution's healing), then the rest; the stats
// follow when one ends
void effects_clock(int m, pic::Canvas& c)
{
    if (!vm || !pt || m <= 0) return;
    {
        for (int i = 0; m && i < pt->count; ++i) {
            const int before = pt->m[i].n_affects;
            if (rules::poison_clock(pt->m[i], m, d->prof->cures)) {
                // "NAME dies from poison" on the menu line
                char nm[20], w[24], t[48];
                pt->m[i].name(nm, sizeof nm);
                ow(d->prof->fight.words[profile::kDiesFromPoison], w, sizeof w);
                snprintf(t, sizeof t, "%s %s", nm, w);
                cv = &c;
                note(c, t);
            }
            if (rules::con_regen(pt->m[i], m, d->facts)) {
                // Constitution 20+: "NAME is fully healed" / "is partially healed"
                char nm[20], w[24], t[48];
                pt->m[i].name(nm, sizeof nm);
                cw(pt->m[i].hp() >= pt->m[i].hp_max() ? profile::kFullyHealed : profile::kPartlyHealed, w, sizeof w);
                snprintf(t, sizeof t, "%s %s", nm, w);
                cv = &c;
                note(c, t);
            }
            magic::tick_affects(pt->m[i], m);
            if (pt->m[i].n_affects != before && names) {    // one ran out
                rules::keep_hammer(pt->m[i], *names, d->facts);
                rules::recalc(pt->m[i], *names, d->facts);
            }
        }
    }
}

// What each screen does as time passes
void tick_screens(uint32_t now, pic::Canvas& c)
{
    if (screen == Screen::Rest) {
        cv = &c;
        rest_tick(now, c);
        return;
    }
    if (screen == Screen::Title) {
        cv = &c;
        title_tick(now, c);
        return;
    }
    if (screen == Screen::Won) {
        cv = &c;
        won_tick(now, c);
        return;
    }
    if (screen == Screen::Cast) {
        cv = &c;
        cast_tick(now, c);
        return;
    }
    if (screen == Screen::Fight) {
        cv = &c;
        fight_tick(now, c);
        return;
    }
    if (!waiting || screen != Screen::Game) return;
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
    // The demo: a key read doesn't wait (a full page, "press a key", a
    // one-word menu go straight on)
    if (in_demo && (wt == ecl::Wait::Key || (wt == ecl::Wait::Print && page_prompt) ||
                    (wt == ecl::Wait::Menu && vm->items() == 1))) {
        auto_tap = true;
        tap(0, 0, c);
        auto_tap = false;
        return;
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

} // namespace

Input input() { return d ? input_mode : Input::None; }

// Esc on the camp's magic screens: a step back
bool back_from_magic(pic::Canvas& c)
{
    switch (screen) {
    case Screen::Magic:
        screen = Screen::Camp;
        draw_camp(c);
        return true;
    case Screen::SpellList:
        if (sl.reading >= 0) {
            reading_tap('E', c);                // a scroll's spells (Use): as Exit
            return true;
        }
        if (sl.fighting) {
            fight_spell_chosen(0, c);
            return true;
        }
        if (sl.choosing) return true;          // a spell must be chosen
        if (sl.casting) cast_done(c);
        else if (sl.scrolls) scribe_tap('E', c);
        else if (magic::memorizing(*pt->sel())) confirm_memorize(c, profile::kMemorizeThese2);
        else back_to_magic(c);
        return true;
    case Screen::Cast:
        if (cr.stage == CastRun::Whom) show_memory(c);
        else if (cr.stage == CastRun::Flame) flame_ask(c, true);
        else if (cr.stage == CastRun::FlameAbort) flame_ask(c, false);
        return true;
    case Screen::Effects:
        end_effects();
        back_to_magic(c);
        return true;
    case Screen::Alter:
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
        if (alter_mode == AlterMode::Menu) {
            screen = Screen::Camp;
            draw_camp(c);
        } else {
            alter_menu(c);
        }
        return true;
    case Screen::Rest:
        if (rest.running) {
            char q[20];
            mw(profile::kStopResting, q, sizeof q);
            ask_yes_no(c, Ask::StopRest, q);
        } else {
            end_rest(c);
        }
        return true;
    default: return false;
    }
}

bool back(pic::Canvas& c)
{
    if (!d) return false;
    cv = &c;
    if (demo_input(c)) return true;
    if (screen == Screen::Won) {
        won_key(c);
        return true;
    }
    if (screen == Screen::Icon) {
        icon_back(c);
        return true;
    }
    if (screen == Screen::Shop) {           // a shop, a temple, the treasure: leaving it (its questions)
        leave_shop(c);
        return true;
    }
    if (screen == Screen::Game && then == Then::Door) {
        door_choice('E');                   // "Locked.": Exit
        return true;
    }
    if (screen == Screen::LoadWhich) {
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return true;
    }
    if (input_mode != Input::None && input_engine && engine_ask == EngineAsk::FileName) {
        input_key('\n', c);                      // Esc ends the file name as Enter does (the original)
        return true;
    }
    if (screen == Screen::Modify) {
        if (input_mode != Input::None) {            // the name's keyboard: the name as it was
            input_mode = Input::None;
            input_engine = false;
            engine_ask = EngineAsk::Name;
            draw_modify(c);
        } else {
            end_modify(c, false);
        }
        return true;
    }
    if (screen == Screen::View) {
        back_from_view(c);
        return true;
    }
    if (screen == Screen::SaveWhich) {
        back_from_save(c);
        return true;
    }
    // Esc on a game question is its No; on the shop's own screens, back to the shop
    if (screen == Screen::YesNo && !mk) {
        const Ask what = ask;
        ask = Ask::None;
        if (items_yes_no(what, 'N', c) || shop_yes_no(what, 'N', c) || train_yes_no(what, 'N', c) ||
            magic_yes_no(what, 'N', c) || alter_yes_no(what, 'N', c))
            return true;
        ask = what;
    }
    if (screen == Screen::TradeWho) {
        back_to_items(c);
        return true;
    }
    if (screen == Screen::Heal) {
        leave_heal(c);
        return true;
    }
    if (screen == Screen::Appraise && appraised) {
        appraise_key('K', c);           // (Esc with one appraised: kept - sold when it can't be carried)
        return true;
    }
    if (screen == Screen::Take || screen == Screen::Appraise) {
        if (screen == Screen::Take && input_mode != Input::None) {
            input_mode = Input::None;
            input_engine = false;
            engine_ask = EngineAsk::Name;
            draw_take(c);
            return true;
        }
        screen = Screen::Shop;
        draw_shop(c);
        return true;
    }
    if (screen == Screen::YesNo && (ask == Ask::Overwrite || ask == Ask::OverwriteNew)) {
        yes_no_key('N', c);                 // (Esc: No - a new file name; no way back, as the original)
        return true;
    }
    if (mk && (screen == Screen::CreatePick || screen == Screen::CreateName || screen == Screen::YesNo)) {
        ask = Ask::None;
        end_create(c);
        return true;
    }
    if (screen == Screen::AddFrom || screen == Screen::AddList || screen == Screen::YesNo) {
        ask = Ask::None;
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return true;
    }
    if (screen == Screen::Camp) {
        leave_camp(c);
        return true;
    }
    if (back_from_magic(c)) return true;
    if (screen == Screen::Fight) return fight_back(c);
    if (screen == Screen::Loot) {
        screen = Screen::Shop;
        draw_shop(c);
        return true;
    }
    if (screen == Screen::Items || screen == Screen::ShopBuy) {
        if (screen == Screen::ShopBuy) {
            screen = Screen::Shop;
            draw_shop(c);
        } else if (items_direct) {
            items_direct = false;           // (a fight's Use: back to the fight, as Exit)
            fight_after_view(c);
        } else {
            screen = Screen::View;
            draw_character(c);
        }
        return true;
    }
    return false;
}

int journal_seen_count() { return d ? n_seen : 0; }

bool journal_seen(int i, char* kind, int* number)
{
    if (!d || i < 0 || i >= n_seen) return false;
    *kind = seen[i].kind;
    *number = seen[i].num;
    return true;
}

bool fight_colours() { return d && fight_palette_on; }

bool exit_requested()
{
    const bool e = exit_wanted;
    exit_wanted = false;
    return e;
}

bool journal_waiting() { return d && journal_due && journal_kind; }

bool journal_request(char* kind, int* number)
{
    if (!d || !journal_due) return false;
    *kind = journal_kind;
    *number = journal_num;
    journal_due = false;
    journal_kind = 0;
    return true;
}

void journal_failed(char kind, int number, const char* why, pic::Canvas& c)
{
    if (!d) return;
    journal_kind = kind;
    journal_num = number;
    journal_due = true;
    cv = &c;
    error(c, why);
}

void input_key(char k, pic::Canvas& c)
{
    if (!d || input_mode == Input::None) return;
    cv = &c;
    if (k == '\n' && input_engine) {
        if (!input_len) return;
        input_mode = Input::None;
        input_engine = false;
        clear_menu_line(c);
        if (engine_ask == EngineAsk::Coins) {
            engine_ask = EngineAsk::Name;
            take_coins(atoi(input_buf), c);
            return;
        }
        if (engine_ask == EngineAsk::ModName) {
            engine_ask = EngineAsk::Name;
            if (pt->sel()) create::set_name(*pt->sel(), input_buf);
            draw_modify(c);
            return;
        }
        if (engine_ask == EngineAsk::FileName) {
            engine_ask = EngineAsk::Name;
            size_t n = 0;
            for (; input_buf[n] && n + 1 < sizeof new_base; ++n)
                new_base[n] = static_cast<char>(toupper(static_cast<unsigned char>(input_buf[n])));
            new_base[n] = 0;
            if (name_for_new && mk) save_new(c, false);     // (that one there too: "Overwrite" again)
            else remove_character(c, false);
            return;
        }
        create_named(input_buf, c);
        return;
    }
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
                                                    : (k >= ' ' && k <= 'Z' && input_len < input_max);
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

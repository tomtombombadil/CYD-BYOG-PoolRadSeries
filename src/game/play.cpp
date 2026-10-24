#include "play.h"

#include "platform/platform.h"

#include <cstdlib>
#include <initializer_list>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/features.h"
#include "platform/file.h"
#include "engine/ecl.h"
#include "engine/ecl_vm.h"
#include "engine/exepack.h"
#include "engine/font.h"
#include "engine/icon_looks.h"
#include "engine/gbc_icons.h"
#include "engine/lightning.h"
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
#include "platform/audio.h"
#include "platform/memlog.h"

namespace play {

const char* no_memory(const char* what);   // (below: the message with the heap's numbers)
bool make_room(unsigned kb, const char* what);   // (below: drops what reads again until kb KB fit)
void tidy_memory();
bool fighting();                                 // (below: a fight is on)

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
    // Gateway: the experience for levels 2-12 of each class (its program's
    // class records, 8 x 11) - the party list shows who can train (null:
    // not Gateway)
    int32_t* gw_xp = nullptr;
    Data(view3d::World& w, ecl::GameState& g) : world(w), gs(g) {}
    ~Data()
    {
        delete ov;
        delete anim_idx;
        free(tr_frame);
        free(tr_pieces);
        free(tr_bg);
        free(tr_shade);
    }
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    layout::Tiles  frame_tiles;
    layout::Tables tables;
    // Treasures: its frame's tiles (BORD block 0, 64 x 64 bytes) and pieces
    // (BORD block 7) - layout::Tables tr
    uint8_t*       tr_frame = nullptr;
    uint8_t*       tr_pieces = nullptr;
    // Treasures in 256 colours: its background picture (BORD block 1,
    // 320 x 192 bytes; the canvas's bg) - nullptr in its EGA mode
    uint8_t*       tr_bg = nullptr;
    uint8_t      (*tr_shade)[8] = nullptr;  // (and its font's black pixels: font::Font shade)
    uint8_t        tr_over = 0;             // Treasures: the overland's OVER.DAX map (0: none)
    uint16_t       tr_max_age[8] = {};      // Treasures: the races' greatest ages (old age after a fight)
    font::Font     font;
    font::Font*    alt_font = nullptr;      // the player's Pools of Darkness font while it's chosen
    bool           alt_tried = false;       // (its file was looked for)
    // View Character's portrait box (view_box): Pool's portrait blocks for
    // heads 1-14 / bodies 1-12 (read once), what the box shows
    uint8_t        head_map[15] = {}, body_map[13] = {};
    bool           maps_read = false;
    bool           view_icon = false, view_action = false;
    view3d::World& world;
    geo::Map       map;
    ecl::GameState& gs;
    dax::Index     idx;
    uint8_t        sky[16] = {};
    char           press_key[text::kMaxString] = {};
    char           script_word[ecl::kScriptWords][48] = {};   // (Secret's longest: 43)
    const char*    script_words[ecl::kScriptWords] = {};
    char           data_dir[96] = {};
    const profile::Profile* prof = nullptr;

    // The event picture (a PIC animation): its file stays open while shown
    pic::Anim      anim;
    plat::File       anim_file;
    int16_t        anim_x = 24, anim_y = 24;   // where it's drawn (the view; Pool's ending: elsewhere)
    dax::Index*    anim_idx = nullptr;      // the PIC file's index while a picture animates (3 KB)
    pic::DkAnim*   dk_anim = nullptr;       // Darkness: its animation's header while one shows

    // A monster's sprite in the 3D view (SPRITn)
    pic::Anim      sprite;

    // Pool's wilderness map (read from the program when first shown), the rider's gallop
    overland::Data* ov = nullptr;
    uint8_t        gallop = 0;

    // The wilderness map's places (from the program)
    uint8_t        city_x[32] = {}, city_y[32] = {};
    int            cities = 0;

    // The party menu's words (from the program and GAME.OVR)
    static constexpr int kItems = 14;        // (Secret: 13 with Initialize Mouse/Joystick; Gateway 14 with Icon Select)
    char           item[kItems][41] = {};
    bool           item_on[kItems] = {};   // the program's own starting flags
    int            items = 0;
    char           choose[41] = {}, load_which[41] = {}, name_head[8] = {}, ac_hp_head[12] = {};
    char           load_from[24] = {}, load_sources[24] = {};      // (Darkness: "Load from where? Pools Secret Exit")
    char           save_dir[128] = {};      // the game's save folder (data_dir/SAVE)
    savegame::Header save;                 // the saved game loaded
    bool           loaded = false;
    char           cache_dir[128] = {};     // _CYD/<folder>: what the engine keeps beside a save
    gbc_icons::Choices icon_picks;          // the Companion icons picked, by name (_CYD/<folder>/ICONS.TXT)
    int            gbc_pairs = -1;          // the Companion's icon pairs on the card (-1: not looked yet)
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
    uint8_t        guy_src[kMaxGuys] = {};      // 0 the save folder's .GUY, 1 a Pool file there, 2 one in Pool's folder,
                                                // 3 a Curse .GUY in Secret's folder, 4 one in Curse's
    char           guy_letter[kMaxGuys] = {};   // a Pool saved game's letter (CHRDAT<letter>n.SAV)
    char           guy_from = 'C';              // the list's source: C(urse) / P(ool)
    int            guys = 0;
    char           w_magic[44] = {}, w_level[9][12] = {};
    char           w_kind[3][12] = {}, w_spells_word[8] = {};      // Gateway's headings: "Magic-User " + "Spells"    // the magic menu, "1st Level" ... (Secret: 7, Darkness 9)
    char           w_these[16] = {};                        // Secret: " these spells?" after Memorize / Scribe
    char           w_alter[36] = {}, w_select[14] = {}, w_place[14] = {};   // Alter's menus
    char           w_speed[24] = {};           // Darkness: Speed's word list ("Faster Slower Exit")
    char           w_save_which[24] = {}, w_slots[24] = {}, w_saving[24] = {}, w_camp_menu[40] = {}, w_crown_menu[44] = {},
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
    char           vw[profile::kViewWords][28] = {};   // View's menu and its questions (profile::view_facts)
    uint8_t        fx_shown[32] = {};                   // Darkness: the effects Display lists (magic.effects_shown)
    uint8_t        dk_ov_pics[5] = {};                  // Darkness: the overland's map picture by region
    char           v_npc[8] = {}, v_age[8] = {}, v_stat[6][8] = {}, v_level[8] = {}, v_exp[12] = {}, v_status[8] = {},
                   v_ac[12] = {}, v_hp[12] = {}, v_thac0[12] = {}, v_damage[20] = {}, v_enc[24] = {}, v_move[16] = {},
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

// ---- The VGA palette (Pools of Darkness on a 256-colour screen: the boards
// with PSRAM - canvas_depth; darkness_facts.md 7 5.1). As the original's
// VGA mode: a picture's palette entries go to the screen as it's shown
// (vga_take - `now` is what the screen shows); the frame's (16-31), the
// walls' (32-143) and the background's (144-175) are also kept, and sent
// again when their screens come back after a picture or a fight used
// those entries (vga_send). Entries 0-15 are the EGA colours (the text).
// Only on a VGA canvas (nullptr: the original's EGA mode, no palette).
struct Vga {
    pic::Rgb now[256];
    pic::Rgb kept[256];
    pic::VgaPalette load;          // a block's entries as it's read
    bool changed = true;
    // Colour cycling (darkness_facts.md 7 4.3): the program's table (read
    // at the start), the entry of the picture shown (-1: none), each
    // range's last turn
    uint8_t  cyc[15][12] = {};
    int8_t   cyc_on = -1;
    uint32_t cyc_at[4] = {};
};
Vga* vga = nullptr;
// A block's palette is to be read (nullptr: not on this screen)
pic::VgaPalette* vga_load()
{
    if (!vga) return nullptr;
    vga->load.first = vga->load.count = 0;
    return &vga->load;
}
// The block just read: its entries first .. first + count - 1 (from, n:
// only these of them) to the screen, and kept for their screen (keep)
void vga_take(bool keep = false, int from = 0, int n = 256)
{
    if (!vga) return;
    const pic::VgaPalette& p = vga->load;
    const int a = p.first > from ? p.first : from;
    const int b = p.first + p.count < from + n ? p.first + p.count : from + n;
    for (int i = a; i < b && i < 256; ++i) {
        vga->now[i] = p.rgb[i];
        if (keep) vga->kept[i] = p.rgb[i];
        vga->changed = true;
    }
    vga->load.count = 0;
}
// A picture is shown (a PIC id; big: a BIGPIC block): its colour cycling
// starts when the game's list has it, else none (ov111+0x0)
void vga_cycle(int id, bool big)
{
    if (!vga) return;
    vga->cyc_on = -1;
    const profile::CycleFacts* cf = profile::cycle_facts(d->prof->game);
    if (!cf) return;
    if (big && id >= cf->big && id < cf->big + cf->bigs) vga->cyc_on = static_cast<int8_t>(cf->n_pic + id - cf->big);
    for (int i = 0; !big && i < cf->n_pic; ++i)
        if (cf->pic[i] == id) vga->cyc_on = static_cast<int8_t>(i);
    const uint32_t now = plat::now_ms();
    for (uint32_t& t : vga->cyc_at) t = now;
    plat::log("[play] %s %d: colour cycling %d\n", big ? "big picture" : "picture", id, vga->cyc_on);
}
// While a picture with colour cycling shows: each range's entries turn
// one place on every speed x 5 / 100 s - entry k takes k - 1's colour, the
// first the last's (ov111+0xBD8)
void vga_cycle_tick(uint32_t now)
{
    if (!vga || vga->cyc_on < 0 || vga->cyc_on >= 15) return;
    const uint8_t* e = vga->cyc[vga->cyc_on];
    for (int r = 0; r < 4; ++r) {
        const int first = e[r * 3], n = e[r * 3 + 1], speed = e[r * 3 + 2];
        if (n < 2 || first + n > 256 || !speed || now - vga->cyc_at[r] < speed * 50u) continue;
        vga->cyc_at[r] = now;
        const pic::Rgb last = vga->now[first + n - 1];
        for (int k = first + n - 1; k > first; --k) vga->now[k] = vga->now[k - 1];
        vga->now[first] = last;
        vga->changed = true;
    }
}

// Kept entries first .. first + n - 1 to the screen again
void vga_send(int first, int n)
{
    if (!vga) return;
    for (int i = first; i < first + n && i < 256; ++i)
        if (memcmp(&vga->now[i], &vga->kept[i], sizeof(pic::Rgb)) != 0) {
            vga->now[i] = vga->kept[i];
            vga->changed = true;
        }
}

// Which screen: the party menu (the games' first screen), its "Load Which
// Game" question, or the game itself
enum class Screen : uint8_t { Game, PartyMenu, LoadWhich, View, Items, Shop, ShopBuy, Camp, SaveWhich, AddFrom,
                               AddList, YesNo, CreatePick, CreateName, TradeWho, Heal, Take, Appraise, Magic,
                               SpellList, Rest, Cast, Effects, Alter, Fight, Loot, Modify, Title, Icon, Won,
                               Portrait, ViewWho };
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
bool camping = false;              // the camp is open (the position line says so)
int  pm_item[Data::kItems];   // the menu item on each list line
int  pm_lines = 0;
bool pm_picking = false;          // Darkness's Drop: "Pick Character Select" on the party menu
char pm_pick_for = 0;               // Gateway: the function a member is being picked for ('P': who pays)
int  train_payer = -1, train_who = -1;      // Gateway: who pays for the training, the trainee
bool pm_picked = false;             // ... picked: the function goes on
int  pm_sel = 0;              // the party menu line the cursor keys highlight
bool keys_used = false;       // a cursor key was used: highlights where the games had none (the party menu)
char pm_saves[12];            // save slots found ("AB")
bool load_where = false;      // Darkness: Load's "Load from where?" asked, a slot next
bool load_secret = false;     // Darkness: Load from Secret's saved games (its folder on the card)
bool exit_wanted = false;     // Exit to DOS: back to the viewer

bool area_view = false;
bool pic_shown = false;       // a script picture covers the 3D view
Then then = Then::Idle;
bool waiting = false;         // the script waits for the player
bool in_game_menu = false;          // the party menu a script opened (PROGRAM 0)

// Combat (play_fight.inc)
// ---- Sound: the game's own effects (engine/sound), Tandy or PC speaker
sound::Player* snd = nullptr;
// Darkness: its effects are digitised samples (ZOUNDS.DAX), each read from
// the card as it sounds (profile clips)
sound::ClipPlayer* clips = nullptr;
uint8_t snd_mode = 1, snd_volume = 230;     // (Settings: 1 Tandy, 2 PC speaker, 3 Off)

bool snd_fill(uint8_t* buf, int n, void* ctx)
{
    return static_cast<sound::Player*>(ctx)->render(buf, n, kAudioHz, snd_volume);
}

bool clip_fill(uint8_t* buf, int n, void* ctx)
{
    return static_cast<sound::ClipPlayer*>(ctx)->render(buf, n, kAudioHz, snd_volume);
}

bool sound_on()
{
    return ((snd && snd->ready()) || clips) && (snd_mode == 1 || snd_mode == 2);
}
sound::Device sound_device() { return snd_mode == 2 ? sound::Device::PcSpeaker : sound::Device::Tandy; }

void start_output()
{
    if (clips) audio_play(clip_fill, clips);
    else audio_play(snd_fill, snd);
}

// A fight holds the sound output from its start to its end (Hit / Miss)
void sound_hold(bool on)
{
    if (!on) {
        audio_hold(false, nullptr, nullptr);
        return;
    }
    if (!sound_on()) return;
    if (clips) audio_hold(true, clip_fill, clips);
    else audio_hold(true, snd_fill, snd);
    if (!audio_held()) memlog::note("NOT ENOUGH MEMORY to hold the sound for the fight");
}

bool open_file(const char* name, plat::File& f);

// Gateway: each effect's rate (the program's table, read with the sound; 0: Clips::hz)
uint16_t clip_rates[16] = {};

// A digitised effect (the game's own number): its block read from the card
// now (the reader and its file buffer on the heap - a sound starts deep in
// a fight), handed to the player - it sounds once the one playing ends
__attribute__((noinline)) void play_clip(int v)
{
    const sound::Clips& cl = d->prof->clips;
    if (v < cl.first || v > cl.last) return;            // (13 / 14: the game plays nothing)
    if (cl.as_nine >> v & 1) v = 9;                     // (Gateway's player: 3 and 6 as 9)
    plat::File f;
    if (!open_file(cl.file, f)) return;
    plat::FileSource* src = new (std::nothrow) plat::FileSource(f);
    uint32_t n = 0;
    uint8_t* pcm = src ? sound::load_clip(*src, v + cl.to_block, &n) : nullptr;
    delete src;
    f.close();
    if (!pcm) {
        static uint32_t noted = 0;
        if (!noted || plat::now_ms() - noted > 60000) {
            noted = plat::now_ms() | 1;
            memlog::note("a sound left out: it didn't read (memory low?)");
        }
        return;
    }
    clips->start(pcm, n, clip_rates[v & 15] ? clip_rates[v & 15] : cl.hz, sound_device());
    start_output();
}

// A sound by the engine's number (sound::Id - Curse's), `game`: the game's
// own number (Darkness's SOUND v)
void sfx(int id, bool game = false)
{
    if (!sound_on()) return;
    if (clips && !game) id = id >= 0 && id < 16 ? d->prof->clips.ids[id] : 0;
    if (clips && !id) return;
    // A sound needs ~7 KB while it plays (its task and DAC): not when that
    // would leave the game short - in a fight nothing is dropped for it
    // (Hit and Miss are never left out - Tom, 2026-10-12: they tell a hit
    // from a miss; in a fight the output is held for them anyway).
    // Darkness: its samples too (up to 5 KB, while they play)
    const bool vital = clips ? !game && (id == d->prof->clips.ids[sound::kHit] || id == d->prof->clips.ids[sound::kMiss])
                             : id == sound::kHit || id == sound::kMiss;
    const unsigned need = clips ? 15 : 10;
    if (!vital && !audio_running() && memlog::largest_kb() < need && (fighting() || !make_room(need, "a sound"))) {
        static uint32_t noted = 0;
        if (!noted || plat::now_ms() - noted > 60000) {
            noted = plat::now_ms() | 1;
            memlog::note("a sound left out: memory low");
        }
        return;
    }
    if (clips) {
        play_clip(id);
        return;
    }
    snd->start(id, sound_device());
    start_output();
}

// Turning left / right in the 3D view: Curse's step sound; Darkness's turns
// are silent (its 3D view's keys - darkness_sound_facts.md 4)
void turn_sound()
{
    if (!d->prof->clips.quiet_turns) sfx(sound::kStep);
}

void fight_load_monster(int id, int copies, int icon, bool reserve = false);
uint8_t* fight_monster_record(int k);
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
enum class EngineAsk : uint8_t { Name, Coins, ModName, FileName, BlastDelay };
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

bool open_file(const char* name, plat::File& f)
{
    char path[160];
    games::path_of(d->data_dir, name, path, sizeof path);
    f = plat::open(path, "r");
    return static_cast<bool>(f);
}

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

// GAME.OVR's words, and the program's (Pool keeps a few in START.EXE: the
// profile marks those text::kInProgram) - text::read_pascal(src, at ...)
class OvrWords : public dax::ByteSource {
public:
    ~OvrWords()
    {
        delete w_;
        delete exe_;
        delete ovr_;
    }
    bool open()
    {
        plat::File f;
        if (!open_file(d->prof->overlay, f)) return false;
        ovr_ = new (std::nothrow) plat::FileSource(f);
        if (!ovr_) return false;
        if (d->prof->variant.program_words) {
            plat::File x;
            if (open_file(d->prof->program, x)) {
                exe_ = new (std::nothrow) plat::FileSource(x);
                if (exe_ && exepack::parse(*exe_, info_) != exepack::Status::Ok) {
                    delete exe_;
                    exe_ = nullptr;
                }
            }
        }
        w_ = new (std::nothrow) text::Words(*ovr_, exe_, exe_ ? &info_ : nullptr);
        return w_ != nullptr;
    }
    size_t read_at(uint32_t pos, uint8_t* buf, size_t n) override
    {
        if (pos & text::kInNames) {
            // (Treasures' effect names: the names file the start made - names_file)
            char path[200];
            names_path(path, sizeof path);
            plat::File f = plat::open(path, "r");
            if (!f) return 0;
            const int got = f.seek(pos & ~text::kInNames) ? f.read(buf, n) : 0;
            f.close();
            return got > 0 ? static_cast<size_t>(got) : 0;
        }
        return w_ ? w_->read_at(pos, buf, n) : 0;
    }
    uint32_t size() const override { return ovr_ ? ovr_->size() : 0; }
    static void names_path(char* out, size_t cap) { snprintf(out, cap, "%s/NAMES.BIN", d->cache_dir); }

private:
    plat::FileSource* ovr_ = nullptr;
    plat::FileSource* exe_ = nullptr;
    exepack::Info        info_;
    text::Words*         w_ = nullptr;
};

// The area's own file of a family (Secret: areas share them two by two)
bool blocks_across() { return d->prof->ecl_ops && d->prof->ecl_ops->area_by_block; }
int  area_file_no() { return blocks_across() ? (d->gs.game_area + 1) / 2 : d->gs.game_area; }
void area_file(char* out, size_t cap, const char* stem) { snprintf(out, cap, "%s%d.DAX", stem, area_file_no()); }
bool open_dax(const char* name, plat::File& f);
bool open_file(const char* name, plat::File& f);
// A file named by a pattern with the area's number in it ("MON%dCHA.DAX");
// Secret: the first of the family that's there when the area's isn't (its
// monsters and their icons are MON1* / CPIC1 for every area)
void area_fmt(char* out, size_t cap, const char* fmt)
{
    snprintf(out, cap, fmt, area_file_no());
    if (!blocks_across()) return;
    char path[200];
    games::path_of(d->data_dir, out, path, sizeof path);
    if (plat::exists(path)) return;
    for (int k = 1; k <= 9; ++k) {
        snprintf(out, cap, fmt, k);
        games::path_of(d->data_dir, out, path, sizeof path);
        if (plat::exists(path)) return;
    }
    snprintf(out, cap, fmt, area_file_no());
}
// Opens the area's file of a family with its index read; Secret: the
// family's file that holds `block` when the area's own hasn't it (name: the
// file opened)
bool open_area_dax(const char* stem, int block, plat::File& f, char* name, size_t cap)
{
    if (d->prof->variant.common_files && block >= 0) {
        // Gateway: the family's file without a number first, then the area's
        // (its loader, GAME.OVR 0x53B0F - gateway_facts.md 8)
        snprintf(name, cap, "%s.DAX", stem);
        if (open_dax(name, f)) {
            if (d->idx.find(static_cast<uint8_t>(block))) return true;
            f.close();
        }
    }
    area_file(name, cap, stem);
    if (open_dax(name, f)) {
        if (!blocks_across() || block < 0 || d->idx.find(static_cast<uint8_t>(block))) return true;
        f.close();
    } else if (!blocks_across()) {
        return false;
    }
    const int own = area_file_no();
    for (int k = 1; k <= 9; ++k) {
        if (k == own) continue;
        snprintf(name, cap, "%s%d.DAX", stem, k);
        if (!open_dax(name, f)) continue;
        if (d->idx.find(static_cast<uint8_t>(block))) return true;
        f.close();
    }
    area_file(name, cap, stem);
    return false;
}

// ---- drawing ------------------------------------------------------------------

void put(pic::Canvas& c, const char* s, int col, int row, uint8_t fg)
{
    font::draw_text(c, d->font, s, col, row, fg, 0);
}

// A list's highlighted line: black on white (Treasures: white on black - DOSBox)
void hl_text(pic::Canvas& c, const char* s, int col, int row)
{
    if (d->prof->variant.tr) {
        put(c, s, col, row, 15);
        return;
    }
    c.fill(col * 8, row * 8, static_cast<int>(strlen(s)) * 8, 8, 15);
    font::draw_text(c, d->font, s, col, row, 0, -1);
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

// ---- Pool's wilderness (overland.h; poolrad_overland_facts.md) ---------------
// In one of its three wilderness areas with the map showing (area word
// 0x4BE6 = 0; 1: a 3D place inside it)
int ov_area()
{
    const profile::Profile& p = *d->prof;
    if (!p.overland || vm->get(0x4BE6)) return -1;
    const int a = d->gs.script - p.variant.overland_area;
    return a >= 0 && a < 3 ? a : -1;
}
bool pool_overland() { return d && vm && ov_area() >= 0; }
// Darkness's overland (darkness_facts.md S4 / S7): byte 0x24 = 1 and not 3D
bool dk_overland() { return d && vm && d->prof->variant.dk_overland_pics && vm->get(0x24) == 1 && !vm->get(0x22); }
// Gateway's wilderness (gateway_facts.md 14): area word 0x4BE6 = 0
bool gw_wild() { return d && vm && d->prof->variant.tile_wild && !vm->get(0x4BE6); }
// Gateway's way ahead (GAME.OVR 0x4712F +0x82A, kept in area word 0x4CF6 as
// the square's values are taken): the facing side's door state (plane 3),
// a side without a wall open (1) in 3D only; its scripts' steps stop on 0
void gw_way_ahead()
{
    if (!d || !vm || !d->prof->ecl_ops->step_square || !d->map.loaded) return;
    const ecl::GameState& g = d->gs;
    const int dir = g.dir & 6;
    const int v = vm->get(0x4BE6) && geo::wall(d->map, g.x, g.y, dir) == 0 ? 1 : geo::door(d->map, g.x, g.y, dir);
    vm->set(0x4CF6, static_cast<uint16_t>(v));
}
// What a step out of (x, y) towards dir meets (geo::passage); in Gateway's
// wilderness the side's door state alone (its plane 0 holds tiles, not walls)
int way(int x, int y, int dir)
{
    return gw_wild() ? geo::door(d->map, x, y, dir) : geo::passage(d->map, x, y, dir);
}

// Its view (DOSBox, pixel checked): 11 x 11 squares of the region's map in
// the view's window - its first square kept 0-5 so the window stays on the
// map - each square GEO plane 0's tile (0-209: sets 1-3, 70 each), opaque;
// the party's square gets the arrow of its facing (block 202's 256-259,
// 13 see-through)
void draw_gw_wild(pic::Canvas& c)
{
    const ecl::GameState& g = d->gs;
    const int x0 = g.x - 5 < 0 ? 0 : g.x - 5 > 5 ? 5 : g.x - 5;
    const int y0 = g.y - 5 < 0 ? 0 : g.y - 5 > 5 ? 5 : g.y - 5;
    c.fill(24, 24, 88, 88, 0);
    for (int r = 0; r < view3d::kCells && d->map.loaded; ++r)
        for (int k = 0; k < view3d::kCells; ++k) {
            const int t = d->map.plane[0][(y0 + r) * geo::kSize + x0 + k];
            const view3d::TileSet& s = d->world.sets[t / view3d::kSetTiles];
            if (t >= 3 * view3d::kSetTiles || t % view3d::kSetTiles >= s.count) continue;
            const uint8_t* px = s.px[t % view3d::kSetTiles];
            const int x = (view3d::kCell0 + k) * 8, y = (view3d::kCell0 + r) * 8;
            for (int yy = 0; yy < 8; ++yy)
                for (int xx = 0; xx < 8; ++xx) {
                    const uint8_t b = px[yy * 4 + xx / 2];
                    c.set(x + xx, y + yy, static_cast<uint8_t>(xx & 1 ? b & 15 : b >> 4));
                }
        }
    view3d::draw_tile(c, d->world, 256 + (g.dir & 7) / 2, g.y - y0, g.x - x0);
    dirty_rows(3, 13);
}

// Its screen (DOSBox): the outer frame with a bar at row 16, the region's
// map picture (BIGPIC) inside, the party's marker (CURSOR.DAX block 1, 13
// see-through) on square (0x25, 0x26) of its 38 x 15 at ((x + 1) * 8,
// (y + 1) * 8); no party list, no position line
bool draw_block(pic::Canvas& c, const char* stem, int block, int x, int y);
bool open_dax(const char* file, plat::File& f);
void draw_dk_overland(pic::Canvas& c)
{
    layout::outer(c, d->tables, d->frame_tiles);
    layout::bar(c, d->tables, d->frame_tiles, 16);
    const int region = vm->get(0x3A);
    const int pic = region >= 0 && region < 5 ? d->dk_ov_pics[region] : 0;
    if (pic) draw_block(c, "BIGPIC", pic, 8, 8);
    plat::File f;
    if (d->prof->variant.marker_file && open_dax(d->prof->variant.marker_file, f)) {
        plat::FileSource src(f);
        const dax::Entry* e = d->idx.find(d->prof->variant.marker_block);
        if (e) pic::draw_vga_block(src, d->idx, *e, 0, c, (vm->get(0x25) + 1) * 8, (vm->get(0x26) + 1) * 8, 13, vga_load());
        vga_take();
        f.close();
    }
    dirty(0, 17 * 8);
}

bool ov_load()
{
    if (d->ov) return true;
    d->ov = new (std::nothrow) overland::Data;
    if (!d->ov) return false;
    plat::File f;
    bool ok = false;
    if (open_file(d->prof->program, f)) {
        plat::FileSource src(f);
        exepack::Info info;
        struct Ctx {
            plat::FileSource* src;
            exepack::Info* info;
            uint32_t base;
        } ctx{&src, &info, d->prof->create.ds_image};
        ok = exepack::parse(src, info) == exepack::Status::Ok &&
             overland::read(*d->ov, *d->prof->overland,
                            [](void* p, uint16_t ds, uint8_t* out, size_t n) {
                                Ctx* c = static_cast<Ctx*>(p);
                                return exepack::read(*c->src, *c->info, c->base + ds, out, n) == exepack::Status::Ok;
                            },
                            &ctx);
        f.close();
    }
    if (!ok) {
        delete d->ov;
        d->ov = nullptr;
    }
    return ok;
}

// The party's square on the whole map
void ov_square(int* col, int* row)
{
    *col = overland::column(*d->ov, ov_area(), vm->get(0x4BC3) & 0xFF);
    *row = vm->get(0x4BC4) & 0xFF;
}

// The left box: 5 x 5 icons (SQRPACI.DAX blocks 1 and 2, 128 frames each,
// opaque; the healed land recoloured), the rider (ICON.DAX 0, 128, 1, 129,
// 2, 130 in turn - a picture each redraw; black see-through; facing south
// or west mirrored) on the party's square
void draw_overland(pic::Canvas& c)
{
    if (!ov_load()) {
        c.fill(8, 8, 120, 120, 0);
        return;
    }
    const overland::Facts& of = *d->prof->overland;
    int col, row, x0, y0;
    ov_square(&col, &row);
    overland::window(col, row, &x0, &y0);
    int fr[overland::kView * overland::kView];
    for (int i = 0; i < overland::kView; ++i)
        for (int j = 0; j < overland::kView; ++j)
            fr[i * overland::kView + j] = overland::frame(*d->ov, of, x0 + j, y0 + i,
                                                          [](void*, uint16_t w) { return vm->get(w); }, nullptr);
    const bool healed = vm->get(of.healed) != 0;
    uint8_t px[24 * 12];
    auto put_icon = [&](int at, int frame) {
        const int x = 8 + 24 * (at % overland::kView), y = 8 + 24 * (at / overland::kView);
        for (int r = 0; r < 24; ++r)
            for (int k = 0; k < 24; ++k) {
                uint8_t v = static_cast<uint8_t>(k & 1 ? px[r * 12 + k / 2] & 15 : px[r * 12 + k / 2] >> 4);
                if (healed) v = overland::healed_colour(*d->ov, of, frame, v);
                c.set(x + k, y + r, v);
            }
    };
    c.fill(8, 8, 120, 120, 0);
    plat::File f;
    if (open_dax("SQRPACI.DAX", f)) {
        plat::FileSource src(f);
        for (int blk = 1; blk <= 2; ++blk) {
            // One pass through the block: each frame wanted drawn as it goes by
            int last = -1;
            for (int k = 0; k < overland::kView * overland::kView; ++k)
                if (fr[k] >= 0 && fr[k] / 128 == blk - 1 && fr[k] % 128 > last) last = fr[k] % 128;
            const dax::Entry* e = last >= 0 ? d->idx.find(static_cast<uint8_t>(blk)) : nullptr;
            if (!e) continue;
            dax::RleReader r(src, d->idx, *e);
            uint8_t hdr[pic::kHeaderSize];
            pic::Header h;
            if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_header(hdr, e->raw_size, h) ||
                h.frame_bytes() != sizeof px)
                continue;
            for (int n = 0; n <= last; ++n) {
                if (r.read(px, sizeof px) != sizeof px) break;
                for (int k = 0; k < overland::kView * overland::kView; ++k)
                    if (fr[k] == (blk - 1) * 128 + n) put_icon(k, fr[k]);
            }
        }
        f.close();
    }
    // The rider
    static const uint8_t kRider[6] = {0, 128, 1, 129, 2, 130};
    if (open_dax("ICON.DAX", f)) {
        plat::FileSource src(f);
        const dax::Entry* e = d->idx.find(kRider[d->gallop % 6]);
        uint8_t hdr[pic::kHeaderSize];
        pic::Header h;
        if (e) {
            dax::RleReader r(src, d->idx, *e);
            if (r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, h) &&
                h.frame_bytes() == sizeof px && r.read(px, sizeof px) == sizeof px) {
                const int pxl = 8 + 24 * (col - x0), pyl = 8 + 24 * (row - y0);
                const bool mirror = d->gs.dir > 3;
                for (int r2 = 0; r2 < 24; ++r2)
                    for (int k = 0; k < 24; ++k) {
                        const int sk = mirror ? 23 - k : k;
                        const uint8_t v = static_cast<uint8_t>(sk & 1 ? px[r2 * 12 + sk / 2] & 15 : px[r2 * 12 + sk / 2] >> 4);
                        if (v) c.set(pxl + k, pyl + r2, v);
                    }
            }
        }
        f.close();
    }
    d->gallop = static_cast<uint8_t>((d->gallop + 1) % 6);
    dirty_rows(1, 15);
}

// Treasures' overland (its o144+0x8C2; the demo's): 11 x 11 squares of
// its OVER.DAX map (8 pixels a square, 16 x 16) round the party - the
// window kept on the map - and the party's arrow (8X8D0 202's 16-19)
void draw_tr_overland(pic::Canvas& c)
{
    auto clamp = [](int v) { return v < 0 ? 0 : v > 5 ? 5 : v; };
    const int x0 = clamp(d->gs.x - 5), y0 = clamp(d->gs.y - 5);
    plat::File f;
    if (!open_dax("OVER.DAX", f)) {
        c.fill(24, 24, 88, 88, 0);
        return;
    }
    plat::FileSource src(f);
    if (!view3d::draw_vga_window(src, d->idx, d->tr_over, c, x0 * 8, y0 * 8, 88, 88, 24, 24, vga != nullptr, vga_load())) {
        c.fill(24, 24, 88, 88, 0);
    }
    vga_take();
    // 256 colours: the season's colours, entries 32-255 of OVER block game
    // byte 0x41 + 1 (blocks 1-4, palettes only; o144+0x9F4 reads them,
    // o144+0x9C0 sends them after the map's drawn - DOSBox VGA: the
    // demo's autumn snows)
    if (vga) {
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(d->gs.area1[0x41] + 1));
        uint8_t hdr[pic::kVgaHeaderSize];
        pic::VgaHeader h;
        if (e) {
            dax::RleReader r(src, d->idx, *e);
            pic::VgaPalette* p = vga_load();
            // (a palette-only block: no picture - height and width 0)
            const bool ok = r.read(hdr, sizeof hdr) == sizeof hdr;
            h.first = hdr[8];
            h.count = static_cast<uint16_t>(hdr[9] + 1);
            if (ok && h.first + h.count <= 256 && pic::kVgaHeaderSize + h.count * 3u <= e->raw_size &&
                pic::read_vga_palette(r, h, p->rgb)) {
                p->first = h.first;
                p->count = h.count;
                vga_take();
            }
        }
    }
    f.close();
    view3d::draw_marker(c, d->world, 16 + ((d->gs.dir & 7) >> 1), (3 + d->gs.x - x0) * 8, (3 + d->gs.y - y0) * 8);
    dirty_rows(3, 13);
}

bool world_parked = false;           // the 3D view's walls on the card during a fight (play_fight.inc)
// The scripts' text colour: light green (Darkness: game byte 0x39 - its
// scripts give speech 14, yellow; 0 or past 15 is 10 - darkness_facts.md S2)
uint8_t script_text_colour()
{
    if (!d->prof->variant.vga) return 10;
    const uint8_t v = d->gs.area1[0x38];
    return v == 0 || v > 15 ? 10 : v;
}

// The scripts' text window (Treasures: five lines 9 pixels apart from 2
// below row 17 - DOSBox)
text::Region script_area()
{
    if (!d->prof->variant.tr) return text::kTextArea;
    text::Region r = text::kTextArea;
    r.line_h = 9;
    r.y_off = 2;
    return r;
}

// Darkness's background (darkness_facts.md 7.3): indoors (the square's
// flags 0x80) BACK1's picture of the look; outdoors SKYGRND - with the gold
// frame (style 0 / 4) night from hour 14 (2), overcast with game byte 0x3A
// (3), else day (1); the other styles the red sky (4)
void dk_background()
{
    const uint8_t* gb = d->gs.area1;
    const bool in = geo::flags(d->map, d->gs.x, d->gs.y) >= 0x80;
    int block;
    if (d->prof->variant.tr) {
        // Treasures (its ovl 0x144 +0x328): indoors BACK1 block game byte
        // 0x54; outdoors SKYGRND byte 0x56 x 3 + the time of day - 2 from
        // 8 to 16 o'clock, 1 from 19 to 4, else 0
        const int h = gb[0x07];
        block = in ? gb[0x54] : gb[0x56] * 3 + (h >= 8 && h <= 16 ? 2 : h <= 4 || (h >= 19 && h <= 23) ? 1 : 0);
    } else if (in) {
        block = d->save.look;
    } else if (gb[0x39] == 0 || gb[0x39] == 4) {
        block = gb[0x07] >= 14 ? 2 : gb[0x3A] ? 3 : 1;
    } else {
        block = 4;
    }
    const uint8_t id = static_cast<uint8_t>((in ? 0x10 : 0x20) | (block & 15));
    if (d->world.back && d->world.back_id == id) return;
    plat::File f;
    if ((block > 0 || (d->prof->variant.tr && !in)) && open_dax(in ? "BACK1.DAX" : "SKYGRND.DAX", f)) {
        plat::FileSource src(f);
        view3d::load_back(src, d->idx, static_cast<uint8_t>(block), id, d->world, vga_load());
        vga_take(true);              // (a VGA screen: entries 144-175, kept for the view)
        f.close();
    }
}

extern int tr_lattice_drawn;
void tr_lattice(pic::Canvas& c);
// Treasures: after BEGIN the position line shows once the view has been
// drawn (the start's pictures come before - DOSBox: none under them; a
// picture shown later leaves it, and the demo has it from its start)
bool tr_position = false;
void tr_background(bool on);
// Darkness / Treasures: someone aged (haste, the clock) - the next fight's
// end looks at old age (their DS 0x9CFE / 0x9318; not in the saves)
bool age_check = false;
void draw_view(pic::Canvas& c)
{
    if (world_parked) return;
    if (dk_overland() && !pic_shown) {
        bigpic = -1;
        draw_dk_overland(c);
        return;
    }
    if (bigpic >= 0) {
        // A big picture covers the screen: outdoors it stays (the wilderness
        // map); in a 3D area the exploring screen comes back
        if (!vm->get(0x4BE6) && !pool_overland() && !gw_wild()) return;
        bigpic = -1;
        pic_shown = false;
        draw_frame(c);
        draw_panel(c);
    }
    if (pic_shown) return;
    tr_position = true;
    if (pool_overland()) {
        draw_overland(c);
        return;
    }
    if (gw_wild()) {
        draw_gw_wild(c);
        return;
    }
    if (d->prof->variant.tr && tr_lattice_drawn != (d->gs.area1[0x3F] ? 2 : 1)) tr_lattice(c);
    if (d->prof->variant.tr && d->gs.area1[0x3F] && d->tr_over) {
        draw_tr_overland(c);
        return;
    }
    if (!d->map.loaded) {
        c.fill(24, 24, 88, 88, 0);
    } else if (area_view && (vm->get(0x4BFB) == 0)) {
        view3d::draw_area_map(c, d->world, d->map, d->gs.x, d->gs.y, d->gs.dir);
    } else {
        if (d->prof->variant.flat_view) {
            // Pool: flat bands (screen_facts.md 4)
            view3d::draw(c, d->world, d->map, d->gs.x, d->gs.y, d->gs.dir,
                         view3d::pool_bands(sky_colour(), vm->view_byte(0), vm->view_byte(1)));
        } else {
            if (d->world.dk) dk_background();
            view3d::draw(c, d->world, d->map, d->gs.x, d->gs.y, d->gs.dir, sky_colour());
            // (a VGA screen: the walls' and the background's entries again
            // after a picture or a fight used them - ovr 0x3C671+0xEF5; no
            // picture's colour cycling)
            vga_send(32, 144);
            if (vga) vga->cyc_on = -1;
        }
    }
    dirty_rows(3, 13);
}

bool dk_overland();
void draw_position(pic::Canvas& c)
{
    if (dk_overland()) return;
    if (d->prof->variant.tr && !tr_position) return;
    const ecl::GameState& g = d->gs;
    // (Treasures clears columns 18-38 only - ovr 0x41207)
    if (d->prof->variant.tr) c.fill(18 * 8, 15 * 8, 21 * 8, 8, 0);
    else c.fill(17 * 8, 15 * 8, 22 * 8, 8, 0);
    char line[40];
    const int hours = vm->get(0x4BC9), tens = vm->get(0x4BC8), ones = vm->get(0x4BC7);
    int x = g.x, y = g.y;
    if (pool_overland() && ov_load()) ov_square(&x, &y);        // (the wilderness: its global column, its row)
    // The minutes as digits; Darkness prints tens x 10 + ones as a number
    // (ovr 0x36ECB: a script's ADD to the ones shows before the clock carries)
    char mins[8];
    if (d->prof->variant.darkness_files) snprintf(mins, sizeof mins, "%02d", tens * 10 + ones);
    else snprintf(mins, sizeof mins, "%d%d", tens, ones);
    if (vm->get(0x4BFB) == 0)
        snprintf(line, sizeof line, "%d,%d %s %02d:%s%s", x, y, geo::dir_name(g.dir), hours, mins,
                 camping ? " CAMPING" : (vm->get(0x7ECA) & 1) ? " SEARCH" : "");
    else
        snprintf(line, sizeof line, "%s %02d:%s", geo::dir_name(g.dir), hours, mins);
    put(c, line, d->prof->variant.tr ? 18 : 17, 15, 10);      // (Treasures: from column 18 - DOSBox)
    dirty_rows(15, 15);
}

// The party list: "Name" and "AC  HP" on row 2, a character a row from
// row 4 - the selected one's name white, the others cyan (red: out of the
// fights, yellow: on the other side); AC and HP right-aligned at columns
// 34 and 38, HP yellow when below the most (as the games print it)
// Gateway: one with the experience for a class's next level (its party
// list shows them magenta)
bool gw_trainable(const party::Character& ch)
{
    if (!d->gw_xp) return false;
    for (int k = 0; k < 8; ++k) {
        const int lv = ch.level(k);
        if (lv <= 0 || lv >= 12) continue;
        const int32_t need = d->gw_xp[k * 11 + lv - 1];
        if (need > 0 && static_cast<int64_t>(ch.exp()) >= need) return true;
    }
    return false;
}

void draw_party(pic::Canvas& c, int col, bool mark_sel = true)
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
        const uint8_t fg = i == pt->selected && mark_sel ? 15 : !ch.in_combat() ? 12 : ch.enemy() ? 14 : gw_trainable(ch) ? 13 : 11;
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
    if (dk_overland()) return;              // (Darkness's overland: its map in the panel's place)
    c.fill(17 * 8, 8, 22 * 8, 14 * 8, 0);
    if (pt->count) draw_party(c, 17);
    else put(c, "NO PARTY YET", 18, 2, 8);
    draw_position(c);
}

// Treasures: the lattice round the picture box (its img 0x199B): BORD 3's
// frames 0 / 1 along the top and bottom, BORD 5's down the sides
// Treasures' lattice round the view: BORD 3 / 5, the overland's BORD 4 /
// 6 (its parchment; resident 0x15D:0x474 - [0x7025] the style drawn)
int tr_lattice_drawn = 0;
void tr_lattice(pic::Canvas& c)
{
    const int style = d->gs.area1[0x3F] ? 2 : 1;
    tr_lattice_drawn = style;
    plat::File f;
    if (!open_dax("BORD.DAX", f)) return;
    plat::FileSource src(f);
    struct Part { uint8_t block, frame; int16_t x, y; };
    static const Part kParts[] = {{3, 0, 0, 0}, {3, 1, 0, 112}, {5, 0, 0, 24}, {5, 1, 112, 24}};
    for (const Part& p : kParts) {
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(p.block + style - 1));
        if (e) pic::draw_vga_block(src, d->idx, *e, p.frame, c, p.x, p.y, 13, vga_load());
        vga_take();
    }
    f.close();
    dirty_rows(0, 16);
}

void draw_frame(pic::Canvas& c)
{
    c.clear(0);
    if (dk_overland()) {
        layout::outer(c, d->tables, d->frame_tiles);
        layout::bar(c, d->tables, d->frame_tiles, 16);
    } else {
        layout::explore(c, d->tables, d->frame_tiles);
        if (d->tables.tr) tr_lattice(c);
    }
    dirty(0, pic::kScreenH);
}

bool menu_on = false;            // the menu line shows `menu` (for the tap highlight)

// (Darkness's fight menu takes rows 23 and 24: text::two_rows)
bool menu_was_two = false;

void show_menu_line(pic::Canvas& c)
{
    menu_on = true;
    if (menu_was_two && !text::two_rows(menu)) {
        c.fill(0, (text::kMenuRow - 1) * 8, pic::kScreenW, 8, 0);
        dirty_rows(text::kMenuRow - 1, text::kMenuRow - 1);
    }
    menu_was_two = text::two_rows(menu);
    text::MenuColors mc;
    mc.plain_sel = d->prof->variant.tr;     // (Treasures: the chosen word white - DOSBox)
    text::draw(c, d->font, menu, mc);
    dirty_rows(menu_was_two ? text::kMenuRow - 1 : text::kMenuRow, text::kMenuRow);
}

void clear_menu_line(pic::Canvas& c)
{
    menu_on = false;
    const int top = menu_was_two ? text::kMenuRow - 1 : text::kMenuRow;
    menu_was_two = false;
    c.fill(0, top * 8, pic::kScreenW, (text::kMenuRow + 1 - top) * 8, 0);
    dirty_rows(top, text::kMenuRow);
}

// Darkness's menu routine (darkness_alter_facts.md A1, the leftovers' facts
// L8): its highlight is one place (DS 0xAAE5) every menu starts from where
// the menu before left it, unless the routine around the menu sets it - the
// first word, or a place of its own it keeps while it runs (the exploring,
// camp, Magic, View, items, shop / temple and treasure menus: a local saved
// after each pick, put back before the menu again). The place: a word's
// index; past the menu's end the first word. Other games: as their callers set it
bool kept_hl();
enum DkMenu : uint8_t { kDkExplore, kDkCamp, kDkMagic, kDkView, kDkItemsList, kDkShopMain, kDkTreasureMain, kDkMenus };
int  dk_place = 0;                  // where the last menu was left
int  dk_own[kDkMenus] = {};         // the menus keeping a place of their own
int  dk_slot = -1;                  // the menu shown keeps its place here (-1: none)
int  dk_slot_sig = 0;               // ... while it is this menu (its words' keys)
bool dk_explore_fresh = true;       // the exploring routine starts again: its place from the last menu's
int  menu_sig()
{
    uint32_t v = static_cast<uint32_t>(menu.count);
    for (int k = 0; k < menu.count; ++k) v = v * 31u + static_cast<uint8_t>(text::key(menu, k));
    return static_cast<int>(v & 0x7FFFFFFF);
}
void hl_place(int at) { menu.selected = at >= 0 && at < menu.count ? at : 0; }
// The menu just built starts where the one before left it
void hl_inherit()
{
    dk_slot = -1;
    if (kept_hl()) hl_place(dk_place);
}
// ... on its first word (its routine sets it)
void hl_first()
{
    dk_slot = -1;
    if (kept_hl()) menu.selected = 0;
}
// ... at its own place
void hl_own(DkMenu m)
{
    dk_slot = -1;
    if (!kept_hl()) return;
    hl_place(dk_own[m]);
    dk_slot = m;
    dk_slot_sig = menu_sig();
}
// A menu word picked (or tapped): the place kept
void hl_picked(int at)
{
    if (!kept_hl() || at < 0) return;
    dk_place = at;
    if (dk_slot >= 0 && dk_slot_sig == menu_sig()) dk_own[dk_slot] = at;
}

// The game's own menu while exploring. Secret's and Darkness's start with
// Move (Secret's GAME.OVR 0x198E3, Darkness's DS 0x2F3A: "Move Area Cast
// View Encamp Search Look"): its move mode, with only "Exit" on the menu
// line - the original's arrows move the party only there; the engine's
// movement keys move it either way (SPEC 12)
bool moving = false;
bool secret_files();
bool darkness_files();
bool gateway();
bool move_menu() { return secret_files() || darkness_files() || gateway(); }
void ow(uint32_t at, char* out, size_t cap);
void idle_menu(pic::Canvas& c)
{
    if (dk_overland()) {
        // Darkness's overland: Move and Encamp (DOSBox)
        text::build(menu, "", moving ? "Exit" : "Move Encamp");
        menu.selected = 0;
        show_menu_line(c);
    } else if (move_menu() && vm->get(0x4BE6)) {
        // (Gateway's move mode: "Move (Use arrow keys):" before Exit - DOSBox)
        static char prompt3d[32];
        prompt3d[0] = 0;
        if (moving) ow(d->prof->variant.move_prompt, prompt3d, sizeof prompt3d);
        text::build(menu, prompt3d, moving ? "Exit" : "Move Area Cast View Encamp Search Look");
        menu.selected = 0;
        if (!moving && kept_hl()) {
            // (Darkness's exploring routine, GAME.OVR 0xD78B: its place from the
            // last menu's as it starts - after a script - then its own)
            if (dk_explore_fresh) dk_own[kDkExplore] = dk_place;
            dk_explore_fresh = false;
            hl_own(kDkExplore);
        }
        show_menu_line(c);
    } else if (pool_overland()) {
        // Pool's wilderness: no Area (its program's words)
        text::build(menu, "", "Cast View Encamp Search Look");
        menu.selected = 0;
        show_menu_line(c);
    } else if (gw_wild()) {
        // Gateway's wilderness: no Area (DS 0x4D9); its move mode's prompt
        // "Move (Use arrow keys):" before Exit (DOSBox)
        static char prompt[32];
        prompt[0] = 0;
        if (moving) ow(d->prof->variant.move_prompt, prompt, sizeof prompt);
        text::build(menu, prompt, moving ? "Exit" : "Move Cast View Encamp Search Look");
        menu.selected = 0;
        show_menu_line(c);
    } else if (vm->get(0x4BE6)) {
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
    plat::File f;
    if (!open_area_dax(stem, block, f, name, sizeof name)) return false;
    plat::FileSource src(f);
    bool ok = false;
    const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
    if (e && d->prof->variant.vga) {
        ok = pic::draw_vga_block(src, d->idx, *e, 0, c, x, y, -1, vga_load());   // (Darkness's BIGPIC1)
        vga_take();
        vga_cycle(block, true);
    } else if (e) {
        dax::RleReader r(src, d->idx, *e);
        uint8_t hdr[pic::kHeaderSize];
        pic::Header h;
        // (a head or body: its 13s through its colour map - Gateway, DOSBox)
        const bool person = !strcmp(stem, "HEAD") || !strcmp(stem, "BODY");
        if (person) pic::sf_keep13(false);
        ok = r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, h) &&
             pic::draw(r, h, 0, c, x, y);
        pic::sf_keep13(true);
    }
    f.close();
    return ok;
}

void log_error(const char* t);

void anim_stop()
{
    anim_block = -1;
    if (d->anim_file) d->anim_file.close();
    delete d->anim_idx;
    d->anim_idx = nullptr;
    delete d->dk_anim;
    d->dk_anim = nullptr;
}

void anim_draw(int frame)
{
    if (anim_block < 0) return;
    plat::FileSource src(d->anim_file);
    const dax::Entry* e = d->anim_idx ? d->anim_idx->find(static_cast<uint8_t>(anim_block)) : nullptr;
    if (e && d->dk_anim) pic::draw_dk_anim(src, *d->anim_idx, *e, *d->dk_anim, frame, *cv, d->anim_x, d->anim_y);
    else if (e) pic::draw_anim(src, *d->anim_idx, *e, d->anim, frame, true, *cv, d->anim_x, d->anim_y);
    dirty_rows(d->anim_y / 8, (d->anim_y + 88) / 8);
}

// The next frame of the picture shown (Darkness's: back to its loop frame
// after the last; a delay of 99 stops it there - darkness_facts.md 7 4.2)
int anim_next(int frame)
{
    if (d->dk_anim) {
        if (d->dk_anim->delay[frame] == 99) return frame;
        return frame + 1 < d->dk_anim->frames ? frame + 1 : d->dk_anim->loop;
    }
    return (frame + 1) % d->anim.frames;
}

bool animation_on();

// An event picture: PIC<area> block id, drawn in the view; it animates
// while the game waits at a menu
bool anim_start(int id, const char* stem = "PIC", int x = 24, int y = 24)
{
    anim_stop();
    d->anim_x = static_cast<int16_t>(x);
    d->anim_y = static_cast<int16_t>(y);
    char name[24];
    area_file(name, sizeof name, stem);
    // (Treasures: PICTURE ids from pic_split on are PIC1A.DAX's)
    if (d->prof->variant.pic_split && id >= d->prof->variant.pic_split && !strcmp(stem, "PIC"))
        snprintf(name, sizeof name, "PIC%dA.DAX", area_file_no());
    if (blocks_across() || d->prof->variant.common_files) {
        // (Secret: whichever of the family's files holds the picture;
        // Gateway: PIC.DAX's or the area's)
        plat::File probe;
        if (open_area_dax(stem, id, probe, name, sizeof name)) probe.close();
    }
    if (!open_file(name, d->anim_file)) return false;
    d->anim_idx = new (std::nothrow) dax::Index;
    if (!d->anim_idx) {
        log_error(no_memory("the picture"));
        d->anim_file.close();
        return false;
    }
    plat::FileSource src(d->anim_file);
    const dax::Entry* e = nullptr;
    if (dax::read_index(src, *d->anim_idx) == dax::Status::Ok) e = d->anim_idx->find(static_cast<uint8_t>(id));
    if (e && d->prof->variant.vga) {
        // Darkness: its own animation format (pic::DkAnim; delays in tenths)
        d->dk_anim = new (std::nothrow) pic::DkAnim;
        dax::RleReader r(src, *d->anim_idx, *e);
        if (d->dk_anim && pic::parse_dk_anim(r, e->raw_size, *d->dk_anim, vga_load())) {
            vga_take();                  // (a VGA screen: its entries, 32-255)
            vga_cycle(id, false);
            d->anim.frames = d->dk_anim->frames;
            for (int k = 0; k < d->dk_anim->frames && k < pic::kMaxAnimFrames; ++k) d->anim.delay[k] = d->dk_anim->delay[k];
            anim_block = id;
            anim_frame = 0;
            anim_at = plat::now_ms();
            last_pic = id;
            anim_draw(0);
            if (!animation_on()) anim_stop();
            return true;
        }
        delete d->dk_anim;
        d->dk_anim = nullptr;
    } else if (e) {
        dax::RleReader r(src, *d->anim_idx, *e);
        if (pic::parse_anim(r, e->raw_size, d->anim, d->prof->variant.sf_pics) && d->anim.frames > 0) {
            anim_block = id;
            anim_frame = 0;
            anim_at = plat::now_ms();
            last_pic = id;
            anim_draw(0);
            if (!animation_on()) anim_stop();       // Alter Pics: Animation off - the first frame only
            return true;
        }
    }
    d->anim_file.close();
    delete d->anim_idx;
    d->anim_idx = nullptr;
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
    for (int r = 0; r < 8; ++r) cv->put_row(cursor_px, cursor_py + r, 8, cursor_under + r * 8);
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
    for (int r = 0; r < 8; ++r) cv->get_row(cursor_px, cursor_py + r, 8, cursor_under + r * 8);
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

// An entry's key: its first capital (Gateway's "initialize JoyStick/Mouse" is J)
char pm_key(int i)
{
    for (const char* q = d->item[i]; *q; ++q)
        if (*q >= 'A' && *q <= 'Z') return *q;
    return static_cast<char>(toupper(static_cast<unsigned char>(d->item[i][0])));
}
// The party menu's first row: 12; Gateway's 23 - its lines, 12 to 15 (its
// party menu, GAME.OVR 0x2650E + 0x55C)
void pm_flags(bool on[]);
int pm_row0()
{
    if (!d->prof->variant.party_menu_row) return 12;
    bool on[Data::kItems];
    pm_flags(on);
    int n = 0;
    for (int i = 0; i < d->items; ++i) n += on[i];
    // (Gateway's "initialize JoyStick/Mouse", always there in the original,
    // counts though the engine leaves it out: the lines above it stay where
    // the original has them - DOSBox; a script's party menu has none)
    if (gateway() && !in_game_menu) ++n;
    const int r = 23 - n;
    return r > d->prof->variant.party_menu_row ? d->prof->variant.party_menu_row : r < 12 ? 12 : r;
}

void pm_flags(bool on[])
{
    for (int i = 0; i < d->items; ++i) {
        on[i] = d->item_on[i];
        switch (pm_key(i)) {
        case 'D': case 'M': case 'V': case 'R': case 'S':
            on[i] = pt->count > 0;
            break;
        case 'I':                               // Secret's Initialize Mouse/Joystick: nothing to set up here
            on[i] = gateway() && pt->count > 0;     // (Gateway's Icon Select: with a party)
            break;
        case 'J':                               // (Gateway's "initialize JoyStick/Mouse")
            on[i] = false;
            break;
        case 'A':                               // (Gateway: none past 6 members - DOSBox)
            if (gateway()) on[i] = pt->count < 6;
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
    // (Treasures: "Select" white - DOSBox)
    put(c, note ? note : d->choose, 0, text::kMenuRow, d->prof->variant.tr && !note ? 15 : 13);
}

void draw_party_menu(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    // (Treasures: no member marked on its party menu, its menu's line white
    // - DOSBox)
    const bool tr = d->prof->variant.tr;
    if (pt->count) draw_party(c, 1, !tr);
    bool on[Data::kItems];
    pm_flags(on);
    pm_lines = 0;
    int n_on = 0;
    for (int i = 0; i < d->items; ++i) n_on += on[i];
    if (pm_sel >= n_on) pm_sel = n_on ? n_on - 1 : 0;
    // (Darkness: from column 9, all green)
    const int col0 = d->prof->variant.party_menu_col ? d->prof->variant.party_menu_col : 2;
    for (int i = 0; i < d->items; ++i) {
        if (!on[i]) continue;
        const int row = pm_row0() + pm_lines;
        const uint8_t fg = tr && pm_lines == pm_sel ? 15 : 10;
        char first[2] = {d->item[i][0], 0};
        put(c, first, col0, row, d->prof->variant.party_menu_col ? fg : 15);
        put(c, d->item[i] + 1, col0 + 1, row, fg);
        pm_item[pm_lines++] = i;
    }
    if (pm_sel >= pm_lines) pm_sel = pm_lines ? pm_lines - 1 : 0;
    if (keys_used && pm_lines) {
        // the cursor keys' line, highlighted as the games' lists are
        const char* it = d->item[pm_item[pm_sel]];
        hl_text(c, it, col0, pm_row0() + pm_sel);
    }
    pm_prompt(c);
    dirty(0, pic::kScreenH);
}

// A file in the save folder (save_dir is the folder's full path on the card)
void save_path(const char* name, char* out, size_t cap) { snprintf(out, cap, "%s/%s", d->save_dir, name); }

void save_file_name(char slot, char* out, size_t cap);
bool darkness_files();

void find_saves()
{
    int n = 0;
    for (char s = savegame::kFirst; s <= savegame::kLast; ++s) {
        char name[24], path[160];
        save_file_name(s, name, sizeof name);
        save_path(name, path, sizeof path);
        plat::File f = plat::open(path, "r");
        if (f) {
            pm_saves[n++] = s;
            f.close();
        }
    }
    pm_saves[n] = 0;
}

bool open_save_file(const char* name, plat::File& f)
{
    char path[160];
    save_path(name, path, sizeof path);
    f = plat::open(path, "r");
    return static_cast<bool>(f);
}

void load_journal_list(char slot);

// The characters' files: Curse's 422-byte records (.SAV in a saved game,
// NAME.GUY when removed), items .SWG, effects .FX; Pool of Radiance's
// 285-byte records (NAME.CHA), items .ITM, effects .SPC
// (profile variant.pool_files; party::from_pool / to_pool); Secret's
// 439-byte records (NAME.WHO), items .STF (67 bytes each), effects .SFX
// (variant.secret_files; party::from_secret / to_secret)
bool pool_files() { return d->prof->variant.pool_files; }
bool secret_files() { return d->prof->variant.secret_files; }
// Gateway to the Savage Frontier (Curse's files and saves; its own ways)
bool gateway() { return d->prof->game == games::Game::GatewayToTheSavageFrontier; }
// Darkness's (variant.darkness_files): 510-byte records (NAME.PC), items
// .THG (63 bytes), effects .EFX; its saves SAVGAMx.PTY
bool darkness_files() { return d->prof->variant.darkness_files; }
party::Format rec_format()
{
    return pool_files()       ? party::Format::Pool
           : secret_files()   ? party::Format::Secret
           : darkness_files() ? party::Format::Darkness
                              : party::Format::Curse;
}
const char* ext_guy() { return pool_files() ? ".CHA" : secret_files() ? ".WHO" : darkness_files() ? ".PC" : ".GUY"; }
const char* ext_items() { return pool_files() ? ".ITM" : secret_files() ? ".STF" : darkness_files() ? ".THG" : ".SWG"; }
const char* ext_fx() { return pool_files() ? ".SPC" : secret_files() ? ".SFX" : darkness_files() ? ".EFX" : ".FX"; }
size_t rec_file_size()
{
    return pool_files()       ? party::kPoolRecordSize
           : secret_files()   ? party::kSecretRecordSize
           : darkness_files() ? party::kDarknessRecordSize
                              : party::kRecordSize;
}
void save_file_name(char slot, char* out, size_t cap)
{
    if (darkness_files()) savegame::file_name_darkness(slot, out, cap);
    else savegame::file_name(slot, out, cap);
}
uint32_t item_stride() { return secret_files() ? party::kSecretItemSize : party::kItemSize; }
// Secret's monsters' items (MON<n>ITM) have no name of their own (the
// bytes are left over from the editor): the name is made from the words
void name_from_words(uint8_t* r)
{
    if (!secret_files() || !names) return;
    const bool det = names->detect, stored = names->stored_names;
    names->stored_names = false;
    names->detect = false;
    char t[48];
    names->name(items::Item{r}, t, sizeof t);
    names->stored_names = stored;
    names->detect = det;
    size_t n = strlen(t);
    if (n > 0x2D) n = 0x2D;
    r[0] = static_cast<uint8_t>(n);
    memcpy(r + 1, t, n);
    memset(r + 1 + n, 0, 0x2D - n);
}
bool write_file(const char* dir, const char* name, const uint8_t* p, size_t n);
void (*g_present_now)() = nullptr;
void present_now()
{
    if (g_present_now) g_present_now();
}

// A character's three files: the record, the items, the effects (Pool's
// Strength and Charisma as its files keep them: rules::pool_stats_out)
bool write_character_files(const char* dir, const char* rec_fn, const char* items_fn, const char* fx_fn,
                           const party::Character& who)
{
    // The files keep the original's items (a fixed item back as it was, its
    // effect gone with it): a copy for the writing when one is fixed
    const party::Character* src = &who;
    party::Character* copy = nullptr;
    for (int i = 0; i < who.n_items && !copy; ++i) {
        uint8_t probe[items::kRecordSize];
        memcpy(probe, who.items[i], sizeof probe);
        if (!items::fix_item(probe, true)) continue;
        copy = new (std::nothrow) party::Character(who);
        if (copy) {
            rules::fix_items(*copy, true);
            src = copy;
        }
    }
    const party::Character& ch = *src;
    uint8_t buf[party::kDarknessRecordSize];    // (the largest: Darkness's record)
    uint8_t fx[party::kMaxAffects * party::kAffectSize];
    size_t n = 0;
    const uint8_t* p = party::record_bytes(ch, rec_format(), buf, &n);
    const uint8_t* f = ch.affects[0];
    if (pool_files()) {
        rules::pool_stats_out(ch, d->facts, buf, fx);
        f = fx;
    }
    bool ok = write_file(dir, rec_fn, p, n);
    if (item_stride() != party::kItemSize) {
        // Secret's .STF: 67 bytes an item, the 4 after Curse's 63 zero
        const size_t len = static_cast<size_t>(ch.n_items) * item_stride();
        uint8_t* ib = len ? new (std::nothrow) uint8_t[len] : nullptr;
        if (len && !ib) ok = false;
        if (ib) {
            memset(ib, 0, len);
            for (int i = 0; i < ch.n_items; ++i) memcpy(ib + i * item_stride(), ch.items[i], party::kItemSize);
        }
        ok = ok && write_file(dir, items_fn, ib, len);
        delete[] ib;
    } else {
        ok = ok && write_file(dir, items_fn, ch.items[0], static_cast<size_t>(ch.n_items) * party::kItemSize);
    }
    ok = ok && write_file(dir, fx_fn, f, static_cast<size_t>(ch.n_affects) * party::kAffectSize);
    delete copy;
    return ok;
}

// After reading a character's files: Pool's Strength and Charisma as the engine keeps them
void character_read(party::Character& ch)
{
    if (pool_files()) rules::pool_stats_in(ch, d->facts);
    rules::fix_items(ch);                   // (an item the original got wrong, fixed when wanted)
}

// Loads saved game `slot`: the game's memory, position and script, and
// the party from their files (.SAV, with .SWG items and .FX effects)
const char* load_problem = nullptr;     // what went wrong with the last load (shown, stays until tapped)

// The slot Darkness's vault was loaded with (its VAULTx.DAT is read when no
// TMPVAULT.DAT is there yet)
char vault_slot = 'A';

// Darkness's vault is a save slot's (VAULTx.DAT), kept in TMPVAULT.DAT while
// playing (facts_shops.md 5): a load starts from the slot's own, a save
// copies the one in use to the slot's
void vault_loaded(char slot)
{
    const auto& v = d->prof->vault;
    if (!v.slot_file) return;
    vault_slot = slot;
    char path[200];
    save_path(v.file, path, sizeof path);
    if (plat::exists(path)) plat::remove(path);
}

void vault_saved(char slot)
{
    const auto& v = d->prof->vault;
    if (!v.slot_file) return;
    char from[200], to[200], name[16];
    save_path(v.file, from, sizeof from);
    if (!plat::exists(from)) {
        snprintf(name, sizeof name, v.slot_file, vault_slot);
        save_path(name, from, sizeof from);
    }
    snprintf(name, sizeof name, v.slot_file, slot);
    save_path(name, to, sizeof to);
    if (!strcmp(from, to)) return;
    plat::File in = plat::open(from, "r");
    if (!in) return;
    plat::File out = plat::open(to, "w");
    if (out) {
        uint8_t buf[256];
        int n;
        while ((n = in.read(buf, sizeof buf)) > 0) out.write(buf, static_cast<size_t>(n));
        out.close();
    }
    in.close();
}

bool load_game(char slot)
{
    load_problem = nullptr;
    vault_loaded(slot);                     // (Darkness, Gateway: the slot's own vault)
    char name[24];
    save_file_name(slot, name, sizeof name);
    plat::File f;
    if (!open_save_file(name, f)) {
        load_problem = "That saved game couldn't be opened.";
        return false;
    }
    bool ok;
    {
        plat::FileSource src(f);
        ok = darkness_files() ? savegame::read_darkness(src, d->gs, d->save)
                              : savegame::read(src, d->gs, d->save, !pool_files(), !secret_files());
    }
    f.close();
    // Whatever was going on is over now (a script's party menu, a wait)
    in_game_menu = false;
    waiting = false;
    then = Then::Idle;
    if (!ok) {
        // (read part-way: what was in memory isn't a game any more)
        plat::log("[play] %s isn't a saved game\n", name);
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
            plat::log("[play] %s missing\n", fn);
            load_problem = "A character of that saved game is missing from the save folder.";
            continue;
        }
        {
            plat::FileSource src(f);
            ok = party::read_record(src, ch, rec_format());
        }
        f.close();
        if (!ok) {
            load_problem = "A character of that saved game couldn't be read.";
            continue;
        }
        snprintf(fn, sizeof fn, "%s%s", d->save.names[i], ext_items());
        if (open_save_file(fn, f)) {
            plat::FileSource src(f);
            party::read_items(src, ch, item_stride());
            f.close();
        }
        snprintf(fn, sizeof fn, "%s%s", d->save.names[i], ext_fx());
        if (open_save_file(fn, f)) {
            plat::FileSource src(f);
            party::read_affects(src, ch);
            f.close();
        }
        character_read(ch);
        ++pt->count;
    }
    vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
    vm->set(0x7F12, d->gs.game_area);
    d->loaded = true;
    age_check = false;
    {
        char m[32];
        snprintf(m, sizeof m, "loaded saved game %c", slot);
        memlog::note(m);
    }
    for (int i = 0; i < 3; ++i) {
        d->wall_block[i] = d->save.wall_block[i];
        d->wall_set[i] = d->save.wall_set[i];
    }
    load_journal_list(slot);
    plat::log("[play] loaded %s: area %d, %d,%d, script %d, %d characters\n", name, d->gs.game_area, d->gs.x,
                  d->gs.y, vm->get(0x4BF2), pt->count);
    return true;
}

void begin_adventuring();

// ---- Darkness: Load from Secret (darkness_facts.md 32 / 33) ----------------------
// Secret's saved games in its own folder on the card (import_dir): their
// SAVGAMx.DAT's member names (seek 0x1414: the count, 8 x 41), each
// member's 439-byte .SAV, .STF items (67 bytes), .SFX effects, converted as
// Darkness's ovr 0x11903 does - its NPCs dropped, copper / silver /
// electrum / gold lost, its own race number, a human's highest former
// level 0, icon flags and flags 2 = 2, items: bundles (type 0x49) left out,
// type 0x2D's byte 0x32 = 2, type 0x3A's 0x3D = 0x59, the class values
// worked out again; Darkness's own game state isn't loaded (a new game)
extern char import_dir[128];
void dk_import_values(party::Character& ch);
void secret_to_darkness(party::Character& ch);

// The earlier game Load / Add take characters from: Darkness's Secret,
// Treasures' Gateway (its 422-byte records of Curse's layout as *.GUY /
// CHRDATxn.SAV, items .SWG of 63 bytes, effects .FX; a saved game's names
// at 0x3214 - treasures_facts.md 2)
struct Earlier {
    const char*   guy;                  // its character files' extension
    uint32_t      rec_size;
    party::Format fmt;
    const char*   items;
    uint32_t      item_size;
    const char*   fx;
    uint32_t      names_at;             // the saved game's member count and names
    int           ctl;                  // the record's control byte (0x80 on: an NPC)
};
const Earlier& earlier()
{
    static const Earlier kSecret = {".WHO", party::kSecretRecordSize, party::Format::Secret, ".STF",
                                    party::kSecretItemSize, ".SFX", 0x1414, 0xFF};
    static const Earlier kGateway = {".GUY", party::kRecordSize, party::Format::Curse, ".SWG", party::kItemSize, ".FX",
                                     0x3214, 0xF7};
    return d->prof->variant.tr ? kGateway : kSecret;
}

// A Gateway character as Treasures takes one in (its ovl 0x7C +0x16E6): its
// copper, silver, electrum and gold gone, the race in Darkness's numbers
// (half-elf 0, dwarf 1, elf 2, gnome 3, halfling 4, human 5, others 6 - a
// half-orc too), flags = 2, the class values worked out again
void gateway_to_treasures(party::Character& ch)
{
    for (int k = 0; k < 4; ++k) ch.rec[0xFB + 2 * k] = ch.rec[0xFC + 2 * k] = 0;
    static const uint8_t kRace[8] = {6, 2, 0, 3, 1, 4, 6, 5};
    ch.ext[party::kExtRace] = ch.rec[0x74] < 8 ? kRace[ch.rec[0x74]] : 6;
    ch.rec[0x14B] = 2;
    ch.ext[party::kExtFlags2] = 2;
    dk_import_values(ch);
    rules::recalc(ch, *names, d->facts);
}
void import_convert(party::Character& ch)
{
    if (d->prof->variant.tr) gateway_to_treasures(ch);
    else secret_to_darkness(ch);
}

void find_secret_saves()
{
    int n = 0;
    for (char sl = savegame::kFirst; sl <= savegame::kLast && import_dir[0]; ++sl) {
        char path[200];
        snprintf(path, sizeof path, "%s/SAVGAM%c.DAT", import_dir, sl);
        if (plat::exists(path)) pm_saves[n++] = sl;
    }
    pm_saves[n] = 0;
}

// A Secret character as Darkness takes one in (its ovr 0x11903): its
// copper, silver, electrum and gold gone, its own race number, a human's
// highest former level 0, icon flags and flags 2 = 2, bundles of scrolls
// left out, type 0x2D's byte 0x32 = 2, type 0x3A's 0x3D = 0x59, the class
// values again (a lower HP max: the difference into the HP rolled)
void secret_to_darkness(party::Character& ch)
{
    for (int k = 0; k < 4; ++k) ch.rec[0xFB + 2 * k] = ch.rec[0xFC + 2 * k] = 0;   // (copper ... gold: gone)
    ch.ext[party::kExtRace] = party::race_to_darkness(ch.rec[0x74]);
    // (0x139, a human's highest former level: 0, then a human's former levels' highest - ovr 0x11CB6)
    ch.rec[0xE6] = 0;
    if (ch.race() == 7)
        for (int c = 0; c < 7; ++c)
            if (ch.old_level(c) > ch.rec[0xE6]) ch.rec[0xE6] = static_cast<uint8_t>(ch.old_level(c));
    ch.rec[0x14B] = 2;
    ch.ext[party::kExtFlags2] = 2;
    int k = 0;
    for (int j = 0; j < ch.n_items; ++j) {
        uint8_t* it = ch.items[j];
        if (it[0x2E] == 0x49) continue;          // (a bundle of scrolls)
        if (it[0x2E] == 0x2D) it[0x32] = 2;
        if (it[0x2E] == 0x3A) it[0x3D] = 0x59;
        if (k != j) memcpy(ch.items[k], it, party::kItemSize);
        ++k;
    }
    ch.n_items = k;
    // Its Constitution recalculation sets the HP max (ovr 0x11D22: 0xE2:0x89 -
    // create::dk_con_recompute): a drop goes into the HP rolled and it runs
    // again (Secret's GOG party in DOSBox: Malachite 58 -> 61)
    dk_import_values(ch);
    rules::recalc(ch, *names, d->facts);
}

bool load_secret_game(char slot)
{
    load_problem = nullptr;
    char path[200];
    snprintf(path, sizeof path, "%s/SAVGAM%c.DAT", import_dir, slot);
    plat::File f = plat::open(path, "r");
    if (!f) {
        load_problem = "That saved game couldn't be opened.";
        return false;
    }
    uint8_t who[1 + 8 * 41];
    bool ok;
    {
        plat::FileSource src(f);
        ok = src.read_at(earlier().names_at, who, sizeof who) == sizeof who && who[0] <= 8;
    }
    f.close();
    if (!ok) {
        load_problem = "That saved game couldn't be read (damaged or short).";
        return false;
    }
    in_game_menu = false;
    waiting = false;
    then = Then::Idle;
    pt->clear();
    for (int i = 0; i < who[0] && pt->count < party::kMaxParty; ++i) {
        const uint8_t* e = who + 1 + i * 41;
        char nm[41];
        const int len = e[0] > 40 ? 40 : e[0];
        memcpy(nm, e + 1, static_cast<size_t>(len));
        nm[len] = 0;
        party::Character& ch = pt->m[pt->count];
        const Earlier& ea = earlier();
        snprintf(path, sizeof path, "%s/%s.SAV", import_dir, nm);
        f = plat::open(path, "r");
        if (!f) {
            load_problem = "A character of that saved game is missing from its folder.";
            continue;
        }
        {
            plat::FileSource src(f);
            ok = f.size() == ea.rec_size && party::read_record(src, ch, ea.fmt);
        }
        f.close();
        if (!ok || ch.rec[0xF7] >= 0x80) {          // (its NPCs stay behind)
            ch = party::Character{};
            continue;
        }
        snprintf(path, sizeof path, "%s/%s%s", import_dir, nm, ea.items);
        f = plat::open(path, "r");
        if (f) {
            plat::FileSource src(f);
            party::read_items(src, ch, ea.item_size);
            f.close();
        }
        snprintf(path, sizeof path, "%s/%s%s", import_dir, nm, ea.fx);
        f = plat::open(path, "r");
        if (f) {
            plat::FileSource src(f);
            party::read_affects(src, ch);
            f.close();
        }
        import_convert(ch);
        character_read(ch);
        ++pt->count;
    }
    vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
    d->loaded = false;                              // (its own game state: a new game)
    plat::log("[play] loaded the earlier game's saved game %c: %d characters\n", slot, pt->count);
    return pt->count > 0;
}

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

void view_box(pic::Canvas& c, const party::Character& ch);
bool view_box_tap(int x, int y, pic::Canvas& c);

bool secret_files();
void draw_character_secret(pic::Canvas& c, const party::Character& ch);
bool vault_on();
bool vault_write();
void open_vault(pic::Canvas& c);
void deposit_item(int i, pic::Canvas& c);

// ---- View Character's menu (coab ovr020; the games' own words,
// profile::view_facts): Items (some), Spells (some in memory - only looked
// at), Trade (money, out of fights: to another member), Drop (money: at a
// treasure or in a shop onto the counter, else gone), a paladin's Heal
// (twice the paladin level, once a day: its effect) and Cure (the
// diseases; its cures a week - the record's 0x191). In a fight: Items and
// Exit, as before. Secret's vault: Deposit in Drop's place (play_fight.inc).
const char* vword(int i) { return d->vw[i]; }
bool any_money(const party::Character& ch)
{
    for (int k = 0; k < 7; ++k)
        if (ch.money(k) > 0) return true;
    return false;
}
bool any_memorized(const party::Character& ch)
{
    for (int i = 0; i < 84; ++i)
        if (ch.rec[0x1E + i]) return true;
    // (Darkness: 141 places, filled from the end - the rest in ext)
    if (darkness_files())
        for (int i = 0; i < party::kDarknessMemorized - 84; ++i)
            if (ch.ext[party::kExtMemorized + i]) return true;
    return false;
}
bool in_fight();
bool can_paladin(const party::Character& ch, uint8_t fx, int word)
{
    return fx && vword(word)[0] && ch.level(3) > 0 && !in_fight() && ch.health() == party::Okay && !new_char;
}
bool can_heal(const party::Character& ch)
{
    const uint8_t fx = profile::view_facts(d->prof->game).paladin_heal;
    return can_paladin(ch, fx, profile::kVwHeal) && !ch.has_affect(fx);
}
bool can_cure(const party::Character& ch)
{
    return can_paladin(ch, profile::view_facts(d->prof->game).paladin_cure, profile::kVwCure) && ch.rec[0x191] > 0;
}
void view_keys(const party::Character& ch, char* keys, size_t cap)
{
    keys[0] = 0;
    auto add = [&](const char* w) { strncat(keys, w, cap - strlen(keys) - 1); };
    if (ch.n_items) {
        add(d->w_title);
        add(" ");
    }
    if (!in_fight() && !new_char) {
        if (any_memorized(ch)) add(vword(profile::kVwSpells));
        const bool money = any_money(ch);
        if (money) add(vword(profile::kVwTrade));
        if (money) add(vword(vault_on() ? profile::kVwDeposit : profile::kVwDrop));
        if (can_heal(ch)) add(vword(profile::kVwHeal));
        if (can_cure(ch)) add(vword(profile::kVwCure));
    }
    add(d->v_exit);
}

void draw_character_gateway(pic::Canvas& c, const party::Character& ch);

void draw_character(pic::Canvas& c)
{
    const party::Character* ch = new_char ? new_char : pt->sel();
    if (ch && (secret_files() || darkness_files())) {
        draw_character_secret(c, *ch);
        return;
    }
    if (ch && gateway()) {
        draw_character_gateway(c, *ch);
        return;
    }
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    dirty(0, pic::kScreenH);
    if (!ch) return;
    view_box(c, *ch);
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
    char keys[80];
    view_keys(*ch, keys, sizeof keys);
    text::build(menu, "", keys);
    menu.selected = 0;
    hl_own(kDkView);
    show_menu_line(c);
}

// Secret's View Character (secret_facts.md 10, its own layout): bars at
// rows 0, 8, 16, 20 and 23, a divider at column 19 from row 8 to 20 (Darkness:
// the same places in the outer frame alone, the experience without commas,
// its three coins - darkness_facts.md 34); name
// and status (row 1), sex and age, Hit Points (3), alignment, race (4),
// class (5), Experience: (6), level and experience (7), the six stats from
// row 9, AC / THAC0 / damage and encumbrance / movement (rows 17-19)
void draw_character_secret(pic::Canvas& c, const party::Character& ch)
{
    c.clear(0);
    dirty(0, pic::kScreenH);
    const layout::Tables& tb = d->tables;
    const layout::Tiles& tl = d->frame_tiles;
    const bool dk = darkness_files();
    if (dk) {
        layout::outer(c, tb, tl);
    } else {
        static const uint8_t kBars[] = {0, 8, 16, 20, 23};
        for (int k = 0; k + 1 < 5; ++k)
            for (int col : {0, 39}) layout::bars_col(c, tb, tl, col, kBars[k], kBars[k + 1]);
        layout::bars_col(c, tb, tl, 19, 8, 20);
        for (int r : kBars) layout::bars_row(c, tb, tl, r);
    }
    char t[48];
    // A number with commas between the thousands (Darkness: none)
    auto commas = [&](unsigned long v, char* out, size_t cap) {
        char raw[16];
        snprintf(raw, sizeof raw, "%lu", v);
        if (dk) {
            snprintf(out, cap, "%s", raw);
            return;
        }
        const int n = static_cast<int>(strlen(raw));
        size_t o = 0;
        for (int i = 0; i < n && o + 2 < cap; ++i) {
            if (i && (n - i) % 3 == 0) out[o++] = ',';
            out[o++] = raw[i];
        }
        out[o] = 0;
    };
    auto right = [&](const char* s, int end_col, int row, uint8_t fg) {
        put(c, s, end_col + 1 - static_cast<int>(strlen(s)), row, fg);
    };
    ch.name(t, sizeof t);
    put(c, t, 1, 1, !ch.in_combat() ? 12 : ch.enemy() ? 14 : 11);
    put(c, d->v_status, 20, 1, 15);
    put(c, name_of(d->health[0], 13, 9, ch.health()), 27, 1, 10);
    const char* sx = name_of(d->sex[0], 7, 2, ch.sex());
    put(c, sx, 1, 3, 15);
    snprintf(t, sizeof t, "%2d%s", ch.age(), d->v_age);
    put(c, t, 8, 3, 15);
    put(c, d->v_hp, 20, 3, 15);
    snprintf(t, sizeof t, "%d/%d", ch.hp(), ch.hp_max());
    put(c, t, 31, 3, ch.hp() < ch.hp_max() ? 14 : 10);
    put(c, name_of(d->alignment[0], 17, 9, ch.alignment()), 1, 4, 15);
    put(c, name_of(d->race[0], 10, 8, ch.race()), 20, 4, 15);
    put(c, name_of(d->cls[0], 27, 18, ch.cls()), 1, 5, 15);
    put(c, d->v_exp, 24, 6, 15);
    put(c, d->v_level, 1, 7, 15);
    {
        int top = 0;
        for (int k = 0; k < 8; ++k)
            if (ch.level(k) > top) top = ch.level(k);
        size_t o = 0;
        t[0] = 0;
        for (int k = 0; k < 8; ++k) {
            const int lv = ch.level(k), old = ch.old_level(k);
            if (lv > 0 || (old > 0 && old < top)) {
                o += static_cast<size_t>(snprintf(t + o, sizeof t - o, "%s%d", o ? "/" : "", lv + old));
                if (o >= sizeof t) break;
            }
        }
        put(c, t, 7, 7, 15);
    }
    commas(static_cast<unsigned long>(ch.exp()), t, sizeof t);
    right(t, d->prof->variant.tr ? 31 : 32, 7, 10);       // (Treasures: a column further left - DOSBox)
    for (int i = 0; i < 6; ++i) {
        put(c, d->v_stat[i], 1, 9 + i, 10);
        const int v = ch.stat(i);
        if (i == 0 && v == 18 && ch.str00() > 0) {
            if (ch.str00() == 100) snprintf(t, sizeof t, "18(00)");
            else snprintf(t, sizeof t, "18(%02d)", ch.str00());
        } else {
            snprintf(t, sizeof t, "%d", v);
        }
        put(c, t, 5, 9 + i, 10);
    }
    put(c, d->v_ac, 1, 17, 15);
    const int ac = ch.ac_raw();
    snprintf(t, sizeof t, "%s%d", ac > 60 ? "-" : "", ac > 60 ? ac - 60 : 60 - ac);
    right(t, 17, 17, 10);
    put(c, d->v_thac0, 1, 18, 15);
    snprintf(t, sizeof t, "%d", ch.thac0());
    right(t, 17, 18, 10);
    put(c, d->v_damage, 1, 19, 15);
    const int b = ch.damage_bonus();
    if (b) snprintf(t, sizeof t, "%dD%d%s%d", ch.dice(), ch.dice_sides(), b > 0 ? "+" : "-", b > 0 ? b : -b);
    else snprintf(t, sizeof t, "%dD%d", ch.dice(), ch.dice_sides());
    right(t, 17, 19, 10);
    put(c, d->v_enc, 20, 17, 15);
    snprintf(t, sizeof t, "%d", ch.encumbrance());
    right(t, 37, 17, 10);
    int mv = ch.movement();
    if (ch.has_affect(0x2A)) mv *= 2;          // slow
    if (ch.has_affect(0x27)) mv /= 2;          // haste
    put(c, d->v_move, 20, 18, 15);
    snprintf(t, sizeof t, "%d", mv);
    right(t, 37, 18, 10);
    // The readied weapon and body armour (rows 21-22, as the original)
    const int wi = items::readied_in(ch.items, ch.n_items, *names, items::kSlotWeapon);
    const int ai = items::readied_in(ch.items, ch.n_items, *names, items::kSlotArmour);
    if (wi >= 0) {
        names->name(items::Item{ch.items[wi]}, t, sizeof t);
        put(c, t, 1, 21, 10);
    }
    if (ai >= 0) {
        names->name(items::Item{ch.items[ai]}, t, sizeof t);
        put(c, t, 1, 22, 10);
    }
    // The money in the panel right of the stats (rows 9-15): copper first,
    // the number ending at column 37 (GAME.OVR 0x2779B: column 38 - its length)
    {
        int row = 9;
        for (int coin = 0; coin < 7 && row <= 15; ++coin) {
            const int n = ch.money(coin);
            if (n <= 0) continue;
            put(c, name_of(d->money[0], 11, 7, coin), 20, row, 10);
            snprintf(t, sizeof t, "%d", n);
            right(t, 37, row, 10);
            ++row;
        }
    }
    char keys[80];
    view_keys(ch, keys, sizeof keys);
    text::build(menu, "", keys);
    menu.selected = 0;
    hl_own(kDkView);                    // (Darkness: GAME.OVR 0x2452E's own place, from Items)
    show_menu_line(c);
}

// Gateway's View Character (gateway_facts.md 12; DOSBox): its frame (the
// outer one, bars at rows 8, 16, 20, column 19 from row 8 to 20); name and
// status (row 1), sex, age, hit points (3), alignment, race (4), class (5),
// level and experience (7), the stats from row 9 with the money panel beside
// them, AC / THAC0 / damage (17-19, their numbers to column 18) beside
// encumbrance / movement (numbers to column 37)
void draw_character_gateway(pic::Canvas& c, const party::Character& ch)
{
    c.clear(0);
    dirty(0, pic::kScreenH);
    layout::character(c, d->tables, d->frame_tiles);
    char t[48];
    auto right = [&](const char* s, int end_col, int row, uint8_t fg) {
        put(c, s, end_col + 1 - static_cast<int>(strlen(s)), row, fg);
    };
    ch.name(t, sizeof t);
    put(c, t, 1, 1, !ch.in_combat() ? 11 : ch.enemy() ? 14 : 11);
    if (ch.npc()) put(c, d->v_npc, static_cast<int>(strlen(t)) + 2, 1, 10);
    put(c, d->v_status, 20, 1, 15);
    put(c, name_of(d->health[0], 13, 9, ch.health()), 27, 1, 10);
    put(c, name_of(d->sex[0], 7, 2, ch.sex()), 1, 3, 15);
    snprintf(t, sizeof t, "%s%d", d->v_age, ch.age());
    put(c, t, 12, 3, 15);
    put(c, d->v_hp, 20, 3, 15);
    snprintf(t, sizeof t, "%d/%d", ch.hp(), ch.hp_max());
    right(t, 37, 3, ch.hp() < ch.hp_max() ? 14 : 10);
    put(c, name_of(d->alignment[0], 17, 9, ch.alignment()), 1, 4, 15);
    put(c, name_of(d->race[0], 10, 8, ch.race()), 20, 4, 15);
    put(c, name_of(d->cls[0], 27, 18, ch.cls()), 1, 5, 15);
    put(c, d->v_level, 1, 7, 15);
    {
        int top = 0;
        for (int k = 0; k < 8; ++k)
            if (ch.level(k) > top) top = ch.level(k);
        size_t o = 0;
        t[0] = 0;
        for (int k = 0; k < 8; ++k) {
            const int lv = ch.level(k), old = ch.old_level(k);
            if (lv > 0 || (old > 0 && old < top)) {
                o += static_cast<size_t>(snprintf(t + o, sizeof t - o, "%s%d", o ? "/" : "", lv + old));
                if (o >= sizeof t) break;
            }
        }
        put(c, t, 7, 7, 15);
    }
    put(c, d->v_exp, 20, 7, 15);
    snprintf(t, sizeof t, "%lu", static_cast<unsigned long>(ch.exp()));
    right(t, 37, 7, 15);
    for (int i = 0; i < 6; ++i) {
        put(c, d->v_stat[i], 1, 9 + i, 10);
        const int v = ch.stat(i);
        if (i == 0 && v == 18 && ch.str00() > 0) {
            if (ch.str00() == 100) snprintf(t, sizeof t, "18(00)");
            else snprintf(t, sizeof t, "18(%02d)", ch.str00());
        } else {
            snprintf(t, sizeof t, "%d", v);
        }
        put(c, t, 5, 9 + i, 10);
    }
    {
        int row = 9;
        for (int coin = 0; coin < 7 && row <= 15; ++coin) {
            const int n = ch.money(coin);
            if (n <= 0) continue;
            put(c, name_of(d->money[0], 11, 7, coin), 20, row, 10);
            snprintf(t, sizeof t, "%d", n);
            right(t, 37, row, 10);
            ++row;
        }
    }
    put(c, d->v_ac, 1, 17, 15);
    const int ac = ch.ac_raw();
    snprintf(t, sizeof t, "%s%d", ac > 60 ? "-" : "", ac > 60 ? ac - 60 : 60 - ac);
    right(t, 18, 17, 10);
    put(c, d->v_thac0, 1, 18, 15);
    snprintf(t, sizeof t, "%d", ch.thac0());
    right(t, 18, 18, 10);
    put(c, d->v_damage, 1, 19, 15);
    const int b = ch.damage_bonus();
    if (b) snprintf(t, sizeof t, "%dD%d%s%d", ch.dice(), ch.dice_sides(), b > 0 ? "+" : "-", b > 0 ? b : -b);
    else snprintf(t, sizeof t, "%dD%d", ch.dice(), ch.dice_sides());
    right(t, 18, 19, 10);
    put(c, d->v_enc, 20, 17, 15);
    snprintf(t, sizeof t, "%d", ch.encumbrance());
    right(t, 37, 17, 10);
    int mv = ch.movement();
    if (ch.has_affect(0x2A)) mv *= 2;          // slow
    if (ch.has_affect(0x27)) mv /= 2;          // haste
    put(c, d->v_move, 20, 18, 15);
    snprintf(t, sizeof t, "%d", mv);
    right(t, 37, 18, 10);
    // The readied weapon and armour (rows 21-22)
    const int wi = items::readied_in(ch.items, ch.n_items, *names, items::kSlotWeapon);
    const int ai = items::readied_in(ch.items, ch.n_items, *names, items::kSlotArmour);
    if (wi >= 0) {
        names->name(items::Item{ch.items[wi]}, t, sizeof t);
        put(c, t, 1, 21, 10);
    }
    if (ai >= 0) {
        names->name(items::Item{ch.items[ai]}, t, sizeof t);
        put(c, t, 1, 22, 10);
    }
    char keys[80];
    view_keys(ch, keys, sizeof keys);
    text::build(menu, "", keys);
    menu.selected = 0;
    show_menu_line(c);
}

void view_character(pic::Canvas& c)
{
    if (!pt->sel()) return;
    view_from = screen;
    screen = Screen::View;
    dk_own[kDkView] = 0;
    d->view_icon = d->view_action = false;      // (a portrait first, the ready pose)
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
    if (screen == Screen::PartyMenu && !pm_pick_for) pm_prompt(c);      // the party menu's own prompt
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
    note_until = plat::now_ms() + static_cast<uint32_t>(speed) * 300;
    if (!note_until) note_until = 1;
}

// An error (or "not in the engine yet") on the menu line: it stays until
// the player taps (Tom, 2026-10-09: errors mustn't vanish before they're
// read). Also noted in /GOLDBOX/_CYD/ERRORS.TXT (Settings - Logs).
// A line in /GOLDBOX/_CYD/ERRORS.TXT only (nothing on the screen): what
// went wrong quietly, for the player's log
void log_error(const char* t)
{
    plat::log("[play] error: %s\n", t);
    char path[96];
    snprintf(path, sizeof path, "%s/_CYD/ERRORS.TXT", games::kRootDir);
    plat::File f = plat::open(path, "a");
    if (f) {
        const uint32_t s = plat::now_ms() / 1000;
        char line[128];
        const int n = snprintf(line, sizeof line, "[%u:%02u:%02u] %s\n", (unsigned)(s / 3600), (unsigned)(s / 60 % 60),
                               (unsigned)(s % 60), t);
        f.write(reinterpret_cast<const uint8_t*>(line), n > 0 && n < (int)sizeof line ? n : 0);
        f.close();
    }
}

void error(pic::Canvas& c, const char* t)
{
    clear_menu_line(c);
    put(c, t, 0, text::kMenuRow, 12);
    note_until = 1;
    note_held = true;
    log_error(t);
}

void pick_line(int i, char* out, size_t cap);     // Create New Character's lists
void draw_input(pic::Canvas& c);

// The shop's goods are listed last first (as the games list them)
const uint8_t* goods(int i) { return ground->item[ground->n - 1 - i]; }
int shop_price(const uint8_t* it);

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
        if (darkness_files()) {
            // (30 characters: the price ending at the 30th, the name over the start)
            char line[31];
            snprintf(line, sizeof line, "%30d", shop_price(it));
            const size_t n = strlen(nm);
            memcpy(line, nm, n < 30 ? n : 30);
            snprintf(out, cap, "%s", line);
            return;
        }
        snprintf(out, cap, "%-21s%9d", nm, rules::price(it, vm->get(0x7F6D) & 0xFF));
    } else {
        const party::Character* ch = pt->sel();
        char nm[48];
        names->name(items::Item{ch->items[i]}, nm, sizeof nm);
        snprintf(out, cap, "%s%s", items::Item{ch->items[i]}.readied() ? d->w_yes : d->w_no, nm);
    }
}

bool list_unlit = false;            // draw_list without its highlight (Gateway's item questions)

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
        if (l.top + r == l.index && !list_unlit) {
            int lead = 0;
            while (line[lead] == ' ') ++lead;
            hl_text(c, line + lead, l.col0 + lead, row);
        } else {
            put(c, line, l.col0, row, screen == Screen::CreatePick && l.top + r == 0 ? 13 : 10);
        }
    }
    char keys[60];
    snprintf(keys, sizeof keys, "%s%s%s%s", what, l.top + l.rows() < l.n ? d->w_next : "", l.top > 0 ? d->w_prev : "",
             d->w_exit[0] == ' ' ? d->w_exit : " Exit");
    text::build(menu, prompt, keys);
    menu.selected = 0;
    // (Darkness: the items' menu keeps its own place - GAME.OVR 0x24B77)
    if (screen == Screen::Items) hl_own(kDkItemsList);
    else hl_first();
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
                          ScribeThese, ScribeThese2, UseIt, PaladinCure, AlterSure };
void ask_yes_no(pic::Canvas& c, Ask what, const char* prompt);
void ask_create(pic::Canvas& c, Ask what, const char* prompt);
bool view_yes_no(Ask what, char k, pic::Canvas& c);

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
bool vault_on();
// (Secret's vault isn't a shop: Trade, Deposit in Drop's place, no Sell / Id)
bool in_shop_items() { return view_from == Screen::Shop && !vault_on(); }

// The menu for the items screen (as the games build it)
// Use is offered to one who's up and about where the area allows it
// (item_use_facts.md 2: record 0x196, the area word 0x4BE5)
bool can_use_items(const party::Character& ch) { return ch.in_combat() && (vm->get(0x4BE5) & 0xFF) == 0; }

void items_keys(char* out, size_t cap)
{
    const party::Character* ch = pt->sel();
    const bool exploring = view_from == Screen::Game || view_from == Screen::Camp || view_from == Screen::Fight;
    snprintf(out, cap, "%s%s%s%s%s%s%s%s", d->w_ready, exploring && can_use_items(*ch) ? iw(profile::kUse) : "",
             !ch->npc() && (!in_shop_items() || darkness_files() || gateway()) && view_from != Screen::Fight ? iw(profile::kTrade) : "",
             iw(vault_on() ? profile::kDeposit : profile::kDrop),
             ch->n_items < party::kMaxItems ? iw(profile::kHalve) : "", iw(profile::kJoin),
             in_shop_items() && !ch->npc() ? iw(profile::kSell) : "", in_shop_items() ? iw(profile::kId) : "");
}

// What the game says about an item, in the text rows under the list
// (rows 21-22, colour 14), wrapped
void say_item(pic::Canvas& c, const char* t, uint8_t colour = 14, int row0 = 21)
{
    c.fill(8, row0 * 8, 38 * 8, (23 - row0) * 8, 0);
    int row = row0;
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
    dirty_rows(row0, 22);
}

void item_name(int i, char* out, size_t cap) { names->name(items::Item{pt->sel()->items[i]}, out, cap); }

void draw_items(pic::Canvas& c)
{
    const party::Character* ch = pt->sel();
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    if (!darkness_files()) layout::bar(c, d->tables, d->frame_tiles, 2);      // (Darkness: the outer frame alone)
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

// Gateway's item questions (Drop It?, Is It a Deal?) show the list unlit
// (DOSBox)
void gw_unlight(pic::Canvas& c)
{
    if (!gateway() || screen != Screen::Items) return;
    list_unlit = true;
    draw_items(c);
    list_unlit = false;
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
void ow(uint32_t at, char* out, size_t cap);
void read_word(dax::ByteSource& src, uint32_t at, char* out, size_t cap);

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
        rules::WornSays says = rules::WornSays::Nothing;
        rules::worn(ch, i, true, &says);            // (an item keyed to another alignment hurts and won't stay)
        rules::recalc(ch, *names, d->facts);
        item_class_values(ch, i);
        draw_items(c);
        // Pool: "NAME is stronger" (Ogre Power), "Must have Giant Strength" (the boulder)
        const uint32_t w = says == rules::WornSays::Stronger    ? d->prof->items.stronger
                           : says == rules::WornSays::NeedGiant ? d->prof->items.need_giant
                                                                : 0;
        if (w) {
            char word[32], nm[20];
            ow(w, word, sizeof word);
            ch.name(nm, sizeof nm);
            if (says == rules::WornSays::Stronger) snprintf(t, sizeof t, "%s %s", nm, word);
            else snprintf(t, sizeof t, "%s", word);
            note(c, t);
        }
        return;
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
    gw_unlight(c);
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

bool items_from_shop = false;       // Gateway: the items opened by the shop's Items (Exit: the shop)
void draw_shop(pic::Canvas& c);

// Back to the items of the one who traded (or their sheet when none are left)
void back_to_items(pic::Canvas& c)
{
    if (trade_from >= 0) pt->selected = trade_from;
    trade_from = -1;
    if (pt->sel()->n_items) {
        screen = Screen::Items;
        draw_items(c);
    } else if (items_from_shop) {
        items_from_shop = false;            // (Gateway: none left - the shop again, DOSBox)
        screen = Screen::Shop;
        draw_shop(c);
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
    rules::join(*pt->sel(), i, secret_files());
    rules::recalc(*pt->sel(), *names, d->facts);
    draw_items(c);
}

// The shop's offer for an item
void sw(int i, char* out, size_t cap);

// A Yes / No with Yes lit (Darkness's own questions start on Yes - DOSBox)
void ask_yes_first(pic::Canvas& c, Ask what, const char* prompt)
{
    ask_yes_no(c, what, prompt);
    if (darkness_files()) {
        menu.selected = 0;
        show_menu_line(c);
    }
}

// Darkness's Sell (its items overlay, GAME.OVR 0x25A73; facts_shops.md 2.2):
// its price in platinum (Secret's rule), "I'll give you N platinum pieces
// for your ITEM" / "Will you sell it? " ("them? "); nothing: "Your ITEM is
// worthless to me." / "Shall I dispose of it for you? " (a pile: " are
// worthless." / "Would you like to discard them? "). The item's name ends
// with a space, as the original's.
void sell_dk(int i, pic::Canvas& c)
{
    const uint8_t* it = pt->sel()->items[i];
    const int price = rules::sell_value(it, d->facts);
    const bool one = items::Item{it}.count() < 2;
    char nm[48], t[120], a[40], b[40];
    item_name(i, nm, sizeof nm);
    if (strlen(nm) + 1 < sizeof nm) strcat(nm, " ");
    if (price > 0) {
        snprintf(t, sizeof t, "%s%d%s%s", iw(profile::kGiveYou), price, iw(profile::kGoldFor), nm);
        sw(profile::kDkWillSell, a, sizeof a);
        sw(one ? profile::kDkIt : profile::kDkThem, b, sizeof b);
        strncat(a, b, sizeof a - strlen(a) - 1);
    } else {
        sw(profile::kDkYour, a, sizeof a);
        sw(one ? profile::kDkWorthless : profile::kDkAreWorthless, b, sizeof b);
        snprintf(t, sizeof t, "%s%s%s", a, nm, b);
        sw(one ? profile::kDkDispose : profile::kDkDiscard, a, sizeof a);
    }
    say_item(c, t);
    item_at = i;
    ask_yes_first(c, Ask::SellDeal, a);
}

void sell_item(int i, pic::Canvas& c)
{
    if (!can_part_with(i, c)) return;
    if (darkness_files()) {
        sell_dk(i, c);
        return;
    }
    char nm[48], t[120];
    item_name(i, nm, sizeof nm);
    snprintf(t, sizeof t, "%s%d%s%s", iw(profile::kGiveYou), rules::sell_value(pt->sel()->items[i], d->facts),
             iw(profile::kGoldFor), nm);
    gw_unlight(c);
    say_item(c, t);
    item_at = i;
    ask_yes_no(c, Ask::SellDeal, iw(profile::kDeal));
}

void identify_item(int i, pic::Canvas& c)
{
    char nm[48], t[120];
    item_name(i, nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s", iw(profile::kIdentify), nm);
    gw_unlight(c);
    say_item(c, t);
    item_at = i;
    ask_yes_first(c, Ask::IdDeal, iw(profile::kDeal));
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
    if (what == Ask::SellDeal && darkness_files()) {
        // Darkness: "Sold!" / "I will give it to one of my sons." / "Very
        // well.  I'll take them."; the platinum - what the character can't
        // carry goes on the counter ("Overloaded.  Money will be put in pool.")
        const int price = rules::sell_value(ch.items[i], d->facts);
        const bool one = items::Item{ch.items[i]}.count() < 2;
        rules::remove_item(ch, i);
        rules::recalc(ch, *names, d->facts);
        int room = rules::max_load(ch) - ch.encumbrance();
        if (room < 0) room = 0;
        const bool over = price > room;
        add_coins(ch, 4, over ? room : price);
        if (over) ground->money[4] += price - room;
        rules::recalc(ch, *names, d->facts);
        back_to_items(c);
        t[0] = 0;
        if (price <= 0) sw(one ? profile::kDkSons : profile::kDkTakeThem, t, sizeof t);
        note(c, over ? iw(profile::kOverloadPool) : price > 0 ? iw(profile::kSold) : t);
        return true;
    }
    if (what == Ask::IdDeal && darkness_files()) {
        // Darkness: 20 platinum - the character's, else theirs and the counter's
        const int own = ch.money(4);
        if (own >= 20) {
            add_coins(ch, 4, -20);
        } else if (own + ground->money[4] >= 20) {
            add_coins(ch, 4, -own);
            ground->money[4] -= 20 - own;
        } else {
            back_to_items(c);
            note(c, iw(profile::kNoMoney));
            return true;
        }
        if (!(ch.items[i][0x35] & 7)) {
            snprintf(t, sizeof t, "%s%s", iw(profile::kNothingNew), nm);
        } else {
            ch.items[i][0x35] &= 0xF8;
            item_name(i, nm, sizeof nm);
            snprintf(t, sizeof t, "%s%s", iw(profile::kSortOf), nm);
        }
        rules::recalc(ch, *names, d->facts);
        back_to_items(c);
        say_item(c, t);
        return true;
    }
    if (what == Ask::SellDeal) {
        const int gold = rules::sell_value(ch.items[i], d->facts);
        rules::remove_item(ch, i);
        const int plat = gold / 5;
        bool over = false;
        if (secret_files()) {
            // Secret (GAME.OVR 0x29FC0): the price in platinum and gold; when
            // it won't fit (its test weighs the price in gold - the coins
            // with the rules fixes) the character takes what it can carry,
            // platinum first, the rest goes on the counter
            rules::recalc(ch, *names, d->facts);
            int room = rules::max_load(ch) - ch.encumbrance();
            if (room < 0) room = 0;
            over = (rules::fixes() ? plat + gold % 5 : gold) > room;
            const int p = over && plat > room ? room : plat;
            room -= p;
            const int g = over && gold % 5 > room ? room : gold % 5;
            add_coins(ch, 4, p);
            add_coins(ch, 3, g);
            ground->money[4] += plat - p;
            ground->money[3] += gold % 5 - g;
            rules::recalc(ch, *names, d->facts);
            back_to_items(c);
            note(c, over ? iw(profile::kOverloadPool) : iw(profile::kSold));
            return true;
        }
        add_coins(ch, 4, plat);
        add_coins(ch, 3, gold % 5);
        rules::recalc(ch, *names, d->facts);
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
    plat::File& f;
    explicit FileSink(plat::File& file) : f(file) {}
    bool put(const uint8_t* p, size_t n) override { return f.write(p, n) == n; }
};

bool write_file(const char* dir, const char* name, const uint8_t* p, size_t n)
{
    char path[200];
    snprintf(path, sizeof path, "%s/%s", dir, name);      // dir: a full card path
    if (!n) {
        if (plat::exists(path)) plat::remove(path);
        return true;
    }
    // (a save folder the copy left out; looked for once a folder - each
    // look is a directory read on the card, and a save writes ~20 files)
    static uint32_t checked = 0;                // (the folder's name, hashed: 4 bytes of static RAM)
    uint32_t h = 2166136261u;
    for (const char* q = dir; *q; ++q) h = (h ^ static_cast<uint8_t>(*q)) * 16777619u;
    if (h != checked) {
        if (!plat::exists(dir)) plat::mkdir(dir);
        checked = h;
    }
    plat::File f = plat::open(path, "w");
    if (!f) return false;
    const bool ok = f.write(p, n) == n;
    f.close();
    return ok;
}

bool save_game(char slot)
{
    vault_saved(slot);
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
    save_file_name(slot, name, sizeof name);
    save_path(name, path, sizeof path);
    if (!plat::exists(d->save_dir)) plat::mkdir(d->save_dir);
    plat::File f = plat::open(path, "w");
    if (!f) return false;
    bool ok;
    {
        FileSink sink(f);
        if (darkness_files()) {
            // (Darkness: its modes - 4 in 3D, 3 outside; the look kept)
            h.state = h.last_state = static_cast<uint8_t>(d->save.state ? d->save.state : 4);
            h.look = d->save.look;
            h.look_slot = d->save.look_slot;
            ok = savegame::write_darkness(sink, d->gs, h);
        } else {
            ok = savegame::write(sink, d->gs, h, !pool_files(), !secret_files());
        }
    }
    f.close();
    for (int i = 0; ok && i < pt->count; ++i) {
        const party::Character& ch = pt->m[i];
        char fn[48];
        char fi[48], ff[48];
        snprintf(fn, sizeof fn, "%s.SAV", h.names[i]);
        snprintf(fi, sizeof fi, "%s%s", h.names[i], ext_items());
        snprintf(ff, sizeof ff, "%s%s", h.names[i], ext_fx());
        ok = write_character_files(d->save_dir, fn, fi, ff, ch);
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
    plat::log("[play] saved game %c: %s\n", slot, ok ? "ok" : "FAILED");
    return ok;
}

void load_journal_list(char slot)
{
    n_seen = 0;
    if (!d->cache_dir[0]) return;
    char name[24], path[200];
    snprintf(name, sizeof name, "SAVGAM%c.JNL", slot);
    snprintf(path, sizeof path, "%s/%s", d->cache_dir, name);   // cache_dir: a full card path
    plat::File f = plat::open(path, "r");
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
    hl_inherit();
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
    const char slot = text::key(menu, k);
    // "Saving to slot A..." on the screen BEFORE the card's writes (Tom,
    // v0.85.5; the original's "Saving...Please Wait" never showed: the
    // screen only caught up after the tap)
    char t[32];
    snprintf(t, sizeof t, "Saving to slot %c...", slot);
    put(c, t, 0, text::kMenuRow, 10);
    dirty_rows(text::kMenuRow, text::kMenuRow);
    present_now();
    const bool ok = save_game(slot);
    {
        char m[32];
        snprintf(m, sizeof m, "saved to slot %c", slot);
        memlog::note(m);
    }
    back_from_save(c);
    if (!ok) {
        char t[48];
        snprintf(t, sizeof t, "Couldn't save game %c on the card.", slot);
        error(c, t);
    }
}

void draw_own_pic(int id);

// The camp's picture (Treasures: game byte 0x4F's when its scripts set one -
// 132 indoors; ov105+0x3149)
int camp_pic()
{
    const auto& v = d->prof->variant;
    return v.camp_pic_byte && d->gs.area1[0x4F] ? d->gs.area1[0x4F] : v.camp_pic;
}

bool no_rest_here() { return darkness_files() && vm->get(0x2B) == 1 && vm->get(0x2C) >= 100; }

// The camp menu's words (without Rest and Fix where no rest is possible)
bool tr_crown()
{
    // (its camp, ovl 0x74 +0x2EDE: game byte 0x3E set and 0x25B not 2)
    return d->prof->variant.tr && d->w_crown_menu[0] && d->gs.area1[0x3E] && d->gs.area1[0x25B] != 2;
}
void camp_words(char* out, size_t cap)
{
    snprintf(out, cap, "%s", tr_crown() ? d->w_crown_menu : d->w_camp_menu);
    if (!no_rest_here()) return;
    // word 3 (Rest) and 5 (Fix) of "Save View Magic Rest Alter Fix Exit"
    char tmp[sizeof d->w_camp_menu];
    size_t n = 0;
    int w = 0;
    for (const char* p = d->w_camp_menu; *p;) {
        while (*p == ' ') ++p;
        const char* e = p;
        while (*e && *e != ' ') ++e;
        if (e > p && w != 3 && w != 5 && n + (e - p) + 2 < sizeof tmp) {
            if (n) tmp[n++] = ' ';
            memcpy(tmp + n, p, static_cast<size_t>(e - p));
            n += static_cast<size_t>(e - p);
        }
        if (e > p) ++w;
        p = e;
    }
    tmp[n] = 0;
    snprintf(out, cap, "%s", tmp);
}

void draw_camp_screen(pic::Canvas& c)
{
    // The exploring screen (Secret: its camp picture in the view)
    anim_stop();
    pic_shown = false;
    head_shown = body_shown = -1;
    camping = true;
    if (bigpic < 0) {
        draw_frame(c);
        draw_view(c);
        if (d->prof->variant.camp_pic) draw_own_pic(camp_pic());
        draw_panel(c);
    }
}

void draw_camp(pic::Canvas& c)
{
    // The party's camp in the text window
    draw_camp_screen(c);
    text::clear(c, text::kTextArea);
    const auto& tv = d->prof->variant;
    if (tv.tr && d->gs.area1[0x3B]) {
        // Treasures: "The party has acquired the..." and each crystal (game
        // byte 0x3B's bits; ovl 0x74 +0x2D1E)
        char t[40];
        ow(tv.tr_acquired, t, sizeof t);
        put(c, t, 1, 17, 10);
        for (int k = 0; k < 3; ++k)
            if (d->gs.area1[0x3B] & (1 << k)) {
                ow(tv.tr_crystal[k], t, sizeof t);
                put(c, t, 11, 18 + k, 15);
            }
    } else put(c, d->w_makes_camp, 1, 18, 10);
    dirty_rows(17, 22);
    // Darkness takes Rest and Fix off when resting can't be done here
    // (its camp at ovr 0x104F1: byte 0x2B = 1 and 0x2C >= 100 - the town's)
    char words[sizeof d->w_camp_menu];
    camp_words(words, sizeof words);
    text::build(menu, d->w_camp, words);
    menu.selected = 0;
    hl_own(kDkCamp);                    // (Darkness: GAME.OVR 0x104F1's own place, from Save)
    show_menu_line(c);
}

bool camp_from_script = false;     // PROGRAM 9: breaking camp lets the script go on

// Secret's day of rest counts from the camp's start, not each Rest
// (GAME.OVR 0x1D158 zeroes its counter [0x8DB2] as the camp begins; the
// rest's 0x2BEB9 counts on from rest to rest: 288 five-minute steps)
int camp_heal_steps = 0;

bool tr_romance(pic::Canvas& c);
void tr_crown_used(pic::Canvas& c);

void open_camp(pic::Canvas& c)
{
    camp_heal_steps = 0;
    dk_own[kDkCamp] = 0;
    screen = Screen::Camp;
    if (d->prof->variant.tr) {
        draw_camp_screen(c);
        if (tr_romance(c)) return;          // (its texts first, then the camp - ovl 0x74 +0x2D08)
    }
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
    camping = false;
    if (d->prof->variant.camp_pic && pic_shown) {
        anim_stop();
        pic_shown = false;
        draw_view(c);
    }
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
        case 'C':
            if (tr_crown()) tr_crown_used(c);
            break;
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
    char fn[24], fi[24], ff[24];
    snprintf(fn, sizeof fn, "%s%s", base, ext_guy());
    snprintf(fi, sizeof fi, "%s%s", base, ext_items());
    snprintf(ff, sizeof ff, "%s%s", base, ext_fx());
    return write_character_files(d->save_dir, fn, fi, ff, ch);
}

void delete_character(const char* base)
{
    for (const char* ext : {ext_guy(), ext_items(), ext_fx()}) {
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

// ---- Treasures' LUCK.DAX texts ------------------------------------------------------
// PROGRAM 3 (ovl 0x74 +0x2A75): the news - block 0 when game byte 0x25B is
// 1 (then 2: the crown), 12 when 0x1F7 is 0xFF, else the first k of 1-10
// whose byte 0x125 + k isn't 1 (11 when all are); a page (0x0C) at a time
// in the script's text window, green, "press a key" after each; CR / LF /
// ^Z left out; the window cleared at the end. The camp's romance (ovl 0x79
// +0x3CC) prints line by line ("line " - the lines run on), \x02 - \x08 the
// two characters' names, a third member's, "HIS" / "HER".
struct LuckRun {
    bool     on = false, romance = false, prompt = false;
    enum class Then : uint8_t { Vm, Romance, Crown, Paper } then = Then::Vm;
    uint8_t* buf = nullptr;
    uint32_t len = 0, pos = 0;
    char     page[720] = {};
} lk;

// The romance's state: the two (party indices), what's still to do
struct Romance {
    int a = -1, b = -1;                 // the characters named at game bytes 0xF4 / 0xE4
    int acts[6] = {}, n = 0, i = 0;     // LUCK blocks (> 0) and steps (kLeave ...)
    int witness = -1;                   // \x06: a member who isn't one of them (its [0xA44])
    bool asking = false, pic = false;   // "Accept Reject" up; PIC 131 shown
} rom;
constexpr int kRomLeave = -1, kRomAsk = -2, kRomPic = -3;

void lk_free()
{
    free(lk.buf);
    lk.buf = nullptr;
    lk.on = false;
}

bool lk_load(int block)
{
    lk_free();
    plat::File f;
    if (!open_dax("LUCK.DAX", f)) return false;
    plat::FileSource src(f);
    const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
    if (e) lk.buf = static_cast<uint8_t*>(malloc(e->raw_size + 16));
    lk.len = e && lk.buf ? dax::load_block(src, d->idx, *e, lk.buf) : 0;
    f.close();
    if (!lk.len) {
        lk_free();
        plat::log("[play] LUCK %d: not read\n", block);
        return false;
    }
    lk.pos = 0;
    return true;
}

// The name in the game block from `at` (a byte a letter, 0 ends it)
void game_name(int at, char* out, size_t cap)
{
    size_t o = 0;
    for (int k = 0; k < 16 && d->gs.area1[at + k] && o + 1 < cap; ++k) out[o++] = static_cast<char>(d->gs.area1[at + k]);
    out[o] = 0;
}

// \x02 - \x08 (ovl 0x79 +0x122 ... +0x2CE): the one whose count is the
// higher first (0x105 > 0x106: B), the other, A's name, B's, a witness,
// his / her of the first, of the second
void rom_word(int code, char* out, size_t cap)
{
    out[0] = 0;
    const bool b_first = d->gs.area1[0x105] > d->gs.area1[0x106];
    auto sex_word = [&](int who) {
        const party::Character* ch = who >= 0 && who < pt->count ? &pt->m[who] : nullptr;
        const bool her = ch && party::darkness_byte(ch->rec, ch->ext, ch->n_items, 0x166);
        ow(her ? d->prof->variant.tr_her : d->prof->variant.tr_his, out, cap);
    };
    switch (code) {
    case 2: game_name(b_first ? 0xE4 : 0xF4, out, cap); break;
    case 3: game_name(b_first ? 0xF4 : 0xE4, out, cap); break;
    case 4: game_name(0xF4, out, cap); break;
    case 5: game_name(0xE4, out, cap); break;
    case 6:
        if (rom.witness >= 0 && rom.witness < pt->count) pt->m[rom.witness].name(out, cap);
        break;
    case 7: sex_word(b_first ? rom.b : rom.a); break;
    case 8: sex_word(b_first ? rom.a : rom.b); break;
    default: break;
    }
}

// The next page's text from the block (false: none left)
bool lk_page()
{
    size_t o = 0;
    bool any = false;
    while (lk.pos < lk.len) {
        const uint8_t ch = lk.buf[lk.pos++];
        if (ch == 0x0C) {
            if (any) break;
            continue;
        }
        if (ch == 0x1A || ch == 0x0A) continue;
        if (ch == 0x0D) {
            if (lk.romance && o && o + 1 < sizeof lk.page && lk.page[o - 1] != ' ') lk.page[o++] = ' ';
            continue;
        }
        if (lk.romance && ch >= 2 && ch <= 8) {
            char t[24];
            rom_word(ch, t, sizeof t);
            for (const char* q = t; *q && o + 1 < sizeof lk.page; ++q) lk.page[o++] = *q;
            any = true;
            continue;
        }
        if (o + 1 < sizeof lk.page) lk.page[o++] = static_cast<char>(ch);
        if (ch != ' ') any = true;
    }
    lk.page[o] = 0;
    return any;
}

void lk_show(pic::Canvas& c)
{
    text::begin(w, c, lk.page, script_area(), 10, true);
    dirty_rows(17, 22);
    t_started = false;
    lk.prompt = false;
}

void lk_start(pic::Canvas& c, int block, bool romance, LuckRun::Then then)
{
    lk.romance = romance;
    lk.then = then;
    plat::log("[play] LUCK %d\n", block);
    if (!lk_load(block) || !lk_page()) {
        lk_free();
        lk.on = true;               // (nothing to show: straight on)
        lk.prompt = true;
        lk.pos = lk.len = 0;
        lk.page[0] = 0;
        return;
    }
    lk.on = true;
    lk_show(c);
}

void rom_next(pic::Canvas& c);
void lk_end(pic::Canvas& c)
{
    lk_free();
    clear_menu_line(c);
    switch (lk.then) {
    case LuckRun::Then::Vm:
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
        waiting = false;
        handle(vm->resume());
        break;
    case LuckRun::Then::Romance: rom_next(c); break;
    case LuckRun::Then::Paper:
        screen = Screen::Items;
        draw_frame(c);
        draw_panel(c);
        back_to_items(c);
        break;
    case LuckRun::Then::Crown:
        // (the crown's text over its picture; the picture goes after - ovl 0x74 +0x2C5A)
        text::clear(c, text::kTextArea);
        c.fill(24, 24, 88, 88, 0);
        dirty_rows(3, 22);
        pic_shown = false;
        draw_view(c);
        draw_camp(c);
        break;
    }
}

void lk_tick(uint32_t now, pic::Canvas& c)
{
    if (lk.prompt) {
        if (!lk.len) lk_end(c);            // (an empty text)
        else if (in_demo) {
            auto_tap = true;
            tap(0, 0, c);
            auto_tap = false;
        }
        return;
    }
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
    if (w.state == text::State::PageFull || w.state == text::State::Done) {
        clear_menu_line(c);
        put(c, d->press_key, 0, text::kMenuRow, 13);
        lk.prompt = true;
    }
}

void lk_tap(pic::Canvas& c)
{
    if (!lk.prompt) {
        if (w.state == text::State::Writing) {
            text::step(w, c, d->font, -1);      // a tap prints the rest of the page
            dirty_rows(17, 22);
        }
        return;
    }
    clear_menu_line(c);
    if (lk.then == LuckRun::Then::Paper) {
        lk_end(c);
        return;
    }
    if (w.state == text::State::PageFull) {
        dirty_rows(17, 22);
        lk.prompt = false;
        t_last = plat::now_ms();
        return;
    }
    if (lk_page()) lk_show(c);
    else lk_end(c);
}

// The lucky paper (an item whose byte 0x3E is 0x0E; ovl 0xC3 +0x22A7): the
// border screen, "The lucky paper of <its name word> reads:" at (2,2),
// white, then LUCK block 50 + its byte 0x3D a line (LF) every second row
// from row 7, yellow - each letter shown only with its crystal (game byte
// 0x3B: letters take the bits 1, 2, 4 in turn), else "@"
void back_to_items(pic::Canvas& c);
void lucky_paper(const uint8_t* it, pic::Canvas& c)
{
    const int block = 50 + it[0x3D];
    if (!lk_load(block)) return;
    plat::log("[play] LUCK %d (lucky paper)\n", block);
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    char t[64], wd[24];
    ow(d->prof->variant.tr_paper[0], t, sizeof t);
    snprintf(wd, sizeof wd, "%s", names->word(it[0x2F]));
    size_t o = strlen(t);
    snprintf(t + o, sizeof t - o, "%s", wd);
    o = strlen(t);
    ow(d->prof->variant.tr_paper[1], t + o, sizeof t - o);
    put(c, t, 2, 2, 15);
    char line[48];
    size_t n = 0;
    int row = 7, mask = 1;
    for (uint32_t k = 0; k < lk.len; ++k) {
        const uint8_t ch = lk.buf[k];
        if (ch == 0x0A) {
            line[n] = 0;
            if (row < 25) put(c, line, 2, row, 14);
            row += 2;
            n = 0;
            continue;
        }
        if (ch == 0x0D || ch == 0x1A) continue;
        char shown = ' ';
        if (ch != ' ') {
            shown = d->gs.area1[0x3B] & mask ? static_cast<char>(ch) : '@';
            mask = mask >= 4 ? 1 : mask * 2;
        }
        if (n + 1 < sizeof line) line[n++] = shown;
    }
    lk_free();
    dirty(0, pic::kScreenH);
    clear_menu_line(c);
    put(c, d->press_key, 0, text::kMenuRow, 13);
    lk.on = true;
    lk.prompt = true;
    lk.len = 1;
    lk.then = LuckRun::Then::Paper;
}

// PROGRAM 3's block (ovl 0x74 +0x2A75)
int luck_news_block()
{
    uint8_t* g = d->gs.area1;
    if (g[0x25B] == 1) {
        g[0x25B] = 2;
        return 0;
    }
    if (g[0x1F7] == 0xFF) return 12;
    int k = 1;
    while (k <= 10 && g[0x125 + k] == 1) ++k;
    return k;
}

// The camp's Crown: its picture (PIC 130) and PROGRAM 3's text
void tr_crown_used(pic::Canvas& c)
{
    anim_stop();
    c.fill(24, 24, 88, 88, 0);
    draw_own_pic(130);
    pic_shown = true;
    clear_menu_line(c);
    lk_start(c, luck_news_block(), false, LuckRun::Then::Crown);
}

// The romance (ovl 0x79 +0x742, +0x8E6): with game byte 0x104 set, the
// two named characters get effect 0x83 (anyone else loses it); when both
// are up and the stages of bytes 0x105 / 0x106 (count / 25, at most 4)
// have changed since bytes 0x4C / 0x4B, the stages' text (LUCK 101-117)
bool tr_romance(pic::Canvas& c)
{
    uint8_t* g = d->gs.area1;
    rom = Romance{};
    if (!g[0x104] || !pt || !names) return false;
    char an[20], bn[20], nm[20];
    game_name(0xF4, an, sizeof an);
    game_name(0xE4, bn, sizeof bn);
    for (int i = 0; i < pt->count; ++i) {
        pt->m[i].name(nm, sizeof nm);
        if (rom.a < 0 && an[0] && strcmp(nm, an) == 0) rom.a = i;
        if (rom.b < 0 && bn[0] && strcmp(nm, bn) == 0) rom.b = i;
    }
    constexpr uint8_t kFx = 0x83;
    for (int i = 0; i < pt->count; ++i) {
        party::Character& ch = pt->m[i];
        const bool one = i == rom.a || i == rom.b;
        if (one && !ch.has_affect(kFx)) spells::add_affect(ch, kFx, 0, 0, false);
        else if (!one && ch.has_affect(kFx)) rules::remove_affects(ch, kFx);
    }
    if (rom.a < 0 || rom.b < 0) return false;
    auto up = [&](int i) { return party::darkness_byte(pt->m[i].rec, pt->m[i].ext, pt->m[i].n_items, 0x1EE) != 0; };
    if (!up(rom.a) || !up(rom.b)) return false;
    auto stage = [](int v) { return v / 25 < 4 ? v / 25 : 4; };
    const int sa = stage(g[0x105]), sb = stage(g[0x106]);
    if (sb == g[0x4B] && sa == g[0x4C]) return false;
    g[0x4B] = static_cast<uint8_t>(sb);
    g[0x4C] = static_cast<uint8_t>(sa);
    for (int i = 0; i < pt->count; ++i)
        if (i != rom.a && i != rom.b && up(i)) {
            rom.witness = i;
            break;
        }
    // The text for each pair of stages (ovl 0x79 +0x9A9 ...)
    static const int8_t kText[5][5] = {{0, 1, 2, 3, 4}, {1, 5, 6, 7, 8}, {2, 6, 9, 10, 11}, {3, 7, 10, 12, 13},
                                       {4, 8, 11, 12, 0}};
    auto add = [&](int v) {
        if (rom.n < 6) rom.acts[rom.n++] = v;
    };
    if (sa == 4 && sb == 4) {
        add(kRomPic);
        add(114);
        add(kRomAsk);
    } else if (kText[sa][sb]) {
        add(100 + kText[sa][sb]);
        if (kText[sa][sb] == 4) add(kRomLeave);         // (104: A leaves the party)
    } else return false;
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    rom_next(c);
    return true;
}

void rom_leave(pic::Canvas& c)
{
    if (rom.a < 0) return;
    if (rom.b > rom.a) --rom.b;
    leave_party(rom.a);
    rom.a = -1;
    draw_party(c, 17);
}

void rom_next(pic::Canvas& c)
{
    while (rom.i < rom.n) {
        const int a = rom.acts[rom.i++];
        if (a > 0) {
            lk_start(c, a, true, LuckRun::Then::Romance);
            return;
        }
        if (a == kRomLeave) rom_leave(c);
        else if (a == kRomPic) {
            anim_stop();
            c.fill(24, 24, 88, 88, 0);
            draw_own_pic(131);
            pic_shown = true;
            rom.pic = true;
        } else if (a == kRomAsk) {
            char words[24];
            ow(d->prof->variant.tr_accept, words, sizeof words);
            text::build(menu, "", words);
            menu.selected = 0;
            show_menu_line(c);
            lk.on = true;               // (the answer comes to lk_tap's menu)
            lk.prompt = false;
            lk.then = LuckRun::Then::Romance;
            rom.asking = true;
            return;
        }
    }
    // Done: the camp as usual
    if (rom.pic) {
        rom.pic = false;
        pic_shown = false;
        c.fill(24, 24, 88, 88, 0);
        draw_view(c);
    }
    draw_camp(c);
}

// "Accept Reject" (ovl 0x79 +0xC76): Accept - LUCK 115; Reject - 116, A
// leaves, 117, and B's Charisma (its own and in use) 6 lower (0 at least),
// B's effect 0x83 marked (byte 3 = 1)
void rom_answer(int k, pic::Canvas& c)
{
    clear_menu_line(c);
    lk.on = false;
    rom.asking = false;
    rom.n = rom.i = 0;
    if (k == 0) {
        rom.acts[rom.n++] = 115;
    } else {
        rom.acts[rom.n++] = 116;
        rom.acts[rom.n++] = kRomLeave;
        rom.acts[rom.n++] = 117;
        if (rom.b >= 0) {
            party::Character& ch = pt->m[rom.b];
            for (int o = 0x1A; o <= 0x1B; ++o) ch.rec[o] = ch.rec[o] > 6 ? static_cast<uint8_t>(ch.rec[o] - 6) : 0;
            for (int i = 0; i < ch.n_affects; ++i)
                if (ch.affects[i][0] == 0x83) ch.affects[i][3] = 1;
            rules::recalc(ch, *names, d->facts);
        }
    }
    rom_next(c);                        // (the picture stays up until the end)
}

void ask_yes_no(pic::Canvas& c, Ask what, const char* prompt)
{
    ask = what;
    screen = Screen::YesNo;
    text::build(menu, prompt, rw(Data::kYesNo));
    menu.selected = 1;          // the games start on No
    dk_slot = -1;
    // (Darkness's camp "Lose it?" lights Yes - GAME.OVR 0x2AFCB: 0x108:0x43 with 'Y')
    if (what == Ask::LoseIt && kept_hl()) menu.selected = 0;
    // (Gateway's questions start on Yes - its question routine, seg 0x116
    // entry 0x39, GAME.OVR 0x46203; DOSBox)
    if (gateway()) menu.selected = 0;
    show_menu_line(c);
}

// Gateway's questions about memorizing, scribing, overwriting a character
// and stopping a spell have a yellow prompt (its question routine's callers
// pass 14, the rest 13 - GAME.OVR 0x1F68C, 0x1F7C0, 0x1F8E6, 0x1FB58,
// 0x22BBE, 0x361BB; DOSBox)
void ask_yellow(pic::Canvas& c)
{
    if (!gateway()) return;
    menu.prompt_fg = 14;
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
    snprintf(fn, sizeof fn, "%s%s", base, ext_guy());
    save_path(fn, path, sizeof path);
    if (!d->prof->party.overwrite) overwrite_ok = true;       // (Pool doesn't ask)
    if (!overwrite_ok && plat::exists(path)) {
        char t[60];
        snprintf(t, sizeof t, "%s%s%s", rw(Data::kOverwrite), base, rw(Data::kQmark));
        ask_yes_no(c, Ask::Overwrite, t);
        ask_yellow(c);
        return;
    }
    new_base[0] = 0;
    if (!write_character(base, *ch)) {
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        char t[48];
        snprintf(t, sizeof t, "Couldn't write %s%s to the card.", base, ext_guy());
        error(c, t);
        return;
    }
    leave_party(pt->selected);
    screen = Screen::PartyMenu;
    draw_party_menu(c);
}

bool create_yes_no(Ask what, char k, pic::Canvas& c);

void yes_no_key(char k, pic::Canvas& c);

// The place of the last Yes / No answered (0 / 1): Darkness's icon editor
// after creation's "Reroll stats?" starts with that place lit (its menus
// keep the highlight's place - alter.kept_highlight)
int yes_no_place = 0;

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
    yes_no_place = k == 'N' ? 1 : 0;
    if (create_yes_no(what, k, c)) return;
    if (view_yes_no(what, k, c)) return;
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
// The earlier game's save folder on the card (its characters can be added:
// Curse's Pool, Secret's Curse; "" if none)
char import_dir[128] = {};
char darkness_dir[128] = {};    // the player's Pools of Darkness folder ("" if none): its font
Options opts;                   // the engine comforts switched on (Settings)

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
        plat::File dir = plat::open(where, "r");
        if (!dir || !dir.isDirectory()) return;
        for (plat::File f = dir.openNextFile(); f && d->guys < Data::kMaxGuys; f = dir.openNextFile()) {
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

// Curse's characters for Secret (its "Curse": *.guy in its save folder,
// GAME.OVR 0x23C7A; and - more characters - Curse's own folder on the card,
// .GUY and the saved games' members)
void sort_guys();
void find_curse_guys(const char* where, uint8_t src, bool members)
{
    if (!where[0]) return;
    plat::File dir = plat::open(where, "r");
    if (!dir || !dir.isDirectory()) return;
    for (plat::File f = dir.openNextFile(); f && d->guys < Data::kMaxGuys; f = dir.openNextFile()) {
        const char* nm = f.name();
        const char* slash = strrchr(nm, '/');
        if (slash) nm = slash + 1;
        const size_t n = strlen(nm);
        const bool guy = n >= 5 && n <= 12 && strcasecmp(nm + n - 4, ".GUY") == 0;
        const bool member = members && n == 12 && strncasecmp(nm, "CHRDAT", 6) == 0 && strcasecmp(nm + 8, ".SAV") == 0;
        if (f.isDirectory() || (!guy && !member) || f.size() != party::kRecordSize) continue;
        uint8_t rec[0x100];
        if (f.read(rec, 0xF8) != 0xF8 || rec[0xF7] > 0x7F) continue;      // NPCs aren't listed
        char name[16];
        const size_t len = rec[0] > 15 ? 15 : rec[0];
        memcpy(name, rec + 1, len);
        name[len] = 0;
        if (in_party(name)) continue;
        strncpy(d->guy_file[d->guys], nm, sizeof d->guy_file[0] - 1);
        strcpy(d->guy_name[d->guys], name);
        d->guy_added[d->guys] = false;
        d->guy_src[d->guys] = src;
        d->guy_letter[d->guys] = member ? static_cast<char>(toupper(nm[6])) : 0;
        ++d->guys;
    }
    dir.close();
}

void find_guys(char from = 'C')
{
    d->guys = 0;
    d->guy_from = from;
    if (from == 'C' && secret_files()) {
        const bool more = CYD_MORE_CHARACTERS && opts.more_guys;
        find_curse_guys(d->save_dir, 3, false);
        if (more && import_dir[0] && strcasecmp(import_dir, d->save_dir) != 0) find_curse_guys(import_dir, 4, true);
        sort_guys();
        return;
    }
    if (from == 'P') {
        find_pool_guys(d->save_dir, 1);
        if (CYD_MORE_CHARACTERS && opts.more_guys && import_dir[0] && strcasecmp(import_dir, d->save_dir) != 0)
            find_pool_guys(import_dir, 2);
        return;
    }
    plat::File dir = plat::open(d->save_dir, "r");
    if (!dir || !dir.isDirectory()) return;
    for (plat::File f = dir.openNextFile(); f && d->guys < Data::kMaxGuys; f = dir.openNextFile()) {
        const char* nm = f.name();
        const char* slash = strrchr(nm, '/');
        if (slash) nm = slash + 1;
        const size_t n = strlen(nm);
        // .GUY files, and (Tom, v0.58.0 - the original lists only those)
        // the members of saved games, CHRDAT<letter><n>.SAV
        // (Pool of Radiance: .CHA, 285-byte records, the control byte at 0x84)
        // (the extension's own length: Darkness's ".PC" is 3)
        const size_t ne = strlen(ext_guy());
        const bool guy = n > ne && n <= 12 && strcasecmp(nm + n - ne, ext_guy()) == 0;
        const bool member = CYD_MORE_CHARACTERS && opts.more_guys && n == 12 && strncasecmp(nm, "CHRDAT", 6) == 0 &&
                            strcasecmp(nm + 8, ".SAV") == 0;
        if (f.isDirectory() || (!guy && !member) || f.size() != rec_file_size()) continue;
        uint8_t rec[0x150];
        const int ctl = pool_files() ? 0x84 : secret_files() ? 0xFF : darkness_files() ? 0x147 : 0xF7;     // (Secret's record: 0xFF)
        if (f.read(rec, ctl + 1) != ctl + 1 || rec[ctl] > 0x7F) continue;     // NPCs aren't listed
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
    sort_guys();
}

// In order: the .GUY characters by name, then each saved game's members
// (game A first, in party order)
void sort_guys()
{
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
            const uint8_t sr = d->guy_src[j]; d->guy_src[j] = d->guy_src[j - 1]; d->guy_src[j - 1] = sr;
        }
}

bool load_pool_guy(int i, party::Character& ch);
bool load_curse_guy(int i, party::Character& ch);

// Darkness's Add from Secret (darkness_facts.md 33): Secret's *.who of
// 439 bytes in its folder (src 5), not its NPCs (control 0xFF), not in the
// party; read as Secret's (.STF items, .SFX effects), then converted
void find_secret_guys()
{
    if (!import_dir[0]) return;
    plat::File dir = plat::open(import_dir, "r");
    if (!dir || !dir.isDirectory()) return;
    for (plat::File f = dir.openNextFile(); f && d->guys < Data::kMaxGuys; f = dir.openNextFile()) {
        const char* nm = f.name();
        const char* slash = strrchr(nm, '/');
        if (slash) nm = slash + 1;
        const size_t n = strlen(nm);
        const Earlier& ea = earlier();
        if (f.isDirectory() || n < 5 || n > 12 || strcasecmp(nm + n - 4, ea.guy) != 0 || f.size() != ea.rec_size)
            continue;
        uint8_t rec[0x100];
        if (f.read(rec, sizeof rec) != sizeof rec || rec[ea.ctl] > 0x7F) continue;
        char name[16];
        const size_t len = rec[0] > 15 ? 15 : rec[0];
        memcpy(name, rec + 1, len);
        name[len] = 0;
        if (in_party(name)) continue;
        strncpy(d->guy_file[d->guys], nm, sizeof d->guy_file[0] - 1);
        strcpy(d->guy_name[d->guys], name);
        d->guy_added[d->guys] = false;
        d->guy_src[d->guys] = 5;
        d->guy_letter[d->guys] = 0;
        ++d->guys;
    }
    dir.close();
}

void secret_to_darkness(party::Character& ch);
bool load_secret_guy(int i, party::Character& ch)
{
    char base[16], path[200];
    strncpy(base, d->guy_file[i], sizeof base - 1);
    base[sizeof base - 1] = 0;
    char* dot = strrchr(base, '.');
    if (dot) *dot = 0;
    snprintf(path, sizeof path, "%s/%s", import_dir, d->guy_file[i]);
    plat::File f = plat::open(path, "r");
    if (!f) return false;
    bool ok;
    const Earlier& ea = earlier();
    {
        plat::FileSource src(f);
        ok = party::read_record(src, ch, ea.fmt);
    }
    f.close();
    if (!ok) return false;
    snprintf(path, sizeof path, "%s/%s%s", import_dir, base, ea.items);
    f = plat::open(path, "r");
    if (f) {
        plat::FileSource src(f);
        party::read_items(src, ch, ea.item_size);
        f.close();
    }
    snprintf(path, sizeof path, "%s/%s%s", import_dir, base, ea.fx);
    f = plat::open(path, "r");
    if (f) {
        plat::FileSource src(f);
        party::read_affects(src, ch);
        f.close();
    }
    import_convert(ch);
    character_read(ch);
    return true;
}

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
    // (Gateway keeps "Add "'s space - it lights with the word: DOSBox)
    if (!gateway())
        for (size_t n = strlen(what); n && what[n - 1] == ' ';) what[--n] = 0;
    draw_list(c, rw(Data::kAddPrompt), what);
    if (gateway() && menu.count > 0 && what[0] && what[strlen(what) - 1] == ' ') {
        menu.end[0] = static_cast<int8_t>(menu.start[0] + strlen(what) - 1);
        show_menu_line(c);
    }
}

// Adds the chosen character, with the games' rules
void add_checked(int i, party::Character& ch, pic::Canvas& c);

void add_character(int i, pic::Canvas& c)
{
    if (i < 0 || i >= d->guys || d->guy_added[i] || pt->count >= party::kMaxParty) return;
    party::Character& ch = pt->m[pt->count];
    if (d->guy_src[i] == 5) {
        if (!load_secret_guy(i, ch)) return;
        add_checked(i, ch, c);
        return;
    }
    if (d->guy_src[i] == 3 || d->guy_src[i] == 4) {
        if (!load_curse_guy(i, ch)) return;
        add_checked(i, ch, c);
        return;
    }
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
    plat::File f;
    if (!open_save_file(d->guy_file[i], f)) return;
    bool ok;
    {
        plat::FileSource src(f);
        ok = party::read_record(src, ch, rec_format());
    }
    f.close();
    if (!ok) return;
    char fn[24];
    snprintf(fn, sizeof fn, "%s%s", base, ext_items());
    if (open_save_file(fn, f)) {
        plat::FileSource src(f);
        party::read_items(src, ch, item_stride());
        f.close();
    }
    snprintf(fn, sizeof fn, "%s%s", base, ext_fx());
    if (open_save_file(fn, f)) {
        plat::FileSource src(f);
        party::read_affects(src, ch);
        f.close();
    }
    character_read(ch);
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
    // (the first source is the game's own: Curse's "Curse Pool Hillsfar",
    // Secret's "Secret Curse" - its C is Curse's characters)
    char k = text::key(menu, text::hit(menu, x / 8));
    const char own = static_cast<char>(toupper(static_cast<unsigned char>(rw(Data::kAddSources)[0])));
    if (secret_files() && k == 'C') {
        find_guys('C');
        if (!d->guys) {
            screen = Screen::PartyMenu;
            draw_party_menu(c);
            return;
        }
        screen = Screen::AddList;
        plist = PickList{};
        draw_add_list(c);
        return;
    }
    if (darkness_files() && k == (d->prof->variant.tr ? 'G' : 'S')) {
        // (Darkness: Secret's characters - its folder on the card; Treasures:
        // Gateway's)
        d->guys = 0;
        d->guy_from = 'S';
        find_secret_guys();
        if (!d->guys) {
            screen = Screen::PartyMenu;
            draw_party_menu(c);
            return;
        }
        screen = Screen::AddList;
        plist = PickList{};
        draw_add_list(c);
        return;
    }
    if (k == own) k = 'O';
    switch (k) {
    case 'O':
        find_guys('O');
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
    // Pool: the portrait picker's "Head Body Keep", its HEAD / BODY blocks for heads 1-14 / bodies 1-12
    char portrait_menu[24] = {};
    uint8_t head_map[15] = {}, body_map[13] = {};
    bool kit_given = false;         // Darkness: the starting kit (at the save)
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
// The class / spell rule tables alone (from the program's data segment)
bool read_rule_tables(plat::FileSource& src, exepack::Info& info, classes::Tables& t)
{
    const auto& pc = d->prof->create;
    const size_t n = pc.tables.hi - pc.tables.lo;
    // (read straight into the tables' own block: no copy at a fight's peak)
    uint8_t* buf = new (std::nothrow) uint8_t[n];
    // (Pool: the class flags / masks elsewhere in the data segment)
    uint8_t buf2[32];
    const size_t n2 = pc.tables.hi2 > pc.tables.lo2 ? pc.tables.hi2 - pc.tables.lo2 : 0;
    const bool ok = buf && exepack::read(src, info, pc.ds_image + pc.tables.lo, buf, n) == exepack::Status::Ok &&
                    (!n2 || (n2 <= sizeof buf2 &&
                             exepack::read(src, info, pc.ds_image + pc.tables.lo2, buf2, n2) == exepack::Status::Ok));
    if (!ok) {
        delete[] buf;
        return false;
    }
    return t.adopt(pc.tables, buf, n, buf2, n2);
}

// The magic's rule tables (spells, slots, class values) without the rest of
// character making (v0.87.1: Making's 6.9 KB stayed through fights for
// these 4.7 KB)
struct MagicRules {
    classes::Tables tables;
};

bool load_rules(MagicRules*& m)
{
    if (!d->prof->create.ds_image) return false;
    m = new (std::nothrow) MagicRules;
    if (!m) return false;
    plat::File f;
    bool ok = false;
    if (open_file(d->prof->program, f)) {
        plat::FileSource src(f);
        exepack::Info info;
        ok = exepack::parse(src, info) == exepack::Status::Ok && read_rule_tables(src, info, m->tables);
        f.close();
    }
    if (!ok) {
        delete m;
        m = nullptr;
    }
    return ok;
}

// A character's class values worked out with the game's tables (loaded for
// the moment: Darkness's Load from Secret)
void dk_import_values(party::Character& ch)
{
    const int hp_max = ch.hp_max();
    MagicRules* m = nullptr;
    if (load_rules(m)) {
        classes::class_bonuses(ch, m->tables);
        create::dk_con_recompute(ch, m->tables);
        if (ch.hp_max() < hp_max) {
            ch.rec[0x12C] = static_cast<uint8_t>(ch.rec[0x12C] + hp_max - ch.hp_max());
            create::dk_con_recompute(ch, m->tables);
        }
    }
    delete m;
}

bool load_rules(Making*& m)
{
    const auto& pc = d->prof->create;
    if (!pc.ds_image) return false;
    m = new (std::nothrow) Making;
    if (!m) return false;
    m->dice = create::Dice(static_cast<uint32_t>(plat::now_ms()) * 2654435761u + 1);
    plat::File f;
    if (!open_file(d->prof->program, f)) return false;
    bool ok = false;
    {
        plat::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok) {
            ok = read_rule_tables(src, info, m->tables);
            uint8_t hp[16];
            ok = ok && exepack::read(src, info, pc.ds_image + pc.hp_count, hp, 8) == exepack::Status::Ok &&
                 exepack::read(src, info, pc.ds_image + pc.hp_dice, hp + 8, 8) == exepack::Status::Ok &&
                 exepack::read(src, info, pc.ds_image + pc.icon_colours, m->facts.icon_colours, 6) ==
                     exepack::Status::Ok;
            memcpy(m->facts.hp_count, hp, 8);
            memcpy(m->facts.hp_dice, hp + 8, 8);
            if (pc.head_blocks && pc.body_blocks &&
                (exepack::read(src, info, pc.ds_image + pc.head_blocks, m->head_map, sizeof m->head_map) !=
                     exepack::Status::Ok ||
                 exepack::read(src, info, pc.ds_image + pc.body_blocks, m->body_map, sizeof m->body_map) !=
                     exepack::Status::Ok))
                memset(m->head_map, 0, sizeof m->head_map);
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
    fa.poison_save = pc.poison_save;
    memcpy(fa.mu_first, pc.mu_first, 4);
    fa.mu_level2 = pc.mu_level2;
    memcpy(fa.mu_level3, pc.mu_level3, 2);
    fa.mu_level4 = pc.mu_level4;
    fa.mu_level5 = pc.mu_level5;
    memcpy(fa.mu_change, pc.mu_change, 4);
    fa.mu_by_level = pc.mu_by_level;
    fa.mu_by_level_from = pc.mu_by_level_from;
    fa.mu_by_level_n = pc.mu_by_level_n;
    fa.mu_by_slot = pc.mu_by_slot;
    fa.mu_by_slot_from = pc.mu_by_slot_from;
    fa.mu_by_slot_n = pc.mu_by_slot_n;
    fa.mu_all_levels = pc.mu_all_levels;
    fa.mu_all_slot = pc.mu_all_slot;
    fa.start_xp = pc.start_xp;
    fa.platinum = pc.platinum;
    const uint32_t at[9] = {pc.pick_race, pc.pick_gender, pc.pick_class, pc.pick_alignment, pc.select,
                            pc.reroll,    pc.char_name,   pc.save_q,     pc.qmark};
    const uint32_t ch_at[5] = {pc.pick_new, pc.no_qualify, pc.change_select, pc.now_first, pc.dot};
    OvrWords src;
    if (src.open()) {
        for (int i = 0; i < 9; ++i)
            if (at[i]) text::read_pascal(src, at[i], m->words[i], sizeof m->words[i]);
        for (int i = 0; i < 5; ++i)
            if (ch_at[i]) text::read_pascal(src, ch_at[i], m->change[i], sizeof m->change[i]);
        if (pc.portrait_menu) text::read_pascal(src, pc.portrait_menu, m->portrait_menu, sizeof m->portrait_menu);
    }
    return ok;
}

bool load_making() { return load_rules(mk); }

// A Curse character into ch as Secret takes one in (GAME.OVR 0x23C7A on,
// its conversion 0x24CB0 - secret_facts_2.md 20): the record (Curse's,
// the engine's own layout), no items, money or effects (it reads NAME.sfx,
// which Curse doesn't make), no spells in memory, the spell book without
// the spells Secret numbers otherwise (36 56 63 90 96 98 100); a paladin's
// cure of disease (1) and Protection from Evil, a ranger's giant-class
// bonus effect (data 0xFF); the class values again
bool load_curse_guy(int i, party::Character& ch)
{
    char path[200];
    snprintf(path, sizeof path, "%s/%s", d->guy_src[i] == 4 ? import_dir : d->save_dir, d->guy_file[i]);
    plat::File f = plat::open(path, "r");
    if (!f) return false;
    bool ok;
    {
        plat::FileSource src(f);
        ok = party::read_record(src, ch, party::Format::Curse);
    }
    f.close();
    if (!ok) return false;
    ch.n_items = 0;
    ch.n_affects = 0;
    memset(ch.ext, 0, sizeof ch.ext);
    memset(ch.rec + 0xFB, 0, 14);                   // money
    memset(ch.rec + 0x1E, 0, 84);                   // spells in memory
    static const uint8_t kRenumbered[] = {0x24, 0x38, 0x3F, 0x5A, 0x60, 0x62, 0x64};
    for (uint8_t sp : kRenumbered) ch.rec[0x78 + sp] = 0;
    const auto& pc = d->prof->create;
    if (ch.level(classes::Paladin) > 0) {
        ch.rec[0x191] = 1;
        if (pc.prot_evil) spells::add_affect(ch, pc.prot_evil, 0, 0xFF, false);
    }
    if (ch.level(classes::Ranger) > 0 && pc.ranger_giant) spells::add_affect(ch, pc.ranger_giant, 0, 0xFF, false);
    MagicRules* mr = nullptr;
    if (load_rules(mr)) classes::class_bonuses(ch, mr->tables);
    delete mr;
    return true;
}

// A Pool character into ch (false: unreadable, or no rule tables)
bool load_pool_guy(int i, party::Character& ch)
{
    char path[200];
    const char* where = d->guy_src[i] == 2 ? import_dir : d->save_dir;
    snprintf(path, sizeof path, "%s/%s", where, d->guy_file[i]);
    plat::File f = plat::open(path, "r");
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
        plat::File s = plat::open(path, "r");
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
    case 0: mk->n_opt = create::races(mk->opt, 17, secret_files() || darkness_files()); break;
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
        ask_create(c, Ask::Reroll, mk->words[5]);
        return;
    }
}

void start_icon(pic::Canvas& c, Screen from);
void create_after_icon(pic::Canvas& c);

// Creation's questions: Darkness's start on Yes (its menu call takes 'Y' as
// the default - GAME.OVR 0x16169, 0x1624D; DOSBox), the others' on No
void ask_create(pic::Canvas& c, Ask what, const char* prompt)
{
    ask_yes_no(c, what, prompt);
    if (darkness_files()) {
        menu.selected = 0;
        show_menu_line(c);
    } else if (gateway() && what == Ask::Reroll) {
        menu.selected = 1;          // (Gateway's "Reroll stats?" is a menu of its own on No - DOSBox)
        show_menu_line(c);
    }
}

// The name, then the icon editor (as the games do), then "save NAME?"
void start_portrait(pic::Canvas& c);

void create_named(const char* name, pic::Canvas& c)
{
    if (!mk) return;
    create::set_name(mk->ch, name);
    if (mk->portrait_menu[0] && d->prof->create.head_blocks) {
        start_portrait(c);
        return;
    }
    start_icon(c, Screen::CreateName);
    if (screen != Screen::Icon) create_after_icon(c);      // (no memory for the editor)
}

void save_new(pic::Canvas& c, bool overwrite_ok);

void create_after_icon(pic::Canvas& c)
{
    if (!mk) return;
    if (!d->prof->create.save_q) {
        // Pool: no "Save NAME?" - the character is saved (over one of that name)
        save_new(c, true);
        return;
    }
    const bool from_icon = screen == Screen::Icon;
    screen = Screen::CreateName;
    // (Darkness asks it over the icon editor's screen - DOSBox)
    if (!(from_icon && darkness_files())) show_new_character(c);
    char t[72], nm[20];
    mk->ch.name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s%s", mk->words[7], nm, mk->words[8]);
    ask_create(c, Ask::SaveNew, t);
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

// Pool: a new character's name goes on the save folder's CHARLIST.TXT too
// (a name a line; the original's list of characters made)
void add_to_charlist(const party::Character& ch)
{
    char path[200], nm[20];
    ch.name(nm, sizeof nm);
    save_path(d->prof->create.charlist, path, sizeof path);
    constexpr size_t kCap = 1024;                       // (on the heap: static RAM is dear)
    char* buf = static_cast<char*>(malloc(kCap));
    if (!buf) return;
    size_t n = 0;
    plat::File f = plat::open(path, "r");
    if (f) {
        const int got = f.read(reinterpret_cast<uint8_t*>(buf), kCap - 40);
        f.close();
        n = got > 0 ? static_cast<size_t>(got) : 0;
    }
    // Already listed: nothing to add
    for (size_t i = 0; i < n;) {
        size_t e = i;
        while (e < n && buf[e] != '\r' && buf[e] != '\n') ++e;
        if (e - i == strlen(nm) && strncmp(buf + i, nm, e - i) == 0) {
            free(buf);
            return;
        }
        i = e;
        while (i < n && (buf[i] == '\r' || buf[i] == '\n')) ++i;
    }
    if (n && buf[n - 1] != '\n') {
        buf[n++] = '\r';
        buf[n++] = '\n';
    }
    n += static_cast<size_t>(snprintf(buf + n, kCap - n, "%s\r\n", nm));
    write_file(d->save_dir, d->prof->create.charlist, reinterpret_cast<const uint8_t*>(buf), n);
    free(buf);
}

// Darkness's starting kit (darkness_create_facts.md; GAME.OVR 0x19558, as
// its creation saves): the kind's six item numbers from the program's data
// segment, their 17-byte rows from ITEM0.DAX, the items readied as it does
__attribute__((noinline)) void give_kit(party::Character& ch)
{
    const auto& pc = d->prof->create;
    if (!pc.kit) return;
    const int kind = create::dk_kit_kind(ch);
    uint8_t ids[6] = {}, recs[6][17];
    memset(recs, 0xFF, sizeof recs);
    {
        plat::File f;
        if (!open_file(d->prof->program, f)) return;
        plat::FileSource src(f);
        exepack::Info info;
        const bool ok = exepack::parse(src, info) == exepack::Status::Ok &&
                        exepack::read(src, info, pc.ds_image + pc.kit + 6 * kind + 1, ids, 6) == exepack::Status::Ok;
        f.close();
        if (!ok) return;
    }
    plat::File f;
    if (!open_dax("ITEM0.DAX", f)) return;
    {
        plat::FileSource src(f);
        dax::Entry e;
        uint32_t data_start = 0;
        if (dax::find_entry(src, pc.kit_block, e, data_start)) {
            // (the rows in order: read along, keeping the six asked for)
            dax::RleReader r(src, data_start, e);
            uint8_t row[17];
            for (int n = 1; n <= 255 && r.read(row, sizeof row) == sizeof row; ++n)
                for (int j = 0; j < 6; ++j)
                    if (ids[j] == n) memcpy(recs[j], row, sizeof row);
        }
    }
    f.close();
    create::dk_kit(ch, kind, ids, recs);
    rules::recalc(ch, *names, d->facts);
}

// Saves the new character as NAME.GUY (asking first if there is one)
void save_new(pic::Canvas& c, bool overwrite_ok)
{
    char base[12], fn[24], path[200];
    guy_base(mk->ch, base, sizeof base);
    if (new_base[0]) snprintf(base, sizeof base, "%s", new_base);       // the name typed (Overwrite -> No)
    snprintf(fn, sizeof fn, "%s%s", base, ext_guy());
    save_path(fn, path, sizeof path);
    if (!overwrite_ok && plat::exists(path)) {
        char t[60];
        snprintf(t, sizeof t, "%s%s%s", rw(Data::kOverwrite), base, rw(Data::kQmark));
        ask_yes_no(c, Ask::OverwriteNew, t);
        ask_yellow(c);
        return;
    }
    new_base[0] = 0;
    if (!mk->kit_given) {
        mk->kit_given = true;
        give_kit(mk->ch);
    }
    const bool ok = write_character(base, mk->ch);
    if (ok && d->prof->create.charlist) add_to_charlist(mk->ch);
    end_create(c);
    if (!ok) {
        char t[48];
        snprintf(t, sizeof t, "Couldn't write %s%s to the card.", base, ext_guy());
        error(c, t);
    }
}


bool create_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what == Ask::Reroll) {
        // Pool asks "Keep this character? " (No rolls again)
        if (d->prof->create.keep_question ? k != 'Y' : k == 'Y') {
            create::roll(mk->ch, mk->tables, mk->facts, mk->dice);
            rules::recalc(mk->ch, *names, d->facts);
            show_new_character(c);
            ask_create(c, Ask::Reroll, mk->words[5]);
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

// ---- Pool's portrait picker (create_facts.md 1.15): "Head Body Keep" -
// the next head (1-14) / body (1-12), round again; only Keep leaves; the
// pictures in a box at cells 28-38, rows 1-11 (HEAD3 / BODY3, the party
// menu's area)
void draw_portrait(pic::Canvas& c)
{
    uint8_t* r = mk->ch.rec;
    int head = r[0x13F], body = r[0x140];
    if (head < 1 || head > 14) head = 1;
    if (body < 1 || body > 12) body = 1;
    const int keep = d->gs.game_area;
    d->gs.game_area = 3;
    c.fill(28 * 8, 8, 11 * 8, 11 * 8, 0);
    draw_block(c, "HEAD", mk->head_map[head], 28 * 8, 8);
    draw_block(c, "BODY", mk->body_map[body], 28 * 8, 8 + 40);
    d->gs.game_area = static_cast<uint8_t>(keep);
    dirty_rows(1, 11);
}

void start_portrait(pic::Canvas& c)
{
    show_new_character(c);
    screen = Screen::Portrait;
    c.fill(27 * 8, 0, 13 * 8, 13 * 8, 0);
    layout::box(c, d->tables, d->frame_tiles, 27, 0, 39, 12);
    draw_portrait(c);
    text::build(menu, "", mk->portrait_menu);
    menu.selected = 0;
    show_menu_line(c);
}

void portrait_key(char k, pic::Canvas& c)
{
    uint8_t* r = mk->ch.rec;
    if (k == 'H') r[0x13F] = static_cast<uint8_t>(r[0x13F] >= 14 ? 1 : r[0x13F] + 1);
    else if (k == 'B') r[0x140] = static_cast<uint8_t>(r[0x140] >= 12 ? 1 : r[0x140] + 1);
    else if (k == 'K') {
        start_icon(c, Screen::CreateName);
        if (screen != Screen::Icon) create_after_icon(c);      // (no memory for the editor)
        return;
    } else {
        return;
    }
    draw_portrait(c);
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
    OvrWords src;
    if (!at || !src.open()) return;
    read_word(src, at, out, cap);
}

// The words of a menu string whose bit is set in mask (bit k: word k), as
// one menu string again
void keep_words(const char* in, unsigned mask, char* out, size_t cap)
{
    size_t o = 0;
    out[0] = 0;
    int k = 0;
    for (const char* q = in; *q;) {
        while (*q == ' ') ++q;
        const char* e = q;
        while (*e && *e != ' ') ++e;
        if (e == q) break;
        if ((mask >> k & 1) && o + (e - q) + 2 < cap) {
            if (o) out[o++] = ' ';
            memcpy(out + o, q, e - q);
            o += e - q;
            out[o] = 0;
        }
        ++k;
        q = e;
    }
}

void treasure_menu(pic::Canvas& c);

void shop_menu(pic::Canvas& c)
{
    if (treasure_on()) {
        treasure_menu(c);
        return;
    }
    char words[48];
    if (darkness_files()) {
        // Darkness: one string each (facts_shops.md 2, 3) - the shop's without
        // Take and Share when no coins lie on the counter; the highlight keeps
        // its place in the line
        char all[48];
        sw(temple ? profile::kDkTempleMenu : profile::kDkShopMenu, all, sizeof all);
        keep_words(all, temple || ground->any_money() ? ~0u : ~0x14u, words, sizeof words);
        text::build(menu, "", words);
        menu.selected = 0;
        hl_own(kDkShopMain);            // (GAME.OVR 0x631A's own place, from Buy / Heal)
        show_menu_line(c);
        return;
    }
    if (gateway()) {
        // Gateway (gateway_facts.md 12): its menus' words that apply - the
        // temple's Take with coins or items on the counter, Pool when the
        // party has coins, Share with coins there; the shop's Items when the
        // one shopping has items, Take and Share with coins there, Pool as
        // the temple's
        char all[48];
        bool party_coins = false;
        for (int i = 0; i < pt->count; ++i)
            if (any_money(pt->m[i])) party_coins = true;
        const bool coins = ground->any_money();
        unsigned mask = ~0u;
        if (temple) {
            sw(profile::kHealMenuMoney, all, sizeof all);
            if (!coins && ground->n == 0) mask &= ~(1u << 2);
            if (!party_coins) mask &= ~(1u << 3);
            if (!coins) mask &= ~(1u << 4);
        } else {
            snprintf(all, sizeof all, "%s", d->w_shop);
            const party::Character* ch = pt->sel();
            if (!ch || ch->n_items == 0) mask &= ~(1u << 1);
            if (!coins) mask &= ~(1u << 3 | 1u << 5);
            if (!party_coins) mask &= ~(1u << 4);
        }
        keep_words(all, mask, words, sizeof words);
        text::build(menu, "", words);
        menu.selected = 0;
        show_menu_line(c);
        return;
    }
    if (temple) sw(ground->any_money() ? profile::kHealMenuMoney : profile::kHealMenu, words, sizeof words);
    else snprintf(words, sizeof words, "%s", ground->any_money() ? d->w_shop_money : d->w_shop);
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

// A screen's own picture (Secret's treasure PIC 60, camp PIC 59), the
// script's picture kept to come back to
void draw_own_pic(int id)
{
    const int keep = last_pic_id, keep_head = last_pic_head;
    host_picture(id, 0xFF);
    last_pic_id = keep;
    last_pic_head = keep_head;
}
void draw_treasure_pic() { draw_own_pic(d->prof->variant.treasure_pic); }

void draw_shop(pic::Canvas& c)
{
    // The exploring screen with the shop's picture, as the script left it
    anim_stop();
    bigpic = -1;
    pic_shown = false;
    head_shown = body_shown = -1;
    draw_frame(c);
    if (treasure_on() && !vault_on() && d->prof->variant.treasure_pic) draw_treasure_pic();
    else if (last_pic_id >= 0 && (!treasure_on() || vault_on())) host_picture(last_pic_id, last_pic_head);
    else draw_view(c);
    draw_panel(c);
    if (gateway() && treasure_on() && !vault_on()) {
        c.fill(17 * 8, 15 * 8, 22 * 8, 8, 0);       // (Gateway's treasure: no place and time - DOSBox)
        dirty_rows(15, 15);
    }
    shop_menu(c);
}

void open_shop(pic::Canvas& c)
{
    screen = Screen::Shop;
    dk_own[kDkShopMain] = 0;
    if (!darkness_files()) {                // (Darkness: the script's words stay - DOSBox)
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
    }
    menu.selected = 0;
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
    if (treasure_on() && darkness_files()) {
        sw(profile::kDkStillTreasure, says, sizeof says);
        sw(profile::kDkClaimIt, back, sizeof back);
    } else if (treasure_on()) {
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
    if (treasure_on()) ask_yes_no(c, Ask::LeaveCoins, "");        // (the treasure's starts on No)
    else ask_yes_first(c, Ask::LeaveCoins, "");
}

void treasure_off();

void leave_shop(pic::Canvas& c, bool asked)
{
    if (vault_on()) {
        // Secret's vault: what's in it kept in its file
        if (!vault_write()) plat::log("[play] the vault: %s not written\n", d->prof->vault.file);
        ground->clear();
        treasure_off();
        asked = true;
    }
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
        // (Secret: its treasure picture gives way to the script's, or the 3D view)
        if (d->prof->variant.treasure_pic) {
            if (last_pic_id >= 0) host_picture(last_pic_id, last_pic_head);
            else host_picture(0xFF, 0);
        }
    }
    screen = Screen::Game;
    clear_menu_line(c);
    waiting = false;
    handle(vm->resume());
}

// A shop's price (Darkness: in platinum, x the count - facts_shops.md 2.1)
int shop_price(const uint8_t* it)
{
    int v = rules::price(it, vm->get(0x7F6D) & 0xFF);
    if (!darkness_files()) return v;
    const int n = items::Item{it}.count();
    if (n > 1) v = (v * n) & 0xFFFF;
    return v ? v : 1;
}

void draw_buy(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    plist.row0 = 1;
    plist.row1 = 22;
    plist.col0 = 1;
    plist.n = ground->n;
    if (darkness_files()) {
        // (rows 1-18; the buyer's and the counter's platinum at rows 20 / 21)
        plist.row1 = 18;
        char w[24], t[40];
        sw(profile::kDkPersonal, w, sizeof w);
        snprintf(t, sizeof t, "%s%10d", w, pt->sel() ? pt->sel()->money(4) : 0);
        put(c, t, 1, 20, 15);
        sw(profile::kDkPooled, w, sizeof w);
        snprintf(t, sizeof t, "%s%10d", w, ground->money[4]);
        put(c, t, 1, 21, 15);
    }
    draw_list(c, d->w_items, d->w_buy);
}

// Darkness's Buy (facts_shops.md 2.1): platinum only - the buyer's, then the
// counter's; "NAME buys [a ]ITEM" for the game's delay
void buy_dk(int i, pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    const uint8_t* it = goods(i);
    const int cost = shop_price(it);
    const int own = ch.money(4);
    if (cost > own + ground->money[4]) {
        note(c, d->w_no_money);
        return;
    }
    if (rules::too_heavy(ch, it, *names, d->facts)) {
        note(c, d->w_over);
        return;
    }
    rules::add_item(ch, it);
    if (cost <= own) {
        add_coins(ch, 4, -cost);
    } else {
        add_coins(ch, 4, -own);
        ground->money[4] -= cost - own;
    }
    rules::recalc(ch, *names, d->facts);
    char nm[20], it_nm[48], w[12], t[80];
    ch.name(nm, sizeof nm);
    for (size_t n = strlen(nm); n > 0 && nm[n - 1] == ' '; --n) nm[n - 1] = 0;
    names->name(items::Item{it}, it_nm, sizeof it_nm);
    sw(items::Item{it}.count() > 0 ? profile::kDkBuys : profile::kDkBuysA, w, sizeof w);
    snprintf(t, sizeof t, "%s%s%s", nm, w, it_nm);
    plat::log("[play] %s for %d platinum\n", t, cost);
    draw_buy(c);
    note(c, t);
}

void buy(int i, pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch || i < 0 || i >= ground->n) return;
    if (darkness_files()) {
        buy_dk(i, c);
        return;
    }
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
    plat::log("[play] bought %s for %d\n", nm, cost);
    draw_buy(c);
}

// ---- The temple's Heal ----------------------------------------------------------
// "NAME, how can we help you?" (row 1, colour 15) over the ten cures (from
// row 4, column 2), "Heal Exit". A cure that does nothing for them asks
// "cast cure anyway: "; then "<cure> will only cost N gold pieces.", "pay
// for cure " - the character pays, or the counter; "NAME is cured."

bool program_ds(uint16_t ds, uint8_t* out, size_t n);

struct HealWords {
    char name[11][24];                // (Darkness: 11 - Restoration too)
    char help[28];
    char heal[8];                   // "Heal" (from "Heal Exit")
};
HealWords* hw = nullptr;
int cure_at = -1;

void heal_line(int i, char* out, size_t cap) { snprintf(out, cap, "%s", hw && i < 11 ? hw->name[i] : ""); }

// Darkness's temple list (its overlay, GAME.OVR 0x386C; facts_shops.md 3):
// the outer frame and the row-16 bar, "NAME, how can we help you?" (1, 1),
// the 11 cures from row 3 at column 2, the character's name (1, 19) and
// hit points at column 18 (magenta when not full) "/max"
void draw_heal_dk(pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    layout::bar(c, d->tables, d->frame_tiles, 16);
    char t[48], nm[20];
    ch.name(nm, sizeof nm);
    for (size_t n = strlen(nm); n > 0 && nm[n - 1] == ' '; --n) nm[n - 1] = 0;
    snprintf(t, sizeof t, "%s%s", nm, hw->help);
    put(c, t, 1, 1, 15);
    put(c, nm, 1, 19, 15);
    snprintf(t, sizeof t, "%d", ch.hp());
    put(c, t, 18, 19, ch.hp() != ch.hp_max() ? 13 : 15);
    const int x = 18 + static_cast<int>(strlen(t));
    snprintf(t, sizeof t, "/%d", ch.hp_max());
    put(c, t, x, 19, 15);
    plist.row0 = 3;
    plist.row1 = 15;
    plist.col0 = 2;
    plist.n = 11;
    draw_list(c, "", hw->heal);
}

void draw_heal(pic::Canvas& c)
{
    if (darkness_files()) {
        draw_heal_dk(c);
        return;
    }
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    if (d->tables.bars[0] || gateway()) layout::bar(c, d->tables, d->frame_tiles, 16);     // (Secret: the shop's bar stays; Gateway too - DOSBox)
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
    if (darkness_files()) {
        // (Darkness: the cures are spells - their names by number, DS 0x08CC)
        uint8_t sp[11] = {};
        program_ds(d->prof->items.temple_spells, sp, sizeof sp);
        {
            OvrWords src;
            const profile::NameTable& nt = d->prof->magic.names;
            const bool open = src.open();
            for (int i = 0; i < 11; ++i) {
                hw->name[i][0] = 0;
                if (open) text::read_pascal(src, text::kInProgram | (nt.at + nt.stride * sp[i]), hw->name[i], sizeof hw->name[i]);
            }
        }
        sw(profile::kHelpYou, hw->help, sizeof hw->help);
        sw(profile::kDkHeal, hw->heal, sizeof hw->heal);
        screen = Screen::Heal;
        plist = PickList{};
        draw_heal(c);
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
    if (gateway()) {
        // (Gateway: the list unlit, the price from row 17 - DOSBox)
        list_unlit = true;
        draw_heal(c);
        list_unlit = false;
        say_item(c, t, 10, 17);
    } else {
        say_item(c, t, 10);
    }
    sw(profile::kPayFor, a, sizeof a);
    ask_yes_no(c, Ask::PayCure, a);
}

// Secret's temple (its overlay, GAME.OVR 0x5AFE; secret_facts_2.md 17): no
// prices, no "cure anyway" - a cure is done at once and says what it did
// ("NAME can see!", "NAME is not blind" ...; Heal's cures first "NAME is
// cured", then "NAME is fully healed")
bool free_temple() { return d->prof->shop_words[profile::kTResurrected] != 0; }

void secret_cure(int i, pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    const rules::CureFacts& f = d->prof->cures;
    const int h = ch.health();
    int said = -1, first = -1;
    auto diseases = [&]() {
        int n = 0;
        for (uint8_t t : f.disease)
            if (t) n += rules::remove_affects(ch, t);
        return n;
    };
    switch (i) {
    case rules::kCureBlindness:
        said = rules::remove_affects(ch, f.blinded) ? profile::kTCanSee : profile::kNotBlind;
        break;
    case rules::kCureDisease: said = diseases() ? profile::kCured : profile::kNotDiseased; break;
    case rules::kCureLight:
    case rules::kCureSerious:
    case rules::kCureCritical:
    case rules::kHealCure: {
        // the dead, stoned and gone aren't healed; full HP: no need (Heal's
        // cures still done)
        bool can = true;
        said = profile::kTIsHealed;
        if (h == party::Dead) can = false, said = profile::kTMustRaise;
        if (h == party::Stoned) can = false, said = profile::kTStonemason;
        if (h == party::Gone) can = false, said = profile::kTNotHere;
        const bool full = ch.hp() == ch.hp_max();
        if (full) said = profile::kTNoNeed;
        if (!can) break;
        int amount = 0;
        if (i == rules::kCureLight) amount = rng.roll(8, 1);
        else if (i == rules::kCureSerious) amount = rng.roll(8, 2) + 1;
        else if (i == rules::kCureCritical) amount = rng.roll(8, 3) + 3;
        else amount = (ch.hp_max() - ch.hp() - rng.roll(4, 1)) & 0xFF;      // (a byte: near full, all)
        if (i == rules::kHealCure) {
            int n = rules::remove_affects(ch, f.blinded) + rules::remove_affects(ch, f.feeblemind);
            n += diseases();
            if (n) {
                first = profile::kCured;
                if (!full) said = profile::kTFullyHealed;
            }
        }
        if (!full) rules::heal(ch, amount);
        break;
    }
    case rules::kNeutralizePoison:
        if (!ch.has_affect(f.poisoned)) {
            said = profile::kNotPoisoned;
            break;
        }
        if (ch.hp() == 0) ch.rec[0x1A4] = 1;
        rules::remove_affects(ch, f.poisoned);
        rules::remove_affects(ch, f.slow_poison);
        rules::remove_affects(ch, f.poison_damage);
        ch.rec[0x195] = party::Okay;
        ch.rec[0x196] = 1;
        said = profile::kTNoLongerPoisoned;
        break;
    case rules::kRaiseDead:
        if (h != party::Dead) {
            said = profile::kNotDead;
        } else if (ch.rec[0x74] == 2) {
            said = profile::kTCantRaise;                         // (elves)
        } else {
            // a point of Constitution (its own and the one in use: the
            // maximum hit points stay - DOSBox, Guy 95 at Con 18 -> 17
            // keeps 95), then 1 HP
            rules::remove_affects(ch, f.poisoned);
            if (ch.rec[0x18] > 0) --ch.rec[0x18];
            if (ch.rec[0x19] > 0) --ch.rec[0x19];
            rules::recalc(ch, *names, d->facts);
            ch.rec[0x1A4] = 1;
            ch.rec[0x195] = party::Okay;
            ch.rec[0x196] = 1;
            said = profile::kTResurrected;
        }
        break;
    case rules::kRemoveCurse:
        if (rules::needs_cure(ch, rules::kRemoveCurse, f)) {
            rules::apply_cure(ch, rules::kRemoveCurse, f, rng);
            said = profile::kTCurseLifted;
        } else {
            said = profile::kNotCursed;
        }
        break;
    case rules::kStoneToFlesh:
        if (h == party::Stoned) {
            rules::apply_cure(ch, rules::kStoneToFlesh, f, rng);
            said = profile::kTTurnedFlesh;
        } else {
            said = profile::kNotStoned;
        }
        break;
    default: break;
    }
    rules::recalc(ch, *names, d->facts);
    draw_heal(c);
    char nm[20], w[48], t[72] = "";
    ch.name(nm, sizeof nm);
    for (size_t n = strlen(nm); n > 0 && nm[n - 1] == ' '; --n) nm[n - 1] = 0;
    c.fill(8, 21 * 8, 38 * 8, 16, 0);
    int row = 21;
    const int lines[2] = {first, said};
    for (int k : lines) {
        if (k < 0) continue;
        sw(k, w, sizeof w);
        snprintf(t, sizeof t, "%s %s", nm, w);
        if (row == 21 && first < 0) {
            say_item(c, t, 10);                 // (two rows when it's long)
            break;
        }
        put(c, t, 1, row++, 10);
    }
    dirty_rows(21, 22);
    plat::log("[play] temple: cure %d - %s\n", i, t);
}

// Darkness's temple says NAME (row 18; cyan, red when not okay, magenta
// when able to train) and a word under it (row 19, green) - its overlay
// 0xec ov+0x1B08; with ask, then "cast cure anyway? " (No first)
void dk_temple_say(pic::Canvas& c, party::Character& ch, int word, bool ask_anyway)
{
    char nm[20], w[48];
    ch.name(nm, sizeof nm);
    sw(word, w, sizeof w);
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    put(c, nm, 1, 18, ch.health() != party::Okay ? 12 : ch.ext[party::kExtTrain] ? 13 : 11);
    text::Writer ws;
    text::begin(ws, c, w, text::Region{1, 19, 38, 22}, 10, false);
    text::step(ws, c, d->font, -1);
    dirty_rows(17, 22);
    plat::log("[play] temple: %s %s\n", nm, w);
    {
        // (the list's highlight goes while it speaks - DOSBox)
        const int row = plist.row0 + plist.index - plist.top;
        char line[32];
        heal_line(plist.index, line, sizeof line);
        c.fill(plist.col0 * 8, row * 8, (39 - plist.col0) * 8, 8, 0);
        put(c, line, plist.col0, row, 10);
    }
    if (ask_anyway) {
        char q[24];
        sw(profile::kCastAnyway, q, sizeof q);
        ask_yes_no(c, Ask::CureAnyway, q);
    }
}

// Restoration (Darkness's spell 102, GAME.OVR 0x2EBEC): one level back in a
// drained class (the first; the original takes the one whose next level
// costs least), hit points back by the lost ones shared over the levels
// lost; the last level back brings the best experience back
bool dk_restore(party::Character& ch)
{
    int lost = 0, cls = -1;
    for (int k = 0; k < 7; ++k) {
        const int l = ch.rec[0x109 + k], best = ch.ext[party::kExtBestLevels + k];
        if (l < best) {
            lost += best - l;
            if (cls < 0) cls = k;
        }
    }
    if (cls < 0) return false;
    const int best_hp = ch.ext[party::kExtBestHp];
    if (ch.hp_max() < best_hp) {
        const int gain = (best_hp - ch.hp_max()) / lost;
        ch.rec[0x78] = static_cast<uint8_t>(ch.hp_max() + gain);
        ch.rec[0x1A4] = static_cast<uint8_t>(ch.hp() + gain);
        ch.rec[0x12C] = static_cast<uint8_t>(ch.rec[0x12C] + gain);
    }
    ++ch.rec[0x109 + cls];
    if (lost == 1) memcpy(ch.rec + 0x127, ch.ext + party::kExtBestXp, 4);
    rules::recalc(ch, *names, d->facts);
    return true;
}

// Darkness's cures (its temple overlay; facts_shops.md 3): free, done at once;
// one that has nothing to do says why and asks "cast cure anyway? "
void dk_cure(int i, pic::Canvas& c)
{
    party::Character& ch = *pt->sel();
    const rules::CureFacts& f = d->prof->cures;
    const int h = ch.health();
    auto okay = [&]() {
        ch.rec[0x195] = party::Okay;
        ch.rec[0x196] = 1;
    };
    auto uncure_poison = [&]() {
        rules::remove_affects(ch, f.poisoned);
        rules::remove_affects(ch, f.slow_poison);
        rules::remove_affects(ch, f.poison_damage);
    };
    int say = -1;
    bool ask = false;
    switch (i) {
    case 0:                                         // Cure Blindness (nothing said)
        if (!rules::remove_affects(ch, f.blinded)) say = profile::kNotBlind, ask = true;
        break;
    case 1: {                                       // Cure Disease
        int n = 0;
        for (uint8_t t : f.disease)
            if (t) n += rules::remove_affects(ch, t);
        say = n ? profile::kCured : profile::kNotDiseased;
        break;
    }
    case 2: case 3: case 4: case 5: {               // the wounds, Heal (nothing said)
        int amount;
        if (i == 2) amount = rng.roll(8, 1);
        else if (i == 3) amount = rng.roll(8, 2) + 1;
        else if (i == 4) amount = rng.roll(8, 3) + 3;
        else {
            // (Heal: to all but 1d4 - nothing when that is past the most;
            // blindness and feeblemind go)
            const int r = rng.roll(4, 1);
            amount = ch.hp() + r > ch.hp_max() ? 0 : ch.hp_max() - ch.hp() - r;
            rules::remove_affects(ch, f.blinded);
            rules::remove_affects(ch, f.feeblemind);
        }
        rules::heal(ch, amount);
        break;
    }
    case 6:                                         // Neutralize Poison
        if (!ch.has_affect(f.poisoned)) {
            say = profile::kNotPoisoned, ask = true;
            break;
        }
        if (ch.hp() == 0) ch.rec[0x1A4] = 1;
        uncure_poison();
        okay();
        break;
    case 7: {                                       // Raise Dead (the spell's routine: "is raised")
        const bool dead = h == party::Dead || h == party::Animated, elf = ch.rec[0x74] == 2;
        if (!dead || elf) {
            say = elf ? profile::kTCantRaise : h == party::Gone ? profile::kDkBeyond : profile::kNotDead;
            ask = true;
            break;
        }
        if (ch.rec[0x18] == 0) break;
        rules::remove_affects(ch, f.poisoned);
        okay();
        --ch.rec[0x18];
        rules::recalc(ch, *names, d->facts);
        ch.rec[0x1A4] = 1;
        say = profile::kDkRaised;
        break;
    }
    case 8:                                         // Remove Curse
        if (!rules::needs_cure(ch, rules::kRemoveCurse, f)) {
            say = profile::kNotCursed, ask = true;
            break;
        }
        rules::apply_cure(ch, rules::kRemoveCurse, f, rng);
        break;
    case 9:                                         // Stone to Flesh
        if (h != party::Stoned) {
            say = profile::kNotStoned, ask = true;
            break;
        }
        okay();
        ch.rec[0x1A4] = 1;
        break;
    case 10:                                        // Restoration
        if (!dk_restore(ch)) say = profile::kDkNoLostLevels, ask = true;
        break;
    default: return;
    }
    rules::recalc(ch, *names, d->facts);
    draw_heal(c);
    if (say >= 0) dk_temple_say(c, ch, say, ask);
}

// Darkness's Repair (facts_shops.md 3): the whole party at once, free - the
// cures all done, the dead resurrected (not elves), the stoned back, every
// lost level restored, the hit points full
void dk_repair()
{
    const rules::CureFacts& f = d->prof->cures;
    // (the stats' recalculation, 0xE2:0x89 for each: Constitution's sets the
    // HP max - DOSBox: Abagail 92 -> 94, Paine 96 -> 98)
    MagicRules* mr = nullptr;
    if (!load_rules(mr)) mr = nullptr;
    for (int m = 0; m < pt->count; ++m) {
        party::Character& ch = pt->m[m];
        rules::remove_affects(ch, f.feeblemind);
        rules::remove_affects(ch, f.blinded);
        for (uint8_t t : f.disease)
            if (t) rules::remove_affects(ch, t);
        if (ch.has_affect(f.poisoned)) {
            if (ch.hp() == 0) ch.rec[0x1A4] = 1;
            rules::remove_affects(ch, f.poisoned);
            rules::remove_affects(ch, f.slow_poison);
            rules::remove_affects(ch, f.poison_damage);
            ch.rec[0x195] = party::Okay;
            ch.rec[0x196] = 1;
        }
        if (ch.health() == party::Dead && ch.rec[0x74] != 2 && ch.rec[0x18] > 0) {
            rules::remove_affects(ch, f.poisoned);         // (Resurrection, spell 105)
            ch.rec[0x195] = party::Okay;
            ch.rec[0x196] = 1;
            ch.rec[0x1A4] = ch.rec[0x78];
        }
        if (ch.health() == party::Stoned) {
            ch.rec[0x195] = party::Okay;
            ch.rec[0x196] = 1;
        }
        while (dk_restore(ch)) {}
        rules::recalc(ch, *names, d->facts);
        if (mr) create::dk_con_recompute(ch, mr->tables);
        if (ch.health() != party::Gone) ch.rec[0x1A4] = ch.rec[0x78];
        char who[20];
        ch.name(who, sizeof who);
        plat::log("[play] temple: Repair - %s HP %d of %d\n", who, ch.hp(), ch.hp_max());
        if (ch.health() == party::Unconscious) {
            ch.rec[0x195] = party::Okay;
            ch.rec[0x196] = 1;
        }
    }
    delete mr;
    plat::log("[play] temple: Repair\n");
}

void heal_pick(int i, pic::Canvas& c)
{
    if (darkness_files()) {
        dk_cure(i, c);
        return;
    }
    if (free_temple()) {
        secret_cure(i, c);
        return;
    }
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
// View Character's coins (view_menu): 0 the counter's (Take), 1 Trade (to
// take_to), 2 Drop - the coins of view_by
int  take_mode = 0, take_to = -1, view_by = -1;

int coins_there(int k) { return take_mode ? pt->m[view_by].money(k) : ground->money[k]; }

// (Gateway's View too - DOSBox: the coin lit on its money panel)
bool secret_coin_panel() { return take_mode && (secret_files() || gateway()); }
// Darkness's vault Money: the selected's coins listed in the text area
// (row 17, the panel's 18-character lines), the screen kept (DOSBox)
bool dk_vault_coins() { return take_mode == 2 && darkness_files() && vault_on(); }
int  dk_vault_sel = 0;

void list_coins()
{
    take_n = 0;
    if (secret_coin_panel() || dk_vault_coins()) {
        // (Secret's View: its money panel's order, copper first)
        for (int k = 0; k < 7; ++k)
            if (coins_there(k) > 0) take_kind[take_n++] = k;
        return;
    }
    if (darkness_files() && !take_mode && !treasure_on()) {
        // (Darkness's Take in a shop: platinum first - facts_shops.md 2.3; the
        // treasure's lists jewelry first)
        for (int k = 4; k < 7; ++k)
            if (coins_there(k) > 0) take_kind[take_n++] = k;
        return;
    }
    for (int k = 6; k >= 0; --k)
        if (coins_there(k) > 0) take_kind[take_n++] = k;
}

void take_line(int i, char* out, size_t cap)
{
    if (i < 0 || i >= take_n) {
        out[0] = 0;
        return;
    }
    const int k = take_kind[i];
    if (secret_coin_panel() || dk_vault_coins()) {
        // (the panel's line: the name at column 20, the number ending at 37)
        char n[12];
        snprintf(n, sizeof n, "%d", coins_there(k));
        const int w = 18 - static_cast<int>(strlen(n));
        snprintf(out, cap, "%-*.*s%s", w, w, d->money[k], n);
        return;
    }
    snprintf(out, cap, "%s %d", d->money[k], coins_there(k));
}

void draw_take(pic::Canvas& c)
{
    if (dk_vault_coins()) {
        draw_shop(c);
        c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
        plist.row0 = 17;
        plist.row1 = 22;
        plist.col0 = 1;
    } else if (secret_coin_panel()) {
        // Secret's View (Trade / Drop / Deposit): the coin is picked on the
        // character's money panel (rows 9-15, columns 20-37), the screen kept
        if (gateway()) draw_character_gateway(c, pt->m[view_by]);
        else draw_character_secret(c, pt->m[view_by]);
        plist.row0 = 9;
        plist.row1 = 15;
        plist.col0 = 20;
    } else {
        c.clear(0);
        layout::outer(c, d->tables, d->frame_tiles);
        // (Secret's - its frame of bars - from row 1, column 1: secret_facts_2.md 16)
        const bool top = d->tables.bars[0] != 0;
        plist.row0 = top ? 1 : 2;
        plist.row1 = top ? 8 : 9;
        plist.col0 = top ? 1 : 2;
    }
    plist.n = take_n;
    char prompt[28], sel[12];
    if (take_mode) {
        // (View's Trade / Drop: its words)
        snprintf(prompt, sizeof prompt, "%s", vword(profile::kVwCoinType));
        snprintf(sel, sizeof sel, "%s", iw(profile::kSelect));
        // (Gateway: its own " Select" - a column after the prompt, DOSBox)
        if (const uint32_t cs = profile::view_facts(d->prof->game).coin_select) ow(cs, sel, sizeof sel);
    } else {
        sw(profile::kCoinType, prompt, sizeof prompt);
        sw(profile::kSelectWord, sel, sizeof sel);
    }
    if (prompt[0] && prompt[strlen(prompt) - 1] != ' ' && strlen(prompt) + 1 < sizeof prompt) strcat(prompt, " ");
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
    if (take_mode) {
        snprintf(a, sizeof a, "%s", vword(profile::kVwHowMuch));
        snprintf(b, sizeof b, "%s", vword(take_mode == 1 ? profile::kVwWillTrade : vault_on() ? profile::kVwWillDeposit : profile::kVwWillDrop));
    } else {
        sw(darkness_files() && take_coin == 5 ? profile::kDkHowMany : profile::kHowMuch, a, sizeof a);
        sw(profile::kWillTake, b, sizeof b);
    }
    if (darkness_files() && !take_mode && take_coin >= 4) {
        // (Darkness: the coin's own word, "Platinum " - its space and the
        // question's make two)
        char cw[16];
        sw(profile::kDkCoinWord + take_coin - 4, cw, sizeof cw);
        snprintf(take_prompt, sizeof take_prompt, "%s%s%s", a, cw, b);
    } else {
        snprintf(take_prompt, sizeof take_prompt, "%s%s%s%s", a, d->money[take_coin], b[0] == ' ' ? "" : " ", b);
    }
    if (dk_vault_coins() || (gateway() && secret_coin_panel())) {
        // (the line's highlight goes while it asks - DOSBox)
        char line[32];
        take_line(i, line, sizeof line);
        const int row = plist.row0 + i - plist.top;
        c.fill(plist.col0 * 8, row * 8, (39 - plist.col0) * 8, 8, 0);
        put(c, line, plist.col0, row, 10);
    }
    input_engine = true;
    engine_ask = EngineAsk::Coins;
    input_prompt = take_prompt;
    input_max = 5;
    input_len = 0;
    input_buf[0] = 0;
    input_mode = Input::Number;
    draw_input(c);
}

void view_coins_done(pic::Canvas& c);
void view_coins(int n, pic::Canvas& c);

void take_coins(int n, pic::Canvas& c)
{
    if (take_mode) {
        view_coins(n, c);
        return;
    }
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

// View's Trade / Heal / Cure: who it's for ("Trade to?", "Heal whom? ",
// "Cure whom? " over the party list, Select Exit)
char view_act = 0;
int  cure_for = -1;                 // "is not diseased" / "cure anyway: " - whom

void back_to_view(pic::Canvas& c)
{
    if (dk_vault_coins()) {
        // (Darkness's vault Money: back to the vault)
        if (view_by >= 0) pt->selected = view_by;
        take_mode = 0;
        screen = Screen::Shop;
        menu.selected = dk_vault_sel;               // (the highlight where it was)
        draw_shop(c);
        return;
    }
    if (view_by >= 0) pt->selected = view_by;
    take_mode = 0;
    screen = Screen::View;
    draw_character(c);
}

void draw_view_who(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    draw_party(c, 1);
    char keys[24], prompt[28];
    snprintf(keys, sizeof keys, "%s%s", iw(profile::kSelect), d->w_exit);
    snprintf(prompt, sizeof prompt, "%s",
             vword(view_act == 'T' ? profile::kVwTradeTo : view_act == 'H' ? profile::kVwHealWhom : profile::kVwCureWhom));
    if (prompt[0] && prompt[strlen(prompt) - 1] != ' ' && strlen(prompt) + 1 < sizeof prompt) strcat(prompt, " ");
    text::build(menu, prompt, keys);
    menu.selected = 0;
    show_menu_line(c);
    dirty(0, pic::kScreenH);
}

void open_coins(pic::Canvas& c);

// The paladin's Cure on one of the party: the diseases go, one of the
// week's cures (its effect starts the week when none runs), "NAME is cured"
void paladin_cure(int to, pic::Canvas& c)
{
    party::Character& me = pt->m[view_by];
    party::Character& tg = pt->m[to];
    for (uint8_t dz : d->prof->cures.disease)
        if (dz) rules::remove_affects(tg, dz);
    if (me.rec[0x191] > 0) --me.rec[0x191];
    const uint8_t fx = profile::view_facts(d->prof->game).paladin_cure;
    if (!me.has_affect(fx)) spells::add_affect(me, fx, 0x2760, 0, true);
    if (names) rules::recalc(tg, *names, d->facts);
    char nm[20], t[48];
    tg.name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s", nm, vword(profile::kVwIsCured));
    back_to_view(c);
    note(c, t);
}

bool view_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what != Ask::PaladinCure) return false;
    if (k == 'Y' && cure_for >= 0) paladin_cure(cure_for, c);
    else back_to_view(c);
    cure_for = -1;
    return true;
}

void view_who_tap(int x, int y, pic::Canvas& c)
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
        back_to_view(c);
        return;
    }
    if (k != 'S') return;
    const int to = pt->selected;
    pt->selected = view_by;
    party::Character& me = pt->m[view_by];
    party::Character& tg = pt->m[to];
    char nm[20], t[48];
    tg.name(nm, sizeof nm);
    if (view_act == 'T') {
        if (to == view_by) {
            back_to_view(c);
            return;
        }
        take_mode = 1;
        take_to = to;
        open_coins(c);
        return;
    }
    if (view_act == 'H') {
        // Twice the paladin level, once a day (coab's PaladinHeal)
        const bool better = rules::heal(tg, me.level(3) * 2);
        spells::add_affect(me, profile::view_facts(d->prof->game).paladin_heal, 1440, 0, false);
        snprintf(t, sizeof t, "%s%s", nm, vword(better ? profile::kVwFeelsBetter : profile::kVwUnaffected));
        back_to_view(c);
        note(c, t);
        return;
    }
    // Cure: one with no disease - "is not diseased", "cure anyway: " Yes No
    bool ill = false;
    for (uint8_t dz : d->prof->cures.disease)
        if (dz && tg.has_affect(dz)) ill = true;
    if (!ill) {
        cure_for = to;
        char line[48];
        snprintf(line, sizeof line, "%s %s", nm, vword(profile::kVwNotDiseased));
        put(c, line, 1, 19, 10);           // (on the party list: coab's DisplayPlayerStatusString)
        dirty_rows(19, 19);
        ask_yes_no(c, Ask::PaladinCure, vword(profile::kVwCureAnyway));
        return;
    }
    paladin_cure(to, c);
}

// Trade / Drop: the coins of view_by (take_mode 1 / 2)
void open_coins(pic::Canvas& c)
{
    list_coins();
    if (!take_n) {
        back_to_view(c);
        return;
    }
    screen = Screen::Take;
    plist = PickList{};
    draw_take(c);
}

void view_coins_done(pic::Canvas& c) { back_to_view(c); }

void view_coins(int n, pic::Canvas& c)
{
    party::Character& me = pt->m[view_by];
    if (take_coin >= 0 && n > 0) {
        if (n > me.money(take_coin)) n = me.money(take_coin);       // (no more than they have)
        if (take_mode == 1) {
            // Trade: too heavy for the other - "Overloaded" (coab trade_money)
            party::Character& to = pt->m[take_to];
            if (names) rules::recalc(to, *names, d->facts);
            if (to.encumbrance() + n > rules::max_load(to)) {
                draw_take(c);
                note(c, d->w_over);
                return;
            }
            add_coins(to, take_coin, n);
            if (names) rules::recalc(to, *names, d->facts);
        } else if (ground && (view_from == Screen::Shop || view_from == Screen::Loot)) {
            ground->money[take_coin] += n;          // (dropped at a treasure / in a shop: onto the counter)
        }
        add_coins(me, take_coin, -n);
        if (names) rules::recalc(me, *names, d->facts);
    }
    // More coins: the list again (coab: until no money is left)
    list_coins();
    if (!take_n) {
        back_to_view(c);
        return;
    }
    draw_take(c);
}

void view_spells(pic::Canvas& c);

// The View menu's own words: true when one was acted on
bool view_menu(char k, pic::Canvas& c)
{
    if (in_fight() || new_char || !pt->sel()) return false;
    party::Character& ch = *pt->sel();
    view_by = pt->selected;
    switch (k) {
    case 'S':
        if (!any_memorized(ch)) return false;
        view_spells(c);
        return true;
    case 'T':
        if (!any_money(ch)) return false;
        view_act = 'T';
        break;
    case 'H':
        if (!can_heal(ch)) return false;
        view_act = 'H';
        break;
    case 'C':
        if (!can_cure(ch)) return false;
        view_act = 'C';
        break;
    case 'D':
        if (!any_money(ch)) return false;
        take_mode = 2;
        take_to = -1;
        open_coins(c);
        return true;
    default: return false;
    }
    screen = Screen::ViewWho;
    draw_view_who(c);
    return true;
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
    if (darkness_files()) {
        // (Darkness clears inside the exploring frame - its junctions stay -
        // and the name is cyan: DOSBox)
        layout::explore(c, d->tables, d->frame_tiles);
        c.fill(8, 8, 38 * 8, 22 * 8, 0);
    } else {
        layout::outer(c, d->tables, d->frame_tiles);
    }
    char nm[20], t[48], w1[24];
    ch.name(nm, sizeof nm);
    put(c, nm, 1, 1, darkness_files() ? 11 : 15);
    sw(profile::kCollection, t, sizeof t);
    put(c, t, 1, 7, 15);
    const int gems = ch.money(5), jewels = ch.money(6);
    if (darkness_files()) {
        // Darkness (facts_shops.md 2.4): "gems : N", "jewelry : N" always (as
        // they were before the one just appraised); its menus one string
        // each, no "You can : "
        char w[24], all[32], keys[40];
        sw(profile::kDkGemsCount, w, sizeof w);
        snprintf(t, sizeof t, "%s%d", w, gems + (appraised && !appraised_jewel));
        put(c, t, 1, 9, 15);
        sw(profile::kDkJewelryCount, w, sizeof w);
        snprintf(t, sizeof t, "%s%d", w, jewels + (appraised && appraised_jewel));
        put(c, t, 1, 10, 15);
        char prompt[16] = "";
        if (appraised) {
            char line[48];
            sw(appraised_jewel ? profile::kJewelValued : profile::kGemValued, t, sizeof t);
            sw(profile::kDkPlatinumDot, w, sizeof w);
            snprintf(line, sizeof line, "%s%d%s", t, appraised, w);
            put(c, line, 1, 12, 15);
            const bool must_sell = ch.encumbrance() + 1 > rules::max_load(ch) || ch.n_items >= party::kMaxItems;
            sw(profile::kDkSellKeep, all, sizeof all);
            keep_words(all, must_sell ? 1u : 3u, keys, sizeof keys);
        } else {
            sw(profile::kAppraisePrompt, prompt, sizeof prompt);
            sw(profile::kDkAppraiseMenu, all, sizeof all);
            keep_words(all, (gems ? 1u : 0u) | (jewels ? 2u : 0u) | 4u, keys, sizeof keys);
        }
        text::build(menu, prompt, keys);
        menu.selected = 0;
        hl_inherit();                   // (GAME.OVR 0x29486: no place of its own)
        show_menu_line(c);
        dirty(0, pic::kScreenH);
        return;
    }
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
            const int plat = darkness_files() ? appraised : appraised / 5;     // (Darkness: the value in platinum)
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
        const int div = d->prof->variant.appraise_div;
        appraised = appraised_jewel ? rules::jewel_value(r, rng, div) : rules::gem_value(r, div);
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
    if (darkness_files() && hw) {
        // (Darkness: casting anyway does nothing for them)
        draw_heal(c);
        return true;
    }
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
        const int hit = text::hit(menu, col);
        if (darkness_files() && hit >= 0) menu.selected = hit;      // (the highlight stays there)
        switch (text::key(menu, hit)) {
        case 'B':
            screen = Screen::ShopBuy;
            plist = PickList{};
            draw_buy(c);
            break;
        case 'V':
            if (vault_on() && d->prof->vault.menu6) {
                view_from = Screen::Shop;               // (Gateway's vault: Vault - the selected's items to put in it)
                open_items(c);
                break;
            }
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
        case 'W':                       // Gateway's vault: Withdraw (as the treasure's Take)
            if (vault_on()) treasure_take(c);
            break;
        case 'M':                       // the treasure's "Take: Money Items Exit"
            if (vault_on() && darkness_files() && strncmp(menu.prompt, "", 1) == 0) {
                // (Darkness's vault: Money - the selected's coins put in it)
                view_from = Screen::Shop;
                view_by = pt->selected;
                dk_vault_sel = menu.selected;
                take_mode = 2;
                take_to = -1;
                open_coins(c);
            } else if (treasure_on()) {
                open_take(c);
            }
            break;
        case 'I':
            if (vault_on() && darkness_files() && strncmp(menu.prompt, "", 1) == 0) {
                view_from = Screen::Shop;               // (Darkness's vault: Items - to put in it)
                open_items(c);
            } else if (treasure_on()) {
                open_loot(c);
            } else if (gateway() && !temple) {
                view_from = Screen::Shop;               // (Gateway's shop: Items - to sell them)
                items_from_shop = true;
                open_items(c);
            }
            break;
        case 'D':                       // the treasure's Detect (Detect Magic memorized)
            if (vault_on() && d->prof->vault.menu6) view_character(c);     // (Gateway's vault: Deposit - View)
            else if (treasure_on()) treasure_detect(c);
            break;
        case 'A':                       // Appraise: gems and jewellery
            open_appraise(c);
            break;
        case 'H':                       // the temple's Heal
            if (temple) open_heal(c);
            break;
        case 'R':                       // Darkness's temple: Repair (the whole party)
            if (temple && darkness_files()) {
                dk_repair();
                if (pt->count) draw_party(c, 17);
                shop_menu(c);
            }
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
        } else if (take_mode) {
            view_coins_done(c);
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
        } else if (items_from_shop && screen == Screen::Items) {
            items_from_shop = false;
            screen = Screen::Shop;
            draw_shop(c);
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
        case 'D':
            if (vault_on()) deposit_item(l.index, c);
            else drop_item(l.index, c);
            break;
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


MagicRules* mrules = nullptr;       // the rule tables while camping / casting
// The spell names, packed: name s at text + at[s] (v0.136.1: 128 x 24
// fixed was 3 KB at a fight's highest point; the names are ~12 letters)
struct SpellNames {
    static constexpr int kMax = 128;    // (Curse 1-100, Secret 1-118, Darkness 1-126)
    uint16_t at[kMax];                  // (0: the empty name at text[0])
    char text[1];                       // (malloc'd past its end: every name and its 0)
};
SpellNames* spn = nullptr;

void mw(int i, char* out, size_t cap)
{
    out[0] = 0;
    const uint32_t at = d->prof->magic.words[i];
    OvrWords src;
    if (!at || !src.open()) return;
    text::read_pascal(src, at, out, cap);
}

void end_effects();

void end_magic()
{
    end_effects();
    delete mrules;
    mrules = nullptr;
    free(spn);
    spn = nullptr;
}

bool load_magic();
const char* magic_error();

// Items that change class values as they're readied / put away
// (curse_finish_facts.md 5): the Ring of Wizardry's spell slots, the
// thief-skill items
void item_class_values(party::Character& ch, int i)
{
    if (i < 0 || i >= ch.n_items) return;
    const items::Passive kind = items::passive(ch.items[i]);
    if (kind != items::Passive::Wizardry && kind != items::Passive::Dexterity && kind != items::Passive::Thievery) return;
    const bool had = mrules != nullptr;
    if (!load_magic()) return;
    if (kind == items::Passive::Wizardry) classes::wizardry(ch, mrules->tables, ch.items[i][0x34] != 0);
    else if (ch.level(classes::Thief) > 0) classes::thief_skills(ch, mrules->tables);
    if (!had) end_magic();              // (loaded for this: the tables don't stay)
}

// Why load_magic failed: a game whose rule tables aren't known yet
// (Secret, being built) or memory
const char* magic_error()
{
    return d->prof->create.ds_image ? "Not enough memory." : "Spells and rules aren't in the engine for this game yet.";
}

// The rule tables and spell names (from the program), for the camp
bool load_magic()
{
    if (!mrules && !spn) make_room(12, "the magic tables");
    if (!mrules && !load_rules(mrules)) {
        end_magic();
        return false;
    }
    if (spn) return true;
    const profile::NameTable& nt = d->prof->magic.names;
    if (!nt.at) {
        spn = static_cast<SpellNames*>(calloc(1, sizeof(SpellNames)));
        return spn != nullptr;
    }
    // (v0.86.1: a failed read was kept - the spell lists stayed blank, only
    // their level headings, until the Play Test closed - Tom, Curse's Fire
    // Knives fight on the 3.2". Now it's dropped, logged and tried again)
    // Two passes over the table, a part at a time (the whole table was
    // 4.4 KB more at a fight's highest point when a spell was first cast):
    // the names' lengths, then the names into a block just their size
    bool named = false, opened = false;
    plat::File f;
    if (open_file(d->prof->program, f)) {
        opened = true;
        plat::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok) {
            const int per = nt.stride && nt.stride < 1024 ? 1024 / nt.stride : 1;
            const int count = nt.count < SpellNames::kMax ? nt.count : SpellNames::kMax;
            uint8_t* buf = static_cast<uint8_t*>(malloc(static_cast<size_t>(per) * nt.stride));
            uint8_t len[SpellNames::kMax] = {};
            named = buf != nullptr;
            for (int pass = 0; named && pass < 2; ++pass) {
                if (pass == 1) {
                    size_t total = 1;
                    for (int s = 0; s < count; ++s) total += len[s] ? len[s] + 1u : 0u;
                    spn = static_cast<SpellNames*>(calloc(1, offsetof(SpellNames, text) + total));
                    if (!spn) {
                        named = false;
                        break;
                    }
                }
                uint16_t next = 1;
                for (int at = 0; named && at < count; at += per) {
                    const int k = count - at < per ? count - at : per;
                    if (exepack::read(src, info, nt.at + static_cast<uint32_t>(at) * nt.stride, buf,
                                      static_cast<size_t>(k) * nt.stride) != exepack::Status::Ok) {
                        named = false;
                        break;
                    }
                    for (int j = 0; j < k; ++j) {
                        const uint8_t* e = buf + static_cast<size_t>(j) * nt.stride;
                        const int n = e[0] >= nt.stride ? 0 : e[0] < 23 ? e[0] : 23;    // (Secret's entry 0 isn't a name)
                        if (pass == 0) {
                            len[at + j] = static_cast<uint8_t>(n);
                        } else if (n) {
                            spn->at[at + j] = next;
                            memcpy(spn->text + next, e + 1, static_cast<size_t>(n));
                            spn->text[next + n] = 0;
                            next = static_cast<uint16_t>(next + n + 1);
                        }
                    }
                }
            }
            free(buf);
        }
        f.close();
    }
    if (!named) {
        free(spn);
        spn = nullptr;
        log_error(opened ? no_memory("the spell names")
                         : "The spell names couldn't be read: the game's program file didn't open.");
        return false;
    }
    return true;
}

const char* spell_name(int s) { return spn && s >= 0 && s < SpellNames::kMax ? spn->text + spn->at[s] : ""; }

void draw_magic_menu(pic::Canvas& c)
{
    // (Darkness: no Rest where resting can't be done - as its camp menu)
    char words[sizeof d->w_magic];
    snprintf(words, sizeof words, "%s", d->w_magic);
    if (no_rest_here()) {
        char* r = strstr(words, " Rest");
        if (r) memmove(r, r + 5, strlen(r + 5) + 1);
    }
    text::build(menu, "", words);
    menu.selected = 0;
    hl_own(kDkMagic);                   // (Darkness: GAME.OVR 0xED74's own place, from Cast)
    show_menu_line(c);
}

void open_magic(pic::Canvas& c)
{
    if (!load_magic()) {
        error(c, magic_error());
        return;
    }
    screen = Screen::Magic;
    dk_own[kDkMagic] = 0;
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
    uint8_t id[144];                // 0: a level heading (level in lvl; Secret's grimoire: up to 117 + 7,
    uint8_t lvl[144];               // Darkness's 126 + 9)
    uint8_t count[144];             // Darkness's memory lists: a spell held more than once on one line, "(n)"
    int     n = 0;
    int     top = 0, sel = -1;
    bool    learning = false;       // "to Memorize" (else the grimoire)
    bool    casting = false;        // "in Memory" (Cast)
    bool    scrolls = false;        // scrolls' spells: "on Scrolls" (Scribe) / "to Scribe" (with learning)
    bool    choosing = false;       // training's new spell: "to Choose", Learn (no Exit)
    bool    fighting = false;       // "in Memory" in a fight: the chosen one is cast there
    int     reading = -1;           // a scroll's spells (the item): "on Scroll", Cast reads one (Use)
    bool    viewing = false;        // View Character's Spells: "in Memory", only looked at
} sl;
int gw_preview = -1;                // Gateway: the spells to memorize shown first (the question's word)

// The scrolls' facts for the magic rules
magic::Scrolls scroll_facts()
{
    magic::Scrolls sc;
    sc.names = names;
    sc.one_spell = d->prof->magic.scroll_one_spell;
    sc.read_magic = d->prof->magic.read_magic;
    sc.types[0] = d->prof->magic.scroll_types[0];
    sc.types[1] = d->prof->magic.scroll_types[1];
    sc.count_at = d->prof->magic.scroll_count_at;
    sc.readers = d->prof->magic.scroll_readers;
    return sc;
}

int list_rows();
void dk_page_sel();

void build_lines(const uint8_t* ids, int n)
{
    sl.n = 0;
    int last = 0;
    const classes::Tables& t = mrules->tables;
    // (Darkness's Cast and View list a spell held more than once on one
    // line with its count - "Cure Light Wounds (6)", DOSBox)
    const bool counted = darkness_files() && (sl.casting || sl.viewing);
    for (int i = 0; i < n && sl.n < 143; ++i) {
        int lv = t.spell_level(ids[i]);
        // (Gateway: a heading for each level and kind - lvl's high bits the kind)
        if (d->prof->magic.heading_word) {
            const int k = t.spell_class(ids[i]);
            lv |= (k >= 0 && k < 3 ? k : 2) << 4;
        }
        if (lv != last) {
            sl.id[sl.n] = 0;
            sl.count[sl.n] = 0;
            sl.lvl[sl.n++] = static_cast<uint8_t>(lv);
            last = lv;
        }
        if (counted && sl.n > 0 && sl.id[sl.n - 1] == ids[i]) {
            ++sl.count[sl.n - 1];
            continue;
        }
        sl.id[sl.n] = ids[i];
        sl.count[sl.n] = 1;
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
    dk_page_sel();
}

// Darkness's lists start on the page's LAST spell, and a turned page's
// last spell is chosen (DOSBox: Memorize, Cast, the next page); Gateway's
// too (DOSBox: Memorize)
void dk_page_sel()
{
    if (!darkness_files() && !gateway()) return;
    const int rows = list_rows();
    for (int i = sl.top + rows - 1; i >= sl.top; --i)
        if (i < sl.n && sl.id[i]) {
            sl.sel = i;
            return;
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
    // (Secret: no "NAME can memorize:" - the kinds from row 18)
    int row = 18;
    if (w1[0] && d->prof->magic.heading_word) {
        // (Gateway: the name on row 18, "can memorize:" under it - DOSBox)
        char nm[20];
        ch.name(nm, sizeof nm);
        put(c, nm, 1, 18, ch.npc() ? 10 : 11);
        put(c, w1, 1, 19, 10);
        row = 20;
    } else if (w1[0]) {
        say_status(c, w1, 19);
        row = 20;
    }
    static const int kKind[3] = {profile::kClericSpells, profile::kDruidSpells, profile::kMuSpells};
    for (int k = 0; k < 3; ++k) {
        bool any = false;
        for (int lv = 1; lv <= t.slot_levels(); ++lv)
            if (classes::slots(ch, k, lv)) any = true;
        if (!any || row > 22) continue;
        mw(kKind[k], w1, sizeof w1);
        put(c, w1, 1, row, 10);
        // (the counts a column past the heading: Curse's / Secret's 18 letters
        // put them at 20, Darkness's "    Cleric :" at 14)
        const int col0 = 1 + static_cast<int>(strlen(w1)) + 1;
        for (int lv = 1; lv <= t.slot_levels(); ++lv) {
            if (!classes::slots(ch, k, lv)) continue;
            char n[4];
            snprintf(n, sizeof n, "%d", magic::room(ch, t, k, lv));
            put(c, n, col0 + (lv - 1) * 3, row, 10);
        }
        ++row;
    }
    dirty_rows(17, 22);
}

void draw_spells(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    if (gateway() && sl.learning) layout::bar(c, d->tables, d->frame_tiles, 2);    // (its frame GAME.OVR 0x4C43D)
    char nm[20], t[48], a[12], b[16];
    pt->sel()->name(nm, sizeof nm);
    snprintf(t, sizeof t, "%s%s", nm, d->w_s);
    put(c, t, 1, 1, pt->sel()->npc() ? 10 : 11);
    mw(profile::kSpellsWord, a, sizeof a);
    if (sl.choosing) cw(profile::kToChoose, b, sizeof b);
    else if (sl.reading >= 0) cw(profile::kOnScroll, b, sizeof b);
    else if (sl.scrolls) cw(sl.learning ? profile::kToScribe : profile::kOnScrolls, b, sizeof b);
    else mw(sl.learning ? profile::kToMemorize : sl.casting || sl.viewing ? profile::kInMemory : profile::kInGrimoire, b,
            sizeof b);
    snprintf(t, sizeof t, "%s%s", a, b);
    put(c, t, static_cast<int>(strlen(nm)) + 4, 1, 10);
    const int rows = list_rows();
    if ((darkness_files() || gateway()) && (sl.sel < sl.top || sl.sel >= sl.top + rows)) dk_page_sel();   // (a turned page)
    for (int r = 0; r < rows && sl.top + r < sl.n; ++r) {
        const int i = sl.top + r;
        const int row = 5 + r;
        if (!sl.id[i]) {
            if (d->prof->magic.heading_word) {
                char h[40];
                snprintf(h, sizeof h, "%s %s%s", d->w_level[(sl.lvl[i] & 15) - 1], d->w_kind[sl.lvl[i] >> 4],
                         d->w_spells_word);
                put(c, h, 1, row, 13);
            } else {
                put(c, d->w_level[sl.lvl[i] - 1], 1, row, 13);       // headings in the prompt colour
            }
            continue;
        }
        char line[40];
        if (sl.count[i] > 1) snprintf(line, sizeof line, "  %s (%d)", spell_name(sl.id[i]), sl.count[i]);
        else snprintf(line, sizeof line, "  %s", spell_name(sl.id[i]));
        if (gateway() && sl.learning) {
            // (Gateway: "*" before a spell to memorize; lit while it's shown first)
            snprintf(line, sizeof line, " *%s", spell_name(sl.id[i]));
            if (i == sl.sel && gw_preview >= 0) {
                c.fill(2 * 8, row * 8, static_cast<int>(strlen(line) - 1) * 8, 8, 15);
                font::draw_text(c, d->font, line + 1, 2, row, 0, -1);
            } else {
                put(c, line, 1, row, 10);
            }
            continue;
        }
        if (i == sl.sel && !sl.learning) {
            hl_text(c, line + 2, 3, row);
        } else {
            put(c, line, 1, row, 10);
        }
    }
    if (!sl.learning && !sl.casting && !sl.scrolls && !sl.choosing && !sl.viewing && sl.reading < 0) {
        if (gateway()) {
            // (Gateway: the counts boxed off, joints at rows 14, 15, 17, 18 -
            // its frame routine GAME.OVR 0x4C12C; DOSBox)
            layout::bar(c, d->tables, d->frame_tiles, 16);
            for (int col : {0, 39})
                for (int r : {15, 17}) layout::tile(c, d->frame_tiles, 27, col, r);
        }
        draw_counts(c);
    }
    // The menu
    char keys[40], prompt[20], k1[12];
    keys[0] = 0;
    prompt[0] = 0;
    if (!sl.learning || gw_preview >= 0) {
        mw(profile::kChooseSpell, prompt, sizeof prompt);
        if (gw_preview >= 0) prompt[0] = k1[0] = 0;
        else if (sl.viewing) k1[0] = 0;
        else if (sl.casting || sl.reading >= 0) cw(profile::kCastKey, k1, sizeof k1);
        else if (sl.scrolls) cw(profile::kScribeKey, k1, sizeof k1);
        else if (sl.choosing) cw(profile::kLearnKey, k1, sizeof k1);
        else mw(profile::kMemorizeKey, k1, sizeof k1);
        snprintf(keys, sizeof keys, "%s%s%s%s", k1, sl.top + rows < sl.n ? d->w_next : "", sl.top > 0 ? d->w_prev : "",
                 sl.choosing ? "" : d->w_exit);
        text::build(menu, prompt, keys);
        menu.selected = 0;
        hl_first();                     // (Darkness: GAME.OVR 0x29E86 sets the first)
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
    sl.viewing = false;
    build_lines(ids, n);
    screen = Screen::SpellList;
    if (gateway() && gw_preview < 0) {
        // Gateway: the list first with its last spell lit and only Exit,
        // then the question (its memorize overlay, GAME.OVR 0x1EC90 +0x9C2:
        // the list routine seg 0xC0 entry 0x5C in its mode 5; DOSBox)
        gw_preview = word;
        draw_spells(c);
        return;
    }
    gw_preview = -1;
    draw_spells(c);
    char q[40];
    if (d->w_these[0]) {
        // Secret: "Memorize" + " these spells?"
        char k[16];
        mw(profile::kMemorizeKey, k, sizeof k);
        snprintf(q, sizeof q, "%s%s", k, d->w_these);
    } else {
        mw(word, q, sizeof q);
    }
    ask_yes_no(c, Ask::MemorizeThese, q);
    ask_yellow(c);
}

void show_grimoire(pic::Canvas& c)
{
    uint8_t ids[120];
    const int n = magic::known(*pt->sel(), mrules->tables, ids, 120);
    sl.learning = false;
    sl.casting = false;
    sl.scrolls = false;
    sl.choosing = false;
    sl.fighting = false;
    sl.viewing = false;
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
    sl.viewing = false;
    build_lines(ids, n);
    screen = Screen::SpellList;
    draw_spells(c);
    char q[40];
    if (d->w_these[0]) {
        // Secret: "Scribe" + " these spells?"
        char k[16];
        cw(profile::kScribeKey, k, sizeof k);
        snprintf(q, sizeof q, "%s%s", k, d->w_these);
    } else {
        cw(word, q, sizeof q);
    }
    ask_yes_no(c, what, q);
    ask_yellow(c);
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
    sl.viewing = false;
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
    sl.viewing = false;
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
    sl.viewing = false;
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
    sl.viewing = false;
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

// View Character's Spells: those in memory, only looked at
void view_spells(pic::Canvas& c)
{
    if (!load_magic()) {
        end_magic();
        error(c, magic_error());
        return;
    }
    uint8_t ids[84];
    const int n = magic::in_memory(*pt->sel(), mrules->tables, false, ids, 84);
    sl.learning = sl.casting = sl.scrolls = sl.choosing = sl.fighting = false;
    sl.reading = -1;
    sl.viewing = true;
    build_lines(ids, n);
    screen = Screen::SpellList;
    draw_spells(c);
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
    if (gw_preview >= 0) {
        if (k == 'E') confirm_memorize(c, gw_preview);
        return;
    }
    if (sl.reading >= 0) {
        reading_tap(k, c);
        return;
    }
    if (sl.viewing) {
        // View Character's Spells: only paged through
        if (k == 'N' && sl.top + list_rows() < sl.n) sl.top += list_rows();
        else if (k == 'P' && sl.top > 0) sl.top = sl.top > list_rows() ? sl.top - list_rows() : 0;
        else if (k == 'E') {
            sl.viewing = false;
            end_magic();
            screen = Screen::View;
            draw_character(c);
            return;
        } else {
            return;
        }
        draw_spells(c);
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
    bool ship = false;              // Treasures' PROGRAM 4: the script goes on after it
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
    hl_first();                         // (Darkness: GAME.OVR 0x27DFE sets the first)
    show_menu_line(c);
}

void open_rest(pic::Canvas& c, bool from_magic)
{
    if (!load_magic()) {
        error(c, magic_error());
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
    if (rest.ship) {
        // (Treasures' PROGRAM 4: an interruption only ends it - its result
        // isn't looked at by the script's PROGRAM)
        rest.ship = rest.interrupted = false;
        clear_menu_line(c);
        screen = Screen::Game;
        draw_position(c);
        waiting = false;
        handle(vm->resume());
        return;
    }
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
    char k = text::key(menu, text::hit(menu, x / 8));
    if (d->prof->variant.pool_rest_keys) {
        // Pool's "Rest daYs Hours Mins Inc Dec Exit" (create_facts.md 4)
        k = k == 'Y' ? 'D' : k == 'D' ? 'S' : k == 'I' ? 'A' : k;
    }
    switch (k) {
    case 'R':
        if (rest.left <= 0) break;
        rest.running = true;
        magic::begin(rest.r);
        rest.r.paladin_cure = profile::view_facts(d->prof->game).paladin_cure;
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
        if (d->prof->variant.secret_files) rest.r.steps = camp_heal_steps;
        const magic::Step st = magic::step(rest.r, *pt, mrules->tables, scroll_facts(), false);
        camp_heal_steps = rest.r.steps;
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
        error(c, magic_error());
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
    rest.r.paladin_cure = profile::view_facts(d->prof->game).paladin_cure;
    clear_menu_line(c);
    draw_rest_time(c);
}

// Treasures' PROGRAM 4 (ovl 0x74 +0x292C): on a ship's passage the party
// heals itself as the camp's Fix does when anyone is hurt (not with game
// byte 1 set), then the script goes on
void ship_rest(pic::Canvas& c)
{
    if (!d->gs.area1[1] && spells::hp_lost(*pt) > 0) {
        fix_party(c);
        if (screen == Screen::Rest && rest.running) {
            rest.ship = true;
            return;
        }
    }
    waiting = false;
    handle(vm->resume());
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
    OvrWords src;
    if (!at || !src.open()) return;
    text::read_pascal(src, at, out, cap);
}

// A GAME.OVR word by its offset
// A word from words already open (GAME.OVR / the program); a joined word
// (text::kJoined - Secret's "is " + an effect's name) is its two put together
void read_word(dax::ByteSource& src, uint32_t at, char* out, size_t cap)
{
    out[0] = 0;
    if (!at) return;
    if (at & text::kJoined) {
        const uint32_t i = at & 0xFFFF;
        if (!d->prof->joined || i >= d->prof->n_joined) return;
        const profile::JoinedWord& j = d->prof->joined[i];
        read_word(src, j.first, out, cap);
        const size_t n = strlen(out);
        if (n + 1 < cap) read_word(src, j.second, out + n, cap - n);
        return;
    }
    text::read_pascal(src, at, out, cap);
}

void ow(uint32_t at, char* out, size_t cap)
{
    out[0] = 0;
    OvrWords src;
    if (!at || !src.open()) return;
    read_word(src, at, out, cap);
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
        error(c, magic_error());
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
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    if (darkness_files()) {
        // (Darkness: the name in cyan on a line of its own, the words under
        // it - DOSBox, camp Neutralize Poison "DOMINIC" / "IS UNAFFECTED")
        put(c, nm, 1, 18, 11);
        put(c, text, 1, 19, 10);
    } else {
        snprintf(t, sizeof t, "%s %s", nm, text);
        put(c, t, 1, 19, 10);
    }
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
    sl.viewing = false;
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
        // (Darkness: the camp's picture in the view again - DOSBox)
        if (camping && darkness_files() && d->prof->variant.camp_pic) draw_own_pic(camp_pic());
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
    hl_inherit();                       // (Darkness: GAME.OVR 0x373FA - no place of its own)
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
    case spells::Said::Word2: ow(cr.cs->word2, t, sizeof t); break;
    case spells::Said::Word3: ow(cr.cs->word3, t, sizeof t); break;
    case spells::Said::Word4: ow(cr.cs->word4, t, sizeof t); break;
    }
    say_line(c, l.who, t);
    if (cr.on_camp) draw_party(c, 17);
    cr.until = plat::now_ms() + game_delay_ms();
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
    // The Robe of Vermin's effect on the caster: a d2 of 1 - "NAME miscasts" /
    // the spell, nothing else, the spell kept (listing ovr023:1403, the cast)
    const uint8_t vermin = d->prof->fight.facts.items.vermin;
    const bool miscast = vermin && pt->m[cr.caster].has_affect(vermin) && rng.roll(2, 1) == 1;
    // "NAME casts" / the spell, for a moment
    char nm[20], w1[16], t[48];
    pt->m[cr.caster].name(nm, sizeof nm);
    cw(miscast ? profile::kMiscasts : profile::kCasts, w1, sizeof w1);
    snprintf(t, sizeof t, "%s %s", nm, w1);
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    put(c, t, 1, 19, 10);
    if (darkness_files()) put(c, nm, 1, 19, 11);        // (Darkness: the name in cyan - DOSBox)
    put(c, spell_name(spell), 1, 20, 10);
    dirty_rows(17, 22);
    if (darkness_files()) menu_on = false;      // (Darkness: its menu line stays drawn meanwhile - DOSBox)
    else clear_menu_line(c);
    screen = Screen::Cast;
    cr.stage = CastRun::Casts;
    if (miscast) {
        // (then the memory list again: nothing said, nothing cast)
        cr.stage = CastRun::Saying;
        cr.n = cr.at = 0;
    }
    cr.until = plat::now_ms() + game_delay_ms();
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
    if (!abort) hl_inherit();           // (Darkness: GAME.OVR 0x2E555)
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
    cr.until = plat::now_ms() + game_delay_ms();
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
    if (d->prof->variant.tr && it[0x3D] && it[0x3E] == 0x0E) {
        lucky_paper(it, c);
        return;
    }
    const magic::Scrolls sc = scroll_facts();
    const bool scroll = magic::is_scroll(sc, it);
    if (!scroll && !magic::usable(sc, it)) return;
    if (!load_magic()) {
        end_magic();
        error(c, magic_error());
        return;
    }
    if (!scroll) {
        use_spell(i, magic::item_spell(it, pool_files()), false, c);
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
    if (m.effect_names.at) {
        // Secret: its own name for each effect number (none: left out);
        // Darkness: only those in its Display's set
        if (m.effects_shown && !(d->fx_shown[(type & 0xFF) / 8] >> (type % 8) & 1)) return;
        // (Treasures: the names file - text::kInNames)
        const uint32_t base = m.effect_names.at & text::kInNames ? m.effect_names.at : text::kInProgram | m.effect_names.at;
        if (type < m.effect_names.count) ow(base + static_cast<uint32_t>(type) * m.effect_names.stride, out, cap);
        return;
    }
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
    // (Darkness: the page's last line inverse, as its lists start there - DOSBox)
    int last = -1;
    if (darkness_files())
        for (int r = rows - 1; r >= 0 && last < 0; --r)
            if (el->top + r < el->n && el->text[el->top + r][0] && el->text[el->top + r][0] != ' ' ? true
                : el->top + r < el->n && el->text[el->top + r][1])
                last = r;
    for (int r = 0; r < rows && el->top + r < el->n; ++r) {
        const char* t = el->text[el->top + r];
        if (r == last) {
            const char* u = t[0] == ' ' ? t + 1 : t;
            const int col = t[0] == ' ' ? 2 : 1;
            hl_text(c, u, col, 4 + r);
        } else {
            put(c, t, 1, 4 + r, el->colour[el->top + r]);
        }
    }
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
        add(nm, darkness_files() ? 13 : 11);        // (Darkness: the names in its prompt colour)
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

// Darkness (its camp overlay, GAME.OVR 0xDCC8 +0x172B; darkness_alter facts):
// no prompt, "Order Drop Speed Icon Level Exit"; Order's and Speed's menus
// are words of one list ("party order " Select / Place + Exit; "game speed:
// " Faster when above 0, Slower when under 9, Exit); "NAME" (row 18) / "has
// been selected" (row 19); Drop (more than one member) = "Pick Character
// Select", then the party menu's "Drop NAME forever?" / "Are you sure?" and
// its words on the menu line; Level 1-5. Its menus keep the highlight's
// place from the menu before; the Alter menu its own.

enum class AlterMode : uint8_t { Menu, Select, Place, Speed, Pics, Level, Pick };
AlterMode alter_mode = AlterMode::Menu;
int alter_hl = 0;                   // (kept_highlight) the Alter menu's own highlight

void aw(int i, char* out, size_t cap) { ow(d->prof->alter.words[i], out, cap); }

bool kept_hl() { return d->prof->alter.kept_highlight; }

// Darkness's menu routine: the highlight stays at its place in the menu
// shown next (past its end: the first word)
void keep_place(int at) { menu.selected = at >= 0 && at < menu.count ? at : 0; }

// Word k of a word list (out: "" if none)
void list_word(const char* list, int k, char* out, size_t cap)
{
    out[0] = 0;
    const char* p = list;
    for (int i = 0; *p; ++i) {
        while (*p == ' ') ++p;
        const char* e = p;
        while (*e && *e != ' ') ++e;
        if (i == k) {
            snprintf(out, cap, "%.*s", static_cast<int>(e - p), p);
            return;
        }
        p = e;
    }
}

// A word list without its word k (the menus the program shows a list's
// other words of)
void drop_word(char* list, int k)
{
    char* p = list;
    for (int i = 0; *p; ++i) {
        while (*p == ' ') ++p;
        char* e = p;
        while (*e && *e != ' ') ++e;
        if (i == k) {
            while (*e == ' ') ++e;
            if (!*e && p > list) --p;          // (the last word: the space before it goes)
            memmove(p, e, strlen(e) + 1);
            return;
        }
        p = e;
    }
}

void alter_menu(pic::Canvas& c)
{
    char prompt[12];
    aw(profile::kAlterPrompt, prompt, sizeof prompt);
    alter_mode = AlterMode::Menu;
    text::build(menu, prompt, d->w_alter);
    if (kept_hl()) keep_place(alter_hl);
    else menu.selected = 0;
    show_menu_line(c);
}

void open_alter(pic::Canvas& c)
{
    screen = Screen::Alter;
    alter_hl = 0;
    alter_menu(c);
}

void start_icon(pic::Canvas& c, Screen from);

// Pics (alter_icon_facts.md 2): the area word 0x4BFF = Pics x 2 +
// Animation, both on at the start; the menu names the current state ("Pics
// on  Animation on  Exit"; Animation isn't offered while Pics is off).
// Animation off: event pictures show their first frame only. Pics itself
// changes nothing else in Curse.
bool pics_on() { return !vm || (vm->get(0x4BFF) & 2); }
bool animation_on() { return !vm || d->prof->variant.pics_portraits || (vm->get(0x4BFF) & 1); }
// Pool's Alter -> Pics: Portraits (View Character's portrait) / Monsters
// (the event pictures while exploring the 3D areas; its ovr8 +0619)
bool portraits_on() { return !vm || !d->prof->variant.pics_portraits || (vm->get(0x4BFF) & 1); }
bool monster_pics_on() { return !vm || !d->prof->variant.pics_portraits || (vm->get(0x4BFF) & 2); }

void pics_menu(pic::Canvas& c)
{
    alter_mode = AlterMode::Pics;
    char words[56], a[20];
    words[0] = 0;
    aw(pics_on() ? profile::kPicsOn : profile::kPicsOff, a, sizeof a);
    strlcat(words, a, sizeof words);
    // (Pool: "Monsters on  Portraits on  Exit" - both always offered)
    if (pics_on() || d->prof->variant.pics_portraits) {
        const bool second = d->prof->variant.pics_portraits ? portraits_on() : animation_on();
        aw(second ? profile::kAnimOn : profile::kAnimOff, a, sizeof a);
        strlcat(words, a, sizeof words);
    }
    aw(profile::kPicsExit, a, sizeof a);
    strlcat(words, a, sizeof words);
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

// Secret's Level (its difficulty; GAME.OVR 0x1BE15): "Novice Squire Veteran
// Adept Champion", the current one lit (area word 0x4CF4 = 0-4; a new game
// and the GOG save: 2, Veteran); a choice sets it and Alter's menu comes
// back. A fight's monsters on the other side are loaded with their hit
// points x (level + 2) / 4 (fight_load_monster).
void level_menu(pic::Canvas& c)
{
    alter_mode = AlterMode::Level;
    char words[48];
    ow(d->prof->alter.level_menu, words, sizeof words);
    text::build(menu, "", words);
    const int level = (vm->get(0x4CF4) & 0xFF) - d->prof->alter.level_base;   // (Darkness: 1-5)
    menu.selected = level >= 0 && level < menu.count ? level : 0;
    show_menu_line(c);
}

// keep: (Darkness) the highlight stays at the place of the word picked
void order_menu(pic::Canvas& c, bool place, bool keep = false)
{
    char prompt[16];
    aw(profile::kPartyOrder, prompt, sizeof prompt);
    const int at = menu.selected;
    alter_mode = place ? AlterMode::Place : AlterMode::Select;
    text::build(menu, prompt, place ? d->w_place : d->w_select);
    if (keep && kept_hl()) keep_place(at);
    else menu.selected = 0;
    show_menu_line(c);
}

// "NAME has been selected" - Darkness: NAME (row 18, in its party list
// colour but not the selected one's) over the words (row 19; GAME.OVR
// 0x359A6), the text rows cleared
void say_selected(pic::Canvas& c)
{
    char t[24];
    aw(profile::kHasBeenSelected, t, sizeof t);
    if (!darkness_files()) {
        say_status(c, t);
        return;
    }
    char nm[20];
    const party::Character& ch = *pt->sel();
    ch.name(nm, sizeof nm);
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    put(c, nm, 1, 18, ch.in_combat() ? 11 : 12);
    put(c, t, 1, 19, 10);
    dirty_rows(17, 22);
}

void draw_speed(pic::Canvas& c, bool keep = false)
{
    const int speed = vm->get(0x4BFC) & 0xFF;
    char a[40], b[26], t[48], words[32], prompt[14], w1[10];      // (Darkness's a: "Game Speed (0=fastest 9=slowest) = ")
    aw(profile::kSpeedIs, a, sizeof a);
    aw(profile::kSpeedRange, b, sizeof b);
    snprintf(t, sizeof t, "%s%d%s", a, speed, b);
    c.fill(8, 18 * 8, 38 * 8, 8, 0);
    put(c, t, 1, 18, 10);
    dirty_rows(18, 18);
    words[0] = 0;
    if (d->w_speed[0]) {
        // Darkness: "Faster Slower Exit" without Slower at 9, Faster at 0
        strlcpy(words, d->w_speed, sizeof words);
        if (speed >= 9) drop_word(words, 1);
        if (speed <= 0) drop_word(words, 0);
    } else {
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
    }
    const char* k = words;
    while (*k == ' ' && !gateway()) ++k;          // (Gateway's " Faster" keeps its space - DOSBox)
    aw(profile::kSpeedPrompt, prompt, sizeof prompt);
    const int at = menu.selected;
    alter_mode = AlterMode::Speed;
    text::build(menu, prompt, k);
    if (keep && kept_hl()) keep_place(at);
    else menu.selected = 0;
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
    if (d->prof->alter.pick) {
        // Darkness: nothing with one member; else "Pick Character Select"
        if (pt->count <= 1) {
            alter_menu(c);
            return;
        }
        char prompt[20], words[12];
        ow(d->prof->alter.pick, prompt, sizeof prompt);
        ow(d->prof->alter.pick_menu, words, sizeof words);
        const int at = menu.selected;
        alter_mode = AlterMode::Pick;
        text::build(menu, prompt, words);
        keep_place(at);
        show_menu_line(c);
        return;
    }
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

void delete_character(const char* base);

// Darkness's Drop (the party menu's, GAME.OVR 0x164AA): "Drop NAME
// forever?", "Are you sure?" (both on No first) - Yes: "You dump NAME out
// back." (one who can't stand) / "NAME bids you farewell.", their files
// gone; No: "NAME breathes a sigh of relief." - each on the menu line for
// the game's delay, then the Alter menu
bool alter_drop_dk(Ask what, char k, pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch) {
        alter_menu(c);
        return true;
    }
    char nm[20], t[64];
    ch->name(nm, sizeof nm);
    if (what == Ask::AlterDrop && k == 'Y') {
        ask_yes_no(c, Ask::AlterSure, rw(Data::kSure));
        return true;
    }
    if (what == Ask::AlterSure && k == 'Y') {
        if (!ch->in_combat()) snprintf(t, sizeof t, "%s%s%s", rw(Data::kDump), nm, rw(Data::kOutBack));
        else snprintf(t, sizeof t, "%s%s", nm, rw(Data::kFarewell));
        char base[12];
        guy_base(*ch, base, sizeof base);
        delete_character(base);
        leave_party(pt->selected);
        draw_panel(c);
    } else {
        snprintf(t, sizeof t, "%s%s", nm, rw(Data::kRelief));
    }
    alter_menu(c);
    note(c, t);
    return true;
}

bool alter_yes_no(Ask what, char k, pic::Canvas& c)
{
    if (what != Ask::AlterDrop && what != Ask::QuitDos && what != Ask::AlterSure) return false;
    screen = Screen::Alter;
    if (d->prof->alter.pick && what != Ask::QuitDos) return alter_drop_dk(what, k, c);
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

// Exit (or Esc): the camp again
void alter_leave(pic::Canvas& c)
{
    screen = Screen::Camp;
    draw_camp(c);
    if (kept_hl()) {
        // Darkness (DOSBox): the camp's text window empty, its menu's
        // highlight on Alter (the camp keeps its own place)
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
        for (int i = 0; i < menu.count; ++i)
            if (text::key(menu, i) == 'A') menu.selected = i;
        show_menu_line(c);
    }
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
    const int hit = text::hit(menu, col);
    const char k = text::key(menu, hit);
    if (!k) return;
    if (kept_hl()) {
        // (Darkness: the highlight stays where the word picked was)
        menu.selected = hit;
        if (alter_mode == AlterMode::Menu) alter_hl = hit;
    }
    switch (alter_mode) {
    case AlterMode::Menu:
        switch (k) {
        case 'O': order_menu(c, false); break;
        case 'D': alter_drop(c); break;
        case 'S': draw_speed(c); break;
        case 'P': pics_menu(c); break;
        case 'L':
            if (d->prof->alter.level_menu) level_menu(c);
            break;
        case 'I': start_icon(c, Screen::Alter); break;
        case 'E': alter_leave(c); break;
        default: break;
        }
        return;
    case AlterMode::Pics: {
        const uint16_t v = vm->get(0x4BFF);
        const bool pool = d->prof->variant.pics_portraits;      // M Monsters, P Portraits
        if (k == (pool ? 'M' : 'P')) vm->set(0x4BFF, static_cast<uint16_t>(v ^ 2));
        else if (k == (pool ? 'P' : 'A')) vm->set(0x4BFF, static_cast<uint16_t>(v ^ 1));
        else if (k == 'E') {
            alter_menu(c);
            return;
        }
        pics_menu(c);
        return;
    }
    case AlterMode::Level: {
        if (hit >= 0 && hit < 5) vm->set(0x4CF4, static_cast<uint16_t>(hit + d->prof->alter.level_base));
        alter_menu(c);
        return;
    }
    case AlterMode::Pick: {
        if (k != 'S') return;
        char t[60], nm[20];
        pt->sel()->name(nm, sizeof nm);
        snprintf(t, sizeof t, "%s%s%s", rw(Data::kDrop), nm, rw(Data::kForever));
        ask_yes_no(c, Ask::AlterDrop, t);
        return;
    }
    case AlterMode::Select:
        if (k == 'S') {
            say_selected(c);
            order_menu(c, true, true);
        } else if (k == 'E') {
            alter_menu(c);
        }
        return;
    case AlterMode::Place:
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
        if (k == 'P') order_menu(c, false, true);
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
        draw_speed(c, true);
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
// hit points; experience past the level after the next is lost (one under
// it kept - create::train_pick). (A magic-user's new spell: with the spells.)

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
    plat::log("[play] training: %s\n", t);
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
    // (free once the game is won; Pool rounds; Secret's halls are free - no
    // "Training costs" word: secret_facts_2.md 18)
    if (!game_won && d->prof->shop_words[profile::kTrainCost] && train_payer < 0 &&
        rules::gold_worth(ch, pool_files()) < 1000) {
        train_note(profile::kTrainCost, c);
        if (d->prof->party.who_pays) {
            // Gateway: "Training costs 1000 gp." then "Who pays 1000 gp
            // Select Exit" over the party (GAME.OVR 0x2650E +0x51B3)
            char prompt[24], words[16];
            ow(d->prof->party.who_pays, prompt, sizeof prompt);
            ow(d->prof->party.pick_menu, words, sizeof words);
            train_who = pt->selected;
            pm_pick_for = 'P';
            text::build(menu, prompt, words);
            menu.selected = 0;
        }
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
    // The class that needs the most experience of those ready (one a time),
    // and the experience held to one under the level after the next - as
    // the original, before it looks at the hall and the answer
    uint32_t keep = 0;
    const int pick = create::train_pick(ch, t, &keep);
    for (int i = 0; i < 4; ++i) ch.rec[0x127 + i] = static_cast<uint8_t>(keep >> (8 * i));
    if (!(has & here)) {
        end_training();
        train_note(profile::kTrainClass, c);
        return;
    }
    train_mask = pick & here;
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
        if (!game_won && d->prof->shop_words[profile::kTrainCost])
            rules::pay(train_payer >= 0 && train_payer < pt->count ? pt->m[train_payer] : *ch, 1000);
        train_payer = -1;
        const int mu = ch->level(classes::MagicUser);
        create::train_classes(*ch, trainer->tables, trainer->facts, trainer->dice, train_mask, false);
        rules::recalc(*ch, *names, d->facts);
        end_training();
        // A magic-user's new level, or any training of a ranger past 8th (the
        // original asks whichever class went up): a new spell
        if ((ch->level(classes::MagicUser) > mu || ch->level(classes::Ranger) > 8) && open_learn(c))
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
// Darkness: the menu word last used stays lit (0 Add, 1 Subtract), and the
// name, once Modify has drawn it, is green (GAME.OVR 0x16606; DOSBox)
int  mod_word = 0;
bool mod_name_seen = false;

void draw_modify(pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch) return;
    draw_character(c);
    char t[24];
    if (darkness_files() && modder) {
        // Darkness marks a stat Modify has changed (its in-use value no longer
        // its own until Keep) with a "*" at column 12 - the highlight colour
        // on the line being changed (GAME.OVR 0x244E5; DOSBox)
        for (int i = 0; i < 6; ++i) {
            const bool changed = ch->stat(i) != modder->ch.stat(i) || (i == 0 && ch->str00() != modder->ch.str00());
            c.fill(12 * 8, (9 + i) * 8, 8, 8, 0);
            if (changed) put(c, "*", 12, 9 + i, mod_item == i ? 13 : 10);
        }
    }
    if (secret_files() || darkness_files()) {
        // Secret's View: the stats from row 9 (the number at column 5), the
        // hit points "95/95" at row 3, column 31; its menu "Add Subtract
        // Keep Exit" (Add / Subtract: the arrows' up / down)
        if (mod_item < 6) {
            const int v = ch->stat(mod_item);
            c.fill(5 * 8, (9 + mod_item) * 8, 7 * 8, 8, 0);
            if (mod_item == 0 && v == 18 && ch->str00() > 0) {
                if (ch->str00() == 100) snprintf(t, sizeof t, "18(00)");
                else snprintf(t, sizeof t, "18(%02d)", ch->str00());
            } else {
                snprintf(t, sizeof t, "%d", v);
            }
            put(c, t, 5, 9 + mod_item, 13);
        } else if (mod_item == 6) {
            c.fill(31 * 8, 3 * 8, 8 * 8, 8, 0);
            snprintf(t, sizeof t, "%d/%d", ch->hp(), ch->hp_max());
            put(c, t, 31, 3, 13);
        } else {
            ch->name(t, sizeof t);
            put(c, t, 1, 1, 13);
            mod_name_seen = true;
        }
        if (darkness_files() && mod_item != 7 && mod_name_seen) {
            ch->name(t, sizeof t);
            put(c, t, 1, 1, 10);
        }
        text::build(menu, rw(Data::kModify), rw(Data::kKeepExit));
        menu.selected = darkness_files() ? mod_word : 0;
        show_menu_line(c);
        return;
    }
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
    if (!create::can_modify(*ch, d->prof->variant.pool_files, secret_files(), darkness_files())) {
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
    mod_word = 0;
    mod_name_seen = false;
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
    if (secret_files() || darkness_files()) {
        if (row >= 9 && row <= 14 && col >= 1 && col <= 12) return row - 9;
        if (row == 3 && col >= 20 && col <= 38) return 6;
        if (row == 1 && col >= 1 && col <= 16) return 7;
        return -1;
    }
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
        case 'A': mod_word = 0; modify_step(1, c); break;            // (Secret's Add / Subtract)
        case 'S': mod_word = 1; modify_step(-1, c); break;
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

void pm_choose_key(char key, pic::Canvas& c);
void pm_choose(int i, pic::Canvas& c) { pm_choose_key(pm_key(i), c); }
void pm_choose_key(char key, pic::Canvas& c)
{
    // Gateway: a member's functions ask "Pick Character Select Exit" first
    // (its party menu overlay GAME.OVR 0x2650E +0x27E; DOSBox)
    if (d->prof->party.pick && !pm_picked && pt->count && strchr("DMITHVR", key)) {
        char prompt[20], words[16];
        ow(d->prof->party.pick, prompt, sizeof prompt);
        ow(d->prof->party.pick_menu, words, sizeof words);
        pm_pick_for = key;
        text::build(menu, prompt, words);
        menu.selected = 0;
        show_menu_line(c);
        return;
    }
    pm_picked = false;
    switch (key) {
    case 'L':
        if (darkness_files() && d->prof->party.load_from && !load_where) {
            // Darkness: "Load from where? Pools Secret Exit" first
            // (darkness_facts.md 32)
            load_where = true;
            screen = Screen::LoadWhich;
            if (d->prof->variant.tr) {
                // (Treasures: the frame alone while it asks - DOSBox)
                layout::outer(c, d->tables, d->frame_tiles);
                dirty(0, pic::kScreenH);
            }
            text::build(menu, d->load_from, d->load_sources);
            menu.selected = 0;
            hl_inherit();
            show_menu_line(c);
            return;
        }
        load_where = false;
        if (load_secret) find_secret_saves();
        else find_saves();
        if (!pm_saves[0]) {
            load_secret = false;
            break;
        }
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
            hl_inherit();
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
        if (!d->prof->party.add_from) {
            // Pool of Radiance: its own characters only, straight to the list
            find_guys();
            if (!d->guys) return;
            screen = Screen::AddList;
            plist = PickList{};
            draw_add_list(c);
            return;
        }
        screen = Screen::AddFrom;
        text::build(menu, rw(Data::kAddFrom), rw(Data::kAddSources));
        menu.selected = 0;
        hl_inherit();
        show_menu_line(c);
        return;
    case 'R':
        remove_character(c, false);
        return;
    case 'D':
        if (pt->sel()) {
            if (d->prof->alter.pick) {
                // Darkness: the party menu's Drop is the camp's (GAME.OVR
                // 0x164AA): "Pick Character Select" first (darkness_alter_facts.md A2)
                char prompt[20], words[12];
                ow(d->prof->alter.pick, prompt, sizeof prompt);
                ow(d->prof->alter.pick_menu, words, sizeof words);
                pm_picking = true;
                text::build(menu, prompt, words);
                menu.selected = 0;
                hl_inherit();
                show_menu_line(c);
                return;
            }
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
    case 'I':                                   // (Gateway's Icon Select)
        if (gateway() && pt->sel()) {
            start_icon(c, Screen::PartyMenu);
            return;
        }
        break;
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
        // The portrait box swaps portrait / icon; the menu line's words
        // (Items, Spells, Trade, Drop, Heal, Cure); anything else is Exit
        if (view_box_tap(x, y, c)) return;
        if (y >= text::kMenuTapTop) {
            const char k = text::key(menu, text::hit(menu, col));
            if (k == 'I') {
                open_items(c);
                return;
            }
            if (view_menu(k, c)) return;
        }
        back_from_view(c);
        return;
    }
    if (screen == Screen::ViewWho) {
        view_who_tap(x, y, c);
        return;
    }
    if (screen == Screen::CreatePick) {
        list_tap(x, y, c);
        return;
    }
    if (screen == Screen::CreateName) return;      // the keyboard types the name
    if (screen == Screen::Portrait) {
        if (y >= text::kMenuTapTop) {
            const int k = text::hit(menu, col);
            if (k >= 0) {
                menu.selected = k;
                portrait_key(text::key(menu, k), c);
            }
        }
        return;
    }
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
        if (load_where) {
            // Pools: its own saved games; Secret: Secret's (its folder on the
            // card) - Treasures: Treasures / Gateway (the first word the
            // game's own, the second the earlier game's, Exit last)
            load_secret = k == 1 && k + 1 < menu.count;
            if (k + 1 < menu.count) {
                pm_choose_key('L', c);
            } else {
                load_where = false;
                screen = Screen::PartyMenu;
                draw_party_menu(c);
            }
            return;
        }
        if (load_secret) {
            load_secret = false;
            load_secret_game(text::key(menu, k));
        } else {
            load_game(text::key(menu, k));
        }
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        if (load_problem) error(c, load_problem);
        return;
    }
    if (pm_pick_for) {
        // Gateway: a member picked on the list, Select goes on with the
        // function, Exit (or the party menu's lines) leaves it
        if (y >= text::kMenuTapTop) {
            const char k = text::key(menu, text::hit(menu, col));
            const char f = pm_pick_for;
            if (k == 'S' && pt->sel() && f == 'P') {
                // who pays: one with 1000 gp's worth, else asked again
                if (rules::gold_worth(*pt->sel(), pool_files()) < 1000) {
                    char t[48];
                    sw(profile::kTrainCost, t, sizeof t);
                    note(c, t);
                    return;
                }
                pm_pick_for = 0;
                train_payer = pt->selected;
                if (train_who >= 0 && train_who < pt->count) pt->selected = train_who;
                pm_prompt(c);
                train_character(c);
                if (screen == Screen::PartyMenu && !pm_pick_for) train_payer = -1;
            } else if (k == 'S' && pt->sel()) {
                pm_pick_for = 0;
                pm_picked = true;
                pm_prompt(c);
                pm_choose_key(f, c);
            } else if (k == 'E') {
                pm_pick_for = 0;
                train_payer = -1;
                draw_party_menu(c);
            }
            return;
        }
        if (row >= 4 && row < 4 + pt->count && col >= 1) {
            pt->selected = row - 4;
            draw_party_menu(c);
            show_menu_line(c);
            return;
        }
        pm_pick_for = 0;
    }
    if (pm_picking) {
        // Darkness's Drop: "Pick Character Select" - a member picked on the
        // list, Select asks "Drop NAME forever?"; a menu line of the party
        // menu leaves it
        if (y >= text::kMenuTapTop) {
            if (text::key(menu, text::hit(menu, col)) != 'S' || !pt->sel()) return;
            pm_picking = false;
            char t[60], nm[20];
            pt->sel()->name(nm, sizeof nm);
            snprintf(t, sizeof t, "%s%s%s", rw(Data::kDrop), nm, rw(Data::kForever));
            ask_yes_no(c, Ask::Drop, t);
            return;
        }
        if (row >= 4 && row < 4 + pt->count && col >= 1) {
            pt->selected = row - 4;
            draw_party_menu(c);
            show_menu_line(c);
            return;
        }
        pm_picking = false;
    }
    if (row >= 4 && row < 4 + pt->count && col >= 1) {
        pt->selected = row - 4;
        draw_party_menu(c);
        return;
    }
    const int line = row - pm_row0();
    if (line >= 0 && line < pm_lines && y < text::kMenuTapTop) {
        if (d->prof->variant.tr) pm_sel = line;     // (Treasures: its menu keeps the line picked white)
        pm_prompt(c);
        pm_choose(pm_item[line], c);
    }
}

// Reads the menu's words from the program (and GAME.OVR) and the save
// folder from the game's configuration file
__attribute__((noinline)) void load_party_text(dax::ByteSource& exe, const exepack::Info& info)
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
        // (every game's names are made from the item's words when shown -
        // Secret's too: its records' own names are a list line's left-overs,
        // "Two-Handed Sword of Giant Slaying" shows as "Two-Handed Sword of
        // vs Giants" - DOSBox; secret_facts_2.md 25)
        names->stored_names = false;
        d->facts.arrow = pi.arrow;
        d->facts.quarrel = pi.quarrel;
        memcpy(d->facts.elf_bonus, pi.elf_bonus, sizeof d->facts.elf_bonus);
        d->facts.elf_bonus_any_race = secret_files();
        d->facts.strength_fx = pi.stat_fx[0];
        d->facts.giant_fx = pi.stat_fx[1];
        d->facts.enlarge_fx = pi.stat_fx[2];
        d->facts.friends_fx = pi.stat_fx[3];
        d->facts.friends_max = d->prof->variant.pool_files ? 25 : 0;      // (Pool: Charisma 25 at most)
        d->facts.feeble_fx = pi.stat_fx[4];
        d->facts.con_regen_fx = pi.stat_fx[5];
        d->facts.berserk_fx = pi.berserk;
        d->facts.light_magic_armour = pi.light_magic_armour;
        d->facts.speed_fx = pi.speed_fx;
        d->facts.sell_secret = pi.sell_secret;
        rules::set_berserk_fx(pi.berserk);
        items::set_passive(pi.passive);
        items::set_flame_fix(&pi.flame, CYD_ITEM_FIXES && opts.item_fixes);
        rules::set_fixes(CYD_ITEM_FIXES && opts.item_fixes);
        rules::set_worn_facts(pi.effect_data, pi.stat_fx[1]);
        {
            const auto& pc = d->prof->create;
            if (pc.ds_image && pc.tables.max_hit_dice &&
                exepack::read(exe, info, pc.ds_image + pc.tables.max_hit_dice, d->facts.max_hd, 8) !=
                    exepack::Status::Ok)
                memset(d->facts.max_hd, 0, sizeof d->facts.max_hd);
        }
        d->facts.hammer_fx = pi.hammer[0];
        d->facts.hammer_code = pi.hammer_code;
        d->facts.hammer_type = pi.hammer[1];
        d->facts.hammer_word = pi.hammer[2];
        d->facts.hammer_word2 = pi.hammer[3];
        plat::File tf;
        if (pi.types_file && open_file(pi.types_file, tf)) {
            plat::FileSource src(tf);
            names->read_types(src, pi.types_head, pi.types_stride);
            tf.close();
        }
        if (gateway()) {
            // (Gateway's party list: a name that may train is magenta - DOSBox)
            const auto& pc = d->prof->create;
            uint8_t raw[44];
            d->gw_xp = static_cast<int32_t*>(malloc(8 * 11 * sizeof(int32_t)));
            for (int k = 0; d->gw_xp && k < 8; ++k) {
                if (exepack::read(exe, info, pc.ds_image + pc.tables.class_records + k * 99u, raw, sizeof raw) !=
                    exepack::Status::Ok) {
                    free(d->gw_xp);
                    d->gw_xp = nullptr;
                    break;
                }
                for (int i = 0; i < 11; ++i) {
                    const uint8_t* p = raw + i * 4;
                    d->gw_xp[k * 11 + i] =
                        static_cast<int32_t>(p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24);
                }
            }
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
        if (secret_files()) {
            // Secret's races in its own order: put them in Curse's (the
            // record's numbers; party::race_from_secret)
            char tmp[8][10];
            memcpy(tmp, d->race, sizeof tmp);
            for (int i = 0; i < 8; ++i) memcpy(d->race[i], tmp[party::race_to_secret(static_cast<uint8_t>(i))], sizeof tmp[0]);
        }
        if (darkness_files()) {
            // Darkness's races in its own order (0 elf ... 6 monster;
            // party::race_from_darkness) and its three coins - platinum,
            // gems, jewelry - in Curse's places 4-6
            char tmp[8][10];
            memcpy(tmp, d->race, sizeof tmp);
            for (int i = 0; i < 8; ++i) memcpy(d->race[i], tmp[party::race_to_darkness(static_cast<uint8_t>(i))], sizeof tmp[0]);
            for (int i = 6; i >= 4; --i) memcpy(d->money[i], d->money[i - 4], sizeof d->money[0]);
            for (int i = 0; i < 4; ++i) d->money[i][0] = 0;
        }
    }
    // (its files closed before the roster's words open theirs: the card
    // has 4 file slots, and Pool's words need START.EXE open too)
    {
    OvrWords src;
    if (src.open()) {
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
            {d->prof->magic.heading_word, d->w_spells_word, sizeof d->w_spells_word},
        };
        for (auto& wd : words)
            if (wd.at) read_word(src, wd.at, wd.out, wd.cap);
        for (int i = 0; i < profile::kItemWords; ++i)
            if (d->prof->item_words[i]) text::read_pascal(src, d->prof->item_words[i], d->iw[i], sizeof d->iw[i]);
        if (d->prof->magic.effects_shown && src.read_at(d->prof->magic.effects_shown, d->fx_shown, 32) != 32)
            memset(d->fx_shown, 0xFF, 32);
        if (pi.menu) {
            // Darkness: "Ready Use Trade Deposit Drop Halve Join Sell Id" - the
            // first word Ready, the rest the item words (" Use" ...)
            char m[64];
            text::read_pascal(src, pi.menu, m, sizeof m);
            static const int kTo[] = {-1, profile::kUse, profile::kTrade, profile::kDeposit, profile::kDrop,
                                      profile::kHalve, profile::kJoin, profile::kSell, profile::kId};
            int w = 0;
            for (const char* q = m; *q && w < 9;) {
                while (*q == ' ') ++q;
                const char* e = q;
                while (*e && *e != ' ') ++e;
                if (e == q) break;
                if (kTo[w] < 0) snprintf(d->w_ready, sizeof d->w_ready, "%.*s", static_cast<int>(e - q), q);
                else snprintf(d->iw[kTo[w]], sizeof d->iw[0], " %.*s", static_cast<int>(e - q), q);
                ++w;
                q = e;
            }
        }
        {
            const profile::ViewFacts& vf = profile::view_facts(d->prof->game);
            for (int i = 0; i < profile::kViewWords; ++i)
                if (vf.words[i]) text::read_pascal(src, vf.words[i], d->vw[i], sizeof d->vw[i]);
            if (vf.menu) {
                // Darkness: "Items Spells Trade Deposit Drop Lay Cure Exit" - words
                // 2-7 are Spells ... Cure (each with its space, as the others)
                char m[64];
                text::read_pascal(src, vf.menu, m, sizeof m);
                static const int kTo[] = {-1, profile::kVwSpells, profile::kVwTrade, profile::kVwDeposit,
                                          profile::kVwDrop, profile::kVwHeal, profile::kVwCure};
                int w = 0;
                for (const char* q = m; *q && w < 7;) {
                    while (*q == ' ') ++q;
                    const char* e = q;
                    while (*e && *e != ' ') ++e;
                    if (e == q) break;
                    if (kTo[w] >= 0)
                        snprintf(d->vw[kTo[w]], sizeof d->vw[0], "%.*s ", static_cast<int>(e - q), q);
                    ++w;
                    q = e;
                }
            }
        }
        for (int i = 0; i < 6 && pv.stats; ++i)
            text::read_pascal(src, pv.stats + static_cast<uint32_t>(i) * pv.stats_stride, d->v_stat[i], sizeof d->v_stat[i]);
        text::read_pascal(src, pp.choose, d->choose, sizeof d->choose);
        text::read_pascal(src, pp.load_which, d->load_which, sizeof d->load_which);
        if (pp.load_from) text::read_pascal(src, pp.load_from, d->load_from, sizeof d->load_from);
        if (pp.load_sources) text::read_pascal(src, pp.load_sources, d->load_sources, sizeof d->load_sources);
        text::read_pascal(src, pp.name, d->name_head, sizeof d->name_head);
        text::read_pascal(src, pp.ac_hp, d->ac_hp_head, sizeof d->ac_hp_head);
    }
    }
    if (!d->choose[0]) strcpy(d->choose, "Choose a function ");
    if (!d->load_which[0]) strcpy(d->load_which, "Load Which Game: ");
    if (!d->name_head[0]) strcpy(d->name_head, "Name");
    if (!d->ac_hp_head[0]) strcpy(d->ac_hp_head, "AC  HP");
    if (!d->v_exit[0]) strcpy(d->v_exit, "Exit");
    if (pp.camp_menu) text::read_pascal(exe, info, pp.camp_menu, d->w_camp_menu, sizeof d->w_camp_menu);
    if (d->prof->variant.tr_crown_menu)
        text::read_pascal(exe, info, d->prof->variant.tr_crown_menu, d->w_crown_menu, sizeof d->w_crown_menu);
    if (d->prof->variant.tr_max_age) {
        uint8_t ages[16];
        if (exepack::read(exe, info, d->prof->create.ds_image + d->prof->variant.tr_max_age, ages, sizeof ages) ==
            exepack::Status::Ok)
            for (int k = 0; k < 8; ++k) d->tr_max_age[k] = static_cast<uint16_t>(ages[2 * k] | ages[2 * k + 1] << 8);
    }
    if (d->prof->variant.tr) {
        // Treasures: its menus show only the words their callers enable (its
        // o121+0x1134) - the camp's lone "C" never (DOSBox)
        char* t = strstr(d->w_camp_menu, " C ");
        if (t) memmove(t, t + 2, strlen(t + 2) + 1);
    }
    if (d->prof->variant.dk_overland_pics &&
        exepack::read(exe, info, d->prof->create.ds_image + d->prof->variant.dk_overland_pics, d->dk_ov_pics, 5) !=
            exepack::Status::Ok)
        memset(d->dk_ov_pics, 0, sizeof d->dk_ov_pics);
    const auto& pm = d->prof->magic;
    if (pm.menu) text::read_pascal(exe, info, pm.menu, d->w_magic, sizeof d->w_magic);
    const auto& pa = d->prof->alter;
    if (pa.menu) text::read_pascal(exe, info, pa.menu, d->w_alter, sizeof d->w_alter);
    if (pa.select) text::read_pascal(exe, info, pa.select, d->w_select, sizeof d->w_select);
    if (pa.place) text::read_pascal(exe, info, pa.place, d->w_place, sizeof d->w_place);
    if (pa.order_list) {
        // Darkness: "Select Place Exit" -> "Select Exit" / "Place Exit"
        char list[24], w0[10], w1[10], w2[10];
        if (text::read_pascal(exe, info, pa.order_list, list, sizeof list)) {
            list_word(list, 0, w0, sizeof w0);
            list_word(list, 1, w1, sizeof w1);
            list_word(list, 2, w2, sizeof w2);
            snprintf(d->w_select, sizeof d->w_select, "%s %s", w0, w2);
            snprintf(d->w_place, sizeof d->w_place, "%s %s", w1, w2);
        }
    }
    if (pa.speed_list) text::read_pascal(exe, info, pa.speed_list, d->w_speed, sizeof d->w_speed);
    for (int i = 0; i < 9 && i < pm.levels.count && pm.levels.at; ++i)
        text::read_pascal(exe, info, pm.levels.at + static_cast<uint32_t>(i) * pm.levels.stride, d->w_level[i],
                          sizeof d->w_level[i]);
    if (pm.these_spells) text::read_pascal(exe, info, pm.these_spells, d->w_these, sizeof d->w_these);
    for (int i = 0; i < 3 && i < pm.kind_names.count && pm.kind_names.at; ++i)
        text::read_pascal(exe, info, pm.kind_names.at + static_cast<uint32_t>(i) * pm.kind_names.stride, d->w_kind[i],
                          sizeof d->w_kind[i]);
    if (pm.level_word) {
        // Secret: "1st" + " Level"
        char lw[12];
        if (text::read_pascal(exe, info, pm.level_word, lw, sizeof lw))
            for (int i = 0; i < 9; ++i)
                if (d->w_level[i][0])
                    snprintf(d->w_level[i] + strlen(d->w_level[i]), sizeof d->w_level[i] - strlen(d->w_level[i]), "%s", lw);
    }
    {
        const uint32_t at[Data::kRosterWords] = {pp.add_from, pp.add_sources, pp.add_prompt, pp.add, pp.added,
                                                 pp.paladin_evil, pp.rangers, pp.no_evil, pp.overwrite, pp.qmark,
                                                 pp.drop, pp.forever, pp.sure, pp.dump, pp.out_back, pp.farewell,
                                                 pp.relief, pp.yes_no, pp.cant_modify, pp.modify, pp.keep_exit,
                                                 pp.from_saved, pp.new_file};
        OvrWords rsrc;
        if (rsrc.open())
            for (int i = 0; i < Data::kRosterWords; ++i)
                if (at[i]) text::read_pascal(rsrc, at[i], d->roster[i], sizeof d->roster[i]);
        if (!d->roster[Data::kYesNo][0]) strcpy(d->roster[Data::kYesNo], "Yes No");
        if (!d->roster[Data::kAdd][0]) strcpy(d->roster[Data::kAdd], "Add ");     // (Pool: "Add Exit")
        if (!d->roster[Data::kModify][0]) strcpy(d->roster[Data::kModify], "Modify: ");
        if (!d->roster[Data::kKeepExit][0]) strcpy(d->roster[Data::kKeepExit], "Keep Exit");
        if (!d->roster[Data::kCantModify][0]) strcpy(d->roster[Data::kCantModify], " can't be modified.");
    }
    if (!d->w_slots[0]) strcpy(d->w_slots, "A B C D E F G H I J");
    char sub[64] = "SAVE";
    plat::File f;
    if (pp.cfg && open_file(pp.cfg, f)) {
        char cfg[256];
        const int n = f.read(reinterpret_cast<uint8_t*>(cfg), sizeof cfg);
        f.close();
        if (n > 0) {
            char got[64];
            // (Darkness's POOL4.CFG: its own save folder first, then Secret's)
            savegame::save_dir(cfg, static_cast<size_t>(n), got, sizeof got, darkness_files() ? 0 : -1);
            if (got[0]) strcpy(sub, got);
        }
    }
    games::path_of(d->data_dir, sub, d->save_dir, sizeof d->save_dir);
    // Steam / SNEG: C: is the folder above the game's ("C:\CURSE\SAVE\");
    // GOG's Pool: "C:\POOLRAD\" - the game's own folder
    if (!plat::exists(d->save_dir) && savegame::drop_own_folder(d->data_dir, sub)) {
        if (sub[0]) games::path_of(d->data_dir, sub, d->save_dir, sizeof d->save_dir);
        else snprintf(d->save_dir, sizeof d->save_dir, "%s/%s", games::kRootDir, d->data_dir);
    }
    // GOG's Secret: BLADES.CFG names C:\SAVE\, which isn't there - its
    // sample party is beside the game's files: the saves go there too
    if (!plat::exists(d->save_dir) && secret_files())
        snprintf(d->save_dir, sizeof d->save_dir, "%s/%s", games::kRootDir, d->data_dir);
}

// ---- the script host ------------------------------------------------------------

// ---- Darkness's goods (facts_shops.md 1) -------------------------------------------
// SHOP n's and TREASURE's items are rows of ITEM0.DAX block 1: 17 bytes a
// row (1-based) - an item's bytes 0x2E-0x3E; row 0xFF a random magic-user
// scroll of three spells (any spell whose class byte is 2), a row whose type
// is 0xFF none. Each new item goes at the head of the original's list (the
// engine's: appended - goods() shows the last first).

// The program's bytes at DS ds (n of them)
bool program_ds(uint16_t ds, uint8_t* out, size_t n)
{
    plat::File f;
    if (!open_file(d->prof->program, f)) return false;
    plat::FileSource src(f);
    exepack::Info info;
    const bool ok = exepack::parse(src, info) == exepack::Status::Ok &&
                    exepack::read(src, info, d->prof->create.ds_image + ds, out, n) == exepack::Status::Ok;
    f.close();
    return ok;
}

__attribute__((noinline)) void dk_add_rows(const uint8_t* rows, int n, bool identify, items::Ground& g)
{
    const auto& pi = d->prof->items;
    if (!pi.rows_file) return;
    // the spells' class bytes, only when a random scroll is wanted
    uint8_t* cls = nullptr;
    for (int k = 0; k < n; ++k)
        if (rows[k] == 0xFF && !cls) {
            uint8_t* t = new (std::nothrow) uint8_t[127 * 16];
            if (t && program_ds(pi.spell_class, t, 127 * 16 - 15)) {
                cls = new (std::nothrow) uint8_t[127];
                if (cls)
                    for (int s2 = 0; s2 < 127; ++s2) cls[s2] = t[s2 * 16];
            }
            delete[] t;
        }
    plat::File f;
    const bool open = open_dax(pi.rows_file, f);
    const dax::Entry* e = open ? d->idx.find(1) : nullptr;
    for (int k = 0; k < n; ++k) {
        uint8_t row[17] = {};
        if (rows[k] == 0xFF) {
            // (L+0x1ABE: type 0x27, words 0x66 0x27 0x28, plus 1, weight 1, value 3000)
            if (!cls) continue;
            row[0] = 0x27, row[1] = 0x66, row[2] = 0x27, row[3] = 0x28, row[4] = 1, row[9] = 1;
            row[12] = 3000 & 0xFF, row[13] = 3000 >> 8;
            for (int j = 0; j < 3; ++j) {
                int sp;
                do sp = rng.roll(126, 1);
                while (cls[sp] != 2);
                row[14 + j] = static_cast<uint8_t>(sp);
            }
        } else {
            if (!e || !rows[k]) continue;
            plat::FileSource src(f);
            dax::RleReader r(src, d->idx, *e);
            r.skip(static_cast<size_t>(rows[k] - 1) * 17);
            if (r.read(row, 17) != 17 || row[0] == 0xFF) continue;
        }
        if (g.n >= items::kMaxGround) {
            // (full: the oldest goes - the original's list shows the newest
            // first, and the engine keeps kMaxGround; its experience still counts)
            if (static_cast<int8_t>(g.item[0][0x32]) > 0) g.lost_plus += static_cast<int8_t>(g.item[0][0x32]);
            memmove(g.item[0], g.item[1], static_cast<size_t>(g.n - 1) * items::kRecordSize);
            --g.n;
        }
        uint8_t* it = g.item[g.n++];
        memset(it, 0, items::kRecordSize);
        memcpy(it + 0x2E, row, 17);
        if (identify) it[0x35] &= 0xF8;
    }
    if (open) f.close();
    delete[] cls;
}

struct Host : ecl::Host {
    bool load_script(int block, uint8_t* code, uint32_t* len) override
    {
        char name[24];
        {
            char t[40];
            snprintf(t, sizeof t, "area %d script %d", d->gs.game_area, block);
            memlog::note(t);
        }
        plat::File f;
        if (!open_area_dax("ECL", block, f, name, sizeof name)) {
            plat::log("[play] script %d of %s: missing\n", block, name);
            return false;
        }
        plat::FileSource src(f);
        bool ok = false;
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
        if (e && e->raw_size >= 2 && static_cast<uint32_t>(e->raw_size) - 2 <= d->gs.code_cap) {
            uint8_t* tmp = static_cast<uint8_t*>(malloc(e->raw_size));
            if (tmp && dax::load_block(src, d->idx, *e, tmp) == e->raw_size) {
                memcpy(code, tmp + 2, e->raw_size - 2);
                *len = e->raw_size - 2;
                ok = true;
            }
            free(tmp);
        }
        f.close();
        plat::log("[play] script %d of %s: %s\n", block, name, ok ? "loaded" : "missing");
        return ok;
    }
    void load_map(int geo_block) override
    {
        char name[24];
        plat::File f;
        d->map.loaded = false;
        if (open_area_dax("GEO", geo_block, f, name, sizeof name)) {
            plat::FileSource src(f);
            geo::load(src, d->idx, static_cast<uint8_t>(geo_block), d->map);
            f.close();
        }
        plat::log("[play] map %s #%d: %s\n", name, geo_block, d->map.loaded ? "ok" : "missing");
    }
    void load_walls(int set, int block) override
    {
        view3d::World& wd = d->world;
        if (set < 1 || set > 3) return;
        // (Secret's LOAD PIECES: 127 = nothing loaded there - the set keeps
        // what it has: a 3-part block in set 1 fills sets 2 and 3)
        if (blocks_across() && block == 0x7F) return;
        d->wall_block[set - 1] = static_cast<int16_t>(block < 0 ? -1 : block);
        d->wall_set[set - 1] = static_cast<int16_t>(block < 0 ? -1 : set);
        if (block < 0) {
            wd.walls[set - 1].loaded = false;
            wd.sets[set - 1].count = 0;
            return;
        }
        char name[24];
        int n = 0;
        plat::File f;
        if (gw_wild()) {
            // Gateway's wilderness (its wall loader, GAME.OVR 0x4712F +0x134D:
            // only the tiles while 0x4BE6 is 0): the 8X8D block into the set
            wd.walls[set - 1].loaded = false;
            view3d::free_extra(wd.sets[set - 1]);
            wd.sets[set - 1].count = 0;
            if (open_area_dax("8X8D", block, f, name, sizeof name)) {
                plat::FileSource src(f);
                view3d::load_tiles(src, d->idx, static_cast<uint8_t>(block), wd.sets[set - 1]);
                f.close();
            }
            if (!wd.sets[set - 1].count) plat::log("[play] wilderness tiles %d: block %d not loaded\n", set, block);
            return;
        }
        if (open_area_dax("WALLDEF", block, f, name, sizeof name)) {
            plat::FileSource src(f);
            if (!view3d::load_walls(src, d->idx, set, static_cast<uint8_t>(block), wd, &n)) n = 0;
            f.close();
        }
        if (n == 0) {
            plat::log("[play] wall set %d: block %d not loaded\n", set, block);
            return;
        }
        // The later parts of a multi-part block fill the next sets
        for (int k = 1; k < n && set - 1 + k < 3; ++k) d->wall_block[set - 1 + k] = d->wall_set[set - 1 + k] = -1;
        if (open_area_dax("8X8D", view3d::tiles_block(static_cast<uint8_t>(block), n, 0), f, name, sizeof name)) {
            plat::FileSource src(f);
            for (int k = 0; k < n; ++k)
                view3d::load_tiles(src, d->idx, static_cast<uint8_t>(view3d::tiles_block(static_cast<uint8_t>(block), n, k)),
                                   wd.sets[set - 1 + k], wd.per_set);
            f.close();
        }
    }
    // Darkness: LOAD FILES' look - WALLDEF1's three parts and 8X8D1's
    // tiles of that number (darkness_facts.md 7.3); kept for saving
    void load_look(int look) override
    {
        if (d->prof->variant.tr && d->gs.area1[0x3F]) {
            // Treasures' overland (game byte 0x3F): the look is its map
            // picture, OVER.DAX (o144+0xA70)
            d->tr_over = static_cast<uint8_t>(look);
            if (screen == Screen::Game && bigpic < 0) {
                anim_stop();
                pic_shown = false;
                draw_view(*cv);
            }
            return;
        }
        if (d->prof->variant.tr) {
            // Treasures: slot 0 takes the look, slots 1 and 2 let go of
            // their tiles (ov43+0xE54)
            load_slot(0, look);
            d->world.tr_block[1] = d->world.tr_block[2] = 0;
            slots_done();
            return;
        }
        view3d::World& wd = d->world;
        plat::File f;
        int n = 0;
        if (open_dax("WALLDEF1.DAX", f)) {
            plat::FileSource src(f);
            if (!view3d::load_walls(src, d->idx, 1, static_cast<uint8_t>(look), wd, &n)) n = 0;
            f.close();
        }
        bool tiles = false;
        if (open_dax("8X8D1.DAX", f)) {
            plat::FileSource src(f);
            tiles = view3d::load_vga_tiles(src, d->idx, static_cast<uint8_t>(look), wd, vga_load());
            vga_take(true);              // (a VGA screen: the walls' entries, 32-143, sent and kept)
            f.close();
        }
        d->save.look = static_cast<uint16_t>(look);
        d->save.look_slot = 1;
        plat::log("[play] look %d: %d wall sets, tiles %s\n", look, n, tiles ? "ok" : "missing");
    }
    // Treasures (op 0x42): a wall slot's block - its walls now, its tiles
    // in slots_done (the saves keep the three blocks)
    void load_slot(int slot, int block) override
    {
        if (d->gs.area1[0x3F]) {
            if (block >= 0) d->tr_over = static_cast<uint8_t>(block);     // (the overland's map: o144+0x1511)
            return;
        }
        view3d::World& wd = d->world;
        plat::File f;
        if (block >= 0 && open_dax("WALLDEF1.DAX", f)) {
            plat::FileSource src(f);
            if (!view3d::load_tr_walls(src, d->idx, slot, block, wd)) plat::log("[play] slot %d: WALLDEF1 %d missing\n", slot, block);
            f.close();
        } else if (block < 0 && slot >= 0 && slot < 3) {
            wd.tr = wd.dk = true;
            wd.tr_block[slot] = 0;          // (its tiles go; its walls stay - ov43+0xFC0)
        }
    }
    void slots_done() override
    {
        view3d::World& wd = d->world;
        view3d::tr_pool_reset(wd);
        plat::File f;
        bool ok = true;
        if (open_dax("8X8D1.DAX", f)) {
            plat::FileSource src(f);
            for (int s = 0; s < 3; ++s)
                if (wd.tr_block[s]) {
                    ok = view3d::load_tr_tiles(src, d->idx, s, wd.tr_block[s], wd, vga_load()) && ok;
                    vga_take(true);          // (a VGA screen: the walls' entries, 32-143)
                }
            f.close();
        }
        d->save.look = static_cast<uint16_t>(wd.tr_block[0] | wd.tr_block[1] << 8);
        d->save.look_slot = wd.tr_block[2];
        plat::log("[play] wall slots %d %d %d: %d tiles%s\n", wd.tr_block[0], wd.tr_block[1], wd.tr_block[2], wd.tr_used,
                  ok ? "" : " (not all loaded)");
        if (screen == Screen::Game) draw_view(*cv);
    }
    // Darkness: the frame's style (BORDERS block 0-3), the screen's frame again
    void age_party(int years) override
    {
        // (every member in the party's list, as the original's loop)
        for (int i = 0; i < pt->count; ++i) {
            party::Character& ch = pt->m[i];
            const int age = (ch.rec[0x76] | ch.rec[0x77] << 8) + years;
            ch.rec[0x76] = static_cast<uint8_t>(age);
            ch.rec[0x77] = static_cast<uint8_t>(age >> 8);
        }
        age_check = true;
        plat::log("[play] the clock: everyone %d year(s) older\n", years);
    }
    void season() override
    {
        // (the overland drawn again in the season's colours - DOSBox VGA:
        // the demo's "autumn snows" text has the snowy map beside it)
        if (vga && screen == Screen::Game && d->gs.area1[0x3F] && d->tr_over && !pic_shown) draw_tr_overland(*cv);
    }
    void style(int v) override
    {
        plat::File f;
        if (open_dax("BORDERS.DAX", f)) {
            plat::FileSource src(f);
            layout::load_vga_tiles(src, d->idx, static_cast<uint8_t>(v), d->frame_tiles, 20, 11, !vga, vga_load());
            vga_take(true);              // (a VGA screen: 16-31 sent and kept - ovr 0x6E4E)
            f.close();
        }
        if (screen == Screen::Game) {
            draw_frame(*cv);
            draw_panel(*cv);
            draw_view(*cv);
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
        const int big = d->prof->variant.bigpic_from ? d->prof->variant.bigpic_from : 0x78;
        if (head == 0xFF && id >= big && dk_overland()) {
            // Darkness's overland map: with the party's marker, the screen it is
            anim_stop();
            bigpic = -1;
            pic_shown = false;
            draw_dk_overland(c);
            return;
        }
        if (head == 0xFF && id >= big) {
            anim_stop();
            bigpic = id;
            // Big picture: the frame with a bar at row 16, the picture inside
            layout::outer(c, d->tables, d->frame_tiles);
            layout::bar(c, d->tables, d->frame_tiles, 16);
            draw_block(c, "BIGPIC", id, 8, 8);
            dirty(0, 17 * 8);
        } else if (head == 0xFF) {
            // Pool with Monsters off: no picture in the 3D areas (the view stays)
            if (!monster_pics_on() && !pool_overland()) return;
            // (Pool's wilderness: the 3D view's frame drawn over the map first)
            if (pool_overland()) layout::box(c, d->tables, d->frame_tiles, 2, 2, 14, 14);
            c.fill(24, 24, 88, 88, 0);
            anim_start(id);
            dirty_rows(2, 14);
        } else {
            anim_stop();
            if (pool_overland()) layout::box(c, d->tables, d->frame_tiles, 2, 2, 14, 14);
            c.fill(24, 24, 88, 88, 0);
            draw_block(c, "HEAD", head, 24, 24);
            draw_block(c, "BODY", id, 24, 64);
            head_shown = head;
            body_shown = id;
            dirty_rows(3, 13);
        }
        pic_shown = true;
        if (d->prof->variant.vga && !d->prof->variant.tr && bigpic < 0) {
            // (Darkness: no position line under an event's picture; Treasures
            // keeps it - DOSBox, the demo)
            c.fill(17 * 8, 15 * 8, 22 * 8, 8, 0);
            dirty_rows(15, 15);
        }
    }
    void anim_step() override
    {
        if (anim_block < 0) return;
        anim_draw(anim_frame);
        anim_frame = anim_next(anim_frame);
        anim_at = plat::now_ms();
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
    void square() override
    {
        d->gs.roof = geo::flags(d->map, d->gs.x, d->gs.y);
        d->gs.wall_ahead = static_cast<uint8_t>(geo::wall(d->map, d->gs.x, d->gs.y, d->gs.dir));
        gw_way_ahead();
    }
    void clear_text() override
    {
        text::clear(*cv, text::kTextArea);
        w.col = w.r.x0;
        w.row = w.r.y0;
        dirty_rows(17, 22);
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
        plat::File f;
        if (!open_area_dax("SPRIT", id, f, name, sizeof name)) return;
        plat::FileSource src(f);
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(id));
        if (e && d->prof->variant.vga) {
            // Darkness (darkness_facts.md 3.5): an 88 x 88 animation of 3
            // frames - 0 near, 1 middle, 2 far - over the whole view; its 0
            // and 13 swapped, 13 see-through (ov111+0x7FA)
            pic::DkAnim* a = new (std::nothrow) pic::DkAnim;
            if (a) {
                dax::RleReader r(src, d->idx, *e);
                if (pic::parse_dk_anim(r, e->raw_size, *a, vga_load())) {
                    vga_take(false, 176, 80);       // (a VGA screen: entries 176-255 only - ov111+0x920)
                    const int fr = distance < 0 ? 0 : distance >= a->frames ? a->frames - 1 : distance;
                    pic::draw_dk_anim(src, d->idx, *e, *a, fr, c, 24 + a->x, 24 + a->y, true);
                }
                delete a;
            }
        } else if (e) {
            dax::RleReader r(src, d->idx, *e);
            if (pic::parse_anim(r, e->raw_size, d->sprite, d->prof->variant.sf_pics) && distance >= 0 && distance < d->sprite.frames) {
                const pic::Header& h = d->sprite.frame[distance];
                pic::draw_anim(src, d->idx, *e, d->sprite, distance, false, c, (h.x_cell + 3) * 8, (h.y_cell + 3) * 8, 0,
                               13, 0);
            }
        }
        f.close();
        dirty_rows(3, 13);
    }
    void shop_items(int n, items::Ground& g) override
    {
        // Darkness's SHOP n: the goods DS[first + n] .. DS[last + n], rows DS[rows + k]
        const auto& pi = d->prof->items;
        uint8_t a = 0, b = 0, rows[256];
        if (pi.goods_runs) {
            // Treasures: the n-th run of rows from DS first + 2, each ended by
            // 0xFF (ov43+0x2C58)
            int k = 2, run = 1;
            if (!program_ds(pi.goods_first, rows, sizeof rows)) {
                log("SHOP: the program's goods didn't read");
                return;
            }
            while (run < n && k < 256) run += rows[k++] == 0xFF;
            int m = 0;
            while (k + m < 256 && rows[k + m] != 0xFF) ++m;
            dk_add_rows(rows + k, m, true, g);
            plat::log("[play] SHOP %d: %d items\n", n, g.n);
            return;
        }
        if (!pi.goods_rows || !program_ds(static_cast<uint16_t>(pi.goods_first + n), &a, 1) ||
            !program_ds(static_cast<uint16_t>(pi.goods_last + n), &b, 1) || b < a ||
            !program_ds(static_cast<uint16_t>(pi.goods_rows + a), rows, b - a + 1u)) {
            log("SHOP: the program's goods didn't read");
            return;
        }
        dk_add_rows(rows, b - a + 1, true, g);
        plat::log("[play] SHOP %d: %d items\n", n, g.n);
    }
    void item_row(int row, items::Ground& g) override
    {
        const uint8_t r = static_cast<uint8_t>(row);
        dk_add_rows(&r, 1, false, g);
    }
    void load_items(int block, items::Ground& g) override
    {
        char name[24];
        plat::File f;
        if (!open_area_dax("ITEM", block, f, name, sizeof name)) return;
        plat::FileSource src(f);
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
        if (e) {
            dax::RleReader r(src, d->idx, *e);
            while (g.n < items::kMaxGround && r.read(g.item[g.n], items::kRecordSize) == items::kRecordSize) ++g.n;
        }
        f.close();
        plat::log("[play] %s #%d: %d items\n", name, block, g.n);
    }
    void random_items(int n, items::Ground& g) override
    {
        // The program's rows of ready-made items (potions, the wand ...), then the dice
        const profile::Profile* p = d->prof;
        if (!p->random_items || !p->random_rows) return;
        // (the program's array of words: its first row starts at word 1)
        treasure::Rows rows{};
        plat::File f;
        bool ok = false;
        if (open_file(p->program, f)) {
            plat::FileSource src(f);
            exepack::Info info;
            ok = exepack::parse(src, info) == exepack::Status::Ok &&
                 exepack::read(src, info, p->create.ds_image + p->random_rows - 2, reinterpret_cast<uint8_t*>(rows.w),
                               sizeof rows.w) == exepack::Status::Ok;
            for (int k = 0; ok && k < treasure::kRowWords; ++k) {
                const uint8_t* b = reinterpret_cast<const uint8_t*>(&rows.w[k]);
                rows.w[k] = static_cast<uint16_t>(b[0] | b[1] << 8);       // (little-endian, as read)
            }
            // Secret: its scrolls' spells, a level's table at a time
            if (const treasure::SecretFacts* sf = p->random_items->secret)
                for (int k = 0; ok && k < 2; ++k)
                    for (int lv = 0; ok && lv < 7; ++lv)
                        ok = exepack::read(src, info,
                                           p->create.ds_image + (k ? sf->cleric_spells : sf->mu_spells) +
                                               lv * (k ? sf->cleric_stride : sf->mu_stride),
                                           rows.spells[k][lv], sizeof rows.spells[k][lv]) == exepack::Status::Ok;
            f.close();
        }
        if (!ok) {
            log("TREASURE: the program's item rows didn't read");
            return;
        }
        uint8_t last = 0;
        for (int k = 0; k < n && g.n < items::kMaxGround; ++k)
            treasure::make(g.item[g.n++], *p->random_items, rows, rng, &last);
        plat::log("[play] TREASURE: %d random items (%d on the ground)\n", n, g.n);
    }
    void load_monster(int id, int copies, int icon) override { fight_load_monster(id, copies, icon); }
    void clear_monsters() override { fight_clear_monsters(); }
    uint8_t* monster_record(int k) override { return fight_monster_record(k); }
    bool add_npc(int id) override { return npc_join(id); }
    void party_changed() override
    {
        for (int i = 0; i < pt->count; ++i) rules::recalc(pt->m[i], *names, d->facts);
        if (cv && screen == Screen::Game && !bigpic_shown()) draw_party(*cv, 17);
    }
    void party_killed() override { party_dead = true; }
    void sound(int id) override { sfx(id); }
    void game_sound(int v) override { sfx(v, true); }
    void log(const char* what) override { plat::log("[ecl %d:%04X] %s\n", d->gs.script, vm->pc() + 0x8000, what); }
};

// ---- running scripts -------------------------------------------------------------

void host_picture(int id, int head) { host->picture(id, head); }

void draw_input(pic::Canvas& c)
{
    clear_menu_line(c);
    // (Darkness's name input starts a column after its prompt - DOSBox, its
    // creation's "Character name: ")
    // (Gateway's too, the name white, its cursor the font's glyph 0 - an
    // underline - in white: DOSBox)
    const bool name_after = engine_ask == EngineAsk::Name && screen == Screen::CreateName && (darkness_files() || gateway());
    const int x = input_engine ? static_cast<int>(strlen(input_prompt)) + (name_after ? 1 : 0) : 0;
    // (Darkness's coin questions: the prompt green, the number white - DOSBox)
    // (Gateway's View's coin question: the prompt green too - DOSBox)
    const bool dk = input_engine && (darkness_files() || (gateway() && take_mode)) && engine_ask == EngineAsk::Coins;
    if (input_engine) put(c, input_prompt, 0, text::kMenuRow, dk ? 10 : 13);
    // (Darkness's typed name is white - DOSBox)
    put(c, input_buf, x, text::kMenuRow,
        dk ? 15 : input_engine ? (name_after ? 15 : 13) : 10);
    if (x + input_len < 40) {
        if (gateway()) font::draw_glyph(c, d->font, 0, (x + input_len) * 8, text::kMenuRow * 8, 15, 0);
        else c.fill((x + input_len) * 8, text::kMenuRow * 8, 8, 8, 15);     // the cursor
    }
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
        text::begin(w, c, vm->text(), script_area(), script_text_colour(), vm->clear());
        if (d->prof->variant.tr) {
            // Treasures: its colour codes ("%" + a hex digit); the last one
            // stays the scripts' colour (game byte 0x38 - img 0x6765)
            w.codes = true;
            const char* t = vm->text();
            for (int k = 0; t[k] && t[k + 1]; ++k)
                if (t[k] == '%' && isxdigit(static_cast<unsigned char>(t[k + 1])) && !islower(static_cast<unsigned char>(t[k + 1])))
                    d->gs.area1[0x38] = static_cast<uint8_t>(isdigit(static_cast<unsigned char>(t[k + 1])) ? t[k + 1] - '0' : t[k + 1] - 'A' + 10);
        }
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
            const int shown = font::kCols;      // (the 40 columns shown)
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
        text::begin(w, c, vm->prompt(), script_area(), 10, true);
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
        pause_until = plat::now_ms() + vm->pause_ms();
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
    case ecl::Wait::Luck:               // Treasures' PROGRAM 3: the news
        lk_start(c, luck_news_block(), false, LuckRun::Then::Vm);
        break;
    case ecl::Wait::ShipRest:           // Treasures' PROGRAM 4: healing on the way
        ship_rest(c);
        break;
    case ecl::Wait::Key:                // "press <enter>/<return> to continue"
        journal_ready();                // (Darkness: JOURNAL n's sentence waits for a key)
        if (gateway() && *vm->prompt()) {
            // Gateway's CALL 0x809: "Press Enter to continue." as its one-word
            // menu, lit (DOSBox)
            text::build(menu, "", vm->prompt());
            menu.selected = 0;
            show_menu_line(c);
            break;
        }
        clear_menu_line(c);
        put(c, d->press_key, 0, text::kMenuRow, 15);
        break;
    case ecl::Wait::Shop:
        temple = false;
        open_shop(c);
        break;
    case ecl::Wait::Temple:
        temple = true;
        if (darkness_files())               // (its temple: no coins on the counter)
            for (int& m : ground->money) m = 0;
        open_shop(c);
        break;
    case ecl::Wait::Combat:
        fight_start(c);
        break;
    case ecl::Wait::Treasure:
        fight_treasure_only(c);
        break;
    case ecl::Wait::Vault:              // Secret's vault (Deposit / Withdraw)
        temple = false;
        open_vault(c);
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
    if (!gateway()) moving = false;         // (a script: Secret's move mode ends; Gateway's stays - DOSBox)
    dk_explore_fresh = true;
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
    gw_way_ahead();
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
// Pool's wilderness step (its ovr15 routine): a square that way unless
// it's sea, coast or the stinking river ("The river's stench drives you
// back.", until the land is healed); 12 hours a step
void ov_step()
{
    const overland::Facts& of = *d->prof->overland;
    int x = vm->get(0x4BC3) & 0xFF, y = vm->get(0x4BC4) & 0xFF;
    const overland::Step st = overland::step(*d->ov, of, ov_area(), &x, &y, step_dir, vm->get(of.healed) != 0);
    if (st == overland::Step::River) {
        char w[48];
        OvrWords src;
        if (src.open() && text::read_pascal(src, of.river_words, w, sizeof w)) note(*cv, w);
        return;
    }
    if (st != overland::Step::Moved) return;
    vm->set(0x4BC3, static_cast<uint16_t>(x));
    vm->set(0x4BC4, static_cast<uint16_t>(y));
    vm->advance_clock(3, 12);                   // (slot 3, the hours: 12)
}

bool do_move()
{
    ecl::GameState& g = d->gs;
    vm->set(0x4BF0, static_cast<uint16_t>(g.x));
    vm->set(0x4BF1, static_cast<uint16_t>(g.y));
    if (dk_overland()) {
        // Darkness's overland (overlay 0x68 +0x214): the square before the
        // step in 0x17 / 0x18, eight directions (DS 0x3EAC / 0x3EB5), held
        // to 0-37 / 0-14, twelve hours a step (12 x clock slot 3)
        static const int8_t kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        const int x = vm->get(0x25), y = vm->get(0x26);
        vm->set(0x17, static_cast<uint16_t>(x));
        vm->set(0x18, static_cast<uint16_t>(y));
        if (vm->get(0x7EC9) < 0xFF) {
            int nx = x + kDx[step_dir & 7], ny = y + kDy[step_dir & 7];
            nx = nx < 0 ? 0 : nx > 37 ? 37 : nx;
            ny = ny < 0 ? 0 : ny > 14 ? 14 : ny;
            vm->set(0x25, static_cast<uint16_t>(nx));
            vm->set(0x26, static_cast<uint16_t>(ny));
            vm->advance_clock(3, 12);
        }
        vm->set(0x7EC9, 0);
        after_move_redraw();
        return true;
    }
    if (pool_overland()) {
        if (vm->get(0x7EC9) < 0xFF && ov_load()) ov_step();
        vm->set(0x7EC9, 0);
        after_move_redraw();
        return true;
    }
    bool locked = false;
    if (vm->get(0x7EC9) < 0xFF && d->map.loaded) {
        const int p = way(g.x, g.y, step_dir);
        locked = p >= 2;       // locked or barred: the party would need to bash or pick it
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
    if (!vm->get(0x4BE6) && !pool_overland() && !dk_overland() && !gw_wild()) {
        // Outdoors the scripts run the show: go round again, unless nothing
        // happened (a script with nothing to do would spin forever)
        if (++idle_cycles < 3) {
            run_entry(0, Then::Move);
            return;
        }
        plat::log("[play] outdoor scripts idle; waiting for the player\n");
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
        vm->set(0x7ED5, d->map.loaded && off && !pool_overland() && !dk_overland() &&
                            way(g.x, g.y, dir_of_step) != 0
                            ? 1 : 0);
    }
    // The original's exploring menu clears 0x7EC9 (field_592) before every
    // key: a "don't move" left by an earlier script (a block's first run)
    // doesn't hold back this step - only this step's script can
    vm->set(0x7EC9, 0);
    gw_way_ahead();
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
            hl_text(c, vm->item(i), 1, row);
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
    camping = false;
    const int last = vm->get(0x4BF2) & 0xFF;
    const bool resume = d->loaded && last != 0;
    const bool dungeon = !d->loaded || vm->get(0x4BE6) != 0;
    gs.script = static_cast<uint8_t>(resume ? last : d->prof->start_script);
    screen = Screen::Game;
    if (!host->load_script(gs.script, ecl::code_of(gs), &gs.code_len) || !vm->init_script(resume)) {
        snprintf(msg, sizeof msg, "Script %d of ECL%d.DAX didn't load.", gs.script, gs.game_area);
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        pm_prompt(c, msg);
        return;
    }
    tr_position = false;         // (Treasures: none under the start's pictures - DOSBox)
    // (Gateway's wilderness: saved there, its 0x4BE6 = 0 stays - the map and
    // tile sets load as the original's, and the view is the map)
    if (resume && !dungeon && d->prof->variant.tile_wild) vm->set(0x4BE6, 0);
    draw_frame(c);
    if (resume) {
        if (dungeon && pool_files()) {
            // Pool's saves keep no wall sets: what the script loads (its
            // first LOAD FILES / LOAD PIECES, as the walk test finds them)
            uint8_t* blk = static_cast<uint8_t*>(malloc(gs.code_len + 2));
            if (blk) {
                blk[0] = blk[1] = 0;
                memcpy(blk + 2, ecl::code_of(gs), gs.code_len);
                const ecl::MapLoad ml = ecl::find_map_load(blk, gs.code_len + 2, *d->prof->ecl_ops);
                free(blk);
                if (ml.has_geo()) host->load_map(ml.geo);
                for (int i = 0; i < 3; ++i)
                    if (ml.walls[i] != ecl::kNone) host->load_walls(i + 1, ml.walls[i]);
            }
        } else if (dungeon && d->prof->variant.tr) {
            // (Treasures: its map and the three slots' blocks - the save's
            // bytes 0x407-0x409, ovl 0x7C +0x2877)
            host->load_map(vm->get(0x4BC5) & 0xFF);
            const int blk[3] = {d->save.look & 0xFF, d->save.look >> 8, d->save.look_slot & 0xFF};
            for (int s2 = 0; s2 < 3; ++s2) host->load_slot(s2, blk[s2] ? blk[s2] : -1);
            host->slots_done();
        } else if (dungeon && darkness_files()) {
            // (Darkness: its map and its look again - darkness_facts.md S8)
            host->load_map(vm->get(0x4BC5) & 0xFF);
            if (d->save.look) host->load_look(d->save.look);
        } else if (dungeon || gw_wild()) {
            // (Gateway's wilderness too: its region's map and tile sets)
            if (d->save.wall_block[0] > 0) host->load_map(vm->get(0x4BC5) & 0xFF);
            for (int i = 0; i < 3; ++i)
                if (d->save.wall_block[i] > 0) host->load_walls(d->save.wall_set[i], d->save.wall_block[i]);
        } else if (!d->prof->overland) {
            host->picture(0x79, 0xFF);          // (Curse's wilderness map; Pool's is drawn as the view)
        }
    }
    d->gs.roof = geo::flags(d->map, gs.x, gs.y);
    draw_view(c);
    draw_panel(c);
    run_entry(4, Then::Begun);
}

// ---- The Gold Box Companion's icons (the player's copy on the card; an
// engine comfort with the icon gallery): picks by name in _CYD/<folder>/ICONS.TXT
void load_icon_picks()
{
    d->icon_picks = gbc_icons::Choices{};
    if (!d->cache_dir[0]) return;
    char path[200];
    snprintf(path, sizeof path, "%s/ICONS.TXT", d->cache_dir);
    plat::File f = plat::open(path, "r");
    if (!f) return;
    char buf[700];
    const int n = f.read(reinterpret_cast<uint8_t*>(buf), sizeof buf);
    f.close();
    if (n > 0) gbc_icons::parse(buf, static_cast<size_t>(n), d->icon_picks);
}

void save_icon_picks()
{
    if (!d->cache_dir[0]) return;
    char buf[700];
    const size_t n = gbc_icons::write(d->icon_picks, buf, sizeof buf);
    write_file(d->cache_dir, "ICONS.TXT", reinterpret_cast<const uint8_t*>(buf), n);
}

// Companion icon file k (1..) into 4-bit rows (malloc'd), or nullptr
uint8_t* load_gbc_icon(int k)
{
    char path[96];
    snprintf(path, sizeof path, "%s/%s/%03d.bmp", games::kRootDir, gbc_icons::kFolder, k);
    plat::File f = plat::open(path, "r");
    if (!f) return nullptr;
    uint8_t* p = static_cast<uint8_t*>(malloc(gbc_icons::kPicBytes));
    plat::FileSource src(f);
    if (p && !gbc_icons::bmp_to_pic4(src, p)) {
        free(p);
        p = nullptr;
    }
    f.close();
    if (p && vga) {
        // (a VGA game in 256 colours: its pictures a byte a pixel - the
        // Companion's EGA colours are the palette's first 16)
        uint8_t* q = static_cast<uint8_t*>(malloc(gbc_icons::kPicBytes * 2));
        if (q)
            for (int i = 0; i < gbc_icons::kPicBytes; ++i) {
                q[2 * i] = p[i] >> 4;
                q[2 * i + 1] = p[i] & 15;
            }
        free(p);
        p = q;
    }
    return p;
}

// How many icon pairs the Companion has on the card (looked once)
int gbc_pairs()
{
    if (d->gbc_pairs >= 0) return d->gbc_pairs;
    int n = 0;
    for (; n < gbc_icons::kMaxPairs; ++n) {
        char path[96];
        snprintf(path, sizeof path, "%s/%s/%03d.bmp", games::kRootDir, gbc_icons::kFolder, 2 * n + 2);
        if (!plat::exists(path)) break;
    }
    d->gbc_pairs = n;
    return n;
}

void rec_name(const uint8_t* rec, char* out)
{
    size_t n = rec[0] > 15 ? 15 : rec[0];
    memcpy(out, rec + 1, n);
    while (n > 0 && out[n - 1] == ' ') --n;
    out[n] = 0;
}

#include "play_fight.inc"

// ---- View Character's portrait box ---------------------------------------------
// Pool of Radiance draws the character's portrait (HEAD3 / BODY3 pictures)
// in a box top right - from the party menu, with its portraits on
// (poolrad_create_facts.md 15; its View: DS 0x49F5 and 0x5376 = 3). The
// engine comfort CYD_ICON_PORTRAIT (Tom, v0.84.0) fills the same space
// with the combat icon three times its size, in a frame of the game's own
// tiles; where a portrait exists it comes first, a tap swaps the two.

// Pool's portrait for this character can be shown (the original: from the
// party menu only; with the comfort, wherever View is)
bool view_has_portrait()
{
    if (!d->prof->create.head_blocks || !d->prof->create.body_blocks || !portraits_on()) return false;
    return (CYD_ICON_PORTRAIT && opts.portrait) || view_from == Screen::PartyMenu;
}

// The box's place: Pool's (its box shares the screen's border), else
// inside the border with room for the text beside it
void view_box_place(int* col, int* row)
{
    const bool pool = d->tables.box[0] != 0;
    *col = pool ? 27 : 26;
    *row = pool ? 0 : 1;
}

void view_box(pic::Canvas& c, const party::Character& ch)
{
    const bool icon_on = CYD_ICON_PORTRAIT && opts.portrait;
    const bool portrait = view_has_portrait();
    if (!icon_on && !portrait) return;
    int col, row;
    view_box_place(&col, &row);
    const int x0 = (col + 1) * 8, y0 = (row + 1) * 8;
    c.fill(x0, y0, 88, 88, 0);
    layout::picture_frame(c, d->tables, d->frame_tiles, col, row);
    dirty_rows(row, row + 12);
    if (portrait && !(icon_on && d->view_icon)) {
        if (!d->maps_read) {
            d->maps_read = true;
            const auto& pc = d->prof->create;
            plat::File f;
            if (open_file(d->prof->program, f)) {
                plat::FileSource src(f);
                exepack::Info info;
                if (exepack::parse(src, info) != exepack::Status::Ok ||
                    exepack::read(src, info, pc.ds_image + pc.head_blocks, d->head_map, sizeof d->head_map) !=
                        exepack::Status::Ok ||
                    exepack::read(src, info, pc.ds_image + pc.body_blocks, d->body_map, sizeof d->body_map) !=
                        exepack::Status::Ok)
                    memset(d->head_map, 0, sizeof d->head_map);
                f.close();
            }
        }
        int head = ch.rec[0x13F], body = ch.rec[0x140];
        if (head < 1 || head > 14) head = 1;
        if (body < 1 || body > 12) body = 1;
        const int keep = d->gs.game_area;
        d->gs.game_area = 3;                    // (HEAD3 / BODY3, as the program)
        draw_block(c, "HEAD", d->head_map[head], x0, y0);
        draw_block(c, "BODY", d->body_map[body], x0, y0 + 40);
        d->gs.game_area = static_cast<uint8_t>(keep);
        return;
    }
    // The combat icon, 3 x 3 a pixel (72 x 72) in the middle; an NPC from
    // the monsters' files: its own picture (as in fights)
    Pic4 ic;
    if (ch.rec[0xF7] >= 0x80 && ch.rec[0x126] && !ch.rec[0x141] && !ch.rec[0x142]) {
        char file[16];
        area_fmt(file, sizeof file, "CPIC%d.DAX");
        for (int fr = 0; fr < 2; ++fr) {
            int w = 0, h = 0;
            ic.px[fr] = load_pic4(file, ch.rec[0x126] + fr * 0x80, 0, &w, &h);
            if (ic.px[fr]) ic.w = static_cast<uint8_t>(w), ic.h = static_cast<uint8_t>(h);
        }
    }
    if (!ic.px[0]) build_icon(ch.rec, ic);
    const uint8_t* p = ic.px[d->view_action && ic.px[1] ? 1 : 0];
    if (p && ic.w && ic.h) {
        const int sc = ic.w > 24 || ic.h > 24 ? 1 : 3;     // (a 48-pixel monster: as it is)
        const int ox = x0 + (88 - ic.w * sc) / 2, oy = y0 + (88 - ic.h * sc) / 2;
        for (int j = 0; j < ic.h; ++j)
            for (int i = 0; i < ic.w; ++i) {
                const int v = nib(p, ic.w, i, j);
                if (v) c.fill(ox + i * sc, oy + j * sc, sc, sc, static_cast<uint8_t>(v));
            }
    }
    free_pic(ic);
}

// A tap on the box: portrait <-> icon, or the icon's ready <-> action pose
bool view_box_tap(int x, int y, pic::Canvas& c)
{
    const bool icon_on = CYD_ICON_PORTRAIT && opts.portrait;
    const party::Character* ch = new_char ? new_char : pt->sel();
    if (!ch || (!icon_on && !view_has_portrait())) return false;
    int col, row;
    view_box_place(&col, &row);
    if (x < col * 8 || x >= (col + 13) * 8 || y < row * 8 || y >= (row + 13) * 8) return false;
    if (!icon_on) return true;                  // (Pool's portrait alone: nothing to swap)
    if (view_has_portrait() && !d->view_icon) d->view_icon = true, d->view_action = false;
    else if (view_has_portrait() && d->view_action) d->view_icon = false;
    else d->view_action = !d->view_action;
    view_box(c, *ch);
    return true;
}

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
    bool     picks_changed = false;     // a Gold Box Companion icon picked / dropped (ICONS.TXT to write)
    Pic4     old_ic, new_ic, base[2];
    char     menus[5][42] = {};
    char     words[profile::kIconWords][20] = {};
#if CYD_ICON_GALLERY
    int      gpage = -1;                // the gallery's page shown (icon_looks::Page), -1 the editor
    int      glast = 0;                 // the page it was on (back to it after No)
    int      gsel = -1;                 // the choice picked
    int      gfirst = 0;                // the first choice on the page shown
    int      gsize = 0;                 // the size the parts below are for
    uint8_t* ghead[icon_looks::kHeads] = {};    // the parts' ready pictures, 24 x 24 (loaded as needed)
    uint8_t* gbody[icon_looks::kBodies] = {};
#endif
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
    const bool lists = d->prof->alter.icon_lists;
    const int at = menu.selected;
    switch (ie->level) {
    case 1: text::build(menu, "", ie->menus[0]); break;
    case 2: text::build(menu, "", ie->menus[1]); break;
    case 3: {
        strlcpy(t, ie->menus[2], sizeof t);
        if (lists) {
            drop_word(t, ie->second ? 2 : 3);       // (Darkness: Hair for the 1st colour, Face for the 2nd)
        } else {
            const char* hf = ie->words[ie->second ? profile::kIconFace : profile::kIconHair];
            char* x = strstr(t, "xxxx");
            if (x && strlen(hf) == 4) memcpy(x, hf, 4);
        }
        text::build(menu, "", t);
        break;
    }
    case 4:
        if (lists) {
            strlcpy(t, ie->menus[3], sizeof t);     // (Darkness: "Small Large Keep Exit" without the size it has)
            drop_word(t, ie->ch->rec[0x144] == 2 ? 1 : 0);
        } else {
            snprintf(t, sizeof t, "%s%s", ie->words[ie->ch->rec[0x144] == 2 ? profile::kIconSmall : profile::kIconLarge],
                     ie->menus[3]);
        }
        text::build(menu, "", t);
        break;
    case 5: text::build(menu, "", ie->menus[4]); break;
    default: text::build(menu, ie->words[profile::kIconOk], rw(Data::kYesNo)); break;
    }
    // The question starts on No, as Curse / Pool / Secret do; Darkness's on
    // Yes (GAME.OVR 0x18465), and its menus keep the highlight's place
    if (kept_hl()) {
        if (ie->level == 6) menu.selected = 0;
        else keep_place(at);
    } else {
        // (Gateway's on Yes too: its question routine - DOSBox)
        menu.selected = ie->level == 6 && !gateway() ? 1 : 0;
    }
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

// The editor's screen: the frame, the words, the old pair (new: icon_new)
void icon_screen(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    put(c, ie->words[profile::kIconOld], 8, 6, 15);
    put(c, ie->words[profile::kIconReadyAction], 3, 10, 15);
    put(c, ie->words[profile::kIconNew], 8, 12, 15);
    put(c, ie->words[profile::kIconReadyAction], 3, 16, 15);
    icon_pair(c, ie->old_ic, 56);
    dirty(0, pic::kScreenH);
}

#if CYD_ICON_GALLERY
void gal_open(pic::Canvas& c, int page);
#endif

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
    plat::File f;
    if (open_file(d->prof->program, f)) {
        plat::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok)
            for (int i = 0; i < 5; ++i)
                if (pa.icon_menu[i]) text::read_pascal(src, info, pa.icon_menu[i], ie->menus[i], sizeof ie->menus[i]);
        f.close();
    }
    for (int i = 0; i < profile::kIconWords; ++i) ow(pa.icon_words[i], ie->words[i], sizeof ie->words[i]);
    // The grey squares (Darkness: COMSPR 15's first picture under both -
    // GAME.OVR 0x17E2D loads it, 0x17B42 draws it)
    const int box = pa.icon_box ? pa.icon_box : 25;
    for (int fr = 0; fr < 2; ++fr) {
        int w = 0, h = 0;
        ie->base[fr].px[0] = load_pic4("COMSPR.DAX", box + (pa.icon_box ? 0 : fr * 0x80), 0, &w, &h);
        ie->base[fr].w = static_cast<uint8_t>(w);
        ie->base[fr].h = static_cast<uint8_t>(h);
    }
    screen = Screen::Icon;
    anim_stop();
    pic_shown = false;
    palette_fight(true, true);
#if CYD_ICON_GALLERY
    if (opts.gallery) {
        icon_take(ie->keep, ch->rec);
        build_icon(ch->rec, ie->old_ic);
        gal_open(c, 0);
        return;
    }
#endif
    // (Darkness: from Alter its menu's place stays lit - menu.selected - and
    // from creation the last Yes / No's)
    if (from == Screen::CreateName) menu.selected = yes_no_place;
    icon_screen(c);
    icon_pass(c);
}

void end_icon_art()
{
    if (!ie) return;
    free_pic(ie->old_ic);
    free_pic(ie->new_ic);
    free_pic(ie->base[0]);
    free_pic(ie->base[1]);
#if CYD_ICON_GALLERY
    for (uint8_t*& p : ie->ghead) free(p), p = nullptr;
    for (uint8_t*& p : ie->gbody) free(p), p = nullptr;
#endif
    delete ie;
    ie = nullptr;
}

void create_after_icon(pic::Canvas& c);

void end_icon(pic::Canvas& c)
{
    const Screen from = ie->from;
    if (ie->picks_changed) save_icon_picks();
    end_icon_art();
    palette_fight(false);
    if (from == Screen::CreateName) {
        create_after_icon(c);
        return;
    }
    if (from == Screen::PartyMenu) {
        // (Gateway's party menu Icon Select: back to the party menu)
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return;
    }
    draw_camp(c);
    if (darkness_files()) {
        // (its camp screen again - GAME.OVR 0x36D6F - with the text window empty: DOSBox)
        text::clear(c, text::kTextArea);
        dirty_rows(17, 22);
    }
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
#if CYD_ICON_GALLERY
            if (opts.gallery) {
                gal_open(c, ie->glast);         // (the gallery is the editor's top: Done asks)
                return;
            }
#endif
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
#if CYD_ICON_GALLERY
        else if (k == 'N' && opts.gallery) gal_open(c, ie->glast);
#endif
        else if (k == 'N') icon_pass(c);
        return;
    }
}

#if CYD_ICON_GALLERY
void gal_tap(int x, int y, pic::Canvas& c);
#endif

void icon_tap(int x, int y, pic::Canvas& c)
{
#if CYD_ICON_GALLERY
    if (ie && ie->gpage >= 0) {
        gal_tap(x, y, c);
        return;
    }
#endif
    if (!ie || y < text::kMenuTapTop) return;
    const int k = text::hit(menu, x / 8);
    if (k < 0) return;
    menu.selected = k;
    show_menu_line(c);
    icon_key(text::key(menu, k), c);
}

#if CYD_ICON_GALLERY
void gal_key(char k, pic::Canvas& c);
#endif

// Esc: as Exit at the level shown (No at the question); the gallery's Done
void icon_back(pic::Canvas& c)
{
#if CYD_ICON_GALLERY
    if (ie->gpage >= 0) {
        gal_key('D', c);
        return;
    }
#endif
    icon_key(ie->level == 6 ? 'N' : 'E', c);
}

#if CYD_ICON_GALLERY
// ---- the icon gallery (engine comfort, features.h CYD_ICON_GALLERY) ----------------
// Pages of cells, each the character's icon with one choice in place: the
// engine's whole icons, every head, every body (weapon), the colour
// schemes (engine/icon_looks.*). A tap (or Up / Down) puts that choice on
// the character at once; Edit goes to the original editor, Done to its
// "Is this icon ok?" (No: back here). The original editor's Exit comes
// back here too - the gallery is the editor's top.
// Tom (v0.71.1, on a 2.8"): the 24 px icons are too small to pick from, so
// each cell shows its icon twice the size, 4 x 3 to a page, with big Prev /
// Next boxes and "1 of 3" to the right of them.
constexpr int kGalCols = 4, kGalRows = 3, kGalPer = kGalCols * kGalRows;
constexpr int kGalX = 12, kGalY = 18, kGalPitchX = 58, kGalPitchY = 55, kGalBox = 2 * kSq + 6;
constexpr int kGalNavX = kGalX + kGalCols * kGalPitchX + 2, kGalNavW = 308 - kGalNavX;

icon_looks::Page gal_page() { return static_cast<icon_looks::Page>(ie->gpage); }
// The page's choices: Icons also has the Companion's icons after the
// engine's own (the player's copy of the Companion on the card)
int gal_count()
{
    return icon_looks::count(gal_page()) + (gal_page() == icon_looks::Page::Looks ? gbc_pairs() : 0);
}
bool gal_gbc(int i) { return gal_page() == icon_looks::Page::Looks && i >= icon_looks::kLooks; }
int gal_pick_now()           // the character's Companion pick (pair from 1), 0 none
{
    char nm[16];
    rec_name(ie->ch->rec, nm);
    return gbc_icons::find(d->icon_picks, nm);
}
void gal_set_pick(int pair)
{
    char nm[16];
    rec_name(ie->ch->rec, nm);
    if (gbc_icons::find(d->icon_picks, nm) == pair) return;
    gbc_icons::set(d->icon_picks, nm, pair);
    ie->picks_changed = true;
}
int gal_pages() { return (gal_count() + kGalPer - 1) / kGalPer; }

// The cell (a choice's index) under canvas (x, y), or -1
int gal_cell_at(int x, int y)
{
    if (!ie || ie->gpage < 0 || x < kGalX || y < kGalY) return -1;
    const int col = (x - kGalX) / kGalPitchX, row = (y - kGalY) / kGalPitchY;
    if (col >= kGalCols || row >= kGalRows) return -1;
    const int i = ie->gfirst + row * kGalCols + col;
    return i < gal_count() ? i : -1;
}

// The square around choice i's icon (its highlight); i on the page shown
void gal_box(int i, int* x, int* y, int* w, int* h)
{
    const int k = i - ie->gfirst;
    *x = kGalX + (k % kGalCols) * kGalPitchX;
    *y = kGalY + (k / kGalCols) * kGalPitchY;
    *w = *h = kGalBox;
}

// The Prev (0) / Next (1) boxes
void gal_nav_box(int which, int* x, int* y, int* w, int* h)
{
    *x = kGalNavX;
    *w = kGalNavW;
    *h = kGalBox;
    *y = which ? kGalY + (kGalRows - 1) * kGalPitchY : kGalY;
}

// Prev 0 / Next 1 under canvas (x, y), or -1 (none when one page)
int gal_nav_at(int x, int y)
{
    if (!ie || ie->gpage < 0 || gal_pages() < 2) return -1;
    for (int k = 0; k < 2; ++k) {
        int bx, by, bw, bh;
        gal_nav_box(k, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) return k;
    }
    return -1;
}

void gal_outline(pic::Canvas& c, int x, int y, int w, int h, uint8_t colour)
{
    c.fill(x, y, w, 2, colour);
    c.fill(x, y + h - 2, w, 2, colour);
    c.fill(x, y, 2, h, colour);
    c.fill(x + w - 2, y, 2, h, colour);
    dirty(y, y + h);
}

void gal_frame(pic::Canvas& c, int i, uint8_t colour)
{
    if (i < ie->gfirst || i >= ie->gfirst + kGalPer) return;     // (not on this page)
    int x, y, w, h;
    gal_box(i, &x, &y, &w, &h);
    gal_outline(c, x, y, w, h, colour);
}

// The parts' ready pictures for the character's size, read once (each
// file opened once); heads padded to 24 rows
// A gallery part's bytes: 24 x 24 4-bit rows (Darkness: its see-through
// plane after them)
uint32_t gal_bytes() { return tile_bytes() + (fight_masks() ? mask_bytes(kSq, kSq) : 0); }

bool gal_load(uint8_t** parts, int n, const char* file, int add)
{
    bool need = false;
    for (int i = 0; i < n; ++i) need |= parts[i] == nullptr;
    if (!need) return true;
    plat::File f;
    if (!open_dax(file, f)) return false;
    plat::FileSource src(f);
    bool ok = true;
    for (int i = 0; i < n && ok; ++i) {
        if (parts[i]) continue;
        const dax::Entry* e = d->idx.find(static_cast<uint8_t>(i + add));
        if (!e) continue;                   // a missing part: its cell stays empty
        dax::RleReader r(src, d->idx, *e);
        if (fight_masks()) {
            // Darkness: VGA parts in their EGA colours with the see-through
            // plane (load_vga4), made 24 rows - the rows past a short part
            // see-through - with the plane after them (gal_bytes)
            int w = 0, h = 0;
            uint8_t* v = load_vga4(r, e->raw_size, 0, &w, &h);
            if (!v || w != kSq || h > kSq) {
                free(v);
                continue;
            }
            uint8_t* p = static_cast<uint8_t*>(malloc(gal_bytes()));
            if (!p) {
                free(v);
                ok = false;
                break;
            }
            memset(p, 0, tile_bytes());
            memcpy(p, v, static_cast<size_t>(rowb(kSq)) * h);
            uint8_t* m = p + tile_bytes();
            memset(m, 0xFF, gal_bytes() - tile_bytes());
            const uint8_t* vm = v + rowb(kSq) * h;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < kSq; ++x)
                    if (!masked(vm, kSq, x, y)) {
                        const int k = y * kSq + x;
                        m[k >> 3] = static_cast<uint8_t>(m[k >> 3] & ~(1 << (k & 7)));
                    }
            free(v);
            parts[i] = p;
            continue;
        }
        uint8_t hdr[pic::kHeaderSize];
        pic::Header ph;
        if (r.read(hdr, sizeof hdr) != sizeof hdr || !pic::parse_header(hdr, e->raw_size, ph) || ph.width_px() != kSq ||
            ph.height > kSq || !ph.frames)
            continue;
        uint8_t* p = static_cast<uint8_t*>(malloc(kTileBytes));
        if (!p) {
            ok = false;
            break;
        }
        memset(p, 0, kTileBytes);
        const uint32_t fb = ph.frame_bytes();
        if (fb > kTileBytes || !pic::read_frame(r, ph, 0, p)) {
            free(p);
            continue;
        }
        parts[i] = p;
    }
    f.close();
    return ok;
}

bool gal_parts()
{
    const int size = ie->ch->rec[0x144];
    if (size != ie->gsize) {
        for (uint8_t*& p : ie->ghead) free(p), p = nullptr;
        for (uint8_t*& p : ie->gbody) free(p), p = nullptr;
        ie->gsize = size;
    }
    const int add = size == 1 ? 0 : 0x40;
    return gal_load(ie->ghead, icon_looks::kHeads, "CHEAD.DAX", add) &&
           gal_load(ie->gbody, icon_looks::kBodies, "CBODY.DAX", add);
}

// A 24 x 24 picture at twice its size, colour 0 see-through
// (mk: Darkness's see-through plane - colour 0 drawn; none: colour 0 see-through)
void blit4_x2(pic::Canvas& c, const uint8_t* p, int w, int h, int x, int y, const uint8_t* mk = nullptr)
{
    if (!p) return;
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const int v = nib(p, w, i, j);
            if (mk ? !masked(mk, w, i, j) : v != 0) c.fill(x + 2 * i, y + 2 * j, 2, 2, static_cast<uint8_t>(v));
        }
}

void gal_cell(pic::Canvas& c, int i)
{
    if (gal_gbc(i)) {
        // A Companion icon (its ready pose)
        int x, y, w, h;
        gal_box(i, &x, &y, &w, &h);
        blit4_x2(c, ie->base[0].px[0], ie->base[0].w, ie->base[0].h, x + 3, y + 3,
                 mask_of(ie->base[0].px[0], ie->base[0].w, ie->base[0].h));
        uint8_t* p = load_gbc_icon(2 * (i - icon_looks::kLooks) + 1);
        blit4_x2(c, p, kSq, kSq, x + 3, y + 3);
        free(p);
        return;
    }
    uint8_t head, body, colours[6];
    icon_looks::preview(gal_page(), i, ie->ch->rec, &head, &body, colours);
    int x, y, w, h;
    gal_box(i, &x, &y, &w, &h);
    x += 3;
    y += 3;
    blit4_x2(c, ie->base[0].px[0], ie->base[0].w, ie->base[0].h, x, y,
             mask_of(ie->base[0].px[0], ie->base[0].w, ie->base[0].h));
    if (body >= icon_looks::kBodies || !ie->gbody[body]) return;
    uint8_t px[kSq * kSq + kSq * kSq / 8];      // (a byte a pixel at most: a VGA game in 256 colours)
    memcpy(px, ie->gbody[body], gal_bytes());
    icon_compose(px, head < icon_looks::kHeads ? ie->ghead[head] : nullptr, kSq, colours);
    blit4_x2(c, px, kSq, kSq, x, y, mask_of(px, kSq, kSq));
}

void gal_menu(pic::Canvas& c)
{
    text::build(menu, "", "Icons Heads Weapons Colors Edit Done");
    static const char kKeys[icon_looks::kPages] = {'I', 'H', 'W', 'C'};
    menu.selected = 0;
    for (int k = 0; k < menu.count; ++k)
        if (text::key(menu, k) == kKeys[ie->gpage]) menu.selected = k;
    show_menu_line(c);
}

// The page holding choice `first` (rounded down to a page's start)
void gal_draw_page(pic::Canvas& c, int first)
{
    ie->gfirst = first - first % kGalPer;
    c.fill(8, kGalY - 2, 304, 184 - kGalY + 2, 0);
    static const char* const kTitle[icon_looks::kPages] = {"Ready-Made Icons", "Heads", "Weapons", "Colors"};
    c.fill(16, 8, 288, 8, 0);
    put(c, gal_gbc(ie->gfirst) ? "Gold Box Companion Icons" : kTitle[ie->gpage], 2, 1, 15);
    dirty(8, 16);
    const int n = gal_count();
    for (int i = ie->gfirst; i < n && i < ie->gfirst + kGalPer; ++i) gal_cell(c, i);
    gal_frame(c, ie->gsel, 15);
    const int pages = gal_pages();
    if (pages > 1) {
        static const char* const kWord[2] = {"PREV", "NEXT"};
        for (int k = 0; k < 2; ++k) {
            int x, y, w, h;
            gal_nav_box(k, &x, &y, &w, &h);
            gal_outline(c, x, y, w, h, 7);
            put(c, kWord[k], (x + (w - 32) / 2) / 8, (y + h / 2 - 4) / 8, 15);
        }
        char t[12];
        snprintf(t, sizeof t, "%d OF %d", ie->gfirst / kGalPer + 1, pages);
        put(c, t, (kGalNavX + (kGalNavW - static_cast<int>(strlen(t)) * 8) / 2) / 8, (kGalY + kGalPitchY + kGalBox / 2 - 4) / 8, 15);
    }
    dirty(kGalY - 2, 184);
}

void gal_open(pic::Canvas& c, int page)
{
    ie->gpage = ie->glast = page;
    ie->level = 1;
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    dirty(0, pic::kScreenH);
    if (!gal_parts()) {
        gal_menu(c);
        error(c, no_memory("the icon gallery"));
        return;
    }
    const int pick = gal_pick_now();
    ie->gsel = pick ? (gal_page() == icon_looks::Page::Looks ? icon_looks::kLooks + pick - 1 : -1)
                    : icon_looks::match(gal_page(), ie->ch->rec);
    gal_draw_page(c, ie->gsel < 0 ? 0 : ie->gsel);
    gal_menu(c);
}

void gal_pick(int i, pic::Canvas& c)
{
    if (i < 0 || i >= gal_count()) return;
    gal_frame(c, ie->gsel, 0);
    if (gal_gbc(i)) {
        gal_set_pick(i - icon_looks::kLooks + 1);
    } else {
        gal_set_pick(0);                    // (the game's own icon again)
        icon_looks::apply(gal_page(), i, ie->ch->rec);
    }
    ie->gsel = i;
    if (i < ie->gfirst || i >= ie->gfirst + kGalPer) gal_draw_page(c, i);   // (Up / Down past the page)
    else gal_frame(c, i, 15);
    build_icon(ie->ch->rec, ie->new_ic);    // (the big preview)
}

// Prev / Next: the page before / after (round)
void gal_turn(int step, pic::Canvas& c)
{
    const int pages = gal_pages();
    if (pages < 2) return;
    const int pg = (ie->gfirst / kGalPer + step + pages) % pages;
    gal_draw_page(c, pg * kGalPer);
}

void gal_key(char k, pic::Canvas& c)
{
    static const char kKeys[icon_looks::kPages] = {'I', 'H', 'W', 'C'};
    for (int p = 0; p < icon_looks::kPages; ++p)
        if (k == kKeys[p]) {
            if (p != ie->gpage) gal_open(c, p);
            else gal_menu(c);
            return;
        }
    if (k != 'E' && k != 'D') return;
    if (k == 'E') gal_set_pick(0);          // (the editor works on the game's own icon)
    // The picks are kept; on to the original editor, or its question
    ie->gpage = -1;
    icon_take(ie->keep, ie->ch->rec);
    icon_screen(c);
    icon_new(c);
    ie->level = k == 'E' ? 1 : 6;
    menu.selected = 0;                      // (Edit: Parts lit)
    icon_menu(c);
}

void gal_tap(int x, int y, pic::Canvas& c)
{
    if (y >= text::kMenuTapTop) {
        const int k = text::hit(menu, x / 8);
        if (k < 0) return;
        menu.selected = k;
        show_menu_line(c);
        gal_key(text::key(menu, k), c);
        return;
    }
    const int nav = gal_nav_at(x, y);
    if (nav >= 0) {
        gal_turn(nav ? 1 : -1, c);
        return;
    }
    gal_pick(gal_cell_at(x, y), c);
}

// Up / Down: the next / previous choice, picked (the page turns with it)
void gal_step(int step, pic::Canvas& c)
{
    const int n = gal_count();
    int i;
    if (ie->gsel >= ie->gfirst && ie->gsel < ie->gfirst + kGalPer) i = (ie->gsel + step + n) % n;
    else i = step > 0 ? ie->gfirst : (ie->gfirst + kGalPer < n ? ie->gfirst + kGalPer : n) - 1;   // (none on this page)
    gal_pick(i, c);
}
#endif

// The sound driver's tables and byte code, from the player's program
void load_sound()
{
    if (d->prof->clips.file) {
        // Darkness: nothing held - each effect is read as it sounds
        plat::File f;
        if (open_file(d->prof->clips.file, f)) {
            f.close();
            clips = new (std::nothrow) sound::ClipPlayer;
        }
        if (!clips) plat::log("[play] no %s: no sound\n", d->prof->clips.file);
        memset(clip_rates, 0, sizeof clip_rates);
        if (clips && d->prof->clips.rate_table && open_file(d->prof->program, f)) {
            // (Gateway: the rates by effect number, from the player's program)
            plat::FileSource src(f);
            exepack::Info info;
            uint8_t b[32];
            if (exepack::parse(src, info) == exepack::Status::Ok &&
                exepack::read(src, info, d->prof->create.ds_image + d->prof->clips.rate_table, b, sizeof b) == exepack::Status::Ok)
                for (int k = 0; k < 16; ++k) clip_rates[k] = static_cast<uint16_t>(b[2 * k] | b[2 * k + 1] << 8);
            f.close();
        }
        return;
    }
    const sound::Layout& l = d->prof->sound;
    if (!l.hi || l.hi <= l.lo) return;
    const size_t n = l.size(), n1 = l.hi - l.lo;
    uint8_t* buf = static_cast<uint8_t*>(malloc(n));
    snd = new (std::nothrow) sound::Player;
    plat::File f;
    bool ok = false;
    if (buf && snd && open_file(d->prof->program, f)) {
        plat::FileSource src(f);
        exepack::Info info;
        ok = exepack::parse(src, info) == exepack::Status::Ok &&
             exepack::read(src, info, l.segment_image + l.lo, buf, n1) == exepack::Status::Ok &&
             (n == n1 || exepack::read(src, info, l.segment_image + l.lo2, buf + n1, n - n1) == exepack::Status::Ok) &&
             snd->set_data(l, buf, n);
        f.close();
    }
    free(buf);
    if (!ok) {
        plat::log("[play] the sound driver's data didn't read: no sound\n");
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
    char     name[42] = {}, menu_prompt[20] = {};    // (Secret's two-line version screen)
    lightning::State lt;                // Pool: lightning on the last picture
    int      flash = -1;
};
TitleRun* title_run = nullptr;

bool title_picture(pic::Canvas& c, int block, int row, int col)
{
    plat::File f;
    if (!open_dax(d->prof->title_file, f)) return false;
    plat::FileSource src(f);
    bool ok = false;
    const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
    if (e && d->prof->variant.vga) {
        // (Darkness: a VGA picture - its EGA colours on the 4-bit canvas)
        ok = pic::draw_vga_block(src, d->idx, *e, 0, c, col * 8, row * 8, -1, vga_load());
        vga_take();                      // (a VGA screen: all 256 entries)
    } else if (e) {
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
            layout::credits(c, d->tables, d->frame_tiles, p.credits_bars);
            for (int i = 0; i < title_run->n_credits; ++i) {
                const printcalls::Line& l = title_run->credits[i];
                font::draw_text(c, d->font, l.s, l.col, l.row, l.fg, l.bg);
            }
        } else {
            // (Treasures: its game picture redrawn for 16 colours in the EGA
            // mode - a 4-bit screen; ovl 0x40 +0x11E)
            const bool ega = p.variant.title_ega[0] && st.block == p.variant.title_ega[0] && !vga;
            title_picture(c, ega ? p.variant.title_ega[1] : st.block, st.row, st.col);
        }
        if (st.sound) sfx(st.sound);
        title_run->step = s;
        if (st.wait_ms > 0) break;
    }
    title_run->since = plat::now_ms();
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
    vga_send(16, 16);       // (a VGA screen: the frame's colours again after the titles - img 0x172)
    c.clear(0);
    dirty_rows(0, 24);
    if (!title_run->words[0]) {
        end_title(c);
        return;
    }
    if (title_run->name[0]) {
        // Secret: the name and the version black on a green bar (rows 22,
        // 23), the menu under them after its own prompt
        font::draw_text(c, d->font, title_run->name, 0, 22, 0, 10);
        font::draw_text(c, d->font, title_run->version, 0, 23, 0, 10);
        text::build(menu, title_run->menu_prompt, title_run->words);
    } else {
        text::build(menu, title_run->version, title_run->words);
    }
    menu.selected = 0;
    show_menu_line(c);
    title_run->since = plat::now_ms();
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
    title_run->flash = !title_run->prompt && profile::title_flash_step(*d->prof, title_run->step)
                           ? lightning::colour(title_run->lt, now) : -1;
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
    // Pool's ending: its loops of the animation, those played; its frame time
    bool     pool = false;
    int      loops = 0, loop = 0, last_pic = -1;
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
    OvrWords src;
    if (src.open()) {
        for (int k = 0; k < wf.page_lines[page] && first + k < 24; ++k) {
            char line[text::kMaxString];
            if (!text::read_pascal(src, wf.text[first + k], line, sizeof line)) continue;
            for (const char* q = line; *q && o + 1 < cap; ++q) buf[o++] = *q;
        }
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
            won->at = plat::now_ms();
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
                for (int x = 24; x < 112; ++x) c.set(x, y, won->fade[c.get(x, y) & 15]);
            dirty_rows(3, 13);
        }
        int speed = vm->get(0x4BFC) & 0xFF;
        if (speed > 9) speed = 9;
        won->stage = WonRun::Hold;
        won->at = plat::now_ms() + static_cast<uint32_t>((10 - speed) * 2) * 250;
        return;
    }
    case 5:
        won->stage = WonRun::Fireworks;
        won->next_rocket = plat::now_ms() + 300;
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

// ---- Pool's ending (PROGRAM 8; poolrad_combat_facts.md 8, its ovr19):
// the dragon's words in the text box (colour 10, a line a row from row
// 17), FINAL<area>.DAX's pictures in the left box and its animations played
// (speed + 3) x their delays in hundredths of a second a frame, 5 x (10 -
// speed) / 2 times round (the last once); the party's figures by its size;
// a pause of 3 x speed x 225 ms; then the screen back and the script on
int pool_speed()
{
    const int s = vm->get(0x4BFC) & 0xFF;
    return s > 9 ? 9 : s;
}

void pool_end_text(pic::Canvas& c, int first, int n)
{
    const auto& wf = d->prof->won;
    c.fill(8, 17 * 8, 38 * 8, 6 * 8, 0);
    OvrWords src;
    if (src.open())
        for (int k = 0; k < n && first + k < 24; ++k) {
            char line[text::kMaxString];
            if (text::read_pascal(src, wf.text[first + k], line, sizeof line)) put(c, line, 1, 17 + k, 10);
        }
    dirty_rows(17, 22);
}

void pool_end_anim(int block, int x, int y, int loops)
{
    won->stage = WonRun::Anim;
    won->loops = loops < 1 ? 1 : loops;
    won->loop = 0;
    won->at = plat::now_ms();
    if (!anim_start(block, "FINAL", x, y) || d->anim.frames < 1) {
        // (no animation: on at once)
        won->stage = WonRun::Hold;
        won->at = plat::now_ms();
        return;
    }
    if (anim_block < 0) {
        // Alter's animations off: its first frame held as long as the loops would take
        uint32_t t = 0;
        for (int k = 0; k < d->anim.frames; ++k) t += d->anim.delay[k];
        won->stage = WonRun::Hold;
        won->at = plat::now_ms() + t * static_cast<uint32_t>(pool_speed() + 3) * 10u * static_cast<uint32_t>(won->loops);
    }
}

void pool_end_step(pic::Canvas& c)
{
    const auto& wf = d->prof->won;
    const int speed = pool_speed();
    const int loops = 5 * (10 - speed) / 2;
    switch (won->page) {
    case 0:
        pool_end_text(c, 0, wf.page_lines[0]);
        draw_block(c, "FINAL", 1, 8, 8);
        dirty_rows(1, 15);
        pool_end_anim(2, 24, 40, loops);
        return;
    case 1:
        draw_block(c, "FINAL", 3, 8, 8);
        // The party's figures (up to three more by its size)
        if (pt->count > 1) draw_block(c, "FINAL", 4, 8, 96);
        if (pt->count > 2) draw_block(c, "FINAL", 5, 112, 80);
        if (pt->count > 3) draw_block(c, "FINAL", 6, 8, 80);
        dirty_rows(1, 15);
        pool_end_text(c, wf.page_lines[0], wf.page_lines[1]);
        pool_end_anim(7, 24, 8, loops);
        return;
    case 2:
        pool_end_text(c, wf.page_lines[0] + wf.page_lines[1], wf.page_lines[2]);
        pool_end_anim(8, 24, 8, 1);
        return;
    case 3:
        anim_stop();
        won->stage = WonRun::Hold;
        won->at = plat::now_ms() + static_cast<uint32_t>(3 * speed * 225);
        return;
    default: {
        // The screen back, the script on
        last_pic = won->last_pic;
        delete won;
        won = nullptr;
        fight_back_to_game(c);
        waiting = false;
        handle(vm->resume());
        return;
    }
    }
}

void pool_end_tick(uint32_t now, pic::Canvas& c)
{
    if (won->stage == WonRun::Hold) {
        if (static_cast<int32_t>(now - won->at) < 0) return;
        ++won->page;
        pool_end_step(c);
        return;
    }
    if (won->stage != WonRun::Anim || anim_block < 0) {
        ++won->page;
        pool_end_step(c);
        return;
    }
    const uint32_t delay = static_cast<uint32_t>(pool_speed() + 3) * d->anim.delay[anim_frame] * 10u;
    if (now - won->at < delay) return;
    won->at = now;
    if (anim_frame + 1 >= d->anim.frames) {
        if (++won->loop >= won->loops) {
            ++won->page;
            pool_end_step(c);
            return;
        }
        anim_frame = -1;
    }
    anim_draw(++anim_frame);
}

void start_won(pic::Canvas& c)
{
    anim_stop();
    if (d->prof->variant.final_end) {
        if (!won) won = new (std::nothrow) WonRun;
        if (!won) {
            waiting = false;                // (no memory: the script goes on without it)
            handle(vm->resume());
            return;
        }
        won->pool = true;
        won->last_pic = last_pic;
        clear_menu_line(c);
        screen = Screen::Won;
        won->page = 0;
        pool_end_step(c);
        return;
    }
    if (!won) won = new (std::nothrow) WonRun;
    if (!won) {
        won_finish(c);
        return;
    }
    // The fade's table (the program's data)
    for (int k = 0; k < 16; ++k) won->fade[k] = static_cast<uint8_t>(k);
    plat::File f;
    if (open_file(d->prof->program, f)) {
        plat::FileSource src(f);
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
    if (!won || won->pool) return;      // (Pool's plays out: no key, as the original)
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
    *under = c.get(x, y);
    c.set(x, y, col);
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
            if (s.drawn) c.set(s.x >> 5, s.y >> 5, s.under);
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
            for (int k = r.n_trail - 1; k >= 0; --k) c.set(r.trail[k].x, r.trail[k].y, r.trail[k].under);
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
    if (won->pool) {
        pool_end_tick(now, c);
        return;
    }
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
    gs.script = d->prof->variant.demo_block ? d->prof->variant.demo_block : 0x52;    // (Treasures: block 4)
    d->loaded = false;
    c.clear(0);
    screen = Screen::Game;
    if (!host->load_script(gs.script, ecl::code_of(gs), &gs.code_len) || !vm->init_script(false)) {
        plat::log("[play] the demo's script didn't load\n");
        end_demo(c);
        return;
    }
    draw_frame(c);
    tr_position = true;          // (Treasures: its first picture has it under it - DOSBox)
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
    if (!opts.demo_tap) return true;          // (as the original: ignored)
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
    plat::File f;
    if ((p.title_version || p.title_menu) && open_file(p.program, f)) {
        plat::FileSource src(f);
        exepack::Info info;
        if (exepack::parse(src, info) == exepack::Status::Ok) {
            if (p.title_version) text::read_pascal(src, info, p.title_version, title_run->version, sizeof title_run->version);
            if (p.title_menu) text::read_pascal(src, info, p.title_menu, title_run->words, sizeof title_run->words);
            if (p.title_name) text::read_pascal(src, info, p.title_name, title_run->name, sizeof title_run->name);
            if (p.title_prompt) text::read_pascal(src, info, p.title_prompt, title_run->menu_prompt, sizeof title_run->menu_prompt);
        }
        f.close();
    }
    if (open_file(p.overlay, f)) {
        plat::FileSource osrc(f);
        if (osrc.size() == p.overlay_size && p.credits_at)
            title_run->n_credits = printcalls::read(osrc, p.credits_at, p.credits_base, title_run->credits, kTitleCredits,
                                                         p.variant.credits_stride ? p.variant.credits_stride : printcalls::kBlock);
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

int flash(int& index)
{
    if (!d || screen != Screen::Title || !title_run) return -1;
    index = d->prof->variant.title_flash;
    return title_run->flash;
}

bool icon_gallery()
{
#if CYD_ICON_GALLERY
    return icon_editing() && ie->gpage >= 0;
#else
    return false;
#endif
}

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

// The letters the game's text is in (Options::font): loads the player's
// Pools of Darkness font the first time it's asked for; without it (or
// short of memory) the game's own
int font_wanted()
{
    if (!CYD_CLEAR_FONT || !d) return font::kGameLetters;
    if (opts.font == 1) return font::kClearLetters;
    if (opts.font == 3) return font::kClearCapsLetters;      // (settings.h kFontClearCaps)
    if (opts.font != 2) return font::kGameLetters;
    if (!d->alt_font && !d->alt_tried && darkness_dir[0]) {
        d->alt_tried = true;
        char path[160];
        snprintf(path, sizeof path, "%s/8X8D0.DAX", darkness_dir);
        plat::File f = plat::open(path, "r");
        font::Font* af = f ? new (std::nothrow) font::Font : nullptr;
        if (af) {
            plat::FileSource src(f);
            dax::Index* idx = new (std::nothrow) dax::Index;
            const bool ok = idx && dax::read_index(src, *idx) == dax::Status::Ok && font::load(src, *idx, *af);
            delete idx;
            if (ok) d->alt_font = af;
            else delete af;
        }
        if (f) f.close();
    }
    return d->alt_font ? font::kAltLetters : font::kGameLetters;
}

void set_options(const Options& o)
{
    opts = o;
    if (!d) return;
    // The items the originals got wrong: as now wanted, the party's too (mid-game)
    items::set_flame_fix(&d->prof->items.flame, CYD_ITEM_FIXES && opts.item_fixes);
    rules::set_fixes(CYD_ITEM_FIXES && opts.item_fixes);
    for (int i = 0; pt && i < pt->count; ++i)
        if (rules::fix_items(pt->m[i]) && names) rules::recalc(pt->m[i], *names, d->facts);
    const int letters = font_wanted();
    if (d->font.letters == letters) return;
    // Mid-game (Tom, 2026-10-11): the text on the screen changes font too
    font::set_letters(d->font, letters, d->alt_font, cv);
    if (cv) {
        if (screen == Screen::Game && !pic_shown && bigpic < 0) draw_view(*cv);   // (the 3D view as it was)
        // The party list and the menu line again: their mixed case as written
        if (screen == Screen::Game && bigpic < 0 && !waiting) draw_panel(*cv);
        if (menu_on) show_menu_line(*cv);
        dirty(0, pic::kScreenH);
    }
}

void set_darkness_dir(const char* dir)
{
    strncpy(darkness_dir, dir ? dir : "", sizeof darkness_dir - 1);
    darkness_dir[sizeof darkness_dir - 1] = 0;
}

void set_import_dir(const char* dir)
{
    strncpy(import_dir, dir ? dir : "", sizeof import_dir - 1);
    import_dir[sizeof import_dir - 1] = 0;
}

// "Not enough memory" with the numbers (for Tom's reports)
// ---- Memory recovery (Tom, 2026-10-12: "not only memory management, but
// also proactive methods of dealing with what the memory management finds,
// ways to recover"). What can be read again from the game's files is
// dropped when a load would otherwise fail, in this order: the sound
// playing (its task and DAC, ~7 KB), the healing words, the magic tables
// and spell names (~9 KB) - those two only while the party is exploring
// (no fight, menu, cast or item use under way).
// Each drop goes in the memory log. make_room(kb): true when the largest
// free block has kb KB (after dropping what it had to).
bool fighting() { return fg != nullptr; }

// (the magic tables go only while the party is exploring - no fight, no
// menu, no cast or item use under way: many screens and questions use them)
bool magic_in_use()
{
    return fg || screen != Screen::Game || cr.stage != CastRun::None || cr.item >= 0;
}

bool make_room(unsigned kb, const char* what)
{
    if (memlog::largest_kb() >= kb) return true;
    char line[120];
    auto dropped = [&](const char* thing) {
        snprintf(line, sizeof line, "made room for %s: dropped %s", what, thing);
        memlog::note(line);
        return memlog::largest_kb() >= kb;
    };
    if (audio_running() && !audio_held()) {         // (a fight's held output stays: Hit / Miss)
        audio_stop();
        if (dropped("the sound playing")) return true;
    }
    if (hw && !fg && screen == Screen::Game) {
        delete hw;
        hw = nullptr;
        if (dropped("the healing words")) return true;
    }
    if ((mrules || spn) && !magic_in_use()) {
        end_magic();
        if (dropped("the magic tables")) return true;
    }
    return memlog::largest_kb() >= kb;
}

// Called now and then while playing: when memory runs low outside a fight
// and the magic tables / healing words aren't in use, they go (they read
// again when needed) - so the next fight starts with the most room
void tidy_memory()
{
    static uint32_t next = 0;
    if (plat::now_ms() < next) return;
    next = plat::now_ms() + 5000;
    if (fg || memlog::largest_kb() >= 32) return;
    make_room(32, "the next fight");
}

const char* no_memory(const char* what)
{
    snprintf(msg, sizeof msg, "Not enough memory for %s (free %u KB, largest block %u KB).", what,
             (unsigned)(plat::free_heap() / 1024),
             (unsigned)(plat::largest_block() / 1024));
    plat::log("[play] %s\n", msg);
    char t[80];
    snprintf(t, sizeof t, "NOT ENOUGH MEMORY for %s", what);
    memlog::note(t);
    return msg;
}

void set_sound(uint8_t mode, uint8_t volume)
{
    snd_mode = mode;
    snd_volume = volume;
    if (mode != 1 && mode != 2) audio_stop();
}

uint8_t sound_mode() { return snd_mode; }

void sound_test(int id, bool game) { sfx(id, game); }

// play::open's steps, each with its own file buffers on the stack only
// while it runs (v0.103.3: open's frame was 2 KB, and the loop task's 8 KB
// stack had under 200 bytes left at Play's start - Tom's S3, whose card
// driver goes deeper, crashed there)
namespace {

// The program: which release, its tables and words. False: not one the
// engine knows (msg set)
// A VGA game in 256 colours: its colour cycling table from the player's
// program (none: no cycling)
__attribute__((noinline)) void open_cycles(games::Game g, const char* prog)
{
    const profile::CycleFacts* cf = profile::cycle_facts(g);
    plat::File f;
    if (!cf || !open_file(prog, f)) return;
    plat::FileSource src(f);
    exepack::Info info;
    if (exepack::parse(src, info) != exepack::Status::Ok ||
        exepack::read(src, info, d->prof->data_base + cf->table, &vga->cyc[0][0], sizeof vga->cyc) != exepack::Status::Ok)
        memset(vga->cyc, 0, sizeof vga->cyc);
    f.close();
}

__attribute__((noinline)) bool open_program(games::Game g, const char* prog)
{
    plat::File f;
    if (!open_file(prog, f)) {
        snprintf(msg, sizeof msg, "%s is missing from the game folder.", prog);
        return false;
    }
    plat::FileSource src(f);
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
        return false;
    }
    if (!p->party.items) {
        // (a game being built whose party menu isn't known yet: Secret's
        // screens and maps only, in the Screen and Walk Tests)
        snprintf(msg, sizeof msg, "The Play Test for %s isn't ready yet: try the Screen and Walk Tests.",
                 games::title(g));
        return false;
    }
    d->prof = p;
    return true;
}

// Treasures in 256 colours: its background picture behind the screen's
// clears and text (on), or black - a fight's time (off)
void tr_background(bool on)
{
    if (cv && d && d->tr_bg) {
        cv->bg = on ? d->tr_bg : nullptr;
        cv->bg_rows = 192;
    }
}

// Treasures: its frame (port facts P7) - BORD.DAX block 0's tiles and
// block 7's pieces, kept while playing
__attribute__((noinline)) bool open_tr_frame()
{
    plat::File f;
    if (!open_dax("BORD.DAX", f)) return false;
    plat::FileSource src(f);
    bool ok = false;
    d->tr_frame = static_cast<uint8_t*>(malloc(64 * 64));
    const dax::Entry* e = d->idx.find(7);
    if (d->tr_frame && e && e->raw_size > 0) d->tr_pieces = static_cast<uint8_t*>(malloc(e->raw_size));
    if (d->tr_pieces && dax::load_block(src, d->idx, *e, d->tr_pieces) == e->raw_size &&
        layout::load_tr_tiles(src, d->idx, 0, d->tr_frame, !vga, vga_load())) {
        vga_take(true);              // (a VGA screen: its entries 16-31, kept)
        layout::Tables& tb = d->tables;
        tb.tr = true;
        tb.chain = false;
        layout::tr_set_pieces(tb, d->tr_pieces, e->raw_size);
        tb.tr_tiles = d->tr_frame;
        tb.tr_vga = vga != nullptr;
        ok = true;
        // Its text font: BORD block 8's 64 glyphs (white on magenta - '@',
        // A-Z ... in the order of the font's glyphs 0-63; DOSBox) in place
        // of 8X8D0's (whose other glyphs stay)
        // - in 256 colours drawn over the background, the glyphs' black
        // pixels their shadow (DOSBox VGA)
        uint8_t* g = static_cast<uint8_t*>(malloc(64 * 64));
        if (vga && cv && cv->bpp == 8) d->tr_shade = static_cast<uint8_t(*)[8]>(malloc(64 * 8));
        if (g && layout::load_tr_tiles(src, d->idx, 8, g, true)) {
            for (int k = 0; k < 64; ++k)
                for (int y = 0; y < 8; ++y) {
                    uint8_t bits = 0, shade = 0;
                    for (int x = 0; x < 8; ++x) {
                        const uint8_t v = g[k * 64 + y * 8 + x];
                        if (v != 13 && v != 0) bits = static_cast<uint8_t>(bits | 0x80 >> x);
                        if (v == 0) shade = static_cast<uint8_t>(shade | 0x80 >> x);
                    }
                    d->font.glyph[k][y] = bits;
                    if (d->tr_shade) d->tr_shade[k][y] = shade;
                }
            d->font.shade = d->tr_shade;
        }
        free(g);
        // 256 colours: the red background (BORD block 1, 320 x 192, its
        // entries 16-21 the frame's) behind the frame - every clear puts it
        // back (the original's img 0x17A3); the EGA mode's is black
        const dax::Entry* b = d->idx.find(1);
        if (vga && cv && cv->bpp == 8 && b) {
            d->tr_bg = static_cast<uint8_t*>(malloc(pic::kScreenW * 192));
            pic::Canvas bc{d->tr_bg, pic::kScreenW, 192, 8};
            bc.vga = true;
            if (d->tr_bg && pic::draw_vga_block(src, d->idx, *b, 0, bc, 0, 0, -1, vga_load())) {
                vga_take(true);
                cv->bg = d->tr_bg;
                cv->bg_rows = 192;
            } else {
                vga_take(true);
                free(d->tr_bg);
                d->tr_bg = nullptr;
            }
        }
    }
    f.close();
    return ok;
}

// The frame's tiles, the font, the 3D view's common tiles
__attribute__((noinline)) bool open_tiles()
{
    plat::File f;
    bool ok = open_dax(d->prof->tiles_file, f);
    if (ok && d->prof->variant.vga) {
        // Darkness: the area map's tiles and the font in 8X8D0, the frame's
        // style from BORDERS (the gold chain to start with); every wall
        // tile comes with an area's look (darkness_facts.md 7)
        plat::FileSource src(f);
        // (a VGA screen: their values; the frame's colours, 16-31, sent and
        // kept - the area map's are the text's EGA ones)
        ok = layout::load_vga_tiles(src, d->idx, d->prof->tiles_block, d->frame_tiles, 0, 20, !vga) &&
             font::load(src, d->idx, d->font);
        f.close();
        if (ok && d->prof->variant.tr) ok = open_tr_frame();
        else if (ok) ok = open_dax("BORDERS.DAX", f);
        if (ok && !d->prof->variant.tr) {
            plat::FileSource bsrc(f);
            ok = layout::load_vga_tiles(bsrc, d->idx, 0, d->frame_tiles, 20, 11, !vga, vga_load());
            vga_take(true);
            f.close();
        }
        d->world.dk = true;
    } else if (ok) {
        plat::FileSource src(f);
        ok = layout::load_tiles(src, d->idx, d->prof->tiles_block, d->frame_tiles, d->prof->variant.tile_window) &&
             (d->prof->font_file || font::load(src, d->idx, d->font)) &&
             view3d::load_tiles(src, d->idx, d->prof->common_tiles_block, d->world.common);
        f.close();
    }
    if (ok && d->prof->font_file) ok = load_font_file(d->prof->font_file, d->idx, d->font);
    if (ok) {
        const int letters = font_wanted();     // (loads the alt font first)
        font::set_letters(d->font, letters, d->alt_font);
    }
    return ok;
}

__attribute__((noinline)) void open_sky()
{
    plat::File f;
    if (d->prof->sky_file && open_dax(d->prof->sky_file, f)) {
        plat::FileSource src(f);
        view3d::load_horizon(src, d->idx, d->prof->horizon_block, d->world);
        f.close();
    }
}

// Treasures: its effect names (BORD.DAX block 9, 38 bytes a name from
// effect 1) into the names file on the card, once - effect 0's room first,
// so effect n's name is at 38 x n (text::kInNames)
__attribute__((noinline)) void open_names()
{
    if (!(d->prof->magic.effect_names.at & text::kInNames) || !d->cache_dir[0]) return;
    char path[200];
    OvrWords::names_path(path, sizeof path);
    if (plat::exists(path)) return;
    plat::mkdir(d->cache_dir);          // (the scan makes it; a card scanned before this game was there may lack it)
    plat::File f;
    if (!open_dax("BORD.DAX", f)) return;
    plat::FileSource src(f);
    const dax::Entry* e = d->idx.find(9);
    plat::File o = e ? plat::open(path, "w") : plat::File();
    bool ok = false;
    if (o) {
        uint8_t buf[38] = {};
        ok = o.write(buf, sizeof buf) == sizeof buf;
        dax::RleReader r(src, d->idx, *e);
        size_t got;
        while (ok && (got = r.read(buf, sizeof buf)) > 0) ok = o.write(buf, got) == got;
        o.close();
        if (!ok) plat::remove(path);
    }
    f.close();
    plat::log("[play] the effect names: %s\n", ok ? "made" : "not made");
}

// The script machine's own words
__attribute__((noinline)) void open_script_words()
{
    OvrWords src;
    const bool ok = src.open();
    for (int i = 0; i < ecl::kScriptWords; ++i) {
        if (ok && d->prof->script_words[i])
            text::read_pascal(src, d->prof->script_words[i], d->script_word[i], sizeof d->script_word[i]);
        d->script_words[i] = d->script_word[i];
    }
}

} // namespace

const char* open(const char* data_dir, games::Game g, pic::Canvas& c, const char* cache_dir)
{
    close();
    cv = &c;
    age_check = false;
    pic::sf_map_reset();                // (Gateway's pictures without a colour map: the last one read)
    const char* prog = profile::program_name(g);
    if (!prog) {
        snprintf(msg, sizeof msg, "No play test for %s yet.", games::title(g));
        return msg;
    }
    if (!plat::card_ready()) return "No SD card found.";
    {
        view3d::World* w = new (std::nothrow) view3d::World;
        ecl::GameState* st = ecl::new_game_state(profile::code_cap(g));
        d = w && st ? new (std::nothrow) Data(*w, *st) : nullptr;
        if (!d) {
            delete w;
            ecl::free_game_state(st);
            return no_memory("the game state");
        }
    }
    strncpy(d->data_dir, data_dir, sizeof d->data_dir - 1);
    if (cache_dir) strncpy(d->cache_dir, cache_dir, sizeof d->cache_dir - 1);
    load_icon_picks();

    if (!open_program(g, prog)) {
        close();
        return msg;
    }
    if (c.vga && c.bpp == 8 && d->prof->variant.vga) {
        // Darkness on a 256-colour screen (canvas_depth): its VGA palette,
        // the 3D view's tiles a byte a pixel
        vga = new (std::nothrow) Vga;
        if (!vga) {
            close();
            return no_memory("the VGA palette");
        }
        for (int i = 0; i < 256; ++i) vga->now[i] = vga->kept[i] = i < 16 ? pic::kEga[i] : pic::Rgb{0, 0, 0};
        d->world.vga = true;
        open_cycles(g, prog);
    }
    if (!open_tiles()) {
        snprintf(msg, sizeof msg, "%s: the tiles or font didn't load.", d->prof->tiles_file);
        close();
        return msg;
    }
    d->world.frame = &d->frame_tiles;
    d->world.per_set = d->prof->variant.per_set_walls;
    text::set_hanging_space(gateway());
    open_sky();
    open_names();

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
    open_script_words();
    vm->set_words(d->script_words);
    vm->set(0x4BFF, 3);                     // Pics and Animation on, as the game starts (a save says its own)
    load_sound();
    if (sound_on()) audio_prepare();    // (the S3's codec and amplifier awake before the first sound)
    note_until = 0;
    last_pic_id = -1;
    ecl::GameState& gs = d->gs;
    gs.game_area = d->prof->start_area;
    gs.x = 7;
    gs.y = 13;
    gs.dir = 0;
    vm->set(0x7F12, gs.game_area);
    if (d->prof->variant.tr) {
        // Treasures' game block as its start sets it (ovl 0x87 +0x54E): the
        // difficulty 3, in 3D, the text green, BACK1 block 3 ...
        gs.area1[0x26] = 3;
        gs.area1[0x21] = 1;
        gs.area1[0x38] = 0x0A;
        gs.area1[0x52] = 2;
        gs.area1[0x54] = 3;
        gs.area1[0x11] = 4;
    }
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
    delete clips;
    clips = nullptr;
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
        view3d::vga_free(*w);                   // (Darkness's background picture, VGA tiles)
        if (cv && d->tr_bg && cv->bg == d->tr_bg) cv->bg = nullptr;
        for (view3d::TileSet& ts : w->sets) view3d::free_extra(ts);     // (Gateway's big wall sets)
        delete d->alt_font;
        free(d->gw_xp);
        delete d;
        delete w;
        ecl::free_game_state(g);
    }
    d = nullptr;
    delete vga;
    vga = nullptr;
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
    if (dk_overland()) {
        // Darkness's overland: eight directions - a turn is 45 degrees (its
        // move mode: up a step, down a turn round)
        switch (a) {
        case Act::TurnLeft:   g.dir = (g.dir + 7) & 7; break;
        case Act::TurnRight:  g.dir = (g.dir + 1) & 7; break;
        case Act::TurnAround: g.dir = (g.dir + 4) & 7; break;
        case Act::Forward:    step(g.dir); return true;
        case Act::StepLeft:   step((g.dir + 6) & 7); return true;
        case Act::StepRight:  step((g.dir + 2) & 7); return true;
        default: return false;
        }
        return true;
    }
    if (pool_overland()) {
        // Pool's wilderness: the arrow keys face that way and step (north,
        // east, south, west); no turning in place
        int dir = -1;
        if (a == Act::Forward) dir = 0;
        else if (a == Act::StepRight) dir = 2;
        else if (a == Act::TurnAround) dir = 4;
        else if (a == Act::StepLeft) dir = 6;
        else if (a == Act::Look) dir = -2;
        if (dir >= 0) {
            g.dir = static_cast<uint8_t>(dir);
            step(dir);
            return true;
        }
        if (dir == -1) return false;
    }
    // Gateway's wilderness: turns and a step ahead only (its step script
    // judges the square ahead), no Area
    if (gw_wild() && (a == Act::StepLeft || a == Act::StepRight || a == Act::Area)) return false;
    switch (a) {
    case Act::TurnLeft:   g.dir = (g.dir + 6) & 7; turn_sound(); break;
    case Act::TurnRight:  g.dir = (g.dir + 2) & 7; turn_sound(); break;
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
#if CYD_ICON_GALLERY
    if (icon_gallery()) {
        gal_step(step, c);
        return true;
    }
#endif
    if (screen == Screen::SpellList && !sl.learning && sl.sel >= 0) {
        const int rows = list_rows();
        int i = sl.sel;
        if (i < sl.top || i >= sl.top + rows) {
            // (Tom: the page was turned - the highlight starts on the page shown,
            // its first line going down, its last going up)
            const int last = std::min(sl.n, sl.top + rows) - 1;
            int j = -1;
            if (step > 0) {
                for (int k = sl.top; k <= last && j < 0; ++k)
                    if (sl.id[k]) j = k;
            } else {
                for (int k = last; k >= sl.top && j < 0; --k)
                    if (sl.id[k]) j = k;
            }
            if (j >= 0) {
                sl.sel = j;
                draw_spells(c);
                keep_menu_choice(keep, c);
                return true;
            }
        }
        for (int k = 0; k < sl.n; ++k) {
            i = (i + step + sl.n) % sl.n;
            if (sl.id[i]) break;
        }
        sl.sel = i;
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

bool fight_pad()
{
    return d && input_mode == Input::None && screen == Screen::Fight && fg &&
           (fg->st == FSt::Move || (fg->st == FSt::Aim && fg->manual));
}

bool fight_dir(int dir, pic::Canvas& c)
{
    if (!fight_pad()) return false;
    cv = &c;
    return fight_way(dir, c);
}

bool fight_select(pic::Canvas& c)
{
    if (!fight_pad()) return false;
    cv = &c;
    if (fg->st == FSt::Move) {
        end_move(c);
        return true;
    }
    // Manual aim: Target, when it's on the menu line
    for (int k = 0; k < menu.count; ++k)
        if (text::key(menu, k) == 'T') {
            menu.selected = k;
            int x, y;
            if (!nav_point(&x, &y)) return false;
            tap(x, y, c);
            return true;
        }
    return false;
}

bool walking()
{
    if (!d || input_mode != Input::None) return false;
    if (screen == Screen::Fight) return fg && (fg->st == FSt::Move || (fg->st == FSt::Aim && fg->manual));
    return screen == Screen::Game && !waiting && then == Then::Idle && (vm->get(0x4BE6) != 0 || pool_overland() || gw_wild());
}

void set_present_now(void (*fn)()) { g_present_now = fn; }

bool walk_pending()
{
    if (!d || input_mode != Input::None || screen != Screen::Game || page_prompt) return false;
    if (!waiting) return then != Then::Idle && then != Then::Door;
    if (vm->wait() != ecl::Wait::Print) return false;
    for (const char* t = vm->text(); *t; ++t)
        if (*t != ' ' && *t != '\n') return false;
    return true;
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
        *x = ((d->prof->variant.party_menu_col ? d->prof->variant.party_menu_col : 2) + 1) * 8 + 2;
        *y = (pm_row0() + pm_sel) * 8 + 2;
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
    bool box = false;                   // a frame around the area (the icon gallery's cells), not its letters
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
    if (screen == Screen::PartyMenu && r >= pm_row0() && r < pm_row0() + pm_lines) {
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
    if (fl.box) {
        c.fill(fl.x0, fl.y0, fl.w, 2, colour);
        c.fill(fl.x0, fl.y0 + fl.h - 2, fl.w, 2, colour);
        c.fill(fl.x0, fl.y0, 2, fl.h, colour);
        c.fill(fl.x0 + fl.w - 2, fl.y0, 2, fl.h, colour);
        return;
    }
    for (int j = 0; j < fl.h; ++j) {
        const uint8_t* o = fl.orig + static_cast<size_t>(j) * fl.w;
        for (int i = 0; i < fl.w; ++i) c.set(fl.x0 + i, fl.y0 + j, o[i] ? colour : 0);
    }
}

} // namespace

bool tap_highlight(int x, int y, pic::Canvas& c, int* y0, int* y1)
{
    tap_highlight_end(c);
    if (!d) return false;
    fl.box = false;
#if CYD_ICON_GALLERY
    // An icon gallery cell: a frame around it
    if (icon_gallery() && y < text::kMenuTapTop) {
        const int i = gal_cell_at(x, y), nav = gal_nav_at(x, y);
        if (i < 0 && nav < 0) return false;
        if (nav >= 0) gal_nav_box(nav, &fl.x0, &fl.y0, &fl.w, &fl.h);
        else gal_box(i, &fl.x0, &fl.y0, &fl.w, &fl.h);
        fl.orig = static_cast<uint8_t*>(malloc(static_cast<size_t>(fl.w) * fl.h));
        if (!fl.orig) return false;
        for (int j = 0; j < fl.h; ++j) c.get_row(fl.x0, fl.y0 + j, fl.w, fl.orig + static_cast<size_t>(j) * fl.w);
        fl.box = true;
        *y0 = fl.y0;
        *y1 = fl.y0 + fl.h;
        if (nav < 0 && i == ie->gsel) {
            ink(c, 0);                      // already lit: off, then on again
            return true;
        }
        ink(c, 15);
        fb_y0 = fl.y0;
        fb_y1 = fl.y0 + fl.h;
        fb_touched = false;
        return true;
    }
#endif
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
        uint8_t* p = fl.orig + static_cast<size_t>(j) * fl.w;
        c.get_row(fl.x0, fl.y0 + j, fl.w, p);
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
        for (int j = 0; j < fl.h; ++j) c.put_row(fl.x0, fl.y0 + j, fl.w, fl.orig + static_cast<size_t>(j) * fl.w);
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
    if (lk.on) {
        if (rom.asking) {
            // (the romance's "Accept Reject")
            if (y >= text::kMenuTapTop) {
                const int k = text::hit(menu, x / 8);
                if (k >= 0) rom_answer(k, c);
            }
            return;
        }
        lk_tap(c);
        return;
    }
    // (Darkness: the menu's place follows the word picked - facts L8)
    if (kept_hl() && menu_on && y >= text::kMenuTapTop && menu.count > 0) hl_picked(text::hit(menu, x / 8));
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
                t_last = plat::now_ms();
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
    // Pool's wilderness: a tap on the square north, east, south or west of
    // the rider steps there (as the arrow keys)
    if (pool_overland() && !pic_shown && x >= 8 && x < 128 && y >= 8 && y < 128 && ov_load()) {
        int col, row, x0, y0;
        ov_square(&col, &row);
        overland::window(col, row, &x0, &y0);
        const int dx = (x - 8) / 24 - (col - x0), dy = (y - 8) / 24 - (row - y0);
        const Act a = dx == 0 && dy < 0 ? Act::Forward : dx == 0 && dy > 0 ? Act::TurnAround
                    : dy == 0 && dx > 0 ? Act::StepRight : dy == 0 && dx < 0 ? Act::StepLeft : Act::Area;
        if (a != Act::Area) act(a, c);
        return;
    }
    // Gateway's wilderness: a tap on a square beside the party's arrow faces
    // that way, a tap on the square ahead steps there (its arrow keys turn
    // and step)
    if (gw_wild() && !pic_shown && !bigpic_shown() && x >= 24 && x < 112 && y >= 24 && y < 112) {
        const ecl::GameState& g = d->gs;
        const int x0 = g.x - 5 < 0 ? 0 : g.x - 5 > 5 ? 5 : g.x - 5;
        const int y0 = g.y - 5 < 0 ? 0 : g.y - 5 > 5 ? 5 : g.y - 5;
        const int dx = (x - 24) / 8 - (g.x - x0), dy = (y - 24) / 8 - (g.y - y0);
        const int to = dx == 0 && dy < 0 ? 0 : dx > 0 && dy == 0 ? 2 : dx == 0 && dy > 0 ? 4 : dx < 0 && dy == 0 ? 6 : -1;
        if (to < 0) return;
        const int turn = (to - (g.dir & 6) + 8) % 8;
        act(turn == 0 ? Act::Forward : turn == 2 ? Act::TurnRight : turn == 4 ? Act::TurnAround : Act::TurnLeft, c);
        return;
    }
    // A tap on a character in the party list selects them
    if (col >= 17 && row >= 4 && row < 4 + pt->count && !bigpic_shown()) {
        pt->selected = row - 4;
        draw_party(c, 17);
        return;
    }
    // The exploring menu: Area Cast View Encamp Search Look (Secret: Move first)
    if (y >= text::kMenuTapTop && (vm->get(0x4BE6) || pool_overland() || dk_overland() || gw_wild())) {
        const char k = text::key(menu, text::hit(menu, col));
        if (moving || (k == 'M' && move_menu())) {
            if (moving && k != 'E') return;
            moving = !moving;
            idle_menu(c);
            return;
        }
        switch (k) {
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
    tidy_memory();
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
            {
                // Cause Disease's course: "NAME is weakened", "NAME takes 1 point of damage from Magic"
                const rules::DiseaseStep ds = rules::disease_clock(pt->m[i], m, d->prof->cures);
                if (ds.weakened || ds.hurt) {
                    char nm[20], w[32], t[64];
                    pt->m[i].name(nm, sizeof nm);
                    if (ds.hurt) {
                        char w2[16];
                        ow(d->prof->fight.words[profile::kTakes1], w, sizeof w);
                        ow(d->prof->fight.words[profile::kFromMagic], w2, sizeof w2);
                        snprintf(t, sizeof t, "%s %s%s", nm, w, w2);
                    } else {
                        ow(d->prof->fight.words[profile::kIsWeakened], w, sizeof w);
                        snprintf(t, sizeof t, "%s %s", nm, w);
                    }
                    cv = &c;
                    note(c, t);
                }
            }
            // (the disease's own Strength step isn't undone by a recalculation now - the original's
            // doesn't recalculate there)
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
            {
                // Pool's mummy rot: "NAME Rots..." each month, "NAME Dies rots away"
                const combat::MonFx& mf = d->prof->fight.facts.mon;
                const rules::RotStep rs = rules::rot_clock(pt->m[i], m, mf.rot, mf.rot_clock);
                for (int k = 0; k < rs.rots + (rs.dies ? 1 : 0); ++k) {
                    char nm[20], w[24], t[48];
                    pt->m[i].name(nm, sizeof nm);
                    ow(d->prof->fight.words[k < rs.rots ? profile::kRotsW : profile::kRotsAway], w, sizeof w);
                    snprintf(t, sizeof t, "%s %s", nm, w);
                    cv = &c;
                    note(c, t);
                }
            }
            {
                // Pool's Manual of Bodily Health: "NAME starts to train", "NAME is hardier"
                const auto& pi = d->prof->items;
                const rules::ManualStep ms = rules::manual_clock(pt->m[i], m, pi.manual_fx[0], pi.manual_fx[1], d->facts);
                for (int k = 0; k < 2; ++k) {
                    if (!(k == 0 ? ms.trains : ms.hardier) || !pi.manual_words[k]) continue;
                    char nm[20], w[24], t[48];
                    pt->m[i].name(nm, sizeof nm);
                    ow(pi.manual_words[k], w, sizeof w);
                    snprintf(t, sizeof t, "%s %s", nm, w);
                    cv = &c;
                    note(c, t);
                }
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
            magic::tick_affects(pt->m[i], m, profile::view_facts(d->prof->game).paladin_cure);
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
    if (lk.on) {
        if (!rom.asking) lk_tick(now, c);
        return;
    }
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
    if (pic_shown || bigpic >= 0 || dk_overland()) vga_cycle_tick(now);    // (a VGA screen's colour cycling)
    const ecl::Wait wt = vm->wait();
    // While the game waits for the player: the event picture animates (at
    // a menu, as in the games) and the wilderness map's square blinks
    const bool asking = wt == ecl::Wait::Menu || (wt == ecl::Wait::ListMenu && list_wait) ||
                        wt == ecl::Wait::Number || wt == ecl::Wait::String;
    if (wt == ecl::Wait::Menu && anim_block >= 0 && d->anim.frames > 1) {
        const uint32_t delay = d->anim.delay[anim_frame] ? d->anim.delay[anim_frame] * 100 : 100;
        if (now - anim_at >= delay && anim_next(anim_frame) != anim_frame) {
            anim_frame = anim_next(anim_frame);
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
            alter_leave(c);
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
        load_where = false;
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
        if (view_yes_no(what, 'N', c) || items_yes_no(what, 'N', c) || shop_yes_no(what, 'N', c) ||
            train_yes_no(what, 'N', c) ||
            magic_yes_no(what, 'N', c) || alter_yes_no(what, 'N', c))
            return true;
        ask = what;
    }
    if (screen == Screen::ViewWho) {
        back_to_view(c);
        return true;
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

int fight_colours() { return d ? fight_palette_on : 0; }

int canvas_depth(games::Game g)
{
    // (the VGA games: Pools of Darkness, Treasures of the Savage Frontier -
    // profile variant.vga)
    return (g == games::Game::PoolsOfDarkness || g == games::Game::TreasuresOfTheSavageFrontier) && plat::vga_screen()
               ? 8
               : 4;
}

bool palette(const pic::Rgb** rgb, bool all)
{
    if (!vga || (!vga->changed && !all)) return false;
    vga->changed = false;
    *rgb = vga->now;
    return true;
}

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
        if (engine_ask == EngineAsk::BlastDelay) {
            engine_ask = EngineAsk::Name;
            blast_delay(atoi(input_buf), c);
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

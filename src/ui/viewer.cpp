#include "viewer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

#include "app/library.h"
#include "engine/dax.h"
#include "engine/font.h"
#include "engine/games.h"
#include "engine/icon.h"
#include "engine/inflate.h"
#include "engine/journal.h"
#include "engine/picture.h"
#include "engine/text.h"
#include "frame.h"
#include "hal/panel_prefs.h"
#include "hal/sdcard.h"
#include "look.h"
#include "play.h"
#include "walk.h"
#include "ui.h"

namespace viewer {

namespace {

enum class Screen : uint8_t { Home, Files, Blocks, View, Look, Walk, Play, Journal, Settings };

Env       env_;
Settings* cfg = nullptr;
Screen    screen = Screen::Home;
bool      dirty = true;            // redraw the current screen

// Home
library::GameDir   game_dirs[library::kMaxGames];
int                n_games = 0;
library::ScanResult scan_result = library::ScanResult::NoCard;
int                home_page = 0;
int                game_sel = 0;

// Files of the chosen game
char  files[library::kMaxFiles][library::kNameLen];
int   n_files = 0;
int   file_page = 0;
int   file_sel = 0;

// The open DAX file
fs::File           cur_file;
library::FileSource* src = nullptr;
alignas(library::FileSource) uint8_t src_mem[sizeof(library::FileSource)];
dax::Index         index_;
dax::Status        index_status = dax::Status::Ok;
pic::Header        pic_hdr[dax::kMaxEntries];   // picture, or an animation's first frame
bool               is_pic[dax::kMaxEntries];    // something to draw (picture or animation)
bool               is_anim[dax::kMaxEntries];   // an animation (PIC, SPRIT...); pic_hdr.frames = its frames
bool               is_vga[dax::kMaxEntries];    // a 256-colour picture (Pools of Darkness)
bool               is_font[dax::kMaxEntries];   // the game's 8x8 font (block 201 of an 8X8D file)
font::Font         cur_font;
pic::Anim          cur_anim;                    // the animation being viewed
int                cur_anim_block = -1;
int                block_page = 0;
int                block_sel = 0;    // entry number in index_

// Walk test
const char* walk_error = nullptr;
// Play test
const char* play_error = nullptr;

// Screen test
int         look_page = 0;
const char* look_error = nullptr;   // why it couldn't open

// Block view
int  frame_no = 0;
int  hex_page = 0;
bool show_info = false;    // 320x240: info line over the picture (middle tap)

// ---- helpers ---------------------------------------------------------------

struct Pager {
    int count, per_page, page;
    int pages() const { return count == 0 ? 1 : (count + per_page - 1) / per_page; }
    int first() const { return page * per_page; }
};

void go(Screen s)
{
    screen = s;
    dirty = true;
}

void close_file()
{
    if (src) {
        src->~FileSource();
        src = nullptr;
    }
    if (cur_file) cur_file.close();
}

int wrap_text(int x, int y, int w, const char* s, ui::Font font, uint16_t col, bool draw, int max_lines = 0);

void rescan();

bool open_file(int i)
{
    close_file();
    char path[160];
    library::path_of(game_dirs[game_sel].data_dir, files[i], path, sizeof path);
    cur_file = sd_fs().open(path, "r");
    if (!cur_file) {
        index_status = dax::Status::ReadError;
        index_.count = 0;
        return false;
    }
    src = new (src_mem) library::FileSource(cur_file);
    index_status = dax::read_index(*src, index_);
    for (int e = 0; e < index_.count; ++e) {
        is_pic[e] = is_anim[e] = is_vga[e] = is_font[e] = false;
        const dax::Entry& en = index_.entries[e];
        if (en.id == font::kBlockId && en.raw_size == font::kBlockBytes) {
            // Shown as a sheet of its glyphs plus a line of text
            is_pic[e] = is_font[e] = true;
            pic_hdr[e] = pic::Header{};
            pic_hdr[e].height = pic::kScreenH;
            pic_hdr[e].width_cols = pic::kScreenW / 8;
            pic_hdr[e].frames = 1;
            continue;
        }
        {
            dax::RleReader r(*src, index_, en);
            uint8_t hdr[pic::kHeaderSize];
            const size_t got = r.read(hdr, sizeof hdr);
            if (got == sizeof hdr) is_pic[e] = pic::parse_header(hdr, en.raw_size, pic_hdr[e]);
            pic::VgaHeader vh;
            if (!is_pic[e] && got >= pic::kVgaHeaderSize && pic::parse_vga_header(hdr, en.raw_size, vh)) {
                is_pic[e] = is_vga[e] = true;
                pic_hdr[e] = pic::Header{};
                pic_hdr[e].height = vh.height;
                pic_hdr[e].width_cols = vh.width_cols;
                pic_hdr[e].frames = vh.frames;
            }
        }
        if (!is_pic[e]) {
            dax::RleReader r(*src, index_, en);
            if (pic::parse_anim(r, en.raw_size, cur_anim)) {
                is_pic[e] = is_anim[e] = true;
                pic_hdr[e] = cur_anim.frame[0];
                pic_hdr[e].frames = static_cast<uint8_t>(cur_anim.frames);
            }
        }
    }
    cur_anim_block = -1;
    Serial.printf("[viewer] %s: %s, %d blocks\n", path, dax::status_text(index_status), index_.count);
    return index_status == dax::Status::Ok;
}

// Bottom row of n keys; returns which key a tap hit, or -1.
int bottom_hit(const ui::Tap& t, int n)
{
    for (int i = 0; i < n; ++i)
        if (ui::bottom_key(i, n).contains(t.x, t.y)) return i;
    return -1;
}

void draw_pager_keys(const Pager& p, const char* middle = nullptr)
{
    const int n = middle ? 3 : 2;
    ui::key(ui::bottom_key(0, n), "< Prev", p.page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    if (middle) ui::key(ui::bottom_key(1, n), middle);
    ui::key(ui::bottom_key(n - 1, n), "Next >", p.page + 1 < p.pages() ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
}

// Pager keys: returns true if the page changed (or middle key -> *middle_hit)
bool pager_tap(const ui::Tap& t, Pager& p, int* page, bool has_middle = false, bool* middle_hit = nullptr)
{
    const int n = has_middle ? 3 : 2;
    const int k = bottom_hit(t, n);
    if (k == 0 && p.page > 0) { --*page; return true; }
    if (k == n - 1 && p.page + 1 < p.pages()) { ++*page; return true; }
    if (has_middle && k == 1 && middle_hit) *middle_hit = true;
    return false;
}

// ---- Home ------------------------------------------------------------------

// One game a page (Tom: names are long, icons should be big): its GOG icon
// as large as fits (GOG's 256 px PNG scaled to 128 px on 320x240, 192 px on
// 480x320), the full title and its folder beside it. < > go through games.

// The whole space between the header and the bottom keys (Tom: no key
// look - one game a page, so use all of it)
ui::Rect home_card()
{
    const int top = ui::header_h() + ui::gap();
    return {0, top, ui::width(), ui::height() - ui::key_h() - ui::gap() * 2 - top};
}

// As big as fits (Tom: bigger is better): the card's height, at most half
// its width, at most the 256 px original
int icon_size(const ui::Rect& card)
{
    int sz = card.h - ui::gap() * 2;
    if (sz > card.w / 2) sz = card.w / 2;
    if (sz > icon::kMaxOut) sz = icon::kMaxOut;
    return sz < 16 ? 0 : sz;
}

// The icon, a row at a time, blended onto the card's colour; and / or
// written to the icon file in /GOLDBOX/_CYD/<folder>/ (made once, at the
// screen's size - Tom: decoding GOG's .dll every time was slow)
struct IconDraw {
    int x, y;
    uint8_t br, bg, bb, dim;
    bool draw;
    fs::File out;
    lgfx::rgb888_t line[icon::kMaxOut];
};

void icon_row(int y, const uint8_t* rgba, int w, void* ctx)
{
    IconDraw& d = *static_cast<IconDraw*>(ctx);
    if (d.out) d.out.write(rgba, static_cast<size_t>(w) * 4);
    if (!d.draw) return;
    for (int x = 0; x < w; ++x) {
        const uint8_t* p = rgba + x * 4;
        const int a = d.dim ? p[3] / 2 : p[3];
        d.line[x] = lgfx::rgb888_t((p[0] * a + d.br * (255 - a)) / 255, (p[1] * a + d.bg * (255 - a)) / 255,
                                   (p[2] * a + d.bb * (255 - a)) / 255);
    }
    ui::gfx().pushImage(d.x, d.y + y, w, 1, d.line);
}

void icon_file(const library::GameDir& g, int px, char* out, size_t cap)
{
    char name[24];
    snprintf(name, sizeof name, "ICON%d.BIN", px);
    library::cache_path(g, name, out, cap);
}

void icon_colours(IconDraw& d, ui::KeyStyle st)
{
    const uint16_t c = style::kBackground;
    d.br = ((c >> 11) & 31) * 255 / 31;
    d.bg = ((c >> 5) & 63) * 255 / 63;
    d.bb = (c & 31) * 255 / 31;
    d.dim = st == ui::KeyStyle::Dim;
}

// Decodes the game's icon from the player's own GOG file at px square:
// drawn at (x, y) when draw, saved as the icon file when save. False if
// there's none or it can't be read.
bool render_icon(const library::GameDir& g, int x, int y, int px, ui::KeyStyle st, bool draw, bool save)
{
    if (!g.icon[0]) return false;
    char path[160];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, g.icon);
    fs::File f = sd_fs().open(path, "r");
    if (!f) return false;
    library::FileSource src(f);
    icon::Found fo;
    bool ok = icon::find(src, fo);
    if (ok) {
        const uint32_t t0 = millis();
        uint8_t* window = fo.png ? static_cast<uint8_t*>(malloc(inflate::kWindow)) : nullptr;
        IconDraw* d = new (std::nothrow) IconDraw;
        ok = d && (!fo.png || window);
        char cache[160];
        icon_file(g, px, cache, sizeof cache);
        if (ok) {
            d->x = x;
            d->y = y;
            d->draw = draw;
            icon_colours(*d, st);
            if (save && library::make_cache_dirs(g)) {
                d->out = sd_fs().open(cache, "w");
                const uint8_t hdr[8] = {'I', 'C', 'N', '1', static_cast<uint8_t>(px), static_cast<uint8_t>(px >> 8),
                                        static_cast<uint8_t>(px), static_cast<uint8_t>(px >> 8)};
                if (d->out) d->out.write(hdr, sizeof hdr);
            }
            if (draw) ui::gfx().startWrite();
            ok = icon::render(src, fo, px, px, window, icon_row, d);
            if (draw) ui::gfx().endWrite();
            if (d->out) {
                d->out.close();
                if (!ok) sd_fs().remove(cache);
            }
        }
        delete d;
        free(window);
        Serial.printf("[library] icon %s: %dx%d%s -> %d px, %s, %lu ms\n", path, fo.w, fo.h, fo.png ? " png" : "", px,
                      ok ? "ok" : "failed", (unsigned long)(millis() - t0));
    }
    f.close();
    return ok;
}

// Draws the game's icon at (x, y), px square: from its icon file when there
// is one for this size, else decoded from the GOG file (and the icon file
// made, for next time - a card moved to a board with another screen size)
bool draw_icon(const library::GameDir& g, int x, int y, int px, ui::KeyStyle st)
{
    if (!g.icon[0]) return false;
    char cache[160];
    icon_file(g, px, cache, sizeof cache);
    fs::File f = sd_fs().open(cache, "r");
    if (f) {
        uint8_t hdr[8];
        bool ok = f.read(hdr, 8) == 8 && memcmp(hdr, "ICN1", 4) == 0 && (hdr[4] | hdr[5] << 8) == px &&
                  (hdr[6] | hdr[7] << 8) == px && f.size() == 8u + static_cast<size_t>(px) * px * 4;
        IconDraw* d = ok ? new (std::nothrow) IconDraw : nullptr;
        uint8_t* row = ok ? static_cast<uint8_t*>(malloc(static_cast<size_t>(px) * 4)) : nullptr;
        if (d && row) {
            d->x = x;
            d->y = y;
            d->draw = true;
            icon_colours(*d, st);
            ui::gfx().startWrite();
            for (int r = 0; r < px && ok; ++r) {
                ok = f.read(row, static_cast<size_t>(px) * 4) == static_cast<size_t>(px) * 4;
                if (ok) icon_row(r, row, px, d);
            }
            ui::gfx().endWrite();
        } else {
            ok = false;
        }
        free(row);
        delete d;
        f.close();
        if (ok) return true;
    }
    return render_icon(g, x, y, px, st, true, true);
}

// Prints text word-wrapped into width w from (x, y); returns the y after it.
// max_lines 0 = no limit; with draw false it only measures.
int wrap_text(int x, int y, int w, const char* s, ui::Font font, uint16_t col, bool draw, int max_lines)
{
    const int lh = ui::line_h(font);
    char line[96];
    int lines = 0;
    while (*s && (max_lines == 0 || lines < max_lines)) {
        int n = 0, cut = 0;
        while (s[n] && n < (int)sizeof line - 1) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (ui::text_width(line, font) > w) break;
            if (s[n] == ' ') cut = n;
            ++n;
        }
        if (s[n] && cut > 0) n = cut;
        if (n == 0) n = 1;
        line[n] = 0;
        if (draw) ui::text(x, y, line, col, font);
        y += lh;
        ++lines;
        s += n;
        while (*s == ' ') ++s;
    }
    return y;
}

// ---- Card scan ---------------------------------------------------------------
// Only at the first boot with a card (no saved library) and on Rescan Card
// (Tom, 2026-10-09). A scrolling list says what it finds and makes; the
// same lines go to /GOLDBOX/_CYD/SCAN.TXT.

struct ScanScreen {
    static constexpr int kLines = 32;
    char     line[kLines][100];
    int      n = 0;
    fs::File log;
    char     pending[100] = {};      // the last line, written to the log once it's final
    int      last_parts = 0;         // screen lines the last one took (wrapped)
};
ScanScreen* scr = nullptr;

int scan_lh() { return ui::line_h(ui::Font::Small) + 3; }
int scan_top() { return ui::header_h() + ui::gap() * 2; }
int scan_rows() { return (ui::height() - scan_top() - ui::gap()) / scan_lh(); }

void scan_draw_row(int i)       // line i (of those kept) in its place on screen
{
    const int first = scr->n > scan_rows() ? scr->n - scan_rows() : 0;
    const int y = scan_top() + (i - first) * scan_lh();
    ui::gfx().fillRect(0, y, ui::width(), scan_lh(), style::kBackground);
    const bool last = i == scr->n - 1;
    ui::text(ui::gap() * 3, y, scr->line[i], last ? style::kText : style::kTextMuted, ui::Font::Small);
}

void scan_draw_all()
{
    ui::gfx().fillRect(0, scan_top(), ui::width(), ui::height() - scan_top(), style::kBackground);
    const int first = scr->n > scan_rows() ? scr->n - scan_rows() : 0;
    for (int i = first; i < scr->n; ++i) scan_draw_row(i);
}

void scan_flush_log()
{
    if (scr->log && scr->pending[0]) scr->log.printf("%s\n", scr->pending);
    scr->pending[0] = 0;
}

// One line of the list (wrapped if it's too wide); replace = it takes the
// place of the last one ("Looking in CURSE..." -> "Found Curse of ...")
void scan_say(const char* text, bool replace, void*)
{
    if (!scr) return;
    if (replace && scr->n > 0) {
        scr->n -= scr->last_parts < scr->n ? scr->last_parts : scr->n;
    } else {
        scan_flush_log();
    }
    strlcpy(scr->pending, text, sizeof scr->pending);
    const int w = ui::width() - ui::gap() * 6;
    const char* s = text;
    bool cont = false;
    scr->last_parts = 0;
    while (*s) {
        char part[100];
        int n = 0, cut = 0;
        if (cont) part[n++] = ' ', part[n++] = ' ';
        const int start = n;
        while (s[n - start] && n < (int)sizeof part - 1) {
            part[n] = s[n - start];
            part[n + 1] = 0;
            if (ui::text_width(part, ui::Font::Small) > w) break;
            if (part[n] == ' ') cut = n;
            ++n;
        }
        if (s[n - start] && cut > start) n = cut;
        part[n] = 0;
        s += n - start;
        while (*s == ' ') ++s;
        if (scr->n == ScanScreen::kLines) {
            memmove(scr->line[0], scr->line[1], sizeof scr->line[0] * (ScanScreen::kLines - 1));
            --scr->n;
        }
        strlcpy(scr->line[scr->n++], part, sizeof scr->line[0]);
        ++scr->last_parts;
        cont = true;
    }
    // Redraw: the whole list when it scrolled, else the newest lines
    if (scr->n > scan_rows() || replace) scan_draw_all();
    else for (int i = 0; i < scr->n; ++i) scan_draw_row(i);
}

void rescan()
{
    scr = new (std::nothrow) ScanScreen;
    ui::clear();
    ui::header("Scanning Your Card", false);
    close_file();
    sd_lost();               // forget a card that was pulled; sd_begin retries
    if (scr && sd_begin()) {
        char path[160];
        snprintf(path, sizeof path, "%s/%s", games::kRootDir, library::kCacheDir);
        if (sd_fs().exists(games::kRootDir) && (sd_fs().exists(path) || sd_fs().mkdir(path))) {
            library::cache_path("SCAN.TXT", path, sizeof path);
            scr->log = sd_fs().open(path, "w");
            if (scr->log) scr->log.printf("CYD BYOG Gold Box Engine %s (%s), %s - card scan\n\n", env_.version, env_.build, BOARD_NAME);
        }
    }
    scan_result = library::scan(game_dirs, library::kMaxGames, &n_games, scan_say, nullptr);
    home_page = 0;

    // What the board makes from the player's files, once: the icons at
    // this screen's size
    const int px = icon_size(home_card());
    for (int i = 0; i < n_games; ++i) {
        const library::GameDir& g = game_dirs[i];
        if (!g.icon[0] || !px) continue;
        scan_say("Preparing the game icon...", false, nullptr);
        char line[100];
        const bool ok = render_icon(g, 0, 0, px, ui::KeyStyle::Normal, false, true);
        snprintf(line, sizeof line, ok ? "Prepared the %s icon" : "The %s icon couldn't be read", games::short_title(g.game));
        scan_say(line, true, nullptr);
    }
    if (scan_result == library::ScanResult::Ok) {
        scan_say(library::save_library(game_dirs, n_games) ? "Saved the library: the board won't scan again until you tap Rescan Card."
                                                           : "The library couldn't be saved on the card.",
                 false, nullptr);
    }
    scan_say("Done.", false, nullptr);
    if (scr) {
        scan_flush_log();
        if (scr->log) scr->log.close();
        delay(1500);         // a moment to read the end of the list
    }
    delete scr;
    scr = nullptr;
}

void draw_home()
{
    ui::clear();
    char title[48];
    snprintf(title, sizeof title, "Gold Box Library  %s", env_.version);
    ui::header(title, false);
    if (scan_result != library::ScanResult::Ok || n_games == 0) {
        const int x = ui::gap() * 3;
        int y = ui::header_h() + ui::gap() * 3;
        const int lh = ui::line_h() + 4;
        const char* first = scan_result == library::ScanResult::NoCard ? "No SD card found."
                          : scan_result == library::ScanResult::NoRootFolder ? "No GOLDBOX folder on the card."
                          : "No game files in GOLDBOX.";
        ui::text(x, y, first, style::kGold);
        y += lh * 3 / 2;
        ui::text(x, y, "Copy each game's folder from your GOG", style::kText);
        y += lh;
        ui::text(x, y, "install to a FAT32 microSD card:", style::kText);
        y += lh;
        ui::text(x, y, "/GOLDBOX/POOLRAD   /GOLDBOX/CURSE", style::kGold);
        y += lh;
        ui::text(x, y, "/GOLDBOX/SECRET    /GOLDBOX/DARKNESS", style::kGold);
        y += lh;
        ui::text(x, y, "then tap Rescan Card.", style::kText);
    } else {
        if (home_page >= n_games) home_page = n_games - 1;
        const library::GameDir& g = game_dirs[home_page];
        const ui::Rect card = home_card();
        const bool hlib = g.format == library::Format::Hlib;
        const ui::KeyStyle st = hlib ? ui::KeyStyle::Dim : ui::KeyStyle::Normal;
        const int gp = ui::gap();
        const int ipx = g.icon[0] ? icon_size(card) : 0;
        const int ix = card.x + gp * 2, iy = card.y + (card.h - ipx) / 2;
        const int tx = ipx ? ix + ipx + gp * 3 : card.x + gp * 4;
        const int tw = card.x + card.w - gp * 2 - tx;      // text stays inside this box

        // The text block, centred top to bottom; every line wraps
        char l1[48], l2[48], l3[48];
        snprintf(l1, sizeof l1, "Folder: %s", g.folder);
        if (hlib) snprintf(l2, sizeof l2, "Newer format");
        else snprintf(l2, sizeof l2, "%d game files", g.dax_files);
        snprintf(l3, sizeof l3, "Game %d of %d", home_page + 1, n_games);
        const uint16_t tcol = hlib ? style::kTextMuted : style::kText;
        const ui::Font sf = ui::Font::Small;
        const int title_h = wrap_text(tx, 0, tw, games::title(g.game), ui::Font::Large, tcol, false, 4);
        const int sub_h = wrap_text(tx, 0, tw, l1, sf, 0, false) + wrap_text(tx, 0, tw, l2, sf, 0, false) +
                          wrap_text(tx, 0, tw, l3, sf, 0, false);
        int y = card.y + (card.h - (title_h + gp * 2 + sub_h + gp * 2)) / 2;
        if (y < card.y) y = card.y;
        y = wrap_text(tx, y, tw, games::title(g.game), ui::Font::Large, tcol, true, 4) + gp * 2;
        y = wrap_text(tx, y, tw, l1, sf, style::kTextMuted, true) + gp;
        y = wrap_text(tx, y, tw, l2, sf, style::kTextMuted, true) + gp;
        wrap_text(tx, y, tw, l3, sf, style::kTextMuted, true);
        if (ipx) draw_icon(g, ix, iy, ipx, st);
    }
    const bool more = n_games > 1;
    ui::key(ui::bottom_key(0, more ? 4 : 2), "Rescan\nCard");
    ui::key(ui::bottom_key(1, more ? 4 : 2), "Settings");
    if (more) {
        ui::key(ui::bottom_key(2, 4), "<", home_page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
        ui::key(ui::bottom_key(3, 4), ">", home_page + 1 < n_games ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    }
}

void tap_home(const ui::Tap& t)
{
    const bool more = n_games > 1;
    const int k = bottom_hit(t, more ? 4 : 2);
    if (k == 0) { rescan(); dirty = true; return; }
    if (k == 1) { go(Screen::Settings); return; }
    if (k == 2 && home_page > 0) { --home_page; dirty = true; return; }
    if (k == 3 && home_page + 1 < n_games) { ++home_page; dirty = true; return; }
    if (scan_result != library::ScanResult::Ok || n_games == 0) return;
    if (home_card().contains(t.x, t.y)) {
        game_sel = home_page;
        n_files = game_dirs[game_sel].format == library::Format::Dax
                ? library::list_dax(game_dirs[game_sel].data_dir, files, library::kMaxFiles) : 0;
        file_page = 0;
        go(Screen::Files);
    }
}

// ---- Files -----------------------------------------------------------------

int file_cols() { return ui::large() ? 4 : 3; }
constexpr int kFileRows = 4;

void draw_files()
{
    ui::clear();
    Pager p{n_files, file_cols() * kFileRows, file_page};
    char title[80];
    snprintf(title, sizeof title, "%s  %d/%d", games::short_title(game_dirs[game_sel].game), p.page + 1, p.pages());
    ui::header(title, true);
    if (game_dirs[game_sel].format == library::Format::Hlib) {
        // The Dark Queen of Krynn and Unlimited Adventures
        const int x = ui::gap() * 3;
        int y = ui::header_h() + ui::gap() * 3;
        const int lh = ui::line_h() + 4;
        ui::text(x, y, games::title(game_dirs[game_sel].game), style::kGold);
        y += lh * 3 / 2;
        ui::text(x, y, "Its files are .TLB / .GLB libraries,", style::kText);
        y += lh;
        ui::text(x, y, "a newer format the viewer", style::kText);
        y += lh;
        ui::text(x, y, "can't read yet.", style::kText);
    }
    for (int i = 0; i < p.per_page && p.first() + i < n_files; ++i) {
        // Every file here is a .DAX: show the name without it, so it fits
        char label[library::kNameLen];
        strlcpy(label, files[p.first() + i], sizeof label);
        const size_t n = strlen(label);
        if (n > 4 && strcasecmp(label + n - 4, ".DAX") == 0) label[n - 4] = 0;
        ui::key(ui::grid_cell(i, file_cols(), kFileRows), label);
    }
    if (look::available(game_dirs[game_sel].game)) {
        // < Prev | Screen Test | Walk Test | Play Test | Next >
        ui::key(ui::bottom_key(0, 5), "< Prev", p.page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
        ui::key(ui::bottom_key(1, 5), "Screen\nTest");
        ui::key(ui::bottom_key(2, 5), "Walk\nTest");
        ui::key(ui::bottom_key(3, 5), "Play\nTest");
        ui::key(ui::bottom_key(4, 5), "Next >", p.page + 1 < p.pages() ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    } else {
        draw_pager_keys(p);
    }
}

void tap_files(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) { go(Screen::Home); return; }
    Pager p{n_files, file_cols() * kFileRows, file_page};
    const bool has_look = look::available(game_dirs[game_sel].game);
    bool look_hit = false, walk_hit = false, play_hit = false;
    if (has_look) {
        const int k = bottom_hit(t, 5);
        if (k == 0 && p.page > 0) { --file_page; dirty = true; return; }
        if (k == 4 && p.page + 1 < p.pages()) { ++file_page; dirty = true; return; }
        look_hit = k == 1;
        walk_hit = k == 2;
        play_hit = k == 3;
    } else if (pager_tap(t, p, &file_page)) {
        dirty = true;
        return;
    }
    if (play_hit) {
        frame::set_scale(frame::Scale::One);    // the game's screen: 1:1 at the top left
        frame::set_left(true);
        frame::set_ega_palette();
        play_error = play::open(game_dirs[game_sel].data_dir, game_dirs[game_sel].game, frame::canvas());
        go(Screen::Play);
        return;
    }
    if (walk_hit) {
        walk_error = walk::open(game_dirs[game_sel].data_dir, game_dirs[game_sel].game);
        frame::set_scale(frame::Scale::One);    // the game's screen: 1:1 at the top left
        frame::set_left(true);
        go(Screen::Walk);
        return;
    }
    if (look_hit) {
        look_error = look::open(game_dirs[game_sel].data_dir, game_dirs[game_sel].game);
        look_page = 0;
        frame::set_scale(frame::Scale::One);    // the game's screen: always 1:1
        go(Screen::Look);
        return;
    }
    for (int i = 0; i < p.per_page && p.first() + i < n_files; ++i) {
        if (ui::grid_cell(i, file_cols(), kFileRows).contains(t.x, t.y)) {
            file_sel = p.first() + i;
            open_file(file_sel);
            block_page = 0;
            go(Screen::Blocks);
            return;
        }
    }
}

// ---- Blocks ----------------------------------------------------------------

int block_cols() { return ui::large() ? 5 : 4; }
constexpr int kBlockRows = 4;

void draw_blocks()
{
    ui::clear();
    Pager p{index_.count, block_cols() * kBlockRows, block_page};
    char title[80];
    snprintf(title, sizeof title, "%s  %d blocks  %d/%d", files[file_sel], index_.count, p.page + 1, p.pages());
    ui::header(title, true);
    if (index_status != dax::Status::Ok) {
        ui::text(ui::gap() * 3, ui::header_h() + ui::gap() * 3, dax::status_text(index_status), style::kWarn);
    }
    for (int i = 0; i < p.per_page && p.first() + i < index_.count; ++i) {
        const int e = p.first() + i;
        const dax::Entry& en = index_.entries[e];
        char label[16], sub[24];
        snprintf(label, sizeof label, "#%u", en.id);
        if (is_font[e]) {
            snprintf(sub, sizeof sub, "Font");
        } else if (is_pic[e]) {
            snprintf(sub, sizeof sub, "%dx%d x%d", pic_hdr[e].width_px(), pic_hdr[e].height, pic_hdr[e].frames);
        } else {
            snprintf(sub, sizeof sub, "%u B", en.raw_size);
        }
        ui::key2(ui::grid_cell(i, block_cols(), kBlockRows), label, sub,
                 is_pic[e] ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    }
    draw_pager_keys(p);
}

void tap_blocks(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) {
        close_file();
        go(Screen::Files);
        return;
    }
    Pager p{index_.count, block_cols() * kBlockRows, block_page};
    if (pager_tap(t, p, &block_page)) { dirty = true; return; }
    for (int i = 0; i < p.per_page && p.first() + i < index_.count; ++i) {
        if (ui::grid_cell(i, block_cols(), kBlockRows).contains(t.x, t.y)) {
            block_sel = p.first() + i;
            frame_no = 0;
            hex_page = 0;
            go(Screen::View);
            return;
        }
    }
}

// ---- Block view ------------------------------------------------------------

// Keys under the picture: on 320x240 and 480x320 at 1:1. At 1.5x the
// picture fills 480x300 and the picture itself is the control: left third
// = previous, right third = next, middle = back.
bool view_has_keys() { return frame::area().y + frame::area().h + ui::key_h() <= ui::height(); }

ui::Rect view_key(int i)
{
    const ui::Rect a = frame::area();
    const int top = a.y + a.h + 2;
    const int gp = ui::gap();
    const int h = ui::height() - top - gp;
    const int kh = h > ui::key_h() ? ui::key_h() : h;
    const int w = (ui::width() - gp * 4) / 3;
    return {gp + i * (w + gp), ui::height() - kh - gp, w, kh};
}

void view_info(char* out, size_t cap)
{
    const dax::Entry& en = index_.entries[block_sel];
    if (is_font[block_sel]) {
        snprintf(out, cap, "%s #%u  the game's font: %d glyphs of 8x8", files[file_sel], en.id, font::kGlyphs);
    } else if (is_anim[block_sel] && cur_anim_block == block_sel) {
        const pic::Header& h = cur_anim.frame[frame_no];
        snprintf(out, cap, "%s #%u  %dx%d  frame %d/%d  at %u,%u  delay %lu", files[file_sel], en.id, h.width_px(),
                 h.height, frame_no + 1, cur_anim.frames, h.x_cell, h.y_cell, (unsigned long)cur_anim.delay[frame_no]);
    } else if (is_pic[block_sel]) {
        const pic::Header& h = pic_hdr[block_sel];
        snprintf(out, cap, "%s #%u  %dx%d  frame %d/%d  at %u,%u", files[file_sel], en.id, h.width_px(), h.height,
                 frame_no + 1, h.frames, h.x_cell, h.y_cell);
    } else {
        snprintf(out, cap, "%s #%u  %u bytes (%u packed)", files[file_sel], en.id, en.raw_size, en.comp_size);
    }
}

int hex_bytes_per_line() { return ui::large() ? 16 : 12; }
int hex_lines()
{
    const int top = ui::header_h() + ui::gap();
    const int bottom = ui::height() - ui::key_h() - ui::gap() * 2;
    return (bottom - top) / 10;
}

void draw_hex()
{
    ui::clear();
    char title[128];
    view_info(title, sizeof title);
    ui::header(title, true);
    const dax::Entry& en = index_.entries[block_sel];
    const int per_line = hex_bytes_per_line(), lines = hex_lines();
    const int per_page = per_line * lines;
    const int pages = en.raw_size == 0 ? 1 : (en.raw_size + per_page - 1) / per_page;
    if (hex_page >= pages) hex_page = 0;
    dax::RleReader r(*src, index_, en);
    r.skip(static_cast<size_t>(hex_page) * per_page);
    const int top = ui::header_h() + ui::gap();
    for (int l = 0; l < lines; ++l) {
        uint8_t b[16];
        const size_t got = r.read(b, per_line);
        if (got == 0) break;
        char line[96];
        int o = snprintf(line, sizeof line, "%04X ", hex_page * per_page + l * per_line);
        for (int i = 0; i < per_line; ++i)
            o += (i < (int)got) ? snprintf(line + o, sizeof line - o, "%02X ", b[i]) : snprintf(line + o, sizeof line - o, "   ");
        for (size_t i = 0; i < got; ++i) line[o++] = (b[i] >= 32 && b[i] < 127) ? static_cast<char>(b[i]) : '.';
        line[o] = 0;
        ui::text(ui::gap(), top + l * 10, line, style::kText, ui::Font::Mono);
    }
    char pg[24];
    snprintf(pg, sizeof pg, "Page %d/%d", hex_page + 1, pages);
    ui::key(ui::bottom_key(0, 3), "< Block");
    ui::key(ui::bottom_key(1, 3), pg, pages > 1 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    ui::key(ui::bottom_key(2, 3), "Block >");
}

// The font: every glyph in a grid (glyph number = row * 20 + column), then
// some text printed with it
void draw_font_sheet(pic::Canvas& c)
{
    c.clear(0);
    for (int g = 0; g < font::kGlyphs; ++g) {
        const int x = 2 + (g % 20) * 16, y = 2 + (g / 20) * 16;
        c.fill(x - 1, y - 1, 10, 10, 1);
        font::draw_glyph(c, cur_font, g, x, y, 15, 1);
    }
    font::draw_text(c, cur_font, "THE QUICK BROWN FOX JUMPS OVER", 1, 20, 14, 0);
    font::draw_text(c, cur_font, "THE LAZY DOG. 0123456789 !?:,'\"-", 1, 21, 14, 0);
    font::draw_text(c, cur_font, "Mixed Case Prints In Capitals", 1, 23, 11, 0);
}

// PIC and FINAL files store animation frames as changes from the first one
bool xor_frames()
{
    return strncasecmp(files[file_sel], "PIC", 3) == 0 || strncasecmp(files[file_sel], "FINAL", 5) == 0;
}

void draw_picture()
{
    pic::Canvas& c = frame::canvas();
    c.clear(0);
    const pic::Header& h = pic_hdr[block_sel];
    const int x = (pic::kScreenW - h.width_px()) / 2, y = (pic::kScreenH - h.height) / 2;
    bool ok;
    frame::set_ega_palette();
    if (is_font[block_sel]) {
        ok = font::load(*src, index_, cur_font);
        if (ok) draw_font_sheet(c);
    } else if (is_vga[block_sel]) {
        // Its own palette: entries it doesn't set stay black
        static pic::Rgb pal[256];
        for (auto& p : pal) p = pic::Rgb{0, 0, 0};
        dax::RleReader r(*src, index_, index_.entries[block_sel]);
        uint8_t hdr[pic::kVgaHeaderSize];
        pic::VgaHeader vh;
        ok = r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_vga_header(hdr, index_.entries[block_sel].raw_size, vh) &&
             pic::read_vga_palette(r, vh, pal);
        for (int i = 0; i < 256; ++i) frame::set_palette(i, pal[i]);
        if (ok) {
            dax::RleReader r2(*src, index_, index_.entries[block_sel]);
            ok = pic::draw_vga(r2, vh, frame_no, c, x, y);
        }
    } else if (is_anim[block_sel]) {
        if (cur_anim_block != block_sel) {
            dax::RleReader r(*src, index_, index_.entries[block_sel]);
            pic::parse_anim(r, index_.entries[block_sel].raw_size, cur_anim);
            cur_anim_block = block_sel;
        }
        // Frames keep their positions relative to the first frame
        const pic::Header& f0 = cur_anim.frame[0];
        const pic::Header& fh = cur_anim.frame[frame_no];
        const int fx = x + (fh.x_cell - f0.x_cell) * 8, fy = y + (fh.y_cell - f0.y_cell) * 8;
        ok = pic::draw_anim(*src, index_, index_.entries[block_sel], cur_anim, frame_no, xor_frames(), c, fx, fy);
    } else {
        dax::RleReader r(*src, index_, index_.entries[block_sel]);
        r.skip(pic::kHeaderSize);
        ok = pic::draw(r, h, frame_no, c, x, y);
    }
    ui::clear();
    frame::present();

    char info[128];
    view_info(info, sizeof info);
    if (!ok) strlcat(info, "  (data short)", sizeof info);
    const ui::Rect a = frame::area();
    if (view_has_keys()) {
        const ui::Rect k0 = view_key(0);
        if (k0.y - (a.y + a.h) >= ui::line_h(ui::Font::Small) + 4) {
            ui::text(ui::gap(), a.y + a.h + 3, info, style::kTextMuted, ui::Font::Small);
        } else if (show_info) {
            // No room under the picture: a bar over its top
            const int lh = ui::line_h(ui::Font::Small) + 4;
            ui::gfx().fillRect(a.x, a.y, a.w, lh, style::kHeader);
            ui::text(a.x + 2, a.y + 2, info, style::kText, ui::Font::Small);
        }
        ui::key(view_key(0), "< Prev");
        ui::key(view_key(1), "Back");
        ui::key(view_key(2), "Next >");
    } else {
        ui::text(ui::gap(), a.y + a.h + 3, info, style::kTextMuted, ui::Font::Small);
    }
}

void draw_view()
{
    if (!src || block_sel >= index_.count) { go(Screen::Blocks); return; }
    if (is_pic[block_sel]) draw_picture(); else draw_hex();
}

// Step to the previous / next frame, then block
void step(int dir)
{
    if (is_pic[block_sel]) {
        const int nf = frame_no + dir;
        if (nf >= 0 && nf < pic_hdr[block_sel].frames) { frame_no = nf; dirty = true; return; }
    }
    const int nb = block_sel + dir;
    if (nb < 0 || nb >= index_.count) return;
    block_sel = nb;
    hex_page = 0;
    frame_no = dir < 0 && is_pic[nb] ? pic_hdr[nb].frames - 1 : 0;
    dirty = true;
}

void back_to_blocks()
{
    block_page = block_sel / (block_cols() * kBlockRows);
    go(Screen::Blocks);
}

void tap_view(const ui::Tap& t)
{
    if (!is_pic[block_sel]) {
        if (ui::back_rect().contains(t.x, t.y)) { back_to_blocks(); return; }
        const int k = bottom_hit(t, 3);
        if (k == 0) step(-1);
        if (k == 2) step(1);
        if (k == 1) { ++hex_page; dirty = true; }
        return;
    }
    if (view_has_keys()) {
        if (view_key(0).contains(t.x, t.y)) { step(-1); return; }
        if (view_key(1).contains(t.x, t.y)) { back_to_blocks(); return; }
        if (view_key(2).contains(t.x, t.y)) { step(1); return; }
    }
    const ui::Rect a = frame::area();
    if (!a.contains(t.x, t.y)) return;
    const int third = (t.x - a.x) * 3 / a.w;
    if (third == 0) step(-1);
    else if (third == 2) step(1);
    else if (!view_has_keys()) back_to_blocks();
    else { show_info = !show_info; dirty = true; }
}

// ---- Screen test -----------------------------------------------------------

void draw_look()
{
    if (look_error) {
        ui::clear();
        ui::header("Screen Test", true);
        // The message, wrapped at spaces to the screen width
        const int x = ui::gap() * 3, maxw = ui::width() - x * 2;
        int y = ui::header_h() + ui::gap() * 3;
        const char* s = look_error;
        while (*s) {
            char line[96];
            int n = 0, cut = 0;
            while (s[n] && n < (int)sizeof line - 1) {
                line[n] = s[n];
                line[n + 1] = 0;
                if (ui::text_width(line) > maxw) break;
                if (s[n] == ' ') cut = n;
                ++n;
            }
            if (s[n] && cut > 0) n = cut;
            line[n] = 0;
            ui::text(x, y, line, style::kText);
            y += ui::line_h() + 4;
            s += n;
            while (*s == ' ') ++s;
        }
        return;
    }
    frame::set_ega_palette();
    look::enter(look_page, frame::canvas());
    ui::clear();
    frame::present();
    char info[128];
    snprintf(info, sizeof info, "%d/%d  %s  (%s)", look_page + 1, look::pages(), look::page_name(look_page),
             look::source());
    const ui::Rect a = frame::area();
    if (view_has_keys()) {
        if (view_key(0).y - (a.y + a.h) >= ui::line_h(ui::Font::Small) + 4)
            ui::text(ui::gap(), a.y + a.h + 3, info, style::kTextMuted, ui::Font::Small);
        ui::key(view_key(0), "< Prev", look_page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
        ui::key(view_key(1), "Back");
        ui::key(view_key(2), "Next >", look_page + 1 < look::pages() ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    } else {
        ui::text(ui::gap(), a.y + a.h + 3, info, style::kTextMuted, ui::Font::Small);
    }
}

void leave_look()
{
    look::close();
    look_error = nullptr;
    // The game screen is always 1:1 here; back to the viewer's own choice
    frame::set_scale(cfg->scale_15x ? frame::Scale::OneAndHalf : frame::Scale::One);
    go(Screen::Files);
}

void present(const look::Rows& r)
{
    if (r.y1 > r.y0) frame::present_rows(r.y0, r.y1);
}

void look_step(int dir)
{
    const int np = look_page + dir;
    if (np >= 0 && np < look::pages()) { look_page = np; dirty = true; }
}

void tap_look(const ui::Tap& t)
{
    if (look_error) {
        if (ui::back_rect().contains(t.x, t.y)) leave_look();
        return;
    }
    if (view_has_keys()) {
        if (view_key(0).contains(t.x, t.y)) { look_step(-1); return; }
        if (view_key(1).contains(t.x, t.y)) { leave_look(); return; }
        if (view_key(2).contains(t.x, t.y)) { look_step(1); return; }
    }
    // Taps on the game screen go to the page (title: next picture; text:
    // go on, menu line)
    int cx, cy;
    if (frame::to_canvas(t.x, t.y, cx, cy)) present(look::tap(cx, cy, millis(), frame::canvas()));
}

// ---- Walk test --------------------------------------------------------------

// Controls under the game screen (SPEC section 4). 320x240: one row of 8
// keys. 480x320: a 3x3 pad like a numeric keypad (7 / 9 turn, 8 forward,
// 4 / 6 side-step, 2 turn around) with Area / Next Map / Esc beside it.
enum WalkKey { kWTurnL, kWStepL, kWFwd, kWStepR, kWTurnR, kWAround, kWArea, kWNext, kWEsc, kWKeys };

ui::Rect walk_key(int k)
{
    const int gp = ui::gap();
    const int top = pic::kScreenH + gp;
    const int h_all = ui::height() - top - gp;
    if (!ui::large()) {
        // Row of 8: StepL TurnL Fwd TurnR StepR Around Area Esc (Next Map: menu / panel tap)
        static const int kOrder[kWKeys] = {1, 0, 2, 4, 3, 5, 6, -1, 7};
        const int i = kOrder[k];
        if (i < 0) return ui::Rect{};
        const int w = (pic::kScreenW - gp * 9) / 8;
        return {gp + i * (w + gp), top, w, h_all};
    }
    const int kh = (h_all - gp * 2) / 3;
    const int pad_w = 210, kw = (pad_w - gp * 4) / 3;
    auto pad = [&](int col, int row) { return ui::Rect{gp + col * (kw + gp), top + row * (kh + gp), kw, kh}; };
    const int rx = pad_w + gp, rw = pic::kScreenW - rx - gp;
    auto side = [&](int row) { return ui::Rect{rx, top + row * (kh + gp), rw, kh}; };
    switch (k) {
    case kWTurnL:  return pad(0, 0);
    case kWFwd:    return pad(1, 0);
    case kWTurnR:  return pad(2, 0);
    case kWStepL:  return pad(0, 1);
    case kWStepR:  return pad(2, 1);
    case kWAround: return pad(1, 2);
    case kWArea:   return side(0);
    case kWNext:   return side(1);
    case kWEsc:    return side(2);
    }
    return ui::Rect{};
}

// Where the Companion strip draws the map (480x320)
constexpr int kCompCell = 9, kCompMapY = 70;
int comp_map_x() { return pic::kScreenW + (ui::width() - pic::kScreenW - kCompCell * geo::kSize) / 2; }

// What the Companion strip shows: the Walk Test's or the Play Test's party
struct MapSource {
    const geo::Map* (*map)();
    int (*x)();
    int (*y)();
    int (*dir)();
    void (*describe)(char*, char*, int);
    bool teleport;      // a tap on a square moves the party there (Walk Test)
};
const MapSource kWalkMap{walk::map, walk::pos_x, walk::pos_y, walk::dir, walk::describe, true};
const MapSource kPlayMap{play::map, play::pos_x, play::pos_y, play::dir, play::describe, false};

// The Gold Box Companion strip (480x320): the whole map, the party arrow
void draw_companion(const MapSource& ms = kWalkMap)
{
    if (!ui::large()) return;
    LGFX& g = ui::gfx();
    const int x0 = pic::kScreenW, w = ui::width() - x0;
    g.fillRect(x0, 0, w, ui::height(), style::kBackground);
    g.drawFastVLine(x0, 0, ui::height(), style::kKeyEdge);
    char l1[48], l2[48];
    ms.describe(l1, l2, sizeof l1);
    ui::text(x0 + 8, 6, "Map", style::kGold);
    ui::text(x0 + 8, 30, l1, style::kText, ui::Font::Small);
    ui::text(x0 + 8, 46, l2, style::kTextMuted, ui::Font::Small);
    const geo::Map* m = ms.map();
    if (!m) return;
    const int cell = kCompCell, mx = comp_map_x(), my = kCompMapY;
    g.fillRect(mx, my, cell * geo::kSize + 1, cell * geo::kSize + 1, style::kKey);
    for (int y = 0; y < geo::kSize; ++y)
        for (int x = 0; x < geo::kSize; ++x) {
            const int sx = mx + x * cell, sy = my + y * cell;
            for (int d = 0; d < 8; d += 2) {
                if (!geo::wall(*m, x, y, d)) continue;
                const int door = geo::door(*m, x, y, d);
                const uint16_t col = door == 1 ? style::kGold : door >= 2 ? style::kWarn : style::kText;
                if (d == 0) g.drawFastHLine(sx, sy, cell + 1, col);
                if (d == 4) g.drawFastHLine(sx, sy + cell, cell + 1, col);
                if (d == 6) g.drawFastVLine(sx, sy, cell + 1, col);
                if (d == 2) g.drawFastVLine(sx + cell, sy, cell + 1, col);
            }
        }
    // The party: a triangle pointing the way it faces
    const int cx = mx + ms.x() * cell + cell / 2, cy = my + ms.y() * cell + cell / 2;
    const int r = cell / 2 - 1;
    const int dir = ms.dir();
    const int fx = cx + geo::dx(dir) * r, fy = cy + geo::dy(dir) * r;
    const int lx = cx + geo::dx((dir + 6) & 7) * r - geo::dx(dir) * r, ly = cy + geo::dy((dir + 6) & 7) * r - geo::dy(dir) * r;
    const int rx = cx + geo::dx((dir + 2) & 7) * r - geo::dx(dir) * r, ry = cy + geo::dy((dir + 2) & 7) * r - geo::dy(dir) * r;
    g.fillTriangle(fx, fy, lx, ly, rx, ry, style::kGold);
    ui::text(x0 + 8, my + cell * geo::kSize + 8, "White: wall", style::kTextMuted, ui::Font::Small);
    ui::text(x0 + 8, my + cell * geo::kSize + 24, "Gold: door, red: locked", style::kTextMuted, ui::Font::Small);
    if (ms.teleport)
        ui::text(x0 + 8, my + cell * geo::kSize + 40, "Tap a square to go there", style::kTextMuted, ui::Font::Small);
}

void draw_walk_keys(const char* side_label = "Next Map")
{
    ui::key_arrow(walk_key(kWTurnL), ui::Arrow::TurnLeft);
    ui::key_arrow(walk_key(kWStepL), ui::Arrow::Left);
    ui::key_arrow(walk_key(kWFwd), ui::Arrow::Forward);
    ui::key_arrow(walk_key(kWStepR), ui::Arrow::Right);
    ui::key_arrow(walk_key(kWTurnR), ui::Arrow::TurnRight);
    ui::key_arrow(walk_key(kWAround), ui::Arrow::TurnAround);
    ui::key(walk_key(kWArea), "Area");
    if (ui::large()) ui::key(walk_key(kWNext), side_label);
    ui::key(walk_key(kWEsc), "Esc");
}

void leave_walk()
{
    walk::close();
    walk_error = nullptr;
    frame::set_left(false);
    frame::set_scale(cfg->scale_15x ? frame::Scale::OneAndHalf : frame::Scale::One);
    go(Screen::Files);
}

void draw_walk()
{
    if (walk_error) {
        ui::clear();
        ui::header("Walk Test", true);
        int y = ui::header_h() + ui::gap() * 3;
        wrap_text(ui::gap() * 3, y, ui::width() - ui::gap() * 6, walk_error, ui::Font::Normal, style::kText, true);
        return;
    }
    frame::set_ega_palette();
    walk::draw(frame::canvas());
    ui::clear();
    frame::present();
    draw_walk_keys();
    draw_companion();
}

void tap_walk(const ui::Tap& t)
{
    if (walk_error) {
        if (ui::back_rect().contains(t.x, t.y)) leave_walk();
        return;
    }
    static const walk::Act kActs[kWKeys] = {walk::Act::TurnLeft, walk::Act::StepLeft, walk::Act::Forward,
                                            walk::Act::StepRight, walk::Act::TurnRight, walk::Act::TurnAround,
                                            walk::Act::Area, walk::Act::NextMap, walk::Act::Forward};
    for (int k = 0; k < kWKeys; ++k) {
        const ui::Rect r = walk_key(k);
        if (r.w == 0 || !r.contains(t.x, t.y)) continue;
        if (k == kWEsc) { leave_walk(); return; }
        walk::act(kActs[k], frame::canvas());
        frame::present();
        if (k == kWNext || k == kWArea || ui::large()) draw_companion();
        return;
    }
    if (ui::large() && t.x >= comp_map_x() && t.y >= kCompMapY && t.x < comp_map_x() + kCompCell * geo::kSize &&
        t.y < kCompMapY + kCompCell * geo::kSize) {
        walk::teleport((t.x - comp_map_x()) / kCompCell, (t.y - kCompMapY) / kCompCell);
        walk::draw(frame::canvas());
        frame::present();
        draw_companion();
        return;
    }
    int cx, cy;
    if (frame::to_canvas(t.x, t.y, cx, cy) && walk::tap(cx, cy, frame::canvas())) {
        frame::present();
        draw_companion();
    }
}

// ---- Play test --------------------------------------------------------------
// A new game run by the game's own scripts. The same controls as the Walk
// Test; 480x320's side column has Look in place of Next Map (320x240: Look
// is on the game's menu line).

int play_last_x = -1, play_last_y = -1, play_last_dir = -1;
const geo::Map* play_last_map = nullptr;

// The keyboard for the game's questions (INPUT NUMBER / STRING). 320x240:
// the letters over the top of the game screen (the text window and the
// menu line, where the typing shows, stay in sight), Del / Space / Enter
// under it. 480x320: the letters under the game screen, Del / Space /
// Enter in the Companion strip.
constexpr char kKbChars[] = "1234567890QWERTYUIOPASDFGHJKL-ZXCVBNM'.?";
constexpr int  kKbCols = 10, kKbRows = 4, kKbKeys = kKbCols * kKbRows;
enum KbAction { kKbDel = kKbKeys, kKbSpace, kKbEnter, kKbAll };
bool kb_shown = false;

ui::Rect kb_key(int k)
{
    const int gp = 2;
    if (k < kKbKeys) {
        const int col = k % kKbCols, row = k / kKbCols;
        const int x0 = 0, w = ui::large() ? ui::width() : pic::kScreenW;
        const int y0 = ui::large() ? pic::kScreenH : 0;
        const int h = ui::large() ? ui::height() - pic::kScreenH : text::kTextArea.y0 * 8;
        const int kw = (w - gp) / kKbCols, kh = (h - gp) / kKbRows;
        return {x0 + gp + col * kw, y0 + gp + row * kh, kw - gp, kh - gp};
    }
    const int i = k - kKbKeys;
    if (ui::large()) {
        const int x0 = pic::kScreenW, w = ui::width() - x0, h = pic::kScreenH / 3;
        return {x0 + gp, gp + i * h, w - gp * 2, h - gp * 2};
    }
    const int top = pic::kScreenH + gp, w = pic::kScreenW / 3;
    return {gp + i * w, top, w - gp * 2, ui::height() - top - gp};
}

bool kb_usable(int k, play::Input in)
{
    if (in != play::Input::Number) return true;
    return k >= kKbKeys ? k != kKbSpace : (kKbChars[k] >= '0' && kKbChars[k] <= '9');
}

void draw_keyboard()
{
    LGFX& g = ui::gfx();
    const play::Input in = play::input();
    if (ui::large()) {
        g.fillRect(0, pic::kScreenH, ui::width(), ui::height() - pic::kScreenH, style::kBackground);
        g.fillRect(pic::kScreenW, 0, ui::width() - pic::kScreenW, pic::kScreenH, style::kBackground);
    } else {
        g.fillRect(0, 0, pic::kScreenW, text::kTextArea.y0 * 8, style::kBackground);
        g.fillRect(0, pic::kScreenH, ui::width(), ui::height() - pic::kScreenH, style::kBackground);
    }
    for (int k = 0; k < kKbAll; ++k) {
        char label[2] = {k < kKbKeys ? kKbChars[k] : '\0', '\0'};
        const char* l = k < kKbKeys ? label : k == kKbDel ? "Del" : k == kKbSpace ? "Space" : "Enter";
        ui::key(kb_key(k), l, kb_usable(k, in) ? (k == kKbEnter ? ui::KeyStyle::Lit : ui::KeyStyle::Normal)
                                               : ui::KeyStyle::Dim);
    }
}

bool tap_keyboard(const ui::Tap& t)
{
    const play::Input in = play::input();
    for (int k = 0; k < kKbAll; ++k) {
        if (!kb_key(k).contains(t.x, t.y)) continue;
        if (!kb_usable(k, in)) return true;
        const char c = k < kKbKeys ? kKbChars[k] : k == kKbDel ? '\b' : k == kKbSpace ? ' ' : '\n';
        play::input_key(c, frame::canvas());
        return true;
    }
    return true;     // the keyboard takes every tap while it is up
}

// ---- Journal ------------------------------------------------------------
// An entry of the Adventurer's Journal (or a tavern tale), as a picture
// from JOURNAL.BIN in the game's folder (made by the Journal Converter on
// the flasher site from the player's own GOG journal). Pages through it with
// Prev Page / Next Page; Back returns to the game.

struct JournalView {
    char     kind = 'J';
    int      number = 0;
    int      top = 0;                     // first picture row on screen
    bool     have = false;                // the file and entry were found
    const char* why = "";
    journal::Picture pic;
    lgfx::rgb888_t pal[16];
    lgfx::rgb888_t line[journal::kMaxWidth];
    int      x0 = 0, y0 = 0;
};
JournalView* jv = nullptr;

constexpr char kJournalFile[] = "JOURNAL.BIN";

ui::Rect journal_area()
{
    const int top = ui::header_h() + 2;
    return {0, top, ui::width(), ui::height() - top - ui::key_h() - ui::gap() * 2};
}
int journal_step() { return journal_area().h - 24; }        // a page, keeping a little for context

bool journal_source(fs::File& f)
{
    char path[160];
    library::path_of(game_dirs[game_sel].data_dir, kJournalFile, path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

void open_journal(char kind, int number)
{
    if (!jv) jv = new (std::nothrow) JournalView;
    if (!jv) return;
    jv->kind = kind;
    jv->number = number;
    jv->top = 0;
    jv->have = false;
    jv->why = "The journal file isn't on the card yet. Make JOURNAL.BIN with the Journal Converter on the flasher "
              "page and copy it into this game's folder, next to its .DAX files.";
    fs::File f;
    if (journal_source(f)) {
        library::FileSource src(f);
        journal::Info info;
        if (!journal::read_info(src, info)) {
            jv->why = "JOURNAL.BIN in this game's folder can't be read. Make it again with the Journal Converter.";
        } else {
            const int wi = journal::pick_width(info, ui::width() - 4);
            if (journal::find(src, info, kind, number, wi, jv->pic)) {
                jv->have = true;
                for (int i = 0; i < 16; ++i) {
                    const uint16_t c = jv->pic.palette[i];
                    jv->pal[i] = lgfx::rgb888_t(((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31);
                }
            } else {
                jv->why = "That entry isn't in this game's JOURNAL.BIN.";
            }
        }
        f.close();
    }
    go(Screen::Journal);
}

void journal_row(int y, const uint8_t* row, int w, void* ctx)
{
    JournalView& v = *static_cast<JournalView*>(ctx);
    for (int x = 0; x < w; ++x) v.line[x] = v.pal[row[x] & 15];
    ui::gfx().pushImage(v.x0, v.y0 + y - v.top, w, 1, v.line);
}

void draw_journal()
{
    ui::clear();
    char title[48];
    const char* what = jv && jv->kind == 'T' ? "Tavern Tale" : "Journal Entry";
    const ui::Rect a = journal_area();
    if (jv && jv->have && jv->pic.h > a.h) {
        const int pages = (jv->pic.h - a.h + journal_step() - 1) / journal_step() + 1;
        const int page = jv->top / journal_step() + 1;
        snprintf(title, sizeof title, "%s %d  %d/%d", what, jv ? jv->number : 0, page, pages);
    } else {
        snprintf(title, sizeof title, "%s %d", what, jv ? jv->number : 0);
    }
    ui::header(title, true);
    const bool more = jv && jv->have && jv->top + a.h < jv->pic.h;
    ui::key(ui::bottom_key(0, 3), "Prev Page", jv && jv->top > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    ui::key(ui::bottom_key(1, 3), "Back to Game");
    ui::key(ui::bottom_key(2, 3), "Next Page", more ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    if (!jv || !jv->have) {
        wrap_text(ui::gap() * 3, a.y + ui::gap() * 2, ui::width() - ui::gap() * 6, jv ? jv->why : "",
                  ui::Font::Normal, style::kText, true);
        return;
    }
    // The page of the picture, on white like the journal's paper
    LGFX& g = ui::gfx();
    g.fillRect(0, a.y, a.w, a.h, TFT_WHITE);
    fs::File f;
    if (!journal_source(f)) return;
    library::FileSource src(f);
    jv->x0 = (ui::width() - jv->pic.w) / 2;
    jv->y0 = a.y;
    const int rows = jv->pic.h - jv->top < a.h ? jv->pic.h - jv->top : a.h;
    g.startWrite();
    journal::rows(src, jv->pic, jv->top, rows, journal_row, jv);
    g.endWrite();
    f.close();
}

void leave_journal()
{
    delete jv;
    jv = nullptr;
    go(Screen::Play);         // the game screen comes back as it was
}

void tap_journal(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) { leave_journal(); return; }
    const int k = bottom_hit(t, 3);
    if (k == 1) { leave_journal(); return; }
    if (!jv || !jv->have) return;
    const ui::Rect a = journal_area();
    if (k == 0 && jv->top > 0) {
        jv->top = jv->top > journal_step() ? jv->top - journal_step() : 0;
        dirty = true;
    } else if (k == 2 && jv->top + a.h < jv->pic.h) {
        jv->top += journal_step();
        dirty = true;
    }
}

void leave_play()
{
    kb_shown = false;
    delete jv;
    jv = nullptr;
    play::close();
    play_error = nullptr;
    frame::set_left(false);
    frame::set_scale(cfg->scale_15x ? frame::Scale::OneAndHalf : frame::Scale::One);
    go(Screen::Files);
}

// Shows what the scripts changed: the canvas rows, and the Companion when
// the party moved
void present_play()
{
    int y0, y1;
    play::take_dirty(y0, y1);
    if (kb_shown && !ui::large() && y0 < text::kTextArea.y0 * 8) y0 = text::kTextArea.y0 * 8;   // under the keys
    if (y1 > y0) frame::present_rows(y0, y1);
    const bool want_kb = play::input() != play::Input::None;
    if (want_kb && !kb_shown) {
        kb_shown = true;
        draw_keyboard();
    } else if (!want_kb && kb_shown) {
        kb_shown = false;
        dirty = true;               // the whole game screen again
        return;
    }
    if (kb_shown) return;
    char jk;
    int jn;
    if (play::journal_request(&jk, &jn)) {
        open_journal(jk, jn);
        return;
    }
    if (play::pos_x() != play_last_x || play::pos_y() != play_last_y || play::dir() != play_last_dir ||
        play::map() != play_last_map) {
        play_last_x = play::pos_x();
        play_last_y = play::pos_y();
        play_last_dir = play::dir();
        play_last_map = play::map();
        draw_companion(kPlayMap);
    }
}

void draw_play()
{
    if (play_error) {
        ui::clear();
        ui::header("Play Test", true);
        int y = ui::header_h() + ui::gap() * 3;
        wrap_text(ui::gap() * 3, y, ui::width() - ui::gap() * 6, play_error, ui::Font::Normal, style::kText, true);
        return;
    }
    frame::set_ega_palette();
    play::draw(frame::canvas());
    ui::clear();
    frame::present();
    int y0, y1;
    play::take_dirty(y0, y1);
    kb_shown = false;
    draw_walk_keys("Look");
    play_last_map = nullptr;
    play_last_x = -1;
    present_play();
}

void tap_play(const ui::Tap& t)
{
    if (play_error) {
        if (ui::back_rect().contains(t.x, t.y)) leave_play();
        return;
    }
    if (kb_shown) {
        tap_keyboard(t);
        present_play();
        return;
    }
    static const play::Act kActs[kWKeys] = {play::Act::TurnLeft, play::Act::StepLeft, play::Act::Forward,
                                            play::Act::StepRight, play::Act::TurnRight, play::Act::TurnAround,
                                            play::Act::Area, play::Act::Look, play::Act::Forward};
    for (int k = 0; k < kWKeys; ++k) {
        const ui::Rect r = walk_key(k);
        if (r.w == 0 || !r.contains(t.x, t.y)) continue;
        if (k == kWEsc) { leave_play(); return; }
        play::act(kActs[k], frame::canvas());
        present_play();
        return;
    }
    int cx, cy;
    if (frame::to_canvas(t.x, t.y, cx, cy)) {
        play::tap(cx, cy, frame::canvas());
        present_play();
    }
}

// ---- Settings --------------------------------------------------------------

enum SetKey { kBrightDown, kBrightUp, kInvert, kSwap, kRotate, kCalibrate, kScale, kSetKeys };

int set_count() { return ui::large() ? kSetKeys : kSetKeys - 1; }   // 1.5x only on 480x320

ui::Rect set_cell(int i) { return ui::grid_cell(i, 2, ui::large() ? 4 : 3); }

void draw_settings()
{
    ui::clear();
    ui::header("Settings", true);
    char b[32];
    snprintf(b, sizeof b, "Brightness - (%d%%)", cfg->brightness * 100 / 255);
    ui::key(set_cell(kBrightDown), b);
    ui::key(set_cell(kBrightUp), "Brightness +");
    const PanelPrefs& pp = panel_prefs_get();
    ui::key(set_cell(kInvert), "Invert Colors", pp.invert ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    ui::key(set_cell(kSwap), "Swap Red/Blue", pp.swap_rb ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    ui::key(set_cell(kRotate), "Rotate 180", cfg->flipped ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    ui::key(set_cell(kCalibrate), "Recalibrate Touch");
    if (ui::large())
        ui::key(set_cell(kScale), cfg->scale_15x ? "Game Screen: 1.5x" : "Game Screen: 1:1",
                cfg->scale_15x ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);

    // Version, board and memory in the space a key row would take
    char l1[96], l2[96];
    snprintf(l1, sizeof l1, "%s (%s)  %s", env_.version, env_.build, BOARD_NAME);
    snprintf(l2, sizeof l2, "Free memory %u KB, largest block %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
    const int lh = ui::line_h(ui::Font::Small) + 2;
    const int y = ui::height() - ui::gap() - ui::key_h() / 2 - lh;
    ui::text(ui::gap() * 2, y, l1, style::kTextMuted, ui::Font::Small);
    ui::text(ui::gap() * 2, y + lh, l2, style::kTextMuted, ui::Font::Small);
}

void tap_settings(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) { go(Screen::Home); return; }
    LGFX& g = ui::gfx();
    for (int i = 0; i < set_count(); ++i) {
        if (!set_cell(i).contains(t.x, t.y)) continue;
        PanelPrefs pp = panel_prefs_get();
        switch (i) {
        case kBrightDown:
            cfg->brightness = cfg->brightness > kMinBrightness + 25 ? cfg->brightness - 25 : kMinBrightness;
            g.setBrightness(cfg->brightness);
            break;
        case kBrightUp:
            cfg->brightness = cfg->brightness < 230 ? cfg->brightness + 25 : 255;
            g.setBrightness(cfg->brightness);
            break;
        case kInvert: pp.invert = !pp.invert; panel_prefs_set(g, pp); break;
        case kSwap:   pp.swap_rb = !pp.swap_rb; panel_prefs_set(g, pp); break;
        case kRotate:
            cfg->flipped = !cfg->flipped;
            env_.apply_rotation(cfg->flipped);
            break;
        case kCalibrate: env_.recalibrate(); break;
        case kScale:
            cfg->scale_15x = !cfg->scale_15x;
            frame::set_scale(cfg->scale_15x ? frame::Scale::OneAndHalf : frame::Scale::One);
            break;
        }
        settings_save(*cfg);
        dirty = true;
        return;
    }
}

} // namespace

void begin(const Env& env, Settings& settings)
{
    env_ = env;
    cfg = &settings;
    frame::set_scale(cfg->scale_15x ? frame::Scale::OneAndHalf : frame::Scale::One);
    // The library as the last scan found it; a scan only when there is none
    // (Tom: scan once, then only on Rescan Card)
    close_file();
    if (sd_begin() && library::load_library(game_dirs, library::kMaxGames, &n_games)) {
        scan_result = library::ScanResult::Ok;
        Serial.printf("[library] loaded: %d game folder(s)\n", n_games);
    } else {
        rescan();
    }
    go(Screen::Home);
}

void tick()
{
    ui::Tap t;
    if (ui::poll_tap(t)) {
        switch (screen) {
        case Screen::Home:     tap_home(t); break;
        case Screen::Files:    tap_files(t); break;
        case Screen::Blocks:   tap_blocks(t); break;
        case Screen::View:     tap_view(t); break;
        case Screen::Look:     tap_look(t); break;
        case Screen::Walk:     tap_walk(t); break;
        case Screen::Play:     tap_play(t); break;
        case Screen::Journal:  tap_journal(t); break;
        case Screen::Settings: tap_settings(t); break;
        }
    }
    if (screen == Screen::Look && !look_error && !dirty) present(look::tick(millis(), frame::canvas()));
    if (screen == Screen::Play && !play_error && !dirty) {
        play::tick(millis(), frame::canvas());
        present_play();
    }
    if (!dirty) return;
    dirty = false;
    switch (screen) {
    case Screen::Home:     draw_home(); break;
    case Screen::Files:    draw_files(); break;
    case Screen::Blocks:   draw_blocks(); break;
    case Screen::View:     draw_view(); break;
    case Screen::Look:     draw_look(); break;
    case Screen::Walk:     draw_walk(); break;
    case Screen::Play:     draw_play(); break;
    case Screen::Journal:  draw_journal(); break;
    case Screen::Settings: draw_settings(); break;
    }
}

} // namespace viewer

#include "viewer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

#include "app/library.h"
#include "engine/dax.h"
#include "engine/games.h"
#include "engine/picture.h"
#include "frame.h"
#include "hal/panel_prefs.h"
#include "hal/sdcard.h"
#include "ui.h"

namespace viewer {

namespace {

enum class Screen : uint8_t { Home, Files, Blocks, View, Settings };

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
pic::Anim          cur_anim;                    // the animation being viewed
int                cur_anim_block = -1;
int                block_page = 0;
int                block_sel = 0;    // entry number in index_

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

void rescan()
{
    close_file();
    sd_lost();               // forget a card that was pulled; sd_begin retries
    scan_result = library::scan(game_dirs, library::kMaxGames, &n_games);
    home_page = 0;
    Serial.printf("[library] scan: %d game folder(s), result %d\n", n_games, (int)scan_result);
}

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
        is_pic[e] = is_anim[e] = is_vga[e] = false;
        const dax::Entry& en = index_.entries[e];
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

constexpr int kHomeCols = 2, kHomeRows = 2;

void draw_home()
{
    ui::clear();
    ui::header("Gold Box Library", false);
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
        Pager p{n_games, kHomeCols * kHomeRows, home_page};
        for (int i = 0; i < p.per_page && p.first() + i < n_games; ++i) {
            const library::GameDir& g = game_dirs[p.first() + i];
            char sub[64];
            snprintf(sub, sizeof sub, "%s - %d files", g.folder, g.dax_files);
            ui::key2(ui::grid_cell(i, kHomeCols, kHomeRows), games::short_title(g.game), sub);
        }
    }
    const bool more = n_games > kHomeCols * kHomeRows;
    ui::key(ui::bottom_key(0, more ? 4 : 2), "Rescan Card");
    ui::key(ui::bottom_key(1, more ? 4 : 2), "Settings");
    if (more) {
        Pager p{n_games, kHomeCols * kHomeRows, home_page};
        ui::key(ui::bottom_key(2, 4), "<", p.page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
        ui::key(ui::bottom_key(3, 4), ">", p.page + 1 < p.pages() ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    }
}

void tap_home(const ui::Tap& t)
{
    const bool more = n_games > kHomeCols * kHomeRows;
    const int k = bottom_hit(t, more ? 4 : 2);
    Pager p{n_games, kHomeCols * kHomeRows, home_page};
    if (k == 0) { rescan(); dirty = true; return; }
    if (k == 1) { go(Screen::Settings); return; }
    if (k == 2 && p.page > 0) { --home_page; dirty = true; return; }
    if (k == 3 && p.page + 1 < p.pages()) { ++home_page; dirty = true; return; }
    if (scan_result != library::ScanResult::Ok) return;
    for (int i = 0; i < p.per_page && p.first() + i < n_games; ++i) {
        if (ui::grid_cell(i, kHomeCols, kHomeRows).contains(t.x, t.y)) {
            game_sel = p.first() + i;
            n_files = library::list_dax(game_dirs[game_sel].data_dir, files, library::kMaxFiles);
            file_page = 0;
            go(Screen::Files);
            return;
        }
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
    for (int i = 0; i < p.per_page && p.first() + i < n_files; ++i) {
        // Every file here is a .DAX: show the name without it, so it fits
        char label[library::kNameLen];
        strlcpy(label, files[p.first() + i], sizeof label);
        const size_t n = strlen(label);
        if (n > 4 && strcasecmp(label + n - 4, ".DAX") == 0) label[n - 4] = 0;
        ui::key(ui::grid_cell(i, file_cols(), kFileRows), label);
    }
    draw_pager_keys(p);
}

void tap_files(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) { go(Screen::Home); return; }
    Pager p{n_files, file_cols() * kFileRows, file_page};
    if (pager_tap(t, p, &file_page)) { dirty = true; return; }
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
        if (is_pic[e]) {
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
    if (is_anim[block_sel] && cur_anim_block == block_sel) {
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
    if (is_vga[block_sel]) {
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
    rescan();
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
        case Screen::Settings: tap_settings(t); break;
        }
    }
    if (!dirty) return;
    dirty = false;
    switch (screen) {
    case Screen::Home:     draw_home(); break;
    case Screen::Files:    draw_files(); break;
    case Screen::Blocks:   draw_blocks(); break;
    case Screen::View:     draw_view(); break;
    case Screen::Settings: draw_settings(); break;
    }
}

} // namespace viewer

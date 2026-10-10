#include "viewer.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

#include "app/features.h"
#include "app/library.h"
#include "engine/dax.h"
#include "engine/font.h"
#include "engine/games.h"
#include "engine/icon.h"
#include "engine/inflate.h"
#include "engine/journal.h"
#include "engine/pdf.h"
#include "engine/picture.h"
#include "engine/text.h"
#include "frame.h"
#include "hal/panel_prefs.h"
#include "hal/bigstack.h"
#include "hal/sdcard.h"
#include "look.h"
#include "logui.h"
#include "pdfview.h"
#include "play.h"
#include "walk.h"
#include "ui.h"

namespace viewer {

namespace {

enum class Screen : uint8_t { Home, Files, Resources, Blocks, View, Look, Walk, Play, Journal, Pdf, GameMenu, Settings, Logs };

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

// The asset viewer's tables (Files / Blocks / View): on the heap only
// while those screens are open - the Play Test needs the memory
// (v0.21.1: linking WiFi took ~23 KB of static RAM)
struct Assets {
    char        files[library::kMaxFiles][library::kNameLen];
    dax::Index  index_;
    pic::Header pic_hdr[dax::kMaxEntries];   // picture, or an animation's first frame
    bool        is_pic[dax::kMaxEntries];    // something to draw (picture or animation)
    bool        is_anim[dax::kMaxEntries];   // an animation (PIC, SPRIT...); pic_hdr.frames = its frames
    bool        is_vga[dax::kMaxEntries];    // a 256-colour picture (Pools of Darkness)
    bool        is_font[dax::kMaxEntries];   // the game's 8x8 font (block 201 of an 8X8D file)
    font::Font  cur_font;
    pic::Anim   cur_anim;                    // the animation being viewed
    pic::Rgb    pal[256];                    // a 256-colour picture's palette
};
Assets* A = nullptr;

// Files of the chosen game (A->files)
int   n_files = 0;
int   file_page = 0;
int   file_sel = 0;

// The open DAX file
fs::File           cur_file;
library::FileSource* src = nullptr;
alignas(library::FileSource) uint8_t src_mem[sizeof(library::FileSource)];
dax::Status        index_status = dax::Status::Ok;
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

void close_file();
void list_files();

// The asset viewer's tables: made for Files / Blocks / View, gone anywhere
// else (the game screens need the memory)
void go(Screen s)
{
    const bool assets = s == Screen::Resources || s == Screen::Blocks || s == Screen::View;
    if (assets && !A) {
        A = new (std::nothrow) Assets;
        if (A) list_files();
        else s = Screen::Home;            // (not enough memory: stays at the library)
    } else if (!assets && A) {
        close_file();
        delete A;
        A = nullptr;
    }
    // Drags: the Settings slider, the journal entry and book pictures;
    // the Logs screen sets its own
    if (s != Screen::Logs) ui::allow_drag(s == Screen::Settings || s == Screen::Journal || s == Screen::Pdf);
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
    library::path_of(game_dirs[game_sel].data_dir, A->files[i], path, sizeof path);
    cur_file = sd_fs().open(path, "r");
    if (!cur_file) {
        index_status = dax::Status::ReadError;
        A->index_.count = 0;
        return false;
    }
    src = new (src_mem) library::FileSource(cur_file);
    index_status = dax::read_index(*src, A->index_);
    for (int e = 0; e < A->index_.count; ++e) {
        A->is_pic[e] = A->is_anim[e] = A->is_vga[e] = A->is_font[e] = false;
        const dax::Entry& en = A->index_.entries[e];
        if (en.id == font::kBlockId && en.raw_size == font::kBlockBytes) {
            // Shown as a sheet of its glyphs plus a line of text
            A->is_pic[e] = A->is_font[e] = true;
            A->pic_hdr[e] = pic::Header{};
            A->pic_hdr[e].height = pic::kScreenH;
            A->pic_hdr[e].width_cols = pic::kScreenW / 8;
            A->pic_hdr[e].frames = 1;
            continue;
        }
        {
            dax::RleReader r(*src, A->index_, en);
            uint8_t hdr[pic::kHeaderSize];
            const size_t got = r.read(hdr, sizeof hdr);
            if (got == sizeof hdr) A->is_pic[e] = pic::parse_header(hdr, en.raw_size, A->pic_hdr[e]);
            pic::VgaHeader vh;
            if (!A->is_pic[e] && got >= pic::kVgaHeaderSize && pic::parse_vga_header(hdr, en.raw_size, vh)) {
                A->is_pic[e] = A->is_vga[e] = true;
                A->pic_hdr[e] = pic::Header{};
                A->pic_hdr[e].height = vh.height;
                A->pic_hdr[e].width_cols = vh.width_cols;
                A->pic_hdr[e].frames = vh.frames;
            }
        }
        if (!A->is_pic[e]) {
            dax::RleReader r(*src, A->index_, en);
            if (pic::parse_anim(r, en.raw_size, A->cur_anim)) {
                A->is_pic[e] = A->is_anim[e] = true;
                A->pic_hdr[e] = A->cur_anim.frame[0];
                A->pic_hdr[e].frames = static_cast<uint8_t>(A->cur_anim.frames);
            }
        }
    }
    cur_anim_block = -1;
    Serial.printf("[viewer] %s: %s, %d blocks\n", path, dax::status_text(index_status), A->index_.count);
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
bool render_icon(const library::GameDir& g, int x, int y, int px, ui::KeyStyle st, bool draw, bool save,
                 const char** why = nullptr)
{
    const char* dummy;
    if (!why) why = &dummy;
    *why = "";
    if (!g.icon[0]) return false;
    char path[160];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, g.icon);
    fs::File f = sd_fs().open(path, "r");
    if (!f) {
        *why = "its file couldn't be opened";
        return false;
    }
    library::FileSource src(f);
    icon::Found fo;
    bool ok = icon::find(src, fo);
    if (!ok) *why = "no picture found in its file";
    if (ok) {
        const uint32_t t0 = millis();
        uint8_t* window = fo.png ? static_cast<uint8_t*>(malloc(inflate::kWindow)) : nullptr;
        IconDraw* d = new (std::nothrow) IconDraw;
        ok = d && (!fo.png || window);
        if (!ok) *why = "not enough memory just now";
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
            if (!ok) *why = "it couldn't be decoded (or memory ran short)";
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
    struct Job {
        const library::GameDir* g;
        int                     x, y, px;
        ui::KeyStyle            st;
        bool                    ok;
    } job{&g, x, y, px, st, false};
    run_on_big_stack(
        [](void* p) {
            auto* j = static_cast<Job*>(p);
            j->ok = render_icon(*j->g, j->x, j->y, j->px, j->st, true, true);
        },
        &job);
    return job.ok;
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
    char     pending[200] = {};      // the last line, written to the log once it's final
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

// JOURNAL.DAT is written piece by piece, wherever the pieces go
struct SdOutput : journal::Output {
    fs::File f;
    bool write_at(uint32_t pos, const uint8_t* d, size_t n) override
    {
        yield();            // a long job: let the system's own tasks run
        return f.seek(pos) && f.write(d, n) == n;
    }
};

struct JournalProgress {
    const char* title;
};

void journal_progress(int done, int total, void* ctx)
{
    char line[100];
    snprintf(line, sizeof line, "Processing the %s journal: page %d of %d", static_cast<JournalProgress*>(ctx)->title,
             done, total);
    scan_say(line, true, nullptr);
    delay(1);
}

// The journal's entries, made once from the player's own PDF (Tom: on the
// board, during the scan; a PDF it doesn't know is reported, not guessed)
void prepare_journal_now(const library::GameDir& g);

// A guard round it: if the board restarted while reading this journal last
// time, this scan leaves it alone (and says so); the next Rescan Card tries
// again
void prepare_journal(const library::GameDir& g)
{
    char try_path[160];
    library::cache_path(g, "JOURNAL.TRY", try_path, sizeof try_path);
    if (sd_fs().exists(try_path)) {
        sd_fs().remove(try_path);
        char line[200];
        snprintf(line, sizeof line,
                 "The board restarted while reading the %s journal last time: skipped this time (Rescan Card tries "
                 "again)", games::title(g.game));
        scan_say(line, false, nullptr);
        return;
    }
    library::make_cache_dirs(g);
    fs::File tf = sd_fs().open(try_path, "w");
    if (tf) {
        tf.print("reading the journal\n");
        tf.close();
    }
    if (scr && scr->log) scr->log.flush();
    prepare_journal_now(g);
    sd_fs().remove(try_path);
}

void prepare_journal_now(const library::GameDir& g)
{
    char line[160], path[200];
    const char* title = games::title(g.game);
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, g.journal);
    fs::File f = sd_fs().open(path, "r");
    if (!f) return;
    library::FileSource src(f);
    pdf::Doc* doc = new (std::nothrow) pdf::Doc;
    const bool readable = doc && pdf::open(src, *doc);
    const journal::Table* t = readable ? journal::find_table(src.size(), doc->id) : nullptr;
    if (!t) {
        snprintf(line, sizeof line,
                 readable ? "The %s journal is an edition this engine doesn't know yet (size %u, id %s): its entries "
                            "can't be shown."
                          : "The %s journal can't be read as a PDF.",
                 title, static_cast<unsigned>(src.size()), doc ? doc->id : "");
        scan_say(line, false, nullptr);
        delete doc;
        f.close();
        return;
    }
    delete doc;
    int nj = 0, nt = 0;
    for (int i = 0; i < t->n_entries; ++i) (t->entries[i].kind == 'T' ? nt : nj)++;
    char out_path[160];
    library::cache_path(g, "JOURNAL.DAT", out_path, sizeof out_path);
    {
        fs::File old = sd_fs().open(out_path, "r");
        if (old) {
            library::FileSource os(old);
            journal::Info info;
            const bool ready = journal::read_info(os, info) && strcmp(info.pdf_id, t->pdf_id) == 0 &&
                               info.entries == t->n_entries;
            old.close();
            if (ready) {
                snprintf(line, sizeof line, "The %s journal is ready (made by an earlier scan)", title);
                scan_say(line, false, nullptr);
                f.close();
                return;
            }
        }
    }
    snprintf(line, sizeof line, "Processing the %s journal... (memory: %u free, %u in one piece)", title,
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    scan_say(line, false, nullptr);
    scan_flush_log();
    if (scr && scr->log) scr->log.flush();
    SdOutput out;
    bool ok = library::make_cache_dirs(g);
    if (ok) out.f = sd_fs().open(out_path, "w");
    JournalProgress jp{title};
    const uint32_t t0 = millis();
    ok = ok && out.f && journal::make(src, *t, out, journal_progress, &jp);
    if (out.f) out.f.close();
    f.close();
    if (ok) {
        snprintf(line, sizeof line, "Prepared the %s journal: %d journal entries, %d tavern tales (%lu s)", title, nj, nt,
                 static_cast<unsigned long>((millis() - t0) / 1000));
    } else {
        sd_fs().remove(out_path);
        snprintf(line, sizeof line, "The %s journal couldn't be processed (card full, or not enough memory).", title);
    }
    scan_say(line, true, nullptr);
}

// The scan's second part: the icons at this screen's size, the journals
// (deep decoding: run_on_big_stack)
void prepare_made(void*)
{
    const int px = icon_size(home_card());
    for (int i = 0; i < n_games; ++i) {
        const library::GameDir& g = game_dirs[i];
        if (!g.icon[0] || !px) continue;
        scan_say("Preparing the game icon...", false, nullptr);
        char line[160];
        const char* why = "";
        const bool ok = render_icon(g, 0, 0, px, ui::KeyStyle::Normal, false, true, &why);
        if (ok)
            snprintf(line, sizeof line, "Prepared the %s icon", games::short_title(g.game));
        else
            snprintf(line, sizeof line, "The %s icon wasn't prepared: %s (free %u KB, largest block %u KB). The library tries again when it shows it.",
                     games::short_title(g.game), why, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
        scan_say(line, true, nullptr);
    }
    for (int i = 0; i < n_games; ++i)
        if (game_dirs[i].journal[0]) prepare_journal(game_dirs[i]);
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

    // The library first: whatever happens while the icons and journals are
    // made, the board doesn't scan again on its own
    if (scan_result == library::ScanResult::Ok) {
        scan_say(library::save_library(game_dirs, n_games) ? "Saved the library: the board won't scan again until you tap Rescan Card."
                                                           : "The library couldn't be saved on the card.",
                 false, nullptr);
    }
    // What the board makes from the player's files, once (icons, journals:
    // PNG / PDF / JPEG decoding, on a stack of its own)
    if (!run_on_big_stack(prepare_made, nullptr))
        scan_say("Not enough memory to make the icons and journal pictures now: tap Rescan Card to try again.", false,
                 nullptr);
    scan_say("Done. Look through the list, then tap Continue.", false, nullptr);
    // The list stays up to be read (Tom, 2026-10-09): the whole log,
    // scrolling, until Continue
    char* fallback = nullptr;
    if (scr) {
        scan_flush_log();
        if (scr->log) {
            scr->log.close();
        } else {
            // No log on the card (no card): the lines kept on screen
            fallback = static_cast<char*>(malloc(ScanScreen::kLines * 101));
            if (fallback) {
                fallback[0] = 0;
                for (int i = 0; i < scr->n; ++i) {
                    strcat(fallback, scr->line[i]);
                    strcat(fallback, "\n");
                }
            }
        }
    }
    delete scr;
    scr = nullptr;
    logui::open(true, fallback);
    free(fallback);
    go(Screen::Logs);
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
        y = wrap_text(x, y, ui::width() - x * 2,
                      "Copy each game's whole install folder from your GOG games into a GOLDBOX folder on a FAT32 "
                      "microSD card, for example:", ui::Font::Normal, style::kText, true);
        y += lh / 2;
        ui::text(x, y, "/GOLDBOX/Curse of the Azure Bonds", style::kGold);
        y += lh * 3 / 2;
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
    if (k == 0) { rescan(); return; }
    if (k == 1) { go(Screen::Settings); return; }
    if (k == 2 && home_page > 0) { --home_page; dirty = true; return; }
    if (k == 3 && home_page + 1 < n_games) { ++home_page; dirty = true; return; }
    if (scan_result != library::ScanResult::Ok || n_games == 0) return;
    if (home_card().contains(t.x, t.y)) {
        game_sel = home_page;
        file_page = 0;
        go(Screen::Files);                 // the chooser: the four tests
    }
}

// ---- Files -----------------------------------------------------------------

void list_files()
{
    n_files = A && game_dirs[game_sel].format == library::Format::Dax
            ? library::list_dax(game_dirs[game_sel].data_dir, A->files, library::kMaxFiles) : 0;
}

int file_cols() { return ui::large() ? 4 : 3; }
constexpr int kFileRows = 4;

// The chooser (Tom, 2026-10-10): the four tests as big keys; the game's
// files (the Resource Test) behind the first one
int chooser_keys() { return look::available(game_dirs[game_sel].game) ? 4 : 1; }
ui::Rect chooser_key(int i)
{
    if (chooser_keys() == 1) return ui::grid_cell(2, 2, 2, false);
    return ui::grid_cell(i, 2, 2, false);
}

void draw_files()
{
    ui::clear();
    ui::header(games::title(game_dirs[game_sel].game), true);
    static const char* const kKeys[4][2] = {{"Resource Test", "The game's files"},
                                            {"Screen Test", "Pictures and screens"},
                                            {"Walk Test", "Walk the maps"},
                                            {"Play Test", "Play the game"}};
    if (chooser_keys() == 1) {
        // The Dark Queen of Krynn and Unlimited Adventures; other games
        // the engine doesn't play yet
        const int x = ui::gap() * 3, wdt = ui::width() - ui::gap() * 6;
        const bool hlib = game_dirs[game_sel].format == library::Format::Hlib;
        wrap_text(x, ui::header_h() + ui::gap() * 3, wdt,
                  hlib ? "Its files are .TLB / .GLB libraries, a newer format the viewer can't read yet."
                       : "The engine can't play this game yet; its files can be looked at.",
                  ui::Font::Normal, style::kText, true);
        if (hlib) return;
    }
    for (int i = 0; i < chooser_keys(); ++i) ui::key_big(chooser_key(i), kKeys[i][0], kKeys[i][1]);
}

void tap_files(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) { go(Screen::Home); return; }
    int k = -1;
    for (int i = 0; i < chooser_keys(); ++i)
        if (chooser_key(i).contains(t.x, t.y)) k = i;
    if (k < 0 || (chooser_keys() == 1 && game_dirs[game_sel].format == library::Format::Hlib)) return;
    if (k == 0) {
        file_page = 0;
        go(Screen::Resources);
        return;
    }
    if (k == 3) {
        frame::set_scale(frame::Scale::One);    // the game's screen: 1:1 at the top left
        frame::set_left(true);
        frame::set_ega_palette();
        char cdir[160];
        library::make_cache_dirs(game_dirs[game_sel]);
        snprintf(cdir, sizeof cdir, "%s/%s/%s", games::kRootDir, library::kCacheDir, game_dirs[game_sel].folder);
        play::set_sound(cfg->sound, cfg->volume);
        // Pool of Radiance's folder on the card: its characters can be added
        play::set_pool_dir("");
        for (int g = 0; g < n_games; ++g)
            if (game_dirs[g].game == games::Game::PoolOfRadiance && game_dirs[g].format == library::Format::Dax) {
                char pd[160];
                snprintf(pd, sizeof pd, "%s/%s", games::kRootDir, game_dirs[g].data_dir);
                play::set_pool_dir(pd);
                break;
            }
        play_error = play::open(game_dirs[game_sel].data_dir, game_dirs[game_sel].game, frame::canvas(), cdir);
        go(Screen::Play);
        return;
    }
    if (k == 2) {
        walk_error = walk::open(game_dirs[game_sel].data_dir, game_dirs[game_sel].game);
        frame::set_scale(frame::Scale::One);    // the game's screen: 1:1 at the top left
        frame::set_left(true);
        go(Screen::Walk);
        return;
    }
    look_error = look::open(game_dirs[game_sel].data_dir, game_dirs[game_sel].game);
    look_page = 0;
    frame::set_scale(frame::Scale::One);    // the game's screen: always 1:1
    go(Screen::Look);
}

// The Resource Test: the game's .DAX files, a page at a time; one opens
// its blocks
void draw_resources()
{
    ui::clear();
    Pager p{n_files, file_cols() * kFileRows, file_page};
    char title[80];
    snprintf(title, sizeof title, "%s Files  %d/%d", games::short_title(game_dirs[game_sel].game), p.page + 1, p.pages());
    ui::header(title, true);
    for (int i = 0; i < p.per_page && p.first() + i < n_files; ++i) {
        // Every file here is a .DAX: show the name without it, so it fits
        char label[library::kNameLen];
        strlcpy(label, A->files[p.first() + i], sizeof label);
        const size_t n = strlen(label);
        if (n > 4 && strcasecmp(label + n - 4, ".DAX") == 0) label[n - 4] = 0;
        ui::key(ui::grid_cell(i, file_cols(), kFileRows), label);
    }
    draw_pager_keys(p);
}

void tap_resources(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) { go(Screen::Files); return; }
    Pager p{n_files, file_cols() * kFileRows, file_page};
    if (pager_tap(t, p, &file_page)) {
        dirty = true;
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
    Pager p{A->index_.count, block_cols() * kBlockRows, block_page};
    char title[80];
    snprintf(title, sizeof title, "%s  %d blocks  %d/%d", A->files[file_sel], A->index_.count, p.page + 1, p.pages());
    ui::header(title, true);
    if (index_status != dax::Status::Ok) {
        ui::text(ui::gap() * 3, ui::header_h() + ui::gap() * 3, dax::status_text(index_status), style::kWarn);
    }
    for (int i = 0; i < p.per_page && p.first() + i < A->index_.count; ++i) {
        const int e = p.first() + i;
        const dax::Entry& en = A->index_.entries[e];
        char label[16], sub[24];
        snprintf(label, sizeof label, "#%u", en.id);
        if (A->is_font[e]) {
            snprintf(sub, sizeof sub, "Font");
        } else if (A->is_pic[e]) {
            snprintf(sub, sizeof sub, "%dx%d x%d", A->pic_hdr[e].width_px(), A->pic_hdr[e].height, A->pic_hdr[e].frames);
        } else {
            snprintf(sub, sizeof sub, "%u B", en.raw_size);
        }
        ui::key2(ui::grid_cell(i, block_cols(), kBlockRows), label, sub,
                 A->is_pic[e] ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    }
    draw_pager_keys(p);
}

void tap_blocks(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) {
        close_file();
        go(Screen::Resources);
        return;
    }
    Pager p{A->index_.count, block_cols() * kBlockRows, block_page};
    if (pager_tap(t, p, &block_page)) { dirty = true; return; }
    for (int i = 0; i < p.per_page && p.first() + i < A->index_.count; ++i) {
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
    const dax::Entry& en = A->index_.entries[block_sel];
    if (A->is_font[block_sel]) {
        snprintf(out, cap, "%s #%u  the game's font: %d glyphs of 8x8", A->files[file_sel], en.id, font::kGlyphs);
    } else if (A->is_anim[block_sel] && cur_anim_block == block_sel) {
        const pic::Header& h = A->cur_anim.frame[frame_no];
        snprintf(out, cap, "%s #%u  %dx%d  frame %d/%d  at %u,%u  delay %lu", A->files[file_sel], en.id, h.width_px(),
                 h.height, frame_no + 1, A->cur_anim.frames, h.x_cell, h.y_cell, (unsigned long)A->cur_anim.delay[frame_no]);
    } else if (A->is_pic[block_sel]) {
        const pic::Header& h = A->pic_hdr[block_sel];
        snprintf(out, cap, "%s #%u  %dx%d  frame %d/%d  at %u,%u", A->files[file_sel], en.id, h.width_px(), h.height,
                 frame_no + 1, h.frames, h.x_cell, h.y_cell);
    } else {
        snprintf(out, cap, "%s #%u  %u bytes (%u packed)", A->files[file_sel], en.id, en.raw_size, en.comp_size);
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
    const dax::Entry& en = A->index_.entries[block_sel];
    const int per_line = hex_bytes_per_line(), lines = hex_lines();
    const int per_page = per_line * lines;
    const int pages = en.raw_size == 0 ? 1 : (en.raw_size + per_page - 1) / per_page;
    if (hex_page >= pages) hex_page = 0;
    dax::RleReader r(*src, A->index_, en);
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
        font::draw_glyph(c, A->cur_font, g, x, y, 15, 1);
    }
    font::draw_text(c, A->cur_font, "THE QUICK BROWN FOX JUMPS OVER", 1, 20, 14, 0);
    font::draw_text(c, A->cur_font, "THE LAZY DOG. 0123456789 !?:,'\"-", 1, 21, 14, 0);
    font::draw_text(c, A->cur_font, "Mixed Case Prints In Capitals", 1, 23, 11, 0);
}

// PIC and FINAL files store animation frames as changes from the first one
bool xor_frames()
{
    return strncasecmp(A->files[file_sel], "PIC", 3) == 0 || strncasecmp(A->files[file_sel], "FINAL", 5) == 0;
}

void draw_picture()
{
    pic::Canvas& c = frame::canvas();
    c.clear(0);
    const pic::Header& h = A->pic_hdr[block_sel];
    const int x = (pic::kScreenW - h.width_px()) / 2, y = (pic::kScreenH - h.height) / 2;
    bool ok;
    frame::set_ega_palette();
    if (A->is_font[block_sel]) {
        ok = font::load(*src, A->index_, A->cur_font);
        if (ok) draw_font_sheet(c);
    } else if (A->is_vga[block_sel]) {
        // Its own palette: entries it doesn't set stay black
        pic::Rgb* pal = A->pal;
        for (int i = 0; i < 256; ++i) pal[i] = pic::Rgb{0, 0, 0};
        dax::RleReader r(*src, A->index_, A->index_.entries[block_sel]);
        uint8_t hdr[pic::kVgaHeaderSize];
        pic::VgaHeader vh;
        ok = r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_vga_header(hdr, A->index_.entries[block_sel].raw_size, vh) &&
             pic::read_vga_palette(r, vh, pal);
        for (int i = 0; i < 256; ++i) frame::set_palette(i, pal[i]);
        if (ok) {
            dax::RleReader r2(*src, A->index_, A->index_.entries[block_sel]);
            ok = pic::draw_vga(r2, vh, frame_no, c, x, y);
        }
    } else if (A->is_anim[block_sel]) {
        if (cur_anim_block != block_sel) {
            dax::RleReader r(*src, A->index_, A->index_.entries[block_sel]);
            pic::parse_anim(r, A->index_.entries[block_sel].raw_size, A->cur_anim);
            cur_anim_block = block_sel;
        }
        // Frames keep their positions relative to the first frame
        const pic::Header& f0 = A->cur_anim.frame[0];
        const pic::Header& fh = A->cur_anim.frame[frame_no];
        const int fx = x + (fh.x_cell - f0.x_cell) * 8, fy = y + (fh.y_cell - f0.y_cell) * 8;
        ok = pic::draw_anim(*src, A->index_, A->index_.entries[block_sel], A->cur_anim, frame_no, xor_frames(), c, fx, fy);
    } else {
        dax::RleReader r(*src, A->index_, A->index_.entries[block_sel]);
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
    if (!src || block_sel >= A->index_.count) { go(Screen::Blocks); return; }
    if (A->is_pic[block_sel]) draw_picture(); else draw_hex();
}

// Step to the previous / next frame, then block
void step(int dir)
{
    if (A->is_pic[block_sel]) {
        const int nf = frame_no + dir;
        if (nf >= 0 && nf < A->pic_hdr[block_sel].frames) { frame_no = nf; dirty = true; return; }
    }
    const int nb = block_sel + dir;
    if (nb < 0 || nb >= A->index_.count) return;
    block_sel = nb;
    hex_page = 0;
    frame_no = dir < 0 && A->is_pic[nb] ? A->pic_hdr[nb].frames - 1 : 0;
    dirty = true;
}

void back_to_blocks()
{
    block_page = block_sel / (block_cols() * kBlockRows);
    go(Screen::Blocks);
}

void tap_view(const ui::Tap& t)
{
    if (!A->is_pic[block_sel]) {
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

// Controls under the game screen (SPEC section 4). 480x320 (Tom,
// 2026-10-10): under the game screen a movement pad of 3 x 2 (turn left,
// forward, turn right / side-step left, turn around, side-step right) and
// a cursor pad (Up, Left, Select, Right, Down - small words, so they don't
// look like the movement arrows: the highlighted thing on the game screen,
// menus and lists); beside them, under the Companion map, Game (the
// engine's Menu) / Look / Esc stacked. 320x240: one row - the Walk Test's
// 8 keys; the Play Test's 9 slots showing either the movement keys (Side-
// step Left, Turn Left, Forward, Turn Right, Side-step Right, Turn Around,
// Menu Keys, Game, Map) or the cursor keys (Up, Down, Select - two slots
// wide -, Left, Right, Move Keys, Game, Map; Tom, v0.52.0 - Esc moved to
// the Game menu): the movement keys while the party can walk
// (play::walking), the cursor keys otherwise; Menu Keys / Move Keys swap
// them by hand until the game's state changes.
enum WalkKey { kWTurnL, kWStepL, kWFwd, kWStepR, kWTurnR, kWAround, kWArea, kWNext, kWEsc,
               kWUp, kWLeft, kWSel, kWRight, kWDown, kWCursor, kWMove, kWMap, kWKeys };
constexpr int kWMoveKeys = kWUp;        // the Walk Test has no cursor keys

enum class Row : uint8_t { Walk, Move, Cursor };
Row row = Row::Walk;                    // what 320x240's row shows

ui::Rect walk_key(int k)
{
    const int gp = ui::gap();
    const int top = pic::kScreenH + gp;
    const int h_all = ui::height() - top - gp;
    if (!ui::large()) {
        if (row == Row::Walk) {
            // Row of 8: StepL TurnL Fwd TurnR StepR Around Area Esc (Next Map: menu / panel tap)
            static const int kOrder[kWKeys] = {1, 0, 2, 4, 3, 5, 6, -1, 7, -1, -1, -1, -1, -1, -1, -1, -1};
            const int i = kOrder[k];
            if (i < 0) return ui::Rect{};
            const int w = (pic::kScreenW - gp * 9) / 8;
            return {gp + i * (w + gp), top, w, h_all};
        }
        // 9 slots
        static const int kMove[kWKeys] = {1, 0, 2, 4, 3, 5, 7, -1, -1, -1, -1, -1, -1, -1, 6, -1, 8};
        static const int kCur[kWKeys] = {-1, -1, -1, -1, -1, -1, 7, -1, -1, 0, 4, 2, 5, 1, -1, 6, 8};
        const int i = (row == Row::Move ? kMove : kCur)[k];
        if (i < 0) return ui::Rect{};
        const int w = (pic::kScreenW - gp * 10) / 9;
        if (row == Row::Cursor && i < 6) {
            // Up Down Select Left Right in slots 0-5 (Tom, v0.66.1): Select
            // gives 8 px so each direction key is 2 wider ("Right" fits);
            // Select's text still fits the rest
            constexpr int kGive = 8, kEach = kGive / 4;
            const int dw = w + kEach, sw = w * 2 + gp - kGive;
            static const int kSlotOf[6] = {0, 1, 2, 2, 3, 4};   // slot -> place in the row
            const int p = kSlotOf[i];
            const int x = gp + p * (dw + gp) + (p > 2 ? sw - dw : 0);
            return {x, top, p == 2 ? sw : dw, h_all};
        }
        const int span = k == kWSel ? 2 : 1;            // Select: two slots
        return {gp + i * (w + gp), top, w * span + gp * (span - 1), h_all};
    }
    if (k == kWCursor || k == kWMove || k == kWMap) return ui::Rect{};
    const int pad_w = (pic::kScreenW - gp * 3) / 2;
    const int kw = (pad_w - gp * 2) / 3;
    const int mh = (h_all - gp) / 2;                    // the movement pad: 2 rows
    const int ch = (h_all - gp * 2) / 3;                // the cursor pad: 3 rows
    auto move = [&](int col, int r) { return ui::Rect{gp + col * (kw + gp), top + r * (mh + gp), kw, mh}; };
    const int cx0 = gp * 2 + pad_w;
    auto cur = [&](int col, int r) { return ui::Rect{cx0 + col * (kw + gp), top + r * (ch + gp), kw, ch}; };
    const int sx = pic::kScreenW + gp, sw = ui::width() - sx - gp;
    auto side = [&](int r) { return ui::Rect{sx, top + r * (ch + gp), sw, ch}; };
    switch (k) {
    case kWTurnL:  return move(0, 0);
    case kWFwd:    return move(1, 0);
    case kWTurnR:  return move(2, 0);
    case kWStepL:  return move(0, 1);
    case kWAround: return move(1, 1);
    case kWStepR:  return move(2, 1);
    case kWUp:     return cur(1, 0);
    case kWLeft:   return cur(0, 1);
    case kWSel:    return cur(1, 1);
    case kWRight:  return cur(2, 1);
    case kWDown:   return cur(1, 2);
    case kWArea:   return side(0);
    case kWNext:   return side(1);
    case kWEsc:    return side(2);
    }
    return ui::Rect{};
}

// Where the Companion strip draws the map (480x320)
constexpr int kCompCell = 9, kCompMapY = 30;
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

// 320x240's Play Test row: the movement keys while the party can walk, the
// cursor keys otherwise; row_flip: Cursor / Move swapped them by hand
// (until play::walking() changes)
bool row_flip = false, row_walking = false;

// 320x240's Map key (v0.52.0): the Companion's map over the game screen
// until the next tap; the game waits meanwhile
bool map_shown = false;

// The keys beside the map (480x320) / at the row's end: Area or Game,
// Next Map or Look, Esc (the strip is redrawn with the map)
const char* side_keys[2] = {"Area", "Next Map"};
void redraw_side_keys()
{
    // 320x240's row: the small text of the other keys (Tom, v0.66.1)
    auto side = [](const ui::Rect& r, const char* s, ui::KeyStyle st = ui::KeyStyle::Normal) {
        if (ui::large()) ui::key(r, s, st);
        else ui::key_small(r, s, st);
    };
    side(walk_key(kWArea), side_keys[0]);
    if (ui::large()) ui::key(walk_key(kWNext), side_keys[1]);
    if (walk_key(kWEsc).w > 0) side(walk_key(kWEsc), "Esc");
    if (walk_key(kWMap).w > 0) side(walk_key(kWMap), "Map", map_shown ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
}

void draw_map_grid(const geo::Map& map, const MapSource& ms, int mx, int my, int cell);

// The Gold Box Companion strip (480x320): the whole map, the party arrow
void draw_companion(const MapSource& ms = kWalkMap)
{
    if (!ui::large()) return;
    LGFX& g = ui::gfx();
    const int x0 = pic::kScreenW, w = ui::width() - x0;
    ui::clear_area({x0, 0, w, ui::height()});
    g.drawFastVLine(x0, 0, ui::height(), style::kKeyEdge);
    char l1[48], l2[48];
    ms.describe(l1, l2, sizeof l1);
    // Where the party is (small: for troubleshooting), then the map
    ui::text(x0 + 6, 2, l1, style::kTextMuted, ui::Font::Small);
    ui::text(x0 + 6, 2 + ui::line_h(ui::Font::Small), l2, style::kTextMuted, ui::Font::Small);
    const geo::Map* m = ms.map();
    if (!m) {
        redraw_side_keys();
        return;
    }
    const int cell = kCompCell, my = kCompMapY;
    draw_map_grid(*m, ms, comp_map_x(), my, cell);
    ui::text(x0 + 6, my + cell * geo::kSize + 3, ms.teleport ? "Tap a square to go there" : "Gold: door, red: locked",
             style::kTextMuted, ui::Font::Small);
    redraw_side_keys();
}

// The map's walls and doors, the party a triangle pointing the way it faces
void draw_map_grid(const geo::Map& map, const MapSource& ms, int mx, int my, int cell)
{
    LGFX& g = ui::gfx();
    const geo::Map* m = &map;
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
}

// 320x240's Map key: the map over the game screen, what the 480x320
// Companion strip shows beside it
void draw_map_page()
{
    LGFX& g = ui::gfx();
    g.fillRect(0, 0, pic::kScreenW, pic::kScreenH, style::kBackground);
    const int cell = 12, mx = 4, my = (pic::kScreenH - cell * geo::kSize - 1) / 2;
    const int tx = mx + cell * geo::kSize + 8, tw = pic::kScreenW - tx - 4;
    char l1[48], l2[48];
    kPlayMap.describe(l1, l2, sizeof l1);
    const geo::Map* m = kPlayMap.map();
    if (m) draw_map_grid(*m, kPlayMap, mx, my, cell);
    else wrap_text(mx, my, tx - mx - 8, "No map here.", ui::Font::Normal, style::kText, true);
    int y = my;
    y = wrap_text(tx, y, tw, l1, ui::Font::Small, style::kTextMuted, true) + 2;
    y = wrap_text(tx, y, tw, l2, ui::Font::Small, style::kTextMuted, true) + 8;
    if (m) {
        y = wrap_text(tx, y, tw, "Gold: door", ui::Font::Small, style::kGold, true);
        y = wrap_text(tx, y, tw, "Red: locked", ui::Font::Small, style::kWarn, true) + 8;
    }
    wrap_text(tx, y, tw, "Tap to go back to the game.", ui::Font::Small, style::kText, true);
}

void draw_walk_keys(const char* side_label, const char* area_label, bool cursor)
{
    side_keys[0] = area_label;
    side_keys[1] = side_label;
    if (!ui::large()) {
        // The row: cleared first (it changes between the movement and cursor keys)
        row = !cursor ? Row::Walk : play::walking() == row_flip ? Row::Cursor : Row::Move;
        // (v0.64.0: the keys forgotten too - the movement row's Forward key, under the cursor row's
        // Select, took Select's tap ring: a small key's ghost on it)
        ui::clear_area({0, pic::kScreenH, pic::kScreenW, ui::height() - pic::kScreenH});
    }
    if (row != Row::Cursor || ui::large()) {
        ui::key_arrow(walk_key(kWTurnL), ui::Arrow::TurnLeft);
        ui::key_arrow(walk_key(kWStepL), ui::Arrow::Left);
        ui::key_arrow(walk_key(kWFwd), ui::Arrow::Forward);
        ui::key_arrow(walk_key(kWStepR), ui::Arrow::Right);
        ui::key_arrow(walk_key(kWTurnR), ui::Arrow::TurnRight);
        ui::key_arrow(walk_key(kWAround), ui::Arrow::TurnAround);
    }
    if (cursor && (ui::large() || row == Row::Cursor)) {
        ui::key_small(walk_key(kWUp), "Up");
        ui::key_small(walk_key(kWLeft), "Left");
        ui::key_small(walk_key(kWSel), "Select");
        ui::key_small(walk_key(kWRight), "Right");
        ui::key_small(walk_key(kWDown), "Down");
    }
    if (!ui::large() && row == Row::Move) ui::key_small(walk_key(kWCursor), "Menu\nKeys");
    if (!ui::large() && row == Row::Cursor) ui::key_small(walk_key(kWMove), "Move\nKeys");
    redraw_side_keys();
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
    draw_walk_keys("Next Map", "Area", false);
    draw_companion();
}

void tap_walk(const ui::Tap& t)
{
    if (walk_error) {
        if (ui::back_rect().contains(t.x, t.y)) leave_walk();
        return;
    }
    static const walk::Act kActs[kWMoveKeys] = {walk::Act::TurnLeft, walk::Act::StepLeft, walk::Act::Forward,
                                            walk::Act::StepRight, walk::Act::TurnRight, walk::Act::TurnAround,
                                            walk::Act::Area, walk::Act::NextMap, walk::Act::Forward};
    for (int k = 0; k < kWMoveKeys; ++k) {
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
    // (the keys that were there forgotten: the tap ring goes on the keyboard's keys only)
    if (ui::large()) {
        ui::clear_area({0, pic::kScreenH, ui::width(), ui::height() - pic::kScreenH});
        ui::clear_area({pic::kScreenW, 0, ui::width() - pic::kScreenW, pic::kScreenH});
    } else {
        ui::clear_area({0, 0, pic::kScreenW, text::kTextArea.y0 * 8});
        ui::clear_area({0, pic::kScreenH, ui::width(), ui::height() - pic::kScreenH});
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

// ---- The engine's Menu (Play Test) ----------------------------------------
// Tom (2026-10-09): the Menu key (where the Walk Test has Area) opens the
// engine's own screen, tabbed along the top: Journal (the entries and
// tavern tales the game has mentioned so far - tap one to read it) and
// Journal PDF (the book). More tabs as the engine grows. Back to Game at
// the bottom.

enum MenuTab { kTabJournal, kTabPdf, kTabSounds, kTabOptions, kTabs };
int  menu_tab = kTabJournal;
int  menu_page = 0;
bool from_menu = false;       // the journal / PDF screens go back to the Menu
char menu_note[160] = {};

// 320x240: Esc lives in the Game menu, at the tab bar's right (Tom,
// v0.52.0 - its place in the row went to Map): back to the game, Esc
ui::Rect menu_esc_rect()
{
    if (ui::large()) return ui::Rect{};
    const int gp = ui::gap(), w = 44;
    return {ui::width() - gp - w, 2, w, ui::header_h() - 4};
}

ui::Rect tab_rect(int i)
{
    const int gp = ui::gap();
    const int right = ui::large() ? ui::width() : menu_esc_rect().x;
    const int w = (right - gp * (kTabs + 1)) / kTabs;
    return {gp + i * (w + gp), 2, w, ui::header_h() - 4};
}

void draw_tabs(int active)
{
    // (320x240: four tabs and Esc - the book's tab is "PDF")
    static const char* const kNames[kTabs] = {"Journal", "Journal PDF", "Sounds", "Options"};
    static const char* const kShort[kTabs] = {"Journal", "PDF", "Sounds", "Options"};
    ui::gfx().fillRect(0, 0, ui::width(), ui::header_h(), style::kHeader);
    for (int i = 0; i < kTabs; ++i)
        ui::key(tab_rect(i), ui::large() ? kNames[i] : kShort[i], i == active ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    if (menu_esc_rect().w > 0) ui::key(menu_esc_rect(), "Esc");
}

bool menu_esc_hit(const ui::Tap& t)
{
    const ui::Rect r = menu_esc_rect();
    return r.w > 0 && r.contains(t.x, t.y);
}

int tab_hit(const ui::Tap& t)
{
    for (int i = 0; i < kTabs; ++i)
        if (tab_rect(i).contains(t.x, t.y)) return i;
    return -1;
}


ui::Rect journal_area();
void open_journal(char kind, int number);
bool open_book();

int menu_row_h() { return ui::line_h(ui::Font::Normal) + ui::gap() * 2; }
int menu_rows() { return journal_area().h / menu_row_h(); }
int menu_pages()
{
    const int n = play::journal_seen_count(), r = menu_rows();
    return r > 0 && n > 0 ? (n + r - 1) / r : 1;
}

void open_menu()
{
    from_menu = true;
    menu_tab = kTabJournal;
    menu_note[0] = 0;
    const int r = menu_rows();
    // Start on the page with the latest entry
    menu_page = r > 0 && play::journal_seen_count() ? (play::journal_seen_count() - 1) / r : 0;
    go(Screen::GameMenu);
}

void leave_menu()
{
    from_menu = false;
    go(Screen::Play);
}

void close_book();
void leave_play();

// The Game menu's Esc (320x240): back to the game, then Esc as the row's
// key did (out of what the game shows; at the top, out of the Play Test)
void menu_esc()
{
    close_book();
    leave_menu();
    if (play_error) return;
    if (!play::back(frame::canvas())) leave_play();
}

// The Sounds tab: the game's sound effects to hear, Tandy or PC speaker
// (the Settings choice; Tom: to compare them)
struct SoundKey {
    uint8_t     id;
    const char* name;
};
constexpr SoundKey kSoundKeys[] = {
    {0x0A, "Step"},  {0x07, "Hit"},       {0x09, "Miss"},      {0x0C, "Missile"},
    {0x06, "Sling"}, {0x02, "Spell"},     {0x03, "Magic Hit"}, {0x04, "Magic Stars"},
    {0x0B, "Fireball"}, {0x08, "Lightning"}, {0x05, "Death"}, {0x0D, "Title"},
};
constexpr int kSoundKeyCount = sizeof kSoundKeys / sizeof kSoundKeys[0];

void draw_slider(const ui::Rect& r, bool volume);
bool slider_set(const ui::Rect& r, int x, bool volume);

ui::Rect sound_key(int i)          // i < 0: the top row (-1 Tandy, -2 PC Speaker, -3 the volume slider)
{
    const ui::Rect a = journal_area();
    const int gp = ui::gap(), cols = 4, rows = kSoundKeyCount / cols + 1;
    const int w = (a.w - gp * (cols + 1)) / cols, h = (a.h - gp * (rows + 1)) / rows;
    if (i < 0) {
        const int dw = (a.w - gp * 4) / 3;
        return {a.x + gp + (-i - 1) * (dw + gp), a.y + gp, dw, h};
    }
    return {a.x + gp + (i % cols) * (w + gp), a.y + gp + (i / cols + 1) * (h + gp), w, h};
}

void draw_sounds()
{
    ui::key(sound_key(-1), "Tandy", cfg->sound == kSoundTandy ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    ui::key(sound_key(-2), "PC Speaker", cfg->sound == kSoundPc ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    draw_slider(sound_key(-3), true);
    for (int i = 0; i < kSoundKeyCount; ++i) ui::key(sound_key(i), kSoundKeys[i].name);
}

void tap_sounds(const ui::Tap& t)
{
    if (sound_key(-3).contains(t.x, t.y)) {
        if (slider_set(sound_key(-3), t.x, true)) {
            draw_slider(sound_key(-3), true);
            settings_save(*cfg);
        }
        return;
    }
    for (int d2 = 1; d2 <= 2; ++d2)
        if (sound_key(-d2).contains(t.x, t.y)) {
            cfg->sound = d2 == 1 ? kSoundTandy : kSoundPc;
            settings_save(*cfg);
            play::set_sound(cfg->sound, cfg->volume);
            dirty = true;
            return;
        }
    for (int i = 0; i < kSoundKeyCount; ++i)
        if (sound_key(i).contains(t.x, t.y)) {
            if (cfg->sound != kSoundTandy && cfg->sound != kSoundPc) {
                cfg->sound = kSoundTandy;           // (it was off: the keys above say which)
                settings_save(*cfg);
                play::set_sound(cfg->sound, cfg->volume);
                dirty = true;
            }
            play::sound_test(kSoundKeys[i].id);
            return;
        }
}

ui::Rect options_key()
{
    const ui::Rect a = journal_area();
    return {ui::gap() * 3, a.y + ui::gap() * 2, ui::width() - ui::gap() * 6, ui::key_h()};
}

void draw_game_menu()
{
    ui::clear();
    draw_tabs(menu_tab);
    const ui::Rect a = journal_area();
    if (menu_tab == kTabSounds) {
        ui::key(ui::bottom_key(0, 1), "Back to Game");
        draw_sounds();
        return;
    }
    if (menu_tab == kTabOptions) {
        ui::key(ui::bottom_key(0, 1), "Back to Game");
        const int x = ui::gap() * 3, wdt = ui::width() - ui::gap() * 6;
        int y = a.y + ui::gap() * 2;
        if (!CYD_BIG_ICON_PREVIEW) {
            wrap_text(x, y, wdt, "Nothing to set here yet.", ui::Font::Normal, style::kText, true);
            return;
        }
        if (ui::large()) {
            wrap_text(x, y, wdt,
                      "Large icons: on this screen the icon editor (Alter, Icon) always shows the new icon large "
                      "beside it. Tap it to see the other pose.",
                      ui::Font::Normal, style::kText, true);
            return;
        }
        ui::key(options_key(), cfg->large_icons ? "Large Icons: On" : "Large Icons: Off",
                cfg->large_icons ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
        wrap_text(x, options_key().y + options_key().h + ui::gap() * 2, wdt,
                  "The icon editor (Alter, Icon) shows the new icon large in its empty space. Tap it to see the "
                  "other pose.",
                  ui::Font::Small, style::kTextMuted, true);
        return;
    }
    const int pages = menu_pages();
    if (pages > 1) {
        ui::key(ui::bottom_key(0, 3), "Prev Page", menu_page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
        ui::key(ui::bottom_key(1, 3), "Back to Game");
        ui::key(ui::bottom_key(2, 3), "Next Page", menu_page < pages - 1 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    } else {
        ui::key(ui::bottom_key(0, 1), "Back to Game");
    }
    const int x = ui::gap() * 3, wdt = ui::width() - ui::gap() * 6;
    if (menu_tab == kTabPdf || menu_note[0]) {
        wrap_text(x, a.y + ui::gap() * 2, wdt, menu_note, ui::Font::Normal, style::kText, true);
        return;
    }
    const int n = play::journal_seen_count();
    if (n == 0) {
        wrap_text(x, a.y + ui::gap() * 2, wdt,
                  "No journal entries yet. When the game tells you to read one, it is listed here.",
                  ui::Font::Normal, style::kText, true);
        return;
    }
    const int r = menu_rows(), rh = menu_row_h();
    for (int i = 0; i < r; ++i) {
        const int k = menu_page * r + i;
        char kind;
        int num;
        if (!play::journal_seen(k, &kind, &num)) break;
        char line[40];
        snprintf(line, sizeof line, "%s %d", kind == 'T' ? "Tavern Tale" : "Journal Entry", num);
        const int y = a.y + i * rh;
        ui::text(x, y + ui::gap(), line, k == n - 1 ? style::kGold : style::kText);
        if (i + 1 < r) ui::gfx().drawFastHLine(x, y + rh - 1, wdt, style::kKeyEdge);
    }
}

void tap_game_menu(const ui::Tap& t)
{
    if (menu_esc_hit(t)) {
        menu_esc();
        return;
    }
    const int tab = tab_hit(t);
    if (tab == kTabPdf) {
        menu_tab = kTabPdf;
        if (!open_book()) {
            strlcpy(menu_note,
                    game_dirs[game_sel].journal[0]
                        ? "The journal PDF couldn't be read as a book of scanned pages."
                        : "There is no journal PDF in this game's folder. GOG's install folder has one: copy the "
                          "whole folder to the card, then Rescan Card.",
                    sizeof menu_note);
            dirty = true;
        }
        return;
    }
    if (tab == kTabJournal || tab == kTabSounds || tab == kTabOptions) {
        menu_tab = tab;
        menu_note[0] = 0;
        dirty = true;
        return;
    }
    if (menu_tab == kTabSounds) {
        if (bottom_hit(t, 1) == 0) leave_menu();
        else tap_sounds(t);
        return;
    }
    if (menu_tab == kTabOptions) {
        if (bottom_hit(t, 1) == 0) {
            leave_menu();
        } else if (CYD_BIG_ICON_PREVIEW && !ui::large() && options_key().contains(t.x, t.y)) {
            cfg->large_icons = !cfg->large_icons;
            settings_save(*cfg);
            dirty = true;
        }
        return;
    }
    const int pages = menu_pages();
    const int nk = pages > 1 ? 3 : 1;
    const int k = bottom_hit(t, nk);
    if (k >= 0) {
        if (nk == 1 || k == 1) leave_menu();
        else if (k == 0 && menu_page > 0) --menu_page, dirty = true;
        else if (k == 2 && menu_page < pages - 1) ++menu_page, dirty = true;
        return;
    }
    if (menu_tab != kTabJournal) return;
    const ui::Rect a = journal_area();
    if (!a.contains(t.x, t.y)) return;
    const int i = menu_page * menu_rows() + (t.y - a.y) / menu_row_h();
    char kind;
    int num;
    if (play::journal_seen(i, &kind, &num)) open_journal(kind, num);
}


// ---- Journal ------------------------------------------------------------
// An entry of the Adventurer's Journal (or a tavern tale): the pictures the
// card scan made from the player's own journal PDF (_CYD/<folder>/
// JOURNAL.DAT), scaled to the screen's width. Prev Page / Next Page; Back
// to Game returns to the game.

constexpr int kMaxJournalPieces = 8;

struct JournalView {
    char     kind = 'J';
    int      number = 0;
    int      top = 0;                     // first row on screen (scaled)
    int      left = 0;                    // zoomed in: first scan column shown
    int      fit = 1;                     // kFitWhole / kFitWidth / kFitFull (the Zoom key cycles them)
    bool     have = false;
    bool     has_pdf = false;             // the game's folder has a journal PDF (the book view)
    char     why[220] = {};
    journal::Info info;
    int      count = 0;
    journal::PieceInfo piece[kMaxJournalPieces];
    int      maxw = 0;                    // the widest piece (scan pixels)
    int      out_h[kMaxJournalPieces] = {};
    int      total_h = 0;
    int      shown_w = 0;                 // panel columns used
    int      scale256 = 256;              // panel pixels per scan pixel x 256
    lgfx::rgb888_t pal[256];
    lgfx::rgb888_t line[480];
    int16_t  sx0[480];                    // per panel column: scan column in the piece (-1: none) and weight
    uint8_t  sxf[480];
    uint8_t  row[2][1700];
    int      row_y[2] = {-1, -1};
    int      row_piece = -1;
};
JournalView* jv = nullptr;

// Zoom levels (Tom, 2026-10-09: the Zoom key cycles them): the whole
// entry / page, its width across the screen, the scan's own pixels
enum { kFitWhole = 0, kFitWidth = 1, kFitFull = 2, kFits = 3 };

// Dragging the picture (Tom, 2026-10-09): it moves when the stylus lifts
// (redrawing a scan as it moves would be too slow); taps on its edges too
int  pan_x = 0, pan_y = 0;
bool panning = false;
bool pan_done(int* dx, int* dy)
{
    int x, y;
    const bool on = ui::drag(x, y);
    pan_x += x;
    pan_y += y;
    if (on) {
        panning = true;
        return false;
    }
    const bool done = panning;
    panning = false;
    *dx = pan_x;
    *dy = pan_y;
    pan_x = pan_y = 0;
    return done;
}

// The book view (the journal PDF's pages)
int  pdf_page = 1;
int  pdf_level = kFitWhole;
int  pdf_vx = 0, pdf_vy = 0;           // the view's top left, in shown pixels
ui::Rect pdf_fit;

ui::Rect journal_area()
{
    const int top = ui::header_h() + 2;
    return {0, top, ui::width(), ui::height() - top - ui::key_h() - ui::gap() * 2};
}
// The viewers - a journal entry, the book (Tom, 2026-10-10): no title bar
// (Back is in the bottom row) and a slim row of keys about 57% of the usual
// height; the page numbers sit in the row between Prev and Next
int slim_h() { return ui::large() ? 32 : 22; }
int slim_gap() { return ui::large() ? 4 : 3; }
ui::Rect slim_key(int i, int n)
{
    const int gp = slim_gap();
    const int w = (ui::width() - gp * (n + 1)) / n;
    return {gp + i * (w + gp), ui::height() - slim_h() - gp, w, slim_h()};
}
int slim_hit(const ui::Tap& t, int n)
{
    for (int i = 0; i < n; ++i)
        if (slim_key(i, n).contains(t.x, t.y)) return i;
    return -1;
}
ui::Rect view_area() { return {0, 0, ui::width(), ui::height() - slim_h() - slim_gap() * 2}; }
// The page numbers' cell (two small lines, not a key)
void slim_label(const ui::Rect& r, const char* a, const char* b)
{
    const int lh = ui::line_h(ui::Font::Small);
    const int y = r.y + (r.h - lh * (b ? 2 : 1)) / 2;
    ui::text_center({r.x, y, r.w, lh}, a, style::kTextMuted, ui::Font::Small);
    if (b) ui::text_center({r.x, y + lh, r.w, lh}, b, style::kTextMuted, ui::Font::Small);
}
// The zoom key names the level shown
const char* const kFitSlim[kFits] = {"Whole", "Width", "Full Size"};

int journal_step() { return view_area().h - 24; }        // a page, keeping a little for context

bool journal_source(fs::File& f)
{
    char path[160];
    library::cache_path(game_dirs[game_sel], "JOURNAL.DAT", path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

// Sizes for fitted (the screen's width) or zoomed (1:1) viewing
void journal_layout()
{
    JournalView& v = *jv;
    const int room = ui::width() - 6;
    int full_h = 0;
    for (int i = 0; i < v.count; ++i) full_h += v.piece[i].h;
    if (v.fit == kFitFull) {
        v.scale256 = 256;
    } else {
        v.scale256 = room * 256 / v.maxw;
        if (v.fit == kFitWhole && full_h > 0) {
            const int fh = view_area().h * 256 / full_h;
            if (fh < v.scale256) v.scale256 = fh;
        }
        if (v.scale256 < 1) v.scale256 = 1;
    }
    v.shown_w = v.maxw * v.scale256 / 256;
    if (v.shown_w > ui::width()) v.shown_w = ui::width();
    if (v.shown_w > 480) v.shown_w = 480;
    v.total_h = 0;
    for (int i = 0; i < v.count; ++i) {
        v.out_h[i] = v.piece[i].h * v.scale256 / 256;
        v.total_h += v.out_h[i];
    }
    const int max_left = v.fit == kFitFull && v.maxw > v.shown_w ? v.maxw - v.shown_w : 0;
    const int max_top = v.total_h > view_area().h ? v.total_h - view_area().h : 0;
    if (v.top > max_top) v.top = max_top;
    if (v.top < 0) v.top = 0;
    if (v.left > max_left) v.left = max_left;
    if (v.left < 0) v.left = 0;
    v.row_piece = -1;
}

void open_journal(char kind, int number)
{
    if (!jv) jv = new (std::nothrow) JournalView;
    if (!jv) return;
    jv->kind = kind;
    jv->number = number;
    jv->top = jv->left = 0;
    jv->fit = kFitWidth;
    jv->have = false;
    jv->row_y[0] = jv->row_y[1] = -1;
    jv->row_piece = -1;
    const library::GameDir& g = game_dirs[game_sel];
    jv->has_pdf = g.journal[0] != 0;
    const char* what = kind == 'T' ? "Tavern Tale" : "Journal Entry";
    // Without the pictures: as the original game, the printed journal - or
    // the PDF as a book
    snprintf(jv->why, sizeof jv->why, "Read %s %d in your Adventurer's Journal.%s", what, number,
             !g.journal[0] ? ""
                           : " The journal PDF in this game's folder isn't prepared (an edition the engine doesn't know, "
                             "or the card needs a rescan): Open Journal PDF shows its pages.");
    fs::File f;
    if (journal_source(f)) {
        library::FileSource src(f);
        int first = 0;
        if (journal::read_info(src, jv->info) && journal::find(src, jv->info, kind, number, &first, &jv->count) &&
            jv->count <= kMaxJournalPieces) {
            bool ok = true;
            jv->maxw = 0;
            for (int i = 0; i < jv->count && ok; ++i) {
                ok = journal::piece(src, jv->info, first + i, jv->piece[i]) && jv->piece[i].w <= 1700;
                if (jv->piece[i].w > jv->maxw) jv->maxw = jv->piece[i].w;
            }
            if (ok && jv->maxw > 0) {
                for (int i = 0; i < 256; ++i)
                    jv->pal[i] = lgfx::rgb888_t(jv->info.palette[i][0], jv->info.palette[i][1], jv->info.palette[i][2]);
                jv->have = true;
                journal_layout();
            }
        }
        f.close();
    }
    go(Screen::Journal);
}

// Panel row y of piece i, sampled (bilinear) from the two scan rows round it
void journal_draw_row(library::FileSource& src, int i, int y, int sy_screen)
{
    JournalView& v = *jv;
    const journal::PieceInfo& p = v.piece[i];
    const int px0 = (v.maxw - p.w) / 2;            // the piece's place across the entry (centred)
    if (v.row_piece != i) {
        // The scan columns the panel's columns come from
        for (int x = 0; x < v.shown_w; ++x) {
            int s = (x * 2 + 1) * 32768 / v.scale256 - 128 + (v.left - px0) * 256;   // ((x + .5) / f - .5 + left - px0)
            int x0 = s >> 8;
            if (s < -128 || x0 >= p.w) {
                v.sx0[x] = -1;
                continue;
            }
            if (s < 0) s = 0, x0 = 0;
            if (x0 >= p.w - 1) {
                x0 = p.w - 1;
                s = x0 << 8;
            }
            v.sx0[x] = static_cast<int16_t>(x0);
            v.sxf[x] = static_cast<uint8_t>(s & 255);
        }
        v.row_piece = i;
        v.row_y[0] = v.row_y[1] = -1;
    }
    int s = (y * 2 + 1) * 32768 / v.scale256 - 128;
    if (s < 0) s = 0;
    int y0 = s >> 8;
    if (y0 >= p.h - 1) {
        y0 = p.h - 1;
        s = y0 << 8;
    }
    const int y1 = y0 + 1 < p.h ? y0 + 1 : y0;
    const int fy = s & 255;
    const int need[2] = {y0, y1};
    uint8_t* rows[2] = {nullptr, nullptr};
    bool used[2] = {false, false};
    for (int k = 0; k < 2; ++k)
        for (int j = 0; j < 2 && !rows[k]; ++j)
            if (v.row_y[j] == need[k]) {
                rows[k] = v.row[j];
                used[j] = true;
            }
    for (int k = 0; k < 2; ++k) {
        if (rows[k]) continue;
        const int j = used[0] ? 1 : 0;
        src.read_at(p.offset + static_cast<uint32_t>(need[k]) * p.w, v.row[j], p.w);
        v.row_y[j] = need[k];
        used[j] = true;
        rows[k] = v.row[j];
    }
    const lgfx::rgb888_t white(255, 255, 255);
    for (int x = 0; x < v.shown_w; ++x) {
        if (v.sx0[x] < 0) {
            v.line[x] = white;
            continue;
        }
        const int x0 = v.sx0[x], x1 = x0 + 1 < p.w ? x0 + 1 : x0, fx = v.sxf[x];
        const lgfx::rgb888_t& a = v.pal[rows[0][x0]];
        const lgfx::rgb888_t& b = v.pal[rows[0][x1]];
        const lgfx::rgb888_t& c = v.pal[rows[1][x0]];
        const lgfx::rgb888_t& d = v.pal[rows[1][x1]];
        auto mix = [&](int pa, int pb, int pc, int pd) {
            const int top = pa * (256 - fx) + pb * fx, bot = pc * (256 - fx) + pd * fx;
            return static_cast<uint8_t>((top * (256 - fy) + bot * fy) >> 16);
        };
        v.line[x] = lgfx::rgb888_t(mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b));
    }
    ui::gfx().pushImage((ui::width() - v.shown_w) / 2, sy_screen, v.shown_w, 1, v.line);
}

// Keys (Tom, 2026-10-09 / 10): with the entry, Back | Zoom | Prev | the
// entry and its page | Next (5 cells); without it, Back | Journal PDF (when
// there is one)
int journal_keys() { return jv && jv->have ? 5 : (jv && jv->has_pdf ? 2 : 1); }

void draw_journal()
{
    ui::clear();
    const ui::Rect a = view_area();
    // Just "2 of 3" (Tom, v0.66.1): the entry's number is in its picture
    char page[24];
    int pages = 1;
    if (jv && jv->have && jv->total_h > a.h)
        pages = (jv->total_h - a.h + journal_step() - 1) / journal_step() + 1;
    snprintf(page, sizeof page, "%d of %d", jv ? jv->top / journal_step() + 1 : 1, pages);
    const int nk = journal_keys();
    if (nk == 5) {
        const bool more = jv->top + a.h < jv->total_h;
        ui::key(slim_key(0, 5), "Back");
        ui::key(slim_key(1, 5), kFitSlim[jv->fit], jv->fit != kFitWidth ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
        ui::key(slim_key(2, 5), "Prev", jv->top > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
        slim_label(slim_key(3, 5), page, nullptr);
        ui::key(slim_key(4, 5), "Next", more ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    } else {
        ui::key(slim_key(0, nk), "Back");
        if (nk == 2) ui::key(slim_key(1, 2), "Journal PDF");
    }
    if (!jv || !jv->have) {
        wrap_text(ui::gap() * 3, a.y + ui::gap() * 2, ui::width() - ui::gap() * 6, jv ? jv->why : "",
                  ui::Font::Normal, style::kText, true);
        return;
    }
    // On white, like the journal's paper
    LGFX& g = ui::gfx();
    g.fillRect(0, a.y, a.w, a.h, TFT_WHITE);
    fs::File f;
    if (!journal_source(f)) return;
    library::FileSource src(f);
    g.startWrite();
    int y0 = 0;                           // where the piece starts (scaled)
    for (int i = 0; i < jv->count; ++i) {
        for (int y = 0; y < jv->out_h[i]; ++y) {
            const int sy = y0 + y - jv->top;
            if (sy < 0) continue;
            if (sy >= a.h) break;
            journal_draw_row(src, i, y, a.y + sy);
        }
        y0 += jv->out_h[i];
        if (y0 - jv->top >= a.h) break;
    }
    g.endWrite();
    f.close();
}

// The journal book needs ~50 KB while it draws a page; beside the Play
// Test that wasn't there any more (Tom, v0.51.0: "This page has no scanned
// picture." again - a memory failure reported as no picture). So while the
// book is open the game screen waits on the card (v0.52.0).
void canvas_park_path(char* out, size_t cap) { library::cache_path("CANVAS.TMP", out, cap); }

void close_book()
{
    pdfview::close();
    if (!frame::parked()) return;
    char path[96];
    canvas_park_path(path, sizeof path);
    if (!frame::unpark(path))
        play_error = "Not enough memory to bring the game screen back after the journal book. Tap Back, then start "
                     "the Play Test again.";
}

void leave_journal()
{
    delete jv;
    jv = nullptr;
    close_book();
    // The Menu (when it opened the entry or the book), or the game screen
    // as it was
    go(from_menu ? Screen::GameMenu : Screen::Play);
}

// The journal PDF as a book (the Menu's Journal PDF tab, or Open Journal
// PDF on an entry): false if there is none or it can't be read
bool open_book()
{
    if (!game_dirs[game_sel].journal[0]) return false;
    char path[200];
    snprintf(path, sizeof path, "%s/%s", games::kRootDir, game_dirs[game_sel].journal);
    struct Job {
        const char* path;
        bool        ok;
    } job{path, false};
    char cpath[96];
    canvas_park_path(cpath, sizeof cpath);
    frame::park(cpath);                  // false: the canvas stays (the card wouldn't take it)
    // PDF parsing on a stack of its own (deep)
    if (!run_on_big_stack([](void* p) { auto* j = static_cast<Job*>(p); j->ok = pdfview::open(j->path); }, &job) ||
        !job.ok) {
        close_book();
        return false;
    }
    pdf_page = 1;
    pdf_level = kFitWhole;
    pdf_vx = pdf_vy = 0;
    go(Screen::Pdf);
    return true;
}

void tap_journal(const ui::Tap& t)
{
    const int nk = journal_keys();
    int k = slim_hit(t, nk);
    if (nk == 5) k = k == 3 ? -2 : k == 4 ? 3 : k;            // (the page cell isn't a key)
    if (k == 0) { leave_journal(); return; }                  // Back
    if (nk == 2 && k == 1) {
        if (!open_book()) {
            strlcpy(jv->why, "The journal PDF couldn't be read as a book of scanned pages.", sizeof jv->why);
            dirty = true;
        }
        return;
    }
    if (!jv || !jv->have || k == -2) return;
    const ui::Rect a = view_area();
    if (k == 2 && jv->top > 0) {
        jv->top = jv->top > journal_step() ? jv->top - journal_step() : 0;
        dirty = true;
    } else if (k == 3 && jv->top + a.h < jv->total_h) {
        jv->top += journal_step();
        dirty = true;
    } else if (k == 1) {
        // Zoom: the next level (width -> full size -> whole -> width), the
        // same part of the entry staying in view
        const int64_t mid = jv->total_h > 0 ? (static_cast<int64_t>(jv->top) + a.h / 2) * 65536 / jv->total_h : 0;
        jv->fit = (jv->fit + 1) % kFits;
        journal_layout();
        jv->top = static_cast<int>(mid * jv->total_h / 65536) - a.h / 2;
        jv->left = jv->fit == kFitFull ? (jv->maxw - jv->shown_w) / 2 : 0;
        journal_layout();
        dirty = true;
    } else if (k < 0 && jv->fit == kFitFull && a.contains(t.x, t.y) && jv->maxw > jv->shown_w) {
        // At full size on something wider than the screen: tap the left or
        // right edge to move that way (or drag)
        const int step = jv->shown_w * 2 / 3;
        if (t.x < a.w / 3) jv->left -= step;
        else if (t.x > a.w * 2 / 3) jv->left += step;
        journal_layout();
        dirty = true;
    }
}

// A drag on the entry moves it (when the stylus lifts)
void tick_journal()
{
    int dx, dy;
    if (!pan_done(&dx, &dy) || !jv || !jv->have) return;
    jv->top -= dy;
    jv->left -= dx * 256 / (jv->scale256 > 0 ? jv->scale256 : 256);
    journal_layout();
    dirty = true;
}

// ---- The journal PDF as a book ------------------------------------------
// Whole pages; Zoom cycles whole page -> page width -> full size (the
// scan's pixels); drag the page, or tap its edges, to move round it.

ui::Rect pdf_area() { return view_area(); }
pdfview::Fit pdf_fit_of(int level)
{
    return level == kFitFull ? pdfview::Fit::Full : level == kFitWidth ? pdfview::Fit::Width : pdfview::Fit::Page;
}

void draw_pdf()
{
    ui::clear();
    // Keys (Tom, 2026-10-09 / 10): Back | Zoom | Prev | the page | Next, slim, no title bar
    char page[24];
    snprintf(page, sizeof page, "%d of %d", pdf_page, pdfview::pages());     // just "3 of 48" (Tom, v0.66.1)
    ui::key(slim_key(0, 5), "Back");
    ui::key(slim_key(1, 5), kFitSlim[pdf_level], pdf_level != kFitWhole ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    ui::key(slim_key(2, 5), "Prev", pdf_page > 1 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    slim_label(slim_key(3, 5), page, nullptr);
    ui::key(slim_key(4, 5), "Next", pdf_page < pdfview::pages() ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    const ui::Rect a = pdf_area();
    ui::text(ui::gap() * 3, a.y + ui::gap() * 2, "Reading the page...", style::kTextMuted, ui::Font::Small);
    // JPEG decoding on a stack of its own (deep)
    struct Job {
        ui::Rect        a;
        pdfview::Result r;
    } job{a, pdfview::Result::NoMemory};
    run_on_big_stack(
        [](void* p) {
            auto* j = static_cast<Job*>(p);
            j->r = pdfview::draw(pdf_page, j->a, pdf_fit_of(pdf_level), &pdf_vx, &pdf_vy, &pdf_fit);
        },
        &job);
    LGFX& g = ui::gfx();
    if (job.r != pdfview::Result::Ok) {
        g.fillRect(0, a.y, a.w, a.h, style::kBackground);
        char why[160];
        if (job.r == pdfview::Result::NoPicture)
            snprintf(why, sizeof why, "This page has no scanned picture.");
        else if (job.r == pdfview::Result::BadData)
            snprintf(why, sizeof why, "This page's picture couldn't be read.");
        else
            snprintf(why, sizeof why, "Not enough memory to show this page (free %u KB, largest block %u KB).",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024));
        wrap_text(ui::gap() * 3, a.y + ui::gap() * 2, a.w - ui::gap() * 6, why, ui::Font::Normal, style::kText, true);
    } else {
        // Clear round the page (the "Reading" line)
        g.fillRect(a.x, a.y, a.w, pdf_fit.y - a.y, style::kBackground);
        g.fillRect(a.x, pdf_fit.y, pdf_fit.x - a.x, pdf_fit.h, style::kBackground);
    }
}

// A drag on the page moves it (when the stylus lifts)
void tick_pdf()
{
    int dx, dy;
    if (!pan_done(&dx, &dy)) return;
    pdf_vx -= dx;
    pdf_vy -= dy;
    dirty = true;                    // pdfview::draw keeps the view on the page
}

void tap_pdf(const ui::Tap& t)
{
    const ui::Rect a = pdf_area();
    int k = slim_hit(t, 5);
    if (k == 3) return;                                       // (the page cell isn't a key)
    if (k == 4) k = 3;
    if (k == 0) {
        if (from_menu && !jv) {
            // The Menu's book tab: back to the Menu (its journal list)
            close_book();
            menu_tab = kTabJournal;
            menu_note[0] = 0;
            go(Screen::GameMenu);
            return;
        }
        leave_journal();
        return;
    } else if (k == 2 && pdf_page > 1) {
        --pdf_page;
        pdf_vx = pdf_vy = 0;
    } else if (k == 3 && pdf_page < pdfview::pages()) {
        ++pdf_page;
        pdf_vx = pdf_vy = 0;
    } else if (k == 1) {
        // The next level, the middle of the view staying in the middle
        int w0 = 1, h0 = 1, w1 = 1, h1 = 1;
        pdfview::shown_size(pdf_page, a, pdf_fit_of(pdf_level), &w0, &h0);
        const int next = (pdf_level + 1) % kFits;
        pdfview::shown_size(pdf_page, a, pdf_fit_of(next), &w1, &h1);
        const int64_t cx = (static_cast<int64_t>(pdf_vx) + (w0 < a.w ? w0 : a.w) / 2) * 65536 / (w0 > 0 ? w0 : 1);
        const int64_t cy = (static_cast<int64_t>(pdf_vy) + (h0 < a.h ? h0 : a.h) / 2) * 65536 / (h0 > 0 ? h0 : 1);
        pdf_level = next;
        pdf_vx = static_cast<int>(cx * w1 / 65536) - a.w / 2;
        pdf_vy = next == kFitWidth ? 0 : static_cast<int>(cy * h1 / 65536) - a.h / 2;
    } else if (k < 0 && a.contains(t.x, t.y)) {
        if (pdf_level == kFitWhole) {
            // Tap a spot on the whole page: see it at full size
            int pw = 0, ph = 0;
            if (!pdf_fit.contains(t.x, t.y) || pdf_fit.w == 0 || !pdfview::page_size(pdf_page, &pw, &ph)) return;
            pdf_level = kFitFull;
            pdf_vx = (t.x - pdf_fit.x) * pw / pdf_fit.w - a.w / 2;
            pdf_vy = (t.y - pdf_fit.y) * ph / pdf_fit.h - a.h / 2;
        } else {
            // Tap an edge to move that way (or drag)
            const int sx = a.w * 2 / 3, sy = a.h * 2 / 3;
            if (t.x < a.x + a.w / 4) pdf_vx -= sx;
            else if (t.x > a.x + a.w * 3 / 4) pdf_vx += sx;
            else if (t.y < a.y + a.h / 2) pdf_vy -= sy;
            else pdf_vy += sy;
        }
    } else {
        return;
    }
    dirty = true;                    // pdfview::draw keeps the view on the page
}

void leave_play()
{
    kb_shown = false;
    delete jv;
    jv = nullptr;
    play::close();
    play_error = nullptr;
    if (frame::parked()) {
        // Left on the card after the journal book (no memory then): the
        // Play Test's memory is free now
        char path[96];
        canvas_park_path(path, sizeof path);
        frame::unpark(path);
    }
    frame::set_left(false);
    frame::set_scale(cfg->scale_15x ? frame::Scale::OneAndHalf : frame::Scale::One);
    go(Screen::Files);
}

// Shows what the scripts changed: the canvas rows, and the Companion when
// the party moved
// ---- The icon editor's big preview (Tom, 2026-10-10, v0.59.0): the NEW
// icon as large as fits - on 480x320 always, in the Companion strip; on
// 320x240 with Large Icons on (Game menu -> Options), in the empty right
// part of the editor's screen. A tap on it flips ready / action.
// An engine comfort, not the original's: CYD_BIG_ICON_PREVIEW (app/features.h)
// turns it off for ports to bigger screens.
bool big_action = false, big_was = false, big_force = false;

bool big_icon_on()
{
    return CYD_BIG_ICON_PREVIEW && play::icon_editing() && (ui::large() || cfg->large_icons);
}

ui::Rect big_icon_area()
{
    if (ui::large()) return {pic::kScreenW + 4, 4, ui::width() - pic::kScreenW - 8, pic::kScreenH - 8};
    return {140, 12, 168, 176};              // inside the editor's frame, right of the four icons
}

void draw_big_icon()
{
    uint8_t px[24 * 24];
    if (!play::icon_preview(big_action, px)) return;
    const ui::Rect a = big_icon_area();
    const int lh = ui::line_h(ui::Font::Small) + 4;
    int s = (a.w < a.h - lh ? a.w : a.h - lh) / 24;
    if (s < 1) s = 1;
    const int bx = a.x + (a.w - 24 * s) / 2, by = a.y + (a.h - lh - 24 * s) / 2;
    LGFX& g = ui::gfx();
    g.startWrite();
    g.fillRect(a.x, a.y, a.w, a.h, ui::large() ? style::kBackground : frame::colour(0));
    for (int y = 0; y < 24; ++y)
        for (int x = 0; x < 24; ++x) g.fillRect(bx + x * s, by + y * s, s, s, frame::colour(px[y * 24 + x]));
    g.endWrite();
    ui::text_center({a.x, by + 24 * s + 2, a.w, lh}, big_action ? "Action" : "Ready", style::kText, ui::Font::Small);
}

void present_play()
{
    // A fight swaps colours 0 and 8 (the field's black shows dark grey)
    static bool fight_colours = false;
    if (play::fight_colours() != fight_colours) {
        fight_colours = !fight_colours;
        if (fight_colours) {
            frame::set_palette(0, pic::kEga[8]);
            frame::set_palette(8, pic::kEga[0]);
        } else {
            frame::set_ega_palette();
        }
        frame::present();
        big_force = true;
    }
    int y0, y1;
    play::take_dirty(y0, y1);
    if (kb_shown && !ui::large() && y0 < text::kTextArea.y0 * 8) y0 = text::kTextArea.y0 * 8;   // under the keys
    if (y1 > y0) frame::present_rows(y0, y1);
    // The icon editor's big preview (after the canvas: on 320x240 it sits over it)
    const bool big = big_icon_on();
    if (big != big_was) {
        big_was = big;
        big_action = false;
        big_force = true;
        if (!big && ui::large()) play_last_map = nullptr;      // the map back in the strip
    }
    if (big && (y1 > y0 || big_force)) draw_big_icon();
    big_force = false;
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
    // 320x240: the movement keys while the party can walk, the cursor keys otherwise
    if (!ui::large()) {
        const bool w = play::walking();
        if (w != row_walking) {
            row_walking = w;
            row_flip = false;               // the game moved on: back to what it needs
        }
        const Row want = w == row_flip ? Row::Cursor : Row::Move;
        if (row != want) draw_walk_keys("Look", "Game", true);
    }
    if (!big && (play::pos_x() != play_last_x || play::pos_y() != play_last_y || play::dir() != play_last_dir ||
                 play::map() != play_last_map)) {
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
    map_shown = false;
    big_force = true;
    frame::set_ega_palette();
    play::draw(frame::canvas());
    ui::clear();
    frame::present();
    int y0, y1;
    play::take_dirty(y0, y1);
    kb_shown = false;
    draw_walk_keys("Look", "Game", true);    // Play Test: the game's own Area is on its menu line; Game = the engine's Menu
    play_last_map = nullptr;
    play_last_x = -1;
    present_play();
}

// The journal entry the game mentioned, shown on the player's next tap
bool play_journal()
{
    char jk;
    int jn;
    if (!play::journal_request(&jk, &jn)) return false;
    open_journal(jk, jn);
    return true;
}

// A tap on the game screen (or Select at the highlighted thing): what it
// acts on lights up first (Tom, 2026-10-09), then the game takes it
void play_canvas_tap(int cx, int cy)
{
    int y0, y1;
    if (play::tap_highlight(cx, cy, frame::canvas(), &y0, &y1)) {
        frame::present_rows(y0, y1);
        if (play::tap_highlight_blink(frame::canvas())) {
            delay(70);
            frame::present_rows(y0, y1);
        }
    }
    play::tap(cx, cy, frame::canvas());
    play::tap_highlight_end(frame::canvas());
    if (play::exit_requested()) {
        leave_play();
        return;
    }
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
    if (big_icon_on() && big_icon_area().contains(t.x, t.y)) {
        big_action = !big_action;              // the big preview: ready <-> action
        draw_big_icon();
        return;
    }
    if (map_shown) {
        // The next tap closes the map; one on it or on the Map key does only that
        map_shown = false;
        frame::present();
        redraw_side_keys();
        if (t.y < pic::kScreenH || walk_key(kWMap).contains(t.x, t.y)) return;
    }
    static const play::Act kActs[kWMoveKeys] = {play::Act::TurnLeft, play::Act::StepLeft, play::Act::Forward,
                                                play::Act::StepRight, play::Act::TurnRight, play::Act::TurnAround,
                                                play::Act::Area, play::Act::Look, play::Act::Forward};
    for (int k = 0; k < kWKeys; ++k) {
        const ui::Rect r = walk_key(k);
        if (r.w == 0 || !r.contains(t.x, t.y)) continue;
        if (k == kWEsc) {
            if (play::back(frame::canvas())) present_play();
            else leave_play();
            return;
        }
        if (k == kWArea) {          // the Game key: the engine's Menu
            kb_shown = false;
            open_menu();
            return;
        }
        if (k == kWMap) {                       // 320x240: the map over the game screen
            map_shown = true;
            draw_map_page();
            redraw_side_keys();
            return;
        }
        if (k == kWCursor || k == kWMove) {     // 320x240: the other set of keys, by hand
            row_flip = !row_flip;
            draw_walk_keys("Look", "Game", true);
            return;
        }
        // A journal entry the game mentioned: this tap shows it (the game still waits)
        if (play_journal()) return;
        int cx, cy;
        switch (k) {
        case kWUp:    play::nav(play::Nav::Up, frame::canvas()); break;
        case kWDown:  play::nav(play::Nav::Down, frame::canvas()); break;
        case kWLeft:  play::nav(play::Nav::Left, frame::canvas()); break;
        case kWRight: play::nav(play::Nav::Right, frame::canvas()); break;
        case kWSel:
            if (play::nav_point(&cx, &cy)) {
                play_canvas_tap(cx, cy);
                return;
            }
            break;
        default: play::act(kActs[k], frame::canvas()); break;
        }
        present_play();
        return;
    }
    int cx, cy;
    if (frame::to_canvas(t.x, t.y, cx, cy)) {
        if (play_journal()) return;
        play_canvas_tap(cx, cy);
    }
}

// ---- Settings --------------------------------------------------------------

// The keys (Tom, 2026-10-09): Brightness is a slider in one key's space;
// Swap Red/Blue shows red, green and blue blocks to check the colours by;
// Logs. More than fit go on further pages (arrows bottom right).
// Sound (Tom, 2026-10-09): the games' effects as on a Tandy 1000 (the
// default) or a PC speaker, or Off; Volume a slider like Brightness.
enum SetItem { kBright, kInvert, kSwap, kRotate, kCalibrate, kScale, kLogs, kSound, kVolume };

const SetItem kSetLarge[] = {kBright, kLogs, kSound, kVolume, kInvert, kSwap, kRotate, kCalibrate, kScale};
const SetItem kSetSmall[] = {kBright, kLogs, kSound, kVolume, kInvert, kSwap, kRotate, kCalibrate};
int set_page = 0;

int set_rows() { return ui::large() ? 4 : 3; }
int set_per_page() { return set_rows() * 2; }
int set_total() { return ui::large() ? int(sizeof kSetLarge / sizeof kSetLarge[0]) : int(sizeof kSetSmall / sizeof kSetSmall[0]); }
SetItem set_item(int i) { return ui::large() ? kSetLarge[i] : kSetSmall[i]; }
int set_pages() { return (set_total() + set_per_page() - 1) / set_per_page(); }
ui::Rect set_cell(int slot) { return ui::grid_cell(slot, 2, set_rows()); }

// The page's arrows, bottom right
ui::Rect set_arrow(int i)
{
    const int w = ui::key_h() * 5 / 4, gp = ui::gap();
    return {ui::width() - gp - (2 - i) * (w + gp) + gp, ui::height() - ui::key_h() - gp, w, ui::key_h()};
}

// The brightness slider inside its cell
ui::Rect slider_track(const ui::Rect& r)
{
    const int lh = ui::line_h(ui::Font::Small);
    const int pad = ui::gap() * 3;
    const int top = r.y + ui::gap() + lh + ui::gap();
    return {r.x + pad, top, r.w - pad * 2, r.y + r.h - ui::gap() - top};
}

void draw_slider(const ui::Rect& r, bool volume)
{
    LGFX& g = ui::gfx();
    ui::key(r, "");
    char b[32];
    const int v = volume ? cfg->volume : cfg->brightness, lo = volume ? kMinVolume : kMinBrightness;
    snprintf(b, sizeof b, "%s %d%%", volume ? "Volume" : "Brightness", v * 100 / 255);
    ui::text(r.x + ui::gap() * 2, r.y + ui::gap(), b, style::kText, ui::Font::Small);
    const ui::Rect t = slider_track(r);
    const int cy = t.y + t.h / 2, th = ui::large() ? 6 : 4;
    const int pos = t.x + (v - lo) * t.w / (255 - lo);
    g.fillRoundRect(t.x, cy - th / 2, t.w, th, th / 2, style::kKeyEdge);
    g.fillRoundRect(t.x, cy - th / 2, pos - t.x + 1, th, th / 2, style::kGold);
    int kr = t.h / 2 - 1;
    if (kr > (ui::large() ? 11 : 8)) kr = ui::large() ? 11 : 8;
    g.fillCircle(pos, cy, kr, style::kGold);
    g.drawCircle(pos, cy, kr, style::kText);
}

// Brightness (or the volume) from a point on the slider
bool slider_set(const ui::Rect& r, int x, bool volume)
{
    const ui::Rect t = slider_track(r);
    const int lo = volume ? kMinVolume : kMinBrightness;
    int v = lo + (x - t.x) * (255 - lo) / (t.w > 0 ? t.w : 1);
    if (v < lo) v = lo;
    if (v > 255) v = 255;
    uint8_t& cur = volume ? cfg->volume : cfg->brightness;
    if (v == cur) return false;
    cur = static_cast<uint8_t>(v);
    if (volume) play::set_sound(cfg->sound, cfg->volume);
    else ui::gfx().setBrightness(cfg->brightness);
    return true;
}

const char* sound_label()
{
    switch (cfg->sound) {
    case kSoundPc: return "Sound: PC Speaker";
    case kSoundOff: return "Sound: Off";
    default: return "Sound: Tandy";
    }
}

void draw_swap(const ui::Rect& r, bool lit)
{
    LGFX& g = ui::gfx();
    ui::key(r, "", lit ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    const int lh = ui::line_h(ui::Font::Normal);
    ui::text_center({r.x, r.y + ui::gap(), r.w, lh}, "Swap Red/Blue");
    const int gp = ui::gap();
    const int top = r.y + gp + lh + gp / 2, h = r.y + r.h - gp - top;
    const int w = (r.w - gp * 4) / 3;
    static const uint16_t kCol[3] = {0xF800, 0x07E0, 0x001F};      // pure red, green, blue
    static const char* const kName[3] = {"Red", "Green", "Blue"};
    for (int i = 0; i < 3; ++i) {
        const ui::Rect b{r.x + gp + i * (w + gp), top, w, h};
        g.fillRect(b.x, b.y, b.w, b.h, kCol[i]);
        ui::text_center(b, kName[i], i == 1 ? 0x0000 : 0xFFFF, ui::Font::Small);
    }
}

void draw_set_item(int slot, SetItem it)
{
    const ui::Rect r = set_cell(slot);
    const PanelPrefs& pp = panel_prefs_get();
    auto lit = [](bool on) { return on ? ui::KeyStyle::Lit : ui::KeyStyle::Normal; };
    switch (it) {
    case kBright:    draw_slider(r, false); break;
    case kInvert:    ui::key(r, "Invert Colors", lit(pp.invert)); break;
    case kSwap:      draw_swap(r, pp.swap_rb); break;
    case kRotate:    ui::key(r, "Rotate 180", lit(cfg->flipped)); break;
    case kCalibrate: ui::key(r, "Recalibrate Touch"); break;
    case kScale:     ui::key(r, cfg->scale_15x ? "Asset Viewer: 1.5x" : "Asset Viewer: 1:1", lit(cfg->scale_15x)); break;
    case kLogs:      ui::key(r, "Logs"); break;
    case kSound:     ui::key(r, sound_label(), lit(cfg->sound != kSoundOff)); break;
    case kVolume:    draw_slider(r, true); break;
    }
}

void draw_settings()
{
    ui::clear();
    ui::header("Settings", true);
    if (set_page >= set_pages()) set_page = set_pages() - 1;
    const int first = set_page * set_per_page();
    for (int i = 0; i < set_per_page() && first + i < set_total(); ++i) draw_set_item(i, set_item(first + i));
    const bool paged = set_pages() > 1;
    if (paged) {
        ui::key_arrow(set_arrow(0), ui::Arrow::Left, set_page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
        ui::key_arrow(set_arrow(1), ui::Arrow::Right, set_page < set_pages() - 1 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    }

    // Version, board and memory in the bottom row (left of the arrows)
    char l[3][64];
    const unsigned fr = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024);
    const unsigned lb = (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024);
    // PSRAM (extra RAM chip): none on the five boards (their GPIO 16 / 17
    // drive the RGB LED - the original ESP32's PSRAM pins)
    char ps[24];
    if (ESP.getPsramSize()) snprintf(ps, sizeof ps, "PSRAM %u KB", (unsigned)(ESP.getPsramSize() / 1024));
    else snprintf(ps, sizeof ps, "no PSRAM");
    int n;
    if (paged) {
        snprintf(l[0], sizeof l[0], "%s (%s)", env_.version, env_.build);
        snprintf(l[1], sizeof l[1], "%s", BOARD_NAME);
        snprintf(l[2], sizeof l[2], "Free %u KB, largest %u KB, %s", fr, lb, ps);
        n = 3;
    } else {
        snprintf(l[0], sizeof l[0], "%s (%s)  %s", env_.version, env_.build, BOARD_NAME);
        snprintf(l[1], sizeof l[1], "Free memory %u KB, largest block %u KB, %s", fr, lb, ps);
        n = 2;
    }
    const int lh = ui::line_h(ui::Font::Small) + 2;
    int y = ui::height() - ui::gap() - ui::key_h() / 2 - lh * n / 2;
    for (int i = 0; i < n; ++i, y += lh) ui::text(ui::gap() * 2, y, l[i], style::kTextMuted, ui::Font::Small);
}

int item_slot(SetItem it)  // the item's place on this page, -1 when not on it
{
    const int first = set_page * set_per_page();
    for (int i = 0; i < set_per_page() && first + i < set_total(); ++i)
        if (set_item(first + i) == it) return i;
    return -1;
}

bool slider_dragged = false;
int  slider_grabbed = -1;               // 0 brightness, 1 volume: the one the drag began on

// Dragging a slider: the brightness / volume follows; saved when the stylus lifts
void settings_tick()
{
    int dx, dy, x, y;
    const bool dragging = ui::drag(dx, dy);
    if (dragging && ui::touch_point(x, y)) {
        for (int k = 0; k < 2; ++k) {
            const int slot = item_slot(k ? kVolume : kBright);
            if (slot < 0 || (slider_grabbed >= 0 && slider_grabbed != k)) continue;
            const ui::Rect r = set_cell(slot);
            if (slider_grabbed < 0 && (x < r.x || x >= r.x + r.w || y < r.y || y >= r.y + r.h)) continue;
            slider_grabbed = k;
            slider_dragged = true;
            if (slider_set(r, x, k == 1)) draw_slider(r, k == 1);
        }
    } else if (!dragging && slider_dragged) {
        slider_dragged = false;
        slider_grabbed = -1;
        settings_save(*cfg);
    }
}

void tap_settings(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) { go(Screen::Home); return; }
    if (set_pages() > 1) {
        if (set_arrow(0).contains(t.x, t.y) && set_page > 0) { --set_page; dirty = true; return; }
        if (set_arrow(1).contains(t.x, t.y) && set_page < set_pages() - 1) { ++set_page; dirty = true; return; }
    }
    LGFX& g = ui::gfx();
    const int first = set_page * set_per_page();
    for (int i = 0; i < set_per_page() && first + i < set_total(); ++i) {
        const ui::Rect r = set_cell(i);
        if (!r.contains(t.x, t.y)) continue;
        PanelPrefs pp = panel_prefs_get();
        switch (set_item(first + i)) {
        case kBright:
            if (slider_set(r, t.x, false)) {
                draw_slider(r, false);
                settings_save(*cfg);
            }
            return;
        case kLogs:
            logui::open(false);
            go(Screen::Logs);
            return;
        case kVolume:
            if (slider_set(r, t.x, true)) {
                draw_slider(r, true);
                settings_save(*cfg);
            }
            return;
        case kSound:
            cfg->sound = cfg->sound == kSoundTandy ? kSoundPc : cfg->sound == kSoundPc ? kSoundOff : kSoundTandy;
            play::set_sound(cfg->sound, cfg->volume);
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
    // A restart that wasn't a power-on or reset: noted on the card
    // (_CYD/RESTART.TXT) so the cause can be found later
    const esp_reset_reason_t why = esp_reset_reason();
    const char* why_text = why == ESP_RST_PANIC     ? "a crash (panic)"
                         : why == ESP_RST_INT_WDT   ? "the interrupt watchdog"
                         : why == ESP_RST_TASK_WDT  ? "the task watchdog"
                         : why == ESP_RST_WDT       ? "a watchdog"
                         : why == ESP_RST_BROWNOUT  ? "a brownout (the power supply dipped)"
                                                    : nullptr;
    if (why_text) Serial.printf("[boot] the last restart was %s\n", why_text);
    if (sd_begin() && why_text && sd_fs().exists(games::kRootDir)) {
        char path[96];
        library::cache_path("RESTART.TXT", path, sizeof path);
        fs::File rf = sd_fs().open(path, "a");
        if (rf) {
            rf.printf("%s (%s): the board restarted after %s\n", env.version, env.build, why_text);
            rf.close();
        }
    }
    if (sd_begin() && library::load_library(game_dirs, library::kMaxGames, &n_games)) {
        scan_result = library::ScanResult::Ok;
        Serial.printf("[library] loaded: %d game folder(s)\n", n_games);
        go(Screen::Home);
    } else {
        rescan();            // ends at its log (Continue -> the library)
    }
}

void tick()
{
    ui::Tap t;
    if (ui::poll_tap(t)) {
        ui::tap_flash(t);            // the tapped key lights up first (Tom, 2026-10-09)
        switch (screen) {
        case Screen::Home:     tap_home(t); break;
        case Screen::Files:    tap_files(t); break;
        case Screen::Resources: tap_resources(t); break;
        case Screen::Blocks:   tap_blocks(t); break;
        case Screen::View:     tap_view(t); break;
        case Screen::Look:     tap_look(t); break;
        case Screen::Walk:     tap_walk(t); break;
        case Screen::Play:     tap_play(t); break;
        case Screen::Journal:  tap_journal(t); break;
        case Screen::Pdf:      tap_pdf(t); break;
        case Screen::GameMenu: tap_game_menu(t); break;
        case Screen::Settings: tap_settings(t); break;
        case Screen::Logs:
            if (!logui::tap(t)) go(logui::from_scan() ? Screen::Home : Screen::Settings);
            break;
        }
        ui::tap_unflash();           // back as it was, unless the screen changed
    }
    if (screen == Screen::Logs && !dirty) logui::tick();
    if (screen == Screen::Settings && !dirty) settings_tick();
    if (screen == Screen::Journal && !dirty) tick_journal();
    if (screen == Screen::Pdf && !dirty) tick_pdf();
    if (screen == Screen::Look && !look_error && !dirty) present(look::tick(millis(), frame::canvas()));
    if (screen == Screen::Play && !play_error && !dirty && !map_shown) {
        play::tick(millis(), frame::canvas());
        present_play();
    }
    if (!dirty) return;
    dirty = false;
    switch (screen) {
    case Screen::Home:     draw_home(); break;
    case Screen::Files:    draw_files(); break;
    case Screen::Resources: draw_resources(); break;
    case Screen::Blocks:   draw_blocks(); break;
    case Screen::View:     draw_view(); break;
    case Screen::Look:     draw_look(); break;
    case Screen::Walk:     draw_walk(); break;
    case Screen::Play:     draw_play(); break;
    case Screen::Journal:  draw_journal(); break;
    case Screen::Pdf:      draw_pdf(); break;
    case Screen::GameMenu: draw_game_menu(); break;
    case Screen::Settings: draw_settings(); break;
    case Screen::Logs:      logui::draw(); break;
    }
}

} // namespace viewer

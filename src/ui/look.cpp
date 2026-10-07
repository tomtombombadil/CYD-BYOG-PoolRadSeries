#include "look.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <new>

#include "app/library.h"
#include "engine/exepack.h"
#include "engine/font.h"
#include "engine/layout.h"
#include "engine/printcalls.h"
#include "engine/profile.h"
#include "engine/text.h"
#include "hal/sdcard.h"

namespace look {

namespace {

constexpr int kMaxCredits = 48;

struct Data {
    layout::Tiles     tiles;
    layout::Tables    tables;
    font::Font        font;
    printcalls::Line  credits[kMaxCredits];
    int               n_credits = 0;
    char              press_key[text::kMaxString] = {};
    char              data_dir[96] = {};
    const profile::Profile* prof = nullptr;
};

Data* d = nullptr;
char  msg[160];
char  src_line[64];
dax::Index idx;          // 2.3 KB: one, reused for each file opened here

enum Page { kTitle, kText, kTextScreen, kOuter, kCombat, kTiles, kPages };
int page = kTitle;

constexpr Rows kAll{0, pic::kScreenH};

// The demo text for the text window (the engine's own words)
const char* const kDemo =
    "This is the game's text window, drawn with the game's own font. Text prints one letter at a time and "
    "wraps whole words at the edge of the area - punctuation stays with its word: \"Well met, adventurer!\" "
    "When the area is full, the game waits until you tap the screen, clears the area and goes on from the "
    "top. When the text is done, the menu line at the bottom shows what you can do next: tap a word to "
    "choose it.";

// ---- helpers ---------------------------------------------------------------

bool open_file(const char* dir, const char* name, fs::File& f)
{
    char path[160];
    library::path_of(dir, name, path, sizeof path);
    f = sd_fs().open(path, "r");
    return static_cast<bool>(f);
}

void put_text(pic::Canvas& c, const char* s, int col, int row, uint8_t fg = 15)
{
    font::draw_text(c, d->font, s, col, row, fg, -1);
}

Rows join(Rows a, Rows b)
{
    if (a.y1 == 0) return b;
    if (b.y1 == 0) return a;
    return {a.y0 < b.y0 ? a.y0 : b.y0, a.y1 > b.y1 ? a.y1 : b.y1};
}

Rows cell_rows(int row0, int row1) { return {row0 * 8, (row1 + 1) * 8}; }

// ---- Title sequence ---------------------------------------------------------

int      t_step = 0;
uint32_t t_since = 0;
bool     t_done = false;
bool     t_timed = false;    // t_since set

bool draw_title_picture(pic::Canvas& c, int block, int row, int col)
{
    fs::File f;
    if (!open_file(d->data_dir, d->prof->title_file, f)) return false;
    library::FileSource src(f);
    bool ok = false;
    if (dax::read_index(src, idx) == dax::Status::Ok) {
        const dax::Entry* e = idx.find(static_cast<uint8_t>(block));
        if (e) {
            dax::RleReader r(src, idx, *e);
            uint8_t hdr[pic::kHeaderSize];
            pic::Header h;
            ok = r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, h) &&
                 pic::draw(r, h, 0, c, col * 8, row * 8);
        }
    }
    f.close();
    return ok;
}

void draw_credits(pic::Canvas& c)
{
    layout::outer(c, d->tables, d->tiles);
    for (uint8_t bar : d->prof->credits_bars) layout::bar(c, d->tables, d->tiles, bar);
    for (int i = 0; i < d->n_credits; ++i) {
        const printcalls::Line& l = d->credits[i];
        font::draw_text(c, d->font, l.s, l.col, l.row, l.fg, l.bg);
    }
}

// Draws steps from `from` on until one that waits; that one is current
void title_show(int from, pic::Canvas& c)
{
    const profile::Profile& p = *d->prof;
    t_done = false;
    for (int s = from; s < p.title_steps; ++s) {
        const profile::TitleStep& st = p.title[s];
        if (st.block == 0 && d->n_credits == 0) continue;     // no GAME.OVR: no credits
        if (st.clear) c.clear(0);
        if (st.block == 0) draw_credits(c);
        else draw_title_picture(c, st.block, st.row, st.col);
        t_step = s;
        if (st.wait_ms > 0) break;
    }
    t_timed = false;
}

void title_next(pic::Canvas& c)
{
    if (t_step + 1 < d->prof->title_steps) title_show(t_step + 1, c);
    else t_done = true;     // the last picture stays; a tap starts again
}

// ---- Text window -------------------------------------------------------------

text::Writer w;
text::Menu   menu;
int          menu_sel = 2;     // the chosen word stays chosen, as in the games
char         pending = 0;      // a chosen word's key, acted on once its highlight was seen
uint32_t     pending_at = 0;
int          char_ms = 12;     // the games' usual speed
uint32_t     t_last = 0;
bool         t_started = false;

constexpr uint32_t kChoiceShowMs = 250;

void text_start(pic::Canvas& c)
{
    text::begin(w, c, kDemo, text::kTextArea, 10, true);
    c.fill(0, text::kMenuRow * 8, pic::kScreenW, 8, 0);
    t_started = false;
}

// After printing: the prompt or the menu
Rows text_after(pic::Canvas& c)
{
    if (w.state == text::State::PageFull) {
        c.fill(0, text::kMenuRow * 8, pic::kScreenW, 8, 0);
        put_text(c, d->press_key, 0, text::kMenuRow, 13);
        return cell_rows(text::kMenuRow, text::kMenuRow);
    }
    if (w.state == text::State::Done) {
        text::build(menu, "", "Again Quick Normal");
        menu.selected = menu_sel;
        text::draw(c, d->font, menu);
        return cell_rows(text::kMenuRow, text::kMenuRow);
    }
    return Rows{};
}

void draw_text_page(pic::Canvas& c)
{
    pending = 0;
    c.clear(0);
    layout::explore(c, d->tables, d->tiles);
    put_text(c, "3D VIEW", 5, 8, 7);
    put_text(c, "PARTY", 18, 2);
    text_start(c);
}

// ---- Still pages ------------------------------------------------------------

void draw_still(pic::Canvas& c)
{
    c.clear(0);
    const layout::Tables& tb = d->tables;
    const layout::Tiles& t = d->tiles;
    switch (page) {
    case kTextScreen:
        layout::outer(c, tb, t);
        layout::bar(c, tb, t, 16);
        put_text(c, "MAP / PICTURE AREA", 2, 7, 7);
        put_text(c, "TEXT AREA", 2, 18);
        put_text(c, "MENU LINE", 0, 24, 14);
        break;
    case kOuter:
        layout::outer(c, tb, t);
        put_text(c, "THE QUICK BROWN FOX JUMPS", 2, 3);
        put_text(c, "OVER THE LAZY DOG.", 2, 4);
        put_text(c, "0123456789 !?:,'\"-", 2, 6, 11);
        for (int g = 64; g < font::kGlyphs; ++g) {
            const int i = g - 64;
            font::draw_glyph(c, d->font, g, (2 + i % 36) * 8, (9 + i / 36) * 8, 14, -1);
        }
        put_text(c, "SYMBOLS 64-176 ABOVE", 2, 14, 7);
        put_text(c, "MENU LINE", 0, 24, 14);
        break;
    case kCombat:
        layout::combat(c, tb, t);
        put_text(c, "BATTLEFIELD", 6, 10, 7);
        put_text(c, "COMBAT", 26, 2);
        // The combat frame ends a row higher: row 23 is its status line
        // (range, the spell or item being used), row 24 the menu as always
        put_text(c, "STATUS LINE", 0, 23, 10);
        put_text(c, "MENU LINE", 0, 24, 14);
        break;
    case kTiles:
        c.clear(1);
        for (int n = 0; n < layout::kTiles; ++n) {
            const int col = 2 + (n % 10) * 3, row = 2 + (n / 10) * 4;
            layout::tile(c, t, n, col, row);
            char num[4];
            snprintf(num, sizeof num, "%d", n);
            put_text(c, num, col, row + 1, 7);
        }
        put_text(c, "TILES 0-39 (8X8D1 #202)", 2, 20);
        put_text(c, "FRAME = 30-39, 3D VIEW = 20-29", 2, 21, 7);
        break;
    }
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
    strncpy(d->data_dir, data_dir, sizeof d->data_dir - 1);

    // The frame layout and strings, from the game's own program
    fs::File f;
    if (!open_file(data_dir, prog, f)) {
        close();
        snprintf(msg, sizeof msg, "%s is missing from the game folder.", prog);
        return msg;
    }
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
    d->prof = p;
    const uint32_t t0 = millis();
    const layout::Status ls = layout::load_tables(fsrc, info, *p, d->tables);
    if (!text::read_pascal(fsrc, info, p->press_any_key, d->press_key, sizeof d->press_key))
        strcpy(d->press_key, "Tap to go on");
    Serial.printf("[look] %s: frame tables %s, prompt \"%s\" (%lu ms)\n", prog, layout::status_text(ls), d->press_key,
                  (unsigned long)(millis() - t0));
    f.close();
    if (ls != layout::Status::Ok) {
        close();
        snprintf(msg, sizeof msg, "%s: %s.", prog, layout::status_text(ls));
        return msg;
    }
    snprintf(src_line, sizeof src_line, "%s, %s release", prog, p->release);

    // Tiles and font
    if (!open_file(data_dir, p->tiles_file, f)) {
        close();
        snprintf(msg, sizeof msg, "%s is missing from the game folder.", p->tiles_file);
        return msg;
    }
    {
        library::FileSource tsrc(f);
        const bool ok = dax::read_index(tsrc, idx) == dax::Status::Ok &&
                        layout::load_tiles(tsrc, idx, p->tiles_block, d->tiles) && font::load(tsrc, idx, d->font);
        f.close();
        if (!ok) {
            close();
            snprintf(msg, sizeof msg, "%s: the frame tiles or font didn't load.", p->tiles_file);
            return msg;
        }
    }

    // The credits, from GAME.OVR (the title sequence skips them without it)
    if (open_file(data_dir, p->overlay, f)) {
        library::FileSource osrc(f);
        if (osrc.size() == p->overlay_size)
            d->n_credits = printcalls::read(osrc, p->credits_at, p->credits_base, d->credits, kMaxCredits);
        f.close();
    }
    Serial.printf("[look] %s: %d credit lines\n", p->overlay, d->n_credits);
    return nullptr;
}

void close()
{
    delete d;
    d = nullptr;
}

int pages() { return kPages; }

const char* page_name(int pg)
{
    switch (pg) {
    case kTitle:      return "Title sequence (tap = next)";
    case kText:       return "Text window and menu line";
    case kTextScreen: return "Text screen";
    case kOuter:      return "Outer frame and font";
    case kCombat:     return "Combat";
    case kTiles:      return "Frame tiles";
    }
    return "";
}

const char* source() { return src_line; }

void enter(int pg, pic::Canvas& c)
{
    page = pg;
    c.clear(0);
    if (!d) return;
    switch (page) {
    case kTitle: title_show(0, c); break;
    case kText:  draw_text_page(c); break;
    default:     draw_still(c); break;
    }
}

Rows tick(uint32_t now, pic::Canvas& c)
{
    if (!d) return Rows{};
    if (page == kTitle) {
        if (t_done) return Rows{};
        if (!t_timed) {
            t_since = now;
            t_timed = true;
            return Rows{};
        }
        if (now - t_since >= d->prof->title[t_step].wait_ms) {
            title_next(c);
            return t_done ? Rows{} : kAll;
        }
        return Rows{};
    }
    if (page == kText && pending && now - pending_at >= kChoiceShowMs) {
        if (pending == 'Q') char_ms = 0;
        if (pending == 'N') char_ms = 12;
        pending = 0;
        text_start(c);
        return join(cell_rows(w.r.y0, w.r.y1), cell_rows(text::kMenuRow, text::kMenuRow));
    }
    if (page == kText && w.state == text::State::Writing) {
        if (!t_started) {
            t_last = now;
            t_started = true;
        }
        int n = -1;
        if (char_ms > 0) {
            n = static_cast<int>((now - t_last) / char_ms);
            if (n == 0) return Rows{};
            t_last += static_cast<uint32_t>(n) * char_ms;
        }
        text::step(w, c, d->font, n);
        return join(cell_rows(w.r.y0, w.r.y1), text_after(c));
    }
    return Rows{};
}

Rows tap(int x, int y, uint32_t now, pic::Canvas& c)
{
    if (!d) return Rows{};
    if (page == kTitle) {
        if (t_done) title_show(0, c);
        else title_next(c);
        return kAll;
    }
    if (page != kText) return Rows{};
    switch (w.state) {
    case text::State::PageFull: {
        c.fill(0, text::kMenuRow * 8, pic::kScreenW, 8, 0);
        text::next_page(w, c);
        t_last = now;
        return join(cell_rows(w.r.y0, w.r.y1), cell_rows(text::kMenuRow, text::kMenuRow));
    }
    case text::State::Writing:
        // A tap fills the rest of the page at once
        text::step(w, c, d->font, -1);
        t_last = now;
        return join(cell_rows(w.r.y0, w.r.y1), text_after(c));
    case text::State::Done: {
        if (pending || y / 8 != text::kMenuRow) return Rows{};
        const int item = text::hit(menu, x / 8);
        if (item < 0) return Rows{};
        // The highlight moves to the tapped word first, then it acts
        menu_sel = menu.selected = item;
        text::draw(c, d->font, menu);
        pending = text::key(menu, item);
        pending_at = now;
        return cell_rows(text::kMenuRow, text::kMenuRow);
    }
    default:
        return Rows{};
    }
}

} // namespace look

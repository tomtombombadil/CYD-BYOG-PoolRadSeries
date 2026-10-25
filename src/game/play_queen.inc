// The Dark Queen of Krynn in the Play Test (being built; queen_facts.md,
// queen_facts_2.md): its files are HLIB libraries in the game folder's
// DISK1-3 (variant.hlib) - ECL.GLB's scripts and GEO.GLB's maps by id, its
// screen from FRAME / GEN.TLB, the 3D view from 8X8DB / 8X8DC / BACK, the
// area map from TOPVIEW (engine/qview.*), its text in GAME.FON's letters,
// its saved games SAVGAMx.QSV with the characters inside.
//
// Included by play.cpp inside its anonymous namespace (as play_fight.inc),
// not compiled on its own.

bool hl() { return d && d->prof && d->prof->variant.hlib; }
bool q_pm_up = false;                   // the party menu's buttons are on the screen

// A file of an HLIB game: in DISK1-3 of the game folder (or the folder itself)
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

// The 16-colour table the screen's parts are drawn through (hlpic::Colours,
// 256 bytes: made at the start - q_open)
hlpic::Colours& q_col() { return *d->hl_col; }

// The boxes' paper (the frame's colour 23 - qview::draw_frame): fills with
// colour 0 on the exploring screen give it (pic::Canvas::paper)
void q_paper(pic::Canvas& c, bool on)
{
    c.paper = on ? static_cast<int16_t>(c.vga && c.bpp == 8 ? 23 : q_col().ega[23]) : int16_t{-1};
}

// The start: the colour table, the letters (GAME.FON in DISK1). False: no
// memory or no font (msg says)
__attribute__((noinline)) bool q_open()
{
    d->hl_col = new (std::nothrow) hlpic::Colours;
    if (!d->hl_col) {
        no_memory("the colour table");
        return false;
    }
    d->hl_col->reset();
    plat::File f;
    bool ok = false;
    if (open_hl("GAME.FON", f)) {
        plat::FileSource src(f);
        ok = font::load_fon(src, d->font);
        f.close();
    }
    if (!ok) {
        snprintf(msg, sizeof msg, "GAME.FON (the letters) is missing from the game's DISK1 folder.");
        return false;
    }
    font::set_letters(d->font, font_wanted(), d->alt_font);
    return true;
}

// The moons' phases (0-3) as the scripts keep them (Champions' words)
void q_moons(int out[3])
{
    for (int i = 0; i < 3; ++i) out[i] = vm ? vm->get(static_cast<uint16_t>(0x4CF9 + i)) & 3 : 0;
}

// The party menu's box (queen_facts_3.md 1): 8-311 x 8-109 (the version
// screen's to row 183), sunk into the marble - black above and left, light
// (the frame's 17) below and right
void q_pm_box(pic::Canvas& c, int last = 109)
{
    c.fill(8, 8, 304, last - 7, 0);
    const uint8_t light = c.vga && c.bpp == 8 ? 17 : q_col().ega[17];
    for (int x = 7; x <= 312; ++x) {
        c.set(x, 7, 0);
        c.set(x, last + 1, light);
    }
    for (int y = 7; y <= last + 1; ++y) {
        c.set(7, y, 0);
        c.set(312, y, light);
    }
}

// The screen: the exploring frame (the view's frame, the compass, the
// moons) or, for the party menu, the marble with one box
__attribute__((noinline)) void q_draw_frame(pic::Canvas& c, bool explore)
{
    q_pm_up = false;
    c.paper = -1;
    c.clear(0);
    plat::File fa, fb;
    const bool a = open_hl("FRAME.TLB", fa), b = open_hl("GEN.TLB", fb);
    {
        plat::FileSource sa(fa), sb(fb);
        int moons[3];
        q_moons(moons);
        qview::draw_frame(c, a ? &sa : nullptr, b ? &sb : nullptr, explore ? d->gs.dir : -1, explore ? moons : nullptr,
                          q_col());
    }
    if (a) fa.close();
    if (b) fb.close();
    q_paper(c, true);
    if (!explore) q_pm_box(c);
    dirty(0, pic::kScreenH);
}

// The compass after a turn
__attribute__((noinline)) void q_compass(pic::Canvas& c)
{
    plat::File f;
    if (!open_hl("FRAME.TLB", f)) return;
    {
        plat::FileSource src(f);
        const int16_t paper = c.paper;
        c.paper = -1;
        qview::draw_compass(c, src, d->gs.dir, q_col());
        c.paper = paper;
    }
    f.close();
}

// The 3D view or the area map, from the map and the look loaded
__attribute__((noinline)) void q_draw_view(pic::Canvas& c, bool area)
{
    plat::File fa, fb, fc;
    if (area) {
        if (open_hl("TOPVIEW.TLB", fa)) {
            plat::FileSource sa(fa);
            qview::Libs libs;
            libs.topview = &sa;
            const int16_t paper = c.paper;
            c.paper = -1;                       // (the area map's black)
            qview::draw_area_map(c, libs, d->map, d->gs.x, d->gs.y, d->gs.dir, q_col());
            c.paper = paper;
            fa.close();
        }
        return;
    }
    // (three files at once: the card's slots)
    const bool a = open_hl("8X8DB.TLB", fa), b = open_hl("8X8DC.TLB", fb), k = open_hl("BACK.TLB", fc);
    {
        plat::FileSource sa(fa), sb(fb), sc(fc);
        qview::Libs libs;
        libs.walls[0] = a ? &sa : nullptr;
        libs.walls[1] = b ? &sb : nullptr;
        libs.back = k ? &sc : nullptr;
        qview::draw(c, libs, d->map, d->gs.x, d->gs.y, d->gs.dir, qview::look_of(d->map, d->save.look), q_col());
    }
    if (a) fa.close();
    if (b) fb.close();
    if (k) fc.close();
}

// ECL.GLB's script `id` (stored as it is: the code from its first byte)
__attribute__((noinline)) bool q_load_script(int id, uint8_t* code, uint32_t* len)
{
    plat::File f;
    if (!open_hl("ECL.GLB", f)) return false;
    bool ok = false;
    {
        plat::FileSource src(f);
        uint32_t pos, n;
        if (id >= 0 && hlib::find_entry(src, static_cast<uint16_t>(id), pos, n) && n <= d->gs.code_cap) {
            hlib::EntryReader r(src, pos, n);
            ok = r.read(code, n) == n;
            if (ok) *len = n;
        }
    }
    f.close();
    plat::log("[play] script %d of ECL.GLB: %s\n", id, ok ? "loaded" : "missing");
    return ok;
}

// GEO.GLB's map `id`
__attribute__((noinline)) void q_load_map(int id)
{
    plat::File f;
    geo::release(d->map);
    if (open_hl("GEO.GLB", f)) {
        plat::FileSource src(f);
        uint32_t pos, n;
        if (hlib::find_entry(src, static_cast<uint16_t>(id), pos, n)) geo::load_hlib(src, pos, n, d->map);
        f.close();
    }
    plat::log("[play] map %d of GEO.GLB: %s (%dx%d)\n", id, d->map.loaded ? "ok" : "missing", d->map.w, d->map.h);
}

// ---- Event pictures' animation (queen_facts_3.md 3) ---------------------
// A picture of more than one part animates: frame 0 its first part (88 x
// 88), frame k the first with part k + 1 over it - a sprite (0x17; a
// one-row empty one leaves the first part alone) or a list of parts to draw
// (kind 0x19: 6 bytes each - the part, then its place); the frames in
// turn, 0.3 s each (the original's timing not read yet). The library stays
// open (anim_file) while it animates.
bool animation_on();
const char* const kQPicFiles[] = {"PICA.TLB", "PICB.TLB", "PICC.TLB"};

bool q_anim_part(plat::FileSource& src, const hlib::Library& lib, int part, int depth)
{
    uint32_t pos, len;
    hlib::Part p;
    if (!hlib::entry(src, lib, part, pos, len) || !hlib::read_part(src, pos, len, p)) return false;
    if (p.kind == hlib::kShapes && depth == 0) {
        // (a list: part, 0?, then its place - each drawn in turn)
        for (uint32_t at = pos + hlib::kPartHeader; at + 6 <= pos + len; at += 6) {
            uint8_t r[6];
            if (src.read_at(at, r, 6) != 6 || r[0] == 0) break;
            q_anim_part(src, lib, r[0], depth + 1);
        }
        return true;
    }
    if (p.height <= 1) return true;                 // (an empty frame)
    return hlpic::draw(src, p, q_col(), *cv, hlpic::place_x(p, 24), hlpic::place_y(p, 24),
                       hlpic::Clip{24, 24, 24 + 88, 24 + 88});
}

__attribute__((noinline)) bool q_anim_draw(int frame)
{
    plat::FileSource src(d->anim_file);
    hlib::Library lib;
    if (hlib::open(src, d->q_anim_pos, d->q_anim_len, lib) != hlib::Status::Ok) return false;
    const int16_t paper = cv->paper;
    cv->paper = -1;
    const bool ok = hlpic::draw_picture_part(src, d->q_anim_pos, d->q_anim_len, 1, q_col(), nullptr, *cv, 24, 24) &&
                    (frame <= 0 || q_anim_part(src, lib, frame + 1, 0));
    cv->paper = paper;
    dirty_rows(3, 13);
    return ok;
}

// Starts picture id's animation (false: not one that animates, or no file
// - q_picture shows it)
__attribute__((noinline)) bool q_anim_start(int id)
{
    if (!animation_on()) return false;
    for (const char* name : kQPicFiles) {
        if (!open_hl(name, d->anim_file)) continue;
        plat::FileSource src(d->anim_file);
        uint32_t pos, len;
        hlib::Library lib;
        if (hlib::find_entry(src, static_cast<uint16_t>(id), pos, len) &&
            hlib::open(src, pos, len, lib) == hlib::Status::Ok) {
            if (lib.count <= 2) break;              // (one picture: no animation)
            d->q_anim_pos = pos;
            d->q_anim_len = len;
            d->anim.frames = lib.count - 1 < pic::kMaxAnimFrames ? lib.count - 1 : pic::kMaxAnimFrames;
            for (int k = 0; k < d->anim.frames; ++k) d->anim.delay[k] = 3;
            anim_block = id;
            anim_frame = 0;
            anim_at = plat::now_ms();
            last_pic = id;
            if (q_anim_draw(0)) return true;
            anim_block = -1;
            break;
        }
        d->anim_file.close();
    }
    if (d->anim_file) d->anim_file.close();
    return false;
}

// An event picture (PICTURE id): PICA / PICB / PICC.TLB's picture of that id
// (ids are each file's own - 5-116, 128-150, 151-239), its first part (88 x
// 88, run-length) in the view's box; a big one (240 up: BIGPIC.TLB, 304 x
// 120) at the screen's top left inside the frame. Its other parts are its
// animation's frames (sprites over the first) - not shown yet.
__attribute__((noinline)) bool q_picture(pic::Canvas& c, int id, bool big)
{
    static const char* const kFiles[] = {"PICA.TLB", "PICB.TLB", "PICC.TLB"};
    const char* const* files = kFiles;
    int n = 3;
    static const char* const kBig[] = {"BIGPIC.TLB"};
    if (big) files = kBig, n = 1;
    bool ok = false;
    for (int k = 0; k < n && !ok; ++k) {
        plat::File f;
        if (!open_hl(files[k], f)) continue;
        {
            plat::FileSource src(f);
            uint32_t pos, len;
            if (hlib::find_entry(src, static_cast<uint16_t>(id), pos, len)) {
                const int x = big ? 8 : 24, y = big ? 8 : 24;
                ok = hlpic::draw_picture_part(src, pos, len, 1, q_col(), nullptr, c, x, y);
            }
        }
        f.close();
    }
    plat::log("[play] picture %d%s: %s\n", id, big ? " (big)" : "", ok ? "shown" : "missing");
    return ok;
}

// ---- Buttons (queen_facts_3.md 1) -------------------------------------------
// The original's menus are buttons on the marble bar at the screen's bottom
// (FRAME.TLB's part 4, 320 x 16 at row 184): a word a raised button - light
// top / left (the frame's colour 19), black bottom / right, filled with its
// boxes' colour (23); the key white, the rest grey (EGA 7) - the prompt in
// a light grey black-edged box of its own with black letters, the line from
// column 1, the letters at y 188.

uint8_t q_entry(const pic::Canvas& c, int e) { return c.vga && c.bpp == 8 ? static_cast<uint8_t>(e) : q_col().ega[e]; }
pic::Canvas* q_canvas = nullptr;            // (the screen's: q_buttons' colours)

text::MenuColors q_buttons()
{
    static pic::Canvas none{nullptr, 0, 0, 4};
    const pic::Canvas& c = q_canvas ? *q_canvas : none;
    text::MenuColors mc;
    mc.buttons = true;
    mc.normal = 7;
    mc.highlight = 15;
    mc.b_fill = q_entry(c, 23);
    mc.b_light = q_entry(c, 19);
    mc.b_box = 7;
    return mc;
}

// The marble bar under the menu (its row 184-199)
__attribute__((noinline)) void q_bar(pic::Canvas& c)
{
    q_canvas = &c;
    plat::File f;
    if (!open_hl("FRAME.TLB", f)) {
        c.fill_black(0, 184, pic::kScreenW, 16);
        return;
    }
    {
        plat::FileSource src(f);
        hlib::Library lib;
        uint32_t pos, len;
        hlib::Part p;
        const int16_t paper = c.paper;
        c.paper = -1;
        if (hlib::open(src, 0, src.size(), lib) == hlib::Status::Ok && hlib::entry(src, lib, 4, pos, len) &&
            hlib::read_part(src, pos, len, p))
            hlpic::draw(src, p, q_col(), c, hlpic::place_x(p, 0), hlpic::place_y(p, 0));
        c.paper = paper;
    }
    f.close();
}

int q_pm_button_at(int x, int y);
void q_pm_button_box(int i, int* x0, int* y0, int* x1, int* y1);
int q_pm_sel_button();

// A tap's ring (play::tap_highlight): the button the cells (row, c0) are a
// menu word of, or the party menu's button there (row: -1 - c0 its index)
bool q_tap_button(int row, int c0, int* x0, int* y0, int* x1, int* y1, int* lit)
{
    if (!d || !hl()) return false;
    if (row == -1) {
        q_pm_button_box(c0, x0, y0, x1, y1);
        *lit = c0 == q_pm_sel_button();
        return true;
    }
    if (!menu_on || row != text::kMenuRow) return false;
    for (int k = 0; k < menu.count; ++k)
        if (text::word_col(menu, k) == c0) {
            text::button_box(menu, k, x0, y0, x1, y1, q_buttons());
            *lit = k == menu.selected;
            return true;
        }
    return false;
}

// ---- The party menu (queen_facts_3.md 1) ----------------------------------
// Two columns of six buttons under the party's box: Add, Remove, Modify,
// Train, Knight New Order, View | Create, Delete, Load, Save, Begin, Exit
// (the profile's words in that order) - the left column x 4-155, the right
// 164-315, rows from y 117 every 14 pixels, 11 high, the words from 4
// pixels in (y 118); one not offered now sunken with black letters.

void q_pm_button_box(int i, int* x0, int* y0, int* x1, int* y1)
{
    *x0 = i < 6 ? 4 : 164;
    *x1 = *x0 + 152;
    *y0 = 117 + 14 * (i % 6);
    *y1 = *y0 + 11;
}

int q_pm_button_at(int x, int y)
{
    for (int i = 0; i < 12 && i < d->items; ++i) {
        int x0, y0, x1, y1;
        q_pm_button_box(i, &x0, &y0, &x1, &y1);
        // (the gaps between count: the button above or beside)
        if (x >= x0 - 4 && x < x1 + 4 && y >= y0 - 2 && y < y1 + 1) return i;
    }
    return -1;
}

int q_pm_sel_button() { return keys_used && pm_lines ? pm_item[pm_sel] : -1; }

__attribute__((noinline)) void q_pm_buttons(pic::Canvas& c, const bool on[])
{
    q_canvas = &c;
    const text::MenuColors mc = q_buttons();
    const int sel = q_pm_sel_button();
    for (int i = 0; i < 12 && i < d->items; ++i) {
        int x0, y0, x1, y1;
        q_pm_button_box(i, &x0, &y0, &x1, &y1);
        // (the right column always raised - DOSBox, with no party too: a
        // tap on one with nothing to do does nothing)
        const bool up = on[i] || i >= 6;
        text::bevel(c, x0, y0, x1, y1, mc.b_light, mc.b_fill, !up);
        const char* w = d->item[i];
        for (int k = 0; w[k] && k < 18; ++k) {
            if (w[k] == ' ' && i != sel) continue;
            const uint8_t fg = i == sel ? 0 : !up ? 0 : k == 0 ? 15 : 7;
            const uint8_t bg = i == sel ? 15 : mc.b_fill;
            font::draw_glyph(c, d->font, font::glyph_of(d->font, w[k]), x0 + 4 + k * 8, y0 + 1, fg, bg);
        }
    }
    q_pm_up = true;
    dirty(116, pic::kScreenH);
}

// The buttons away (a question on the bar - DOSBox: the party menu's
// buttons go while "Load from where?" asks): the lower marble and the bars
__attribute__((noinline)) void q_pm_hide(pic::Canvas& c)
{
    if (!q_pm_up) return;
    q_pm_up = false;
    plat::File fa, fb;
    const bool a = open_hl("GEN.TLB", fa), b = open_hl("FRAME.TLB", fb);
    {
        plat::FileSource sa(fa), sb(fb);
        const int16_t paper = c.paper;
        c.paper = -1;
        hlib::Library lib;
        uint32_t pos, len;
        hlib::Part p;
        auto part = [&](plat::FileSource& s, int i) {
            if (hlib::open(s, 0, s.size(), lib) == hlib::Status::Ok && hlib::entry(s, lib, i, pos, len) &&
                hlib::read_part(s, pos, len, p))
                hlpic::draw(s, p, q_col(), c, hlpic::place_x(p, 0), hlpic::place_y(p, 0), hlpic::Clip{0, 111, 320, 200});
        };
        if (a) part(sa, 1);
        if (b) for (int i : {2, 3, 4}) part(sb, i);
        c.paper = paper;
    }
    if (a) fa.close();
    if (b) fb.close();
    dirty(111, pic::kScreenH);
}

// ---- The title (queen_facts_3.md 2) ----------------------------------------
// TITLE.TLB's picture `id`: its parts at their places (the first the
// background or the whole screen, sprites over it), in its own colours
__attribute__((noinline)) bool q_title_picture(pic::Canvas& c, int id)
{
    plat::File f;
    if (!open_hl("TITLE.TLB", f)) return false;
    bool ok = false;
    {
        plat::FileSource src(f);
        uint32_t pos, len;
        hlib::Library lib;
        if (hlib::find_entry(src, static_cast<uint16_t>(id), pos, len) &&
            hlib::open(src, pos, len, lib) == hlib::Status::Ok) {
            const int16_t paper = c.paper;
            c.paper = -1;
            for (int part = 1; part < lib.count; ++part)
                ok = hlpic::draw_picture_part(src, pos, len, part, q_col(), nullptr, c, 0, 0) || ok;
            c.paper = paper;
        }
    }
    f.close();
    return ok;
}

// A line centred on the screen's 40 columns at pixel row y
void q_centred(pic::Canvas& c, const char* s, int y, uint8_t fg)
{
    const int n = static_cast<int>(strlen(s)), x = (font::kCols - n) / 2 * 8;
    for (int i = 0; i < n; ++i) font::draw_glyph(c, d->font, font::glyph_of(d->font, s[i]), x + i * 8, y, fg, 0);
}

// ---- Screens of boxes (queen_facts_3.md 5) -----------------------------------
// The original's character screens: grey boxes (the frame's colour 23) sunk
// into the marble - each box's inside x0-x1 / y0-y1, black above and left,
// light (17) below and right; the marble between them FRAME.TLB's 320 x 7
// strip (part 7) at the gaps above row 110 and its 8 x 94 bar (part 11,
// x 152-159, rows 69-162) between side-by-side boxes; the lower marble
// (GEN.TLB) and the bars as the party menu's.
struct QBox { int16_t x0, y0, x1, y1; };

void q_box(pic::Canvas& c, const QBox& b)
{
    c.fill(b.x0, b.y0, b.x1 - b.x0 + 1, b.y1 - b.y0 + 1, 0);
    const uint8_t light = c.vga && c.bpp == 8 ? 17 : q_col().ega[17];
    for (int x = b.x0 - 1; x <= b.x1 + 1; ++x) {
        c.set(x, b.y0 - 1, 0);
        c.set(x, b.y1 + 1, light);
    }
    for (int y = b.y0 - 1; y <= b.y1 + 1; ++y) {
        c.set(b.x0 - 1, y, 0);
        c.set(b.x1 + 1, y, light);
    }
}

// FRAME.TLB's part i drawn with its top left at (x, y)
void q_frame_part_at(pic::Canvas& c, plat::FileSource& src, const hlib::Library& lib, int i, int x, int y)
{
    uint32_t pos, len;
    hlib::Part p;
    if (hlib::entry(src, lib, i, pos, len) && hlib::read_part(src, pos, len, p)) hlpic::draw(src, p, q_col(), c, x, y);
}

__attribute__((noinline)) void q_screen(pic::Canvas& c, const QBox* boxes, int n, bool divider)
{
    q_draw_frame(c, false);             // (the bars, the lower marble, the party menu's box)
    plat::File f;
    if (open_hl("FRAME.TLB", f)) {
        plat::FileSource src(f);
        hlib::Library lib;
        const int16_t paper = c.paper;
        c.paper = -1;
        if (hlib::open(src, 0, src.size(), lib) == hlib::Status::Ok) {
            // (the party menu's box away: marble strips over rows 8-109)
            for (int y = 8; y < 110; y += 7) q_frame_part_at(c, src, lib, 7, 0, y);
            if (divider) q_frame_part_at(c, src, lib, 11, 152, 69);
        }
        c.paper = paper;
        f.close();
    }
    for (int k = 0; k < n; ++k) q_box(c, boxes[k]);
    dirty(0, pic::kScreenH);
}


// (play_queen_late.inc)
void q_draw_character(pic::Canvas& c, const party::Character& ch);
void q_name_tables(dax::ByteSource& exe, const exepack::Info& info);
bool q_save(char slot, const savegame::Header& h, const char* path);

// (play_queen_late.inc: its fights)
bool q_load_monster(int id, uint8_t* rec, uint8_t* sx, uint8_t* items, int* n_items, int items_cap, uint8_t* aff,
                    int* n_aff, int aff_cap);
uint8_t* q_load_pic4(const char* file, int id, int* w, int* h);
void q_load_tiles();
void q_draw_fight_frame(pic::Canvas& c);
void q_status(pic::Canvas& c, const char* t);

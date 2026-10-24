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

// The screen: the exploring frame (the view's frame, the compass, the
// moons) or, for the party menu, the marble with one box
__attribute__((noinline)) void q_draw_frame(pic::Canvas& c, bool explore)
{
    c.paper = -1;
    c.clear(0);
    plat::File fa, fb;
    const bool a = open_hl("FRAME.TLB", fa), b = open_hl("GEN.TLB", fb);
    {
        plat::FileSource sa(fa), sb(fb);
        int moons[3];
        q_moons(moons);
        qview::draw_frame(c, a ? &sa : nullptr, b ? &sb : nullptr, explore ? d->gs.dir : -1, moons, q_col());
    }
    if (a) fa.close();
    if (b) fb.close();
    q_paper(c, true);
    if (!explore) c.fill(8, 8, pic::kScreenW - 16, 176, 0);      // (the party menu: one box)
    dirty(0, pic::kScreenH);
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

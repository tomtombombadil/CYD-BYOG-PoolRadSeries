// Combat (Curse first): the Play Test's front end for engine/combat - part
// of play.cpp (included inside its anonymous namespace: it uses the Play
// Test's screen, words and files).
//
// LOAD MONSTER reads MON<area>CHA (the record), SPC (effects) and ITM
// (items) of the monster, its CPIC<area> icon; COMBAT then builds the
// battlefield (indoors from the 3D map, outdoors a random field), places
// both sides and plays the rounds: initiative, each fighter's turn - the
// player's menu "Move View Aim Use Cast Turn Quick Done" (Move: tap a
// square next to the fighter - or the pad - to step or attack that way,
// the menu line to stop; Aim: "Next Prev Manual Target Center Exit", or tap
// someone on the field; Done: "Guard Delay Quit Bandage Speed Exit"), the
// computer's turns for monsters and Quick fighters; attacks, damage,
// dying and bleeding, the round's end. Then "The party has won." with the
// experience, and the treasure ("View Take Pool Share Exit").
//
// The screen (as the games draw it): the combat frame, a 7 x 7 view of the
// field (24 x 24 squares from pixel 8, 8), the fighter whose turn it is on
// the right (name, "Hitpoints", "AC", the readied weapon; messages from
// row 10), row 23 for "Range = N", the menu on row 24. Colours 0 and 8 are
// swapped while fighting (the field's black shows dark grey).

constexpr int kViewX = 8, kViewY = 8, kSq = 24;

struct MonsterLoad {
    combat::Monster* m = nullptr;
    int n = 0, cap = 0;
    combat::Group g[combat::kMaxGroups];
    int groups = 0;
};
MonsterLoad* ml = nullptr;

void fight_clear_monsters()
{
    if (!ml) return;
    free(ml->m);
    delete ml;
    ml = nullptr;
}

// Reads DAX block `block` of a file (raw bytes), up to cap; its size or -1
int read_block(const char* file, int block, uint8_t* out, int cap)
{
    fs::File f;
    if (!open_dax(file, f)) return -1;
    library::FileSource src(f);
    int n = -1;
    const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
    if (e) {
        dax::RleReader r(src, d->idx, *e);
        const uint32_t want = e->raw_size < static_cast<uint32_t>(cap) ? e->raw_size : static_cast<uint32_t>(cap);
        n = static_cast<int>(r.read(out, want));
    }
    f.close();
    return n;
}

// ADD NPC: MON<area> block `id` (its record, effects, items) joins the party
// at its end and is selected; its fight icon is CPIC<area> block `id`
bool npc_join(int id)
{
    if (pt->count >= party::kMaxParty) return false;
    party::Character& ch = pt->m[pt->count];
    ch = party::Character{};
    char file[24];
    snprintf(file, sizeof file, "MON%dCHA.DAX", d->gs.game_area);
    if (read_block(file, id, ch.rec, sizeof ch.rec) != static_cast<int>(sizeof ch.rec)) {
        Serial.printf("[play] ADD NPC: %s #%d not there\n", file, id);
        return false;
    }
    snprintf(file, sizeof file, "MON%dITM.DAX", d->gs.game_area);
    const int ni = read_block(file, id, &ch.items[0][0], sizeof ch.items);
    ch.n_items = ni > 0 ? ni / static_cast<int>(party::kItemSize) : 0;
    snprintf(file, sizeof file, "MON%dSPC.DAX", d->gs.game_area);
    const int na = read_block(file, id, &ch.affects[0][0], sizeof ch.affects);
    ch.n_affects = na > 0 ? na / static_cast<int>(party::kAffectSize) : 0;
    ch.rec[0x126] = static_cast<uint8_t>(id);
    // The lowest icon slot no one else has
    for (int slot = 0; slot < 8; ++slot) {
        bool used = false;
        for (int i = 0; i < pt->count; ++i)
            if (pt->m[i].rec[0x143] == slot) used = true;
        if (!used) {
            ch.rec[0x143] = static_cast<uint8_t>(slot);
            break;
        }
    }
    pt->selected = pt->count++;
    char nm[20];
    ch.name(nm, sizeof nm);
    Serial.printf("[play] ADD NPC %d: %s joins\n", id, nm);
    return true;
}

void fight_load_monster(int id, int copies, int icon)
{
    if (!ml) ml = new (std::nothrow) MonsterLoad;
    if (!ml || ml->groups >= combat::kMaxGroups || ml->n >= combat::kMaxMonsters) return;
    uint8_t rec[party::kRecordSize];
    // MON<area>CHA.DAX
    char cha[24];
    snprintf(cha, sizeof cha, "MON%dCHA.DAX", d->gs.game_area);
    if (read_block(cha, id, rec, sizeof rec) != static_cast<int>(sizeof rec)) {
        Serial.printf("[play] %s #%d: no monster\n", cha, id);
        return;
    }
    combat::Group& g = ml->g[ml->groups];
    g = combat::Group{};
    g.id = static_cast<uint8_t>(id);
    g.icon = static_cast<uint8_t>(icon);
    char itm[24];
    snprintf(itm, sizeof itm, "MON%dITM.DAX", d->gs.game_area);
    uint8_t buf[combat::kGroupItems * items::kRecordSize];
    const int ni = read_block(itm, id, buf, sizeof buf);
    if (ni > 0) {
        g.n_items = static_cast<uint8_t>(ni / items::kRecordSize);
        memcpy(g.item, buf, static_cast<size_t>(g.n_items) * items::kRecordSize);
    }
    uint8_t aff[combat::kMonsterAffects * party::kAffectSize];
    char spc[24];
    snprintf(spc, sizeof spc, "MON%dSPC.DAX", d->gs.game_area);
    const int na = read_block(spc, id, aff, sizeof aff);
    if (copies < 1) copies = 1;
    if (ml->n + copies > combat::kMaxMonsters) copies = combat::kMaxMonsters - ml->n;
    if (ml->n + copies > ml->cap) {
        const int cap = ml->n + copies;
        void* p = realloc(ml->m, static_cast<size_t>(cap) * sizeof(combat::Monster));
        if (!p) {
            Serial.println("[play] LOAD MONSTER: not enough memory");
            return;
        }
        ml->m = static_cast<combat::Monster*>(p);
        ml->cap = cap;
    }
    for (int k = 0; k < copies; ++k) {
        combat::Monster& m = ml->m[ml->n++];
        memset(&m, 0, sizeof m);
        memcpy(m.rec, rec, sizeof rec);
        m.n_aff = na > 0 ? na / static_cast<int>(party::kAffectSize) : 0;
        memcpy(m.aff, aff, static_cast<size_t>(m.n_aff) * party::kAffectSize);
        m.group = static_cast<uint8_t>(ml->groups);
    }
    ++ml->groups;
    Serial.printf("[play] LOAD MONSTER %d x%d (icon %d): %d monsters\n", id, copies, icon, ml->n);
}

// ---- Pictures, 4 bits a pixel
struct Pic4 {
    uint8_t w = 0, h = 0;
    uint8_t* px[2] = {};                // ready, attack: rows of w / 2 bytes
};

void free_pic(Pic4& p)
{
    free(p.px[0]);
    free(p.px[1]);
    p = Pic4{};
}

// Frame `frame` of a picture block into 4-bit rows (malloc'd); w, h
uint8_t* load_pic4(const char* file, int block, int frame, int* w, int* h)
{
    fs::File f;
    if (!open_dax(file, f)) return nullptr;
    library::FileSource src(f);
    uint8_t* out = nullptr;
    const dax::Entry* e = d->idx.find(static_cast<uint8_t>(block));
    if (e) {
        dax::RleReader r(src, d->idx, *e);
        uint8_t hdr[pic::kHeaderSize];
        pic::Header ph;
        if (r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, ph) && frame < ph.frames) {
            const uint32_t fb = ph.frame_bytes();
            out = static_cast<uint8_t*>(malloc(fb));
            bool ok = out != nullptr;
            for (int k = 0; ok && k <= frame; ++k) ok = r.read(out, fb) == fb;
            if (!ok) {
                free(out);
                out = nullptr;
            } else {
                *w = ph.width_px();
                *h = ph.height;
            }
        }
    }
    f.close();
    return out;
}

inline int nib(const uint8_t* p, int w, int x, int y)
{
    const uint8_t b = p[y * (w / 2) + x / 2];
    return x & 1 ? b & 15 : b >> 4;
}
inline void set_nib(uint8_t* p, int w, int x, int y, int v)
{
    uint8_t& b = p[y * (w / 2) + x / 2];
    b = static_cast<uint8_t>(x & 1 ? (b & 0xF0) | (v & 15) : (b & 0x0F) | (v << 4));
}

// Draws a 4-bit picture clipped to (cx0, cy0)-(cx1, cy1): mirrored, colour 0 see-through
void blit4(pic::Canvas& c, const uint8_t* p, int w, int h, int x, int y, bool mirror, bool see_through, int cx0,
           int cy0, int cx1, int cy1)
{
    if (!p) return;
    for (int j = 0; j < h; ++j) {
        const int yy = y + j;
        if (yy < cy0 || yy >= cy1) continue;
        uint8_t* row = c.px + static_cast<size_t>(yy) * c.w;
        for (int i = 0; i < w; ++i) {
            const int xx = x + i;
            if (xx < cx0 || xx >= cx1) continue;
            const int v = nib(p, w, mirror ? w - 1 - i : i, j);
            if (see_through && v == 0) continue;
            row[xx] = static_cast<uint8_t>(v);
        }
    }
}

// ---- The fight's state (on the heap while fighting)
enum class FSt : uint8_t {
    Begin, Turn, Menu, Move, Aim, DoneMenu, Speed, Ai, Pages, RoundEnd, Results, Destroyed, AllyAsk, FleeAsk,
    ContinueAsk, Next, UseAim, AbortAsk
};

struct Page {
    char    line[5][40];
    uint8_t colour[5];
    int     lines = 0;
};

struct Fight {
    combat::Battle b;
    combat::Tables t;
    uint8_t  (*tile)[kSq * kSq / 2] = nullptr;      // the field's 40 pictures (a block of their own)
    Pic4     icon[8 + combat::kMaxGroups];
    uint8_t* cursor = nullptr;          // COMSPR 0x19 (ready): the box under the one whose turn it is
    uint8_t* skull = nullptr;           // COMSPR 11 (attack)
    uint8_t* outline = nullptr;         // COMSPR 0x19 (attack): the box round a target being aimed at
    char     w[profile::kFightWords][40];
    FSt      st = FSt::Begin;
    FSt      after = FSt::Turn;         // where the pages go on to
    int      cur = -1;
    uint32_t until = 0;
    Page     page[12];
    int      pages = 0, at = 0;
    // Move: where the fighter started (Esc undoes the move)
    int      move_x = 0, move_y = 0, move_left = 0;
    // Aim: the candidates by distance, the one shown
    int      cand[combat::kMaxFighters];
    int      n_cand = 0, ci = 0;
    bool     manual = false;
    int      mx = 0, my = 0;            // Manual's square
    int      pending = -1;              // an ally to attack (asked)
    int      flee_dir = 8;
    // Casting: the spell, the targets picked, how many more, an area's radius (-1: none)
    int      spell = 0;
    int      sp_t[combat::kMaxFighters] = {};
    int      sp_n = 0, sp_need = 0, sp_area = -1;
    bool     ai_checked = false;        // this turn's morale and spell choice done
    int      use_item = -1;             // Use: the item the spell comes from (its charge goes)
    bool     use_scroll = false;        // ... a scroll
    combat::Outcome out;
    int      share = 0;
    bool     treasure_only = false;
};
Fight* fg = nullptr;

const char* fw(int i) { return fg ? fg->w[i] : ""; }
bool in_fight() { return fg != nullptr; }

void load_fight_words()
{
    fs::File f;
    if (!open_file(d->prof->overlay, f)) return;
    library::FileSource src(f);
    for (int i = 0; i < profile::kFightWords; ++i)
        if (d->prof->fight.words[i]) text::read_pascal(src, d->prof->fight.words[i], fg->w[i], sizeof fg->w[i]);
    f.close();
}

bool load_fight_tables()
{
    fs::File f;
    if (!open_file(d->prof->program, f)) return false;
    library::FileSource src(f);
    exepack::Info info;
    struct Ctx {
        library::FileSource* src;
        exepack::Info* info;
        uint32_t base;
    } ctx{&src, &info, d->prof->create.ds_image};
    bool ok = exepack::parse(src, info) == exepack::Status::Ok &&
              combat::read_tables(
                  fg->t, d->prof->fight.tables,
                  [](void* p, uint16_t ds, uint8_t* out, size_t n) {
                      Ctx* c = static_cast<Ctx*>(p);
                      return exepack::read(*c->src, *c->info, c->base + ds, out, n) == exepack::Status::Ok;
                  },
                  &ctx);
    f.close();
    return ok;
}

// Frames first .. first + n - 1 of a 24 x 24 picture block into the tiles
void load_frames(const char* file, int at, int n)
{
    fs::File f;
    if (!open_dax(file, f)) return;
    library::FileSource src(f);
    const dax::Entry* e = d->idx.find(1);
    if (e) {
        dax::RleReader r(src, d->idx, *e);
        uint8_t hdr[pic::kHeaderSize];
        pic::Header ph;
        if (r.read(hdr, sizeof hdr) == sizeof hdr && pic::parse_header(hdr, e->raw_size, ph) && ph.width_px() == kSq &&
            ph.height == kSq)
            for (int k = 0; k < n && k < ph.frames && at + k < 40; ++k)
                if (r.read(fg->tile[at + k], sizeof fg->tile[0]) != sizeof fg->tile[0]) break;
    }
    f.close();
}

// The field's pictures: DUNGCOM / WILDCOM (0-33), RANDCOM (34-39)
void load_tiles(bool indoors)
{
    memset(fg->tile, 0, 40 * sizeof fg->tile[0]);
    load_frames(indoors ? "DUNGCOM.DAX" : "WILDCOM.DAX", 0, indoors ? 25 : 34);
    load_frames("RANDCOM.DAX", 34, 6);
}

// A party member's icon: the head over the body's top rows, recoloured
void load_monster_icon(int slot, int block);

void compose_icon(int slot, const party::Character& ch)
{
    if (ch.rec[0x126] && !ch.rec[0x141] && !ch.rec[0x142]) {
        load_monster_icon(slot, ch.rec[0x126]);     // an NPC from the monsters' files: its own picture
        return;
    }
    Pic4& ic = fg->icon[slot];
    free_pic(ic);
    const int head = ch.rec[0x141], body = ch.rec[0x142];
    const bool small = ch.rec[0x144] == 1;
    static const uint8_t kDefault[6] = {1, 2, 3, 4, 6, 7};
    for (int fr = 0; fr < 2; ++fr) {
        int bw, bh, hw, hh;
        uint8_t* b = load_pic4("CBODY.DAX", body + (small ? 0 : 0x40) + fr * 0x80, 0, &bw, &bh);
        uint8_t* h = load_pic4("CHEAD.DAX", head + (small ? 0 : 0x40) + fr * 0x80, 0, &hw, &hh);
        if (!b || bw != kSq || bh != kSq) {
            free(b);
            free(h);
            continue;
        }
        if (h && hw == kSq)
            for (int y = 0; y < hh && y < kSq; ++y)
                for (int x = 0; x < kSq; ++x) {
                    const int v = nib(h, hw, x, y);
                    if (v) set_nib(b, kSq, x, y, v);
                }
        free(h);
        // Recolour: default colour d -> the byte's low nibble, d + 8 -> its high
        for (int y = 0; y < kSq; ++y)
            for (int x = 0; x < kSq; ++x) {
                const int v = nib(b, kSq, x, y);
                for (int i = 0; i < 6; ++i) {
                    const uint8_t cb = ch.rec[0x145 + i];
                    if (v == kDefault[i]) set_nib(b, kSq, x, y, cb & 15);
                    else if (v == kDefault[i] + 8) set_nib(b, kSq, x, y, cb >> 4);
                    else continue;
                    break;
                }
            }
        ic.px[fr] = b;
        ic.w = ic.h = kSq;
    }
}

void load_monster_icon(int slot, int block)
{
    Pic4& ic = fg->icon[slot];
    free_pic(ic);
    char file[16];
    snprintf(file, sizeof file, "CPIC%d.DAX", d->gs.game_area);
    for (int fr = 0; fr < 2; ++fr) {
        int w, h;
        uint8_t* p = load_pic4(file, block + fr * 0x80, 0, &w, &h);
        if (!p) continue;
        ic.px[fr] = p;
        ic.w = static_cast<uint8_t>(w);
        ic.h = static_cast<uint8_t>(h);
    }
}

void end_fight_art()
{
    if (!fg) return;
    free(fg->tile);
    fg->tile = nullptr;
    for (Pic4& p : fg->icon) free_pic(p);
    free(fg->cursor);
    free(fg->skull);
    free(fg->outline);
    fg->cursor = fg->skull = fg->outline = nullptr;
}

// ---- Drawing
bool fight_palette_on = false;

void palette_fight(bool on)
{
    fight_palette_on = on;
    dirty(0, pic::kScreenH);
}

uint8_t name_colour(const combat::Fighter& f) { return !f.up() ? 12 : f.team() ? 14 : 11; }

void fighter_name(const combat::Fighter& f, char* out, size_t cap)
{
    const int len = f.rec[0] < 15 ? f.rec[0] : 15;
    snprintf(out, cap, "%.*s", len, reinterpret_cast<const char*>(f.rec + 1));
}

void draw_icon(pic::Canvas& c, int i, bool attack = false)
{
    const combat::Fighter& f = fg->b.f[i];
    if (!f.size) return;
    const Pic4& ic = fg->icon[f.icon];
    const uint8_t* p = ic.px[attack && ic.px[1] ? 1 : 0];
    if (!p) return;
    const int x = kViewX + (f.x - fg->b.vx) * kSq, y = kViewY + (f.y - fg->b.vy) * kSq;
    blit4(c, p, ic.w, ic.h, x, y, f.facing >= 4, true, kViewX, kViewY, kViewX + 7 * kSq, kViewY + 7 * kSq);
}

void draw_square(pic::Canvas& c, int x, int y)
{
    const int sx = kViewX + (x - fg->b.vx) * kSq, sy = kViewY + (y - fg->b.vy) * kSq;
    const uint8_t* g = combat::tile(fg->b, fg->t, x, y);
    const int pic_no = x >= 0 && x < combat::kW && y >= 0 && y < combat::kH && fg->b.ground[y][x] ? g[3] : -1;
    if (pic_no < 0 || pic_no >= 40 || !fg->tile) c.fill(sx, sy, kSq, kSq, 0);
    else blit4(c, fg->tile[pic_no], kSq, kSq, sx, sy, false, false, 0, 0, pic::kScreenW, pic::kScreenH);
}

void draw_field(pic::Canvas& c)
{
    for (int j = 0; j < 7; ++j)
        for (int i = 0; i < 7; ++i) draw_square(c, fg->b.vx + i, fg->b.vy + j);
    for (int k = 0; k < fg->b.n; ++k)
        if (fg->b.f[k].up()) draw_icon(c, k);
    dirty(kViewY, kViewY + 7 * kSq);
}

void clear_panel(pic::Canvas& c, int r0 = 1, int r1 = 21)
{
    c.fill(23 * 8, r0 * 8, 16 * 8, (r1 - r0 + 1) * 8, 0);
    dirty_rows(r0, r1);
}

// Words wrapped into the panel (columns 23-38) from `row`; the next row
int panel_text(pic::Canvas& c, const char* s, int row, uint8_t colour, int last = 21)
{
    char line[17];
    while (*s && row <= last) {
        while (*s == ' ') ++s;
        int n = static_cast<int>(strlen(s));
        if (n > 16) {
            n = 16;
            while (n > 0 && s[n] != ' ') --n;
            if (n == 0) n = 16;
        }
        memcpy(line, s, n);
        line[n] = 0;
        put(c, line, 23, row++, colour);
        s += n;
    }
    return row;
}

void draw_panel_for(pic::Canvas& c, int i)
{
    clear_panel(c);
    if (i < 0) return;
    const combat::Fighter& f = fg->b.f[i];
    char nm[20], t[48];
    fighter_name(f, nm, sizeof nm);
    put(c, nm, 23, 1, name_colour(f));
    put(c, fw(profile::kHitpoints), 23, 3, 10);
    snprintf(t, sizeof t, "%d", f.hp());
    put(c, t, 33, 3, f.hp() < f.hp_max() ? 14 : 10);
    put(c, fw(profile::kAcW), 23, 5, 10);
    const int ac = f.rec[0x19A];
    snprintf(t, sizeof t, "%s%d", ac > 60 ? "-" : "", ac > 60 ? ac - 60 : 60 - ac);
    put(c, t, 26, 5, 10);
    int row = 7;
    const int wi = combat::weapon(f, *names);
    if (wi >= 0) {
        names->name(items::Item{f.items[wi]}, t, sizeof t);
        row = panel_text(c, t, 7, 10, 9) + 1;
    }
    if (!f.up()) put(c, name_of(d->health[0], 13, 9, f.status()), 23, row, 15);
}

// The view brought to fighter i (radius r: moved only when further than
// that from the centre)
void focus(int i, int r)
{
    combat::Battle& b = fg->b;
    const combat::Fighter& f = b.f[i];
    int cx = b.vx + 3, cy = b.vy + 3;
    if (f.x - cx > r || cx - f.x > r || r == 0xFF) cx = f.x;
    if (f.y - cy > r || cy - f.y > r || r == 0xFF) cy = f.y;
    if (cx < 3) cx = 3;
    if (cx > combat::kW - 4) cx = combat::kW - 4;
    if (cy < 3) cy = 3;
    if (cy > combat::kH - 4) cy = combat::kH - 4;
    b.vx = cx - 3;
    b.vy = cy - 3;
}

bool on_screen(int i)
{
    const combat::Fighter& f = fg->b.f[i];
    return f.x >= fg->b.vx - 1 && f.x < fg->b.vx + 7 && f.y >= fg->b.vy - 1 && f.y < fg->b.vy + 7;
}

// The one whose turn it is: the box under them, their icon on it
void mark(pic::Canvas& c, int i)
{
    const combat::Fighter& f = fg->b.f[i];
    if (!f.size || !fg->cursor) return;
    int sx[4], sy[4];
    const int n = combat::squares(f.size, sx, sy);
    for (int k = 0; k < n; ++k) {
        const int x = kViewX + (f.x + sx[k] - fg->b.vx) * kSq, y = kViewY + (f.y + sy[k] - fg->b.vy) * kSq;
        blit4(c, fg->cursor, kSq, kSq, x, y, false, true, kViewX, kViewY, kViewX + 7 * kSq, kViewY + 7 * kSq);
    }
    draw_icon(c, i);
}

void draw_fight(pic::Canvas& c)
{
    c.fill(0, 0, pic::kScreenW, 24 * 8, 0);
    layout::combat(c, d->tables, d->frame_tiles);
    draw_field(c);
    if (fg->cur >= 0) {
        mark(c, fg->cur);
        draw_panel_for(c, fg->cur);
    }
    dirty(0, pic::kScreenH);
}

void status_line(pic::Canvas& c, const char* t)
{
    c.fill(0, 23 * 8, pic::kScreenW, 8, 0);
    if (t && *t) put(c, t, 0, 23, 10);
    dirty_rows(23, 23);
}

// ---- Messages: pages in the panel from row 10, a game delay each
void add_page(int who, const char* a, const char* b2 = nullptr, int whom = -1, const char* c2 = nullptr)
{
    if (fg->pages >= 12) return;
    Page& p = fg->page[fg->pages++];
    p.lines = 0;
    auto line = [&](const char* s, uint8_t col) {
        if (p.lines < 5 && s) {
            snprintf(p.line[p.lines], sizeof p.line[0], "%s", s);
            p.colour[p.lines++] = col;
        }
    };
    char nm[20];
    if (who >= 0) {
        fighter_name(fg->b.f[who], nm, sizeof nm);
        line(nm, name_colour(fg->b.f[who]));
    }
    line(a, 10);
    if (whom >= 0) {
        fighter_name(fg->b.f[whom], nm, sizeof nm);
        line(nm, name_colour(fg->b.f[whom]));
    }
    line(b2, 10);
    line(c2, 10);
}

void show_page(pic::Canvas& c)
{
    const Page& p = fg->page[fg->at++];
    clear_panel(c, 10, 21);
    int row = 10;
    for (int k = 0; k < p.lines; ++k) row = panel_text(c, p.line[k], row, p.colour[k]);
    fg->until = millis() + game_delay_ms() * 2;
    if (!fg->until) fg->until = 1;
}

// The pages queued: shown one after another, then on to `next`
void say(pic::Canvas& c, FSt next)
{
    fg->after = next;
    fg->st = FSt::Pages;
    fg->at = 0;
    if (menu_on) clear_menu_line(c);
    if (fg->pages) show_page(c);
    else fg->until = 1;
}

// ---- Turns
void begin_round(pic::Canvas& c);
void next_turn(pic::Canvas& c);
void fight_menu(pic::Canvas& c);
void end_fight(pic::Canvas& c);

void end_turn(pic::Canvas& c)
{
    if (fg->cur >= 0) {
        combat::Fighter& f = fg->b.f[fg->cur];
        f.delay = 0;
        f.moves = 0;
    }
    status_line(c, "");
    next_turn(c);
}

// An attack and what it did, as pages
void do_attack(pic::Canvas& c, int a, int t, bool behind)
{
    combat::Battle& b = fg->b;
    if (!on_screen(a) || !on_screen(t)) {
        focus(t, 3);
        draw_fight(c);
    }
    draw_icon(c, a, true);
    dirty(kViewY, kViewY + 7 * kSq);
    const combat::Attack at = combat::attack(b, a, t, names, rng, behind);
    char line[48];
    bool first = true;
    if (at.slain) {
        add_page(a, fw(profile::kSlays), fw(profile::kCruelBlow), t);
        first = false;
    }
    for (int k = 0; k < at.n && !at.slain; ++k) {
        if (!at.hits[k].hit) continue;
        const int dmg = at.hits[k].damage;
        snprintf(line, sizeof line, "%s%s%d%s%s", behind ? fw(profile::kFromBehind) : "", fw(profile::kHittingFor), dmg,
                 dmg == 1 ? fw(profile::kPointW) : fw(profile::kPointsW), fw(profile::kOfDamage));
        add_page(a, fw(at.backstab ? profile::kBackstabs : profile::kAttacksW), line, t);
        first = false;
    }
    if (first) add_page(a, fw(at.backstab ? profile::kBackstabs : profile::kAttacksW), fw(profile::kMisses), t);
    if (at.down) {
        const combat::Down dn = combat::down_state(b.f[t]);
        if (dn == combat::Down::Dead) add_page(t, fw(profile::kIsKilled));
        else if (dn == combat::Down::Dying) add_page(t, fw(profile::kGoesDown), fw(profile::kIsDying));
        else add_page(t, fw(profile::kGoesDown));
    }
}

// After an attack's pages: the turn ends unless attacks are left and the
// target went down (then pick another)
void after_attack(pic::Canvas& c)
{
    draw_fight(c);
    const combat::Fighter& f = fg->b.f[fg->cur];
    const bool left = f.attacks[0] > 0 || f.attacks[1] > 0;
    const bool target_down = f.target >= 0 && !fg->b.f[f.target].up();
    if (combat::standing(fg->b, 0) == 0 || combat::standing(fg->b, 1) == 0) {
        end_turn(c);
        return;
    }
    if (left && target_down) {
        if (f.quick || f.team()) {
            fg->st = FSt::Ai;
            fg->until = millis() + 100;
        } else {
            fight_menu(c);
        }
        return;
    }
    end_turn(c);
}

// Leaving an enemy's side: each enemy next to them now and not after the
// step gets its attacks (from behind)
void free_attacks(pic::Canvas& c, int i, int dir)
{
    combat::Battle& b = fg->b;
    combat::Fighter& f = b.f[i];
    const int my = f.team() ? 1 : 0;
    for (int e = 0; e < b.n; ++e) {
        combat::Fighter& en = b.f[e];
        if (!en.up() || !en.size || (en.team() ? 1 : 0) == my || !combat::adjacent(b, i, e)) continue;
        // Still next to them after the step?
        f.x += combat::dx(dir);
        f.y += combat::dy(dir);
        const bool still = combat::adjacent(b, i, e);
        f.x -= combat::dx(dir);
        f.y -= combat::dy(dir);
        if (still || !combat::free_attack_ok(b, fg->t, e, i)) continue;
        if (en.attacks[0] <= 0 && en.attacks[1] <= 0) en.attacks[0] = 1;
        do_attack(c, e, i, true);
        if (!f.up()) return;
    }
}

// Guards next to the new square attack
void guard_attacks(pic::Canvas& c, int i)
{
    combat::Battle& b = fg->b;
    const int my = b.f[i].team() ? 1 : 0;
    for (int e = 0; e < b.n && b.f[i].up(); ++e) {
        combat::Fighter& en = b.f[e];
        if (!en.guarding || !en.up() || (en.team() ? 1 : 0) == my || !combat::adjacent(b, i, e)) continue;
        en.guarding = false;
        if (en.attacks[0] <= 0 && en.attacks[1] <= 0) en.attacks[0] = 1;
        do_attack(c, e, i, false);
    }
}

// A blow in melee: a fighter's sweep of the weak ones next to them ("sweeps":
// one attack on each, and the turn is over), else the attack
void melee(pic::Canvas& c, int a, int t)
{
    combat::Battle& b = fg->b;
    combat::Fighter& f = b.f[a];
    int sw[8];
    const int n = combat::adjacent(b, a, t) ? combat::sweep(b, a, t, sw, 8) : 0;
    if (!n) {
        do_attack(c, a, t, false);
        return;
    }
    f.swept = true;
    Serial.printf("[play] fighter %d sweeps %d\n", a, n);
    add_page(a, fw(profile::kSweeps));
    for (int k = 0; k < n && f.up(); ++k) {
        if (!b.f[sw[k]].up()) continue;
        f.attacks[0] = 1;
        f.attacks[1] = 0;
        do_attack(c, a, sw[k], false);
    }
    f.attacks[0] = f.attacks[1] = 0;
}

void forget_spell(combat::Fighter& f, int sp);
void begin_targeting(pic::Canvas& c, int sp);
void cast_now(pic::Canvas& c, bool forgotten = false);
const combat::FightSpell* fight_spell(int sp);

// A monster's item gone: off its group's list (the copies share it)
void remove_group_item(const combat::Fighter& f, int k)
{
    uint8_t (*list)[items::kRecordSize] = f.items;
    const int n = f.n_items;
    if (!list || k < 0 || k >= n) return;
    for (int j = k; j + 1 < n; ++j) memcpy(list[j], list[j + 1], items::kRecordSize);
    memset(list[n - 1], 0, items::kRecordSize);
    for (int j = 0; j < fg->b.n; ++j)
        if (fg->b.f[j].items == list) fg->b.f[j].n_items = n - 1;
    for (int g = 0; ml && g < combat::kMaxGroups; ++g)
        if (ml->g[g].item == list) ml->g[g].n_items = static_cast<uint8_t>(n - 1);
}

// A shot: one arrow / quarrel of the pile, or the thrown weapon, goes
void spend_ammo(int i, int k)
{
    combat::Fighter& f = fg->b.f[i];
    if (k < 0 || k >= f.n_items) return;
    if (f.member >= 0) {
        party::Character& ch = pt->m[f.member];
        const int left = ch.items[k][0x39];
        if (left > 1) ch.items[k][0x39] = static_cast<uint8_t>(left - 1);
        else rules::remove_item(ch, k);
        f.n_items = ch.n_items;
        rules::recalc(ch, *names, d->facts);
        return;
    }
    const int left = f.items[k][0x39];
    if (left > 1) f.items[k][0x39] = static_cast<uint8_t>(left - 1);
    else remove_group_item(f, k);
}

// ---- Use in a fight: the item's spell goes off at once (no casting time),
// at the item's level; using it ends the turn (even when aborted)
void spend_item(int i, int sp)
{
    const int k = fg->use_item;
    fg->use_item = -1;
    combat::Fighter& f = fg->b.f[i];
    if (k < 0 || k >= f.n_items) return;
    if (f.member >= 0) {
        party::Character& ch = pt->m[f.member];
        if (fg->use_scroll) magic::scroll_used(ch, scroll_facts(), k, sp);
        else magic::used(ch, k);
        rules::recalc(ch, *names, d->facts);
        f.n_items = ch.n_items;
        return;
    }
    // A monster's: its group's items (shared by the copies)
    if (magic::use_charge(f.items[k])) remove_group_item(f, k);
}

// "NAME uses an item", "Item: NAME" on row 23
void announce_item(pic::Canvas& c, int i, int k)
{
    const combat::Fighter& f = fg->b.f[i];
    char w1[16], nm[40], t[48];
    cw(profile::kUsesItem, w1, sizeof w1);
    add_page(i, w1);
    cw(profile::kItemColon, t, sizeof t);
    names->name(items::Item{f.items[k]}, nm, sizeof nm);
    strncat(t, nm, sizeof t - strlen(t) - 1);
    status_line(c, t);
}

void fight_use(int k, int sp, bool scroll, pic::Canvas& c)
{
    items_direct = false;
    screen = Screen::Fight;
    palette_fight(true);
    const int i = fg->cur;
    combat::Fighter& f = fg->b.f[i];
    if (f.member >= 0) f.n_items = pt->m[f.member].n_items;
    draw_fight(c);
    fg->use_item = k;
    fg->use_scroll = scroll;
    fg->spell = sp;
    fg->sp_n = 0;
    fg->sp_area = -1;
    if (scroll && f.member >= 0 && !magic::reads_scroll(pt->m[f.member], rng.roll(100, 1))) {
        char w1[12];
        cw(profile::kOops, w1, sizeof w1);
        add_page(i, w1);
        spend_item(i, sp);
        fg->spell = 0;
        say(c, FSt::Next);
        return;
    }
    const combat::FightSpell* fs = fight_spell(sp);
    if (!fs || fs->does == combat::SpellDoes::NotYet || spells::entry(mrules->tables, sp).when == 0) {
        fg->use_item = -1;
        fg->spell = 0;
        fight_menu(c);
        error(c, "Not in the engine yet.");
        return;
    }
    if (scroll) {
        begin_targeting(c, sp);
        return;
    }
    announce_item(c, i, k);
    say(c, FSt::UseAim);
}

void start_turn(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    combat::Fighter& f = b.f[fg->cur];
    f.received = f.turns = 0;
    f.guarding = false;
    fg->ai_checked = false;
    fg->use_item = -1;
    if (!f.team() || on_screen(fg->cur)) focus(fg->cur, 2);
    draw_fight(c);
    if (f.spell) {
        // A spell begun earlier this round goes off (lost if they were hurt)
        const int sp = f.spell;
        f.spell = 0;
        if (!f.can_cast) {
            if (!f.spell_n) forget_spell(f, sp);
            f.spell_n = 0;
            add_page(fg->cur, fw(profile::kLostSpell));
            say(c, FSt::Next);
            return;
        }
        if (f.spell_n) {
            // The computer's: the targets it chose
            if (!load_magic()) {
                end_turn(c);
                return;
            }
            fg->spell = sp;
            fg->sp_n = 0;
            for (int k = 0; k < f.spell_n; ++k)
                if (b.f[f.spell_t[k]].size) fg->sp_t[fg->sp_n++] = f.spell_t[k];
            f.spell_n = 0;
            if (!fg->sp_n) {
                end_turn(c);
                return;
            }
            cast_now(c, true);
            return;
        }
        begin_targeting(c, sp);
        return;
    }
    if (f.quick || f.team() || f.rec[0xF7] >= 0x80) {
        fg->st = FSt::Ai;
        fg->until = millis() + game_delay_ms();
        if (!fg->until) fg->until = 1;
        clear_menu_line(c);
        return;
    }
    fight_menu(c);
}

void next_turn(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    if (combat::standing(b, 0) == 0 || combat::standing(b, 1) == 0) {
        fg->cur = -1;
        fg->st = FSt::RoundEnd;
        fg->until = 1;
        return;
    }
    fg->cur = combat::next(b, rng);
    if (fg->cur < 0) {
        fg->st = FSt::RoundEnd;
        fg->until = 1;
        return;
    }
    start_turn(c);
}

void begin_round(pic::Canvas& c)
{
    combat::start_round(fg->b, rng);
    next_turn(c);
}

void round_end(pic::Canvas& c)
{
    vm->advance_clock(1, 1);                // a round is a minute
    vm->take_minutes();
    combat::tick(fg->b);                    // everyone's effects a minute on
    const bool over = combat::end_round(fg->b);
    if (over) {
        end_fight(c);
        return;
    }
    if (combat::anyone_dying(fg->b)) {
        clear_menu_line(c);
        put(c, fw(profile::kTeammateDying), 0, text::kMenuRow, 15);
        fg->st = FSt::Begin;            // a pause, then the next round
        fg->until = millis() + game_delay_ms() * 3;
        if (!fg->until) fg->until = 1;
        return;
    }
    begin_round(c);
}

// ---- The player's menu
void fight_menu(pic::Canvas& c)
{
    const combat::Fighter& f = fg->b.f[fg->cur];
    char words[64];
    words[0] = 0;
    if (f.moves > 1) strcat(words, fw(profile::kFMove));
    strcat(words, fw(profile::kFViewAim));
    if (f.n_items) strcat(words, fw(profile::kFUse));
    bool spells = false;
    for (int k = 0; k < magic::kListSize; ++k)
        if (f.rec[magic::kListAt + k] && !(f.rec[magic::kListAt + k] & 0x80)) spells = true;
    if (spells && f.can_cast) strcat(words, fw(profile::kFCast));
    if (f.rec[0x109] > 0 && !f.turned_undead) strcat(words, fw(profile::kFTurn));
    strcat(words, fw(profile::kFQuickDone));
    fg->st = FSt::Menu;
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

void move_prompt(pic::Canvas& c)
{
    char t[48];
    snprintf(t, sizeof t, "%s%d ", fw(profile::kMoveLeft), fg->b.f[fg->cur].moves / 2);
    clear_menu_line(c);
    put(c, t, 0, text::kMenuRow, 15);
    menu_on = false;
}

void start_move(pic::Canvas& c)
{
    const combat::Fighter& f = fg->b.f[fg->cur];
    fg->move_x = f.x;
    fg->move_y = f.y;
    fg->move_left = f.moves;
    fg->st = FSt::Move;
    move_prompt(c);
}

// Leaving Move: what's left stays (under 2: none)
void end_move(pic::Canvas& c)
{
    combat::Fighter& f = fg->b.f[fg->cur];
    if (f.moves < 2) f.moves = 0;
    if (!f.up()) {
        end_turn(c);
        return;
    }
    fight_menu(c);
}

// One step (or an attack that way) by the fighter whose turn it is
void try_step(pic::Canvas& c, int dir)
{
    combat::Battle& b = fg->b;
    const int i = fg->cur;
    combat::Fighter& f = b.f[i];
    bool edge;
    int blocker;
    const int cost = combat::step_cost(b, fg->t, i, dir, &edge, &blocker);
    if (blocker >= 0) {
        const combat::Fighter& o = b.f[blocker];
        if (f.attacks[0] <= 0 && f.attacks[1] <= 0) return;
        if (o.team() == f.team()) {
            fg->pending = blocker;
            fg->st = FSt::AllyAsk;
            text::build(menu, fw(profile::kAttackAlly), rw(Data::kYesNo));
            menu.selected = 1;
            show_menu_line(c);
            return;
        }
        f.target = blocker;
        melee(c, i, blocker);
        say(c, FSt::Turn);              // (after_attack)
        return;
    }
    if (edge) {
        fg->flee_dir = dir;
        fg->st = FSt::FleeAsk;
        text::build(menu, fw(profile::kFleeAsk), rw(Data::kYesNo));
        menu.selected = 1;
        show_menu_line(c);
        return;
    }
    if (cost == 0xFF || cost > f.moves) {
        note(c, fw(profile::kCantGo));
        return;
    }
    free_attacks(c, i, dir);
    if (!f.up()) {
        say(c, FSt::Turn);
        return;
    }
    combat::step(b, i, dir);
    f.moves -= cost;
    guard_attacks(c, i);
    if (fg->pages) {
        draw_fight(c);
        say(c, f.up() ? FSt::Move : FSt::Turn);
        return;
    }
    if (!on_screen(i) || f.x - b.vx < 1 || f.x - b.vx > 5 || f.y - b.vy < 1 || f.y - b.vy > 5) focus(i, 2);
    draw_fight(c);
    if (f.moves < 2) {
        end_move(c);
        return;
    }
    move_prompt(c);
}

// ---- Aim: the candidates by distance
void aim_menu(pic::Canvas& c);
int spell_power(const combat::Fighter& f);

bool can_target(int t, int* dist)
{
    combat::Battle& b = fg->b;
    const int i = fg->cur;
    const combat::Fighter& f = b.f[i];
    if (fg->spell) {
        // A spell: anyone in its reach and in sight (themselves too)
        if (t < 0 || !b.f[t].size) return false;
        int sq = 0;
        const bool seen = t == i || combat::range(b, fg->t, i, t, false, &sq);
        if (dist) *dist = sq;
        const int pw = fg->use_item >= 0 ? combat::power_of(f.rec, mrules->tables, fg->spell, true) : spell_power(f);
        return seen && sq <= spells::reach(mrules->tables, fg->spell, pw);
    }
    if (t == i || !b.f[t].up() || !b.f[t].size) return false;
    int sq;
    const bool seen = combat::range(b, fg->t, i, t, false, &sq);
    if (dist) *dist = sq;
    if (!seen) return false;
    int ammo, reach = combat::missile(b, f, &ammo);
    if (reach < 1) reach = 1;
    return sq <= reach && (f.attacks[0] > 0 || f.attacks[1] > 0);
}

void start_aim(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    const int i = fg->cur;
    fg->n_cand = 0;
    int dist[combat::kMaxFighters];
    for (int k = 0; k < b.n; ++k) {
        if (!b.f[k].up() || !b.f[k].size) continue;
        int sq = 0;
        combat::range(b, fg->t, i, k, true, &sq);
        // in order of distance
        int at = fg->n_cand++;
        while (at > 0 && dist[at - 1] > sq) {
            fg->cand[at] = fg->cand[at - 1];
            dist[at] = dist[at - 1];
            --at;
        }
        fg->cand[at] = k;
        dist[at] = sq;
    }
    fg->ci = fg->n_cand > 1 ? 1 : 0;        // the nearest other than themselves
    fg->manual = false;
    fg->st = FSt::Aim;
    aim_menu(c);
}

void aim_menu(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    const int t = fg->manual ? (b.who[fg->my][fg->mx] ? b.who[fg->my][fg->mx] - 1 : -1) : fg->cand[fg->ci];
    if (t >= 0 || fg->manual) {
        if (t >= 0) focus(t, 2);
        draw_fight(c);
        mark(c, fg->cur);
        if (t >= 0) draw_panel_for(c, t);
        // The box round what's aimed at
        const int ax = t >= 0 ? b.f[t].x : fg->mx, ay = t >= 0 ? b.f[t].y : fg->my;
        if (fg->outline)
            blit4(c, fg->outline, kSq, kSq, kViewX + (ax - b.vx) * kSq, kViewY + (ay - b.vy) * kSq, false, true, kViewX,
                  kViewY, kViewX + 7 * kSq, kViewY + 7 * kSq);
    }
    int sq = 0;
    bool ok = t >= 0 && can_target(t, &sq);
    if (fg->spell && fg->manual && t < 0 && fg->sp_area >= 0) {
        // An area spell may go to an empty square in reach and sight
        int len;
        const bool seen = combat::path(b, fg->t, fg->cur, fg->mx, fg->my, false, &len);
        sq = len / 2;
        ok = seen && combat::tile(b, fg->t, fg->mx, fg->my)[0] != 0xFF &&
             sq <= spells::reach(mrules->tables, fg->spell, spell_power(b.f[fg->cur]));
    }
    char line[24];
    snprintf(line, sizeof line, "%s%d", fw(profile::kRangeIs), sq);
    status_line(c, t >= 0 ? line : "");
    char words[64];
    if (fg->manual) snprintf(words, sizeof words, "%s%s", ok ? fw(profile::kFTarget) : "", fw(profile::kCenterExit));
    else snprintf(words, sizeof words, "%s%s%s", fw(profile::kNextPrevManual), ok ? fw(profile::kFTarget) : "",
                  fw(profile::kCenterExit));
    text::build(menu, fg->manual ? "" : fw(profile::kAimPrompt), words);
    menu.selected = 0;
    show_menu_line(c);
}

void aim_target(pic::Canvas& c, int t)
{
    combat::Fighter& f = fg->b.f[fg->cur];
    status_line(c, "");
    if (fg->b.f[t].team() == f.team()) {
        fg->pending = t;
        fg->st = FSt::AllyAsk;
        text::build(menu, fw(profile::kAttackAlly), rw(Data::kYesNo));
        menu.selected = 1;
        show_menu_line(c);
        return;
    }
    f.target = t;
    // Missiles: the readied ammunition (arrows / quarrels) or the thrown weapon goes
    int ammo = -1;
    const bool shot = !combat::adjacent(fg->b, fg->cur, t) && combat::missile(fg->b, f, &ammo) > 0;
    if (shot) do_attack(c, fg->cur, t, false);
    else melee(c, fg->cur, t);
    if (shot) spend_ammo(fg->cur, ammo);
    say(c, FSt::Turn);
}

void spell_target(pic::Canvas& c, int t);

void aim_tap(int x, int y, pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    if (y < text::kMenuTapTop) {
        // A tap on the field: that square (Manual) / that fighter
        if (x < kViewX || x >= kViewX + 7 * kSq || y < kViewY || y >= kViewY + 7 * kSq) return;
        const int sx = b.vx + (x - kViewX) / kSq, sy = b.vy + (y - kViewY) / kSq;
        const int w = b.who[sy][sx];
        if (fg->manual) {
            fg->mx = sx;
            fg->my = sy;
        } else if (w) {
            for (int k = 0; k < fg->n_cand; ++k)
                if (fg->cand[k] == w - 1) fg->ci = k;
        }
        aim_menu(c);
        return;
    }
    const int t = fg->manual ? (b.who[fg->my][fg->mx] ? b.who[fg->my][fg->mx] - 1 : -1) : fg->cand[fg->ci];
    switch (text::key(menu, text::hit(menu, x / 8))) {
    case 'N': fg->ci = (fg->ci + 1) % (fg->n_cand ? fg->n_cand : 1); break;
    case 'P': fg->ci = (fg->ci + fg->n_cand - 1) % (fg->n_cand ? fg->n_cand : 1); break;
    case 'M':
        fg->manual = true;
        fg->mx = t >= 0 ? b.f[t].x : b.f[fg->cur].x;
        fg->my = t >= 0 ? b.f[t].y : b.f[fg->cur].y;
        break;
    case 'C': break;                    // (the view comes to it)
    case 'T':
        if (fg->spell) {
            spell_target(c, t);
            return;
        }
        if (t >= 0 && can_target(t, nullptr)) {
            aim_target(c, t);
            return;
        }
        break;
    case 'E':
        status_line(c, "");
        if (fg->spell && fg->sp_n > 0) {
            cast_now(c);
            return;
        }
        if (fg->use_item >= 0 && fg->spell) {
            // An item's spell: "Abort Spell? Yes No" (Yes: the use is gone, the turn over)
            fg->st = FSt::AbortAsk;
            text::build(menu, fw(profile::kAbortSpell), rw(Data::kYesNo));
            menu.selected = 1;
            show_menu_line(c);
            return;
        }
        fg->spell = 0;
        draw_fight(c);
        fight_menu(c);
        return;
    default: return;
    }
    aim_menu(c);
}

// ---- Done: "Guard Delay Quit Bandage Speed Exit"
void done_menu(pic::Canvas& c)
{
    const combat::Fighter& f = fg->b.f[fg->cur];
    char words[64];
    words[0] = 0;
    const int w = combat::weapon(f, *names);
    if (w < 0 || (names->type(f.items[w][0x2E]).flags & 0x04)) strcat(words, fw(profile::kFGuard));
    strcat(words, fw(profile::kDelayQuit));
    if (combat::anyone_dying(fg->b)) strcat(words, fw(profile::kFBandage));
    strcat(words, fw(profile::kSpeedExitW));
    fg->st = FSt::DoneMenu;
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

void speed_menu(pic::Canvas& c)
{
    const int speed = vm->get(0x4BFC) & 0xFF;
    char prompt[24], words[32];
    snprintf(prompt, sizeof prompt, "%s%d%s", fw(profile::kGameSpeed), speed, fw(profile::kSpeedClose));
    words[0] = 0;
    if (speed < 9) strcat(words, fw(profile::kFSlower));
    if (speed > 0) strcat(words, fw(profile::kFFaster));
    strcat(words, fw(profile::kFExit));
    fg->st = FSt::Speed;
    text::build(menu, prompt, words);
    menu.selected = 0;
    show_menu_line(c);
}

// ---- The computer's turn
void ai_step(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    const int i = fg->cur;
    combat::Fighter& f = b.f[i];
    // Quick party members bandage a dying friend first
    if (!f.team() && combat::anyone_dying(b)) {
        const int k = combat::bandage(b);
        if (k >= 0) {
            add_page(k, fw(profile::kBandaged));
            f.delay = 0;
            say(c, FSt::Turn);
            fg->cur = i;
            return;
        }
    }
    // Morale (monsters, NPCs), once a turn
    if (!fg->ai_checked) {
        fg->ai_checked = true;
        b.morale_base = vm->get(0x7EC6) & 0xFF;
        const combat::Morale mo = combat::morale(b, fg->t, i);
        if (mo == combat::Morale::Surrender) {
            f.rec[0x195] = party::Unconscious;
            f.rec[0x196] = 0;
            f.size = 0;
            combat::occupancy(b);
            add_page(i, fw(profile::kSurrenders));
            draw_fight(c);
            say(c, FSt::Next);
            return;
        }
        if (mo == combat::Morale::Flee && !f.fleeing) {
            f.fleeing = true;
            add_page(i, fw(profile::kFleesPanic));
            say(c, FSt::Ai);
            return;
        }
        // The computer's magic items (monsters, NPCs and Quick members), then spells
        if (!f.fleeing && f.n_items && combat::standing(b, f.team() ? 0 : 1) && load_magic()) {
            int tg[combat::kMaxFighters], nt = 0;
            const int k = combat::choose_item(b, fg->t, mrules->tables, i, *names, d->prof->fight.spells,
                                              d->prof->fight.n_spells, rng, tg, &nt);
            if (k >= 0) {
                fg->use_item = k;
                fg->use_scroll = false;
                fg->spell = magic::item_spell(f.items[k]);
                fg->sp_n = nt;
                for (int j = 0; j < nt; ++j) fg->sp_t[j] = tg[j];
                draw_fight(c);
                announce_item(c, i, k);
                cast_now(c);
                return;
            }
        }
        if ((f.team() || f.rec[0xF7] >= 0x80) && !f.fleeing && f.can_cast && load_magic()) {
            int tg[combat::kMaxFighters], nt = 0;
            const int sp = combat::choose_spell(b, fg->t, mrules->tables, i, d->prof->fight.spells,
                                                d->prof->fight.n_spells, rng, tg, &nt);
            if (sp) {
                forget_spell(f, sp);
                const int delay = spells::entry(mrules->tables, sp).delay / 3;
                if (delay > 0) {
                    f.spell = sp;
                    f.spell_n = 0;
                    for (int k = 0; k < nt && k < 24; ++k) f.spell_t[f.spell_n++] = static_cast<uint8_t>(tg[k]);
                    f.delay = f.delay > delay ? delay : 1;
                    add_page(i, fw(profile::kBeginsCasting));
                    say(c, FSt::Begin);
                    return;
                }
                fg->spell = sp;
                fg->sp_n = nt;
                for (int k = 0; k < nt; ++k) fg->sp_t[k] = tg[k];
                cast_now(c, true);
                return;
            }
        }
    }
    const combat::Plan p = combat::think(b, fg->t, i, rng);
    switch (p.act) {
    case combat::Act::Attack:
        if (p.missile) do_attack(c, i, p.target, false);
        else melee(c, i, p.target);
        if (p.missile) spend_ammo(i, p.ammo);
        say(c, FSt::Turn);
        return;
    case combat::Act::Step: {
        const int cost = combat::step_cost(b, fg->t, i, p.dir, nullptr, nullptr);
        free_attacks(c, i, p.dir);
        if (f.up()) {
            combat::step(b, i, p.dir);
            f.moves -= cost;
            guard_attacks(c, i);
        }
        if (on_screen(i) && !f.team()) focus(i, 2);
        draw_fight(c);
        if (fg->pages) {
            say(c, f.up() ? FSt::Ai : FSt::Turn);
            return;
        }
        if (!f.up()) {
            end_turn(c);
            return;
        }
        fg->st = FSt::Ai;
        fg->until = millis() + (on_screen(i) ? 150 : 1);
        return;
    }
    case combat::Act::Guard:
        f.guarding = true;
        end_turn(c);
        return;
    case combat::Act::Flee: {
        const bool away = combat::flee(b, fg->t, i, rng);
        add_page(i, fw(away ? profile::kGotAway : profile::kEscapeBlocked));
        draw_fight(c);
        say(c, FSt::Next);
        return;
    }
    default:
        end_turn(c);
        return;
    }
}

// ---- Start and end
void fight_party_icons()
{
    for (int i = 0; i < pt->count; ++i) compose_icon(i, pt->m[i]);
}

bool solid_side(void*, int dir)
{
    const int x = d->gs.x, y = d->gs.y;
    return geo::wall(d->map, x, y, dir) != 0 && geo::door(d->map, x, y, dir) == 0;
}

void fight_start(pic::Canvas& c)
{
    if (!fg) fg = new (std::nothrow) Fight;
    if (!fg || !ml || !pt->count) {
        error(c, "Not enough memory.");
        delete fg;
        fg = nullptr;
        fight_clear_monsters();
        waiting = false;
        handle(vm->resume());
        return;
    }
    anim_stop();
    load_fight_words();
    fg->tile = static_cast<uint8_t(*)[kSq * kSq / 2]>(malloc(40 * sizeof *fg->tile));
    if (!fg->tile || !load_fight_tables()) {
        error(c, "The combat tables can't be read.");
        end_fight_art();
        delete fg;
        fg = nullptr;
        fight_clear_monsters();
        waiting = false;
        handle(vm->resume());
        return;
    }
    combat::Battle& b = fg->b;
    b.~Battle();
    new (&b) combat::Battle();         // (in place: a temporary would take 10 KB of stack)
    b.indoors = vm->get(0x4BE6) != 0;
    if (b.indoors) combat::build_indoors(b, fg->t, d->map, d->gs.x, d->gs.y, rng);
    else combat::build_outdoors(b, fg->t, vm->get(0x4CA1) & 0xFF, rng);
    load_tiles(b.indoors);
    // The fighters: the party, then the monsters
    for (int i = 0; i < pt->count; ++i) {
        party::Character& ch = pt->m[i];
        combat::Fighter& f = b.f[b.n++];
        f.rec = ch.rec;
        f.aff = ch.affects;
        f.n_aff = &ch.n_affects;
        f.items = ch.items;
        f.n_items = ch.n_items;
        f.member = i;
        f.icon = i;
        f.quick = false;
        f.max_aff = party::kMaxAffects;
    }
    b.party_size = pt->count;
    for (int k = 0; k < ml->n && b.n < combat::kMaxFighters; ++k) {
        combat::Monster& m = ml->m[k];
        combat::Fighter& f = b.f[b.n++];
        f.rec = m.rec;
        f.aff = m.aff;
        f.n_aff = &m.n_aff;
        f.items = ml->g[m.group].item;
        f.n_items = ml->g[m.group].n_items;
        f.monster = k;
        f.icon = 8 + m.group;
        f.quick = true;
        f.max_aff = combat::kMonsterAffects;
    }
    b.fx = &d->prof->fight.facts;
    b.names = names;
    b.arrow = d->facts.arrow;
    b.quarrel = d->facts.quarrel;
    b.surprise = vm->get(0x7ECB) & 6;
    b.to_hit_party = static_cast<int8_t>(vm->get(0x7F71));
    b.to_hit_monsters = static_cast<int8_t>(vm->get(0x7F70));
    combat::place(b, fg->t, d->gs.dir & 6, vm->fight_distance(), b.indoors ? solid_side : nullptr, nullptr);
    // Their pictures
    fight_party_icons();
    for (int g = 0; g < ml->groups; ++g) load_monster_icon(8 + g, ml->g[g].icon);
    int w, h;
    fg->cursor = load_pic4("COMSPR.DAX", 0x19, 0, &w, &h);
    fg->skull = load_pic4("COMSPR.DAX", 11 + 0x80, 0, &w, &h);
    fg->outline = load_pic4("COMSPR.DAX", 0x19 + 0x80, 0, &w, &h);
    focus(0, 0xFF);
    screen = Screen::Fight;
    palette_fight(true);
    fg->cur = -1;
    draw_fight(c);
    clear_menu_line(c);
    put(c, fw(profile::kBattleBegins), 0, text::kMenuRow, 10);
    fg->st = FSt::Begin;
    fg->until = millis() + game_delay_ms() * 3;
    if (!fg->until) fg->until = 1;
    Serial.printf("[play] a fight: %d against %d (%s)\n", combat::standing(b, 0), combat::standing(b, 1),
                  b.indoors ? "indoors" : "outdoors");
}

// Back to exploring after the fight (and its treasure)
void fight_back_to_game(pic::Canvas& c)
{
    screen = Screen::Game;
    anim_stop();
    bigpic = -1;
    pic_shown = false;
    head_shown = body_shown = -1;
    if (!vm->get(0x4BE6)) host->picture(0x79, 0xFF);
    draw_frame(c);
    draw_view(c);
    draw_panel(c);
    text::clear(c, text::kTextArea);
    dirty(0, pic::kScreenH);
}

// The effects that end with the fight, the party put back on its feet
void fight_cleanup_party(combat::Result r)
{
    for (int i = 0; i < pt->count; ++i) {
        party::Character& ch = pt->m[i];
        for (uint8_t t : d->prof->fight.ends)
            if (t) rules::remove_affects(ch, t);
        uint8_t* rec = ch.rec;
        if (r == combat::Lost) continue;
        if (rec[0x195] == party::Running) rec[0x195] = party::Okay;
        if (r == combat::Fled) continue;
        if (rec[0x195] == party::Dying) rec[0x195] = party::Unconscious;
        if (rec[0x195] == party::Unconscious && rec[0x1A4] > 0) rec[0x195] = party::Okay;
        rec[0x196] = rec[0x195] == party::Okay || rec[0x195] == party::Animated;
    }
}

void open_treasure(pic::Canvas& c);

// The fight is over: the outcome, experience, treasure
void end_fight(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    fg->out = combat::finish(b, ml ? ml->m : nullptr);
    const combat::Result r = fg->out.result;
    int out = 0;
    for (int i = b.party_size; i < b.n; ++i)
        if (!b.f[i].gone && !b.f[i].up()) ++out;
    vm->set(0x7EC8, static_cast<uint16_t>(out));
    vm->set(0x7EC7, static_cast<uint16_t>(r));
    // The beaten monsters' coins and items go to the treasure
    if (r == combat::Won) {
        for (int m = 0; m < 7; ++m) ground->money[m] += fg->out.money[m];
        if (vm->get(0x7EE3) != 1)
            for (int i = b.party_size; i < b.n; ++i) {
                const combat::Fighter& f = b.f[i];
                if (f.gone || !f.team() || f.status() == party::Okay || f.status() == party::Running) continue;
                for (int k = 0; k < f.n_items && ground->n < items::kMaxGround; ++k) {
                    memcpy(ground->item[ground->n], f.items[k], items::kRecordSize);
                    ground->item[ground->n][0x34] = 0;
                    ++ground->n;
                }
            }
        int total = fg->out.exp + combat::money_exp(ground->money);
        for (int k = 0; k < ground->n; ++k) {
            const int plus = static_cast<int8_t>(ground->item[k][0x32]);
            if (plus > 0) total += 400 * plus;
        }
        fg->share = combat::award(b, total);
    } else {
        fg->share = 0;
        if (r == combat::Fled) ground->clear();
    }
    fight_cleanup_party(r);
    // Left behind (a flight): the fallen stay
    if (r == combat::Fled)
        for (int i = pt->count - 1; i >= 0; --i)
            if (pt->m[i].health() != party::Okay && pt->m[i].health() != party::Animated) leave_party(i);
    for (int i = 0; i < pt->count; ++i) rules::recalc(pt->m[i], *names, d->facts);
    end_fight_art();
    fight_clear_monsters();
    palette_fight(false);
    // The result screen
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    if (r == combat::Lost) {
        put(c, fw(profile::kRejoice), 1, 3, 10);
        clear_menu_line(c);
        put(c, fw(profile::kPressAnyKey), 0, text::kMenuRow, 15);
        fg->st = FSt::Destroyed;
    } else {
        put(c, fw(r == combat::Fled ? profile::kHasFled : profile::kHasWon), 1, 3, 10);
        char t[48];
        snprintf(t, sizeof t, "%s%d", fw(profile::kEachReceives), fg->share);
        put(c, t, 1, 5, 10);
        put(c, fw(profile::kExpPoints), 1, 7, 10);
        clear_menu_line(c);
        put(c, fw(profile::kPressEnter), 0, text::kMenuRow, 15);
        fg->st = FSt::Results;
    }
    fg->until = 0;
    dirty(0, pic::kScreenH);
}

// After the results: the treasure, or straight back to the script
void fight_done(pic::Canvas& c)
{
    const bool lost = fg->st == FSt::Destroyed;
    vm->set(0x7F70, 0);
    vm->set(0x7F71, 0);
    vm->set(0x7F72, 0);
    vm->set(0x7EE3, 0);
    vm->set(0x7EE6, 0);
    delete fg;
    fg = nullptr;
    if (lost) {
        // The party is gone: back to the party menu
        while (pt->count) leave_party(pt->count - 1);
        screen = Screen::PartyMenu;
        draw_party_menu(c);
        return;
    }
    fight_back_to_game(c);
    if (ground->any_money() || ground->n) {
        open_treasure(c);
        return;
    }
    clear_menu_line(c);
    waiting = false;
    handle(vm->resume());
}

// COMBAT with no monsters: the treasure the script set out
void fight_treasure_only(pic::Canvas& c)
{
    if (!ground->any_money() && !ground->n) {
        waiting = false;
        handle(vm->resume());
        return;
    }
    if (!fg) fg = new (std::nothrow) Fight;
    if (!fg) {
        open_treasure(c);
        return;
    }
    load_fight_words();
    int total = combat::money_exp(ground->money);
    for (int k = 0; k < ground->n; ++k) {
        const int plus = static_cast<int8_t>(ground->item[k][0x32]);
        if (plus > 0) total += 400 * plus;
    }
    // (no fighters: the party shares as the games do - the survivors)
    combat::Battle& b = fg->b;
    b.~Battle();
    new (&b) combat::Battle();         // (in place: a temporary would take 10 KB of stack)
    for (int i = 0; i < pt->count; ++i) {
        b.f[i].rec = pt->m[i].rec;
        b.f[i].member = i;
    }
    b.n = b.party_size = pt->count;
    fg->share = combat::award(b, total);
    fg->treasure_only = true;
    screen = Screen::Fight;
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    put(c, fw(profile::kFoundTreasure), 1, 3, 10);
    char t[48];
    snprintf(t, sizeof t, "%s%d", fw(profile::kEachReceives), fg->share);
    put(c, t, 1, 5, 10);
    put(c, fw(profile::kExpPoints), 1, 7, 10);
    clear_menu_line(c);
    put(c, fw(profile::kPressEnter), 0, text::kMenuRow, 15);
    fg->st = FSt::Results;
    dirty(0, pic::kScreenH);
}

// ---- Ticks and taps
void fight_tick(uint32_t now, pic::Canvas& c)
{
    if (!fg || !fg->until || static_cast<int32_t>(now - fg->until) < 0) return;
    fg->until = 0;
    switch (fg->st) {
    case FSt::Begin: begin_round(c); break;
    case FSt::Ai: ai_step(c); break;
    case FSt::RoundEnd: round_end(c); break;
    case FSt::Pages:
        if (fg->at < fg->pages) {
            show_page(c);
            break;
        }
        fg->pages = fg->at = 0;
        clear_panel(c, 10, 21);
        if (fg->cur >= 0) draw_panel_for(c, fg->cur);
        switch (fg->after) {
        case FSt::Next:
            end_turn(c);
            break;
        case FSt::Begin:
            status_line(c, "");
            next_turn(c);                   // (their delay stays: the spell comes later)
            break;
        case FSt::Ai:
            fg->st = FSt::Ai;
            fg->until = 1;
            break;
        case FSt::Move:
            fg->st = FSt::Move;
            draw_fight(c);
            move_prompt(c);
            break;
        case FSt::UseAim:
            begin_targeting(c, fg->spell);
            break;
        default:
            after_attack(c);
            break;
        }
        break;
    default: break;
    }
}


// ---- Casting in a fight
// Cast: "NAME's Spells in Memory" (the list screen); a camp spell: "Camp
// Only Spell"; one with a casting time: "Begins Casting" - it goes off when
// their turn comes round again this round, unless they're hurt first ("lost
// a spell"). Then the targets as the spell's aim says (themselves; one to
// four picked with Aim's Target; an area round a fighter or a square:
// Manual), "Casts a Spell" with "Spell: NAME" on row 23, and what it did:
// "is Blessed", "takes 7 points of damage from Fire", "falls asleep" ...

int spell_power(const combat::Fighter& f)
{
    const uint8_t* r = f.rec;
    const int cl = r[0x109], pa = r[0x10C], ra = r[0x10D], mu = r[0x10E];
    if (cl == 0 && mu == 0 && pa < 9 && ra < 8) return 6;
    if (!fg->spell || !mrules) return cl > mu ? cl : mu;
    auto most = [](int a, int b2) { return a > b2 ? a : b2; };
    switch (mrules->tables.spell_class(fg->spell)) {
    case 0: return most(cl, pa - 8);
    case 1: return most(ra - 7, 0);
    case 2: return most(mu, ra - 8);
    default: return 12;
    }
}

void forget_spell(combat::Fighter& f, int sp)
{
    for (int k = 0; k < magic::kListSize; ++k)
        if (f.rec[magic::kListAt + k] == sp) {
            f.rec[magic::kListAt + k] = 0;
            return;
        }
}

const combat::FightSpell* fight_spell(int sp)
{
    return combat::fight_spell(d->prof->fight.spells, d->prof->fight.n_spells, sp);
}

void fight_cast_menu(pic::Canvas& c)
{
    combat::Fighter& f = fg->b.f[fg->cur];
    if (f.member < 0 || !load_magic()) return;
    uint8_t ids[84];
    const int n = magic::in_memory(pt->m[f.member], mrules->tables, false, ids, 84);
    if (!n) return;
    pt->selected = f.member;
    sl.learning = sl.scrolls = sl.choosing = false;
    sl.casting = true;
    sl.fighting = true;
    build_lines(ids, n);
    screen = Screen::SpellList;
    draw_spells(c);
}

void begin_targeting(pic::Canvas& c, int sp)
{
    if (!load_magic()) {
        end_turn(c);
        return;
    }
    const spells::Entry e = spells::entry(mrules->tables, sp);
    fg->spell = sp;
    fg->sp_n = 0;
    fg->sp_area = -1;
    const int aim = e.aim & 0x0F;
    if (aim == 0) {
        fg->sp_t[fg->sp_n++] = fg->cur;
        cast_now(c);
        return;
    }
    if (e.targets == spells::kParty) {
        // Their whole side
        const bool mine = fg->b.f[fg->cur].team();
        for (int k = 0; k < fg->b.n; ++k)
            if (fg->b.f[k].up() && fg->b.f[k].size && fg->b.f[k].team() == mine) fg->sp_t[fg->sp_n++] = k;
        cast_now(c);
        return;
    }
    if (aim >= 8 && aim <= 14) {
        fg->sp_need = 1;
        fg->sp_area = e.aim & 7;
    } else if (aim == 5) {
        fg->sp_need = 1;
        fg->sp_area = 1;
    } else if (aim == 15) {
        fg->sp_need = 1;
    } else {
        fg->sp_need = (e.aim & 3) + 1;
    }
    start_aim(c);
    // The first shown: the nearest enemy for a spell against them, else the nearest other
    const combat::FightSpell* fs = fight_spell(sp);
    const bool hostile = fs && fs->does != combat::SpellDoes::Ours && fs->does != combat::SpellDoes::Heal &&
                         fs->does != combat::SpellDoes::Haste && fs->does != combat::SpellDoes::Prayer &&
                         fs->does != combat::SpellDoes::Mirror && fs->does != combat::SpellDoes::Affect;
    fg->ci = -1;
    for (int k = 0; k < fg->n_cand && fg->ci < 0; ++k)
        if (fg->cand[k] != fg->cur && (!hostile || fg->b.f[fg->cand[k]].team() != fg->b.f[fg->cur].team())) fg->ci = k;
    for (int k = 0; k < fg->n_cand && fg->ci < 0; ++k)
        if (fg->cand[k] != fg->cur) fg->ci = k;
    if (fg->ci < 0) fg->ci = 0;
    aim_menu(c);
}

void fight_spell_chosen(int sp, pic::Canvas& c)
{
    sl.fighting = false;
    screen = Screen::Fight;
    draw_fight(c);
    if (!sp) {
        fight_menu(c);
        return;
    }
    combat::Fighter& f = fg->b.f[fg->cur];
    const spells::Entry e = spells::entry(mrules->tables, sp);
    if (e.when == 0) {
        fight_menu(c);
        note(c, fw(profile::kCampOnly));
        return;
    }
    const combat::FightSpell* fs = fight_spell(sp);
    if (!fs || fs->does == combat::SpellDoes::NotYet) {
        fight_menu(c);
        error(c, "Not in the engine yet.");
        return;
    }
    const int delay = e.delay / 3;
    if (delay > 0) {
        f.spell = sp;
        f.delay = f.delay > delay ? delay : 1;
        add_page(fg->cur, fw(profile::kBeginsCasting));
        fg->after = FSt::Turn;
        fg->st = FSt::Pages;
        fg->at = 0;
        show_page(c);
        // (the turn ends without its delay going: next_turn after the page)
        fg->after = FSt::Begin;
        return;
    }
    begin_targeting(c, sp);
}

void spell_target(pic::Canvas& c, int t)
{
    combat::Battle& b = fg->b;
    if (t < 0 && !(fg->manual && fg->sp_area >= 0)) return;
    if (t >= 0 && !can_target(t, nullptr) && !(fg->manual && fg->sp_area >= 0)) return;
    const combat::FightSpell* bfs = fight_spell(fg->spell);
    if (bfs && bfs->does == combat::SpellDoes::Bolt) {
        // A bolt: from the target on, away from the caster
        const int x = t >= 0 ? b.f[t].x : fg->mx, y = t >= 0 ? b.f[t].y : fg->my;
        fg->sp_n = combat::bolt_line(b, fg->t, fg->cur, x, y, 7, fg->sp_t, combat::kMaxFighters);
        if (!fg->sp_n) fg->sp_t[fg->sp_n++] = t >= 0 ? t : fg->cur;
        cast_now(c);
        return;
    }
    if (fg->sp_area >= 0) {
        // The area's centre: the fighter or the square
        const int x = t >= 0 ? b.f[t].x : fg->mx, y = t >= 0 ? b.f[t].y : fg->my;
        int in[combat::kMaxFighters];
        const int n = combat::in_area(b, fg->t, x, y, fg->sp_area, in, combat::kMaxFighters);
        fg->sp_n = 0;
        for (int k = 0; k < n; ++k) fg->sp_t[fg->sp_n++] = in[k];
        if (!fg->sp_n) fg->sp_t[fg->sp_n++] = t >= 0 ? t : fg->cur;     // (an empty square: no one)
        cast_now(c);
        return;
    }
    for (int k = 0; k < fg->sp_n; ++k)
        if (fg->sp_t[k] == t) {
            note(c, fw(profile::kAlreadyTargeted));
            return;
        }
    fg->sp_t[fg->sp_n++] = t;
    if (fg->sp_n >= fg->sp_need || fg->sp_n >= combat::kMaxFighters) {
        cast_now(c);
        return;
    }
    aim_menu(c);
}

void spell_pages(int caster, const combat::SpellLine* l, int n, const combat::FightSpell& fs)
{
    char t[48];
    for (int k = 0; k < n; ++k) {
        const combat::Fighter& f = fg->b.f[l[k].who];
        switch (l[k].did) {
        case combat::Did::Word:
            ow(fs.word, t, sizeof t);
            add_page(l[k].who, t);
            break;
        case combat::Did::Damage: {
            const char* from = "";
            const int kind = fs.kind & 0xF7;
            if (kind == 0x01) from = fw(profile::kFromFire);
            else if (kind == 0x02) from = fw(profile::kFromCold);
            else if (kind == 0x04) from = fw(profile::kFromElec);
            else if (kind == 0x10) from = fw(profile::kFromAcid);
            else if (fs.kind == 0x08) from = fw(profile::kFromMagic);
            if (l[k].amount == 1) snprintf(t, sizeof t, "%s%s", fw(profile::kTakes1), from);
            else snprintf(t, sizeof t, "%s%d%s%s", fw(profile::kTakes), l[k].amount, fw(profile::kPointsOfDamage), from);
            add_page(l[k].who, t);
            break;
        }
        case combat::Did::Unaffected: add_page(l[k].who, fw(profile::kIsUnaffectedS)); break;
        case combat::Did::Word2:
            ow(fs.word2, t, sizeof t);
            add_page(l[k].who, t);
            break;
        case combat::Did::Misses: add_page(caster, fw(profile::kMisses), nullptr, l[k].who); break;
        case combat::Did::Healed: {
            char w2[24];
            cw(f.hp() >= f.hp_max() ? profile::kFullyHealed : profile::kPartlyHealed, w2, sizeof w2);
            add_page(l[k].who, w2);
            break;
        }
        case combat::Did::Down: {
            const combat::Down dn = combat::down_state(f);
            if (dn == combat::Down::Dead) add_page(l[k].who, fw(profile::kIsKilled));
            else if (dn == combat::Down::Dying) add_page(l[k].who, fw(profile::kGoesDownS), fw(profile::kIsDying));
            else add_page(l[k].who, fw(profile::kGoesDownS));
            break;
        }
        }
    }
}

void cast_now(pic::Canvas& c, bool forgotten)
{
    combat::Battle& b = fg->b;
    const int i = fg->cur;
    combat::Fighter& f = b.f[i];
    const int sp = fg->spell;
    fg->spell = 0;
    const combat::FightSpell* fs = fight_spell(sp);
    const bool from_item = fg->use_item >= 0;
    if (!from_item) status_line(c, "");
    if (!fs) {
        end_turn(c);
        return;
    }
    int pw = 0;
    draw_fight(c);
    if (from_item) {
        pw = combat::power_of(f.rec, mrules->tables, sp, true);
    } else {
        // The spell leaves their memory; "Casts a Spell", "Spell: NAME"
        if (!forgotten) forget_spell(f, sp);
        char t[40];
        snprintf(t, sizeof t, "%s%s", fw(profile::kSpellColon), spell_name(sp));
        status_line(c, t);
        add_page(i, fw(profile::kCastsASpell));
    }
    draw_icon(c, i, true);
    combat::SpellLine lines[24];
    const int n = combat::cast(b, mrules->tables, i, sp, *fs, fg->sp_t, fg->sp_n, rng, lines, 24, pw);
    spell_pages(i, lines, n, *fs);
    if (from_item) spend_item(i, sp);
    f.attacks[0] = f.attacks[1] = 0;
    say(c, FSt::Next);
}

// Turn: "NAME turns undead...", then each "is turned" / "Is destroyed", or
// "Nothing Happens..."
void fight_turn_undead(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    const int i = fg->cur;
    add_page(i, fw(profile::kTurnsUndead));
    int out[16];
    const int n = combat::turn_undead(b, fg->t, i, rng, out, 16);
    for (int k = 0; k < n; ++k) {
        if (out[k] >= 1000) add_page(out[k] - 1000, fw(profile::kIsDestroyed));
        else add_page(out[k], fw(profile::kIsTurned));
    }
    if (!n) add_page(-1, fw(profile::kNothingHappens));
    draw_fight(c);
    say(c, FSt::Next);
}

void fight_tap(int x, int y, pic::Canvas& c)
{
    if (!fg) return;
    const int col = x / 8;
    if (note_until) {
        redraw_menu(c);
        if (fg->st == FSt::Move) move_prompt(c);
        return;
    }
    switch (fg->st) {
    case FSt::Pages:
        if (fg->until) fg->until = 1;   // a tap moves on
        return;
    case FSt::Results:
    case FSt::Destroyed:
        if (fg->treasure_only) {
            fg->treasure_only = false;
            delete fg;
            fg = nullptr;
            fight_back_to_game(c);
            open_treasure(c);
            return;
        }
        fight_done(c);
        return;
    case FSt::Menu: {
        if (y < text::kMenuTapTop) return;
        combat::Fighter& f = fg->b.f[fg->cur];
        switch (text::key(menu, text::hit(menu, col))) {
        case 'M': start_move(c); break;
        case 'V':
            if (f.member >= 0) {
                pt->selected = f.member;
                palette_fight(false);
                view_character(c);
            }
            break;
        case 'A': start_aim(c); break;
        case 'Q':
            f.quick = true;
            fg->st = FSt::Ai;
            fg->until = millis() + 200;
            clear_menu_line(c);
            break;
        case 'D': done_menu(c); break;
        case 0: break;
        case 'C': fight_cast_menu(c); break;
        case 'T': fight_turn_undead(c); break;
        case 'U':
            // The items screen (Use there; Exit comes back here)
            if (f.member >= 0 && pt->m[f.member].n_items) {
                pt->selected = f.member;
                palette_fight(false);
                view_from = Screen::Fight;
                items_direct = true;
                open_items(c);
            }
            break;
        default: break;
        }
        return;
    }
    case FSt::Move: {
        if (y >= text::kMenuTapTop) {
            end_move(c);
            return;
        }
        if (x < kViewX || x >= kViewX + 7 * kSq || y < kViewY || y >= kViewY + 7 * kSq) return;
        const combat::Fighter& f = fg->b.f[fg->cur];
        const int sx = fg->b.vx + (x - kViewX) / kSq, sy = fg->b.vy + (y - kViewY) / kSq;
        // The direction from the fighter's nearest square to the tapped one
        int fx = f.x, fy = f.y;
        if (f.size == 3 || f.size == 4) fx = sx > f.x ? f.x + 1 : f.x;
        if (f.size == 2 || f.size == 4) fy = sy > f.y ? f.y + 1 : f.y;
        const int dir = combat::direction(fx, fy, sx, sy);
        if (dir < 8) try_step(c, dir);
        return;
    }
    case FSt::Aim: aim_tap(x, y, c); return;
    case FSt::DoneMenu: {
        if (y < text::kMenuTapTop) return;
        combat::Fighter& f = fg->b.f[fg->cur];
        switch (text::key(menu, text::hit(menu, col))) {
        case 'G':
            f.guarding = true;
            add_page(fg->cur, fw(profile::kGuarding));
            f.delay = 0;
            say(c, FSt::Turn);
            break;
        case 'D':
            f.delay = 1;
            status_line(c, "");
            next_turn(c);
            break;
        case 'Q': end_turn(c); break;
        case 'B': {
            const int k = combat::bandage(fg->b);
            if (k >= 0) {
                add_page(k, fw(profile::kBandaged));
                f.delay = 0;
                say(c, FSt::Turn);
            }
            break;
        }
        case 'S': speed_menu(c); break;
        case 'E': fight_menu(c); break;
        default: break;
        }
        return;
    }
    case FSt::Speed: {
        if (y < text::kMenuTapTop) return;
        int speed = vm->get(0x4BFC) & 0xFF;
        const uint16_t high = static_cast<uint16_t>(vm->get(0x4BFC) & 0xFF00);
        switch (text::key(menu, text::hit(menu, col))) {
        case 'S': if (speed < 9) ++speed; break;
        case 'F': if (speed > 0) --speed; break;
        case 'E': done_menu(c); return;
        default: return;
        }
        vm->set(0x4BFC, static_cast<uint16_t>(high | speed));
        speed_menu(c);
        return;
    }
    case FSt::AllyAsk: {
        if (y < text::kMenuTapTop) return;
        const char k = text::key(menu, text::hit(menu, col));
        if (k != 'Y' && k != 'N') return;
        if (k == 'Y' && fg->pending >= 0) {
            vm->set(0x7F33, 1);
            // NPCs on the party's side turn on it
            for (int i = 0; i < fg->b.n; ++i) {
                combat::Fighter& o = fg->b.f[i];
                if (o.rec[0xF7] >= 0x80 && o.status() == party::Okay && !o.team()) {
                    o.rec[0x197] = 1;
                    o.target = -1;
                }
            }
            fg->b.f[fg->cur].target = fg->pending;
            do_attack(c, fg->cur, fg->pending, false);
            fg->pending = -1;
            say(c, FSt::Turn);
            return;
        }
        fg->pending = -1;
        fight_menu(c);
        return;
    }
    case FSt::AbortAsk: {
        if (y < text::kMenuTapTop) return;
        const char k = text::key(menu, text::hit(menu, col));
        if (k == 'N') {
            aim_menu(c);
            return;
        }
        if (k != 'Y') return;
        spend_item(fg->cur, fg->spell);
        fg->spell = 0;
        add_page(fg->cur, fw(profile::kSpellAborted));
        draw_fight(c);
        say(c, FSt::Next);
        return;
    }
    case FSt::FleeAsk: {
        if (y < text::kMenuTapTop) return;
        const char k = text::key(menu, text::hit(menu, col));
        if (k == 'N') {
            fight_menu(c);
            return;
        }
        if (k != 'Y') return;
        const int i = fg->cur;
        const bool away = combat::flee(fg->b, fg->t, i, rng);
        add_page(i, fw(away ? profile::kGotAway : profile::kEscapeBlocked));
        draw_fight(c);
        say(c, FSt::Turn);
        return;
    }
    default: return;
    }
}

// Esc in a fight: Move undoes the move, menus go back
bool fight_back(pic::Canvas& c)
{
    if (!fg) return false;
    switch (fg->st) {
    case FSt::Move: {
        combat::Battle& b = fg->b;
        combat::Fighter& f = b.f[fg->cur];
        const int x = f.x, y = f.y;
        f.x = fg->move_x;
        f.y = fg->move_y;
        combat::occupancy(b);
        bool clear = true;
        int sx[4], sy[4];
        const int n = combat::squares(f.size, sx, sy);
        for (int k = 0; k < n; ++k)
            if (b.who[f.y + sy[k]][f.x + sx[k]] != fg->cur + 1) clear = false;
        if (!clear) {
            f.x = x;
            f.y = y;
            combat::occupancy(b);
            end_turn(c);
            return true;
        }
        f.moves = fg->move_left;
        focus(fg->cur, 2);
        draw_fight(c);
        fight_menu(c);
        return true;
    }
    case FSt::Aim:
        status_line(c, "");
        draw_fight(c);
        fight_menu(c);
        return true;
    case FSt::DoneMenu: fight_menu(c); return true;
    case FSt::Speed: done_menu(c); return true;
    case FSt::AllyAsk:
    case FSt::FleeAsk: fight_menu(c); return true;
    case FSt::AbortAsk: aim_menu(c); return true;
    default: return true;               // (no leaving a fight)
    }
}

// The pad in a fight: steps (Move) - 8 north, 7 / 9 north-west / north-east,
// 4 / 6 west / east, 2 south
bool fight_act(Act a, pic::Canvas& c)
{
    if (!fg || fg->st != FSt::Move) return false;
    int dir = 8;
    switch (a) {
    case Act::Forward: dir = 0; break;
    case Act::TurnLeft: dir = 7; break;
    case Act::TurnRight: dir = 1; break;
    case Act::StepLeft: dir = 6; break;
    case Act::StepRight: dir = 2; break;
    case Act::TurnAround: dir = 4; break;
    default: return false;
    }
    try_step(c, dir);
    return true;
}

// After View Character in a fight
void fight_after_view(pic::Canvas& c)
{
    if (!fg) return;
    screen = Screen::Fight;
    palette_fight(true);
    combat::Fighter& f = fg->b.f[fg->cur];
    if (f.member >= 0) {
        party::Character& ch = pt->m[f.member];
        rules::recalc(ch, *names, d->facts);
        f.n_items = ch.n_items;
        compose_icon(f.member, ch);
    }
    draw_fight(c);
    fight_menu(c);
}

// ---- The treasure: "View Take Pool Share Exit" over the exploring screen
// (the shop's screen: Take - "Take: Money Items Exit" when both lie there -,
// Pool, Share as in a shop; the items: "Items: " and Take, newest first;
// leaving with treasure left: "There is still treasure left.  Do you want to
// go back and claim your treasure?" Yes No - what's left is lost)
bool treasure = false;                  // the shop screen is the treasure's
bool treasure_on() { return treasure; }
void treasure_off() { treasure = false; }

// A treasure word (GAME.OVR, read when shown)
const char* tw(int i)
{
    static char buf[4][48];
    static int k = 0;
    char* out = buf[k++ & 3];
    out[0] = 0;
    const uint32_t at = d->prof->fight.words[i];
    fs::File f;
    if (!at || !open_file(d->prof->overlay, f)) return out;
    library::FileSource src(f);
    text::read_pascal(src, at, out, 48);
    f.close();
    return out;
}

void treasure_take(pic::Canvas& c)
{
    if (ground->any_money() && ground->n) {
        char prompt[12], words[24];
        snprintf(prompt, sizeof prompt, "%s", tw(profile::kTakeColon));
        snprintf(words, sizeof words, "%s", tw(profile::kMoneyItems));
        text::build(menu, prompt, words);
        menu.selected = 0;
        show_menu_line(c);
        return;
    }
    if (ground->any_money()) open_take(c);
    else if (ground->n) open_loot(c);
}

void draw_loot(pic::Canvas& c)
{
    c.clear(0);
    layout::outer(c, d->tables, d->frame_tiles);
    plist.row0 = 1;
    plist.row1 = 22;
    plist.col0 = 1;
    plist.n = ground->n;
    char prompt[12], what[8];
    snprintf(prompt, sizeof prompt, "%s", tw(profile::kItemsColon));
    snprintf(what, sizeof what, "%s", tw(profile::kTakeW));
    draw_list(c, prompt, what);
}

void open_loot(pic::Canvas& c)
{
    screen = Screen::Loot;
    plist = PickList{};
    draw_loot(c);
}

void take_loot(int i, pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    if (!ch || i < 0 || i >= ground->n) return;
    const int at = ground->n - 1 - i;
    if (rules::too_heavy(*ch, ground->item[at], *names, d->facts)) {
        note(c, d->w_over);
        return;
    }
    rules::add_item(*ch, ground->item[at]);
    rules::recalc(*ch, *names, d->facts);
    for (int k = at; k + 1 < ground->n; ++k) memcpy(ground->item[k], ground->item[k + 1], items::kRecordSize);
    --ground->n;
    if (!ground->n) {
        screen = Screen::Shop;
        draw_shop(c);
        return;
    }
    draw_loot(c);
}

void treasure_menu(pic::Canvas& c)
{
    char words[48];
    char a[32], b2[16];
    if (ground->any_money() || ground->n) {
        snprintf(a, sizeof a, "%s", tw(ground->any_money() ? profile::kViewTakePoolShare : profile::kViewTakePool));
        snprintf(b2, sizeof b2, "%s", tw(profile::kExitSp));
        snprintf(words, sizeof words, "%s%s", a, b2);
    } else {
        snprintf(words, sizeof words, "%s", tw(profile::kViewPoolExit));
    }
    text::build(menu, "", words);
    menu.selected = 0;
    show_menu_line(c);
}

void open_treasure(pic::Canvas& c)
{
    treasure = true;
    temple = false;
    screen = Screen::Shop;
    text::clear(c, text::kTextArea);
    dirty_rows(17, 22);
    if (pt->count) draw_party(c, 17);
    treasure_menu(c);
}

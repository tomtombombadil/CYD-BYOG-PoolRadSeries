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

// The k-th loaded monster's own record (LOAD CHARACTER past the party: the
// scripts can change one before the fight - the Thieves' Guild hideout makes
// four of the loaded thieves fight on the party's side); nullptr past them
uint8_t* fight_monster_record(int k)
{
    return ml && k >= 0 && k < ml->n ? ml->m[k].rec : nullptr;
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
    ContinueAsk, Next, UseAim, AbortAsk, RoundRest, Clouds, Resume, FlameAsk, FlameAbort
};

// A page's pictures: a missile or spell flying, a burst / sparkles on a
// fighter (the games' magic hits), a fallen one's skull blinking; each with
// the sound it starts with (a sound alone: kind None)
struct Fx {
    // Redraw: the field again (a cloud laid); Bolt: a flight to (x1, y1), then lightning along the
    // fight's bolt segments
    enum Kind : uint8_t { None, Fly, Burst, Skull, Redraw, Bolt } kind = None;
    uint8_t sound = 0;
    combat::Flight fl;                  // Fly / Burst: the pictures, their cycle, ms a step
    uint8_t loops = 1;                  // Burst: times round the cycle
    int8_t  who = -1;                   // Burst / Skull: the fighter
    uint8_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;     // Fly: from, to (field squares)
};

constexpr int kPages = 24;            // a turn's messages at most (a fireball on a crowd: the rest dropped)

struct Page {
    char    line[5][48];                // (the longest: "takes 100 points of damage from Electricity")
    uint8_t colour[5];
    int     lines = 0;
    uint8_t sound = 0;                  // the game's sound effect as it shows (0: none)
    Fx      fx[2];                      // then these, one after the other; then the page's wait
};

constexpr int kTileBytes = kSq * kSq / 2;
constexpr int kTiles = 40;

// A fight's state in pieces - the battle, this, each field picture a block
// of its own - so a heap broken up by the area's pictures and a big monster
// load still finds room (Tom, 2026-10-10: the Thieves' Guild ambush, 17
// monsters, ran out on the 2.8")
struct Fight {
    combat::Battle& b;                  // (its own block: make_fight)
    explicit Fight(combat::Battle& battle) : b(battle) {}
    ~Fight()
    {
        for (uint8_t*& t : tile) {
            free(t);
            t = nullptr;
        }
        delete &b;
    }
    Fight(const Fight&) = delete;
    Fight& operator=(const Fight&) = delete;
    combat::Tables t;
    uint8_t* tile[kTiles] = {};         // the field's 40 pictures (each kTileBytes)
    Pic4     icon[8 + combat::kMaxGroups];
    uint8_t* cursor = nullptr;          // COMSPR 0x19 (ready): the box under the one whose turn it is
    uint8_t* skull = nullptr;           // COMSPR 11 (attack)
    uint8_t* outline = nullptr;         // COMSPR 0x19 (attack): the box round a target being aimed at
    char     w[profile::kFightWords][40];
    FSt      st = FSt::Begin;
    FSt      after = FSt::Turn;         // where the pages go on to
    int      cur = -1;
    uint32_t until = 0;
    Page     page[kPages];
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
    int      pick_budget = -1;          // spell aim 5: picked one by one against this (-1: not that kind)
    bool     pick_size = false;         // ... costs by size (Faerie Fire), else by Hit Dice
    bool     page_dropped = false;      // the last add_page found no room (its pictures / sound aren't set)
    // Thrown weapons used up (a dagger, a spear, darts): onto the treasure after the fight
    uint8_t  thrown[6][items::kRecordSize];
    int      n_thrown = 0;
    bool     ai_checked = false;        // this turn's morale and spell choice done
    bool     fx_done = false;           // this turn's start effects (turn_effects) done
    bool     continuing = false;        // "Continue Battle": Yes - the party's turns with no enemies left
    int      use_item = -1;             // Use: the item the spell comes from (its charge goes)
    bool     use_scroll = false;        // ... a scroll
    combat::Outcome out;
    int      share = 0;
    bool     treasure_only = false;
    // The page's pictures playing (fx_at: the one; 2 = done, the page waits)
    int      fx_at = 2;
    bool     fx_on = false, fx_redraw = false;  // one is running; a skull to clear at the page's end
    Fx       fx;
    combat::FlightPath path;
    uint8_t* fx_pic[2] = {};            // its ready / attack pictures (COMSPR)
    int      fx_step = 0;
    int      bolt_at = -1;              // a Bolt picture: the segment flying (-1: the flight to the square first)
    uint8_t  under[kSq * kSq];          // what the flying picture covers
    int      under_x = 0, under_y = 0;
    bool     has_under = false;
    int      aim_x = -1, aim_y = -1;    // where a spell was aimed (an area's centre)
};
Fight* fg = nullptr;

// A new fight's state (nullptr: no memory)
Fight* make_fight()
{
    combat::Battle* b = new (std::nothrow) combat::Battle;
    if (!b) return nullptr;
    Fight* f = new (std::nothrow) Fight(*b);
    if (!f) delete b;
    return f;
}

// The field's pictures' blocks (false: no memory - those got are freed)
bool alloc_tiles()
{
    for (uint8_t*& t : fg->tile)
        if (!t && !(t = static_cast<uint8_t*>(malloc(kTileBytes)))) {
            for (uint8_t*& u : fg->tile) {
                free(u);
                u = nullptr;
            }
            return false;
        }
    return true;
}

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
                if (r.read(fg->tile[at + k], kTileBytes) != kTileBytes) break;
    }
    f.close();
}

// The field's pictures: DUNGCOM / WILDCOM (0-33), RANDCOM (34-39)
void load_tiles(bool indoors)
{
    for (uint8_t* t : fg->tile)
        if (t) memset(t, 0, kTileBytes);
    load_frames(indoors ? "DUNGCOM.DAX" : "WILDCOM.DAX", 0, indoors ? 25 : 34);
    load_frames("RANDCOM.DAX", 34, 6);
}

// A party member's icon from its record (head 0x141, body 0x142, size
// 0x144, colours 0x145-0x14A; alter_icon_facts.md 1.6): CBODY / CHEAD
// blocks (+ 0x40 large, + 0x80 the attack frame), the head over the
// body's top rows - where both have a colour the two colour numbers OR'd,
// as the program does -, then each pixel recoloured once from its own
// colour: default d -> the byte's low nibble, d + 8 -> its high
void build_icon(const uint8_t* rec, Pic4& ic)
{
    free_pic(ic);
    const int head = rec[0x141], body = rec[0x142];
    const bool small = rec[0x144] == 1;
    static const uint8_t kDefault[6] = {1, 2, 3, 4, 6, 7};
    uint8_t map[16];
    for (int i = 0; i < 16; ++i) map[i] = static_cast<uint8_t>(i);
    for (int i = 0; i < 6; ++i) {
        map[kDefault[i]] = rec[0x145 + i] & 15;
        map[kDefault[i] + 8] = rec[0x145 + i] >> 4;
    }
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
                    if (v) set_nib(b, kSq, x, y, v | nib(b, kSq, x, y));
                }
        free(h);
        for (int y = 0; y < kSq; ++y)
            for (int x = 0; x < kSq; ++x) set_nib(b, kSq, x, y, map[nib(b, kSq, x, y)]);
        ic.px[fr] = b;
        ic.w = ic.h = kSq;
    }
}

// A party member's icon: the head over the body's top rows, recoloured
void load_monster_icon(int slot, int block);

void compose_icon(int slot, const party::Character& ch)
{
    // An NPC from the monsters' files (ADD NPC: computer-run, its MON block
    // at 0x126): its own picture. (A player character's 0x126 is a random
    // number and head / body 0 are ordinary choices - v0.40.3: the GOG
    // sample party fought without icons.)
    if (ch.rec[0xF7] >= 0x80 && ch.rec[0x126] && !ch.rec[0x141] && !ch.rec[0x142]) {
        load_monster_icon(slot, ch.rec[0x126]);
        if (fg->icon[slot].px[0]) return;
    }
    build_icon(ch.rec, fg->icon[slot]);
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
    for (uint8_t*& t : fg->tile) {
        free(t);
        t = nullptr;
    }
    for (Pic4& p : fg->icon) free_pic(p);
    free(fg->cursor);
    free(fg->skull);
    free(fg->outline);
    fg->cursor = fg->skull = fg->outline = nullptr;
    free(fg->fx_pic[0]);
    free(fg->fx_pic[1]);
    fg->fx_pic[0] = fg->fx_pic[1] = nullptr;
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
    if (pic_no < 0 || pic_no >= kTiles || !fg->tile[pic_no]) c.fill(sx, sy, kSq, kSq, 0);
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
    fg->page_dropped = fg->pages >= kPages;
    if (fg->page_dropped) return;
    Page& p = fg->page[fg->pages++];
    p.lines = 0;
    p.sound = 0;
    p.fx[0] = p.fx[1] = Fx{};
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

// The sound the last page queued makes as it shows
void page_sound(int id)
{
    if (fg->pages > 0 && !fg->page_dropped) fg->page[fg->pages - 1].sound = static_cast<uint8_t>(id);
}

// ---- The pages' pictures (facts: the Project's combat_setup_facts.md 6.5 /
// 6.6 and sound_facts.md). A flight: the picture steps 8 pixels at a time
// toward the target, stopping a step short, `delay` ms a step, drawn over
// the field and the field put back after each step; a burst: the 4
// pictures on the fighter's square 70 ms each (sparkles: as many rounds as
// the game speed + 1); the fallen: their squares blink skull / box 9 times,
// 10 ms each, and the skull stays until the page ends.
constexpr int kViewW = 7 * kSq;

void fx_free()
{
    free(fg->fx_pic[0]);
    free(fg->fx_pic[1]);
    fg->fx_pic[0] = fg->fx_pic[1] = nullptr;
}

bool fx_load(int pic)
{
    fx_free();
    int w = 0, h = 0, w2 = 0, h2 = 0;
    fg->fx_pic[0] = load_pic4("COMSPR.DAX", pic, 0, &w, &h);
    fg->fx_pic[1] = load_pic4("COMSPR.DAX", pic + 0x80, 0, &w2, &h2);
    if (!fg->fx_pic[0] || w != kSq || h != kSq || (fg->fx_pic[1] && (w2 != kSq || h2 != kSq))) {
        Serial.printf("[fight] COMSPR %d: no 24 x 24 picture (%d x %d)\n", pic, w, h);
        fx_free();
        return false;
    }
    return true;
}

// The field under a picture at (x, y): kept / put back
void under_put(pic::Canvas& c)
{
    if (!fg->has_under) return;
    fg->has_under = false;
    for (int j = 0; j < kSq; ++j) {
        const int y = fg->under_y + j;
        if (y < kViewY || y >= kViewY + kViewW) continue;
        for (int i = 0; i < kSq; ++i) {
            const int x = fg->under_x + i;
            if (x >= kViewX && x < kViewX + kViewW) c.px[y * c.w + x] = fg->under[j * kSq + i];
        }
    }
    dirty(fg->under_y, fg->under_y + kSq);
}

void under_save(pic::Canvas& c, int x, int y)
{
    fg->under_x = x;
    fg->under_y = y;
    fg->has_under = true;
    for (int j = 0; j < kSq; ++j)
        for (int i = 0; i < kSq; ++i) {
            const int xx = x + i, yy = y + j;
            if (xx >= kViewX && xx < kViewX + kViewW && yy >= kViewY && yy < kViewY + kViewW)
                fg->under[j * kSq + i] = c.px[yy * c.w + xx];
        }
}

// One of the cycle's pictures (bit 0: the attack picture, bit 1 mirrored)
void fx_draw(pic::Canvas& c, int x, int y, uint8_t slot)
{
    const uint8_t* p = (slot & 1) && fg->fx_pic[1] ? fg->fx_pic[1] : fg->fx_pic[0];
    blit4(c, p, kSq, kSq, x, y, (slot & 2) != 0, true, kViewX, kViewY, kViewX + kViewW, kViewY + kViewW);
    dirty(y, y + kSq);
}

int sq_x(int fx) { return kViewX + (fx - fg->b.vx) * kSq; }
int sq_y(int fy) { return kViewY + (fy - fg->b.vy) * kSq; }

// One step of the running picture; false when it's over
bool fx_step(pic::Canvas& c)
{
    Fx& e = fg->fx;
    const int k = fg->fx_step++;
    switch (e.kind) {
    case Fx::Bolt:
    case Fx::Fly: {
        under_put(c);
        while (!combat::flight_step(fg->path)) {
            // A bolt: on along its segments (the lightning picture, COMSPR 6, 50 ms a step)
            if (e.kind != Fx::Bolt || fg->bolt_at + 1 >= fg->b.n_bolt) return false;
            if (fg->bolt_at < 0) {
                if (!fx_load(6)) return false;
                e.fl = combat::spell_flight(6, 50);
            }
            const combat::BoltSeg& sg = fg->b.bolt[++fg->bolt_at];
            combat::flight_begin(fg->path, sg.x0, sg.y0, sg.x1, sg.y1);
        }
        const int x = kViewX + (fg->path.x - fg->b.vx * 3) * 8, y = kViewY + (fg->path.y - fg->b.vy * 3) * 8;
        under_save(c, x, y);
        fx_draw(c, x, y, e.fl.slot[e.fl.frames ? k % e.fl.frames : 0]);
        fg->until = millis() + (e.fl.delay ? e.fl.delay : 1);
        return true;
    }
    case Fx::Burst: {
        under_put(c);
        if (k >= e.loops * 4) return false;
        const combat::Fighter& f = fg->b.f[e.who];
        const int x = sq_x(f.x), y = sq_y(f.y);
        under_save(c, x, y);
        fx_draw(c, x, y, e.fl.slot[k & 3]);
        fg->until = millis() + 70;
        return true;
    }
    case Fx::Skull: {
        if (k >= 9) return false;
        const combat::Fighter& f = fg->b.f[e.who];
        const uint8_t* p = k & 1 ? fg->cursor : fg->skull;
        int sx[4], sy[4];
        const int n = combat::squares(f.size ? f.size : 1, sx, sy);
        for (int q = 0; q < n && p; ++q) {
            const int x = sq_x(f.x + sx[q]), y = sq_y(f.y + sy[q]);
            blit4(c, p, kSq, kSq, x, y, false, true, kViewX, kViewY, kViewX + kViewW, kViewY + kViewW);
            dirty(y, y + kSq);
        }
        fg->fx_redraw = true;
        fg->until = millis() + 10;
        return true;
    }
    default: return false;
    }
}

// Starts a page's picture (its sound first); false: nothing to show
bool fx_begin(pic::Canvas& c, const Fx& e)
{
    if (e.sound) sfx(e.sound);
    if (e.kind == Fx::None) return false;
    if (e.kind == Fx::Redraw) {
        draw_field(c);
        if (fg->cur >= 0 && fg->b.f[fg->cur].up()) mark(c, fg->cur);
        return false;
    }
    fg->fx = e;
    fg->fx_step = 0;
    fg->has_under = false;
    switch (e.kind) {
    case Fx::Fly:
    case Fx::Bolt:
        if (!fx_load(e.fl.pic)) return false;
        fg->bolt_at = -1;
        combat::flight_begin(fg->path, e.x0, e.y0, e.x1, e.y1);
        break;
    case Fx::Burst:
        if (e.who < 0 || !fx_load(e.fl.pic)) return false;
        if (!on_screen(e.who)) {
            focus(e.who, 3);
            draw_field(c);
            if (fg->cur >= 0) mark(c, fg->cur);
        }
        break;
    case Fx::Skull:
        if (e.who < 0 || !fg->skull) return false;
        if (!on_screen(e.who)) {
            focus(e.who, 3);
            draw_field(c);
            if (fg->cur >= 0) mark(c, fg->cur);
        }
        break;
    default: return false;
    }
    if (!fx_step(c)) {
        fx_free();
        return false;
    }
    fg->fx_on = true;
    return true;
}

// The page's next picture, or its wait when they're all done
void page_fx(pic::Canvas& c)
{
    const Page& p = fg->page[fg->at - 1];
    while (fg->fx_at < 2) {
        if (fx_begin(c, p.fx[fg->fx_at])) return;
        ++fg->fx_at;
    }
    fg->until = millis() + game_delay_ms() * 2;
    if (!fg->until) fg->until = 1;
}

// The running picture done (or cut short by a tap): the field put back
void fx_end(pic::Canvas& c)
{
    under_put(c);
    fx_free();
    fg->fx_on = false;
    ++fg->fx_at;
}

// A page over: a skull left on the field goes
void page_over(pic::Canvas& c)
{
    if (!fg->fx_redraw) return;
    fg->fx_redraw = false;
    draw_field(c);
    if (fg->cur >= 0 && fg->b.f[fg->cur].up()) mark(c, fg->cur);
}

bool log_pages = false;                  // (the playsim's PAGELOG: each page's lines printed)

void show_page(pic::Canvas& c)
{
    page_over(c);
    const Page& p = fg->page[fg->at++];
    if (log_pages) Serial.printf("[page] %s %s %s\n", p.lines > 0 ? p.line[0] : "", p.lines > 1 ? p.line[1] : "",
                                 p.lines > 2 ? p.line[2] : "");
    if (p.sound) sfx(p.sound);
    clear_panel(c, 10, 21);
    int row = 10;
    for (int k = 0; k < p.lines; ++k) row = panel_text(c, p.line[k], row, p.colour[k]);
    fg->fx_at = 0;
    page_fx(c);
}

// A page's pictures, as the games show them
Fx fly_fx(int from, int to, const combat::Flight& fl)
{
    Fx e;
    e.kind = Fx::Fly;
    e.fl = fl;
    e.x0 = static_cast<uint8_t>(fg->b.f[from].x);
    e.y0 = static_cast<uint8_t>(fg->b.f[from].y);
    e.x1 = static_cast<uint8_t>(fg->b.f[to].x);
    e.y1 = static_cast<uint8_t>(fg->b.f[to].y);
    return e;
}

// A magic hit on fighter `who`: damage - the burst (sound 3), once; an
// effect - sparkles (sound 4), the game speed + 1 times round
Fx hit_fx(int who, bool sparkles)
{
    Fx e;
    e.kind = Fx::Burst;
    e.who = static_cast<int8_t>(who);
    e.fl = combat::spell_flight(sparkles ? 9 : 10, 70);
    e.sound = sparkles ? sound::kMagicStars : sound::kMagicHit;
    int speed = vm ? vm->get(0x4BFC) & 0xFF : 0;
    if (speed > 9) speed = 9;
    e.loops = static_cast<uint8_t>(sparkles ? speed + 1 : 1);
    return e;
}

Fx skull_fx(int who)
{
    Fx e;
    e.kind = Fx::Skull;
    e.who = static_cast<int8_t>(who);
    e.sound = sound::kDeath;
    return e;
}

// The last page queued gets picture n (0, 1)
void page_fx_set(int n, const Fx& e)
{
    if (fg->pages > 0 && !fg->page_dropped) fg->page[fg->pages - 1].fx[n] = e;
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

// Someone down: "goes down" / "and is Dying" / "is killed", the skull blinking on them
void down_page(int t)
{
    const combat::Down dn = combat::down_state(fg->b.f[t]);
    if (dn == combat::Down::Dead) add_page(t, fw(profile::kIsKilled));
    else if (dn == combat::Down::Dying) add_page(t, fw(profile::kGoesDown), fw(profile::kIsDying));
    else add_page(t, fw(profile::kGoesDown));
    page_fx_set(1, skull_fx(t));
}

// Stepping into a cloud: its save or nausea, as a page (the cloud spell's words)
const combat::FightSpell* fight_spell(int sp);
void poison_page(int i)
{
    add_page(i, fw(profile::kIsPoisoned));
    down_page(i);
}

void cloud_step(int i)
{
    combat::Battle& b = fg->b;
    const combat::Fighter& f = b.f[i];
    if (f.up() && combat::in_poison(b, f.x, f.y)) {
        if (combat::breathe_poison(b, i, rng)) poison_page(i);
        return;
    }
    if (!f.up() || !combat::in_cloud(b, f.x, f.y)) return;
    const combat::Did r = combat::breathe_cloud(b, i, rng);
    if (r == combat::Did::Unaffected) return;
    const auto& fs = d->prof->fight;
    for (int k = 0; k < fs.n_spells; ++k)
        if (fs.spells[k].does == combat::SpellDoes::Cloud) {
            char t[48];
            ow(r == combat::Did::Word ? fs.spells[k].word : fs.spells[k].word2, t, sizeof t);
            add_page(i, t);
            return;
        }
}

// "takes N points of damage from Fire" ... (spell_facts.md 1.7)
void damage_text(int amount, int kind, char* t, size_t cap)
{
    const char* from = "";
    const int k = kind & 0xF7;
    if (k == 0x01) from = fw(profile::kFromFire);
    else if (k == 0x02) from = fw(profile::kFromCold);
    else if (k == 0x04) from = fw(profile::kFromElec);
    else if (k == 0x10) from = fw(profile::kFromAcid);
    else if (kind == 0x08) from = fw(profile::kFromMagic);
    if (amount == 1) snprintf(t, cap, "%s%s", fw(profile::kTakes1), from);
    else snprintf(t, cap, "%s%d%s%s", fw(profile::kTakes), amount, fw(profile::kPointsOfDamage), from);
}

void spell_pages(int caster, const combat::SpellLine* l, int n, const combat::FightSpell& fs, bool each);

// The beholder's spells (Fear, Slow, Sleep): cast as the computer casts,
// each picking its own targets
void special_cast(int i, int sp)
{
    combat::Battle& b = fg->b;
    if (!load_magic()) return;
    const combat::FightSpell* fs = fight_spell(sp);
    if (!fs) return;
    const int pw = combat::power_of(b.f[i].rec, mrules->tables, sp, false);
    int tg[combat::kMaxFighters], nt = 0;
    if (!combat::spell_targets(b, fg->t, mrules->tables, i, *fs, sp, pw, rng, tg, &nt) || nt <= 0) return;
    const int ax = b.f[tg[0]].x, ay = b.f[tg[0]].y;
    if (fs->does == combat::SpellDoes::Fear || fs->does == combat::SpellDoes::Cone)
        nt = combat::cone(b, fg->t, i, ax, ay, 6, fs->does == combat::SpellDoes::Fear ? 3 : 2, tg, combat::kMaxFighters);
    add_page(i, fw(profile::kCastsASpell));
    Fx e;
    e.kind = Fx::Fly;
    e.fl = combat::spell_flight(5, 30);
    e.x0 = static_cast<uint8_t>(b.f[i].x);
    e.y0 = static_cast<uint8_t>(b.f[i].y);
    e.x1 = static_cast<uint8_t>(ax);
    e.y1 = static_cast<uint8_t>(ay);
    e.sound = sound::kSpell;
    page_fx_set(0, e);
    combat::SpellLine lines[24];
    const int n = combat::cast(b, mrules->tables, i, sp, *fs, tg, nt, rng, lines, 24, pw);
    spell_pages(i, lines, n, *fs, false);
}

// What the engine says happened besides the plain blows (monster_fx_facts.md),
// as pages: the special's words, its picture flying, the results
void event_page(const combat::Event& e)
{
    char t[48];
    const int who = e.who;
    auto said = [&](int word) { add_page(who, fw(word)); };
    auto fallen = [&](int word) {
        add_page(who, fw(word));
        page_fx_set(1, skull_fx(who));
    };
    switch (e.ev) {
    case combat::Ev::Engulfs:
    case combat::Ev::Hugs: {
        char nm[20];
        fighter_name(fg->b.f[e.to >= 0 ? e.to : who], nm, sizeof nm);
        snprintf(t, sizeof t, "%s%s", fw(e.ev == combat::Ev::Engulfs ? profile::kEngulfs : profile::kHugs), nm);
        add_page(who, t);
        break;
    }
    case combat::Ev::BreathesAcid: said(profile::kBreathesAcid); break;
    case combat::Ev::BreathesFire: said(profile::kBreathesFire); break;
    case combat::Ev::HoundFire: said(profile::kBreathesFireH); break;
    case combat::Ev::ThrowsLightning: said(profile::kThrowsLightning); break;
    case combat::Ev::SpitsAcid: said(profile::kSpitsAcid); break;
    case combat::Ev::SpitsMisses: said(profile::kSpitsMisses); break;
    case combat::Ev::Gazes: said(profile::kGazes); break;
    case combat::Ev::RayDisintegrate: said(profile::kRayDisint); break;
    case combat::Ev::RayStone: said(profile::kRayStone); break;
    case combat::Ev::RayDeath: said(profile::kRayDeath); break;
    case combat::Ev::RayWounds: said(profile::kWoundsYou); break;
    case combat::Ev::Fly: {
        // On the last page: its first free picture
        if (!fg->pages || fg->page_dropped) break;
        Fx f;
        f.kind = Fx::Fly;
        f.fl = combat::spell_flight(e.pic, e.delay);
        if (e.frames == 1) f.fl.frames = 1;
        f.x0 = static_cast<uint8_t>(fg->b.f[who].x);
        f.y0 = static_cast<uint8_t>(fg->b.f[who].y);
        f.x1 = e.x;
        f.y1 = e.y;
        if (e.pic == 6) f.sound = sound::kLightning;
        Page& p = fg->page[fg->pages - 1];
        p.fx[p.fx[0].kind == Fx::None ? 0 : 1] = f;
        break;
    }
    case combat::Ev::Avoids: said(profile::kAvoidsIt); break;
    case combat::Ev::Poisoned: said(profile::kIsPoisoned); break;
    case combat::Ev::Paralyzed: said(profile::kIsParalyzedC); break;
    case combat::Ev::ParalyzedLow: said(profile::kIsParalyzedL); break;
    case combat::Ev::Reflects: said(profile::kReflects); break;
    case combat::Ev::Stoned: fallen(profile::kIsStonedR); break;
    case combat::Ev::GazeStoned: fallen(profile::kIsStonedG); break;
    case combat::Ev::Disintegrated: fallen(profile::kIsDisint); break;
    case combat::Ev::Killed: fallen(profile::kIsKilled); break;
    case combat::Ev::Suffocates: fallen(profile::kSuffocates); break;
    case combat::Ev::Damage:
        damage_text(e.amount, e.kind, t, sizeof t);
        add_page(who, t);
        page_fx_set(1, hit_fx(who, false));
        break;
    case combat::Ev::Unaffected: said(profile::kIsUnaffectedS); break;
    case combat::Ev::Down: {
        const combat::Down dn = combat::down_state(fg->b.f[who]);
        if (dn == combat::Down::Dead) add_page(who, fw(profile::kIsKilled));
        else if (dn == combat::Down::Dying) add_page(who, fw(profile::kGoesDownS), fw(profile::kIsDying));
        else add_page(who, fw(profile::kGoesDownS));
        page_fx_set(1, skull_fx(who));
        break;
    }
    case combat::Ev::StandsUp:
    case combat::Ev::GetsUp: {
        add_page(who, fw(e.ev == combat::Ev::StandsUp ? profile::kStandsUp : profile::kGetsBackUp));
        Fx r;
        r.kind = Fx::Redraw;
        page_fx_set(1, r);
        break;
    }
    case combat::Ev::Cast: special_cast(who, e.amount); break;
    case combat::Ev::Dispelled: fallen(profile::kIsDispelled); break;
    case combat::Ev::ResistsDispel: said(profile::kResistsDispel); break;
    case combat::Ev::Zapped: said(profile::kGetsZapped); break;
    case combat::Ev::BoltFly: {
        // Thrown lightning: to the square, then along the bolt
        if (!fg->pages || fg->page_dropped) break;
        Fx f;
        f.kind = Fx::Bolt;
        f.fl = combat::spell_flight(e.pic, 50);
        f.x0 = static_cast<uint8_t>(fg->b.f[who].x);
        f.y0 = static_cast<uint8_t>(fg->b.f[who].y);
        f.x1 = e.x;
        f.y1 = e.y;
        f.sound = sound::kLightning;
        Page& p = fg->page[fg->pages - 1];
        p.fx[p.fx[0].kind == Fx::None ? 0 : 1] = f;
        break;
    }
    case combat::Ev::DiesPoison: fallen(profile::kDiesFromPoison); break;
    case combat::Ev::Regen: {
        char w[24];
        cw(fg->b.f[who].hp() >= fg->b.f[who].hp_max() ? profile::kFullyHealed : profile::kPartlyHealed, w, sizeof w);
        add_page(who, w);
        break;
    }
    }
}

// An attack and what it did, as pages. A shot (`ammo`: the item that flies,
// -1 none) flies first: its picture and sound on the first page. The
// weapon's attacks made (each shot uses a missile).
int do_attack(pic::Canvas& c, int a, int t, bool behind, bool shot = false, int ammo = -1)
{
    combat::Battle& b = fg->b;
    if (!on_screen(a) || !on_screen(t)) {
        focus(t, 3);
        draw_fight(c);
    }
    draw_icon(c, a, true);
    dirty(kViewY, kViewY + 7 * kSq);
    Fx fly;
    if (shot) {
        const combat::Fighter& f = b.f[a];
        fly = fly_fx(a, t, combat::shot_flight(b, f, ammo, combat::direction(f.x, f.y, b.f[t].x, b.f[t].y)));
        fly.sound = fly.fl.sound;
    }
    const combat::Attack at = combat::attack(b, a, t, names, rng, behind);
    char line[48];
    bool first = true;
    // The sounds: a shot's flight, then a blow each hit, a swish for no hit at all
    auto hit_page = [&]() {
        if (first && shot) {
            page_fx_set(0, fly);
            Fx blow;
            blow.sound = sound::kHit;
            page_fx_set(1, blow);
        } else {
            page_sound(sound::kHit);
        }
        first = false;
    };
    if (at.slain) {
        add_page(a, fw(profile::kSlays), fw(profile::kCruelBlow), t);
        hit_page();
    }
    for (int k = 0; k < at.n && !at.slain; ++k) {
        if (at.avoided[k]) {
            // A missile dodged: "Avoids it"
            add_page(t, fw(profile::kAvoidsIt));
            if (first && shot) page_fx_set(0, fly);
            first = false;
            continue;
        }
        if (!at.hits[k].hit) continue;
        const int dmg = at.hits[k].damage;
        snprintf(line, sizeof line, "%s%s%d%s%s", behind ? fw(profile::kFromBehind) : "", fw(profile::kHittingFor), dmg,
                 dmg == 1 ? fw(profile::kPointW) : fw(profile::kPointsW), fw(profile::kOfDamage));
        add_page(a, fw(at.backstab ? profile::kBackstabs : profile::kAttacksW), line, t);
        hit_page();
        // What the hit did besides (poison, paralysis, a touch's damage, engulfing ...)
        for (int j = 0; j < at.n_ev; ++j)
            if (at.ev[j].hit == k) event_page(at.ev[j]);
    }
    if (first) {
        add_page(a, fw(at.backstab ? profile::kBackstabs : profile::kAttacksW), fw(profile::kMisses), t);
        if (shot) page_fx_set(0, fly);
        else page_sound(sound::kMiss);
    }
    if (at.down) down_page(t);
    return at.shots;
}

// After an attack's pages: the turn ends unless attacks are left and the
// target went down (then pick another)
void after_attack(pic::Canvas& c)
{
    draw_fight(c);
    const combat::Fighter& f = fg->b.f[fg->cur];
    if (!f.up() || !f.size) {           // (down or gone meanwhile - free attacks, a zap)
        end_turn(c);
        return;
    }
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
        // None left: one (in slot 2 for those with no slot 1 attacks - the spiders; coab's facts)
        if (en.attacks[0] <= 0 && en.attacks[1] <= 0) en.attacks[en.rec[0x11C] == 0 ? 1 : 0] = 1;
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
        // (with the attacks it has left: none, no blow - the original's)
        if (en.attacks[0] <= 0 && en.attacks[1] <= 0) continue;
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
bool fight_miscast(pic::Canvas& c, int sp);
void remember_spell(combat::Fighter& f, int sp);
void aim_exit(pic::Canvas& c);
void strike(pic::Canvas& c, int t);
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

// Shots: `n` arrows / quarrels of the pile (one for each attack made), or
// the thrown weapon, go (the pile's count less n; none left - or a single
// one: it's gone)
void spend_ammo(int i, int k, int n)
{
    combat::Fighter& f = fg->b.f[i];
    if (k < 0 || k >= f.n_items || n < 1) return;
    if (f.member >= 0) {
        party::Character& ch = pt->m[f.member];
        const int left = ch.items[k][0x39];
        if (left > n) {
            ch.items[k][0x39] = static_cast<uint8_t>(left - n);
        } else {
            // A thrown weapon that also strikes (flags 4 and 0x10: a spear, a
            // hand axe ...) lands on the field: the treasure after the fight;
            // the rest (darts, javelins, flasks) are gone, as is one with
            // effect 0x89 (the original's test, listing ovr014:1BD4-1C62)
            const items::TypeInfo& ti = names->type(ch.items[k][0x2E]);
            const bool lands = (ti.flags & 0x14) == 0x14 && ch.items[k][0x3E] != 0x89;
            if (lands && fg->n_thrown < 6) {
                memcpy(fg->thrown[fg->n_thrown], ch.items[k], items::kRecordSize);
                fg->thrown[fg->n_thrown][0x34] = 0;
                ++fg->n_thrown;
            }
            rules::remove_item(ch, k);
        }
        f.n_items = ch.n_items;
        rules::recalc(ch, *names, d->facts);
        return;
    }
    const int left = f.items[k][0x39];
    if (left > n) f.items[k][0x39] = static_cast<uint8_t>(left - n);
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
    if (!f.can_use) {                   // (silenced, coughing: no items this round)
        fight_menu(c);
        return;
    }
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
    const bool camp_only = spells::entry(mrules->tables, sp).when == 0;
    if (!fs || fs->does == combat::SpellDoes::NotYet || camp_only) {
        fg->use_item = -1;
        fg->spell = 0;
        fight_menu(c);
        if (camp_only) note(c, fw(profile::kCampOnly));
        else error(c, "Not in the engine yet.");
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
    fg->spell = 0;
    if (!f.team() || on_screen(fg->cur)) focus(fg->cur, 2);
    draw_fight(c);
    // Coughing (a stinking cloud's): no items and no spells this round
    const uint8_t cough = d->prof->fight.facts.cough;
    if (cough && f.has(cough) && !f.coughed) {
        f.coughed = true;
        f.can_use = false;
        f.can_cast = false;
        add_page(fg->cur, fw(profile::kIsCoughing));
        say(c, FSt::Resume);
        return;
    }
    // Effects at the turn's start (spell_facts.md 1.8): silenced (no
    // spells or items), fighting snakes or fumbling (the turn lost)
    if (!fg->fx_done) {
        fg->fx_done = true;                     // (once a turn, whatever was said before it)
        const combat::TurnFx tf = combat::turn_effects(b, fg->cur);
        if (tf == combat::TurnFx::Silenced) {
            add_page(fg->cur, fw(profile::kIsSilenced));
            say(c, FSt::Resume);
            return;
        }
        if (tf == combat::TurnFx::Snakes) {
            add_page(fg->cur, fw(profile::kFightingSnakes));
            say(c, FSt::Next);
            return;
        }
        if (tf == combat::TurnFx::Fumbling) {
            end_turn(c);
            return;
        }
        if (tf == combat::TurnFx::Confused) {
            add_page(fg->cur, fw(profile::kIsConfusedT));
            say(c, FSt::Next);
            return;
        }
        if (tf == combat::TurnFx::RunsAway || tf == combat::TurnFx::Berserk || tf == combat::TurnFx::Enraged) {
            // (the turn goes on: fleeing, at the nearest one, or as ever)
            add_page(fg->cur, fw(tf == combat::TurnFx::RunsAway ? profile::kRunsAway
                                 : tf == combat::TurnFx::Berserk ? profile::kGoesBerserk
                                                                 : profile::kIsEnraged));
            say(c, FSt::Resume);
            return;
        }
        if (tf == combat::TurnFx::Suffocates) {
            // Engulfed too long
            add_page(fg->cur, fw(profile::kSuffocates));
            page_fx_set(1, skull_fx(fg->cur));
            draw_fight(c);
            say(c, FSt::Next);
            return;
        }
    }
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
        if (fight_miscast(c, sp)) {
            if (f.spell_n) remember_spell(f, sp);
            f.spell_n = 0;
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
    if (combat::standing(b, 0) == 0 || (combat::standing(b, 1) == 0 && !fg->continuing)) {
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
    fg->fx_done = false;
    start_turn(c);
}

void begin_round(pic::Canvas& c)
{
    combat::start_round(fg->b, rng);
    vm->set(0x7ECB, 0);                 // (the surprise word: cleared after the initiative, as the original)
    next_turn(c);
}

void round_rest(pic::Canvas& c);
void round_clouds(pic::Canvas& c);
void fight_hammer(int i);

void round_end(pic::Canvas& c)
{
    vm->advance_clock(1, 1);                // a round is a minute
    vm->take_minutes();
    // Everyone's effects a minute on; regeneration; a troll getting up
    combat::Event ev[8];
    const int nev = combat::tick(fg->b, ev, 8);
    for (int k = 0; k < nev; ++k) event_page(ev[k]);
    // Spiritual Hammer: back in hand each round while it lasts, gone after
    for (int k = 0; k < fg->b.party_size; ++k) fight_hammer(k);
    // Those standing in a poisonous cloud breathe it again
    for (int k = 0; k < fg->b.n; ++k) {
        const combat::Fighter& f = fg->b.f[k];
        if (f.up() && f.size && combat::in_poison(fg->b, f.x, f.y) && combat::breathe_poison(fg->b, k, rng)) poison_page(k);
    }
    if (fg->pages) {
        draw_fight(c);
        say(c, FSt::Clouds);                // (then on with the round's end)
        return;
    }
    round_clouds(c);
}

// The clouds a round older: those that go clear ("The air clears a little...")
void round_clouds(pic::Canvas& c)
{
    if (combat::clouds_round(fg->b)) {
        // "The air clears a little..." on the menu line for the game's delay
        draw_field(c);
        clear_menu_line(c);
        put(c, fw(profile::kAirClears), 0, text::kMenuRow, 10);
        fg->st = FSt::RoundRest;
        fg->until = millis() + game_delay_ms();
        if (!fg->until) fg->until = 1;
        return;
    }
    round_rest(c);
}

void round_rest(pic::Canvas& c)
{
    const bool over = combat::end_round(fg->b);
    if (over && !in_demo && combat::standing(fg->b, 1) == 0 && combat::standing(fg->b, 0) > 1) {
        // No enemies left, more than one standing: "Continue Battle: Yes No"
        // (time to bandage and heal before the fight ends - the original's)
        fg->st = FSt::ContinueAsk;
        text::build(menu, fw(profile::kContinueBattle), rw(Data::kYesNo));
        menu.selected = 1;
        show_menu_line(c);
        return;
    }
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
    if (f.n_items && f.can_use) strcat(words, fw(profile::kFUse));
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

// A readied weapon that only shoots (a bow, a crossbow, a sling - not one
// that also strikes, like a dagger or a spear): no blows with it
bool pure_missile(const combat::Fighter& f)
{
    const int w = combat::weapon(f, *names);
    if (w < 0) return false;
    const items::TypeInfo& ti = names->type(f.items[w][0x2E]);
    return ti.range > 1 && !(ti.flags & 0x04);
}

bool enemy_next_to(int i)
{
    const combat::Battle& b = fg->b;
    for (int k = 0; k < b.n; ++k)
        if (k != i && b.f[k].up() && b.f[k].size && b.f[k].team() != b.f[i].team() && combat::adjacent(b, i, k))
            return true;
    return false;
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
        if (pure_missile(f)) {          // (the original's "Not with that weapon")
            note(c, fw(profile::kNotWithWeapon));
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
    sfx(sound::kStep);
    f.moves -= cost;
    guard_attacks(c, i);
    cloud_step(i);
    if (fg->pages) {
        draw_fight(c);
        say(c, !f.up() ? FSt::Turn : combat::helpless(b, f) ? FSt::Next : FSt::Move);
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
    if (pure_missile(f)) {
        // Only with ammunition, and not with an enemy next to them (facts 2.4)
        if (reach <= 0 || enemy_next_to(i)) return false;
    } else if (combat::adjacent(b, i, t)) {
        reach = 1;
    }
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
    fg->st = FSt::Aim;                  // (also back from "Abort Spell?" - No / Esc)
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
    strike(c, t);
    say(c, FSt::Turn);
}

// The blow or the shot at t: missiles - the readied ammunition (arrows /
// quarrels) or the thrown weapon - go when it isn't next to them
void strike(pic::Canvas& c, int t)
{
    combat::Fighter& f = fg->b.f[fg->cur];
    int ammo = -1;
    const bool shot = !combat::adjacent(fg->b, fg->cur, t) && combat::missile(fg->b, f, &ammo) > 0;
    if (shot) spend_ammo(fg->cur, ammo, do_attack(c, fg->cur, t, false, true, ammo));
    else melee(c, fg->cur, t);
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
    case 'E': aim_exit(c); return;
    default: return;
    }
    aim_menu(c);
}

// Leaving the aim (Exit, Esc): a weapon's - back to the menu; a spell's
// (memorized or an item's) - "Abort Spell? Yes No" as the original (Yes:
// the spell or the use is gone, the turn over; No: aim again); picking one
// by one - cast with those picked (even none: the original)
void aim_exit(pic::Canvas& c)
{
    status_line(c, "");
    if (fg->spell && (fg->sp_n > 0 || fg->pick_budget >= 0)) {
        cast_now(c);
        return;
    }
    if (fg->spell) {
        fg->st = FSt::AbortAsk;
        text::build(menu, fw(profile::kAbortSpell), rw(Data::kYesNo));
        menu.selected = 1;
        show_menu_line(c);
        return;
    }
    fg->use_item = -1;
    draw_fight(c);
    fight_menu(c);
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
// Everyone the computer plays - Quick members, NPCs and the monsters too
// (the original's computer turn readies weapons for all: coab's facts,
// AI_items_selection) - readies the weapon the games' rating picks; a
// two-handed one puts the shield away (a cursed shield: the weapon goes
// back), free hands take the best shield (its plus + 1). A monster's
// record is worked out again as the original's routine does (with its
// group's items). True: it changed.
bool ai_ready_weapon(int i)
{
    combat::Fighter& f = fg->b.f[i];
    const bool member = f.member >= 0 && f.member < pt->count;
    if (!member && (f.monster < 0 || !ml || !f.items)) return false;
    const int want = combat::choose_weapon(fg->b, i);
    if (want == combat::kKeep) return false;
    party::Character* tmp = nullptr;
    if (!member) {
        tmp = new (std::nothrow) party::Character;
        if (!tmp) return false;
        memcpy(tmp->rec, f.rec, sizeof tmp->rec);
        tmp->n_items = f.n_items < party::kMaxItems ? f.n_items : party::kMaxItems;
        memcpy(tmp->items, f.items, static_cast<size_t>(tmp->n_items) * items::kRecordSize);
        tmp->n_affects = f.n_aff ? (*f.n_aff < party::kMaxAffects ? *f.n_aff : party::kMaxAffects) : 0;
        memcpy(tmp->affects, f.aff, static_cast<size_t>(tmp->n_affects) * party::kAffectSize);
    }
    party::Character& ch = member ? pt->m[f.member] : *tmp;
    auto recalc = [&]() {
        if (member) rules::recalc(ch, *names, d->facts);
        else rules::recalc_values(ch, *names, d->facts);
    };
    const int held = combat::weapon(f, *names);
    if (held >= 0) {
        ch.items[held][0x34] = 0;
        rules::worn(ch, held, false);
    }
    if (want >= 0) {
        ch.items[want][0x34] = 1;
        rules::worn(ch, want, true);
    }
    recalc();
    // The shield
    int shield = -1, best = -1, best_plus = 0;
    for (int k = 0; k < ch.n_items; ++k) {
        const items::TypeInfo& ti = names->type(ch.items[k][0x2E]);
        if (ti.slot != 1) continue;
        if (ch.items[k][0x34]) shield = k;
        const int plus = static_cast<int8_t>(ch.items[k][0x32]);
        if ((ch.rec[0x12B] & ti.classes) && plus + 1 > best_plus) {
            best = k;
            best_plus = plus + 1;
        }
    }
    if (ch.rec[0x185] > 2) {
        const int k = shield >= 0 && !ch.items[shield][0x36] ? shield : want;
        if (k >= 0) {
            ch.items[k][0x34] = 0;
            rules::worn(ch, k, false);
        }
    } else if (ch.rec[0x185] < 2 && best >= 0 && best != shield && !(shield >= 0 && ch.items[shield][0x36]) &&
               ch.rec[0x185] + names->type(ch.items[best][0x2E]).hands - (shield >= 0 ? names->type(ch.items[shield][0x2E]).hands : 0) <= 2) {
        if (shield >= 0) {
            ch.items[shield][0x34] = 0;
            rules::worn(ch, shield, false);
        }
        ch.items[best][0x34] = 1;
        rules::worn(ch, best, true);
    }
    recalc();
    if (member) {
        f.n_items = ch.n_items;
        f.items = ch.items;
    } else {
        // Back into the monster (its record, effects) and its group's items
        memcpy(f.rec, tmp->rec, sizeof tmp->rec);
        memcpy(f.items, tmp->items, static_cast<size_t>(tmp->n_items) * items::kRecordSize);
        if (f.n_aff) {
            *f.n_aff = tmp->n_affects < f.max_aff ? tmp->n_affects : f.max_aff;
            memcpy(f.aff, tmp->affects, static_cast<size_t>(*f.n_aff) * party::kAffectSize);
        }
        delete tmp;
    }
    combat::recount_attacks(fg->b, i);      // (its attacks for the weapon now: coab reclac_attacks)
    return true;
}

void ai_step(pic::Canvas& c)
{
    combat::Battle& b = fg->b;
    const int i = fg->cur;
    combat::Fighter& f = b.f[i];
    // Quick party members bandage a dying friend first (at the turn's start)
    if (!f.team() && !fg->ai_checked && combat::anyone_dying(b)) {
        const int k = combat::bandage(b);
        if (k >= 0) {
            add_page(k, fw(profile::kBandaged));
            f.delay = 0;
            say(c, FSt::Next);            // (the turn is over: not an attack)
            fg->cur = i;
            return;
        }
    }
    // Morale (monsters, NPCs), once a turn
    if (!fg->ai_checked) {
        fg->ai_checked = true;
        // The holds it had (engulfing, hugging) let go, as the original does
        combat::release_holds(b, i);
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
        if (!f.fleeing && f.n_items && f.can_use && combat::standing(b, f.team() ? 0 : 1) && load_magic()) {
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
                const int delay = spells::entry(mrules->tables, sp).delay / 3;
                if (delay == 0 && fight_miscast(c, sp)) return;
                forget_spell(f, sp);
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
        // The best weapon for now (the panel shows it)
        if (ai_ready_weapon(i)) draw_panel_for(c, i);
        // Its special actions (breath, spit, gazes, rays, thrown lightning)
        if (!f.fleeing) {
            combat::Event ev[24];
            bool ends = false;
            const int nev = combat::special(b, fg->t, i, rng, ev, 24, &ends);
            if (nev > 0) {
                if (!on_screen(i)) focus(i, 3);    // the one doing it in view
                draw_fight(c);
                draw_icon(c, i, true);
                for (int k = 0; k < nev; ++k) event_page(ev[k]);
                draw_fight(c);
                if (ends || !f.up()) f.attacks[0] = f.attacks[1] = 0;
                say(c, ends || !f.up() ? FSt::Next : FSt::Ai);
                return;
            }
        }
    } else if (combat::missile_in_melee(b, i)) {
        // A bow with an enemy next to them: another weapon, and the turn is over
        if (ai_ready_weapon(i)) draw_panel_for(c, i);
        end_turn(c);
        return;
    }
    const combat::Plan p = combat::think(b, fg->t, i, rng);
    switch (p.act) {
    case combat::Act::Attack:
        if (p.missile) spend_ammo(i, p.ammo, do_attack(c, i, p.target, false, true, p.ammo));
        else melee(c, i, p.target);
        say(c, FSt::Turn);
        return;
    case combat::Act::Step: {
        const int cost = combat::step_cost(b, fg->t, i, p.dir, nullptr, nullptr);
        free_attacks(c, i, p.dir);
        if (f.up()) {
            combat::step(b, i, p.dir);
            sfx(sound::kStep);
            f.moves -= cost;
            guard_attacks(c, i);
            cloud_step(i);
        }
        if (on_screen(i) && !f.team()) focus(i, 2);
        draw_fight(c);
        if (fg->pages) {
            say(c, !f.up() ? FSt::Turn : combat::helpless(b, f) ? FSt::Next : FSt::Ai);
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
    // What only other screens use goes first (they load again when needed):
    // the most room for the fight's blocks
    end_magic();
    delete hw;
    hw = nullptr;
    if (!fg) fg = make_fight();
    if (!fg || !ml || !pt->count) {
        error(c, ml && pt->count ? no_memory("the fight") : "Not enough memory.");
        delete fg;
        fg = nullptr;
        fight_clear_monsters();
        vm->set(0x7EC7, 0x81);          // (no fight: as if the party fled - never a win)
        resume_after_note = true;       // the script goes on once the error is tapped
        return;
    }
    anim_stop();
    load_fight_words();
    const bool room = alloc_tiles();
    if (!room || !load_fight_tables()) {
        error(c, room ? "The combat tables can't be read." : no_memory("the fight's field"));
        end_fight_art();
        delete fg;
        fg = nullptr;
        fight_clear_monsters();
        vm->set(0x7EC7, 0x81);          // (no fight: as if the party fled - never a win)
        resume_after_note = true;       // the script goes on once the error is tapped
        return;
    }
    combat::Battle& b = fg->b;
    b.~Battle();
    new (&b) combat::Battle();         // (in place: a temporary would take 10 KB of stack)
    b.tables = &fg->t;
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
    // The original recalculates every combatant's values at the battle's
    // setup (coab's facts: SetupCombatActions; the listing's sub_380E0 calls
    // sub_66C20 for each): a monster's to-hit, AC, damage dice and movement
    // come from its record's base values and its readied items - the
    // record's own stored ones are not used
    if (party::Character* tmp = new (std::nothrow) party::Character) {
        for (int k = 0; k < ml->n; ++k) {
            combat::Monster& m = ml->m[k];
            const combat::Group& g = ml->g[m.group];
            memcpy(tmp->rec, m.rec, sizeof tmp->rec);
            tmp->n_items = g.n_items < party::kMaxItems ? g.n_items : party::kMaxItems;
            memcpy(tmp->items, g.item, static_cast<size_t>(tmp->n_items) * items::kRecordSize);
            tmp->n_affects = 0;
            rules::recalc_values(*tmp, *names, d->facts);
            memcpy(m.rec, tmp->rec, sizeof m.rec);
        }
        delete tmp;
    }
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
    // The scripts' surprise word (coab's facts: area2 0x596): (team + 1) &
    // it - 1 the party, 2 the monsters; the original clears it with the
    // first round's initiative, so it counts for this fight only
    b.surprise = vm->get(0x7ECB) & 3;
    vm->set(0x7ECB, 0);
    b.to_hit_party = static_cast<int8_t>(vm->get(0x7F71));
    b.to_hit_monsters = static_cast<int8_t>(vm->get(0x7F70));
    combat::place(b, fg->t, d->gs.dir & 6, vm->fight_distance(), b.indoors ? solid_side : nullptr, nullptr);
    combat::battle_start(b);                 // those invisible from the start
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
        const uint8_t charm = d->prof->fight.facts.charm;
        for (int k = 0; charm && k < ch.n_affects; ++k)
            if (ch.affects[k][0] == charm) combat::uncharm(ch.rec, ch.affects[k]);     // back on the party's side
        ch.rec[0x197] = 0;                              // (whatever happened: the party's side after a fight)
        for (uint8_t t : d->prof->fight.ends)
            if (t) rules::remove_affects(ch, t);
        rules::keep_hammer(ch, *names, d->facts);       // (its effect ended: the hammer goes)
        // Animated dead: they collapse - dead, on their own side again
        const uint8_t anim = d->prof->fight.facts.sp.animated;
        for (int k = 0; anim && k < ch.n_affects; ++k)
            if (ch.affects[k][0] == anim) {
                ch.rec[0x197] = static_cast<uint8_t>(ch.affects[k][3] >> 4);
                rules::remove_affects(ch, anim);
                ch.rec[0x195] = party::Dead;
                ch.rec[0x196] = 0;
                ch.rec[0x1A4] = 0;
                ch.rec[0xE9] = 0;
                ch.rec[0x11A] = 0;
                ch.rec[0x1A5] = 12;
                if (fg && i < fg->b.n) ch.rec[0xF7] = fg->b.f[i].was_control;
                break;
            }
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
    combat::Result r = fg->out.result;
    if (in_demo && r == combat::Lost)
        // The demo's party is all NPCs (who don't count as the party's
        // survivors in a real game): it wins while one of them stands
        for (int i = 0; i < b.party_size; ++i)
            if (b.f[i].status() == party::Okay || b.f[i].status() == party::Animated) r = combat::Won;
    int out = 0;
    for (int i = b.party_size; i < b.n; ++i)
        if (!b.f[i].gone && !b.f[i].up()) ++out;
    vm->set(0x7EC8, static_cast<uint16_t>(out));
    vm->set(0x7EC7, static_cast<uint16_t>(r));
    if (in_demo) {
        // The demo (demo_facts.md 5): no clean-up, experience, treasure or
        // results; the script goes on (all down: the demo ends)
        end_fight_art();
        fight_clear_monsters();
        palette_fight(false);
        ground->clear();
        delete fg;
        fg = nullptr;
        if (r == combat::Lost) {
            end_demo(c);
            return;
        }
        fight_back_to_game(c);
        clear_menu_line(c);
        waiting = false;
        handle(vm->resume());
        return;
    }
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
        // The party's own thrown weapons: there to take back (no experience for them)
        for (int k = 0; k < fg->n_thrown && ground->n < items::kMaxGround; ++k)
            memcpy(ground->item[ground->n++], fg->thrown[k], items::kRecordSize);
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
    if (!fg) fg = make_fight();
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
    case FSt::RoundRest:
        clear_menu_line(c);
        round_rest(c);
        break;
    case FSt::Pages:
        if (fg->fx_on) {
            if (!fx_step(c)) {
                fx_end(c);
                page_fx(c);
            }
            break;
        }
        if (fg->at < fg->pages) {
            show_page(c);
            break;
        }
        page_over(c);
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
        case FSt::Clouds:
            round_clouds(c);
            break;
        case FSt::Resume:
            start_turn(c);                  // (the turn goes on after its message)
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
    // An item's spell: the item's level (6 - the monsters' own, 12)
    if (fg->use_item >= 0 && fg->spell && mrules) return combat::power_of(f.rec, mrules->tables, fg->spell, true);
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

// The Robe of Vermin's effect on the one casting (a d2 of 1): the spell
// miscasts as it goes off - "Casts a Spell" / "Spell: NAME" and nothing
// else, the spell kept in memory, the turn over (listing ovr023:1403; in a
// fight the cast's words are "Casts a Spell", not "miscasts")
bool fight_miscast(pic::Canvas& c, int sp)
{
    combat::Fighter& f = fg->b.f[fg->cur];
    const uint8_t vermin = d->prof->fight.facts.items.vermin;
    if (!vermin || !f.has(vermin) || rng.roll(2, 1) != 1) return false;
    char t[40];
    snprintf(t, sizeof t, "%s%s", fw(profile::kSpellColon), spell_name(sp));
    status_line(c, t);
    add_page(fg->cur, fw(profile::kCastsASpell));
    fg->spell = 0;
    f.moves = f.attacks[0] = f.attacks[1] = 0;
    say(c, FSt::Next);
    return true;
}
// A spell back in memory (the computer's, taken out when chosen, miscast)
void remember_spell(combat::Fighter& f, int sp)
{
    for (int k = 0; k < magic::kListSize; ++k)
        if (!f.rec[magic::kListAt + k]) {
            f.rec[magic::kListAt + k] = static_cast<uint8_t>(sp);
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

// The fire shield's flame: "flame type: Hot Cold"; leaving it: "Abort spell? Yes No"
void flame_menu(pic::Canvas& c)
{
    fg->st = FSt::FlameAsk;
    text::build(menu, fw(profile::kFlameType), fw(profile::kHotCold));
    menu.selected = 0;
    show_menu_line(c);
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
    fg->pick_budget = -1;
    fg->aim_x = fg->aim_y = -1;
    const int aim = e.aim & 0x0F;
    const combat::FightSpell* bfs = fight_spell(sp);
    if (bfs && bfs->does == combat::SpellDoes::FireShield && !fg->b.f[fg->cur].quick && !fg->b.f[fg->cur].team()) {
        // "flame type: Hot Cold"
        fg->sp_t[fg->sp_n++] = fg->cur;
        flame_menu(c);
        return;
    }
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
        // One by one until 2+ pass the budget (Faerie Fire: the level, by
        // size; else 2d4, by Hit Dice - never shown, as in the original)
        const combat::FightSpell* afs = fight_spell(sp);
        fg->pick_size = afs && afs->does == combat::SpellDoes::Faerie;
        fg->pick_budget = fg->pick_size ? combat::power_of(fg->b.f[fg->cur].rec, mrules->tables, sp) : rng.roll(4, 2);
        fg->sp_need = combat::kMaxFighters;
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
                         fs->does != combat::SpellDoes::Mirror && fs->does != combat::SpellDoes::CureBlind &&
                         (fs->does != combat::SpellDoes::Affect || e.targets == spells::kCombat);
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
        f.moves = f.attacks[0] = f.attacks[1] = 0;     // (the turn is the casting's now)
        clear_menu_line(c);
        add_page(fg->cur, fw(profile::kBeginsCasting));
        fg->after = FSt::Turn;
        fg->st = FSt::Pages;
        fg->at = 0;
        show_page(c);
        // (the turn ends without its delay going: next_turn after the page)
        fg->after = FSt::Begin;
        return;
    }
    if (fight_miscast(c, sp)) return;
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
        fg->aim_x = x;
        fg->aim_y = y;
        fg->sp_n = combat::bolt_line(b, fg->t, fg->cur, x, y, 7, fg->sp_t, combat::kMaxFighters);
        if (!fg->sp_n) fg->sp_t[fg->sp_n++] = t >= 0 ? t : fg->cur;
        cast_now(c);
        return;
    }
    if (fg->sp_area >= 0) {
        // The area's centre: the fighter or the square
        const int x = t >= 0 ? b.f[t].x : fg->mx, y = t >= 0 ? b.f[t].y : fg->my;
        fg->aim_x = x;
        fg->aim_y = y;
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
    if (fg->sp_n >= fg->sp_need || fg->sp_n >= combat::kMaxFighters ||
        (fg->pick_budget >= 0 && combat::picks_done(b, fg->sp_t, fg->sp_n, fg->pick_budget, fg->pick_size))) {
        cast_now(c);
        return;
    }
    aim_menu(c);
}

// What a spell did, as pages: a burst on each one hurt, sparkles on each
// one it works on, the skull on the fallen. `each`: a spell aimed at
// several picked one by one flies to each (with its sound) after the first.
void spell_pages(int caster, const combat::SpellLine* l, int n, const combat::FightSpell& fs, bool each)
{
    char t[48];
    int flown[combat::kMaxFighters], n_flown = 0;
    if (fg->sp_n > 0) flown[n_flown++] = fg->sp_t[0];
    for (int k = 0; k < n; ++k) {
        const combat::Fighter& f = fg->b.f[l[k].who];
        const int before = fg->pages;
        switch (l[k].did) {
        case combat::Did::Word:
            ow(fs.word, t, sizeof t);
            add_page(l[k].who, t);
            if (fs.does != combat::SpellDoes::Poison) page_fx_set(1, hit_fx(l[k].who, true));
            break;
        case combat::Did::Damage: {
            damage_text(l[k].amount, fs.kind, t, sizeof t);
            add_page(l[k].who, t);
            page_fx_set(1, hit_fx(l[k].who, false));
            break;
        }
        case combat::Did::Unaffected: add_page(l[k].who, fw(profile::kIsUnaffectedS)); break;
        case combat::Did::Word2:
            ow(fs.word2, t, sizeof t);
            add_page(l[k].who, t);
            page_fx_set(1, hit_fx(l[k].who, true));
            break;
        case combat::Did::Word3:
            ow(fs.word3, t, sizeof t);
            add_page(l[k].who, t);
            page_fx_set(1, hit_fx(l[k].who, true));
            break;
        case combat::Did::Risen: {
            // Back on its feet: "gets back up" (the party's side) / "stands up and grins"
            add_page(l[k].who, fw(f.team() ? profile::kStandsUp : profile::kGetsBackUp));
            Fx r;
            r.kind = Fx::Redraw;
            page_fx_set(1, r);
            break;
        }
        case combat::Did::Misses: add_page(caster, fw(profile::kMisses), nullptr, l[k].who); break;
        case combat::Did::LostImage: add_page(l[k].who, fw(profile::kLostImage)); break;
        case combat::Did::Fallen:
            if (fg->pages > 0 && !fg->page_dropped) fg->page[fg->pages - 1].fx[1] = skull_fx(l[k].who);
            break;
        case combat::Did::Healed: {
            char w2[24];
            cw(f.hp() >= f.hp_max() ? profile::kFullyHealed : profile::kPartlyHealed, w2, sizeof w2);
            add_page(l[k].who, w2);
            page_fx_set(1, hit_fx(l[k].who, true));
            break;
        }
        case combat::Did::Down: {
            const combat::Down dn = combat::down_state(f);
            if (dn == combat::Down::Dead) add_page(l[k].who, fw(profile::kIsKilled));
            else if (dn == combat::Down::Dying) add_page(l[k].who, fw(profile::kGoesDownS), fw(profile::kIsDying));
            else add_page(l[k].who, fw(profile::kGoesDownS));
            page_fx_set(1, skull_fx(l[k].who));
            break;
        }
        }
        // The spell flies on to the next one picked
        if (each && fg->pages > before && l[k].who != caster) {
            bool seen = false;
            for (int j = 0; j < n_flown; ++j) seen = seen || flown[j] == l[k].who;
            if (!seen && n_flown < combat::kMaxFighters) {
                flown[n_flown++] = l[k].who;
                Fx e = fly_fx(caster, l[k].who, combat::spell_flight(5, 30));
                e.sound = sound::kSpell;
                fg->page[before].fx[0] = e;
            }
        }
    }
}

// Spiritual Hammer: a party fighter with its effect holds the hammer ("Gains
// an item" when it's new); without it the hammer goes
void fight_hammer(int i)
{
    combat::Fighter& f = fg->b.f[i];
    if (f.member < 0 || f.member >= pt->count) return;
    party::Character& ch = pt->m[f.member];
    const int before = ch.n_items;
    const bool gained = rules::keep_hammer(ch, *names, d->facts);
    if (gained) add_page(i, fw(profile::kGainsItem));
    if (gained || ch.n_items != before) rules::recalc(ch, *names, d->facts);
    f.items = ch.items;
    f.n_items = ch.n_items;
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
    const int first_page = fg->pages;
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
    // Where it flies: the square aimed at, else the first one it's for
    int ax = fg->aim_x, ay = fg->aim_y;
    if (ax < 0 && fg->sp_n > 0) {
        ax = b.f[fg->sp_t[0]].x;
        ay = b.f[fg->sp_t[0]].y;
    }
    fg->aim_x = fg->aim_y = -1;
    const spells::Entry se = spells::entry(mrules->tables, sp);
    const int aim = se.aim & 0x0F;
    const bool each = aim >= 1 && aim <= 4 && se.targets != spells::kParty;
    if (fs->does == combat::SpellDoes::Defoliate && ax >= 0)
        fg->sp_n = combat::cone(b, fg->t, i, ax, ay, 3, 1, fg->sp_t, combat::kMaxFighters);   // 3 squares, 1 ray
    if ((fs->does == combat::SpellDoes::Cone || fs->does == combat::SpellDoes::Fear) && ax >= 0) {
        // A cone from the caster through the square aimed at: Fear 3 rays of 6 squares, Cone of Cold 2 of (level + 1) / 2
        const int lv = from_item ? pw : combat::power_of(f.rec, mrules->tables, sp, false);
        const bool fear = fs->does == combat::SpellDoes::Fear;
        const int reach = fear ? 6 : ((lv + 1) / 2 > 1 ? (lv + 1) / 2 : 1);
        fg->sp_n = combat::cone(b, fg->t, i, ax, ay, reach, fear ? 3 : 2, fg->sp_t, combat::kMaxFighters);
    }
    if ((fs->does == combat::SpellDoes::Cloud || fs->does == combat::SpellDoes::Poison) && ax >= 0) {
        // A cloud on the field (Stinking Cloud 2 x 2 from the square aimed at, Cloudkill 3 x 3 round it) for
        // the caster's level in rounds; those in it breathe it
        const bool poison = fs->does == combat::SpellDoes::Poison;
        const int rounds = from_item ? pw : combat::power_of(f.rec, mrules->tables, sp, false);
        if (combat::lay_cloud(b, fg->t, ax, ay, rounds, poison)) {
            add_page(i, fw(poison ? profile::kPoisonCloud : profile::kNoxiousCloud));
            Fx e;
            e.kind = Fx::Redraw;
            page_fx_set(1, e);
        }
        fg->sp_n = combat::cloud_fighters(b, ax, ay, fg->sp_t, combat::kMaxFighters, poison);
    }
    if (fs->does == combat::SpellDoes::Teleport) {
        // Dimension Door: to the square aimed at, within 3 squares a level, when it can stand there
        const int lv = from_item ? pw : combat::power_of(f.rec, mrules->tables, sp, false);
        int len = 0;
        combat::path(b, fg->t, i, ax, ay, true, &len);
        if (ax >= 0 && len / 2 <= 3 * lv && combat::teleport(b, fg->t, i, ax, ay)) {
            char said[48];
            ow(fs->word, said, sizeof said);
            add_page(i, said);
            Fx r;
            r.kind = Fx::Redraw;
            page_fx_set(1, r);
            focus(i, 2);
        }
        if (from_item) spend_item(i, sp);
        f.attacks[0] = f.attacks[1] = 0;
        f.moves = 0;
        say(c, FSt::Next);
        return;
    }
    combat::SpellLine lines[24];
    b.aim_x = ax;
    b.aim_y = ay;
    const int n = combat::cast(b, mrules->tables, i, sp, *fs, fg->sp_t, fg->sp_n, rng, lines, 24, pw);
    b.aim_x = b.aim_y = -1;
    spell_pages(i, lines, n, *fs, each);
    // Restoration: the levels themselves; Spiritual Hammer: the hammer
    if (fs->does == combat::SpellDoes::Restore)
        for (int k = 0; k < n; ++k) {
            const combat::Fighter& rf = b.f[lines[k].who];
            if (lines[k].did == combat::Did::Word && rf.member >= 0 && rf.member < pt->count)
                create::restore(pt->m[rf.member], mrules->tables);
        }
    if (fs->does == combat::SpellDoes::Hammer) fight_hammer(i);
    // Strength, items and effects changed: the party's values again
    if (fs->does == combat::SpellDoes::Enlarge || fs->does == combat::SpellDoes::Reduce ||
        fs->does == combat::SpellDoes::GiantStrength ||
        fs->does == combat::SpellDoes::RemoveCurse || fs->does == combat::SpellDoes::Dispel ||
        fs->does == combat::SpellDoes::Restore)
        for (int k = 0; k < pt->count; ++k) rules::recalc(pt->m[k], *names, d->facts);
    // The spell flies with its first page, with its sound: the fireball's
    // roar, the bolt's crackle (its own picture), else the spell's
    if (fg->pages > first_page) {
        const bool bolt = sp == d->prof->sound.lightning_spell;
        Fx e;
        e.kind = ax >= 0 ? (b.n_bolt > 0 ? Fx::Bolt : Fx::Fly) : Fx::None;
        e.fl = combat::spell_flight(bolt && b.n_bolt == 0 ? 6 : 5, bolt && b.n_bolt == 0 ? 50 : 30);
        e.x0 = static_cast<uint8_t>(f.x);
        e.y0 = static_cast<uint8_t>(f.y);
        e.x1 = static_cast<uint8_t>(ax < 0 ? f.x : ax);
        e.y1 = static_cast<uint8_t>(ay < 0 ? f.y : ay);
        e.sound = static_cast<uint8_t>(sp == d->prof->sound.fireball_spell ? sound::kFireball
                                       : bolt                              ? sound::kLightning
                                                                           : sound::kSpell);
        fg->page[first_page].fx[0] = e;
    }
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
        if (out[k] >= 1000) {
            add_page(out[k] - 1000, fw(profile::kIsDestroyed));
            page_fx_set(1, skull_fx(out[k] - 1000));
        } else {
            add_page(out[k], fw(profile::kIsTurned));
            page_fx_set(1, hit_fx(out[k], true));
        }
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
        if (fg->fx_on) {                // a tap cuts the pictures short and moves on
            fx_end(c);
            fg->fx_at = 2;
        }
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
    case FSt::Ai: {
        // A tap takes the party back from Quick (the original's Space): every
        // player character runs by the player again; one acting now gets
        // the menu at once
        bool any = false;
        for (int k = 0; k < fg->b.n; ++k) {
            combat::Fighter& o = fg->b.f[k];
            if (o.member < 0 || o.rec[0xF7] >= 0x80 || o.status() == party::Animated || !o.quick || o.fleeing)
                continue;
            o.quick = false;
            any = true;
        }
        if (any && fg->cur >= 0 && fg->b.f[fg->cur].member >= 0 && !fg->b.f[fg->cur].quick && fg->b.f[fg->cur].up())
            fight_menu(c);
        return;
    }
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
        case 'A':
            fg->spell = 0;              // (a weapon's aim)
            fg->use_item = -1;
            start_aim(c);
            break;
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
            say(c, FSt::Next);            // (the turn is over: not an attack)
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
                say(c, FSt::Next);            // (the turn is over: not an attack)
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
            strike(c, fg->pending);
            fg->pending = -1;
            say(c, FSt::Turn);
            return;
        }
        fg->pending = -1;
        fight_menu(c);
        return;
    }
    case FSt::FlameAsk: {
        if (y < text::kMenuTapTop) return;
        const char k = text::key(menu, text::hit(menu, col));
        if (k != 'H' && k != 'C') return;
        fg->b.flame = k == 'H' ? 1 : 2;
        cast_now(c);
        return;
    }
    case FSt::FlameAbort: {
        if (y < text::kMenuTapTop) return;
        const char k = text::key(menu, text::hit(menu, col));
        if (k == 'N') {
            flame_menu(c);
            return;
        }
        if (k != 'Y') return;
        // The spell is gone (an item's use) and the turn over
        if (fg->use_item >= 0) spend_item(fg->cur, fg->spell);
        else forget_spell(fg->b.f[fg->cur], fg->spell);
        fg->spell = 0;
        fg->use_item = -1;
        add_page(fg->cur, fw(profile::kSpellAborted));
        draw_fight(c);
        say(c, FSt::Next);
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
        if (fg->use_item >= 0) spend_item(fg->cur, fg->spell);
        else forget_spell(fg->b.f[fg->cur], fg->spell);
        fg->spell = 0;
        fg->use_item = -1;
        fg->b.f[fg->cur].spell = 0;
        add_page(fg->cur, fw(profile::kSpellAborted));
        draw_fight(c);
        say(c, FSt::Next);
        return;
    }
    case FSt::ContinueAsk: {
        if (y < text::kMenuTapTop) return;
        const char k = text::key(menu, text::hit(menu, col));
        if (k != 'Y' && k != 'N') return;
        clear_menu_line(c);
        fg->b.no_action = fg->b.round + 15;
        fg->continuing = k == 'Y';
        if (k == 'Y') begin_round(c);
        else end_fight(c);
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
        say(c, FSt::Next);            // (the turn is over: not an attack)
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
        aim_exit(c);
        return true;
    case FSt::DoneMenu: fight_menu(c); return true;
    case FSt::Speed: done_menu(c); return true;
    case FSt::AllyAsk:
    case FSt::FleeAsk: fight_menu(c); return true;
    case FSt::AbortAsk: aim_menu(c); return true;
    case FSt::ContinueAsk:
        clear_menu_line(c);
        end_fight(c);                   // (Esc: No)
        return true;
    case FSt::FlameAsk:
        fg->st = FSt::FlameAbort;
        text::build(menu, fw(profile::kAbortSpellQ), fw(profile::kYesNoF));
        menu.selected = 1;
        show_menu_line(c);
        return true;
    case FSt::FlameAbort: flame_menu(c); return true;
    default: return true;               // (no leaving a fight)
    }
}

// The pad in a fight: steps (Move) - 8 north, 7 / 9 north-west / north-east,
// 4 / 6 west / east, 2 south
bool fight_moving() { return fg && screen == Screen::Fight && fg->st == FSt::Move; }
void fight_move_prompt(pic::Canvas& c) { move_prompt(c); }

bool fight_way(int dir, pic::Canvas& c);

bool fight_act(Act a, pic::Canvas& c)
{
    if (!fg || (fg->st != FSt::Move && !(fg->st == FSt::Aim && fg->manual))) return false;
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
    return fight_way(dir, c);
}

// A direction (0-7) in Move or Manual aim
bool fight_way(int dir, pic::Canvas& c)
{
    if (!fg || dir < 0 || dir > 7 || (fg->st != FSt::Move && !(fg->st == FSt::Aim && fg->manual))) return false;
    if (fg->st == FSt::Aim) {
        // Manual aim: the pad moves the square (the original's cursor keys)
        static const int kDx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, kDy[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
        const int x = fg->mx + kDx[dir], y = fg->my + kDy[dir];
        if (x >= 0 && x < combat::kW && y >= 0 && y < combat::kH) {
            fg->mx = x;
            fg->my = y;
        }
        aim_menu(c);
        return true;
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
        combat::recount_attacks(fg->b, fg->cur);       // (another weapon readied: its attacks)
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

int detect_spell();
void treasure_menu(pic::Canvas& c);

// Detect: the spell cast as in camp - out of memory, its effect on the caster
void treasure_detect(pic::Canvas& c)
{
    party::Character* ch = pt->sel();
    const int sp = detect_spell();
    if (!ch || !sp || !load_magic()) return;
    magic::remove(*ch, sp);
    const int pw = spells::power(*ch, mrules->tables, sp);
    spells::add_affect(*ch, d->prof->items.detect[0], spells::lasts(mrules->tables, sp, pw), pw, false);
    // "NAME casts" / the spell, "NAME is affected"
    char nm[20], w1[16], t[48];
    ch->name(nm, sizeof nm);
    cw(profile::kCasts, w1, sizeof w1);
    text::clear(c, text::kTextArea);
    snprintf(t, sizeof t, "%s %s", nm, w1);
    put(c, t, 1, 18, 10);
    put(c, spell_name(sp), 1, 19, 10);
    snprintf(t, sizeof t, "%s %s", nm, tw(profile::kIsAffectedW));
    put(c, t, 1, 21, 10);
    dirty_rows(17, 22);
    names->detect = true;
    treasure_menu(c);
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

// Detect on the treasure screen (curse_finish_facts.md 4): the selected
// member's first memorized Detect Magic (in the list's order); 0 none
int detect_spell()
{
    const party::Character* ch = pt->sel();
    const uint8_t* ds = d->prof->items.detect;
    if (!ch || !ch->rec[0x196] || !ds[0]) return 0;
    for (int k = 0; k < magic::kListSize; ++k) {
        const int s = ch->rec[magic::kListAt + k];
        if (s && !(s & 0x80) && (s == ds[1] || s == ds[2] || s == ds[3])) return s;
    }
    return 0;
}

void treasure_menu(pic::Canvas& c)
{
    char words[48];
    char a[32], b2[16];
    if (ground->any_money() || ground->n) {
        snprintf(a, sizeof a, "%s", tw(ground->any_money() ? profile::kViewTakePoolShare : profile::kViewTakePool));
        // " Detect Exit" with items there and a Detect Magic memorized by the one selected
        snprintf(b2, sizeof b2, "%s", tw(ground->n && detect_spell() ? profile::kDetectExit : profile::kExitSp));
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

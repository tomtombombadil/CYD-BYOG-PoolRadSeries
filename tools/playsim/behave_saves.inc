// Behaviour checks: saved games (included by sweep.cpp with -DBEHAVE_SAVES).
// Saving and loading through the game's own path (camp -> Save, the party
// menu's Load) must keep everything the original's save keeps (coab's
// SaveGame / loadSaveGame / SavePlayer / import_char01: the game area, the
// script's memory 0x4B00 / 0x7C00 / 0x7A00 and running block, position,
// facing, wall sets, the characters' records, items and effects), in the
// original's file layout - so either game can load the other's saves.
#include "behave_oracle.inc"

namespace bsav {

using namespace play;
const char* P = "saves";

// The Play Test opened (with the engine's own folder beside the saves, as
// on the board: /GOLDBOX/_CYD/<folder>) and saved game `slot` loaded - not
// begun (what the load itself read), or begun (BEGIN, then pumped)
bool start(char slot, bool begin)
{
    play::close();
    const std::string& host = game_dir;
    const size_t s = host.rfind('/');
    fs::sim_root() = host.substr(0, s);
    static std::string folder, cache;
    folder = host.substr(s + 1);
    cache = "/GOLDBOX/_CYD/" + folder;
    if (play::open(folder.c_str(), games::Game::CurseOfTheAzureBonds, C, cache.c_str())) return false;
    for (int guard = 0; play::screen == play::Screen::Title && guard < 40; ++guard) {
        if (play::title_run && play::title_run->prompt) {
            tap_word('P');
            continue;
        }
        play::tap(2, 2, C);
    }
    play::tap(24, (12 + pm_line('L')) * 8 + 2, C);
    const char* p = strchr(play::pm_saves, slot);
    if (!p) return false;
    tap_item(static_cast<int>(p - play::pm_saves));
    if (play::pt->count <= 0) return false;
    if (!begin) return true;
    play::tap(24, (12 + pm_line('B')) * 8 + 2, C);
    return pump();
}
bool load_only(char slot) { return start(slot, false); }

bool file(const char* name, std::vector<uint8_t>& out) { return slurp(name, out); }

// Differences between two byte runs, listed (at most a few)
std::string diffs(const uint8_t* a, const uint8_t* b, size_t n, const char* what, size_t base = 0)
{
    std::string s;
    int k = 0;
    for (size_t o = 0; o < n; ++o)
        if (a[o] != b[o]) {
            if (k++ < 6) {
                char t[64];
                snprintf(t, sizeof t, " %s+%03zX:%d/%d", what, o + base, a[o], b[o]);
                s += t;
            }
        }
    if (k > 6) s += " (" + std::to_string(k) + " in all)";
    return s;
}

// ---- 1. The GOG sample save (written by the original): the engine reads the
// fields the original's load reads, where it reads them
void sample_load()
{
    std::vector<uint8_t> sav;
    if (!file("SAVGAMA.DAT", sav)) {
        expect(P, "the sample save", false);
        return;
    }
    const auto* sz = b_row("save_sizes", 13149);
    expect(P, "the original's SAVGAMA.DAT size", sz && sav.size() == static_cast<size_t>((*sz)[0]), "%zu", sav.size());
    if (!load_only('A')) {
        expect(P, "load A", false);
        return;
    }
    const ecl::GameState& g = d->gs;
    // the layout (coab loadSaveGame): area byte, area1 0x800, area2 0x800, 0x400, script 0x1E00,
    // x, y, facing, wall type, roof, last state, state, 3 x (block, set), count, 8 x 41 names
    size_t o = 0;
    expect(P, "load: the game area (byte 0)", g.game_area == sav[o], "%d / %d", g.game_area, sav[o]);
    o = 1;
    std::string bad = diffs(g.area1, &sav[o], 0x800, "area1");
    o += 0x800;
    bad += diffs(g.area2, &sav[o], 0x800, "area2");
    o += 0x800;
    bad += diffs(g.table, &sav[o], 0x400, "table");
    o += 0x400;
    bad += diffs(g.code, &sav[o], 0x1E00, "script");
    o += 0x1E00;
    expect(P, "load: script memory, game data, table and running script as saved", bad.empty(), "%s", bad.c_str());
    expect(P, "load: position, facing, wall type, roof", g.x == sav[o] && g.y == sav[o + 1] && g.dir == sav[o + 2] &&
                                                             g.wall_ahead == sav[o + 3] && g.roof == sav[o + 4],
           "%d,%d dir %d wall %d roof %d / %d,%d %d %d %d", g.x, g.y, g.dir, g.wall_ahead, g.roof, sav[o], sav[o + 1], sav[o + 2],
           sav[o + 3], sav[o + 4]);
    o += 5;
    expect(P, "load: the states", d->save.last_state == sav[o] && d->save.state == sav[o + 1], "%d %d", d->save.last_state,
           d->save.state);
    o += 2;
    bool wok = true;
    for (int i = 0; i < 3; ++i)
        wok = wok && d->save.wall_block[i] == static_cast<int16_t>(sav[o + i * 4] | sav[o + i * 4 + 1] << 8) &&
              d->save.wall_set[i] == static_cast<int16_t>(sav[o + i * 4 + 2] | sav[o + i * 4 + 3] << 8);
    expect(P, "load: the wall sets", wok);
    o += 12;
    expect(P, "load: the party's size", pt->count == sav[o] && d->save.count == sav[o], "%d / %d", pt->count, sav[o]);
    ++o;
    // Each character: the record, items, effects as their files hold them
    for (int i = 0; i < pt->count; ++i) {
        char nm[48];
        const uint8_t* s = &sav[o + i * 41];
        snprintf(nm, sizeof nm, "%.*s", s[0], reinterpret_cast<const char*>(s + 1));
        std::vector<uint8_t> rec, swg, fx;
        char fn[64];
        snprintf(fn, sizeof fn, "%s.SAV", nm);
        file(fn, rec);
        snprintf(fn, sizeof fn, "%s.SWG", nm);
        file(fn, swg);
        snprintf(fn, sizeof fn, "%s.FX", nm);
        file(fn, fx);
        const party::Character& c = pt->m[i];
        char w[96];
        snprintf(w, sizeof w, "load: %s's record (422 bytes) as the file", nm);
        expect(P, w, rec.size() == party::kRecordSize && memcmp(c.rec, rec.data(), party::kRecordSize) == 0, "%s",
               rec.size() == party::kRecordSize ? diffs(c.rec, rec.data(), party::kRecordSize, "rec").c_str() : "size");
        snprintf(w, sizeof w, "load: %s's items (%zu) and effects (%zu)", nm, swg.size() / 63, fx.size() / 9);
        expect(P, w, c.n_items == static_cast<int>(swg.size() / 63) && c.n_affects == static_cast<int>(fx.size() / 9) &&
                         (swg.empty() || memcmp(c.items, swg.data(), swg.size()) == 0) &&
                         (fx.empty() || memcmp(c.affects, fx.data(), fx.size()) == 0),
               "%d items %d effects", c.n_items, c.n_affects);
        // The original recalculates on loading (import_char01: reclac_player_values,
        // ReclacClassBonuses); the engine's recalculation of the record must change nothing
        static party::Character r2;
        r2 = c;
        rules::recalc(r2, *names, d->facts);
        Making* m = nullptr;
        if (load_rules(m)) {
            classes::class_bonuses(r2, m->tables);
            rules::recalc(r2, *names, d->facts);
            delete m;
        }
        const std::string rb = diffs(r2.rec, c.rec, party::kRecordSize, "rec");
        snprintf(w, sizeof w, "load: %s recalculated (as the original's load does) - the same", nm);
        // (thief skills: the original's own +7 - see the classes checks)
        bool only_thief = true;
        for (int k = 0; k < party::kRecordSize; ++k)
            if (r2.rec[k] != c.rec[k] && !(k >= 0xEA && k <= 0xF1)) only_thief = false;
        expect(P, w, only_thief, "%s", rb.c_str());
    }
}

// ---- 2. A game changed in every way the save keeps: saved at camp, loaded
// in a fresh start, compared field by field
void round_trip()
{
    if (!start('A', true)) {
        expect(P, "boot A", false);
        return;
    }
    // Walk and turn
    for (int k = 0; k < 5; ++k) {
        play::act(k % 2 ? play::Act::Forward : play::Act::TurnRight, C);
        pump();
    }
    // The game's clock (area1 words 0x4BC7-0x4BCB: minutes, tens, hours, day, year)
    vm->advance_clock(1, 7);
    vm->advance_clock(3, 5);
    vm->take_minutes();
    // Script memory: area1, area2 (game data), the table, globals the scripts keep
    vm->set(0x4C05, 0x1234);
    vm->set(0x4EFE, 0xBEEF);
    vm->set(0x7E10, 0x5678);
    vm->set(0x7FFE, 0x0F0F);
    d->gs.table[0x123] = 0x5A;
    d->gs.table[0x3FF] = 0xA5;
    // The party: order swapped, money, health (unconscious / dead / stoned),
    // levels lost, memorized and being-memorized spells, an NPC, items with
    // charges / readied / cursed / identified, effects with their minutes
    std::swap(pt->m[0], pt->m[1]);
    party::Character& a = pt->m[0];
    party::Character& b = pt->m[1];
    for (int i = 0; i < 7; ++i) {
        a.rec[0xFB + i * 2] = static_cast<uint8_t>(11 + i);
        a.rec[0xFC + i * 2] = static_cast<uint8_t>(i);
    }
    b.rec[0x195] = party::Unconscious;
    b.rec[0x1A4] = 0;
    if (pt->count > 2) pt->m[2].rec[0x195] = party::Dead;
    if (pt->count > 3) pt->m[3].rec[0x195] = party::Stoned;
    a.rec[0xE7] = 1;
    a.rec[0xE8] = 6;
    a.rec[0x1E] = 0x15;
    a.rec[0x1F] = 0x0F | 0x80;
    if (pt->count > 4) pt->m[4].rec[0xF7] = 0x8A;            // an NPC's control byte
    // An item from the player's own files (the first with charges), changed
    {
        FILE* f = fopen(getenv("ITEMS_BIN") ? getenv("ITEMS_BIN") : "/tmp/claude-0/sw/items.bin", "rb");
        uint8_t it[63];
        bool got = false;
        while (f && fread(it, 1, 63, f) == 63)
            if (it[0x3C] > 0 || it[0x3D] > 0) {
                got = true;
                break;
            }
        if (f) fclose(f);
        if (got && a.n_items < party::kMaxItems) {
            it[0x34] = 1;                                   // readied
            it[0x36] = 1;                                   // cursed
            it[0x35] = 0;                                   // names known
            it[0x39] = 3;                                   // count / charges
            memcpy(a.items[a.n_items++], it, 63);
        }
        expect(P, "an item from the player's files to carry", got);
    }
    if (a.n_affects < party::kMaxAffects) {
        uint8_t* e = a.affects[a.n_affects++];
        memset(e, 0, party::kAffectSize);
        e[0] = 0x01;                                        // bless
        e[1] = 0x34;                                        // minutes left (u16)
        e[2] = 0x12;
        e[3] = 0x77;
    }
    note_seen('J', 17);
    note_seen('J', 42);
    // Keep what the game holds now
    static ecl::GameState g1;
    static party::Party p1;
    g1 = d->gs;
    p1 = *pt;
    const int x = d->gs.x, y = d->gs.y, dir = d->gs.dir, area = d->gs.game_area;
    const int blk[3] = {d->wall_block[0], d->wall_block[1], d->wall_block[2]};
    const int set[3] = {d->wall_set[0], d->wall_set[1], d->wall_set[2]};
    const int seen0 = n_seen;
    if (!save_to('C')) {
        expect(P, "saved at camp (slot C)", false);
        return;
    }
    // The file the camp wrote
    std::vector<uint8_t> sav;
    file("SAVGAMC.DAT", sav);
    expect(P, "camp save: SAVGAMC.DAT 13,149 bytes", sav.size() == 13149, "%zu", sav.size());
    if (sav.size() == 13149) {
        const size_t pos = 1 + 0x800 * 2 + 0x400 + 0x1E00;
        expect(P, "camp save: game area, position, facing where the original keeps them",
               sav[0] == area && sav[pos] == x && sav[pos + 1] == y && sav[pos + 2] == dir, "%d %d,%d %d", sav[0], sav[pos], sav[pos + 1],
               sav[pos + 2]);
        expect(P, "camp save: the state (camping: 2, as the original's game_state)", sav[pos + 6] == 2, "%d", sav[pos + 6]);
        bool names_ok = sav[pos + 19] == p1.count;
        for (int i = 0; i < p1.count; ++i) {
            char want[16];
            snprintf(want, sizeof want, "CHRDATC%d", i + 1);
            const uint8_t* s = &sav[pos + 20 + i * 41];
            names_ok = names_ok && s[0] == strlen(want) && memcmp(s + 1, want, s[0]) == 0;
        }
        expect(P, "camp save: the characters' file names (CHRDATC1 ..., 41-byte Pascal strings)", names_ok);
        for (int i = 0; i < p1.count; ++i) {
            char fn[32];
            std::vector<uint8_t> r, s, e;
            snprintf(fn, sizeof fn, "CHRDATC%d.SAV", i + 1);
            const bool hr = file(fn, r);
            snprintf(fn, sizeof fn, "CHRDATC%d.SWG", i + 1);
            const bool hs = file(fn, s);
            snprintf(fn, sizeof fn, "CHRDATC%d.FX", i + 1);
            const bool he = file(fn, e);
            const party::Character& c = p1.m[i];
            char w[96];
            snprintf(w, sizeof w, "camp save: member %d's files (422; 63 an item; 9 an effect; none when empty)", i + 1);
            expect(P, w, hr && r.size() == 422 && hs == (c.n_items > 0) && s.size() == static_cast<size_t>(c.n_items) * 63 &&
                             he == (c.n_affects > 0) && e.size() == static_cast<size_t>(c.n_affects) * 9,
                   "%zu %zu %zu", r.size(), s.size(), e.size());
        }
    }
    // A fresh start: the game loaded (not begun) as it was
    if (!load_only('C')) {
        expect(P, "load C", false);
        return;
    }
    const ecl::GameState& g2 = d->gs;
    expect(P, "load C: position, facing, area", g2.x == x && g2.y == y && g2.dir == dir && g2.game_area == area, "%d,%d %d %d", g2.x,
           g2.y, g2.dir, g2.game_area);
    std::string bad = diffs(g1.area1, g2.area1, 0x800, "area1") + diffs(g1.area2, g2.area2, 0x800, "area2") +
                      diffs(g1.table, g2.table, 0x400, "table") + diffs(g1.code, g2.code, 0x1E00, "script");
    expect(P, "load C: script memory (clock, globals), game data, table, running script", bad.empty(), "%s", bad.c_str());
    expect(P, "load C: the clock (0x4BC7-0x4BCB)", memcmp(g1.area1 + 0x18E, g2.area1 + 0x18E, 10) == 0);
    bool wall = true;
    for (int i = 0; i < 3; ++i) wall = wall && d->wall_block[i] == blk[i] && d->wall_set[i] == set[i];
    expect(P, "load C: the wall sets", wall);
    expect(P, "load C: the party's size and order", pt->count == p1.count && memcmp(pt->m[0].rec, p1.m[0].rec, 16) == 0 &&
                                                        memcmp(pt->m[1].rec, p1.m[1].rec, 16) == 0);
    for (int i = 0; i < pt->count && i < p1.count; ++i) {
        const party::Character& u = p1.m[i];
        const party::Character& v = pt->m[i];
        char w[96];
        snprintf(w, sizeof w, "load C: member %d's record, every byte", i + 1);
        const std::string rd = diffs(u.rec, v.rec, party::kRecordSize, "rec");
        expect(P, w, rd.empty(), "%s", rd.c_str());
        snprintf(w, sizeof w, "load C: member %d's items (%d) and effects (%d), every byte", i + 1, u.n_items, u.n_affects);
        expect(P, w, u.n_items == v.n_items && u.n_affects == v.n_affects &&
                         memcmp(u.items, v.items, sizeof u.items[0] * u.n_items) == 0 &&
                         memcmp(u.affects, v.affects, sizeof u.affects[0] * u.n_affects) == 0);
    }
    expect(P, "load C: health kept (unconscious, dead, stoned)",
           pt->m[1].health() == party::Unconscious && (pt->count < 3 || pt->m[2].health() == party::Dead) &&
               (pt->count < 4 || pt->m[3].health() == party::Stoned));
    expect(P, "load C: the journal entries met (the engine's own file)", n_seen == seen0, "%d / %d", n_seen, seen0);
    // Saved again straight away (party menu): the same bytes but the slot letter and the state
    save_from = Screen::PartyMenu;
    if (!save_game('D')) {
        expect(P, "save D", false);
        return;
    }
    std::vector<uint8_t> c1, c2;
    file("SAVGAMC.DAT", c1);
    file("SAVGAMD.DAT", c2);
    std::string fd;
    for (size_t k = 0; k < c1.size() && k < c2.size(); ++k)
        if (c1[k] != c2[k] && !(c1[k] == 'C' && c2[k] == 'D')) {
            char t[40];
            snprintf(t, sizeof t, " %04zX:%d/%d", k, c1[k], c2[k]);
            fd += t;
        }
    expect(P, "saved again: the same file (but the slot's letter and the state)", c1.size() == c2.size() && fd == " 3207:2/0",
           "%s", fd.c_str());
}

// ---- 3. The original's sample save loaded and saved again unchanged: the
// same bytes the original wrote (so each game reads the other's saves)
void resave_sample()
{
    if (!load_only('A')) {
        expect(P, "load A", false);
        return;
    }
    save_from = Screen::PartyMenu;
    if (!save_game('E')) {
        expect(P, "save E", false);
        return;
    }
    std::vector<uint8_t> a, e;
    file("SAVGAMA.DAT", a);
    file("SAVGAME.DAT", e);
    // (not compared: the "last state" byte 0x3206 - the original's load never
    // reads it, it takes the state byte for both; and a name slot's bytes past
    // its Pascal string - the original leaves whatever its buffer held)
    std::string fd;
    int n = 0;
    const size_t names = 1 + 0x800 * 2 + 0x400 + 0x1E00 + 5 + 2 + 12 + 1;
    for (size_t k = 0; k < a.size() && k < e.size(); ++k) {
        if (k == names - 15) continue;
        if (k >= names && k < names + 8 * 41) {
            const size_t slot = (k - names) / 41, at = (k - names) % 41;
            if (slot >= a[names - 1] || at > a[names + slot * 41]) continue;     // (unused slots too)
        }
        if (a[k] != e[k] && !(a[k] == 'A' && e[k] == 'E')) {
            if (n++ < 8) {
                char t[40];
                snprintf(t, sizeof t, " %04zX:%d/%d", k, a[k], e[k]);
                fd += t;
            }
        }
    }
    expect(P, "the sample save loaded and saved: the original's bytes", a.size() == e.size() && n == 0, "%s", fd.c_str());
    for (int i = 1; i <= pt->count; ++i)
        for (const char* ext : {"SAV", "SWG", "FX"}) {
            char f1[24], f2[24];
            snprintf(f1, sizeof f1, "CHRDATA%d.%s", i, ext);
            snprintf(f2, sizeof f2, "CHRDATE%d.%s", i, ext);
            std::vector<uint8_t> x1, x2;
            const bool h1 = file(f1, x1), h2 = file(f2, x2);
            char w[64];
            snprintf(w, sizeof w, "the sample's %s saved again: the same", f1);
            expect(P, w, h1 == h2 && x1 == x2, "%s %s", h1 ? "" : "(none)", h2 ? "" : "(none written)");
        }
}

} // namespace bsav

static void behave_saves()
{
    using namespace bsav;
    if (!b_oracle_load("classes")) {
        expect(P, "oracle file", false, "no oracle_classes.txt");
        return;
    }
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("sample")) sample_load();
    if (want("resave")) resave_sample();
    if (want("trip")) round_trip();
}

// Game sweeps (Claude's side, Linux): the Play Test driven through every
// spell, monster, item, class and script of the player's own game files,
// to find crashes (build with -fsanitize=address,undefined), hangs and
// wrong results. Nothing from the games is stored: reports go to stdout.
// Build from the repo root:
//   g++ -std=c++17 -O1 -g -fsanitize=address,undefined -Itools/playsim/shim -Isrc
//       -o sweep tools/playsim/sweep.cpp src/engine/*.cpp
// Run: sweep <game folder> <mode> [args]; modes:
//   spells           every fight spell cast by a party member at 8AC2's fight (save A)
//   campspells       every camp spell cast while exploring
//   monsters [a]     every monster of MON<a>CHA (all areas without a) fought at 8AC2
//   items <file>     every item record in <file> (63-byte records, made by the
//                    sweep's helper from the player's ITEM / MON*ITM files):
//                    readied, put away (the record must come back the same), used
//   classes          every race / class / alignment made, trained to the top
//   events [a]       every ECL block of area a (all without): its first run, then a
//                    step from every square in every direction, menus answered at random
//   saveload         save, load, save again: the files and the state must match
//   monkey [seed [n]] n random taps, keys, moves and cursor keys anywhere, checking
//                    the party's invariants and screens that can't be left
//                    (MAREA / MBLOCK: start in that script block)
// Env: BLOCK (events: one block), FIGHTLOG / PAGELOG / TURNLOG / STLOG (a fight's
// progress), AIMLOG, ENTRIES, ONLY. Each run saves over the slots: give each
// its own copy of the game folder.
#include "ui/play.cpp"
#include "engine/rules.h"
#include <cstdarg>
#include <random>
#include <string>
#include <vector>

void audio_play(AudioFill fill, void* ctx)
{
    uint8_t b[512];
    for (int k = 0; k < 50 && fill(b, 512, ctx); ++k) {}
}
void audio_stop() {}
bool audio_running() { return false; }
uint32_t g_now = 1000;
SerialT Serial;
static std::vector<uint8_t> px(320 * 200);
static pic::Canvas C{px.data(), 320, 200};
static std::mt19937 rnd(12345);
static int pick(int n) { return n > 0 ? static_cast<int>(rnd() % static_cast<unsigned>(n)) : 0; }
static std::string game_dir;
static int problems = 0;

static void problem(const char* what)
{
    ++problems;
    printf("!! PROBLEM: %s\n", what);
}

static bool tap_word(char k)
{
    for (int i = 0; i < play::menu.count; ++i)
        if (text::key(play::menu, i) == k) {
            play::tap((static_cast<int>(strlen(play::menu.prompt)) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C);
            return true;
        }
    return false;
}

static void tap_item(int i)
{
    if (i < 0 || i >= play::menu.count) i = 0;
    const int col = play::menu.count ? static_cast<int>(strlen(play::menu.prompt)) + play::menu.start[i] : 0;
    play::tap(col * 8 + 2, text::kMenuRow * 8 + 2, C);
}

static int pm_line(char k)
{
    for (int i = 0; i < play::pm_lines; ++i)
        if (play::pm_key(play::pm_item[i]) == k) return i;
    return -1;
}

// What the fight's turn-taker does: `on_menu` decides (default Quick)
static int (*on_menu)() = nullptr;

// Drives the game until the player is free to move (true), the game is
// over (false), or it seems stuck (false, reported)
static const char* stuck_why = "";
static bool god = false;          // (events: the party can't fall - the scripts are what's tested)
static bool pump(int max_iter = 400000)
{
    using namespace play;
    int same = 0;
    int last_sig = -1;
    for (int it = 0; it < max_iter; ++it) {
        g_now += 20;
        play::tick(g_now, C);
        {
            char jk;
            int jn;
            play::journal_request(&jk, &jn);
        }
        if (!d) return false;
        if (getenv("FIGHTLOG") && fg && it % 10000 == 0) {
            const combat::Fighter& cf = fg->b.f[fg->cur < 0 ? 0 : fg->cur];
            printf("  [it %d round %d cur %d (%s team %d hp %d at %d,%d) st %d n %d]\n", it, fg->b.round, fg->cur,
                   (const char*)&cf.rec[0], cf.team(), cf.hp(), cf.x, cf.y, (int)fg->st, fg->b.n);
        }
        if (getenv("STLOG") && fg && fg->b.round >= 20 && fg->b.round <= 21) {
            static int lc = -9, ls = -9, lp = -9;
            if (fg->cur != lc || (int)fg->st != ls || fg->pages != lp) {
                lc = fg->cur;
                ls = (int)fg->st;
                lp = fg->pages;
                const combat::Fighter& f = fg->b.f[fg->cur < 0 ? 0 : fg->cur];
                printf("   st: cur %d st %d pages %d at %d atk %d/%d moves %d delay %d menu [%s%s]\n", fg->cur, ls, fg->pages, fg->at,
                       f.attacks[0], f.attacks[1], f.moves, f.delay, menu.prompt, menu.s);
            }
        }
        if (getenv("TURNLOG") && fg && fg->b.round == 20) {
            static int last_cur = -2;
            if (fg->cur != last_cur && fg->cur >= 0) {
                last_cur = fg->cur;
                const combat::Fighter& f = fg->b.f[fg->cur];
                printf("   turn %d %s team %d st %d up %d status %d at %d,%d moves %d atk %d/%d quick %d guard %d target %d delay %d naff %d:",
                       fg->cur, (const char*)&f.rec[0], f.team(), (int)fg->st, (int)f.up(), f.status(), f.x, f.y, f.moves,
                       f.attacks[0], f.attacks[1], (int)f.quick, (int)f.guarding, f.target, f.delay, f.n_aff ? *f.n_aff : -1);
                for (int k = 0; f.n_aff && k < *f.n_aff; ++k) printf(" %02X", f.aff[k][0]);
                printf("\n");
                static bool listed = false;
                if (!listed) {
                    listed = true;
                    for (int k = 0; k < fg->b.n; ++k) {
                        const combat::Fighter& o = fg->b.f[k];
                        printf("     #%d team %d at %d,%d size %d up %d hp %d gone %d\n", k, o.team(), o.x, o.y, o.size, (int)o.up(), o.hp(), (int)o.gone);
                    }
                }
                {
                    combat::Battle copy = fg->b;
                    create::Dice dd = rng;
                    const combat::Plan pl = combat::think(copy, fg->t, fg->cur, dd);
                    printf("     plan act %d target %d dir %d missile %d\n", (int)pl.act, pl.target, pl.dir, (int)pl.missile);
                }
            }
        }
        if (getenv("PAGELOG")) {
            static int last_at = -1, last_n = -1;
            static bool was = false;
            const bool in_pages = fg && fg->st == FSt::Pages;
            if (in_pages && (!was || fg->at != last_at || fg->pages != last_n)) {
                last_at = fg->at;
                last_n = fg->pages;
                if (fg->at < fg->pages) {
                    printf("   r%d:", fg->b.round);
                    for (int l = 0; l < fg->page[fg->at].lines; ++l) printf(" %s /", fg->page[fg->at].line[l]);
                    printf("\n");
                }
            }
            was = in_pages;
        }
        if (god && pt)
            for (int i = 0; i < pt->count; ++i) {
                party::Character& m = pt->m[i];
                if (m.health() == party::Okay && m.hp() < m.hp_max()) m.rec[0x1A4] = m.rec[0x78];
            }
        if (input_mode != Input::None) {
            const char* t = input_mode == Input::Number ? "1" : "A";
            for (const char* q = t; *q; ++q) play::input_key(*q, C);
            play::input_key('\n', C);
            continue;
        }
        const int sig = static_cast<int>(screen) * 1000 + (fg ? static_cast<int>(fg->st) * 10 : 0) + (waiting ? 1 : 0);
        if (sig == last_sig) ++same;
        else same = 0;
        last_sig = sig;
        if (same > 60000) {
            char t[160];
            snprintf(t, sizeof t, "stuck: screen %d fight st %d waiting %d wait %d then %d menu [%s%s]", (int)screen,
                     fg ? (int)fg->st : -1, (int)waiting, vm ? (int)vm->wait() : -1, (int)then, menu.prompt, menu.s);
            stuck_why = "stuck";
            problem(t);
            return false;
        }
        if (it % 50) continue;          // (time passes between the answers)
        switch (screen) {
        case Screen::Game:
            if (note_until && note_held) {
                play::tap(2, 2, C);
                continue;
            }
            if (then == Then::Door) {
                play::back(C);
                continue;
            }
            if (!waiting) {
                if (then == Then::Idle && !note_until) return true;
                continue;
            }
            switch (vm->wait()) {
            case ecl::Wait::Print:
                if (page_prompt) play::tap(10, 10, C);
                break;
            case ecl::Wait::Menu:
                if (anim_block >= 0 || true) tap_item(vm->items() > 1 ? pick(vm->items()) : 0);
                break;
            case ecl::Wait::ListMenu:
                if (list_wait) play::tap(12, (list_row0 + pick(vm->items())) * 8 + 2, C);
                else if (page_prompt) play::tap(10, 10, C);
                break;
            case ecl::Wait::Who:
                play::tap(17 * 8 + 2, (4 + pick(pt->count)) * 8 + 2, C);
                tap_word('S');
                break;
            case ecl::Wait::Key: play::tap(10, 10, C); break;
            default: break;
            }
            break;
        case Screen::Fight:
            if (!fg) break;
            switch (fg->st) {
            case FSt::Menu: {
                const int r = on_menu ? on_menu() : 0;
                if (r == 0) tap_word('Q');
                break;
            }
            case FSt::Aim: {
                // Target, Next and Target again; Exit only after many tries
                // (one creature picked again and again would never end it)
                static int tries = 0;
                if (same == 0) tries = 0;
                ++tries;
                if (getenv("AIMLOG") && tries < 8) {
                    const combat::Fighter& af = fg->b.f[fg->cur];
                    const int ct = fg->manual ? -1 : fg->cand[fg->ci];
                    int sq = -1;
                    if (ct >= 0) combat::range(fg->b, fg->t, fg->cur, ct, false, &sq);
                    printf("  [aim %d: %s%s cand %d (%d of %d) sq %d at %d,%d -> %d,%d spell %02X]\n", tries, menu.prompt, menu.s, ct, fg->ci,
                           fg->n_cand, sq, af.x, af.y, ct >= 0 ? fg->b.f[ct].x : -1, ct >= 0 ? fg->b.f[ct].y : -1, fg->spell);
                }
                if (tries > 40) tap_word('E');
                else if (tries % 3 == 0 || !tap_word('T')) tap_word('N');
                break;
            }
            case FSt::AbortAsk: tap_word('Y'); break;
            case FSt::ContinueAsk: tap_word('N'); break;
            case FSt::FlameAsk: tap_word(pick(2) ? 'H' : 'C'); break;
            case FSt::AllyAsk:
            case FSt::FleeAsk: tap_word('N'); break;
            case FSt::DoneMenu: tap_word('E'); break;
            case FSt::Results:
            case FSt::Destroyed: play::tap(2, 2, C); break;
            default: break;
            }
            break;
        case Screen::Won: play::tap(100, 100, C); break;
        case Screen::Title: return false;      // (the party is gone: the title again)
        case Screen::PartyMenu:
            if (in_game_menu && pt->count) {
                play::tap(24, (12 + pm_line('B')) * 8 + 2, C);
                break;
            }
            return false;
        case Screen::Cast:
            if (cr.stage == CastRun::Whom) {
                play::tap((cr.on_camp ? 17 : 1) * 8 + 2, (4 + 1) * 8 + 2, C);     // member 1
                tap_word('S');
            }
            else if (cr.stage == CastRun::Flame) tap_word(pick(2) ? 'H' : 'C');
            else if (cr.stage == CastRun::FlameAbort) tap_word('N');
            break;
        case Screen::Rest:
            break;
        default:
            if (!play::back(C)) {
                char t[80];
                snprintf(t, sizeof t, "Esc does nothing on screen %d", (int)screen);
                problem(t);
                return false;
            }
            break;
        }
    }
    {
        char t[200];
        snprintf(t, sizeof t, "pump: out of time (screen %d fight st %d round %d party %d waiting %d wait %d then %d menu [%s%s])",
                 (int)screen, fg ? (int)fg->st : -1, fg ? fg->b.round : -1, pt ? pt->count : -1, (int)waiting,
                 vm ? (int)vm->wait() : -1, (int)then, menu.prompt, menu.s);
        problem(t);
    }
    return false;
}

// The Play Test opened, the title passed, saved game `slot` loaded, BEGIN
static bool boot(char slot = 'A')
{
    play::close();
    const std::string& host = game_dir;
    const size_t s = host.rfind('/');
    fs::sim_root() = host.substr(0, s);
    static std::string folder;
    folder = host.substr(s + 1);
    const char* e = play::open(folder.c_str(), games::Game::CurseOfTheAzureBonds, C, nullptr);
    if (e) {
        printf("open: %s\n", e);
        return false;
    }
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
    if (!play::pt->count) return false;
    play::tap(24, (12 + pm_line('B')) * 8 + 2, C);
    return pump();
}

// The fight at 8AC2 (save A's inn): RUNAT as the playsim does
static bool start_fight(int area = 0, int mon = 0, int copies = 0)
{
    const uint16_t a = 0x8AC2;
    if (mon) {
        play::d->gs.game_area = static_cast<uint8_t>(area);
        play::vm->set(a + 2, static_cast<uint16_t>(mon));
        if (copies) play::vm->set(a + 4, static_cast<uint16_t>(copies));
        play::vm->set(a + 6, static_cast<uint16_t>(mon));
    }
    play::then = play::Then::Idle;
    play::handle(play::vm->run(a));
    for (int k = 0; k < 400 && play::screen != play::Screen::Fight; ++k) {
        g_now += 20;
        play::tick(g_now, C);
        if (play::waiting && play::vm->wait() == ecl::Wait::Menu) tap_item(0);
        if (play::waiting && play::vm->wait() == ecl::Wait::Print && play::page_prompt) play::tap(10, 10, C);
    }
    return play::screen == play::Screen::Fight;
}

static void party_line(const char* tag)
{
    printf("  %s:", tag);
    for (int i = 0; play::pt && i < play::pt->count; ++i)
        printf(" %d/%d%s", play::pt->m[i].hp(), play::pt->m[i].hp_max(),
               play::pt->m[i].health() == party::Okay ? "" : play::pt->m[i].health() == party::Dead ? "x" : "*");
    printf("\n");
}

// ---- spells
static int sweep_spell = 0;
static bool spell_cast = false;
static int cast_spell_menu()
{
    using namespace play;
    combat::Fighter& f = fg->b.f[fg->cur];
    if (spell_cast || f.member < 0) return 0;
    if (!load_magic()) return 0;
    // A caster of the spell's kind: clerics' by a cleric, magic-users' by a
    // magic-user (the level counts); the items' and the druids' by anyone
    const int kind = mrules->tables.spell_class(sweep_spell);
    const party::Character& ch = pt->m[f.member];
    if ((kind == 0 && ch.level(classes::Cleric) == 0) || (kind == 2 && ch.level(classes::MagicUser) == 0)) return 0;
    spell_cast = true;
    printf("  %s casts %02X\n", "member", sweep_spell);
    f.rec[magic::kListAt] = static_cast<uint8_t>(sweep_spell);
    fight_spell_chosen(sweep_spell, C);
    return 1;
}

static void sweep_spells()
{
    for (int sp = 1; sp <= 0x64; ++sp) {
        if (getenv("ONLY") && sp != static_cast<int>(strtol(getenv("ONLY"), nullptr, 16))) continue;
        bool known = false;
        if (!boot()) {
            problem("boot");
            return;
        }
        for (int i = 0; i < play::d->prof->fight.n_spells; ++i)
            if (play::d->prof->fight.spells[i].spell == sp) known = true;
        if (!known) continue;
        printf("== spell %02X\n", sp);
        if (!start_fight()) {
            problem("no fight");
            continue;
        }
        sweep_spell = sp;
        spell_cast = false;
        on_menu = cast_spell_menu;
        play::log_pages = true;
        pump();
        on_menu = nullptr;
        play::log_pages = false;
        if (!spell_cast) problem("never cast");
        party_line("after");
    }
}

// ---- camp spells
static void sweep_campspells()
{
    if (!boot()) return;
    const auto& m = play::d->prof->magic;
    for (int k = 0; k < m.n_camp; ++k) {
        const int sp = m.camp[k].spell;
        if (!boot()) return;
        using namespace play;
        if (!load_magic()) {
            problem("magic");
            return;
        }
        const spells::Entry e = spells::entry(mrules->tables, sp);
        printf("== camp spell %02X (targets %d)\n", sp, e.targets);
        // The caster: a magic-user for theirs (PHILIPPE), else a cleric (SHARA)
        const int who = mrules->tables.spell_class(sp) == 2 ? 5 : 4;
        pt->selected = who;
        pt->m[who].rec[magic::kListAt] = static_cast<uint8_t>(sp);
        // member 1 hurt, for the cures to find (and the one picked)
        pt->m[1].rec[0x1A4] = 3;
        cr = CastRun{};
        cr.caster = who;
        screen = Screen::SpellList;
        choose_spell(sp, C);
        pump();
        party_line("after");
        for (int i = 0; i < pt->count; ++i) {
            const party::Character& ch = pt->m[i];
            printf("   fx%d:", i);
            for (int a = 0; a < ch.n_affects; ++a) printf(" %02X/%d/%d", ch.affects[a][0], ch.affects[a][1] | ch.affects[a][2] << 8, ch.affects[a][3]);
            printf("\n");
        }
    }
}

// ---- monsters
static void sweep_monsters(int only)
{
    for (int area = 1; area <= 6; ++area) {
        if (only && area != only) continue;
        char path[300];
        snprintf(path, sizeof path, "%s/MON%dCHA.DAX", game_dir.c_str(), area);
        snprintf(path, sizeof path, "/GOLDBOX/%s/MON%dCHA.DAX", game_dir.substr(game_dir.rfind('/') + 1).c_str(), area);
        fs::File f = sd_fs().open(path, "r");
        if (!f) {
            printf("no %s\n", path);
            continue;
        }
        library::FileSource src(f);
        static dax::Index dir;
        std::vector<int> ids;
        if (dax::read_index(src, dir) == dax::Status::Ok)
            for (int i = 0; i < dir.count; ++i) ids.push_back(dir.entries[i].id);
        f.close();
        for (int id : ids) {
            if (!boot()) return;
            printf("== monster MON%d #%d\n", area, id);
            if (!start_fight(area, id, 4)) {
                problem("no fight");
                continue;
            }
            play::log_pages = true;
            const bool ok = pump();
            play::log_pages = false;
            printf("  fight %s, round %d result %d\n", ok ? "over" : "not over", play::fg ? play::fg->b.round : -1,
                   play::vm ? play::vm->get(0x7EC7) : -1);
            party_line("party");
        }
    }
}

// ---- items
static void sweep_items(const char* file)
{
    FILE* in = fopen(file, "rb");
    if (!in) {
        printf("no %s\n", file);
        return;
    }
    std::vector<uint8_t> all;
    uint8_t b[63];
    while (fread(b, 1, 63, in) == 63) all.insert(all.end(), b, b + 63);
    fclose(in);
    const int n = static_cast<int>(all.size() / 63);
    if (!boot()) return;
    using namespace play;
    for (int k = 0; k < n; ++k) {
        if (k % 20 == 0 && !boot()) return;
        party::Character& ch = pt->m[k % pt->count];
        pt->selected = k % pt->count;
        if (ch.n_items >= party::kMaxItems) ch.n_items = 0;
        const int i = ch.n_items;
        if (!rules::add_item(ch, &all[k * 63])) continue;
        ch.items[i][0x34] = 0;
        ch.items[i][0x36] = 0;                  // (not cursed: it has to come off again)
        rules::recalc(ch, *names, d->facts);
        static party::Character before;
        before = ch;
        char nm[48];
        names->name(items::Item{ch.items[i]}, nm, sizeof nm);
        printf("== item %d: %s (type %d, %02X %02X %02X)\n", k, nm, ch.items[i][0x2E], ch.items[i][0x3C], ch.items[i][0x3D],
               ch.items[i][0x3E]);
        view_from = Screen::Game;
        screen = Screen::Items;
        ready_item(i, C);
        const bool on = ch.items[i][0x34] != 0;
        printf("  readied %d: AC %d THAC0 %d Str %d/%d Dex %d Con %d HP %d/%d fx %d\n", (int)on, ch.ac(), ch.thac0(), ch.stat(0),
               ch.str00(), ch.stat(3), ch.stat(4), ch.hp(), ch.hp_max(), ch.n_affects);
        if (note_until) redraw_menu(C);
        if (on) {
            ready_item(i, C);
            if (note_until) redraw_menu(C);
            rules::recalc(ch, *names, d->facts);
            // The same again (but the spell slots a ring may leave, as the original)
            for (int o = 0; o < party::kRecordSize; ++o)
                if (ch.rec[o] != before.rec[o] && !(o >= 0x12D && o <= 0x13B) && !(o >= 0x1E && o < 0x1E + 84)) {
                    char t[96];
                    snprintf(t, sizeof t, "item %d: record byte %03X %d -> %d after ready + put away", k, o, before.rec[o], ch.rec[o]);
                    problem(t);
                }
            if (ch.n_affects != before.n_affects) problem("effects differ after ready + put away");
            // Use it (readied again)
            ready_item(i, C);
            if (note_until) redraw_menu(C);
            if (ch.items[i][0x34]) {
                use_item(i, C);
                pump(60000);
                printf("  used: items %d HP %d/%d fx %d\n", ch.n_items, ch.hp(), ch.hp_max(), ch.n_affects);
            }
        }
        screen = Screen::Game;
    }
}

// ---- classes
static void sweep_classes()
{
    if (!boot()) return;
    using namespace play;
    Making* m = nullptr;
    if (!load_rules(m)) {
        problem("rules");
        return;
    }
    int races[16];
    const int nr = create::races(races, 16);
    for (int ri = 0; ri < nr; ++ri) {
        int cl[32];
        const int nc = create::classes_for(m->tables, races[ri], cl, 32);
        for (int ci = 0; ci < nc; ++ci) {
            int al[16];
            const int na = create::alignments_for(m->tables, cl[ci], al, 16);
            for (int ai = 0; ai < na; ++ai)
                for (int sex = 0; sex < 2; ++sex) {
                    static party::Character c;
                    c = party::Character{};
                    create::begin(c, m->tables, m->facts, m->dice, races[ri], sex, cl[ci], al[ai]);
                    create::roll(c, m->tables, m->facts, m->dice);
                    rules::recalc(c, *names, d->facts);
                    if (c.hp() <= 0 || c.hp_max() <= 0) {
                        char t[80];
                        snprintf(t, sizeof t, "race %d class %d align %d: HP %d/%d", races[ri], cl[ci], al[ai], c.hp(), c.hp_max());
                        problem(t);
                    }
                    // Up to the top: experience for everything, training round by round
                    int rounds = 0;
                    for (; rounds < 40; ++rounds) {
                        c.rec[0x127] = 0x00;
                        c.rec[0x128] = 0x00;
                        c.rec[0x129] = 0x20;     // 2 million
                        const int mask = create::trainable(c, m->tables);
                        if (!mask) break;
                        const int hp = c.hp_max();
                        create::train_classes(c, m->tables, m->facts, m->dice, mask, false);
                        rules::recalc(c, *names, d->facts);
                        if (c.hp_max() < hp) {
                            char t[80];
                            snprintf(t, sizeof t, "race %d class %d: HP max fell %d -> %d training", races[ri], cl[ci], hp, c.hp_max());
                            problem(t);
                        }
                    }
                    if (sex == 0 && ai == 0) {
                        printf("== race %d class %d: levels", races[ri], cl[ci]);
                        for (int k = 0; k < 8; ++k) printf(" %d", c.level(k));
                        printf(" HP %d THAC0 %d AC %d saves %d %d %d %d %d after %d rounds\n", c.hp_max(), c.thac0(), c.ac(), c.rec[0xDF],
                               c.rec[0xE0], c.rec[0xE1], c.rec[0xE2], c.rec[0xE3], rounds);
                    }
                    // Human Change, when it can
                    if (create::can_change(c)) {
                        int to[16];
                        const int nt = create::change_classes(c, m->tables, to, 16);
                        if (nt > 0) create::change_class(c, m->tables, m->facts, to[pick(nt)]);
                    }
                }
        }
    }
    delete m;
}

// ---- events: every ECL block, every square
static void sweep_events(int only)
{
    const int areas[] = {1, 2, 3, 4, 5, 6};
    for (int area : areas) {
        if (only && area != only) continue;
        char path[300];
        snprintf(path, sizeof path, "/GOLDBOX/%s/ECL%d.DAX", game_dir.substr(game_dir.rfind('/') + 1).c_str(), area);
        fs::File f = sd_fs().open(path, "r");
        if (!f) {
            printf("no %s\n", path);
            continue;
        }
        library::FileSource src(f);
        static dax::Index dir;
        std::vector<int> ids;
        if (dax::read_index(src, dir) == dax::Status::Ok)
            for (int i = 0; i < dir.count; ++i) ids.push_back(dir.entries[i].id);
        f.close();
        for (int block : ids) {
            using namespace play;
            if (getenv("BLOCK") && atoi(getenv("BLOCK")) != block) continue;
            printf("== ECL%d block %d\n", area, block);
            god = true;
            // Into the block as a NEWECL would; again whenever the game has left it
            auto enter = [&]() {
                if (!boot()) return false;
                d->gs.game_area = static_cast<uint8_t>(area);
                vm->set(0x7F12, d->gs.game_area);
                d->gs.script = static_cast<uint8_t>(block);
                waiting = false;
                if (!host->load_script(d->gs.script, d->gs.code, &d->gs.code_len) || !vm->init_script()) {
                    problem("script didn't load");
                    return false;
                }
                if (getenv("ENTRIES")) printf("  entries %04X %04X %04X %04X %04X len %u\n", vm->entry(0), vm->entry(1), vm->entry(2), vm->entry(3), vm->entry(4), d->gs.code_len);
                run_entry(4, Then::NewFirst);
                return pump();
            };
            if (!enter()) {
                printf("  first run: %s (screen %d)\n", stuck_why, d ? (int)screen : -1);
                stuck_why = "";
            }
            int steps = 0, lost = 0, reentries = 0;
            for (int y = 0; y < 16 && lost < 20; ++y)
                for (int x = 0; x < 16 && lost < 20; ++x) {
                    if (!d || screen != Screen::Game || waiting || d->gs.game_area != area || d->gs.script != block) {
                        ++reentries;
                        if (!enter()) {
                            ++lost;
                            continue;
                        }
                    }
                    for (int i = 0; i < pt->count; ++i) {
                        pt->m[i].rec[0x1A4] = pt->m[i].rec[0x78];
                        pt->m[i].rec[0x195] = party::Okay;
                        pt->m[i].rec[0x196] = 1;
                    }
                    d->gs.x = static_cast<uint8_t>(x);
                    d->gs.y = static_cast<uint8_t>(y);
                    d->gs.dir = static_cast<uint8_t>(pick(4) * 2);
                    after_move_redraw();
                    play::act(play::Act::Forward, C);
                    ++steps;
                    if (!pump()) {
                        if (stuck_why[0]) printf("  at %d,%d: %s (screen %d)\n", x, y, stuck_why, d ? (int)screen : -1);
                        stuck_why = "";
                        ++lost;
                    }
                }
            god = false;
            printf("  %d steps, %d times back into the block, %d lost\n", steps, reentries, lost);
        }
    }
}

// ---- save and load
static bool slurp(const char* name, std::vector<uint8_t>& out)
{
    char path[300];
    snprintf(path, sizeof path, "%s/SAVE/%s", game_dir.c_str(), name);
    FILE* f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof path, "%s/%s", game_dir.c_str(), name);
        f = fopen(path, "rb");
    }
    if (!f) return false;
    out.clear();
    int ch;
    while ((ch = fgetc(f)) != EOF) out.push_back(static_cast<uint8_t>(ch));
    fclose(f);
    return true;
}

static void ticks(int n)
{
    for (int k = 0; k < n; ++k) {
        g_now += 20;
        play::tick(g_now, C);
    }
}

static bool save_to(char slot)
{
    using namespace play;
    tap_word('E');                      // Encamp
    ticks(200);
    if (screen != Screen::Camp) {
        printf("  encamp: screen %d menu [%s%s]\n", (int)screen, menu.prompt, menu.s);
        return false;
    }
    tap_word('S');                      // Save
    ticks(20);
    printf("  save menu [%s%s] screen %d\n", menu.prompt, menu.s, (int)screen);
    if (!tap_word(slot)) return false;
    ticks(20);
    if (screen == Screen::YesNo) tap_word('Y');
    ticks(20);
    printf("  after save: screen %d menu [%s%s]\n", (int)screen, menu.prompt, menu.s);
    if (screen == Screen::Camp) tap_word('E');      // leave the camp
    return pump(20000);
}

static void sweep_saveload()
{
    using namespace play;
    if (!boot('A')) return;
    // Change things: walk, effects, items, money
    for (int k = 0; k < 6; ++k) {
        play::act(k % 3 ? play::Act::Forward : play::Act::TurnLeft, C);
        pump();
    }
    pt->m[0].rec[0x103] = 77;
    pt->m[1].rec[0x1A4] = 5;
    vm->set(0x4C05, 0x1234);
    vm->set(0x7E10, 0x5678);
    static ecl::GameState g1;
    static party::Party p1;
    g1 = d->gs;
    p1 = *pt;
    const int x = d->gs.x, y = d->gs.y, dr = d->gs.dir;
    if (!save_to('C')) problem("couldn't save C");
    printf("  saved C at %d,%d dir %d area %d script %d\n", x, y, dr, d->gs.game_area, d->gs.script);
    if (!boot('C')) {
        problem("saved game C doesn't load");
        return;
    }
    printf("  loaded C at %d,%d dir %d area %d script %d\n", d->gs.x, d->gs.y, d->gs.dir, d->gs.game_area, d->gs.script);
    if (d->gs.x != x || d->gs.y != y || d->gs.dir != dr) problem("position not the same after load");
    if (vm->get(0x4C05) != 0x1234 || vm->get(0x7E10) != 0x5678) problem("script memory not the same after load");
    if (pt->count != p1.count) problem("party size differs");
    for (int i = 0; i < pt->count && i < p1.count; ++i) {
        const party::Character& a = p1.m[i];
        const party::Character& b = pt->m[i];
        for (int o = 0; o < party::kRecordSize; ++o)
            if (a.rec[o] != b.rec[o]) {
                char t[80];
                snprintf(t, sizeof t, "member %d record %03X: %d saved, %d loaded", i, o, a.rec[o], b.rec[o]);
                problem(t);
            }
        if (a.n_items != b.n_items || memcmp(a.items, b.items, sizeof a.items[0] * a.n_items)) problem("items differ");
        if (a.n_affects != b.n_affects || memcmp(a.affects, b.affects, sizeof a.affects[0] * a.n_affects)) problem("effects differ");
    }
    for (size_t o = 0; o < sizeof g1.area1; ++o)
        if (g1.area1[o] != d->gs.area1[o]) {
            char t[64];
            snprintf(t, sizeof t, "area1 byte %03zX differs", o);
            problem(t);
        }
    for (size_t o = 0; o < sizeof g1.area2; ++o)
        if (g1.area2[o] != d->gs.area2[o] && o != 0x58E) {
            char t[64];
            snprintf(t, sizeof t, "area2 byte %03zX differs", o);
            problem(t);
        }
    // Saved again as D straight away: the same file
    if (!save_to('D')) problem("couldn't save D");
    std::vector<uint8_t> c1, c2;
    if (!slurp("SAVGAMC.DAT", c1) || !slurp("SAVGAMD.DAT", c2)) problem("save files not found");
    else if (c1 != c2) {
        size_t o = 0;
        // (the characters' file names say C and D: CHRDATC1 / CHRDATD1)
        while (o < c1.size() && o < c2.size() && (c1[o] == c2[o] || (c1[o] == 'C' && c2[o] == 'D'))) ++o;
        if (o == c1.size() && c1.size() == c2.size()) o = SIZE_MAX;
    }
    if (c1.size() && c2.size() && c1.size() == c2.size()) {
        // the characters' files: the same bytes
        for (int i = 1; i <= pt->count; ++i)
            for (const char* ext : {"SAV", "SWG", "FX"}) {
                char a[24], b[24];
                snprintf(a, sizeof a, "CHRDATC%d.%s", i, ext);
                snprintf(b, sizeof b, "CHRDATD%d.%s", i, ext);
                std::vector<uint8_t> x1, x2;
                const bool h1 = slurp(a, x1), h2 = slurp(b, x2);
                if (h1 != h2 || x1 != x2) {
                    char t[80];
                    snprintf(t, sizeof t, "%s / %s differ", a, b);
                    problem(t);
                }
            }
    }
    if (false) {
        size_t o = 0;
        char t[96];
        snprintf(t, sizeof t, "SAVGAMC / SAVGAMD differ (sizes %zu %zu, first at %zu)", c1.size(), c2.size(), o);
        problem(t);
    }
}


// ---- monkey: random taps, keys and moves everywhere (crashes, stuck
// screens, broken invariants)
// A boot, then (MAREA / MBLOCK set) into that script block as a NEWECL would
static bool monkey_boot()
{
    using namespace play;
    if (!boot()) return false;
    if (!getenv("MAREA")) return true;
    const int area = atoi(getenv("MAREA"));
    int block = getenv("MBLOCK") ? atoi(getenv("MBLOCK")) : 1;
    d->gs.game_area = static_cast<uint8_t>(area);
    vm->set(0x7F12, d->gs.game_area);
    d->gs.script = static_cast<uint8_t>(block);
    waiting = false;
    if (!host->load_script(d->gs.script, d->gs.code, &d->gs.code_len) || !vm->init_script()) return false;
    run_entry(4, Then::NewFirst);
    pump(20000);
    return d && pt && pt->count;
}

static void sweep_monkey(int rounds)
{
    using namespace play;
    if (rounds <= 0) rounds = 20000;
    int boots = 0;
    // The monkey saves over the slots: every boot starts from the folder's
    // first state (kept in SAVE.MONKEY)
    const std::string save = game_dir + "/SAVE", keep = game_dir + "/SAVE.MONKEY";
    const std::string snap = "rm -rf '" + keep + "' && cp -r '" + save + "' '" + keep + "'";
    const std::string back_up = "rm -rf '" + save + "' && cp -r '" + keep + "' '" + save + "'";
    if (system(snap.c_str()) != 0) return;
    if (!monkey_boot()) return;
    int last_screen = -1, same_screen = 0;
    for (int r = 0; r < rounds; ++r) {
        if (!d || screen == Screen::Title || !pt || pt->count < 3) {
            if (++boots > 200 || system(back_up.c_str()) != 0 || !monkey_boot()) break;
            continue;
        }
        // The icon editor and Alter swallow random taps: leave them soon
        if ((screen == Screen::Icon || screen == Screen::Alter || screen == Screen::Effects) && pick(20) == 0) {
            play::back(C);
            continue;
        }
        if (play::walking() && pick(100) < 60) {
            static const play::Act acts[] = {play::Act::Forward, play::Act::Forward, play::Act::Forward, play::Act::TurnLeft,
                                             play::Act::TurnRight, play::Act::TurnAround, play::Act::StepLeft, play::Act::StepRight};
            play::act(acts[pick(8)], C);
            for (int k = 0; k < 5; ++k) {
                g_now += 20;
                play::tick(g_now, C);
            }
            continue;
        }
        const int a = pick(100);
        {
            static char ring[16][96];
            static int ri = 0;
            static bool told = false;
            snprintf(ring[ri++ % 16], 96, "a %d screen %d fg %d st %d menu [%s%s] vf %d", a, (int)screen, fg ? 1 : 0,
                     fg ? (int)fg->st : -1, menu.prompt, menu.s, (int)view_from);
            if (!told && !fg && view_from == Screen::Fight && (screen == Screen::View || screen == Screen::Items)) {
                told = true;
                printf("  [fight gone under View / Items:]\n");
                for (int k = 0; k < 16; ++k) printf("    %s\n", ring[(ri + k) % 16]);
            }
        }
        if (a < 45 && menu.count > 0) {
            tap_item(pick(menu.count));
        } else if (a < 60) {
            play::tap(pick(320), pick(200), C);             // anywhere on the game screen
        } else if (a < 70) {
            static const play::Act acts[] = {play::Act::Forward, play::Act::TurnLeft, play::Act::TurnRight, play::Act::TurnAround,
                                             play::Act::StepLeft, play::Act::StepRight, play::Act::Area, play::Act::Look};
            play::act(acts[pick(8)], C);
        } else if (a < 78) {
            static const play::Nav navs[] = {play::Nav::Up, play::Nav::Down, play::Nav::Left, play::Nav::Right};
            play::nav(navs[pick(4)], C);
        } else if (a < 82) {
            int x, y;
            if (play::nav_point(&x, &y)) play::tap(x, y, C);
        } else if (a < 86) {
            play::back(C);
        } else if (a < 90 && input_mode != Input::None) {
            play::input_key(pick(3) ? static_cast<char>('A' + pick(26)) : '\n', C);
        } else {
            for (int k = 0; k < 50; ++k) {
                g_now += 20;
                play::tick(g_now, C);
            }
        }
        for (int k = 0; k < 5; ++k) {
            g_now += 20;
            play::tick(g_now, C);
        }
        if (!d) continue;
        {
            char jk;
            int jn;
            play::journal_request(&jk, &jn);
        }
        // Invariants
        if (pt->count < 0 || pt->count > party::kMaxParty) problem("party size out of range");
        if (pt->count && (pt->selected < 0 || pt->selected >= pt->count)) {
            char t[64];
            snprintf(t, sizeof t, "selected %d of %d (screen %d)", pt->selected, pt->count, (int)screen);
            problem(t);
            pt->selected = 0;
        }
        for (int i = 0; i < pt->count; ++i) {
            const party::Character& m = pt->m[i];
            if (m.n_items < 0 || m.n_items > party::kMaxItems || m.n_affects < 0 || m.n_affects > party::kMaxAffects)
                problem("items / effects count out of range");
        }
        if ((int)screen == last_screen) ++same_screen;
        else {
            same_screen = 0;
            last_screen = (int)screen;
        }
        if (same_screen > 3000 && screen != Screen::Game) {
            char t[160];
            snprintf(t, sizeof t, "screen %d for 3000 actions (menu [%s%s])", (int)screen, menu.prompt, menu.s);
            problem(t);
            printf("  [view_from %d items_direct %d note_held %d]\n", (int)view_from, (int)items_direct, (int)note_held);
            if (fg)
                printf("  [fight st %d round %d cur %d manual %d spell %02X sp_n %d budget %d sel %d count %d]\n", (int)fg->st,
                       fg->b.round, fg->cur, (int)fg->manual, fg->spell, fg->sp_n, fg->pick_budget, menu.selected, menu.count);
            same_screen = 0;
            play::back(C);
        }
        static int last_area = -1;
        if (d->gs.game_area != last_area) {
            last_area = d->gs.game_area;
            printf("  monkey %d: now in area %d\n", r, last_area);
        }
        if (r % 1000 == 0) printf("  monkey %d: screen %d party %d area %d script %d\n", r, (int)screen, pt->count, d->gs.game_area, d->gs.script);
    }
    printf("  %d boots\n", boots);
}

// ---- behaviour checks (CLAUDE.md "Game testing"): what each thing DOES,
// against what the original does (the rules, the program's tables, the
// coab facts - claude/behaviour_facts.md), never against our own code.
// Build with -DCYD_TEST_HOOKS (the die hook) and -DBEHAVE_<PART> for the
// parts wanted: CLASSES, ITEMS, SPELLS, MONSTERS, EVENTS, SAVES.
#ifdef CYD_TEST_HOOKS
static int b_pass = 0, b_fail = 0;
// One check: "PASS part: what - detail" / "FAIL ..."; a failure counts as a problem
static bool expect(const char* part, const char* what, bool ok, const char* fmt = "", ...)
{
    char detail[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    printf("%s %s: %s%s%s\n", ok ? "PASS" : "FAIL", part, what, detail[0] ? " - " : "", detail);
    if (ok) ++b_pass;
    else {
        ++b_fail;
        ++problems;
    }
    return ok;
}
// The dice: as usual, all highest, all lowest, or these faces in turn (1-based;
// when they run out: as usual)
static int b_dice_mode = 0;                 // 0 usual, 1 highest, 2 lowest
static std::vector<int> b_faces;
static size_t b_face_at = 0;
static int b_die(int n)
{
    if (b_face_at < b_faces.size()) {
        const int f = b_faces[b_face_at++];
        return f - 1 < n ? f - 1 : n - 1;
    }
    if (b_dice_mode == 1) return n - 1;
    if (b_dice_mode == 2) return 0;
    return -1;
}
static void dice_usual() { b_dice_mode = 0; b_faces.clear(); b_face_at = 0; create::g_die_hook = b_die; }
static void dice_high() { dice_usual(); b_dice_mode = 1; }
static void dice_low() { dice_usual(); b_dice_mode = 2; }
static void dice_faces(std::initializer_list<int> f) { dice_usual(); b_faces.assign(f); }
#endif

#ifdef BEHAVE_CLASSES
#include "behave_classes.inc"
#endif
#ifdef BEHAVE_ITEMS
#include "behave_items.inc"
#endif
#ifdef BEHAVE_SPELLS
#include "behave_spells.inc"
#endif
#ifdef BEHAVE_MONSTERS
#include "behave_monsters.inc"
#endif
#ifdef BEHAVE_EVENTS
#include "behave_events.inc"
#endif
#ifdef BEHAVE_SAVES
#include "behave_saves.inc"
#endif

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("sweep <game folder> <mode> [arg]\n");
        return 1;
    }
    game_dir = argv[1];
    while (game_dir.size() > 1 && game_dir.back() == '/') game_dir.pop_back();
    fs::sim_root() = game_dir.substr(0, game_dir.rfind('/'));
    const std::string mode = argv[2];
    const int arg = argc > 3 ? atoi(argv[3]) : 0;
    if (mode == "spells") sweep_spells();
    else if (mode == "campspells") sweep_campspells();
    else if (mode == "monsters") sweep_monsters(arg);
    else if (mode == "items") sweep_items(argc > 3 ? argv[3] : "items.bin");
    else if (mode == "classes") sweep_classes();
    else if (mode == "events") sweep_events(arg);
    else if (mode == "saveload") sweep_saveload();
    else if (mode == "monkey") {
        rnd.seed(static_cast<unsigned>(arg ? arg : 1));
        sweep_monkey(argc > 4 ? atoi(argv[4]) : 20000);
    }
#ifdef CYD_TEST_HOOKS
    else if (mode == "behave") {
        // (each part: a function of its own .inc; argv[3] = one part's name, or all)
        const std::string only = argc > 3 ? argv[3] : "";
        auto want = [&](const char* p) { return only.empty() || only == p; };
        (void)want;
#ifdef BEHAVE_CLASSES
        if (want("classes")) behave_classes();
#endif
#ifdef BEHAVE_ITEMS
        if (want("items")) behave_items();
#endif
#ifdef BEHAVE_SPELLS
        if (want("spells")) behave_spells();
#endif
#ifdef BEHAVE_MONSTERS
        if (want("monsters")) behave_monsters();
#endif
#ifdef BEHAVE_EVENTS
        if (want("events")) behave_events();
#endif
#ifdef BEHAVE_SAVES
        if (want("saves")) behave_saves();
#endif
        create::g_die_hook = nullptr;
        printf("== behaviour: %d passed, %d failed\n", b_pass, b_fail);
    }
#endif
    else printf("no mode %s\n", mode.c_str());
    play::close();
    printf("== %d problems\n", problems);
    return problems ? 2 : 0;
}

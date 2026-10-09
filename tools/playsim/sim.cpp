// Play Test simulator: runs src/ui/play.cpp on the PC against a game folder
// (the player's own files, outside the repo), answers menus, takes canvas
// snapshots (out/*.ppm). Build from the repo root:
//   g++ -std=c++17 -O1 -g -Itools/playsim/shim -Isrc -o playsim tools/playsim/sim.cpp src/engine/*.cpp
// Run: playsim <game folder> [moves: F L R B A K]; env: SAVE=A (load saved
// game A at the party menu first; otherwise BEGIN with no party; VIEW=n then
// views character n), AREA / BLOCK (start
// script), SETVAR=addr=value, CHOICES=digits, TYPE=text, TELE=x,y,dir,
// FINDLOCK, PAUSESHOT, ANIMSHOTS, MS (ms to settle). Needs an out/ folder.
#include "ui/play.cpp"
#include "engine/rules.h"
#include <vector>
#include <string>
uint32_t g_now = 1000;
SerialT Serial;
static std::vector<uint8_t> px(320 * 200);
static pic::Canvas C{px.data(), 320, 200};
static int shot_n = 0;
static void shot(const char* tag)
{
    char name[64];
    snprintf(name, sizeof name, "out/%02d_%s.ppm", shot_n++, tag);
    FILE* f = fopen(name, "wb");
    if (!f) return;                     // no out/ folder: no snapshots
    fprintf(f, "P6 320 200 255\n");
    for (uint8_t v : px) { const pic::Rgb& c = pic::kEga[v & 15]; fputc(c.r, f); fputc(c.g, f); fputc(c.b, f); }
    fclose(f);
}
static std::string last_text;
static std::vector<int> choices; static size_t ci = 0;
static int next_choice(int dflt) { return ci < choices.size() ? choices[ci++] : dflt; }
// Runs until the game waits for the player: auto-answers with `choice`
static void settle(int choice = 0, int max_ms = 60000)
{
    for (int t = 0; t < max_ms; t += 20) {
        g_now += 20;
        play::tick(g_now, C);
        { char jk; int jn; if (play::journal_request(&jk, &jn)) printf("  JOURNAL %c %d\n", jk, jn); }
        using namespace play;
        if (!waiting) return;
        const auto wt = vm->wait();
        if (play::input() != play::Input::None) {
            const char* t = getenv("TYPE") ? getenv("TYPE") : "42";
            printf("  INPUT %s: typing %s\n", play::input() == play::Input::Number ? "number" : "text", t);
            for (const char* q = t; *q; ++q) play::input_key(*q, C);
            shot("input");
            play::input_key('\n', C);
            continue;
        }
        if (wt == ecl::Wait::Print && !play::t_started && last_text != std::string(vm->text()) + std::to_string((long)play::w.row)) { last_text = std::string(vm->text()) + std::to_string((long)play::w.row); printf("  TEXT%s: %s\n", vm->clear() ? " (clear)" : "", vm->text()); }
        if (wt == ecl::Wait::Pause && getenv("PAUSESHOT")) { static int last = -1; if (last != (int)play::vm->pc()) { last = play::vm->pc(); shot("pause"); } }
        if (page_prompt) { shot("page"); play::tap(10, 10, C); continue; }
        if (wt == ecl::Wait::Menu) {
            for (int k = 0; k < 20; ++k) { g_now += 20; play::tick(g_now, C); }
            if (play::anim_block >= 0 && getenv("ANIMSHOTS")) for (int k = 0; k < play::d->anim.frames; ++k) { play::anim_draw(k); char t[16]; snprintf(t, 16, "frame%d", k); shot(t); }
            printf("  [anim block=%d frames=%d frame=%d] ", play::anim_block, play::d->anim.frames, play::anim_frame); printf("  [cursor wanted=%d on=%d bigpic=%d cities=%d city=%d]\n", (int)play::cursor_wanted(), (int)play::cursor_on, play::bigpic, play::d->cities, play::vm->get(0x4CA1));
            printf("  MENU prompt=%s:", vm->prompt());
            for (int i = 0; i < vm->items(); ++i) printf(" [%s]", vm->item(i));
            int c = vm->items() > 1 ? next_choice(choice) : 0;
            printf(" -> %d\n", c);
            shot("menu");
            if (c >= vm->items()) c = 0;
            int col = 0;
            if (c < menu.count) col = (int)strlen(menu.prompt) + menu.start[c];
            if (menu.count == 0) col = 0;
            play::tap(col * 8 + 2, text::kMenuRow * 8 + 2, C);
            continue;
        }
        if (wt == ecl::Wait::ListMenu && list_wait) {
            printf("  LIST prompt=%s:", vm->prompt());
            for (int i = 0; i < vm->items(); ++i) printf(" [%s]", vm->item(i));
            int c = next_choice(choice);
            printf(" -> %d\n", c);
            shot("list");
            play::tap(12, (list_row0 + (c < vm->items() ? c : 0)) * 8 + 2, C);
        }
    }
    printf("  (still waiting)\n");
}
int main(int argc, char** argv)
{
    // argv[1]: the game folder on the PC; its parent stands for /GOLDBOX
    std::string host = argv[1];
    while (host.size() > 1 && host.back() == '/') host.pop_back();
    const size_t slash = host.rfind('/');
    fs::sim_root() = slash == std::string::npos ? "." : host.substr(0, slash);
    const std::string folder = slash == std::string::npos ? host : host.substr(slash + 1);
    const char* dir = folder.c_str();
    const char* e = play::open(dir, games::Game::CurseOfTheAzureBonds, C, getenv("CACHE"));
    if (e) { printf("open: %s\n", e); return 1; }
    // The party menu: tap its lines by their first letter
    auto pm_line = [](char k) { for (int i = 0; i < play::pm_lines; ++i) if (play::pm_key(play::pm_item[i]) == k) return i; return -1; };
    printf("party menu:");
    for (int i = 0; i < play::pm_lines; ++i) printf(" [%s]", play::d->item[play::pm_item[i]]);
    printf("\n  prompt [%s] heads [%s] [%s] save dir [%s]\n", play::d->choose, play::d->name_head, play::d->ac_hp_head, play::d->save_dir);
    shot("party_menu");
    if (getenv("CREATE")) {
        // CREATE=race,sex,class,alignment (list positions), NAME=...
        auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } return false; };
        play::tap(24, (12 + pm_line('C')) * 8 + 2, C);
        const char* q = getenv("CREATE");
        for (int stage = 0; stage < 4 && play::screen == play::Screen::CreatePick; ++stage) {
            printf("  pick:"); for (int i = 0; i < play::mk->n_opt; ++i) { char l[40]; play::pick_line(i + 1, l, 40); printf(" [%s]", l); } printf("\n");
            { char t[16]; snprintf(t, 16, "pick%d", stage); shot(t); }
            play::plist.index = 1 + atoi(q);
            while (*q && *q != ',') ++q; if (*q) ++q;
            tap_word('S');
        }
        for (int r = 0; r < 2; ++r) {
            const party::Character& ch = play::mk->ch;
            printf("  rolled: %d %d %d %d %d %d (18/%d) hp %d/%d lvls", ch.stat(0), ch.stat(1), ch.stat(2), ch.stat(3), ch.stat(4), ch.stat(5), ch.stat(6), ch.hp(), ch.hp_max());
            for (int k = 0; k < 8; ++k) printf(" %d", ch.level(k));
            printf(" age %d thac0 %d ac %d xp %u align %d\n", ch.age(), ch.thac0(), ch.ac(), ch.exp(), ch.alignment());
            shot("rolled");
            tap_word(r == 0 ? 'Y' : 'N');
        }
        printf("  input %d prompt [%s]\n", (int)play::input(), play::input_prompt);
        for (const char* n = getenv("NAME") ? getenv("NAME") : "TESTER"; *n; ++n) play::input_key(*n, C);
        play::input_key('\n', C);
        printf("  ask [%s]\n", play::menu.prompt);
        shot("named");
        tap_word('Y');
        if (play::screen == play::Screen::YesNo) { printf("  ask [%s]\n", play::menu.prompt); tap_word('Y'); }
        printf("  screen %d\n", (int)play::screen);
    }
    // BEGIN needs a party: saved game A (GOG's sample party) unless SAVE names another
    const char* save = getenv("SAVE") ? getenv("SAVE") : "A";
    {
        play::tap(24, (12 + pm_line('L')) * 8 + 2, C);
        printf("  saves [%s] menu [%s%s]\n", play::pm_saves, play::menu.prompt, play::menu.s);
        const int k = (int)(strchr(play::pm_saves, save[0]) - play::pm_saves);
        play::tap(((int)strlen(play::menu.prompt) + play::menu.start[k]) * 8 + 2, text::kMenuRow * 8 + 2, C);
        for (int i = 0; i < play::pt->count; ++i) { char n[20]; play::pt->m[i].name(n, 20); printf("  %d: %-15s AC %d HP %d/%d race %d class %d\n", i, n, play::pt->m[i].ac(), play::pt->m[i].hp(), play::pt->m[i].hp_max(), play::pt->m[i].race(), play::pt->m[i].cls()); }
        printf("party menu:");
        for (int i = 0; i < play::pm_lines; ++i) printf(" [%s]", play::d->item[play::pm_item[i]]);
        printf("\n");
        shot("party_loaded");
        if (getenv("ROSTER")) {
            // Remove the selected character (MATHEW.GUY), Add them back, Drop -> No
            auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } return false; };
            play::tap(24, (12 + pm_line('R')) * 8 + 2, C);
            if (play::screen == play::Screen::YesNo) { printf("  ask [%s]\n", play::menu.prompt); tap_word('Y'); }
            printf("  removed: party %d\n", play::pt->count);
            shot("removed");
            play::tap(24, (12 + pm_line('A')) * 8 + 2, C);
            printf("  add menu [%s%s]\n", play::menu.prompt, play::menu.s);
            tap_word('C');
            printf("  %d to add; menu [%s%s]\n", play::d->guys, play::menu.prompt, play::menu.s);
            shot("add_list");
            tap_word('A');
            printf("  added: party %d\n", play::pt->count);
            shot("added");
            tap_word('E');
            play::tap(24, (12 + pm_line('D')) * 8 + 2, C);
            printf("  ask [%s] [%s]\n", play::menu.prompt, play::menu.s);
            tap_word('N');
            shot("not_dropped");
            g_now += 5000; play::tick(g_now, C);
        }
        if (getenv("VIEW")) {
            // Select a character (tap their line), View Character, back
            play::tap(16, (4 + atoi(getenv("VIEW"))) * 8 + 2, C);
            play::tap(24, (12 + pm_line('V')) * 8 + 2, C);
            shot("view");
            play::tap(2, text::kMenuRow * 8 + 2, C);
            shot("view_back");
        }
    }
    play::tap(24, (12 + pm_line('B')) * 8 + 2, C);
    if (getenv("AREA")) {
        // Start somewhere else: as a script would (SAVE area; NEWECL block)
        play::d->gs.game_area = (uint8_t)atoi(getenv("AREA"));
        play::vm->set(0x7F12, play::d->gs.game_area);
        play::d->gs.script = (uint8_t)atoi(getenv("BLOCK"));
        play::waiting = false;
        if (getenv("SETVAR")) { unsigned a, v; sscanf(getenv("SETVAR"), "%x=%u", &a, &v); play::vm->set((uint16_t)a, (uint16_t)v); }
        play::host->load_script(play::d->gs.script, play::d->gs.code, &play::d->gs.code_len);
        play::vm->init_script();
        play::run_entry(4, play::Then::NewFirst);
    }
    if (getenv("CHOICES")) for (const char* q = getenv("CHOICES"); *q; ++q) if (*q >= '0' && *q <= '9') choices.push_back(*q - '0');
    settle(0, getenv("MS") ? atoi(getenv("MS")) : 60000);
    shot("start");
    if (getenv("RUNAT")) {
        // Run the script from an address (e.g. a shop), then BUY=n,n,...
        // buys those goods for character 0, READY=i readies their item i
        unsigned a; sscanf(getenv("RUNAT"), "%x", &a);
        play::then = play::Then::Idle;
        play::handle(play::vm->run((uint16_t)a));
        settle(0, 5000);
        printf("  screen %d, %d goods, menu [%s]\n", (int)play::screen, play::ground->n, play::menu.s);
        if (play::screen == play::Screen::Shop) {
            shot("shop");
            auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } return false; };
            tap_word('B');
            shot("buy_list");
            if (getenv("BUY")) for (const char* q = getenv("BUY"); *q; ) { int n = atoi(q); play::plist.index = n; tap_word('B'); g_now += 5000; play::tick(g_now, C); while (*q && *q != ',') ++q; if (*q) ++q; }
            shot("bought");
            tap_word('E');
            tap_word('V');
            shot("view");
            if (tap_word('I')) {
                if (getenv("READY")) for (const char* q = getenv("READY"); *q; ) { play::plist.index = atoi(q); tap_word('R'); g_now += 5000; play::tick(g_now, C); while (*q && *q != ',') ++q; if (*q) ++q; }
                shot("items");
                tap_word('E');
                shot("view2");
            }
            play::tap(2, 2, C);
            tap_word('E');
            settle(0, 5000);
            shot("after_shop");

            const party::Character& ch = play::pt->m[0];
            char n[20]; ch.name(n, 20);
            printf("  %s: AC %d THAC0 %d %dd%d%+d items %d gold worth %d\n", n, ch.ac(), ch.thac0(), ch.dice(), ch.dice_sides(), ch.damage_bonus(), ch.n_items, rules::gold_worth(ch));
        }
    }
    if (getenv("FINDLOCK")) for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) for (int dd = 0; dd < 8; dd += 2) if (geo::passage(play::d->map, x, y, dd) >= 2) printf("locked %d,%d,%d\n", x, y, dd);
    if (getenv("TELE")) { int x, y, dd; sscanf(getenv("TELE"), "%d,%d,%d", &x, &y, &dd); play::d->gs.x = x; play::d->gs.y = y; play::d->gs.dir = dd; play::after_move_redraw(); }
    const char* moves = argc > 2 ? argv[2] : "FFFF";
    for (const char* m = moves; *m; ++m) {
        play::Act a = play::Act::Forward;
        switch (*m) {
        case 'F': a = play::Act::Forward; break;
        case 'L': a = play::Act::TurnLeft; break;
        case 'R': a = play::Act::TurnRight; break;
        case 'B': a = play::Act::TurnAround; break;
        case 'A': a = play::Act::Area; break;
        case 'K': a = play::Act::Look; break;
        }
        printf("   open N%d E%d S%d W%d\n", geo::passage(play::d->map, play::pos_x(), play::pos_y(), 0), geo::passage(play::d->map, play::pos_x(), play::pos_y(), 2), geo::passage(play::d->map, play::pos_x(), play::pos_y(), 4), geo::passage(play::d->map, play::pos_x(), play::pos_y(), 6));
        const bool ok = play::act(a, C);
        printf("%c -> %s at %d,%d %s  then=%d waiting=%d\n", *m, ok ? "ok" : "refused", play::pos_x(), play::pos_y(),
               geo::dir_name(play::dir()), (int)play::then, (int)play::waiting);
        if (play::then == play::Then::Door) { printf("  LOCKED\n"); play::act(play::Act::Forward, C); }
        settle();
        { char t[16]; snprintf(t, sizeof t, "after_%c", *m); shot(t); }
        char l1[48], l2[48]; play::describe(l1, l2, 48);
        printf("   now %d,%d %s | %s | %s\n", play::pos_x(), play::pos_y(), geo::dir_name(play::dir()), l1, l2);
    }
    if (getenv("SAVESLOT")) {
        // Encamp, Save to that slot
        auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } return false; };
        tap_word('E');
        shot("camp");
        tap_word('S');
        printf("  save menu [%s%s]\n", play::menu.prompt, play::menu.s);
        tap_word(getenv("SAVESLOT")[0]);
        shot("saved");
        tap_word('E');
        printf("  saved at %d,%d %s script %d\n", play::pos_x(), play::pos_y(), geo::dir_name(play::dir()), play::vm->get(0x4BF2));
    }
    shot("end");
    play::close();
}

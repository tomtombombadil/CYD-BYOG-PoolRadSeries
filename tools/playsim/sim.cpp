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
    const bool swap = play::fight_colours();     // the fight's colours: 0 and 8 swapped
    for (uint8_t v : px) { int k = v & 15; if (swap && (k == 0 || k == 8)) k ^= 8; const pic::Rgb& c = pic::kEga[k]; fputc(c.r, f); fputc(c.g, f); fputc(c.b, f); }
    fclose(f);
}
// ITEMOPS on the items screen: a digit picks list line n, a letter taps
// that menu word, @n taps party row n (Trade), '#' lists the items
static void item_ops(const char* ops)
{
    auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } printf("  (no %c in [%s%s])\n", k, play::menu.prompt, play::menu.s); return false; };
    auto list = [&]() {
        const party::Character* ch = play::pt->sel();
        char n[20]; ch->name(n, 20);
        printf("  %s: %d items, gold worth %d, menu [%s%s]\n", n, ch->n_items, rules::gold_worth(*ch), play::menu.prompt, play::menu.s);
        if (play::screen == play::Screen::Items)
            for (int i = 0; i < ch->n_items; ++i) { char l[64]; play::list_line(i, l, 64); printf("    %d %s\n", i, l); }
    };
    list();
    for (const char* q = ops; *q; ++q) {
        if (*q == '#') list();
        else if (*q == '@') { ++q; play::tap(16, (4 + (*q - '0')) * 8 + 2, C); }
        else if (*q >= '0' && *q <= '9') play::plist.index = *q - '0';
        else { tap_word(*q); printf("  after %c: screen %d menu [%s%s]\n", *q, (int)play::screen, play::menu.prompt, play::menu.s); }
        g_now += 5000; play::tick(g_now, C);
    }
    shot("item_ops");
}
// Ops on any screen (CAMPRUN): letters tap menu words, digits pick a list
// line, %n taps party row n at column 17, &n taps text row n, =n; types a
// number, '#' prints the screen, '~' runs ticks for a minute of game time,
// '?' the selected one's memorized list, '!' everyone's HP and effects, -n sets member n to 1 HP,
// '[' gives the selected one a magic-user's scroll of 3 spells, ']' lists their items and spell book
static void run_ops(const char* q)
{
    auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } printf("  (no %c in [%s%s])\n", k, play::menu.prompt, play::menu.s); return false; };
    int n = 0;
    for (; *q; ++q) {
        if (*q == '#') {
            const party::Character& ch = *play::pt->sel();
            char nm[20]; ch.name(nm, 20);
            printf("  screen %d menu [%s%s] %s HP %d/%d\n", (int)play::screen, play::menu.prompt, play::menu.s, nm, ch.hp(), ch.hp_max());
            char t[16]; snprintf(t, 16, "ops%d", n++); shot(t);
        } else if (*q == '%') { ++q; play::tap(17 * 8 + 2, (4 + (*q - '0')) * 8 + 2, C); }
        else if (*q == '&') { ++q; int r = 0; while (*q >= '0' && *q <= '9') r = r * 10 + (*q++ - '0'); --q; play::tap(4 * 8 + 2, r * 8 + 2, C); }
        else if (*q == '=') { ++q; while (*q && *q != ';') play::input_key(*q++, C); play::input_key('\n', C); }
        else if (*q == '?') { const party::Character& ch = *play::pt->sel(); printf("  list:"); for (int i = 0; i < 84; ++i) if (ch.rec[0x1E + i]) printf(" %s%d", ch.rec[0x1E + i] & 0x80 ? "*" : "", ch.rec[0x1E + i] & 0x7F); printf("  (to learn %d)\n", ch.rec[0x72]); }
        else if (*q == '[') { party::Character& ch = *play::pt->sel(); uint8_t* it = ch.items[ch.n_items++]; memset(it, 0, 63); it[0x2E] = 0x3D; it[0x2F] = 0xD1; it[0x30] = 0xD4; it[0x3C] = 0x0C; it[0x3D] = 0x1E; it[0x3E] = 0x2F; it[0x37] = 1; }
        else if (*q == ']') { const party::Character& ch = *play::pt->sel(); printf("  items %d:", ch.n_items); for (int i = 0; i < ch.n_items; ++i) printf(" %02X(%02X %02X %02X w%02X)", ch.items[i][0x2E], ch.items[i][0x3C], ch.items[i][0x3D], ch.items[i][0x3E], ch.items[i][0x30]); printf("  book:"); for (int s2 = 1; s2 <= 100; ++s2) if (ch.rec[0x79 + s2 - 1]) printf(" %d", s2); printf("\n"); }
        else if (*q == '-') { ++q; play::pt->m[*q - '0'].rec[0x1A4] = 1; }
        else if (*q == '!') { for (int i = 0; i < play::pt->count; ++i) { const party::Character& ch = play::pt->m[i]; char nm[20]; ch.name(nm, 20); printf("  %s HP %d/%d st %d fx:", nm, ch.hp(), ch.hp_max(), ch.health()); for (int k = 0; k < ch.n_affects; ++k) printf(" %02X/%d/%d", ch.affects[k][0], ch.affects[k][1] | ch.affects[k][2] << 8, ch.affects[k][3]); printf("\n"); } }
        else if (*q == '~') { for (int k = 0; k < 600; ++k) { g_now += 100; play::tick(g_now, C); } }
        else if (*q >= '0' && *q <= '9') play::plist.index = *q - '0';
        else tap_word(*q);
        g_now += 5000; play::tick(g_now, C);
    }
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
    if (getenv("HLTEST")) {
        // The tap highlight on a party menu line, then put back
        int y0, y1;
        const bool on = play::tap_highlight(24, (12 + pm_line('C')) * 8 + 2, C, &y0, &y1);
        printf("  highlight %d rows %d-%d\n", on, on ? y0 : 0, on ? y1 : 0);
        shot("hl_on");
        play::tap_highlight_end(C);
        shot("hl_off");
    }
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
        if (getenv("ITEMOPS")) {
            // ITEMOPS: the selected character's items: a digit picks list line n,
            // a letter taps that menu word, @n taps party row n (Trade), '#' lists
            auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } printf("  (no %c in [%s%s])\n", k, play::menu.prompt, play::menu.s); return false; };
            play::tap(16, (4 + (getenv("WHO") ? atoi(getenv("WHO")) : 0)) * 8 + 2, C);
            play::tap(24, (12 + pm_line('V')) * 8 + 2, C);
            tap_word('I');
            item_ops(getenv("ITEMOPS"));
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
    if (getenv("CAMPRUN")) run_ops(getenv("CAMPRUN"));
    shot("start");
    if (getenv("RUNAT")) {
        // Run the script from an address (e.g. a shop), then BUY=n,n,...
        // buys those goods for character 0, READY=i readies their item i
        unsigned a; sscanf(getenv("RUNAT"), "%x", &a);
        play::then = play::Then::Idle;
        play::handle(play::vm->run((uint16_t)a));
        settle(0, 5000);
        printf("  screen %d, %d goods, menu [%s]\n", (int)play::screen, play::ground->n, play::menu.s);
        if (play::screen == play::Screen::Fight && getenv("FIGHT")) {
            // FIGHT: the fight played out - Quick for each party member on
            // their menu (FIGHT=m: their menu is left alone and snapshots
            // taken), the results and treasure tapped through
            auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } return false; };
            shot("fight_start");
            int turns = 0, n = 0;
            const char* fops = getenv("FIGHTOPS");      // taps while the fight waits: letters, &rr rows; then Quick
            for (int k = 0; k < 200000 && (play::screen == play::Screen::Fight || play::screen == play::Screen::SpellList); ++k) {
                g_now += 50;
                play::tick(g_now, C);
                if (!play::fg) continue;
                const bool asks = play::screen == play::Screen::SpellList ||
                                  (play::fg->st == play::FSt::Menu || play::fg->st == play::FSt::Aim || play::fg->st == play::FSt::DoneMenu);
                if (asks && fops && *fops) {
                    if (*fops == '@') {             // @n: Quick the others until member n's menu
                        if (play::fg->st == play::FSt::Menu && play::fg->cur != fops[1] - '0') { tap_word('Q'); continue; }
                        fops += 2;
                        continue;
                    }
                    if (*fops == '&') { const int r = (fops[1] - '0') * 10 + (fops[2] - '0'); fops += 3; play::tap(4 * 8 + 2, r * 8 + 2, C); }
                    else if (*fops == '#') { ++fops; static int fo = 0; char t[16]; snprintf(t, 16, "fops%d", fo++); shot(t); printf("  [fight st %d screen %d menu %s%s]\n", (int)play::fg->st, (int)play::screen, play::menu.prompt, play::menu.s); }
                    else { tap_word(*fops++); }
                    continue;
                }
                static int pages_shot = 0;
                if (play::fg->st == play::FSt::Pages && pages_shot < 150 && play::fg->until > g_now + 10) { char t[16]; snprintf(t, 16, "fpage%d", pages_shot++); shot(t); g_now = play::fg->until; }
                if (play::fg->st == play::FSt::Aim) {
                    static int aims = 0;
                    if (aims++ < 3) { char t[16]; snprintf(t, 16, "faim%d", aims); shot(t); printf("  [aim: menu %s%s]\n", play::menu.prompt, play::menu.s); }
                    if (!tap_word('T')) tap_word('E');
                    continue;
                }
                if (play::fg->st == play::FSt::Menu) {
                    if (n < 6) { char t[16]; snprintf(t, 16, "fmenu%d", n++); shot(t); }
                    ++turns;
                    tap_word('Q');
                } else if (play::fg->st == play::FSt::Results || play::fg->st == play::FSt::Destroyed) {
                    shot("fight_results");
                    printf("  fight over: result %d, each %d xp, round %d\n", play::vm->get(0x7EC7), play::fg->share, play::fg->b.round);
                    for (int i = 0; i < play::pt->count; ++i) { char nm[20]; play::pt->m[i].name(nm, 20); printf("   %s HP %d/%d status %d xp %u\n", nm, play::pt->m[i].hp(), play::pt->m[i].hp_max(), play::pt->m[i].health(), play::pt->m[i].exp()); }
                    play::tap(2, 2, C);
                }
            }
            printf("  after the fight: screen %d menu [%s%s] ground %d items, gold %d\n", (int)play::screen, play::menu.prompt, play::menu.s, play::ground->n, play::ground->money[3]);
            shot("after_fight");
        }
        if ((play::screen == play::Screen::Shop || play::screen == play::Screen::PartyMenu) && getenv("SHOPRUN")) {
            // SHOPRUN: letters tap menu words, digits pick a list line, %n taps
            // party row n (the shop's list at column 17), =n; types a number
            // and Enter, '#' shows the screen and menu, @rr taps row rr
            auto tap_word = [](char k) { for (int i = 0; i < play::menu.count; ++i) if (text::key(play::menu, i) == k) { play::tap(((int)strlen(play::menu.prompt) + play::menu.start[i]) * 8 + 2, text::kMenuRow * 8 + 2, C); return true; } printf("  (no %c in [%s%s])\n", k, play::menu.prompt, play::menu.s); return false; };
            int n = 0;
            for (const char* q = getenv("SHOPRUN"); *q; ++q) {
                const party::Character& ch = *play::pt->sel();
                if (*q == '#') {
                    char nm[20]; ch.name(nm, 20);
                    printf("  screen %d temple %d menu [%s%s] %s HP %d/%d gold worth %d counter %d %d %d %d %d gems %d jewels %d items %d\n", (int)play::screen, (int)play::temple, play::menu.prompt, play::menu.s, nm, ch.hp(), ch.hp_max(), rules::gold_worth(ch), play::ground->money[0], play::ground->money[1], play::ground->money[2], play::ground->money[3], play::ground->money[4], ch.money(5), ch.money(6), ch.n_items);
                    char t[16]; snprintf(t, 16, "shoprun%d", n++); shot(t);
                } else if (*q == '%') { ++q; play::tap(17 * 8 + 2, (4 + (*q - '0')) * 8 + 2, C); }
                else if (*q == '^') { ++q; play::tap(24, (12 + pm_line(*q)) * 8 + 2, C); }   // a party menu line
                else if (*q == '&') { party::Character& w = *play::pt->sel(); w.rec[0x127] = 0xA0; w.rec[0x128] = 0x86; w.rec[0x129] = 0x01; }   // 100000 xp (a test)
                else if (*q == '?') { for (int k = 0; k < 8; ++k) printf(" %d", play::pt->sel()->level(k)); printf(" levels, xp %u, HP %d/%d\n", play::pt->sel()->exp(), play::pt->sel()->hp(), play::pt->sel()->hp_max()); }
                else if (*q == '!') { ++q; play::tap(16, (4 + (*q - '0')) * 8 + 2, C); }       // party menu: select row n
                else if (*q == '$') { party::Character& w = *play::pt->sel(); w.rec[0xFB + 10] = 3; w.rec[0xFB + 12] = 2; }   // 3 gems, 2 jewels (a test)
                else if (*q == '@') { int r = (q[1] - '0') * 10 + (q[2] - '0'); q += 2; play::tap(4 * 8 + 2, r * 8 + 2, C); }   // @rr taps text row rr
                else if (*q == '=') { ++q; while (*q && *q != ';') play::input_key(*q++, C); play::input_key('\n', C); }
                else if (*q >= '0' && *q <= '9') play::plist.index = *q - '0';
                else tap_word(*q);
                g_now += 5000; play::tick(g_now, C);
            }
        } else if (play::screen == play::Screen::Shop) {
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
                if (getenv("SHOPOPS")) item_ops(getenv("SHOPOPS"));
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
            if (getenv("GAMEOPS")) {
                // View -> Items from the exploring screen, then item ops
                play::pt->selected = 0;
                tap_word('V');
                tap_word('I');
                item_ops(getenv("GAMEOPS"));
            }
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

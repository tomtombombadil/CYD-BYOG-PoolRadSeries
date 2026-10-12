#include "logui.h"

#include <cstdio>

#include "app/library.h"
#include "logview.h"
#include "sendlog.h"

namespace logui {

namespace {

constexpr int kLogs = 4;
const char* const kLogFiles[kLogs] = {"SCAN.TXT", "RESTART.TXT", "ERRORS.TXT", "MEMORY.TXT"};
const char* const kTab[kLogs] = {"Card Scan", "Restarts", "Errors", "Memory"};

bool after_scan_ = false;
bool from_game_ = false;
int  tab = 0;

int body_top() { return ui::header_h() + ui::gap(); }
int body_bottom() { return ui::height() - ui::key_h() - ui::gap() * 2; }

// Send Logs: a key at the header's right (not after the scan)
ui::Rect send_key()
{
    const int w = ui::large() ? 140 : 96;
    return {ui::width() - w - ui::gap(), 2, w, ui::header_h() - 4};
}

ui::Rect tab_key(int i) { return after_scan_ ? ui::bottom_key(0, 1) : ui::bottom_key(i, kLogs); }

bool load(int i)
{
    char path[96];
    library::cache_path(kLogFiles[i], path, sizeof path);
    return logview::open_file(path, true);
}

} // namespace

void open(bool after_scan, const char* fallback)
{
    after_scan_ = after_scan;
    from_game_ = false;
    tab = 0;
    if (!load(0) && fallback) logview::open_text(fallback, true);
    ui::allow_drag(true);
}

void open_from_game()
{
    open(false);
    from_game_ = true;
    tab = 2;                               // Errors first: what went wrong in the game
    load(tab);
}

bool from_scan() { return after_scan_; }
bool from_game() { return from_game_; }

void draw()
{
    if (sendlog::is_open()) {
        sendlog::draw();
        return;
    }
    ui::clear();
    ui::header(after_scan_ ? "Card Scan" : "Logs", !after_scan_);
    logview::set_area({ui::gap(), body_top(), ui::width() - ui::gap() * 2, body_bottom() - body_top()});
    logview::draw();
    if (after_scan_) {
        ui::key(tab_key(0), "Continue", ui::KeyStyle::Lit);
        return;
    }
    ui::key(send_key(), "Send Logs");
    for (int i = 0; i < kLogs; ++i) ui::key(tab_key(i), kTab[i], tab == i ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
}

bool tap(const ui::Tap& t)
{
    bool stay = true;
    if (sendlog::is_open()) {
        if (!sendlog::tap(t)) {           // Back: the log shown before, again
            load(tab);
            draw();
        }
        return true;
    }
    if (!after_scan_ && send_key().contains(t.x, t.y)) {
        logview::close();                 // (its text's memory goes to the packing)
        sendlog::open();
        return true;
    }
    if (!after_scan_ && ui::back_rect().contains(t.x, t.y)) {
        stay = false;
    } else if (logview::tap(t)) {
        stay = true;
    } else if (after_scan_) {
        stay = !tab_key(0).contains(t.x, t.y);
    } else {
        for (int i = 0; i < kLogs; ++i)
            if (tab_key(i).contains(t.x, t.y) && tab != i) {
                tab = i;
                load(i);
                draw();
            }
    }
    if (!stay) {
        logview::close();
        ui::allow_drag(false);
    }
    return stay;
}

void tick()
{
    if (!sendlog::is_open()) logview::tick();
}

} // namespace logui

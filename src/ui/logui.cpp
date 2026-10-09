#include "logui.h"

#include <cstdio>

#include "app/library.h"
#include "logview.h"

namespace logui {

namespace {

constexpr int kLogs = 3;
const char* const kLogFiles[kLogs] = {"SCAN.TXT", "RESTART.TXT", "ERRORS.TXT"};
const char* const kTab[kLogs] = {"Card Scan", "Restarts", "Errors"};

bool after_scan_ = false;
int  tab = 0;

int body_top() { return ui::header_h() + ui::gap(); }
int body_bottom() { return ui::height() - ui::key_h() - ui::gap() * 2; }

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
    tab = 0;
    if (!load(0) && fallback) logview::open_text(fallback, true);
    ui::allow_drag(true);
}

bool from_scan() { return after_scan_; }

void draw()
{
    ui::clear();
    ui::header(after_scan_ ? "Card Scan" : "Logs", !after_scan_);
    logview::set_area({ui::gap(), body_top(), ui::width() - ui::gap() * 2, body_bottom() - body_top()});
    logview::draw();
    if (after_scan_) {
        ui::key(tab_key(0), "Continue", ui::KeyStyle::Lit);
        return;
    }
    for (int i = 0; i < kLogs; ++i) ui::key(tab_key(i), kTab[i], tab == i ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
}

bool tap(const ui::Tap& t)
{
    bool stay = true;
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

void tick() { logview::tick(); }

} // namespace logui

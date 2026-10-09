#include "netui.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "app/library.h"
#include "app/net.h"
#include "boards/board_select.h"
#include "keyboard.h"
#include "logview.h"

namespace netui {

namespace {

// Where the logs go, and how the mails are marked (Tom, 2026-10-09)
constexpr const char* kLogAddress = "cyd.classic.games.logs@gmail.com";
constexpr const char* kSubjectTag = "CYD-BYOG-GoldBoxGameEngine";

enum class Sub : uint8_t { Logs, Wifi, Mail, Keys };
enum class Typing : uint8_t { WifiPass, WifiName, MailUser, MailPass, MailServer };

const char* version_ = "";
const char* build_ = "";
Sub    sub = Sub::Logs;
bool   after_scan_ = false;
int    log_tab = 0;                      // 0 the scan log, 1 restarts, 2 errors
Typing typing = Typing::WifiPass;

// WiFi
constexpr int kMaxNets = 20;
net::Network* nets = nullptr;              // while the WiFi screen is open
int  n_nets = -1;                        // -1: still looking
int  net_page = 0;
char chosen[33];
bool connecting = false;
char wifi_msg[120];

// Mail
char mail_msg[220];
bool mail_ok = false;
char new_user[96], new_pass[64];

constexpr int kLogs = 3;
const char* const kLogFiles[kLogs] = {"SCAN.TXT", "RESTART.TXT", "ERRORS.TXT"};

int body_top() { return ui::header_h() + ui::gap(); }
int body_bottom() { return ui::height() - ui::key_h() - ui::gap() * 2; }

// Text wrapped at spaces into width w; returns the y under it
int wrapped(int x, int y, int w, const char* s, uint16_t col, ui::Font f)
{
    const int lh = ui::line_h(f) + 2;
    char line[160];
    while (*s) {
        int n = 0, cut = 0;
        while (s[n] && n < (int)sizeof line - 1) {
            line[n] = s[n];
            line[n + 1] = 0;
            if (ui::text_width(line, f) > w) break;
            if (s[n] == ' ') cut = n;
            ++n;
        }
        if (s[n] && cut > 0) n = cut;
        line[n] = 0;
        ui::text(x, y, line, col, f);
        y += lh;
        s += n;
        while (*s == ' ') ++s;
    }
    return y;
}

// ---- Logs ------------------------------------------------------------------

void log_path(int tab, char* out, size_t cap) { library::cache_path(kLogFiles[tab], out, cap); }

void load_log()
{
    char path[96];
    log_path(log_tab, path, sizeof path);
    logview::open_file(path, true);
}

ui::Rect log_key(int i) { return after_scan_ ? ui::bottom_key(0, 1) : ui::bottom_key(i, kLogs + 1); }

void draw_logs()
{
    ui::clear();
    ui::header(after_scan_ ? "Card Scan" : "Logs", !after_scan_);
    logview::set_area({ui::gap(), body_top(), ui::width() - ui::gap() * 2, body_bottom() - body_top()});
    logview::draw();
    if (after_scan_) {
        ui::key(log_key(0), "Continue", ui::KeyStyle::Lit);
        return;
    }
    static const char* const kTab[kLogs] = {"Card Scan", "Restarts", "Errors"};
    for (int i = 0; i < kLogs; ++i) ui::key(log_key(i), kTab[i], log_tab == i ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
    ui::key(log_key(kLogs), "Email\nLogs");
}

void open_mail();

bool tap_logs(const ui::Tap& t)
{
    if (!after_scan_ && ui::back_rect().contains(t.x, t.y)) return false;
    if (logview::tap(t)) return true;
    if (after_scan_) return !log_key(0).contains(t.x, t.y);
    for (int i = 0; i < kLogs; ++i)
        if (log_key(i).contains(t.x, t.y) && log_tab != i) {
            log_tab = i;
            load_log();
            draw_logs();
            return true;
        }
    if (log_key(kLogs).contains(t.x, t.y)) open_mail();
    return true;
}

// ---- WiFi ------------------------------------------------------------------

int status_y() { return body_top(); }
int list_top() { return body_top() + (ui::line_h(ui::Font::Small) + 2) * 2 + ui::gap(); }
int row_h() { return ui::key_h() - ui::gap(); }
int rows_per_page()
{
    const int r = (body_bottom() - list_top() + ui::gap()) / (row_h() + ui::gap());
    return r < 1 ? 1 : r;
}
int net_pages() { return n_nets > 0 ? (n_nets + rows_per_page() - 1) / rows_per_page() : 1; }
ui::Rect net_row(int i) { return {ui::gap(), list_top() + i * (row_h() + ui::gap()), ui::width() - ui::gap() * 2, row_h()}; }

enum WifiKey { kPrev, kNext, kAgain, kOther, kForget, kWifiKeys };
ui::Rect wifi_key(int i) { return ui::bottom_key(i, kWifiKeys); }

void draw_wifi_status()
{
    LGFX& g = ui::gfx();
    g.fillRect(0, status_y(), ui::width(), list_top() - status_y(), style::kBackground);
    char top[80];
    const net::Saved& s = net::saved();
    if (s.ssid[0]) snprintf(top, sizeof top, "Saved network: %s", s.ssid);
    else snprintf(top, sizeof top, "No network saved yet: tap yours.");
    ui::text(ui::gap() * 2, status_y(), top, style::kText, ui::Font::Small);
    ui::text(ui::gap() * 2, status_y() + ui::line_h(ui::Font::Small) + 2, wifi_msg,
             connecting ? style::kGold : style::kTextMuted, ui::Font::Small);
}

// Signal strength as four bars, a lock for a network with a password
void draw_signal(const ui::Rect& r, int rssi, bool secure, uint16_t col)
{
    LGFX& g = ui::gfx();
    const int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1;
    const int bw = ui::large() ? 5 : 4, step = bw + 2, hmax = r.h / 2;
    const int x0 = r.x + r.w - ui::gap() * 2 - step * 4, base = r.y + r.h / 2 + hmax / 2;
    for (int i = 0; i < 4; ++i) {
        const int h = hmax * (i + 1) / 4;
        g.fillRect(x0 + i * step, base - h, bw, h, i < bars ? col : style::kKeyEdge);
    }
    if (secure) {
        const int lx = x0 - ui::gap() * 2 - 10, ly = base - 8;
        g.drawRoundRect(lx + 2, ly - 6, 6, 8, 3, col);
        g.fillRect(lx, ly, 10, 8, col);
    }
}

void draw_wifi()
{
    ui::clear();
    ui::header("WiFi", true);
    draw_wifi_status();
    if (n_nets < 0) {
        ui::text(ui::gap() * 2, list_top() + ui::gap(), "Looking for networks...", style::kTextMuted);
    } else if (n_nets == 0) {
        ui::text(ui::gap() * 2, list_top() + ui::gap(), "No networks found.", style::kTextMuted);
    } else {
        const int per = rows_per_page();
        if (net_page >= net_pages()) net_page = net_pages() - 1;
        for (int i = 0; i < per && net_page * per + i < n_nets; ++i) {
            const net::Network& n = nets[net_page * per + i];
            const bool saved = strcmp(n.ssid, net::saved().ssid) == 0;
            const ui::Rect r = net_row(i);
            ui::key(r, "", saved ? ui::KeyStyle::Lit : ui::KeyStyle::Normal);
            ui::text(r.x + ui::gap() * 2, r.y + (r.h - ui::line_h(ui::Font::Normal)) / 2, n.ssid, style::kText);
            draw_signal(r, n.rssi, n.secure, saved ? style::kText : style::kGold);
        }
    }
    const bool many = n_nets > 0 && net_pages() > 1;
    ui::key_arrow(wifi_key(kPrev), ui::Arrow::Left, many && net_page > 0 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    ui::key_arrow(wifi_key(kNext), ui::Arrow::Right,
                  many && net_page < net_pages() - 1 ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
    ui::key(wifi_key(kAgain), "Look\nAgain", n_nets < 0 ? ui::KeyStyle::Dim : ui::KeyStyle::Normal);
    ui::key(wifi_key(kOther), "Other\nNetwork");
    ui::key(wifi_key(kForget), "Forget", net::saved().ssid[0] ? ui::KeyStyle::Normal : ui::KeyStyle::Dim);
}

void look_for_networks()
{
    n_nets = -1;
    net_page = 0;
    net::scan_start();
}

void open_keys(Typing what, const char* title, const char* prompt, const char* initial, size_t max)
{
    typing = what;
    sub = Sub::Keys;
    keyboard::open(title, prompt, initial, max);
    keyboard::draw();
}

void join(const char* ssid, const char* pass)
{
    snprintf(chosen, sizeof chosen, "%s", ssid);
    snprintf(new_pass, sizeof new_pass, "%s", pass);
    connecting = true;
    snprintf(wifi_msg, sizeof wifi_msg, "Connecting to %s...", ssid);
    net::connect_start(ssid, pass);
}

void ask_password(const char* ssid)
{
    snprintf(chosen, sizeof chosen, "%s", ssid);
    char title[48];
    snprintf(title, sizeof title, "Password: %s", ssid);
    const net::Saved& s = net::saved();
    open_keys(Typing::WifiPass, title, "", strcmp(s.ssid, ssid) == 0 ? s.wifi_pass : "", 63);
}

bool tap_wifi(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) return false;
    if (connecting) return true;
    const int per = rows_per_page();
    for (int i = 0; n_nets > 0 && i < per && net_page * per + i < n_nets; ++i) {
        if (!net_row(i).contains(t.x, t.y)) continue;
        const net::Network& n = nets[net_page * per + i];
        if (n.secure) {
            ask_password(n.ssid);
        } else {
            join(n.ssid, "");
            draw_wifi_status();
        }
        return true;
    }
    if (wifi_key(kPrev).contains(t.x, t.y) && net_page > 0) {
        --net_page;
        draw_wifi();
    } else if (wifi_key(kNext).contains(t.x, t.y) && n_nets > 0 && net_page < net_pages() - 1) {
        ++net_page;
        draw_wifi();
    } else if (wifi_key(kAgain).contains(t.x, t.y) && n_nets >= 0) {
        wifi_msg[0] = 0;
        look_for_networks();
        draw_wifi();
    } else if (wifi_key(kOther).contains(t.x, t.y)) {
        open_keys(Typing::WifiName, "Other Network", "Name: ", "", 32);
    } else if (wifi_key(kForget).contains(t.x, t.y) && net::saved().ssid[0]) {
        snprintf(wifi_msg, sizeof wifi_msg, "Forgot %s.", net::saved().ssid);
        net::forget_wifi();
        draw_wifi();
    }
    return true;
}

void tick_wifi()
{
    if (n_nets < 0) {
        const int n = nets ? net::scan_poll(nets, kMaxNets) : 0;
        if (n >= 0) {
            n_nets = n;
            draw_wifi();
        }
        return;
    }
    if (!connecting) return;
    const net::Conn c = net::connect_poll();
    if (c == net::Conn::Connecting) return;
    connecting = false;
    if (c == net::Conn::Connected) {
        net::save_wifi(chosen, new_pass);
        snprintf(wifi_msg, sizeof wifi_msg, "Connected to %s (%s) - saved.", chosen, net::ip_text());
    } else {
        snprintf(wifi_msg, sizeof wifi_msg, "Couldn't connect to %s - check the password.", chosen);
    }
    memset(new_pass, 0, sizeof new_pass);
    draw_wifi();
}

// ---- Email Logs ----------------------------------------------------------------

enum MailKey { kSend, kAccount, kMailKeys };
ui::Rect mail_key(int i) { return ui::bottom_key(i, kMailKeys); }

int msg_top = 0;                     // where the message goes (set by draw_mail)

void subject(char* out, size_t cap) { snprintf(out, cap, "%s logs - %s (%s) - %s", kSubjectTag, version_, build_, BOARD_NAME); }

void draw_mail_msg()
{
    LGFX& g = ui::gfx();
    g.fillRect(0, msg_top, ui::width(), body_bottom() - msg_top, style::kBackground);
    wrapped(ui::gap() * 2, msg_top, ui::width() - ui::gap() * 4, mail_msg, mail_ok ? style::kGold : style::kText,
            ui::Font::Small);
}

void draw_mail()
{
    ui::clear();
    ui::header("Email Logs", true);
    const net::Saved& s = net::saved();
    const int x = ui::gap() * 2, w = ui::width() - ui::gap() * 4;
    int y = body_top();
    char l[200];
    snprintf(l, sizeof l, "To: %s", kLogAddress);
    y = wrapped(x, y, w, l, style::kText, ui::Font::Small);
    if (s.mail_user[0]) snprintf(l, sizeof l, "From: %s (%s, port %u)", s.mail_user, s.mail_server, s.mail_port);
    else snprintf(l, sizeof l, "From: your own mail account - tap Account to set it up.");
    y = wrapped(x, y, w, l, s.mail_user[0] ? style::kText : style::kGold, ui::Font::Small);
    char sub_line[160];
    subject(sub_line, sizeof sub_line);
    snprintf(l, sizeof l, "Subject: %s", sub_line);
    y = wrapped(x, y, w, l, style::kTextMuted, ui::Font::Small);
    y = wrapped(x, y, w, "Attached: SCAN.TXT, RESTART.TXT, ERRORS.TXT (those on the card)", style::kTextMuted, ui::Font::Small);
    if (!s.ssid[0]) y = wrapped(x, y, w, "WiFi isn't set up yet: Settings - WiFi.", style::kGold, ui::Font::Small);
    msg_top = y + ui::gap();
    draw_mail_msg();
    ui::key(mail_key(kSend), "Send", s.mail_user[0] && s.ssid[0] ? ui::KeyStyle::Lit : ui::KeyStyle::Dim);
    ui::key(mail_key(kAccount), "Account");
}

void open_mail()
{
    sub = Sub::Mail;
    mail_ok = false;
    const net::Saved& s = net::saved();
    if (!s.mail_user[0])
        snprintf(mail_msg, sizeof mail_msg,
                 "The board signs in to your own mail account to send. Gmail: use an app password (Google "
                 "Account - Security - 2-Step Verification - App passwords), not your Gmail password.");
    else
        mail_msg[0] = 0;
    draw_mail();
}

void progress(const char* step, void*)
{
    snprintf(mail_msg, sizeof mail_msg, "%s", step);
    mail_ok = false;
    draw_mail_msg();
}

void send_logs()
{
    char paths[kLogs][96];
    const char* files[kLogs];
    for (int i = 0; i < kLogs; ++i) {
        library::cache_path(kLogFiles[i], paths[i], sizeof paths[i]);
        files[i] = paths[i];
    }
    char sub_line[160], body[600];
    subject(sub_line, sizeof sub_line);
    const uint32_t up = millis() / 1000;
    snprintf(body, sizeof body,
             "Logs from the CYD BYOG Gold Box game engine.\n\nFirmware: %s (%s)\nBoard: %s\n"
             "Free memory: %u KB (largest block %u KB)\nUp for: %u:%02u:%02u\n\nAttached: the card scan's log "
             "(SCAN.TXT), the restarts noted (RESTART.TXT) and the errors shown (ERRORS.TXT), those on the card.\n",
             version_, build_, BOARD_NAME, (unsigned)(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024), (unsigned)(up / 3600),
             (unsigned)(up / 60 % 60), (unsigned)(up % 60));
    mail_ok = net::send_mail(kLogAddress, sub_line, body, files, kLogs, mail_msg, sizeof mail_msg, progress, nullptr);
    net::wifi_off();
    draw_mail_msg();
}

bool tap_mail(const ui::Tap& t)
{
    if (ui::back_rect().contains(t.x, t.y)) {
        sub = Sub::Logs;
        draw_logs();
        return true;
    }
    const net::Saved& s = net::saved();
    if (mail_key(kSend).contains(t.x, t.y)) {
        if (!s.ssid[0]) {
            snprintf(mail_msg, sizeof mail_msg, "Set up WiFi first: Settings - WiFi.");
            draw_mail_msg();
        } else if (!s.mail_user[0]) {
            snprintf(mail_msg, sizeof mail_msg, "Tap Account to set up the account it sends from.");
            draw_mail_msg();
        } else {
            send_logs();
        }
    } else if (mail_key(kAccount).contains(t.x, t.y)) {
        open_keys(Typing::MailUser, "Your Email Address", "", s.mail_user, sizeof new_user - 1);
    }
    return true;
}

// ---- Typing ----------------------------------------------------------------

void typed_done()
{
    const char* txt = keyboard::text();
    switch (typing) {
    case Typing::WifiName:
        if (!*txt) break;
        ask_password(txt);
        return;
    case Typing::WifiPass:
        sub = Sub::Wifi;
        join(chosen, txt);
        draw_wifi();
        return;
    case Typing::MailUser: {
        if (!strchr(txt, '@')) {
            snprintf(mail_msg, sizeof mail_msg, "That isn't an email address.");
            break;
        }
        snprintf(new_user, sizeof new_user, "%s", txt);
        open_keys(Typing::MailPass, "App Password", "", "", sizeof new_pass - 1);
        return;
    }
    case Typing::MailPass: {
        // Google shows app passwords in groups of four: the spaces don't count
        size_t k = 0;
        for (const char* p = txt; *p && k + 1 < sizeof new_pass; ++p)
            if (*p != ' ') new_pass[k++] = *p;
        new_pass[k] = 0;
        char server[80];
        uint16_t port = 465;
        const net::Saved& s = net::saved();
        if (s.mail_server[0] && strcasecmp(strrchr(s.mail_user, '@') ? strrchr(s.mail_user, '@') : "",
                                           strrchr(new_user, '@')) == 0) {
            snprintf(server, sizeof server, "%s:%u", s.mail_server, s.mail_port);
        } else {
            char host[64];
            net::default_server(new_user, host, sizeof host, &port);
            snprintf(server, sizeof server, "%s:%u", host, port);
        }
        open_keys(Typing::MailServer, "Mail Server", "", server, 70);
        return;
    }
    case Typing::MailServer: {
        char host[64];
        snprintf(host, sizeof host, "%s", txt);
        uint16_t port = 465;
        if (char* colon = strchr(host, ':')) {
            *colon = 0;
            const int p = atoi(colon + 1);
            if (p > 0 && p < 65536) port = static_cast<uint16_t>(p);
        }
        net::save_mail(new_user, new_pass, host, port);
        memset(new_pass, 0, sizeof new_pass);
        snprintf(mail_msg, sizeof mail_msg, "Saved. Tap Send.");
        mail_ok = true;
        sub = Sub::Mail;
        draw_mail();
        return;
    }
    }
    // back to the screen that asked
    if (typing == Typing::WifiName || typing == Typing::WifiPass) {
        sub = Sub::Wifi;
        draw_wifi();
    } else {
        sub = Sub::Mail;
        draw_mail();
    }
}

bool tap_keys(const ui::Tap& t)
{
    switch (keyboard::tap(t)) {
    case keyboard::Result::Typing: return true;
    case keyboard::Result::Done: typed_done(); return true;
    case keyboard::Result::Cancel:
        memset(new_pass, 0, sizeof new_pass);
        if (typing == Typing::WifiName || typing == Typing::WifiPass) {
            sub = Sub::Wifi;
            draw_wifi();
        } else {
            sub = Sub::Mail;
            draw_mail();
        }
        return true;
    }
    return true;
}

void leave()
{
    free(nets);
    nets = nullptr;
    n_nets = 0;
    logview::close();
    net::wifi_off();
    ui::allow_drag(false);
    connecting = false;
}

} // namespace

void begin(const char* version, const char* build)
{
    version_ = version;
    build_ = build;
    net::load();
}

bool from_scan() { return after_scan_; }

void open_logs(bool after_scan, const char* fallback)
{
    after_scan_ = after_scan;
    sub = Sub::Logs;
    log_tab = 0;
    char path[96];
    log_path(0, path, sizeof path);
    if (!logview::open_file(path, true) && fallback) logview::open_text(fallback, true);
    ui::allow_drag(true);
}

void open_wifi()
{
    after_scan_ = false;
    sub = Sub::Wifi;
    wifi_msg[0] = 0;
    connecting = false;
    if (!nets) nets = static_cast<net::Network*>(calloc(kMaxNets, sizeof(net::Network)));
    look_for_networks();
    ui::allow_drag(false);
}

void draw()
{
    switch (sub) {
    case Sub::Logs: draw_logs(); break;
    case Sub::Wifi: draw_wifi(); break;
    case Sub::Mail: draw_mail(); break;
    case Sub::Keys: keyboard::draw(); break;
    }
}

bool tap(const ui::Tap& t)
{
    const Sub was = sub;
    bool stay = true;
    switch (sub) {
    case Sub::Logs: stay = tap_logs(t); break;
    case Sub::Wifi: stay = tap_wifi(t); break;
    case Sub::Mail: stay = tap_mail(t); break;
    case Sub::Keys: stay = tap_keys(t); break;
    }
    if (!stay) leave();
    else if (sub != was) ui::allow_drag(sub == Sub::Logs);    // only the log scrolls by dragging
    return stay;
}

void tick()
{
    if (sub == Sub::Logs) logview::tick();
    else if (sub == Sub::Wifi) tick_wifi();
}

} // namespace netui

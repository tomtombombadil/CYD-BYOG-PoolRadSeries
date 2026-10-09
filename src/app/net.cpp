#include "net.h"

#include <Arduino.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <cstdio>
#include <cstring>
#include <new>
#include <strings.h>

#include "app/library.h"
#include "hal/sdcard.h"

namespace net {

namespace {

Saved    saved_{};
uint32_t connect_deadline = 0;
bool     radio_on = false;
char     ip_[20];

constexpr const char* kSpace = "byog-net";

void radio()
{
    if (radio_on) return;
    WiFi.persistent(false);          // the board's own copy is ours (NVS "byog-net")
    WiFi.mode(WIFI_STA);
    radio_on = true;
}

// The mail server connection over WiFi (TLS checked against the built-in
// certificate authorities)
class TlsLink : public smtp::Link {
public:
    NetworkClientSecure c;
    bool write(const char* s, size_t n) override { return c.write(reinterpret_cast<const uint8_t*>(s), n) == n; }
    bool read_line(char* out, size_t cap) override
    {
        size_t k = 0;
        const uint32_t until = millis() + 20000;
        while (static_cast<int32_t>(millis() - until) < 0) {
            if (!c.available()) {
                if (!c.connected()) break;
                delay(5);
                continue;
            }
            const int ch = c.read();
            if (ch < 0) continue;
            if (ch == '\n') {
                out[k] = 0;
                return true;
            }
            if (ch != '\r' && k + 1 < cap) out[k++] = static_cast<char>(ch);
        }
        out[k] = 0;
        return false;
    }
    bool start_tls() override { return c.startTLS() == 1; }
};

} // namespace

void load()
{
    Preferences p;
    memset(&saved_, 0, sizeof saved_);
    if (!p.begin(kSpace, true)) return;
    p.getString("ssid", saved_.ssid, sizeof saved_.ssid);
    p.getString("wpass", saved_.wifi_pass, sizeof saved_.wifi_pass);
    p.getString("muser", saved_.mail_user, sizeof saved_.mail_user);
    p.getString("mpass", saved_.mail_pass, sizeof saved_.mail_pass);
    p.getString("mserv", saved_.mail_server, sizeof saved_.mail_server);
    saved_.mail_port = p.getUShort("mport", 465);
    p.end();
}

const Saved& saved() { return saved_; }

void save_wifi(const char* ssid, const char* pass)
{
    snprintf(saved_.ssid, sizeof saved_.ssid, "%s", ssid);
    snprintf(saved_.wifi_pass, sizeof saved_.wifi_pass, "%s", pass);
    Preferences p;
    if (!p.begin(kSpace, false)) return;
    p.putString("ssid", saved_.ssid);
    p.putString("wpass", saved_.wifi_pass);
    p.end();
}

void forget_wifi() { save_wifi("", ""); }

void save_mail(const char* user, const char* pass, const char* server, uint16_t port)
{
    snprintf(saved_.mail_user, sizeof saved_.mail_user, "%s", user);
    snprintf(saved_.mail_pass, sizeof saved_.mail_pass, "%s", pass);
    snprintf(saved_.mail_server, sizeof saved_.mail_server, "%s", server);
    saved_.mail_port = port;
    Preferences p;
    if (!p.begin(kSpace, false)) return;
    p.putString("muser", saved_.mail_user);
    p.putString("mpass", saved_.mail_pass);
    p.putString("mserv", saved_.mail_server);
    p.putUShort("mport", port);
    p.end();
}

void default_server(const char* address, char* server, size_t cap, uint16_t* port)
{
    const char* at = strrchr(address, '@');
    const char* dom = at ? at + 1 : "";
    struct Known {
        const char* domain;
        const char* server;
        uint16_t    port;
    };
    static const Known kKnown[] = {
        {"gmail.com", "smtp.gmail.com", 465},         {"googlemail.com", "smtp.gmail.com", 465},
        {"yahoo.com", "smtp.mail.yahoo.com", 465},    {"aol.com", "smtp.aol.com", 465},
        {"icloud.com", "smtp.mail.me.com", 587},      {"me.com", "smtp.mail.me.com", 587},
        {"mac.com", "smtp.mail.me.com", 587},         {"outlook.com", "smtp-mail.outlook.com", 587},
        {"hotmail.com", "smtp-mail.outlook.com", 587}, {"live.com", "smtp-mail.outlook.com", 587},
    };
    for (const Known& k : kKnown)
        if (strcasecmp(dom, k.domain) == 0) {
            snprintf(server, cap, "%s", k.server);
            *port = k.port;
            return;
        }
    snprintf(server, cap, "smtp.%s", dom);
    *port = 465;
}

void wifi_off()
{
    if (!radio_on) return;
    WiFi.scanDelete();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
    radio_on = false;
}

void scan_start()
{
    radio();
    WiFi.disconnect(false, false);
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
}

int scan_poll(Network* out, int cap)
{
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return -1;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;                 // hidden: "Other Network"
        const int rssi = WiFi.RSSI(i);
        int at = -1;
        for (int j = 0; j < k; ++j)
            if (strcmp(out[j].ssid, ssid.c_str()) == 0) at = j;
        if (at >= 0) {
            if (rssi > out[at].rssi) out[at].rssi = rssi;
            continue;
        }
        if (k == cap) continue;
        snprintf(out[k].ssid, sizeof out[k].ssid, "%s", ssid.c_str());
        out[k].rssi = rssi;
        out[k].secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        ++k;
    }
    WiFi.scanDelete();
    // strongest first
    for (int i = 1; i < k; ++i)
        for (int j = i; j > 0 && out[j].rssi > out[j - 1].rssi; --j) {
            const Network t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    return k;
}

void connect_start(const char* ssid, const char* pass)
{
    radio();
    WiFi.disconnect(false, false);
    WiFi.begin(ssid, pass && *pass ? pass : nullptr);
    connect_deadline = millis() + 20000;
}

Conn connect_poll()
{
    const wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) return Conn::Connected;
    if (s == WL_CONNECT_FAILED || static_cast<int32_t>(millis() - connect_deadline) >= 0) return Conn::Failed;
    return Conn::Connecting;
}

const char* ip_text()
{
    snprintf(ip_, sizeof ip_, "%s", WiFi.localIP().toString().c_str());
    return ip_;
}

bool send_mail(const char* to, const char* subject, const char* body, const char* const* files, int n_files,
               char* result, size_t cap, Progress progress, void* ctx)
{
    auto step = [&](const char* s) {
        if (progress) progress(s, ctx);
    };
    if (!saved_.ssid[0]) {
        snprintf(result, cap, "Set up WiFi first (Settings - WiFi).");
        return false;
    }
    if (!saved_.mail_user[0] || !saved_.mail_pass[0]) {
        snprintf(result, cap, "Set up the account it sends from first.");
        return false;
    }
    if (!radio_on || WiFi.status() != WL_CONNECTED) {
        char s[64];
        snprintf(s, sizeof s, "Connecting to %s...", saved_.ssid);
        step(s);
        connect_start(saved_.ssid, saved_.wifi_pass);
        Conn c;
        while ((c = connect_poll()) == Conn::Connecting) delay(100);
        if (c != Conn::Connected) {
            snprintf(result, cap, "Couldn't connect to %s.", saved_.ssid);
            return false;
        }
    }

    TlsLink* link = new (std::nothrow) TlsLink;
    if (!link) {
        snprintf(result, cap, "Not enough memory to send.");
        return false;
    }
    link->c.useBuiltinCACertBundle();
    link->c.setHandshakeTimeout(20);
    const bool starttls = saved_.mail_port != 465;
    if (starttls) link->c.setPlainStart();
    {
        char s[96];
        snprintf(s, sizeof s, "Connecting to %s...", saved_.mail_server);
        step(s);
    }
    if (!link->c.connect(saved_.mail_server, saved_.mail_port)) {
        snprintf(result, cap, "Couldn't reach %s (port %u).", saved_.mail_server, saved_.mail_port);
        delete link;
        return false;
    }

    // The attachments: the files that exist
    constexpr int kMaxFiles = 6;
    fs::File f[kMaxFiles];
    library::FileSource* src[kMaxFiles] = {};
    smtp::Attachment att[kMaxFiles];
    int n = 0;
    for (int i = 0; i < n_files && n < kMaxFiles; ++i) {
        f[n] = sd_fs().open(files[i], "r");
        if (!f[n]) continue;
        src[n] = new (std::nothrow) library::FileSource(f[n]);
        if (!src[n]) {
            f[n].close();
            continue;
        }
        const char* slash = strrchr(files[i], '/');
        att[n] = {slash ? slash + 1 : files[i], src[n]};
        ++n;
    }
    const smtp::Account a{saved_.mail_user, saved_.mail_pass, starttls};
    const smtp::Message m{saved_.mail_user, to, subject, body, att, n};
    char reply[128];
    const smtp::Status st = smtp::send(*link, a, m, reply, sizeof reply, progress, ctx);
    for (int i = 0; i < n; ++i) {
        delete src[i];
        f[i].close();
    }
    link->c.stop();
    delete link;
    if (st == smtp::Status::Ok) {
        snprintf(result, cap, "Sent to %s.", to);
        return true;
    }
    if (reply[0]) snprintf(result, cap, "%s %s", smtp::status_text(st), reply);
    else snprintf(result, cap, "%s", smtp::status_text(st));
    return false;
}

} // namespace net

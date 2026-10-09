// WiFi and mail (Tom, 2026-10-09: to get the logs off the board). WiFi is
// only switched on while the WiFi screen is open or mail is being sent -
// it takes memory the games need. The network and mail account are kept
// in the board's flash (NVS), never on the card.
#pragma once

#include <cstddef>
#include <cstdint>

#include "engine/smtp.h"

namespace net {

struct Saved {
    char     ssid[33];
    char     wifi_pass[65];
    char     mail_user[96];        // the player's own address: signs in, sends
    char     mail_pass[64];        // its app password
    char     mail_server[64];
    uint16_t mail_port;            // 465: TLS from the start; 587: STARTTLS
};

void load();
const Saved& saved();
void save_wifi(const char* ssid, const char* pass);
void forget_wifi();
void save_mail(const char* user, const char* pass, const char* server, uint16_t port);

// The usual mail server for an address ("tom@gmail.com" -> smtp.gmail.com, 465)
void default_server(const char* address, char* server, size_t cap, uint16_t* port);

// ---- WiFi ------------------------------------------------------------------
void wifi_off();

struct Network {
    char ssid[33];
    int  rssi;                     // dBm
    bool secure;
};
// Starts looking for networks; scan_poll: -1 still looking, else how many
// were found (put in out, strongest first, each name once)
void scan_start();
int  scan_poll(Network* out, int cap);

enum class Conn : uint8_t { Connecting, Connected, Failed };
void connect_start(const char* ssid, const char* pass);
Conn connect_poll();               // gives up after 20 s
const char* ip_text();             // "192.168.1.23" when connected

// ---- Mail ------------------------------------------------------------------
// Connects to the saved network (if not connected), then sends a message
// with the given SD card files attached (those that exist).
using Progress = smtp::Progress;
bool send_mail(const char* to, const char* subject, const char* body, const char* const* files, int n_files,
               char* result, size_t cap, Progress progress, void* ctx);

} // namespace net

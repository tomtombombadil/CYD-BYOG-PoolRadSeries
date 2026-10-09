// Sending an email (Tom, 2026-10-09: the board mails its logs): the SMTP
// conversation and the message (a text part and the logs as attachments).
// Plain C++, host-tested with a scripted server; the network side (WiFi,
// TLS) is the platform's - it hands over a Link.
//
// The board signs in to the player's own mail account (an app password:
// public mail servers don't take mail from a device that doesn't sign in).
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace smtp {

// The connection to the mail server
class Link {
public:
    virtual ~Link() = default;
    virtual bool write(const char* s, size_t n) = 0;
    // One line from the server, without its CR LF. False when nothing came
    // in time or the connection closed.
    virtual bool read_line(char* out, size_t cap) = 0;
    // Switches to TLS after STARTTLS. False if that fails.
    virtual bool start_tls() = 0;
};

struct Account {
    const char* user;        // the address that signs in (and sends)
    const char* password;    // the account's app password
    bool        starttls;    // the link starts plain (port 587); else it is TLS from the start (465)
};

struct Attachment {
    const char*      name;   // file name shown in the mail
    dax::ByteSource* data;
};

struct Message {
    const char*       from;
    const char*       to;
    const char*       subject;
    const char*       body;  // plain text ('\n' line ends)
    const Attachment* att;
    int               n_att;
};

enum class Status : uint8_t {
    Ok,
    NoGreeting,     // the server said nothing / not ready
    Hello,          // EHLO refused
    StartTls,       // STARTTLS refused or the TLS switch failed
    SignIn,         // the user name / password weren't accepted
    From,           // MAIL FROM refused
    To,             // RCPT TO refused
    Data,           // DATA refused
    NotSent,        // the server refused the message at its end
    Link,           // the connection broke while sending
};

// The steps as they happen ("Signing in...", "Sending..."), for the screen
using Progress = void (*)(const char* step, void* ctx);

// Sends m. reply gets the server's last line (its reason when refused).
Status send(Link& link, const Account& a, const Message& m, char* reply, size_t cap, Progress progress = nullptr,
            void* ctx = nullptr);

const char* status_text(Status s);

// Base64 of n bytes into out (4 * ((n + 2) / 3) characters, then a 0).
// Returns the characters written.
size_t base64(const uint8_t* in, size_t n, char* out);

} // namespace smtp

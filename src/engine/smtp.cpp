#include "smtp.h"

#include <cstdio>
#include <cstring>

namespace smtp {

namespace {

constexpr const char* kBoundary = "cyd-byog-part-6c2f91";

// A server reply: lines until one without '-' after its code. Returns the
// code (0: nothing / not a reply); the last line goes to reply.
int reply_code(Link& link, char* reply, size_t cap)
{
    char line[256];
    for (int n = 0; n < 64; ++n) {
        if (!link.read_line(line, sizeof line)) return 0;
        if (reply && cap) snprintf(reply, cap, "%s", line);
        if (strlen(line) < 3 || line[0] < '1' || line[0] > '5') return 0;
        if (line[3] != '-') return (line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0');
    }
    return 0;
}

bool put(Link& link, const char* s) { return link.write(s, strlen(s)); }

// A command line (CR LF added); the reply's code
int command(Link& link, const char* cmd, char* reply, size_t cap)
{
    if (!put(link, cmd) || !put(link, "\r\n")) return 0;
    return reply_code(link, reply, cap);
}

// Copies s without CR / LF (no extra header lines from a typed address)
void one_line(char* out, size_t cap, const char* s)
{
    size_t k = 0;
    for (; s && *s && k + 1 < cap; ++s)
        if (*s != '\r' && *s != '\n') out[k++] = *s;
    out[k] = 0;
}

// The text part: '\n' -> CR LF, a '.' starting a line doubled
bool put_text(Link& link, const char* s)
{
    char buf[130];
    size_t k = 0;
    bool line_start = true;
    for (; *s; ++s) {
        if (k > sizeof buf - 4) {
            if (!link.write(buf, k)) return false;
            k = 0;
        }
        if (*s == '\r') continue;
        if (*s == '\n') {
            buf[k++] = '\r';
            buf[k++] = '\n';
            line_start = true;
            continue;
        }
        if (line_start && *s == '.') buf[k++] = '.';
        buf[k++] = *s;
        line_start = false;
    }
    if (!line_start) {
        buf[k++] = '\r';
        buf[k++] = '\n';
    }
    return k == 0 || link.write(buf, k);
}

// An attachment's bytes in base64, 76 characters a line
bool put_base64(Link& link, dax::ByteSource& src)
{
    uint8_t in[57 * 8];
    char out[80];
    const uint32_t size = src.size();
    for (uint32_t pos = 0; pos < size;) {
        const size_t want = size - pos < sizeof in ? size - pos : sizeof in;
        const size_t got = src.read_at(pos, in, want);
        if (got == 0) return false;
        pos += static_cast<uint32_t>(got);
        for (size_t i = 0; i < got; i += 57) {
            const size_t n = got - i < 57 ? got - i : 57;
            size_t k = base64(in + i, n, out);
            out[k++] = '\r';
            out[k++] = '\n';
            if (!link.write(out, k)) return false;
        }
    }
    return true;
}

} // namespace

size_t base64(const uint8_t* in, size_t n, char* out)
{
    static const char kAlpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t k = 0;
    for (size_t i = 0; i < n; i += 3) {
        const uint32_t v = static_cast<uint32_t>(in[i]) << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        out[k++] = kAlpha[v >> 18 & 63];
        out[k++] = kAlpha[v >> 12 & 63];
        out[k++] = i + 1 < n ? kAlpha[v >> 6 & 63] : '=';
        out[k++] = i + 2 < n ? kAlpha[v & 63] : '=';
    }
    out[k] = 0;
    return k;
}

const char* status_text(Status s)
{
    switch (s) {
    case Status::Ok:         return "Sent.";
    case Status::NoGreeting: return "The mail server didn't answer.";
    case Status::Hello:      return "The mail server refused the board's hello.";
    case Status::StartTls:   return "The mail server's secure connection failed.";
    case Status::SignIn:     return "The mail server didn't accept the address / app password.";
    case Status::From:       return "The mail server refused the sender.";
    case Status::To:         return "The mail server refused the address it goes to.";
    case Status::Data:
    case Status::NotSent:    return "The mail server refused the message.";
    case Status::Link:       return "The connection to the mail server broke.";
    }
    return "?";
}

Status send(Link& link, const Account& a, const Message& m, char* reply, size_t cap, Progress progress, void* ctx)
{
    auto step = [&](const char* s) {
        if (progress) progress(s, ctx);
    };
    if (reply && cap) reply[0] = 0;
    if (reply_code(link, reply, cap) != 220) return Status::NoGreeting;
    if (command(link, "EHLO cyd-byog", reply, cap) != 250) return Status::Hello;
    if (a.starttls) {
        if (command(link, "STARTTLS", reply, cap) != 220 || !link.start_tls()) return Status::StartTls;
        if (command(link, "EHLO cyd-byog", reply, cap) != 250) return Status::Hello;
    }

    // AUTH PLAIN: base64 of "\0user\0password"
    step("Signing in...");
    char user[128], pass[128];
    one_line(user, sizeof user, a.user);
    one_line(pass, sizeof pass, a.password);
    uint8_t plain[260];
    const size_t nu = strlen(user), np = strlen(pass);
    plain[0] = 0;
    memcpy(plain + 1, user, nu);
    plain[1 + nu] = 0;
    memcpy(plain + 2 + nu, pass, np);
    char line[700];
    memcpy(line, "AUTH PLAIN ", 11);
    base64(plain, 2 + nu + np, line + 11);
    const int auth = command(link, line, reply, cap);
    memset(plain, 0, sizeof plain);
    memset(line, 0, sizeof line);
    memset(pass, 0, sizeof pass);
    if (auth != 235) return auth ? Status::SignIn : Status::Link;

    char from[128], to[128];
    one_line(from, sizeof from, m.from);
    one_line(to, sizeof to, m.to);
    snprintf(line, sizeof line, "MAIL FROM:<%s>", from);
    if (command(link, line, reply, cap) != 250) return Status::From;
    snprintf(line, sizeof line, "RCPT TO:<%s>", to);
    const int rc = command(link, line, reply, cap);
    if (rc != 250 && rc != 251) return Status::To;
    if (command(link, "DATA", reply, cap) != 354) return Status::Data;

    step("Sending...");
    char subject[160];
    one_line(subject, sizeof subject, m.subject);
    snprintf(line, sizeof line,
             "From: <%s>\r\nTo: <%s>\r\nSubject: %s\r\nMIME-Version: 1.0\r\n"
             "Content-Type: multipart/mixed; boundary=\"%s\"\r\n\r\n"
             "--%s\r\nContent-Type: text/plain; charset=us-ascii\r\nContent-Transfer-Encoding: 7bit\r\n\r\n",
             from, to, subject, kBoundary, kBoundary);
    if (!put(link, line) || !put_text(link, m.body ? m.body : "")) return Status::Link;
    for (int i = 0; i < m.n_att; ++i) {
        char name[64];
        one_line(name, sizeof name, m.att[i].name);
        for (char* p = name; *p; ++p)
            if (*p == '"') *p = '\'';
        snprintf(line, sizeof line,
                 "--%s\r\nContent-Type: text/plain; charset=us-ascii; name=\"%s\"\r\n"
                 "Content-Disposition: attachment; filename=\"%s\"\r\nContent-Transfer-Encoding: base64\r\n\r\n",
                 kBoundary, name, name);
        if (!put(link, line) || !put_base64(link, *m.att[i].data)) return Status::Link;
    }
    snprintf(line, sizeof line, "--%s--\r\n.", kBoundary);
    const int sent = command(link, line, reply, cap);
    if (sent != 250) return sent ? Status::NotSent : Status::Link;
    command(link, "QUIT", nullptr, 0);
    return Status::Ok;
}

} // namespace smtp

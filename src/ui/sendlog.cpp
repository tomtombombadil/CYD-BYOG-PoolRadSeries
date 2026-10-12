#include "sendlog.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/library.h"
#include "engine/log_pack.h"
#include "engine/third_party/qrcodegen.h"
#include "hal/memlog.h"
#include "hal/sdcard.h"

namespace sendlog {

namespace {

// Under the code (Tom's words in CYD-Classic-Games, "log" -> "logs")
const char* const kHow = "Point your phone's camera at the QR code to get a link that sends the logs to the developer.";

// The logs, most wanted first, and each one's share of the room (percent);
// what one doesn't need goes to the next ones in this order
struct Section {
    const char* file;
    const char* title;
    int share;
};
constexpr int kSections = 5;
const Section kSection[kSections] = {
    {"ERRORS.TXT", "Errors", 25},
    {"MEMORY.TXT", "Memory", 25},
    {"MEMORY.OLD", "Memory, the session before", 20},
    {"RESTART.TXT", "Restarts", 10},
    {"SCAN.TXT", "Card Scan", 20},
};

const char* version_ = "?";
const char* build_ = "";

bool        up = false;
uint8_t*    qr = nullptr;          // the code while the screen is up
const char* problem = nullptr;     // instead of the code
uint32_t    packed_bytes = 0;      // how much went in (0: all of it)

// ---- the text --------------------------------------------------------------------------

uint32_t sizes[kSections];
uint32_t total = 0;

void path_of(int i, char* out, size_t cap) { library::cache_path(kSection[i].file, out, cap); }

void read_sizes()
{
    total = 0;
    for (int i = 0; i < kSections; ++i) {
        sizes[i] = 0;
        char p[96];
        path_of(i, p, sizeof p);
        if (!sd_fs().exists(p)) continue;
        fs::File f = sd_fs().open(p, "r");
        if (!f) continue;
        sizes[i] = static_cast<uint32_t>(f.size());
        f.close();
        total += sizes[i];
    }
}

// The newest `want` bytes of log i at out (from a line's start); its length
size_t read_tail(int i, uint32_t want, char* out)
{
    if (!want) return 0;
    char p[96];
    path_of(i, p, sizeof p);
    fs::File f = sd_fs().open(p, "r");
    if (!f) return 0;
    f.seek(sizes[i] - want);
    // in small pieces: one big read can make the card driver borrow a
    // buffer as big as the read
    size_t got = 0;
    while (got < want) {
        const size_t piece = want - got < 1024 ? want - got : 1024;
        const size_t r = f.read(reinterpret_cast<uint8_t*>(out + got), piece);
        got += r;
        if (r < piece) break;
    }
    f.close();
    if (want < sizes[i]) {                 // from the next whole line on
        size_t k = 0;
        while (k < got && out[k] != '\n') ++k;
        if (k < got) ++k;
        memmove(out, out + k, got - k);
        got -= k;
    }
    return got;
}

// The report in at most `budget` bytes (cap > budget); its length
size_t build(char* out, size_t cap, size_t budget, bool* all)
{
    int n = snprintf(out, cap,
                     "CYD BYOG Gold Box logs\nBoard: %s\nFirmware: %s%s%s%s\n"
                     "Memory now: free %u KB, largest block %u KB\n",
                     BOARD_NAME, version_, *build_ ? " (" : "", build_, *build_ ? ")" : "",
                     memlog::free_kb(), memlog::largest_kb());
    if (n < 0 || static_cast<size_t>(n) >= cap) return 0;
    size_t len = static_cast<size_t>(n);
    constexpr size_t kTitleRoom = 48;      // "\n==== <title> (newest lines) ====\n"
    const size_t fixed = len + kTitleRoom * kSections;
    const size_t room = budget > fixed ? budget - fixed : 0;
    uint32_t give[kSections];
    size_t used = 0;
    for (int i = 0; i < kSections; ++i) {
        const size_t s = room * kSection[i].share / 100;
        give[i] = sizes[i] < s ? sizes[i] : static_cast<uint32_t>(s);
        used += give[i];
    }
    for (int i = 0; i < kSections && used < room; ++i) {
        const size_t more = sizes[i] - give[i] < room - used ? sizes[i] - give[i] : room - used;
        give[i] += static_cast<uint32_t>(more);
        used += more;
    }
    *all = true;
    for (int i = 0; i < kSections; ++i) {
        const bool cut = give[i] < sizes[i];
        *all &= !cut;
        n = snprintf(out + len, cap - len, sizes[i] ? "\n==== %s%s ====\n" : "\n==== %s: none ====\n",
                     kSection[i].title, cut ? " (newest lines)" : "");
        if (n < 0 || len + n >= cap) return 0;
        len += static_cast<size_t>(n);
        if (len + give[i] >= cap) return 0;
        len += read_tail(i, give[i], out + len);
    }
    out[len] = 0;
    return len;
}

// ---- the code ----------------------------------------------------------------------

// Everything packing needs, freed together
struct Work {
    char*    text = nullptr;
    size_t   text_cap = 0;
    uint8_t* packed = nullptr;     // format byte + DEFLATE
    size_t   packed_cap = 0;
    char*    b43 = nullptr;
    size_t   b43_cap = 0;
    uint8_t* url_seg = nullptr;
    uint8_t* b43_seg = nullptr;
    uint8_t* tmp = nullptr;
    int      maxv = 1;
    bool ok() const { return text && packed && b43 && url_seg && b43_seg && tmp; }
    void free_all()
    {
        free(text); free(packed); free(b43); free(url_seg); free(b43_seg); free(tmp);
        *this = Work{};
    }
};

int side() { return ui::height(); }    // the code's square, at the left

// The newest `budget` bytes as a code no bigger than w.maxv into qr
bool encode(Work& w, size_t budget, bool final, bool* all)
{
    const size_t n = build(w.text, w.text_cap, budget, all);
    if (!n) return false;
    w.packed[0] = logpack::kFormat;
    const size_t z = logpack::deflate(reinterpret_cast<const uint8_t*>(w.text), n, w.packed + 1, w.packed_cap - 1);
    if (!z) return false;
    const size_t len = logpack::base43(w.packed, z + 1, w.b43, w.b43_cap);
    if (!len) return false;
    qrcodegen_Segment segs[2] = {
        qrcodegen_makeBytes(reinterpret_cast<const uint8_t*>(logpack::kUrl), strlen(logpack::kUrl), w.url_seg),
        qrcodegen_makeAlphanumeric(w.b43, w.b43_seg)};
    return qrcodegen_encodeSegmentsAdvanced(segs, 2, qrcodegen_Ecc_LOW, qrcodegen_VERSION_MIN, w.maxv,
                                            final ? qrcodegen_Mask_AUTO : qrcodegen_Mask_0, final, w.tmp, qr);
}

const char* info_text(char* out, size_t cap)
{
    const unsigned all_kb = static_cast<unsigned>((total + 1023) / 1024);
    if (!packed_bytes) snprintf(out, cap, "All of the logs (%u KB).", all_kb);
    else snprintf(out, cap, "The newest lines: %u KB of %u KB.", static_cast<unsigned>((packed_bytes + 1023) / 1024), all_kb);
    return out;
}

void make()
{
    problem = nullptr;
    packed_bytes = 0;
    if (!sd_begin()) { problem = "No SD card found."; return; }
    read_sizes();
    if (!total) { problem = "The logs are empty."; return; }

    // Modules 2 px at least, a 3-module white edge: the biggest version that fits
    Work w;
    const int most = side() / 2 - 6;
    w.maxv = (most - 17) / 4;
    if (w.maxv > qrcodegen_VERSION_MAX) w.maxv = qrcodegen_VERSION_MAX;
    const size_t qlen = qrcodegen_BUFFER_LEN_FOR_VERSION(w.maxv);
    // (more text than this never fits: logs pack about 4-6 to 1)
    const size_t most_text = ui::large() ? 24000 : 14000;
    const size_t want = total + 512 < most_text ? total + 512 : most_text;
    w.text_cap = want + 64;
    w.packed_cap = qlen;                   // more than the code's data can hold
    w.b43_cap = qlen * 3 / 2 + 4;
    qr = static_cast<uint8_t*>(malloc(qlen));
    w.tmp = static_cast<uint8_t*>(malloc(qlen));
    w.text = static_cast<char*>(malloc(w.text_cap));
    w.packed = static_cast<uint8_t*>(malloc(w.packed_cap));
    w.b43 = static_cast<char*>(malloc(w.b43_cap));
    w.url_seg = static_cast<uint8_t*>(malloc(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_BYTE, strlen(logpack::kUrl))));
    w.b43_seg = static_cast<uint8_t*>(malloc(qrcodegen_calcSegmentBufferSize(qrcodegen_Mode_ALPHANUMERIC, w.b43_cap)));
    if (!qr || !w.ok()) {
        memlog::note("NOT ENOUGH MEMORY for Send Logs");
        problem = "Not enough memory to make the QR code.";
        free(qr);
        qr = nullptr;
        w.free_all();
        return;
    }
    // The most text that fits: all of it, or a search between 600 bytes and that
    bool all = false;
    size_t best = 0;
    if (encode(w, want, false, &all)) {
        best = want;
    } else {
        size_t lo = 600, hi = want;
        for (int k = 0; k < 9 && lo + 128 < hi; ++k) {
            const size_t mid = (lo + hi) / 2;
            if (encode(w, mid, false, &all)) { best = mid; lo = mid; }
            else hi = mid;
        }
        if (!best && encode(w, lo, false, &all)) best = lo;
    }
    if (!best || !encode(w, best, true, &all)) {
        problem = "The logs couldn't be packed.";
        free(qr);
        qr = nullptr;
    } else if (!all) {
        packed_bytes = static_cast<uint32_t>(best);
    }
    w.free_all();
    char info[64], line[80];
    snprintf(line, sizeof line, "Send Logs: %s", qr ? info_text(info, sizeof info) : problem);
    memlog::note(line);
}

// ---- the screen --------------------------------------------------------------------

ui::Rect back_key()
{
    const int x = side() + ui::gap();
    return {x, ui::height() - ui::key_h() - ui::gap(), ui::width() - x - ui::gap(), ui::key_h()};
}

// Words wrapped into width w from (x, y); returns the y under them
int para(int x, int y, int w, const char* s, uint16_t col, ui::Font f)
{
    const int lh = ui::line_h(f) + 1;
    char line[64];
    while (*s) {
        while (*s == ' ') ++s;
        size_t fit = 0, k = 0;
        // the longest run of whole words that fits
        while (s[k]) {
            size_t e = k;
            while (s[e] == ' ') ++e;
            while (s[e] && s[e] != ' ') ++e;
            if (e >= sizeof line) break;
            memcpy(line, s, e);
            line[e] = 0;
            if (fit && ui::text_width(line, f) > w) break;
            fit = e;
            k = e;
        }
        if (!fit) break;
        memcpy(line, s, fit);
        line[fit] = 0;
        ui::text(x, y, line, col, f);
        y += lh;
        s += fit;
    }
    return y;
}

void draw_left_message(const char* s)
{
    LGFX& g = ui::gfx();
    g.fillRect(0, 0, side(), ui::height(), style::kBackground);
    const int pad = ui::gap() * 3;
    para(pad, ui::height() / 2 - ui::line_h() * 2, side() - pad * 2, s, style::kText, ui::Font::Normal);
}

void draw_code()
{
    LGFX& g = ui::gfx();
    const int n = qrcodegen_getSize(qr);
    const int m = side() / (n + 6);
    const int q = (n + 6) * m;
    const int ox = (side() - q) / 2, oy = (ui::height() - q) / 2;
    g.startWrite();
    g.fillRect(0, 0, side(), ui::height(), style::kBackground);
    g.fillRect(ox, oy, q, q, 0xFFFF);
    const int x0 = ox + 3 * m, y0 = oy + 3 * m;
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n;) {
            if (!qrcodegen_getModule(qr, x, y)) { ++x; continue; }
            int e = x;
            while (e < n && qrcodegen_getModule(qr, e, y)) ++e;
            g.fillRect(x0 + x * m, y0 + y * m, (e - x) * m, m, 0x0000);
            x = e;
        }
    g.endWrite();
}

void draw_side()
{
    const int x = side() + ui::gap(), w = ui::width() - x - ui::gap();
    int y = ui::gap() * 2;
    ui::text(x, y, "Send Logs", style::kGold, ui::Font::Normal);
    y += ui::line_h() + ui::gap() * 2;
    if (qr) {
        y = para(x, y, w, kHow, style::kText, ui::Font::Small);
        y += ui::gap() * 2;
        char info[64];
        para(x, y, w, info_text(info, sizeof info), style::kTextMuted, ui::Font::Small);
    }
    ui::key(back_key(), "Back");
}

} // namespace

void set_build(const char* version, const char* build)
{
    version_ = version ? version : "?";
    build_ = build ? build : "";
}

void open()
{
    close();
    up = true;
    // a message up before the slow part (Tom's rule)
    ui::clear();
    draw_left_message("Packing the logs...");
    draw_side();
    make();
    draw();
}

void close()
{
    free(qr);
    qr = nullptr;
    problem = nullptr;
    up = false;
}

bool is_open() { return up; }

void draw()
{
    ui::clear();
    if (qr) draw_code();
    else draw_left_message(problem ? problem : "");
    draw_side();
}

bool tap(const ui::Tap& t)
{
    if (!back_key().contains(t.x, t.y)) return true;
    close();
    return false;
}

} // namespace sendlog

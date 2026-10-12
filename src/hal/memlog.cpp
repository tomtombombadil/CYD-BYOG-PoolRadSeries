#include "memlog.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "app/library.h"
#include "hal/sdcard.h"

namespace memlog {

namespace {
constexpr unsigned kLowKb = 24;     // a fight wants about this in one piece
bool     ready = false;
bool     was_low = false;
uint32_t next_minute = 0;

void path(char* out, size_t cap, const char* file) { library::cache_path(file, out, cap); }
} // namespace

unsigned free_kb() { return static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT) / 1024); }
unsigned largest_kb() { return static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) / 1024); }

void begin(const char* version, const char* board)
{
    if (!sd_begin()) return;
    char now[96], old[96];
    path(now, sizeof now, "MEMORY.TXT");
    path(old, sizeof old, "MEMORY.OLD");
    if (sd_fs().exists(old)) sd_fs().remove(old);
    if (sd_fs().exists(now)) sd_fs().rename(now, old);
    ready = true;
    char line[96];
    snprintf(line, sizeof line, "%s on the %s", version, board);
    note(line);
}

void note(const char* what)
{
    const unsigned fr = free_kb(), lg = largest_kb();
    const unsigned lo = static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT) / 1024);
    const uint32_t s = millis() / 1000;
    char line[160];
    const int n = snprintf(line, sizeof line, "[%u:%02u:%02u] %s: free %u KB, largest block %u KB, lowest %u KB\n",
                           (unsigned)(s / 3600), (unsigned)(s / 60 % 60), (unsigned)(s % 60), what, fr, lg, lo);
    Serial.print(line);
    if (!ready || n <= 0) return;
    char p[96];
    path(p, sizeof p, "MEMORY.TXT");
    fs::File f = sd_fs().open(p, "a");      // (fails quietly when the card's 3 file slots are in use)
    if (!f) return;
    f.write(reinterpret_cast<const uint8_t*>(line), n < (int)sizeof line ? n : sizeof line - 1);
    f.close();
}

void tick(bool active)
{
    const uint32_t now = millis();
    const bool low = largest_kb() < kLowKb;
    if (low && !was_low) note("LOW - the largest block is getting small");
    was_low = low;
    if (!active || static_cast<int32_t>(now - next_minute) < 0) return;
    next_minute = now + 60000;
    note("a minute");
}

} // namespace memlog

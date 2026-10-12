// The emulator build's card and input (see src/hal/qemu.h). Never in a
// board build: tools/qemu/qemu.ini adds this library.
#include "hal/qemu.h"

#include <Arduino.h>
#include <SD_MMC.h>
#include <esp_heap_caps.h>

namespace qemu {

namespace {
bool mounted = false;
// The script: "x y wait_ms" a line (# comments); "mem" prints the heap
struct Step {
    int16_t  x, y;                  // x < 0: a heap line only
    uint32_t wait;
};
Step     steps[256];
int      n_steps = -1, at = 0, phase = 0;
uint32_t next = 0;

void mem(const char* why)
{
    Serial.printf("[qemu] %s: heap free %u, largest block %u, min free %u\n", why,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
}

void load()
{
    n_steps = 0;
    File f = SD_MMC.open("/GOLDBOX/_QEMU/TAPS.TXT", "r");
    if (!f) {
        Serial.println("[qemu] no TAPS.TXT");
        return;
    }
    char line[64];
    while (f.available() && n_steps < 256) {
        const size_t n = f.readBytesUntil('\n', line, sizeof line - 1);
        line[n] = 0;
        if (line[0] == '#' || !line[0]) continue;
        int x, y;
        unsigned w = 1000;
        if (!strncmp(line, "mem", 3)) {
            sscanf(line + 3, "%u", &w);
            steps[n_steps++] = Step{-1, -1, w};
        } else if (sscanf(line, "%d %d %u", &x, &y, &w) >= 2) {
            steps[n_steps++] = Step{static_cast<int16_t>(x), static_cast<int16_t>(y), w};
        }
    }
    f.close();
    Serial.printf("[qemu] %d steps\n", n_steps);
    next = millis() + 3000;
}
} // namespace

bool sd_begin()
{
    if (mounted) return true;
    mounted = SD_MMC.begin("/sd", true, false, 20000, 3);
    Serial.printf("[qemu] card %s\n", mounted ? "mounted (SD/MMC)" : "not mounted");
    return mounted;
}

fs::FS& sd_fs() { return SD_MMC; }

void sd_end()
{
    if (!mounted) return;
    SD_MMC.end();
    mounted = false;
}

bool touch(int32_t* x, int32_t* y)
{
    if (!mounted) return false;
    if (n_steps < 0) load();
    const uint32_t now = millis();
    if (at >= n_steps) {
        if (at == n_steps) {
            mem("script done");
            ++at;
        }
        return false;
    }
    if (static_cast<int32_t>(now - next) < 0) return false;
    const Step& s = steps[at];
    if (s.x < 0) {
        mem("mem");
        ++at;
        next = now + s.wait;
        return false;
    }
    // pressed for 4 polls (the filter wants 2 that agree; QEMU's polls can
    // be far apart in time), then released
    if (phase == 0) Serial.printf("[qemu] tap %d,%d\n", s.x, s.y);
    if (phase < 4) {
        ++phase;
        *x = s.x;
        *y = s.y;
        return true;
    }
    phase = 0;
    ++at;
    next = now + s.wait;
    char why[32];
    snprintf(why, sizeof why, "after tap %d", at);
    mem(why);
    return false;
}

} // namespace qemu

// The emulator build's card and input (see src/hal/qemu.h). Never in a
// board build: tools/qemu/qemu.ini adds this library.
#include "hal/qemu.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#if defined(CYD_QEMU_FLASHCARD)
// The S3 build: the card is a read-only FAT partition ("card") in the flash
#include <esp_partition.h>
#include <esp_vfs_fat.h>
#include <vfs_api.h>
namespace {
struct FlashCard : fs::FS {
    FlashCard() : fs::FS(fs::FSImplPtr(new VFSImpl())) {}
    void at(const char* mp) { _impl->mountpoint(mp); }
} card;
}
#define CARD card
#else
#include <SD_MMC.h>
#define CARD SD_MMC
#endif

namespace qemu {

namespace {
bool mounted = false;
// The script: "x y wait_ms" a line (# comments); "mem" prints the heap;
// "journal J 4 [wait_ms]" opens journal entry J 4 (the viewer's memory
// and stack without playing to a mention)
struct Step {
    int16_t  x, y;                  // x < 0: a heap line only (-3: a journal entry, kind, number y)
    uint32_t wait;
    char     kind;
};
char     journal_kind = 0;
int      journal_number = 0;
Step     steps[256];
int      n_steps = -1, at = 0, phase = 0;
uint32_t next = 0;

void mem(const char* why)
{
    Serial.printf("[qemu] %s: heap free %u, largest block %u, min free %u, loop stack never used %u\n", why,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                  (unsigned)uxTaskGetStackHighWaterMark(nullptr));
}

void load()
{
    n_steps = 0;
    File f = CARD.open("/GOLDBOX/_QEMU/TAPS.TXT", "r");
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
        if (!strncmp(line, "panic", 5)) {             // (a crash on purpose: the Restarts log's crash line)
            steps[n_steps++] = Step{-2, -2, 0, 0};
        } else if (!strncmp(line, "journal", 7)) {
            char k = 0;
            int num = 0;
            if (sscanf(line + 7, " %c %d %u", &k, &num, &w) >= 2)
                steps[n_steps++] = Step{-3, static_cast<int16_t>(num), w, k};
        } else if (!strncmp(line, "mem", 3)) {
            sscanf(line + 3, "%u", &w);
            steps[n_steps++] = Step{-1, -1, w, 0};
        } else if (sscanf(line, "%d %d %u", &x, &y, &w) >= 2) {
            steps[n_steps++] = Step{static_cast<int16_t>(x), static_cast<int16_t>(y), w, 0};
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
#if defined(CYD_QEMU_FLASHCARD)
    static bool tried = false;          // (mounted once; never unmounted)
    if (tried) return false;
    tried = true;
    esp_vfs_fat_mount_config_t mc = {};
    mc.max_files = 3;
    mc.format_if_mount_failed = false;
    const esp_err_t e = esp_vfs_fat_spiflash_mount_ro("/sd", "card", &mc);
    mounted = e == ESP_OK;
    if (mounted) card.at("/sd");
    else {
        Serial.printf("[qemu] flash card: %s\n", esp_err_to_name(e));
        const esp_partition_t* pt = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "card");
        uint8_t b[512];
        if (pt && esp_partition_read(pt, 0, b, sizeof b) == ESP_OK)
            Serial.printf("[qemu] card partition at 0x%lx: %02x %02x %02x .. %02x %02x\n", (unsigned long)pt->address,
                          b[0], b[1], b[2], b[510], b[511]);
        else Serial.println("[qemu] no card partition");
    }
    Serial.printf("[qemu] card %s\n", mounted ? "mounted (flash FAT, read-only)" : "not mounted");
#else
    mounted = SD_MMC.begin("/sd", true, false, 20000, 3);
    Serial.printf("[qemu] card %s\n", mounted ? "mounted (SD/MMC)" : "not mounted");
#endif
    return mounted;
}

fs::FS& sd_fs() { return CARD; }

void sd_end()
{
#if !defined(CYD_QEMU_FLASHCARD)
    if (!mounted) return;
    SD_MMC.end();
    mounted = false;
#endif
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
    if (s.x == -2) {
        Serial.println("[qemu] crashing on purpose");
        *reinterpret_cast<volatile int*>(4) = 1;
    }
    if (s.x == -3) {
        journal_kind = s.kind;
        journal_number = s.y;
        Serial.printf("[qemu] journal %c %d\n", s.kind, s.y);
        ++at;
        next = now + s.wait;
        return false;
    }
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

bool journal(char* kind, int* number)
{
    if (!journal_kind) return false;
    *kind = journal_kind;
    *number = journal_number;
    journal_kind = 0;
    return true;
}

void mem_now(const char* why) { mem(why); }

} // namespace qemu

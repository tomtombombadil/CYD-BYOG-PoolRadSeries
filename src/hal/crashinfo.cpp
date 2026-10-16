#include "crashinfo.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sdkconfig.h>

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
#include <esp_core_dump.h>
#include <esp_err.h>
#define HAVE_DUMP 1
#endif

bool crash_summary(char* out, size_t cap)
{
    if (!cap) return false;
    out[0] = 0;
#if defined(HAVE_DUMP)
    const esp_err_t ce = esp_core_dump_image_check();
    if (ce != ESP_OK) {
        printf("[boot] no crash dump (%s)\n", esp_err_to_name(ce));
        return false;
    }
    auto* s = static_cast<esp_core_dump_summary_t*>(malloc(sizeof(esp_core_dump_summary_t)));
    if (!s) return false;
    const esp_err_t se = esp_core_dump_get_summary(s);
    if (se != ESP_OK) printf("[boot] the crash dump's summary: %s\n", esp_err_to_name(se));
    bool ok = se == ESP_OK;
    if (ok) {
        s->exc_task[sizeof s->exc_task - 1] = 0;
        int n = snprintf(out, cap, "crash in task '%s' at PC 0x%08lx", s->exc_task,
                         static_cast<unsigned long>(s->exc_pc));
#if defined(__XTENSA__)
        if (n > 0 && static_cast<size_t>(n) < cap)
            n += snprintf(out + n, cap - n, ", cause %lu, address 0x%08lx, backtrace",
                          static_cast<unsigned long>(s->ex_info.exc_cause),
                          static_cast<unsigned long>(s->ex_info.exc_vaddr));
        for (uint32_t i = 0; i < s->exc_bt_info.depth && i < 16 && n > 0 && static_cast<size_t>(n) < cap; ++i)
            n += snprintf(out + n, cap - n, " 0x%08lx", static_cast<unsigned long>(s->exc_bt_info.bt[i]));
        if (s->exc_bt_info.corrupted && n > 0 && static_cast<size_t>(n) < cap)
            snprintf(out + n, cap - n, " (corrupted)");
#else
        // RISC-V: no backtrace on the board - the return address, and the
        // stack's words that look like code addresses (most of a backtrace)
        if (n > 0 && static_cast<size_t>(n) < cap)
            n += snprintf(out + n, cap - n, ", cause %lu, value 0x%08lx, ra 0x%08lx, sp 0x%08lx, stack",
                          static_cast<unsigned long>(s->ex_info.mcause), static_cast<unsigned long>(s->ex_info.mtval),
                          static_cast<unsigned long>(s->ex_info.ra), static_cast<unsigned long>(s->ex_info.sp));
        int shown = 0;
        for (uint32_t i = 0; i + 4 <= s->exc_bt_info.dump_size && shown < 16 && n > 0 && static_cast<size_t>(n) < cap;
             i += 4) {
            uint32_t w;
            memcpy(&w, s->exc_bt_info.stackdump + i, 4);
            if ((w >> 24) == 0x42 || (w >> 24) == 0x40) {
                n += snprintf(out + n, cap - n, " 0x%08lx", static_cast<unsigned long>(w));
                ++shown;
            }
        }
#endif
    }
    free(s);
    esp_core_dump_image_erase();        // (told once)
    return ok;
#else
    return false;
#endif
}

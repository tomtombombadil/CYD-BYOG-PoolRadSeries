#pragma once
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstdarg>
extern uint32_t g_now;
inline uint32_t millis() { return g_now; }
struct SerialT {
    void printf(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
    void println(const char* s) { puts(s); }
};
extern SerialT Serial;

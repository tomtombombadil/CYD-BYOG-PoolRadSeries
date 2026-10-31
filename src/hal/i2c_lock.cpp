#include "i2c_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
// Made on first use - whichever task gets there first: on the capacitive
// boards that is the touch task (ui.cpp's sampler, core 0), which can start
// before the main task's first codec / calibration lock. A static mutex,
// made inside a critical section, so two first uses at once still make one.
StaticSemaphore_t storage;
SemaphoreHandle_t mtx = nullptr;
portMUX_TYPE init_mux = portMUX_INITIALIZER_UNLOCKED;

SemaphoreHandle_t the_mutex()
{
    if (!mtx) {
        portENTER_CRITICAL(&init_mux);
        if (!mtx) mtx = xSemaphoreCreateRecursiveMutexStatic(&storage);
        portEXIT_CRITICAL(&init_mux);
    }
    return mtx;
}
}

void i2c_lock()
{
    if (SemaphoreHandle_t m = the_mutex()) xSemaphoreTakeRecursive(m, portMAX_DELAY);
}

void i2c_unlock()
{
    if (mtx) xSemaphoreGiveRecursive(mtx);
}

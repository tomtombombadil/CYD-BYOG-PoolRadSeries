#include "i2c_lock.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
SemaphoreHandle_t mtx = nullptr;
}

void i2c_lock()
{
    if (!mtx) mtx = xSemaphoreCreateRecursiveMutex();     // (first use: setup(), before the touch task)
    if (mtx) xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
}

void i2c_unlock()
{
    if (mtx) xSemaphoreGiveRecursive(mtx);
}

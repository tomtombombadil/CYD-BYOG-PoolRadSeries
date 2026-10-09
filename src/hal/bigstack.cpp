#include "bigstack.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace {

struct Job {
    void (*fn)(void*);
    void*             ctx;
    SemaphoreHandle_t done;
};

void trampoline(void* p)
{
    Job* j = static_cast<Job*>(p);
    j->fn(j->ctx);
    xSemaphoreGive(j->done);
    vTaskDelete(nullptr);
}

} // namespace

bool run_on_big_stack(void (*fn)(void*), void* ctx, size_t stack)
{
    Job j{fn, ctx, xSemaphoreCreateBinary()};
    if (!j.done) return false;
    TaskHandle_t h = nullptr;
    // Same priority and core as the caller: it simply waits meanwhile
    if (xTaskCreatePinnedToCore(trampoline, "deep", stack, &j, uxTaskPriorityGet(nullptr), &h, xPortGetCoreID()) !=
        pdPASS) {
        vSemaphoreDelete(j.done);
        return false;
    }
    xSemaphoreTake(j.done, portMAX_DELAY);
    vSemaphoreDelete(j.done);
    return true;
}

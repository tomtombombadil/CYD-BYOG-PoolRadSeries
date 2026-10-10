#include "audio.h"

#include <Arduino.h>
#include <driver/dac_continuous.h>

#include "boards/board_select.h"

namespace {

constexpr int kBuf = 512;               // samples a DMA buffer (23 ms)
constexpr int kBufs = 4;
constexpr int kRamp = 256;              // samples to move between 0 V and the middle (no click)

TaskHandle_t task = nullptr;
volatile bool stop_wanted = false;
volatile bool ending = false;           // the task is on its way out
AudioFill fill_fn = nullptr;
void* fill_ctx = nullptr;

void amp(bool on)
{
#ifdef BOARD_PIN_AUDIO_EN
    digitalWrite(BOARD_PIN_AUDIO_EN, on ? LOW : HIGH);
#else
    (void)on;
#endif
}

void audio_task(void*)
{
    dac_continuous_handle_t h = nullptr;
    dac_continuous_config_t cfg = {};
    cfg.chan_mask = DAC_CHANNEL_MASK_CH1;           // GPIO 26
    cfg.desc_num = kBufs;
    cfg.buf_size = kBuf;
    cfg.freq_hz = kAudioHz;
    cfg.offset = 0;
    cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
    cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
    uint8_t* buf = static_cast<uint8_t*>(malloc(kBuf));
    if (!buf || dac_continuous_new_channels(&cfg, &h) != ESP_OK || dac_continuous_enable(h) != ESP_OK) {
        Serial.println("[audio] the DAC didn't start");
        if (h) dac_continuous_del_channels(h);
        free(buf);
        task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    size_t done = 0;
    // From 0 V up to the middle, then the amplifier on
    for (int i = 0; i < kBuf; ++i) buf[i] = static_cast<uint8_t>(i < kRamp ? i * 128 / kRamp : 128);
    dac_continuous_write(h, buf, kBuf, &done, -1);
    amp(true);
    int quiet = 0;
    for (;;) {
        while (!stop_wanted && quiet < 4) {
            const bool more = fill_fn(buf, kBuf, fill_ctx);
            quiet = more ? 0 : quiet + 1;
            dac_continuous_write(h, buf, kBuf, &done, -1);
        }
        // A sound started while the last quiet buffer played (audio_play
        // saw this task still going): keep going with it
        ending = true;
        if (stop_wanted || !fill_fn(buf, kBuf, fill_ctx)) break;
        ending = false;
        quiet = 0;
        dac_continuous_write(h, buf, kBuf, &done, -1);
    }
    // Down to 0 V again, amplifier off
    amp(false);
    for (int i = 0; i < kBuf; ++i) buf[i] = static_cast<uint8_t>(i < kRamp ? 128 - i * 128 / kRamp : 0);
    dac_continuous_write(h, buf, kBuf, &done, -1);
    memset(buf, 0, kBuf);
    dac_continuous_write(h, buf, kBuf, &done, -1);
    dac_continuous_disable(h);
    dac_continuous_del_channels(h);
    free(buf);
    task = nullptr;
    vTaskDelete(nullptr);
}

} // namespace

void audio_play(AudioFill fill, void* ctx)
{
    if (task && ending)
        for (int i = 0; i < 100 && task; ++i) delay(2);     // (ending: start again after it)
    if (task || !fill) return;
    ending = false;
    fill_fn = fill;
    fill_ctx = ctx;
    stop_wanted = false;
    if (xTaskCreatePinnedToCore(audio_task, "audio", 3072, nullptr, 4, &task, 0) != pdPASS) {
        task = nullptr;
        Serial.println("[audio] no memory for the sound task");
    }
}

void audio_stop()
{
    if (!task) return;
    stop_wanted = true;
    for (int i = 0; i < 200 && task; ++i) delay(5);
}

bool audio_running() { return task != nullptr; }

#include "platform/audio.h"

#include <Arduino.h>

#include "boards/board_select.h"
#include "platform/memlog.h"

#if defined(BOARD_AUDIO_DAC)
#include <driver/dac_continuous.h>
#elif defined(BOARD_AUDIO_ES8311) || defined(BOARD_AUDIO_PDM)
#include <driver/i2s_std.h>
#include <driver/i2s_pdm.h>
#include "hal/es8311.h"
#endif

namespace {

constexpr int kBuf = 512;               // samples a DMA buffer (23 ms)
constexpr int kBufs = 4;
constexpr int kRamp = 256;              // samples to move between 0 V and the middle (no click)

TaskHandle_t task = nullptr;
volatile bool stop_wanted = false;
volatile bool ending = false;           // the task is on its way out
volatile bool dac_failed = false;       // noted in the memory log by the next audio_play
volatile bool held = false;             // a fight: the task and DAC stay between sounds (audio_hold)
// (v0.86.1: Tom's 4.0" memory log showed sounds needing ~7 KB each time,
// mid-fight with 8 KB free: they failed without a word)
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

// ---- The output: 8-bit samples (128 = the middle) out of the board ----------
// open / close make and free it (only while the task runs), enable / disable
// start and stop the samples, write blocks until the samples are queued.
// kRamps: the output's 0 is 0 V (the DAC, PDM), so a burst ramps from 0 V
// to the middle and back (no click); the codec's 0 is silence already.

#if defined(BOARD_AUDIO_DAC)

constexpr bool kRamps = true;
constexpr int kTail = 1;                // silent buffers before the output stops
dac_continuous_handle_t h = nullptr;

bool out_open()
{
    dac_continuous_config_t cfg = {};
    cfg.chan_mask = DAC_CHANNEL_MASK_CH1;           // GPIO 26
    cfg.desc_num = kBufs;
    cfg.buf_size = kBuf;
    cfg.freq_hz = kAudioHz;
    cfg.offset = 0;
    cfg.clk_src = DAC_DIGI_CLK_SRC_DEFAULT;
    cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
    if (dac_continuous_new_channels(&cfg, &h) == ESP_OK) return true;
    if (h) dac_continuous_del_channels(h);
    h = nullptr;
    return false;
}
bool out_enable() { return dac_continuous_enable(h) == ESP_OK; }
void out_write(const uint8_t* buf, int n)
{
    size_t done = 0;
    dac_continuous_write(h, const_cast<uint8_t*>(buf), n, &done, -1);
}
void out_disable() { dac_continuous_disable(h); }
void out_close()
{
    dac_continuous_del_channels(h);
    h = nullptr;
}

#elif defined(BOARD_AUDIO_ES8311) || defined(BOARD_AUDIO_PDM)

// I2S: the ESP32-S3 board's codec (standard I2S, MCLK = 256 x rate), or the
// ESP32-C5 board's amplifier input through its filter (PDM as a DAC)
constexpr int kTail = kBufs;            // (the queued samples play out before it stops)
#if defined(BOARD_AUDIO_ES8311)
constexpr bool kRamps = false;
bool codec_tried = false;
void codec_setup();
#else
constexpr bool kRamps = true;
#endif
i2s_chan_handle_t h = nullptr;
int16_t* wide = nullptr;                // the samples as 16 bits

bool out_open()
{
    wide = static_cast<int16_t*>(malloc(kBuf * sizeof(int16_t)));
    if (!wide) return false;
    i2s_chan_config_t cc = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    cc.dma_desc_num = kBufs;
    cc.dma_frame_num = kBuf;
    cc.auto_clear = true;                           // (nothing queued: silence)
    if (i2s_new_channel(&cc, &h, nullptr) != ESP_OK) {
        h = nullptr;
        free(wide);
        wide = nullptr;
        return false;
    }
#if defined(BOARD_AUDIO_ES8311)
    i2s_std_config_t sc = {};
    sc.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kAudioHz);
    sc.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    sc.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    sc.gpio_cfg.mclk = static_cast<gpio_num_t>(BOARD_PIN_I2S_MCLK);
    sc.gpio_cfg.bclk = static_cast<gpio_num_t>(BOARD_PIN_I2S_BCLK);
    sc.gpio_cfg.ws = static_cast<gpio_num_t>(BOARD_PIN_I2S_LRCK);
    sc.gpio_cfg.dout = static_cast<gpio_num_t>(BOARD_PIN_I2S_DOUT);
    sc.gpio_cfg.din = I2S_GPIO_UNUSED;
    const bool ok = i2s_channel_init_std_mode(h, &sc) == ESP_OK;
#else
    i2s_pdm_tx_config_t pc = {};
    pc.clk_cfg = I2S_PDM_TX_CLK_DAC_DEFAULT_CONFIG(kAudioHz);
    pc.slot_cfg = I2S_PDM_TX_SLOT_DAC_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    pc.slot_cfg.hp_en = false;                      // (the ramps to and from 0 V must stay)
    pc.gpio_cfg.clk = I2S_GPIO_UNUSED;
    pc.gpio_cfg.dout = static_cast<gpio_num_t>(BOARD_PIN_SPEAKER);
#if SOC_I2S_PDM_MAX_TX_LINES > 1
    pc.gpio_cfg.dout2 = I2S_GPIO_UNUSED;
#endif
    const bool ok = i2s_channel_init_pdm_tx_mode(h, &pc) == ESP_OK;
#endif
    if (ok) return true;
    i2s_del_channel(h);
    h = nullptr;
    free(wide);
    wide = nullptr;
    return false;
}
bool out_enable() { return i2s_channel_enable(h) == ESP_OK; }
void out_write(const uint8_t* buf, int n)
{
    for (int i = 0; i < n; ++i) wide[i] = static_cast<int16_t>((static_cast<int>(buf[i]) - 128) * 256);
    size_t done = 0;
    i2s_channel_write(h, wide, n * sizeof(int16_t), &done, portMAX_DELAY);
}
void out_disable() { i2s_channel_disable(h); }
void out_close()
{
    i2s_del_channel(h);
    h = nullptr;
    free(wide);
    wide = nullptr;
}
#if defined(BOARD_AUDIO_ES8311)
// The codec, once, from the caller's task - the touch's: they share the
// I2C, which isn't safe from two tasks (v0.103.1: it was set up from the
// sound task on the other core, with Serial output on its small stack, and
// the S3 crashed as a game's title played its first sound). Its clocks
// run meanwhile (they come from the I2S).
void codec_setup()
{
    if (codec_tried) return;
    codec_tried = true;
    bool ok = out_open();
    if (ok) {
        ok = out_enable() &&
             es8311::begin(BOARD_I2C_PORT, BOARD_PIN_I2C_SDA, BOARD_PIN_I2C_SCL, kAudioHz);
        i2s_channel_disable(h);
        out_close();
    }
    if (!ok) memlog::note("sound: the ES8311 codec didn't answer - no sound");
}
#endif

#else   // no sound on this board

constexpr bool kRamps = false;
constexpr int kTail = 1;
bool out_open() { return false; }
bool out_enable() { return false; }
void out_write(const uint8_t*, int) {}
void out_disable() {}
void out_close() {}

#endif

void audio_task(void*)
{
    uint8_t* buf = static_cast<uint8_t*>(malloc(kBuf));
    if (!buf || !out_open()) {
        Serial.println("[audio] the sound output didn't start");
        dac_failed = true;
        free(buf);
        held = false;
        task = nullptr;
        vTaskDelete(nullptr);
        return;
    }
    for (;;) {
        // A burst of sound: the output on, from 0 V up to the middle, the amplifier on
        if (!out_enable()) break;
        for (int i = 0; i < kBuf; ++i)
            buf[i] = static_cast<uint8_t>(kRamps && i < kRamp ? i * 128 / kRamp : 128);
        out_write(buf, kBuf);
        amp(true);
        int quiet = 0;
        for (;;) {
            while (!stop_wanted && quiet < 4) {
                const bool more = fill_fn(buf, kBuf, fill_ctx);
                quiet = more ? 0 : quiet + 1;
                out_write(buf, kBuf);
            }
            // A sound started while the last quiet buffer played (audio_play
            // saw this task still going): keep going with it
            if (!held) ending = true;
            if (stop_wanted || !fill_fn(buf, kBuf, fill_ctx)) break;
            ending = false;
            quiet = 0;
            out_write(buf, kBuf);
        }
        // Down to 0 V again, amplifier off, the output off (the queued
        // buffers played out first)
        amp(false);
        for (int i = 0; i < kBuf; ++i)
            buf[i] = static_cast<uint8_t>(!kRamps ? 128 : i < kRamp ? 128 - i * 128 / kRamp : 0);
        out_write(buf, kBuf);
        if (kRamps) memset(buf, 0, kBuf);
        for (int i = 0; i < kTail; ++i) out_write(buf, kBuf);
        out_disable();
        if (stop_wanted || !held) break;
        // Held (a fight): asleep until the next sound - or the hold's end
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (stop_wanted || !held) break;
    }
    out_close();
    free(buf);
    task = nullptr;
    vTaskDelete(nullptr);
}

bool start_task()
{
    ending = false;
    stop_wanted = false;
#if defined(BOARD_AUDIO_ES8311)
    codec_setup();
#endif
#if defined(BOARD_AUDIO_DAC)
    constexpr uint32_t kStack = 3072;
#else
    constexpr uint32_t kStack = 5120;      // (the I2S driver's calls go deeper - and the S3's windowed stack)
#endif
    if (xTaskCreatePinnedToCore(audio_task, "audio", kStack, nullptr, 4, &task, 0) == pdPASS) return true;
    task = nullptr;
    Serial.println("[audio] no memory for the sound task");
    static uint32_t last_note = 0;          // (once a minute at most: sounds come often)
    if (!last_note || millis() - last_note > 60000) {
        last_note = millis() | 1;
        memlog::note("NOT ENOUGH MEMORY for a sound (its task)");
    }
    return false;
}

} // namespace

void audio_play(AudioFill fill, void* ctx)
{
#if defined(CYD_QEMU)
    (void)fill;                     // (the emulator build: QEMU has no DAC DMA - no sound)
    (void)ctx;
    return;
#endif
    if (dac_failed) {
        dac_failed = false;
        memlog::note("NOT ENOUGH MEMORY for a sound (the output didn't start)");
    }
    if (!fill) return;
    if (task && held) {                 // (held: wake it - it picks the sound up)
        fill_fn = fill;
        fill_ctx = ctx;
        xTaskNotifyGive(task);
        return;
    }
    if (task && ending)
        for (int i = 0; i < 100 && task; ++i) delay(2);     // (ending: start again after it)
    if (task) return;
    fill_fn = fill;
    fill_ctx = ctx;
    start_task();
}

void audio_hold(bool on, AudioFill fill, void* ctx)
{
#if defined(CYD_QEMU)
    (void)on;
    (void)fill;
    (void)ctx;
    return;
#endif
    if (!on) {
        held = false;
        if (task) xTaskNotifyGive(task);    // (asleep: it ends; playing: it ends after the sound)
        return;
    }
    if (!fill) return;
    if (task && ending)
        for (int i = 0; i < 100 && task; ++i) delay(2);
    fill_fn = fill;
    fill_ctx = ctx;
    held = true;
    if (!task && !start_task()) held = false;
}

bool audio_held() { return held && task != nullptr; }

void audio_stop()
{
    held = false;
    if (!task) return;
    stop_wanted = true;
    xTaskNotifyGive(task);
    for (int i = 0; i < 200 && task; ++i) delay(5);
}

bool audio_running() { return task != nullptr; }

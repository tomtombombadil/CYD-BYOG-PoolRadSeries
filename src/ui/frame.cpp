#include "frame.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstring>

#include "hal/sdcard.h"

namespace frame {

namespace {

uint8_t*    pixels = nullptr;
pic::Canvas cv{nullptr, pic::kScreenW, pic::kScreenH};
uint16_t    lut[256];                  // palette index -> RGB565, bytes swapped for the panel
Scale       mode = Scale::One;

constexpr int kBandRows = 8;           // output rows converted per push
uint16_t    band[480 * kBandRows];     // 7.5 KB
uint16_t    xmap[480];                 // output column -> canvas column (1.5x)

uint16_t panel_color(const pic::Rgb& c)
{
    const uint16_t v = lgfx::color565(c.r, c.g, c.b);
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

bool at_left = false;

bool scaled() { return mode == Scale::OneAndHalf && ui::large() && !at_left; }

} // namespace

bool begin()
{
    if (pixels) return true;
    pixels = static_cast<uint8_t*>(heap_caps_malloc(pic::kScreenW * pic::kScreenH, MALLOC_CAP_8BIT));
    if (!pixels) {
        Serial.printf("[frame] no memory for the canvas (largest block %u)\n",
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return false;
    }
    cv.px = pixels;
    cv.clear(0);
    for (int i = 0; i < 480; ++i) xmap[i] = static_cast<uint16_t>(i * 2 / 3);
    for (int i = 0; i < 256; ++i) lut[i] = 0;
    set_ega_palette();
    return true;
}

pic::Canvas& canvas() { return cv; }

uint16_t colour(int index)
{
    const uint16_t v = lut[index & 0xFF];
    return static_cast<uint16_t>((v >> 8) | (v << 8));
}

namespace {
bool is_parked = false;
constexpr size_t kCanvasBytes = static_cast<size_t>(pic::kScreenW) * pic::kScreenH;
}

bool park(const char* path)
{
    if (!pixels || is_parked || !sd_begin()) return false;
    fs::File f = sd_fs().open(path, "w");
    if (!f) return false;
    const bool ok = f.write(pixels, kCanvasBytes) == kCanvasBytes;
    f.close();
    if (!ok) return false;
    heap_caps_free(pixels);
    pixels = nullptr;
    cv.px = nullptr;
    is_parked = true;
    return true;
}

bool unpark(const char* path)
{
    if (!is_parked) return true;
    uint8_t* p = static_cast<uint8_t*>(heap_caps_malloc(kCanvasBytes, MALLOC_CAP_8BIT));
    if (!p) {
        Serial.printf("[frame] no memory to bring the canvas back (largest block %u)\n",
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return false;
    }
    bool ok = sd_begin();
    if (ok) {
        fs::File f = sd_fs().open(path, "r");
        ok = f && f.read(p, kCanvasBytes) == kCanvasBytes;
        if (f) f.close();
    }
    if (!ok) memset(p, 0, kCanvasBytes);       // a blank screen rather than none; the game redraws
    pixels = p;
    cv.px = p;
    is_parked = false;
    return true;
}

bool parked() { return is_parked; }

void set_palette(int index, const pic::Rgb& c)
{
    // Index 16 is the EGA pictures' "transparent"; a 256-colour palette
    // uses it as a real colour
    if (index >= 0 && index < 256) lut[index] = panel_color(c);
}

void set_ega_palette()
{
    for (int i = 0; i < 16; ++i) set_palette(i, pic::kEga[i]);
    lut[pic::kTransparent] = 0;
}

void set_scale(Scale s) { mode = s; }
void set_left(bool left) { at_left = left; }
Scale scale() { return mode; }

ui::Rect area()
{
    if (scaled()) return {0, 0, 480, 300};
    const int x = at_left ? 0 : (ui::width() - pic::kScreenW) / 2;
    return {x, 0, pic::kScreenW, pic::kScreenH};
}

void present() { present_rows(0, pic::kScreenH); }

void present_rows(int y0, int y1)
{
    if (!pixels) return;
    if (y0 < 0) y0 = 0;
    if (y1 > pic::kScreenH) y1 = pic::kScreenH;
    if (y0 >= y1) return;
    LGFX& g = ui::gfx();
    const ui::Rect a = area();
    g.startWrite();
    if (!scaled()) {
        for (int y = y0; y < y1; y += kBandRows) {
            const int rows = (y1 - y < kBandRows) ? y1 - y : kBandRows;
            for (int r = 0; r < rows; ++r) {
                const uint8_t* src = pixels + (y + r) * pic::kScreenW;
                uint16_t* dst = band + r * pic::kScreenW;
                for (int x = 0; x < pic::kScreenW; ++x) dst[x] = lut[src[x]];
            }
            g.pushImage(a.x, a.y + y, pic::kScreenW, rows, reinterpret_cast<const lgfx::swap565_t*>(band));
        }
    } else {
        // Canvas rows y0..y1 cover output rows ceil(y0*3/2) .. ceil(y1*3/2)
        const int oy0 = (y0 * 3 + 1) / 2, oy1 = (y1 * 3 + 1) / 2;
        for (int oy = oy0; oy < oy1; oy += kBandRows) {
            const int rows = (oy1 - oy < kBandRows) ? oy1 - oy : kBandRows;
            for (int r = 0; r < rows; ++r) {
                const uint8_t* src = pixels + ((oy + r) * 2 / 3) * pic::kScreenW;
                uint16_t* dst = band + r * 480;
                for (int x = 0; x < 480; ++x) dst[x] = lut[src[xmap[x]]];
            }
            g.pushImage(a.x, a.y + oy, 480, rows, reinterpret_cast<const lgfx::swap565_t*>(band));
        }
    }
    g.endWrite();
}

bool to_canvas(int px, int py, int& cx, int& cy)
{
    const ui::Rect a = area();
    if (!a.contains(px, py)) return false;
    if (scaled()) {
        cx = (px - a.x) * 2 / 3;
        cy = (py - a.y) * 2 / 3;
    } else {
        cx = px - a.x;
        cy = py - a.y;
    }
    return true;
}

} // namespace frame

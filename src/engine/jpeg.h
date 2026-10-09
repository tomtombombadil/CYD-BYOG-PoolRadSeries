// Baseline JPEG decoding for the journal scans (the GOG journal PDFs keep
// each page as one JPEG). A wrapper round ChaN's TJpgDec (third_party/),
// streaming from a ByteSource, with only the wanted MCUs decoded fully.
//
// Plain C++, host-tested in tools/host_tests/test_dax.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace jpeg {

constexpr size_t kPoolSize = 12 * 1024;     // the decoder's workspace

struct Info {
    int width = 0, height = 0;
    int mcu_w = 0, mcu_h = 0;               // 8 or 16
};

// want(x, y, w, h, ctx): 1 = decode that MCU, 0 = skip it, -1 = stop
using WantFn = int (*)(int x, int y, int w, int h, void* ctx);
// block(x, y, w, h, rgb, ctx): a decoded block, RGB888, w x h; false = stop
using BlockFn = bool (*)(int x, int y, int w, int h, const uint8_t* rgb, void* ctx);

// The JPEG at [at, at + len) in src. pool: kPoolSize bytes. want may be
// nullptr (everything); it gets full-size coordinates. scale 0-3 = 1/1,
// 1/2, 1/4, 1/8: block() then gets the smaller picture's coordinates.
// False on bad data (a stop asked for is not one).
bool decode(dax::ByteSource& src, uint32_t at, uint32_t len, void* pool, WantFn want, BlockFn block, void* ctx,
            Info* info = nullptr, int scale = 0);

// Only the size and MCU size.
bool probe(dax::ByteSource& src, uint32_t at, uint32_t len, void* pool, Info& info);

} // namespace jpeg

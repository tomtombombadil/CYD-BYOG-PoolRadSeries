// A small streaming PNG reader for icons (GOG's 256 x 256 game icons are
// PNGs inside their .ico / .dll).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Reads 8-bit truecolour (colour type 2), truecolour + alpha (6), palette
// (3, with tRNS transparency) and grey (0) / grey + alpha (4), not
// interlaced. Rows come out one at a time as RGBA through a callback;
// memory = the caller's 32 KB inflate window + two rows.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace png {

constexpr int kMaxSize = 512;

struct Info {
    int w = 0, h = 0;
    int bit_depth = 0, colour = 0, interlace = 0;
};

// True if the bytes at `at` are a PNG this reader handles; fills info.
bool probe(dax::ByteSource& src, uint32_t at, uint32_t size, Info& info);

// Called with each row, top to bottom: rgba = w * 4 bytes. Return false to
// stop.
using RowFn = bool (*)(int y, const uint8_t* rgba, void* ctx);

// Decodes the PNG at `at` (size bytes). window: inflate::kWindow bytes.
bool decode(dax::ByteSource& src, uint32_t at, uint32_t size, uint8_t* window, RowFn row, void* ctx);

} // namespace png

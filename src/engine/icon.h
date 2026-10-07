// A game's icon for the library, read from the player's own files.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// The DOS games have no icons of their own (START.EXE is a DOS program).
// GOG's installs carry the game's icon in goggame-<id>.ico and/or
// goggame-<id>.dll in the install folder; the player copies that file into
// the game's folder on the card and the library shows it.
//
// Formats (Windows standards):
//   .ico: u16 0, u16 1, u16 count; per image 16 bytes: u8 width, u8 height
//         (0 = 256), u8 colours, u8 0, u16 planes, u16 bits, u32 bytes,
//         u32 file offset.
//   .dll / .exe (PE): the resource section's RT_GROUP_ICON (14) lists the
//         images like an .ico (14-byte entries ending in a u16 resource id
//         instead of an offset); each image is an RT_ICON (3) resource.
//   An image is a PNG (GOG's 256 x 256 one) or a DIB: BITMAPINFOHEADER (40
//   bytes; height doubled), a palette for 1/4/8 bits, the colour rows
//   bottom-up (each padded to 4 bytes), then a 1-bit transparency mask
//   (1 = see-through). 32-bit DIBs carry alpha.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace icon {

constexpr int kMaxDib = 256;     // largest DIB image read
constexpr int kMaxOut = 256;     // largest size drawn

struct Found {
    uint32_t offset = 0;         // the image in the file
    uint32_t size = 0;
    int      w = 0, h = 0, bits = 0;
    bool     png = false;
};

// The biggest image in an .ico or a PE (.dll / .exe) file; at equal sizes
// the one with more colours. False if the file has no image this reads.
bool find_in_ico(dax::ByteSource& src, Found& out);
bool find_in_pe(dax::ByteSource& src, Found& out);
// Either: an .ico by its "00 00 01 00" start, else a PE.
bool find(dax::ByteSource& src, Found& out);

// Called with each output row, top to bottom: rgba = out_w * 4 bytes
// (R G B A, A = 0 see-through ... 255 solid).
using RowFn = void (*)(int y, const uint8_t* rgba, int w, void* ctx);

// Draws the image at out_w x out_h (out <= kMaxOut): averaged down when
// smaller than the image, repeated pixels when bigger. window: a PNG needs
// inflate::kWindow bytes of scratch (unused for DIBs - may be null then).
bool render(dax::ByteSource& src, const Found& f, int out_w, int out_h, uint8_t* window, RowFn row, void* ctx);

} // namespace icon

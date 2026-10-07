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
//   An image is a PNG (not read here - the big 256 px ones) or a DIB:
//   BITMAPINFOHEADER (40 bytes; height doubled), a palette for 1/4/8 bits,
//   the colour rows bottom-up (each padded to 4 bytes), then a 1-bit
//   transparency mask (1 = see-through). 32-bit images carry alpha.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace icon {

constexpr int kMaxSize = 64;    // largest image decode() handles

struct Found {
    uint32_t offset = 0;        // the image (DIB) in the file
    uint32_t size = 0;
    int      w = 0, h = 0, bits = 0;
};

// Picks the image to show at about `want` pixels: that size if there is
// one, else the largest smaller one, else the smallest; more colours first.
// Only DIB images up to kMaxSize. False if the file has none.
bool find_in_ico(dax::ByteSource& src, int want, Found& out);
bool find_in_pe(dax::ByteSource& src, int want, Found& out);
// Either: an .ico by its "00 00 01 00" start, else a PE.
bool find(dax::ByteSource& src, int want, Found& out);

// Decodes the image into rgba (w * h * 4 bytes, rows top-down, R G B A,
// A = 0 see-through ... 255 solid).
bool decode(dax::ByteSource& src, const Found& f, uint8_t* rgba);

} // namespace icon

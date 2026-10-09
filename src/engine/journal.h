// The Adventurer's Journal: pictures of its entries, made by the journal
// converter (web/journal/, runs in the player's browser) from the player's
// own GOG journal PDF and copied next to the game's files as JOURNAL.BIN.
// The games say "record it in journal entry 31" or "you overhear tavern
// tale 12"; the engine shows that entry (SPEC section 7).
//
// JOURNAL.BIN (little-endian):
//   "GBJ1", u8 version (1), u8 widths (1-4), u16 entries
//   u16 width[widths]                     each entry is stored at each width
//   entries x { u8 kind ('J' journal entry, 'T' tavern tale), u8 0,
//               u16 number, u32 offset[widths] }    offset of its picture
//   picture: u16 w, u16 h, u16 palette[16] (RGB565), u32 data bytes,
//            then the rows: a byte per run, (length - 1) << 4 | colour,
//            runs never cross a row.
//
// Plain C++, host-tested in tools/host_tests/test_dax.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace journal {

constexpr int kMaxWidths = 4;
constexpr int kMaxWidth = 640;

struct Info {
    int      widths = 0;
    uint16_t width[kMaxWidths] = {};
    int      entries = 0;
};

struct Picture {
    int      w = 0, h = 0;
    uint16_t palette[16] = {};
    uint32_t data_at = 0, data_len = 0;
};

bool read_info(dax::ByteSource& src, Info& out);

// The width to use on a screen `room` pixels wide: the widest that fits,
// else the narrowest. -1 if the file has none.
int pick_width(const Info& info, int room);

// Finds entry (kind, number) at width index wi. False if it isn't there.
bool find(dax::ByteSource& src, const Info& info, char kind, int number, int wi, Picture& out);

// Decodes rows first .. first + count - 1 (colour indexes, w of them) and
// hands each to fn. False on bad data.
using RowFn = void (*)(int y, const uint8_t* row, int w, void* ctx);
bool rows(dax::ByteSource& src, const Picture& p, int first, int count, RowFn fn, void* ctx);

// Looks through printed game text for "JOURNAL ENTRY 31" (also "JOURNAL
// AS ENTRY 59", "JOURNAL\nENTRY 44", "ENTRY #12") or "TAVERN TALE 12".
// Returns the kind ('J' / 'T') of the last complete mention and sets
// *number, else 0.
char find_mention(const char* text, int* number);

} // namespace journal

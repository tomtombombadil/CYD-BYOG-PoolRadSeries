// The Adventurer's Journal. The games say "record it in journal entry 31"
// or "you overhear tavern tale 12"; the printed journal (GOG: a PDF of
// scanned pages, in the game's folder) has the text. The board makes the
// entries' pictures itself from the player's own PDF during the card scan
// (Tom, 2026-10-09), using a table of where each entry is on the pages
// (journal_tables.cpp: page numbers and positions only - nothing from the
// journal is in the firmware), and shows entry N when the game mentions it.
//
// JOURNAL.DAT (in /GOLDBOX/_CYD/<game folder>/, little-endian):
//   "GBJ2"                          written last: a half-made file has none
//   u8 version (1), u8 0, u16 entries, u16 pieces, u16 0
//   char pdf_id[32]                 the PDF's /ID (which PDF it came from)
//   u8 palette[256][3]              RGB
//   entries x { u8 kind ('J' journal entry, 'T' tavern tale), u8 0,
//               u16 number, u16 first piece, u16 pieces }
//   pieces x  { u16 w, u16 h, u32 offset }  a rectangle of a page, at the
//             scan's resolution, w x h bytes (palette indexes), top to bottom
// An entry is its pieces stacked (narrower ones centred).
//
// Plain C++, host-tested in tools/host_tests/test_dax.cpp.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace journal {

// ---- the tables (journal_tables.cpp, made by tools/journal/make_table.py) --

struct Piece {
    uint8_t  page;                 // PDF page, from 1
    uint16_t x, y, w, h;           // in pixels of the page's picture
};

struct EntryDef {
    char     kind;                 // 'J' / 'T'
    uint8_t  number;
    uint16_t first;                // into the pieces
    uint8_t  count;
};

struct Table {
    const char*     name;          // for the scan log
    uint32_t        pdf_size;      // the PDF file
    const char*     pdf_id;        // its trailer /ID, lower-case hex
    const EntryDef* entries;
    int             n_entries;
    const Piece*    pieces;
    int             n_pieces;
};

// The table for a PDF of this size and /ID, or nullptr.
const Table* find_table(uint32_t pdf_size, const char* pdf_id);
extern const Table* const kTables[];
extern const int kTableCount;

// ---- making JOURNAL.DAT ---------------------------------------------------

class Output {
public:
    virtual ~Output() = default;
    virtual bool write_at(uint32_t pos, const uint8_t* data, size_t n) = 0;
};

// Pages done so far of all the pages it needs
using Progress = void (*)(int done, int total, void* ctx);

// Reads the PDF's page pictures and writes JOURNAL.DAT. False on a PDF it
// can't read (or no memory); the file is then left without its "GBJ2".
bool make(dax::ByteSource& pdf, const Table& t, Output& out, Progress progress, void* ctx);

// The palette (index -> RGB) and the colour a scanned pixel is stored as:
// paper and the faint print from the other side of the page become white
void palette_rgb(int index, uint8_t* r, uint8_t* g, uint8_t* b);
uint8_t index_of(int r, int g, int b);

// ---- reading it -------------------------------------------------------------

struct Info {
    int      entries = 0, pieces = 0;
    char     pdf_id[33] = {};
    uint8_t  palette[256][3] = {};
};

struct PieceInfo {
    int      w = 0, h = 0;
    uint32_t offset = 0;
};

bool read_info(dax::ByteSource& src, Info& out);
// The entry's pieces: first index and count. False if it isn't there.
bool find(dax::ByteSource& src, const Info& info, char kind, int number, int* first, int* count);
bool piece(dax::ByteSource& src, const Info& info, int i, PieceInfo& out);

// ---- the game's text ----------------------------------------------------------

// Looks through printed game text for "JOURNAL ENTRY 31" (also "JOURNAL
// AS ENTRY 59", "JOURNAL\nENTRY 44", "ENTRY #12") or "TAVERN TALE 12".
// Returns the kind ('J' / 'T') of the last complete mention and sets
// *number, else 0.
char find_mention(const char* text, int* number);

} // namespace journal

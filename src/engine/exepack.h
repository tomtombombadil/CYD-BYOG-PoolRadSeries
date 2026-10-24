// Reading the player's own game program (START.EXE / GAME.EXE), which
// Microsoft EXEPACK has compressed.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Some of what the engine needs is not in the DAX files but in the program
// itself - e.g. which 8x8 tile goes where in the screen frame. The firmware
// never carries a copy of it (bring your own game): it reads those few
// bytes from the program on the player's SD card, unpacking just the bytes
// it asks for.
//
// EXEPACK (worked out from the GOG START.EXE files, 2026-10-07): the DOS
// load module starts with the packed program; at CS:0 (CS from the MZ
// header) sits the EXEPACK header, then the unpacker stub ("Packed file is
// corrupt" message). Header, u16 each:
//   real IP, real CS, mem start, exepack size, real SP, real SS,
//   dest len (the unpacked program, in 16-byte paragraphs),
//   [skip len - only in the 18-byte variant every Gold Box game uses],
//   "RB"
// Unpacking runs BACKWARDS from the end of the packed data (CS * 16 minus
// (skip len - 1) paragraphs), after dropping trailing 0xFF padding bytes:
//   command byte, then the length's high byte, then its low byte
//   0xB0: fill   - the next byte back is a value, written `length` times
//   0xB2: copy   - the next `length` bytes back are copied
//   bit 0 set:  the last command
// Output also runs backwards from dest len * 16. Whatever lies below the
// last output byte is already unpacked and stays where it is. The unpacker
// never overwrites packed bytes it has yet to read, so read() takes the
// packed bytes straight from the file and needs no buffer.
//
// The unpacked copy (Tom, 2026-10-24: games were slow to start - every
// read() walks the packed data from its end, and a start makes dozens):
// write_copy() unpacks the program once into a file of the engine's own
// (the game's _CYD/<folder>/PROGRAM.BIN, made by the card scan or at the
// first Play); parse() knows that file too, and read() then serves from
// it with one plain read. The copy:
//   "CYDXPK1\0", u32 the program file's size, u32 its checksum (FNV-1a
//   of the whole file), u32 image size, u16 ip, cs, sp, ss, u32 0,
//   then the unpacked image TOP BYTE FIRST (byte k of the program at
//   copy offset 32 + image size - 1 - k) - the unpacker makes it from the
//   end down, so the file is written straight through with a small buffer.
//
// A program that isn't packed (The Dark Queen of Krynn's DQK.EXE: a plain
// MZ program with .RTLink/Plus overlays and its data segment in the file
// after them - queen_facts.md 7) is read as it is: Info::plain, its
// "image" the whole file (an offset is a file offset), no copy.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace exepack {

enum class Status : uint8_t { Ok, NotExe, NotPacked, BadData, ReadError };
const char* status_text(Status s);

struct Info {
    uint32_t load_start = 0;     // file offset of the DOS load module
    uint32_t packed_end = 0;     // end of the packed data, from load_start
    uint32_t image_size = 0;     // unpacked program bytes (dest len * 16)
    uint16_t ip = 0, cs = 0, sp = 0, ss = 0;   // the real entry point / stack
    uint32_t file_size = 0;      // the program file's size (also when read from its copy)
    uint32_t copy_at = 0;        // a copy: where its image starts (0: the packed program)
    bool     plain = false;      // not packed: the image is the file itself
};

constexpr uint32_t kCopyHeader = 32;

// Reads the MZ and EXEPACK headers. An MZ program without EXEPACK's
// header is Ok as plain (image = the file).
Status parse(dax::ByteSource& exe, Info& out);

// Copies bytes [pos, pos + n) of the unpacked program into buf. Walks the
// packed data once from its end, reading through a small window of its own.
// From a copy (info.copy_at): one read of the copy's bytes.
Status read(dax::ByteSource& exe, const Info& info, uint32_t pos, uint8_t* buf, size_t n);

// Where write_copy() puts the copy's bytes, in order
class Sink {
public:
    virtual ~Sink() = default;
    virtual bool put(const uint8_t* p, size_t n) = 0;
};

// FNV-1a of the whole file (the copy's check that it's of this program)
uint32_t checksum(dax::ByteSource& exe);

// The copy of the packed program exe (parsed into info, its checksum sum):
// one walk through the packed data, the bytes out through a 256-byte buffer.
// A plain program needs none (NotPacked).
Status write_copy(dax::ByteSource& exe, const Info& info, uint32_t sum, Sink& out);

// The copy is whole and of a program file of this size and checksum
bool copy_matches(dax::ByteSource& copy, uint32_t file_size, uint32_t sum);

} // namespace exepack

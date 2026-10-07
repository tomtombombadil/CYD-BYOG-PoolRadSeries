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
};

// Reads the MZ and EXEPACK headers.
Status parse(dax::ByteSource& exe, Info& out);

// Copies bytes [pos, pos + n) of the unpacked program into buf. Walks the
// packed data once from its end, reading through a small window of its own.
Status read(dax::ByteSource& exe, const Info& info, uint32_t pos, uint8_t* buf, size_t n);

} // namespace exepack

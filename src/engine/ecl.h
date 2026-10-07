// ECL: the games' event scripts (ECLn.DAX), as far as the engine reads them
// so far - decoding instructions and following the script's jumps, to find
// which 3D map and wall sets an area's script loads.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Format (learned from coab, Curse of the Azure Bonds; own code):
//   An ECL block's first 2 bytes are skipped; the rest is loaded at VM
//   address 0x8000. It starts with 5 entry points (each a filler byte and
//   an operand holding a VM address): run on every step, search, before
//   camping, camping interrupted, first run.
//   An instruction is an opcode byte and its operands. An operand is a
//   code byte, a low byte, and:
//     code 1, 2, 3, 0x81: a high byte too (code 2 = the 16-bit value itself,
//       1 / 3 = a memory address, 0x81 = a string in memory)
//     code 0x80: `low` bytes of packed string follow
//     anything else (0 = the low byte itself): nothing more
//   Most opcodes have a fixed operand count; menus and ON GOTO / GOSUB read
//   2 or 3, then as many more as one of those says.
// The opcode numbers and operand counts differ between the games, so they
// come from the game's profile (an OpSet).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ecl {

constexpr int kMaxOps = 24;

// Operand counts: >= 0 fixed; kCount3 = 3, then as many as the 3rd says;
// kCount2 = 2, then as many as the 2nd says; kUnknown = not an opcode.
constexpr int8_t kCount3 = -1, kCount2 = -2, kUnknown = -128;

struct OpSet {
    int8_t  sizes[256];
    uint8_t exit, go_to, go_sub, ret, new_ecl, on_goto, on_gosub;
    uint8_t if_first, if_last;      // IF opcodes: the next instruction may be skipped
    uint8_t load_files, load_pieces;
};

struct Operand {
    uint8_t code = 0, low = 0, high = 0;
    bool immediate() const { return code == 0; }
    uint16_t word() const { return static_cast<uint16_t>(low | high << 8); }
};

struct Insn {
    uint8_t  op = 0;
    uint32_t at = 0, next = 0;       // offsets in the block (after its 2-byte header)
    int      count = 0;              // operands (only the first kMaxOps kept)
    Operand  ops[kMaxOps];
};

// Decodes the instruction at offset `at` of a block's code (the block
// without its first 2 bytes). False if it isn't one or runs past the end.
bool decode(const uint8_t* code, uint32_t len, uint32_t at, const OpSet& set, Insn& out);

// The 5 entry points (offsets in the code; 0xFFFFFFFF if not in the block).
bool entries(const uint8_t* code, uint32_t len, const OpSet& set, uint32_t out[5]);

constexpr uint8_t kNone = 0xFF;

// What an area script loads first: its 3D map (GEO block) and wall sets
// 1-3 (WALLDEF blocks), from the first LOAD FILES / LOAD PIECES reached
// from its entry points with plain-number operands. kNone where nothing is
// loaded (0xFF or 0x7F in the script).
struct MapLoad {
    uint8_t geo = kNone;
    uint8_t walls[3] = {kNone, kNone, kNone};
    bool has_geo() const { return geo != kNone; }
    bool has_walls() const { return walls[0] != kNone || walls[1] != kNone || walls[2] != kNone; }
};

// block = the whole ECL block as stored (with its 2-byte header).
MapLoad find_map_load(const uint8_t* block, uint32_t len, const OpSet& set);

} // namespace ecl

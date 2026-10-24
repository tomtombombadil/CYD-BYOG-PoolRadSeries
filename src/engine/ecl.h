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

// Words the script machine says itself (GAME.OVR strings; the profile has
// their offsets, the front end reads them): the encounter menu's notes,
// DAMAGE's lines ("  NAME is hit FOR 7 points of Damage.", "  NAME dies. ")
enum ScriptWord {
    kWBothWait, kWMonstersFlee, kWDies, kWIsHitFor, kWPointsOfDamage, kWPartyKilled,
    // Darkness (darkness_facts.md S10): PRESS KEY's prompt, JOURNAL's
    // sentence (two parts), WHAT MENU's question, YES NO's menu
    kWDkPressKey, kWDkJournal, kWDkJournalEnd, kWDkWhatDo, kWDkYesNo,
    // Secret's experience award (op 0x41): "Congratulations the party gains
    // experience!", "Congratulations " + NAME + " gains experience!"
    kWPartyGainsXp, kWCongrats, kWGainsXp,
    // Gateway's (its interpreter's 0x41, GAME.OVR 0x41F2): the amount told,
    // then " experience points!"
    kWXpPoints,
    kScriptWords
};


constexpr int kMaxOps = 24;

// Operand counts: >= 0 fixed; kCount3 = 3, then as many as the 3rd says;
// kCount2 = 2, then as many as the 2nd says; kCount4 = 4, then as many as
// the 4th says (Darkness's TREASURE); kUnknown = not an opcode.
constexpr int8_t kCount3 = -1, kCount2 = -2, kCount4 = -3, kUnknown = -128;

struct OpSet {
    int8_t  sizes[256];
    uint8_t exit, go_to, go_sub, ret, new_ecl, on_goto, on_gosub;
    uint8_t if_first, if_last;      // IF opcodes: the next instruction may be skipped
    uint8_t load_files, load_pieces;
    // Pool of Radiance's memory map (poolrad_facts.md 4.2): its scripts
    // are loaded at 0x9900 and keep their words elsewhere; decode() moves
    // every address to where Curse keeps the same thing, so the script
    // machine works with one map (vm_address)
    bool pool_map;
    // CALL's machine-code addresses that differ: the 3D view redrawn, a
    // sound (Pool: 0x2C90, 0xBA03)
    uint16_t redraw_call = 0x2E10, sound_call = 0xB200;
    // PROGRAM: 8 ends the script after the ending (Curse) or the script
    // goes on after it (Pool); 3 kills the party (Curse; Pool's program
    // does nothing for it - its handler knows 0, 8 and 9 only)
    bool program_ends = true, program_kills = true;
    // Secret: its areas 1-6 share the numbered files two by two (ECL1 for
    // areas 1 and 2 ...: the file is (area + 1) / 2) and the blocks are
    // numbered across a family's files (NEW ECL 33 in area 2: ECL1's) - a
    // block is looked for in the family's other files when its area's
    // hasn't it (Curse: the file is the area's own)
    bool area_by_block = false;
    // Secret: the scripts' view of a character (party::script_value_secret)
    bool secret_fields = false;
    // Secret: COMBAT with no monsters and this area word 1 (cleared) opens
    // the vault (GAME.OVR 0x269D; its scripts' Deposit / Withdraw); 0: none
    uint16_t vault_word = 0;
    // Secret: op 0x41 is an experience award (GAME.OVR ovr4 0x37F4 = file
    // 0x43FF): its 2nd operand's experience to everyone (1st operand not 0)
    // or the selected member (0), each its share by the classes held
    bool xp_award = false;
    // Darkness: one more opcode that ends an instruction stream (its END,
    // 0x23: the text window cleared, the view back, EXIT; 0: none - EXIT
    // itself)
    uint8_t end_op = 0;
    // Darkness: LOAD FILES' second operand is the "look" - WALLDEF1 /
    // 8X8D1 / BACK1 block of that number at once (darkness_facts.md 5 S2);
    // find_map_load gives it as walls[0]
    bool files_look = false;
    // Darkness (darkness_facts.md S3): the scripts' memory is bytes - a
    // 1 KB game block at 0x0001-0x0400 (GameState::area1's first 1,024),
    // the selected character's own 510-byte record at 0x0401-0x05FE, the
    // script at 0x8000; an operand's code says the size (1 a byte, 3 a
    // word). The engine's own calls with Curse's addresses (0x4BF2 the
    // block, 0x4BC6 the clock, 0xC04B x ...) are turned into its places.
    bool byte_map = false;
    // Death Knights of Krynn (deathknights_facts.md 3; its operand loader,
    // GAME.OVR overlay 0x41): byte_map's sizes, but at Curse's addresses -
    // 0x4B00-0x4EFF a 1 KB block, 0x7A00-0x7BFF the 512-byte table,
    // 0x7C00-0x7FFF a 1 KB block whose character codes are the selected
    // character's fields (party::script_value_dkk), 0x8000 the script;
    // C04B-C04F and 033D as Curse's. A byte each address; a word operand
    // (code 3) the byte there and the next
    bool curse_bytes = false;
    // Champions (its clock routine, GAME.OVR 0x2ACB7 / 0x2A87C): a day
    // passing (the hours carrying) steps the moons - each its counter
    // (0x4CFC white, 0x4CFD red, 0x4CFE black) up, or, at 8 / 1 / 6, back
    // to 0 with its phase (0x4CF9-0x4CFB) on, 3 back to 0
    bool moons = false;
    // Gateway (gateway_facts.md 10; its CALL handler, GAME.OVR 0xAB8 +
    // 0x3150): CALL 0x809 waits for a key ("Press Enter to continue."),
    // CALL 0x80C asks Yes / No - 0 / 1 into 0x7F79 (0: none of these)
    uint16_t press_call = 0, yes_no_call = 0;
    // Gateway (gateway_facts.md 14): LOAD FILES loads the map outdoors too
    // (0x4BE6 = 0: its wilderness is drawn from the region's map)
    bool files_outdoors = false;
    // Gateway (GAME.OVR seg 0x53 entry 0x84, seg 0x123 entry 0x4D): CALL
    // 0xC01E's step and CALL 0xC01A take the square's flags (C04F) and the
    // wall ahead (C04E) again at once - its wilderness scripts read the
    // ground of the square ahead that way
    bool step_square = false;
    // Gateway (seg 0x53 +0xF77 / +0x10FC): this address is the facing as
    // C04D is (0-3) - its scripts keep the facing there leaving a city and
    // choose the gate by it coming back (0: none)
    uint16_t facing_alias = 0;
    // Gateway (its CALL handler, interpreter overlay +0x3320): the sound call
    // plays the effect 0x03DE holds by the game's own number (0x80 up: its
    // music - not played); Curse's: a step, or the fireball's roar for 10
    bool sound_word = false;
    // Treasures of the Savage Frontier (treasures_facts.md 3): its script
    // buffer's size (0: kCodeSize), SHOP's last list (Darkness 11) and its
    // three new opcodes - 0x42 LOAD 3D (map, the wall slots' blocks), 0x43
    // FIND ITEM, 0x44 DESTROY ITEM (type, three name words)
    uint32_t code_size = 0;
    // The Dark Queen of Krynn: its scripts are stored without the DAX
    // blocks' 2-byte prefix (ECL.GLB's entries: the code from the first byte)
    bool     no_prefix = false;
    uint8_t  shops = 11;
    bool     tr_ops = false;
    // Gateway (its interpreter, GAME.OVR overlay 0xAB8 +0x29BD / +0x361F):
    // 0x32 FIND ITEM and 0x40 DESTROY ITEMS take the item type (0 any) and
    // three name words (the 2nd 0: any words) as Treasures' 0x43 / 0x44 -
    // DESTROY takes the first such item only; its 0x41 award tells the
    // amount (kWXpPoints)
    bool     item_words = false;
    bool     xp_number = false;
    // Gateway: CALL 0x80F trades the selected member's coins up
    // (rules::trade_up - before its scripts take a price in platinum)
    uint16_t change_call = 0;
};

// A script's memory address as the engine's machine (Curse's map) knows
// it: unchanged for Curse; Pool's area words 0x4900-0x4CFF -> 0x4B00-,
// game / character 0x6B00-0x6EFF -> 0x7C00-, the table 0x9700-0x98FF ->
// 0x7A00-, the code 0x9900-0xB6FF -> 0x8000-; others unchanged
uint16_t vm_address(const OpSet& set, uint16_t a);

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
// Addresses (operands of code 1, 3 and 0x81, and the jumps' targets) come
// out as vm_address gives them.
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

// block = the whole ECL block as stored (with its 2-byte header; none with
// OpSet::no_prefix).
MapLoad find_map_load(const uint8_t* block, uint32_t len, const OpSet& set);

} // namespace ecl

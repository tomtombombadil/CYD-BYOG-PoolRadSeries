#include "profile.h"

namespace profile {

namespace {

// Curse of the Azure Bonds, GOG release: START.EXE (EXEPACK, 18-byte
// header). Data segment at 0x48D0 of the unpacked program; table addresses
// as named in the coab reimplementation's notes (facts only, no code).
// Party menu: its 12 entries at 0xB133 of the unpacked program, each a
// string[40] and an "on" byte (Create New Character, Add Character to
// Party, Exit to DOS start on; the others follow the party); its prompt
// and the party list's headings in GAME.OVR's code segments.
// View Character: the class (27-byte slots, 18 with "unknown"), race,
// alignment, sex, coin and health names at 0xB898-0xBC37 of the unpacked
// program; the screen's words in GAME.OVR (as its code prints them).
// Items: 255 name words in 21-byte slots at 0xBC37 (word 1 first); the
// ITEMS file; arrows 73, quarrels 28, darts 9, flask of oil 86; elves'
// bow / sword bonus types 41-44, 37, 36 (coab's lists); the shop's words.
// Character rules: the program's data segment starts at image 0xABE0
// (coab's "seg600" addresses); its rule tables (THAC0 0x3E3A, thief
// skills 0x3EC0 / 0x3F20 / 0x3F33, race / class tables 0x3F88-0x41DA,
// per-class experience and spell slots 0x429B (99 bytes a class), saving
// throws 0x45BE, spells 0x37DC) - all checked against the GOG sample party
// (THAC0, saves, spell slots match exactly). Effect and spell numbers as
// coab names them.
// Title: picture 1 for 5 s; picture 2 with 3 on it at row 11, column 6 for
// 10 s; picture 4 at row 11 for 10 s; the credits for 10 s.
constexpr TitleStep kCurseTitle[] = {
    {1, 0, 0, true, 5000},
    {2, 0, 0, true, 0},
    {3, 11, 6, false, 10000},
    {4, 11, 0, false, 10000},
    {0, 0, 0, true, 10000},
};

// Curse's ECL opcodes (operand counts as coab lists them)
constexpr int8_t K3 = ecl::kCount3, K2 = ecl::kCount2;
const ecl::OpSet kCurseEcl = [] {
    ecl::OpSet s{};
    for (auto& v : s.sizes) v = ecl::kUnknown;
    const int8_t sizes[0x41] = {
        0, 1, 1, 2, 3, 3, 3, 3, 2, 2, 1, 3, 3,
        0, 1, 2, 2, 1, 1, 0, 4, K3, 0, 0, 0, 0,
        0, 0, 0, 1, 6, 2, 1, 3, 2, 4, 0, K2, K2,
        8, 3, 14, 3, K2, 6, 1, 5, 3, 3, 0, 1, 0,
        1, 3, 1, 3, 1, 1, 0, 3, 1, 0, 0, 1, 1,
    };
    for (int i = 0; i < 0x41; ++i) s.sizes[i] = sizes[i];
    s.sizes[0x34] = 2;   // ECL CLOCK and ADD NPC read 2 operands (coab's table
    s.sizes[0x36] = 2;   // says 1; with 2, more of Curse's scripts decode)
    s.exit = 0x00; s.go_to = 0x01; s.go_sub = 0x02; s.ret = 0x13; s.new_ecl = 0x20;
    s.on_goto = 0x25; s.on_gosub = 0x26; s.if_first = 0x16; s.if_last = 0x1B;
    s.load_files = 0x21; s.load_pieces = 0x37;
    return s;
}();

const Profile kProfiles[] = {
    {games::Game::CurseOfTheAzureBonds, "GOG", "START.EXE", 57789, 62432, 0x48D0,
     {0x6E60, 0x6E88, 0x6EB0, 0x6EF2, 0x6F1B, 0x6F0A, 0x6ED6, 0x6EE3, 0x6F31, 0x6F3E, 0x6F4D, 0x6F64, 0x6F7B},
     "8X8D1.DAX", 202,
     20830,
     "TITLE.DAX", kCurseTitle, sizeof kCurseTitle / sizeof kCurseTitle[0],
     "GAME.OVR", 272137, 706, 218, {3, 8},
     2, 6, 203, "SKY.DAX", 252, &kCurseEcl,
     0x6D9A, 2, 1,
     {0x79, 0x4CA1, 0x6D5A, 0x6D7A, 32, 0x50},
     {0xB133, 12, 42, 0x20111, 0x1EE80, 0x37E31, 0x37E36, "CURSE.CFG", 0x1F5AB, 0x1F5BD, 0x1F673, 0xB00A,
      0x1B382, 0x1B34E, 0x23595, 0x235A6, 0x235BF, 0x235D1, 0x235D6, 0x235D9, 0x235FD, 0x23617,
      0x1C506, 0x1C511, 0x2255C, 0x22562, 0x2256D, 0x2257C, 0x22586, 0x22591, 0x225A5, 0x32BF0},
     {{0xB898, 27, 18}, {0xBA7E, 10, 8}, {0xBACE, 17, 9}, {0xBB67, 7, 2}, {0xBB75, 11, 7}, {0xBBC2, 13, 9},
      0x27094, 0x2709A, 0x2709F, 0x270BD, 0x270C5, 0x270D7, 0x276AA, 0x276B1, 0x276B8, 0x276C7,
      0x276D6, 0x276E8, 0x27BA8, 5},
     {{0xBC37, 21, 255}, "ITEMS", 73, 28, 9, 86, 0x87, 0xB1, {41, 42, 43, 44, 37, 36},
      0x773D, 0x7745, 0x3CF1A, 0x3CF20, 0x3CF26, 0x7D3F, 0x7D18, 0x7B5D, 0x2916E,
      0x2859A, 0x285A0, 0x2856B, 0x37AF6, 0x37AFD, 0x28EF7, 0x28F03, 0x28F0F, 0x28F1E, 0x38F44,
      0x270CA, 0x270D1},
     {0xABE0,
      {0x37DC, 0x47B0, 0x37DC, 0x65, 0x3E3A, 0x3EA2, 0x3EAA, 0x3EBB, 0x3EC0, 0x3F20, 0x3F33, 0x3F88, 0x3FFA,
       0x404E, 0x4124, 0x4174, 0x41DA, 0x429B, 0x45BE},
      0x081A, 0x0822, 0x3EC3,
      0x61, 0x1A, 0x2F, 0x12, 0x30, 0x6B, 0x7C, 0x08, 0x86,
      {0x0B, 0x12, 0x0C, 0x15}, 0x0F, {0x22, 0x10}, 0x1F, 0x2F,
      0x206B3, 0x206C8, 0x206D4, 0x206DF, 0x206C1, 0x20710, 0x2071F, 0x20730, 0x20736}},
};

} // namespace

const char* program_name(games::Game g)
{
    for (const Profile& p : kProfiles)
        if (p.game == g) return p.program;
    return nullptr;
}

const Profile* find(games::Game g, uint32_t program_size, uint32_t image_size)
{
    for (const Profile& p : kProfiles)
        if (p.game == g && p.program_size == program_size && p.image_size == image_size) return &p;
    return nullptr;
}

} // namespace profile

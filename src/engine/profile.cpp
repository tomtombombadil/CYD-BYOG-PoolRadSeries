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
     {0xB133, 12, 42, 0x20111, 0x1EE80, 0x37E31, 0x37E36, "CURSE.CFG"},
     {{0xB898, 27, 18}, {0xBA7E, 10, 8}, {0xBACE, 17, 9}, {0xBB67, 7, 2}, {0xBB75, 11, 7}, {0xBBC2, 13, 9},
      0x27094, 0x2709A, 0x2709F, 0x270BD, 0x270C5, 0x270D7, 0x276AA, 0x276B1, 0x276B8, 0x276C7,
      0x276D6, 0x276E8, 0x27BA8, 5}},
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

#include "party.h"

#include <cstring>

namespace party {

void Character::name(char* out, size_t cap) const
{
    if (!cap) return;
    size_t n = rec[0];
    if (n > kNameMax) n = kNameMax;
    if (n > cap - 1) n = cap - 1;
    memcpy(out, rec + 1, n);
    out[n] = 0;
}

bool read_record(dax::ByteSource& src, Character& out)
{
    out = Character{};
    return src.read_at(0, out.rec, kRecordSize) == kRecordSize;
}

namespace {

// Byte runs one to one: Pool offset, Curse offset, length
constexpr uint16_t kPoolRuns[][3] = {
    {0x00, 0x00, 16},   {0x2C, 0x72, 1},    {0x2D, 0x73, 1},    {0x2E, 0x74, 1},    {0x2F, 0x75, 1},
    {0x30, 0x76, 2},    {0x32, 0x78, 1},    {0x33, 0x79, 56},   {0x6B, 0xDD, 1},    {0x6C, 0xDE, 1},
    {0x6D, 0xDF, 5},    {0x72, 0xE4, 1},    {0x73, 0xE5, 1},    {0x74, 0xE7, 1},    {0x75, 0xE8, 1},
    {0x76, 0xE9, 1},    {0x77, 0xEA, 8},    {0x83, 0xF6, 1},    {0x84, 0xF7, 1},    {0x85, 0xF8, 1},
    {0x86, 0xF9, 2},    {0x88, 0xFB, 14},   {0x96, 0x109, 8},   {0x9E, 0x119, 3},   {0xA1, 0x11C, 8},
    {0xA9, 0x124, 1},   {0xAA, 0x125, 1},   {0xAB, 0x126, 1},   {0xAC, 0x127, 4},   {0xB0, 0x12B, 1},
    {0xB1, 0x12C, 1},   {0xB2, 0x12D, 3},   {0xB5, 0x137, 3},   {0xB8, 0x13C, 2},   {0xBA, 0x13E, 3},
    {0xBD, 0x141, 3},   {0xC0, 0x144, 7},   {0x100, 0x185, 1},  {0x101, 0x186, 1},  {0x102, 0x187, 2},
    {0x10C, 0x195, 1},  {0x10D, 0x196, 1},  {0x10E, 0x197, 1},  {0x10F, 0x198, 1},  {0x110, 0x199, 1},
    {0x111, 0x19A, 2},  {0x113, 0x19C, 8},  {0x11B, 0x1A4, 1},  {0x11C, 0x1A5, 1},
};
constexpr int kListAt = 0x1E, kListSize = 84;   // Curse's memorized list

// Secret offset, Curse offset, length (secret_facts.md 7; pointers left out)
constexpr uint16_t kSecretRuns[][3] = {
    {0x00, 0x00, 0x1E},  {0x69, 0x72, 4},     {0x6E, 0x76, 3},     {0x71, 0x79, 100},   {0xE6, 0xDD, 2},
    {0xE8, 0xDF, 6},     {0xEE, 0xE5, 5},     {0xF3, 0xEA, 8},     {0xFF, 0xF7, 0x12},  {0x111, 0x109, 7},
    {0x118, 0x111, 7},   {0x11F, 0x119, 1},   {0x120, 0x11B, 9},   {0x129, 0x124, 9},   {0x132, 0x12D, 5},
    {0x139, 0x132, 5},   {0x147, 0x137, 5},   {0x14E, 0x13C, 0x10}, {0x160, 0x14C, 1},   {0x199, 0x185, 4},
    {0x1A6, 0x195, 0x11},
    {0x6D, 0x191, 1},                   // the paladin's cures left (Curse's place; secret_facts_2.md 16)
};
// Secret offset, Ext place, length
constexpr uint16_t kSecretExt[][3] = {
    {0xD5, kExtBook, 17}, {0x137, kExtCleric67, 2}, {0x13E, kExtDruidMore, 9}, {0x14C, kExtMage67, 2},
    {0x15E, kExtFlags2, 1},   {0x15F, kExt15F, 1},       {0x1A5, kExtMagicRes, 1},
    {0x6B, kExtRace, 1},
};

// Darkness offset, Curse offset, length (darkness_facts.md 30, 38; pointers
// left out)
constexpr uint16_t kDarknessRuns[][3] = {
    {0x00, 0x00, 0x1E},  {0xAB, 0x72, 4},     {0xB0, 0x76, 3},     {0xB3, 0x79, 100},   {0x131, 0xDE, 1},
    {0x132, 0xDF, 6},    {0x138, 0xE5, 2},    {0x13A, 0xE9, 9},    {0x147, 0xF7, 4},    {0x14B, 0x103, 6},
    {0x151, 0x109, 7},   {0x158, 0x111, 7},   {0x166, 0x119, 1},   {0x167, 0x11B, 9},   {0x170, 0x124, 1},
    {0x171, 0x126, 5},   {0x17B, 0x12B, 2},   {0x17D, 0x12D, 5},   {0x186, 0x132, 5},   {0x18F, 0x137, 5},
    {0x198, 0x13C, 2},   {0x19A, 0x141, 11},  {0x1A6, 0x14C, 1},   {0x1DF, 0x185, 4},   {0x1ED, 0x195, 0x11},
    {0xAF, 0x191, 1},                   // the paladin's cures left (as Secret's 0x6D)
    {0x1E, kListAt, kListSize},         // the first 84 memorized places, in place
};
// Darkness offset, Ext place, length
constexpr uint16_t kDarknessExt[][3] = {
    {0x117, kExtBook, 17},    {0x128, kExtBook2, 9},     {0x182, kExtCleric67, 2}, {0x184, kExtCleric89, 2},
    {0x18B, kExtDruidMore, 2}, {0x18D, kExtDruid89, 2},  {0x194, kExtMage67, 2},   {0x196, kExtMage89, 2},
    {0x1E + kListSize, kExtMemorized, kDarknessMemorized - kListSize},
    {0x15F, kExtBestLevels, 7}, {0x176, kExtBestXp, 4},  {0x17A, kExtBestHp, 1},
    {0x1A5, kExtFlags2, 1},   {0x1EB, kExtMagicRes, 1},  {0x1EC, kExtTrain, 1},    {0xAD, kExtRace, 1},
};

// Champions offset, Curse offset, length (champions_facts.md 2; pointers left
// out): name and stats; 0x58-0x59 (THAC0); class; age, HP; the book's spells
// 1-100; 0xCE-0xE2 (icon size, saves, movement, levels, thief skills); NPC,
// modified; the coins (steel in gold's place, platinum in platinum's);
// levels and former levels; sex; alignment to the experience and hit dice;
// the spell slots (cleric, druid, magic-user); xp award to the icon's
// colours; flags; hands, save bonus, encumbrance; status to movement
constexpr uint16_t kKrynnRuns[][3] = {
    {0x00, 0x00, 0x1E},  {0x58, 0x72, 2},     {0x5B, 0x75, 1},     {0x60, 0x76, 3},     {0x63, 0x79, 100},
    {0xCE, 0xDD, 0x15},  {0xE7, 0xF7, 2},     {0xED, 0xFB, 4},     {0xF1, 0x103, 2},    {0xF3, 0x101, 2},
    {0xF5, 0x105, 4},    {0xF9, 0x109, 16},   {0x109, 0x119, 1},   {0x10A, 0x11B, 0x12}, {0x11C, 0x12D, 10},
    {0x12B, 0x137, 5},   {0x130, 0x13C, 0xF},  {0x13F, 0x14B, 1},  {0x17B, 0x185, 4},   {0x188, 0x195, 0x11},
};
// Champions offset, Ext place, length
constexpr uint16_t kKrynnExt[][3] = {
    {0xC7, kExtBook, 7},      {0x5C, kExtKnight, 1},    {0x5D, kExtGod, 1},       {0x5E, kExtRobe, 1},
    {0x5F, kExtK5F, 1},       {0xE9, kExtKE9, 4},       {0x126, kExtKSlots, 5},   {0x140, kExtFlags2, 1},
    {0x141, kExtK141, 1},     {0x187, kExtMagicRes, 1}, {0x5A, kExtRace, 1},
};

// Death Knights offset, Curse offset, length (deathknights_facts.md 2;
// pointers left out): name and stats; 0x1E (to learn) and THAC0; class; age
// and HP; icon size, saves; movement, the highest level; level vs undead and
// thief skills; NPC; steel in platinum's place (Darkness's three coins), gems, jewelry; levels (the
// knight in the monk's); sex; alignment to the experience; class flags, HP
// rolled; xp award and bonus; icon, flags; the item count; hands, save
// bonus, encumbrance; status to movement
constexpr uint16_t kDkkRuns[][3] = {
    {0x00, 0x00, 0x1E},  {0x1E, 0x72, 2},     {0x21, 0x75, 1},     {0x26, 0x76, 3},     {0x29, 0xDE, 6},
    {0x2F, 0xE4, 2},     {0x31, 0xE9, 9},     {0x3E, 0xF7, 1},     {0x3F, 0x103, 2},    {0x41, 0x105, 4},
    {0x45, 0x109, 8},    {0x55, 0x119, 1},    {0x56, 0x11B, 0x10}, {0x6B, 0x12B, 2},    {0x6D, 0x13C, 3},
    {0x70, 0x141, 11},   {0x7C, 0x14C, 1},    {0xB5, 0x185, 4},    {0xC7, 0x195, 0x11},
};
// Death Knights offset, Ext place, length
constexpr uint16_t kDkkExt[][3] = {
    {0x20, kExtRace, 1},      {0x22, kExtKnight, 1},    {0x23, kExtGod, 1},       {0x24, kExtRobe, 1},
    {0x25, kExtK5F, 1},       {0x4D, kExtBestLevels, 7}, {0x54, kExtDkBestKnight, 1}, {0x66, kExtBestXp, 4},
    {0x6A, kExtBestHp, 1},    {0x7B, kExtFlags2, 1},    {0xC1, kExtMagicRes, 1},  {0xC6, kExtTrain, 1},
};
// Its spell part: offset, Curse offset, length (the memorized places in
// place, spells 1-100, the slots of levels 1-5)
constexpr uint16_t kDkkWizRuns[][3] = {
    {0x00, kListAt, kDkkMemorized}, {0x4A, 0x79, 100}, {0xBC, 0x12D, 5}, {0xC4, 0x132, 5}, {0xD4, 0x137, 5},
};
// ... and its Ext places: spells 101-114, levels 6-8 of the three kinds,
// the third group
constexpr uint16_t kDkkWizExt[][3] = {
    {0xAE, kExtBook, 14},      {0xC1, kExtCleric67, 2}, {0xC3, kExtCleric89, 1}, {0xC9, kExtDruidMore, 2},
    {0xCB, kExtDruid89, 1},    {0xD9, kExtMage67, 2},   {0xDB, kExtMage89, 1},   {0xCC, kExtDkThird, 8},
};

// Dark Queen offset, Curse offset, length (queen_facts_2.md 8; pointers left
// out): stats (own / in use, Str00 below), THAC0 base, age, HP max, icon size and
// saves, movement and the highest level, level vs undead, thief skills, NPC,
// steel gems jewelry (Darkness's places), former levels, sex, alignment,
// experience, xp award, icon, flags, hands, save bonus, the item count,
// status, hostile, quick fight, the fight's figures to movement
constexpr uint16_t kQueenRuns[][3] = {
    {0x78, 0x10, 12},    {0x84, 0x1D, 1},     {0x85, 0x1C, 1},     {0x87, 0x73, 1},     {0x52, 0x76, 2},
    {0x89, 0x78, 1},     {0x8A, 0xDE, 6},     {0x90, 0xE4, 2},     {0x5C, 0xE9, 1},     {0x93, 0xEA, 8},
    {0x9B, 0xF7, 1},     {0x4C, 0x103, 2},    {0x4E, 0x105, 4},    {0x60, 0x119, 1},    {0x62, 0x11B, 1},
    {0x44, 0x127, 4},    {0x54, 0x12B, 2},    {0xC4, 0x13C, 1},    {0xC7, 0x13D, 2},    {0xCA, 0x185, 2},
    {0x56, 0x187, 2},    {0xC9, 0x14C, 1},    {0x64, 0x195, 1},    {0x66, 0x197, 1},    {0x18B, 0x196, 1},
    {0x18C, 0x198, 1},   {0x18D, 0x199, 13},
};
// Dark Queen offset, Ext place, length
constexpr uint16_t kQueenExt[][3] = {
    {0x58, kExtRace, 1}, {0x19A, kExtKnight, 1}, {0x19B, kExtGod, 1}, {0x19D, kExtRobe, 1}, {0xCC, kExtMagicRes, 1},
    {0x9E, kExtBestLevels, 7}, {0x48, kExtBestXp, 4}, {0xBE, kExtBestHp, 1}, {0xCD, kExtTrain, 1},
};

} // namespace

void from_queen(const uint8_t* q, Character& c)
{
    uint8_t* r = c.rec;
    memset(r, 0, kRecordSize);
    memset(c.ext, 0, kExtSize);
    c.n_items = c.n_affects = 0;
    // The name: a C string of up to 15 at 0x68 - Curse's Pascal one
    size_t n = 0;
    while (n < kNameMax && n < 15 && q[0x68 + n]) ++n;
    r[0] = static_cast<uint8_t>(n);
    memcpy(r + 1, q + 0x68, n);
    for (const auto& run : kQueenRuns) memcpy(r + run[1], q + run[0], run[2]);
    for (const auto& run : kQueenExt) memcpy(c.ext + run[1], q + run[0], run[2]);
    r[0x74] = race_from_krynn(q[0x58]);
    // The class: its knight (1, the druid's place) is the engine's Krynn
    // knight (7, the monk's place); the others as Curse's numbers
    r[0x75] = q[0x5A] == 1 ? 7 : q[0x5A];
    // Levels and former levels: cleric knight fighter paladin ranger
    // magic-user thief - the knight's into the monk's place
    static const int8_t kLevelTo[7] = {0, 7, 2, 3, 4, 5, 6};
    for (int i = 0; i < 7; ++i) {
        r[0x109 + kLevelTo[i]] = q[0xA5 + i];
        r[0x111 + kLevelTo[i]] = q[0xAC + i];
    }
}

void krynn_to_dkk(const uint8_t* k, uint8_t* p, uint8_t* w)
{
    static const uint16_t kRuns[][3] = {{0x000, 0x00, 0x1E}, {0x058, 0x1E, 0x0B}, {0x0CF, 0x29, 0x08}, {0x0DA, 0x31, 0x0C},
                                        {0x0F3, 0x3F, 0x27}, {0x11A, 0x6B, 0x02}, {0x130, 0x6D, 0x03}, {0x135, 0x70, 0x0C},
                                        {0x142, 0x7C, 0x46}, {0x188, 0xC7, 0x11}};
    memset(p, 0, kDkkRecordSize);
    memset(w, 0, kDkkSpellSize);
    for (const auto& r : kRuns) memcpy(p + r[1], k + r[0], r[2]);
    // (its pointers: 0x3A, 0x7D, 0xB9, 0xBD, 0xC2 - nothing here to clear)
    const uint8_t cls = p[0x21];
    if (cls != 2 && cls != 6 && cls != 0x0E) {
        memcpy(w, k + 0x1E, 0x3A);              // memorized
        memcpy(w + 0x4A, k + 0x63, 0x6B);       // the book
        for (int i = 0; i < 4; ++i) memcpy(w + 0xBC + 8 * i, k + 0x11C + 5 * i, 5);   // the slots
    }
    const uint16_t age = static_cast<uint16_t>(p[0x26] | p[0x27] << 8);
    uint16_t a = age;
    switch (p[0x20]) {
    case 0: case 1: if (a > 1200) a = 1200; break;
    case 2: if (a > 200) a = 200; break;
    case 3: case 4: if (a > 300) a = 300; break;
    case 5: if (a > 199) a = 70; break;
    case 6: if (a > 50) a = 50; break;
    default: break;
    }
    p[0x26] = static_cast<uint8_t>(a);
    p[0x27] = static_cast<uint8_t>(a >> 8);
}

void from_dkk(const uint8_t* p, const uint8_t* w, uint8_t* r)
{
    memset(r, 0, kRecordSize);
    for (const auto& run : kDkkRuns) memcpy(r + run[1], p + run[0], run[2]);
    if (w)
        for (const auto& run : kDkkWizRuns) memcpy(r + run[1], w + run[0], run[2]);
    r[0x74] = race_from_krynn(p[0x20]);
}

void dkk_spells_in(const uint8_t* w, Character& c)
{
    for (const auto& run : kDkkWizRuns) memcpy(c.rec + run[1], w + run[0], run[2]);
    for (const auto& run : kDkkWizExt) memcpy(c.ext + run[1], w + run[0], run[2]);
}

void from_dkk(const uint8_t* p, const uint8_t* w, Character& c)
{
    from_dkk(p, nullptr, c.rec);
    for (const auto& run : kDkkExt) memcpy(c.ext + run[1], p + run[0], run[2]);
    if (w) dkk_spells_in(w, c);
}

void to_dkk(const Character& c, uint8_t* p, uint8_t* w)
{
    const uint8_t* r = c.rec;
    memset(p, 0, kDkkRecordSize);
    memset(w, 0, kDkkSpellSize);
    for (const auto& run : kDkkRuns) memcpy(p + run[0], r + run[1], run[2]);
    for (const auto& run : kDkkExt) memcpy(p + run[0], c.ext + run[1], run[2]);
    for (const auto& run : kDkkWizRuns) memcpy(w + run[0], r + run[1], run[2]);
    for (const auto& run : kDkkWizExt) memcpy(w + run[0], c.ext + run[1], run[2]);
    p[0x20] = race_from_krynn(c.ext[kExtRace]) == r[0x74] ? c.ext[kExtRace] : race_to_krynn(r[0x74]);
    // The memorized spells: 74 places; any the engine put further on go
    // into its free places, from the end
    int free_at = kDkkMemorized - 1;
    for (int i = kDkkMemorized; i < kListSize; ++i) {
        if (!r[kListAt + i]) continue;
        while (free_at >= 0 && w[free_at]) --free_at;
        if (free_at < 0) break;
        w[free_at] = r[kListAt + i];
    }
    p[0x7C] = static_cast<uint8_t>(c.n_items);
}

bool script_value_dkk(const Party& p, uint16_t off, uint16_t* out)
{
    const Character* c = p.sel();
    if (!c) return false;
    return script_value_dkk(c->rec, c->ext, p.selected, off, out);
}

bool script_value_dkk(const uint8_t* r, const uint8_t* ext, int index, uint16_t off, uint16_t* out)
{
    // (its operand loader's switch, GAME.OVR 0x72E2 - the Death Knights
    // record's fields at their places in Curse's layout)
    auto u16 = [r](int o) { return static_cast<uint16_t>(r[o] | r[o + 1] << 8); };
    if (off <= 0x0F) { *out = r[1 + off]; return true; }                      // the name's letters
    if (off >= 0x14 && off <= 0x1A) { *out = r[0x11 + (off - 0x14) * 2]; return true; }   // stats in use
    if (off >= 0x1B && off <= 0x64) { *out = r[kListAt + (off - 0x1B)]; return true; }   // memorized
    if (off >= 0x65 && off <= 0x6B) { *out = r[0x10 + (off - 0x65) * 2]; return true; }   // own stats
    if (off >= 0x9A && off <= 0x9E) { *out = r[0xDF + (off - 0x9A)]; return true; }      // saves
    if (off >= 0xA5 && off <= 0xAC) { *out = r[0xEA + (off - 0xA5)]; return true; }      // thief skills
    if (off >= 0xC3 && off <= 0xC8) {                                          // steel, gems, jewelry
        static const uint16_t kAt[3] = {0x103, 0x105, 0x107};
        *out = u16(kAt[(off - 0xC3) / 2]);
        return true;
    }
    switch (off) {
    case 0x71:  *out = r[0x73]; return true;    // THAC0 base
    case 0x72:                                  // race, in its own numbers
        *out = ext && race_from_krynn(ext[kExtRace]) == r[0x74] ? ext[kExtRace] : race_to_krynn(r[0x74]);
        return true;
    case 0x74:  *out = u16(0x76); return true;  // age
    case 0x76:  *out = r[0x78]; return true;    // HP max
    case 0x9F:  *out = r[0x1A5]; return true;   // movement
    case 0xB8:  *out = r[0xF7]; return true;    // NPC
    case 0xBB:  *out = ext ? ext[kExtRobe] : 0; return true;
    case 0xBC:  *out = ext ? ext[kExtGod] : 0; return true;
    case 0xBF:  *out = ext ? ext[kExtKnight] : 0; return true;
    case 0xD3:  *out = ext ? ext[kExtMagicRes] : 0; return true;
    case 0xD6:  *out = r[0x119]; return true;   // sex
    case 0xD8:  *out = r[0x11B]; return true;   // alignment
    case 0xEB: {                                // the classes held: mage 1, cleric 2, thief 4, fighter 8,
        static const uint8_t kCls[7] = {5, 0, 6, 2, 7, 3, 4};      // knight 0x10, paladin 0x40, ranger 0x80
        static const uint8_t kBit[7] = {1, 2, 4, 8, 0x10, 0x40, 0x80};
        uint16_t v = 0;
        for (int i = 0; i < 7; ++i)
            if (static_cast<int8_t>(r[0x109 + kCls[i]]) > 0) v = static_cast<uint16_t>(v + kBit[i]);
        *out = v;
        return true;
    }
    case 0x100: *out = r[0x196] ? 1 : 0x80; return true;    // in action
    case 0x10E: *out = r[0x199]; return true;   // THAC0 in use
    case 0x10F: *out = r[0x19A]; return true;   // AC
    case 0x110: *out = r[0x19B]; return true;   // AC behind
    case 0x119: *out = r[0x1A4]; return true;   // HP now
    case 0x11A: *out = r[0x1A5]; return true;   // movement
    case 0x2B1:
    case 0x2B4: *out = static_cast<uint16_t>(index); return true;
    default:
        return false;
    }
}

void script_set_dkk(Character& c, uint16_t off, uint16_t v) { script_set_dkk(c.rec, c.ext, off, v); }

void script_set_dkk(uint8_t* r, uint8_t*, uint16_t off, uint16_t v)
{
    // (its write switch, GAME.OVR 0x7642)
    if (off >= 0x1B && off <= 0x64) {           // the memorized spells
        r[kListAt + (off - 0x1B)] = static_cast<uint8_t>(v);
        return;
    }
    if (off >= 0xC3 && off <= 0xC8) {           // steel, gems, jewelry
        static const uint16_t kAt[3] = {0x103, 0x105, 0x107};
        const int o = kAt[(off - 0xC3) / 2];
        r[o] = static_cast<uint8_t>(v);
        r[o + 1] = static_cast<uint8_t>(v >> 8);
        return;
    }
    switch (off) {
    case 0xB8: r[0xF7] = static_cast<uint8_t>(v); break;    // NPC
    case 0x100:                                 // 1: back to okay, in action
        if (v == 1) {
            r[0x195] = 0;
            r[0x196] = 1;
        }
        break;
    case 0x10C: script_set(r, off, v); break;   // side: as Curse's
    case 0x119: r[0x1A4] = static_cast<uint8_t>(v); break;   // HP now
    default: break;
    }
}

uint8_t race_from_krynn(uint8_t v)
{
    static const uint8_t kTo[8] = {2, 2, 4, 1, 1, 5, 7, 0};
    return v < 8 ? kTo[v] : v;
}

uint8_t race_to_krynn(uint8_t v)
{
    // (Curse's gnome: a hill dwarf; half-orc: human)
    static const uint8_t kTo[8] = {7, 3, 1, 4, 2, 5, 6, 6};
    return v < 8 ? kTo[v] : v;
}

void from_krynn(const uint8_t* p, uint8_t* r)
{
    memset(r, 0, kRecordSize);
    for (const auto& run : kKrynnRuns) memcpy(r + run[1], p + run[0], run[2]);
    memcpy(r + kListAt, p + 0x1E, kKrynnMemorized);        // the memorized spells, in place
    r[0x74] = race_from_krynn(p[0x5A]);
    r[0x14C] = p[0x142];                                    // (the item count)
}

void from_krynn(const uint8_t* p, Character& c)
{
    from_krynn(p, c.rec);
    for (const auto& run : kKrynnExt) memcpy(c.ext + run[1], p + run[0], run[2]);
}

void to_krynn(const Character& c, uint8_t* p)
{
    const uint8_t* r = c.rec;
    memset(p, 0, kKrynnRecordSize);
    for (const auto& run : kKrynnRuns) memcpy(p + run[0], r + run[1], run[2]);
    for (const auto& run : kKrynnExt) memcpy(p + run[0], c.ext + run[1], run[2]);
    p[0x5A] = race_from_krynn(c.ext[kExtRace]) == r[0x74] ? c.ext[kExtRace] : race_to_krynn(r[0x74]);
    // The memorized spells: 58 places; any the engine put further on go
    // into its free places, from the end
    memcpy(p + 0x1E, r + kListAt, kKrynnMemorized);
    int free_at = kKrynnMemorized - 1;
    for (int i = kKrynnMemorized; i < kListSize; ++i) {
        if (!r[kListAt + i]) continue;
        while (free_at >= 0 && p[0x1E + free_at]) --free_at;
        if (free_at < 0) break;
        p[0x1E + free_at] = r[kListAt + i];
    }
    p[0x142] = static_cast<uint8_t>(c.n_items);
}

void from_pool(const uint8_t* p, Character& c) { from_pool(p, c.rec); }

void from_pool(const uint8_t* p, uint8_t* r)
{
    memset(r, 0, kRecordSize);
    for (const auto& run : kPoolRuns) memcpy(r + run[1], p + run[0], run[2]);
    for (int i = 0; i < 6; ++i) r[0x10 + i * 2] = r[0x11 + i * 2] = p[0x10 + i];
    r[0x1C] = r[0x1D] = p[0x16];                            // 18/xx
    memcpy(r + kListAt, p + 0x17, kPoolMemorized);          // the memorized spells, in place
    r[0xE6] = r[0xE5];                                      // (Curse's hit dice for multi-classes)
}

void to_pool(const Character& c, uint8_t* p)
{
    const uint8_t* r = c.rec;
    memset(p, 0, kPoolRecordSize);
    for (const auto& run : kPoolRuns) memcpy(p + run[0], r + run[1], run[2]);
    for (int i = 0; i < 6; ++i) p[0x10 + i] = r[0x10 + i * 2];   // its own value
    p[0x16] = r[0x1D];
    // The memorized spells: Pool has 21 places; any the engine put further
    // on go into its free places, from the end (as the game fills them)
    memcpy(p + 0x17, r + kListAt, kPoolMemorized);
    int free_at = kPoolMemorized - 1;
    for (int i = kPoolMemorized; i < kListSize; ++i) {
        if (!r[kListAt + i]) continue;
        while (free_at >= 0 && p[0x17 + free_at]) --free_at;
        if (free_at < 0) break;
        p[0x17 + free_at] = r[kListAt + i];
    }
    p[0xC7] = static_cast<uint8_t>(c.n_items);
}

uint8_t race_from_secret(uint8_t v)
{
    static const uint8_t kTo[8] = {0, 2, 4, 1, 3, 5, 7, 0};   // (tribble: as a monster)
    return v < 8 ? kTo[v] : v;
}

uint8_t race_to_secret(uint8_t v)
{
    static const uint8_t kTo[8] = {7, 3, 1, 4, 2, 5, 6, 6};   // (half-orc: Secret has none - human)
    return v < 8 ? kTo[v] : v;
}

void from_secret(const uint8_t* p, uint8_t* r)
{
    memset(r, 0, kRecordSize);
    for (const auto& run : kSecretRuns) memcpy(r + run[1], p + run[0], run[2]);
    memcpy(r + kListAt, p + 0x1E, kSecretMemorized);       // the memorized spells, in place
    r[0x74] = race_from_secret(p[0x6B]);
}

void from_secret(const uint8_t* p, Character& c)
{
    from_secret(p, c.rec);
    for (const auto& run : kSecretExt) memcpy(c.ext + run[1], p + run[0], run[2]);
}

void to_secret(const Character& c, uint8_t* p)
{
    const uint8_t* r = c.rec;
    memset(p, 0, kSecretRecordSize);
    for (const auto& run : kSecretRuns) memcpy(p + run[0], r + run[1], run[2]);
    for (const auto& run : kSecretExt) memcpy(p + run[0], c.ext + run[1], run[2]);
    // The race: its own number when it still means the race the record has
    // (a tribble stays one), else Curse's turned into Secret's
    p[0x6B] = race_from_secret(c.ext[kExtRace]) == r[0x74] ? c.ext[kExtRace] : race_to_secret(r[0x74]);
    // The memorized spells: 75 places; any the engine put further on go
    // into its free places, from the end
    memcpy(p + 0x1E, r + kListAt, kSecretMemorized);
    int free_at = kSecretMemorized - 1;
    for (int i = kSecretMemorized; i < kListSize; ++i) {
        if (!r[kListAt + i]) continue;
        while (free_at >= 0 && p[0x1E + free_at]) --free_at;
        if (free_at < 0) break;
        p[0x1E + free_at] = r[kListAt + i];
    }
    p[0x160] = static_cast<uint8_t>(c.n_items);
}

uint8_t race_from_darkness(uint8_t v)
{
    static const uint8_t kTo[7] = {2, 4, 1, 3, 5, 7, 0};
    return v < 7 ? kTo[v] : v;
}

uint8_t race_to_darkness(uint8_t v)
{
    static const uint8_t kTo[8] = {6, 2, 0, 3, 1, 4, 5, 5};   // (half-orc: Darkness has none - human)
    return v < 8 ? kTo[v] : v;
}

void from_darkness(const uint8_t* p, uint8_t* r)
{
    memset(r, 0, kRecordSize);
    for (const auto& run : kDarknessRuns) memcpy(r + run[1], p + run[0], run[2]);
    r[0x74] = race_from_darkness(p[0xAD]);
}

// Darkness's 141 memorized places, filled from the end (Guy's at 137-140):
// the engine's spell rules look at Curse's 84 in the record, so the spells
// move there - packed from the end - and only the 85th on wait in ext; on
// the way back they fill the 141 from the end again (the original's order)
static void pack_memorized(Character& c)
{
    // (as read: places 84-140 in ext, 0-83 in the record - from the end)
    uint8_t list[kDarknessMemorized];
    int n = 0;
    for (int i = kDarknessMemorized - kListSize - 1; i >= 0; --i)
        if (c.ext[kExtMemorized + i]) list[n++] = c.ext[kExtMemorized + i];
    for (int i = kListSize - 1; i >= 0; --i)
        if (c.rec[kListAt + i]) list[n++] = c.rec[kListAt + i];
    memset(c.rec + kListAt, 0, kListSize);
    memset(c.ext + kExtMemorized, 0, kDarknessMemorized - kListSize);
    for (int k = 0; k < n; ++k) {
        if (k < kListSize) c.rec[kListAt + kListSize - 1 - k] = list[k];
        else c.ext[kExtMemorized + (kDarknessMemorized - kListSize - 1) - (k - kListSize)] = list[k];
    }
}

void from_darkness(const uint8_t* p, Character& c)
{
    from_darkness(p, c.rec);
    for (const auto& run : kDarknessExt) memcpy(c.ext + run[1], p + run[0], run[2]);
    pack_memorized(c);
}

void to_darkness(const Character& c, uint8_t* p)
{
    const uint8_t* r = c.rec;
    memset(p, 0, kDarknessRecordSize);
    for (const auto& run : kDarknessRuns) memcpy(p + run[0], r + run[1], run[2]);
    for (const auto& run : kDarknessExt) memcpy(p + run[0], c.ext + run[1], run[2]);
    {
        // (the memorized spells from the end of the 141, as the original fills them)
        uint8_t list[kDarknessMemorized];
        int n = 0;
        for (int i = kListSize - 1; i >= 0; --i)
            if (r[kListAt + i]) list[n++] = r[kListAt + i];
        for (int i = kDarknessMemorized - kListSize - 1; i >= 0; --i)
            if (c.ext[kExtMemorized + i]) list[n++] = c.ext[kExtMemorized + i];
        memset(p + 0x1E, 0, kDarknessMemorized);
        for (int k = 0; k < n; ++k) p[0x1E + kDarknessMemorized - 1 - k] = list[k];
    }
    // The race: its own number while it still means the race the record has
    p[0xAD] = race_from_darkness(c.ext[kExtRace]) == r[0x74] ? c.ext[kExtRace] : race_to_darkness(r[0x74]);
    p[0x1A6] = static_cast<uint8_t>(c.n_items);
}

uint8_t darkness_byte(const uint8_t* rec, const uint8_t* ext, int n_items, uint16_t off)
{
    if (off >= kDarknessRecordSize) return 0;
    if (off == 0xAD) {
        const uint8_t own = ext ? ext[kExtRace] : 0;
        return ext && race_from_darkness(own) == rec[0x74] ? own : race_to_darkness(rec[0x74]);
    }
    if (off == 0x1A6) return static_cast<uint8_t>(n_items);
    for (const auto& run : kDarknessExt)
        if (off >= run[0] && off < run[0] + run[2]) return ext ? ext[run[1] + off - run[0]] : 0;
    for (const auto& run : kDarknessRuns)
        if (off >= run[0] && off < run[0] + run[2]) return rec[run[1] + off - run[0]];
    return 0;
}

bool darkness_script_set(Character& c, uint16_t off, uint8_t v)
{
    // (its program's list: GAME.OVR 0x6C40 - darkness_facts.md 30)
    const bool ok = (off >= 0x1E && off <= 0xAA) || off == 0xB0 || off == 0xB1 || off == 0x137 ||
                    (off >= 0x14B && off <= 0x150) || off == 0x167 || off == 0x170 || off == 0x1EB ||
                    off == 0x1ED || off == 0x1EF || off == 0x1FC;
    if (!ok) return false;
    auto put = [&](uint16_t o, uint8_t b) {
        for (const auto& run : kDarknessRuns)
            if (o >= run[0] && o < run[0] + run[2]) c.rec[run[1] + o - run[0]] = b;
        for (const auto& run : kDarknessExt)
            if (o >= run[0] && o < run[0] + run[2]) c.ext[run[1] + o - run[0]] = b;
    };
    put(off, v);
    if (off == 0x1ED) put(0x1EE, v == 0 ? 1 : 0);      // "enabled" goes with the status
    return true;
}

bool read_record(dax::ByteSource& src, Character& out, Format f)
{
    if (f == Format::Curse) return read_record(src, out);
    if (f == Format::Darkness) {
        uint8_t dk[kDarknessRecordSize];
        out = Character{};
        if (src.read_at(0, dk, kDarknessRecordSize) != kDarknessRecordSize) return false;
        from_darkness(dk, out);
        return true;
    }
    if (f == Format::Secret) {
        uint8_t sec[kSecretRecordSize];
        out = Character{};
        if (src.read_at(0, sec, kSecretRecordSize) != kSecretRecordSize) return false;
        from_secret(sec, out);
        return true;
    }
    if (f == Format::Krynn) {
        uint8_t k[kKrynnRecordSize];
        out = Character{};
        if (src.read_at(0, k, kKrynnRecordSize) != kKrynnRecordSize) return false;
        from_krynn(k, out);
        return true;
    }
    if (f == Format::KrynnToDkk) {
        uint8_t k[kKrynnRecordSize], dk[kDkkRecordSize], wiz[kDkkSpellSize];
        out = Character{};
        if (src.read_at(0, k, kKrynnRecordSize) != kKrynnRecordSize) return false;
        krynn_to_dkk(k, dk, wiz);
        from_dkk(dk, wiz, out);
        return true;
    }
    if (f == Format::DeathKnights) {
        // (the record alone: its spell part is a file of its own, read by
        // the caller with from_dkk)
        uint8_t k[kDkkRecordSize];
        out = Character{};
        if (src.read_at(0, k, kDkkRecordSize) != kDkkRecordSize) return false;
        from_dkk(k, nullptr, out);
        return true;
    }
    uint8_t pool[kPoolRecordSize];
    out = Character{};
    if (src.read_at(0, pool, kPoolRecordSize) != kPoolRecordSize) return false;
    from_pool(pool, out);
    return true;
}

const uint8_t* record_bytes(const Character& c, Format f, uint8_t* buf, size_t* n)
{
    if (f == Format::Curse) {
        *n = kRecordSize;
        return c.rec;
    }
    if (f == Format::Darkness) {
        to_darkness(c, buf);
        *n = kDarknessRecordSize;
        return buf;
    }
    if (f == Format::Secret) {
        to_secret(c, buf);
        *n = kSecretRecordSize;
        return buf;
    }
    if (f == Format::Krynn) {
        to_krynn(c, buf);
        *n = kKrynnRecordSize;
        return buf;
    }
    if (f == Format::DeathKnights) {
        // (the record; buf holds the spell part after it - the caller
        // saves buf + kDkkRecordSize as the .WIZ)
        to_dkk(c, buf, buf + kDkkRecordSize);
        *n = kDkkRecordSize;
        return buf;
    }
    to_pool(c, buf);
    *n = kPoolRecordSize;
    return buf;
}

void read_items(dax::ByteSource& src, Character& out) { read_items(src, out, kItemSize); }

void read_items(dax::ByteSource& src, Character& out, uint32_t stride)
{
    out.n_items = 0;
    if (stride < kItemSize) return;
    const uint32_t n = src.size() / stride;
    for (uint32_t i = 0; i < n && out.n_items < kMaxItems; ++i)
        if (src.read_at(i * stride, out.items[out.n_items], kItemSize) == kItemSize) ++out.n_items;
}

void read_affects(dax::ByteSource& src, Character& out)
{
    out.n_affects = 0;
    const uint32_t n = src.size() / kAffectSize;
    for (uint32_t i = 0; i < n && out.n_affects < kMaxAffects; ++i)
        if (src.read_at(i * kAffectSize, out.affects[out.n_affects], kAffectSize) == kAffectSize) ++out.n_affects;
}

// The fields the scripts can read (Curse; from coab's list of them)
bool script_value(const Party& p, uint16_t off, uint16_t* out, bool pool)
{
    const Character* c = p.sel();
    if (!c) return false;
    return script_value(c->rec, p.selected, off, out, pool);
}

bool script_value(const uint8_t* r, int index, uint16_t off, uint16_t* out, bool pool)
{
    // Pool's program gives scripts fewer fields: Int, race, class, the
    // magic-user's level, sex and 0xE4 are plain memory there (poolrad_facts.md 4.4).
    // Race is given all the same (Tom, v0.85.1): the scripts' race checks -
    // Podol Plaza's patrols stopping a party with humans more often, the
    // Temple of Bane's elves and half-elves finding its trap doors - never
    // matched in the original (a Pool bug); here they work as written.
    if (pool && (off == 0x15 || off == 0x73 || off == 0xC9 || off == 0xD6 || off == 0xE4)) return false;
    auto u16 = [r](int o) { return static_cast<uint16_t>(r[o] | r[o + 1] << 8); };
    if (off >= 0xA5 && off <= 0xAC) {           // thief skills
        *out = r[0xEA + (off - 0xA5)];
        return true;
    }
    switch (off) {
    case 0x15:  *out = r[0x13]; return true;    // Int
    case 0x18:  *out = r[0x19]; return true;    // Con
    case 0x72:  *out = r[0x74]; return true;    // race
    case 0x73:  *out = r[0x75]; return true;    // class
    case 0x9B:  *out = r[0xE0]; return true;    // saving throw: petrification
    case 0xA0:  *out = r[0xE5]; return true;    // hit dice
    case 0xB8:  *out = r[0xF7]; return true;    // control
    case 0xBB:  *out = u16(0xFB); return true;  // copper
    case 0xBD:  *out = u16(0xFD); return true;  // silver (the record: copper, silver, electrum ...)
    case 0xBF:  *out = u16(0xFF); return true;  // electrum
    case 0xC1:  *out = u16(0x101); return true; // gold
    case 0xC3:  *out = u16(0x103); return true; // platinum
    case 0xC9: {                                // magic-user level (+ the old one: a human past it)
        int cur = 0;
        for (int k = 0; k < 7 && !cur; ++k) cur = r[0x109 + k];
        *out = static_cast<uint16_t>(r[0x10E] + (r[0x74] == 7 && cur > r[0xE6] ? r[0x116] : 0));
        return true;
    }
    case 0xD6:  *out = r[0x119]; return true;   // sex
    case 0xD8:  *out = r[0x11B]; return true;   // alignment
    case 0xE4:  *out = r[0x192] & 1; return true;
    case 0xF7:  *out = u16(0x13C); return true;
    case 0xF9:  *out = r[0x13E]; return true;
    case 0x100: *out = r[0x196] ? 1 : 0x80; return true;
    case 0x10C: *out = r[0x197] == 1 ? 0x81 : r[0x197] == 0 && r[0x198] ? 0x80 : 0; return true;   // side
    case 0x11B: *out = r[0x1A5]; return true;   // movement
    case 0x2B1:
    case 0x2B4: *out = static_cast<uint16_t>(index); return true;
    case 0x2CF: {                               // charisma: a percentage
        const int cha = r[0x1B];
        static const uint8_t kLow[] = {0, 5, 10, 15, 20};          // 3-7
        if (cha >= 3 && cha <= 7) *out = kLow[cha - 3];
        else if (cha >= 8 && cha <= 12) *out = 25;
        else if (cha >= 13 && cha <= 17) *out = static_cast<uint16_t>(cha == 16 ? 50 : cha == 17 ? 55 : 30 + (cha - 13) * 5);
        else if (cha >= 18 && cha <= 25) *out = 60;
        else *out = 0;
        return true;
    }
    default:
        return false;
    }
}

void script_set(Party& p, uint16_t off, uint16_t v)
{
    Character* c = p.sel();
    if (c) script_set(c->rec, off, v);
}

void script_set(uint8_t* r, uint16_t off, uint16_t v)
{
    auto w16 = [r, v](int o) {
        r[o] = static_cast<uint8_t>(v);
        r[o + 1] = static_cast<uint8_t>(v >> 8);
    };
    if (off >= 0x20 && off <= 0x70) {           // a spell slot
        r[off - 1] = static_cast<uint8_t>(v);
        return;
    }
    switch (off) {
    case 0xB8: r[0xF7] = static_cast<uint8_t>(v > 0xB2 ? v - 0x32 : v); break;   // control
    case 0xBB: w16(0xFB); break;
    case 0xBD: w16(0xFD); break;
    case 0xBF: w16(0xFF); break;
    case 0xC1: w16(0x101); break;
    case 0xC3: w16(0x103); break;
    case 0xF7: w16(0x13C); break;
    case 0xF9: r[0x13E] = static_cast<uint8_t>(v); break;
    case 0x100:                                 // out of action (0x87: stoned)
        if (v >= 0x80) {
            r[0x196] = 0;
            if (v == 0x87) r[0x195] = 7;
        }
        break;
    case 0x10C:                                 // side: ours / ours, the computer's / the enemy's
        if (v == 0) {
            r[0x197] = 0;
            r[0x198] = 0;
        } else if (v == 0x80) {
            r[0x197] = 0;
            r[0x198] = 1;
        } else if (v == 0x81) {
            r[0x197] = 1;
            r[0x198] = 1;
        }
        break;
    default: break;
    }
}

bool script_value_secret(const Party& p, uint16_t off, uint16_t* out)
{
    const Character* c = p.sel();
    if (!c) return false;
    return script_value_secret(c->rec, c->ext, p.selected, off, out);
}

bool script_value_secret(const uint8_t* r, const uint8_t* ext, int index, uint16_t off, uint16_t* out)
{
    auto u16 = [r](int o) { return static_cast<uint16_t>(r[o] | r[o + 1] << 8); };
    // A level: its own, + the old one for a human whose new class passed it
    auto level = [&](int cls) {
        int cur = 0;
        for (int k = 0; k < 7 && !cur; ++k) cur = r[0x109 + k];
        return static_cast<uint16_t>(r[0x109 + cls] + (r[0x74] == 7 && cur > r[0xE6] ? r[0x111 + cls] : 0));
    };
    if (off >= 0xA5 && off <= 0xAC) {           // thief skills
        *out = r[0xEA + (off - 0xA5)];
        return true;
    }
    switch (off) {
    case 0x15:  *out = r[0x13]; return true;    // Int
    case 0x18:  *out = r[0x19]; return true;    // Con
    case 0x19:  *out = r[0x1B]; return true;    // Cha
    case 0x72:                                  // race, in Secret's numbers
        *out = ext && race_from_secret(ext[kExtRace]) == r[0x74] ? ext[kExtRace] : race_to_secret(r[0x74]);
        return true;
    case 0x73:  *out = r[0x75]; return true;    // class
    case 0x76:  *out = r[0x78]; return true;    // HP max
    case 0x9B:  *out = r[0xE0]; return true;    // saving throw: petrification
    case 0xA0:  *out = r[0xE5]; return true;    // hit dice
    case 0xB8:  *out = r[0xF7]; return true;    // control
    case 0xBB:  *out = u16(0xFB); return true;  // copper ... platinum, gems
    case 0xBD:  *out = u16(0xFD); return true;
    case 0xBF:  *out = u16(0xFF); return true;
    case 0xC1:  *out = u16(0x101); return true;
    case 0xC3:  *out = u16(0x103); return true;
    case 0xC5:  *out = u16(0x105); return true;
    case 0xC9:  *out = level(5); return true;   // magic-user
    case 0xCA:  *out = level(0); return true;   // cleric
    case 0xCB:  *out = level(6); return true;   // thief
    case 0xCC:  *out = level(2); return true;   // fighter
    case 0xCF:  *out = level(3); return true;   // paladin
    case 0xD0:  *out = level(4); return true;   // ranger
    case 0xD6:  *out = r[0x119]; return true;   // sex
    case 0xD8:  *out = r[0x11B]; return true;   // alignment
    case 0xF7:  *out = u16(0x13C); return true;
    case 0xF9:  *out = r[0x13E]; return true;
    case 0x100: {                               // the status, in the scripts' numbers
        static const uint8_t kTo[9] = {1, 0, 0, 6, 5, 4, 3, 7, 2};
        *out = r[0x195] < 9 ? kTo[r[0x195]] : 0;
        return true;
    }
    case 0x10C: *out = r[0x197] == 1 ? 0x81 : r[0x197] == 0 && r[0x198] ? 0x80 : 0; return true;   // side
    case 0x119: *out = r[0x1A4]; return true;   // HP now
    case 0x11B: *out = r[0x1A5]; return true;   // movement
    case 0x2B1:
    case 0x2B4: *out = static_cast<uint16_t>(index); return true;
    case 0x2CF: return script_value(r, index, off, out);   // Charisma's percentage: as Curse's
    default:
        return false;
    }
}

void script_set_secret(Party& p, uint16_t off, uint16_t v)
{
    Character* c = p.sel();
    if (c) script_set_secret(c->rec, off, v);
}

void script_set_secret(uint8_t* r, uint16_t off, uint16_t v)
{
    if (off >= 0x1B && off <= 0x65) {           // the memorized spells
        r[0x1E + (off - 0x1B)] = static_cast<uint8_t>(v);
        return;
    }
    switch (off) {
    case 0xB8: case 0xBB: case 0xBD: case 0xBF: case 0xC1: case 0xC3: case 0xF7: case 0xF9: case 0x10C:
        script_set(r, off, v);                  // as Curse's
        break;
    case 0xC5:                                  // gems
        r[0x105] = static_cast<uint8_t>(v);
        r[0x106] = static_cast<uint8_t>(v >> 8);
        break;
    case 0x100:                                 // 1: back to okay, in the fight
        if (v == 1) {
            r[0x195] = 0;
            r[0x196] = 1;
        }
        break;
    case 0x119: r[0x1A4] = static_cast<uint8_t>(v); break;   // HP now
    default: break;
    }
}

void remove(Party& p, int i)
{
    if (i < 0 || i >= p.count) return;
    for (int k = i; k + 1 < p.count; ++k) p.m[k] = p.m[k + 1];
    p.m[p.count - 1] = Character{};
    --p.count;
    p.selected = i > 0 ? i - 1 : 0;
}

} // namespace party

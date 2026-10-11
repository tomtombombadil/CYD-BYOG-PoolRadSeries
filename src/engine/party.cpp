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

} // namespace

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

bool read_record(dax::ByteSource& src, Character& out, Format f)
{
    if (f == Format::Curse) return read_record(src, out);
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
    to_pool(c, buf);
    *n = kPoolRecordSize;
    return buf;
}

void read_items(dax::ByteSource& src, Character& out)
{
    out.n_items = 0;
    const uint32_t n = src.size() / kItemSize;
    for (uint32_t i = 0; i < n && out.n_items < kMaxItems; ++i)
        if (src.read_at(i * kItemSize, out.items[out.n_items], kItemSize) == kItemSize) ++out.n_items;
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

void remove(Party& p, int i)
{
    if (i < 0 || i >= p.count) return;
    for (int k = i; k + 1 < p.count; ++k) p.m[k] = p.m[k + 1];
    p.m[p.count - 1] = Character{};
    --p.count;
    p.selected = i > 0 ? i - 1 : 0;
}

} // namespace party

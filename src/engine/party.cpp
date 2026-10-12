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
};
// Secret offset, Ext place, length
constexpr uint16_t kSecretExt[][3] = {
    {0xD5, kExtBook, 17}, {0x137, kExtCleric67, 2}, {0x13E, kExtDruidMore, 9}, {0x14C, kExtMage67, 2},
    {0x6D, kExt6D, 1},    {0x15E, kExtFlags2, 1},   {0x15F, kExt15F, 1},       {0x1A5, kExtMagicRes, 1},
    {0x6B, kExtRace, 1},
};

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

bool read_record(dax::ByteSource& src, Character& out, Format f)
{
    if (f == Format::Curse) return read_record(src, out);
    if (f == Format::Secret) {
        uint8_t sec[kSecretRecordSize];
        out = Character{};
        if (src.read_at(0, sec, kSecretRecordSize) != kSecretRecordSize) return false;
        from_secret(sec, out);
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
    if (f == Format::Secret) {
        to_secret(c, buf);
        *n = kSecretRecordSize;
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

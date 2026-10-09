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
bool script_value(const Party& p, uint16_t off, uint16_t* out)
{
    const Character* c = p.sel();
    if (!c) return false;
    const uint8_t* r = c->rec;
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
    case 0xBD:  *out = u16(0xFF); return true;  // electrum
    case 0xBF:  *out = u16(0xFD); return true;  // silver
    case 0xC1:  *out = u16(0x101); return true; // gold
    case 0xC3:  *out = u16(0x103); return true; // platinum
    case 0xC9:  *out = r[0x10E]; return true;   // magic-user level
    case 0xD6:  *out = r[0x119]; return true;   // sex
    case 0xD8:  *out = r[0x11B]; return true;   // alignment
    case 0xE4:  *out = r[0x192] & 1; return true;
    case 0xF7:  *out = u16(0x13C); return true;
    case 0xF9:  *out = r[0x13E]; return true;
    case 0x100: *out = c->in_combat() ? 1 : 0x80; return true;
    case 0x11B: *out = r[0x1A5]; return true;   // movement
    case 0x2B1:
    case 0x2B4: *out = static_cast<uint16_t>(p.selected); return true;
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

} // namespace party

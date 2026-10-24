#include "items.h"

#include <cstdio>
#include <cstring>

namespace items {

void Names::set_words(const uint8_t* table, int count, int stride)
{
    used_ = 1;
    memset(off_, 0, sizeof off_);
    for (int i = 0; i < count && i + 1 < kWords; ++i) {
        const uint8_t* e = table + i * stride;
        size_t n = e[0];
        if (n >= static_cast<size_t>(stride)) n = stride - 1;
        if (n == 0) continue;
        if (used_ + n + 1 > sizeof text_) break;
        off_[i + 1] = static_cast<uint16_t>(used_);
        memcpy(text_ + used_, e + 1, n);
        text_[used_ + n] = 0;
        used_ += n + 1;
    }
}

bool Names::read_types(dax::ByteSource& src, uint32_t head, uint32_t stride)
{
    uint8_t rec[16];
    for (int t = 0; t < kTypes; ++t) {
        if (src.read_at(head + t * stride, rec, sizeof rec) != sizeof rec) return t > 0;
        TypeInfo& ti = types_[t];
        ti.slot = rec[0];
        ti.hands = rec[1];
        ti.dice_large = rec[2];
        ti.sides_large = rec[3];
        ti.bonus_large = static_cast<int8_t>(rec[4]);
        ti.attacks = rec[5];
        ti.ac = rec[6];
        ti.blow = rec[7];
        ti.dice = rec[9];
        ti.sides = rec[10];
        ti.bonus = static_cast<int8_t>(rec[11]);
        ti.range = rec[12];
        ti.classes = rec[13];
        ti.flags = rec[14];
    }
    return true;
}

void Names::name(const Item& it, char* out, size_t cap, bool all_words) const
{
    if (!cap) return;
    size_t o = 0;
    auto add = [&](const char* s) {
        while (*s && o + 1 < cap) out[o++] = *s++;
        out[o] = 0;
    };
    out[0] = 0;
    if (detect && (static_cast<int8_t>(it.r[0x32]) > 0 || static_cast<int8_t>(it.r[0x33]) > 0 || it.r[0x36]))
        add("* ");
    if (stored_names) {
        const int n = it.r[0] < 0x2D ? it.r[0] : 0x2D;
        for (int i = 0; i < n && o + 1 < cap; ++i) out[o++] = static_cast<char>(it.r[1 + i]);
        out[o] = 0;
        return;
    }
    if (it.count() > 0) {
        char n[8];
        snprintf(n, sizeof n, "%d ", it.count());
        add(n);
    }
    const int hide = all_words ? 0 : it.hidden();
    int shown = 0;
    if (it.word(1) && !(hide & 4)) shown |= 1;
    if (it.word(2) && !(hide & 2)) shown |= 2;
    if (it.word(3) && !(hide & 1)) shown |= 4;
    const int t = it.type();
    const bool missile = t == plural_.arrow || t == plural_.quarrel || t == plural_.dart;
    bool plural = false;
    bool first = true;
    for (int i = 3; i >= 1; --i) {
        if (!(shown & (1 << (i - 1)))) continue;
        if (!first) add(" ");
        first = false;
        add(word(it.word(i)));
        if (it.count() < 2 || plural) continue;
        if (plural_first) {
            add("s");
            plural = true;
            continue;
        }
        if ((1 << (i - 1)) == shown || (i == 1 && shown > 4 && t != plural_.flask) || (i == 2 && !(shown & 1)) ||
            (i == 3 && t == plural_.flask) || (missile && it.word(3) != plural_.keep1 && it.word(3) != plural_.keep2)) {
            add("s");
            plural = true;
        }
    }
    // No trailing space (the games trim it)
    while (o > 0 && out[o - 1] == ' ') out[--o] = 0;
}

int readied_in(const uint8_t (*recs)[kRecordSize], int n, const Names& names, uint8_t slot)
{
    for (int i = 0; i < n; ++i) {
        const Item it{recs[i]};
        if (it.readied() && names.type(it.type()).slot == slot) return i;
    }
    return -1;
}

// ---- Passive codes --------------------------------------------------------

const Passive kCursePassive[kPassiveCodes] = {
    Passive::Effect, Passive::Wizardry, Passive::Dexterity, Passive::None,
    Passive::Alignment, Passive::GiantGirdle, Passive::Dwarves, Passive::None,
    Passive::Ioun, Passive::HammerOff, Passive::Fumbling, Passive::Thievery,
    Passive::Stupidity, Passive::Powerless, Passive::None, Passive::None,
};
// (Pool's handlers 0x80-0x8B, poolrad_combat_facts.md 2.2; its items:
// 0x81 Ring of Fire Resistance, 0x83 Gauntlets of Ogre Power, 0x84 the
// keyed long swords, 0x85 Cloak of Displacement, 0x86 Cloak of Elvenkind,
// 0x87 the boulder, 0x88 the sword +3 against undead - GBC's item list)
const Passive kPoolPassive[kPassiveCodes] = {
    Passive::Effect, Passive::Effect, Passive::Effect, Passive::OgrePower,
    Passive::Alignment, Passive::Effect, Passive::Effect, Passive::NeedGiant,
    Passive::Effect, Passive::HammerOff, Passive::Effect, Passive::Effect,
    Passive::None, Passive::None, Passive::None, Passive::None,
};

// Secret's (its ready routine GAME.OVR 0x28B84 and the stats routine
// 0x374A8): Curse's codes, but 3 is Strength 18/00 in the stats (its
// Gauntlets of Ogre Power, 0x83), and its ring of wizardry (1) doubles the
// 5th level's spells (classes::spell_slots); 4 (keyed to an alignment) does
// nothing in its ready routine
const Passive kSecretPassive[kPassiveCodes] = {
    Passive::Effect, Passive::Wizardry, Passive::Dexterity, Passive::Ogre1800,
    Passive::None, Passive::GiantGirdle, Passive::Dwarves, Passive::None,
    Passive::Ioun, Passive::HammerOff, Passive::Fumbling, Passive::Thievery,
    Passive::Stupidity, Passive::Powerless, Passive::None, Passive::None,
};

namespace {
const Passive* g_passive = kCursePassive;
}

void set_passive(const Passive* codes) { g_passive = codes ? codes : kCursePassive; }

namespace {
const FlameFix* g_flame = nullptr;
bool g_flame_on = false;
}

void set_flame_fix(const FlameFix* f, bool on)
{
    g_flame = f && f->type ? f : nullptr;
    g_flame_on = on;
}

bool flame_fix_on() { return g_flame && g_flame_on; }

bool fix_item(uint8_t* r, bool original)
{
    const FlameFix* f = g_flame;
    if (!f || r[0x2E] != f->type || r[0x2F] != f->word) return false;
    const bool is_orig = r[0x30] == f->plus_word && r[0x32] == f->plus && r[0x3D] == f->spell && r[0x3E] == 0;
    const bool is_fixed = r[0x30] == f->plus1_word && r[0x32] == 1 && r[0x3D] == f->effect && r[0x3E] == f->code;
    const bool want_fixed = g_flame_on && !original;
    if (want_fixed && is_orig) {
        r[0x30] = f->plus1_word;            // (the name: "+1")
        r[0x32] = 1;
        r[0x3D] = f->effect;
        r[0x3E] = f->code;
        return true;
    }
    if (!want_fixed && is_fixed) {
        r[0x30] = f->plus_word;
        r[0x32] = f->plus;
        r[0x3D] = f->spell;
        r[0x3E] = 0;
        return true;
    }
    return false;
}

Passive passive(const uint8_t* item)
{
    if (item[0x3E] < 0x80) return Passive::None;
    const int code = item[0x3E] & 0x7F;
    return code < kPassiveCodes ? g_passive[code] : Passive::None;
}

} // namespace items

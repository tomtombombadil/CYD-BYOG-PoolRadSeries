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

bool Names::read_types(dax::ByteSource& src)
{
    uint8_t rec[16];
    for (int t = 0; t < kTypes; ++t) {
        if (src.read_at(2 + t * 16u, rec, sizeof rec) != sizeof rec) return t > 0;
        TypeInfo& ti = types_[t];
        ti.slot = rec[0];
        ti.hands = rec[1];
        ti.dice_large = rec[2];
        ti.sides_large = rec[3];
        ti.bonus_large = static_cast<int8_t>(rec[4]);
        ti.attacks = rec[5];
        ti.ac = rec[6];
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

} // namespace items

#include "treasure.h"

#include <cstring>

namespace treasure {

uint16_t ByType::of(int t) const
{
    for (int i = 0; i < n; ++i)
        if (type[i] == t) return value[i];
    return other;
}

namespace {

void set16(uint8_t* r, int at, int v)
{
    r[at] = static_cast<uint8_t>(v);
    r[at + 1] = static_cast<uint8_t>(v >> 8);
}

// A ready-made row: its words, weight, value and effects; plus 1, 1 vs saves
void from_row(uint8_t* r, const uint16_t* row)
{
    r[0x2F] = static_cast<uint8_t>(row[0]);
    r[0x30] = static_cast<uint8_t>(row[1]);
    r[0x31] = static_cast<uint8_t>(row[2]);
    r[0x32] = 1;
    r[0x33] = 1;
    set16(r, 0x37, row[3]);
    r[0x39] = 0;
    set16(r, 0x3A, row[4]);
    r[0x3C] = static_cast<uint8_t>(row[5]);
    r[0x3D] = static_cast<uint8_t>(row[6]);
    r[0x3E] = static_cast<uint8_t>(row[7]);
}

// The item made from its type
void create(uint8_t* r, int type, const Facts& f, const Rows& rows, create::Dice& d)
{
    memset(r, 0, items::kRecordSize);
    r[0x2E] = static_cast<uint8_t>(type);
    r[0x35] = 6;                                   // words 1 and 2 hidden until identified
    if (type == f.mu_scroll || type == f.cleric_scroll) {
        const bool mu = type == f.mu_scroll;
        const int n = d.roll(3, 1);
        r[0x31] = mu ? f.mu_word : f.cleric_word;
        r[0x30] = static_cast<uint8_t>(f.with_word + n);
        r[0x32] = 1;
        set16(r, 0x37, f.scroll_weight);
        int value = 0;
        for (int i = 0; i < n; ++i) {
            const int band = d.roll(f.bands ? f.bands : 5, 1);
            const uint8_t* b = mu ? f.mu_band[band - 1] : f.cleric_band[band - 1];
            r[0x3C + i] = static_cast<uint8_t>(d.roll(b[0], 1) + b[1]);
            value += f.scroll_value * band;
        }
        set16(r, 0x3A, value);
        return;
    }
    if (f.picks) {
        // Pool: the row its type picks (by a die for some; none: a nameless item)
        for (int k = 0; k < f.n_picks; ++k) {
            const Facts::Pick& p = f.picks[k];
            if (p.type != type) continue;
            const int i = p.sides ? d.roll(p.sides, 1) + p.add - 1 : 0;
            const int start = i >= 0 && i < 11 ? p.start[i] : 0;
            if (start && start + 7 < kRowWords) {
                from_row(r, &rows.w[start]);
                if (start == f.plus2_start) r[0x32] = r[0x33] = 2;
            }
            return;
        }
    } else {
        int row = -1;
        if (type == f.potion) row = d.roll(8, 1) <= 5 ? 2 : 0;
        else if (type == f.giant) row = 1;
        else if (type == f.wand) row = 4;
        if (row >= 0) {
            from_row(r, rows.row(row));
            return;
        }
    }
    // Weapons, armour, arrows, bracers, rings: +1 (d20 1-14) or +2 (Pool: 20 +3)
    const int p20 = d.roll(20, 1);
    int plus = p20 <= 14 ? 1 : f.plus3 && p20 == 20 ? 3 : 2;
    const uint8_t pw = static_cast<uint8_t>(f.plus_word + plus);
    if (type == f.leather || type == f.padded) {
        r[0x31] = static_cast<uint8_t>(type);
        r[0x30] = f.armor_word;
        r[0x2F] = pw;
        r[0x35] = 4;
    } else if (type == f.studded) {
        r[0x31] = static_cast<uint8_t>(type);
        r[0x30] = f.leather_word;
        r[0x2F] = pw;
        r[0x35] = 4;
    } else if (type >= f.ring_mail && type <= f.plate) {
        r[0x31] = static_cast<uint8_t>(type);
        r[0x30] = f.mail_word;
        r[0x2F] = pw;
        r[0x35] = 4;
    } else if (type == f.arrow) {
        r[0x31] = f.arrow_word;
        r[0x30] = pw;
    } else if (type == f.bracers) {
        plus = plus * 2 + 2;                       // AC 6 or AC 4
        r[0x31] = f.bracers_word;
        r[0x30] = f.of_word;
        r[0x2F] = plus == 4 ? f.ac6_word : plus == 6 ? f.ac4_word : f.ac2_word;
    } else if (type == f.ring) {
        r[0x31] = f.ring_word;
        r[0x30] = f.of_prot_word;
        r[0x2F] = pw;
    } else {
        r[0x31] = static_cast<uint8_t>(type);      // the type's own word
        r[0x30] = pw;
    }
    r[0x32] = static_cast<uint8_t>(plus);
    set16(r, 0x37, f.weight.of(type));
    if (type == f.bundle10[0] || type == f.bundle10[1]) r[0x39] = 10;
    else if (type == f.dart) r[0x39] = 5;
    set16(r, 0x3A, plus * f.value.of(type));
    // A javelin of lightning (Pool's first word 0x90 isn't a row's start:
    // the original reads across two rows)
    if (type == f.javelin && d.roll(5, 1) == 5) from_row(r, f.javelin_start ? &rows.w[f.javelin_start] : rows.row(6));
}

int secret_roll(const SecretFacts& s, int t, create::Dice& d)
{
    for (int depth = 0; depth < 4 && t < s.n_tables; ++depth) {
        const RollTable& rt = s.tables[t];
        const int k = d.roll(rt.die, 1);
        int pick = -1;
        for (int i = 0; i < rt.n && pick < 0; ++i)
            if (k <= rt.r[i].hi) pick = i;
        if (pick < 0) return 0;
        if (rt.r[pick].type) return rt.r[pick].type;
        t = rt.r[pick].sub;
    }
    return 0;
}

void secret_create(uint8_t* r, int type, const SecretFacts& s, const Rows& rows, create::Dice& d)
{
    memset(r, 0, items::kRecordSize);
    r[0x2E] = static_cast<uint8_t>(type);
    r[0x35] = 6;
    int row = -1;
    if ((type >= s.plus_lo && type <= s.plus_hi) || type == s.plus_more[0] || type == s.plus_more[1] ||
        type == s.plus_more[2] || type == s.plus_more[3]) {
        const int p = d.roll(s.plus_die, 1);
        int plus = p <= s.plus_band[0] ? 1 : p <= s.plus_band[1] ? 2 : 3;
        if (type == s.javelin && d.roll(5, 1) == 5) row = s.javelin_row;
        const NameRule* nr = nullptr;
        for (int k = 0; k < s.n_names; ++k)
            if (s.names[k].type == type) nr = &s.names[k];
        if (nr && nr->plus) plus = nr->plus;
        const uint8_t pw = static_cast<uint8_t>(s.plus_word + plus);
        if (nr) {
            r[0x31] = nr->w3 ? nr->w3 : static_cast<uint8_t>(type);
            r[0x30] = nr->w2 ? nr->w2 : pw;
            r[0x2F] = nr->w1 ? nr->w1 : nr->w2 ? pw : 0;
            r[0x35] = nr->hidden;
            r[0x33] = nr->saves;
        } else {
            r[0x31] = static_cast<uint8_t>(type);
            r[0x30] = pw;
        }
        r[0x32] = static_cast<uint8_t>(plus);
        const uint16_t w = s.weight.of(type);
        set16(r, 0x37, w);
        if (w == s.weight.other || type == s.twenty[0]) r[0x39] = 20;
        set16(r, 0x3A, plus * s.value.of(type));
    } else if (type == s.mu_scroll || type == s.cleric_scroll) {
        const bool mu = type == s.mu_scroll;
        const int n = d.roll(s.spells_die, 1);
        r[0x2F] = static_cast<uint8_t>(s.count_word + n);
        r[0x30] = s.scroll_word;
        r[0x31] = mu ? s.mu_word : s.cleric_word;
        r[0x32] = 1;
        set16(r, 0x37, s.scroll_weight);
        int value = 0;
        for (int i = 0; i < n; ++i) {
            if (mu) r[0x35] = s.mu_hidden;
            else r[0x35] = s.cleric_hidden;
            const int lv = d.roll(mu ? s.mu_levels : s.cleric_levels, 1);
            const int k = d.roll((mu ? s.mu_dice : s.cleric_dice)[lv - 1], 1);
            r[0x3C + i] = k < 11 ? rows.spells[mu ? 0 : 1][lv - 1][k]
                                 : lv < 7 ? rows.spells[mu ? 0 : 1][lv][0] : 0;     // (11: the next level's [0])
            value += s.scroll_value * lv;
        }
        set16(r, 0x3A, value);
        return;
    } else if (type == s.potion) {
        row = s.potion_rows[d.roll(8, 1) - 1];
    } else if (type == s.wand) {
        row = s.wand_row;
    } else if (type == s.giant) {
        row = s.giant_row;
    }
    if (row >= 0) {
        from_row(r, rows.row(row));
        r[0x33] = 0;                               // (Secret's: nothing against saves)
    }
}

} // namespace

void make(uint8_t* rec, const Facts& f, const Rows& rows, create::Dice& d, uint8_t* last)
{
    if (f.secret) {
        const int type = secret_roll(*f.secret, 0, d);
        if (last) *last = static_cast<uint8_t>(type);
        secret_create(rec, type, *f.secret, rows, d);
        return;
    }
    const int k = d.roll(100, 1);
    int type;
    if (k <= 60) {
        const int j = d.roll(100, 1);
        if ((j <= 47 || (j >= 50 && j <= 59)) && j != f.heavy_crossbow) {
            type = j;
        } else if (j >= 60 && j <= 90) {
            const int s = d.roll(10, 1);
            type = f.swords[s <= 4 ? 0 : s <= 7 ? 1 : s - 6];
        } else if (j >= 91 && j <= 94) {
            type = f.arrow;
        } else if (j >= 95 && j <= 97) {
            type = f.ring;
        } else if (j >= 98) {
            type = f.bracers;
        } else if (j != f.heavy_crossbow && f.keep_type && last && *last) {
            type = *last;                          // (Pool's 48 / 49: its type left as it was)
        } else {
            type = f.shield;                       // 48, 49, and the heavy crossbow
        }
    } else if (k <= 85) {
        type = f.mu_scroll;
    } else if (k <= 92) {
        type = f.cleric_scroll;
    } else if (k <= 98) {
        if (f.misc_die) {
            type = f.misc[d.roll(f.misc_die, 1) - 1];
        } else {
            const int p = d.roll(15, 1);
            type = p <= 9 ? f.potion : p == 10 ? f.giant : f.wand;
        }
    } else {
        type = f.last ? f.last : f.shield;
    }
    if (last) *last = static_cast<uint8_t>(type);
    create(rec, type, f, rows, d);
}

} // namespace treasure

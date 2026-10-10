#include "rules.h"

#include <cstring>

namespace rules {

namespace {

// Curse record offsets (party.h lists the rest)
constexpr int kStrFull = 0x11, kDexFull = 0x17, kStr00 = 0x1C;
constexpr int kThac0 = 0x73, kAttackLevel = 0xDD, kBaseMove = 0xE4, kMultiLevel = 0xE6;
constexpr int kDiceBase = 0x11E, kSidesBase = 0x120, kBonusBase = 0x122;
constexpr int kBaseAc = 0x124, kUsesStrength = 0x125;
constexpr int kHands = 0x185, kSaveBonus = 0x186, kWeight = 0x187;
constexpr int kHitBonus = 0x199, kAc = 0x19A, kAcBehind = 0x19B;
constexpr int kDice = 0x19E, kSides = 0x1A0, kBonus = 0x1A2, kMove = 0x1A5;

int u16(const uint8_t* r, int o) { return r[o] | r[o + 1] << 8; }
void put16(uint8_t* r, int o, int v)
{
    r[o] = static_cast<uint8_t>(v);
    r[o + 1] = static_cast<uint8_t>(v >> 8);
}

int str_hit(const party::Character& c)
{
    if (!c.rec[kUsesStrength]) return 0;
    const int s = strength_group(c);
    if (s >= 1 && s <= 3) return -3;
    if (s == 4 || s == 5) return -2;
    if (s == 6 || s == 7) return -1;
    if (s >= 17 && s <= 19) return 1;
    if (s >= 20 && s <= 22) return 2;
    if (s >= 23 && s <= 25) return 3;
    if (s == 26 || s == 27) return 4;
    if (s >= 28 && s <= 30) return s - 23;
    return 0;
}

int str_damage(const party::Character& c)
{
    if (!c.rec[kUsesStrength]) return 0;
    const int s = strength_group(c);
    if (s == 1 || s == 2) return -2;
    if (s >= 3 && s <= 5) return -1;
    if (s == 16) return 1;
    if (s >= 17 && s <= 19) return s - 16;
    if (s >= 20 && s <= 29) return s - 17;
    if (s == 30) return 14;
    return 0;
}

int dex_missile(const party::Character& c)
{
    const int d = c.rec[kDexFull];
    if (d <= 2) return -4;
    if (d <= 5) return d - 6;
    if (d >= 16 && d <= 18) return d - 15;
    if (d == 19 || d == 20) return 3;
    if (d >= 21 && d <= 23) return 4;
    if (d == 24 || d == 25) return 5;
    return 0;
}

// The fighter level that counts (a human who changed class adds the old
// one once the new class has passed it)
int fighter_level(const party::Character& c)
{
    int lv = c.level(2);
    if (c.race() == 7) {
        int k = 0;
        while (k < 7 && c.level(k) == 0) ++k;
        if (c.level(k) > c.rec[kMultiLevel]) lv += c.old_level(2);
    }
    return lv;
}

constexpr int kPerCopper[5] = {1, 10, 100, 200, 1000};

} // namespace

int strength_group(const party::Character& c)
{
    const int s = c.rec[kStrFull];
    if (s <= 17) return s;
    if (s == 18) {
        const int e = c.rec[kStr00];
        if (e == 0) return 18;
        if (e <= 50) return 19;
        if (e <= 75) return 20;
        if (e <= 90) return 21;
        if (e <= 99) return 22;
        return 23;
    }
    if (s <= 25) return s + 5;
    return 0;
}

int max_encumbrance(const party::Character& c)
{
    const int s = strength_group(c);
    if (s >= 1 && s <= 3) return -350;
    if (s == 4 || s == 5) return -250;
    if (s == 6 || s == 7) return -150;
    if (s == 12 || s == 13) return 100;
    if (s == 14 || s == 15) return 200;
    if (s == 16) return 350;
    if (s >= 17 && s <= 21) return (s - 17) * 250 + 500;
    if (s >= 22 && s <= 26) return (s - 22) * 1000 + 2000;
    if (s == 27) return 7500;
    if (s >= 28 && s <= 30) return (s - 28) * 3000 + 9000;
    return 0;
}

int dex_ac_bonus(const party::Character& c)
{
    const int d = c.rec[kDexFull];
    if (d >= 1 && d <= 3) return -4;
    if (d >= 4 && d <= 6) return d - 7;
    if (d >= 15 && d <= 18) return d - 14;
    if (d == 19 || d == 20) return 4;
    if (d >= 21 && d <= 23) return 5;
    if (d == 24 || d == 25) return 6;
    return 0;
}

namespace {

int find_effect(const party::Character& c, uint8_t type)
{
    if (!type) return -1;
    for (int i = 0; i < c.n_affects; ++i)
        if (c.affects[i][0] == type) return i;
    return -1;
}

// An effect's strength: data 101 or less = 18/(data - 1), else data - 100
void decode_strength(int data, int* str, int* str00)
{
    data &= 0x7F;
    if (data <= 101) {
        *str00 = data - 1;
        *str = 18;
    } else {
        *str = data - 100;
        *str00 = 0;
    }
}

void max_strength(int* a, int* a00, int b, int b00)
{
    if (b > *a || (b == 18 && b00 > *a00)) {
        *a = b;
        *a00 = b00;
    }
}

} // namespace

void stats(party::Character& c, const ItemFacts& f)
{
    uint8_t* r = c.rec;
    for (int s = 0; s < 6; ++s) {
        const int own = r[0x10 + s * 2];
        int v = own, set = 0xFF;
        int str00 = r[0x1D];
        for (int i = 0; i < c.n_items; ++i) {
            const uint8_t* it = c.items[i];
            if (it[0x3E] <= 0x80 || !it[0x34]) continue;
            const int code = it[0x3E] & 0x7F, a2 = it[0x3D];
            switch (s) {
            case 0: {
                int b = 0, b00 = 0;
                if (code == 5 && a2 <= 6) {
                    b = a2 == 0 ? 18 : 18 + a2;
                    b00 = a2 == 0 ? 100 : 0;
                } else if (code == 8 && own < 18 && a2 == 0) {
                    b = own + 1;
                } else if (code == 13) {
                    set = 3;
                }
                max_strength(&v, &str00, b, b00);
                break;
            }
            case 1:
                if (code == 8 && own < 18 && a2 == 1) ++v;
                else if (code == 12) set = 7;
                else if (code == 13) set = 3;
                break;
            case 2:
                if (code == 8 && own < 18 && a2 == 2) ++v;
                break;
            case 3:
                if (code == 2) v += own <= 6 ? 4 : own <= 13 ? 2 : 1;
                else if (code == 8 && own < 18 && a2 == 3) ++v;
                else if (code == 10) v -= 2;
                break;
            case 4:
                if (code == 6) ++v;
                else if (code == 8 && own < 18 && a2 == 4) ++v;
                break;
            case 5:
                if (code == 6) --v;
                else if (code == 8 && own < 18 && a2 == 5) ++v;
                break;
            }
        }
        if (s == 0) {
            int k = find_effect(c, f.strength_fx);
            if (k >= 0) {
                int b, b00;
                decode_strength(c.affects[k][3], &b, &b00);
                if (v <= 18 && str00 < 100) {
                    b += v;
                    if (b > 18) {
                        const bool fighter = r[0x10B] || r[0x113] || r[0x10C] || r[0x114] || r[0x10D] || r[0x115];
                        if (fighter) {
                            b00 = r[0x1C] + (b - 18) * 10;
                            if (b00 > 100) b00 = 100;
                        }
                        b = 18;
                    }
                }
                max_strength(&v, &str00, b, b00);
            }
            const uint8_t more[2] = {f.giant_fx, f.enlarge_fx};
            for (uint8_t t : more) {
                k = find_effect(c, t);
                if (k < 0) continue;
                int b, b00;
                decode_strength(c.affects[k][3], &b, &b00);
                max_strength(&v, &str00, b, b00);
            }
            if (set != 0xFF) {
                r[0x11] = static_cast<uint8_t>(set);
                r[0x1C] = 0;
            } else {
                r[0x11] = static_cast<uint8_t>(v < 0 ? 0 : v);
                r[0x1C] = static_cast<uint8_t>(str00);
            }
            continue;
        }
        if ((s == 1 || s == 2) && find_effect(c, f.feeble_fx) >= 0) set = 3;
        if (s == 5) {
            const int k = find_effect(c, f.friends_fx);
            if (k >= 0) v += c.affects[k][3];        // (the original adds; coab sets it)
        }
        if (v < 0) v = 0;
        r[0x11 + s * 2] = static_cast<uint8_t>(set != 0xFF ? set : v);
    }
}

void recalc(party::Character& c, const items::Names& names, const ItemFacts& f)
{
    uint8_t* r = c.rec;
    stats(c, f);
    // Readied items by slot (0-8; slot 9 holds two), missiles
    const uint8_t* slot[9] = {};
    const uint8_t* rings[2] = {};
    const uint8_t* arrows = nullptr;
    const uint8_t* quarrels = nullptr;
    int weight = 0, hands = 0;
    for (int i = 0; i < c.n_items; ++i) {
        const items::Item it{c.items[i]};
        int w = it.weight();
        if (it.count() > 0) w *= it.count();
        weight += w;
        if (!it.readied()) continue;
        const items::TypeInfo& t = names.type(it.type());
        if (t.slot <= 8) slot[t.slot] = c.items[i];
        else if (t.slot == 9) {
            if (!rings[0]) rings[0] = c.items[i];
            else if (!rings[1]) rings[1] = c.items[i];
        }
        if (it.type() == f.arrow) arrows = c.items[i];
        if (it.type() == f.quarrel) quarrels = c.items[i];
        hands += t.hands;
    }
    for (int m = 0; m < 7; ++m) weight += c.money(m);
    put16(r, kWeight, weight);
    r[kHands] = static_cast<uint8_t>(hands);

    r[kDice] = r[kDiceBase];
    r[kDice + 1] = r[kDiceBase + 1];
    r[kSides] = r[kSidesBase];
    r[kSides + 1] = r[kSidesBase + 1];
    int dmg = static_cast<int8_t>(r[kBonusBase]);
    r[kBonus + 1] = r[kBonusBase + 1];

    int bonus[5] = {};
    bool magic_armour = false;
    r[kSaveBonus] = 0;
    int ac = r[kBaseAc];
    int move = r[kBaseMove];
    int hit = static_cast<int8_t>(r[kThac0]);
    bonus[0] = dex_ac_bonus(c);

    const uint8_t* weapon = slot[items::kSlotWeapon];
    if (!weapon) {
        hit += str_hit(c);
        dmg += str_damage(c);
    } else {
        // The weapon's to-hit and damage
        const items::Item it{weapon};
        const items::TypeInfo& t = names.type(it.type());
        hit = static_cast<int8_t>(r[kThac0]);
        if (t.flags & 0x02) hit += dex_missile(c);
        dmg = t.bonus;
        if (t.flags & 0x04) {
            hit += str_hit(c);
            dmg += str_damage(c);
        }
        int plus = it.plus();
        if ((t.flags & 0x80) && quarrels) plus += items::Item{quarrels}.plus();
        if ((t.flags & 0x01) && arrows) plus += items::Item{arrows}.plus();
        dmg += plus;
        if (c.race() == 2)
            for (uint8_t e : f.elf_bonus)
                if (e && it.type() == e) {
                    ++plus;
                    break;
                }
        hit += plus;
        r[kDice] = t.dice;
        r[kSides] = t.sides;
    }

    for (int i = 0; i < c.n_items; ++i) {
        const items::Item it{c.items[i]};
        if (!it.readied()) continue;
        const items::TypeInfo& t = names.type(it.type());
        // Armour's weight slows the wearer
        if (t.slot == items::kSlotArmour) {
            const int w = it.weight();
            move = w <= 150 ? r[kBaseMove] : w <= 399 ? 9 : 6;
            if (move != 0 && move <= 9) move += 3;
        }
        // AC from armour, shields, rings and the like
        if (t.ac > 0x7F) {
            const int v = t.ac & 0x7F;
            if (t.slot == 1) {
                bonus[1] = it.plus() + v;
            } else if (v == 0) {
                if (t.slot == 9) {
                    if (it.plus() > bonus[3]) bonus[3] = it.plus();
                } else {
                    bonus[2] += it.plus();
                }
                r[kSaveBonus] = static_cast<uint8_t>(r[kSaveBonus] + it.r[0x33]);
            } else if (it.plus() + v > bonus[4]) {
                bonus[4] = it.plus() + v;
                if (it.plus() > 0 && t.slot == items::kSlotArmour) magic_armour = true;
            }
        }
    }
    if (magic_armour) bonus[3] = 0;

    // Carrying too much slows down too
    int over = weight - max_encumbrance(c);
    if (over < 0) over = 0;
    const int can = over <= 0x200 ? move : over <= 0x300 ? 9 : over <= 0x400 ? 6 : 3;
    if (can < move) move = can;

    if (bonus[4] < ac) bonus[4] = ac;
    ac = 0;
    for (int b : bonus) ac += b;
    r[kAc] = static_cast<uint8_t>(ac);
    r[kAcBehind] = static_cast<uint8_t>(bonus[4] + bonus[2] + bonus[3] - 2);
    r[kMove] = static_cast<uint8_t>(move);
    r[kHitBonus] = static_cast<uint8_t>(hit);
    r[kBonus] = static_cast<uint8_t>(dmg);
    const int fl = fighter_level(c);
    r[kAttackLevel] = static_cast<uint8_t>(fl > 0 && c.race() > 0 ? fl : 1);
}

int gold_worth(const int money[7])
{
    long copper = 0;
    for (int m = 0; m < 5; ++m) copper += static_cast<long>(money[m]) * kPerCopper[m];
    return static_cast<int>(copper / kPerCopper[3]);
}

int gold_worth(const party::Character& c)
{
    int m[7];
    for (int i = 0; i < 7; ++i) m[i] = c.money(i);
    return gold_worth(m);
}

void pay(int money[7], int gold)
{
    // From the smallest coins up, over-paying with each; then the change
    // back in the largest coins
    long copper = static_cast<long>(gold) * kPerCopper[3];
    for (int m = 0; m < 5 && copper > 0; ++m) {
        long n = copper / kPerCopper[m] + 1;
        if (money[m] < n) n = money[m];
        copper -= n * kPerCopper[m];
        money[m] -= static_cast<int>(n);
    }
    if (copper < 0) {
        copper = -copper;
        for (int m = 4; m >= 0 && copper > 0; --m) {
            const long n = copper / kPerCopper[m];
            copper -= n * kPerCopper[m];
            money[m] += static_cast<int>(n);
        }
    }
}

void pay(party::Character& c, int gold)
{
    int m[7];
    for (int i = 0; i < 7; ++i) m[i] = c.money(i);
    pay(m, gold);
    for (int i = 0; i < 7; ++i) put16(c.rec, 0xFB + i * 2, m[i] < 0 ? 0 : m[i]);
}

int max_load(const party::Character& c) { return 1500 + max_encumbrance(c); }

namespace {
void add_coins(party::Character& c, int coin, int n)
{
    put16(c.rec, 0xFB + coin * 2, c.money(coin) + n);
    put16(c.rec, kWeight, u16(c.rec, kWeight) + n);
}
} // namespace

void pool(party::Party& p, int money[7])
{
    for (int i = 0; i < p.count; ++i) {
        party::Character& c = p.m[i];
        if (c.npc()) continue;
        for (int m = 0; m < 7; ++m) {
            money[m] += c.money(m);
            add_coins(c, m, -c.money(m));
        }
    }
}

void share(party::Party& p, int money[7])
{
    int pcs = 0;
    for (int i = 0; i < p.count; ++i)
        if (!p.m[i].npc()) ++pcs;
    if (!pcs) return;
    int each[7], rest[7];
    for (int m = 0; m < 7; ++m) {
        each[m] = money[m] > 0 ? money[m] / pcs : 0;
        rest[m] = money[m] > 0 ? money[m] % pcs : 0;
    }
    for (int i = 0; i < p.count; ++i) {
        party::Character& c = p.m[i];
        if (c.npc()) continue;
        for (int m = 6; m >= 0; --m) {
            const int room = max_load(c) - c.encumbrance();
            if (c.encumbrance() + each[m] <= max_load(c)) {
                add_coins(c, m, each[m]);
                if (rest[m] > 0 && c.encumbrance() + 1 <= max_load(c)) {
                    add_coins(c, m, 1);
                    --rest[m];
                }
            } else {
                add_coins(c, m, room);
                rest[m] += each[m] - room;
            }
        }
    }
    for (int m = 6; m >= 0; --m) {
        for (int i = 0; i < p.count && rest[m] > 0; ++i) {
            party::Character& c = p.m[i];
            const int room = max_load(c) - c.encumbrance();
            if (room <= 0) continue;
            const int n = rest[m] > room ? room : rest[m];
            add_coins(c, m, n);
            rest[m] -= n;
        }
    }
    for (int m = 0; m < 7; ++m) money[m] = rest[m];
}

bool too_heavy(party::Character& c, const uint8_t* item, const items::Names& names, const ItemFacts& f)
{
    recalc(c, names, f);
    if (c.n_items >= party::kMaxItems) return true;
    const items::Item it{item};
    int w = it.weight();
    if (it.count() > 0) w *= it.count();
    return u16(c.rec, kWeight) + w > max_encumbrance(c) + 1500;
}

bool add_item(party::Character& c, const uint8_t* item)
{
    if (c.n_items >= party::kMaxItems) return false;
    memcpy(c.items[c.n_items++], item, items::kRecordSize);
    return true;
}

int price(const uint8_t* item, int factor)
{
    int v = items::Item{item}.value();
    if (v == 0) v = 1;
    switch (factor) {
    case 0x01: return v >> 4;
    case 0x02: return v >> 3;
    case 0x04: return v >> 2;
    case 0x08: return v >> 1;
    case 0x20: return v << 1;
    case 0x40: return v << 2;
    case 0x80: return v << 3;
    default: return v;
    }
}

bool worn(party::Character& c, int i, bool on)
{
    if (i < 0 || i >= c.n_items) return true;
    uint8_t* it = c.items[i];
    if (it[0x3E] < 0x80) return true;
    const int code = it[0x3E] & 0x7F, v = it[0x3D];
    if (code == 0 && v) {
        int k = -1;
        for (int j = 0; j < c.n_affects; ++j)
            if (c.affects[j][0] == v) k = j;
        if (on && k < 0 && c.n_affects < party::kMaxAffects) {
            uint8_t* a = c.affects[c.n_affects++];
            memset(a, 0, party::kAffectSize);
            a[0] = static_cast<uint8_t>(v);
        } else if (!on && k >= 0) {
            for (int j = k; j + 1 < c.n_affects; ++j) memcpy(c.affects[j], c.affects[j + 1], party::kAffectSize);
            --c.n_affects;
            memset(c.affects[c.n_affects], 0, party::kAffectSize);
        }
        return true;
    }
    if (code == 4 && on && (v & 0x0F) != c.alignment()) {
        it[0x34] = 0;
        const int hp = c.hp() - (v >> 4);
        c.rec[0x1A4] = static_cast<uint8_t>(hp > 0 ? hp : 0);
        if (hp <= 0) {
            c.rec[0x195] = hp <= -10 ? party::Dead : hp < 0 ? party::Dying : party::Unconscious;
            c.rec[0x196] = 0;
        }
        return false;
    }
    return true;
}

void remove_item(party::Character& c, int i)
{
    if (i < 0 || i >= c.n_items) return;
    for (int k = i; k + 1 < c.n_items; ++k) memcpy(c.items[k], c.items[k + 1], items::kRecordSize);
    --c.n_items;
    memset(c.items[c.n_items], 0, items::kRecordSize);
}

bool halve(party::Character& c, int i)
{
    if (i < 0 || i >= c.n_items || c.n_items >= party::kMaxItems) return false;
    uint8_t* r = c.items[i];
    const int half = r[0x39] / 2;
    if (half <= 0) return false;
    uint8_t* n = c.items[c.n_items++];
    memcpy(n, r, items::kRecordSize);
    r[0x39] = static_cast<uint8_t>(r[0x39] - half);
    n[0x39] = static_cast<uint8_t>(half);
    n[0x34] = 0;                       // the new pile isn't readied
    return true;
}

namespace {

// Items that go together in one pile: the same in every way, counted
bool same_pile(const uint8_t* a, const uint8_t* b)
{
    if (a[0x39] == 0 || b[0x39] == 0) return false;
    static const int kSame[] = {0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x36, 0x37, 0x3C, 0x3D, 0x3E};
    for (int o : kSame)
        if (a[o] != b[o]) return false;
    return a[0x3C] < 2;               // (the games' rule)
}

} // namespace

int join(party::Character& c, int i)
{
    if (i < 0 || i >= c.n_items) return 0;
    int joined = 0;
    for (int k = 0; k < c.n_items;) {
        if (k == i || !same_pile(c.items[i], c.items[k])) {
            ++k;
            continue;
        }
        const int room = 255 - c.items[i][0x39];
        if (room <= 0) break;
        const int move = c.items[k][0x39] < room ? c.items[k][0x39] : room;
        c.items[i][0x39] = static_cast<uint8_t>(c.items[i][0x39] + move);
        c.items[k][0x39] = static_cast<uint8_t>(c.items[k][0x39] - move);
        ++joined;
        if (c.items[k][0x39] == 0) {
            remove_item(c, k);
            if (k < i) --i;
        } else {
            ++k;
        }
    }
    return joined;
}

int sell_value(const uint8_t* item, const ItemFacts& f)
{
    const items::Item it{item};
    int v = it.value() > 0 ? it.value() / 2 : 0;
    if (it.count() > 1) {
        if (it.type() != f.arrow && it.type() != f.quarrel) v = it.count() * v / 20;
        else v *= it.count();
    }
    return v;
}

int remove_affects(party::Character& c, uint8_t type)
{
    if (!type) return 0;
    int n = 0;
    for (int i = 0; i < c.n_affects;) {
        if (c.affects[i][0] != type) {
            ++i;
            continue;
        }
        for (int k = i; k + 1 < c.n_affects; ++k) memcpy(c.affects[k], c.affects[k + 1], party::kAffectSize);
        --c.n_affects;
        memset(c.affects[c.n_affects], 0, party::kAffectSize);
        ++n;
    }
    return n;
}

namespace {

constexpr int kHp = 0x1A4, kHpMax = 0x78, kHealth = 0x195, kInCombat = 0x196;

bool any_disease(const party::Character& c, const CureFacts& f)
{
    for (uint8_t t : f.disease)
        if (t && c.has_affect(t)) return true;
    return false;
}

bool cursed_item(const party::Character& c)
{
    for (int i = 0; i < c.n_items; ++i)
        if (c.items[i][0x36]) return true;
    return false;
}

} // namespace

bool heal(party::Character& c, int amount)
{
    const int h = c.health();
    if (!(h == party::Okay || h == party::Animated || h == party::Unconscious || h == party::Dying)) return false;
    int hp = c.hp() + (amount > 0 ? amount : 0);
    if (hp > c.hp_max()) hp = c.hp_max();
    c.rec[kHp] = static_cast<uint8_t>(hp);
    if (!c.in_combat()) {
        if (c.rec[kHealth] == party::Dying) c.rec[kHealth] = party::Unconscious;
        if (c.rec[kHealth] == party::Unconscious && c.hp() > 0) {
            c.rec[kHealth] = party::Okay;
            c.rec[kInCombat] = 1;
        }
    }
    return true;
}

bool needs_cure(const party::Character& c, Cure cure, const CureFacts& f)
{
    switch (cure) {
    case kCureBlindness: return c.has_affect(f.blinded);
    case kCureDisease: return any_disease(c, f);
    case kNeutralizePoison: return c.has_affect(f.poisoned);
    case kRaiseDead: return c.health() == party::Dead || c.health() == party::Animated;
    case kRemoveCurse: return cursed_item(c) || c.has_affect(f.curse);
    case kStoneToFlesh: return c.health() == party::Stoned;
    default: return true;
    }
}

void apply_cure(party::Character& c, Cure cure, const CureFacts& f, create::Dice& d)
{
    switch (cure) {
    case kCureBlindness: remove_affects(c, f.blinded); break;
    case kCureDisease:
        for (uint8_t t : f.disease) remove_affects(c, t);
        break;
    case kCureLight: heal(c, d.roll(8, 1)); break;
    case kCureSerious: heal(c, d.roll(8, 2) + 1); break;
    case kCureCritical: heal(c, d.roll(8, 3) + 3); break;
    case kHealCure:
        heal(c, c.hp_max() - c.hp() - d.roll(4, 1));
        remove_affects(c, f.blinded);
        for (uint8_t t : f.disease) remove_affects(c, t);
        remove_affects(c, f.feeblemind);
        break;
    case kNeutralizePoison:
        remove_affects(c, f.poisoned);
        remove_affects(c, f.slow_poison);
        remove_affects(c, f.poison_damage);
        break;
    case kRaiseDead:
        if (!needs_cure(c, cure, f)) break;
        remove_affects(c, f.animate_dead);
        remove_affects(c, f.poisoned);
        c.rec[kHp] = 1;
        c.rec[kHealth] = party::Okay;
        c.rec[kInCombat] = 1;
        break;
    case kRemoveCurse:
        if (remove_affects(c, f.curse)) break;
        for (int i = 0; i < c.n_items; ++i)
            if (c.items[i][0x36]) {
                c.items[i][0x34] = 0;          // it comes off (still cursed)
                break;
            }
        break;
    case kStoneToFlesh:
        if (c.health() != party::Stoned) break;
        c.rec[kHealth] = party::Okay;
        c.rec[kInCombat] = 1;
        c.rec[kHp] = 1;
        break;
    default: break;
    }
    (void)kHpMax;
}

int gem_value(int r)
{
    return r <= 25 ? 10 : r <= 50 ? 50 : r <= 70 ? 100 : r <= 90 ? 500 : r <= 99 ? 1000 : r == 100 ? 5000 : 0;
}

int jewel_value(int r, create::Dice& d)
{
    if (r < 1 || r > 100) return 0;
    if (r <= 10) return d.random(900) + 100;
    if (r <= 20) return d.random(1000) + 200;
    if (r <= 40) return d.random(1500) + 300;
    if (r <= 50) return d.random(2500) + 500;
    if (r <= 70) return d.random(5000) + 1000;
    if (r <= 90) return d.random(6000) + 2000;
    return d.random(10000) + 2000;
}

} // namespace rules


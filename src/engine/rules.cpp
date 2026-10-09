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

void recalc(party::Character& c, const items::Names& names, const ItemFacts& f)
{
    uint8_t* r = c.rec;
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

} // namespace rules

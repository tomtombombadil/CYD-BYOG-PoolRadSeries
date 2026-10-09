#include "spells.h"

#include <cstring>

namespace spells {

namespace {

constexpr int kHp = 0x1A4, kHealth = 0x195, kInCombat = 0x196, kSide = 0x197;
constexpr int kConNow = 0x18, kConFull = 0x19;
constexpr int kReadied = 0x34, kCursed = 0x36;

void remove_at(party::Character& c, int i)
{
    for (int k = i; k + 1 < c.n_affects; ++k) memcpy(c.affects[k], c.affects[k + 1], party::kAffectSize);
    --c.n_affects;
    memset(c.affects[c.n_affects], 0, party::kAffectSize);
}

// Takes an effect away; true if they had it (the games' "is Cured")
bool cure(party::Character& c, int type)
{
    const int i = find_affect(c, type);
    if (i < 0) return false;
    remove_at(c, i);
    return true;
}

// The effect on one target: a running one of the same kind gives way
void give(party::Character& c, int type, int minutes, int data, bool call)
{
    const int i = find_affect(c, type);
    if (i >= 0 && (c.affects[i][1] | c.affects[i][2] << 8) > 0) remove_at(c, i);
    add_affect(c, type, minutes, data, call);
}

struct Out {
    Line* out;
    int   cap, n = 0;
    void say(int who, Said what)
    {
        if (n < cap) out[n++] = Line{static_cast<uint8_t>(who), what};
    }
};

} // namespace

Entry entry(const classes::Tables& t, int s)
{
    Entry e;
    const uint16_t at = static_cast<uint16_t>(t.lay.spells + s * 16);
    auto b = [&](int i) { return t.u8(static_cast<uint16_t>(at + i)); };
    e.cls = b(0);
    e.level = b(1);
    e.range = static_cast<int8_t>(b(2));
    e.range_level = b(3);
    e.lasts = b(4);
    e.lasts_level = b(5);
    e.targets = b(7);
    e.affect = b(10);
    e.when = b(11);
    e.aim = b(6);
    e.on_save = b(8);
    e.save = b(9);
    e.delay = b(12);
    e.priority = b(13);
    return e;
}

int reach(const classes::Tables& t, int s, int pw)
{
    const Entry e = entry(t, s);
    if (e.range == -1) return 1;
    int r = e.range + e.range_level * pw;
    if (r == 0 && e.aim != 0) r = 1;
    if (r < 0) r = 1;
    return r;
}

int power(const party::Character& c, const classes::Tables& t, int s)
{
    if (s <= 0) return 0;
    using classes::skill_level;
    if (c.level(classes::Cleric) == 0 && c.level(classes::MagicUser) == 0 && c.level(classes::Paladin) < 9 &&
        c.level(classes::Ranger) < 8)
        return 6;
    auto most = [](int a, int b) { return a > b ? a : b; };
    switch (t.spell_class(s)) {
    case 0: return most(skill_level(c, classes::Cleric), skill_level(c, classes::Paladin) - 8);
    case 1: return most(skill_level(c, classes::Ranger) - 7, 0);
    case 2: return most(skill_level(c, classes::MagicUser), skill_level(c, classes::Ranger) - 8);
    case 3: return 12;
    default: return 0;
    }
}

int lasts(const classes::Tables& t, int s, int pw)
{
    const Entry e = entry(t, s);
    return e.lasts + e.lasts_level * pw;
}

void add_affect(party::Character& c, int type, int minutes, int data, bool call)
{
    if (c.n_affects >= party::kMaxAffects) return;
    uint8_t* a = c.affects[c.n_affects++];
    memset(a, 0, party::kAffectSize);
    a[0] = static_cast<uint8_t>(type);
    a[1] = static_cast<uint8_t>(minutes);
    a[2] = static_cast<uint8_t>(minutes >> 8);
    a[3] = static_cast<uint8_t>(data);
    a[4] = call ? 1 : 0;
}

int find_affect(const party::Character& c, int type)
{
    for (int i = 0; i < c.n_affects; ++i)
        if (c.affects[i][0] == type) return i;
    return -1;
}

bool can_cast(const party::Character& c) { return c.health() != party::Animated && c.in_combat(); }

int cast(party::Party& p, int caster, int target, const CampSpell& cs, const classes::Tables& t,
         const rules::CureFacts& cures, const Facts& f, create::Dice& d, Line* out, int cap)
{
    Out o{out, cap};
    if (caster < 0 || caster >= p.count) return 0;
    party::Character& me = p.m[caster];
    const Entry e = entry(t, cs.spell);
    const int pw = power(me, t, cs.spell);
    const int minutes = lasts(t, cs.spell, pw);

    // Who it's cast on
    int who[party::kMaxParty], n = 0;
    if (e.targets == kParty) {
        for (int i = 0; i < p.count; ++i) who[n++] = i;
    } else if (e.targets == kMember) {
        if (target >= 0 && target < p.count) who[n++] = target;
    } else {
        who[n++] = caster;
    }

    // The effect on each one, said with the spell's word
    auto affect_all = [&](int data, bool call) {
        if (e.affect <= 0) return;
        for (int k = 0; k < n; ++k) {
            give(p.m[who[k]], e.affect, minutes, data, call);
            if (cs.word) o.say(who[k], Said::Word);
        }
    };

    switch (cs.does) {
    case Does::NotYet: break;
    case Does::Affect: affect_all(pw, false); break;
    case Does::Prayer: affect_all(me.rec[kSide] * 16 + pw, false); break;
    case Does::Mirror: affect_all((d.roll(4, 1) << 4) + pw, false); break;
    case Does::Haste: {
        // The first `power` of the party: a slowed one is cured instead
        int left = pw, m = 0;
        for (int k = 0; k < n; ++k) {
            if (left <= 0) break;
            --left;
            if (cure(p.m[who[k]], f.slow)) {
                o.say(who[k], Said::Cured);
                continue;
            }
            who[m++] = who[k];
        }
        n = m;
        affect_all(pw, false);
        break;
    }
    case Does::Heal:
        for (int k = 0; k < n; ++k) {
            party::Character& c = p.m[who[k]];
            if (rules::heal(c, d.roll(cs.sides, cs.n) + cs.plus))
                o.say(who[k], c.hp() >= c.hp_max() ? Said::Fully : Said::Partly);
        }
        break;
    case Does::CureBlind:
        for (int k = 0; k < n; ++k)
            if (cure(p.m[who[k]], cures.blinded)) {
                o.say(who[k], Said::Cured);
                o.say(who[k], Said::CanSee);
            }
        break;
    case Does::CureDisease:
        for (int k = 0; k < n; ++k) {
            party::Character& c = p.m[who[k]];
            for (int i = 0; i < 3; ++i) {
                if (!f.disease[i] || !cure(c, f.disease[i])) continue;
                o.say(who[k], Said::Cured);
                for (uint8_t also : f.disease_with[i])
                    if (also) cure(c, also);
            }
        }
        break;
    case Does::Neutralize:
        for (int k = 0; k < n; ++k) {
            party::Character& c = p.m[who[k]];
            if (c.health() == party::Animated) continue;
            if (find_affect(c, cures.poisoned) < 0) {
                o.say(who[k], Said::Unaffected);
                continue;
            }
            if (c.hp() == 0) c.rec[kHp] = 1;
            cure(c, cures.poisoned);
            cure(c, cures.slow_poison);
            cure(c, cures.poison_damage);
            c.rec[kInCombat] = 1;
            c.rec[kHealth] = party::Okay;
            o.say(who[k], Said::Unpoisoned);
        }
        break;
    case Does::SlowPoison:
        for (int k = 0; k < n; ++k) {
            party::Character& c = p.m[who[k]];
            if (c.health() == party::Animated || find_affect(c, cures.poisoned) < 0) continue;
            if (c.hp() == 0) c.rec[kHp] = 1;
            give(c, e.affect, minutes, 0xFF, true);
            if (cs.word) o.say(who[k], Said::Word);
            add_affect(c, cures.poison_damage, 10, 0xFF, true);
        }
        break;
    case Does::RemoveCurse:
        for (int k = 0; k < n; ++k) {
            party::Character& c = p.m[who[k]];
            if (cure(c, cures.curse)) {
                o.say(who[k], Said::Cured);
                o.say(who[k], Said::Uncursed);
                continue;
            }
            for (int i = 0; i < c.n_items; ++i)
                if (c.items[i][kCursed]) {
                    c.items[i][kReadied] = 0;      // it comes off (still cursed)
                    o.say(who[k], Said::ItemUncursed);
                    break;
                }
        }
        break;
    case Does::Raise:
        for (int k = 0; k < n; ++k) {
            party::Character& c = p.m[who[k]];
            if ((c.health() != party::Dead && c.health() != party::Animated) || c.rec[kConNow] == 0 ||
                c.race() == f.elf_race)
                continue;
            cure(c, cures.animate_dead);
            cure(c, cures.poisoned);
            c.rec[kHealth] = party::Okay;
            c.rec[kInCombat] = 1;
            if (c.rec[kConFull] == c.rec[kConNow]) --c.rec[kConFull];    // (no item raising it)
            --c.rec[kConNow];
            c.rec[kHp] = 1;
            o.say(who[k], Said::Raised);
        }
        break;
    }
    return o.n;
}

int hp_lost(const party::Party& p)
{
    int n = 0;
    for (int i = 0; i < p.count; ++i) n += p.m[i].hp_max() - p.m[i].hp();
    return n;
}

FixPlan fix_plan(const party::Party& p, const classes::Tables& t, const CampSpell* camp, int n_camp, create::Dice& d)
{
    FixPlan f;
    const int lost = hp_lost(p);
    if (lost <= 0) return f;
    int most = 0, most_heal = 0;
    for (int i = 0; i < p.count; ++i) {
        const party::Character& c = p.m[i];
        if (c.health() != party::Okay) continue;
        // The cure spells held now
        for (int k = 0; k < 84; ++k) {
            const int s = c.rec[0x1E + k];
            if (!s || (s & 0x80)) continue;
            for (int j = 0; j < n_camp; ++j)
                if (camp[j].spell == s && camp[j].does == Does::Heal) f.heal += d.roll(camp[j].sides, camp[j].n) + camp[j].plus;
        }
        // Those memorized again: a day's slots of each cure's level, 15
        // minutes a level each, after 4 hours' start (6 past 2nd level)
        int time = 0, start = 0;
        bool low = false, high = false, top = false;
        for (int j = 0; j < n_camp; ++j) {
            if (camp[j].does != Does::Heal) continue;
            const int cls = t.spell_class(camp[j].spell), lv = t.spell_level(camp[j].spell);
            if (cls < 0 || cls > 2 || lv < 1 || lv > 5) continue;
            const int slots = c.rec[0x12D + cls * 5 + lv - 1];
            if (!slots) continue;
            for (int k = 0; k < slots; ++k) f.heal += d.roll(camp[j].sides, camp[j].n) + camp[j].plus;
            time += slots * lv * 15;
            if (lv <= 2) low = true;
            else high = true;
            if (lv >= 5) top = true;
        }
        if (low) {
            start = 240;
            most_heal += 27;
        }
        if (high) {
            start = 360;
            most_heal += top ? 78 : 34;
        }
        if (time + start > most) most = time + start;
    }
    // Less to heal than the healers can: a shorter rest
    if (lost < most_heal) most /= most_heal / lost;
    f.minutes = most;
    return f;
}

int fix_heal(party::Party& p, int heal)
{
    for (int i = 0; i < p.count && heal > 0; ++i) {
        party::Character& c = p.m[i];
        int need = c.hp_max() - c.hp();
        if (need <= 0) continue;
        if (need > heal) need = heal;
        if (rules::heal(c, need)) heal -= need;
    }
    return heal;
}

} // namespace spells

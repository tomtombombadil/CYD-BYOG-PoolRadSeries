#include "magic.h"

#include <cstring>

#include "rules.h"

namespace magic {

namespace {

constexpr int kCastAt = 0x12D;      // [3][5] spells a day: cleric, druid, magic-user
constexpr int kBookAt = 0x79;       // known spells, 1-100
constexpr int kInt = 0x13, kWis = 0x15;

int cast_count(const party::Character& c, int cls, int level)
{
    return cls >= 0 && cls < 3 && level >= 1 && level <= 5 ? c.rec[kCastAt + cls * 5 + level - 1] : 0;
}

bool usable(const classes::Tables& t, int s)
{
    return s >= 1 && s < t.lay.spell_count && t.spell_class(s) < 3 && t.spell_level(s) >= 1 && t.spell_level(s) <= 5;
}

void sort_by_level(const classes::Tables& t, uint8_t* ids, int n)
{
    for (int i = 1; i < n; ++i)
        for (int j = i; j > 0; --j) {
            const int a = t.spell_level(ids[j - 1]), b = t.spell_level(ids[j]);
            if (a < b || (a == b && ids[j - 1] <= ids[j])) break;
            const uint8_t x = ids[j];
            ids[j] = ids[j - 1];
            ids[j - 1] = x;
        }
}

// The first spell being memorized: its list slot, or -1 (in list order)
int first_learning(const party::Character& c)
{
    for (int i = kListSize - 1; i >= 0; --i)
        if (c.rec[kListAt + i] & 0x80) return i;
    return -1;
}

} // namespace

bool can_use(const party::Character& c, const classes::Tables& t, int s)
{
    if (!usable(t, s)) return false;
    switch (t.spell_class(s)) {
    case Cleric:
        return c.rec[kWis] > 8 &&
               (classes::skill_level(c, classes::Cleric) > 0 || classes::skill_level(c, classes::Paladin) > 8);
    case Druid: return c.rec[kWis] > 8 && classes::skill_level(c, classes::Ranger) > 6;
    case MagicUser: return c.rec[kInt] > 8;
    default: return false;
    }
}

int held(const party::Character& c, const classes::Tables& t, int cls, int level)
{
    int n = 0;
    for (int i = 0; i < kListSize; ++i) {
        const int s = c.rec[kListAt + i] & 0x7F;
        if (s && t.spell_class(s) == cls && t.spell_level(s) == level) ++n;
    }
    return n;
}

int room(const party::Character& c, const classes::Tables& t, int cls, int level)
{
    const int r = cast_count(c, cls, level) - held(c, t, cls, level);
    return r > 0 ? r : 0;
}

bool any_room(const party::Character& c, const classes::Tables& t)
{
    (void)t;
    for (int cls = 0; cls < 3; ++cls)
        for (int lv = 1; lv <= 5; ++lv)
            if (cast_count(c, cls, lv) > 0) return true;
    return false;
}

int in_memory(const party::Character& c, const classes::Tables& t, bool learning, uint8_t* ids, int cap)
{
    int n = 0;
    for (int i = kListSize - 1; i >= 0 && n < cap; --i) {
        const uint8_t v = c.rec[kListAt + i];
        if (!v || ((v & 0x80) != 0) != learning) continue;
        if (!can_use(c, t, v & 0x7F)) continue;
        ids[n++] = v & 0x7F;
    }
    sort_by_level(t, ids, n);
    return n;
}

int known(const party::Character& c, const classes::Tables& t, uint8_t* ids, int cap)
{
    int n = 0;
    for (int s = 1; s < t.lay.spell_count && s <= 100 && n < cap; ++s)
        if (c.rec[kBookAt + s - 1] && can_use(c, t, s)) ids[n++] = static_cast<uint8_t>(s);
    sort_by_level(t, ids, n);
    return n;
}

int learnable(const party::Character& c, const classes::Tables& t, uint8_t* ids, int cap)
{
    int n = 0;
    for (int s = 1; s < t.lay.spell_count && s <= 100 && n < cap; ++s)
        if (usable(t, s) && cast_count(c, t.spell_class(s), t.spell_level(s)) > 0 && can_use(c, t, s) &&
            !c.rec[kBookAt + s - 1])
            ids[n++] = static_cast<uint8_t>(s);
    sort_by_level(t, ids, n);
    return n;
}

void learn(party::Character& c, int s)
{
    if (s >= 1 && s <= 100) c.rec[kBookAt + s - 1] = 1;
}

bool add(party::Character& c, const classes::Tables& t, int s)
{
    if (!usable(t, s) || room(c, t, t.spell_class(s), t.spell_level(s)) <= 0) return false;
    for (int i = kListSize - 1; i >= 0; --i)
        if (!c.rec[kListAt + i]) {
            c.rec[kListAt + i] = static_cast<uint8_t>(s | 0x80);
            return true;
        }
    return false;
}

void cancel(party::Character& c)
{
    for (int i = 0; i < kListSize; ++i)
        if (c.rec[kListAt + i] & 0x80) c.rec[kListAt + i] = 0;
    c.rec[kToLearnAt] = 0;
}

bool memorizing(const party::Character& c) { return first_learning(c) >= 0; }

bool remove(party::Character& c, int s)
{
    for (int i = 0; i < kListSize; ++i)
        if (c.rec[kListAt + i] == s) {
            c.rec[kListAt + i] = 0;
            return true;
        }
    return false;
}

bool is_scroll(const Scrolls& sc, const uint8_t* it)
{
    if (!sc.names) return false;
    const int slot = sc.names->type(it[0x2E]).slot;
    return slot >= 11 && slot <= 13;
}

int scroll_spells(party::Character& c, const classes::Tables& t, const Scrolls& sc, bool scribing, uint8_t* ids,
                  int cap)
{
    int n = 0;
    const bool cleric = classes::skill_level(c, classes::Cleric) > 0;
    for (int i = 0; i < c.n_items; ++i) {
        uint8_t* it = c.items[i];
        if (!is_scroll(sc, it)) continue;
        if ((sc.read_magic && c.has_affect(sc.read_magic)) || (cleric && sc.names->type(it[0x2E]).slot == 12))
            it[0x35] = 0;                   // read: its spells are known
        if (it[0x35]) continue;
        for (int k = 0; k < 3 && n < cap; ++k) {
            const uint8_t v = it[kScrollAt + k];
            if (scribing ? v > 0x80 : v > 0) ids[n++] = v & 0x7F;
        }
    }
    sort_by_level(t, ids, n);
    return n;
}

Scribe scribe(party::Character& c, const classes::Tables& t, const Scrolls& sc, int s)
{
    if (s >= 1 && s <= 100 && c.rec[kBookAt + s - 1]) return Scribe::Known;
    for (int i = 0; i < c.n_items; ++i)
        if (is_scroll(sc, c.items[i]))
            for (int k = 0; k < 3; ++k)
                if (c.items[i][kScrollAt + k] == (s | 0x80)) return Scribe::Already;
    if (cast_count(c, t.spell_class(s), t.spell_level(s)) <= 0) return Scribe::Cannot;
    for (int i = 0; i < c.n_items; ++i)
        if (is_scroll(sc, c.items[i]))
            for (int k = 0; k < 3; ++k)
                if (c.items[i][kScrollAt + k] == s) {
                    c.items[i][kScrollAt + k] = static_cast<uint8_t>(s | 0x80);
                    return Scribe::Ok;
                }
    return Scribe::Cannot;
}

bool scribing(const party::Character& c, const Scrolls& sc)
{
    for (int i = 0; i < c.n_items; ++i)
        if (is_scroll(sc, c.items[i]))
            for (int k = 0; k < 3; ++k)
                if (c.items[i][kScrollAt + k] > 0x80) return true;
    return false;
}

void cancel_scribes(party::Character& c, const Scrolls& sc)
{
    for (int i = 0; i < c.n_items; ++i)
        if (is_scroll(sc, c.items[i]))
            for (int k = 0; k < 3; ++k) c.items[i][kScrollAt + k] &= 0x7F;
}

void scribed(party::Character& c, const Scrolls& sc, int i, int k)
{
    if (i < 0 || i >= c.n_items || k < 0 || k > 2) return;
    uint8_t* it = c.items[i];
    const int s = it[kScrollAt + k] & 0x7F;
    if (s >= 1 && s <= 100) c.rec[kBookAt + s - 1] = 1;
    it[kScrollAt + k] = 0;
    it[0x30] = static_cast<uint8_t>(it[0x30] - 1);
    if (it[0x30] < sc.one_spell) {
        // Used up
        for (int j = i; j + 1 < c.n_items; ++j) memcpy(c.items[j], c.items[j + 1], party::kItemSize);
        --c.n_items;
        memset(c.items[c.n_items], 0, party::kItemSize);
    }
}

// The next spell to scribe (item, slot) in the games' order, or false
static bool next_scribe(const party::Character& c, const Scrolls& sc, int* item, int* slot)
{
    for (int i = 0; i < c.n_items; ++i)
        if (is_scroll(sc, c.items[i]))
            for (int k = 0; k < 3; ++k)
                if (c.items[i][kScrollAt + k] > 0x80) {
                    *item = i;
                    *slot = k;
                    return true;
                }
    return false;
}

int rest_minutes(party::Character& c, const classes::Tables& t, const Scrolls& sc)
{
    int max_level = 0, total = 0;
    for (int i = 0; i < kListSize; ++i) {
        const uint8_t v = c.rec[kListAt + i];
        if (!(v & 0x80)) continue;
        const int lv = t.spell_level(v & 0x7F);
        if (lv > max_level) max_level = lv;
        total += lv;
    }
    for (int i = 0; i < c.n_items; ++i)
        if (is_scroll(sc, c.items[i]))
            for (int k = 0; k < 3; ++k) {
                const uint8_t v = c.items[i][kScrollAt + k];
                if (v <= 0x80) continue;
                const int lv = t.spell_level(v & 0x7F);
                if (lv > max_level) max_level = lv;
                total += lv;
            }
    int hours = 0;
    if (total > 0) hours = 4;
    if (max_level > 2) hours = 6;
    c.rec[kToLearnAt] = static_cast<uint8_t>(hours);
    return hours * 60 + total * 15;
}

void begin(Rest& r) { r = Rest{}; }

int tick_affects(party::Character& c, int minutes)
{
    int ended = 0;
    for (int i = 0; i < c.n_affects;) {
        uint8_t* a = c.affects[i];
        const int m = a[1] | a[2] << 8;
        if (m == 0) {
            ++i;
            continue;
        }
        if (m > minutes) {
            const int left = m - minutes;
            a[1] = static_cast<uint8_t>(left);
            a[2] = static_cast<uint8_t>(left >> 8);
            ++i;
            continue;
        }
        for (int k = i; k + 1 < c.n_affects; ++k) memcpy(c.affects[k], c.affects[k + 1], party::kAffectSize);
        --c.n_affects;
        memset(c.affects[c.n_affects], 0, party::kAffectSize);
        ++ended;
    }
    return ended;
}

// The level of what comes next (a spell to scribe first, then one to
// memorize), 0: nothing
static int next_level(const party::Character& c, const classes::Tables& t, const Scrolls& sc)
{
    int i, k;
    if (next_scribe(c, sc, &i, &k)) return t.spell_level(c.items[i][kScrollAt + k] & 0x7F);
    const int next = first_learning(c);
    return next >= 0 ? t.spell_level(c.rec[kListAt + next] & 0x7F) : 0;
}

Step step(Rest& r, party::Party& p, const classes::Tables& t, const Scrolls& sc)
{
    Step st;
    for (int& l : st.learnt) l = 0;
    for (int& l : st.scribed) l = 0;
    // A day's rest heals a point
    if (++r.steps >= 8 * 36) {
        r.steps = 0;
        for (int i = 0; i < p.count; ++i) rules::heal(p.m[i], 1);
        st.healed = true;
    }
    // Spells come one after another once the start has passed: each
    // takes 3 steps a level
    for (int i = 0; i < p.count; ++i) {
        party::Character& c = p.m[i];
        if (r.wait[i] > 0) --r.wait[i];
        if (r.wait[i] == 0 && c.rec[kToLearnAt] == 0) {
            int item, slot;
            const int at = first_learning(c);
            if (next_scribe(c, sc, &item, &slot)) {
                st.scribed[i] = c.items[item][kScrollAt + slot] & 0x7F;
                scribed(c, sc, item, slot);
                r.wait[i] = next_level(c, t, sc) * 3;
            } else if (at >= 0) {
                const int s = c.rec[kListAt + at] & 0x7F;
                c.rec[kListAt + at] = static_cast<uint8_t>(s);
                st.learnt[i] = s;
                r.wait[i] = next_level(c, t, sc) * 3;
            }
        }
    }
    // An hour: the start counts down
    if (++r.hour >= 12) {
        r.hour = 0;
        for (int i = 0; i < p.count; ++i) {
            party::Character& c = p.m[i];
            if (c.rec[kToLearnAt] > 0 && --c.rec[kToLearnAt] == 0) r.wait[i] = next_level(c, t, sc) * 2;
        }
    }
    for (int i = 0; i < p.count; ++i) tick_affects(p.m[i], 5);
    return st;
}

} // namespace magic

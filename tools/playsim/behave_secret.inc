// Behaviour checks: Secret of the Silver Blades (included by sweep.cpp with
// -DBEHAVE_SECRET; run with GAME=3 on Secret's folder). What its program
// does - the class values from its own tables, the Level's monsters, the
// vault's file, training's hit points - against the oracle (Secret's tables, read from the
// player's program by a scratch script, and the constants of the
// Project's claude/secret_facts.md / secret_facts_2.md), never against the
// engine's own code. ONLY= values, train, level, vault, spells, monsters,
// allspells, allmonsters, allitems, allsituations.
#include "behave_oracle.inc"

namespace bsecret {

using namespace play;
Making* m = nullptr;
const char* P = "secret";

void set_stat(party::Character& c, int i, int v)
{
    c.rec[0x10 + i * 2] = static_cast<uint8_t>(v);
    c.rec[0x11 + i * 2] = static_cast<uint8_t>(v);
}
void bare(party::Character& c, int race, int cls, int stat = 12)
{
    c = party::Character{};
    c.rec[0x74] = static_cast<uint8_t>(race);
    c.rec[0x75] = static_cast<uint8_t>(cls);
    for (int i = 0; i < 6; ++i) set_stat(c, i, stat);
    c.rec[0x196] = 1;
}
void levels(party::Character& c, const int lv[8])
{
    int hd = 0;
    for (int k = 0; k < 8; ++k) {
        c.rec[0x109 + k] = static_cast<uint8_t>(lv[k]);
        if (lv[k] > hd) hd = lv[k];
    }
    c.rec[0xE5] = static_cast<uint8_t>(hd);
}
// The highest level a class has (its experience row: -1 past it)
int top_level(int k)
{
    const auto* r = b_row("xp", k);
    if (!r) return 0;
    int top = 1;
    for (size_t i = 1; i < r->size() && (*r)[i] > 0; ++i) top = static_cast<int>(i) + 1;
    return top;
}
int othac0(int k, int lv)
{
    const auto* r = b_row("thac0", k);
    return r && lv >= 0 && lv + 1 < static_cast<int>(r->size()) ? (*r)[1 + lv] : -1;
}
const std::vector<int>* osave(int k, int lv) { return b_row("save", k, lv); }
// The original's THAC0 (GAME.OVR 0x3C1B1): 0, then the most of every
// class's row at that class's level - the classes not held at level 0
int othac0_all(const int lv[8])
{
    int want = 0;
    for (int j = 0; j < 7; ++j) want = std::max(want, othac0(j, lv[j]));
    return want;
}

// THAC0 and saves of every class at every level its table has, and two
// classes together (the better of each)
void class_values()
{
    static party::Character c;
    for (int k = 0; k < 7; ++k) {
        const int top = top_level(k);
        for (int L = 1; L <= top && L <= 18; ++L) {
            bare(c, 7, k);
            int lv[8] = {};
            lv[k] = L;
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            char w[80];
            snprintf(w, sizeof w, "THAC0 class %d level %d", k, L);
            expect(P, w, c.rec[0x73] == othac0_all(lv), "%d, the original's %d", c.rec[0x73], othac0_all(lv));
            const auto* s = osave(k, L);
            bool ok = s != nullptr;
            for (int i = 0; ok && i < 5; ++i) ok = c.rec[0xDF + i] == (*s)[2 + i];
            snprintf(w, sizeof w, "saves class %d level %d", k, L);
            expect(P, w, ok, "%d %d %d %d %d", c.rec[0xDF], c.rec[0xE0], c.rec[0xE1], c.rec[0xE2], c.rec[0xE3]);
        }
    }
    // A fighter / thief (the original's save routine: the best of the classes')
    const int pairs[][4] = {{2, 7, 6, 8}, {2, 8, 6, 9}, {5, 9, 6, 10}, {0, 6, 2, 7}};
    for (const auto& p : pairs) {
        bare(c, 1, 2);
        int lv[8] = {};
        lv[p[0]] = p[1];
        lv[p[2]] = p[3];
        levels(c, lv);
        classes::class_bonuses(c, m->tables);
        const auto* a = osave(p[0], p[1]);
        const auto* b = osave(p[2], p[3]);
        bool ok = a && b;
        for (int i = 0; ok && i < 5; ++i) ok = c.rec[0xDF + i] == std::min((*a)[2 + i], (*b)[2 + i]);
        const int want = othac0_all(lv);
        char w[80];
        snprintf(w, sizeof w, "classes %d/%d at %d/%d: saves (a dwarf: no Constitution in them)", p[0], p[2], p[1], p[3]);
        expect(P, w, ok, "%d %d %d %d %d", c.rec[0xDF], c.rec[0xE0], c.rec[0xE1], c.rec[0xE2], c.rec[0xE3]);
        snprintf(w, sizeof w, "classes %d/%d at %d/%d: THAC0", p[0], p[2], p[1], p[3]);
        expect(P, w, c.rec[0x73] == want, "%d, the original's %d", c.rec[0x73], want);
    }
}

// Training (free in Secret): a single class from level 2 to 3 gains its
// own die (DS 0x9B7; the better of two rolls), Constitution 12 adding
// nothing - all highest: the die's sides, all lowest: 1
int ocol(const char* key, int idx)
{
    const auto& r = b_rows(key);
    return r.empty() || idx < 0 || idx >= static_cast<int>(r[0].size()) ? 0 : r[0][idx];
}
void training()
{
    static party::Character c;
    for (int k = 0; k < 7; ++k)
        for (int dice = 0; dice < 2; ++dice) {
            bare(c, 7, k);
            int lv[8] = {};
            lv[k] = 2;
            levels(c, lv);
            classes::class_bonuses(c, m->tables);
            c.rec[0x78] = 30;
            c.rec[0x1A4] = 25;
            const int mask = m->tables.u8(static_cast<uint16_t>(m->tables.lay.class_masks + k));
            if (dice) dice_low();
            else dice_high();
            create::train_classes(c, m->tables, m->facts, m->dice, mask, false);
            dice_usual();
            const int gain = dice ? 1 : ocol("hpdice", k);
            const bool ok = c.level(k) == 3 && c.hp_max() == 30 + gain && c.hp() == 25 + gain;
            char w[80];
            snprintf(w, sizeof w, "training hit points class %d dice %s", k, dice ? "low" : "high");
            expect(P, w, ok, "level %d max %d now %d, the original's 3 %d %d", c.level(k), c.hp_max(), c.hp(), 30 + gain,
                   25 + gain);
        }
}

// Alter's Level: the other side's hit points x (level + 2) / 4 as they come
// into a fight (the fire knives, MON1CHA 72, whose record's own HP is read)
void level()
{
    uint8_t raw[party::kSecretRecordSize];
    if (read_block("MON1CHA.DAX", 72, raw, sizeof raw) != static_cast<int>(sizeof raw)) {
        expect(P, "MON1CHA 72", false);
        return;
    }
    const auto* mul = b_row("level_mul", 2);
    for (int L = 0; L < 5; ++L) {
        vm->set(0x4CF4, static_cast<uint16_t>(L));
        fight_clear_monsters();
        fight_load_monster(72, 1, 72);
        const int f = mul ? (*mul)[L] : 0;
        int want = raw[0x1B5] * f / 4 & 0xFF, want_max = raw[0x70] * f / 4 & 0xFF;
        if (!want || !want_max) want = want_max = 1;
        const bool ok = ml && ml->n == 1 && ml->m[0].get(0x1A4) == want && ml->m[0].get(0x78) == want_max;
        char w[60];
        snprintf(w, sizeof w, "Level %d: a monster's hit points", L);
        expect(P, w, ok, "%d / %d, the original's %d / %d", ml && ml->n ? ml->m[0].get(0x1A4) : -1,
               ml && ml->n ? ml->m[0].get(0x78) : -1, want, want_max);
    }
    fight_clear_monsters();
    vm->set(0x4CF4, 2);
}

// The vault: what it keeps goes to VAULT.DAT in the original's layout (7
// coin counts of 4 bytes, then 67-byte items, the first deposited first)
// and comes back the same
void vault_file()
{
    const auto* v = b_row("vault", 28);
    ground->clear();
    ground->money[3] = 504;
    ground->money[4] = 100;
    ground->money[6] = 70000;
    uint8_t a[items::kRecordSize] = {}, b[items::kRecordSize] = {};
    a[0x2E] = 18;
    a[0x31] = 18;
    a[0x32] = 1;
    b[0x2E] = 51;
    b[0x31] = 51;
    b[0x30] = 90;
    b[0x2F] = 62;
    // (deposited: a then b - the list shows a first)
    memcpy(ground->item[0], b, sizeof b);
    memcpy(ground->item[1], a, sizeof a);
    ground->n = 2;
    expect(P, "vault written", vault_write());
    char path[200];
    save_path(d->prof->vault.file, path, sizeof path);
    plat::File f = plat::open(path, "r");
    std::vector<uint8_t> bytes;
    if (f) {
        uint8_t t[256];
        int n;
        while ((n = f.read(t, sizeof t)) > 0) bytes.insert(bytes.end(), t, t + n);
        f.close();
    }
    const size_t want = v ? static_cast<size_t>((*v)[0] + 2 * (*v)[1]) : 0;
    expect(P, "VAULT.DAT's size", bytes.size() == want, "%zu, the original's %zu", bytes.size(), want);
    auto u32 = [&](size_t o) { return o + 4 <= bytes.size() ? bytes[o] | bytes[o + 1] << 8 | bytes[o + 2] << 16 | (uint32_t)bytes[o + 3] << 24 : 0u; };
    expect(P, "VAULT.DAT's coins (copper first, 4 bytes each)",
           u32(12) == 504 && u32(16) == 100 && u32(24) == 70000 && u32(0) == 0, "%u %u %u", u32(12), u32(16), u32(24));
    expect(P, "VAULT.DAT's first item: the first deposited", bytes.size() >= 28 + 67 && bytes[28 + 0x2E] == 18 &&
                                                                 bytes[28 + 67 + 0x2E] == 51);
    ground->clear();
    vault_read();
    const bool back = ground->n == 2 && ground->money[3] == 504 && ground->money[6] == 70000 &&
                      ground->item[1][0x2E] == 18 && ground->item[0][0x2E] == 51;
    expect(P, "the vault read back (the list's order)", back, "%d items", ground->n);
    plat::remove(path);
    ground->clear();
}

// ---- Its spells past 100 in a fight (secret_facts_2.md 19): the fire
// knives' fight, the field cleared, plain creatures put where wanted
int s_d20 = 0, s_mode = 1;
int s_die(int n)
{
    if (n == 20 && s_d20) return s_d20 - 1;
    return s_mode == 1 ? n - 1 : 0;
}
void s_dice(int mode, int d20 = 0)
{
    s_mode = mode;
    s_d20 = d20;
    create::g_die_hook = s_die;
}
combat::Fighter& F(int i) { return fg->b.f[i]; }
bool new_fight()
{
    create::g_die_hook = nullptr;
    if (!boot() || !start_fight() || !load_magic() || !fg || fg->b.n < 8) return false;
    combat::Battle& b = fg->b;
    const int floor = b.ground[b.f[0].y][b.f[0].x];
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = static_cast<uint8_t>(floor);
    for (int i = 0; i < b.n; ++i) b.f[i].size = 0;
    b.aim_x = b.aim_y = -1;
    b.tables = &fg->t;
    combat::occupancy(b);
    return true;
}
// A plain creature: team, HP, Hit Dice, saves 15, no effects
void put(int i, int team, int x, int y, int hp, int hd = 1)
{
    combat::Fighter& f = F(i);
    f.size = 1;
    f.x = x;
    f.y = y;
    f.rec[0x197] = static_cast<uint8_t>(team);
    f.rec[0x1A4] = f.rec[0x78] = static_cast<uint8_t>(hp);
    f.rec[0x195] = party::Okay;
    f.rec[0x196] = 1;
    for (int k = 0; k < 5; ++k) f.rec[0xDF + k] = 15;
    if (f.n_aff) *f.n_aff = 0;
    f.rec[0xE5] = static_cast<uint8_t>(hd);
    f.rec[0x19A] = f.rec[0x19B] = 50;
    f.flags2 = 0;
    f.magic_res = 0;
    f.fleeing = false;
    combat::occupancy(fg->b);
}
void caster_mu(int i, int level)
{
    combat::Fighter& f = F(i);
    for (int k = 0; k < 8; ++k) f.rec[0x109 + k] = f.rec[0x111 + k] = 0;
    f.rec[0x109 + classes::MagicUser] = static_cast<uint8_t>(level);
    f.rec[0x74] = 7;
}
combat::SpellLine L[24];
int nl = 0;
int cast(int caster, int s, std::initializer_list<int> tg)
{
    const auto& fl = d->prof->fight;
    const combat::FightSpell* fs = combat::fight_spell(fl.spells, fl.n_spells, s);
    if (!fs) return -1;
    int t[24], n = 0;
    for (int i : tg) t[n++] = i;
    nl = combat::cast(fg->b, mrules->tables, caster, s, *fs, t, n, rng, L, 24, 0);
    return nl;
}
int aff_time(int i, int type)
{
    const combat::Fighter& f = F(i);
    for (int k = 0; f.n_aff && k < *f.n_aff; ++k)
        if (f.aff[k][0] == type) return f.aff[k][1] | f.aff[k][2] << 8;
    return -1;
}
int amount(int who)
{
    for (int k = 0; k < nl; ++k)
        if (L[k].who == who && L[k].did == combat::Did::Damage) return L[k].amount;
    return 0;
}
void spells_past_100()
{
    if (!new_fight()) {
        expect(P, "a fight to cast in", false);
        return;
    }
    char w[100];
    const int c = 0, t1 = 6, t2 = 7, t3 = 8;
    // Power Word Stun (117): no save; by the target's hit points 1-30 4d4,
    // 31-60 2d4, 61-90 1d4 rounds of effect 0x6A, more none (dice high)
    const auto* pws = b_row("pws", 30);
    const int stun = d->prof->fight.facts.sp.stun;
    for (int hp : {25, 50, 80, 100}) {
        put(c, 0, 10, 10, 50);
        caster_mu(c, 12);
        put(t1, 1, 12, 10, hp);
        s_dice(1);
        cast(c, 0x75, {t1});
        int want = 0;
        for (int k = 0; pws && k < 3; ++k)
            if (want == 0 && hp <= (*pws)[k * 3]) want = (*pws)[k * 3 + 1] * (*pws)[k * 3 + 2];
        const int got = aff_time(t1, stun);
        snprintf(w, sizeof w, "Power Word Stun on %d hit points", hp);
        expect(P, w, want ? got == want : got < 0, "%d rounds, the original's %d", got, want);
    }
    // Death Spell (110): 2 x 4d20 points (dice high: 160) spent on the
    // targets by their Hit Dice; no save; flags 2 & 8 left be; 9+ never
    const auto* cost = b_row("death_cost", 1);
    put(c, 0, 10, 10, 50);
    caster_mu(c, 14);
    put(t1, 1, 12, 10, 40, 4);
    put(t2, 1, 13, 10, 40, 2);
    put(t3, 1, 12, 11, 40, 9);
    F(t2).flags2 = 8;
    s_dice(1);
    cast(c, 0x6E, {t1, t2, t3});
    expect(P, "Death Spell: a 4 Hit Dice target drops dead", F(t1).status() == party::Dead && cost && (*cost)[4] <= 160);
    expect(P, "Death Spell: flags 2 & 8 left be", F(t2).status() == party::Okay);
    expect(P, "Death Spell: 9 Hit Dice never", F(t3).status() == party::Okay);
    // 160 points: ten of 4 Hit Dice (16 each) - all ten would be 160
    // Disintegrate (111) / Flesh to Stone (114): the save against spells
    for (int sp : {0x6F, 0x72})
        for (int roll : {1, 20}) {
            put(c, 0, 10, 10, 50);
            caster_mu(c, 12);
            put(t1, 1, 12, 10, 40);
            s_dice(1, roll);
            cast(c, sp, {t1});
            const int want = roll == 1 ? (sp == 0x6F ? party::Dead : party::Stoned) : party::Okay;
            snprintf(w, sizeof w, "%s, d20 %d", sp == 0x6F ? "Disintegrate" : "Flesh to Stone", roll);
            expect(P, w, F(t1).status() == want, "status %d, the original's %d", F(t1).status(), want);
        }
    // Delayed Blast Fireball (115) at once: level d6 + level, a save for half
    const auto* dbf = b_row("dbf", 6);
    for (int roll : {1, 20}) {
        put(c, 0, 10, 10, 50);
        caster_mu(c, 12);
        put(t1, 1, 14, 10, 200);
        s_dice(1, roll);
        fg->b.aim_x = 14;
        fg->b.aim_y = 10;
        cast(c, 0x73, {t1});
        fg->b.aim_x = fg->b.aim_y = -1;
        const int full = dbf ? 12 * (*dbf)[0] + 12 : -1;
        const int want = roll == 20 ? full / 2 : full;
        snprintf(w, sizeof w, "Delayed Blast Fireball at once, level 12, d20 %d", roll);
        expect(P, w, amount(t1) == want, "%d, the original's %d", amount(t1), want);
    }
    create::g_die_hook = nullptr;
}

// ---- Its monsters' abilities (secret_facts_2.md 15): a blow by a monster
// with its effect, the d20 set
void give(int i, int type)
{
    combat::Fighter& f = F(i);
    if (!f.n_aff || *f.n_aff >= f.max_aff) return;
    uint8_t* a = f.aff[(*f.n_aff)++];
    memset(a, 0, party::kAffectSize);
    a[0] = static_cast<uint8_t>(type);
    a[1] = a[2] = 0xFF;
}
int hit_with(int a, int c, int d20)
{
    // one blow of 1d4 (to hit 45 against AC 50: a 19 or 20 hits)
    combat::Fighter& f = F(a);
    f.attacks[0] = 1;
    f.attacks[1] = 0;
    f.rec[0x199] = 45;
    f.rec[0x19E] = 1;
    f.rec[0x1A0] = 4;
    f.rec[0x1A2] = 0;
    F(c).received = 0;
    F(c).turns = 0;
    s_dice(1, d20);
    const combat::Attack r = combat::attack(fg->b, a, c, names, rng, false);
    create::g_die_hook = nullptr;
    int hits = 0;
    for (int k = 0; k < r.n; ++k) hits += r.hits[k].hit;
    return hits;
}
void monster_abilities()
{
    if (!new_fight()) {
        expect(P, "a fight", false);
        return;
    }
    const auto& mf = d->prof->fight.facts.mon;
    const int a = 6, t = 0;
    char w[100];
    // The cockatrice (0x41): its hit - a save against petrification or stoned
    for (int save : {20, 2}) {
        put(a, 1, 10, 10, 40, 5);
        give(a, mf.petrify_hit);
        put(t, 0, 11, 10, 60);
        for (int k = 0; k < 5; ++k) F(t).rec[0xDF + k] = static_cast<uint8_t>(save);
        const int hits = hit_with(a, t, 19);
        const int want = save == 20 ? party::Stoned : party::Okay;
        snprintf(w, sizeof w, "the cockatrice's hit, the save %s", save == 20 ? "failed" : "made");
        expect(P, w, hits > 0 && F(t).status() == want, "%d hits, status %d, the original's %d", hits, F(t).status(), want);
    }
    // The remorhaz (0x42): a natural 20 swallows whole (dead), a 19 doesn't
    for (int roll : {20, 19}) {
        put(a, 1, 10, 10, 60, 7);
        give(a, mf.swallow);
        put(t, 0, 11, 10, 120);
        hit_with(a, t, roll);
        const bool dead = F(t).status() == party::Dead;
        snprintf(w, sizeof w, "the remorhaz's d20 %d", roll);
        expect(P, w, dead == (roll == 20), "status %d", F(t).status());
    }
    // The iron golem (0x3C): a blow without a +3 weapon does nothing
    put(t, 1, 11, 10, 80, 18);
    give(t, mf.need_plus3);
    put(a, 0, 10, 10, 60);
    F(a).n_items = 0;
    const int hits = hit_with(a, t, 19);
    expect(P, "the iron golem: a blow with no weapon does nothing", F(t).hp() == 80, "%d hits, HP %d", hits, F(t).hp());
}

// Every spell, monster, item and the rest (2026-10-18: Secret playable -
// the full checks; a file each)
#include "behave_secret_spells.inc"
#include "behave_secret_monsters.inc"
#include "behave_secret_items.inc"
#include "behave_secret_situations.inc"

} // namespace bsecret

static void behave_secret()
{
    using namespace bsecret;
    b_oracle.clear();
    if (!b_oracle_load("secret")) {
        expect(P, "oracle file", false, "no oracle_secret.txt");
        return;
    }
    if (!boot()) {
        expect(P, "boot", false);
        return;
    }
    if (!play::load_rules(bsecret::m)) {
        expect(P, "rule tables", false);
        return;
    }
    expect(P, "the Secret tables' shapes", m->tables.lay.secret);
    const char* only = getenv("ONLY");
    auto want = [&](const char* s) { return !only || strstr(only, s); };
    if (want("values")) class_values();
    if (want("train")) training();
    if (want("level")) level();
    if (want("vault")) vault_file();
    if (want("spells")) spells_past_100();
    if (want("monsters")) monster_abilities();
    if (want("allspells")) all_spells();
    if (want("allmonsters")) all_monsters();
    if (want("allitems")) all_items();
    if (want("allsituations")) all_situations();
    delete m;
    m = nullptr;
}

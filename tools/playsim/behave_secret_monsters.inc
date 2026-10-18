// Behaviour checks: Secret of the Silver Blades - monsters (included by
// behave_secret.inc inside namespace bsecret; ONLY=allmonsters). Expected values
// from Secret's own program and tables (the oracle, scratch) and the
// Project's claude/secret_facts.md / secret_facts_2.md - never from the
// engine's own code. Every monster of MON1CHA (its record, MON1ITM items,
// MON1SPC effects) as the original loads it into a fight, and each effect
// its monsters use (the handlers of GAME.OVR's effect overlay, 0x10CCB on).
// MPART=a,b,c,d picks sections.

namespace mons {

const char* Q = "secret monsters";
int sb(uint8_t v) { return static_cast<int8_t>(v); }
int ot(const char* key, int i) { return ocol(key, i); }
bool in_row(const char* key, int v)
{
    const auto& r = b_rows(key);
    if (r.empty()) return false;
    for (int x : r[0])
        if (x == v) return true;
    return false;
}
bool part(const char* p)
{
    const char* s = getenv("MPART");
    return !s || strstr(s, p);
}

// ---- The player's monster file: every block of MON1CHA
std::vector<int> ids()
{
    std::vector<int> out;
    char path[300];
    snprintf(path, sizeof path, "/GOLDBOX/%s/MON1CHA.DAX", game_dir.substr(game_dir.rfind('/') + 1).c_str());
    plat::File f = plat::open(path, "r");
    if (!f) return out;
    plat::FileSource src(f);
    static dax::Index dir;
    if (dax::read_index(src, dir) == dax::Status::Ok)
        for (int i = 0; i < dir.count; ++i) out.push_back(dir.entries[i].id);
    f.close();
    return out;
}

// Secret's own record (439 bytes - its offsets), the items and effects
struct Raw {
    uint8_t s[party::kSecretRecordSize];
    uint8_t item[16][items::kRecordSize];
    int     n_items = 0;
    uint8_t aff[16][party::kAffectSize];
    int     n_aff = 0;
    char    name[20];
    int     u16(int o) const { return s[o] | s[o + 1] << 8; }
    bool    has(int e) const
    {
        for (int k = 0; k < n_aff; ++k)
            if (aff[k][0] == e) return true;
        return false;
    }
};
bool raw(int id, Raw& r)
{
    if (play::read_block("MON1CHA.DAX", id, r.s, sizeof r.s) != static_cast<int>(sizeof r.s)) return false;
    int n = play::read_block("MON1ITM.DAX", id, &r.item[0][0], sizeof r.item);
    r.n_items = n > 0 ? n / static_cast<int>(items::kRecordSize) : 0;
    n = play::read_block("MON1SPC.DAX", id, &r.aff[0][0], sizeof r.aff);
    r.n_aff = n > 0 ? n / static_cast<int>(party::kAffectSize) : 0;
    const int len = r.s[0] < 15 ? r.s[0] : 15;
    memcpy(r.name, r.s + 1, static_cast<size_t>(len));
    r.name[len] = 0;
    return true;
}

// ---- What Secret's recalculation (GAME.OVR 0x3A292, run on every one of
// a fight at its setup and when the computer readies a weapon) makes of a
// record and its readied items - in Secret's own offsets: THAC0 0x6A, base
// AC 0x129, base dice 0x123-0x128, base movement 0xED, uses Strength
// 0x12A, money 0x103; the weapon's part 0x38151, armour 0x383C9 / 0x3833F
struct Values {
    int hit, ac, ac_behind, move, d1, s1, b1, d2, s2, b2;
};
int str_group(const uint8_t* s)
{
    const int st = s[0x11], pct = s[0x1C];
    if (st <= 17) return st;
    if (st == 18) return pct == 0 ? 18 : pct <= 50 ? 19 : pct <= 75 ? 20 : pct <= 90 ? 21 : pct <= 99 ? 22 : 23;
    return st <= 25 ? st + 5 : 0;
}
Values expected(const Raw& r, const uint8_t (*it)[items::kRecordSize], int n)
{
    using namespace play;
    const uint8_t* s = r.s;
    Values v{};
    const bool uses_str = s[0x12A] != 0;
    const int g = str_group(s);
    const int sh = uses_str ? ot("m_str_hit", g) : 0, sd = uses_str ? ot("m_str_dmg", g) : 0;
    const int dex = s[0x17];
    const uint8_t* weapon = nullptr;
    const uint8_t* arrows = nullptr;
    const uint8_t* quarrels = nullptr;
    int weight = 0;
    for (int k = 0; k < n; ++k) {
        const items::Item item{it[k]};
        int wt = item.weight();
        if (item.count() > 0) wt *= item.count();
        weight += wt;
        if (!it[k][0x34]) continue;
        const items::TypeInfo& ti = names->type(it[k][0x2E]);
        if (ti.slot == items::kSlotWeapon) weapon = it[k];
        if (it[k][0x2E] == ot("m_arrow", 0)) arrows = it[k];
        if (it[k][0x2E] == ot("m_quarrel", 0)) quarrels = it[k];
    }
    for (int m = 0; m < 7; ++m) weight += r.u16(0x103 + 2 * m);
    v.d1 = s[0x123];
    v.d2 = s[0x124];
    v.s1 = s[0x125];
    v.s2 = s[0x126];
    v.b1 = sb(s[0x127]);
    v.b2 = sb(s[0x128]);
    v.hit = sb(s[0x6A]);
    v.move = s[0xED];
    int bonus[5] = {ot("m_dex_ac", dex), 0, 0, 0, 0};
    if (!weapon) {
        v.hit += sh;
        v.b1 += sd;
    } else {
        const items::TypeInfo& ti = names->type(weapon[0x2E]);
        if (ti.flags & 0x02) v.hit += ot("m_dex_react", dex);
        v.b1 = ti.bonus;
        if (ti.flags & 0x04) {
            v.hit += sh;
            v.b1 += sd;
        }
        int plus = sb(weapon[0x32]);
        if ((ti.flags & 0x80) && quarrels) plus += sb(quarrels[0x32]);
        if ((ti.flags & 0x01) && arrows) plus += sb(arrows[0x32]);
        v.b1 += plus;
        // (Secret: any race - GAME.OVR 0x382C6; AD&D's elves only with the fixes on)
        if (in_row("m_hit_types", weapon[0x2E]) && (!rules::fixes() || s[0x6B] == 1)) ++plus;
        v.hit += plus;
        v.d1 = ti.dice;
        v.s1 = ti.sides;
    }
    bool magic_armour = false;
    for (int k = 0; k < n; ++k) {
        if (!it[k][0x34]) continue;
        const items::Item item{it[k]};
        const items::TypeInfo& ti = names->type(it[k][0x2E]);
        if (ti.slot == items::kSlotArmour) {
            const int w = item.weight();
            v.move = w <= 150 ? s[0xED] : w <= 399 ? 9 : 6;
            // (+3 only for armour with a plus - GAME.OVR 0x38399 tests byte 0x32)
            if (v.move != 0 && v.move <= 9 && it[k][0x32] != 0) v.move += 3;
        }
        if (ti.ac > 0x7F) {
            const int a = ti.ac & 0x7F;
            const int plus = sb(it[k][0x32]);
            if (ti.slot == 1) bonus[1] = plus + a;
            else if (a == 0) {
                if (ti.slot == 9) {
                    if (plus > bonus[3]) bonus[3] = plus;
                } else {
                    bonus[2] += plus;
                }
            } else if (plus + a > bonus[4]) {
                bonus[4] = plus + a;
                if (plus > 0 && ti.slot == items::kSlotArmour) magic_armour = true;
            }
        }
    }
    if (magic_armour) bonus[3] = 0;
    int over = weight - ot("m_max_enc", g);
    if (over < 0) over = 0;
    const int can = over <= 0x200 ? v.move : over <= 0x300 ? 9 : over <= 0x400 ? 6 : 3;
    if (can < v.move) v.move = can;
    if (bonus[4] < s[0x129]) bonus[4] = s[0x129];
    v.ac = (bonus[0] + bonus[1] + bonus[2] + bonus[3] + bonus[4]) & 0xFF;
    v.ac_behind = (bonus[4] + bonus[2] + bonus[3] - 2) & 0xFF;
    v.hit = sb(static_cast<uint8_t>(v.hit));
    return v;
}

// A fight against `copies` of MON1 #id at the fire knives' LOAD MONSTER
// (848A), the Level as asked (2: Veteran, the GOG save's)
bool fight(int id, int copies, int level = 2)
{
    using namespace play;
    if (!boot()) return false;
    vm->set(0x4CF4, static_cast<uint16_t>(level));
    // (start_fight's: block 0 too - the basilisk)
    const uint16_t a = 0x848A;
    d->gs.game_area = 1;
    vm->set(a + 2, static_cast<uint16_t>(id));
    vm->set(a + 4, static_cast<uint16_t>(copies));
    vm->set(a + 6, static_cast<uint16_t>(id));
    then = Then::Idle;
    handle(vm->run(a));
    for (int k = 0; k < 400 && screen != Screen::Fight; ++k) {
        g_now += 20;
        tick(g_now, C);
        if (waiting && vm->wait() == ecl::Wait::Menu) tap_item(0);
        if (waiting && vm->wait() == ecl::Wait::Print && page_prompt) tap(10, 10, C);
    }
    return screen == Screen::Fight && fg;
}

// ---- A: every monster of MON1CHA in a fight
void every_monster(bool fixes_on)
{
    using namespace play;
    int checked = 0;
    rules::set_fixes(fixes_on);
    const char* Q = fixes_on ? "secret monsters (fixes on)" : "secret monsters";
    for (int id : ids()) {
        Raw r;
        if (!raw(id, r)) {
            expect(Q, "monster record", false, "#%d", id);
            continue;
        }
        dice_usual();
        if (!fight(id, 4)) {
            expect(Q, "a fight with it", false, "#%d %s", id, r.name);
            continue;
        }
        combat::Battle& b = fg->b;
        char tag[64];
        snprintf(tag, sizeof tag, "#%d %s", id, r.name);
        const uint8_t* s = r.s;
        // LOAD MONSTER copies the record as it is (GAME.OVR 0x101A: a copy of
        // the 0x1B7 bytes for each; nothing rolled) with its effects and items
        const int first = b.party_size;
        int copies = 0;
        bool same_hp = true, same_aff = true, same_items = true, same_x = true;
        for (int i = first; i < b.n; ++i) {
            const combat::Fighter& f = b.f[i];
            ++copies;
            if (f.hp() != s[0x1B5] || f.hp_max() != s[0x70]) same_hp = false;
            if (!f.n_aff || *f.n_aff < r.n_aff || memcmp(f.aff, r.aff, static_cast<size_t>(r.n_aff) * 9) != 0) same_aff = false;
            if (f.n_items != r.n_items) same_items = false;
            for (int k = 0; k < r.n_items && k < f.n_items; ++k)
                if (memcmp(f.items[k] + 0x2E, r.item[k] + 0x2E, 0x3F - 0x2E) != 0 && f.items[k][0x2E] != r.item[k][0x2E])
                    same_items = false;
            if (f.flags2 != s[0x15E] || f.magic_res != s[0x1A5]) same_x = false;
        }
        expect(Q, "LOAD MONSTER: 4 copies", copies == 4, "%s: %d", tag, copies);
        expect(Q, "hit points as the record (0x1B5 / 0x70, not rolled)", same_hp, "%s: record %d/%d, in the fight %d/%d", tag,
               s[0x1B5], s[0x70], b.f[first].hp(), b.f[first].hp_max());
        expect(Q, "its effects (MON1SPC) as the file", same_aff, "%s: %d effects", tag, r.n_aff);
        expect(Q, "its items (MON1ITM) as the file", same_items, "%s: %d items, fighter %d", tag, r.n_items, b.f[first].n_items);
        expect(Q, "flags 2 (0x15E) and magic resistance (0x1A5) kept", same_x, "%s: %02X %d", tag, b.f[first].flags2,
               b.f[first].magic_res);
        const combat::Fighter& f = b.f[first];
        const uint8_t* fr = f.rec;
        // Its record's other fight bytes in the engine's places: side (0x1A8),
        // size (0xE7), undead type (0xF2), monster kind flags (0x15D),
        // Hit Dice (0xEE), saves (0xE8-0xEC), class levels (0x111)
        bool bytes = fr[0x197] == s[0x1A8] && fr[0xDE] == s[0xE7] && fr[0xE9] == s[0xF2] && fr[0x14B] == s[0x15D] &&
                     fr[0xE5] == s[0xEE] && fr[0x11B] == s[0x120] && fr[0x119] == s[0x11F] && fr[0xF7] == s[0xFF];
        for (int k = 0; k < 5; ++k) bytes = bytes && fr[0xDF + k] == s[0xE8 + k];
        for (int k = 0; k < 7; ++k) bytes = bytes && fr[0x109 + k] == s[0x111 + k];
        expect(Q, "side, size, undead type, kind flags, Hit Dice, alignment, sex, control, saves, levels from the record", bytes,
               "%s: side %d size %d undead %d kind %02X HD %d al %d/%d sex %d/%d control %02X/%02X", tag, fr[0x197], fr[0xDE],
               fr[0xE9], fr[0x14B], fr[0xE5], fr[0x11B], s[0x120], fr[0x119], s[0x11F], fr[0xF7], s[0xFF]);
        expect(Q, "its footprint (record 0xE7)", f.gone || f.size == (s[0xE7] & 7), "%s: size %d, record %d", tag, f.size,
               s[0xE7] & 7);
        // The values the original works out at the battle's setup
        const Values v = expected(r, r.item, r.n_items);
        // (the computer readies the group's weapons: compare with the items the fighter holds now)
        const Values vf = expected(r, f.items, f.n_items);
        const bool vals = sb(fr[0x199]) == vf.hit && fr[0x19A] == vf.ac && fr[0x19B] == vf.ac_behind && fr[0x1A5] == vf.move;
        expect(Q, "to-hit, AC, rear AC, movement (the setup's recalculation)", vals,
               "%s: hit %d (want %d), AC %d (%d), rear %d (%d), move %d (%d)", tag, sb(fr[0x199]), vf.hit, fr[0x19A], vf.ac,
               fr[0x19B], vf.ac_behind, fr[0x1A5], vf.move);
        const bool dice = fr[0x19E] == vf.d1 && fr[0x1A0] == vf.s1 && sb(fr[0x1A2]) == vf.b1 && fr[0x19F] == vf.d2 &&
                          fr[0x1A1] == vf.s2 && sb(fr[0x1A3]) == vf.b2;
        expect(Q, "attack dice (the weapon's, else the record's)", dice, "%s: %dd%d%+d / %dd%d%+d (want %dd%d%+d / %dd%d%+d)", tag,
               fr[0x19E], fr[0x1A0], sb(fr[0x1A2]), fr[0x19F], fr[0x1A1], sb(fr[0x1A3]), vf.d1, vf.s1, vf.b1, vf.d2, vf.s2, vf.b2);
        if (v.hit != vf.hit || v.d1 != vf.d1)
            printf("  (%s: readied otherwise in the fight - the file's: hit %d %dd%d)\n", tag, v.hit, v.d1, v.s1);
        // A round: moves = movement x 2 (half squares); attacks (half counts
        // 0x121, 0x122): (half + 1 when the round count is odd) / 2
        // (GAME.OVR 0x15DFC), the count 0 in the first round
        dice_faces({6});
        b.round = 0;
        combat::start_round(b, rng);
        int half1 = s[0x121], half2 = s[0x122], missiles = 0;
        for (int k = 0; k < f.n_items; ++k) {
            if (!f.items[k][0x34] || names->type(f.items[k][0x2E]).slot != 0) continue;
            const items::TypeInfo& ti = names->type(f.items[k][0x2E]);
            if (ti.range <= 1) continue;
            const uint8_t* shot = (ti.flags & 0x10) ? f.items[k] : nullptr;
            for (int j = 0; (ti.flags & 0x08) && j < f.n_items; ++j)
                if (f.items[j][0x34] && (((ti.flags & 1) && f.items[j][0x2E] == ot("m_arrow", 0)) ||
                                         ((ti.flags & 0x80) && f.items[j][0x2E] == ot("m_quarrel", 0))))
                    shot = f.items[j];
            if (shot || ti.flags == 0x0A) {
                half1 = ti.attacks < 2 ? 2 : ti.attacks;
                missiles = shot ? shot[0x39] : 0;
                printf("  (%s: a missile weapon, type %d: %d half attacks, %d missiles)\n", tag, f.items[k][0x2E], half1, missiles);
            }
        }
        auto capm = [&](int a) { return missiles > 0 && missiles < a ? missiles : a; };
        int mv = vf.move;
        if (mv < 1 || mv > 96) mv = 1;
        if (!f.gone && f.size) {
            expect(Q, "round 1 (count 0): moves = movement x 2", f.moves == mv * 2, "%s: %d (movement %d)", tag, f.moves, mv);
            expect(Q, "round 1 (count 0): attacks = half attacks / 2 (slot 1, slot 2)",
                   f.attacks[0] == capm(half1 / 2) && f.attacks[1] == half2 / 2, "%s: %d / %d (half %d / %d)", tag, f.attacks[0],
                   f.attacks[1], half1, half2);
        }
        b.round = 1;
        combat::start_round(b, rng);
        if (!f.gone && f.size)
            expect(Q, "round 2 (count 1): attacks = (half attacks + 1) / 2",
                   f.attacks[0] == capm((half1 + 1) / 2) && f.attacks[1] == (half2 + 1) / 2, "%s: %d / %d", tag, f.attacks[0],
                   f.attacks[1]);
        // Experience when beaten (GAME.OVR 0x6B4A): 0x150 x 0x131 + 0x14E for
        // each of the other side (side 1) neither okay nor fled
        for (int i = first; i < b.n; ++i) {
            b.f[i].rec[0x195] = party::Dead;
            b.f[i].rec[0x196] = 0;
        }
        const combat::Outcome o = combat::finish(b, ml ? ml->m : nullptr);
        int placed = 0;
        for (int i = first; i < b.n; ++i)
            if (!b.f[i].gone) ++placed;
        const int want = s[0x1A8] != 1 ? 0 : placed * (s[0x150] * s[0x131] + r.u16(0x14E));
        expect(Q, "experience: 0x150 x 0x131 + 0x14E each beaten one", o.exp == want, "%s: %d (want %d, %d placed)", tag, o.exp,
               want, placed);
        dice_usual();
        ++checked;
    }
    printf("  (%d monsters)\n", checked);
    rules::set_fixes(true);
}

// ---- A2: the Level (GAME.OVR 0x101A) on every monster: the other side's
// hit points and maximum x (level + 2) / 4 kept in a byte, both 1 when
// either comes to 0; the party's side as it is
void every_level()
{
    using namespace play;
    if (!boot()) return;
    for (int id : ids()) {
        Raw r;
        if (!raw(id, r)) continue;
        for (int L = 0; L < 5; ++L) {
            vm->set(0x4CF4, static_cast<uint16_t>(L));
            fight_clear_monsters();
            fight_load_monster(id, 1, id);
            const int f = ot("m_level_mul", L);
            int want = r.s[0x1B5], want_max = r.s[0x70];
            if (r.s[0x1A8] == 1) {
                want = r.s[0x1B5] * f / 4 & 0xFF;
                want_max = r.s[0x70] * f / 4 & 0xFF;
                if (!want || !want_max) want = want_max = 1;
            }
            const bool ok = ml && ml->n == 1 && ml->m[0].rec[0x1A4] == want && ml->m[0].rec[0x78] == want_max;
            char w[80];
            snprintf(w, sizeof w, "the Level %d: #%d %s's hit points", L, id, r.name);
            expect(Q, w, ok, "%d / %d, the original's %d / %d", ml && ml->n ? ml->m[0].rec[0x1A4] : -1,
                   ml && ml->n ? ml->m[0].rec[0x78] : -1, want, want_max);
        }
    }
    fight_clear_monsters();
    vm->set(0x4CF4, 2);
}

// ---- B: each effect Secret's monsters use (MON1SPC: 06 18 25 38 3A 3B 3C
// 40 41 42 43 46 49 4F 50 51 53 56 58 5A 5C 5D 60 62 63 67 68), its handler
// in GAME.OVR (the effect overlay 0x10CCB; table at 0x14637) against the
// engine's code path, plain creatures in the fire knives' fight
// The dice: a queue of (sides, face) taken when the sides match, else the
// highest (or lowest) face
std::vector<std::pair<int, int>> dq;
bool d_low = false;
int q_die(int n)
{
    for (size_t k = 0; k < dq.size(); ++k)
        if (dq[k].first == n) {
            const int v = dq[k].second;
            dq.erase(dq.begin() + static_cast<long>(k));
            return v - 1;
        }
    return d_low ? 0 : n - 1;
}
void dice(std::initializer_list<std::pair<int, int>> q, bool low = false)
{
    dq.assign(q.begin(), q.end());
    d_low = low;
    create::g_die_hook = q_die;
}
void no_dice()
{
    dq.clear();
    create::g_die_hook = nullptr;
}
// Plain items for blows (synthetic: type, plus, readied)
uint8_t wpn[2][items::kRecordSize];
void arm(int i, int type, int plus)
{
    memset(wpn[i & 1], 0, sizeof wpn[0]);
    wpn[i & 1][0x2E] = static_cast<uint8_t>(type);
    wpn[i & 1][0x32] = static_cast<uint8_t>(plus);
    wpn[i & 1][0x34] = 1;
    F(i).items = wpn + (i & 1);
    F(i).n_items = 1;
}
void unarm(int i)
{
    F(i).items = nullptr;
    F(i).n_items = 0;
}
// One blow from a to c (the slot: 0 attack 1, 1 attack 2) - 1d4, to-hit
// 45 against AC 50, the d20 as asked (then the rest of the queue)
combat::Attack blow(int a, int c, int slot, int d20, std::initializer_list<std::pair<int, int>> rest = {})
{
    combat::Fighter& f = F(a);
    f.attacks[0] = slot == 0 ? 1 : 0;
    f.attacks[1] = slot == 1 ? 1 : 0;
    f.rec[0x199] = 45;
    f.rec[0x19E] = f.rec[0x19F] = 1;
    f.rec[0x1A0] = f.rec[0x1A1] = 4;
    f.rec[0x1A2] = f.rec[0x1A3] = 0;
    f.target = c;
    F(c).received = 0;
    F(c).turns = 0;
    F(c).rec[0x19A] = 50;
    std::vector<std::pair<int, int>> q{{20, d20}};
    q.insert(q.end(), rest.begin(), rest.end());
    dq = q;
    d_low = false;
    create::g_die_hook = q_die;
    const combat::Attack r = combat::attack(fg->b, a, c, names, rng, false);
    no_dice();
    return r;
}
int dmg_of(const combat::Attack& r)
{
    int n = 0;
    for (int k = 0; k < r.n; ++k)
        if (r.hits[k].hit) n += r.hits[k].damage;
    return n;
}
bool hits(const combat::Attack& r)
{
    for (int k = 0; k < r.n; ++k)
        if (r.hits[k].hit) return true;
    return false;
}
void saves(int i, int v)
{
    for (int k = 0; k < 5; ++k) F(i).rec[0xDF + k] = static_cast<uint8_t>(v);
}
int harm_of(int c, int amount, int kind, int level = 0)
{
    combat::Harm h;
    h.kind = kind;
    h.level = level;
    return combat::harm(fg->b, c, amount, h, rng, nullptr);
}
combat::Event evs[32];
int nev = 0;
bool ends = false;
int special(int i)
{
    g_now += 0;
    nev = combat::special(fg->b, fg->t, i, rng, evs, 32, &ends);
    return nev;
}
bool said(combat::Ev e, int who = -1)
{
    for (int k = 0; k < nev; ++k)
        if (evs[k].ev == e && (who < 0 || evs[k].who == who)) return true;
    return false;
}
void show()
{
    if (!getenv("MDEBUG")) return;
    for (int k = 0; k < nev; ++k) printf("   ev %d who %d amount %d kind %d\n", static_cast<int>(evs[k].ev), evs[k].who, evs[k].amount, evs[k].kind);
}
int damage_to(int who)
{
    show();
    int n = 0;
    for (int k = 0; k < nev; ++k)
        if (evs[k].ev == combat::Ev::Damage && evs[k].who == who) n += evs[k].amount;
    return n;
}
int combat_find(int i, int type)
{
    for (int k = 0; F(i).n_aff && k < *F(i).n_aff; ++k)
        if (F(i).aff[k][0] == type) return k;
    return -1;
}
// Everyone off the field but these (the others' size 0)
void only(std::initializer_list<int> keep)
{
    combat::Battle& b = fg->b;
    for (int i = 0; i < b.n; ++i) {
        bool k = false;
        for (int j : keep) k = k || j == i;
        if (!k) b.f[i].size = 0;
        if (b.f[i].n_aff) *b.f[i].n_aff = 0;
    }
    // (the fallen leave their bodies on the ground: plain floor again)
    const int floor = b.ground[0][0];
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = static_cast<uint8_t>(floor);
    b.round = 0;
    combat::occupancy(b);
}

void effects()
{
    using namespace play;
    char w[120];
    if (!new_fight()) {
        expect(Q, "a fight", false);
        return;
    }
    combat::Battle& b = fg->b;
    const int m = 6, p = 0, p2 = 1, m2 = 7;
    // 06 / 5D / 62 (GAME.OVR 0x11082 / 0x13B99 / 0x13D10, its phase 6 - the
    // damage routine's): damage of kind & 4 / 1 / 2 none (breath's 0x21 /
    // 0x22 too), other kinds as they come
    const struct { int fx, kind, other; const char* what; } imm[] = {
        {0x06, 4, 1, "electricity"}, {0x5D, 1, 2, "fire"}, {0x5D, 0x21, 0x10, "fire breath"}, {0x62, 2, 4, "cold"},
        {0x62, 0x22, 8, "cold breath"}};
    for (const auto& c : imm) {
        only({m});
        put(m, 1, 10, 10, 100, 10);
        give(m, c.fx);
        dice({});
        const int a = harm_of(m, 20, c.kind), o = harm_of(m, 20, c.other);
        no_dice();
        snprintf(w, sizeof w, "effect %02X: no %s damage, other kinds whole", c.fx, c.what);
        expect(Q, w, a == 0 && o == 20 && F(m).hp() == 80, "%d / %d, HP %d", a, o, F(m).hp());
    }
    // 49 (0x1310F, phase 6): damage of kind & 0x20 (breath) none
    only({m});
    put(m, 1, 10, 10, 100, 10);
    give(m, 0x49);
    dice({});
    {
        const int a = harm_of(m, 30, 0x21), c = harm_of(m, 30, 0x22), o = harm_of(m, 30, 1);
        no_dice();
        expect(Q, "effect 49 (the dragons): no breath damage, a fire spell whole", a == 0 && c == 0 && o == 30, "%d %d %d", a,
               c, o);
    }
    // 4F (0x13354, phases 6 and 9 - the iron golem): fire and magic heals by
    // the damage (to its maximum), electricity slows it 3 rounds; nothing
    // else hurts it; a spell's effect never takes
    {
        only({m});
        put(m, 1, 10, 10, 80, 18);
        F(m).rec[0x1A4] = 50;
        give(m, 0x4F);
        dice({});
        const int fire = harm_of(m, 20, 9, 10);
        expect(Q, "effect 4F: fire and magic heals by the damage", fire == 0 && F(m).hp() == 70, "%d, HP %d", fire, F(m).hp());
        harm_of(m, 40, 9, 10);
        expect(Q, "effect 4F: healed to its maximum", F(m).hp() == 80, "HP %d", F(m).hp());
        const int el = harm_of(m, 30, 0x0C, 10);
        expect(Q, "effect 4F: electricity slows it 3 rounds, no damage", el == 0 && aff_time(m, d->prof->fight.facts.slow) == ot("m_golem_slow", 0) &&
                                                                           F(m).hp() == 80,
               "%d, slowed %d, HP %d", el, aff_time(m, d->prof->fight.facts.slow), F(m).hp());
        const int ac = harm_of(m, 30, 0x10), br = harm_of(m, 30, 0x21), mg = harm_of(m, 30, 8, 10), none = harm_of(m, 30, 0);
        expect(Q, "effect 4F: acid, fire breath, magic, plain damage nothing", ac == 0 && br == 0 && mg == 0 && none == 0 &&
                                                                                F(m).hp() == 80,
               "%d %d %d %d", ac, br, mg, none);
        combat::Harm h;
        h.level = 12;
        expect(Q, "effect 4F: a spell's effect (charm) resisted", combat::resists(b, m, d->prof->fight.facts.charm, h, rng));
        no_dice();
    }
    // 3C (0x125FB, phase 5 - a weapon's blow on it): none but a +3 weapon or better's
    for (int plus : {0, 2, 3}) {
        only({m, p});
        put(m, 1, 11, 10, 80, 18);
        give(m, 0x3C);
        put(p, 0, 10, 10, 60);
        arm(p, 18, plus);
        const combat::Attack r = blow(p, m, 0, 19);
        snprintf(w, sizeof w, "effect 3C (the iron golem): a +%d long sword's hit", plus);
        expect(Q, w, hits(r) && (plus >= 3 ? F(m).hp() < 80 : F(m).hp() == 80), "HP %d", F(m).hp());
        unarm(p);
    }
    // 5A (0x138B2, phase 5 - the giant slug): a blunt weapon's blow (its
    // type's byte 7 & 0x80: the mace) nothing; an edged one's and an empty
    // hand's whole
    {
        const struct { int type; bool hurts; const char* what; } bl[] = {{9, false, "a mace (blunt)"}, {18, true, "a long sword"}, {0, true, "no weapon"}};
        for (const auto& c : bl) {
            only({m, p});
            put(m, 1, 11, 10, 80, 12);
            give(m, 0x5A);
            put(p, 0, 10, 10, 60);
            if (c.type) arm(p, c.type, 0);
            else unarm(p);
            const combat::Attack r = blow(p, m, 0, 19);
            snprintf(w, sizeof w, "effect 5A (the giant slug): %s's hit %s", c.what, c.hurts ? "hurts" : "does nothing");
            expect(Q, w, hits(r) && (F(m).hp() < 80) == c.hurts, "HP %d (blow byte %02X)", F(m).hp(),
                   names->type(c.type).blow);
            unarm(p);
        }
        // and it never breathes acid (Curse's 0x5A was the black dragon's breath)
        only({m, p});
        put(m, 1, 10, 10, 54, 12);
        give(m, 0x5A);
        put(p, 0, 12, 10, 60);
        dice({});
        special(m);
        no_dice();
        expect(Q, "effect 5A: no breath of any kind", !said(combat::Ev::BreathesAcid) && damage_to(p) == 0, "%d events", nev);
    }
    // 67 (0x1421E, phase 5): a blow with no weapon or a +0 one does nothing
    // when the attacker is of a player race (1-6), or a monster under 4 Hit
    // Dice; a magic weapon always hurts
    {
        const struct { int race, hd, type, plus; bool hurts; const char* what; } nm[] = {
            {7, 1, 18, 0, false, "a human, a +0 sword"}, {7, 1, 18, 1, true, "a human, a +1 sword"},
            {7, 9, 0, 0, false, "a human of 9 HD, no weapon"}, {0, 4, 0, 0, true, "a monster of 4 HD, no weapon"},
            {0, 3, 0, 0, false, "a monster of 3 HD, no weapon"}, {0, 3, 18, 1, true, "a monster of 3 HD, a +1 sword"}};
        for (const auto& c : nm) {
            only({m, p});
            put(m, 1, 11, 10, 80, 6);
            give(m, 0x67);
            put(p, 0, 10, 10, 60, c.hd);
            F(p).rec[0x74] = static_cast<uint8_t>(c.race);
            if (c.type) arm(p, c.type, c.plus);
            else unarm(p);
            const combat::Attack r = blow(p, m, 0, 19);
            snprintf(w, sizeof w, "effect 67: %s %s", c.what, c.hurts ? "hurts" : "does nothing");
            expect(Q, w, hits(r) && (F(m).hp() < 80) == c.hurts, "HP %d", F(m).hp());
            unarm(p);
            F(p).rec[0x74] = 7;
        }
    }
    // 40 (0x12814): only after attack 2's hit (its list 3 - not attack 1's,
    // list 2: 0x41 0x56 0x58), the target saves against poison or dies; 56
    // (0x13821) the same at -2, after either attack; 58 (0x1383E -> 0x10CDF)
    // after either attack: a save against poison or paralysed 10 rounds
    {
        const int poisoned = d->prof->fight.facts.mon.poisoned, para = d->prof->fight.facts.mon.paralyzed;
        const struct { int fx, slot, save, d20; int status; bool para; const char* what; } ps[] = {
            {0x40, 1, 12, 11, party::Dead, false, "40 on attack 2, the save failed: dead"},
            {0x40, 1, 12, 12, party::Okay, false, "40 on attack 2, the save made"},
            {0x40, 0, 12, 11, party::Okay, false, "40 on attack 1: nothing"},
            {0x56, 0, 12, 13, party::Dead, false, "56 on attack 1, 13 against 12 at -2: dead"},
            {0x56, 1, 12, 14, party::Okay, false, "56 on attack 2, 14 against 12 at -2: made"},
            {0x58, 0, 12, 11, party::Okay, true, "58 on attack 1, the save failed: paralysed"},
            {0x58, 1, 12, 11, party::Okay, true, "58 on attack 2, the save failed: paralysed"},
            {0x58, 0, 12, 12, party::Okay, false, "58, the save made"}};
        for (const auto& c : ps) {
            only({m, p});
            put(m, 1, 11, 10, 60, 5);
            give(m, c.fx);
            put(p, 0, 10, 10, 60);
            saves(p, c.save);
            unarm(m);
            const combat::Attack r = blow(m, p, c.slot, 19, {{20, c.d20}});
            const int pt_ = aff_time(p, para);
            snprintf(w, sizeof w, "effect %s", c.what);
            expect(Q, w, hits(r) && F(p).status() == c.status && (c.para ? pt_ == ot("m_para", 0) : pt_ < 0),
                   "status %d, paralysed %d, poisoned %d", F(p).status(), pt_, aff_time(p, poisoned));
        }
    }
    // 41 (0x12844, after either attack): a save against petrification or stoned
    for (int slot : {0, 1})
        for (int d20 : {11, 12}) {
            only({m, p});
            put(m, 1, 11, 10, 60, 5);
            give(m, 0x41);
            put(p, 0, 10, 10, 60);
            saves(p, 12);
            const combat::Attack r = blow(m, p, slot, 19, {{20, d20}});
            snprintf(w, sizeof w, "effect 41 (the cockatrice) on attack %d, the save d20 %d against 12", slot + 1, d20);
            expect(Q, w, hits(r) && F(p).status() == (d20 < 12 ? party::Stoned : party::Okay), "status %d", F(p).status());
        }
    // 42 (0x128F1, phase 4 - its damage): a natural 20 swallows whole, either attack
    for (int slot : {0, 1})
        for (int d20 : {20, 19}) {
            only({m, p});
            put(m, 1, 11, 10, 60, 14);
            give(m, 0x42);
            put(p, 0, 10, 10, 120);
            blow(m, p, slot, d20);
            snprintf(w, sizeof w, "effect 42 (the remorhaz) on attack %d, d20 %d", slot + 1, d20);
            expect(Q, w, (F(p).status() == party::Dead) == (d20 == 20), "status %d", F(p).status());
        }
    // 25 (0x11C9D, phases 1 and 15 - blink): once it has acted this round
    // (its delay 0) blows miss it
    for (int delay : {0, 5}) {
        only({m, p});
        put(m, 1, 11, 10, 60, 5);
        give(m, 0x25);
        F(m).delay = delay;
        put(p, 0, 10, 10, 60);
        const combat::Attack r = blow(p, m, 0, 19);
        snprintf(w, sizeof w, "effect 25 (the phase spider), its delay %d", delay);
        expect(Q, w, hits(r) == (delay != 0), "hit %d", hits(r));
    }
    // 38 (0x12387, phases 8 - the fight's start - and 19 - the round's end):
    // invisible (0x19) for the round, again each round
    {
        const int inv = d->prof->fight.facts.invisible;
        only({m});
        put(m, 1, 11, 10, 60, 5);
        give(m, 0x38);
        combat::battle_start(b, &rng);
        const bool at_start = aff_time(m, inv) >= 0;
        for (int k = 0; F(m).n_aff && k < *F(m).n_aff; ++k)
            if (F(m).aff[k][0] == inv) F(m).aff[k][0] = 0x7F;      // (seen: gone)
        combat::tick(b);
        expect(Q, "effect 38 (the dreadlord): invisible at the start and again at a round's end", at_start && aff_time(m, inv) >= 0,
               "start %d, after the round %d", at_start, aff_time(m, inv));
        // 18 (no handler; looked for by 0x19's, 0x116EA): it sees the invisible - no -4 to hit them
        only({m, p});
        put(m, 1, 11, 10, 60, 5);
        put(p, 0, 10, 10, 60);
        give(p, inv);
        F(m).rec[0x199] = 45;
        const combat::Attack r1 = blow(m, p, 0, 5);          // 5 + 45 = 50: hits AC 50 unless -4
        give(m, 0x18);
        const combat::Attack r2 = blow(m, p, 0, 5);
        expect(Q, "effect 18: an invisible target -4, not for one with 18", !hits(r1) && hits(r2), "%d %d", hits(r1), hits(r2));
    }
    // 5C / 60 / 63 (0x13B81 / 0x13C70 / 0x13D31, phase 9): no fear (6F); no
    // charm (0B) or sleep (35); no poison (37) or paralysis (34), saves
    // against poison always made
    {
        const auto& fx = d->prof->fight.facts;
        combat::Harm h;
        h.level = 12;
        const struct { int fx, effect; const char* what; } mr[] = {
            {0x5C, fx.fear, "5C: fear"}, {0x60, fx.charm, "60: charm"}, {0x60, fx.mon.sleep, "60: sleep"},
            {0x63, fx.mon.poisoned, "63: poison"}, {0x63, fx.mon.paralyzed, "63: paralysis"}};
        for (const auto& c : mr) {
            only({m});
            put(m, 1, 11, 10, 60, 5);
            const bool before = combat::resists(b, m, c.effect, h, rng);
            give(m, c.fx);
            const bool after = combat::resists(b, m, c.effect, h, rng);
            snprintf(w, sizeof w, "effect %s resisted", c.what);
            expect(Q, w, !before && after, "%d %d", before, after);
        }
        only({m});
        put(m, 1, 11, 10, 60, 5);
        give(m, 0x63);
        saves(m, 20);
        dice({{20, 2}});
        const bool made = combat::saving_throw(F(m), 0, 0, rng);
        no_dice();
        expect(Q, "effect 63: a save against poison made on a 2", made);
    }
    // A monster's blow (the roll routine 0x36DB5): d20 + its to-hit against
    // the AC hits, a natural 1 misses and a natural 20 hits; the damage its
    // dice + bonus. Protection from Evil on the target: its -2 to be hit is
    // in phase 0xB, before the roll that overwrites it - lost (its saves'
    // +2 stay)
    {
        const int pe = d->prof->fight.facts.prot_evil;
        const struct { int d20, hit, pe; bool hits; const char* what; } th[] = {
            {4, 45, 0, false, "4 + 45 against AC 50 misses"}, {5, 45, 0, true, "5 + 45 against AC 50 hits"},
            {1, 70, 0, false, "a natural 1 misses (to-hit 70)"}, {20, 10, 0, true, "a natural 20 hits (to-hit 10)"},
            {5, 45, 1, true, "5 + 45 hits one with Protection from Evil (an evil monster)"}};
        for (const auto& c : th) {
            only({m, p});
            put(m, 1, 11, 10, 60, 5);
            F(m).rec[0x11B] = 8;                         // (chaotic evil)
            put(p, 0, 10, 10, 60);
            if (c.pe) give(p, pe);
            unarm(m);
            F(m).rec[0x199] = static_cast<uint8_t>(c.hit);
            combat::Fighter& f = F(m);
            f.attacks[0] = 1;
            f.attacks[1] = 0;
            f.rec[0x19E] = 2;
            f.rec[0x1A0] = 6;
            f.rec[0x1A2] = 3;
            f.target = p;
            F(p).received = 0;
            F(p).rec[0x19A] = 50;
            dice({{20, c.d20}, {6, 4}, {6, 5}});
            const combat::Attack r = combat::attack(b, m, p, names, rng, false);
            no_dice();
            snprintf(w, sizeof w, "a monster's blow: %s", c.what);
            expect(Q, w, hits(r) == c.hits && (!c.hits || F(p).hp() == 60 - (4 + 5 + 3)), "hit %d, HP %d", hits(r), F(p).hp());
        }
    }
    // Morale (GAME.OVR 0xE22F, Curse's): control & 0x7F x 2 (0x80: 0) - under
    // the share of its hit points lost, or 0: its side's health against 100
    // - the script's morale; under: it flees when no enemy moves more than
    // half its moves, else surrenders with Intelligence over 5
    {
        const struct { int control, hp, health, base, move, enemy_move, intl; combat::Morale want; const char* what; } mo[] = {
            {0x80 | 25, 60, 50, 0, 12, 12, 10, combat::Morale::Fight, "morale 50, 40% lost: fights"},
            {0x80 | 25, 40, 50, 40, 12, 12, 10, combat::Morale::Flee, "morale 50, 60% lost, its side 50 < 100 - 40: flees"},
            {0x80 | 25, 40, 50, 60, 12, 12, 10, combat::Morale::Fight, "its side 50, not under 100 - 60: fights"},
            {0x80, 100, 50, 40, 12, 15, 10, combat::Morale::Surrender, "morale 0, a faster enemy, Int 10: surrenders"},
            {0x80, 100, 50, 40, 12, 15, 5, combat::Morale::Fight, "a faster enemy, Int 5: fights on"}};
        for (const auto& c : mo) {
            only({m, p});
            put(m, 1, 11, 10, 100, 5);
            F(m).rec[0x1A4] = static_cast<uint8_t>(c.hp);
            F(m).rec[0xF7] = static_cast<uint8_t>(c.control);
            F(m).rec[0x1A5] = static_cast<uint8_t>(c.move);
            F(m).rec[0x13] = static_cast<uint8_t>(c.intl);
            put(p, 0, 2, 2, 60);
            F(p).rec[0x1A5] = static_cast<uint8_t>(c.enemy_move);
            b.enemy_health = c.health;
            b.morale_base = c.base;
            const combat::Morale got = combat::morale(b, fg->t, m);
            snprintf(w, sizeof w, "morale: %s", c.what);
            expect(Q, w, got == c.want, "%d", static_cast<int>(got));
        }
        b.morale_base = 0;
    }
    // The record's magic resistance (0x1A5; GAME.OVR 0x35FC9): a spell's
    // effect resisted on d100 <= resistance + (11 - the caster's level) x 5
    // (the drider 15, the lizard king 20)
    for (int lvl : {11, 9})
        for (int roll : {0, 1}) {
            only({m});
            put(m, 1, 11, 10, 60, 8);
            F(m).magic_res = 15;
            const int chance = 15 + (11 - lvl) * 5;
            combat::Harm h;
            h.level = lvl;
            dice({{100, chance + roll}});
            const bool res = combat::resists(b, m, d->prof->fight.facts.charm, h, rng);
            no_dice();
            snprintf(w, sizeof w, "magic resistance 15 against level %d: d100 %d", lvl, chance + roll);
            expect(Q, w, res == (roll == 0), "%d", res);
            F(m).magic_res = 0;
        }
    no_dice();
}

// ---- C: the monsters' own turns (phase 14: 3A 43 46 50 51 53 68 6B -
// each handler at its turn's start)
void turns()
{
    using namespace play;
    char w[120];
    if (!new_fight()) {
        expect(Q, "a fight", false);
        return;
    }
    combat::Battle& b = fg->b;
    const int m = 6, p = 0, p2 = 1, m2 = 7;
    // 3A (0x1242D): its pick within its pseudo-spell 0x3D's reach (6
    // squares): "gazes...", a save against petrification (kind 1) or
    // stoned; one with 0x48 sends it back when the gazer has 0x3B
    const int r6 = ot("m_reach", 0);
    for (int dist : {r6, r6 + 1}) {
        only({m, p});
        put(m, 1, 10, 10, 40, 6);
        give(m, 0x3A);
        put(p, 0, 10 + dist, 10, 60);
        saves(p, 12);
        dice({{20, 5}});
        special(m);
        no_dice();
        snprintf(w, sizeof w, "effect 3A: the gaze at %d squares, the save failed", dist);
        expect(Q, w, (F(p).status() == party::Stoned) == (dist <= r6) && !ends, "status %d, gazed %d", F(p).status(),
               said(combat::Ev::Gazes));
    }
    for (int mirror : {0, 1}) {
        only({m, p});
        put(m, 1, 10, 10, 40, 6);
        give(m, 0x3B);
        give(m, 0x3A);
        saves(m, 12);
        put(p, 0, 12, 10, 60);
        saves(p, 12);
        if (mirror) give(p, 0x48);
        dice({{20, 5}});
        special(m);
        no_dice();
        snprintf(w, sizeof w, "effects 3A + 3B: the gaze at one %s 0x48", mirror ? "with" : "without");
        expect(Q, w, mirror ? F(m).status() == party::Stoned && F(p).status() == party::Okay
                            : F(p).status() == party::Stoned && F(m).status() == party::Okay,
               "gazer %d, target %d", F(m).status(), F(p).status());
    }
    // 43 (0x12D5F): the round count 1 to its attacks byte (0x121), its own
    // target next to it: 8, fire and breath (0x21), a breath save for half; it fights on
    {
        const struct { int round, half, dist; int want; const char* what; } py[] = {
            {0, 24, 1, 0, "the first round (count 0): nothing"}, {1, 24, 1, 1, "round 2 next to it: its fire"},
            {24, 24, 1, 1, "count 24 of 24: its fire"}, {25, 24, 1, 0, "count 25 of 24: nothing"},
            {3, 24, 2, 0, "two squares away: nothing"}};
        for (const auto& c : py) {
            only({m, p});
            put(m, 1, 10, 10, 54, 12);
            give(m, 0x43);
            F(m).rec[0x11C] = static_cast<uint8_t>(c.half);
            put(p, 0, 10 + c.dist, 10, 60);
            saves(p, 12);
            F(m).target = p;
            b.round = c.round;
            dice({{20, 5}});
            special(m);
            no_dice();
            snprintf(w, sizeof w, "effect 43 (the pyrohydra): %s", c.what);
            expect(Q, w, damage_to(p) == c.want * ot("m_pyro", 0) && !ends, "%d", damage_to(p));
        }
        // the save made: half; its target someone else (not next to it): nothing
        only({m, p, p2});
        put(m, 1, 10, 10, 54, 12);
        give(m, 0x43);
        F(m).rec[0x11C] = 24;
        put(p, 0, 11, 10, 60);
        put(p2, 0, 14, 10, 60);
        saves(p, 12);
        b.round = 2;
        F(m).target = p;
        dice({{20, 15}});
        special(m);
        no_dice();
        expect(Q, "effect 43: the save made, half", damage_to(p) == ot("m_pyro", 0) / 2, "%d", damage_to(p));
        F(m).target = p2;
        dice({{20, 5}});
        special(m);
        no_dice();
        expect(Q, "effect 43: at its own target only (not next to it: nothing)", damage_to(p) == 0 && damage_to(p2) == 0);
    }
    // 46 (0x12FAF): one next to it (its pseudo-spell 0x44's reach): a save
    // against spells or confused (23) 1d10 + 2 rounds
    for (int dist : {1, 2}) {
        only({m, p});
        put(m, 1, 10, 10, 56, 10);
        give(m, 0x46);
        put(p, 0, 10 + dist, 10, 60);
        saves(p, 12);
        dice({{20, 5}, {10, 7}});
        special(m);
        no_dice();
        const int conf = aff_time(p, d->prof->fight.facts.sp.confuse);
        snprintf(w, sizeof w, "effect 46 (the umber hulk) at %d squares, the save failed", dist);
        expect(Q, w, dist == 1 ? conf == 7 + ot("m_umber", 2) : conf < 0, "confused %d", conf);
    }
    // 50 (0x13457): every turn (no roll), one next to it: 7, fire and
    // breath, a breath save for half; it fights on
    for (int dist : {1, 2}) {
        only({m, p});
        put(m, 1, 10, 10, 32, 7);
        give(m, 0x50);
        put(p, 0, 10 + dist, 10, 60);
        saves(p, 12);
        dice({{100, 100}, {20, 5}});
        special(m);
        no_dice();
        snprintf(w, sizeof w, "effect 50 (the hell hound) at %d squares, d100 100", dist);
        expect(Q, w, damage_to(p) == (dist == 1 ? ot("m_hound", 0) : 0) && !ends, "%d, ends %d", damage_to(p), ends);
    }
    // 51 (0x13587): its pick within 6 (pseudo-spell 0x3D); (11 - squares) x
    // 10% - 10% when the count is 0 - 4d8 acid (0x10), a breath save for
    // half; else "Spits and misses"
    {
        const struct { int round, dist, d100; bool spits, misses; const char* what; } sl[] = {
            {0, 3, 10, true, false, "the first round, d100 10 of 10%"}, {0, 3, 11, false, true, "the first round, d100 11"},
            {1, 3, 80, true, false, "3 squares, d100 80 of 80%"}, {1, 3, 81, false, true, "3 squares, d100 81"},
            {1, 7, 1, false, false, "7 squares: past its reach"}};
        for (const auto& c : sl) {
            only({m, p});
            put(m, 1, 10, 10, 54, 12);
            give(m, 0x51);
            put(p, 0, 10 + c.dist, 10, 200);
            saves(p, 12);
            b.round = c.round;
            dice({{100, c.d100}, {20, 5}});
            special(m);
            no_dice();
            snprintf(w, sizeof w, "effect 51 (the giant slug): %s", c.what);
            expect(Q, w, (damage_to(p) == ot("m_slug", 0) * ot("m_slug", 1)) == c.spits && said(combat::Ev::SpitsMisses) == c.misses && !ends,
                   "%d, misses %d", damage_to(p), said(combat::Ev::SpitsMisses));
        }
    }
    // 53 / 68 (0x327F9): 3 a fight (the effect's data: 3 when the count is
    // 0 or over 3), its pick within 6, 3 lines of 7 / 9: its maximum hit
    // points, cold / fire and breath, a breath save for half; never with
    // one of its side on the path; its turn ends
    for (int fx : {0x53, 0x68}) {
        const int kind = fx == 0x53 ? 0x22 : 0x21;
        only({m, p, p2, m2});
        put(m, 1, 10, 10, 44, 11);
        give(m, fx);
        put(p, 0, 13, 10, 200);
        put(p2, 0, 14, 10, 200);
        saves(p, 12);
        saves(p2, 12);
        put(m2, 1, 3, 3, 30);
        b.round = 0;
        dice({{20, 5}, {20, 15}});
        special(m);
        no_dice();
        snprintf(w, sizeof w, "effect %02X: breath at 3 squares - its maximum HP, half on a save", fx);
        expect(Q, w, damage_to(p) == 44 && damage_to(p2) == 22 && ends, "%d %d, ends %d", damage_to(p), damage_to(p2), ends);
        // two more, then none
        int n_breaths = 1;
        for (int k = 0; k < 3; ++k) {
            F(p).rec[0x1A4] = F(p2).rec[0x1A4] = 200;
            b.round = 1 + k;
            dice({});
            special(m);
            no_dice();
            if (damage_to(p) > 0) ++n_breaths;
        }
        snprintf(w, sizeof w, "effect %02X: 3 breaths a fight", fx);
        expect(Q, w, n_breaths == 3, "%d", n_breaths);
        // a friend on its path: no breath
        only({m, p, m2});
        put(m, 1, 10, 10, 44, 11);
        give(m, fx);
        put(p, 0, 13, 10, 200);
        put(m2, 1, 12, 10, 30);
        b.round = 0;
        dice({});
        special(m);
        no_dice();
        snprintf(w, sizeof w, "effect %02X: one of its side on the path - no breath", fx);
        expect(Q, w, damage_to(p) == 0 && damage_to(m2) == 0 && !ends, "%d %d", damage_to(p), damage_to(m2));
        // its count over 3 later in the fight: 3 again
        only({m, p});
        put(m, 1, 10, 10, 44, 11);
        give(m, fx);
        F(m).aff[combat_find(m, fx)][3] = 0xFF;         // (as the file's)
        put(p, 0, 13, 10, 200);
        saves(p, 12);
        b.round = 4;
        dice({{20, 5}});
        special(m);
        no_dice();
        const int k = combat_find(m, fx);
        snprintf(w, sizeof w, "effect %02X: a first breath in round 5 - 3 then 2 left", fx);
        expect(Q, w, damage_to(p) == 44 && k >= 0 && F(m).aff[k][3] == 2, "%d, left %d", damage_to(p), k >= 0 ? F(m).aff[k][3] : -1);
        (void)kind;
    }
    b.round = 0;
}

// ---- D: the fight's end - the share of the experience (GAME.OVR 0x6DA3;
// the divisor counted at 0x6F90: the party less those out of the fight or
// animated): +10% for the prime requisites - cleric Wisdom over 14 in
// Secret's program (AD&D's 16 with the fixes on), fighter Strength, paladin
// Strength and Wisdom, ranger Strength, Intelligence and Wisdom, magic-user
// Intelligence, thief Dexterity, each over 15, druids none - two classes
// half; the treasure's worth (0x6C8D: each coin on its own - copper / 200,
// silver / 20, electrum / 2, gold, platinum x 5, gems x 250, jewellery x
// 2200)
uint32_t xp(int i)
{
    const uint8_t* r = F(i).rec;
    return r[0x127] | r[0x128] << 8 | r[0x129] << 16 | static_cast<uint32_t>(r[0x12A]) << 24;
}
void fight_end()
{
    using namespace play;
    if (!new_fight()) {
        expect(Q, "a fight", false);
        return;
    }
    char w[140];
    // class, the stat in use (Str 0x11, Int 0x13, Wis 0x15, Dex 0x17) at 15 or 16
    struct M { int cls; int stats[3]; int levels; const char* what; };
    const M ms[] = {{0, {0x15}, 1, "cleric"}, {2, {0x11}, 1, "fighter"}, {5, {0x13}, 1, "magic-user"},
                    {6, {0x17}, 1, "thief"}, {1, {0x15}, 1, "druid (none)"}, {3, {0x11, 0x15}, 1, "paladin"}};
    for (int fixes = 0; fixes < 2; ++fixes)
        for (int value : {15, 16}) {
            rules::set_fixes(fixes != 0);
            for (int i = 0; i < 6; ++i) {
                put(i, 0, 2 + i, 2, 30);
                for (int k = 0x10; k < 0x1E; ++k) F(i).rec[k] = 10;
                F(i).rec[0x75] = static_cast<uint8_t>(ms[i].cls);
                for (int k = 0; k < 8; ++k) F(i).rec[0x109 + k] = 0;
                F(i).rec[0x109 + ms[i].cls] = 5;
                for (int st : ms[i].stats) if (st) F(i).rec[st] = static_cast<uint8_t>(value);
                F(i).rec[0x127] = F(i).rec[0x128] = F(i).rec[0x129] = F(i).rec[0x12A] = 0;
            }
            const int share = combat::award(fg->b, 6000);
            for (int i = 0; i < 6; ++i) {
                const bool plus = (ms[i].cls == 0 ? value >= (fixes ? 16 : 15) : value >= 16) && ms[i].cls != 1;
                const uint32_t want = 1000 + (plus ? 100 : 0);
                snprintf(w, sizeof w, "the share: a %s at %d, fixes %s", ms[i].what, value, fixes ? "on" : "off");
                expect(Q, w, share == 1000 && xp(i) == want, "share %d, got %u, the original's %u", share, xp(i), want);
            }
        }
    rules::set_fixes(true);
    // Out of the fight (in combat 0) or animated: no share, not counted;
    // a fighter / magic-user (class 13) half
    for (int i = 0; i < 6; ++i) {
        put(i, 0, 2 + i, 2, 30);
        for (int k = 0x10; k < 0x1E; ++k) F(i).rec[k] = 10;
        F(i).rec[0x75] = 2;
        for (int k = 0; k < 8; ++k) F(i).rec[0x109 + k] = 0;
        F(i).rec[0x109 + 2] = 5;
        F(i).rec[0x127] = F(i).rec[0x128] = F(i).rec[0x129] = F(i).rec[0x12A] = 0;
    }
    F(4).rec[0x196] = 0;
    F(4).rec[0x195] = party::Dead;
    F(5).rec[0x195] = party::Animated;
    F(3).rec[0x75] = 13;
    F(3).rec[0x109 + 5] = 5;
    const int share = combat::award(fg->b, 4000);
    expect(Q, "the share: 4 left of 6 (one dead, one animated)", share == 1000 && xp(4) == 0 && xp(5) == 0 && xp(0) == 1000,
           "share %d, %u %u %u", share, xp(0), xp(4), xp(5));
    expect(Q, "the share: a fighter / magic-user half", xp(3) == 500, "%u", xp(3));
    // The treasure's worth, each coin on its own
    {
        const int m[7] = {399, 39, 3, 7, 2, 1, 1};
        const int want = 399 / 200 + 39 / 20 + 3 / 2 + 7 + 2 * 5 + 250 + 2200;
        expect(Q, "the treasure's experience: each coin on its own", combat::money_exp(m) == want, "%d, the original's %d",
               combat::money_exp(m), want);
    }
}

} // namespace mons

void all_monsters()
{
    b_oracle_load("secret_monsters");
    if (mons::part("a")) {
        mons::every_monster(false);         // the original's
        mons::every_monster(true);          // the Item Fixes on (the default)
        mons::every_level();
    }
    if (mons::part("b")) mons::effects();
    if (mons::part("c")) mons::turns();
    if (mons::part("d")) mons::fight_end();
    mons::no_dice();
}

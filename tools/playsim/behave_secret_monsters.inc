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
            if (v.move != 0 && v.move <= 9) v.move += 3;
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
                     fr[0xE5] == s[0xEE];
        for (int k = 0; k < 5; ++k) bytes = bytes && fr[0xDF + k] == s[0xE8 + k];
        for (int k = 0; k < 7; ++k) bytes = bytes && fr[0x109 + k] == s[0x111 + k];
        expect(Q, "side, size, undead type, kind flags, Hit Dice, saves, levels from the record", bytes,
               "%s: side %d size %d undead %d kind %02X HD %d", tag, fr[0x197], fr[0xDE], fr[0xE9], fr[0x14B], fr[0xE5]);
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

} // namespace mons

void all_monsters()
{
    b_oracle_load("secret_monsters");
    if (mons::part("a")) {
        mons::every_monster(false);         // the original's
        mons::every_monster(true);          // the Item Fixes on (the default)
    }
}

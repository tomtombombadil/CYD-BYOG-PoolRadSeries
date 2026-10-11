// Behaviour checks: monsters and the combat rules (included by sweep.cpp
// with -DBEHAVE_MONSTERS). What every monster of the player's MON1-6CHA
// files brings into a fight and what its special abilities do, and the
// rules of a fight (to-hit, damage, dying, saves, backstab, sweeps, free
// attacks, initiative, morale, experience, turning undead), against what the
// original does (claude/behaviour_facts.md: monsters; the rules from coab
// and its listing, AD&D's tables). Rule tables come from the oracle file
// (ORACLE_DIR/oracle_monsters.txt, scratch space), the monsters' numbers from
// the player's own files at run time.

#include <map>

namespace bm {

const char* kPart = "monsters";

// ---- The oracle (rule tables: "key v0 v1 ...")
std::map<std::string, std::vector<int>> orc;

bool load_oracle()
{
    const char* dir = getenv("ORACLE_DIR");
    std::string path = std::string(dir ? dir : "/tmp/claude-0/bt") + "/oracle_monsters.txt";
    FILE* in = fopen(path.c_str(), "r");
    if (!in) return false;
    char line[512];
    while (fgets(line, sizeof line, in)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char* save = nullptr;
        char* key = strtok_r(line, " \t\n", &save);
        if (!key) continue;
        std::vector<int>& v = orc[key];
        for (char* t = strtok_r(nullptr, " \t\n", &save); t; t = strtok_r(nullptr, " \t\n", &save)) v.push_back(atoi(t));
    }
    fclose(in);
    return !orc.empty();
}

int ov(const char* key, int i)
{
    auto it = orc.find(key);
    if (it == orc.end() || i < 0 || i >= static_cast<int>(it->second.size())) return 0;
    return it->second[i];
}
bool in_list(const char* key, int v)
{
    auto it = orc.find(key);
    if (it == orc.end()) return false;
    for (int x : it->second)
        if (x == v) return true;
    return false;
}

int sb(uint8_t v) { return static_cast<int8_t>(v); }

// ---- The monster files
std::vector<int> ids(int area)
{
    std::vector<int> out;
    char path[300];
    snprintf(path, sizeof path, "/GOLDBOX/%s/MON%dCHA.DAX", game_dir.substr(game_dir.rfind('/') + 1).c_str(), area);
    fs::File f = sd_fs().open(path, "r");
    if (!f) return out;
    library::FileSource src(f);
    static dax::Index dir;
    if (dax::read_index(src, dir) == dax::Status::Ok)
        for (int i = 0; i < dir.count; ++i) out.push_back(dir.entries[i].id);
    f.close();
    return out;
}

struct Raw {
    uint8_t rec[party::kRecordSize];
    uint8_t item[16][items::kRecordSize];
    int     n_items = 0;
    uint8_t aff[16][party::kAffectSize];
    int     n_aff = 0;
    char    name[20];
};

bool raw(int area, int id, Raw& r)
{
    char file[24];
    snprintf(file, sizeof file, "MON%dCHA.DAX", area);
    if (play::read_block(file, id, r.rec, sizeof r.rec) != static_cast<int>(sizeof r.rec)) return false;
    snprintf(file, sizeof file, "MON%dITM.DAX", area);
    int n = play::read_block(file, id, &r.item[0][0], sizeof r.item);
    r.n_items = n > 0 ? n / static_cast<int>(items::kRecordSize) : 0;
    snprintf(file, sizeof file, "MON%dSPC.DAX", area);
    n = play::read_block(file, id, &r.aff[0][0], sizeof r.aff);
    r.n_aff = n > 0 ? n / static_cast<int>(party::kAffectSize) : 0;
    const int len = r.rec[0] < 15 ? r.rec[0] : 15;
    memcpy(r.name, r.rec + 1, static_cast<size_t>(len));
    r.name[len] = 0;
    return true;
}

// ---- What the original's recalculation (coab reclac_player_values, the
// listing's sub_66C20 - run for EVERY combatant at the battle's setup,
// sub_380E0) makes of a record and its readied items
struct Values {
    int hit, ac, ac_behind, move, d1, s1, b1, d2, s2, b2;
};

int str_group(const uint8_t* r)
{
    const int s = r[0x11], pct = r[0x1C];
    if (s <= 17) return s;
    if (s == 18) return pct == 0 ? 18 : pct <= 50 ? 19 : pct <= 75 ? 20 : pct <= 90 ? 21 : pct <= 99 ? 22 : 23;
    return s <= 25 ? s + 5 : 0;
}

Values expected(const uint8_t* r, const uint8_t (*it)[items::kRecordSize], int n)
{
    using namespace play;
    Values v{};
    const bool uses_str = r[0x125] != 0;
    const int g = str_group(r);
    const int sh = uses_str ? ov("str_hit", g) : 0, sd = uses_str ? ov("str_dmg", g) : 0;
    const int dex = r[0x17];
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
        if (it[k][0x2E] == ov("arrow", 0)) arrows = it[k];
        if (it[k][0x2E] == ov("quarrel", 0)) quarrels = it[k];
    }
    for (int m = 0; m < 7; ++m) weight += r[0xFB + 2 * m] | r[0xFC + 2 * m] << 8;
    v.d1 = r[0x11E];
    v.s1 = r[0x120];
    v.b1 = sb(r[0x122]);
    v.d2 = r[0x11F];
    v.s2 = r[0x121];
    v.b2 = sb(r[0x123]);
    v.hit = sb(r[0x73]);
    v.move = r[0xE4];
    int bonus[5] = {ov("dex_ac", dex), 0, 0, 0, 0};
    if (!weapon) {
        v.hit += sh;
        v.b1 += sd;
    } else {
        const items::TypeInfo& ti = names->type(weapon[0x2E]);
        if (ti.flags & 0x02) v.hit += ov("dex_react", dex);
        v.b1 = ti.bonus;
        if (ti.flags & 0x04) {
            v.hit += sh;
            v.b1 += sd;
        }
        int plus = sb(weapon[0x32]);
        if ((ti.flags & 0x80) && quarrels) plus += sb(quarrels[0x32]);
        if ((ti.flags & 0x01) && arrows) plus += sb(arrows[0x32]);
        v.b1 += plus;
        if (r[0x74] == 2 && in_list("elf_types", weapon[0x2E])) ++plus;
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
            v.move = w <= 150 ? r[0xE4] : w <= 399 ? 9 : 6;
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
    int over = weight - ov("max_enc", g);
    if (over < 0) over = 0;
    const int can = over <= 0x200 ? v.move : over <= 0x300 ? 9 : over <= 0x400 ? 6 : 3;
    if (can < v.move) v.move = can;
    if (bonus[4] < r[0x124]) bonus[4] = r[0x124];
    v.ac = (bonus[0] + bonus[1] + bonus[2] + bonus[3] + bonus[4]) & 0xFF;
    v.ac_behind = (bonus[4] + bonus[2] + bonus[3] - 2) & 0xFF;
    v.hit = sb(static_cast<uint8_t>(v.hit));
    return v;
}

// A fight with `copies` of MON<area> block `id` at the inn's script (as
// start_fight, block 0 too)
bool fight(int area, int id, int copies)
{
    const uint16_t a = 0x8AC2;
    play::d->gs.game_area = static_cast<uint8_t>(area);
    play::vm->set(a + 2, static_cast<uint16_t>(id));
    play::vm->set(a + 4, static_cast<uint16_t>(copies));
    play::vm->set(a + 6, static_cast<uint16_t>(id));
    play::then = play::Then::Idle;
    play::handle(play::vm->run(a));
    for (int k = 0; k < 400 && play::screen != play::Screen::Fight; ++k) {
        g_now += 20;
        play::tick(g_now, C);
        if (play::waiting && play::vm->wait() == ecl::Wait::Menu) tap_item(0);
        if (play::waiting && play::vm->wait() == ecl::Wait::Print && play::page_prompt) play::tap(10, 10, C);
    }
    return play::screen == play::Screen::Fight && play::fg;
}

// ---- A: every monster of every MON file in a fight
void every_monster()
{
    using namespace play;
    int checked = 0;
    for (int area = 1; area <= 6; ++area) {
        const std::vector<int> list = ids(area);
        for (int id : list) {
            if (getenv("ONLY_AREA") && atoi(getenv("ONLY_AREA")) != area) continue;
            if (!boot()) {
                expect(kPart, "boot", false);
                return;
            }
            Raw r;
            if (!raw(area, id, r)) {
                expect(kPart, "monster record", false, "MON%d #%d", area, id);
                continue;
            }
            dice_usual();
            if (!fight(area, id, 4)) {
                expect(kPart, "a fight with it", false, "MON%d #%d %s", area, id, r.name);
                continue;
            }
            combat::Battle& b = fg->b;
            char tag[64];
            snprintf(tag, sizeof tag, "MON%d #%d %s", area, id, r.name);
            // LOAD MONSTER copies the record as it is (coab load_mob / CMD_LoadMonster): 4 copies,
            // each with the record's own hit points (nothing rolled), its effects and items
            const int first = b.party_size;
            int copies = 0;
            bool same_hp = true, same_aff = true, same_items = true;
            for (int i = first; i < b.n; ++i) {
                const combat::Fighter& f = b.f[i];
                ++copies;
                if (f.hp() != r.rec[0x1A4] || f.hp_max() != r.rec[0x78]) same_hp = false;
                // (those invisible from the start have their invisibility added: compare the file's part)
                if (!f.n_aff || *f.n_aff < r.n_aff || memcmp(f.aff, r.aff, static_cast<size_t>(r.n_aff) * 9) != 0) same_aff = false;
                if (f.n_items != r.n_items) same_items = false;
                for (int k = 0; k < r.n_items && k < f.n_items; ++k)
                    if (memcmp(f.items[k] + 0x2E, r.item[k] + 0x2E, 0x3F - 0x2E) != 0) same_items = false;
            }
            expect(kPart, "LOAD MONSTER: 4 copies", copies == 4, "%s: %d", tag, copies);
            expect(kPart, "hit points as the record (not rolled)", same_hp, "%s: record %d/%d, in the fight %d/%d", tag,
                   r.rec[0x1A4], r.rec[0x78], b.f[first].hp(), b.f[first].hp_max());
            expect(kPart, "its effects (MON SPC) as the file", same_aff, "%s: %d effects", tag, r.n_aff);
            expect(kPart, "its items (MON ITM) as the file", same_items, "%s: %d items, fighter %d", tag, r.n_items,
                   b.f[first].n_items);
            // Its side and its footprint (record 0xDE low bits; 0x80 large)
            const combat::Fighter& f = b.f[first];
            expect(kPart, "its side (record 0x197)", f.team() == r.rec[0x197], "%s: %d", tag, f.team());
            const int foot = r.rec[0xDE] & 7;
            expect(kPart, "its footprint (record 0xDE)", f.gone || f.size == foot, "%s: size %d, record %d", tag, f.size, foot);
            // The values the original works out at the battle's setup
            const Values v = expected(r.rec, r.item, r.n_items);
            const uint8_t* fr = f.rec;
            const bool vals = sb(fr[0x199]) == v.hit && fr[0x19A] == v.ac && fr[0x19B] == v.ac_behind && fr[0x1A5] == v.move;
            expect(kPart, "to-hit value, AC, rear AC, movement (the setup's recalculation)", vals,
                   "%s: hit %d (want %d), AC %d (%d), rear %d (%d), move %d (%d)", tag, sb(fr[0x199]), v.hit, fr[0x19A], v.ac,
                   fr[0x19B], v.ac_behind, fr[0x1A5], v.move);
            const bool dice = fr[0x19E] == v.d1 && fr[0x1A0] == v.s1 && sb(fr[0x1A2]) == v.b1 && fr[0x19F] == v.d2 &&
                              fr[0x1A1] == v.s2 && sb(fr[0x1A3]) == v.b2;
            expect(kPart, "attack dice (the weapon's, else the record's)", dice, "%s: %dd%d%+d / %dd%d%+d (want %dd%d%+d / %dd%d%+d)",
                   tag, fr[0x19E], fr[0x1A0], sb(fr[0x1A2]), fr[0x19F], fr[0x1A1], sb(fr[0x1A3]), v.d1, v.s1, v.b1, v.d2, v.s2,
                   v.b2);
            // A round: moves = movement x 2 (half squares), attacks by the half counts (0x11C, 0x11D)
            dice_faces({6});
            combat::start_round(b, rng);
            int half1 = r.rec[0x11C], half2 = r.rec[0x11D], missiles = 0;
            // A missile weapon readied that can shoot: the ITEMS file's attacks (at least 2), no more
            // than the missiles (coab reclac_attacks / GetCurrentAttackItem)
            for (int k = 0; k < r.n_items; ++k) {
                if (!r.item[k][0x34] || names->type(r.item[k][0x2E]).slot != 0) continue;
                const items::TypeInfo& ti = names->type(r.item[k][0x2E]);
                if (ti.range <= 1) continue;
                const uint8_t* shot = (ti.flags & 0x10) ? r.item[k] : nullptr;
                for (int j = 0; (ti.flags & 0x08) && j < r.n_items; ++j)
                    if (r.item[j][0x34] && (((ti.flags & 1) && r.item[j][0x2E] == ov("arrow", 0)) ||
                                            ((ti.flags & 0x80) && r.item[j][0x2E] == ov("quarrel", 0))))
                        shot = r.item[j];
                if (shot || ti.flags == 0x0A) {
                    half1 = ti.attacks < 2 ? 2 : ti.attacks;
                    missiles = shot ? shot[0x39] : 0;
                    printf("  (%s: a missile weapon, type %d: %d half attacks, %d missiles)\n", tag, r.item[k][0x2E], half1, missiles);
                }
            }
            const bool haste = r.n_aff && [&]() {
                for (int k = 0; k < r.n_aff; ++k)
                    if (r.aff[k][0] == 0x27) return true;
                return false;
            }();
            const int mul = haste ? 2 : 1;
            auto capm = [&](int a) { return missiles > 0 && missiles < a ? missiles : a; };
            int mv = v.move;
            if (mv < 1 || mv > 96) mv = 1;
            const combat::Fighter& g = b.f[first];
            if (!g.gone && g.size) {
                expect(kPart, "round 0: moves = movement x 2", g.moves == mv * 2 * mul, "%s: %d (movement %d%s)", tag, g.moves, mv,
                       haste ? ", hasted" : "");
                expect(kPart, "round 0: attacks = half attacks / 2 (slot 1, slot 2)",
                       g.attacks[0] == capm((half1 * mul) / 2) && g.attacks[1] == (half2 * mul) / 2, "%s: %d / %d (half %d / %d%s)", tag,
                       g.attacks[0], g.attacks[1], half1, half2, haste ? ", hasted" : "");
            }
            b.round = 1;
            combat::start_round(b, rng);
            if (!g.gone && g.size)
                expect(kPart, "round 1: attacks = (half attacks + 1) / 2",
                       g.attacks[0] == capm((half1 * mul + 1) / 2) && g.attacks[1] == (half2 * mul + 1) / 2, "%s: %d / %d", tag,
                       g.attacks[0], g.attacks[1]);
            // Experience when it's beaten (coab calc_battle_exp): 0x13E x 0x12C + 0x13C
            for (int i = first; i < b.n; ++i) {
                b.f[i].rec[0x195] = party::Dead;
                b.f[i].rec[0x196] = 0;
            }
            const combat::Outcome o = combat::finish(b, ml ? ml->m : nullptr);
            int placed = 0;
            for (int i = first; i < b.n; ++i)
                if (!b.f[i].gone) ++placed;
            // (only the enemy side's: allies on the party's side bring none)
            const int want = r.rec[0x197] != 1 ? 0
                                               : placed * (r.rec[0x13E] * r.rec[0x12C] + static_cast<int16_t>(r.rec[0x13C] | r.rec[0x13D] << 8));
            expect(kPart, "experience: XP per HP x rolled HP + base, each beaten one", o.exp == want, "%s: %d (want %d, %d placed)",
                   tag, o.exp, want, placed);
            dice_usual();
            ++checked;
        }
    }
    printf("  (%d monsters)\n", checked);
}

// ---- A clean arena: a fight against `copies` of MON<area> #id, the field
// all plain floor and everyone off it (put() places them)
combat::Battle* B = nullptr;
int M = 0;                              // the first monster

bool arena(int area, int id, int copies = 1)
{
    using namespace play;
    dice_usual();
    if (!boot() || !fight(area, id, copies)) {
        B = nullptr;
        return false;
    }
    B = &fg->b;
    combat::Battle& b = *B;
    M = b.party_size;
    for (int y = 0; y < combat::kH; ++y)
        for (int x = 0; x < combat::kW; ++x) b.ground[y][x] = 0x17;
    b.n_clouds = 0;
    b.round = 1;
    b.no_action = 16;
    b.to_hit_party = b.to_hit_monsters = 0;
    for (int i = 0; i < b.n; ++i) {
        combat::Fighter& f = b.f[i];
        f.size = 0;
        f.received = f.turns = 0;
        f.delay = 5;
        f.attacks[0] = 1;
        f.attacks[1] = 0;
        f.target = -1;
        f.guarding = f.fleeing = false;
    }
    combat::occupancy(b);
    return true;
}

void put(int i, int x, int y, int facing = 0)
{
    combat::Fighter& f = B->f[i];
    f.x = x;
    f.y = y;
    f.facing = facing;
    f.gone = false;
    f.size = f.member >= 0 ? 1 : ((f.rec[0xDE] & 7) ? (f.rec[0xDE] & 7) : 1);
    combat::occupancy(*B);
}

// A plain item record of a type, readied (synthetic: type and readied only)
void hold(int i, int type, int plus = 0)
{
    combat::Fighter& f = B->f[i];
    if (f.member < 0) return;
    party::Character& ch = play::pt->m[f.member];
    ch.n_items = 1;
    memset(ch.items[0], 0, items::kRecordSize);
    ch.items[0][0x2E] = static_cast<uint8_t>(type);
    ch.items[0][0x32] = static_cast<uint8_t>(plus);
    ch.items[0][0x34] = 1;
    f.items = ch.items;
    f.n_items = 1;
}
void bare(int i)
{
    combat::Fighter& f = B->f[i];
    if (f.member >= 0) play::pt->m[f.member].n_items = 0;
    f.n_items = 0;
}

int find_member(int cls_off)
{
    for (int i = 0; i < play::pt->count; ++i)
        if (play::pt->m[i].rec[cls_off] > 0) return i;
    return -1;
}

// ---- B: the rules of a fight, each with forced dice
void rules_checks()
{
    using namespace play;
    if (!arena(1, 83)) {
        expect(kPart, "a fight (lizard man)", false);
        return;
    }
    for (int i = 0; i < pt->count; ++i) {
        const uint8_t* r = pt->m[i].rec;
        char nm[20];
        pt->m[i].name(nm, sizeof nm);
        printf("  party %d %s class %d levels %d %d %d %d %d %d %d HD %d thac0 %d hit %d AC %d saves %d %d %d %d %d hp %d\n", i, nm,
               r[0x75], r[0x109], r[0x10A], r[0x10B], r[0x10C], r[0x10D], r[0x10E], r[0x10F], r[0xE5], sb(r[0x73]), sb(r[0x199]),
               r[0x19A], r[0xDF], r[0xE0], r[0xE1], r[0xE2], r[0xE3], r[0x1A4]);
    }
    const int F = find_member(0x10B);           // a fighter
    expect(kPart, "the sample party has a fighter", F >= 0);
    if (F < 0) return;
    combat::Battle& b = *B;

    // -- To hit (coab PC_CanHitTarget): d20, 1 misses, 20 hits; else hit when
    //    roll + to-hit value + the side's bonus >= the target's AC (stored)
    auto swing = [&](int a, int t, std::initializer_list<int> faces, bool behind = false) {
        b.f[a].attacks[0] = 1;
        b.f[a].attacks[1] = 0;
        b.f[t].received = 0;
        b.f[t].turns = 0;
        dice_faces(faces);
        const combat::Attack at = combat::attack(b, a, t, names, rng, behind);
        dice_usual();
        return at;
    };
    put(F, 20, 10, 2);
    put(M, 21, 10, 6);
    bare(F);
    uint8_t* fr = b.f[F].rec;
    uint8_t* mr = b.f[M].rec;
    fr[0x199] = 45;
    mr[0x19A] = 55;
    fr[0x19E] = 1;
    fr[0x1A0] = 4;
    fr[0x1A2] = 0;
    mr[0x1A4] = 100;
    mr[0x78] = 100;
    {
        combat::Attack at = swing(F, M, {10, 1});
        expect(kPart, "to hit: d20 10 + 45 >= AC 55 hits", at.n == 1 && at.hits[0].hit);
        at = swing(F, M, {9});
        expect(kPart, "to hit: d20 9 + 45 < 55 misses", at.n == 1 && !at.hits[0].hit);
        fr[0x199] = 120;
        at = swing(F, M, {1});
        expect(kPart, "to hit: a natural 1 misses (to-hit 120)", at.n == 1 && !at.hits[0].hit);
        fr[0x199] = static_cast<uint8_t>(-100);
        mr[0x19A] = 255;
        at = swing(F, M, {20, 1});
        expect(kPart, "to hit: a natural 20 hits (to-hit -100, AC 255)", at.n == 1 && at.hits[0].hit);
        fr[0x199] = 45;
        mr[0x19A] = 55;
        b.to_hit_party = 2;
        at = swing(F, M, {8, 1});
        expect(kPart, "to hit: the party's script bonus (0x7F71) +2: 8 hits", at.n == 1 && at.hits[0].hit);
        b.to_hit_party = 0;
        b.to_hit_monsters = 2;
        at = swing(F, M, {8, 1});
        expect(kPart, "to hit: the monsters' bonus doesn't help the party", at.n == 1 && !at.hits[0].hit);
        // the monster's own attack with the monsters' bonus
        b.f[M].rec[0x199] = 45;
        fr[0x19A] = 55;
        b.f[M].attacks[1] = 0;
        b.f[M].attacks[0] = 1;
        b.f[F].received = 0;
        dice_faces({8, 1});
        at = combat::attack(b, M, F, names, rng);
        dice_usual();
        expect(kPart, "to hit: a monster with the monsters' bonus (0x7F70) +2: 8 hits", at.n == 1 && at.hits[0].hit);
        b.to_hit_monsters = 0;
    }
    // -- Damage (coab sub_3E192): the dice + bonus, at least 0
    {
        fr[0x199] = 100;
        mr[0x19A] = 50;
        fr[0x19E] = 2;
        fr[0x1A0] = 6;
        fr[0x1A2] = 3;
        mr[0x1A4] = 100;
        combat::Attack at = swing(F, M, {15, 4, 5});
        expect(kPart, "damage: 2d6 (4, 5) + 3 = 12", at.n == 1 && at.hits[0].damage == 12 && b.f[M].hp() == 88, "%d, hp %d",
               at.hits[0].damage, b.f[M].hp());
        fr[0x1A2] = static_cast<uint8_t>(-10);
        at = swing(F, M, {15, 1, 1});
        expect(kPart, "damage: never below 0 (2 - 10)", at.n == 1 && at.hits[0].hit && at.hits[0].damage == 0, "%d",
               at.hits[0].damage);
        fr[0x1A2] = 0;
    }
    // -- Large targets (coab AttackTarget01): a weapon's large dice and bonus
    {
        const items::TypeInfo& ls = names->type(36);
        expect(kPart, "ITEMS: long sword 1d8, 1d12 against large (AD&D)", ls.dice == 1 && ls.sides == 8 && ls.dice_large == 1 &&
               ls.sides_large == 12, "%dd%d / %dd%d", ls.dice, ls.sides, ls.dice_large, ls.sides_large);
        hold(F, 36);
        fr[0x19E] = 1;
        fr[0x1A0] = 8;
        fr[0x1A2] = 2;
        mr[0xDE] = 0x81;                                // large, one square
        mr[0x1A4] = 100;
        combat::Attack at = swing(F, M, {15, 12});
        expect(kPart, "large target: the long sword's 1d12 (12) + 2", at.hits[0].damage == 14, "%d", at.hits[0].damage);
        mr[0xDE] = 0x01;
        at = swing(F, M, {15, 12});
        expect(kPart, "man-sized target: 1d8 (the 12 face is an 8) + 2", at.hits[0].damage == 10, "%d", at.hits[0].damage);
        bare(F);
    }
    // -- Hit points: 0 unconscious; -1..-9 dying (bleeding by the overflow); -10 dead
    {
        const int P = F;
        auto set_hp = [&](int hp) {
            b.f[P].rec[0x1A4] = static_cast<uint8_t>(hp);
            b.f[P].rec[0x195] = party::Okay;
            b.f[P].rec[0x196] = 1;
            b.f[P].bleeding = 0;
            put(P, 20, 10, 2);
        };
        set_hp(10);
        combat::damage(b, P, 10);
        expect(kPart, "HP to 0: unconscious, out of the fight", b.f[P].status() == party::Unconscious && !b.f[P].up() && b.f[P].hp() == 0);
        set_hp(10);
        combat::damage(b, P, 13);
        expect(kPart, "HP to -3: dying, bleeding 3, HP kept 0", b.f[P].status() == party::Dying && b.f[P].bleeding == 3 && b.f[P].hp() == 0);
        // bleeding: +1 a round, dead past 9 (combat_rules_facts 4.2): 7 round ends
        for (int r = 0; r < 6; ++r) combat::end_round(b);
        const bool alive6 = b.f[P].status() == party::Dying;
        combat::end_round(b);
        expect(kPart, "dying at -3: dead at the 7th round's end", alive6 && b.f[P].status() == party::Dead, "bleeding %d",
               b.f[P].bleeding);
        set_hp(10);
        combat::damage(b, P, 19);
        expect(kPart, "HP to -9: dying", b.f[P].status() == party::Dying && b.f[P].bleeding == 9);
        combat::bandage(b);
        expect(kPart, "bandage: the dying one unconscious, bleeding stops", b.f[P].status() == party::Unconscious && b.f[P].bleeding == 0);
        set_hp(10);
        combat::damage(b, P, 20);
        expect(kPart, "HP to -10: dead", b.f[P].status() == party::Dead);
        set_hp(10);
        b.f[P].rec[0x195] = party::Animated;
        combat::damage(b, P, 10);
        expect(kPart, "animated dead at 0: dead", b.f[P].status() == party::Dead);
        set_hp(40);
        b.round = 1;
    }
    // -- Saving throws (the listing's do_saving_throw): d20, 1 fails, 20 saves,
    //    else roll + bonus + record 0x186 (a byte) >= the record's save (0xDF + kind)
    {
        combat::Fighter& f = b.f[F];
        const uint8_t saves[5] = {10, 11, 12, 13, 14};
        memcpy(f.rec + 0xDF, saves, 5);
        f.rec[0x186] = 0;
        combat::set_actor(b, F);
        bool all = true;
        for (int k = 0; k < 5; ++k) {
            dice_faces({saves[k]});
            const bool made = combat::saving_throw(f, k, 0, rng);
            dice_faces({saves[k] - 1});
            const bool failed = !combat::saving_throw(f, k, 0, rng);
            all = all && made && failed;
        }
        expect(kPart, "saves: each of the 5 kinds against its own number", all);
        dice_faces({8});
        expect(kPart, "saves: the bonus counts (8 + 2 >= 10)", combat::saving_throw(f, 0, 2, rng));
        f.rec[0x186] = 3;
        dice_faces({7});
        expect(kPart, "saves: record 0x186 counts (7 + 3 >= 10)", combat::saving_throw(f, 0, 0, rng));
        f.rec[0x186] = 0;
        dice_faces({1});
        expect(kPart, "saves: a natural 1 fails (bonus +30)", !combat::saving_throw(f, 0, 30, rng));
        f.rec[0xDF] = 40;
        dice_faces({20});
        expect(kPart, "saves: a natural 20 saves (needs 40)", combat::saving_throw(f, 0, 0, rng));
        f.rec[0xDF] = 15;
        dice_faces({2});
        expect(kPart, "saves: a total below 0 wraps round to a save (byte sum: 2 - 4)", combat::saving_throw(f, 0, -4, rng));
        dice_usual();
    }
    // -- Backstab (coab CanBackStabTarget): a thief straight behind a man-sized
    //    target that has had an attack this round: rear AC - 4, damage x ((level - 1) / 4 + 2)
    const int T = find_member(0x10F);
    expect(kPart, "the sample party has a thief", T >= 0);
    if (T >= 0) {
        uint8_t* tr = b.f[T].rec;
        // The target back on its feet, man-sized, facing `facing`, with `received` attacks this round
        auto ready_target = [&](int facing, int received) {
            mr[0xDE] = 1;
            mr[0x19A] = 60;                 // (front AC: needs a 20)
            mr[0x19B] = 54;                 // rear: 54 - 4 = 50 -> a 10 hits a backstab
            mr[0x1A4] = 100;
            mr[0x78] = 100;
            mr[0x195] = party::Okay;
            mr[0x196] = 1;
            put(M, 21, 10, facing);
            b.f[M].received = received;
            b.f[M].turns = 0;
        };
        auto stab = [&](std::initializer_list<int> faces) {
            b.f[T].attacks[0] = 1;
            b.f[T].attacks[1] = 0;
            dice_faces(faces);
            const combat::Attack at = combat::attack(b, T, M, names, rng);
            dice_usual();
            return at;
        };
        tr[0x199] = 40;
        tr[0x19E] = 1;
        tr[0x1A0] = 4;
        tr[0x1A2] = 0;
        put(T, 20, 10, 2);                  // the thief west of the target
        // One attack on it already (it faced that attacker); this one, the
        // second, turns it about (the listing's sub_3F9DB) - to face east,
        // away from the thief: straight behind it
        for (int lv : {1, 4, 5, 8, 9, 12, 13}) {
            tr[0x10F] = static_cast<uint8_t>(lv);
            ready_target(6, 1);
            const combat::Attack at = stab({10, 3});
            const int mul = (lv - 1) / 4 + 2;
            expect(kPart, "backstab: rear AC - 4, damage x ((level - 1) / 4 + 2)", at.backstab && at.n == 1 && at.hits[0].hit &&
                   at.hits[0].damage == 3 * mul, "level %d: %s, damage %d (want %d)", lv, at.backstab ? "backstabs" : "no backstab",
                   at.n ? at.hits[0].damage : -1, 3 * mul);
        }
        tr[0x10F] = 5;
        ready_target(6, 1);
        combat::Attack at = stab({9, 3});
        expect(kPart, "backstab: rear AC - 4 (54 - 4 = 50: a 9 + 40 misses)", at.backstab && at.n == 1 && !at.hits[0].hit);
        ready_target(2, 0);
        at = stab({20, 3});
        expect(kPart, "backstab: not on the first attack it receives (it turns to face them)", !at.backstab);
        ready_target(4, 1);
        at = stab({20, 3});
        expect(kPart, "backstab: not from the side", !at.backstab);
        ready_target(6, 1);
        mr[0xDE] = 0x81;
        at = stab({20, 3});
        expect(kPart, "backstab: not a large target", !at.backstab);
        tr[0x10F] = 0;
        ready_target(6, 1);
        at = stab({20, 3});
        expect(kPart, "backstab: not without thief levels", !at.backstab);
        tr[0x10F] = 5;
        hold(T, 38);
        ready_target(6, 1);
        at = stab({20, 3});
        expect(kPart, "backstab: not with a two-handed sword", !at.backstab);
        bool listed_ok = true;
        for (int w : orc["backstab_types"]) {
            hold(T, w);
            ready_target(6, 1);
            at = stab({20, 3});
            if (!at.backstab) {
                listed_ok = false;
                printf("  (no backstab with type %d)\n", w);
            }
        }
        expect(kPart, "backstab: with each of the thieves' weapons (drow long sword, club, dagger, broad / long / short sword)",
               listed_ok);
        bare(T);
        // The facing in a round: the 1st attack turns it to its attacker, each
        // later one about; a thief behind it backstabs as the 3rd, not the 2nd
        const int O = T == 0 ? 1 : 0;           // (another member: the first attacker)
        ready_target(2, 0);
        put(O, 22, 10, 6);                  // a fighter east of it
        b.f[O].attacks[0] = 1;
        b.f[O].attacks[1] = 0;
        dice_faces({1});
        combat::attack(b, O, M, names, rng);
        const int f1 = b.f[M].facing;
        at = stab({20, 3});
        const int f2 = b.f[M].facing;
        expect(kPart, "facing: the 1st attack faces the attacker; the 2nd (the thief behind it) turns it about - no backstab",
               f1 == 2 && f2 == 6 && !at.backstab, "facing %d then %d", f1, f2);
        // again: the fighter twice (faces it, then about), the thief 3rd: about again - its back to the thief
        ready_target(2, 0);
        for (int k = 0; k < 2; ++k) {
            b.f[O].attacks[0] = 1;
            dice_faces({1});
            combat::attack(b, O, M, names, rng);
        }
        at = stab({20, 3});
        expect(kPart, "facing: the thief as the 3rd attack (after two turns) backstabs", at.backstab, "facing %d", b.f[M].facing);
        b.f[O].size = 0;
        combat::occupancy(b);
    }
    // -- Sweeps (coab TrySweepAttack): fighters' level, targets of 0 Hit Dice next to them
    {
        put(F, 20, 10, 2);
        b.f[F].rec[0x10B] = 3;
        b.f[F].rec[0xDD] = 3;
        int ring[3] = {-1, -1, -1};
        int nm = 0;
        for (int i = M; i < b.n && nm < 3; ++i) ring[nm++] = i;
        (void)ring;
        // (only one monster loaded here: the sweep needs more - checked with a fresh arena below)
    }
    // -- Movement: a step costs the ground's cost x 2 half squares, a diagonal x 3
    {
        put(F, 20, 10, 2);
        b.f[M].size = 0;
        combat::occupancy(b);
        bool edge = false;
        int blocker = -1;
        const int c0 = combat::step_cost(b, fg->t, F, 2, &edge, &blocker);
        const int c1 = combat::step_cost(b, fg->t, F, 3, &edge, &blocker);
        expect(kPart, "a step: 2 half squares straight, 3 slanted (plain floor)", c0 == 2 && c1 == 3, "%d / %d", c0, c1);
        b.ground[10][21] = 0x30 + 0;            // (any ground of cost 2: a big rock outdoors' value)
        const int c2 = combat::step_cost(b, fg->t, F, 2, &edge, &blocker);
        const int cost = fg->t.ground[0x30][0];
        expect(kPart, "a step onto ground of cost c: 2c straight", c2 == 2 * cost, "%d (cost %d)", c2, cost);
        b.ground[10][21] = 0x17;
    }
    // -- Initiative (coab CalculateInitiative): d6 + Dex reaction, at least 1;
    //    the surprised side -6 (below 0: none); moves = movement x 2
    {
        for (int i = 0; i < b.n; ++i) b.f[i].size = 0;
        put(F, 20, 10, 2);
        put(M, 25, 10, 6);
        b.f[F].rec[0x17] = 17;                  // Dex 17: +2
        b.f[M].rec[0x17] = 3;                   // Dex 3: -3
        b.surprise = 0;
        dice_faces({4, 2});
        combat::start_round(b, rng);
        expect(kPart, "initiative: d6 4 + Dex 17's +2 = 6", b.f[F].delay == 6, "%d", b.f[F].delay);
        expect(kPart, "initiative: d6 2 + Dex 3's -3, at least 1", b.f[M].delay == 1, "%d", b.f[M].delay);
        b.surprise = 1;                         // our side surprised
        dice_faces({6, 6});
        combat::start_round(b, rng);
        expect(kPart, "surprise word 1: our side -6 (6 + 2 - 6 = 2), the enemies not", b.f[F].delay == 2 && b.f[M].delay == 3,
               "%d / %d", b.f[F].delay, b.f[M].delay);
        b.surprise = 2;                         // the enemies surprised
        dice_faces({3, 6});
        combat::start_round(b, rng);
        expect(kPart, "surprise word 2: the enemies -6 (6 - 3 - 6 < 0: no action)", b.f[M].delay == 0 && b.f[F].delay == 5,
               "%d / %d", b.f[F].delay, b.f[M].delay);
        expect(kPart, "surprise lasts one round's initiative", b.surprise == 0);
        dice_usual();
        // The highest delay acts first
        b.f[F].delay = 3;
        b.f[M].delay = 7;
        expect(kPart, "the next to act: the highest delay", combat::next(b, rng) == M);
        b.f[M].delay = 0;
        expect(kPart, "... then the rest; 0 delay: done", combat::next(b, rng) == F);
        b.f[F].delay = 0;
        expect(kPart, "the round is over when nobody has delay", combat::next(b, rng) == -1);
    }
    // -- 15 rounds without an attack end the fight (no-action limit)
    {
        b.round = 10;
        b.no_action = 15;
        b.f[F].attacks[0] = 1;
        put(F, 20, 10, 2);
        put(M, 21, 10, 6);
        b.f[M].rec[0x1A4] = 100;
        dice_faces({1});
        combat::attack(b, F, M, names, rng);
        dice_usual();
        expect(kPart, "an attack moves the limit to round + 15", b.no_action == 25, "%d", b.no_action);
        b.round = 23;
        const bool over1 = combat::end_round(b);
        const bool over2 = combat::end_round(b);
        expect(kPart, "the fight ends at the limit (round 25), not before", !over1 && over2, "%d %d", over1, over2);
    }
    // -- Fleeing (coab flee_battle): movement against the fastest enemy
    {
        b.round = 1;
        put(F, 20, 10, 2);
        put(M, 25, 10, 6);
        b.f[F].rec[0x1A5] = 12;
        b.f[M].rec[0x1A5] = 6;
        b.f[F].rec[0x195] = party::Okay;
        b.f[F].rec[0x196] = 1;
        expect(kPart, "flee: faster than every enemy - gets away", combat::flee(b, fg->t, F, rng) && b.f[F].status() == party::Running);
        b.f[F].rec[0x195] = party::Okay;
        b.f[F].rec[0x196] = 1;
        put(F, 20, 10, 2);
        b.f[M].rec[0x1A5] = 15;
        expect(kPart, "flee: slower than an enemy - escape blocked", !combat::flee(b, fg->t, F, rng) && b.f[F].up());
        b.f[M].rec[0x1A5] = 12;
        dice_faces({1});
        const bool away = combat::flee(b, fg->t, F, rng);
        b.f[F].rec[0x195] = party::Okay;
        b.f[F].rec[0x196] = 1;
        put(F, 20, 10, 2);
        dice_faces({2});
        const bool stays = !combat::flee(b, fg->t, F, rng);
        dice_usual();
        expect(kPart, "flee: as fast: d2 = 1 gets away, 2 doesn't", away && stays);
    }
}

// ---- B2: more rules, with several monsters
void rules_more()
{
    using namespace play;
    // -- Sweeps (coab TrySweepAttack): attack level (fighters: their level),
    //    fewer attacks left than it, the target 0 Hit Dice and next to them,
    //    more such enemies next to them than attacks left: one each, up to the level
    if (!arena(1, 83, 4)) {
        expect(kPart, "a fight (4 lizard men)", false);
        return;
    }
    combat::Battle& b = *B;
    const int F = find_member(0x10B);
    b.f[F].rec[0xDD] = 4;
    put(F, 20, 10, 2);
    for (int k = 0; k < 4; ++k) b.f[M + k].rec[0xE5] = 0;
    put(M, 21, 10, 6);
    put(M + 1, 21, 9, 6);
    put(M + 2, 21, 11, 6);
    put(M + 3, 30, 10, 6);                    // (not next to them)
    b.f[F].attacks[0] = 1;
    b.f[F].swept = false;
    int sw[8];
    int n = combat::sweep(b, F, M, sw, 8);
    expect(kPart, "sweep: level 4, 1 attack, 3 of 0 HD next to them: all 3, the target first", n == 3 && sw[0] == M, "%d", n);
    b.f[F].rec[0xDD] = 2;
    n = combat::sweep(b, F, M, sw, 8);
    expect(kPart, "sweep: no more than the level (2)", n == 2, "%d", n);
    b.f[F].rec[0xDD] = 1;
    n = combat::sweep(b, F, M, sw, 8);
    expect(kPart, "sweep: attack level 1 (not a fighter): none", n == 0, "%d", n);
    b.f[F].rec[0xDD] = 4;
    b.f[M + 1].size = b.f[M + 2].size = 0;
    combat::occupancy(b);
    n = combat::sweep(b, F, M, sw, 8);
    expect(kPart, "sweep: one alone next to them (no more than the attacks left): none", n == 0, "%d", n);
    put(M + 1, 21, 9, 6);
    put(M + 2, 21, 11, 6);
    b.f[M].rec[0xE5] = 1;
    n = combat::sweep(b, F, M, sw, 8);
    expect(kPart, "sweep: a target of 1 Hit Die: none", n == 0, "%d", n);
    b.f[M].rec[0xE5] = 0;
    b.f[F].attacks[0] = 1;
    dice_faces({1});
    combat::attack(b, F, M + 3, names, rng);      // (any attack first this round)
    dice_usual();
    b.f[F].attacks[0] = 1;
    n = combat::sweep(b, F, M, sw, 8);
    expect(kPart, "sweep: not after an attack this round (the original zeroes the count)", n == 0, "%d", n);
    // a monster of a race with fighter levels (record 0xDD after the setup) sweeps too
    b.f[F].swept = false;

    // -- Free attacks on one stepping away (coab move_step_away_attack)
    {
        for (int k = 0; k < 4; ++k) b.f[M + k].size = 0;
        put(F, 20, 10, 2);
        put(M, 21, 10, 6);                    // facing F
        b.f[M].delay = 0;
        b.f[M].received = 1;
        expect(kPart, "free attack: an enemy facing them (acted, attacked) gets it", combat::free_attack_ok(b, fg->t, M, F));
        b.f[M].facing = 2;                    // back to them
        expect(kPart, "free attack: not from one turned away that has acted and been attacked",
               !combat::free_attack_ok(b, fg->t, M, F));
        b.f[M].delay = 3;
        expect(kPart, "free attack: one that hasn't acted yet this round always", combat::free_attack_ok(b, fg->t, M, F));
        b.f[M].delay = 0;
        b.f[M].received = 0;
        expect(kPart, "free attack: one not attacked this round always", combat::free_attack_ok(b, fg->t, M, F));
        b.f[M].facing = 0;                    // north: F (west) is 2 off its facing
        b.f[M].received = 1;
        expect(kPart, "free attack: in its front arc (facing +- 2)", combat::free_attack_ok(b, fg->t, M, F));
    }
    // -- Morale (coab FleeCheck): (control & 0x7F) x 2 against the share of
    //    hit points lost; then the enemies' health against 100 - the script's morale
    {
        combat::Fighter& m = b.f[M];
        put(M, 30, 10, 6);
        put(F, 20, 10, 2);
        m.rec[0xF7] = 0x80 + 25;              // morale 50
        m.rec[0x78] = 100;
        m.rec[0x1A4] = 60;                    // 40% lost
        b.enemy_health = 50;
        b.morale_base = 60;                   // 100 - 60 = 40
        expect(kPart, "morale: 50 against 40% lost - fights", combat::morale(b, fg->t, M) == combat::Morale::Fight);
        m.rec[0x1A4] = 40;                    // 60% lost; side health 50 >= 40: fights on
        expect(kPart, "morale: below, but its side's health 50 >= 100 - 60: fights", combat::morale(b, fg->t, M) == combat::Morale::Fight);
        b.enemy_health = 30;
        m.rec[0x1A5] = 12;
        b.f[F].rec[0x1A5] = 12;
        expect(kPart, "morale: side's health 30 < 40, no enemy faster: flees", combat::morale(b, fg->t, M) == combat::Morale::Flee);
        b.f[F].rec[0x1A5] = 15;
        m.rec[0x13] = 10;
        expect(kPart, "morale: an enemy faster, Int over 5: surrenders", combat::morale(b, fg->t, M) == combat::Morale::Surrender);
        m.rec[0x13] = 5;
        expect(kPart, "morale: an enemy faster, Int 5: fights on", combat::morale(b, fg->t, M) == combat::Morale::Fight);
        m.rec[0xF7] = 0x80 + 60;              // over 102: 0 - always the second test
        m.rec[0x1A4] = 100;
        b.enemy_health = 30;
        b.f[F].rec[0x1A5] = 12;
        expect(kPart, "morale: (control & 0x7F) x 2 over 102 counts as 0", combat::morale(b, fg->t, M) == combat::Morale::Flee);
        m.rec[0xF7] = 0x10;                   // a player-controlled one: no morale
        expect(kPart, "morale: only for NPC / monster control (0x80+)", combat::morale(b, fg->t, M) == combat::Morale::Fight);
        m.rec[0xF7] = 0x80;
    }
    // -- Experience (coab addExp): the share among those standing (not
    //    animated); +10% for prime requisites over 15; multi-classes / classes
    {
        for (int i = 0; i < b.party_size; ++i) {
            b.f[i].rec[0x195] = party::Okay;
            b.f[i].rec[0x196] = 1;
        }
        b.f[b.party_size - 1].rec[0x196] = 0;           // one out of the fight
        b.f[b.party_size - 1].rec[0x195] = party::Unconscious;
        uint32_t before[8];
        for (int i = 0; i < b.party_size; ++i) before[i] = pt->m[i].exp();
        const int standing = b.party_size - 1;
        const int share = combat::award(b, 1000 * standing);
        bool ok = share == 1000;
        for (int i = 0; i < b.party_size; ++i) {
            const uint8_t* r = pt->m[i].rec;
            int want = 1000;
            const int cl = r[0x75];
            const int str = r[0x11], in = r[0x13], wis = r[0x15], dex = r[0x17];
            if (cl == 0 && wis > 15) want = 1100;
            if (cl == 2 && str > 15) want = 1100;
            if (cl == 3 && str > 15 && wis > 15) want = 1100;
            if (cl == 4 && str > 15 && in > 15 && wis > 15) want = 1100;
            if (cl == 5 && in > 15) want = 1100;
            if (cl == 6 && dex > 15) want = 1100;
            if (cl == 8 || (cl >= 10 && cl <= 14) || cl == 16) want = 500;
            if (cl == 9 || cl == 15) want = 333;
            if (i == b.party_size - 1) want = 0;
            const uint32_t got = pt->m[i].exp() - before[i];
            if (got != static_cast<uint32_t>(want)) {
                ok = false;
                printf("  (member %d class %d: %u, want %d)\n", i, cl, got, want);
            }
        }
        expect(kPart, "experience: shares, prime requisites, multi-classes", ok, "share %d", share);
        // the money's worth (cp + 10 sp + 100 ep + 200 gp + 1000 pp) / 200 + 250 a gem + 2200 a jewel
        const int money[7] = {200, 20, 2, 1, 1, 2, 1};
        expect(kPart, "experience: the money's worth", combat::money_exp(money) == (200 + 200 + 200 + 200 + 1000) / 200 + 500 + 2200,
               "%d", combat::money_exp(money));
    }
    // -- Turn undead (the listing's turn_undead): d12 undead at most, one d20,
    //    the weakest first (record 0xE9), the table by level; T turns, D / D* destroy
    {
        const int C = find_member(0x109);
        if (C < 0) {
            expect(kPart, "a cleric in the sample party", false);
            return;
        }
        // the program's table against the DMG's (types 1-10: past them the
        // program's bytes are its text - the original reads the same; no Curse
        // monster has an undead type at all). Differences: UNSURE lines
        int diffs = 0;
        for (int type = 1; type <= 10; ++type)
            for (int col = 1; col <= 10; ++col) {
                char key[12];
                snprintf(key, sizeof key, "turn%d", type);
                const int want = ov(key, col - 1);
                const int got = static_cast<int8_t>(fg->t.turn[type * 10 + col]);
                const bool match = want == 99 ? (got > 20 || got < -20) : got == want;
                if (!match) {
                    ++diffs;
                    printf("  UNSURE turn table type %d column %d: program %d, the DMG as remembered %d\n", type, col, got, want);
                }
            }
        expect(kPart, "the program's turn table is close to the DMG's (T = 1, D = 0, D* = -1; the engine uses the program's)",
               diffs <= 2, "%d cells differ", diffs);
        for (int i = 0; i < b.n; ++i) b.f[i].size = 0;
        put(C, 20, 10, 2);
        for (int k = 0; k < 4; ++k) {
            combat::Fighter& u = b.f[M + k];
            u.rec[0x195] = party::Okay;
            u.rec[0x196] = 1;
            u.rec[0xE9] = 1;                  // skeletons
            u.fleeing = false;
            put(M + k, 24 + k, 10, 6);
        }
        b.f[C].rec[0x109] = 3;                // 3rd level: needs 4
        int out[16];
        dice_faces({4, 4});                   // d12 = 4 of them, d20 = 4
        n = combat::turn_undead(b, fg->t, C, rng, out, 16);
        dice_usual();
        expect(kPart, "turn: level 3 vs skeletons, d20 4 (needs 4): d12 4 turned", n == 4 && out[0] < 1000, "%d", n);
        for (int k = 0; k < 4; ++k) {
            b.f[M + k].fleeing = false;
        }
        dice_faces({4, 3});
        n = combat::turn_undead(b, fg->t, C, rng, out, 16);
        dice_usual();
        expect(kPart, "turn: d20 3 (needs 4): nothing happens", n == 0, "%d", n);
        b.f[C].rec[0x109] = 6;                // D
        dice_faces({2, 1});                   // d12 = 2: 2 destroyed (no extra for D)
        n = combat::turn_undead(b, fg->t, C, rng, out, 16);
        dice_usual();
        expect(kPart, "turn: level 6 (D): d12 2 destroyed, no more", n == 2 && out[0] >= 1000 && out[1] >= 1000, "%d", n);
        for (int k = 0; k < 4; ++k) {
            combat::Fighter& u = b.f[M + k];
            u.rec[0x195] = party::Okay;
            u.rec[0x196] = 1;
            put(M + k, 24 + k, 10, 6);
        }
        b.f[C].rec[0x109] = 8;                // D*
        dice_faces({2, 1});                   // d12 = 2, but D*: one more while the 6 counter lasts
        n = combat::turn_undead(b, fg->t, C, rng, out, 16);
        dice_usual();
        expect(kPart, "turn: level 8 (D*): d12 2 - each the count runs out one more (all 4)", n == 4, "%d", n);
    }
}

// ---- C: the monsters' special abilities (monster_fx_facts.md, checked
// against the listing there), each with a forced save made AND failed
int P = 0;                                  // the victim: a party member

// A fight with one MON<area> #id next to party member P (P west, facing it)
bool vs(int area, int id, int copies = 1)
{
    if (!arena(area, id, copies)) {
        expect(kPart, "a fight with it", false, "MON%d #%d", area, id);
        return false;
    }
    combat::Battle& b = *B;
    P = 0;
    put(P, 20, 10, 2);
    put(M, 21, 10, 6);
    uint8_t* r = b.f[P].rec;
    r[0x195] = party::Okay;
    r[0x196] = 1;
    r[0x78] = 100;
    r[0x1A4] = 100;
    for (int k = 0; k < 5; ++k) r[0xDF + k] = 11;     // saves 11: a 11 makes it, a 10 doesn't
    r[0x186] = 0;
    // (no effects: the sample paladin's protection from evil would add 2 to its saves against evil ones)
    for (int k = 0; k < *b.f[P].n_aff; ++k) printf("  (member %d had effect %02X)\n", P, b.f[P].aff[k][0]);
    *b.f[P].n_aff = 0;
    b.f[M].rec[0x1A4] = b.f[M].rec[0x78];
    return true;
}
void heal_p()
{
    uint8_t* r = B->f[P].rec;
    r[0x195] = party::Okay;
    r[0x196] = 1;
    r[0x1A4] = 100;
    *B->f[P].n_aff = 0;
    put(P, 20, 10, 2);
}

// The monster's attack with one slot only (0: attack 1, 1: attack 2), the
// to-hit high (any roll but a 1 hits)
combat::Attack blow(int slot, std::initializer_list<int> faces, int a = -1, int t = -1)
{
    combat::Battle& b = *B;
    if (a < 0) a = M;
    if (t < 0) t = P;
    combat::Fighter& f = b.f[a];
    f.rec[0x199] = 100;
    f.attacks[0] = slot == 0 ? 1 : 0;
    f.attacks[1] = slot == 1 ? 1 : 0;
    b.f[t].received = 0;
    dice_faces(faces);
    const combat::Attack at = combat::attack(b, a, t, play::names, play::rng);
    dice_usual();
    return at;
}
bool has_ev(const combat::Attack& at, combat::Ev e)
{
    for (int k = 0; k < at.n_ev; ++k)
        if (at.ev[k].ev == e) return true;
    return false;
}
int aff_index(const combat::Fighter& f, int type)
{
    for (int k = 0; f.n_aff && k < *f.n_aff; ++k)
        if (f.aff[k][0] == type) return k;
    return -1;
}

void specials()
{
    using namespace play;
    if (!boot()) return;
    const combat::MonFx mx = d->prof->fight.facts.mon;
    // -- Poison (0x40 +0: giant spider, MON6 #66; its bite is attack 2):
    //    a save against poison (type 0) or "is Poisoned", killed
    if (vs(6, 66)) {
        combat::Battle& b = *B;
        combat::Attack at = blow(1, {15, 3, 10});         // hit, 1d8 = 3, save 10: fails
        expect(kPart, "giant spider: failed save vs poison - poisoned, killed", has_ev(at, combat::Ev::Poisoned) &&
               b.f[P].status() == party::Dead && aff_index(b.f[P], mx.poisoned) >= 0, "status %d", b.f[P].status());
        heal_p();
        at = blow(1, {15, 3, 11});
        expect(kPart, "giant spider: save made - only the bite's damage", !has_ev(at, combat::Ev::Poisoned) &&
               b.f[P].status() == party::Okay && b.f[P].hp() == 97, "hp %d", b.f[P].hp());
        heal_p();
        at = blow(0, {15, 3, 10});
        expect(kPart, "giant spider: no attack 1 (none in its record): nothing", b.f[P].hp() == 100, "hp %d", b.f[P].hp());
    }
    // -- Poison -2 (0x46: phase spider, MON6 #65)
    if (vs(6, 65)) {
        combat::Battle& b = *B;
        combat::Attack at = blow(1, {15, 3, 12});        // 12 - 2 = 10 < 11: fails
        expect(kPart, "phase spider: save at -2 (12 - 2 fails)", b.f[P].status() == party::Dead);
        heal_p();
        at = blow(1, {15, 3, 13});
        expect(kPart, "phase spider: 13 - 2 saves", b.f[P].status() == party::Okay);
        // Blink (0x25): attacks on it miss once it has acted this round
        b.f[M].delay = 0;
        b.f[P].attacks[0] = 1;
        b.f[P].rec[0x199] = 100;
        b.f[M].received = 0;
        dice_faces({19, 3});
        at = combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "phase spider blinks: having acted, it isn't hit", at.n == 1 && !at.hits[0].hit);
        b.f[M].delay = 3;
        b.f[P].attacks[0] = 1;
        dice_faces({19, 3});
        at = combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "phase spider: before it acts it can be hit", at.n == 1 && at.hits[0].hit);
        expect(kPart, "phase spider: hidden from the computer once it has acted", [&]() {
            b.f[M].delay = 0;
            return combat::hidden(b, P, M);
        }());
    }
    // -- Thri-kreen (0x43 bite = attack 2: save or paralysed 2d8 rounds; 0x68 dodges 60% of missiles)
    if (vs(6, 64)) {
        combat::Battle& b = *B;
        combat::Attack at = blow(1, {15, 2, 10, 3, 4});
        const int k = aff_index(b.f[P], mx.paralyzed);
        const int mins = k >= 0 ? (b.f[P].aff[k][1] | b.f[P].aff[k][2] << 8) : -1;
        expect(kPart, "thri-kreen bite: failed save - paralysed 2d8 (3 + 4) rounds, data 12", has_ev(at, combat::Ev::Paralyzed) &&
               mins == 7 && k >= 0 && b.f[P].aff[k][3] == 12, "minutes %d", mins);
        expect(kPart, "paralysed: helpless", combat::helpless(b, b.f[P]));
        b.f[M].attacks[0] = 1;
        b.f[M].attacks[1] = 1;
        b.f[P].received = 0;
        const int hp = b.f[P].hp();
        dice_faces({2});
        at = combat::attack(b, M, P, names, rng);
        dice_usual();
        expect(kPart, "a helpless one: slain with one cruel blow (HP + 5: dying at -5), all its attacks used", at.slain &&
               b.f[P].status() == party::Dying && b.f[P].bleeding == 5 && b.f[M].attacks[0] == 0 && b.f[M].attacks[1] == 0,
               "hp was %d, status %d bleeding %d", hp, b.f[P].status(), b.f[P].bleeding);
        heal_p();
        at = blow(1, {15, 2, 11});
        expect(kPart, "thri-kreen bite: save made - not paralysed", aff_index(b.f[P], mx.paralyzed) < 0);
        // missiles: a shot from 2+ squares away - 60% "Avoids it"
        heal_p();
        put(P, 15, 10, 2);
        hold(P, 43);                              // a long bow ...
        party::Character& ch = pt->m[P];
        ch.n_items = 2;
        memset(ch.items[1], 0, items::kRecordSize);
        ch.items[1][0x2E] = static_cast<uint8_t>(ov("arrow", 0));
        ch.items[1][0x34] = 1;
        ch.items[1][0x39] = 10;
        b.f[P].n_items = 2;
        b.f[P].attacks[0] = 1;
        b.f[P].rec[0x199] = 100;
        b.f[M].received = 0;
        dice_faces({15, 3, 60});
        at = combat::attack(b, P, M, names, rng);
        b.f[P].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 3, 61});
        const combat::Attack at2 = combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "thri-kreen: d100 60 avoids an arrow, 61 doesn't", at.avoided[0] && !at2.avoided[0] && at2.hits[0].hit);
        bare(P);
    }
    // -- The dracolich (MON1 #60): its touch (attack 1) paralyses (save,
    //    time 0) and chills 2d8 (cold + magic); fire breath; immunities
    if (vs(1, 60)) {
        combat::Battle& b = *B;
        put(M, 21, 10, 6);
        combat::Attack at = blow(0, {15, 1, 1, 1, 10, 4, 4});   // 3d8 = 3, save 10 fails, chill 2d8 = 8
        const int k = aff_index(b.f[P], mx.paralyzed);
        expect(kPart, "dracolich touch: failed save - paralysed until cured (time 0)", has_ev(at, combat::Ev::ParalyzedLow) && k >= 0 &&
               (b.f[P].aff[k][1] | b.f[P].aff[k][2] << 8) == 0);
        expect(kPart, "dracolich touch: chill 2d8 (8) besides the blow (3)", b.f[P].hp() == 100 - 3 - 8, "hp %d", b.f[P].hp());
        bool kind_ok = false;
        for (int j = 0; j < at.n_ev; ++j)
            if (at.ev[j].ev == combat::Ev::Damage && at.ev[j].kind == 0x0A) kind_ok = true;
        expect(kPart, "dracolich chill: cold + magic (0x0A), not acid (coab differs)", kind_ok);
        heal_p();
        at = blow(0, {15, 1, 1, 1, 11, 4, 4});
        expect(kPart, "dracolich touch: save made - not paralysed", aff_index(b.f[P], mx.paralyzed) < 0);
        // non-magic weapons do nothing to it (0x77): a party member with a +0 sword
        hold(P, 36, 0);
        b.f[P].rec[0x19E] = 1; b.f[P].rec[0x1A0] = 8; b.f[P].rec[0x1A2] = 0;
        b.f[P].rec[0x199] = 100;
        b.f[P].attacks[0] = 1;
        b.f[M].rec[0x1A4] = 66;
        b.f[M].received = 0;
        dice_faces({15, 5});
        at = combat::attack(b, P, M, names, rng);
        expect(kPart, "dracolich: a +0 sword does nothing", at.hits[0].hit && at.hits[0].damage == 0 && b.f[M].hp() == 66);
        hold(P, 36, 1);
        b.f[P].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 5});
        at = combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "dracolich: a +1 sword hurts it (the 12 large die face 5)", at.hits[0].damage > 0, "%d", at.hits[0].damage);
        bare(P);
        // immune to cold, electricity; spells' cold "Protected"
        combat::Harm h;
        h.kind = 2;
        bool dn = false, res = false;
        expect(kPart, "dracolich: cold does nothing (0x6E)", combat::harm(b, M, 20, h, rng, &dn, &res) == 0 && res);
        h.kind = 4;
        expect(kPart, "dracolich: electricity does nothing (0x87)", combat::harm(b, M, 20, h, rng, &dn, &res) == 0 && res);
        h.kind = 0x40;
        expect(kPart, "dracolich: death magic (0x40) does nothing (0x85)", combat::harm(b, M, 20, h, rng, &dn, &res) == 0 && res);
        h.kind = 1;
        b.f[M].rec[0xDF + 3] = 30;
        const int fire = combat::harm(b, M, 20, h, rng, &dn, &res);
        expect(kPart, "dracolich: fire hurts it", fire == 20, "%d", fire);
        // its mind (0x7D): every save against poison / paralysis / death made - but a natural 1
        combat::set_actor(b, P);
        b.f[M].rec[0xDF] = 30;
        dice_faces({2});
        const bool s2 = combat::saving_throw(b.f[M], 0, 0, rng);
        dice_faces({1});
        const bool s1 = combat::saving_throw(b.f[M], 0, 0, rng);
        dice_faces({2});
        const bool sb1 = combat::saving_throw(b.f[M], 1, 0, rng);
        dice_usual();
        expect(kPart, "0x7D: type-0 saves made on any roll but a 1; other kinds as ever", s2 && !s1 && !sb1);
        // Fire breath: max HP (66) to those on 3 lines of 9, save vs breath for half, 3 a fight; ends the turn
        b.round = 0;
        b.f[M].rec[0x1A4] = 66;
        put(M, 25, 10, 6);
        put(P, 20, 10, 2);
        combat::Event ev[24];
        bool ends = false;
        dice_faces({1, 10});                       // the target pick, P's save 10: fails
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        int dmg = -1;
        for (int j = 0; j < n; ++j)
            if (ev[j].ev == combat::Ev::Damage && ev[j].who == P) dmg = ev[j].amount;
        const int kb = aff_index(b.f[M], mx.fire_breath);
        expect(kPart, "dracolich breathes fire: its maximum HP (66), the turn over, 2 breaths left", n > 0 && ev[0].ev == combat::Ev::BreathesFire &&
               ends && (dmg == 66 || b.f[P].status() != party::Okay) && kb >= 0 && b.f[M].aff[kb][3] == 2, "damage %d", dmg);
        heal_p();
        b.round = 1;
        dice_faces({1, 11});
        n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        dmg = -1;
        for (int j = 0; j < n; ++j)
            if (ev[j].ev == combat::Ev::Damage && ev[j].who == P) dmg = ev[j].amount;
        expect(kPart, "dracolich breath: a save vs breath halves it (33)", dmg == 33, "%d", dmg);
        b.f[M].aff[kb][3] = 0;
        n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        bool breathed = false;
        for (int j = 0; j < n; ++j) breathed = breathed || ev[j].ev == combat::Ev::BreathesFire;
        expect(kPart, "dracolich: no breaths left - none (the gaze 0x7E never fires either)", !breathed, "%d events", n);
    }
    // -- The black dragon (MON1 #53): acid down a line of 6, max HP (48), 3 a fight; not with a friend on it
    if (vs(1, 53, 2)) {
        combat::Battle& b = *B;
        b.round = 0;
        put(M, 25, 10, 6);
        put(P, 20, 10, 2);
        combat::Event ev[24];
        bool ends = false;
        dice_faces({1, 10});
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        int dmg = -1;
        for (int j = 0; j < n; ++j)
            if (ev[j].ev == combat::Ev::Damage && ev[j].who == P) dmg = ev[j].amount;
        expect(kPart, "black dragon breathes acid: 48 (its maximum HP), save failed; the turn over", n > 0 &&
               ev[0].ev == combat::Ev::BreathesAcid && ends && (dmg == 48 || b.f[P].status() != party::Okay), "damage %d", dmg);
        heal_p();
        put(M + 1, 22, 10, 6);                     // another dragon between: a friend on the line
        b.round = 1;
        n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        expect(kPart, "black dragon: a friend on the line - no breath (coab inverted)", n == 0 && !ends, "%d events", n);
    }
    // -- The hell hound (MON6 #68): d100 <= 50 and next to its pick: 7 fire, save for half
    if (vs(6, 68)) {
        combat::Battle& b = *B;
        combat::Event ev[24];
        bool ends = false;
        dice_faces({1, 50, 10});
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "hell hound: d100 50, next to it: breathes fire for 7", n > 0 && ev[0].ev == combat::Ev::HoundFire && ends &&
               b.f[P].hp() == 93, "hp %d", b.f[P].hp());
        heal_p();
        dice_faces({1, 51});
        n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "hell hound: d100 51 - no breath, it fights", n == 0 && !ends);
        dice_faces({1, 50, 11});
        n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "hell hound: a save halves the 7 (3)", b.f[P].hp() == 97, "hp %d", b.f[P].hp());
    }
    // -- The giant slug (MON3 #30): 30% of its turns within 6: 60 (its max HP) acid, save half; it fights on
    if (vs(3, 30)) {
        combat::Battle& b = *B;
        put(M, 24, 10, 6);
        combat::Event ev[24];
        bool ends = false;
        dice_faces({1, 30, 11});
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "giant slug spits: 60 halved by the save (30); doesn't end its turn", n > 0 && ev[0].ev == combat::Ev::SpitsAcid &&
               !ends && b.f[P].hp() == 70, "hp %d", b.f[P].hp());
        heal_p();
        dice_faces({1, 31});
        n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "giant slug: d100 31 - \"Spits Acid and Misses\"", n > 0 && ev[0].ev == combat::Ev::SpitsMisses && b.f[P].hp() == 100);
    }
    // -- The ankheg (MON5 #56): acid bite +1d4 (attack 1); spits once (d100 <= 25, within 3): 8d4 save half
    if (vs(5, 56)) {
        combat::Battle& b = *B;
        blow(0, {15, 1, 1, 1, 3});                 // 3d6 = 3, acid 1d4 = 3
        expect(kPart, "ankheg bite: + 1d4 acid", b.f[P].hp() == 94, "hp %d", b.f[P].hp());
        heal_p();
        combat::Event ev[24];
        bool ends = false;
        b.f[M].target = P;
        dice_faces({1, 25, 4, 4, 4, 4, 4, 4, 4, 4, 10});
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "ankheg spits: 8d4 (32), save failed; ends the turn; spit and acid bite gone", n > 0 &&
               ev[0].ev == combat::Ev::SpitsAcid && ends && b.f[P].hp() == 68 && aff_index(b.f[M], mx.ankheg_spit) < 0 &&
               aff_index(b.f[M], mx.acid_bite) < 0, "hp %d", b.f[P].hp());
    }
    // -- Tyranthraxus (MON6 #71): fire 2d10 after any hit; thrown lightning in rounds 0-3
    if (vs(6, 71)) {
        combat::Battle& b = *B;
        put(M, 21, 10, 6);
        blow(0, {15, 1, 1, 1, 1, 1, 1, 1, 5, 5});      // 7d6 = 7, fire 2d10 = 10
        expect(kPart, "Tyranthraxus: 2d10 fire after its hit", b.f[P].hp() == 83, "hp %d", b.f[P].hp());
        heal_p();
        b.round = 4;
        combat::Event ev[24];
        bool ends = false;
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        bool thrown = false;
        for (int j = 0; j < n; ++j) thrown = thrown || ev[j].ev == combat::Ev::ThrowsLightning;
        expect(kPart, "Tyranthraxus: no lightning from round 4", !thrown);
        b.round = 1;
        put(M, 30, 10, 6);
        std::vector<int> faces = {1};
        for (int k = 0; k < 16; ++k) faces.push_back(2);    // 16d6 = 32
        faces.push_back(11);                                // save made: 16
        dice_usual();
        b_faces.assign(faces.begin(), faces.end());
        n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "Tyranthraxus throws lightning: 16d6 (32), save vs spells halves it (16); ends the turn", ends &&
               b.f[P].hp() == 84, "hp %d", b.f[P].hp());
        // immune to fire, electricity; 15% magic resistance
        combat::Harm h;
        h.kind = 1;
        bool dn, res;
        expect(kPart, "Tyranthraxus: immune to fire", combat::harm(b, M, 30, h, rng, &dn, &res) == 0);
        h.kind = 4;
        expect(kPart, "Tyranthraxus: immune to electricity", combat::harm(b, M, 30, h, rng, &dn, &res) == 0);
        h.kind = 8;
        h.level = 9;                                 // 15 + (11 - 9) x 5 = 25
        dice_faces({25});
        const int r25 = combat::harm(b, M, 30, h, rng, &dn, &res);
        dice_faces({26});
        const int r26 = combat::harm(b, M, 30, h, rng, &dn, &res);
        dice_usual();
        expect(kPart, "magic resistance 15%: a 9th level caster's magic resisted on d100 <= 25", r25 == 0 && r26 == 30, "%d %d", r25, r26);
    }
    // -- The salamander (MON5 #57): +1d6 heat a hit; non-magic weapons nothing; fire, charm / sleep, paralysis immune
    if (vs(5, 57)) {
        combat::Battle& b = *B;
        blow(0, {15, 1, 1, 4});                    // 2d6 = 2, heat 4
        expect(kPart, "salamander: + 1d6 heat to its hit", b.f[P].hp() == 94, "hp %d", b.f[P].hp());
        heal_p();
        // resist fire (0x14) keeps the heat off
        uint8_t* a = b.f[P].aff[(*b.f[P].n_aff)++];
        memset(a, 0, party::kAffectSize);
        a[0] = mx.heat_proof[0];
        blow(0, {15, 1, 1, 4});
        expect(kPart, "salamander: no heat for one resisting fire", b.f[P].hp() == 98, "hp %d", b.f[P].hp());
        heal_p();
        // 0x77: a monster of 4+ Hit Dice hurts it bare-handed; one of less doesn't
        combat::Harm h;
        combat::Fighter& s = b.f[M];
        bool dn, res;
        h.kind = 1;
        expect(kPart, "salamander: immune to fire", combat::harm(b, M, 20, h, rng, &dn, &res) == 0);
        h = combat::Harm{};
        expect(kPart, "salamander: no sleep or charm, no paralysis (spells' effects)", combat::resists(b, M, mx.sleep, h, rng) &&
               combat::resists(b, M, d->prof->fight.facts.charm, h, rng) && combat::resists(b, M, mx.paralyzed, h, rng));
        (void)s;
    }
    // -- Weapon defences: the rakshasa (MON4 #67) - none / +0 nothing, +1 / +2 half,
    //    +3 full; a blessed quarrel slays it; magic of any kind none
    if (vs(4, 67)) {
        combat::Battle& b = *B;
        b.f[P].rec[0x199] = 100;
        b.f[P].rec[0x19E] = 1; b.f[P].rec[0x1A0] = 8; b.f[P].rec[0x1A2] = 0;
        auto hit_with = [&](int plus) {
            if (plus < 0) bare(P);
            else hold(P, 36, plus);
            b.f[P].attacks[0] = 1;
            b.f[P].attacks[1] = 0;
            b.f[M].rec[0x1A4] = 35;
            b.f[M].received = 0;
            dice_faces({15, 8});
            const combat::Attack at = combat::attack(b, P, M, names, rng);
            dice_usual();
            return at.hits[0].damage;
        };
        const int bare_d = hit_with(-1), p0 = hit_with(0), p1 = hit_with(1), p2 = hit_with(2), p3 = hit_with(3);
        expect(kPart, "rakshasa: bare / +0 nothing, +1 / +2 half, +3 full (8)", bare_d == 0 && p0 == 0 && p1 == 4 && p2 == 4 && p3 == 8,
               "%d %d %d %d %d", bare_d, p0, p1, p2, p3);
        combat::Harm h;
        h.kind = 8;
        bool dn, res;
        expect(kPart, "rakshasa: magic damage none (0x81)", combat::harm(b, M, 20, h, rng, &dn, &res) == 0);
        h.kind = 0;
        expect(kPart, "rakshasa: a spell's effect resisted (0x81)", combat::resists(b, M, mx.sleep, h, rng));
        bare(P);
    }
    // -- Half damage (0x51: Alias, MON3 #22) and the vegepygmies' 1 from piercing weapons (0x55: MON3 #27)
    if (vs(3, 22)) {
        combat::Battle& b = *B;
        b.f[M].rec[0x197] = 1;                  // (on the other side for the test)
        b.f[P].rec[0x199] = 100;
        b.f[P].rec[0x19E] = 1; b.f[P].rec[0x1A0] = 8; b.f[P].rec[0x1A2] = 0;
        bare(P);
        b.f[P].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 7});
        const combat::Attack at = combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "Alias: a blow's damage halved (7 -> 3)", at.hits[0].damage == 3, "%d", at.hits[0].damage);
        // invisible from the start (0x8A): -4 to hit for those without detect invisibility
        const int inv = aff_index(b.f[M], d->prof->fight.facts.invisible);
        expect(kPart, "Alias: invisible from the battle's start", inv >= 0);
    }
    if (vs(3, 27)) {
        combat::Battle& b = *B;
        b.f[P].rec[0x199] = 100;
        auto hit_type = [&](int type) {
            hold(P, type);
            b.f[P].rec[0x19E] = 1; b.f[P].rec[0x1A0] = 8; b.f[P].rec[0x1A2] = 0;
            b.f[P].attacks[0] = 1;
            b.f[P].attacks[1] = 0;
            b.f[M].rec[0x1A4] = 15;
            b.f[M].received = 0;
            dice_faces({15, 7});
            const combat::Attack at = combat::attack(b, P, M, names, rng);
            dice_usual();
            return at.hits[0].damage;
        };
        const int spear = hit_type(31), sword = hit_type(36);
        expect(kPart, "vegepygmy: a piercing weapon (spear, ITEMS byte 7 = 1) does 1; a sword full", spear == 1 && sword == 7,
               "%d / %d", spear, sword);
        combat::Harm h;
        h.kind = 4;
        bool dn, res;
        expect(kPart, "vegepygmy: immune to electricity", combat::harm(b, M, 10, h, rng, &dn, &res) == 0);
        bare(P);
    }
    // -- The shambling mound (MON3 #25): both swings hit -> engulfs (held, 2d4
    //    turns of breath); electricity heals it 8; fire none; fire / cold: save, else half
    if (vs(3, 25)) {
        combat::Battle& b = *B;
        put(M, 21, 10, 6);
        b.f[M].rec[0x199] = 100;
        b.f[M].attacks[0] = 2;
        b.f[M].attacks[1] = 0;
        b.f[P].received = 0;
        dice_faces({15, 1, 1, 15, 1, 1, 2, 1});     // two hits of 2d8 = 2; the hold's 2d4 = 3
        combat::Attack at = combat::attack(b, M, P, names, rng);
        dice_usual();
        const int sk = aff_index(b.f[P], mx.suffocate);
        expect(kPart, "shambling mound: both swings hit - engulfs (no moves, 2d4 = 3 turns of breath)",
               has_ev(at, combat::Ev::Engulfs) && aff_index(b.f[P], mx.held_fast) >= 0 && sk >= 0 && b.f[P].aff[sk][3] == 3 &&
               b.f[P].moves == 0 && aff_index(b.f[M], mx.engulfing) >= 0);
        // the victim's turns: a breath less each; none left - suffocates
        int turns = 0;
        while (b.f[P].up() && turns < 6) {
            b.f[P].delay = 3;
            const combat::TurnFx tf = combat::turn_effects(b, P);
            ++turns;
            if (tf == combat::TurnFx::Suffocates) break;
        }
        expect(kPart, "engulfed: suffocates on the 4th turn (data 3, 2, 1, 0)", turns == 4 && b.f[P].status() == party::Dead, "%d turns",
               turns);
        combat::release_holds(b, M);                 // (its hold on the dead one: let go at its turn)
        heal_p();
        // engulf again; the mound's next computer turn lets go (the original's quirk)
        b.f[M].attacks[0] = 2;
        b.f[P].received = 0;
        dice_faces({15, 1, 1, 15, 1, 1, 2, 1});
        combat::attack(b, M, P, names, rng);
        dice_usual();
        combat::release_holds(b, M);
        expect(kPart, "engulf: the mound's next computer turn lets its victim go (quirk)", aff_index(b.f[P], mx.held_fast) < 0 &&
               aff_index(b.f[P], mx.suffocate) < 0 && aff_index(b.f[M], mx.engulfing) < 0);
        heal_p();
        b.f[M].attacks[0] = 2;
        b.f[P].received = 0;
        dice_faces({15, 1, 1, 1});
        at = combat::attack(b, M, P, names, rng);
        dice_usual();
        expect(kPart, "engulf: one swing of two hits - no engulfing", !has_ev(at, combat::Ev::Engulfs));
        combat::Harm h;
        bool dn, res;
        h.kind = 4;
        b.f[M].rec[0x1A4] = 40;
        const int e = combat::harm(b, M, 20, h, rng, &dn, &res);
        expect(kPart, "shambling mound: electricity heals it 8 (not +8 AC: coab differs)", e == 0 && b.f[M].hp() == 48, "hp %d",
               b.f[M].hp());
        h.kind = 1;
        expect(kPart, "shambling mound: fire none", combat::harm(b, M, 20, h, rng, &dn, &res) == 0);
        h.kind = 2;
        h.on_save = 2;
        b.f[M].rec[0xDF + 4] = 11;
        dice_faces({10});
        const int c1 = combat::harm(b, M, 20, h, rng, &dn, &res);
        dice_faces({11});
        const int c2 = combat::harm(b, M, 20, h, rng, &dn, &res);
        dice_usual();
        expect(kPart, "shambling mound: cold - a failed save halves it (10), a made one none", c1 == 10 && c2 == 0, "%d / %d", c1, c2);
    }
    // -- The owl bear (MON5 #55): attack 1 roll 18+ hugs (held fast)
    if (vs(5, 55)) {
        combat::Battle& b = *B;
        put(M, 21, 10, 6);
        combat::Attack at = blow(0, {18, 1});
        expect(kPart, "owl bear: a roll of 18 hugs - no moving", has_ev(at, combat::Ev::Hugs) && aff_index(b.f[P], mx.held_fast) >= 0 &&
               aff_index(b.f[M], mx.hugging) >= 0);
        heal_p();
        *b.f[M].n_aff = 1;                            // (only its hug check left)
        at = blow(0, {17, 1});
        expect(kPart, "owl bear: 17 - no hug", !has_ev(at, combat::Ev::Hugs));
    }
    // -- The troll (MON2 #7): hurt, it regenerates 3 a round after 3 rounds;
    //    down to anything but fire / acid it stands up again 3d6 rounds later, all its HP
    if (vs(2, 7)) {
        combat::Battle& b = *B;
        combat::Fighter& t = b.f[M];
        b.f[P].rec[0x199] = 100;
        b.f[P].rec[0x19E] = 1; b.f[P].rec[0x1A0] = 8; b.f[P].rec[0x1A2] = 0;
        bare(P);
        b.f[P].attacks[0] = 1;
        t.received = 0;
        dice_faces({15, 8});
        combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "troll hurt: 36 - 8 = 28, regenerating in 3 rounds", t.hp() == 28 && aff_index(t, mx.regen_wait) >= 0);
        combat::tick(b);
        combat::tick(b);
        const int h2 = t.hp();
        combat::tick(b);
        const int h3 = t.hp();
        combat::tick(b);
        expect(kPart, "troll: then 3 HP a round", h2 == 28 && h3 == 31 && t.hp() == 34, "%d %d %d", h2, h3, t.hp());
        combat::tick(b);
        expect(kPart, "troll: not above its maximum", t.hp() == 36, "%d", t.hp());
        // down by a blow: it gets up 3d6 rounds later with all its HP
        t.rec[0x1A4] = 5;
        b.f[P].attacks[0] = 1;
        t.received = 0;
        dice_faces({15, 8, 2, 2, 2});                // the fall's 3d6 = 6
        combat::attack(b, P, M, names, rng);
        dice_usual();
        const int up = aff_index(t, mx.troll_up);
        const int mins = up >= 0 ? (t.aff[up][1] | t.aff[up][2] << 8) : -1;
        expect(kPart, "troll down by a blow: getting up in 3d6 (6) rounds", !t.up() && mins == 6, "%d", mins);
        int rounds = 0;
        combat::Event ev[4];
        while (!t.up() && rounds < 30) {
            combat::tick(b, ev, 4);
            ++rounds;
        }
        expect(kPart, "troll: stands up (\"stands up and grins\") 6 rounds later with all 36 HP", rounds == 6 && t.up() && t.hp() == 36,
               "%d rounds, hp %d", rounds, t.hp());
        combat::Harm h;
        h.kind = 1;
        bool dn, res;
        combat::harm(b, M, 100, h, rng, &dn, &res);
        expect(kPart, "troll down by fire: stays down", !t.up() && aff_index(t, mx.troll_up) < 0);
    }
    // -- The hooded medusa (MON4 #39): stone gaze within 12, save vs petrification; a mirror sends it back
    if (vs(4, 39)) {
        combat::Battle& b = *B;
        put(M, 25, 10, 6);
        combat::Event ev[24];
        bool ends = false;
        dice_faces({1, 10});
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "medusa gazes: failed save vs petrification - stoned; it fights on", n > 0 && ev[0].ev == combat::Ev::Gazes && !ends &&
               b.f[P].status() == party::Stoned);
        heal_p();
        dice_faces({1, 11});
        combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        expect(kPart, "medusa: a save made - not stoned", b.f[P].status() == party::Okay);
        // poison (0x40) with its snakes (attack 2)
        heal_p();
        put(M, 21, 10, 6);
        blow(1, {15, 1, 10});
        expect(kPart, "medusa's snakes: poison - failed save, killed", b.f[P].status() == party::Dead);
    }
    // -- The beholder (MON4 #40): rays by range (<= 2 disintegrate, 3 stone, 4 death, 5 wounds 2d8 + 1)
    if (vs(4, 40)) {
        combat::Battle& b = *B;
        struct R { int x; combat::Ev ray; int status; } rays[4] = {
            {22, combat::Ev::RayDisintegrate, party::Gone}, {23, combat::Ev::RayStone, party::Stoned},
            {24, combat::Ev::RayDeath, party::Dead}, {25, combat::Ev::RayWounds, party::Okay}};
        for (const R& r : rays) {
            heal_p();
            put(M, r.x, 10, 6);
            combat::Event ev[24];
            bool ends = false;
            if (r.status == party::Okay) dice_faces({3, 3});     // wounds: 2d8 = 6, + 1 (no save)
            else dice_faces({10});                               // a save of 10 fails
            const int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
            dice_usual();
            bool fired = false;
            for (int j = 0; j < n; ++j) fired = fired || ev[j].ev == r.ray;
            const bool effect = r.status == party::Okay ? b.f[P].hp() == 93 : b.f[P].status() == r.status;
            expect(kPart, "beholder: the ray for that range", fired && effect, "range %d: status %d hp %d", r.x - 20, b.f[P].status(),
                   b.f[P].hp());
        }
    }
}

// A readied missile weapon for P: `launcher` with `ammo` (count 10, plus, name word 3)
void shooter(int launcher, int ammo, int plus = 0, int word3 = 0)
{
    hold(P, launcher);
    party::Character& ch = play::pt->m[P];
    ch.n_items = 2;
    memset(ch.items[1], 0, items::kRecordSize);
    ch.items[1][0x2E] = static_cast<uint8_t>(ammo);
    ch.items[1][0x31] = static_cast<uint8_t>(word3);
    ch.items[1][0x32] = static_cast<uint8_t>(plus);
    ch.items[1][0x34] = 1;
    ch.items[1][0x39] = 10;
    B->f[P].n_items = 2;
}

void specials_more()
{
    using namespace play;
    if (!boot()) return;
    const combat::MonFx mx = d->prof->fight.facts.mon;
    const combat::Facts fxs = d->prof->fight.facts;
    // -- Magic resistance 50% (the dark elves, MON5 #49): d100 <= 50 + (11 - level) x 5;
    //    then their 90% against sleep / charm (0x6B)
    if (vs(5, 49)) {
        combat::Battle& b = *B;
        combat::Harm h;
        h.level = 11;
        dice_faces({50});
        const bool r1 = combat::resists(b, M, mx.sleep, h, rng);
        dice_faces({51, 90});
        const bool r2 = combat::resists(b, M, mx.sleep, h, rng);
        dice_faces({51, 91});
        const bool r3 = combat::resists(b, M, mx.sleep, h, rng);
        h.level = 5;                                   // 50 + 30 = 80
        dice_faces({80});
        const bool r4 = combat::resists(b, M, mx.paralyzed, h, rng);
        dice_faces({81});
        const bool r5 = combat::resists(b, M, mx.paralyzed, h, rng);
        dice_usual();
        expect(kPart, "dark elf: 50% magic resistance by level (11: 50; 5: 80), then 90% against sleep", r1 && r2 && !r3 && r4 && !r5,
               "%d %d %d %d %d", r1, r2, r3, r4, r5);
    }
    // -- The Zhentrim mage (MON3 #21): the minor globe (spells below level 4),
    //    missiles from a +0 weapon dodged (0x29)
    if (vs(3, 21)) {
        combat::Battle& b = *B;
        combat::Harm h;
        h.spell = 0x0F;
        h.spell_level = 3;
        h.kind = 1;
        bool dn, res;
        expect(kPart, "Zhentrim mage: the minor globe stops a 3rd level spell", combat::harm(b, M, 20, h, rng, &dn, &res) == 0);
        b.f[M].rec[0x1A4] = 25;
        h.spell_level = 4;
        expect(kPart, "... not a 4th level one", combat::harm(b, M, 20, h, rng, &dn, &res) == 20);
        b.f[M].rec[0x1A4] = 25;
        put(P, 15, 10, 2);
        shooter(43, ov("arrow", 0), 0);
        b.f[P].rec[0x199] = 100;
        b.f[P].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 3});
        combat::Attack at = combat::attack(b, P, M, names, rng);
        expect(kPart, "Zhentrim mage: a +0 arrow from afar \"Avoids it\"", at.avoided[0]);
        shooter(43, ov("arrow", 0), 1);
        b.f[P].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 3});
        at = combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "Zhentrim mage: a +1 arrow hits", !at.avoided[0] && at.hits[0].hit);
        bare(P);
    }
    // -- The efreeti (MON5 #52): fire -1 a die rolled, never below the dice
    if (vs(5, 52)) {
        combat::Battle& b = *B;
        combat::Harm h;
        h.kind = 1;
        h.dice = 5;
        bool dn, res;
        const int a = combat::harm(b, M, 20, h, rng, &dn, &res);
        const int c = combat::harm(b, M, 6, h, rng, &dn, &res);
        expect(kPart, "efreeti: fire 20 (5 dice) -> 15, 6 -> 5", a == 15 && c == 5, "%d %d", a, c);
    }
    // -- The high priest (MON4 #72): cold half and +3 on its saves against it;
    //    protection from good: +2 on its saves when a good one acts
    if (vs(4, 72)) {
        combat::Battle& b = *B;
        combat::Fighter& m = b.f[M];
        for (int k = 0; k < 5; ++k) m.rec[0xDF + k] = 15;
        m.rec[0x186] = 0;
        combat::set_actor(b, M);
        combat::Harm h;
        h.kind = 2;
        h.save = 4;
        h.on_save = 2;
        bool dn, res;
        dice_faces({12});
        const int a = combat::harm(b, M, 20, h, rng, &dn, &res);
        dice_faces({11});
        m.rec[0x1A4] = 60;
        const int c = combat::harm(b, M, 20, h, rng, &dn, &res);
        dice_usual();
        expect(kPart, "high priest: cold halved (10), a save at +3 (12 + 3) halves again (5); 11 + 3 fails (10)", a == 5 && c == 10,
               "%d %d", a, c);
        b.f[P].rec[0x11B] = 0;                          // lawful good
        combat::set_actor(b, P);
        dice_faces({13});
        const bool g = combat::saving_throw(m, 0, 0, rng);
        b.f[P].rec[0x11B] = 4;                          // true neutral
        dice_faces({13});
        const bool n = combat::saving_throw(m, 0, 0, rng);
        dice_usual();
        expect(kPart, "high priest: protection from good - +2 on its saves when the one acting is good", g && !n);
    }
    // -- The Zhentrim cleric (MON3 #19): fire half, +3 on its saves against it
    if (vs(3, 19)) {
        combat::Battle& b = *B;
        combat::Harm h;
        h.kind = 1;
        bool dn, res;
        expect(kPart, "Zhentrim cleric: fire halved", combat::harm(b, M, 20, h, rng, &dn, &res) == 10);
    }
    // -- Dragonbait (MON3 #23): protection from evil for those next to it (+2 on saves when an evil one acts)
    if (vs(3, 23)) {
        combat::Battle& b = *B;
        put(M, 21, 10, 6);
        b.f[M].rec[0x11B] = 8;                          // (an evil one acting - for the test)
        combat::set_actor(b, M);
        dice_faces({9});
        const bool near = combat::saving_throw(b.f[P], 0, 0, rng);
        put(P, 15, 10, 2);
        dice_faces({9});
        const bool far = combat::saving_throw(b.f[P], 0, 0, rng);
        dice_usual();
        expect(kPart, "Dragonbait: those next to it +2 on saves when an evil one acts (9 + 2 >= 11)", near && !far);
    }
    // -- The margoyle (MON6 #69): +0 weapons nothing for characters; a monster of 4+ Hit Dice hurts it bare
    if (vs(6, 69, 2)) {
        combat::Battle& b = *B;
        b.f[P].rec[0x199] = 100;
        hold(P, 36, 0);
        b.f[P].rec[0x19E] = 1; b.f[P].rec[0x1A0] = 8; b.f[P].rec[0x1A2] = 0;
        b.f[P].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 5});
        combat::Attack at = combat::attack(b, P, M, names, rng);
        expect(kPart, "margoyle: a character's +0 sword does nothing", at.hits[0].hit && at.hits[0].damage == 0);
        put(M + 1, 22, 10, 6);
        put(P, 15, 10, 2);
        b.f[M + 1].rec[0x197] = 0;                      // (one of them against the other)
        b.f[M + 1].rec[0x199] = 100;
        b.f[M + 1].attacks[0] = 1;
        b.f[M + 1].attacks[1] = 0;
        b.f[M].received = 0;
        dice_faces({15, 5});
        at = combat::attack(b, M + 1, M, names, rng);
        const int hd6 = at.hits[0].damage;
        b.f[M + 1].rec[0xE5] = 3;
        b.f[M + 1].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 5});
        at = combat::attack(b, M + 1, M, names, rng);
        dice_usual();
        expect(kPart, "margoyle: a monster of 6 Hit Dice hurts it bare (5); of 3 not", hd6 == 5 && at.hits[0].damage == 0, "%d / %d", hd6,
               at.hits[0].damage);
        bare(P);
    }
    // -- A blessed quarrel slays the rakshasa (0x82)
    if (vs(4, 67)) {
        combat::Battle& b = *B;
        put(P, 15, 10, 2);
        shooter(46, ov("quarrel", 0), 0, mx.blessed_word);
        b.f[P].rec[0x199] = 100;
        b.f[P].attacks[0] = 1;
        b.f[M].received = 0;
        dice_faces({15, 1});
        combat::Attack at = combat::attack(b, P, M, names, rng);
        dice_usual();
        expect(kPart, "rakshasa: a blessed quarrel slays it (gone)", at.down && b.f[M].status() == party::Gone);
        bare(P);
    }
    // -- The medusa's gaze back at it from a readied mirror (0x7F + name word "Mirror")
    if (vs(4, 39)) {
        combat::Battle& b = *B;
        put(M, 25, 10, 6);
        hold(P, 36);
        pt->m[P].items[0][0x2F] = mx.mirror_word;
        b.f[M].rec[0xE0] = 15;
        combat::Event ev[24];
        bool ends = false;
        dice_faces({1, 10});
        int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        dice_usual();
        bool reflects = false;
        for (int j = 0; j < n; ++j) reflects = reflects || ev[j].ev == combat::Ev::Reflects;
        expect(kPart, "medusa: a mirror reflects the gaze - the medusa stoned, not the one with it", reflects &&
               b.f[M].status() == party::Stoned && b.f[P].status() == party::Okay);
        bare(P);
    }
    // -- A troll down to acid stays down
    if (vs(2, 7)) {
        combat::Battle& b = *B;
        combat::Harm h;
        h.kind = 0x10;
        bool dn, res;
        combat::harm(b, M, 100, h, rng, &dn, &res);
        expect(kPart, "troll down by acid: stays down", !b.f[M].up() && aff_index(b.f[M], mx.troll_up) < 0);
    }
    // -- The beholder past 5 squares: Fear, Slow, Sleep (each once)
    if (vs(4, 40)) {
        combat::Battle& b = *B;
        put(M, 27, 10, 6);
        combat::Event ev[24];
        bool ends = false;
        const int n = combat::special(b, fg->t, M, rng, ev, 24, &ends);
        int cast[3] = {}, k = 0;
        for (int j = 0; j < n; ++j)
            if (ev[j].ev == combat::Ev::Cast && k < 3) cast[k++] = ev[j].amount;
        expect(kPart, "beholder past 5 squares: casts Fear, Slow, Sleep (0x54, 0x37, 0x15)", k == 3 && cast[0] == 0x54 &&
               cast[1] == 0x37 && cast[2] == 0x15, "%d: %02X %02X %02X", k, cast[0], cast[1], cast[2]);
        combat::Harm h;
        h.kind = 8;
        bool dn, res;
        expect(kPart, "beholder: magic none (0x81)", combat::harm(b, M, 20, h, rng, &dn, &res) == 0);
    }
    (void)fxs;
}

// ---- D: the computer's turn through the Play Test (play::ai_step): weapons, missiles
void computer_turns()
{
    using namespace play;
    // Red Plume (MON1 #16): broad sword readied, a long bow not, arrows readied.
    // The original's rating (coab AI_items_selection): the bow 1d6 + 2 x 3
    // attacks - 1 = 12 against the sword 2d4 + 3 one-handed = 11: the bow
    // when nobody is next to it (12 > 11 / 2)
    if (!vs(1, 16)) return;
    combat::Battle& b = *B;
    put(M, 30, 10, 6);
    put(P, 20, 10, 2);
    combat::start_round(b, rng);
    b.f[M].delay = 10;
    fg->cur = M;
    fg->ai_checked = false;
    fg->fx_done = true;
    fg->pages = fg->at = 0;
    int arrows_before = -1;
    for (int k = 0; k < b.f[M].n_items; ++k)
        if (b.f[M].items[k][0x2E] == ov("arrow", 0)) arrows_before = b.f[M].items[k][0x39];
    dice_faces({20, 20, 20, 20, 20, 20, 20, 20, 20, 20});
    for (int step = 0; step < 6 && fg && fg->cur == M; ++step) {
        ai_step(C);
        while (fg && fg->st == FSt::Pages && fg->cur == M) {
            g_now += 5000;
            play::tick(g_now, C);
            play::tap(10, 10, C);
        }
        if (fg && fg->st == FSt::Ai && fg->cur == M) continue;
        break;
    }
    dice_usual();
    const int w = combat::weapon(b.f[M], *names);
    int arrows_after = -1;
    for (int k = 0; k < b.f[M].n_items; ++k)
        if (b.f[M].items[k][0x2E] == ov("arrow", 0)) arrows_after = b.f[M].items[k][0x39];
    expect(kPart, "Red Plume (a monster) readies its long bow at range (the computer's weapon choice)", w >= 0 &&
           b.f[M].items[w][0x2E] == 43, "weapon type %d", w >= 0 ? b.f[M].items[w][0x2E] : -1);
    expect(kPart, "... its to-hit / dice worked out for the bow (1d6)", b.f[M].rec[0x19E] == 1 && b.f[M].rec[0x1A0] == 6, "%dd%d",
           b.f[M].rec[0x19E], b.f[M].rec[0x1A0]);
    expect(kPart, "... and shoots twice a round (bow: 4 half attacks), an arrow each", arrows_after == arrows_before - 2, "%d -> %d", arrows_before,
           arrows_after);
    // next to an enemy: the sword again
    if (!vs(1, 16)) return;
    combat::Battle& b2 = *B;
    put(M, 21, 10, 6);
    combat::start_round(b2, rng);
    b2.f[M].delay = 10;
    fg->cur = M;
    fg->ai_checked = false;
    fg->fx_done = true;
    dice_faces({1, 1, 1, 1});
    ai_step(C);
    dice_usual();
    const int w2 = combat::weapon(b2.f[M], *names);
    expect(kPart, "Red Plume next to an enemy: the broad sword (2d4)", w2 >= 0 && b2.f[M].items[w2][0x2E] == 35 &&
           b2.f[M].rec[0x19E] == 2 && b2.f[M].rec[0x1A0] == 4, "type %d", w2 >= 0 ? b2.f[M].items[w2][0x2E] : -1);
    // A whole fight against each armed kind plays to its end
    const int fights[][2] = {{1, 16}, {1, 84}, {1, 1}, {4, 38}, {5, 49}, {3, 21}};
    for (const auto& fgt : fights) {
        if (!boot() || !fight(fgt[0], fgt[1], 3)) {
            expect(kPart, "a whole fight", false, "MON%d #%d", fgt[0], fgt[1]);
            continue;
        }
        god = true;
        const bool over = pump(600000);
        god = false;
        expect(kPart, "a whole fight plays to its end", over || !fg, "MON%d #%d: round %d", fgt[0], fgt[1], fg ? fg->b.round : -1);
    }
}

void dump_items()
{
    if (!boot()) return;
    for (int t = 0; t < 128; ++t) {
        const items::TypeInfo& ti = play::names->type(t);
        if (ti.slot != 0) continue;
        printf("type %3d range %2d attacks %d flags %02X dice %dd%d%+d large %dd%d%+d hands %d blow %02X classes %02X\n", t, ti.range,
               ti.attacks, ti.flags, ti.dice, ti.sides, ti.bonus, ti.dice_large, ti.sides_large, ti.bonus_large, ti.hands, ti.blow,
               ti.classes);
    }
}

} // namespace bm

static void behave_monsters()
{
    if (!bm::load_oracle()) {
        expect(bm::kPart, "oracle file", false, "no oracle_monsters.txt");
        return;
    }
    if (getenv("DUMP_ITEMS")) {
        bm::dump_items();
        return;
    }
    if (!getenv("SKIP_A")) bm::every_monster();
    if (!getenv("SKIP_B")) bm::rules_checks();
    if (!getenv("SKIP_B")) bm::rules_more();
    if (!getenv("SKIP_C")) bm::specials();
    if (!getenv("SKIP_C")) bm::specials_more();
    if (!getenv("SKIP_D")) bm::computer_turns();
}

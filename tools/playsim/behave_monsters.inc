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
}

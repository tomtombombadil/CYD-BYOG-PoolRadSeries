// Behaviour checks: events - the scripts, encounters and triggers (included
// by sweep.cpp with -DBEHAVE_EVENTS). What each script instruction does and
// where / when the scripts' events fire, against the original (coab's
// facts: ovr003 the instructions, ovr008 their helpers, ovr015 / ovr021 the
// step, time and rest; claude/behaviour_facts.md: events). Small synthetic
// scripts run through the engine's own machine; the triggers of the
// player's own ECL blocks are checked square by square against
// expectations worked out from the scripts by a separate reading of them
// (ORACLE_DIR/oracle_events.txt, made in scratch space - never in the repo).

#include <functional>
#include <map>
#include <set>

// ---- a tiny assembler for synthetic scripts
struct BAsm {
    std::vector<uint8_t> c;
    BAsm()
    {
        for (int i = 0; i < 5; ++i) {           // the 5 entry points: a filler byte and a word
            c.push_back(0);
            c.push_back(2);
            c.push_back(0);
            c.push_back(0);
        }
    }
    uint16_t here() const { return static_cast<uint16_t>(0x8000 + c.size()); }
    void entry(int i, uint16_t a)
    {
        c[i * 4 + 2] = static_cast<uint8_t>(a);
        c[i * 4 + 3] = static_cast<uint8_t>(a >> 8);
    }
    BAsm& op(uint8_t o)
    {
        c.push_back(o);
        return *this;
    }
    BAsm& b(uint8_t v)                          // a byte, the value itself
    {
        c.push_back(0);
        c.push_back(v);
        return *this;
    }
    BAsm& w(uint16_t v)                         // a word, the value itself
    {
        c.push_back(2);
        c.push_back(static_cast<uint8_t>(v));
        c.push_back(static_cast<uint8_t>(v >> 8));
        return *this;
    }
    BAsm& m(uint16_t a)                         // memory at a
    {
        c.push_back(1);
        c.push_back(static_cast<uint8_t>(a));
        c.push_back(static_cast<uint8_t>(a >> 8));
        return *this;
    }
    BAsm& s(const char* t)                      // a packed string (6 bits a character)
    {
        std::vector<int> v;
        for (const char* p = t; *p; ++p) v.push_back((*p >= 0x40 ? *p - 0x40 : *p) & 0x3F);
        std::vector<uint8_t> out;
        for (size_t i = 0; i < v.size(); i += 4) {
            const int a = v[i], bb = i + 1 < v.size() ? v[i + 1] : 0, cc = i + 2 < v.size() ? v[i + 2] : 0,
                      dd = i + 3 < v.size() ? v[i + 3] : 0;
            const size_t left = v.size() - i;
            out.push_back(static_cast<uint8_t>(a << 2 | bb >> 4));
            if (left >= 2) out.push_back(static_cast<uint8_t>((bb & 0xF) << 4 | cc >> 2));
            if (left >= 3) out.push_back(static_cast<uint8_t>((cc & 3) << 6 | dd));
        }
        c.push_back(0x80);
        c.push_back(static_cast<uint8_t>(out.size()));
        c.insert(c.end(), out.begin(), out.end());
        return *this;
    }
    BAsm& sm(uint16_t a)                        // a string in memory at a
    {
        c.push_back(0x81);
        c.push_back(static_cast<uint8_t>(a));
        c.push_back(static_cast<uint8_t>(a >> 8));
        return *this;
    }
    // Patches a word operand written with w() at code offset `at` (its code byte)
    void patch(size_t at, uint16_t v)
    {
        c[at + 1] = static_cast<uint8_t>(v);
        c[at + 2] = static_cast<uint8_t>(v >> 8);
    }
};

enum : uint8_t {
    kExit = 0, kGoto, kGosub, kCompare, kAdd, kSub, kDiv, kMul, kRandom, kSave, kLoadChar, kLoadMon, kSetupMon,
    kApproach, kPicture, kInputNum, kInputStr, kPrint, kPrintClear, kReturn, kCmpAnd, kVMenu, kIfEq, kIfNe, kIfLt,
    kIfGt, kIfLe, kIfGe, kClearMon, kPartyStr, kCheckParty, kOp1F, kNewEcl, kLoadFiles, kPartySurprise, kSurprise,
    kCombat, kOnGoto, kOnGosub, kTreasure, kRob, kEncMenu, kGetTable, kHMenu, kParlay, kCall, kDamage, kAnd, kOr,
    kSpriteOff, kFindItem, kPrintRet, kClock, kSaveTable, kAddNpc, kLoadPieces, kProgram, kWho, kDelay, kSpell,
    kProtection, kClearBox, kDump, kFindSpecial, kDestroyItems
};

// Loads the code as the running block and runs from `at` (the machine
// alone, no front end); per-script state as after a NEWECL
static ecl::Stop b_run(const BAsm& a, uint16_t at)
{
    using namespace play;
    ecl::GameState& g = d->gs;
    memset(g.code, 0, sizeof g.code);
    memcpy(g.code, a.c.data(), a.c.size());
    g.code_len = static_cast<uint32_t>(a.c.size());
    vm->init_script(true);
    return vm->run(at);
}

static uint16_t b_get(uint16_t a) { return play::vm->get(a); }

// The number of sides each die asked for (the hook: as usual after)
static std::vector<int> b_asked;
static int b_ask_hook(int n)
{
    b_asked.push_back(n);
    return b_die(n);
}

// ---- reading the oracle file: "key value value ..." lines
static std::map<std::string, std::vector<std::string>> b_oracle;
static bool b_load_oracle()
{
    const char* dir = getenv("ORACLE_DIR");
    std::string path = std::string(dir ? dir : "/tmp/claude-0/bt") + "/oracle_events.txt";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return false;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        std::vector<std::string> parts;
        char* save = nullptr;
        for (char* t = strtok_r(line, " \t\r\n", &save); t; t = strtok_r(nullptr, " \t\r\n", &save)) parts.push_back(t);
        if (parts.size() < 2) continue;
        const std::string key = parts[0];
        parts.erase(parts.begin());
        b_oracle[key] = parts;
    }
    fclose(f);
    return !b_oracle.empty();
}
static int b_oi(const char* key, int i, int def = -9999)
{
    auto it = b_oracle.find(key);
    if (it == b_oracle.end() || i >= static_cast<int>(it->second.size())) return def;
    return static_cast<int>(strtol(it->second[i].c_str(), nullptr, 0));
}

// ============================================================================
// A: the instructions, one by one (coab ovr003 / ovr008)
// ============================================================================
static void b_ops()
{
    using namespace play;
    const char* P = "events";
    // ---- COMPARE: the flags are op1 against op2 (compare_variables(b, a))
    {
        const uint8_t ifs[6] = {kIfEq, kIfNe, kIfLt, kIfGt, kIfLe, kIfGe};
        const int pairs[3][2] = {{3, 5}, {5, 5}, {5, 3}};
        int bad = 0;
        char why[200] = "";
        for (auto& pr : pairs)
            for (int k = 0; k < 6; ++k) {
                BAsm a;
                a.entry(0, a.here());
                a.op(kSave).b(0).m(0x4C00);
                a.op(kCompare).b(static_cast<uint8_t>(pr[0])).b(static_cast<uint8_t>(pr[1]));
                a.op(ifs[k]);
                a.op(kSave).b(1).m(0x4C00);
                a.op(kExit);
                b_run(a, 0x8000 + 20);
                const int x = pr[0], y = pr[1];
                const bool want = k == 0 ? x == y : k == 1 ? x != y : k == 2 ? x < y : k == 3 ? x > y : k == 4 ? x <= y : x >= y;
                if ((b_get(0x4C00) == 1) != want) {
                    ++bad;
                    snprintf(why + strlen(why), sizeof why - strlen(why), " %d,%d IF#%d", x, y, k);
                }
            }
        expect(P, "COMPARE + the six IFs: op1 against op2 (18 cases)", bad == 0, "%d wrong:%s", bad, why);
    }
    // ---- COMPARE strings (compare_strings: op1's string against op2's)
    {
        int bad = 0;
        const char* pairs[3][2] = {{"ABC", "ABD"}, {"ABC", "ABC"}, {"ABD", "ABC"}};
        for (auto& pr : pairs) {
            BAsm a;
            a.op(kSave).b(0).m(0x4C00);
            a.op(kCompare).s(pr[0]).s(pr[1]);
            a.op(kIfLt);
            a.op(kSave).b(1).m(0x4C00);
            a.op(kExit);
            b_run(a, 0x8000 + 20);
            const bool want = strcmp(pr[0], pr[1]) < 0;
            if ((b_get(0x4C00) == 1) != want) ++bad;
        }
        expect(P, "COMPARE two strings: IF< when op1's comes first", bad == 0, "%d wrong", bad);
    }
    // ---- ADD / SUBTRACT (op2 - op1) / DIVIDE (op1 / op2, remainder 0x7F3F) / MULTIPLY, 16-bit
    {
        BAsm a;
        a.op(kAdd).w(0xFFF0).w(0x0020).m(0x4C00);
        a.op(kSub).b(3).b(10).m(0x4C01);
        a.op(kSub).b(10).b(3).m(0x4C02);
        a.op(kDiv).b(17).b(5).m(0x4C03);
        a.op(kMul).w(300).w(300).m(0x4C04);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "ADD wraps at 16 bits", b_get(0x4C00) == 0x0010, "%04X", b_get(0x4C00));
        expect(P, "SUBTRACT is op2 - op1", b_get(0x4C01) == 7 && b_get(0x4C02) == static_cast<uint16_t>(3 - 10), "%d %d",
               b_get(0x4C01), b_get(0x4C02));
        expect(P, "DIVIDE op1 / op2, the remainder in 0x7F3F", b_get(0x4C03) == 3 && b_get(0x7F3F) == 2, "%d r %d", b_get(0x4C03),
               b_get(0x7F3F));
        expect(P, "MULTIPLY wraps at 16 bits", b_get(0x4C04) == static_cast<uint16_t>(90000), "%u", b_get(0x4C04));
    }
    // ---- RANDOM n: 0 .. n (a die of n + 1; 0xFF stays 255: 0 .. 254)
    {
        int bad = 0;
        for (int mx : {0, 1, 9, 100, 254, 255}) {
            dice_usual();
            b_asked.clear();
            create::g_die_hook = b_ask_hook;
            b_dice_mode = 1;                     // the top face
            BAsm a;
            a.op(kRandom).b(static_cast<uint8_t>(mx)).m(0x4C00);
            a.op(kExit);
            b_run(a, 0x8000 + 20);
            const int sides = mx < 255 ? mx + 1 : 255;
            if (b_asked.size() != 1 || b_asked[0] != sides || b_get(0x4C00) != sides - 1) {
                ++bad;
                printf("   RANDOM %d: asked %d, got %d\n", mx, b_asked.empty() ? -1 : b_asked[0], b_get(0x4C00));
            }
        }
        dice_usual();
        expect(P, "RANDOM n: one die of n + 1 sides (255: 255), the value 0 .. sides - 1", bad == 0, "%d wrong", bad);
    }
    // ---- SAVE a string: a character a word, 0 at its end
    {
        BAsm a;
        for (int k = 0; k < 6; ++k) a.op(kSave).b(0x55).m(static_cast<uint16_t>(0x4C10 + k));
        a.op(kSave).s("AB").m(0x4C10);
        a.op(kCompare).sm(0x4C10).s("AB");
        a.op(kIfEq);
        a.op(kSave).b(1).m(0x4C00);
        a.op(kExit);
        BAsm pre;
        b_run(pre, 0);
        vm->set(0x4C00, 0);
        b_run(a, 0x8000 + 20);
        expect(P, "SAVE a string: a character a word, then 0; read back the same",
               b_get(0x4C10) == 'A' && b_get(0x4C11) == 'B' && b_get(0x4C12) == 0 && b_get(0x4C13) == 0x55 && b_get(0x4C00) == 1,
               "%02X %02X %02X %02X eq %d", b_get(0x4C10), b_get(0x4C11), b_get(0x4C12), b_get(0x4C13), b_get(0x4C00));
    }
    // ---- GOSUB / RETURN; ON GOTO / ON GOSUB (index past the list: on after it)
    {
        BAsm a;
        a.op(kGosub).w(0);
        const size_t fix1 = a.c.size() - 3;
        a.op(kSave).b(2).m(0x4C01);                 // after the RETURN
        a.op(kExit);
        a.patch(fix1, a.here());
        a.op(kSave).b(1).m(0x4C00);
        a.op(kReturn);
        b_run(a, 0x8000 + 20);
        expect(P, "GOSUB / RETURN come back after the GOSUB", b_get(0x4C00) == 1 && b_get(0x4C01) == 2, "%d %d", b_get(0x4C00),
               b_get(0x4C01));
        int bad = 0;
        for (int idx = 0; idx < 4; ++idx)
            for (int gs = 0; gs < 2; ++gs) {
                BAsm o;
                o.op(kSave).b(0).m(0x4C00);
                o.op(kSave).b(0).m(0x4C01);
                o.op(gs ? kOnGosub : kOnGoto).b(static_cast<uint8_t>(idx)).b(3).w(0).w(0).w(0);
                const size_t t0 = o.c.size() - 9;
                o.op(kSave).b(9).m(0x4C01);             // after the list
                o.op(kExit);
                for (int k = 0; k < 3; ++k) {
                    o.patch(t0 + 3 * k, o.here());
                    o.op(kSave).b(static_cast<uint8_t>(10 + k)).m(0x4C00);
                    o.op(gs ? kReturn : kExit);
                }
                b_run(o, 0x8000 + 20);
                const int want0 = idx < 3 ? 10 + idx : 0, want1 = idx < 3 ? (gs ? 9 : 0) : 9;
                if (b_get(0x4C00) != want0 || b_get(0x4C01) != want1) ++bad;
            }
        expect(P, "ON GOTO / ON GOSUB: the index's target; past the list nothing (8 cases)", bad == 0, "%d wrong", bad);
    }
    // ---- COMPARE AND: = when both pairs are equal, else <>; the other flags off
    {
        int bad = 0;
        for (int k = 0; k < 4; ++k) {
            BAsm a;
            a.op(kCompare).b(1).b(5);                   // sets < and <=
            a.op(kSave).b(0).m(0x4C00);
            a.op(kSave).b(0).m(0x4C01);
            a.op(kCmpAnd).b(1).b(static_cast<uint8_t>(k & 1 ? 2 : 1)).b(3).b(static_cast<uint8_t>(k & 2 ? 4 : 3));
            a.op(kIfEq).op(kSave).b(1).m(0x4C00);
            a.op(kIfLt).op(kSave).b(1).m(0x4C01);
            a.op(kExit);
            b_run(a, 0x8000 + 20);
            if ((b_get(0x4C00) == 1) != (k == 0) || b_get(0x4C01) != 0) ++bad;
        }
        expect(P, "COMPARE AND: = only when both pairs match; < cleared", bad == 0, "%d wrong", bad);
    }
    // ---- AND / OR: the result a byte; the flags as 0 against it (IF= when 0)
    {
        BAsm a;
        a.op(kAnd).w(0x1F0).w(0x10F).m(0x4C00);
        a.op(kIfEq).op(kSave).b(1).m(0x4C02);
        a.op(kOr).w(0x100).w(0x200).m(0x4C01);
        a.op(kIfEq).op(kSave).b(1).m(0x4C03);
        a.op(kOr).b(0x40).b(0x01).m(0x4C04);
        a.op(kIfLt).op(kSave).b(1).m(0x4C05);
        a.op(kExit);
        BAsm pre;
        b_run(pre, 0);
        for (uint16_t x = 0x4C00; x <= 0x4C05; ++x) vm->set(x, 0);
        b_run(a, 0x8000 + 20);
        expect(P, "AND / OR keep the low byte; IF= when it is 0, IF< (0 < it) when not",
               b_get(0x4C00) == 0x00 && b_get(0x4C02) == 1 && b_get(0x4C01) == 0 && b_get(0x4C03) == 1 && b_get(0x4C04) == 0x41 &&
                   b_get(0x4C05) == 1,
               "%X %d %X %d %X %d", b_get(0x4C00), b_get(0x4C02), b_get(0x4C01), b_get(0x4C03), b_get(0x4C04), b_get(0x4C05));
    }
    // ---- GETTABLE (op1's address + op2) / SAVE TABLE (op2's address + op3 <- op1)
    {
        BAsm a;
        a.op(kSaveTable).b(77).m(0x4C20).b(5);
        a.op(kGetTable).m(0x4C20).b(5).m(0x4C00);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "SAVE TABLE / GETTABLE: base address + index", b_get(0x4C25) == 77 && b_get(0x4C00) == 77, "%d %d", b_get(0x4C25),
               b_get(0x4C00));
    }
    // ---- ECL CLOCK amount, slot: the clock's carries (10 10 6 24 30 12 256)
    {
        BAsm a;
        for (int k = 0; k < 7; ++k) a.op(kSave).b(0).m(static_cast<uint16_t>(0x4BC6 + k));
        a.op(kSave).b(9).m(0x4BC7);          // 9 minutes
        a.op(kSave).b(5).m(0x4BC8);          // 50
        a.op(kSave).b(23).m(0x4BC9);         // 23 h
        a.op(kSave).b(29).m(0x4BCA);         // day 29
        a.op(kSave).b(11).m(0x4BCB);         // month 11
        a.op(kClock).b(1).b(1);              // + 1 minute: everything rolls over
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "ECL CLOCK 1 minute rolls minutes, tens, hours, days, months into the year",
               b_get(0x4BC7) == 0 && b_get(0x4BC8) == 0 && b_get(0x4BC9) == 0 && b_get(0x4BCA) == 0 && b_get(0x4BCB) == 0 &&
                   b_get(0x4BCC) == 1,
               "%d %d %d %d %d %d", b_get(0x4BC7), b_get(0x4BC8), b_get(0x4BC9), b_get(0x4BCA), b_get(0x4BCB), b_get(0x4BCC));
        BAsm b2;
        for (int k = 0; k < 7; ++k) b2.op(kSave).b(0).m(static_cast<uint16_t>(0x4BC6 + k));
        b2.op(kClock).b(30).b(3);            // 30 hours
        b2.op(kExit);
        b_run(b2, 0x8000 + 20);
        vm->take_minutes();
        expect(P, "ECL CLOCK 30 hours: a day and 6 hours", b_get(0x4BC9) == 6 && b_get(0x4BCA) == 1, "%d h %d d", b_get(0x4BC9),
               b_get(0x4BCA));
    }
    // ---- LOAD CHARACTER: index 0 = the first; past the party: not found
    // (0x7D00 reads 0 once); EXIT puts the step's member back
    {
        const int n = pt->count;
        pt->selected = 2;
        BAsm a;
        a.op(kLoadChar).b(0);
        a.op(kSave).m(0x7D00).m(0x4C00);
        a.op(kSave).m(0x7EB4).m(0x4C01);        // 0x7C00 + 0x2B4: the selected one's place
        a.op(kLoadChar).b(static_cast<uint8_t>(n));
        a.op(kSave).m(0x7D00).m(0x4C02);
        a.op(kSave).m(0x7D00).m(0x4C03);
        a.op(kSave).m(0x7EB4).m(0x4C04);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        const int st0 = pt->m[0].in_combat() ? 1 : 0x80;
        expect(P, "LOAD CHARACTER 0 selects the first member", b_get(0x4C01) == 0 && b_get(0x4C00) == st0, "place %d status %X",
               b_get(0x4C01), b_get(0x4C00));
        expect(P, "LOAD CHARACTER past the party: not found - 0x7D00 reads 0 once, the member stays",
               b_get(0x4C02) == 0 && b_get(0x4C03) != 0 && b_get(0x4C04) == 0, "%X %X place %d", b_get(0x4C02), b_get(0x4C03),
               b_get(0x4C04));
        expect(P, "EXIT puts the member selected before the script back", pt->selected == 2, "selected %d", pt->selected);
        pt->selected = 2;
        BAsm r;
        r.op(kLoadChar).b(1);
        r.op(kReturn);                              // nothing to return to: an EXIT
        b_run(r, 0x8000 + 20);
        expect(P, "RETURN with nothing to return to ends the script as EXIT does (the member put back)", pt->selected == 2,
               "selected %d", pt->selected);
    }
    // ---- the character's fields the scripts read / write (get_player_values /
    // alter_character; offsets in the record from the original's listing)
    {
        const party::Character& c = pt->m[1];
        pt->selected = 1;
        struct F {
            uint16_t off;
            int rec;        // record offset (-1: special)
            bool word;
        };
        // byte fields
        const F fs[] = {{0x15, 0x13, false}, {0x18, 0x19, false}, {0x72, 0x74, false}, {0x73, 0x75, false}, {0x9B, 0xE0, false},
                        {0xA0, 0xE5, false}, {0xB8, 0xF7, false}, {0xBB, 0xFB, true},   {0xBD, 0xFD, true},   {0xBF, 0xFF, true},
                        {0xC1, 0x101, true}, {0xC3, 0x103, true}, {0xD6, 0x119, false}, {0xD8, 0x11B, false}, {0xF7, 0x13C, true},
                        {0xF9, 0x13E, false}, {0x11B, 0x1A5, false}};
        int bad = 0;
        for (const F& f : fs) {
            const int want = f.word ? (c.rec[f.rec] | c.rec[f.rec + 1] << 8) : c.rec[f.rec];
            if (b_get(static_cast<uint16_t>(0x7C00 + f.off)) != want) {
                ++bad;
                printf("   7C%02X: %d, record %03X %d\n", f.off, b_get(static_cast<uint16_t>(0x7C00 + f.off)), f.rec, want);
            }
        }
        for (int k = 0; k < 8; ++k)
            if (b_get(static_cast<uint16_t>(0x7CA5 + k)) != c.rec[0xE9 + k + 1]) ++bad;   // thief skills: field_E9 + (off - 0xA4)
        if (b_get(0x7CE4) != (c.rec[0x192] & 1)) ++bad;
        expect(P, "the selected character's fields read at 0x7C00 + off (17 fields, 8 thief skills, 0x7CE4)", bad == 0, "%d wrong",
               bad);
        // Charisma's percentage (0x7ECF): 3 0, 4 5, 5 10, 6 15, 7 20, 8-12 25, 13 30, 14 35, 15 40, 16 50, 17 55, 18-25 60
        party::Character keep = pt->m[1];
        bad = 0;
        for (int cha = 3; cha <= 25; ++cha) {
            pt->m[1].rec[0x1B] = static_cast<uint8_t>(cha);
            const int want = cha <= 7 ? (cha - 3) * 5 : cha <= 12 ? 25 : cha <= 15 ? 30 + (cha - 13) * 5 : cha == 16 ? 50 : cha == 17 ? 55 : 60;
            if (b_get(0x7ECF) != want) ++bad;
        }
        pt->m[1] = keep;
        expect(P, "0x7ECF: the charisma percentage, 3 .. 25", bad == 0, "%d wrong", bad);
        // The magic-user level: + the old one only for a human dual-class past it
        pt->m[1].rec[0x74] = 7;
        memset(&pt->m[1].rec[0x109], 0, 16);
        pt->m[1].rec[0x10B] = 6;                // fighter 6 now
        pt->m[1].rec[0x116] = 5;                // was a magic-user 5
        pt->m[1].rec[0x10E] = 0;
        pt->m[1].rec[0xE6] = 5;
        const int mu_past = b_get(0x7CC9);
        pt->m[1].rec[0xE6] = 6;
        const int mu_not = b_get(0x7CC9);
        pt->m[1] = keep;
        expect(P, "0x7CC9 magic-user level: the old one counts once the new class is past it", mu_past == 5 && mu_not == 0,
               "past %d, not past %d", mu_past, mu_not);
        // Writes: money words, control (> 0xB2: - 0x32), spell slots 0x7C20-0x7C70 = record off - 1
        BAsm a;
        a.op(kSave).w(1234).m(0x7CC1);
        a.op(kSave).b(0xC0).m(0x7CB8);
        a.op(kSave).b(0x21).m(0x7C25);
        a.op(kSave).b(0x87).m(0x7D00);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        const party::Character& cc = pt->m[1];
        expect(P, "writes: gold word, control - 0x32 over 0xB2, spell slot = record off - 1, 0x87 to 0x7D00 stones",
               (cc.rec[0x101] | cc.rec[0x102] << 8) == 1234 && cc.rec[0xF7] == 0xC0 - 0x32 && cc.rec[0x24] == 0x21 && cc.rec[0x196] == 0 &&
                   cc.rec[0x195] == party::Stoned,
               "gold %d control %X slot %X combat %d health %d", cc.rec[0x101] | cc.rec[0x102] << 8, cc.rec[0xF7], cc.rec[0x24],
               cc.rec[0x196], cc.rec[0x195]);
        pt->m[1] = keep;
    }
    // ---- PARTYSTRENGTH: per member (hp + 5 (AC byte - 60) + 5 (hit byte - 39) + 8 MU + 4 cleric) / 10, a byte sum
    {
        int want = 0;
        for (int i = 0; i < pt->count; ++i) {
            const uint8_t* r = pt->m[i].rec;
            const int ac = r[0x19A] > 60 ? r[0x19A] - 60 : 0, hb = r[0x199] > 39 ? r[0x199] - 39 : 0;
            want = (want + (r[0x1A4] + 5 * ac + 5 * hb + 8 * r[0x10E] + 4 * r[0x109]) / 10) & 0xFF;   // (sample party: no dual-classes)
        }
        BAsm a;
        a.op(kPartyStr).m(0x4C00);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "PARTYSTRENGTH (the formula, every member)", b_get(0x4C00) == want, "%d, want %d", b_get(0x4C00), want);
    }
    // ---- CHECKPARTY: op1 - 0x7FFF, compared signed: 0x8001 (op1 0) an effect
    // anyone has; 0xA5-0xAC (op1 0x80A4-0x80AB) a thief skill's lowest,
    // highest, average; 0x9F (0x809E) movement; anything else writes nothing
    // (Curse's one use, op1 [7CA5]: nothing)
    {
        const int w = 0xA6;                                    // op1 0x80A5
        int lo = 255, hi = 0, sum = 0;
        for (int i = 0; i < pt->count; ++i) {
            const int v = pt->m[i].rec[0xE9 + (w - 0xA4)];
            lo = v < lo ? v : lo;
            hi = v > hi ? v : hi;
            sum += v;
        }
        BAsm a;
        a.op(kSave).b(9).m(0x4C03);
        a.op(kCheckParty).m(static_cast<uint16_t>(0x7FFF + w)).b(0).m(0x4C00).m(0x4C01).m(0x4C02).m(0x4C03);
        a.op(kSave).b(7).m(0x4C04);
        a.op(kCheckParty).m(0x7CA5).b(0).m(0x4C04).m(0x4C04).m(0x4C04).m(0x4C04);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "CHECKPARTY [80A5]: a thief skill's lowest, highest, average, then 0",
               b_get(0x4C00) == lo && b_get(0x4C01) == hi && b_get(0x4C02) == sum / pt->count && b_get(0x4C03) == 0,
               "%d %d %d %d want %d %d %d", b_get(0x4C00), b_get(0x4C01), b_get(0x4C02), b_get(0x4C03), lo, hi, sum / pt->count);
        expect(P, "CHECKPARTY [7CA5] (the scripts' own use): writes nothing", b_get(0x4C04) == 7, "%d", b_get(0x4C04));
    }
    // ---- FIND ITEM (anyone carries one of the type) / DESTROY ITEMS (all of it, everyone)
    {
        party::Character keep1 = pt->m[1], keep4 = pt->m[4];
        for (int i : {1, 4}) {
            party::Character& c = pt->m[i];
            memset(c.items[c.n_items], 0, party::kItemSize);
            c.items[c.n_items][0x2E] = 0x2F;            // an item of type 47 for both (none readied)
            ++c.n_items;
        }
        int type = -1;
        for (int i = 0; i < pt->count && type < 0; ++i)
            if (pt->m[i].n_items) type = pt->m[i].items[0][0x2E];
        int have = 0;
        for (int i = 0; i < pt->count; ++i)
            for (int k = 0; k < pt->m[i].n_items; ++k) have += pt->m[i].items[k][0x2E] == type;
        BAsm a;
        a.op(kSave).b(0).m(0x4C00);
        a.op(kSave).b(0).m(0x4C01);
        a.op(kFindItem).b(static_cast<uint8_t>(type));
        a.op(kIfEq).op(kSave).b(1).m(0x4C00);
        a.op(kDestroyItems).b(static_cast<uint8_t>(type));
        a.op(kFindItem).b(static_cast<uint8_t>(type));
        a.op(kIfNe).op(kSave).b(1).m(0x4C01);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        int left = 0;
        for (int i = 0; i < pt->count; ++i)
            for (int k = 0; k < pt->m[i].n_items; ++k) left += pt->m[i].items[k][0x2E] == type;
        expect(P, "FIND ITEM finds a carried type; DESTROY ITEMS takes every one from everyone",
               type >= 0 && have > 0 && b_get(0x4C00) == 1 && b_get(0x4C01) == 1 && left == 0, "type %d had %d left %d (%d %d)", type,
               have, left, b_get(0x4C00), b_get(0x4C01));
        pt->m[1] = keep1;
        pt->m[4] = keep4;
    }
    // ---- SPELL: the first member with it memorized (1-based place in the list, member); none: 0xFF, the last member
    {
        party::Character keep2 = pt->m[2], keep3 = pt->m[3];
        memset(&pt->m[2].rec[0x1E], 0, 84);
        memset(&pt->m[3].rec[0x1E], 0, 84);
        pt->m[3].rec[0x1E + 1] = 0x05;
        pt->m[3].rec[0x1E + 4] = 0x0B;              // after a gap: the 4th slot (2nd spell)
        pt->m[3].rec[0x1E + 6] = 0x0B;
        int who = -1, place = 0, sp = 0x0B;
        for (int i = 0; i < pt->count && who < 0; ++i)
            for (int k = 1; k <= 0x65; ++k)
                if (pt->m[i].rec[0x1E + k] == sp) {
                    who = i;
                    break;
                }
        // the original's scan (sub_28E33): index 1 .. 0x65, the byte at record
        // 0x1E + index (empty slots counted; coab's IdList() skips them - the
        // listing wins)
        if (who >= 0)
            for (int i = 1; i <= 0x65; ++i)
                if (pt->m[who].rec[0x1E + i] == sp) {
                    place = i;
                    break;
                }
        BAsm a;
        a.op(kSpell).b(static_cast<uint8_t>(sp)).m(0x4C00).m(0x4C01);
        a.op(kSpell).b(0x63).m(0x4C02).m(0x4C03);           // a spell nobody has
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "SPELL finds the first member with it: place in the list, member", who >= 0 && b_get(0x4C00) == place && b_get(0x4C01) == who,
               "spell %02X: %d / %d, want %d / %d", sp, b_get(0x4C00), b_get(0x4C01), place, who);
        expect(P, "SPELL none: 0xFF and the last member", b_get(0x4C02) == 0xFF && b_get(0x4C03) == pt->count - 1, "%d %d",
               b_get(0x4C02), b_get(0x4C03));
        pt->m[2] = keep2;
        pt->m[3] = keep3;
    }
    // ---- CLEARMONSTERS clears the pooled money / items; TREASURE sets the 7 coins
    {
        BAsm a;
        a.op(kTreasure).b(1).b(2).b(3).b(4).b(5).b(6).b(7).b(0xFF);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        bool ok = true;
        for (int k = 0; k < 7; ++k) ok = ok && ground->money[k] == k + 1;
        BAsm b2;
        b2.op(kClearMon);
        b2.op(kExit);
        b_run(b2, 0x8000 + 20);
        expect(P, "TREASURE sets the 7 coin amounts; CLEARMONSTERS clears them", ok && !ground->any_money());
    }
    // ---- ROB: who (0 the selected), % of every coin, each item at the chance
    {
        party::Character keep[party::kMaxParty];
        for (int i = 0; i < pt->count; ++i) keep[i] = pt->m[i];
        pt->selected = 0;
        for (int i = 0; i < pt->count; ++i) {
            pt->m[i].rec[0x101] = 200;
            pt->m[i].rec[0x102] = 0;
        }
        dice_high();                                   // every d100 100: no item goes at chance 50
        BAsm a;
        a.op(kRob).b(0).b(25).b(50);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        const int g0 = pt->m[0].rec[0x101] | pt->m[0].rec[0x102] << 8, g1 = pt->m[1].rec[0x101] | pt->m[1].rec[0x102] << 8;
        const int items0 = pt->m[0].n_items;
        dice_low();                                    // every d100 1: every item goes at chance >= 1
        BAsm b2;
        b2.op(kRob).b(1).b(100).b(100);
        b2.op(kExit);
        b_run(b2, 0x8000 + 20);
        dice_usual();
        bool all_gone = true;
        for (int i = 0; i < pt->count; ++i) {
            // the original: heavier items cut the chance (over 255: -90, over 24: -50) from then on
            int ch = 100, want_left = 0;
            for (int k = 0; k < keep[i].n_items; ++k) {
                const int wt = keep[i].items[k][0x37] | keep[i].items[k][0x38] << 8;
                if (wt > 255) ch = ch > 90 ? ch - 90 : 0;
                else if (wt > 24) ch = ch > 50 ? ch - 50 : 0;
                if (!(1 <= ch)) ++want_left;
            }
            if (pt->m[i].n_items != want_left || pt->m[i].rec[0x101] || pt->m[i].rec[0x102]) all_gone = false;
        }
        expect(P, "ROB 0, 25%: the selected loses a quarter of each coin, the others nothing; no item at a d100 of 100",
               g0 == 150 && g1 == 200 && items0 == keep[0].n_items, "gold %d %d items %d/%d", g0, g1, items0, keep[0].n_items);
        expect(P, "ROB all, 100%: everyone's money; items at the chance less the weight cuts", all_gone);
        for (int i = 0; i < pt->count; ++i) pt->m[i] = keep[i];
    }
    // ---- SURPRISE: the original writes its result to 0x02CB - not the fight's word 0x7ECB
    {
        vm->set(0x7ECB, 0x55);
        dice_low();
        BAsm a;
        a.op(kSurprise).b(0).b(5).b(0).b(5);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        dice_usual();
        expect(P, "SURPRISE leaves 0x7ECB as it was (its result goes to 0x02CB)", b_get(0x7ECB) == 0x55, "%X", b_get(0x7ECB));
        vm->set(0x7ECB, 0);
    }
    // ---- NEWECL: the block it leaves goes to 0x4BF2
    {
        const int was = d->gs.script;
        BAsm a;
        a.op(kNewEcl).b(static_cast<uint8_t>(was));
        b_run(a, 0x8000 + 20);
        expect(P, "NEWECL: the block left in 0x4BF2", b_get(0x4BF2) == was && d->gs.script == was && vm->new_script());
    }
    // ---- PARTY SURPRISE: 1 with a ranger (class 4) or cleric / ranger (10), the second word 0
    {
        party::Character keep = pt->m[0];
        BAsm a;
        a.op(kPartySurprise).m(0x4C00).m(0x4C01);
        a.op(kExit);
        vm->set(0x4C01, 7);
        b_run(a, 0x8000 + 20);
        int ranger = 0;
        for (int i = 0; i < pt->count; ++i) ranger |= pt->m[i].rec[0x75] == 4 || pt->m[i].rec[0x75] == 10;
        const int r0 = b_get(0x4C00), r1 = b_get(0x4C01);
        pt->m[0].rec[0x75] = 10;
        b_run(a, 0x8000 + 20);
        const int r2 = b_get(0x4C00);
        pt->m[0] = keep;
        expect(P, "PARTY SURPRISE: 1 with a ranger or cleric / ranger, the other word 0", r0 == ranger && r1 == 0 && r2 == 1, "%d %d %d",
               r0, r1, r2);
    }
    // ---- CALL C01E: a step forward (the map wraps)
    {
        d->gs.x = 15;
        d->gs.y = 0;
        d->gs.dir = 2;
        BAsm a;
        a.op(kCall).w(0xC01E);
        a.op(kCall).w(0xC01E);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "CALL C01E: forward a square, wrapping at the edge", d->gs.x == 1 && d->gs.y == 0, "%d,%d", d->gs.x, d->gs.y);
    }
    // ---- the position words: 0xC04D a facing 0-3 (4 and up: less 4 until it fits)
    {
        BAsm a;
        a.op(kSave).b(7).m(0xC04D);
        a.op(kSave).m(0xC04D).m(0x4C00);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        expect(P, "0xC04D: facing 7 is 3 (west)", d->gs.dir == 6 && b_get(0x4C00) == 3, "dir %d read %d", d->gs.dir, b_get(0x4C00));
    }
    // ---- DAMAGE (the dice forced): attacks, saves, the selected member
    {
        party::Character keep[party::kMaxParty];
        const int n = pt->count;
        for (int i = 0; i < n; ++i) keep[i] = pt->m[i];
        auto heal = [&]() {
            for (int i = 0; i < n; ++i) {
                pt->m[i] = keep[i];
                pt->m[i].rec[0x1A4] = 40;
                pt->m[i].rec[0x78] = 40;
                pt->m[i].rec[0x195] = party::Okay;
                pt->m[i].rec[0x196] = 1;
            }
        };
        auto lost = [&](int i) { return 40 - pt->m[i].hp(); };
        auto finish = [&](ecl::Stop r) {
            for (int k = 0; k < 40 && r == ecl::Stop::Waiting; ++k) r = vm->resume();
        };
        // 0x80 | 0x40 | 0x20: everyone, no save: 2d6 + 3 each (high dice: 15)
        heal();
        dice_high();
        BAsm a;
        a.op(kDamage).b(0xE0).b(2).b(6).b(3).b(0);
        a.op(kExit);
        finish(b_run(a, 0x8000 + 20));
        int bad = 0;
        for (int i = 0; i < n; ++i) bad += lost(i) != 15;
        expect(P, "DAMAGE 0xE0: everyone, no save, the same dice (2d6 + 3 at most: 15)", bad == 0, "%d wrong", bad);
        // 0x80 | 0x40: everyone saves (top dice: a 20 always saves): none hurt
        heal();
        dice_high();
        BAsm b2;
        b2.op(kDamage).b(0xC0).b(2).b(6).b(3).b(4);
        b2.op(kExit);
        finish(b_run(b2, 0x8000 + 20));
        bad = 0;
        for (int i = 0; i < n; ++i) bad += lost(i) != 0;
        expect(P, "DAMAGE 0xC0: everyone saves on a 20 - no damage", bad == 0, "%d hurt", bad);
        // ... 0x10: hurt even when saved
        heal();
        dice_high();
        BAsm b3;
        b3.op(kDamage).b(0xD0).b(2).b(6).b(3).b(4);
        b3.op(kExit);
        finish(b_run(b3, 0x8000 + 20));
        bad = 0;
        for (int i = 0; i < n; ++i) bad += lost(i) != 15;
        expect(P, "DAMAGE 0xD0: saved, and hurt all the same (full damage)", bad == 0, "%d wrong", bad);
        // a natural 1 never saves (dice low: 1s; damage 2d6 + 3 = 5)
        heal();
        dice_low();
        BAsm b4;
        b4.op(kDamage).b(0xC0).b(2).b(6).b(3).b(4);
        b4.op(kExit);
        finish(b_run(b4, 0x8000 + 20));
        bad = 0;
        for (int i = 0; i < n; ++i) bad += lost(i) != 5;
        expect(P, "DAMAGE 0xC0 with every d20 a 1: no one saves (5 each)", bad == 0, "%d wrong", bad);
        // 0x80, op5 0x80 | type 0: the selected member, no save
        heal();
        pt->selected = 3;
        dice_high();
        BAsm b5;
        b5.op(kDamage).b(0x80).b(1).b(8).b(0).b(0x80);
        b5.op(kExit);
        finish(b_run(b5, 0x8000 + 20));
        bad = 0;
        for (int i = 0; i < n; ++i) bad += lost(i) != (i == 3 ? 8 : 0);
        expect(P, "DAMAGE 0x80, 0x80: only the selected member, no save (1d8: 8)", bad == 0 && pt->selected == 3, "%d wrong", bad);
        // 0x80 random member: the d(party) die picks (faces: member 2 = face 2), its d20 (1: fails)
        heal();
        dice_faces({4, 6, 2, 1});              // 1d6+... : damage die 4,6 (2d6=10), member die 2, save d20 1
        BAsm b6;
        b6.op(kDamage).b(0x80).b(2).b(6).b(0).b(3);
        b6.op(kExit);
        finish(b_run(b6, 0x8000 + 20));
        bad = 0;
        for (int i = 0; i < n; ++i) bad += lost(i) != (i == 1 ? 10 : 0);
        expect(P, "DAMAGE 0x80: a random member (its die), a failed save: the dice", bad == 0, "%d wrong", bad);
        // attacks (no 0x80): n attacks, each a d20 + op5 against the AC byte; a 20 always hits, a 1 never
        heal();
        // the dice in order: damage 1d3 (3), the random member the original rolls
        // anyway (unused here), then each attack: member, d20, the next damage
        dice_faces({3, 6, 1, 20, 2, 1, 5, 1});
        BAsm b7;
        b7.op(kDamage).b(2).b(1).b(3).b(0).b(0);
        b7.op(kExit);
        finish(b_run(b7, 0x8000 + 20));
        dice_usual();
        // attack 1: member 1 (face 1), d20 20: hit for 3; damage again 2; attack 2: member 1 (face 1), d20 5 + 0 > AC?
        const int ac1 = keep[0].rec[0x19A];
        const int want0 = 3 + (5 > ac1 ? 2 : 0);
        expect(P, "DAMAGE 2 attacks: a 20 hits, a 5 hits only over the AC byte; new dice each attack", lost(0) == want0,
               "lost %d, want %d (AC byte %d)", lost(0), want0, ac1);
        // the dying rules (damage_player): > 9 below 0 dead, below 0 dying, 0 unconscious
        heal();
        pt->m[0].rec[0x1A4] = 5;
        pt->m[1].rec[0x1A4] = 5;
        pt->m[2].rec[0x1A4] = 5;
        pt->selected = 0;
        dice_faces({5});
        BAsm d0;
        d0.op(kDamage).b(0x80).b(1).b(20).b(0).b(0x80);
        d0.op(kExit);
        finish(b_run(d0, 0x8000 + 20));
        pt->selected = 1;
        dice_faces({9});
        finish(b_run(d0, 0x8000 + 20));
        pt->selected = 2;
        dice_faces({16});
        finish(b_run(d0, 0x8000 + 20));
        dice_usual();
        expect(P, "DAMAGE to 0: unconscious; 4 below: dying; 11 below: dead (none in combat after)",
               pt->m[0].health() == party::Unconscious && pt->m[1].health() == party::Dying && pt->m[2].health() == party::Dead &&
                   !pt->m[0].in_combat() && !pt->m[2].in_combat() && pt->m[0].hp() == 0,
               "%d %d %d", pt->m[0].health(), pt->m[1].health(), pt->m[2].health());
        for (int i = 0; i < n; ++i) pt->m[i] = keep[i];
    }
    // ---- PROGRAM: 0 the party menu, 9 the camp (the script ends), 8 won, 3 the party is gone
    {
        BAsm a;
        a.op(kProgram).b(0);
        a.op(kSave).b(1).m(0x4C00);
        a.op(kExit);
        vm->set(0x4C00, 0);
        const ecl::Stop r = b_run(a, 0x8000 + 20);
        const bool menu_wait = r == ecl::Stop::Waiting && vm->wait() == ecl::Wait::PartyMenu;
        vm->resume();
        expect(P, "PROGRAM 0: the party menu, then the script goes on", menu_wait && b_get(0x4C00) == 1);
        BAsm b9;
        b9.op(kProgram).b(9);
        b9.op(kSave).b(1).m(0x4C01);
        b9.op(kExit);
        vm->set(0x4C01, 0);
        const ecl::Stop r9 = b_run(b9, 0x8000 + 20);
        const bool camp = r9 == ecl::Stop::Waiting && vm->wait() == ecl::Wait::Camp;
        vm->resume();
        expect(P, "PROGRAM 9: the camp, then the script ends (EXIT)", camp && b_get(0x4C01) == 0);
    }
    // ---- HORIZONTAL MENU: the choice a byte; VERTICAL MENU the index; PARLAY the value for the answer
    {
        BAsm a;
        a.op(kHMenu).m(0x4C00).b(3).s("ONE").s("TWO").s("SIX");
        a.op(kParlay).b(10).b(20).b(30).b(40).b(50).m(0x4C01);
        a.op(kVMenu).m(0x4C02).s("PICK").b(2).s("A").s("B");
        a.op(kExit);
        ecl::Stop r = b_run(a, 0x8000 + 20);
        const bool h = r == ecl::Stop::Waiting && vm->wait() == ecl::Wait::Menu && vm->items() == 3;
        r = vm->answer(2);
        const bool p = r == ecl::Stop::Waiting && vm->wait() == ecl::Wait::Menu && vm->items() == 5;
        r = vm->answer(3);
        const bool v = r == ecl::Stop::Waiting && vm->wait() == ecl::Wait::ListMenu && vm->items() == 2;
        vm->answer(1);
        expect(P, "HORIZONTAL MENU / PARLAY / VERTICAL MENU: the answers stored", h && p && v && b_get(0x4C00) == 2 && b_get(0x4C01) == 40 &&
                                                                                       b_get(0x4C02) == 1,
               "%d %d %d", b_get(0x4C00), b_get(0x4C01), b_get(0x4C02));
    }
    // ---- DUMP: the selected member leaves the party
    {
        party::Party keep = *pt;
        pt->selected = 2;
        char nm[20], nm3[20];
        pt->m[3].name(nm3, sizeof nm3);
        BAsm a;
        a.op(kDump);
        a.op(kExit);
        b_run(a, 0x8000 + 20);
        pt->m[2].name(nm, sizeof nm);
        expect(P, "DUMP: the selected member leaves; the next moves up", pt->count == keep.count - 1 && !strcmp(nm, nm3));
        *pt = keep;
        vm->set(0x7F3E, static_cast<uint16_t>(pt->count));
    }
}

// The encounter menu's choices (CMD_EncounterMenu): for each result code
// 0-4 and each choice, what the result word gets (or "again": the menu is
// asked again). Distance 0 (Parlay in place of Advance).
static void b_encounter()
{
    using namespace play;
    const char* P = "events";
    int bad = 0, n = 0;
    for (int code = 0; code <= 4; ++code)
        for (int choice = 0; choice < 4; ++choice) {
            // the party fast or slow against the flee speed / the monsters' speed
            for (int fast = 0; fast < 2; ++fast) {
                BAsm a;
                a.op(kSave).b(0xEE).m(0x4C00);
                a.op(kEncMenu).b(1).b(0).b(1).m(0x4C00).b(static_cast<uint8_t>(code)).b(static_cast<uint8_t>(code))
                    .b(static_cast<uint8_t>(code)).b(static_cast<uint8_t>(code)).b(static_cast<uint8_t>(code))
                    .s("").s("").s("").b(static_cast<uint8_t>(fast ? 1 : 250)).b(static_cast<uint8_t>(fast ? 250 : 1));
                a.op(kExit);
                ecl::Stop r = b_run(a, 0x8000 + 20);
                int guard = 0;
                while (r == ecl::Stop::Waiting && vm->wait() != ecl::Wait::Menu && guard++ < 5) r = vm->resume();
                if (r != ecl::Stop::Waiting) {
                    ++bad;
                    continue;
                }
                r = vm->answer(choice);
                bool again = false;
                guard = 0;
                while (r == ecl::Stop::Waiting && guard++ < 5) {
                    if (vm->wait() == ecl::Wait::Menu) {
                        again = true;
                        break;
                    }
                    r = vm->resume();
                }
                // coab: near (distance 0): Parlay is choice 4
                const int sel = choice == 3 ? 4 : choice;
                int want = -1;                  // -1: asked again
                const bool party_fast = fast != 0;
                switch (code) {
                case 0: want = sel != 2 ? 1 : (party_fast ? 2 : 1); break;
                case 1: want = sel == 0 ? 1 : sel == 1 ? -1 : sel == 2 ? 2 : sel == 3 ? -1 : 3; break;
                case 2: want = sel == 0 ? (!party_fast ? 0 : 1) : 0; break;
                case 3: want = sel == 0 ? 1 : (sel == 1 || sel == 3) ? -1 : sel == 2 ? 2 : 3; break;
                case 4: want = sel == 0 ? 1 : sel == 2 ? 2 : 3; break;
                }
                // (code 2, choice 0: the monsters flee when faster than the party's fastest)
                // ("fast": the flee speed 1, the monsters' speed 250 - they outrun the party)
                if (code == 2 && sel == 0) want = party_fast ? 0 : 1;
                const int got = again ? -1 : b_get(0x4C00);
                ++n;
                if (got != want) {
                    ++bad;
                    printf("   encounter code %d choice %d fast %d: %d, want %d\n", code, choice, fast, got, want);
                }
                if (again) {
                    vm->init_script(true);              // (leave the menu)
                }
            }
        }
    expect(P, "ENCOUNTER MENU: every result code x choice (distance 0, party fast / slow)", bad == 0, "%d of %d wrong", bad, n);
    // Fleeing (code 0, Flee): the party's slowest against the speed needed -
    // the original's calc_group_inituative: every member (out of action too),
    // movement byte 0x1A5, doubled when hasted, halved when slowed
    auto flee = [&](const char* what, const std::function<void()>& setup, int want) {
        party::Party keep = *pt;
        for (int i = 0; i < pt->count; ++i) pt->m[i].rec[0x1A5] = 12;
        setup();
        BAsm a;
        a.op(kEncMenu).b(1).b(0).b(1).m(0x4C00).b(0).b(0).b(0).b(0).b(0).s("").s("").s("").b(12).b(12);
        a.op(kExit);
        ecl::Stop r = b_run(a, 0x8000 + 20);
        for (int g = 0; r == ecl::Stop::Waiting && vm->wait() != ecl::Wait::Menu && g < 5; ++g) r = vm->resume();
        vm->answer(2);
        expect(P, what, b_get(0x4C00) == want, "result %d, want %d", b_get(0x4C00), want);
        *pt = keep;
    };
    const auto& fx = d->prof->fight.facts;
    flee("fleeing: every member as fast as needed - the party flees (2)", [] {}, 2);
    flee("fleeing: a dead member who is slow still holds the party back (1)", [] {
        pt->m[3].rec[0x1A5] = 6;
        pt->m[3].rec[0x195] = party::Dead;
        pt->m[3].rec[0x196] = 0;
    }, 1);
    if (fx.haste)
        flee("fleeing: a slow member hasted counts double (6 x 2 = 12: flees)", [&] {
            party::Character& c = pt->m[2];
            c.rec[0x1A5] = 6;
            memset(c.affects[c.n_affects], 0, party::kAffectSize);
            c.affects[c.n_affects][0] = fx.haste;
            ++c.n_affects;
        }, 2);
    if (fx.slow)
        flee("fleeing: a slowed member counts half (12 / 2 = 6: can't flee)", [&] {
            party::Character& c = pt->m[2];
            memset(c.affects[c.n_affects], 0, party::kAffectSize);
            c.affects[c.n_affects][0] = fx.slow;
            ++c.n_affects;
        }, 1);
}

// The fight's surprise word 0x7ECB as the scripts SAVE it: the original's
// initiative takes 6 from a side when (its team + 1) & the word - 1 the
// party, 2 the monsters (ovr014 sub_3E000); cleared after the first round
static void b_surprise()
{
    using namespace play;
    const char* P = "events";
    for (int word = 1; word <= 2; ++word) {
        if (!boot()) return;
        vm->set(0x7ECB, static_cast<uint16_t>(word));
        dice_faces({});
        b_dice_mode = 1;                    // every die its top: d6 initiative 6 for everyone
        if (!start_fight(1, 0)) {
            expect(P, "surprise: a fight", false);
            dice_usual();
            return;
        }
        for (int k = 0; k < 2000 && fg && fg->cur < 0; ++k) {
            g_now += 20;
            play::tick(g_now, C);
        }
        dice_usual();
        if (!fg) {
            expect(P, "surprise: the fight runs", false);
            return;
        }
        // Every die its top: the same d6 for everyone, so the first to act is
        // on the side not surprised (the surprised side's 6 less)
        const int first = fg->cur;
        const int team = first >= 0 ? fg->b.f[first].team() : -1;
        char what[96];
        snprintf(what, sizeof what, "0x7ECB = %d: %s side surprised - the other acts first", word, word == 1 ? "the party's" : "the monsters'");
        expect(P, what, team == (word == 1 ? 1 : 0), "first to act: #%d team %d", first, team);
        expect(P, "0x7ECB cleared once the first round's initiative is rolled", b_get(0x7ECB) == 0, "%d", b_get(0x7ECB));
    }
}

// ============================================================================
// Steps, time, Look, map edges, rest encounters (ovr015 / sub_29758 / ovr021)
// ============================================================================

// A dungeon block of the player's: its map; a square with an open side
struct BSquare {
    int x = -1, y = -1, dir = 0;
};

// Puts a synthetic script in as the running block (the map stays) and lets
// the front end run its step / arrival entries
static void b_use(const BAsm& a)
{
    using namespace play;
    ecl::GameState& g = d->gs;
    memset(g.code, 0, sizeof g.code);
    memcpy(g.code, a.c.data(), a.c.size());
    g.code_len = static_cast<uint32_t>(a.c.size());
    vm->init_script(true);
    waiting = false;
    then = Then::Idle;
}

static void b_steps()
{
    using namespace play;
    const char* P = "events";
    if (!boot()) return;
    if (!d->map.loaded || !vm->get(0x4BE6)) {
        expect(P, "steps: save A starts in a 3D area", false);
        return;
    }
    // A square with an open way forward, and one facing a solid wall
    BSquare open, wall, edge;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x)
            for (int dir = 0; dir < 8; dir += 2) {
                const int p = geo::passage(d->map, x, y, dir);
                const int nx = x + geo::dx(dir), ny = y + geo::dy(dir);
                const bool on = nx >= 0 && nx < 16 && ny >= 0 && ny < 16;
                if (p == 1 && on && open.x < 0) open = {x, y, dir};
                if (p == 0 && wall.x < 0) wall = {x, y, dir};
                if (p == 1 && !on && edge.x < 0) edge = {x, y, dir};
            }
    // A script that counts its runs: step entry -> 0x4C00, arrival -> 0x4C01,
    // what 0x7ECA / 0x7ED5 were in each
    BAsm a;
    const uint16_t step = a.here();
    a.op(kAdd).m(0x4C00).b(1).m(0x4C00);
    a.op(kSave).m(0x7ED5).m(0x4C02);
    a.op(kSave).m(0xC04B).m(0x4C05);
    a.op(kExit);
    const uint16_t arrive = a.here();
    a.op(kAdd).m(0x4C01).b(1).m(0x4C01);
    a.op(kSave).m(0x7ECA).m(0x4C03);
    a.op(kSave).m(0xC04B).m(0x4C06);
    a.op(kExit);
    const uint16_t none = a.here();
    a.op(kExit);
    a.entry(0, step);
    a.entry(1, arrive);
    a.entry(2, none);
    a.entry(3, none);
    a.entry(4, none);
    auto clock = [&]() {
        long t = 0;
        const int sc[6] = {1, 1, 10, 60, 1440, 43200};
        for (int k = 1; k <= 5; ++k) t += static_cast<long>(b_get(static_cast<uint16_t>(0x4BC6 + k))) * sc[k];
        return t;
    };
    auto reset = [&]() {
        b_use(a);
        for (uint16_t x = 0x4C00; x <= 0x4C06; ++x) vm->set(x, 0);
        vm->set(0x7ECA, 0);
    };
    // ---- a step: the step entry before the move (on the old square), then the arrival on the new; 1 minute
    if (open.x >= 0) {
        reset();
        d->gs.x = open.x;
        d->gs.y = open.y;
        d->gs.dir = open.dir;
        const long t0 = clock();
        play::act(play::Act::Forward, C);
        pump(20000);
        const int nx = (open.x + geo::dx(open.dir)) & 15, ny = (open.y + geo::dy(open.dir)) & 15;
        expect(P, "a step: the step script once on the old square, the move, the arrival script once on the new",
               b_get(0x4C00) == 1 && b_get(0x4C01) == 1 && b_get(0x4C05) == open.x && b_get(0x4C06) == nx && d->gs.x == nx && d->gs.y == ny,
               "runs %d %d, x %d -> %d, at %d,%d", b_get(0x4C00), b_get(0x4C01), b_get(0x4C05), b_get(0x4C06), d->gs.x, d->gs.y);
        expect(P, "a step takes 1 minute", clock() - t0 == 1, "%ld", clock() - t0);
        // Search on: 10 minutes a step; 0x7ECA 1 in the scripts
        reset();
        vm->set(0x7ECA, 1);
        d->gs.x = open.x;
        d->gs.y = open.y;
        d->gs.dir = open.dir;
        const long t1 = clock();
        play::act(play::Act::Forward, C);
        pump(20000);
        expect(P, "searching: a step takes 10 minutes, 0x7ECA reads 1", clock() - t1 == 10 && b_get(0x4C03) == 1, "%ld min, %d",
               clock() - t1, b_get(0x4C03));
        vm->set(0x7ECA, 0);
    }
    // ---- a wall ahead: the step script runs, no move, the arrival script still runs, no time
    if (wall.x >= 0) {
        reset();
        d->gs.x = wall.x;
        d->gs.y = wall.y;
        d->gs.dir = wall.dir;
        const long t0 = clock();
        play::act(play::Act::Forward, C);
        pump(20000);
        expect(P, "into a wall: step script, no move, arrival script, no time",
               b_get(0x4C00) == 1 && b_get(0x4C01) == 1 && d->gs.x == wall.x && d->gs.y == wall.y && clock() == t0, "runs %d %d at %d,%d %ld",
               b_get(0x4C00), b_get(0x4C01), d->gs.x, d->gs.y, clock() - t0);
    }
    // ---- 0x7EC9 >= 0xFF set by the step script: no move
    if (open.x >= 0) {
        BAsm s = a;
        BAsm st;
        st.c = a.c;
        // a step entry that stops the move
        const uint16_t stop = st.here();
        st.op(kSave).b(0xFF).m(0x7EC9);
        st.op(kExit);
        st.entry(0, stop);
        b_use(st);
        vm->set(0x4C01, 0);
        d->gs.x = open.x;
        d->gs.y = open.y;
        d->gs.dir = open.dir;
        play::act(play::Act::Forward, C);
        pump(20000);
        expect(P, "the step script's 0x7EC9 = 0xFF: the party stays (the arrival script still runs)",
               d->gs.x == open.x && d->gs.y == open.y && b_get(0x4C01) == 1 && b_get(0x7EC9) == 0, "at %d,%d runs %d 7EC9 %d", d->gs.x,
               d->gs.y, b_get(0x4C01), b_get(0x7EC9));
    }
    // ---- the map's edge: an open side off the map sets 0x7ED5 for the step script (TryStepForward)
    if (edge.x >= 0) {
        reset();
        d->gs.x = edge.x;
        d->gs.y = edge.y;
        d->gs.dir = edge.dir;
        play::act(play::Act::Forward, C);
        pump(20000);
        expect(P, "a step through an open side off the map: 0x7ED5 = 1 in the step script", b_get(0x4C02) == 1, "%d", b_get(0x4C02));
    } else {
        expect(P, "the map's edge: an open side off the map in save A's area", false, "none found");
    }
    if (open.x >= 0) {
        reset();
        vm->set(0x7ED5, 1);
        d->gs.x = open.x;
        d->gs.y = open.y;
        d->gs.dir = open.dir;
        play::act(play::Act::Forward, C);
        pump(20000);
        expect(P, "a step inside the map: 0x7ED5 = 0 in the step script", b_get(0x4C02) == 0, "%d", b_get(0x4C02));
    }
    // ---- Look: 10 minutes; the arrival script runs with 0x7ECA = 1; search mode as it was after
    for (int search = 0; search < 2; ++search) {
        reset();
        vm->set(0x7ECA, static_cast<uint16_t>(search));
        const int x0 = d->gs.x, y0 = d->gs.y;
        const long t0 = clock();
        play::act(play::Act::Look, C);
        pump(20000);
        char what[96];
        snprintf(what, sizeof what, "Look (search %s): 10 minutes, the arrival script with 0x7ECA = 1, no step script, no move",
                 search ? "on" : "off");
        expect(P, what,
               clock() - t0 == 10 && b_get(0x4C01) == 1 && b_get(0x4C03) == 1 && b_get(0x4C00) == 0 && d->gs.x == x0 && d->gs.y == y0,
               "%ld min, runs %d/%d, 7ECA %d", clock() - t0, b_get(0x4C00), b_get(0x4C01), b_get(0x4C03));
        snprintf(what, sizeof what, "after Look: search mode %s as before", search ? "on" : "off");
        expect(P, what, b_get(0x7ECA) == search, "0x7ECA %d", b_get(0x7ECA));
    }
    // ---- turning: no script, no time (the original's menu loop: turns stay in it)
    {
        reset();
        const long t0 = clock();
        play::act(play::Act::TurnLeft, C);
        pump(20000);
        play::act(play::Act::TurnAround, C);
        pump(20000);
        expect(P, "turning runs no script and takes no time", b_get(0x4C00) == 0 && b_get(0x4C01) == 0 && clock() == t0);
    }
}

// Rest: an encounter check every `period` five-minute steps (0x7ED2), at
// `chance` % (0x7ED3); the count goes on from rest to rest (the original's
// counter is reset only when the program starts and when it reaches the
// period)
static void b_rest()
{
    using namespace play;
    const char* P = "events";
    if (!boot()) return;
    auto rest_for = [&](int minutes) {
        // Encamp, Rest with this much time, run it out
        open_camp(C);
        open_rest(C, false);
        rest.left = minutes;
        rest.running = true;
        magic::begin(rest.r);
        for (int k = 0; k < 4000 && screen == Screen::Rest; ++k) {
            g_now += 50;
            play::tick(g_now, C);
        }
    };
    vm->set(0x7ED2, 4);                 // a check every 4 steps (20 minutes)
    vm->set(0x7ED3, 100);               // always
    b_asked.clear();
    // 3 steps, then 3 more: the 4th step overall is checked (interrupted)
    rest_for(15);
    const bool first_quiet = !rest.interrupted;
    const int sc0 = static_cast<int>(screen);
    if (screen != Screen::Camp && screen != Screen::Game) play::back(C);
    for (int k = 0; k < 20 && screen != Screen::Game; ++k) {
        if (screen == Screen::Camp) tap_word('E');
        else play::back(C);
        ticks(10);
    }
    pump(20000);
    vm->set(0x7ED2, 4);
    vm->set(0x7ED3, 100);
    // a second rest of 15 minutes: steps 4-6 - the 4th breaks in
    open_camp(C);
    open_rest(C, false);
    rest.left = 15;
    rest.running = true;
    magic::begin(rest.r);
    bool interrupted = false;
    for (int k = 0; k < 4000 && screen == Screen::Rest; ++k) {
        g_now += 50;
        play::tick(g_now, C);
        if (rest.interrupted) interrupted = true;
    }
    expect(P, "rest encounters: the step count goes on from one rest to the next (4 steps over two rests: checked)",
           first_quiet && interrupted, "first rest quiet %d (screen %d), second interrupted %d", first_quiet, sc0, interrupted);
}

// ============================================================================
// Triggers: every block, every square and facing, one step forward - the
// first things the player meets, against the oracle (worked out from the
// scripts by the scratch reader)
// ============================================================================

// The state dumped for the scratch reader: the block, then the memory and map
static void b_dump(FILE* f, int area, int block)
{
    using namespace play;
    const ecl::GameState& g = d->gs;
    fprintf(f, "STATE %d %d %d %d %d %u %d\n", area, block, g.x, g.y, g.dir, g.code_len, pt->selected);
    auto hex = [&](const char* tag, const uint8_t* p, size_t n) {
        fprintf(f, "%s ", tag);
        for (size_t i = 0; i < n; ++i) fprintf(f, "%02X", p[i]);
        fprintf(f, "\n");
    };
    hex("AREA1", g.area1, sizeof g.area1);
    hex("AREA2", g.area2, sizeof g.area2);
    hex("TABLE", g.table, sizeof g.table);
    hex("CODE", g.code, g.code_len);
    for (int k = 0; k < 4; ++k) hex("GEO", d->map.plane[k], 256);
    fprintf(f, "MAPLOADED %d\n", d->map.loaded ? 1 : 0);
    fprintf(f, "PARTY %d\n", pt->count);
    for (int i = 0; i < pt->count; ++i) {
        hex("REC", pt->m[i].rec, party::kRecordSize);
        fprintf(f, "ITEMS %d", pt->m[i].n_items);
        for (int k = 0; k < pt->m[i].n_items; ++k) fprintf(f, " %d", pt->m[i].items[k][0x2E]);
        fprintf(f, "\nFX %d", pt->m[i].n_affects);
        for (int k = 0; k < pt->m[i].n_affects; ++k) fprintf(f, " %d", pt->m[i].affects[k][0]);
        fprintf(f, "\n");
    }
    fprintf(f, "END\n");
}

// One step from (x, y, dir): the waits met (up to 3), then where the party is
// A short hash of a text (the game's words stay out of the reports)
static unsigned b_hash(const char* t)
{
    unsigned h = 2166136261u;
    for (; *t; ++t)
        if (*t != ' ' && *t != '\n') h = (h ^ static_cast<uint8_t>(*t)) * 16777619u;
    return h & 0xFFFFFF;
}

static std::string b_one_step(int area, int block, int x, int y, int dir)
{
    using namespace play;
    d->gs.x = x;
    d->gs.y = y;
    d->gs.dir = dir;
    after_move_redraw();
    play::act(play::Act::Forward, C);
    std::string out;
    int events = 0;
    uint16_t last_pc = 0xFFFF;
    int last_wait = -1;
    for (int it = 0; it < 6000 && events < 3; ++it) {
        g_now += 20;
        play::tick(g_now, C);
        if (!d) return out + " gone";
        if (d->gs.script != block || d->gs.game_area != area) {
            char t[32];
            snprintf(t, sizeof t, " NEWECL:%d", d->gs.script);
            return out + t;
        }
        if (screen != Screen::Game) {
            char t[32];
            snprintf(t, sizeof t, " SCREEN:%d", (int)screen);
            return out + t;
        }
        if (then == Then::Door) return out + " DOOR";
        if (!waiting) {
            if (then == Then::Idle && !note_until) break;
            continue;
        }
        const int wt = (int)vm->wait();
        if (wt == (int)ecl::Wait::Pause) continue;
        if (vm->pc() == last_pc && wt == last_wait) {
            if (it % 25 == 0) {
                if (wt == (int)ecl::Wait::Print || wt == (int)ecl::Wait::Key) play::tap(10, 10, C);
                else if (wt == (int)ecl::Wait::Menu) tap_item(0);
                else if (wt == (int)ecl::Wait::ListMenu) {
                    if (list_wait) play::tap(12, list_row0 * 8 + 2, C);
                    else play::tap(10, 10, C);
                } else if (wt == (int)ecl::Wait::Who) {
                    play::tap(17 * 8 + 2, 4 * 8 + 2, C);
                    tap_word('S');
                } else if (input_mode != Input::None) {
                    const char* t = input_mode == Input::Number ? "1" : "A";
                    for (const char* q = t; *q; ++q) play::input_key(*q, C);
                    play::input_key('\n', C);
                }
            }
            continue;
        }
        last_pc = vm->pc();
        last_wait = wt;
        char t[48];
        if (wt == (int)ecl::Wait::Print) snprintf(t, sizeof t, " P:%06X", b_hash(vm->text()));
        else if (wt == (int)ecl::Wait::Menu || wt == (int)ecl::Wait::ListMenu) snprintf(t, sizeof t, " M%d", vm->items());
        else {
            snprintf(t, sizeof t, " W%d", wt);
            out += t;
            return out;               // a fight, a shop, the camp ...: the oracle stops there too
        }
        out += t;
        // empty prints ("\n") count as nothing
        if (wt == (int)ecl::Wait::Print && !*vm->text()) out.resize(out.size() - strlen(t));
        else ++events;
    }
    char t[32];
    snprintf(t, sizeof t, " AT:%d,%d,%d", d->gs.x, d->gs.y, d->gs.dir);
    return out + t;
}

// Into a block as a NEWECL would (the sweep's way): its first run done
static bool b_enter(int area, int block)
{
    using namespace play;
    if (!boot()) return false;
    god = true;
    d->gs.game_area = static_cast<uint8_t>(area);
    vm->set(0x7F12, d->gs.game_area);
    d->gs.script = static_cast<uint8_t>(block);
    waiting = false;
    if (!host->load_script(d->gs.script, d->gs.code, &d->gs.code_len) || !vm->init_script()) return false;
    rnd.seed(1);
    run_entry(4, Then::NewFirst);
    const bool ok = pump();
    god = false;
    return ok && d->gs.script == block && d->gs.game_area == area && screen == Screen::Game && !waiting;
}

static const int b_blocks[][2] = {{1, 80}, {1, 81}, {1, 82}, {2, 1},  {2, 2},  {2, 3},  {2, 4},  {3, 16}, {3, 17},
                                  {3, 18}, {3, 21}, {4, 32}, {4, 33}, {4, 34}, {4, 35}, {4, 37}, {5, 48}, {5, 49},
                                  {5, 50}, {5, 51}, {5, 53}, {6, 64}, {6, 66}, {6, 67}, {6, 69}};

// Mode "events_dump": the states for the scratch reader (stdout)
static void behave_events_dump(const char* path)
{
    FILE* f = fopen(path, "w");
    if (!f) return;
    for (auto& ab : b_blocks) {
        if (getenv("BLOCK") && atoi(getenv("BLOCK")) != ab[1]) continue;
        dice_low();
        const bool ok = b_enter(ab[0], ab[1]);
        dice_usual();
        if (!ok) {
            fprintf(f, "SKIP %d %d\n", ab[0], ab[1]);
            continue;
        }
        b_dump(f, ab[0], ab[1]);
    }
    fclose(f);
}

static void b_triggers()
{
    using namespace play;
    const char* P = "events";
    if (!b_load_oracle()) {
        expect(P, "triggers: the oracle file", false, "no ORACLE_DIR/oracle_events.txt");
        return;
    }
    for (auto& ab : b_blocks) {
        if (getenv("BLOCK") && atoi(getenv("BLOCK")) != ab[1]) continue;
        char key[48];
        snprintf(key, sizeof key, "BLOCK%d_%d", ab[0], ab[1]);
        if (b_oracle.find(key) == b_oracle.end()) continue;
        dice_low();
        if (!b_enter(ab[0], ab[1])) {
            dice_usual();
            expect(P, "triggers: into the block", false, "ECL%d block %d", ab[0], ab[1]);
            continue;
        }
        // the state to go back to before every step
        static ecl::GameState g0;
        static party::Party p0;
        g0 = d->gs;
        p0 = *pt;
        const geo::Map m0 = d->map;
        int n = 0, bad = 0, shown = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                for (int dir = 0; dir < 8; dir += 2) {
                    char k2[64];
                    snprintf(k2, sizeof k2, "T%d_%d_%d_%d_%d", ab[0], ab[1], x, y, dir);
                    auto it = b_oracle.find(k2);
                    if (it == b_oracle.end()) continue;
                    std::string want;
                    for (auto& s : it->second) want += " " + s;
                    if (want == " SKIP") continue;
                    // back to the block's state
                    if (!d || d->gs.script != ab[1] || d->gs.game_area != ab[0] || screen != Screen::Game || waiting) {
                        if (!b_enter(ab[0], ab[1])) break;
                    }
                    vm->init_script(true);
                    d->gs = g0;
                    *pt = p0;
                    d->map = m0;
                    waiting = false;
                    then = Then::Idle;
                    note_until = 0;
                    dice_low();
                    const std::string got = b_one_step(ab[0], ab[1], x, y, dir);
                    ++n;
                    if (got != want) {
                        ++bad;
                        if (shown++ < 12) printf("   ECL%d/%d %d,%d dir %d: engine%s | original%s\n", ab[0], ab[1], x, y, dir, got.c_str(), want.c_str());
                    }
                }
        dice_usual();
        char what[64];
        snprintf(what, sizeof what, "triggers ECL%d block %d: a step from every square and facing", ab[0], ab[1]);
        expect(P, what, bad == 0 && n > 0, "%d of %d differ", bad, n);
    }
}

static void behave_events()
{
    if (!boot()) {
        expect("events", "boot", false);
        return;
    }
    const char* only = getenv("EVPART");
    auto want = [&](const char* p) { return !only || !strcmp(only, p); };
    if (want("ops")) {
        b_ops();
        if (!boot()) return;
        b_encounter();
    }
    if (want("surprise")) b_surprise();
    if (want("steps")) b_steps();
    if (want("rest")) b_rest();
    if (want("triggers")) b_triggers();
}

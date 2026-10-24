#include "ecl_vm.h"
#include "rules.h"

#include <cstdlib>
#include <new>
#include <cstdio>
#include <cstring>

namespace ecl {

namespace {

constexpr uint16_t kBase = 0x8000;
constexpr int kBudget = 50000;      // instructions a run may take (scripts that never stop)

char unpack_char(int v)
{
    // 1-26 = A-Z, 27-31 = [ \ ] ^ _, 32-63 = ASCII 32-63
    return static_cast<char>(v <= 0x1F ? v + 0x40 : v);
}

// Clock slots (area words 0x4BC6 on) and how far each counts
const uint8_t kClockScale[7] = {10, 10, 6, 24, 30, 12, 0};   // 0 = 256

} // namespace

uint32_t Vm::game_delay() const
{
    int speed = get(0x4BFC) & 0xFF;
    if (speed == 0) speed = 4;
    return static_cast<uint32_t>(speed) * 100;
}

int Vm::sight() const
{
    if (!get(0x4BE6)) return 2;
    int x = s_.x, y = s_.y, n = 0;
    while (n < 2 && h_.wall_type(x, y, s_.dir) == 0) {
        ++n;
        x = (x + (s_.dir == 2 ? 1 : s_.dir == 6 ? -1 : 0)) & 15;
        y = (y + (s_.dir == 4 ? 1 : s_.dir == 0 ? -1 : 0)) & 15;
    }
    return n;
}

void Vm::reset_encounter()
{
    enc_.sprite_on = enc_.pic_on = enc_.pic_due = false;
}

// The monsters as the party sees them: their sprite in the 3D view (0-2
// squares away), and once they are next to the party their picture (not
// while the encounter menu is up). True when the picture should follow
// after a moment, so the sprite is seen first
bool Vm::show_encounter()
{
    const bool dungeon = get(0x4BE6) != 0;
    bool drew_sprite = false;
    if (!enc_.pic_on && dungeon) {
        h_.sprite(enc_.sprite, enc_.distance);
        enc_.sprite_on = drew_sprite = true;
    }
    const uint8_t head = static_cast<uint8_t>(get(0x7EE1) & 0xFF);
    if ((!enc_.pic_on || enc_.head != head) && enc_.distance == 0 && dungeon && !enc_.in_menu) {
        enc_.head = head;
        enc_.pic_on = true;
        if (drew_sprite) {
            enc_.pic_due = true;
            return true;
        }
        h_.picture(enc_.pic, head);
    }
    return false;
}

// The encounter menu's steps: the text for the distance, then the menu
Stop Vm::encounter_step()
{
    if (enc_phase_ == EncPhase::Note) {
        if (!enc_again_) {
            enc_phase_ = EncPhase::None;
            enc_.in_menu = false;
            return Stop::Running;
        }
        enc_phase_ = EncPhase::Text;
    }
    const bool dungeon = get(0x4BE6) != 0;
    if (enc_phase_ == EncPhase::Text) {
        // The text for this distance, else the next one round that has text
        enc_phase_ = EncPhase::Menu;
        const int d = enc_.distance < 0 ? 0 : enc_.distance > 2 ? 2 : enc_.distance;
        const char* t = "";
        for (int k = 0; k < 3 && !*t; ++k) t = enc_text_[(d + k) % 3] ? enc_text_[(d + k) % 3] : "";
        if (*t) {
            text_ = t;
            clear_ = dungeon;
            return wait_for(Wait::Print);
        }
    }
    static const char* const kNear[4] = {"Combat", "Wait", "Flee", "Parlay"};
    static const char* const kFar[4] = {"Combat", "Wait", "Flee", "Advance"};
    const bool near = enc_.distance == 0 || !dungeon;
    n_items_ = 4;
    for (int i = 0; i < 4; ++i) item_[i] = near ? kNear[i] : kFar[i];
    prompt_ = "";
    return wait_for(Wait::Menu);
}

void Vm::advance_clock(int slot, int amount)
{
    if (amount > 0 && slot >= 0 && slot < 5) {
        int m = amount;
        for (int k = 2; k <= slot; ++k) m *= kClockScale[k - 1];
        if (minutes_ < 100000) minutes_ += m;
    }
    for (int carry = amount; slot >= 0 && slot < 7 && carry; ++slot) {
        const int scale = kClockScale[slot] ? kClockScale[slot] : 256;
        const int v = get(static_cast<uint16_t>(0x4BC6 + slot)) + carry;
        set(static_cast<uint16_t>(0x4BC6 + slot), static_cast<uint16_t>(v % scale));
        carry = v / scale;
    }
}

void unpack_string(const uint8_t* p, size_t n, char* out, size_t cap)
{
    size_t o = 0;
    auto put = [&](int v) {
        if (v && o + 1 < cap) out[o++] = unpack_char(v);
    };
    for (size_t i = 0; i + 1 <= n; i += 3) {
        const int b0 = p[i], b1 = i + 1 < n ? p[i + 1] : 0, b2 = i + 2 < n ? p[i + 2] : 0;
        put(b0 >> 2);
        if (i + 1 < n) put(((b0 & 3) << 4) | (b1 >> 4));
        if (i + 2 < n) {
            put(((b1 & 0xF) << 2) | (b2 >> 6));
            put(b2 & 0x3F);
        }
    }
    if (cap) out[o] = 0;
}

// ---- the game state with room for a bigger script ----------------------------

GameState* new_game_state(uint32_t cap)
{
    if (cap < kCodeSize) cap = kCodeSize;
    const size_t bytes = offsetof(GameState, code) + cap;
    void* m = malloc(bytes < sizeof(GameState) ? sizeof(GameState) : bytes);
    if (!m) return nullptr;
    GameState* g = new (m) GameState;
    memset(code_of(*g), 0, cap);
    g->code_cap = cap;
    return g;
}

void free_game_state(GameState* g)
{
    if (!g) return;
    g->~GameState();
    free(g);
}

// ---- memory -----------------------------------------------------------------

static uint16_t word_at(const uint8_t* a, uint32_t off) { return static_cast<uint16_t>(a[off] | a[off + 1] << 8); }
static void word_to(uint8_t* a, uint32_t off, uint16_t v)
{
    a[off] = static_cast<uint8_t>(v);
    a[off + 1] = static_cast<uint8_t>(v >> 8);
}

// ---- Darkness's memory (darkness_facts.md S3, S4) ----------------------------

uint16_t Vm::dk_addr(uint16_t a)
{
    // The engine's own calls name Curse's places; Darkness's scripts use its
    // own (1-0x5FE, the script) and pass through
    if (a < 0x4B00 || (a >= kBase && a < 0xC000)) return a;
    if (a >= 0x4BC6 && a <= 0x4BCC) return static_cast<uint16_t>(0x05 + (a - 0x4BC6));     // the clock
    switch (a) {
    case 0x4BF2: return 0x16;           // the block the party is in
    case 0x4BC5: return 0x13;           // the map loaded
    case 0x4BE5: return 0x02;           // no spells / items' Use in this area
    case 0x4BE6: return 0x22;           // in 3D
    case 0x4BF0: return 0x17;           // the last square
    case 0x4BF1: return 0x18;
    case 0x4BFB: return 0x1A;           // no Area map here
    case 0x4BFC: return 0x12;           // game speed
    case 0x4CF4: return 0x27;           // difficulty (1-5)
    case 0x7EC9: return 0x03;           // the step blocked
    case 0x7ECA: return 0x19;           // Search / Look
    case 0x7ECB: return 0x2E;           // surprise
    case 0x7EC6: return 0x1B;           // the monsters' morale (its fight's morale, GAME.OVR 0xAD85)
    case 0x7EE2: return 0x14;           // COMBAT: the temple
    case 0x7F6D: return 0x28;           // the shop's prices in 16ths
    case 0x7EA8: return 0x30;           // the training hall's classes (SAVE 127, then PROGRAM 0)
    case 0x7ED2: return 0x2B;           // resting's encounters
    case 0x7ED3: return 0x2C;
    case 0x7F3E: return 0x20;           // the party's size
    case 0x7F3F: return 0x29;           // DIVIDE's remainder
    case 0xC04B: return 0x34;           // x, y, facing
    case 0xC04C: return 0x35;
    case 0xC04D: return 0x11;
    case 0xC04E: return 0x32;           // the wall ahead
    case 0xC04F: return 0x2D;           // the square's flags
    }
    return 0;                           // (nothing there)
}

uint16_t Vm::dk_read(uint16_t a, int size) const
{
    if (size == 2) return static_cast<uint16_t>(dk_read(a, 1) | dk_read(static_cast<uint16_t>(a + 1), 1) << 8);
    if (a == 0) return 0;
    if (a <= 0x400) {
        const bool three_d = s_.area1[0x21] != 0;
        switch (a) {
        case 0x10: {                    // a member detecting magic (effect 5)
            for (int i = 0; party_ && i < party_->count; ++i)
                if (party_->m[i].has_affect(5)) return 1;
            return 0;
        }
        case 0x11: return static_cast<uint16_t>(three_d ? s_.dir / 2 : s_.dir);
        case 0x23: return static_cast<uint16_t>(s_.dir);
        case 0x2D: return s_.roof;
        case 0x32: return s_.wall_ahead;
        case 0x34: return static_cast<uint16_t>(s_.x);
        case 0x35: return static_cast<uint16_t>(s_.y);
        }
        return s_.area1[a - 1];
    }
    if (a <= 0x5FE) {
        // the selected character's own record (a loaded monster's: no ext)
        const uint16_t off = static_cast<uint16_t>(a - 0x401);
        if (const uint8_t* mr = sel_rec()) return party::darkness_byte(mr, nullptr, 0, off);
        if (party_ && party_->sel()) {
            const party::Character& c = *party_->sel();
            return party::darkness_byte(c.rec, c.ext, c.n_items, off);
        }
        return 0;
    }
    if (a >= kBase && a < kBase + s_.code_cap) return code_of(s_)[a - kBase];
    return 0;
}

void Vm::dk_write(uint16_t a, uint16_t v, int size)
{
    if (size == 2) {
        dk_write(a, v & 0xFF, 1);
        dk_write(static_cast<uint16_t>(a + 1), v >> 8, 1);
        return;
    }
    const uint8_t b = static_cast<uint8_t>(v);
    if (a == 0) return;
    if (a <= 0x400) {
        switch (a) {
        case 0x11:                      // facing (0-3 in 3D, 0-7 outside)
            s_.dir = s_.area1[0x21] ? (b % 4) * 2 : b & 7;
            s_.moved = true;
            return;
        case 0x34: s_.x = b & 15; s_.moved = true; return;
        case 0x35: s_.y = b & 15; s_.moved = true; return;
        case 0x3A:                      // the frame's style: BORDERS block (4: 0)
            if (b < 5 && b != s_.area1[0x39]) {
                s_.area1[0x39] = b;
                h_.style(b == 4 ? 0 : b);
            }
            return;
        }
        s_.area1[a - 1] = b;
        // (Treasures: a write to 0x41 reads the season's colours, game
        // byte 0x41 - its operand loader ovr 0x7D28 -> o144+0x9F3)
        if (a == 0x41 && set_.tr_ops) h_.season();
        return;
    }
    if (a <= 0x5FE) {
        if (party_ && party_->sel() && sel_mon_ < 0) party::darkness_script_set(*party_->sel(), static_cast<uint16_t>(a - 0x401), b);
        return;
    }
    if (a >= kBase && a < kBase + s_.code_cap) code_of(s_)[a - kBase] = b;
}

void Vm::put(const Op& o, uint16_t v)
{
    if (set_.byte_map) dk_write(o.word(), v, o.code == 1 ? 1 : 2);
    else set(o.word(), v);
}

uint16_t Vm::get(uint16_t a) const
{
    if (set_.byte_map) return a == 0x7EE1 ? 0xFF : dk_read(dk_addr(a), 1);    // (no head pictures)
    if (a >= 0x4B00 && a <= 0x4EFF) return word_at(s_.area1, (a - 0x4B00u) * 2);
    if (a >= 0x7A00 && a <= 0x7BFF) return word_at(s_.table, (a - 0x7A00u) * 2);
    if (a >= 0x7C00 && a <= 0x7FFF) {
        // The selected character's fields
        uint16_t v;
        if (a == 0x7D00 && not_found_) {
            not_found_ = false;                            // LOAD CHARACTER found no one
            return 0;
        }
        const uint16_t off = static_cast<uint16_t>(a - 0x7C00);
        if (const uint8_t* mr = sel_rec()) {
            // a loaded monster LOAD CHARACTER chose (its place: after the party)
            const int at = (party_ ? party_->count : 0) + sel_mon_;
            if (set_.secret_fields ? party::script_value_secret(mr, nullptr, at, off, &v)
                                   : party::script_value(mr, at, off, &v, set_.pool_map))
                return v;
        } else if (party_ && (set_.secret_fields ? party::script_value_secret(*party_, off, &v)
                                                 : party::script_value(*party_, off, &v, set_.pool_map))) {
            return v;
        }
        if (a == 0x7D00) return 0;                         // "no character loaded"
        if (a == 0x7F12) return s_.game_area;
        return word_at(s_.area2, (a - 0x7C00u) * 2);
    }
    if (a >= kBase && a < kBase + s_.code_cap) return code_of(s_)[a - kBase];
    switch (a) {
    case 0x033D: return static_cast<uint16_t>(s_.dir);
    case 0x03DE: return sound_kind_;
    case 0xC04B: return static_cast<uint16_t>(s_.x);
    case 0xC04C: return static_cast<uint16_t>(s_.y);
    case 0xC04D: return static_cast<uint16_t>(s_.dir / 2);
    case 0xC04E: return s_.wall_ahead;
    case 0xC04F: return s_.roof;
    case 0xC059: return view_bytes_[0];
    case 0xC05F: return view_bytes_[1];
    }
    return 0;
}

void Vm::set(uint16_t a, uint16_t v)
{
    if (set_.byte_map) {
        dk_write(dk_addr(a), v, 1);
        return;
    }
    if (a >= 0x4B00 && a <= 0x4EFF) {
        word_to(s_.area1, (a - 0x4B00u) * 2, v);
        return;
    }
    if (a >= 0x7A00 && a <= 0x7BFF) {
        word_to(s_.table, (a - 0x7A00u) * 2, v);
        return;
    }
    if (a >= 0x7C00 && a <= 0x7FFF) {
        word_to(s_.area2, (a - 0x7C00u) * 2, v);
        if (a == 0x7C00 && v == 0) cleared_name_ = true;
        if (a == 0x7D00 && v == 0) cleared_status_ = true;
        const uint16_t off = static_cast<uint16_t>(a - 0x7C00);
        if (uint8_t* mr = sel_rec()) {
            if (a < 0x7EB0) {
                if (set_.secret_fields) party::script_set_secret(mr, off, v);
                else party::script_set(mr, off, v);
            }
        } else if (party_ && a < 0x7EB0) {
            if (set_.secret_fields) party::script_set_secret(*party_, off, v);
            else party::script_set(*party_, off, v);
        }
        if (a == 0x7F12) s_.game_area = static_cast<uint8_t>(v);
        if ((a == 0x7F22 || a == 0x7F24 || a == 0x7F26) && v > 0x80) h_.load_walls(1 + (a - 0x7F22) / 2, v & 0x7F);
        return;
    }
    if (a >= kBase && a < kBase + s_.code_cap) {
        code_of(s_)[a - kBase] = static_cast<uint8_t>(v);
        return;
    }
    switch (a) {
    case 0x03DE: sound_kind_ = v; break;
    case 0xC04B: s_.x = static_cast<int8_t>(v) & 15; s_.moved = true; break;
    case 0xC04C: s_.y = static_cast<int8_t>(v) & 15; s_.moved = true; break;
    case 0xC04D: s_.dir = (v % 4) * 2; s_.moved = true; break;
    case 0xC059: view_bytes_[0] = static_cast<uint8_t>(v); break;     // (seen at the next redraw)
    case 0xC05F: view_bytes_[1] = static_cast<uint8_t>(v); break;
    default: break;
    }
}

// Strings in memory: a character a word (a byte in the script), 0 ends
void Vm::store_string(uint16_t a, const char* t)
{
    const uint16_t name_at = set_.byte_map ? 0x401 : 0x7C00;      // (Darkness: its record's name)
    uint8_t* mr = a == name_at ? sel_rec() : nullptr;
    if (a == name_at && (mr || (party_ && party_->sel()))) {
        // The selected character's name
        uint8_t* r = mr ? mr : party_->sel()->rec;
        size_t n = strlen(t);
        if (n > party::kNameMax) n = party::kNameMax;
        r[0] = static_cast<uint8_t>(n);
        memcpy(r + 1, t, n);
        return;
    }
    for (;; ++t) {
        set(a, static_cast<uint8_t>(*t));
        a = static_cast<uint16_t>(a + 1);
        if (!*t) break;
    }
}

void Vm::read_string(uint16_t a, char* out, size_t cap) const
{
    if (a == (set_.byte_map ? 0x401 : 0x7C00)) {
        const uint8_t* r = sel_rec();
        if (!r && party_ && party_->sel()) r = party_->sel()->rec;
        if (r) {                                // the selected character's name
            size_t n = r[0] < party::kNameMax ? r[0] : party::kNameMax;
            if (cap && n > cap - 1) n = cap - 1;
            if (cap) {
                memcpy(out, r + 1, n);
                out[n] = 0;
            }
            return;
        }
    }
    size_t o = 0;
    for (; o + 1 < cap; ++o, a = static_cast<uint16_t>(a + 1)) {
        const uint16_t c = get(a) & 0xFF;
        if (!c) break;
        out[o] = static_cast<char>(c);
    }
    if (cap) out[o] = 0;
}

// ---- operands -----------------------------------------------------------------

void Vm::add_string(const char* src, size_t n, bool packed)
{
    if (n_slots_ >= kMaxStrings) return;
    char* dst = arena_ + arena_used_;
    const size_t room = sizeof arena_ - arena_used_;
    if (room < 2) return;
    if (packed) {
        unpack_string(reinterpret_cast<const uint8_t*>(src), n, dst, room);
    } else {
        const size_t k = n < room - 1 ? n : room - 1;
        memcpy(dst, src, k);
        dst[k] = 0;
    }
    arena_used_ += strlen(dst) + 1;
    slot_[n_slots_++] = dst;
}

// Reads n operands after position pc_ (the opcode, or the last operand byte
// when continuing a list); pc_ ends on the last byte read + 1
// Pool's addresses where the engine keeps them (ecl::vm_address)
void Vm::to_map(Op& o) const
{
    const uint16_t a = vm_address(set_, o.word());
    o.low = static_cast<uint8_t>(a);
    o.high = static_cast<uint8_t>(a >> 8);
}

// A jump's target: a plain number is a code address too
uint16_t Vm::target(const Op& o) const { return o.code == 2 ? vm_address(set_, o.word()) : o.word(); }

bool Vm::operands(int n, Op* out)
{
    uint32_t p = pc_;
    for (int i = 0; i < n; ++i) {
        Op o{};
        o.code = byte_at(p + 1);
        o.low = byte_at(p + 2);
        p += 2;
        if (o.code == 1 || o.code == 2 || o.code == 3) {
            o.high = byte_at(++p);
            if (o.code != 2) to_map(o);
        } else if (o.code == 0x80) {
            if (p + o.low >= s_.code_cap) return false;
            o.str = n_slots_;
            add_string(reinterpret_cast<const char*>(code_of(s_) + p + 1), o.low, true);
            p += o.low;
        } else if (o.code == 0x81) {
            o.high = byte_at(++p);
            to_map(o);
            o.str = n_slots_;
            char tmp[256];
            read_string(o.word(), tmp, sizeof tmp);
            add_string(tmp, strlen(tmp), false);
        }
        if (p >= s_.code_cap) return false;
        out[i] = o;
    }
    pc_ = p + 1;
    return true;
}

uint16_t Vm::value(const Op& o) const
{
    switch (o.code) {
    case 0: return o.low;
    case 1:
    case 3: return set_.byte_map ? dk_read(o.word(), o.code == 1 ? 1 : 2) : get(o.word());
    case 2:
    case 0x81: return o.word();
    }
    return 0;
}

const char* Vm::string_of(const Op& o) const
{
    if (o.str >= 0 && o.str < n_slots_) return slot_[o.str];
    return "";
}

// ---- running ------------------------------------------------------------------

bool Vm::init_script(bool reload)
{
    uint32_t e[5];
    if (!entries(code_of(s_), s_.code_len, set_, e)) return false;
    for (int i = 0; i < 5; ++i) entry_[i] = static_cast<uint16_t>(e[i] == 0xFFFFFFFFu ? 0 : kBase + e[i]);
    sp_ = 0;
    for (bool& f : flags_) f = false;
    cleared_name_ = cleared_status_ = false;
    sel_mon_ = -1;
    reset_encounter();
    enc_phase_ = EncPhase::None;
    enc_.in_menu = false;
    set(0x7EE1, 0xFF);                 // no head picture
    view_bytes_[0] = 0x41;             // (Pool: the view's colours as each area starts)
    view_bytes_[1] = 0x09;
    set(0x7ED2, 0);
    set(0x7ED3, 0);
    if (set_.byte_map) {
        // Darkness: each block's variables (0x97-0xC8) cleared at its start
        // but in the block a saved game was loaded in; the mode stays
        if (!reload) memset(s_.area1 + 0x96, 0, 50);
        return true;
    }
    if (!reload) {
        for (uint16_t a = 0x4C00; a <= 0x4C20; ++a) set(a, 0);
        for (uint16_t a = 0x7F79; a <= 0x7F82; ++a) set(a, 0);
    }
    set(0x4BE6, 1);                    // in a 3D area unless the script says
    return true;
}

void Vm::stop_script()
{
    stop_ = true;
    sp_ = 0;
    reset_encounter();
}

void Vm::restore_selected()
{
    if (!restore_) return;
    restore_ = false;
    sel_mon_ = -1;
    if (party_ && start_sel_ >= 0 && start_sel_ < party_->count) party_->selected = start_sel_;
}

void Vm::party_size()
{
    if (party_) set(0x7F3E, static_cast<uint16_t>(party_->count));
}

// 0 .. n - 1 (n > 0): every random number the scripts use
uint32_t Vm::rnd(uint32_t n)
{
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
#ifdef CYD_TEST_HOOKS
    if (create::g_die_hook && n > 0) {
        const int v = create::g_die_hook(static_cast<int>(n));
        if (v >= 0) return static_cast<uint32_t>(v) < n ? static_cast<uint32_t>(v) : n - 1;
    }
#endif
    return n ? rng_ % n : 0;
}

uint8_t Vm::roll(int sides, int n)
{
    int t = 0;
    for (int k = 0; k < n; ++k) t += sides > 0 ? static_cast<int>(rnd(static_cast<uint32_t>(sides))) + 1 : 0;
    return static_cast<uint8_t>(t);             // (the games' dice: a byte)
}

// DAMAGE's lines one at a time (each on a line of its own), then "press
// <enter>"; the whole party killed: "The entire party is killed!"
Stop Vm::damage_step()
{
    if (dmg_at_ < 0) return Stop::Running;
    if (dmg_at_ < 2 * n_dmg_) {
        const int k = dmg_at_++;
        text_ = "\n";
        if (k & 1) {
            const DmgLine& l = dmg_[k / 2];
            char nm[20] = {};
            if (party_ && l.who < party_->count) party_->m[l.who].name(nm, sizeof nm);
            if (l.dies) {
                snprintf(dmg_text_, sizeof dmg_text_, "  %s%s", nm, word(kWDies));
                h_.sound(5);                                // (the death sound)
            }
            else snprintf(dmg_text_, sizeof dmg_text_, "  %s%s%d%s", nm, word(kWIsHitFor), l.amount, word(kWPointsOfDamage));
            text_ = dmg_text_;
        }
        clear_ = false;
        return wait_for(Wait::Print);
    }
    if (dmg_killed_ && dmg_at_ == 2 * n_dmg_) {
        ++dmg_at_;
        text_ = word(kWPartyKilled);
        clear_ = true;
        return wait_for(Wait::Print);
    }
    if (dmg_at_ == 2 * n_dmg_ + (dmg_killed_ ? 1 : 0)) {
        ++dmg_at_;
        return wait_for(Wait::Key);
    }
    dmg_at_ = -1;
    if (dmg_killed_) {
        dmg_killed_ = false;
        h_.party_killed();
    }
    return Stop::Running;
}

void Vm::compare(uint16_t a, uint16_t b)
{
    flags_[0] = a == b;
    flags_[1] = a != b;
    flags_[2] = a < b;
    flags_[3] = a > b;
    flags_[4] = a <= b;
    flags_[5] = a >= b;
}

void Vm::skip_next()
{
    Insn in;
    if (decode(code_of(s_), s_.code_len, pc_, set_, in)) pc_ = in.next;
    else ++pc_;
}

Stop Vm::wait_for(Wait w)
{
    wait_ = w;
    return Stop::Waiting;
}

Stop Vm::run(uint16_t address)
{
    if (address < kBase) return Stop::Stopped;
    pc_ = address - kBase;
    stop_ = new_script_ = false;
    wait_ = Wait::None;
    budget_ = kBudget;
    if (party_) start_sel_ = party_->selected;
    return resume();
}

Stop Vm::resume()
{
    if (wait_ == Wait::Combat) {
        monsters_ = false;                              // the fight's monsters are gone
        sel_mon_ = -1;
    }
    wait_ = Wait::None;
    budget_ = kBudget;                                  // (each stretch between waits has its own)
    if (enc_.pic_due) {
        // The monster's picture, after its sprite was seen for a moment
        enc_.pic_due = false;
        h_.picture(enc_.pic, enc_.head);
    }
    if (enc_phase_ != EncPhase::None) {
        const Stop r = encounter_step();
        if (r != Stop::Running) return r;
    }
    if (dmg_at_ >= 0) {
        const Stop r = damage_step();
        if (r != Stop::Running) return r;
    }
    if (xp_pause_) {
        xp_pause_ = false;
        pause_ms_ = game_delay();
        return wait_for(Wait::Pause);
    }
    if (dk_then_ != Wait::None) {
        const Wait w = dk_then_;
        dk_then_ = Wait::None;
        return wait_for(w);
    }
    while (!stop_) {
        if (--budget_ <= 0) {
            h_.log("script ran too long; stopped");
            stop_script();
            return Stop::Error;
        }
        const Stop r = step();
        if (r != Stop::Running) return r;
    }
    return new_script_ ? Stop::NewScript : Stop::Stopped;
}

Stop Vm::answer(int v)
{
    if (enc_phase_ == EncPhase::Menu) {
        // The encounter menu: Combat, Wait, Flee, then Advance (seen from
        // afar) or Parlay (close by / outdoors). What each leads to comes
        // from the script; the result word: 0 the monsters flee, 1 combat,
        // 2 the party flees, 3 parlay
        const bool near = enc_.distance == 0 || !get(0x4BE6);
        int sel = v;
        if (near && sel == 3) sel = 4;
        if (sel < 0 || sel > 4) sel = 1;
        // The party's slowest and fastest movement (no party: 12, a person on
        // foot): every member, out of action too, doubled when hasted, halved
        // when slowed (coab's facts: calc_group_inituative, a byte each)
        int party_min = 12, party_max = 12;
        if (party_ && party_->count) {
            party_min = 255;
            party_max = 0;
            for (int i = 0; i < party_->count; ++i) {
                const party::Character& c = party_->m[i];
                int mv = c.movement();
                if (haste_ && c.has_affect(haste_)) mv = (mv * 2) & 0xFF;
                else if (slow_ && c.has_affect(slow_)) mv >>= 1;
                if (mv < party_min) party_min = mv;
                if (mv > party_max) party_max = mv;
            }
        }
        auto result = [&](int r) {
            set(enc_dest_, static_cast<uint16_t>(r));
            enc_again_ = false;
        };
        auto note = [&](const char* t, bool again) {
            text_ = t;
            clear_ = true;
            enc_again_ = again;
            enc_phase_ = EncPhase::Note;
            return wait_for(Wait::Print);
        };
        auto approach = [&]() {
            if (enc_.distance > 0) {
                --enc_.distance;
                show_encounter();
                enc_again_ = true;
                return true;
            }
            return false;
        };
        enc_again_ = false;
        switch (enc_result_[sel]) {
        case 0:
            if (sel != 2) result(1);
            else result(party_min >= enc_flee_ ? 2 : 1);
            break;
        case 1:
            if (sel == 0) result(1);
            else if (sel == 1) return note(word(kWBothWait), true);
            else if (sel == 2) result(2);
            else if (sel == 3) {
                if (!approach()) return note(word(kWBothWait), true);
            } else if (!approach()) result(3);
            break;
        case 2:
            if (sel == 0 && enc_monster_speed_ <= party_max) {
                result(1);
                break;
            }
            result(0);
            return note(word(kWMonstersFlee), false);
        case 3:
            if (sel == 0) result(1);
            else if (sel == 1 || sel == 3) {
                if (!approach()) return note(word(kWBothWait), true);
            } else if (sel == 2) result(2);
            else if (!approach()) result(3);
            break;
        default:
            if (sel == 0) result(1);
            else if (sel == 2) result(2);
            else if (!approach()) result(3);
            break;
        }
        if (enc_again_) {
            enc_phase_ = EncPhase::Text;
            const Stop r = encounter_step();
            if (r != Stop::Running) return r;
        } else {
            enc_phase_ = EncPhase::None;
            enc_.in_menu = false;
        }
        return resume();
    }
    if (set_.byte_map) {
        // Darkness: the answer where its operand says (its size); YES NO's
        // and the menus' flags
        switch (op_) {
        case 0x15:
        case 0x2B:
        case 0x31: dk_write(dest_, static_cast<uint16_t>(v & 0xFF), dest_size_); break;
        case 0x0F: dk_write(dest_, static_cast<uint16_t>(v), dest_size_); break;
        case 0x22:                      // YES NO: "=" Yes, "<>" No
            for (bool& f : flags_) f = false;
            flags_[v == 0 ? 0 : 1] = true;
            break;
        case 0x39:
            if (party_ && v >= 0 && v < party_->count) party_->selected = v;
            break;
        default: break;
        }
        return resume();
    }
    switch (op_) {
    case 0x15:                          // VERTICAL MENU: the index
    case 0x2B:                          // HORIZONTAL MENU: the index (a byte)
        set(dest_, static_cast<uint16_t>(v & 0xFF));
        break;
    case 0x29:                          // ENCOUNTER MENU
        set(dest_, static_cast<uint16_t>(v));
        break;
    case 0x2C:                          // PARLAY: the value given for that answer
        if (v >= 0 && v < 5) set(dest_, answer_values_[v]);
        break;
    case 0x0F:
        set(dest_, static_cast<uint16_t>(v));
        break;
    case 0x39:                          // WHO: that member is selected
        if (party_ && v >= 0 && v < party_->count) party_->selected = v;
        break;
    default: break;
    }
    return resume();
}

Stop Vm::answer_string(const char* t)
{
    if (op_ == 0x10) store_string(dest_, t && *t ? t : " ");
    return resume();
}

// Darkness's opcodes that differ from Curse's (darkness_facts.md S2), and
// those that write a result (its size from the operand). False: the common
// handler does it.
bool Vm::dk_step(Stop& r)
{
    Op o[kMaxOps];
    auto need = [&](int n) {
        if (operands(n, o)) return true;
        h_.log("bad operands");
        stop_script();
        r = Stop::Error;
        return false;
    };
    // n more operands into o[first..] (the rest read past)
    auto more = [&](int first, int n) {
        for (int k = 0; k < n; ++k) {
            Op t{};
            --pc_;
            if (!operands(1, &t)) return false;
            if (first + k < kMaxOps) o[first + k] = t;
        }
        return true;
    };
    auto size_of = [](const Op& op) { return op.code == 1 || op.code == 0 ? 1 : 2; };
    r = Stop::Running;
    switch (op_) {
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: {                                // ADD SUBTRACT DIVIDE MULTIPLY: op3's size
        if (!need(3)) return true;
        const uint16_t a = value(o[0]), b = value(o[1]);
        uint16_t v = 0;
        if (op_ == 0x04) v = static_cast<uint16_t>(a + b);
        if (op_ == 0x05) v = static_cast<uint16_t>(b - a);
        if (op_ == 0x06) {
            v = b ? static_cast<uint16_t>(a / b) : 0;
            dk_write(0x29, b ? static_cast<uint16_t>(a % b) : 0, 1);
        }
        if (op_ == 0x07) v = static_cast<uint16_t>(a * b);
        put(o[2], v);
        return true;
    }
    case 0x08: {                                // RANDOM: 0 .. v
        if (!need(2)) return true;
        int max = value(o[0]) & 0xFF;
        if (max < 0xFF) ++max;
        put(o[1], static_cast<uint16_t>(max ? rnd(static_cast<uint32_t>(max)) : 0));
        return true;
    }
    case 0x09:                                  // SAVE: op2's size; a string as bytes + 0
        if (!need(2)) return true;
        if (o[0].code < 0x80) put(o[1], value(o[0]));
        else store_string(o[1].word(), string_of(o[0]));
        return true;
    case 0x0C: {                                // SETUP MONSTER: sprite, how far, picture, sides
        if (!need(4)) return true;
        enc_.sprite = static_cast<uint8_t>(value(o[0]));
        enc_.max = value(o[1]) & 0xFF;
        enc_.pic = static_cast<uint8_t>(value(o[2]));
        enc_.sides = static_cast<uint8_t>(value(o[3]));
        enc_.distance = sight();
        if (enc_.distance > enc_.max) enc_.distance = enc_.max;
        if (show_encounter()) {
            pause_ms_ = game_delay();
            r = wait_for(Wait::Pause);
        }
        return true;
    }
    case 0x0F:                                  // INPUT NUMBER
        if (!need(2)) return true;
        dest_ = o[1].word();
        dest_size_ = size_of(o[1]);
        r = wait_for(Wait::Number);
        return true;
    case 0x15: {                                // VERTICAL MENU
        if (!need(3)) return true;
        const int n = value(o[2]) & 0xFF;
        if (!more(3, n)) return true;
        dest_ = o[0].word();
        dest_size_ = size_of(o[0]);
        prompt_ = string_of(o[1]);
        n_items_ = 0;
        for (int i = 0; i < n && 3 + i < kMaxOps && n_items_ < kMaxStrings; ++i) item_[n_items_++] = string_of(o[3 + i]);
        r = wait_for(Wait::ListMenu);
        return true;
    }
    case 0x1D:                                  // PRESS KEY
        if (!need(0)) return true;
        prompt_ = word(kWDkPressKey);
        r = wait_for(Wait::Key);
        return true;
    case 0x1E:                                  // CHECKPARTY: read, nothing done (its program)
        need(6);
        return true;
    case 0x1F: {                                // JOURNAL n: the sentence, then a key
        if (!need(1)) return true;
        const int n = value(o[0]);
        snprintf(dk_text_, sizeof dk_text_, "%s%d%s", word(kWDkJournal), n, word(kWDkJournalEnd));
        h_.journal(n);
        text_ = dk_text_;
        clear_ = false;
        prompt_ = word(kWDkPressKey);
        dk_then_ = Wait::Key;
        r = wait_for(Wait::Print);
        return true;
    }
    case 0x21: {                                // LOAD FILES: map, look
        if (!need(2)) return true;
        const int a = value(o[0]) & 0xFF, b = value(o[1]) & 0xFF;
        if (a < 0x7F && s_.area1[0x21]) {
            set(0x4BC5, static_cast<uint16_t>(a));
            h_.load_map(a);
            set(0x7EC9, 0);
        }
        if (b != 0xFF) h_.load_look(b);
        return true;
    }
    case 0x22:                                  // YES NO
        if (!need(0)) return true;
        dk_items_[0] = "Yes";
        dk_items_[1] = "No";
        {
            // ("Yes No" - its own words when the profile gives them)
            const char* w = word(kWDkYesNo);
            static char yes[8], no[8];
            if (*w && sscanf(w, "%7s %7s", yes, no) == 2) {
                dk_items_[0] = yes;
                dk_items_[1] = no;
            }
        }
        n_items_ = 2;
        item_[0] = dk_items_[0];
        item_[1] = dk_items_[1];
        prompt_ = "";
        r = wait_for(Wait::Menu);
        return true;
    case 0x23:                                  // END: the text window clear, the view back, EXIT
        if (!need(0)) return true;
        if (set_.tr_ops) h_.clear_text();       // (Treasures: the text window empty - DOSBox)
        h_.redraw();
        restore_selected();
        stop_script();
        return true;
    case 0x27: {                                // TREASURE: platinum, gems, jewelry, n items
        if (!need(4)) return true;
        const int n = value(o[3]) & 0xFF;
        if (ground_) {
            ground_->money[4] += value(o[0]);       // (added to what lies there: its program)
            ground_->money[5] += value(o[1]);
            ground_->money[6] += value(o[2]);
        }
        // (the items one at a time: there may be more than kMaxOps - the
        // debug block's 87)
        for (int k = 0; k < n; ++k) {
            Op t{};
            --pc_;
            if (!operands(1, &t)) return true;
            if (ground_) h_.item_row(value(t) & 0xFF, *ground_);
        }
        return true;
    }
    case 0x29:                                  // nothing (its program)
        need(0);
        return true;
    case 0x2A:                                  // GET TABLE: op3's size
        if (!need(3)) return true;
        put(o[2], dk_read(static_cast<uint16_t>(o[0].word() + (value(o[1]) & 0xFF)), size_of(o[2])));
        return true;
    case 0x2B:                                  // HORIZONTAL MENU
    case 0x31: {                                // WHAT MENU: "what do you do?", then the menu
        if (!need(2)) return true;
        const int n = value(o[1]) & 0xFF;
        if (!more(2, n)) return true;
        dest_ = o[0].word();
        dest_size_ = size_of(o[0]);
        n_items_ = 0;
        for (int i = 0; i < n && 2 + i < kMaxOps && n_items_ < kMaxStrings; ++i) item_[n_items_++] = string_of(o[2 + i]);
        prompt_ = "";
        if (op_ == 0x31) {
            text_ = word(kWDkWhatDo);
            clear_ = false;
            dk_then_ = Wait::Menu;
            r = wait_for(Wait::Print);
        } else {
            r = wait_for(Wait::Menu);
        }
        return true;
    }
    case 0x2C: {                                // SOUND: 2-14 an effect, 0x80 + n music
        if (!need(1)) return true;
        const int v = value(o[0]) & 0xFF;
        if (v > 0x7F) {
            char t[40];
            snprintf(t, sizeof t, "MUSIC %d (not in the engine yet)", v & 0x7F);
            h_.log(t);
        } else if (v >= 2 && v <= 14) {
            h_.game_sound(v);
        }
        return true;
    }
    case 0x2D: {                                // CALL 1 / 2 / 3
        if (!need(1)) return true;
        const int w = value(o[0]);
        if (w == 1) {
            h_.redraw();
        } else if (w == 2) {                    // a step ahead
            s_.x = (s_.x + (s_.dir == 2 ? 1 : s_.dir == 6 ? -1 : 0)) & 15;
            s_.y = (s_.y + (s_.dir == 4 ? 1 : s_.dir == 0 ? -1 : 0)) & 15;
            s_.moved = true;
            h_.redraw();
        } else if (w == 3) {                    // the picture's next frame
            h_.anim_step();
            pause_ms_ = game_delay();
            r = wait_for(Wait::Pause);
        }
        return true;
    }
    case 0x2F:
    case 0x30: {                                // AND / OR
        if (!need(3)) return true;
        const uint16_t v = static_cast<uint8_t>(op_ == 0x2F ? (value(o[0]) & value(o[1])) : (value(o[0]) | value(o[1])));
        compare(0, v);
        put(o[2], v);
        return true;
    }
    case 0x35:                                  // SAVE TABLE: op1's size
        if (!need(3)) return true;
        dk_write(static_cast<uint16_t>(o[1].word() + value(o[2])), value(o[0]), size_of(o[0]));
        return true;
    case 0x37: {                                // FIND NPC: a name's place in the party (0xFF none; +0x80 not okay)
        if (!need(2)) return true;
        const char* nm = string_of(o[0]);
        int place = 0xFF;
        for (int i = 0; party_ && i < party_->count && place == 0xFF; ++i) {
            const party::Character& c = party_->m[i];
            char n[20];
            c.name(n, sizeof n);
            if (c.rec[0xF7] >= 0x80 && !strcmp(n, nm)) place = i | (c.health() == party::Okay ? 0 : 0x80);
        }
        dk_write(o[1].word(), static_cast<uint16_t>(place), o[0].code >= 0x80 ? 2 : size_of(o[0]));
        return true;
    }
    case 0x3A:                                  // DELAY: 600 ms
        if (!need(0)) return true;
        pause_ms_ = 600;
        r = wait_for(Wait::Pause);
        return true;
    case 0x3C: {                                // SHOP n (1-11; Treasures 1-16)
        if (!need(1)) return true;
        int n = value(o[0]) & 0xFF;
        if (n < 1 || n > set_.shops) n = 1;
        if (ground_) {
            ground_->clear();
            h_.shop_items(n, *ground_);
        }
        r = wait_for(Wait::Shop);
        return true;
    }
    case 0x41: {                                // EXPERIENCE: who (0 the selected), amount x 100
        if (!need(2)) return true;
        const bool all = value(o[0]) != 0;
        const uint32_t amount = static_cast<uint32_t>(value(o[1])) * 100u;
        for (int i = 0; party_ && i < party_->count; ++i) {
            if (!all && i != party_->selected) continue;
            party::Character& c = party_->m[i];
            if (c.health() != party::Okay) continue;
            int classes = 0;
            for (int k = 0; k < 7; ++k) classes += c.rec[0x109 + k] > 0;
            if (!classes) classes = 1;
            uint32_t xp = c.exp() + amount / static_cast<uint32_t>(classes);
            for (int k = 0; k < 4; ++k) c.rec[0x127 + k] = static_cast<uint8_t>(xp >> (8 * k));
        }
        h_.party_changed();
        return true;
    }
    case 0x42: {                                // Treasures: LOAD 3D - the map, the wall slots' blocks
        if (!set_.tr_ops) return false;
        if (!need(4)) return true;
        const int a = value(o[0]) & 0xFF;
        if (a < 0x7F && s_.area1[0x21]) {
            set(0x4BC5, static_cast<uint16_t>(a));
            h_.load_map(a);
            set(0x7EC9, 0);
        }
        // (each slot its block - one of 3 sets fills the slots from it on;
        // 0 or 0xFF empties the slot: ov43+0xF2D)
        for (int slot = 0; slot < 3; ++slot) {
            const int b = value(o[1 + slot]) & 0xFF;
            h_.load_slot(slot, b == 0 || b == 0xFF ? -1 : b);
        }
        h_.slots_done();
        return true;
    }
    case 0x43:                                  // Treasures: FIND ITEM - type (0 any), the name words
    case 0x44: {                                // DESTROY ITEM: the first such item (ov43+0x1BDC, +0x2764)
        if (!set_.tr_ops) return false;
        if (!need(4)) return true;
        const int type = value(o[0]) & 0xFF;
        // (the three words compared as the program does: the item's 0x2F-0x31
        // against the 4th, 3rd and 2nd operands; the 2nd 0: any words)
        const uint8_t w[3] = {static_cast<uint8_t>(value(o[3])), static_cast<uint8_t>(value(o[2])),
                              static_cast<uint8_t>(value(o[1]))};
        bool found = false;
        for (int i = 0; party_ && i < party_->count && !found; ++i) {
            party::Character& c = party_->m[i];
            for (int k = 0; k < c.n_items && !found; ++k) {
                const uint8_t* it = c.items[k];
                if (type && it[0x2E] != type) continue;
                if (w[2] && (it[0x2F] != w[0] || it[0x30] != w[1] || it[0x31] != w[2])) continue;
                found = true;
                if (op_ == 0x44) {
                    if (it[0x34]) rules::worn(c, k, false);
                    rules::remove_item(c, k);
                    h_.party_changed();
                }
            }
        }
        if (op_ == 0x43) {
            for (bool& f : flags_) f = false;
            flags_[0] = found;
            flags_[1] = !found;
        }
        return true;
    }
    default:
        return false;
    }
}

Stop Vm::step()
{
    arena_used_ = 0;
    n_slots_ = 0;
    if (pc_ >= s_.code_len) {
        h_.log("script ran off its end");
        stop_script();
        return Stop::Error;
    }
    op_ = code_of(s_)[pc_];
    Op o[kMaxOps];
    char line[96];
    auto need = [&](int n) {
        if (!operands(n, o)) {
            h_.log("bad operands");
            stop_script();
            return false;
        }
        return true;
    };
    // A list of n more after `first` operands: as many as o holds, the rest
    // read past (so the next instruction is where it should be); `keep`:
    // that one of the list also into *kept, wherever it is
    auto more = [&](int first, int n, int keep = -1, Op* kept = nullptr) {
        const int fit = n > kMaxOps - first ? kMaxOps - first : n;
        --pc_;
        if (!operands(fit, o + first)) return false;
        if (kept && keep >= 0 && keep < fit) *kept = o[first + keep];
        for (int k = fit; k < n; ++k) {
            Op t{};
            --pc_;
            if (!operands(1, &t)) return false;
            if (kept && k == keep) *kept = t;
        }
        return true;
    };
    auto stub = [&](int n, const char* what) {
        if (!need(n)) return Stop::Error;
        snprintf(line, sizeof line, "%s (not in the engine yet)", what);
        h_.log(line);
        return Stop::Running;
    };

    if (set_.byte_map) {
        Stop r = Stop::Running;
        if (dk_step(r)) return r;
    }
    switch (op_) {
    case 0x00:                                  // EXIT
        ++pc_;
        restore_selected();
        stop_script();
        return Stop::Running;
    case 0x01:                                  // GOTO
        if (!need(1)) return Stop::Error;
        pc_ = target(o[0]) - kBase;
        return Stop::Running;
    case 0x02:                                  // GOSUB
        if (!need(1)) return Stop::Error;
        if (sp_ < 32) stack_[sp_++] = pc_;
        pc_ = target(o[0]) - kBase;
        return Stop::Running;
    case 0x03:                                  // COMPARE
        if (!need(2)) return Stop::Error;
        if (o[0].code >= 0x80 || o[1].code >= 0x80) {
            const int c = strcmp(string_of(o[0]), string_of(o[1]));
            flags_[0] = c == 0;
            flags_[1] = c != 0;
            flags_[2] = c < 0;
            flags_[3] = c > 0;
            flags_[4] = c <= 0;
            flags_[5] = c >= 0;
        } else {
            compare(value(o[0]), value(o[1]));
        }
        return Stop::Running;
    case 0x04:
    case 0x05:
    case 0x06:
    case 0x07: {                                // ADD SUBTRACT DIVIDE MULTIPLY
        if (!need(3)) return Stop::Error;
        const uint16_t a = value(o[0]), b = value(o[1]);
        uint16_t r = 0;
        if (op_ == 0x04) r = static_cast<uint16_t>(a + b);
        if (op_ == 0x05) r = static_cast<uint16_t>(b - a);
        if (op_ == 0x06) {
            r = b ? static_cast<uint16_t>(a / b) : 0;
            set(0x7F3F, b ? static_cast<uint16_t>(a % b) : 0);
        }
        if (op_ == 0x07) r = static_cast<uint16_t>(a * b);
        set(o[2].word(), r);
        return Stop::Running;
    }
    case 0x08: {                                // RANDOM: 0 .. v inclusive
        if (!need(2)) return Stop::Error;
        int max = value(o[0]) & 0xFF;
        if (max < 0xFF) ++max;
        set(o[1].word(), static_cast<uint16_t>(max ? rnd(static_cast<uint32_t>(max)) : 0));
        return Stop::Running;
    }
    case 0x09:                                  // SAVE
        if (!need(2)) return Stop::Error;
        if (o[0].code < 0x80) set(o[1].word(), value(o[0]));
        else store_string(o[1].word(), string_of(o[0]));
        return Stop::Running;
    case 0x0A: {                                // LOAD CHARACTER
        if (!need(1)) return Stop::Error;
        const int v = value(o[0]) & 0xFF, i = v & 0x7F;
        restore_ = true;
        const int members = party_ ? party_->count : 0;
        if (party_ && i < members) {
            party_->selected = i;
            sel_mon_ = -1;
            not_found_ = false;
        } else if (h_.monster_record(i - members)) {
            // past the party: the monsters loaded for the next fight (the
            // original's list goes on into them; coab's facts, LOAD MONSTER)
            sel_mon_ = i - members;
            not_found_ = false;
        } else {
            not_found_ = true;
        }
        if ((v & 0x80) && cleared_name_ && cleared_status_ && party_ && party_->count && sel_mon_ < 0) {
            // The selected member leaves the party
            const int gone = party_->selected;
            if (gone == start_sel_) restore_ = false;
            else if (gone < start_sel_) --start_sel_;       // (the step's member moves up a place)
            party::remove(*party_, gone);
            party_size();
            h_.party_changed();
            cleared_name_ = cleared_status_ = false;
        }
        return Stop::Running;
    }
    case 0x0B:                                  // LOAD MONSTER: monster id, copies, icon block
        if (!need(3)) return Stop::Error;
        monsters_ = true;
        h_.load_monster(value(o[0]) & 0xFF, value(o[1]) & 0xFF, value(o[2]) & 0xFF);
        return Stop::Running;
    case 0x0C: {                                // SETUP MONSTER: sprite, how far, picture
        if (!need(3)) return Stop::Error;
        enc_.sprite = static_cast<uint8_t>(value(o[0]));
        enc_.max = value(o[1]) & 0xFF;
        enc_.pic = static_cast<uint8_t>(value(o[2]));
        enc_.distance = sight();
        if (enc_.distance > enc_.max) enc_.distance = enc_.max;
        if (show_encounter()) {
            pause_ms_ = game_delay();
            return wait_for(Wait::Pause);
        }
        return Stop::Running;
    }
    case 0x0D:                                  // APPROACH: the monsters come a square closer
        ++pc_;
        if (enc_.distance > 0) {
            --enc_.distance;
            if (show_encounter()) {
                pause_ms_ = game_delay();
                return wait_for(Wait::Pause);
            }
        }
        return Stop::Running;
    case 0x0E: {                                // PICTURE
        if (!need(1)) return Stop::Error;
        const int id = value(o[0]) & 0xFF;
        if (id == 0xFF) reset_encounter();
        h_.picture(id, get(0x7EE1) & 0xFF);
        return Stop::Running;
    }
    case 0x0F:                                  // INPUT NUMBER
    case 0x10:                                  // INPUT STRING
        if (!need(2)) return Stop::Error;
        dest_ = o[1].word();
        return wait_for(op_ == 0x0F ? Wait::Number : Wait::String);
    case 0x11:                                  // PRINT
    case 0x12:                                  // PRINTCLEAR
        if (!need(1)) return Stop::Error;
        if (o[0].code >= 0x80) {
            text_ = string_of(o[0]);
        } else {
            snprintf(num_, sizeof num_, "%u", static_cast<unsigned>(value(o[0])));
            text_ = num_;
        }
        clear_ = op_ == 0x12;
        return wait_for(Wait::Print);
    case 0x13:                                  // RETURN
        ++pc_;
        if (sp_ > 0) pc_ = stack_[--sp_];
        else {
            restore_selected();                 // nothing to return to: an EXIT (coab's facts)
            stop_script();
        }
        return Stop::Running;
    case 0x14:                                  // COMPARE AND
        if (!need(4)) return Stop::Error;
        for (bool& f : flags_) f = false;
        if (value(o[0]) == value(o[1]) && value(o[2]) == value(o[3])) flags_[0] = true;
        else flags_[1] = true;
        return Stop::Running;
    case 0x15: {                                // VERTICAL MENU
        if (!need(3)) return Stop::Error;
        const int n = value(o[2]) & 0xFF;
        if (!more(3, n)) return Stop::Error;
        dest_ = o[0].word();
        prompt_ = string_of(o[1]);
        n_items_ = 0;
        for (int i = 0; i < n && 3 + i < kMaxOps && n_items_ < kMaxStrings; ++i) item_[n_items_++] = string_of(o[3 + i]);
        return wait_for(Wait::ListMenu);
    }
    case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B:   // IF
        ++pc_;
        if (!flags_[op_ - 0x16]) skip_next();
        return Stop::Running;
    case 0x1C:                                  // CLEARMONSTERS: and the treasure
        ++pc_;
        monsters_ = false;
        sel_mon_ = -1;
        h_.clear_monsters();
        if (ground_) ground_->clear();
        return Stop::Running;
    case 0x1D: {                                // PARTYSTRENGTH: a byte sum over everyone
        if (!need(1)) return Stop::Error;
        uint8_t sum = 0;
        for (int i = 0; party_ && i < party_->count; ++i) {
            const uint8_t* r = party_->m[i].rec;
            int cur = 0;
            for (int k = 0; k < 7 && !cur; ++k) cur = r[0x109 + k];
            const bool dual = r[0x74] == 7 && cur > r[0xE6];
            const int hp = r[0x1A4], a = r[0x19A] > 60 ? r[0x19A] - 60 : 0, h = r[0x199] > 39 ? r[0x199] - 39 : 0;
            const int mu = r[0x10E] + (dual ? r[0x116] : 0), cl = r[0x109] + (dual ? r[0x111] : 0);
            sum = static_cast<uint8_t>(sum + (hp + 5 * a + 5 * h + 8 * mu + 4 * cl) / 10);
        }
        set(o[0].word(), sum);
        return Stop::Running;
    }
    case 0x1E: {                                // CHECKPARTY: what (0: an effect; a thief skill; movement)
        if (!need(6)) return Stop::Error;
        const uint16_t what = o[0].code == 1 ? o[0].word() : value(o[0]);
        const int w = static_cast<int16_t>(static_cast<uint16_t>(what - 0x7FFF));
        const int fx = value(o[1]) & 0xFF;
        const uint16_t A = o[2].word(), B = o[3].word(), C = o[4].word(), D = o[5].word();
        const int n = party_ ? party_->count : 0;
        if (what == 0) {
            bool found = false;
            for (int i = 0; i < n; ++i)
                if (party_->m[i].has_affect(static_cast<uint8_t>(fx))) found = true;
            set(A, 0);
            set(B, 0);
            set(C, 0);
            set(D, found ? 1 : 0);
        } else if ((w >= 0xA5 && w <= 0xAC) || w == 0x9F) {
            // (as the games compare: signed, so other values write nothing)
            int lo = 0xFF, hi = 0, total = 0;
            for (int i = 0; i < n; ++i) {
                const int v = w == 0x9F ? party_->m[i].rec[0x1A5] : party_->m[i].rec[0xE9 + (w - 0xA4)];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
                total += v;
            }
            set(A, static_cast<uint16_t>(lo));
            set(B, static_cast<uint16_t>(hi));
            set(C, static_cast<uint16_t>(n ? total / n : 0));
            set(D, 0);
        }
        return Stop::Running;
    }
    case 0x20: {                                // NEWECL
        if (!need(1)) return Stop::Error;
        const int block = value(o[0]) & 0xFF;
        set(0x4BF2, s_.script);
        s_.script = static_cast<uint8_t>(block);
        if (!h_.load_script(block, code_of(s_), &s_.code_len) || !init_script()) {
            snprintf(line, sizeof line, "NEWECL %d: script not found", block);
            h_.log(line);
            stop_script();
            return Stop::Error;
        }
        stop_ = true;
        new_script_ = true;
        return Stop::Running;
    }
    case 0x21:                                  // LOAD FILES
    case 0x37: {                                // LOAD PIECES
        if (!need(3)) return Stop::Error;
        const int a = value(o[0]) & 0xFF, b = value(o[1]) & 0xFF, c = value(o[2]) & 0xFF;
        const bool dungeon = get(0x4BE6) != 0;
        if (op_ == 0x21) {
            if (a != 0xFF && a != 0x7F && dungeon) {
                set(0x4BC5, static_cast<uint16_t>(a));
                h_.load_map(a);
                set(0x7EC9, 0);
            }
            if (c != 0xFF && !dungeon) h_.picture(0x79, 0xFF);
        } else if (a == 0x7F) {
            // Pool's program: block 0 into set 1 (Phlan's WALLDEF3 block 0,
            // three parts: all three sets); Curse has no block 0 - none
            h_.load_walls(1, set_.pool_map ? 0 : -1);
        } else if (get(0x4BE7) && get(0x4BE8)) {
            if (a != 0xFF) h_.load_walls(1, a);
            if (c != 0xFF) h_.load_walls(3, c);
        } else {
            h_.load_walls(1, a == 0xFF ? -1 : a);
            h_.load_walls(2, b == 0xFF ? -1 : b);
            h_.load_walls(3, c == 0xFF ? -1 : c);
        }
        return Stop::Running;
    }
    case 0x22: {                                // PARTY SURPRISE: a ranger (or cleric / ranger) in the party
        if (!need(2)) return Stop::Error;
        bool ranger = false;
        for (int i = 0; party_ && i < party_->count; ++i)
            if (party_->m[i].rec[0x75] == 4 || party_->m[i].rec[0x75] == 10) ranger = true;
        set(o[0].word(), ranger ? 1 : 0);
        set(o[1].word(), 0);
        return Stop::Running;
    }
    case 0x23: {                                // SURPRISE: two d6 against (d + 2 - a) and (b + 2 - c)
        if (!need(4)) return Stop::Error;
        const int a = value(o[0]) & 0xFF, b = value(o[1]) & 0xFF, c = value(o[2]) & 0xFF, dd = value(o[3]) & 0xFF;
        const int r1 = roll(6, 1), r2 = roll(6, 1);
        int result = 0;
        if (r1 <= dd + 2 - a) result = 1;               // the party surprised
        if (r2 <= b + 2 - c) result = 2;                // the monsters (the games: this wins over both)
        // ... and nothing more: the original writes the result to address
        // 0x2CB, which its memory map drops (the listing's sub_2771E and
        // cmd_table01 / sub_30723: below 0x4B00 and not one of its special
        // addresses), so the fight's surprise word (0x7ECB) is the scripts'
        // own to set
        (void)result;
        return Stop::Running;
    }
    case 0x24:                                  // COMBAT
        ++pc_;
        if (!monsters_) {
            // No monsters: a shop or temple the script opened, or the
            // treasure after a fight (Secret: first its vault)
            if (set_.vault_word && get(set_.vault_word) == 1) {
                set(set_.vault_word, 0);
                return wait_for(Wait::Vault);
            }
            if (get(0x7F6C) == 1) {
                set(0x7F6C, 0);
                return wait_for(Wait::Shop);
            }
            if (get(0x7EE2) == 1) {
                set(0x7EE2, 0);
                return wait_for(Wait::Temple);
            }
            return wait_for(Wait::Treasure);
        }
        return wait_for(Wait::Combat);
    case 0x25:                                  // ON GOTO
    case 0x26: {                                // ON GOSUB
        if (!need(2)) return Stop::Error;
        const int i = value(o[0]) & 0xFF, n = value(o[1]) & 0xFF;
        Op to{};
        if (!more(2, n, i, &to)) return Stop::Error;
        if (i < n) {
            if (op_ == 0x26 && sp_ < 32) stack_[sp_++] = pc_;
            pc_ = target(to) - kBase;
        }
        return Stop::Running;
    }
    case 0x27: {                                // TREASURE: 7 coin amounts, ITEM<area> block
        if (!need(8)) return Stop::Error;
        if (ground_) {
            for (int m = 0; m < 7; ++m) ground_->money[m] = value(o[m]);
            const int block = value(o[7]) & 0xFF;
            if (block < 0x80) h_.load_items(block, *ground_);
            else if (block != 0xFF) h_.random_items(block - 0x80, *ground_);
        }
        return Stop::Running;
    }
    case 0x28: {                                // ROB: who (0 the selected, else all), % of money, item chance
        if (!need(3)) return Stop::Error;
        const int who = value(o[0]) & 0xFF, pct = value(o[1]) & 0xFF, chance0 = value(o[2]) & 0xFF;
        if (!party_ || !party_->count) return Stop::Running;
        for (int i = 0; i < party_->count; ++i) {
            if (who == 0 && i != party_->selected) continue;
            party::Character& c = party_->m[i];
            for (int m = 0; m < 7; ++m) {
                const int at = 0xFB + m * 2;
                const long have = c.rec[at] | c.rec[at + 1] << 8;
                const long left = pct >= 100 ? 0 : have * (100 - pct) / 100;
                c.rec[at] = static_cast<uint8_t>(left);
                c.rec[at + 1] = static_cast<uint8_t>(left >> 8);
            }
            // Each item at the chance (cut for heavy items, and from then on)
            int ch = chance0;
            for (int k = 0; k < c.n_items;) {
                const int wt = c.items[k][0x37] | c.items[k][0x38] << 8;
                if (wt > 255) ch = ch > 90 ? ch - 90 : 0;
                else if (wt > 24) ch = ch > 50 ? ch - 50 : 0;
                if (roll(100, 1) <= ch) {
                    if (c.items[k][0x34]) rules::worn(c, k, false);
                    for (int j = k; j + 1 < c.n_items; ++j) memcpy(c.items[j], c.items[j + 1], party::kItemSize);
                    --c.n_items;
                    memset(c.items[c.n_items], 0, party::kItemSize);
                } else {
                    ++k;
                }
            }
        }
        h_.party_changed();
        return Stop::Running;
    }
    case 0x29: {                                // ENCOUNTER MENU
        // sprite, how far, picture, result word, 5 results, 3 texts (near,
        // middle, far), the speed the party needs to flee, the monsters' speed
        if (!need(14)) return Stop::Error;
        enc_.sprite = static_cast<uint8_t>(value(o[0]));
        enc_.max = value(o[1]) & 0xFF;
        enc_.pic = static_cast<uint8_t>(value(o[2]));
        enc_dest_ = o[3].word();
        for (int i = 0; i < 5; ++i) enc_result_[i] = static_cast<uint8_t>(value(o[4 + i]));
        for (int i = 0; i < 3; ++i) enc_text_[i] = o[9 + i].code >= 0x80 ? string_of(o[9 + i]) : "";
        enc_flee_ = static_cast<uint8_t>(value(o[12]));
        enc_monster_speed_ = static_cast<uint8_t>(value(o[13]));
        enc_.in_menu = true;
        enc_.distance = sight();
        if (enc_.distance > enc_.max) enc_.distance = enc_.max;
        show_encounter();
        enc_phase_ = EncPhase::Text;
        return encounter_step();
    }
    case 0x2A:                                  // GETTABLE
        if (!need(3)) return Stop::Error;
        set(o[2].word(), get(static_cast<uint16_t>(o[0].word() + (value(o[1]) & 0xFF))));
        return Stop::Running;
    case 0x2B: {                                // HORIZONTAL MENU
        if (!need(2)) return Stop::Error;
        const int n = value(o[1]) & 0xFF;
        if (!more(2, n)) return Stop::Error;
        dest_ = o[0].word();
        n_items_ = 0;
        for (int i = 0; i < n && 2 + i < kMaxOps && n_items_ < kMaxStrings; ++i) item_[n_items_++] = string_of(o[2 + i]);
        prompt_ = "";
        return wait_for(Wait::Menu);
    }
    case 0x2C: {                                // PARLAY
        if (!need(6)) return Stop::Error;
        for (int i = 0; i < 5; ++i) answer_values_[i] = value(o[i]);
        dest_ = o[5].word();
        static const char* const kItems[5] = {"Haughty", "Sly", "Nice", "Meek", "Abusive"};
        n_items_ = 5;
        for (int i = 0; i < 5; ++i) item_[i] = kItems[i];
        prompt_ = "";
        return wait_for(Wait::Menu);
    }
    case 0x2D: {                                // CALL
        if (!need(1)) return Stop::Error;
        const uint16_t w = o[0].word();
        if (w == set_.redraw_call) {
            h_.redraw();
        } else if (w == 0x6803) {
            // The event picture's next frame, then the game's delay
            h_.anim_step();
            int speed = get(0x4BFC) & 0xFF;
            if (speed == 0) speed = 4;
            pause_ms_ = static_cast<uint32_t>(speed) * 100;
            return wait_for(Wait::Pause);
        } else if (w == set_.sound_call) {
            h_.sound(sound_kind_ == 10 ? 0x0B : 0x0A);         // a step; 10: the fireball's roar
        } else if (w == 0xC018) {
            // The wall type ahead (worked out anyway)
        } else if (w == 0xC01E) {
            s_.x = (s_.x + (s_.dir == 2 ? 1 : s_.dir == 6 ? -1 : 0)) & 15;
            s_.y = (s_.y + (s_.dir == 4 ? 1 : s_.dir == 0 ? -1 : 0)) & 15;
            s_.moved = true;
        } else if (!set_.pool_map || w == 0x8000 || w == 0x8001) {
            // (Pool's program does nothing for addresses it doesn't know;
            // 0x8000 / 0x8001 are its duels)
            snprintf(line, sizeof line, "CALL %04X (not in the engine yet)", w);
            h_.log(line);
        }
        return Stop::Running;
    }
    case 0x2E: {                                // DAMAGE: flags / attacks, dice, sides, bonus, save type / to-hit
        if (!need(5)) return Stop::Error;
        const int f = value(o[0]) & 0xFF, n = value(o[1]) & 0xFF, sd = value(o[2]) & 0xFF;
        const int p = value(o[3]) & 0xFF, t = value(o[4]) & 0xFF;
        if (!party_ || !party_->count) return Stop::Running;
        const int before = party_->selected;
        n_dmg_ = 0;
        auto hurt = [&](int i, int dmg) {
            party::Character& c = party_->m[i];
            if (c.health() == party::Dead) return;
            if (n_dmg_ < kMaxDamageLines)
                dmg_[n_dmg_++] = DmgLine{static_cast<uint8_t>(i), static_cast<uint8_t>(dmg), dmg > c.hp() + 10};
            const int hp = c.hp();
            int over = 0, now = hp - dmg;
            if (now < 0) {
                over = -now;
                now = 0;
            }
            int st = c.health();
            if (over > 9 || (now == 0 && st == party::Animated)) st = party::Dead;
            else if (over > 0) st = party::Dying;
            else if (now == 0) st = party::Unconscious;
            c.rec[0x195] = static_cast<uint8_t>(st);
            if (st == party::Okay || st == party::Animated) {
                c.rec[0x1A4] = static_cast<uint8_t>(now);
            } else {
                c.rec[0x1A4] = 0;
                c.rec[0x196] = 0;
            }
        };
        auto saves = [&](int i, int type, int bonus) {
            const party::Character& c = party_->m[i];
            const int r = roll(20, 1);
            if (r == 1) return false;
            if (r == 20) return true;
            return r + bonus + static_cast<int8_t>(c.rec[0x186]) >= c.rec[0xDF + (type > 4 ? 4 : type)];
        };
        int dmg = roll(sd, n) + p;
        const int count = party_->count;
        const int random = (f & 0x40) ? 0 : roll(count, 1) - 1;
        if (f & 0x80) {
            const int bonus = f & 0x1F, type = t & 7;
            auto one = [&](int i, bool save, int ty) {
                if (!save || !saves(i, ty, bonus) || (f & 0x10)) hurt(i, dmg & 0xFF);
            };
            if (f & 0x40) {
                for (int i = 0; i < count; ++i) one(i, !(f & 0x20), type);
            } else if (t & 0x80) {
                one(party_->selected, type != 0, type - 1);
            } else {
                one(random, true, type);
            }
        } else {
            for (int k = 0; k < f; ++k) {
                const int i = roll(count, 1) - 1;
                const party::Character& c = party_->m[i];
                int r = roll(20, 1);
                if (r == 20) r = 100;
                if (r > 1 && r + t > c.rec[0x19A]) hurt(i, dmg & 0xFF);
                dmg = roll(sd, n) + p;
            }
        }
        bool any = false;
        for (int i = 0; i < count; ++i)
            if (party_->m[i].in_combat()) any = true;
        party_->selected = before;
        h_.party_changed();
        dmg_killed_ = !any;
        dmg_at_ = 0;
        if (!any) stop_script();
        return damage_step();
    }
    case 0x2F:                                  // AND
    case 0x30: {                                // OR
        if (!need(3)) return Stop::Error;
        const uint16_t r = static_cast<uint8_t>(op_ == 0x2F ? (value(o[0]) & value(o[1])) : (value(o[0]) | value(o[1])));
        compare(0, r);                          // (the original: 0 against the result)
        set(o[2].word(), r);
        return Stop::Running;
    }
    case 0x31:                                  // SPRITE OFF
        ++pc_;
        h_.picture(0xFF, 0xFF);
        return Stop::Running;
    case 0x32:                                  // FIND ITEM: anyone carries one of that type
    case 0x3F: {                                // FIND SPECIAL: the selected character has that effect
        if (!need(1)) return Stop::Error;
        const int v = value(o[0]) & 0xFF;
        bool found = false;
        if (party_ && party_->count) {
            if (op_ == 0x32) {
                for (int i = 0; i < party_->count && !found; ++i)
                    for (int k = 0; k < party_->m[i].n_items; ++k)
                        if (party_->m[i].items[k][0x2E] == v) found = true;
            } else {
                found = party_->sel()->has_affect(static_cast<uint8_t>(v));
            }
        }
        for (bool& f : flags_) f = false;
        flags_[0] = found;
        flags_[1] = !found;
        return Stop::Running;
    }
    case 0x33:                                  // PRINT RETURN
        ++pc_;
        text_ = "\n";
        clear_ = false;
        return wait_for(Wait::Print);
    case 0x34:                                  // ECL CLOCK
        if (set_.sizes[0x34] == 1) {
            // Pool's: one operand, the clock's slot 1 (its program's handler)
            if (!need(1)) return Stop::Error;
            advance_clock(1, value(o[0]) & 0xFF);
            return Stop::Running;
        }
        if (!need(2)) return Stop::Error;
        advance_clock(value(o[1]) & 0xFF, value(o[0]) & 0xFF);
        return Stop::Running;
    case 0x35:                                  // SAVE TABLE
        if (!need(3)) return Stop::Error;
        set(static_cast<uint16_t>(o[1].word() + value(o[2])), value(o[0]));
        return Stop::Running;
    case 0x36: {                                // ADD NPC: MON<area> block, morale
        if (!need(2)) return Stop::Error;
        const int id = value(o[0]) & 0xFF, morale = value(o[1]) & 0xFF;
        if (party_ && party_->count < party::kMaxParty && h_.add_npc(id) && party_->sel()) {
            party_->sel()->rec[0xF7] = static_cast<uint8_t>(0x80 | (morale >> 1));
            party_size();
            h_.party_changed();
        }
        return Stop::Running;
    }
    case 0x38: {                                // PROGRAM
        if (!need(1)) return Stop::Error;
        const int v = value(o[0]) & 0xFF;
        restore_selected();
        if (v == 0) return wait_for(Wait::PartyMenu);      // the party menu, then on
        if (v == 9) {                                       // the camp, then EXIT
            stop_script();
            return wait_for(Wait::Camp);
        }
        if (set_.tr_ops && (v == 3 || v == 4))             // (Treasures: its ovl 0x74 +0x2A75 / +0x292C)
            return wait_for(v == 3 ? Wait::Luck : Wait::ShipRest);
        if (v == 3 && set_.program_kills) {                 // the party is killed
            stop_script();
            h_.party_killed();
            return Stop::Running;
        }
        if (v == 8) {                                       // the game won: the ending
            if (set_.program_ends) stop_script();           // (Pool's script goes on after it)
            return wait_for(Wait::Won);
        }
        return Stop::Running;                               // (other values: nothing)
    }
    case 0x39:                                  // WHO: "<prompt> Select" over the party list
        if (!need(1)) return Stop::Error;
        if (!party_ || !party_->count) return Stop::Running;
        prompt_ = string_of(o[0]);
        return wait_for(Wait::Who);
    case 0x3A: {                                // DELAY: the game's delay (speed x 0.1 s)
        ++pc_;
        int speed = get(0x4BFC) & 0xFF;
        if (speed == 0) speed = 4;
        pause_ms_ = static_cast<uint32_t>(speed) * 100;
        return wait_for(Wait::Pause);
    }
    case 0x3B: {                                // SPELL: who has it memorized (place, member)
        if (!need(3)) return Stop::Error;
        const int sp = value(o[0]) & 0xFF;
        int place = 0xFF, who = 0;
        if (party_ && party_->count) {
            who = party_->count - 1;
            for (int i = 0; i < party_->count && place == 0xFF; ++i)
                for (int k = 0; k < 0x65; ++k)
                    if (party_->m[i].rec[0x1F + k] == sp) {
                        place = k + 1 <= 100 ? k + 1 : 0xFF;
                        who = i;
                        break;
                    }
        } else {
            place = 1;
        }
        set(o[1].word(), static_cast<uint16_t>(place));
        set(o[2].word(), static_cast<uint16_t>(who));
        return Stop::Running;
    }
    case 0x3C: return stub(1, "PROTECTION");    // copy protection: skipped (Tom)
    case 0x3D:                                  // CLEAR BOX
        ++pc_;
        h_.clear_box();
        return Stop::Running;
    case 0x3E:                                  // DUMP: the selected member leaves the party
        ++pc_;
        if (party_ && party_->count) {
            party::remove(*party_, party_->selected);
            start_sel_ = party_->selected;
            party_size();
            h_.party_changed();
        }
        return Stop::Running;
    case 0x40: {                                // DESTROY ITEMS: every item of that type, everyone's
        if (!need(1)) return Stop::Error;
        const int type = value(o[0]) & 0xFF;
        for (int i = 0; party_ && i < party_->count; ++i) {
            party::Character& c = party_->m[i];
            for (int k = 0; k < c.n_items;) {
                if (c.items[k][0x2E] != type) {
                    ++k;
                    continue;
                }
                if (c.items[k][0x34]) rules::worn(c, k, false);
                for (int j = k; j + 1 < c.n_items; ++j) memcpy(c.items[j], c.items[j + 1], party::kItemSize);
                --c.n_items;
                memset(c.items[c.n_items], 0, party::kItemSize);
            }
        }
        h_.party_changed();
        return Stop::Running;
    }
    case 0x41:                                  // Secret: EXPERIENCE (GAME.OVR 0x43FF)
        if (set_.xp_award) {
            if (!need(2)) return Stop::Error;
            const bool all = (value(o[0]) & 0xFF) != 0;
            const uint16_t amount = value(o[1]);
            // Each its share: the amount / the classes it has a level in, to
            // those whose record 0x1A7 (Curse's 0x196) is set
            auto award = [&](party::Character& c) {
                int n = 0;
                for (int k = 0; k < 7; ++k) n += c.level(k) > 0;
                if (!n || !c.rec[0x196]) return;
                const uint32_t e = c.exp() + amount / n;
                for (int i = 0; i < 4; ++i) c.rec[0x127 + i] = static_cast<uint8_t>(e >> (8 * i));
            };
            if (party_ && party_->count) {
                if (all) {
                    for (int i = 0; i < party_->count; ++i) award(party_->m[i]);
                    snprintf(xp_text_, sizeof xp_text_, "%s", word(kWPartyGainsXp));
                } else {
                    party::Character& c = *party_->sel();
                    char nm[20] = {};
                    c.name(nm, sizeof nm);
                    for (size_t n = strlen(nm); n > 0 && nm[n - 1] == ' '; --n) nm[n - 1] = 0;
                    award(c);
                    snprintf(xp_text_, sizeof xp_text_, "%s%s%s", word(kWCongrats), nm, word(kWGainsXp));
                }
                h_.party_changed();
            }
            // its words in the text area (cleared), then the game's delay
            text_ = xp_text_;
            clear_ = true;
            xp_pause_ = true;
            return wait_for(Wait::Print);
        }
        [[fallthrough]];
    default: {
        Insn in;
        if (decode(code_of(s_), s_.code_len, pc_, set_, in)) {
            snprintf(line, sizeof line, "opcode %02X passed over", op_);
            h_.log(line);
            pc_ = in.next;
            return Stop::Running;
        }
        snprintf(line, sizeof line, "unknown opcode %02X at %04X", op_, static_cast<unsigned>(pc_ + kBase));
        h_.log(line);
        stop_script();
        return Stop::Error;
    }
    }
}

} // namespace ecl

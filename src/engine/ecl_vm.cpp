#include "ecl_vm.h"

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

// ---- memory -----------------------------------------------------------------

static uint16_t word_at(const uint8_t* a, uint32_t off) { return static_cast<uint16_t>(a[off] | a[off + 1] << 8); }
static void word_to(uint8_t* a, uint32_t off, uint16_t v)
{
    a[off] = static_cast<uint8_t>(v);
    a[off + 1] = static_cast<uint8_t>(v >> 8);
}

uint16_t Vm::get(uint16_t a) const
{
    if (a >= 0x4B00 && a <= 0x4EFF) return word_at(s_.area1, (a - 0x4B00u) * 2);
    if (a >= 0x7A00 && a <= 0x7BFF) return word_at(s_.table, (a - 0x7A00u) * 2);
    if (a >= 0x7C00 && a <= 0x7FFF) {
        // The selected character's fields
        uint16_t v;
        if (party_ && party::script_value(*party_, static_cast<uint16_t>(a - 0x7C00), &v)) return v;
        if (a == 0x7D00) return 0;                         // "no character loaded"
        if (a == 0x7F12) return s_.game_area;
        return word_at(s_.area2, (a - 0x7C00u) * 2);
    }
    if (a >= kBase && a < kBase + kCodeSize) return s_.code[a - kBase];
    switch (a) {
    case 0x033D: return static_cast<uint16_t>(s_.dir);
    case 0xC04B: return static_cast<uint16_t>(s_.x);
    case 0xC04C: return static_cast<uint16_t>(s_.y);
    case 0xC04D: return static_cast<uint16_t>(s_.dir / 2);
    case 0xC04E: return s_.wall_ahead;
    case 0xC04F: return s_.roof;
    }
    return 0;
}

void Vm::set(uint16_t a, uint16_t v)
{
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
        if (a == 0x7F12) s_.game_area = static_cast<uint8_t>(v);
        if ((a == 0x7F22 || a == 0x7F24 || a == 0x7F26) && v > 0x80) h_.load_walls(1 + (a - 0x7F22) / 2, v & 0x7F);
        return;
    }
    if (a >= kBase && a < kBase + kCodeSize) {
        s_.code[a - kBase] = static_cast<uint8_t>(v);
        return;
    }
    switch (a) {
    case 0xC04B: s_.x = static_cast<int8_t>(v) & 15; s_.moved = true; break;
    case 0xC04C: s_.y = static_cast<int8_t>(v) & 15; s_.moved = true; break;
    case 0xC04D: s_.dir = (v % 4) * 2; s_.moved = true; break;
    default: break;
    }
}

// Strings in memory: a character a word (a byte in the script), 0 ends
void Vm::store_string(uint16_t a, const char* t)
{
    for (;; ++t) {
        set(a, static_cast<uint8_t>(*t));
        a = static_cast<uint16_t>(a + 1);
        if (!*t) break;
    }
}

void Vm::read_string(uint16_t a, char* out, size_t cap) const
{
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
        } else if (o.code == 0x80) {
            if (p + o.low >= kCodeSize) return false;
            o.str = n_slots_;
            add_string(reinterpret_cast<const char*>(s_.code + p + 1), o.low, true);
            p += o.low;
        } else if (o.code == 0x81) {
            o.high = byte_at(++p);
            o.str = n_slots_;
            char tmp[256];
            read_string(o.word(), tmp, sizeof tmp);
            add_string(tmp, strlen(tmp), false);
        }
        if (p >= kCodeSize) return false;
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
    case 3: return get(o.word());
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
    if (!entries(s_.code, s_.code_len, set_, e)) return false;
    for (int i = 0; i < 5; ++i) entry_[i] = static_cast<uint16_t>(e[i] == 0xFFFFFFFFu ? 0 : kBase + e[i]);
    sp_ = 0;
    for (bool& f : flags_) f = false;
    reset_encounter();
    enc_phase_ = EncPhase::None;
    enc_.in_menu = false;
    set(0x7EE1, 0xFF);                 // no head picture
    set(0x7ED2, 0);
    set(0x7ED3, 0);
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
    if (decode(s_.code, s_.code_len, pc_, set_, in)) pc_ = in.next;
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
    return resume();
}

Stop Vm::resume()
{
    wait_ = Wait::None;
    if (enc_.pic_due) {
        // The monster's picture, after its sprite was seen for a moment
        enc_.pic_due = false;
        h_.picture(enc_.pic, enc_.head);
    }
    if (enc_phase_ != EncPhase::None) {
        const Stop r = encounter_step();
        if (r != Stop::Running) return r;
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
        // No party yet: its slowest and fastest movement taken as 12
        // (a person on foot) until characters exist
        const int party_min = 12, party_max = 12;
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
            else if (sel == 1) return note("Both sides wait.", true);
            else if (sel == 2) result(2);
            else if (sel == 3) {
                if (!approach()) return note("Both sides wait.", true);
            } else if (!approach()) result(3);
            break;
        case 2:
            if (sel == 0 && enc_monster_speed_ <= party_max) {
                result(1);
                break;
            }
            result(0);
            return note("The monsters flee.", false);
        case 3:
            if (sel == 0) result(1);
            else if (sel == 1 || sel == 3) {
                if (!approach()) return note("Both sides wait.", true);
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
    default: break;
    }
    return resume();
}

Stop Vm::answer_string(const char* t)
{
    if (op_ == 0x10) store_string(dest_, t && *t ? t : " ");
    return resume();
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
    op_ = s_.code[pc_];
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
    auto more = [&](int first, int n) {       // a list of n more after `first` operands
        --pc_;
        if (n > kMaxOps - first) n = kMaxOps - first;
        return operands(n, o + first);
    };
    auto stub = [&](int n, const char* what) {
        if (!need(n)) return Stop::Error;
        snprintf(line, sizeof line, "%s (not in the engine yet)", what);
        h_.log(line);
        return Stop::Running;
    };

    switch (op_) {
    case 0x00:                                  // EXIT
        ++pc_;
        stop_script();
        return Stop::Running;
    case 0x01:                                  // GOTO
        if (!need(1)) return Stop::Error;
        pc_ = o[0].word() - kBase;
        return Stop::Running;
    case 0x02:                                  // GOSUB
        if (!need(1)) return Stop::Error;
        if (sp_ < 32) stack_[sp_++] = pc_;
        pc_ = o[0].word() - kBase;
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
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        set(o[1].word(), static_cast<uint16_t>(max ? rng_ % static_cast<uint32_t>(max) : 0));
        return Stop::Running;
    }
    case 0x09:                                  // SAVE
        if (!need(2)) return Stop::Error;
        if (o[0].code < 0x80) set(o[1].word(), value(o[0]));
        else store_string(o[1].word(), string_of(o[0]));
        return Stop::Running;
    case 0x0A: return stub(1, "LOAD CHARACTER");
    case 0x0B:                                  // LOAD MONSTER (monsters themselves: combat, to come)
        monsters_ = true;
        return stub(3, "LOAD MONSTER");
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
        else stop_script();
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
        if (ground_) ground_->clear();
        return Stop::Running;
    case 0x1D:                                  // PARTYSTRENGTH
        if (!need(1)) return Stop::Error;
        set(o[0].word(), 0);
        return Stop::Running;
    case 0x1E: {                                // CHECKPARTY
        if (!need(6)) return Stop::Error;
        for (int i = 2; i < 6; ++i) set(o[i].word(), 0);
        h_.log("CHECKPARTY (no party yet)");
        return Stop::Running;
    }
    case 0x20: {                                // NEWECL
        if (!need(1)) return Stop::Error;
        const int block = value(o[0]) & 0xFF;
        set(0x4BF2, s_.script);
        s_.script = static_cast<uint8_t>(block);
        if (!h_.load_script(block, s_.code, &s_.code_len) || !init_script()) {
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
            h_.load_walls(1, -1);
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
    case 0x22:                                  // PARTY SURPRISE
        if (!need(2)) return Stop::Error;
        set(o[0].word(), 0);
        set(o[1].word(), 0);
        return Stop::Running;
    case 0x23: return stub(4, "SURPRISE");
    case 0x24:                                  // COMBAT
        ++pc_;
        if (!monsters_) {
            // No monsters: a shop or temple the script opened, or the
            // treasure after a fight
            if (get(0x7F6C) == 1) {
                set(0x7F6C, 0);
                return wait_for(Wait::Shop);
            }
            if (get(0x7EE2) == 1) {
                set(0x7EE2, 0);
                return wait_for(Wait::Temple);
            }
            h_.log("treasure after a fight (not in the engine yet)");
            return Stop::Running;
        }
        h_.log("COMBAT (not in the engine yet)");
        text_ = "(Combat isn't in the engine yet.)";
        clear_ = false;
        return wait_for(Wait::Print);
    case 0x25:                                  // ON GOTO
    case 0x26: {                                // ON GOSUB
        if (!need(2)) return Stop::Error;
        const int i = value(o[0]) & 0xFF, n = value(o[1]) & 0xFF;
        if (!more(2, n)) return Stop::Error;
        if (i < n && 2 + i < kMaxOps) {
            if (op_ == 0x26 && sp_ < 32) stack_[sp_++] = pc_;
            pc_ = o[2 + i].word() - kBase;
        }
        return Stop::Running;
    }
    case 0x27: {                                // TREASURE: 7 coin amounts, ITEM<area> block
        if (!need(8)) return Stop::Error;
        if (ground_) {
            for (int m = 0; m < 7; ++m) ground_->money[m] = value(o[m]);
            const int block = value(o[7]) & 0xFF;
            if (block < 0x80) h_.load_items(block, *ground_);
            else if (block != 0xFF) h_.log("TREASURE: random items (not in the engine yet)");
        }
        return Stop::Running;
    }
    case 0x28: return stub(3, "ROB");
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
        if (w == 0x2E10) {
            h_.redraw();
        } else if (w == 0x6803) {
            // The event picture's next frame, then the game's delay
            h_.anim_step();
            int speed = get(0x4BFC) & 0xFF;
            if (speed == 0) speed = 4;
            pause_ms_ = static_cast<uint32_t>(speed) * 100;
            return wait_for(Wait::Pause);
        } else if (w == 0xC01E) {
            s_.x = (s_.x + (s_.dir == 2 ? 1 : s_.dir == 6 ? -1 : 0)) & 15;
            s_.y = (s_.y + (s_.dir == 4 ? 1 : s_.dir == 0 ? -1 : 0)) & 15;
            s_.moved = true;
        } else {
            snprintf(line, sizeof line, "CALL %04X (not in the engine yet)", w);
            h_.log(line);
        }
        return Stop::Running;
    }
    case 0x2E: return stub(5, "DAMAGE");
    case 0x2F:                                  // AND
    case 0x30: {                                // OR
        if (!need(3)) return Stop::Error;
        const uint16_t r = static_cast<uint8_t>(op_ == 0x2F ? (value(o[0]) & value(o[1])) : (value(o[0]) | value(o[1])));
        compare(r, 0);
        set(o[2].word(), r);
        return Stop::Running;
    }
    case 0x31:                                  // SPRITE OFF
        ++pc_;
        h_.picture(0xFF, 0xFF);
        return Stop::Running;
    case 0x32:                                  // FIND ITEM
    case 0x3F:                                  // FIND SPECIAL
        if (!need(1)) return Stop::Error;
        for (bool& f : flags_) f = false;
        flags_[1] = true;                       // no party: never found
        return Stop::Running;
    case 0x33:                                  // PRINT RETURN
        ++pc_;
        text_ = "\n";
        clear_ = false;
        return wait_for(Wait::Print);
    case 0x34:                                  // ECL CLOCK
        if (!need(2)) return Stop::Error;
        advance_clock(value(o[1]) & 0xFF, value(o[0]) & 0xFF);
        return Stop::Running;
    case 0x35:                                  // SAVE TABLE
        if (!need(3)) return Stop::Error;
        set(static_cast<uint16_t>(o[1].word() + value(o[2])), value(o[0]));
        return Stop::Running;
    case 0x36: return stub(2, "ADD NPC");
    case 0x38: {                                // PROGRAM
        if (!need(1)) return Stop::Error;
        const int v = value(o[0]) & 0xFF;
        if (v == 0) return wait_for(Wait::PartyMenu);      // the party menu, then on
        snprintf(line, sizeof line, "PROGRAM %d (not in the engine yet)", v);
        h_.log(line);
        if (v == 3 || v == 8 || v == 9) stop_script();
        return Stop::Running;
    }
    case 0x39: return stub(1, "WHO");
    case 0x3A: {                                // DELAY: the game's delay (speed x 0.1 s)
        ++pc_;
        int speed = get(0x4BFC) & 0xFF;
        if (speed == 0) speed = 4;
        pause_ms_ = static_cast<uint32_t>(speed) * 100;
        return wait_for(Wait::Pause);
    }
    case 0x3B:                                  // SPELL
        if (!need(3)) return Stop::Error;
        set(o[1].word(), 0xFF);
        set(o[2].word(), 0);
        return Stop::Running;
    case 0x3C: return stub(1, "PROTECTION");    // copy protection: skipped (Tom)
    case 0x3D:                                  // CLEAR BOX
        ++pc_;
        h_.clear_box();
        return Stop::Running;
    case 0x3E:                                  // DUMP
        ++pc_;
        return Stop::Running;
    case 0x40: return stub(1, "DESTROY ITEMS");
    default: {
        Insn in;
        if (decode(s_.code, s_.code_len, pc_, set_, in)) {
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

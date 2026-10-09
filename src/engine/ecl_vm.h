// The ECL script machine: runs the games' area scripts (Curse first).
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// Behaviour learned from coab (Curse of the Azure Bonds); own code.
//
// Memory the scripts see (16-bit addresses):
//   0x4B00-0x4EFF  area data, a word each (0x4BC5 map block, 0x4BC6-0x4BCC
//                  the clock, 0x4BE6 in a 3D area, 0x4BF0 / 0x4BF1 the last
//                  square, 0x4BF2 the last script block, 0x4BFB area view
//                  blocked, 0x4BFC game speed, 0x4BFD / 0x4BFE outdoor /
//                  indoor sky colour, 0x4C00-0x4C20 per-script variables)
//   0x7A00-0x7BFF  a table, a word each
//   0x7C00-0x7FFF  more game data, a word each; character fields when a
//                  party exists (none yet: 0x7D00 = "no character")
//                  0x7EC9 >= 0xFF: the party doesn't move this step
//                  0x7ECA search flags, 0x7EE1 head picture, 0x7F12 area,
//                  0x7F22 / 24 / 26 > 0x80: load wall set 1 / 2 / 3
//   0x8000-0x9DFF  the script itself, a byte each
//   0xC04B / 0xC04C / 0xC04D  party x, y, facing (0-3 = N E S W)
//   0xC04E / 0xC04F  wall type ahead, the square's roof flags
// Operands: code 0 = a byte, 2 = a word, 1 / 3 = memory at an address,
// 0x80 = a packed string (3 bytes = four 6-bit characters, 1-26 = A-Z,
// 32-63 as ASCII, 0 skipped), 0x81 = a string in memory.
//
// The machine runs until the script stops (EXIT / RETURN with nothing to
// return to), switches script (NEWECL) or needs the player (text being
// printed, a menu, a pause). The front end then shows it and calls resume()
// or answer(). Everything that needs a party (combat, treasure, checks on
// characters) is logged and passed over for now.
#pragma once

#include <cstddef>
#include <cstdint>

#include "ecl.h"

namespace ecl {

constexpr uint32_t kCodeSize = 0x1E00;
constexpr int kMaxStrings = 16;
constexpr int kMaxInput = 40;       // INPUT STRING: characters the player may type

struct GameState {
    uint8_t  area1[0x800] = {};
    uint8_t  area2[0x800] = {};
    uint8_t  table[0x400] = {};
    uint8_t  code[kCodeSize] = {};
    uint32_t code_len = 0;
    uint8_t  script = 0;            // the ECL block running
    uint8_t  game_area = 2;         // ECL<n>, GEO<n> ... files
    int      x = 7, y = 13, dir = 0;  // dir 0, 2, 4, 6
    uint8_t  wall_ahead = 0, roof = 0;
    bool     moved = false;          // a script set the position
};

// What the machine asks the front end for
enum class Wait : uint8_t {
    None,
    Print,        // text(): print it (clear first if clear()), then resume()
    Menu,         // items: a menu on the menu line, answer(index)
    ListMenu,     // prompt + items as a list in the text area, answer(index)
    Number,       // answer(number), up to 65535 (typed on the menu line)
    String,       // answer_string(text), up to kMaxInput characters
    Pause,        // pause_ms(), then resume()
};

enum class Stop : uint8_t { Running, Waiting, Stopped, NewScript, Error };

class Host {
public:
    virtual ~Host() = default;
    // Loads ECL block `block` of the current area into code (kCodeSize max,
    // without its 2-byte header). False if it isn't there.
    virtual bool load_script(int block, uint8_t* code, uint32_t* len) = 0;
    virtual void load_map(int geo_block) = 0;
    virtual void load_walls(int set, int block) = 0;    // block < 0: none
    virtual void picture(int id, int head) = 0;         // id 0xFF: back to the view
    virtual void redraw() = 0;                          // the 3D view again (a picture goes), position
    virtual void clear_box() { redraw(); }              // the exploring frame again, the picture kept
    virtual void anim_step() {}                         // the event picture's next frame (CALL 6803)
    // Encounters: the wall type on side `dir` of square (x, y) (0 = none),
    // to see how far the party can see; and a monster's sprite (SPRITn
    // block `id`) drawn in a freshly drawn 3D view, `distance` 0-2 away
    virtual int wall_type(int x, int y, int dir) { (void)x; (void)y; (void)dir; return 1; }
    virtual void sprite(int id, int distance) { (void)id; (void)distance; }
    virtual void log(const char* what) = 0;
};

class Vm {
public:
    Vm(GameState& s, Host& h, const OpSet& set) : s_(s), h_(h), set_(set) {}

    // Reads the loaded block's 5 entry points and resets the machine's
    // per-script state (as the games do on a new script).
    bool init_script(bool reload = false);
    uint16_t entry(int i) const { return entry_[i]; }   // 0 step, 1 search, 2 pre-camp, 3 camp, 4 first

    Stop run(uint16_t address);
    Stop resume();
    Stop answer(int value);
    Stop answer_string(const char* text);

    Wait wait() const { return wait_; }
    const char* text() const { return text_; }
    bool clear() const { return clear_; }
    int items() const { return n_items_; }
    const char* item(int i) const { return i >= 0 && i < n_items_ ? item_[i] : ""; }
    const char* prompt() const { return prompt_; }
    uint32_t pause_ms() const { return pause_ms_; }
    bool new_script() const { return new_script_; }

    uint16_t get(uint16_t addr) const;
    void set(uint16_t addr, uint16_t v);
    bool flag(int i) const { return flags_[i]; }
    uint16_t pc() const { return pc_; }
    // Game time: adds `amount` to clock slot `slot` (0x4BC6 + slot) with
    // carries (slot 1 = minutes, 2 = ten minutes, 3 = hours ...)
    void advance_clock(int slot, int amount);

private:
    struct Op {
        uint8_t code, low, high;
        uint16_t word() const { return static_cast<uint16_t>(low | high << 8); }
        int str = -1;               // string slot
    };
    bool operands(int n, Op* out);
    uint16_t value(const Op& o) const;
    const char* string_of(const Op& o) const;
    uint8_t byte_at(uint32_t p) const { return p < kCodeSize ? s_.code[p] : 0; }
    Stop step();
    void skip_next();
    void compare(uint16_t a, uint16_t b);
    void store_string(uint16_t addr, const char* text);
    void read_string(uint16_t addr, char* out, size_t cap) const;
    void stop_script();
    Stop wait_for(Wait w);
    void add_string(const char* src, size_t n, bool packed);
    // Encounters (SETUP MONSTER, APPROACH, ENCOUNTER MENU)
    int  sight() const;                 // squares the party can see ahead (0-2)
    bool show_encounter();              // true: the picture is due after a short pause
    void reset_encounter();
    Stop encounter_step();              // the encounter menu, after a wait
    uint32_t game_delay() const;

    GameState& s_;
    Host& h_;
    const OpSet& set_;
    uint16_t entry_[5] = {};
    uint32_t pc_ = 0;               // offset in the code
    uint32_t stack_[32];
    int      sp_ = 0;
    bool     flags_[6] = {};
    bool     stop_ = false, new_script_ = false;
    Wait     wait_ = Wait::None;
    uint16_t dest_ = 0;             // where an answer goes
    uint8_t  op_ = 0;               // the instruction waiting
    uint16_t answer_values_[8] = {};
    int      budget_ = 0;           // instructions left before giving up (endless loops)
    uint32_t rng_ = 0x2545F491;

    // String operands of the instruction being run
    char     arena_[2048];
    size_t   arena_used_ = 0;
    char*    slot_[kMaxStrings] = {};
    int      n_slots_ = 0;
    char     num_[8] = {};

    struct Encounter {
        bool sprite_on = false, pic_on = false, in_menu = false, pic_due = false;
        uint8_t sprite = 0, pic = 0, head = 0xFF;
        int distance = 0, max = 0;
    } enc_;
    // The encounter menu in progress: what it waits for next
    enum class EncPhase : uint8_t { None, Text, Menu, Note };
    EncPhase enc_phase_ = EncPhase::None;
    uint16_t enc_dest_ = 0;
    uint8_t  enc_result_[5] = {};
    uint8_t  enc_flee_ = 0, enc_monster_speed_ = 0;
    const char* enc_text_[3] = {};
    bool     enc_again_ = false;

    const char* text_ = "";
    bool clear_ = false;
    const char* item_[kMaxStrings] = {};
    int n_items_ = 0;
    const char* prompt_ = "";
    uint32_t pause_ms_ = 0;
};

// Unpacks a packed ECL string (n bytes) into out.
void unpack_string(const uint8_t* p, size_t n, char* out, size_t cap);

} // namespace ecl

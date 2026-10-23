// The games' text: Pascal strings read from the player's program files,
// text windows that wrap words the way the games do, and the menu line.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
//
// Text window (behaviour learned from coab, Curse of the Azure Bonds): text
// is printed into a rectangle of cells one character at a time. A "word" is
// any leading punctuation, the letters up to a space or punctuation, the
// punctuation after them and one space. A word that doesn't fit (its
// trailing space included) goes to the start of the next line, and spaces
// at the start of a line are dropped. When the rectangle is full and text
// is left, the game asks for a key, clears the rectangle and goes on from
// its top. The cursor stays where the text ended, so the next text goes on
// from there.
//
// Menu line (row 24): words whose first letter is a capital (or digit) -
// "Area Cast View" - are the choices. Capitals and digits print in the
// highlight colour, the rest in the normal colour, and the chosen word in
// reverse (highlight background, black letters). The font prints lower
// case as capitals, so only the colours show which letters are keys.
#pragma once

#include <cstddef>
#include <cstdint>

#include "dax.h"
#include "exepack.h"
#include "font.h"
#include "picture.h"

namespace text {

// ---- Strings from the game's files ----------------------------------------

constexpr size_t kMaxString = 256;   // length byte + up to 255 characters

// A Pascal string (length byte, then the characters) at pos. False if it
// runs past the end, is empty, or holds bytes that aren't printable ASCII.
bool read_pascal(dax::ByteSource& src, uint32_t pos, char* out, size_t cap);
// The same from the unpacked image of an EXEPACK program.
bool read_pascal(dax::ByteSource& exe, const exepack::Info& info, uint32_t pos, char* out, size_t cap);

// A game's words: GAME.OVR's, and (Pool of Radiance) the few its program
// keeps in a resident code segment - offsets marked kInProgram are offsets
// in the program's unpacked image (read_at unpacks 256 bytes there once,
// so read_pascal's two reads cost one unpacking); others are GAME.OVR's
constexpr uint32_t kInProgram = 0x80000000u;
// A word made of two: the game profile's joined[at & 0xFFFF] (Secret's "is "
// + an effect's name) - the front end puts the two together
constexpr uint32_t kJoined = 0x40000000u;
// A word in the game's names file (Treasures: its effect names, BORD.DAX
// block 9, copied to the card's _CYD folder by the front end with effect 0's
// room first - the name of effect n at 38 x n); the front end reads it
constexpr uint32_t kInNames = 0x20000000u;
class Words : public dax::ByteSource {
public:
    Words(dax::ByteSource& ovr, dax::ByteSource* exe, const exepack::Info* info) : ovr_(ovr), exe_(exe), info_(info) {}
    size_t read_at(uint32_t pos, uint8_t* buf, size_t n) override;
    uint32_t size() const override { return ovr_.size(); }
private:
    dax::ByteSource&       ovr_;
    dax::ByteSource*       exe_;
    const exepack::Info*   info_;
    uint8_t                win_[kMaxString];
    uint32_t               win_at_ = 0, win_len_ = 0;
};

// ---- Text windows ----------------------------------------------------------

struct Region {
    int8_t x0, y0, x1, y1;    // cells, inclusive
    // Lines line_h pixels apart from y_off below the top (Treasures' text
    // window: 9 apart from 2 - five lines in rows 17-22; DOSBox)
    uint8_t line_h = 8, y_off = 0;
    // The last line's row (rows count from y0, one a line)
    int last_row() const { return y0 + ((y1 - y0 + 1) * 8 - y_off) / line_h - 1; }
    int line_y(int row) const { return y0 * 8 + y_off + (row - y0) * line_h; }
};
// The game's text areas: under the 3D view / pictures, its last two rows,
// and the combat panel on the right.
constexpr Region kTextArea{1, 17, 38, 22};
constexpr Region kTextAreaLow{1, 21, 38, 22};
constexpr Region kCombatPanel{23, 1, 38, 21};

enum class State : uint8_t { Idle, Writing, PageFull, Done };
// Gateway's line breaks: a word fits when its letters do (its trailing space
// may fall past the window's last column, not drawn); off: the space must fit too
void set_hanging_space(bool on);

struct Writer {
    Region      r{1, 17, 38, 22};
    int         col = 1, row = 17;      // where the next character goes
    uint8_t     fg = 15;
    const char* s = nullptr;            // not owned: keep it alive while writing
    int         len = 0, pos = 0;
    int         unit_end = 0;           // end of the word being printed
    State       state = State::Idle;
    // Treasures: "%" + a hex digit in the text sets the colour from there on
    // (its img 0x6765: no columns taken; begin() turns it off)
    bool        codes = false;
};

// Starts printing s into r. clear_area: clear r and start at its top left;
// otherwise go on from the cursor (if it is inside r).
void begin(Writer& w, pic::Canvas& c, const char* s, const Region& r, uint8_t fg, bool clear_area);

// Prints up to n more characters (n < 0: as many as fit). Returns the state:
// Writing (more to print), PageFull (call next_page() after the player
// taps), or Done.
State step(Writer& w, pic::Canvas& c, const font::Font& f, int n);

// After PageFull: clears the region and goes on from its top.
void next_page(Writer& w, pic::Canvas& c);

// Clears a rectangle of cells to black.
void clear(pic::Canvas& c, const Region& r);

// ---- Menu line -------------------------------------------------------------

constexpr int kMenuRow = 24;
// Taps for the menu line count from the row above it (the frame's bottom
// row, or combat's status line) - one 8-pixel row is a thin target (Tom).
constexpr int kMenuTapTop = (kMenuRow - 1) * 8;
constexpr int kMaxItems = 20;

struct MenuColors {
    uint8_t highlight = 15, normal = 10, prompt = 13;   // the games' usual menu colours
    bool    plain_sel = false;      // Treasures: the chosen word white on black, not black on white
};

// A menu on two rows (Pools of Darkness's fight menu, "Move Aim Use Cast
// Turn Guard Quick@Delay Bandage View Speed End" - DS 0x2F8C): '@' in the
// choices ends row 23's; the rest goes on row 24
constexpr char kRowBreak = '@';
struct Menu {
    char    s[72] = {};            // the choices, e.g. "Area Cast View"
    char    prompt[41] = {};       // printed before them, in the prompt colour
    int     count = 0;
    int8_t  start[kMaxItems] = {}; // first character of each choice (its key)
    int8_t  end[kMaxItems] = {};   // its last character
    int     selected = 0;
    bool    marked = false;        // (Gateway's '~' marks: only each choice's first letter lit)
    uint8_t prompt_fg = 0;         // the prompt's colour when not the usual (Gateway's questions: 14)
};

// Splits s into choices: each starts at a capital letter or digit.
void build(Menu& m, const char* prompt, const char* s);
// Draws the menu line (row 24), clearing the rest of the row.
void draw(pic::Canvas& c, const font::Font& f, const Menu& m, const MenuColors& col = MenuColors{});
// The choice under canvas column col on the menu row (its trailing space
// counts, so targets have no gaps), or -1.
int hit(const Menu& m, int col);
// The same on a two-row menu: row 23 (the first) or 24; a one-row menu's
// choices are row 24's
int hit_row(const Menu& m, int col, int row);
// Whether the menu takes two rows (rows 23 and 24)
bool two_rows(const Menu& m);
// Where choice k is drawn: its first cell's column and row
void item_cell(const Menu& m, int k, int* col, int* row);
// The key a choice stands for (its first character).
char key(const Menu& m, int item);

} // namespace text

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

// ---- Text windows ----------------------------------------------------------

struct Region {
    int8_t x0, y0, x1, y1;    // cells, inclusive
};
// The game's text areas: under the 3D view / pictures, its last two rows,
// and the combat panel on the right.
constexpr Region kTextArea{1, 17, 38, 22};
constexpr Region kTextAreaLow{1, 21, 38, 22};
constexpr Region kCombatPanel{23, 1, 38, 21};

enum class State : uint8_t { Idle, Writing, PageFull, Done };

struct Writer {
    Region      r{1, 17, 38, 22};
    int         col = 1, row = 17;      // where the next character goes
    uint8_t     fg = 15;
    const char* s = nullptr;            // not owned: keep it alive while writing
    int         len = 0, pos = 0;
    int         unit_end = 0;           // end of the word being printed
    State       state = State::Idle;
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
};

struct Menu {
    char    s[41] = {};            // the choices, e.g. "Area Cast View"
    char    prompt[41] = {};       // printed before them, in the prompt colour
    int     count = 0;
    int8_t  start[kMaxItems] = {}; // first character of each choice (its key)
    int8_t  end[kMaxItems] = {};   // its last character
    int     selected = 0;
};

// Splits s into choices: each starts at a capital letter or digit.
void build(Menu& m, const char* prompt, const char* s);
// Draws the menu line (row 24), clearing the rest of the row.
void draw(pic::Canvas& c, const font::Font& f, const Menu& m, const MenuColors& col = MenuColors{});
// The choice under canvas column col on the menu row (its trailing space
// counts, so targets have no gaps), or -1.
int hit(const Menu& m, int col);
// The key a choice stands for (its first character).
char key(const Menu& m, int item);

} // namespace text

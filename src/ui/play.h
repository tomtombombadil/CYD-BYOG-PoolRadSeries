// Play Test (milestones 4-5): Curse run by its own area scripts from the
// player's files. It starts at the games' party menu (Load Saved Game -
// the GOG release comes with a sample party - BEGIN Adventuring); then
// the opening, events, text, menus and pictures as the party walks.
// What needs more of the party than its list (combat, treasure, checks)
// is passed over and logged.
#pragma once

#include <cstdint>

#include "engine/games.h"
#include "engine/geo.h"
#include "engine/picture.h"

namespace play {

bool available(games::Game g);

// Loads the shared pieces and starts a new game. nullptr = ready, else why
// not. Draws into c.
const char* open(const char* data_dir, games::Game g, pic::Canvas& c);
void close();

enum class Act : uint8_t { TurnLeft, TurnRight, TurnAround, Forward, StepLeft, StepRight, Area, Look };

// Draws the whole game screen.
void draw(pic::Canvas& c);
// A key; false if it does nothing now. While text or a one-choice menu
// waits, any key goes on.
bool act(Act a, pic::Canvas& c);
// A tap on the canvas (pixel x, y): menu words, list lines, "press a key".
void tap(int x, int y, pic::Canvas& c);
// Moves printing text and pauses on. Call often.
void tick(uint32_t now_ms, pic::Canvas& c);

// Esc: back out of a question the Play Test asked (Load Which Game);
// false if there is none (the viewer leaves the Play Test).
bool back(pic::Canvas& c);
// Exit to DOS was chosen on the party menu: true once.
bool exit_requested();

// Typing: the game asks for a number or a line of text (INPUT NUMBER /
// STRING). The front end shows a keyboard while input() != None and sends
// keys: 'A'-'Z', '0'-'9', ' ', punctuation, '\b' = delete, '\n' = done.
enum class Input : uint8_t { None, Number, Text };
Input input();
void input_key(char k, pic::Canvas& c);

// The game mentioned a journal entry ('J') or tavern tale ('T') and now
// waits for the player: true once, with what to show.
bool journal_request(char* kind, int* number);

// The journal entries ('J') and tavern tales ('T') the game has mentioned
// so far, in the order heard (the Menu's Journal list)
int journal_seen_count();
bool journal_seen(int i, char* kind, int* number);

// Canvas rows changed since the last call (y1 == 0: none).
void take_dirty(int& y0, int& y1);

// For the companion panel
const geo::Map* map();
int pos_x();
int pos_y();
int dir();
void describe(char* line1, char* line2, int cap);

} // namespace play

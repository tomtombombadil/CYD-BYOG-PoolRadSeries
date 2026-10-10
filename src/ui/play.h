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

// Loads the shared pieces and shows the party menu. nullptr = ready, else
// why not. Draws into c. cache_dir: the engine's own folder for this game
// (_CYD/<folder>; what it keeps beside saved games), or nullptr.
const char* open(const char* data_dir, games::Game g, pic::Canvas& c, const char* cache_dir = nullptr);
void close();

enum class Act : uint8_t { TurnLeft, TurnRight, TurnAround, Forward, StepLeft, StepRight, Area, Look };

// Draws the whole game screen.
void draw(pic::Canvas& c);
// A key; false if it does nothing now. While text or a one-choice menu
// waits, any key goes on.
bool act(Act a, pic::Canvas& c);
// A tap on the canvas (pixel x, y): menu words, list lines, "press a key".
void tap(int x, int y, pic::Canvas& c);
// The tap highlight (Tom, 2026-10-09): call before tap(). Lights up what
// the tap will act on; true when the canvas changed (rows *y0..*y1 to
// show). When it was lit already it blanks it first: show that, then call
// tap_highlight_blink (lit again: show that too). tap_highlight_end after
// tap() puts it back unless something was drawn there (marks it dirty).
bool tap_highlight(int x, int y, pic::Canvas& c, int* y0, int* y1);
bool tap_highlight_blink(pic::Canvas& c);
void tap_highlight_end(pic::Canvas& c);
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
// True while a fight is on screen: colours 0 and 8 swap (the games' combat
// palette)
bool fight_colours();

// The game's sound (Settings: 1 Tandy, 2 PC speaker, 3 Off) and its volume
// 0-255; the Menu's sound test plays one of the game's sound effects (the
// games' numbers, engine/sound.h)
void set_sound(uint8_t mode, uint8_t volume);
uint8_t sound_mode();
void sound_test(int id);

// For the companion panel
const geo::Map* map();
int pos_x();
int pos_y();
int dir();
void describe(char* line1, char* line2, int cap);

} // namespace play

// An on-screen keyboard for the engine's own screens (WiFi password, mail
// account): the typed text at the top, four rows of ten keys (letters,
// digits, symbols on a second page), then Shift, #+=, Space, Delete, Done.
// The header's back key cancels. (The games' own questions use the Play
// Test's keyboard on the game screen - play::input.)
#pragma once

#include <cstddef>

#include "ui.h"

namespace keyboard {

constexpr size_t kMax = 96;      // the longest text it takes

// title: the header; prompt: shown before the text ("Password: ");
// initial: the text to start from
void open(const char* title, const char* prompt, const char* initial, size_t max_len = kMax);
void draw();

enum class Result : unsigned char { Typing, Done, Cancel };
Result tap(const ui::Tap& t);

const char* text();

} // namespace keyboard

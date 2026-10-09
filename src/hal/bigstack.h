// Deep work on a stack of its own (v0.22.0): the PDF / JPEG code (the
// card scan's journals, the journal book) runs several calls deep and
// needs ~16 KB of stack; loop() keeps the usual 8 KB, so those 8 KB are
// only taken while such work runs (Tom: every KB for the games).
#pragma once

#include <cstddef>

// Runs fn(ctx) on a task with `stack` bytes of stack and waits until it's
// done. False (fn not run) when there's no memory for that stack.
bool run_on_big_stack(void (*fn)(void*), void* ctx, size_t stack = 16 * 1024);

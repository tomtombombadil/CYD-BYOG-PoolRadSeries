// What the last crash was (S3 v0.103.0: Tom's board rebooted at Play with
// nothing to go on). A panic writes a core dump to the flash's coredump
// partition (the Arduino libraries have it on); at the next boot its
// summary - the task, the program counter and the backtrace - becomes a
// line for the Restarts log, and the dump is erased so it's told once.
// The addresses are decoded on the PC with the build's .elf (CI keeps it:
// the elf-<env> artifact) and addr2line.
#pragma once

#include <cstddef>

// The summary of the dump left by the last crash in out (a line, no
// newline); false when there is none
bool crash_summary(char* out, size_t cap);

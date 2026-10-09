#pragma once
#include <FS.h>
inline bool sd_begin() { return true; }
inline fs::FS& sd_fs() { static fs::FS f; return f; }

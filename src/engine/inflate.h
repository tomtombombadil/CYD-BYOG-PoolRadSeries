// A small streaming raw DEFLATE (RFC 1951) decoder.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// Based on the whole-buffer decoder in Tom's CYD-Classic-Games
// (src/games/common/inflate.*, MIT), changed to stream: bytes come in one at
// a time and go out one at a time through a 32 KB window the caller gives,
// so big outputs (a 256 x 256 PNG is 262 KB unpacked) never sit in RAM.
#pragma once

#include <cstddef>
#include <cstdint>

namespace inflate {

constexpr size_t kWindow = 32768;

struct Input {
    virtual ~Input() = default;
    virtual int byte() = 0;                // next byte, or -1 at the end
};

struct Output {
    virtual ~Output() = default;
    virtual bool put(uint8_t b) = 0;       // false: stop (bad data, or had enough)
};

// Decodes raw DEFLATE from in to out. window: kWindow bytes of the caller's.
// True when the final block ended cleanly.
bool raw(Input& in, Output& out, uint8_t* window);

} // namespace inflate

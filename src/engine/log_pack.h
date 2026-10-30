// Packing the logs into a QR code (Settings -> Logs -> Send Logs). Plain
// C++ (host-tested in tools/host_tests/test_log_pack.cpp).
//
// Copied from Tom's CYD-Classic-Games (MIT, src/ui/log_pack.*), only the
// URL changed. The text is compressed with raw DEFLATE (RFC 1951; fixed
// Huffman codes and a 4 KB window - logs repeat a lot) and written in
// "base43": QR's alphanumeric characters minus space and '%' (both awkward
// in a URL), 2 bytes -> 3 characters. Alphanumeric QR mode stores 5.5 bits
// a character, so this packs ~97 % as densely as raw bytes while staying
// plain URL text. The web page (web/l/index.html) reverses it with the
// browser's own DecompressionStream("deflate-raw").
#pragma once

#include <cstddef>
#include <cstdint>

namespace logpack {

// The QR code is this URL followed by the packed text
constexpr const char* kUrl = "https://tomtombombadil.github.io/CYD-GoldBoxGameEngine/l/#";
constexpr const char* kAlphabet = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ$*+-./:";   // 43
constexpr uint8_t kFormat = 1;          // first byte of the packed data

// Raw DEFLATE of `in`. Returns the bytes written, or 0 if `cap` is too small
// (or out of memory for the match tables, ~16 KB, freed on return).
size_t deflate(const uint8_t* in, size_t n, uint8_t* out, size_t cap);

// Base43 text of `in` (NUL-terminated). Needs cap >= n * 3 / 2 + 2.
// Returns the length, 0 if cap is too small.
size_t base43(const uint8_t* in, size_t n, char* out, size_t cap);

// The whole packet: kFormat, then deflate(text), as base43. Returns the
// length of `out` or 0 (also when the deflated text doesn't fit `work`).
size_t pack(const char* text, size_t n, uint8_t* work, size_t work_cap, char* out, size_t cap);

} // namespace logpack

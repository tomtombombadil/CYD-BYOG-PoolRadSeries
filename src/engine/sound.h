// The games' sound effects: their own sound driver re-done - a small byte-
// code sequencer ticking at 236.69 Hz (the games reprogram the PC's timer to
// that), four voices for the PC speaker and four for the Tandy 1000's sound
// chip (three square-wave tones and a noise channel, 2 dB volume steps) -
// playing the byte code and tables read from the player's program (Curse:
// START.EXE segment 0x699, the profile says where), and a synth that turns
// the result into 8-bit samples for the DAC.
//
// Plain C++ (no Arduino), host-tested in tools/host_tests/test_dax.cpp.
// How the driver works was learned from the original program's listing
// (facts: the Project's claude/sound_facts.md); own code.
//
// Threads: start() may be called from the game while render() runs in the
// audio task: the request is picked up at the next tick.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace sound {

enum class Device : uint8_t { Tandy = 0, PcSpeaker = 1 };

// Where the driver's data sits in its segment (offsets in that segment)
struct Layout {
    uint32_t segment_image;     // the segment's first byte in the unpacked program
    uint16_t lo, hi;            // the part to read: tables and byte code
    uint16_t pc_table, tandy_table;     // per sound, 4 words: each voice's start (0: left alone)
    uint8_t  sounds;            // entries in each table
    uint8_t  fireball_spell, lightning_spell;   // the spells with sounds of their own (the rest: kSpell)
};

// The games' sound numbers (PlaySound): 0 stop, 2..0x0E the effects
enum Id : uint8_t {
    kStop = 0, kSpell = 2, kMagicHit = 3, kMagicStars = 4, kDeath = 5, kSling = 6, kHit = 7, kLightning = 8,
    kMiss = 9, kStep = 0x0A, kFireball = 0x0B, kMissile = 0x0C, kTitle = 0x0D,
};

class Player {
public:
    // The driver's bytes [lo, hi) of its segment (copied); false when they
    // don't look like the driver's tables
    bool set_data(const Layout& l, const uint8_t* bytes, size_t n);
    bool ready() const { return data_ != nullptr; }
    ~Player();

    // Starts sound `id` (the games' number) on the device: it replaces the
    // one playing; 0 stops. Safe from another thread than render().
    void start(int id, Device d);
    // Renders n unsigned 8-bit samples (silence = 128) at `hz`, volume
    // 0-255; false when nothing is sounding any more
    bool render(uint8_t* out, int n, int hz, int volume);

    // ---- the sequencer, for tests
    void tick();
    bool busy() const;
    struct Out {                        // what the hardware gets this tick
        bool     speaker_on;            // PC: the square wave at 1193182 / divisor Hz
        uint16_t divisor;
        uint16_t tone[3];               // Tandy: tone counters N (3579545 / (32 N) Hz)
        uint8_t  atten[4];              // 0 loud .. 15 off (voice 3: noise)
        uint8_t  noise;                 // noise control: bit 2 white, bits 0-1 rate (3: voice 2's)
    };
    const Out& out() const { return out_; }

private:
    struct Voice {
        uint16_t f[24];                 // the games' 0x30-byte record, word by word
    };
    void begin(int n, Device d);
    void tick_voice(int v);
    void parse(int v);
    uint8_t  byte(uint32_t off) const;
    uint16_t word(uint32_t off) const;
    void     output();

    uint8_t* data_ = nullptr;
    size_t   n_ = 0;
    Layout   lay_{};
    Voice    voice_[4] = {};
    uint8_t  owner_[4] = {};
    int      current_ = 0;              // the internal sound playing (0: none)
    Device   dev_ = Device::Tandy;
    uint16_t ret_ = 0;                  // the byte code's one-level CALL
    Out      out_{};
    std::atomic<int> pending_{-1};      // a start() not picked up yet: id | device << 8
    // The synth
    uint32_t tick_acc_ = 0;
    uint32_t phase_[4] = {};
    uint32_t lfsr_ = 0x0F35;
    int      quiet_ = 0;                // ticks silent in a row
};

} // namespace sound

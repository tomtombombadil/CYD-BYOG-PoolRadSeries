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
//
// Pools of Darkness has no such driver: its effects are digitised samples
// (ZOUNDS.DAX) played by DigPak drivers - Clips, ClipPlayer below.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "dax.h"

namespace sound {

enum class Device : uint8_t { Tandy = 0, PcSpeaker = 1 };

// Where the driver's data sits in its segment (offsets in that segment)
struct Layout {
    uint32_t segment_image;     // the segment's first byte in the unpacked program
    uint16_t lo, hi;            // the part to read: tables and byte code
    uint16_t pc_table, tandy_table;     // per sound, 4 words: each voice's start (0: left alone)
    uint8_t  sounds;            // entries in each table
    uint8_t  fireball_spell, lightning_spell;   // the spells with sounds of their own (the rest: kSpell)
    // A second part to read (0: none) - Secret's Tandy byte code sits
    // further on in the segment, past the driver's own code
    uint16_t lo2 = 0, hi2 = 0;
    size_t   size() const { return static_cast<size_t>(hi - lo) + (hi2 > lo2 ? hi2 - lo2 : 0); }
};

// The games' sound numbers (PlaySound): 0 stop, 2..0x0E the effects
enum Id : uint8_t {
    kStop = 0, kSpell = 2, kMagicHit = 3, kMagicStars = 4, kDeath = 5, kSling = 6, kHit = 7, kLightning = 8,
    kMiss = 9, kStep = 0x0A, kFireball = 0x0B, kMissile = 0x0C, kTitle = 0x0D,
};

// The later games' effects as digitised samples (Pools of Darkness - facts:
// the Project's claude/darkness_sound_facts.md): blocks of unsigned 8-bit
// samples in a DAX file, each handed whole (8SVX header and all) to the
// game's DigPak driver at one rate. No byte code, no Layout.
struct Clips {
    const char* file = nullptr;     // the DAX file (nullptr: Layout's byte code instead)
    uint16_t hz = 0;                // the rate the game asks the driver for
    uint8_t  first = 0, last = 0;   // the effect numbers it plays: number n = block n - 1
    uint8_t  ids[16] = {};          // the engine's sounds (Id, Curse's numbers) -> this game's (0: none)
    bool     quiet_turns = false;   // turning left / right makes no step sound (Curse's does)
};

// An effect's samples: the DAX block `block` of the source in a block of its
// own from malloc (the caller frees it; nullptr: not there, or no memory),
// *n its size. Reads only the one index entry (no 3 KB index).
uint8_t* load_clip(dax::ByteSource& src, int block, uint32_t* n);

// The PC speaker driver's (IBMSND.COM) treatment of the samples, in place:
// each 2,048 bytes stretched to 0-255 when their range is 40 or more (a
// range of 256 counts as 0: left as it is), then each byte / 4, at most 51,
// + 1: the pulse widths 1-52 of the speaker's 66-clock pulses
void pc_speaker_levels(uint8_t* p, uint32_t n);

// The digitised effects' player. The game loads an effect and hands it
// over; it sounds once the one playing has ended (the original's driver
// call waits for that) - one waits at most, a newer one takes its place.
// Tandy: the samples as they are (the Tandy 1000 SL / TL's DAC - and the
// Sound Blaster's, the GOG release's); PC speaker: as its driver makes them
// (pc_speaker_levels). Threads: start() from the game, render() from the
// audio task (it frees what has played).
class ClipPlayer {
public:
    ~ClipPlayer();
    // `pcm`: n samples at `hz`, from malloc - taken over (freed here, also
    // when it never plays)
    void start(uint8_t* pcm, uint32_t n, uint32_t hz, Device d);
    // As Player::render: n unsigned 8-bit samples at `hz`; false when
    // nothing sounds any more
    bool render(uint8_t* out, int n, int hz, int volume);
    // Everything dropped (the output stopped first)
    void clear();

private:
    struct Clip {
        uint8_t* pcm;
        uint32_t n;
        uint32_t hz;
        Device   dev;
    };
    static void drop(Clip* c);
    std::atomic<Clip*> next_{nullptr};
    Clip*    cur_ = nullptr;            // the audio task's
    uint64_t pos_ = 0;                  // in cur_, 16.16 samples
};

class Player {
public:
    // The driver's bytes [lo, hi) of its segment, then [lo2, hi2) when
    // there's a second part (copied); false when they don't look like the
    // driver's tables
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

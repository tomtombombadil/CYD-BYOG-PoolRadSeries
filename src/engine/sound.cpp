#include "sound.h"

#include <cstdlib>
#include <cstring>

namespace sound {

namespace {

// The voice record's words (the games' byte offset / 2)
enum Field {
    kWait = 0, kPc = 1, kFreq = 2, kSlide = 3, kOut = 4, kVol = 5, kVolStep = 6, kTempo = 7,
    kModTab = 14, kPhase = 15, kModInc = 16, kModDepth = 17, kModWrap = 18,
};

constexpr uint32_t kTickMilliHz = 236694;           // 1193182 / 5041 Hz, x 1000
constexpr uint32_t kPit = 1193182, kChip = 3579545;

// 2 dB a step: 72 x 10^(-step / 10), 15 = off (loud: the boards' 8-bit DAC and
// small amplifier make little of a quieter signal; voices together may clip)
constexpr uint8_t kAmp[16] = {72, 57, 45, 36, 29, 23, 18, 14, 11, 9, 7, 6, 5, 4, 3, 0};
constexpr int kSpeakerAmp = 120;

// The noise shift register as PC emulators of the Tandy chip run it (it
// restarts from the preset each time the noise control is written)
constexpr uint32_t kNoisePreset = 0x0F35, kWhiteTaps = 0x14002, kPeriodicTaps = 0x08000;

} // namespace

Player::~Player() { free(data_); }

bool Player::set_data(const Layout& l, const uint8_t* bytes, size_t n)
{
    free(data_);
    data_ = nullptr;
    if (!bytes || l.hi <= l.lo || n < static_cast<size_t>(l.hi - l.lo)) return false;
    lay_ = l;
    n_ = l.hi - l.lo;
    data_ = static_cast<uint8_t*>(malloc(n_));
    if (!data_) return false;
    memcpy(data_, bytes, n_);
    // Every table entry: 0 or a place inside the bytes read
    for (int t = 0; t < 2; ++t) {
        const uint16_t table = t ? l.tandy_table : l.pc_table;
        if (table < l.lo || table + l.sounds * 8u > l.hi) {
            free(data_);
            data_ = nullptr;
            return false;
        }
        for (int k = 0; k < l.sounds * 4; ++k) {
            const uint16_t p = word(table + k * 2u);
            if (p && (p < l.lo || p >= l.hi)) {
                free(data_);
                data_ = nullptr;
                return false;
            }
        }
    }
    current_ = 0;
    memset(voice_, 0, sizeof voice_);
    return true;
}

uint8_t Player::byte(uint32_t off) const
{
    return data_ && off >= lay_.lo && off < lay_.hi ? data_[off - lay_.lo] : 0;
}

uint16_t Player::word(uint32_t off) const { return static_cast<uint16_t>(byte(off) | byte(off + 1) << 8); }

void Player::start(int id, Device d)
{
    if (id == 1 || id < 0 || (id > 0x0E && id != 0xFF)) return;      // 1: (an enable flag), 0x0F: not ours
    pending_.store((id == 0xFF ? 0 : id) | static_cast<int>(d) << 8);
}

void Player::begin(int n, Device d)
{
    if (d != dev_) memset(voice_, 0, sizeof voice_);
    dev_ = d;
    current_ = n;
    if (n <= 0 || n >= lay_.sounds) {
        current_ = 0;
        return;
    }
    const uint16_t table = d == Device::Tandy ? lay_.tandy_table : lay_.pc_table;
    for (int v = 0; v < 4; ++v) {
        const uint16_t p = word(table + n * 8u + v * 2u);
        if (!p) continue;
        memset(&voice_[v], 0, sizeof voice_[v]);
        voice_[v].f[kPc] = p;
        voice_[v].f[kWait] = 1;                     // parsed on the next tick
        owner_[v] = static_cast<uint8_t>(n);
    }
}

void Player::parse(int v)
{
    uint16_t pc = voice_[v].f[kPc];
    int sel = v;
    for (int guard = 0; guard < 256 && pc; ++guard) {
        const uint8_t op = byte(pc);
        Voice& s = voice_[sel];
        if (op == 0xFF) {                           // set a field; field 0: wait
            const uint8_t ff = byte(pc + 1u);
            const uint16_t w = word(pc + 2u);
            pc = static_cast<uint16_t>(pc + 4);
            if (ff == 0) {
                voice_[v].f[kWait] = w;
                voice_[v].f[kPc] = w ? pc : 0;
                if (!w) owner_[v] = 0;              // the voice ends
                return;
            }
            if (ff / 2 < 24) s.f[ff / 2] = w;
        } else if (op == 0xFE) {                    // loop on a counter field
            const uint8_t ff = byte(pc + 1u);
            const uint16_t to = word(pc + 2u);
            uint16_t& c = s.f[(ff / 2) % 24];
            if (c == 0) {
                pc = to;
            } else if (--c != 0) {
                pc = to;
            } else {
                pc = static_cast<uint16_t>(pc + 4);
            }
        } else if (op == 0xFD) {                    // select a voice (and clear it)
            sel = (word(pc + 1u) / 0x30) & 3;
            for (int k = kFreq; k <= kModWrap; ++k) voice_[sel].f[k] = 0;
            pc = static_cast<uint16_t>(pc + 3);
        } else if (op == 0xFC) {                    // call (one level)
            ret_ = static_cast<uint16_t>(pc + 3);
            pc = word(pc + 1u);
        } else if (op == 0xFB) {                    // return
            pc = ret_;
        } else if (op == 0xFA) {                    // clear the selected voice
            for (int k = kFreq; k <= kModWrap; ++k) s.f[k] = 0;
            pc = static_cast<uint16_t>(pc + 1);
        } else {                                    // a note: its length (pitch: no game uses it yet)
            const uint8_t note = byte(pc + 1u), len = byte(pc + 2u);
            pc = static_cast<uint16_t>(pc + 3);
            const uint16_t tempo = voice_[v].f[kTempo];
            voice_[v].f[kWait] = static_cast<uint16_t>(len * (tempo > 1 ? tempo : 1));
            if (note & 0x80) {
                voice_[v].f[kPc] = voice_[v].f[kWait] ? pc : 0;
                return;
            }
        }
    }
    voice_[v].f[kPc] = 0;                           // (ran away: the voice ends)
    voice_[v].f[kWait] = 0;
    owner_[v] = 0;
}

void Player::tick_voice(int v)
{
    uint16_t* f = voice_[v].f;
    f[kVol] = static_cast<uint16_t>(f[kVol] + f[kVolStep]);
    f[kFreq] = static_cast<uint16_t>(f[kFreq] + f[kSlide]);
    int mod = 0;
    uint16_t p = static_cast<uint16_t>(f[kPhase] + f[kModInc]);
    if (p != 0) {
        if (p >= f[kModWrap]) p = static_cast<uint16_t>(p - f[kModWrap]);
        f[kPhase] = p;
        const int s = static_cast<int8_t>(byte(f[kModTab] + (p >> 4)));
        mod = (s * 256 * static_cast<int16_t>(f[kModDepth])) >> 16;
    }
    f[kOut] = static_cast<uint16_t>(f[kFreq] + mod);
    if (--f[kWait] == 0) parse(v);
}

void Player::output()
{
    Out o{};
    o.atten[0] = o.atten[1] = o.atten[2] = o.atten[3] = 15;
    if (current_ && dev_ == Device::PcSpeaker) {
        for (int v = 0; v < 4; ++v) {
            const uint16_t* f = voice_[v].f;
            if (!f[kVol] || !f[kWait]) continue;
            o.speaker_on = (f[kVol] & 3) == 3 && f[kOut] != 0;     // (divisor 0 on a sound's first tick: quiet)
            o.divisor = f[kOut];
            break;
        }
    } else if (current_) {
        for (int v = 0; v < 4; ++v) {
            const uint16_t* f = voice_[v].f;
            const uint8_t att = static_cast<uint8_t>((0xFFFF - f[kVol]) >> 12);
            if (v < 3) {
                o.tone[v] = static_cast<uint16_t>((f[kOut] >> 6) & 0x3FF);
                o.atten[v] = o.tone[v] ? att : 15;
            } else {
                o.noise = static_cast<uint8_t>((f[kOut] >> 6) & 7);
                o.atten[3] = att;
            }
        }
    }
    out_ = o;
}

void Player::tick()
{
    const int p = pending_.exchange(-1);
    if (p >= 0) begin(p & 0xFF ? (p & 0xFF) - 1 : 0, static_cast<Device>(p >> 8));
    for (int v = 0; v < 4; ++v)
        if (voice_[v].f[kWait]) tick_voice(v);
    output();
}

bool Player::busy() const
{
    if (!current_) return false;
    for (const Voice& v : voice_)
        if (v.f[kWait]) return true;
    return false;
}

bool Player::render(uint8_t* out, int n, int hz, int volume)
{
    if (!data_ || hz <= 0) {
        memset(out, 128, static_cast<size_t>(n));
        return false;
    }
    const uint32_t per_tick = static_cast<uint32_t>(hz) * 1000u;
    uint32_t inc[4] = {};
    uint32_t noise_inc = 0;
    auto rates = [&]() {
        for (int v = 0; v < 3; ++v)
            inc[v] = out_.tone[v] ? static_cast<uint32_t>((static_cast<uint64_t>(kChip) << 27) / (static_cast<uint64_t>(out_.tone[v]) * hz)) : 0;
        if (dev_ == Device::PcSpeaker) {
            const uint32_t div = out_.divisor ? out_.divisor : 65536u;
            inc[0] = out_.speaker_on ? static_cast<uint32_t>((static_cast<uint64_t>(kPit) << 32) / (static_cast<uint64_t>(div) * hz)) : 0;
        }
        // The noise shifts at the chip's clock / 512, 1024, 2048 - or at voice 2's tone
        const int r = out_.noise & 3;
        if (r < 3) noise_inc = static_cast<uint32_t>((static_cast<uint64_t>(kChip) << 32) / (static_cast<uint64_t>(512u << r) * hz));
        else noise_inc = inc[2];
        lfsr_ = kNoisePreset;                       // (the games write the noise control every tick: it starts over)
    };
    rates();
    bool any = false;
    for (int i = 0; i < n; ++i) {
        tick_acc_ += kTickMilliHz;
        if (tick_acc_ >= per_tick) {
            tick_acc_ -= per_tick;
            tick();
            rates();
            quiet_ = busy() ? 0 : quiet_ + 1;
        }
        int s = 0;
        if (dev_ == Device::PcSpeaker) {
            if (inc[0]) {
                phase_[0] += inc[0];
                s = (phase_[0] & 0x80000000u) ? kSpeakerAmp : -kSpeakerAmp;
            }
        } else {
            for (int v = 0; v < 3; ++v) {
                if (!inc[v] || out_.atten[v] >= 15) continue;
                phase_[v] += inc[v];
                s += (phase_[v] & 0x80000000u) ? kAmp[out_.atten[v]] : -kAmp[out_.atten[v]];
            }
            if (out_.atten[3] < 15 && noise_inc) {
                const uint32_t before = phase_[3];
                phase_[3] += noise_inc;
                if (phase_[3] < before) {           // a shift
                    if (lfsr_ & 1) lfsr_ ^= (out_.noise & 4) ? kWhiteTaps : kPeriodicTaps;
                    lfsr_ >>= 1;
                }
                s += (lfsr_ & 1) ? kAmp[out_.atten[3]] : -kAmp[out_.atten[3]];
            }
        }
        if (s) any = true;
        s = s * volume * (dev_ == Device::Tandy ? 2 : 1) / 255;     // (Tandy: its voices are quieter than the speaker's one)
        out[i] = static_cast<uint8_t>(128 + (s > 127 ? 127 : s < -128 ? -128 : s));
    }
    return any || busy() || pending_.load() >= 0 || quiet_ < 8;
}

} // namespace sound

#include "sound.h"
#include "hlib.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace sound {

namespace {

// The voice record's words (the games' byte offset / 2)
enum Field {
    kWait = 0, kPc = 1, kFreq = 2, kSlide = 3, kOut = 4, kVol = 5, kVolStep = 6, kTempo = 7,
    kModTab = 14, kPhase = 15, kModInc = 16, kModDepth = 17, kModWrap = 18,
};

constexpr uint32_t kTickMilliHz = 236694;           // 1193182 / 5041 Hz, x 1000
constexpr uint32_t kPit = 1193182, kChip = 3579545;

// The chip's volume steps are 2 dB; here 1.2 dB (127 x 0.871^step, 15 = off):
// fades keep their shape but stay audible - the boards' 8-bit DAC and small
// amplifier make little of a quiet signal (Tom: Tandy was barely audible)
constexpr uint8_t kAmp[16] = {127, 111, 96, 84, 73, 64, 55, 48, 42, 37, 32, 28, 24, 21, 18, 0};
constexpr int kSpeakerAmp = 127;

// The voices together, as loud as the 8-bit DAC goes: straight up to 104,
// then a soft knee towards 127 (several loud voices round off instead of
// clipping hard). Tom's boards: their DAC and small amplifier need the
// whole range to be heard at all.
int soft_limit(int x)
{
    const int a = x < 0 ? -x : x;
    if (a <= 104) return x;
    const int t = a - 104;
    const int y = 104 + 23 * t / (t + 23);
    return x < 0 ? -y : y;
}

// The noise shift register as PC emulators of the Tandy chip run it (it
// restarts from the preset each time the noise control is written)
constexpr uint32_t kNoisePreset = 0x0F35, kWhiteTaps = 0x14002, kPeriodicTaps = 0x08000;

} // namespace

Player::~Player() { free(data_); }

bool Player::set_data(const Layout& l, const uint8_t* bytes, size_t n)
{
    free(data_);
    data_ = nullptr;
    if (!bytes || l.hi <= l.lo || n < l.size()) return false;
    lay_ = l;
    n_ = l.size();
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
            if (p && (p < l.lo || p >= l.hi) && (p < l.lo2 || p >= l.hi2)) {
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
    if (!data_) return 0;
    if (off >= lay_.lo && off < lay_.hi) return data_[off - lay_.lo];
    if (off >= lay_.lo2 && off < lay_.hi2) return data_[(lay_.hi - lay_.lo) + (off - lay_.lo2)];
    return 0;
}

uint16_t Player::word(uint32_t off) const { return static_cast<uint16_t>(byte(off) | byte(off + 1) << 8); }

void Player::start(int id, Device d)
{
    if (id == 1 || id < 0 || (id > 0x0E && id != 0xFF)) return;      // 1: (an enable flag), 0x0F: not ours
    if (lay_.last_id && id > lay_.last_id && id != 0xFF) return;      // (Champions' PlaySound: 2-12 only)
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
        // (the speaker's one square wave as it is; the chip's voices with 2x
        // gain into the limiter: about as loud as the speaker, Tom 2026-10-10)
        if (dev_ == Device::Tandy) s = soft_limit(s * 2);
        s = s * volume / 255;
        out[i] = static_cast<uint8_t>(128 + (s > 127 ? 127 : s < -128 ? -128 : s));
    }
    return any || busy() || pending_.load() >= 0 || quiet_ < 8;
}

// ---- Digitised effects (Pools of Darkness; darkness_sound_facts.md)

namespace {

// The PC speaker driver (IBMSND.COM): 2,048-byte pieces stretched when their
// range is at least 40; pulse widths byte / 4 + 1, at most 52, of a pulse 66
// timer clocks long (two pulses a sample: 1193182 / 66 Hz). A silent sample
// (0x80) is width 33: half the pulse.
constexpr uint32_t kPcChunk = 2048;
constexpr int kPcMinRange = 40, kPcTop = 0x33, kPcMid = 33;

} // namespace

uint8_t* load_clip(dax::ByteSource& src, int block, uint32_t* n)
{
    *n = 0;
    dax::Entry e{};
    uint32_t start = 0;
    if (block < 0 || block > 255 || !dax::find_entry(src, static_cast<uint8_t>(block), e, start) || !e.raw_size)
        return nullptr;
    uint8_t* p = static_cast<uint8_t*>(malloc(e.raw_size));
    // (the reader on the heap: its 256-byte buffer stays off the stack - a
    // sound starts deep in a fight)
    dax::RleReader* r = p ? new (std::nothrow) dax::RleReader(src, start, e) : nullptr;
    const size_t got = r ? r->read(p, e.raw_size) : 0;
    const bool ok = r && !r->error() && got == e.raw_size;
    delete r;
    if (!ok) {
        free(p);
        return nullptr;
    }
    *n = static_cast<uint32_t>(got);
    return p;
}

uint8_t* load_glb_clip(dax::ByteSource& src, int entry, uint32_t* n, uint16_t* hz)
{
    *n = 0;
    *hz = 0;
    hlib::Library lib;
    uint32_t pos = 0, len = 0;
    if (hlib::open(src, 0, static_cast<uint32_t>(src.size()), lib) != hlib::Status::Ok ||
        !hlib::entry(src, lib, entry, pos, len) || len < 6)
        return nullptr;
    uint8_t h[6];
    if (src.read_at(pos, h, 6) != 6) return nullptr;
    const uint32_t bytes = static_cast<uint32_t>(h[4] << 8 | h[5]);
    if (!bytes || bytes + 6 > len) return nullptr;
    uint8_t* p = static_cast<uint8_t*>(malloc(bytes * 2));
    if (!p) return nullptr;
    // (read into the upper half, then spread forwards: a sample's two
    // places are never past the byte still to read)
    if (src.read_at(pos + 6, p + bytes, bytes) != bytes) {
        free(p);
        return nullptr;
    }
    for (uint32_t i = 0; i < bytes; ++i) {
        const uint8_t b = p[bytes + i];
        p[2 * i] = static_cast<uint8_t>((b & 15) * 17);
        p[2 * i + 1] = static_cast<uint8_t>((b >> 4) * 17);
    }
    *n = bytes * 2;
    *hz = static_cast<uint16_t>(h[0] << 8 | h[1]);
    return p;
}

void pc_speaker_levels(uint8_t* p, uint32_t n)
{
    for (uint32_t at = 0; at < n; at += kPcChunk) {
        const uint32_t len = n - at < kPcChunk ? n - at : kPcChunk;
        uint8_t lo = 0xFF, hi = 0;
        for (uint32_t i = 0; i < len; ++i) {
            if (p[at + i] < lo) lo = p[at + i];
            if (p[at + i] > hi) hi = p[at + i];
        }
        uint8_t range = static_cast<uint8_t>(hi - lo + 1);     // (8 bits, as the driver's: 0-255 is 0)
        if (range < 2) range = 2;
        if (range < kPcMinRange) continue;
        for (uint32_t i = 0; i < len; ++i)
            p[at + i] = static_cast<uint8_t>(((p[at + i] - lo) << 8) / range);
    }
    for (uint32_t i = 0; i < n; ++i) {
        const int v = p[i] >> 2;
        p[i] = static_cast<uint8_t>((v > kPcTop ? kPcTop : v) + 1);
    }
}

void ClipPlayer::drop(Clip* c)
{
    if (!c) return;
    free(c->pcm);
    delete c;
}

ClipPlayer::~ClipPlayer() { clear(); }

void ClipPlayer::clear()
{
    drop(next_.exchange(nullptr));
    drop(cur_);
    cur_ = nullptr;
    pos_ = 0;
}

void ClipPlayer::start(uint8_t* pcm, uint32_t n, uint32_t hz, Device d)
{
    if (!pcm) return;
    Clip* c = n && hz ? new (std::nothrow) Clip{pcm, n, hz, d} : nullptr;
    if (!c) {
        free(pcm);
        return;
    }
    if (d == Device::PcSpeaker) pc_speaker_levels(pcm, n);
    drop(next_.exchange(c));                // (one waiting at most: the newer one)
}

bool ClipPlayer::render(uint8_t* out, int n, int hz, int volume)
{
    bool any = false;
    for (int i = 0; i < n; ++i) {
        if (!cur_) {
            cur_ = next_.exchange(nullptr);
            pos_ = 0;
        }
        if (!cur_ || hz <= 0) {
            out[i] = 128;
            continue;
        }
        const uint32_t k = static_cast<uint32_t>(pos_ >> 16);
        if (k >= cur_->n) {                 // played: the next one (if any) at once
            drop(cur_);
            cur_ = nullptr;
            --i;
            continue;
        }
        // Between two samples in a straight line (the DAC's and the
        // speaker's own smoothing)
        const int a = cur_->pcm[k], b = k + 1 < cur_->n ? cur_->pcm[k + 1] : a;
        int s = a + (b - a) * static_cast<int>(pos_ & 0xFFFF) / 65536;
        s = cur_->dev == Device::PcSpeaker ? (s - kPcMid) * 127 / kPcMid : s - 128;
        s = s * volume / 255;
        out[i] = static_cast<uint8_t>(128 + (s > 127 ? 127 : s < -128 ? -128 : s));
        pos_ += (static_cast<uint64_t>(cur_->hz) << 16) / static_cast<uint32_t>(hz);
        any = true;
    }
    return any || cur_ != nullptr || next_.load() != nullptr;
}

} // namespace sound

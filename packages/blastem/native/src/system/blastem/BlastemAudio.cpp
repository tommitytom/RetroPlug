// The audio sink: BlastEm's chips, band-limited to the host rate. Every source is one chip and one side per blip, and
// the block reads a ring rather than blip directly, because a block is both longer than one blip frame can hold and
// rarely an exact number of blip frames. The entry points that reach this file are render_audio.h's source API, which
// BlastemShim.cpp forwards from BlastEm's callbacks.
#include "system/blastem/BlastemAudio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rp::blastem {

// The source's state and the per-sample arithmetic: this is the whole design of the file.
struct AudioSink::Source {
    const char*   name = nullptr;  // the chip's own label from render_audio_source ("PSG", "YM2612"); the seam's key
    blip_t*       left = nullptr;  // one blip per side, both clocked at the chip's native rate
    blip_t*       right = nullptr;
    std::vector<std::int16_t> ringLeft;  // host-rate samples read from blip, drained by the block
    std::vector<std::int16_t> ringRight;
    std::uint32_t ringRead = 0, ringFill = 0;
    double        nativeRate = 0.0;  // master clocks / sample_divider, from the chip's own call, as a real rate
    double        dt = 0.0;          // seconds per native sample: 1 / nativeRate
    std::uint32_t lowpassAlpha = 0;  // BlastEm's one-pole coefficient, 16.16
    std::int16_t  lastLeft = 0, lastRight = 0;  // the low-pass state, as render_audio.c kept it
    float         gain = 1.0f;  // the chip's own gain (audio.fm_gain / audio.psg_gain, 0 dB by default)
    std::uint32_t pendingClocks = 0;  // native samples added since the last end_frame: blip's time base
    std::uint32_t flushClocks = 0;    // the most native clocks ONE blip frame may hold: 3900 * nativeRate / hostRate
    // The chip's own render_audio_source arguments, kept because setHostRate and reset() both retune the blip with no
    // chip in the room to ask again.
    std::uint64_t masterClock = 0;
    std::uint64_t sampleDivider = 1;
};

namespace {

// One blip read's worth of samples. blip_max_frame (4000) bounds what ONE time frame can produce, and every frame this
// sink ends stays under it, so one read empties a frame; the ring, not this, is what bounds a block.
constexpr std::uint32_t kScratchSamples = 4096;
// The output samples ONE blip frame is allowed to hold, under blip's own 4096-sample cliff (its time base is a 64-bit
// accumulator with time_unit at 2^52, and t * factor wraps past that).
constexpr double kFrameSamples = 3900.0;
// BlastEm's own low-pass cutoff (DEFAULT_LOWPASS_CUTOFF in genesis.c, config's audio.lowpass_cutoff).
constexpr double kLowpassCutoffHz = 3390.0;

}  // namespace

AudioSink::~AudioSink() { clear(); }

// BlastEm's filter, from render_audio.c's lowpass_sample: alpha = dt / (dt + rc) with rc = (1 / cutoff) / 2pi and
// cutoff 3390 Hz, held as 16.16. It runs at the chip's native rate, before the resampler, exactly as BlastEm's own sink
// does.
static std::int16_t lowpass(std::uint32_t alpha, std::int16_t last, std::int16_t current) {
    const std::int32_t tmp = current * static_cast<std::int32_t>(alpha) +
                             last * static_cast<std::int32_t>(0x10000u - alpha);
    return static_cast<std::int16_t>(tmp >> 16);
}

double AudioSink::sourceNativeRate(const char* name) const {
    if (!name) return 0.0;
    for (const Source* s : sources_) {
        if (s->name && std::strcmp(s->name, name) == 0) return s->nativeRate;
    }
    return 0.0;
}

void AudioSink::prepare(std::uint32_t maxBlockFrames) {
    const std::uint32_t need = maxBlockFrames + kPrime + 64;  // the block, the prime, and the slack a resample delivers
    if (need <= capacity_) return;                            // growth only, as reserveAudio's contract says
    capacity_ = need;
    maxBlockFrames_ = maxBlockFrames;
    scratchLeft_.assign(kScratchSamples, std::int16_t{0});
    scratchRight_.assign(kScratchSamples, std::int16_t{0});
    // The only audible cost of a growth: every ring restarts at the prime. A growth is a quiescent control-thread
    // operation (a node reserving before its blocks run), never a mid-block one.
    for (Source* s : sources_) sizeSource(*s);
}

// A source is LIVE when both its blips exist; an allocation failure leaves it silent rather than half-built, and every
// entry point that touches blip checks. add() refuses such a source outright; a growth that fails mid-run cannot,
// because the chip still holds the pointer and will keep feeding it.
bool AudioSink::sizeSource(Source& s) {
    if (s.left) blip_delete(s.left);
    if (s.right) blip_delete(s.right);
    s.left = blip_new(static_cast<int>(capacity_));
    s.right = blip_new(static_cast<int>(capacity_));
    s.ringLeft.assign(capacity_, std::int16_t{0});
    s.ringRight.assign(capacity_, std::int16_t{0});
    s.pendingClocks = 0;
    s.lastLeft = s.lastRight = 0;
    adjustClock(&s, s.masterClock, s.sampleDivider);
    prime(s);
    return s.left && s.right;
}

// The ring's restart: kPrime host samples of silence, so a block that a resample's fractional phase leaves one sample
// short still reads a sample, and it is always the same one at the same absolute time.
void AudioSink::prime(Source& s) {
    std::fill(s.ringLeft.begin(), s.ringLeft.end(), std::int16_t{0});
    std::fill(s.ringRight.begin(), s.ringRight.end(), std::int16_t{0});
    s.ringRead = 0;
    s.ringFill = kPrime;
}

AudioSink::Source* AudioSink::add(const char* name, std::uint64_t masterClock, std::uint64_t sampleDivider,
                                  std::uint8_t /*channels*/) {
    // No prepare yet means no ring to fill, and a chip handed a null source still runs (its samples simply go nowhere,
    // which is what a source-less chip means to every entry point here).
    if (!capacity_) return nullptr;
    Source* s = new Source();
    s->name = name;
    s->masterClock = masterClock;
    s->sampleDivider = sampleDivider ? sampleDivider : 1;
    if (!sizeSource(*s)) {
        delete s;
        return nullptr;
    }
    sources_.push_back(s);
    return s;
}

void AudioSink::remove(Source* source) {
    if (!source) return;
    const auto it = std::find(sources_.begin(), sources_.end(), source);
    if (it == sources_.end()) return;  // a source another sink handed over, or one already freed
    blip_delete((*it)->left);
    blip_delete((*it)->right);
    delete *it;
    sources_.erase(it);
}

// render_audio_adjust_clock is the one place a chip's rate changes, and it ENDS the frame first: a divider that moves
// mid-run (the VDP test register switches the PSG from 240 to 112, genesis.c:1146-1148) would otherwise re-rate a blip
// that still holds a partial frame, which blip does not allow.
void AudioSink::adjustClock(Source* s, std::uint64_t masterClock, std::uint64_t sampleDivider) {
    if (!s) return;
    if (s->left) blip_end_frame(s->left, s->pendingClocks);
    if (s->right) blip_end_frame(s->right, s->pendingClocks);
    s->pendingClocks = 0;
    s->masterClock = masterClock;
    s->sampleDivider = sampleDivider ? sampleDivider : 1;
    // The chip emits one sample every `sampleDivider` master clocks, so its rate is a rational number and it is kept as
    // one: 53,693,175 / 240 is 223,721.5625, and a truncated integer would put the resampler a hair out, which the ring
    // would collect as latency (about 7 host samples a minute).
    const double native = static_cast<double>(masterClock) / static_cast<double>(s->sampleDivider);
    s->nativeRate = native;
    s->dt = 1.0 / native;
    // blip's time base is a 64-bit accumulator with `time_unit` at 2^52, so ONE frame can hold at most 4096 output
    // samples' worth of clocks before t*factor overflows (`blip_max_frame = 4000` is the same cliff). 3900 leaves a
    // margin, and the count is in native clocks: 3900 host samples = 3900 * native / host clocks.
    s->flushClocks = static_cast<std::uint32_t>(kFrameSamples * native / hostRate_);
    if (!s->flushClocks) s->flushClocks = 1;
    // BlastEm's cutoff, from config (audio.lowpass_cutoff, DEFAULT_LOWPASS_CUTOFF 3390 in genesis.c):
    // alpha = dt / (dt + rc), rc = (1 / cutoff) / (2 pi), held as 16.16.
    const double rc = (1.0 / kLowpassCutoffHz) / (2.0 * 3.14159265358979323846);
    s->lowpassAlpha = static_cast<std::uint32_t>(0x10000 * (s->dt / (s->dt + rc)));
    if (s->left) blip_set_rates(s->left, native, hostRate_);
    if (s->right) blip_set_rates(s->right, native, hostRate_);
}

void AudioSink::putStereo(Source* s, std::int16_t left, std::int16_t right) {
    if (!s || !s->left || !s->right) return;
    // A frame's worth of clocks is a fixed budget (see flushClocks), so a block spans several blip frames and every one
    // of them is moved into the ring as soon as it closes. That is why the ring, not blip, is what the block drains.
    if (s->pendingClocks >= s->flushClocks) flush(*s);
    left = lowpass(s->lowpassAlpha, s->lastLeft, left);
    right = lowpass(s->lowpassAlpha, s->lastRight, right);
    // A band-limited step per native sample: the delta from the previous filtered sample, at this sample's clock.
    blip_add_delta(s->left, s->pendingClocks, static_cast<int>(left) - s->lastLeft);
    blip_add_delta(s->right, s->pendingClocks, static_cast<int>(right) - s->lastRight);
    s->lastLeft = left;
    s->lastRight = right;
    ++s->pendingClocks;
}

// A mono chip (channels 1) is the same sound in both ears: BlastEm's own mixer duplicates it the same way.
void AudioSink::putMono(Source* s, std::int16_t value) { putStereo(s, value, value); }

void AudioSink::setGainDb(Source* s, float gainDb) {
    if (!s) return;
    // render_audio.c's db_to_mult, and the chip's own default is 0 dB (genesis.c's set_audio_config).
    s->gain = std::pow(10.0f, gainDb / 20.0f);
}

// Ends the current blip frame and moves everything it produced into the ring. Called from putStereo (a block is more
// than one blip frame can hold) and from drain (the block's tail), so it is the only place a frame is ended; an
// adjustClock ends its own frame without moving anything, because what it produced stays in blip and comes out here.
void AudioSink::flush(Source& s) {
    if (!s.left || !s.right) return;
    blip_end_frame(s.left, s.pendingClocks);
    blip_end_frame(s.right, s.pendingClocks);
    s.pendingClocks = 0;
    std::uint32_t room = capacity_ - s.ringFill;
    std::uint32_t done = 0;
    while (done < room) {
        const std::uint32_t chunk =
            std::min<std::uint32_t>(room - done, static_cast<std::uint32_t>(scratchLeft_.size()));
        const int got = blip_read_samples(s.left, scratchLeft_.data(), static_cast<int>(chunk), 0);
        const int gotRight = blip_read_samples(s.right, scratchRight_.data(), static_cast<int>(chunk), 0);
        // Both sides are fed the same clocks at the same rates, so their avail is identical; a mismatch is a bug (and
        // counting it beats writing one side's zeros into the other's slot).
        if (got != gotRight) {
            ++starved_;
            return;
        }
        if (got <= 0) return;
        for (int k = 0; k < got; ++k) {
            const std::uint32_t at = (s.ringRead + s.ringFill + static_cast<std::uint32_t>(k)) % capacity_;
            s.ringLeft[at] = scratchLeft_[static_cast<std::size_t>(k)];
            s.ringRight[at] = scratchRight_[static_cast<std::size_t>(k)];
        }
        s.ringFill += static_cast<std::uint32_t>(got);
        done += static_cast<std::uint32_t>(got);
    }
}

void AudioSink::drain(std::uint32_t frames, float* left, float* right) {
    for (std::uint32_t i = 0; i < frames; ++i) {
        left[i] = 0.0f;
        right[i] = 0.0f;
    }
    for (Source* s : sources_) {
        if (!s->left || !s->right) continue;  // an allocation that failed; a silent source, not a starved one
        // The block's tail: putStereo has already flushed every full blip frame, so this closes the last partial one.
        flush(*s);
        const float scale = s->gain / 32768.0f;
        for (std::uint32_t i = 0; i < frames; ++i) {
            if (s->ringFill == 0) {
                ++starved_;
                continue;
            }
            left[i] += static_cast<float>(s->ringLeft[s->ringRead]) * scale;
            right[i] += static_cast<float>(s->ringRight[s->ringRead]) * scale;
            s->ringRead = (s->ringRead + 1) % capacity_;
            --s->ringFill;
        }
    }
}

void AudioSink::setHostRate(double sampleRate) {
    if (sampleRate <= 0.0) return;
    hostRate_ = sampleRate;
    // Every ring restarts at the prime: the 16 samples of silence the block reads ahead of the core's own output are
    // silence at the NEW rate, and the phase blip carries is the only thing that survives.
    for (Source* s : sources_) {
        prime(*s);
        adjustClock(s, s->masterClock, s->sampleDivider);  // also recomputes flushClocks, which is in NATIVE clocks
    }
}

void AudioSink::reset() {
    for (Source* s : sources_) {
        if (s->left) blip_reset(s->left, static_cast<int>(capacity_));
        if (s->right) blip_reset(s->right, static_cast<int>(capacity_));
        s->pendingClocks = 0;
        s->lastLeft = s->lastRight = 0;
        // blip_reset leaves each blip at blip_buf's DEFAULT rate, so the rates go back on here or every state load
        // leaves the core silent at whatever blip's default factor means for this host.
        adjustClock(s, s->masterClock, s->sampleDivider);
        prime(*s);
    }
}

void AudioSink::clear() {
    for (Source* s : sources_) {
        blip_delete(s->left);
        blip_delete(s->right);
        delete s;
    }
    sources_.clear();
    starved_ = 0;
}

}  // namespace rp::blastem

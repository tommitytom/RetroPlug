#pragma once

#include <cstdint>
#include <vector>

#include "blip_buf.h"  // blip_t (deps/blastem: blip_buf 1.1.0, our mirror's `retroplug` branch)

namespace rp::blastem {

// One audio_source (one chip) as this host drives it: BlastEm's own 3390 Hz one-pole low-pass and gain at the chip's
// NATIVE sample rate, then blip_buf's band-limited step synthesis to the host rate, then a small ring the block drains
// exactly. BlastEm's render_audio.c is not compiled: it is a process-global registry capped at 8 sources (a ninth
// aborts), and it resamples linearly.
//
// The native rate is whatever the chip told us: `master_clock / sample_divider` from its render_audio_source call, never
// a constant in this file.
//
// What it sounds like, against BlastEm's own render_audio.c path measured on captured chip streams:
//   - Level: the fundamental is within 0.1 dB for the YM and 0.5 dB for the PSG. blip's kernel takes a little treble:
//     -0.35 dB at 3 kHz down to -3.4 dB at 16 kHz for the PSG at 48 kHz, and rather less for the YM, whose own linear
//     interpolation already rolls off at its 53 kHz rate.
//   - No DC: blip's integrator is a DC blocker, so the PSG's +0.036 FS offset and the YM's +0.011 are removed, as
//     hardware's coupling capacitors would remove them. A full-scale peak therefore differs from BlastEm's by up to
//     7 dB, which is why a level case pins an RMS and not a peak.
//   - The bass corner scales with the host rate: host / (2 pi 2^9), 14.9 Hz at 48 kHz, 29.8 at 96 kHz and 59.7 at
//     192 kHz. It is blip's own (bass_shift in blip_buf.c) and it is accepted: the cost of a host at 192 kHz is bass
//     below 60 Hz, which is a deviation this host documents rather than hides. Raising bass_shift in the mirror's copy
//     is the lever if such a host ever ships, at the price of a slower drift toward silence for a DC offset.
//   - Latency: kPrime primed samples plus blip's kernel delay, about 26 samples at 48 kHz.
class AudioSink {
public:
    AudioSink() = default;
    ~AudioSink();  // out of line: drops every source, which is the only place their blips are freed
    // Raw pointers and a destructor: a copy would free one set of chips twice.
    AudioSink(const AudioSink&) = delete;
    AudioSink& operator=(const AudioSink&) = delete;

    // [control, at activation] Size every source's ring and every blip for `maxBlockFrames`, and prime the ring with
    // kPrime host samples of silence. Growth only, and the ring carries one DMA overshoot on top of the block (see
    // kDmaOvershootSeconds); a `maxBlockFrames` beyond what a ring can name is refused rather than wrapped.
    void prepare(std::uint32_t maxBlockFrames);

    // [block] The block's `frames` host samples, summed from every live source into `left`/`right`, after each source's
    // own gain. Zero-fills both lanes first (the adding is finishBlock's, SystemBase's contract) and counts a
    // starvation if a source is short, which is a bug and the cases assert it never happens.
    void drain(std::uint32_t frames, float* left, float* right);

    // [any] Host samples the sink has starved since activation: 0 on a driven core.
    std::uint64_t starvedSamples() const { return starved_; }

    // [any] Host samples a ring had to drop because a source produced more than the ring holds. 0 on a driven core: the
    // ring is sized for the longest 68k DMA, so a drop means the bound is wrong, not that a ROM is unusual.
    std::uint64_t overrunSamples() const { return overrun_; }

    // The silence every ring carries ahead of its first real sample, in host frames. The fractional part of a
    // native-to-host resample leaves a block one host sample short now and then, and this is what absorbs it.
    static constexpr std::uint32_t kPrime = 16;

    // The native rate the named source's own render_audio_source / render_audio_adjust_clock call gave it, or 0. A test
    // seam: it is how a case sees that a divider change really reached the sink. Double, not integer: the chip emits one
    // sample every `divider` master clocks, and 53,693,175 / 240 is 223,721.5625.
    double sourceNativeRate(const char* name) const;

    // Called by the render_audio.h entry points below.
    struct Source;
    Source* add(const char* name, std::uint64_t masterClock, std::uint64_t sampleDivider, std::uint8_t channels);
    void     remove(Source* source);
    void     adjustClock(Source* source, std::uint64_t masterClock, std::uint64_t sampleDivider);
    void     putStereo(Source* source, std::int16_t left, std::int16_t right);
    void     putMono(Source* source, std::int16_t value);
    void     setGainDb(Source* source, float gainDb);
    // [control/block] The host sample rate every ring reads at (the same for all sources). Re-primes every ring.
    void setHostRate(double sampleRate);
    // Drops every source (a core being destroyed).
    void clear();
    // Drops and re-primes every source's ring and blip, keeping the sources and their rates: what a state load needs
    // (the ring, the blips and the filter are the host's state and a savestate does not carry them).
    void reset();

    // The sink a source belongs to, or null. A chip's callback names the source it holds, and the source carries its
    // own sink, so this is how every render_audio.h forwarder finds the right one: routing through the current instance
    // would hand one instance's callback to whichever instance ran last on the thread.
    static AudioSink* owner(Source* source);

private:
    void prime(Source& source);
    // Give one source a ring and two blips of `capacity`, retune them, and prime the ring. add() and a growth both run
    // through it. False (with the source left exactly as it was) when a blip could not be allocated.
    bool sizeSource(Source& source, std::uint32_t capacity);
    // The ring size a block and a host rate need. 0 when the request is beyond what a ring can name.
    static std::uint32_t capacityFor(std::uint32_t maxBlockFrames, double hostRate);
    // Grow to `capacity` if it is larger than the current one, committing capacity_ only once every source has been
    // sized (a throw part way leaves the sink as it was). True when it grew, so a caller can skip its own re-rate.
    bool grow(std::uint32_t capacity);
    // Ends the current blip frame, moves everything it produced into the ring and leaves blip EMPTY. The ONLY place a
    // frame is ended: putStereo calls it when the frame's clock budget is spent (blip's 64-bit accumulator cannot hold
    // a block), drain calls it for the block's tail, and adjustClock calls it before it re-rates. Emptying it matters:
    // blip writes a delta at its own avail plus the delta's offset, so a surplus left here is what walks off the end of
    // a buffer sized for one frame.
    void flush(Source& source);
    // The ring's scratch for one blip read: a MEMBER, not a local, because a block runs on the audio thread and 16 KiB
    // of stack there is not a thing to spend.
    std::vector<std::int16_t> scratchLeft_;
    std::vector<std::int16_t> scratchRight_;
    std::vector<Source*>      sources_;
    std::uint32_t             capacity_ = 0;
    std::uint32_t             maxBlockFrames_ = 0;  // the largest block a ring was sized for; setHostRate re-uses it
    double                    hostRate_ = 48000.0;
    std::uint64_t             starved_ = 0;
    std::uint64_t             overrun_ = 0;
};

}  // namespace rp::blastem

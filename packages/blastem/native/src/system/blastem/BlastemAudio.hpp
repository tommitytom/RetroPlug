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
class AudioSink {
public:
    AudioSink() = default;
    ~AudioSink();  // out of line: drops every source, which is the only place their blips are freed

    // [control, at activation] Size every source's ring and every blip for the plan's largest block, and prime the
    // ring with kPrime host samples of silence. Growth only.
    void prepare(std::uint32_t maxBlockFrames);

    // [block] The block's `frames` host samples, summed from every live source into `left`/`right` (added, not
    // overwritten, as SystemBase's contract says), after each source's own gain. Fills with silence and counts a
    // starvation if a source is short, which is a bug and the cases assert it never happens.
    void drain(std::uint32_t frames, float* left, float* right);

    // [any] Host samples the sink has starved since activation: 0 on a driven core.
    std::uint64_t starvedSamples() const { return starved_; }

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

private:
    void prime(Source& source);
    // Give one source a ring and two blips of the current capacity_, retune them, and prime the ring. add() and
    // prepare()'s growth both run through it. False when a blip could not be allocated, which leaves the source silent.
    bool sizeSource(Source& source);
    // Ends the current blip frame and moves what it produced into the ring. The ONLY place a frame is ended: putStereo
    // calls it when the frame's clock budget is spent (putStereo has no other way to stay inside blip's 64-bit
    // accumulator), and drain calls it for the block's tail.
    void flush(Source& source);
    // The ring's scratch for one blip read: a MEMBER, not a local, because a block runs on the audio thread and 16 KiB
    // of stack there is not a thing to spend (drain's scratch was a local array in the first draft).
    std::vector<std::int16_t> scratchLeft_;
    std::vector<std::int16_t> scratchRight_;
    std::vector<Source*>      sources_;
    std::uint32_t             capacity_ = 0;
    std::uint32_t             maxBlockFrames_ = 0;
    double                    hostRate_ = 48000.0;
    std::uint64_t             starved_ = 0;
};

}  // namespace rp::blastem

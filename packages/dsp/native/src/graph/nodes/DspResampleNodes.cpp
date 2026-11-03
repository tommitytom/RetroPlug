#include "graph/nodes/DspResampleNodes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "dsp/Resample.hpp"
#include "graph/node.hpp"

namespace rp::graph {

// A named namespace for the settings struct, as DspNodes.cpp's: its JSON Schema $defs key is its qualified name.
namespace dsp {

struct ResampleNodeSettings {
    // The buffer is named by the settings, not bound to a resource port as rp.core.buffer-player's is, because the
    // resampler has to be built for the buffer's rate and building it allocates. A buffer on a port is first seen on
    // the block thread, where nothing may allocate; a buffer the settings name is pinned at create, on the control
    // thread, and a ref that moves is a rebuild (node.hpp's resource_ref), whose fresh instance builds its resamplers
    // in activate, on the control thread as well. What that buys costs ordering, and the cost is not covered: the
    // planner refuses a resource bound to a port reader when the same resource has a port writer (a reader must take
    // the writer's output on a wire), and a settings ref is not a binding it sees, so this node resamples whichever
    // prefix of the buffer the block schedule has written when it runs: a silently truncated stream, with no
    // diagnostic. D2's driver fills a buffer before the plan that plays it, never in the same plan as the resampler.
    rp::resource_ref<"audio-buffer"> buffer;
    std::uint32_t                    channels = 2;
    std::string                      layout = "stereo";
    // Off: the buffer, then silence, which is the kernel's whole-buffer stream (the kit path's resample, before its
    // cut) continued for as long as the plan runs. On: the buffer again from its first frame at its end, as the player
    // loops, one seamless stream through the resampler. Off by default, unlike the player, because the stream the
    // resampler exists for - a kit's, D2's offline chain - is the buffer once; a document that wants a loop says so.
    bool loop = false;
};

}  // namespace dsp

namespace {

using namespace dsp;

// One resampler per channel the layout and the buffer both have, built in activate as node members: a Resampler owns
// an r8brain object and has a real destructor, and objects built in tail memory are never destroyed (node.hpp's hook
// list), so this node has no extra_memory. A layout channel past the buffer's last is silence, as the player's.
//
// At the plan's rate equal to the buffer's the samples still go through the kernel rather than around it: r8brain
// builds no stage at equal rates and hands each chunk back as it took it, and a float's round trip through double is
// exact, so the output is the buffer bit for bit, from one code path rather than a bypass that could drift from it.
//
// A re-activation at the same rate (a new block size) keeps the stream as it is. One at a new rate rebuilds the
// resamplers for it and carries on from the buffer frame the output had reached, not from the buffer's start: an
// activate preserves state (rp_node.h). That frame is the output's length in buffer time, to within one frame, and
// what is heard from there is a fresh r8brain run-in: r8brain compensates its own latency (`getLatency()` is 0), so a
// stream started at buffer frame `from` places its output at `from` to within that frame, and the input the old
// resamplers had consumed beyond it is fed again rather than heard twice. The resume uses the `loop` the last commit
// left, so a loop toggled before a rate change wraps where the stream would have clamped, or clamps where it would
// have wrapped.
struct Resample {
    static constexpr const char* id = "rp.dsp.resample";
    static constexpr const char* name = "Resample";
    static constexpr const char* category = "dsp/source";
    static constexpr bool        heap = true;  // r8brain allocates when the resamplers are built
    using Settings = ResampleNodeSettings;

    struct { rp::audio_out<rp::stereo> out; } outputs;

    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "a resampler")) return e;
        return checkLayout(s.layout, s.channels);
    }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, s.layout);
    }

    std::vector<rp::dsp::Resampler> resamplers;
    const rp_audio_buffer*          buffer = nullptr;  // pinned by the instance, so it outlives every block
    std::uint32_t                   channels = 2;
    bool                            loop = false;
    double                          rate = 0.0;     // the plan rate the resamplers were built for; 0 before the first
    std::uint32_t                   position = 0;   // the next buffer frame to feed them, 0 .. frames
    std::uint32_t                   origin = 0;     // the buffer frame their stream started at
    std::uint64_t                   emitted = 0;    // frames output since then

    // At create (control) and at each commit (audio). A commit never changes the buffer - a ref that moved is a
    // rebuild - nor the channel count, which changes the layout and so rebuilds too; only `loop` takes effect here.
    void on_settings(const Settings& s) noexcept {
        buffer = s.buffer.audio();
        channels = s.channels;
        loop = s.loop;
    }

    void activate(double sampleRate, std::uint32_t) {
        // ResourceTable::createAudioBuffer refuses a buffer of no frames and a rate that is not positive, but
        // rp_audio_buffer is a public ABI struct: this is the one place its frame count divides and its rate reaches
        // r8brain's constructor and the resume's arithmetic.
        if (!buffer || buffer->frames == 0 || !(buffer->sample_rate > 0.0)) return;  // nothing to resample: silence
        if (sampleRate == rate) return;
        std::uint32_t from = 0;
        if (rate > 0.0) {
            const auto elapsed =
                static_cast<std::uint64_t>(static_cast<double>(emitted) * (buffer->sample_rate / rate));
            from = loop ? static_cast<std::uint32_t>((origin + elapsed) % buffer->frames)
                        : static_cast<std::uint32_t>(std::min<std::uint64_t>(origin + elapsed, buffer->frames));
        }
        // Built aside and moved in, so a bad_alloc (which the authoring layer reports as the activation's failure)
        // leaves the resamplers this node had, and their stream, as they were.
        std::vector<rp::dsp::Resampler> built(std::min(channels, buffer->channels));
        for (auto& r : built) r.init(buffer->sample_rate, sampleRate);
        resamplers = std::move(built);
        rate = sampleRate;
        position = origin = from;
        emitted = 0;
    }

    // Fills `n` frames of `out` from one resampler, feeding it `source` from `at` (and silence past the end, or the
    // source again when looping) whenever it runs dry. Every channel's resampler is fed the same counts in the same
    // order, so each leaves `at` where the others do.
    void render(rp::dsp::Resampler& r, const float* source, std::uint32_t& at, float* out, std::uint32_t n) noexcept {
        const std::uint32_t frames = buffer->frames;
        std::size_t done = 0;
        for (;;) {
            done += r.pull(out + done, n - done);
            if (done == n) return;
            // The store is empty now, so a push takes at least a frame unless the resampler was never built.
            if (at >= frames && loop) at = 0;
            std::size_t took = 0;
            if (at < frames) {
                took = r.push(source + at, frames - at);
                at += static_cast<std::uint32_t>(took);
            } else {
                took = r.pushSilence(rp::dsp::Resampler::kChunk);
            }
            if (took == 0) {
                std::fill(out + done, out + n, 0.0f);
                return;
            }
        }
    }

    void process(const rp::Block& b) noexcept {
        const std::uint32_t n = b.frames();
        std::uint32_t       end = position;
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
            float* const y = outputs.out[c];
            if (c >= resamplers.size()) {
                std::fill(y, y + n, 0.0f);
                continue;
            }
            std::uint32_t at = position;
            render(resamplers[c], buffer->data[c], at, y, n);
            end = at;
        }
        position = end;
        emitted += n;
    }
};

}  // namespace

const rp_package* dspResamplePackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Resample>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp.resample","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph

extern "C" const rp_package* rp_dsp_resample_package(void) { return rp::graph::dspResamplePackage(); }

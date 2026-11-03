#include "graph/nodes/DspNodes.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#ifndef RP_DEVICE  // what validate, adjust_layout and the DPCM node use: the desktop's
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#endif

#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Dpcm.hpp"
#include "dsp/Gain.hpp"
#include "graph/node.hpp"

namespace rp::graph {

// The settings structs live in a named namespace, not an anonymous one: a Settings struct's JSON Schema $defs key is
// its qualified name, which compilers spell differently for an anonymous namespace. Only they need the name;
// everything else below stays internal to this file. The kernels are rp::dsp (src/dsp/), so a kernel type is always
// spelled out in full here, and the biquad's settings struct carries `Node` because the kernel has one under the same
// short name (`rp::dsp::BiquadSettings` is the kernel's: this file builds one unnamed for `biquadCoefficients`).
// Gain's and dither's settings structs have no kernel counterpart, so nothing else needs the suffix.
namespace dsp {

// The eight filters and the six dither forms, labelled as every other enum in a document is: the kebab-case of the
// kernel enumerator it names (node.hpp's enum_param uses the same convention for a param), in the order the kernel
// declares them, so a label's index is its enumerator's value (the static_asserts below hold that).
using BiquadType = rp::choice<"low-pass", "high-pass", "band-pass", "band-stop", "peak", "low-shelf", "high-shelf",
                              "all-pass">;
using DitherAlgorithm = rp::choice<"none", "high-pass-tpdf", "shaped-tpdf", "error-diffusion", "jjn", "sierra-lite">;

static_assert(BiquadType::value_of<"low-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::LowPass) &&
                  BiquadType::value_of<"high-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::HighPass) &&
                  BiquadType::value_of<"band-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::BandPass) &&
                  BiquadType::value_of<"band-stop">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::BandStop) &&
                  BiquadType::value_of<"peak">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::Peak) &&
                  BiquadType::value_of<"low-shelf">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::LowShelf) &&
                  BiquadType::value_of<"high-shelf">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::HighShelf) &&
                  BiquadType::value_of<"all-pass">() == static_cast<std::uint32_t>(rp::dsp::BiquadType::AllPass),
              "a filter label's index is its kernel enumerator's value");
static_assert(DitherAlgorithm::value_of<"none">() == static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::None) &&
                  DitherAlgorithm::value_of<"high-pass-tpdf">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::HighPassTPDF) &&
                  DitherAlgorithm::value_of<"shaped-tpdf">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::ShapedTPDF) &&
                  DitherAlgorithm::value_of<"error-diffusion">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::ErrorDiffusion) &&
                  DitherAlgorithm::value_of<"jjn">() == static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::JJN) &&
                  DitherAlgorithm::value_of<"sierra-lite">() ==
                      static_cast<std::uint32_t>(rp::dsp::DitherAlgorithm::SierraLite),
              "a dither label's index is its kernel enumerator's value");

// The seed a live patch gets when its document names none. A kit compile fills the seed from a hash of the input, so
// every compile gives the same bytes; a running patch has no whole input to hash, and a fixed seed keeps its noise the
// same on every run, which is what makes a rendered patch reproducible. Any value does: init salts it with each TPDF
// algorithm's own constant, so the two do not draw the same noise from it.
constexpr std::uint32_t kDefaultDitherSeed = 0x9e3779b9u;

// What one channel's noise is offset from the next channel's by: an odd constant from the golden ratio, so channels 0
// and 1 of a patch draw unrelated streams rather than two streams one seed apart.
constexpr std::uint32_t kChannelSeedStep = 0x9e3779b9u;

// A label as the kernel's enumerator: a cast, since the static_asserts above hold the two in step.
static rp::dsp::BiquadType biquadTypeOf(const BiquadType& t) noexcept {
    return static_cast<rp::dsp::BiquadType>(t.value());
}
static rp::dsp::DitherAlgorithm ditherAlgorithmOf(const DitherAlgorithm& a) noexcept {
    return static_cast<rp::dsp::DitherAlgorithm>(a.value());
}

struct GainNodeSettings {
    std::uint32_t channels = 2;
};

struct BiquadNodeSettings {
    std::uint32_t channels = 2;
    BiquadType    type = BiquadType::make<"low-pass">();
};

struct DitherNodeSettings {
    std::uint32_t   channels = 2;
    // `none` is plain rounding, the kit path's when no dither is set: the least surprising thing to insert, and the
    // depth is the kit path's 4 bits, so the node's default is a plain bit crusher rather than noise nobody asked for.
    DitherAlgorithm algorithm = DitherAlgorithm::make<"none">();
    int             bits = 4;  // 1 .. 16, the kernel's range
    std::uint32_t   seed = kDefaultDitherSeed;
    // A buffer whose hash is the seed instead: the kit path's, `rp::dsp::inputSeed` over the whole buffer the dither
    // quantises (a kit's resampled sample at the bank rate, before it is trimmed to whole frames). Empty, the seed
    // above is used and nothing about the node changes. A settings ref rather than a port, for the reason
    // rp.dsp.resample's buffer is one (DspResampleNodes.cpp has the threading argument): the hash is taken at activate,
    // on the control thread, from a resource create has pinned. What that costs is ordering: an rp.core.buffer-recorder
    // writing this buffer in the same plan is not ordered before this node, so the hash is of whatever the buffer held
    // when activate ran. D2's driver records a buffer in one job and reads it in the next.
    rp::resource_ref<"audio-buffer"> seedFrom;
};

struct NormalizeNodeSettings {
    // The buffer is a settings ref, as rp.dsp.resample's is: the peak is measured at activate, on the control thread,
    // over a resource create has pinned, and the node streams that buffer rather than reading a handle on the block
    // thread. Empty names none, which a document is refused for: a normalise without a buffer is silence with no
    // reason to play it.
    rp::resource_ref<"audio-buffer"> buffer;
    // The kit spec's gain, a setting and not a param. A normalising gain is `gain / peak` (the kit's `GainEffect`
    // folds the peak in exactly that way): about 7.4 for a source peaking at -26 dBFS, and negative for a phase flip,
    // where a param's range would clamp a document's value silently. A setting carries any float the spec has.
    float                            gain = 1.0f;
    std::uint32_t                    channels = 2;
};

struct DpcmNodeSettings {
    rp::resource_ref<"audio-buffer"> buffer;
    // The 7-bit map's peak normalisation, risa's per-sample flag: on, the buffer's DC-blocked peak divides a 45 swing
    // (its loudest sample lands near full scale); off, the map is a plain 63 / 32768 scale. A setting, so no param
    // range clamps it.
    bool normalize = true;
};

}  // namespace dsp

namespace {

using namespace dsp;

// The kernels the two per-channel nodes build in their instance tail memory are never destroyed (node.hpp's hook list:
// nothing runs a destructor over tail memory), so their state must own nothing for that to be sound. A kernel that
// allocates or holds a handle - the resampler's, which is why its node keeps it in a member - fails to compile here
// rather than leaking one object per instance.
static_assert(std::is_trivially_destructible_v<rp::dsp::Biquad>);
static_assert(std::is_trivially_destructible_v<rp::dsp::Dither>);

// ---- rp.dsp.gain --------------------------------------------------------------------------------------------------

// A held gain is the kernel's multiply, exact: the kit path's arithmetic, so a gain node at 0.37 gives the bytes a
// kit compiled with that gain gives. A gain that moved is a ramp across one block, from the value the last block ended
// at to the block's new value, so a live move does not step. The block's value is its last one (a frame-stamped change
// included), so a change anywhere in a block is the ramp's target for that block rather than the next one.
struct Gain {
    static constexpr const char* id = "rp.dsp.gain";
    static constexpr const char* name = "Gain";
    static constexpr const char* category = "dsp/gain";
    using Settings = GainNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    // A linear multiply, because that is what the kernel is and what the kit path's bytes depend on: a level in dB
    // would need a conversion the kit path does not do, and a held value would no longer be the exact multiply. The
    // range covers any sane kit spec's plain gain, a phase flip or a boost or cut; a normalising gain is
    // rp.dsp.normalize's, computed from the buffer at activate and never this param's. A kit compile refuses a plain
    // gain outside the range rather than letting the planner clamp it silently.
    struct { rp::float_param<"Gain", rp::range{-100.0, 100.0, 1.0}> gain; } params;

#ifndef RP_DEVICE
    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a gain"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }
#endif

    rp::dsp::Gain kernel;
    float         applied = 1.0f;  // the gain the last block ended at
    bool          snap = true;     // the next block takes its value whole: a fresh instance or a fresh activation

    void activate(double, std::uint32_t) noexcept { snap = true; }

    void process(const rp::Block& b) noexcept {
        const std::uint32_t n = b.frames();
        const float target = static_cast<float>(params.gain.latest());
        if (snap || target == applied) {
            snap = false;
            applied = target;
            kernel.init(target);
            for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c)
                kernel.process(inputs.in[c], outputs.out[c], n);
            return;
        }
        // One step applies to every channel, since the ramp is the same on each of them: the division happens once a
        // block, not per sample and per channel.
        const float step = (target - applied) / static_cast<float>(n);
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
            const float* x = inputs.in[c];
            float*       y = outputs.out[c];
            for (std::uint32_t i = 0; i < n; ++i) {
                // The last sample lands on the target itself rather than one step short of it, so the next block is
                // the held case: an exact multiply.
                const float g = i + 1 == n ? target : applied + step * static_cast<float>(i + 1);
                y[i] = x[i] * g;
            }
        }
        applied = target;
    }
};

// ---- rp.dsp.biquad -------------------------------------------------------------------------------------------------

// One kernel per channel, in host-supplied memory. The coefficients are the block's params at the sample rate the
// node was activated at, recomputed at the block start that first sees a different value: a sweep between blocks
// changes the coefficients and keeps the state (what `setup` does), so it does not click from a reset, while a fresh
// instance or a fresh activation starts from cleared state (what `init` does). The kernel's output clamp is part of its
// arithmetic, so the node's output is clamped to [-1, 1] as the kit path's is.
//
// A frequency or Q beyond the node's range is not refused: the params' ranges are the node's, not the kernel's. A
// document's or `setParam`'s value outside one is clamped silently by the planner and the host, and a wired value is
// not clamped at all, so the kernel computes the coefficients the RBJ formulas give for whatever it reads. The node
// clamps nothing of its own, a frequency above Nyquist included, because the kit path hands the spec's frequency
// straight to the kernel and a clamp here would be a different byte for the same kit. So the ranges cover any sane kit
// value (1 Hz to 96 kHz, Q 0.001 to 1000, -96 to 96 dB, where a kit's `FilterEffect` is four plain floats), and a kit
// compile refuses a spec outside them itself rather than letting the planner clamp it. It refuses a frequency at or
// above its source rate's Nyquist too, which the planner cannot see: it knows neither the plan's rate nor the source's.
//
// Above Nyquist the RBJ formulas put the poles outside the unit circle, so the kernel's unclamped feedback grows until
// the state is infinite and then NaN, and the clamp writes a NaN through (`std::clamp` of a NaN is the NaN). That is
// reachable at every plan rate under 192 kHz, from a document or `setParam` as well as a wire (at 48 kHz most of the
// range is above Nyquist, at the standalone's 22050 and 32000 Hz nearly all of it), and nothing between the node and
// the host output rejects a non-finite sample. The growth is slow enough to be heard: about 700 samples from a cleared
// state at 12 kHz on a 22.05 kHz plan, and the kernel's clamp writes rail-to-rail output in every block until the
// guard fires, so a held above-Nyquist setting is full-scale bursts with the silent blocks the guard writes between
// them rather than silence. So the node guards the state and leaves the coefficients alone: a channel whose filter
// came out of a block with any of its four state values not finite has that filter reset and its block written as
// silence, so a sweep past Nyquist cannot poison the stream, and a frequency that comes back into range is a fresh
// filter's from the next block on.
//
// That guard differs from the kernel run directly in two regimes, and it is byte-visible in both: a kit filter whose
// frequency is above its source rate's Nyquist produces constant codes today (the state goes NaN, and the quantiser
// reads every NaN as code 0), where this node recovers; and any non-finite input sample, which a decoded 32-bit float
// WAV can carry (nothing between the sample cache and the kit compiler checks), latches the kernel for good, where this
// node resets. No case in the kit baseline is in either regime, so none of its bytes moved. A kit compile refuses both
// rather than choosing between the kernel call and this node: a source with a non-finite sample and a filter at or
// above its source rate's Nyquist are errors, so it never asks this node to match the kernel there. A finite source
// can still reach the regime (a filter whose gain overflows on huge samples, a non-finite value an earlier node in the
// same pass made), so the node reports every reset through the host's rp.host.guards/1 when the host offers it: an
// offline job counts the reports in its completion and a kit compile refuses the pass, so a kit that compiles never
// carried a reset; a live host offers nothing, and the guard works as it always has.
struct Biquad {
    static constexpr const char* id = "rp.dsp.biquad";
    static constexpr const char* name = "Biquad";
    static constexpr const char* category = "dsp/filter";
    using Settings = BiquadNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct {
        rp::float_param<"Frequency", rp::range{1.0, 96000.0, 1000.0}, "Hz"> frequency;
        rp::float_param<"Q", rp::range{0.001, 1000.0, 1.0}>                  q;
        rp::float_param<"Gain", rp::range{-96.0, 96.0, 0.0}, "dB">           gainDb;  // Peak and the shelves only
    } params;

#ifndef RP_DEVICE
    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a biquad"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }
#endif

    static std::size_t extra_memory(const Settings& s) {
        return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Biquad);
    }

    std::span<rp::dsp::Biquad> filters;  // one per channel, in the instance's tail memory

    const rp_host_guards* guards = nullptr;  // where a reset is reported, when the host offers it
    double               rate = 48000.0;  // as activate gave it
    rp::dsp::BiquadType  type = rp::dsp::BiquadType::LowPass;
    bool                 clear = true;  // the next block computes coefficients and clears the state
    // What the coefficients were last computed for, so a block that changed none of it does no trigonometry.
    float frequency = 0.0f, q = 0.0f, gainDb = 0.0f;

    // The host's guard report, taken once at create: null on a host that offers none, the live engine's included.
    void bind_host(const rp_host* host) noexcept {
        guards = static_cast<const rp_host_guards*>(host->get_interface(host, RP_HOST_GUARDS_INTERFACE));
    }

    void attach(std::span<std::byte> mem) noexcept {
        // The host builds these once per instance and never destroys them (node.hpp's hook list), which is sound only
        // because Biquad is trivially destructible: the static_assert above holds that.
        auto* const first = reinterpret_cast<rp::dsp::Biquad*>(mem.data());
        const std::size_t count = mem.size() / sizeof(rp::dsp::Biquad);
        for (std::size_t i = 0; i < count; ++i) ::new (static_cast<void*>(first + i)) rp::dsp::Biquad();
        filters = std::span<rp::dsp::Biquad>(first, count);
    }

    // The filter type is a setting, so a commit changes it here (on the block thread) and the next block recomputes.
    void on_settings(const Settings& s) noexcept {
        const rp::dsp::BiquadType wanted = biquadTypeOf(s.type);
        if (wanted != type) clear = true;
        type = wanted;
    }

    void activate(double sampleRate, std::uint32_t) noexcept {
        rate = sampleRate;
        clear = true;
    }

    void process(const rp::Block& b) noexcept {
        const float f = static_cast<float>(params.frequency.latest());
        const float qq = static_cast<float>(params.q.latest());
        const float g = static_cast<float>(params.gainDb.latest());
        if (clear || f != frequency || qq != q || g != gainDb) {
            // One coefficient set for every channel: `setup`/`init` would re-run the RBJ trigonometry per channel, on
            // the block thread, for the same answer.
            const rp::dsp::BiquadCoefficients c =
                rp::dsp::biquadCoefficients({type, static_cast<float>(rate), f, qq, g});
            for (auto& filter : filters) {
                filter.c = c;
                if (clear) filter.reset();
            }
            clear = false;
            frequency = f;
            q = qq;
            gainDb = g;
        }
        for (std::uint32_t c = 0; c < outputs.out.channel_count && c < filters.size(); ++c) {
            rp::dsp::Biquad& filter = filters[c];
            float* const     y = outputs.out[c];
            filter.process(inputs.in[c], y, b.frames());
            if (!std::isfinite(filter.x1) || !std::isfinite(filter.x2) || !std::isfinite(filter.y1) ||
                !std::isfinite(filter.y2)) {
                // Nothing of a block whose filter ran away is worth keeping: the state is what a poisoned filter is,
                // and the block may already hold the NaNs it wrote, which the clamp passed through.
                filter.reset();
                std::fill(y, y + b.frames(), 0.0f);
                if (guards) guards->fired(guards->ctx, "rp.dsp.biquad reset a filter whose state went non-finite");
            }
        }
    }
};

// ---- rp.dsp.dither ------------------------------------------------------------------------------------------------

// One kernel per channel, in host-supplied memory (the noise generator is most of it). The kernel quantises to codes
// and the node maps them back to audio at the quantised levels, so the same node is the kit path's dither and a bit
// crusher in a live patch.
//
// A settings commit restarts the noise: a new algorithm, depth or seed is a new stream from the first sample of the
// next block. A fresh activation restarts it too, so a plan that has just started gives the stream a fresh kernel
// gives, whatever the plan it replaced left in the state. So does a re-activation, which `GraphHost::reactivate` runs
// as a deactivate and an activate over every live instance: a new plan rate, or a host's first larger block, restarts
// the noise and snaps a gain that was mid-ramp.
//
// Each channel's noise is its own: the kernel's `init` salts its seed with the algorithm's constant, so a channel's
// seed is the stream's (the `seedFrom` hash, or `seed`) plus its index times a golden-ratio step. Every channel seeded
// from the one word would add the same TPDF sequence to every channel, which sums coherently (6 dB louder than
// decorrelated noise, and dead centre) instead of spreading. Channel 0 keeps that seed itself, so a mono patch is
// unchanged. The error diffusion forms draw no noise and are unaffected.
//
// A block whose input is not finite would latch the state: a NaN error is a NaN error for the rest of the stream
// (`toCode` reads a NaN as code 0, so the channel sits at -1 for good), and a `+inf` leaves JJN's and Sierra Lite's
// `past[]` infinite, which pins the output at +1 (the TPDF and error-diffusion forms clamp the value before deriving
// their state from it, so an infinity there only quantises that one sample to full scale). A channel that comes out of
// a block with any of its state not finite therefore has its state restarted from the stream's seed: the latch lasts
// one block rather than the stream. The block itself is left as the kernel quantised it, which is finite and at worst
// full scale, so only the state is restarted and the click is one block's. A `seedFrom` buffer restarts it from the
// word the stream started from: the buffer's hash, not a live patch's seed, so a recovered channel draws that seed's
// sequence again from its first draw where the kit kernel would still be latching.
//
// That restart is a second place the node differs from the kernel run directly, and a kit source can carry the regime:
// nothing between `SampleCache` and the kit compile checks a sample for finiteness, so a 32-bit float WAV carrying a
// NaN or an infinity latches the kernel for the rest of the slot where this node recovers at the next block. No
// baseline case carries one, so no kit byte moved; a kit compile refuses a source with a non-finite sample, so it
// never asks this node to match the kernel there. A finite input can still overflow the state (a sample near the
// float range's top, scaled and fed back), so the node reports every restart through the host's rp.host.guards/1 when
// the host offers it, as the biquad reports its resets: an offline job counts it, a live host hears nothing.
struct Dither {
    static constexpr const char* id = "rp.dsp.dither";
    static constexpr const char* name = "Dither";
    static constexpr const char* category = "dsp/quantise";
    using Settings = DitherNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;

#ifndef RP_DEVICE
    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "a dither")) return e;
        if (s.bits < rp::dsp::kMinDitherBits || s.bits > rp::dsp::kMaxDitherBits)
            return "bits must be between " + std::to_string(rp::dsp::kMinDitherBits) + " and " +
                   std::to_string(rp::dsp::kMaxDitherBits);
        return std::nullopt;
    }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }
#endif

    static std::size_t extra_memory(const Settings& s) {
        return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Dither);
    }

    std::span<rp::dsp::Dither> states;

    rp::dsp::DitherAlgorithm algorithm = rp::dsp::DitherAlgorithm::None;
    int                      bits = 4;
    std::uint32_t            seed = kDefaultDitherSeed;        // the setting, when `seedFrom` names nothing
    std::uint32_t            hashedSeed = kDefaultDitherSeed;  // `seedFrom`'s hash, taken at activate
    std::uint32_t            activeSeed = kDefaultDitherSeed;  // what the noise is seeded from: the hash, or `seed`
    // The buffer a `seedFrom` names, pinned by the instance (so it outlives every block) and read only at activate.
    const rp_audio_buffer* seedFrom = nullptr;
    bool                   restart = true;  // the next block seeds the noise from the fields above
    const rp_host_guards*  guards = nullptr;  // where a restart is reported, when the host offers it

    // The host's guard report, taken once at create: null on a host that offers none, the live engine's included.
    void bind_host(const rp_host* host) noexcept {
        guards = static_cast<const rp_host_guards*>(host->get_interface(host, RP_HOST_GUARDS_INTERFACE));
    }

    void attach(std::span<std::byte> mem) noexcept {
        // Never destroyed, like the biquad's states above; the static_assert holds that Dither is trivially
        // destructible, which it is with its noise generator held inline.
        auto* const first = reinterpret_cast<rp::dsp::Dither*>(mem.data());
        const std::size_t count = mem.size() / sizeof(rp::dsp::Dither);
        for (std::size_t i = 0; i < count; ++i) ::new (static_cast<void*>(first + i)) rp::dsp::Dither();
        states = std::span<rp::dsp::Dither>(first, count);
    }

    void on_settings(const Settings& s) noexcept {
        algorithm = ditherAlgorithmOf(s.algorithm);
        bits = s.bits;
        seed = s.seed;
        seedFrom = s.seedFrom.audio();
        // A commit restarts the noise, from whichever word the stream was seeded by: a `seedFrom` buffer's hash is
        // taken once, at activate, so a commit that changes `seed` while one is named changes nothing about the noise
        // (the hash replaces the setting), and a commit without one takes the new value.
        activeSeed = seedFrom ? hashedSeed : seed;
        restart = true;
    }

    // The seed the noise starts from is settled here, on the control thread: a `seedFrom` buffer is hashed whole
    // (`rp::dsp::inputSeed`), and without one the `seed` setting stands. The hash is the kit path's: over the first
    // channel's samples, which are all a kit's mono buffer has, and over every one of them (the trimmed input would be
    // a different word). A re-activation hashes the buffer again, which is the same word.
    void activate(double, std::uint32_t) noexcept {
        if (seedFrom) hashedSeed = rp::dsp::inputSeed(seedFrom->data[0], seedFrom->frames);
        activeSeed = seedFrom ? hashedSeed : seed;
        restart = true;
    }

    void seedState(rp::dsp::Dither& state, std::uint32_t channel) const noexcept {
        state.init(algorithm, bits, activeSeed + channel * kChannelSeedStep);
    }

    void process(const rp::Block& b) noexcept {
        if (restart) {
            for (std::uint32_t c = 0; c < states.size(); ++c) seedState(states[c], c);
            restart = false;
        }
        for (std::uint32_t c = 0; c < outputs.out.channel_count && c < states.size(); ++c) {
            rp::dsp::Dither& state = states[c];
            state.processLevels(inputs.in[c], outputs.out[c], b.frames());
            if (!std::isfinite(state.prevDither) || !std::isfinite(state.shapingState) ||
                !std::isfinite(state.err1) || !std::isfinite(state.err2) || !std::isfinite(state.error) ||
                !std::isfinite(state.past[0]) || !std::isfinite(state.past[1]) || !std::isfinite(state.past[2]) ||
                !std::isfinite(state.past[3])) {
                seedState(state, c);
                if (guards) guards->fired(guards->ctx, "rp.dsp.dither restarted a channel whose state went non-finite");
            }
        }
    }
};

// ---- rp.dsp.normalize ---------------------------------------------------------------------------------------------

// A buffer player that normalises, for a kit chain's normalising gain (`GainEffect{normalize: true}`): the peak of the
// channels it plays is measured at activate, `normalizedGain(gain, peak)` folds it into the gain exactly as
// lsdj/Effects.cpp does, and the buffer is streamed times that gain, from its first frame, silence past its end. It
// plays frames rather than seconds: the plan's rate is the buffer's in a kit job, and a buffer player's position is a
// buffer frame.
//
// The peak is measured across the channels this node plays: each played channel's `rp::dsp::peak`, then the greatest
// of those, which is the same fold the mono path makes, continued across the channels. A channel past the buffer's
// last is silence and contributes nothing, so `channels: 1` over a stereo buffer is normalised by the channel it
// plays and the unplayed one never scales it. One gain scales every channel, as the kit path's single multiply does
// to its mono buffer, and no channel is normalised on its own. Kit sources are mono, so a kit compile never asks the
// question.
//
// The peak is read where activation finds the buffer. An rp.core.buffer-recorder writing that buffer in the same plan
// is not ordered before this node (a settings ref is not a binding the planner sees), so the peak is taken over
// whatever the buffer held then, silence for a fresh one, and a normalise of zeros is the bare gain, with no
// diagnostic. D2's driver records a buffer in one job and reads it in the next.
//
// Nothing is allocated, here or in `process`: the buffer is read where the resource table holds it, and the only state
// is the scale and the position. So the node declares no `heap` and the planner plans it on the Pico, unlike
// rp.dsp.resample, whose buffer needs r8brain built at activate.
struct Normalize {
    static constexpr const char* id = "rp.dsp.normalize";
    static constexpr const char* name = "Normalize";
    static constexpr const char* category = "dsp/source";
    using Settings = NormalizeNodeSettings;

    struct { rp::audio_out<rp::channels<2, "any">> out; } outputs;

#ifndef RP_DEVICE
    static std::optional<std::string> validate(const Settings& s) {
        if (auto e = checkChannels(s.channels, "a normalise")) return e;
        if (s.buffer.empty()) return "a normalise names an audio buffer";
        return std::nullopt;
    }
    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }
#endif

    const rp_audio_buffer* buffer = nullptr;  // pinned by the instance, so it outlives every block
    float                  gain = 1.0f;       // the setting, as the last commit left it
    std::uint32_t          channels = 2;      // the output's, which is how many of the buffer's the peak folds
    float                  peak = 0.0f;       // the played channels', measured at activate
    float                  scale = 1.0f;      // normalizedGain(gain, peak)
    std::uint32_t          position = 0;      // the next buffer frame to play, 0 .. frames

    // A commit never moves the buffer (a ref that moved is a rebuild) and never changes the channel count (a new
    // layout rebuilds), and the peak is the last activate's, so a gain that changed takes effect from the next block
    // with no block-thread work beyond the multiply.
    void on_settings(const Settings& s) noexcept {
        buffer = s.buffer.audio();
        gain = s.gain;
        channels = s.channels;
        scale = rp::dsp::normalizedGain(gain, peak);
    }

    // The peak is measured here, on the control thread. The position is not reset: a re-activation (a new block size,
    // a new plan) carries the stream on, as the player does, where a fresh instance starts at the buffer's first frame.
    void activate(double, std::uint32_t) noexcept {
        peak = 0.0f;
        if (buffer)
            for (std::uint32_t c = 0; c < channels && c < buffer->channels; ++c)
                peak = std::max(peak, rp::dsp::peak(buffer->data[c], buffer->frames));
        scale = rp::dsp::normalizedGain(gain, peak);
    }

    void process(const rp::Block& b) noexcept {
        const std::uint32_t n = b.frames();
        const std::uint32_t frames = buffer ? buffer->frames : 0;
        const std::uint32_t at = position < frames ? position : frames;
        const std::uint32_t available = frames - at;
        for (std::uint32_t c = 0; c < outputs.out.channel_count; ++c) {
            float* const y = outputs.out[c];
            if (!buffer || c >= buffer->channels) {
                std::fill(y, y + n, 0.0f);  // a layout channel the buffer lacks, as the player's
                continue;
            }
            const float* const x = buffer->data[c];
            for (std::uint32_t i = 0; i < n; ++i) y[i] = i < available ? x[at + i] * scale : 0.0f;
        }
        position = n < available ? at + n : frames;
    }
};

// ---- rp.dsp.dpcm --------------------------------------------------------------------------------------------------

// The DPCM node declares `heap`, so it is never built for the device (node_device.hpp): the planner refuses it on the
// Pico, and a device build leaves it out of the package.
#ifndef RP_DEVICE

// risa's DMC encoder as a node: at activate, on the control thread, the kernel (src/dsp/Dpcm.hpp) runs over the whole
// bound buffer into storage this instance owns, and every block after that streams one delta bit per frame as an audio
// level: +1 for a one, -1 for a zero, silence past the buffer's end. The bits are the risa bank's payload rather than
// audio, so a document records them through rp.core.buffer-recorder and the packer that turns each level back into
// its bit (`level > 0`) runs in TypeScript. Silence past the buffer is 0.0f, which that packer reads as a zero bit
// rather than as no bit; a recorder longer than the buffer therefore appends real-looking bits, and an unwritten
// recorder tail is 0.0f too. A kit job records exactly the resampled length, so this cannot move a kit byte.
//
// Mono, with no `channels` setting: the encoder is the NES's single DMC channel, and a multi-channel buffer has no
// meaning it could define without mixing or dropping a channel nobody asked it to. A buffer of another channel count
// therefore fails the activation with a reason, rather than being silently taken apart; a kit chain is mono.
//
// A NaN in the buffer is outside the contract: nothing between a resource and the encode checks a sample for finiteness
// (a kit compile refuses a source or pass that carries one, but a live node's buffer is whatever its document names),
// and `std::lround` of a NaN is unspecified, so the bits there are not defined. An infinity is clamped like any value
// out of the int16 range, and encodes at a rail.
//
// The storage is a member, built at activate and freed with the instance on the control thread, as rp.dsp.resample's
// resamplers are, which is why the type declares `heap` and the planner refuses it on the Pico.
//
// `normalize` is the type's rebuild key, and it is one because re-encoding is a whole-buffer pass: a settings commit
// runs on the block thread, where a node may do no more than copy, and a re-encode there would cost the buffer's
// length in the middle of a block. A commit that changed it therefore builds a fresh instance, which encodes in
// activate on the control thread; the stream restarts at the first bit, which is what a newly encoded stream means
// anyway. Every other setting is a copy in `on_settings`.
//
// The bits are read where the first activation finds the buffer. An rp.core.buffer-recorder writing it in the same plan
// is not ordered before this node (a settings ref is not a binding the planner sees), so a DMC encode of a fresh buffer
// is the encode of silence, with no diagnostic. D2's driver records a buffer in one job and reads it in the next.
struct Dpcm {
    static constexpr const char* id = "rp.dsp.dpcm";
    static constexpr const char* name = "DPCM";
    static constexpr const char* category = "dsp/codec";
    static constexpr bool        heap = true;  // the bit storage is built at activate
    using Settings = DpcmNodeSettings;

    struct { rp::audio_out<rp::mono> out; } outputs;

    static std::optional<std::string> validate(const Settings& s) {
        if (s.buffer.empty()) return "a DPCM node names an audio buffer";
        return std::nullopt;
    }

    // A change of `normalize` re-encodes, so it is a rebuild rather than a commit (the comment above says why).
    static bool rebuild_key(const Settings& s) noexcept { return s.normalize; }

    const rp_audio_buffer*    buffer = nullptr;  // pinned by the instance, so it outlives every block
    bool                      normalize = true;  // the setting, as activate read it
    std::vector<std::uint8_t> bits;              // one per buffer frame, built at activate
    std::uint32_t             position = 0;      // the next bit to play, 0 .. bits.size()

    void on_settings(const Settings& s) noexcept {
        buffer = s.buffer.audio();
        normalize = s.normalize;
    }

    // The encode is here, on the control thread: a fresh instance, or the one a re-activation keeps, builds its bits
    // before the first block.
    void activate(double, std::uint32_t) {
        if (!buffer) {
            bits.clear();
            position = 0;
            return;
        }
        if (buffer->channels != 1)
            throw std::runtime_error("a DPCM node takes a mono buffer, and this one has " +
                                     std::to_string(buffer->channels) + " channels");
        // A re-activation (a new block size, a new plan rate) over a buffer of the length already encoded keeps those
        // bits, and the stream carries on where it was, as the player's position does. The guard is on the length
        // because a re-encode of that buffer is the same bits: a moved reference is a rebuild and a kit job's instance
        // is fresh, so a buffer rewritten in place under a live instance is the one case not re-read.
        if (bits.size() == buffer->frames) return;
        // Built aside and moved in, so a failed allocation leaves the previous stream's bits as they were.
        std::vector<std::uint8_t> encoded(buffer->frames);
        rp::dsp::encodeDpcm(buffer->data[0], buffer->frames, normalize, encoded.data());
        bits = std::move(encoded);
    }

    void process(const rp::Block& b) noexcept {
        const std::uint32_t n = b.frames();
        const std::uint32_t total = static_cast<std::uint32_t>(bits.size());
        const std::uint32_t at = position < total ? position : total;
        const std::uint32_t available = total - at;
        float* const        y = outputs.out[0];
        for (std::uint32_t i = 0; i < n; ++i) y[i] = i < available ? (bits[at + i] ? 1.0f : -1.0f) : 0.0f;
        position = n < available ? at + n : total;
    }
};
#endif  // RP_DEVICE

}  // namespace

const rp_package* dspPackage() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<Gain>(),
        rp::node_type<Biquad>(),
        rp::node_type<Dither>(),
        rp::node_type<Normalize>(),
#ifndef RP_DEVICE
        rp::node_type<Dpcm>(),
#endif
    };
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph

extern "C" const rp_package* rp_dsp_package(void) { return rp::graph::dspPackage(); }

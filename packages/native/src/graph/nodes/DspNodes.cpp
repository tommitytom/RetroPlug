#include "graph/nodes/DspNodes.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

#include <rfl/Literal.hpp>

#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Gain.hpp"
#include "graph/node.hpp"

namespace rp::graph {

// The settings structs live in a named namespace, not an anonymous one: a Settings struct's JSON Schema $defs key is
// its qualified name, which compilers spell differently for an anonymous namespace. Only they need the name;
// everything else below stays internal to this file. The kernels are rp::dsp (src/dsp/), so a kernel type is always
// spelled out in full here, and each settings struct carries `Node` because the kernel has a settings struct of its own
// under the same short name (`rp::dsp::BiquadSettings` is what `Biquad::process` builds, and it turns up in this file
// under that short name).
namespace dsp {

// The eight filters and the six dither forms, labelled as every other enum in a document is: the kebab-case of the
// kernel enumerator it names (node.hpp's enum_param uses the same convention for a param), in the order the kernel
// declares them, so a label's index is its enumerator's value (the static_asserts below hold that).
using BiquadType = rfl::Literal<"low-pass", "high-pass", "band-pass", "band-stop", "peak", "low-shelf", "high-shelf",
                                 "all-pass">;
using DitherAlgorithm = rfl::Literal<"none", "high-pass-tpdf", "shaped-tpdf", "error-diffusion", "jjn", "sierra-lite">;

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
    // would need a conversion the kit path does not do, and a held value would no longer be the exact multiply.
    struct { rp::float_param<"Gain", rp::range{0.0, 4.0, 1.0}> gain; } params;

    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a gain"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }

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
        // One step for every channel: the ramp is the same on each of them, and a per-sample division is not.
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
// A frequency or Q beyond the project's range is the document's to refuse (the params' range), and the kernel then
// computes the coefficients the RBJ formulas give there; the node clamps nothing of its own, a frequency above
// Nyquist included, because the kit path hands the spec's frequency straight to the kernel and a clamp here would be a
// different byte for the same kit.
struct Biquad {
    static constexpr const char* id = "rp.dsp.biquad";
    static constexpr const char* name = "Biquad";
    static constexpr const char* category = "dsp/filter";
    using Settings = BiquadNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;
    struct {
        rp::float_param<"Frequency", rp::range{20.0, 20000.0, 1000.0}, "Hz"> frequency;
        rp::float_param<"Q", rp::range{0.1, 10.0, 1.0}>                       q;
        rp::float_param<"Gain", rp::range{-24.0, 24.0, 0.0}, "dB">            gainDb;  // Peak and the shelves only
    } params;

    static std::optional<std::string> validate(const Settings& s) { return checkChannels(s.channels, "a biquad"); }

    static void adjust_layout(Layout& l, const Settings& s) {
        l.ports[0].channels = ChannelSpec::fixed(s.channels, "any");
    }

    static std::size_t extra_memory(const Settings& s) {
        return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Biquad);
    }

    std::span<rp::dsp::Biquad> filters;  // one per channel, in the instance's tail memory

    double               rate = 48000.0;  // as activate gave it
    rp::dsp::BiquadType  type = rp::dsp::BiquadType::LowPass;
    bool                 clear = true;  // the next block computes coefficients and clears the state
    // What the coefficients were last computed for, so a block that changed none of it does no trigonometry.
    float frequency = 0.0f, q = 0.0f, gainDb = 0.0f;

    void attach(std::span<std::byte> mem) noexcept {
        // The host builds these once per instance and never destroys them (node.hpp's hook list), which is sound only
        // because Biquad is trivially destructible: the static_assert above holds that.
        auto* const first = reinterpret_cast<rp::dsp::Biquad*>(mem.data());
        const std::size_t count = mem.size() / sizeof(rp::dsp::Biquad);
        for (std::size_t i = 0; i < count; ++i) std::construct_at(first + i);
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
        for (std::uint32_t c = 0; c < outputs.out.channel_count && c < filters.size(); ++c)
            filters[c].process(inputs.in[c], outputs.out[c], b.frames());
    }
};

// ---- rp.dsp.dither ------------------------------------------------------------------------------------------------

// One kernel per channel, in host-supplied memory (the noise generator is most of it). The kernel quantises to codes
// and the node maps them back to audio at the quantised levels, so the same node is the kit path's dither and a bit
// crusher in a live patch.
//
// A settings commit restarts the noise: a new algorithm, depth or seed is a new stream from the first sample of the
// next block. A fresh activation restarts it too, so a plan that has just started gives the stream a fresh kernel
// gives, whatever the plan it replaced left in the state.
struct Dither {
    static constexpr const char* id = "rp.dsp.dither";
    static constexpr const char* name = "Dither";
    static constexpr const char* category = "dsp/quantise";
    using Settings = DitherNodeSettings;

    struct { rp::audio_in<rp::channels<2, "any">> in; } inputs;
    struct { rp::audio_out<rp::match<"in">> out; } outputs;

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

    static std::size_t extra_memory(const Settings& s) {
        return static_cast<std::size_t>(s.channels) * sizeof(rp::dsp::Dither);
    }

    std::span<rp::dsp::Dither> states;

    rp::dsp::DitherAlgorithm algorithm = rp::dsp::DitherAlgorithm::None;
    int                      bits = 4;
    std::uint32_t            seed = kDefaultDitherSeed;
    bool                     restart = true;  // the next block seeds the noise from the three fields above

    void attach(std::span<std::byte> mem) noexcept {
        // Never destroyed, like the biquad's states above; the static_assert holds that Dither is trivially
        // destructible, which it is with its noise generator held inline.
        auto* const first = reinterpret_cast<rp::dsp::Dither*>(mem.data());
        const std::size_t count = mem.size() / sizeof(rp::dsp::Dither);
        for (std::size_t i = 0; i < count; ++i) std::construct_at(first + i);
        states = std::span<rp::dsp::Dither>(first, count);
    }

    void on_settings(const Settings& s) noexcept {
        algorithm = ditherAlgorithmOf(s.algorithm);
        bits = s.bits;
        seed = s.seed;
        restart = true;
    }

    void activate(double, std::uint32_t) noexcept { restart = true; }

    void process(const rp::Block& b) noexcept {
        if (restart) {
            for (auto& state : states) state.init(algorithm, bits, seed);
            restart = false;
        }
        for (std::uint32_t c = 0; c < outputs.out.channel_count && c < states.size(); ++c)
            states[c].processLevels(inputs.in[c], outputs.out[c], b.frames());
    }
};

}  // namespace

const rp_package* dspPackage() {
    static const rp_node_type* const kTypes[] = {
        rp::node_type<Gain>(),
        rp::node_type<Biquad>(),
        rp::node_type<Dither>(),
    };
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.dsp","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

}  // namespace rp::graph

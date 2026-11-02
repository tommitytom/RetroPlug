// The rp.dsp package (src/graph/nodes/DspNodes.cpp, docs/modular-graph-phase-d1-design.md section 5): gain, biquad
// and dither as graph nodes, through real plans built by the graph rig, held to the rp::dsp kernels they wrap.
//
// - Each node's output through a plan equals the kernel run directly over the same signal, bit for bit, and any split
//   of the stream into blocks (1, 7, 64, 1024 frames and a ragged mix) gives the same stream: a node carries its
//   kernel's state across blocks exactly as the kernel does.
// - The params' ranges carry any sane kit value (a phase-flip or large plain gain, a 10 Hz high-pass at Q 20, a
//   +30 dB shelf), and a value outside a range is still clamped to its bound, silently.
// - A gain that moved ramps across one block and lands on the new value; a held one is the kernel's exact multiply,
//   from the plan's first block on. The biquad's coefficients follow a sweep at the next block while its state, which
//   is what a reset would throw away, is kept: a change of type recomputes them and clears the state, which is a fresh
//   filter's stream.
// - A dither's settings are its stream's: a new seed restarts the noise, and the node's output is the kernel's
//   quantised levels, so a live patch and a kit compile run the same arithmetic.
// - A plan block allocates and frees nothing counted from the plan's first block (the suite's counting allocator), and
//   all three types plan on the Pico: they declare neither heap nor threads, and their extra memory is requested for
//   the settings' channel count (the request's size is the assertion), never for the descriptor's maximum.
// - Each type's descriptor is a golden beside golden/rp.test.gain.descriptor.json, recorded with
//   RP_GRAPH_GOLDEN_UPDATE=1.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "CountingAllocator.hpp"
#include "GraphRig.hpp"
#include "dsp/Biquad.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Gain.hpp"
#include "graph/Descriptor.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "graph/nodes/DspResampleNodes.hpp"

namespace {

using rp::graph::test::GraphRig;

constexpr std::size_t kFrames = 4096;

// The packages whose types this file's descriptor goldens are. A package id registers once, so the resampler's type is a
// second package of its own (`rp.dsp.resample`) rather than more types in this one, and this list is what a case
// walking "every rp.dsp type's descriptor" walks.
std::vector<const rp_package*> dspPackages() { return {rp::graph::dspPackage(), rp::graph::dspResamplePackage()}; }

// The block sizes a stream is split into, as Dsp.test.cpp's: one call, the usual host block, an odd size, one frame,
// the resampler's chunk and a ragged sequence that cycles.
const std::vector<std::vector<std::size_t>> kSplits = {{4096}, {64}, {7}, {1}, {1024}, {1, 31, 512, 3, 1024, 77}};

/** A sine plus noise, overdriven past [-1, 1] in places, so the biquad's clamp and the dither's error terms work. */
std::vector<float> signal(std::size_t frames) {
    std::vector<float> out(frames);
    std::uint32_t      rng = 0x1234567u;
    for (std::size_t i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 0.3f;
        out[i] = noise + 1.1f * std::sin(static_cast<float>(i) * 0.031f);
    }
    return out;
}

/** The first index two streams differ at, bit for bit, or -1. One number keeps a failing CHECK readable; a length
 *  mismatch is a value no index can be, so it cannot be mistaken for a wrong first sample. */
std::ptrdiff_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<std::ptrdiff_t>::max();
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

/** Calls `run(offset, n)` over [0, frames) in the block sizes `split` cycles through. */
template <class Run>
void inBlocks(std::size_t frames, const std::vector<std::size_t>& split, Run run) {
    std::size_t at = 0, k = 0;
    while (at < frames) {
        const std::size_t n = std::min(split[k++ % split.size()], frames - at);
        run(at, n);
        at += n;
    }
}

/** A rig whose plans may run blocks up to `maxFrames` frames, so a split can be compared with one call over the
 *  whole stream: GraphRig sizes its host output rows from the same number. A rig can run at another plan rate too, for
 *  a case about what a rate's Nyquist does to a filter, and the buffers it makes carry that rate. */
struct DspRig : GraphRig {
    explicit DspRig(rp::graph::Target target = rp::graph::Target::Desktop, std::uint32_t maxFrames = 4096,
                    double rate = 48000.0)
        : GraphRig({rate, maxFrames, target}, 8, nullptr, maxFrames), rate_(rate) {}

    // An audio buffer resource holding one stream per channel, at the plan's rate. Nothing in these plans reads it
    // (the player just walks the buffer), so the rate is the rig's for a reader's benefit rather than the node's.
    rp::graph::ResourceTable::Id buffer(const std::vector<std::vector<float>>& perChannel) {
        const auto channels = static_cast<std::uint32_t>(perChannel.size());
        const auto id = resources.createAudioBuffer(channels, static_cast<std::uint32_t>(perChannel[0].size()), rate_);
        auto* const b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c) std::copy(perChannel[c].begin(), perChannel[c].end(), b->data[c]);
        return id;
    }

    // An audio buffer resource holding `samples` on every one of its channels.
    rp::graph::ResourceTable::Id buffer(const std::vector<float>& samples, std::uint32_t channels) {
        return buffer(std::vector<std::vector<float>>(channels, samples));
    }

    static std::string res(rp::graph::ResourceTable::Id id) { return "\"res:" + std::to_string(id) + "\""; }

    double rate_;  // what activate gives the plan, and what a buffer made here claims

    // The host output `bus` names, one port for a mono bus and a range for a stereo one.
    static std::string port(std::uint32_t channels) {
        return channels == 1 ? "out.1" : "out.1-" + std::to_string(channels);
    }

    /** Every host channel of the plan's output over `frames`, run in the block sizes `split` cycles through. */
    std::vector<std::vector<float>> render(std::size_t frames, const std::vector<std::size_t>& split) {
        std::vector<std::vector<float>> out(outs.size());
        for (auto& channel : out) channel.reserve(frames);
        inBlocks(frames, split, [&](std::size_t, std::size_t n) {
            run(static_cast<std::uint32_t>(n));
            for (std::size_t c = 0; c < out.size(); ++c)
                out[c].insert(out[c].end(), outs[c].begin(), outs[c].begin() + static_cast<std::ptrdiff_t>(n));
        });
        return out;
    }

    /** One channel of the host output, this block. */
    std::vector<float> block(std::uint32_t channel, std::size_t frames) {
        return std::vector<float>(outs[channel].begin(), outs[channel].begin() + static_cast<std::ptrdiff_t>(frames));
    }

    /** The plan of a player of `sample` into `node` and on to the host output: the shape every case below uses, as the
     *  plan call's whole result, so a case can read the reason a plan was refused. */
    rp::graph::SetGraphResult plan(rp::graph::ResourceTable::Id sample, const std::string& node,
                                   const std::string& settings, std::uint32_t channels = 1) {
        const std::string layout = channels == 1 ? "mono" : "stereo";
        return set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":)" +
                   std::to_string(channels) + R"(,"layout":")" + layout + R"(","loop":false},
                               "resources":{"sample":)" + res(sample) + R"(}},
                   {"id":"x","type":")" + node + R"(","settings":)" + settings + R"(},
                   {"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + std::to_string(channels) + R"(]}}],
                   "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.)" + port(channels) + R"("}]})");
    }

    /** The same plan, as a pass or a fail. */
    bool play(rp::graph::ResourceTable::Id sample, const std::string& node, const std::string& settings,
              std::uint32_t channels = 1) {
        return plan(sample, node, settings, channels).ok;
    }
};

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

}  // namespace

TEST_CASE("rp.dsp.gain through a plan is its kernel, whole or split any way", "[dsp-nodes]") {
    const std::vector<float> in = signal(kFrames);
    rp::dsp::Gain            kernel;
    kernel.init(0.37f);
    std::vector<float> whole(kFrames);
    kernel.process(in.data(), whole.data(), kFrames);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        DspRig     rig;
        const auto sample = rig.buffer(in, 1);
        REQUIRE(rig.play(sample, "rp.dsp.gain", R"({"channels":1},"params":{"gain":0.37})"));
        CHECK(firstDifference(whole, rig.render(kFrames, split)[0]) == -1);
    }
}

TEST_CASE("rp.dsp.gain ramps a moved value across one block and multiplies a held one exactly", "[dsp-nodes]") {
    // A constant 1.0 in, so each output sample IS the gain the node used there.
    DspRig rig;
    REQUIRE(rig.set(R"({"nodes":[{"id":"s","type":"rp.test.source","settings":{"value":1,"channels":1}},
                                 {"id":"x","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":0.5}},
                                 {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                       "wires":[{"from":"s.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})")
                .ok);

    // A held value is the kernel's multiply from the plan's first block on: no fade-in from the param's default.
    rig.run(64);
    std::vector<float> held = rig.block(0, 64);
    for (const float v : held) CHECK(v == 0.5f);

    // A live move ramps across the block it lands in, and its last sample is the new value itself, so the block after
    // is the held case again.
    REQUIRE(rig.host->setParam("x", "gain", 0.25));
    rig.run(64);
    const std::vector<float> ramp = rig.block(0, 64);
    CHECK(ramp[0] < 0.5f);   // it starts from the old value rather than stepping to the new one
    CHECK(ramp[0] > 0.25f);
    CHECK(ramp[63] == 0.25f);  // and arrives exactly
    for (std::size_t i = 1; i < ramp.size(); ++i) CHECK(ramp[i] < ramp[i - 1]);

    rig.run(64);
    for (const float v : rig.block(0, 64)) CHECK(v == 0.25f);

    // A change stamped inside a block is that block's target: the ramp spans the whole block rather than stepping at
    // the change's frame.
    REQUIRE(rig.host->setParam("x", "gain", 1.0, 32));
    rig.run(64);
    const std::vector<float> late = rig.block(0, 64);
    CHECK(late[0] > 0.25f);
    CHECK(late[0] < 1.0f);
    CHECK(late[31] < 1.0f);  // the change's own frame is mid-ramp, not a jump
    CHECK(late[63] == 1.0f);
}

TEST_CASE("rp.dsp.gain carries a kit's gain whole, and still clamps at its bounds", "[dsp-nodes]") {
    const std::vector<float> in = signal(kFrames);
    // A phase flip and a large plain gain: both are legal kit values, and both were outside the range D1 shipped, where
    // the planner clamped them silently. Neither is the normalising gain rp.dsp.normalize computes from its buffer.
    for (const float gain : {-1.0f, 7.4f}) {
        INFO("gain " << gain);
        rp::dsp::Gain kernel;
        kernel.init(gain);
        std::vector<float> reference(kFrames);
        kernel.process(in.data(), reference.data(), kFrames);

        const std::string params = R"({"channels":1},"params":{"gain":)" + std::to_string(gain) + "}";
        for (const auto& split : kSplits) {
            INFO("split starting " << split[0]);
            DspRig     rig;
            const auto sample = rig.buffer(in, 1);
            REQUIRE(rig.play(sample, "rp.dsp.gain", params));
            CHECK(firstDifference(reference, rig.render(kFrames, split)[0]) == -1);
        }
    }

    // A document's value outside the range is clamped to the bound (the planner does it, silently), so the node gives
    // the kernel's stream at that bound rather than the value asked for: the clamp the wider range must keep.
    for (const auto& [asked, bound] : {std::pair{150.0f, 100.0f}, std::pair{-150.0f, -100.0f}}) {
        INFO("gain " << asked << " clamped to " << bound);
        rp::dsp::Gain kernel;
        kernel.init(bound);
        std::vector<float> reference(kFrames);
        kernel.process(in.data(), reference.data(), kFrames);

        DspRig     rig;
        const auto sample = rig.buffer(in, 1);
        REQUIRE(rig.play(sample, "rp.dsp.gain",
                         R"({"channels":1},"params":{"gain":)" + std::to_string(asked) + "}"));
        CHECK(firstDifference(reference, rig.render(kFrames, {4096})[0]) == -1);
    }
}

TEST_CASE("rp.dsp.biquad through a plan is its kernel, whole or split any way", "[dsp-nodes]") {
    const std::vector<float>      in = signal(kFrames);
    const rp::dsp::BiquadSettings settings{rp::dsp::BiquadType::Peak, 48000.0f, 1000.0f, 1.0f, 6.0f};

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        DspRig     rig;
        const auto sample = rig.buffer(in, 2);
        REQUIRE(rig.play(sample, "rp.dsp.biquad",
                         R"({"channels":2,"type":"peak"},"params":{"frequency":1000,"q":1,"gainDb":6})", 2));
        const auto out = rig.render(kFrames, split);

        rp::dsp::Biquad kernel;  // from a fresh filter, over the same splits
        kernel.init(settings);
        std::vector<float> reference(kFrames);
        inBlocks(kFrames, split, [&](std::size_t at, std::size_t n) {
            kernel.process(in.data() + at, reference.data() + at, n);
        });
        // Both channels: each one is its own filter over its own copy of the signal, so each gives this stream, where
        // one state shared across them would not.
        CHECK(firstDifference(reference, out[0]) == -1);
        CHECK(firstDifference(reference, out[1]) == -1);

        // The clamp is the kernel's, so a signal driven past full scale comes out inside [-1, 1], and a peak at +6 dB
        // puts it there.
        for (const float v : out[0]) CHECK(std::abs(v) <= 1.0f);
        CHECK(std::any_of(out[0].begin(), out[0].end(), [](float v) { return std::abs(v) == 1.0f; }));
    }
}

TEST_CASE("rp.dsp.biquad carries a kit's filter whole, and still clamps at its bounds", "[dsp-nodes]") {
    const std::vector<float> in = signal(kFrames);
    // A 10 Hz high-pass at Q 20 and a 3 kHz high shelf at +30 dB: legal kit specs outside the ranges D1 shipped, where
    // the planner clamped them silently.
    struct Spec {
        const char*         type;
        rp::dsp::BiquadType kernelType;
        float               frequency, q, gainDb;
    };
    for (const Spec& spec : {Spec{"high-pass", rp::dsp::BiquadType::HighPass, 10.0f, 20.0f, 0.0f},
                             Spec{"high-shelf", rp::dsp::BiquadType::HighShelf, 3000.0f, 0.7f, 30.0f}}) {
        INFO(spec.type << " at " << spec.frequency << " Hz, Q " << spec.q << ", " << spec.gainDb << " dB");
        rp::dsp::Biquad kernel;
        kernel.init({spec.kernelType, 48000.0f, spec.frequency, spec.q, spec.gainDb});
        std::vector<float> reference(kFrames);
        kernel.process(in.data(), reference.data(), kFrames);

        const std::string settings = R"({"channels":2,"type":")" + std::string(spec.type) +
                                     R"("},"params":{"frequency":)" + std::to_string(spec.frequency) +
                                     R"(,"q":)" + std::to_string(spec.q) +
                                     R"(,"gainDb":)" + std::to_string(spec.gainDb) + "}";
        for (const auto& split : kSplits) {
            INFO("split starting " << split[0]);
            DspRig     rig;
            const auto sample = rig.buffer(in, 2);
            REQUIRE(rig.play(sample, "rp.dsp.biquad", settings, 2));
            const auto out = rig.render(kFrames, split);
            CHECK(firstDifference(reference, out[0]) == -1);
            CHECK(firstDifference(reference, out[1]) == -1);
        }
    }

    // A document's value outside a range is clamped to the bound (the planner does it, silently), so each param asked
    // beyond its range gives the kernel's stream at the bound: the clamp the wider ranges must keep. Every bound is
    // under the 48 kHz plan's Nyquist, where the node and the kernel agree (the guard never fires). Each case also runs
    // the kernel at the value asked, so it fails unless the bound and the asked value really do give different streams.
    struct Clamped {
        float frequency, q, gainDb;
        float boundFrequency, boundQ, boundGainDb;
    };
    for (const Clamped& c : {Clamped{0.5f, 1.0f, 12.0f, 1.0f, 1.0f, 12.0f},
                             Clamped{1000.0f, 0.0005f, 12.0f, 1000.0f, 0.001f, 12.0f},
                             Clamped{1000.0f, 2000.0f, 12.0f, 1000.0f, 1000.0f, 12.0f},
                             Clamped{1000.0f, 1.0f, 120.0f, 1000.0f, 1.0f, 96.0f},
                             Clamped{1000.0f, 1.0f, -120.0f, 1000.0f, 1.0f, -96.0f}}) {
        INFO("high-shelf clamped to " << c.boundFrequency << " Hz, Q " << c.boundQ << ", " << c.boundGainDb
                                      << " dB");
        rp::dsp::Biquad askedKernel;
        askedKernel.init({rp::dsp::BiquadType::HighShelf, 48000.0f, c.frequency, c.q, c.gainDb});
        std::vector<float> asked(kFrames);
        askedKernel.process(in.data(), asked.data(), kFrames);

        rp::dsp::Biquad kernel;
        kernel.init({rp::dsp::BiquadType::HighShelf, 48000.0f, c.boundFrequency, c.boundQ, c.boundGainDb});
        std::vector<float> reference(kFrames);
        kernel.process(in.data(), reference.data(), kFrames);

        // The case would fail without the clamp: the asked value's own stream differs from the bound's.
        CHECK(firstDifference(asked, reference) != -1);

        DspRig     rig;
        const auto sample = rig.buffer(in, 1);
        REQUIRE(rig.play(sample, "rp.dsp.biquad",
                         R"({"channels":1,"type":"high-shelf"},"params":{"frequency":)" +
                             std::to_string(c.frequency) +
                             R"(,"q":)" + std::to_string(c.q) +
                             R"(,"gainDb":)" + std::to_string(c.gainDb) + "}"));
        CHECK(firstDifference(reference, rig.render(kFrames, {4096})[0]) == -1);
    }
}

TEST_CASE("rp.dsp state is per channel, and its memory is sized from the settings' channels", "[dsp-nodes]") {
    // Six channels of different signal through one biquad and one dither. Each channel's output is its own kernel's,
    // so neither node keeps one state for all of them, and extra memory sized for fewer channels than the settings
    // name leaves the extra channels' states unwritten here: a wrong stream, not a heap write, because the per-channel
    // loops are bounded by the span they were attached to.
    std::vector<std::vector<float>> in(6);
    for (std::size_t c = 0; c < in.size(); ++c) {
        in[c] = signal(512);
        for (auto& v : in[c]) v *= 0.1f * static_cast<float>(c + 1);
    }
    DspRig     rig;
    const auto sample = rig.buffer(in);
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":6,"layout":"5.1","loop":false},
                                             "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                                   {"id":"b","type":"rp.dsp.biquad","settings":{"channels":6,"type":"low-pass"},
                                    "params":{"frequency":1000,"q":1}},
                                   {"id":"d","type":"rp.dsp.dither","settings":{"channels":6,"algorithm":"error-diffusion","bits":6,"seed":21}},
                                   {"id":"out","type":"rp.host.audio-out","settings":{"buses":[6]}}],
                                 "wires":[{"from":"p.out","to":"b.in"},{"from":"b.out","to":"d.in"},
                                          {"from":"d.out","to":"out.out.1-6"}]})")
                .ok);
    rig.run(512);

    for (std::uint32_t c = 0; c < 6; ++c) {
        INFO("channel " << c);
        rp::dsp::Biquad biquad;  // a fresh filter and dither for this channel alone
        biquad.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
        rp::dsp::Dither dither;
        dither.init(rp::dsp::DitherAlgorithm::ErrorDiffusion, 6, 21);
        std::vector<float> filtered(512), levelled(512);
        biquad.process(in[c].data(), filtered.data(), 512);
        dither.processLevels(filtered.data(), levelled.data(), 512);
        CHECK(firstDifference(levelled, rig.block(c, 512)) == -1);
    }

    // The request is the settings' channel count and not the descriptor's maximum, which is over-sized and would run
    // every case above just as correctly while wasting 63 kernels a node: the size itself is the assertion.
    struct MemoryCase {
        const char* typeId;
        std::size_t state;
    };
    for (const MemoryCase& mc :
         {MemoryCase{"rp.dsp.biquad", sizeof(rp::dsp::Biquad)}, MemoryCase{"rp.dsp.dither", sizeof(rp::dsp::Dither)}}) {
        const auto* entry = rig.registry.find(mc.typeId);
        REQUIRE(entry);
        INFO(mc.typeId);
        const char* const settings[] = {R"({"channels":1})", R"({"channels":6})"};
        rp_memory_req      req[2]{};
        rp_error           err{};
        for (int i = 0; i < 2; ++i) {
            rp_settings s{settings[i], std::strlen(settings[i]), RP_SETTINGS_JSON};
            REQUIRE(entry->type->query_memory(entry->type, &s, &req[i], &err));
        }
        CHECK(req[1].size - req[0].size == 5 * mc.state);
    }
}

TEST_CASE("rp.dsp.biquad recomputes its coefficients on a sweep and keeps its state", "[dsp-nodes]") {
    DspRig     rig;
    const std::vector<float> in = signal(kFrames);
    const auto sample = rig.buffer(in, 1);
    REQUIRE(rig.play(sample, "rp.dsp.biquad", R"({"channels":1,"type":"low-pass"},"params":{"frequency":1000,"q":1})"));

    rig.run(64);
    const std::vector<float> first = rig.block(0, 64);
    rp::dsp::Biquad reference;  // the same two blocks a filter swept between them, keeping its state
    reference.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    std::vector<float> filtered(64);
    reference.process(in.data(), filtered.data(), 64);
    CHECK(firstDifference(filtered, first) == -1);

    REQUIRE(rig.host->setParam("x", "frequency", 2000.0));
    rig.run(64);
    const std::vector<float> swept = rig.block(0, 64);
    reference.setup({rp::dsp::BiquadType::LowPass, 48000.0f, 2000.0f, 1.0f, 0.0f});
    reference.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, swept) == -1);

    // A filter reset to the new coefficients would start from silence here, which on this signal is a different
    // stream: the state the sweep kept is what the plan's second block shows.
    rp::dsp::Biquad cleared;
    cleared.init({rp::dsp::BiquadType::LowPass, 48000.0f, 2000.0f, 1.0f, 0.0f});
    cleared.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, swept) != -1);
}

TEST_CASE("rp.dsp.biquad recomputes its coefficients when the type changes", "[dsp-nodes]") {
    // The filter type is a setting and the three params stay put across the commit, so the coefficient cache cannot
    // fire: only the type check makes the next block a new filter rather than the old one's coefficients forever.
    const std::vector<float> in = signal(kFrames);
    DspRig                   rig;
    const auto               sample = rig.buffer(in, 1);
    const auto document = [&](const char* type) {
        const std::string settings = R"({"channels":1,"type":")" + std::string(type) + R"("})";
        return R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
                              "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                            {"id":"x","type":"rp.dsp.biquad","settings":)" + settings + R"(,
                             "params":{"frequency":1000,"q":1}},
                            {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                          "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})";
    };
    REQUIRE(rig.set(document("low-pass")).ok);

    rig.run(64);
    std::vector<float> filtered(64);
    rp::dsp::Biquad    reference;
    reference.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    reference.process(in.data(), filtered.data(), 64);
    CHECK(firstDifference(filtered, rig.block(0, 64)) == -1);

    // The same document as a high-pass, a commit: the next block computes the new type's coefficients and clears the
    // state, which is a fresh filter's stream over the same input. A node that kept the old coefficients would give
    // the low-pass stream here, since none of the params moved.
    REQUIRE(rig.set(document("high-pass")).ok);
    rig.run(64);
    const std::vector<float> changed = rig.block(0, 64);
    rp::dsp::Biquad          fresh;
    fresh.init({rp::dsp::BiquadType::HighPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    fresh.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, changed) == -1);

    // And it is the cleared state as well as the new coefficients: a filter that carried its state into them is a
    // different stream over the same input, so this pins the reset rather than the recompute alone.
    rp::dsp::Biquad carried;
    carried.init({rp::dsp::BiquadType::LowPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    std::vector<float> discard(64);
    carried.process(in.data(), discard.data(), 64);
    carried.setup({rp::dsp::BiquadType::HighPass, 48000.0f, 1000.0f, 1.0f, 0.0f});
    carried.process(in.data() + 64, filtered.data(), 64);
    CHECK(firstDifference(filtered, changed) != -1);
}

TEST_CASE("rp.dsp.biquad guards the state a sweep past Nyquist poisons", "[dsp-nodes]") {
    // The standalone offers 22050 Hz, where the frequency knob's top half is above Nyquist (11025 Hz): the RBJ
    // formulas put the poles outside the unit circle, the kernel's feedback grows until the state is infinite and then
    // NaN, and its clamp passes a NaN through. Nothing between the node and the host output rejects a non-finite
    // sample, so the node resets a channel's filter whose state came out of a block non-finite and writes that block
    // as silence: the sweep cannot poison the stream, and the filter recovers when the frequency comes back.
    constexpr std::uint32_t kBlock = 64, kBlocks = 32;
    // The blocks the frequency changes at: past Nyquist from `kSweep`, back in range at `kBack`.
    constexpr std::uint32_t kSweep = 8, kBack = 16;
    const std::vector<float> in = signal(kBlock * kBlocks);
    const auto frequencyOf = [](std::uint32_t block) { return block >= kSweep && block < kBack ? 20000.0f : 2000.0f; };

    DspRig     rig(rp::graph::Target::Desktop, 4096, 22050.0);
    const auto sample = rig.buffer(in, 1);
    REQUIRE(rig.play(sample, "rp.dsp.biquad", R"({"channels":1,"type":"low-pass"},"params":{"frequency":2000,"q":1})"));

    // One block of a filter following the plan's coefficient schedule, with the node's guard: a state that is not
    // finite is reset and the block silenced.
    rp::dsp::Biquad reference;
    reference.init({rp::dsp::BiquadType::LowPass, 22050.0f, frequencyOf(0), 1.0f, 0.0f});
    const auto step = [&](rp::dsp::Biquad& filter, std::uint32_t block, float* out) {
        if (block > 0 && frequencyOf(block) != frequencyOf(block - 1))
            filter.setup({rp::dsp::BiquadType::LowPass, 22050.0f, frequencyOf(block), 1.0f, 0.0f});
        filter.process(in.data() + block * kBlock, out, kBlock);
        if (std::isfinite(filter.x1) && std::isfinite(filter.x2) && std::isfinite(filter.y1) &&
            std::isfinite(filter.y2))
            return false;
        filter.reset();
        std::fill(out, out + kBlock, 0.0f);
        return true;
    };

    std::vector<float>       got;
    std::vector<float>       block(kBlock);
    std::ptrdiff_t           lastReset = -1;
    for (std::uint32_t k = 0; k < kBlocks; ++k) {
        if (k == kSweep) REQUIRE(rig.host->setParam("x", "frequency", 20000.0));
        if (k == kBack) REQUIRE(rig.host->setParam("x", "frequency", 2000.0));
        rig.run(kBlock);
        const std::vector<float> running = rig.block(0, kBlock);
        INFO("block " << k);
        if (step(reference, k, block.data())) lastReset = k;
        CHECK(firstDifference(block, running) == -1);
        got.insert(got.end(), running.begin(), running.end());
    }
    // The guard fired: without it the stream is NaN from the first runaway block to the end. It also stopped by
    // `kBack`, where the frequency is in range again, so the recovery loop below is guaranteed to run.
    REQUIRE(lastReset >= static_cast<std::ptrdiff_t>(kSweep));
    REQUIRE(lastReset < static_cast<std::ptrdiff_t>(kBack));
    for (const float v : got) CHECK(std::isfinite(v));

    // And the reset is a reset: from the block after the last one, a filter built fresh (no history at all) and
    // handed the same coefficient schedule gives the node's stream sample for sample, which a filter still carrying
    // the poisoned state could not.
    rp::dsp::Biquad fresh;
    fresh.init({rp::dsp::BiquadType::LowPass, 22050.0f, frequencyOf(static_cast<std::uint32_t>(lastReset) + 1), 1.0f,
                0.0f});
    for (std::uint32_t k = static_cast<std::uint32_t>(lastReset) + 1; k < kBlocks; ++k) {
        if (k > static_cast<std::uint32_t>(lastReset) + 1 && frequencyOf(k) != frequencyOf(k - 1))
            fresh.setup({rp::dsp::BiquadType::LowPass, 22050.0f, frequencyOf(k), 1.0f, 0.0f});
        fresh.process(in.data() + k * kBlock, block.data(), kBlock);
        INFO("recovery block " << k);
        CHECK(firstDifference(block, std::vector<float>(got.begin() + k * kBlock, got.begin() + (k + 1) * kBlock)) ==
              -1);
    }
}

TEST_CASE("rp.dsp.dither through a plan is its kernel's quantised levels, whole or split any way", "[dsp-nodes]") {
    const std::vector<float> in = signal(kFrames);
    struct Case {
        const char*              json;
        rp::dsp::DitherAlgorithm algorithm;
        int                      bits;
        std::uint32_t            seed;
    };
    const Case cases[] = {
        {R"({"channels":1,"algorithm":"high-pass-tpdf","bits":4,"seed":12345})",
         rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 12345},
        {R"({"channels":1,"algorithm":"sierra-lite","bits":8,"seed":1})", rp::dsp::DitherAlgorithm::SierraLite, 8, 1},
        {R"({"channels":1,"algorithm":"none","bits":16})", rp::dsp::DitherAlgorithm::None, 16, 0x9e3779b9u},
    };

    for (const Case& c : cases) {
        for (const auto& split : kSplits) {
            INFO("case " << c.json << ", split starting " << split[0]);
            DspRig     rig;
            const auto sample = rig.buffer(in, 1);
            REQUIRE(rig.play(sample, "rp.dsp.dither", c.json));
            const std::vector<float> out = rig.render(kFrames, split)[0];

            rp::dsp::Dither kernel;
            kernel.init(c.algorithm, c.bits, c.seed);
            std::vector<float> reference(kFrames);
            inBlocks(kFrames, split, [&](std::size_t at, std::size_t n) {
                kernel.processLevels(in.data() + at, reference.data() + at, n);
            });
            CHECK(firstDifference(reference, out) == -1);
        }
    }
}

TEST_CASE("rp.dsp.dither's output is audio on the kernel's levels", "[dsp-nodes]") {
    // 4 bits, no dither: every sample is one of the sixteen levels, the kit decoder's, rather than a code.
    const std::vector<float> in = signal(kFrames);
    DspRig                   rig;
    const auto sample = rig.buffer(in, 1);
    REQUIRE(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"algorithm":"none","bits":4})"));
    const std::vector<float> out = rig.render(kFrames, {kFrames})[0];
    for (const float v : out) {
        const int code = static_cast<int>(std::round((v + 1.0f) * 0.5f * 15.0f));
        CHECK(v == rp::dsp::codeToLevel(static_cast<std::uint32_t>(code), 4));
    }
    CHECK(std::any_of(out.begin(), out.end(), [](float v) { return v < -0.9f; }));  // the overdriven parts reach the
    CHECK(std::any_of(out.begin(), out.end(), [](float v) { return v > 0.9f; }));   // outermost levels
}

TEST_CASE("rp.dsp.dither gives each channel its own noise", "[dsp-nodes]") {
    // Every channel seeded from the one setting would add the same TPDF sequence to left and right: correlated noise
    // sums coherently, up to 6 dB louder than decorrelated dither and dead centre instead of spread. Identical input
    // on both channels leaves the difference the noise, and channel 0 keeps the setting's own seed.
    const std::vector<float> in = signal(512);
    DspRig                   rig;
    const auto               sample = rig.buffer(in, 2);
    REQUIRE(rig.play(sample, "rp.dsp.dither",
                     R"({"channels":2,"algorithm":"high-pass-tpdf","bits":8,"seed":12345})", 2));
    const auto out = rig.render(512, {64});

    rp::dsp::Dither first;
    first.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 8, 12345);
    std::vector<float> reference(512);
    first.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out[0]) == -1);

    // Channel 1 is the setting's seed plus the node's golden-ratio step, pinned here: if that constant moves, this
    // test moves rather than every multi-channel patch's noise changing silently.
    rp::dsp::Dither second;
    second.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 8, 12345u + 0x9e3779b9u);
    second.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out[1]) == -1);

    // The second channel is its own stream, not the first one's. Two independent TPDF noises move a sample's code
    // apart only when they straddle a rounding boundary, which is about a quarter of them; two channels seeded alike
    // move it apart on none.
    std::size_t differing = 0;
    for (std::size_t i = 0; i < out[0].size(); ++i)
        if (std::memcmp(&out[0][i], &out[1][i], sizeof(float)) != 0) ++differing;
    CHECK(differing > 64);
}

TEST_CASE("rp.dsp.dither restarts a channel whose input is not finite", "[dsp-nodes]") {
    // A non-finite input sample latches the stateful algorithms' state. A NaN poisons every form's: the error terms
    // and the shaping state are differences of it (and the kernel reads a NaN code as 0, so the channel would sit at
    // -1 for good). An infinity poisons only the diffusion forms' `past[]`, which holds `value - q` of the unclamped
    // value; the TPDF and error-diffusion forms clamp the value before deriving their state from it, so a `+inf`
    // sample there just quantises to full scale and leaves them running. Either way the node restarts such a channel's
    // state from the settings after the block it appeared in, so one bad sample costs one block.
    struct Case {
        float                    bad;
        const char*              json;
        rp::dsp::DitherAlgorithm algorithm;
    };
    const Case cases[] = {
        {std::numeric_limits<float>::quiet_NaN(),
         R"({"channels":1,"algorithm":"high-pass-tpdf","bits":4,"seed":8})", rp::dsp::DitherAlgorithm::HighPassTPDF},
        {std::numeric_limits<float>::quiet_NaN(), R"({"channels":1,"algorithm":"jjn","bits":4,"seed":8})",
         rp::dsp::DitherAlgorithm::JJN},
        {std::numeric_limits<float>::quiet_NaN(), R"({"channels":1,"algorithm":"shaped-tpdf","bits":4,"seed":8})",
         rp::dsp::DitherAlgorithm::ShapedTPDF},
        {std::numeric_limits<float>::quiet_NaN(), R"({"channels":1,"algorithm":"error-diffusion","bits":4,"seed":8})",
         rp::dsp::DitherAlgorithm::ErrorDiffusion},
        {std::numeric_limits<float>::quiet_NaN(), R"({"channels":1,"algorithm":"sierra-lite","bits":4,"seed":8})",
         rp::dsp::DitherAlgorithm::SierraLite},
        {std::numeric_limits<float>::infinity(), R"({"channels":1,"algorithm":"jjn","bits":4,"seed":8})",
         rp::dsp::DitherAlgorithm::JJN},
        {std::numeric_limits<float>::infinity(), R"({"channels":1,"algorithm":"sierra-lite","bits":4,"seed":8})",
         rp::dsp::DitherAlgorithm::SierraLite},
    };
    for (const Case& c : cases) {
        INFO(c.json << " with " << c.bad);
        std::vector<float> in = signal(512);
        in[200] = c.bad;
        DspRig     rig;
        const auto sample = rig.buffer(in, 1);
        REQUIRE(rig.play(sample, "rp.dsp.dither", c.json));
        const std::vector<float> out = rig.render(512, {64})[0];
        for (const float v : out) CHECK(std::isfinite(v));  // the levels are finite whatever the input is

        // The bad sample is in block 3 (frames 192..255), so block 4 on is a fresh kernel's stream over the input from
        // there. A state left latched gives -1 (NaN) or +1 (infinity) there for the rest of the plan.
        rp::dsp::Dither fresh;
        fresh.init(c.algorithm, 4, 8);
        std::vector<float> reference(256);
        fresh.processLevels(in.data() + 256, reference.data(), 256);
        CHECK(firstDifference(reference, std::vector<float>(out.begin() + 256, out.end())) == -1);
    }
}

TEST_CASE("rp.dsp.dither's seed setting is its noise: a new seed restarts the stream", "[dsp-nodes]") {
    DspRig     rig;
    const std::vector<float> in = signal(kFrames);
    const auto sample = rig.buffer(in, 1);
    REQUIRE(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"algorithm":"high-pass-tpdf","bits":4,"seed":7})"));
    const std::vector<float> first = rig.render(256, {256})[0];

    rp::dsp::Dither seeded;
    seeded.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 7);
    std::vector<float> reference(256);
    seeded.processLevels(in.data(), reference.data(), 256);
    CHECK(firstDifference(reference, first) == -1);

    // The same document with another seed is a commit the node restarts the noise on, so the plan's stream from there
    // is a fresh kernel's with that seed. The player keeps its place across the swap, so the input is the next frames.
    REQUIRE(rig.set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
                                              "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                                    {"id":"x","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf","bits":4,"seed":99}},
                                    {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                                  "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})")
                .ok);
    const std::vector<float> second = rig.render(256, {256})[0];
    rp::dsp::Dither other;
    other.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 99);
    other.processLevels(in.data() + 256, reference.data(), 256);
    CHECK(firstDifference(reference, second) == -1);
    CHECK(firstDifference(first, second) != -1);  // and a different seed is different noise
}

TEST_CASE("rp.dsp's plan allocates and frees nothing, counted from its first block", "[dsp-nodes]") {
    DspRig     rig;
    const std::vector<float> in = signal(1024);
    const auto sample = rig.buffer(in, 2);
    const std::string doc =
        R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":2,"layout":"stereo"},
                       "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                      {"id":"g","type":"rp.dsp.gain","settings":{"channels":2}},
                      {"id":"b","type":"rp.dsp.biquad","settings":{"channels":2,"type":"peak"},
                       "params":{"frequency":1500,"q":0.9,"gainDb":5}},
                      {"id":"d","type":"rp.dsp.dither","settings":{"channels":2,"algorithm":"shaped-tpdf","bits":8,"seed":3}},
                      {"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                    "wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"b.in"},
                             {"from":"b.out","to":"d.in"},{"from":"d.out","to":"out.out.1-2"}]})";
    REQUIRE(rig.set(doc).ok);

    // Counted from the plan's first block, which is where a node that built rather than seeded something on the block
    // thread would allocate: the dither seeds its noise there, and that must not allocate either.
    rp::test::beginCountingAllocations();
    rig.run(64, 5);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
}

TEST_CASE("each rp.dsp type's descriptor is its golden", "[dsp-nodes][dsp-resample]") {
    const char* update = std::getenv("RP_GRAPH_GOLDEN_UPDATE");
    for (const rp_package* pkg : dspPackages()) {
        REQUIRE(pkg);
        REQUIRE(pkg->node_type_count > 0);
        for (std::uint32_t i = 0; i < pkg->node_type_count; ++i) {
            const rp_node_type* type = pkg->node_types[i];
            const std::string   json = type->descriptor(type);
            INFO(type->id);
            REQUIRE_FALSE(json.empty());
            const std::string path = std::string(RP_GRAPH_GOLDEN_DIR) + "/" + type->id + ".descriptor.json";
            if (update) std::ofstream(path) << json << "\n";  // recorded, then compared below like any other golden
            CHECK(json == readFile(path));
            REQUIRE(rp::graph::parseDescriptor(json));  // and the strict parser accepts it unchanged
        }
    }
}

TEST_CASE("the three rp.dsp nodes plan on the Pico", "[dsp-nodes]") {
    // This package's types alone: the resampler's node is the one that declares heap, and the planner refuses it on the
    // Pico. The parsed descriptor's flags are the ones the planner reads, so a field added to `requires` cannot break
    // this case for a formatting reason.
    for (std::uint32_t i = 0; i < rp::graph::dspPackage()->node_type_count; ++i) {
        const rp_node_type* type = rp::graph::dspPackage()->node_types[i];
        INFO(type->id);
        auto parsed = rp::graph::parseDescriptor(type->descriptor(type));
        REQUIRE(parsed);
        CHECK_FALSE(parsed->heap);
        CHECK_FALSE(parsed->threads);
    }

    DspRig     pico(rp::graph::Target::Pico);
    const auto sample = pico.buffer(signal(1024), 2);
    const std::string doc =
        R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":2,"layout":"stereo"},
                       "resources":{"sample":)" + DspRig::res(sample) + R"(}},
                      {"id":"g","type":"rp.dsp.gain","settings":{"channels":2}},
                      {"id":"b","type":"rp.dsp.biquad","settings":{"channels":2,"type":"high-shelf"}},
                      {"id":"d","type":"rp.dsp.dither","settings":{"channels":2,"algorithm":"jjn","bits":6,"seed":5}},
                      {"id":"out","type":"rp.host.audio-out","settings":{"buses":[2]}}],
                    "wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"b.in"},
                             {"from":"b.out","to":"d.in"},{"from":"d.out","to":"out.out.1-2"}]})";
    const auto r = pico.set(doc);
    INFO(GraphRig::codes(r));
    CHECK(r.ok);
    pico.run(64);
}

TEST_CASE("rp.dsp settings are validated with a reason", "[dsp-nodes]") {
    DspRig     rig;
    const auto sample = rig.buffer(signal(64), 1);
    // A channel count is a document's, and the extra memory is sized from it, so it is refused rather than clamped.
    for (const char* settings : {R"({"channels":0})", R"({"channels":65})"}) {
        INFO(settings);
        const auto r = rig.plan(sample, "rp.dsp.gain", settings);
        REQUIRE_FALSE(r.ok);
        REQUIRE_FALSE(r.errors.empty());
        CHECK(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("between 1 and 64") != std::string::npos);
    }
    // The bit depth is the kernel's 1..16, and a label outside the schema's set is refused when the document is read.
    for (const char* settings : {R"({"channels":1,"bits":0})", R"({"channels":1,"bits":17})"}) {
        INFO(settings);
        const auto r = rig.plan(sample, "rp.dsp.dither", settings);
        REQUIRE_FALSE(r.ok);
        REQUIRE_FALSE(r.errors.empty());
        CHECK(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("between 1 and 16") != std::string::npos);
    }
    CHECK_FALSE(rig.play(sample, "rp.dsp.biquad", R"({"channels":1,"type":"mellotron"})"));
    CHECK(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"bits":16})"));
}

// The rp.dsp package (src/graph/nodes/DspNodes.cpp, docs/modular-graph-phase-d1-design.md section 5): gain, biquad
// and dither as graph nodes, through real plans built by the graph rig, held to the rp::dsp kernels they wrap.
//
// - Each node's output through a plan equals the kernel run directly over the same signal, bit for bit, and any split
//   of the stream into blocks (1, 7, 64, 1024 frames and a ragged mix) gives the same stream: a node carries its
//   kernel's state across blocks exactly as the kernel does.
// - A gain that moved ramps across one block and lands on the new value; a held one is the kernel's exact multiply,
//   from the plan's first block on. The biquad's coefficients follow a sweep at the next block while its state, which
//   is what a reset would throw away, is kept: a change of type recomputes them and clears the state, which is a fresh
//   filter's stream.
// - A dither's settings are its stream's: a new seed restarts the noise, and the node's output is the kernel's
//   quantised levels, so a live patch and a kit compile run the same arithmetic.
// - A warmed plan block allocates and frees nothing (the suite's counting allocator), and all three types plan on the
//   Pico: they declare neither heap nor threads, and their extra memory is requested for the settings' channel count
//   (the request's size is the assertion), never for the descriptor's maximum.
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

namespace {

using rp::graph::test::GraphRig;

constexpr std::size_t kFrames = 4096;

// The packages whose types this file's descriptor goldens are. A package id registers once, so the resampler's type is a
// second package of its own (`rp.dsp.resample`) rather than more types in this one; the package that adds it is
// appended here, and this list is what a case walking "every rp.dsp type's descriptor" walks.
std::vector<const rp_package*> dspPackages() { return {rp::graph::dspPackage()}; }

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
 *  whole stream: GraphRig sizes its host output rows from the same number. */
struct DspRig : GraphRig {
    explicit DspRig(rp::graph::Target target = rp::graph::Target::Desktop, std::uint32_t maxFrames = 4096)
        : GraphRig({48000.0, maxFrames, target}, 8, nullptr, maxFrames) {}

    // An audio buffer resource holding one stream per channel.
    rp::graph::ResourceTable::Id buffer(const std::vector<std::vector<float>>& perChannel) {
        const auto channels = static_cast<std::uint32_t>(perChannel.size());
        const auto id = resources.createAudioBuffer(channels, static_cast<std::uint32_t>(perChannel[0].size()), 48000.0);
        auto* const b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c) std::copy(perChannel[c].begin(), perChannel[c].end(), b->data[c]);
        return id;
    }

    // An audio buffer resource holding `samples` on every one of its channels.
    rp::graph::ResourceTable::Id buffer(const std::vector<float>& samples, std::uint32_t channels) {
        return buffer(std::vector<std::vector<float>>(channels, samples));
    }

    static std::string res(rp::graph::ResourceTable::Id id) { return "\"res:" + std::to_string(id) + "\""; }

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

TEST_CASE("a warmed rp.dsp plan block allocates and frees nothing", "[dsp-nodes]") {
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
    rig.run(64);  // no warm-up beyond the first block, where the dither seeds its noise: that must not allocate either

    rp::test::beginCountingAllocations();
    rig.run(64, 4);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
}

TEST_CASE("each rp.dsp type's descriptor is its golden", "[dsp-nodes]") {
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
    // Pico.
    for (std::uint32_t i = 0; i < rp::graph::dspPackage()->node_type_count; ++i) {
        const rp_node_type* type = rp::graph::dspPackage()->node_types[i];
        const std::string   json = type->descriptor(type);
        INFO(type->id);
        CHECK(json.find(R"("requires":{"heap":false,"threads":false})") != std::string::npos);
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

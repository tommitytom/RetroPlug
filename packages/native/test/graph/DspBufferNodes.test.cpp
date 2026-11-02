// The rp.dsp nodes that read a whole buffer before they play one (src/graph/nodes/DspNodes.cpp,
// docs/modular-graph-phase-d2-design.md section 4): rp.dsp.normalize, rp.dsp.dpcm and rp.dsp.dither's `seedFrom`,
// through real plans built by the graph rig, held to the kit path they replace.
//
// - rp.dsp.normalize's output is today's normalising `GainEffect` through `rp::lsdj::processEffect`, bit for bit, at
//   block sizes of 1, 7, 64 and 1024 frames: a sine, a source so quiet its normalised gain is far above any param's
//   range, a silence (the gain alone) and a negative gain. Its peak is the channels it plays, and one gain scales them.
// - A dither's `seedFrom` gives channel 0 the seed `rp::lsdj::processEffect(DitherEffect)` hashes out of the same
//   buffer, for each of the five algorithms, and the other channels their own derivation of it; without one the seed
//   setting stands as it did.
// - rp.dsp.dpcm streams the kernel's delta bits as +1 / -1 audio, sample for sample, normalising or not, at every
//   split.
// - A warmed plan block allocates and frees nothing counted from the plan's first block, the dither's hash and the
//   DPCM bits being built at activate, on the control thread.
// - A settings commit is each node's own: a normalise re-folds a new gain against the peak activate measured, a dither
//   with `seedFrom` restarts a committed seed from the buffer's hash rather than from the setting, and a DPCM
//   `normalize` toggle is a rebuild that re-encodes from the first bit.
// - Each type refuses a document it cannot honour (no buffer named, a resource that is not a live audio buffer), and
//   rp.dsp.dpcm refuses a multi-channel buffer at activate. The DPCM node is the package's one type that declares
//   `heap`, so the Pico refuses it; rp.dsp.normalize allocates nothing and plans there.
//
// The two types' descriptor goldens are DspNodes.test.cpp's case, which walks every type of this package and carries
// the tag `[dsp-nodes]`; this file's tag names only the cases below.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "CountingAllocator.hpp"
#include "GraphRig.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Dpcm.hpp"
#include "dsp/Gain.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "lsdj/Effects.hpp"

namespace {

using rp::graph::test::GraphRig;

constexpr double kRate = 48000.0;

// The block sizes a stream is split into: one frame, an odd size, a host block, and the largest block these plans run.
const std::vector<std::vector<std::size_t>> kSplits = {{1}, {7}, {64}, {1024}};

/** A sine, overdriven past [-1, 1] when `amplitude` says so, with a little noise (scaled with it) so no two samples
 *  repeat a code. */
std::vector<float> signal(std::size_t frames, float amplitude = 1.1f) {
    std::vector<float> out(frames);
    std::uint32_t      rng = 0x1234567u;
    for (std::size_t i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 0.05f * amplitude;
        out[i] = noise + amplitude * std::sin(static_cast<float>(i) * 0.031f);
    }
    return out;
}

/** The first index two streams differ at, bit for bit, -1 for none, or a value no index can be for unequal lengths. */
std::ptrdiff_t firstDifference(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<std::ptrdiff_t>::max();
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::memcmp(&a[i], &b[i], sizeof(float)) != 0) return static_cast<std::ptrdiff_t>(i);
    return -1;
}

/** A float written into a document exactly: `std::to_string` prints six decimals, which would round a gain like
 *  1e-7 to 0, and a setting must reach the node as the case wrote it. */
std::string number(float v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.9g", static_cast<double>(v));
    return text;
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

/** A rig whose plans may run blocks up to `maxFrames` frames, with a helper for the audio buffers these nodes name. */
struct BufferRig : GraphRig {
    explicit BufferRig(rp::graph::Target target = rp::graph::Target::Desktop, std::uint32_t maxFrames = 2048)
        : GraphRig({kRate, maxFrames, target}, 8, nullptr, maxFrames) {}

    // An audio buffer resource holding one stream per channel, at the plan's rate (these nodes ignore a buffer's rate:
    // they play frames).
    rp::graph::ResourceTable::Id buffer(const std::vector<std::vector<float>>& perChannel) {
        const auto channels = static_cast<std::uint32_t>(perChannel.size());
        // Every channel is `frames` long, as a buffer's are: a longer one would overwrite the next channel's
        // samples, or write past the last, rather than fail here.
        const auto frames = static_cast<std::uint32_t>(perChannel[0].size());
        for (const auto& channel : perChannel) REQUIRE(channel.size() == frames);
        const auto id = resources.createAudioBuffer(channels, frames, kRate);
        auto* const b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c) std::copy(perChannel[c].begin(), perChannel[c].end(), b->data[c]);
        return id;
    }

    // An audio buffer resource holding `samples` on every one of its channels.
    rp::graph::ResourceTable::Id buffer(const std::vector<float>& samples, std::uint32_t channels = 1) {
        return buffer(std::vector<std::vector<float>>(channels, samples));
    }

    static std::string res(rp::graph::ResourceTable::Id id) { return "\"res:" + std::to_string(id) + "\""; }

    /** Every host channel of the plan's output over `frames`, run in the block sizes `split` cycles through. */
    std::vector<std::vector<float>> render(std::size_t frames, const std::vector<std::size_t>& split,
                                           std::uint32_t channels) {
        std::vector<std::vector<float>> out(channels);
        for (auto& channel : out) channel.reserve(frames);
        inBlocks(frames, split, [&](std::size_t, std::size_t n) {
            run(static_cast<std::uint32_t>(n));
            for (std::uint32_t c = 0; c < channels; ++c)
                out[c].insert(out[c].end(), outs[c].begin(), outs[c].begin() + static_cast<std::ptrdiff_t>(n));
        });
        return out;
    }

    /** One channel of the host output, this block. */
    std::vector<float> block(std::uint32_t channel, std::size_t frames) {
        return std::vector<float>(outs[channel].begin(), outs[channel].begin() + static_cast<std::ptrdiff_t>(frames));
    }

    /** The plan naming `node` with `settings`, its output on the host's first `channels` channels. */
    rp::graph::SetGraphResult source(const std::string& node, const std::string& settings, std::uint32_t channels) {
        const std::string port = channels == 1 ? "out.1" : "out.1-" + std::to_string(channels);
        return set(R"({"nodes":[{"id":"x","type":")" + node + R"(","settings":)" + settings +
                   R"(},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + std::to_string(channels) +
                   R"(]}}],"wires":[{"from":"x.out","to":"out.)" + port + R"("}]})");
    }

    /** The plan playing `sample` into `node` with `settings`, on to the host's first `channels` channels. */
    rp::graph::SetGraphResult play(rp::graph::ResourceTable::Id sample, const std::string& node,
                                   const std::string& settings, std::uint32_t channels) {
        const std::string layout = channels == 1 ? "mono" : "stereo";
        const std::string port = channels == 1 ? "out.1" : "out.1-" + std::to_string(channels);
        return set(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":)" +
                   std::to_string(channels) + R"(,"layout":")" + layout + R"(","loop":false},
                               "resources":{"sample":)" + res(sample) + R"(}},
                   {"id":"x","type":")" + node + R"(","settings":)" + settings +
                   R"(},{"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" + std::to_string(channels) +
                   R"(]}}],"wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.)" + port + R"("}]})");
    }
};

/** `count` frames of the normalising `GainEffect` over `in`, then `tail` frames of silence: what a normalising player
 *  of the same buffer, run `count + tail` frames, has to give. */
std::vector<float> normalised(const std::vector<float>& in, float gain, std::size_t tail) {
    std::vector<float> out = in;
    rp::lsdj::processEffect(rp::lsdj::GainEffect{true, gain}, out, static_cast<float>(kRate));
    out.resize(out.size() + tail, 0.0f);
    return out;
}

}  // namespace

TEST_CASE("rp.dsp.normalize is the kit path's normalising GainEffect, whole or split any way", "[dsp-buffer-nodes]") {
    // A sine; a source so quiet that normalising it multiplies by about 700, far above any param range a document's
    // value would survive; silence, where only the scale shows at all; and a phase flip. The silence case's gain is
    // negative on purpose: 0 * -0.7 is -0.0, which differs from +0 bit for bit, so the case pins the scale where any
    // positive one would give the same +0.
    struct Case {
        const char*        what;
        std::vector<float> in;
        float              gain;
    };
    const std::vector<Case> cases = {
        {"a sine", signal(1024), 0.5f},
        {"a quiet source", signal(1024, 0.0013f), 1.0f},
        {"silence", std::vector<float>(1024, 0.0f), -0.7f},
        {"a negative gain", signal(1024), -0.8f},
    };
    constexpr std::size_t kTail = 137;

    for (const Case& c : cases) {
        INFO(c.what << ", gain " << c.gain);
        // The kit path's own call: the whole buffer, the peak folded into the gain by rp::dsp::normalizedGain.
        const std::vector<float> reference = normalised(c.in, c.gain, kTail);
        for (const auto& split : kSplits) {
            INFO("split starting " << split[0]);
            BufferRig  rig;
            const auto sample = rig.buffer(c.in);
            REQUIRE(rig.source("rp.dsp.normalize",
                               R"({"buffer":)" + BufferRig::res(sample) + R"(,"gain":)" + number(c.gain) +
                                   R"(,"channels":1})",
                               1)
                        .ok);
            const std::vector<float> out = rig.render(c.in.size() + kTail, split, 1)[0];
            CHECK(firstDifference(reference, out) == -1);
        }
    }

    // The quiet source really is past every param's range once normalised, so this is not a case rp.dsp.gain could have
    // carried: 1.0 / peak of a 0.0013-amplitude sine, where the widest param bound is 100.
    CHECK(rp::dsp::normalizedGain(1.0f, rp::dsp::peak(cases[1].in.data(), cases[1].in.size())) > 100.0f);
}

TEST_CASE("rp.dsp.normalize's peak is the channels it plays, and one gain scales them alike", "[dsp-buffer-nodes]") {
    const std::vector<float> left = signal(512, 0.2f), right = signal(512, 0.7f);
    const float              peak = std::max(rp::dsp::peak(left.data(), left.size()),
                                             rp::dsp::peak(right.data(), right.size()));
    // The louder channel's peak, not each channel's own: a per-channel normalise would put both channels at the same
    // amplitude, where one gain over the mix keeps their balance.
    const float scale = rp::dsp::normalizedGain(0.6f, peak);
    REQUIRE(scale == 0.6f * (1.0f / rp::dsp::peak(right.data(), right.size())));

    BufferRig  rig;
    const auto sample = rig.buffer({left, right});
    REQUIRE(rig.source("rp.dsp.normalize",
                       R"({"buffer":)" + BufferRig::res(sample) + R"(,"gain":0.6,"channels":2})", 2)
                .ok);
    const std::vector<std::vector<float>> out = rig.render(512, {64}, 2);

    for (std::uint32_t channel = 0; channel < 2; ++channel) {
        INFO("channel " << channel);
        const std::vector<float>& source = channel == 0 ? left : right;
        rp::dsp::Gain             kernel;
        kernel.init(scale);
        std::vector<float> reference(source.size());
        kernel.process(source.data(), reference.data(), source.size());
        CHECK(firstDifference(reference, out[channel]) == -1);
    }

    // A layout channel the buffer lacks is silence, as the player's.
    BufferRig  mono;
    const std::vector<float> quiet = signal(256, 0.4f);
    const auto one = mono.buffer(quiet, 1);
    REQUIRE(mono.source("rp.dsp.normalize",
                        R"({"buffer":)" + BufferRig::res(one) + R"(,"gain":1,"channels":2})", 2)
                .ok);
    const auto pair = mono.render(256, {64}, 2);
    rp::dsp::Gain alone;
    alone.init(rp::dsp::normalizedGain(1.0f, rp::dsp::peak(quiet.data(), quiet.size())));
    std::vector<float> reference(256);
    alone.process(quiet.data(), reference.data(), 256);
    CHECK(firstDifference(reference, pair[0]) == -1);
    CHECK(firstDifference(pair[1], std::vector<float>(256, 0.0f)) == -1);

    // Only the channels the node plays are in the peak: `channels: 1` over a stereo buffer takes the played channel's
    // own, so a quiet channel is not scaled by the loud one it never plays.
    BufferRig  stereoRig;
    const auto wide = stereoRig.buffer({left, right});
    REQUIRE(stereoRig.source("rp.dsp.normalize",
                             R"({"buffer":)" + BufferRig::res(wide) + R"(,"gain":0.6,"channels":1})", 1)
                .ok);
    const std::vector<float> narrow = stereoRig.render(512, {64}, 1)[0];
    rp::dsp::Gain             played;
    played.init(rp::dsp::normalizedGain(0.6f, rp::dsp::peak(left.data(), left.size())));
    std::vector<float> playedReference(left.size());
    played.process(left.data(), playedReference.data(), left.size());
    CHECK(firstDifference(playedReference, narrow) == -1);
    // The unplayed channel's scale is the other answer, and it is a different stream.
    rp::dsp::Gain whole;
    whole.init(scale);
    whole.process(left.data(), playedReference.data(), left.size());
    CHECK(firstDifference(playedReference, narrow) != -1);
}

TEST_CASE("rp.dsp.normalize re-folds a committed gain against the peak its activation measured", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    const float              peak = rp::dsp::peak(in.data(), in.size());
    const auto               document = [](const std::string& buffer, float gain) {
        return R"({"nodes":[{"id":"x","type":"rp.dsp.normalize","settings":{"buffer":)" + buffer +
               R"(,"gain":)" + number(gain) + R"(,"channels":1}},
                          {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                 "wires":[{"from":"x.out","to":"out.out.1"}]})";
    };

    BufferRig  rig;
    const auto sample = rig.buffer(in);
    const auto buffer = BufferRig::res(sample);
    REQUIRE(rig.set(document(buffer, 0.5f)).ok);
    rp_node* const instance = rig.host->node("x");
    REQUIRE(instance);
    const std::vector<float> first = rig.render(128, {64}, 1)[0];

    // A plan replacement commits the new gain to the live instance rather than re-activating it, so the peak activate
    // measured stands and only the fold is redone; the same instance proves it was a swap and not a rebuild, and the
    // position carries on, so the stream continues at frame 128.
    REQUIRE(rig.set(document(buffer, -3.5f)).ok);
    CHECK(rig.host->node("x") == instance);
    rp::test::beginCountingAllocations();
    rig.run(64);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
    const std::vector<float> second = rig.block(0, 64);

    const float        scale = rp::dsp::normalizedGain(-3.5f, peak);
    const float        oldScale = rp::dsp::normalizedGain(0.5f, peak);
    std::vector<float> expected(64), unchanged(64);
    for (std::size_t i = 0; i < expected.size(); ++i) {
        expected[i] = in[128 + i] * scale;
        unchanged[i] = in[128 + i] * oldScale;
    }
    CHECK(firstDifference(expected, second) == -1);
    CHECK(firstDifference(unchanged, second) != -1);  // the committed gain really moved the scale
    // And the stream is not the old one carried on at the new scale: `first` is the same frames at the gain before it.
    CHECK(firstDifference(std::vector<float>(first.begin(), first.begin() + 64), second) != -1);
}

TEST_CASE("a dither's seedFrom gives channel 0 the kit path's seed, for every algorithm", "[dsp-buffer-nodes]") {
    struct Case {
        const char*          label;
        rp::lsdj::DitherType type;
        bool                 tpdf;  // one of the two forms that draw noise, so `seedFrom` decides its codes
    };
    const Case cases[] = {
        {"high-pass-tpdf", rp::lsdj::DitherType::HighPassTPDF, true},
        {"shaped-tpdf", rp::lsdj::DitherType::ShapedTPDF, true},
        {"error-diffusion", rp::lsdj::DitherType::ErrorDiffusion, false},
        {"jjn", rp::lsdj::DitherType::JJN, false},
        {"sierra-lite", rp::lsdj::DitherType::SierraLite, false},
    };
    const std::vector<float> in = signal(1024);

    for (const Case& c : cases) {
        INFO(c.label);
        // The kit path's quantise: the same buffer, hashed whole by rp::dsp::inputSeed and quantised at 4 bits.
        std::vector<float> reference = in;
        rp::lsdj::processEffect(rp::lsdj::DitherEffect{c.type}, reference, static_cast<float>(kRate));

        BufferRig  rig;
        const auto sample = rig.buffer(in);  // the buffer the dither plays and hashes: one resource, named twice
        // The seed setting is a different word on purpose: with `seedFrom` set it is not what seeds the noise.
        const std::string settings = R"({"channels":1,"algorithm":")" + std::string(c.label) +
                                     R"(","bits":4,"seed":17,"seedFrom":)" + BufferRig::res(sample) + "}";
        REQUIRE(rig.play(sample, "rp.dsp.dither", settings, 1).ok);
        const std::vector<float> out = rig.render(in.size(), {64}, 1)[0];
        REQUIRE(out.size() == reference.size());

        // The node's levels are the codes' levels (rp::dsp::codeToLevel), and the code recovered from a level is the
        // code itself, so this is the kit path's code stream and not merely another one at the same levels.
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto code = static_cast<std::uint32_t>(reference[i]);
            INFO("frame " << i);
            REQUIRE(out[i] == rp::dsp::codeToLevel(code, 4));
            CHECK(static_cast<std::uint32_t>(std::round((out[i] + 1.0f) * 0.5f * 15.0f)) == code);
        }

        // Three of the five forms draw no noise at all (the kernel's init salts only the TPDF forms), so for those
        // whatever seeds them they give the same codes and the levels above are all the case can pin. The two that do
        // draw noise are held to the hash by a control: the same document without `seedFrom`, seeded with the 17 the
        // settings name, gives a different stream.
        if (!c.tpdf) continue;
        BufferRig        seeded;
        const auto       seededSample = seeded.buffer(in);
        const std::string withoutSeedFrom = R"({"channels":1,"algorithm":")" + std::string(c.label) +
                                            R"(","bits":4,"seed":17})";
        REQUIRE(seeded.play(seededSample, "rp.dsp.dither", withoutSeedFrom, 1).ok);
        const std::vector<float> other = seeded.render(in.size(), {64}, 1)[0];
        CHECK(firstDifference(out, other) != -1);
    }
}

TEST_CASE("a dither's seedFrom seeds the other channels the way the seed setting does", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    BufferRig                rig;
    const auto               sample = rig.buffer(in, 2);
    REQUIRE(rig.play(sample, "rp.dsp.dither",
                     R"({"channels":2,"algorithm":"high-pass-tpdf","bits":4,"seedFrom":)" + BufferRig::res(sample) +
                         "}",
                     2)
                .ok);
    const std::vector<std::vector<float>> out = rig.render(512, {64}, 2);

    // Channel 0 is the hash itself; channel 1 is that seed plus the node's golden-ratio step, as every channel past 0
    // is seeded from whatever word the stream started from.
    const std::uint32_t hashed = rp::dsp::inputSeed(in.data(), in.size());
    rp::dsp::Dither first;
    first.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, hashed);
    std::vector<float> reference(512);
    first.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out[0]) == -1);

    rp::dsp::Dither second;
    second.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, hashed + 0x9e3779b9u);
    second.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out[1]) == -1);
    CHECK(firstDifference(out[0], out[1]) != -1);
}

TEST_CASE("a dither without seedFrom keeps its seed setting", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    BufferRig                rig;
    const auto               sample = rig.buffer(in);
    REQUIRE(rig.play(sample, "rp.dsp.dither", R"({"channels":1,"algorithm":"sierra-lite","bits":4,"seed":12345})", 1)
                .ok);
    const std::vector<float> out = rig.render(512, {64}, 1)[0];

    rp::dsp::Dither kernel;
    kernel.init(rp::dsp::DitherAlgorithm::SierraLite, 4, 12345);
    std::vector<float> reference(512);
    kernel.processLevels(in.data(), reference.data(), 512);
    CHECK(firstDifference(reference, out) == -1);
}

TEST_CASE("a dither with seedFrom restarts a committed seed from the buffer's hash", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    const auto document = [](const std::string& buffer, std::uint32_t seed) {
        return R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,
                             "layout":"mono","loop":false},"resources":{"sample":)" + buffer + R"(}},
                         {"id":"x","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf",
                          "bits":4,"seed":)" + std::to_string(seed) + R"(,"seedFrom":)" + buffer + R"(}},
                         {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                "wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"out.out.1"}]})";
    };

    BufferRig  rig;
    const auto sample = rig.buffer(in);
    const auto buffer = BufferRig::res(sample);
    REQUIRE(rig.set(document(buffer, 7)).ok);
    const std::vector<float> first = rig.render(256, {64}, 1)[0];

    // The commit restarts the noise, and `seedFrom` still stands in the seed's place: the stream from there is a fresh
    // kernel's from the buffer's hash over the frames from 256, not one from 99 and not the old sequence carried on.
    // The player keeps its place across the swap, as it does without a seed change.
    REQUIRE(rig.set(document(buffer, 99)).ok);
    const std::vector<float> second = rig.render(256, {64}, 1)[0];

    std::vector<float> expected(256);
    rp::dsp::Dither     fromHash;
    fromHash.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, rp::dsp::inputSeed(in.data(), in.size()));
    fromHash.processLevels(in.data() + 256, expected.data(), expected.size());
    CHECK(firstDifference(expected, second) == -1);

    rp::dsp::Dither fromSetting;
    fromSetting.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, 99);
    fromSetting.processLevels(in.data() + 256, expected.data(), expected.size());
    CHECK(firstDifference(expected, second) != -1);
    CHECK(firstDifference(first, second) != -1);
}

TEST_CASE("rp.dsp.dpcm streams the kernel's delta bits as plus and minus one", "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(1024);
    constexpr std::size_t    kTail = 71;

    for (const bool normalize : {true, false}) {
        std::vector<std::uint8_t> bits(in.size());
        rp::dsp::encodeDpcm(in.data(), in.size(), normalize, bits.data());
        std::vector<float> reference(in.size() + kTail, 0.0f);
        for (std::size_t i = 0; i < in.size(); ++i) reference[i] = bits[i] ? 1.0f : -1.0f;
        // Both polarities are in the stream, so a node that wrote one level throughout would not pass by matching
        // the kernel's bits alone.
        REQUIRE(std::count(bits.begin(), bits.end(), std::uint8_t{1}) > 0);
        REQUIRE(std::count(bits.begin(), bits.end(), std::uint8_t{0}) > 0);

        for (const auto& split : kSplits) {
            INFO("normalize " << normalize << ", split starting " << split[0]);
            BufferRig  rig;
            const auto sample = rig.buffer(in);
            REQUIRE(rig.source("rp.dsp.dpcm",
                               R"({"buffer":)" + BufferRig::res(sample) + R"(,"normalize":)" +
                                   (normalize ? "true" : "false") + "}",
                               1)
                        .ok);
            CHECK(firstDifference(reference, rig.render(in.size() + kTail, split, 1)[0]) == -1);
        }
    }
}

TEST_CASE("rp.dsp.dpcm's normalize toggle rebuilds and plays the other encoding from the first bit",
          "[dsp-buffer-nodes]") {
    const std::vector<float> in = signal(512);
    const auto document = [](const std::string& buffer, bool normalize) {
        return R"({"nodes":[{"id":"x","type":"rp.dsp.dpcm","settings":{"buffer":)" + buffer +
               R"(,"normalize":)" + (normalize ? "true" : "false") + R"(}},
                          {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1]}}],
                 "wires":[{"from":"x.out","to":"out.out.1"}]})";
    };

    BufferRig  rig;
    const auto sample = rig.buffer(in);
    const auto buffer = BufferRig::res(sample);
    REQUIRE(rig.set(document(buffer, true)).ok);
    const std::vector<float> first = rig.render(512, {64}, 1)[0];

    // Re-encoding is a whole-buffer pass on the control thread, so a change of `normalize` is a rebuild: the fresh
    // instance encodes in activate and plays the new bit stream from its first bit, not the old stream carried on.
    REQUIRE(rig.set(document(buffer, false)).ok);
    const std::vector<float> second = rig.render(512, {64}, 1)[0];

    std::vector<std::uint8_t> bits(in.size());
    rp::dsp::encodeDpcm(in.data(), in.size(), false, bits.data());
    std::vector<float> expected(in.size());
    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] = bits[i] ? 1.0f : -1.0f;
    CHECK(firstDifference(expected, second) == -1);
    CHECK(firstDifference(first, second) != -1);  // and the two encodings are different streams
}

TEST_CASE("a re-activation carries the buffer-reading nodes on from where they were", "[dsp-buffer-nodes]") {
    // A host's first larger block restarts every live instance (GraphHost::reactivate), on the control thread, and
    // what that must not do is replay these nodes' buffers from their first frame. Each node is played, re-activated
    // to a larger block bound, and played on: the two halves together are the buffer played once, from frame 0.
    const std::vector<float> in = signal(512);
    constexpr std::size_t    kFirst = 100;

    {
        // normalize: the frames from kFirst on are the kit path's normalising gain over the same frames, so the
        // position carried on across the re-activation.
        std::vector<float> reference = in;
        rp::lsdj::processEffect(rp::lsdj::GainEffect{true, 0.5f}, reference, static_cast<float>(kRate));
        BufferRig  rig(rp::graph::Target::Desktop, 1024);
        const auto sample = rig.buffer(in);
        REQUIRE(rig.source("rp.dsp.normalize",
                           R"({"buffer":)" + BufferRig::res(sample) + R"(,"gain":0.5,"channels":1})", 1)
                    .ok);
        std::vector<float> stream = rig.render(kFirst, {64}, 1)[0];
        REQUIRE(rig.host->reactivate(kRate, 2048));
        const auto after = rig.render(in.size() - kFirst, {64}, 1)[0];
        stream.insert(stream.end(), after.begin(), after.end());
        CHECK(firstDifference(reference, stream) == -1);
    }

    {
        // dpcm: the same, one bit per frame, so its position carried on across the re-activation as well.
        std::vector<std::uint8_t> bits(in.size());
        rp::dsp::encodeDpcm(in.data(), in.size(), true, bits.data());
        std::vector<float> reference(in.size());
        for (std::size_t i = 0; i < reference.size(); ++i) reference[i] = bits[i] ? 1.0f : -1.0f;
        BufferRig  rig(rp::graph::Target::Desktop, 1024);
        const auto sample = rig.buffer(in);
        REQUIRE(rig.source("rp.dsp.dpcm", R"({"buffer":)" + BufferRig::res(sample) + R"(,"normalize":true})", 1)
                    .ok);
        std::vector<float> stream = rig.render(kFirst, {64}, 1)[0];
        REQUIRE(rig.host->reactivate(kRate, 2048));
        const auto after = rig.render(in.size() - kFirst, {64}, 1)[0];
        stream.insert(stream.end(), after.begin(), after.end());
        CHECK(firstDifference(reference, stream) == -1);
    }
}

TEST_CASE("the buffer-reading nodes' plan allocates and frees nothing from its first block", "[dsp-buffer-nodes]") {
    BufferRig  rig;
    const auto normalisedSource = rig.buffer(signal(1024, 0.9f));
    const auto dpcmSource = rig.buffer(signal(1024, 0.5f));
    const auto ditherSource = rig.buffer(signal(1024));
    REQUIRE(rig.set(R"({"nodes":[
                        {"id":"n","type":"rp.dsp.normalize","settings":{"buffer":)" +
                    BufferRig::res(normalisedSource) + R"(,"gain":0.3,"channels":1}},
                        {"id":"c","type":"rp.dsp.dpcm","settings":{"buffer":)" + BufferRig::res(dpcmSource) +
                    R"(}},
                        {"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
                         "resources":{"sample":)" + BufferRig::res(ditherSource) + R"(}},
                        {"id":"d","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf","bits":4,
                         "seedFrom":)" + BufferRig::res(ditherSource) + R"(}},
                        {"id":"out","type":"rp.host.audio-out","settings":{"buses":[1,1,1]}}],
                      "wires":[{"from":"n.out","to":"out.out.1"},{"from":"c.out","to":"out.out.2"},
                               {"from":"p.out","to":"d.in"},{"from":"d.out","to":"out.out.3"}]})")
                .ok);

    // Counted from the plan's first block, where a node that hashed or encoded inside `process` would allocate: the
    // dither's hash and the DPCM bits are built at activate, on the control thread.
    rp::test::beginCountingAllocations();
    rig.run(64, 6);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
}

TEST_CASE("the buffer-reading nodes refuse a document they cannot honour", "[dsp-buffer-nodes]") {
    BufferRig rig;

    struct Refusal {
        const char* node;
        const char* noBuffer;
        const char* missingBuffer;
    };
    for (const Refusal& c : {Refusal{"rp.dsp.normalize", R"({"channels":1})", R"({"buffer":"res:9999","channels":1})"},
                             Refusal{"rp.dsp.dpcm", "{}", R"({"buffer":"res:9999"})"}}) {
        INFO(c.node);
        // No buffer named: a settings error with a reason, since a normalise or a DPCM without one is silence nobody
        // asked for rather than a document the node can play.
        const auto none = rig.source(c.node, c.noBuffer, 1);
        REQUIRE_FALSE(none.ok);
        REQUIRE_FALSE(none.errors.empty());
        CHECK(none.errors[0].code == "settings-invalid");
        CHECK(none.errors[0].message.find("names an audio buffer") != std::string::npos);

        // A ref that names nothing live is refused when the instance is created, with the field's name.
        const auto missing = rig.source(c.node, c.missingBuffer, 1);
        REQUIRE_FALSE(missing.ok);
        REQUIRE_FALSE(missing.errors.empty());
        CHECK(missing.errors[0].message.find("buffer") != std::string::npos);
    }

    // A resource of another type is refused the same way.
    std::string why;
    const auto  bytes = rig.resources.createBytes("rom", {1, 2, 3}, &why);
    REQUIRE(bytes != 0);
    const auto wrong = rig.source("rp.dsp.normalize", R"({"buffer":)" + BufferRig::res(bytes) + R"(,"channels":1})", 1);
    REQUIRE_FALSE(wrong.ok);
    CHECK(wrong.errors[0].message.find("buffer") != std::string::npos);

    // A channel count is a document's, and the port's layout is labelled from it, so an impossible one is refused
    // rather than clamped.
    const auto live = rig.buffer(signal(64));
    for (const std::string& settings :
         {R"({"buffer":)" + BufferRig::res(live) + R"(,"channels":0})",
          R"({"buffer":)" + BufferRig::res(live) + R"(,"channels":65})"}) {
        INFO(settings);
        const auto r = rig.source("rp.dsp.normalize", settings, 1);
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errors[0].code == "settings-invalid");
        CHECK(r.errors[0].message.find("between 1 and 64") != std::string::npos);
    }

    // A DPCM node is one DMC channel's: a stereo buffer fails the activation, with a reason, rather than mixing or
    // dropping a channel silently.
    const auto stereo = rig.buffer({signal(64), signal(64, 0.5f)});
    const auto refused =
        rig.source("rp.dsp.dpcm", R"({"buffer":)" + BufferRig::res(stereo) + R"(,"normalize":true})", 1);
    REQUIRE_FALSE(refused.ok);
    REQUIRE_FALSE(refused.errors.empty());
    CHECK(refused.errors[0].code == "activate-failed");
    CHECK(refused.errors[0].message.find("mono") != std::string::npos);
}

TEST_CASE("the Pico refuses rp.dsp.dpcm, which builds storage, and plans rp.dsp.normalize", "[dsp-buffer-nodes]") {
    // The DPCM node's bit storage is built at activate, so the type declares heap and the planner refuses it there.
    const rp_node_type* type = nullptr;
    for (std::uint32_t i = 0; i < rp::graph::dspPackage()->node_type_count; ++i) {
        const rp_node_type* candidate = rp::graph::dspPackage()->node_types[i];
        if (std::string(candidate->id) == "rp.dsp.dpcm") type = candidate;
    }
    REQUIRE(type);
    CHECK(std::string(type->descriptor(type)).find(R"("requires":{"heap":true,"threads":false})") != std::string::npos);

    BufferRig  pico(rp::graph::Target::Pico);
    const auto dpcmSource = pico.buffer(signal(256));
    CHECK(GraphRig::codes(pico.source("rp.dsp.dpcm", R"({"buffer":)" + BufferRig::res(dpcmSource) + "}", 1)) ==
          "capability");

    // A normalising player allocates nothing, so it plans and runs there.
    BufferRig  pico2(rp::graph::Target::Pico);
    const auto source = pico2.buffer(signal(256));
    REQUIRE(pico2.source("rp.dsp.normalize",
                         R"({"buffer":)" + BufferRig::res(source) + R"(,"gain":0.5,"channels":1})", 1)
                .ok);
    pico2.run(64);
    const std::vector<float> played = pico2.block(0, 64);
    CHECK(std::any_of(played.begin(), played.end(), [](float v) { return v != 0.0f; }));
}

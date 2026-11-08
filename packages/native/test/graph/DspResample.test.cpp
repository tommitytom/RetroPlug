// rp.dsp.resample (src/graph/nodes/DspResampleNodes.cpp, docs/modular-graph-phase-d1-design.md section 5): an audio
// buffer played at the plan's rate through the resample kernel, through real plans built by the graph rig.
//
// - The node's output is the kernel's stream run directly - the buffer, then silence, fed to an rp::dsp::Resampler -
//   bit for bit, whatever blocks the plan runs in (1, 7, 64, 512 and 1024 frames and a ragged mix), and its first
//   resampledLength frames are the kit path's whole-buffer resample: 22.05 kHz into a 48 kHz plan, 48 kHz down to the
//   kit rate, and a buffer shorter than one 1024-frame chunk.
// - At equal rates the output is the buffer itself, bit for bit, though it goes through r8brain.
// - `loop` feeds the buffer again at its end, one seamless stream; a layout channel the buffer lacks is silence, and so
//   is a node that names no buffer.
// - A buffer the settings rename is a rebuild whose stream starts at the new buffer's first frame, at its own rate; a
//   re-activation at a new plan rate carries on from the buffer frame the output had reached.
// - A plan block allocates and frees nothing counted from the plan's first block (the suite's counting allocator): the
//   resamplers are built when the node activates, on the control thread.
// - The node declares heap, so the Pico refuses it (the planner's `capability` code). Its descriptor golden is
//   DspNodes.test.cpp's case, which walks this package beside rp.dsp and carries this file's tag too.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "graph/CountingAllocator.hpp"
#include "GraphRig.hpp"
#include "dsp/Resample.hpp"
#include "graph/nodes/DspResampleNodes.hpp"

namespace {

using rp::graph::test::GraphRig;

constexpr std::uint32_t kMaxBlock = 1024;

// The block sizes a stream is split into: one frame, an odd size, a host block, the resampler's chunk, and a ragged
// sequence that cycles.
const std::vector<std::vector<std::size_t>> kSplits = {{1}, {7}, {64}, {512}, {1024}, {1, 31, 512, 3, 1024, 77}};

/** A sine plus noise; `salt` makes another channel's content differ. */
std::vector<float> signal(std::size_t frames, std::uint32_t salt = 0) {
    std::vector<float> out(frames);
    std::uint32_t      rng = 0x1234567u ^ salt;
    for (std::size_t i = 0; i < frames; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float noise = (static_cast<float>(rng >> 8) * (1.0f / 16777216.0f) * 2.0f - 1.0f) * 0.2f;
        out[i] = noise + 0.7f * std::sin(static_cast<float>(i) * (0.031f + 0.01f * static_cast<float>(salt)));
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

/** The kernel run directly: `source` from frame `from`, then silence (or `source` again from its first frame, when
 *  looping), resampled `inRate` -> `outRate`, `frames` frames of it. Each pull asks for everything left rather than
 *  for one block's frames as the node's feed does; the pushes are the same stream either way, since the kernel only
 *  ever hands r8brain whole chunks. `loopFrom` is the output frame a loop set mid-stream takes effect at. */
std::vector<float> kernelStream(const std::vector<float>& source, double inRate, double outRate, std::size_t frames,
                                bool loop = false, std::size_t from = 0, std::size_t loopFrom = 0) {
    rp::dsp::Resampler r;
    r.init(inRate, outRate);
    std::vector<float> out(frames);
    std::size_t        done = 0, at = from;
    while (done < frames) {
        done += r.pull(out.data() + done, frames - done);
        if (at >= source.size() && loop && done >= loopFrom) at = 0;
        if (at < source.size()) at += r.push(source.data() + at, source.size() - at);
        else r.pushSilence(rp::dsp::Resampler::kChunk);
    }
    return out;
}

/** The first `n` frames of a stream. */
std::vector<float> head(const std::vector<float>& v, std::size_t n) {
    return std::vector<float>(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n));
}

/** A rig at `rate` whose plans run blocks of up to 1024 frames. */
struct ResampleRig : GraphRig {
    explicit ResampleRig(double rate, rp::graph::Target target = rp::graph::Target::Desktop)
        : GraphRig({rate, kMaxBlock, target}, 8, nullptr, kMaxBlock) {}

    // An audio buffer resource at `rate`, one stream per channel.
    rp::graph::ResourceTable::Id buffer(const std::vector<std::vector<float>>& perChannel, double rate) {
        const auto channels = static_cast<std::uint32_t>(perChannel.size());
        const auto id = resources.createAudioBuffer(channels, static_cast<std::uint32_t>(perChannel[0].size()), rate);
        auto* const b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
        for (std::uint32_t c = 0; c < channels; ++c) std::copy(perChannel[c].begin(), perChannel[c].end(), b->data[c]);
        return id;
    }

    /** A resampler of `settings` (a JSON object's members, the buffer among them) into the host output's first
     *  `channels` channels. */
    rp::graph::SetGraphResult play(const std::string& settings, std::uint32_t channels) {
        const std::string layout = channels == 1 ? "mono" : "stereo";
        const std::string port = channels == 1 ? "out.1" : "out.1-2";
        return set(R"({"nodes":[{"id":"r","type":"rp.dsp.resample","settings":{"channels":)" +
                   std::to_string(channels) + R"(,"layout":")" + layout + R"(")" +
                   (settings.empty() ? "" : "," + settings) + R"(}},
                               {"id":"out","type":"rp.host.audio-out","settings":{"buses":[)" +
                   std::to_string(channels) + R"(]}}],
                   "wires":[{"from":"r.out","to":"out.)" + port + R"("}]})");
    }

    static std::string ref(rp::graph::ResourceTable::Id id) { return R"("buffer":"res:)" + std::to_string(id) + "\""; }

    /** The first `channels` host channels over `frames`, run in the block sizes `split` cycles through. */
    std::vector<std::vector<float>> render(std::size_t frames, const std::vector<std::size_t>& split,
                                           std::uint32_t channels) {
        std::vector<std::vector<float>> out(channels);
        std::size_t                     at = 0, k = 0;
        while (at < frames) {
            const std::size_t n = std::min(split[k++ % split.size()], frames - at);
            run(static_cast<std::uint32_t>(n));
            for (std::uint32_t c = 0; c < channels; ++c)
                out[c].insert(out[c].end(), outs[c].begin(), outs[c].begin() + static_cast<std::ptrdiff_t>(n));
            at += n;
        }
        return out;
    }
};

}  // namespace

TEST_CASE("rp.dsp.resample from 22.05 kHz into a 48 kHz plan is its kernel's stream, split any way", "[dsp-resample]") {
    constexpr std::size_t kSource = 4096, kOut = 10000;  // past the source's 8916 frames, into the silence after it
    const std::vector<std::vector<float>> source = {signal(kSource), signal(kSource, 3)};
    std::vector<std::vector<float>>       reference, kit;
    for (const auto& channel : source) {
        reference.push_back(kernelStream(channel, 22050.0, 48000.0, kOut));
        // And the stream's first resampledLength frames are the kit path's whole-buffer resample.
        kit.emplace_back();
        rp::dsp::resample(22050.0, 48000.0, channel.data(), channel.size(), kit.back());
        REQUIRE(kit.back().size() == rp::dsp::resampledLength(kSource, 22050.0, 48000.0));
        CHECK(firstDifference(head(reference.back(), kit.back().size()), kit.back()) == -1);
    }

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer(source, 22050.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 2).ok);
        const auto out = rig.render(kOut, split, 2);
        for (std::size_t c = 0; c < source.size(); ++c) {
            CHECK(firstDifference(out[c], reference[c]) == -1);
            // Held to rp::dsp::resample() itself, a library call outside this file's feed loop, so the independence
            // is structural rather than through the stream that mirrors it.
            CHECK(firstDifference(head(out[c], kit[c].size()), kit[c]) == -1);
        }
    }
}

TEST_CASE("rp.dsp.resample from 48 kHz to the kit rate is its kernel's stream", "[dsp-resample]") {
    constexpr double      kKitRate = 11468.0;
    constexpr std::size_t kSource = 9000, kOut = 3000;  // the source is 2150 frames at the kit rate
    const std::vector<float> source = signal(kSource, 1);
    const std::vector<float> reference = kernelStream(source, 48000.0, kKitRate, kOut);
    std::vector<float>       kit;
    rp::dsp::resample(48000.0, kKitRate, source.data(), source.size(), kit);
    CHECK(firstDifference(head(reference, kit.size()), kit) == -1);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(kKitRate);
        const auto  buffer = rig.buffer({source}, 48000.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
        const std::vector<float> out = rig.render(kOut, split, 1)[0];
        CHECK(firstDifference(out, reference) == -1);
        CHECK(firstDifference(head(out, kit.size()), kit) == -1);  // rp::dsp::resample() itself, per split
    }
}

TEST_CASE("rp.dsp.resample of a buffer shorter than one chunk is its kernel's stream", "[dsp-resample]") {
    // 600 frames, the kit tests' short source: the kernel zero-pads the one partial chunk it is, so the node must not
    // wait for a chunk the buffer cannot fill, nor resample the part it has on its own.
    const std::vector<float> source = signal(600, 2);
    const std::vector<float> reference = kernelStream(source, 22050.0, 48000.0, 4096);
    std::vector<float>       kit;
    rp::dsp::resample(22050.0, 48000.0, source.data(), source.size(), kit);
    REQUIRE(kit.size() == 1306);
    CHECK(firstDifference(head(reference, kit.size()), kit) == -1);

    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer({source}, 22050.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
        CHECK(firstDifference(rig.render(4096, split, 1)[0], reference) == -1);
    }
}

TEST_CASE("rp.dsp.resample at the plan's own rate is the buffer, bit for bit", "[dsp-resample]") {
    const std::vector<float> source = signal(3000, 4);
    std::vector<float>       expected(source);
    expected.resize(5000, 0.0f);  // then silence
    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer({source}, 48000.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
        CHECK(firstDifference(rig.render(5000, split, 1)[0], expected) == -1);
    }
}

TEST_CASE("rp.dsp.resample loops the buffer into one seamless stream when told to", "[dsp-resample]") {
    const std::vector<float> source = signal(1500, 5);
    const std::vector<float> looped = kernelStream(source, 22050.0, 48000.0, 9000, true);
    for (const auto& split : kSplits) {
        INFO("split starting " << split[0]);
        ResampleRig rig(48000.0);
        const auto  buffer = rig.buffer({source}, 22050.0);
        REQUIRE(rig.play(ResampleRig::ref(buffer) + R"(,"loop":true)", 1).ok);
        CHECK(firstDifference(rig.render(9000, split, 1)[0], looped) == -1);
    }
    // And the default is the buffer once: the stream after it is the silent one, not the loop.
    ResampleRig rig(48000.0);
    const auto  buffer = rig.buffer({source}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
    const std::vector<float> once = rig.render(9000, {64}, 1)[0];
    CHECK(firstDifference(once, kernelStream(source, 22050.0, 48000.0, 9000)) == -1);
    CHECK(firstDifference(once, looped) != -1);
}

TEST_CASE("rp.dsp.resample takes a loop committed after the buffer has run out", "[dsp-resample]") {
    // `loop` is the one setting a commit carries into the running node, and the node promises a loop switched on after
    // the buffer's end restarts it. 1500 frames at 22.05 kHz are 3265 at 48 kHz, so 4096 output frames have run the
    // buffer out and fed the kernel silence past it.
    constexpr std::size_t    kOut = 4096;
    const std::vector<float> source = signal(1500, 10);
    ResampleRig              rig(48000.0);
    const auto               buffer = rig.buffer({source}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
    CHECK(firstDifference(rig.render(kOut, {64}, 1)[0], kernelStream(source, 22050.0, 48000.0, kOut)) == -1);

    // The same ref, so this is a commit of `loop` into the instance that is already running, not a rebuild. The
    // reference is one stream over both halves of the run, with the loop switched on at the frame the commit lands
    // between.
    REQUIRE(rig.play(ResampleRig::ref(buffer) + R"(,"loop":true)", 1).ok);
    const std::vector<float> after = rig.render(kOut, {64}, 1)[0];
    const std::vector<float> both = kernelStream(source, 22050.0, 48000.0, 2 * kOut, true, 0, kOut);
    CHECK(firstDifference(after, std::vector<float>(both.begin() + kOut, both.end())) == -1);
    CHECK(firstDifference(after, std::vector<float>(kOut, 0.0f)) != -1);  // the buffer, not the silence past it
}

TEST_CASE("rp.dsp.resample sounds the buffer's channels, and silence for the rest", "[dsp-resample]") {
    const std::vector<float> source = signal(2000, 6);
    ResampleRig              rig(48000.0);
    const auto               buffer = rig.buffer({source}, 32000.0);
    // A mono buffer into a stereo layout: one resampler, and the second channel silent, as the player's.
    REQUIRE(rig.play(ResampleRig::ref(buffer), 2).ok);
    const auto out = rig.render(4000, {64}, 2);
    CHECK(firstDifference(out[0], kernelStream(source, 32000.0, 48000.0, 4000)) == -1);
    CHECK(firstDifference(out[1], std::vector<float>(4000, 0.0f)) == -1);

    // No buffer named at all: it plans, and every block is silence.
    ResampleRig empty(48000.0);
    REQUIRE(empty.play("", 2).ok);
    const auto silent = empty.render(256, {64}, 2);
    CHECK(firstDifference(silent[0], std::vector<float>(256, 0.0f)) == -1);
    CHECK(firstDifference(silent[1], std::vector<float>(256, 0.0f)) == -1);
}

TEST_CASE("rp.dsp.resample of a renamed buffer starts that buffer's stream, at its own rate", "[dsp-resample]") {
    const std::vector<float> a = signal(3000, 7), b = signal(2500, 8);
    ResampleRig              rig(48000.0);
    const auto               first = rig.buffer({a}, 22050.0);
    const auto               second = rig.buffer({b}, 32000.0);
    REQUIRE(rig.play(ResampleRig::ref(first), 1).ok);
    CHECK(firstDifference(rig.render(1000, {64}, 1)[0], kernelStream(a, 22050.0, 48000.0, 1000)) == -1);

    // A ref that moved is a rebuild: the fresh instance built its resampler for 32 kHz on the control thread, and its
    // stream is the second buffer's from that buffer's first frame.
    REQUIRE(rig.play(ResampleRig::ref(second), 1).ok);
    CHECK(firstDifference(rig.render(4000, {64}, 1)[0], kernelStream(b, 32000.0, 48000.0, 4000)) == -1);
}

TEST_CASE("rp.dsp.resample re-activated at a new plan rate carries on where its output was", "[dsp-resample]") {
    const std::vector<float> source = signal(4000, 9);
    ResampleRig              rig(48000.0);
    const auto               buffer = rig.buffer({source}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 1).ok);
    CHECK(firstDifference(rig.render(2048, {64}, 1)[0], kernelStream(source, 22050.0, 48000.0, 2048)) == -1);

    // 2048 frames at 48 kHz are 940 frames of the 22.05 kHz buffer, so the stream at 44.1 kHz is a fresh resampler's
    // from there; a block-size change alone leaves the stream as it was.
    std::string why;
    REQUIRE(rig.host->reactivate(44100.0, kMaxBlock, &why));
    const auto from = static_cast<std::size_t>(2048.0 * (22050.0 / 48000.0));
    REQUIRE(from == 940);
    const std::vector<float> after = kernelStream(source, 22050.0, 44100.0, 3072, false, from);
    CHECK(firstDifference(rig.render(2048, {64}, 1)[0], head(after, 2048)) == -1);
    REQUIRE(rig.host->reactivate(44100.0, 512, &why));
    CHECK(firstDifference(rig.render(1024, {512}, 1)[0], std::vector<float>(after.begin() + 2048, after.end())) == -1);
}

TEST_CASE("rp.dsp.resample re-activated past the buffer's end wraps a loop and clamps without one", "[dsp-resample]") {
    // The resume frame is the output's length in buffer time, modulo the buffer when looping and clamped to its end
    // otherwise. 4096 output frames at 48 kHz are 1881 frames of a 1500-frame 22.05 kHz buffer, which is one pass and
    // 381 frames into the second: the loop resumes at 381, the non-loop stream at the buffer's end and so in silence.
    constexpr std::size_t    kOut = 4096;
    const std::vector<float> source = signal(1500, 11);
    const auto               elapsed = static_cast<std::size_t>(kOut * (22050.0 / 48000.0));
    REQUIRE(elapsed == 1881);
    REQUIRE(elapsed > source.size());

    ResampleRig rig(48000.0);
    const auto  buffer = rig.buffer({source}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer) + R"(,"loop":true)", 1).ok);
    CHECK(firstDifference(rig.render(kOut, {64}, 1)[0], kernelStream(source, 22050.0, 48000.0, kOut, true)) == -1);

    std::string why;
    REQUIRE(rig.host->reactivate(44100.0, kMaxBlock, &why));
    const std::vector<float> resumed = rig.render(kOut, {64}, 1)[0];
    CHECK(firstDifference(resumed, kernelStream(source, 22050.0, 44100.0, kOut, true, elapsed % source.size())) == -1);

    // The same two runs without `loop`, on a plan of its own: the resume is the buffer's end, so it is pure silence.
    ResampleRig once(48000.0);
    const auto  plain = once.buffer({source}, 22050.0);
    REQUIRE(once.play(ResampleRig::ref(plain), 1).ok);
    CHECK(firstDifference(once.render(kOut, {64}, 1)[0], kernelStream(source, 22050.0, 48000.0, kOut)) == -1);
    REQUIRE(once.host->reactivate(44100.0, kMaxBlock, &why));
    const std::vector<float> silent = once.render(kOut, {64}, 1)[0];
    CHECK(firstDifference(silent, kernelStream(source, 22050.0, 44100.0, kOut, false, source.size())) == -1);
    for (const float v : silent) CHECK(v == 0.0f);  // zeros, whatever sign r8brain gives them
}

TEST_CASE("rp.dsp.resample's plan allocates and frees nothing, counted from its first block", "[dsp-resample]") {
    ResampleRig rig(48000.0);
    const auto  buffer = rig.buffer({signal(2048), signal(2048, 1)}, 22050.0);
    REQUIRE(rig.play(ResampleRig::ref(buffer), 2).ok);

    // Counted from the plan's first block, where a resampler built lazily instead of in activate would allocate: the
    // node's activate has already run, on the control thread, so a lazy build is the only thing that could show up
    // here. Ten blocks of 512 run past the source's 4458 frames at 48 kHz, so the count covers the chunks the
    // resamplers complete, the partial one at the source's end and the silent ones after it.
    rp::test::beginCountingAllocations();
    rig.run(512, 10);
    const rp::test::AllocationCount m = rp::test::endCountingAllocations();
    CHECK(m.allocations == 0);
    CHECK(m.frees == 0);
}

TEST_CASE("the Pico refuses rp.dsp.resample: it declares heap", "[dsp-resample]") {
    const rp_node_type* type = rp::graph::dspResamplePackage()->node_types[0];
    CHECK(std::string(type->descriptor(type)).find(R"("requires":{"heap":true,"threads":false})") != std::string::npos);

    ResampleRig pico(48000.0, rp::graph::Target::Pico);
    const auto  buffer = pico.buffer({signal(1024)}, 22050.0);
    CHECK(GraphRig::codes(pico.play(ResampleRig::ref(buffer), 1)) == "capability");
    ResampleRig desktop(48000.0);
    const auto  same = desktop.buffer({signal(1024)}, 22050.0);
    CHECK(desktop.play(ResampleRig::ref(same), 1).ok);
}

TEST_CASE("rp.dsp.resample settings are validated with a reason", "[dsp-resample]") {
    ResampleRig rig(48000.0);
    const auto  buffer = rig.buffer({signal(256)}, 22050.0);
    const auto  bad =
        rig.set(R"({"nodes":[{"id":"r","type":"rp.dsp.resample","settings":{"channels":2,"layout":"mono",)" +
                ResampleRig::ref(buffer) + R"(}}],"wires":[]})");
    REQUIRE_FALSE(bad.ok);
    CHECK(GraphRig::codes(bad) == "settings-invalid");
    CHECK(bad.errors[0].message.find("not 2") != std::string::npos);

    // A resource of another type is refused when the instance is created, with the field's name.
    std::string why;
    const auto  bytes = rig.resources.createBytes("rom", {1, 2, 3}, &why);
    REQUIRE(bytes != 0);
    const auto wrong = rig.play(ResampleRig::ref(bytes), 1);
    REQUIRE_FALSE(wrong.ok);
    REQUIRE_FALSE(wrong.errors.empty());
    CHECK(wrong.errors[0].message.find("buffer") != std::string::npos);
}

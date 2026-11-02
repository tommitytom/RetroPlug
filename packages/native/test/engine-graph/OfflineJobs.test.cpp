// Offline jobs (docs/modular-graph-phase-d2-design.md section 3, host/engine/OfflineJobs.hpp): a graph document run to
// completion on a host of its own, on a worker of the engine's offline pool. A job returns exactly what the same nodes
// give through a GraphRig run directly; jobs past the pool's concurrency queue and each returns its own result; a
// cancel stops a queued job at once and a running one at its next block or before its next input, with the cancel
// reason; an engine destroyed with jobs in flight abandons them and joins every worker; the outstanding bound refuses;
// every refusal says why, at submission or in the completion; a file input is SampleCache's decode, windowed exactly as the two kit windows
// spell it (LSDj's and risa's), the cap's two arithmetics and every edge included, with its report; a kit-shaped job
// names one buffer twice - as the normalising source's and as the dither's `seedFrom` - beside the DPCM encode, and
// gives the kit arithmetic for all three; and a job run while an audio thread plays the live plan leaves that plan's
// output bit for bit, its warmed blocks allocating nothing.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[offline]"`; the hidden
// "[offline-bench]" times a trivial job and a one-second resample.

#include <algorithm>
#include <atomic>
#include <cfenv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#include "../graph/CountingAllocator.hpp"
#include "../graph/GraphRig.hpp"
#include "GraphDocs.hpp"
#include "dsp/Dither.hpp"
#include "dsp/Dpcm.hpp"
#include "dsp/Gain.hpp"
#include "dsp/Resample.hpp"
#include "graph/node.hpp"
#include "host/engine/DefaultFloatEnvironment.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/engine/OfflineJobs.hpp"
#include "host/graph/HostServices.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "kit/SampleCache.hpp"
#include "lsdj/KitBank.hpp"

using rp::host::OfflineCap;
using rp::host::OfflineCapArithmetic;
using rp::host::OfflineCompletion;
using rp::host::OfflineFile;
using rp::host::OfflineJobs;
using rp::host::OfflineOutput;
using rp::host::OfflineRequest;
using rp::host::OfflineSamples;

namespace {

// A node whose activation refuses, for the activate-failed completion; a planner refusal of the same document names it.
struct Unready {
    static constexpr const char* id = "rp.test.unready";
    struct { rp::audio_out<rp::mono> out; } outputs;
    bool activate(double, std::uint32_t) { return false; }
    void process(const rp::Block&) noexcept {}
};

const rp_package* unreadyPackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Unready>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.offline","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

// A node that holds its block until the test releases it, so a cancel can reach a running job after the job's own last
// cancel check (a one-block job's): the worker still sees the flag and makes the cancel the outcome.
struct Gate {
    static constexpr const char* id = "rp.test.gate";
    static inline std::atomic<bool> entered{false};
    static inline std::atomic<bool> release{false};
    struct { rp::audio_out<rp::mono> out; } outputs;
    void process(const rp::Block& b) noexcept {
        entered.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        for (std::uint32_t i = 0; i < b.frames(); ++i) outputs.out[0][i] = 0.5f;
    }
};

const rp_package* gatePackage() {
    static const rp_node_type* const kTypes[] = {rp::node_type<Gate>()};
    static const rp_package kPackage{RP_ABI_VERSION, R"({"id":"rp.test.gate","version":"1.0.0"})",
                                     static_cast<std::uint32_t>(std::size(kTypes)), kTypes};
    return &kPackage;
}

constexpr double kTwoPi = 6.283185307179586;

// Interleaved test audio: a sine per channel at its own frequency, plus a little seeded noise, so each channel and
// each seed is told apart, and a resampler's filter has something above the kit rate's Nyquist to remove.
std::vector<float> signal(std::size_t frames, std::uint32_t channels, std::uint32_t seed, double rate = 44100.0) {
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<float> noise(-0.05f, 0.05f);
    std::vector<float>                    out(frames * channels);
    for (std::size_t f = 0; f < frames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c)
            out[f * channels + c] =
                0.6f * static_cast<float>(std::sin(kTwoPi * (220.0 * (c + 1) + seed) * double(f) / rate)) + noise(rng);
    return out;
}

// Buffer player (or resampler) -> gain -> recorder, `channels` wide, playing res:1 into res:2.
std::string chainDocument(bool resample, double gain, std::uint32_t channels) {
    const std::string c = std::to_string(channels);
    const std::string layout = channels == 1 ? "mono" : "stereo";
    const std::string source =
        resample ? R"({"id":"src","type":"rp.dsp.resample","settings":{"buffer":"res:1","channels":)" + c +
                       R"(,"layout":")" + layout + R"("}})"
                 : R"({"id":"src","type":"rp.core.buffer-player","settings":{"channels":)" + c + R"(,"layout":")" +
                       layout + R"(","loop":false},"resources":{"sample":"res:1"}})";
    char g[64];
    std::snprintf(g, sizeof g, "%.17g", gain);
    return R"({"nodes":[)" + source + R"(,{"id":"g","type":"rp.dsp.gain","settings":{"channels":)" + c +
           R"(},"params":{"gain":)" + g + R"(}},{"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":)" +
           c + R"(},"resources":{"target":"res:2"}}],
             "wires":[{"from":"src.out","to":"g.in"},{"from":"g.out","to":"rec.in"}]})";
}

// The same nodes through a GraphRig directly, at `planRate` in blocks of `block`: the recorder's buffer, interleaved.
std::vector<float> rigReference(const std::vector<float>& input, std::uint32_t channels, double inputRate,
                                double planRate, bool resample, double gain, std::uint32_t outFrames,
                                std::uint32_t block = OfflineJobs::kBlockFrames) {
    rp::graph::test::GraphRig rig(rp::graph::HostConfig{planRate, block, rp::graph::Target::Desktop}, 0, nullptr,
                                   block);
    const std::uint32_t frames = static_cast<std::uint32_t>(input.size() / channels);
    const auto in = rig.resources.createAudioBuffer(channels, frames, inputRate);
    const auto out = rig.resources.createAudioBuffer(channels, outFrames, planRate);
    REQUIRE(in == 1);
    REQUIRE(out == 2);
    auto* b = static_cast<rp_audio_buffer*>(rig.resources.get(in)->payload);
    for (std::uint32_t f = 0; f < frames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c) b->data[c][f] = input[std::size_t(f) * channels + c];
    const auto planned = rig.set(chainDocument(resample, gain, channels));
    INFO(rp::graph::test::GraphRig::codes(planned));
    REQUIRE(planned.ok);
    rig.transport.sample_rate = planRate;
    for (std::uint32_t done = 0; done < outFrames;) {
        const std::uint32_t n = std::min(block, outFrames - done);
        rig.host->processBlock(n, rig.transport);
        done += n;
    }
    const auto* r = static_cast<const rp_audio_buffer*>(rig.resources.get(out)->payload);
    std::vector<float> interleaved(std::size_t(channels) * outFrames);
    for (std::uint32_t f = 0; f < outFrames; ++f)
        for (std::uint32_t c = 0; c < channels; ++c) interleaved[std::size_t(f) * channels + c] = r->data[c][f];
    return interleaved;
}

OfflineRequest chainRequest(std::vector<float> input, std::uint32_t channels, double inputRate, double planRate,
                            bool resample, double gain, std::uint32_t outFrames) {
    OfflineRequest r;
    r.document = chainDocument(resample, gain, channels);
    r.inputs.emplace_back(OfflineSamples{std::move(input), channels, inputRate});
    r.outputs.push_back({channels, outFrames, planRate});
    r.returns = {2};
    r.frames = outFrames;
    r.rate = planRate;
    return r;
}

// A job long enough to be running whenever the case acts on it, and to keep running until it is cancelled: a looping
// resampler through a gain for as many frames as a job may ask for (a day of audio, hours of work).
OfflineRequest longRequest() {
    OfflineRequest r;
    r.document = R"({"nodes":[{"id":"p","type":"rp.dsp.resample","settings":{"buffer":"res:1","channels":1,
                                                                         "layout":"mono","loop":true}},
                             {"id":"g","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":0.5}},
                             {"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
                              "resources":{"target":"res:2"}}],
                    "wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"rec.in"}]})";
    r.inputs.emplace_back(OfflineSamples{signal(4096, 1, 7), 1, 44100.0});
    r.outputs.push_back({1, 4096, 48000.0});
    r.returns = {2};
    r.frames = std::numeric_limits<std::uint32_t>::max();
    r.rate = 48000.0;
    return r;
}

// Drain `drain` (waiting between drains) until every id in `ids` has completed, or 60 s have passed.
template <class Drain>
std::map<std::uint32_t, OfflineCompletion> collect(Drain&& drain, const std::vector<std::uint32_t>& ids) {
    std::map<std::uint32_t, OfflineCompletion> done;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (done.size() < ids.size() && std::chrono::steady_clock::now() < deadline)
        for (auto& c : drain(std::chrono::milliseconds(50))) done.emplace(c.id, std::move(c));
    return done;
}

OfflineCompletion one(OfflineJobs& jobs, std::uint32_t id) {
    auto done = collect([&](auto w) { return jobs.drain(w); }, {id});
    REQUIRE(done.count(id) == 1);
    return std::move(done[id]);
}

// Sets flush-to-zero and denormals-are-zero on the calling thread, where the target has them; false where it does not.
bool setFlushToZero() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    _mm_setcsr(_mm_getcsr() | 0x8040u);  // MXCSR FTZ (bit 15) and DAZ (bit 6)
    return true;
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    std::uint64_t fpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= std::uint64_t(1) << 24;  // FPCR.FZ
    asm volatile("msr fpcr, %0" : : "r"(fpcr));
    return true;
#else
    return false;
#endif
}

// The thread count of this process, from /proc on Linux; -1 elsewhere.
int threadCount() {
#if defined(__linux__)
    int n = 0;
    for ([[maybe_unused]] const auto& e : std::filesystem::directory_iterator("/proc/self/task")) ++n;
    return n;
#else
    return -1;
#endif
}

// The thread count once it holds still: the same on every read for 50 ms, or the last read after ten seconds. Two
// things make one read wrong while threads come and go. A thread pthread_join has returned for can be listed a moment
// longer (the kernel wakes the joiner as the exiting thread clears its id, before its task is released), and a listing
// taken while a thread exits can skip one that is still there (/proc's task listing walks by position). On a loaded
// machine either is long enough to count, so a count is taken only once nothing changes.
int threadCountSteady() {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int        n = threadCount();
    auto       since = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const int m = threadCount();
        if (m != n) {
            n = m;
            since = std::chrono::steady_clock::now();
        } else if (std::chrono::steady_clock::now() - since >= std::chrono::milliseconds(50)) {
            break;
        }
    }
    return n;
}

// The count a joined-workers check starts from. A thread is started and joined first: a runtime that starts a helper
// thread of its own at the process's first thread (ThreadSanitizer's background thread) then has it before the count,
// rather than during the case, where it would read as a worker never joined.
int threadCountBaseline() {
    std::thread([] {}).join();
    return threadCountSteady();
}

// The steady count once it is no more than `atMost`, or what it reads after ten seconds: a thread still running stays
// counted past the deadline.
int threadCountSettled(int atMost) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int        n = threadCountSteady();
    while (n > atMost && std::chrono::steady_clock::now() < deadline) n = threadCountSteady();
    return n;
}

// A WAV file, little-endian byte by byte: 16-bit PCM (`floatSamples` false), whose decode converts, or 32-bit float.
void writeWav(const std::filesystem::path& path, const std::vector<float>& samples, std::uint32_t rate,
              bool floatSamples) {
    std::vector<std::uint8_t> bytes;
    const auto u16 = [&](std::uint16_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v));
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto u32 = [&](std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(v >> shift));
    };
    const auto tag = [&](const char* s) { bytes.insert(bytes.end(), s, s + 4); };
    const std::uint32_t width = floatSamples ? 4 : 2;
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * width);
    tag("RIFF");
    u32(36u + dataBytes);
    tag("WAVE");
    tag("fmt ");
    u32(16);
    u16(floatSamples ? 3 : 1);
    u16(1);
    u32(rate);
    u32(rate * width);
    u16(static_cast<std::uint16_t>(width));
    u16(static_cast<std::uint16_t>(width * 8));
    tag("data");
    u32(dataBytes);
    for (const float v : samples) {
        if (floatSamples) {
            std::uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof bits);
            u32(bits);
        } else {
            const long pcm = std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
            u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(pcm)));
        }
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

// A directory of the case's own under the temp dir, removed with it.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        path = std::filesystem::temp_directory_path() / ("rp-offline-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

// The PAL DMC rates (wav2dmc.py's table, which the risa kit compiler's rate index picks from), the risa cap's rate.
constexpr double kPalRates[16] = {
    4177.40,  4696.63,  5261.41,  5579.22,  6023.94,  7044.94,  7917.18,  8397.01,
    9446.63,  11233.80, 12595.50, 14089.89, 16965.40, 21315.52, 25191.00, 33252.09,
};
constexpr std::uint32_t kLsdjCapFrames = static_cast<std::uint32_t>(rp::lsdj::kMaxSampleFrames);
constexpr std::uint32_t kRisaCapFrames = 4081u * 8u;  // the DMC's longest stream (4081 bytes) in bits

// The two kit windows, written out as references: what the job's window must reproduce.
std::pair<std::size_t, std::size_t> lsdjWindow(std::size_t size, std::uint32_t sampleRate, std::size_t offset,
                                               std::size_t length) {
    // LSDj's: the cap is 32576 kit frames at 11468 Hz, measured back to the decoded rate in float, ratio first.
    const float srRatio = static_cast<float>(sampleRate) / static_cast<float>(rp::lsdj::kGameboySampleRate);
    const std::size_t maxFrames = static_cast<std::size_t>(
        std::ceil(static_cast<float>(rp::lsdj::kMaxSampleFrames) * srRatio));
    const std::size_t length0 = (length == 0) ? size : length;
    const std::size_t startFrame = std::min(offset, size);
    return {startFrame, std::min(maxFrames, std::min(length0, size - startFrame))};
}

std::pair<std::size_t, std::size_t> risaWindow(std::size_t size, std::uint32_t sampleRate, std::size_t offset,
                                               std::size_t length, double targetRate) {
    // risa's: the cap is 4081 DMC bytes (32648 bits) at the slot's PAL rate, measured back in double, product first.
    const double srcRate = static_cast<double>(sampleRate);
    const std::size_t maxOutFrames =
        static_cast<std::size_t>(std::ceil(static_cast<double>(4081 * 8) * srcRate / targetRate));
    const std::size_t length0 = length == 0 ? size : length;
    const std::size_t start = std::min(offset, size);
    return {start, std::min(maxOutFrames, std::min(length0, size - start))};
}

OfflineCap lsdjCap() {
    return {kLsdjCapFrames, double(rp::lsdj::kGameboySampleRate), OfflineCapArithmetic::FloatRatio};
}
OfflineCap risaCap(int index) { return {kRisaCapFrames, kPalRates[index], OfflineCapArithmetic::DoubleScale}; }

OfflineRequest fileRequest(const std::filesystem::path& path, std::uint32_t offset, std::uint32_t length,
                           std::optional<OfflineCap> cap) {
    OfflineRequest r;
    r.inputs.emplace_back(OfflineFile{path.string(), offset, length, cap});
    r.returns = {1};
    return r;
}

// offlineWindow's value for a cap the window arithmetic can carry (a job refuses one it cannot, so no case here asks
// for that).
rp::host::OfflineWindow checkedWindow(std::size_t decodedFrames, std::uint32_t decodedRate, std::uint32_t offset,
                                      std::uint32_t length, const std::optional<OfflineCap>& cap) {
    const auto window = rp::host::offlineWindow(decodedFrames, decodedRate, offset, length, cap);
    REQUIRE(window.has_value());
    return *window;
}

}  // namespace

TEST_CASE("an offline job runs a document to completion and returns what the same nodes give run directly",
          "[engine-graph][offline]") {
    OfflineJobs jobs;
    // A resample to the kit rate (the frames a kit cuts there) and a plain player at the source rate, mono and stereo,
    // each through a gain into a recorder: the job's bytes are the rig's, bit for bit.
    struct Case {
        bool          resample;
        std::uint32_t channels;
        double        planRate;
    };
    for (const Case k :
         {Case{true, 1, 11468.0}, Case{true, 2, 11468.0}, Case{false, 2, 44100.0}, Case{false, 1, 22050.0}}) {
        INFO("resample " << k.resample << ", " << k.channels << " channel(s) at " << k.planRate);
        const std::size_t frames = 5000;
        const auto input = signal(frames, k.channels, 3);
        const std::uint32_t outFrames =
            k.resample ? static_cast<std::uint32_t>(rp::dsp::resampledLength(frames, 44100.0, k.planRate))
                       : static_cast<std::uint32_t>(frames);
        const auto reference = rigReference(input, k.channels, 44100.0, k.planRate, k.resample, 0.75, outFrames);
        const auto s = jobs.submit(chainRequest(input, k.channels, 44100.0, k.planRate, k.resample, 0.75, outFrames));
        INFO(s.error);
        REQUIRE(s.id != 0);
        const auto c = one(jobs, s.id);
        INFO(c.error);
        REQUIRE(c.ok);
        REQUIRE(c.buffers.size() == 1);
        CHECK(c.buffers[0].handle == 2);
        CHECK(c.buffers[0].channels == k.channels);
        CHECK(c.buffers[0].frames == outFrames);
        CHECK(c.buffers[0].rate == k.planRate);
        CHECK(c.buffers[0].samples == reference);
        CHECK(c.inputs.empty());  // no file input to report
        // Not vacuous: the recorder wrote, and the gain applied.
        float peak = 0.0f;
        for (const float v : c.buffers[0].samples) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.2f);
    }
    // A run whose length is not a whole number of blocks ends on a partial one, and the recorder stops at its buffer's
    // end: a buffer shorter than the run keeps its frames, and a run shorter than the buffer leaves the rest zero.
    const auto input = signal(3000, 1, 5);
    auto shortRun = chainRequest(input, 1, 48000.0, 48000.0, false, 1.0, 3000);
    shortRun.frames = 1500;
    const auto s = jobs.submit(std::move(shortRun));
    REQUIRE(s.id != 0);
    const auto c = one(jobs, s.id);
    REQUIRE(c.ok);
    REQUIRE(c.buffers[0].samples.size() == 3000);
    CHECK(std::equal(input.begin(), input.begin() + 1500, c.buffers[0].samples.begin()));
    CHECK(std::all_of(c.buffers[0].samples.begin() + 1500, c.buffers[0].samples.end(),
                      [](float v) { return v == 0.0f; }));
    // A job with no document returns its inputs as the submission gave them, an input among its returns.
    OfflineRequest echo;
    echo.inputs.emplace_back(OfflineSamples{signal(100, 2, 9), 2, 32000.0});
    echo.outputs.push_back({1, 10, 32000.0});
    echo.returns = {2, 1};
    const auto e = one(jobs, jobs.submit(echo).id);
    REQUIRE(e.ok);
    REQUIRE(e.buffers.size() == 2);
    CHECK(e.buffers[0].handle == 2);
    CHECK(e.buffers[0].samples == std::vector<float>(10, 0.0f));
    CHECK(e.buffers[1].handle == 1);
    CHECK(e.buffers[1].channels == 2);
    CHECK(e.buffers[1].rate == 32000.0);
    CHECK(e.buffers[1].samples == std::get<OfflineSamples>(echo.inputs[0]).samples);
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("a kit-shaped job names one buffer twice and runs the buffer-reading nodes on its worker",
          "[engine-graph][offline]") {
    // The shape a kit compile submits: one input named by a normalising source and by the dither's `seedFrom`, and the
    // same buffer encoded to DMC bits. The worker is the job's control and block thread at once, so this is where the
    // DPCM node's storage is allocated and the dither's hash is taken; the pool's own registry has to hold every
    // rp.dsp type, and one handle named twice has to resolve to the one resource.
    const std::vector<float> input = signal(2048, 1, 5);
    const std::uint32_t      frames = static_cast<std::uint32_t>(input.size());
    OfflineJobs              jobs;
    OfflineRequest           r;
    r.document = R"({"nodes":[
        {"id":"n","type":"rp.dsp.normalize","settings":{"buffer":"res:1","gain":-0.7,"channels":1}},
        {"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono","loop":false},
         "resources":{"sample":"res:1"}},
        {"id":"d","type":"rp.dsp.dither","settings":{"channels":1,"algorithm":"high-pass-tpdf","bits":4,
         "seedFrom":"res:1"}},
        {"id":"c","type":"rp.dsp.dpcm","settings":{"buffer":"res:1","normalize":true}},
        {"id":"rn","type":"rp.core.buffer-recorder","settings":{"channels":1},"resources":{"target":"res:2"}},
        {"id":"rd","type":"rp.core.buffer-recorder","settings":{"channels":1},"resources":{"target":"res:3"}},
        {"id":"rc","type":"rp.core.buffer-recorder","settings":{"channels":1},"resources":{"target":"res:4"}}],
        "wires":[{"from":"n.out","to":"rn.in"},{"from":"p.out","to":"d.in"},{"from":"d.out","to":"rd.in"},
                 {"from":"c.out","to":"rc.in"}]})";
    r.inputs.emplace_back(OfflineSamples{input, 1, 48000.0});
    for (int i = 0; i < 3; ++i) r.outputs.push_back({1, frames, 48000.0});
    r.returns = {2, 3, 4};
    r.frames = frames;
    r.rate = 48000.0;
    const auto s = jobs.submit(r);
    INFO(s.error);
    REQUIRE(s.id != 0);
    const auto c = one(jobs, s.id);
    INFO(c.error);
    REQUIRE(c.ok);
    REQUIRE(c.buffers.size() == 3);
    CHECK(c.buffers[0].handle == 2);
    CHECK(c.buffers[1].handle == 3);
    CHECK(c.buffers[2].handle == 4);

    // The normalising gain, as the kit path folds it.
    std::vector<float> normalised(frames);
    {
        rp::dsp::Gain gain;
        gain.init(rp::dsp::normalizedGain(-0.7f, rp::dsp::peak(input.data(), input.size())));
        gain.process(input.data(), normalised.data(), input.size());
    }
    CHECK(c.buffers[0].samples == normalised);

    // The dither's levels, from the hash of the whole buffer the job hands both the player and `seedFrom`.
    std::vector<std::uint16_t> codes(frames);
    {
        rp::dsp::Dither dither;
        dither.init(rp::dsp::DitherAlgorithm::HighPassTPDF, 4, rp::dsp::inputSeed(input.data(), input.size()));
        dither.process(input.data(), codes.data(), frames);
    }
    std::vector<float> levels(frames);
    for (std::size_t i = 0; i < frames; ++i) levels[i] = rp::dsp::codeToLevel(codes[i], 4);
    CHECK(c.buffers[1].samples == levels);

    // The DMC bits as levels, the encode a risa slot's DMC bytes are packed from.
    std::vector<std::uint8_t> bits(frames);
    rp::dsp::encodeDpcm(input.data(), input.size(), true, bits.data());
    std::vector<float> dpcm(frames);
    for (std::size_t i = 0; i < frames; ++i) dpcm[i] = bits[i] ? 1.0f : -1.0f;
    CHECK(c.buffers[2].samples == dpcm);
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("a guard that fires in an offline job is counted in its completion; a non-finite recording is flagged",
          "[engine-graph][offline]") {
    // rp.dsp.biquad resets a filter whose state went non-finite and rp.dsp.dither restarts a channel whose state did,
    // where the kernels latch. A job's host takes their reports (rp.host.guards/1), and the job still completes, ok,
    // carrying how many fired and the first one's reason: native reports, the caller decides (a kit compile refuses
    // the pass, a render can accept a filter that recovered). Each route here starts from finite samples, which a kit
    // compile's own refusals let through: a +96 dB peak over samples near the top of the float range overflows the
    // filter's state, a filter above the plan's Nyquist runs away on an ordinary signal, and an error diffusion (JJN's)
    // over samples near the top of the range overflows the scaled value its fed-back errors are taken from. The same
    // jobs with the guard's trigger taken away count none, so the count is the guard's. A live host offers no report
    // (checked against the engine below) and its nodes recover as before. A value no guard touches is the recording's,
    // and a returned buffer says whether it holds one.
    OfflineJobs jobs;
    const std::uint32_t frames = 4096;
    const auto run = [&](const char* node, const std::vector<float>& input) {
        OfflineRequest r;
        r.document = std::string(R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,
            "layout":"mono","loop":false},"resources":{"sample":"res:1"}},)") + node +
                     R"(,{"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
            "resources":{"target":"res:2"}}],"wires":[{"from":"p.out","to":"x.in"},{"from":"x.out","to":"rec.in"}]})";
        r.inputs.emplace_back(OfflineSamples{input, 1, 48000.0});
        r.outputs.push_back({1, frames, 48000.0});
        r.returns = {2};
        r.frames = frames;
        r.rate = 48000.0;
        const auto s = jobs.submit(r);
        INFO(s.error);
        REQUIRE(s.id != 0);
        return one(jobs, s.id);
    };
    std::vector<float> huge = signal(frames, 1, 3);
    for (float& v : huge) v *= 3.0e38f;  // every sample finite, the loudest near FLT_MAX
    const std::vector<float> ordinary = signal(frames, 1, 4);

    const char* peak = R"({"id":"x","type":"rp.dsp.biquad","settings":{"channels":1,"type":"peak"},
                           "params":{"frequency":1000,"q":1,"gainDb":96}})";
    const char* runaway = R"({"id":"x","type":"rp.dsp.biquad","settings":{"channels":1,"type":"low-pass"},
                              "params":{"frequency":30000,"q":1}})";
    const char* diffusion = R"({"id":"x","type":"rp.dsp.dither","settings":{"channels":1,
                                "algorithm":"jjn","bits":4}})";
    const std::string biquadReason = "rp.dsp.biquad reset a filter whose state went non-finite";
    const std::string ditherReason = "rp.dsp.dither restarted a channel whose state went non-finite";

    const auto guarded = [](const OfflineCompletion& c, const std::string& reason) {
        INFO(c.error);
        REQUIRE(c.ok);
        CHECK(c.guardsFired > 0);
        CHECK(c.guardReason == reason);
        REQUIRE(c.buffers.size() == 1);  // the recording comes back too: what to make of it is the caller's
    };
    guarded(run(peak, huge), biquadReason);
    guarded(run(runaway, ordinary), biquadReason);
    guarded(run(diffusion, huge), ditherReason);

    // The triggers taken away: the +96 dB peak, the above-Nyquist filter and the error diffusion, each over ordinary
    // samples. The low-pass inside Nyquist is the last route's own control: the runaway is the setting, not the signal.
    const auto peakOrdinary = run(peak, ordinary);
    INFO(peakOrdinary.error);
    CHECK(peakOrdinary.ok);
    CHECK(peakOrdinary.guardsFired == 0);
    CHECK(peakOrdinary.guardReason.empty());
    REQUIRE(peakOrdinary.buffers.size() == 1);
    CHECK_FALSE(peakOrdinary.buffers[0].nonFinite);
    const char* settled = R"({"id":"x","type":"rp.dsp.biquad","settings":{"channels":1,"type":"low-pass"},
                             "params":{"frequency":20000,"q":1}})";
    const auto  settledOrdinary = run(settled, ordinary);
    INFO(settledOrdinary.error);
    CHECK(settledOrdinary.ok);
    CHECK(settledOrdinary.guardsFired == 0);
    CHECK(settledOrdinary.guardReason.empty());

    // A value no guard touches reaches the recording, and the returned buffer says so: a plain gain over the huge
    // samples overflows to infinities, which the job completes with (nothing in it misbehaved) and reports.
    const char* loudGain = R"({"id":"x","type":"rp.dsp.gain","settings":{"channels":1},"params":{"gain":50}})";
    const auto  overflowed = run(loudGain, huge);
    INFO(overflowed.error);
    REQUIRE(overflowed.ok);
    REQUIRE(overflowed.buffers.size() == 1);
    CHECK(overflowed.buffers[0].nonFinite);
    CHECK(std::any_of(overflowed.buffers[0].samples.begin(), overflowed.buffers[0].samples.end(),
                      [](float v) { return std::isinf(v); }));
    const auto ditherOrdinary = run(diffusion, ordinary);
    INFO(ditherOrdinary.error);
    CHECK(ditherOrdinary.ok);
    CHECK(ditherOrdinary.guardsFired == 0);
    CHECK(jobs.outstanding() == 0);

    // The live engine offers its nodes no guard report, so a live guard recovers with nobody told; the systems service
    // answers, so the null is the engine's answer for this id and not a lookup that reaches nothing.
    Engine engine(48000.0);
    CHECK(engine.graphService(RP_HOST_GUARDS_INTERFACE) == nullptr);
    CHECK(engine.graphService(RP_HOST_SYSTEMS_INTERFACE) != nullptr);
}

TEST_CASE("an offline job computes in the default floating-point mode whatever its submitter's mode",
          "[engine-graph][offline]") {
    // A worker starts with the environment of the thread whose submission started it under glibc (its documented
    // pthread_create behaviour; a Windows thread starts in the default mode), and an audio host commonly sets
    // flush-to-zero on its threads. A job pins the default mode for its run, so a buffer of subnormals through a unity
    // gain comes back as it went in, where under denormals-are-zero it would come back zeros.
    std::vector<float> subnormal(1024);
    for (std::size_t i = 0; i < subnormal.size(); ++i) subnormal[i] = 1.0e-39f * static_cast<float>(i % 7 + 1);
    REQUIRE(std::fpclassify(subnormal[0]) == FP_SUBNORMAL);
    OfflineRequest r;
    r.document = R"({"nodes":[{"id":"p","type":"rp.core.buffer-player","settings":{"channels":1,"layout":"mono",
        "loop":false},"resources":{"sample":"res:1"}},{"id":"g","type":"rp.dsp.gain","settings":{"channels":1},
        "params":{"gain":1}},{"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
        "resources":{"target":"res:2"}}],"wires":[{"from":"p.out","to":"g.in"},{"from":"g.out","to":"rec.in"}]})";
    r.inputs.emplace_back(OfflineSamples{subnormal, 1, 48000.0});
    r.outputs.push_back({1, 1024, 48000.0});
    r.returns = {2};
    r.frames = 1024;
    r.rate = 48000.0;

    // The job's worker is made by the submitter's submit, on a thread that flushes subnormals, so on glibc the worker
    // inherits that mode. The probe is a thread of that submitter's own: on a platform whose threads start in the
    // default mode the job would pass vacuously, so that is where this case skips.
    OfflineJobs       jobs;
    OfflineCompletion c;
    bool              flushable = false;
    bool              inherited = false;
    bool              submitted = false;
    std::thread       submitter([&] {
        if (!setFlushToZero()) return;
        flushable = true;
        std::thread probe([&] {
            volatile float tiny = 1.0e-39f;
            volatile float one = 1.0f;
            inherited = std::fpclassify(tiny * one) == FP_ZERO;
        });
        probe.join();
        if (!inherited) return;
        const auto s = jobs.submit(r);
        submitted = s.id != 0;
        if (submitted) c = one(jobs, s.id);
    });
    submitter.join();
    if (!flushable) SKIP("this target has no flush-to-zero mode this test knows how to set");
    if (!inherited) SKIP("a new thread does not inherit the submitter's flush-to-zero mode on this platform");
    REQUIRE(submitted);
    INFO(c.error);
    REQUIRE(c.ok);
    REQUIRE(c.buffers.size() == 1);
    CHECK(c.buffers[0].samples == subnormal);
}

TEST_CASE("the floating-point guard puts the caller's environment back", "[engine-graph][offline]") {
#if defined(RP_OFFLINE_FP_MXCSR)
    const unsigned originalMxcsr = _mm_getcsr();
    const int      originalRound = std::fegetround();
    std::fesetround(FE_UPWARD);
    _mm_setcsr(_mm_getcsr() | 0x8040u);  // MXCSR FTZ (bit 15) and DAZ (bit 6) set before the guard saves them
    const unsigned flushing = _mm_getcsr();
    {
        const rp::host::DefaultFloatEnvironment environment;
        CHECK((_mm_getcsr() & 0x8040u) == 0);
        // MXCSR's own rounding bits (13-14), not just the x87 control word fegetround reads: a guard that re-applied
        // the caller's SSE rounding would pass every other check here, and the kernels compute in SSE.
        CHECK((_mm_getcsr() & 0x6000u) == 0);
        CHECK(std::fegetround() == FE_TONEAREST);
    }
    CHECK(_mm_getcsr() == flushing);
    CHECK(std::fegetround() == FE_UPWARD);
    _mm_setcsr(originalMxcsr);
    std::fesetround(originalRound);
#elif defined(RP_OFFLINE_FP_FPCR)
    std::uint64_t originalFpcr = 0;
    asm volatile("mrs %0, fpcr" : "=r"(originalFpcr));
    const int originalRound = std::fegetround();
    std::fesetround(FE_UPWARD);
    constexpr std::uint64_t kFlush = (std::uint64_t(1) << 24) | (std::uint64_t(1) << 19);  // FPCR.FZ and FZ16
    std::uint64_t flushing = 0;
    asm volatile("mrs %0, fpcr" : "=r"(flushing));
    flushing |= kFlush;
    asm volatile("msr fpcr, %0" : : "r"(flushing));
    {
        const rp::host::DefaultFloatEnvironment environment;
        std::uint64_t inside = 0;
        asm volatile("mrs %0, fpcr" : "=r"(inside));
        CHECK((inside & kFlush) == 0);
        CHECK(std::fegetround() == FE_TONEAREST);
    }
    std::uint64_t after = 0;
    asm volatile("mrs %0, fpcr" : "=r"(after));
    CHECK(after == flushing);
    CHECK(std::fegetround() == FE_UPWARD);
    asm volatile("msr fpcr, %0" : : "r"(originalFpcr));
    std::fesetround(originalRound);
#else
    SKIP("this target has no flush-to-zero bit this test knows how to inspect");
#endif
}

TEST_CASE("offline jobs past the pool's concurrency queue, and each returns its own result",
          "[engine-graph][offline]") {
    OfflineJobs jobs;
    const std::size_t count = std::min<std::size_t>(3 * OfflineJobs::concurrency() + 1, OfflineJobs::kMaxJobs);
    const auto input = signal(6000, 1, 11);
    std::vector<std::uint32_t> ids;
    std::map<std::uint32_t, double> gains;
    for (std::size_t i = 0; i < count; ++i) {
        const double gain = 0.1 + 0.05 * double(i);
        const auto s = jobs.submit(chainRequest(input, 1, 44100.0, 11468.0, true, gain, 1500));
        INFO(s.error);
        REQUIRE(s.id != 0);
        ids.push_back(s.id);
        gains[s.id] = gain;
    }
    CHECK(jobs.workers() <= OfflineJobs::concurrency());
    const auto done = collect([&](auto w) { return jobs.drain(w); }, ids);
    REQUIRE(done.size() == count);
    // Each job's result is its own gain over the same resample: the rig's run of that gain, bit for bit.
    for (const auto id : ids) {
        const auto& c = done.at(id);
        INFO(c.error);
        REQUIRE(c.ok);
        CHECK(c.buffers[0].samples == rigReference(input, 1, 44100.0, 11468.0, true, gains[id], 1500));
    }
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("two jobs submitted against one parked worker start a second one", "[engine-graph][offline]") {
    if (OfflineJobs::concurrency() < 2) SKIP("one hardware thread: the pool runs one worker");
    OfflineJobs jobs;
    // One worker, started and parked again.
    REQUIRE(one(jobs, jobs.submit(chainRequest(signal(64, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 64)).id).ok);
    REQUIRE(jobs.workers() == 1);
    // Two long jobs back to back: the parked worker is claimed by the first, so the second starts a worker of its own
    // rather than queue behind a job that runs for hours.
    const auto a = jobs.submit(longRequest()).id;
    const auto b = jobs.submit(longRequest()).id;
    CHECK(jobs.workers() == 2);
    CHECK(jobs.cancel(a));
    CHECK(jobs.cancel(b));
    CHECK(collect([&](auto w) { return jobs.drain(w); }, {a, b}).size() == 2);
}

TEST_CASE("a cancel stops an offline job, queued or running, with the cancel reason", "[engine-graph][offline]") {
    OfflineJobs jobs;
    // Fill every worker with a long job, then one more, which queues.
    std::vector<std::uint32_t> running;
    for (unsigned i = 0; i < OfflineJobs::concurrency(); ++i) running.push_back(jobs.submit(longRequest()).id);
    const auto queued = jobs.submit(longRequest()).id;
    REQUIRE(queued != 0);
    // The queued one completes at the cancel, unrun.
    CHECK(jobs.cancel(queued));
    const auto first = jobs.drain();
    REQUIRE(first.size() == 1);
    CHECK(first[0].id == queued);
    CHECK_FALSE(first[0].ok);
    CHECK(first[0].error == "cancelled");
    CHECK_FALSE(jobs.cancel(queued));  // finished: nothing to cancel
    // The running ones stop at their next block: a day of audio each, cut short within seconds.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const auto t0 = std::chrono::steady_clock::now();
    for (const auto id : running) CHECK(jobs.cancel(id));
    const auto done = collect([&](auto w) { return jobs.drain(w); }, running);
    REQUIRE(done.size() == running.size());
    for (const auto& [id, c] : done) {
        CHECK_FALSE(c.ok);
        CHECK(c.error == "cancelled");
        CHECK(c.buffers.empty());
    }
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
    CHECK_FALSE(jobs.cancel(9999));  // never taken
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("a cancel that arrives after a job's last block still completes cancelled", "[engine-graph][offline]") {
    const rp_package* extra[] = {gatePackage()};
    OfflineJobs       jobs(extra);
    Gate::entered.store(false, std::memory_order_release);
    Gate::release.store(false, std::memory_order_release);
    OfflineRequest r;
    r.document = R"({"nodes":[{"id":"g","type":"rp.test.gate"}],"wires":[]})";
    r.frames = 10;  // one partial block, and the run checks for a cancel only before it
    r.rate = 48000.0;
    const auto s = jobs.submit(r);
    REQUIRE(s.id != 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!Gate::entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool entered = Gate::entered.load(std::memory_order_acquire);
    CHECK(entered);
    if (entered) CHECK(jobs.cancel(s.id));  // the run is past its last check; the worker still makes this the outcome
    Gate::release.store(true, std::memory_order_release);
    const auto c = one(jobs, s.id);
    CHECK_FALSE(c.ok);
    CHECK(c.error == "cancelled");
}

TEST_CASE("a cancel reaches a job between its file decodes", "[engine-graph][offline]") {
    TempDir     dir;
    OfflineJobs jobs;
    // Several long sources: the worker is decoding when the cancel lands, and a job that went on decoding after it
    // would report every one of them, where one that checks between decodes stops at the next.
    const auto path = dir.path / "long.wav";
    writeWav(path, signal(std::size_t(48000) * 60, 1, 5, 48000.0), 48000, true);
    constexpr std::size_t kFiles = 16;
    OfflineRequest        r;
    r.frames = 10;
    r.rate = 48000.0;
    for (std::size_t i = 0; i < kFiles; ++i) r.inputs.emplace_back(OfflineFile{path.string(), 0, 0, std::nullopt});
    const auto s = jobs.submit(r);
    REQUIRE(s.id != 0);
    // The sleep is what puts the cancel into a decode, and the case relies on the 16 decodes outlasting it: a cancel
    // that lands while the job is still queued completes it unrun and passes without exercising the check, and a
    // sleep that outlasts every decode lets the job finish first, so the cancel returns false.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    CHECK(jobs.cancel(s.id));
    const auto c = one(jobs, s.id);
    CHECK_FALSE(c.ok);
    CHECK(c.error == "cancelled");
    CHECK(c.inputs.size() < kFiles);  // the decodes after the cancel never ran
}

TEST_CASE("an engine destroyed with offline jobs in flight abandons them and joins every worker",
          "[engine-graph][offline]") {
    const int before = threadCountBaseline();
    const auto t0 = std::chrono::steady_clock::now();
    {
        Engine e(48000.0);
        CHECK(e.offlineJobsOutstanding() == 0);
        CHECK(e.drainOfflineJobs().empty());
        CHECK_FALSE(e.cancelOfflineJob(1));
        // Running and queued long jobs, a short one that may have finished undrained.
        for (unsigned i = 0; i < OfflineJobs::concurrency() + 2; ++i)
            REQUIRE(e.submitOfflineJob(longRequest()).id != 0);
        REQUIRE(e.submitOfflineJob(chainRequest(signal(500, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 500)).id != 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (before >= 0) CHECK(threadCount() > before);  // the workers are there
    }
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
    if (before >= 0) CHECK(threadCountSettled(before) <= before);  // and gone, every one (the pool's destructor joins)
    // The pool alone, the same way.
    {
        OfflineJobs jobs;
        for (unsigned i = 0; i < OfflineJobs::concurrency() + 2; ++i) REQUIRE(jobs.submit(longRequest()).id != 0);
    }
    if (before >= 0) CHECK(threadCountSettled(before) <= before);
}

TEST_CASE("the offline job bound refuses past it, and takes again once a drain makes room", "[engine-graph][offline]") {
    OfflineJobs jobs;
    std::vector<std::uint32_t> ids;
    for (std::size_t i = 0; i < OfflineJobs::kMaxJobs; ++i) {
        const auto s = jobs.submit(longRequest());
        REQUIRE(s.id != 0);
        ids.push_back(s.id);
    }
    CHECK(jobs.outstanding() == OfflineJobs::kMaxJobs);
    const auto refused = jobs.submit(longRequest());
    CHECK(refused.id == 0);
    CHECK(refused.error == "busy: 64 offline jobs are outstanding");
    for (const auto id : ids) CHECK(jobs.cancel(id));
    // Finished and undrained still counts: the bound holds room for every completion.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(jobs.submit(longRequest()).id == 0);
    const auto done = collect([&](auto w) { return jobs.drain(w); }, ids);
    CHECK(done.size() == OfflineJobs::kMaxJobs);
    const auto again = jobs.submit(chainRequest(signal(64, 1, 2), 1, 48000.0, 48000.0, false, 1.0, 64));
    CHECK(again.id == ids.back() + 1);  // ids count up, the refusals taking none
    CHECK(one(jobs, again.id).ok);
}

TEST_CASE("every offline refusal says why, at submission or in its completion", "[engine-graph][offline]") {
    const rp_package* extra[] = {rp::graph::test::testPackage(), unreadyPackage()};
    OfflineJobs jobs(extra);
    const auto refusal = [&](OfflineRequest r) { return jobs.submit(std::move(r)).error; };
    const auto failure = [&](OfflineRequest r) {
        const auto s = jobs.submit(std::move(r));
        INFO(s.error);
        REQUIRE(s.id != 0);
        const auto c = one(jobs, s.id);
        CHECK_FALSE(c.ok);
        return c.error;
    };
    const auto base = [] { return chainRequest(signal(64, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 64); };

    // At submission.
    auto r = base();
    r.rate = 0.0;
    CHECK(refusal(r).find("sample rate") != std::string::npos);
    r = base();
    r.outputs.assign(OfflineJobs::kMaxResources, OfflineOutput{1, 1, 48000.0});
    CHECK(refusal(r) == "a job takes at most 64 inputs and outputs, not 65");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).channels = 0;
    CHECK(refusal(r) == "input 0 has no channels");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).channels = 3;
    CHECK(refusal(r) == "input 0 holds 64 samples, not a whole number of 3-channel frames");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).rate = std::numeric_limits<double>::quiet_NaN();
    CHECK(refusal(r) == "input 0 needs a sample rate that is a positive number");
    r = base();
    r.inputs[0] = OfflineFile{};
    CHECK(refusal(r) == "input 0 names no file");
    r = base();
    r.inputs[0] = OfflineFile{"x.wav", 0, 0, OfflineCap{10, -1.0, OfflineCapArithmetic::DoubleScale}};
    CHECK(refusal(r) == "input 0's cap needs a rate that is a positive number");
    r = base();
    r.outputs[0].frames = 0;
    CHECK(refusal(r) == "output 0 needs at least one channel and one frame");
    r = base();
    r.returns = {3};
    CHECK(refusal(r) == "a return names res:3, which is no input or output of the job");

    // In the completion: a file that will not decode, a document that is not one, the planner's refusals (a type no
    // offline host has, a script node or a system node among them, and a handle an empty input leaves naming nothing),
    // a node that will not activate, and a node that faults.
    r = base();
    r.inputs[0] = OfflineFile{"/nonexistent/rp-offline.wav", 0, 0, std::nullopt};
    CHECK(failure(r) == "input 0: /nonexistent/rp-offline.wav could not be read or decoded");
    r = base();
    r.document = "{";
    CHECK(failure(r).rfind("not a graph document: ", 0) == 0);
    r = base();
    r.document = R"({"nodes":[{"id":"gb","type":"rp.sameboy.gb"},{"id":"route","type":"rp.midi.route"}],"wires":[]})";
    CHECK(failure(r) == "the document was refused: gb: unknown-type: no node type \"rp.sameboy.gb\" is registered; "
                        "route: unknown-type: no node type \"rp.midi.route\" is registered");
    r = base();
    std::get<OfflineSamples>(r.inputs[0]).samples.clear();  // an input of no frames: res:1 names nothing
    CHECK(failure(r).find("unknown-resource") != std::string::npos);
    r = base();
    r.document = R"({"nodes":[{"id":"u","type":"rp.test.unready"}],"wires":[]})";
    CHECK(failure(r).rfind("the document was refused: u: activate-failed", 0) == 0);
    r = base();
    r.document = R"({"nodes":[{"id":"f","type":"rp.test.faulty","settings":{"failAt":2}},
                              {"id":"rec","type":"rp.core.buffer-recorder","settings":{"channels":1},
                               "resources":{"target":"res:2"}}],
                     "wires":[{"from":"f.out","to":"rec.in"}]})";
    r.frames = 4096;
    CHECK(failure(r) == "a node faulted: f");
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("the offline RPCs carry a job's samples as float32 bytes, and refuse an input of neither or both kinds",
          "[engine-graph][offline]") {
    Engine           e(48000.0);
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, invoker);
    const auto input = signal(256, 2, 4);
    const auto bytesOf = [](const std::vector<float>& v) {
        const auto* p = reinterpret_cast<const std::byte*>(v.data());
        return rfl::Bytestring(p, p + v.size() * sizeof(float));
    };
    RpcOfflineRequest req;
    req.document = chainDocument(false, 0.5, 2);
    req.inputs.push_back({bytesOf(input), 2u, 48000.0, std::nullopt, std::nullopt, std::nullopt, std::nullopt});
    req.outputs.push_back({2, 256, 48000.0});
    req.returns = {2};
    req.frames = 256;
    req.rate = 48000.0;
    const auto s = rpc.submitOfflineJob(req);
    REQUIRE(s.id.has_value());
    CHECK_FALSE(s.error.has_value());
    std::vector<RpcOfflineCompletion> done;
    for (int i = 0; i < 100 && done.empty(); ++i) {
        auto drained = rpc.drainOfflineJobs(100.0);
        CHECK(drained.inFlight <= 1);
        done = std::move(drained.completions);
    }
    REQUIRE(done.size() == 1);
    CHECK(done[0].id == *s.id);
    INFO(done[0].error);
    REQUIRE(done[0].ok);
    REQUIRE(done[0].buffers.size() == 1);
    std::vector<float> got(done[0].buffers[0].samples.size() / sizeof(float));
    std::memcpy(got.data(), done[0].buffers[0].samples.data(), done[0].buffers[0].samples.size());
    REQUIRE(got.size() == input.size());
    for (std::size_t i = 0; i < got.size(); ++i) CHECK(got[i] == input[i] * 0.5f);
    const auto idle = rpc.drainOfflineJobs(std::numeric_limits<double>::quiet_NaN());  // nothing in flight: no wait
    CHECK(idle.completions.empty());
    CHECK(idle.inFlight == 0);
    // A job still running is counted in flight, so a caller can tell a job native holds from one it does not.
    const auto running = rpc.submitOfflineJob([&] {
        RpcOfflineRequest r = req;
        r.frames = std::numeric_limits<std::uint32_t>::max();
        r.document = chainDocument(false, 0.5, 2);
        return r;
    }());
    REQUIRE(running.id.has_value());
    CHECK(rpc.drainOfflineJobs(0.0).inFlight == 1);
    CHECK(rpc.cancelOfflineJob(*running.id));
    std::size_t cancelled = 0;
    for (int i = 0; i < 100 && cancelled == 0; ++i) cancelled = rpc.drainOfflineJobs(100.0).completions.size();
    CHECK(cancelled == 1);
    CHECK_FALSE(rpc.cancelOfflineJob(*s.id));

    auto bad = req;
    bad.inputs[0].path = "x.wav";
    CHECK(rpc.submitOfflineJob(bad).error ==
          "input 0 must be samples (samples, channels and rate) or a file (a path and its window)");
    bad = req;
    bad.inputs[0] = {std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
    CHECK(rpc.submitOfflineJob(bad).error ==
          "input 0 must be samples (samples, channels and rate) or a file (a path and its window)");
    bad = req;
    bad.inputs[0].rate.reset();
    CHECK(rpc.submitOfflineJob(bad).error == "input 0 needs samples, channels and a rate");
    bad = req;
    bad.inputs[0].samples = rfl::Bytestring(3);
    CHECK(rpc.submitOfflineJob(bad).error == "input 0's samples are 3 bytes, not a whole number of float32 values");
    bad = req;
    bad.inputs[0] = {std::nullopt, std::nullopt, std::nullopt, std::nullopt, 4u, std::nullopt, std::nullopt};
    CHECK(rpc.submitOfflineJob(bad).error == "input 0 needs a path");
    bad = req;
    bad.inputs[0] = {std::nullopt, std::nullopt, std::nullopt, "x.wav", std::nullopt, std::nullopt,
                     RpcOfflineCap{10, 11468.0, "float"}};
    CHECK(rpc.submitOfflineJob(bad).error ==
          "input 0's cap arithmetic \"float\" is neither \"float-ratio\" nor \"double-scale\"");
    CHECK(e.offlineJobsOutstanding() == 0);
}

TEST_CASE("the offline window is each kit compiler's window, the cap's arithmetic included",
          "[engine-graph][offline]") {
    // Directly, over rates and lengths where the two arithmetics' roundings matter: every PAL rate for risa's, LSDj's
    // kit rate for its own, at decoded rates a file carries, with offsets at, past and before the end and the zero
    // length, against the windows as their homes compute them.
    for (const std::uint32_t rate :
         {8000u, 11025u, 11468u, 16000u, 22050u, 32000u, 44100u, 48000u, 88200u, 96000u, 192000u}) {
        for (const std::size_t size : {std::size_t(0), std::size_t(1), std::size_t(9999), std::size_t(400000)}) {
            for (const std::uint32_t offset : {0u, 1u, 5000u, 399999u, 400000u, 400001u}) {
                for (const std::uint32_t length : {0u, 1u, 30000u}) {
                    INFO(rate << " Hz, " << size << " frames, offset " << offset << ", length " << length);
                    const auto w = checkedWindow(size, rate, offset, length, lsdjCap());
                    const auto [ls, lf] = lsdjWindow(size, rate, offset, length);
                    CHECK(w.start == ls);
                    CHECK(w.frames == lf);
                    for (int i = 0; i < 16; ++i) {
                        const auto r = checkedWindow(size, rate, offset, length, risaCap(i));
                        const auto [rs, rf] = risaWindow(size, rate, offset, length, kPalRates[i]);
                        CHECK(r.start == rs);
                        CHECK(r.frames == rf);
                    }
                    const auto none = checkedWindow(size, rate, offset, length, std::nullopt);
                    const std::size_t left = size - std::min<std::size_t>(offset, size);
                    CHECK(none.frames == std::min<std::size_t>(length ? length : size, left));
                }
            }
        }
    }
    // Every whole decoded rate a file may carry up to 96 kHz, the cap binding: the job's window and the kit's agree at
    // each one, LSDj's and all sixteen of risa's.
    int lsdjMismatches = 0, risaMismatches = 0;
    for (std::uint32_t rate = 1000; rate <= 96000; ++rate) {
        if (checkedWindow(1u << 22, rate, 0, 0, lsdjCap()).frames != lsdjWindow(1u << 22, rate, 0, 0).second)
            ++lsdjMismatches;
        for (int i = 0; i < 16; ++i)
            if (checkedWindow(1u << 22, rate, 0, 0, risaCap(i)).frames !=
                risaWindow(1u << 22, rate, 0, 0, kPalRates[i]).second)
                ++risaMismatches;
    }
    CHECK(lsdjMismatches == 0);
    CHECK(risaMismatches == 0);
    // The two arithmetics really differ somewhere a kit reaches: the same cap through the other one moves a frame.
    bool differs = false;
    for (std::uint32_t rate = 8000; rate <= 96000 && !differs; rate += 1)
        differs = checkedWindow(1u << 20, rate, 0, 0, lsdjCap()).frames !=
                  checkedWindow(1u << 20, rate, 0, 0,
                                OfflineCap{kLsdjCapFrames, 11468.0, OfflineCapArithmetic::DoubleScale}).frames;
    CHECK(differs);
}

TEST_CASE("a file input is SampleCache's decode, windowed as the two kit windows spell it, with its report",
          "[engine-graph][offline]") {
    TempDir     dir;
    OfflineJobs jobs;
    // Two decoded rates, three seconds each: long enough that both caps bind from the start (LSDj's at its kit rate,
    // risa's at the fastest PAL rate) and neither does from late in the file. 16-bit PCM, so the decode converts.
    for (const std::uint32_t rate : {44100u, 22050u}) {
        const auto path = dir.path / ("src-" + std::to_string(rate) + ".wav");
        writeWav(path, signal(std::size_t(rate) * 3, 1, rate, double(rate)), rate, false);
        rp::kit::SampleCache cache;
        const rp::kit::SampleData* decoded = cache.getOrLoad(path.string());
        REQUIRE(decoded);
        const std::size_t size = decoded->buffer.size();
        REQUIRE(decoded->sampleRate == rate);

        // The whole file: the decode, bit for bit, at its rate, with its report.
        auto whole = one(jobs, jobs.submit(fileRequest(path, 0, 0, std::nullopt)).id);
        INFO(whole.error);
        REQUIRE(whole.ok);
        CHECK(whole.buffers[0].samples == decoded->buffer);
        CHECK(whole.buffers[0].rate == double(rate));
        CHECK(whole.buffers[0].channels == 1);
        REQUIRE(whole.inputs.size() == 1);
        CHECK(whole.inputs[0].handle == 1);
        CHECK(whole.inputs[0].decodedRate == rate);
        CHECK(whole.inputs[0].decodedFrames == size);
        CHECK(whole.inputs[0].frames == size);
        CHECK_FALSE(whole.inputs[0].nonFinite);

        struct Edge {
            const char*               name;
            std::uint32_t             offset, length;
            int                       kind;  // 0: LSDj's window, 1..: risa's at PAL index kind - 1
        };
        const auto n = static_cast<std::uint32_t>(size);
        const std::vector<Edge> edges = {
            {"LSDj, no window: the cap binds", 0, 0, 0},
            {"LSDj, offset past the end", n + 10, 0, 0},
            {"LSDj, offset at the end", n, 0, 0},
            {"LSDj, zero length from late on: to the end, under the cap", n - 1000, 0, 0},
            {"LSDj, a length past what is left", n - 300, 5000, 0},
            {"LSDj, a length under the cap", 100, 2000, 0},
            {"risa fastest, no window: the cap binds", 0, 0, 16},
            {"risa slowest, no window: the file is shorter than the cap", 0, 0, 1},
            {"risa, offset past the end", n + 1, 0, 13},
            {"risa, zero length from the middle", n / 2, 0, 16},
            {"risa, a length past what is left", n - 10, 50, 7},
        };
        for (const Edge& edge : edges) {
            INFO(rate << " Hz: " << edge.name);
            const auto cap = edge.kind == 0 ? lsdjCap() : risaCap(edge.kind - 1);
            const auto [start, frames] = edge.kind == 0 ? lsdjWindow(size, rate, edge.offset, edge.length)
                                                        : risaWindow(size, rate, edge.offset, edge.length,
                                                                     kPalRates[edge.kind - 1]);
            const auto c = one(jobs, jobs.submit(fileRequest(path, edge.offset, edge.length, cap)).id);
            INFO(c.error);
            REQUIRE(c.ok);
            REQUIRE(c.inputs.size() == 1);
            CHECK(c.inputs[0].decodedRate == rate);
            CHECK(c.inputs[0].decodedFrames == size);
            CHECK(c.inputs[0].frames == frames);
            CHECK(c.buffers[0].frames == frames);
            CHECK(c.buffers[0].rate == double(rate));
            CHECK(c.buffers[0].samples ==
                  std::vector<float>(decoded->buffer.begin() + std::ptrdiff_t(start),
                                     decoded->buffer.begin() + std::ptrdiff_t(start + frames)));
        }
        // Not vacuous: at each rate the LSDj and the fastest risa caps cut the whole file short.
        CHECK(lsdjWindow(size, rate, 0, 0).second < size);
        CHECK(risaWindow(size, rate, 0, 0, kPalRates[15]).second < size);
    }

    // A non-finite sample is reported only where the window keeps it. A float WAV, so the NaN survives the decode.
    std::vector<float> withNan = signal(2000, 1, 1);
    withNan[1500] = std::numeric_limits<float>::quiet_NaN();
    const auto nanPath = dir.path / "nan.wav";
    writeWav(nanPath, withNan, 32000, true);
    const auto kept = one(jobs, jobs.submit(fileRequest(nanPath, 1000, 0, std::nullopt)).id);
    REQUIRE(kept.ok);
    CHECK(kept.inputs[0].nonFinite);
    const auto cut = one(jobs, jobs.submit(fileRequest(nanPath, 0, 1500, std::nullopt)).id);
    REQUIRE(cut.ok);
    CHECK_FALSE(cut.inputs[0].nonFinite);

    // An empty window is an input of no frames: reported, returned as a buffer of none at the decoded rate, and named
    // by no resource. The kit compiler makes of it an LSDj slot of no bytes and a one-byte risa slot (a legal 16k+1
    // length) from that report; test-native/kit-compile.test.ts holds both banks (its "EMP" slots).
    const auto empty = one(jobs, jobs.submit(fileRequest(nanPath, 5000, 0, lsdjCap())).id);
    REQUIRE(empty.ok);
    CHECK(empty.inputs[0].frames == 0);
    CHECK(empty.inputs[0].decodedFrames == 2000);
    CHECK(empty.buffers[0].frames == 0);
    CHECK(empty.buffers[0].samples.empty());
    CHECK(empty.buffers[0].rate == 32000.0);

    // A cap whose scaled frame count is infinite is refused, naming the cap, rather than cast to size_t.
    for (const auto arithmetic : {OfflineCapArithmetic::FloatRatio, OfflineCapArithmetic::DoubleScale}) {
        const auto c = one(jobs, jobs.submit(fileRequest(nanPath, 0, 0, OfflineCap{1, 1.0e-300, arithmetic})).id);
        CHECK_FALSE(c.ok);
        CHECK(c.error.find("the cap's frames do not scale to a frame count") != std::string::npos);
    }
    CHECK(one(jobs, jobs.submit(fileRequest(nanPath, 0, 0, std::nullopt)).id).ok);
}

TEST_CASE("an offline job leaves the live plan an audio thread plays bit for bit, its warmed blocks allocating nothing",
          "[engine-graph][offline]") {
    // Two engines play the same live plan, a Game Boy booting mGB, block for block on an audio thread of their own; one
    // of them has offline jobs running (resampling, the costliest node) the whole time. Every block's output is the
    // same, and the block thread allocates and frees nothing over its warmed blocks with the jobs in flight.
    constexpr int           kBlocks = 240;
    // Counted from the first block after a short warm-up, as C4's whole-block cases are: eight blocks let the plan
    // settle and the jobs start, and the window then runs through the rest of the run, which carries every one of the
    // engine's periodic state publishes, the first (about block 46 of a booting Game Boy) and each 24 k frames after
    // it, so the publish path is counted too rather than skipped past.
    constexpr int           kWarm = 8;
    constexpr std::uint32_t kFrames = 512;
    const auto play = [&](bool withJobs, rp::test::AllocationCount& counted) {
        Engine e(48000.0);
        rp::host::test::Roms roms;
        std::vector<rp::host::test::DocSystem> systems{{1, rp::host::test::kGbType}};
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1})).ok);
        std::vector<float> out;
        out.reserve(std::size_t(kBlocks) * kFrames * 2);
        // Long jobs, one per worker, run until cancelled after the audio thread joins, so something is in flight
        // through the whole counted window; the audio thread records how many at its first counted block and at its
        // last.
        std::vector<std::uint32_t> ids;
        if (withJobs)
            for (unsigned i = 0; i < OfflineJobs::concurrency(); ++i) {
                const auto s = e.submitOfflineJob(longRequest());
                if (s.id) ids.push_back(s.id);
            }
        std::atomic<std::size_t> atWarm{0}, atLast{0};
        std::atomic<bool>        playing{true};
        std::atomic<int>         blocks{0};          // how many the audio thread has finished
        std::atomic<bool>        drainBesideBlocks{false};  // a drain ran while it was still playing
        e.setAudioThreadOwns(true);
        std::thread audio([&] {
            std::vector<float> l(kFrames), r(kFrames);
            float*             outs[2] = {l.data(), r.data()};
            for (int b = 0; b < kBlocks; ++b) {
                if (b == kWarm) {
                    atWarm.store(e.offlineJobsOutstanding(), std::memory_order_release);
                    rp::test::beginCountingAllocations();
                }
                if (b == kBlocks - 1) atLast.store(e.offlineJobsOutstanding(), std::memory_order_release);
                e.drainCommands();
                e.processBlock(kFrames, outs, 2);
                blocks.store(b + 1, std::memory_order_release);
                for (std::uint32_t i = 0; i < kFrames; ++i) {
                    out.push_back(l[i]);
                    out.push_back(r[i]);
                }
            }
            playing.store(false, std::memory_order_release);
            counted = rp::test::endCountingAllocations();
        });
        // The main thread drains the pool beside the running blocks, as a host's control thread does, so this case is
        // the one that watches a drain and a `processBlock` interleave, and it records that it did rather than trusting
        // the schedule: a drain that ran with the audio thread between its first block and its last. The long jobs
        // never complete, so the drains find nothing and change nothing else.
        if (withJobs)
            while (playing.load(std::memory_order_acquire)) {
                e.drainOfflineJobs(std::chrono::milliseconds(5));
                const int done = blocks.load(std::memory_order_acquire);
                if (done > 0 && done < kBlocks) drainBesideBlocks.store(true, std::memory_order_release);
            }
        audio.join();
        e.setAudioThreadOwns(false);
        if (withJobs) {
            CHECK_FALSE(ids.empty());
            CHECK(atWarm.load(std::memory_order_acquire) > 0);
            CHECK(atLast.load(std::memory_order_acquire) > 0);
            CHECK(drainBesideBlocks.load(std::memory_order_acquire));
            for (const auto id : ids) CHECK(e.cancelOfflineJob(id));
            CHECK(collect([&](auto w) { return e.drainOfflineJobs(w); }, ids).size() == ids.size());
        }
        return out;
    };
    rp::test::AllocationCount quiet, busy;
    const auto alone = play(false, quiet);
    const auto beside = play(true, busy);
    // Not vacuous: the plan sounds.
    double energy = 0.0;
    for (const float v : alone) energy += double(v) * v;
    CHECK(energy > 0.0);
    CHECK(beside == alone);
    CHECK(busy.allocations == quiet.allocations);
    CHECK(busy.frees == quiet.frees);
    CHECK(busy.allocations == 0);
    CHECK(busy.frees == 0);
}

TEST_CASE("offline job costs", "[.][offline-bench]") {
    // What one job costs end to end (submit to completion drained): the host built, one block, torn down; and one
    // second of 44.1 kHz audio resampled to the LSDj kit rate, recorded and returned.
    OfflineJobs jobs;
    const auto time = [&](const OfflineRequest& r, int runs) {
        double best = 1e9, total = 0.0;
        for (int i = 0; i < runs; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const auto c = one(jobs, jobs.submit(r).id);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            REQUIRE(c.ok);
            best = std::min(best, ms);
            total += ms;
        }
        return std::pair{best, total / runs};
    };
    const auto trivial = time(chainRequest(signal(64, 1, 1), 1, 48000.0, 48000.0, false, 1.0, 64), 200);
    const auto oneSecond = chainRequest(signal(44100, 1, 1), 1, 44100.0, 11468.0, true, 1.0, 11468);
    const auto resample = time(oneSecond, 50);
    std::printf("[offline-bench] trivial job: best %.3f ms, mean %.3f ms\n", trivial.first, trivial.second);
    std::printf("[offline-bench] 1 s of 44.1 kHz resampled to 11468 Hz: best %.3f ms, mean %.3f ms\n", resample.first,
                resample.second);
    // A kit's fan-out: fifteen such resamples at once on the pool, the requests made before the clock starts. The first
    // round starts the workers it needs; the second finds them parked.
    std::vector<OfflineRequest> requests;
    for (int i = 0; i < 15; ++i)
        requests.push_back(chainRequest(signal(44100, 1, i), 1, 44100.0, 11468.0, true, 1.0, 11468));
    for (const char* round : {"cold", "warm"}) {
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<std::uint32_t> ids;
        for (const auto& r : requests) ids.push_back(jobs.submit(r).id);
        const auto done = collect([&](auto w) { return jobs.drain(w); }, ids);
        REQUIRE(done.size() == 15);
        std::printf("[offline-bench] 15 of them at once (%u workers, %s): %.3f ms\n", OfflineJobs::concurrency(), round,
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
}

// Engine jobs (docs/modular-graph-phase-d3-design.md section 3, host/engine/OfflineJobs.hpp): a render run on a worker
// of the engine's offline pool by a RenderHost the worker builds, runs and destroys. A render job writes the WAV a
// RenderHost run directly in continuous blocks writes for the same spec, byte for byte, reporting its progress (rising
// chunk by chunk to the rendered length on a fixed-length render) and its console lines as it goes; a cancel stops it,
// queued, starting or running, and one taken after its body finished still wins, keeping the files written; an engine
// destroyed with renders in flight cancels and joins them; renders hold at most all but one of the pool's workers, so a
// graph job still runs beside them (a pool of 1 starting its second thread for one), and the ones held back start in
// submission order; an engine job computes in the default floating-point mode; renders run back to back on one reused
// worker and in parallel on several, each giving the bytes a direct run gives; a render's JS stack fits a 512 KiB
// thread, a recursion past it ending the render in an error; the console record keeps its bounds; the RPCs carry all of
// it; and a kind that is not "render" is refused. Continuous blocks themselves (design section 4,
// EngineRpcService::setContinuousBlocks), over an mGB that MIDI plays: a render's bytes do not depend on the lengths
// its calls ask for, a message staged between two calls landing where the block they end in ends, where exact blocks
// land it where the call ended; a carried tail goes only to a call of its own shape (a per-channel render's across its
// calls, on an eight-lane host), and a call of another shape, a renderBlocks run or a rate change drops it; exact
// blocks carry nothing.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[engine-jobs]"`. The ROMs are the
// repo's own (resources/roms), which render silence through the render library with no song or MIDI but for an mGB
// booted as a Super Game Boy with Start unpressed, whose boot jingle sounds: the back-to-back and parallel case renders
// two, so on CI the job-against-direct comparison holds sound there and the WAV's header and length elsewhere. The case
// that needs a sounding render of a song reads an LSDj one from the fixture tree (RETROPLUG_RESOURCES_DIR) and skips
// without it. A few cases give the pool a body of their own (EngineJobBody) to hold its rules to an exact timeline.
// Every core in this binary powers on in a fixed state (Deterministic.cpp), so two renders of one spec agree, and
// SameBoy's power-on generator is off.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

#if !defined(_WIN32)
#include <pthread.h>
#endif

#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/engine/OfflineJobs.hpp"
#include "host/render/RenderHost.hpp"
#include "host/rpc/EngineRpcService.hpp"

#include "GraphDocs.hpp"

using rp::host::EngineJobBody;
using rp::host::EngineJobCompletion;
using rp::host::EngineJobHooks;
using rp::host::EngineJobOutcome;
using rp::host::EngineJobRequest;
using rp::host::EngineJobLine;
using rp::host::EngineJobProgress;
using rp::host::EngineJobRecord;
using rp::host::EngineJobsDrain;
using rp::host::EngineJobStream;
using rp::host::OfflineJobs;
using rp::host::OfflineRequest;
using rp::host::OfflineSamples;

namespace {

namespace fs = std::filesystem;

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("rp-engine-jobs-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

std::vector<char> readBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

int threadCount() {
#if defined(__linux__)
    int n = 0;
    for ([[maybe_unused]] const auto& e : fs::directory_iterator("/proc/self/task")) ++n;
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

// A render job's spec: `rom` rendered for `durationMs` into `out`, plus `extra` fields (a leading comma each).
std::string renderSpec(const std::string& rom, const std::string& out, int durationMs, const std::string& extra = "") {
    return R"({"rom":")" + rom + R"(","out":")" + out + R"(","durationMs":)" + std::to_string(durationMs) + extra + "}";
}

// Ten minutes of audio: a render that runs for many seconds of wall time, which the cases cancel.
std::string longSpec(const TempDir& dir, const std::string& name) {
    return renderSpec(RP_MGB_ROM_PATH, dir.file(name), 600000);
}

// The fixture tree's path for `relative`, or empty when it is not there (as CI runs).
std::string fixture(const char* relative) {
    const char* root = std::getenv("RETROPLUG_RESOURCES_DIR");
    if (!root) return {};
    const std::string path = std::string(root) + relative;
    return fs::exists(path) ? path : std::string();
}

// What a RenderHost run directly on this thread gives for `spec`: its result, and its console lines, collected rather
// than printed, run as an engine job runs its RenderHost.
struct Direct {
    retroplug::RenderHost::Result result;
    std::vector<std::string>      lines;
};
Direct renderDirectly(const std::string& spec) {
    Direct                d;
    retroplug::RenderHost host;
    d.result = host.run(spec, {}, {}, [&d](bool, const char* text) { d.lines.emplace_back(text); });
    return d;
}

// What the drains reported for each job: its completion, and every progress report before it in order.
struct Seen {
    std::map<std::uint32_t, EngineJobCompletion>            done;
    std::map<std::uint32_t, std::vector<EngineJobProgress>> progress;
};

// Drain `drain` until every job in `ids` has completed (or five minutes have passed, a sanitizer build being slow),
// polling every millisecond so a running render's progress is seen many times.
template <class Drain>
Seen collect(Drain&& drain, const std::vector<std::uint32_t>& ids) {
    Seen       seen;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (std::chrono::steady_clock::now() < deadline) {
        EngineJobsDrain d;
        drain(d);
        for (auto& p : d.progress) seen.progress[p.id].push_back(std::move(p));
        for (auto& c : d.completions) seen.done[c.id] = std::move(c);
        if (std::all_of(ids.begin(), ids.end(), [&](std::uint32_t id) { return seen.done.count(id) != 0; })) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return seen;
}

Seen collect(OfflineJobs& jobs, const std::vector<std::uint32_t>& ids) {
    return collect([&](EngineJobsDrain& d) { jobs.drain(std::chrono::milliseconds(0), nullptr, &d); }, ids);
}

// Wait until job `id` has reported some audio rendered: it is running, past its boot. False after `limit`.
bool waitRendering(OfflineJobs& jobs, std::uint32_t id, Seen& seen,
                   std::chrono::seconds limit = std::chrono::seconds(60)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        EngineJobsDrain d;
        jobs.drain(std::chrono::milliseconds(0), nullptr, &d);
        bool rendering = false;
        for (auto& p : d.progress) {
            if (p.id == id && p.renderedMs > 0) rendering = true;
            seen.progress[p.id].push_back(std::move(p));
        }
        for (auto& c : d.completions) seen.done[c.id] = std::move(c);
        if (rendering) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

// Every line the job wrote, progress reports' and completion's, in order.
std::vector<EngineJobLine> linesOf(const Seen& seen, std::uint32_t id) {
    std::vector<EngineJobLine> lines;
    if (auto it = seen.progress.find(id); it != seen.progress.end())
        for (const auto& p : it->second) lines.insert(lines.end(), p.lines.begin(), p.lines.end());
    const auto& c = seen.done.at(id);
    lines.insert(lines.end(), c.lines.begin(), c.lines.end());
    return lines;
}

// A graph job that runs no document and returns its one input: done as soon as a worker takes it.
OfflineRequest echoRequest() {
    OfflineRequest r;
    r.inputs.emplace_back(OfflineSamples{{0.25f, -0.5f, 0.75f}, 1, 48000.0});
    r.returns = {1};
    r.frames = 0;
    r.rate = 48000.0;
    return r;
}

// Submit and wait for one graph job's completion, which must come while renders run.
rp::host::OfflineCompletion graphJobBesideRenders(OfflineJobs& jobs) {
    const auto s = jobs.submit(echoRequest());
    INFO(s.error);
    REQUIRE(s.id != 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        for (auto& c : jobs.drain(std::chrono::milliseconds(50)))
            if (c.id == s.id) return c;
    }
    FAIL("the graph job did not complete while renders held the pool");
    return {};
}

// Turns on flush-to-zero (and denormals-are-zero) on this thread; false on a target this test cannot set it on.
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

// Whether a subnormal survives a multiply by one on this thread: false under flush-to-zero.
bool keepsSubnormals() {
    volatile float tiny = 1.0e-39f;
    volatile float one = 1.0f;
    return std::fpclassify(tiny * one) != FP_ZERO;
}

// Spin until `flag` is set or ten seconds pass; whether it was set.
bool waitFor(const std::atomic<bool>& flag) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!flag.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return flag.load(std::memory_order_acquire);
}

std::string text(const std::vector<EngineJobLine>& lines) {
    std::string s;
    for (const auto& l : lines) s += (l.stream == EngineJobStream::Err ? "[err] " : "") + l.text + "\n";
    return s;
}

}  // namespace

TEST_CASE("an engine job renders the bytes a direct RenderHost run renders, reporting its progress and its lines",
          "[engine-graph][engine-jobs]") {
    TempDir     dir;
    OfflineJobs jobs;
    // Ten seconds of audio, long enough that the drains see its progress several times on the way.
    const std::string out = dir.file("out.wav");
    const std::string spec = renderSpec(RP_MGB_ROM_PATH, out, 10000);
    const auto        s = jobs.submitEngine({"render", spec});
    INFO(s.error);
    REQUIRE(s.id != 0);
    const Seen seen = collect(jobs, {s.id});
    REQUIRE(seen.done.count(s.id) == 1);
    const auto& c = seen.done.at(s.id);
    const auto  lines = linesOf(seen, s.id);
    INFO(c.message << "\n" << text(lines));
    REQUIRE(c.status == "done");
    CHECK(c.message.empty());
    REQUIRE(c.outputs == std::vector<std::string>{out});
    const auto jobBytes = readBytes(out);
    CHECK(jobBytes.size() == 44 + std::size_t(441000) * 2 * 2);  // ten seconds of 16-bit stereo at 44.1 kHz

    // The same spec run directly, over the same file: the bytes agree, the WAV header included, and so do the console
    // lines, which the job records without the newline the console ends each with, and drops none of.
    const Direct direct = renderDirectly(spec);
    REQUIRE(direct.result.status == "done");
    CHECK(jobBytes == readBytes(out));
    std::vector<std::string> expected;
    for (std::string l : direct.lines) {
        if (!l.empty() && l.back() == '\n') l.pop_back();
        expected.push_back(l);
    }
    std::vector<std::string> got;
    for (const auto& l : lines) got.push_back(l.text);
    CHECK(got == expected);
    CHECK(std::any_of(lines.begin(), lines.end(), [](const EngineJobLine& l) {
        return l.stream == EngineJobStream::Out && l.text.rfind("rendering ", 0) == 0;
    }));
    CHECK(c.droppedLines == 0);

    // Progress: seen while it ran, never going back, and the completion ends at the length rendered.
    const auto& reports = seen.progress.count(s.id) ? seen.progress.at(s.id) : std::vector<EngineJobProgress>{};
    CHECK(reports.size() >= 2);
    double last = 0.0;
    for (const auto& p : reports) {
        CHECK(p.renderedMs >= last);
        CHECK(p.droppedLines == 0);
        last = p.renderedMs;
    }
    CHECK(last > 0.0);
    CHECK(c.renderedMs >= last);
    CHECK(c.renderedMs == 10000.0);
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("a sounding render as an engine job is the direct run's, byte for byte", "[engine-graph][engine-jobs]") {
    const std::string rom = fixture("/roms/lsdj942telemelt_5songs.gbc");
    const std::string sav = fixture("/roms/lsdj942telemelt_5songs.sav");
    if (rom.empty() || sav.empty()) SKIP("the LSDj song fixture is not present (RETROPLUG_RESOURCES_DIR)");
    TempDir           dir;
    OfflineJobs       jobs;
    const std::string extra = R"(,"sav":")" + sav + R"(","songIndex":0)";
    const auto        s = jobs.submitEngine({"render", renderSpec(rom, dir.file("job.wav"), 3000, extra)});
    REQUIRE(s.id != 0);
    const Seen seen = collect(jobs, {s.id});
    REQUIRE(seen.done.count(s.id) == 1);
    INFO(seen.done.at(s.id).message);
    REQUIRE(seen.done.at(s.id).status == "done");
    REQUIRE(renderDirectly(renderSpec(rom, dir.file("direct.wav"), 3000, extra)).result.status == "done");
    const auto bytes = readBytes(dir.file("job.wav"));
    CHECK(bytes == readBytes(dir.file("direct.wav")));
    // And it is not silence: some 16-bit sample past the header is not zero.
    bool sound = false;
    for (std::size_t i = 44; i + 1 < bytes.size() && !sound; i += 2) sound = bytes[i] != 0 || bytes[i + 1] != 0;
    CHECK(sound);
}

TEST_CASE("a render's JS stack fits the smallest thread a pool worker may be, so a recursion past it is an error",
          "[engine-graph][engine-jobs]") {
    // A pool worker is a plain std::thread, whose stack is 512 KiB on macOS and 1 MiB on Windows. A spec nested a
    // hundred thousand deep recurses JSON.parse far past either, so a RenderHost must stop it with a RangeError the
    // worker reports, where QuickJS's default 1 MiB limit let it run off the end of the stack. Run where it is
    // smallest: a 512 KiB thread on POSIX, and Windows' default one there.
    const std::string spec = R"({"rom":"x","deep":)" + std::string(100000, '[') + std::string(100000, ']') + "}";
    retroplug::RenderHost::Result result;
    const auto body = [&] {
        retroplug::RenderHost host;
        result = host.run(spec, {}, {}, [](bool, const char*) {});
    };
#if defined(_WIN32)
    std::thread(body).join();
#else
    pthread_attr_t attr;
    REQUIRE(pthread_attr_init(&attr) == 0);
    REQUIRE(pthread_attr_setstacksize(&attr, 512 * 1024) == 0);
    pthread_t thread;
    const auto trampoline = [](void* fn) -> void* {
        (*static_cast<decltype(body)*>(fn))();
        return nullptr;
    };
    REQUIRE(pthread_create(&thread, &attr, trampoline, const_cast<void*>(static_cast<const void*>(&body))) == 0);
    pthread_attr_destroy(&attr);
    pthread_join(thread, nullptr);
#endif
    CHECK(result.status == "error");
    CHECK(result.message == "Maximum call stack size exceeded");
}

TEST_CASE("an auto-detected render lowers its count once, at its end, to the length it wrote",
          "[engine-graph][engine-jobs-hff]") {
    // Tagged apart from [engine-jobs]: a song to its HFF is three minutes of audio, about twelve seconds of wall time,
    // which a sanitizer build multiplies past what its legs budget. The drains report the render's count chunk by
    // chunk, the chunks it holds back past the stop included; the completion's is the length it wrote, which the
    // count dropped to.
    const std::string rom = fixture("/roms/lsdj942telemelt_5songs.gbc");
    const std::string sav = fixture("/roms/lsdj942telemelt_5songs.sav");
    if (rom.empty() || sav.empty()) SKIP("the LSDj song fixture is not present (RETROPLUG_RESOURCES_DIR)");
    TempDir           dir;
    OfflineJobs       jobs;
    const std::string spec = R"({"rom":")" + rom + R"(","sav":")" + sav + R"(","out":")" + dir.file("hff.wav") +
                             R"(","songIndex":3})";
    const auto s = jobs.submitEngine({"render", spec});
    REQUIRE(s.id != 0);
    const Seen seen = collect(jobs, {s.id});
    REQUIRE(seen.done.count(s.id) == 1);
    const auto& c = seen.done.at(s.id);
    INFO(c.message);
    REQUIRE(c.status == "done");
    CHECK(c.renderedMs == 191700.0);  // MACHETE to its HFF
    // Rising while it runs, then down once to the length (the render reports the length before it ends, so the drop
    // can come in a progress report as well as in the completion), and never again.
    double most = 0.0, last = 0.0;
    int    drops = 0;
    for (const auto& p : seen.progress.at(s.id)) {
        if (p.renderedMs < last) ++drops;
        CHECK((p.renderedMs >= last || p.renderedMs == c.renderedMs));
        last = p.renderedMs;
        most = std::max(most, last);
    }
    if (c.renderedMs < last) ++drops;
    CHECK(drops <= 1);
    // Whether a drain caught a count above the length depends on when the polls fell (the window is the few chunks
    // held past the stop), so only that it never fell below is held here; the mock tier pins the drop exactly.
    INFO("most counted " << most << " ms, length " << c.renderedMs << " ms");
    CHECK(most >= c.renderedMs);
    CHECK(readBytes(dir.file("hff.wav")).size() == 44 + std::size_t(191700) * 441 / 10 * 4);
}

TEST_CASE("a cancel stops an engine job, queued at once and running at its next chunk", "[engine-graph][engine-jobs]") {
    TempDir     dir;
    OfflineJobs jobs({}, 2);  // a budget of one render: the second queues
    REQUIRE(jobs.engineBudget() == 1);
    const auto running = jobs.submitEngine({"render", longSpec(dir, "a.wav")}).id;
    const auto queued = jobs.submitEngine({"render", longSpec(dir, "b.wav")}).id;
    REQUIRE(running != 0);
    REQUIRE(queued != 0);
    // The queued one completes at the cancel, unrun.
    CHECK(jobs.cancel(queued));
    EngineJobsDrain first;
    jobs.drain(std::chrono::milliseconds(0), nullptr, &first);
    REQUIRE(first.completions.size() == 1);
    CHECK(first.completions[0].id == queued);
    CHECK(first.completions[0].status == "cancelled");
    CHECK(first.completions[0].renderedMs == 0.0);
    CHECK(first.completions[0].outputs.empty());
    CHECK_FALSE(jobs.cancel(queued));
    // The running one, once it renders, stops within a chunk of the cancel: ten minutes of audio cut short.
    Seen seen;
    REQUIRE(waitRendering(jobs, running, seen));
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(jobs.cancel(running));
    seen = collect(jobs, {running});
    REQUIRE(seen.done.count(running) == 1);
    const auto& c = seen.done.at(running);
    CHECK(c.status == "cancelled");
    CHECK(c.outputs.empty());
    CHECK(c.renderedMs > 0.0);
    CHECK(c.renderedMs < 600000.0);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
    CHECK(jobs.outstanding() == 0);
}

TEST_CASE("an engine destroyed with renders in flight cancels them and joins every worker",
          "[engine-graph][engine-jobs]") {
    TempDir    dir;
    const int  before = threadCountBaseline();
    const auto t0 = std::chrono::steady_clock::now();
    {
        Engine e(48000.0);
        // Running renders, and queued ones past the budget.
        for (unsigned i = 0; i < OfflineJobs::concurrency() + 1; ++i)
            REQUIRE(e.submitEngineJob({"render", longSpec(dir, "r" + std::to_string(i) + ".wav")}).id != 0);
        // Until the first is rendering, so the teardown meets renders mid-run rather than still booting.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        for (bool rendering = false; !rendering && std::chrono::steady_clock::now() < deadline;) {
            EngineJobsDrain d;
            e.drainOfflineJobs(std::chrono::milliseconds(0), nullptr, &d);
            for (const auto& p : d.progress) rendering = rendering || p.renderedMs > 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (before >= 0) CHECK(threadCount() > before);
    }
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60));
    // Every worker gone (the pool's destructor joins them), every render's runtime with it.
    if (before >= 0) CHECK(threadCountSettled(before) <= before);
}

TEST_CASE("renders hold all but one of the pool's workers, so a graph job still runs beside them",
          "[engine-graph][engine-jobs]") {
    TempDir dir;
    SECTION("a pool of three: two renders run, a third queues, and a graph job takes the third worker") {
        OfflineJobs jobs({}, 3);
        CHECK(jobs.engineBudget() == 2);
        std::vector<std::uint32_t> renders;
        for (int i = 0; i < 3; ++i)
            renders.push_back(jobs.submitEngine({"render", longSpec(dir, "r" + std::to_string(i) + ".wav")}).id);
        for (const auto id : renders) REQUIRE(id != 0);
        Seen seen;
        REQUIRE(waitRendering(jobs, renders[0], seen));
        REQUIRE(waitRendering(jobs, renders[1], seen));
        const auto graph = graphJobBesideRenders(jobs);
        INFO(graph.error);
        CHECK(graph.ok);
        REQUIRE(graph.buffers.size() == 1);
        CHECK(graph.buffers[0].samples == std::vector<float>{0.25f, -0.5f, 0.75f});
        CHECK(jobs.workers() == 3);
        // The renders are still where they were: two running (each drain reports their progress), one queued.
        EngineJobsDrain d;
        std::size_t     inFlight = 0;
        jobs.drain(std::chrono::milliseconds(0), &inFlight, &d);
        CHECK(d.completions.empty());
        CHECK(d.progress.size() == 2);
        CHECK(inFlight == 3);
        // Graph jobs past the free worker still run, one after another, while the renders hold theirs.
        for (int i = 0; i < 3; ++i) CHECK(graphJobBesideRenders(jobs).ok);
        CHECK(jobs.workers() == 3);
        for (const auto id : renders) CHECK(jobs.cancel(id));
        seen = collect(jobs, renders);
        for (const auto id : renders) CHECK(seen.done.at(id).status == "cancelled");
    }
    SECTION("a pool of one: the render takes the worker, and a graph job starts the pool's second thread") {
        OfflineJobs jobs({}, 1);
        CHECK(jobs.engineBudget() == 1);
        CHECK(jobs.maxWorkers() == 2);
        const auto render = jobs.submitEngine({"render", longSpec(dir, "one.wav")}).id;
        const auto queued = jobs.submitEngine({"render", longSpec(dir, "two.wav")}).id;
        REQUIRE(render != 0);
        REQUIRE(queued != 0);
        Seen seen;
        REQUIRE(waitRendering(jobs, render, seen));
        CHECK(jobs.workers() == 1);  // the queued render started no thread: the budget holds it back
        CHECK(graphJobBesideRenders(jobs).ok);
        CHECK(jobs.workers() == 2);
        CHECK(jobs.cancel(render));
        CHECK(jobs.cancel(queued));
        seen = collect(jobs, {render, queued});
        CHECK(seen.done.at(render).status == "cancelled");
        CHECK(seen.done.at(queued).status == "cancelled");
        CHECK(jobs.workers() == 2);
    }
}

TEST_CASE("renders run back to back on one reused worker and in parallel on several, each giving a direct run's bytes",
          "[engine-graph][engine-jobs]") {
    TempDir dir;
    // A Game Boy, a NES and an SMS core, two Super Game Boys and an LSDj song when the fixtures are here: each core's
    // process-wide and thread-local state is met by the renders after it on the same thread, and beside it on others.
    // The Super Game Boy is an mGB with the model set and Start left unpressed, which plays the boot jingle: the one
    // sound the repo's own ROMs make with no song or MIDI, so the comparison holds sound on CI too. The parallel round
    // runs first, so with the tag run alone its two jingles are the process's first, side by side: SameBoy's SGB sine
    // table is built at load, where upstream fills it on the first jingle, which two at once would race on (the TSan
    // leg's guard).
    const std::string sgb = R"(,"start":false,"roles":[{"kind":"sameboy","config":{"model":"sgb"}}])";
    std::vector<std::pair<std::string, std::string>> roms = {{"gb", RP_MGB_ROM_PATH},
                                                             {"nes", RP_BLIPTOASTER_ROM_PATH},
                                                             {"sms", RP_SMS_ROM_PATH},
                                                             {"sgb", RP_MGB_ROM_PATH},
                                                             {"sgb-2", RP_MGB_ROM_PATH}};
    std::vector<std::string> extras = {"", "", "", sgb, sgb};
    if (const auto rom = fixture("/roms/lsdj942telemelt_5songs.gbc"); !rom.empty()) {
        if (const auto sav = fixture("/roms/lsdj942telemelt_5songs.sav"); !sav.empty()) {
            roms.push_back({"lsdj", rom});
            extras.push_back(R"(,"sav":")" + sav + R"(","songIndex":0)");
        }
    }
    const auto runAll = [&](OfflineJobs& jobs, const std::string& prefix) {
        std::vector<std::uint32_t> ids;
        for (std::size_t i = 0; i < roms.size(); ++i) {
            const auto out = dir.file(prefix + roms[i].first + ".wav");
            ids.push_back(jobs.submitEngine({"render", renderSpec(roms[i].second, out, 2000, extras[i])}).id);
            REQUIRE(ids.back() != 0);
        }
        const Seen seen = collect(jobs, ids);
        for (std::size_t i = 0; i < roms.size(); ++i) {
            INFO(roms[i].first);
            REQUIRE(seen.done.count(ids[i]) == 1);
            INFO(seen.done.at(ids[i]).message);
            CHECK(seen.done.at(ids[i]).status == "done");
        }
    };
    {
        OfflineJobs jobs({}, static_cast<unsigned>(roms.size() + 1));  // a budget of every render at once
        runAll(jobs, "parallel-");
        CHECK(jobs.workers() == roms.size());
    }
    {
        OfflineJobs jobs({}, 1);
        runAll(jobs, "serial-");
        runAll(jobs, "serial-again-");  // the same worker, its second round
        CHECK(jobs.workers() == 1);     // every render ran on the one thread
    }
    for (std::size_t i = 0; i < roms.size(); ++i) {
        INFO(roms[i].first);
        const auto ref = dir.file("ref-" + roms[i].first + ".wav");
        REQUIRE(renderDirectly(renderSpec(roms[i].second, ref, 2000, extras[i])).result.status == "done");
        const auto reference = readBytes(ref);
        for (const char* prefix : {"parallel-", "serial-", "serial-again-"})
            CHECK(readBytes(dir.file(prefix + roms[i].first + ".wav")) == reference);
        if (extras[i] == sgb) {  // the jingle sounds: some 16-bit sample past the header is not zero
            bool sound = false;
            for (std::size_t b = 44; b + 1 < reference.size() && !sound; b += 2)
                sound = reference[b] != 0 || reference[b + 1] != 0;
            CHECK(sound);
        }
    }
}

TEST_CASE("a cancel that lands as a render starts ends it within a chunk of the cancel",
          "[engine-graph][engine-jobs]") {
    TempDir     dir;
    OfflineJobs jobs;
    const auto  id = jobs.submitEngine({"render", longSpec(dir, "boot.wav")}).id;
    REQUIRE(id != 0);
    // Cancelled as soon as a drain shows it running, usually before it has reported any audio, while it boots its
    // system: the render's check before each chunk, its first included, ends it there. How far it got before the
    // cancel landed is the scheduler's, so the bound is taken from a drain made right after the cancel: the count
    // only rises, so what that drain sees is at least where the render stood at the cancel, and the render stops
    // before the chunk after the one it was in.
    double     seenBefore = -1.0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (seenBefore < 0.0 && std::chrono::steady_clock::now() < deadline) {
        EngineJobsDrain d;
        jobs.drain(std::chrono::milliseconds(0), nullptr, &d);
        for (const auto& p : d.progress)
            if (p.id == id) seenBefore = p.renderedMs;
    }
    REQUIRE(seenBefore >= 0.0);
    CHECK(jobs.cancel(id));
    EngineJobsDrain after;
    jobs.drain(std::chrono::milliseconds(0), nullptr, &after);
    double atCancel = -1.0;
    for (const auto& p : after.progress)
        if (p.id == id) atCancel = p.renderedMs;
    for (const auto& c : after.completions)
        if (c.id == id) atCancel = c.renderedMs;
    REQUIRE(atCancel >= 0.0);
    Seen seen = collect(jobs, {id});
    for (auto& c : after.completions) seen.done[c.id] = c;
    REQUIRE(seen.done.count(id) == 1);
    const auto& c = seen.done.at(id);
    INFO("rendered when first seen running: " << seenBefore << " ms, just after the cancel: " << atCancel << " ms");
    CHECK(c.status == "cancelled");
    CHECK(c.outputs.empty());
    CHECK(c.renderedMs <= atCancel + 100.0);  // at most the one chunk under way when the cancel landed
    CHECK(c.renderedMs < 600000.0);
}

TEST_CASE("a cancel taken after a job's body finished still ends it cancelled, keeping the files it wrote",
          "[engine-graph][engine-jobs]") {
    // A body of the test's own that has finished its work and waits, so the cancel lands after the end of the run
    // and before the worker publishes it: cancel() answered true, so the outcome is the cancel, and the file a render
    // would have finished writing is still named.
    std::atomic<bool>   finished{false};
    std::atomic<bool>   release{false};
    const EngineJobBody body = [&](const EngineJobRequest&, const EngineJobHooks& hooks) {
        hooks.rendered(1000.0);
        finished.store(true, std::memory_order_release);
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        return EngineJobOutcome{"done", "", {"/written.wav"}};
    };
    OfflineJobs jobs({}, 2, body);
    const auto  id = jobs.submitEngine({"render", "{}"}).id;
    REQUIRE(id != 0);
    REQUIRE(waitFor(finished));
    CHECK(jobs.cancel(id));
    release.store(true, std::memory_order_release);
    const Seen seen = collect(jobs, {id});
    REQUIRE(seen.done.count(id) == 1);
    CHECK(seen.done.at(id).status == "cancelled");
    CHECK(seen.done.at(id).message.empty());
    CHECK(seen.done.at(id).outputs == std::vector<std::string>{"/written.wav"});
    CHECK(seen.done.at(id).renderedMs == 1000.0);
}

TEST_CASE("renders held back by the budget start in submission order", "[engine-graph][engine-jobs]") {
    std::mutex                 mutex;
    std::vector<std::string>   started;
    std::atomic<bool>          release{false};
    const EngineJobBody body = [&](const EngineJobRequest& r, const EngineJobHooks&) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            started.push_back(r.spec);
        }
        while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
        return EngineJobOutcome{"done", "", {}};
    };
    OfflineJobs jobs({}, 2, body);  // a budget of one
    std::vector<std::uint32_t> ids;
    for (const char* spec : {"a", "b", "c", "d"}) ids.push_back(jobs.submitEngine({"render", spec}).id);
    // The first holds the budget until every one is queued behind it.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (bool first = false; !first && std::chrono::steady_clock::now() < deadline;) {
        std::lock_guard<std::mutex> lock(mutex);
        first = !started.empty();
    }
    release.store(true, std::memory_order_release);
    const Seen seen = collect(jobs, ids);
    for (const auto id : ids) CHECK(seen.done.at(id).status == "done");
    std::lock_guard<std::mutex> lock(mutex);
    CHECK(started == std::vector<std::string>{"a", "b", "c", "d"});
    CHECK(jobs.workers() == 1);  // the held renders started no thread of their own
}

TEST_CASE("an engine job runs in the default floating-point mode whatever its submitter's",
          "[engine-graph][engine-jobs]") {
    // As a graph job: a worker inherits the mode of the thread whose submission started it under glibc, and an audio
    // host commonly flushes subnormals. The body reads the mode it runs in; the probe skips where a new thread does
    // not inherit its creator's mode, since the case would then pass vacuously.
    std::atomic<bool>   kept{false};
    const EngineJobBody body = [&](const EngineJobRequest&, const EngineJobHooks&) {
        kept.store(keepsSubnormals(), std::memory_order_release);
        return EngineJobOutcome{"done", "", {}};
    };
    OfflineJobs jobs({}, 2, body);
    bool        flushable = false;
    bool        inherited = false;
    Seen        seen;
    std::uint32_t id = 0;
    std::thread submitter([&] {
        if (!setFlushToZero()) return;
        flushable = true;
        std::thread probe([&] { inherited = !keepsSubnormals(); });
        probe.join();
        if (!inherited) return;
        id = jobs.submitEngine({"render", "{}"}).id;
        if (id != 0) seen = collect(jobs, {id});
    });
    submitter.join();
    if (!flushable) SKIP("this target has no flush-to-zero mode this test knows how to set");
    if (!inherited) SKIP("a new thread does not inherit the submitter's flush-to-zero mode on this platform");
    REQUIRE(id != 0);
    REQUIRE(seen.done.count(id) == 1);
    CHECK(seen.done.at(id).status == "done");
    CHECK(kept.load(std::memory_order_acquire));
}

TEST_CASE("an engine job's console record keeps its bounds between takes, and counts what it drops",
          "[engine-graph][engine-jobs]") {
    EngineJobRecord record;
    EngineJobProgress p;
    record.rendered(250.0);
    record.write(EngineJobStream::Out, "rendering x\n");
    record.write(EngineJobStream::Err, "a warning\n");
    record.write(EngineJobStream::Out, "no newline");
    record.take(p);
    CHECK(p.renderedMs == 250.0);
    REQUIRE(p.lines.size() == 3);
    CHECK(p.lines[0].text == "rendering x");
    CHECK(p.lines[0].stream == EngineJobStream::Out);
    CHECK(p.lines[1].text == "a warning");
    CHECK(p.lines[1].stream == EngineJobStream::Err);
    CHECK(p.lines[2].text == "no newline");
    CHECK(p.droppedLines == 0);
    // A take empties it, the rendered length aside.
    record.take(p);
    CHECK(p.lines.empty());
    CHECK(p.renderedMs == 250.0);
    // Past the line bound, lines are dropped and counted, and the count goes with the next take.
    for (std::size_t i = 0; i < EngineJobRecord::kMaxLines + 10; ++i) record.write(EngineJobStream::Out, "x\n");
    record.take(p);
    CHECK(p.lines.size() == EngineJobRecord::kMaxLines);
    CHECK(p.droppedLines == 10);
    record.take(p);
    CHECK(p.droppedLines == 0);
    // Past the byte bound the same, and a line longer than the bound alone is cut to it at a UTF-8 boundary.
    const std::string half(EngineJobRecord::kMaxBytes / 2, 'a');
    record.write(EngineJobStream::Out, half);
    record.write(EngineJobStream::Out, half);
    record.write(EngineJobStream::Out, "one byte too many");
    record.take(p);
    CHECK(p.lines.size() == 2);
    CHECK(p.droppedLines == 1);
    // A three-byte character straddling the bound is left out whole rather than split.
    std::string wide(EngineJobRecord::kMaxBytes - 1, 'b');
    wide += "\xE2\x82\xAC";  // U+20AC, its first byte the bound's last
    record.write(EngineJobStream::Err, wide);
    record.take(p);
    REQUIRE(p.lines.size() == 1);
    CHECK(p.lines[0].text == std::string(EngineJobRecord::kMaxBytes - 1, 'b'));
}

TEST_CASE("the engine job RPCs carry a render's progress, lines and completion, and refuse an unknown kind",
          "[engine-graph][engine-jobs]") {
    TempDir          dir;
    Engine           e(48000.0);
    QueuedInvoker    invoker(e);
    EngineRpcService rpc(e, invoker);
    const auto       refused = rpc.submitEngineJob({"graph", "{}"});
    CHECK_FALSE(refused.id);
    REQUIRE(refused.error);
    CHECK(*refused.error == R"(unknown engine job kind "graph": the one kind is "render")");
    CHECK(e.submitEngineJob({"", "{}"}).id == 0);
    CHECK(OfflineJobs().submitEngine({"Render", "{}"}).id == 0);
    // A spec the worker cannot read is the job's to report, not a refusal.
    const auto bad = rpc.submitEngineJob({"render", "{}"});
    REQUIRE(bad.id);
    const auto taken = rpc.submitEngineJob({"render", renderSpec(RP_MGB_ROM_PATH, dir.file("rpc.wav"), 3000)});
    REQUIRE(taken.id);
    std::map<std::uint32_t, RpcEngineJobCompletion> done;
    std::vector<RpcEngineJobLine>                   lines;
    double                                          last = 0.0;
    bool                                            monotonic = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(300);
    while (done.size() < 2 && std::chrono::steady_clock::now() < deadline) {
        RpcOfflineDrain d = rpc.drainOfflineJobs(100);
        CHECK(d.completions.empty());  // no graph job was submitted
        for (auto& p : d.engineProgress) {
            if (p.id != *taken.id) continue;
            monotonic = monotonic && p.renderedMs >= last;
            last = p.renderedMs;
            lines.insert(lines.end(), p.lines.begin(), p.lines.end());
        }
        for (auto& c : d.engineCompletions) done[c.id] = std::move(c);
    }
    REQUIRE(done.size() == 2);
    CHECK(done.at(*bad.id).status == "error");
    CHECK(done.at(*bad.id).message == "render worker: job spec missing 'rom'");
    const auto& c = done.at(*taken.id);
    INFO(c.message);
    CHECK(c.status == "done");
    CHECK(c.outputs == std::vector<std::string>{dir.file("rpc.wav")});
    CHECK(c.renderedMs == 3000.0);
    CHECK(monotonic);
    lines.insert(lines.end(), c.lines.begin(), c.lines.end());
    REQUIRE_FALSE(lines.empty());
    CHECK(std::all_of(lines.begin(), lines.end(),
                      [](const RpcEngineJobLine& l) { return l.stream == "stdout" || l.stream == "stderr"; }));
    CHECK(std::any_of(lines.begin(), lines.end(), [&](const RpcEngineJobLine& l) {
        return l.stream == "stdout" && l.text == dir.file("rpc.wav");
    }));
    CHECK(rpc.drainOfflineJobs(0).inFlight == 0);
    CHECK(e.offlineJobsOutstanding() == 0);
}

// ---- continuous blocks: the engine side of an engine job's render (EngineRpcService::setContinuousBlocks) ----------

namespace {

constexpr double kBlocksRate = 48000.0;  // 48 frames a millisecond, so every length below is a whole frame count

// An mGB placed alone on a host, booted for 2048 ms (96 whole blocks, so nothing is carried), then sent a note to
// sound: mGB is silent until MIDI plays it, and ignores MIDI for its first second and a half. The host is stereo, or,
// for `perChannel`, its four stems on a pair each (8 lanes), as buildGraph places a perChannel render.
struct BlocksRig {
    Engine                                 engine{kBlocksRate};
    QueuedInvoker                          invoker{engine};
    EngineRpcService                       svc{engine, invoker};
    rp::host::test::Roms                   roms;
    std::vector<rp::host::test::DocSystem> systems{{1, rp::host::test::kGbType}};

    explicit BlocksRig(bool continuous, bool perChannel = false) {
        svc.setContinuousBlocks(continuous);
        rp::host::test::withRoms(engine, systems, roms);
        REQUIRE(place(perChannel));
        const std::size_t boot =
            perChannel ? svc.renderAudioPerChannel(1, 2048.0).at(0).size() : svc.renderAudio(2048.0).size();
        REQUIRE(boot == 98304 * 2 * sizeof(float));
        REQUIRE(svc.carriedFrames() == 0);
        REQUIRE(svc.stageMidiIn({0x90, 60, 100}, 0));
    }

    // Plan the stereo or the per-channel document, as audioDriver's withShape swaps them around a render call.
    bool place(bool perChannel) {
        using namespace rp::host::test;
        namespace d = docs_detail;
        // The host's MIDI reaches mGB as buildGraph wires it: through its protocol node, onto the link port.
        std::vector<std::string> nodes{docNode("midi-in", "rp.host.midi-in"), docNode("sys-1/mgb", "rp.mgb.midi")};
        std::vector<std::string> wires{docWire("midi-in.midi", "sys-1/mgb.midi"),
                                       docWire("sys-1/mgb.serialIn", "sys-1.serialIn")};
        if (!perChannel) return engine.setGraph(graphRequest(stereoDocument(systems, DocExtras{nodes, wires}), {1})).ok;
        nodes.push_back(d::sys(systems[0]));
        for (std::uint32_t k = 0; k < 4; ++k)
            wires.push_back(d::wire("sys-1.ch" + std::to_string(k + 1), d::out(2 * k, 2)));
        nodes.push_back(d::node("out", "rp.host.audio-out", d::buses({2, 2, 2, 2})));
        return engine.setGraph(graphRequest(d::document(nodes, wires), {1})).ok;
    }
};

std::vector<std::byte> concat(std::vector<std::byte> a, const rfl::Bytestring& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

bool sounds(const std::vector<std::byte>& pcm) {
    for (std::size_t i = 0; i + sizeof(float) <= pcm.size(); i += sizeof(float)) {
        float v;
        std::memcpy(&v, pcm.data() + i, sizeof v);
        if (v != 0.0f) return true;
    }
    return false;
}

} // namespace

namespace {

// A second of the note pulled in calls of the lengths `ms`, a second note (an octave up) staged after call `offAfter`
// (none when that is past the last call).
struct Split {
    std::vector<double> ms;
    std::size_t         offAfter;
};

std::vector<std::byte> pullSplit(bool continuous, const Split& split) {
    BlocksRig              rig(continuous);
    std::vector<std::byte> out;
    for (std::size_t k = 0; k < split.ms.size(); ++k) {
        const rfl::Bytestring pcm = rig.svc.renderAudio(split.ms[k]);
        CHECK(pcm.size() == static_cast<std::size_t>(split.ms[k] * 48) * 2 * sizeof(float));
        out = concat(std::move(out), pcm);
        if (k == split.offAfter) REQUIRE(rig.svc.stageMidiIn({0x90, 72, 100}, 0));
    }
    CHECK(out.size() == 48000 * 2 * sizeof(float));
    return out;
}

} // namespace

TEST_CASE("in continuous blocks a render's bytes do not depend on the lengths its calls ask for",
          "[engine-graph][engine-jobs]") {
    // The second note is staged after a call ending at 470, 480 or 490 ms: all inside the 23rd block after the boot
    // (469.33 to 490.67 ms), so in continuous blocks it lands where that block ends whichever it is, and a call the
    // carry satisfies (the 10 ms after 470) renders no block for it to land in. In exact blocks it lands where the call
    // ended, so there the splits differ: the control that shows the comparison can fail.
    const std::vector<Split> splits{{{480.0, 520.0}, 0},
                                    {{100.0, 370.0, 530.0}, 1},
                                    {{37.0, 453.0, 250.0, 260.0}, 1},
                                    {{470.0, 10.0, 13.0, 507.0}, 0}};
    std::vector<std::vector<std::byte>> continuous, exact;
    for (const Split& split : splits) {
        continuous.push_back(pullSplit(true, split));
        exact.push_back(pullSplit(false, split));
    }
    CHECK(sounds(continuous[0]));
    for (std::size_t k = 1; k < splits.size(); ++k) CHECK(continuous[k] == continuous[0]);
    // And the second note is in them: a continuous run that never stages it differs, so the splits agree because each
    // landed it at the one block end, not because every one of them lost it.
    CHECK(pullSplit(true, {{480.0, 520.0}, 2}) != continuous[0]);
    CHECK(exact[1] != exact[0]);  // the second note at 470 ms against 480
    CHECK(exact[2] != exact[0]);  // at 490 ms
}

TEST_CASE("in continuous blocks a carried tail goes only to a call of its shape, and a renderBlocks or a rate change "
          "drops it",
          "[engine-graph][engine-jobs]") {
    // 50 ms is 2400 frames: three blocks run and 672 frames (14 ms) are carried. A per-system call (the same two lanes
    // on this host, another shape) drops them and starts at the next block; the control drains them first.
    BlocksRig dropped(true);
    CHECK(dropped.svc.renderAudio(50.0).size() == 2400 * 2 * sizeof(float));
    CHECK(dropped.svc.carriedFrames() == 672);
    const auto viaDrop = dropped.svc.renderAudioPerSystem(100.0);
    CHECK(dropped.svc.carriedFrames() == 5120 - 4800);

    BlocksRig drained(true);
    CHECK(drained.svc.renderAudio(50.0).size() == 2400 * 2 * sizeof(float));
    CHECK(drained.svc.renderAudio(14.0).size() == 672 * 2 * sizeof(float));
    CHECK(drained.svc.carriedFrames() == 0);
    const auto viaDrain = drained.svc.renderAudioPerSystem(100.0);
    REQUIRE(viaDrop.size() == 1);
    REQUIRE(viaDrain.size() == 1);
    CHECK(viaDrop[0].size() == 4800 * 2 * sizeof(float));
    CHECK(sounds(std::vector<std::byte>(viaDrop[0].begin(), viaDrop[0].end())));
    CHECK(viaDrop[0] == viaDrain[0]);

    // A renderBlocks run's blocks come after the carried frames, which it drops.
    CHECK(dropped.svc.carriedFrames() == 320);
    RpcRenderBlocksRequest blocks{2, {RpcRenderBlock{256, {}, {}, std::nullopt, std::nullopt, std::nullopt}}};
    CHECK(dropped.svc.renderBlocks(blocks).error.empty());
    CHECK(dropped.svc.carriedFrames() == 0);

    // A rate change (which needs an engine with no systems) drops the frames carried at the old rate: 10 ms at 44.1 kHz
    // then runs a new block and carries 583, where handing out the 544 frames 10 ms carried at 48 kHz would carry 103.
    Engine           engine(kBlocksRate);
    QueuedInvoker    invoker(engine);
    EngineRpcService svc(engine, invoker);
    svc.setContinuousBlocks(true);
    REQUIRE(engine.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument({}), {})).ok);
    CHECK(svc.renderAudio(10.0).size() == 480 * 2 * sizeof(float));
    CHECK(svc.carriedFrames() == 544);
    REQUIRE(svc.setSampleRate(44100.0));
    CHECK(svc.renderAudio(10.0).size() == 441 * 2 * sizeof(float));
    CHECK(svc.carriedFrames() == 583);
}

TEST_CASE("in continuous blocks a per-channel render carries its tail to its next call, and a mix call drops it",
          "[engine-graph][engine-jobs]") {
    // The shape every split render streams in: mGB's four stems, eight lanes. 50 then 100 ms (the 672 frames 50 ms
    // carries going to the second call) against one call of 150 ms: the same stems, and both carry 992 frames.
    BlocksRig split(true, true);
    auto      first = split.svc.renderAudioPerChannel(1, 50.0);
    CHECK(split.svc.carriedFrames() == 672);
    const auto second = split.svc.renderAudioPerChannel(1, 100.0);
    CHECK(split.svc.carriedFrames() == 992);
    BlocksRig whole(true, true);
    const auto once = whole.svc.renderAudioPerChannel(1, 150.0);
    CHECK(whole.svc.carriedFrames() == 992);
    REQUIRE(first.size() == 4);
    REQUIRE(second.size() == 4);
    REQUIRE(once.size() == 4);
    for (std::size_t k = 0; k < 4; ++k) {
        const auto joined = concat(std::vector<std::byte>(first[k].begin(), first[k].end()), second[k]);
        CHECK(joined.size() == 7200 * 2 * sizeof(float));
        CHECK(joined == std::vector<std::byte>(once[k].begin(), once[k].end()));
    }
    CHECK(sounds(std::vector<std::byte>(once[0].begin(), once[0].end())));  // the note, on the first pulse stem

    // A mix call between (the stereo document planned for it, as withShape does) drops the stems' 992 frames and
    // carries its own: 10 ms takes 480 frames of a new block and carries 544, where handing out the stems' frames would
    // carry 512. Back on the stems, the mix's 544 are dropped in turn: another new block, 544 carried, not 64.
    REQUIRE(split.place(false));
    CHECK(split.svc.renderAudio(10.0).size() == 480 * 2 * sizeof(float));
    CHECK(split.svc.carriedFrames() == 544);
    REQUIRE(split.place(true));
    CHECK(split.svc.renderAudioPerChannel(1, 10.0).at(0).size() == 480 * 2 * sizeof(float));
    CHECK(split.svc.carriedFrames() == 544);
}

TEST_CASE("exact blocks carry nothing: each call renders its own length", "[engine-graph][engine-jobs]") {
    BlocksRig rig(false);
    CHECK(rig.svc.renderAudio(50.0).size() == 2400 * 2 * sizeof(float));
    CHECK(rig.svc.carriedFrames() == 0);
    CHECK(rig.svc.renderAudioPerSystem(10.0)[0].size() == 480 * 2 * sizeof(float));
    CHECK(rig.svc.carriedFrames() == 0);
}

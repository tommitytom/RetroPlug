#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <string>
#include <thread>
#include <vector>

#include "graph/NodeRegistry.hpp"
#include "graph/rp_node.h"
#include "host/engine/OfflineTypes.hpp"

// Offline jobs (docs/modular-graph-phase-d2-design.md section 3): a graph document run to completion on a host of its
// own, apart from the live engine, with the buffers it recorded handed back. A job is submitted with its inputs (audio
// the submission carries, or a file native decodes and windows), the empty buffers it records into, the buffers to
// return and the frames to run; it is taken or refused at once, runs on a worker thread, and its completion is
// collected later from a bounded queue the control plane polls, as a capability job's is (CapabilityJobs.hpp).
//
// Threads and ownership. The pool (an OfflineJobs) is the engine's, made on the control thread at the first submission
// and joined as the engine goes. Its node registry is its own, filled in its constructor before any worker exists and
// never changed after, so the workers share it read-only (NodeRegistry's rule: concurrent find() is safe once filling
// is over); it holds the built-in C++ packages a job's document may name (rp.core, rp.dsp, rp.dsp.resample) and
// nothing of the engine's, so a script pack loading, reloading or freed, which swaps the ENGINE's registry entries on
// the control thread, can never race a job's planning: no job can see that registry. A script node, a system node or
// a host I/O node in a job's document is refused by the planner as an unknown type. The packages themselves are static
// objects that outlive every engine.
//
// Each job runs on one worker thread from start to end, which is the job's control thread and its block thread at
// once: the worker builds the job's ResourceTable and its inputs and outputs in it, builds a GraphHost over the pool's
// registry and that table (quiescent: no audio thread, so setGraph flushes inline and run() is the block driver, as
// the graph tests drive it), plans the document, runs `frames` frames in blocks of kBlockFrames (the last one
// partial), checking for a cancel between blocks, tears the host down, copies the returned buffers out and frees the
// table, all on that thread. So every node a job builds, a resampler and its r8brain objects among them, is destroyed
// inside its host's life and before the pool's destructor returns, which an engine reaches before the program's
// statics go (docs/modular-graph-phase-d1-design.md section 8). A job never touches the engine's plan, its command
// ring, its resource table or its audio thread: the only state a worker shares is this pool's queue and completions,
// under its mutex.
//
// The workers are started as jobs need them, up to maxWorkers() (the pool's size, which is the machine's hardware
// threads, at least one, at most kMaxWorkers, unless a test gives another; see engine jobs below for a size of 1), so a
// host that never submits has no thread at all; jobs past that wait, in the order "Sharing the pool" below gives. A
// worker never retires on its own: once started it stays parked between jobs until the pool goes, so an engine that ran
// one kit compile keeps up to kMaxWorkers idle threads for the rest of its life (each a parked wait, costing no CPU).
// At most kMaxJobs are outstanding at once, queued, running, or finished and not yet drained, so every job taken has
// room for its completion and none is ever dropped.
//
// Resources. A job's table is fresh, so its handles are known before it runs: input i is res:(i + 1) and output j is
// res:(inputs + j + 1), in the order the submission lists them, which is how a document names them. An input of no
// frames (a window that comes out empty) is an empty buffer: createAudioBuffer refuses zero frames, so its handle is
// taken by a placeholder freed at once, names no resource (a document binding it is refused as unknown-resource), and
// returns as a buffer of no frames. An OUTPUT of no frames is refused at submission, so a caller whose chain meets an
// empty buffer (a window that came out empty, a resample whose length comes out 0) does not submit the jobs after it:
// it knows what they would make. Every buffer is planar inside the job, interleaved in a completion.
//
// Engine jobs (docs/modular-graph-phase-d3-design.md section 3). The pool runs a second kind of job: a script bundle
// run with a QuickJS runtime and an Engine of its own, which carries everything a graph job's host does not (system
// nodes, ROM and battery resources, the protocol script packs, an advancing transport). The one kind is "render": the
// worker builds a retroplug::RenderHost (host/render/RenderHost.hpp), runs the job's spec through it in continuous
// blocks (whole blocks whatever each render call asks for, design section 4) and destroys it, all on that worker and
// inside the job's floating-point pin, so the runtime, the job's Engine, every core it boots and every node and
// resampler its plan builds are made and freed on the one thread, before the worker takes its next job. Nothing a
// render job touches belongs to the engine that owns this pool: the RenderHost's Engine is its own, as are its resource
// table, snapshot registry and script host. What it shares with other jobs, and with the live engine building cores on
// its control thread and running them on its audio thread, is the process's:
//   - Mesen's per-process home folder, under which each core's scratch paths (the SMS staged ROM, the GBA BIOS, the
//     NES SD card) are numbered per core by atomic counters rather than by system id, which every job's Engine starts
//     at 1;
//   - Mesen's MessageManager, whose process-wide SimpleLock keeps its holder and count in atomics (two cores built on
//     two threads at once raced on them as plain members);
//   - Mesen's two lazily built process tables behind a double-checked flag, the NES game database (GameDatabase, which
//     every iNES load reaches) and the GBA CPU's dispatch tables (GbaConsole's constructor): each flag is an atomic,
//     loaded with acquire before its unlocked check and stored with release after the build, so a core that sees the
//     build published also sees the table (MesenShared.test.cpp's "[mesen-statics]", red as plain bools under
//     ThreadSanitizer);
//   - Mesen's VRC7 sound chip tables (emu2413.cpp, built by the first OPLL_new behind a plain flag and no lock), the
//     one known to be unsynchronised: two VRC7 carts built at once as the process's first would race on it;
//   - SameBoy's core statics, made safe by cmake/patches/sameboy-process-statics.patch: its random generator
//     (Core/random.c, which runs at every reset for power-on RAM and during play for the Super Game Boy jingle's
//     noise and a camera shoot) and the camera's noise seed (Core/camera.c) are read and written as relaxed atomics,
//     so two callers at once may draw the same value, which only makes noise less random; the jingle's sine table
//     (Core/sgb.c) is built by a load-time constructor rather than on first use; and apu.c's band-limited steps were
//     already built that way. Everything else the core keeps at file scope is const, or the debugger's, which is not
//     built. A deterministic build turns the generator off, which is why ThreadSanitizer cannot see a race on it in
//     the engine-graph test binary;
//   - r8brain's statics, and the embedded bundle's bytes, which nothing writes.
// A worker that ran one render runs the next on a clean thread: QuickJS keeps no thread-local state (its class ids are
// per runtime, and a RenderHost registers no class), and the thread-local state Mesen keeps (its notification dispatch
// buffers, which hold no reference past a send, and SimpleLock's thread id) is per thread by design.
//
// An engine job reports as it runs: its script's progress (the milliseconds of audio rendered, RenderHost's
// onRendered) and its console writes go into the job's EngineJobRecord under the record's own lock, which the worker
// takes and a drain takes inside the pool's (never the other way round), and each drain hands back every running
// engine job's progress with the lines written since the drain before. The rendered milliseconds are the script's last
// report, which a render raises chunk by chunk and, when it detects a song's end, lowers once at the end to the length
// it wrote (its count included the chunks it held back past the stop); a completion's is that final length. A "line"
// is one console write, which can hold newlines of its own (a multi-line string, or a script exception's report with
// its stack). A record is bounded between drains (EngineJobRecord::kMaxLines, kMaxBytes), because a plugin whose
// editor is closed drains nothing for as long as a render runs; a line past either bound is dropped and counted, and
// the count is reported (and reset) with the lines. A cancel is the render's own cooperative check, polled once per
// chunk the render library pulls (100 ms of audio), so a running render stops within a chunk of the cancel, or before
// its first chunk if the cancel lands while it starts: its system's boot, the settle after it and the presses that
// start its playback (a song's load steps among them) all run first, a fraction of a second of wall time.
//
// Sharing the pool. A render runs a full emulator flat out for seconds or minutes, where a kit compile's graph jobs
// take milliseconds, so engine jobs run on at most engineBudget() workers at once, all but one of size(), and further
// engine jobs wait, in submission order, while graph jobs start past them: a worker takes the oldest job it may run.
// Graph jobs run on at most size() workers. A pool of size 1 cannot leave a worker free and still run a render at
// all, so its budget is 1, and it starts a second thread (maxWorkers() is 2) for whichever kind arrives while the other
// holds the first: a graph job while a render runs, so a kit compile shares the machine's one hardware thread with the
// render rather than waiting minutes behind it, and a render while a graph job runs, so it starts without waiting
// behind a kit. So a graph job always finds a worker, as long as the OS can start a thread when the pool
// needs one: a worker that cannot be started leaves the job queued for the workers there are (enqueue), which may all
// be rendering. Both kinds count toward kMaxJobs.
namespace rp::host {

// An engine job's record of what its script reported: the milliseconds of audio it has rendered, and the console lines
// it wrote since the record was last taken, within the bounds (at most kMaxLines lines and kMaxBytes bytes of them; a
// line past either is dropped and counted, and one longer than kMaxBytes alone is cut to it at a UTF-8 boundary).
// The job's worker writes it as the script reports; a drain, and at the end the worker, take it. Any thread, under its
// own lock, which a drain takes inside the pool's and the worker takes alone.
class EngineJobRecord {
public:
    static constexpr std::size_t kMaxLines = 256;
    static constexpr std::size_t kMaxBytes = 64 * 1024;

    void rendered(double ms);
    // One console write, the newline the console ends it with dropped.
    void write(EngineJobStream stream, std::string_view text);
    // The rendered length, and the lines and dropped count since the last take, which leave the record.
    template <class Report>
    void take(Report& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        out.renderedMs = renderedMs_;
        out.lines = std::move(lines_);
        out.droppedLines = dropped_;
        lines_.clear();
        bytes_ = 0;
        dropped_ = 0;
    }

private:
    std::mutex                 mutex_;
    double                     renderedMs_ = 0.0;
    std::vector<EngineJobLine> lines_;
    std::size_t                bytes_ = 0;
    std::uint32_t              dropped_ = 0;
};

// What an engine job's body hears from the pool and reports through: the rendered milliseconds and the console writes
// go into the job's record, and `cancelled` is the job's cancel flag, which the body polls (a render between its boot
// steps and once per chunk, and its runtime's interrupt handler past RenderHost::kCancelGrace).
struct EngineJobHooks {
    std::function<void(double ms)>                      rendered;
    std::function<bool()>                               cancelled;
    std::function<void(bool error, const char* text)>   console;
};
// How an engine job's body ended: "done", "cancelled" or "error", the error's message, and the files it wrote.
struct EngineJobOutcome {
    std::string              status;
    std::string              message;
    std::vector<std::string> outputs;
};
// An engine job's body, run on its worker inside the pool's floating-point pin. The pool's own runs a RenderHost over
// the spec; a test gives one of its own to hold the pool's scheduling, cancel and floating-point rules to an exact
// timeline.
using EngineJobBody = std::function<EngineJobOutcome(const EngineJobRequest&, const EngineJobHooks&)>;

class OfflineJobs {
public:
    static constexpr std::size_t   kMaxJobs = 64;        // outstanding: queued, running, or finished and not drained
    static constexpr std::size_t   kMaxResources = 64;   // inputs plus outputs, per job
    static constexpr unsigned      kMaxWorkers = 8;
    static constexpr std::uint32_t kBlockFrames = 1024;  // the block every job runs in, the last one partial
    // How long one drain may wait for a completion, whatever it is asked.
    static constexpr std::chrono::milliseconds kMaxDrainWait{1000};

    // The node types a job's document may name: the built-in C++ packages, then `extra` (a test's own), registered
    // here, before any worker runs, and never changed. A package the registry refuses is logged and left out. `size`
    // is the pool's size, concurrency() when 0 (a test gives another to run a pool of 1 on any machine), and `body`
    // what an engine job runs, the RenderHost body when empty (a test gives its own).
    explicit OfflineJobs(std::span<const rp_package* const> extra = {}, unsigned size = 0, EngineJobBody body = {});
    // Cancels every job: the queued ones are dropped unrun, the running ones stop at their next block boundary or
    // before their next input (a graph job) or at their next boot step or chunk (an engine job, whose script is
    // interrupted if it has not seen the cancel within RenderHost::kCancelGrace); then joins every worker.
    // Completions not yet drained go with the pool.
    ~OfflineJobs();
    OfflineJobs(const OfflineJobs&) = delete;
    OfflineJobs& operator=(const OfflineJobs&) = delete;

    // The default pool size: std::thread::hardware_concurrency(), at least 1, at most kMaxWorkers.
    static unsigned concurrency();
    // The pool's size: graph jobs run on at most this many workers at once.
    unsigned size() const { return size_; }
    // Engine jobs run on at most this many workers at once: all but one of size(), and 1 for a pool of 1.
    unsigned engineBudget() const { return size_ > 1 ? size_ - 1 : 1; }
    // The threads the pool starts at most: size(), or 2 for a pool of 1, whose second thread a job of one kind takes
    // while one of the other holds the first (a graph job beside a render, or a render beside a graph job).
    unsigned maxWorkers() const { return size_ > 1 ? size_ : 2; }

    // ---- any thread (the engine calls them on its control thread) ----
    // Take a job, or refuse it at once (id 0, the reason in `error`): a rate GraphHost refuses, more than kMaxResources
    // inputs and outputs, an input of samples with no channels, a sample count that is not a whole number of frames or
    // a rate that is not a positive number, a file input with no path or a cap whose rate is not a positive number, an
    // output createAudioBuffer would refuse, a return naming no handle of the job, kMaxJobs outstanding, and a worker
    // that cannot be started. What fails only once the job runs (a file that will not decode, a document the planner
    // refuses, a node that will not activate or faults, a cancel) is its completion's error.
    OfflineSubmit submit(OfflineRequest request);
    // Take an engine job, or refuse it at once: a kind that is not "render", kMaxJobs outstanding (graph and engine
    // jobs together), and a worker that cannot be started. Everything about its spec is the job's to find out, and its
    // completion's status "error" says what.
    OfflineSubmit submitEngine(EngineJobRequest request);
    // The graph jobs' completions finished since the last drain, oldest first. With `engine`, the engine jobs'
    // completions are taken too, and every running engine job's progress with the console lines it wrote since the
    // drain before (each line one console write, which can hold newlines of its own); without it, they stay for a
    // drain that takes them, still outstanding. With `wait`, and nothing to take finished, waits up to `wait` (at most
    // kMaxDrainWait) for one, returning at once when no job is queued or running; never waits otherwise. `inFlight`,
    // when given, is set to the jobs still queued or running after it, read under the same lock.
    std::vector<OfflineCompletion> drain(std::chrono::milliseconds wait = std::chrono::milliseconds(0),
                                         std::size_t* inFlight = nullptr, EngineJobsDrain* engine = nullptr);
    // Cancel job `id`: a queued one completes now, unrun; a running graph job stops at its next block boundary, or as
    // it finishes when it is already past its last (a job decoding a file finishes that decode and stops before its
    // next input; one planning finishes the plan and stops before its first block); a running engine job at its next
    // chunk. Either completes with the reason "cancelled" (a graph job's error, an engine job's status). False for an
    // id that is neither queued nor running (finished, or never taken).
    bool cancel(std::uint32_t id);
    // Jobs outstanding: queued, running, or finished and not yet drained.
    std::size_t outstanding() const;
    // Worker threads started so far (never more than maxWorkers()).
    std::size_t workers() const;
    // The node types a job's document may name.
    const graph::NodeRegistry& registry() const { return registry_; }

private:
    enum class Kind : std::uint8_t { Graph, Engine };

    struct Job {
        std::uint32_t     id = 0;
        Kind              kind = Kind::Graph;
        OfflineRequest    request;  // a graph job's
        EngineJobRequest  engine;   // an engine job's
        std::atomic<bool> cancelled{false};
        EngineJobRecord   record;   // an engine job's
    };

    // ---- under mutex_ ----
    // Whether a queued job of this kind may start now, given the workers its kind already holds.
    bool runnable(const Job& job) const;
    // How many queued jobs could start now: what a parked worker would take.
    std::size_t runnableQueued() const;
    // Queue the job, starting a worker when the runnable jobs outnumber the parked ones; refuses (and drops the job)
    // when the pool is going, is full, or can start no worker at all.
    OfflineSubmit enqueue(std::unique_ptr<Job> job);
    // An engine job's completion with `status`, its record taken into it.
    static EngineJobCompletion engineCompletion(Job& job, std::string status);

    void              workerLoop();
    // Runs the job, and releases its request's samples once the job's table holds them.
    OfflineCompletion run(Job& job);
    // Runs an engine job's body on this thread, inside the floating-point pin.
    EngineJobCompletion runEngine(Job& job);
    // The pool's own engine job body: builds a RenderHost on this thread, runs the spec in continuous blocks, and
    // destroys it here.
    static EngineJobOutcome renderBody(const EngineJobRequest& request, const EngineJobHooks& hooks);

    // Filled by the constructor and never changed: the workers read it without the lock.
    graph::NodeRegistry registry_;
    const unsigned      size_;
    const EngineJobBody body_;

    mutable std::mutex                mutex_;
    std::condition_variable           work_;      // a worker waits for a job it may run, or for the pool to stop
    std::condition_variable           finished_;  // a drain waits for a completion
    std::deque<std::unique_ptr<Job>>  queue_;
    std::vector<Job*>                 running_;   // owned by the worker running each
    std::deque<OfflineCompletion>     completions_;
    std::deque<EngineJobCompletion>   engineCompletions_;
    std::size_t                       outstanding_ = 0;
    std::size_t                       idle_ = 0;  // workers not running a job: parked, or started and not yet parked
    unsigned                          runningGraph_ = 0;
    unsigned                          runningEngine_ = 0;
    std::vector<std::thread>          workers_;
    std::uint32_t                     nextId_ = 1;
    bool                              stopping_ = false;
};

}  // namespace rp::host

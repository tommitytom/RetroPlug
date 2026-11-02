#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "graph/NodeRegistry.hpp"
#include "host/engine/OfflineTypes.hpp"
#include "graph/rp_node.h"

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
// The workers are started as jobs need them, up to concurrency() (the machine's hardware threads, at least one, at
// most kMaxWorkers), so a host that never submits has no thread at all; jobs past that wait in submission order. A
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
namespace rp::host {

class OfflineJobs {
public:
    static constexpr std::size_t   kMaxJobs = 64;        // outstanding: queued, running, or finished and not drained
    static constexpr std::size_t   kMaxResources = 64;   // inputs plus outputs, per job
    static constexpr unsigned      kMaxWorkers = 8;
    static constexpr std::uint32_t kBlockFrames = 1024;  // the block every job runs in, the last one partial
    // How long one drain may wait for a completion, whatever it is asked.
    static constexpr std::chrono::milliseconds kMaxDrainWait{1000};

    // The node types a job's document may name: the built-in C++ packages, then `extra` (a test's own), registered
    // here, before any worker runs, and never changed. A package the registry refuses is logged and left out.
    explicit OfflineJobs(std::span<const rp_package* const> extra = {});
    // Cancels every job: the queued ones are dropped unrun, the running ones stop at their next block boundary; then
    // joins every worker. Completions not yet drained go with the pool.
    ~OfflineJobs();
    OfflineJobs(const OfflineJobs&) = delete;
    OfflineJobs& operator=(const OfflineJobs&) = delete;

    // The workers the pool runs jobs on at most: std::thread::hardware_concurrency(), at least 1, at most kMaxWorkers.
    static unsigned concurrency();

    // ---- any thread (the engine calls them on its control thread) ----
    // Take a job, or refuse it at once (id 0, the reason in `error`): a rate GraphHost refuses, more than kMaxResources
    // inputs and outputs, an input of samples with no channels, a sample count that is not a whole number of frames or
    // a rate that is not a positive number, a file input with no path or a cap whose rate is not a positive number, an
    // output createAudioBuffer would refuse, a return naming no handle of the job, kMaxJobs outstanding, and a worker
    // that cannot be started. What fails only once the job runs (a file that will not decode, a document the planner
    // refuses, a node that will not activate or faults, a cancel) is its completion's error.
    OfflineSubmit submit(OfflineRequest request);
    // The completions finished since the last drain, oldest first. With `wait`, and none finished, waits up to `wait`
    // (at most kMaxDrainWait) for one, returning at once when no job is queued or running; never waits otherwise.
    // `inFlight`, when given, is set to the jobs still queued or running after it, read under the same lock.
    std::vector<OfflineCompletion> drain(std::chrono::milliseconds wait = std::chrono::milliseconds(0),
                                         std::size_t* inFlight = nullptr);
    // Cancel job `id`: a queued one completes now, unrun; a running one stops at its next block boundary, or as it
    // finishes when it is already past its last (a job decoding a file or planning finishes that decode or that plan first, and stops before the next input).
    // Either completes with the reason "cancelled". False for an id that is neither queued nor running (finished, or
    // never taken).
    bool cancel(std::uint32_t id);
    // Jobs outstanding: queued, running, or finished and not yet drained.
    std::size_t outstanding() const;
    // Worker threads started so far (never more than concurrency()).
    std::size_t workers() const;
    // The node types a job's document may name.
    const graph::NodeRegistry& registry() const { return registry_; }

private:
    struct Job {
        std::uint32_t     id = 0;
        OfflineRequest    request;
        std::atomic<bool> cancelled{false};
    };

    void              workerLoop();
    // Runs the job, and releases its request's samples once the job's table holds them.
    OfflineCompletion run(Job& job);

    // Filled by the constructor and never changed: the workers read it without the lock.
    graph::NodeRegistry registry_;

    mutable std::mutex                mutex_;
    std::condition_variable           work_;      // a worker waits for a job, or for the pool to stop
    std::condition_variable           finished_;  // a drain waits for a completion
    std::deque<std::unique_ptr<Job>>  queue_;
    std::vector<Job*>                 running_;   // owned by the worker running each
    std::deque<OfflineCompletion>     completions_;
    std::size_t                       outstanding_ = 0;
    std::size_t                       idle_ = 0;  // workers parked, waiting for a job
    std::vector<std::thread>          workers_;
    std::uint32_t                     nextId_ = 1;
    bool                              stopping_ = false;
};

}  // namespace rp::host

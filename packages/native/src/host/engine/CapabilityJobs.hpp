#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "graph/rp_jobs.h"         // rp_job_request, rp_job, rp_host_jobs, the capability ids
#include "system/SystemTypes.hpp"  // SystemId

class SystemBase;

// The capability job model (docs/modular-graph-phase-c5-design.md section 6): the control plane asks a node of the
// running graph to do something to the system behind it, a read, a write, a dump, and the answer comes later. Every
// job has the same life whoever answers it: Engine::submitJob takes it or refuses it at once, and its completion is
// collected by Engine::drainJobs, from a bounded queue the control plane polls. A job completes in one of three ways:
//
//   - at once, on the control thread: a read of an emulated system, served from the snapshot registry's published
//     copy, or a read of its ROM, which never changes. The completion is queued before submitJob returns.
//   - at a block boundary: a write to an emulated system rides the command ring, in push order with the plans, is
//     applied by the block thread at the drain that reaches it, and completes once the block after it has published the
//     region, so a read the control plane makes after the completion sees it. While no audio thread runs, the push
//     flushes inline and the engine republishes the region there and then, so the job's state is final before
//     submitJob returns (as a write is in the CLI between renders) and its completion is queued by the next drain.
//   - later, on another thread, with progress: a job a device runs on its own thread (an N8 cart's dump or upload,
//     seconds of USB transfers). The record below carries what that thread reports: its progress, then its result
//     bytes or its error, then the terminal state, stored last with release, which is what the control thread acquires
//     before it reads the rest.
//
// A job's record is the control thread's: made by submitJob (a device node's included) or adoptJob, freed once its
// state is terminal by the next sweep (drainJobs, submitJob, adoptJob and Engine::writeRam each sweep first), or by the
// engine's destruction. Another thread only writes the fields the hand-off covers, never frees it, and never touches it
// after storing a terminal state: the block thread writes a write's `core`, `failure` and its `Applied` state at the
// apply (a detached write's `Done`, and nothing after) and its terminal state at the confirm, a device thread its
// progress, result, error and failure. So the block thread allocates and frees nothing for a job: a write's bytes are
// allocated on the control thread with the record and freed back on it.
//
// A device's node takes its jobs through graph/rp_jobs.h (docs/modular-graph-phase-e2-design.md section 5): it is
// handed the request as an rp_job_request (requestView) and the record as the opaque rp_job (handle), and it finishes
// the job through rp.host.jobs/1 (jobReports), which turns the handle back into the record (record). The rules a
// device's jobs keep (one at a time per device, refused "busy" or "not connected" before the engine keeps a record;
// abandoned as its node leaves the plan; progress overwriting) are that header's. The engine's own bound
// (Engine::kMaxJobs outstanding, engine-wide, the back-pressure that keeps room for every completion) does not know
// about devices: drainJobs reports a running job once per drain whose progress changed since the last, with the value
// it finds then, so a job that reports in a tight loop while nobody drains accumulates nothing, and its listener sees
// the latest value, not every step.

namespace rp::host {

// What the control plane asks for. The arguments an operation does not read are ignored.
struct JobRequest {
    std::string               nodeId;      // a node of the last published plan
    std::string               capability;  // one of graph/rp_jobs.h's capability ids
    std::string               op;          // memory: regions, read, write; sram: dump, restore; rom: header, title,
                                           // upload (a device's node only)
    std::string               region;      // memory: the region's name
    std::uint32_t             offset = 0;  // memory: where a read or write starts in the region
    std::uint32_t             length = 0;  // memory read, rom header: how many bytes (0: to the end)
    std::vector<std::uint8_t> bytes;       // memory write, sram restore, rom upload
    std::string               name;        // rom upload: the file name the image is stored under on the device;
                                           // sram on a cart: the ROM file name whose battery file the job reaches
};

// submitJob's answer: the job's id (counting up from 1, never reused), or 0 with the refusal in `error`.
struct JobSubmit {
    std::uint32_t id = 0;
    std::string   error;
};

// One entry of drainJobs: a finished job (`done`), or a running one that reported progress since the last drain.
struct JobCompletion {
    std::uint32_t             id = 0;
    bool                      done = false;
    bool                      ok = false;
    std::string               error;
    std::vector<std::uint8_t> bytes;  // the result; the caller owns it once drained
    std::uint32_t             progress = 0;
    std::uint32_t             total = 0;
};

// A job not yet collected: the control thread's, as above.
struct Job {
    enum State : std::uint8_t {
        Queued,   // taken, not yet applied (a write in the ring, a device job not yet finished)
        Applied,  // a write applied, waiting for the block that publishes it (the control thread's quiescent flush,
                  // or the block thread's confirm, finishes it)
        Done,     // finished: `result` holds its bytes
        Failed,   // finished: `failure` says why (and `error`, for one a device thread wrote)
    };
    enum Failure : std::uint8_t {
        None,
        OutOfRange,  // the region was smaller than the write by the time it applied (a model switch shrank it)
        Removed,     // the node left the plan before the job completed: its core is gone or about to go
        Discarded,   // the engine dropped the queued command at teardown, never applying it
        Device,      // a device thread failed it, the reason in `error`
        Overflow,    // more writes were applied than the engine's table of unpublished writes holds (a broken bound)
    };

    std::uint32_t id = 0;
    // A write the old system-id RPC (writeRam) made: it applies on the same path and nobody collects it, so it queues
    // no completion and is done once applied.
    bool          detached = false;
    std::string   nodeId;      // what its completion's error names
    SystemId      system = 0;
    std::size_t   region = 0;  // an index into the core's MemoryCapability::regions()
    std::uint32_t offset = 0;
    std::vector<std::uint8_t> bytes;  // the write's: read by the block thread, owned and freed here
    // The core the write was applied to: the block thread's (or the control thread's while no audio thread runs), set
    // at the apply and only compared at the confirm, never followed, so a system id re-added with a new core between
    // the two cannot pass for the one written.
    SystemBase*   core = nullptr;

    std::atomic<std::uint8_t>  state{Queued};
    std::atomic<std::uint8_t>  failure{None};
    std::atomic<std::uint32_t> progress{0};
    std::atomic<std::uint32_t> total{0};
    std::uint32_t              reported = 0;  // the progress drainJobs last handed back (control thread)
    std::vector<std::uint8_t>  result;         // a device job's bytes, written before its terminal state
    std::string                error;          // a device job's reason, likewise

    bool finished() const {
        const std::uint8_t s = state.load(std::memory_order_acquire);
        return s == Done || s == Failed;
    }
};

// The C request a device's node is handed for `r`, borrowing its strings and bytes: valid while `r` is.
inline rp_job_request requestView(const JobRequest& r) {
    return rp_job_request{r.nodeId.c_str(), r.capability.c_str(), r.op.c_str(), r.region.c_str(), r.name.c_str(),
                          r.offset,         r.length,             r.bytes.data(), r.bytes.size()};
}

// The record as the opaque handle a device's node is handed (graph/rp_jobs.h), and back. rp_job stays incomplete in
// every host, so each host (a test's fake one included) keeps a record of its own: a node hands its link the report
// table and the job of the same host, so jobReports only ever receives handles this engine minted, and the casts are
// sound.
inline rp_job* handle(Job* job) { return reinterpret_cast<rp_job*>(job); }
inline Job*    record(rp_job* job) { return reinterpret_cast<Job*>(job); }

// rp.host.jobs/1 over the records above: what a device reports a job it took through. One for every engine, since a
// report reaches only its record. Each report is the record's hand-off as the job model says: progress, then the
// result or the error, then the terminal state, stored last with release, which is what the control thread acquires
// before it reads the rest.
inline const rp_host_jobs* jobReports() {
    static const rp_host_jobs kReports{
        [](rp_job* h, std::uint32_t done, std::uint32_t total) {
            Job* job = record(h);
            job->total.store(total, std::memory_order_relaxed);
            job->progress.store(done, std::memory_order_release);
        },
        [](rp_job* h, const std::uint8_t* data, std::size_t size) {
            Job* job = record(h);
            job->result.assign(data, data + size);
            job->state.store(Job::Done, std::memory_order_release);
        },
        [](rp_job* h, const char* reason) {
            Job* job = record(h);
            job->error = reason != nullptr ? reason : "";
            job->failure.store(Job::Device, std::memory_order_relaxed);
            job->state.store(Job::Failed, std::memory_order_release);
        },
        [](rp_job* h) {
            Job* job = record(h);
            job->failure.store(Job::Removed, std::memory_order_relaxed);
            job->state.store(Job::Failed, std::memory_order_release);
        }};
    return &kReports;
}

}  // namespace rp::host

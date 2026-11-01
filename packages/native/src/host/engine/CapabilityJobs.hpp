#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

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
// after storing a terminal state: the block thread writes a write's `core` and `failure` at the apply and its state at
// the confirm, a device thread its progress, result, error and failure. So the block thread allocates and frees
// nothing for a job: a write's bytes are allocated on the control thread with the record and freed back on it.
//
// Two rules a device's jobs keep, beside the engine's own bound (Engine::kMaxJobs outstanding, engine-wide, the
// back-pressure that keeps room for every completion):
//
//   - One job at a time per device. The device's node hands a job to its device link (DeviceJobs, CartLink::submitJob),
//     which refuses a second at submission ("busy"), and one while the device is not attached ("not connected"),
//     synchronously, before the engine keeps a record; the engine's bound does not know about devices. A node leaving
//     the plan abandons its job (DeviceJobSlot::abandon), which fails it at once.
//   - Progress overwrites. A device thread stores its latest progress in the record, and drainJobs reports a running
//     job once per drain whose progress changed since the last, with the value it finds then: a job that reports in a
//     tight loop while nobody drains accumulates nothing, and its listener sees the latest value, not every step.

// The three interface ids a job may name, as the descriptors spell them. Defined here, outside the namespace, because
// the job model is what carries them.
#define RP_SYSTEM_MEMORY_INTERFACE "rp.system.memory/1"
#define RP_SYSTEM_SRAM_INTERFACE "rp.system.sram/1"
#define RP_SYSTEM_ROM_INTERFACE "rp.system.rom/1"

namespace rp::host {

// What the control plane asks for. The arguments an operation does not read are ignored.
struct JobRequest {
    std::string               nodeId;      // a node of the last published plan
    std::string               capability;  // one of the interface ids above
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

// What a device's node presents under each capability id it answers (rp.n8.nes: rp.system.sram/1 and rp.system.rom/1),
// through rp_node_vtable::get_interface; Engine::submitJob hands it every job for that node and capability. [control]
// `submit` takes the job into `job`, the record the engine keeps once it is taken, or refuses it with the reason in
// *error ("busy", "not connected", an operation it does not have, arguments it will not take), leaving the record
// untouched. A job taken is the device's to finish: on its own thread, through a DeviceJobSlot, or at once, by storing
// the record's terminal state before returning (a read the device's host can answer from what it keeps).
struct DeviceJobs {
    void* ctx;
    bool (*submit)(void* ctx, const JobRequest& request, Job& job, std::string* error);
};

// The one job a device runs at a time, between the control thread that hands it a record and the device's own thread
// that reports into it. The record is the engine's, freed once terminal, and the node that submitted the job can leave
// the plan while the device is still busy (a node removed, a plan torn down), so every touch the device makes goes
// through here, under a lock the control thread shares: once `abandon` has failed the record, the device's later
// reports reach nothing, and the record may be freed without waiting for the device to finish. The lock is held for a
// store or a copy, never across device I/O, and never by the block thread.
class DeviceJobSlot {
public:
    // [control] Take the slot for `job`, submitted by the node `owner`; false while another job holds it.
    bool claim(Job& job, const void* owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (busy_) return false;
        busy_ = true;
        job_ = &job;
        owner_ = owner;
        abandoned_.store(false, std::memory_order_relaxed);
        return true;
    }
    // [control] Give back a claim whose job never started (the device would not take it after all), the record
    // untouched.
    void unclaim() {
        std::lock_guard<std::mutex> lock(mutex_);
        clear();
    }
    // [device] The progress so far: overwrites, as CapabilityJobs.hpp says.
    void progress(std::uint32_t done, std::uint32_t total) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!job_) return;
        job_->total.store(total, std::memory_order_relaxed);
        job_->progress.store(done, std::memory_order_release);
    }
    // [device] Finish the job with its result, or with the reason it failed, and free the slot for the next. The
    // terminal state is the record's last touch, stored with release.
    void succeed(std::vector<std::uint8_t> bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job_) {
            job_->result = std::move(bytes);
            job_->state.store(Job::Done, std::memory_order_release);
        }
        clear();
    }
    void fail(std::string error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job_) {
            job_->error = std::move(error);
            job_->failure.store(Job::Device, std::memory_order_relaxed);
            job_->state.store(Job::Failed, std::memory_order_release);
        }
        clear();
    }
    // [control] The node `owner` is leaving the plan: a job it holds the slot for fails now (Removed), and the device
    // reports into its record no more. The slot stays busy until the device finishes (succeed or fail), since the
    // device is still busy; abandoned() tells it to stop at the next point that leaves the device coherent.
    void abandon(const void* owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!busy_ || owner_ != owner) return;
        abandoned_.store(true, std::memory_order_relaxed);
        if (!job_) return;
        job_->failure.store(Job::Removed, std::memory_order_relaxed);
        job_->state.store(Job::Failed, std::memory_order_release);
        job_ = nullptr;
    }
    // [any] Whether a job holds the slot (taken, not yet finished), and whether its node abandoned it.
    bool busy() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return busy_;
    }
    bool abandoned() const { return abandoned_.load(std::memory_order_relaxed); }

private:
    void clear() {
        busy_ = false;
        job_ = nullptr;
        owner_ = nullptr;
    }

    mutable std::mutex mutex_;
    bool               busy_ = false;
    Job*               job_ = nullptr;  // null once finished or abandoned: the device touches it no more
    const void*        owner_ = nullptr;
    std::atomic<bool>  abandoned_{false};
};

}  // namespace rp::host

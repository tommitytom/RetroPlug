#pragma once

// A job host of the pack's own tests (graph/rp_jobs.h): the record a job runs into, the request a case builds, and the
// rp.host.jobs/1 a device reports through, over that record. Not the engine's (host/engine/CapabilityJobs.hpp): the
// pack's tests reach nothing of packages/native, and a record that is not the engine's is what the job ABI promises
// works, since every host keeps rp_job incomplete and casts its own record to and from it. N8Host and N8CartLink take
// the job through their DeviceJobSlot and report into it exactly as they would into the engine's.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "graph/rp_jobs.h"

namespace rp::n8::test {

// What a case asks for, as the control plane would: the strings and bytes rp_job_request borrows.
struct JobRequest {
    std::string               nodeId;
    std::string               capability;
    std::string               op;
    std::string               region;
    std::uint32_t             offset = 0;
    std::uint32_t             length = 0;
    std::vector<std::uint8_t> bytes;
    std::string               name;
};

// The C request for `r`, borrowing its strings and bytes: valid while `r` is.
inline rp_job_request requestView(const JobRequest& r) {
    return rp_job_request{r.nodeId.c_str(), r.capability.c_str(), r.op.c_str(), r.region.c_str(), r.name.c_str(),
                          r.offset,         r.length,             r.bytes.empty() ? nullptr : r.bytes.data(),
                          r.bytes.size()};
}

// One job's record: untouched until the device takes it (Queued), then its progress, and its result or its reason
// before its terminal state, which is stored last with release and read with acquire, as the job model hands it off.
struct Job {
    enum State : std::uint8_t { Queued, Done, Failed };
    enum Failure : std::uint8_t { None, Removed, Device };

    std::atomic<std::uint8_t>  state{Queued};
    std::atomic<std::uint8_t>  failure{None};
    std::atomic<std::uint32_t> progress{0};
    std::atomic<std::uint32_t> total{0};
    std::vector<std::uint8_t>  result;
    std::string                error;

    bool finished() const {
        const std::uint8_t s = state.load(std::memory_order_acquire);
        return s == Done || s == Failed;
    }
};

inline rp_job* handle(Job* job) { return reinterpret_cast<rp_job*>(job); }
inline Job*    record(rp_job* job) { return reinterpret_cast<Job*>(job); }

// rp.host.jobs/1 over the records above.
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

}  // namespace rp::n8::test

#pragma once

// The one job a device runs at a time (graph/rp_jobs.h), between the control thread that hands it a record and the
// device's own thread that reports into it. Desktop only: it holds a std::mutex, and a device image has neither the
// threads it serialises nor the lock, so this is the desktop half of the SDK (graph/desktop/), and no device build may
// include it.

#ifdef RP_DEVICE
#error "graph/desktop/DeviceJobSlot.hpp is desktop-only (a std::mutex between a device's thread and the control thread)"
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "graph/rp_jobs.h"

namespace rp {

// The record is the host's, freed once terminal, and the node that submitted the job can leave the plan while the
// device is still busy (a node removed, a plan torn down), so every touch the device makes goes through here, under a
// lock the control thread shares: once `abandon` has failed the record, the device's later reports reach nothing, and
// the record may be freed without waiting for the device to finish. The lock is held for a report, never across device
// I/O, and never by the block thread. Every report goes through the host's rp.host.jobs/1, which the node submitting
// the job hands over with it.
class DeviceJobSlot {
public:
    // [control] Take the slot for `job`, submitted by the node `owner`, reporting through `reports`; false while
    // another job holds it.
    bool claim(const rp_host_jobs* reports, rp_job* job, const void* owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (busy_) return false;
        busy_ = true;
        reports_ = reports;
        job_ = job;
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
    // [device] The progress so far: overwrites, as graph/rp_jobs.h says.
    void progress(std::uint32_t done, std::uint32_t total) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!job_) return;
        reports_->progress(job_, done, total);
    }
    // [device] Finish the job with its result (the host copies it), or with the reason it failed, and free the slot for
    // the next. The terminal report is the record's last touch.
    void succeed(const std::vector<std::uint8_t>& bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job_) reports_->succeed(job_, bytes.data(), bytes.size());
        clear();
    }
    void fail(const std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (job_) reports_->fail(job_, error.c_str());
        clear();
    }
    // [control] The node `owner` is leaving the plan: a job it holds the slot for fails now (its node's removal), and
    // the device reports into its record no more. The slot stays busy until the device finishes (succeed or fail),
    // since the device is still busy; abandoned() tells it to stop at the next point that leaves the device coherent.
    void abandon(const void* owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!busy_ || owner_ != owner) return;
        abandoned_.store(true, std::memory_order_relaxed);
        if (!job_) return;
        reports_->abandon(job_);
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
        reports_ = nullptr;
        job_ = nullptr;
        owner_ = nullptr;
    }

    mutable std::mutex  mutex_;
    bool                busy_ = false;
    const rp_host_jobs* reports_ = nullptr;
    rp_job*             job_ = nullptr;  // null once finished or abandoned: the device touches it no more
    const void*         owner_ = nullptr;
    std::atomic<bool>   abandoned_{false};
};

}  // namespace rp

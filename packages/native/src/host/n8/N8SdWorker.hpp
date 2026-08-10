#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace retroplug {

// The current/last SD job as the UI reads it (matches the __rp_getN8SdStatus object in n8SdOps.ts).
struct N8SdStatusDto {
    bool          busy = false;
    std::string   op;                // "load" | "dump" | "restore" | "" (idle)
    std::uint64_t bytesDone = 0;
    std::uint64_t bytesTotal = 0;    // 0 = indeterminate
    std::string   phase;             // short human label, e.g. "Uploading"
    bool          done = false;      // the last job finished (success or error)
    std::string   error;             // "" = ok
    std::string   result;            // human summary on success
    std::uint64_t version = 0;       // bumped on any change, so the UI re-renders only when it moves
};

// A single-in-flight background runner for the blocking N8 SD / menu control ops (ROM upload, 64 KB SRAM
// read/write). The blocking Edio I/O runs on a dedicated worker thread so it never touches the UI thread;
// the UI polls status() each frame. Lifecycle modeled on N8Link: a busy flag + a std::thread member LAST,
// joined in the dtor (and before the next start). The job closure (built by N8Host) borrows the serial
// port from N8Link, drives a C++ Edio, and reports progress through the Progress handle; throwing marks the
// job failed.
class N8SdWorker {
public:
    // Progress-reporting handle passed to a running job. Thread-safe (the UI reads the same fields).
    class Progress {
    public:
        void phase(const std::string& p);   // short status label; bumps version
        void total(std::uint64_t n);         // set bytesTotal
        void advance(std::uint64_t n);       // bytesDone += n
        void result(const std::string& r);   // success summary

    private:
        friend class N8SdWorker;
        explicit Progress(N8SdWorker& w) : w_(w) {}
        N8SdWorker& w_;
    };

    // A unit of work. Runs on the worker thread; a thrown std::exception becomes the job's error.
    using Job = std::function<void(Progress&)>;

    N8SdWorker() = default;
    ~N8SdWorker();
    N8SdWorker(const N8SdWorker&)            = delete;
    N8SdWorker& operator=(const N8SdWorker&) = delete;

    // Start `job` tagged with `op` ("load"/"dump"/"restore"). Returns false (does nothing) if already busy.
    bool start(const std::string& op, Job job);

    bool          busy() const { return busy_.load(std::memory_order_acquire); }
    N8SdStatusDto status();

private:
    void bump() { version_.fetch_add(1, std::memory_order_release); }

    std::atomic<bool>          busy_{false};
    std::atomic<std::uint64_t> bytesDone_{0};
    std::atomic<std::uint64_t> bytesTotal_{0};
    std::atomic<bool>          done_{false};
    std::atomic<std::uint64_t> version_{0};

    mutable std::mutex meta_;  // guards op_ / phase_ / error_ / result_ (short strings; UI reads a copy)
    std::string        op_;
    std::string        phase_;
    std::string        error_;
    std::string        result_;

    std::thread thread_;  // LAST: joined in the dtor / before the next start, so members outlive the job
};

}  // namespace retroplug

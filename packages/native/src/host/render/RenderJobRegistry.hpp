#pragma once

// RenderJobRegistry — owns the in-flight background render jobs. Each job runs a RenderHost on its OWN
// dedicated std::thread, so multiple systems can render at once and the UI menu can close while a render
// runs (the job lives here, not in the React tree). Cancellation is cooperative: cancel() flips an atomic
// the worker polls between chunks. snapshot() gives the UI a thread-safe copy of every job's status to poll
// each frame. Owned at the plugin/app level so a render survives the editor window closing; the destructor
// requests-cancel + joins every thread.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace retroplug {

class RenderJobRegistry {
public:
    using JobId = std::uint64_t;

    enum class State { Rendering, Done, Error, Cancelled };

    // A copyable snapshot of one job's state (returned to the UI; never exposes the thread/atomic).
    struct Status {
        JobId id = 0;
        std::uint32_t systemId = 0;         // the source system this render came from (for the tile badge)
        State state = State::Rendering;
        double progress = 0.0;              // 0..1
        std::string message;                // error detail (state == Error)
        std::vector<std::string> outputs;   // WAV paths written (state == Done)
    };

    RenderJobRegistry() = default;
    ~RenderJobRegistry();
    RenderJobRegistry(const RenderJobRegistry&) = delete;
    RenderJobRegistry& operator=(const RenderJobRegistry&) = delete;

    // Start a render on a dedicated thread. `jobJson` is the RenderOpts-shaped spec the worker consumes.
    // `systemId` tags the job with its originating system (0 if none). Returns the new job id.
    JobId start(std::uint32_t systemId, std::string jobJson);

    // Request cooperative cancellation (the worker aborts at the next chunk boundary). No-op if unknown.
    void cancel(JobId id);

    // A thread-safe copy of every tracked job's status (for the per-frame UI poll).
    std::vector<Status> snapshot() const;

    // Join + drop every job that has finished (Done / Error / Cancelled). Called periodically by the UI so
    // terminal jobs don't accumulate; safe to call anytime (running jobs are left untouched).
    void clearFinished();

    // A stable lowercase name for a state ("rendering" | "done" | "error" | "cancelled").
    static const char* stateName(State s);

private:
    struct Job {
        Status status;
        std::string jobJson;
        std::atomic<bool> cancelRequested{false};
        std::atomic<bool> finished{false};
        std::thread thread;
    };

    void runJob(Job* job);

    mutable std::mutex mutex_;
    std::unordered_map<JobId, std::unique_ptr<Job>> jobs_;
    JobId nextId_ = 1;
};

} // namespace retroplug

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
        double renderedMs = 0.0;            // audio rendered so far, in milliseconds
        std::string message;                // error detail (state == Error)
        std::vector<std::string> outputs;   // WAV paths written (state == Done)
    };

    RenderJobRegistry() = default;
    ~RenderJobRegistry();
    RenderJobRegistry(const RenderJobRegistry&) = delete;
    RenderJobRegistry& operator=(const RenderJobRegistry&) = delete;

    // Start a render on a dedicated thread. `jobJson` is the RenderOpts-shaped spec the worker consumes.
    // `owner` is an opaque token (the editor that started it) so a multi-instance host only shows its own
    // jobs; `systemId` tags the job with its originating system (0 if none). Returns the new job id.
    JobId start(const void* owner, std::uint32_t systemId, std::string jobJson);

    // Request cooperative cancellation (the worker aborts at the next chunk boundary). No-op if unknown.
    void cancel(JobId id);

    // A thread-safe copy of the tracked jobs belonging to `owner` (for that editor's per-frame UI poll).
    std::vector<Status> snapshot(const void* owner) const;

    // Join + drop one finished job (the UI dismisses a done/cancelled badge, or clears an error). No-op if
    // the job is unknown or still running.
    void dismiss(JobId id);

    // Join + drop every job that has finished (Done / Error / Cancelled). Safe anytime (running jobs are
    // left untouched); the destructor and a periodic sweep use it.
    void clearFinished();

    // A stable lowercase name for a state ("rendering" | "done" | "error" | "cancelled").
    static const char* stateName(State s);

private:
    struct Job {
        Status status;
        std::string jobJson;
        const void* owner = nullptr;  // the editor that started it; snapshot() filters on this
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

#include "host/render/RenderJobRegistry.hpp"

#include "host/render/RenderHost.hpp"

namespace retroplug {

RenderJobRegistry::~RenderJobRegistry() {
    // Request cancellation of everything, then join. (The registry is being torn down, so no other thread
    // is calling start()/cancel() concurrently — safe to iterate without holding the lock for the joins.)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [id, job] : jobs_) job->cancelRequested.store(true, std::memory_order_relaxed);
    }
    for (auto& [id, job] : jobs_)
        if (job->thread.joinable()) job->thread.join();
}

RenderJobRegistry::JobId RenderJobRegistry::start(const void* owner, std::uint32_t systemId, std::string jobJson) {
    std::lock_guard<std::mutex> lk(mutex_);
    const JobId id = nextId_++;
    auto job = std::make_unique<Job>();
    job->status.id = id;
    job->status.systemId = systemId;
    job->status.state = State::Rendering;
    job->jobJson = std::move(jobJson);
    job->owner = owner;
    Job* raw = job.get(); // stable: the Job lives on the heap; a rehash moves the unique_ptr, not the object
    jobs_[id] = std::move(job);
    raw->thread = std::thread([this, raw] { runJob(raw); });
    return id;
}

void RenderJobRegistry::cancel(JobId id) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = jobs_.find(id);
    if (it != jobs_.end()) it->second->cancelRequested.store(true, std::memory_order_relaxed);
}

std::vector<RenderJobRegistry::Status> RenderJobRegistry::snapshot(const void* owner) const {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<Status> out;
    for (const auto& [id, job] : jobs_)
        if (job->owner == owner) out.push_back(job->status);
    return out;
}

void RenderJobRegistry::dismiss(JobId id) {
    std::thread toJoin;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = jobs_.find(id);
        if (it == jobs_.end() || !it->second->finished.load(std::memory_order_acquire)) return;
        toJoin = std::move(it->second->thread);
        jobs_.erase(it);
    }
    if (toJoin.joinable()) toJoin.join();
}

void RenderJobRegistry::clearFinished() {
    std::vector<std::thread> toJoin; // join OUTSIDE the lock (a finished thread joins immediately anyway)
    {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto it = jobs_.begin(); it != jobs_.end();) {
            if (it->second->finished.load(std::memory_order_acquire)) {
                toJoin.push_back(std::move(it->second->thread));
                it = jobs_.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& t : toJoin)
        if (t.joinable()) t.join();
}

const char* RenderJobRegistry::stateName(State s) {
    switch (s) {
        case State::Rendering: return "rendering";
        case State::Done: return "done";
        case State::Error: return "error";
        case State::Cancelled: return "cancelled";
    }
    return "unknown";
}

void RenderJobRegistry::runJob(Job* job) {
    RenderHost host;

    RenderHost::Result r = host.run(
        job->jobJson,
        [this, job](double ms) {
            std::lock_guard<std::mutex> lk(mutex_);
            job->status.renderedMs = ms;
        },
        [job] { return job->cancelRequested.load(std::memory_order_relaxed); });

    {
        std::lock_guard<std::mutex> lk(mutex_);
        job->status.outputs = r.outputs;
        job->status.message = r.message;
        if (r.status == "done") {
            job->status.state = State::Done; // renderedMs keeps the worker's last report: the length written
        } else if (r.status == "cancelled") {
            job->status.state = State::Cancelled;
        } else {
            job->status.state = State::Error;
        }
    }
    // Last: publish "finished" so clearFinished() may reap this job. `host` is still alive on this stack and
    // is destroyed as runJob returns; clearFinished joins the thread, so the Job outlives the reap.
    job->finished.store(true, std::memory_order_release);
}

} // namespace retroplug

// retroplug-render-host-test — a thin harness for the background-render pieces:
//
//   retroplug-render-host-test <job-json>          drive ONE RenderHost render (byte-identical parity vs
//                                                   `retroplug-cli render`; exits 0 on a "done" result)
//   retroplug-render-host-test --registry <j1> <j2>...   run N jobs CONCURRENTLY through RenderJobRegistry,
//                                                   poll to completion, assert all done (exits 0)
//   retroplug-render-host-test --cancel <job-json>  start a job, cancel it mid-render, assert it ends
//                                                   "cancelled" (exits 0)
//
// Not shipped; built by name for the Phase-2/3 checks.

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "host/render/RenderHost.hpp"
#include "host/render/RenderJobRegistry.hpp"

namespace {

using retroplug::RenderJobRegistry;

int runOne(const char* jobJson) {
    retroplug::RenderHost host;
    double lastProgress = -1.0;
    retroplug::RenderHost::Result r = host.run(
        jobJson,
        [&lastProgress](double f) { lastProgress = f; },
        [] { return false; });
    std::fprintf(stderr, "render-host-test: status=%s progress=%.3f message=%s outputs=%zu\n",
                 r.status.c_str(), lastProgress, r.message.c_str(), r.outputs.size());
    for (const std::string& p : r.outputs) std::fprintf(stderr, "  out: %s\n", p.c_str());
    return r.ok() ? 0 : 1;
}

// Run N jobs concurrently through the registry; poll snapshots until all terminal. Returns 0 iff all Done.
int runRegistry(const std::vector<std::string>& jobs) {
    RenderJobRegistry registry;
    for (std::size_t i = 0; i < jobs.size(); ++i) registry.start(nullptr, static_cast<std::uint32_t>(i + 1), jobs[i]);

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto snap = registry.snapshot(nullptr);
        std::size_t terminal = 0;
        for (const auto& s : snap)
            if (s.state != RenderJobRegistry::State::Rendering) ++terminal;
        if (terminal == snap.size()) {
            int rc = 0;
            for (const auto& s : snap) {
                std::fprintf(stderr, "job %llu (sys %u): %s progress=%.3f outputs=%zu %s\n",
                             (unsigned long long)s.id, s.systemId, RenderJobRegistry::stateName(s.state),
                             s.progress, s.outputs.size(), s.message.c_str());
                if (s.state != RenderJobRegistry::State::Done) rc = 1;
            }
            registry.clearFinished();
            return rc;
        }
    }
}

// Start one (long) job, cancel it shortly after, assert it terminates "cancelled".
int runCancel(const char* jobJson) {
    RenderJobRegistry registry;
    RenderJobRegistry::JobId id = registry.start(nullptr, 1, jobJson);
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // let it get past boot into the render loop
    registry.cancel(id);

    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto snap = registry.snapshot(nullptr);
        if (snap.empty()) break;
        const auto& s = snap.front();
        if (s.state != RenderJobRegistry::State::Rendering) {
            std::fprintf(stderr, "cancel: job %llu ended %s (progress=%.3f)\n",
                         (unsigned long long)s.id, RenderJobRegistry::stateName(s.state), s.progress);
            return s.state == RenderJobRegistry::State::Cancelled ? 0 : 1;
        }
    }
    return 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--registry") == 0) {
        std::vector<std::string> jobs;
        for (int i = 2; i < argc; ++i) jobs.emplace_back(argv[i]);
        return runRegistry(jobs);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--cancel") == 0) {
        return runCancel(argv[2]);
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: retroplug-render-host-test <job-json> | --registry <j>... | --cancel <j>\n");
        return 2;
    }
    return runOne(argv[1]);
}

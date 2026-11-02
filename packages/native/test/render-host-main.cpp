// retroplug-render-host-test - a thin harness for the engine jobs that run renders (host/engine/OfflineJobs.hpp):
//
//   retroplug-render-host-test <job-json>          run ONE render as an engine job (its bytes are those of
//                                                   `retroplug-cli render`, which submits the same job; exits 0 on
//                                                   a "done" completion)
//   retroplug-render-host-test --parallel <j1> <j2>...   run N renders as engine jobs on one pool sized so all N run at
//                                                   once; assert every one is done and that they overlapped (exits 0)
//   retroplug-render-host-test --cancel <job-json>  submit a render, cancel it mid-render, assert it completes
//                                                   "cancelled" with no outputs and no file left (exits 0)
//
// Not shipped; built by name for the render checks (and under the sanitizers, in build-tsan/ and build-asan/, for the
// pool's threads). The [engine-jobs] cases of retroplug-engine-graph-test are the automated guard.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "host/engine/OfflineJobs.hpp"

namespace {

using rp::host::EngineJobCompletion;
using rp::host::EngineJobsDrain;
using rp::host::OfflineJobs;

// Every line goes to stderr, flushed as written, so the jobs' console lines and this tool's own status lines keep their
// order in a redirected log.
void echo(const std::vector<rp::host::EngineJobLine>& lines) {
    for (const auto& l : lines) std::fprintf(stderr, "%s\n", l.text.c_str());
    std::fflush(stderr);
}

struct Collected {
    std::map<std::uint32_t, EngineJobCompletion> done;
    std::size_t                                  mostRunning = 0;  // the most jobs one drain saw running at once
};

// Drains the pool until `count` jobs have completed (or `timeout` passes), echoing the jobs' console lines.
Collected collect(OfflineJobs& pool, std::size_t count, std::chrono::seconds timeout = std::chrono::seconds(900)) {
    Collected   out;
    const auto  end = std::chrono::steady_clock::now() + timeout;
    while (out.done.size() < count && std::chrono::steady_clock::now() < end) {
        EngineJobsDrain drain;
        pool.drain(std::chrono::milliseconds(100), nullptr, &drain);
        std::set<std::uint32_t> running;
        for (const auto& p : drain.progress) {
            running.insert(p.id);
            echo(p.lines);
        }
        for (auto& c : drain.completions) {
            running.insert(c.id);
            echo(c.lines);
            out.done.emplace(c.id, std::move(c));
        }
        out.mostRunning = std::max(out.mostRunning, running.size());
    }
    return out;
}

std::uint32_t submit(OfflineJobs& pool, const std::string& spec) {
    rp::host::OfflineSubmit s = pool.submitEngine({"render", spec});
    if (s.id == 0) std::fprintf(stderr, "render-host-test: submit refused: %s\n", s.error.c_str());
    return s.id;
}

void print(const EngineJobCompletion& c) {
    std::fprintf(stderr, "job %u: %s rendered=%.0fms outputs=%zu %s\n", c.id, c.status.c_str(), c.renderedMs,
                 c.outputs.size(), c.message.c_str());
    for (const std::string& p : c.outputs) std::fprintf(stderr, "  out: %s\n", p.c_str());
    std::fflush(stderr);
}

// Run the jobs on a pool whose engine budget covers all of them; returns 0 iff every one completes "done" and, with
// more than one, the pool ran them side by side.
int runJobs(const std::vector<std::string>& specs) {
    // An engine budget is the pool's size less one, so N renders at once want N + 1 workers.
    OfflineJobs pool({}, static_cast<unsigned>(std::min<std::size_t>(specs.size() + 1, OfflineJobs::kMaxWorkers)));
    std::vector<std::uint32_t> ids;
    for (const std::string& spec : specs) {
        const std::uint32_t id = submit(pool, spec);
        if (id == 0) return 1;
        ids.push_back(id);
    }
    const Collected got = collect(pool, ids.size());
    int             rc = got.done.size() == ids.size() ? 0 : 1;
    if (rc != 0)
        std::fprintf(stderr, "render-host-test: %zu of %zu jobs never completed\n", ids.size() - got.done.size(),
                     ids.size());
    for (const auto& [id, c] : got.done) {
        print(c);
        if (c.status != "done") rc = 1;
    }
    const std::size_t want = std::min<std::size_t>(ids.size(), pool.engineBudget());
    if (rc == 0 && got.mostRunning < want) {
        std::fprintf(stderr, "render-host-test: %zu jobs ran at once, expected %zu\n", got.mostRunning, want);
        rc = 1;
    } else if (rc == 0) {
        std::fprintf(stderr, "render-host-test: up to %zu jobs ran at once\n", got.mostRunning);
    }
    return rc;
}

// Submit one (long) job, cancel it shortly after, assert it completes "cancelled" and leaves nothing behind. `out` is
// the spec's output path, which a cancelled render must not leave.
int runCancel(const std::string& spec, const std::string& out) {
    OfflineJobs         pool;
    const std::uint32_t id = submit(pool, spec);
    if (id == 0) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(300)); // let it get past boot into the render loop
    if (!pool.cancel(id)) {
        std::fprintf(stderr, "cancel: job %u was not running (it finished before the cancel)\n", id);
        return 1;
    }
    const Collected got = collect(pool, 1, std::chrono::seconds(60));
    if (got.done.empty()) {
        std::fprintf(stderr, "cancel: job %u never completed\n", id);
        return 1;
    }
    const EngineJobCompletion& c = got.done.begin()->second;
    std::fprintf(stderr, "cancel: job %u ended %s (rendered=%.0fms, %zu outputs)\n", c.id, c.status.c_str(),
                 c.renderedMs, c.outputs.size());
    if (c.status != "cancelled") return 1;
    if (!c.outputs.empty()) return 1;
    if (!out.empty() && std::filesystem::exists(out)) {
        std::fprintf(stderr, "cancel: %s was left behind\n", out.c_str());
        return 1;
    }
    return 0;
}

// The spec's "out" for the cancel leg's file check; a plain scan, as the spec is the caller's own JSON.
std::string outOf(const std::string& spec) {
    const std::string key = "\"out\":\"";
    const std::size_t at = spec.find(key);
    if (at == std::string::npos) return {};
    const std::size_t from = at + key.size();
    return spec.substr(from, spec.find('"', from) - from);
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--parallel") == 0) {
        std::vector<std::string> jobs;
        for (int i = 2; i < argc; ++i) jobs.emplace_back(argv[i]);
        return runJobs(jobs);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--cancel") == 0) {
        return runCancel(argv[2], outOf(argv[2]));
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: retroplug-render-host-test <job-json> | --parallel <j>... | --cancel <j>\n");
        return 2;
    }
    return runJobs({argv[1]});
}

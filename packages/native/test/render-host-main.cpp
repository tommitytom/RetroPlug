// retroplug-render-host-test - a thin harness for the engine jobs that run renders (host/engine/OfflineJobs.hpp):
//
//   retroplug-render-host-test <job-json>          run ONE render as an engine job (its bytes are those of
//                                                   `retroplug-cli render`, which submits the same job; exits 0 on
//                                                   a "done" completion)
//   retroplug-render-host-test --parallel <j1> <j2>...   run N renders as engine jobs on one pool sized so all N run at
//                                                   once; assert every one is done and that min(N, the most a pool
//                                                   may run) ran side by side at some drain (exits 0)
//   retroplug-render-host-test --cancel <job-json>  submit a render, cancel it once it has rendered audio, assert it
//                                                   completes "cancelled" with no outputs and its files as they were
//                                                   before it ran (a `.part` already there included), no new one or
//                                                   `.part` left (exits 0)
//
// Not shipped; built by name for the render checks (and under the sanitizers, in build-tsan/ and build-asan/, for the
// pool's threads). The [engine-jobs] cases of retroplug-engine-graph-test are the automated guard.

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <rfl/json.hpp>

#include "host/engine/OfflineJobs.hpp"

namespace {

using rp::host::EngineJobCompletion;
using rp::host::EngineJobsDrain;
using rp::host::OfflineJobs;

// Every line goes to stderr, flushed as written, so the jobs' console lines and this tool's own status lines keep their
// order in a redirected log. Each is labelled with its job, as parallel jobs' lines interleave, and the lines a job's
// record dropped between two drains are counted where they would have been.
void echo(std::uint32_t id, const std::vector<rp::host::EngineJobLine>& lines, std::uint32_t dropped) {
    for (const auto& l : lines) std::fprintf(stderr, "[job %u] %s\n", id, l.text.c_str());
    if (dropped != 0) std::fprintf(stderr, "[job %u] [%u console lines dropped]\n", id, dropped);
    std::fflush(stderr);
}

struct Collected {
    std::map<std::uint32_t, EngineJobCompletion> done;
    std::size_t mostRunning = 0;  // the most jobs one drain reported running: its progress, the jobs running at it
};

// Drains the pool until `count` jobs have completed (or `timeout` passes), echoing the jobs' console lines. A drain's
// progress is every job running when it was taken, and only those: a completion is not counted as running, because the
// worker that published it may already have taken the next queued job, which that drain then reports running, so
// counting both would see two jobs at once from a pool that ran them one after the other. A render runs for seconds
// and a drain comes at least every 100 ms, so jobs that run side by side are seen doing so.
Collected collect(OfflineJobs& pool, std::size_t count, std::chrono::seconds timeout = std::chrono::seconds(900)) {
    Collected   out;
    const auto  end = std::chrono::steady_clock::now() + timeout;
    while (out.done.size() < count && std::chrono::steady_clock::now() < end) {
        EngineJobsDrain drain;
        pool.drain(std::chrono::milliseconds(100), nullptr, &drain);
        std::set<std::uint32_t> running;
        for (const auto& p : drain.progress) {
            running.insert(p.id);
            echo(p.id, p.lines, p.droppedLines);
        }
        for (auto& c : drain.completions) {
            echo(c.id, c.lines, c.droppedLines);
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

// Run the jobs on a pool whose engine budget covers all of them (or as many as a pool may run, kMaxWorkers - 1);
// returns 0 iff every one completes "done" and, with more than one, the pool ran that many side by side.
int runJobs(const std::vector<std::string>& specs) {
    // An engine budget is the pool's size less one, so N renders at once want N + 1 workers. The overlap expected is
    // fixed here rather than read back from the pool, so a pool sized short of it fails rather than expecting less.
    const std::size_t want = std::min<std::size_t>(specs.size(), OfflineJobs::kMaxWorkers - 1);
    OfflineJobs pool({}, static_cast<unsigned>(std::min<std::size_t>(specs.size() + 1, OfflineJobs::kMaxWorkers)));
    if (pool.engineBudget() < want) {
        std::fprintf(stderr, "render-host-test: the pool's engine budget is %u, short of the %zu jobs to run at once\n",
                     pool.engineBudget(), want);
        return 1;
    }
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
    if (rc == 0 && ids.size() > 1) {
        if (got.mostRunning < want) {
            std::fprintf(stderr, "render-host-test: at most %zu jobs ran at once, expected %zu\n", got.mostRunning,
                         want);
            rc = 1;
        } else {
            std::fprintf(stderr, "render-host-test: %zu jobs ran at once\n", got.mostRunning);
        }
    }
    return rc;
}

// What a spec names for its files, read with the JSON reader the backend links (fields it does not name are ignored).
struct SpecFiles {
    std::optional<std::string> out;
    std::optional<std::string> split;
};

// A file the render could write, as it was: its size and last write time.
struct FileState {
    std::uintmax_t                  size = 0;
    std::filesystem::file_time_type written{};
    bool operator==(const FileState&) const = default;
};
using Files = std::map<std::filesystem::path, FileState>;

// The files a render of `spec` could write, as they are now. Its `out`, a trailing .wav (in any case) trimmed as
// render.ts's outBase does, gives a stem: a mix spec writes `out`, or `<stem>_N.wav` under "onExists":"rename", and a
// split one `<stem>_<stream>.wav`, or `<stem>_N_<stream>.wav` under rename. Each is streamed to a `.part` of the
// render's own (`<file>.<token>.part`) before it is moved into place, so every `.part` of those names is listed too,
// the render's or one there before it ran. Nullopt when the spec names no readable `out` (it names none, the render
// then naming its file from the ROM or the song, which this tool does not predict, or it is not JSON the reader takes).
std::optional<Files> renderFiles(const std::string& spec) {
    namespace fs = std::filesystem;
    const auto files = rfl::json::read<SpecFiles>(spec);
    if (!files || !files->out || files->out->empty()) return std::nullopt;
    Files            found;
    const auto       note = [&found](const fs::path& p) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return;
        FileState st;
        st.size = fs::file_size(p, ec);
        st.written = fs::last_write_time(p, ec);
        found.emplace(p, st);
    };
    const std::string out = *files->out;
    const bool        mix = !files->split || *files->split == "mix";
    std::string       prefix = out;
    std::string ext = prefix.size() >= 4 ? prefix.substr(prefix.size() - 4) : std::string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
    if (ext == ".wav") prefix.resize(prefix.size() - 4);
    const fs::path    base(prefix);
    const fs::path    dir = base.has_parent_path() ? base.parent_path() : fs::path(".");
    const std::string stem = base.filename().string() + "_";
    const std::string own = fs::path(out).filename().string(); // a mix spec's file, its `.part` files `<own>.*.part`
    const auto        startsWith = [](const std::string& name, const std::string& head) {
        return name.size() > head.size() && name.compare(0, head.size(), head) == 0;
    };
    const auto endsWith = [](const std::string& name, const char* tail) {
        const std::size_t n = std::strlen(tail);
        return name.size() >= n && name.compare(name.size() - n, n, tail) == 0;
    };
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string name = e.path().filename().string();
        const bool        part = endsWith(name, ".part");
        if ((mix && (name == own || (startsWith(name, own + ".") && part))) ||
            (startsWith(name, stem) && (part || endsWith(name, ".wav"))))
            note(e.path());
    }
    return found;
}

// Submit one (long) job, cancel it once a drain shows it has rendered audio, and assert it completes "cancelled",
// mid-render (its rendered length above zero), with no outputs, and with the files its spec names as they were before
// it ran: none new, a leftover `.part` among them, and none changed or deleted, so an earlier export at the same paths
// stays, and so does a `.part` another render left there.
int runCancel(const std::string& spec) {
    const std::optional<Files> before = renderFiles(spec);
    if (!before) std::fprintf(stderr, "cancel: the spec names no readable \"out\", so no file check\n");
    OfflineJobs         pool;
    const std::uint32_t id = submit(pool, spec);
    if (id == 0) return 1;
    // Wait until it renders: past its boot, its settle and the presses that start playback, into its chunks.
    bool       rendering = false;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!rendering && std::chrono::steady_clock::now() < until) {
        EngineJobsDrain drain;
        pool.drain(std::chrono::milliseconds(100), nullptr, &drain);
        for (const auto& p : drain.progress) {
            echo(p.id, p.lines, p.droppedLines);
            if (p.id == id && p.renderedMs > 0) rendering = true;
        }
        for (const auto& c : drain.completions) {
            echo(c.id, c.lines, c.droppedLines);
            print(c);
            std::fprintf(stderr, "cancel: job %u finished before it was cancelled (a render too short to cancel)\n",
                         id);
            return 1;
        }
    }
    if (!rendering) {
        std::fprintf(stderr, "cancel: job %u rendered no audio within a minute\n", id);
        return 1;
    }
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
    if (c.status != "cancelled") {
        std::fprintf(stderr, "cancel: job %u ended %s, not cancelled: %s\n", id, c.status.c_str(), c.message.c_str());
        return 1;
    }
    if (!(c.renderedMs > 0)) {
        std::fprintf(stderr, "cancel: job %u reports no audio rendered, so the cancel did not land mid-render\n", id);
        return 1;
    }
    if (!c.outputs.empty()) {
        // A cancel taken after the body finished still ends the job cancelled, keeping its files (OfflineJobs.hpp).
        std::fprintf(stderr, "cancel: job %u kept %zu outputs: its render finished before the cancel landed\n", id,
                     c.outputs.size());
        return 1;
    }
    if (before) {
        int changed = 0;
        for (const auto& [path, now] : renderFiles(spec).value_or(Files{})) {
            const auto was = before->find(path);
            if (was == before->end()) {
                std::fprintf(stderr, "cancel: %s was left behind\n", path.string().c_str());
                ++changed;
            } else if (!(was->second == now)) {
                std::fprintf(stderr, "cancel: %s, there before the render, was changed\n", path.string().c_str());
                ++changed;
            }
        }
        for (const auto& entry : *before) {
            std::error_code ec;
            if (!std::filesystem::exists(entry.first, ec)) {
                std::fprintf(stderr, "cancel: %s, there before the render, was deleted\n",
                             entry.first.string().c_str());
                ++changed;
            }
        }
        if (changed != 0) return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::strcmp(argv[1], "--parallel") == 0) {
        std::vector<std::string> jobs;
        for (int i = 2; i < argc; ++i) jobs.emplace_back(argv[i]);
        return runJobs(jobs);
    }
    if (argc >= 3 && std::strcmp(argv[1], "--cancel") == 0) {
        return runCancel(argv[2]);
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: retroplug-render-host-test <job-json> | --parallel <j>... | --cancel <j>\n");
        return 2;
    }
    return runJobs({argv[1]});
}

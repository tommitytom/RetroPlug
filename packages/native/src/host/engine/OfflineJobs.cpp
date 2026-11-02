#include "host/engine/OfflineJobs.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

#include "graph/GraphDocument.hpp"
#include "graph/GraphHost.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "graph/nodes/DspResampleNodes.hpp"
#include "host/engine/DefaultFloatEnvironment.hpp"
#include "host/render/RenderHost.hpp"
#include "kit/SampleCache.hpp"

namespace rp::host {

namespace {

bool positiveRate(double rate) { return rate > 0.0 && std::isfinite(rate); }

// The planner's errors as one line, each "node.port: code: message" as the engine's refusals read.
std::string describe(const std::vector<graph::PlanError>& errors) {
    std::string s;
    for (const auto& e : errors) {
        if (!s.empty()) s += "; ";
        if (!e.nodeId.empty()) s += e.nodeId + (e.port.empty() ? "" : "." + e.port) + ": ";
        s += e.code + ": " + e.message;
    }
    return s;
}

// Why createAudioBuffer would refuse this shape, or nullopt.
std::optional<std::string> bufferRefusal(std::uint64_t channels, std::uint64_t frames, double rate) {
    if (channels == 0 || frames == 0) return std::string("needs at least one channel and one frame");
    if (!positiveRate(rate)) return std::string("needs a sample rate that is a positive number");
    if (channels * frames > graph::ResourceTable::kMaxAudioSamples)
        return "is larger than " + std::to_string(graph::ResourceTable::kMaxAudioSamples) + " samples";
    return std::nullopt;
}

// A handle that names nothing: an input of no frames takes its place in the table's count, so the handles after it
// stay where the submission put them, and is freed at once (a byte resource no pin holds goes with its owner's drop).
graph::ResourceTable::Id reservePlaceholder(graph::ResourceTable& table) {
    const auto id = table.createBytes("bytes", {});
    table.drop(id);
    return id;
}

// The cap's frames at the decoded rate, as the two kit windows spell it; nullopt when that is not a frame count (a
// non-finite result, or one past size_t's range), which a job refuses rather than casting.
std::optional<std::size_t> capFrames(std::uint32_t decodedRate, const OfflineCap& cap) {
    double scaled = 0.0;
    if (cap.arithmetic == OfflineCapArithmetic::FloatRatio) {
        const float ratio = static_cast<float>(decodedRate) / static_cast<float>(cap.rate);
        scaled = static_cast<double>(std::ceil(static_cast<float>(cap.frames) * ratio));
    } else {
        scaled = std::ceil(static_cast<double>(cap.frames) * static_cast<double>(decodedRate) / cap.rate);
    }
    if (!std::isfinite(scaled) || scaled < 0.0) return std::nullopt;
    const double max = static_cast<double>(std::numeric_limits<std::size_t>::max());
    if (scaled > max) return std::nullopt;
    // A 64-bit size_t's max is 2^64 - 1, which a double rounds up to 2^64: a scaled value at that magnitude is past
    // the range even though it compares equal.
    if constexpr (sizeof(std::size_t) >= sizeof(double))
        if (scaled == max) return std::nullopt;
    return static_cast<std::size_t>(scaled);
}

// The guard report a job's host offers its nodes (rp.host.guards/1): how many guards fired, and the first one's reason.
// A node whose guard replaces what its kernel would have computed reports it, and the completion carries the count
// and the reason on an ok result, as each returned buffer carries whether it holds a non-finite sample: native
// reports, the caller decides (a kit compile refuses the pass; a render can accept a recovering filter). Nodes report
// on the block thread, which for a job is its worker; the counters are atomic so that stays true of any driver.
struct GuardReport {
    std::atomic<std::uint32_t> count{0};
    std::atomic<const char*>   first{nullptr};
    rp_host_guards             service{this, &GuardReport::fired};

    static void fired(void* ctx, const char* reason) {
        auto* const self = static_cast<GuardReport*>(ctx);
        const char* none = nullptr;
        self->first.compare_exchange_strong(none, reason, std::memory_order_relaxed);
        self->count.fetch_add(1, std::memory_order_relaxed);
    }

    static const void* interfaces(void* ctx, const char* id) {
        return std::strcmp(id, RP_HOST_GUARDS_INTERFACE) == 0 ? &static_cast<GuardReport*>(ctx)->service : nullptr;
    }
};

}  // namespace

std::optional<OfflineWindow> offlineWindow(std::size_t decodedFrames, std::uint32_t decodedRate,
                                           std::uint32_t offset, std::uint32_t length,
                                           const std::optional<OfflineCap>& cap) {
    // Each step as the two kit windows spell it, the casts included: the cap is the one place their arithmetic differs,
    // and a different rounding there is a different frame count (test/engine-graph/OfflineJobs.test.cpp's refs).
    std::size_t maxFrames = decodedFrames;
    if (cap) {
        const auto frames = capFrames(decodedRate, *cap);
        if (!frames) return std::nullopt;
        maxFrames = *frames;
    }
    const std::size_t length0 = length == 0 ? decodedFrames : length;
    const std::size_t start = std::min<std::size_t>(offset, decodedFrames);
    return OfflineWindow{start, std::min(maxFrames, std::min(length0, decodedFrames - start))};
}

OfflineJobs::OfflineJobs(std::span<const rp_package* const> extra, unsigned size, EngineJobBody body)
    : size_(size == 0 ? concurrency() : std::min(size, kMaxWorkers)), body_(std::move(body)) {
    // Room for every worker up front, so starting one never grows the vector under a submission.
    workers_.reserve(maxWorkers());
    const auto add = [this](const rp_package* p) {
        std::string why;
        if (!registry_.addPackage(p, &why))
            std::fprintf(stderr, "[offline] a node package was refused (%s); its types are not registered\n",
                         why.c_str());
    };
    for (const rp_package* p : {graph::corePackage(), graph::dspPackage(), graph::dspResamplePackage()}) add(p);
    for (const rp_package* p : extra) add(p);
}

OfflineJobs::~OfflineJobs() {
    std::vector<std::thread> workers;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        queue_.clear();  // unrun: nothing of theirs was built
        for (Job* job : running_) job->cancelled.store(true, std::memory_order_release);
        workers.swap(workers_);
    }
    work_.notify_all();
    finished_.notify_all();
    for (std::thread& t : workers) t.join();
}

unsigned OfflineJobs::concurrency() {
    return std::clamp(std::thread::hardware_concurrency(), 1u, kMaxWorkers);
}

OfflineSubmit OfflineJobs::submit(OfflineRequest request) {
    const auto refuse = [](std::string why) { return OfflineSubmit{0, std::move(why)}; };
    if (auto why = graph::checkConfig(request.rate, kBlockFrames)) return refuse("the job's " + *why);
    const std::size_t handles = request.inputs.size() + request.outputs.size();
    if (handles > kMaxResources)
        return refuse("a job takes at most " + std::to_string(kMaxResources) + " inputs and outputs, not " +
                      std::to_string(handles));
    for (std::size_t i = 0; i < request.inputs.size(); ++i) {
        const std::string name = "input " + std::to_string(i);
        if (const auto* s = std::get_if<OfflineSamples>(&request.inputs[i])) {
            if (s->channels == 0) return refuse(name + " has no channels");
            if (s->samples.size() % s->channels != 0)
                return refuse(name + " holds " + std::to_string(s->samples.size()) +
                              " samples, not a whole number of " + std::to_string(s->channels) + "-channel frames");
            if (!positiveRate(s->rate)) return refuse(name + " needs a sample rate that is a positive number");
            if (s->samples.size() > graph::ResourceTable::kMaxAudioSamples)
                return refuse(name + " is larger than " + std::to_string(graph::ResourceTable::kMaxAudioSamples) +
                              " samples");
        } else {
            const auto& f = std::get<OfflineFile>(request.inputs[i]);
            if (f.path.empty()) return refuse(name + " names no file");
            if (f.cap && !positiveRate(f.cap->rate))
                return refuse(name + "'s cap needs a rate that is a positive number");
        }
    }
    for (std::size_t j = 0; j < request.outputs.size(); ++j) {
        const auto& o = request.outputs[j];
        if (auto why = bufferRefusal(o.channels, o.frames, o.rate))
            return refuse("output " + std::to_string(j) + " " + *why);
    }
    for (const std::uint32_t h : request.returns)
        if (h == 0 || h > handles)
            return refuse("a return names res:" + std::to_string(h) + ", which is no input or output of the job");

    auto job = std::make_unique<Job>();
    job->kind = Kind::Graph;
    job->request = std::move(request);
    return enqueue(std::move(job));
}

OfflineSubmit OfflineJobs::submitEngine(EngineJobRequest request) {
    // The kinds are the embedded bundles a job can run; D3 embeds one.
    if (request.kind != "render")
        return OfflineSubmit{0, "unknown engine job kind \"" + request.kind + "\": the one kind is \"render\""};
    auto job = std::make_unique<Job>();
    job->kind = Kind::Engine;
    job->engine = std::move(request);
    return enqueue(std::move(job));
}

bool OfflineJobs::runnable(const Job& job) const {
    return job.kind == Kind::Graph ? runningGraph_ < size_ : runningEngine_ < engineBudget();
}

std::size_t OfflineJobs::runnableQueued() const {
    std::size_t graph = 0, engine = 0;
    for (const auto& job : queue_) (job->kind == Kind::Graph ? graph : engine)++;
    return std::min<std::size_t>(graph, size_ - runningGraph_) +
           std::min<std::size_t>(engine, engineBudget() - runningEngine_);
}

OfflineSubmit OfflineJobs::enqueue(std::unique_ptr<Job> job) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return {0, "the engine is going"};
    if (outstanding_ >= kMaxJobs) return {0, "busy: " + std::to_string(kMaxJobs) + " offline jobs are outstanding"};
    job->id = nextId_;
    queue_.push_back(std::move(job));
    // One more worker when the jobs that could start now outnumber the workers not running one (a parked worker a
    // notify has woken stays counted until it claims a job, and a worker just started counts from its start, so the
    // jobs queued since have claimed them), while there is room; made here, under the lock it starts by taking. A job
    // its kind's limit holds back starts no worker: a worker that finishes a job of that kind takes it.
    if (runnableQueued() > idle_ && workers_.size() < maxWorkers()) {
        // A thread that cannot be started (std::system_error, or std::bad_alloc for its state) leaves the job queued
        // for the workers there are; with none, it is refused and leaves nothing behind, its id not taken.
        bool        failed = false;
        std::string why;
        try {
            workers_.emplace_back([this] { workerLoop(); });
            ++idle_;
        } catch (const std::exception& e) {
            failed = true;
            why = e.what();
        } catch (...) {
            failed = true;
            why = "an unknown error";
        }
        if (failed && workers_.empty()) {
            queue_.pop_back();
            return {0, "no worker thread could be started: " + why};
        }
    }
    const std::uint32_t id = nextId_++;
    ++outstanding_;
    work_.notify_one();
    return {id, {}};
}

std::vector<OfflineCompletion> OfflineJobs::drain(std::chrono::milliseconds wait, std::size_t* inFlight,
                                                  EngineJobsDrain* engine) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto ready = [&] { return !completions_.empty() || (engine && !engineCompletions_.empty()); };
    if (!ready() && wait.count() > 0) {
        finished_.wait_for(lock, std::min(wait, kMaxDrainWait), [&] {
            return stopping_ || ready() || (queue_.empty() && running_.empty());
        });
    }
    std::vector<OfflineCompletion> out;
    out.reserve(completions_.size());
    for (auto& c : completions_) out.push_back(std::move(c));
    outstanding_ -= completions_.size();
    completions_.clear();
    if (engine) {
        for (auto& c : engineCompletions_) engine->completions.push_back(std::move(c));
        outstanding_ -= engineCompletions_.size();
        engineCompletions_.clear();
        // Every running engine job's progress, its lines since the drain before taken from its record. A job in
        // running_ is alive while this lock is held: its worker takes the lock to take it out before it frees it.
        for (Job* job : running_) {
            if (job->kind != Kind::Engine) continue;
            EngineJobProgress p;
            p.id = job->id;
            job->record.take(p);
            engine->progress.push_back(std::move(p));
        }
    }
    if (inFlight) *inFlight = queue_.size() + running_.size();
    return out;
}

bool OfflineJobs::cancel(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if ((*it)->id != id) continue;
        std::unique_ptr<Job> job = std::move(*it);
        queue_.erase(it);
        if (job->kind == Kind::Engine) {
            engineCompletions_.push_back(engineCompletion(*job, "cancelled"));
        } else {
            OfflineCompletion c;
            c.id = id;
            c.error = "cancelled";
            completions_.push_back(std::move(c));
        }
        finished_.notify_all();
        // A queued job holds no worker, so cancelling it frees none for another job to take.
        return true;
    }
    for (Job* job : running_) {
        if (job->id != id) continue;
        job->cancelled.store(true, std::memory_order_release);
        return true;
    }
    return false;
}

std::size_t OfflineJobs::outstanding() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return outstanding_;
}

std::size_t OfflineJobs::workers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return workers_.size();
}

void OfflineJobs::workerLoop() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        // Counted idle by whoever made this worker so: the submission that started it, or the end of its last job.
        auto next = queue_.end();
        work_.wait(lock, [&] {
            if (stopping_) return true;
            next = std::find_if(queue_.begin(), queue_.end(), [&](const auto& j) { return runnable(*j); });
            return next != queue_.end();
        });
        if (stopping_) return;
        --idle_;
        std::unique_ptr<Job> job = std::move(*next);
        queue_.erase(next);
        running_.push_back(job.get());
        (job->kind == Kind::Graph ? runningGraph_ : runningEngine_)++;
        lock.unlock();
        OfflineCompletion   done;
        EngineJobCompletion engineDone;
        if (job->kind == Kind::Graph) done = run(*job);
        else engineDone = runEngine(*job);
        lock.lock();
        running_.erase(std::find(running_.begin(), running_.end(), job.get()));
        (job->kind == Kind::Graph ? runningGraph_ : runningEngine_)--;
        ++idle_;
        // A cancel taken while the job ran wins, even over a run that got to its end first: cancel() answered true,
        // and the caller holds that answer as the job's outcome. A render that finished before its cancel was seen
        // keeps the files it wrote in its outputs: they are whole, and on disk either way.
        const bool cancelled = job->cancelled.load(std::memory_order_acquire);
        if (stopping_) return;  // the pool is going, and its completions with it
        if (job->kind == Kind::Graph) {
            if (cancelled) {
                done.ok = false;
                done.error = "cancelled";
                done.buffers.clear();
            }
            completions_.push_back(std::move(done));
        } else {
            EngineJobCompletion c = engineCompletion(*job, cancelled ? "cancelled" : std::move(engineDone.status));
            if (!cancelled) c.message = std::move(engineDone.message);
            c.outputs = std::move(engineDone.outputs);
            engineCompletions_.push_back(std::move(c));
        }
        finished_.notify_all();
        // The job's slot is free: a job its kind's limit held back may start now, which this worker takes as it loops
        // if nothing older is runnable; the others re-check in case that leaves one for them.
        if (!queue_.empty()) work_.notify_all();
    }
}

EngineJobCompletion OfflineJobs::engineCompletion(Job& job, std::string status) {
    EngineJobCompletion c;
    c.id = job.id;
    c.status = std::move(status);
    job.record.take(c);
    return c;
}

void EngineJobRecord::rendered(double ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    renderedMs_ = ms;
}

void EngineJobRecord::write(EngineJobStream stream, std::string_view text) {
    if (!text.empty() && text.back() == '\n') text.remove_suffix(1);  // the console ends every write with one
    if (text.size() > kMaxBytes) {
        // Cut at a UTF-8 boundary: back off any continuation bytes the cut would split from their lead byte.
        std::size_t cut = kMaxBytes;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        text = text.substr(0, cut);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (lines_.size() >= kMaxLines || bytes_ + text.size() > kMaxBytes) {
        ++dropped_;
        return;
    }
    bytes_ += text.size();
    lines_.push_back({stream, std::string(text)});
}

EngineJobCompletion OfflineJobs::runEngine(Job& job) {
    EngineJobCompletion out;
    const auto cancelled = [&job] { return job.cancelled.load(std::memory_order_acquire); };
    // The whole run in the default mode, as a graph job's: a render's arithmetic is then the CLI's, whatever the
    // submitting thread's mode (a worker inherits it under glibc).
    const DefaultFloatEnvironment floatEnvironment;
    if (cancelled()) {
        out.status = "cancelled";
        return out;
    }
    try {
        const EngineJobHooks hooks{
            [&job](double ms) { job.record.rendered(ms); },
            cancelled,
            [&job](bool error, const char* text) {
                job.record.write(error ? EngineJobStream::Err : EngineJobStream::Out, text);
            },
        };
        EngineJobOutcome r = body_ ? body_(job.engine, hooks) : renderBody(job.engine, hooks);
        out.status = std::move(r.status);
        out.message = std::move(r.message);
        out.outputs = std::move(r.outputs);
    } catch (const std::exception& e) {
        out.status = "error";
        out.message = std::string("the engine job failed: ") + e.what();
    } catch (...) {
        out.status = "error";
        out.message = "the engine job failed";
    }
    return out;
}

EngineJobOutcome OfflineJobs::renderBody(const EngineJobRequest& request, const EngineJobHooks& hooks) {
    // Built, run and destroyed on this thread: the runtime anchors its stack check to the thread that makes it, and
    // the job's Engine, its cores and its plan's nodes go before this worker takes another job. In continuous blocks:
    // the engine runs whole blocks whatever each of the render's calls asks for (docs/modular-graph-phase-d3-design.md
    // section 4).
    retroplug::RenderHost         host;
    retroplug::RenderHost::Result r = host.run(request.spec, hooks.rendered, hooks.cancelled, hooks.console,
                                               retroplug::RenderHost::Blocks::Continuous);
    return {std::move(r.status), std::move(r.message), std::move(r.outputs)};
}

OfflineCompletion OfflineJobs::run(Job& job) {
    OfflineCompletion out;
    out.id = job.id;
    const auto fail = [&out](std::string why) {
        out.ok = false;
        out.error = std::move(why);
        out.buffers.clear();
        return std::move(out);
    };
    const auto cancelled = [&job] { return job.cancelled.load(std::memory_order_acquire); };
    const DefaultFloatEnvironment floatEnvironment;  // the whole run, decode to copy-out, in the default mode
    OfflineRequest& req = job.request;
    try {
        if (cancelled()) return fail("cancelled");
        // Declared before the host, which pins what its plan binds: the host goes first, at the end of its scope.
        graph::ResourceTable resources;
        struct Slot {
            bool          empty = false;  // an input of no frames: its handle names nothing
            std::uint32_t channels = 1;
            double        rate = 0.0;
        };
        std::vector<Slot> slots;  // by handle - 1
        std::string       why;
        for (std::size_t i = 0; i < req.inputs.size(); ++i) {
            if (cancelled()) return fail("cancelled");  // between decodes, so a many-file job stops at the next one
            const std::string name = "input " + std::to_string(i);
            graph::ResourceTable::Id id = 0;
            if (auto* s = std::get_if<OfflineSamples>(&req.inputs[i])) {
                const auto frames = static_cast<std::uint32_t>(s->samples.size() / s->channels);
                if (frames == 0) {
                    id = reservePlaceholder(resources);
                    slots.push_back({true, s->channels, s->rate});
                } else {
                    id = resources.createAudioBuffer(s->channels, frames, s->rate, &why);
                    if (id == 0) return fail(name + ": " + why);
                    auto* b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
                    for (std::uint32_t f = 0; f < frames; ++f)
                        for (std::uint32_t c = 0; c < s->channels; ++c)
                            b->data[c][f] = s->samples[std::size_t(f) * s->channels + c];
                    slots.push_back({false, s->channels, s->rate});
                }
                // The table holds the samples now: the request's copy goes, rather than stay resident for the run.
                std::vector<float>().swap(s->samples);
            } else {
                const auto& f = std::get<OfflineFile>(req.inputs[i]);
                // A cache of the job's own, so a file edited between two jobs decodes as it is now; the decode is
                // SampleCache's, so the samples are what every kit compile reads from this path.
                kit::SampleCache cache;
                const kit::SampleData* d = cache.getOrLoad(f.path);
                if (!d) return fail(name + ": " + f.path + " could not be read or decoded");
                const auto maybeWindow = offlineWindow(d->buffer.size(), d->sampleRate, f.offset, f.length, f.cap);
                if (!maybeWindow)
                    return fail(name + ": the cap's frames do not scale to a frame count at the file's " +
                                std::to_string(d->sampleRate) + " Hz");
                const OfflineWindow w = *maybeWindow;
                OfflineInputReport report;
                report.handle = static_cast<std::uint32_t>(i + 1);
                report.decodedRate = d->sampleRate;
                report.decodedFrames = static_cast<std::uint32_t>(d->buffer.size());
                report.frames = static_cast<std::uint32_t>(w.frames);
                const float* from = d->buffer.data() + w.start;
                report.nonFinite = std::any_of(from, from + w.frames, [](float v) { return !std::isfinite(v); });
                out.inputs.push_back(report);
                if (w.frames == 0) {
                    id = reservePlaceholder(resources);
                    slots.push_back({true, 1, static_cast<double>(d->sampleRate)});
                } else {
                    id = resources.createAudioBuffer(1, report.frames, d->sampleRate, &why);
                    if (id == 0) return fail(name + ": " + why);
                    auto* b = static_cast<rp_audio_buffer*>(resources.get(id)->payload);
                    std::copy(from, from + w.frames, b->data[0]);
                    slots.push_back({false, 1, static_cast<double>(d->sampleRate)});
                }
            }
            if (id != i + 1) return fail(name + " did not get its handle, res:" + std::to_string(i + 1));
        }
        for (std::size_t j = 0; j < req.outputs.size(); ++j) {
            const auto& o = req.outputs[j];
            const auto id = resources.createAudioBuffer(o.channels, o.frames, o.rate, &why);
            if (id == 0) return fail("output " + std::to_string(j) + ": " + why);
            if (id != req.inputs.size() + j + 1)
                return fail("output " + std::to_string(j) + " did not get its handle, res:" +
                            std::to_string(req.inputs.size() + j + 1));
            slots.push_back({false, o.channels, o.rate});
        }

        if (!req.document.empty()) {
            if (cancelled()) return fail("cancelled");
            auto doc = graph::parseGraphDocument(req.document);
            if (!doc) return fail(std::string("not a graph document: ") + doc.error().what());
            GuardReport       guards;  // before the host, whose nodes report into it until they go with it
            graph::HostConfig config;
            config.sampleRate = req.rate;
            config.maxFrames = kBlockFrames;
            config.interfaces = &GuardReport::interfaces;
            config.interfacesCtx = &guards;
            graph::GraphHost host(registry_, resources, config);
            const graph::SetGraphResult planned = host.setGraph(*doc);
            if (!planned.ok) return fail("the document was refused: " + describe(planned.errors));
            const rp_transport transport{req.rate, 120.0, 0.0, 0};
            for (std::uint32_t done = 0; done < req.frames;) {
                if (cancelled()) return fail("cancelled");
                const std::uint32_t n = std::min(kBlockFrames, req.frames - done);
                host.processBlock(n, transport);
                done += n;
            }
            // A faulted node is bypassed from its fault on, so what was recorded is not what the document asked for.
            if (host.faultCount() != 0) {
                std::string faulted;
                for (const auto& n : doc->nodes)
                    if (host.isFaulted(n.id)) faulted += (faulted.empty() ? "" : ", ") + n.id;
                return fail("a node faulted: " + faulted);
            }
            // Copied while the nodes that own the reason are still alive: the host and they go at this scope's end.
            out.guardsFired = guards.count.load(std::memory_order_relaxed);
            if (out.guardsFired != 0) out.guardReason = guards.first.load(std::memory_order_relaxed);
        }  // the host goes here, on this thread, and every node it built with it

        for (const std::uint32_t h : req.returns) {
            const Slot& slot = slots[h - 1];
            OfflineBuffer buffer;
            buffer.handle = h;
            buffer.channels = slot.channels;
            buffer.rate = slot.rate;
            if (!slot.empty) {
                const auto* b = static_cast<const rp_audio_buffer*>(resources.get(h)->payload);
                buffer.frames = b->frames;
                buffer.samples.resize(std::size_t(b->channels) * b->frames);
                for (std::uint32_t f = 0; f < b->frames; ++f)
                    for (std::uint32_t c = 0; c < b->channels; ++c)
                        buffer.samples[std::size_t(f) * b->channels + c] = b->data[c][f];
                buffer.nonFinite = std::any_of(buffer.samples.begin(), buffer.samples.end(),
                                               [](float v) { return !std::isfinite(v); });
            }
            out.buffers.push_back(std::move(buffer));
        }
        out.ok = true;
        return out;
    } catch (const std::exception& e) {
        return fail(std::string("the job failed: ") + e.what());
    } catch (...) {
        return fail("the job failed");
    }
}

}  // namespace rp::host

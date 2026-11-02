#include "host/engine/OfflineJobs.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cfenv>
#include <exception>
#include <string_view>
#include <system_error>
#include <utility>

#include "graph/GraphDocument.hpp"
#include "graph/GraphHost.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "graph/nodes/DspNodes.hpp"
#include "graph/nodes/DspResampleNodes.hpp"
#include "kit/SampleCache.hpp"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define RP_OFFLINE_FP_MXCSR 1
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
#define RP_OFFLINE_FP_FPCR 1
#endif

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

// The default floating-point environment, held for a job's run and the caller's put back after: round to nearest, no
// exception trapping, and subnormals kept (no flush-to-zero, no denormals-are-zero). A worker thread starts with the
// environment of the thread that submitted the job which started it, and an audio host commonly sets flush-to-zero on
// its threads; a job's floats are a kit's bytes, which the kernels compute in the default mode (a biquad's and the
// resampler's tails pass through subnormals, and a normalise over a quiet source divides by one), so a job must not
// compute them in whatever mode its submitter happened to run in. fesetenv(FE_DFL_ENV) gives the standard part; the
// flush bits are set explicitly as well, since not every C library's default environment clears them (x86's MXCSR
// FTZ and DAZ, AArch64's FPCR FZ and FZ16).
class DefaultFloatEnvironment {
public:
    DefaultFloatEnvironment() {
        std::fegetenv(&saved_);
        std::fesetenv(FE_DFL_ENV);
#if defined(RP_OFFLINE_FP_MXCSR)
        savedMxcsr_ = _mm_getcsr();
        _mm_setcsr(savedMxcsr_ & ~(kFtz | kDaz));
#elif defined(RP_OFFLINE_FP_FPCR)
        asm volatile("mrs %0, fpcr" : "=r"(savedFpcr_));
        const std::uint64_t fpcr = savedFpcr_ & ~(kFz | kFz16);
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
    }
    ~DefaultFloatEnvironment() {
        std::fesetenv(&saved_);
#if defined(RP_OFFLINE_FP_MXCSR)
        _mm_setcsr(savedMxcsr_);
#elif defined(RP_OFFLINE_FP_FPCR)
        asm volatile("msr fpcr, %0" : : "r"(savedFpcr_));
#endif
    }
    DefaultFloatEnvironment(const DefaultFloatEnvironment&) = delete;
    DefaultFloatEnvironment& operator=(const DefaultFloatEnvironment&) = delete;

private:
    std::fenv_t saved_{};
#if defined(RP_OFFLINE_FP_MXCSR)
    static constexpr unsigned kFtz = 0x8000u;  // MXCSR bit 15, flush-to-zero
    static constexpr unsigned kDaz = 0x0040u;  // MXCSR bit 6, denormals-are-zero
    unsigned savedMxcsr_ = 0;
#elif defined(RP_OFFLINE_FP_FPCR)
    static constexpr std::uint64_t kFz = std::uint64_t(1) << 24;    // FPCR.FZ
    static constexpr std::uint64_t kFz16 = std::uint64_t(1) << 19;  // FPCR.FZ16
    std::uint64_t savedFpcr_ = 0;
#endif
};

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

OfflineWindow offlineWindow(std::size_t decodedFrames, std::uint32_t decodedRate, std::uint32_t offset,
                            std::uint32_t length, const std::optional<OfflineCap>& cap) {
    // Each step as the kit windows spell it (KitUtil::compileSample, RisaDmcCodec::encode), the casts included: the cap
    // is the one place their arithmetic differs, and a different rounding there is a different frame count.
    std::size_t maxFrames = decodedFrames;
    if (cap) {
        if (cap->arithmetic == OfflineCapArithmetic::FloatRatio) {
            const float ratio = static_cast<float>(decodedRate) / static_cast<float>(cap->rate);
            maxFrames = static_cast<std::size_t>(std::ceil(static_cast<float>(cap->frames) * ratio));
        } else {
            maxFrames = static_cast<std::size_t>(
                std::ceil(static_cast<double>(cap->frames) * static_cast<double>(decodedRate) / cap->rate));
        }
    }
    const std::size_t length0 = length == 0 ? decodedFrames : length;
    const std::size_t start = std::min<std::size_t>(offset, decodedFrames);
    return {start, std::min(maxFrames, std::min(length0, decodedFrames - start))};
}

OfflineJobs::OfflineJobs(std::span<const rp_package* const> extra) {
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

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return refuse("the engine is going");
    if (outstanding_ >= kMaxJobs)
        return refuse("busy: " + std::to_string(kMaxJobs) + " offline jobs are outstanding");
    // One more worker unless more are parked than jobs already wait for one (a parked worker a notify has woken stays
    // counted until it takes the lock, so the jobs queued since have claimed it), while there is room; made here, under
    // the lock it starts by taking.
    if (idle_ <= queue_.size() && workers_.size() < concurrency()) {
        try {
            workers_.emplace_back([this] { workerLoop(); });
        } catch (const std::system_error& e) {
            if (workers_.empty()) return refuse(std::string("no worker thread could be started: ") + e.what());
        }
    }
    auto job = std::make_unique<Job>();
    job->id = nextId_++;
    job->request = std::move(request);
    const std::uint32_t id = job->id;
    queue_.push_back(std::move(job));
    ++outstanding_;
    work_.notify_one();
    return {id, {}};
}

std::vector<OfflineCompletion> OfflineJobs::drain(std::chrono::milliseconds wait, std::size_t* inFlight) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (completions_.empty() && wait.count() > 0) {
        finished_.wait_for(lock, std::min(wait, kMaxDrainWait), [this] {
            return stopping_ || !completions_.empty() || outstanding_ == 0;
        });
    }
    std::vector<OfflineCompletion> out;
    out.reserve(completions_.size());
    for (auto& c : completions_) out.push_back(std::move(c));
    outstanding_ -= completions_.size();
    completions_.clear();
    if (inFlight) *inFlight = outstanding_;  // what is left is queued or running: every completion was just taken
    return out;
}

bool OfflineJobs::cancel(std::uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if ((*it)->id != id) continue;
        queue_.erase(it);
        OfflineCompletion c;
        c.id = id;
        c.error = "cancelled";
        completions_.push_back(std::move(c));
        finished_.notify_all();
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
        ++idle_;
        work_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        --idle_;
        if (stopping_) return;
        std::unique_ptr<Job> job = std::move(queue_.front());
        queue_.pop_front();
        running_.push_back(job.get());
        lock.unlock();
        OfflineCompletion done = run(*job);
        lock.lock();
        running_.erase(std::find(running_.begin(), running_.end(), job.get()));
        // A cancel taken while the job ran wins, even over a run that got to its end first: cancel() answered true,
        // and the caller holds that answer as the job's outcome.
        if (job->cancelled.load(std::memory_order_acquire)) {
            done.ok = false;
            done.error = "cancelled";
            done.buffers.clear();
        }
        if (stopping_) return;  // the pool is going, and its completions with it
        completions_.push_back(std::move(done));
        finished_.notify_all();
    }
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
                // SampleCache's, the kit compiler's, so the samples cannot drift from what it decodes.
                kit::SampleCache cache;
                const kit::SampleData* d = cache.getOrLoad(f.path);
                if (!d) return fail(name + ": " + f.path + " could not be read or decoded");
                const OfflineWindow w = offlineWindow(d->buffer.size(), d->sampleRate, f.offset, f.length, f.cap);
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
    }
}

}  // namespace rp::host

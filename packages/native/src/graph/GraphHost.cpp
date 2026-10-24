#include "graph/GraphHost.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <stdexcept>
#include <thread>

#include "graph/CanonicalJson.hpp"
#include "graph/Executor.hpp"
#include "graph/Queue.hpp"

namespace rp::graph {

namespace {

// The ABI's free has no alignment argument, so host allocations are capped at max_align_t. Through
// operator new rather than malloc, so whatever replaces operator new for the process (an allocator, or the
// no-heap test's hook) sees a node's host allocations too.
void* hostAlloc(void*, std::size_t size, std::size_t align) {
    return align <= alignof(std::max_align_t) ? ::operator new(size ? size : 1, std::nothrow) : nullptr;
}

void hostFree(void*, void* ptr) { ::operator delete(ptr); }

void hostLog(void*, int, const char* message) { std::fprintf(stderr, "[graph] %s\n", message); }

constexpr const char* kAudioOut = "rp.host.audio-out";

}  // namespace

// GraphHost's own services first, then the embedder's.
const void* GraphHost::hostInterface(const rp_host* host, const char* id) {
    static constexpr rp_host_audio_buffers kAudioBuffers{&GraphHost::audioBuffers};
    if (std::strcmp(id, RP_HOST_AUDIO_BUFFERS_INTERFACE) == 0) return &kAudioBuffers;
    const auto* self = static_cast<const GraphHost*>(host->ctx);
    return self->config_.interfaces ? self->config_.interfaces(self->config_.interfacesCtx, id) : nullptr;
}

float* const* GraphHost::audioBuffers(const rp_host* host, std::uint32_t* count) {
    const auto* self = static_cast<const GraphHost*>(host->ctx);
    *count = self->hostOutputCount_;
    return self->hostOutputs_;
}

std::optional<std::string> checkConfig(double sampleRate, std::uint32_t maxFrames) {
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate)) return "the sample rate must be a positive number";
    if (maxFrames < 1 || maxFrames > kMaxBlockFrames)
        return "the block size must be 1 to " + std::to_string(kMaxBlockFrames) + " frames, not " +
               std::to_string(maxFrames);
    return std::nullopt;
}

GraphHost::GraphHost(const NodeRegistry& registry, ResourceTable& resources, HostConfig config)
    : registry_(registry), resources_(resources), config_(config) {
    if (auto why = checkConfig(config.sampleRate, config.maxFrames)) throw std::invalid_argument(*why);
    host_ = rp_host{RP_ABI_VERSION, this, &hostAlloc, &hostFree, &hostLog, &GraphHost::hostInterface};
}

GraphHost::~GraphHost() {
    assert(!audioThreadOwns() && "destroy a GraphHost only after its audio thread has stopped");
    // The audio thread has stopped (setAudioThreadOwns(false) is the caller's job): adopt anything still
    // queued so every plan is in exactly one place, then free them all here, on the control thread. Host
    // commands still queued, trailers included, are discarded, not applied: the embedder may be part way
    // through destroying itself.
    drainCommands(true, false);
    reclaim();
    delete current_;
    current_ = nullptr;
    live_.clear();
    feedback_.clear();
}

bool GraphHost::push(const Command& c) {
    if (!commands_.tryPush(c)) return false;
    flushIfQuiescent();
    return true;
}

void GraphHost::flushIfQuiescent() {
    if (audioThreadOwns()) return;
    drain();
    reclaim();
}

void GraphHost::setAudioThreadOwns(bool owns) {
    audioOwns_.store(owns, std::memory_order_release);
    if (!owns) flushIfQuiescent();
}

bool GraphHost::waitForCommits() {
    if (!audioThreadOwns()) return true;  // inline flush already adopted it
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.adoptionTimeoutMs);
    while (commitsInFlight_.load(std::memory_order_acquire) > 0) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

SetGraphResult GraphHost::setGraph(std::string_view documentJson) {
    auto doc = parseGraphDocument(documentJson);
    if (!doc) {
        SetGraphResult r;
        r.errors.push_back({"", "", "document", std::string("not a graph document: ") + doc.error().what()});
        return r;
    }
    return setGraph(*doc);
}

SetGraphResult GraphHost::setGraph(const GraphDocument& document, std::shared_ptr<const void> hostData,
                                   const HostCommand* trailer) {
    SetGraphResult result;
    // The host's outputs are one set of buffers: a second writer would overwrite the first.
    const NodeDoc* firstOut = nullptr;
    for (const NodeDoc& n : document.nodes) {
        if (n.type != kAudioOut) continue;
        if (!firstOut) {
            firstOut = &n;
            continue;
        }
        result.errors.push_back({n.id, "", "audio-out-twice",
                                 "a graph has at most one rp.host.audio-out (first is " + firstOut->id + ")"});
        return result;
    }
    if (!waitForCommits()) {
        result.errors.push_back({"", "", "busy", "the audio thread has not adopted a published settings change"});
        return result;
    }
    reclaim();
    PlanOutcome outcome = planDocument(document, config_.sampleRate, config_.maxFrames);
    result.layouts = std::move(outcome.layouts);
    if (!outcome.errors.empty()) {
        result.errors = std::move(outcome.errors);
        return result;
    }
    outcome.storage->hostData = std::move(hostData);
    if (!publish(outcome, document, trailer)) {
        result.errors.push_back({"", "", "busy", "the command ring is full"});
        return result;
    }
    result.ok = true;
    return result;
}

bool GraphHost::pushHostCommand(const void* data, std::size_t size) {
    if (size > kHostCommandBytes) return false;
    Command c;
    c.kind = Command::Kind::Host;
    std::memcpy(c.host.bytes, data, size);
    return push(c);
}

void GraphHost::setHostCommandHandler(HostCommandHandler handler, void* ctx) {
    hostHandler_ = handler;
    hostHandlerCtx_ = ctx;
}

PlanOutcome GraphHost::planDocument(const GraphDocument& document, double sampleRate, std::uint32_t maxFrames) {
    PlanInput in;
    in.doc = &document;
    in.registry = &registry_;
    in.resources = &resources_;
    in.live = &live_;
    in.feedback = &feedback_;
    in.host = &host_;
    in.sampleRate = sampleRate;
    in.maxFrames = maxFrames;
    in.target = config_.target;
    in.faults = &faults_;
    in.quiescent = !audioThreadOwns();
    return plan(in);
}

// Hand a successful plan to the audio thread, its trailer in the same command. False, with the plan freed
// unpublished and the trailer never applied, if the ring is full.
bool GraphHost::publish(PlanOutcome& outcome, const GraphDocument& document, const HostCommand* trailer) {
    PlanStorage* storage = outcome.storage.release();
    const std::shared_ptr<const void> hostData = storage->hostData;
    const bool commits = !storage->commits.empty();
    if (commits) commitsInFlight_.fetch_add(1, std::memory_order_acq_rel);
    Command c;
    c.kind = Command::Kind::SwapPlan;
    c.plan = storage;
    if (trailer) {
        c.trailer = true;
        c.host = *trailer;
    }
    if (!commands_.tryPush(c)) {
        if (commits) commitsInFlight_.fetch_sub(1, std::memory_order_acq_rel);
        delete storage;  // never published: frees its prepared settings and pins
        return false;
    }
    // Published: the plan will be adopted, so the control-side records can move on.
    document_ = document;
    hostData_ = hostData;
    live_ = std::move(outcome.instances);
    feedback_ = std::move(outcome.feedback);
    for (auto& f : outcome.onPublished) f();
    flushIfQuiescent();
    return true;
}

void GraphHost::markFaulted(Instance& inst) {
    if (!inst.faulted.exchange(true, std::memory_order_relaxed)) faults_.fetch_add(1, std::memory_order_relaxed);
}

bool GraphHost::setParam(std::string_view nodeId, std::string_view paramId, double value, std::uint32_t frame) {
    const auto it = live_.find(std::string(nodeId));
    if (it == live_.end()) return false;
    const auto& params = it->second->layout.params;
    for (std::size_t k = 0; k < params.size(); ++k) {
        if (params[k].id != paramId) continue;
        const auto& wired = it->second->wiredParams;
        if (value != value || (k < wired.size() && wired[k])) return false;
        const ParamDesc& d = params[k];
        double v = std::clamp(value, d.min, d.max);
        if (d.type == ParamType::Int || d.type == ParamType::Enum) v = std::round(v);
        else if (d.type == ParamType::Bool) v = v >= 0.5 ? 1.0 : 0.0;
        Command c;
        c.kind = Command::Kind::SetParam;
        c.queue = &it->second->params[k]->queue;
        c.frame = frame;
        c.value = v;
        return push(c);
    }
    return false;
}

void GraphHost::drain() { drainCommands(false); }

void GraphHost::discardPending() { drainCommands(true); }

void GraphHost::drainCommands(bool discard, bool applyTrailers) {
    PlanStorage* replaced = nullptr;
    Command c;
    while (commands_.tryPop(c)) {
        switch (c.kind) {
            case Command::Kind::SwapPlan:
                c.plan->adopt();
                if (replaced && !released_.tryPush(replaced)) leakedPlans_.fetch_add(1, std::memory_order_relaxed);
                replaced = current_;
                current_ = c.plan;
                if (!c.plan->commits.empty()) commitsInFlight_.fetch_sub(1, std::memory_order_acq_rel);
                // The rest of the same edit, against the plan it rode in with. Applied under discard too:
                // the plan is adopted either way, and its trailer is what keeps the embedder in step with it.
                // Only the destructor hands it back as discarded.
                if (c.trailer && hostHandler_)
                    hostHandler_(hostHandlerCtx_, c.host, current_->hostData.get(), !applyTrailers);
                break;
            case Command::Kind::SetParam:
                pushControl(*c.queue, c.frame, c.value);
                break;
            case Command::Kind::Host:
                if (hostHandler_) hostHandler_(hostHandlerCtx_, c.host, current_ ? current_->hostData.get() : nullptr, discard);
                break;
        }
    }
    // The last replaced plan is handed back only after the drain. One replaced mid-drain is released
    // safely too: a command pushed after swap k can only reach plan k's instances, and plan k is still held
    // as current or replaced until the swap after it.
    if (replaced && !released_.tryPush(replaced)) leakedPlans_.fetch_add(1, std::memory_order_relaxed);
}

std::size_t GraphHost::reclaim() {
    std::size_t n = 0;
    PlanStorage* p = nullptr;
    while (released_.tryPop(p)) {
        delete p;
        ++n;
    }
    // The audio thread only counts a leak (it must not log); say so here, once per new leak.
    if (const auto leaked = leakedPlans(); leaked != leaksReported_) {
        std::fprintf(stderr, "[graph] release ring full: %u plan(s) leaked rather than freed on the audio thread\n",
                     static_cast<unsigned>(leaked - leaksReported_));
        leaksReported_ = leaked;
    }
    return n;
}

bool GraphHost::reactivate(double sampleRate, std::uint32_t maxFrames, std::string* error) {
    auto fail = [&](std::string why) {
        if (error) *error = std::move(why);
        return false;
    };
    if (audioThreadOwns()) return fail("reactivate needs the audio thread stopped");
    if (auto why = checkConfig(sampleRate, maxFrames)) return fail(*why);
    if (live_.empty()) {
        config_.sampleRate = sampleRate;
        config_.maxFrames = maxFrames;
        return true;
    }
    reclaim();
    // Plan first, at the new size: if the document no longer plans (a resource dropped since), nothing has
    // changed yet. Same document, same instances, so no state is lost; reused instances need no
    // activation to be planned.
    const GraphDocument document = document_;
    PlanOutcome outcome = planDocument(document, sampleRate, maxFrames);
    if (!outcome.errors.empty()) return fail("the current graph no longer plans: " + outcome.errors[0].message);

    // Then re-activate every instance. One that refuses puts every instance already switched back on the
    // old rate and size, so the running plan (built for those) stays right for all of them; the refuser
    // is bypassed if it will not come back on the old ones either. The new plan is dropped unpublished.
    auto backToOld = [&](Instance& inst) {
        rp_error err{};
        inst.active = inst.node->vt->activate(inst.node, config_.sampleRate, config_.maxFrames, &err);
        if (!inst.active) markFaulted(inst);
    };
    std::vector<Instance*> switched;
    for (auto& [id, inst] : live_) {
        if (inst->active) inst->node->vt->deactivate(inst->node);
        rp_error err{};
        inst->active = inst->node->vt->activate(inst->node, sampleRate, maxFrames, &err);
        if (inst->active) {
            switched.push_back(inst.get());
            continue;
        }
        const std::string why = "node " + jsonString(id) + " could not activate: " + errorText(err, "no reason given");
        for (Instance* s : switched) {
            s->node->vt->deactivate(s->node);
            backToOld(*s);
        }
        backToOld(*inst);
        return fail(why);
    }
    // Quiescent, so the command ring is empty and publishing cannot be refused. The replan carries the
    // host data the document was set with: a rate change must not strip the engine's tables from the plan.
    outcome.storage->hostData = hostData_;
    [[maybe_unused]] const bool published = publish(outcome, document);
    assert(published);
    config_.sampleRate = sampleRate;
    config_.maxFrames = maxFrames;
    return true;
}

bool GraphHost::isFaulted(std::string_view id) const {
    const auto it = live_.find(std::string(id));
    return it != live_.end() && it->second->faulted.load(std::memory_order_relaxed);
}

rp_node* GraphHost::node(std::string_view id) const {
    const auto it = live_.find(std::string(id));
    return it == live_.end() ? nullptr : it->second->node;
}

const void* GraphHost::hostData() const { return current_ ? current_->hostData.get() : nullptr; }

void GraphHost::processBlock(std::uint32_t frames, const rp_transport& transport, float* const* outputs,
                             std::uint32_t outputCount) {
    drain();
    run(frames, transport, outputs, outputCount);
}

void GraphHost::run(std::uint32_t frames, const rp_transport& transport, float* const* outputs,
                    std::uint32_t outputCount) {
    // Silence first, so channels no bus covers (or every channel, before the first plan or in a refused
    // block) never carry what the host left in its buffers, which a DAW often aliases to the inputs.
    if (outputs)
        for (std::uint32_t c = 0; c < outputCount; ++c)
            if (outputs[c]) std::memset(outputs[c], 0, frames * sizeof(float));
    hostOutputs_ = outputs;
    hostOutputCount_ = outputCount;
    // Every buffer in a plan is the plan's maxFrames long, and nodes were activated with that maximum; a
    // host that delivers a longer block must split it, or reactivate first.
    if (current_ && frames > 0 && frames <= current_->maxFrames) runBlock(current_->view, frames, transport);
    hostOutputs_ = nullptr;
    hostOutputCount_ = 0;
}

}  // namespace rp::graph

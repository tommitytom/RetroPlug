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

// No optional host services yet; this is where they will be offered by id.
const void* hostInterface(const rp_host*, const char*) { return nullptr; }

}  // namespace

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
    host_ = rp_host{RP_ABI_VERSION, this, &hostAlloc, &hostFree, &hostLog, &hostInterface};
}

GraphHost::~GraphHost() {
    assert(!audioThreadOwns() && "destroy a GraphHost only after its audio thread has stopped");
    // The audio thread has stopped (setAudioThreadOwns(false) is the caller's job): adopt anything still
    // queued so every plan is in exactly one place, then free them all here, on the control thread.
    drain();
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

bool GraphHost::waitForAdoption() {
    if (!audioThreadOwns()) return true;  // inline flush already adopted it
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(config_.adoptionTimeoutMs);
    while (swapInFlight_.load(std::memory_order_acquire)) {
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

SetGraphResult GraphHost::setGraph(const GraphDocument& document) {
    SetGraphResult result;
    if (!waitForAdoption()) {
        result.errors.push_back({"", "", "busy", "the audio thread has not adopted the previous plan"});
        return result;
    }
    reclaim();
    PlanOutcome outcome = planDocument(document, config_.sampleRate, config_.maxFrames);
    result.layouts = std::move(outcome.layouts);
    if (!outcome.errors.empty()) {
        result.errors = std::move(outcome.errors);
        return result;
    }
    if (!publish(outcome, document)) {
        result.errors.push_back({"", "", "busy", "the command ring is full"});
        return result;
    }
    result.ok = true;
    return result;
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

// Hand a successful plan to the audio thread. False, with the plan freed unpublished, if the ring is full.
bool GraphHost::publish(PlanOutcome& outcome, const GraphDocument& document) {
    PlanStorage* storage = outcome.storage.release();
    swapInFlight_.store(true, std::memory_order_release);
    Command c;
    c.kind = Command::Kind::SwapPlan;
    c.plan = storage;
    if (!commands_.tryPush(c)) {
        swapInFlight_.store(false, std::memory_order_release);
        delete storage;  // never published: frees its prepared settings and pins
        return false;
    }
    // Published: the plan will be adopted, so the control-side records can move on.
    document_ = document;
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

void GraphHost::drain() {
    PlanStorage* replaced = nullptr;
    Command c;
    while (commands_.tryPop(c)) {
        switch (c.kind) {
            case Command::Kind::SwapPlan:
                c.plan->adopt();
                if (replaced && !released_.tryPush(replaced)) leakedPlans_.fetch_add(1, std::memory_order_relaxed);
                replaced = current_;
                current_ = c.plan;
                swapInFlight_.store(false, std::memory_order_release);
                break;
            case Command::Kind::SetParam:
                pushControl(*c.queue, c.frame, c.value);
                break;
        }
    }
    // Hand the replaced plan back only after every command in this drain has been applied, so a param
    // write queued behind a swap can never land in memory the control thread is already freeing.
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
    // Quiescent, so the command ring is empty and publishing cannot be refused.
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

void GraphHost::processBlock(std::uint32_t frames, const rp_transport& transport, float* const* outputs,
                             std::uint32_t outputCount) {
    drain();
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

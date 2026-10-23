#include "graph/GraphHost.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

#include "graph/Executor.hpp"
#include "graph/Queue.hpp"

namespace rp::graph {

namespace {

// The ABI's free has no alignment argument, so host allocations are capped at max_align_t.
void* hostAlloc(void*, std::size_t size, std::size_t align) {
    return align <= alignof(std::max_align_t) ? std::malloc(size ? size : 1) : nullptr;
}

void hostFree(void*, void* ptr) { std::free(ptr); }

void hostLog(void*, int, const char* message) { std::fprintf(stderr, "[graph] %s\n", message); }

// No optional host services yet; this is where they will be offered by id.
const void* hostInterface(const rp_host*, const char*) { return nullptr; }

constexpr auto kAdoptionTimeout = std::chrono::seconds(2);

}  // namespace

GraphHost::GraphHost(const NodeRegistry& registry, ResourceTable& resources, HostConfig config)
    : registry_(registry), resources_(resources), config_(config) {
    host_ = rp_host{RP_ABI_VERSION, this, &hostAlloc, &hostFree, &hostLog, &hostInterface};
}

GraphHost::~GraphHost() {
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
    const auto deadline = std::chrono::steady_clock::now() + kAdoptionTimeout;
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

    PlanInput in;
    in.doc = &document;
    in.registry = &registry_;
    in.resources = &resources_;
    in.live = &live_;
    in.feedback = &feedback_;
    in.host = &host_;
    in.sampleRate = config_.sampleRate;
    in.maxFrames = config_.maxFrames;
    in.target = config_.target;
    in.faults = &faults_;
    in.quiescent = !audioThreadOwns();
    PlanOutcome outcome = plan(in);
    result.layouts = std::move(outcome.layouts);
    if (!outcome.errors.empty()) {
        result.errors = std::move(outcome.errors);
        return result;
    }

    PlanStorage* storage = outcome.storage.release();
    swapInFlight_.store(true, std::memory_order_release);
    Command c;
    c.kind = Command::Kind::SwapPlan;
    c.plan = storage;
    if (!commands_.tryPush(c)) {
        swapInFlight_.store(false, std::memory_order_release);
        delete storage;  // never published: frees its prepared settings and pins
        result.errors.push_back({"", "", "busy", "the command ring is full"});
        return result;
    }
    // Published: the plan will be adopted, so the control-side records can move on.
    document_ = document;
    live_ = std::move(outcome.instances);
    feedback_ = std::move(outcome.feedback);
    for (auto& f : outcome.onPublished) f();
    flushIfQuiescent();
    result.ok = true;
    return result;
}

bool GraphHost::setParam(std::string_view nodeId, std::string_view paramId, double value, std::uint32_t frame) {
    const auto it = live_.find(std::string(nodeId));
    if (it == live_.end()) return false;
    const auto& params = it->second->layout.params;
    for (std::size_t k = 0; k < params.size(); ++k) {
        if (params[k].id != paramId) continue;
        if (value != value) return false;
        Command c;
        c.kind = Command::Kind::SetParam;
        c.queue = &it->second->params[k]->queue;
        c.frame = frame;
        c.value = value < params[k].min ? params[k].min : value > params[k].max ? params[k].max : value;
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
                if (replaced && !released_.tryPush(replaced))
                    std::fprintf(stderr, "[graph] release ring full: leaking a plan rather than freeing on the audio thread\n");
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
    if (replaced && !released_.tryPush(replaced))
        std::fprintf(stderr, "[graph] release ring full: leaking a plan rather than freeing on the audio thread\n");
}

std::size_t GraphHost::reclaim() {
    std::size_t n = 0;
    PlanStorage* p = nullptr;
    while (released_.tryPop(p)) {
        delete p;
        ++n;
    }
    return n;
}

bool GraphHost::reactivate(double sampleRate, std::uint32_t maxFrames) {
    if (audioThreadOwns()) return false;
    config_.sampleRate = sampleRate;
    config_.maxFrames = maxFrames;
    for (auto& [id, inst] : live_) {
        if (inst->active) inst->node->vt->deactivate(inst->node);
        rp_error err{};
        inst->active = inst->node->vt->activate(inst->node, sampleRate, maxFrames, &err);
        if (!inst->active) return false;
    }
    // Same document, same instances (so no state is lost), but every buffer re-sized for maxFrames.
    return live_.empty() || setGraph(GraphDocument(document_)).ok;
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
    hostOutputs_ = outputs;
    hostOutputCount_ = outputCount;
    // Every buffer in a plan is the plan's maxFrames long, and nodes were activated with that maximum; a
    // host that delivers a longer block must split it, or reactivate first.
    if (current_ && frames > 0 && frames <= current_->maxFrames) runBlock(current_->view, frames, transport);
    hostOutputs_ = nullptr;
    hostOutputCount_ = 0;
}

}  // namespace rp::graph

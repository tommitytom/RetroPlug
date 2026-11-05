#include "DeviceCheck.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <type_traits>

#include "devtest/DevtestNodes.hpp"
#include "graph/Executor.hpp"
#include "graph/Plan.hpp"
#include "graph/PlanLoader.hpp"
#include "graph/Queue.hpp"
#include "graph/rp_device.h"

namespace rp::device_check {

namespace {

constexpr double kSampleRate = 48000.0;
// A control stream's capacity: the planner's (PlanStorage.hpp's kStreamChanges), so a node sees the queue it would.
constexpr std::uint32_t kControlChanges = 64;

// Bump allocation over the caller's memory, started afresh for each type. Everything a type's run needs comes from it
// (its instance, its port views, its buffers and queues), so the check holds nothing of its own beyond the stack.
struct Arena {
    std::byte*  base;
    std::size_t size;
    std::size_t used = 0;

    void* raw(std::size_t bytes, std::size_t align) {
        const std::uintptr_t start = reinterpret_cast<std::uintptr_t>(base) + used;
        const std::uintptr_t aligned = (start + (align - 1)) & ~std::uintptr_t(align - 1);
        const std::size_t    at = used + static_cast<std::size_t>(aligned - start);
        if (at > size || bytes > size - at) return nullptr;
        used = at + bytes;
        std::memset(base + at, 0, bytes);
        return base + at;
    }
    template <class T>
    T* take(std::size_t count) {
        return static_cast<T*>(raw(count * sizeof(T), alignof(T)));
    }
};

// The buffer behind res:1. Static rather than in the arena, as a resource outlives the nodes that pin it.
float                 gBufferData[kBufferChannels][kBufferFrames];
float*                gBufferChannels[kBufferChannels] = {gBufferData[0], gBufferData[1]};
rp_audio_buffer       gBuffer{kBufferChannels, kBufferFrames, kSampleRate, gBufferChannels};
rp_resource           gResource{"audio-buffer", &gBuffer};

const rp_resource* acquireResource(void*, std::uint32_t id, const char* type, rp_error* err) {
    if (id == kBufferResource && (!type || std::strcmp(type, gResource.type) == 0)) return &gResource;
    // The ABI's acquire fills err (rp_node.h), and node.hpp always passes one; a host that is handed none says nothing.
    if (err) std::strcpy(err->message, "the device check's host holds res:1, an audio buffer, and nothing else");
    return nullptr;
}
bool releaseResource(void*, std::uint32_t id) { return id == kBufferResource; }
const rp_host_resources kResources{nullptr, &acquireResource, &releaseResource};

// The device links (rp.host.devices/2) of a host that owns no device: every acquire answers null, so a node that binds
// one (rp.n8.nes) runs on its own disconnected stub, as it does in any host with no such device. A node that finds no
// devices interface at all refuses to activate, so the check offers this one rather than none.
void* acquireDevice(void*, const char*, const char*) { return nullptr; }
void  releaseDevice(void*, void*) {}
const rp_host_devices kDevices{nullptr, &acquireDevice, &releaseDevice};

void ignoreLog(void*, int, const char*) {}
const void* hostInterface(const rp_host*, const char* id) {
    if (std::strcmp(id, RP_HOST_RESOURCES_INTERFACE) == 0) return &kResources;
    return std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0 ? &kDevices : nullptr;
}
const rp_host kHost{RP_ABI_VERSION, nullptr, nullptr, nullptr, &ignoreLog, &hostInterface};

// The input signal every audio input and the buffer carry: small, deterministic, and not silence, so the kernels run
// on numbers.
float signal(std::uint32_t frame, std::uint32_t channel) {
    return static_cast<float>((frame * 7u + channel * 13u) % 64u) / 256.0f - 0.125f;
}

const TypeCase* findCase(const char* id, const TypeCase* cases, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i)
        if (std::strcmp(cases[i].id, id) == 0) return &cases[i];
    return nullptr;
}

rp_control_queue* controlQueue(Arena& a, double start) {
    auto* q = a.take<rp_control_queue>(1);
    auto* changes = a.take<rp_control_change>(kControlChanges);
    if (!q || !changes) return nullptr;
    q->start = start;
    q->changes = changes;
    q->capacity = kControlChanges;
    return q;
}

rp_queue* messageQueue(Arena& a, std::uint32_t messages, std::uint32_t bytes) {
    auto* q = a.take<rp_queue>(1);
    auto* items = a.take<rp_message>(messages);
    auto* arena = a.take<std::uint8_t>(bytes);
    if (!q || !items || !arena) return nullptr;
    q->items = items;
    q->capacity = messages;
    q->bytes = arena;
    q->bytes_capacity = bytes;
    return q;
}

// One port's view, with the buffers or queue behind it. An input's audio carries the signal; an output's starts
// silent.
bool bindPort(Arena& a, const PortCase& p, rp_port_view& view, std::uint32_t frames) {
    view.kind = p.kind;
    switch (p.kind) {
        case RP_PORT_AUDIO: {
            auto* channels = a.take<float*>(p.channels);
            if (!channels) return false;
            for (std::uint32_t c = 0; c < p.channels; ++c) {
                channels[c] = a.take<float>(frames);
                if (!channels[c]) return false;
                if (!p.output)
                    for (std::uint32_t i = 0; i < frames; ++i)
                        channels[c][i] = signal(i, c);
            }
            view.u.audio.channels = channels;
            view.u.audio.channel_count = p.channels;
            return true;
        }
        case RP_PORT_EVENTS:
        case RP_PORT_BYTES:
            view.u.queue = messageQueue(a, p.messages, p.bytes);
            return view.u.queue != nullptr;
        case RP_PORT_CONTROL:
            view.u.control = controlQueue(a, 0.0);
            return view.u.control != nullptr;
        case RP_PORT_RESOURCE:
            // An input is handed the host's buffer, as a wire from a resource output would hand it one, so a player
            // plays it and a recorder writes it; an output's slot starts empty, for the node to fill.
            view.u.resource = a.take<rp_resource*>(1);
            if (view.u.resource && !p.output) *view.u.resource = &gResource;
            return view.u.resource != nullptr;
        case RP_PORT_LINK:
        default:
            return true;
    }
}

// Binds the type's ports and params into a one-step plan and runs one block of it. False if the arena ran out.
bool runOneBlock(Arena& a, rp_node* node, const TypeCase& c, std::uint32_t frames, bool* faulted) {
    std::uint32_t inputs = 0, outputs = 0, queues = 0, controls = c.paramCount;
    for (std::uint32_t i = 0; i < c.portCount; ++i) {
        const PortCase& p = c.ports[i];
        (p.output ? outputs : inputs) += 1;
        if (p.output && (p.kind == RP_PORT_EVENTS || p.kind == RP_PORT_BYTES)) ++queues;
        if (p.output && p.kind == RP_PORT_CONTROL) ++controls;
    }
    auto* in = a.take<rp_port_view>(inputs);
    auto* out = a.take<rp_port_view>(outputs);
    auto* params = a.take<rp_control_queue*>(c.paramCount);
    auto* toClear = a.take<graph::OutputQueue>(queues);
    auto* toRoll = a.take<rp_control_queue*>(controls);
    auto* connected = a.take<std::uint8_t>(outputs);
    if (!in || !out || !params || !toClear || !toRoll || !connected) return false;

    std::uint32_t nIn = 0, nOut = 0, nClear = 0, nRoll = 0;
    for (std::uint32_t i = 0; i < c.portCount; ++i) {
        const PortCase& p = c.ports[i];
        rp_port_view&   view = p.output ? out[nOut++] : in[nIn++];
        if (!bindPort(a, p, view, frames)) return false;
        if (!p.output) continue;
        if (p.kind == RP_PORT_EVENTS || p.kind == RP_PORT_BYTES) toClear[nClear++] = graph::OutputQueue{view.u.queue};
        if (p.kind == RP_PORT_CONTROL) toRoll[nRoll++] = view.u.control;
    }
    for (std::uint32_t k = 0; k < c.paramCount; ++k) {
        params[k] = controlQueue(a, c.params[k]);
        if (!params[k]) return false;
        toRoll[nRoll++] = params[k];
    }

    // Every output wired, so the node writes each one as it would in a plan that reads it.
    for (std::uint32_t o = 0; o < outputs; ++o) connected[o] = 1;
    node->vt->ports_changed(node, connected, outputs);

    std::atomic<bool>          fault{false};
    std::atomic<std::uint32_t> faults{0}, drops{0};
    graph::PlanStep            step;
    step.node = node;
    step.process = rp_process{0, nullptr, in, inputs, out, outputs, params, c.paramCount};
    step.faulted = &fault;
    const graph::PlanView plan{std::span<graph::PlanStep>(&step, 1),
                               std::span<const graph::OutputQueue>(toClear, queues),
                               std::span<rp_control_queue* const>(toRoll, controls),
                               {},
                               &faults,
                               &drops};
    const rp_transport transport{kSampleRate, 120.0, 0.0, RP_TRANSPORT_PLAYING};
    graph::runBlock(plan, frames, transport);
    *faulted = fault.load(std::memory_order_relaxed);
    return true;
}

Step runType(const rp_node_type& type, const TypeCase& c, Arena& a, std::uint32_t frames, Result& r) {
    const rp_settings settings{reinterpret_cast<const char*>(c.settings), c.settingsSize, RP_SETTINGS_MSGPACK};
    rp_memory_req req{};
    if (!type.query_memory(&type, &settings, &req, &r.error)) return Step::QueryMemory;
    if (req.size > r.instanceBytes) r.instanceBytes = req.size;
    void* memory = a.raw(req.size, req.align ? req.align : alignof(std::max_align_t));
    if (!memory) return Step::Arena;
    rp_node* node = type.create(&type, &kHost, memory, &settings, &r.error);
    if (!node) return Step::Create;
    if (!node->vt->activate(node, kSampleRate, frames, &r.error)) {
        type.destroy(node);
        return Step::Activate;
    }
    bool       faulted = false;
    const bool bound = runOneBlock(a, node, c, frames, &faulted);
    node->vt->deactivate(node);
    type.destroy(node);
    if (!bound) return Step::Arena;
    return faulted ? Step::Process : Step::Ok;
}

// A node's id (not NUL-terminated, in the blob) into the result's error, cut to fit, with what happened to it.
void nodeAt(PlanResult& out, PlanStep step, std::string_view node, const char* what) {
    out.step = step;
    const std::size_t n = node.size() < sizeof out.error.node - 1 ? node.size() : sizeof out.error.node - 1;
    std::memcpy(out.error.node, node.data(), n);
    out.error.node[n] = '\0';
    std::strncpy(out.error.message, what, sizeof out.error.message - 1);
}

// The stream move `m` writes, or null: its node's own param stream (null for a wired param or one past its params).
rp_control_queue* moveStream(const graph::LoadedPlan& plan, const PlanMove& m) {
    const graph::LoadedNode* n = plan.find(m.node);
    return n ? plan.param(static_cast<std::uint32_t>(n - plan.nodes.data()), m.param) : nullptr;
}

}  // namespace

const rp_host& host() { return kHost; }

Result run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
           void* arena, std::size_t arenaBytes, std::uint32_t frames) {
    for (std::uint32_t c = 0; c < kBufferChannels; ++c)
        for (std::uint32_t i = 0; i < kBufferFrames; ++i) gBufferData[c][i] = signal(i, c);
    Result        r;
    std::size_t   matched = 0;
    std::uint32_t index = 0;
    for (std::size_t p = 0; p < packageCount; ++p) {
        for (std::uint32_t t = 0; t < packages[p]->node_type_count; ++t, ++index) {
            const rp_node_type& type = *packages[p]->node_types[t];
            r.type = index;
            r.id = type.id;
            const TypeCase* c = findCase(type.id, cases, caseCount);
            if (!c) {
                r.step = Step::NoCase;
                return r;
            }
            ++matched;
            Arena a{static_cast<std::byte*>(arena), arenaBytes};
            r.step = runType(type, *c, a, frames, r);
            if (a.used > r.arenaBytes) r.arenaBytes = a.used;
            if (r.step != Step::Ok) return r;
            ++r.types;
        }
    }
    // Each case's id is a different type's, so a case left over is one no registered type took.
    if (matched != caseCount) {
        r.step = Step::CaseUnused;
        r.type = index;
        r.id = nullptr;
        return r;
    }
    r.id = nullptr;
    return r;
}

void runPlan(const PlanCase& c, const rp_package* const* packages, std::size_t packageCount, void* arena,
             std::size_t arenaBytes, PlanResult* out) {
    // Reset in place: a PlanResult{} assigned would be built on this frame first, with the loader's chain on top of it.
    static_assert(std::is_trivially_copyable_v<PlanResult>);
    PlanResult& r = *out;
    std::memset(static_cast<void*>(&r), 0, sizeof r);
    for (std::size_t i = 0; i < c.splitCount; ++i)
        if (c.splits[i] == 0) {
            nodeAt(r, PlanStep::Case, "-", "a block size of 0 frames, which would never end the run");
            return;
        }
    graph::LoadedPlan* plan =
        graph::loadPlan(std::span<const std::uint8_t>(c.blob, c.blobSize),
                        std::span<std::byte>(static_cast<std::byte*>(arena), arenaBytes),
                        std::span<const rp_package* const>(packages, packageCount), &kHost, &r.error);
    if (!plan) {
        r.step = PlanStep::Load;
        if (r.error.node[0] == '\0') std::strcpy(r.error.node, "-");  // the blob refused whole: no node at fault
        return;
    }
    r.nodes = static_cast<std::uint32_t>(plan->nodes.size());
    r.arenaBytes = plan->bytes;
    r.sampleRate = plan->sampleRate;
    r.maxFrames = plan->maxFrames;
    auto stop = [&](PlanStep step, std::string_view node, const char* what) {
        nodeAt(r, step, node, what);
        graph::unloadPlan(plan);
    };

    // The exporter sized the plan with the desktop's builds of the device's types, which the design takes to be at
    // least as large as the device's own (its section 11, question 1): the device's layout is held to that figure.
    if (c.arenaEstimate && r.arenaBytes > c.arenaEstimate)
        return stop(PlanStep::Estimate, "-", "the plan took more arena than the exporter's sizing gave it");

    // Every move is held to a stream before the first block, so a case that names the wrong param fails before it runs.
    for (std::size_t i = 0; i < c.moveCount; ++i)
        if (!moveStream(*plan, c.moves[i]))
            return stop(PlanStep::Move, c.moves[i].node,
                        "a param move names no param stream: no such node, a param past its params, or one a wire "
                        "drives");

    const rp_transport transport{plan->sampleRate, 120.0, 0.0, RP_TRANSPORT_PLAYING};
    for (std::size_t split = 0; r.frames < c.frames; ++split) {
        for (std::size_t i = 0; i < c.moveCount; ++i) {
            if (c.moves[i].block != r.blocks) continue;
            if (!graph::pushControl(*moveStream(*plan, c.moves[i]), 0, c.moves[i].value))
                return stop(PlanStep::Move, c.moves[i].node, "a param move found its param's stream full");
            ++r.moves;
        }
        // A block never passes the plan's maxFrames: runBlock does not check, and the plan's buffers hold no more.
        std::uint32_t frames = c.splitCount ? c.splits[split % c.splitCount] : plan->maxFrames;
        if (frames > plan->maxFrames) frames = plan->maxFrames;
        if (frames > c.frames - r.frames) frames = c.frames - r.frames;
        graph::runBlock(plan->view, frames, transport);
        ++r.blocks;
        r.frames += frames;
        if (plan->faults.load(std::memory_order_relaxed) != 0) {
            for (const graph::LoadedNode& n : plan->nodes)
                if (n.faulted.load(std::memory_order_relaxed))
                    return stop(PlanStep::Process, n.name(), "the block faulted the node");
        }
    }
    r.drops = plan->drops.load(std::memory_order_relaxed);
    const graph::LoadedNode* d = c.digest ? plan->find(c.digest) : nullptr;
    if (d)
        if (const void* view = d->node->vt->get_interface(d->node, RP_DEVTEST_DIGEST_INTERFACE)) {
            rp_devtest_digest seen;
            std::memcpy(&seen, view, sizeof seen);
            r.digested = true;
            r.digest = seen.hash;
            r.digestBlocks = seen.blocks;
            r.heard = seen.heard;
        }

    // The run's integer results, which are the device's to match exactly: every frame, every move, nothing dropped, and
    // a digest that heard every block. The digest's value is float arithmetic, and is not judged.
    if (r.moves != c.moveCount) {
        for (std::size_t i = 0; i < c.moveCount; ++i)
            if (c.moves[i].block >= r.blocks)
                return stop(PlanStep::Result, c.moves[i].node, "a param move falls past the run's last block");
    }
    if (r.drops != 0) {
        for (const graph::LoadedNode& n : plan->nodes)
            if (n.drops.load(std::memory_order_relaxed) != 0)
                return stop(PlanStep::Result, n.name(), "a full output queue dropped messages");
        return stop(PlanStep::Result, "-", "a full queue dropped messages");
    }
    if (c.digest && !r.digested)
        return stop(PlanStep::Result, c.digest, "the case's digest node is not in the plan, or is no digest");
    if (c.digest && !r.heard) return stop(PlanStep::Result, c.digest, "the digest heard nothing but silence");
    if (c.digest && r.digestBlocks != r.blocks)
        return stop(PlanStep::Result, c.digest, "the digest counted other blocks than ran");
    graph::unloadPlan(plan);
}

}  // namespace rp::device_check

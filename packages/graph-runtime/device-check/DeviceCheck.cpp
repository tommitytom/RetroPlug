#include "DeviceCheck.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
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
#include "graph/rp_device_midi_in.h"

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

// The one package memory the host answers rp.host.packages/1 with (openPackage): a script type's, set up for its run.
struct Offered {
    const rp_package* package = nullptr;
    void*             memory = nullptr;
};
Offered gOffered;

void* packageMemory(void*, const rp_package* package, rp_error* err) {
    if (package && package == gOffered.package) return gOffered.memory;
    if (err) std::strcpy(err->message, "the device check's host set no memory up for the package");
    return nullptr;
}
const rp_host_packages kPackages{nullptr, &packageMemory};

// The clock a script node's deadline reads (offerClock), or none.
const rp_host_clock* gClock = nullptr;

// The host's MIDI input (rp.device.midi-in/1): the stimulus's messages for the block runPlan staged, one a call, none
// once they are all taken or while no case stages any. The bytes it hands out stay valid until its next call.
struct Staging {
    bool          on = false;
    std::uint32_t block = 0;
    std::uint32_t taken = 0;
    std::uint32_t handed = 0;  // every message handed out since the case began
    std::uint8_t  data[64] = {};
};
Staging gStaging;

bool nextMessage(void*, std::uint32_t frames, rp_device_midi_message* out) {
    if (!gStaging.on || gStaging.taken >= stimulusCount(gStaging.block)) return false;
    std::uint32_t frame = 0;
    const std::uint32_t size = stimulusMessage(gStaging.block, gStaging.taken++, frames, gStaging.data, &frame);
    *out = rp_device_midi_message{frame, size, gStaging.data};
    ++gStaging.handed;
    return true;
}
const rp_device_midi_in kMidiIn{nullptr, &nextMessage};

// A script node's console.log (rp.host.script-log/1): the lines it ends, counted. The loop node logs a line as its
// loop begins, which is how the deadline case knows the loop was entered before the deadline faulted it.
std::uint32_t gLogLines = 0;
void          countLines(void*, const char* text, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) gLogLines += text[i] == '\n';
}
const rp_host_script_log kScriptLog{nullptr, &countLines};

void ignoreLog(void*, int, const char*) {}
const void* hostInterface(const rp_host*, const char* id) {
    if (std::strcmp(id, RP_HOST_SCRIPT_LOG_INTERFACE) == 0) return &kScriptLog;
    if (std::strcmp(id, RP_HOST_RESOURCES_INTERFACE) == 0) return &kResources;
    if (std::strcmp(id, RP_HOST_PACKAGES_INTERFACE) == 0) return &kPackages;
    if (std::strcmp(id, RP_HOST_CLOCK_INTERFACE) == 0) return gClock;
    if (std::strcmp(id, RP_DEVICE_MIDI_IN_INTERFACE) == 0) return &kMidiIn;
    return std::strcmp(id, RP_HOST_DEVICES_INTERFACE) == 0 ? &kDevices : nullptr;
}
const rp_host kHost{RP_ABI_VERSION, nullptr, nullptr, nullptr, &ignoreLog, &hostInterface};

// The bytes a parser gives a message whose first byte is `first` (the device-pack toolchain's runner's): a data byte's
// two, a channel message's two or three, a short SysEx, a system common's, and a real-time byte or a lone F7 alone.
std::uint32_t messageOf(std::uint8_t first, std::uint8_t* out) {
    out[0] = first;
    if (first < 0x80) return out[1] = 0x40, 2;
    if (first < 0xC0 || (first >= 0xE0 && first < 0xF0)) return out[1] = 0x3C, out[2] = 0x64, 3;
    if (first < 0xE0) return out[1] = 0x05, 2;
    if (first == 0xF0) return out[1] = 0x7D, out[2] = 0x01, out[3] = 0xF7, 4;
    if (first == 0xF1 || first == 0xF3) return out[1] = 0x10, 2;
    if (first == 0xF2) return out[1] = 0x10, out[2] = 0x20, 3;
    return 1;
}

// A script node's fault's words, through rp.script.fault/1, or null for a node that is no script node or has none.
const char* scriptFault(rp_node* node) {
    const auto* fault = static_cast<const rp_script_fault*>(node->vt->get_interface(node, RP_SCRIPT_FAULT_INTERFACE));
    return fault ? fault->reason(node) : nullptr;
}

// What `what` happened, with a script node's fault's words after it when it has some, into `message`.
void faultWords(char* message, std::size_t size, const char* what, rp_node* node) {
    const char* words = scriptFault(node);
    if (words)
        std::snprintf(message, size, "%s: %s", what, words);
    else
        std::snprintf(message, size, "%s", what);
}

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

// A type's node bound into a one-step plan: its ports' views, with the buffers and queues behind them, and its params'
// streams, in the arena, every output wired, so the node writes each as it would in a plan that reads it.
struct OneStep {
    graph::PlanStep            step;
    graph::OutputQueue*        toClear = nullptr;
    rp_control_queue**         toRoll = nullptr;
    std::uint32_t              queues = 0;
    std::uint32_t              controls = 0;
    std::atomic<bool>          fault{false};
    std::atomic<std::uint32_t> faults{0}, drops{0};

    // False if the arena ran out.
    bool bind(Arena& a, rp_node* node, const TypeCase& c, std::uint32_t frames) {
        std::uint32_t inputs = 0, outputs = 0;
        controls = c.paramCount;
        for (std::uint32_t i = 0; i < c.portCount; ++i) {
            const PortCase& p = c.ports[i];
            (p.output ? outputs : inputs) += 1;
            if (p.output && (p.kind == RP_PORT_EVENTS || p.kind == RP_PORT_BYTES)) ++queues;
            if (p.output && p.kind == RP_PORT_CONTROL) ++controls;
        }
        auto* in = a.take<rp_port_view>(inputs);
        auto* out = a.take<rp_port_view>(outputs);
        auto* params = a.take<rp_control_queue*>(c.paramCount);
        toClear = a.take<graph::OutputQueue>(queues);
        toRoll = a.take<rp_control_queue*>(controls);
        auto* connected = a.take<std::uint8_t>(outputs);
        if (!in || !out || !params || !toClear || !toRoll || !connected) return false;

        std::uint32_t nIn = 0, nOut = 0, nClear = 0, nRoll = 0;
        for (std::uint32_t i = 0; i < c.portCount; ++i) {
            const PortCase& p = c.ports[i];
            rp_port_view&   view = p.output ? out[nOut++] : in[nIn++];
            if (!bindPort(a, p, view, frames)) return false;
            if (!p.output) continue;
            if (p.kind == RP_PORT_EVENTS || p.kind == RP_PORT_BYTES)
                toClear[nClear++] = graph::OutputQueue{view.u.queue};
            if (p.kind == RP_PORT_CONTROL) toRoll[nRoll++] = view.u.control;
        }
        for (std::uint32_t k = 0; k < c.paramCount; ++k) {
            params[k] = controlQueue(a, c.params[k]);
            if (!params[k]) return false;
            toRoll[nRoll++] = params[k];
        }
        for (std::uint32_t o = 0; o < outputs; ++o) connected[o] = 1;
        node->vt->ports_changed(node, connected, outputs);
        step.node = node;
        step.process = rp_process{0, nullptr, in, inputs, out, outputs, params, c.paramCount};
        step.faulted = &fault;
        return true;
    }

    // One block; true if the node faulted. Its output queues are emptied after the block, as a plan's are, unless
    // `keepOutputs` asks that they be left for the caller to read and empty.
    bool run(std::uint32_t frames, bool keepOutputs = false) {
        const graph::PlanView plan{std::span<graph::PlanStep>(&step, 1),
                                   std::span<const graph::OutputQueue>(toClear, keepOutputs ? 0 : queues),
                                   std::span<rp_control_queue* const>(toRoll, controls),
                                   {},
                                   &faults,
                                   &drops};
        const rp_transport transport{kSampleRate, 120.0, 0.0, RP_TRANSPORT_PLAYING};
        graph::runBlock(plan, frames, transport);
        return fault.load(std::memory_order_relaxed);
    }
};

// The type's package's memory (a script pack's context), placed in the arena and set up, which the host offers its
// nodes until the object goes; nothing for a package with no memory of its own. Step::Package when it is refused or
// does not fit.
struct PackageScope {
    bool open = false;

    Step take(Arena& a, const rp_package& package, rp_error* err) {
        if (!package.memory) return Step::Ok;
        rp_memory_req req{};
        if (!package.memory->query(&package, &req, err)) return Step::Package;
        void* memory = a.raw(req.size, req.align ? req.align : alignof(std::max_align_t));
        if (!memory) {
            std::strcpy(err->message, "the package's memory does not fit the arena");
            return Step::Package;
        }
        if (!openPackage(&package, memory, err)) return Step::Package;
        open = true;
        return Step::Ok;
    }
    ~PackageScope() {
        if (open) closePackage();
    }
};

Step runType(const rp_package& package, const rp_node_type& type, const TypeCase& c, Arena& a, std::uint32_t frames,
             Result& r) {
    PackageScope pack;
    if (const Step s = pack.take(a, package, &r.error); s != Step::Ok) return s;
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
    OneStep    one;
    const bool bound = one.bind(a, node, c, frames);
    const bool faulted = bound && one.run(frames);
    if (faulted) faultWords(r.error.message, sizeof r.error.message, "the block faulted the node", node);
    node->vt->deactivate(node);
    type.destroy(node);
    if (!bound) return Step::Arena;
    return faulted ? Step::Process : Step::Ok;
}

// What a script node's context holds (rp.script.heap/1, collected), or false for a node that has no context.
bool heapOf(rp_node* node, rp_script_heap_use* use) {
    const auto* heap = static_cast<const rp_script_heap*>(node->vt->get_interface(node, RP_SCRIPT_HEAP_INTERFACE));
    return heap && heap->use(node, true, use);
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

bool openPackage(const rp_package* package, void* memory, rp_error* err) {
    closePackage();
    if (!package->memory->setup(package, memory, err)) return false;
    gOffered = Offered{package, memory};
    return true;
}

void closePackage() {
    if (gOffered.package) gOffered.package->memory->teardown(gOffered.package, gOffered.memory);
    gOffered = Offered{};
}

void offerClock(const rp_host_clock* clock) { gClock = clock; }

std::uint32_t stimulusCount(std::uint32_t block) {
    switch (block % kStimulusCycle) {
        case 0: return RP_DEVICE_MIDI_IN_MESSAGES;
        case 1: return RP_DEVICE_MIDI_IN_BYTES / 64;
        case 4: return 0;
        default: return RP_DEVICE_MIDI_IN_MESSAGES - 1;
    }
}

std::uint32_t stimulusMessage(std::uint32_t block, std::uint32_t k, std::uint32_t frames, std::uint8_t* out,
                              std::uint32_t* frame) {
    switch (block % kStimulusCycle) {
        case 0:
            *frame = k * frames / RP_DEVICE_MIDI_IN_MESSAGES;
            out[0] = static_cast<std::uint8_t>(k % 2 ? 0x80 : 0x90);
            out[1] = static_cast<std::uint8_t>(48 + (block + k) % 24);
            out[2] = 100;
            return 3;
        case 1:
            *frame = k * frames * 64 / RP_DEVICE_MIDI_IN_BYTES;
            std::memset(out, 0, 64);
            out[0] = 0xF0;
            out[1] = 0x7D;
            out[63] = 0xF7;
            return 64;
        default: {
            const std::uint32_t sweep = block / kStimulusCycle * 2 + (block % kStimulusCycle - 2);
            *frame = (k + 1) / 2 * (frames / 32);
            return messageOf(static_cast<std::uint8_t>((sweep * (RP_DEVICE_MIDI_IN_MESSAGES - 1) + k) % 256), out);
        }
    }
}

Result run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
           void* arena, std::size_t arenaBytes, std::uint32_t frames) {
    Result r;
    run(packages, packageCount, cases, caseCount, arena, arenaBytes, frames, &r);
    return r;
}

void run(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases, std::size_t caseCount,
         void* arena, std::size_t arenaBytes, std::uint32_t frames, Result* out) {
    for (std::uint32_t c = 0; c < kBufferChannels; ++c)
        for (std::uint32_t i = 0; i < kBufferFrames; ++i) gBufferData[c][i] = signal(i, c);
    static_assert(std::is_trivially_copyable_v<Result>);
    Result& r = *out;
    std::memset(static_cast<void*>(&r), 0, sizeof r);
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
                return;
            }
            ++matched;
            Arena a{static_cast<std::byte*>(arena), arenaBytes};
            r.step = runType(*packages[p], type, *c, a, frames, r);
            if (a.used > r.arenaBytes) r.arenaBytes = a.used;
            if (r.step != Step::Ok) return;
            ++r.types;
        }
    }
    // Each case's id is a different type's, so a case left over is one no registered type took.
    if (matched != caseCount) {
        r.step = Step::CaseUnused;
        r.type = index;
        r.id = nullptr;
        return;
    }
    r.id = nullptr;
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
    const std::uint32_t loadStart = c.cycles ? c.cycles() : 0;
    graph::LoadedPlan*  plan =
        graph::loadPlan(std::span<const std::uint8_t>(c.blob, c.blobSize),
                        std::span<std::byte>(static_cast<std::byte*>(arena), arenaBytes),
                        std::span<const rp_package* const>(packages, packageCount), &kHost, &r.error);
    r.loadCycles = c.cycles ? c.cycles() - loadStart : 0;
    if (!plan) {
        r.step = PlanStep::Load;
        if (r.error.node[0] == '\0') std::strcpy(r.error.node, "-");  // the blob refused whole: no node at fault
        return;
    }
    if (c.afterLoad) c.afterLoad();
    r.nodes = static_cast<std::uint32_t>(plan->nodes.size());
    r.arenaBytes = plan->bytes;
    r.sampleRate = plan->sampleRate;
    r.maxFrames = plan->maxFrames;
    // The host's MIDI input stages nothing outside a stimulus case's blocks, and nothing once the case ends.
    gStaging = Staging{};
    auto stop = [&](PlanStep step, std::string_view node, const char* what) {
        gStaging.on = false;
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
        gStaging.on = c.stimulus;
        gStaging.block = r.blocks;
        gStaging.taken = 0;
        const std::uint32_t blockStart = c.cycles && r.blocks == 0 ? c.cycles() : 0;
        graph::runBlock(plan->view, frames, transport);
        if (c.cycles && r.blocks == 0) r.firstBlockCycles = c.cycles() - blockStart;
        ++r.blocks;
        r.frames += frames;
        r.messages = gStaging.handed;
        if (plan->faults.load(std::memory_order_relaxed) != 0) {
            for (const graph::LoadedNode& n : plan->nodes)
                if (n.faulted.load(std::memory_order_relaxed)) {
                    nodeAt(r, PlanStep::Process, n.name(), "");
                    faultWords(r.error.message, sizeof r.error.message, "the block faulted the node", n.node);
                    gStaging.on = false;
                    graph::unloadPlan(plan);
                    return;
                }
        }
    }
    gStaging.on = false;
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
    if (c.expectDigest && r.digest != c.want)
        return stop(PlanStep::Result, c.digest ? c.digest : "-", "the digest is not the one the case expects");

    // Each package the plan set memory up for, as one of its nodes reads its context.
    for (const graph::LoadedPackage& pk : plan->packages) {
        if (r.packCount == PlanResult::kPackReports) break;
        PlanResult::Pack& pack = r.packs[r.packCount++];
        const std::string_view name = pk.name();
        const std::size_t      n = name.size() < sizeof pack.name - 1 ? name.size() : sizeof pack.name - 1;
        std::memcpy(pack.name, name.data(), n);
        pack.name[n] = '\0';
        pack.memoryBytes = pk.bytes;
        for (const graph::LoadedNode& node : plan->nodes) {
            bool ours = false;
            for (std::uint32_t t = 0; t < pk.package->node_type_count && !ours; ++t)
                ours = pk.package->node_types[t] == node.type;
            rp_script_heap_use use{};
            if (!ours || !heapOf(node.node, &use)) continue;
            pack.contextBytes = use.bytes;
            pack.fixedBytes = use.fixed;
            pack.inUseBytes = use.fixed + use.heap;
            break;
        }
        if (c.contextBytes && pack.contextBytes != c.contextBytes)
            return stop(PlanStep::Heap, "-",
                        "a script pack's context is not the size the case says its image declares");
    }
    graph::unloadPlan(plan);
}

void benchScript(const rp_package* const* packages, std::size_t packageCount, const TypeCase* cases,
                 std::size_t caseCount, void* arena, std::size_t arenaBytes, std::uint32_t rounds, Cycles cycles,
                 BenchResult* out) {
    static_assert(std::is_trivially_copyable_v<BenchResult>);
    BenchResult& r = *out;
    std::memset(static_cast<void*>(&r), 0, sizeof r);
    auto fail = [&](BenchStep step, const char* what) {
        r.step = step;
        if (what) std::strncpy(r.error.message, what, sizeof r.error.message - 1);
    };
    const rp_package*   package = nullptr;
    const rp_node_type* type = nullptr;
    for (std::size_t p = 0; p < packageCount && !type; ++p)
        for (std::uint32_t t = 0; t < packages[p]->node_type_count && !type; ++t)
            if (std::strcmp(packages[p]->node_types[t]->id, kBenchType) == 0) {
                package = packages[p];
                type = packages[p]->node_types[t];
            }
    const TypeCase* c = type ? findCase(kBenchType, cases, caseCount) : nullptr;
    if (!c) return fail(BenchStep::Type, "no registered type, or no case, is the bench's");

    Arena        a{static_cast<std::byte*>(arena), arenaBytes};
    PackageScope pack;
    if (pack.take(a, *package, &r.error) != Step::Ok) return fail(BenchStep::Package, nullptr);
    const rp_settings settings{reinterpret_cast<const char*>(c->settings), c->settingsSize, RP_SETTINGS_MSGPACK};
    rp_memory_req     req{};
    if (!type->query_memory(type, &settings, &req, &r.error)) return fail(BenchStep::Create, nullptr);
    void* memory = a.raw(req.size, req.align ? req.align : alignof(std::max_align_t));
    if (!memory) return fail(BenchStep::Create, "the instance does not fit the arena");
    rp_node* node = type->create(type, &kHost, memory, &settings, &r.error);
    if (!node) return fail(BenchStep::Create, nullptr);
    if (!node->vt->activate(node, kSampleRate, kBlockFrames, &r.error)) {
        type->destroy(node);
        return fail(BenchStep::Activate, nullptr);
    }
    auto finish = [&](BenchStep step, const char* what) {
        if (step != BenchStep::Ok) fail(step, what);
        node->vt->deactivate(node);
        type->destroy(node);
    };

    // The node's first events or bytes input and output: the queues the bench fills and reads.
    OneStep one;
    if (!one.bind(a, node, *c, kBlockFrames)) return finish(BenchStep::Ports, "its ports do not fit the arena");
    rp_queue* in = nullptr;
    rp_queue* sent = nullptr;
    for (std::uint32_t i = 0; i < one.step.process.input_count && !in; ++i)
        if (one.step.process.inputs[i].kind == RP_PORT_EVENTS || one.step.process.inputs[i].kind == RP_PORT_BYTES)
            in = one.step.process.inputs[i].u.queue;
    for (std::uint32_t i = 0; i < one.step.process.output_count && !sent; ++i)
        if (one.step.process.outputs[i].kind == RP_PORT_EVENTS || one.step.process.outputs[i].kind == RP_PORT_BYTES)
            sent = one.step.process.outputs[i].u.queue;
    if (!in || !sent) return finish(BenchStep::Ports, "it has no events or bytes input and output to feed and read");

    // One block of the stimulus's `block` (none for a block of its fifth kind), timed; false if it faulted the node.
    std::uint8_t bytes[64];
    auto         block = [&](std::uint32_t stimulus, std::uint32_t* took) {
        graph::clearQueue(*in);
        const std::uint32_t count = stimulusCount(stimulus);
        for (std::uint32_t k = 0; k < count; ++k) {
            std::uint32_t       frame = 0;
            const std::uint32_t size = stimulusMessage(stimulus, k, kBlockFrames, bytes, &frame);
            graph::pushMessage(*in, frame, std::span<const std::uint8_t>(bytes, size));
        }
        const std::uint32_t start = cycles ? cycles() : 0;
        const bool          faulted = one.run(kBlockFrames, true);
        *took = cycles ? cycles() - start : 0;
        ++r.blocks;
        r.messages += in->count;
        r.passed += sent->count;
        graph::clearQueue(*sent);
        return !faulted;
    };
    char what[sizeof r.error.message];
    // The first block (none), which makes the node's instance and its pools; then the rounds: none, notes, SysEx.
    if (!block(kStimulusCycle - 1, &r.firstCycles)) {
        faultWords(what, sizeof what, "the first block faulted the node", node);
        return finish(BenchStep::Process, what);
    }
    r.noteMessages = stimulusCount(0);
    r.sysexMessages = stimulusCount(1);
    for (std::uint32_t round = 0; round < rounds; ++round) {
        static constexpr std::uint32_t kKinds[] = {kStimulusCycle - 1, 0, 1};
        std::uint64_t* totals[] = {&r.emptyCycles, &r.noteCycles, &r.sysexCycles};
        for (std::size_t k = 0; k < std::size(kKinds); ++k) {
            std::uint32_t took = 0;
            if (!block(round * kStimulusCycle + kKinds[k], &took)) {
                faultWords(what, sizeof what, "a block faulted the node", node);
                return finish(BenchStep::Process, what);
            }
            *totals[k] += took;
        }
    }
    rp_script_heap_use use{};
    if (heapOf(node, &use)) {
        r.contextBytes = use.bytes;
        r.inUseBytes = use.fixed + use.heap;
    }
    if (r.passed != r.messages)
        return finish(BenchStep::Result, "the node passed on other than every message it was handed");
    finish(BenchStep::Ok, nullptr);
}

namespace {

// A loop case's settings as the msgpack a device reads: {"loop": <its name, 31 bytes at most>, "count": <its count>,
// "warm": <its warm-up>, "length": <its strings' length>, "rounds": 0}: at most kLoopSettingsBytes.
constexpr std::uint32_t kLoopSettingsBytes = 1 + (1 + 4) + (1 + 31) + 2 * ((1 + 5) + 5) + (1 + 6) + 5 + (1 + 6) + 1;
std::uint32_t loopSettings(const LoopCase& c, std::uint8_t* out) {
    std::uint32_t n = 0;
    out[n++] = 0x85;
    out[n++] = 0xa4;
    std::memcpy(out + n, "loop", 4);
    n += 4;
    const std::uint32_t length = static_cast<std::uint32_t>(std::strlen(c.loop));
    out[n++] = static_cast<std::uint8_t>(0xa0 | length);
    std::memcpy(out + n, c.loop, length);
    n += length;
    out[n++] = 0xa5;
    std::memcpy(out + n, "count", 5);
    n += 5;
    out[n++] = 0xce;
    for (int shift = 24; shift >= 0; shift -= 8) out[n++] = static_cast<std::uint8_t>(c.count >> shift);
    out[n++] = 0xa4;
    std::memcpy(out + n, "warm", 4);
    n += 4;
    out[n++] = 0xce;
    for (int shift = 24; shift >= 0; shift -= 8) out[n++] = static_cast<std::uint8_t>(c.warm >> shift);
    out[n++] = 0xa6;
    std::memcpy(out + n, "length", 6);
    n += 6;
    out[n++] = 0xce;
    for (int shift = 24; shift >= 0; shift -= 8) out[n++] = static_cast<std::uint8_t>(c.length >> shift);
    out[n++] = 0xa6;
    std::memcpy(out + n, "rounds", 6);
    n += 6;
    out[n++] = 0x00;  // for ever
    return n;
}

// Whether a script node's fault is its process call's deadline (rp.script.fault/1): its kind, and words that name the
// process call, so a case whose instance's making ran out of time is not taken for a loop that did.
bool processDeadline(rp_node* node) {
    const auto* fault = static_cast<const rp_script_fault*>(node->vt->get_interface(node, RP_SCRIPT_FAULT_INTERFACE));
    const char* words = fault ? fault->reason(node) : nullptr;
    static constexpr char kWords[] = "process ran past its deadline";
    return words && fault->kind(node) == RP_SCRIPT_FAULT_DEADLINE &&
           std::strncmp(words, kWords, sizeof kWords - 1) == 0;
}

}  // namespace

void loopDeadline(const rp_package* package, const TypeCase* cases, std::size_t caseCount, void* arena,
                  std::size_t arenaBytes, Micros micros, LoopResult* out) {
    static_assert(std::is_trivially_copyable_v<LoopResult>);
    LoopResult& r = *out;
    std::memset(static_cast<void*>(&r), 0, sizeof r);
    auto fail = [&](LoopStep step, const char* what) {
        r.step = step;
        if (what) std::strncpy(r.error.message, what, sizeof r.error.message - 1);
    };
    const rp_node_type* type = nullptr;
    for (std::uint32_t t = 0; package && t < package->node_type_count && !type; ++t)
        if (std::strcmp(package->node_types[t]->id, kLoopType) == 0) type = package->node_types[t];
    const TypeCase* c = type ? findCase(kLoopType, cases, caseCount) : nullptr;
    if (!c) return fail(LoopStep::Type, "the package has no such type, or no case is the type's");

    Arena        a{static_cast<std::byte*>(arena), arenaBytes};
    PackageScope pack;
    if (pack.take(a, *package, &r.error) != Step::Ok) return fail(LoopStep::Package, nullptr);
    // Each case's node in the arena after the pack's memory, one at a time.
    const std::size_t afterPack = a.used;
    for (std::uint32_t k = 0; k < kLoopCount; ++k) {
        r.loop = k;
        a.used = afterPack;
        std::uint8_t      bytes[kLoopSettingsBytes];
        const rp_settings settings{reinterpret_cast<const char*>(bytes), loopSettings(kLoops[k], bytes),
                                   RP_SETTINGS_MSGPACK};
        rp_memory_req     req{};
        if (!type->query_memory(type, &settings, &req, &r.error)) return fail(LoopStep::Create, nullptr);
        void* memory = a.raw(req.size, req.align ? req.align : alignof(std::max_align_t));
        if (!memory) return fail(LoopStep::Create, "the instance does not fit the arena");
        rp_node* node = type->create(type, &kHost, memory, &settings, &r.error);
        if (!node) return fail(LoopStep::Create, nullptr);
        if (!node->vt->activate(node, kSampleRate, kBlockFrames, &r.error)) {
            type->destroy(node);
            return fail(LoopStep::Activate, nullptr);
        }
        // Its blocks until one faults it (the data's first, then the loop), the last timed.
        gLogLines = 0;
        OneStep    one;
        const bool bound = one.bind(a, node, *c, kBlockFrames);
        bool       faulted = false;
        while (bound && !faulted && r.blocks[k] < kLoopBlocks) {
            const std::uint64_t start = micros ? micros() : 0;
            faulted = one.run(kBlockFrames);
            r.micros[k] = micros ? micros() - start : 0;
            ++r.blocks[k];
        }
        char what[sizeof r.error.message];
        faultWords(what, sizeof what, "its blocks did not end in its process call's deadline", node);
        const bool ended = faulted && processDeadline(node);
        r.entered[k] = gLogLines > 0;
        rp_script_heap_use use{};
        if (k == 0 && heapOf(node, &use)) r.contextBytes = use.bytes;
        node->vt->deactivate(node);
        type->destroy(node);
        if (!bound) return fail(LoopStep::Ports, "its ports do not fit the arena");
        if (!ended) return fail(LoopStep::Result, what);
        if (!r.entered[k]) return fail(LoopStep::Result, "its deadline faulted it before its loop began");
    }
}

namespace {

// The policy's ports: an events input of the device MIDI input's capacity, and an output that matches it.
constexpr PortCase kPolicyPorts[] = {
    {RP_PORT_EVENTS, false, 0, RP_DEVICE_MIDI_IN_MESSAGES, RP_DEVICE_MIDI_IN_BYTES},
    {RP_PORT_EVENTS, true, 0, RP_DEVICE_MIDI_IN_MESSAGES, RP_DEVICE_MIDI_IN_BYTES}};
constexpr std::uint8_t kPolicySettings[] = {0x80};  // {}: it has none
constexpr TypeCase     kPolicyCase{kPolicyType, kPolicySettings, sizeof kPolicySettings, kPolicyPorts, 2, nullptr, 0};

// A note on, made where it lies in the queue.
void pushNote(rp_queue& q, std::uint32_t k) {
    const std::uint8_t note[3] = {0x90, static_cast<std::uint8_t>(36 + k % 48), 100};
    graph::pushMessage(q, k, std::span<const std::uint8_t>(note, 3));
}

// A SysEx of `size` bytes (F0, data, F7), written where it lies in the queue: the largest is the queue's 4096 bytes,
// which no stack of the box's could hold to copy from.
void pushSysEx(rp_queue& q, std::uint32_t frame, std::uint32_t size) {
    if (q.count == q.capacity || size < 2 || q.bytes_capacity - q.bytes_used < size) return;
    std::uint8_t* p = q.bytes + q.bytes_used;
    p[0] = 0xF0;
    for (std::uint32_t i = 1; i + 1 < size; ++i) p[i] = static_cast<std::uint8_t>((frame + i) & 0x7F);
    p[size - 1] = 0xF7;
    q.items[q.count++] = rp_message{frame, size, q.bytes_used, 0};
    q.bytes_used += size;
}

// Block `b` of the run policyHeap describes, into `q`.
void policyBlock(rp_queue& q, std::uint32_t b) {
    graph::clearQueue(q);
    if (b == 0) {
        for (std::uint32_t k = 0; k < q.capacity; ++k) pushNote(q, k);
    } else if (b <= 63) {
        const std::uint32_t notes = 64 - b;
        for (std::uint32_t k = 0; k < notes; ++k) pushNote(q, k);
        pushSysEx(q, notes, q.bytes_capacity - 3 * notes);
    } else if (b == 64) {
        for (std::uint32_t k = 0; k < 63; ++k) pushSysEx(q, k, 64);
    } else if (b == 65) {
        pushSysEx(q, 0, q.bytes_capacity);
    } else {
        for (std::uint32_t k = 0; k < q.capacity; ++k) pushSysEx(q, k, 64);
    }
}

}  // namespace

void policyHeap(const rp_package* package, void* arena, std::size_t arenaBytes, PolicyResult* out) {
    static_assert(std::is_trivially_copyable_v<PolicyResult>);
    PolicyResult& r = *out;
    std::memset(static_cast<void*>(&r), 0, sizeof r);
    auto fail = [&](PolicyStep step, const char* what) {
        r.step = step;
        if (what) std::strncpy(r.error.message, what, sizeof r.error.message - 1);
    };
    const rp_node_type* type = nullptr;
    for (std::uint32_t t = 0; package && t < package->node_type_count && !type; ++t)
        if (std::strcmp(package->node_types[t]->id, kPolicyType) == 0) type = package->node_types[t];
    if (!type) return fail(PolicyStep::Type, "the package has no such type");

    Arena        a{static_cast<std::byte*>(arena), arenaBytes};
    PackageScope pack;
    if (pack.take(a, *package, &r.error) != Step::Ok) return fail(PolicyStep::Package, nullptr);
    const rp_settings settings{reinterpret_cast<const char*>(kPolicySettings), sizeof kPolicySettings,
                               RP_SETTINGS_MSGPACK};
    rp_memory_req     req{};
    if (!type->query_memory(type, &settings, &req, &r.error)) return fail(PolicyStep::Create, nullptr);
    void* memory = a.raw(req.size, req.align ? req.align : alignof(std::max_align_t));
    if (!memory) return fail(PolicyStep::Create, "the instance does not fit the arena");
    rp_node* node = type->create(type, &kHost, memory, &settings, &r.error);
    if (!node) return fail(PolicyStep::Create, nullptr);
    if (!node->vt->activate(node, kSampleRate, kBlockFrames, &r.error)) {
        type->destroy(node);
        return fail(PolicyStep::Activate, nullptr);
    }
    auto finish = [&](PolicyStep step, const char* what) {
        if (step != PolicyStep::Ok) fail(step, what);
        node->vt->deactivate(node);
        type->destroy(node);
    };
    OneStep one;
    if (!one.bind(a, node, kPolicyCase, kBlockFrames))
        return finish(PolicyStep::Ports, "its ports do not fit the arena");
    rp_queue& in = *one.step.process.inputs[0].u.queue;
    rp_queue& sent = *one.step.process.outputs[0].u.queue;
    char      what[sizeof r.error.message];
    rp_script_heap_use use{};
    for (std::uint32_t b = 0; b < kPolicyBlocks; ++b) {
        policyBlock(in, b);
        const bool faulted = one.run(kBlockFrames, true);
        ++r.blocks;
        r.messages += in.count;
        r.passed += sent.count;
        graph::clearQueue(sent);
        if (faulted) {
            faultWords(what, sizeof what, "a block faulted the node", node);
            return finish(PolicyStep::Process, what);
        }
        if (b == 0 && heapOf(node, &use)) r.firstInUseBytes = use.fixed + use.heap;
    }
    if (heapOf(node, &use)) {
        r.contextBytes = use.bytes;
        r.inUseBytes = use.fixed + use.heap;
    }
    if (r.passed != r.messages) return finish(PolicyStep::Result, "the node passed on other than every message");
    finish(PolicyStep::Ok, nullptr);
}

}  // namespace rp::device_check

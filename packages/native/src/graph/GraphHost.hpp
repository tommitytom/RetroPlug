#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "graph/GraphDocument.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PlanStorage.hpp"
#include "graph/Planner.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/rp_node.h"
#include "transport/SpscRing.hpp"

namespace rp::graph {

// A host service a node asks for by id (rp_host.get_interface), offered by whoever embeds the GraphHost. [any]
using HostInterfaceFn = const void* (*)(void* ctx, const char* id);

struct HostConfig {
    double        sampleRate = 48000.0;
    std::uint32_t maxFrames = 1024;
    Target        target = Target::Desktop;
    // How long setGraph waits for a stalled audio thread to adopt a published settings commit (to a node whose
    // prepare reads its current settings) before answering busy.
    std::uint32_t adoptionTimeoutMs = 2000;
    // The embedder's own host services; GraphHost answers its own ids (rp.host.audio-buffers/1) first.
    HostInterfaceFn interfaces = nullptr;
    void*           interfacesCtx = nullptr;
};

// The host's output buffers for this block, which rp.host.audio-out copies into. [audio]
#define RP_HOST_AUDIO_BUFFERS_INTERFACE "rp.host.audio-buffers/1"
struct rp_host_audio_buffers {
    float* const* (*outputs)(const rp_host* host, std::uint32_t* count);
};

// An embedder's command, carried on the ring in push order with plan swaps and params, and handed back to
// the embedder's handler at drain with the host data of the plan current at that point. Opaque here: the
// payload is trivially copyable, at most kHostCommandBytes, aligned no more than alignof(std::max_align_t),
// and is copied in by memcpy, so the handler reads it back by memcpy too.
constexpr std::size_t kHostCommandBytes = 48;
struct HostCommand {
    alignas(std::max_align_t) std::byte bytes[kHostCommandBytes];
};
static_assert(sizeof(HostCommand) == kHostCommandBytes, "the payload is exactly kHostCommandBytes");
// Runs on the audio thread at drain [audio], or on the control thread during a quiescent inline flush and in
// discardPending. It must not block, and must not call back into the GraphHost: a setGraph from a quiescent
// flush would nest a drain and a reclaim, which can free the plan whose host data it was handed.
// `discard`: the command was dropped unapplied (discardPending or teardown), so free what it owns instead.
// A plan's trailer (setGraph) is applied with its plan, which is always adopted, even in discardPending; only
// the GraphHost's own destructor hands it back as discarded.
using HostCommandHandler = void (*)(void* ctx, const HostCommand& command, const void* hostData, bool discard);

constexpr std::uint32_t kMaxBlockFrames = 1u << 16;

// Why a rate / block size cannot be used (a positive finite rate, 1..kMaxBlockFrames frames), or nullopt.
std::optional<std::string> checkConfig(double sampleRate, std::uint32_t maxFrames);

struct SetGraphResult {
    bool                               ok = false;
    std::vector<PlanError>             errors;
    std::map<std::string, std::string> layouts;
};

// Owns a running graph: the live node instances, the wire-owned feedback buffers, and the plan the audio
// thread is executing. Mirrors the engine's existing threading contract (spec/01 section 7):
//
//   - every control -> audio change is a POD command on ONE SPSC ring (a plan swap, a live param value,
//     an embedder's host command);
//   - the audio thread never frees: a replaced plan goes back on the release ring and is destroyed by
//     reclaim() on the control thread (and with it any instance it alone kept alive);
//   - audioThreadOwns decides who drains. While false the control thread flushes inline after every push,
//     so the ring is empty at every handoff.
//
// It never plans while a published settings commit to a node whose prepare reads its current settings is
// unadopted: setGraph first waits (about one block while audio runs, up to a timeout if it has stalled) for
// the audio thread to adopt it, so the planner never asks such a node to prepare settings while its last
// commit may be running. A plan with no such commit (none at all, or only commits to types that declare
// RP_TYPE_STATELESS_PREPARE) needs no wait, so several can sit in the ring at once, adopted in order in one
// drain.
class GraphHost {
public:
    // Throws std::invalid_argument for a config checkConfig refuses.
    GraphHost(const NodeRegistry& registry, ResourceTable& resources, HostConfig config = {});
    ~GraphHost();  // the audio thread must have stopped (setAudioThreadOwns(false))
    GraphHost(const GraphHost&) = delete;
    GraphHost& operator=(const GraphHost&) = delete;

    // ---- control thread ------------------------------------------------------------------------------
    SetGraphResult setGraph(std::string_view documentJson);
    // `hostData` rides with the plan: the handler and hostData() see the one of the plan current at the
    // time, and it is freed with that plan on reclaim (control thread). `trailer`, a host command, rides in
    // the plan's own swap: the handler gets it in the same pop that adopts the plan, with that plan's host
    // data, so no drain ever runs a block between the two, and a refused setGraph applies neither.
    SetGraphResult setGraph(const GraphDocument& document, std::shared_ptr<const void> hostData = nullptr,
                            const HostCommand* trailer = nullptr);
    // Queue an embedder command behind everything pushed so far. False when the ring is full or `size`
    // exceeds kHostCommandBytes. Flushes inline while quiescent, as every push does.
    bool pushHostCommand(const void* data, std::size_t size);
    // Call before setAudioThreadOwns(true): it writes plain members that drain() reads.
    void setHostCommandHandler(HostCommandHandler handler, void* ctx);
    // A live param change, clamped to the param's resolved range (and rounded for int, enum and bool, as a
    // document value is) and applied at `frame` of the next block. False for an unknown node or param, a
    // NaN value, or a param a wire drives.
    bool setParam(std::string_view nodeId, std::string_view paramId, double value, std::uint32_t frame = 0);
    // Set true BEFORE the audio thread starts calling processBlock, false AFTER it has stopped.
    void setAudioThreadOwns(bool owns);
    // Teardown, once the audio thread has stopped and before setAudioThreadOwns(false) would apply what is
    // left: adopt every queued plan (applying its trailer, the rest of the same edit) and hand every queued
    // host command back as discarded.
    void discardPending();
    bool audioThreadOwns() const { return audioOwns_.load(std::memory_order_acquire); }
    // Destroy plans the audio thread has handed back. Returns how many.
    std::size_t reclaim();
    // Quiescent only: re-plan the current document at a new rate / block size, then re-activate every
    // instance at it (preserving state), then publish. False, with a reason, if the document no longer
    // plans (nothing has changed) or a node will not activate: every instance goes back to the old rate and
    // size, the old plan keeps running, and a node that will not come back either is bypassed as faulted.
    bool reactivate(double sampleRate, std::uint32_t maxFrames, std::string* error = nullptr);
    rp_node*      node(std::string_view id) const;
    // Nodes faulted so far, each counted once. reclaim() logs new ones, naming each still in the graph. Acquired:
    // the executor releases the count after the node's flag, so every flag it counts is visible after reading it.
    std::uint32_t faultCount() const { return faults_.load(std::memory_order_acquire); }
    // Has this node faulted (and so been bypassed since)? False for an unknown id.
    bool          isFaulted(std::string_view id) const;
    // Messages dropped so far because an events or bytes output queue was full (each holds what its port
    // declares, the defaults otherwise), as published by the thread that runs blocks at the end of each one: the
    // queues' own counts are only ever touched there. An unconnected output's are not counted, since nothing reads
    // it, nor is a control stream's (a full stream keeps its end value right, and counts in its own `dropped`).
    // reclaim() names each node still in the graph the first time its outputs drop. Acquired, as faultCount is:
    // a node's count read after it includes the node's share of what it read. The counts are 32-bit and wrap,
    // rather than saturate: the executor's add stays one wait-free fetch_add, and what reads them compares or
    // subtracts (reclaim's "new since", a test's "this many more"), which a wrap leaves right.
    std::uint32_t droppedMessages() const { return drops_.load(std::memory_order_acquire); }
    // The same for one node's outputs, over the life of its instance; 0 for an unknown id. It wraps as the total
    // does.
    std::uint32_t droppedMessages(std::string_view id) const;
    std::size_t   instanceCount() const { return live_.size(); }
    // Replaced plans the audio thread could not hand back (the release ring was full) and so leaked
    // rather than free on its own thread. Cannot happen while released_ is twice the command ring: setGraph
    // reclaims before every publish, so between two reclaims the audio thread releases at most the ring's
    // usable depth plus one (the plan current when the queue was filled). Kept as a counter in case that
    // sizing is ever broken; reclaim() logs it.
    std::uint32_t leakedPlans() const { return leakedPlans_.load(std::memory_order_relaxed); }

    // ---- audio thread (or the control thread while quiescent) ----------------------------------------
    // Apply every queued command in order: plan swaps, params and host commands.
    void drain();
    // Run one block of the current plan. Every host output starts the block silent; rp.host.audio-out
    // writes the buses it has. A block longer than the running plan's buffers is refused (nothing runs,
    // the outputs stay silent) rather than overrun them.
    void run(std::uint32_t frames, const rp_transport& transport, float* const* outputs = nullptr,
             std::uint32_t outputCount = 0);
    // drain() then run().
    void processBlock(std::uint32_t frames, const rp_transport& transport, float* const* outputs = nullptr,
                      std::uint32_t outputCount = 0);
    // The running plan's host data (null before the first plan, or for a plan given none).
    const void* hostData() const;

    const HostConfig& config() const { return config_; }

private:
    struct Command {
        enum class Kind : std::uint8_t { SwapPlan, SetParam, Host };
        Kind              kind = Kind::SwapPlan;
        bool              trailer = false;  // SwapPlan: `host` is the plan's trailer
        PlanStorage*      plan = nullptr;
        rp_control_queue* queue = nullptr;
        std::uint32_t     frame = 0;
        double            value = 0.0;
        HostCommand       host{};           // Host: the command; SwapPlan: the trailer, if any
    };
    static_assert(std::is_trivially_copyable_v<Command>, "the ring copies a Command as bytes");

    bool        push(const Command& c);
    void        drainCommands(bool discard, bool applyTrailers = true);
    void        flushIfQuiescent();
    bool        waitForCommits();
    static const void* hostInterface(const rp_host* host, const char* id);
    static float* const* audioBuffers(const rp_host* host, std::uint32_t* count);
    PlanOutcome planDocument(const GraphDocument& document, double sampleRate, std::uint32_t maxFrames);
    bool        publish(PlanOutcome& outcome, const GraphDocument& document, const HostCommand* trailer = nullptr);
    void        markFaulted(Instance& inst);

    // Named rather than read back off commands_: a decltype of a non-static member inside the class body
    // has been fragile under MSVC.
    static constexpr std::size_t kCommandCapacity = 256;

    const NodeRegistry&                                   registry_;
    ResourceTable&                                        resources_;
    HostConfig                                            config_;
    rp_host                                               host_{};
    SpscRing<Command, kCommandCapacity>                   commands_;
    // Deeper than the command ring: a drain can adopt every queued swap and release all but the last, on
    // top of the plan they replaced (see leakedPlans()).
    SpscRing<PlanStorage*, 2 * kCommandCapacity>          released_;
    std::atomic<bool>                                     audioOwns_{false};
    // Published plans that await adoption (PlanStorage::awaitsAdoption) and that the audio thread has not
    // finished adopting.
    std::atomic<std::uint32_t>                            commitsInFlight_{0};
    HostCommandHandler                                    hostHandler_ = nullptr;
    void*                                                 hostHandlerCtx_ = nullptr;
    std::atomic<std::uint32_t>                            faults_{0};
    std::atomic<std::uint32_t>                            drops_{0};
    std::atomic<std::uint32_t>                            leakedPlans_{0};
    std::uint32_t                                         leaksReported_ = 0;  // control side
    std::uint32_t                                         faultsReported_ = 0; // control side
    std::uint32_t                                         dropsReported_ = 0;  // control side
    PlanStorage*                                          current_ = nullptr;  // audio side
    GraphDocument                                         document_;           // control side: last published
    std::shared_ptr<const void>                           hostData_;           // control side: its host data
    std::map<std::string, std::shared_ptr<Instance>>      live_;               // control side
    std::map<std::string, std::shared_ptr<FeedbackFront>> feedback_;           // control side
    float* const*                                         hostOutputs_ = nullptr;
    std::uint32_t                                         hostOutputCount_ = 0;
};

}  // namespace rp::graph

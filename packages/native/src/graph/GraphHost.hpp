#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "graph/GraphDocument.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PlanStorage.hpp"
#include "graph/Planner.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/rp_node.h"
#include "transport/SpscRing.hpp"

namespace rp::graph {

struct HostConfig {
    double        sampleRate = 48000.0;
    std::uint32_t maxFrames = 1024;
    Target        target = Target::Desktop;
    // How long setGraph waits for a stalled audio thread to adopt the previous plan before answering busy.
    std::uint32_t adoptionTimeoutMs = 2000;
};

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
//   - every control -> audio change is a POD command on ONE SPSC ring (a plan swap, a live param value);
//   - the audio thread never frees: a replaced plan goes back on the release ring and is destroyed by
//     reclaim() on the control thread (and with it any instance it alone kept alive);
//   - audioThreadOwns decides who drains. While false the control thread flushes inline after every push,
//     so the ring is empty at every handoff.
//
// It never plans while a swap is in flight: setGraph first waits (about one block while audio runs, up to
// a timeout if it has stalled) for the audio thread to adopt the previous plan, so the planner never reads
// node state the audio thread is writing.
class GraphHost {
public:
    // Throws std::invalid_argument for a config checkConfig refuses.
    GraphHost(const NodeRegistry& registry, ResourceTable& resources, HostConfig config = {});
    ~GraphHost();  // the audio thread must have stopped (setAudioThreadOwns(false))
    GraphHost(const GraphHost&) = delete;
    GraphHost& operator=(const GraphHost&) = delete;

    // ---- control thread ------------------------------------------------------------------------------
    SetGraphResult setGraph(std::string_view documentJson);
    SetGraphResult setGraph(const GraphDocument& document);
    // A live param change, clamped to the param's resolved range (and rounded for int, enum and bool, as a
    // document value is) and applied at `frame` of the next block. False for an unknown node or param, a
    // NaN value, or a param a wire drives.
    bool setParam(std::string_view nodeId, std::string_view paramId, double value, std::uint32_t frame = 0);
    // Set true BEFORE the audio thread starts calling processBlock, false AFTER it has stopped.
    void setAudioThreadOwns(bool owns);
    bool audioThreadOwns() const { return audioOwns_.load(std::memory_order_acquire); }
    // Destroy plans the audio thread has handed back. Returns how many.
    std::size_t reclaim();
    // Quiescent only: re-plan the current document at a new rate / block size, then re-activate every
    // instance at it (preserving state), then publish. False, with a reason, if the document no longer
    // plans (nothing has changed) or a node will not activate: every instance goes back to the old rate and
    // size, the old plan keeps running, and a node that will not come back either is bypassed as faulted.
    bool reactivate(double sampleRate, std::uint32_t maxFrames, std::string* error = nullptr);
    rp_node*      node(std::string_view id) const;
    std::uint32_t faultCount() const { return faults_.load(std::memory_order_relaxed); }
    // Has this node faulted (and so been bypassed since)? False for an unknown id.
    bool          isFaulted(std::string_view id) const;
    std::size_t   instanceCount() const { return live_.size(); }
    // Replaced plans the audio thread could not hand back (the release ring was full) and so leaked
    // rather than free on its own thread. Never expected; reclaim() logs it when it happens.
    std::uint32_t leakedPlans() const { return leakedPlans_.load(std::memory_order_relaxed); }

    // ---- audio thread (or the control thread while quiescent) ----------------------------------------
    // Drain commands (adopting a new plan if one is waiting), then run one block. Every host output starts
    // the block silent; rp.host.audio-out writes the buses it has. A block longer than the running plan's
    // buffers is refused (nothing runs, the outputs stay silent) rather than overrun them.
    void processBlock(std::uint32_t frames, const rp_transport& transport, float* const* outputs = nullptr,
                      std::uint32_t outputCount = 0);

    // Read by host-implemented nodes during processBlock.
    float* const* hostOutputs() const { return hostOutputs_; }
    std::uint32_t hostOutputCount() const { return hostOutputCount_; }

    const HostConfig& config() const { return config_; }

private:
    struct Command {
        enum class Kind : std::uint8_t { SwapPlan, SetParam };
        Kind              kind = Kind::SwapPlan;
        PlanStorage*      plan = nullptr;
        rp_control_queue* queue = nullptr;
        std::uint32_t     frame = 0;
        double            value = 0.0;
    };

    bool        push(const Command& c);
    void        drain();
    void        flushIfQuiescent();
    bool        waitForAdoption();
    PlanOutcome planDocument(const GraphDocument& document, double sampleRate, std::uint32_t maxFrames);
    bool        publish(PlanOutcome& outcome, const GraphDocument& document);
    void        markFaulted(Instance& inst);

    const NodeRegistry&                                   registry_;
    ResourceTable&                                        resources_;
    HostConfig                                            config_;
    rp_host                                               host_{};
    SpscRing<Command, 256>                                commands_;
    SpscRing<PlanStorage*, 64>                            released_;
    std::atomic<bool>                                     audioOwns_{false};
    std::atomic<bool>                                     swapInFlight_{false};
    std::atomic<std::uint32_t>                            faults_{0};
    std::atomic<std::uint32_t>                            leakedPlans_{0};
    std::uint32_t                                         leaksReported_ = 0;  // control side
    PlanStorage*                                          current_ = nullptr;  // audio side
    GraphDocument                                         document_;           // control side: last published
    std::map<std::string, std::shared_ptr<Instance>>      live_;               // control side
    std::map<std::string, std::shared_ptr<FeedbackFront>> feedback_;           // control side
    float* const*                                         hostOutputs_ = nullptr;
    std::uint32_t                                         hostOutputCount_ = 0;
};

}  // namespace rp::graph

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "graph/GraphDocument.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/PlanStorage.hpp"
#include "graph/ResourceTable.hpp"

namespace rp::graph {

// Which capability rules apply: the Pico refuses nodes that need a heap or threads.
enum class Target : std::uint8_t { Desktop, Pico };

struct PlanError {
    std::string nodeId;
    std::string port;
    std::string code;
    std::string message;
};

struct PlanInput {
    const GraphDocument*                                         doc = nullptr;
    const NodeRegistry*                                          registry = nullptr;
    ResourceTable*                                               resources = nullptr;
    const std::map<std::string, std::shared_ptr<Instance>>*      live = nullptr;
    const std::map<std::string, std::shared_ptr<FeedbackFront>>* feedback = nullptr;
    const rp_host*                                               host = nullptr;
    double                                                       sampleRate = 48000.0;
    std::uint32_t                                                maxFrames = 1024;
    Target                                                       target = Target::Desktop;
    std::atomic<std::uint32_t>*                                  faults = nullptr;
    std::atomic<std::uint32_t>*                                  drops = nullptr;
    // True when no audio thread is running the current plan, so the planner may read buffers it uses.
    bool                                                         quiescent = true;
};

struct PlanOutcome {
    std::vector<PlanError>                                errors;
    std::unique_ptr<PlanStorage>                          storage;    // null when errors is non-empty
    std::map<std::string, std::shared_ptr<Instance>>      instances;  // the live table once published
    std::map<std::string, std::shared_ptr<FeedbackFront>> feedback;   // the wire-owned buffers once published
    std::map<std::string, std::string>                    layouts;    // node id -> canonical layout JSON
    // Control-side bookkeeping to run only once the plan has actually been published.
    std::vector<std::function<void()>>                    onPublished;
};

// A node's rp_error text, bounded (a node may leave it unterminated) and never empty.
std::string errorText(const rp_error& err, const char* fallback);

// The planner passes (docs/modular-graph-design.md section 5.2): validate the document on the types'
// layouts, order and check cycles, then reconcile instances, then assign buffers. Runs on the control
// thread and may allocate; never touches a node the audio thread is running except through the ABI's
// control-thread calls. A reshaped instance gets a new PortState, sharing by id the streams it keeps, which
// becomes the instance's only when the plan is published; the old one stays as the running plan uses it. A
// failed plan has no effect: instances it created are destroyed and settings it prepared are released before
// it returns.
PlanOutcome plan(const PlanInput& in);

}  // namespace rp::graph

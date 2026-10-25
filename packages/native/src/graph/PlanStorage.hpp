#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "graph/Descriptor.hpp"
#include "graph/NodeRegistry.hpp"
#include "graph/Plan.hpp"
#include "graph/ResourceTable.hpp"
#include "graph/rp_node.h"

namespace rp::graph {

// Control-thread-owned structures behind a plan. Everything the audio thread touches through a PlanView
// lives in one of these, with addresses fixed before the plan is published.

// A fixed-capacity control stream with a stable address.
struct StreamStore {
    rp_control_queue               queue{};
    std::vector<rp_control_change> changes;
    StreamStore(std::uint32_t capacity, double start);
};

// A fixed-capacity events / bytes queue with a stable address.
struct QueueStore {
    rp_queue                  queue{};
    std::vector<rp_message>   items;
    std::vector<std::uint8_t> bytes;
    QueueStore(std::uint32_t itemCapacity, std::uint32_t byteCapacity);
};

// What an instance holds for ONE layout: the layout, the streams (params, control outputs) and resource
// output slots laid out by it, and the control side's records of what it last sent them. They belong to the
// instance, not to any plan, so a swap that reuses the instance keeps every value they hold. A reshape gives
// the instance a new PortState, built on the control thread while the running plan still uses the old one:
// each stream and slot whose param or port persists by id is shared into it, never copied, and the rest are
// new. Every plan holds the PortState of each instance it runs, so the old one outlives the plan that uses it.
struct PortState {
    Layout                                     layout;
    std::vector<std::shared_ptr<StreamStore>>  params;           // one per layout param
    std::vector<std::shared_ptr<StreamStore>>  controlOutputs;   // per output ordinal; null unless control
    std::vector<std::shared_ptr<rp_resource*>> resourceOutputs;  // per output ordinal; null unless resource
    // Control side only.
    std::map<std::string, double>              appliedParams;    // document values already sent
    std::vector<std::uint8_t>                  wiredParams;      // per param: driven by a wire, so setParam is refused
    std::vector<std::uint8_t>                  lastMask;
    bool                                       maskSent = false;
};

// One live node instance.
struct Instance {
    std::string                               id;
    const NodeRegistry::Entry*                entry = nullptr;
    rp_node*                                  node = nullptr;
    void*                                     memory = nullptr;
    std::size_t                               align = 0;
    std::string                               settings;  // canonical JSON as the control thread last applied it
    // Control side: the ports of the last published plan, which a reshape replaces once its plan is published.
    // The audio thread never reads this; it reaches the streams through the plan it runs.
    std::shared_ptr<PortState>                ports;
    std::vector<rp_node*>                     lastPeers;
    bool                                      peersSent = false;
    bool                                      active = false;
    // Set by the executor (audio thread) when the node faults; it survives every later plan swap, which
    // is what keeps a wedged node bypassed. Read by GraphHost::isFaulted on the control thread.
    std::atomic<bool>                         faulted{false};
    bool                                      faultReported = false;  // control side: GraphHost::reclaim named it
    // Messages this instance's full output queues have dropped: added by the executor after each block, from
    // the queues' own counts, which only the thread running the block touches. Read by
    // GraphHost::droppedMessages on the control thread.
    std::atomic<std::uint32_t>                drops{0};
    bool                                      dropsReported = false;  // control side: GraphHost::reclaim named it

    Instance() = default;
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
    ~Instance();  // control thread: deactivate, destroy, free
};

// The consumer side of a feedback wire: what it reads this block is what the producer wrote last block.
// Owned by the wire (shared with every plan that has the wire), so a swap keeps the block in flight.
struct FeedbackFront {
    PortKind                     kind = PortKind::Audio;
    std::uint32_t                channels = 0;
    std::uint32_t                frames = 0;  // audio: how many frames each channel holds
    std::vector<float>           samples;
    std::vector<float*>          pointers;
    std::unique_ptr<QueueStore>  queue;
    std::unique_ptr<StreamStore> stream;
};

// Events and bytes queues are sized by their ports (QueueCapacity, Descriptor.hpp); every control stream holds this
// many changes a block.
constexpr std::uint32_t kStreamChanges = 64;

struct PlanStorage {
    // Every buffer in the plan is this many frames long; the block driver refuses a longer block.
    std::uint32_t                               maxFrames = 0;

    // Ownership: every instance, port state and feedback buffer the plan references stays alive until the
    // plan does.
    std::vector<std::shared_ptr<Instance>>      instances;
    std::vector<std::shared_ptr<PortState>>     ports;  // the one each instance was planned with
    std::vector<std::shared_ptr<FeedbackFront>> feedbackFronts;
    ResourceTable*                              resources = nullptr;
    std::vector<ResourceTable::Id>              pins;

    // Buffers.
    std::vector<std::unique_ptr<float[]>>       audio;          // each maxFrames long
    std::unique_ptr<float[]>                    silence;
    std::unique_ptr<float[]>                    scratch;
    std::vector<std::unique_ptr<float*[]>>      channelArrays;  // float* const* targets for views
    std::vector<std::unique_ptr<QueueStore>>    queues;
    std::unique_ptr<QueueStore>                 emptyQueue;     // unconnected inputs: always empty
    std::unique_ptr<QueueStore>                 scratchQueue;   // unconnected outputs: cleared each block
    std::unique_ptr<StreamStore>                emptyStream;
    std::vector<std::unique_ptr<rp_resource*>>  resourceSlots;

    // The flat plan.
    std::vector<std::vector<rp_port_view>>      inputViews;
    std::vector<std::vector<rp_port_view>>      outputViews;
    std::vector<std::vector<rp_control_queue*>> paramViews;
    std::vector<PlanStep>                       steps;
    std::vector<OutputQueue>                    queuesToClear;
    std::vector<rp_control_queue*>              controlsToRoll;
    std::vector<FeedbackCopy>                   feedback;
    PlanView                                    view;
    // The embedder's data for this plan (GraphHost::setGraph), freed with it on the control thread.
    std::shared_ptr<const void>                 hostData;

    // Applied by the audio thread when it adopts the plan, before the first block.
    struct SettingsCommit {
        rp_node* node = nullptr;
        void*    prepared = nullptr;
        void*    old = nullptr;
        bool     committed = false;
    };
    struct PortsChanged {
        rp_node*                  node = nullptr;
        std::vector<std::uint8_t> mask;
    };
    struct Peers {
        rp_node*                 node = nullptr;
        const rp_link_interface* link = nullptr;
        std::vector<rp_node*>    peers;
    };
    struct ParamSet {
        rp_control_queue* queue = nullptr;
        double            value = 0.0;
    };
    std::vector<SettingsCommit> commits;
    std::vector<PortsChanged>   portsChanged;
    std::vector<Peers>          peers;
    std::vector<ParamSet>       paramSets;
    // A commit here goes to a node whose prepare may read its current settings (a type without
    // RP_TYPE_STATELESS_PREPARE), so the host must not plan again until this plan has been adopted.
    bool                        awaitsAdoption = false;

    PlanStorage() = default;
    PlanStorage(const PlanStorage&) = delete;
    PlanStorage& operator=(const PlanStorage&) = delete;
    // Control thread: frees settings objects (the replaced ones if committed, the unused prepared ones if
    // not), then drops its instances (destroying those it alone kept, while its buffers, port states and pins
    // still exist), then its resource pins. Port states and feedback buffers go with their last owner.
    ~PlanStorage();

    // Audio thread, at adoption: commit settings, tell nodes their peers and connected outputs, apply
    // document param values. Alloc-free.
    void adopt();
};

}  // namespace rp::graph

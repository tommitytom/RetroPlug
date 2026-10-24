#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "host/graph/SystemTable.hpp"
#include "system/AudioRouting.hpp"
#include "system/SystemTypes.hpp"

class SystemBase;

namespace rp::host {

// The output shape a document is built for (docs/modular-graph-phase-b1-design.md section 4.4): the host's
// own outputs routed by the project's mode (Multi), each system on its own pair (PerSystem), or one
// system's streams on a pair each (PerChannel).
struct HostShape {
    enum class Kind : std::uint8_t { Multi, PerSystem, PerChannel };
    Kind          kind = Kind::Multi;
    std::uint32_t channels = 2;  // Multi only: the host's output count
    bool operator==(const HostShape&) const = default;
};

// Turns today's structural calls into graph documents (section 4). Control thread only. It mirrors
// Project: the ordered system list, each Game Boy's link group, the routing mode and the host shape, and
// after every change it builds the document and its system table and hands both to `publish` (the
// engine's GraphHost::setGraph), which answers whether the document planned.
//
// A built core is parked here under a token until the system node created for it takes it (take()). A
// core it frees itself (one never adopted) goes through `released` first, as a node's destroy does.
class Translator {
public:
    using Publish = std::function<bool(const std::string& documentJson, std::shared_ptr<const SystemTable> table)>;
    Translator(Publish publish, std::function<void(SystemBase*)> released);
    ~Translator();  // frees any core still parked
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    // Each returns false when the change could not be planned (the model is then left as it was, and a
    // core that was handed in is freed).
    bool add(std::unique_ptr<SystemBase> core);
    bool replace(SystemId id, std::unique_ptr<SystemBase> core);  // keeps id's slot; false if id is absent
    bool remove(SystemId id);
    bool setRouting(AudioRouting mode);
    bool setLinkGroup(SystemId id, std::uint8_t group);  // a no-op when unchanged, as today
    bool setShape(HostShape shape);                        // a no-op when unchanged

    AudioRouting routing() const { return routing_; }
    const HostShape& shape() const { return shape_; }
    std::size_t  systemCount() const { return systems_.size(); }
    // The core of `id`, for the control-thread readers that may touch a core only while no audio thread
    // runs (the debug facet, the per-channel render's layout). Null for an unknown id.
    SystemBase*  find(SystemId id) const;
    // Every core, in slot order (a sample-rate change re-rates them, quiescent only).
    std::vector<SystemBase*> cores() const;

    // rp.host.cores/1: the core parked under `token`, now the caller's.
    SystemBase* take(std::uint64_t token);

    // What the model plans as: the document's JSON and the table that rides with it. Public for tests.
    std::string documentJson() const;
    SystemTable table() const;

private:
    struct System {
        SystemId      id = 0;
        SystemKind    kind = SystemKind::SameBoy;
        SystemBase*   core = nullptr;  // parked here, or owned by its node
        std::uint64_t token = 0;
        std::uint8_t  linkGroup = 0;   // Game Boys only; 0 = unlinked
        std::uint32_t buildMode = 0;   // NES only: the channel export mode it was built with
    };

    System entryFor(std::unique_ptr<SystemBase> core);
    void   unpark(std::uint64_t token);  // free a parked core the plan never took
    bool   publish();

    // The order systems are summed and stepped in: unlinked by slot, then each link group (by first
    // appearance) with its members by slot. Indices into systems_.
    std::vector<std::size_t>              sumOrder() const;
    std::vector<std::vector<std::size_t>> linkGroups() const;
    // The export mode a NES has once today's syncSplitPlan has armed or restored it.
    std::uint32_t                         nesMode(const System& s) const;

    Publish                                            publish_;
    std::function<void(SystemBase*)>                   released_;
    std::vector<System>                                systems_;  // slot order
    std::map<std::uint64_t, std::unique_ptr<SystemBase>> parked_;
    std::uint64_t                                      nextToken_ = 1;
    AudioRouting                                       routing_ = AudioRouting::Stereo;
    HostShape                                          shape_;
};

}  // namespace rp::host

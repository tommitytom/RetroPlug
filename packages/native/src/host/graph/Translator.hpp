#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "host/graph/SystemTable.hpp"
#include "system/SystemTypes.hpp"

class SystemBase;

namespace rp::host {

// How per-system audio fans out across the 8 plugin output channels. The
// plugin always declares 8 outputs (DISTRHO_PLUGIN_NUM_OUTPUTS); modes
// pick which of those get written, the rest are zero.
//   Stereo          all systems sum into outs 0/1; outs 2..7 silent.
//   TwoPerInstance  system i writes to outs (2i % 8)/(2i % 8 + 1).
//   OnePerInstance  system i writes a mono mix to out (i % 8).
//   ChannelSplit    ONE system fans its per-channel streams across the outputs
//                   (a Game Boy's 4 STEREO channels -> outs 0/1,2/3,4/5,6/7; a
//                   NES's 5 MONO core channels -> outs 0..4).
//   PinSplit        ONE NES fans its three 2A03 output-pin streams (Pulse | TND
//                   | lumped Expansion) across outs 0..2, mono. NES-only: any
//                   other single system falls back to Stereo.
//   StereoPinSplit  the same three pins, but each on its OWN stereo PAIR (outs
//                   0/1, 2/3, 4/5) with the mono stem mirrored into both lanes.
//                   Six lanes instead of three, and the reason to spend them is
//                   the DAW: every pin arrives as a normal centred stereo track,
//                   so hosts that can only bus/FX whole stereo pairs can treat
//                   each pin separately. Under PinSplit the pins share pairs
//                   (Pulse on L of track 1, TND on its R), which those hosts
//                   cannot pull apart. NES-only, like PinSplit.
//
// All three split modes are single-system only; any other project, and a single
// system the host is too narrow for, falls back to Stereo.
//
// Modes 0..2 fan MANY systems across the fixed pairs; modes 3..5 split ONE system's
// channels (a pair per stream, or a lane per mono stream). The routing value is owned
// by TS and pushed through the SetAudioRouting RPC; the translator wires it.
enum class AudioRouting : std::uint8_t {
    Stereo         = 0,
    TwoPerInstance = 1,
    OnePerInstance = 2,
    ChannelSplit   = 3,
    PinSplit       = 4,
    StereoPinSplit = 5,
};

// The output shape a document is built for (docs/modular-graph-phase-b1-design.md section 4.4): the host's
// own outputs routed by the project's mode (Multi), each system on its own pair (PerSystem), or one
// system's streams on a pair each (PerChannel).
struct HostShape {
    enum class Kind : std::uint8_t { Multi, PerSystem, PerChannel };
    Kind          kind = Kind::Multi;
    std::uint32_t channels = 2;  // Multi only: the host's output count
    bool operator==(const HostShape&) const = default;
};

// Turns the structural calls into graph documents (section 4). Control thread only. It holds the
// ordered system list, each Game Boy's link group, the routing mode and the host shape, and
// after every change it builds the document and its system table and hands both to `publish` (the
// engine's GraphHost::setGraph), which answers whether the document planned. `rearmsTap` is true for the
// edits that re-arm the NES tap (add, replace, remove and setRouting, a same-mode one included), whose
// plan carries the engine's SyncTap; false for a link group, the host shape and the empty first plan.
//
// A built core is parked here under a token until the system node created for it takes it (take()). A
// core it frees itself (one never adopted) goes through `released` first, as a node's destroy does.
class Translator {
public:
    using Publish = std::function<bool(const std::string& documentJson, std::shared_ptr<const SystemTable> table,
                                       bool rearmsTap)>;
    Translator(Publish publishFn, std::function<void(SystemBase*)> released);
    ~Translator();  // frees any core still parked
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    // Each returns false when the change could not be planned (the model is then left as it was, and a
    // core that was handed in is freed).
    bool add(std::unique_ptr<SystemBase> core);
    bool replace(SystemId id, std::unique_ptr<SystemBase> core);  // keeps id's slot; false if id is absent
    bool remove(SystemId id);
    bool setRouting(AudioRouting mode);
    bool setLinkGroup(SystemId id, std::uint8_t group);  // a no-op when unchanged
    bool setShape(HostShape shape);                        // a no-op when unchanged; false for Multi(0)

    AudioRouting routing() const { return routing_; }
    const HostShape& shape() const { return shape_; }
    std::size_t  systemCount() const { return systems_.size(); }
    // The core of `id` in the model, null for an unknown id: the invoker's presence check before a replace
    // or remove. The control-thread readers (the debug facet, the per-channel render) go through
    // Engine::findSystem, the running plan's table, instead.
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
    bool   publish(bool rearmsTap);

    // The order systems are summed and stepped in: unlinked by slot, then each link group (by first
    // appearance) with its members by slot. Indices into systems_.
    std::vector<std::size_t>              sumOrder() const;
    std::vector<std::vector<std::size_t>> linkGroups() const;
    // The export mode a NES has once the engine's SyncTap has armed or restored it.
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

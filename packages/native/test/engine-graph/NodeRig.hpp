#pragma once

// A GraphHost carrying the system node packages, the host I/O nodes and the test nodes (TestNodes.hpp), with
// stand-ins for the engine's services: a node naming a `system` and a `rom` resource builds its core through the rig's
// factory and the rig's resource table, as the Engine's rp.host.systems/1 and putResource would; what the host MIDI out
// and the sync trace would hand the engine is recorded instead; the device links are the ones a case installs.

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "GraphDocs.hpp"
#include "TestNodes.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/devices/DeviceRegistry.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/LaunchpadNodes.hpp"
#include "host/graph/N8Nodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/graph/SystemNodes.hpp"
#include "system/CoreBackends.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemFactory.hpp"

namespace rp::host::test {

// One call a node made on the engine's sync trace.
struct HostCall {
    std::uint32_t             frame = 0;
    std::vector<std::uint8_t> data;
};

// One message a node appended to the host MIDI out, as the system it names.
struct HostMidiOut {
    std::uint32_t             system = 0, frame = 0;
    std::vector<std::uint8_t> data;
    bool operator==(const HostMidiOut&) const = default;
};

// A core the rig hands a node with another console's kind, so a case can drive the node's build check (no real host
// gets a build's kind wrong). Only the two remaining pure virtuals (onActivate, onSampleRateChanged) reach the core it
// wraps; everything else resolves to SystemBase's defaults, which is safe because a node only observes the wrapper
// through the refused build. It owns that core, so the node's destroy frees both.
class KindWrapper final : public SystemBase {
public:
    KindWrapper(std::unique_ptr<SystemBase> core, SystemKind kind)
        : SystemBase(core->id()), core_(std::move(core)), kind_(kind) {}

    SystemKind kind() const override { return kind_; }
    void       onActivate(double sampleRate) override { core_->onActivate(sampleRate); }
    void       onSampleRateChanged(double sampleRate) override { core_->onSampleRateChanged(sampleRate); }

private:
    std::unique_ptr<SystemBase> core_;
    SystemKind                  kind_;
};

struct NodeRig {
    // The kind a core the factory built is REPORTED as, when set: a node's build compares the kind of the core
    // rp.host.systems/1 hands back with the kind its type builds, and no real host gets that wrong, so a case that
    // holds the check sets this and the rig hands the right core back behind a wrapper reporting another kind.
    std::optional<SystemKind>                            reportKind;
    rp::graph::NodeRegistry                              registry;
    rp::graph::ResourceTable                             resources;
    double                                               rate = 48000.0;
    std::vector<SystemId>                                released;  // ids whose node gave its core up
    // The cores nodes built, alive with the plans that hold them: a test that has to feed one input (a note-on, a RAM
    // poke) reads the core the node built.
    std::vector<SystemBase*>                             built;
    // The factory a `system` node builds through, every core backend registered: the engine's own on a smaller scale.
    SystemFactory                                        factory;
    HostSystems                                          systems{this, &buildSystem, &destroySystem};
    // The host I/O stand-in: nothing is ever staged, and what the host MIDI out hands the engine is recorded.
    std::vector<HostCall>                                traced;
    std::vector<HostMidiOut>                             midiOut;
    EngineHostIo                                         hostIo{this, &takeNothing, &recordMidiOut};
    EngineSyncTrace                                      trace{this, &recordTrace};
    // rp.host.devices/1: the links a case installs (devices.install), each name with none answering never connected, as
    // an Engine's registry does.
    DeviceRegistry                                       devices;
    HostDevices                                          hostDevices{this, &acquireCart, &releaseCart, &acquireSurface,
                                                                     &releaseSurface};
    // The `rom` resource each console's fixture was put under, by node type: one per rig, as one per Engine.
    std::map<std::string, std::uint32_t>                 roms;
    // Last, so it is destroyed first: its nodes free their cores through the host's destroySystem into `released`.
    std::unique_ptr<rp::graph::GraphHost>                host;

    // `maxFrames` is the plan's maximum block, as an Engine's is: a node builds its core for it, so a rig can hold what
    // a small host block sizes a core to.
    explicit NodeRig(double sampleRate, std::uint32_t maxFrames = 8192,
                     rp::graph::Target target = rp::graph::Target::Desktop) {
        rate = sampleRate;
        registerCoreBackends(factory);
        for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), sameboyPackage(), mesenPackage(),
                                    hostIoPackage(), n8Package(), launchpadPackage(), testPackage()})
            registry.addPackage(p);
        rp::graph::HostConfig config;
        config.sampleRate = sampleRate;
        config.maxFrames = maxFrames;
        config.target = target;
        config.interfaces = [](void* ctx, const char* id) -> const void* {
            auto* rig = static_cast<NodeRig*>(ctx);
            const std::string want(id);
            if (want == RP_HOST_SYSTEMS_INTERFACE) return &rig->systems;
            if (want == RP_ENGINE_HOST_IO_INTERFACE) return &rig->hostIo;
            if (want == RP_ENGINE_SYNC_TRACE_INTERFACE) return &rig->trace;
            if (want == RP_HOST_DEVICES_INTERFACE) return &rig->hostDevices;
            return nullptr;
        };
        config.interfacesCtx = this;
        host = std::make_unique<rp::graph::GraphHost>(registry, resources, config);
    }

    // An immutable byte resource a document names as "res:<id>", as Engine::putResource makes one.
    std::uint32_t put(const char* type, std::vector<std::uint8_t> bytes) {
        std::string why;
        const std::uint32_t id = resources.createBytes(type, std::move(bytes), &why);
        if (id == 0) throw std::runtime_error(why);
        return id;
    }

    // The settings members of a system node that builds its core (`type`'s node) as system `id`: its `system` and the
    // `rom` resource its type's fixture was put under (put on first use, so two systems of one console share the
    // bytes), then `extra`. A `rom` id given names that resource instead, for a case that put its own (another
    // console's fixture, or two systems off one put). What Engine::putResource plus a document's {"system","rom"}
    // are, on the rig's table.
    std::string boot(const char* type, SystemId id, const std::string& extra = "", std::uint32_t rom = 0) {
        if (rom == 0) {
            std::uint32_t& fixture = roms[type];
            if (fixture == 0) fixture = put("rom", romFor(type));
            rom = fixture;
        }
        std::string settings = R"("system":)" + std::to_string(id) + R"(,"rom":"res:)" + std::to_string(rom) + "\"";
        if (!extra.empty()) settings += "," + extra;
        return settings;
    }
    rp::graph::SetGraphResult set(const std::string& json) { return host->setGraph(json); }

    SystemBase* builtCore(SystemId id) const {
        for (SystemBase* core : built)
            if (core->id() == id) return core;
        return nullptr;
    }

    template <class T>
    T& node(const std::string& id) {
        T* t = rp::peer_as<T>(host->node(id));
        if (!t) throw std::runtime_error("no node " + id + " of that type");
        return *t;
    }

    // rp.host.systems/1: the rig's factory builds the core, and the node owns it until its destructor, which the rig
    // records (the core's id in `released`) and deletes. No snapshot slot to claim here, and no live-id check: those are
    // the engine's, and the refusals they add are EngineGraph.test.cpp's.
    static SystemBase* buildSystem(void* ctx, std::uint64_t id, SystemBuildRequest request, rp_error* err) {
        auto& rig = *static_cast<NodeRig*>(ctx);
        if (id == 0) {
            std::snprintf(err->message, sizeof err->message, "0 is not a system id");
            return nullptr;
        }
        if (id > UINT32_MAX) {
            std::snprintf(err->message, sizeof err->message, "system %llu is beyond a system id",
                          static_cast<unsigned long long>(id));
            return nullptr;
        }
        std::string why;
        std::unique_ptr<SystemBase> core = rig.factory.build(static_cast<SystemId>(id), std::move(request), rig.rate, &why);
        if (!core) {
            std::snprintf(err->message, sizeof err->message, "%s", why.c_str());
            return nullptr;
        }
        if (rig.reportKind) core = std::make_unique<KindWrapper>(std::move(core), *rig.reportKind);
        rig.built.push_back(core.get());
        return core.release();
    }
    static void destroySystem(void* ctx, SystemBase* core) {
        auto& rig = *static_cast<NodeRig*>(ctx);
        rig.released.push_back(core->id());
        // Erased before the delete: `built` is what a later builtCore() looks in, and a pointer to a freed core would be
        // handed to a dynamic_cast.
        std::erase(rig.built, core);
        delete core;
    }

    static std::vector<std::uint8_t> bytesOf(const std::uint8_t* data, std::size_t size) { return {data, data + size}; }
    static void takeNothing(void*, rp_queue*) {}
    static void recordMidiOut(void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data,
                              std::size_t size) {
        static_cast<NodeRig*>(ctx)->midiOut.push_back({system, frame, bytesOf(data, size)});
    }
    static void recordTrace(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
        static_cast<NodeRig*>(ctx)->traced.push_back({frame, bytesOf(data, size)});
    }
    static CartLink* acquireCart(void* ctx, const char* name) {
        return static_cast<NodeRig*>(ctx)->devices.acquireCart(name);
    }
    static void releaseCart(void* ctx, CartLink* link) { static_cast<NodeRig*>(ctx)->devices.release(link); }
    static SurfaceLink* acquireSurface(void* ctx, const char* name) {
        return static_cast<NodeRig*>(ctx)->devices.acquireSurface(name);
    }
    static void releaseSurface(void* ctx, SurfaceLink* link) { static_cast<NodeRig*>(ctx)->devices.release(link); }
};

}  // namespace rp::host::test

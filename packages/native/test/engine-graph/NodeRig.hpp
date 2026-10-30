#pragma once

// A GraphHost carrying the system node packages, the host I/O nodes and the test nodes (TestNodes.hpp), with
// stand-ins for the engine's services: a test parks a built core, plans a document naming its token, and the node
// created for it takes it; a node naming a `system` and a `rom` resource builds one through the rig's factory, as the
// Engine's rp.host.systems/1 would; what the host MIDI out, the cart mirror and the sync trace would hand the engine is
// recorded instead.

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "TestNodes.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/graph/HostIoNodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/graph/SystemNodes.hpp"
#include "system/CoreBackends.hpp"
#include "system/SystemBase.hpp"
#include "system/SystemFactory.hpp"

namespace rp::host::test {

inline std::vector<std::uint8_t> readFile(const char* path) {
    std::vector<std::uint8_t> bytes;
    if (std::FILE* f = std::fopen(path, "rb")) {
        std::fseek(f, 0, SEEK_END);
        bytes.resize(static_cast<std::size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
        std::fclose(f);
    }
    return bytes;
}

// One call a node made on the engine's cart sink or sync trace.
struct HostCall {
    std::uint32_t             frame = 0;
    std::vector<std::uint8_t> data;
    bool                      flush = false;
};

// One message a node appended to the host MIDI out, as the system it names.
struct HostMidiOut {
    std::uint32_t             system = 0, frame = 0;
    std::vector<std::uint8_t> data;
    bool operator==(const HostMidiOut&) const = default;
};

struct NodeRig {
    rp::graph::NodeRegistry                              registry;
    rp::graph::ResourceTable                             resources;
    double                                               rate = 48000.0;
    std::map<std::uint64_t, std::unique_ptr<SystemBase>> parked;
    std::uint64_t                                        nextToken = 1;
    std::vector<SystemId>                                released;  // ids whose node gave its core up
    // The cores nodes built, alive with the plans that hold them: a test that has to feed one input (a note-on, a RAM
    // poke) reads the core the node built, as it reads a parked one it built itself.
    std::vector<SystemBase*>                             built;
    // The factory a `system` node builds through, every core backend registered: the engine's own on a smaller scale.
    SystemFactory                                        factory;
    HostCores                                            cores{this, &take, &release};
    HostSystems                                          systems{this, &buildSystem, &destroySystem};
    // The host I/O stand-in: nothing is ever staged, `connected` is the control surface, and the host MIDI out and the
    // cart's calls are recorded. The control-surface out goes nowhere, since no test reads it off this rig
    // (HostIo.test.cpp reads it off an Engine), but a node calls it, so it is wired.
    bool                                                 connected = false;
    std::vector<HostCall>                                cart, traced;
    std::vector<HostMidiOut>                             midiOut;
    EngineHostIo                                         hostIo{this, &takeNothing, &takeNothing, &isConnected, &recordMidiOut,
                                                                &ignoreControllerOut, &recordCart};
    EngineSyncTrace                                      trace{this, &recordTrace};
    // Last, so it is destroyed first: its nodes free their cores through `cores` into `released`.
    std::unique_ptr<rp::graph::GraphHost>                host;

    explicit NodeRig(double sampleRate) {
        rate = sampleRate;
        registerCoreBackends(factory);
        for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), sameboyPackage(), mesenPackage(),
                                    hostIoPackage(), n8Package(), testPackage()})
            registry.addPackage(p);
        rp::graph::HostConfig config;
        config.sampleRate = sampleRate;
        config.maxFrames = 8192;
        config.interfaces = [](void* ctx, const char* id) -> const void* {
            auto* rig = static_cast<NodeRig*>(ctx);
            const std::string want(id);
            if (want == RP_HOST_CORES_INTERFACE) return &rig->cores;
            if (want == RP_HOST_SYSTEMS_INTERFACE) return &rig->systems;
            if (want == RP_ENGINE_HOST_IO_INTERFACE) return &rig->hostIo;
            if (want == RP_ENGINE_SYNC_TRACE_INTERFACE) return &rig->trace;
            return nullptr;
        };
        config.interfacesCtx = this;
        host = std::make_unique<rp::graph::GraphHost>(registry, resources, config);
    }

    std::uint64_t park(std::unique_ptr<SystemBase> core) {
        parked.emplace(nextToken, std::move(core));
        return nextToken++;
    }

    // An immutable byte resource a document names as "res:<id>", as Engine::putResource makes one.
    std::uint32_t put(const char* type, std::vector<std::uint8_t> bytes) {
        std::string why;
        const std::uint32_t id = resources.createBytes(type, std::move(bytes), &why);
        if (id == 0) throw std::runtime_error(why);
        return id;
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

    static SystemBase* take(void* ctx, std::uint64_t token) {
        auto& p = static_cast<NodeRig*>(ctx)->parked;
        const auto it = p.find(token);
        if (it == p.end()) return nullptr;
        SystemBase* core = it->second.release();
        p.erase(it);
        return core;
    }
    static bool release(void* ctx, SystemBase* core) {
        static_cast<NodeRig*>(ctx)->released.push_back(core->id());
        return false;
    }
    // rp.host.systems/1: the rig's factory builds the core, and the node owns it until its destructor, which the rig
    // records (the core's id in `released`) and deletes. No snapshot slot to claim here, and no live-id check: those are
    // the engine's, and the refusals they add are EngineGraph.test.cpp's.
    static SystemBase* buildSystem(void* ctx, std::uint64_t id, const SystemBuildRequest& request, rp_error* err) {
        auto& rig = *static_cast<NodeRig*>(ctx);
        if (id == 0 || id > UINT32_MAX) {
            std::snprintf(err->message, sizeof err->message, "0 is not a system id");
            return nullptr;
        }
        std::string why;
        std::unique_ptr<SystemBase> core = rig.factory.build(static_cast<SystemId>(id), request, rig.rate, &why);
        if (!core) {
            std::snprintf(err->message, sizeof err->message, "%s", why.c_str());
            return nullptr;
        }
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
    static bool isConnected(void* ctx) { return static_cast<NodeRig*>(ctx)->connected; }
    static void recordMidiOut(void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data,
                              std::size_t size) {
        static_cast<NodeRig*>(ctx)->midiOut.push_back({system, frame, bytesOf(data, size)});
    }
    static void ignoreControllerOut(void*, const std::uint8_t*, std::size_t) {}
    static void recordCart(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush) {
        static_cast<NodeRig*>(ctx)->cart.push_back({frame, bytesOf(data, size), flush});
    }
    static void recordTrace(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
        static_cast<NodeRig*>(ctx)->traced.push_back({frame, bytesOf(data, size), false});
    }
};

}  // namespace rp::host::test

#pragma once

// A GraphHost carrying the system node packages, the host I/O nodes and the test nodes (TestNodes.hpp), with
// stand-ins for the engine's services: a test parks a built core, plans a document naming its token, and the node
// created for it takes it; what the host I/O nodes and the sync trace would hand the engine is recorded instead.

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
#include "system/SystemBase.hpp"

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

// One call a node made on the engine's host I/O or sync trace.
struct HostCall {
    std::uint32_t             system = 0;
    std::uint32_t             frame = 0;
    std::vector<std::uint8_t> data;
    bool                      flush = false;
};

struct NodeRig {
    rp::graph::NodeRegistry                              registry;
    rp::graph::ResourceTable                             resources;
    std::map<std::uint64_t, std::unique_ptr<SystemBase>> parked;
    std::uint64_t                                        nextToken = 1;
    std::vector<SystemId>                                released;  // ids whose node freed its core
    HostCores                                            cores{this, &take, &release};
    // The host I/O stand-in: nothing is ever staged, `connected` is the control surface, and every output call is
    // recorded, a drop counted.
    bool                                                 connected = false;
    std::vector<HostCall>                                midiOut, controllerOut, cart, traced;
    std::uint32_t                                        dropped = 0;
    EngineHostIo                                         hostIo{this, &takeNothing, &takeNothing, &isConnected, &recordMidiOut,
                                                                &recordControllerOut, &recordCart, &countDropped};
    EngineSyncTrace                                      trace{this, &recordTrace};
    // Last, so it is destroyed first: its nodes free their cores through `cores` into `released`.
    std::unique_ptr<rp::graph::GraphHost>                host;

    explicit NodeRig(double sampleRate) {
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

    rp::graph::SetGraphResult set(const std::string& json) { return host->setGraph(json); }

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

    static std::vector<std::uint8_t> bytesOf(const std::uint8_t* data, std::size_t size) { return {data, data + size}; }
    static void takeNothing(void*, rp_queue*) {}
    static bool isConnected(void* ctx) { return static_cast<NodeRig*>(ctx)->connected; }
    static void recordMidiOut(void* ctx, std::uint32_t system, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
        static_cast<NodeRig*>(ctx)->midiOut.push_back({system, frame, bytesOf(data, size), false});
    }
    static void recordControllerOut(void* ctx, const std::uint8_t* data, std::size_t size) {
        static_cast<NodeRig*>(ctx)->controllerOut.push_back({0, 0, bytesOf(data, size), false});
    }
    static void recordCart(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size, bool flush) {
        static_cast<NodeRig*>(ctx)->cart.push_back({0, frame, bytesOf(data, size), flush});
    }
    static void countDropped(void* ctx) { ++static_cast<NodeRig*>(ctx)->dropped; }
    static void recordTrace(void* ctx, std::uint32_t frame, const std::uint8_t* data, std::size_t size) {
        static_cast<NodeRig*>(ctx)->traced.push_back({0, frame, bytesOf(data, size), false});
    }
};

}  // namespace rp::host::test

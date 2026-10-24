#pragma once

// A GraphHost carrying the system node packages, with a stand-in for the engine's core handoff: a test
// parks a built core, plans a document naming its token, and the node created for it takes it.

#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/nodes/CoreNodes.hpp"
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

struct NodeRig {
    rp::graph::NodeRegistry                              registry;
    rp::graph::ResourceTable                             resources;
    std::unique_ptr<rp::graph::GraphHost>                host;
    std::map<std::uint64_t, std::unique_ptr<SystemBase>> parked;
    std::uint64_t                                        nextToken = 1;
    std::vector<SystemId>                                released;  // ids whose node freed its core
    HostCores                                            cores{this, &take, &release};

    explicit NodeRig(double sampleRate) {
        for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), sameboyPackage(), mesenPackage()})
            registry.addPackage(p);
        rp::graph::HostConfig config;
        config.sampleRate = sampleRate;
        config.maxFrames = 8192;
        config.interfaces = [](void* ctx, const char* id) -> const void* {
            return std::string(id) == RP_HOST_CORES_INTERFACE ? &static_cast<NodeRig*>(ctx)->cores : nullptr;
        };
        config.interfacesCtx = this;
        host = std::make_unique<rp::graph::GraphHost>(registry, resources, config);
    }

    std::uint64_t park(std::unique_ptr<SystemBase> core) {
        parked.emplace(nextToken, std::move(core));
        return nextToken++;
    }

    rp::graph::SetGraphResult set(const std::string& json) { return host->setGraph(json); }

    static SystemBase* take(void* ctx, std::uint64_t token) {
        auto& p = static_cast<NodeRig*>(ctx)->parked;
        const auto it = p.find(token);
        if (it == p.end()) return nullptr;
        SystemBase* core = it->second.release();
        p.erase(it);
        return core;
    }
    static void release(void* ctx, SystemBase* core) { static_cast<NodeRig*>(ctx)->released.push_back(core->id()); }
};

}  // namespace rp::host::test

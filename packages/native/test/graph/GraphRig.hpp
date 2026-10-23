#pragma once

// A GraphHost wired to the core, host and test packages, plus host output buffers, for the graph-core
// tests. Quiescent by default, so setGraph flushes inline and run() is the block driver.

#include <memory>
#include <string>
#include <vector>

#include "TestNodes.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/nodes/CoreNodes.hpp"

namespace rp::graph::test {

constexpr std::uint32_t kHostFrames = 1024;

struct GraphRig {
    NodeRegistry               registry;
    ResourceTable              resources;
    std::unique_ptr<GraphHost> host;
    std::vector<std::vector<float>> outs;
    std::vector<float*>        outPointers;
    rp_transport               transport{48000.0, 120.0, 0.0, 0};

    explicit GraphRig(HostConfig config = {48000.0, 64, Target::Desktop}, std::uint32_t hostChannels = 8) {
        registry.addPackage(corePackage());
        registry.addPackage(hostPackage());
        registry.addPackage(testPackage());
        host = std::make_unique<GraphHost>(registry, resources, config);
        outs.assign(hostChannels, std::vector<float>(kHostFrames, 0.0f));  // room for blocks after a reactivate
        for (auto& o : outs) outPointers.push_back(o.data());
    }

    SetGraphResult set(const std::string& json) { return host->setGraph(json); }

    void run(std::uint32_t frames = 64, int blocks = 1) {
        for (int i = 0; i < blocks; ++i) {
            for (auto& o : outs) std::fill(o.begin(), o.end(), -99.0f);
            host->processBlock(frames, transport, outPointers.data(), static_cast<std::uint32_t>(outPointers.size()));
        }
    }

    template <class T>
    T& node(const std::string& id) {
        T* t = rp::peer_as<T>(host->node(id));
        if (!t) throw std::runtime_error("no node " + id + " of that type");
        return *t;
    }

    static std::string codes(const SetGraphResult& r) {
        std::string s;
        for (const auto& e : r.errors) s += (s.empty() ? "" : ",") + e.code;
        return s;
    }
};

}  // namespace rp::graph::test

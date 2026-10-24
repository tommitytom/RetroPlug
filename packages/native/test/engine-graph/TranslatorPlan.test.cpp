// Every document the translator builds must PLAN: a GraphHost with the real packages is handed each one
// (Translator.test.cpp only captures them), so a port name, a settings key or a node limit the planner
// refuses fails here. The cores are real, deterministic ones, and one block is run to show the plan executes.
//
// Run via `pnpm test:plugin`.

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "NodeRig.hpp"
#include "graph/GraphDocument.hpp"
#include "graph/GraphHost.hpp"
#include "graph/HostNodes.hpp"
#include "graph/nodes/CoreNodes.hpp"
#include "host/graph/HostServices.hpp"
#include "host/graph/KernelNode.hpp"
#include "host/graph/SystemNodes.hpp"
#include "host/graph/Translator.hpp"
#include "system/mesen/MesenNesConfig.hpp"
#include "system/mesen/MesenNesSystem.hpp"
#include "system/sameboy/SameBoyConfig.hpp"
#include "system/sameboy/SameBoySystem.hpp"

using rp::host::AudioRouting;
using rp::host::HostShape;
using rp::host::Translator;
using rp::host::test::readFile;

namespace {

constexpr double kRate = 48000.0;

std::unique_ptr<SystemBase> mgb(SystemId id, std::uint8_t group = 0) {
    SameBoyConfig cfg;
    cfg.model = SameBoyModel::DmgB;
    cfg.fastBoot = false;
    cfg.linkGroupId = group;
    auto s = std::make_unique<SameBoySystem>(id, cfg, readFile(RP_MGB_ROM_PATH));
    s->onActivate(kRate);
    return s;
}

std::unique_ptr<SystemBase> bliptoaster(SystemId id) {
    MesenNesConfig cfg;
    cfg.romPath = RP_BLIPTOASTER_ROM_PATH;
    auto s = std::make_unique<MesenNesSystem>(id, cfg, readFile(RP_BLIPTOASTER_ROM_PATH));
    s->onActivate(kRate);
    return s;
}

// A GraphHost carrying all five packages, with the translator publishing into it: take/release stand in for
// rp.host.cores/1 and a no-op for rp.engine.kernel/1.
struct PlanRig {
    rp::graph::NodeRegistry               registry;
    rp::graph::ResourceTable              resources;
    rp::host::HostCores                   cores{};
    rp::host::EngineKernel                kernel{};
    int                                   kernelRuns = 0;
    std::vector<std::string>              refusals;
    std::unique_ptr<rp::graph::GraphHost> host;
    std::unique_ptr<Translator>           tr;  // destroyed first: its cores free through `cores`

    PlanRig() {
        for (const rp_package* p : {rp::graph::corePackage(), rp::graph::hostPackage(), rp::host::sameboyPackage(),
                                    rp::host::mesenPackage(), rp::host::dspPackage()})
            registry.addPackage(p);
        cores = {this, [](void* ctx, std::uint64_t token) { return static_cast<PlanRig*>(ctx)->tr->take(token); },
                 [](void*, SystemBase*) {}};
        kernel = {this, [](void* ctx, std::uint32_t) { ++static_cast<PlanRig*>(ctx)->kernelRuns; }};
        rp::graph::HostConfig config;
        config.sampleRate = kRate;
        config.maxFrames = 8192;
        config.interfaces = [](void* ctx, const char* id) -> const void* {
            auto* self = static_cast<PlanRig*>(ctx);
            if (std::strcmp(id, RP_HOST_CORES_INTERFACE) == 0) return &self->cores;
            if (std::strcmp(id, RP_ENGINE_KERNEL_INTERFACE) == 0) return &self->kernel;
            return nullptr;
        };
        config.interfacesCtx = this;
        host = std::make_unique<rp::graph::GraphHost>(registry, resources, config);
        tr = std::make_unique<Translator>(
            [this](const std::string& json, std::shared_ptr<const rp::host::SystemTable> table, bool) {
                auto doc = rp::graph::parseGraphDocument(json);
                if (!doc) {
                    refusals.push_back("parse: " + doc.error().what());
                    return false;
                }
                const auto result = host->setGraph(*doc, std::move(table));
                for (const auto& e : result.errors) refusals.push_back(e.code + " at " + e.nodeId + "." + e.port + ": " + e.message);
                return result.ok;
            },
            [](SystemBase*) {});
    }
    ~PlanRig() {
        tr.reset();
        host.reset();
    }

    // Run one block of the current plan into `width` lanes, and say whether anything came out.
    void runBlock(std::size_t width) {
        std::vector<std::vector<float>> lanes(width, std::vector<float>(512, 0.0f));
        std::vector<float*>             ptrs;
        for (auto& l : lanes) ptrs.push_back(l.data());
        const rp_transport t{kRate, 120.0, 0.0, 0};
        host->processBlock(512, t, ptrs.data(), static_cast<std::uint32_t>(width));
    }

    void expectPlanned(const std::string& what, std::size_t width) {
        INFO(what);
        for (const auto& r : refusals) UNSCOPED_INFO(r);
        REQUIRE(refusals.empty());
        runBlock(width);
        CHECK(host->faultCount() == 0);
    }
};

constexpr AudioRouting kMultiModes[] = {AudioRouting::Stereo, AudioRouting::TwoPerInstance, AudioRouting::OnePerInstance};

}  // namespace

TEST_CASE("Multi documents plan under every routing, for 1, 3 and 5 systems", "[engine-graph][translator][plan]") {
    PlanRig r;
    r.tr->setShape({HostShape::Kind::Multi, 2});
    SystemId next = 1;
    for (std::size_t count : {1u, 3u, 5u}) {
        while (r.tr->systemCount() < count) {
            REQUIRE(r.tr->add(next % 2 ? mgb(next) : bliptoaster(next)));
            ++next;
        }
        for (std::uint32_t n : {2u, 4u, 8u})
            for (AudioRouting mode : kMultiModes) {
                REQUIRE(r.tr->setShape({HostShape::Kind::Multi, n}));
                REQUIRE(r.tr->setRouting(mode));
                r.expectPlanned(std::to_string(count) + " systems, " + std::to_string(n) + " outputs, routing " +
                                    std::to_string(static_cast<int>(mode)),
                                n);
            }
    }
}

TEST_CASE("18 systems under Stereo plan, so the add chain passes 16 inputs", "[engine-graph][translator][plan]") {
    PlanRig r;
    for (SystemId id = 1; id <= 18; ++id) REQUIRE(r.tr->add(mgb(id)));
    r.expectPlanned("18 systems", 2);
    CHECK(r.host->node("lane-0-1") != nullptr);
}

TEST_CASE("a lone system's split documents plan", "[engine-graph][translator][plan]") {
    SECTION("NES") {
        PlanRig r;
        REQUIRE(r.tr->add(bliptoaster(1)));
        for (AudioRouting mode : {AudioRouting::ChannelSplit, AudioRouting::PinSplit, AudioRouting::StereoPinSplit}) {
            REQUIRE(r.tr->setShape({HostShape::Kind::Multi, 8}));
            REQUIRE(r.tr->setRouting(mode));
            r.expectPlanned("NES split " + std::to_string(static_cast<int>(mode)), 8);
        }
    }
    SECTION("Game Boy") {
        PlanRig r;
        REQUIRE(r.tr->add(mgb(1)));
        REQUIRE(r.tr->setShape({HostShape::Kind::Multi, 8}));
        REQUIRE(r.tr->setRouting(AudioRouting::ChannelSplit));
        r.expectPlanned("GB ChannelSplit", 8);
    }
}

TEST_CASE("PerSystem and PerChannel documents plan", "[engine-graph][translator][plan]") {
    SECTION("PerSystem") {
        PlanRig r;
        REQUIRE(r.tr->add(mgb(1)));
        REQUIRE(r.tr->add(bliptoaster(2)));
        REQUIRE(r.tr->setShape({HostShape::Kind::PerSystem, 0}));
        r.expectPlanned("PerSystem", 4);
    }
    SECTION("PerChannel, a Game Boy") {
        PlanRig r;
        REQUIRE(r.tr->add(mgb(1)));
        REQUIRE(r.tr->setShape({HostShape::Kind::PerChannel, 0}));
        r.expectPlanned("PerChannel GB", 8);
    }
    SECTION("PerChannel, a NES with its channels armed") {
        PlanRig r;
        REQUIRE(r.tr->add(bliptoaster(1)));
        REQUIRE(r.tr->setRouting(AudioRouting::ChannelSplit));
        REQUIRE(r.tr->setShape({HostShape::Kind::PerChannel, 0}));
        r.expectPlanned("PerChannel NES", 10);
    }
}

TEST_CASE("two linked Game Boys and an unlinked one plan", "[engine-graph][translator][plan]") {
    PlanRig r;
    REQUIRE(r.tr->add(mgb(1, 1)));
    REQUIRE(r.tr->add(mgb(2, 0)));
    REQUIRE(r.tr->add(mgb(3, 1)));
    REQUIRE(r.tr->setShape({HostShape::Kind::Multi, 8}));
    for (AudioRouting mode : kMultiModes) {
        REQUIRE(r.tr->setRouting(mode));
        r.expectPlanned("linked group, routing " + std::to_string(static_cast<int>(mode)), 8);
    }
    REQUIRE(r.tr->setShape({HostShape::Kind::PerSystem, 0}));
    r.expectPlanned("linked group, PerSystem", 6);
}

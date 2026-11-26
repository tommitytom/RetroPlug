// No heap block crosses the pack boundary (docs/downloadable-packs-m5a-plan.md, the user's decision of 2026-11-25:
// Windows keeps its static CRT, so a pack's library has a heap of its own there, and nothing either side allocates may be
// freed by the other). The mesen-heap pack is the Mesen package built as a pack with a heap of its own, and both it and
// this binary tag every block they allocate (HeapTag.hpp): a delete of the other side's block is counted. The cases
// drive every path a core crosses the boundary on, through the host's own callers - a build with seeds and a long ROM
// name, the build's refusals on both sides, the snapshot registry's reads, the stream layout, the ROM title job, every
// debug service, a seeded rebuild, and the destruction - and hold both counts at zero. Two negative controls prove the
// counting is live in this binary and in the module.
//
// Linux (gcc) and Windows; macOS builds no module (packages/native/cmake/tests.cmake says why) and skips.
//
// Run via `pnpm test:plugin heap` or `build/bin/retroplug-packs-heap-test`.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "GraphDocs.hpp"
#include "HeapTag.hpp"
#include "graph/Packages.hpp"
#include "graph/rp_jobs.h"
#include "host/engine/Engine.hpp"
#include "host/packs/NativeLibrary.hpp"
#include "host/packs/PackTable.hpp"
#include "host/rpc/DebugRpcService.hpp"
#include "system/SystemBase.hpp"

using rp::host::packs::PackTable;
using rp::host::test::DocSystem;

#if RP_HEAP_OWN_RUNTIME
namespace {

constexpr double        kRate = 48000.0;
constexpr std::uint32_t kFrames = 1024;

std::filesystem::path heapPack() { return std::filesystem::path(RP_PACKS_BUILD_DIR) / "mesen-heap" / "0.1.0"; }

// The pack's counts and its two negative-control hooks (PackHeap.cpp), found in its loaded library.
struct PackHooks {
    std::uint32_t (*violations)() = nullptr;
    std::uint64_t (*blocks)() = nullptr;
    void* (*allocate)(std::size_t) = nullptr;
    void (*release)(void*) = nullptr;
};

template <class Fn>
Fn hook(void* library, const char* name) {
    return reinterpret_cast<Fn>(rp::host::packs::librarySymbol(library, name));
}

PackHooks hooksOf(void* library) {
    PackHooks h;
    h.violations = hook<std::uint32_t (*)()>(library, "rp_heap_violations");
    h.blocks = hook<std::uint64_t (*)()>(library, "rp_heap_blocks");
    h.allocate = hook<void* (*)(std::size_t)>(library, "rp_heap_allocate");
    h.release = hook<void (*)(void*)>(library, "rp_heap_free");
    return h;
}

void render(Engine& e, int blocks) {
    std::vector<float> l(kFrames), r(kFrames);
    float*             outs[2] = {l.data(), r.data()};
    for (int i = 0; i < blocks; ++i) e.processBlock(kFrames, outs, 2);
}

// A settings member naming resource `id` (DocSystem::extra's form).
std::string res(const char* member, std::uint32_t id) {
    return std::string("\"") + member + "\":\"res:" + std::to_string(id) + "\"";
}

// Past any short-string buffer, so a ROM name the wrong side frees is a heap block.
const char* const kLongRomName = R"("romName":"a ROM name longer than any short-string buffer holds")";

}  // namespace
#endif

TEST_CASE("the heap test's counts are live: a block freed by the side that did not allocate it is counted",
          "[packs-heap]") {
#if !RP_HEAP_OWN_RUNTIME
    SKIP("no pack with a heap of its own on this platform (packages/native/cmake/tests.cmake)");
#else
    PackTable  table(rp::graph::productPackages());
    const auto load = table.load(heapPack());
    INFO(load.error);
    REQUIRE(load);
    const PackHooks pack = hooksOf(load.pack->library);
    REQUIRE(pack.violations != nullptr);
    REQUIRE(pack.blocks != nullptr);
    REQUIRE(pack.allocate != nullptr);
    REQUIRE(pack.release != nullptr);

    const auto hostBefore = rp::test::heap::hostViolations();
    const auto hostBlocksBefore = rp::test::heap::hostBlocks();
    void*      packBlock = pack.allocate(64);
    REQUIRE(packBlock != nullptr);
    ::operator delete(packBlock);  // the host deleting the pack's block
    CHECK(rp::test::heap::hostViolations() == hostBefore + 1);

    const auto packBefore = pack.violations();
    void* hostBlock = ::operator new(64);
    CHECK(rp::test::heap::hostBlocks() > hostBlocksBefore);  // the host's operator new is the tagged one
    pack.release(hostBlock);  // the pack deleting the host's block
    CHECK(pack.violations() == packBefore + 1);
#endif
}

TEST_CASE("no heap block crosses the boundary: a Mesen pack with a heap of its own is built, run, read, debugged "
          "and destroyed",
          "[packs-heap]") {
#if !RP_HEAP_OWN_RUNTIME
    SKIP("no pack with a heap of its own on this platform (packages/native/cmake/tests.cmake)");
#else
    PackTable  table(rp::graph::productPackages());
    const auto load = table.load(heapPack());
    INFO(load.error);
    REQUIRE(load);
    const PackHooks pack = hooksOf(load.pack->library);
    REQUIRE(pack.violations != nullptr);
    REQUIRE(pack.blocks != nullptr);
    const auto hostBefore = rp::test::heap::hostViolations();
    const auto packBefore = pack.violations();
    const auto packBlocksBefore = pack.blocks();
    {
        Engine e(kRate, table);
        REQUIRE(e.packsRegistered() == 1);

        // Three consoles: a NES with a battery seed and a long ROM name, a Master System with smsggdj's battery and a
        // long ROM name (the one console whose build keeps the name), and a GBA.
        const std::uint32_t nesSram = e.putResource("sram", std::vector<std::uint8_t>(8192, 0x5a));
        const std::uint32_t smsSram = e.putResource("sram", rp::host::test::readFile(RP_SMS_SAV_PATH));
        REQUIRE(nesSram != 0);
        REQUIRE(smsSram != 0);
        rp::host::test::Roms   roms;
        std::vector<DocSystem> systems{
            {1, rp::host::test::kNesType, 0, res("sram", nesSram) + "," + kLongRomName, ""},
            {2, "rp.mesen.sms", 0, res("sram", smsSram) + "," + kLongRomName, ""},
            {3, "rp.mesen.gba", 0, "", ""}};
        rp::host::test::withRoms(e, systems, roms);
        const auto placed = e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2, 3}));
        REQUIRE(placed.ok);
        CHECK(pack.blocks() > packBlocksBefore);  // the pack's cores allocate through the pack's own operator new
        render(e, 40);

        // The snapshot registry's reads (its claim already took each core's savestate and battery), each core's stream
        // layout, and the ROM capability's title job.
        CHECK(e.readState(1));
        (void)e.readSram(1);  // a NES battery only if the cart has one; the claim asked the core either way
        CHECK(e.readSram(2));  // smsggdj keeps one
        CHECK(e.readRam(1));
        for (const SystemId id : {SystemId{1}, SystemId{2}, SystemId{3}}) {
            REQUIRE(e.findSystem(id) != nullptr);
            CHECK_FALSE(e.findSystem(id)->channelLayout().empty());
        }
        rp::host::JobRequest title;
        title.nodeId = "sys-3";
        title.capability = RP_SYSTEM_ROM_INTERFACE;
        title.op = "title";  // a title fits any short-string buffer, so this count cannot see it; the contract scan holds it
        CHECK(e.submitJob(title).id != 0);
        (void)e.drainJobs();

        // Every debug service the NES presents, through the service the RPC calls.
        DebugRpcService debug(e);
        CHECK(debug.loadLabels(1, RP_BLIPTOASTER_DBG_PATH));
        (void)debug.symbolAddress(1, "main");
        CHECK_FALSE(debug.getCpuRegisters(1).empty());
        (void)debug.drainCoreBytes(1);
        (void)debug.getApuState(1);
        (void)debug.getExpansionAudioState(1);
        (void)debug.getPpuState(1);
        CHECK(debug.beginProfile(1));
        render(e, 4);
        (void)debug.readProfile(1);
        CHECK(debug.setTrace(1, true));
        render(e, 2);
        (void)debug.readTrace(1, 32);
        CHECK(debug.setTrace(1, false));
        const auto registers = debug.getCpuRegisters(1);
        std::uint32_t pc = 0;
        for (const auto& reg : registers)
            if (reg.name == "pc") pc = reg.value;
        CHECK_FALSE(debug.disassemble(1, pc, 16).empty());
        (void)debug.getCallStack(1);
        (void)debug.drainEvents(1);
        CHECK(debug.setBreakpoints(1, {rp::BreakpointSpec{"execute", pc, 0, ""}}));
        (void)debug.runUntilBreak(1, 100000);
        (void)debug.stepInto(1);
        (void)debug.stepOver(1);
        render(e, 2);
        (void)debug.drainBreakHits(1);
        CHECK(debug.setBreakpoints(1, {}));

        // A seeded rebuild: system 4 boots from system 1's savestate.
        const auto state = e.readState(1);
        REQUIRE(state);
        const std::uint32_t stateRes = e.putResource("state", *state);
        REQUIRE(stateRes != 0);
        systems.push_back({4, rp::host::test::kNesType, 0, res("state", stateRes), ""});
        rp::host::test::withRoms(e, systems, roms);
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), {1, 2, 3, 4})).ok);
        render(e, 4);

        // The pack's build refusing: a NES node handed a Game Boy ROM ("the ROM is not a NES ROM").
        const std::uint32_t gbRom = e.putResource("rom", rp::host::test::readFile(RP_MGB_ROM_PATH));
        REQUIRE(gbRom != 0);
        auto wrongConsole = systems;
        wrongConsole.push_back({5, rp::host::test::kNesType, gbRom, kLongRomName, ""});
        CHECK_FALSE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(wrongConsole),
                                                            {1, 2, 3, 4, 5}))
                        .ok);
        // The host refusing before the build: system 1 rebuilt (a fresh put moves its ROM ref) while its core is live.
        const std::uint32_t freshRom = e.putResource("rom", rp::host::test::readFile(RP_BLIPTOASTER_ROM_PATH));
        REQUIRE(freshRom != 0);
        auto liveId = systems;
        liveId[0].rom = freshRom;
        CHECK_FALSE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(liveId), {1, 2, 3, 4})).ok);

        // Every core destroyed: an empty document, and the reclaim that frees the plans that held them.
        REQUIRE(e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument({}), {})).ok);
        e.reclaim();
        CHECK(e.liveSystemCount() == 0);
        CHECK(e.faultCount() == 0);
    }
    const auto hostCrossed = rp::test::heap::hostViolations() - hostBefore;
    const auto packCrossed = pack.violations() - packBefore;
    INFO("the host freed " << hostCrossed << " of the pack's blocks; the pack freed " << packCrossed
                           << " of the host's (break on crossed() in HostHeap.cpp / PackHeap.cpp to see where)");
    CHECK(hostCrossed == 0);
    CHECK(packCrossed == 0);
#endif
}

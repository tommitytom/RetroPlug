// The capability job model on the emulated cores (docs/modular-graph-phase-c5-design.md section 6): rp.system.memory/1,
// rp.system.sram/1 and rp.system.rom/1 through Engine::submitJob and Engine::drainJobs, held to what the system-id RPCs
// return for the same system (readRam, readSram, writeRam, and the ROM file's own prefix, which TS reads with
// readFilePrefix) bit for bit, on each console the suite builds (the GBA on its sibling fixture, which SKIPs, counted,
// when the resources tree is absent); a write that completes once the block after its drain has published it, and
// before submitJob returns while no audio thread runs; a job whose node leaves the plan, or that the engine discards,
// completing with an error; writes the audio thread's stop flushes, and writes a block that runs nothing leaves for the
// next; a full command ring; the job count's bound; every refusal; a job a device thread runs, reporting progress; and a
// warmed block with a write in flight that allocates and frees nothing.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[capability]"`.

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <thread>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "../graph/CountingAllocator.hpp"
#include "GraphDocs.hpp"
#include "host/engine/CapabilityJobs.hpp"
#include "host/engine/DspCommand.hpp"
#include "host/engine/Engine.hpp"
#include "host/engine/EngineInvoker.hpp"
#include "host/rpc/EngineRpcService.hpp"
#include "system/SystemBase.hpp"

using rp::host::test::DocSystem;
using rp::host::test::readFile;

namespace {

constexpr double kRate = 48000.0;
const char* const kMemory = RP_SYSTEM_MEMORY_INTERFACE;
const char* const kSram = RP_SYSTEM_SRAM_INTERFACE;
const char* const kRom = RP_SYSTEM_ROM_INTERFACE;

std::vector<std::uint8_t> bytesOf(const rfl::Bytestring& b) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(b.data());
    return {p, p + b.size()};
}

std::vector<std::uint8_t> bytesOf(const std::string& s) { return {s.begin(), s.end()}; }

rfl::Bytestring bytestringOf(const std::vector<std::uint8_t>& v) {
    const auto* p = reinterpret_cast<const std::byte*>(v.data());
    return rfl::Bytestring(p, p + v.size());
}

// An Engine with the system-id RPCs over it, as a host composes them, and the systems it places.
struct Rig {
    Engine                    e{kRate};
    QueuedInvoker             invoker{e};
    EngineRpcService          rpc{e, invoker};
    rp::host::test::Roms      roms;
    std::vector<float>        l = std::vector<float>(512), r = std::vector<float>(512);

    bool plan(std::vector<DocSystem> systems) {
        std::vector<SystemId> slots;
        for (const auto& s : systems) slots.push_back(s.id);
        rp::host::test::withRoms(e, systems, roms);
        return e.setGraph(rp::host::test::graphRequest(rp::host::test::stereoDocument(systems), slots)).ok;
    }
    void block(std::uint32_t frames = 512) {
        float* outs[2] = {l.data(), r.data()};
        e.processBlock(frames, outs, 2);
    }
    rp::host::JobSubmit submit(const std::string& node, const char* capability, const std::string& op,
                               std::string region = "", std::uint32_t offset = 0, std::uint32_t length = 0,
                               std::vector<std::uint8_t> bytes = {}) {
        rp::host::JobRequest q;
        q.nodeId = node;
        q.capability = capability;
        q.op = op;
        q.region = std::move(region);
        q.offset = offset;
        q.length = length;
        q.bytes = std::move(bytes);
        return e.submitJob(std::move(q));
    }
    // The one completion of job `id` a drain hands back now, which must be there.
    rp::host::JobCompletion completion(std::uint32_t id) {
        auto all = e.drainJobs();
        REQUIRE(all.size() == 1);
        REQUIRE(all[0].id == id);
        REQUIRE(all[0].done);
        return std::move(all[0]);
    }
    // A job that completes at once, and the bytes it completed with.
    std::vector<std::uint8_t> now(const rp::host::JobSubmit& s) {
        INFO(s.error);
        REQUIRE(s.id != 0);
        auto c = completion(s.id);
        INFO(c.error);
        REQUIRE(c.ok);
        return std::move(c.bytes);
    }
};

// The bytes of a fixture the resources tree holds, or empty when it is not there (as CI runs).
std::vector<std::uint8_t> resourceFixture(const char* relative) {
    const char* root = std::getenv("RETROPLUG_RESOURCES_DIR");
    if (!root) return {};
    return readFile((std::string(root) + relative).c_str());
}

// Every capability of system 1 against what the system-id RPCs answer for it, and against `rom`, the file it was built
// from. `title` is the ROM's title as this capability reads it: the header field up to its first byte that is not
// printable ASCII.
void checkAgainstTheRpcs(Rig& rig, const std::vector<std::uint8_t>& rom, const std::string& title) {
    for (int b = 0; b < 8; ++b) rig.block();
    const auto ram = rig.rpc.readRam(1);
    REQUIRE(ram);
    const auto wram = bytesOf(*ram);
    CHECK(rig.now(rig.submit("sys-1", kMemory, "regions")) ==
          bytesOf(R"([{"name":"ram","size":)" + std::to_string(wram.size()) + "}]"));
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram")) == wram);
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 16, 32)) ==
          std::vector<std::uint8_t>(wram.begin() + 16, wram.begin() + 48));
    if (const auto sram = rig.rpc.readSram(1)) {
        CHECK(rig.now(rig.submit("sys-1", kSram, "dump")) == bytesOf(*sram));
    } else {
        const auto refused = rig.submit("sys-1", kSram, "dump");
        CHECK(refused.id == 0);
        CHECK(refused.error.find("publishes no battery") != std::string::npos);
    }
    // TS reads a header with readFilePrefix, a prefix of the file the resource was put from.
    for (const std::uint32_t length : {16u, 0x150u}) {
        INFO(length);
        CHECK(rig.now(rig.submit("sys-1", kRom, "header", "", 0, length)) ==
              std::vector<std::uint8_t>(rom.begin(), rom.begin() + length));
    }
    CHECK(rig.now(rig.submit("sys-1", kRom, "header")) == rom);
    CHECK(rig.now(rig.submit("sys-1", kRom, "title")) == bytesOf(title));
    CHECK(rig.e.jobsOutstanding() == 0);
}

}  // namespace

TEST_CASE("each capability answers what the system-id RPCs and the ROM file answer, on each console",
          "[engine-graph][capability]") {
    SECTION("Game Boy") {
        Rig rig;
        REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
        const auto rom = readFile(RP_MGB_ROM_PATH);
        std::string title;
        for (std::size_t i = 0x134; i < 0x144 && rom[i] >= 0x20 && rom[i] <= 0x7e; ++i)
            title.push_back(static_cast<char>(rom[i]));
        REQUIRE(!title.empty());
        checkAgainstTheRpcs(rig, rom, title);
    }
    SECTION("NES") {
        Rig rig;
        REQUIRE(rig.plan({{1, rp::host::test::kNesType}}));
        checkAgainstTheRpcs(rig, readFile(RP_BLIPTOASTER_ROM_PATH), "");
    }
    SECTION("Master System") {
        Rig rig;
        REQUIRE(rig.plan({{1, "rp.mesen.sms"}}));
        checkAgainstTheRpcs(rig, readFile(RP_SMS_ROM_PATH), "");
    }
}

TEST_CASE("each capability answers what the system-id RPCs and the ROM file answer, on a GBA",
          "[engine-graph][capability]") {
    const auto rom = resourceFixture("/roms/nanoloop287d.gba");
    if (rom.empty()) SKIP("the GBA fixture ROM is absent from the resources tree");
    Rig rig;
    DocSystem gba{1, "rp.mesen.gba"};
    gba.rom = rig.e.putResource("rom", rom);
    REQUIRE(gba.rom != 0);
    REQUIRE(rig.plan({gba}));
    std::string title;
    // The game title field, $A0-$AB, which this homebrew cart leaves blank (as the ROM stores it, so empty).
    for (std::size_t i = 0xA0; i < 0xAC && rom[i] >= 0x20 && rom[i] <= 0x7e; ++i)
        title.push_back(static_cast<char>(rom[i]));
    checkAgainstTheRpcs(rig, rom, title);
}

TEST_CASE("a Game Boy title that fills its field stops before the CGB flag, not at a NUL",
          "[engine-graph][capability]") {
    // Fifteen printable bytes fill $0134-$0142, so nothing NUL-terminates the field and $0143 holds the CGB flag:
    // appending it would make the title a 16-character string ending in U+0080.
    Rig rig;
    auto rom = readFile(RP_MGB_ROM_PATH);
    REQUIRE(rom.size() > 0x143);
    const std::string title = "LSDj-v9.3.3aboy";
    REQUIRE(title.size() == 15);
    for (std::size_t i = 0; i < title.size(); ++i) rom[0x134 + i] = static_cast<std::uint8_t>(title[i]);
    rom[0x143] = 0x80;
    DocSystem gb{1, rp::host::test::kGbType};
    gb.rom = rig.e.putResource("rom", rom);
    REQUIRE(gb.rom != 0);
    REQUIRE(rig.plan({gb}));
    CHECK(rig.now(rig.submit("sys-1", kRom, "title")) == bytesOf(title));
}

TEST_CASE("a write completes once the block after its drain has published it, and the next read sees it",
          "[engine-graph][capability]") {
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    for (int b = 0; b < 4; ++b) rig.block();
    const std::vector<std::uint8_t> written = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x23, 0x45, 0x67};
    const auto before = rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x40, 8));
    REQUIRE(before != written);
    rig.invoker.setAudioThreadOwns(true);  // pushes queue for the block thread, which this thread is now
    const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0x40, 0, written);
    REQUIRE(write.id != 0);
    CHECK(rig.e.drainJobs().empty());  // in the ring
    rig.invoker.drainInto(rig.e);
    CHECK(rig.e.drainJobs().empty());  // applied, not yet published: a read now would see the old bytes
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x40, 8)) == before);
    rig.block();
    const auto done = rig.completion(write.id);
    CHECK(done.ok);
    CHECK(done.bytes.empty());
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x40, 8)) == written);
    // The system-id read agrees, at the same offset of the same region.
    const auto ram = bytesOf(*rig.rpc.readRam(1));
    CHECK(std::vector<std::uint8_t>(ram.begin() + 0x40, ram.begin() + 0x48) == written);
    rig.invoker.setAudioThreadOwns(false);
}

TEST_CASE("writes submitted while a real audio thread runs blocks complete, each visible to the read after it",
          "[engine-graph][capability]") {
    // A second thread drains and runs blocks as a host's audio thread does, while this one submits, polls and reads,
    // the old system-id write among them: what ThreadSanitizer holds the job records' hand-off to.
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    rig.block();
    rig.invoker.setAudioThreadOwns(true);
    std::atomic<bool> stop{false};
    std::thread audio([&] {
        std::vector<float> l(256), r(256);
        float* outs[2] = {l.data(), r.data()};
        while (!stop.load(std::memory_order_acquire)) {
            rig.invoker.drainInto(rig.e);
            rig.e.processBlock(256, outs, 2);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    // A failing REQUIRE below throws: the thread must be stopped and joined on the way out, or std::thread's destructor
    // calls std::terminate and the failure is never reported, and only then is the engine's ownership bit cleared, or
    // the Rig destroys a GraphHost that believes an audio thread still owns it. `stopNow` is the same three steps the
    // end of the case takes, so the passing and the failing path tear down alike.
    struct StopOnExit {
        std::atomic<bool>& stop;
        std::thread&        thread;
        QueuedInvoker&      invoker;
        bool                stopped = false;
        void stopNow() {
            if (stopped) return;
            stopped = true;
            stop.store(true, std::memory_order_release);
            if (thread.joinable()) thread.join();
            invoker.setAudioThreadOwns(false);
        }
        ~StopOnExit() { stopNow(); }
    } stopOnExit{stop, audio, rig.invoker};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (std::uint8_t k = 1; k <= 16; ++k) {
        const std::vector<std::uint8_t> written = {k, static_cast<std::uint8_t>(k * 3), static_cast<std::uint8_t>(~k)};
        const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0x100 + k * 4, 0, written);
        REQUIRE(write.id != 0);
        CHECK(rig.invoker.writeRam(1, 0x200 + k, {k}));
        bool done = false;
        while (!done && std::chrono::steady_clock::now() < deadline) {
            for (const auto& c : rig.e.drainJobs())
                if (c.id == write.id && c.done) {
                    CHECK(c.ok);
                    done = true;
                }
            if (!done) std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        REQUIRE(done);
        CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x100 + k * 4, 3)) == written);
    }
    stopOnExit.stopNow();
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x201, 1)) == std::vector<std::uint8_t>{1});
    CHECK(rig.e.jobsOutstanding() == 0);
}

TEST_CASE("while no audio thread runs, a write completes before submitJob returns, published for the next read",
          "[engine-graph][capability]") {
    // The CLI between renders: the push flushes inline and the engine republishes the region there and then.
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kNesType}}));
    rig.block();
    const std::vector<std::uint8_t> written = {1, 2, 3, 4, 5};
    const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0x300, 0, written);
    REQUIRE(write.id != 0);
    CHECK(rig.completion(write.id).ok);
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x300, 5)) == written);
    // The system-id write takes the same path, landing on the core and queuing no completion.
    const std::vector<std::uint8_t> poked = {9, 8, 7};
    REQUIRE(rig.rpc.writeRam(1, 0x310, bytestringOf(poked)));
    auto live = rig.e.findSystem(1)->getMemory(rp::MemoryType::Ram, rp::AccessType::Read);
    CHECK(std::vector<std::uint8_t>(live.data() + 0x310, live.data() + 0x313) == poked);
    CHECK(rig.e.drainJobs().empty());
    CHECK(rig.e.jobsOutstanding() == 0);
}

TEST_CASE("a write whose node leaves the plan before it is published, or that the engine discards, completes with an "
          "error, touching no freed core",
          "[engine-graph][capability]") {
    Rig rig;
    const char* const gb = rp::host::test::kGbType;
    const std::vector<std::uint8_t> written = {0x5a, 0xa5};
    SECTION("the node removed") {
        REQUIRE(rig.plan({{1, gb}, {2, gb}}));
        rig.block();
        rig.invoker.setAudioThreadOwns(true);
        const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0, 0, written);
        REQUIRE(write.id != 0);
        // Published behind the write, so the drain applies the write to system 1's core (alive: its plan is not yet
        // reclaimed) and then adopts the plan without it, which the block runs.
        REQUIRE(rig.plan({{2, gb}}));
        rig.invoker.drainInto(rig.e);
        rig.block();
        const auto c = rig.completion(write.id);
        CHECK_FALSE(c.ok);
        CHECK(c.error == "node sys-1 left the plan before the job completed");
        rig.invoker.setAudioThreadOwns(false);
        rig.e.reclaim();
        CHECK_FALSE(rig.e.coreLive(1));
    }
    SECTION("the plan replaced by one that rebuilds the system under a new id") {
        REQUIRE(rig.plan({{1, gb}}));
        rig.block();
        rig.invoker.setAudioThreadOwns(true);
        const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0, 0, written);
        REQUIRE(write.id != 0);
        REQUIRE(rig.plan({{3, gb}}));  // a rebuild, as the stores make one: the same ROM under a fresh id
        rig.invoker.drainInto(rig.e);
        rig.block();
        CHECK(rig.completion(write.id).error == "node sys-1 left the plan before the job completed");
        rig.invoker.setAudioThreadOwns(false);
    }
    SECTION("the plan replaced by one that keeps the node: the write completes") {
        REQUIRE(rig.plan({{1, gb}}));
        rig.block();
        rig.invoker.setAudioThreadOwns(true);
        const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0, 0, written);
        REQUIRE(write.id != 0);
        REQUIRE(rig.plan({{1, gb}, {2, gb}}));
        rig.invoker.drainInto(rig.e);
        rig.block();
        CHECK(rig.completion(write.id).ok);
        CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0, 2)) == written);
        rig.invoker.setAudioThreadOwns(false);
    }
    SECTION("discarded at teardown") {
        REQUIRE(rig.plan({{1, gb}}));
        rig.block();
        rig.invoker.setAudioThreadOwns(true);
        const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0, 0, written);
        REQUIRE(write.id != 0);
        REQUIRE(rig.plan({{2, gb}}));
        rig.invoker.freePending();  // the audio thread stopped: the plan is adopted, the write dropped
        rig.invoker.setAudioThreadOwns(false);
        CHECK(rig.completion(write.id).error == "the engine stopped before the job ran");
    }
    CHECK(rig.e.jobsOutstanding() == 0);
}

TEST_CASE("the jobs outstanding are bounded: a submission past the bound is refused until a drain makes room",
          "[engine-graph][capability]") {
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    rig.invoker.setAudioThreadOwns(true);
    // Reads completed and writes still in the ring count alike: each holds room for its completion.
    const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0, 0, {1});
    REQUIRE(write.id != 0);
    for (std::size_t k = 1; k < Engine::kMaxJobs; ++k) REQUIRE(rig.submit("sys-1", kRom, "title").id != 0);
    CHECK(rig.e.jobsOutstanding() == Engine::kMaxJobs);
    const auto refused = rig.submit("sys-1", kRom, "title");
    CHECK(refused.id == 0);
    CHECK(refused.error == "busy: 64 jobs are outstanding; drain their completions first");
    CHECK(rig.e.drainJobs().size() == Engine::kMaxJobs - 1);  // the write is still in the ring
    REQUIRE(rig.submit("sys-1", kRom, "title").id != 0);
    rig.invoker.drainInto(rig.e);
    rig.block();
    const auto rest = rig.e.drainJobs();
    REQUIRE(rest.size() == 2);
    CHECK(rest[0].done);
    CHECK(rest[1].done);
    rig.invoker.setAudioThreadOwns(false);
    CHECK(rig.e.jobsOutstanding() == 0);
}

TEST_CASE("a submission is refused, at once and with the reason, for what it cannot do", "[engine-graph][capability]") {
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    rig.block();
    const auto size = rig.e.ramSize(1);
    REQUIRE(size > 0);
    const auto refusal = [&](rp::host::JobSubmit s) {
        CHECK(s.id == 0);
        return s.error;
    };
    CHECK(refusal(rig.submit("sys-9", kMemory, "regions")) == "the running graph has no node sys-9");
    CHECK(refusal(rig.submit("out", kMemory, "regions")) == "node out presents no rp.system.memory/1");
    CHECK(refusal(rig.submit("split-1", kRom, "title")) == "node split-1 presents no rp.system.rom/1");
    CHECK(refusal(rig.submit("sys-1", "rp.system.video/1", "frame")) == "unknown capability rp.system.video/1");
    CHECK(refusal(rig.submit("sys-1", kMemory, "erase")) == "rp.system.memory/1 has no operation 'erase'");
    CHECK(refusal(rig.submit("sys-1", kRom, "upload")) == "rp.system.rom/1 has no operation 'upload'");
    CHECK(refusal(rig.submit("sys-1", kMemory, "read", "vram")) == "node sys-1 has no memory region 'vram'");
    CHECK(refusal(rig.submit("sys-1", kMemory, "read", "ram", static_cast<std::uint32_t>(size), 0)) ==
          "the read runs past region ram (" + std::to_string(size) + " bytes)");
    CHECK(refusal(rig.submit("sys-1", kMemory, "write", "ram", 0, 0, {})) == "an empty write");
    CHECK(refusal(rig.submit("sys-1", kMemory, "write", "ram", static_cast<std::uint32_t>(size) - 1, 0, {1, 2})) ==
          "the write runs past region ram (" + std::to_string(size) + " bytes)");
    CHECK(refusal(rig.submit("sys-1", kSram, "restore", "", 0, 0, {0})).find("restored by rebuilding it") !=
          std::string::npos);
    // A refusal takes no id and leaves no completion.
    CHECK(rig.e.drainJobs().empty());
    CHECK(rig.e.jobsOutstanding() == 0);
    // The ids count up from the last taken, refusals using none.
    const auto a = rig.submit("sys-1", kRom, "title");
    const auto b = rig.submit("sys-1", kRom, "title");
    CHECK(b.id == a.id + 1);
    CHECK(rig.e.drainJobs().size() == 2);
}

TEST_CASE("a warmed block with capability writes in flight allocates and frees nothing", "[engine-graph][capability]") {
    // The drain applies them (a job's and an old system-id one), the block runs and publishes, and the publish completes
    // the job's: the records and their bytes are the control thread's, made before the window and freed after it.
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    for (int b = 0; b < 4; ++b) rig.block(256);
    rig.invoker.setAudioThreadOwns(true);
    const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0x20, 0, {1, 2, 3, 4});
    REQUIRE(write.id != 0);
    REQUIRE(rig.invoker.writeRam(1, 0x30, {5, 6, 7, 8}));
    rp::test::beginCountingAllocations();
    rig.invoker.drainInto(rig.e);
    rig.block(64);
    const auto counted = rp::test::endCountingAllocations();
    CHECK(counted.allocations == 0);
    CHECK(counted.frees == 0);
    CHECK(rig.completion(write.id).ok);
    rig.invoker.setAudioThreadOwns(false);
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x20, 4)) == std::vector<std::uint8_t>{1, 2, 3, 4});
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x30, 4)) == std::vector<std::uint8_t>{5, 6, 7, 8});
}

TEST_CASE("writes the audio thread's stop flushes complete at the next drain, published", "[engine-graph][capability]") {
    // Stopping the audio thread applies what is still queued on the stopping thread, with no block to publish it: the
    // next drain (or submission) republishes and finishes them, so neither waits for a render.
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    rig.block();
    rig.invoker.setAudioThreadOwns(true);
    const auto a = rig.submit("sys-1", kMemory, "write", "ram", 0x50, 0, {0x11, 0x22});
    const auto b = rig.submit("sys-1", kMemory, "write", "ram", 0x60, 0, {0x33});
    REQUIRE(a.id != 0);
    REQUIRE(b.id != 0);
    rig.invoker.setAudioThreadOwns(false);
    const auto done = rig.e.drainJobs();
    REQUIRE(done.size() == 2);
    CHECK(done[0].id == a.id);
    CHECK(done[1].id == b.id);
    CHECK(done[0].ok);
    CHECK(done[1].ok);
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x50, 2)) == std::vector<std::uint8_t>{0x11, 0x22});
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x60, 1)) == std::vector<std::uint8_t>{0x33});
}

TEST_CASE("a write a block that runs nothing leaves unpublished completes at the next block that runs",
          "[engine-graph][capability]") {
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    rig.block();
    rig.invoker.setAudioThreadOwns(true);
    const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0x70, 0, {0x44, 0x55});
    REQUIRE(write.id != 0);
    rig.invoker.drainInto(rig.e);
    rig.block(0);  // an empty block runs nothing, the publish included
    CHECK(rig.e.drainJobs().empty());
    // Longer than the plan's buffers: refused, silent, and nothing published.
    std::vector<float> l(Engine::kDefaultMaxBlockFrames + 1), r(Engine::kDefaultMaxBlockFrames + 1);
    float* outs[2] = {l.data(), r.data()};
    rig.e.processBlock(Engine::kDefaultMaxBlockFrames + 1, outs, 2);
    CHECK(rig.e.drainJobs().empty());
    rig.block();
    CHECK(rig.completion(write.id).ok);
    CHECK(rig.now(rig.submit("sys-1", kMemory, "read", "ram", 0x70, 2)) == std::vector<std::uint8_t>{0x44, 0x55});
    rig.invoker.setAudioThreadOwns(false);
    rig.e.reclaim();  // reports the oversized block, once
}

TEST_CASE("a full command ring refuses a write, the job's and the system-id one alike", "[engine-graph][capability]") {
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    rig.block();
    rig.invoker.setAudioThreadOwns(true);
    DspCommand bpm;
    bpm.kind = DspCommand::Kind::SetBpm;
    bpm.setBpm = {120.0};
    std::size_t pushed = 0;
    while (rig.e.pushCommand(bpm)) ++pushed;
    CHECK(pushed > 0);
    CHECK(rig.submit("sys-1", kMemory, "write", "ram", 0, 0, {1}).error == "busy: the command ring is full");
    CHECK_FALSE(rig.invoker.writeRam(1, 0, {1}));
    CHECK_FALSE(rig.rpc.writeRam(1, 0, bytestringOf({1})));
    CHECK(rig.e.jobsOutstanding() == 0);  // a refused write keeps no record
    rig.invoker.drainInto(rig.e);
    const auto write = rig.submit("sys-1", kMemory, "write", "ram", 0, 0, {1});
    CHECK(write.id != 0);
    rig.invoker.drainInto(rig.e);
    rig.block();
    CHECK(rig.completion(write.id).ok);
    rig.invoker.setAudioThreadOwns(false);
}

TEST_CASE("a job a device thread runs reports its progress once per change, then its result or its error",
          "[engine-graph][capability]") {
    // The engine plays its part of a device's job (CapabilityJobs.hpp) with this test as the device: a record adopted on
    // the control thread, written by another thread (progress, then the result or the error, then the final state, last
    // and with release), drained here. What ThreadSanitizer holds the device thread's hand-off to.
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    const auto run = [&](bool succeed) {
        auto job = std::make_unique<rp::host::Job>();
        rp::host::Job* record = job.get();
        record->total.store(4, std::memory_order_relaxed);
        const auto adopted = rig.e.adoptJob(std::move(job));
        REQUIRE(adopted.id != 0);
        std::atomic<std::uint32_t> acknowledged{0};
        std::thread device([&] {
            for (std::uint32_t k = 1; k <= 3; ++k) {
                record->progress.store(k, std::memory_order_release);
                while (acknowledged.load(std::memory_order_acquire) < k) std::this_thread::yield();
            }
            if (succeed) {
                record->result = {0xca, 0xfe};
                record->progress.store(4, std::memory_order_relaxed);
                record->state.store(rp::host::Job::Done, std::memory_order_release);
            } else {
                record->error = "the cart did not answer";
                record->failure.store(rp::host::Job::Device, std::memory_order_relaxed);
                record->state.store(rp::host::Job::Failed, std::memory_order_release);
            }
        });
        // Let the device finish even when the deadline passed before its last acknowledgement, and join it on the way
        // out of any failure too: a joinable thread's destructor calls std::terminate, which would hide the failure.
        struct FinishOnExit {
            std::atomic<std::uint32_t>& acknowledged;
            std::thread&                device;
            ~FinishOnExit() {
                acknowledged.store(4, std::memory_order_release);
                if (device.joinable()) device.join();
            }
        } finish{acknowledged, device};
        std::vector<std::uint32_t> seen;
        std::optional<rp::host::JobCompletion> final;
        while (!final && std::chrono::steady_clock::now() < deadline) {
            for (auto& c : rig.e.drainJobs()) {
                CHECK(c.id == adopted.id);
                if (c.done) {
                    final = std::move(c);
                    continue;
                }
                CHECK(c.total == 4);
                seen.push_back(c.progress);
                // A second drain before the device moves on reports nothing: once per change. (The final progress is
                // stored just before the result, so a drain between the two reports it too; the device does not wait
                // for that one.)
                if (c.progress <= 3) CHECK(rig.e.drainJobs().empty());
                acknowledged.store(c.progress, std::memory_order_release);
            }
            std::this_thread::yield();
        }
        REQUIRE(final);
        const std::vector<std::uint32_t> steps = {1, 2, 3}, stepsAndLast = {1, 2, 3, 4};
        CHECK((seen == steps || (succeed && seen == stepsAndLast)));
        CHECK(final->ok == succeed);
        if (succeed) {
            CHECK(final->bytes == std::vector<std::uint8_t>{0xca, 0xfe});
            CHECK(final->progress == 4);
        } else {
            CHECK(final->error == "the cart did not answer");
        }
        CHECK(rig.e.jobsOutstanding() == 0);
    };
    SECTION("its result") { run(true); }
    SECTION("its error") { run(false); }
}

TEST_CASE("a node that is no system node presents no capability yet", "[engine-graph][capability]") {
    // The device seam (Engine::submitDeviceJob): the refusal a node whose type presents none of the interfaces gets.
    Rig rig;
    REQUIRE(rig.plan({{1, rp::host::test::kGbType}}));
    for (const char* capability : {kMemory, kSram, kRom}) {
        const auto refused = rig.submit("split-1", capability, "dump");
        CHECK(refused.id == 0);
        CHECK(refused.error == std::string("node split-1 presents no ") + capability);
    }
}

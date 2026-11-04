// The cart's production adapters behind a real Engine: Engine::submitJob and the staged MIDI reach rp.n8.nes, which
// hands them to N8CartLink, N8Host and N8Link, down to a fake serial port (packages/n8/native/test/FakeN8.hpp, the
// model of the cart retroplug-n8-test drives N8Host with). The adapters are the N8 pack's provider (N8Provider.hpp),
// started into the engine's registry as a product starts it, with FakeN8's ports in place of deps/serial's.
// N8Node.test.cpp holds the node to a fake CartLink and retroplug-n8-test holds the link and its jobs with no engine;
// each forward between them is thin, and this is the one case that composes them: a note through the whole path, then
// in one section a ROM title and a refused dump through Engine::submitJob, a job abandoned by a replan with the
// note-offs of what the node held written once the stream resumes, and an upload and the title it leaves, and in
// another a host's teardown order with a note held (clearGraph, whose destroy releases it, then the provider stopped:
// its link cleared and ~N8Host's join), each section reporting on its own.
//
// Here rather than in retroplug-n8-test because that binary links no engine, cores or ROM fixtures, and because this
// binary runs in `pnpm test:plugin` (CI) where `pnpm test:n8` does not; no serial port is opened.
//
// Run via `pnpm test:plugin engine-graph`, or `build/bin/retroplug-engine-graph-test "[n8-adapters]"`.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "FakeN8.hpp"
#include "GraphDocs.hpp"
#include "TestDevices.hpp"
#include "host/devices/ProviderHost.hpp"
#include "host/engine/CapabilityJobs.hpp"
#include "host/engine/Engine.hpp"
#include "n8/desktop/N8Host.hpp"
#include "n8/desktop/N8Provider.hpp"

namespace {

using rp::host::JobRequest;
using rp::host::test::DocExtras;
using rp::host::test::DocSystem;
using rp::host::test::docNode;
using rp::host::test::docWire;
using rp::host::test::graphRequest;
using rp::host::test::stereoDocument;
using retroplug::test::FakeN8;
using Bytes = std::vector<std::uint8_t>;

constexpr double        kRate   = 48000.0;
constexpr std::uint32_t kFrames = 64;

void block(Engine& e) {
    std::vector<float> l(kFrames), r(kFrames);
    float*             outs[2] = {l.data(), r.data()};
    e.processBlock(kFrames, outs, 2);
}

// One NES and its cart node, host MIDI wired to both, as N8Node.test.cpp's documents; and the same without the cart.
Engine::GraphRequest withCart(std::vector<DocSystem>& systems) {
    return graphRequest(stereoDocument(systems, DocExtras{{docNode("midi-in", "rp.host.midi-in"),
                                                           docNode("cart", "rp.n8.nes")},
                                                          {docWire("midi-in.midi", "sys-1.midi"),
                                                           docWire("midi-in.midi", "cart.midi")}}),
                        {1});
}
Engine::GraphRequest withoutCart(std::vector<DocSystem>& systems) {
    return graphRequest(stereoDocument(systems, DocExtras{{docNode("midi-in", "rp.host.midi-in")},
                                                          {docWire("midi-in.midi", "sys-1.midi")}}),
                        {1});
}

JobRequest request(const char* capability, const char* op, const char* name = "") {
    JobRequest r;
    r.nodeId     = "cart";
    r.capability = capability;
    r.op         = op;
    r.name       = name;
    return r;
}

template <class Pred>
bool waitUntil(Pred pred, int ms = 3000) {
    for (int i = 0; i < ms && !pred(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return pred();
}

// Drain the engine until job `id` completes, keeping what the drains returned; null when it never does.
std::optional<rp::host::JobCompletion> completion(Engine& e, std::uint32_t id) {
    std::optional<rp::host::JobCompletion> done;
    waitUntil([&] {
        for (auto& c : e.drainJobs())
            if (c.id == id && c.done) done = c;
        return done.has_value();
    });
    return done;
}

std::ptrdiff_t logIndex(FakeN8& dev, const std::string& entry) {
    std::lock_guard<std::mutex> l(dev.mu);
    return std::find(dev.log.begin(), dev.log.end(), entry) - dev.log.begin();
}

// The provider a case started, stopped as a product stops it: the engine's plans first, while the link is installed,
// then the provider. A holder, so a REQUIRE that fails mid-case still stops it, in that order, before the FakeN8 whose
// port its serial thread polls goes out of scope.
class StartedProvider {
public:
    StartedProvider(Engine& engine, void* self) : engine_(engine), self_(self) {}
    StartedProvider(const StartedProvider&) = delete;
    StartedProvider& operator=(const StartedProvider&) = delete;
    ~StartedProvider() { stop(); }

    void* get() const { return self_; }
    void  stop() {
        if (!self_) return;
        engine_.clearGraph();
        rp_n8_provider()->destroy(std::exchange(self_, nullptr));
    }

private:
    Engine& engine_;
    void*   self_;
};

}  // namespace

TEST_CASE("the cart's production adapters behind a real Engine: a note, then the jobs, or a host's teardown",
          "[engine-graph][n8-adapters]") {
    FakeN8 dev;
    dev.files["EDN8/gamedata/game.nes/bram.srm"] = Bytes(4096, 0x5A);
    dev.fileReadDelayMs                          = 20;  // a dump of eight 512-byte blocks takes ~160 ms
    const auto cfgDir = std::filesystem::temp_directory_path() / "rp-n8-adapters-test";
    std::filesystem::create_directories(cfgDir);
    std::filesystem::remove(cfgDir / "n8.cfg");
    {
        // Started as a product starts it (the generated rp_start_providers): after the engine, into its registry, the
        // provider's ports FakeN8's rather than deps/serial's. It owns the N8Host and the cart link it installs.
        Engine                 e(kRate);
        rp::host::ProviderHost deviceHost(e, cfgDir.string());
        const auto             ports = [] { return std::vector<retroplug::N8PortDto>{{"/dev/fake", true}}; };
        StartedProvider provider(e, rp::n8::createProvider(deviceHost.get(), retroplug::test::factoryFor(dev), ports));
        REQUIRE(provider.get() != nullptr);
        retroplug::N8Host&     host = rp::n8::providerHost(provider.get());
        rp::host::test::Roms   roms;
        std::vector<DocSystem> systems{{1, rp::host::test::kNesType}};
        withRoms(e, systems, roms);
        REQUIRE(e.setGraph(withCart(systems)).ok);
        host.setPort("/dev/fake");
        host.connect(true);
        REQUIRE(host.link().isConnected());
        host.link().setLookaheadMs(0);

        // A note staged at the engine reaches the cart's FIFO, through the node, N8CartLink and N8Link.
        e.stageMidi(0, Bytes{0x90, 60, 100});
        block(e);
        REQUIRE(waitUntil([&] { return dev.count("fifo:90 3C 64") == 1; }));

        SECTION("a title, a refusal, an abandon and an upload through Engine::submitJob") {
            // rom.title answers from what this host booted, which is nothing yet, at once.
            const auto title = e.submitJob(request(RP_SYSTEM_ROM_INTERFACE, "title"));
            CHECK(title.id == 0);
            CHECK(title.error == "nothing this host booted is running");

            // A dump while a game runs is refused at submission, from the config block read between chunks, and the
            // stream never paused for it (the note-off the block carries reaches the cart).
            {
                std::lock_guard<std::mutex> l(dev.mu);
                dev.menu = false;  // the config block names mapper 0, a game
            }
            const auto atGame = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump", "game.nes"));
            CHECK(atGame.id == 0);
            CHECK(atGame.error == "the battery file can only be reached at the N8 menu: power-cycle to the menu first");
            e.stageMidi(0, Bytes{0x80, 60, 0});
            block(e);
            REQUIRE(waitUntil([&] { return dev.count("fifo:80 3C 00") == 1; }));
            {
                std::lock_guard<std::mutex> l(dev.mu);
                dev.menu = true;
            }

            // A dump at the menu is taken; a replan without the cart node abandons it at reclaim (Removed, through the
            // engine's record), and the note the node still held (62) gets its note-off once streaming resumes.
            e.stageMidi(0, Bytes{0x90, 62, 100});
            block(e);
            REQUIRE(waitUntil([&] { return dev.count("fifo:90 3E 64") == 1; }));
            const auto dump = e.submitJob(request(RP_SYSTEM_SRAM_INTERFACE, "dump", "game.nes"));
            REQUIRE(dump.id != 0);
            REQUIRE(waitUntil([&] { return dev.count("file-rd") >= 1; }));
            REQUIRE(e.setGraph(withoutCart(systems)).ok);
            block(e);
            block(e);  // the plan that held the node is reclaimed here at the latest
            const auto removed = completion(e, dump.id);
            REQUIRE(removed.has_value());
            CHECK_FALSE(removed->ok);
            CHECK(removed->error == "node cart left the plan before the job completed");
            REQUIRE(waitUntil([&] { return !host.cartJobBusy(); }));
            REQUIRE(waitUntil([&] { return dev.count("fifo:80 3E 00") == 1; }));
            CHECK(dev.count("file-rd") < 8);  // it stopped early
            CHECK(logIndex(dev, "file-rd") < logIndex(dev, "fifo:80 3E 00"));
            CHECK(host.link().isConnected());

            // The node again, and an upload through it: the menu installs and boots the image, streaming stops, and the
            // title is the name it was booted under.
            REQUIRE(e.setGraph(withCart(systems)).ok);
            block(e);
            JobRequest upload = request(RP_SYSTEM_ROM_INTERFACE, "upload", "game.nes");
            upload.bytes      = {'N', 'E', 'S', 0x1A, 1, 1, 0x50, 0x40};
            upload.bytes.resize(16 + 16 * 1024 + 8 * 1024, 0xEA);
            const auto boot = e.submitJob(upload);
            REQUIRE(boot.id != 0);
            const auto booted = completion(e, boot.id);
            REQUIRE(booted.has_value());
            CHECK(booted->ok);
            CHECK_FALSE(host.link().isConnected());  // a boot stops streaming
            {
                std::lock_guard<std::mutex> l(dev.mu);
                CHECK(dev.booted == "usb-games/game.nes");
            }
            const auto named = e.submitJob(request(RP_SYSTEM_ROM_INTERFACE, "title"));
            REQUIRE(named.id != 0);
            const auto answered = completion(e, named.id);
            REQUIRE(answered.has_value());
            CHECK(answered->bytes == Bytes{'g', 'a', 'm', 'e', '.', 'n', 'e', 's'});

            // Connected again after the boot, then a product's teardown order, which the other section holds with a
            // note held: the plans, then the providers.
            host.connect(true);
            REQUIRE(host.link().isConnected());
            provider.stop();
        }
        SECTION("a host's teardown with a note held: the plans first, then the links") {
            // A product's order: the plans first, while the link is installed, so the node's destroy hands the note
            // it holds (60) to the link and releases its handle there; then the providers. The note-off reaches the
            // cart through the link's serial thread, which the provider's ~N8Host joins.
            e.clearGraph();
            REQUIRE(waitUntil([&] { return dev.count("fifo:80 3C 00") == 1; }));
            CHECK(host.link().isConnected());
            provider.stop();
        }
    }
    // The provider's ~N8Host joined the serial thread with nothing left that borrows it.
    std::lock_guard<std::mutex> l(dev.mu);
    CHECK(dev.filesOpen == 0);
}

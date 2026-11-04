// Guards the cart's capability jobs (N8Host::submitCartJob, docs/modular-graph-phase-c5-design.md section 6) and the
// pause they run in (N8Link::PausedJob): a job runs on the link's serial thread over the port the link already holds,
// what was pushed before the pause is delivered first, a push during it is dropped and counted, and the job completes
// once streaming has resumed, on the same port and with the reliable link's sequence state intact. A ROM upload boots
// through the cart's menu and stops streaming; the battery jobs read and write the battery file the N8 OS keeps on the
// SD card, at the menu only, and never the cart's own battery.
//
// The fake device is FakeN8.hpp's model of the cart's USB side, which the engine-graph suite drives through a real
// Engine too. The jobs' records and the report interface they finish through are FakeJobs.hpp's, a host of the pack's
// own rather than the engine's.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "FakeJobs.hpp"
#include "n8/desktop/Edio.hpp"
#include "n8/desktop/N8CartLink.hpp"
#include "n8/desktop/N8Host.hpp"
#include "FakeN8.hpp"

using retroplug::Edio;
using retroplug::ISerialPort;
using retroplug::N8CartLink;
using retroplug::N8Host;
using retroplug::N8Link;
using retroplug::N8PortDto;
using rp::n8::test::Job;
using rp::n8::test::JobRequest;
using Bytes = std::vector<std::uint8_t>;
using retroplug::test::FakeN8;
using retroplug::test::factoryFor;

namespace {

std::string tempDir() {
    const auto dir = std::filesystem::temp_directory_path() / "rp-n8-cartjobs-test";
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "n8.cfg");
    return dir.string();
}

template <class Pred>
bool waitUntil(Pred pred, int ms = 3000) {
    for (int i = 0; i < ms && !pred(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return pred();
}

JobRequest request(const char* capability, const char* op) {
    JobRequest r;
    r.nodeId     = "cart";
    r.capability = capability;
    r.op         = op;
    return r;
}

// A connected host on the fake cart, streaming with no lookahead. `link` false skips the reliable link's probe.
struct Rig {
    FakeN8     dev;
    N8Host     host{factoryFor(dev), [] { return std::vector<N8PortDto>{{"/dev/fake", true}}; }, tempDir()};
    N8CartLink cart{host};
    int        owner = 0;  // the address that stands for the node submitting
    // Every case ends with no file left open on the card, whatever its jobs did.
    ~Rig() {
        std::lock_guard<std::mutex> l(dev.mu);
        CHECK(dev.filesOpen == 0);
    }

    void connect() {
        host.setPort("/dev/fake");
        host.connect(true);
        REQUIRE(host.link().isConnected());
        host.link().setLookaheadMs(0);
    }
    bool submit(const JobRequest& r, Job& job, std::string* error = nullptr) {
        // As Engine::submitDeviceJob hands it over: the request as C, the record as its rp_job handle, and the report
        // interface over that record (FakeJobs.hpp's, a host's own, as the engine's is its own).
        const rp_job_request request = rp::n8::test::requestView(r);
        char                 why[256] = {};  // as Engine::kDeviceJobErrorBytes
        const bool ok =
            cart.submitJob(request, rp::n8::test::handle(&job), rp::n8::test::jobReports(), &owner, why, sizeof why);
        if (error) *error = why;
        return ok;
    }
    void push(std::initializer_list<std::uint8_t> bytes) {
        const Bytes b(bytes);
        cart.push(0, b.data(), b.size(), false, 48000.0);
    }
};

}  // namespace

namespace {

// The battery file the N8 OS keeps for game.nes, and a save to put there.
const char* const kGameBattery = "EDN8/gamedata/game.nes/bram.srm";
Bytes saveOf(std::size_t n, unsigned seed) {
    Bytes b(n);
    for (std::size_t i = 0; i < n; ++i) b[i] = static_cast<std::uint8_t>(i * seed + 1);
    return b;
}
JobRequest battery(const char* op, const char* rom = "game.nes") {
    auto r = request(RP_SYSTEM_SRAM_INTERFACE, op);
    r.name = rom;
    return r;
}

// An iNES image of mapper 69 (Sunsoft 5B, a cart with its own sound chip), `kb` KB of PRG behind its header.
Bytes inesImage(std::size_t kb) {
    Bytes rom = {'N', 'E', 'S', 0x1A, 16, 4, 0x50, 0x40};
    rom.resize(16 + kb * 1024, 0xEA);
    return rom;
}

JobRequest uploadOf(const Bytes& rom) {
    auto r = request(RP_SYSTEM_ROM_INTERFACE, "upload");
    r.bytes = rom;
    r.name = "game.nes";
    return r;
}

}  // namespace

TEST_CASE("a dump job pauses streaming on the port it has, delivers what was pushed first, reads the battery file "
          "with progress, and resumes before it completes", "[n8][cart-jobs]") {
    Rig rig;
    const Bytes save = saveOf(3000, 7);
    rig.dev.files[kGameBattery] = save;
    rig.dev.fileReadDelayMs = 20;
    rig.connect();

    // A note pushed 20 ms ahead and then a job at once: the note goes out at its own time, before the first read.
    rig.host.link().setLookaheadMs(20);
    rig.push({0x90, 0x3C, 0x7F});
    rig.push({0x80, 0x3C, 0x00});
    rig.host.link().setLookaheadMs(0);
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(rig.host.cartJobBusy());

    // While it runs: the link reads disconnected, and a push goes nowhere and is counted.
    REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
    CHECK_FALSE(rig.cart.connected());
    rig.push({0x90, 0x40, 0x7F});
    CHECK(rig.cart.dropped() == 1);

    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == save);
    CHECK(job.total.load() == save.size());     // a block a step, in bytes
    CHECK(job.progress.load() == save.size());
    // Completed after streaming resumed, on the one port the link opened.
    CHECK(rig.cart.connected());
    CHECK_FALSE(rig.host.cartJobBusy());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.opens == 1);
        const auto at = [&](const std::string& e) {
            return std::find(rig.dev.log.begin(), rig.dev.log.end(), e) - rig.dev.log.begin();
        };
        const auto end = static_cast<std::ptrdiff_t>(rig.dev.log.size());
        CHECK(at("fifo:90 3C 7F") < end);
        CHECK(at("fifo:90 3C 7F") < at("fifo:80 3C 00"));
        CHECK(at("fifo:80 3C 00") < at("file-rd"));
    }
    CHECK(rig.dev.count("file-rd") == (save.size() + 511) / 512);  // one 512-byte block a step
    CHECK(rig.dev.count("srm-") == 0);                              // the cart's own battery: never
    CHECK(rig.dev.count("fifo:90 40") == 0);  // the push the pause dropped never reached the cart

    rig.push({0x91, 0x41, 0x7F});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:91 41 7F") == 1; }));
}

TEST_CASE("a restore job writes the battery file, creating the folder, for the game's next boot", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    const Bytes save = saveOf(8192 + 100, 13);
    auto r = battery("restore", "fresh.nes");
    r.bytes = save;
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(std::string(job.result.begin(), job.result.end()) ==
          R"({"path":"EDN8/gamedata/fresh.nes/bram.srm","created":true})");
    CHECK(job.total.load() == save.size());
    CHECK(job.progress.load() == save.size());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files["EDN8/gamedata/fresh.nes/bram.srm"] == save);
    }
    CHECK(rig.dev.count("srm-") == 0);
    CHECK(rig.cart.connected());

    // A dump reads back what the restore wrote.
    Job back;
    REQUIRE(rig.submit(battery("dump", "fresh.nes"), back));
    REQUIRE(waitUntil([&] { return back.finished(); }));
    CHECK(back.result == save);

    // Arguments refused at submission, the record untouched.
    Job none;
    std::string why;
    auto empty = battery("restore");
    CHECK_FALSE(rig.submit(empty, none, &why));
    CHECK(why == "a battery restore takes 1 to 1048576 bytes");
    auto bad = battery("dump", "../x.nes");
    CHECK_FALSE(rig.submit(bad, none, &why));
    CHECK(why == "a battery job names the ROM's file, a plain file name");
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "dump"), none, &why));
    CHECK(why == "a battery job needs the ROM's file name: name it, or boot the ROM through rom.upload first");
    CHECK(none.state.load() == Job::Queued);
}

TEST_CASE("battery jobs refuse a running game before the stream pauses, so the game keeps its notes, and a dump "
          "finds no file a game never saved", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.menu = false;  // a game runs
    rig.dev.files[kGameBattery] = saveOf(512, 3);
    rig.connect();
    // Refused at submission, from the config block read between chunks: the stream never pauses, so a note pushed
    // afterwards reaches the cart instead of being dropped for a job that was going to refuse anyway.
    for (const char* op : {"dump", "restore"}) {
        auto r = battery(op);
        r.bytes = Bytes(16, 1);
        Job job;
        std::string why;
        CHECK_FALSE(rig.submit(r, job, &why));
        CHECK(why == "the battery file can only be reached at the N8 menu: power-cycle to the menu first");
        CHECK(job.state.load() == Job::Queued);  // the record is untouched
        CHECK_FALSE(rig.host.cartJobBusy());
        CHECK(rig.cart.connected());
        rig.push({static_cast<std::uint8_t>(op[0] == 'd' ? 0x90 : 0x91), 0x3C, 0x7F});
    }
    CHECK(rig.cart.dropped() == 0);
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1 && rig.dev.count("fifo:91 3C 7F") == 1; }));
    CHECK(rig.dev.count("file-") == 0);
    CHECK(rig.dev.count("srm-") == 0);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files[kGameBattery] == saveOf(512, 3));  // untouched
        rig.dev.menu = true;
    }
    Job missing;
    REQUIRE(rig.submit(battery("dump", "never.nes"), missing));
    REQUIRE(waitUntil([&] { return missing.finished(); }));
    CHECK(missing.state.load() == Job::Failed);
    CHECK(missing.error == "no battery file for never.nes (EDN8/gamedata/never.nes/bram.srm)");
    CHECK(rig.cart.connected());
}

TEST_CASE("one cart job at a time, none while disconnected, and an operation the cart does not have is refused",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(2048, 5);
    rig.dev.fileReadDelayMs = 50;
    std::string why;
    Job early;
    CHECK_FALSE(rig.submit(battery("dump"), early, &why));
    CHECK(why == "not connected");

    rig.connect();
    Job job, second;
    REQUIRE(rig.submit(battery("dump"), job));
    CHECK_FALSE(rig.submit(battery("dump"), second, &why));
    CHECK(why == "busy: the cart is running another job");
    // The host's own config writes wait for the job too: a port switch or a disconnect would pull the port from under
    // it.
    rig.host.connect(false);
    CHECK(rig.host.getConfig().enabled);
    REQUIRE(waitUntil([&] { return job.finished() && !rig.host.cartJobBusy(); }));

    CHECK_FALSE(rig.submit(request(RP_SYSTEM_MEMORY_INTERFACE, "read"), second, &why));
    CHECK(why == "the cart presents no rp.system.memory/1");
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "erase"), second, &why));
    CHECK(why == "rp.system.sram/1 has no operation 'erase'");
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "header"), second, &why));
    CHECK(why == "nothing this host booted is running");
}

TEST_CASE("a ROM upload boots through the menu, stops streaming and clears the enabled toggle, and says so",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();  // at the menu: the link streams to it, which skips what is not a command
    REQUIRE(rig.host.getConfig().enabled);
    Bytes rom = {'N', 'E', 'S', 0x1A, 16, 4, 0x50, 0x40};  // mapper 69 (Sunsoft 5B): a cart with its own sound chip
    rom.resize(16 + 20000, 0xEA);

    auto r = request(RP_SYSTEM_ROM_INTERFACE, "upload");
    r.bytes = rom;
    r.name  = "game.nes";
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    REQUIRE(job.state.load() == Job::Done);
    CHECK(std::string(job.result.begin(), job.result.end()) ==
          R"({"path":"usb-games/game.nes","mapIndex":69,"streaming":"stopped"})");
    CHECK(job.progress.load() == rom.size());

    // The game runs, streaming stopped (the port given back), the toggle cleared, the expansion volume at unity for a
    // mapper with expansion audio (the host's default is AUTO).
    CHECK_FALSE(rig.cart.connected());
    CHECK_FALSE(rig.host.getConfig().enabled);
    CHECK_FALSE(rig.host.cartJobBusy());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files["usb-games/game.nes"] == rom);
        CHECK(rig.dev.booted == "usb-games/game.nes");
        CHECK_FALSE(rig.dev.menu);
        CHECK(rig.dev.cfg[3] == N8Host::EXP_VOL_UNITY);
        CHECK(rig.dev.openNow == 0);
    }

    // What it booted answers at once, with no transfer and the link down.
    auto header = request(RP_SYSTEM_ROM_INTERFACE, "header");
    header.length = 16;
    Job h, t;
    REQUIRE(rig.submit(header, h));
    CHECK(h.state.load() == Job::Done);
    CHECK(h.result == Bytes(rom.begin(), rom.begin() + 16));
    REQUIRE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "title"), t));
    CHECK(std::string(t.result.begin(), t.result.end()) == "game.nes");

    // Reconnecting reaches the new game.
    rig.host.connect(true);
    REQUIRE(rig.cart.connected());
    // Still the game this host booted: what it booted still answers after the reconnect.
    Job kept;
    REQUIRE(rig.submit(header, kept));
    CHECK(kept.result == Bytes(rom.begin(), rom.begin() + 16));

    // An upload with no menu to load it is refused before it pauses anything, so no byte (the menu's own commands
    // included) reaches the game's FIFO, and the cart keeps streaming to it.
    const std::size_t menuCommands = rig.dev.count("fifo:2A");  // the first upload's *t, *n and *s
    Job again;
    std::string why;
    CHECK_FALSE(rig.submit(r, again, &why));
    CHECK(why ==
          "the cart is running a game, which has no menu to load a ROM: power-cycle the console to its menu first");
    CHECK(again.state.load() == Job::Queued);
    CHECK(rig.dev.count("fifo:2A") == menuCommands);
    CHECK(rig.cart.connected());
    CHECK(rig.host.getConfig().enabled);
    CHECK_FALSE(rig.host.cartJobBusy());

    auto unnamed = r;
    unnamed.name = "../x.nes";
    CHECK_FALSE(rig.submit(unnamed, again, &why));
    CHECK(why == "an upload names the file it is stored under, a plain file name");
}

TEST_CASE("the reliable link keeps its sequence across a pause: no new sync, the next frame carries the next seq",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.menu = false;
    rig.dev.linkRom = true;  // BlipToaster, which answers the probe
    rig.dev.files[kGameBattery] = saveOf(1024, 3);
    // The fake answers the config block as the menu does while a game runs. That is the one combination which takes a
    // job with the reliable link up (a real cart's link belongs to a game, and a running game is refused before the
    // pause), and it is what this case pins: the pause keeps the sequence state.
    rig.dev.cfg[0] = 0xFF;  // map_idx 255: the menu, which the job's own first step checks again
    rig.dev.cfg[2] = 0;
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));

    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F4") + rig.dev.count("fifo:F5") >= 2; }));
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    REQUIRE(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 3));
    REQUIRE(rig.cart.connected());
    rig.push({0x80, 0x3C, 0x00});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 2;
    }));
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.applied[0] == Bytes{0x90, 0x3C, 0x7F});
    CHECK(rig.dev.applied[1] == Bytes{0x80, 0x3C, 0x00});
    // The probe's sync and the first frame's: none after the pause, whose frame simply carries seq 1.
    CHECK(rig.dev.syncs == 2);
    CHECK(rig.dev.frameSeqs == std::vector<int>{0, 1});
    CHECK(rig.dev.opens == 1);
    CHECK(rig.host.link().reliable());
}

TEST_CASE("the menu probe waits out an ack the game still owes, so its config read sees only its reply",
          "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };
    // A game runs, the link is up, and the game writes a frame's ack a little after the host writes the frame, so the
    // ack is still in the cart's queue when a submission's config read arrives: the cart's MCU puts what it owes ahead
    // of the reply. Reading across that is what a probe must not do - the ack becomes the read's first bytes, and the
    // reply's own tail is left in the input to be parsed as link traffic.
    //
    // The last three bytes of the config block are an ack that names a frame two ahead, so a tail parsed as link
    // traffic would point the sequence past the frame the game is about to acknowledge.
    const auto cfgTail = [](FakeN8& dev) {
        dev.cfg[13] = 0xF5;
        dev.cfg[14] = 0x7F;
        dev.cfg[15] = 0x00;
    };

    SECTION("the probe waits, so the config read answers the menu") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;  // BlipToaster, which answers the probe
        rig.dev.cfg[0] = 0xFF;   // the config block says the menu, so the job is taken (the fake's combination)
        rig.dev.cfg[2] = 0;
        cfgTail(rig.dev);
        rig.dev.linkAckDelayMs = 60;
        rig.dev.files[kGameBattery] = saveOf(1024, 3);
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));

        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
        Job job;
        const auto t0 = Clock::now();
        REQUIRE(rig.submit(battery("dump"), job));
        CHECK(ms(t0) >= 30);  // it waited for the ack the game still owed, then read the block
        REQUIRE(waitUntil([&] { return job.finished(); }));
        CHECK(job.state.load() == Job::Done);
        CHECK(job.result == saveOf(1024, 3));
        CHECK(rig.host.link().linkOutstanding() == 0);
        std::lock_guard<std::mutex> l(rig.dev.mu);
        // The frame went once. An ack the read had swallowed would leave it outstanding, and a tail parsed as an ack
        // would point the link past it: either way the frame is sent again and the game reads its seq a second time.
        CHECK(rig.dev.frameSeqs == std::vector<int>{0});
        CHECK(rig.dev.syncs == 2);
        CHECK(rig.dev.applied == std::vector<Bytes>{Bytes{0x90, 0x3C, 0x7F}});
    }

    SECTION("the volume's Auto read waits for it too") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;
        rig.dev.cfg[0] = 69;  // Sunsoft 5B: a cart with its own sound chip, so Auto writes unity
        rig.dev.cfg[2] = 0;
        cfgTail(rig.dev);
        rig.dev.linkAckDelayMs = 60;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));

        rig.cart.setExpansionVolume(64);
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.cfg[3] == 64;
        }));
        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
        rig.host.setExpVol(N8Host::EXP_VOL_AUTO);
        // Auto asks the cart which mapper runs, so that read has the probe's exposure: it waits for the ack, then
        // reads the block and writes unity for this mapper.
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.cfg[3] == N8Host::EXP_VOL_UNITY;
        }));
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.frameSeqs == std::vector<int>{0});
        CHECK(rig.dev.syncs == 2);
    }
}

TEST_CASE("a port that throws as a job starts stops the link with the reason, and the job says the link went down",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(1024, 3);
    rig.connect();
    // The run's own pre-flush throws, as a USB unplug during a job does: the job never runs, so nothing addresses the
    // card, and its record must still carry a reason rather than the generic failure a job with no outcome gets.
    rig.dev.throwOnFlushInput = 1;  // connect() flushed once already; the job's own flush is the next
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.error == "the link went down before the job ran");
    // The failure is the link's, and lastError is where Settings > N8 reads it: the thread ended, so a connect can
    // start it afresh.
    CHECK(rig.host.link().lastError() == "serial write failed (N8 unplugged?): fake port: flushInput failed");
    CHECK_FALSE(rig.host.link().isConnected());
    CHECK_FALSE(rig.host.cartJobBusy());
    CHECK(rig.dev.count("file-") == 0);
    Job again;
    std::string why;
    CHECK_FALSE(rig.submit(battery("dump"), again, &why));
    CHECK(why == "not connected");
}

TEST_CASE("a port that throws after a job ran still posts its outcome, and the link stops with the reason",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(1024, 3);
    rig.connect();
    // The flush after the job throws: the transfer itself completed, so its completion is the one that was decided.
    rig.dev.throwOnFlushInput = 2;  // 1: the run's own pre-flush, 2: the flush after it ran
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 3));
    CHECK(rig.host.link().lastError() == "serial write failed (N8 unplugged?): fake port: flushInput failed");
    CHECK_FALSE(rig.host.link().isConnected());
    CHECK(rig.dev.count("file-") > 0);
}

TEST_CASE("an upload whose trailing flush throws still closes the port it booted from", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    rig.dev.throwOnFlushInput = 2;  // 1: the run's own pre-flush, 2: the flush after the boot
    Job job;
    REQUIRE(rig.submit(uploadOf(inesImage(8)), job));
    REQUIRE(waitUntil([&] { return job.finished() && !rig.host.cartJobBusy(); }));
    // The boot was decided before the throw, so its completion stands, and the port an upload frees for the CLI's
    // tools is closed as the boot would have closed it rather than held until a disconnect.
    CHECK(job.state.load() == Job::Done);
    CHECK(rig.host.link().lastError() == "serial write failed (N8 unplugged?): fake port: flushInput failed");
    CHECK_FALSE(rig.host.link().isConnected());
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.openNow == 0);
    CHECK_FALSE(rig.dev.menu);
    CHECK(rig.dev.booted == "usb-games/game.nes");
}

TEST_CASE("a job its node abandons fails at once, stops at its next chunk, and the cart resumes streaming",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.fileDelayMs = 20;
    rig.dev.files[kGameBattery] = saveOf(4096, 9);
    rig.connect();
    Job job;
    REQUIRE(rig.submit(uploadOf(inesImage(64)), job));  // eight 8 KB chunks
    REQUIRE(waitUntil([&] { return rig.dev.count("file-wr") >= 2; }));
    rig.cart.abandonJobs(&rig.owner);
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.failure.load() == Job::Removed);
    const std::uint32_t progressAtAbandon = job.progress.load();

    REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
    CHECK(rig.cart.connected());                      // nothing booted: streaming resumed to the menu, as it was
    CHECK(rig.dev.count("file-wr") < 8);              // it stopped early
    CHECK(job.progress.load() == progressAtAbandon);  // and reported nothing more into the record
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.menu);
        CHECK(rig.dev.booted.empty());
        CHECK(rig.dev.files.count("usb-games/game.nes") == 0);  // the partial image went with the job
    }

    // A dump abandoned between its blocks stops there, having changed nothing.
    rig.dev.fileReadDelayMs = 20;
    Job dump;
    REQUIRE(rig.submit(battery("dump"), dump));
    REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") >= 2; }));
    rig.cart.abandonJobs(&rig.owner);
    REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
    CHECK(dump.failure.load() == Job::Removed);
    CHECK(rig.dev.count("file-rd") < 8);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.files[kGameBattery] == saveOf(4096, 9));
    }

    // Another node's abandon is not this job's: a second job runs to its end.
    Job next;
    REQUIRE(rig.submit(battery("dump"), next));
    int other = 0;
    rig.cart.abandonJobs(&other);
    REQUIRE(waitUntil([&] { return next.finished(); }));
    CHECK(next.state.load() == Job::Done);
}

TEST_CASE("an upload abandoned after its last chunk still boots nothing, and its file goes", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.fileDelayMs = 150;
    rig.connect();
    Job job;
    // One chunk: the abandon lands during its write, after the loop's last check.
    REQUIRE(rig.submit(uploadOf(inesImage(4)), job));
    REQUIRE(waitUntil([&] { return rig.dev.count("file-wr") >= 1; }));
    rig.cart.abandonJobs(&rig.owner);
    REQUIRE(waitUntil([&] { return !rig.host.cartJobBusy(); }));
    CHECK(job.failure.load() == Job::Removed);
    CHECK(rig.cart.connected());
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.menu);
    CHECK(rig.dev.booted.empty());
    CHECK(rig.dev.files.count("usb-games/game.nes") == 0);
}

TEST_CASE("a disconnect during a job ends it with the reason, and a job taken just before one still completes",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.fileDelayMs = 20;
    rig.connect();
    Job job;
    REQUIRE(rig.submit(uploadOf(inesImage(64)), job));
    REQUIRE(waitUntil([&] { return rig.dev.count("file-wr") >= 1; }));
    rig.host.link().disconnect();  // joins: the job stops at its next chunk
    REQUIRE(job.finished());
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.error == "the link was disconnected during the job");
    CHECK_FALSE(rig.host.cartJobBusy());

    // Taken and then the link torn down before the serial thread could run it: it fails, never hangs.
    rig.connect();
    Job raced;
    REQUIRE(rig.submit(battery("dump"), raced));
    rig.host.link().disconnect();
    REQUIRE(raced.finished());
    CHECK_FALSE(rig.host.cartJobBusy());
}

TEST_CASE("a connect forgets what this host booted once the cart runs something else, and the battery jobs default "
          "to the ROM it last booted", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    Job up;
    REQUIRE(rig.submit(uploadOf(inesImage(8)), up));
    REQUIRE(waitUntil([&] { return up.finished(); }));
    REQUIRE(up.state.load() == Job::Done);
    Job before;
    REQUIRE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "title"), before));

    // A power-cycle: the cart is back at its menu, whose map_idx the next connect reads; the OS wrote the game's
    // battery back to its file on the way.
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;
        rig.dev.files[kGameBattery] = saveOf(1024, 11);
    }
    rig.host.connect(true);
    REQUIRE(rig.cart.connected());
    Job after;
    std::string why;
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_ROM_INTERFACE, "header"), after, &why));
    CHECK(why == "nothing this host booted is running");

    // The battery file of what it booted, with no name given.
    Job dump;
    REQUIRE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "dump"), dump));
    REQUIRE(waitUntil([&] { return dump.finished(); }));
    CHECK(dump.state.load() == Job::Done);
    CHECK(dump.result == saveOf(1024, 11));
}

TEST_CASE("the pause waits a bounded time for acks a silent cart never sends, and a disconnect inside that wait "
          "returns promptly", "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    const auto ms = [](Clock::time_point t) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
    };
    SECTION("the bound") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;
        // The config block says the menu shows, so the job is taken and the pause's ack wait runs (see the sequence
        // case for why that combination is the fake's, not a real cart's).
        rig.dev.cfg[0] = 0xFF;
        rig.dev.cfg[2] = 0;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.linkDeaf = true;
        }
        // A frame the cart never acknowledges, outstanding before the submission: the probe's read waits only until
        // the cart has been silent for the drain's bound, since a cart that silent is not acknowledging anything.
        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
        Job job;
        const auto t0 = Clock::now();
        REQUIRE(rig.submit(battery("dump"), job));
        REQUIRE(waitUntil([&] { return job.finished(); }));
        const auto took = ms(t0);
        CHECK(took >= 250);  // it waited for the ack...
        CHECK(took < 2000);  // ...a bounded time, then ran the job (which found no battery file)
        CHECK(job.error == "no battery file for game.nes (EDN8/gamedata/game.nes/bram.srm)");
    }
    SECTION("a disconnect inside the wait") {
        Rig rig;
        rig.dev.menu = false;
        rig.dev.linkRom = true;
        rig.dev.cfg[0] = 0xFF;
        rig.dev.cfg[2] = 0;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.linkDeaf = true;
        }
        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
        Job job;
        REQUIRE(rig.submit(battery("dump"), job));
        std::this_thread::sleep_for(std::chrono::milliseconds(60));  // inside the 250 ms the drain waits for acks
        const auto t0 = Clock::now();
        rig.host.link().disconnect();
        CHECK(ms(t0) < 200);
        REQUIRE(job.finished());
        CHECK(job.error == "the link went down before the job ran");
        CHECK_FALSE(rig.host.cartJobBusy());
    }
}

TEST_CASE("a cart reset to its menu with a frame its game never acknowledged still takes a job", "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    Rig rig;
    rig.dev.menu = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0] = 69;
    rig.dev.cfg[2] = 0;
    rig.dev.files[kGameBattery] = saveOf(1024, 5);
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 1;  // the game read it and has acknowledged it
    }));

    // The console's reset button: the N8 goes back to its menu with USB still up, so the link and its sequence state
    // stay, and the menu acknowledges nothing (it skips every byte that is not one of its commands). The next frame is
    // outstanding for good, re-sent whenever the cart says nothing for the link's timeout.
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;
    }
    rig.push({0x80, 0x3C, 0x00});
    REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
    // The probe reads once the cart has been silent for the drain's bound since that frame went out, the menu answers,
    // and the job runs after the pause's own bounded wait for the same ack.
    Job job;
    std::string why;
    const auto t0 = Clock::now();
    REQUIRE(rig.submit(battery("dump"), job, &why));
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 1000);
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 5));
    REQUIRE(waitUntil([&] { return rig.cart.connected(); }));  // streaming resumed to the menu
    CHECK(rig.host.link().linkOutstanding() == 1);              // still owed: the menu will never acknowledge it
}

TEST_CASE("a probe on a link that never goes quiet is refused for that, not as a config block that did not answer",
          "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    Rig rig;
    rig.dev.menu = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0] = 69;
    rig.dev.cfg[2] = 0;
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.linkDamaged = true;
    }
    // Every copy of the frame reaches the game damaged and is reported at once, so it is re-sent at once: the cart
    // never stops acknowledging and the frame never stops being outstanding, so no turn is quiet for a read.
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] { return rig.host.link().linkResends() >= 3; }));
    Job job;
    std::string why;
    const auto t0 = Clock::now();
    CHECK_FALSE(rig.submit(battery("dump"), job, &why));
    CHECK(why == "the reliable link never went quiet for the cart's config read: the cart kept acknowledging the "
                 "stream");
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() >= 900);  // its deadline
    CHECK(rig.cart.connected());  // nothing paused
    CHECK(rig.dev.count("file-") == 0);
}

TEST_CASE("a submission's wake outlasts a turn the link is busy on, so a failing config write's wait cannot refuse "
          "it", "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    Rig rig;
    rig.dev.menu = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0] = 69;  // a game: the probe's answer refuses the job, quickly when it is read in time
    rig.dev.cfg[2] = 0;
    rig.dev.linkAckDelayMs = 60;
    rig.dev.refuseExpVolWrites = 1'000'000;  // a port that is open but refuses every config write
    rig.cart.setExpansionVolume(64);
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    // The connect-time write is refused, and so is the serial thread's first, which arms the second-long retry wait.
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.expVolAttempts >= 2;
    }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] { return rig.host.link().linkOutstanding() == 1; }));
    // The frame's ack is 60 ms out, so the turn the submission wakes is a busy one and the probe waits. The wake must
    // still stand when the link goes quiet: spent on the busy turn, whose write re-arms the wait, it would leave the
    // probe a second behind it and refused at its deadline ("the cart's config block did not answer").
    Job job;
    std::string why;
    const auto t0 = Clock::now();
    CHECK_FALSE(rig.submit(battery("dump"), job, &why));
    CHECK(why == "the battery file can only be reached at the N8 menu: power-cycle to the menu first");
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 500);
    CHECK(rig.cart.connected());
}

TEST_CASE("a paused job whose run throws is run once, its completion is still posted, and the link stops with the "
          "reason", "[n8][cart-jobs]") {
    FakeN8 dev;
    N8Link link(factoryFor(dev));
    link.setLinkAllowed(false);
    REQUIRE(link.connect("/dev/fake"));
    std::atomic<int> runs{0}, unrun{0}, dones{0};
    N8Link::PausedJob job;
    job.run = [&](Edio* edio) -> N8Link::AfterJob {
        if (!edio) {
            ++unrun;  // what a job that never ran is told
            return N8Link::AfterJob::Resume;
        }
        ++runs;
        throw std::runtime_error("the job's own throw");
    };
    job.done = [&] { ++dones; };
    REQUIRE(link.runPaused(std::move(job)));
    REQUIRE(waitUntil([&] { return dones.load() == 1; }));
    // It ran: being told afterwards that the link went down before it could would overwrite what it recorded.
    CHECK(runs.load() == 1);
    CHECK(unrun.load() == 0);
    CHECK_FALSE(link.jobPending());
    CHECK_FALSE(link.isConnected());
    CHECK(link.lastError() == "serial write failed (N8 unplugged?): the job's own throw");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));  // the thread is gone: nothing runs it again
    CHECK(runs.load() == 1);
    CHECK(unrun.load() == 0);
    CHECK(dones.load() == 1);
}

TEST_CASE("a restore whose write fails says the battery file is replaced and incomplete, and closes it; a failed read "
          "closes too", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(2048, 3);
    rig.connect();
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.failNextWrite = true;
    }
    auto r = battery("restore");
    r.bytes = saveOf(4096, 5);
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished() && !rig.host.cartJobBusy(); }));
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.error.rfind("the battery file EDN8/gamedata/game.nes/bram.srm was replaced and is incomplete: restore "
                          "again (", 0) == 0);
    CHECK(rig.cart.connected());
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.filesOpen == 0);                 // closed on the failure path
        CHECK(rig.dev.files[kGameBattery].empty());    // the open emptied the old save: neither save is there
        rig.dev.files[kGameBattery] = saveOf(2048, 3);
        rig.dev.failNextRead = true;
    }
    Job dump;
    REQUIRE(rig.submit(battery("dump"), dump));
    REQUIRE(waitUntil([&] { return dump.finished() && !rig.host.cartJobBusy(); }));
    CHECK(dump.state.load() == Job::Failed);
    CHECK(dump.error == "Edio: file read error 0x01");
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.filesOpen == 0);
}

TEST_CASE("a restore replaces a longer file whole and says whether it made the ROM's folder", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files[kGameBattery] = saveOf(8192, 3);
    rig.connect();
    auto r = battery("restore");
    r.bytes = saveOf(1000, 5);
    Job over;
    REQUIRE(rig.submit(r, over));
    REQUIRE(waitUntil([&] { return over.finished() && !rig.host.cartJobBusy(); }));
    REQUIRE(over.state.load() == Job::Done);
    CHECK(std::string(over.result.begin(), over.result.end()) ==
          R"({"path":"EDN8/gamedata/game.nes/bram.srm","created":false})");
    auto fresh = battery("restore", "fresh.nes");
    fresh.bytes = saveOf(16, 1);
    Job made;
    REQUIRE(rig.submit(fresh, made));
    REQUIRE(waitUntil([&] { return made.finished() && !rig.host.cartJobBusy(); }));
    CHECK(std::string(made.result.begin(), made.result.end()) ==
          R"({"path":"EDN8/gamedata/fresh.nes/bram.srm","created":true})");
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.files[kGameBattery] == saveOf(1000, 5));  // truncated by the open, not overlaid
    CHECK(rig.dev.filesOpen == 0);
}

TEST_CASE("a result's paths are JSON strings, so a name the card accepts cannot break the result", "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    // plainFileName allows a quote (it refuses a separator and a control byte), so the result must escape it: TS parses
    // these results, and an unescaped quote would end the string early.
    auto r = battery("restore", "quo\"te.nes");
    r.bytes = saveOf(16, 1);
    Job job;
    REQUIRE(rig.submit(r, job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    REQUIRE(job.state.load() == Job::Done);
    CHECK(std::string(job.result.begin(), job.result.end()) ==
          R"({"path":"EDN8/gamedata/quo\"te.nes/bram.srm","created":true})");

    auto up = request(RP_SYSTEM_ROM_INTERFACE, "upload");
    up.bytes = inesImage(8);
    up.name  = "quo\"te.nes";
    Job uploaded;
    REQUIRE(rig.submit(up, uploaded));
    REQUIRE(waitUntil([&] { return uploaded.finished(); }));
    REQUIRE(uploaded.state.load() == Job::Done);
    CHECK(std::string(uploaded.result.begin(), uploaded.result.end()) ==
          R"({"path":"usb-games/quo\"te.nes","mapIndex":69,"streaming":"stopped"})");
}

TEST_CASE("a reconnect re-applies the standing expansion volume, which is the path a cart node relies on",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.connect();
    rig.cart.setExpansionVolume(64);
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.cfg[3] == 64;
    }));
    // A power-cycle clears the register (master_vol is 0 on a fresh cart). A cart node's own re-send on the connected
    // edge asks for a value N8Host already holds, which setExpVol does not rewrite; the connect path is what writes it
    // again, and this pins that it does.
    rig.host.connect(false);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.cfg[3] = 0;
    }
    rig.host.connect(true);
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.cfg[3] == 64;
    }));
}

TEST_CASE("a dump keeps the restore's bound and tells a missing folder from a card that will not list",
          "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files["EDN8/gamedata/empty.nes/bram.srm"] = {};
    rig.dev.files["EDN8/gamedata/huge.nes/bram.srm"] = Bytes((1u << 20) + 1, 0);
    rig.connect();
    const auto run = [&](JobRequest req) {
        Job j;
        REQUIRE(rig.submit(req, j));
        REQUIRE(waitUntil([&] { return j.finished() && !rig.host.cartJobBusy(); }));
        CHECK(j.state.load() == Job::Failed);
        return j.error;
    };
    CHECK(run(battery("dump", "empty.nes")) ==
          "the battery file EDN8/gamedata/empty.nes/bram.srm holds 0 bytes: a battery file is 1 to 1048576");
    CHECK(run(battery("dump", "huge.nes")) ==
          "the battery file EDN8/gamedata/huge.nes/bram.srm holds 1048577 bytes: a battery file is 1 to 1048576");
    CHECK(rig.dev.count("file-rd") == 0);
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.failNextList = true;
    }
    CHECK(run(battery("dump", "empty.nes")) ==
          "the battery folder EDN8/gamedata/empty.nes could not be listed: Edio: operation error 0x01");
}

TEST_CASE("a battery job's name wins over the ROM this host booted, names are plain, and a port change forgets the "
          "booted ROM", "[n8][cart-jobs]") {
    Rig rig;
    rig.dev.files["EDN8/gamedata/other.nes/bram.srm"] = saveOf(700, 9);
    rig.connect();
    Job up;
    REQUIRE(rig.submit(uploadOf(inesImage(8)), up));  // boots game.nes
    REQUIRE(waitUntil([&] { return up.finished(); }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;  // power-cycled to the menu
    }
    rig.host.connect(true);
    Job named;
    REQUIRE(rig.submit(battery("dump", "other.nes"), named));
    REQUIRE(waitUntil([&] { return named.finished() && !rig.host.cartJobBusy(); }));
    CHECK(named.result == saveOf(700, 9));

    // A name holding a NUL never reaches the cart: a request crosses as C strings, and the engine refuses one before it
    // does (rp.n8.nes's jobs case, N8Node.test.cpp).
    for (const std::string bad : {std::string("."), std::string(".."), std::string("a\\b"), std::string(256, 'x')}) {
        Job none;
        std::string why;
        auto r = request(RP_SYSTEM_SRAM_INTERFACE, "dump");
        r.name = bad;
        CHECK_FALSE(rig.submit(r, none, &why));
        CHECK(why == "a battery job names the ROM's file, a plain file name");
    }

    rig.host.connect(false);
    rig.host.setPort("/dev/another-cart");
    Job none;
    std::string why;
    CHECK_FALSE(rig.submit(request(RP_SYSTEM_SRAM_INTERFACE, "dump"), none, &why));
    CHECK(why == "a battery job needs the ROM's file name: name it, or boot the ROM through rom.upload first");
}

namespace {

// The notes a cart node hands its link when it leaves the plan: 60 on channel 1 and 67 on channel 2.
rp::n8::CartNotes heldNotes() {
    rp::n8::CartNotes notes;
    notes.set(60);
    notes.set(128 + 67);
    return notes;
}

// Where `entry` first appears in the cart's log, or the log's length when it never does.
std::ptrdiff_t logIndex(FakeN8& dev, const std::string& entry) {
    std::lock_guard<std::mutex> l(dev.mu);
    return std::find(dev.log.begin(), dev.log.end(), entry) - dev.log.begin();
}

}  // namespace

TEST_CASE("the notes a leaving cart node held get their note-offs from the serial thread, behind what it pushed",
          "[n8][cart-release]") {
    SECTION("raw: after a note-on still waiting for its release time") {
        Rig rig;
        rig.connect();
        // The note-on is due 200 ms out (a lookahead and a late frame), so it is still in the ring when the node's
        // destroy hands over its books: its note-off must not overtake it.
        const Bytes on{0x90, 0x3C, 0x7F};
        rig.cart.push(9600, on.data(), on.size(), false, 48000.0);
        rig.cart.releaseNotes(heldNotes());
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));
        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
        CHECK(logIndex(rig.dev, "fifo:90 3C 7F") < logIndex(rig.dev, "fifo:80 3C 00"));
        CHECK(rig.cart.connected());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:80 3C 00") == 1);  // once
    }
    SECTION("framed: the reliable link delivers them in order, as any message") {
        Rig rig;
        rig.dev.menu    = false;
        rig.dev.linkRom = true;  // BlipToaster, which answers the probe
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        rig.push({0x90, 0x3C, 0x7F});
        rig.cart.releaseNotes(heldNotes());
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 3;
        }));
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.applied[0] == Bytes{0x90, 0x3C, 0x7F});
        CHECK(rig.dev.applied[1] == Bytes{0x80, 0x3C, 0x00});
        CHECK(rig.dev.applied[2] == Bytes{0x81, 0x43, 0x00});
    }
    SECTION("a job's pause: none while it holds the port, all on the first streaming turn after it") {
        Rig rig;
        rig.dev.files[kGameBattery] = saveOf(3000, 7);
        rig.dev.fileReadDelayMs     = 20;
        rig.connect();
        Job job;
        REQUIRE(rig.submit(battery("dump"), job));
        REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
        rig.cart.releaseNotes(heldNotes());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:8") == 0);
        REQUIRE(waitUntil([&] { return job.finished(); }));
        CHECK(job.state.load() == Job::Done);
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));
        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
        CHECK(rig.dev.count("file-rd") == 6);
        std::lock_guard<std::mutex> l(rig.dev.mu);
        const auto lastRead = std::find(rig.dev.log.rbegin(), rig.dev.log.rend(), "file-rd");
        const auto release  = std::find(rig.dev.log.rbegin(), rig.dev.log.rend(), "fifo:80 3C 00");
        CHECK(release < lastRead);  // reversed: the note-off is the later entry
    }
    SECTION("a disconnect writes a release still waiting before the port closes; a link that is down drops one") {
        Rig rig;
        rig.connect();
        // A note-on due a second out never goes (the disconnect throws the ring away), and the release does not wait
        // for it: a note-off for a note that never sounded is harmless, a held one left behind is not.
        const Bytes on{0x90, 0x3C, 0x7F};
        rig.cart.push(48000, on.data(), on.size(), false, 48000.0);
        rig.cart.releaseNotes(heldNotes());
        rig.host.connect(false);
        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
        CHECK(rig.dev.count("fifo:90") == 0);

        // Down: nothing runs to write it, and the next connect does not either (the console it reaches may have been
        // power-cycled since, and a cart node that still drives it keeps its own books).
        rig.cart.releaseNotes(heldNotes());
        rig.connect();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
    }
}

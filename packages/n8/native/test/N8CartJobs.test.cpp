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
#include "n8/Edio.hpp"
#include "n8/desktop/PortTransport.hpp"
#include "n8/desktop/N8CartLink.hpp"
#include "n8/desktop/N8Host.hpp"
#include "FakeN8.hpp"

using rp::n8::Edio;
using retroplug::PortTransport;
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

// A port whose cart answers a memory read late: the read that follows the config block's command times out with the
// reply already in the input, as a seed menu read that ran out its short timeout does - the block is then still there
// for whatever reads the port next, which at connect is the link's own probe. One shot: the guard's later reads, which
// flush first, answer normally.
class LateCfgPort : public retroplug::test::FakeN8Port {
public:
    LateCfgPort(FakeN8& dev, std::atomic<int>& stalled) : FakeN8Port(dev), stalled_(stalled) {}
    std::size_t write(const std::uint8_t* data, std::size_t n) override {
        const std::size_t took = FakeN8Port::write(data, n);
        // A 13-byte CMD_MEM_RD of the FPGA config block: the menu check's own read.
        if (!armed_ && n == 13 && data[2] == Edio::CMD_MEM_RD) {
            const std::uint32_t addr = static_cast<std::uint32_t>(data[4]) |
                                       (static_cast<std::uint32_t>(data[5]) << 8) |
                                       (static_cast<std::uint32_t>(data[6]) << 16) |
                                       (static_cast<std::uint32_t>(data[7]) << 24);
            armed_ = addr == static_cast<std::uint32_t>(Edio::ADDR_CFG);
        }
        return took;
    }
    std::size_t read(std::uint8_t* b, std::size_t n, std::uint32_t timeoutMs) override {
        if (armed_) {
            armed_ = false;
            ++stalled_;
            return 0;  // the reply has not arrived yet: the read gives up on it
        }
        return FakeN8Port::read(b, n, timeoutMs);
    }

private:
    std::atomic<int>& stalled_;
    bool              armed_ = false;
};

// A port that hands back one byte a read, as USB may split what the cart wrote: an ack then arrives a byte at a time.
class ByteAtATimePort : public retroplug::test::FakeN8Port {
public:
    using FakeN8Port::FakeN8Port;
    std::size_t read(std::uint8_t* b, std::size_t n, std::uint32_t timeoutMs) override {
        return FakeN8Port::read(b, std::min<std::size_t>(n, 1), timeoutMs);
    }
};

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
    // Connect on a cart running a game (the fake's config block then names mapper 0): the menu reads the FIFO as its
    // own commands, so a streamed message goes nowhere there and a case that pushes MIDI while it streams starts here.
    void connectGame() {
        {
            std::lock_guard<std::mutex> l(dev.mu);
            dev.menu = false;
        }
        connect();
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

TEST_CASE("a dump job pauses streaming on the port it has, reads the battery file with progress, and resumes before "
          "it completes", "[n8][cart-jobs]") {
    Rig rig;
    const Bytes save = saveOf(3000, 7);
    rig.dev.files[kGameBattery] = save;
    rig.dev.fileReadDelayMs = 20;
    rig.connectGame();

    // A note pushed 20 ms ahead and then a job at once: the note goes out at its own time, before the first read.
    rig.host.link().setLookaheadMs(20);
    rig.push({0x90, 0x3C, 0x7F});
    rig.push({0x80, 0x3C, 0x00});
    rig.host.link().setLookaheadMs(0);
    // Both notes are out before the console goes to its menu, which is the only place the SD card is reachable: the
    // guard reads that state between chunks, and from then on the stream is dropped rather than read as commands.
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;
    }
    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
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
    // Completed after streaming resumed, on the one port the link opened. The cart reads disconnected at its menu: what
    // a cart node shows there, since the FIFO is a command parser, not a MIDI stream.
    CHECK(rig.host.link().isConnected());
    CHECK_FALSE(rig.cart.connected());
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
    CHECK(rig.dev.count("fifo:90 40") == 0);  // the push the pause dropped (or the menu refused) never reached the cart
    CHECK(rig.cart.dropped() == 1);            // ...counted, and nothing else was lost

    // And a message pushed now goes nowhere: the guard has the menu, so the push is dropped and counted.
    rig.push({0x91, 0x41, 0x7F});
    CHECK(rig.cart.dropped() == 2);  // the pause's push, and this one
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(rig.dev.count("fifo:91 41 7F") == 0);
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
    CHECK(rig.host.link().isConnected());
    CHECK_FALSE(rig.cart.connected());  // the menu: attached, streaming nothing

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
    CHECK(rig.host.link().isConnected());
    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
    CHECK_FALSE(rig.cart.connected());  // the menu, once the guard has read it
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
    // This combination - a game's link with the config block saying the menu - is the fake's, and one the guard
    // refuses: it seeds the state at connect. The case is the pause's sequence state, so the guard is off.
    rig.host.link().setMenuCheck({});
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

TEST_CASE("the menu guard reads the cart's state between chunks, so a streamed frame never reaches a menu, and a "
          "read waits out an ack the game still owes", "[n8][cart-jobs]") {
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
        // The fake's inconsistent combination - a game's link with the config block saying the menu - is one the guard
        // refuses, and it seeds that state at connect: off, for this section, which is the probe's own wait.
        rig.host.link().setMenuCheck({});
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

    SECTION("a frame pushed at the menu goes nowhere, and the job's probe still reads it") {
        Rig rig;
        rig.dev.linkRom = true;  // a game's link, stale after the console went back to its menu
        cfgTail(rig.dev);
        rig.dev.files[kGameBattery] = saveOf(1024, 3);
        rig.connect();  // at the menu: the guard's seed hears its answer before anything may stream
        // A message pushed now would be read as one of the menu's commands, so it is dropped rather than framed, and
        // nothing is left outstanding for a re-send to carry into the menu.
        rig.push({0x90, 0x3C, 0x7F});
        CHECK(rig.host.link().linkOutstanding() == 0);
        Job job;
        const auto t0 = Clock::now();
        REQUIRE(rig.submit(battery("dump"), job));
        CHECK(ms(t0) < 1000);  // nothing outstanding to wait out: the menu owes no ack
        REQUIRE(waitUntil([&] { return job.finished(); }));
        CHECK(job.state.load() == Job::Done);
        CHECK(job.result == saveOf(1024, 3));
        CHECK(rig.host.link().linkOutstanding() == 0);
        std::lock_guard<std::mutex> l(rig.dev.mu);
        CHECK(rig.dev.frameSeqs.empty());  // no frame was ever written, so none was re-sent
        CHECK(rig.dev.syncs == 0);
        CHECK(rig.dev.applied.empty());
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
    // card, and its record must still carry a reason rather than the generic failure a job with no outcome gets. Every
    // flush fails from here, not the next one only: the guard's cadence reads flush the input too, and on a loaded
    // runner one of them lands between this and the job's own pre-flush and takes a one-shot failure with it (the job
    // then runs and this case's whole tail reads a link that worked).
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);  // the link's thread reads it under the lock
        rig.dev.failEveryFlush = true;
    }
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.error == "the link went down before the job ran");
    // The failure is the link's, and lastError is where Settings > N8 reads it: the thread ended, so a connect can
    // start it afresh.
    CHECK(rig.host.link().lastError() == "serial write failed (N8 unplugged?): fake port: flushInput failed");
    CHECK_FALSE(rig.host.link().isConnected());
    // A stream that stopped itself is not left saying "at the menu" (it was seeded there at connect, being the fake's
    // default): what a status read answers until the next disconnect is where the cart runs now, which is unknown.
    CHECK_FALSE(rig.host.link().atMenu());
    CHECK_FALSE(rig.host.getConfig().atMenu);
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
    rig.dev.fileReadDelayMs     = 20;  // long enough to arm the failure while the job is inside its run
    rig.connect();
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
    // The flush after the job throws: the transfer itself completed, so its completion is the one that was decided. No
    // other flush can take it: the job owns the port while it runs, so the stream's own reads (and their flushes) wait.
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.failOnFlushInput = 1;
    }
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
    // The flush after the boot throws: the boot itself is decided, so its completion is the one that was decided (the
    // port stays closed after it, so every later flush would fail too).
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);  // the link's thread reads it under the lock
        rig.dev.failFlushAtGame = true;
    }
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

TEST_CASE("a job its node abandons fails at once, stops at its next chunk, and the link resumes, at the menu",
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
    CHECK(rig.host.link().isConnected());             // nothing booted: the link is up, at the menu it resumed to
    CHECK_FALSE(rig.cart.connected());                // ...which a cart node reads as disconnected
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
    CHECK(rig.host.link().isConnected());
    CHECK_FALSE(rig.cart.connected());  // the menu the abandoned upload left the cart at
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
    REQUIRE(rig.host.link().isConnected());
    REQUIRE_FALSE(rig.cart.connected());  // the menu: connected reads 0 there
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
        // The fake's combination is one the guard refuses, and it seeds its state at connect: off, for this case.
        rig.host.link().setMenuCheck({});
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
        // The fake's combination is one the guard refuses, and it seeds its state at connect: off, for this case.
        rig.host.link().setMenuCheck({});
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

    // The console's reset button: the N8 goes back to its menu with USB still up, and the menu acknowledges nothing (it
    // skips every byte that is not one of its commands). The frame below stops the window, the guard asks, and the
    // menu's answer resets the stale window there rather than re-sending it into a command parser.
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;
    }
    rig.push({0x80, 0x3C, 0x00});
    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
    CHECK(rig.host.link().linkOutstanding() == 0);  // the stale frame went with the window
    CHECK(rig.host.link().linkResends() == 0);      // held, not re-sent into the menu
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        // The fake's menu branch returns before it reads a frame, so this says only that no frame reached a GAME after
        // the reset; the linkResends() line above is what says none was written into the menu.
        CHECK(rig.dev.frameSeqs == std::vector<int>{0});
    }
    // The probe reads the menu and the job runs; the pause's own drain finds nothing to deliver, which is the point.
    Job job;
    std::string why;
    const auto t0 = Clock::now();
    REQUIRE(rig.submit(battery("dump"), job, &why));
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 1000);
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 5));
    CHECK(rig.host.link().isConnected());
    CHECK_FALSE(rig.cart.connected());  // the menu, where streaming stays off
}

TEST_CASE("a probe on a link that never goes quiet is refused for that, not as a config block that did not answer",
          "[n8][cart-jobs]") {
    using Clock = std::chrono::steady_clock;
    Rig rig;
    rig.dev.menu = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0] = 69;
    rig.dev.cfg[2] = 0;
    rig.dev.linkAckDelayMs = 30;  // a game that answers, but not instantly
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    // A stream the game keeps acknowledging: every turn has an ack in the last ack bound, so no turn is quiet for a
    // config read, and the cart is answering as a game (its window moves), so the guard has nothing to look for either.
    std::atomic<bool> streaming{true};
    std::thread feeder([&] {
        while (streaming.load(std::memory_order_relaxed)) {
            rig.push({0x90, 0x3C, 0x7F});
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // acks in flight before the probe asks
    Job job;
    std::string why;
    const auto t0 = Clock::now();
    CHECK_FALSE(rig.submit(battery("dump"), job, &why));
    streaming.store(false, std::memory_order_relaxed);
    feeder.join();
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
    CHECK(rig.host.link().isConnected());
    CHECK_FALSE(rig.cart.connected());  // the menu
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

// Where `entry` last appears in the cart's log, or -1 when it never does.
std::ptrdiff_t lastLogIndex(FakeN8& dev, const std::string& entry) {
    std::lock_guard<std::mutex> l(dev.mu);
    const auto it = std::find(dev.log.rbegin(), dev.log.rend(), entry);
    return it == dev.log.rend() ? -1 : static_cast<std::ptrdiff_t>(dev.log.rend() - it - 1);
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
        rig.connectGame();
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
    SECTION("a job's pause: nothing is written while the cart is at its menu") {
        Rig rig;
        rig.dev.files[kGameBattery] = saveOf(3000, 7);
        rig.dev.fileReadDelayMs     = 20;
        rig.connect();  // the menu, where a battery job runs
        Job job;
        REQUIRE(rig.submit(battery("dump"), job));
        REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
        rig.cart.releaseNotes(heldNotes());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:8") == 0);
        REQUIRE(waitUntil([&] { return job.finished(); }));
        CHECK(job.state.load() == Job::Done);
        CHECK(rig.dev.count("file-rd") == 6);
        // The release was refused: the guard has the menu, and releaseNotes stops at its first line there (a menu
        // holds no notes of ours, and its FIFO is a command parser). Nothing of it reached the cart, before or after
        // the job.
        CHECK(rig.dev.count("fifo:8") == 0);
    }
    SECTION("a job's pause with the guard off: nothing while it holds the port, all on the first turn after it") {
        // The same episode with no menu guard (a fake device's, or a case that turned it off): the cart reads connected
        // at its menu, so the release queues, and the job's pause is the only thing that may use the port until it is
        // done. This is the coverage the guard took away, since with it on every job runs at the menu.
        Rig rig;
        rig.dev.files[kGameBattery] = saveOf(3000, 7);
        rig.dev.fileReadDelayMs     = 20;
        rig.host.link().setMenuCheck({});
        rig.connect();
        Job job;
        REQUIRE(rig.submit(battery("dump"), job));
        REQUIRE(waitUntil([&] { return rig.dev.count("file-rd") > 0; }));
        rig.cart.releaseNotes(heldNotes());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:8") == 0);  // nothing inside the pause: only the job's own traffic uses the port
        REQUIRE(waitUntil([&] { return job.finished(); }));
        CHECK(job.state.load() == Job::Done);
        CHECK(rig.dev.count("file-rd") == 6);
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));  // the first streaming turn
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
    }
    SECTION("a disconnect writes a release still waiting before the port closes; a link that is down drops one") {
        Rig rig;
        rig.connectGame();
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
        rig.connectGame();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
    }
}

TEST_CASE("a handover releases only the notes the leaving node left, not a key the new one struck",
          "[n8][cart-release]") {
    // A cart node's destroy runs on the control thread after the plan swap, so the node taking over has already pushed.
    // Its release therefore waits behind the LEAVING node's own last push (CartLink::pushMark) instead of behind
    // everything pushed so far, and a key the new node struck since is skipped: a per-key holder count would be wrong
    // (a node forwards the DAW's duplicate note-ons, so a count would rise and never come down), which is why the link
    // keeps the index of the chunk whose note-on it last wrote for each key.
    SECTION("raw: the new node struck one of the keys the old one held") {
        Rig rig;
        rig.connectGame();
        rig.push({0x90, 0x3C, 0x7F});  // the leaving node, key 60
        rig.push({0x90, 0x43, 0x7F});  // ... and key 67
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 43 7F") == 1; }));
        const std::uint64_t mark = rig.cart.pushMark();
        rig.push({0x90, 0x3C, 0x7F});  // the node taking over, on a key the old one held
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 2; }));
        rig.cart.releaseNotes(heldNotes(), mark);
        // Key 67 is the leaving node's alone, so its note-off goes; key 60 is the new node's, so none is written.
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:80 3C 00") == 0);
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
        CHECK(rig.cart.connected());
    }
    SECTION("raw: the new node's note-on is still waiting for its release time when the release runs") {
        Rig rig;
        rig.connectGame();
        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
        const std::uint64_t mark = rig.cart.pushMark();
        // 200 ms out at 48 kHz: the node taking over's note-on is still in the ring, so the release goes out ahead of
        // it, and the wire reads on, off, on - the key sounds, as the new node meant it to.
        const Bytes on{0x90, 0x3C, 0x7F};
        rig.cart.push(9600, on.data(), on.size(), false, 48000.0);
        rig.cart.releaseNotes(heldNotes(), mark);
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 2; }));
        CHECK(rig.dev.count("fifo:80 3C 00") == 1);
        CHECK(lastLogIndex(rig.dev, "fifo:80 3C 00") < lastLogIndex(rig.dev, "fifo:90 3C 7F"));
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
    }
    SECTION("raw: a release waits for a SysEx the bytes port split across chunks") {
        Rig rig;
        rig.connectGame();
        // A node's bytes port can hand over half a SysEx: the cart's parser is inside a message until the F7, so a
        // note-off written between the halves is payload, not a message, and the key it meant to release stays down.
        rig.push({0xF0, 0x7E, 0x7F});  // the first half: F0 and two of its data bytes
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 7E 7F") == 1; }));
        rig.cart.releaseNotes(heldNotes());
        // The tail at once, so the two halves are well inside the link's mid-message bound whatever the runner: a sleep
        // here could outlast it on a stalled one, and the gate would let the release out first (that is what the bound
        // is for, so it is the case's own timing, not the link's, that this keeps out of the way).
        rig.push({0x09, 0x01, 0xF7});  // and its tail, which completes it
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
        CHECK(lastLogIndex(rig.dev, "fifo:09 01 F7") < lastLogIndex(rig.dev, "fifo:80 3C 00"));
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
    }
    SECTION("raw: a status byte ends a SysEx that never got its F7") {
        // A SysEx ends at any non-real-time status byte, as it does on the wire and in the framed sender's own feed.
        // Treated as still open, the tracker swallows every later byte for the rest of the connection, so every later
        // release waits out the mid-message bound instead of going at a boundary the cart is really at.
        Rig rig;
        rig.connectGame();
        rig.push({0xF0, 0x7E, 0x7F});  // a head whose tail is never coming
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 7E 7F") == 1; }));
        rig.push({0x90, 0x3C, 0x7F});  // a new status: it cuts the SysEx short and is a message of its own
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
        // A release now has nothing open to wait for, and goes at the next turn. Read as still inside a SysEx it goes
        // only when the bound runs out (a quarter of a second), which is what the short wait below tells apart.
        rig.cart.releaseNotes(heldNotes());
        CHECK(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }, 150));
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
    }
    SECTION("raw: running status: a data byte continues the last channel message") {
        // A message's status need not be repeated when the one before it is a channel message, and a bytes port may
        // hand over part of one: a data byte where a status would stand continues the last channel status. Read as
        // "no status", the tracker reports a boundary in the middle of that message, and a release written there is
        // its data.
        Rig rig;
        rig.connectGame();
        rig.push({0x90, 0x3C, 0x7F, 0x3E});  // a note-on, then the note and velocity of a running-status one
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F 3E") == 1; }));
        rig.cart.releaseNotes(heldNotes());
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:80 3C 00") == 0);  // held: the second message is not finished
        rig.push({0x7F});                             // the velocity that completes it
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
        CHECK(lastLogIndex(rig.dev, "fifo:7F") < lastLogIndex(rig.dev, "fifo:80 3C 00"));
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
    }
    SECTION("raw: running status a release interrupts: the stream's status goes out again before its next chunk") {
        // A release is the link's own message, so it leaves the cart's parser repeating the note-off's status (8n). The
        // bytes port may hand over a run of messages that repeat their status by running status, so the stream's next
        // chunk can be data bytes alone, and the cart (and the tracker, which follows the same rule) would read them
        // as note-offs of its own. The status the stream was running on is written again, alone, first: a status with
        // no data yet is no message, and both parsers read the chunk as the stream meant it.
        Rig rig;
        rig.connectGame();
        // Four programme changes, the last of them still running on C0 at the chunk's end (a message is a status and
        // its one data byte, so the eight bytes land on message boundaries and the ninth continues C0).
        rig.push({0xC0, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07});
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:C0 01 02 03 04 05 06 07") == 1; }));
        rig.cart.releaseNotes(heldNotes());
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));
        rig.push({0x08});  // the ninth message's own byte, under running status
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:08") == 1; }));
        CHECK(rig.dev.count("fifo:C0") == 2);  // the chunk's own run, and the one written back for it
        CHECK(lastLogIndex(rig.dev, "fifo:C0") > lastLogIndex(rig.dev, "fifo:80 3C 00"));  // after the releases
        CHECK(lastLogIndex(rig.dev, "fifo:C0") < lastLogIndex(rig.dev, "fifo:08"));
    }
    SECTION("raw: a SysEx cuts the message it interrupts, so a release need not wait out the bound") {
        // A status byte ends a message the parser was inside and starts its own, so an F0 behind half a note-on leaves
        // nothing open behind the SysEx: the note-on is gone from the wire. Counted as still owed once the F7 has
        // closed the SysEx, the tracker reports a message open for the rest of its bound and holds a release that the
        // cart would read as a message of its own at once.
        Rig rig;
        rig.connectGame();
        rig.push({0x90, 0x3C});  // half a note-on
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C") == 1; }));
        rig.push({0xF0, 0x7E, 0x7F, 0xF7});  // a SysEx, which cuts it short on the wire
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 7E 7F F7") == 1; }));
        rig.cart.releaseNotes(heldNotes());
        // The SysEx is over and nothing is open behind it, so the note-offs go at the next turn rather than when the
        // mid-message bound (250 ms) runs out: the short wait below is what tells those apart.
        CHECK(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }, 150));
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
        CHECK(lastLogIndex(rig.dev, "fifo:F0 7E 7F F7") < lastLogIndex(rig.dev, "fifo:80 3C 00"));
    }
    SECTION("framed: a release between a split SysEx's halves still delivers the whole SysEx") {
        Rig rig;
        rig.dev.menu    = false;
        rig.dev.linkRom = true;  // BlipToaster, which answers the probe
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        // The sender holds the first half until its F7 and has written nothing yet, which is exactly the window in
        // which the raw path's gate reads a boundary and a framed release used to be read INTO the held message: its
        // status cut the SysEx short, its bytes came out as spurious note-offs, and the tail was parsed as running
        // status. The release is framed whole instead, and the sender's own parser is left as it was.
        rig.push({0xF0, 0x7E, 0x7F});
        std::this_thread::sleep_for(std::chrono::milliseconds(30));  // the serial thread has fed the half to the sender
        rig.cart.releaseNotes(heldNotes());
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 2;  // the keys' note-offs, ahead of the SysEx they may not join
        }));
        rig.push({0x09, 0x01, 0xF7});  // its tail, which completes it
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 3;
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::lock_guard<std::mutex> l(rig.dev.mu);
        REQUIRE(rig.dev.applied.size() == 3);  // and nothing else was framed out of it
        CHECK(rig.dev.applied[0] == Bytes{0x80, 0x3C, 0x00});
        CHECK(rig.dev.applied[1] == Bytes{0x81, 0x43, 0x00});
        CHECK(rig.dev.applied[2] == Bytes{0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7});
        CHECK_FALSE(rig.dev.applied[2] == Bytes{0x80, 0x09, 0x01});  // no note-off read out of the tail
    }
    SECTION("framed: the reliable link delivers the same order") {
        Rig rig;
        rig.dev.menu    = false;
        rig.dev.linkRom = true;  // BlipToaster, which answers the probe
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        rig.push({0x90, 0x3C, 0x7F});
        rig.push({0x90, 0x43, 0x7F});
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 2;
        }));
        const std::uint64_t mark = rig.cart.pushMark();
        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 3;
        }));
        rig.cart.releaseNotes(heldNotes(), mark);
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 4;
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        std::lock_guard<std::mutex> l(rig.dev.mu);
        REQUIRE(rig.dev.applied.size() == 4);  // and the second note for key 60 never comes
        CHECK(rig.dev.applied[3] == Bytes{0x81, 0x43, 0x00});
        for (const Bytes& m : rig.dev.applied) {
            const bool cutsKey60 = (m[0] & 0xF0) == 0x80 && m[1] == 0x3C;
            CHECK_FALSE(cutsKey60);
        }
    }
}

TEST_CASE("a menu episode does not leave a release waiting behind the chunks it dropped", "[n8][cart-release]") {
    // The guard drops the ring at the menu, and a release waits until the serial thread has taken that many chunks off
    // it (chunksWritten_ reaching the push count it was asked at). A chunk the guard drops is a chunk gone, so the
    // count has to include it: otherwise every later release on this connection waits for chunks nobody will push - the
    // console holding a tone until the link ends - and every later handover reads the new node's note-on as older than
    // the mark it waits behind, which is the key-cutting bug this release exists to stop.
    SECTION("raw: the leaving node's last push never went, so its release waits for a count that includes it") {
        Rig rig;
        rig.connectGame();
        // Still in the ring when the console goes to its menu: the lookahead keeps it there, and the trip drops it.
        rig.host.link().setLookaheadMs(1000);
        rig.push({0x90, 0x3C, 0x7F});
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.menu = true;  // the menu, and the chunk in flight is dropped with the ring
        }
        REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
        REQUIRE(waitUntil([&] { return !rig.cart.connected(); }));
        // A game booted by hand from the cart's own browser, which is the state read back a game again.
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.menu   = false;
            rig.dev.cfg[0] = 69;
            rig.dev.cfg[2] = 0;
        }
        REQUIRE(waitUntil([&] { return rig.cart.connected(); }));
        rig.host.link().setLookaheadMs(0);
        rig.push({0x90, 0x3E, 0x7F});  // one chunk written after the trip, so the ring is empty again
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3E 7F") == 1; }));
        // The node that pushed it leaves with no successor (the setting off, the system removed): its release waits for
        // every push, and every push has gone, dropped or written, so the note-offs go.
        rig.cart.releaseNotes(heldNotes());
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
        CHECK(rig.dev.count("fifo:81 43 00") == 1);
    }
    SECTION("raw: a handover after the trip still keeps the new node's key") {
        Rig rig;
        rig.connectGame();
        rig.host.link().setLookaheadMs(1000);
        rig.push({0x90, 0x3C, 0x7F});  // the leaving node, key 60, still in the ring at the trip
        const std::uint64_t mark = rig.cart.pushMark();
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.menu = true;
        }
        REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.menu   = false;
            rig.dev.cfg[0] = 69;
            rig.dev.cfg[2] = 0;
        }
        REQUIRE(waitUntil([&] { return rig.cart.connected(); }));
        rig.host.link().setLookaheadMs(0);
        rig.push({0x90, 0x3C, 0x7F});  // the node taking over, on the key the old one held
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
        rig.cart.releaseNotes(heldNotes(), mark);
        REQUIRE(waitUntil([&] { return rig.dev.count("fifo:81 43 00") == 1; }));  // the old node's other key went
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(rig.dev.count("fifo:80 3C 00") == 0);  // and the new node's is still down
        CHECK(rig.cart.connected());
    }
}

TEST_CASE("a clock between a message's halves is not a message boundary", "[n8][cart-release]") {
    // A real-time byte (0xF8 and up) may stand between any two bytes of a message without ending it, so the tracker of
    // what the cart's parser is inside must step over one. Read as a status it would look like a boundary, and a
    // leaving node's note-off written there would land inside the message as its payload.
    Rig rig;
    rig.connectGame();
    rig.push({0x90, 0x3C});  // half a note-on: the parser is inside a message
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C") == 1; }));
    rig.push({0xF8});  // a clock
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F8") == 1; }));
    rig.cart.releaseNotes(heldNotes());
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(rig.dev.count("fifo:80 3C 00") == 0);  // held: the message is not finished
    rig.push({0x7F});                             // its tail, which completes it
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:80 3C 00") == 1; }));
    CHECK(rig.dev.count("fifo:81 43 00") == 1);
}

TEST_CASE("a seed menu read that timed out leaves nothing for the connect-time probe to read as the link's ack",
          "[n8][link]") {
    // Connect reads where the cart runs before the serial thread starts, with a short timeout: a read that times out
    // leaves the rest of its reply - here the whole block - in the input, and the probe would take a config block's
    // F5 <next> for the ack that says the ROM speaks the framed link. The stream would then be framed for a game that
    // reads no frames, and nothing sent to it would be played. The probe flushes what the port already holds first.
    FakeN8 dev;
    dev.menu    = false;  // a game, whose block a menu read reads
    dev.cfg[13] = 0xF5;   // and a block a probe can misread: F5 and a 7-bit byte, as an ack is
    dev.cfg[14] = 0x7F;
    std::atomic<int> stalled{0};
    N8Link           link([&dev, &stalled](const std::string&) -> std::unique_ptr<PortTransport> {
        return std::make_unique<LateCfgPort>(dev, stalled);
    });
    link.setMenuCheck([](Edio& edio) {
        std::uint8_t cfg[Edio::SIZE_CFG] = {0};
        if (!edio.memRD(Edio::ADDR_CFG, cfg, sizeof cfg).ok()) return -1;
        const int mapper = ((cfg[2] >> 4) << 8) | cfg[0];
        return mapper == 255 ? 1 : 0;  // the N8 OS's own mapper while its menu shows
    });
    REQUIRE(link.connect("/dev/fake"));
    CHECK(stalled.load() == 1);  // the seed read ran out its timeout with the block still unread
    std::this_thread::sleep_for(std::chrono::milliseconds(200));  // past the probe's own listen
    CHECK_FALSE(link.reliable());
    // And the stream is raw, which is what a game without the link reads: its note goes to the FIFO as it was pushed.
    const Bytes on{0x90, 0x3C, 0x7F};
    CHECK(link.push(0, on.data(), on.size(), 48000.0));
    REQUIRE(waitUntil([&] { return dev.count("fifo:90 3C 7F") == 1; }));
    CHECK(dev.frameSeqs.empty());  // no frame was ever delivered, which is what being framed would have made of it
}

TEST_CASE("a cart at its menu is not streamed to: its FIFO is a command parser there", "[n8][menu-guard]") {
    // The N8 menu reads the same FIFO a game reads MIDI from, so a data pair inside a message - 0x2A 0x73, say - is its
    // '*s': start an app. The guard asks the menu where the cart runs ('*t' in a SysEx, which only the menu answers),
    // and drops rather than streams while the answer says menu.
    Rig rig;
    rig.connect();  // at its menu: the fake's default
    // The connect-time link probe holds the serial thread for up to 100 ms, and a push waits in the ring behind it:
    // wait it out, so a failing run shows the cart reading the bytes rather than the test outrunning the link.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const Bytes on{0x90, 0x2A, 0x73};
    CHECK_FALSE(rig.cart.push(0, on.data(), on.size(), false, 48000.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(rig.dev.menu);  // nothing booted it
    CHECK(rig.dev.booted.empty());
    CHECK(rig.dev.count("fifo:90 2A 73") == 0);
    CHECK_FALSE(rig.cart.connected());  // attached, streaming nothing: what a cart node reads

    // A game booted by hand is streamed to, and reads connected again.
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = false;  // the user started a game from the cart's own browser
    }
    REQUIRE(waitUntil([&] { return rig.cart.connected(); }));
    rig.push({0x90, 0x2A, 0x73});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 2A 73") == 1; }));
    // And once the guard has read the menu during a stream, what is pushed goes nowhere and is counted: the cart reads
    // disconnected the moment the read lands, which is how this waits for it.
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;
    }
    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }));
    const std::uint64_t dropped = rig.cart.dropped();
    rig.push({0x90, 0x2A, 0x73});
    CHECK(rig.cart.dropped() == dropped + 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(rig.dev.count("fifo:90 2A 73") == 1);
}

TEST_CASE("a job submitted while the ring holds a push, right after the console resets to its menu, writes nothing "
          "into the menu",
          "[n8][menu-guard]") {
    // The job's own menu probe reads the config block, and that read is the guard's chance to see the reset: it must
    // trip before the pause delivers what the ring still holds, or the menu reads a note-on as its own commands.
    Rig rig;
    rig.connectGame();
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;  // the console's reset button, a moment before the submission
        rig.dev.files[kGameBattery] = saveOf(1024, 13);
    }
    // Still in the ring when the job is taken: a lookahead this long is what the pause's drain would deliver first.
    rig.host.link().setLookaheadMs(200);
    rig.push({0x91, 0x41, 0x7F});
    Job job;
    REQUIRE(rig.submit(battery("dump"), job));
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 13));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(rig.dev.count("fifo:91 41 7F") == 0);  // the ring it held went nowhere
}

TEST_CASE("leaving the menu re-probes the link, so a game the user starts by hand streams framed", "[n8][menu-guard]") {
    Rig rig;
    rig.dev.linkRom = true;  // BlipToaster, which answers the probe
    rig.connect();           // at its menu: the probe finds nothing, and the link stays raw
    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = false;  // the user started a game from the cart's own browser
        rig.dev.cfg[0] = 69;
        rig.dev.cfg[2] = 0;
    }
    // The guard's next probe goes unanswered, and leaving the menu runs the link probe again: the frames start.
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 1;
    }));
}

TEST_CASE("a job submitted under a stream into the menu is accepted, and the stream is dropped there",
          "[n8][menu-guard]") {
    // The design's starvation case: a framed link whose game reset to its menu keeps a stale window that is re-sent and
    // re-stamped on every turn, so at about 200 messages a second no turn was quiet for the probe. Once the guard has
    // the menu, nothing is framed, the window is reset and every turn is quiet.
    Rig rig;
    rig.dev.menu    = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0]  = 69;
    rig.dev.cfg[2]  = 0;
    rig.dev.files[kGameBattery] = saveOf(1024, 9);
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 1;
    }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menu = true;  // the console's reset button: the game is gone, and the link is still framed
    }
    std::atomic<bool> streaming{true};
    std::thread feeder([&] {
        while (streaming.load(std::memory_order_relaxed)) {
            rig.push({0x90, 0x3C, 0x7F});
            std::this_thread::sleep_for(std::chrono::milliseconds(5));  // about 200 messages a second
        }
    });
    Job job;
    const auto t0       = std::chrono::steady_clock::now();
    const bool accepted = rig.submit(battery("dump"), job);
    const auto waited =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    streaming.store(false, std::memory_order_relaxed);
    feeder.join();
    CHECK(accepted);
    CHECK(waited < 900);  // a turn was quiet for the read, rather than the probe waiting out its deadline
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 9));
    CHECK_FALSE(rig.cart.connected());  // the menu: attached, streaming nothing
}

TEST_CASE("a menu that never goes quiet is caught by its answer, and nothing is re-sent into it", "[n8][menu-guard]") {
    using Clock = std::chrono::steady_clock;
    Rig rig;
    rig.dev.menu    = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0]  = 69;
    rig.dev.cfg[2]  = 0;
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 1;
    }));
    // The console resets to its menu, and the menu chatters on the back-channel: the quiet rule stamps the last heard
    // on ANY byte, so no turn is ever quiet. The guard's probe needs none, and a chattered byte is no answer and no
    // ack.
    rig.dev.resetToMenu();
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.chatterMs = 40;
    }
    rig.push({0x90, 0x3E, 0x7F});  // an unacknowledged frame: this is what a re-send would take into the menu
    // A second message pushed while the hold waits for the menu's answer, a lookahead out so it is still in the ring
    // (or in hand) at the trip: the trip drops it, and nothing may write it afterwards. It is what the FIFO count below
    // is for - a frame the fake's menu branch never reads cannot show it.
    rig.host.link().setLookaheadMs(1100);
    rig.push({0x90, 0x41, 0x7F});
    rig.host.link().setLookaheadMs(0);
    const auto t0 = Clock::now();
    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 4000));  // the trip, which is the menu's 'k' heard
    const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    CHECK(waited < 900);  // as soon as the window stopped (the link's 100 ms timeout), not after a quiet turn
    // The frame was held, not re-sent, while the probe waited: the menu would have read a re-send as a command.
    CHECK(rig.host.link().linkResends() == 0);
    // And nothing of the stream goes to the cart's FIFO after the trip. The fake's frameSeqs cannot say this (its menu
    // branch returns before it reads a frame), so this counts what the host WROTE, which a re-send would add to; the
    // guard's own probes go on at the menu, and are not the stream's.
    const auto streamed = [&] { return rig.dev.count("fifo:") - rig.dev.count("fifo:F0 2A 74 F7"); };
    const std::size_t writes = streamed();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(streamed() == writes);  // nothing more was written after the trip
    CHECK(rig.dev.menu);
    CHECK(rig.dev.booted.empty());
}

TEST_CASE("a menu whose chatter lands ahead of its answers is read as a menu, not as a game", "[n8][menu-guard]") {
    // A cart that talks while the host is not reading leaves bytes in the input, ahead of the menu's 'k'. Taken for the
    // answer, a status byte there would read as a game and leave the guard streaming into the menu: the answer is a
    // 'k' wherever it lands among them.
    Rig rig;
    rig.dev.chatterMs      = 40;  // one byte whenever the cart owes the host no reply
    rig.dev.chatterGarbles = true;  // ... left in the input for the next read, as one said between reads is
    rig.connect();                  // at its menu: the fake's default, so the connect-time read seeds "menu"
    REQUIRE(rig.host.link().atMenu());
    rig.push({0x90, 0x2A, 0x73});  // '*s' if the menu ever read it
    std::this_thread::sleep_for(std::chrono::milliseconds(600));  // several read cadences
    CHECK(rig.host.link().atMenu());
    CHECK_FALSE(rig.cart.connected());
    CHECK(rig.dev.menu);
    CHECK(rig.dev.booted.empty());
    CHECK(rig.dev.count("fifo:90 2A 73") == 0);
}

TEST_CASE("a framed stream to a menu the console's reset reached trips the guard, and nothing is written after",
          "[n8][menu-guard]") {
    // What the real cart did (docs/n8-menu-guard-reset-design.md section 1): RESET drops BlipToaster to the menu with
    // USB and the framed link still up, and the config block keeps the game's mapper. The acks stop at once, and only
    // the menu's own answer tells it from a game that has merely gone quiet.
    Rig rig;
    rig.dev.menu    = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0]  = 69;  // BlipToaster's mapper, which the reset leaves in the block
    rig.dev.cfg[2]  = 0;
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 1;
    }));
    rig.dev.resetToMenu();
    rig.push({0x90, 0x3E, 0x7F});  // a frame the menu never acknowledges: the window stops moving
    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 2000));
    CHECK(rig.host.link().atMenu());
    CHECK(rig.host.link().linkOutstanding() == 0);  // the stale window went with the trip
    // Nothing of the stream goes to the cart's FIFO after the trip; the guard's own probe still does, which is how it
    // sees a game the user starts from the menu.
    const auto streamed = [&] { return rig.dev.count("fifo:") - rig.dev.count("fifo:F0 2A 74 F7"); };
    const std::size_t before = streamed();
    rig.push({0x90, 0x41, 0x7F});
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(streamed() == before);
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.menu);
    CHECK_FALSE(rig.dev.error40);
}

TEST_CASE("a raw stream to a menu the console's reset reached trips the guard within a probe's cadence",
          "[n8][menu-guard]") {
    // Raw MIDI has no acks to stop, so nothing the cart says on its own marks the reset: the guard has to ask.
    using Clock = std::chrono::steady_clock;
    Rig rig;
    rig.connectGame();  // a game whose block names mapper 0, which the reset leaves there
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 3C 7F") == 1; }));
    rig.dev.resetToMenu();
    const auto t0 = Clock::now();
    REQUIRE(waitUntil([&] { return !rig.cart.connected(); }, 2000));
    // One cadence is 250 ms; the rest is room for a loaded runner.
    CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 600);
    CHECK(rig.host.link().atMenu());
    rig.push({0x90, 0x2A, 0x73});  // '*s' if the menu ever read it
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(rig.dev.count("fifo:90 2A 73") == 0);
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK(rig.dev.menu);
    CHECK(rig.dev.booted.empty());
    CHECK_FALSE(rig.dev.error40);
}

TEST_CASE("a framed stream of '*'-bearing messages after a reset never drives the menu to ERROR:40",
          "[n8][menu-guard]") {
    // ERROR:40 is a '*' followed by a byte that is not one of the menu's commands, which a framed stream carries sooner
    // or later (0x2A 0x40, '*@', in every message here); on the real cart the reset stream reached it within 21 s, and
    // the OS stopped there. Once the guard has tripped no byte of the stream reaches the menu, so it never sees one.
    Rig rig;
    rig.dev.menu    = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0]  = 69;
    rig.dev.cfg[2]  = 0;
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 1;
    }));
    rig.dev.resetToMenu();
    // The frame that stops the window carries no '*': what is written before the guard can know is the exposure the
    // design leaves (section 4), and this case is about what comes after.
    rig.push({0x90, 0x3E, 0x7F});
    // Time for the guard to see the window stop and ask; the checks below say whether it did.
    (void)waitUntil([&] { return rig.host.link().atMenu(); }, 1000);
    for (int i = 0; i < 100; ++i) {
        rig.push({0x90, 0x2A, 0x40});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    CHECK(rig.host.link().atMenu());
    std::lock_guard<std::mutex> l(rig.dev.mu);
    CHECK_FALSE(rig.dev.error40);
    CHECK(rig.dev.menu);
}

TEST_CASE("a job submitted at a menu the console's reset reached is taken", "[n8][menu-guard][!shouldfail]") {
    // The block keeps the game's mapper after a reset, so a check that reads it refuses the job as "a game runs" at the
    // one place a battery file can be reached.
    Rig rig;
    rig.dev.menu   = false;
    rig.dev.cfg[0] = 69;
    rig.dev.cfg[2] = 0;
    rig.dev.files[kGameBattery] = saveOf(1024, 21);
    rig.connect();
    rig.dev.resetToMenu();
    Job job;
    std::string why;
    const bool taken = rig.submit(battery("dump"), job, &why);
    CHECK(why == "");
    REQUIRE(taken);
    REQUIRE(waitUntil([&] { return job.finished(); }));
    CHECK(job.state.load() == Job::Done);
    CHECK(job.result == saveOf(1024, 21));
}

TEST_CASE("a 'k' inside the reliable link's acks is not the menu's answer", "[n8][menu-guard]") {
    // The menu answers the guard's probe with a 'k' (0x6B), and an ack is F5 <next> <last> with both 7-bit, so a 'k'
    // can stand inside one. Only a 'k' outside an ack is the menu's, and a game whose acks carry one keeps its stream.
    SECTION("as <next> and <last> of a stream's acks") {
        Rig rig;
        rig.dev.menu    = false;
        rig.dev.linkRom = true;
        rig.dev.cfg[0]  = 69;
        rig.dev.cfg[2]  = 0;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        // Frames 0 to 109: the acks of frames 0x6A and 0x6B name 0x6B as <next> and as <last>.
        for (int i = 0; i < 110; ++i) rig.push({0x90, static_cast<std::uint8_t>(i & 0x7F), 0x40});
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 110;
        }));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK_FALSE(rig.host.link().atMenu());
        CHECK(rig.cart.connected());
    }
    SECTION("as <last> of the sync's ack, which the link probe leaves for the stream's first read") {
        // The probe goes on as soon as it has read F5 <next>: a port that hands the ack over a byte at a time leaves
        // its <last> in the input.
        FakeN8 dev;
        dev.menu        = false;
        dev.linkRom     = true;
        dev.syncAckLast = 0x6B;
        N8Link link([&dev](const std::string&) -> std::unique_ptr<PortTransport> {
            return std::make_unique<ByteAtATimePort>(dev);
        });
        link.setMenuCheck([](Edio&) { return 0; });  // the guard on, and a game at connect
        REQUIRE(link.connect("/dev/fake"));
        REQUIRE(waitUntil([&] { return link.reliable(); }));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        CHECK_FALSE(link.atMenu());
        const Bytes on{0x90, 0x3C, 0x7F};
        CHECK(link.push(0, on.data(), on.size(), 48000.0));
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(dev.mu);
            return dev.applied.size() == 1;
        }));
    }
}

TEST_CASE("a menu whose answer comes late is still the menu: one unanswered probe does not leave it",
          "[n8][menu-guard]") {
    // The lab's menu answers '*t' in about 20 ms, well inside the probe's 100 ms, but a menu busy with a slow turn can
    // miss that window now and then. Leaving the menu on one miss would stream into it until the next probe tripped the
    // guard again: it takes three misses in a row.
    Rig rig;
    rig.connectGame();
    rig.dev.resetToMenu();
    REQUIRE(waitUntil([&] { return rig.host.link().atMenu(); }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.menuAnswerDelayMs = 150;  // every answer from here on comes after the probe's window
    }
    const auto streamed = [&] { return rig.dev.count("fifo:") - rig.dev.count("fifo:F0 2A 74 F7"); };
    const std::size_t before = streamed();
    bool left = false;
    for (int i = 0; i < 300 && !left; ++i) {  // 1.5 s, six probes, each answered late
        left = !rig.host.link().atMenu();
        rig.push({0x90, 0x3C, 0x7F});
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK_FALSE(left);
    CHECK(streamed() == before);  // no link probe (F4 00) and no note went to the menu
}

TEST_CASE("a framed link whose cart answers neither the menu's probe nor the link is streamed raw again, not held",
          "[n8][menu-guard]") {
    // A framed link asks only in a stall, so a RESET between songs and a game without the link started from the cart's
    // own browser go unseen: the next frame stalls, and neither a 'k' nor an ack ever comes. Two stall probes that hear
    // nothing (two seconds) give the window up and probe the link again, which that game does not answer.
    Rig rig;
    rig.dev.menu    = false;
    rig.dev.linkRom = true;
    rig.dev.cfg[0]  = 69;
    rig.dev.cfg[2]  = 0;
    rig.connect();
    REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
    rig.push({0x90, 0x3C, 0x7F});
    REQUIRE(waitUntil([&] {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        return rig.dev.applied.size() == 1;
    }));
    {
        std::lock_guard<std::mutex> l(rig.dev.mu);
        rig.dev.linkRom = false;  // the game running now reads plain MIDI and acknowledges nothing
    }
    rig.push({0x90, 0x3E, 0x7F});  // the frame that stalls
    REQUIRE(waitUntil([&] { return !rig.host.link().reliable(); }, 4000));
    CHECK(rig.cart.connected());
    CHECK_FALSE(rig.host.link().atMenu());
    rig.push({0x90, 0x41, 0x7F});
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:90 41 7F") == 1; }));  // raw, as that game reads it
}

TEST_CASE("the raw probe waits for a message to end, however long it stays open", "[n8][menu-guard]") {
    // The probe is a SysEx of its own, so inside another message it would cut that one short. A node's bytes port can
    // leave a SysEx open for a while; a release goes anyway after 250 ms (a held note is the worse harm), but a probe
    // that waits costs nothing.
    Rig rig;
    rig.connectGame();
    rig.push({0xF0, 0x7E, 0x7F});  // a SysEx head whose tail comes late
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 7E 7F") == 1; }));
    const std::size_t probes = rig.dev.count("fifo:F0 2A 74 F7");
    std::this_thread::sleep_for(std::chrono::milliseconds(700));  // past the release's bound, and two cadences
    CHECK(rig.dev.count("fifo:F0 2A 74 F7") == probes);
    rig.push({0x09, 0x01, 0xF7});  // the tail
    REQUIRE(waitUntil([&] { return rig.dev.count("fifo:F0 2A 74 F7") > probes; }, 1000));
    CHECK(lastLogIndex(rig.dev, "fifo:09 01 F7") < lastLogIndex(rig.dev, "fifo:F0 2A 74 F7"));
}

TEST_CASE("a framed game that is slow or damaged resumes on its ack, and is asked at most once a second",
          "[n8][menu-guard]") {
    // In a stall the guard holds every re-send, so the probe's own bytes are all a live BlipToaster reads: it drops
    // them as unframed and acknowledges F5 <next> <next> (bliptoaster src/midi/parse_asm.s), and that ack is what
    // resumes the stream. Neither game may trip the guard.
    SECTION("acks that take longer than the link's timeout") {
        Rig rig;
        rig.dev.menu    = false;
        rig.dev.linkRom = true;
        rig.dev.cfg[0]  = 69;
        rig.dev.cfg[2]  = 0;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.linkAckDelayMs = 150;  // from here on: the link probe's own ack must come inside its 100 ms
        }
        bool tripped = false;
        for (int i = 0; i < 20; ++i) {
            rig.push({0x90, static_cast<std::uint8_t>(0x30 + i), 0x7F});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            tripped = tripped || rig.host.link().atMenu();
        }
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 20;
        }));
        CHECK_FALSE(tripped);
        CHECK_FALSE(rig.host.link().atMenu());
        CHECK(rig.dev.count("fifo:F0 2A 74 F7") >= 1);  // the last frame's ack came after the window had stopped
    }
    SECTION("every frame reported damaged") {
        Rig rig;
        rig.dev.menu    = false;
        rig.dev.linkRom = true;
        rig.dev.cfg[0]  = 69;
        rig.dev.cfg[2]  = 0;
        rig.connect();
        REQUIRE(waitUntil([&] { return rig.host.link().reliable(); }));
        rig.push({0x90, 0x3C, 0x7F});
        REQUIRE(waitUntil([&] {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            return rig.dev.applied.size() == 1;
        }));
        {
            std::lock_guard<std::mutex> l(rig.dev.mu);
            rig.dev.linkDamaged = true;
        }
        rig.push({0x90, 0x3E, 0x7F});
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        const std::uint64_t resends = rig.host.link().linkResends();
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        CHECK(rig.host.link().linkResends() > resends);  // the re-sends go on: the stall resumed on the probe's ack
        CHECK_FALSE(rig.host.link().atMenu());
        CHECK(rig.cart.connected());
        CHECK(rig.dev.count("fifo:F0 2A 74 F7") <= 3);  // a probe a second, not one a re-send
    }
}

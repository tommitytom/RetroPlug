// Guards the N8 SD / menu control ops (Settings > N8 Pro) without hardware. Two layers:
//   - N8SdWorker in isolation: the single-in-flight threading (busy / reject-while-busy / done / progress /
//     error), with trivial jobs that never touch serial.
//   - N8Host driving the load over a scripted fake ISerialPort that IGNORES writes and serves the exact reply bytes
//     each Edio op reads (traced from Edio.cpp), the connection manager that pauses N8Link streaming around a control
//     op and resumes it after, and the battery transfers it refuses (the capability jobs reach the battery file).
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "host/n8/Edio.hpp"  // ISerialPort
#include "host/n8/N8Host.hpp"
#include "host/n8/N8SdWorker.hpp"

using retroplug::ISerialPort;
using retroplug::N8Host;
using retroplug::N8Link;
using retroplug::N8PortDto;
using retroplug::N8SdWorker;

namespace {

// A serial port that ignores writes and pops pre-scripted reply bytes off `toRead`. An empty queue returns
// 0 (Edio then throws a read timeout) - so a mis-counted script surfaces as a job error, not a hang.
struct ScriptedFake : ISerialPort {
    std::deque<std::uint8_t> toRead;
    std::size_t write(const std::uint8_t*, std::size_t n) override { return n; }
    std::size_t read(std::uint8_t* b, std::size_t n, int) override {
        std::size_t i = 0;
        while (i < n && !toRead.empty()) { b[i++] = toRead.front(); toRead.pop_front(); }
        return i;
    }
    void flushInput() override {}
};

// A factory that dispenses pre-built read-queues in call order (each factory() call -> the next queue). Lets
// a test spell out exactly what each serial session (setup-connect / control-op / reconnect) should serve.
N8Link::PortFactory scriptedFactory(std::shared_ptr<std::deque<std::deque<std::uint8_t>>> queues) {
    return [queues](const std::string&) -> std::unique_ptr<ISerialPort> {
        auto f = std::make_unique<ScriptedFake>();
        if (!queues->empty()) { f->toRead = std::move(queues->front()); queues->pop_front(); }
        return f;
    };
}

N8Host::PortLister listerWith(std::vector<N8PortDto> ports) {
    return [ports]() { return ports; };
}

std::string tempDir() {
    const auto dir = std::filesystem::temp_directory_path() / "rp-n8sd-test";
    std::filesystem::create_directories(dir);
    return dir.string();
}

// Poll the SD status until the job finishes (done) or the timeout elapses.
bool waitDone(N8Host& host, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (host.sdStatus().done) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return host.sdStatus().done;
}

const std::deque<std::uint8_t> HANDSHAKE = {0x00, 0xA5};  // getStatus() OK reply (low byte 0, 0xA5 high)

std::deque<std::uint8_t> handshakePlus(const std::vector<std::uint8_t>& more) {
    std::deque<std::uint8_t> q = HANDSHAKE;
    for (std::uint8_t b : more) q.push_back(b);
    return q;
}

void writeFile(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

}  // namespace

TEST_CASE("N8SdWorker runs one job, exposes progress, and rejects a second while busy", "[n8sd]") {
    N8SdWorker w;
    std::atomic<bool> release{false};

    REQUIRE(w.start("dump", [&](N8SdWorker::Progress& p) {
        p.phase("Working");
        p.total(100);
        p.advance(40);
        // hold the job open so the test can observe busy + a rejected second start
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!release.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        p.result("ok");
    }));

    REQUIRE(w.busy());
    REQUIRE_FALSE(w.start("dump", [](N8SdWorker::Progress&) {}));  // one in-flight job only

    release.store(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (w.busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

    const auto s = w.status();
    REQUIRE(s.done);
    REQUIRE_FALSE(s.busy);
    REQUIRE(s.error.empty());
    REQUIRE(s.result == "ok");
    REQUIRE(s.op == "dump");
}

TEST_CASE("N8SdWorker records a thrown job as an error", "[n8sd]") {
    N8SdWorker w;
    REQUIRE(w.start("load", [](N8SdWorker::Progress&) { throw std::runtime_error("boom"); }));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (w.busy() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto s = w.status();
    REQUIRE(s.done);
    REQUIRE(s.error == "boom");
    REQUIRE(s.result.empty());
}

TEST_CASE("N8Host.startDumpSram and startRestoreSram refuse: the cart's own battery is never transferred over USB",
          "[n8sd]") {
    // No serial session at all: the refusal comes through the worker's error channel before any port is opened.
    auto opens = std::make_shared<int>(0);
    N8Host host([opens](const std::string&) -> std::unique_ptr<ISerialPort> {
                    ++*opens;
                    return std::make_unique<ScriptedFake>();
                },
                listerWith({{"/dev/fake", true}}), tempDir());
    host.setPort("/dev/fake");
    const std::string srm = (std::filesystem::path(tempDir()) / "refused.srm").string();
    writeFile(srm, std::vector<std::uint8_t>(256, 0x11));

    for (const char* op : {"dump", "restore"}) {
        if (std::string(op) == "dump") host.startDumpSram((std::filesystem::path(tempDir()) / "dump.srm").string());
        else host.startRestoreSram(srm);
        REQUIRE(waitDone(host));
        const auto s = host.sdStatus();
        CHECK(s.op == op);
        CHECK(s.error.rfind("the battery file can only be reached at the N8 menu", 0) == 0);
        CHECK(s.error.find("rp.system.sram/1") != std::string::npos);
        CHECK(s.result.empty());
    }
    CHECK(*opens == 0);
}

TEST_CASE("N8Host.startLoadRom drives the menu and stops streaming (link + enabled)", "[n8sd]") {
    // Two sessions in call order: the streaming connect, then the load control op (the exact read sequence for
    // a small ROM, see Edio.cpp: connect, '*t'->'k', fileOpen status, fileWrite ack + status, fileClose status,
    // appInstall status + map index 5). No third session: a load doesn't reconnect (the old stream is stale).
    auto queues = std::make_shared<std::deque<std::deque<std::uint8_t>>>();
    queues->push_back(HANDSHAKE);  // connect(true): start streaming first, so the load has a live link to stop
    queues->push_back(handshakePlus({
        0x6b,             // menu.test '*t' -> 'k'
        0x00, 0xA5,       // fileOpen checkStatus
        0x00,             // fileWrite txDataACK block ack
        0x00, 0xA5,       // fileWrite checkStatus
        0x00, 0xA5,       // fileClose checkStatus
        0x00,             // appInstall status
        0x05, 0x00,       // appInstall map index = 5
    }));

    N8Host host(scriptedFactory(queues), listerWith({{"/dev/fake", true}}), tempDir());
    host.setPort("/dev/fake");
    host.connect(true);  // streaming on: enabled + connected
    REQUIRE(host.getConfig().connected);
    REQUIRE(host.getConfig().enabled);
    const std::string rom = (std::filesystem::path(tempDir()) / "test.nes").string();
    writeFile(rom, std::vector<std::uint8_t>(100, 0x11));

    host.startLoadRom(rom);
    REQUIRE(waitDone(host));
    const auto s = host.sdStatus();
    REQUIRE(s.error.empty());
    REQUIRE(s.op == "load");
    REQUIRE(s.result.find("map 5") != std::string::npos);
    // A load boots a new ROM, so the old stream is stale: the link stays down AND the enabled toggle clears (so
    // the Status row reads "Off", not a forever "Connecting..."). Both are persisted to n8.cfg.
    REQUIRE_FALSE(host.getConfig().connected);
    REQUIRE_FALSE(host.getConfig().enabled);
}

TEST_CASE("N8Host pauses streaming for an SD op and resumes it after", "[n8sd]") {
    // Three serial sessions in call order: setup connect, the load's control op, the reconnect. A load whose ROM file
    // cannot be read fails inside the op, after the borrow, and a failed op leaves the link streaming as it found it.
    auto queues = std::make_shared<std::deque<std::deque<std::uint8_t>>>();
    queues->push_back(HANDSHAKE);  // connect(true)
    queues->push_back(HANDSHAKE);  // the load's control op
    queues->push_back(HANDSHAKE);  // reconnect

    N8Host host(scriptedFactory(queues), listerWith({{"/dev/fake", true}}), tempDir());
    host.connect(true);  // start streaming (session 1)
    REQUIRE(host.getConfig().connected);

    host.startLoadRom((std::filesystem::path(tempDir()) / "no-such.nes").string());
    REQUIRE(waitDone(host));
    REQUIRE(host.sdStatus().error.find("cannot open file") != std::string::npos);
    REQUIRE(host.getConfig().connected);  // streaming resumed after the borrow (session 3)
    REQUIRE(queues->empty());
}

// Guards the N8 SD / menu control ops (Settings > N8 Pro) without hardware. Two layers:
//   - N8SdWorker in isolation: the single-in-flight threading (busy / reject-while-busy / done / progress /
//     error), with trivial jobs that never touch serial.
//   - N8Host driving real ops (dumpSram / loadRom / restoreSram) over a scripted fake ISerialPort that
//     IGNORES writes and serves the exact reply bytes each Edio op reads (traced from Edio.cpp), plus the
//     connection manager that pauses N8Link streaming around a control op and resumes it after.
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
std::vector<std::uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
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

TEST_CASE("N8Host.startDumpSram reads 64 KB and writes the file", "[n8sd]") {
    // One serial session (no streaming to pause): handshake, then 64 KB of a known pattern for the memRD reads.
    std::vector<std::uint8_t> pattern(retroplug::Edio::SIZE_SRM_GAME);
    for (std::size_t i = 0; i < pattern.size(); i++) pattern[i] = static_cast<std::uint8_t>(i & 0xFF);
    auto queues = std::make_shared<std::deque<std::deque<std::uint8_t>>>();
    queues->push_back(handshakePlus(std::vector<std::uint8_t>(pattern.begin(), pattern.end())));

    N8Host host(scriptedFactory(queues), listerWith({{"/dev/fake", true}}), tempDir());
    host.setPort("/dev/fake");
    const std::string dest = (std::filesystem::path(tempDir()) / "dump.srm").string();

    host.startDumpSram(dest);
    REQUIRE(waitDone(host));
    const auto s = host.sdStatus();
    REQUIRE(s.error.empty());
    REQUIRE(s.op == "dump");
    REQUIRE(s.bytesDone == retroplug::Edio::SIZE_SRM_GAME);

    const auto out = readFile(dest);
    REQUIRE(out.size() == pattern.size());
    REQUIRE(out == pattern);
}

TEST_CASE("N8Host.startLoadRom drives the menu and leaves streaming stopped", "[n8sd]") {
    // The exact read sequence for a small ROM (see Edio.cpp): connect, '*t'->'k', fileOpen status,
    // fileWrite ack + status, fileClose status, appInstall status + map index (5).
    auto queues = std::make_shared<std::deque<std::deque<std::uint8_t>>>();
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
    const std::string rom = (std::filesystem::path(tempDir()) / "test.nes").string();
    writeFile(rom, std::vector<std::uint8_t>(100, 0x11));

    host.startLoadRom(rom);
    REQUIRE(waitDone(host));
    const auto s = host.sdStatus();
    REQUIRE(s.error.empty());
    REQUIRE(s.op == "load");
    REQUIRE(s.result.find("map 5") != std::string::npos);
    REQUIRE_FALSE(host.getConfig().connected);  // a load boots a new ROM -> streaming stays stopped
}

TEST_CASE("N8Host.startRestoreSram writes then verifies the readback", "[n8sd]") {
    std::vector<std::uint8_t> srm(256);
    for (std::size_t i = 0; i < srm.size(); i++) srm[i] = static_cast<std::uint8_t>((i * 7) & 0xFF);
    // handshake, then the verify memRD returns exactly what was written (so the readback matches).
    auto queues = std::make_shared<std::deque<std::deque<std::uint8_t>>>();
    queues->push_back(handshakePlus(std::vector<std::uint8_t>(srm.begin(), srm.end())));

    N8Host host(scriptedFactory(queues), listerWith({{"/dev/fake", true}}), tempDir());
    host.setPort("/dev/fake");
    const std::string path = (std::filesystem::path(tempDir()) / "restore.srm").string();
    writeFile(path, srm);

    host.startRestoreSram(path);
    REQUIRE(waitDone(host));
    const auto s = host.sdStatus();
    REQUIRE(s.error.empty());
    REQUIRE(s.op == "restore");
    REQUIRE(s.result == "Restored 256 bytes");
}

TEST_CASE("N8Host pauses streaming for an SD op and resumes it after", "[n8sd]") {
    std::vector<std::uint8_t> pattern(retroplug::Edio::SIZE_SRM_GAME, 0x00);
    // Three serial sessions in call order: setup connect, the dump control op, the reconnect.
    auto queues = std::make_shared<std::deque<std::deque<std::uint8_t>>>();
    queues->push_back(HANDSHAKE);                                                                  // connect(true)
    queues->push_back(handshakePlus(std::vector<std::uint8_t>(pattern.begin(), pattern.end())));   // dump
    queues->push_back(HANDSHAKE);                                                                  // reconnect

    N8Host host(scriptedFactory(queues), listerWith({{"/dev/fake", true}}), tempDir());
    host.connect(true);  // start streaming (session 1)
    REQUIRE(host.getConfig().connected);

    const std::string dest = (std::filesystem::path(tempDir()) / "paused.srm").string();
    host.startDumpSram(dest);
    REQUIRE(waitDone(host));
    REQUIRE(host.sdStatus().error.empty());
    REQUIRE(host.getConfig().connected);  // streaming resumed after the borrow (session 3)
}
